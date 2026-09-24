# tools/cleanup.ps1 - frees disk space: stale agent screenshots, scratch files, old test runs, old Unreal logs.
# DRY RUN by default: lists what it would delete (with sizes) and writes the list to Saved/AgentLogs/janitor/.
# -Apply actually deletes. Run by the janitor agent (see .claude/agents/janitor.md) or by Jimmy.
#
# Safety rules (the script enforces all of them):
# - Only touches output folders that git ignores, in the main checkout and every worktree lane:
#   Saved/AgentLogs, Saved/Logs, Saved/Crashes, plus the main checkout's Progress/ (only files named in -Paths).
# - Scratch = Saved/AgentLogs/scratch/* and experiment renders Saved/AgentLogs/previews/**/exp_*.
# - Skips anything modified in the last -MinAgeMinutes (a running agent may still use it).
# - Keeps the newest -KeepRuns run folders per area and every folder an OPEN task line in docs/ still references
#   (lines of done tasks, "- [x] ...", don't count).
# - Never touches Content/, Source/, art/, data/, docs/, Config/ or anything else in the repo.
param(
    [switch]$Apply,
    [string[]]$Paths = @(),   # extra files/folders to delete, e.g. superseded Progress images; must be under an allowed root
    [int]$KeepRuns = 3,       # newest run folders kept per area (playtest, editor, tests, anim, crashes)
    [int]$KeepLogs = 3,       # newest Unreal log files kept per checkout
    [int]$MinAgeMinutes = 60
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'cleanup'
$root = Get-RepoRoot
$cutoff = (Get-Date).AddMinutes(-$MinAgeMinutes)

# All checkouts: main + worktree lanes.
$checkouts = @(git -C $root worktree list --porcelain | Where-Object { $_ -like 'worktree *' } | ForEach-Object { $_.Substring(9).Replace('/', '\') })
if ($checkouts.Count -eq 0) { $checkouts = @($root) }

# Folders still referenced by open (not done) lines in docs/.
$referenced = @{}
Get-ChildItem (Join-Path $root 'docs') -Recurse -Filter *.md | ForEach-Object {
    foreach ($line in (Get-Content $_.FullName)) {
        if ($line -match '^\s*- \[x\]') { continue }
        foreach ($m in [regex]::Matches($line, 'AgentLogs[\\/]([A-Za-z0-9_-]+)[\\/]([A-Za-z0-9_.-]+)')) {
            $referenced[($m.Groups[1].Value + '/' + $m.Groups[2].Value).ToLower()] = $true
        }
    }
}

$candidates = New-Object System.Collections.ArrayList
function Add-Candidate($Item, [string]$Reason) {
    if ($Item.LastWriteTime -gt $cutoff) { return }
    if ($Item.PSIsContainer) {
        $newest = Get-ChildItem $Item.FullName -Recurse -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($newest -and $newest.LastWriteTime -gt $cutoff) { return }
        $bytes = (Get-ChildItem $Item.FullName -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    } else { $bytes = $Item.Length }
    [void]$candidates.Add([pscustomobject]@{ Path = $Item.FullName; MB = [math]::Round(([double]$bytes) / 1MB, 1); Reason = $Reason })
}

foreach ($co in $checkouts) {
    $logs = Join-Path $co 'Saved\AgentLogs'
    # Scratch: temporary by definition.
    $scratch = Join-Path $logs 'scratch'
    if (Test-Path $scratch) { Get-ChildItem $scratch | ForEach-Object { Add-Candidate $_ 'scratch' } }
    # Experiment renders: previews named exp_* (any depth) are scratch too; the kept previews never use that prefix.
    $previews = Join-Path $logs 'previews'
    if (Test-Path $previews) { Get-ChildItem $previews -Recurse -Filter 'exp_*' | ForEach-Object { Add-Candidate $_ 'experiment render' } }
    # Run folders: keep the newest $KeepRuns per area and anything an open task references.
    foreach ($area in @('playtest', 'editor', 'tests', 'anim')) {
        $dir = Join-Path $logs $area
        if (-not (Test-Path $dir)) { continue }
        Get-ChildItem $dir -Directory | Sort-Object LastWriteTime -Descending | Select-Object -Skip $KeepRuns | ForEach-Object {
            if (-not $referenced.ContainsKey(($area + '/' + $_.Name).ToLower())) { Add-Candidate $_ ('old ' + $area + ' run') }
        }
    }
    # Unreal logs and crash folders.
    $ueLogs = Join-Path $co 'Saved\Logs'
    if (Test-Path $ueLogs) { Get-ChildItem $ueLogs -File | Sort-Object LastWriteTime -Descending | Select-Object -Skip $KeepLogs | ForEach-Object { Add-Candidate $_ 'old Unreal log' } }
    $crashes = Join-Path $co 'Saved\Crashes'
    if (Test-Path $crashes) { Get-ChildItem $crashes -Directory | Sort-Object LastWriteTime -Descending | Select-Object -Skip $KeepRuns | ForEach-Object { Add-Candidate $_ 'old crash report' } }
}

# Explicit paths: only under Progress/ or a checkout's Saved/AgentLogs.
$allowedRoots = @((Join-Path $root 'Progress')) + @($checkouts | ForEach-Object { Join-Path $_ 'Saved\AgentLogs' })
$Paths = @($Paths | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })  # -File passes a comma list as one string
foreach ($p in $Paths) {
    $full = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $root $p }))
    $ok = $false
    foreach ($a in $allowedRoots) { if ($full.StartsWith($a + '\', [StringComparison]::OrdinalIgnoreCase)) { $ok = $true } }
    if (-not $ok) { Write-Host ('REFUSED (outside allowed roots): ' + $full); continue }
    if (-not (Test-Path $full)) { Write-Host ('missing, skipped: ' + $full); continue }
    Add-Candidate (Get-Item $full) 'named by janitor'
}

$total = [math]::Round(($candidates | Measure-Object MB -Sum).Sum, 1)
$list = ($candidates | Sort-Object MB -Descending | ForEach-Object { '{0,8} MB  {1}  ({2})' -f $_.MB, $_.Path, $_.Reason }) -join "`r`n"
$manifest = Join-Path (Get-LogDir 'janitor') ((Get-Timestamp) + $(if ($Apply) { '-deleted.txt' } else { '-dryrun.txt' }))
Write-TextFile $manifest ($list + "`r`nTotal: $total MB in $($candidates.Count) items`r`n")
Write-Host $list

if (-not $Apply) {
    Write-Status -Name $name -State 'succeeded' -Message "DRY RUN: would free $total MB in $($candidates.Count) items (list: $manifest). Re-run with -Apply to delete." -LogPath $manifest
    exit 0
}
$failed = 0
foreach ($c in $candidates) {
    try { Remove-Item -LiteralPath $c.Path -Recurse -Force -ErrorAction Stop } catch { $failed++; Write-Host ('could not delete: ' + $c.Path + ' - ' + $_.Exception.Message) }
}
Write-Status -Name $name -State $(if ($failed) { 'failed' } else { 'succeeded' }) -Message "Deleted $($candidates.Count - $failed) items, freed about $total MB ($failed failed). List: $manifest" -LogPath $manifest
exit $(if ($failed) { 1 } else { 0 })

# tools/integrate.ps1 - the lead's one-step lane integration (P-003a).
# Usage: tools/integrate.ps1 -Lanes eng2,eng3 [-Batch eng2] [-DryRun] [-MaxRounds 2]
#   1. Batch lane = -Batch or the first lane. Every listed lane must be clean (others may be ahead of main).
#   2. In the batch lane: git merge --no-edit main, then git merge --no-edit lane/<x> for each other lane, in order.
#      Any conflict: git merge --abort, print the conflicting files, status failed, stop (never resolves).
#   3. tools/build.ps1 (UBT always runs with -WaitMutex) and the full tools/run-tests.ps1 -Filter Project, both in the
#      batch lane; verdicts come from the lane's Saved/AgentLogs/status/{build,run-tests}.json.
#   4. Green: if main moved meanwhile, merge main into the batch lane again; if the new main commits touch Source/,
#      data/, Config/ or *.Build.cs, build + test again (at most -MaxRounds build/test rounds in total).
#      Then git merge --ff-only lane/<batch> in main and print ONE evidence line for docs/TASKS.md.
#   -DryRun: prints the plan and checks the lanes (and main's working tree overlap), then stops before any merge.
# Safety: never pushes, never force-merges, never resets, never deletes lanes, branches or worktrees.
# Main's working tree may hold uncommitted files (e.g. .claude/settings.json, Config/DefaultEditor.ini); the script
# refuses only if the fast-forward would touch one of them.
# Windows PowerShell 5.1 compatible. ASCII only. Writes Saved/AgentLogs/status/integrate.json.
param(
    [Parameter(Mandatory = $true)][string[]]$Lanes,
    [string]$Batch = '',
    [switch]$DryRun,
    [int]$MaxRounds = 2
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'integrate'
$root = (Get-RepoRoot).Replace('\', '/')
$lanesDir = (Split-Path -Parent (Get-RepoRoot)).Replace('\', '/') + '/VibeGame-lanes'
$relevantPattern = '^(Source/|data/|Config/)|\.Build\.cs$'

# Accept both "-Lanes eng2,eng3" and "-Lanes 'eng2,eng3'" (powershell -File passes the latter as one string).
$laneNames = @($Lanes | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Batch) { $Batch = $laneNames[0] }

function Fail([string]$Msg, $Details = $null) {
    Write-Status -Name $name -State 'failed' -Message $Msg -Details $Details
    exit 1
}

function Get-LaneMap {
    $map = @{}
    $path = $null
    foreach ($l in @(git -C $root worktree list --porcelain)) {
        if ($l -like 'worktree *') { $path = $l.Substring(9).Replace('\', '/') }
        elseif (($l -like 'branch refs/heads/lane/*') -and $path -and $path.StartsWith($lanesDir + '/', [StringComparison]::OrdinalIgnoreCase)) {
            $map[$l.Substring(23)] = [pscustomobject]@{ Name = $l.Substring(23); Path = $path; Branch = $l.Substring(18) }
        }
    }
    return $map
}

function Get-DirtyLines([string]$Path) {
    $lines = @(git -C $Path status --porcelain 2>$null | Where-Object { $_ })
    if ($LASTEXITCODE -ne 0) { return @('?? git status failed') }
    return $lines
}

function Get-Counts([string]$Branch) {
    # behind/ahead of main
    $c = (git -C $root rev-list --left-right --count ('main...' + $Branch) 2>$null)
    if ($c -match '^(\d+)\s+(\d+)$') { return @([int]$Matches[1], [int]$Matches[2]) }
    return @(-1, -1)
}

function Test-MergeInProgress([string]$Path) {
    git -C $Path rev-parse -q --verify MERGE_HEAD 2>$null | Out-Null
    return ($LASTEXITCODE -eq 0)
}

# Runs git merge --no-edit <ref> in the batch lane. Returns '' on success, else an error text (merge aborted).
function Invoke-LaneMerge([string]$Path, [string]$Ref) {
    $out = (git -C $Path merge --no-edit $Ref 2>&1 | Out-String)
    if ($LASTEXITCODE -eq 0) { Write-Host ('  merged ' + $Ref); return '' }
    $conflicts = @(git -C $Path diff --name-only --diff-filter=U 2>$null | Where-Object { $_ })
    if (Test-MergeInProgress $Path) { git -C $Path merge --abort 2>&1 | Out-Null }
    if ($conflicts.Count -gt 0) {
        Write-Host ('CONFLICT merging ' + $Ref + ' (merge aborted, nothing resolved):')
        $conflicts | ForEach-Object { Write-Host ('  ' + $_) }
        return ('conflict merging ' + $Ref + ' in ' + $Path + ': ' + ($conflicts -join ', '))
    }
    return ('git merge ' + $Ref + ' failed in ' + $Path + ': ' + $out.Trim())
}

# Runs a lane script and reads its status JSON (fresh = written after $Since).
function Invoke-LaneScript([string]$Path, [string]$Script, [string]$ScriptArgs) {
    $since = Get-Date
    $file = $Path + '/tools/' + $Script + '.ps1'
    Write-Host ('  running ' + $Script + '.ps1 ' + $ScriptArgs + ' in ' + $Path + ' ...')
    # Call operator, not Start-Process -Wait: the latter also waits for lingering descendants (mspdbsrv, shader workers).
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $file) + @($ScriptArgs -split ' ' | Where-Object { $_ })
    & powershell.exe @argList | Out-Host
    $p = [pscustomobject]@{ ExitCode = $LASTEXITCODE }
    $statusPath = $Path + '/Saved/AgentLogs/status/' + $Script + '.json'
    $st = $null
    if (Test-Path $statusPath) { try { $st = Get-Content -Path $statusPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $st = $null } }
    $fresh = $false
    if ($st -and $st.updatedAt) { try { $fresh = ([datetime]$st.updatedAt) -ge $since.AddSeconds(-2) } catch { $fresh = $false } }
    $ok = ($p.ExitCode -eq 0) -and $fresh -and ($st.state -eq 'succeeded')
    $msg = if ($st) { [string]$st.message } else { 'no status file ' + $statusPath }
    if (-not $fresh) { $msg = 'stale or missing status file (' + $statusPath + '): ' + $msg }
    Write-Host ('  ' + $Script + ': ' + $(if ($ok) { 'OK' } else { 'FAILED (exit ' + $p.ExitCode + ')' }) + ' - ' + $msg)
    return [pscustomobject]@{ Ok = $ok; ExitCode = $p.ExitCode; Status = $st; Message = $msg; LogPath = $(if ($st) { [string]$st.logPath } else { '' }) }
}

# Files a fast-forward of main to $Ref would change that are dirty/untracked in main's working tree.
function Get-MainOverlap([string]$Ref) {
    $dirty = @()
    foreach ($l in @(git -C $root status --porcelain --untracked-files=all 2>$null | Where-Object { $_ })) {
        $p = $l.Substring(3)
        if ($p -like '* -> *') { $dirty += ($p -split ' -> ')[0]; $dirty += ($p -split ' -> ')[1] } else { $dirty += $p }
    }
    $dirty = @($dirty | ForEach-Object { $_.Trim('"') })
    $changed = @(git -C $root diff --name-only ('main...' + $Ref) 2>$null | Where-Object { $_ })
    return [pscustomobject]@{ Dirty = $dirty; Overlap = @($changed | Where-Object { $dirty -contains $_ }) }
}

# --- Validate ---
if ($laneNames.Count -eq 0) { Fail 'no lanes given (-Lanes eng2,eng3)' }
$dups = @($laneNames | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
if ($dups.Count -gt 0) { Fail ('lane listed twice: ' + ($dups -join ',')) }
if ($laneNames -notcontains $Batch) { $laneNames = @($Batch) + $laneNames }
$others = @($laneNames | Where-Object { $_ -ne $Batch })

$mainBranch = (git -C $root symbolic-ref --short HEAD 2>$null)
if ($mainBranch -ne 'main') { Fail ('main checkout ' + $root + ' is on "' + $mainBranch + '", expected main') }
if (Test-MergeInProgress $root) { Fail 'main checkout has a merge in progress' }

$map = Get-LaneMap
$problems = @()
$infoLines = @()
foreach ($n in $laneNames) {
    if (-not $map.ContainsKey($n)) { $problems += ('unknown lane ' + $n + ' (not a worktree under ' + $lanesDir + ')'); continue }
    $l = $map[$n]
    $dirty = @(Get-DirtyLines $l.Path)
    $c = Get-Counts $l.Branch
    $role = if ($n -eq $Batch) { 'batch' } else { 'merge' }
    $infoLines += ('{0,-6} {1,-5} {2}  +{3} -{4}  {5}' -f $n, $role, (git -C $l.Path rev-parse --short HEAD), $c[1], $c[0], $(if ($dirty.Count) { 'DIRTY' } else { 'clean' }))
    if ($dirty.Count -gt 0) { $problems += ('lane ' + $n + ' is dirty: ' + (($dirty | Select-Object -First 5) -join '; ')) }
    if (Test-MergeInProgress $l.Path) { $problems += ('lane ' + $n + ' has a merge in progress') }
    if (($n -ne $Batch) -and ($c[1] -eq 0)) { Write-Host ('note: lane ' + $n + ' has nothing ahead of main (merge is a no-op)') }
}
$infoLines | ForEach-Object { Write-Host $_ }
if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Host ('ERROR ' + $_) }
    Fail ($problems -join '; ') @{ lanes = $infoLines }
}
$bl = $map[$Batch]

if ($DryRun) {
    $mainHead = (git -C $root rev-parse --short main)
    Write-Host ('Plan (dry run, nothing merged):')
    Write-Host ('  1. in ' + $bl.Path + ': git merge --no-edit main (' + $mainHead + ')')
    $i = 2
    foreach ($o in $others) { Write-Host ('  ' + $i + '. in ' + $bl.Path + ': git merge --no-edit lane/' + $o); $i++ }
    Write-Host ('  ' + $i + '. tools/build.ps1 (-WaitMutex) + tools/run-tests.ps1 -Filter Project in ' + $bl.Path); $i++
    Write-Host ('  ' + $i + '. if main moved: merge main again; rebuild/retest if Source/ data/ Config/ *.Build.cs changed (max ' + $MaxRounds + ' rounds)'); $i++
    Write-Host ('  ' + $i + '. in ' + $root + ': git merge --ff-only ' + $bl.Branch)
    $ov = Get-MainOverlap $bl.Branch
    if ($ov.Dirty.Count) { Write-Host ('  main uncommitted (left alone): ' + ($ov.Dirty -join ', ')) }
    if ($ov.Overlap.Count) { Write-Host ('  WARNING: ' + $bl.Branch + ' already changes files dirty in main: ' + ($ov.Overlap -join ', ') + ' (the ff would be refused)') }
    Write-Status -Name $name -State 'succeeded' -Message ('dry run OK: lanes ' + ($laneNames -join ',') + ' clean, batch ' + $Batch) -Details @{ lanes = $infoLines; batch = $Batch; dryRun = $true }
    exit 0
}

# --- Merge ---
Write-Status -Name $name -State 'running' -Message ('merging ' + ($laneNames -join ',') + ' in batch lane ' + $Batch)
$mergedMain = (git -C $root rev-parse main)
$err = Invoke-LaneMerge $bl.Path $mergedMain
if ($err) { Fail $err }
foreach ($o in $others) {
    $err = Invoke-LaneMerge $bl.Path ('lane/' + $o)
    if ($err) { Fail $err }
}

# --- Build + test, re-merge main while it moves ---
$round = 0
$needBuild = $true
$build = $null; $tests = $null
while ($true) {
    if ($needBuild) {
        if ($round -ge $MaxRounds) { Fail ('main kept changing code; ' + $MaxRounds + ' build/test rounds used. Rerun integrate.ps1.') }
        $round++
        Write-Status -Name $name -State 'running' -Message ('round ' + $round + ': build + tests in ' + $Batch)
        $build = Invoke-LaneScript $bl.Path 'build' ''
        if (-not $build.Ok) { Fail ('build failed in ' + $Batch + ' (round ' + $round + '): ' + $build.Message) @{ buildLog = $build.LogPath } }
        $tests = Invoke-LaneScript $bl.Path 'run-tests' '-Filter Project'
        if (-not $tests.Ok) { Fail ('tests failed in ' + $Batch + ' (round ' + $round + '): ' + $tests.Message) @{ testLog = $tests.LogPath } }
        $needBuild = $false
    }
    $mainNow = (git -C $root rev-parse main)
    if ($mainNow -eq $mergedMain) { break }
    $moved = @(git -C $root diff --name-only $mergedMain $mainNow 2>$null | Where-Object { $_ })
    Write-Host ('main moved ' + $mergedMain.Substring(0, 7) + ' -> ' + $mainNow.Substring(0, 7) + ' (' + $moved.Count + ' files); merging it again')
    $err = Invoke-LaneMerge $bl.Path $mainNow
    if ($err) { Fail $err }
    $mergedMain = $mainNow
    $code = @($moved | Where-Object { $_ -match $relevantPattern })
    if ($code.Count -gt 0) { Write-Host ('  code/data/config changed (' + (($code | Select-Object -First 5) -join ', ') + '): rebuild + retest'); $needBuild = $true }
}

# --- Fast-forward main ---
if (@(Get-DirtyLines $bl.Path).Count -gt 0) { Fail ('batch lane ' + $Batch + ' became dirty during the run') }
$ov = Get-MainOverlap $bl.Branch
if ($ov.Overlap.Count -gt 0) { Fail ('refusing the fast-forward: it would touch uncommitted files in main: ' + ($ov.Overlap -join ', ')) }
$out = (git -C $root merge --ff-only $bl.Branch 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { Fail ('git merge --ff-only ' + $bl.Branch + ' in main failed (nothing forced): ' + $out.Trim()) }
$mainShort = (git -C $root rev-parse --short HEAD)

$d = $tests.Status.details
$passedN = @($d.passed).Count
$totalN = if ($d.discovered -ge 0) { [int]$d.discovered } else { $passedN }
$reportDir = ([string]$d.reportDir).Replace('\', '/')
$evidence = ('Integrated ' + ($laneNames -join ',') + ' (batch ' + $Batch + ') into main ' + $mainShort + ': build green, tests ' + $passedN + '/' + $totalN + ' (' + $round + ' round' + $(if ($round -gt 1) { 's' } else { '' }) + '), report ' + $reportDir)
Write-Host $evidence
Write-Status -Name $name -State 'succeeded' -Message $evidence -LogPath $tests.LogPath -Details @{ lanes = $laneNames; batch = $Batch; mainCommit = $mainShort; rounds = $round; tests = ('' + $passedN + '/' + $totalN); reportDir = $reportDir; buildLog = $build.LogPath }
exit 0

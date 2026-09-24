# tools/lane.ps1 - lead helper for parallel worktree lanes (C:/GameDev/VibeGame-lanes/<lane> on branch lane/<lane>).
# Modes:
#   -List (default)        one line per lane: name, head, ahead/behind main, dirty, FREE (clean and 0 ahead of main).
#   -Free [-Prefix eng]    print the first FREE lane matching the prefix and fast-forward it to main;
#                          if none is free, create the next number (eng10, ...) from main and print its name.
#   -New <name> [-Base main]  git worktree add ../VibeGame-lanes/<name> -b lane/<name> <base>, copy tools/local.settings.json.
# Safety: never deletes lanes, branches or worktrees, never resets, never force-merges, never pushes.
# Only fast-forwards (merge --ff-only) lanes that are clean and fully merged into main.
# Windows PowerShell 5.1 compatible. ASCII only. Writes Saved/AgentLogs/status/lane.json.
param(
    [switch]$List,
    [switch]$Free,
    [string]$Prefix = 'eng',
    [string]$New = '',
    [string]$Base = 'main'
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'lane'
$root = (Get-RepoRoot).Replace('\', '/')
$lanesDir = (Split-Path -Parent (Get-RepoRoot)).Replace('\', '/') + '/VibeGame-lanes'
$settingsSrc = $root + '/tools/local.settings.json'

function Get-Lanes {
    # Worktrees under the lanes folder that are on a lane/<name> branch.
    $lanes = @()
    $path = $null
    foreach ($l in @(git -C $root worktree list --porcelain)) {
        if ($l -like 'worktree *') { $path = $l.Substring(9).Replace('\', '/') }
        elseif (($l -like 'branch refs/heads/lane/*') -and $path -and $path.StartsWith($lanesDir + '/', [StringComparison]::OrdinalIgnoreCase)) {
            $lanes += [pscustomobject]@{ Name = $l.Substring(23); Path = $path; Branch = $l.Substring(18) }
        }
    }
    return $lanes
}

function Get-LaneInfo($Lane) {
    $head = (git -C $Lane.Path rev-parse --short HEAD 2>$null)
    $counts = (git -C $root rev-list --left-right --count ('main...' + $Lane.Branch) 2>$null)
    $behind = -1; $ahead = -1
    if ($counts -match '^(\d+)\s+(\d+)$') { $behind = [int]$Matches[1]; $ahead = [int]$Matches[2] }
    $dirtyLines = @(git -C $Lane.Path status --porcelain 2>$null | Where-Object { $_ })
    $dirty = ($dirtyLines.Count -gt 0) -or ($LASTEXITCODE -ne 0)
    $isFree = (-not $dirty) -and ($ahead -eq 0)
    return [pscustomobject]@{ Name = $Lane.Name; Path = $Lane.Path; Branch = $Lane.Branch; Head = $head; Ahead = $ahead; Behind = $behind; Dirty = $dirty; Free = $isFree }
}

function Get-LaneNumber([string]$LaneName) {
    if ($LaneName -match ('^' + [regex]::Escape($Prefix) + '(\d+)$')) { return [int]$Matches[1] }
    return -1
}

function New-Lane([string]$LaneName, [string]$BaseRef) {
    if ($LaneName -notmatch '^[A-Za-z0-9_-]+$') { return 'invalid lane name: ' + $LaneName }
    $dest = $lanesDir + '/' + $LaneName
    if (Test-Path $dest) { return 'lane folder already exists: ' + $dest }
    git -C $root show-ref --verify --quiet ('refs/heads/lane/' + $LaneName)
    if ($LASTEXITCODE -eq 0) { return 'branch already exists: lane/' + $LaneName }
    $out = (git -C $root worktree add $dest -b ('lane/' + $LaneName) $BaseRef 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { return 'git worktree add failed: ' + $out.Trim() }
    if (Test-Path $settingsSrc) { Copy-Item -LiteralPath $settingsSrc -Destination ($dest + '/tools/local.settings.json') -Force }
    else { Write-Host ('warning: ' + $settingsSrc + ' missing; run tools/doctor.ps1 in the lane') }
    return ''
}

# --- New ---
if ($New) {
    $err = New-Lane $New $Base
    if ($err) { Write-Status -Name $name -State 'failed' -Message ('-New ' + $New + ': ' + $err); exit 1 }
    Write-Host $New
    Write-Status -Name $name -State 'succeeded' -Message ('created lane ' + $New + ' at ' + $lanesDir + '/' + $New + ' from ' + $Base) -Details @{ lane = $New }
    exit 0
}

# --- Free ---
if ($Free) {
    $infos = @(Get-Lanes | Where-Object { (Get-LaneNumber $_.Name) -ge 0 } | Sort-Object { Get-LaneNumber $_.Name } | ForEach-Object { Get-LaneInfo $_ })
    foreach ($i in $infos) {
        if (-not $i.Free) { continue }
        # Re-check right before touching it: clean and nothing unmerged.
        $again = Get-LaneInfo $i
        if (-not $again.Free) { continue }
        if ($again.Behind -gt 0) {
            $out = (git -C $i.Path merge --ff-only main 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0) { Write-Host ('skipped ' + $i.Name + ': fast-forward failed: ' + $out.Trim()); continue }
        }
        Write-Host $i.Name
        Write-Status -Name $name -State 'succeeded' -Message ('free lane ' + $i.Name + ' at main (' + (git -C $i.Path rev-parse --short HEAD) + ')') -Details @{ lane = $i.Name; created = $false }
        exit 0
    }
    $max = 0
    foreach ($i in $infos) { $n = Get-LaneNumber $i.Name; if ($n -gt $max) { $max = $n } }
    $newName = $Prefix + ($max + 1)
    $err = New-Lane $newName 'main'
    if ($err) { Write-Status -Name $name -State 'failed' -Message ('no free ' + $Prefix + ' lane; creating ' + $newName + ' failed: ' + $err); exit 1 }
    Write-Host $newName
    Write-Status -Name $name -State 'succeeded' -Message ('no free ' + $Prefix + ' lane; created ' + $newName + ' from main') -Details @{ lane = $newName; created = $true }
    exit 0
}

# --- List (default) ---
$infos = @(Get-Lanes | ForEach-Object { Get-LaneInfo $_ })
$lines = @()
foreach ($i in $infos) {
    $lines += ('{0,-6} {1}  +{2} -{3}  {4}{5}' -f $i.Name, $i.Head, $i.Ahead, $i.Behind, $(if ($i.Dirty) { 'dirty' } else { 'clean' }), $(if ($i.Free) { '  FREE' } else { '' }))
}
$lines | ForEach-Object { Write-Host $_ }
$freeNames = @($infos | Where-Object { $_.Free } | ForEach-Object { $_.Name })
Write-Status -Name $name -State 'succeeded' -Message ("$($infos.Count) lanes, free: " + ($freeNames -join ',')) -Details @{ lanes = $lines }
exit 0

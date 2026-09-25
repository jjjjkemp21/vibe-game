# tools/integrate.ps1 - the lead's one-step lane integration (P-003a).
# Usage: tools/integrate.ps1 -Lanes eng2,eng3 [-Batch eng2] [-DryRun] [-MaxRounds 2]
#   1. Batch lane = -Batch or the first lane. Every listed lane must be clean (others may be ahead of main).
#   2. In the batch lane: git merge --no-edit main, then git merge --no-edit lane/<x> for each other lane, in order.
#      One conflict is resolved automatically: docs/CODEMAP.md alone, with every hunk inside the
#      <!-- codemap:generated:begin/end --> block (e.g. generated test counts). The script keeps the batch side of each
#      hunk, reruns tools/codemap.ps1 in the batch lane, checks no conflict markers remain, then git add + commit.
#      Any other conflict: git merge --abort, print the conflicting files, status failed, stop.
#   3. tools/build.ps1 (UBT always runs with -WaitMutex) and the full tools/run-tests.ps1 -Filter Project, both in the
#      batch lane; verdicts come from the lane's Saved/AgentLogs/status/{build,run-tests}.json.
#      Skipped (and said so in the evidence line) when git diff main...lane/<batch> touches no Source/, data/,
#      Config/ or *.Build.cs path (docs- or tools-only lanes).
#   4. Green: if main moved meanwhile, merge main into the batch lane again; if the new main commits touch Source/,
#      data/, Config/ or *.Build.cs, build + test again (at most -MaxRounds build/test rounds in total).
#      Then git merge --ff-only lane/<batch> in main and print ONE evidence line for docs/TASKS.md.
#   5. Board (only if Saved/Studio/board.db exists; BOARD_DB overrides): every `merge` item on the integrated lanes
#      -> status=done commit=<main head> evidence=<the evidence line>; then board.py export, and commit docs/BOARD.md +
#      docs/board/items.jsonl in main if they changed (git commit -- <those paths> only).
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
$codemapRel = 'docs/CODEMAP.md'
$codemapBegin = '<!-- codemap:generated:begin -->'
$codemapEnd = '<!-- codemap:generated:end -->'
$boardFiles = @('docs/BOARD.md', 'docs/board/items.jsonl')

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

# Conflict-marker line kinds: <<<<<<< ours, ||||||| base (diff3), ======= separator, >>>>>>> theirs.
function Get-MarkerKind([string]$Line) {
    if ($Line -match '^<<<<<<<( |$)') { return 'start' }
    if ($Line -match '^\|\|\|\|\|\|\|( |$)') { return 'base' }
    if ($Line -match '^=======\s*$') { return 'sep' }
    if ($Line -match '^>>>>>>>( |$)') { return 'end' }
    return ''
}

# Resolves a merge whose ONLY conflict is docs/CODEMAP.md with every hunk inside the generated block: keeps the batch
# side ("ours") of each hunk, keeps the cleanly merged rest, reruns tools/codemap.ps1 in the lane, checks for leftover
# markers, git add + commit (the merge message git prepared). Returns '' when committed, else why not (the caller aborts).
function Resolve-CodemapConflict([string]$Path) {
    $file = $Path + '/' + $codemapRel
    if (-not (Test-Path -LiteralPath $file)) { return $codemapRel + ' is missing in the lane' }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $text = [IO.File]::ReadAllText($file, $utf8)
    $nl = "`n"
    if ($text.Contains("`r`n")) { $nl = "`r`n" }
    $keep = New-Object System.Collections.Generic.List[string]
    $inBlock = $false
    $hunk = ''   # '' outside a hunk, else 'ours' / 'skip' (base and theirs sides)
    $hunks = 0
    $n = 0
    foreach ($l in ($text -split "`r?`n")) {
        $n++
        $kind = Get-MarkerKind $l
        $isMark = ($l.Trim() -eq $codemapBegin) -or ($l.Trim() -eq $codemapEnd)
        if ($hunk) {
            if ($isMark) { return 'a conflict hunk contains a codemap block marker (line ' + $n + ')' }
            if ($kind -eq 'start') { return 'nested conflict marker (line ' + $n + ')' }
            if ($kind -eq 'end') { $hunk = ''; continue }
            if (($kind -eq 'base') -or ($kind -eq 'sep')) { $hunk = 'skip'; continue }
            if ($hunk -eq 'ours') { $keep.Add($l) }
            continue
        }
        if ($kind -eq 'start') {
            if (-not $inBlock) { return 'conflict outside the generated block (line ' + $n + ')' }
            $hunk = 'ours'; $hunks++; continue
        }
        if ($kind -and ($kind -ne 'sep')) { return 'stray conflict marker (line ' + $n + ')' }
        if ($l.Trim() -eq $codemapBegin) { $inBlock = $true }
        elseif ($l.Trim() -eq $codemapEnd) { $inBlock = $false }
        $keep.Add($l)
    }
    if ($hunk) { return 'unterminated conflict hunk' }
    if ($hunks -eq 0) { return 'no conflict hunks found in ' + $codemapRel }
    [IO.File]::WriteAllText($file, ($keep -join $nl), $utf8)

    Write-Host ('  ' + $codemapRel + ': ' + $hunks + ' conflict hunk(s), all in the generated block; regenerating with codemap.ps1')
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File ($Path + '/tools/codemap.ps1') | Out-Host
    if ($LASTEXITCODE -ne 0) { return 'tools/codemap.ps1 failed in ' + $Path + ' (exit ' + $LASTEXITCODE + ')' }
    $after = [IO.File]::ReadAllText($file, $utf8)
    foreach ($l in ($after -split "`r?`n")) {
        $k = Get-MarkerKind $l
        if ($k -and ($k -ne 'sep')) { return 'conflict markers remain in ' + $codemapRel + ' after codemap.ps1' }
    }
    git -C $Path add -- $codemapRel 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return 'git add ' + $codemapRel + ' failed' }
    $left = @(git -C $Path diff --name-only --diff-filter=U 2>$null | Where-Object { $_ })
    if ($left.Count -gt 0) { return 'still unmerged: ' + ($left -join ', ') }
    $stray = @(git -C $Path diff --name-only 2>$null | Where-Object { $_ })
    if ($stray.Count -gt 0) { return 'codemap.ps1 changed other files: ' + ($stray -join ', ') }
    $out = (git -C $Path commit --no-edit 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { return 'git commit of the merge failed: ' + $out.Trim() }
    return ''
}

# Runs git merge --no-edit <ref> in the batch lane. Returns '' on success, else an error text (merge aborted).
function Invoke-LaneMerge([string]$Path, [string]$Ref) {
    $out = (git -C $Path merge --no-edit $Ref 2>&1 | Out-String)
    if ($LASTEXITCODE -eq 0) { Write-Host ('  merged ' + $Ref); return '' }
    $conflicts = @(git -C $Path diff --name-only --diff-filter=U 2>$null | Where-Object { $_ })
    if (($conflicts.Count -eq 1) -and ($conflicts[0] -eq $codemapRel) -and (Test-MergeInProgress $Path)) {
        $why = Resolve-CodemapConflict $Path
        if (-not $why) { Write-Host ('  merged ' + $Ref + ' (' + $codemapRel + ' generated block conflict auto-resolved)'); return '' }
        Write-Host ('  ' + $codemapRel + ' not auto-resolved: ' + $why)
    }
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

# Re-imports every data/tables/DT_* source that differs from $BaseRef into its /Game/Data asset, headless, with the
# batch lane's own fresh build (so new row-struct fields exist), then commits the assets in the batch lane. Lanes never
# touch .uasset; this is the one place the pipeline writes DataTable binaries outside the editor (T-072, 2026-09-24).
function Sync-DataTables([string]$Path, [string]$BaseRef) {
    $changed = @(git -C $Path diff --name-only $BaseRef HEAD -- 'data/tables' 2>$null | Where-Object { $_ -match '^data/tables/DT_[^/]+\.(csv|json)$' })
    if ($changed.Count -eq 0) { return $null }
    $assets = @()
    foreach ($f in $changed) {
        $tbl = [IO.Path]::GetFileNameWithoutExtension($f)
        $asset = 'Content/Data/' + $tbl + '.uasset'
        if (-not (Test-Path -LiteralPath ($Path + '/' + $asset))) { Write-Host ('  table ' + $tbl + ': no asset yet (new table) - left for the editor-operator'); continue }
        $argsJson = '{"dest_path":"/Game/Data/' + $tbl + '","src_path":"' + ($Path + '/' + $f) + '"}'
        Write-Host ('  re-importing ' + $tbl + ' from ' + $f + ' (headless, batch lane build)')
        $since = Get-Date
        # Windows PowerShell 5.1 strips embedded double quotes from native-command arguments: escape them there.
        $passJson = if ($PSVersionTable.PSVersion.Major -lt 7) { $argsJson -replace '"', '\"' } else { $argsJson }
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File ($Path + '/tools/unreal-python.ps1') -Function reimport_table -ArgsJson $passJson | Out-Host
        $st = $null; $statusPath = $Path + '/Saved/AgentLogs/status/unreal-python.json'
        if (Test-Path $statusPath) { try { $st = Get-Content -Path $statusPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $st = $null } }
        $fresh = $false
        if ($st -and $st.updatedAt) { try { $fresh = ([datetime]$st.updatedAt) -ge $since.AddSeconds(-2) } catch { $fresh = $false } }
        if (($LASTEXITCODE -ne 0) -or (-not $fresh) -or ($st.state -ne 'succeeded')) { return ('re-import of ' + $tbl + ' failed in ' + $Path + ': ' + $(if ($st) { [string]$st.message } else { 'no status' })) }
        $assets += $asset
    }
    if ($assets.Count -eq 0) { return $null }
    $dirty = @(git -C $Path status --porcelain -- $assets 2>$null | Where-Object { $_ })
    if ($dirty.Count -eq 0) { Write-Host '  re-imported tables unchanged'; return $null }
    git -C $Path add -- $assets 2>&1 | Out-Null
    git -C $Path commit -q -m ('integrate: re-import ' + (($assets | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) }) -join ', ') + ' from source') -- $assets 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { return ('committing the re-imported tables failed in ' + $Path) }
    Write-Host ('  committed re-imported tables: ' + ($assets -join ', '))
    return $null
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

# Paths matching $relevantPattern that git diff main...<branch> touches, over all given branches (sorted, unique).
function Get-CodeChanges([string[]]$Branches) {
    $all = @()
    foreach ($b in $Branches) { $all += @(git -C $root diff --name-only ('main...' + $b) 2>$null | Where-Object { $_ -match $relevantPattern }) }
    return @($all | Sort-Object -Unique)
}

# The board's open `merge` items on the given lanes; @() without a DB. Throws the board error text if ls fails.
function Get-BoardMergeItems([string[]]$LaneList) {
    $r = Invoke-Board @('ls', '--status', 'merge', '--lane', ($LaneList -join ','), '-n', '0', '--json')
    if ($null -eq $r) { return @() }
    if ($r.ExitCode -ne 0) { throw ('board.py ls failed: ' + $r.Err) }
    if ($r.Out.Count -eq 0) { return @() }
    return @((($r.Out -join "`n") | ConvertFrom-Json) | ForEach-Object { $_ })
}

# Step 5. Never fails the run (main is already fast-forwarded): problems become warnings. Silent without a DB.
function Invoke-BoardDone([string]$Commit, [string]$Evidence) {
    $res = [ordered]@{ db = (Test-BoardDb); done = @(); failed = @(); exportCommit = '' }
    if (-not $res.db) { return $res }
    try { $items = @(Get-BoardMergeItems $laneNames) } catch { Write-Host ('warning: ' + $_.Exception.Message); $items = @() }
    foreach ($it in $items) {
        $r = Invoke-Board @('--as', 'integrate', 'set', [string]$it.id, 'status=done', ('commit=' + $Commit), ('evidence=' + $Evidence))
        $label = '#' + $it.id + $(if ($it.key) { ' ' + $it.key } else { '' })
        if ($r -and ($r.ExitCode -eq 0)) { $res.done += $label }
        else { $res.failed += $label; Write-Host ('warning: board set ' + $label + ' status=done failed: ' + $(if ($r) { $r.Err } else { 'no DB' })) }
    }
    if ($res.done.Count) { Write-Host ('board: done ' + ($res.done -join ', ')) }
    $ex = Invoke-Board @('export')
    if ((-not $ex) -or ($ex.ExitCode -ne 0)) { Write-Host ('warning: board.py export failed: ' + $(if ($ex) { $ex.Err } else { 'no DB' })); return $res }
    if (@(git -C $root status --porcelain -- $boardFiles 2>$null | Where-Object { $_ }).Count -eq 0) { return $res }
    git -C $root add -- $boardFiles 2>&1 | Out-Null
    $msg = 'board: export after integrating ' + ($laneNames -join ',') + ' (main ' + $Commit + ')'
    $out = (git -C $root commit -m $msg -- $boardFiles 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { Write-Host ('warning: committing the board export failed: ' + $out.Trim()); return $res }
    $res.exportCommit = (git -C $root rev-parse --short HEAD)
    Write-Host ('board: committed ' + ($boardFiles -join ' + ') + ' as ' + $res.exportCommit)
    return $res
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
    $code = @(Get-CodeChanges @($laneNames | ForEach-Object { $map[$_].Branch }))
    if ($code.Count -gt 0) {
        Write-Host ('  ' + $i + '. tools/build.ps1 (-WaitMutex) + tools/run-tests.ps1 -Filter Project in ' + $bl.Path + ' (' + $code.Count + ' code/data/config files, e.g. ' + (($code | Select-Object -First 3) -join ', ') + ')'); $i++
    } else {
        Write-Host ('  ' + $i + '. build + tests SKIPPED: the lanes touch no Source/, data/, Config/ or *.Build.cs path (as of now)'); $i++
    }
    Write-Host ('  ' + $i + '. if main moved: merge main again; rebuild/retest if Source/ data/ Config/ *.Build.cs changed (max ' + $MaxRounds + ' rounds)'); $i++
    Write-Host ('  ' + $i + '. in ' + $root + ': git merge --ff-only ' + $bl.Branch); $i++
    $boardPlan = 'no board DB (' + (Get-BoardDbPath) + '), skipped'
    if (Test-BoardDb) {
        try {
            $mi = @(Get-BoardMergeItems $laneNames)
            $ids = @($mi | ForEach-Object { '#' + $_.id + $(if ($_.key) { ' ' + $_.key } else { '' }) })
            $boardPlan = 'merge items -> done: ' + $(if ($ids.Count) { $ids -join ', ' } else { '(none)' }) + '; export; commit ' + ($boardFiles -join ' + ') + ' if changed'
        } catch { $boardPlan = 'WARNING ' + $_.Exception.Message }
    }
    Write-Host ('  ' + $i + '. board: ' + $boardPlan)
    $ov = Get-MainOverlap $bl.Branch
    if ($ov.Dirty.Count) { Write-Host ('  main uncommitted (left alone): ' + ($ov.Dirty -join ', ')) }
    if ($ov.Overlap.Count) { Write-Host ('  WARNING: ' + $bl.Branch + ' already changes files dirty in main: ' + ($ov.Overlap -join ', ') + ' (the ff would be refused)') }
    Write-Status -Name $name -State 'succeeded' -Message ('dry run OK: lanes ' + ($laneNames -join ',') + ' clean, batch ' + $Batch) -Details @{ lanes = $infoLines; batch = $Batch; dryRun = $true; codeFiles = $code.Count; board = $boardPlan }
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
$laneCode = @(Get-CodeChanges @($bl.Branch))
$skipBuild = ($laneCode.Count -eq 0)
$needBuild = -not $skipBuild
if ($skipBuild) { Write-Host ('no Source/, data/, Config/ or *.Build.cs changes in main...' + $bl.Branch + ': build + tests skipped') }
$build = $null; $tests = $null
while ($true) {
    if ($needBuild) {
        if ($round -ge $MaxRounds) { Fail ('main kept changing code; ' + $MaxRounds + ' build/test rounds used. Rerun integrate.ps1.') }
        $round++
        Write-Status -Name $name -State 'running' -Message ('round ' + $round + ': build + tests in ' + $Batch)
        $build = Invoke-LaneScript $bl.Path 'build' ''
        if (-not $build.Ok) { Fail ('build failed in ' + $Batch + ' (round ' + $round + '): ' + $build.Message) @{ buildLog = $build.LogPath } }
        $err = Sync-DataTables $bl.Path $mergedMain
        if ($err) { Fail $err }
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
    if ($skipBuild) { $skipBuild = (@(Get-CodeChanges @($bl.Branch)).Count -eq 0) }
    if ($skipBuild) { if ($code.Count -gt 0) { Write-Host '  main changed code, but the lanes still change none: build + tests stay skipped' } }
    elseif (($code.Count -gt 0) -or ($round -eq 0)) { Write-Host ('  code/data/config changed (' + (($code | Select-Object -First 5) -join ', ') + '): rebuild + retest'); $needBuild = $true }
}

# --- Fast-forward main ---
if (@(Get-DirtyLines $bl.Path).Count -gt 0) { Fail ('batch lane ' + $Batch + ' became dirty during the run') }
$ov = Get-MainOverlap $bl.Branch
if ($ov.Overlap.Count -gt 0) { Fail ('refusing the fast-forward: it would touch uncommitted files in main: ' + ($ov.Overlap -join ', ')) }
$assetChanges = @(git -C $root diff --name-only ('HEAD..' + $bl.Branch) -- 'Content' 2>$null | Where-Object { $_ })
if (($assetChanges.Count -gt 0) -and ((Get-EditorProcesses).Count -gt 0)) { Fail ('refusing the fast-forward: it changes ' + $assetChanges.Count + ' asset(s) (e.g. ' + $assetChanges[0] + ') while the main editor is open; close it (tools/stop-editor.ps1) and rerun') }
$out = (git -C $root merge --ff-only $bl.Branch 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { Fail ('git merge --ff-only ' + $bl.Branch + ' in main failed (nothing forced): ' + $out.Trim()) }
$mainShort = (git -C $root rev-parse --short HEAD)

if ($round -eq 0) {
    $testsText = 'skipped'; $reportDir = ''; $testLog = ''; $buildLog = ''
    $evidence = ('Integrated ' + ($laneNames -join ',') + ' (batch ' + $Batch + ') into main ' + $mainShort + ': build + tests skipped (no Source/, data/, Config/ or *.Build.cs changes)')
} else {
    $d = $tests.Status.details
    $passedN = @($d.passed).Count
    $totalN = if ($d.discovered -ge 0) { [int]$d.discovered } else { $passedN }
    $reportDir = ([string]$d.reportDir).Replace('\', '/')
    $testsText = ('' + $passedN + '/' + $totalN); $testLog = $tests.LogPath; $buildLog = $build.LogPath
    $evidence = ('Integrated ' + ($laneNames -join ',') + ' (batch ' + $Batch + ') into main ' + $mainShort + ': build green, tests ' + $passedN + '/' + $totalN + ' (' + $round + ' round' + $(if ($round -gt 1) { 's' } else { '' }) + '), report ' + $reportDir)
}
Write-Host $evidence
Write-Status -Name $name -State 'running' -Message ('main fast-forwarded to ' + $mainShort + '; updating the board')
$boardRes = Invoke-BoardDone $mainShort $evidence
Write-Status -Name $name -State 'succeeded' -Message $evidence -LogPath $testLog -Details @{ lanes = $laneNames; batch = $Batch; mainCommit = $mainShort; rounds = $round; tests = $testsText; reportDir = $reportDir; buildLog = $buildLog; board = $boardRes }
exit 0

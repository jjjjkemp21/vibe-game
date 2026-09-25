# tools/run-tests.ps1 - runs Unreal automation tests headless (UnrealEditor-Cmd, -nullrhi) and parses the report.
# Example: tools/run-tests.ps1 -Filter Project.GoldenPath
# Filters are PREFIX matches on dotted test paths by default (Unreal "StartsWith:"): -Filter Project runs Project.* only.
# A plain substring match would also run engine tests that merely contain the word (e.g. "Project Promotion Pass",
# ConfigSettings "Project" tests), which create projects/maps and rewrite Config/DefaultGame.ini.
# To run one exact test, pass its full path with -Substring.
# Exit 0 = the process finished normally, every discovered test completed, and none failed.
# The run FAILS (exit 1, status "failed") on: a crash signature in the log or an abnormal process exit; fewer completed
# tests than the runner discovered for the filter ("Found N automation tests"); a missing or truncated index.json report.
# Dry run of the verdict logic on an existing report folder (no Unreal process, no status file written):
#   tools/run-tests.ps1 -ParseReportDir Saved/AgentLogs/tests/<stamp> [-ParseExitCode <n>]
param(
    [string]$Filter = 'Project',
    [switch]$Substring,
    [int]$TimeoutMinutes = 45,
    [switch]$AllowWhileEditorOpen,
    [string]$ParseReportDir = '',
    [int]$ParseExitCode = [int]::MinValue,
    [switch]$NoTableSync
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'run-tests'
$parseOnly = [bool]$ParseReportDir
function Set-RunStatus([string]$State, [string]$Message, [string]$LogPath = '', $Details = $null) {
    if ($parseOnly) { Write-Host ('[' + $name + ' dry-run] ' + $State + ': ' + $Message) }
    else { Write-Status -Name $name -State $State -Message $Message -LogPath $LogPath -Details $Details }
}

# A lane changes only the CSV/JSON source of a DataTable; integrate.ps1 re-imports the binary /Game/Data/DT_* at merge time.
# Without this, functional tests in the lane would read the OLD binary table and pass or fail on stale values. So for this
# run only, re-import (headless) every table whose source differs from main and whose binary doesn't, then restore the
# committed binary afterwards (lanes never commit .uasset). Main and the integration batch lane (binary already committed)
# skip it. -NoTableSync turns it off.
function Sync-LaneTables([string]$Root) {
    $none = [pscustomobject]@{ Assets = @(); Error = $null }
    $branch = git -C $Root rev-parse --abbrev-ref HEAD 2>$null
    if ($branch -notlike 'lane/*') { return $none }
    $base = git -C $Root merge-base HEAD main 2>$null
    if (-not $base) { return $none }
    $assets = @()
    foreach ($f in @(git -C $Root diff --name-only $base -- 'data/tables' 2>$null | Where-Object { $_ -match '^data/tables/DT_[^/]+\.(csv|json)$' })) {
        $tbl = [IO.Path]::GetFileNameWithoutExtension($f)
        $asset = 'Content/Data/' + $tbl + '.uasset'
        if (-not (Test-Path -LiteralPath (Join-Path $Root $asset))) { continue }
        git -C $Root diff --quiet $base -- $asset 2>$null
        if ($LASTEXITCODE -ne 0) { continue }
        Write-Host ('  table ' + $tbl + ': source changed in this lane; re-importing it for this test run only')
        $argsJson = '{"dest_path":"/Game/Data/' + $tbl + '","src_path":"' + (Join-Path $Root $f).Replace('\', '/') + '"}'
        # Windows PowerShell 5.1 strips embedded double quotes from native-command arguments: escape them there.
        $passJson = if ($PSVersionTable.PSVersion.Major -lt 7) { $argsJson -replace '"', '\"' } else { $argsJson }
        $since = Get-Date
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'unreal-python.ps1') -Function reimport_table -ArgsJson $passJson | Out-Host
        $st = $null; $statusPath = Join-Path $Root 'Saved/AgentLogs/status/unreal-python.json'
        if (Test-Path $statusPath) { try { $st = Get-Content -Path $statusPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $st = $null } }
        $fresh = $false
        if ($st -and $st.updatedAt) { try { $fresh = ([datetime]$st.updatedAt) -ge $since.AddSeconds(-2) } catch { $fresh = $false } }
        if (($LASTEXITCODE -ne 0) -or (-not $fresh) -or ($st.state -ne 'succeeded')) {
            if ($assets.Count -gt 0) { git -C $Root checkout -- $assets 2>&1 | Out-Null }
            git -C $Root checkout -- $asset 2>&1 | Out-Null
            return [pscustomobject]@{ Assets = @(); Error = ('re-import of ' + $tbl + ' for the test run failed (build the lane first?): ' + $(if ($st) { [string]$st.message } else { 'no status' })) }
        }
        $assets += $asset
    }
    return [pscustomobject]@{ Assets = $assets; Error = $null }
}

if ($parseOnly) {
    $reportDir = (Resolve-Path $ParseReportDir).Path
    $logBase = Join-Path $reportDir 'run'
    $r = [pscustomobject]@{ ExitCode = $ParseExitCode; TimedOut = $false; OutLog = ($logBase + '.out.log'); ErrLog = ($logBase + '.err.log') }
} else {
    try { $uproject = Get-ProjectFile; $cmdExe = Get-EditorCmdExe } catch { Write-Status -Name $name -State 'failed' -Message $_.Exception.Message; exit 1 }
    if (((Get-EditorProcesses).Count -gt 0) -and (-not $AllowWhileEditorOpen)) {
        Write-Status -Name $name -State 'failed' -Message 'The editor is open. For the canonical headless run: save, tools/stop-editor.ps1, then rerun. (Or pass -AllowWhileEditorOpen for read-only tests, or use the in-editor testing tools via unreal-mcp.)'
        exit 3
    }

    $stamp = Get-Timestamp
    $reportDir = Join-Path (Get-LogDir 'tests') $stamp
    New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
    $logBase = Join-Path $reportDir 'run'
    $runArg = if ($Substring -or $Filter.Contains(':')) { $Filter } else { 'StartsWith:' + $Filter }
    $argList = '"' + $uproject + '" -ExecCmds="Automation RunTests ' + $runArg + ';Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath="' + $reportDir + '" -unattended -nopause -nosplash -nullrhi -NoSound -stdout -FullStdOutLogOutput'
    Write-Status -Name $name -State 'running' -Message ('Running tests matching ' + $Filter) -LogPath ($logBase + '.out.log')
    $synced = @()
    if (-not $NoTableSync) {
        $sync = Sync-LaneTables (Split-Path -Parent $uproject)
        if ($sync.Error) { Write-Status -Name $name -State 'failed' -Message $sync.Error; exit 1 }
        $synced = $sync.Assets
    }
    try { $r = Invoke-Logged -FilePath $cmdExe -ArgumentList $argList -LogBase $logBase -TimeoutMinutes $TimeoutMinutes }
    finally { if ($synced.Count -gt 0) { git -C (Split-Path -Parent $uproject) checkout -- $synced 2>&1 | Out-Null; Write-Host ('  restored the committed ' + ($synced -join ', ')) } }
}

$passed = New-Object System.Collections.ArrayList
$failed = New-Object System.Collections.ArrayList
$notRunNames = New-Object System.Collections.ArrayList
$problems = New-Object System.Collections.ArrayList

# 1) The JSON report (index.json). Missing or unparsable = failed run.
$index = Join-Path $reportDir 'index.json'
$reportOk = $false
if (-not (Test-Path $index)) { [void]$problems.Add('JSON report missing') }
else {
    try {
        $j = Get-Content -Path $index -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        if (($null -eq $j) -or ($null -eq $j.PSObject.Properties['tests'])) { throw 'no "tests" array' }
        foreach ($t in @($j.tests)) {
            if (-not $t) { continue }
            $tn = [string]$t.fullTestPath
            if (-not $tn) { $tn = [string]$t.testDisplayName }
            $st = [string]$t.state
            if ($st -eq 'Success') { [void]$passed.Add($tn) }
            elseif ($st -eq 'Fail') {
                $msgs = @()
                foreach ($e in @($t.entries)) { if ($e -and $e.event -and ($e.event.type -eq 'Error')) { $msgs += [string]$e.event.message } }
                [void]$failed.Add(($tn + ' [' + $st + '] ' + ($msgs -join ' | ')))
            }
            else { [void]$notRunNames.Add(($tn + ' [' + $st + ']')) }
        }
        $reportOk = $true
    } catch {
        $why = ([string]$_.Exception.Message -split "`r?`n")[0]
        if ($why.Length -gt 120) { $why = $why.Substring(0, 120) }
        [void]$problems.Add('JSON report truncated or unreadable (' + $why + ')')
        $passed.Clear(); $failed.Clear(); $notRunNames.Clear()
    }
}

# 2) The log: discovered count, per-test fallback counts, crash signatures, completion marker.
$logText = ''
if (Test-Path $r.OutLog) {
    # Shared read: the stdout redirect handle can still be open for a moment after the process exits.
    for ($try = 0; $try -lt 10; $try++) {
        try {
            $fs = [IO.FileStream]::new($r.OutLog, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            try { $logText = [IO.StreamReader]::new($fs).ReadToEnd() } finally { $fs.Dispose() }
            break
        } catch { $readErr = $_.Exception.Message; Start-Sleep -Milliseconds 500 }
    }
    if (-not $logText) { [void]$problems.Add('could not read the test log (' + $readErr + ')') }
}
if ((-not $reportOk) -and $logText) {
    # Counts from the log so the message is still useful; the run stays failed because the report is bad.
    foreach ($m in [regex]::Matches($logText, 'Test Completed\. Result=\{(\w+)\}\s+Name=\{([^}]*)\}\s+Path=\{([^}]*)\}')) {
        $res = $m.Groups[1].Value; $path = $m.Groups[3].Value
        if ($res -eq 'Success') { [void]$passed.Add($path) } else { [void]$failed.Add($path + ' [' + $res + ']') }
    }
}
$discovered = -1
$found = [regex]::Matches($logText, 'Found (\d+) automation tests based on')
if ($found.Count -gt 0) { $discovered = [int]$found[$found.Count - 1].Groups[1].Value }
$completed = $passed.Count + $failed.Count
$notRun = if ($discovered -ge 0) { [Math]::Max(0, $discovered - $completed) } else { $notRunNames.Count }
if (($discovered -lt 0) -and ($completed -gt 0)) { [void]$problems.Add('the runner never logged how many tests it discovered') }
elseif (($discovered -ge 0) -and ($completed -lt $discovered)) { [void]$problems.Add('only ' + $completed + ' of ' + $discovered + ' discovered tests completed') }

$crashSig = [regex]::Match($logText, '=== Critical error: ===|Unhandled Exception:|LaunchWindowsStartup\.ExceptionHandler|Assertion failed:')
if ($crashSig.Success) { [void]$problems.Add('crash signature in log ("' + $crashSig.Value + '")') }
if (($completed -gt 0) -and (-not [regex]::IsMatch($logText, '\*\*\*\* TEST COMPLETE\. EXIT CODE: -?\d+ \*\*\*\*'))) {
    [void]$problems.Add('log has no "TEST COMPLETE" line (the runner did not finish)')
}
if (($r.ExitCode -ne [int]::MinValue) -and (-not $r.TimedOut)) {
    # Unreal exits 0 when every test passed and 255 (RequestExitWithStatus 255) when some failed; anything else is abnormal.
    $okCodes = if ($failed.Count -gt 0) { @(0, 1, 255, -1) } else { @(0) }
    if ($okCodes -notcontains $r.ExitCode) { [void]$problems.Add('abnormal process exit code ' + $r.ExitCode) }
}

# 3) Verdict.
$discText = if ($discovered -ge 0) { '' + $discovered } else { '?' }
$counts = $discText + ' discovered, ' + $passed.Count + ' passed, ' + $failed.Count + ' failed, ' + $notRun + ' not run'
$details = @{ filter = $Filter; reportDir = $reportDir; discovered = $discovered; notRun = $notRun; problems = @($problems); passed = @($passed); failed = @($failed); notRunTests = @($notRunNames); exitCode = $r.ExitCode; timedOut = $r.TimedOut }
if ($r.TimedOut) {
    Set-RunStatus 'timeout' ('Tests timed out after ' + $TimeoutMinutes + ' min. ' + $counts + '.') $r.OutLog $details
    exit 1
}
if ($completed -eq 0) {
    Set-RunStatus 'failed' ('No tests ran for filter "' + $Filter + '" (' + $counts + '). Was the build current? List tests with -ExecCmds="Automation List;Quit". ' + ($problems -join '; ')) $r.OutLog $details
    Write-Host (Get-LogTail $r.OutLog 40)
    exit 1
}
if ($problems.Count -gt 0) {
    Set-RunStatus 'failed' ('Run crashed or incomplete: ' + ($problems -join '; ') + '. ' + $counts + '. Report: ' + $reportDir) $r.OutLog $details
    $failed | ForEach-Object { Write-Host ('FAIL ' + $_) }
    Write-Host (Get-LogTail $r.OutLog 25)
    exit 1
}
if ($failed.Count -gt 0) {
    Set-RunStatus 'failed' ($counts + '. Report: ' + $reportDir) $r.OutLog $details
    $failed | ForEach-Object { Write-Host ('FAIL ' + $_) }
    exit 1
}
Set-RunStatus 'succeeded' ($counts + '. Report: ' + $reportDir) $r.OutLog $details
$passed | ForEach-Object { Write-Host ('PASS ' + $_) }
exit 0

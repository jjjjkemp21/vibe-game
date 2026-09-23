# tools/run-tests.ps1 - runs Unreal automation tests headless (UnrealEditor-Cmd, -nullrhi) and parses the report.
# Example: tools/run-tests.ps1 -Filter Project.GoldenPath
# Filters are PREFIX matches on dotted test paths by default (Unreal "StartsWith:"): -Filter Project runs Project.* only.
# A plain substring match would also run engine tests that merely contain the word (e.g. "Project Promotion Pass",
# ConfigSettings "Project" tests), which create projects/maps and rewrite Config/DefaultGame.ini.
# To run one exact test, pass its full path with -Substring.
# Exit 0 = at least one test ran and none failed.
param(
    [string]$Filter = 'Project',
    [switch]$Substring,
    [int]$TimeoutMinutes = 45,
    [switch]$AllowWhileEditorOpen
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'run-tests'

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
$r = Invoke-Logged -FilePath $cmdExe -ArgumentList $argList -LogBase $logBase -TimeoutMinutes $TimeoutMinutes

$passed = New-Object System.Collections.ArrayList
$failed = New-Object System.Collections.ArrayList
$index = Join-Path $reportDir 'index.json'
if (Test-Path $index) {
    try {
        $j = Get-Content -Path $index -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($t in @($j.tests)) {
            if (-not $t) { continue }
            $tn = [string]$t.fullTestPath
            if (-not $tn) { $tn = [string]$t.testDisplayName }
            if ([string]$t.state -eq 'Success') { [void]$passed.Add($tn) }
            else {
                $msgs = @()
                foreach ($e in @($t.entries)) { if ($e -and $e.event -and ($e.event.type -eq 'Error')) { $msgs += [string]$e.event.message } }
                [void]$failed.Add(($tn + ' [' + $t.state + '] ' + ($msgs -join ' | ')))
            }
        }
    } catch { }
}
if ((($passed.Count + $failed.Count) -eq 0) -and (Test-Path $r.OutLog)) {
    foreach ($m in @(Select-String -Path $r.OutLog -Pattern 'Test Completed\. Result=\{(\w+)\}\s+Name=\{([^}]*)\}\s+Path=\{([^}]*)\}' -ErrorAction SilentlyContinue)) {
        $res = $m.Matches[0].Groups[1].Value; $path = $m.Matches[0].Groups[3].Value
        if ($res -eq 'Success') { [void]$passed.Add($path) } else { [void]$failed.Add($path + ' [' + $res + ']') }
    }
}

$details = @{ filter = $Filter; reportDir = $reportDir; passed = @($passed); failed = @($failed); exitCode = $r.ExitCode; timedOut = $r.TimedOut }
if ($r.TimedOut) {
    Write-Status -Name $name -State 'timeout' -Message ('Tests timed out after ' + $TimeoutMinutes + ' min.') -LogPath $r.OutLog -Details $details
    exit 1
}
if (($passed.Count + $failed.Count) -eq 0) {
    Write-Status -Name $name -State 'failed' -Message ('No tests ran for filter "' + $Filter + '". Was the build current? List tests with -ExecCmds="Automation List;Quit".') -LogPath $r.OutLog -Details $details
    Write-Host (Get-LogTail $r.OutLog 40)
    exit 1
}
if ($failed.Count -gt 0) {
    Write-Status -Name $name -State 'failed' -Message ('' + $failed.Count + ' failed, ' + $passed.Count + ' passed.') -LogPath $r.OutLog -Details $details
    $failed | ForEach-Object { Write-Host ('FAIL ' + $_) }
    exit 1
}
Write-Status -Name $name -State 'succeeded' -Message ('' + $passed.Count + ' passed, 0 failed. Report: ' + $reportDir) -LogPath $r.OutLog -Details $details
$passed | ForEach-Object { Write-Host ('PASS ' + $_) }
exit 0

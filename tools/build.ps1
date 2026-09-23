# tools/build.ps1 - compiles the project's editor target with Unreal Build Tool. The editor must be CLOSED.
# Exit 0 = build succeeded (writes Saved\AgentState\last-successful-build.txt, used by the Stop hook).
# Can take several minutes: agents should run it in the background or with a long timeout.
# -NoHotReloadFromIDE: UBT otherwise refuses to build while ANY editor on this engine install has Live Coding
# active (e.g. the main checkout's editor while a worktree lane builds). Safe because this script already
# refuses when an editor has THIS checkout's .uproject open (Get-EditorProcesses).
param(
    [ValidateSet('Development', 'DebugGame')][string]$Configuration = 'Development',
    [int]$TimeoutMinutes = 90
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'build'

try {
    $uproject = Get-ProjectFile
    $bat = Get-BuildBat
    $target = Get-EditorTargetName
} catch {
    Write-Status -Name $name -State 'failed' -Message $_.Exception.Message
    exit 1
}

if ((Get-EditorProcesses).Count -gt 0) {
    $msg = 'The Unreal Editor has this project open (Live Coding blocks external builds). Save, run tools/stop-editor.ps1, then build. For edits inside existing .cpp function bodies only, Live Coding (LiveCoding.Compile) is an alternative.'
    Write-Status -Name $name -State 'failed' -Message $msg
    exit 3
}

$logBase = Join-Path (Get-LogDir 'build') ('build-' + (Get-Timestamp))
$inner = '"' + $bat + '" ' + $target + ' Win64 ' + $Configuration + ' "-Project=' + $uproject + '" -WaitMutex -NoHotReloadFromIDE'
$argList = '/d /s /c "' + $inner + '"'
Write-Status -Name $name -State 'running' -Message ('Building ' + $target + ' Win64 ' + $Configuration) -LogPath ($logBase + '.out.log')

$r = Invoke-Logged -FilePath $env:ComSpec -ArgumentList $argList -LogBase $logBase -TimeoutMinutes $TimeoutMinutes

if (($r.ExitCode -eq 0) -and (-not $r.TimedOut)) {
    Write-TextFile (Join-Path (Get-RepoRoot) 'Saved\AgentState\last-successful-build.txt') ((Get-Date).ToString('s') + ' ' + $target + ' ' + $Configuration)
    Write-Status -Name $name -State 'succeeded' -Message ('Build succeeded: ' + $target + ' ' + $Configuration) -LogPath $r.OutLog
    Write-Host (Get-LogTail $r.OutLog 8)
    exit 0
}

$errors = @()
if (Test-Path $r.OutLog) {
    $errors = @(Select-String -Path $r.OutLog -Pattern ': error ', 'error C\d+', 'error LNK\d+', 'ERROR:', 'Unable to build' -ErrorAction SilentlyContinue |
        Select-Object -First 40 | ForEach-Object { $_.Line.Trim() })
}
$state = 'failed'
if ($r.TimedOut) { $state = 'timeout' }
Write-Status -Name $name -State $state -Message ('Build failed (exit ' + $r.ExitCode + '). See log.') -LogPath $r.OutLog -Details @{ errors = $errors }
Write-Host '---- error lines ----'
$errors | ForEach-Object { Write-Host $_ }
Write-Host '---- log tail ----'
Write-Host (Get-LogTail $r.OutLog 60)
Write-Host (Get-LogTail $r.ErrLog 20)
exit 1

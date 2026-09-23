# tools/unreal-python.ps1 - runs Unreal Python HEADLESS (UnrealEditor-Cmd -run=pythonscript), no editor window.
# Default script: Content/Python/pipeline_cli.py, which calls a function from pipeline_unreal.py:
#   tools/unreal-python.ps1 -Function import_static_mesh -ArgsJson '{"src_path":"C:/GameDev/X/art/export/Props/SM_A.fbx","dest_dir":"/Game/Art/Props","name":"SM_A"}'
# Or run any script: tools/unreal-python.ps1 -Script Content/Python/my_script.py
# The pythonscript commandlet does not load a level by itself; functions must load what they need.
# Refuses to run while the editor has the project open (both would write the same assets).
param(
    [string]$Script = 'Content/Python/pipeline_cli.py',
    [string]$Function = '',
    [string]$ArgsJson = '{}',
    [int]$TimeoutMinutes = 30,
    [switch]$AllowWhileEditorOpen
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'unreal-python'

try { $uproject = Get-ProjectFile; $cmdExe = Get-EditorCmdExe } catch { Write-Status -Name $name -State 'failed' -Message $_.Exception.Message; exit 1 }
if (((Get-EditorProcesses).Count -gt 0) -and (-not $AllowWhileEditorOpen)) {
    Write-Status -Name $name -State 'failed' -Message 'The editor is open. Use unreal-mcp (live editor) instead, or save + tools/stop-editor.ps1 first.'
    exit 3
}
$scriptPath = $Script
if (-not [IO.Path]::IsPathRooted($scriptPath)) { $scriptPath = Join-Path (Get-RepoRoot) $Script }
if (-not (Test-Path $scriptPath)) { Write-Status -Name $name -State 'failed' -Message ('Script not found: ' + $scriptPath); exit 1 }
$scriptFwd = ((Resolve-Path $scriptPath).Path) -replace '\\', '/'

$env:PIPELINE_FUNCTION = $Function
$env:PIPELINE_ARGS = $ArgsJson
$logBase = Join-Path (Get-LogDir 'unreal-python') ((([IO.Path]::GetFileNameWithoutExtension($scriptPath)) + '-' + $Function).TrimEnd('-') + '-' + (Get-Timestamp))
$argList = '"' + $uproject + '" -run=pythonscript -script="' + $scriptFwd + '" -unattended -nopause -nosplash -nullrhi -NoSound -stdout -FullStdOutLogOutput'
Write-Status -Name $name -State 'running' -Message ('Running ' + $scriptFwd + ' ' + $Function) -LogPath ($logBase + '.out.log')
$r = Invoke-Logged -FilePath $cmdExe -ArgumentList $argList -LogBase $logBase -TimeoutMinutes $TimeoutMinutes

$resultJson = $null
$pyErrors = @()
if (Test-Path $r.OutLog) {
    $hit = Select-String -Path $r.OutLog -Pattern 'RESULT_JSON:(.*)$' -ErrorAction SilentlyContinue | Select-Object -Last 1
    if ($hit) { $resultJson = $hit.Matches[0].Groups[1].Value.Trim() }
    $pyErrors = @(Select-String -Path $r.OutLog -Pattern 'LogPython: Error', 'PIPELINE_FAILED', 'Traceback' -ErrorAction SilentlyContinue | Select-Object -First 30 | ForEach-Object { $_.Line.Trim() })
}
if ($resultJson -and ($pyErrors.Count -eq 0) -and (-not $r.TimedOut)) {
    $resultPath = $logBase + '.result.json'
    Write-TextFile $resultPath $resultJson
    Write-Status -Name $name -State 'succeeded' -Message ('OK. Result: ' + $resultPath) -LogPath $r.OutLog
    Write-Host $resultJson
    exit 0
}
if ((-not $Function) -and ($r.ExitCode -eq 0) -and ($pyErrors.Count -eq 0) -and (-not $r.TimedOut)) {
    Write-Status -Name $name -State 'succeeded' -Message 'Script finished without Python errors.' -LogPath $r.OutLog
    exit 0
}
Write-Status -Name $name -State 'failed' -Message ('Python run failed (exit ' + $r.ExitCode + ', timedOut=' + $r.TimedOut + ').') -LogPath $r.OutLog -Details @{ errors = $pyErrors }
$pyErrors | ForEach-Object { Write-Host $_ }
Write-Host (Get-LogTail $r.OutLog 50)
exit 1

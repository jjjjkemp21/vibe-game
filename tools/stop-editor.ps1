# tools/stop-editor.ps1 - closes the Unreal Editor for this project gracefully (like clicking the X).
# SAVE FIRST (through unreal-mcp). If a save prompt blocks the close, the script reports it.
# -Force kills the process after the wait (unsaved changes are lost).
param([int]$WaitSeconds = 120, [switch]$Force)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'stop-editor'

$procs = Get-EditorProcesses
if ($procs.Count -eq 0) {
    Write-Status -Name $name -State 'succeeded' -Message 'Editor was not running.'
    exit 0
}
foreach ($c in $procs) {
    $p = Get-Process -Id $c.ProcessId -ErrorAction SilentlyContinue
    if ($p) { [void]$p.CloseMainWindow() }
}
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    if ((Get-EditorProcesses).Count -eq 0) {
        Write-Status -Name $name -State 'succeeded' -Message 'Editor closed.'
        exit 0
    }
    Start-Sleep -Seconds 3
}
if ($Force) {
    foreach ($c in (Get-EditorProcesses)) { Stop-Process -Id $c.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 3
    Write-Status -Name $name -State 'succeeded' -Message 'Editor force-closed (unsaved changes, if any, were lost).'
    exit 0
}
Write-Status -Name $name -State 'failed' -Message 'Editor still running. A "Save changes?" dialog is probably open: save via unreal-mcp or ask Cowork to handle the dialog, then rerun. Use -Force only if losing unsaved changes is acceptable.'
exit 2

# tools/launch-editor.ps1 - starts the Unreal Editor for this project with Epic's Unreal MCP server,
# then waits until the server port is listening. Idempotent: if the editor is already running it just waits.
# Exit 0 = ready (MCP reachable). Exit 2 = still starting (run again to keep waiting). Exit 1/4 = failed.
# -MaxFps: frame cap for agent work (Jimmy, 2026-09-24: 30 fps while agents use the editor). -MaxFps 0 = uncapped (Jimmy plays).
# To change it in an editor that is already running: unreal-mcp run_python `unreal.SystemLibrary.execute_console_command(None, 't.MaxFPS 0')`.
param([int]$WaitSeconds = 480, [int]$MaxFps = 30)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'launch-editor'

try {
    $uproject = Get-ProjectFile
    $editor = Get-EditorExe
    $proj = Get-ProjectName
} catch {
    Write-Status -Name $name -State 'failed' -Message $_.Exception.Message
    exit 1
}
$port = Get-UeMcpPort
$url = 'http://127.0.0.1:' + $port + '/mcp'
$editorLog = Join-Path (Get-RepoRoot) ('Saved\Logs\' + $proj + '.log')

$startedNow = $false
if ((Get-EditorProcesses).Count -eq 0) {
    $owner = Get-PortOwner $port
    if ($owner) {
        Write-Status -Name $name -State 'failed' -Message ('Port ' + $port + ' is already used by ' + $owner + '. Run tools/doctor.ps1 to choose a free port, restart Claude Code, then retry.')
        exit 4
    }
    Start-Process -FilePath $editor -ArgumentList ('"' + $uproject + '" -ModelContextProtocolStartServer -ModelContextProtocolPort=' + $port + $(if ($MaxFps -gt 0) { ' -ExecCmds="t.MaxFPS ' + $MaxFps + '"' } else { '' })) | Out-Null
    $startedNow = $true
    Write-Status -Name $name -State 'starting' -Message ('Editor started; waiting for the MCP server on ' + $url + '. First start after enabling plugins or new shaders can take 10-40 minutes.') -LogPath $editorLog
}

$begin = Get-Date
$deadline = $begin.AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    if (Test-PortListening $port) {
        Write-Status -Name $name -State 'ready' -Message ('Editor running and Unreal MCP listening at ' + $url) -LogPath $editorLog -Details @{ url = $url; port = $port }
        exit 0
    }
    $elapsed = ((Get-Date) - $begin).TotalSeconds
    if (($elapsed -gt 45) -and ((Get-EditorProcesses).Count -eq 0)) {
        Write-Status -Name $name -State 'failed' -Message 'The editor process exited during startup. Check the editor log (and any dialog Cowork can see).' -LogPath $editorLog -Details @{ logTail = (Get-LogTail $editorLog 40) }
        exit 1
    }
    Start-Sleep -Seconds 5
}

$hint = 'Still starting. Run tools/launch-editor.ps1 again to keep waiting.'
if (Test-Path $editorLog) {
    $mcpLines = @(Select-String -Path $editorLog -Pattern 'ModelContextProtocol' -ErrorAction SilentlyContinue | Select-Object -Last 5 | ForEach-Object { $_.Line })
    if ($mcpLines.Count -eq 0) { $hint += ' No ModelContextProtocol lines in the log yet: if the editor UI is fully open, a dialog may be waiting (Cowork: look at the screen), or the plugin is not enabled (tools/configure-unreal.ps1).' }
}
Write-Status -Name $name -State 'starting' -Message $hint -LogPath $editorLog -Details @{ startedNow = $startedNow; url = $url }
exit 2

# tools/configure-unreal.ps1 - enables the plugins and settings this pipeline needs. Idempotent.
#  - .uproject: ModelContextProtocol (Unreal MCP), AllToolsets, PythonScriptPlugin, EditorScriptingUtilities
#  - Config/DefaultEngine.ini: Python Developer Mode (API stub file) + Python Remote Execution (backup bridge)
#  - Config/DefaultEngine.ini: best-effort MCP auto-start (tools/launch-editor.ps1 passes the start flag anyway)
# Run with the editor CLOSED.
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'configure-unreal'

if ((Get-EditorProcesses).Count -gt 0) {
    Write-Status -Name $name -State 'failed' -Message 'Close the Unreal Editor first (tools/stop-editor.ps1), then rerun.'
    exit 3
}

try { $uproject = Get-ProjectFile } catch { Write-Status -Name $name -State 'failed' -Message $_.Exception.Message; exit 1 }
$backupDir = Get-LogDir 'backup'
Copy-Item -Path $uproject -Destination (Join-Path $backupDir ((Split-Path -Leaf $uproject) + '.' + (Get-Timestamp) + '.bak')) -Force

$json = Get-Content -Path $uproject -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not ($json.PSObject.Properties.Name -contains 'Plugins')) {
    $json | Add-Member -NotePropertyName 'Plugins' -NotePropertyValue @()
}
$plugins = @($json.Plugins | Where-Object { $_ })
$changed = @()
foreach ($pn in @('ModelContextProtocol', 'AllToolsets', 'PythonScriptPlugin', 'EditorScriptingUtilities')) {
    $existing = $plugins | Where-Object { $_.Name -eq $pn } | Select-Object -First 1
    if ($existing) {
        if (-not ($existing.PSObject.Properties.Name -contains 'Enabled') -or (-not $existing.Enabled)) {
            $existing | Add-Member -NotePropertyName 'Enabled' -NotePropertyValue $true -Force
            $changed += $pn
        }
    } else {
        $plugins += [pscustomobject]@{ Name = $pn; Enabled = $true }
        $changed += $pn
    }
}
$json.Plugins = $plugins
Write-TextFile $uproject ($json | ConvertTo-Json -Depth 20)

$ini = Join-Path (Get-RepoRoot) 'Config\DefaultEngine.ini'
Set-IniValue $ini '/Script/PythonScriptPlugin.PythonScriptPluginSettings' 'bDeveloperMode' 'True'
Set-IniValue $ini '/Script/PythonScriptPlugin.PythonScriptPluginSettings' 'bRemoteExecution' 'True'
Set-IniValue $ini '/Script/ModelContextProtocolEngine.ModelContextProtocolSettings' 'bAutoStartServer' 'True'

# Validate the .uproject is still valid JSON
try { $null = Get-Content -Path $uproject -Raw -Encoding UTF8 | ConvertFrom-Json } catch {
    Write-Status -Name $name -State 'failed' -Message ('The .uproject is no longer valid JSON; restore it from ' + $backupDir)
    exit 1
}
$msg = 'Plugins enabled: ModelContextProtocol, AllToolsets, PythonScriptPlugin, EditorScriptingUtilities. Changed now: ' + ($(if ($changed.Count) { $changed -join ', ' } else { 'none' })) + '. Python Developer Mode + Remote Execution set in DefaultEngine.ini. Next: tools/build.ps1'
Write-Status -Name $name -State 'succeeded' -Message $msg -Details @{ uproject = $uproject; ini = $ini; changed = $changed }
exit 0

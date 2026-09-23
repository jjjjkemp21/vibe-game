# tools/doctor.ps1 - checks the toolchain, picks free ports, writes tools\local.settings.json.
# Safe to re-run any time. Exit 0 = everything critical found.
param([switch]$Quiet)
. (Join-Path $PSScriptRoot '_common.ps1')
Update-SessionPath
$name = 'doctor'
Write-Status -Name $name -State 'running' -Message 'Checking toolchain'

$report = [ordered]@{}
$problems = New-Object System.Collections.ArrayList
$warnings = New-Object System.Collections.ArrayList

try { $report.project = Get-ProjectFile; $report.editorTarget = Get-EditorTargetName } catch { [void]$problems.Add($_.Exception.Message) }

try {
    $report.engineDir = Get-EngineDir
    foreach ($f in @((Get-EditorExe), (Get-EditorCmdExe), (Get-BuildBat))) { if (-not (Test-Path $f)) { [void]$problems.Add('Missing: ' + $f) } }
} catch { [void]$problems.Add($_.Exception.Message) }

try {
    $blender = Get-BlenderExe
    $report.blenderExe = $blender
    $ver = & $blender --version 2>$null | Select-Object -First 1
    $report.blenderVersion = [string]$ver
    if ($ver -match 'Blender\s+(\d+)\.(\d+)') {
        $maj = [int]$Matches[1]; $min = [int]$Matches[2]
        if (($maj -lt 5) -or (($maj -eq 5) -and ($min -lt 1))) { [void]$problems.Add('Blender ' + $maj + '.' + $min + ' is too old; need 5.1+ (5.2 LTS recommended).') }
    } else { [void]$problems.Add('Could not read the Blender version.') }
} catch { [void]$problems.Add($_.Exception.Message) }

function Get-ToolLine([string]$Exe, [string[]]$ArgList) {
    $c = Get-Command $Exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $c) { return $null }
    try { return ([string](& $c.Source @ArgList 2>$null | Select-Object -First 1)).Trim() } catch { return $null }
}
$report.git = Get-ToolLine 'git' @('--version')
$report.gitLfs = Get-ToolLine 'git' @('lfs', 'version')
$report.uv = Get-ToolLine 'uv' @('--version')
$report.claude = Get-ToolLine 'claude' @('--version')
if (-not $report.git) { [void]$problems.Add('git not found on PATH.') }
if (-not $report.gitLfs) { [void]$problems.Add('git lfs not available.') }
if (-not $report.uv) { [void]$warnings.Add('uv not found on PATH (needed for the Blender MCP server).') }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -products * -version '[17.14,19.0)' -requires Microsoft.VisualStudio.Workload.NativeGame Microsoft.VisualStudio.Workload.NativeDesktop -property installationVersion 2>$null | Select-Object -First 1
    $report.visualStudio = [string]$vs
    if (-not $vs) { [void]$problems.Add('No Visual Studio 2022 17.14+ / 2026 instance with the C++ game workloads.') }
} else { [void]$problems.Add('Visual Studio Installer (vswhere) not found.') }

# Ports: Unreal MCP (default 8000) and the Blender add-on socket (default 9876)
$port = Get-UeMcpPort
$owner = Get-PortOwner $port
if ($owner -and ($owner -ne 'UnrealEditor')) {
    $newPort = $null
    foreach ($candidate in 8010..8030) { if (-not (Get-PortOwner $candidate)) { $newPort = $candidate; break } }
    if ($newPort) { [void]$warnings.Add('Port ' + $port + ' is used by ' + $owner + '; switching Unreal MCP to ' + $newPort + '.'); $port = $newPort }
    else { [void]$problems.Add('No free port found for the Unreal MCP server.') }
}
$report.ueMcpPort = $port
$blenderOwner = Get-PortOwner 9876
if ($blenderOwner -and ($blenderOwner -ne 'blender')) { [void]$warnings.Add('Port 9876 (Blender MCP add-on default) is used by ' + $blenderOwner + '. Configure the add-on to another port if live Blender MCP fails.') }

# Persist settings
$settings = [ordered]@{
    engineDir = $report.engineDir
    blenderExe = $report.blenderExe
    ueMcpPort = $port
    updatedAt = (Get-Date).ToString('s')
}
Write-TextFile (Get-LocalSettingsPath) ($settings | ConvertTo-Json -Depth 4)

# Keep .mcp.json's unreal-mcp URL in sync with the chosen port
$mcpPath = Join-Path (Get-RepoRoot) '.mcp.json'
if (Test-Path $mcpPath) {
    try {
        $mcp = Get-Content -Path $mcpPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $want = 'http://127.0.0.1:' + $port + '/mcp'
        if ($mcp.mcpServers -and ($mcp.mcpServers.PSObject.Properties.Name -contains 'unreal-mcp')) {
            if ($mcp.mcpServers.'unreal-mcp'.url -ne $want) {
                $mcp.mcpServers.'unreal-mcp'.url = $want
                Write-TextFile $mcpPath ($mcp | ConvertTo-Json -Depth 10)
                [void]$warnings.Add('.mcp.json updated to ' + $want + ' - restart Claude Code to pick it up.')
            }
        }
    } catch { [void]$warnings.Add('Could not parse .mcp.json: ' + $_.Exception.Message) }
}

$report.problems = @($problems)
$report.warnings = @($warnings)
$reportPath = Join-Path (Get-LogDir 'doctor') 'doctor.json'
Write-TextFile $reportPath ($report | ConvertTo-Json -Depth 6)
if (-not $Quiet) { Write-Host ($report | ConvertTo-Json -Depth 6) }

if ($problems.Count -gt 0) {
    Write-Status -Name $name -State 'failed' -Message ('Problems: ' + ($problems -join ' | ')) -LogPath $reportPath -Details $report
    exit 1
}
Write-Status -Name $name -State 'succeeded' -Message ('Toolchain OK. Unreal MCP port ' + $port + '.') -LogPath $reportPath -Details $report
exit 0

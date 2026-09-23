# tools/blender-run.ps1 - runs a Blender asset recipe HEADLESS (no Blender window).
# Example: tools/blender-run.ps1 -Recipe art/recipes/sm_golden_crate.py
# The recipe exports to art/export/<Category>/<Asset>.fbx, renders Saved/AgentLogs/previews/<Asset>.png,
# and prints RESULT_JSON (saved to Saved/AgentLogs/blender/<recipe>.result.json). Recipes can run in parallel.
param(
    [Parameter(Mandatory = $true)][string]$Recipe,
    [string]$Out = '',
    [string]$Preview = '',
    [switch]$SaveBlend,
    [int]$TimeoutMinutes = 20
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'blender-run'

$recipePath = $Recipe
if (-not [IO.Path]::IsPathRooted($recipePath)) { $recipePath = Join-Path (Get-RepoRoot) $Recipe }
if (-not (Test-Path $recipePath)) { Write-Status -Name $name -State 'failed' -Message ('Recipe not found: ' + $recipePath); exit 1 }
$recipePath = (Resolve-Path $recipePath).Path
try { $blender = Get-BlenderExe } catch { Write-Status -Name $name -State 'failed' -Message $_.Exception.Message; exit 1 }

$recipeName = [IO.Path]::GetFileNameWithoutExtension($recipePath)
$logBase = Join-Path (Get-LogDir 'blender') ($recipeName + '-' + (Get-Timestamp))
$argList = '--background --factory-startup --python-exit-code 1 --python "' + $recipePath + '" --'
if ($Out) { $argList += ' --out "' + $Out + '"' }
if ($Preview) { $argList += ' --preview "' + $Preview + '"' }
if ($SaveBlend) { $argList += ' --save-blend' }

Write-Status -Name $name -State 'running' -Message ('Running recipe ' + $recipeName) -LogPath ($logBase + '.out.log')
$r = Invoke-Logged -FilePath $blender -ArgumentList $argList -LogBase $logBase -TimeoutMinutes $TimeoutMinutes

$resultJson = $null
if (Test-Path $r.OutLog) {
    $hit = Select-String -Path $r.OutLog -Pattern '^RESULT_JSON:(.*)$' -ErrorAction SilentlyContinue | Select-Object -Last 1
    if ($hit) { $resultJson = $hit.Matches[0].Groups[1].Value.Trim() }
}
if (($r.ExitCode -eq 0) -and $resultJson -and (-not $r.TimedOut)) {
    $resultPath = Join-Path (Get-LogDir 'blender') ($recipeName + '.result.json')
    Write-TextFile $resultPath $resultJson
    Write-Status -Name $name -State 'succeeded' -Message ('Recipe OK. Result: ' + $resultPath + '. LOOK at the preview image before continuing.') -LogPath $r.OutLog
    Write-Host $resultJson
    exit 0
}
Write-Status -Name $name -State 'failed' -Message ('Recipe failed (exit ' + $r.ExitCode + ', timedOut=' + $r.TimedOut + ').') -LogPath $r.OutLog
Write-Host (Get-LogTail $r.OutLog 60)
Write-Host (Get-LogTail $r.ErrLog 30)
exit 1

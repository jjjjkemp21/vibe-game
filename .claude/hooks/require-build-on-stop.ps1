# Stop hook: if C++ sources changed after the last successful tools/build.ps1, ask Claude (once per change) to
# compile and test before finishing. Exit 2 = keep working (stderr goes to Claude). Loop-safe.
# Windows PowerShell 5.1 compatible. ASCII only.
$ErrorActionPreference = 'Continue'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
} catch { exit 0 }
if ($data -and ($data.PSObject.Properties.Name -contains 'stop_hook_active') -and $data.stop_hook_active) { exit 0 }

$root = $env:CLAUDE_PROJECT_DIR
if ((-not $root) -and $data -and ($data.PSObject.Properties.Name -contains 'cwd')) { $root = [string]$data.cwd }
if (-not $root) { $root = (Get-Location).Path }
$src = Join-Path $root 'Source'
if (-not (Test-Path $src)) { exit 0 }

$newest = Get-ChildItem -Path $src -Recurse -File -Include '*.h', '*.hpp', '*.cpp', '*.inl', '*.cs' -ErrorAction SilentlyContinue |
    Sort-Object -Property LastWriteTimeUtc -Descending | Select-Object -First 1
if (-not $newest) { exit 0 }

$stateDir = Join-Path $root 'Saved\AgentState'
$stamp = Join-Path $stateDir 'last-successful-build.txt'
$stampTime = [datetime]::MinValue
if (Test-Path $stamp) { $stampTime = (Get-Item $stamp).LastWriteTimeUtc }
if ($newest.LastWriteTimeUtc -le $stampTime) { exit 0 }

New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
$marker = Join-Path $stateDir 'stop-hook-nag.txt'
$key = $newest.FullName + '|' + $newest.LastWriteTimeUtc.Ticks
if ((Test-Path $marker) -and (((Get-Content -Path $marker -Raw) -as [string]).Trim() -eq $key)) { exit 0 }
Set-Content -Path $marker -Value $key -Encoding ASCII

[Console]::Error.WriteLine('C++ source changed after the last successful build (newest: ' + $newest.Name + '). Before finishing: compile (tools/build.ps1 with the editor closed, or Live Coding for edits inside existing .cpp function bodies), run the relevant automation tests, and report the evidence. If you deliberately stopped mid-change or already compiled with Live Coding, say so explicitly and stop.')
exit 2

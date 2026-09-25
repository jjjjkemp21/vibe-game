# tools/_common.ps1 - shared helpers for the pipeline scripts. Dot-source it: . (Join-Path $PSScriptRoot '_common.ps1')
# Windows PowerShell 5.1 compatible. ASCII only.

$ErrorActionPreference = 'Continue'
$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Get-RepoRoot { return $script:RepoRoot }

function Update-SessionPath {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Get-ProjectFile {
    $f = Get-ChildItem -Path $script:RepoRoot -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $f) { throw ('No .uproject file found in ' + $script:RepoRoot) }
    return $f.FullName
}

function Get-ProjectName { return [IO.Path]::GetFileNameWithoutExtension((Get-ProjectFile)) }

function Get-LocalSettingsPath { return (Join-Path $script:RepoRoot 'tools\local.settings.json') }

function Get-LocalSettings {
    $p = Get-LocalSettingsPath
    if (Test-Path $p) {
        try { return (Get-Content -Path $p -Raw -Encoding UTF8 | ConvertFrom-Json) } catch { return $null }
    }
    return $null
}

function Get-SettingValue([string]$Name) {
    $s = Get-LocalSettings
    if ($s -and ($s.PSObject.Properties.Name -contains $Name)) { return $s.$Name }
    return $null
}

function Test-EngineDir([string]$Dir) {
    if (-not $Dir) { return $false }
    return (Test-Path (Join-Path $Dir 'Engine\Binaries\Win64\UnrealEditor.exe'))
}

function Get-EngineDir {
    if (Test-EngineDir $env:UE_ENGINE_DIR) { return $env:UE_ENGINE_DIR }
    $fromSettings = Get-SettingValue 'engineDir'
    if (Test-EngineDir $fromSettings) { return $fromSettings }
    $dat = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path $dat) {
        try {
            $j = Get-Content -Path $dat -Raw | ConvertFrom-Json
            foreach ($i in @($j.InstallationList)) {
                if (($i.AppName -eq 'UE_5.8') -and (Test-EngineDir $i.InstallLocation)) { return $i.InstallLocation }
            }
        } catch { }
    }
    $regKey = 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8'
    if (Test-Path $regKey) {
        $p = Get-ItemProperty -Path $regKey -ErrorAction SilentlyContinue
        if ($p -and ($p.PSObject.Properties.Name -contains 'InstalledDirectory') -and (Test-EngineDir $p.InstalledDirectory)) { return $p.InstalledDirectory }
    }
    $default = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8'
    if (Test-EngineDir $default) { return $default }
    throw 'Unreal Engine 5.8 was not found. Install it from the Epic Games Launcher, or set engineDir in tools\local.settings.json.'
}

function Get-EditorExe    { return (Join-Path (Get-EngineDir) 'Engine\Binaries\Win64\UnrealEditor.exe') }
function Get-EditorCmdExe { return (Join-Path (Get-EngineDir) 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') }
function Get-BuildBat     { return (Join-Path (Get-EngineDir) 'Engine\Build\BatchFiles\Build.bat') }

function Get-EditorTargetName {
    $src = Join-Path $script:RepoRoot 'Source'
    $t = Get-ChildItem -Path $src -Filter '*Editor.Target.cs' -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($t) { return ($t.Name -replace '\.Target\.cs$', '') }
    return ((Get-ProjectName) + 'Editor')
}

function Get-BlenderExe {
    $candidates = @()
    if ($env:BLENDER_EXE) { $candidates += $env:BLENDER_EXE }
    $fromSettings = Get-SettingValue 'blenderExe'
    if ($fromSettings) { $candidates += $fromSettings }
    $root = Join-Path $env:ProgramFiles 'Blender Foundation'
    if (Test-Path $root) {
        $dirs = Get-ChildItem -Path $root -Directory -Filter 'Blender*' -ErrorAction SilentlyContinue |
            Sort-Object -Property @{ Expression = { $v = ($_.Name -replace '[^0-9\.]', ''); try { [version]$v } catch { [version]'0.0' } } } -Descending
        foreach ($d in $dirs) { $candidates += (Join-Path $d.FullName 'blender.exe') }
    }
    $cmd = Get-Command 'blender.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { $candidates += $cmd.Source }
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    throw 'blender.exe not found. Install Blender 5.2 LTS, or set blenderExe in tools\local.settings.json.'
}

function Get-UeMcpPort {
    $p = Get-SettingValue 'ueMcpPort'
    if ($p) { return [int]$p }
    return 8000
}

function Get-LogDir([string]$Sub) {
    $d = Join-Path $script:RepoRoot 'Saved\AgentLogs'
    if ($Sub) { $d = Join-Path $d $Sub }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    return $d
}

function Get-Timestamp { return (Get-Date).ToString('yyyyMMdd-HHmmss') }

function Write-TextFile([string]$Path, [string]$Text) {
    $dir = Split-Path -Parent $Path
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Write-Status {
    param([string]$Name, [string]$State, [string]$Message = '', [string]$LogPath = '', $Details = $null)
    $obj = [ordered]@{
        script = $Name; state = $State; message = $Message; logPath = $LogPath
        updatedAt = (Get-Date).ToString('s'); details = $Details
    }
    $path = Join-Path (Get-LogDir 'status') ($Name + '.json')
    Write-TextFile $path ($obj | ConvertTo-Json -Depth 8)
    Write-Host ('[' + $Name + '] ' + $State + ': ' + $Message)
}

# --- board.py hooks (docs/tools/board.md "Integrations") ---
# The DB is main's Saved/Studio/board.db (BOARD_DB overrides it, e.g. in tests). Every hook is a silent no-op while the
# DB file is missing: board.py creates the DB on ANY command (even ls), so never call it without Test-BoardDb.
function Get-BoardDbPath {
    if ($env:BOARD_DB) { return $env:BOARD_DB }
    return (Join-Path $script:RepoRoot 'Saved\Studio\board.db')
}

function Test-BoardDb { return (Test-Path -LiteralPath (Get-BoardDbPath) -PathType Leaf) }

# Runs python tools/board.py <args>. Returns $null when the DB is missing, else ExitCode / Out (stdout lines) / Err.
function Invoke-Board([string[]]$BoardArgs) {
    if (-not (Test-BoardDb)) { return $null }
    $ErrorActionPreference = 'Continue'   # local: stderr of a native command must not throw under a caller's 'Stop'
    $boardPy = Join-Path $script:RepoRoot 'tools\board.py'
    # PS 5.1 drops embedded double quotes when it builds a native command line.
    $safe = @($BoardArgs | ForEach-Object { ([string]$_).Replace('"', "'") })
    $enc = $null
    try { $enc = [Console]::OutputEncoding; [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { $enc = $null }
    try {
        $all = @(& python $boardPy @safe 2>&1)
        $code = $LASTEXITCODE
    } catch {
        $all = @($_.ToString()); $code = -1
    } finally {
        if ($enc) { try { [Console]::OutputEncoding = $enc } catch { } }
    }
    $out = @($all | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] } | ForEach-Object { [string]$_ })
    $err = @($all | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] } | ForEach-Object { $_.ToString() })
    return [pscustomobject]@{ ExitCode = $code; Out = $out; Err = (($err -join ' ').Trim()) }
}

# board.py <args> --json parsed into a flat array of objects; @() when the DB is missing or the call failed.
function Get-BoardJson([string[]]$BoardArgs) {
    $r = Invoke-Board (@($BoardArgs) + @('--json'))
    if (($null -eq $r) -or ($r.ExitCode -ne 0) -or ($r.Out.Count -eq 0)) { return @() }
    try { $v = ($r.Out -join "`n") | ConvertFrom-Json } catch { return @() }
    return @($v | ForEach-Object { $_ })   # PS 5.1 emits a JSON array as one object: enumerate it
}

# Runs a native program with stdout/stderr redirected to files. Returns ExitCode/TimedOut/OutLog/ErrLog.
function Invoke-Logged {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string]$ArgumentList = '',
        [Parameter(Mandatory = $true)][string]$LogBase,
        [int]$TimeoutMinutes = 60
    )
    $out = $LogBase + '.out.log'
    $err = $LogBase + '.err.log'
    $startArgs = @{ FilePath = $FilePath; NoNewWindow = $true; PassThru = $true; RedirectStandardOutput = $out; RedirectStandardError = $err }
    if ($ArgumentList) { $startArgs['ArgumentList'] = $ArgumentList }
    $p = Start-Process @startArgs
    $null = $p.Handle
    $finished = $p.WaitForExit($TimeoutMinutes * 60 * 1000)
    if (-not $finished) {
        & taskkill.exe /PID $p.Id /T /F 2>$null | Out-Null
        return [pscustomobject]@{ ExitCode = -999; TimedOut = $true; OutLog = $out; ErrLog = $err }
    }
    $p.WaitForExit()
    return [pscustomobject]@{ ExitCode = $p.ExitCode; TimedOut = $false; OutLog = $out; ErrLog = $err }
}

function Get-LogTail([string]$Path, [int]$Lines = 60) {
    if ($Path -and (Test-Path $Path)) { return ((Get-Content -Path $Path -Tail $Lines -ErrorAction SilentlyContinue) -join "`n") }
    return ''
}

# UnrealEditor.exe processes that have THIS project open.
function Get-EditorProcesses {
    # Editors that have THIS checkout's .uproject open (full path), so an editor running on the main
    # checkout does not block builds in a worktree lane (C:\GameDev\VibeGame-lanes\<lane>) and vice versa.
    $want = (Get-ProjectFile).Replace('/', '\')
    $mine = @()
    foreach ($p in @(Get-CimInstance -ClassName Win32_Process -Filter "Name = 'UnrealEditor.exe'" -ErrorAction SilentlyContinue)) {
        if ($p.CommandLine -and ($p.CommandLine.Replace('/', '\') -like ('*' + $want + '*'))) { $mine += $p }
    }
    return ,$mine
}

function Test-PortListening([int]$Port) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne(700)
        if ($ok -and $client.Connected) { $client.EndConnect($iar); return $true }
        return $false
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Get-PortOwner([int]$Port) {
    $c = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $c) { return $null }
    $proc = Get-Process -Id $c.OwningProcess -ErrorAction SilentlyContinue
    if ($proc) { return $proc.ProcessName }
    return ('pid ' + $c.OwningProcess)
}

function Set-IniValue([string]$Path, [string]$Section, [string]$Key, [string]$Value) {
    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path $Path) { foreach ($l in @(Get-Content -Path $Path -Encoding UTF8)) { $lines.Add([string]$l) } }
    $header = '[' + $Section + ']'
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq $header) { $start = $i; break } }
    if ($start -lt 0) {
        if (($lines.Count -gt 0) -and ($lines[$lines.Count - 1].Trim() -ne '')) { $lines.Add('') }
        $lines.Add($header)
        $lines.Add($Key + '=' + $Value)
    } else {
        $end = $lines.Count
        for ($j = $start + 1; $j -lt $lines.Count; $j++) { if ($lines[$j].Trim().StartsWith('[')) { $end = $j; break } }
        $found = $false
        for ($k = $start + 1; $k -lt $end; $k++) {
            if ($lines[$k] -match ('^\s*' + [regex]::Escape($Key) + '\s*=')) { $lines[$k] = ($Key + '=' + $Value); $found = $true; break }
        }
        if (-not $found) { $lines.Insert($start + 1, ($Key + '=' + $Value)) }
    }
    Write-TextFile $Path ((($lines.ToArray()) -join "`r`n") + "`r`n")
}

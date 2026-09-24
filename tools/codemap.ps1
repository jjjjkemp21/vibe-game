# tools/codemap.ps1 - regenerates the generated block of docs/CODEMAP.md (the code map agents read first).
# Rewrites only the text between <!-- codemap:generated:begin --> and <!-- codemap:generated:end -->; the hand-written
# top part stays. One line per item, each <= 140 characters, sorted ordinally so reruns only diff on real changes.
# Lists: Source/VibeGame headers (no Variant_*, no Tests/), test folders with their name prefixes and counts,
# data/tables with their row structs, docs/specs and docs/levels with their first heading.
# Text only (no build, no editor). Writes Saved/AgentLogs/status/codemap.json.
# Windows PowerShell 5.1 compatible. ASCII only.
param(
    [switch]$Check   # do not write; exit 1 if docs/CODEMAP.md is out of date
)
. (Join-Path $PSScriptRoot '_common.ps1')
$name = 'codemap'
$root = Get-RepoRoot
$src = Join-Path $root 'Source\VibeGame'
$mapPath = Join-Path $root 'docs\CODEMAP.md'
$maxLen = 140
$beginMark = '<!-- codemap:generated:begin -->'
$endMark = '<!-- codemap:generated:end -->'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-Rel([string]$Full, [string]$Base) {
    return $Full.Substring($Base.Length).TrimStart('\', '/').Replace('\', '/')
}

function Limit([string]$Line) {
    $Line = $Line.TrimEnd()
    if ($Line.Length -le $maxLen) { return $Line }
    return $Line.Substring(0, $maxLen - 3).TrimEnd() + '...'
}

function Sort-Ordinal([string[]]$Items) {
    if (-not $Items -or $Items.Count -eq 0) { return @() }
    $a = [string[]]$Items.Clone()
    [Array]::Sort($a, [StringComparer]::Ordinal)
    return $a
}

function Read-Text([string]$Path) { return [IO.File]::ReadAllText($Path, $utf8) }

$out = New-Object System.Collections.Generic.List[string]

# --- Headers -------------------------------------------------------------------------------------------------------
$headerFiles = @(Get-ChildItem -Path $src -Recurse -Filter '*.h' -File | Where-Object {
    $rel = Get-Rel $_.FullName $src
    -not ($rel -like 'Variant_*') -and -not ($rel -like 'Tests/*')
})
$headerText = @{}   # rel path -> text (reused for the row-struct lookup)
$headerLines = @()
foreach ($f in $headerFiles) {
    $rel = Get-Rel $f.FullName $src
    $text = Read-Text $f.FullName
    $headerText[$rel] = $text
    $lines = $text -split "`r?`n"

    # Reflected type names: the class/struct/enum declared after each UCLASS/USTRUCT/UENUM/UINTERFACE macro.
    $types = New-Object System.Collections.Generic.List[string]
    $pending = $false
    foreach ($l in $lines) {
        if ($l -match '^\s*(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\(') { $pending = $true; continue }
        if ($pending -and $l -match '^\s*(class|struct|enum)\s+(.*)$') {
            $toks = @($Matches[2] -split '[\s:{;]+' | Where-Object { $_ -and $_ -ne 'class' -and $_ -notmatch '_API$' })
            if ($toks.Count -gt 0) { $types.Add($toks[0]) }
            $pending = $false
        }
    }

    # Purpose: the first // line in the top 15 lines, minus the "Lure: " prefix; copyright boilerplate counts as none.
    $purpose = ''
    $top = [Math]::Min(15, $lines.Count)
    for ($i = 0; $i -lt $top; $i++) {
        if ($lines[$i] -match '^\s*//\s?(.*)$') {
            $c = $Matches[1].Trim()
            if ($c -match '^Copyright') { break }
            $purpose = ($c -replace '^Lure:\s*', '')
            break
        }
    }
    $line = '- `' + $rel + '`'
    if ($types.Count -gt 0) { $line += ' ' + ($types -join ', ') }
    if ($purpose) { $line += ' | ' + $purpose }
    $headerLines += (Limit $line)
}
$out.Add('### Headers (`Source/VibeGame/`, no Variant_* or Tests/): path, reflected types | purpose')
foreach ($l in (Sort-Ordinal $headerLines)) { $out.Add($l) }

# --- Tests ---------------------------------------------------------------------------------------------------------
$testsDir = Join-Path $src 'Tests'
$testRx = New-Object Text.RegularExpressions.Regex('IMPLEMENT_\w*AUTOMATION_TEST\s*\([^"]*"([^"]+)"')
$byFolder = @{}   # folder -> @{ 'Project.X' -> @{ third -> count } }
$testTotal = 0
foreach ($f in @(Get-ChildItem -Path $testsDir -Recurse -Filter '*.cpp' -File)) {
    $relDir = Get-Rel $f.DirectoryName $src
    if (-not $byFolder.ContainsKey($relDir)) { $byFolder[$relDir] = @{} }
    foreach ($m in $testRx.Matches((Read-Text $f.FullName))) {
        $seg = $m.Groups[1].Value.Split('.')
        $two = ($seg[0..([Math]::Min(1, $seg.Count - 1))] -join '.')
        $third = ''
        if ($seg.Count -ge 3) { $third = $seg[2] }
        if (-not $byFolder[$relDir].ContainsKey($two)) { $byFolder[$relDir][$two] = @{} }
        $g = $byFolder[$relDir][$two]
        if ($g.ContainsKey($third)) { $g[$third]++ } else { $g[$third] = 1 }
        $testTotal++
    }
}
$testLines = @()
foreach ($dir in $byFolder.Keys) {
    foreach ($two in $byFolder[$dir].Keys) {
        $g = $byFolder[$dir][$two]
        $n = 0; foreach ($v in $g.Values) { $n += $v }
        # Third segments by count (desc), then name (ordinal): deterministic, the biggest groups survive truncation.
        $parts = @($g.Keys | ForEach-Object { New-Object psobject -Property @{ K = $_; N = $g[$_] } })
        $parts = @($parts | Sort-Object -Property @{ Expression = { -$_.N } }, @{ Expression = { $_.K } } -CaseSensitive)
        $names = @($parts | ForEach-Object { if ($_.K) { $_.K + ' ' + $_.N } else { '(no 3rd) ' + $_.N } })
        $testLines += (Limit ('- `' + $dir + '/` ' + $two + '.* (' + $n + '): ' + ($names -join ', ')))
    }
}
$out.Add('')
$out.Add('### Tests (`Source/VibeGame/`): folder, prefix (count): 3rd name segment count. Total ' + $testTotal)
foreach ($l in (Sort-Ordinal $testLines)) { $out.Add($l) }

# --- Data tables ---------------------------------------------------------------------------------------------------
$tableLines = @()
foreach ($f in @(Get-ChildItem -Path (Join-Path $root 'data\tables') -File)) {
    $tname = [IO.Path]::GetFileNameWithoutExtension($f.Name) -replace '^DT_', ''
    $found = ''
    foreach ($cand in @(('F' + $tname + 'Row'), ('FLure' + $tname + 'Row'))) {
        foreach ($rel in (Sort-Ordinal @($headerText.Keys))) {
            if ($headerText[$rel] -match ('(?m)^\s*struct\s+(\w+_API\s+)?' + $cand + '\s*:')) { $found = $cand + ' (' + $rel + ')'; break }
        }
        if ($found) { break }
    }
    if (-not $found) { $found = '(row struct not found)' }
    $tableLines += (Limit ('- `data/tables/' + $f.Name + '` ' + $found))
}
$out.Add('')
$out.Add('### Data tables (`data/tables/`, imported to `/Game/Data/DT_*`): file, row struct (header)')
foreach ($l in (Sort-Ordinal $tableLines)) { $out.Add($l) }

# --- Specs and level plans -----------------------------------------------------------------------------------------
$docLines = @()
foreach ($d in @('docs\specs', 'docs\levels')) {
    $dp = Join-Path $root $d
    if (-not (Test-Path $dp)) { continue }
    foreach ($f in @(Get-ChildItem -Path $dp -Filter '*.md' -File)) {
        $h = ''
        foreach ($l in [IO.File]::ReadAllLines($f.FullName, $utf8)) {
            if ($l -match '^#+\s+(.*)$') { $h = $Matches[1].Trim(); break }
        }
        $docLines += (Limit ('- `' + (Get-Rel $f.FullName $root) + '` ' + $h))
    }
}
$out.Add('')
$out.Add('### Specs and level plans: path, first heading')
foreach ($l in (Sort-Ordinal $docLines)) { $out.Add($l) }

# --- Splice into docs/CODEMAP.md -----------------------------------------------------------------------------------
if (-not (Test-Path $mapPath)) {
    Write-Status -Name $name -State 'failed' -Message "docs/CODEMAP.md not found"
    exit 1
}
$old = Read-Text $mapPath
$bi = $old.IndexOf($beginMark)
$ei = $old.IndexOf($endMark)
if ($bi -lt 0 -or $ei -lt $bi) {
    Write-Status -Name $name -State 'failed' -Message "docs/CODEMAP.md has no $beginMark ... $endMark block"
    exit 1
}
$nl = "`n"
if ($old.Contains("`r`n")) { $nl = "`r`n" }
$block = $beginMark + $nl + '<!-- Generated by tools/codemap.ps1; do not edit by hand. -->' + $nl + ($out -join $nl) + $nl
$new = $old.Substring(0, $bi) + $block + $old.Substring($ei)
$changed = ($new -ne $old)
$details = [ordered]@{
    headers = $headerLines.Count; testLines = $testLines.Count; tests = $testTotal
    tables = $tableLines.Count; docs = $docLines.Count; chars = $new.Length; changed = $changed
}
if ($Check) {
    $state = 'succeeded'; if ($changed) { $state = 'failed' }
    Write-Status -Name $name -State $state -Message ("check: " + $(if ($changed) { 'docs/CODEMAP.md is out of date' } else { 'up to date' })) -LogPath $mapPath -Details $details
    exit $(if ($changed) { 1 } else { 0 })
}
if ($changed) { [IO.File]::WriteAllText($mapPath, $new, $utf8) }
$msg = "{0} headers, {1} test lines ({2} tests), {3} tables, {4} docs; CODEMAP {5} chars; {6}" -f `
    $headerLines.Count, $testLines.Count, $testTotal, $tableLines.Count, $docLines.Count, $new.Length, $(if ($changed) { 'updated' } else { 'unchanged' })
Write-Status -Name $name -State 'succeeded' -Message $msg -LogPath $mapPath -Details $details
exit 0

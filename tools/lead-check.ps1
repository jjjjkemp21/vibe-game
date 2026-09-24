# lead-check.ps1: the lead's housekeeping check (Jimmy, 2026-09-23: keep agent context small, clean up disk regularly).
# Reports:
#   - each subagent of the newest Claude session that wrote in the last -ActiveMinutes, with its context size;
#     flags HANDOFF when context > -ContextLimitK (250k; senior agents -SeniorContextLimitK 400k) (the lead then asks it to write a handoff and starts a fresh agent)
#   - Saved/ size per checkout (main + worktree lanes) and hours since the last janitor run;
#     flags JANITOR when any Saved/ > -SavedLimitMB or the last run is older than -JanitorHours
# Read-only. Writes Saved/AgentLogs/status/lead-check.json.
param(
    [int]$ActiveMinutes = 20,
    [int]$ContextLimitK = 250,
    [int]$SeniorContextLimitK = 400,  # *-senior-* agents (Jimmy, 2026-09-24)
    [int]$SavedLimitMB = 500,
    [double]$JanitorHours = 3
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$flags = @()
$lines = @()

# --- Agents: newest session's subagents folder under ~/.claude/projects/<repo path with :\ -> ->
$projName = ($repo -replace '[:\\/]', '-')
$projDir = Join-Path $env:USERPROFILE ".claude\projects\$projName"
$agents = @()
if (Test-Path $projDir) {
    $session = Get-ChildItem $projDir -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'subagents') } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($session) {
        $cutoff = (Get-Date).AddMinutes(-$ActiveMinutes)
        Get-ChildItem (Join-Path $session.FullName 'subagents') -Filter 'agent-*.jsonl' |
            Where-Object { $_.LastWriteTime -gt $cutoff } | ForEach-Object {
                $id = $_.BaseName -replace '^agent-', ''
                $metaPath = Join-Path $_.DirectoryName "agent-$id.meta.json"
                $meta = if (Test-Path $metaPath) { Get-Content $metaPath -Raw | ConvertFrom-Json } else { $null }
                # Read only the last 4 MB as raw bytes (Get-Content -Tail crawls on transcripts with multi-MB image lines),
                # then take the last usage block: context = input + cache read + cache creation tokens.
                $ctx = 0
                $fs = [IO.File]::Open($_.FullName, 'Open', 'Read', 'ReadWrite')
                try {
                    $len = [Math]::Min($fs.Length, 4MB)
                    [void]$fs.Seek(-$len, 'End')
                    $buf = New-Object byte[] $len
                    [void]$fs.Read($buf, 0, $len)
                } finally { $fs.Dispose() }
                $text = [Text.Encoding]::UTF8.GetString($buf)
                $u = [regex]::Matches($text, '"usage":\{"input_tokens":(\d+),"cache_creation_input_tokens":(\d+),"cache_read_input_tokens":(\d+)')
                if ($u.Count -gt 0) {
                    $g = $u[$u.Count - 1].Groups
                    $ctx = [int](([int64]$g[1].Value + [int64]$g[2].Value + [int64]$g[3].Value) / 1000)
                }
                # Finished (or waiting on its own background work): the last stop_reason is end_turn. Never flagged.
                $stops = [regex]::Matches($text, '"stop_reason":"(\w+)"')
                $done = $stops.Count -gt 0 -and $stops[$stops.Count - 1].Groups[1].Value -eq 'end_turn'
                $a = [ordered]@{ id = $id; type = $meta.agentType; description = $meta.description; contextK = $ctx
                                 lastWrite = $_.LastWriteTime.ToString('HH:mm'); done = $done }
                $agents += $a
                $limit = if ($meta.agentType -like '*-senior-*') { $SeniorContextLimitK } else { $ContextLimitK }
                $mark = if ($done) { 'done' } elseif ($ctx -gt $limit) { 'HANDOFF' } else { 'ok' }
                if ($mark -eq 'HANDOFF') { $flags += "HANDOFF $id ($($meta.agentType): $($meta.description)) ~${ctx}k" }
                $lines += ('{0,-8} {1,-18} ~{2,4}k  {3}  {4} [{5}]' -f $mark, $id, $ctx, $a.lastWrite, $meta.description, $meta.agentType)
            }
    }
}
if (-not $agents) { $lines += "no subagents active in the last $ActiveMinutes min" }

# --- Disk: Saved/ per checkout, last janitor run
$checkouts = @(git -C $repo worktree list --porcelain | Where-Object { $_ -like 'worktree *' } | ForEach-Object { $_.Substring(9) })
$disk = @()
foreach ($c in $checkouts) {
    $saved = Join-Path $c 'Saved'
    $mb = 0
    if (Test-Path $saved) {
        $mb = [int]((Get-ChildItem $saved -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1MB)
    }
    $disk += [ordered]@{ checkout = $c; savedMB = $mb }
    $lines += ('disk     {0,-40} {1,5} MB' -f $c, $mb)
}
$last = Get-ChildItem (Join-Path $repo 'Saved/AgentLogs/janitor') -Filter '*-deleted.txt' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$hours = if ($last) { [math]::Round(((Get-Date) - $last.LastWriteTime).TotalHours, 1) } else { 999 }
# Due when the last run is older than -JanitorHours, or a Saved/ is over the limit and the last run is over 1 h old
# (the janitor keeps the last 3 hours of files, so running it more often than hourly frees little).
$over = @($disk | Where-Object { $_.savedMB -gt $SavedLimitMB } | ForEach-Object { "$($_.checkout) $($_.savedMB) MB" })
if ($hours -gt $JanitorHours) { $flags += "JANITOR last run $hours h ago (> $JanitorHours h)" }
elseif ($over -and $hours -gt 1) { $flags += "JANITOR Saved over $SavedLimitMB MB: $($over -join ', ') (last run $hours h ago)" }
$lines += "janitor  last run $hours h ago"

$lines | ForEach-Object { Write-Output $_ }
if ($flags) { Write-Output '--- action needed:'; $flags | ForEach-Object { Write-Output $_ } } else { Write-Output '--- nothing to do' }

$statusDir = Join-Path $repo 'Saved/AgentLogs/status'
New-Item -ItemType Directory -Force $statusDir | Out-Null
[ordered]@{ script = 'lead-check'; state = 'succeeded'; message = ($(if ($flags) { $flags -join '; ' } else { 'nothing to do' }))
            updatedAt = (Get-Date).ToString('s'); agents = $agents; disk = $disk; janitorHoursAgo = $hours } |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $statusDir 'lead-check.json') -Encoding utf8

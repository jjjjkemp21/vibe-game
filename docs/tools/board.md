# board: the studio's work-item tracker (agents only; Jimmy approved 2026-09-24)

The single source of truth for the status of all work. It's a local SQLite database driven by one CLI and designed for AI agents rather than humans: terse, fixed-format output; validated state changes; instant answers to "what's next", "who owns this" and "what collides".

## Design principles (why it looks like this)
1. **Token-lean by default.** List output has one line per item, fixed column order, and no header or decoration. Titles are cut to 60 chars and agent ids to 7. Lists default to 20 rows and end with `+N more`. A full record comes only from `show`.
2. **Plain English keywords, one token each.** Statuses: `todo ready doing review blocked merge done dropped`. Depts: `lead eng art qa design`. Kinds: `obj task bug q` (q = a question for Jimmy). Levels: `junior mid senior`. No codes, no emoji, no box drawing: those cost tokens and get misread.
3. **Validated, forgiving writes.** A bad transition or a missing field is refused with a one-line error ending in the exact command that would work. Repeating a write that's already true is a silent no-op, so retries are safe.
4. **One shared, live truth.** The DB lives in the main checkout (`C:/GameDev/VibeGame/Saved/Studio/board.db`). Every lane calls the main copy of the tool by absolute path: `python C:/GameDev/VibeGame/tools/board.py <cmd>`. The tool refuses to run if its schema version is older than the DB's; the fix is always "use the main copy".
5. **Content lives in files; the board holds pointers.** Briefs, reports and handoffs stay in `Saved/AgentLogs/tasks/<id>/`. The board stores their paths, not their text.
6. **Nothing is ever lost.** Every write appends an event (who, when, from→to, note). `export` writes a readable snapshot and a full JSONL dump into git, so the DB can always be rebuilt with `import`.

## Data
- `items`:
  - identity: `id` (int), `key` (optional alias, e.g. `T-040a`), `kind`, `dept`, `title`, `parent` (objective id)
  - state: `status`, `pri` P0-P3, `sev` (bugs), `level`, `pct` 0-100
  - assignment: `owner` (a dept manager or lead), `agent` (agent id), `lane`
  - links: `milestone`, `packet` (task folder), `handoff`, `commit`, `evidence`
  - timestamps: `created`, `updated`
- `deps(item, needs)`: an item is blocked until everything it needs is `done`.
- `claims(item, path, symbol)`: the files (optionally `file:Function`) an open item intends to change.
- `events(id, item, ts, actor, verb, from, to, note)`: append-only.
- SQLite STRICT tables, WAL mode, busy_timeout 5 s, one transaction per command. `PRAGMA user_version` holds the schema version.

## Commands (`--json` on any read gives compact JSON for scripts; `--as <actor>` names the writer, e.g. `--as eng-mgr`)
| Command | Purpose |
|---|---|
| `ls [--dept --status --kind --parent --ms --agent --lane --level --all] [-n N]` | Open items (not done or dropped), sorted by status flow then priority |
| `next [--dept D] [--level L] [-n 5]` | Ready items whose deps are all done, best first: what to dispatch now |
| `show ID` | Every field, deps, claims and the last 5 events, as `key: value` lines |
| `new --kind K --dept D --title T [--parent P --pri --sev --level --key --ms --needs a,b --files f1,f2:Sym]` | Create an item; prints `#<id>` |
| `set ID k=v ...` | Change fields, including status transitions (validated) |
| `note ID "text"` | Add an event without changing state |
| `claim ID path[:Symbol] ...` / `unclaim ID [path]` | Declare file ownership; warns at once on overlap with another open item |
| `conflicts` | Every overlapping claim among open items |
| `resume` | The restart view: all doing/review/blocked/merge items with agent, lane, age, handoff and last event, plus orphans (items in doing whose agent can't still be running) |
| `tree ID` | An objective and its children, one line each |
| `log [ID] [-n 10]` | Recent events |
| `batch` | Read many commands from stdin, one per line, applied in ONE transaction (all or nothing) |
| `export` | Write `docs/BOARD.md` (readable summary by milestone and status) and `docs/board/items.jsonl` (full dump) |
| `import FILE` | Rebuild the DB from a JSONL dump |
| `selftest` | Run the unit tests on a temporary DB |

List line format (space-separated, fixed order):
`<id> <kind> <dept> <level|-> <status> <pri> <lane|-> <agent7|-> <pct|-> <key|-> <title>`
Example: `41 task eng mid doing P1 eng5 a771470 60% T-040a Line collides with dock planks`

## State machine (guards refuse with a fix hint)
- `todo → ready`. An item waiting on deps shows as `ready` but `next` skips it until its deps are done.
- `ready → doing`: needs `agent`. Code tasks (eng; also qa tests) also need `lane`.
- `doing → review`: needs `packet` (the report exists at `<packet>/report.md`) or a `note`.
- `review → merge`: code in a lane that is accepted and waiting for the lead's integration.
- `review|merge → done`: needs `commit` or `evidence`. `tools/integrate.ps1` sets this automatically.
- `todo|ready → done`: only for work merged before its item existed; the same `set` must give both `commit=` and a `note=`.
- Any open status → `blocked`: needs a note with the reason. Leaving `blocked` restores the previous status.
- Any status → `dropped`: needs a note.
- `done → doing` (rework): needs a note.
- `obj` items close only when every child is done or dropped.

## Who writes what
- Lead: creates objectives (`obj`) and questions for Jimmy (`q`); sets priorities; marks `done` after integration (automatic via integrate.ps1).
- Managers: create task and bug items under their objective, set level, deps and claims, and move items `ready → doing → review → merge`. The team log keeps only decisions, risks and notes; the task table moves to the board.
- Workers: one `note` or `set pct=` at real milestones, plus `set status=review` when they finish. Workers never create items.
- Write only when something changes. The live `[NN%]` status line stays in agents' text; the `pct` field is updated only at stage changes.

## Integrations
- `tools/integrate.ps1`: after the fast-forward, marks the `merge` items on the integrated lanes `done` with the commit and the test evidence line, then runs `export` and commits the two snapshot files.
- `tools/lead-check.ps1`: shows the board item (id and key) next to each running agent, and lists orphans.
- `tools/lane.ps1 -Free -Item ID`: records the lane on the item.

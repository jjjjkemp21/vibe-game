# LEAD.md: the lead's playbook (lead only)

The lead (the main Claude Code session) reads this at the start of every session. It holds Jimmy's rules about how the lead runs the team. Subagents don't need it, so it is kept out of CLAUDE.md to save every agent's tokens. Nothing here was dropped from CLAUDE.md; it was moved here on 2026-09-23 (Jimmy asked for lower-token agents).

## Session start
1. Read memory `lure-project-status` and the top Status block of docs/TASKS.md.
2. Schedule the housekeeping tick (see Housekeeping below). It is session-only.
3. `git worktree list` and each lane's `git log --oneline -3` to see the real state.

GitHub: remote `origin` = private repo https://github.com/jjjjkemp21/vibe-game (`gh` at `C:\Program Files\GitHub CLI\gh.exe`). Only the lead pushes `main`, and only after the release gate (CLAUDE.md rule 10, full checklist in the `verification` skill).

Lead-only scripts: `tools/lead-check.ps1` (running agents' context size + disk; flags HANDOFF / JANITOR) and `tools/codemap.ps1` (regenerates the generated part of docs/CODEMAP.md).

## Running the team
- The lead is the project manager: assigns tasks, briefs agents with the relevant vision and rules, verifies results, and keeps `docs/TASKS.md` current.
- Work log: when a task starts, the lead adds `In progress: <owner agent>, started <date>` to that task line in `docs/TASKS.md`; when it's done, the task moves to "Done" with its commit hash.
- Typical flow per task: implementer (unreal-engineer / model-artist / animation-artist / level-designer / editor-operator) -> qa-engineer tests -> playtester plays -> designer reviews -> lead fixes or accepts -> release gate before publishing. C++ work and Blender work (model-artist, animation-artist) can run in parallel; two Blender agents can too, as long as they work on different recipes.
- Lanes (lead side): the main checkout is the editor lane (editor-operator, playtester, art commits, integration by the lead). The lead merges a lane into `main` after its tests pass (`git merge main` in the lane, then `git merge --ff-only lane/<x>` in main), then rebuilds the main checkout (editor closed briefly) before editor or playtest work. Use as many lanes and agents as the work needs for speed and accuracy (Jimmy, 2026-09-23). Give each lane one task, and pick parallel tasks that don't edit the same files; if two must share a file (e.g. DT_Movement), keep the edits additive and say so in both briefs. Create a lane with `git worktree add ../VibeGame-lanes/<lane> -b lane/<lane> <base>` and copy `tools/local.settings.json` into it. A QA lane (`qa1`) exists for independent test work while the editor runs in main. Anything touching the running editor, or closing/building/relaunching it, is serialized by the lead.
- After each merge batch, rerun `tools/codemap.ps1` so docs/CODEMAP.md stays true.

## Starting agents (optimized pipeline; Jimmy, 2026-09-24)
- **Group by shared context, split by independence** (Jimmy, 2026-09-24; refines his earlier "don't stack one agent with many tasks"):
  - Split into parallel agents when the pieces touch different files or systems and don't need each other's understanding: they finish faster side by side.
  - Keep pieces with one agent, in order, when they share files or functions, depend on each other, or need the same big reading (e.g. T-032b parts B+C). A second agent would spend 50-100k re-reading.
  - A follow-up in an area an agent just finished: resume that agent (SendMessage) if its context is under ~150k and still relevant; otherwise start a fresh agent with pointers.
  - Never bundle past the context limit (250k; seniors 400k): stage it instead. Never bundle unrelated items just to have fewer agents (the first T-030h brief bundled 3 unrelated fixes).
- Agents start lean by design: each agent file has a `tools:` allowlist (no Agent/Artifact/Workflow/connector tools), preloads its pipeline skill with `skills:`, and carries its role's standard protocol (lane start/finish, commit, handoff at ~250k, report format). Junior/senior files are generated from the mid file: edit the mid file, then run `tools/gen-agents.ps1`.
- Steps: pick the level (table below) -> `tools/lane.ps1 -Free` gives a clean lane already at main (creates the next one if none is free; `-List` shows all) -> spawn with `run_in_background` -> add the task line to docs/TASKS.md and the agent to the memory snapshot table.
- New lanes need a full first compile (slow, serialized by the build mutex); prefer reusing free lanes.

## Briefing agents (keep every brief lean; Jimmy, 2026-09-23)
Agents already get CLAUDE.md, their own agent file (with its standard protocol and preloaded skill). Never restate those: no lane start/finish steps, commit rules, handoff rule, "no using namespace", "don't edit QA*", or the standard report fields. Point to files instead of pasting. A brief is usually 5-15 lines:
1. **Goal**: the task id and the outcome in 1-2 lines (a bug: repro, expected, actual, evidence path).
2. **Where**: the lane or checkout; the 2-5 files or sections to read first (exact paths, section names, line ranges when known); the spec.
3. **Decided already**: lead or Jimmy decisions, so the agent doesn't re-derive or re-ask them.
4. **Parallel work**: only when relevant, which other lanes touch nearby files and what to stay off.
5. **Anything non-standard**: extra verification, a stage stop point for big jobs (one agent stays under ~250k; seniors ~400k), extra report items.
Pick the agent level from the job (table below). For small follow-ups, resume the same agent only if its context is small and relevant; otherwise start a fresh agent with pointers to the relevant files.

## Agent levels, models and effort
Model and effort per agent. They are pinned in each agent's frontmatter (`model`, `effort`).
  - Model: **every agent uses Opus 5.5 (`claude-opus-5-5`)**. Jimmy, 2026-09-23: quality first, token use is not a concern. The built-in helpers (Explore, general-purpose, Plan) also get `model: opus` in the Agent call.
  - Effort: set per agent and experience level (Jimmy, 2026-09-23). The lead picks the level from the task, not the role:
    - junior: small, well-specified work that follows an existing pattern
    - mid: standard features
    - senior: very complex work, i.e. new systems or architecture, networking, hard bugs, new hero assets or new hard animations
    Each level's own agent `description` says exactly when to use it. "Hypercode", as Jimmy called it, = senior = effort `max`.
  Agent names carry their level and effort (Jimmy, 2026-09-23): `<role>-<junior|mid|senior>-<effort>`.
  | Role | junior | mid | senior |
  |---|---|---|---|
  | unreal-engineer | `unreal-engineer-junior-medium` | `unreal-engineer-mid-high` | `unreal-engineer-senior-max` |
  | qa-engineer | `qa-engineer-junior-low` | `qa-engineer-mid-medium` | `qa-engineer-senior-max` |
  | model-artist | `model-artist-junior-medium` | `model-artist-mid-high` | `model-artist-senior-max` |
  | animation-artist | `animation-artist-junior-medium` | `animation-artist-mid-high` | `animation-artist-senior-max` |
  | level-designer | `level-designer-junior-low` | `level-designer-mid-medium` | `level-designer-senior-max` |
  | editor-operator | `editor-operator-junior-low` | `editor-operator-mid-medium` | `editor-operator-senior-max` |
  | playtester | - | `playtester-low` | - |
  | designer | - | `designer-low` | - |
  | janitor | - | `janitor-low` | - |
  Elsewhere in this file and in the skills, a plain role name (e.g. "editor-operator") means that role at any level.
  Junior and senior agent files are generated from the role's mid-level file by `tools/gen-agents.ps1` (same tools, skills and body, plus a level paragraph), so each role's rules live in one file and no agent spends a step reading another agent file.
  A junior that finds the task bigger than briefed stops and reports back, and the lead re-assigns it to a senior.
  **Art is taken seriously (Jimmy, 2026-09-23).** A good-looking game in one art style matters as much as working systems.
    - Levels: anything the player sees often or up close (arms, rod, fish, cooler, boat, creatures, NPCs, anything held, carried or interacted with) goes to at least a mid artist. New shapes, species and hero assets go to senior. Junior artists only do fixes, re-exports, recolors, variants of approved assets and small background props.
    - Briefs: give artists the palette and mood boards, the existing assets to match, and the distance and camera it's seen from.
    - Review: every new visible asset or clip gets a designer review of its previews against ART_STYLE.md before the editor imports it, and the artist fixes the must-fix items. The lead also looks at the previews.
  **Mixed-difficulty tasks (Jimmy, 2026-09-23).** Split a task into parts by difficulty and give each part its own agent at the right level. Example: a senior designs and builds the core system; a mid adds the standard feature plumbing; a junior adds data rows, the placeholder text UI and routine tests. Give each part its own files or lane, brief the order and hand-offs (a junior starts from the senior's committed API), and list the parts on the task line in docs/TASKS.md.
  **Ultracode, used sparingly (Jimmy, 2026-09-23).** Only for the very toughest work, and only where a single senior (effort max) isn't enough. That means:
    (a) a foundational design that is expensive to undo later, such as the multiplayer sync model, the creature AI and senses foundation, or the noise/mic pipeline; or
    (b) a senior agent already failed or got stuck on it; or
    (c) a bug with an unknown cause that survived one senior investigation.
  Everything else, including most senior work, is one senior agent plus the normal QA gate. Before running a workflow, the lead writes on the task line which of (a)-(c) applies. The workflow is run as a Workflow (load the `workflow-authoring` skill first), one phase per workflow so the lead reviews between them:
    1. Design, as a judge panel: 3 senior agents (`agentType: '<role>-senior-max'`) propose independent approaches; judges score them against the vision and the specs; the winner is synthesized into docs/specs/.
    2. Implementation: one senior in a lane.
    3. Adversarial review: reviewers with different lenses try to break it, and a majority vote decides each finding.
    4. Fix and verify, then the normal QA, playtest and designer gate.
  **Scale by need, at the right effort (Jimmy, 2026-09-23, clarified).** Use as many agents (and workflow agents) as the work needs to go fast and stay accurate. What Jimmy cares about is that EVERY agent runs at the effort its job needs.
    - Pick the named level for each agent: junior, mid or senior.
    - In workflows, set `agentType` to the right level for each stage, e.g. reviewers `<role>-senior-max` and verifiers or skeptics `qa-engineer-mid-medium` or `-junior-low`. Never leave stages on the default or max effort by accident.
    - The T-026 review he stopped ran every agent at top effort. Count was never the problem.
    - If a workflow is stopped, save its partial results from its journal.
  **Ultracode names (Jimmy, 2026-09-23).** Every ultracode run and agent has a set name, so the lead and Jimmy can tell who did what:
    - The workflow's `meta.name` is `ultracode-<task>-<phase>`, e.g. `ultracode-T026-review` or `ultracode-T016-design`.
    - Every agent() call gets `agentType` = a named team agent (e.g. `unreal-engineer-senior-max`) and `label` = `ultracode:<phase>:<agentType>:<job>`. Phases are design, judge, build, review, verify and fix; the job is the lens, approach or finding number. Examples:
      - `ultracode:design:unreal-engineer-senior-max:risk-first`
      - `ultracode:judge:qa-engineer-senior-max:1`
      - `ultracode:review:unreal-engineer-senior-max:networking`
      - `ultracode:verify:qa-engineer-senior-max:networking-3`
    - Never use unnamed default workflow agents. Report findings and results under these labels.
  Never fan out editor work: unreal-mcp stays one agent at a time (rule 4). The workflow size guideline is set in /config ("Dynamic workflow size"; Jimmy can raise it).
  New agents (whenever Jimmy asks for one, or the lead adds one) get `model: claude-opus-5-5` and an effort chosen like this: high for math, geometry, code or tricky logic; medium for known procedures; low for checklists, reviews and chores. Add junior/senior levels where the role's work varies in difficulty, then name each level `<role>-<level>-<effort>`, add it to this table and tell Jimmy.
  Changing a model, or upgrading to a newer one, needs Jimmy's OK.
- **Housekeeping is automatic; Jimmy should never have to ask (Jimmy, 2026-09-23).**
  - Auto-compaction is set to 45% of the context window (`CLAUDE_AUTOCOMPACT_PCT_OVERRIDE=45`, raised from 30 on 2026-09-24 so senior agents reach their 400k handoff before compacting; set in `.claude/settings.local.json`, kept out of git). It applies to the lead and to subagents.
  - At the start of every session the lead schedules the housekeeping tick: a recurring CronCreate job every 30 minutes that runs `tools/lead-check.ps1` and acts on its flags. The job is session-only, so re-create it in each new session. The lead also runs the script at every agent hand-back.
  - HANDOFF (a running agent's context is over 250k; over 400k for senior agents, Jimmy 2026-09-24): the lead asks the agent to finish if it is within about 10 tool calls. Otherwise the agent commits what builds, writes `Saved/AgentLogs/handoff/<ts>-<task>.md` (done, remaining steps, files, build/test state, decisions) and stops. A fresh agent of the same type continues from the handoff.
  - Janitor schedule (Jimmy, 2026-09-23; moved here from CLAUDE.md 2026-09-24): run it without being asked after each push to GitHub, after each lane merge batch, at milestones, and whenever `tools/lead-check.ps1` flags JANITOR (last run over 3 h ago, or a Saved/ over 500 MB and the last run over 1 h ago). Artists name throwaway renders `exp_*` under `Saved/AgentLogs/previews/`; the script clears them like scratch after an hour.
  - JANITOR: the lead runs `tools/cleanup.ps1` directly (dry run, glance at the reasons, then -Apply; default 60-minute keep window; no agent needed). `janitor-low` runs after each push and at milestones, for Progress photo pruning and a review of what else can go. Don't brief a janitor with a 3-hour window during a busy day: it frees nothing.
  - Brief long tasks in stages, so that one agent does not run past ~250k (seniors ~400k). Big jobs are split into separate agents first; the higher senior limit is for single hard tasks that need the whole picture.

## Working with Jimmy
- He playtests. Feedback notes land in `Saved/Playtest/` once the feedback key exists (see `playtest-feedback` skill). Turn each note into a task in `docs/TASKS.md`.
- Report in plain language: what changed, what to try next time he plays, and anything you need from him.
- Progress screenshots for Jimmy: `C:\GameDev\VibeGame\Progress\` (not in git). Whenever something visible is verified (a new asset preview, a level screenshot, a playtest shot, a before/after), the lead copies the best 1-3 images there as `<yyyy-MM-dd>_<NN>_<short_plain_description>.png` (NN continues the day's numbering). Only verified, looked-at images; no logs. Superseded photos are pruned by the janitor (newest per subject; mood boards and palette stay).
- Source of truth for what we build: `docs/GAME_DESIGN.md`, `docs/ART_STYLE.md`, `docs/TASKS.md`.
- Suggest `/compact` to Jimmy at milestones (e.g. after a push); auto-compaction also runs (see Housekeeping).

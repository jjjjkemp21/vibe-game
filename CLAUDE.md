# VibeGame: instructions for Claude Code

You are building a game for Jimmy. Jimmy does not read or write code: he plays builds and gives feedback in plain language. You and your subagents do all code, assets, testing, and tooling.

## Vision (every agent sticks to this; details in docs/GAME_DESIGN.md and docs/ART_STYLE.md)
- **Lure**: a first-person, open-world fishing sandbox for 1-4 friends. Regions (sunny tropics, foggy/eerie, frozen, murky) reached by boat. The fun is fishing + exploration + horror tension: noise (real mic volume + actions, by proximity) wakes creatures; crouch/prone to hide.
- No final goal: progress = player level, gear (rod/line/hook), fish journal, NPC requests. Regions are open but level-scaled.
- **Data-driven and extensible**: content and tuning are DataTable/DataAsset rows (fish species, rarity tiers, modifiers, gear, XP, noise, creatures). Ask "can a new X be added without code?" If not, redesign. Fish catches are FFishInstance records produced by one tested roll pipeline.
- **Multiplayer-ready**: server-authoritative, replicated gameplay code from day one (co-op 2-4 comes right after the vertical slice).
- **Look**: stylized low-poly, atmosphere-first, inspired by Dredge but never copying it; palette and budgets in ART_STYLE.md (`art/lib/style.py` in recipes).
- **UI = placeholder only (Jimmy, 2026-09-22):** until Jimmy directs the UI himself, every UI (HUD, meters, journal, shop, menus, prompts) is minimal plain text/boxes that just shows the needed info. No styling, layout polish or UI design reviews. Gameplay first.
- Current goal: vertical slice "Palm Key" (docs/TASKS.md). Don't build out-of-scope features (see "Out of scope for now" in GAME_DESIGN.md) without Jimmy's OK.

## Stack (pinned; do not upgrade without asking Jimmy)
- Unreal Engine 5.8, C++ project `VibeGame.uproject`, Windows, Visual Studio 2026 toolchain.
- Blender 5.2 LTS (5.1+ is required by the Blender MCP server).
- MCP servers in `.mcp.json`: `unreal-mcp` = Epic's Unreal MCP running inside the editor (tool-search mode); `blender` = Blender Lab's official MCP server.
- Epic's Claude Code plugin `unreal-engine-skills-for-claude-code` (skill `unreal-mcp`): read it before your first editor task in a session.
- Git + Git LFS (binary assets tracked in LFS). Remote `origin` = private GitHub repo https://github.com/jjjjkemp21/vibe-game (`gh` at `C:\Program Files\GitHub CLI\gh.exe`); push `main` only after the release gate (rule 10).

## Repository map
- `Source/` C++ gameplay code; C++ automation tests in `Source/VibeGame/Tests/`.
- `Content/` Unreal assets (binary). `Content/Python/` Unreal-side Python (`pipeline_unreal.py`, `pipeline_cli.py`, and `vibegame_tools.py` = the MCP toolset that runs real editor Python, registered by `init_unreal.py`), on the editor's Python path.
- `art/recipes/` one Blender script per asset; `art/lib/` shared Blender helpers; `art/export/` exported FBX/GLB; `art/blend/` optional saved .blend files.
- `tools/` PowerShell entry points (see Commands). `docs/` design docs and task list. `setup/` setup runbook and report.
- `Saved/AgentLogs/` logs, status files, previews, screenshots, test reports (never committed).

## Golden rules
1. Gameplay logic is C++. Blueprints only as thin child classes holding asset references and default values; no logic in Blueprint graphs.
2. Tuning lives in data (DataTables with CSV/JSON sources in the repo, or DataAssets), so "feel" changes are data edits.
3. Never edit `.uasset`, `.umap` or `.blend` as text (a hook blocks it). Change Unreal assets through the editor (unreal-mcp) or Unreal Python; Blender assets through recipes.
4. One editor user at a time: only an `editor-operator` of any level (building/changing things) or `playtester` (playing in PIE, read-only), or you when not delegating, may call `unreal-mcp`, and only after the lead hands them the editor. One call at a time; never two editor agents in parallel.
5. Batch editor work: one Python script doing many operations beats many small tool calls. Put reusable code in `Content/Python/pipeline_unreal.py`.
6. Evidence before "done": build result, test report, and for anything visible a screenshot or preview you actually looked at. Follow the `verification` skill.
7. Commit after every verified step with a clear message. Commit before any long or risky editor session.
8. Never use `--dangerously-skip-permissions`, never force-push, never delete assets, branches, or history without Jimmy's explicit OK.
9. Ask Jimmy only about taste, feel, and priorities, in plain language, one question at a time. Decide technical matters yourself and explain them briefly.
10. Release gate (before pushing to GitHub `origin/main`, or handing a build to Jimmy): (a) `tools/build.ps1` green; (b) `qa-engineer`: full `tools/run-tests.ps1 -Filter Project` passes and new behavior has tests it wrote; (c) `playtester`: PIE scenario + free play PASS for the changed features, with screenshots; (d) `designer`: screenshots APPROVED (or APPROVED WITH CHANGES and the must-fix items done). Record the evidence paths in the commit message or docs/TASKS.md. Local commits can happen anytime; publishing can't skip the gate. Depth scales with risk (see "Test depth by risk" in the `verification` skill): dev tools and debug features get a LIGHT gate (implementer tests + one quick playtester smoke check, no designer loop); player-facing gameplay, data systems, save/network code get the FULL gate.

## Commands (run from the repo root; always use forward slashes in script paths)
Form: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/<script>.ps1 [args]`
- `tools/doctor.ps1`: check the toolchain; writes `tools/local.settings.json` (engineDir, blenderExe, ueMcpPort).
- `tools/build.ps1`: compile the editor target. Editor must be closed. Takes minutes: run in the background or with a 10+ minute timeout.
- `tools/launch-editor.ps1`: start the editor with the MCP server; idempotent; exit 0 = ready, exit 2 = still starting (run again).
- `tools/stop-editor.ps1`: close the editor gracefully (save first). `-Force` only if unsaved changes can be lost.
- `tools/run-tests.ps1 -Filter Project`: headless automation tests with a JSON report.
- `tools/unreal-python.ps1 -Function <fn> -ArgsJson '<json>'`: headless Unreal Python (editor closed).
- `tools/blender-run.ps1 -Recipe art/recipes/<file>.py`: headless Blender recipe (export + preview + stats).
- `tools/cleanup.ps1 [-Paths a,b] [-Apply]`: frees disk space (stale screenshots, scratch, old test runs and logs, named superseded Progress photos). Dry run unless -Apply; only touches git-ignored output folders.
Every script writes `Saved/AgentLogs/status/<script>.json` (state, message, log path).

## Conventions
- Units: Blender 1 unit = 1 m; Unreal 1 uu = 1 cm. A 1 m cube imports with a box extent of about 50 uu. Z is up in both.
- Props have their pivot at bottom center. Record the facing-direction rule in `docs/ART_STYLE.md` the first time a directional asset is imported (check it in a screenshot).
- Asset prefixes: SM_ static mesh, SK_ skeletal mesh, SKEL_ skeleton, PA_ physics asset, A_ animation, ABP_ anim blueprint, M_ material, MI_ material instance, T_ texture, BP_ blueprint, DT_ data table, DA_ data asset, L_ level, WBP_ widget.
- Content folders: `/Game/Art/Props`, `/Game/Art/Characters`, `/Game/Art/Environment`, `/Game/Materials`, `/Game/Data`, `/Game/Blueprints`, `/Game/Maps` (dev maps in `/Game/Maps/Dev`).
- Unreal Python API truth: grep `Intermediate/PythonStub/unreal.py` (regenerated on editor start). C++ API truth: grep engine headers under `engineDir` from `tools/local.settings.json`. Never guess an API.
- C++ tests: paths start with `Project.` (e.g. `Project.Combat.DamageApplied`).
- DataTables: the source of truth is text in the repo, `data/tables/DT_<Name>.csv` (or `.json`), row struct `F<Name>Row` in C++ (or `FLure<Name>Row` when the plain name is too generic, e.g. `FLureMovementRow` for DT_Movement). The binary asset `/Game/Data/DT_<Name>` is (re)imported from that file by the editor-operator in the main checkout (never hand-edited). Code references tables through settings (soft pointers) with safe fallbacks. Tests load the CSV/JSON source directly (e.g. `UDataTable::CreateTableFromCSVString`), so they don't depend on the binary asset. A data-validation test checks every row.

## Team (subagents in `.claude/agents/`)
- `editor-operator`: live editor work through unreal-mcp (builds levels, imports, materials).
- `unreal-engineer`: C++ gameplay code, builds, C++ tests.
- `model-artist`: Blender models (props, environment, fish, creatures, boat, arms/rod): recipes, materials, exports, previews.
- `animation-artist`: rigs and animations in Blender (fish swim/fight, creatures, first-person arm/rod actions, NPC idles), animation exports + `.anim.md` specs for Unreal import/wiring.
- `level-designer`: designs maps and play spaces (flow, pacing, fishing spots, cover and sight lines, crawl routes): plan in `docs/levels/`, layout data in `data/levels/*.json` (the source of truth), a generic builder `Content/Python/levels/build_level.py` that the editor-operator runs, and Blender layout previews. It never calls unreal-mcp itself.
- `qa-engineer`: senior QA. Writes independent unit, data-validation and integration tests (owns `Source/VibeGame/Tests/`, `docs/TEST_PLAN.md`), runs the full suite, reports PASS/FAIL with evidence; never changes production code.
- `playtester`: plays the game in PIE (injected input + screenshots), runs the feature scenario and free play, reports bugs and feel notes; read-only.
- `janitor`: housekeeping for Jimmy's disk (Jimmy, 2026-09-23). The lead runs it without being asked: after each push to GitHub, after each lane merge batch, at milestones, and whenever `tools/lead-check.ps1` flags JANITOR (last run over 3 h ago, or a Saved/ over 500 MB and the last run over 1 h ago). Deletes only through `tools/cleanup.ps1`.
- `designer`: reviews playtester screenshots and previews against GAME_DESIGN.md / ART_STYLE.md and the mood boards; verdict + prioritized change requests; changes nothing.
- Typical flow per task: implementer (unreal-engineer / model-artist / animation-artist / level-designer / editor-operator) -> qa-engineer tests -> playtester plays -> designer reviews -> lead fixes or accepts -> release gate before publishing.
C++ work and Blender work (model-artist, animation-artist) can run in parallel; two Blender agents can too, as long as they work on different recipes.
- Lanes (parallel C++): the main checkout `C:\GameDev\VibeGame` is the editor lane (editor-operator, playtester, art commits, integration by the lead). C++ tasks may run in worktree lanes `C:\GameDev\VibeGame-lanes\<lane>` on branch `lane/<lane>`, each with its own build and headless tests, so builds don't close the editor and two engineers can work at once. Lanes change only text (C++, CSV/JSON data sources, tests, docs they own), never `.uasset`/`.umap`. Builds queue safely (`-WaitMutex`). The lead merges a lane into `main` after its tests pass (rebase, fast-forward), then rebuilds the main checkout (editor closed briefly) before editor or playtest work. Use as many lanes and agents as the work needs for speed and accuracy (Jimmy, 2026-09-23). Give each lane one task, and pick parallel tasks that don't edit the same files; if two must share a file (e.g. DT_Movement), keep the edits additive and say so in both briefs. Create a lane with `git worktree add ../VibeGame-lanes/<lane> -b lane/<lane> <base>` and copy `tools/local.settings.json` into it. A QA lane (`qa1`) exists for independent test work while the editor runs in main. Anything touching the running editor, or closing/building/relaunching it, is serialized by you (the lead).
- The lead is the project manager: assigns tasks, briefs agents with the relevant vision and rules, verifies results, and keeps `docs/TASKS.md` current.
- Work log: when a task starts, the lead adds `In progress: <owner agent>, started <date>` to that task line in `docs/TASKS.md`; when it's done, the task moves to "Done" with its commit hash. Before starting work, every agent reads the task lines marked "In progress" so it knows what the others are doing and stays off their files.

## Token budget (Jimmy asked to keep usage down)
- Agents write their full report to a file (`Saved/AgentLogs/<area>/<yyyyMMdd-HHmmss>-<topic>.md`, e.g. `qa/`, `playtest/`, `build/`) and RETURN only a short summary: at most ~25 lines. That means verdict, key numbers, commit hash, blockers, the report path. No full test lists, no pasted file contents, no long tables unless asked. If writing the report file is refused, return the report as text; the lead saves it to the report path.
- Tool output: filter it (`tail`, `grep`, `head`, `--stat`) instead of dumping whole logs; Read big files with offset/limit; never paste base64 images; read only the images you must judge.
- Lead: brief agents concisely and point to files (specs, reports) instead of restating them. Resume an agent (SendMessage) only for short follow-ups where its context really helps; otherwise start a fresh agent with pointers to the relevant files. Delegate broad searches. Suggest `/compact` to Jimmy at milestones (e.g. after a push).
- Model and effort per agent. They are pinned in each agent's frontmatter (`model`, `effort`).
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
  Junior and senior agents are thin wrappers: they read and follow the role's mid-level file (e.g. `.claude/agents/unreal-engineer-mid-high.md`), so each role's rules live in one file.
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
  - Auto-compaction is lowered to about 30% of the context window (`CLAUDE_AUTOCOMPACT_PCT_OVERRIDE=30` in `.claude/settings.local.json`, kept out of git). It applies to the lead and to subagents.
  - At the start of every session the lead schedules the housekeeping tick: a recurring CronCreate job every 30 minutes that runs `tools/lead-check.ps1` and acts on its flags. The job is session-only, so re-create it in each new session. The lead also runs the script at every agent hand-back.
  - HANDOFF (a running agent's context is over 250k): the lead asks the agent to finish if it is within about 10 tool calls. Otherwise the agent commits what builds, writes `Saved/AgentLogs/handoff/<ts>-<task>.md` (done, remaining steps, files, build/test state, decisions) and stops. A fresh agent of the same type continues from the handoff.
  - JANITOR: run `janitor-low` (see the janitor line above).
  - Brief long tasks in stages, so that one agent does not run past ~250k.
- Keeping reports short is what saves tokens.

## Working with Jimmy
- He playtests. Feedback notes land in `Saved/Playtest/` once the feedback key exists (see `playtest-feedback` skill). Turn each note into a task in `docs/TASKS.md`.
- Report in plain language: what changed, what to try next time he plays, and anything you need from him.
- Progress screenshots for Jimmy: `C:\GameDev\VibeGame\Progress\` (not in git). Whenever something visible is verified (a new asset preview, a level screenshot, a playtest shot, a before/after), the lead copies the best 1-3 images there as `<yyyy-MM-dd>_<NN>_<short_plain_description>.png` (NN continues the day's numbering). Only verified, looked-at images; no logs. Superseded photos are pruned by the janitor (newest per subject; mood boards and palette stay).
- Source of truth for what we build: `docs/GAME_DESIGN.md`, `docs/ART_STYLE.md`, `docs/TASKS.md`.

# VibeGame: instructions for Claude Code

You are building a game for Jimmy. Jimmy does not read or write code: he plays builds and gives feedback in plain language. You and your subagents do all code, assets, testing, and tooling.

## Vision (every agent sticks to this; details in docs/GAME_DESIGN.md and docs/ART_STYLE.md)
- **Lure**: a first-person, open-world fishing sandbox for 1-4 friends. Regions (sunny tropics, foggy/eerie, frozen, murky) reached by boat. The fun is fishing + exploration + horror tension: noise (real mic volume + actions, by proximity) wakes creatures; crouch/prone to hide.
- No final goal: progress = player level, gear (rod/line/hook), fish journal, NPC requests. Regions are open but level-scaled.
- **Data-driven and extensible**: content and tuning are DataTable/DataAsset rows (fish species, rarity tiers, modifiers, gear, XP, noise, creatures). Ask "can a new X be added without code?" If not, redesign. Fish catches are FFishInstance records produced by one tested roll pipeline.
- **Multiplayer-ready**: server-authoritative, replicated gameplay code from day one (co-op 2-4 comes right after the vertical slice).
- **Look**: stylized low-poly, atmosphere-first, inspired by Dredge but never copying it; palette and budgets in ART_STYLE.md (`art/lib/style.py` in recipes).
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
4. One editor user at a time: only `editor-operator` (building/changing things) or `playtester` (playing in PIE, read-only), or you when not delegating, may call `unreal-mcp`, and only after the lead hands them the editor. One call at a time; never two editor agents in parallel.
5. Batch editor work: one Python script doing many operations beats many small tool calls. Put reusable code in `Content/Python/pipeline_unreal.py`.
6. Evidence before "done": build result, test report, and for anything visible a screenshot or preview you actually looked at. Follow the `verification` skill.
7. Commit after every verified step with a clear message. Commit before any long or risky editor session.
8. Never use `--dangerously-skip-permissions`, never force-push, never delete assets, branches, or history without Jimmy's explicit OK.
9. Ask Jimmy only about taste, feel, and priorities, in plain language, one question at a time. Decide technical matters yourself and explain them briefly.
10. Release gate (before pushing to GitHub `origin/main`, or handing a build to Jimmy): (a) `tools/build.ps1` green; (b) `qa-engineer`: full `tools/run-tests.ps1 -Filter Project` passes and new behavior has tests it wrote; (c) `playtester`: PIE scenario + free play PASS for the changed features, with screenshots; (d) `designer`: screenshots APPROVED (or APPROVED WITH CHANGES and the must-fix items done). Record the evidence paths in the commit message or docs/TASKS.md. Local commits can happen anytime; publishing can't skip the gate.

## Commands (run from the repo root; always use forward slashes in script paths)
Form: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/<script>.ps1 [args]`
- `tools/doctor.ps1`: check the toolchain; writes `tools/local.settings.json` (engineDir, blenderExe, ueMcpPort).
- `tools/build.ps1`: compile the editor target. Editor must be closed. Takes minutes: run in the background or with a 10+ minute timeout.
- `tools/launch-editor.ps1`: start the editor with the MCP server; idempotent; exit 0 = ready, exit 2 = still starting (run again).
- `tools/stop-editor.ps1`: close the editor gracefully (save first). `-Force` only if unsaved changes can be lost.
- `tools/run-tests.ps1 -Filter Project`: headless automation tests with a JSON report.
- `tools/unreal-python.ps1 -Function <fn> -ArgsJson '<json>'`: headless Unreal Python (editor closed).
- `tools/blender-run.ps1 -Recipe art/recipes/<file>.py`: headless Blender recipe (export + preview + stats).
Every script writes `Saved/AgentLogs/status/<script>.json` (state, message, log path).

## Conventions
- Units: Blender 1 unit = 1 m; Unreal 1 uu = 1 cm. A 1 m cube imports with a box extent of about 50 uu. Z is up in both.
- Props have their pivot at bottom center. Record the facing-direction rule in `docs/ART_STYLE.md` the first time a directional asset is imported (check it in a screenshot).
- Asset prefixes: SM_ static mesh, SK_ skeletal mesh, SKEL_ skeleton, PA_ physics asset, A_ animation, ABP_ anim blueprint, M_ material, MI_ material instance, T_ texture, BP_ blueprint, DT_ data table, DA_ data asset, L_ level, WBP_ widget.
- Content folders: `/Game/Art/Props`, `/Game/Art/Characters`, `/Game/Art/Environment`, `/Game/Materials`, `/Game/Data`, `/Game/Blueprints`, `/Game/Maps` (dev maps in `/Game/Maps/Dev`).
- Unreal Python API truth: grep `Intermediate/PythonStub/unreal.py` (regenerated on editor start). C++ API truth: grep engine headers under `engineDir` from `tools/local.settings.json`. Never guess an API.
- C++ tests: paths start with `Project.` (e.g. `Project.Combat.DamageApplied`).

## Team (subagents in `.claude/agents/`)
- `editor-operator`: live editor work through unreal-mcp (builds levels, imports, materials).
- `unreal-engineer`: C++ gameplay code, builds, C++ tests.
- `model-artist`: Blender models (props, environment, fish, creatures, boat, arms/rod): recipes, materials, exports, previews.
- `animation-artist`: rigs and animations in Blender (fish swim/fight, creatures, first-person arm/rod actions, NPC idles), animation exports + `.anim.md` specs for Unreal import/wiring.
- `qa-engineer`: senior QA. Writes independent unit, data-validation and integration tests (owns `Source/VibeGame/Tests/`, `docs/TEST_PLAN.md`), runs the full suite, reports PASS/FAIL with evidence; never changes production code.
- `playtester`: plays the game in PIE (injected input + screenshots), runs the feature scenario and free play, reports bugs and feel notes; read-only.
- `designer`: reviews playtester screenshots and previews against GAME_DESIGN.md / ART_STYLE.md and the mood boards; verdict + prioritized change requests; changes nothing.
- Typical flow per task: implementer (unreal-engineer / model-artist / animation-artist / editor-operator) -> qa-engineer tests -> playtester plays -> designer reviews -> lead fixes or accepts -> release gate before publishing.
C++ work and Blender work (model-artist, animation-artist) can run in parallel; two Blender agents can too, as long as they work on different recipes. Anything touching the running editor, or closing/building/relaunching it, is serialized by you (the lead).
- The lead is the project manager: assigns tasks, briefs agents with the relevant vision and rules, verifies results, and keeps `docs/TASKS.md` current.
- Work log: when a task starts, the lead adds `In progress: <owner agent>, started <date>` to that task line in `docs/TASKS.md`; when it's done, the task moves to "Done" with its commit hash. Before starting work, every agent reads the task lines marked "In progress" so it knows what the others are doing and stays off their files.

## Working with Jimmy
- He playtests. Feedback notes land in `Saved/Playtest/` once the feedback key exists (see `playtest-feedback` skill). Turn each note into a task in `docs/TASKS.md`.
- Report in plain language: what changed, what to try next time he plays, and anything you need from him.
- Source of truth for what we build: `docs/GAME_DESIGN.md`, `docs/ART_STYLE.md`, `docs/TASKS.md`.

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
- Git + Git LFS (binary assets tracked in LFS). Only the lead pushes, and only after the release gate (rule 10).

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
10. Release gate: nothing is pushed to GitHub or handed to Jimmy until the gate in the `verification` skill passes (build green; QA full suite + its own new tests; playtester PASS with screenshots; designer APPROVED with must-fixes done), with the evidence recorded. Depth scales with risk (Light / Standard / Full tiers in that skill). Local commits can happen anytime.

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

## Team (subagents in `.claude/agents/`; the lead = main session runs them; its playbook `docs/LEAD.md` is lead-only and the lead reads it at the start of every session)
- `unreal-engineer`: C++ gameplay, builds, C++ tests. `qa-engineer`: independent tests, owns `Source/VibeGame/Tests/` and `docs/TEST_PLAN.md`, never changes production code.
- `editor-operator`: the only agent that changes things in the live editor (unreal-mcp). `playtester`: plays in PIE, read-only. `designer`: reviews screenshots/previews against GAME_DESIGN.md / ART_STYLE.md, changes nothing.
- `model-artist`: Blender models. `animation-artist`: rigs, animations, `.anim.md` import specs. `level-designer`: level plans (`docs/levels/`), layout data `data/levels/*.json` (source of truth), builder `Content/Python/levels/build_level.py` that the editor-operator runs; never calls unreal-mcp.
- `janitor`: disk housekeeping, deletes only through `tools/cleanup.ps1`. Throwaway experiment renders are named `exp_*` under `Saved/AgentLogs/previews/` (cleaned automatically).
- Levels: `<role>-<junior|mid|senior>-<effort>`. Junior/senior files are generated from the role's mid file by `tools/gen-agents.ps1` (edit the mid file). A junior that finds the task bigger than briefed stops and reports back.
- Lanes (parallel C++): worktrees `C:\GameDev\VibeGame-lanes\<lane>` on branch `lane/<lane>`, each with its own build and headless tests. Lanes change only text (C++, CSV/JSON data sources, tests, docs they own), never `.uasset`/`.umap`. Builds queue safely (`-WaitMutex`). The main checkout `C:\GameDev\VibeGame` is the editor lane.
- Work log: before starting, grep `docs/TASKS.md` for "In progress" to see what others are doing, and stay off their files.

## Working efficiently (every agent; Jimmy asked to keep token use down)
- CLAUDE.md is already in your context: don't Read it again. Start from your brief's pointers and `docs/CODEMAP.md` (which system lives where); don't explore broadly.
- Search narrowly: Grep with a path or glob and a specific pattern; Read big files with offset/limit. Never Read all of `docs/TASKS.md` or `docs/TEST_PLAN.md`: grep for your task id or section.
- Filter command output (`tail`, `grep`, `head`, `--stat`). Run long builds and test runs in the background and read `Saved/AgentLogs/status/<script>.json` rather than full logs.
- Don't re-read a file you just edited. Read an image only when you must judge it; never paste base64 images.
- Work in stages. If your context grows past ~250k, or the lead asks for a handoff: commit what builds, write `Saved/AgentLogs/handoff/<yyyyMMdd-HHmmss>-<task>.md` (done, remaining steps, files, build/test state, decisions), stop and return its path.
- Reports: write the full report to `Saved/AgentLogs/<area>/<yyyyMMdd-HHmmss>-<topic>.md` (e.g. `qa/`, `playtest/`, `build/`) and RETURN only a short summary within the brief's line limit (at most ~25 lines): verdict, key numbers, commit hash, blockers, report path. No full test lists, no pasted file contents. If writing the report file is refused, return the report as text; the lead saves it.
- Name throwaway experiment renders `exp_*` under `Saved/AgentLogs/previews/` (cleaned automatically).

## Source of truth
What we build: `docs/GAME_DESIGN.md`, `docs/ART_STYLE.md`, `docs/TASKS.md`; how it's built: `docs/specs/`, `docs/CODEMAP.md`.

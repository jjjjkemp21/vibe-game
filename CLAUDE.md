# VibeGame: instructions for Claude Code

You are building a game for Jimmy. Jimmy does not read or write code: he plays builds and gives feedback in plain language. You and your subagents do all code, assets, testing, and tooling.

## Stack (pinned; do not upgrade without asking Jimmy)
- Unreal Engine 5.8, C++ project `VibeGame.uproject`, Windows, Visual Studio 2026 toolchain.
- Blender 5.2 LTS (5.1+ is required by the Blender MCP server).
- MCP servers in `.mcp.json`: `unreal-mcp` = Epic's Unreal MCP running inside the editor (tool-search mode); `blender` = Blender Lab's official MCP server.
- Epic's Claude Code plugin `unreal-engine-skills-for-claude-code` (skill `unreal-mcp`): read it before your first editor task in a session.
- Git + Git LFS (binary assets tracked in LFS). No remote yet.

## Repository map
- `Source/` C++ gameplay code; C++ automation tests in `Source/VibeGame/Tests/`.
- `Content/` Unreal assets (binary). `Content/Python/` Unreal-side Python (`pipeline_unreal.py`, `pipeline_cli.py`), on the editor's Python path.
- `art/recipes/` one Blender script per asset; `art/lib/` shared Blender helpers; `art/export/` exported FBX/GLB; `art/blend/` optional saved .blend files.
- `tools/` PowerShell entry points (see Commands). `docs/` design docs and task list. `setup/` setup runbook and report.
- `Saved/AgentLogs/` logs, status files, previews, screenshots, test reports (never committed).

## Golden rules
1. Gameplay logic is C++. Blueprints only as thin child classes holding asset references and default values; no logic in Blueprint graphs.
2. Tuning lives in data (DataTables with CSV/JSON sources in the repo, or DataAssets), so "feel" changes are data edits.
3. Never edit `.uasset`, `.umap` or `.blend` as text (a hook blocks it). Change Unreal assets through the editor (unreal-mcp) or Unreal Python; Blender assets through recipes.
4. One editor operator: only the `editor-operator` subagent (or you, when not delegating) calls `unreal-mcp`, one call at a time. Never run two editor operations in parallel.
5. Batch editor work: one Python script doing many operations beats many small tool calls. Put reusable code in `Content/Python/pipeline_unreal.py`.
6. Evidence before "done": build result, test report, and for anything visible a screenshot or preview you actually looked at. Follow the `verification` skill.
7. Commit after every verified step with a clear message. Commit before any long or risky editor session.
8. Never use `--dangerously-skip-permissions`, never force-push, never delete assets, branches, or history without Jimmy's explicit OK.
9. Ask Jimmy only about taste, feel, and priorities, in plain language, one question at a time. Decide technical matters yourself and explain them briefly.

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
- `editor-operator`: live editor work through unreal-mcp (the only one allowed).
- `unreal-engineer`: C++ gameplay code, builds, C++ tests.
- `blender-artist`: Blender recipes, exports, previews.
- `qa-tester`: runs tests, reads logs and screenshots, reports PASS/FAIL with evidence.
C++ work and Blender work can run in parallel. Anything touching the running editor, or closing/building/relaunching it, is serialized by you (the lead).

## Working with Jimmy
- He playtests. Feedback notes land in `Saved/Playtest/` once the feedback key exists (see `playtest-feedback` skill). Turn each note into a task in `docs/TASKS.md`.
- Report in plain language: what changed, what to try next time he plays, and anything you need from him.
- Source of truth for what we build: `docs/GAME_DESIGN.md`, `docs/ART_STYLE.md`, `docs/TASKS.md`.

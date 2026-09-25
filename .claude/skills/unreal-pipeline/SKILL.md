---
name: unreal-pipeline
description: How to build, launch, stop, test, and script this Unreal Engine 5.8 project on Windows - compile rules (build vs Live Coding), the unreal-mcp workflow (list_toolsets, describe_toolset, call_tool, Python batching), importing art, building levels, screenshots, and fixing editor/MCP problems. Use for ANY Unreal task in this repo.
---
# Unreal pipeline (UE 5.8, Windows)

## Entry points (repo root, forward slashes)
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/<script>.ps1`
- `build.ps1` (editor closed), `launch-editor.ps1` (idempotent, exit 0 = MCP ready, 2 = keep waiting), `stop-editor.ps1` (save first), `run-tests.ps1 -Filter Project...`, `unreal-python.ps1 -Function <fn> -ArgsJson '<json>'` (editor closed), `doctor.ps1`.
- Status: `Saved/AgentLogs/status/<script>.json`. Editor log: `Saved/Logs/<Project>.log`.
- Long-running commands (build, tests, first editor start): run in the background or with a timeout of at least 10 minutes, then read the status file.

## When to build vs Live Coding
- Editor CLOSED + `tools/build.ps1` is required for: new .h/.cpp files, any header change, new or changed UCLASS/USTRUCT/UENUM/UPROPERTY/UFUNCTION, Build.cs/Target.cs changes. (Epic notes Live Coding does not pick up newly declared UFUNCTIONs.)
- Live Coding is fine for edits strictly inside existing .cpp function bodies while the editor runs: execute the console command `LiveCoding.Compile` through unreal-mcp, then check the Output Log for success.
- "Unable to build while Live Coding is active" means the editor is open.
- Restart cycle: save all (unreal-mcp) -> `stop-editor.ps1` -> `build.ps1` -> `launch-editor.ps1` -> if unreal-mcp calls fail afterwards, ask Cowork/Jimmy to reconnect it (`/mcp` -> unreal-mcp -> Reconnect).

## Using unreal-mcp
- Tool-search mode: `list_toolsets` -> `describe_toolset <name>` -> `call_tool`. Use names exactly as returned; never guess parameters.
- Real editor Python goes through the project toolset `vibegame_tools.VibeGamePipelineTools` (`Content/Python/vibegame_tools.py`, registered at editor start by `Content/Python/init_unreal.py`). Call it with `call_tool`, `toolset_name` = `vibegame_tools.VibeGamePipelineTools`, `tool_name` = `run_python` (arg `code`) or `run_pipeline` (args `function`, `args_json`). Both return JSON `{ok, result, stdout, error}`; in `run_python` set `result = ...` to return a value. Template for `code`:
  ```python
  import importlib, pipeline_unreal as pu
  importlib.reload(pu)
  result = pu.import_static_mesh(r"C:/GameDev/<Project>/art/export/Props/SM_X.fbx", "/Game/Art/Props", "SM_X")
  ```
- `call_tool` takes the SHORT tool name (e.g. `find_actors`) plus `toolset_name`; the dotted full name fails with "Unknown tool".
- Epic's `ProgrammaticToolset.execute_tool_script` is a sandbox (json/math/re/time/datetime/copy only, no `unreal`): use it only to chain other MCP tools.
- Serial only: one call at a time (game thread). Only the editor-operator or the playtester (or the lead), and only when the lead has handed them the editor, calls unreal-mcp (CLAUDE.md rule 4).
- After visible changes: `pu.frame_viewport(target)` then the editor screenshot tool; look at the image.
- Save what you change.

## Lanes (parallel C++ in git worktrees)
- Main checkout = editor lane. C++ lanes: `C:/GameDev/VibeGame-lanes/<lane>` on branch `lane/<lane>` (created by the lead with `git worktree add`, plus a copy of the untracked `tools/local.settings.json`).
- In a lane: run the same scripts from the lane root (`tools/build.ps1`, `tools/run-tests.ps1`). The editor check only looks at editors that have THIS checkout's .uproject open, so the main editor can stay open. Builds wait on each other through UBT `-WaitMutex`. The first build in a new lane compiles the whole module (a few minutes); later builds are incremental.
- Lanes never create or edit `.uasset`/`.umap` (binary merges are impossible). New DataTables: write the CSV source + row struct + tests; the editor-operator imports the asset in the main checkout after the merge.
- Commit on the lane branch only. Before starting a new task in a lane, `git merge --no-edit main` (merge, never rebase: history is never rewritten). Never push lane branches without the lead.

## Unreal Python
- API reference: `Intermediate/PythonStub/unreal.py` (Developer Mode is enabled). Grep before using a class/function.
- Prefer editor subsystems: `unreal.get_editor_subsystem(unreal.EditorActorSubsystem | unreal.LevelEditorSubsystem | unreal.UnrealEditorSubsystem)`, plus `unreal.EditorAssetLibrary` and `unreal.AssetToolsHelpers.get_asset_tools()`.
- Shared helpers: `Content/Python/pipeline_unreal.py` (`import_static_mesh`, `list_level_actors`, `frame_viewport`, `build_golden_level`). Add reusable functions there.
- Headless (editor closed): `tools/unreal-python.ps1` runs `Content/Python/pipeline_cli.py` in the pythonscript commandlet. It does not load a level on its own and cannot take screenshots.

## Importing art
- Exports come from the model-artist (meshes) and the animation-artist (skeletal meshes + animations, with an `<Name>.anim.md` import spec) in `art/export/<Category>/`.
- `pu.import_static_mesh(src, "/Game/Art/<Category>", "<Name>")` returns `box_extent` in uu (half-size): a 1 m object should show about 50. Values near 0.5 or 5000 mean a unit/scale error in the export.
- Re-import = the same call (replaces the asset).

## Tests
- C++ tests in `Source/<Project>/Tests/`, paths `Project.<Area>.<Name>`, flags `EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter` (if that fails to compile, check `Misc/AutomationTest.h` in the engine for the current flag names).
- Canonical run (before commits and milestones): editor closed, `tools/run-tests.ps1 -Filter Project`. Report JSON: `Saved/AgentLogs/tests/<timestamp>/index.json`.
- Quick iteration: the testing toolset in unreal-mcp, inside the running editor.

## Screenshots
- Preferred: `pu.take_screenshot("C:/GameDev/<Project>/Saved/AgentLogs/<name>.png")` via `run_python`, then check the file exists (next frame) and Read it. Log line: `LogClient: High resolution screenshot saved as ...`.
- It needs a rendering viewport. If no file appears, the editor is CPU-throttled in the background: `bThrottleCPUWhenNotForeground=False` must be set (it is in `Config/DefaultEditorPerProjectUserSettings.ini`; live: ConfigSettingsToolset `SetSectionProperties` Editor/General/EditorPerformanceSettings).
- `HighResShot` via `execute_console_command(None, ...)` did NOT write a file in testing; avoid it.
- Epic's `EditorAppToolset.CaptureViewport` returns a base64 PNG inline (large); use only when a file path is not needed.

## Troubleshooting
- MCP unreachable: `tools/launch-editor.ps1`; check the editor log for `LogModelContextProtocol`.
- Port conflict: `tools/doctor.ps1` picks a free port and updates `.mcp.json`; Claude Code must be restarted to pick it up.
- Few or no toolsets: the `AllToolsets` plugin is not enabled (`tools/configure-unreal.ps1` with the editor closed).
- Rebuild prompt at editor start ("modules missing or built with a different engine version"): close the editor and run `tools/build.ps1`.

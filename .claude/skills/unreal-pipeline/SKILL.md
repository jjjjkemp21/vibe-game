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
- Batch through the programmatic Python tool (Epic: `ProgrammaticToolset.execute_tool_script`). Template:
  ```python
  import importlib, pipeline_unreal as pu
  importlib.reload(pu)
  print(pu.import_static_mesh(r"C:/GameDev/<Project>/art/export/Props/SM_X.fbx", "/Game/Art/Props", "SM_X"))
  ```
- Serial only: one call at a time (game thread). Only the editor-operator (or the lead) calls unreal-mcp.
- After visible changes: `pu.frame_viewport(target)` then the editor screenshot tool; look at the image.
- Save what you change.

## Unreal Python
- API reference: `Intermediate/PythonStub/unreal.py` (Developer Mode is enabled). Grep before using a class/function.
- Prefer editor subsystems: `unreal.get_editor_subsystem(unreal.EditorActorSubsystem | unreal.LevelEditorSubsystem | unreal.UnrealEditorSubsystem)`, plus `unreal.EditorAssetLibrary` and `unreal.AssetToolsHelpers.get_asset_tools()`.
- Shared helpers: `Content/Python/pipeline_unreal.py` (`import_static_mesh`, `list_level_actors`, `frame_viewport`, `build_golden_level`). Add reusable functions there.
- Headless (editor closed): `tools/unreal-python.ps1` runs `Content/Python/pipeline_cli.py` in the pythonscript commandlet. It does not load a level on its own and cannot take screenshots.

## Importing art
- Exports come from the blender-artist in `art/export/<Category>/<Name>.fbx`.
- `pu.import_static_mesh(src, "/Game/Art/<Category>", "<Name>")` returns `box_extent` in uu (half-size): a 1 m object should show about 50. Values near 0.5 or 5000 mean a unit/scale error in the export.
- Re-import = the same call (replaces the asset).

## Tests
- C++ tests in `Source/<Project>/Tests/`, paths `Project.<Area>.<Name>`, flags `EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter` (if that fails to compile, check `Misc/AutomationTest.h` in the engine for the current flag names).
- Canonical run (before commits and milestones): editor closed, `tools/run-tests.ps1 -Filter Project`. Report JSON: `Saved/AgentLogs/tests/<timestamp>/index.json`.
- Quick iteration: the testing toolset in unreal-mcp, inside the running editor.

## Screenshot fallback
`unreal.SystemLibrary.execute_console_command(None, "HighResShot 1920x1080")` writes to `Saved/Screenshots/WindowsEditor/` on the next frame.

## Troubleshooting
- MCP unreachable: `tools/launch-editor.ps1`; check the editor log for `LogModelContextProtocol`.
- Port conflict: `tools/doctor.ps1` picks a free port and updates `.mcp.json`; Claude Code must be restarted to pick it up.
- Few or no toolsets: the `AllToolsets` plugin is not enabled (`tools/configure-unreal.ps1` with the editor closed).
- Rebuild prompt at editor start ("modules missing or built with a different engine version"): close the editor and run `tools/build.ps1`.

# Claude Code setup phases (K1-K8)

## Context
You are Claude Code running in the root of Jimmy's Unreal Engine 5.8 C++ project on Windows. Claude Cowork (another Claude that controls the desktop through screen control) has already:
- installed the toolchain (Visual Studio 2026, Git + LFS, Epic Games Launcher, UE 5.8, Blender 5.2, Python, uv, Claude Code),
- created this project from the Third Person C++ template,
- written the kit files (CLAUDE.md, .claude/, tools/, art/, Content/Python/, docs/, setup/),
- run `tools/doctor.ps1`, `tools/configure-unreal.ps1` and `tools/build.ps1`,
- launched the editor with `tools/launch-editor.ps1` (Unreal MCP server should be listening).

Cowork is watching this terminal and can do desktop actions for you. Jimmy is available only for sign-ins and approvals.

Read `CLAUDE.md` first, then run K1 to K8 in order.

## Rules for this run
- After each phase, update `setup/SETUP_REPORT.md` (phase log row: PASS / FAIL / BLOCKED / WARN, what you did, evidence paths) and commit it together with that phase's changes.
- Use the scripts in `tools/` exactly as `CLAUDE.md` describes, with forward-slash paths. Long commands (build, tests, editor start): run them in the background or with a timeout of at least 10 minutes, then read `Saved/AgentLogs/status/<script>.json`.
- If a checkpoint fails: read the logs, fix the cause, retry. After two failed attempts at the same checkpoint, mark the phase BLOCKED in the report with the exact error lines and stop.
- Handoff protocol (desktop actions, restarts, dialogs): print ONE line that starts with `>>> COWORK:` followed by exact instructions, write the same text under "Pending handoff" in the report, then stop and wait. Cowork does it and replies `done` (or describes what it saw). Clear "Pending handoff" afterwards.
- Never edit binary assets as text. Never use `--dangerously-skip-permissions`. Never install the PyPI package `blender-mcp` (see K3).
- Save evidence under `Saved/AgentLogs/setup/`.

## K1 - Environment and first commit
1. Run `tools/doctor.ps1`. CHECKPOINT: exit 0; `tools/local.settings.json` has engineDir, blenderExe, ueMcpPort.
2. If `.git` does not exist: `git init`. Run `git lfs install` (safe to repeat).
3. Confirm `.gitignore` and `.gitattributes` exist. `git add -A`, then `git commit -m "Initial UE 5.8 C++ project + AI pipeline kit"`.
4. CHECKPOINT: `git log --oneline` shows the commit; `git lfs ls-files` lists the project's .uasset/.umap files; `git status` is clean; `git check-ignore Saved/x.txt` confirms Saved/ is ignored.

## K2 - Unreal MCP acceptance
1. Run `tools/launch-editor.ps1` (returns quickly if the editor is already running). Exit 0 is required; on exit 2 run it again.
2. Call the `unreal-mcp` tool `list_toolsets`. If unreal-mcp tools are not available in this session: `>>> COWORK: In the Claude Code window type /mcp, select unreal-mcp, choose Reconnect (or Enable), press Esc, then type: done`.
3. CHECKPOINT: the toolset list is substantial (scene/actors, assets, materials, testing, programmatic scripting...). Save the names to `Saved/AgentLogs/setup/unreal_toolsets.txt`. Only a few toolsets means the AllToolsets plugin is not active: check the .uproject, stop the editor, run `tools/configure-unreal.ps1`, relaunch.
4. Use `describe_toolset` on the scene/actor toolset and call a read-only tool that lists actors in the current level. CHECKPOINT: a list of actors comes back.
5. Find the programmatic Python execution tool (Epic documents `ProgrammaticToolset.execute_tool_script`). Run a script that prints `unreal.SystemLibrary.get_engine_version()` and `unreal.Paths.project_dir()`. CHECKPOINT: version starts with 5.8.
6. Take an editor viewport screenshot with the unreal-mcp screenshot tool and look at it. Save or copy it to `Saved/AgentLogs/setup/k2_viewport.png` when the tool gives a file.
7. CHECKPOINT: `Intermediate/PythonStub/unreal.py` exists (Python Developer Mode). If missing: confirm `bDeveloperMode=True` under `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]` in `Config/DefaultEngine.ini`, then restart the editor (stop-editor, launch-editor).
8. CHECKPOINT: Epic's `unreal-mcp` skill from the plugin `unreal-engine-skills-for-claude-code` is available to you. If not: `>>> COWORK: Type /plugin install unreal-engine-skills-for-claude-code@claude-plugins-official and approve. Then type /exit, run tools\open-claude.cmd --continue, and type: continue setup at K2 step 8`.
9. Commit the report.

## K3 - Install the official Blender MCP server (Blender Lab)
Background: the official server is at https://projects.blender.org/lab/blender_mcp (overview: https://www.blender.org/lab/mcp-server/). It has two parts: a Blender add-on/extension (requires Blender 5.1+) that listens on a local socket (default port 9876) for live-session tools, and a Python MCP server (stdio, entry point `blender-mcp`) that Claude Code launches and that can also run Python in a background Blender process. The PyPI package named `blender-mcp` is a DIFFERENT community project: never use `uvx blender-mcp` or `pip install blender-mcp`.
1. `git clone https://projects.blender.org/lab/blender_mcp.git ../_tools/blender_mcp` (that is `C:\GameDev\_tools\blender_mcp`, outside this repo). Read its README and setup documentation completely. Where the repo's instructions differ from the steps below, follow the repo.
2. Add-on: obtain the add-on package the README describes (the zip from the latest release on the repo's Releases page, or build it from the repo's add-on folder with `"<blenderExe>" --command extension build --source-dir <addon folder> --output-dir <out folder>`). Install it headlessly: `"<blenderExe>" --command extension install-file -r user_default -e <zip>`. CHECKPOINT: the install command reports success (`"<blenderExe>" --command extension list` can confirm).
3. Server: install the MCP server with uv as the README describes (for example a virtual environment inside the cloned repo), so that it can be started by an absolute command path. Configure anything the README requires for background mode (such as the path to Blender) using `blenderExe` from `tools/local.settings.json`.
4. Register it in `.mcp.json` as server name `blender` (stdio) with absolute paths (JSON-escape backslashes, or use forward slashes). Keep the `unreal-mcp` entry unchanged. Validate the JSON (`python -c "import json;json.load(open('.mcp.json'))"`).
5. Commit `.mcp.json` and the report.
6. `>>> COWORK: Restart Claude Code to load the blender MCP server: type /exit, run tools\open-claude.cmd --continue, approve the "blender" MCP server if asked, then type: continue setup at K4`

## K4 - Blender MCP acceptance
1. CHECKPOINT: `blender` MCP tools are available. If not: check the server command from `.mcp.json` by running it in a terminal for a few seconds (it should start and wait for input), fix, and hand off another restart.
2. Using the background-mode Python tool, print `bpy.app.version_string`. CHECKPOINT: 5.1 or newer (5.2.x expected).
3. Optional live check: `>>> COWORK: Open Blender 5.2 from the Start menu. Start the MCP add-on's server (look in Edit > Preferences > Add-ons for the MCP add-on's options, or the 3D Viewport sidebar opened with N). Leave Blender open, then type: done`. Call a live tool (for example a scene summary). If the live check fails but background mode works, mark it WARN (not blocking): live mode is only for interactive debugging. Afterwards ask Cowork to close Blender without saving.
4. Commit the report.

## K5 - Golden path (Blender -> Unreal -> level -> screenshot)
1. Run `tools/blender-run.ps1 -Recipe art/recipes/sm_golden_crate.py`. CHECKPOINT: exit 0; `art/export/Props/SM_GoldenCrate.fbx` exists; open `Saved/AgentLogs/previews/SM_GoldenCrate.png` and confirm a brown beveled box; RESULT_JSON dimensions are about 1 x 1 x 1 m.
2. In the live editor, through the unreal-mcp Python execution tool, run one script (replace `<repo>` with this repo's absolute path, forward slashes):
   ```python
   import importlib, pipeline_unreal as pu
   importlib.reload(pu)
   r = pu.import_static_mesh(r"<repo>/art/export/Props/SM_GoldenCrate.fbx", "/Game/Art/Props", "SM_GoldenCrate")
   lvl = pu.build_golden_level()
   print(r)
   print(lvl)
   ```
   CHECKPOINT: `box_extent` is between 48 and 52 on all three axes. If it is near 0.5 or 5000, the unit export is wrong: fix `export_fbx` in `art/lib/pipeline_blender.py`, rerun step 1, reimport.
3. Take a viewport screenshot (the level viewport is already framed on the crate by `build_golden_level`). Look at it. CHECKPOINT: a lit brown crate standing on a floor. Save or copy it to `Saved/AgentLogs/setup/golden_path.png`. Fallback: `HighResShot 1280x720` console command, file in `Saved/Screenshots/WindowsEditor/`.
4. If the live Python route fails after two attempts, use the headless route: save, `tools/stop-editor.ps1`, `tools/unreal-python.ps1 -Function import_static_mesh -ArgsJson '{"src_path": "<repo>/art/export/Props/SM_GoldenCrate.fbx", "dest_dir": "/Game/Art/Props", "name": "SM_GoldenCrate"}'`, then `-Function build_golden_level`, then `tools/launch-editor.ps1`, open the level through unreal-mcp, and take the screenshot. Record which route worked.
5. Commit: "Golden path: crate recipe, import, dev level".

## K6 - C++ automation test (headless)
1. Create `Source/<ProjectName>/Tests/GoldenPathTest.cpp` with the reference code below (use the real module folder name under `Source/`).
2. Save all in the editor through unreal-mcp, then `tools/stop-editor.ps1`. If it reports a blocking dialog: `>>> COWORK: The Unreal Editor shows a dialog while closing. Choose Save (or Save Selected), let it close, then type: done`.
3. `tools/build.ps1`. CHECKPOINT: succeeded. If the automation flag names fail to compile, check `Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h` under engineDir and adapt.
4. `tools/run-tests.ps1 -Filter Project.GoldenPath`. CHECKPOINT: 1 passed, 0 failed; note the report directory.
5. `tools/launch-editor.ps1` until exit 0. Confirm unreal-mcp answers `list_toolsets`; if not, hand off `/mcp` -> Reconnect.
6. Commit: "Add golden path automation test".

Reference test (`Source/<ProjectName>/Tests/GoldenPathTest.cpp`):
```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGoldenPathCrateImportedTest,
	"Project.GoldenPath.CrateImported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGoldenPathCrateImportedTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Props/SM_GoldenCrate.SM_GoldenCrate"));
	if (!TestNotNull(TEXT("SM_GoldenCrate loads"), Mesh))
	{
		return false;
	}
	const FVector Extent = Mesh->GetBounds().BoxExtent;
	TestTrue(TEXT("Crate X extent is ~50 uu (1 m wide)"), FMath::IsNearlyEqual(Extent.X, 50.0, 2.0));
	TestTrue(TEXT("Crate Y extent is ~50 uu (1 m deep)"), FMath::IsNearlyEqual(Extent.Y, 50.0, 2.0));
	TestTrue(TEXT("Crate Z extent is ~50 uu (1 m tall)"), FMath::IsNearlyEqual(Extent.Z, 50.0, 2.0));
	return true;
}

#endif
```

## K7 - Guardrails and team
1. Hook test: use the Write tool to create `Saved/AgentLogs/setup/hooktest/Fake.uasset` with any text. CHECKPOINT: the write is BLOCKED by the guard hook; copy the block message into the report. If the write succeeds, hooks are not active: delete the file, check `/hooks` via handoff to Cowork (`>>> COWORK: type /hooks, take note of what is listed, press Esc, and tell me`), and see the runbook's troubleshooting for the shell-form fallback.
2. Stop hook smoke test from the shell: `echo '{"stop_hook_active": false}' | powershell -NoProfile -ExecutionPolicy Bypass -File .claude/hooks/require-build-on-stop.ps1; echo "exit=$?"`. CHECKPOINT: `exit=0` (the build stamp is newer than all sources after K6).
3. Confirm the four subagent files load (editor-operator, unreal-engineer, blender-artist, qa-tester) and the four skills exist (unreal-pipeline, blender-pipeline, verification, playtest-feedback).
4. Delegate to `qa-tester`: "Check that the golden-path test report from K6 shows a pass, and look at Saved/AgentLogs/setup/golden_path.png and describe it. Report PASS/FAIL with evidence." CHECKPOINT: PASS summary returned.
5. Delegate to `blender-artist`: "Rerun art/recipes/sm_golden_crate.py and report dimensions, triangle count and preview path." CHECKPOINT: success with about 1 x 1 x 1 m.
6. Commit the report.

## K8 - Wrap-up
1. Run `tools/doctor.ps1` once more.
2. Fill in the Acceptance criteria table in `setup/SETUP_REPORT.md` (AC1-AC9) with PASS/FAIL/WARN and evidence paths. Add "Notes for Jimmy": 3-5 plain-language sentences on what now works and what the next step is (design interview, task T-001).
3. `git add -A` and `git commit -m "Setup complete"`.
4. Print: `>>> COWORK: Setup complete. Read setup/SETUP_REPORT.md and report to Jimmy.`

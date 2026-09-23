# Setup report

Updated by Claude Code after every phase of setup/CLAUDE_CODE_SETUP.md. Cowork reads this file to follow progress.

## Pending handoff
>>> COWORK: If a Blender window is already open, close it without saving. Open Blender 5.2 from the Start menu. Start the MCP add-on's server (Edit > Preferences > Add-ons > MCP: click Start, or the 3D Viewport sidebar opened with N). Leave Blender open, then type: done

## Phase log
| Phase | Status | What was done | Evidence |
|---|---|---|---|
| K1 Environment + first commit | PASS | doctor exit 0 (UE 5.8, Blender 5.2.2 LTS, VS 18.5, git 2.51 + LFS 3.7, uv 0.12.18, Claude Code 2.1.280, MCP port 8000). git init + lfs install; added `*.slnx` to .gitignore (generated solution files); first commit 8858e7c. 753 LFS files incl. 4 .umap; status clean; Saved/ ignored. | Saved/AgentLogs/status/doctor.json, tools/local.settings.json |
| K2 Unreal MCP acceptance | PASS | launch-editor exit 0. 52 toolsets listed (AllToolsets active). SceneTools.find_actors returned 72 actors in /Game/ThirdPerson/Lvl_ThirdPerson. **Fix:** Epic's ProgrammaticToolset.execute_tool_script is a sandbox with no `unreal` module, so added project toolset `vibegame_tools.VibeGamePipelineTools` (run_python / run_pipeline), registered at editor start by Content/Python/init_unreal.py; it printed engine 5.8.3-58210709+++UE5+Release-5.8, project dir C:/GameDev/VibeGame/. **Fix:** screenshots never rendered because the editor was CPU-throttled in the background (~2 fps); set bThrottleCPUWhenNotForeground=False (live + Config/DefaultEditorPerProjectUserSettings.ini); added pu.take_screenshot. Screenshot checked: Third Person template level. PythonStub present (bDeveloperMode=True). Updated unreal-pipeline skill, editor-operator agent, CLAUDE.md. Step 8: Cowork installed plugin unreal-engine-skills-for-claude-code (v3.1.1); skill `unreal-mcp` loaded after restart; unreal-mcp answered again (get_current_level). | Saved/AgentLogs/setup/unreal_toolsets.txt, Saved/AgentLogs/setup/k2_viewport.png, Saved/Logs/VibeGame.log (`VibeGamePipelineTools registered`) |
| K3 Blender MCP install | PASS | Cloned https://projects.blender.org/lab/blender_mcp.git (ff54e4d) to C:\GameDev\_tools\blender_mcp. Add-on built from addon/blender_mcp_addon (`extension build`, mcp-1.0.0.zip) and installed with `extension install-file -r user_default -e` ("STATUS Installed \"mcp\""; `extension list`: mcp [installed]). Server: uv venv mcp/.venv, `uv pip install -e .` (blender-mcp 1.0.2), entry mcp/.venv/Scripts/blender-mcp.exe. Registered in .mcp.json as `blender` (stdio, BLENDER_PATH = Blender 5.2 exe, port 9876); JSON valid; unreal-mcp entry unchanged. **Fix:** every background call timed out (120 s) under MCP stdio on Windows because Blender inherited the server's stdin pipe; local patch adds `stdin=subprocess.DEVNULL` in blmcp/tools_helpers/blender_cli.py (saved as setup/patches/blender_mcp_stdin_devnull.patch). Stdio smoke test after patch: 26 tools, execute_blender_code_for_cli returned version 5.2.2 LTS in 5 s. Made C:/GameDev/_tools/empty.blend for file-less background calls. | Saved/AgentLogs/setup/k3_blender_mcp_smoke.txt, setup/patches/blender_mcp_stdin_devnull.patch |
| K4 Blender MCP acceptance | IN PROGRESS (steps 1-2 PASS, live check pending) | After restart the 26 `blender` MCP tools are available. execute_blender_code_for_cli (background) returned bpy.app.version_string = 5.2.2 LTS, binary C:/Program Files/Blender Foundation/Blender 5.2/blender.exe. | Claude Code session transcript |
| K5 Golden path | TODO | | |
| K6 C++ automation test | TODO | | |
| K7 Guardrails + team | TODO | | |
| K8 Wrap-up | TODO | | |

## Acceptance criteria
| ID | Criterion | Status | Evidence |
|---|---|---|---|
| AC1 | Toolchain: VS 2026 (or 2022 17.14+) C++ game workloads, UE 5.8, Blender 5.1+, Git + LFS, Claude Code, uv | TODO | |
| AC2 | Project builds with tools/build.ps1 | TODO | |
| AC3 | Editor starts with the Unreal MCP server listening; Python API stub exists | TODO | |
| AC4 | Claude Code: unreal-mcp connected, toolsets listed, Python execution works, Epic plugin installed | TODO | |
| AC5 | Blender MCP (official Blender Lab server) registered as `blender`; background Python returns the Blender version | TODO | |
| AC6 | Golden path: recipe -> FBX + preview -> imported SM_GoldenCrate with box extent ~50 uu -> L_GoldenPath saved -> screenshot shows crate on floor | TODO | |
| AC7 | Automation test Project.GoldenPath.CrateImported passes headless | TODO | |
| AC8 | Guardrails: binary-asset hook blocks a .uasset write; Stop hook runs; git repo with LFS tracking .uasset/.umap; commits present | TODO | |
| AC9 | Team: 4 subagents + 4 skills present; qa-tester and blender-artist delegations succeeded | TODO | |

## Warnings / non-blocking issues

## Notes for Jimmy

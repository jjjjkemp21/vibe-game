# Setup report

Updated by Claude Code after every phase of setup/CLAUDE_CODE_SETUP.md. Cowork reads this file to follow progress.

## Pending handoff
(none)

## Phase log
| Phase | Status | What was done | Evidence |
|---|---|---|---|
| K1 Environment + first commit | PASS | doctor exit 0 (UE 5.8, Blender 5.2.2 LTS, VS 18.5, git 2.51 + LFS 3.7, uv 0.12.18, Claude Code 2.1.280, MCP port 8000). git init + lfs install; added `*.slnx` to .gitignore (generated solution files); first commit 8858e7c. 753 LFS files incl. 4 .umap; status clean; Saved/ ignored. | Saved/AgentLogs/status/doctor.json, tools/local.settings.json |
| K2 Unreal MCP acceptance | PASS | launch-editor exit 0. 52 toolsets listed (AllToolsets active). SceneTools.find_actors returned 72 actors in /Game/ThirdPerson/Lvl_ThirdPerson. **Fix:** Epic's ProgrammaticToolset.execute_tool_script is a sandbox with no `unreal` module, so added project toolset `vibegame_tools.VibeGamePipelineTools` (run_python / run_pipeline), registered at editor start by Content/Python/init_unreal.py; it printed engine 5.8.3-58210709+++UE5+Release-5.8, project dir C:/GameDev/VibeGame/. **Fix:** screenshots never rendered because the editor was CPU-throttled in the background (~2 fps); set bThrottleCPUWhenNotForeground=False (live + Config/DefaultEditorPerProjectUserSettings.ini); added pu.take_screenshot. Screenshot checked: Third Person template level. PythonStub present (bDeveloperMode=True). Updated unreal-pipeline skill, editor-operator agent, CLAUDE.md. Step 8: Cowork installed plugin unreal-engine-skills-for-claude-code (v3.1.1); skill `unreal-mcp` loaded after restart; unreal-mcp answered again (get_current_level). | Saved/AgentLogs/setup/unreal_toolsets.txt, Saved/AgentLogs/setup/k2_viewport.png, Saved/Logs/VibeGame.log (`VibeGamePipelineTools registered`) |
| K3 Blender MCP install | TODO | | |
| K4 Blender MCP acceptance | TODO | | |
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

---
name: blender-pipeline
description: How to create, export, preview, and validate 3D assets with Blender 5.2 for this Unreal project - recipe scripts in art/recipes, helpers in art/lib/pipeline_blender.py, headless runs with tools/blender-run.ps1, Unreal-ready export rules, preview renders, budgets, and when to use the Blender MCP server. Use for ANY modeling, material, rigging, animation, or asset-sourcing task.
---
# Blender pipeline (Blender 5.2 LTS)

## Every asset is a recipe
- One script per asset: `art/recipes/<prefix>_<name>.py` (e.g. `sm_barrel_wood.py`). It builds the asset from scratch, then calls `pb.finish(args, objects)` which exports, renders a preview, and prints RESULT_JSON.
- Start from `art/recipes/sm_golden_crate.py` as the template.
- Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/<file>.py`
  - export: `art/export/<Category>/<Asset>.fbx`
  - preview: `Saved/AgentLogs/previews/<Asset>.png`
  - result: `Saved/AgentLogs/blender/<recipe>.result.json`
- Independent recipes can run in parallel (separate Blender processes).
- When `docs/ART_STYLE.md` is filled in, put its palette, bevel sizes, and budgets in `art/lib/style.py` and import it from recipes.

## Unreal-ready rules
- Metric, unit scale 1.0 (1 unit = 1 m = 100 uu). Z up.
- Props: pivot at bottom center (move the mesh data, keep the object at the origin).
- Name object and mesh data with the Unreal asset name (SM_...), materials M_...; one material slot per distinct surface.
- Modifiers are applied on export (`use_mesh_modifiers=True`).
- Default triangle budgets until ART_STYLE.md says otherwise: small prop <= 2k, large prop <= 10k, hero prop <= 30k.

## Verify visually, every time
Open the preview PNG after each run. Compare with the request and ART_STYLE.md. Fix the recipe before handing the export to Unreal.

## What code-built art is good and bad at (be honest with the lead)
- Good: props, architecture, modular kits, stylized or low-poly items, mechanical animation (doors, lifts, pickups), procedural variation (geometry nodes).
- Weak: realistic organic characters, faces, hand-keyed character animation. For characters, prefer the Unreal mannequin with Epic's Game Animation Sample animations (retargeted in Unreal), or an AI-generated or marketplace mesh cleaned up here. Never spend paid credits on external services without Jimmy's OK.

## Blender MCP server (`blender`, official Blender Lab server)
- Background-mode Python tool: `execute_blender_code_for_cli(blend_file, code)`; `code` must assign a dict to `result`. `blend_file` must be a real .blend (an empty string makes Blender try to open the working folder): use `C:/GameDev/_tools/empty.blend` when you have no file.
- Install: clone `C:/GameDev/_tools/blender_mcp` (uv venv in `mcp/.venv`, server `mcp/.venv/Scripts/blender-mcp.exe`, `BLENDER_PATH` set in `.mcp.json`). It carries a local patch, `setup/patches/blender_mcp_stdin_devnull.patch`: without it every background call hangs 120 s on Windows. Re-apply it (`git apply`) after pulling the clone.
- Live tools need Blender open with the add-on's server started (ask Cowork via the lead).
- Use MCP for inspection and debugging; production changes go into recipes so they are reproducible.
- The PyPI package called `blender-mcp` is a different community project: never install it.

## Animation (animation-artist)
- Rig and animation recipes build ON the model-artist's mesh recipe (import its export or call its build function); never hand-edit the mesh inside an animation recipe.
- 30 fps. Action names = Unreal asset names (`A_<Subject>_<Action>`, e.g. `A_FishGeneric_Swim`, `A_FPArms_Cast`). One Blender action per clip; push each to its own NLA track before export so all clips export.
- Export skeletal assets with `apply_scale_options="FBX_SCALE_ALL"`, `add_leaf_bones=False`, `bake_anim=True`, `bake_anim_use_nla_strips=True`, `bake_anim_use_all_actions=False` (see Troubleshooting). Keep the root bone at the origin; use root motion only where the spec says so.
- Scale over species: one rig per body type (e.g. a spine bone chain for all fish) with procedural or parameterized motion (amplitude, frequency, speed) that the game drives from data. Don't make per-species clips unless a species truly moves differently.
- Every animated export gets `art/export/<Category>/<Name>.anim.md`: skeleton, actions (frame range, loop or one-shot, root motion, notify frames), and Unreal import/retarget/montage notes.
- Preview: render a key-frame strip or contact sheet to `Saved/AgentLogs/previews/<Name>_anim.png` and look at it (pops, sliding, broken weights, interpenetration).

## Troubleshooting
- FBX operator missing under `--factory-startup`: `ensure_fbx_exporter()` enables `io_scene_fbx`; or export GLB with `bpy.ops.export_scene.gltf(export_format='GLB')`.
- Wrong size in Unreal (extent near 0.5 or 5000 instead of 50 for 1 m): check `apply_unit_scale` / scene unit scale.
- Skeletal meshes (armatures): the default export puts the unit conversion on object transforms, which often causes a 100x armature scale in Unreal. For rigged assets export with `apply_scale_options="FBX_SCALE_ALL"` and `add_leaf_bones=False`, then compare the imported skeleton height with the Unreal mannequin before building on it.
- Blender 5.x API differences: check at runtime (`hasattr`, `dir()`), or use the MCP server's API lookup, before assuming older API names.

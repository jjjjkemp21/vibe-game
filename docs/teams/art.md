# Art department handbook

Owner: `art-manager-high`. Team: `model-artist-*` and `animation-artist-*` at junior, mid and senior level.
This file adds the art standards on top of `docs/teams/STUDIO.md` (flow, team log, escalation, reporting). Read that first; this file doesn't repeat it.

**The bar:** every asset looks like it belongs in the same game as the approved set, at the distance and in the light the player sees it. Art matters as much as systems (Jimmy, 2026-09-23). "It works" is not done; "it reads, it matches, it holds up in play" is.

## 1. Art bible: ART_STYLE.md is the single source
- Read `docs/ART_STYLE.md` and look at the mood boards `art/reference/moodboard_a..d_*.png` before briefing or reviewing. Don't copy rules into packets; point to the section.
- Colors come only from `art/lib/style.py` (`palette(region)`, `make_material`, `mix_hex`). No invented hex values in recipes. A new color is a proposal to the lead (taste = Jimmy), then added to ART_STYLE.md and style.py before use.
- The accent red `#FF4D3D` is for the bobber, UI highlights and the listed landmarks only. Nothing else red on fishing water.
- Shapes: chunky, slightly exaggerated, bevel 2-3% of the smallest dimension with 1-2 segments (`style.add_bevel`). No fine detail that turns into noise at distance.
- Materials: flat palette color, roughness 0.6-0.9, metallic 0; wet/glossy 0.25-0.45. Only small shared textures, never photo textures.
- Dredge is a mood reference, never a source. Don't trace its shapes, creatures, boats or UI, and keep no Dredge captures in `art/reference/`.
- Mood lives in Unreal (light, fog, water, sky). Assets stay neutral enough to work in every region's preset.
- Changing the bible: the manager proposes, the lead decides with Jimmy, then ART_STYLE.md is updated in the same commit as style.py.

## 2. Pipeline and review gates
Every new asset or clip goes through these stages. Each gate is Accept, Rework (numbered change requests) or Escalate.

| # | Stage | Who | Output | Gate |
|---|---|---|---|---|
| 0 | Brief | manager | `Saved/AgentLogs/tasks/<id>/brief.md` | self-check against the packet list below |
| 1 | Blockout | artist | recipe with primary forms, final scale, pivot and facing, flat palette colors; blockout sheet (§4) | **Manager review A:** silhouette, proportion, scale, readability. Hero assets and new families: the manager may ask the lead for an early designer look |
| 2 | Detail | artist | bevels, secondary forms, materials, topology loops, UVs if textured, sockets, `UCX_` collision | artist self-check against §7 |
| 3 | Final previews | artist | the full preview set (§4), RESULT_JSON, report | **Manager review B:** the whole §7 checklist, previews opened by the manager |
| 4 | Designer review | designer, booked through the lead | `Saved/AgentLogs/design/<ts>-<id>.md` | APPROVED, or APPROVED WITH CHANGES with every must-fix done and re-checked |
| 5 | Import spec | artist | `.import.md` or `.anim.md` next to the export (§9) | manager checks it against the export |
| 6 | Editor import | editor-operator, booked through the lead | PIE screenshot | manager compares it with the previews (scale, facing, materials, light) |

- Animation runs the same gates with its own stages: key poses (a stepped pose strip) → gate A → timing and splines → polish → final strip → gate B.
- Put the stage stop in the packet ("stop after blockout and report"). A blockout that fails gate A never goes to detail.
- Short path: fixes, recolors and variants of an approved asset skip stages 1-2. A re-export with no visual change also skips stage 4.

**Art packet additions** (on top of the LEAD.md brief template): asset name and category; budget class (§5); how the player sees it (held in first person / at 5 m / spotted at 20 m; which region and time of day); ART_STYLE sections and mood boards; the approved assets it must match; animation needs (pivot, forward axis, bend points, vertex group or part names, agreed between both artists and written into both packets); the stage stop; the required preview set.

## 3. Naming, units, pivot, export
The `blender-pipeline` skill is the rulebook; the key points:
- Recipes: `art/recipes/sm_*.py` (static), `sk_*.py` (skinned mesh), `anim_*.py` (rig and clips), `preview_*.py` / `ref_*.py` (lineups, mood boards). One recipe per asset; rerunning it reproduces it exactly (fixed seeds, no manual steps).
- Unreal names: `SM_`, `SK_`, `SKEL_`, `A_<Subject>_<Action>`, materials `M_<Asset>_<Surface>`; shared surfaces keep their shared name (e.g. `M_Fish_Eye`). Object name = mesh data name = asset name. Collision `UCX_<Asset>_NN`, sockets `SOCKET_<name>`.
- 1 Blender unit = 1 m, Z up. Directional assets face Blender +X (ART_STYLE "Orientation rule"). Props: pivot at bottom center. Fish, creatures and rigs: pivot and root as written in their `.anim.md`.
- Exports go to `art/export/<Category>/` (Props, Fish, Characters, Environment), matching `/Game/Art/<Category>`. Skeletal FBX only through `pb.export_skeletal_fbx()` (centimeters, every bone at scale 1.0). 30 fps, one action per clip.

## 4. Preview standard
Previews go to `Saved/AgentLogs/previews/<Asset>*.png`, 768 px per cell, built with `pb.finish(views=...)` / `pb.contact_sheet` / `art/lib/fp_preview.py`. Throwaways are `exp_*`.
- **Blockout sheet (gate A):** side profile (facing +X), front 3/4, top, plus the in-game camera view, with a 1.8 m figure or the FP hands for scale.
- **Model sheet (gate B):** front 3/4, side profile, top, back 3/4 in neutral light; plus a black-fill silhouette (side and top) for fish, creatures, held items and hero props.
- **In-game camera:** held items from the first-person camera (`fp_preview.py`, 90 deg horizontal FOV, day and dusk backdrops; lower third, centre 40% clear). World props from 1.7 m eye height at interaction distance (~3-5 m) and spotting distance (15-20 m) in the tropical day light. Fish side-on at 5 m and seen from the dock. Creatures at 20-30 m in fog or dusk.
- **Turntable strip:** 8 frames at 45 deg steps in one row, for hero assets, new families and anything that will be animated.
- **Lineup:** the new asset at true scale next to the approved assets it must match (`preview_fish_lineup.py`, the rod in the FP hands, the cooler on the dock).
- **Animation:** `<Name>_anim.png`, at least 8 labelled frames from the gameplay camera plus a side or top strip. Loops show the first and last frame side by side; one-shots mark the gameplay event frame.

## 5. Budgets
Aim at 50-70% of a cap: the low-poly look comes from chunky shapes, not from density. Spend triangles on the silhouette.

| Class | Tris (ART_STYLE) | Texture | Material slots* | Bones* |
|---|---|---|---|---|
| Small prop (background) | 2k | 512 or none | 3 | - |
| Large prop | 10k | 512 | 5 | - |
| Hero / held / interacted prop | 30k (low-poly: usually far less) | 1024 | 6 | a few non-deforming (lids, handles) |
| Fish | 3k | 512 shared pattern masks | 6 | shared `SKEL_Fish` (11); a new body type 16 or fewer |
| Boat | 8k | 1024 | 6 | none (bob and sway are procedural) |
| Island module | 10k | 512 | 4 | - |
| FP arms | hero | 1024 | 4 | `SK_FPArms` (18); 32 or fewer |
| Creatures (shark, shadow) | hero cap, target 10k or fewer | 1024 | 6 | 40 or fewer |
| NPCs / other players | hero | 1024 | 6 | the UE mannequin skeleton (retargeted), no custom humanoid rig |

\* Department defaults (P-003c) until ART_STYLE.md sets them. Textures are power of two; prefer the shared gradient, wood strip and fish masks over new per-asset textures. Check triangles with `style.check_budget(tris, kind)` in RESULT_JSON.

## 6. Kits and consistency
- **Reuse first.** Shared code lives in `art/lib/`: `style.py`, `pipeline_blender.py`, `meshkit.py`, `fishkit.py`, `fishrig.py`, `fp_preview.py`. Look there before writing a helper; a builder used by two assets moves into `art/lib/`.
- **One builder per family.** New fish are `fishkit` parameter sets, each with one strong identifying feature (fin, jaw, stripe, glow). Dock, island and shop pieces share one kit recipe each. A new family member is parameters, not new code.
- **One rig per body type.** Fish share `SKEL_Fish`; motion is parameterized (amplitude, frequency, speed) and driven from data. Per-species clips only when a species truly moves differently.
- **Same surface, same material.** Wood, rope, metal fittings, fish eyes and skin reuse the existing material names and presets across assets.
- **Kit pieces** snap on the grid written in the kit recipe's header, with the pivot at the snap point; variants keep the connection points identical.
- **The approved reference set** (compare every new asset with at least one in a lineup): `SK_FPArms`, `SM_Rod_Basic`, `SM_Bobber`, `SM_Bonefish`, `SM_CoralSnapper`, `SM_Cooler_Starter`, and the mood boards. Add each newly approved asset to this list.
- **Shared library edits** (`art/lib/*`): one owner at a time, named in every affected packet. After a change, rerun every dependent recipe and compare RESULT_JSON (tris, dimensions, bones) to prove nothing drifted, or re-export and re-review the ones that changed on purpose.
- New conventions go into this handbook in the same commit that introduces them.

## 7. Review checklist (the manager applies it at gates A and B)
Open every preview yourself. Cite the view or frame in each change request.
1. **Silhouette:** reads as a black fill; one clear identifying feature; creatures read as danger through shape.
2. **Readability at game distance:** holds up in the in-game camera views; no noise detail; tracked gameplay objects use their readability scale (ART_STYLE).
3. **Palette:** style.py colors only; on-screen within ~10% of the hex under the day preset; the red accent rule; fits the target region.
4. **Scale and proportion:** RESULT_JSON dimensions in meters make sense against the 1.8 m figure or the FP hands; exaggeration is intentional and chunky; fish length follows ReferenceWeight.
5. **Shape language and shading:** bevel rule, faceted vs smooth shading on purpose, matches its family and the reference set.
6. **Topology:** manifold, no stray or duplicate vertices, normals out, no n-gons where it deforms, triangles spent on the silhouette.
7. **UVs** (textured assets only): no unintended overlaps, texel density consistent with the family, within the texture budget.
8. **Setup:** pivot, +X facing, names, material slots, sockets and `UCX_` collision as specified.
9. **Rig and deform:** bone names and count, root at the origin, a clean rest pose, weights without spikes, volume held at bends, the export scale check passed.
10. **Loops:** edge loops where it bends; looping clips have no pop at the wrap (first frame = last frame).
11. **Motion:** clear anticipation and strong poses, 30 fps snappy timing, one-shots short, gameplay event frames marked in the spec.
12. **Clipping:** nothing passes through anything: rod through the hand, fingers through handles, fish through cooler walls, arms breaking the first-person framing rule.
13. **Budget:** tris, textures, slots and bones within §5.
14. **Reproducible and honest:** the recipe rerun matches; the report names what the artist compared against and what it changed.

## 8. Levels (who gets what)
- **senior-max:** new shapes and families, new species, hero assets (boat, shark, the shadow creature, NPC looks), new rigs, hard clips (first-person interactions with contacts, creature motion), and any task that already failed twice at mid.
- **mid-high:** anything the player sees often or up close (arms, rod, bobber, fish, cooler, anything held, carried or interacted with), new members of an existing family built from its kit, standard clips, and must-fix rework on those.
- **junior-medium:** fixes to approved assets, re-exports, recolors, parameter variants of approved assets, small background props seen only at distance, import spec updates. A junior that finds new shapes or new rigging in its task stops and reports back.
- Model and animation work on the same asset are separate tasks in order: the mesh recipe first (gate B), then the rig and clips build on it. The rig never edits the mesh by hand.

## 9. Where art works, designer review and import
- **Art works in main**, not in lanes (lanes are for C++). Each artist commits only its own paths with `git commit -- <paths>`. Parallel artists own disjoint files (recipes, exports, preview names); the packet lists them.
- **Designer review:** after gate B, tell the lead "design review ready: <ids>, previews <paths>, compare with <assets>, seen from <camera/distance>". The lead starts the designer (Design department). Must-fix items become a numbered rework packet. You decide should-fix items and log the decision. If a must-fix changed the silhouette, palette or motion noticeably, ask for a re-review.
- **Import spec** (stage 5), next to the export:
  - Static meshes: `art/export/<Category>/<Asset>.import.md` with the destination folder, new or reimport, the `pipeline_unreal` function, materials (import or reuse which `M_`), collision, sockets, expected bounds in cm, the facing check, and what references it (Blueprints, data rows).
  - Skeletal assets and clips: `<Name>.anim.md` (blender-pipeline skill: skeleton, clips, loops, notifies, Convert Scene Unit OFF).
- **Import:** tell the lead "import ready: <assets>, specs <paths>, commit <hash>, ~N min editor time, PIE check: <what to screenshot>". The lead books the editor and picks the editor-operator. Check the returned screenshot against the previews before you close.
- Anything that needs code or data (sockets used by C++, AnimInstance changes, DT rows such as ReferenceWeight) is a cross-department request through the lead, never an edit by the art team.

## 10. Definition of done (art)
An asset or clip is done when all of these hold:
1. The recipe is committed and reruns to the same RESULT_JSON; the export is in `art/export/<Category>/`.
2. The full preview set (§4) exists, and both the artist and the manager looked at it.
3. It is within budget (§5) and passed checklist §7 at gate B.
4. The designer APPROVED it, with every must-fix done.
5. The import spec is written, the editor-operator imported it, and a PIE screenshot matches the previews.
6. ART_STYLE.md or this handbook is updated if a convention changed (new color via Jimmy, the approved reference set, a kit grid).
7. The team log `Saved/AgentLogs/teams/art.md` and the task line are current.

# SK_FPArms: rig + animation spec (T-004)

Source of truth: `art/recipes/anim_fp_arms.py` (rig + actions, built on the model-artist's `art/recipes/sk_fp_arms.py`
mesh; SM_Rod_Basic from `art/recipes/sm_rod_basic.py` is staged only for checks and previews). Rerun:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fp_arms.py`
(deterministic: same rig, same keys). All numbers below come from its RESULT_JSON
(`Saved/AgentLogs/blender/anim_fp_arms.result.json`).

Previews (`Saved/AgentLogs/previews/`; first-person frames are 1920x1080 over the palette backdrops of
`art/lib/fp_preview.py`, tropical day and dusk):
`SK_FPArms_anim.png` (contact sheet), `SK_FPArms_anim_fp.png` / `_anim_fp_dusk.png` (HoldRod_Idle standing),
`SK_FPArms_prone_fp.png` / `_prone_fp_dusk.png` (Prone_HoldRod_Idle at a dock edge), `SK_FPArms_prone_hold_gap_fp.png`
(prone hold under a 60 cm ceiling), `SK_FPArms_prone_tuck_gap_fp.png` (tuck in the 60 cm gap),
`SK_FPArms_prone_wall_fp.png` (tuck, wall 50 cm ahead), `SK_FPArms_prone_blend.png` (hold -> tuck crossfade and the
rod-tip path), `SK_FPArms_prone_clearance.png` (side/top views with the ceiling, floor and wall lines).
`SK_FPArms_cm_reimport.png` (2026-09-23 unit check: the old meter files, left, and the new cm files, right,
re-imported and posed by HoldRod_Idle / Prone_TuckRod frame 0, eye and side views; identical, max 0.0004 mm).

## Files (art/export/Characters/)

| File | Content | Import as |
|---|---|---|
| `SK_FPArms.fbx` | skinned mesh (1456 tris, 2 materials) + 16-bone skeleton, bind pose, **no animation** | Skeletal Mesh `SK_FPArms`, skeleton **`SKEL_FPArms`** |
| `A_FPArms_Idle.fbx` | armature only, one take `A_FPArms_Idle` | Animation on `SKEL_FPArms` |
| `A_FPArms_HoldRod_Idle.fbx` | armature only, one take | Animation on `SKEL_FPArms` |
| `A_FPArms_StanceDip.fbx` | armature only, one take | Animation on `SKEL_FPArms`, set **additive** (below) |
| `A_FPArms_Prone_HoldRod_Idle.fbx` | armature only, one take | Animation on `SKEL_FPArms` |
| `A_FPArms_Prone_TuckRod.fbx` | armature only, one take | Animation on `SKEL_FPArms` |

One clip per file, so the asset name = file name = take name.

**2026-09-23: all six files re-exported in CENTIMETERS** (fix for the 100x `root` bone scale found at import). Same
meshes, bones, rest pose, rolls, `hand_r_rod`/`hand_l_crank` frames, clips, names and timings; only the unit changed.
All six must be re-imported (steps under "Unreal import").

## Axes, scale, origin

- Origin = **camera / eye point**. Attach the arms mesh to the first-person camera with a **zero relative
  transform**. No root motion anywhere; the `root` bone never moves.
- Unreal = Blender x 100 cm with Y negated: Blender +X = Unreal +X (forward), Blender +Y (left) = Unreal -Y, Z up.
- **Units: the files are centimeters.** FBX header `UnitScaleFactor` = `OriginalUnitScaleFactor` = **1.0** exactly;
  bone translations, vertices and translation keys are cm values (e.g. `hand_r` bind head (48, -17, -21.5) in the file);
  no scale on any node: armature node, mesh node, all 16 bones and every scale key are 1.0 (largest deviation
  3.6e-7, float noise). Nothing for Unreal to convert, so every bone imports at scale 1.0.
- Export: `pb.export_skeletal_fbx()` in `art/lib/pipeline_blender.py` (shared by every skeletal asset; settings in
  `SKELETAL_FBX_SETTINGS` / `SKELETAL_FBX_BAKE_SETTINGS`). The rig is authored in meters; the export works on temporary
  x100 copies (bones, vertices, object and pose `location` keys; rotations, rolls and scale keys untouched) with the
  scene unit scale at 0.01 and `apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE"` ("All Local"),
  `global_scale=1.0`, `axis_forward=-Z, axis_up=Y` (unchanged), `add_leaf_bones=False`, primary bone axis Y /
  secondary X, `use_armature_deform_only=False`, `armature_nodetype=NULL`, `use_mesh_modifiers=True`,
  `mesh_smooth_type=FACE`; clips: NLA strips baked at 30 fps, all bones keyed, step 1, no curve simplification.
  The export reads the written file back and fails unless the header is 1.0 and every node/scale key is within 1e-5
  of 1.0 (RESULT_JSON `fbx_units`). The Blender armature object is named `Armature`, so Unreal's FBX importer drops
  that node and `root` becomes the skeleton root.
- Checked against the previous (meter) files, value by value: node translations and translation keys exactly x100
  (max error 2.4e-5 cm), rotations identical (max 4.6e-5 deg), vertices x100 (3.6e-6 cm), per-corner normals, UVs,
  weights, key times and take names/ranges identical. Against the model-artist's static `sk_fp_arms.py` export: same
  vertices in Unreal space (max 3e-5 cm), same bounds x -12.4..67.9, y +-26.0, z -34.8..-16.1 cm, same -90 deg axis
  node, same forward axis.
- Blender re-import check (RESULT_JSON `reimport_check`): 16 bones, bone heads and axes 0.0 mm / 0.0 deg off (the
  importer puts its cm -> m factor 0.01 on the armature object; bone heads in the armature are the source x100 with
  0.0 mm error; rest and posed bone scales 1.0 within 1.1e-6), mesh bounds identical, baked poses of all 5 clips
  0.0 mm / 0.0 deg off at frames 0/30/45/60/90, take ranges 0-90 / 0-90 / 0-8 / 0-90 / 0-90.
- Expected in Unreal: mesh bounds about 80 x 52 x 19 cm; `hand_r` at (48, 17, -21.5) cm in component space; the
  `root` bone's local scale 1.0. **If the skeleton shows an extra `Armature` root bone, any bone has a scale other than
  1.0, or the bounds are 100x off, stop and report it**; don't fix it in the editor.

## Skeleton `SKEL_FPArms` (16 bones, unchanged)

Bone heads in Unreal component space (cm, +Y = right). Blender bones run along their local Y axis (FBX default), so
don't read bone "forward" as X.

| Bone | Parent | Deforms | Head (cm) | Purpose |
|---|---|---|---|---|
| `root` | - | no | (0, 0, 0) | camera point, identity rotation, never animated |
| `arms` | root | no | (-10, 0, -25) | chest pivot for the whole arm rig (StanceDip, optional sway) |
| `upperarm_l` / `_r` | arms | yes | (-8, -+19, -23) | shoulders sit behind/below the eye, out of view; no clavicle |
| `lowerarm_l` / `_r` | upperarm | yes | (22.3, -+20.4, -29.2) | elbow hinge (bone Z = hinge axis) |
| `lowerarm_twist_l` / `_r` | lowerarm | yes | (35.2, -+18.7, -25.4) | takes 60 % of the hand's roll about the forearm (anti candy-wrapper) |
| `hand_l` / `_r` | lowerarm | yes | (48, -+17, -21.5) | wrist |
| `fingers_l` / `_r` | hand | yes | (58.5, -+16.0, -20.7) | mitten finger block (curl at the knuckles) |
| `thumb_l` / `_r` | hand | yes | (53.1, -+13.2, -19.5) | thumb |
| **`hand_r_rod`** | hand_r | no | (54.1, 13.8, -24.1) | **rod attachment**: its frame IS SM_Rod_Basic's pivot frame (X tip, Z up, reel below). **Animated in `A_FPArms_Prone_TuckRod`** (see below); in every other clip it keeps its bind offset in the fist |
| **`hand_l_crank`** | hand_r_rod | no | (50.5, 3.8, -30.9) | left-hand IK target: the `hand_l` transform that holds SM_Rod_Basic's crank knob (socket CrankKnob; checked against the B-M1 rod: 0.03 mm) |

(`_l` values have -Y, `_r` values +Y.) Skin: 12 deform groups, max 2 influences per vertex, weights sum to 1.0,
3-loop blends at elbows and wrists, plus a forearm twist ramp from the cuff to the wrist.

## Actions (30 fps, root motion: none)

| Asset | Frames | Length | Type | What it is |
|---|---|---|---|---|
| `A_FPArms_Idle` | 0-90 | 3.0 s | loop | empty hands low in view, relaxed curl, one slow breath |
| `A_FPArms_HoldRod_Idle` | 0-90 | 3.0 s | loop | standing/crouched: right fist on the rod grip, left fist on the crank knob, rod tip up 36 deg; rod sways +-1.2 deg pitch / +-0.8 deg yaw with the breath; hands solved from the rod (no sliding) |
| `A_FPArms_StanceDip` | 0-8 | 0.267 s | one-shot, **additive** | only `arms` moves: drops 3.5 cm and pitches 3 deg forward at frame 3, overshoots at 6, rest at 8 |
| `A_FPArms_Prone_HoldRod_Idle` | 0-90 | 3.0 s | loop | **prone and still**: fishing-ready, rod held low and nearly level (11.5 deg up, 2 deg right), tip just above the horizon; elbows tucked under the chest; steadier sway (+-0.6 deg pitch / +-0.5 deg yaw) |
| `A_FPArms_Prone_TuckRod` | 0-90 | 3.0 s | loop | **prone and crawling**: rod turned around in the right fist (ice-pick grip near the butt), running back along the forearm and the body; both fists peek in at the bottom corners; breath only (the crawl rhythm is the procedural prone bob, below) |

Notifies: none needed. All four loops are 3.0 s: put their players in one sync group `FPArmsBreath` so crossfades
stay in phase. Motion check (RESULT_JSON `motion_check`): loops have max per-frame step <= 0.8 mm (rod tip <= 2.7 mm)
and first/last delta 0.0 mm (no pop at the seam); StanceDip starts and ends exactly at rest.

### HoldRod_Idle recomposed (designer B-S1)
- `hand_r_rod` at frame 0, arms component space: **(42.9, 21.0, -23.0) cm, rotation P 35.1 / Y -4.8 / R 0.0**
  (was (42.9, 16.0, -20.3), Y -0.3): 5 cm right, 2.7 cm down, rod yawed 4.5 deg left so the tip stays put. The left
  fist follows the crank: `hand_l` / `hand_l_crank` (46.0, 9.2, -26.1) cm, P -9.9 / Y 113.7 / R 17.6.
- On screen (1920x1080, 90 deg): fists span x 56-86 % (center 70.8 %, target 68-72 %), fist tops at 81.2 % of the
  height (target >= 80 %), rod tip at (52.7 %, 14.1 %) (target x 50-60 %, y 12-20 %), reel spool 100 % unoccluded
  between the fists. The right elbow now points further out so the rod butt clears the upper arm (off-screen overlap
  9.5 mm; 23 mm in the previous pose).

### The prone poses in numbers
Measured on the skinned mesh + the SM_Rod_Basic mesh (B-M1 version) on every 3rd frame, with the StanceDip additive
(bottom and overshoot frames) on top. Camera at the eye, arms attached with a zero transform, FirstPersonScale 1.0
unless noted (the game's 0.6 only makes it safer).

| | Prone_HoldRod_Idle | Prone_TuckRod | limit |
|---|---|---|---|
| highest point above the eye | **+11.5 cm** (rod top, frame 33) | **-12.2 cm** (everything below the eye) | +15 cm (60 cm gap, eye 45 cm) |
| gap to a 60 cm ceiling, eye 45 / 35 cm (DT_Movement Prone EyeHeight = 35) | 3.5 / 13.5 cm | 27.2 / 37.2 cm | > 0 |
| furthest forward | 206 cm (rod tip: **not wall-safe**, by design) | **44.5 cm** (right fist, frame 27) | 47 cm (wall at 50 cm, 3 cm for the bob) |
| forward reach when the camera pitches +-45 deg | - | 49.4 cm (29.6 cm at FirstPersonScale 0.6) | 50 cm |
| camera pitch-up before touching a +15 / +25 cm ceiling | 0 / 3 deg (3 / 8 deg at scale 0.6) | 38 / 52 deg | see ArmsPitchFollowUp |
| on screen at frame 0 | fists x 57-86 % (center 71 %), tops 86.6 %; tip (56.4 %, 47.7 %); spool partly visible | fists at the bottom corners (x 11-82 %), tops 81.3 %; rod out of view | |

Crossfades, simulated as Unreal blends them (bone-local translation lerp + shortest-arc rotation, 21 steps):

| Blend | highest point | rod on screen | rod tip path |
|---|---|---|---|
| Prone_HoldRod_Idle <-> Prone_TuckRod | +6.1 cm (the hold's own start) | never left of 58.6 % of the width | down and around the player's RIGHT side, 16-52 cm below the eye |
| HoldRod_Idle <-> Prone_TuckRod | the standing tip at the start (+72.6 cm), falling | never left of 53 % | drops, then around the right side |
| HoldRod_Idle <-> Prone_HoldRod_Idle | the standing tip at the start, falling | never left of 53 % | lowers straight ahead |

Why the tuck keys a 172 deg flip of `hand_r_rod` (not 180): an exact 180 deg rotation has no shortest arc, and in tests
the blend swung the tip either 1.2 m up (through any ceiling) or across the whole view. The 172 deg flip about a
fixed axis in the fist gives the blend one direction: down and around the right side. The rod sits 8 deg off the
fist's channel as a result; that's hidden inside the fist.

`hand_r_rod` in Prone_TuckRod frame 0 (component space): (16.8, 32.7, -22.2) cm, P -2.4 / Y 139.3 / R -15.3 (the rod
points back and out to the right). In Prone_HoldRod_Idle frame 0: (42.9, 21.0, -26.2) cm, P 11.0 / Y 1.8 / R 0.0.

## Unreal import (editor-operator)

Import settings for these files (and every skeletal FBX from `pb.export_skeletal_fbx`): **Convert Scene ON** (axis
conversion, as before), **Convert Scene Unit OFF** (the file is already cm; ON would also be an exact no-op because the
header is exactly 1.0, but OFF makes it explicit), **Force Front X Axis OFF**, **Import Uniform Scale 1.0**, no import
rotation/translation offset, normals imported, no physics asset. In `Content/Python/pipeline_unreal.py` terms:
`_fbx_options(...)` with `convert_scene_unit=False`; `reimport_interchange(path, convert_scene_unit=False, ...)`.

**Re-import of the existing assets (2026-09-23 cm fix)**, in this order, all in `/Game/Art/Characters/FPArms/`
(the source paths are unchanged, `art/export/Characters/<name>.fbx`):
1. `SK_FPArms`: re-import with Convert Scene Unit **OFF** and **Update Skeleton Reference Pose ON**
   (`reimport_interchange("/Game/Art/Characters/FPArms/SK_FPArms", convert_scene_unit=False,
   update_skeleton_ref_pose=True)`). The `SKEL_FPArms` reference pose must change: `root` scale 100 -> 1 and bone
   translations m -> cm. Same 16 bones and hierarchy, so the skeleton, `ABP_FPArms` and sockets stay valid.
2. All five clips, Convert Scene Unit **OFF** (`reimport_interchange("/Game/Art/Characters/FPArms/<A_FPArms_*>",
   convert_scene_unit=False)`): `A_FPArms_Idle`, `A_FPArms_HoldRod_Idle`, `A_FPArms_StanceDip`,
   `A_FPArms_Prone_HoldRod_Idle`, `A_FPArms_Prone_TuckRod`. Their old tracks carry the 100x root, so none may be left
   un-reimported. Afterwards check that `A_FPArms_StanceDip` is still Additive Local Space / Skeleton Reference Pose and
   the loops still loop with root motion off (steps 3-4 below).
3. Verify before handing back (stop and report if any fails):
   - every bone's reference-pose LOCAL scale is 1.0, `root` included
     (`skel.get_reference_pose().get_ref_bone_pose(b, unreal.AnimPoseSpaces.LOCAL).scale3d`);
   - LOCAL translation lengths in cm, not m: `arms` 26.9 cm (was 0.269 under a 100x root), `lowerarm_r` 31.0 cm,
     `hand_r` 27.0 cm;
   - `skeleton_report()` unchanged from the first import: 16 bones, component-space heads `hand_r` (48, 17, -21.5),
     `hand_r_rod` (54.1, 13.8, -24.1), `hand_l_crank` (50.5, 3.8, -30.9) cm, bounds about 80.3 x 51.9 x 18.7 cm;
   - `A_FPArms_HoldRod_Idle` frame 0: `hand_r_rod` at (42.9, 21.0, -23.0) cm in component space;
   - PIE shot from the camera like `Saved/AgentLogs/editor/20260923-t004-import/pie_fp_rod.png`: same framing, and
     SM_Rod_Basic on `hand_r_rod` now at scale 1.0 even with a scale-inheriting attach rule.

First-time import (a fresh project or a new copy), for reference:
1. `SK_FPArms.fbx` -> `/Game/Art/Characters/FPArms/`, Skeletal Mesh, create a new skeleton and rename it
   `SKEL_FPArms`; import animations **off**; settings above. Materials `M_FPArms_Sleeve` (#7C8A63) and
   `M_FPArms_Skin` (#B98563).
2. The five `A_FPArms_*.fbx` -> same folder: animation only, Skeleton = `SKEL_FPArms`, animation length = exported
   time, 30 fps (from the file), settings above. Names stay `A_FPArms_*`.
3. `A_FPArms_StanceDip`: Additive Anim Type = **Local Space**, Base Pose Type = **Skeleton Reference Pose**.
4. All loops (`Idle`, `HoldRod_Idle`, `Prone_HoldRod_Idle`, `Prone_TuckRod`): looping in their players; Enable Root
   Motion off. **Don't strip `hand_r_rod` tracks** (it's non-deforming but animated in the tuck).
5. Skeleton slot: `StanceAdditive` in slot group `Additive` (Anim Slot Manager), as before.
6. Screenshot check: SM_Rod_Basic attached to `hand_r_rod`; preview `A_FPArms_HoldRod_Idle` frame 0 from the camera
   point (rod on the right half, reel below the rod between the fists, fists low right) and `A_FPArms_Prone_TuckRod`
   (rod pointing back beside the right forearm, reel hanging outward). Designer B-M3 in-engine shots follow.

## Wiring (unreal-engineer, C++; ABP stays thin)

- **Components** (unchanged): arms `USkeletalMeshComponent` attached to the FP camera, zero relative transform;
  `SetOnlyOwnerSee(true)`, `FirstPersonPrimitiveType = FirstPerson`, `NoCollision`, no shadow casting; camera
  `bEnableFirstPersonFieldOfView = true`, `FirstPersonFieldOfView = 90`, `bEnableFirstPersonScale = true`,
  `FirstPersonScale = 0.6`. `VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`.
- **Rod**: `RodMesh->AttachToComponent(ArmsMesh, SnapToTargetNotIncludingScale, TEXT("hand_r_rod"))`, zero relative
  transform, FirstPerson + OnlyOwnerSee. Attach to the **bone** (not a skeleton socket with its own offset): the tuck
  moves the rod by animating that bone.
- **AnimInstance** (C++ `UFPArmsAnimInstance`): replace `bHoldingRod` with `EFPArmsPose ArmsPose` (UENUM BlueprintType:
  `Idle = 0, HoldRod = 1, ProneHold = 2, ProneTuck = 3`) and `float ArmsPoseBlendTime`. `ABP_FPArms` graph:
  four Sequence Players (`A_FPArms_Idle`, `A_FPArms_HoldRod_Idle`, `A_FPArms_Prone_HoldRod_Idle`,
  `A_FPArms_Prone_TuckRod`, sync group `FPArmsBreath`) -> `Blend Poses by EFPArmsPose` (or by int), **Transition
  Type: Standard Blend, Blend Type: Linear** (what the clearance was measured with; don't switch to Inertialization
  without re-checking), every Blend Time pin bound to `ArmsPoseBlendTime` -> `Slot DefaultSlot` ->
  `Slot StanceAdditive` -> Output.
- **Switch rule** (owning client, cosmetic; other players never see these arms). New DT_Movement columns per stance
  (row struct `FMovementRow`; defaults below, all "feel" values):

  | Column | Stand | Sprint | Crouch | Prone | Meaning |
  |---|---|---|---|---|---|
  | `RodPoseStill` | HoldRod | HoldRod | HoldRod | ProneHold | pose while holding the rod and still |
  | `RodPoseMoving` | HoldRod | HoldRod | HoldRod | ProneTuck | pose while holding the rod and moving |
  | `RodMoveSpeedIn` | 15 | 15 | 15 | 15 | cm/s: faster than this = moving (switches at once) |
  | `RodMoveSpeedOut` | 5 | 5 | 5 | 5 | cm/s: slower than this (and no move input) counts toward still |
  | `RodStillDelay` | 0.3 | 0.3 | 0.3 | 0.3 | s below `RodMoveSpeedOut` before switching back to the still pose |
  | `RodPoseBlendTime` | 0.3 | 0.3 | 0.3 | 0.3 | s crossfade into this row's poses (stance changes too) |
  | `ArmsPitchFollowUp` | 1.0 | 1.0 | 1.0 | 0.0 | share of the camera's UPWARD pitch the arms follow |

  ```
  Row      = DT_Movement[CurrentStance]
  bInput   = MovementInput.Size2D() > 0.2          // pushing against a wall still counts as crawling
  if (!bMoving && (Speed2D > Row.RodMoveSpeedIn || bInput))  { bMoving = true;  StillTimer = 0; }
  else if (bMoving) {
      StillTimer = (Speed2D < Row.RodMoveSpeedOut && !bInput) ? StillTimer + dt : 0;
      if (StillTimer >= Row.RodStillDelay) bMoving = false;
  }
  ArmsPose          = !bHoldingRod ? Idle : (bMoving ? Row.RodPoseMoving : Row.RodPoseStill);
  ArmsPoseBlendTime = Row.RodPoseBlendTime;
  ```
  Tucking is immediate and untucking waits `RodStillDelay`, so a short pause mid-crawl doesn't flick the rod out.
  While a cast / reel / land montage is active (T-006/T-007), those systems decide: either keep `ProneHold` (movement
  blocked while reeling) or cancel the line when the player crawls; that's a design call for those tasks.
- **ArmsPitchFollowUp** (why): the prone hold's rod tip is 2 m ahead, so it rises into a 60 cm ceiling after 0-3 deg
  of looking up (3-8 deg at FirstPersonScale 0.6). Lying prone, your arms stay on the ground when you lift your head:
  apply `ArmsMesh relative pitch = -(1 - ArmsPitchFollowUp) * max(0, CameraPitch)` (rotation about the eye, i.e. the
  component origin), interpolated with the stance change. Looking down still follows fully, so the rod stays in view
  when watching the water. With 0.0 the prone hold keeps its +11.5 cm at any look-up; the tuck is fine up to 38 deg
  either way.
- **Optional** (cheap, recommended for the T-005 crawl cave): while prone and still, keep `ProneTuck` if a sphere
  trace (radius 5 cm) from the camera along the camera's horizontal forward hits something within
  `RodHoldClearance` cm (new Prone column, e.g. 130 = the hold's reach at FirstPersonScale 0.6); 0 = off. The prone
  hold reaches 2 m forward and would cut into a wall or a bend of the tunnel.
- **StanceDip** (unchanged): on every stance change and on landing,
  `PlaySlotAnimationAsDynamicMontage(StanceDip, "StanceAdditive", 0.0f, 0.05f, PlayRate)` (DT_Movement
  `StanceDipPlayRate`: 1.0, prone 0.85). Checked on top of both prone clips: it only lowers them.
- **Left hand on the crank (later, reeling)**: in HoldRod_Idle and Prone_HoldRod_Idle `hand_l` sits exactly on
  `hand_l_crank`. When a reel animation spins the crank, drive a Two Bone IK on `hand_l` (joint `lowerarm_l`) to the
  `hand_l_crank` transform; disable that IK in `ProneTuck` (the left hand lets go there).
- **Rod bend** under line tension: game-driven on a future skeletal rod (bone chain along the 20 blank rings), not a
  clip.
- **Prone without a rod**: `Idle` is used, and it is not wall-safe (fingertips 68 cm ahead). If an empty-handed prone
  state ever exists, ask for a prone idle.

## Procedural walk bob and sway (C++, tuning in data)

Not a clip. Apply as an offset on the arms mesh's relative transform (the rod rides along). The camera itself stays
steady (optional camera bob, default 0). Per-stance columns in `DT_Movement` (already in lane eng1): `BobStepRate`,
`BobVertical`, `BobLateral`, `BobRoll`, `BobPitch`, `BobYaw`, `BobForward` (Prone row: 0 / 0.4 / 1.6 / 2.0 / 0.3 / 1.5 /
1.2). Global values: `BobBlendSpeed` 8 /s, `BobHoldRodScale` 0.7, `LookSwayPerDegPerSec` 0.02, `LookSwayMaxDeg` 2.5,
`LookSwaySpeed` 10 /s.

Per tick, with v = horizontal speed and MaxSpeed from the current stance row:
```
r      = clamp(v / MaxSpeed, 0, 1)
A      = FInterpTo(A, bOnGround ? r : 0, dt, BobBlendSpeed)      // starts, stops, jumps fade smoothly
f      = BobStepRate * (0.5 + 0.5 * r)                             // steps/s; never 0, so the phase keeps moving while A fades
phase += PI * f * dt                                               // PI per step, 2 PI per stride
s      = 0.5 * (1 - cos(2 * phase))                                // 0..1, once per step
k      = A * ((bHoldingRod && ArmsPose != ProneTuck) ? BobHoldRodScale : 1)   // full crawl sway while tucked
Offset.Z   = -BobVertical * k * s
Offset.Y   =  BobLateral  * k * sin(phase)                         // Unreal Y (right)
Offset.X   = -BobForward  * k * s
Rot.Pitch  = -BobPitch * k * s
Rot.Roll   =  BobRoll  * k * sin(phase)
Rot.Yaw    =  BobYaw   * k * sin(phase)
// look sway: arms lag the view a little
Target     = clamp(-LookRateDegPerSec * LookSwayPerDegPerSec, -LookSwayMaxDeg, LookSwayMaxDeg)   // yaw and pitch
Sway       = FInterpTo(Sway, Target, dt, LookSwaySpeed)
ArmsMesh->SetRelativeLocationAndRotation(Offset, Rot + Sway)       // + the ArmsPitchFollowUp counter-pitch
```
The tuck is the crawl's visual: its clip only breathes, and the prone bob (lateral 1.6 cm, roll 2 deg, yaw 1.5 deg)
sways the two fists with each crawl step. The tuck keeps 5.5 cm to a 50 cm wall, more than `BobForward` 1.2 cm.

## Compromises (known, documented in the recipe)

- **Right wrist, off-screen**: HoldRod_Idle has a strong ulnar bend (79 deg) plus 26 deg flexion; Prone_HoldRod_Idle
  45 deg flexion / 48 deg deviation / -80 deg twist; Prone_TuckRod 42 / 49 / -79. Cause: the placeholder mitten has
  one finger block, so it only closes around a rod within ~30 deg of its knuckle line, and the FP arms float at eye
  height. All below the bottom edge of the 90 deg frame. Split fingers (B-L1) would allow natural grips.
- **Standing hold**: the rod butt touches the underside of the right upper arm (9.5 mm, off-screen). The right
  fingers wrap 18 mm into the reel seat and the left fist 15 mm into the reel body (a closed mitten around the parts).
- **Prone hold**: the left shoulder is protracted 7 cm forward and 6 cm inward to reach the crank (no clavicle).
- **Tuck**: the rod sits 8 deg off the fist's channel (the 172 deg flip); the butt cap touches the thumb by up to
  11 mm at the fist's edge. In first person the rod is out of view (only a sliver of the butt cap shows at the right
  fist), which is intended: a clear view while crawling. The fists read as mittens (B-L1). The rod's lowest point
  (reel) is 34.7 cm below the eye: level with the floor at DT_Movement's 35 cm eye height. The elbows rest 3-6 cm
  below that floor (sleeve surface up to 12 cm): about 64 deg below the view axis, outside the frustum; lifting them
  would push the off-screen wrists to 66-72 deg deviation.
- **Crossfade hold <-> tuck**: for about 0.1 s mid-blend the rod turns in the fist and its tip sweeps up to ~1.9 m out
  to the player's right, low. It crosses a side wall if one is that close; it never crosses the view or rises.
- **Dusk in the gap** is very dark in the previews (low sun in front, rock ceiling): expected, not a pose issue.
- **Polygon order** of `SK_FPArms.fbx` can differ between reruns of the recipe (bmesh extrude ordering in the
  mesh recipe's thumb, `meshkit.extrude_branch`): the same polygons, winding, normals and weights, only listed in a
  different order. No visual or gameplay effect.

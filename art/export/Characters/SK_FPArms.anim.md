# SK_FPArms: rig + animation spec (T-004)

Source of truth: `art/recipes/anim_fp_arms.py` (rig + actions, built on the model-artist's `art/recipes/sk_fp_arms.py`
mesh). Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fp_arms.py`
(deterministic: same rig, same keys). Previews: `Saved/AgentLogs/previews/SK_FPArms_anim.png` (contact sheet) and
`SK_FPArms_anim_fp.png` (90 deg first-person frame with SM_Rod_Basic).

## Files (art/export/Characters/)

| File | Content | Import as |
|---|---|---|
| `SK_FPArms.fbx` | skinned mesh (1456 tris, 2 materials) + 16-bone skeleton, bind pose, **no animation** | Skeletal Mesh `SK_FPArms`, new skeleton renamed **`SKEL_FPArms`** |
| `A_FPArms_Idle.fbx` | armature only, one take `A_FPArms_Idle` | Animation on `SKEL_FPArms` |
| `A_FPArms_HoldRod_Idle.fbx` | armature only, one take `A_FPArms_HoldRod_Idle` | Animation on `SKEL_FPArms` |
| `A_FPArms_StanceDip.fbx` | armature only, one take `A_FPArms_StanceDip` | Animation on `SKEL_FPArms`, then set **additive** (below) |

One clip per file, so the asset name = file name = take name.

## Axes, scale, origin

- Origin = **camera / eye point**. Attach the arms mesh to the first-person camera with a **zero relative
  transform**. No root motion anywhere; the `root` bone never moves.
- Unreal = Blender x 100 cm with Y negated: Blender +X = Unreal +X (forward), Blender +Y (left) = Unreal -Y, Z up.
- Exported with `apply_scale_options=FBX_SCALE_ALL`, `add_leaf_bones=False`, primary bone axis Y / secondary X, NLA
  strips baked at 30 fps, no curve simplification. The Blender armature object is named `Armature`, so Unreal's FBX
  importer drops that node and `root` becomes the skeleton root.
- Blender re-import check (in the recipe, RESULT_JSON `reimport_check`): 16 bones, bone heads and axes 0.0 mm /
  0.0 deg off, mesh bounds identical to the model-artist's static export (x -12.4..67.9 cm, y +-26.0 cm,
  z -34.8..-16.1 cm from the eye), baked poses 0.0 mm off at the probed frames, take ranges 0-90 / 0-90 / 0-8.
- Expected in Unreal: mesh bounds about 80 x 52 x 19 cm; `hand_r` at (48, 17, -21.5) cm in component space.
  **If the skeleton shows an extra `Armature` root bone, or the bounds are 100x off, stop and report it**; don't fix it
  in the editor.

## Skeleton `SKEL_FPArms` (16 bones)

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
| **`hand_r_rod`** | hand_r | no | (54.1, 13.8, -24.1) | **rod attachment**: its frame IS SM_Rod_Basic's pivot frame (X tip, Z up, reel below) |
| **`hand_l_crank`** | hand_r_rod | no | (50.5, 3.8, -30.9) | left-hand IK target: the `hand_l` transform that holds SM_Rod_Basic's crank knob (socket CrankKnob) |

(`_l` values have -Y, `_r` values +Y.) Skin: 12 deform groups, max 2 influences per vertex, weights sum to 1.0,
3-loop blends at elbows and wrists (from the model-artist's groups), plus a forearm twist ramp from the cuff to the
wrist. Checked at extremes (120 deg elbow, 70 deg wrist flexion, 60 deg extension + 90 deg twist, 90 deg pronation
with a fist): no candy-wrapper, no hand collapse.

## Actions (30 fps, root motion: none)

| Asset | Frames | Length | Type | What it is |
|---|---|---|---|---|
| `A_FPArms_Idle` | 0-90 | 3.0 s | loop (frame 90 = frame 0) | empty hands low in view, relaxed curl, one slow breath (shoulders +-5 mm, wrists lag +-9 mm, 2 deg wrist nod), arms offset in phase |
| `A_FPArms_HoldRod_Idle` | 0-90 | 3.0 s | loop (frame 90 = frame 0) | right fist around the rod grip (rod straight ahead, tip up 36 deg, right half of the view), left fist on the crank knob, rod sways +-1.2 deg pitch / +-0.8 deg yaw with the breath; hands solved from the rod, so no sliding |
| `A_FPArms_StanceDip` | 0-8 | 0.267 s | one-shot, **additive** | only the `arms` bone moves: eases in, drops 3.5 cm and pitches 3 deg forward at frame 3 (hands drop about 6-7 cm), overshoots +0.3 cm at frame 6, back at rest on frame 8 |

Notifies: none needed. Informational: StanceDip "bottom" = frame 3 (0.1 s).
Both loops are 3.0 s, so a cross-blend between Idle and HoldRod_Idle stays in phase (optional sync group `FPArmsBreath`).
Motion check (RESULT_JSON `motion_check`): loops have max per-frame step 0.8 mm and first/last delta 0.0 mm (no pop at the
seam); StanceDip starts and ends exactly at rest.

## Unreal import (editor-operator)

1. `SK_FPArms.fbx` -> `/Game/Art/Characters/FPArms/`: Skeletal Mesh, create a new skeleton and rename it
   `SKEL_FPArms`; import animations **off**; no physics asset needed; Force Front X Axis **off**; uniform scale 1.0;
   normals imported. Materials `M_FPArms_Sleeve` (#7C8A63) and `M_FPArms_Skin` (#B98563).
2. The three `A_*.fbx` -> same folder: import as animation only, Skeleton = `SKEL_FPArms`, animation length = exported
   time, 30 fps (from the file). Names stay `A_FPArms_*`.
3. `A_FPArms_StanceDip`: Additive Anim Type = **Local Space**, Base Pose Type = **Skeleton Reference Pose** (frame 0
   equals the reference pose; only `arms` differs).
4. `A_FPArms_Idle`, `A_FPArms_HoldRod_Idle`: looping in their players; Enable Root Motion off.
5. Skeleton slot: add slot `StanceAdditive` in a new slot group `Additive` (Anim Slot Manager), so a dip never
   interrupts a later cast/reel montage in `DefaultSlot`.
6. Screenshot to confirm the orientation rule: attach SM_Rod_Basic to `hand_r_rod` (below), preview
   `A_FPArms_HoldRod_Idle` at frame 0 from the camera point: rod on the right half, tip up, reel hanging below the rod,
   left fist at the reel.

## Wiring (unreal-engineer, C++; ABP stays thin)

- **Components**: arms `USkeletalMeshComponent` attached to the FP camera, zero relative transform;
  `SetOnlyOwnerSee(true)`, `FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson`, collision
  `NoCollision`, no shadow casting. Camera: `bEnableFirstPersonFieldOfView = true`, **`FirstPersonFieldOfView = 90`**
  (the arms were composed for 90 deg horizontal; this keeps them identical when the player changes the world FOV),
  `bEnableFirstPersonScale = true`, `FirstPersonScale = 0.6` (Epic's FP template value; scaling toward the eye keeps the
  image and stops clipping into walls). The same APIs are used in `Templates/TP_FirstPerson` of UE 5.8.
- **Rod**: `RodMesh->AttachToComponent(ArmsMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale,
  TEXT("hand_r_rod"))`, zero relative transform, also FirstPerson + OnlyOwnerSee. Its sockets (LineTip, ReelLine,
  CrankKnob) come along. Expected in HoldRod_Idle frame 0, arms component space: `hand_r_rod` at
  **(42.9, 16.0, -20.3) cm, rotation P 35.1 / Y -0.3 / R 0.0** (reel straight down). In the bind pose the rod would point
  left-up out of the open hand; that's expected, only the HoldRod pose closes the fist.
- **AnimInstance** (C++ `UFPArmsAnimInstance`, exposes `bHoldingRod`); `ABP_FPArms` child graph only:
  `Sequence Player A_FPArms_Idle` + `Sequence Player A_FPArms_HoldRod_Idle` -> `Blend Poses by bool (bHoldingRod,
  0.2 s)` -> `Slot DefaultSlot` -> `Slot StanceAdditive` -> Output. Set the arms mesh to always tick its pose
  (`VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`).
- **StanceDip**: on every stance change (stand/crouch/prone) and on landing:
  `PlaySlotAnimationAsDynamicMontage(StanceDip, "StanceAdditive", 0.0f, 0.05f, PlayRate)`; PlayRate 1.0 for crouch,
  0.85 for prone (heavier), 1.0 on landing. It's additive, so it works with or without the rod. The camera-height
  change itself stays in the character's C++ (T-004).
- **Left hand on the crank (later, reeling)**: in both clips `hand_l` already sits exactly on `hand_l_crank`. When a reel
  animation spins the crank, drive a Two Bone IK on `hand_l` (joint `lowerarm_l`) to the `hand_l_crank` transform
  (take rotation from the effector). Needs a skeletal rod with a crank bone; SM_Rod_Basic is static.
- **Rod bend** under line tension: game-driven on a future skeletal rod (bone chain along the 20 blank rings), not a clip.

## Procedural walk bob and sway (C++, tuning in data)

Not a clip. Apply as an offset on the arms mesh's relative transform (the rod rides along). The camera itself stays
steady (optional camera bob, default 0). Proposed per-stance columns for `DT_Movement` (next to `MaxSpeed`, cm/s):

| Column | Meaning |
|---|---|
| `BobStepRate` | steps per second at MaxSpeed. 0 = derive: `clamp(1.2 + 0.0025 * MaxSpeed, 1.0, 3.0)` |
| `BobVertical` | cm, dip per footstep |
| `BobLateral` | cm, side sway per stride (left+right step) |
| `BobRoll` | deg, roll per stride |
| `BobPitch` | deg, nod down per footstep |
| `BobYaw` | deg, yaw per stride (prone crawl) |
| `BobForward` | cm, push per footstep (prone crawl) |

Defaults (the MaxSpeeds here are examples; the step rate comes from the formula, use the real row values):

| Stance row | MaxSpeed (example) | BobStepRate | Vertical | Lateral | Roll | Pitch | Yaw | Forward |
|---|---|---|---|---|---|---|---|---|
| Walk (stand) | 350 | 2.1 | 0.8 | 0.6 | 0.6 | 0.4 | 0 | 0 |
| Run | 600 | 2.7 | 1.6 | 1.0 | 1.2 | 0.9 | 0 | 0.3 |
| Crouch | 200 | 1.7 | 0.5 | 0.9 | 0.9 | 0.3 | 0 | 0 |
| Prone | 100 | 1.45 | 0.4 | 1.6 | 2.0 | 0.3 | 1.5 | 1.2 |

Global values (one DataAsset or a row): `BobBlendSpeed` 8 /s, `BobHoldRodScale` 0.7 (steadier while holding the rod),
`LookSwayPerDegPerSec` 0.02, `LookSwayMaxDeg` 2.5, `LookSwaySpeed` 10 /s.

Per tick, with v = horizontal speed and MaxSpeed from the current stance row:
```
r      = clamp(v / MaxSpeed, 0, 1)
A      = FInterpTo(A, bOnGround ? r : 0, dt, BobBlendSpeed)      // starts, stops, jumps fade smoothly
f      = BobStepRate * (0.5 + 0.5 * r)                             // steps/s; never 0, so the phase keeps moving while A fades
phase += PI * f * dt                                               // PI per step, 2 PI per stride
s      = 0.5 * (1 - cos(2 * phase))                                // 0..1, once per step
k      = A * (bHoldingRod ? BobHoldRodScale : 1)
Offset.Z   = -BobVertical * k * s
Offset.Y   =  BobLateral  * k * sin(phase)                         // Unreal Y (right)
Offset.X   = -BobForward  * k * s
Rot.Pitch  = -BobPitch * k * s
Rot.Roll   =  BobRoll  * k * sin(phase)
Rot.Yaw    =  BobYaw   * k * sin(phase)
// look sway: arms lag the view a little
Target     = clamp(-LookRateDegPerSec * LookSwayPerDegPerSec, -LookSwayMaxDeg, LookSwayMaxDeg)   // yaw and pitch
Sway       = FInterpTo(Sway, Target, dt, LookSwaySpeed)
ArmsMesh->SetRelativeLocationAndRotation(Offset, Rot + Sway)
```
Changing a stance's MaxSpeed in data rescales the bob automatically (r and the derived step rate). "Feel" changes are
data edits.

## Compromises (known, documented in the recipe)

- **Right wrist in HoldRod_Idle**: a strong ulnar bend (~85 deg; no flexion, forearm twist -8 deg), and the rod butt
  rests along the underside of the right forearm (~3 cm overlap). Both are below the bottom edge of the 90 deg FP
  frame (the butt end is about 66 deg below the view axis). Cause: the placeholder mitten has one finger block, so
  it only closes around a rod within ~30 deg of its knuckle line, and FP arms float at eye height, so the elbow hangs
  below the fist. A 60 deg diagonal grip gave a neutral wrist but left the fingers curled beside the rod, and the fist
  matters more in FP. Fixes if it ever shows (e.g. in cast/reel clips): split fingers in the mesh (model-artist), or a
  shorter rear grip on the rod (0.32 m behind the hand now).
- Left shoulder is protracted 7 cm forward / 6 cm inward in HoldRod_Idle so the left hand reaches the crank (no
  clavicle; the shoulder is out of view).
- The mitten fingers are one bone per hand, so a rod grip is a single curl, not per-finger wrapping.

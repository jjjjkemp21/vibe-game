# SK_FPArms: rig + animation spec (T-004; T-028 rod aim, T-030 hold fish / carry cooler)

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
T-028 / T-030 (2026-09-23): `SK_FPArms_rodaim_fp.png` (the 9 rod-aim poses from the FP camera, laid out as the aim
offset grid), `SK_FPArms_rodaim_upright_fp.png` (RodAim_UpRight full size), `SK_FPArms_rodaim_views.png` (simulated
aim-offset blends in FP + side / top / outside views), `SK_FPArms_holdfish_fp.png` + `SK_FPArms_holdfish.png`
(HoldFish_Idle with the reference-size Bonefish on `hand_r_fish`; the sheet also shows 0.8x / 1.3x / 1.6x fish on the
size blend), `SK_FPArms_holdfish_large_fp.png` (HoldFish_Large_Idle, a 1.6x trophy), `SK_FPArms_carrycooler_fp.png` +
`SK_FPArms_carrycooler.png` (CarryCooler_Idle with SM_Cooler_Starter from its FBX on bone `cooler`).
`SK_FPArms_cm_reimport.png` (2026-09-23 unit check: the old meter files, left, and the new cm files, right,
re-imported and posed by HoldRod_Idle / Prone_TuckRod frame 0, eye and side views; identical, max 0.0004 mm).

## Files (art/export/Characters/)

| File | Content | Import as |
|---|---|---|
| `SK_FPArms.fbx` | skinned mesh (1456 tris, 2 materials) + 18-bone skeleton, bind pose, **no animation** | Skeletal Mesh `SK_FPArms`, skeleton **`SKEL_FPArms`** |
| `A_FPArms_Idle.fbx` | armature only, one take `A_FPArms_Idle` | Animation on `SKEL_FPArms` |
| `A_FPArms_HoldRod_Idle.fbx` | armature only, one take | Animation on `SKEL_FPArms` |
| `A_FPArms_StanceDip.fbx` | armature only, one take | Animation on `SKEL_FPArms`, set **additive** (below) |
| `A_FPArms_Prone_HoldRod_Idle.fbx` | armature only, one take | Animation on `SKEL_FPArms` |
| `A_FPArms_Prone_TuckRod.fbx` | armature only, one take | Animation on `SKEL_FPArms` |
| `A_FPArms_HoldFish_Idle.fbx` | armature only, one take (T-030) | Animation on `SKEL_FPArms` |
| `A_FPArms_HoldFish_Large_Idle.fbx` | armature only, one take (T-030, trophy-size fish) | Animation on `SKEL_FPArms` |
| `A_FPArms_CarryCooler_Idle.fbx` | armature only, one take (T-030) | Animation on `SKEL_FPArms` |
| `A_FPArms_RodAim_Center.fbx`, `_Up`, `_Down`, `_Left`, `_Right`, `_UpLeft`, `_UpRight`, `_DownLeft`, `_DownRight` | armature only, one take each, a single pose (T-028) | Animation on `SKEL_FPArms`, set **additive, Mesh Space** (below) |

One clip per file, so the asset name = file name = take name.

**2026-09-23: all six files re-exported in CENTIMETERS** (fix for the 100x `root` bone scale found at import). Same
meshes, bones, rest pose, rolls, `hand_r_rod`/`hand_l_crank` frames, clips, names and timings; only the unit changed.
All six must be re-imported (steps under "Unreal import").

**2026-09-23 (T-028 / T-030): two new non-deforming bones, `hand_r_fish` and `cooler`**, appended after `hand_l_crank`
(18 bones; the 16 existing bones keep their order, names, rest pose and every key). `SK_FPArms.fbx` must be
re-imported so `SKEL_FPArms` gets them; the 12 new clips are imported new. The five existing clip files were re-exported
with the two extra bone tracks (identical keys otherwise: re-import optional; without it the new bones just sit at their
reference pose, which is all those clips need).

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
- Blender re-import check (RESULT_JSON `reimport_check`; 2026-09-23: 18 bones, all 16 clips, same results): 16 bones, bone heads and axes 0.0 mm / 0.0 deg off (the
  importer puts its cm -> m factor 0.01 on the armature object; bone heads in the armature are the source x100 with
  0.0 mm error; rest and posed bone scales 1.0 within 1.1e-6), mesh bounds identical, baked poses of all 5 clips
  0.0 mm / 0.0 deg off at frames 0/30/45/60/90, take ranges 0-90 / 0-90 / 0-8 / 0-90 / 0-90.
- Expected in Unreal: mesh bounds about 80 x 52 x 19 cm; `hand_r` at (48, 17, -21.5) cm in component space; the
  `root` bone's local scale 1.0. **If the skeleton shows an extra `Armature` root bone, any bone has a scale other than
  1.0, or the bounds are 100x off, stop and report it**; don't fix it in the editor.

## Skeleton `SKEL_FPArms` (18 bones: the T-004 16 + `hand_r_fish`, `cooler`)

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
| **`hand_r_fish`** | hand_r | no | (51.2, 15.7, -32.7) | **fish-in-hand attach** (T-030): its frame is the held fish's frame at the fish's fishkit bone `Grip` (X = fish forward/head, Z = fish up/dorsal), placed so the fish's throat lies in the right palm. Keyed in every clip, but it only moves with `hand_r` |
| **`cooler`** | arms | no | (44.8, 0, -47.2) | **carried-cooler attach** (T-030): SM_Cooler_Starter's pivot (bottom center) with the cooler's own axes (its front faces the player). Animated only in `A_FPArms_CarryCooler_Idle` (rides the breath); its reference pose = that clip's frame 0 |

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
| `A_FPArms_HoldFish_Idle` | 0-90 | 3.0 s | loop | T-030, **rod stowed, two hands**: the fish side-on in front below the horizon, head right; the right hand wraps the throat under the gills, the left palm supports the belly ahead of the anal fin (lower left); one slow breath (fish sways +-1.2 deg pitch / +-0.8 deg yaw). Authored for fish scale 1.0 |
| `A_FPArms_HoldFish_Large_Idle` | 0-90 | 3.0 s | loop | the same hold for a 1.6x trophy: fish turned tail-away and head-up, hands on the big fish's throat and belly; blended with HoldFish_Idle by size |
| `A_FPArms_CarryCooler_Idle` | 0-90 | 3.0 s | loop | T-030, **no rod, two-handed low carry**: both fists on the cooler's rope handles, the cooler low in front with its front wall and lid edge towards the eye (lower third); box and hands breathe together (+-0.7 cm, +-0.6 deg), so no grip sliding |
| `A_FPArms_RodAim_*` (9) | 0-1 | 1 pose | aim-offset pose, **additive Mesh Space** | T-028: `Center` = HoldRod_Idle frame 0 exactly; `Up` pulled back and high, `Down` dipped towards the water, `Left` / `Right` tip swung to that side, plus the 4 corners. Two identical keys; Unreal samples frame 0 |

Notifies: none needed. All seven loops are 3.0 s: put their players in one sync group `FPArmsBreath` so crossfades
stay in phase. Motion check (RESULT_JSON `motion_check`): loops have max per-frame step <= 0.8 mm (rod tip <= 2.7 mm)
and first/last delta 0.0 mm (no pop at the seam); StanceDip starts and ends exactly at rest. The Blender re-import
check reproduces every clip's baked pose (all 17 clip files, `hand_r_fish` and `cooler` included) with 0.0 mm / 0.0 deg
error.

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

### HoldFish (T-030; two hands after the designer review 2026-09-23)
A short reward pose (an exception to the first-person held-item rule): the fish is held side-on in front, below the
horizon (only the dorsal fin tip may reach it). Rod stowed (hide the rod mesh; the clip keeps `hand_r_rod` in the fist).
- **Grip points** (on the reference Bonefish, fish space from its `Grip` bone, cm, Unreal axes):
  - right hand: **under the gills**, contact (7.6, 2.9, -4.2): the throat lies across the right palm along the fist
    channel, fingers wrapped up the far flank, thumb over the near gill cover;
  - left hand: **under the belly ahead of the anal fin**, contact (-15.0, 1.7, -2.5), palm open (the fish's weight).
  Both contacts are turned 35 deg about the spine towards the fish's near (right) flank, so the palms cup the
  near-lower belly and show in front of the fish.
- **Attach**: the fish mesh (`SK_<Species>`, SKEL_Fish) attaches to bone **`hand_r_fish`** (`SnapToTargetNotIncludingScale`,
  relative rotation 0) with uniform scale S = `(Weight / ReferenceWeight)^(1/3)` and
  **relative location = -S x GripCS + (1 - S) x (7.6, 2.9, -4.2) cm**, where `GripCS` is the fish's `Grip` bone in its
  own component space at scale 1 (`GetBoneLocation("Grip", EBoneSpaces::ComponentSpace)`, or the fish spec's value:
  Bonefish (9.43, 0, 0), CoralSnapper (9.66, 0, 0)). That keeps the throat on the right palm at any size (the fish
  grows about that point, along its belly line towards the left palm). At S = 1 it is just `-GripCS`.
- **Two clips, blended by size** (so a trophy doesn't fill the screen): `A_FPArms_HoldFish_Idle` is authored for S = 1.0,
  `A_FPArms_HoldFish_Large_Idle` for S = 1.6 (the fish turned tail-away 45 deg more and 16 deg more head-up, the arms
  further round). Blend them with **`HoldFishSizeAlpha = clamp((S - 1.0) / (1.6 - 1.0), 0, 1)`** (both in the
  `FPArmsBreath` sync group). Checked with the Bonefish, crossfaded as Unreal does:

  | S | blend | fish width on screen | fish top (y %) | left palm off its belly contact |
  |---|---|---|---|---|
  | 0.8 | 0 | 47 % (x 27-74) | 64 | 4.5 cm along the belly (still under it) |
  | 1.0 | 0 | 59 % (x 16-75) | 58.5 | 0 |
  | 1.3 | 0.5 | 65 % (x 17-83) | 54.5 | 6.5 cm (under the belly, a little deeper) |
  | 1.6 | 1 | 66 % (x 27-93) | 51 | 0 |

  So every size stays under 70 % of the width. The right palm stays on the throat at every size (exact).
- Frame 0 (S = 1, arms component space): `hand_r_fish` (46.9, 8.0, -16.8) cm, P 5.1 / Y 79.7 / R -15.0 (head to the
  right and a little forward). On screen: the fish at x 16-75 %, top 58.5 % (lowered about 10 % after the review); the
  right hand under the head at x 60-78 %, the left hand under the rear belly at x 31-50 %, tops at 77 % and 89 %.
- The fish must be a **first-person primitive** like the rod (`FirstPersonPrimitiveType = FirstPerson`,
  `SetOnlyOwnerSee(true)`, no collision, no shadow), or FirstPersonScale 0.6 puts the arms and the fish at different depths.
- Contact: up to 18 fish vertices sit up to 1.4 cm inside the palms and fingers (the grip, hidden by the fish); bigger
  fish sink up to 2.4 cm (CoralSnapper's deeper belly the same). Pectoral and anal fin tips poke through the fingers
  by a few mm.

### CarryCooler (T-030; two-handed low carry, lead decision after the designer review 2026-09-23)
- **Handles**: SM_Cooler_Starter's sockets `Handle_L` (0, -31.25, 19.1) cm and `Handle_R` (0, 31.25, 19.1) cm in the
  cooler's space (checked against the current `art/export/Props/SM_Cooler_Starter.fbx`: 0.015 mm). Each fist's grip
  channel is centered **exactly on its handle socket** (0.015 mm): **hands 62.5 cm apart, 19.1 cm above the cooler's
  base, the rope running front-back through each fist**. The cooler is carried with **its front (latch, sticker)
  facing the player**, so the right fist holds `Handle_L` and the left fist `Handle_R`. Wrist bones in cooler space
  (Unreal axes, cm): `hand_r` (3.1, -30.4, 25.7), `hand_l` (3.1, 30.4, 25.7).
- **Attach**: the carried cooler's mesh attaches to bone **`cooler`** with a zero relative transform (the bone is the
  cooler's pivot, bottom center, with the cooler's own axes: its +X points at the player). Frame 0: (44.8, 0, -47.2) cm,
  P 9.5 / Y 180 in arms component space: the box low in front, handles 28 cm below the eye and 48 cm ahead, tilted
  10 deg top-away so the front wall and the lid edge face the eye. It rides the breath (+-0.2 / +-0.7 cm, +-0.6 deg) and
  the hands move with it (no sliding). If the model's handle sockets move, re-run the recipe (`COOLER_HANDLE_L`; the
  RESULT_JSON check `socket_handle_L_vs_recipe_mm` flags a mismatch).
- On screen: the front wall (latch strap, stripe) and the lid edge fill the bottom third (lid edge at 65 % of the height,
  x 8-92 %); the center and the upper view stay clear.
- **Compromise (needs a model change to fix): the fists are not visible.** With the handles at mid height (19 cm) on the
  short sides, the cooler's front wall hides them from the eye in every pose that keeps the box in the lower third
  (checked over carry distance, height and tilt: to see the fists over the box, its top would reach 53 % of the
  screen). Only the forearms show at the bottom corners. **Fix: handles near the rim** (socket height 26 cm or more,
  ideally 30-33 cm; the body rim is at 33 cm): then both fists show at the lower corners (about 10 % of the width each)
  with the lid in the lower third. The model is being revised now; when the new sockets land, the recipe needs new
  carry numbers (`COOLER_HANDLES_POS` about (42, 0, -24.5) cm, `COOLER_TILT_DEG` about -10 for 30 cm handles).
- While carried: turn off the cooler's collision with its carrier (the box is 26-70 cm in front of the eye), and make
  it a first-person primitive for the owner (same FirstPersonScale reason as the fish). Other players see the
  replicated world cooler; a third-person carry needs a third-person body (later).
- Contact: each wrist heel and thumb touch the cooler's end wall by up to 1.4 cm (the rope is 2.4 cm off the wall;
  hidden). Wrists 9 deg flex / 73 deg ulnar bend (the mitten grip, as on the rod; hidden).

### Rod aim offset (T-028; framing revised after the designer review 2026-09-23)
Nine single-pose clips, one per point of a 3x3 aim-offset grid. Each extreme moves the grip, turns the rod so its tip
lands on a chosen point of the first-person frame, and turns the upper body (`arms` bone) partly with it; both hands are
solved from the rod as in HoldRod_Idle, so every pose has the right fist on the grip and the left fist on the crank knob.
Screen numbers for 1920x1080 at 90 deg; tip = SM_Rod_Basic's line tip.

| Pose | Grip move (cm: fwd, left, up) | Rod pitch / yaw / roll (deg, yaw + = left) | Body yaw (deg) | Tip (x, y %) | Fists x % / top y % | Reel visible | `hand_r_rod` (component, cm) |
|---|---|---|---|---|---|---|---|
| Center | 0 | 35.1 / 4.8 / 0 (= HoldRod_Idle f0) | 0 | 52.7, 14.1 | 56-86 / 81 | 100 % | (42.9, 21.0, -23.0) |
| Up | -12, +2, +6 | 37.3 / -0.2 / 0 | 0 | 56, 4.5 | 56-98 / 77 | 97 % | (30.9, 19.0, -17.0) |
| Down | +7, 0, +2 | -2.7 / 7.3 / 15 | 0 | 50, 62 | 55-80 / 71 | 40 % | (49.9, 21.0, -21.0) |
| Left | +2, +6, 0 | 26.9 / 43.0 / 0 | 40 | 22, 20 | 47-77 / 80 | 90 % | (44.9, 15.0, -23.0) |
| Right | 0, +7, -2 | 14.6 / -49.4 / -35 | -20 | 96, 40 | 57-81 / 85 | 84 % | (42.9, 14.0, -25.0) |
| UpLeft | -8, +5, +7 | 31.9 / 38.9 / 0 | 40 | 25, 6 | 47-88 / 71 | 90 % | (34.9, 16.0, -16.0) |
| UpRight | -10, +9, +5 | 28.6 / -44.8 / -35 | -20 | 92, 10 | 55-87 / 77 | 60 % | (32.9, 12.0, -18.0) |
| DownLeft | +3, +6, +2 | 3.3 / 39.6 / 0 | 40 | 24, 56 | 48-75 / 74 | 77 % | (45.9, 15.0, -21.0) |
| DownRight | 0, +7, +1 | 0.5 / -48.4 / -35 | -20 | 95, 62 | 58-80 / 78 | 67 % | (42.9, 14.0, -22.0) |

- Review fixes: the Down row's grips are 5 cm higher (fist tops 71-78 % instead of 80-87 %: a full fist in frame); the
  Right column's grips are 9 cm further left (fists 4-7 % more central); the rod is rolled in the Right column (-35 deg)
  and in Down (+15 deg) so the reel swings out from behind the right fist (reel 40-84 % visible, was 0-17 %).
- **Up** is 2.2 deg steeper than Center, not the 5 deg the review asked for: with the tip kept inside the frame and the
  fists visible, about 39-40 deg is the geometric limit for a 1.65 m rod held 30-40 cm from the eye (the Center tip is
  already 14 % from the top). Up reads through the fists pulled 12 cm back and 6 cm up; the game-driven rod bend under
  tension (later) adds to it.
- The corners give the aim offset full strength diagonally (with only 5 samples, input (1, 1) would blend 50 % Up +
  50 % Right). "Fish runs left: rod up and to the right" is exactly `UpRight`.
- The body turn depends only on the yaw input (Left 40 deg, Right 20 deg), so the blend of the `arms` bone is separable
  and monotonic. The rod tip stays inside the frame over the whole input square (9x9 grid simulated on HoldRod_Idle
  frames 0/22/45/67: 0 of 324 samples off-screen).
- Blends (Unreal's mesh-space aim offset, simulated): between grid points the left fist drifts off the crank knob by up
  to **5.7 cm** (worst at yaw +0.5, pitch 0; 0 at the 9 poses). **Keep the Two Bone IK in the wiring below**: the
  crank stays within 99.7 % of the left arm's reach everywhere, so the IK always lands. The right fist never drifts
  (the rod is its child).
- Off-screen compromise: the right wrist bends hard in the extremes (the mitten grip's 79 deg ulnar bend plus up to
  ~100 deg of flex). All of it is below or beside the frame; the outside views in `SK_FPArms_rodaim_views.png` show it
  bent, not broken. The rod butt touches the right forearm by up to 5 cm in Right (off-screen).
- **Standing / crouched only.** On `Prone_HoldRod_Idle` the same deltas don't fit (the left fist drifts up to 20 cm,
  `Up` lifts the rod to +21 cm above the eye where a 60 cm crawl gap allows +15, the tip leaves the frame at the
  right/down corners). Apply the aim offset only on the `HoldRod` branch (below); a prone fight keeps ProneHold still
  (prone aim poses would be a follow-up).

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

**T-028 / T-030 import (2026-09-23)**, all in `/Game/Art/Characters/FPArms/`, settings above:
1. Re-import `SK_FPArms` (Convert Scene Unit OFF, **Update Skeleton Reference Pose ON**) so `SKEL_FPArms` gains
   `hand_r_fish` (parent `hand_r`) and `cooler` (parent `arms`): 18 bones. The 16 existing bones keep their order and
   reference pose, so `ABP_FPArms` and the existing clips stay valid (re-importing the five old clips is optional:
   identical keys plus the two new bones at rest).
2. Import the three new loops `A_FPArms_HoldFish_Idle`, `A_FPArms_HoldFish_Large_Idle`, `A_FPArms_CarryCooler_Idle`:
   animation only on `SKEL_FPArms`, 30 fps, frames 0-90, Enable Root Motion off, looping in their players.
   (2026-09-23 review revision: re-import `A_FPArms_HoldFish_Idle`, `A_FPArms_CarryCooler_Idle` and all nine
   `A_FPArms_RodAim_*` if they were already imported, and re-import `SK_FPArms` so the `hand_r_fish` / `cooler`
   reference poses update.)
3. Import the nine `A_FPArms_RodAim_*` (Center, Up, Down, Left, Right, UpLeft, UpRight, DownLeft, DownRight): animation
   only on `SKEL_FPArms`, frames 0-1 (one pose). On each: **Additive Anim Type = Mesh Space**, **Base Pose Type =
   Selected animation frame**, Ref Pose Seq = `A_FPArms_RodAim_Center`, Ref Frame Index = 0 (Center is HoldRod_Idle
   frame 0, so Center's own delta is zero).
4. Create the aim offset `AO_FPArms_RodAim` (Aim Offset asset, Skeleton `SKEL_FPArms`, preview base pose
   `A_FPArms_HoldRod_Idle`): horizontal axis **`RodAimYaw`, -1..1**, vertical axis **`RodAimPitch`, -1..1** (grid
   divisions 2 on both; no smoothing needed, T-028 smooths the inputs). Samples: Center (0, 0), Up (0, 1), Down (0, -1),
   Left (-1, 0), Right (1, 0), UpLeft (-1, 1), UpRight (1, 1), DownLeft (-1, -1), DownRight (1, -1).
   Sign convention: **yaw +1 = rod tip to the player's RIGHT** (Unreal's positive yaw), **pitch +1 = Up** (pulled back
   and high = more tension), -1 = dipped towards the water.
5. Verify (stop and report if any fails): `skeleton_report()` shows 18 bones with `hand_r_fish` head at
   (51.2, 15.7, -32.7) cm and `cooler` at (44.8, 0, -47.2) cm (component space); `A_FPArms_RodAim_Up` frame 0 has
   `hand_r_rod` at (30.9, 19.0, -17.0) cm; the aim offset preview at (1, 1) matches `SK_FPArms_rodaim_upright_fp.png`
   (rod up and on the right, tip near the top-right corner, both fists on the rod).
6. Screenshot checks: SM_Rod_Basic on `hand_r_rod` with the aim offset at the 4 edge points; a Bonefish on
   `hand_r_fish` (relative location (-9.43, 0, 0) cm at scale 1) with HoldFish_Idle, like `SK_FPArms_holdfish_fp.png`;
   SM_Cooler_Starter on `cooler` (zero transform) with CarryCooler_Idle: both fists on the rope handles.

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
- **Left hand on the crank (later, reeling)**: in HoldRod_Idle, Prone_HoldRod_Idle and all nine RodAim poses `hand_l`
  sits exactly on `hand_l_crank`. When a reel animation spins the crank, the same Two Bone IK (below, T-028) keeps the
  fist on it; it sits only on the HoldRod branch, so `ProneTuck` (the left hand lets go there) is unaffected.
- **T-030 poses** (`EFPArmsPose` gets `HoldFish` and `CarryCooler`, added by the engineers): two more Sequence
  Players into `Blend Poses by EFPArmsPose`, same sync group `FPArmsBreath`, same Standard / Linear blend and
  `ArmsPoseBlendTime`: `HoldFish` -> **Blend (alpha `HoldFishSizeAlpha`)** of `A_FPArms_HoldFish_Idle` (A) and
  `A_FPArms_HoldFish_Large_Idle` (B), `CarryCooler` -> `A_FPArms_CarryCooler_Idle`. `UFPArmsAnimInstance` exposes
  `float HoldFishSizeAlpha = clamp((FishScale - 1.0) / 0.6, 0, 1)` (the two scales are tuning values, data). Rule:
  `CarryCooler` while carrying a cooler (wins over everything; no rod, no fish), else `HoldFish` while a fish is in hand
  (rod hidden), else the rod/idle rule above. Suggested blend time 0.25 s (data). Attach the fish to `hand_r_fish` and
  the cooler to `cooler` as described in the HoldFish / CarryCooler sections; both first-person primitives for the
  owner. The rod mesh is hidden (not detached) in both poses. StanceDip still plays on top (it moves the fish and the
  cooler with the arms).
- **T-028 rod aim offset**: `UFPArmsAnimInstance` exposes `float RodAimYaw, RodAimPitch` (-1..1, smoothed by T-028).
  Graph: `A_FPArms_HoldRod_Idle` player -> **`AimOffset Player AO_FPArms_RodAim`** (Yaw pin `RodAimYaw`, Pitch pin
  `RodAimPitch`, Alpha 1) -> **Two Bone IK** (IKBone `hand_l`, joint target none (keep the pose's elbow), Effector
  Location Space = **Bone Space**, Effector Target `hand_l_crank`, **Take Rotation from Effector Space ON**, Allow
  Stretching off) -> the `HoldRod` pin of `Blend Poses by EFPArmsPose`. Only the HoldRod branch gets the aim offset:
  prone doesn't fit (see "Rod aim offset"), and the other poses have no rod. The IK closes the up-to-4 cm drift of the
  left fist off the crank between grid points; at the 9 poses it is a no-op. Zero inputs = HoldRod_Idle exactly.
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

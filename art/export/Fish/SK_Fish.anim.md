# SK_Fish: the shared fish skeleton + clips (T-008; S3 update: T-058, T-059, T-060)

Source of truth: `art/recipes/anim_fish.py` (build, checks, exports, previews) on `art/lib/fishrig.py` (skeleton, skin,
every motion as a function of the frame). Meshes: the model-artist's `sm_fish_bonefish.py` / `sm_fish_coralsnapper.py`
(fishkit, 136eb12), not edited. Rerun:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fish.py`.
It is deterministic: two runs give structurally identical FBX files (every value equal; only the FBX object UIDs and
time stamps change, so a rerun without a content change rewrites the binaries: don't commit that churn).
All numbers below come from `Saved/AgentLogs/blender/anim_fish.result.json`.

Previews (`Saved/AgentLogs/previews/`): `SK_Fish_anim.png` (both species x 10 clips, 3/4 view at each clip's tightest
bend), `A_Fish_<Clip>_anim.png` (8-13 key frames x Bonefish top/side, CoralSnapper top/side at amplitude 0.8;
`A_Fish_Hooked_Hang_anim.png` adds the hanging rows: nose up, its right side, its back, with the hook and line),
`SK_Fish_seams.png` (every loop across its seam: frames N-2, N-1, N = 0, 1, 2),
`SK_Fish_strobe.png` (midlines per clip + the ambient WPO wave), `SK_Fish_flop_dock.png` (Landed_Flop lying on dock
planks), `SK_Fish_tuck.png`, `SK_Fish_pec.png` (pectoral close-ups), `SK_Fish_weights.png`,
`A_Fish_Curled_anim.png` (the cooler pose, both species) and `A_Fish_Curled_cooler*.png` (4 fish in the starter cooler:
top view, first person with the cooler on the floor (the design view) and on a counter, 1-4 fish fills, the slot
diagram, the 1.3x fish unclamped; recipe
`art/recipes/anim_fish_cooler.py`, section "Cooler display" below).
The clips as the player sees them (S3, `art/recipes/anim_fish_views.py`, preview only, RESULT_JSON
`Saved/AgentLogs/blender/anim_fish_views.result.json`): `SK_Fish_dock_<Clip>.png` (the fight clips and Swim_Tired from
the dock at 5 m and 12 m, game pixels, at the S3 play rates), `SK_Fish_tired_vs_run.png`,
`SK_Fish_exhausted_before_after.png`, `A_Fish_Hooked_Hang_fp.png` (+ `_fp_day_full.png`, `_fp_dusk_full.png`: first
person, hanging from the rod) and `A_Fish_Hooked_Hang_strobe.png` (the Mouth stays on the hook). Section "Seen from
the player's camera (S3)" below.

## Files (art/export/Fish/)

| File | Content | Import as |
|---|---|---|
| `SK_Bonefish.fbx` | skinned mesh (672 tris, 6 slots) + 11-bone skeleton, bind pose, no animation | Skeletal Mesh `SK_Bonefish`, **creates** skeleton, rename it **`SKEL_Fish`** |
| `SK_CoralSnapper.fbx` | skinned mesh (796 tris, 6 slots) + the same 11 bones at its own positions | Skeletal Mesh `SK_CoralSnapper` on **`SKEL_Fish`** |
| `A_Fish_Rest.fbx` | armature only, the straight fish, 2 identical keys (frames 0-1) | Animation on `SKEL_Fish`: the **additive base**, not additive itself |
| `A_Fish_Swim_Idle.fbx` ... `A_Fish_Hooked_Hang.fbx` (9 motion clips, table below) | armature only, one take each (take name = file name) | Animation on `SKEL_Fish`, then **additive** (below) |
| `A_Fish_Curled.fbx` | armature only, a 1-frame pose (2 identical keys, frames 0-1) | Animation on `SKEL_Fish`, **additive** like the clips, loop off |

`SM_Bonefish` / `SM_CoralSnapper` (static, same folder) stay the ambient fish. SK and SM use the same material
names (`M_Bonefish_*`, `M_CoralSnapper_*`, shared `M_Fish_Eye`): import everything into `/Game/Art/Fish` so the
importer reuses one set of materials.

## Axes, units, origin

- **Centimeters**: FBX `UnitScaleFactor` = 1.0 exactly, bone/vertex/key values in cm, no scale on any node (largest
  deviation 2.4e-7). Written by `pb.export_skeletal_fbx`, the armature exported as `Armature` so Unreal drops that node
  and `root` is the skeleton root. **Convert Scene Unit OFF**, Convert Scene ON, Force Front X Axis OFF, uniform
  scale 1.0, no physics asset.
- Unreal = Blender x 100 with Y negated: **+X = nose, +Y = the fish's RIGHT side, +Z = up** (Blender +Y is its left).
- Origin = the body center (halfway nose tip to tail tips) on the spine line. No root motion; `root` never moves.
- Blender re-import of all 11 files (RESULT_JSON `reimport_check`): 11 bones, bone heads and axes 0.0 mm / 0.0 deg
  off, rest bone scale 1.0, rest mesh identical vertex by vertex, only the 8 deform groups exported as skin, clip poses
  <= 0.0001 mm / 0.0 deg off at 6 frames each.

## Skeleton `SKEL_Fish` (11 bones, one skeleton for every fishkit species)

Every bone's reference ROTATION is the identity (bone frame = fish frame), and no clip keys a translation, so a local
yaw means the same bend on every species. Only the bone POSITIONS differ per species.

| Bone | Parent | Deforms | Bonefish head (cm) | CoralSnapper head (cm) | Purpose |
|---|---|---|---|---|---|
| `root` | - | no | (0, 0, 0) | (0, 0, 0) | mesh pivot, never animated |
| `Spine_01` | root | yes | (13.14, 0, 0) | (13.61, 0, 0) | chest, joint J1. **The anchor: never yaws or pitches** (roll only), in every clip but `A_Fish_Hooked_Hang` |
| `Head` | Spine_01 | yes | (13.14, 0, 0) | (13.61, 0, 0) | head about J1 (curl, shakes, dive tilt) |
| `Mouth` | Head | no | (26.30, 0, -1.11) | (27.72, 0, -2.45) | nose tip: **line / hook attach** (bone names work as socket names); **never moves in `A_Fish_Hooked_Hang`** |
| `Fin_Pectoral_L` / `_R` | Spine_01 | yes | (15.10, -/+3.00, -2.95) | (15.26, -/+3.21, -4.06) | pectoral root chord center; flaps (tuck/flare) |
| `Grip` | Spine_01 | no | (9.43, 0, 0) | (9.66, 0, 0) | chest center: **hand / display attach**, still in every clip but `A_Fish_Hooked_Hang` (never hold that one at Grip) |
| `Spine_02` | Spine_01 | yes | (5.72, 0, 0) | (5.70, 0, 0) | J2 |
| `Spine_03` | Spine_02 | yes | (-1.71, 0, 0) | (-2.22, 0, 0) | J3 |
| `Spine_04` | Spine_03 | yes | (-8.71, 0, 0) | (-9.70, 0, 0) | J4 |
| `Tail` | Spine_04 | yes | (-14.49, 0, 0) | (-15.85, 0, 0) | J5, tail root; carries the caudal fin |

Component space, cm; `_L` has -Y, `_R` has +Y. Skin: linear blend, max 2 influences, weights sum to 1.0, every vertex
weighted by its X (fins move exactly with the body beside them); the pectorals hinge on their root strip.
Bounds at rest: Bonefish 53.5 x 11.4 x 21.1 cm, CoralSnapper 56.3 x 12.5 x 28.9 cm (reference weight size).

## Clips (30 fps, all loops except the Curled pose, rotation-only, root motion none)

| Asset | Frames | Length | Tail beat | What |
|---|---|---|---|---|
| `A_Fish_Rest` | 0-1 | 0.033 s | - | straight fish (2 identical keys): the additive base pose |
| `A_Fish_Swim_Idle` | 0-60 | 2.0 s | 1.0 Hz | slow cruise / hover, pectorals sculling |
| `A_Fish_Swim_Fast` (S3) | 0-30 | 1.0 s | 3.0 Hz | a hard swim against the line (moves Swim, Charge, unknown moves; the escape), a notch below Fight_Run: stroke strength keyed (strong, weak, strong), the head wags and nods against the line, the body rolls into its strokes (up to 12 deg: flank flashes), a small porpoise; pectorals half tucked, fluttering |
| `A_Fish_Hooked_Thrash` (S3) | 0-90 | 3.0 s | - | the hook set (the first 1.0 s of every fight plays f0-35 at the Bonefish's rate, f0-26 at the CoralSnapper's): whole-body head shakes f1-16 (5 Hz; the head throws 18 deg with a 9 deg nod a quarter shake later, the body wags in quick C-bends and rolls up to 28 deg with every shake), coil-snap-rebound f16-40 with a tail slap in pitch, a tail flutter as it dies out (f37-45); mirrored f45-90. Pectorals flared, fluttering |
| `A_Fish_Fight_Run` (S3) | 0-45 | 1.5 s | 4.0 Hz | an all-out run against the line: the strongest surge first (f0-15; the move restarts the clip), head shakes against the line f24-36 (6 Hz, the body answering 2 frames later), a gasp f33-42 (a weaker stroke, pectorals flared), a surge again at the wrap; the body rolls into the strokes at irregular times (up to 22 deg: flank flashes), the head wags and nods with every stroke; pectorals flat |
| `A_Fish_Fight_Dive` (S3) | 0-60 | 2.0 s | 3.0 Hz | digging for the reef: head 13 deg nose-down and nodding down with every stroke, body arched into the dive, tail porpoising, a slow twist that flashes the flank (28 deg one way, then 26 the other), head-shake bursts f8-20 and f38-50 (6 Hz, the second mirrored) |
| `A_Fish_Fight_Dart` (S3) | 0-36 | 1.2 s | - | C-start dart to the fish's LEFT (coil f0-4, power stroke f8, three fading beats to f18), then to its RIGHT (f18-36); banks into each C-start (left side down in a dart left, up to 24 deg) so it reads side-on too; pectorals flared as a brake in the coil, slapped flat for the stroke |
| `A_Fish_Landed_Flop` | 0-90 | 3.0 s | - | out of water: curls up (f0-10), slaps flat (f13), rebound, tail flicks (f24-36), second flop (f44-66). Curls only toward the fish's LEFT |
| `A_Fish_Curled` | 0-1 | pose | - | **not a loop**: 1-frame pose, dead still. The iced catch in a cooler: lying on its side, back-arched in the flank plane, the curl in the tail half (head 20 deg at the neck, Spine_02 straight, Spine_03/Spine_04/Tail 36/44/36 deg toward the back = 116 deg), pectorals flat; the snapper crest stays straight and reads as a clean saw-tooth. Bonefish 42.6 x 22.0 cm, CoralSnapper 44.6 x 29.6 cm footprint (from 53.5 / 56.3 cm straight), thickness unchanged (8.5 cm) |
| `A_Fish_Swim_Tired` (S3, new) | 0-90 | 3.0 s | 0.67 Hz | the exhausted fish (stamina 0, T-058): **upright** (a lean of at most 3.5 deg; the actor roll must be 0) and spent. It starts in a glide (the moment it gives up; the role change restarts the clip), one slow heavy stroke f20-46 (the tail sweeps wide, the tail half lifts out of its sag, the head heaves up 9 deg), a weaker return f46-62, a glide, a half-hearted flick f68-80, a glide into the loop. In the glides the tail sags 14 deg, the nose drops level and the pectorals flare out, sculling slowly to stay upright; they fold in for the stroke. Calmer than any fight clip, and not the even cruise of Swim_Idle |
| `A_Fish_Hooked_Hang` (S3, new) | 0-120 | 4.0 s | - | the landed fish dangling on the hook (T-060): **the head is locked: `Mouth` moves 0.0 mm in component space on every frame, at any alpha**, and the body kicks behind it in bursts and pauses: a burst f6-54 (a small wind-up f9, the first coil f14, the hardest kick f17, then a fading kick every 5 frames), a pause f54-72 (only a slow twist sway), a second burst f72-104 (strongest kick f83), a small twitch f106-117. Each kick curls the body (yaw), twists it up to 50 deg at the tail (the pale belly turns toward the viewer) and arches the tail half in the screen plane, so it reads side-on; pectorals flare in the kicks. Authored for the hang view (nose up the line, its right side toward the viewer). **Breaks the anchored chest: never hold it at `Grip`** |

Loops: frame N equals frame 0 exactly and the motion is continuous across the seam: the seam's second difference is
below each clip's median, except Landed_Flop (11.0 vs 7.2 mm; its curl starts from a hold at f0; not an S3 change).
Suggested notifies (audio/VFX, optional; the tail-tip speed peaks, central difference): Landed_Flop `FishSlap` at f13 and
f55 (light at f19, f61); Fight_Dart `Splash` at f6 and f24; Hooked_Thrash `Splash` at f22 and f67 (the coil-snap; light
at f9 and f54, the shakes); Hooked_Hang `FishKick` at f17 and f83 (light at f111), also where T-061 can give the line a
kick. The fight clips run in open water under the surface; Swim_Fast, Run and Dive need no notifies.

## Unreal import (editor-operator)

`pipeline_unreal.import_skeletal_mesh` / `import_animation` hard-code Convert Scene Unit ON, and `import_skeletal_mesh`
turns **Update Skeleton Reference Pose ON** whenever a skeleton is given. For these files use the snippet below. It uses
the same helpers with Convert Scene Unit OFF, and keeps `SKEL_Fish`'s reference pose = the Bonefish = `A_Fish_Rest`.

```python
import unreal
import pipeline_unreal as pu

SRC, DEST = "C:/GameDev/VibeGame/art/export/Fish/", "/Game/Art/Fish"
CLIPS = ["A_Fish_Swim_Idle", "A_Fish_Swim_Fast", "A_Fish_Hooked_Thrash", "A_Fish_Fight_Run", "A_Fish_Fight_Dive",
         "A_Fish_Fight_Dart", "A_Fish_Landed_Flop", "A_Fish_Curled", "A_Fish_Swim_Tired", "A_Fish_Hooked_Hang"]
NOT_LOOPING = ("A_Fish_Rest", "A_Fish_Curled")          # the base pose and the 1-frame cooler pose

def opts(kind, skel=None):
    o = pu._fbx_options(kind, skel, import_materials=(kind == "skeletal"), update_ref_pose=False)
    key = "skeletal_mesh_import_data" if kind == "skeletal" else "anim_sequence_import_data"
    o.get_editor_property(key).set_editor_property("convert_scene_unit", False)   # the files are cm
    return o

def mesh(name, skel=None):
    paths = pu._run_import(SRC + name + ".fbx", DEST, name, options=opts("skeletal", skel), factory=unreal.FbxFactory())
    return pu._first_of(paths, unreal.SkeletalMesh, DEST + "/" + name)

bone = mesh("SK_Bonefish")                                          # 1. creates the skeleton
skel = pu._rename(bone.get_editor_property("skeleton"), "SKEL_Fish")
skel.set_skeleton_preview_mesh(unreal.load_asset(DEST + "/SK_Bonefish"))
snap = mesh("SK_CoralSnapper", skel)                                # 2. on SKEL_Fish, ref pose NOT updated
pu._save(unreal.load_asset(DEST + "/SK_Bonefish"), snap, skel)

for name in ["A_Fish_Rest"] + CLIPS:                                # 3. animations
    o = opts("animation", skel)
    o.set_editor_property("override_animation_name", name)
    paths = pu._run_import(SRC + name + ".fbx", DEST, name, options=o, factory=unreal.FbxFactory())
    anim = pu._rename(pu._first_of(paths, unreal.AnimSequence, DEST + "/" + name), name)
    anim.set_editor_property("enable_root_motion", False)
    anim.set_editor_property("loop", name not in NOT_LOOPING)
    pu._save(anim)

rest = unreal.load_asset(DEST + "/A_Fish_Rest")                     # 4. additive on A_Fish_Rest frame 0
for name in CLIPS:
    a = unreal.load_asset(DEST + "/" + name)
    # Order matters: setting the type properties clears ref_pose_seq, so set the base clip LAST.
    a.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_LOCAL_SPACE_BASE)
    a.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
    a.set_editor_property("ref_pose_seq", rest)
    a.set_editor_property("ref_frame_index", 0)
    pu._save(a)
    print(pu.anim_report(a.get_path_name()))
```

Verify before handing back (stop and report if any fails; don't fix it in the editor):
1. `pu.skeleton_report(DEST + "/SK_Bonefish")`: 11 bones, `root` first, heads as in the Bonefish column above, bounds
   about 53.5 x 11.4 x 21.1 cm, physics asset None, every bone scale 1.0. Note: `skeleton_report` reads the
   SKELETON's pose, so on SK_CoralSnapper it prints the Bonefish numbers. That is expected and not a check of the snapper.
2. The snapper's OWN positions: on a SkeletalMeshComponent showing SK_CoralSnapper,
   `c.get_ref_pose_position(c.get_bone_index("Mouth"))` = (14.11, 0, -2.45) (local to Head; Bonefish (13.16, 0,
   -1.11)), and `Tail` local to Spine_04 = (-6.15, 0, 0) (Bonefish (-5.78, 0, 0)).
3. `anim_report` of each clip: `AAT_LOCAL_SPACE_BASE`, `ABPT_ANIM_FRAME`, loop True, root motion False, frames
   Swim_Idle 60, Swim_Fast 30, Hooked_Thrash 90, Fight_Run 45, Fight_Dive 60, Fight_Dart 36, Landed_Flop 90,
   Swim_Tired 90, Hooked_Hang 120 (+1 key each), 11 tracks; `A_Fish_Curled` the same but loop False and 1 frame
   (+1 key); `A_Fish_Rest` not additive, 1 frame.
   **S3 update of an existing import** (the fight clips revised, two clips new): run steps 3-4 for these 7 only:
   `["A_Fish_Swim_Fast", "A_Fish_Hooked_Thrash", "A_Fish_Fight_Run", "A_Fish_Fight_Dive", "A_Fish_Fight_Dart",
   "A_Fish_Swim_Tired", "A_Fish_Hooked_Hang"]`. The first five replace the existing assets under the same names
   (`_run_import` replaces; step 4 sets the additive base again), so ABP_Fish keeps its pins; their lengths change
   (Swim_Fast 24 -> 30 frames, Fight_Run 30 -> 45). SK_*, A_Fish_Rest, Swim_Idle, Landed_Flop and Curled are
   unchanged (the committed files re-import to the current rig within 0.0001 mm): don't re-import them.
4. Screenshot: the ABP preview (below) on SK_CoralSnapper with Role = Dart, paused at 0.13 s (frame 4): the head and
   tail both curl toward the fish's LEFT (Unreal -Y), the fish banks (left side down), the crest intact, the fish NOT
   stretched to the Bonefish's length. Expected component-space bone heads at Fight_Dart frame 4, alpha 1 (S3: the bank
   moved them from the T-008 numbers): Bonefish `Mouth` (24.54, -5.56, -3.69), `Tail` (-12.17, -7.48, -3.33);
   CoralSnapper `Mouth` (25.83, -5.45, -5.11), `Tail` (-13.38, -7.98, -3.55).
   If the snapper shows the Bonefish's numbers, the additive base is wrong.
   Hooked_Hang check (Role = Hang once ABP_Fish has that pin, Eng follow-up 3; paused at frame 16 = 0.53 s): `Mouth`
   exactly at its reference position, Bonefish
   (26.30, 0, -1.11), CoralSnapper (27.72, 0, -2.45) (also at alpha 0.8); `Tail` Bonefish (-13.81, -4.89, -2.00),
   CoralSnapper (-15.12, -5.22, -2.13). Swim_Tired frame 36: `Tail` Bonefish (-14.08, 1.90, -0.41), CoralSnapper
   (-15.41, 2.02, -0.44).

## Why additive (read before changing the import)

Both species share one skeleton but not one set of proportions. A normal (non-additive) clip carries the Bonefish's
bone offsets in its translation tracks. On the snapper, Unreal's default retargeting ("Animation") would pull every
joint to the Bonefish positions and deform it. The per-bone "Skeleton" retarget mode would work, but it can't be set
from Python. So every clip is a **Local Space additive whose base is `A_Fish_Rest` frame 0**. It was exported from the
same armature as the clips, so every translation delta is exactly 0 and the rotation deltas are the clip's local
rotations. Played on the mesh's own reference pose (`Local Space Ref Pose` gives the PLAYING mesh's ref pose,
BoneContainer.cpp:148-151), each species keeps its proportions. Don't use `ABPT_REF_POSE` (the skeleton's pose): it
changes when a mesh is imported with Update Skeleton Reference Pose ON, and every clip would then carry wrong deltas.
Checked end to end in Blender (RESULT_JSON `reimport_check.additive`): the re-imported A_Fish_Rest + each clip,
applied as Local Space additives to each re-imported SK, match the source fish vertex by vertex within **0.0001 mm**
for both species and all 10 clips (the 9 motion clips and A_Fish_Curled).

## ABP_Fish (thin child of a C++ UFishAnimInstance; unreal-engineer + editor-operator)

C++ `UFishAnimInstance` (the unreal-engineer) exposes, read by the graph only:
`EFishAnimRole Role` (UENUM, in this order: `SwimIdle, SwimFast, Thrash, Run, Dive, Dart, Flop, Curled, Tired,
Hang`; `Curled` is new for T-030, `Tired` and `Hang` for S3 (Eng follow-ups 2 and 3), each appended at the END so the
existing Blend Poses pins keep their order; `Tired` plays `A_Fish_Swim_Tired`, `Hang` plays `A_Fish_Hooked_Hang`,
both plain Sequence Players like the fight roles), `float PlayRate`,
`float Amplitude` (0..1), `float RoleBlendTime` (default 0.2 s), `float DartStartTime` (0.0 or 0.6),
`UAnimSequenceBase* DisplayPose` + `float DisplayPoseTime` (T-030f: the held pose, set by `SetHeldPose`).
Graph, nothing else:
`Local Space Ref Pose` -> **Apply Additive** (Base) <- Additive: **Blend Poses by EFishAnimRole** (one Sequence
Player per role, clips per the table below, PlayRate pin <- `PlayRate`, blend time per pose <- `RoleBlendTime`,
**Reset Child on Activation** on; the Dart player's Start Position <- `DartStartTime`; the **Curled** player (node
clip `A_Fish_Curled`) has its **Sequence** pin shown and <- `DisplayPose`, Start Position <- `DisplayPoseTime`) ;
Apply Additive **Alpha <- `Amplitude`** -> Output Pose. Curled is a pose slot: whatever clip DT_CoolerDisplay
`FishPose` names plays there (a new display pose = a new additive clip + a table edit, no code), held because C++
sets PlayRate 0, Amplitude 1, RoleBlendTime 0. The cooler uses this path only when the graph has the Curled pin
(`UFishAnimInstance::ClassHasRolePin`); without it the display falls back to a single-node player, which is not
cook-safe for additive clips (T-030f). The Apply Additive alpha is clamped to 0..1, so the clips are authored at the maximum
and species scale down.

## Which clip each fight state plays (T-007 `FLureFightNetState`; S3 target, after the Eng follow-ups below)

| State | Role -> clip | Play rate | Alpha |
|---|---|---|---|
| ambient, not hooked | none: static `SM_*` + MF_FishSwim (below) | - | - |
| hook set: the first `HookSetThrashTime` (1.0 s) of the fight | Thrash -> `A_Fish_Hooked_Thrash` (from 0: the shakes first, the coil-snap from f16) | effort | effort |
| `MoveId` Run | Run -> `A_Fish_Fight_Run` (restarts: the surge first) | effort | effort |
| `MoveId` Dive | Dive -> `A_Fish_Fight_Dive` | effort | effort |
| `MoveId` Dart | Dart -> `A_Fish_Fight_Dart`, `DartStartTime` 0.0 when the dart goes to the fish's left (-Y), 0.6 to its right | effort | effort |
| `MoveId` Swim, Charge, unknown moves | SwimFast -> `A_Fish_Swim_Fast` | effort | effort |
| `MoveId` Rest | SwimIdle -> `A_Fish_Swim_Idle` (calm: "reel now") | effort | effort |
| `MoveId` Sulk (S3) | SwimIdle -> `A_Fish_Swim_Idle` (was Thrash: Sulk is a rest at the reef, and the Thrash is now violent; Eng follow-up 4) | effort | effort |
| `bExhausted` (S3) | **Tired -> `A_Fish_Swim_Tired`, upright: actor roll 0** (was SwimIdle at rate 0.5, alpha x 0.5, rolled 70 deg onto its side; Eng follow-up 2) | other | species |
| `Outcome` Landed: in hand, on the dock | Flop -> `A_Fish_Landed_Flop` (in hand: held at `Grip`) | other | **1.0 always** |
| hanging on the hook (S3: the landed fish dangling from the rod, every machine) | **Hang -> `A_Fish_Hooked_Hang`**: the actor stays placed by the reference `Mouth`, which the clip never moves (was Flop on the owner's machine and a still mesh on the others; Eng follow-up 3) | other | species |
| `Outcome` Snapped / Escaped | SwimFast, then swim away and despawn | swim | species |
| in a cooler (T-030 display, not a fight state) | Curled -> `A_Fish_Curled` (a still pose; a fish dropped in from the hand may blend in from the Flop over the usual 0.2 s, it reads as settling; a fish spawned into a cooler, e.g. on load, starts as Curled with no blend) | any | **1.0 always** |

Rate and alpha columns: **effort** (S3, Eng follow-up 1) = PlayRate `AnimRate x (ReferenceWeight / Weight)^(1/6) x
lerp(TiredRate, FreshRate, Stamina)` and alpha `AnimAmplitude x lerp(0.75, 1, Stamina)`: a fresh fish fights at full
rate and size and calms down smoothly as it tires, whatever its ground speed. **other** = `AnimRate x (ReferenceWeight
/ Weight)^(1/6)`. **swim** = the T-008 speed rule below, now only for the escape. **species** = `AnimAmplitude`.
The clips are authored for a fresh fish at rate 1.0.

Proposed data (so new moves and species need no code): a new `AnimRole` field on `FLureFightMove` (T-007), with
None falling back by move Id as above and SwimFast for unknown Ids; and DT_FishSpecies "Look" columns `AnimAmplitude`
(0..1, default 1), `AnimRate` (default 1) and `LieOffsetCm`. Suggested values: Bonefish 1.0 / 1.15 / 4.24 and
CoralSnapper 0.8 / 0.85 / 4.26. Change of mind vs the handoff: Rest plays the calm Swim_Idle, not the Thrash, because
T-007 tells the player to reel while the fish rests, so it must read as calm.

Rules for the C++ side:
- **Play rate while fighting (S3): effort, not speed.** A hooked fish pulls in place: its ground speed is mostly the
  reel's drag, so the T-008 speed rule played a fresh fish that is being reeled in at the 0.5 floor (the T-008 Run's
  tail at 1.5 Hz read as cruising) and a spent one faster (gate A diagnosis, Eng follow-up 1). Use the **effort** rule.
- **Play rate, the escape swim** (tail beat matched to speed, stride = 0.7 body length per beat):
  `PlayRate = AnimRate x U / (0.7 x L x ClipHz)`, clamped to 0.5..2.0. U = the fish actor's speed (cm/s), L = its length
  (cm) = 53.5 / 56.3 at reference size x the actor scale, ClipHz = the Tail beat column (S3: Swim_Idle 1.0, Swim_Fast 3.0,
  Run 4.0, Dive 3.0; DT_FishVisual `RoleTailBeats` still has the T-008 2.5 / 3.0 / 2.0, Eng follow-up 1). At rate 1 the
  Bonefish covers 37 / 112 / 150 / 112 cm/s in Idle / Fast / Run / Dive. **Other roles**:
  `AnimRate x (ReferenceWeight / Weight)^(1/6)` (a fish twice as heavy moves 11 % slower).
- **Scale**: the caught fish's component scale = `(Weight / ReferenceWeight)^(1/3)`, uniform (fishkit rule).
- **In hand**: attach the fish so its `Grip` bone sits in the hand socket. That is the relative location minus Grip's
  component position (x scale); read it at runtime with `GetBoneLocation("Grip", EBoneSpaces::ComponentSpace)` (no
  data). Grip never moves in any clip but the Hang, so the fish stays steady while the head and tail swing.
- **On the dock**: actor rotation = FRotator(0, dock yaw, **90**) (roll +90 puts Unreal +Y, the fish's right side, down)
  and location = dock top + (0, 0, `LieOffsetCm` x scale). Landed_Flop only curls toward the fish's left (up), so
  nothing ever goes below the planks at alpha 1 (checked on every frame: 0.0 mm). At alpha 0.8 the lower pectoral would
  stick out 3.5 mm below the plank, which is why the Flop always plays at alpha 1.
- **Line**: attach the line / hook to bone `Mouth` (its X axis = the fish's forward).
- **Hanging on the hook (S3)**: play Hang. Keep the placement as it is: `ALureFishItem::UpdateHooked`
  (LureFishItem.cpp:822-835) and the T-032 physics line (`AttachEndActor(Fish, HangLineLength, GetMouthOffset())`,
  LureCatchLinkSubsystem.cpp:136, :163) pin the REFERENCE Mouth offset to the line end, and A_Fish_Hooked_Hang keeps
  the Mouth exactly there (0.0 mm, any alpha), so the head stays on the line while the body kicks. Never hold the Hang
  at `Grip` (its chest moves); in the hand the fish plays Flop.

## Seen from the player's camera (S3; `anim_fish_views.py`)

**From the dock.** Eye 1.7 m on a 0.6 m dock (2.3 m above the water), the game camera (90 deg, 1920x1080) aimed at the
fish, 30 cm deep at 5 m and 50 cm deep at 12 m, EEVEE with a see-through water surface (tint and surface color
calibrated on the A2 playtest shot 03_fight_start). The sheets show the GAME pixels around the fish (zoomed, nearest
neighbour) every 1/15 s of game time at the S3 rates. Tail-tip screen speed, px per game second (mean), stamina 1
unless noted; "today" = the same clip at the 0.5 floor that today's speed rule gives while reeling:

| Clip, species (S3 rate / alpha) | 5 m behind | 5 m side | 12 m | stamina 0.2 | today, rate 0.5 |
|---|---|---|---|---|---|
| Fight_Run, Bonefish (1.15 / 1.0) | 324 | 144 | 143 | 187 | 146 |
| Fight_Run, CoralSnapper (0.85 / 0.8) | 208 | 86 | | | |
| Swim_Fast, Bonefish | 168 (rear quarter) | 95 | 66 | 93 | 74 |
| Swim_Fast, CoralSnapper | 103 (rear quarter) | 56 | | | |
| Fight_Dive, CoralSnapper (actor pitch -20) | 150 | 74 | 67 | 85 | 89 |
| Fight_Dive, Bonefish | 236 | 123 | | | |
| Fight_Dart, Bonefish | 279 | 184 | 65 (side) | 107 (side) | |
| Fight_Dart, CoralSnapper | 176 | 104 | | | |
| Hooked_Thrash, Bonefish | 230 | 138 | 104 | 158 | |
| Hooked_Thrash, CoralSnapper | 159 | 90 | | | |
| Swim_Tired, Bonefish (1.15 / 1.0) | 25 (front 23) | 12 | 11 (front) | | |
| Swim_Tired, CoralSnapper (0.85 / 0.8) | 19 (front 18) | | | | |
| Swim_Idle, Bonefish (the Rest move) | 43 | | | | |
| today's exhausted fish: Swim_Idle at 0.5, alpha 0.5, rolled 70 deg | 8 (front 8) | | | | |

So a fresh Run reads about 7x a resting fish and 13x the tired one from behind; a tiring fish (stamina 0.2) still
reads as fighting (4x the Idle); the tired fish's stroke swings the tail 14 px at 5 m from the front or behind (6 px
side-on; the head heave and the tail-half lift carry that view), 3x the old rolled fish.
Previews: `SK_Fish_dock_<Clip>.png`, `SK_Fish_tired_vs_run.png` (Run fresh / tiring, Tired from its stroke and from its
glide, Idle), `SK_Fish_exhausted_before_after.png`.

**In first person, hanging.** The fp_preview camera (90 deg, eye at 1.7 m), the rod in the HoldRod_Idle pose drawn at
FirstPersonScale 0.6 (the drawn rod tip at 52.7 % / 14.6 % of the screen, as in the arms spec), the hook 40 cm below
it (DT_Catch `HangLineLength`) at 47.9 % of the screen height and 1.07 m ahead; the fish nose up with its right side
to the viewer, as `UpdateHooked` turns it. The Mouth stays on the hook in every frame (world check 0.0 mm at alpha 1.0
and 0.8). Previews: `A_Fish_Hooked_Hang_fp.png` (day and dusk, both species, 13 key frames),
`A_Fish_Hooked_Hang_fp_day_full.png` / `_dusk_full.png` (the whole frame at f14), `A_Fish_Hooked_Hang_strobe.png`
(every 2nd frame of both bursts, player view and edge view: every midline leaves the hook along the same head line).

## T-061: how the hanging fish's body moves (for the line)

Jimmy's note: when the dangling fish wiggles, the line should move "similar to the physics of the user rotating their
camera". While the head hangs still, the body kicks; this is where it goes.

- **Runtime rule (no table needed):** body-centre offset = **0.38 x (Spine_03's component-space location minus its
  reference location)**, e.g. `GetBoneLocation("Spine_03", EBoneSpaces::ComponentSpace)` minus the same at rest. Checked
  on every frame against the body's volume centroid: within 1.2 mm (Bonefish) / 2.3 mm (CoralSnapper); the offset peaks
  at 1.9 / 2.1 cm. It follows any retune of the clip, the alpha and the play rate. (Grip or Spine_02 alone are 3-4 mm
  off; the Tail bone 6-7 mm.)
- **Physics:** the kicks are internal forces, so for a moment the fish's centre of mass stays put and the hook moves the
  other way: line end offset = minus the body-centre offset, across the line (up to ~2 cm x the actor scale, fastest
  ~1 cm per frame at f16). That alone is a small jiggle. For the camera-turn feel Jimmy asks for, also give the
  pendulum / physics line an impulse at the `FishKick` notifies (f17, f83; light f111) against the tail tip's direction
  of motion; the gain is a tuning number in data.
- **Table** (Bonefish, reference size, alpha 1; component space, cm, offsets from the reference pose; +X = the nose =
  up the line, +Y = the fish's right = toward the viewer, +Z = its back). Rows are the kick extremes and the notify
  frames; between them the motion is smooth (monotone splines, the extremes flat). Every frame, both species:
  RESULT_JSON `hang_offsets_t061` of anim_fish.py (deterministic; rerun the recipe to regenerate it).

| Frame | Body centre (dx, dy, dz) | Tail bone (dx, dy, dz) | Tail tip (dx, dy, dz) |
|---|---|---|---|
| 0 | (0.00, 0.00, 0.00) | (0.00, 0.00, 0.00) | (0.00, 0.00, 0.00) |
| 9 | (0.02, 0.39, 0.03) | (0.29, 3.32, 1.01) | (0.90, 6.45, 3.18) |
| 11 | (0.02, -0.44, -0.06) | (0.25, -2.99, -1.18) | (0.76, -5.50, -3.64) |
| 14 | (0.40, -1.86, -0.24) | (5.22, -12.57, -5.47) | (14.54, -19.18, -15.37) |
| 17 FishKick | (0.02, 0.32, 0.04) | (0.22, 2.82, 1.15) | (0.68, 5.15, 3.52) |
| 19 | (0.29, 1.50, 0.18) | (3.86, 11.06, 4.72) | (11.05, 17.82, 13.60) |
| 22 | (0.01, -0.34, -0.05) | (0.13, -2.19, -0.90) | (0.41, -4.00, -2.75) |
| 24 | (0.20, -1.34, -0.18) | (2.66, -9.34, -3.98) | (7.77, -15.64, -11.70) |
| 27 | (0.00, 0.09, 0.01) | (0.03, 1.10, 0.46) | (0.10, 1.99, 1.41) |
| 29 | (0.10, 0.88, 0.11) | (1.38, 6.83, 2.93) | (4.11, 11.86, 8.78) |
| 34 | (0.04, -0.61, -0.09) | (0.50, -4.16, -1.80) | (1.51, -7.40, -5.45) |
| 39 | (0.01, 0.18, 0.02) | (0.08, 1.72, 0.68) | (0.25, 3.18, 2.09) |
| 44 | (0.00, -0.15, -0.02) | (0.02, -0.90, -0.29) | (0.07, -1.75, -0.91) |
| 54-72 pause | (0.00, 0.00, 0.00) | (0.00, 0.00, 0.00) | (0.00, 0.00, 0.00) |
| 75 | (0.02, -0.41, -0.03) | (0.24, -3.06, -0.85) | (0.75, -6.03, -2.71) |
| 80 | (0.26, 1.42, 0.16) | (3.49, 10.69, 4.28) | (10.10, 17.79, 12.52) |
| 83 FishKick | (0.01, -0.27, -0.04) | (0.08, -1.73, -0.67) | (0.25, -3.21, -2.08) |
| 85 | (0.16, -1.20, -0.15) | (2.13, -8.50, -3.42) | (6.31, -14.73, -10.22) |
| 90 | (0.05, 0.63, 0.07) | (0.73, 5.07, 2.04) | (2.21, 9.16, 6.22) |
| 95 | (0.01, -0.31, -0.04) | (0.11, -2.00, -0.79) | (0.34, -3.70, -2.44) |
| 100 | (0.00, 0.07, 0.00) | (0.01, 0.76, 0.22) | (0.05, 1.50, 0.71) |
| 104 | (0.00, -0.02, 0.00) | (0.00, 0.00, 0.00) | (0.00, 0.00, 0.00) |
| 109 | (0.03, -0.56, -0.05) | (0.46, -4.20, -1.30) | (1.44, -8.09, -4.09) |
| 111 light kick | (0.00, -0.20, -0.02) | (0.05, -1.38, -0.40) | (0.15, -2.71, -1.26) |
| 113 | (0.01, 0.17, 0.01) | (0.06, 1.56, 0.39) | (0.19, 3.12, 1.26) |
| 117-120 | (0.00, 0.00, 0.00) | (0.00, 0.00, 0.00) | (0.00, 0.00, 0.00) |

Reference positions (component cm): body centre Bonefish (8.67, 0, 0.04), CoralSnapper (9.06, 0, 0.72); Tail bone
(-14.49, 0, 0) / (-15.85, 0, 0); tail tip (-26.75, 0, 0) / (-28.13, 0, 0). Peak offsets: centre 1.92 / 2.14 cm, Tail
14.67 / 15.64 cm, tip 28.56 / 29.51 cm (the CoralSnapper plays at alpha 0.8 in game: scale its numbers by ~0.8).
The slow twist sway in the pause turns the body about its own spine line, so it moves none of these points.

## Eng follow-ups (S3)

C++ and data changes for the unreal-engineer (S1), routed by the lead; the art team changes none of this. Numbers 1-4
were agreed with the art manager at gate A (Saved/AgentLogs/tasks/S3-fish/gateA_review.md), 5-6 are new at gate B.
1. **Fight play rate from effort, not ground speed (T-059).** Today `FFightFishVisual::ComputeAnimState`
   (FightFishVisual.cpp:220-226) matches the swim roles' tail beat to the fish actor's speed, which in a fight is mostly
   the reel's drag: a fresh Bonefish that is reeled in plays Run at the 0.5 floor (a 1.5 Hz tail: it reads as
   cruising), a spent one plays faster (0.89), and a resting one hits the 2.0 ceiling (gate A table:
   Saved/AgentLogs/tasks/S3-fish/gateA_playrate_table.md). Proposed, while Fighting:
   `PlayRate = AnimRate x (ReferenceWeight / Weight)^(1/6) x lerp(TiredRate, FreshRate, Stamina)` and
   `Alpha = AnimAmplitude x lerp(TiredAmplitudeShare, 1, Stamina)`. Data (DT_FishVisual): a `RoleEffortRates` list
   (Role, FreshRate, TiredRate): Run, SwimFast, Dive 1.0 / 0.6; Dart, Thrash 1.0 / 0.7; SwimIdle 1.0 / 0.8; and
   `TiredAmplitudeShare` 0.75. It needs the fish's stamina in `FFightFishAnimInput` (FightFishVisual.h:236) from
   `FLureFightNetState::Stamina` (FishFightTypes.h:594, 0..1). The speed rule stays for Escaping only, with
   `RoleTailBeats` updated to the S3 clips: SwimFast 2.5 -> 3.0, Run 3.0 -> 4.0, Dive 2.0 -> 3.0 (SwimIdle 1.0).
   Resulting tail beats for a fresh reference fish: Bonefish Run 4.6 Hz, Swim_Fast and Dive 3.45 Hz; CoralSnapper
   Run 3.4 Hz, 2.55 Hz; at stamina 0 x0.6. The rate no longer depends on the fight's move speeds, which S1 is retuning.
2. **The exhausted fish swims upright (T-058).** `bExhausted` -> the new role `Tired` (A_Fish_Swim_Tired) at the
   "other" rate, alpha `AnimAmplitude`, instead of SwimIdle at `ExhaustedPlayRate` 0.5 x `ExhaustedAmplitudeScale` 0.5
   (FightFishVisual.cpp:203-208); `ExhaustedRollDeg` 70 -> 0 (T-058a; FightFishVisual.cpp:294). Append `Tired` to
   `EFishAnimRole` after `Curled` (FishAnimInstance.h:16) and add its Blend Poses pin (Reset Child on Activation on:
   the clip starts with the glide, the moment the fish gives up).
3. **The dangling fish plays Hang (T-060).** Append `Hang` after `Tired`, pin with A_Fish_Hooked_Hang, "other" rate,
   alpha `AnimAmplitude`. Today the owner's hanging fish is the adopted landed visual (`AdoptVisual`,
   LureFishItem.cpp:221-236) still in Phase Landed, so it plays Landed_Flop (LureFightFish.cpp:170-173,
   FightFishVisual.cpp:191-193) and its head curls off the line; on the other machines the item's own SkeletalFish
   is a still mesh on the hook (`UpdateHandAnim` animates only in Hand mode, LureFishItem.cpp:691-699). Proposed:
   whenever a fish item is held in Hook mode (`UpdateHooked`, LureFishItem.cpp:800; the T-032 physics line,
   LureCatchLinkSubsystem.cpp:131-138), play Hang on whichever mesh shows the fish, on every rendering machine; in the
   hand and on the dock keep Flop. The placement code stays as it is (section "Rules for the C++ side"). The usual
   0.2 s blend from Flop into Hang lets the head settle onto the line for 0.2 s when the fish is lifted; accepted (a
   0 s blend pops the body straight).
4. **DT_FishVisual `MoveRoles` Sulk -> SwimIdle** (was Thrash): Sulk is a rest at the reef, and Hooked_Thrash is now
   a violent hook-set clip.
5. **Face the swim direction while fighting (new).** `FacingRotation` (FightFishVisual.cpp:275-296) faces the fish's
   ground velocity, so a fish being reeled in turns toward the player while it plays Run (Bonefish Run while reeling:
   velocity -30 cm/s toward the player, 29 sideways -> its nose 136 deg from "away"): a running fish seen head-on.
   Face the move's own swim direction (away / to the side, as the DT_FightPattern move pulls) while it swims; the
   ground velocity only for Rest, Sulk and Tired (a spent fish is towed in head-first, which is the view Swim_Tired's
   front rows were checked from).
6. **T-061, the wiggle moves the line:** the runtime rule and the table in section "T-061" (the hook moves opposite to
   the body centre; kick impulses at the `FishKick` notifies). Optional: the audio/VFX notifies of the Clips section.

## Cooler display: A_Fish_Curled + the 4 slots of SM_Cooler_Starter (T-030)

A reference fish (53.5 / 56.3 cm) can't lie straight in the starter cooler (liner about 35 x 51 cm at the floor,
28 cm deep). Fish shown in the cooler lie on their side, curled (`A_Fish_Curled`), stacked in 4 fixed slots.
Source of truth: `art/recipes/anim_fish_cooler.py` (RESULT_JSON `Saved/AgentLogs/blender/anim_fish_cooler.result.json`,
`slots_ue`); it is deterministic (fixed seed) and reruns to the same table.

**The pose.** A back-arched curl in the flank plane (pitch, not the lateral yaw of Dart/Flop), pectorals flat. The
neck takes 20 deg, Spine_02 none, and the tail half the rest (Spine_03/Spine_04/Tail 36/44/36 deg toward the back), so
the snapper's crest (over the neck and Spine_02) stays straight and reads as a clean saw-tooth (designer review
2026-09-23; with the old 24 deg neck + 25 deg Spine_02 its spines crossed and read as broken shards). Lying on its side the fish stays flat, so its thickness (8.5 cm) and
its lie offset are those of the straight fish (`LieOffsetCm` 4.24 Bonefish / 4.26 CoralSnapper, either side down).
A lateral C lying on its side would lift head and tail ~12 cm like a bowl, and 4 of them don't fit under the lid.
Verify in Unreal (component space, alpha 1): Bonefish `Mouth` (25.89, 0, 3.45), `Tail` (-8.38, 0, 9.81);
CoralSnapper `Mouth` (27.71, 0, 2.52), `Tail` (-9.34, 0, 10.45). The two species must differ (additive base check).

**The rule (unreal-engineer).** Per fish shown in the cooler, in the cooler's space:
- shown scale `s = min((Weight / ReferenceWeight)^(1/3), MaxDisplayScale)`, `MaxDisplayScale` = **1.0** for the
  starter cooler (below);
- relative location to the cooler mesh's `Contents` socket = **(X, Y, BedZ + LieOffsetCm(species) x s)** cm;
- relative rotation = **FRotator(Pitch 0, Yaw, Roll)** from the slot row (Roll +90 = the fish's right side down,
  -90 = its left side down);
- animation: ABP_Fish with Role `Curled`, alpha 1 (see the tables above): `UFishAnimInstance::SetHeldPose(FishPose,
  PoseTime)` from DT_CoolerDisplay (T-030f); a spawned display fish starts as Curled with no blend;
- fill order slot 0, 1, 2, 3 (each slot rests on the ones below it). After a fish is taken out, re-seat the rest into
  slots 0..n-1, so there is never a gap under a fish. The slots move with the cooler (carry, lid closed: the top fish
  stays 7.2 mm under the closed lid).

| Slot | X | Y | BedZ | Pitch | Yaw | Roll | Side down |
|---|---|---|---|---|---|---|---|
| 0 | -8.5 | 2.5 | 0.00 | 0 | -85 | 90 | right |
| 1 | -7.0 | 1.5 | 6.40 | 0 | 60 | -90 | left |
| 2 | -6.0 | -1.0 | 12.44 | 0 | -40 | 90 | right |
| 3 | -9.5 | -2.5 | 18.75 | 0 | 85 | -90 | left |

This is the table in DT_CoolerDisplay (Starter and Large rows; the recipe writes it with `FISH_COOLER_WRITE_DT=1`
and checks it on every run, RESULT_JSON `matches_dt`). **The pile lies along the back wall** (T-030g follow-up,
2026-09-24): the front wall hid a lone fish lying at the front from a standing player farther than about 1 m. The
recipe searches in the front frame as before, turns that layout 180 deg about the Contents Z axis (the liner is
symmetric), then polishes it against the exact checks (`polish()`): the plain turn (T-030g's table) showed every eye
and tail in only 48 of 64 shop fills (slot 0's eye under the snapper in slot 2 or 3). The polish moved slot 0 1 cm
further back, slot 1 by (-1.5, -0.5) cm and 5 deg, slot 2 by 0.5 cm, slot 3 by 0.5 cm and 5 deg.

Units cm and degrees, Unreal axes, relative to the `Contents` socket (the liner floor center, 5 cm above the cooler's
pivot; +X = the cooler's front, the latch side). X, Y locate the fish's origin (its body center); the origin sits off
the middle of a back-arched fish's curl, so the slot X values are not centered. Example: a CoralSnapper at s = 1.0 in
slot 2 goes to (-6.0, -1.0, 12.44 + 4.26) = (-6.0, -1.0, 16.70).

Slots alternate sides (right, left, right, left), and same-side slots lie at least 20 deg apart (nose-to-tail chords
115 / -90 / 70 / -115 deg in Blender cooler space, same-side
slots 25 deg apart at the least). Crests (dorsal fins) of neighbouring fish can still lie over each other in places: that is
accepted (lead, 2026-09-23), because the straight snapper crest is what keeps the pile from reading as a jumble.

**How the player sees it (T-030, unreal-engineer).** The slots are designed for the "shop" view: the player **puts the
cooler down on the floor with its front (latch, +X) toward the player**, then opens it (the lid hinges at the back,
so it opens away from the player), and looks in standing (eye 1.65 m) about 0.6 m from the cooler's center. From
there every fish's eye and tail tip is visible in all 64 checked mixes (ray-cast check `view_checks.shop`), for 1, 2,
3 or 4 fish. So when the cooler is set down, yaw it so +X faces the player. Seen from a 0.9 m countertop 0.55 m away
(`view_checks.countertop`), no mix shows every eye and tail (12 of 64 with the old front pile): the 28 cm deep
liner's front wall hides everything low. Don't put an open cooler on a counter for display.

**From farther away (`far_views`, report only; `_far` preview).** Standing 1.5 m from the cooler's center, a lone fish
shows 29-40 % of its upper side over the front rim (the top fish of a pair 40-56 %). From 2.0 m a lone fish shows only
a sliver of back and fins (6-10 %; the 30 cm front wall hides everything lower than about 7 cm at the back). Eyes and
tails are not visible from either distance: they lie low on a flat fish. The game's check
(`Project.Catch.Display.EveryFishVisible`: the smallest shown fish's top clears the sight line over the front rim out
to 1.5 m) passes in every slot with 3.6 / 8.5 / 13.5 / 23.4 cm to spare.

Suggested data (so a new cooler needs no code): a `DT_CoolerSlot` table, one row per slot, e.g.
`Name,CoolerId,SlotIndex,Location,Rotation,MaxDisplayScale` with rows like
`Starter_2,Starter,2,"(X=-6.0,Y=-1.0,Z=12.44)","(Pitch=0,Yaw=-40,Roll=90)",1.0`
(Location Z = BedZ). A cooler shows at most as many fish as it has slot rows. A new cooler model gets its rows by
rerunning `anim_fish_cooler.py` against its liner (the `Large` placeholder row reuses the starter mesh, so it can only
show 4 of its 8 fish until it has its own model and slots).

**Why a display cap of 1.0 and what happens to bigger fish.** Fish scale with weight (a 1.3x fish is 2.2x the
reference weight: 57-60 cm long even curled). The geometric limit for 4 fish is **1.0**: at 1.05 the stack top reaches
32.64 cm, just over the 32.6 cm limit (the closed lid's underside is at 33.0, with a 4 mm margin), and at 1.1 it
reaches 34.4 cm and the fish cut 12 mm into the liner. At 1.0 the stack top is 32.2 cm and every fish shows 15-30 % of
its footprint from above (the top one all of it), so the cap is 1.0. Bigger fish are **shown at 1.0 in the cooler**
(the display only; the instance keeps its weight and value). Unclamped, the 1.3x fish in slot 3 sticks 24 mm through
the liner wall and 18 mm above the lid (preview `_big`). Smaller fish (tested down to 0.7) sit in the same slot on
the same bed; where a small fish lies under a slot, the fish above rests up to (1 - s) x 8.5 cm higher than it needs
to, a gap you could only see from the side, which the cooler wall hides.

**Checked (RESULT_JSON `exact_checks`, 64 cases: all 16 species mixes at s = 1.0 and at 0.7, and 32 random mixes
with each fish at its own scale in 0.7..1.0):** 0 triangle intersections between fish, every vertex at least 3.0 mm
inside the liner wall (2.65 mm from the real, faceted liner wall mesh, no triangle through it; the old front layout
had 4.0: 64/64 at the back wall needs slot 0 that much closer to the wall), the top fish at least 7.2 mm under the
closed lid, nothing below the floor. Pose metrics
(anim_fish RESULT_JSON): cross-section >= 0.928 of rest (limit 0.80), concave-side fold >= 0.466 (CoralSnapper;
limit 0.30), pectorals 0.0 mm into the flank.

Previews (`Saved/AgentLogs/previews/`): `A_Fish_Curled_anim.png` (the pose, both species, on each side over the pale
straight fish, from the back, 3/4); `A_Fish_Curled_cooler.png` (contact sheet) with `_fp_shop` (the design view: cooler on the floor, front toward
a standing player 0.6 m away, game FP camera 90 deg, 1920x1080), `_fp_countertop` (the open cooler on a 0.9 m counter
0.55 m ahead, for comparison), `_top` (straight down), `_big` (the 1.3x fish unclamped), `_slots` (slot diagram with
the Unreal axes) and `_fills` (shop view, 600 px crops at game pixels: 1, 2, 3, 4 fish; top row Bonefish,
CoralSnapper, Bonefish, CoralSnapper, bottom row the other order). Shown mix: slot 0 Bonefish, 1 and 2 CoralSnapper, 3 a 1.3x
Bonefish shown at the cap.

## Ambient swim: MF_FishSwim (static SM_ fish, world position offset)

Many ambient fish don't need a skeleton: the static `SM_*` meshes wiggle in the material. The formula is the same
wave as the clips, including the chest-still correction, so a static and a skinned fish look alike. It is checked
against Swim_Idle in `SK_Fish_strobe.png`, and the reference implementation is `fishrig.wpo_lateral()`.
- u = (BoundsMaxX - LocalX) / (BoundsMaxX - BoundsMinX) from `ObjectLocalBounds` and the vertex local position
  (0 = nose, 1 = tail tips); L = BoundsMaxX - BoundsMinX (cm).
- E(u) = 0.05 + 0.95 x ((u - 0.28) / 0.72)^1.8 for u >= 0.28, else 0.05 + 0.20 x ((0.28 - u) / 0.28)^2.
- w(u) = E(u) x sin(2 pi x (f x Time + Phase - u / 0.95)).
- offset(u) = A x L x [w(u) - w(0.2558) - (w(0.3953) - w(0.2558)) x (u - 0.2558) / 0.1395] (chest J1/J2 held still).
- WPO = TransformVector(Local -> World, (0, offset, 0)).
- Parameters from Custom Primitive Data, so every material slot agrees and a skinned fish (CPD 0) is off: [0] A =
  0.08 x AnimAmplitude (0 = off), [1] f = 1.0 Hz x AnimRate (or speed-coupled like the swim roles), [2] Phase =
  random per fish so schools don't beat in sync. The shared M_ materials need "Used with Skeletal Mesh" (they are on
  the SK meshes too, with CPD 0).

## Verification numbers (RESULT_JSON, all checks PASS)

| Check | Result | Limit |
|---|---|---|
| weights | sum error 0.0, max 2 influences, 0 unweighted | sum 1, <= 3 |
| skinned rest vs static mesh | 0.0 mm | 0.01 |
| body cross-section (LBS pinch), worst frame of any clip | area >= 0.887 of rest (Fight_Run; the others >= 0.928) | >= 0.80 |
| concave-side fold (longitudinal compression) | >= 0.645 of rest in the motion clips (CoralSnapper Fight_Dive); 0.466 in A_Fish_Curled (CoralSnapper) | >= 0.30 |
| pectoral blade inside the flank | <= 0.14 mm (CoralSnapper Fight_Dive) | <= 1.0 |
| Landed_Flop below the dock plane (alpha 1) | 0.0 mm, both species | <= 1.0 |
| loop seams | 0.0 mm; seam second difference below each clip's median (Landed_Flop: 11.0 vs 7.2 mm, a hold at f0) | 0 |
| keyed actions vs pose functions | 0.0 mm / 0.0 deg | 0.01 mm |
| anchored chest (S3): Grip and the chest axis on every frame, every clip but Hooked_Hang | 0.0 mm / 0.0 deg | 0.01 mm / 0.01 deg |
| Hooked_Hang Mouth in component space (S3), every frame, alpha 1.0 and 0.8 (Unreal's nlerp), both species | 0.0 mm (and 0.0 mm from the hook in world space, hanging) | 0.01 mm |
| Swim_Tired upright (S3): largest body roll | 3.5 deg | <= 5 deg |
| FBX units / scale | UnitScaleFactor 1.0, node and key scale 1.0 (<= 2.4e-7) | cm, 1.0 |
| re-import (bones, mesh, clip poses) | <= 0.0001 mm / 0.0 deg | 0.05 mm |
| additive emulation, both species x 10 clips | <= 0.0001 mm | 0.05 mm |
| A_Fish_Curled in the starter cooler (anim_fish_cooler RESULT_JSON, 64 mixes) | 0 fish-fish intersections, >= 3.0 mm inside the liner (2.65 mm from its mesh), >= 7.2 mm under the closed lid; shop view: every eye and tail visible in 64/64 | 0, > 0, > 0, 64/64 |

## Compromises (known, measured)

- **Pectoral tuck = tangency.** A flat fin plate hinged on a convex flank can't lie fully flat. `TUCK_DEG` -22 is the
  deepest angle with no penetration on both species (at -24 it cuts 0.6-0.8 mm). The fin tip still stands 12 mm
  (Bonefish) / 17 mm (CoralSnapper) off the flank, from 31 / 41 mm as modeled.
- **Pectorals hang from the chest.** Parented to the head, every head turn swept the fin root sideways: up to 16 mm into
  the flank, and 9 mm below the dock plane. Under the chest the blade tracks the flank, but its small root strip shears
  when the head turns hard: strain up to 0.80 for 2-4 frames at the tightest curls (Dart, Flop; Thrash 0.71, Dive
  0.62, Hang 0.49). The close-ups
  (`SK_Fish_pec.png`) still read as attached. A general rule opens the inside-of-turn pectoral by 0.25 deg per deg of
  head yaw, which keeps that fin off the flank.
- **Anchored chest.** The chest never yaws in a clip that can be held at Grip (every clip but Hooked_Hang), which keeps
  the Grip hold steady; so the C-curl bends at the neck (head 30 deg)
  and along the tail (-64 deg at the tip). It reads as a C-start from above and as a tail-led flop on the dock.
- The Flop must play at alpha 1 (dock clearance, above).
- **A_Fish_Curled is more J than C.** The head can only bend at the neck (the one joint in front of the anchored
  chest), and any bend at the neck or Spine_02 squeezes the CoralSnapper's crest spines together until they cross.
  So the neck takes 20 deg, Spine_02 none, and the tail half 116 deg: it reads as a stiff, straight-backed fish with
  its tail curled up, not a round C. A gentler curl doesn't fit 4 fish in the starter cooler.
- **Cooler display cap 1.0.** Fish bigger than the reference are shown at reference size in the cooler (details in
  "Cooler display"). Adding a species changes the slot envelope: rerun `anim_fish_cooler.py` and update the slot
  table (the recipe fails loudly if 4 fish no longer fit at 1.0).
- **A_Fish_Hooked_Hang breaks the anchored chest (S3).** To keep the head still, the Head bone takes the inverse of the
  chest's rotation (`fishrig.Pose.head_fixed`), and the chest yaws and twists with the kicks. So the Mouth is exact at
  any alpha, but Grip moves: the Hang must never be held at Grip. The anchor check skips it; the hang_mouth check
  replaces it.
- **Side-on kicks (S3).** A lateral kick seen side-on only moves the tail toward or away from the viewer. The Hang
  twists the body (up to 50 deg at the tail: the pale belly turns to the viewer) and arches the tail half in the screen
  plane, so the kicks cross the screen; seen from its back (the edge view) it still reads as a plain lateral wag.
- **The hanging fish fills the lower half of the first-person frame.** With the camera level the hook sits at 47.9 % of
  the screen height and the fish hangs down to the bottom edge; its strongest kicks (f14-f19, f80-f85) reach the edge.
  That is the hang geometry (HangLineLength 40 cm, the rod pose), not the clip: a player looking a little down frames it.
- **Swim_Tired side-on (S3).** Seen from the side the stroke swings the tail only 6 px at 5 m (14 px from the front or
  behind); the head heave (9 deg) and the tail-half lift carry that view.
- **The dock views are an approximation (S3).** The water in `anim_fish_views.py` is a multiplicative tint plus a
  surface color over an opaque bottom, calibrated by eye on the A2 playtest shot 03_fight_start. The screen numbers
  compare clips with each other; the in-game read depends on the real water material.

## Extending (mid / junior agents)

- **New fishkit species**: its mesh recipe (fishkit), then one line in `anim_fish.py` `SPECIES`. Rerun, check
  `pectoral_hinge_off_canon_deg` < 10 and the verdict, and import its SK on `SKEL_Fish` (step 2 of the snippet; no clip
  changes). Add its `LieOffsetCm` from RESULT_JSON `species.<Name>.dock_lie` to DT_FishSpecies. Then rerun
  `anim_fish_cooler.py` (the cooler slots are designed for every species at once) and update the slot table.
- **New clip**: a function `f -> Pose` plus a `ClipDef` in `fishrig.CLIPS`. Use `Keys` for choreographed channels,
  keep it rotation-only, and don't yaw or pitch the chest (the anchor check fails if you do; the only exception is a
  clip that is never held at Grip and sets `head_fixed`, like the Hang: give it a role the anchor check skips). Add its
  key frames to `STRIP_FRAMES` in anim_fish.py. Rerun, look at its strip and `SK_Fish_seams.png`, and check the
  verdict. Then look at it from the player's camera: add its rows to `FIGHT_ROWS` in `anim_fish_views.py` (dock) and
  play it at the rate the game will give it. Then import it as step 3-4 and add a role.
- **Tuning** (amplitude, frequency, timing): edit the numbers in the fishrig CLIPS section and rerun. The verdict and
  the strips tell you if it broke; `anim_fish_views.py` tells you how it reads from the dock (RESULT_JSON screen
  speeds). Judge fight clips at the rate the game plays them, never at rate 1 alone (the gate A lesson: the old clips
  were played at the 0.5 floor). Mind 30 fps: a shake period under 5 frames plays as a jagged triangle in Unreal
  (linear key interpolation), which is why the Run's head shake is 6 Hz, not 7.5.
- **After a rerun**: a rerun rewrites every FBX (new UIDs and time stamps). Commit only the files of clips you changed;
  `git checkout --` the others (they are identical in content).
- **A different body type** (eel, ray, shark): a new rig module. Don't bend this one.

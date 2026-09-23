# SK_Fish: the shared fish skeleton + clips (T-008)

Source of truth: `art/recipes/anim_fish.py` (build, checks, exports, previews) on `art/lib/fishrig.py` (skeleton, skin,
every motion as a function of the frame). Meshes: the model-artist's `sm_fish_bonefish.py` / `sm_fish_coralsnapper.py`
(fishkit, 136eb12), not edited. Rerun:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fish.py`.
It is deterministic: two runs give structurally identical FBX files (every value equal; only the FBX object UIDs and
time stamps change, so a rerun without a content change rewrites the binaries: don't commit that churn).
All numbers below come from `Saved/AgentLogs/blender/anim_fish.result.json`.

Previews (`Saved/AgentLogs/previews/`): `SK_Fish_anim.png` (both species x 7 clips, 3/4 view at each clip's tightest
bend), `A_Fish_<Clip>_anim.png` (8 frames x Bonefish top/side, CoralSnapper top/side at amplitude 0.8),
`SK_Fish_strobe.png` (midlines per clip + the ambient WPO wave), `SK_Fish_flop_dock.png` (Landed_Flop lying on dock
planks), `SK_Fish_tuck.png`, `SK_Fish_pec.png` (pectoral close-ups), `SK_Fish_weights.png`.

## Files (art/export/Fish/)

| File | Content | Import as |
|---|---|---|
| `SK_Bonefish.fbx` | skinned mesh (672 tris, 6 slots) + 11-bone skeleton, bind pose, no animation | Skeletal Mesh `SK_Bonefish`, **creates** skeleton, rename it **`SKEL_Fish`** |
| `SK_CoralSnapper.fbx` | skinned mesh (796 tris, 6 slots) + the same 11 bones at its own positions | Skeletal Mesh `SK_CoralSnapper` on **`SKEL_Fish`** |
| `A_Fish_Rest.fbx` | armature only, the straight fish, 2 identical keys (frames 0-1) | Animation on `SKEL_Fish`: the **additive base**, not additive itself |
| `A_Fish_Swim_Idle.fbx` ... `A_Fish_Landed_Flop.fbx` (7) | armature only, one take each (take name = file name) | Animation on `SKEL_Fish`, then **additive** (below) |

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
- Blender re-import of all 10 files (RESULT_JSON `reimport_check`): 11 bones, bone heads and axes 0.0 mm / 0.0 deg
  off, rest bone scale 1.0, rest mesh identical vertex by vertex, only the 8 deform groups exported as skin, clip poses
  <= 0.0001 mm / 0.0 deg off at 6 frames each.

## Skeleton `SKEL_Fish` (11 bones, one skeleton for every fishkit species)

Every bone's reference ROTATION is the identity (bone frame = fish frame), and no clip keys a translation, so a local
yaw means the same bend on every species. Only the bone POSITIONS differ per species.

| Bone | Parent | Deforms | Bonefish head (cm) | CoralSnapper head (cm) | Purpose |
|---|---|---|---|---|---|
| `root` | - | no | (0, 0, 0) | (0, 0, 0) | mesh pivot, never animated |
| `Spine_01` | root | yes | (13.14, 0, 0) | (13.61, 0, 0) | chest, joint J1. **The anchor: never yaws or pitches** (roll only) |
| `Head` | Spine_01 | yes | (13.14, 0, 0) | (13.61, 0, 0) | head about J1 (curl, shakes, dive tilt) |
| `Mouth` | Head | no | (26.30, 0, -1.11) | (27.72, 0, -2.45) | nose tip: **line / hook attach** (bone names work as socket names) |
| `Fin_Pectoral_L` / `_R` | Spine_01 | yes | (15.10, -/+3.00, -2.95) | (15.26, -/+3.21, -4.06) | pectoral root chord center; flaps (tuck/flare) |
| `Grip` | Spine_01 | no | (9.43, 0, 0) | (9.66, 0, 0) | chest center: **hand / display attach**, still in every clip |
| `Spine_02` | Spine_01 | yes | (5.72, 0, 0) | (5.70, 0, 0) | J2 |
| `Spine_03` | Spine_02 | yes | (-1.71, 0, 0) | (-2.22, 0, 0) | J3 |
| `Spine_04` | Spine_03 | yes | (-8.71, 0, 0) | (-9.70, 0, 0) | J4 |
| `Tail` | Spine_04 | yes | (-14.49, 0, 0) | (-15.85, 0, 0) | J5, tail root; carries the caudal fin |

Component space, cm; `_L` has -Y, `_R` has +Y. Skin: linear blend, max 2 influences, weights sum to 1.0, every vertex
weighted by its X (fins move exactly with the body beside them); the pectorals hinge on their root strip.
Bounds at rest: Bonefish 53.5 x 11.4 x 21.1 cm, CoralSnapper 56.3 x 12.5 x 28.9 cm (reference weight size).

## Clips (30 fps, all loops, rotation-only, root motion none)

| Asset | Frames | Length | Tail beat | What |
|---|---|---|---|---|
| `A_Fish_Rest` | 0-1 | 0.033 s | - | straight fish (2 identical keys): the additive base pose |
| `A_Fish_Swim_Idle` | 0-60 | 2.0 s | 1.0 Hz | slow cruise / hover, pectorals sculling |
| `A_Fish_Swim_Fast` | 0-24 | 0.8 s | 2.5 Hz | fast swim, pectorals half tucked |
| `A_Fish_Hooked_Thrash` | 0-90 | 3.0 s | - | head-shake burst (f1-16), coil-snap-rebound (f16-40), mirrored (f45-85); body roll +-12 deg |
| `A_Fish_Fight_Run` | 0-30 | 1.0 s | 3.0 Hz | all-out run: big tail, pectorals tucked, head shivers against the line |
| `A_Fish_Fight_Dive` | 0-60 | 2.0 s | 2.0 Hz | digging strokes, head 12-14 deg nose-down, body arched, slow flank-flashing roll, head shakes f36-50 |
| `A_Fish_Fight_Dart` | 0-36 | 1.2 s | - | C-start dart to the fish's LEFT (coil f0-4, power stroke f8, beats to f18), then to its RIGHT (f18-36) |
| `A_Fish_Landed_Flop` | 0-90 | 3.0 s | - | out of water: curls up (f0-10), slaps flat (f13), rebound, tail flicks (f24-36), second flop (f44-66). Curls only toward the fish's LEFT |

Loops: frame N equals frame 0 exactly and the motion is continuous across the seam (the seam's second difference is
below each clip's median). Suggested notifies (audio/VFX, optional): Landed_Flop `FishSlap` at f13 and f55 (light at
f19, f61); Fight_Dart `Splash` at f8 and f26; Hooked_Thrash `Splash` at f26 and f71.

## Unreal import (editor-operator)

`pipeline_unreal.import_skeletal_mesh` / `import_animation` hard-code Convert Scene Unit ON, and `import_skeletal_mesh`
turns **Update Skeleton Reference Pose ON** whenever a skeleton is given. For these files use the snippet below. It uses
the same helpers with Convert Scene Unit OFF, and keeps `SKEL_Fish`'s reference pose = the Bonefish = `A_Fish_Rest`.

```python
import unreal
import pipeline_unreal as pu

SRC, DEST = "C:/GameDev/VibeGame/art/export/Fish/", "/Game/Art/Fish"
CLIPS = ["A_Fish_Swim_Idle", "A_Fish_Swim_Fast", "A_Fish_Hooked_Thrash", "A_Fish_Fight_Run", "A_Fish_Fight_Dive",
         "A_Fish_Fight_Dart", "A_Fish_Landed_Flop"]

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
    anim.set_editor_property("loop", name != "A_Fish_Rest")
    pu._save(anim)

rest = unreal.load_asset(DEST + "/A_Fish_Rest")                     # 4. additive on A_Fish_Rest frame 0
for name in CLIPS:
    a = unreal.load_asset(DEST + "/" + name)
    a.set_editor_property("ref_pose_seq", rest)
    a.set_editor_property("ref_frame_index", 0)
    a.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
    a.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_LOCAL_SPACE_BASE)
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
   60/24/90/30/60/36/90 (+1 key each), 11 tracks; `A_Fish_Rest` not additive, 1 frame.
4. Screenshot: the ABP preview (below) on SK_CoralSnapper with Role = Dart, paused at 0.13 s (frame 4): the head and
   tail both curl toward the fish's LEFT (Unreal -Y), the crest intact, the fish NOT stretched to the Bonefish's
   length. Expected component-space bone heads at Fight_Dart frame 4, alpha 1: Bonefish `Mouth` (24.54, -6.58,
   -1.11), `Tail` (-12.17, -8.19, 0); CoralSnapper `Mouth` (25.83, -7.06, -2.45), `Tail` (-13.38, -8.73, 0).
   If the snapper shows the Bonefish's numbers, the additive base is wrong.

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
applied as Local Space additives to each re-imported SK, match the source fish vertex by vertex within **0.0002 mm**
for both species and all 7 clips.

## ABP_Fish (thin child of a C++ UFishAnimInstance; unreal-engineer + editor-operator)

C++ `UFishAnimInstance` (the unreal-engineer) exposes, read by the graph only:
`EFishAnimRole Role` (UENUM, in this order: `SwimIdle, SwimFast, Thrash, Run, Dive, Dart, Flop`), `float PlayRate`,
`float Amplitude` (0..1), `float RoleBlendTime` (default 0.2 s), `float DartStartTime` (0.0 or 0.6).
Graph, nothing else:
`Local Space Ref Pose` -> **Apply Additive** (Base) <- Additive: **Blend Poses by EFishAnimRole** (one Sequence
Player per role, clips per the table below, PlayRate pin <- `PlayRate`, blend time per pose <- `RoleBlendTime`,
**Reset Child on Activation** on; the Dart player's Start Position <- `DartStartTime`) ; Apply Additive **Alpha <-
`Amplitude`** -> Output Pose. The Apply Additive alpha is clamped to 0..1, so the clips are authored at the maximum
and species scale down.

## Which clip each fight state plays (T-007 `FLureFightNetState`)

| State | Role -> clip | Play rate | Alpha |
|---|---|---|---|
| ambient, not hooked | none: static `SM_*` + MF_FishSwim (below) | - | - |
| hook set: first ~1.0 s after `bActive` turns true | Thrash -> `A_Fish_Hooked_Thrash` (from 0: shakes first) | other | species |
| `MoveId` Run | Run -> `A_Fish_Fight_Run` | swim | species |
| `MoveId` Dive | Dive -> `A_Fish_Fight_Dive` | swim | species |
| `MoveId` Dart | Dart -> `A_Fish_Fight_Dart`, `DartStartTime` 0.0 when the dart goes to the fish's left (-Y), 0.6 to its right | other | species |
| `MoveId` Swim, Charge | SwimFast -> `A_Fish_Swim_Fast` | swim | species |
| `MoveId` Rest | SwimIdle -> `A_Fish_Swim_Idle` (calm: "reel now") | swim | species |
| `MoveId` Sulk | Thrash -> `A_Fish_Hooked_Thrash` (stubborn head shakes at the reef) | other | species |
| `bExhausted` (MoveId None, tired) | SwimIdle at rate 0.5, alpha x 0.5, plus an actor roll of ~70 deg onto its side | fixed | species x 0.5 |
| `Outcome` Landed (in hand / on the dock) | Flop -> `A_Fish_Landed_Flop` | other | **1.0 always** |
| `Outcome` Snapped / Escaped | SwimFast, then swim away and despawn | swim | species |

Proposed data (so new moves and species need no code): a new `AnimRole` field on `FLureFightMove` (T-007), with
None falling back by move Id as above and SwimFast for unknown Ids; and DT_FishSpecies "Look" columns `AnimAmplitude`
(0..1, default 1), `AnimRate` (default 1) and `LieOffsetCm`. Suggested values: Bonefish 1.0 / 1.15 / 4.24 and
CoralSnapper 0.8 / 0.85 / 4.26. Change of mind vs the handoff: Rest plays the calm Swim_Idle, not the Thrash, because
T-007 tells the player to reel while the fish rests, so it must read as calm.

Rules for the C++ side:
- **Play rate, swim roles** (tail beat matched to speed, stride = 0.7 body length per beat):
  `PlayRate = AnimRate x U / (0.7 x L x ClipHz)`, clamped to 0.5..2.0. U = the fish actor's speed (cm/s), L = its length
  (cm) = 53.5 / 56.3 at reference size x the actor scale, ClipHz = the Tail beat column (1.0 / 2.5 / 3.0 / 2.0). At rate
  1 the Bonefish covers 37 / 94 / 112 / 75 cm/s in Idle / Fast / Run / Dive. **Other roles**:
  `AnimRate x (ReferenceWeight / Weight)^(1/6)` (a fish twice as heavy moves 11 % slower).
- **Scale**: the caught fish's component scale = `(Weight / ReferenceWeight)^(1/3)`, uniform (fishkit rule).
- **In hand**: attach the fish so its `Grip` bone sits in the hand socket. That is the relative location minus Grip's
  component position (x scale); read it at runtime with `GetBoneLocation("Grip", EBoneSpaces::ComponentSpace)` (no
  data). Grip never moves, so the fish stays steady while the head and tail swing.
- **On the dock**: actor rotation = FRotator(0, dock yaw, **90**) (roll +90 puts Unreal +Y, the fish's right side, down)
  and location = dock top + (0, 0, `LieOffsetCm` x scale). Landed_Flop only curls toward the fish's left (up), so
  nothing ever goes below the planks at alpha 1 (checked on every frame: 0.0 mm). At alpha 0.8 the lower pectoral would
  stick out 3.5 mm below the plank, which is why the Flop always plays at alpha 1.
- **Line**: attach the line / hook to bone `Mouth` (its X axis = the fish's forward).

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
| body cross-section (LBS pinch), worst frame of any clip | area >= 0.938 of rest | >= 0.80 |
| concave-side fold (longitudinal compression) | >= 0.69 of rest | >= 0.30 |
| pectoral blade inside the flank | <= 0.06 mm | <= 1.0 |
| Landed_Flop below the dock plane (alpha 1) | 0.0 mm, both species | <= 1.0 |
| loop seams | 0.0 mm; seam second difference below each clip's median | 0 |
| keyed actions vs pose functions | 0.0 mm / 0.0 deg | 0.01 mm |
| FBX units / scale | UnitScaleFactor 1.0, node and key scale 1.0 (<= 2.4e-7) | cm, 1.0 |
| re-import (bones, mesh, clip poses) | <= 0.0001 mm / 0.0 deg | 0.05 mm |
| additive emulation, both species x 7 clips | <= 0.0002 mm | 0.05 mm |

## Compromises (known, measured)

- **Pectoral tuck = tangency.** A flat fin plate hinged on a convex flank can't lie fully flat. `TUCK_DEG` -22 is the
  deepest angle with no penetration on both species (at -24 it cuts 0.6-0.8 mm). The fin tip still stands 12 mm
  (Bonefish) / 17 mm (CoralSnapper) off the flank, from 31 / 41 mm as modeled.
- **Pectorals hang from the chest.** Parented to the head, every head turn swept the fin root sideways: up to 16 mm into
  the flank, and 9 mm below the dock plane. Under the chest the blade tracks the flank, but its small root strip shears
  when the head turns hard: strain up to 0.80 for 2-4 frames at the tightest curls (Dart, Flop). The close-ups
  (`SK_Fish_pec.png`) still read as attached. A general rule opens the inside-of-turn pectoral by 0.25 deg per deg of
  head yaw, which keeps that fin off the flank.
- **Anchored chest.** The chest never yaws (so the Grip hold is steady), so the C-curl bends at the neck (head 30 deg)
  and along the tail (-64 deg at the tip). It reads as a C-start from above and as a tail-led flop on the dock.
- The Flop must play at alpha 1 (dock clearance, above).

## Extending (mid / junior agents)

- **New fishkit species**: its mesh recipe (fishkit), then one line in `anim_fish.py` `SPECIES`. Rerun, check
  `pectoral_hinge_off_canon_deg` < 10 and the verdict, and import its SK on `SKEL_Fish` (step 2 of the snippet; no clip
  changes). Add its `LieOffsetCm` from RESULT_JSON `species.<Name>.dock_lie` to DT_FishSpecies.
- **New clip**: a function `f -> Pose` plus a `ClipDef` in `fishrig.CLIPS`. Use `Keys` for choreographed channels,
  keep it rotation-only, and don't yaw or pitch the chest. Rerun, look at its strip, and check the verdict. Then import
  it as step 3-4 and add a role.
- **Tuning** (amplitude, frequency, timing): edit the numbers in the fishrig CLIPS section and rerun. The verdict and
  the strips tell you if it broke.
- **A different body type** (eel, ray, shark): a new rig module. Don't bend this one.

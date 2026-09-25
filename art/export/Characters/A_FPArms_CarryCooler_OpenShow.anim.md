# A_FPArms_CarryCooler_Open / _Show: the carried cooler opened and shown (T-064a)

Source of truth: `art/recipes/anim_fp_arms_cooler.py` (imports `art/recipes/anim_fp_arms.py` read-only: same mesh,
same 18-bone `SKEL_FPArms`, same IK and breath). Rerun:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fp_arms_cooler.py`
(deterministic; numbers below come from its RESULT_JSON `gate_b`, `Saved/AgentLogs/blender/anim_fp_arms_cooler.result.json`).
Base rules (axes, units, bones, import settings, the `cooler` bone, CarryCooler) are in `SK_FPArms.anim.md`.

Previews (`Saved/AgentLogs/previews/`): `SK_FPArms_cooler_open_fp.png` / `_open_fp_dusk.png` (Open, owner's lid 235),
`_open_lid100_fp.png` / `_dusk` (Open, the fallback without the lid follow-up), `_show_fp.png` / `_dusk`,
`_show_outside.png` (another player at 2.5 m), `_grips.png` (all four fists, side-on), `_crossfade_ik.png` /
`_crossfade_fp.png` (mid-blend: plain, the gate A IK plan, the gate B fix), `_anim.png` (breath frames).

## Files (art/export/Characters/)
| File | Content | Import as |
|---|---|---|
| `A_FPArms_CarryCooler_Open.fbx` | armature only, one take, 18 bone tracks | Animation on `SKEL_FPArms` |
| `A_FPArms_CarryCooler_Show.fbx` | armature only, one take, 18 bone tracks | Animation on `SKEL_FPArms` |

`SK_FPArms.fbx` is not re-exported: same mesh and skeleton (the recipe checks its rig against the committed file:
18 bones, heads 0.0 mm, axes 0.0 deg off). No existing clip changes.

## Actions (30 fps, root motion: none, no notifies)
| Asset | Frames | Length | Type | What it is |
|---|---|---|---|---|
| `A_FPArms_CarryCooler_Open` | 0-90 | 3.0 s | loop | The cooler is held open and tilted 55 deg toward the eye (74 deg from the carry). The handles are 48 cm ahead of and 27 cm below the eye, the fish pile faces the player in the lower third, and both fists stay on the rope handles at the lower corners. |
| `A_FPArms_CarryCooler_Show` | 0-90 | 3.0 s | loop | The cooler is turned 65 deg away (mouth, fish and lid toward whoever stands in front) and held low and out, a presenting pose. The handles are 59 cm ahead and 29 cm below the eye, elbows down and nearly straight. The front wall and latch face up at the bottom of the view, and the fists show at the lower corners. |

Both loops ride the carry's breath (same phases): the cooler moves +-0.1 / +-0.5 cm and +-0.5 deg (Show's pivot
+-1.0 cm vertically, since the pivot sits 31 cm off the turn axis), and the fists move with it. Put both in sync group
**`FPArmsBreath`** with the other loops.

Checks on the keyed actions: max per-frame step 0.55 / 0.68 mm, loop seam 0.49 / 0.67 mm, first-last delta 0.0 mm (no
pop). Fists on their rope sockets on every frame (0.0 mm). Re-import: both takes 0-90, 18 bones, baked pose 0.0 mm /
0.0 deg off at frames 0/30/45/60/90, bone scales 1.0 (dev 1e-6), header UnitScaleFactor 1.0. Contact: fingers and
thumbs sit up to 14.8 mm (Open) / 13.1 mm (Show) into the wall behind the rope, hidden under the fist (carry: 15.3).
Elbow bend over the loop: Open >= 51 deg, Show >= 12.7 deg (a straight-armed presenting pose, in reach on every frame).

## Frame 0 in numbers (Unreal, arms component space)
| Pose | `cooler` bone location (cm) | rotation P / Y / R | wrist bones in cooler space (cm) |
|---|---|---|---|
| CarryCooler_Idle (for reference) | (36.2, 0, -51.6) | 18.5 / -180 / 0 | hand_r (3.1, -30.6, 37.2), hand_l (3.1, 30.6, 37.2) |
| Open | (73.2, 0, -44.7) | -55.5 / -180 / 0 | hand_r (3.1, -35.1, 36.2), hand_l (3.1, 35.1, 36.2) |
| Show | (31.3, 0, -42.5) | 64.5 / -180 / 0 | hand_r (-3.1, -35.1, 25.0), hand_l (-3.1, 35.1, 25.0) |

On screen (FP 90 deg, 1920x1080):
- **Open, owner's lid 235:** the lid covers 22 % of the centre-40 % box and the cooler 10 %. Fish, arms: 0 % (the fish
  fill the lower third). With the lid at LidOpenPitch 100 (the fallback), the lid covers 84 % of the box.
- **Show:** the body covers 5.4 % of the box (latch only). Lid and fish 0 % (the lid is out of view at 100 and at 235).
  26 % of each fist is visible, at x 23 / 77 %.
- **Another player at 2.5 m (eye 1.70 m)** sees 22.6k px of fish, 26.9k px of open mouth and 15.4k px of lid (lid at
  100) in a full 1920x1080 frame, with both hands on the handles.

## Unreal import (editor-operator)
Settings as in `SK_FPArms.anim.md` "Unreal import": **Convert Scene Unit OFF**, Convert Scene ON, Force Front X OFF,
uniform scale 1.0. Import both files into `/Game/Art/Characters/FPArms/` as animation only on `SKEL_FPArms`, 30 fps
from the file, frames 0-90, Enable Root Motion off, looping in their players. Don't strip any bone track (`cooler` is
animated). Verify (stop and report if one fails): frame 0 `cooler` at (73.2, 0, -44.7) cm, P -55.5 (Open) and
(31.3, 0, -42.5) cm, P 64.5 (Show) in component space; every bone's local scale 1.0; PIE or a preview with
SM_Cooler_Starter on `cooler` (zero transform): both fists on the rope handles, like `SK_FPArms_cooler_open_fp.png`
and `_show_fp.png`.

## Wiring and Eng follow-ups (unreal-engineer, C++; ABP_FPArms stays thin)
1. **`EFPArmsPose`** gets `CarryCoolerOpen = 6` and `CarryCoolerShow = 7`. Add two Sequence Players to `Blend Poses by
   EFPArmsPose` (`A_FPArms_CarryCooler_Open`, `A_FPArms_CarryCooler_Show`), same sync group `FPArmsBreath` and the same
   Standard / Linear blend. Rule: carrying the cooler and it is open -> `CarryCoolerOpen`; open and showing (bShowing)
   -> `CarryCoolerShow`; else `CarryCooler`.
2. **Blend time** between CarryCooler, CarryCoolerOpen and CarryCoolerShow = DT_Catch `ShowTurnTime` (0.4 s). The lid
   keeps its own `LidOpenTime` (0.25 s), so the lid finishes opening before the turn does.
3. **The turn now comes from the arms.** On the owner's machine set `CarriedOpenRotation` / `CarriedShowRotation` to
   (0,0,0) and `CarriedOpenOffset` / `CarriedShowOffset` to (0,0,0) (LureCatchSettings): the cooler stays on `cooler`
   with a zero relative transform in all three poses. `ThirdPerson*` (other machines) are unchanged.
4. **Owner-only lid pitch 235.** New DT_Catch column **`CarriedLidPitchFP` = 235** (data, a feel value). While the
   carried cooler is open, the OWNER's first-person cooler opens its lid to this pitch in Open and in Show; other
   players keep seeing `LidOpenPitch` (100, replicated). Why: at 100 the lid stands up at the far edge and covers 84 %
   of the centre box in Open; at 235 it folds back behind the cooler (22 %). In Show the lid is out of the owner's view
   at either value, so using 235 in both avoids a 135 deg lid swing on every Open <-> Show turn.
   Fallback without this: the clips work unchanged with lid 100 (`SK_FPArms_cooler_open_lid100_fp.png`).
5. **Keep the fists on the ropes during the blends: a small C++ skeletal-control node** (e.g.
   `FAnimNode_LureCarriedCoolerGrip : FAnimNode_SkeletalControlBase`), placed right after `Blend Poses by EFPArmsPose`.
   Why: `cooler`'s pivot is the cooler's base, 30.6 cm below the handle axis. A plain crossfade lerps that pivot while
   it turns, so the handles swing off the fists: up to **8.2 cm (Carry <-> Open), 3.6 cm (Carry <-> Show), 13.8 cm
   (Open <-> Show)**. The gate A plan (Two Bone IK of the hands to cooler-space targets) fixes Carry <-> Open (0.7 cm)
   but NOT the Show blends (5.6 / 5.2 cm). The Show pose is straight-armed, so the arms clamp at full reach and the
   forearm sinks up to 4 cm into the cooler. The node instead moves the cooler to the fists, then closes the rest:
   - Grip points: `G_r` = `hand_r` CS transform applied to its local grip offset, and the same for `G_l`. Local offset
     = RefPoseCS(hand)^-1 applied to the bind-pose grip point. Bind-pose values in component space: grip `hand_r`
     (54.1, 13.8, -24.1) cm (= `hand_r_rod`'s head), grip `hand_l` (54.1, -13.8, -24.1) cm; wrists `hand_r`
     (48, 17, -21.5), `hand_l` (48, -17, -21.5).
   - Rope sockets in `cooler` space: the right fist holds `Handle_L` (0, -31.5, 30.6), the left fist holds
     `Handle_R` (0, 31.5, 30.6) cm (the cooler faces the player).
   - Step 1, fit the cooler: take the blended `cooler` CS transform C. Turn C's rotation by
     `FQuat::FindBetweenNormals(C.Rot * (Handle_R - Handle_L)).GetSafeNormal(), (G_l - G_r).GetSafeNormal())`. Then
     set its location so the midpoint of the two sockets, (0, 0, 30.6), lands on (G_l + G_r) / 2.
   - Step 2, per arm: `AnimationCore::SolveTwoBoneIK` on upperarm / lowerarm / hand. Effector = the hand's location +
     (its socket on the fitted cooler - its grip point). Joint target = the input elbow (keep the bend plane). No
     stretch. The hand keeps its component-space rotation (as TwoBoneIK with bTakeRotationFromEffectorSpace and
     bMaintainEffectorRelRot off). Output upperarm, lowerarm, hand and `cooler`; children follow.
   - Alpha: 1 while the current and the previous ArmsPose are both cooler poses (CarryCooler / Open / Show), else 0.
     At any authored pose it changes nothing (grips on the sockets, 0.0 mm), so it is safe to leave on.
   - Simulated result (every 10 % of each blend, breath frames 0 and 45): fists on the ropes **0.0 mm** in all three
     pairs, no clamping. The cooler moves up to 8.2 / 3.5 / 13.6 cm from the plain blend and turns about its handles.
     Contact at mid-blend: only fingers and thumbs touch the wall behind the rope, up to 21 mm (the plain blend itself
     has 23; steady poses 13-15). A test idea: blend Open -> Show at alpha 0.5, frame 0 and expect |grip - socket|
     < 1 mm with the node (13.8 cm without).
6. StanceDip (additive on `arms`) still moves arms and cooler together. Nothing else changes.

## Compromises
- Show is a straight-armed presenting pose (elbow bend down to 12.7 deg). 26 % of each fist is visible (art-mgr
  accepted it at gate A2, target ~30 %). The ceiling is set by the tipped front wall and the rope brackets.
- The owner sees a different lid pitch (235) than other players (100). It is cosmetic and owner-only.
- Contact: fingers and thumbs sink 13-15 mm into the wall behind the rope (hidden, as in the carry), and up to 21 mm for
  a few frames mid-blend.

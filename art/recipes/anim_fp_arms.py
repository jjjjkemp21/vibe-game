"""anim_fp_arms: rig + animations for SK_FPArms (T-004), built on the model-artist's mesh recipe sk_fp_arms.py.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fp_arms.py
Exports (art/export/Characters/):
  SK_FPArms.fbx                   skinned mesh + skeleton in the bind (rest) pose, no animation
  A_FPArms_Idle.fbx               armature only, one take  (loop, frames 0-90)
  A_FPArms_HoldRod_Idle.fbx       armature only, one take  (loop, frames 0-90)
  A_FPArms_StanceDip.fbx          armature only, one take  (one-shot, frames 0-8; import as additive)
  A_FPArms_Prone_HoldRod_Idle.fbx armature only, one take  (loop, frames 0-90; prone and still)
  A_FPArms_Prone_TuckRod.fbx      armature only, one take  (loop, frames 0-90; prone and crawling)
  A_FPArms_HoldFish_Idle.fbx      armature only, one take  (loop, frames 0-90; T-030, rod stowed, fish on hand_r_fish)
  A_FPArms_HoldFish_Large_Idle.fbx armature only, one take (loop, frames 0-90; the same hold for a 1.6x fish)
  A_FPArms_CarryCooler_Idle.fbx   armature only, one take  (loop, frames 0-90; T-030, cooler on bone `cooler`)
  A_FPArms_RodAim_<Center|Up|Down|Left|Right|UpLeft|UpRight|DownLeft|DownRight>.fbx
                                  armature only, one take  (single pose, frames 0-1; T-028 aim offset, mesh-space
                                  additive against RodAim_Center = HoldRod_Idle frame 0)
All files are CENTIMETERS (FBX UnitScaleFactor 1.0, bone/mesh/key values in cm, no scale on any node) through
pb.export_skeletal_fbx (art/lib/pipeline_blender.py); the rig itself is authored in meters. RESULT_JSON "fbx_units"
has each file's unit/scale check, "reimport_check" the Blender re-import.
Spec for Unreal: art/export/Characters/SK_FPArms.anim.md
Previews (Saved/AgentLogs/previews/): SK_FPArms_anim.png (contact sheet) and full-size 1920x1080 first-person frames
over the palette backdrops of art/lib/fp_preview.py (tropical day / dusk, lit like the mood boards):
  SK_FPArms_anim_fp.png / _fp_dusk.png           HoldRod_Idle f0 standing
  SK_FPArms_prone_fp.png / _prone_fp_dusk.png    Prone_HoldRod_Idle f0, lying at a dock edge (eye 35 cm up)
  SK_FPArms_prone_hold_gap_fp.png                Prone_HoldRod_Idle, highest frame, under a 60 cm ceiling (eye 45)
  SK_FPArms_prone_tuck_gap_fp.png                Prone_TuckRod in the 60 cm crawl gap (eye 45)
  SK_FPArms_prone_wall_fp.png                    Prone_TuckRod, most forward frame, wall 50 cm in front
  SK_FPArms_prone_blend.png                      Prone_HoldRod_Idle -> Prone_TuckRod crossfade (Unreal-style) + path
  SK_FPArms_prone_clearance.png                  side/top views with the ceiling, floor and wall lines + numbers
  SK_FPArms_rodaim_fp.png                        the 9 rod-aim poses from the FP camera (3x3, as the aim offset grid)
  SK_FPArms_rodaim_upright_fp.png                RodAim_UpRight full size
  SK_FPArms_rodaim_views.png                     simulated aim-offset blends (FP) + side/top/outside views
  SK_FPArms_holdfish_fp.png / SK_FPArms_holdfish.png        HoldFish_Idle with the Bonefish (sm_fish_bonefish) staged
  SK_FPArms_holdfish_large_fp.png                HoldFish_Large_Idle with a 1.6x Bonefish
  SK_FPArms_carrycooler_fp.png / SK_FPArms_carrycooler.png  CarryCooler_Idle with SM_Cooler_Starter (its FBX) staged

T-028 ROD AIM (aim offset): each extreme moves the grip (AIM_POSES) and turns the rod so its tip lands on a chosen
point of the first-person frame (solve_rod_aim), so the rod stays in view by construction; the upper body ('arms' bone)
turns too (aim_pose: yaw from the yaw input only, pitch from the pitch input only). Both hands are solved from the rod
as in HoldRod_Idle. aim_checks() simulates Unreal's mesh-space aim-offset blend over the input grid.
T-030 attach bones (non-deforming, appended after hand_l_crank): hand_r_fish (child of hand_r: where the fish's fishkit
bone Grip goes) and cooler (child of arms: SM_Cooler_Starter's pivot, keyed only in CarryCooler_Idle).

Nothing here edits the mesh: build() from sk_fp_arms.py gives the mesh and its vertex groups; this recipe adds the
armature, two forearm-twist vertex groups split off the lowerarm groups, and the actions. SM_Rod_Basic
(sm_rod_basic.build()) is only staged for previews and clearance checks; it is not part of any export.

Axes: Blender +X forward (= Unreal +X), +Y left (= Unreal -Y), Z up, origin = camera/eye point (see sk_fp_arms.py).

HOW THE MOTION IS MADE (deterministic: a rerun gives the same rig and keys)
Every frame is solved analytically: shoulder offsets, a wrist target and a hand orientation per arm go through a
two-bone IK with a fixed elbow pole; the forearm twist bone takes TWIST_ALPHA of the hand's roll about the forearm;
fingers/thumb get rest-space rotations about their joints. Pose matrices are converted to bone-local transforms and
keyed on every frame (linear). In the rod clips the right hand is solved FROM the rod transform and the left hand FROM
the crank knob, so neither grip can slide.

THE ROD GRIP (why the numbers below; found by searching grip tilt/roll, rod yaw/pitch and elbow poles, then judged
on cross-section renders): the mitten has ONE finger block, so it only closes around a rod that runs within ~30 deg
of its knuckle line. Tilts of 45-60 deg (the diagonal of a real grip) leave the block curled beside the rod. So the rod
sits in the fist's channel tilted GRIP_TILT_DEG (the fingers curl about that same axis). The price, below the bottom
edge of the first-person frame: the right wrist sits in a strong ulnar bend. The FP arms float at camera height, so the
elbow always hangs below the fist; no pose within reach gives a neutral wrist, a closed fist and a clear butt at once.

STANDING HOLD (designer B-S1): hand_r_rod at (43, 21, -23) cm and the rod yawed 4.5 deg left, so the fists sit right
of center and low (the bottom-center stays free for the note box / tension meter) and the reel spool shows between the
fists. The right elbow pole points further out (RIGHT_POLE_HOLD) so the rod butt clears the upper arm (off-screen).

PRONE (designer B-M2 + Jimmy 2026-09-22: you can fish while lying prone; the rod is tucked while crawling)
- A_FPArms_Prone_HoldRod_Idle (prone, still): the standard grip, rod held low and nearly level (11.5 deg up) so its tip
  stays about 8-10 cm above the eye (the 60 cm crawl gap leaves 15 cm with a 45 cm eye), extending forward instead of
  up. The right elbow is tucked under the chest (prone on the elbows), which keeps the off-screen wrist moderate.
- A_FPArms_Prone_TuckRod (prone, crawling): the rod is turned around in the right fist (an ice-pick grip near the butt)
  and runs back along the forearm and the body; both fists peek in at the bottom corners. Everything stays below the
  eye and closer than PRONE_WALL_LIMIT_M, so a crawl-gap ceiling or a wall 50 cm ahead is never cut.
  hand_r_rod ANIMATES in this clip (in every other clip it keeps its rest offset in the fist): the rod is rotated
  TUCK_FLIP_DEG (not 180) about a fixed axis in the fist and slid TUCK_SLIDE_M towards its tip. The 172 deg flip about
  that axis makes Unreal's local-space crossfade (shortest-arc quaternion blend per bone) swing the rod down and
  around the player's RIGHT side, below the eye; an exact 180 deg flip has no preferred direction and swung the tip
  up 1.2 m or across the view in tests. The fist is solved from a virtual exactly-reversed rod (TUCK_DIR_*), so the
  real rod sits 8 deg off the fist's channel, hidden inside the fist.
- The clearance numbers (RESULT_JSON "prone") are measured on the skinned mesh plus the SM_Rod_Basic mesh on every
  frame, with the StanceDip additive on top, and along simulated crossfades between every pair of rod poses.

CAST (T-062, Jimmy A2: the charge must show in the arms, not only the rod): Poser.cast_charge / cast_release build
A_FPArms_Cast_Charge (36 frames; Unreal evaluates it at CastCharge01 * length, frame 0 = HoldRod_Idle frame 0) and
A_FPArms_Cast_Release (24-frame one-shot montage, notify CastRelease at CAST_RELEASE_FRAME, last frame = HoldRod_Idle
frame 0). Each frame is six channels on HoldRod_Idle frame 0 (rod swing about the grip, grip offset, chest pitch,
shoulder reach) plus elbow poles, with both hands solved from the rod (see the CAST_* constants). Not exported yet:
gate A of T-062 (key poses) is under review.
"""
import importlib.util
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Matrix, Quaternion, Vector  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402

import fp_preview  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
RECIPES = REPO / "art" / "recipes"
ASSET = "SK_FPArms"
CATEGORY = "Characters"
EXPORT_DIR = REPO / "art" / "export" / CATEGORY
PREVIEW_DIR = REPO / "Saved" / "AgentLogs" / "previews"
FPS = 30

# ---------------------------------------------------------------------------------------------------------------
# Rig
# ---------------------------------------------------------------------------------------------------------------
ARMS_PIVOT = Vector((-0.10, 0.0, -0.25))  # 'arms' bone: a chest point between the shoulders (dip/sway pivot)
TWIST_ALPHA = 0.6                          # share of the hand's roll about the forearm taken by lowerarm_twist
TWIST_RAMP_M = (0.10, 0.25)                # metres from the elbow along the forearm: twist weight ramps 0 -> 1
SMALL_BONE = 0.05
# hand_r_rod rest frame inside the right fist, at the mesh's grip point: rod tip axis = the knuckle line (uh) tilted
# GRIP_TILT_DEG towards the fingers (th); rod up (+Z; the reel hangs at -Z) = the back of the hand (vh) rolled
# GRIP_ROLL_DEG about the rod axis (positive towards the wrist).
GRIP_TILT_DEG = 30.0
GRIP_ROLL_DEG = 50.0
# Left fist on the crank knob (SM_Rod_Basic socket CrankKnob): the fist's grip channel (knuckle line tilted
# KNOB_TILT_DEG) lies along the knob axis, pointing at the reel; the knob sits KNOB_IN_FIST_M from the channel center
# towards the thumb; KNOB_ROLL_DEG turns the fist about the knob axis.
CRANK_KNOB_ROD = Vector((0.0569, 0.0495, -0.1011))
KNOB_TILT_DEG = 30.0
KNOB_IN_FIST_M = 0.035
KNOB_ROLL_DEG = -15.0

PARENT = {"root": None, "arms": "root", "hand_r_rod": "hand_r", "hand_l_crank": "hand_r_rod",
          "hand_r_fish": "hand_r", "cooler": "arms"}
for _s in ("l", "r"):
    PARENT.update({"upperarm_" + _s: "arms", "lowerarm_" + _s: "upperarm_" + _s,
                   "lowerarm_twist_" + _s: "lowerarm_" + _s, "hand_" + _s: "lowerarm_" + _s,
                   "thumb_" + _s: "hand_" + _s, "fingers_" + _s: "hand_" + _s})

# ---------------------------------------------------------------------------------------------------------------
# Actions (30 fps). Loops: the last frame equals the first (Unreal plays 0..N and wraps without a hitch).
# ---------------------------------------------------------------------------------------------------------------
LOOP_FRAMES = 90                    # 3.0 s = one slow breath (all loops share it: sync group FPArmsBreath)
DIP_FRAMES = 8                      # 0.267 s
AIM_FRAMES = 1                      # aim-offset poses: frames 0-1, two identical keys (Unreal samples frame 0)
# Rod aim grid (T-028): (yaw input, pitch input) -> name. Yaw +1 = tip to the RIGHT, pitch +1 = Up (pulled back/high).
AIM_GRID = {(0, 0): "Center", (0, 1): "Up", (0, -1): "Down", (-1, 0): "Left", (1, 0): "Right",
            (-1, 1): "UpLeft", (1, 1): "UpRight", (-1, -1): "DownLeft", (1, -1): "DownRight"}
AIM_ACTIONS = {k: "A_FPArms_RodAim_" + v for k, v in AIM_GRID.items()}
ACTIONS = [("A_FPArms_Idle", 0, LOOP_FRAMES), ("A_FPArms_HoldRod_Idle", 0, LOOP_FRAMES),
           ("A_FPArms_StanceDip", 0, DIP_FRAMES), ("A_FPArms_Prone_HoldRod_Idle", 0, LOOP_FRAMES),
           ("A_FPArms_Prone_TuckRod", 0, LOOP_FRAMES),
           ("A_FPArms_HoldFish_Idle", 0, LOOP_FRAMES), ("A_FPArms_HoldFish_Large_Idle", 0, LOOP_FRAMES),
           ("A_FPArms_CarryCooler_Idle", 0, LOOP_FRAMES)]
ACTIONS += [(AIM_ACTIONS[k], 0, AIM_FRAMES) for k in ((0, 0), (0, 1), (0, -1), (-1, 0), (1, 0),
                                                        (-1, 1), (1, 1), (-1, -1), (1, -1))]

# HoldRod_Idle (standing / crouched): the rod (= hand_r_rod) in camera space: grip point, tip raised ROD_PITCH,
# turned ROD_YAW left. Designer B-S1: frame 0 puts hand_r_rod at (43, 21, -23) cm in Unreal.
ROD_GRIP = Vector((0.43, -0.21, -0.2266))
ROD_PITCH_DEG = 36.0
ROD_YAW_DEG = 4.5
ROD_ROLL_DEG = 0.0
RIGHT_SHOULDER_HOLD = Vector((0.02, 0.0, 0.0))
RIGHT_POLE_HOLD = Vector((0.0, -0.6, -1.0))        # right elbow down and out (keeps the butt off the upper arm)
LEFT_SHOULDER_HOLD = Vector((0.07, -0.06, 0.0))   # protraction: the left arm reaches across to the crank
LEFT_POLE_HOLD = Vector((0.0, 1.0, -0.6))          # left elbow out to the side (small forearm twist)
GRIP_CURL_DEG = 86.0                # fingers closed around the rod grip
KNOB_CURL_DEG = 80.0
HOLD_SWAY = (1.2, 0.8, Vector((0.003, 0.002, 0.007)))   # rod pitch deg, yaw deg, grip offsets m (per breath)

# Prone_HoldRod_Idle (prone, still): standard grip, rod low and nearly level, elbows tucked (prone on the elbows).
PRONE_ROD_GRIP = Vector((0.43, -0.21, -0.26))
PRONE_ROD_PITCH_DEG = 11.5
PRONE_ROD_YAW_DEG = -2.0            # a little right: the tip stays off the crosshair
PRONE_SWAY = (0.6, 0.5, Vector((0.002, 0.0015, 0.004)))  # steadier than standing: the elbows rest on the ground
PRONE_RIGHT_SHOULDER = Vector((0.02, 0.0, 0.04))
PRONE_LEFT_SHOULDER = Vector((0.07, -0.06, 0.04))
PRONE_RIGHT_POLE_AZ_DEG = 220.0      # elbow pole azimuth (see pole_dir): down and in, under the chest
PRONE_LEFT_POLE = Vector((0.0, 1.0, -0.6))

# Prone_TuckRod (prone, crawling): the right fist holds the rod near its butt in an ice-pick grip, rod running back.
TUCK_HELD = Vector((0.35, -0.17, -0.21))  # fist grip point (camera space); the fist holds rod-local x = -TUCK_SLIDE_M
TUCK_DIR_YAW_DEG = 215.0             # virtual reversed rod the fist is solved from: back, 35 deg out to the right,
TUCK_DIR_PITCH_DEG = -8.0            # 8 deg down along the body
TUCK_HAND_ROLL_DEG = 105.0           # the fist's roll about that virtual rod
TUCK_SLIDE_M = 0.24                  # grip slid from the pivot (rear-grip center) to near the butt cap
TUCK_FLIP_AXIS_DEG = 120.0           # flip axis in the rod's YZ plane: cos(a) * rod Z + sin(a) * rod Y
TUCK_FLIP_DEG = 172.0                # < 180: gives Unreal's shortest-arc blend one direction (down, around the right)
TUCK_RIGHT_POLE_AZ_DEG = 320.0       # right elbow out to the side on the ground
TUCK_SHOULDER = Vector((0.02, 0.0, 0.0))
TUCK_LEFT_OFFSET = Vector((-0.02, 0.03, -0.06))   # left fist: mirror of the right, lower and further out
TUCK_LEFT_CURL_DEG = 70.0

# Rod aim poses (T-028 aim offset, additive against HoldRod_Idle; Center = HoldRod_Idle frame 0 exactly). Each extreme
# moves the grip (cm, camera space: x forward, y LEFT, z up; plus the same shift on both shoulders) and turns the rod
# so its tip lands on a chosen point of the 90 deg first-person frame (% from the top-left corner): the rod stays in
# view by construction. Center's tip is at (52.7, 14.1). The pitch/yaw are solved (RESULT_JSON rod_aim).
# Designer review 2026-09-23: Down row grips 5 cm higher (a full fist in frame), Right column grips 9 cm towards the
# center, the rod rolled in the Right column and in Down so the reel swings out from behind the fists (AIM_ROLL).
# T-033b (designer review 2026-09-24): Right column grips 3-3.5 cm further left (fists ~4.5 % of the width more central,
# right knuckles clear of the frame edge).
ROD_TIP_M = 1.648                    # SM_Rod_Basic line tip on the rod's X axis
AIM_POSES = {  # name: (grip offset cm, shoulder offset cm, tip target %)
    "Up": ((-12.0, 2.0, 6.0), (-2.0, 0.0, 1.0), (56.0, 4.5)),        # pulled back and high: strong tension
    "Down": ((7.0, 0.0, 2.0), (2.0, 0.0, -1.0), (50.0, 62.0)),        # dipped towards the water
    "Left": ((2.0, 6.0, 0.0), (0.0, 1.0, 0.0), (22.0, 20.0)),          # tip swung to the left
    "Right": ((-3.5, 2.5, 2.0), (0.0, -1.0, 0.0), (96.0, 40.0)),     # tip swung to the right
    "UpLeft": ((-8.0, 5.0, 7.0), (-2.0, 1.0, 1.0), (25.0, 6.0)),
    "UpRight": ((-11.5, 6.5, 6.0), (-2.0, -1.0, 1.0), (92.0, 10.0)),
    "DownLeft": ((3.0, 6.0, 2.0), (2.0, 1.0, -1.0), (24.0, 56.0)),
    "DownRight": ((-2.0, 2.0, 1.0), (2.0, -1.0, -1.0), (95.0, 62.0)),
}
AIM_ROLL = {"Right": 18.0, "UpRight": 15.0, "DownRight": 23.5, "Down": 15.0}   # rod roll (deg): swings the reel out from
                                                                     # behind the right fist towards the eye
# T-033 (playtest A2: the hands clipped into one blob at a hard-right swing). The Right column is re-solved so each
# forearm turns WITH the rod instead of the wrist doing it: the upper body turns 35 deg right (AIM_BODY_YAW, fixed for
# the whole column so the 'arms' blend stays separable), the right elbow swings in under the rod and the left elbow
# follows the crank (AIM_ELBOW_SWIVEL: (right, left) degrees about each shoulder-wrist axis, + = counter-clockwise
# seen from the wrist). The grip, roll and swivels come from a search (wrist bend/twist vs Center, arm and rod
# clearances, both fists in frame and apart, reel spool not hidden, sleeves below the frame; T-033 in SK_FPArms.anim.md).
AIM_BODY_YAW = {1: -35.0}            # yaw input sign -> body yaw (deg); a side not listed is searched (aim_pose)
AIM_ELBOW_SWIVEL = {"Right": (22.0, 12.0), "UpRight": (62.0, 14.0), "DownRight": (34.0, 8.5)}

# HoldFish (T-030, designer review 2026-09-23: two hands). Rod stowed. The right hand cradles the fish under the
# gills (the throat lies across the palm along the fist channel, head out of the thumb side, fingers wrapped up the
# far flank, thumb on the near flank); the left palm supports the belly ahead of the anal fin, lower left. Both contact
# points sit on the reference Bonefish's belly line. The attach bone hand_r_fish is where the fish's fishkit bone Grip
# goes (chest center on the spine line, still in every fish clip): X = fish forward (head), Z = fish up, Y = fish left.
FISH_TILT_DEG = 30.0                 # fish axis across the palms = the fist channel (knuckle line tilted to the fingers)
FISH_THROAT = Vector((0.076, 0.0, -0.051))   # right-palm contact, fish space from Grip (Bonefish: x 17 cm, under the gills)
FISH_BELLY = Vector((-0.150, 0.0, -0.030))   # left-palm contact, fish space from Grip (Bonefish: x -5.6 cm, belly)
FISH_CONTACT_ROLL_DEG = -35.0        # both contacts turned about the fish's spine towards its near (right) flank, so
                                     # the palms cup the near-lower belly and show in front of the fish
FISH_PALM_M = 0.009                  # palm surface above the rod grip point along the palm normal (sk_fp_arms)
FISH_SINK_M = 0.006                  # the belly rests this far into the palms (weight; the fingers wrap)
# Two poses, blended by fish size in Unreal (HoldFishSize alpha = (scale - 1) / (1.6 - 1), clamped): the reference
# size and a 1.6x trophy held further round (tail away) and head-up so it stays near 70 % of the screen width.
# pos = hand_r_fish (the fish's Grip at REFERENCE size) at frame 0, camera space; yaw = head direction (-90 = right).
FISH_POSES = {
    "A_FPArms_HoldFish_Idle": dict(scale=1.0, pos=Vector((0.47, -0.08, -0.165)), yaw=-80.0, pitch=6.0, roll=-15.0),
    "A_FPArms_HoldFish_Large_Idle": dict(scale=1.6, pos=Vector((0.44, -0.12, -0.172)), yaw=-125.0, pitch=22.0,
                                         roll=-15.0),
}
FISH_SWAY = (1.2, 0.8, Vector((0.003, 0.002, 0.007)))   # pitch deg, yaw deg, offsets m (per breath)
FISH_CURL_DEG = 72.0                 # right fingers wrapped round the throat
FISH_LEFT_CURL_DEG = 28.0            # left palm open under the belly
FISH_LEFT_YAW_DEG = -30.0            # left palm turned about its normal (least wrist bend in a 30 deg search)
FISH_RIGHT_SHOULDER = Vector((0.02, 0.0, 0.0))
FISH_RIGHT_POLE_AZ_DEG = 250.0       # right elbow down and a little in (forearm supinated, palm up)
FISH_LEFT_SHOULDER = Vector((0.03, -0.02, 0.0))
FISH_LEFT_POLE_AZ_DEG = 200.0        # left elbow pole (mirrored azimuth, see pole_dir): in, under the chest
THUMB_FISH = [("th", -25.0), ("vh", 10.0)]
THUMB_SUPPORT = [("th", -6.0), ("vh", 4.0)]
FISH_SCALES = (0.8, 1.0, 1.3, 1.6)    # checked sizes (Weight/ReferenceWeight)^(1/3); RESULT_JSON hold_fish.sizes

# CarryCooler (T-030, lead decision after the designer review 2026-09-23: two-handed low carry). Both fists on the
# rope handles of SM_Cooler_Starter (sockets Handle_L/Handle_R at the grip centers), the box low in front with its
# FRONT (latch, sticker) facing the player, tilted top-away so the front wall and the lid edge face the eye; the fists
# peek in at the bottom corners, the lid edge stays in the lower third. The attach bone `cooler` is the cooler's pivot
# (bottom center) with the cooler's own axes, i.e. the carry frame turned 180 deg about Z.
COOLER_HANDLE_L = Vector((0.0, 0.315, 0.306))    # socket Handle_L in cooler space (Blender +Y; rim handles, 7e4ba79); Handle_R = mirror
COOLER_HANDLES_POS = Vector((0.46, 0.0, -0.222))  # midpoint between the two handle grips at frame 0, camera space
COOLER_TILT_DEG = 19.0              # about the handle axis (+ = top away from the eye); - = lid tipped slightly towards the eye
COOLER_SWAY = (0.6, Vector((0.002, 0.0, 0.007)))  # tilt deg, offsets m (per breath)
HANDLE_DIR = 1.0                     # fist channel along the rope: +1 = the carry frame's forward (+X)
HANDLE_ROLL_DEG = 0.0                # roll of the fist about the rope (least wall contact in a 15 deg search)
CARRY_SHOULDER = Vector((0.0, 0.0, -0.01))
CARRY_POLE = Vector((-0.1, -1.0, 0.05))         # right elbow out wide, level (left mirrored): forearms come down outside the box

# Idle (empty hands): wrist offsets from the rest wrists (right side; the left is mirrored) and hand deltas.
IDLE_WRIST_OFFSET = Vector((-0.02, -0.01, -0.012))
IDLE_HAND_ROLL_DEG = 15.0           # about the hand's forward axis: palms turn towards each other
IDLE_HAND_FLEX_DEG = 8.0            # relaxed droop at the wrist
IDLE_CURL_DEG = 14.0                # extra finger curl over the rest pose (rest already has 18)

# Thumb shapes: rotations about the right hand's rest axes (th forward, uh thumb side, vh back), applied in order.
THUMB_RELAXED = [("th", -8.0), ("vh", 6.0)]
THUMB_GRIP = [("th", -35.0), ("vh", 10.0)]
THUMB_KNOB = [("th", -30.0), ("vh", 5.0)]

# StanceDip ('arms' bone only, additive): per-frame drop (cm), pull back (cm), pitch forward (deg) about ARMS_PIVOT.
# Ease in (f0-2), bottom at f3 (the "weight" beat), recover with a small overshoot at f6, settle at rest on f8.
DIP_Z_CM = [0.0, -0.9, -2.5, -3.5, -2.9, -1.3, 0.3, 0.2, 0.0]
DIP_X_CM = [0.0, -0.2, -0.5, -0.7, -0.6, -0.3, 0.1, 0.05, 0.0]
DIP_PITCH_DEG = [0.0, 0.7, 2.1, 3.0, 2.5, 1.0, -0.4, -0.2, 0.0]

# Prone clearance targets (lead brief 2026-09-22; DT_Movement Prone EyeHeight = 35 cm in lane eng1)
PRONE_EYE_M = (0.35, 0.45)           # eye height above the floor: DT_Movement value, and the brief's upper bound
CRAWL_GAP_M = 0.60                   # standard crawl gap (docs/specs/movement-rules.md A12)
PRONE_CEILING_LIMIT_M = CRAWL_GAP_M - PRONE_EYE_M[1]    # 0.15: nothing may rise higher above the eye
PRONE_WALL_M = 0.50                  # wall test distance in front of the eye
PRONE_WALL_LIMIT_M = 0.47            # keep 3 cm for the crawl bob (BobForward 1.2 cm) and the dip
FIRST_PERSON_SCALE = 0.6             # Unreal FirstPersonScale from the spec (scales FP primitives towards the eye)

M3 = Matrix.Diagonal((1.0, -1.0, 1.0))  # mirror across Y = 0 (3x3)
M4 = Matrix.Diagonal((1.0, -1.0, 1.0, 1.0))
SIDES_R = None                          # the right Side, set in main() (authoring frame for mirrored rotations)


def mir(v):
    return Vector((v.x, -v.y, v.z))


def rot3(axis, deg):
    return Matrix.Rotation(math.radians(deg), 3, Vector(axis).normalized())


def mat4(R3, t):
    m = R3.to_4x4()
    m.translation = Vector(t)
    return m


def about_point(R3, p):
    """4x4: rotate by R3 about point p."""
    p = Vector(p)
    return Matrix.Translation(p) @ R3.to_4x4() @ Matrix.Translation(-p)


def basis_yz(y, z):
    """3x3 rotation with columns (x, y, z): y = the given direction, z = `z` made perpendicular, x = y cross z."""
    y = Vector(y).normalized()
    z = Vector(z) - y * Vector(z).dot(y)
    z.normalize()
    x = y.cross(z)
    return Matrix((x, y, z)).transposed()


def tilted(u, t, deg):
    return (u * math.cos(math.radians(deg)) + t * math.sin(math.radians(deg))).normalized()


def smoothstep(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


def pole_dir(az_deg):
    """Elbow pole direction for the RIGHT arm from an azimuth around the forward axis: 0 = out to the right (-Y),
    90 = up, 180 = in (towards the midline), 270 = down; plus a matching forward/back lean."""
    a = math.radians(az_deg)
    return Vector((0.3 * math.cos(a + math.pi / 2.0), -math.cos(a), math.sin(a)))


def direction(yaw_deg, pitch_deg):
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    return Vector((math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p)))


def load_recipe(stem):
    spec = importlib.util.spec_from_file_location(stem, str(RECIPES / (stem + ".py")))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---------------------------------------------------------------------------------------------------------------
# Rest skeleton
# ---------------------------------------------------------------------------------------------------------------
class Side:
    """Rest-pose landmarks of one arm (the mesh recipe's bone guides, metres, camera space)."""

    def __init__(self, info, s):
        g = info["bone_guides_m"]

        def v(bone, key):
            return Vector(g[bone + "_" + s][key])

        self.s = s
        self.sign = 1.0 if s == "l" else -1.0          # +Y = left
        self.S, self.E, self.W = v("upperarm", "head"), v("upperarm", "tail"), v("lowerarm", "tail")
        self.K, self.F = v("fingers", "head"), v("fingers", "tail")
        self.TH, self.TT = v("thumb", "head"), v("thumb", "tail")
        fr = info["hand_frame_r"]
        th, uh, vh = (Vector(fr[k]).normalized() for k in ("hand_forward", "hand_thumb_side", "hand_back"))
        if s == "l":
            th, uh, vh = mir(th), mir(uh), mir(vh)
        self.th, self.uh, self.vh = th, uh, vh
        self.grip = Vector(info["rod_grip_point_m"][s])
        self.n = (self.E - self.S).cross(self.W - self.E).normalized()   # elbow hinge axis
        self.L1 = (self.E - self.S).length
        self.L2 = (self.W - self.E).length
        pole = Vector((0.0, -0.35, -1.0))                  # sk_fp_arms ELBOW_POLE (right arm): down and out
        self.pole = pole if s == "r" else mir(pole)

    def R(self, R_right):
        """A rotation authored for the right side, mirrored for the left side."""
        return R_right if self.s == "r" else M3 @ R_right @ M3


def rod_rest_matrix(sd, reverse=False, slide=0.0, extra_roll=0.0):
    """Rod frame in the right fist (armature rest space): X = rod tip direction, Y = rod left, Z = rod up (reel at -Z),
    origin = SM_Rod_Basic's pivot. Default = the hand_r_rod rest frame (the standard grip): the rod's pivot at the
    fist's grip point, so the rod attached with a zero transform sits in the fist.
    reverse: the rod turned around in the channel (tip out of the pinky side); slide: the fist holds rod-local
    x = -slide (the pivot moves `slide` towards the tip); extra_roll: rotation about the rod axis (degrees)."""
    X = tilted(sd.uh, sd.th, GRIP_TILT_DEG)
    Z = sd.vh * math.cos(math.radians(GRIP_ROLL_DEG)) - sd.th * math.sin(math.radians(GRIP_ROLL_DEG))
    Z = (Z - X * Z.dot(X)).normalized()
    if reverse:
        X = -X
    if extra_roll:
        Z = rot3(X, extra_roll) @ Z
    Y = Z.cross(X)
    return mat4(Matrix((X, Y, Z)).transposed(), sd.grip + X * slide)


def tuck_rod_rest(sd):
    """The tuck grip (rest space): the standard grip rotated TUCK_FLIP_DEG about an axis in the rod's YZ plane (through
    the grip point), then slid so the fist holds rod-local x = -TUCK_SLIDE_M (near the butt cap)."""
    R0 = rod_rest_matrix(sd)
    Y, Z = R0.col[1].xyz, R0.col[2].xyz
    a = math.radians(TUCK_FLIP_AXIS_DEG)
    K = (Z * math.cos(a) + Y * math.sin(a)).normalized()
    Rf = rot3(K, TUCK_FLIP_DEG) @ R0.to_3x3()
    return mat4(Rf, sd.grip + Rf.col[0] * TUCK_SLIDE_M)


def build_armature(sides, crank_rest=None):
    """Create the armature (or add hand_l_crank). Bone Y runs along the bone; rolls: arm bones' Z = elbow hinge axis,
    hand/fingers/thumb Z = back of the hand. root, arms, hand_r_rod, hand_l_crank do not deform."""
    arm_obj = bpy.data.objects.get("Armature")
    if arm_obj is None:
        data = bpy.data.armatures.new("SKEL_FPArms")
        data.display_type = "STICK"
        arm_obj = bpy.data.objects.new("Armature", data)   # Unreal's FBX import drops a Blender node named Armature
        bpy.context.scene.collection.objects.link(arm_obj)
    pb.select_only([arm_obj])
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_obj.data.edit_bones

    def add(name, head, tail, parent, roll_z=None, deform=True, matrix=None):
        b = eb.new(name)
        b.head, b.tail = Vector(head), Vector(tail)
        if roll_z is not None:
            b.align_roll(Vector(roll_z))
        if matrix is not None:
            b.matrix = matrix
        b.parent = eb[parent] if parent else None
        b.use_connect = False
        b.use_deform = deform
        return b

    if crank_rest is None:
        add("root", (0, 0, 0), (0, 0.1, 0), None, deform=False)                       # identity frame
        add("arms", ARMS_PIVOT, ARMS_PIVOT + Vector((0, 0.1, 0)), "root", deform=False)  # identity frame
        for s in ("l", "r"):
            sd = sides[s]
            add("upperarm_" + s, sd.S, sd.E, "arms", sd.n)
            add("lowerarm_" + s, sd.E, sd.W, "upperarm_" + s, sd.n)
            add("lowerarm_twist_" + s, sd.E.lerp(sd.W, 0.5), sd.W, "lowerarm_" + s, sd.n)
            add("hand_" + s, sd.W, sd.K, "lowerarm_" + s, sd.vh)
            add("fingers_" + s, sd.K, sd.F, "hand_" + s, sd.vh)
            add("thumb_" + s, sd.TH, sd.TT, "hand_" + s, sd.vh)
        rr = rod_rest_matrix(sides["r"])
        add("hand_r_rod", rr.translation, rr.translation + rr.col[1].xyz * SMALL_BONE, "hand_r", deform=False,
            matrix=rr)
    elif isinstance(crank_rest, dict):
        # T-030 attach bones, appended after the T-004 bones (the existing bone order stays as it was)
        for name, M in crank_rest.items():
            add(name, M.translation, M.translation + M.col[1].xyz * SMALL_BONE, PARENT[name], deform=False, matrix=M)
    else:
        t = crank_rest.translation
        add("hand_l_crank", t, t + crank_rest.col[1].xyz * SMALL_BONE, "hand_r_rod", deform=False, matrix=crank_rest)
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm_obj


def palm_cradle(sd):
    """A palm-up contact frame in the hand (armature rest space): X = the fist channel (knuckle line tilted towards the
    fingers, pointing out of the thumb side), Z = out of the palm, origin on the palm surface (minus FISH_SINK_M)."""
    X = tilted(sd.uh, sd.th, FISH_TILT_DEG)
    Z = -sd.vh
    Z = (Z - X * Z.dot(X)).normalized()
    Y = Z.cross(X)
    return mat4(Matrix((X, Y, Z)).transposed(), sd.grip + Z * (FISH_PALM_M - FISH_SINK_M))


def fish_rest_matrix(sd):
    """hand_r_fish rest frame (armature rest space): the fish's Grip frame when its throat contact (FISH_THROAT) lies
    in the right palm cradle, the fish axis along the channel (head out of the thumb side), dorsal out of the palm."""
    return palm_cradle(sd) @ Matrix.Translation(-FISH_THROAT) @ rot3((1, 0, 0), -FISH_CONTACT_ROLL_DEG).to_4x4()


def rest_matrices(arm_obj):
    return {b.name: b.matrix_local.copy() for b in arm_obj.data.bones}


# ---------------------------------------------------------------------------------------------------------------
# Skinning
# ---------------------------------------------------------------------------------------------------------------
def add_twist_weights(mesh_obj, sides):
    """Split each lowerarm group into lowerarm + lowerarm_twist along the forearm (0 at the cuff, 1 at the wrist),
    so rolling the hand spreads along the forearm instead of pinching the wrist (no candy wrapper)."""
    for s, sd in sides.items():
        lo = mesh_obj.vertex_groups["lowerarm_" + s]
        tw = mesh_obj.vertex_groups.new(name="lowerarm_twist_" + s)
        dn = (sd.W - sd.E).normalized()
        for v in mesh_obj.data.vertices:
            w_lo = next((g.weight for g in v.groups if g.group == lo.index), 0.0)
            if w_lo <= 0.0:
                continue
            f = smoothstep(((v.co - sd.E).dot(dn) - TWIST_RAMP_M[0]) / (TWIST_RAMP_M[1] - TWIST_RAMP_M[0]))
            if f <= 0.0:
                continue
            if f >= 1.0:
                lo.remove([v.index])
            else:
                lo.add([v.index], w_lo * (1.0 - f), "REPLACE")
            tw.add([v.index], w_lo * f, "REPLACE")


def weight_stats(mesh_obj):
    worst, unweighted, maxinf = 0.0, 0, 0
    for v in mesh_obj.data.vertices:
        tot = sum(g.weight for g in v.groups if g.weight > 0)
        maxinf = max(maxinf, sum(1 for g in v.groups if g.weight > 1e-4))
        if tot <= 1e-6:
            unweighted += 1
        worst = max(worst, abs(tot - 1.0))
    return {"verts": len(mesh_obj.data.vertices), "unweighted": unweighted, "max_weight_sum_error": round(worst, 5),
            "max_influences": maxinf, "groups": sorted(g.name for g in mesh_obj.vertex_groups)}


def skin(mesh_obj, arm_obj):
    mod = mesh_obj.modifiers.new("Armature", "ARMATURE")
    mod.object = arm_obj
    mod.use_vertex_groups = True
    mod.use_bone_envelopes = False
    mod.use_deform_preserve_volume = False   # plain linear skinning, as in Unreal


# ---------------------------------------------------------------------------------------------------------------
# Pose solving (armature space = world: the armature object stays at the identity)
# ---------------------------------------------------------------------------------------------------------------
class ArmTarget:
    """What one arm does in a frame. R_hand: world rotation of the hand BONE matrix. curl: extra finger curl (deg)
    about the knuckle line tilted curl_tilt towards the fingers. thumb: rest-space 3x3 for the RIGHT thumb."""

    def __init__(self, wrist, R_hand, shoulder_off=None, curl=0.0, curl_tilt=0.0, thumb=None, pole=None):
        self.wrist = Vector(wrist)
        self.R_hand = R_hand
        self.shoulder_off = Vector(shoulder_off) if shoulder_off is not None else Vector()
        self.curl = curl
        self.curl_tilt = curl_tilt
        self.thumb = thumb if thumb is not None else Matrix.Identity(3)
        self.pole = pole


def ik_elbow(S, W, L1, L2, pole):
    d = W - S
    dist = d.length
    dn = d / dist
    reach = (L1 + L2) * 0.9995
    if dist > reach:
        raise RuntimeError("wrist target out of reach: %.4f m > %.4f m" % (dist, reach))
    a = (dist * dist + L1 * L1 - L2 * L2) / (2.0 * dist)
    h = math.sqrt(max(0.0, L1 * L1 - a * a))
    pp = (pole - dn * pole.dot(dn)).normalized()
    return S + dn * a + pp * h


def lowerarm_rotation(sd, S_new, W_new, pole):
    """(elbow, Q1, Q2): the IK elbow and the upper/lower arm delta rotations for a shoulder and wrist."""
    E_new = ik_elbow(S_new, W_new, sd.L1, sd.L2, pole)
    n_new = (E_new - S_new).cross(W_new - E_new).normalized()
    Q1 = basis_yz(E_new - S_new, n_new) @ basis_yz(sd.E - sd.S, sd.n).transposed()
    Q2 = basis_yz(W_new - E_new, n_new) @ basis_yz(sd.W - sd.E, sd.n).transposed()
    return E_new, Q1, Q2


def solve_arm(sd, B, P, target, arms_delta):
    """Fill P (pose matrices, armature space) for one arm. Shoulder = rest shoulder carried by the 'arms' bone +
    target.shoulder_off; the wrist target and the hand rotation are in world space. Returns wrist metrics."""
    s = sd.s
    S_new = arms_delta @ sd.S + target.shoulder_off
    W_new = target.wrist
    E_new, Q1, Q2 = lowerarm_rotation(sd, S_new, W_new, target.pole if target.pole is not None else sd.pole)
    P["upperarm_" + s] = mat4(Q1 @ B["upperarm_" + s].to_3x3(), S_new)
    P["lowerarm_" + s] = mat4(Q2 @ B["lowerarm_" + s].to_3x3(), E_new)
    P["hand_" + s] = mat4(target.R_hand, W_new)
    # forearm twist: TWIST_ALPHA of the hand's roll about the forearm axis, relative to "carried by the lowerarm"
    q = (target.R_hand @ (Q2 @ B["hand_" + s].to_3x3()).transposed()).to_quaternion()
    axis = (W_new - E_new).normalized()
    twist = 2.0 * math.atan2(Vector((q.x, q.y, q.z)).dot(axis), q.w)
    twist = (twist + math.pi) % (2.0 * math.pi) - math.pi
    R_tw = Matrix.Rotation(TWIST_ALPHA * twist, 3, axis)
    P["lowerarm_twist_" + s] = mat4(R_tw @ Q2 @ B["lowerarm_twist_" + s].to_3x3(), E_new.lerp(W_new, 0.5))
    # fingers / thumb: rest-space rotations about their joints, carried by the hand
    D_hand = P["hand_" + s] @ B["hand_" + s].inverted()
    R_curl = sd.R(rot3(tilted(SIDES_R.uh, SIDES_R.th, target.curl_tilt), target.curl))
    P["fingers_" + s] = D_hand @ about_point(R_curl, sd.K) @ B["fingers_" + s]
    P["thumb_" + s] = D_hand @ about_point(sd.R(target.thumb), sd.TH) @ B["thumb_" + s]
    # wrist metrics in the posed hand frame
    Rr = target.R_hand @ B["hand_" + s].to_3x3().transposed()
    th, uh, vh = Rr @ sd.th, Rr @ sd.uh, Rr @ sd.vh
    flex = math.degrees(math.atan2(-axis.dot(vh), axis.dot(th)))
    dev = math.degrees(math.atan2(axis.dot(uh), axis.dot(th)))
    elbow = math.degrees((E_new - S_new).angle(W_new - E_new))
    return {"twist_deg": round(math.degrees(twist), 1), "wrist_flex_deg": round(flex, 1),
            "wrist_dev_deg": round(dev, 1), "elbow_bend_deg": round(elbow, 1), "elbow_z_m": round(E_new.z, 3)}


def full_pose(B, sides, arms_M, targets, rod_rest=None, cooler=None):
    """Pose matrices for every bone. arms_M: pose matrix of the 'arms' bone. targets: {'l': ArmTarget, 'r': ...}.
    rod_rest: the rod's frame in the right fist in REST space (default: the hand_r_rod rest = the standard grip); the
    hand_r_rod pose keeps that relation to the posed hand. cooler: the cooler bone's pose (default: its rest carried by
    the 'arms' bone; it is keyed only in CarryCooler_Idle)."""
    P = {"root": B["root"].copy(), "arms": arms_M}
    arms_delta = arms_M @ B["arms"].inverted()
    metrics = {s: solve_arm(sides[s], B, P, targets[s], arms_delta) for s in ("l", "r")}
    P["hand_r_rod"] = P["hand_r"] @ B["hand_r"].inverted() @ (B["hand_r_rod"] if rod_rest is None else rod_rest)
    if "hand_l_crank" in B:
        P["hand_l_crank"] = P["hand_r_rod"] @ B["hand_r_rod"].inverted() @ B["hand_l_crank"]
    if "hand_r_fish" in B:
        P["hand_r_fish"] = P["hand_r"] @ B["hand_r"].inverted() @ B["hand_r_fish"]
    if "cooler" in B:
        P["cooler"] = cooler if cooler is not None else arms_delta @ B["cooler"]
    return P, metrics


def pose_to_basis(B, P):
    out = {}
    for name in B:
        par = PARENT[name]
        rel = B[name] if par is None else B[par].inverted() @ B[name]
        Ppar = Matrix.Identity(4) if par is None else P[par]
        out[name] = rel.inverted() @ Ppar.inverted() @ P[name]
    return out


def apply_basis(arm_obj, basis, prev_q=None):
    for name, L in basis.items():
        pbone = arm_obj.pose.bones[name]
        pbone.rotation_mode = "QUATERNION"
        loc, q, _sc = L.decompose()
        if prev_q is not None and name in prev_q and prev_q[name].dot(q) < 0.0:
            q.negate()
        pbone.location = loc
        pbone.rotation_quaternion = q
        pbone.scale = (1.0, 1.0, 1.0)
        if prev_q is not None:
            prev_q[name] = q.copy()


def rest_pose(arm_obj):
    for pbone in arm_obj.pose.bones:
        pbone.rotation_mode = "QUATERNION"
        pbone.location = (0.0, 0.0, 0.0)
        pbone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pbone.scale = (1.0, 1.0, 1.0)


def blend_basis(ba, bb, alpha):
    """Unreal-style local-space crossfade of two poses (bone-local transforms): translation lerp, rotation nlerp on
    the shortest arc (FTransform::AccumulateWithShortestRotation + normalize). Blender's pose basis differs from the
    parent-relative transform by a constant rest offset, which commutes with this blend."""
    out = {}
    for n in ba:
        la, qa, _ = ba[n].decompose()
        lb, qb, _ = bb[n].decompose()
        if qa.dot(qb) < 0.0:
            qb.negate()
        q = qa.copy()
        q.w, q.x, q.y, q.z = (qa.w + (qb.w - qa.w) * alpha, qa.x + (qb.x - qa.x) * alpha,
                              qa.y + (qb.y - qa.y) * alpha, qa.z + (qb.z - qa.z) * alpha)
        q.normalize()
        out[n] = Matrix.Translation(la.lerp(lb, alpha)) @ q.to_matrix().to_4x4()
    return out


# ---------------------------------------------------------------------------------------------------------------
# Motion definitions
# ---------------------------------------------------------------------------------------------------------------
def rod_matrix(pitch_deg, yaw_deg, roll_deg, grip):
    R = rot3((0, 0, 1), yaw_deg) @ rot3((0, 1, 0), -pitch_deg) @ rot3((1, 0, 0), roll_deg)
    return mat4(R, grip)


def thumb_rot(spec):
    R = Matrix.Identity(3)
    for ax, deg in spec:
        R = rot3(getattr(SIDES_R, ax), deg) @ R
    return R


def loop_w():
    return 2.0 * math.pi / (LOOP_FRAMES / FPS)


def idle_targets(B, sides, t):
    """Empty hands, relaxed, one slow breath per loop (t in seconds)."""
    w = loop_w()
    out = {}
    for s, sd in sides.items():
        ph = 0.0 if s == "r" else 0.35                      # the arms are not mirror clones
        breath = math.sin(w * t + ph)
        lag = math.sin(w * t + ph - 0.6)
        base = sd.W + (IDLE_WRIST_OFFSET if s == "r" else mir(IDLE_WRIST_OFFSET))
        wrist = base + Vector((0.004 * math.sin(w * t + ph - 0.4),
                               sd.sign * 0.002 * math.sin(2.0 * w * t + ph + 0.5),
                               0.009 * lag))
        shoulder = Vector((0.002 * math.sin(w * t + ph + 0.3), 0.0, 0.005 * breath))
        R_delta_r = (rot3(SIDES_R.uh, IDLE_HAND_FLEX_DEG + 2.0 * math.sin(w * t + ph - 0.9))
                     @ rot3(SIDES_R.th, IDLE_HAND_ROLL_DEG))
        R_hand = sd.R(R_delta_r) @ B["hand_" + s].to_3x3()
        curl = IDLE_CURL_DEG + 3.0 * math.sin(w * t + ph - 1.2)
        out[s] = ArmTarget(wrist, R_hand, shoulder, curl, 0.0, thumb_rot(THUMB_RELAXED))
    return out


def swaying_rod(t, grip, pitch, yaw, sway):
    """The rod (hand_r_rod) world matrix at time t: a slow sway, one breath per loop."""
    w = loop_w()
    a_pitch, a_yaw, a_grip = sway
    g = grip + Vector((a_grip.x * math.sin(w * t - 0.3), a_grip.y * math.sin(2.0 * w * t),
                       a_grip.z * math.sin(w * t - 0.5)))
    return rod_matrix(pitch + a_pitch * math.sin(w * t - 0.9), yaw + a_yaw * math.sin(w * t + 0.4), ROD_ROLL_DEG, g)


def holdrod_rod(t):
    return swaying_rod(t, ROD_GRIP, ROD_PITCH_DEG, ROD_YAW_DEG, HOLD_SWAY)


def prone_rod(t):
    return swaying_rod(t, PRONE_ROD_GRIP, PRONE_ROD_PITCH_DEG, PRONE_ROD_YAW_DEG, PRONE_SWAY)


def knob_grip_matrix(B, sd):
    """hand_l bone matrix in ROD space when the left fist holds the crank knob."""
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    back = Z * math.cos(math.radians(KNOB_ROLL_DEG)) - X * math.sin(math.radians(KNOB_ROLL_DEG))
    R = basis_yz(-Y, back) @ basis_yz(tilted(sd.uh, sd.th, KNOB_TILT_DEG), sd.vh).transposed()
    center = CRANK_KNOB_ROD + Y * KNOB_IN_FIST_M
    return Matrix.Translation(center) @ R.to_4x4() @ Matrix.Translation(-sd.grip) @ B["hand_l"]


def rod_hands_targets(B, R_rod, knob_grip, t, r_shoulder, r_pole, l_shoulder, l_pole):
    """Right hand from the rod (standard grip), left hand from the crank knob (knob_grip = hand_l in rod space)."""
    breath = Vector((0.0, 0.0, 0.004 * math.sin(loop_w() * t)))
    P_hr = R_rod @ B["hand_r_rod"].inverted() @ B["hand_r"]
    P_hl = R_rod @ knob_grip
    return {"r": ArmTarget(P_hr.translation, P_hr.to_3x3(), r_shoulder + breath, GRIP_CURL_DEG, GRIP_TILT_DEG,
                           thumb_rot(THUMB_GRIP), pole=r_pole),
            "l": ArmTarget(P_hl.translation, P_hl.to_3x3(), l_shoulder + breath, KNOB_CURL_DEG, KNOB_TILT_DEG,
                           thumb_rot(THUMB_KNOB), pole=l_pole)}


def holdrod_targets(B, sides, t, knob_grip):
    return rod_hands_targets(B, holdrod_rod(t), knob_grip, t, RIGHT_SHOULDER_HOLD, RIGHT_POLE_HOLD,
                             LEFT_SHOULDER_HOLD, LEFT_POLE_HOLD)


def prone_hold_targets(B, sides, t, knob_grip):
    return rod_hands_targets(B, prone_rod(t), knob_grip, t, PRONE_RIGHT_SHOULDER, pole_dir(PRONE_RIGHT_POLE_AZ_DEG),
                             PRONE_LEFT_SHOULDER, PRONE_LEFT_POLE)


def tuck_targets(B, sides, t):
    """Both fists for the tuck at time t. The right fist is solved from a virtual rod reversed in the fist
    (rod_rest_matrix(reverse, slide)) pointing TUCK_DIR_*; the left fist mirrors the right's base pose, lower and
    further out, breathing with its own phase."""
    w = loop_w()
    sd = sides["r"]
    G = rod_rest_matrix(sd, reverse=True, slide=TUCK_SLIDE_M).inverted() @ B["hand_r"]   # hand_r in rod space

    def right_hand(tt, breathe):
        d = direction(TUCK_DIR_YAW_DEG, TUCK_DIR_PITCH_DEG)
        held = TUCK_HELD.copy()
        roll = TUCK_HAND_ROLL_DEG
        if breathe:
            held += Vector((0.002 * math.sin(w * tt - 0.3), 0.001 * math.sin(2.0 * w * tt),
                            0.005 * math.sin(w * tt - 0.5)))
            roll += 1.0 * math.sin(w * tt - 0.9)
        return rod_matrix(TUCK_DIR_PITCH_DEG, TUCK_DIR_YAW_DEG, roll, held + d * TUCK_SLIDE_M) @ G

    P_hr = right_hand(t, True)
    base = right_hand(0.0, False)
    P_hl = M4 @ (base @ B["hand_r"].inverted()) @ M4 @ B["hand_l"]
    P_hl.translation += TUCK_LEFT_OFFSET + Vector((0.002 * math.sin(w * t + 0.05), -0.001 * math.sin(2.0 * w * t + 0.35),
                                                   0.005 * math.sin(w * t - 0.15)))
    breath = Vector((0.0, 0.0, 0.004 * math.sin(w * t)))
    r_pole = pole_dir(TUCK_RIGHT_POLE_AZ_DEG)
    return {"r": ArmTarget(P_hr.translation, P_hr.to_3x3(), TUCK_SHOULDER + breath, GRIP_CURL_DEG, GRIP_TILT_DEG,
                           thumb_rot(THUMB_GRIP), pole=r_pole),
            "l": ArmTarget(P_hl.translation, P_hl.to_3x3(), TUCK_SHOULDER + breath, TUCK_LEFT_CURL_DEG, GRIP_TILT_DEG,
                           thumb_rot(THUMB_GRIP), pole=mir(r_pole))}


def dip_arms_matrix(f):
    """'arms' bone pose for StanceDip frame f: drop, pitch forward about the chest pivot, overshoot, settle."""
    loc = ARMS_PIVOT + Vector((DIP_X_CM[f], 0.0, DIP_Z_CM[f])) * 0.01
    return mat4(rot3((0, 1, 0), DIP_PITCH_DEG[f]), loc)


# --- T-028 rod aim ------------------------------------------------------------------------------------------------
def screen_of(p):
    """Camera-space point -> (x %, y %) of the 90 deg FP frame from the top-left (see screen())."""
    q = screen(p)
    return None if q is None else (q[0] * 100.0, q[1] * 100.0)


def solve_rod_aim(grip, target, yaw0, pitch0):
    """(yaw, pitch) in degrees so the rod tip (ROD_TIP_M along the rod) from `grip` projects onto `target` (%)."""
    yaw, pitch = yaw0, pitch0
    for _i in range(40):
        f = screen_of(grip + direction(yaw, pitch) * ROD_TIP_M)
        ex, ey = f[0] - target[0], f[1] - target[1]
        if abs(ex) < 1e-6 and abs(ey) < 1e-6:
            break
        h = 1e-3
        fy = screen_of(grip + direction(yaw + h, pitch) * ROD_TIP_M)
        fp = screen_of(grip + direction(yaw, pitch + h) * ROD_TIP_M)
        J = Matrix((((fy[0] - f[0]) / h, (fp[0] - f[0]) / h), ((fy[1] - f[1]) / h, (fp[1] - f[1]) / h)))
        d = J.inverted() @ Vector((ex, ey))
        yaw, pitch = yaw - d.x, pitch - d.y
    return yaw, pitch


def aim_rod(name):
    """The rod (hand_r_rod) world matrix for an aim pose; Center = holdrod_rod(0) exactly. Returns (matrix, info)."""
    R0 = holdrod_rod(0.0)
    if name == "Center":
        tip = screen_of(R0 @ Vector((ROD_TIP_M, 0.0, 0.0)))
        return R0, {"grip_offset_cm": [0, 0, 0], "pitch_deg": None, "yaw_deg": None, "tip_pct": tip}
    g_off, _s_off, target = AIM_POSES[name]
    grip = R0.translation + Vector(g_off) * 0.01
    yaw, pitch = solve_rod_aim(grip, target, ROD_YAW_DEG, ROD_PITCH_DEG)
    roll = ROD_ROLL_DEG + AIM_ROLL.get(name, 0.0)
    M = rod_matrix(pitch, yaw, roll, grip)
    return M, {"grip_offset_cm": list(g_off), "pitch_deg": round(pitch, 2), "yaw_deg": round(yaw, 2), "roll_deg": roll,
               "tip_pct": [round(c, 2) for c in screen_of(M @ Vector((ROD_TIP_M, 0.0, 0.0)))]}


def aim_arms_matrix(B, yaw_deg, pitch_deg):
    """'arms' bone pose for an aim pose: the upper body turns about the chest pivot (yaw left +, pitch up +)."""
    R = rot3((0, 0, 1), yaw_deg) @ rot3((0, 1, 0), -pitch_deg)
    return about_point(R, ARMS_PIVOT) @ B["arms"]


def _aim_try(B, sides, name, knob_grip, yaw_deg, pitch_deg):
    arms_M = aim_arms_matrix(B, yaw_deg, pitch_deg)
    Rb = arms_M.to_3x3() @ B["arms"].to_3x3().transposed()
    R_rod, _info = aim_rod(name)
    s_off = Vector(AIM_POSES[name][1]) * 0.01 if name != "Center" else Vector()
    tg = rod_hands_targets(B, R_rod, knob_grip, 0.0, RIGHT_SHOULDER_HOLD + s_off, Rb @ RIGHT_POLE_HOLD,
                           LEFT_SHOULDER_HOLD + s_off, Rb @ LEFT_POLE_HOLD)
    swivel = AIM_ELBOW_SWIVEL.get(name)
    if swivel is not None:
        delta = arms_M @ B["arms"].inverted()
        for s, deg in zip(("r", "l"), swivel):
            S = delta @ sides[s].S + tg[s].shoulder_off
            tg[s].pole = rot3(tg[s].wrist - S, deg) @ tg[s].pole
    return arms_M, tg


_AIM_CACHE = {}


def aim_pose(B, sides, name, knob_grip):
    """(arms_M, targets, info) of an aim pose. The rod frame is fixed by aim_rod(); the upper body ('arms' bone) turns
    too, so the arms don't do it all: body YAW depends only on the yaw input and body PITCH only on the pitch input
    (Left/Right search the yaw 0..40 deg towards the aim side, Up/Down the pitch 0..15 deg; the corners combine their
    side's yaw and their Up/Down pitch), which keeps the aim-offset blend of the 'arms' bone separable and monotonic.
    The search (2.5 deg steps) keeps both wrists closest to their Center angles: sum of the flex, deviation and half
    the twist changes, plus 0.4 per degree of body turn. A side listed in AIM_BODY_YAW uses that yaw (T-033)."""
    if name in _AIM_CACHE:
        return _AIM_CACHE[name]
    sx, sy = next(k for k, v in AIM_GRID.items() if v == name)
    if sx != 0 and sy != 0:
        ay = aim_pose(B, sides, AIM_GRID[(sx, 0)], knob_grip)[2]["body_yaw_deg"]
        ap = aim_pose(B, sides, AIM_GRID[(0, sy)], knob_grip)[2]["body_pitch_deg"]
        best = (ay, ap, None)
    elif name == "Center":
        best = (0.0, 0.0, 0.0)
    elif sy == 0 and sx in AIM_BODY_YAW:
        best = (AIM_BODY_YAW[sx], 0.0, None)              # T-033: fixed for the Right column
    else:
        ref = full_pose(B, sides, B["arms"], _aim_try(B, sides, "Center", knob_grip, 0.0, 0.0)[1])[1]
        best = None
        cands = [(-sx * i * 2.5, 0.0) for i in range(0, 17)] if sx != 0 else [(0.0, sy * i * 2.5) for i in range(0, 7)]
        for ay, ap in cands:
            try:
                arms_M, tg = _aim_try(B, sides, name, knob_grip, ay, ap)
                m = full_pose(B, sides, arms_M, tg)[1]
            except RuntimeError:
                continue                                   # out of reach
            cost = 0.4 * (abs(ay) + abs(ap))
            for s in ("l", "r"):
                cost += (abs(m[s]["wrist_flex_deg"] - ref[s]["wrist_flex_deg"])
                         + abs(m[s]["wrist_dev_deg"] - ref[s]["wrist_dev_deg"])
                         + 0.5 * abs(m[s]["twist_deg"] - ref[s]["twist_deg"]))
            if best is None or cost < best[2]:
                best = (ay, ap, cost)
    arms_M, tg = _aim_try(B, sides, name, knob_grip, best[0], best[1])
    full_pose(B, sides, arms_M, tg)                     # raises if a corner is out of reach
    info = dict(aim_rod(name)[1])
    info.update({"body_yaw_deg": best[0], "body_pitch_deg": best[1],
                 "wrist_cost": None if best[2] is None else round(best[2], 1)})
    _AIM_CACHE[name] = (arms_M, tg, info)
    return _AIM_CACHE[name]


def topo_bones(B):
    out, seen = [], set()

    def visit(n):
        if n in seen:
            return
        if PARENT[n] is not None:
            visit(PARENT[n])
        seen.add(n)
        out.append(n)
    for n in B:
        visit(n)
    return out


def _local(P, n):
    par = PARENT[n]
    return P[n] if par is None else P[par].inverted() @ P[n]


def mesh_additive(B, base_P, ref_P, samples, alpha=1.0):
    """Unreal's aim offset, simulated: each sample is a MESH-SPACE additive against ref_P (rotation delta in component
    space, translation delta in the bone's local space); the samples blend by weight (quaternion accumulation,
    identity for the rest weight) and the result is applied to base_P with `alpha`. samples: [(P_i, weight)]."""
    out = {}
    for n in topo_bones(B):
        q_ref = ref_P[n].to_quaternion()
        acc = Quaternion((1.0 - sum(w for _p, w in samples), 0.0, 0.0, 0.0))
        dt = Vector()
        for P_i, w in samples:
            dq = P_i[n].to_quaternion() @ q_ref.inverted()
            if dq.w < 0.0:
                dq.negate()
            acc = acc + dq * w
            dt += (_local(P_i, n).translation - _local(ref_P, n).translation) * w
        acc.normalize()
        acc = Quaternion().slerp(acc, alpha)
        R_new = acc @ base_P[n].to_quaternion()
        t_new = _local(base_P, n).translation + dt * alpha
        par = PARENT[n]
        if par is None:
            out[n] = mat4(R_new.to_matrix(), t_new)
        else:
            loc_rot = out[par].to_quaternion().inverted() @ R_new
            out[n] = out[par] @ mat4(loc_rot.to_matrix(), t_new)
    return out


def aim_weights(u, v):
    """Bilinear weights of the 3x3 aim grid for input (yaw u, pitch v) in [-1, 1]: [(grid key, weight)] without the
    Center (its delta is zero)."""
    sx, sy = (1 if u >= 0 else -1), (1 if v >= 0 else -1)
    au, av = abs(u), abs(v)
    ws = [((sx, 0), au * (1 - av)), ((0, sy), (1 - au) * av), ((sx, sy), au * av)]
    return [(k, w) for k, w in ws if w > 1e-9]


# --- T-030 hold fish / carry cooler -----------------------------------------------------------------------------
def fish_matrix(t, cfg):
    """hand_r_fish (the reference fish's Grip frame) in camera space at time t: one slow breath per loop."""
    w = loop_w()
    a_pitch, a_yaw, a_off = FISH_SWAY
    p = cfg["pos"] + Vector((a_off.x * math.sin(w * t - 0.3), a_off.y * math.sin(2.0 * w * t),
                             a_off.z * math.sin(w * t - 0.5)))
    return rod_matrix(cfg["pitch"] + a_pitch * math.sin(w * t - 0.9), cfg["yaw"] + a_yaw * math.sin(w * t + 0.4),
                      cfg["roll"], p)


def belly_contact(scale):
    """The left-palm contact in the hand_r_fish frame for a fish of `scale` (it grows about the throat contact)."""
    return FISH_THROAT + (FISH_BELLY - FISH_THROAT) * scale


def holdfish_targets(B, sides, t, cfg):
    """Both hands from the fish frame: the right palm under the throat (hand_r_fish rest), the left palm under the
    belly at FISH_BELLY, its channel along the fish (thumb towards the tail)."""
    breath = Vector((0.0, 0.0, 0.004 * math.sin(loop_w() * t)))
    F = fish_matrix(t, cfg)
    P_hr = F @ B["hand_r_fish"].inverted() @ B["hand_r"]
    P_hl = (F @ rot3((1, 0, 0), FISH_CONTACT_ROLL_DEG).to_4x4() @ Matrix.Translation(belly_contact(cfg["scale"]))
            @ rot3((0, 0, 1), 180.0 + FISH_LEFT_YAW_DEG).to_4x4()
            @ palm_cradle(sides["l"]).inverted() @ B["hand_l"])
    return {"r": ArmTarget(P_hr.translation, P_hr.to_3x3(), FISH_RIGHT_SHOULDER + breath, FISH_CURL_DEG,
                           FISH_TILT_DEG, thumb_rot(THUMB_FISH), pole=pole_dir(FISH_RIGHT_POLE_AZ_DEG)),
            "l": ArmTarget(P_hl.translation, P_hl.to_3x3(), FISH_LEFT_SHOULDER + breath, FISH_LEFT_CURL_DEG,
                           FISH_TILT_DEG, thumb_rot(THUMB_SUPPORT), pole=mir(pole_dir(FISH_LEFT_POLE_AZ_DEG)))}


def fish_attach_offset(grip, scale):
    """Relative location of a fish scaled by `scale` on hand_r_fish (fish space, m): the throat contact stays in the
    right palm for every size (the fish grows about it, along its belly line towards the left palm)."""
    return -grip * scale + fish_throat_contact() * (1.0 - scale)


def fish_throat_contact():
    """The right-palm contact point in the hand_r_fish frame (FISH_THROAT turned by FISH_CONTACT_ROLL_DEG)."""
    return rot3((1, 0, 0), FISH_CONTACT_ROLL_DEG) @ FISH_THROAT


def carry_frame(t):
    """The carry frame at time t: origin midway between the two handle grips, X forward (away from the player), tilted
    COOLER_TILT_DEG about the handle axis (top away)."""
    w = loop_w()
    a_tilt, a_off = COOLER_SWAY
    p = COOLER_HANDLES_POS + Vector((a_off.x * math.sin(w * t - 0.3), 0.0, a_off.z * math.sin(w * t - 0.5)))
    return mat4(rot3((0, 1, 0), COOLER_TILT_DEG + a_tilt * math.sin(w * t - 0.9)), p)


def cooler_matrix(t):
    """The cooler bone (cooler pivot, bottom center, cooler axes; its front faces the player) at time t."""
    return (carry_frame(t) @ rot3((0, 0, 1), 180.0).to_4x4()
            @ Matrix.Translation(Vector((0.0, 0.0, -COOLER_HANDLE_L.z))))


def handle_frame(side, roll_deg=None, sign=None):
    """Grip frame on a rope handle in the CARRY frame (the rod-grip convention: X = along the rope through the fist
    channel, Z = the rod-up side of the fist), mirrored for the left. The right fist holds the handle on the player's
    right (the cooler's Handle_L, since the cooler faces the player)."""
    roll_deg = HANDLE_ROLL_DEG if roll_deg is None else roll_deg
    sign = HANDLE_DIR if sign is None else sign
    X = Vector((sign, 0.0, 0.0))
    Z = Vector((0.0, 0.0, 1.0))
    R = rot3(X, roll_deg) @ Matrix((X, Z.cross(X), Z)).transposed()
    H_r = mat4(R, Vector((0.0, -COOLER_HANDLE_L.y, 0.0)))
    return H_r if side == "r" else M4 @ H_r @ M4


def carry_targets(B, sides, t, roll_deg=None, sign=None):
    C = carry_frame(t)
    breath = Vector((0.0, 0.0, 0.004 * math.sin(loop_w() * t)))
    out = {}
    for s, sd in sides.items():
        G = rod_rest_matrix(sd)                           # the rope in the fist channel, like the rod grip
        P_h = C @ handle_frame(s, roll_deg, sign) @ G.inverted() @ B["hand_" + s]
        pole = CARRY_POLE if s == "r" else mir(CARRY_POLE)
        out[s] = ArmTarget(P_h.translation, P_h.to_3x3(), CARRY_SHOULDER + breath, GRIP_CURL_DEG, GRIP_TILT_DEG,
                           thumb_rot(THUMB_GRIP), pole=pole)
    return out


# ---------------------------------------------------------------------------------------------------------------
# T-062 cast: A_FPArms_Cast_Charge (Unreal evaluates it at CastCharge01) + A_FPArms_Cast_Release (one-shot montage)
# ---------------------------------------------------------------------------------------------------------------
# A cast pose = six channels applied to HoldRod_Idle frame 0 (breath phase 0), plus the elbow poles:
#   swing     rod pitch about the grip (deg, + = tip back/up), about cast_swing_axis() (horizontal, across the rod)
#   gx/gy/gz  grip offset (m, chest frame: x forward, y left, z up)
#   cp        'arms' (chest) pitch about ARMS_PIVOT (deg, + = lean back: everything in front of the chest rises)
#   shx       both shoulders forward (m, - = drawn back)
# Both hands are solved from the rod exactly as in HoldRod_Idle (right fist on the grip, left fist on the crank knob),
# so no grip slides on any frame. All channels 0 = HoldRod_Idle frame 0.
# Why the wind-up is not bigger (T-062 gate A search): in first person the forearms and elbows are off-screen at every
# charge, so the arms read through the fists. More grip travel back or right cuts the right fist at the right edge;
# more lift, more lean or a rod past vertical carries the left fist (on the crank knob) into the centre 40 % box.
CAST_CHANNELS = ("swing", "gx", "gy", "gz", "cp", "shx")
CAST_IDLE = {ch: 0.0 for ch in CAST_CHANNELS}
CAST_WIND = dict(swing=38.0, gx=-0.04, gy=-0.035, gz=-0.005, cp=2.0, shx=-0.01)   # charge 1 = Cast_Release frame 0
CAST_CHARGE_FRAMES = 36        # 1.2 s = DT_Fishing ChargeTime; Unreal samples time = CastCharge01 * length
CAST_RELEASE_FRAMES = 24       # 0.8 s one-shot
CAST_RELEASE_FRAME = 3         # notify CastRelease (0.100 s): rod ~38 deg up, tip at full speed = the line leaves
CAST_SETTLE_FROM = 9           # end of the follow-through hold; one ease from here into HoldRod_Idle frame 0
# Release keys (frame: channels). 0-3: drive (the hands lead, the rod lags back), whip, RELEASE; 4-6: follow-through
# (rod tip down at the water); 6-9: a short moving hold; then the settle (cast_release_channels). After the release
# the hands stop (gx ~ constant) and the shoulders reach on (shx >= gx): the bent elbow keeps the forearm off the rear
# grip, which runs under the forearm like a braced butt (an extended arm put the grip through the forearm, in view).
CAST_RELEASE_KEYS = {
    0: dict(CAST_WIND),
    1: dict(swing=44.0, gx=0.0, gy=-0.03, gz=0.0, cp=1.0, shx=0.0),
    2: dict(swing=24.0, gx=0.035, gy=-0.02, gz=0.01, cp=-0.3, shx=0.01),
    3: dict(swing=4.0, gx=0.055, gy=-0.012, gz=0.022, cp=-1.0, shx=0.02),
    4: dict(swing=-16.0, gx=0.056, gy=-0.008, gz=0.036, cp=-1.3, shx=0.05),
    5: dict(swing=-30.0, gx=0.058, gy=-0.006, gz=0.048, cp=-1.5, shx=0.058),
    6: dict(swing=-37.0, gx=0.06, gy=-0.005, gz=0.055, cp=-1.5, shx=0.06),
    9: dict(swing=-35.5, gx=0.057, gy=-0.005, gz=0.053, cp=-1.3, shx=0.058),
    CAST_RELEASE_FRAMES: dict(CAST_IDLE),
}
CAST_R_POLE_WIND = Vector((-0.15, -1.0, -0.7))   # right elbow further out at the wind-up (hold: RIGHT_POLE_HOLD)
CAST_L_POLE_WIND = Vector((0.1, 0.9, -0.6))      # left elbow a little forward and higher (hold: LEFT_POLE_HOLD)
CAST_FT_POLE_AZ_DEG = 335.0    # right elbow swings out (pole_dir) through the follow-through, away from the rear grip
                               # (340-345 clear a little more but twist the forearm 52-60 deg vs 12 at the idle)


def ease_out(x, k):
    x = max(0.0, min(1.0, x))
    return 1.0 - (1.0 - x) ** k


def settle_ease(t):
    """0..1 -> 0..1: zero speed at both ends, peak speed at t = 1/3, a long soft landing (double zero at 1)."""
    t = max(0.0, min(1.0, t))
    return 1.0 - (1.0 - t) ** 3 * (1.0 + 3.0 * t)


def pchip(xs, ys, x):
    """Monotone cubic (Fritsch-Carlson) through the keys (xs, ys) with zero end slopes: no overshoot between keys,
    the clip starts and ends at rest (clean montage blends)."""
    n = len(xs)
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    for i in range(1, n - 1):
        if d[i - 1] * d[i] > 0.0:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    i = max(j for j in range(n - 1) if xs[j] <= x)
    t = (x - xs[i]) / h[i]
    h00, h10, h01, h11 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t, -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
    return h00 * ys[i] + h10 * h[i] * m[i] + h01 * ys[i + 1] + h11 * h[i] * m[i + 1]


def cast_swing_axis():
    """Horizontal axis across the HoldRod_Idle rod: swinging about it is a pure pitch of the rod."""
    d0 = holdrod_rod(0.0).to_3x3().col[0].normalized()
    return d0.cross(Vector((0.0, 0.0, 1.0))).normalized()


def cast_charge_channels(c):
    """Charge 0..1 -> channels. The rod leads (fast ease-out: the press answers at once), the grip follows, the chest
    leans in last (smoothstep from 15 %), so the last third reads as building tension."""
    s_rod, s_grip, s_body = ease_out(c, 1.8), ease_out(c, 1.5), smoothstep((c - 0.15) / 0.85)
    s = {"swing": s_rod, "gx": s_grip, "gy": s_grip, "gz": s_grip, "cp": s_body, "shx": s_body}
    return {ch: CAST_IDLE[ch] + (CAST_WIND[ch] - CAST_IDLE[ch]) * s[ch] for ch in CAST_CHANNELS}


def cast_release_channels(f):
    """Release frame (may be fractional) -> channels: monotone cubic through CAST_RELEASE_KEYS up to CAST_SETTLE_FROM,
    then one settle_ease into the idle (no hitch in the deceleration)."""
    if f >= CAST_SETTLE_FROM:
        s = settle_ease((f - CAST_SETTLE_FROM) / float(CAST_RELEASE_FRAMES - CAST_SETTLE_FROM))
        a, b = CAST_RELEASE_KEYS[CAST_SETTLE_FROM], CAST_RELEASE_KEYS[CAST_RELEASE_FRAMES]
        return {ch: a[ch] + (b[ch] - a[ch]) * s for ch in CAST_CHANNELS}
    keys = sorted(k for k in CAST_RELEASE_KEYS if k <= CAST_SETTLE_FROM)
    return {ch: pchip(keys, [CAST_RELEASE_KEYS[k][ch] for k in keys], f) for ch in CAST_CHANNELS}


def cast_poles(s_wind, w_ft=0.0):
    """Elbow poles: hold -> wind-up by s_wind; the right one turned towards CAST_FT_POLE_AZ_DEG by w_ft."""
    r = RIGHT_POLE_HOLD.lerp(CAST_R_POLE_WIND, s_wind)
    return r.lerp(pole_dir(CAST_FT_POLE_AZ_DEG), w_ft), LEFT_POLE_HOLD.lerp(CAST_L_POLE_WIND, s_wind)


def cast_targets(B, knob_grip, ch, r_pole, l_pole):
    """Channels + poles -> ('arms' bone matrix, arm targets). The rod is HoldRod_Idle frame 0's, swung and moved,
    then carried by the chest turn; shoulders and poles turn with the chest."""
    R0 = holdrod_rod(0.0)
    D = about_point(rot3((0.0, 1.0, 0.0), -ch["cp"]), ARMS_PIVOT)
    rod = D @ mat4(rot3(cast_swing_axis(), ch["swing"]) @ R0.to_3x3(),
                   R0.translation + Vector((ch["gx"], ch["gy"], ch["gz"])))
    Rb = D.to_3x3()
    sh = Rb @ Vector((ch["shx"], 0.0, 0.0))
    tg = rod_hands_targets(B, rod, knob_grip, 0.0, RIGHT_SHOULDER_HOLD + sh, Rb @ r_pole, LEFT_SHOULDER_HOLD + sh,
                           Rb @ l_pole)
    return D @ B["arms"], tg


class Poser:
    """Pose sources for the actions and the previews. Each returns (P, metrics); arms_M applies the additive
    StanceDip on top (moves everything rigidly about the chest pivot), for previews and clearance checks."""

    def __init__(self, B, sides, knob_grip):
        self.B, self.sides, self.knob_grip = B, sides, knob_grip
        self.tuck_rest = tuck_rod_rest(sides["r"])

    def _dip(self, P, arms_M):
        if arms_M is None:
            return P
        D = arms_M @ self.B["arms"].inverted()
        return {n: (M if n == "root" else D @ M) for n, M in P.items()}

    def idle(self, f, arms_M=None):
        P, m = full_pose(self.B, self.sides, self.B["arms"], idle_targets(self.B, self.sides, f / FPS))
        return self._dip(P, arms_M), m

    def hold(self, f, arms_M=None):
        P, m = full_pose(self.B, self.sides, self.B["arms"],
                         holdrod_targets(self.B, self.sides, f / FPS, self.knob_grip))
        return self._dip(P, arms_M), m

    def prone_hold(self, f, arms_M=None):
        P, m = full_pose(self.B, self.sides, self.B["arms"],
                         prone_hold_targets(self.B, self.sides, f / FPS, self.knob_grip))
        return self._dip(P, arms_M), m

    def tuck(self, f, arms_M=None):
        P, m = full_pose(self.B, self.sides, self.B["arms"], tuck_targets(self.B, self.sides, f / FPS),
                         rod_rest=self.tuck_rest)
        return self._dip(P, arms_M), m

    def hold_fish(self, f, arms_M=None, name="A_FPArms_HoldFish_Idle"):
        P, m = full_pose(self.B, self.sides, self.B["arms"],
                         holdfish_targets(self.B, self.sides, f / FPS, FISH_POSES[name]))
        return self._dip(P, arms_M), m

    def hold_fish_large(self, f, arms_M=None):
        return self.hold_fish(f, arms_M, "A_FPArms_HoldFish_Large_Idle")

    def carry(self, f, arms_M=None):
        t = f / FPS
        P, m = full_pose(self.B, self.sides, self.B["arms"], carry_targets(self.B, self.sides, t),
                         cooler=cooler_matrix(t))
        return self._dip(P, arms_M), m

    def cast_charge(self, f, arms_M=None):
        """A_FPArms_Cast_Charge at frame f (charge = f / CAST_CHARGE_FRAMES, f may be fractional). Frame 0 IS
        HoldRod_Idle frame 0; the last frame IS Cast_Release frame 0."""
        c = f / float(CAST_CHARGE_FRAMES)
        if c <= 0.0:
            return self.hold(0, arms_M)
        r_pole, l_pole = cast_poles(ease_out(c, 1.7))
        body, tg = cast_targets(self.B, self.knob_grip, cast_charge_channels(c), r_pole, l_pole)
        P, m = full_pose(self.B, self.sides, body, tg)
        return self._dip(P, arms_M), m

    def cast_release(self, f, arms_M=None):
        """A_FPArms_Cast_Release at frame f. Frame 0 = the full wind-up; the last frame IS HoldRod_Idle frame 0."""
        if f >= CAST_RELEASE_FRAMES:
            return self.hold(0, arms_M)
        w_ft = smoothstep((f - 2) / 4.0) * (1.0 - smoothstep((f - 12) / 12.0))
        r_pole, l_pole = cast_poles(1.0 - smoothstep(f / 6.0), w_ft)
        body, tg = cast_targets(self.B, self.knob_grip, cast_release_channels(f), r_pole, l_pole)
        P, m = full_pose(self.B, self.sides, body, tg)
        return self._dip(P, arms_M), m

    def aim(self, name):
        def fn(_f, arms_M=None):
            body, tg, _info = aim_pose(self.B, self.sides, name, self.knob_grip)
            P, m = full_pose(self.B, self.sides, body, tg)
            return self._dip(P, arms_M), m
        return fn

    def dip_basis(self, f):
        basis = {n: Matrix.Identity(4) for n in self.B}
        basis["arms"] = self.B["arms"].inverted() @ dip_arms_matrix(f)
        return basis

    def source(self, action):
        src = {"A_FPArms_Idle": self.idle, "A_FPArms_HoldRod_Idle": self.hold,
               "A_FPArms_Prone_HoldRod_Idle": self.prone_hold, "A_FPArms_Prone_TuckRod": self.tuck,
               "A_FPArms_HoldFish_Idle": self.hold_fish, "A_FPArms_HoldFish_Large_Idle": self.hold_fish_large,
               "A_FPArms_CarryCooler_Idle": self.carry}
        src.update({a: self.aim(AIM_GRID[k]) for k, a in AIM_ACTIONS.items()})
        return src.get(action)


# ---------------------------------------------------------------------------------------------------------------
# Actions + NLA
# ---------------------------------------------------------------------------------------------------------------
def key_action(arm_obj, name, frames, basis_for_frame):
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    ad = arm_obj.animation_data or arm_obj.animation_data_create()
    ad.action = act
    prev_q = {}
    for f in frames:
        apply_basis(arm_obj, basis_for_frame(f), prev_q)
        for pbone in arm_obj.pose.bones:
            pbone.keyframe_insert("location", frame=f, group=pbone.name)
            pbone.keyframe_insert("rotation_quaternion", frame=f, group=pbone.name)
    slot = ad.action_slot
    for layer in act.layers:
        for strip in layer.strips:
            for fc in strip.channelbag(slot).fcurves:
                for kp in fc.keyframe_points:
                    kp.interpolation = "LINEAR"
    ad.action = None
    track = ad.nla_tracks.new()
    track.name = name
    strip = track.strips.new(name, int(frames[0]), act)
    strip.name = name
    return act


def build_actions(arm_obj, poser):
    B = poser.B
    makers = {name: (lambda f, fn=poser.source(name): pose_to_basis(B, fn(f)[0]))
              for name, _f0, _f1 in ACTIONS if poser.source(name)}
    makers["A_FPArms_StanceDip"] = poser.dip_basis
    return {name: key_action(arm_obj, name, list(range(f0, f1 + 1)), makers[name]) for name, f0, f1 in ACTIONS}


def play(arm_obj, action_name, frame):
    """Evaluate one exported action at a frame (the NLA track solo'd)."""
    ad = arm_obj.animation_data
    for tr in ad.nla_tracks:
        tr.mute = tr.name != action_name
    ad.action = None
    bpy.context.scene.frame_set(frame)


def set_static_pose(arm_obj, B, P):
    """Pose directly (previews that are not an exported action): all tracks muted."""
    for tr in arm_obj.animation_data.nla_tracks:
        tr.mute = True
    apply_basis(arm_obj, pose_to_basis(B, P))
    bpy.context.view_layer.update()


def set_basis(arm_obj, basis):
    for tr in arm_obj.animation_data.nla_tracks:
        tr.mute = True
    apply_basis(arm_obj, basis)
    bpy.context.view_layer.update()


# ---------------------------------------------------------------------------------------------------------------
# Export + re-import check
# ---------------------------------------------------------------------------------------------------------------
def export_all(arm_obj, mesh_obj):
    """SK_FPArms + one file per action, in CENTIMETERS with no scale on any node (pb.export_skeletal_fbx: the rig is
    authored in meters; the export writes cm values and UnitScaleFactor 1.0). Returns (paths, per-file scale reports)."""
    ad = arm_obj.animation_data
    ad.action = None
    for tr in ad.nla_tracks:
        tr.mute = True
    rest_pose(arm_obj)
    bpy.context.scene.frame_set(0)
    path = EXPORT_DIR / "SK_FPArms.fbx"
    units = {"SK_FPArms": pb.export_skeletal_fbx(path, arm_obj, [mesh_obj], bake_anim=False)}
    out = {"SK_FPArms": str(path)}
    for name, _f0, _f1 in ACTIONS:
        for tr in ad.nla_tracks:
            tr.mute = tr.name != name
        path = EXPORT_DIR / (name + ".fbx")
        units[name] = pb.export_skeletal_fbx(path, arm_obj, [], bake_anim=True)
        out[name] = str(path)
    for tr in ad.nla_tracks:
        tr.mute = True
    rest_pose(arm_obj)
    return out, units


def snapshot():
    return {k: set(getattr(bpy.data, k)) for k in ("objects", "meshes", "armatures", "actions", "materials")}


def cleanup_since(snap):
    for o in set(bpy.data.objects) - snap["objects"]:
        bpy.data.objects.remove(o, do_unlink=True)
    for k in ("meshes", "armatures", "actions", "materials"):
        coll = getattr(bpy.data, k)
        for block in set(coll) - snap[k]:
            coll.remove(block)


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), anim_offset=0.0)
    new = [o for o in bpy.data.objects if o not in before]
    return (next((o for o in new if o.type == "ARMATURE"), None), next((o for o in new if o.type == "MESH"), None))


def motion_check(arm_obj):
    """Pops and loop seams, measured on the keyed actions: the largest per-frame move of the hands, fingertips,
    thumbs, rod bone and elbows (mm), and the first-to-last frame difference (0 for a clean loop / a dip that
    returns to rest). tip_max_step_mm: the rod tip (1.65 m out on hand_r_rod) per frame."""
    probes = [("hand_r", "head"), ("hand_l", "head"), ("fingers_r", "tail"), ("fingers_l", "tail"), ("thumb_r", "tail"),
              ("thumb_l", "tail"), ("hand_r_rod", "head"), ("hand_l_crank", "head"), ("lowerarm_r", "head"),
              ("lowerarm_l", "head"), ("hand_r_fish", "head"), ("cooler", "head")]
    out = {}
    for name, f0, f1 in ACTIONS:
        pos, tips = [], []
        for f in range(f0, f1 + 1):
            play(arm_obj, name, f)
            pos.append([(arm_obj.matrix_world @ getattr(arm_obj.pose.bones[b], end)).copy() for b, end in probes])
            tips.append(arm_obj.matrix_world @ arm_obj.pose.bones["hand_r_rod"].matrix @ Vector((1.648, 0, 0)))
        steps = [max((a - b).length for a, b in zip(pos[i + 1], pos[i])) for i in range(len(pos) - 1)]
        out[name] = {"max_step_mm": round(max(steps) * 1000, 2),
                     "first_last_delta_mm": round(max((a - b).length for a, b in zip(pos[-1], pos[0])) * 1000, 3),
                     "loop_seam_step_mm": round(max((a - b).length for a, b in zip(pos[1], pos[0])) * 1000, 2),
                     "tip_max_step_mm": round(max((tips[i + 1] - tips[i]).length
                                                  for i in range(len(tips) - 1)) * 1000, 2),
                     "tip_first_last_delta_mm": round((tips[-1] - tips[0]).length * 1000, 3)}
    return out


def scale_dev(M):
    return max(abs(c - 1.0) for c in M.to_scale())


def reimport_check(exports, B, mesh_bounds, poser):
    """Re-import every FBX (Blender's importer, scene in meters): bone positions/axes, mesh bounds and baked poses must
    match the source. The files are cm (UnitScaleFactor 1.0), so the importer puts its cm -> m factor (0.01) on the
    armature OBJECT and the bones themselves carry cm values: bone_heads_cm_max_error_mm compares the armature-space
    bone heads with the source x100, and *_bone_scale_dev is the largest deviation from 1.0 of any bone's rest or posed
    armature-space scale (no bone may carry the unit conversion)."""
    scene = bpy.context.scene
    fps = scene.render.fps
    k = pb.SKELETAL_FBX_CM_PER_UNIT
    res = {}
    snap = snapshot()
    arm, mesh = import_fbx(exports["SK_FPArms"])
    mw = arm.matrix_world
    head_err = max((mw @ arm.data.bones[n].head_local - B[n].translation).length for n in B)
    head_cm_err = max((arm.data.bones[n].head_local - B[n].translation * k).length for n in B)
    rot_err = max(math.degrees((mw.to_3x3().normalized() @ arm.data.bones[n].matrix_local.to_3x3()).to_quaternion()
                               .rotation_difference(B[n].to_3x3().to_quaternion()).angle) for n in B)
    mn, mx = pb.world_bounds([mesh])
    res["SK_FPArms"] = {
        "bones": len(arm.data.bones), "armature_object_scale": [round(c, 6) for c in mw.to_scale()],
        "max_bone_head_error_mm": round(head_err * 1000, 3), "max_bone_axis_error_deg": round(rot_err, 3),
        "bone_heads_cm_max_error_mm": round(head_cm_err * 10, 3),
        "rest_bone_scale_dev": max(scale_dev(b.matrix_local) for b in arm.data.bones),
        "hand_r_head_cm": [round(c, 3) for c in arm.data.bones["hand_r"].head_local],
        "mesh_min_m": [round(c, 4) for c in mn], "mesh_max_m": [round(c, 4) for c in mx],
        "source_min_m": [round(c, 4) for c in mesh_bounds[0]], "source_max_m": [round(c, 4) for c in mesh_bounds[1]],
        "skinned_groups": len(mesh.vertex_groups),
    }
    cleanup_since(snap)
    for name, _f0, f1 in ACTIONS:
        snap = snapshot()
        arm, _m = import_fbx(exports[name])
        act = arm.animation_data.action if arm.animation_data else None
        entry = {"action": act.name if act else None,
                 "frame_range": [round(c, 2) for c in act.frame_range] if act else None,
                 "armature_object_scale": [round(c, 6) for c in arm.matrix_world.to_scale()]}
        sdev = 0.0
        for f in range(0, f1 + 1, 3):
            scene.frame_set(f)
            sdev = max([sdev] + [scale_dev(pbone.matrix) for pbone in arm.pose.bones])
        entry["posed_bone_scale_dev"] = sdev
        fn = poser.source(name)
        if fn is not None:
            err, rot = 0.0, 0.0
            for f in (0, 30, 45, 60, 90):
                scene.frame_set(f)
                P, _m2 = fn(f)
                for n in ("hand_r", "hand_l", "hand_r_rod", "fingers_r", "thumb_l", "hand_l_crank", "hand_r_fish",
                          "cooler"):
                    M = arm.matrix_world @ arm.pose.bones[n].matrix
                    err = max(err, (M.translation - P[n].translation).length)
                    rot = max(rot, math.degrees(M.to_quaternion().rotation_difference(P[n].to_quaternion()).angle))
            entry["max_pose_error_mm"] = round(err * 1000, 3)
            entry["max_pose_error_deg"] = round(rot, 3)
        else:
            errs = []
            for f in range(0, f1 + 1):
                scene.frame_set(f)
                want = dip_arms_matrix(f).translation
                errs.append((arm.matrix_world @ arm.pose.bones["arms"].head - want).length)
            entry["max_pose_error_mm"] = round(max(errs) * 1000, 3)
        res[name] = entry
        cleanup_since(snap)
    scene.render.fps = fps
    return res


# ---------------------------------------------------------------------------------------------------------------
# Unreal numbers for the spec (component space of SK_FPArms, cm / degrees)
# ---------------------------------------------------------------------------------------------------------------
def ue_transform(M):
    """Blender armature-space matrix -> Unreal component-space location (cm) and FRotator (pitch, yaw, roll)."""
    t = M.translation
    R = M.to_3x3().normalized()
    X = Vector((R.col[0].x, -R.col[0].y, R.col[0].z))
    Y = -Vector((R.col[1].x, -R.col[1].y, R.col[1].z))
    Z = Vector((R.col[2].x, -R.col[2].y, R.col[2].z))
    pitch = math.degrees(math.atan2(X.z, math.hypot(X.x, X.y)))
    yaw = math.degrees(math.atan2(X.y, X.x))
    cp, sp, cy, sy = (math.cos(math.radians(pitch)), math.sin(math.radians(pitch)),
                      math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    sy_axis = Vector((-sy, cy, 0.0))  # FRotationMatrix(pitch, yaw, 0) Y axis
    roll = math.degrees(math.atan2(Z.dot(sy_axis), Y.dot(sy_axis)))
    return {"location_cm": [round(t.x * 100, 2), round(-t.y * 100, 2), round(t.z * 100, 2)],
            "rotation_pyr_deg": [round(pitch, 2), round(yaw, 2), round(roll, 2)]}


# ---------------------------------------------------------------------------------------------------------------
# Geometry checks: screen composition, clearance, crossfades
# ---------------------------------------------------------------------------------------------------------------
FP_W, FP_H = fp_preview.FP_RESOLUTION
HALF_H_TAN = math.tan(math.radians(fp_preview.FP_HFOV_DEG) / 2.0) * FP_H / FP_W


def screen(p):
    """Camera-space point -> (x, y) fractions of a 16:9 90 deg FP frame (0,0 = top-left), None behind the eye."""
    if p.x <= 1e-4:
        return None
    return (0.5 + 0.5 * (-p.y / p.x), 0.5 - 0.5 * (p.z / p.x) / HALF_H_TAN)


def on_screen(q):
    return q is not None and 0.0 <= q[0] <= 1.0 and 0.0 <= q[1] <= 1.0


class Geo:
    """Evaluates the skinned arms mesh and the SM_Rod_Basic mesh (on hand_r_rod) for the current pose."""

    def __init__(self, arm_obj, mesh_obj, rod_obj):
        self.arm, self.mesh = arm_obj, mesh_obj
        dg = bpy.context.evaluated_depsgraph_get()
        ev = rod_obj.evaluated_get(dg)
        me = ev.to_mesh()
        self.rod_local = [v.co.copy() for v in me.vertices]
        ev.to_mesh_clear()
        idx = {mesh_obj.vertex_groups[n].index for n in ("hand_r", "fingers_r", "thumb_r", "hand_l", "fingers_l",
                                                          "thumb_l")}
        self.hand_mask = [sum(g.weight for g in v.groups if g.group in idx) >= 0.5 for v in mesh_obj.data.vertices]

    def arms_points(self):
        dg = bpy.context.evaluated_depsgraph_get()
        ev = self.mesh.evaluated_get(dg)
        me = ev.to_mesh()
        pts = [self.mesh.matrix_world @ v.co for v in me.vertices]
        polys = [tuple(p.vertices) for p in me.polygons]
        ev.to_mesh_clear()
        return pts, polys

    def rod_frame(self):
        return self.arm.matrix_world @ self.arm.pose.bones["hand_r_rod"].matrix

    def rod_points(self):
        M = self.rod_frame()
        return [M @ p for p in self.rod_local]

    def envelope(self):
        arms, _ = self.arms_points()
        rod = self.rod_points()
        front = [p for p in arms + rod if p.x > 0.0]
        vis_rod = [screen(p) for p in rod]
        vis_rod = [q for q in vis_rod if on_screen(q)]
        return {"max_z_arms": max(p.z for p in arms), "max_z_rod": max(p.z for p in rod),
                "max_x_arms": max(p.x for p in arms), "max_x_rod": max(p.x for p in rod),
                "front": front, "leftmost_rod_on_screen": min((q[0] for q in vis_rod), default=None),
                "tip": self.rod_frame() @ Vector((1.648, 0.0, 0.0))}


def pitch_tolerance(points, ceiling_m, scale=1.0):
    """Largest camera pitch-up (deg) before any point in front of the eye (arms attached to the camera) rises above
    ceiling_m. scale: Unreal FirstPersonScale (points drawn scaled towards the eye)."""
    best = 0.0
    for deg in range(0, 91):
        s, c = math.sin(math.radians(deg)), math.cos(math.radians(deg))
        if max(scale * (p.x * s + p.z * c) for p in points) > ceiling_m:
            break
        best = deg
    return best


def reach_over_pitch(points, scale=1.0, lo=-45, hi=45):
    """Largest horizontal reach (m) of the points over camera pitches lo..hi deg (level wall test when 0)."""
    worst = -9.0
    for deg in range(lo, hi + 1, 5):
        s, c = math.sin(math.radians(deg)), math.cos(math.radians(deg))
        worst = max(worst, max(scale * (p.x * c - p.z * s) for p in points))
    return worst


def clip_envelope(geo, poser, action, frames, with_dip=True):
    """Max height / reach over the clip's frames, the StanceDip additive (bottom and overshoot frames) included."""
    fn = poser.source(action)
    agg = {"max_z_m": -9.0, "max_x_m": -9.0, "max_z_frame": None, "max_x_frame": None}
    highest, forward = None, None
    dips = [None] + ([dip_arms_matrix(3), dip_arms_matrix(6)] if with_dip else [])
    for f in frames:
        for d in dips:
            P, _m = fn(f, arms_M=d)
            set_static_pose(geo.arm, poser.B, P)
            e = geo.envelope()
            z = max(e["max_z_arms"], e["max_z_rod"])
            x = max(e["max_x_arms"], e["max_x_rod"])
            if z > agg["max_z_m"]:
                agg["max_z_m"], agg["max_z_frame"], highest = z, f, e
            if x > agg["max_x_m"]:
                agg["max_x_m"], agg["max_x_frame"], forward = x, f, e
    agg["max_z_m"], agg["max_x_m"] = round(agg["max_z_m"], 4), round(agg["max_x_m"], 4)
    return agg, highest, forward


def crossfades(geo, poser):
    """Simulated Unreal crossfades (local space, shortest-arc rotations) between the rod poses at frame 0: the
    highest point, the furthest reach and the leftmost on-screen rod point along the way."""
    B = poser.B
    poses = {"HoldRod": poser.hold(0)[0], "ProneHold": poser.prone_hold(0)[0], "ProneTuck": poser.tuck(0)[0]}
    out = {}
    for a, b in (("ProneHold", "ProneTuck"), ("HoldRod", "ProneTuck"), ("HoldRod", "ProneHold")):
        ba, bb = pose_to_basis(B, poses[a]), pose_to_basis(B, poses[b])
        mz, mx, lx, tips = -9.0, -9.0, 9.0, []
        for i in range(0, 21):
            set_basis(geo.arm, blend_basis(ba, bb, i / 20.0))
            e = geo.envelope()
            mz = max(mz, e["max_z_arms"], e["max_z_rod"])
            if 0 < i < 20:
                mx = max(mx, e["max_x_arms"], e["max_x_rod"])
                if e["leftmost_rod_on_screen"] is not None:
                    lx = min(lx, e["leftmost_rod_on_screen"])
            tips.append([round(c, 2) for c in e["tip"]])
        out[a + "->" + b] = {"max_z_m": round(mz, 3), "max_x_between_m": round(mx, 3),
                             "leftmost_rod_on_screen": round(lx, 3) if lx < 9 else None,
                             "tip_path_m": tips[::4]}
    return out


def rod_penetration(geo, held_x):
    """Deepest rod vertex inside the arms mesh (closest-point normal test), outside the fist zone (+-6 cm around
    rod-local x = held_x). Returns {zone: [count, max depth mm]}."""
    arms, polys = geo.arms_points()
    bvh = BVHTree.FromPolygons(arms, polys)
    M = geo.rod_frame()
    out = {}
    for p in geo.rod_local:
        w = M @ p
        loc, nrm, _i, d = bvh.find_nearest(w)
        if loc is None or (w - loc).dot(nrm) >= 0.0:
            continue
        zone = ("fist" if abs(p.x - held_x) < 0.06 else "butt" if p.x < held_x else
                "reel" if (p.z < -0.02 and p.x < 0.2) else "rod")
        q = screen(w)
        e = out.setdefault(zone, [0, 0.0, 0])
        e[0] += 1
        e[1] = max(e[1], round(d * 1000, 1))
        e[2] += 1 if on_screen(q) else 0
    return {k: {"verts": v[0], "max_depth_mm": v[1], "on_screen_verts": v[2]} for k, v in out.items()}


def screen_metrics(geo, P, B, rod_rest=None):
    """Where the fists, the rod tip and the reel spool land in the 90 deg FP frame (fractions from the top-left)."""
    set_static_pose(geo.arm, B, P)
    arms, polys = geo.arms_points()
    hands = [screen(p) for p, m in zip(arms, geo.hand_mask) if m]
    hands = [q for q in hands if q is not None]
    M = geo.rod_frame()
    tip = screen(M @ Vector((1.648, 0.0, 0.0)))
    # reel spool visibility: points on the camera-facing half of the spool (rod-local x 0.12-0.146, r 0.02)
    bvh = BVHTree.FromPolygons(arms, polys)
    vis, tot = 0, 0
    for i in range(5):
        x = 0.121 + 0.006 * i
        for k in range(12):
            a = 2.0 * math.pi * k / 12
            p = M @ Vector((x, 0.0205 * math.cos(a), -0.066 + 0.0205 * math.sin(a)))
            if M.to_3x3() @ Vector((0.0, math.cos(a), math.sin(a))) @ (-p) <= 0.0:
                continue                                   # faces away from the eye
            tot += 1
            hit = bvh.ray_cast(Vector((0.0, 0.0, 0.0)), p.normalized(), p.length - 0.002)
            vis += 1 if (hit[0] is None and on_screen(screen(p))) else 0
    bb = (min(q[0] for q in hands), max(q[0] for q in hands), min(q[1] for q in hands), max(q[1] for q in hands))
    return {"hands_x_pct": [round(bb[0] * 100, 1), round(bb[1] * 100, 1)],
            "hands_center_x_pct": round((bb[0] + bb[1]) * 50, 1), "hands_top_y_pct": round(bb[2] * 100, 1),
            "tip_pct": None if tip is None else [round(tip[0] * 100, 1), round(tip[1] * 100, 1)],
            "reel_spool_visible_pct": round(100.0 * vis / max(1, tot), 0)}


def prone_checks(geo, poser):
    frames = list(range(0, LOOP_FRAMES + 1, 3))
    B = poser.B
    res = {"limits": {"eye_height_m": list(PRONE_EYE_M), "crawl_gap_m": CRAWL_GAP_M,
                      "max_height_above_eye_m": round(PRONE_CEILING_LIMIT_M, 3), "wall_m": PRONE_WALL_M,
                      "max_reach_m": PRONE_WALL_LIMIT_M, "first_person_scale": FIRST_PERSON_SCALE}}
    hi_frames = {}
    for key, action in (("Prone_HoldRod_Idle", "A_FPArms_Prone_HoldRod_Idle"),
                        ("Prone_TuckRod", "A_FPArms_Prone_TuckRod"), ("HoldRod_Idle", "A_FPArms_HoldRod_Idle")):
        agg, highest, forward = clip_envelope(geo, poser, action, frames)
        pts = highest["front"]
        fw = forward["front"]
        agg.update({
            "pitch_up_ok_deg": {"ceiling_15cm": pitch_tolerance(pts, 0.15), "ceiling_25cm": pitch_tolerance(pts, 0.25),
                                "ceiling_15cm_fp_scale": pitch_tolerance(pts, 0.15, FIRST_PERSON_SCALE),
                                "ceiling_25cm_fp_scale": pitch_tolerance(pts, 0.25, FIRST_PERSON_SCALE)},
            "reach_over_pitch_pm45_m": round(reach_over_pitch(fw), 3),
            "reach_over_pitch_pm45_fp_scale_m": round(reach_over_pitch(fw, FIRST_PERSON_SCALE), 3),
        })
        if key != "HoldRod_Idle":
            agg["ceiling_ok"] = agg["max_z_m"] <= PRONE_CEILING_LIMIT_M
            agg["height_above_eye_cm"] = round(agg["max_z_m"] * 100, 1)
            agg["clearance_to_60cm_ceiling_cm"] = {"eye_45cm": round((PRONE_CEILING_LIMIT_M - agg["max_z_m"]) * 100, 1),
                                                   "eye_35cm": round((0.25 - agg["max_z_m"]) * 100, 1)}
        if key == "Prone_TuckRod":
            agg["wall_ok"] = agg["max_x_m"] <= PRONE_WALL_LIMIT_M
            agg["gap_to_50cm_wall_cm"] = round((PRONE_WALL_M - agg["max_x_m"]) * 100, 1)
        res[key] = agg
        hi_frames[key] = (agg["max_z_frame"], agg["max_x_frame"])
    res["crossfades"] = crossfades(geo, poser)
    # rod through the hands/forearms (off the fist zone)
    for key, fn, held_x in (("HoldRod_Idle", poser.hold, 0.0), ("Prone_HoldRod_Idle", poser.prone_hold, 0.0),
                            ("Prone_TuckRod", poser.tuck, -TUCK_SLIDE_M)):
        set_static_pose(geo.arm, B, fn(0)[0])
        res.setdefault("rod_penetration_f0", {})[key] = rod_penetration(geo, held_x)
    return res, hi_frames


# ---------------------------------------------------------------------------------------------------------------
# Previews
# ---------------------------------------------------------------------------------------------------------------
CELL = (640, 360)
FULL = fp_preview.FP_RESOLUTION


def label_material(name, hex_color):
    """Unlit label color: an emission shader for EEVEE, the viewport color for Workbench."""
    mat = bpy.data.materials.get(name)
    if mat is not None:
        return mat
    mat = bpy.data.materials.new(name)
    if hasattr(mat, "use_nodes") and not mat.use_nodes:
        mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = style.hex_to_linear_rgba(hex_color)
    em.inputs["Strength"].default_value = 1.0
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(em.outputs[0], out.inputs[0])
    mat.diffuse_color = style.hex_to_linear_rgba(hex_color)
    return mat


def stage_label(text, origin, right, up, forward, half_w, res, rel_size=0.05, dist=None):
    """Parchment text on an ink plate in the top-left corner of a view. origin/right/up/forward: the camera frame
    (world); half_w: half the view width at `dist` in front of the camera (ortho: half the ortho scale)."""
    dist = dist if dist is not None else 0.05
    half_h = half_w * res[1] / res[0]
    size = half_h * 2.0 * rel_size
    R = Matrix((Vector(right), Vector(up), -Vector(forward))).transposed().to_4x4()
    curve = bpy.data.curves.new("PV_LabelCurve", "FONT")
    curve.body = text
    curve.size = size
    curve.align_y = "TOP"
    txt = bpy.data.objects.new("PV_Label", curve)
    bpy.context.scene.collection.objects.link(txt)
    curve.materials.append(label_material("PV_LabelText", style.UI.PARCHMENT))
    corner = Vector(origin) + Vector(forward) * dist + Vector(right) * (-half_w * 0.97) + Vector(up) * (half_h * 0.95)
    txt.matrix_world = Matrix.Translation(corner) @ R
    bpy.context.view_layer.update()
    w, h = txt.dimensions.x, txt.dimensions.y
    pad = size * 0.35
    me = bpy.data.meshes.new("PV_LabelPlate")
    me.from_pydata([(-pad, pad, 0), (w + pad, pad, 0), (w + pad, -h - pad, 0), (-pad, -h - pad, 0)], [], [(0, 1, 2, 3)])
    me.materials.append(label_material("PV_LabelPlate", style.UI.INK))
    plate = bpy.data.objects.new("PV_LabelPlate", me)
    bpy.context.scene.collection.objects.link(plate)
    plate.matrix_world = Matrix.Translation(corner + Vector(forward) * (size * 0.05)) @ R
    for o in (txt, plate):
        if hasattr(o, "visible_shadow"):
            o.visible_shadow = False

    def cleanup():
        for o in (txt, plate):
            data = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            if isinstance(data, bpy.types.Mesh):
                bpy.data.meshes.remove(data)
            else:
                bpy.data.curves.remove(data)
    return cleanup


def fp_frame(path, kind, label, res=FULL, samples=32):
    """First-person frame over a palette backdrop (fp_preview: EEVEE, day or dusk) with a corner label."""
    half_w = 0.05 * math.tan(math.radians(fp_preview.FP_HFOV_DEG) / 2.0)
    cleanup = stage_label(label, (0, 0, 0), (0, -1, 0), (0, 0, 1), (1, 0, 0), half_w, res,
                          rel_size=0.028 if res[1] >= 720 else 0.05)
    try:
        fp_preview.render_fp(path, kind, resolution=res, samples=samples)
    finally:
        cleanup()
    return str(path)


def shot(path, location, target, lens=35.0, ortho=None, res=CELL, world_hex=style.TROPICAL.SKY_DAY, label=None,
         rotation=None):
    """Workbench render (technical views: close-ups, side/top, weights) with a corner label."""
    scene = bpy.context.scene
    cam_data = bpy.data.cameras.new("PV_Cam")
    cam_data.sensor_fit = "HORIZONTAL"
    cam_data.sensor_width = 36.0
    cam_data.clip_start = 0.01
    cam_data.clip_end = 500.0
    if ortho:
        cam_data.type = "ORTHO"
        cam_data.ortho_scale = ortho
    else:
        cam_data.lens = lens
    cam = bpy.data.objects.new("PV_Cam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = Vector(location)
    if rotation is not None:
        cam.rotation_euler = rotation
    else:
        cam.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    bpy.context.view_layer.update()
    cleanups = []
    if label:
        Mw = cam.matrix_world
        right, up, fwd = Mw.col[0].xyz.normalized(), Mw.col[1].xyz.normalized(), -Mw.col[2].xyz.normalized()
        d = 0.05
        half_w = ortho / 2.0 if ortho else d * 18.0 / lens
        cleanups.append(stage_label(label, Mw.translation, right, up, fwd, half_w, res, dist=d))
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.film_transparent = False
    scene.render.filepath = str(path)
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_cavity = True
    if scene.world is None:
        scene.world = bpy.data.worlds.new("PV_World")
    scene.world.color = style.linear(world_hex)
    vs = scene.view_settings
    old_vt = (vs.view_transform, vs.look, vs.exposure)
    vs.view_transform, vs.look, vs.exposure = "Standard", "None", 0.0
    bpy.ops.render.render(write_still=True)
    vs.view_transform, vs.look, vs.exposure = old_vt
    for c in cleanups:
        c()
    bpy.data.objects.remove(cam, do_unlink=True)
    bpy.data.cameras.remove(cam_data)
    return str(path)


def box(name, center, size, hex_color, shadow=False):
    """Axis-aligned preview box (test geometry and guide lines)."""
    me = bpy.data.meshes.new(name)
    hx, hy, hz = (s / 2.0 for s in size)
    c = Vector(center)
    vs = [c + Vector((sx * hx, sy * hy, sz * hz)) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    me.from_pydata(vs, [], faces)
    me.materials.append(style.make_material("PV_" + hex_color.strip("#"), hex_color, "flat"))
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    if hasattr(o, "visible_shadow"):
        o.visible_shadow = shadow
    return o


def stage_env(kind):
    """Preview-only test geometry around the prone eye. 'dock': planks 35 cm below the eye ending 0.9 m ahead (the
    DT_Movement prone eye height, fishing off a dock edge). 'gap': the 60 cm crawl gap with the worst-case 45 cm eye:
    sand floor 45 cm below, a slate ceiling 15 cm above (no shadow, so the arms stay readable). 'wall': floor 35 cm
    below and a slate wall 50 cm in front of the eye."""
    objs = []
    if kind == "dock":
        objs.append(box("PV_Dock", (-1.05, 0.0, -0.40), (3.9, 3.0, 0.10), style.TROPICAL.WEATHERED_WOOD, shadow=True))
    elif kind == "gap":
        objs.append(box("PV_GapFloor", (10.0, 0.0, -PRONE_EYE_M[1] - 0.05), (26.0, 30.0, 0.10), style.TROPICAL.SAND,
                        shadow=True))
        objs.append(box("PV_GapCeiling", (0.25, 0.0, PRONE_CEILING_LIMIT_M + 0.1), (6.5, 30.0, 0.2), style.FOGGY.ROCK))
    elif kind == "wall":
        objs.append(box("PV_WallFloor", (0.0, 0.0, -PRONE_EYE_M[0] - 0.05), (6.0, 30.0, 0.10), style.TROPICAL.SAND,
                        shadow=True))
        objs.append(box("PV_Wall", (PRONE_WALL_M + 0.1, 0.0, 0.5), (0.2, 30.0, 2.0), style.FOGGY.ROCK))

    def cleanup():
        for o in objs:
            me = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            bpy.data.meshes.remove(me)
    return cleanup


def stage_guides(view):
    """Guide bars for the orthographic clearance views: ceiling lines 15 cm (60 cm gap, eye 45) and 25 cm (eye 35)
    above the eye, floor lines 35 / 45 cm below, the 50 cm wall; plus the eye marker. view: 'side' or 'top'."""
    objs = []
    t = 0.006
    red, amber, ink = style.UI.DANGER, "#E8C46A", style.UI.INK
    if view == "side":
        y = -0.6                                   # in front of both arms for a camera on -Y
        objs.append(box("PV_G_ceil15", (0.5, y, PRONE_CEILING_LIMIT_M + t / 2), (6.0, 0.02, t), red))
        objs.append(box("PV_G_ceil25", (0.5, y, 0.25 + t / 2), (6.0, 0.02, t), amber))
        objs.append(box("PV_G_floor35", (0.5, y, -0.35 - t / 2), (6.0, 0.02, t), ink))
        objs.append(box("PV_G_floor45", (0.5, y, -0.45 - t / 2), (6.0, 0.02, t), ink))
        objs.append(box("PV_G_wall", (PRONE_WALL_M + t / 2, y, -0.1), (t, 0.02, 0.8), red))
        objs.append(box("PV_G_eye", (0.0, y, 0.0), (0.024, 0.024, 0.024), style.TROPICAL.ACCENT))
        objs.append(box("PV_G_eyeaxis", (0.06, y, 0.0), (0.12, 0.006, 0.006), style.TROPICAL.ACCENT))
    else:
        objs.append(box("PV_G_wall", (PRONE_WALL_M + t / 2, 0.0, 0.3), (t, 4.0, 0.01), red))
        objs.append(box("PV_G_eye", (0.0, 0.0, 0.3), (0.024, 0.024, 0.024), style.TROPICAL.ACCENT))
        objs.append(box("PV_G_eyeaxis", (0.06, 0.0, 0.3), (0.12, 0.006, 0.006), style.TROPICAL.ACCENT))

    def cleanup():
        for o in objs:
            me = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            bpy.data.meshes.remove(me)
    return cleanup


def stage_tip_trail(points):
    objs = [box("PV_Trail%d" % i, p, (0.04, 0.04, 0.04), style.TROPICAL.ACCENT) for i, p in enumerate(points)]

    def cleanup():
        for o in objs:
            me = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            bpy.data.meshes.remove(me)
    return cleanup


def render_previews(arm_obj, rod_obj, poser, arms_mod, geo, prone, hi_frames):
    """Contact sheet (4 columns) + full-size FP frames. FP views: fp_preview backdrops (EEVEE, palette sky/water);
    technical views: Workbench over the day sky."""
    B = poser.B
    tmp = PREVIEW_DIR / "SK_FPArms_anim_parts"
    tmp.mkdir(parents=True, exist_ok=True)
    cells, full = [], {}

    def cell(name):
        return tmp / (name + ".png")

    def pose(P):
        set_static_pose(arm_obj, B, P)

    side_cam = dict(location=(0.25, -2.0, -0.18), target=(0.25, 0.0, -0.18), ortho=0.85)
    # --- Idle (no rod)
    rod_obj.hide_render = True
    for f in (0, 45):
        play(arm_obj, "A_FPArms_Idle", f)
        cells.append(fp_frame(cell("idle_fp_%02d" % f), "day", "Idle f%d  FP 90" % f, res=CELL))
    for f in (0, 45):
        play(arm_obj, "A_FPArms_Idle", f)
        cells.append(shot(cell("idle_side_%02d" % f), label="Idle f%d  side" % f, **side_cam))
    # --- HoldRod_Idle (rod attached to hand_r_rod with a zero transform)
    rod_obj.hide_render = False
    play(arm_obj, "A_FPArms_HoldRod_Idle", 0)
    full["hold_day"] = fp_frame(PREVIEW_DIR / "SK_FPArms_anim_fp.png", "day", "HoldRod_Idle f0 (standing), day")
    full["hold_dusk"] = fp_frame(PREVIEW_DIR / "SK_FPArms_anim_fp_dusk.png", "dusk", "HoldRod_Idle f0 (standing), dusk")
    cells.append(fp_frame(cell("hold_fp_00"), "day", "HoldRod_Idle f0  day", res=CELL))
    play(arm_obj, "A_FPArms_HoldRod_Idle", 45)
    cells.append(fp_frame(cell("hold_fp_45"), "day", "HoldRod_Idle f45  day", res=CELL))
    play(arm_obj, "A_FPArms_HoldRod_Idle", 0)
    cells.append(fp_frame(cell("hold_fp_dusk"), "dusk", "HoldRod_Idle f0  dusk", res=CELL))
    cells.append(shot(cell("hold_side"), label="HoldRod_Idle f0  side", location=(0.45, -2.2, 0.05),
                      target=(0.45, 0.0, 0.05), ortho=1.4))
    P0, _ = poser.hold(0)
    Rr = P0["hand_r_rod"]
    g = Rr.translation
    X, Y, Z = (Rr.to_3x3().col[i].normalized() for i in range(3))
    kw = Rr @ CRANK_KNOB_ROD
    cells.append(shot(cell("hold_grip_top"), g + (Z * 0.9 - Y * 0.5 + X * 0.25).normalized() * 0.55, g + X * 0.03,
                      lens=50.0, label="grip R, from above-right"))
    cells.append(shot(cell("hold_grip_under"), g + (-Z * 0.8 - Y * 0.6 + X * 0.35).normalized() * 0.55,
                      g + X * 0.03, lens=50.0, label="grip R, from below-right"))
    cells.append(shot(cell("hold_knob_l"), kw + (Y * 0.8 + Z * 0.3 + X * 0.35).normalized() * 0.55, kw, lens=50.0,
                      label="L fist on crank knob"))
    cells.append(shot(cell("hold_below"), (0.25, -0.55, -0.55), (0.3, -0.15, -0.25), lens=30.0,
                      label="HoldRod f0 from below (butt clear)"))
    # --- StanceDip on HoldRod
    for f in (3, 6):
        pose(poser.hold(0, arms_M=dip_arms_matrix(f))[0])
        cells.append(fp_frame(cell("dip_hold_fp_%02d" % f), "day", "HoldRod + StanceDip f%d" % f, res=CELL))
    rod_obj.hide_render = True
    play(arm_obj, "A_FPArms_StanceDip", 3)
    cells.append(shot(cell("dip_side_03"), label="StanceDip f3 (clip)", **side_cam))
    rod_obj.hide_render = False
    # --- Prone_HoldRod_Idle
    env = stage_env("dock")
    play(arm_obj, "A_FPArms_Prone_HoldRod_Idle", 0)
    full["prone_day"] = fp_frame(PREVIEW_DIR / "SK_FPArms_prone_fp.png", "day",
                                 "Prone_HoldRod_Idle f0: prone at a dock edge (eye 35 cm), day")
    full["prone_dusk"] = fp_frame(PREVIEW_DIR / "SK_FPArms_prone_fp_dusk.png", "dusk",
                                  "Prone_HoldRod_Idle f0: prone at a dock edge (eye 35 cm), dusk")
    cells.append(fp_frame(cell("prone_fp_00"), "day", "Prone_HoldRod_Idle f0  dock", res=CELL))
    play(arm_obj, "A_FPArms_Prone_HoldRod_Idle", 45)
    cells.append(fp_frame(cell("prone_fp_45"), "dusk", "Prone_HoldRod_Idle f45  dusk", res=CELL))
    env()
    env = stage_env("gap")
    fz = hi_frames["Prone_HoldRod_Idle"][0]
    play(arm_obj, "A_FPArms_Prone_HoldRod_Idle", fz)
    hz = prone["Prone_HoldRod_Idle"]["height_above_eye_cm"]
    full["prone_hold_gap"] = fp_frame(PREVIEW_DIR / "SK_FPArms_prone_hold_gap_fp.png", "day",
                                      "Prone_HoldRod_Idle f%d (highest): 60 cm gap, eye 45 cm, ceiling +15 cm; "
                                      "rod top +%.1f cm" % (fz, hz))
    cells.append(fp_frame(cell("prone_gap"), "day", "Prone hold f%d  60 cm gap (+15)" % fz, res=CELL))
    # --- Prone_TuckRod
    play(arm_obj, "A_FPArms_Prone_TuckRod", 0)
    full["tuck_gap"] = fp_frame(PREVIEW_DIR / "SK_FPArms_prone_tuck_gap_fp.png", "day",
                                "Prone_TuckRod f0: crawling in the 60 cm gap, eye 45 cm, ceiling +15 cm")
    cells.append(fp_frame(cell("tuck_gap_00"), "day", "Prone_TuckRod f0  60 cm gap", res=CELL))
    play(arm_obj, "A_FPArms_Prone_TuckRod", 45)
    cells.append(fp_frame(cell("tuck_gap_45"), "dusk", "Prone_TuckRod f45  gap, dusk", res=CELL))
    env()
    env = stage_env("wall")
    fx = hi_frames["Prone_TuckRod"][1]
    play(arm_obj, "A_FPArms_Prone_TuckRod", fx)
    mx = prone["Prone_TuckRod"]["max_x_m"]
    full["tuck_wall"] = fp_frame(PREVIEW_DIR / "SK_FPArms_prone_wall_fp.png", "day",
                                 "Prone_TuckRod f%d (most forward): wall 50 cm ahead; nearest point %.1f cm"
                                 % (fx, mx * 100))
    cells.append(fp_frame(cell("tuck_wall"), "day", "Prone_TuckRod  wall 50 cm", res=CELL))
    env()
    # --- crossfade ProneHold -> ProneTuck, as Unreal blends it
    env = stage_env("gap")
    ba, bb = pose_to_basis(B, poser.prone_hold(0)[0]), pose_to_basis(B, poser.tuck(0)[0])
    blend_cells = []
    for a in (0.0, 0.25, 0.5, 0.75, 1.0):
        set_basis(arm_obj, blend_basis(ba, bb, a))
        blend_cells.append(fp_frame(cell("blend_%03d" % int(a * 100)), "day", "ProneHold -> Tuck  %d%%" % (a * 100),
                                    res=CELL))
    env()
    trail = []
    for i in range(0, 21):
        set_basis(arm_obj, blend_basis(ba, bb, i / 20.0))
        trail.append(geo.rod_frame() @ Vector((1.648, 0.0, 0.0)))
    set_basis(arm_obj, blend_basis(ba, bb, 0.5))
    g_clean = stage_guides("top")
    t_clean = stage_tip_trail(trail)
    blend_top = shot(cell("blend_top"), (0.55, -0.5, 3.0), None, ortho=5.4, rotation=(0.0, 0.0, math.radians(-90.0)),
                     label="rod tip path (red), rod at 50%, from above")
    t_clean()
    g_clean()
    blend_cells.append(blend_top)
    cells.extend(blend_cells[1:4])
    full["blend"] = pb.contact_sheet(blend_cells, PREVIEW_DIR / "SK_FPArms_prone_blend.png", cols=3, cell=CELL)
    # --- clearance views (orthographic, with the guide lines)
    clear_cells = []
    g_clean = stage_guides("side")
    play(arm_obj, "A_FPArms_Prone_HoldRod_Idle", fz)
    clear_cells.append(shot(cell("clear_hold_side"), (0.9, -3.0, -0.05), (0.9, 0.0, -0.05), ortho=2.6,
                            label="Prone hold f%d side: top %+.1f cm (red 15 / amber 25 cm = 60 cm gap)"
                            % (fz, hz)))
    play(arm_obj, "A_FPArms_Prone_TuckRod", fx)
    clear_cells.append(shot(cell("clear_tuck_side"), (0.0, -3.0, -0.1), (0.0, 0.0, -0.1), ortho=1.6,
                            label="Tuck f%d side: top %+.1f cm, front %.1f cm (red: wall 50 cm)"
                            % (fx, prone["Prone_TuckRod"]["height_above_eye_cm"], mx * 100)))
    g_clean()
    g_clean = stage_guides("top")
    clear_cells.append(shot(cell("clear_tuck_top"), (0.0, -0.2, 3.0), None, ortho=2.4,
                            rotation=(0.0, 0.0, math.radians(-90.0)), label="Tuck f%d from above (red: wall 50 cm)" % fx))
    play(arm_obj, "A_FPArms_Prone_TuckRod", 0)
    clear_cells.append(shot(cell("clear_tuck_34"), (0.95, -0.95, 0.35), (0.1, -0.15, -0.2), lens=35.0,
                            label="Tuck f0, front-right (rod back along the forearm)"))
    g_clean()
    cells.extend(clear_cells)
    full["clearance"] = pb.contact_sheet(clear_cells, PREVIEW_DIR / "SK_FPArms_prone_clearance.png", cols=2,
                                         cell=(960, 540))
    # --- weight test: extremes (not exported)
    rod_obj.hide_render = True
    for i, (P, lab, cam) in enumerate(weight_test_poses(poser)):
        set_static_pose(arm_obj, B, P)
        cells.append(shot(cell("weights_%d" % i), cam[0], cam[1], lens=35.0, label=lab))
    rest_pose(arm_obj)
    sheet = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_anim.png", cols=4, cell=CELL)
    return sheet, full


def weight_test_poses(poser):
    """Extreme elbow/wrist/twist poses for checking the skin weights (rendered, never exported)."""
    B, sides = poser.B, poser.sides
    sd_r, sd_l = sides["r"], sides["l"]
    out = []

    def target(sd, wrist, rest_axis_deg, curl=0.0, thumb=None, shoulder=None):
        S_new = sd.S + (shoulder or Vector())
        _E, _Q1, Q2 = lowerarm_rotation(sd, S_new, wrist, sd.pole)
        R_local = Matrix.Identity(3)
        for ax, deg in rest_axis_deg:            # rotations about the side's own rest hand axes
            R_local = rot3(getattr(sd, ax), deg) @ R_local
        R_hand = Q2 @ R_local @ B["hand_" + sd.s].to_3x3()
        return ArmTarget(wrist, R_hand, shoulder, curl, 0.0, thumb_rot(thumb or []))

    # 1: right elbow flexed ~120 deg + wrist flexed 70; left arm straight-ish + wrist extended 60 and twisted 90
    wr = sd_r.S + (Vector((0.26, 0.06, 0.11)).normalized() * 0.29)
    wl = sd_l.S + Vector((0.53, -0.02, 0.06))
    t1 = {"r": target(sd_r, wr, [("uh", 70.0)], curl=10.0),
          "l": target(sd_l, wl, [("uh", 60.0), ("th", 90.0)], curl=0.0)}
    P1, _ = full_pose(B, sides, B["arms"], t1)
    # 2: right wrist pronated -90 + ulnar deviation 30 + fist; left fist curl 100 + thumb across, supinated -80
    wr2 = sd_r.W + Vector((-0.06, 0.02, 0.02))
    wl2 = sd_l.W + Vector((-0.08, -0.02, 0.05))
    t2 = {"r": target(sd_r, wr2, [("th", -90.0), ("vh", -30.0)], curl=100.0, thumb=THUMB_GRIP),
          "l": target(sd_l, wl2, [("th", 80.0)], curl=100.0, thumb=[("th", -50.0), ("vh", 20.0)])}
    P2, _ = full_pose(B, sides, B["arms"], t2)

    def arm_view(P, sd, dist=0.85):
        S, E, W = (P[b + "_" + sd.s].translation for b in ("upperarm", "lowerarm", "hand"))
        n = (E - S).cross(W - E).normalized()
        if n.z < 0.0:
            n = -n
        mid = (S + E + W) / 3.0
        return mid + n * dist, mid

    def wrist_view(P, sd, dist=0.48):
        E, W = P["lowerarm_" + sd.s].translation, P["hand_" + sd.s].translation
        d = (W - E).normalized()
        side = Vector((0.0, sd.sign, 0.0))
        perp = (side - d * side.dot(d)).normalized()
        return W + perp * dist + Vector((0.0, 0.0, 0.12)), W + d * 0.03

    out.append((P1, "A: R elbow ~120 + wrist flex 70", arm_view(P1, sd_r)))
    out.append((P1, "A: L wrist ext 60 + twist 90", wrist_view(P1, sd_l)))
    out.append((P2, "B: R pronate 90 + ulnar 30, fist", wrist_view(P2, sd_r)))
    out.append((P2, "B: L supinate 80, fist + thumb", wrist_view(P2, sd_l)))
    return out


# ---------------------------------------------------------------------------------------------------------------
# T-028 / T-030: staging, checks and previews of the rod aim, hold-fish and carry-cooler poses
# ---------------------------------------------------------------------------------------------------------------
def load_fish(stem="sm_fish_bonefish"):
    """The model-artist's fish (static preview copy) and its fishkit Grip point (fishrig: s = GRIP_S on the spine)."""
    import fishrig
    obj, info = load_recipe(stem).build()
    bones = info["suggested_bones_m"]
    jx = [bones[b]["tail"][0] for b in ("Head", "Spine_01", "Spine_02", "Spine_03", "Spine_04")]
    body_len = (jx[0] - jx[4]) / (fishrig.JOINT_S[4] - fishrig.JOINT_S[0])
    grip = Vector((bones["Head"]["head"][0] - fishrig.GRIP_S * body_len, 0.0, 0.0))
    for c in obj.children:
        c.hide_render = True
    return obj, grip


class StagedCooler:
    """SM_Cooler_Starter + lid imported from the model-artist's exports (preview and checks only)."""

    def __init__(self):
        self.objs, self.orig = [], {}
        for stem in ("SM_Cooler_Starter", "SM_Cooler_Starter_Lid"):
            before = set(bpy.data.objects)
            bpy.ops.import_scene.fbx(filepath=str(REPO / "art" / "export" / "Props" / (stem + ".fbx")))
            new = set(bpy.data.objects) - before
            bpy.context.view_layer.update()
            for o in new:
                if o.type == "MESH" and o.name.startswith("UCX_"):
                    o.hide_render = True
                    if stem == "SM_Cooler_Starter":
                        self.ucx = o
                self.orig[o.name] = o.matrix_world.copy()
            mesh = next(o for o in new if o.type == "MESH" and o.name.split(".")[0] == stem)
            if stem == "SM_Cooler_Starter":
                self.body = mesh
                sock = {o.name.split(".")[0]: o.matrix_world.translation.copy() for o in new if o.type == "EMPTY"}
                self.hinge = sock["SOCKET_LidHinge"]
                self.sockets = sock
            else:
                self.lid = mesh
            self.objs += [(o, stem.endswith("_Lid")) for o in new if o.type == "MESH" and o.parent is None]

    def place(self, C):
        """Body at C (its pivot, bottom center); the lid (pivot on the hinge) on the body's LidHinge, closed."""
        for o, is_lid in self.objs:
            M = self.orig[o.name]
            o.matrix_world = C @ Matrix.Translation(self.hinge) @ M if is_lid else C @ M
        bpy.context.view_layer.update()

    def hide(self, hidden):
        for o in (self.body, self.lid):
            o.hide_render = hidden

    def points(self, objs=None):
        dg = bpy.context.evaluated_depsgraph_get()
        pts = []
        for o in (objs or (self.body, self.lid)):
            ev = o.evaluated_get(dg)
            me = ev.to_mesh()
            pts += [o.matrix_world @ v.co for v in me.vertices]
            ev.to_mesh_clear()
        return pts


def place_fish(arm_obj, fish_obj, grip, scale=1.0):
    """The fish on hand_r_fish as Unreal attaches it: relative location fish_attach_offset(), uniform scale."""
    M = arm_obj.matrix_world @ arm_obj.pose.bones["hand_r_fish"].matrix
    fish_obj.matrix_world = M @ Matrix.Translation(fish_attach_offset(grip, scale)) @ Matrix.Scale(scale, 4)
    bpy.context.view_layer.update()


def mesh_world(obj):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    pts = [obj.matrix_world @ v.co for v in me.vertices]
    polys = [tuple(p.vertices) for p in me.polygons]
    ev.to_mesh_clear()
    return pts, polys


def inside_count(points, bvh):
    """Points inside a closed mesh (closest-point normal test): (count, max depth mm)."""
    n, depth = 0, 0.0
    for p in points:
        loc, nrm, _i, d = bvh.find_nearest(p)
        if loc is not None and (p - loc).dot(nrm) < 0.0:
            n += 1
            depth = max(depth, d)
    return n, round(depth * 1000, 1)


def inside_convex(points, hull_obj):
    """Points inside a convex hull mesh (UCX_): (count, max depth mm)."""
    pts, polys = mesh_world(hull_obj)
    c = sum(pts, Vector()) / len(pts)
    planes = []
    for poly in polys:
        a, b, d = pts[poly[0]], pts[poly[1]], pts[poly[2]]
        n = (b - a).cross(d - a).normalized()
        if n.dot(a - c) < 0.0:
            n = -n
        planes.append((a, n))
    cnt, depth = 0, 0.0
    for p in points:
        dist = min(-(p - a).dot(n) for a, n in planes)
        if dist > 0.0:
            cnt += 1
            depth = max(depth, dist)
    return cnt, round(depth * 1000, 1)


def screen_box(points):
    qs = [screen(p) for p in points]
    qs = [q for q in qs if q is not None]
    ons = [q for q in qs if on_screen(q)]
    if not ons:
        return {"on_screen_pct": 0.0}
    return {"on_screen_pct": round(100.0 * len(ons) / len(points), 1),
            "x_pct": [round(min(q[0] for q in ons) * 100, 1), round(max(q[0] for q in ons) * 100, 1)],
            "top_y_pct": round(min(q[1] for q in ons) * 100, 1)}


def aim_checks(geo, poser):
    """Per pose: tip/fists on screen, wrists, Unreal hand_r_rod. Blends: Unreal's aim offset simulated over a 9x9
    input grid on HoldRod_Idle (breath frames) and on Prone_HoldRod_Idle: the left fist's drift off hand_l_crank
    and whether the rod tip leaves the frame; the prone rod's highest point per extreme."""
    B, sides = poser.B, poser.sides
    res = {"poses": {}}
    P_aim = {}
    for key, name in AIM_GRID.items():
        P, m = poser.aim(name)(0)
        P_aim[key] = P
        info = dict(aim_pose(B, sides, name, poser.knob_grip)[2])
        info.update(screen_metrics(geo, P, B))
        vis = [on_screen(screen(p)) for p in geo.rod_points()]
        info["rod_verts_on_screen_pct"] = round(100.0 * sum(vis) / len(vis), 1)
        info["rod_penetration"] = rod_penetration(geo, 0.0)
        info["wrists"] = m
        info["unreal_hand_r_rod"] = ue_transform(P["hand_r_rod"])
        res["poses"][name] = info
    center = P_aim[(0, 0)]
    grid = [i / 4.0 for i in range(-4, 5)]

    def drift(P):
        return ((P["hand_l"].translation - P["hand_l_crank"].translation).length,
                math.degrees(P["hand_l"].to_quaternion().rotation_difference(P["hand_l_crank"].to_quaternion()).angle))

    for label, base_fn, frames in (("HoldRod_Idle", poser.hold, (0, 22, 45, 67)),
                                   ("Prone_HoldRod_Idle", poser.prone_hold, (0, 45))):
        worst = [0.0, 0.0, None, 0.0]
        off = []
        for f in frames:
            base = base_fn(f)[0]
            for u in grid:
                for v in grid:
                    P = mesh_additive(B, base, center, [(P_aim[k], w) for k, w in aim_weights(u, v)])
                    d, a = drift(P)
                    # a Two Bone IK on hand_l to hand_l_crank closes the drift: shoulder -> crank wrist vs arm length
                    sd = sides["l"]
                    reach = (P["hand_l_crank"].translation - P["upperarm_l"].translation).length / (sd.L1 + sd.L2)
                    worst[3] = max(worst[3], reach)
                    if d > worst[0]:
                        worst[0], worst[2] = d, [f, u, v]
                    worst[1] = max(worst[1], a)
                    if not on_screen(screen(P["hand_r_rod"] @ Vector((ROD_TIP_M, 0.0, 0.0)))):
                        off.append([f, u, v])
        res["blend_on_" + label] = {"left_fist_off_crank_max_mm": round(worst[0] * 1000, 1),
                                    "worst_at_frame_yaw_pitch": worst[2],
                                    "left_fist_off_crank_max_deg": round(worst[1], 1),
                                    "crank_ik_max_reach_of_arm_pct": round(worst[3] * 100, 1),
                                    "tip_off_screen_count": len(off), "tip_off_screen_first": off[:6]}
    P = mesh_additive(B, center, center, [(P_aim[(1, 1)], 1.0)])
    res["sim_selfcheck_mm"] = round(max((P[n].translation - P_aim[(1, 1)][n].translation).length for n in B) * 1000, 4)
    base = poser.prone_hold(0)[0]
    hi = {}
    for key, name in AIM_GRID.items():
        P = mesh_additive(B, base, center, [(P_aim[key], 1.0)] if key != (0, 0) else [])
        set_static_pose(geo.arm, B, P)
        e = geo.envelope()
        hi[name] = round(max(e["max_z_rod"], e["max_z_arms"]) * 100, 1)
    res["prone_base_max_height_above_eye_cm"] = hi
    return res, P_aim


def fish_checks(geo, poser, fish_obj, grip):
    B = poser.B
    P, m = poser.hold_fish(0)
    set_static_pose(geo.arm, B, P)
    place_fish(geo.arm, fish_obj, grip)
    arms, polys = geo.arms_points()
    fpts, fpolys = mesh_world(fish_obj)
    hands = [p for p, k in zip(arms, geo.hand_mask) if k]
    left = [p for p, v in zip(arms, geo.mesh.data.vertices)
            if any(g.weight > 0.5 and geo.mesh.vertex_groups[g.group].name in ("hand_l", "fingers_l", "thumb_l")
                   for g in v.groups)]
    res = {"wrists": m,
           "fish_verts_inside_arms": inside_count(fpts, BVHTree.FromPolygons(arms, polys)),
           "screen_fish": screen_box(fpts), "screen_hands": screen_box(hands), "screen_left_hand": screen_box(left),
           "fish_grip_m": [round(c, 4) for c in grip],
           "unreal_hand_r_fish_f0": ue_transform(P["hand_r_fish"]),
           "unreal_hand_r_fish_local": ue_transform(B["hand_r"].inverted() @ B["hand_r_fish"])}
    res["wrists_large"] = poser.hold_fish_large(0)[1]
    sizes = {}
    for sc in FISH_SCALES:
        P = fish_size_pose(poser, sc)
        set_basis(geo.arm, P)
        place_fish(geo.arm, fish_obj, grip, sc)
        pts, _pl = mesh_world(fish_obj)
        arms, polys = geo.arms_points()
        box = screen_box(pts)
        # the left palm vs the belly contact of this size: the palm point (hand-derived) vs the fish-derived contact
        F = geo.arm.matrix_world @ geo.arm.pose.bones["hand_r_fish"].matrix
        Pl = geo.arm.matrix_world @ geo.arm.pose.bones["hand_l"].matrix
        palm = (Pl @ poser.B["hand_l"].inverted() @ palm_cradle(poser.sides["l"])).translation
        contact = F @ rot3((1, 0, 0), FISH_CONTACT_ROLL_DEG).to_4x4() @ belly_contact(sc)
        sizes["%.1f" % sc] = {"blend_alpha": round(fish_size_alpha(sc), 3), "x_pct": box.get("x_pct"),
                              "width_pct": round(box["x_pct"][1] - box["x_pct"][0], 1),
                              "top_y_pct": box.get("top_y_pct"),
                              "fish_verts_inside_arms": inside_count(pts, BVHTree.FromPolygons(arms, polys)),
                              "left_palm_to_belly_contact_mm": round((palm - contact).length * 1000, 1)}
    res["sizes"] = sizes
    set_static_pose(geo.arm, B, poser.hold_fish(0)[0])
    place_fish(geo.arm, fish_obj, grip)
    return res


def fish_size_alpha(scale):
    s0 = FISH_POSES["A_FPArms_HoldFish_Idle"]["scale"]
    s1 = FISH_POSES["A_FPArms_HoldFish_Large_Idle"]["scale"]
    return max(0.0, min(1.0, (scale - s0) / (s1 - s0)))


def fish_size_pose(poser, scale, f=0):
    """Pose basis for a fish of `scale`: HoldFish_Idle and HoldFish_Large_Idle crossfaded as Unreal blends them
    (bone-local lerp / shortest-arc nlerp) by fish_size_alpha()."""
    B = poser.B
    return blend_basis(pose_to_basis(B, poser.hold_fish(f)[0]), pose_to_basis(B, poser.hold_fish_large(f)[0]),
                       fish_size_alpha(scale))


def carry_checks(geo, poser, cooler):
    B, sides = poser.B, poser.sides
    P, m = poser.carry(0)
    set_static_pose(geo.arm, B, P)
    C = P["cooler"]
    cooler.place(C)
    arms, polys = geo.arms_points()
    res = {"wrists": m, "arms_verts_inside_cooler_hull": inside_convex(arms, cooler.ucx)}
    res["grip_to_handle_mm"] = {}
    res["hand_in_cooler_space"] = {}
    for s, sock in (("l", "SOCKET_Handle_R"), ("r", "SOCKET_Handle_L")):     # the cooler faces the player
        G = rod_rest_matrix(sides[s])
        grip_w = P["hand_" + s] @ B["hand_" + s].inverted() @ G
        res["grip_to_handle_mm"][s] = round((grip_w.translation - C @ cooler.sockets[sock]).length * 1000, 3)
        res["hand_in_cooler_space"]["hand_" + s] = ue_transform(C.inverted() @ P["hand_" + s])
    res["socket_handle_L_vs_recipe_mm"] = round((cooler.sockets["SOCKET_Handle_L"] - COOLER_HANDLE_L).length * 1000, 3)
    hands = [p for p, k in zip(arms, geo.hand_mask) if k]
    res["screen_cooler"] = screen_box(cooler.points())
    res["screen_hands"] = screen_box(hands)
    res["unreal_cooler_f0"] = ue_transform(C)
    return res


def render_t030_t028_previews(arm_obj, rod_obj, poser, geo, fish_obj, grip, cooler, P_aim):
    """Rod aim grid + simulated blends, HoldFish and CarryCooler: first-person frames and technical views."""
    B = poser.B
    tmp = PREVIEW_DIR / "SK_FPArms_anim_parts"
    tmp.mkdir(parents=True, exist_ok=True)
    full = {}

    def cell(name):
        return tmp / (name + ".png")

    # --- rod aim grid (exported clips, FP)
    fish_obj.hide_render = True
    cooler.hide(True)
    rod_obj.hide_render = False
    cells = []
    for sy in (1, 0, -1):
        for sx in (-1, 0, 1):
            name = AIM_GRID[(sx, sy)]
            play(arm_obj, AIM_ACTIONS[(sx, sy)], 0)
            cells.append(fp_frame(cell("aim_" + name), "day", "RodAim_%s (yaw %+d, pitch %+d)" % (name, sx, sy),
                                  res=CELL))
    full["rodaim"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_rodaim_fp.png", cols=3, cell=CELL)
    play(arm_obj, AIM_ACTIONS[(1, 1)], 0)
    full["rodaim_upright_full"] = fp_frame(PREVIEW_DIR / "SK_FPArms_rodaim_upright_fp.png", "day",
                                           "RodAim_UpRight (fish runs left: rod up and right), day")
    # --- simulated aim-offset blends and side views
    center = P_aim[(0, 0)]
    cells = []
    for u, v, base_name, base_P in ((0.5, 0.5, "HoldRod f0", poser.hold(0)[0]),
                                    (-0.5, 0.75, "HoldRod f45", poser.hold(45)[0]),
                                    (1.0, -0.5, "HoldRod f22", poser.hold(22)[0])):
        P = mesh_additive(B, base_P, center, [(P_aim[k], w) for k, w in aim_weights(u, v)])
        set_static_pose(arm_obj, B, P)
        cells.append(fp_frame(cell("aimblend_%+.2f_%+.2f" % (u, v)), "day",
                              "aim offset blend yaw %+.2f pitch %+.2f on %s" % (u, v, base_name), res=CELL))
    for key in ((0, 1), (0, 0), (0, -1)):
        play(arm_obj, AIM_ACTIONS[key], 0)
        cells.append(shot(cell("aim_side_" + AIM_GRID[key]), (0.7, -2.6, 0.2), (0.7, 0.0, 0.2), ortho=2.2,
                          label="RodAim_%s  side" % AIM_GRID[key]))
    for key in ((-1, 0), (1, 0)):
        play(arm_obj, AIM_ACTIONS[key], 0)
        cells.append(shot(cell("aim_top_" + AIM_GRID[key]), (0.7, 0.0, 3.0), None, ortho=2.4,
                          rotation=(0.0, 0.0, math.radians(-90.0)), label="RodAim_%s  top" % AIM_GRID[key]))
    for key in ((0, 1), (1, 1), (1, -1), (-1, 1)):          # the strongest right-wrist bends, seen from outside
        play(arm_obj, AIM_ACTIONS[key], 0)
        cells.append(shot(cell("aim_out_" + AIM_GRID[key]), (0.55, -0.75, -0.65), (0.3, -0.12, -0.25), lens=30.0,
                          label="RodAim_%s  from below-right" % AIM_GRID[key]))
    full["rodaim_blend"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_rodaim_views.png", cols=3, cell=CELL)
    # --- HoldFish (rod stowed)
    rod_obj.hide_render = True
    fish_obj.hide_render = False
    cells = []
    play(arm_obj, "A_FPArms_HoldFish_Idle", 0)
    place_fish(arm_obj, fish_obj, grip)
    full["holdfish_day"] = fp_frame(PREVIEW_DIR / "SK_FPArms_holdfish_fp.png", "day",
                                    "HoldFish_Idle f0: Bonefish (reference size) on hand_r_fish, day")
    cells.append(fp_frame(cell("fish_fp_00"), "day", "HoldFish_Idle f0  day", res=CELL))
    play(arm_obj, "A_FPArms_HoldFish_Idle", 45)
    place_fish(arm_obj, fish_obj, grip)
    cells.append(fp_frame(cell("fish_fp_45"), "dusk", "HoldFish_Idle f45  dusk", res=CELL))
    for sc in (1.6, 1.3, 0.8):
        set_basis(arm_obj, fish_size_pose(poser, sc))
        place_fish(arm_obj, fish_obj, grip, sc)
        cells.append(fp_frame(cell("fish_fp_x%02d" % int(sc * 10)), "day",
                              "fish %.1fx: HoldFish/_Large blend %.2f" % (sc, fish_size_alpha(sc)), res=CELL))
    play(arm_obj, "A_FPArms_HoldFish_Large_Idle", 0)
    place_fish(arm_obj, fish_obj, grip, 1.6)
    full["holdfish_trophy"] = fp_frame(PREVIEW_DIR / "SK_FPArms_holdfish_large_fp.png", "day",
                                       "HoldFish_Large_Idle f0: Bonefish at scale 1.6 (trophy), day")
    play(arm_obj, "A_FPArms_HoldFish_Idle", 0)
    place_fish(arm_obj, fish_obj, grip)
    g = (arm_obj.pose.bones["hand_r_fish"].matrix).translation
    cells.append(shot(cell("fish_side"), (0.3, -2.2, -0.15), (0.3, 0.0, -0.15), ortho=1.1, label="HoldFish f0  side"))
    cells.append(shot(cell("fish_front"), g + Vector((0.75, 0.25, 0.05)), g, lens=40.0,
                      label="HoldFish f0  from the front (palm cradle)"))
    cells.append(shot(cell("fish_under"), g + Vector((0.25, -0.35, -0.45)), g, lens=40.0,
                      label="HoldFish f0  from below-right"))
    fish_obj.hide_render = True
    cells.append(shot(cell("fish_hand_only"), g + Vector((0.25, -0.35, 0.35)), g, lens=45.0,
                      label="HoldFish f0  hand without the fish"))
    full["holdfish_sheet"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_holdfish.png", cols=3, cell=CELL)
    # --- CarryCooler
    cooler.hide(False)
    cells = []
    play(arm_obj, "A_FPArms_CarryCooler_Idle", 0)
    cooler.place(arm_obj.pose.bones["cooler"].matrix.copy())
    full["carry_day"] = fp_frame(PREVIEW_DIR / "SK_FPArms_carrycooler_fp.png", "day",
                                 "CarryCooler_Idle f0: SM_Cooler_Starter on bone cooler, day")
    cells.append(fp_frame(cell("cool_fp_00"), "day", "CarryCooler_Idle f0  day", res=CELL))
    play(arm_obj, "A_FPArms_CarryCooler_Idle", 45)
    cooler.place(arm_obj.pose.bones["cooler"].matrix.copy())
    cells.append(fp_frame(cell("cool_fp_45"), "dusk", "CarryCooler_Idle f45  dusk", res=CELL))
    play(arm_obj, "A_FPArms_CarryCooler_Idle", 0)
    C = arm_obj.pose.bones["cooler"].matrix.copy()
    cooler.place(C)
    c = C.translation + Vector((0.0, 0.0, 0.2))
    cells.append(shot(cell("cool_side"), (0.2, -2.4, -0.3), (0.2, 0.0, -0.3), ortho=1.3, label="CarryCooler f0  side"))
    cells.append(shot(cell("cool_front"), c + Vector((1.3, 0.45, 0.35)), c + Vector((0.0, 0.0, 0.05)), lens=35.0,
                      label="CarryCooler f0  from the front"))
    hr = C @ COOLER_HANDLE_L                                 # the right fist's handle (the cooler faces the player)
    cells.append(shot(cell("cool_handle_r"), hr + Vector((0.2, -0.35, 0.12)), hr, lens=45.0,
                      label="right fist on Handle_L (cooler faces the player)"))
    cells.append(shot(cell("cool_back"), c + Vector((-0.9, -0.6, 0.9)), c, lens=35.0,
                      label="CarryCooler f0  from above-behind"))
    full["carry_sheet"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_carrycooler.png", cols=3, cell=CELL)
    cooler.hide(True)
    rest_pose(arm_obj)
    return full


# ---------------------------------------------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------------------------------------------
def setup():
    """Scene, mesh, rig, skin and the pose sources (no actions yet). Returns a dict (also used by dev scripts)."""
    global SIDES_R
    pb.reset_scene()
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0

    arms_mod = load_recipe("sk_fp_arms")
    mesh_obj, info = arms_mod.build()
    mesh_bounds = pb.world_bounds([mesh_obj])
    sides = {"l": Side(info, "l"), "r": Side(info, "r")}
    SIDES_R = sides["r"]

    # rig: bones from the guides; hand_l_crank = the left hand's knob grip carried by hand_r_rod
    arm_obj = build_armature(sides)
    B = rest_matrices(arm_obj)
    knob_grip = knob_grip_matrix(B, sides["l"])
    crank_rest = B["hand_r_rod"] @ knob_grip          # rod-space grip mapped onto the rest rod frame
    build_armature(sides, crank_rest)
    # T-030 attach bones: hand_r_fish (the fish's Grip in the right palm), cooler (the carried cooler's pivot)
    build_armature(sides, {"hand_r_fish": fish_rest_matrix(sides["r"]), "cooler": cooler_matrix(0.0)})
    B = rest_matrices(arm_obj)
    add_twist_weights(mesh_obj, sides)
    skin(mesh_obj, arm_obj)
    return {"arms_mod": arms_mod, "mesh_obj": mesh_obj, "mesh_bounds": mesh_bounds, "sides": sides,
            "arm_obj": arm_obj, "B": B, "knob_grip": knob_grip, "poser": Poser(B, sides, knob_grip)}


def main():
    args = pb.parse_args(ASSET, CATEGORY)
    ctx = setup()
    arms_mod, mesh_obj, mesh_bounds, sides = ctx["arms_mod"], ctx["mesh_obj"], ctx["mesh_bounds"], ctx["sides"]
    arm_obj, B, knob_grip, poser = ctx["arm_obj"], ctx["B"], ctx["knob_grip"], ctx["poser"]
    rod_mod = load_recipe("sm_rod_basic")
    wstats = weight_stats(mesh_obj)

    build_actions(arm_obj, poser)
    metrics = {name: poser.source(name)(0)[1] for name, _a, _b in ACTIONS if poser.source(name)}

    motion = motion_check(arm_obj)
    exports, fbx_units = export_all(arm_obj, mesh_obj)
    check = reimport_check(exports, B, mesh_bounds, poser)

    # SM_Rod_Basic staged on hand_r_rod for the checks and previews (zero transform = Copy Transforms of the bone)
    rod_obj, rod_info = rod_mod.build()
    knob_socket = next(c for c in rod_obj.children if c.name == "SOCKET_CrankKnob").location
    con = rod_obj.constraints.new("COPY_TRANSFORMS")
    con.target = arm_obj
    con.subtarget = "hand_r_rod"
    bpy.context.view_layer.update()
    geo = Geo(arm_obj, mesh_obj, rod_obj)
    P_hold0, _ = poser.hold(0)
    hold_screen = screen_metrics(geo, P_hold0, B)
    prone_hold_screen = screen_metrics(geo, poser.prone_hold(0)[0], B)
    tuck_screen = screen_metrics(geo, poser.tuck(0)[0], B)
    prone, hi_frames = prone_checks(geo, poser)
    sheet, full = render_previews(arm_obj, rod_obj, poser, arms_mod, geo, prone, hi_frames)
    rod_obj.hide_render = True

    # T-028 rod aim / T-030 hold fish + carry cooler: checks and previews (fish and cooler staged from the
    # model-artist's recipe / export, preview only)
    aim, P_aim = aim_checks(geo, poser)
    fish_obj, fish_grip = load_fish()
    fish_obj.hide_render = True
    cooler = StagedCooler()
    cooler.hide(True)
    fish = fish_checks(geo, poser, fish_obj, fish_grip)
    carry = carry_checks(geo, poser, cooler)
    full.update(render_t030_t028_previews(arm_obj, rod_obj, poser, geo, fish_obj, fish_grip, cooler, P_aim))

    P_tuck0, _ = poser.tuck(0)
    extra = {
        "exports": exports,
        "fbx_units": fbx_units,
        "skeleton": "SKEL_FPArms (armature object 'Armature' is dropped by Unreal; root bone = 'root')",
        "bones": [{"name": b.name, "parent": b.parent.name if b.parent else None, "deform": b.use_deform,
                   "head_m": [round(c, 4) for c in b.head_local], "tail_m": [round(c, 4) for c in b.tail_local]}
                  for b in arm_obj.data.bones],
        "actions": [{"name": n, "frames": [f0, f1], "seconds": round((f1 - f0) / FPS, 3)} for n, f0, f1 in ACTIONS],
        "weights": wstats,
        "wrist_metrics_f0": metrics,
        "crank_knob_socket_delta_mm": round((Vector(knob_socket) - CRANK_KNOB_ROD).length * 1000, 3),
        "rod_line_tip_m": rod_info.get("tip_m"),
        "unreal_rest": {n: ue_transform(B[n]) for n in ("root", "arms", "hand_r", "hand_r_rod", "hand_l_crank",
                                                         "hand_r_fish", "cooler")},
        "rod_aim": aim,
        "hold_fish": fish,
        "carry_cooler": carry,
        "unreal_holdrod_f0": {n: ue_transform(P_hold0[n]) for n in ("hand_r_rod", "hand_l_crank", "hand_l")},
        "unreal_prone_hold_f0": {n: ue_transform(poser.prone_hold(0)[0][n]) for n in ("hand_r_rod", "hand_l")},
        "unreal_prone_tuck_f0": {n: ue_transform(P_tuck0[n]) for n in ("hand_r_rod", "hand_r", "hand_l")},
        "tuck_hand_r_rod_local_ue": ue_transform(P_tuck0["hand_r"].inverted() @ P_tuck0["hand_r_rod"]),
        "rest_hand_r_rod_local_ue": ue_transform(B["hand_r"].inverted() @ B["hand_r_rod"]),
        "screen_holdrod_f0": hold_screen, "screen_prone_hold_f0": prone_hold_screen, "screen_tuck_f0": tuck_screen,
        "prone": prone,
        "motion_check": motion,
        "reimport_check": check,
        "preview_sheet": sheet,
        "preview_full": full,
    }
    args.out = exports["SK_FPArms"]
    args.preview = sheet
    rest_pose(arm_obj)
    bpy.context.view_layer.update()
    if args.save_blend:
        pb.BLEND_ROOT.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(pb.BLEND_ROOT / "anim_fp_arms.blend"))
    pb.report(args, [mesh_obj], extra)


if __name__ == "__main__":
    main()

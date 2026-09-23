"""anim_fp_arms: rig + animations for SK_FPArms (T-004), built on the model-artist's mesh recipe sk_fp_arms.py.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fp_arms.py
Exports (art/export/Characters/):
  SK_FPArms.fbx             skinned mesh + skeleton in the bind (rest) pose, no animation
  A_FPArms_Idle.fbx         armature only, one take  (loop, frames 0-90)
  A_FPArms_HoldRod_Idle.fbx armature only, one take  (loop, frames 0-90)
  A_FPArms_StanceDip.fbx    armature only, one take  (one-shot, frames 0-8; import as additive)
Spec for Unreal: art/export/Characters/SK_FPArms.anim.md
Preview: Saved/AgentLogs/previews/SK_FPArms_anim.png (contact sheet) + SK_FPArms_anim_fp.png (90 deg FP frame)

Nothing here edits the mesh: build() from sk_fp_arms.py gives the mesh and its vertex groups; this recipe adds the
armature, two forearm-twist vertex groups split off the lowerarm groups, and the actions. SM_Rod_Basic
(sm_rod_basic.build()) is only staged for previews; it is not part of any export.

Axes: Blender +X forward (= Unreal +X), +Y left (= Unreal -Y), Z up, origin = camera/eye point (see sk_fp_arms.py).

HOW THE MOTION IS MADE (deterministic: a rerun gives the same rig and keys)
Every frame is solved analytically: shoulder offsets, a wrist target and a hand orientation per arm go through a
two-bone IK with a fixed elbow pole; the forearm twist bone takes TWIST_ALPHA of the hand's roll about the forearm;
fingers/thumb get rest-space rotations about their joints. Pose matrices are converted to bone-local transforms and
keyed on every frame (linear). In HoldRod_Idle the right hand is solved FROM the rod transform and the left hand FROM
the crank knob, so neither grip can slide.

THE ROD GRIP (why the numbers below; found by searching grip tilt/roll, rod yaw/pitch and elbow poles, then judged
on cross-section renders): the mitten has ONE finger block, so it only closes around a rod that runs within ~30 deg
of its knuckle line. Tilts of 45-60 deg (the diagonal of a real grip) leave the block curled beside the rod. So the rod
sits in the fist's channel tilted GRIP_TILT_DEG (the fingers curl about that same axis) and points straight ahead,
tip up ROD_PITCH_DEG, on the right half of the view (the water in the middle, where the bobber lands, stays clear).
The price, invisible in first person because it is below the bottom edge of the frame: the right wrist sits in a strong
ulnar bend (~85 deg; no flexion, no forearm twist) and the rod butt rests along the underside of the forearm (~3 cm
overlap, as a real butt presses against the forearm). The FP arms float at camera height, so the elbow always hangs
below the fist; no pose within reach gives a neutral wrist, a closed fist and a clear butt at once.
"""
import importlib.util
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

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

PARENT = {"root": None, "arms": "root", "hand_r_rod": "hand_r", "hand_l_crank": "hand_r_rod"}
for _s in ("l", "r"):
    PARENT.update({"upperarm_" + _s: "arms", "lowerarm_" + _s: "upperarm_" + _s,
                   "lowerarm_twist_" + _s: "lowerarm_" + _s, "hand_" + _s: "lowerarm_" + _s,
                   "thumb_" + _s: "hand_" + _s, "fingers_" + _s: "hand_" + _s})

# ---------------------------------------------------------------------------------------------------------------
# Actions (30 fps). Loops: the last frame equals the first (Unreal plays 0..N and wraps without a hitch).
# ---------------------------------------------------------------------------------------------------------------
LOOP_FRAMES = 90                    # 3.0 s = one slow breath
DIP_FRAMES = 8                      # 0.267 s
ACTIONS = [("A_FPArms_Idle", 0, LOOP_FRAMES), ("A_FPArms_HoldRod_Idle", 0, LOOP_FRAMES),
           ("A_FPArms_StanceDip", 0, DIP_FRAMES)]

# HoldRod_Idle: the rod (= hand_r_rod) in camera space: grip point, tip raised ROD_PITCH, turned ROD_YAW left.
ROD_GRIP = Vector((0.43, -0.16, -0.20))
ROD_PITCH_DEG = 36.0
ROD_YAW_DEG = 0.0
ROD_ROLL_DEG = 0.0
RIGHT_SHOULDER_HOLD = Vector((0.02, 0.0, 0.0))
RIGHT_POLE_HOLD = Vector((0.0, -0.35, -1.0))       # right elbow down and a little out (the rest-pose pole)
LEFT_SHOULDER_HOLD = Vector((0.07, -0.06, 0.0))   # protraction: the left arm reaches across to the crank
LEFT_POLE_HOLD = Vector((0.0, 1.0, -0.6))          # left elbow out to the side (small forearm twist)
GRIP_CURL_DEG = 86.0                # fingers closed around the rod grip
KNOB_CURL_DEG = 80.0

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

M3 = Matrix.Diagonal((1.0, -1.0, 1.0))  # mirror across Y = 0 (3x3)
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


def rod_rest_matrix(sd):
    """hand_r_rod rest matrix (armature space): X = rod tip direction, Y = rod left, Z = rod up, at the grip point.
    Its frame is exactly SM_Rod_Basic's local frame, so the rod attached with a zero transform sits in the fist."""
    X = tilted(sd.uh, sd.th, GRIP_TILT_DEG)
    Z = sd.vh * math.cos(math.radians(GRIP_ROLL_DEG)) - sd.th * math.sin(math.radians(GRIP_ROLL_DEG))
    Z = (Z - X * Z.dot(X)).normalized()
    Y = Z.cross(X)
    return mat4(Matrix((X, Y, Z)).transposed(), sd.grip)


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
    else:
        t = crank_rest.translation
        add("hand_l_crank", t, t + crank_rest.col[1].xyz * SMALL_BONE, "hand_r_rod", deform=False, matrix=crank_rest)
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm_obj


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
            "wrist_dev_deg": round(dev, 1), "elbow_bend_deg": round(elbow, 1)}


def full_pose(B, sides, arms_M, targets):
    """Pose matrices for every bone. arms_M: pose matrix of the 'arms' bone. targets: {'l': ArmTarget, 'r': ...}."""
    P = {"root": B["root"].copy(), "arms": arms_M}
    arms_delta = arms_M @ B["arms"].inverted()
    metrics = {s: solve_arm(sides[s], B, P, targets[s], arms_delta) for s in ("l", "r")}
    P["hand_r_rod"] = P["hand_r"] @ B["hand_r"].inverted() @ B["hand_r_rod"]
    if "hand_l_crank" in B:
        P["hand_l_crank"] = P["hand_r_rod"] @ B["hand_r_rod"].inverted() @ B["hand_l_crank"]
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


def holdrod_rod(t):
    """The rod (hand_r_rod) world matrix at time t: a slow sway, one breath per loop."""
    w = loop_w()
    grip = ROD_GRIP + Vector((0.003 * math.sin(w * t - 0.3), 0.002 * math.sin(2.0 * w * t),
                              0.007 * math.sin(w * t - 0.5)))
    return rod_matrix(ROD_PITCH_DEG + 1.2 * math.sin(w * t - 0.9), ROD_YAW_DEG + 0.8 * math.sin(w * t + 0.4),
                      ROD_ROLL_DEG, grip)


def knob_grip_matrix(B, sd):
    """hand_l bone matrix in ROD space when the left fist holds the crank knob."""
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    back = Z * math.cos(math.radians(KNOB_ROLL_DEG)) - X * math.sin(math.radians(KNOB_ROLL_DEG))
    R = basis_yz(-Y, back) @ basis_yz(tilted(sd.uh, sd.th, KNOB_TILT_DEG), sd.vh).transposed()
    center = CRANK_KNOB_ROD + Y * KNOB_IN_FIST_M
    return Matrix.Translation(center) @ R.to_4x4() @ Matrix.Translation(-sd.grip) @ B["hand_l"]


def holdrod_targets(B, sides, t, knob_grip):
    """Right hand from the rod, left hand from the crank knob (knob_grip = hand_l matrix in rod space)."""
    w = loop_w()
    R_rod = holdrod_rod(t)
    breath = Vector((0.0, 0.0, 0.004 * math.sin(w * t)))
    P_hr = R_rod @ B["hand_r_rod"].inverted() @ B["hand_r"]
    P_hl = R_rod @ knob_grip
    return {"r": ArmTarget(P_hr.translation, P_hr.to_3x3(), RIGHT_SHOULDER_HOLD + breath, GRIP_CURL_DEG,
                           GRIP_TILT_DEG, thumb_rot(THUMB_GRIP), pole=RIGHT_POLE_HOLD),
            "l": ArmTarget(P_hl.translation, P_hl.to_3x3(), LEFT_SHOULDER_HOLD + breath, KNOB_CURL_DEG,
                           KNOB_TILT_DEG, thumb_rot(THUMB_KNOB), pole=LEFT_POLE_HOLD)}


def dip_arms_matrix(f):
    """'arms' bone pose for StanceDip frame f: drop, pitch forward about the chest pivot, overshoot, settle."""
    loc = ARMS_PIVOT + Vector((DIP_X_CM[f], 0.0, DIP_Z_CM[f])) * 0.01
    return mat4(rot3((0, 1, 0), DIP_PITCH_DEG[f]), loc)


class Poser:
    """Pose sources for the actions and the previews."""

    def __init__(self, B, sides, knob_grip):
        self.B, self.sides, self.knob_grip = B, sides, knob_grip

    def idle(self, f):
        return full_pose(self.B, self.sides, self.B["arms"], idle_targets(self.B, self.sides, f / FPS))

    def hold(self, f, arms_M=None):
        arms_M = self.B["arms"] if arms_M is None else arms_M
        P, m = full_pose(self.B, self.sides, self.B["arms"], holdrod_targets(self.B, self.sides, f / FPS,
                                                                                self.knob_grip))
        if arms_M is not self.B["arms"]:       # preview: the additive dip applied on top (moves everything rigidly)
            D = arms_M @ self.B["arms"].inverted()
            P = {n: (M if n == "root" else D @ M) for n, M in P.items()}
        return P, m

    def dip_basis(self, f):
        basis = {n: Matrix.Identity(4) for n in self.B}
        basis["arms"] = self.B["arms"].inverted() @ dip_arms_matrix(f)
        return basis


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
    makers = {
        "A_FPArms_Idle": lambda f: pose_to_basis(B, poser.idle(f)[0]),
        "A_FPArms_HoldRod_Idle": lambda f: pose_to_basis(B, poser.hold(f)[0]),
        "A_FPArms_StanceDip": poser.dip_basis,
    }
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


# ---------------------------------------------------------------------------------------------------------------
# Export + re-import check
# ---------------------------------------------------------------------------------------------------------------
def export_skeletal(path, objs, bake):
    pb.ensure_fbx_exporter()
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    pb.select_only(objs)
    kw = dict(filepath=str(path), use_selection=True, object_types={o.type for o in objs},
              apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", global_scale=1.0,
              axis_forward="-Z", axis_up="Y", use_mesh_modifiers=True, mesh_smooth_type="FACE",
              add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X",
              use_armature_deform_only=False, armature_nodetype="NULL", bake_anim=bake)
    if bake:
        kw.update(bake_anim_use_all_bones=True, bake_anim_use_nla_strips=True, bake_anim_use_all_actions=False,
                  bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0)
    bpy.ops.export_scene.fbx(**kw)
    return str(path)


def export_all(arm_obj, mesh_obj):
    ad = arm_obj.animation_data
    ad.action = None
    for tr in ad.nla_tracks:
        tr.mute = True
    rest_pose(arm_obj)
    bpy.context.scene.frame_set(0)
    out = {"SK_FPArms": export_skeletal(EXPORT_DIR / "SK_FPArms.fbx", [arm_obj, mesh_obj], bake=False)}
    for name, _f0, _f1 in ACTIONS:
        for tr in ad.nla_tracks:
            tr.mute = tr.name != name
        out[name] = export_skeletal(EXPORT_DIR / (name + ".fbx"), [arm_obj], bake=True)
    for tr in ad.nla_tracks:
        tr.mute = True
    rest_pose(arm_obj)
    return out


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
    returns to rest)."""
    probes = [("hand_r", "head"), ("hand_l", "head"), ("fingers_r", "tail"), ("fingers_l", "tail"), ("thumb_r", "tail"),
              ("thumb_l", "tail"), ("hand_r_rod", "head"), ("hand_l_crank", "head"), ("lowerarm_r", "head"),
              ("lowerarm_l", "head")]
    out = {}
    for name, f0, f1 in ACTIONS:
        pos = []
        for f in range(f0, f1 + 1):
            play(arm_obj, name, f)
            pos.append([(arm_obj.matrix_world @ getattr(arm_obj.pose.bones[b], end)).copy() for b, end in probes])
        steps = [max((a - b).length for a, b in zip(pos[i + 1], pos[i])) for i in range(len(pos) - 1)]
        out[name] = {"max_step_mm": round(max(steps) * 1000, 2),
                     "first_last_delta_mm": round(max((a - b).length for a, b in zip(pos[-1], pos[0])) * 1000, 3),
                     "loop_seam_step_mm": round(max((a - b).length for a, b in zip(pos[1], pos[0])) * 1000, 2)}
    return out


def reimport_check(exports, B, mesh_bounds, poser):
    """Re-import every FBX: bone positions/axes, mesh bounds and baked poses must match the source."""
    scene = bpy.context.scene
    fps = scene.render.fps
    res = {}
    snap = snapshot()
    arm, mesh = import_fbx(exports["SK_FPArms"])
    mw = arm.matrix_world
    head_err = max((mw @ arm.data.bones[n].head_local - B[n].translation).length for n in B)
    rot_err = max(math.degrees((mw.to_3x3().normalized() @ arm.data.bones[n].matrix_local.to_3x3()).to_quaternion()
                               .rotation_difference(B[n].to_3x3().to_quaternion()).angle) for n in B)
    mn, mx = pb.world_bounds([mesh])
    res["SK_FPArms"] = {
        "bones": len(arm.data.bones), "armature_object_scale": [round(c, 4) for c in mw.to_scale()],
        "max_bone_head_error_mm": round(head_err * 1000, 3), "max_bone_axis_error_deg": round(rot_err, 3),
        "mesh_min_m": [round(c, 4) for c in mn], "mesh_max_m": [round(c, 4) for c in mx],
        "source_min_m": [round(c, 4) for c in mesh_bounds[0]], "source_max_m": [round(c, 4) for c in mesh_bounds[1]],
        "skinned_groups": len(mesh.vertex_groups),
    }
    cleanup_since(snap)
    checks = {"A_FPArms_Idle": (poser.idle, [0, 45, 90]), "A_FPArms_HoldRod_Idle": (poser.hold, [0, 30, 60, 90])}
    for name, _f0, f1 in ACTIONS:
        snap = snapshot()
        arm, _m = import_fbx(exports[name])
        act = arm.animation_data.action if arm.animation_data else None
        entry = {"action": act.name if act else None,
                 "frame_range": [round(c, 2) for c in act.frame_range] if act else None}
        if name in checks:
            fn, frames = checks[name]
            err = 0.0
            for f in frames:
                scene.frame_set(f)
                P, _m2 = fn(f)
                for n in ("hand_r", "hand_l", "hand_r_rod", "fingers_r", "thumb_l"):
                    err = max(err, (arm.matrix_world @ arm.pose.bones[n].head - P[n].translation).length)
            entry["max_pose_error_mm"] = round(err * 1000, 3)
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
# Previews
# ---------------------------------------------------------------------------------------------------------------
CELL = (640, 360)


def shot(path, location, target, lens=35.0, ortho=None, res=CELL, world_hex="#3A3A3A", label=None,
         view_transform=None):
    """Workbench render with an optional label in the top-left corner."""
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
    cam.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    helpers = [cam]
    if label:
        d = 0.05
        hw = ortho / 2.0 if ortho else d * 18.0 / lens
        hh = hw * res[1] / res[0]
        size = hh * 0.12
        curve = bpy.data.curves.new("PV_LabelCurve", "FONT")
        curve.body = label
        curve.size = size
        txt = bpy.data.objects.new("PV_Label", curve)
        scene.collection.objects.link(txt)
        txt.parent = cam
        txt.location = (-hw * 0.96, hh * 0.96 - size * 0.8, -d)
        curve.materials.append(style.make_material("PV_LabelMat", style.UI.PARCHMENT, "flat"))
        helpers.append(txt)
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(path)
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_cavity = True
    if scene.world is None:
        scene.world = bpy.data.worlds.new("PV_World")
    scene.world.color = style.linear(world_hex)
    old_vt = scene.view_settings.view_transform
    if view_transform:
        scene.view_settings.view_transform = view_transform
    bpy.ops.render.render(write_still=True)
    scene.view_settings.view_transform = old_vt
    for h in helpers:
        data = h.data
        bpy.data.objects.remove(h, do_unlink=True)
        if isinstance(data, bpy.types.Camera):
            bpy.data.cameras.remove(data)
        elif data is not None:
            bpy.data.curves.remove(data)
    return str(path)


def fp_shot(path, label, res=CELL):
    return shot(path, (0, 0, 0), (1, 0, 0), lens=pb.lens_for_hfov(90.0), res=res, world_hex=style.TROPICAL.SKY_DAY,
                label=label, view_transform="Standard")


def render_previews(arm_obj, rod_obj, poser, arms_mod):
    """Contact sheet (4 columns): Idle, HoldRod_Idle (with SM_Rod_Basic), StanceDip, dip on HoldRod, weight test."""
    B, sides = poser.B, poser.sides
    tmp = PREVIEW_DIR / "SK_FPArms_anim_parts"
    tmp.mkdir(parents=True, exist_ok=True)
    cells = []

    def cell(name):
        return tmp / (name + ".png")

    side_cam = dict(location=(0.25, -2.0, -0.18), target=(0.25, 0.0, -0.18), ortho=0.85)
    water_cleanup = arms_mod.stage_water()
    water = [o for o in bpy.data.objects if o.name.startswith("Plane")]

    def show_water(on):
        for o in water:
            o.hide_render = not on

    # --- Idle (no rod)
    rod_obj.hide_render = True
    for f in (0, 45):
        play(arm_obj, "A_FPArms_Idle", f)
        show_water(True)
        cells.append(fp_shot(cell("idle_fp_%02d" % f), "Idle f%d  FP 90" % f))
    show_water(False)
    for f in (0, 45):
        play(arm_obj, "A_FPArms_Idle", f)
        cells.append(shot(cell("idle_side_%02d" % f), label="Idle f%d  side" % f, **side_cam))
    # --- HoldRod_Idle (rod attached to hand_r_rod with a zero transform)
    rod_obj.hide_render = False
    for f in (0, 45):
        play(arm_obj, "A_FPArms_HoldRod_Idle", f)
        show_water(True)
        cells.append(fp_shot(cell("hold_fp_%02d" % f), "HoldRod_Idle f%d  FP 90" % f))
        if f == 0:
            fp_shot(PREVIEW_DIR / "SK_FPArms_anim_fp.png", "HoldRod_Idle f0, 90 deg hFOV, rod on hand_r_rod",
                    res=(1280, 720))
    show_water(False)
    play(arm_obj, "A_FPArms_HoldRod_Idle", 0)
    P0, _ = poser.hold(0)
    Rr = P0["hand_r_rod"]
    g = Rr.translation
    X, Y, Z = (Rr.to_3x3().col[i].normalized() for i in range(3))
    kw = Rr @ CRANK_KNOB_ROD
    cells.append(shot(cell("hold_grip_top"), g + (Z * 0.9 - Y * 0.5 + X * 0.25).normalized() * 0.55, g + X * 0.03,
                      lens=50.0, label="grip R, from above-right"))
    cells.append(shot(cell("hold_grip_under"), g + (-Z * 0.8 - Y * 0.6 + X * 0.35).normalized() * 0.55,
                      g + X * 0.03, lens=50.0, label="grip R, from below-right (fingers)"))
    cells.append(shot(cell("hold_grip_front"), g + (X * 0.9 - Y * 0.35 + Z * 0.1).normalized() * 0.55, g,
                      lens=50.0, label="grip R, from the rod tip side"))
    cells.append(shot(cell("hold_knob_l"), kw + (Y * 0.8 + Z * 0.3 + X * 0.35).normalized() * 0.55, kw, lens=50.0,
                      label="L fist on crank knob"))
    cells.append(shot(cell("hold_34"), (1.05, 0.45, 0.10), (0.45, -0.08, -0.24), lens=35.0,
                      label="HoldRod_Idle f0, front-left"))
    cells.append(shot(cell("hold_side"), label="HoldRod_Idle f0  side", location=(0.45, -2.2, 0.05),
                      target=(0.45, 0.0, 0.05), ortho=1.4))
    # --- StanceDip as authored (empty hands, rest arms) + applied on HoldRod
    rod_obj.hide_render = True
    for f in (0, 3, 6):
        play(arm_obj, "A_FPArms_StanceDip", f)
        cells.append(shot(cell("dip_side_%02d" % f), label="StanceDip f%d (clip)" % f, **side_cam))
    rod_obj.hide_render = False
    show_water(True)
    for f in (0, 3, 6):
        P, _ = poser.hold(0, arms_M=dip_arms_matrix(f))
        set_static_pose(arm_obj, B, P)
        cells.append(fp_shot(cell("dip_hold_fp_%02d" % f), "HoldRod + StanceDip f%d" % f))
    show_water(False)
    # --- weight test: extremes (not exported)
    rod_obj.hide_render = True
    for i, (P, lab, cam) in enumerate(weight_test_poses(poser)):
        set_static_pose(arm_obj, B, P)
        cells.append(shot(cell("weights_%d" % i), cam[0], cam[1], lens=35.0, label=lab))
    water_cleanup()
    rest_pose(arm_obj)
    sheet = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_anim.png", cols=4, cell=CELL)
    return sheet, cells


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
# main
# ---------------------------------------------------------------------------------------------------------------
def main():
    global SIDES_R
    args = pb.parse_args(ASSET, CATEGORY)
    pb.reset_scene()
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0

    arms_mod = load_recipe("sk_fp_arms")
    rod_mod = load_recipe("sm_rod_basic")
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
    B = rest_matrices(arm_obj)
    add_twist_weights(mesh_obj, sides)
    skin(mesh_obj, arm_obj)
    wstats = weight_stats(mesh_obj)

    poser = Poser(B, sides, knob_grip)
    build_actions(arm_obj, poser)
    _P, hold_metrics = poser.hold(0)
    _P, idle_metrics = poser.idle(0)

    motion = motion_check(arm_obj)
    exports = export_all(arm_obj, mesh_obj)
    check = reimport_check(exports, B, mesh_bounds, poser)

    # SM_Rod_Basic staged on hand_r_rod for the previews (zero transform = Copy Transforms of the bone)
    rod_obj, _rod_info = rod_mod.build()
    con = rod_obj.constraints.new("COPY_TRANSFORMS")
    con.target = arm_obj
    con.subtarget = "hand_r_rod"
    sheet, _cells = render_previews(arm_obj, rod_obj, poser, arms_mod)
    rod_obj.hide_render = True

    P_hold0, _ = poser.hold(0)
    extra = {
        "exports": exports,
        "skeleton": "SKEL_FPArms (armature object 'Armature' is dropped by Unreal; root bone = 'root')",
        "bones": [{"name": b.name, "parent": b.parent.name if b.parent else None, "deform": b.use_deform,
                   "head_m": [round(c, 4) for c in b.head_local], "tail_m": [round(c, 4) for c in b.tail_local]}
                  for b in arm_obj.data.bones],
        "actions": [{"name": n, "frames": [f0, f1], "seconds": round((f1 - f0) / FPS, 3)} for n, f0, f1 in ACTIONS],
        "weights": wstats,
        "hold_metrics": hold_metrics, "idle_metrics": idle_metrics,
        "unreal_rest": {n: ue_transform(B[n]) for n in ("root", "arms", "hand_r", "hand_r_rod", "hand_l_crank")},
        "unreal_holdrod_f0": {n: ue_transform(P_hold0[n]) for n in ("hand_r_rod", "hand_l_crank", "hand_l")},
        "motion_check": motion,
        "reimport_check": check,
        "preview_sheet": sheet,
        "preview_fp": str(PREVIEW_DIR / "SK_FPArms_anim_fp.png"),
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

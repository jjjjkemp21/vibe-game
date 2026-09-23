"""Shared fish rig + parametric motion for every fishkit fish (T-008, animation-artist). Used by art/recipes/anim_fish.py.

ONE SKELETON FOR ALL FISHKIT SPECIES (Unreal: SKEL_Fish)
Every species built with art/lib/fishkit.py gets the same bones, hierarchy and rest ORIENTATIONS; only the bone
POSITIONS differ (they come from the species' own mesh). Adding a species = its mesh recipe + one line in
anim_fish.py SPECIES; nothing here changes.

    root            (0,0,0) = the mesh pivot (body center on the spine line); never animated, no deform
    +- Spine_01     chest, pivot at joint J1 (s 0.33). THE ANCHOR: no clip ever yaws or pitches it (roll only)
       +- Head      pivot J1: head yaw/pitch about the neck (head shakes, recoil, the curl)
       |  +- Mouth  no deform: the species' nose tip (was SOCKET_Mouth). Line / hook attach, frame = fish frame
       +- Fin_Pectoral_L  pivot = the fin's root chord center; flaps about PEC_HINGE_L (flare +, tuck -)
       +- Fin_Pectoral_R  mirror
       +- Grip      no deform: chest center on the spine line (s 0.42). Hand / display attach (steady in every clip)
       +- Spine_02  J2 (s 0.51) -> Spine_03 J3 (0.69) -> Spine_04 J4 (0.86) -> Tail J5 (1.0, tail root)

WHY THE PECTORALS HANG FROM THE CHEST, NOT THE HEAD (measured 2026-09-23, RESULT_JSON metrics): the fin root sits
~2 cm in front of the neck pivot J1 but the blade lies ~3 cm behind it, against flank that is 60-90 % chest-weighted.
Under Head, every head yaw swept the fin's pivot sideways: in a 22 deg curl the concave-side blade cut up to 16 mm into
the flank and the convex-side blade dropped 9 mm below the dock plane (Landed_Flop). No head-follow share fixes both,
because a rigid blade can cancel the pivot's sweep at its middle or at its tip, not at both (a share of 0.4-0.6 still
left 2-4 mm either way). Under Spine_01 the blade tracks the flank it lies on: worst penetration 0.3 mm, dock drop
0.0 mm, and any head-follow share only made both worse (0.1: 1.0 mm / 2.1 mm). The cost is that the fin's root strip
(root station to the 40 % station) shears when the head turns hard; RESULT_JSON pec_strip_strain_max reports it.

s = fishkit body station (0 nose tip, 1 tail root; the tail fin tips are at s ~1.29). u = s / S_TIP_CANON is the
position along the whole fish (0 nose tip, 1 tail tips); the motion model works in u.

INVARIANTS (the reasons the clips can be shared; keep them when extending)
1. Identity rest frames: every bone's armature-space rest rotation is the identity (Blender bone pointing +Y, roll 0),
   so in Unreal every bone's reference frame is the fish frame (X forward, Y right, Z up). Local yaw = lateral bend
   (about Z), pitch = about Y, roll = about X, for every bone and every species.
2. Rotation-only clips: no clip keys a location. Joint positions (bone lengths) come from each species' own mesh.
3. Additive import: Unreal imports every clip as additive (Local Space) relative to A_Fish_Rest, so the translation
   deltas are exactly zero and each mesh keeps its own proportions under the shared clips (see SK_Fish.anim.md).
4. The chest (Spine_01, Grip) never moves in any clip (roll aside, which keeps the spine line): attach a hand or a
   display point at Grip and the head and tail swing around it.

SKIN (skin(): linear blend, max 3 influences, sums 1.0)
Every vertex of the body, the median fins (dorsal, anal, caudal), the pelvic fins and the eyes is weighted by its X
position alone (hat functions between the bone centers WEIGHT_CENTERS_S). Parts at the same X therefore move exactly
like the body there: fins never separate from the body, whatever the bend. Pectoral fins: the root station keeps the
body weights at the root; the blade ramps to its Fin_Pectoral bone over PEC_BLADE_RAMP of the fin length (the root stays
glued, the blade flaps). With fishkit's fin stations (root, 40 %, 75 %, tip) the first blade station already lies past
the ramp, so the fin hinges on its root strip (max 2 influences); the ramp only matters for fins with denser stations.

MOTION (Pose + clip functions below; everything is a function of the frame, nothing is hand-keyed)
- wave:  a traveling wave y(u, t) = A E(u) sin(2 pi (c - u / lambda)) of the midline (lateral displacement in fish
         lengths, E = carangiform envelope, narrow at the chest, widest at the tail). Converted to bone yaws from the
         chords between the joints, then turned so the chest stays still. The ambient WPO swim material uses the same
         wave with the same chest-still correction (wpo_lateral(); SK_Fish.anim.md "Ambient swim"), so a static fish
         and a skinned fish look alike.
- curl:  a C-bend of strength k (k = 1: head +30 deg, tail tip -64 deg; k > 0 = concave to the fish's LEFT).
- head:  direct head yaw / pitch (shakes, dive tilt); body reacts to head shakes with a short lag.
- roll:  whole-fish roll about the spine line (on Spine_01).
- pecs:  pectoral flap angles (deg): + flare out, - tuck (TUCK_DEG = the blade touches the flank; never go past it).
         Every pose adds PEC_TURN_FLARE x the head yaw to the fin on the inside of the turn (clearance).
Choreographed channels (curl strength, head shake envelope, roll) are Keys: monotone cubic splines, periodic over the
loop, flat at extremes and holds (like Blender's auto-clamped handles), so a keyed hold never drifts and a curl that
is keyed >= 0 never dips below 0 (Landed_Flop stays dock-safe).
Loops: frames 0..N with pose(N) == pose(0) (Unreal plays 0..N and wraps without a hitch).

To add a clip: write a function f -> Pose in the CLIPS section and add a ClipDef to CLIPS. To add a body type (eel, ray,
shark): make a new rig module; don't bend this one.
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector
from mathutils.bvhtree import BVHTree

import pipeline_blender as pb

DEG = math.pi / 180.0
FPS = 30

# ---------------------------------------------------------------------------------------------------------------
# Skeleton
# ---------------------------------------------------------------------------------------------------------------
JOINT_S = (0.33, 0.51, 0.69, 0.86, 1.0)      # J1..J5 on fishkit body stations (same for every species)
GRIP_S = 0.42
S_TIP_CANON = 1.29                            # tail-tip station of the reference fish (1.297 bonefish, 1.279 snapper)
U_JOINTS = tuple(s / S_TIP_CANON for s in JOINT_S)
CHAIN = ("Head", "Spine_01", "Spine_02", "Spine_03", "Spine_04", "Tail")   # nose -> tail
POSTERIOR = ("Spine_02", "Spine_03", "Spine_04", "Tail")
BONES = (  # name, parent, deform
    ("root", None, False),
    ("Spine_01", "root", True),
    ("Head", "Spine_01", True),
    ("Mouth", "Head", False),
    ("Fin_Pectoral_L", "Spine_01", True),
    ("Fin_Pectoral_R", "Spine_01", True),
    ("Grip", "Spine_01", False),
    ("Spine_02", "Spine_01", True),
    ("Spine_03", "Spine_02", True),
    ("Spine_04", "Spine_03", True),
    ("Tail", "Spine_04", True),
)
PARENT = {n: p for n, p, _d in BONES}
DEFORM = tuple(n for n, _p, d in BONES if d)
# Weight hat functions: bone -> the station where it has weight 1 (linear blend to the neighbours' centers).
# Head is rigid to s 0.27 (just behind the gill cover), Tail rigid from s 1.04 (just behind the tail root).
WEIGHT_CENTERS_S = (("Head", 0.27), ("Spine_01", 0.42), ("Spine_02", 0.60), ("Spine_03", 0.775),
                    ("Spine_04", 0.93), ("Tail", 1.04))
# Pectoral hinge (left fin; the right one is mirrored): the root chord direction, measured on both slice species
# (bonefish (-0.346,-0.340,-0.874), snapper (-0.297,-0.342,-0.892), 2.9 deg apart). A shared clip rotates every
# species' fin about this one axis; rig_species() warns if a species' chord is more than PEC_HINGE_TOL_DEG off.
PEC_HINGE_L = Vector((-0.3218, -0.3411, -0.8832)).normalized()
PEC_HINGE_TOL_DEG = 10.0
PEC_BLADE_RAMP = 0.35        # share of the fin length over which the blade blends from body weights to the fin bone
BONE_DISPLAY_M = 0.02        # bone tails point +Y (identity frame); length is display only


def mirror_y(v):
    return Vector((v[0], -v[1], v[2]))


# fishkit's part groups (Body, Fin_Pectoral_L, ...) are renamed Part_<name> before skinning: Fin_Pectoral_L/R are also
# bone names, and the FBX exporter writes every vertex group named like a bone as skin weights (a part group of weight
# 1.0 would pin the fin root to the fin bone). The Part_ groups stay on the mesh for the checks; the exporter skips
# them (the re-import check lists exactly the 8 deform groups).
PART_PREFIX = "Part_"


def part(name):
    return PART_PREFIX + name


def rename_part_groups(mesh_obj):
    for g in mesh_obj.vertex_groups:
        if not g.name.startswith(PART_PREFIX):
            g.name = PART_PREFIX + g.name


class SpeciesGeo:
    """Rest-pose landmarks of one fishkit fish, from the mesh and build()'s rig info (meters, final scale)."""

    def __init__(self, mesh_obj, info, species):
        self.species = species
        self.mesh = mesh_obj
        bones = info["suggested_bones_m"]
        # joints J1..J5 = the tails of Head, Spine_01..Spine_04 in the model-artist's suggested chain; snapped onto
        # the spine line Z = 0 (the kit keeps the ring centers within a few mm of it) so every rest frame is identical
        self.joint_x = [bones[b]["tail"][0] for b in ("Head", "Spine_01", "Spine_02", "Spine_03", "Spine_04")]
        self.nose_x = bones["Head"]["head"][0]
        self.tip_x = bones["Tail"]["tail"][0]
        self.body_len = (self.joint_x[0] - self.joint_x[4]) / (JOINT_S[4] - JOINT_S[0])   # nose tip -> tail root
        self.s_tip = (self.nose_x - self.tip_x) / self.body_len
        self.length = self.nose_x - self.tip_x
        for x, s in zip(self.joint_x, JOINT_S):   # the suggested joints must sit on the standard stations
            if abs(self.s_of_x(x) - s) > 0.002:
                raise ValueError("%s: joint at x=%.4f is s=%.4f, expected %.2f (fishkit JOINTS_S changed?)"
                                 % (species, x, self.s_of_x(x), s))
        # (a second fish in the scene gets SOCKET_Mouth.001: match the base name)
        sock = next((c for c in mesh_obj.children if c.name.split(".")[0] == pb.SOCKET_PREFIX + "Mouth"), None)
        if sock is None:
            raise ValueError("%s: no SOCKET_Mouth on the mesh" % species)
        self.mouth = Vector(sock.location)
        self.grip = Vector((self.x_of_s(GRIP_S), 0.0, 0.0))
        self.pecs = {side: find_pectoral(mesh_obj, side) for side in ("L", "R")}

    def s_of_x(self, x):
        return (self.nose_x - x) / self.body_len

    def x_of_s(self, s):
        return self.nose_x - s * self.body_len

    def joint(self, i):
        return Vector((self.joint_x[i], 0.0, 0.0))


def group_members(obj, name, min_w=0.5):
    gi = obj.vertex_groups[name].index
    return [v.index for v in obj.data.vertices if any(g.group == gi and g.weight >= min_w for g in v.groups)]


def find_pectoral(obj, side):
    """Root chord, hinge and tip of a fishkit pectoral fin (fishkit.paired_fin -> fin_loft): the loft creates its
    stations in order, root ring first and the tip pole last, so the island's lowest 4 vertex indices are the root
    station and the highest is the tip. Checked geometrically (the tip is the farthest vertex from the root; the root
    sits on the body within 6 mm), because a swept-back fin's second station also lies close to the flank."""
    idx = sorted(group_members(obj, part("Fin_Pectoral_" + side)))
    me = obj.data
    root, tip_i = idx[:4], idx[-1]
    pts = [me.vertices[i].co.copy() for i in root]
    c = sum(pts, Vector()) / len(pts)
    tip = me.vertices[tip_i].co.copy()
    if max(idx, key=lambda i: (me.vertices[i].co - c).length) != tip_i:
        raise ValueError("pectoral %s: the last vertex is not the tip; fishkit fin_loft order changed?" % side)
    # hinge = principal axis of the root station (the chord A-B; the minor axis is the fin thickness)
    import numpy as np
    P = np.array([[p.x - c.x, p.y - c.y, p.z - c.z] for p in pts])
    _w, V = np.linalg.eigh(P.T @ P)
    axis = Vector(V[:, 2]).normalized()
    if axis.z > 0.0:
        axis = -axis
    canon = PEC_HINGE_L if side == "L" else mirror_y(PEC_HINGE_L)
    off = math.degrees(axis.angle(canon))
    return {"root": root, "blade": [i for i in idx if i not in root], "center": c, "hinge": axis, "tip": tip,
            "length": (tip - c).length, "hinge_off_canon_deg": round(off, 2)}


# ---------------------------------------------------------------------------------------------------------------
# Armature + skin
# ---------------------------------------------------------------------------------------------------------------
def build_armature(geo, obj_name, data_name="SKEL_Fish"):
    """The armature for one species: identity rest frames (bone tails +Y), heads at the species' landmarks."""
    data = bpy.data.armatures.new(data_name)
    data.display_type = "STICK"
    arm = bpy.data.objects.new(obj_name, data)
    bpy.context.scene.collection.objects.link(arm)
    heads = {
        "root": Vector(), "Spine_01": geo.joint(0), "Head": geo.joint(0), "Mouth": geo.mouth,
        "Fin_Pectoral_L": geo.pecs["L"]["center"], "Fin_Pectoral_R": geo.pecs["R"]["center"], "Grip": geo.grip,
        "Spine_02": geo.joint(1), "Spine_03": geo.joint(2), "Spine_04": geo.joint(3), "Tail": geo.joint(4),
    }
    pb.select_only([arm])
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    for name, parent, deform in BONES:
        b = eb.new(name)
        b.head = heads[name]
        b.tail = heads[name] + Vector((0.0, BONE_DISPLAY_M, 0.0))
        b.roll = 0.0
        b.parent = eb[parent] if parent else None
        b.use_connect = False
        b.use_deform = deform
    bpy.ops.object.mode_set(mode="OBJECT")
    for b in arm.data.bones:
        dev = (b.matrix_local.to_3x3() - Matrix.Identity(3))
        if max(abs(v) for row in dev for v in row) > 1e-6:
            raise RuntimeError("bone %s rest frame is not the identity" % b.name)
    return arm


def _hat_weights(s):
    """Body weights at station s: {bone: w}, linear between consecutive WEIGHT_CENTERS_S (<= 2 influences)."""
    cs = WEIGHT_CENTERS_S
    if s <= cs[0][1]:
        return {cs[0][0]: 1.0}
    if s >= cs[-1][1]:
        return {cs[-1][0]: 1.0}
    for (b0, s0), (b1, s1) in zip(cs, cs[1:]):
        if s0 <= s <= s1:
            t = (s - s0) / (s1 - s0)
            return {b0: 1.0 - t, b1: t} if 0.0 < t < 1.0 else ({b0: 1.0} if t <= 0.0 else {b1: 1.0})
    raise AssertionError(s)


def smoothstep(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3.0 - 2.0 * x)


def skin(mesh_obj, arm, geo):
    """Deform vertex groups (named like the bones) + Armature modifier. The fishkit part groups (Body, Fin_*, Eye_*)
    stay on the mesh (the FBX exporter only writes groups named like bones)."""
    me = mesh_obj.data
    groups = {n: (mesh_obj.vertex_groups.get(n) or mesh_obj.vertex_groups.new(name=n)) for n in DEFORM}
    pec_w = {}
    for side in ("L", "R"):
        p = geo.pecs[side]
        root_w = _hat_weights(geo.s_of_x(p["center"].x))
        h = p["hinge"]
        for i in p["root"] + p["blade"]:
            co = me.vertices[i].co
            r = co - p["center"]
            dist = (r - h * r.dot(h)).length                   # distance from the hinge line
            f = smoothstep(dist / (PEC_BLADE_RAMP * p["length"])) if i in p["blade"] else 0.0
            w = {b: (1.0 - f) * v for b, v in root_w.items()}
            w["Fin_Pectoral_" + side] = w.get("Fin_Pectoral_" + side, 0.0) + f
            pec_w[i] = w
    for v in me.vertices:
        w = pec_w.get(v.index) or _hat_weights(geo.s_of_x(v.co.x))
        for b, val in w.items():
            if val > 1e-6:
                groups[b].add([v.index], val, "REPLACE")
    mesh_obj.parent = arm
    mesh_obj.matrix_parent_inverse = Matrix.Identity(4)
    mod = mesh_obj.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    mod.use_vertex_groups = True
    mod.use_bone_envelopes = False
    mod.use_deform_preserve_volume = False     # plain linear blend skinning, as in Unreal
    return weight_stats(mesh_obj)


def weight_stats(mesh_obj):
    deform = {mesh_obj.vertex_groups[n].index for n in DEFORM}
    worst, unweighted, maxinf = 0.0, 0, 0
    for v in mesh_obj.data.vertices:
        ws = [g.weight for g in v.groups if g.group in deform and g.weight > 1e-6]
        tot = sum(ws)
        maxinf = max(maxinf, len(ws))
        unweighted += tot <= 1e-6
        worst = max(worst, abs(tot - 1.0))
    return {"verts": len(mesh_obj.data.vertices), "unweighted": unweighted, "max_weight_sum_error": round(worst, 6),
            "max_influences": maxinf}


def rig_species(mesh_obj, info, species, arm_name):
    """Build + skin one species. Returns (armature, SpeciesGeo, weight stats)."""
    rename_part_groups(mesh_obj)
    geo = SpeciesGeo(mesh_obj, info, species)
    arm = build_armature(geo, arm_name, "SKEL_Fish_" + species)
    stats = skin(mesh_obj, arm, geo)
    stats["pectoral_hinge_off_canon_deg"] = {s: geo.pecs[s]["hinge_off_canon_deg"] for s in ("L", "R")}
    if max(stats["pectoral_hinge_off_canon_deg"].values()) > PEC_HINGE_TOL_DEG:
        print("WARNING: %s pectoral chord is %.1f deg off PEC_HINGE_L; shared clips will slide its fin root"
              % (species, max(stats["pectoral_hinge_off_canon_deg"].values())))
    return arm, geo, stats


# ---------------------------------------------------------------------------------------------------------------
# Keyed channels (monotone cubic, periodic)
# ---------------------------------------------------------------------------------------------------------------
class Keys:
    """A 1-D channel from keys [(frame, value)], monotone cubic Hermite (PCHIP: flat at extremes and holds, never
    overshoots between keys). period=N: periodic, keys in [0, N), a key at frame 0 required."""

    def __init__(self, keys, period):
        ks = sorted((float(f), float(v)) for f, v in keys)
        if ks[0][0] != 0.0 or ks[-1][0] >= period:
            raise ValueError("periodic keys need frame 0 and frames < period")
        P = float(period)
        self.P = P
        ext = [(f - P, v) for f, v in ks[-2:]] + ks + [(P, ks[0][1])] + [(f + P, v) for f, v in ks[1:3]]
        self.x = [k[0] for k in ext]
        self.y = [k[1] for k in ext]
        n = len(ext)
        h = [self.x[i + 1] - self.x[i] for i in range(n - 1)]
        d = [(self.y[i + 1] - self.y[i]) / h[i] for i in range(n - 1)]
        m = [0.0] * n
        for i in range(1, n - 1):
            if d[i - 1] * d[i] > 0.0:
                w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
                m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
        self.m = m

    def __call__(self, f):
        f = f % self.P
        x, y, m = self.x, self.y, self.m
        i = max(k for k in range(len(x) - 1) if x[k] <= f)
        h = x[i + 1] - x[i]
        t = (f - x[i]) / h
        t2, t3 = t * t, t * t * t
        return ((2 * t3 - 3 * t2 + 1) * y[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * y[i + 1]
                + (t3 - t2) * h * m[i + 1])


def mirror_keys(keys, offset, sign=-1.0):
    """The same keys shifted by `offset` frames with the value sign flipped (a mirrored phrase)."""
    return [(f + offset, sign * v) for f, v in keys]


# ---------------------------------------------------------------------------------------------------------------
# Pose model
# ---------------------------------------------------------------------------------------------------------------
# Carangiform envelope of the swim wave (share of the tail-tip amplitude along u): narrow waist just behind the
# neck (U_PIVOT), a little head recoil, growing toward the tail.
ENV_HEAD, ENV_MIN, U_PIVOT, ENV_POW = 0.25, 0.05, 0.28, 1.8
WAVE_LAMBDA = 0.95           # wavelength in fish lengths (u units)
# C-bend distribution: local yaw (deg) per bone at curl strength k = 1 (concave to the fish's left). The chest can't
# bend (it is the anchor), so the head takes a large share: with 22 deg the curl read as a "J" (tail only) in the
# previews; 30 deg gives a C (dart) and a head-and-tail lift (flop). Tail tip total: -64 deg.
CURL_DEG = {"Head": 30.0, "Spine_02": -10.0, "Spine_03": -15.0, "Spine_04": -18.0, "Tail": -21.0}
# tail flick at k = 1 (tail toward the fish's left)
FLICK_DEG = {"Spine_04": -8.0, "Tail": -26.0}
# body reaction to a head shake (share of the head yaw, applied HEAD_REACT_LAG frames later, opposite sense)
HEAD_REACT = {"Spine_02": -0.10, "Spine_03": -0.22, "Spine_04": -0.30, "Tail": -0.40}
HEAD_REACT_LAG = 2.0
# Pectoral tuck: the flap angle (deg) that lays the fin against the flank. Measured by tuck_scan() on both species
# (RESULT_JSON tuck_scan): the blade touches the flank at -22 deg (0.0 mm), -24 cuts in 0.6-0.8 mm and the old guess
# -32 cut in 3.6-4.7 mm. A flat plate hinged on a convex flank can only reach tangency, so the tip still stands 12 mm
# (Bonefish) / 17 mm (CoralSnapper) off the flank; from 31 / 41 mm as modeled.
TUCK_DEG = -22.0
# Inside-of-turn clearance (applied to every pose in Pose.quats()): the pectoral on the concave side of a head yaw
# opens by PEC_TURN_FLARE x the yaw (deg per deg), as a fish's inside pectoral brakes in a turn. Without it a tucked
# fin touched the flank by 1.4 mm at a 30 deg head curl (Fight_Dart f22). Only the concave-side fin moves, so the
# down-side fin of Landed_Flop (convex side, the fish's right) is unchanged and the dock lie stays exact.
PEC_TURN_FLARE = 0.25


def envelope(u):
    if u >= U_PIVOT:
        return ENV_MIN + (1.0 - ENV_MIN) * ((u - U_PIVOT) / (1.0 - U_PIVOT)) ** ENV_POW
    return ENV_MIN + (ENV_HEAD - ENV_MIN) * ((U_PIVOT - u) / U_PIVOT) ** 2


WAVE_U = (0.0,) + U_JOINTS + (1.0,)          # nose, J1..J5, tail tip
SEGMENTS = {"Head": (0, 1), "Spine_01": (1, 2), "Spine_02": (2, 3), "Spine_03": (3, 4), "Spine_04": (4, 5),
            "Tail": (5, 6)}                  # front point, back point in WAVE_U


def midline_wave(amp, cycles, lam=WAVE_LAMBDA):
    """Lateral displacement (fish lengths) at WAVE_U for a wave with nose phase `cycles` (in cycles)."""
    return [amp * envelope(u) * math.sin(2.0 * math.pi * (cycles - u / lam)) for u in WAVE_U]


def wpo_lateral(u, cycles, amp, lam=WAVE_LAMBDA):
    """The ambient swim of the static SM_ fish (material MF_FishSwim, SK_Fish.anim.md "Ambient swim"): lateral offset
    in fish lengths at u for the nose phase `cycles`. It is the swim wave minus the straight line through its values at
    the chest joints J1 and J2, so the chest stays still (in place and in heading) exactly as in the skinned clips,
    whose bone chain is turned so the chest chord is straight. Without that line the static fish swept its tail about
    35% less than Swim_Idle at the same amplitude (strobe preview)."""
    def w(x):
        return amp * envelope(x) * math.sin(2.0 * math.pi * (cycles - x / lam))
    u1, u2 = U_JOINTS[0], U_JOINTS[1]
    return w(u) - (w(u1) + (w(u2) - w(u1)) * (u - u1) / (u2 - u1))


def chain_yaws_from_midline(ys):
    """Absolute bone yaws (rad) from chord angles of the midline displacement, turned so the chest (Spine_01) is 0,
    then made local (each bone relative to its parent)."""
    ab = {}
    for b, (i0, i1) in SEGMENTS.items():
        ab[b] = math.atan2(ys[i0] - ys[i1], WAVE_U[i1] - WAVE_U[i0])
    chest = ab["Spine_01"]
    ab = {b: a - chest for b, a in ab.items()}
    return {"Head": ab["Head"], "Spine_02": ab["Spine_02"], "Spine_03": ab["Spine_03"] - ab["Spine_02"],
            "Spine_04": ab["Spine_04"] - ab["Spine_03"], "Tail": ab["Tail"] - ab["Spine_04"]}


class Pose:
    """Local rotations of one frame, composed from components. yaw/pitch/roll in radians per chain bone (relative to
    the parent, identity rest), pectoral flap in degrees (+ flare, - tuck)."""

    def __init__(self):
        self.yaw = {b: 0.0 for b in CHAIN}
        self.pitch = {b: 0.0 for b in CHAIN}
        self.roll = 0.0
        self.pec = {"L": 0.0, "R": 0.0}

    def wave(self, amp, cycles, lam=WAVE_LAMBDA):
        for b, a in chain_yaws_from_midline(midline_wave(amp, cycles, lam)).items():
            self.yaw[b] += a
        return self

    def curl(self, k):
        for b, d in CURL_DEG.items():
            self.yaw[b] += k * d * DEG
        return self

    def flick(self, k):
        for b, d in FLICK_DEG.items():
            self.yaw[b] += k * d * DEG
        return self

    def head(self, yaw_deg=0.0, pitch_deg=0.0, react_yaw_deg=None):
        """Head yaw (+ nose left) / pitch (+ nose down). react_yaw_deg: the (lagged) head yaw the body reacts to."""
        self.yaw["Head"] += yaw_deg * DEG
        self.pitch["Head"] += pitch_deg * DEG
        if react_yaw_deg:
            for b, c in HEAD_REACT.items():
                self.yaw[b] += c * react_yaw_deg * DEG
        return self

    def arch(self, tail_pitch_deg):
        """Posterior pitch spread over Spine_03 .. Tail (+ tail up)."""
        for b, share in (("Spine_03", 0.25), ("Spine_04", 0.35), ("Tail", 0.40)):
            self.pitch[b] += share * tail_pitch_deg * DEG
        return self

    def pecs(self, left_deg, right_deg):
        self.pec["L"] += left_deg
        self.pec["R"] += right_deg
        return self

    def quats(self):
        """{bone: Quaternion} local rotations (every bone of BONES)."""
        q = {n: Quaternion() for n, _p, _d in BONES}
        for b in CHAIN:
            q[b] = (Quaternion((0.0, 0.0, 1.0), self.yaw[b]) @ Quaternion((0.0, 1.0, 0.0), self.pitch[b]))
        q["Spine_01"] = q["Spine_01"] @ Quaternion((1.0, 0.0, 0.0), self.roll)
        yaw_h = math.degrees(self.yaw["Head"])            # + = nose left: the fish's left side is the concave one
        pec_l = self.pec["L"] + PEC_TURN_FLARE * max(0.0, yaw_h)
        pec_r = self.pec["R"] + PEC_TURN_FLARE * max(0.0, -yaw_h)
        q["Fin_Pectoral_L"] = Quaternion(PEC_HINGE_L, pec_l * DEG)
        q["Fin_Pectoral_R"] = Quaternion(mirror_y(PEC_HINGE_L), -pec_r * DEG)
        return q


# ---------------------------------------------------------------------------------------------------------------
# Clips (30 fps). Each returns Pose for a frame f in [0, N]. Numbers are the "reference fish" at amplitude 1.0:
# species personality (rate, amplitude) is applied at runtime in Unreal and in the previews (personality()).
# ---------------------------------------------------------------------------------------------------------------
class ClipDef:
    def __init__(self, name, frames, fn, role, loop=True, cycle_hz=None, notes=""):
        self.name, self.frames, self.fn, self.role, self.loop = name, frames, fn, role, loop
        self.cycle_hz, self.notes = cycle_hz, notes

    @property
    def seconds(self):
        return self.frames / FPS

    def pose(self, f):
        return self.fn(float(f) % self.frames if self.loop else float(f))


def _sin(f, n_frames, cycles, phase=0.0):
    return math.sin(2.0 * math.pi * (cycles * f / n_frames + phase))


def _burst(f, start, length):
    """sin^2 window, 0 outside [start, start + length]."""
    t = (f - start) / length
    return math.sin(math.pi * t) ** 2 if 0.0 <= t <= 1.0 else 0.0


# --- Swim_Idle: slow cruise / hover (T-007 move Rest, the tired fish). 2 tail cycles in 2 s (1 Hz); pectorals scull
# 2 Hz, alternating.
IDLE_N, IDLE_CYC, IDLE_AMP = 60, 2, 0.080


def swim_idle(f):
    p = Pose().wave(IDLE_AMP, IDLE_CYC * f / IDLE_N)
    s = 14.0 * _sin(f, IDLE_N, 2 * IDLE_CYC)
    return p.pecs(6.0 + s, 6.0 - s)


# --- Swim_Fast: burst swim, 2.5 Hz, pectorals half tucked.
FAST_N, FAST_CYC, FAST_AMP = 24, 2, 0.115


def swim_fast(f):
    p = Pose().wave(FAST_AMP, FAST_CYC * f / FAST_N)
    fl = 3.0 * _sin(f, FAST_N, FAST_CYC * 2)
    return p.pecs(0.6 * TUCK_DEG + fl, 0.6 * TUCK_DEG - fl)


# --- Fight_Run (T-007 move Run): all-out run away from the angler. 3 Hz, big tail, pectorals flat, the head
# shivers against the line; a stronger stroke every loop keeps it from looking mechanical.
RUN_N, RUN_CYC, RUN_AMP = 30, 3, 0.14


def fight_run(f):
    amp = RUN_AMP * (1.0 + 0.10 * _sin(f, RUN_N, 1))
    p = Pose().wave(amp, RUN_CYC * f / RUN_N, lam=0.9)
    shiver = 3.0 * _sin(f, RUN_N, 6)
    p.head(yaw_deg=shiver)
    p.roll += 3.0 * DEG * _sin(f, RUN_N, RUN_CYC, 0.25)
    return p.pecs(TUCK_DEG, TUCK_DEG)


# --- Fight_Dive (T-007 move Dive): heavy, digging strokes nose-down toward the reef, 2 Hz; head tilted down,
# body arched into the dive, a slow roll that flashes the flank, one stubborn head-shake burst per loop.
DIVE_N, DIVE_CYC, DIVE_AMP = 60, 4, 0.15


def fight_dive(f):
    p = Pose().wave(DIVE_AMP, DIVE_CYC * f / DIVE_N, lam=1.0)
    shake = 9.0 * _burst(f, 36.0, 14.0) * _sin(f, DIVE_N, 10)          # 5 Hz shakes, f36-50
    shake_lag = 9.0 * _burst(f - HEAD_REACT_LAG, 36.0, 14.0) * _sin(f - HEAD_REACT_LAG, DIVE_N, 10)
    p.head(yaw_deg=shake, pitch_deg=12.0 + 2.0 * _sin(f, DIVE_N, DIVE_CYC), react_yaw_deg=shake_lag)
    p.arch(-10.0)                                                        # tail tip down: body arched into the dive
    p.roll += 10.0 * DEG * _sin(f, DIVE_N, 1)
    return p.pecs(TUCK_DEG, TUCK_DEG)


# --- Fight_Dart (T-007 move Dart): C-start darts, left then right. Coil (4 f), power stroke (4 f), two fading
# beats, glide. A dart LEFT coils concave-left (k > 0) and strokes right.
DART_N = 36
_DART_KEYS = [(0, 0.0), (4, 1.0), (8, -0.55), (11, 0.28), (14, -0.10), (18, 0.0)]
DART_CURL = Keys(_DART_KEYS[:-1] + mirror_keys(_DART_KEYS[:-1], 18), DART_N)
# pectorals (both fins): flared as a brake while it coils, slapped flat for the power stroke (2 frames), held flat
# through the beats, eased back out in the glide. Keyed (periodic, monotone) so there is no one-frame pop.
_DART_PEC = [(0, 12.0), (2, 18.0), (4, TUCK_DEG), (10, TUCK_DEG), (15, 10.0)]
DART_PEC = Keys(_DART_PEC + [(f + 18, v) for f, v in _DART_PEC], DART_N)


def fight_dart(f):
    k = DART_CURL(f)
    p = Pose().curl(k)
    pec = DART_PEC(f)
    return p.pecs(pec, pec)


# --- Hooked_Thrash (the hook set, T-007 move Sulk, the last metres): head shakes (5 Hz bursts) with the body
# answering, then coil - snap - rebound; mirrored second phrase. Pectorals flared and fluttering, body twisting.
THRASH_N = 90
_THRASH_CURL = [(0, 0.0), (16, 0.0), (22, 0.85), (26, -0.65), (30, 0.30), (34, -0.10), (40, 0.0)]
THRASH_CURL = Keys(_THRASH_CURL + mirror_keys(_THRASH_CURL, 45), THRASH_N)
THRASH_ROLL = Keys([(0, 0.0), (10, 12.0), (20, -8.0), (30, 10.0), (45, 0.0), (55, -12.0), (65, 8.0), (75, -10.0)],
                   THRASH_N)


def _thrash_shake(f):
    """Head shake (deg): two bursts of ~2.5 shakes at 5 Hz, the second mirrored."""
    a = 15.0 * _burst(f, 1.0, 15.0) * math.sin(2.0 * math.pi * (f - 1.0) / 6.0)
    b = -15.0 * _burst(f, 46.0, 15.0) * math.sin(2.0 * math.pi * (f - 46.0) / 6.0)
    return a + b


def hooked_thrash(f):
    p = Pose().curl(THRASH_CURL(f))
    p.head(yaw_deg=_thrash_shake(f), react_yaw_deg=_thrash_shake(f - HEAD_REACT_LAG))
    p.roll += THRASH_ROLL(f) * DEG
    fl = 8.0 * _sin(f, THRASH_N, 18)
    return p.pecs(22.0 + fl, 22.0 - fl)


# --- Landed_Flop: out of the water. Curls concave to the fish's LEFT only (k >= 0, flicks >= 0) and slaps flat, so
# lying on its RIGHT side on a dock the head and tail lift off the planks and never dip below them. Pectorals tucked
# flat (the right one stays pinned; the upper left one flicks out with the curls). In hand: hold it at Grip.
FLOP_N = 90
FLOP_CURL = Keys([(0, 0.0), (7, 0.95), (10, 1.0), (13, 0.0), (16, 0.18), (19, 0.0),
                  (44, 0.0), (50, 0.75), (52, 0.78), (55, 0.0), (58, 0.45), (61, 0.0), (63, 0.10), (66, 0.0)],
                 FLOP_N)
FLOP_FLICK = Keys([(0, 0.0), (24, 0.0), (27, 0.40), (30, 0.0), (33, 0.30), (36, 0.0),
                   (70, 0.0), (73, 0.35), (76, 0.0)], FLOP_N)


def landed_flop(f):
    k = FLOP_CURL(f)
    p = Pose().curl(k).flick(FLOP_FLICK(f))
    return p.pecs(TUCK_DEG + 20.0 * k, TUCK_DEG)


def rest_pose_fn(_f):
    return Pose()


CLIPS = [
    ClipDef("A_Fish_Swim_Idle", IDLE_N, swim_idle, "SwimIdle", cycle_hz=IDLE_CYC * FPS / IDLE_N,
            notes="slow cruise / hover; fight move Rest (calm = reel now); the tired fish (slow rate + actor roll)"),
    ClipDef("A_Fish_Swim_Fast", FAST_N, swim_fast, "SwimFast", cycle_hz=FAST_CYC * FPS / FAST_N,
            notes="fast swim; fight moves Swim and Charge"),
    ClipDef("A_Fish_Hooked_Thrash", THRASH_N, hooked_thrash, "Thrash",
            notes="head shakes + coil/snap; the hook set, fight move Sulk, the last metres"),
    ClipDef("A_Fish_Fight_Run", RUN_N, fight_run, "Run", cycle_hz=RUN_CYC * FPS / RUN_N, notes="fight move Run"),
    ClipDef("A_Fish_Fight_Dive", DIVE_N, fight_dive, "Dive", cycle_hz=DIVE_CYC * FPS / DIVE_N,
            notes="fight move Dive"),
    ClipDef("A_Fish_Fight_Dart", DART_N, fight_dart, "Dart", notes="fight move Dart: a dart left, then right"),
    ClipDef("A_Fish_Landed_Flop", FLOP_N, landed_flop, "Flop", notes="out of the water: in hand or on a dock"),
]
REST_CLIP = ClipDef("A_Fish_Rest", 1, rest_pose_fn, "Rest", notes="1-frame straight fish: the additive base pose")


def personality(quats, amplitude):
    """What Unreal's Apply Additive does with alpha = amplitude: every local rotation scaled from the identity (Unreal
    blends from the identity with a normalized lerp; this slerp differs by < 0.2 deg for these angles).
    Previews only."""
    if amplitude >= 0.9999:
        return quats
    return {n: Quaternion().slerp(q, amplitude) for n, q in quats.items()}


# ---------------------------------------------------------------------------------------------------------------
# Posing, keying, NLA
# ---------------------------------------------------------------------------------------------------------------
def apply_quats(arm, quats, prev=None):
    for name, q in quats.items():
        pbone = arm.pose.bones[name]
        pbone.rotation_mode = "QUATERNION"
        q = q.copy()
        if prev is not None and name in prev and prev[name].dot(q) < 0.0:
            q.negate()
        pbone.location = (0.0, 0.0, 0.0)
        pbone.rotation_quaternion = q
        pbone.scale = (1.0, 1.0, 1.0)
        if prev is not None:
            prev[name] = q


def rest(arm):
    apply_quats(arm, {n: Quaternion() for n, _p, _d in BONES})


def mute_all(arm):
    ad = arm.animation_data
    if ad is not None:
        ad.action = None
        for tr in ad.nla_tracks:
            tr.mute = True


def key_clip(arm, clip):
    """One Blender action per clip (keys on every frame 0..N, linear), pushed to its own NLA track."""
    act = bpy.data.actions.new(clip.name)
    act.use_fake_user = True
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = act
    prev = {}
    for f in range(0, clip.frames + 1):
        apply_quats(arm, clip.pose(f).quats(), prev)
        for pbone in arm.pose.bones:
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
    track.name = clip.name
    strip = track.strips.new(clip.name, 0, act)
    strip.name = clip.name
    track.mute = True
    return act


def solo(arm, clip_name, frame):
    ad = arm.animation_data
    for tr in ad.nla_tracks:
        tr.mute = tr.name != clip_name
    ad.action = None
    bpy.context.scene.frame_set(frame)


# ---------------------------------------------------------------------------------------------------------------
# Deformation checks (pinching, folds, fin penetration, dock clearance) on the evaluated skinned mesh
# ---------------------------------------------------------------------------------------------------------------
class DeformProbe:
    """Measures the skinned mesh in its current pose against the rest mesh.
    - ring_area_min: smallest (deformed / rest) cross-section area of any body ring (LBS thinning; < 0.8 = pinch)
    - fold_min: smallest (deformed / rest) length, along the local body axis, of any longitudinal body edge between
      two neighbouring rings (the concave side of a bend compresses; <= 0 = the surface folds over itself)
    - pec_in_mm: deepest pectoral blade vertex inside the body surface
    - pec_tip_mm: the pectoral tips' largest distance outside the body surface (how far a fin stands off the flank)
    - min_y: lowest Y of the whole mesh (fish-local; Landed_Flop dock check with the fish lying on its right side)"""

    def __init__(self, mesh_obj):
        self.obj = mesh_obj
        me = mesh_obj.data
        body = set(group_members(mesh_obj, part("Body")))
        by_x = {}
        for i in body:
            by_x.setdefault(round(me.vertices[i].co.x, 4), []).append(i)
        rings = []
        for x in sorted(by_x, reverse=True):
            vs = by_x[x]
            if len(vs) < 8:
                continue
            vs.sort(key=lambda i: math.atan2(me.vertices[i].co.y, me.vertices[i].co.z))
            rings.append(vs)
        self.rings = rings
        self.rest = [v.co.copy() for v in me.vertices]
        self.rest_area = [self._area([self.rest[i] for i in r]) for r in rings]
        self.rest_dx = [[self.rest[a].x - self.rest[b].x for a, b in zip(r0, r1)] for r0, r1 in zip(rings, rings[1:])]
        bm = bmesh.new()
        bm.from_mesh(me)
        self.body_polys = [[v.index for v in f.verts] for f in bm.faces if all(v.index in body for v in f.verts)]
        bm.free()
        # blade = every pectoral vertex but the root station (the 4 lowest indices, see find_pectoral); tip = the last
        pl, pr = (sorted(group_members(mesh_obj, part("Fin_Pectoral_" + s))) for s in ("L", "R"))
        self.pec_blade = pl[4:] + pr[4:]
        self.pec_tips = {pl[-1], pr[-1]}
        # the root strip: fin_loft joins station 0 (root) vertex k to station 1 vertex k
        self.pec_strip = [(p[k], p[4 + k]) for p in (pl, pr) for k in range(4)]
        self.pec_strip_len0 = [(self.rest[a] - self.rest[b]).length for a, b in self.pec_strip]

    @staticmethod
    def _area(pts):
        n = Vector()
        for a, b in zip(pts, pts[1:] + pts[:1]):
            n += a.cross(b)
        return 0.5 * n.length

    def coords(self):
        dg = bpy.context.evaluated_depsgraph_get()
        ev = self.obj.evaluated_get(dg)
        me = ev.to_mesh()
        co = [v.co.copy() for v in me.vertices]
        ev.to_mesh_clear()
        return co

    def pec_clearance(self, co):
        """(deepest pectoral blade vertex inside the body, largest pectoral tip distance outside it), mm. Signed by the
        nearest body face's outward normal (fishkit recalculates normals outward)."""
        bvh = BVHTree.FromPolygons(co, self.body_polys)
        pen, tip = 0.0, -1e9
        for i in self.pec_blade:
            loc, nrm, _fi, _d = bvh.find_nearest(co[i])
            if loc is None:
                continue
            d = (co[i] - loc).dot(nrm)
            pen = max(pen, -d)
            if i in self.pec_tips:
                tip = max(tip, d)
        return pen * 1000.0, tip * 1000.0

    def measure(self, co=None):
        co = co or self.coords()
        area = min(self._area([co[i] for i in r]) / a0 for r, a0 in zip(self.rings, self.rest_area))
        fold = 9.0
        cents = [sum((co[i] for i in r), Vector()) / len(r) for r in self.rings]
        for k, (r0, r1) in enumerate(zip(self.rings, self.rings[1:])):
            axis = cents[k] - cents[k + 1]
            if axis.length < 1e-9:
                continue
            axis.normalize()
            for (a, b), dx0 in zip(zip(r0, r1), self.rest_dx[k]):
                fold = min(fold, (co[a] - co[b]).dot(axis) / dx0)
        pen, tip = self.pec_clearance(co)
        strain = max(abs((co[a] - co[b]).length / l0 - 1.0) for (a, b), l0 in zip(self.pec_strip, self.pec_strip_len0))
        return {"ring_area_min": area, "fold_min": fold, "pec_in_mm": pen, "pec_tip_mm": tip,
                "pec_strip_strain": strain, "min_y": min(c.y for c in co)}


def tuck_scan(arm, probe, angles=range(0, -61, -2)):
    """Pectoral flap sweep on the straight body (both fins at the same angle): [(angle, blade penetration mm, tip
    standoff mm)]. The tuck is the most-tucked angle whose penetration stays under the fin's half-thickness."""
    out = []
    for a in angles:
        apply_quats(arm, Pose().pecs(a, a).quats())
        bpy.context.view_layer.update()
        pen, tip = probe.pec_clearance(probe.coords())
        out.append((a, round(pen, 2), round(tip, 2)))
    rest(arm)
    bpy.context.view_layer.update()
    return out

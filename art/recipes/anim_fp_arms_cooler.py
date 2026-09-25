"""anim_fp_arms_cooler: two FP-arms loops for the carried cooler, opened and shown (T-064a), on SKEL_FPArms.

  A_FPArms_CarryCooler_Open  the cooler held open, its mouth tilted toward the eye, so the player sees the fish inside
  A_FPArms_CarryCooler_Show  the cooler turned outward (mouth away from the player), as if showing it to someone
Both: 3.0 s loops (the carry's breath), sync group FPArmsBreath; both fists stay on the rim rope handles, the `cooler`
bone carries the cooler (SM_Cooler_Starter's pivot, bottom center, its own axes: +X = its front, the latch).

Built on art/recipes/anim_fp_arms.py, imported READ-ONLY (its setup() gives the same mesh, 18-bone rig, skin and pose
helpers; nothing in it is changed). This recipe only adds the two actions (and, from gate B on, their exports).
Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fp_arms_cooler.py
Spec: art/export/Characters/A_FPArms_CarryCooler_OpenShow.anim.md

HOW (deterministic): the cooler turns about its HANDLE AXIS (the line through both rope grips), so the grips stay where
the carry has them and only the fists turn with the rope; each pose = the handle midpoint (camera space), the turn
about that axis (+ = top away from the eye), the fists' roll about their rope (searched for the least wrist bend),
elbow poles. Every frame is solved with anim_fp_arms' two-bone IK from the rope grips, so the fists cannot slide.
The lid is engineering's (DT_Catch LidOpenPitch): authored for LID_OPEN_PITCH_DEG = 100, the current value.

Gate A (T064A_STAGE unset or "A"): key poses only (frame 0), no FBX. Previews in Saved/AgentLogs/previews/:
  SK_FPArms_cooler_gateA_open_fp.png / _show_fp.png  FP camera, day, 1920x1080, centre-40% box drawn, lid coverage
  SK_FPArms_cooler_gateA.png                          contact sheet: both FP frames + side views (frustum lines)
T064A_EXPLORE=1 renders candidate open/show poses (exp_* files) for the pose search.

Gate A2 (Show rework, art-mgr gateA_review.md): T064A_STAGE=A2S runs the Show pose search (SHOW_GRID, numbers only,
RESULT_JSON a2.table; T064A_GRID_PREFIX limits it, T064A_ROLLSCAN=<key> scans the fist roll on the rope);
T064A_STAGE=A2 T064A_A2=<key>,<key> renders the candidates strip SK_FPArms_cooler_gateA2_show.png (columns: gate A
Show, then the candidates; rows: FP day with the centre box, the other player's view at 2.5 m (eye 1.70 m) as a 2x
crop and as the true 90 deg frame, side view, right-fist close-up). Measures: body/lid/fish share of the centre box;
fist pixels visible (vs. props removed and no frame edge) and the visible wrist-and-hand skin, both against the
accepted carry; the fish / open-mouth / lid pixels the other player sees; arms-in-hull depth; breath-loop reach.
POSES[Show] = candidate A (x59_z29_t65_P1_S8, accepted at gate A2); the A2 strip keeps gate A's Show as its reference.

Gate B (the DEFAULT run, T064A_STAGE unset or "B"): keys both loops (frames 0-90 = 3.0 s, the carry's breath phases;
frame 90 = frame 0), checks them on the keyed actions (pops, loop seam, fists vs rope sockets, arms in the cooler hull,
elbow bend), checks the rig against the committed SK_FPArms.fbx, exports art/export/Characters/A_FPArms_CarryCooler_
{Open,Show}.fbx with pb.export_skeletal_fbx (cm; SK_FPArms.fbx itself is NOT re-exported: the mesh and the 18 bones are
unchanged), re-imports them, simulates the Unreal crossfades Carry<->Open, Carry<->Show, Open<->Show (plain blend,
the gate A Two Bone IK plan, and the gate B fix: cooler fit to the fists + IK), and renders the STOP 2 previews
SK_FPArms_cooler_{open,open_lid100,show}_fp[_dusk].png, _show_outside.png, _grips.png, _crossfade_ik.png,
_crossfade_fp.png, _anim.png. T064A_RENDER=0 skips the renders; T064A_XFADE_ONLY=1 runs only the crossfade numbers.
The owner's lid: 235 deg in both poses (LID_FP_*; eng follow-up), other players see LidOpenPitch 100.
"""
import importlib.util
import json
import math
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import fp_preview  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
RECIPES = REPO / "art" / "recipes"
PREVIEW_DIR = REPO / "Saved" / "AgentLogs" / "previews"
ASSET = "SK_FPArms"
CATEGORY = "Characters"
STAGE = os.environ.get("T064A_STAGE", "B")


def load_module(stem):
    spec = importlib.util.spec_from_file_location(stem, str(RECIPES / (stem + ".py")))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[stem] = mod
    spec.loader.exec_module(mod)
    return mod


fa = load_module("anim_fp_arms")          # read-only: rig, IK, helpers, StagedCooler, previews helpers
Vec = Vector

# ---------------------------------------------------------------------------------------------------------------
# Tuning
# ---------------------------------------------------------------------------------------------------------------
LID_OPEN_PITCH_DEG = 100.0          # DT_Catch Default LidOpenPitch (2026-09-24): the lid's opening these poses assume
CENTRE_BOX = 0.40                   # ART_STYLE: the centre 40 % of the width and of the height stays clear
FISH_STAGED = [("Bonefish", 1.0), ("CoralSnapper", 1.0), ("CoralSnapper", 1.0)]   # DT_CoolerDisplay Starter slots 0-2

# Pose = handle midpoint (camera space, m), turn about the handle axis (deg, + = top away from the eye), fist roll
# about the rope (deg; None = search), rope direction sign through the fist (None = search), elbow pole (right arm;
# left mirrored), shoulder offset.
POSES = {
    # Open: mouth turned 55 deg toward the eye (74 deg from the carry's 19 deg top-away), handles 2 cm further out
    # and 5 cm lower than the carry: the fish pile (along the back wall) faces the eye in the lower third, the
    # fists at the lower corners. Gate A search: T064A_EXPLORE (lower = the fish drop out of the frame, more turn =
    # the box fills the centre).
    "A_FPArms_CarryCooler_Open": {"handles": Vec((0.48, 0.0, -0.27)), "tilt": -55.0, "roll": None, "sign": None,
                                  "pole": Vec((-0.1, -1.0, 0.05)), "shoulder": Vec((0.0, 0.0, -0.01))},
    # Show (gate A2 candidate A = x59_z29_t65_P1_S8, accepted by art-mgr 2026-09-24): mouth turned 65 deg away (46
    # deg past the carry), held low and out, a presenting pose: handles 59 cm ahead / 29 cm below the eye, elbows
    # down, shoulders 7 cm forward / 8 cm out. The front wall (latch) faces up at the bottom of the view with both
    # fists at the lower corners; the mouth, fish and lid face whoever stands in front (the outside read comes first).
    "A_FPArms_CarryCooler_Show": {"handles": Vec((0.59, 0.0, -0.29)), "tilt": 65.0, "yaw": 0.0, "roll": None,
                                  "sign": None, "pole": Vec((0.0, -0.6, -1.0)), "shoulder": Vec((0.07, -0.08, -0.02)),
                                  "hull_top": 48},
}
# gate A's Show (the A2 strip's reference column)
GATE_A_SHOW = {"handles": Vec((0.48, 0.0, -0.22)), "tilt": 65.0, "roll": None, "sign": None,
               "pole": Vec((-0.3, -1.0, -0.5)), "shoulder": Vec((0.03, -0.05, -0.02))}
LID_PROPOSAL_DEG = 235.0            # Eng follow-up proposal: first-person carried-open lid folded back level (180 + 55)
EXPLORE = []       # (key, clip, overrides): the gate A pose search (T064A_EXPLORE=1)
for _t, _hz in ((45.0, -0.22), (50.0, -0.24), (45.0, -0.25), (55.0, -0.22)):
    EXPLORE.append(("s_%d_%d" % (_t, -_hz * 100), "A_FPArms_CarryCooler_Show",
                    {"handles": Vec((0.48, 0.0, _hz)), "tilt": _t}))
EXPLORE_RENDER = os.environ.get("T064A_EXPLORE_RENDER", "all")   # comma list of keys to render, "all", or "" (none)
ROLL_SEARCH = range(-180, 180, 10)
CONTACT_OK_MM = 16.0               # fingers/thumb behind the rope touch the wall: the carry has 15.3 mm (hidden)


# ---------------------------------------------------------------------------------------------------------------
# Pose solve
# ---------------------------------------------------------------------------------------------------------------
def grip_frame(cfg, t):
    """The handle-axis frame at time t: origin midway between the rope grips, X forward (away from the player),
    turned cfg['tilt'] about the handle axis (Y). The breath = the carry's (anim_fp_arms COOLER_SWAY, same phases)."""
    w = fa.loop_w()
    a_tilt, a_off = fa.COOLER_SWAY
    p = cfg["handles"] + Vec((a_off.x * math.sin(w * t - 0.3), 0.0, a_off.z * math.sin(w * t - 0.5)))
    R = fa.rot3((0, 1, 0), cfg["tilt"] + a_tilt * math.sin(w * t - 0.9))
    if cfg.get("yaw"):                                   # gate A2 search: the handle axis turned about the vertical
        R = fa.rot3((0, 0, 1), cfg["yaw"]) @ R
    return fa.mat4(R, p)


def cooler_matrix(cfg, t):
    """The `cooler` bone (cooler pivot, bottom center, cooler axes; its front faces the player) at time t."""
    return (grip_frame(cfg, t) @ fa.rot3((0, 0, 1), 180.0).to_4x4()
            @ Matrix.Translation(Vec((0.0, 0.0, -fa.COOLER_HANDLE_L.z))))


def targets(B, sides, cfg, t, roll, sign):
    C = grip_frame(cfg, t)
    breath = Vec((0.0, 0.0, 0.004 * math.sin(fa.loop_w() * t)))
    out = {}
    for s, sd in sides.items():
        G = fa.rod_rest_matrix(sd)
        P_h = C @ fa.handle_frame(s, roll, sign) @ G.inverted() @ B["hand_" + s]
        pole = cfg["pole"] if s == "r" else fa.mir(cfg["pole"])
        sh = cfg["shoulder"] if s == "r" else fa.mir(cfg["shoulder"])     # authored for the right arm
        out[s] = fa.ArmTarget(P_h.translation, P_h.to_3x3(), sh + breath, fa.GRIP_CURL_DEG,
                              fa.GRIP_TILT_DEG, fa.thumb_rot(fa.THUMB_GRIP), pole=pole)
    return out


def wrist_cost(m):
    return max(abs(m[s]["wrist_flex_deg"]) + abs(m[s]["wrist_dev_deg"]) + 0.5 * abs(m[s]["twist_deg"]) for s in m)


def arms_points(ctx):
    dg = bpy.context.evaluated_depsgraph_get()
    mesh_obj = ctx["mesh_obj"]
    ev = mesh_obj.evaluated_get(dg)
    me = ev.to_mesh()
    pts = [mesh_obj.matrix_world @ v.co for v in me.vertices]
    ev.to_mesh_clear()
    return pts


def penetration_by_group(ctx, cooler):
    """Arms vertices inside the cooler hull, per dominant deform group: {group: (count, max depth mm)}."""
    mesh_obj = ctx["mesh_obj"]
    pts = arms_points(ctx)
    names = {g.index: g.name for g in mesh_obj.vertex_groups}
    by = {}
    for v, p in zip(mesh_obj.data.vertices, pts):
        if v.groups:
            g = max(v.groups, key=lambda e: e.weight)
            by.setdefault(names[g.group], []).append(p)
    out = {}
    for g, ps in by.items():
        n, d = fa.inside_convex(ps, cooler.ucx)
        if n:
            out[g] = (n, d)
    return out


def grip_drift_mm(ctx, P_a, P_b, cooler, alpha=0.5):
    """Unreal-style crossfade of two poses at alpha: how far each fist's grip point leaves its rope handle (mm)."""
    B, sides, arm_obj = ctx["B"], ctx["sides"], ctx["arm_obj"]
    fa.apply_basis(arm_obj, fa.blend_basis(fa.pose_to_basis(B, P_a), fa.pose_to_basis(B, P_b), alpha))
    bpy.context.view_layer.update()
    pb_ = arm_obj.pose.bones
    C = pb_["cooler"].matrix.copy()
    out = {}
    for s, sock in (("l", "SOCKET_Handle_R"), ("r", "SOCKET_Handle_L")):
        G = fa.rod_rest_matrix(sides[s])
        grip = (pb_["hand_" + s].matrix @ B["hand_" + s].inverted() @ G).translation
        out[s] = round((grip - C @ cooler.sockets[sock]).length * 1000.0, 1)
    return out


def solve_roll(ctx, cfg, cooler):
    """Fist roll about the rope + rope direction through the fist: the least wrist bend at frame 0 among the grips
    whose arms stay out of the cooler's hull (UCX_; the rope handles are outside it). Two stages: the wrist cost of
    every grip (analytic), then the hull check on the 12 best."""
    B, sides = ctx["B"], ctx["sides"]
    if cfg["roll"] is not None and cfg["sign"] is not None:
        return cfg["roll"], cfg["sign"]
    cands = []
    for sign in ((1.0, -1.0) if cfg["sign"] is None else (cfg["sign"],)):
        for roll in (ROLL_SEARCH if cfg["roll"] is None else (cfg["roll"],)):
            try:
                P, m = fa.full_pose(B, sides, B["arms"], targets(B, sides, cfg, 0.0, float(roll), sign),
                                    cooler=cooler_matrix(cfg, 0.0))
            except RuntimeError:
                continue
            cands.append((wrist_cost(m), float(roll), sign, P))
    if not cands:
        raise RuntimeError("no reachable grip for %s" % cfg)
    cands.sort(key=lambda c: c[0])
    best = None
    for c, roll, sign, P in cands[:cfg.get("hull_top", 12)]:
        fa.apply_basis(ctx["arm_obj"], fa.pose_to_basis(B, P))
        bpy.context.view_layer.update()
        cooler.place(P["cooler"])
        n, depth = fa.inside_convex(arms_points(ctx), cooler.ucx)
        score = c + 3.0 * depth + 0.2 * n + 30.0 * max(0.0, depth - CONTACT_OK_MM)
        if best is None or score < best[0]:
            best = (score, roll, sign)
    return best[1], best[2]


def pose(B, sides, cfg, f):
    t = f / fa.FPS
    return fa.full_pose(B, sides, B["arms"], targets(B, sides, cfg, t, cfg["_roll"], cfg["_sign"]),
                        cooler=cooler_matrix(cfg, t))


# ---------------------------------------------------------------------------------------------------------------
# Staging: cooler (lid open), fish in the DT_CoolerDisplay slots
# ---------------------------------------------------------------------------------------------------------------
class OpenCooler(fa.StagedCooler):
    def place(self, C, lid_pitch=LID_OPEN_PITCH_DEG):
        """Body at C; the lid on LidHinge opened `lid_pitch` deg (Unreal relative pitch: the front rises)."""
        for o, is_lid in self.objs:
            M = self.orig[o.name]
            if is_lid:
                o.matrix_world = C @ Matrix.Translation(self.hinge) @ Matrix.Rotation(math.radians(-lid_pitch), 4, "Y") @ M
            else:
                o.matrix_world = C @ M
        bpy.context.view_layer.update()


class StagedFish:
    """Curled fish (anim_fish_cooler.Curled, read-only) in the DT_CoolerDisplay Starter slots, cooler space."""

    def __init__(self):
        afc = load_module("anim_fish_cooler")
        rows = json.loads((REPO / "data" / "tables" / "DT_CoolerDisplay.json").read_text(encoding="utf-8"))
        row = next(r for r in rows if r["Name"] == "Starter")
        need = sorted({sp for sp, _s in FISH_STAGED})
        fishes = {sp: afc.af.Fish(stem, asset, sp) for stem, asset, sp in afc.af.SPECIES if sp in need}
        curls = {sp: afc.Curled(f) for sp, f in fishes.items()}
        for f in fishes.values():
            f.mesh.hide_render = True
            f.mesh.hide_viewport = True
        contents = Vec((0.0, 0.0, 0.05))                  # SM_Cooler_Starter socket Contents (liner floor center)
        self.objs, self.local = [], []
        for i, (sp, s) in enumerate(FISH_STAGED):
            slot = row["Slots"][i]
            rot = slot["Rotation"]
            side, yaw_b = None, None
            for sd in (1, -1):                           # Blender slot matrix that reads back as the DT rotator
                for yb in (-rot["Yaw"], rot["Yaw"]):
                    p, y, r = afc.ue_rotator(afc.slot_matrix(sd, yb, (0, 0, 0)).to_3x3())
                    if abs(p - rot["Pitch"]) < 0.01 and abs(y - rot["Yaw"]) < 0.01 and abs(r - rot["Roll"]) < 0.01:
                        side, yaw_b = sd, yb
            if side is None:
                raise RuntimeError("slot %d rotator not reproduced" % i)
            cf = curls[sp]
            loc = contents + Vec((slot["Location"]["X"] / 100.0, -slot["Location"]["Y"] / 100.0,
                                  slot["Location"]["Z"] / 100.0 + cf.lie[side] * s))
            M = afc.slot_matrix(side, yaw_b, loc) @ Matrix.Diagonal((s, s, s, 1.0))
            ob = bpy.data.objects.new("PV_Fish%d_%s" % (i, sp), cf.mesh)
            bpy.context.scene.collection.objects.link(ob)
            self.objs.append(ob)
            self.local.append(M)

    def place(self, C):
        for ob, M in zip(self.objs, self.local):
            ob.matrix_world = C @ M
        bpy.context.view_layer.update()


# ---------------------------------------------------------------------------------------------------------------
# Measuring (ray grid from the eye; first hit decides)
# ---------------------------------------------------------------------------------------------------------------
W, H = fp_preview.FP_RESOLUTION
TX = math.tan(math.radians(fp_preview.FP_HFOV_DEG) / 2.0)
TY = TX * H / W


def ray_dir(u, v):
    """NDC (-1..1, +u right, +v up) -> world direction from the eye."""
    return Vec((1.0, -u * TX, v * TY)).normalized()


def first_hits(nu, nv, u0=-1.0, u1=1.0, v0=-1.0, v1=1.0):
    scene = bpy.context.scene
    dg = bpy.context.evaluated_depsgraph_get()
    out = []
    for i in range(nu):
        for j in range(nv):
            u = u0 + (u1 - u0) * (i + 0.5) / nu
            v = v0 + (v1 - v0) * (j + 0.5) / nv
            o, d, name = Vec(), ray_dir(u, v), None
            for _k in range(8):                       # skip what the game doesn't render (UCX_ hulls, guides)
                hit, loc, _n, _idx, obj, _M = scene.ray_cast(dg, o, d, distance=50.0)
                if not hit:
                    break
                if obj.name in RAY_TARGETS:
                    name = obj.name
                    break
                o = loc + d * 1e-4
            out.append((u, v, name))
    return out


def classify(name, groups):
    if name is None:
        return None
    for k, names in groups.items():
        if name in names:
            return k
    return "other"


RAY_TARGETS = set()


def coverage(groups, whole_frame=True):
    """Centre box: share covered by each group (first hit); whole frame: share of each group."""
    RAY_TARGETS.clear()
    for names in groups.values():
        RAY_TARGETS.update(names)
    c = CENTRE_BOX
    cen = first_hits(64, 64, -c, c, -c, c)
    res = {"centre_box": {}}
    sets = [("centre_box", cen)]
    if whole_frame:
        res["frame"] = {}
        sets.append(("frame", first_hits(160, 90)))
    for key, hits in sets:
        n = len(hits)
        cls = [classify(h[2], groups) for h in hits]
        for g in list(groups) + ["other"]:
            res[key][g] = round(100.0 * sum(1 for x in cls if x == g) / n, 1)
        res[key]["any"] = round(100.0 * sum(1 for x in cls if x) / n, 1)
    # highest point of the lid / cooler on screen (% of the height from the bottom)
    return res


def screen_top_pct(points):
    ys = [50.0 + 50.0 * (p.z / p.x) / TY for p in points if p.x > 1e-3]
    return round(max(ys), 1) if ys else None


# ---------------------------------------------------------------------------------------------------------------
# Preview pieces
# ---------------------------------------------------------------------------------------------------------------
def stage_centre_box(hex_color="#FFE14D"):
    """The centre-40 % box as thin unlit bars 5 cm in front of the eye (drawn in every FP frame)."""
    d = 0.05
    hw, hh = d * TX * CENTRE_BOX, d * TY * CENTRE_BOX
    t = d * TX * 2.0 * 3.0 / W                         # 3 px
    mat = fa.label_material("PV_CentreBox", hex_color)
    objs = []
    for name, c, sz in (("top", (d, 0.0, hh), (2 * hw + t, t)), ("bot", (d, 0.0, -hh), (2 * hw + t, t)),
                        ("l", (d, hw, 0.0), (t, 2 * hh + t)), ("r", (d, -hw, 0.0), (t, 2 * hh + t))):
        me = bpy.data.meshes.new("PV_CB_" + name)
        a, b = sz[0] / 2.0, sz[1] / 2.0
        me.from_pydata([(0, -a, -b), (0, a, -b), (0, a, b), (0, -a, b)], [], [(0, 1, 2, 3)])
        me.materials.append(mat)
        ob = bpy.data.objects.new("PV_CB_" + name, me)
        bpy.context.scene.collection.objects.link(ob)
        ob.location = c
        if hasattr(ob, "visible_shadow"):
            ob.visible_shadow = False
        objs.append(ob)

    def cleanup():
        for o in objs:
            me = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            bpy.data.meshes.remove(me)
    return cleanup


def stage_frustum_side():
    """Side-view guides: the eye, the frame's top/bottom rays (+-29.4 deg) and the centre box's (+-12.7 deg)."""
    objs = []
    y = -0.7
    ang_frame = math.degrees(math.atan(TY))
    ang_box = math.degrees(math.atan(TY * CENTRE_BOX))
    for name, a, col in (("ftop", ang_frame, style.UI.INK), ("fbot", -ang_frame, style.UI.INK),
                         ("btop", ang_box, "#E8C46A"), ("bbot", -ang_box, "#E8C46A")):
        L = 1.4
        ob = fa.box("PV_G_" + name, (0, y, 0), (L, 0.004, 0.004), col)
        ob.matrix_world = (Matrix.Translation((0, y, 0)) @ Matrix.Rotation(math.radians(-a), 4, "Y")
                           @ Matrix.Translation((L / 2.0, 0, 0)))
        objs.append(ob)
    objs.append(fa.box("PV_G_eye", (0.0, y, 0.0), (0.024, 0.024, 0.024), style.TROPICAL.ACCENT))

    def cleanup():
        for o in objs:
            me = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            bpy.data.meshes.remove(me)
    return cleanup


# ---------------------------------------------------------------------------------------------------------------
# Gate A
# ---------------------------------------------------------------------------------------------------------------
def evaluate(ctx, cfg, cooler, fish):
    B, sides, arm_obj, mesh_obj = ctx["B"], ctx["sides"], ctx["arm_obj"], ctx["mesh_obj"]
    cfg["_roll"], cfg["_sign"] = solve_roll(ctx, cfg, cooler)
    P, m = pose(B, sides, cfg, 0)
    fa.apply_basis(arm_obj, fa.pose_to_basis(B, P))
    bpy.context.view_layer.update()
    C = P["cooler"]
    cooler.place(C, cfg.get("lid", LID_OPEN_PITCH_DEG))
    fish.place(C)
    groups = {"lid": {cooler.lid.name}, "cooler": {cooler.body.name}, "fish": {o.name for o in fish.objs},
              "arms": {mesh_obj.name}}
    cov = coverage(groups)
    inside = fa.inside_convex(arms_points(ctx), cooler.ucx)
    lid_pts = cooler.points([cooler.lid])
    pen = penetration_by_group(ctx, cooler)
    P_carry = ctx["poser"].carry(0)[0]
    drift = grip_drift_mm(ctx, P_carry, P, cooler)
    fa.apply_basis(arm_obj, fa.pose_to_basis(B, P))
    bpy.context.view_layer.update()
    res = {"roll_deg": cfg["_roll"], "rope_sign": cfg["_sign"], "wrists": m, "coverage_pct": cov,
           "lid_top_screen_pct": screen_top_pct(lid_pts),
           "cooler_top_screen_pct": screen_top_pct(cooler.points([cooler.body])),
           "arms_verts_inside_cooler_hull": inside, "inside_by_group": pen,
           "carry_crossfade_grip_drift_mm_at_half": drift,
           "unreal_cooler": fa.ue_transform(C), "handles_cam_m": [round(c, 3) for c in cfg["handles"]],
           "tilt_deg": cfg["tilt"], "lid_deg": cfg.get("lid", LID_OPEN_PITCH_DEG)}
    return res, P


def label_for(name, cfg, r):
    c = r["coverage_pct"]["centre_box"]
    return ("%s f0  day | lid covers %.0f%% of the centre box (cooler %.0f%%, fish %.0f%%, arms %.0f%%) | "
            "handles (%.0f, %.0f) cm, turn %+.0f deg | lid %g deg"
            % (name.replace("A_FPArms_", ""), c["lid"], c["cooler"], c["fish"], c["arms"],
               cfg["handles"].x * 100, -cfg["handles"].z * 100, cfg["tilt"], cfg.get("lid", LID_OPEN_PITCH_DEG)))


def gate_a(ctx):
    cooler = OpenCooler()
    fish = StagedFish()
    out, full, cells = {}, {}, []
    cell = (960, 540)
    if os.environ.get("T064A_EXPLORE", "") == "1":
        want = [k for k, _c, _o in EXPLORE] if EXPLORE_RENDER == "all" else [k for k in EXPLORE_RENDER.split(",") if k]
        for key, clip, over in EXPLORE:
            if want and key not in want:
                continue
            cfg = dict(POSES[clip], **over)
            try:
                r, _P = evaluate(ctx, cfg, cooler, fish)
            except RuntimeError as e:
                out[key] = {"error": str(e)}
                continue
            out[key] = r
            if want:
                cb = stage_centre_box()
                try:
                    cells.append(fa.fp_frame(PREVIEW_DIR / ("exp_t064a_%s.png" % key), "day",
                                             key + ": " + label_for("", cfg, r), res=(640, 360), samples=8))
                finally:
                    cb()
                if clip.endswith("Show"):
                    C = _P["cooler"]
                    c = C.translation
                    cells.append(fa.shot(PREVIEW_DIR / ("exp_t064a_%s_top.png" % key), (0.25, 0.0, 1.5),
                                         (0.25, 0.0, -0.3), ortho=1.2, res=(640, 360), label=key + " top"))
        if cells:
            full["explore_sheet"] = pb.contact_sheet(cells, PREVIEW_DIR / "exp_t064a_sheet.png", cols=4,
                                                     cell=(640, 360))
        return out, full
    side_cells, grip_cells = [], []
    for name, cfg in POSES.items():
        short = name.replace("A_FPArms_CarryCooler_", "").lower()
        lids = [LID_OPEN_PITCH_DEG] + ([LID_PROPOSAL_DEG] if short == "open" else [])
        for lid in lids:
            cfg_l = dict(cfg, lid=lid)
            r, P = evaluate(ctx, cfg_l, cooler, fish)
            key = name if lid == LID_OPEN_PITCH_DEG else name + "__lid%d" % lid
            out[key] = r
            fn = "SK_FPArms_cooler_gateA_%s_fp.png" % short if lid == LID_OPEN_PITCH_DEG else                 "SK_FPArms_cooler_gateA_%s_lid%d_fp.png" % (short, lid)
            label = label_for(name, cfg_l, r)
            if lid != LID_OPEN_PITCH_DEG:
                label = "PROPOSAL (eng follow-up): " + label
            cb = stage_centre_box()
            try:
                full[key] = fa.fp_frame(PREVIEW_DIR / fn, "day", label)
            finally:
                cb()
            cells.append(full[key])
            if lid != LID_OPEN_PITCH_DEG:
                continue
            fg = stage_frustum_side()
            try:
                side_cells.append(fa.shot(PREVIEW_DIR / ("exp_t064a_side_%s.png" % short), (0.3, -2.4, -0.15),
                                          (0.3, 0.0, -0.15), ortho=1.3, res=cell,
                                          label="%s f0 side: eye orange, frame edges black, centre box amber"
                                          % name.replace("A_FPArms_", "")))
            finally:
                fg()
            C = P["cooler"]
            hr = C @ fa.COOLER_HANDLE_L                      # the right fist's rope (the cooler faces the player)
            grip_cells.append(fa.shot(PREVIEW_DIR / ("exp_t064a_grip_%s.png" % short), hr + Vec((0.10, -0.62, 0.22)),
                                      hr + Vec((-0.04, 0.0, 0.0)), lens=35.0, res=cell,
                                      label="%s f0: right fist on the rope (Handle_L)" % name.replace("A_FPArms_", "")))
            grip_cells.append(fa.shot(PREVIEW_DIR / ("exp_t064a_out_%s.png" % short),
                                      C.translation + Vec((1.6, -0.9, 0.5)), C.translation + Vec((0.0, 0.0, 0.1)),
                                      lens=35.0, res=cell,
                                      label="%s f0 from outside, front-right" % name.replace("A_FPArms_", "")))
    full["sheet"] = pb.contact_sheet(cells + side_cells + grip_cells, PREVIEW_DIR / "SK_FPArms_cooler_gateA.png",
                                     cols=3, cell=cell)
    return out, full


# ---------------------------------------------------------------------------------------------------------------
# Gate A2: Show rework (art-mgr gateA_review.md): both fists read (>= ~30 % of each), the body covers <= 15 % of the
# centre box, and the outside read (mouth, fish, lid to another player at 2.5 m) is kept first.
# ---------------------------------------------------------------------------------------------------------------
SHOW = "A_FPArms_CarryCooler_Show"
EYE_M = 1.65                         # DT_Movement Stand EyeHeight (the carrier's FP camera above the floor)
OBSERVER_EYE_M = 1.70                # art-mgr: the other player's eye, 2.5 m in front
OBSERVER = Vec((2.5, 0.0, OBSERVER_EYE_M - EYE_M))   # carrier camera space
FIST_MIN_PCT = 30.0
BODY_MAX_PCT = 15.0
ZOOM_HFOV_DEG = 2.0 * math.degrees(math.atan(0.5))   # 53.13 deg at 960x540 = the pixel-exact centre crop of 1920x1080


def show_cfg(x, z, tilt, yaw=0.0, pole=None, shoulder=None):
    """A Show candidate: handles at (x, z) camera space, turned `tilt` about the handle axis, `yaw` about the vertical.
    Shoulders reach forward with the handles (protraction up to 7 cm), elbows out and down (as gate A)."""
    sh = shoulder if shoulder is not None else Vec((min(0.07, 0.03 + 0.5 * max(0.0, x - 0.48)), -0.05, -0.02))
    return {"handles": Vec((x, 0.0, z)), "tilt": tilt, "yaw": yaw, "roll": None, "sign": None,
            "pole": Vec(pole) if pole is not None else Vec((-0.3, -1.0, -0.5)), "shoulder": sh, "hull_top": 48}


# Search 1 (x 50-60, z 24-34, turn 30-65): at turns below ~60 deg the lid (hinged on the far edge, open 100 deg)
# stands between the observer and the mouth (outside fish < 10k px vs 20k at 65) -> search 2 around 60-75 deg.
# Search 2 (x 56-63, z 26-30, turn 60-75): x 60 / turn 65-70 comes closest (fists 25-29 %, body 5-13 %), but the
# roll search's best-wrist grips pushed the fist into the wall (hull 32 mm) -> search 3: 48 grips hull-checked and
# three elbow poles (P0 out+down as gate A, P1 down, P2 out level).
SHOW_POLES = {"P0": (-0.3, -1.0, -0.5), "P1": (0.0, -0.6, -1.0), "P2": (-0.3, -1.0, 0.0)}
SHOW_GRID = [("x%d_z%d_t%d_%s" % (round(x * 100), round(-z * 100), t, pk), show_cfg(x, z, t, pole=pv))
             for x in (0.58, 0.60, 0.62) for z in (-0.27, -0.29) for t in (65.0, 70.0)
             for pk, pv in SHOW_POLES.items()]
SHOW_REF = ("gateA_show", dict(GATE_A_SHOW))
# Search 4: x 59-60 with the shoulders 3 cm further out (S8) against the forearm-in-wall clip of search 3's x 60 rows.
SHOW_GRID += [("x%d_z%d_t%d_%s_S%d" % (round(x * 100), round(-z * 100), t, pk, round(-sy * 100)),
               show_cfg(x, z, t, pole=SHOW_POLES[pk],
                        shoulder=Vec((min(0.07, 0.03 + 0.5 * max(0.0, x - 0.48)), sy, -0.02))))
              for x in (0.59, 0.60) for z in (-0.28, -0.29) for t in (65.0, 68.0) for pk in ("P0", "P1")
              for sy in (-0.08,)]
SHOW_A2 = [k for k in os.environ.get("T064A_A2", "").split(",") if k]    # the 2 candidates to render (keys)


def fist_indices(mesh_obj, bones=("hand", "fingers", "thumb")):
    names = {g.index: g.name for g in mesh_obj.vertex_groups}
    out = {"l": [], "r": []}
    for v in mesh_obj.data.vertices:
        if v.groups:
            g = names[max(v.groups, key=lambda e: e.weight).group]
            for s in out:
                if g in tuple(b + "_" + s for b in bones):
                    out[s].append(v.index)
    return out


def fist_polygons(mesh_obj, fists):
    out = {}
    for s, idx in fists.items():
        vs = set(idx)
        out[s] = {p.index for p in mesh_obj.data.polygons if sum(1 for v in p.vertices if v in vs) * 2 > len(p.vertices)}
    return out


def fist_pixels(ctx, blockers):
    """Per fist, in pixels (as the eye sees it): `seen` = fist pixels inside the FP frame and not covered by the
    cooler/lid/fish; `whole` = the fist's pixels with the props removed and no frame edge. visible_pct = seen / whole
    (the forearm hiding the fist counts in both, so only the props and the frame edge reduce it)."""
    scene = bpy.context.scene
    dg = bpy.context.evaluated_depsgraph_get()
    pts = arms_points(ctx)
    mesh_name = ctx["mesh_obj"].name
    out = {}
    for s, idx in ctx["fists"].items():
        polys = ctx["fist_polys"][s]
        wrist = ctx["wrist_polys"][s]
        us = [-pts[i].y / pts[i].x / TX for i in idx]
        vs = [pts[i].z / pts[i].x / TY for i in idx]
        u0, u1, v0, v1 = min(us) - 0.1, max(us) + 0.1, min(vs) - 0.1, max(vs) + 0.1   # + the wrist around the fist
        nu, nv = 72, max(8, int(72 * (v1 - v0) * TY / max(1e-6, (u1 - u0) * TX)))
        seen = whole = wseen = 0
        for i in range(nu):
            for j in range(nv):
                u, v = u0 + (u1 - u0) * (i + 0.5) / nu, v0 + (v1 - v0) * (j + 0.5) / nv
                d, o = ray_dir(u, v), Vec()
                first_any = first_arms = None
                for _k in range(12):
                    hit, loc, _n, pi, obj, _M = scene.ray_cast(dg, o, d, distance=5.0)
                    if not hit:
                        break
                    if obj.name == mesh_name:
                        first_arms = pi
                        if first_any is None:
                            first_any = ("arms", pi)
                        break
                    if obj.name in blockers and first_any is None:
                        first_any = ("prop", pi)
                    o = loc + d * 1e-4
                if (first_arms is not None and first_arms in wrist and first_any == ("arms", first_arms)
                        and abs(u) <= 1.0 and abs(v) <= 1.0):
                    wseen += 1
                if first_arms is not None and first_arms in polys:
                    whole += 1
                    if first_any == ("arms", first_arms) and abs(u) <= 1.0 and abs(v) <= 1.0:
                        seen += 1
        px = ((u1 - u0) / nu * W / 2.0) * ((v1 - v0) / nv * H / 2.0)
        out[s] = {"visible_pct": round(100.0 * seen / max(1, whole), 1), "seen_px": int(seen * px),
                  "whole_px": int(whole * px), "wrist_seen_px": int(wseen * px)}
    return out


def fist_screen(P, B, sides):
    """Each fist's grip point on screen (% from the left, % from the bottom)."""
    out = {}
    for s in ("l", "r"):
        G = fa.rod_rest_matrix(sides[s])
        g = (P["hand_" + s] @ B["hand_" + s].inverted() @ G).translation
        out[s] = (round(50.0 - 50.0 * (g.y / g.x) / TX, 1), round(50.0 + 50.0 * (g.z / g.x) / TY, 1))
    return out


def observer_matrix():
    """The other player's eye (looking back at the carrier, level): Unreal-style camera frame, +X = view."""
    return Matrix.Translation(OBSERVER) @ fa.rot3((0, 0, 1), 180.0).to_4x4()


class ObserverStage:
    """Moves the carrier (arms, cooler, fish) into the observer's camera space (the FP renderer's camera sits at the
    origin looking +X, level) and adds a placeholder body (grey torso + head) so the view reads as a person."""

    def __init__(self, ctx, P, cooler, fish, lid, O=None):
        self.ctx, self.cooler, self.fish = ctx, cooler, fish
        self.Oi = (O if O is not None else observer_matrix()).inverted()
        ctx["arm_obj"].matrix_world = self.Oi          # the skinned mesh moves with its armature (the modifier
        ctx["mesh_obj"].matrix_world = self.Oi         # deforms in armature space: both or neither)
        Cp = self.Oi @ P["cooler"]
        cooler.place(Cp, lid)
        fish.place(Cp)
        B = ctx["B"]
        S = B["upperarm_r"].translation
        sw = abs(S.y) * 2.0 + 0.06
        grey = "#8E969C"
        parts = [("PV_Body_Torso", Vec((S.x - 0.11, 0.0, S.z - 0.28)), (0.24, sw, 0.62)),
                 ("PV_Body_Neck", Vec((-0.09, 0.0, S.z + 0.07)), (0.10, 0.10, 0.12)),
                 ("PV_Body_Head", Vec((-0.09, 0.0, -0.01)), (0.20, 0.17, 0.24))]
        self.objs = [fa.box(n, self.Oi @ c, sz, grey) for n, c, sz in parts]
        bpy.context.view_layer.update()

    def read(self, groups):
        """Pixels (1920x1080, 90 deg frame) the observer sees of: fish, liner (the open mouth), lid, body, arms."""
        scene = bpy.context.scene
        dg = bpy.context.evaluated_depsgraph_get()
        pts = self.cooler.points() + [o.matrix_world.translation for o in self.fish.objs]
        us = [-p.y / p.x / TX for p in pts if p.x > 1e-3]
        vs = [p.z / p.x / TY for p in pts if p.x > 1e-3]
        u0, u1, v0, v1 = min(us) - 0.01, max(us) + 0.01, min(vs) - 0.01, max(vs) + 0.01
        step = 2
        nu, nv = int((u1 - u0) / 2.0 * W / step) + 1, int((v1 - v0) / 2.0 * H / step) + 1
        body = self.cooler.body
        ev = body.evaluated_get(dg)
        liner_mats = {i for i, sl in enumerate(ev.material_slots) if sl.material and "Liner" in sl.material.name}
        counts = {k: 0 for k in ("fish", "liner", "lid", "body", "arms")}
        for i in range(nu):
            for j in range(nv):
                d = ray_dir(u0 + (i + 0.5) * (u1 - u0) / nu, v0 + (j + 0.5) * (v1 - v0) / nv)
                o = Vec()
                for _k in range(8):
                    hit, loc, _n, idx, obj, _M = scene.ray_cast(dg, o, d, distance=50.0)
                    if not hit:
                        break
                    k = None
                    if obj.name in groups["fish"]:
                        k = "fish"
                    elif obj.name == body.name:
                        k = "liner" if ev.data.polygons[idx].material_index in liner_mats else "body"
                    elif obj.name in groups["lid"]:
                        k = "lid"
                    elif obj.name in groups["arms"]:
                        k = "arms"
                    elif obj.name.startswith("PV_Body_"):
                        break
                    if k:
                        counts[k] += 1
                        break
                    o = loc + d * 1e-4
        px_per_ray = ((u1 - u0) / nu * W / 2.0) * ((v1 - v0) / nv * H / 2.0)
        return {k: int(round(v * px_per_ray)) for k, v in counts.items()}

    def render(self, path, label, hfov, res=(960, 540), kind="day"):
        half_w = 0.05 * math.tan(math.radians(hfov) / 2.0)
        cleanup = fa.stage_label(label, (0, 0, 0), (0, -1, 0), (0, 0, 1), (1, 0, 0), half_w, res, rel_size=0.04)
        try:
            fp_preview.render_fp(path, kind, resolution=res, hfov_deg=hfov, samples=24)
        finally:
            cleanup()
        return str(path)

    def close(self, P, lid):
        for o in self.objs:
            me = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            bpy.data.meshes.remove(me)
        self.ctx["arm_obj"].matrix_world = Matrix.Identity(4)
        self.ctx["mesh_obj"].matrix_world = Matrix.Identity(4)
        self.cooler.place(P["cooler"], lid)
        self.fish.place(P["cooler"])
        bpy.context.view_layer.update()


def show_eval(ctx, cfg, cooler, fish, outside=True, lid=LID_OPEN_PITCH_DEG):
    """Solve + measure one Show candidate at frame 0 (the FP numbers with the lid at `lid`; the observer always sees
    LID_OPEN_PITCH_DEG, the replicated value)."""
    B, sides, arm_obj, mesh_obj = ctx["B"], ctx["sides"], ctx["arm_obj"], ctx["mesh_obj"]
    cfg["_roll"], cfg["_sign"] = solve_roll(ctx, cfg, cooler)
    P, m = pose(B, sides, cfg, 0)
    fa.apply_basis(arm_obj, fa.pose_to_basis(B, P))
    bpy.context.view_layer.update()
    cooler.place(P["cooler"], lid)
    fish.place(P["cooler"])
    groups = {"lid": {cooler.lid.name}, "cooler": {cooler.body.name}, "fish": {o.name for o in fish.objs},
              "arms": {mesh_obj.name}}
    cov = coverage(groups, whole_frame=False)["centre_box"]
    fists = fist_pixels(ctx, groups["lid"] | groups["cooler"] | groups["fish"])
    n_in, depth = fa.inside_convex(arms_points(ctx), cooler.ucx)
    res = {"handles_cam_cm": [round(c * 100, 1) for c in cfg["handles"]], "tilt_deg": cfg["tilt"],
           "yaw_deg": cfg.get("yaw", 0.0), "centre_box_pct": cov, "fists": fists, "fist_screen_pct": fist_screen(P, B, sides),
           "wrist_cost": round(wrist_cost(m), 1), "wrists": m, "hull_verts": n_in, "hull_depth_mm": round(depth, 1),
           "hull_by_group": {g: [n, round(d_, 1)] for g, (n, d_) in penetration_by_group(ctx, cooler).items()},
           "roll_deg": cfg["_roll"], "rope_sign": cfg["_sign"], "unreal_cooler": fa.ue_transform(P["cooler"])}
    if STAGE == "A2":                                     # the breath loop must stay in reach (arms near straight)
        bends, fails = [], []
        for f in range(0, fa.LOOP_FRAMES + 1, 3):
            try:
                bends.append(min(v["elbow_bend_deg"] for v in pose(B, sides, cfg, f)[1].values()))
            except RuntimeError:
                fails.append(f)
        res["loop_min_elbow_bend_deg"], res["loop_out_of_reach_frames"] = (round(min(bends), 1) if bends else None,
                                                                           fails)
        fa.apply_basis(arm_obj, fa.pose_to_basis(B, P))
        bpy.context.view_layer.update()
    if outside:
        st = ObserverStage(ctx, P, cooler, fish, LID_OPEN_PITCH_DEG)
        try:
            res["observer_px"] = st.read(groups)
        finally:
            st.close(P, lid)
    res["passes"] = (min(f["visible_pct"] for f in fists.values()) >= FIST_MIN_PCT
                     and cov["cooler"] <= BODY_MAX_PCT)
    return res, P, groups


def carry_reference(ctx, cooler, fish):
    """The accepted carry (CarryCooler_Idle f0, lid closed, no fish shown): the fist-visibility yardstick."""
    B, arm_obj, mesh_obj = ctx["B"], ctx["arm_obj"], ctx["mesh_obj"]
    P, m_carry = ctx["poser"].carry(0)[:2]
    fa.apply_basis(arm_obj, fa.pose_to_basis(B, P))
    bpy.context.view_layer.update()
    cooler.place(P["cooler"], 0.0)
    for o in fish.objs:
        o.hide_render = o.hide_viewport = True
    bpy.context.view_layer.update()
    groups = {"lid": {cooler.lid.name}, "cooler": {cooler.body.name}, "arms": {mesh_obj.name}}
    cov = coverage(groups, whole_frame=False)["centre_box"]
    fists = fist_pixels(ctx, groups["lid"] | groups["cooler"])
    for o in fish.objs:
        o.hide_render = o.hide_viewport = False
    return {"centre_box_pct": cov, "fists": fists, "fist_screen_pct": fist_screen(P, B, ctx["sides"]),
            "wrists": m_carry, "wrist_cost": round(wrist_cost(m_carry), 1)}


def a2_line(key, r):
    f, c, o = r["fists"], r["centre_box_pct"], r.get("observer_px", {})
    return ("%-20s fists L %4.0f%% R %4.0f%% wrist px %5d/%5d | box body %4.1f%% lid %4.1f%% fish %4.1f%% arms %4.1f%% | out fish %5d "
            "liner %5d lid %5d body %5d | wrist %5.1f hull %d/%.1fmm %s"
            % (key, f["l"]["visible_pct"], f["r"]["visible_pct"], f["l"]["wrist_seen_px"], f["r"]["wrist_seen_px"],
               c["cooler"], c["lid"], c["fish"], c["arms"],
               o.get("fish", 0), o.get("liner", 0), o.get("lid", 0), o.get("body", 0), r["wrist_cost"],
               r["hull_verts"], r["hull_depth_mm"], "PASS" if r["passes"] else ""))


def gate_a2(ctx):
    cooler = OpenCooler()
    fish = StagedFish()
    ctx["fists"] = fist_indices(ctx["mesh_obj"])
    ctx["fist_polys"] = fist_polygons(ctx["mesh_obj"], ctx["fists"])
    ctx["wrist_polys"] = fist_polygons(ctx["mesh_obj"], fist_indices(ctx["mesh_obj"], ("hand", "fingers", "thumb",
                                                                                          "lowerarm_twist")))
    sd = ctx["sides"]["r"]
    out = {"arm_reach_m": round(sd.L1 + sd.L2, 3), "shoulder_r_cam": [round(c, 3) for c in ctx["B"]["upperarm_r"].translation],
           "carry_ref": carry_reference(ctx, cooler, fish), "search": {}, "table": []}
    full = {}
    only = os.environ.get("T064A_GRID_PREFIX", "")      # search subset (key prefix match), e.g. "x59_,x60_z28_t68"
    grid_s = [(k, c) for k, c in SHOW_GRID if not only or any(k.startswith(p_) for p_ in only.split(","))]
    todo = [SHOW_REF] + (grid_s if STAGE == "A2S" else [(k, c) for k, c in SHOW_GRID if k in SHOW_A2])
    scan = os.environ.get("T064A_ROLLSCAN", "")          # "key[,key]": every fist roll on the rope, no outside read
    if scan:
        grid = dict(SHOW_GRID)
        todo = [("%s_r%d_s%d" % (k, r_, sg), dict(grid[k], roll=float(r_), sign=float(sg)))
                for k in scan.split(",") for sg in (1, -1) for r_ in range(-180, 180, 15)]
    for key, cfg in todo:
        cfg = dict(cfg)
        try:
            r, P, groups = show_eval(ctx, cfg, cooler, fish, outside=not scan)
        except RuntimeError as e:
            out["search"][key] = {"error": str(e)}
            out["table"].append("%-20s ERROR %s" % (key, e))
            continue
        out["search"][key] = r
        out["table"].append(a2_line(key, r))
        print("A2 " + out["table"][-1], flush=True)
        if STAGE != "A2":
            continue
        # the candidates strip: FP day with the box, outside at 2.5 m (2x crop and true 90 deg), side view
        c, f = r["centre_box_pct"], r["fists"]
        o = r["observer_px"]
        cells = []
        cb = stage_centre_box()
        try:
            cells.append(fa.fp_frame(PREVIEW_DIR / ("exp_t064a_a2_%s_fp.png" % key), "day",
                                     "%s FP | body %.0f%% of box | fists L%.0f R%.0f%% (carry %.0f) | wrist skin %.1fk px "
                                     "(carry %.1fk) | (%.0f, %.0f) cm, %+.0f deg"
                                     % (key, c["cooler"], f["l"]["visible_pct"], f["r"]["visible_pct"],
                                        out["carry_ref"]["fists"]["r"]["visible_pct"],
                                        f["r"]["wrist_seen_px"] / 1000.0,
                                        out["carry_ref"]["fists"]["r"]["wrist_seen_px"] / 1000.0,
                                        cfg["handles"].x * 100, -cfg["handles"].z * 100, cfg["tilt"]),
                                     res=(960, 540), samples=24))
        finally:
            cb()
        st = ObserverStage(ctx, P, cooler, fish, LID_OPEN_PITCH_DEG)
        try:
            cells.append(st.render(PREVIEW_DIR / ("exp_t064a_a2_%s_out2x.png" % key),
                                   "%s outside 2.5 m, eye 1.70, 2x crop | fish %d px, mouth %d px, "
                                   "lid %d px" % (key, o["fish"], o["liner"], o["lid"]),
                                   ZOOM_HFOV_DEG))
            cells.append(st.render(PREVIEW_DIR / ("exp_t064a_a2_%s_out90.png" % key),
                                   "%s outside: same, the true 90 deg frame" % key, fp_preview.FP_HFOV_DEG))
        finally:
            st.close(P, LID_OPEN_PITCH_DEG)
        fg = stage_frustum_side()
        try:
            cells.append(fa.shot(PREVIEW_DIR / ("exp_t064a_a2_%s_side.png" % key), (0.3, -2.4, -0.15),
                                 (0.3, 0.0, -0.15), ortho=1.3, res=(960, 540),
                                 label="%s side: eye orange, frame edges black, centre box amber" % key))
        finally:
            fg()
        C = P["cooler"]
        hr = C @ fa.COOLER_HANDLE_L                      # the right fist's rope (Handle_L)
        w = r["wrists"]["r"]
        cells.append(fa.shot(PREVIEW_DIR / ("exp_t064a_a2_%s_grip.png" % key), hr + Vec((0.45, -0.45, 0.05)),
                             hr + Vec((0.0, 0.0, -0.02)), lens=35.0, res=(960, 540),
                             label="%s right fist | flex %.0f dev %.0f twist %.0f (carry %.0f/%.0f/%.0f)" % (key, w["wrist_flex_deg"], w["wrist_dev_deg"],
                                                                    w["twist_deg"], *[out["carry_ref"]["wrists"]["r"][k_]
                                                                    for k_ in ("wrist_flex_deg", "wrist_dev_deg",
                                                                               "twist_deg")])))
        full[key] = cells
    if STAGE == "A2" and full:
        keys = [k for k in [SHOW_REF[0]] + SHOW_A2 if k in full]
        rows = []
        for i in range(5):
            rows += [full[k][i] for k in keys]
        full["sheet"] = pb.contact_sheet(rows, PREVIEW_DIR / "SK_FPArms_cooler_gateA2_show.png", cols=len(keys),
                                         cell=(960, 540))
    return out, full


# ---------------------------------------------------------------------------------------------------------------
# Gate B (the default run): the two 3.0 s loops keyed and exported (cm, pb.export_skeletal_fbx), motion / contact /
# re-import checks, the crossfade + Two Bone IK simulation, and the STOP 2 previews.
# ---------------------------------------------------------------------------------------------------------------
EXPORT_DIR = REPO / "art" / "export" / CATEGORY
OPEN = "A_FPArms_CarryCooler_Open"
CARRY = "A_FPArms_CarryCooler_Idle"          # anim_fp_arms' carry (read-only), the crossfade partner
ACTIONS = [(OPEN, 0, fa.LOOP_FRAMES), (SHOW, 0, fa.LOOP_FRAMES)]   # 0-90 = 3.0 s, frame 90 = frame 0
LID_FP_OPEN_DEG = LID_PROPOSAL_DEG           # accepted at gate A: the owner's carried-open lid (eng follow-up)
LID_FP_SHOW_DEG = float(os.environ.get("T064A_SHOW_LID", "235"))   # the owner's lid while showing (see the spec)
LID_OPEN_TIME_S = 0.25                       # DT_Catch LidOpenTime (the lid's own opening, engineering)
SHOW_TURN_TIME_S = 0.4                       # DT_Catch ShowTurnTime = the arm-pose blend time
IK_TOL_MM = 10.0                             # art-mgr: fists within 1 cm of the ropes through every crossfade
XFADE_PAIRS = [(CARRY, OPEN), (CARRY, SHOW), (OPEN, SHOW)]
XFADE_ALPHAS = [i / 10.0 for i in range(1, 10)]
XFADE_FRAMES = (0, 45)                       # sync group FPArmsBreath: both clips at the same phase
XFADE_MODES = ("none", "ik", "ik_rel", "fit", "fit_ik")
SOCKET_OF = {"l": "SOCKET_Handle_R", "r": "SOCKET_Handle_L"}   # the cooler faces the player
HAND_CHILDREN = {"r": ("fingers_r", "thumb_r", "hand_r_rod", "hand_l_crank", "hand_r_fish"),
                 "l": ("fingers_l", "thumb_l")}
PROBES = [("hand_r", "head"), ("hand_l", "head"), ("fingers_r", "tail"), ("fingers_l", "tail"), ("thumb_r", "tail"),
          ("thumb_l", "tail"), ("lowerarm_r", "head"), ("lowerarm_l", "head"), ("cooler", "head")]
RENDER = os.environ.get("T064A_RENDER", "1") == "1"


def short(name):
    return name.replace("A_FPArms_CarryCooler_", "").replace("A_FPArms_", "")


def clip_pose(ctx, name, f):
    if name == CARRY:
        return ctx["poser"].carry(f)[0]
    return pose(ctx["B"], ctx["sides"], ctx["cfgs"][name], f)[0]


def fp_lid(name):
    """The lid the OWNER sees in each pose (other players: LID_OPEN_PITCH_DEG when open, from the replicated state)."""
    return {CARRY: 0.0, OPEN: LID_FP_OPEN_DEG, SHOW: LID_FP_SHOW_DEG}[name]


def static(arm_obj):
    ad = arm_obj.animation_data
    if ad:
        for tr in ad.nla_tracks:
            tr.mute = True
        ad.action = None


def set_pose(ctx, P):
    static(ctx["arm_obj"])
    fa.apply_basis(ctx["arm_obj"], fa.pose_to_basis(ctx["B"], P))
    bpy.context.view_layer.update()


def pose_now(arm_obj):
    return {p.name: p.matrix.copy() for p in arm_obj.pose.bones}


def ue_vec_cm(v):
    return [round(v.x * 100.0, 1), round(-v.y * 100.0, 1), round(v.z * 100.0, 1)]


def grip_point(P, B, sides, s):
    return (P["hand_" + s] @ B["hand_" + s].inverted() @ fa.rod_rest_matrix(sides[s])).translation


def grip_errors_mm(P, ctx, cooler):
    C = P["cooler"]
    return {s: round((grip_point(P, ctx["B"], ctx["sides"], s) - C @ cooler.sockets[SOCKET_OF[s]]).length * 1000.0, 1)
            for s in ("l", "r")}


def hand_in_cooler(P, s):
    return P["cooler"].inverted() @ P["hand_" + s].translation


def two_bone_ik(Pm, s, target, keep_rel=False):
    """Unreal FAnimNode_TwoBoneIK on upperarm/lowerarm/hand: the upper arm turns by the shortest arc to the new elbow,
    the lower arm to the target; the hand keeps its COMPONENT-space rotation (bTakeRotationFromEffectorSpace and
    bMaintainEffectorRelRot off; keep_rel=True simulates bMaintainEffectorRelRot); joint target = the input elbow
    (the bend plane is kept); no stretching (a target out of reach is clamped to full reach). The twist bone and the
    hand's children follow rigidly."""
    P = dict(Pm)
    S, E, W = (Pm[b + "_" + s].translation for b in ("upperarm", "lowerarm", "hand"))
    L1, L2 = (E - S).length, (W - E).length
    d = target - S
    reach = (L1 + L2) * 0.999                  # fa.ik_elbow's own limit is 0.9995
    clamped = d.length > reach
    T = S + d.normalized() * reach if clamped else target
    E2 = fa.ik_elbow(S, T, L1, L2, E - S)
    R1 = (E - S).rotation_difference(E2 - S).to_matrix()
    R2 = (R1 @ (W - E)).rotation_difference(T - E2).to_matrix()
    Rl = R2 @ R1
    P["upperarm_" + s] = fa.mat4(R1 @ Pm["upperarm_" + s].to_3x3(), S)
    P["lowerarm_" + s] = fa.mat4(Rl @ Pm["lowerarm_" + s].to_3x3(), E2)
    tw = Pm["lowerarm_twist_" + s]
    P["lowerarm_twist_" + s] = fa.mat4(Rl @ tw.to_3x3(), E2 + Rl @ (tw.translation - E))
    Rh = (Rl @ Pm["hand_" + s].to_3x3()) if keep_rel else Pm["hand_" + s].to_3x3()
    P["hand_" + s] = fa.mat4(Rh, T)
    D = P["hand_" + s] @ Pm["hand_" + s].inverted()
    for c in HAND_CHILDREN[s]:
        P[c] = D @ Pm[c]
    return P, clamped


def crossfade(ctx, Pa, Pb, alpha, mode):
    """Unreal's linear crossfade of two clip poses at `alpha` (+ the proposed Two Bone IK). mode: "none", "ik"
    (position only, hand keeps its component rotation: the proposal) or "ik_rel" (bMaintainEffectorRelRot).
    IK target per fist = the blended `cooler` bone @ lerp(hand-in-cooler of A, of B, alpha): constants per clip."""
    B, arm_obj = ctx["B"], ctx["arm_obj"]
    static(arm_obj)
    fa.apply_basis(arm_obj, fa.blend_basis(fa.pose_to_basis(B, Pa), fa.pose_to_basis(B, Pb), alpha))
    bpy.context.view_layer.update()
    Pm = pose_now(arm_obj)
    clamped = False
    if mode == "fit":
        return fit_cooler(Pm, ctx), False
    if mode == "fit_ik":
        # fit, then close the rest (the grips' spacing changes a little in a blend) with a position-only two-bone IK
        # per arm: each hand moves by (its socket - its grip point); the hand keeps its component rotation
        Pm = fit_cooler(Pm, ctx)
        cooler = ctx["cooler_stage"]
        for s in ("l", "r"):
            d = Pm["cooler"] @ cooler.sockets[SOCKET_OF[s]] - grip_point(Pm, B, ctx["sides"], s)
            Pm, c = two_bone_ik(Pm, s, Pm["hand_" + s].translation + d)
            clamped = clamped or c
        return Pm, clamped
    if mode != "none":
        for s in ("l", "r"):
            target = Pm["cooler"] @ hand_in_cooler(Pa, s).lerp(hand_in_cooler(Pb, s), alpha)
            Pm, c = two_bone_ik(Pm, s, target, keep_rel=(mode == "ik_rel"))
            clamped = clamped or c
    return Pm, clamped


def fit_cooler(Pm, ctx):
    """The gate B fix (eng follow-up, a small C++ skeletal-control node after the pose blend): move the `cooler` bone
    to the blended FISTS instead of the fists to the cooler. Grip points = each hand's rope-grip point (a constant in
    hand space); the cooler keeps its blended rotation, turned by the shortest arc that lines its handle axis up with
    the grip-to-grip line, and is placed so the midpoint of its two handle sockets sits midway between the grips. The
    cooler then turns about its handles (not about its base, which is what swings the handles off the fists in a
    plain blend). At an authored pose it changes nothing (the grips are on the sockets already)."""
    B, sides = ctx["B"], ctx["sides"]
    cooler = ctx["cooler_stage"]
    gl, gr = grip_point(Pm, B, sides, "l"), grip_point(Pm, B, sides, "r")
    sl, sr = cooler.sockets[SOCKET_OF["l"]], cooler.sockets[SOCKET_OF["r"]]
    C = Pm["cooler"]
    a_c = (C.to_3x3() @ (sl - sr)).normalized()
    R = a_c.rotation_difference((gl - gr).normalized()).to_matrix() @ C.to_3x3()
    P = dict(Pm)
    P["cooler"] = fa.mat4(R, (gl + gr) * 0.5 - R @ ((sl + sr) * 0.5))
    return P


def xfade_lid(a, b, alpha):
    """The owner's lid during a crossfade: opens over LidOpenTime while the arms blend over ShowTurnTime."""
    la, lb = fp_lid(a), fp_lid(b)
    k = min(1.0, alpha * SHOW_TURN_TIME_S / LID_OPEN_TIME_S) if la != lb else 1.0
    return la + (lb - la) * k


def crossfade_checks(ctx, cooler):
    out = {}
    for a, b in XFADE_PAIRS:
        key = "%s->%s" % (short(a), short(b))
        row = {m: {"max_mm": {"l": 0.0, "r": 0.0}, "worst_alpha": None, "hull": {"verts": 0, "depth_mm": 0.0},
                   "hull_by_group": {}, "clamped": False}
               for m in XFADE_MODES}
        row["ik_clamped"] = False
        row["fit_cooler_shift_max_cm"] = 0.0
        for f in XFADE_FRAMES:
            Pa, Pb = clip_pose(ctx, a, f), clip_pose(ctx, b, f)
            for alpha in XFADE_ALPHAS:
                for m in XFADE_MODES:
                    P, cl = crossfade(ctx, Pa, Pb, alpha, m)
                    e = grip_errors_mm(P, ctx, cooler)
                    r = row[m]
                    if max(e.values()) > max(r["max_mm"].values()):
                        r["worst_alpha"] = [f, alpha]
                    r["max_mm"] = {s: max(r["max_mm"][s], e[s]) for s in e}
                    r["clamped"] = r["clamped"] or cl
                    if m == "ik":
                        row["ik_clamped"] = row["ik_clamped"] or cl
                    if m == "fit":
                        Pn = pose_now(ctx["arm_obj"])
                        row["fit_cooler_shift_max_cm"] = round(max(
                            row["fit_cooler_shift_max_cm"],
                            100.0 * max((P["cooler"] @ v - Pn["cooler"] @ v).length
                                        for v in cooler.sockets.values())), 1)
                    if f == 0 and alpha in (0.3, 0.5, 0.7):
                        set_pose(ctx, P)
                        cooler.place(P["cooler"], xfade_lid(a, b, alpha))
                        n, dep = fa.inside_convex(arms_points(ctx), cooler.ucx)
                        r["hull"] = {"verts": max(n, r["hull"]["verts"]),
                                     "depth_mm": round(max(dep, r["hull"]["depth_mm"]), 1)}
                        for g, (n_, d_) in penetration_by_group(ctx, cooler).items():
                            old = r["hull_by_group"].get(g, [0, 0.0])
                            r["hull_by_group"][g] = [max(old[0], n_), round(max(old[1], d_), 1)]
        row["ik_ok"] = max(row["ik"]["max_mm"].values()) <= IK_TOL_MM
        row["fit_ok"] = max(row["fit"]["max_mm"].values()) <= IK_TOL_MM
        row["fit_ik_ok"] = max(row["fit_ik"]["max_mm"].values()) <= IK_TOL_MM
        out[key] = row
    return out


def print_xfades(xf):
    for key, row in xf.items():
        print("GATEB XFADE %-10s %s" % (key, " | ".join(
            "%s L%5.1f R%5.1f hull %d/%.1f%s" % (m, row[m]["max_mm"]["l"], row[m]["max_mm"]["r"], row[m]["hull"]["verts"],
                                                row[m]["hull"]["depth_mm"], " CLAMPED" if row[m]["clamped"] else "")
            for m in XFADE_MODES)), flush=True)
        print("GATEB XFADE %-10s fit moves the cooler up to %.1f cm; hull by group: %s"
              % (key, row["fit_cooler_shift_max_cm"], {m: row[m]["hull_by_group"] for m in XFADE_MODES}), flush=True)


def build_actions(ctx):
    B, arm_obj = ctx["B"], ctx["arm_obj"]
    return {name: fa.key_action(arm_obj, name, list(range(f0, f1 + 1)),
                                lambda f, n=name: fa.pose_to_basis(B, clip_pose(ctx, n, f)))
            for name, f0, f1 in ACTIONS}


def motion_and_contact(ctx, cooler):
    """On the KEYED actions: per-frame steps and loop seam (pops), each fist's grip point vs its rope socket (sliding),
    the cooler's breath range, arms inside the cooler hull and the elbow bend over the loop."""
    arm_obj, B, sides = ctx["arm_obj"], ctx["B"], ctx["sides"]
    out = {}
    for name, f0, f1 in ACTIONS:
        pos, drift, cs, qs, bends = [], 0.0, [], [], []
        hull = {"verts": 0, "depth_mm": 0.0, "by_group": {}}
        for f in range(f0, f1 + 1):
            fa.play(arm_obj, name, f)
            P = pose_now(arm_obj)
            pos.append([getattr(arm_obj.pose.bones[b], end).copy() for b, end in PROBES])
            drift = max([drift] + list(grip_errors_mm(P, ctx, cooler).values()))
            cs.append(P["cooler"].translation.copy())
            qs.append(P["cooler"].to_quaternion())
            if f % 6 == 0:
                cooler.place(P["cooler"], fp_lid(name))
                n, dep = fa.inside_convex(arms_points(ctx), cooler.ucx)
                hull["verts"], hull["depth_mm"] = max(hull["verts"], n), round(max(hull["depth_mm"], dep), 1)
                for g, (n_, d_) in penetration_by_group(ctx, cooler).items():
                    old = hull["by_group"].get(g, [0, 0.0])
                    hull["by_group"][g] = [max(old[0], n_), round(max(old[1], d_), 1)]
                bends.append(min(v["elbow_bend_deg"] for v in pose(B, sides, ctx["cfgs"][name], f)[1].values()))
        steps = [max((a - b).length for a, b in zip(pos[i + 1], pos[i])) for i in range(len(pos) - 1)]
        out[name] = {"frames": [f0, f1], "max_step_mm": round(max(steps) * 1000, 2),
                     "first_last_delta_mm": round(max((a - b).length for a, b in zip(pos[-1], pos[0])) * 1000, 3),
                     "loop_seam_step_mm": round(max((a - b).length for a, b in zip(pos[1], pos[0])) * 1000, 2),
                     "fist_to_rope_max_mm": round(drift, 2),
                     "cooler_breath_range_cm": [round((max(p[i] for p in cs) - min(p[i] for p in cs)) * 100, 2)
                                                for i in range(3)],
                     "cooler_breath_max_turn_deg": round(max(math.degrees(qs[0].rotation_difference(q).angle)
                                                             for q in qs), 2),
                     "arms_in_cooler_hull": hull, "loop_min_elbow_bend_deg": min(bends)}
    static(arm_obj)
    return out


def export_clips(ctx):
    arm_obj = ctx["arm_obj"]
    ad = arm_obj.animation_data
    ad.action = None
    paths, units = {}, {}
    for name, _f0, _f1 in ACTIONS:
        for tr in ad.nla_tracks:
            tr.mute = tr.name != name
        path = EXPORT_DIR / (name + ".fbx")
        units[name] = pb.export_skeletal_fbx(path, arm_obj, [], bake_anim=True)
        paths[name] = str(path)
    static(arm_obj)
    fa.rest_pose(arm_obj)
    return paths, units


def skeleton_check(ctx):
    """The rig this recipe keys against the committed SK_FPArms.fbx (SKEL_FPArms): same 18 bones, same rest."""
    B, k = ctx["B"], pb.SKELETAL_FBX_CM_PER_UNIT
    snap = fa.snapshot()
    arm, _mesh = fa.import_fbx(EXPORT_DIR / "SK_FPArms.fbx")
    mw = arm.matrix_world
    names = [b.name for b in arm.data.bones]
    res = {"bones": len(names), "same_names": sorted(names) == sorted(B),
           "head_err_mm": round(max((arm.data.bones[n].head_local - B[n].translation * k).length for n in B) * 10, 3),
           "axis_err_deg": round(max(math.degrees((mw.to_3x3().normalized() @ arm.data.bones[n].matrix_local.to_3x3())
                                                  .to_quaternion().rotation_difference(B[n].to_3x3().to_quaternion())
                                                  .angle) for n in B), 3)}
    fa.cleanup_since(snap)
    return res


def reimport_check(ctx, paths):
    scene = bpy.context.scene
    fps = scene.render.fps
    res = {}
    for name, _f0, f1 in ACTIONS:
        snap = fa.snapshot()
        arm, _m = fa.import_fbx(paths[name])
        act = arm.animation_data.action if arm.animation_data else None
        entry = {"action": act.name if act else None, "bones": len(arm.data.bones),
                 "frame_range": [round(c, 2) for c in act.frame_range] if act else None,
                 "armature_object_scale": [round(c, 6) for c in arm.matrix_world.to_scale()]}
        sdev, err, rot = 0.0, 0.0, 0.0
        for f in range(0, f1 + 1, 3):
            scene.frame_set(f)
            sdev = max([sdev] + [fa.scale_dev(p.matrix) for p in arm.pose.bones])
        for f in (0, 30, 45, 60, 90):
            scene.frame_set(f)
            P = clip_pose(ctx, name, f)
            for n in ctx["B"]:
                M = arm.matrix_world @ arm.pose.bones[n].matrix
                err = max(err, (M.translation - P[n].translation).length)
                rot = max(rot, math.degrees(M.to_quaternion().rotation_difference(P[n].to_quaternion()).angle))
        entry.update({"posed_bone_scale_dev": sdev, "max_pose_error_mm": round(err * 1000, 3),
                      "max_pose_error_deg": round(rot, 3)})
        res[name] = entry
        fa.cleanup_since(snap)
    scene.render.fps = fps
    return res


def socket_markers(C, cooler, hex_color="#E0402A", size=0.012):
    objs = [fa.box("PV_Sock_" + s, C @ cooler.sockets[SOCKET_OF[s]], (size, size, size), hex_color)
            for s in ("l", "r")]
    bpy.context.view_layer.update()

    def cleanup():
        for o in objs:
            me = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            bpy.data.meshes.remove(me)
    return cleanup


def top_view(path, P, cooler, label):
    """Above-and-behind the eye, looking down at the handles (both fists and ropes, about 1 px per mm)."""
    C = P["cooler"]
    mid = C @ ((cooler.sockets["SOCKET_Handle_L"] + cooler.sockets["SOCKET_Handle_R"]) * 0.5)
    return fa.shot(path, Vec((mid.x - 0.30, 0.0, mid.z + 0.62)), mid, lens=30.0, res=(960, 540), label=label)


def gate_b(ctx):
    cooler = OpenCooler()
    fish = StagedFish()
    ctx["cooler_stage"] = cooler
    mesh_obj = ctx["mesh_obj"]
    ctx["fists"] = fist_indices(mesh_obj)
    ctx["fist_polys"] = fist_polygons(mesh_obj, ctx["fists"])
    ctx["wrist_polys"] = fist_polygons(mesh_obj, fist_indices(mesh_obj, ("hand", "fingers", "thumb", "lowerarm_twist")))
    ctx["cfgs"] = {}
    out = {"lid": {"LidOpenPitch_replicated": LID_OPEN_PITCH_DEG, "fp_open": LID_FP_OPEN_DEG,
                   "fp_show": LID_FP_SHOW_DEG}}
    for name in POSES:
        cfg = dict(POSES[name])
        r_, s_ = solve_roll(ctx, cfg, cooler)
        cfg.update(roll=r_, sign=s_, _roll=r_, _sign=s_)
        ctx["cfgs"][name] = cfg
    out["grip_roll"] = {short(n): [c["_roll"], c["_sign"]] for n, c in ctx["cfgs"].items()}
    print("GATEB rolls %s" % out["grip_roll"], flush=True)

    # 1. keyed actions + checks on them, exports, re-import
    build_actions(ctx)
    if os.environ.get("T064A_XFADE_ONLY") == "1":
        out["crossfades"] = crossfade_checks(ctx, cooler)
        print_xfades(out["crossfades"])
        return {"exports": {OPEN: ""}, "crossfades": out["crossfades"]}, {}
    out["motion"] = motion_and_contact(ctx, cooler)
    print("GATEB motion %s" % json.dumps(out["motion"]), flush=True)
    out["skeleton_vs_SK_FPArms_fbx"] = skeleton_check(ctx)
    paths, units = export_clips(ctx)
    out["exports"], out["fbx_units"] = paths, units
    out["reimport_check"] = reimport_check(ctx, paths)
    print("GATEB reimport %s | skeleton %s" % (json.dumps(out["reimport_check"]),
                                               out["skeleton_vs_SK_FPArms_fbx"]), flush=True)

    # 2. numbers for the spec: the cooler bone at frame 0 (Unreal component space), hand targets in cooler space
    out["unreal"] = {}
    for name in (CARRY, OPEN, SHOW):
        P0 = clip_pose(ctx, name, 0)
        out["unreal"][short(name)] = {"cooler_f0": fa.ue_transform(P0["cooler"]),
                                      "hand_in_cooler_cm": {"hand_" + s: ue_vec_cm(hand_in_cooler(P0, s))
                                                            for s in ("l", "r")},
                                      "fist_to_rope_f0_mm": grip_errors_mm(P0, ctx, cooler)}
    # the grip node's constants: each fist's rope-grip point at the bind pose (component space, cm) and the sockets
    out["unreal"]["grip_node"] = {
        "grip_ref_cs_cm": {"hand_" + s: ue_vec_cm(fa.rod_rest_matrix(ctx["sides"][s]).translation) for s in ("l", "r")},
        "hand_ref_cs_cm": {"hand_" + s: ue_vec_cm(ctx["B"]["hand_" + s].translation) for s in ("l", "r")},
        "sockets_cooler_cm": {k: ue_vec_cm(v) for k, v in cooler.sockets.items() if "Handle" in k}}
    print("GATEB unreal %s" % json.dumps(out["unreal"]), flush=True)

    # 3. frame-0 look measurements (the owner's lid) + the observer's read of Show
    res_open = {}
    for lid in (LID_FP_OPEN_DEG, LID_OPEN_PITCH_DEG):
        r, _P = evaluate(ctx, dict(ctx["cfgs"][OPEN], lid=lid), cooler, fish)
        res_open["lid%d" % lid] = {k: r[k] for k in ("coverage_pct", "lid_top_screen_pct", "cooler_top_screen_pct",
                                                     "wrists", "inside_by_group")}
    out["open_f0"] = res_open
    res_show = {}
    for lid in sorted({LID_OPEN_PITCH_DEG, LID_FP_SHOW_DEG, LID_FP_OPEN_DEG}):
        r, _P, _g = show_eval(ctx, dict(ctx["cfgs"][SHOW]), cooler, fish, outside=(lid == LID_FP_SHOW_DEG), lid=lid)
        res_show["lid%d" % lid] = {k: r[k] for k in r if k != "unreal_cooler"}
        print("GATEB show lid %d: %s" % (lid, a2_line("show", r)), flush=True)
    out["show_f0"] = res_show
    for k, v in res_open.items():
        print("GATEB open %s: centre box %s" % (k, v["coverage_pct"]["centre_box"]), flush=True)

    # 4. crossfades: fists vs ropes without / with the proposed IK
    out["crossfades"] = crossfade_checks(ctx, cooler)
    print_xfades(out["crossfades"])
    full =gate_b_previews(ctx, cooler, fish, out) if RENDER else {}
    return out, full


def gate_b_previews(ctx, cooler, fish, out):
    B, sides, arm_obj = ctx["B"], ctx["sides"], ctx["arm_obj"]
    full = {}

    def pose_scene(P, lid):
        set_pose(ctx, P)
        cooler.place(P["cooler"], lid)
        fish.place(P["cooler"])

    # FP frames (frame 0), 1920x1080, centre box drawn
    P_open = clip_pose(ctx, OPEN, 0)
    for lid, tag in ((LID_FP_OPEN_DEG, ""), (LID_OPEN_PITCH_DEG, "_lid%d" % LID_OPEN_PITCH_DEG)):
        c = out["open_f0"]["lid%d" % lid]["coverage_pct"]["centre_box"]
        what = ("MAIN: lid %d (owner only, eng follow-up)" % lid if not tag
                else "FALLBACK: lid %d (LidOpenPitch, no follow-up)" % lid)
        for kind in ("day", "dusk"):
            pose_scene(P_open, lid)
            cb = stage_centre_box()
            try:
                fn = "SK_FPArms_cooler_open%s_fp%s.png" % (tag, "" if kind == "day" else "_dusk")
                full["open%s_%s" % (tag, kind)] = fa.fp_frame(
                    PREVIEW_DIR / fn, kind, "CarryCooler_Open f0 %s | %s | centre box: lid %.0f%% cooler %.0f%% "
                    "fish %.0f%% arms %.0f%%" % (kind, what, c["lid"], c["cooler"], c["fish"], c["arms"]))
            finally:
                cb()
    P_show = clip_pose(ctx, SHOW, 0)
    rs = out["show_f0"]["lid%d" % LID_FP_SHOW_DEG]
    c, f = rs["centre_box_pct"], rs["fists"]
    for kind in ("day", "dusk"):
        pose_scene(P_show, LID_FP_SHOW_DEG)
        cb = stage_centre_box()
        try:
            full["show_" + kind] = fa.fp_frame(
                PREVIEW_DIR / ("SK_FPArms_cooler_show_fp%s.png" % ("" if kind == "day" else "_dusk")), kind,
                "CarryCooler_Show f0 %s | owner's lid %d | centre box: cooler %.1f%% lid %.1f%% fish %.1f%% | fists "
                "visible L %.0f%% R %.0f%%" % (kind, LID_FP_SHOW_DEG, c["cooler"], c["lid"], c["fish"],
                                               f["l"]["visible_pct"], f["r"]["visible_pct"]))
        finally:
            cb()

    # outside: the other player at 2.5 m (eye 1.70 m), straight on (2x crop + true 90 deg) and 35 deg to the right
    o = rs.get("observer_px", {})
    cells = []
    set_pose(ctx, P_show)
    st = ObserverStage(ctx, P_show, cooler, fish, LID_OPEN_PITCH_DEG)
    try:
        cells.append(st.render(PREVIEW_DIR / "exp_t064a_b_out2x.png",
                               "Show f0, other player 2.5 m ahead (eye 1.70), 2x crop\nfish %d px, mouth %d px, "
                               "lid %d px (lid %d)" % (o.get("fish", 0), o.get("liner", 0), o.get("lid", 0),
                                                       LID_OPEN_PITCH_DEG), ZOOM_HFOV_DEG))
        cells.append(st.render(PREVIEW_DIR / "exp_t064a_b_out90.png", "same, the true 90 deg frame",
                               fp_preview.FP_HFOV_DEG))
    finally:
        st.close(P_show, LID_FP_SHOW_DEG)
    a = math.radians(35.0)
    pos = Vec((2.5 * math.cos(a), -2.5 * math.sin(a), OBSERVER_EYE_M - EYE_M))
    tgt = P_show["cooler"].translation
    yaw = math.degrees(math.atan2(tgt.y - pos.y, tgt.x - pos.x))
    O = Matrix.Translation(pos) @ fa.rot3((0, 0, 1), yaw).to_4x4()
    st = ObserverStage(ctx, P_show, cooler, fish, LID_OPEN_PITCH_DEG, O=O)
    try:
        cells.append(st.render(PREVIEW_DIR / "exp_t064a_b_out35.png",
                               "Show f0, other player 2.5 m away, 35 deg to the carrier's right, 2x crop",
                               ZOOM_HFOV_DEG))
    finally:
        st.close(P_show, LID_FP_SHOW_DEG)
    full["show_outside"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_cooler_show_outside.png", cols=3,
                                            cell=(960, 540))

    # grip close-ups: both fists of both clips, frame 0 (Workbench), rope sockets marked red
    cells = []
    for name in (OPEN, SHOW):
        P = clip_pose(ctx, name, 0)
        pose_scene(P, fp_lid(name))
        m = pose(B, sides, ctx["cfgs"][name], 0)[1]
        hull = out["motion"][name]["arms_in_cooler_hull"]["by_group"]
        mk = socket_markers(P["cooler"], cooler)
        try:
            for s, sock in (("r", "SOCKET_Handle_L"), ("l", "SOCKET_Handle_R")):
                h = P["cooler"] @ cooler.sockets[sock]
                mid = P["cooler"] @ Vec((0.0, 0.0, cooler.sockets[sock].z))
                side = (h - mid).normalized()                 # out along the handle axis, past the fist
                w = m[s]
                sink = max([hull.get(g, [0, 0.0])[1] for g in ("fingers_" + s, "thumb_" + s, "hand_" + s)])
                cells.append(fa.shot(PREVIEW_DIR / ("exp_t064a_b_grip_%s_%s.png" % (short(name).lower(), s)),
                                     h + side * 0.38 + Vec((-0.10, 0.0, 0.14)), h, lens=35.0, res=(960, 540),
                                     label="%s f0 %s fist, side-on | wrist flex %.0f dev %.0f twist %.0f | "
                                           "fingers %.0f mm into the wall"
                                           % (short(name), "R" if s == "r" else "L", w["wrist_flex_deg"],
                                              w["wrist_dev_deg"], w["twist_deg"], sink)))
        finally:
            mk()
    full["grips"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_cooler_grips.png", cols=2, cell=(960, 540))

    # crossfade strips at mid-blend (frame 0): top view and FP; plain blend, + IK, + cooler fit
    cells, cells_fp = [], []
    for a_, b_ in XFADE_PAIRS:
        key = "%s->%s" % (short(a_), short(b_))
        row = out["crossfades"][key]
        Pa, Pb = clip_pose(ctx, a_, 0), clip_pose(ctx, b_, 0)
        lid = xfade_lid(a_, b_, 0.5)
        fps_, tops = [], []
        for m in ("none", "ik", "fit_ik"):
            P, _cl = crossfade(ctx, Pa, Pb, 0.5, m)
            e = grip_errors_mm(P, ctx, cooler)
            pose_scene(P, lid)
            tag = {"none": "plain blend", "ik": "+ Two Bone IK (gate A plan)",
                   "fit_ik": "+ cooler fit + IK (gate B fix)"}[m]
            stem = "exp_t064a_b_xf_%s_%s" % (key.replace("->", "_"), m)
            cb = stage_centre_box()
            try:
                fps_.append(fa.fp_frame(PREVIEW_DIR / (stem + "_fp.png"), "day",
                                        "%s 50%%, %s\nfists off the ropes %.1f cm here, max %.1f cm over the blend"
                                        % (key, tag, max(e.values()) / 10, max(row[m]["max_mm"].values()) / 10),
                                        res=(960, 540), samples=16))
            finally:
                cb()
            mk = socket_markers(P["cooler"], cooler)
            try:
                tops.append(top_view(PREVIEW_DIR / (stem + "_top.png"), P, cooler,
                                     "%s 50%%, %s, from above (red = rope sockets)" % (key, tag)))
            finally:
                mk()
        cells += tops
        cells_fp += fps_
    full["crossfade"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_cooler_crossfade_ik.png", cols=3,
                                         cell=(960, 540))
    full["crossfade_fp"] = pb.contact_sheet(cells_fp, PREVIEW_DIR / "SK_FPArms_cooler_crossfade_fp.png", cols=3,
                                            cell=(960, 540))

    # breath strip from the keyed actions (frames 0 / 23 / 45 / 68), FP
    cells = []
    for name, _f0, _f1 in ACTIONS:
        for fr in (0, 23, 45, 68):
            fa.play(arm_obj, name, fr)
            C = arm_obj.pose.bones["cooler"].matrix.copy()
            cooler.place(C, fp_lid(name))
            fish.place(C)
            cells.append(fa.fp_frame(PREVIEW_DIR / ("exp_t064a_b_anim_%s_%02d.png" % (short(name).lower(), fr)),
                                     "day", "%s f%d (keyed action)" % (short(name), fr), res=(640, 360), samples=8))
    static(arm_obj)
    full["anim"] = pb.contact_sheet(cells, PREVIEW_DIR / "SK_FPArms_cooler_anim.png", cols=4, cell=(640, 360))
    return full


def main():
    args = pb.parse_args(ASSET, CATEGORY)
    ctx = fa.setup()
    pb.ensure_fbx_exporter()           # the cooler / fish stagings import FBX
    for n in ("_roll", "_sign"):
        for cfg in POSES.values():
            cfg.pop(n, None)
    if STAGE == "B":
        res, full = gate_b(ctx)
        args.out = res["exports"][OPEN]
        args.preview = full.get("open_day")
        pb.report(args, [ctx["mesh_obj"]], {"stage": STAGE, "gate_b": res, "preview_full": full})
        return
    if STAGE in ("A2", "A2S"):
        res, full = gate_a2(ctx)
        args.out = ""
        args.preview = full.get("sheet")
        pb.report(args, [ctx["mesh_obj"]], {"stage": STAGE, "lid_open_pitch_deg": LID_OPEN_PITCH_DEG, "a2": res,
                                            "preview_full": {k: v for k, v in full.items() if k == "sheet"}})
        return
    res, full = gate_a(ctx)
    extra = {"stage": STAGE, "lid_open_pitch_deg": LID_OPEN_PITCH_DEG, "poses": res,
             "preview_full": full}
    args.out = ""
    args.preview = full.get("sheet") or full.get("explore_sheet")
    pb.report(args, [ctx["mesh_obj"]], extra)


if __name__ == "__main__":
    main()

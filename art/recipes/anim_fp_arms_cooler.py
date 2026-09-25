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
STAGE = os.environ.get("T064A_STAGE", "A")


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
    # Show: mouth turned 65 deg away (46 deg past the carry): the front wall (latch, sticker) faces up at the
    # bottom of the view, the mouth faces whoever stands in front. Shoulders 3 cm forward / 5 cm out, elbows out and
    # down so the forearms pass outside the box's side walls.
    "A_FPArms_CarryCooler_Show": {"handles": Vec((0.48, 0.0, -0.22)), "tilt": 65.0, "roll": None, "sign": None,
                                  "pole": Vec((-0.3, -1.0, -0.5)), "shoulder": Vec((0.03, -0.05, -0.02))},
}
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
    return fa.mat4(fa.rot3((0, 1, 0), cfg["tilt"] + a_tilt * math.sin(w * t - 0.9)), p)


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
    for c, roll, sign, P in cands[:12]:
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


def coverage(groups):
    """Centre box: share covered by each group (first hit); whole frame: share of each group."""
    RAY_TARGETS.clear()
    for names in groups.values():
        RAY_TARGETS.update(names)
    c = CENTRE_BOX
    cen = first_hits(64, 64, -c, c, -c, c)
    full = first_hits(160, 90)
    res = {"centre_box": {}, "frame": {}}
    for key, hits in (("centre_box", cen), ("frame", full)):
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


def main():
    args = pb.parse_args(ASSET, CATEGORY)
    ctx = fa.setup()
    pb.ensure_fbx_exporter()           # the cooler / fish stagings import FBX
    for n in ("_roll", "_sign"):
        for cfg in POSES.values():
            cfg.pop(n, None)
    res, full = gate_a(ctx)
    extra = {"stage": STAGE, "lid_open_pitch_deg": LID_OPEN_PITCH_DEG, "poses": res,
             "preview_full": full}
    args.out = ""
    args.preview = full.get("sheet") or full.get("explore_sheet")
    pb.report(args, [ctx["mesh_obj"]], extra)


if __name__ == "__main__":
    main()

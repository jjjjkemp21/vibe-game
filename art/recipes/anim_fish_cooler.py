"""anim_fish_cooler: layout proof for A_Fish_Curled in the starter cooler (T-030) + the 4 display slots.

Preview/validation only: nothing here is exported (A_Fish_Curled itself is exported by anim_fish.py). The fish
rig and the pose come from art/lib/fishrig.py (curled()), the fish meshes from the model-artist's recipes (via
anim_fish.Fish), the cooler from art/recipes/sm_cooler_starter.py (build(), liner constants), the scene pieces from
art/recipes/preview_cooler_starter.py (dock, EEVEE renders). None of them is edited here.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fish_cooler.py
Result: Saved/AgentLogs/blender/anim_fish_cooler.result.json ("slots_ue" = the slot table in SK_Fish.anim.md).
Previews (Saved/AgentLogs/previews/):
  A_Fish_Curled_cooler.png        contact sheet of the frames below
  A_Fish_Curled_cooler_fp_shop.png     THE design view: the cooler put down on the floor, front (latch) toward the
                                  player, who stands (eye 1.65 m) 0.6 m from its center; game FP camera (90 deg,
                                  1920x1080) looking down into it
  A_Fish_Curled_cooler_fp_countertop.png  the open cooler on a 0.9 m counter 0.55 m ahead (weak tie-breaker only)
  A_Fish_Curled_cooler_fills.png  shop view, 1-4 fish in both species orders (FILL_ORDERS), crops at game pixels
  A_Fish_Curled_cooler_top.png    open cooler straight down (orthographic): 4 curled fish in slots 0-3
  A_Fish_Curled_cooler_big.png    the same with the 1.3x fish NOT clamped (what the display cap prevents)
  A_Fish_Curled_cooler_slots.png  slot diagram: top view, fish tinted by slot, slot numbers, Unreal +X/+Y arrows
The shown fish: slot 0 Bonefish 1.0, slot 1 CoralSnapper 1.0, slot 2 CoralSnapper 1.0, slot 3 Bonefish 1.3 (shown
at the display cap, see below).

HOW THE SLOTS ARE MADE (deterministic)
- Each fish lies on its side (Unreal roll +90 = its right side down, -90 = its left side down), yawed, curled.
- A slot must take EITHER species, so every test uses the union ("envelope") of both species at the same transform.
- Fish are dropped in slot order onto a height field of the cooler floor (5 mm cells): slot i rests on the floor and
  on slots < i. A deterministic annealing search (fixed seed) picks each slot's side, yaw and floor position so that
  the stack is as low as possible, every vertex stays inside the liner (with MARGIN_M) and under the closed lid.
- The fit limit: the largest scale in LIMIT_PROBE at which 4 fish fit at all (stack under the lid, inside the liner,
  visibility ignored): 1.0 with the tail-half curl (2026-09-23; 1.05 misses the lid margin by 0.4 mm). The display
  cap S_CAP = DISPLAY_CAP = 1.0; at it every fish shows at least VIS_FEASIBLE of its footprint from above, and from
  the shop view every eye and tail tip is visible (view_check, all mixes x 1-4 fish). Slots are designed with every fish at S_CAP; a smaller fish sits in the
  same place on the same bed (it drops onto the bed below it).
- The pile lies along the BACK wall (T-030g follow-up, 2026-09-24): the front wall hid a lone fish lying at the
  front from a standing player farther than about 1 m. The search runs in the front frame (as before), its layout is
  turned 180 deg about the Contents Z axis (PILE_TURN_DEG; the liner is symmetric, so the fit holds) and then
  polished (polish(): a short seeded annealing of small yaw / X / Y moves scored by the EXACT checks below), because
  the plain turn showed every eye and tail in only 48 of 64 shop fills. The polish also keeps the game's sight-line
  rule (CPP_*, the C++ test) and the lone fish at the back (X < 0). Far views from 1.5 / 2.0 m (FAR_CAMS, fills 1-2)
  are reported and rendered.
- The result compares slots_ue with the Starter/Large rows of data/tables/DT_CoolerDisplay.json ("matches_dt");
  FISH_COOLER_WRITE_DT=1 writes them (Slots only) before the compare.
- Exact checks (BVH triangle overlap, every vertex vs the liner and the lid) for all 16 species mixes at scales
  S_CAP, 1.0 and 0.7, plus 32 seeded random mixes where every fish has its own scale in 0.7 .. S_CAP, all with the
  game's rule for the height (below); every vertex at least POLISH_LINER_MM inside the analytic liner and no triangle
  through the real (28-point, faceted) liner wall mesh.

GAME RULE (for the engineer; also in SK_Fish.anim.md)
  shown scale      s = min((Weight / ReferenceWeight)^(1/3), S_CAP)
  fish location    = Contents socket + (X, Y, BedZ + LieOffsetCm(species) x s)     (Unreal cm, cooler space)
  fish rotation    = FRotator(Pitch 0, Yaw, Roll) of the slot
  pose             = A_Fish_Curled at alpha 1 (additive on A_Fish_Rest, like the other clips)
  fill order       = slot 0, 1, 2, 3; after taking a fish out, re-seat the rest into slots 0..n-1.
"""
import importlib.util
import json
import math
import os
import random
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "lib"))

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402

import fishkit as fk  # noqa: E402
import fp_preview as fpp  # noqa: E402
import fishrig as fr  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

T0 = time.time()
PREVIEW = pb.PREVIEW_ROOT / "A_Fish_Curled_cooler.png"
CELL = 0.005                    # height-field cell (m)
BASE_CELL = 0.0025              # base raster of each lying fish (m), resampled per yaw and scale
MARGIN_M = 0.004                # every vertex at least this far inside the liner wall
LID_MARGIN_M = 0.004            # ... and under the closed lid's underside
GAP_M = 0.003                   # air gap left when a fish is dropped onto another (the height field is sampled)
VIS_MIN, VIS_WEIGHT = 0.30, 0.30  # each fish should show >= 30 % of its footprint from above (4 fish read as 4)
VIEW_WEIGHT = 0.05              # per eye or tail hidden from a camera, x the camera's weight below
RULE_WEIGHT = 0.05              # per degree off the rotation rule (designer must-fix 3)
# Rotation rule (designer must-fix 3, "no two fish parallel with their crests over each other"): slots alternate
# sides (mirrored); two slots on the SAME side (0 and 2, 1 and 3) lie at least ROT_APART_MIN apart (nose-to-tail
# chord lines); and the crests (dorsal fin footprints seen from above) of different fish overlap as little as
# possible (CREST_WEIGHT, a soft term only). A fixed 20-40 deg yaw step between neighbours was tried first:
# with the eye/tail rule it found no layout under the lid, and it didn't stop crests meeting (they sit inside each
# C, so what matters is where the Cs open, not the angle between them). No seed got the crest overlap under ~300
# cells (5 mm); lead decision 2026-09-23: fin-over-fin overlap is accepted, because the straight snapper crest
# (fishrig CURLED_BODY_DEG, curl in the tail half) is what removed the jumble. SEED was picked by how the shop view
# reads (all 4 eyes clear) over the alternative layout without the crest term, whose snapper crests met mid-cooler.
ROT_APART_MIN = 20.0
CREST_WEIGHT = 0.0005           # per 5 mm cell where two fish's crests (dorsal fins) lie over each other
# The player's eye in cooler space (+X = the cooler's front, the latch side, facing the player) and its weight.
# "shop": the cooler put down on the floor at the counter, the player standing (eye 1.65 m) 0.6 m from its center:
# every eye and tail must be visible (designer must-fix 2). "countertop": the cooler lifted onto a 0.9 m counter,
# 0.55 m ahead: the 28 cm deep liner's front wall hides everything low in the front half from this angle; 4 flat
# fish can't all show their eyes and tails there (see SK_Fish.anim.md), so it only counts as a tie-breaker.
# The search (design()) runs in the FRONT frame (the layout it finds is then turned to the back wall, see PILE_TURN_DEG),
# so these two cameras are the search's; the shop view is checked again, exactly, after the turn.
CAMS = [("shop", (0.60, 0.0, 1.65), 1.0), ("countertop", (0.55, 0.0, 0.75), 0.2)]
# "far150" / "far200" (T-030g follow-up, report + renders only): the same standing eye 1.5 m / 2.0 m from the cooler's
# center, fills 1 and 2. Per fish: eye/tail hidden count and the share of its upper surface in view (view_share).
FAR_CAMS = [("far150", (1.50, 0.0, 1.65)), ("far200", (2.00, 0.0, 1.65))]
# The pile lies along the BACK wall (-X): the front wall hid a lone fish lying at the front from a standing player
# farther than about 1 m. The searched layout is turned 180 deg about the Contents Z axis (X, Y negated, yaw + 180;
# the liner is symmetric, so the fit holds), then polished (polish(), below) against the exact shop view.
PILE_TURN_DEG = 180.0
# The polish: a short deterministic annealing over small moves of the turned slots (yaw +-5..30 deg, X/Y +-1..5 cells),
# scored with the EXACT checks (real meshes, ray casts): every eye and tail hidden from the shop view (all 16 mixes, 4
# fish), the exact fit (the same mixes and scales as the final checks) with every vertex at least POLISH_LINER_MM inside
# the analytic liner (the 28-point liner mesh lies up to ~0.3 mm inside that curve: wall_check below measures the mesh)
# and POLISH_LID_MM under the closed lid, and the game's sight-line rule (CPP_*); a tiny tie-breaker keeps it close to
# the turned layout. A plain turn of the front-camera layout showed only 48 of 64 shop fills (slot 0's eye under the
# snapper in slot 2 or 3); moves that reach 64/64 closer than 4 mm to the wall poke ~1 mm through the liner mesh.
# Seeds 400 and 600 both reach 64/64 (400, 500, 600 tried; 500 stays at 56/64); 600 moves the turned layout least
# (10 steps; both restarts agree) and keeps slot 3 at the back wall.
POLISH_SEED, POLISH_RESTARTS, POLISH_ITERS = 600, 2, 1500
POLISH_LINER_MM, POLISH_LID_MM = -3.8, 4.0     # the polish's targets (3 per mm short of the liner one)
# Hard fit limits of the final slots (every checked mix and scale): every vertex at least FIT_LINER_MM inside the
# analytic liner and WALL_MIN_MM inside the real liner wall mesh (no triangle through it), LID_MARGIN_M under the lid.
# 64/64 shop fills at the back wall needs slot 0 about 1 mm closer to the back wall than the search's 4 mm margin.
FIT_LINER_MM, WALL_MIN_MM = -2.5, 2.0
# The game's own check (Source/VibeGame/Tests/Catch/CoolerLidFocusTest.cpp, Project.Catch.Display.EveryFishVisible),
# Contents space, cm: the top of the top fish of a pile of slot+1 (origin Z + LieOffsetCm x s) clears the sight line
# over the front rim (RimX, RimZ) by 1 cm from an eye CPP_EYE_Z up at up to CPP_VIEW_D, for the smallest shown fish.
CPP_RIM_X, CPP_RIM_Z, CPP_EYE_Z, CPP_VIEW_D, CPP_LIE_CM = 22.0, 30.0, 160.0, 150.0, 4.25
RAY_TOP_Z = 0.345               # above this nothing in or of the cooler can block a view ray
VIS_FEASIBLE = 0.12             # a layout where any fish shows less than this from straight above is rejected
LIMIT_PROBE = (1.1, 1.05, 1.0)   # the fit limit (visibility ignored); 1.15+ is far off (stack 34-36 cm > lid 33)
DISPLAY_CAP = 1.0               # the slots are designed for fish up to this scale (see the module docstring)
YAW_STEP = 5                    # deg
SEED = 20260923
RESTARTS, ITERS = 12, 16000     # annealing effort per design
if os.environ.get("FISH_COOLER_EFFORT"):   # experiments only ("restarts,iters"); the committed slots use the above
    RESTARTS, ITERS = (int(v) for v in os.environ["FISH_COOLER_EFFORT"].split(","))
SHOW = [("Bonefish", 1.0), ("CoralSnapper", 1.0), ("CoralSnapper", 1.0), ("Bonefish", 1.3)]
FILL_ORDERS = [("Bonefish", "CoralSnapper", "Bonefish", "CoralSnapper"),
               ("CoralSnapper", "Bonefish", "CoralSnapper", "Bonefish")]
DATA = HERE.parent.parent / "data" / "tables"
DT_COOLER = DATA / "DT_CoolerDisplay.json"
DT_ROWS = ("Starter", "Large")  # rows that use these slots (Large = placeholder on the starter mesh)
SLOT_TINT = ["#E8C46A", "#3FA34D", "#3ED1C4", "#C0392B"]
if os.environ.get("FISH_COOLER_SET"):      # experiments only: override constants, e.g. '{"SEED": 7}'; the committed
    import ast                             # slots always come from the values above
    globals().update(ast.literal_eval(os.environ["FISH_COOLER_SET"]))
    PREVIEW = pb.PREVIEW_ROOT / ("exp_" + os.environ.get("FISH_COOLER_TAG", "cooler") + ".png")


def log(msg):
    print("[anim_fish_cooler %6.1fs] %s" % (time.time() - T0, msg), flush=True)


def load(stem):
    spec = importlib.util.spec_from_file_location(stem, HERE / (stem + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


af = load("anim_fish")
pcs = load("preview_cooler_starter")
cooler = pcs.cooler


# ---------------------------------------------------------------------------------------------------------------
# Curled fish (baked static copies of the skinned, posed meshes; fish-local meters)
# ---------------------------------------------------------------------------------------------------------------
class Curled:
    def __init__(self, fish):
        self.species = fish.species
        af.pose_fish(fish, fr.curled().quats())
        dg = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(fish.mesh.evaluated_get(dg), preserve_all_data_layers=False,
                                             depsgraph=dg)
        me.name = "PV_Curled_" + fish.species
        self.mesh = me
        self.co = np.array([v.co[:] for v in me.vertices])
        self.polys = [tuple(p.vertices) for p in me.polygons]
        af.pose_fish(fish, fr.Pose().quats())
        fish.mesh.hide_render = True
        # lying on its side, lowest point below the origin (right side down / left side down)
        self.lie = {side: float(-(self.co @ lie_rot(side)[:3, :3].T)[:, 2].min()) for side in (1, -1)}
        # view targets (fish-local, curled): the eye on the side facing up (its center, 3 mm out) and the two tail
        # fin lobe tips; side +1 lies on its right side, so its left eye (+Y) is up
        eyes = {}
        for g in ("Eye_L", "Eye_R"):
            idx = fr.group_members(fish.mesh, fr.part(g))
            c = self.co[idx].mean(axis=0)
            eyes[1 if c[1] > 0.0 else -1] = c + np.array([0.0, math.copysign(0.003, c[1]), 0.0])
        cau = fr.group_members(fish.mesh, fr.part("Fin_Caudal"))
        rest = fish.rest_co
        tips = [min((i for i in cau if rest[i].z > 0.0), key=lambda i: rest[i].x),
                min((i for i in cau if rest[i].z < 0.0), key=lambda i: rest[i].x)]
        self.targets = {side: np.array([eyes[side], self.co[tips[0]], self.co[tips[1]]]) for side in (1, -1)}
        self.nose = self.co[fish.nose_vert]
        self.tail_mid = self.co[tips].mean(axis=0)
        dorsal = set(fr.group_members(fish.mesh, fr.part("Fin_Dorsal")))
        self.dorsal_polys = [q for q in self.polys if all(i in dorsal for i in q)]


def lie_rot(side):
    """Fish-local -> lying on a side, as a 4x4 numpy array. side +1: right side down (fish +Y, its left, points up;
    Unreal roll +90); side -1: left side down (Unreal roll -90)."""
    return np.array(Matrix.Rotation(side * math.pi / 2.0, 4, "X"))


def slot_matrix(side, yaw_deg, loc):
    return (Matrix.Translation(Vector(loc)) @ Matrix.Rotation(math.radians(yaw_deg), 4, "Z")
            @ Matrix.Rotation(side * math.pi / 2.0, 4, "X"))


def fit_2d(co):
    """The curled fish lying flat: smallest-area rectangle around its footprint (the flank plane) and its thickness."""
    best = None
    for a10 in range(0, 1800, 5):
        a = math.radians(a10 / 10.0)
        c, s = math.cos(a), math.sin(a)
        u = co[:, 0] * c + co[:, 2] * s
        v = -co[:, 0] * s + co[:, 2] * c
        L, W = np.ptp(u), np.ptp(v)
        if L < W:
            L, W = W, L
        if best is None or L * W < best[0]:
            best = (L * W, L, W)
    return {"length_cm": round(best[1] * 100.0, 1), "width_cm": round(best[2] * 100.0, 1),
            "thickness_cm": round(float(np.ptp(co[:, 1])) * 100.0, 2),
            "nose_to_tail_extent_x_cm": round(float(np.ptp(co[:, 0])) * 100.0, 1)}


# ---------------------------------------------------------------------------------------------------------------
# Height fields
# ---------------------------------------------------------------------------------------------------------------
def base_raster(cf, side):
    """Top and bottom surface of the fish lying on `side` at yaw 0, scale 1 (origin at the fish origin), sampled by
    vertical rays on a BASE_CELL grid. Returns (x0, y0, top, bottom) with NaN where the fish isn't."""
    R = lie_rot(side)[:3, :3]
    P = self_pts = cf.co @ R.T
    tree = BVHTree.FromPolygons([Vector(p) for p in self_pts], cf.polys)
    mn, mx = P.min(axis=0), P.max(axis=0)
    xs = np.arange(math.floor(mn[0] / BASE_CELL) - 1, math.ceil(mx[0] / BASE_CELL) + 2) * BASE_CELL
    ys = np.arange(math.floor(mn[1] / BASE_CELL) - 1, math.ceil(mx[1] / BASE_CELL) + 2) * BASE_CELL
    crest_tree = BVHTree.FromPolygons([Vector(p) for p in self_pts], cf.dorsal_polys)
    top = np.full((len(xs), len(ys)), np.nan)
    bot = np.full((len(xs), len(ys)), np.nan)
    crest = np.zeros((len(xs), len(ys)), dtype=bool)
    down, up = Vector((0.0, 0.0, -1.0)), Vector((0.0, 0.0, 1.0))
    zt, zb = mx[2] + 0.05, mn[2] - 0.05
    for i, x in enumerate(xs):
        for j, y in enumerate(ys):
            h = tree.ray_cast(Vector((x, y, zt)), down, 1.0)
            if h[0] is not None:
                top[i, j] = h[0].z
                h2 = tree.ray_cast(Vector((x, y, zb)), up, 1.0)
                bot[i, j] = h2[0].z if h2[0] is not None else h[0].z
                crest[i, j] = crest_tree.ray_cast(Vector((x, y, zt)), down, 1.0)[0] is not None
    return xs[0], ys[0], top, bot, crest


class Variant:
    """The envelope of both species lying on `side`, yawed, at scale s, on the CELL grid around the fish origin:
    occupied cells (di, dj), their bottom and top heights (relative to the origin), and every vertex (for the liner)."""

    def __init__(self, bases, curls, side, yaw_deg, s):
        a = math.radians(yaw_deg)
        c, sn = math.cos(a), math.sin(a)
        Rz = np.array([[c, -sn, 0.0], [sn, c, 0.0], [0.0, 0.0, 1.0]])
        R = Rz @ lie_rot(side)[:3, :3]
        self.pts = np.vstack([cf.co @ R.T for cf in curls]) * s
        mn, mx = self.pts.min(axis=0), self.pts.max(axis=0)
        ii = np.arange(math.floor(mn[0] / CELL) - 1, math.ceil(mx[0] / CELL) + 2)
        jj = np.arange(math.floor(mn[1] / CELL) - 1, math.ceil(mx[1] / CELL) + 2)
        I, J = np.meshgrid(ii, jj, indexing="ij")
        X, Y = I * CELL, J * CELL
        # back into the base raster frame (yaw 0, scale 1); sample the cell's 4 quarter points so thin fins count
        top = np.full(I.shape, -np.inf)
        bot = np.full(I.shape, np.inf)
        crest = np.zeros(I.shape, dtype=bool)
        for ox, oy in ((-0.25, -0.25), (0.25, -0.25), (-0.25, 0.25), (0.25, 0.25), (0.0, 0.0)):
            Xs, Ys = X + ox * CELL, Y + oy * CELL
            bx = (c * Xs + sn * Ys) / s
            by = (-sn * Xs + c * Ys) / s
            for x0, y0, t, b, cr in bases:
                ix = np.rint((bx - x0) / BASE_CELL).astype(int)
                iy = np.rint((by - y0) / BASE_CELL).astype(int)
                ok = (ix >= 0) & (ix < t.shape[0]) & (iy >= 0) & (iy < t.shape[1])
                tv = np.full(I.shape, np.nan)
                bv = np.full(I.shape, np.nan)
                tv[ok] = t[ix[ok], iy[ok]]
                bv[ok] = b[ix[ok], iy[ok]]
                top = np.where(np.isnan(tv), top, np.maximum(top, tv * s))
                bot = np.where(np.isnan(bv), bot, np.minimum(bot, bv * s))
                if ox == 0.0:
                    cv = np.zeros(I.shape, dtype=bool)
                    cv[ok] = cr[ix[ok], iy[ok]]
                    crest |= cv
        occ = np.isfinite(top)
        self.ci, self.cj = I[crest], J[crest]                                    # crest / dorsal fin footprint
        self.di, self.dj = I[occ], J[occ]
        self.top, self.bot = top[occ], bot[occ]
        self.top_max = float(self.top.max())
        self.side, self.yaw, self.s = side, yaw_deg, s
        self.targets = [(cf.targets[side] @ R.T) * s for cf in curls]          # per species: eye, tail tip a, b
        chord = sum(((cf.nose - cf.tail_mid) @ R.T for cf in curls))           # tail -> nose, both species
        self.chord_deg = math.degrees(math.atan2(chord[1], chord[0]))


def cpp_min_scale():
    """The smallest shown scale the C++ test checks: a quarter of a species' lightest weight, weight-scaled like the
    fight fish ((W / ReferenceWeight)^(1/3), clamped to DT_FishVisual Default MinScale..MaxScale)."""
    species = json.loads((DATA / "DT_FishSpecies.json").read_text(encoding="utf-8"))
    vis = next(r for r in json.loads((DATA / "DT_FishVisual.json").read_text(encoding="utf-8")) if r["Name"] == "Default")
    return min(max(vis["MinScale"], (0.25 * r["WeightMin"] / r["ReferenceWeight"]) ** (1.0 / 3.0)) for r in species)


S_MIN = cpp_min_scale()


def cpp_sight_deficit_cm(x_cm, bed_cm):
    """How far (cm) the smallest fish in a slot at Unreal X x_cm on bed bed_cm lies under the C++ test's line of sight
    (+ its 1 cm) over the front rim from CPP_VIEW_D; <= 0 = seen. The line is highest at the farthest distance."""
    line = CPP_RIM_Z - (CPP_RIM_X - x_cm) * (CPP_EYE_Z - CPP_RIM_Z) / (CPP_VIEW_D - CPP_RIM_X)
    return line + 1.0 - (bed_cm + 2.0 * CPP_LIE_CM * S_MIN)


def liner_excess(P):
    """Largest distance (m, approx.) of points P (cooler space) outside the liner wall shrunk by MARGIN_M."""
    hx, hy = pcs._liner_half_np(P[:, 2])
    hx, hy = hx - MARGIN_M, hy - MARGIN_M
    e = cooler.EXP
    m = ((np.abs(P[:, 0]) / hx) ** e + (np.abs(P[:, 1]) / hy) ** e) ** (1.0 / e)
    return float(((m.max() - 1.0) * hx.min()))


class Layout:
    """The cooler floor as a height field; drop fish in order."""
    NI, NJ = 110, 120                # half extents in cells: x +-0.55 m, y +-0.60 m (room for any overhang)

    def __init__(self, floor_z, lid_z):
        self.floor_z, self.lid_z = floor_z, lid_z
        self.vis_weight, self.view_weight, self.rule_weight = VIS_WEIGHT, VIEW_WEIGHT, RULE_WEIGHT

    def drop(self, placements, variants, want_owner=False):
        """placements: [(variant key, pi, pj)]. Returns (cost, rows [origin z, top z, liner excess, share seen from
        above, targets hidden per camera]). Cost (m): the stack's top + 200 x any liner or lid violation
        + vis_weight x each slot's shortfall below VIS_MIN of its footprint seen from above + view_weight x every eye
        or tail hidden from a camera (x its CAMS weight) + rule_weight x every degree a slot pair breaks the rotation rule."""
        H = np.full((2 * self.NI + 1, 2 * self.NJ + 1), self.floor_z)
        owner = np.full(H.shape, -1)
        tops = []
        rows, cost, cells = [], 0.0, []
        for k, (key, pi, pj) in enumerate(placements):
            v = variants[key]
            gi, gj = v.di + pi + self.NI, v.dj + pj + self.NJ
            under = H[gi, gj]
            gap = np.where(under > self.floor_z + 1e-9, GAP_M, 0.0)     # an air gap over fish, none over the floor
            z = float((under + gap - v.bot).max())
            new_top = z + v.top
            higher = new_top > under
            H[gi[higher], gj[higher]] = new_top[higher]
            owner[gi[higher], gj[higher]] = k
            if self.view_weight > 0.0:
                T = np.full(H.shape, -np.inf)
                T[gi, gj] = new_top
                tops.append(T)
            P = v.pts + np.array([pi * CELL, pj * CELL, z])
            ex = liner_excess(P)
            top = z + v.top_max
            rows.append([z, top, ex])
            cells.append((gi, gj))
            cost += 200.0 * max(0.0, ex) + 200.0 * max(0.0, top - (self.lid_z - LID_MARGIN_M))
        for k, (gi, gj) in enumerate(cells):
            vis = float((owner[gi, gj] == k).mean())
            rows[k].append(vis)
            cost += self.vis_weight * max(0.0, VIS_MIN - vis)
        if self.view_weight > 0.0:
            for k, (key, pi, pj) in enumerate(placements):
                others = [tops[j] for j in range(len(tops)) if j != k]
                Hk = np.maximum.reduce(others) if others else np.full(H.shape, -np.inf)
                off = np.array([pi * CELL, pj * CELL, rows[k][0]])
                hidden = {}
                for name, cam, w in CAMS:
                    n = 0
                    for tg in variants[key].targets:
                        seen = self.seen(tg + off, Hk, cam)
                        n += int(not seen[0]) + int(not (seen[1] or seen[2]))
                    hidden[name] = n
                    cost += self.view_weight * w * n
                rows[k].append(hidden)
        if self.rule_weight > 0.0:
            n = len(placements)
            for a_ in range(n):                                           # same side (0/2, 1/3): not parallel
                for b_ in range(a_ + 2, n, 2):
                    d = abs((variants[placements[a_][0]].chord_deg - variants[placements[b_][0]].chord_deg + 90.0)
                            % 180.0 - 90.0)                               # angle between the chord LINES, 0..90
                    cost += self.rule_weight * max(0.0, ROT_APART_MIN - d)
            count = np.zeros(H.shape, dtype=np.int8)
            for key, pi, pj in placements:                                # crests (dorsal fins) over each other
                v = variants[key]
                count[v.ci + pi + self.NI, v.cj + pj + self.NJ] += 1
            crest_overlap = int((count > 1).sum())
            for r in rows:
                r.append(crest_overlap)
            cost += CREST_WEIGHT * crest_overlap
        stack = max(r[1] for r in rows)
        if want_owner:
            return cost + stack, rows, owner
        return cost + stack, rows

    def seen(self, P, Hk, cam):
        """Per target point (cooler space): is the straight line to the camera clear of the other fish (their height
        field Hk) and of the cooler wall?"""
        C = np.array(cam)
        t_end = np.clip((RAY_TOP_Z - P[:, 2]) / (C[2] - P[:, 2]), 0.0, 1.0)
        t = np.linspace(0.0, 1.0, 72)[1:][None, :] * t_end[:, None]
        Q = P[:, None, :] + t[:, :, None] * (C - P)[:, None, :]
        ii = np.clip(np.rint(Q[..., 0] / CELL).astype(int) + self.NI, 0, 2 * self.NI)
        jj = np.clip(np.rint(Q[..., 1] / CELL).astype(int) + self.NJ, 0, 2 * self.NJ)
        blocked = (Hk[ii, jj] > Q[..., 2] + 0.001).any(axis=1)
        hx, hy = pcs._liner_half_np(Q[..., 2])
        e = cooler.EXP
        m = (np.abs(Q[..., 0]) / hx) ** e + (np.abs(Q[..., 1]) / hy) ** e
        wall = ((m > 1.0) & (Q[..., 2] < cooler.Z_TOP)).any(axis=1)
        return ~(blocked | wall)


def alternate(state):
    """Designer rule: alternate slots lie on alternate sides (slot 0's side, then mirrored, ...)."""
    s0 = state[0][0][0]
    return [((s0 * (1 if k % 2 == 0 else -1), yaw), pi, pj) for k, ((_s, yaw), pi, pj) in enumerate(state)]


def anneal(layout, variants, keys, n_slots, rng, iters):
    """Annealing over [(key, pi, pj)] x n_slots. keys: list of (side, yaw). Deterministic for a given rng."""
    state = []
    for k in range(n_slots):
        side, yaw = keys[rng.randrange(len(keys))]
        state.append(((side, yaw), rng.randint(-8, 8), rng.randint(-10, 10)))
    state = alternate(state)
    cost, _ = layout.drop(state, variants)
    best = (cost, list(state))
    T = 0.02
    for it in range(iters):
        t = T * (1.0 - it / iters) + 1e-5
        new = list(state)
        k = rng.randrange(n_slots)
        (side, yaw), pi, pj = new[k]
        r = rng.random()
        if r < 0.3:
            yaw = (yaw + rng.choice((-2, -1, 1, 2)) * YAW_STEP) % 360
        elif r < 0.4:
            yaw = (yaw + 180) % 360
        elif r < 0.5:
            new = [((-sd, yw), a_, b_) for (sd, yw), a_, b_ in new]          # mirror every slot (keeps alternation)
            (side, yaw), pi, pj = new[k]
        elif r < 0.9:
            pi += rng.randint(-3, 3)
            pj += rng.randint(-3, 3)
        else:
            k2 = rng.randrange(n_slots)
            new[k], new[k2] = new[k2], new[k]
            (side, yaw), pi, pj = new[k]
        new[k] = ((side, yaw), max(-20, min(20, pi)), max(-28, min(28, pj)))
        new = alternate(new)
        c, _ = layout.drop(new, variants)
        if c < cost or rng.random() < math.exp(-(c - cost) / t):
            state, cost = new, c
            if c < best[0]:
                best = (c, list(new))
    return best


def design(layout, bases, curls, s, rng_seed):
    keys = [(side, yaw) for side in (1, -1) for yaw in range(0, 360, YAW_STEP)]
    variants = {k: Variant(bases[k[0]], curls, k[0], k[1], s) for k in keys}
    best = None
    for restart in range(RESTARTS):
        rng = random.Random(rng_seed + restart)
        c, st = anneal(layout, variants, keys, 4, rng, ITERS)
        if best is None or c < best[0]:
            best = (c, st)
    cost, state = best
    _c, rows, owner = layout.drop(state, variants, want_owner=True)
    labels = []
    for k in range(len(state)):
        ii, jj = np.nonzero(owner == k)
        labels.append((float((ii - layout.NI).mean() * CELL), float((jj - layout.NJ).mean() * CELL))
                      if len(ii) else (0.0, 0.0))
    fits = all(r[2] <= 0.0 for r in rows) and max(r[1] for r in rows) <= layout.lid_z - LID_MARGIN_M
    crest = rows[0][5] if len(rows[0]) > 5 else None
    reads = min(r[3] for r in rows) >= VIS_FEASIBLE and (layout.view_weight == 0.0
                                                         or sum(r[4][CAMS[0][0]] for r in rows) == 0)
    angles = [variants[st[0]].chord_deg for st in state]
    return {"scale": s, "cost": cost, "state": state, "rows": rows, "fits": fits, "reads": reads, "variants": variants,
            "stack_top_m": max(r[1] for r in rows), "visible": [round(r[3], 2) for r in rows], "labels": labels,
            "hidden_targets": [r[4] if len(r) > 4 else None for r in rows],
            "chord_deg": [round(a_, 1) for a_ in angles], "crest_overlap_cells": crest}


# ---------------------------------------------------------------------------------------------------------------
# Slots, exact checks, Unreal conversion
# ---------------------------------------------------------------------------------------------------------------
def slots_from(des, curls):
    lie_max = max(cf.lie[side] for cf in curls for side in (1, -1))
    out = []
    for ((side, yaw), pi, pj), (z, top, ex, vis, *_rest) in zip(des["state"], des["rows"]):
        bed = z - lie_max * des["scale"]           # the bed under this slot (lowest point of the envelope)
        if bed < cooler.FLOOR_Z + GAP_M:            # resting on the floor (the raster bottom is sampled): exact
            bed = cooler.FLOOR_Z
        out.append({"side": side, "yaw": yaw, "x": pi * CELL, "y": pj * CELL, "bed_z": bed, "origin_z_cap": z,
                    "visible_from_above": round(vis, 2)})
    for sl, lab in zip(out, des["labels"]):
        sl["label_xy"] = lab
    return out


def _num(v):
    return ("%.2f" % v).rstrip("0").rstrip(".") if abs(v - round(v)) > 1e-9 else "%.1f" % v


def dt_slot_line(t):
    """One slot of DT_CoolerDisplay.json, in the file's one-line style (Location Z = BedZ)."""
    return ('{ "Location": { "X": %s, "Y": %s, "Z": %s }, "Rotation": { "Pitch": %s, "Yaw": %s, "Roll": %s } }'
            % tuple(_num(v) for v in (t["X"], t["Y"], t["BedZ"], t["Pitch"], t["Yaw"], t["Roll"])))


def write_dt(table):
    """Replace the Slots of the DT_ROWS rows in DT_CoolerDisplay.json with slots_ue (text edit: the rest of the file,
    its formatting and the other fields stay as they are)."""
    text = DT_COOLER.read_text(encoding="utf-8")
    body = ",\n".join("\t\t\t" + dt_slot_line(t) for t in table)
    for name in DT_ROWS:
        at = text.index('"Name": "%s"' % name)
        a_ = text.index('"Slots": [', at) + len('"Slots": [')
        b_ = text.index("\t\t],", a_)
        text = text[:a_] + "\n" + body + "\n" + text[b_:]
    json.loads(text)                                                   # still valid JSON
    DT_COOLER.write_text(text, encoding="utf-8", newline="")
    log("wrote the slots of %s to %s" % (", ".join(DT_ROWS), DT_COOLER))


def compare_dt(table):
    """slots_ue vs the DT_ROWS rows of DT_CoolerDisplay.json: the largest difference (cm / deg); ok = equal to the
    rounding slots_ue writes."""
    rows = json.loads(DT_COOLER.read_text(encoding="utf-8"))

    def ang(a, b):
        return abs((a - b + 180.0) % 360.0 - 180.0)
    out = {"ok": True}
    for name in DT_ROWS:
        dt = next(r for r in rows if r["Name"] == name)["Slots"]
        if len(dt) != len(table):
            out.update({"ok": False, name: "slot count %d vs %d" % (len(dt), len(table))})
            continue
        dl = max(max(abs(d["Location"]["X"] - t["X"]), abs(d["Location"]["Y"] - t["Y"]),
                     abs(d["Location"]["Z"] - t["BedZ"])) for d, t in zip(dt, table))
        da = max(max(ang(d["Rotation"]["Pitch"], t["Pitch"]), ang(d["Rotation"]["Yaw"], t["Yaw"]),
                     ang(d["Rotation"]["Roll"], t["Roll"])) for d, t in zip(dt, table))
        out[name] = {"max_loc_diff_cm": round(dl, 4), "max_rot_diff_deg": round(da, 4)}
        out["ok"] = out["ok"] and dl <= 0.001 and da <= 0.001
    return out


def fish_matrix(slot, cf, s):
    z = slot["bed_z"] + cf.lie[slot["side"]] * s
    return slot_matrix(slot["side"], slot["yaw"], (slot["x"], slot["y"], z)) @ Matrix.Diagonal((s, s, s, 1.0))


def exact_check(slots, curls_by, species, scales, lid_z):
    """One mix: species[i] at scales[i] in slot i. Triangle-pair overlaps between fish, liner excess, lid gap."""
    trees, pts_all = [], []
    for slot, sp, s in zip(slots, species, scales):
        cf = curls_by[sp]
        M = np.array(fish_matrix(slot, cf, s))
        P = cf.co @ M[:3, :3].T + M[:3, 3]
        pts_all.append(P)
        trees.append(BVHTree.FromPolygons([Vector(p) for p in P], cf.polys))
    overlaps = sum(len(trees[a].overlap(trees[b])) for a in range(len(trees)) for b in range(a))
    ex = max(liner_excess(P) for P in pts_all) - MARGIN_M     # distance outside the REAL wall (< 0 = inside)
    top = max(float(P[:, 2].max()) for P in pts_all)
    low = min(float(P[:, 2].min()) for P in pts_all)
    return {"tri_overlaps": overlaps, "liner_excess_mm": round(ex * 1000.0, 1),
            "lid_gap_mm": round((lid_z - top) * 1000.0, 1), "below_floor_mm": round((cooler.FLOOR_Z - low) * 1000.0, 2)}


def view_check(slots, curls_by, species, scales, body_tree, cam):
    """One fill: species[i] at scales[i] in slot i. For each fish: 0 if its eye and at least one tail fin tip are seen
    from `cam` (cooler space), else the number of those hidden (eye, tail). Ray casts against the cooler body and every
    fish; a hit on the fish itself within 15 mm of the target counts as seeing it."""
    trees, tgts = [], []
    for slot, sp, s in zip(slots, species, scales):
        cf = curls_by[sp]
        M = np.array(fish_matrix(slot, cf, s))
        P = cf.co @ M[:3, :3].T + M[:3, 3]
        trees.append(BVHTree.FromPolygons([Vector(p) for p in P], cf.polys))
        tgts.append(cf.targets[slot["side"]] @ M[:3, :3].T + M[:3, 3])
    C = Vector(cam)
    out = []
    for k, T in enumerate(tgts):
        seen = []
        for t in T:
            t = Vector(t)
            d = t - C
            dist = d.length
            d.normalize()
            ok = True
            for j, tree in [(-1, body_tree)] + list(enumerate(trees)):
                loc, _n, _i, hd = tree.ray_cast(C, d, dist - 0.002)
                if loc is None:
                    continue
                if j == k and (loc - t).length < 0.015:
                    continue
                ok = False
                break
            seen.append(ok)
        out.append(int(not seen[0]) + int(not (seen[1] or seen[2])))
    return out


def view_share(slots, curls_by, species, scales, body_tree, cam):
    """One fill: per fish, the share of its upward-facing vertices (normal z > 0.3) that `cam` sees (ray to the
    camera clear of the cooler body and every fish, its own body included)."""
    trees, ups = [], []
    for slot, sp, s in zip(slots, species, scales):
        cf = curls_by[sp]
        M = np.array(fish_matrix(slot, cf, s))
        P = cf.co @ M[:3, :3].T + M[:3, 3]
        trees.append(BVHTree.FromPolygons([Vector(p) for p in P], cf.polys))
        nz = np.array([v.normal[:] for v in cf.mesh.vertices]) @ M[:3, :3].T
        nz = nz[:, 2] / np.maximum(np.linalg.norm(nz, axis=1), 1e-9)
        ups.append(P[nz > 0.3])
    C = Vector(cam)
    out = []
    for k, U in enumerate(ups):
        n = 0
        for t in U:
            t = Vector(t)
            d = t - C
            dist = d.length
            d.normalize()
            ok = True
            for j, tree in [(-1, body_tree)] + list(enumerate(trees)):
                loc, _n, _i, _hd = tree.ray_cast(C, d, dist + 0.01)
                if loc is not None and not (j == k and (loc - t).length < 0.004):
                    if (loc - C).length < dist - 0.004:
                        ok = False
                        break
            n += int(ok)
        out.append(n / max(1, len(U)))
    return out


def exact_fit(slots, curls_by, names, s_cap, lid_z):
    """The exact fit checks: all 16 species mixes at S_CAP, 1.0 and 0.7, and 32 seeded random mixes where every fish
    has its own scale in 0.7 .. S_CAP. Returns (worst over all, the checks)."""
    checks = []
    for m in range(16):
        mix = [names[(m >> i) & 1] for i in range(4)]
        for s in sorted({s_cap, 1.0, 0.7}):
            r = exact_check(slots, curls_by, mix, [s] * 4, lid_z)
            r.update({"mix": "".join(sp[0] for sp in mix), "scale": s})
            checks.append(r)
    rng = random.Random(SEED)                   # mixed sizes: every fish its own scale in 0.7 .. S_CAP
    for _ in range(32):
        mix = [rng.choice(names) for _i in range(4)]
        sc = [round(rng.uniform(0.7, s_cap), 3) for _i in range(4)]
        r = exact_check(slots, curls_by, mix, sc, lid_z)
        r.update({"mix": "".join(sp[0] for sp in mix), "scale": sc})
        checks.append(r)
    worst = {"tri_overlaps": max(c["tri_overlaps"] for c in checks),
             "liner_excess_mm": max(c["liner_excess_mm"] for c in checks),
             "lid_gap_mm_min": min(c["lid_gap_mm"] for c in checks),
             "below_floor_mm": max(c["below_floor_mm"] for c in checks)}
    return worst, checks


def wall_polys(parts):
    """The liner WALL faces of the real cooler body mesh (cooler space): steep faces inside the rim, above the floor."""
    me = parts["body"].data
    V = np.array([v.co[:] for v in me.vertices])
    out = []
    for q in me.polygons:
        P = V[list(q.vertices)]
        n = np.array(q.normal[:])
        c = P.mean(axis=0)
        hx, hy = pcs._liner_half_np(np.array([c[2]]))
        if (abs(n[2]) < 0.8 and abs(c[0]) < hx[0] + 0.004 and abs(c[1]) < hy[0] + 0.004
                and cooler.FLOOR_Z + 0.001 < c[2] < cooler.Z_TOP + 0.001):
            out.append(tuple(q.vertices))
    return BVHTree.FromPolygons([Vector(v) for v in V], out)


def wall_check(slots, curls_by, names, s, wall_tree):
    """Against the real (faceted) liner wall mesh, every species mix at scale s: triangle overlaps and the least signed
    distance (mm) of a fish vertex inside the wall (< 0 = through it)."""
    least, overlaps = 1e9, 0
    for m in range(16):
        mix = [names[(m >> i) & 1] for i in range(4)]
        for slot, sp in zip(slots, mix):
            cf = curls_by[sp]
            M = np.array(fish_matrix(slot, cf, s))
            P = cf.co @ M[:3, :3].T + M[:3, 3]
            overlaps += len(BVHTree.FromPolygons([Vector(p) for p in P], cf.polys).overlap(wall_tree))
            for p in P:
                loc, n, _i, d = wall_tree.find_nearest(Vector(p), 0.02)
                if loc is None:
                    continue
                n = np.array(n[:])
                if n @ np.array([-loc.x, -loc.y, 0.0]) < 0.0:          # the normal pointing into the cooler
                    n = -n
                least = min(least, float((np.array(p) - np.array(loc[:])) @ n))
    return {"wall_overlaps": overlaps, "wall_min_mm": round(least * 1000.0, 2)}


def turn_state(state):
    """The searched slots turned PILE_TURN_DEG (180) about the Contents Z axis: cells negated, yaw + 180."""
    assert PILE_TURN_DEG == 180.0
    return [((side, (yaw + 180) % 360), -pi, -pj) for (side, yaw), pi, pj in state]


def state_slots(layout, variants, state, s, curls):
    """Slots (as slots_from) for a state [((side, yaw), pi, pj)] at scale s: the height-field drop, top-view share and
    labels, readability terms off."""
    saved = (layout.vis_weight, layout.view_weight, layout.rule_weight)
    layout.vis_weight, layout.view_weight, layout.rule_weight = 0.0, 0.0, 0.0
    _c, rows, owner = layout.drop(state, variants, want_owner=True)
    layout.vis_weight, layout.view_weight, layout.rule_weight = saved
    labels = []
    for k in range(len(state)):
        ii, jj = np.nonzero(owner == k)
        labels.append((float((ii - layout.NI).mean() * CELL), float((jj - layout.NJ).mean() * CELL))
                      if len(ii) else (0.0, 0.0))
    return slots_from({"state": state, "rows": rows, "labels": labels, "scale": s}, curls)


def rule_report(layout, variants, state):
    """The rotation rule (designer must-fix 3) for a state: each slot's nose-to-tail chord (deg, cooler space), the
    least angle between the chord lines of same-side slots (0/2, 1/3; >= ROT_APART_MIN) and the crest overlap cells."""
    chords = [variants[key].chord_deg for key, _i, _j in state]
    apart = min(abs((chords[a_] - chords[b_] + 90.0) % 180.0 - 90.0)
                for a_ in range(len(state)) for b_ in range(a_ + 2, len(state), 2))
    count = np.zeros((2 * layout.NI + 1, 2 * layout.NJ + 1), dtype=np.int8)
    for key, pi, pj in state:
        v = variants[key]
        count[v.ci + pi + layout.NI, v.cj + pj + layout.NJ] += 1
    return {"chord_deg": [round(c, 1) for c in chords], "same_side_apart_min_deg": round(apart, 1),
            "crest_overlap_cells": int((count > 1).sum())}


def polish(layout, variants, bases, state0, s, curls, curls_by, lid_z, body_tree):
    """Anneal the turned slots on the exact checks (see POLISH_*). Deterministic (POLISH_SEED). Returns the best
    state and a record of its score."""
    names = [c.species for c in curls]
    cam = CAMS[0][1]

    def score(state):
        for key, _i, _j in state:
            if key not in variants:
                variants[key] = Variant(bases[key[0]], curls, key[0], key[1], s)
        sl = state_slots(layout, variants, state, s, curls)
        hidden = sum(sum(view_check(sl, curls_by, [names[(m >> i) & 1] for i in range(4)], [s] * 4, body_tree, cam))
                     for m in range(16))                  # 4 fish = every occluder: fewer fish hide nothing more
        worst, _checks = exact_fit(sl, curls_by, names, s, lid_z)
        cpp = [cpp_sight_deficit_cm(t["x"] * 100.0, (t["bed_z"] - cooler.FLOOR_Z) * 100.0) for t in sl]
        dist = sum(abs((a[0][1] - b[0][1] + 180) % 360 - 180) / YAW_STEP + abs(a[1] - b[1]) + abs(a[2] - b[2])
                   for a, b in zip(state, state0))
        c = (hidden + 5.0 * worst["tri_overlaps"] + 3.0 * max(0.0, worst["liner_excess_mm"] - POLISH_LINER_MM)
             + max(0.0, POLISH_LID_MM - worst["lid_gap_mm_min"]) + max(0.0, worst["below_floor_mm"] - 0.5)
             + sum(max(0.0, d) for d in cpp) + (10.0 if sl[0]["x"] >= 0.0 else 0.0) + 0.002 * dist)
        return c, {"shop_hidden_4_fish": hidden, "exact_worst": worst, "cpp_sight_deficit_cm": [round(d, 2) for d in cpp],
                   "moved_steps": dist}

    c0, rec0 = score(state0)
    best = (c0, list(state0), rec0)
    log("polish start (the turned layout): cost %.3f %s" % (c0, rec0))
    for restart in range(POLISH_RESTARTS):
        rng = random.Random(POLISH_SEED + restart)
        state, cost = list(state0), c0
        for it in range(POLISH_ITERS):
            t = 1.5 * (1.0 - it / POLISH_ITERS) + 1e-3
            new = list(state)
            k = rng.randrange(len(new))
            (side, yaw), pi, pj = new[k]
            r = rng.random()
            if r < 0.3:
                yaw = (yaw + rng.choice((-10, -5, 5, 10))) % 360
            elif r < 0.4:
                yaw = (yaw + rng.choice((-30, -20, 20, 30))) % 360
            elif r < 0.85:
                pi += rng.randint(-2, 2)
                pj += rng.randint(-2, 2)
            else:
                pi += rng.randint(-5, 5)
                pj += rng.randint(-5, 5)
            new[k] = ((side, yaw), pi, pj)
            c, rec = score(new)
            if c < cost or rng.random() < math.exp(-(c - cost) / t):
                state, cost = new, c
                if c < best[0]:
                    best = (c, list(new), rec)
        log("polish restart %d: best cost %.3f %s" % (restart, best[0], best[2]))
    return best[1], dict(best[2], cost=round(best[0], 4), start=rec0)


def ue_rotator(M3):
    """Blender rotation (3x3, cooler space) -> Unreal FRotator (pitch, yaw, roll) deg, the way FMatrix::Rotator()
    reads it after the FBX axis change (Y negated). Verified by rebuilding FRotationMatrix."""
    S = np.diag([1.0, -1.0, 1.0])
    Mu = S @ np.array(M3) @ S
    X, Y, Z = Mu[:, 0], Mu[:, 1], Mu[:, 2]
    pitch = math.atan2(X[2], math.hypot(X[0], X[1]))
    yaw = math.atan2(X[1], X[0])
    sy_axis = np.array([-math.sin(yaw), math.cos(yaw), 0.0])
    roll = math.atan2(Z @ sy_axis, Y @ sy_axis)
    P, Yw, R = pitch, yaw, roll
    SP, CP, SY, CY, SR, CR = math.sin(P), math.cos(P), math.sin(Yw), math.cos(Yw), math.sin(R), math.cos(R)
    rows = np.array([[CP * CY, CP * SY, SP],
                     [SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP],
                     [-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP]])
    err = float(np.abs(rows - Mu.T).max())
    if err > 1e-6:
        raise RuntimeError("FRotator conversion mismatch %g" % err)
    return [round(math.degrees(a), 2) + 0.0 for a in (pitch, yaw, roll)]


def slots_ue(slots, s_cap):
    out = []
    for i, sl in enumerate(slots):
        M = slot_matrix(sl["side"], sl["yaw"], (0, 0, 0)).to_3x3()
        p, y, r = ue_rotator(M)
        out.append({"slot": i, "X": round(sl["x"] * 100.0, 1) + 0.0, "Y": round(-sl["y"] * 100.0, 1) + 0.0,
                    "BedZ": round((sl["bed_z"] - cooler.FLOOR_Z) * 100.0, 2) + 0.0,
                    "Pitch": p, "Yaw": y, "Roll": r,
                    "side_down": "right" if sl["side"] == 1 else "left",
                    "origin_z_at_cap_cm": round((sl["origin_z_cap"] - cooler.FLOOR_Z) * 100.0, 2)})
    return out


# ---------------------------------------------------------------------------------------------------------------
# Renders
# ---------------------------------------------------------------------------------------------------------------
def frame(name):
    return PREVIEW.with_name(PREVIEW.stem + "_" + name + PREVIEW.suffix)


def place_fish(slots, curls_by, mix, parent, s_cap, clamp=True, tint=False):
    objs = []
    for i, (slot, (sp, scale)) in enumerate(zip(slots, mix)):
        cf = curls_by[sp]
        s = min(scale, s_cap) if clamp else scale
        me = cf.mesh
        if tint:
            me = cf.mesh.copy()
            me.materials.clear()
            me.materials.append(af.flat_mat(SLOT_TINT[i]))
        o = bpy.data.objects.new("PV_Fish_%d" % i, me)
        bpy.context.scene.collection.objects.link(o)
        o.matrix_world = parent @ fish_matrix(slot, cf, s)
        objs.append(o)
    return objs


def add_text(text, loc, size, color):
    cu = bpy.data.curves.new("PV_Text", "FONT")
    cu.body = text
    cu.size = size
    cu.align_x, cu.align_y = "CENTER", "CENTER"
    cu.materials.append(af.flat_mat(color))
    o = bpy.data.objects.new("PV_Text", cu)
    bpy.context.scene.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = (0.0, 0.0, -math.pi / 2.0)       # reads left to right in the slot diagram
    return o


def render_all(slots, curls_by, s_cap):
    paths = []
    scene = bpy.context.scene
    scene.display.shading.show_specular_highlight = False
    pcs.build_dock()
    parts = cooler.build()
    fish = []

    def stage(M, mix, clamp=True):
        for o in fish:
            bpy.data.objects.remove(o, do_unlink=True)
        fish[:] = place_fish(slots[:len(mix)], curls_by, mix, M, s_cap, clamp=clamp)
        pcs.place_cooler(parts, M, open_lid=True)
        return M @ Vector((0.0, 0.0, 0.17))

    def cam_world(M, name):
        return tuple(M @ Vector(next(c[1] for c in CAMS + FAR_CAMS if c[0] == name)))

    # 1. the shop view: the cooler put down on the floor at the counter, front toward the player, who stands with the
    #    eye 1.65 m up and 0.6 m from its center, looking down into it (the game's FP camera, 90 deg, 1920x1080)
    M_shop = pcs.xform(0.60, 0.0, pcs.DECK_Z, 180.0)
    c = stage(M_shop, SHOW)
    paths.append(pcs.render_eevee(frame("fp_shop"), cam_world(M_shop, "shop"), tuple(c), hfov_deg=90.0,
                                  resolution=(1920, 1080)))
    # 2. the same cooler lifted onto a 0.9 m counter, 0.55 m ahead (the old "counter" view; see CAMS)
    counter_top = pcs.DECK_Z + 0.90
    counter = bpy.data.objects.new("PV_Counter", bpy.data.meshes.new("PV_Counter"))
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation((0.75, 0.0, counter_top - 0.45))
                          @ Matrix.Diagonal((0.70, 2.4, 0.90, 1.0)))
    bm.to_mesh(counter.data)
    bm.free()
    counter.data.materials.append(style.make_material("PV_CounterWood", "#6B4A33", "wood"))
    scene.collection.objects.link(counter)
    M_top = pcs.xform(0.55, 0.0, counter_top, 180.0)
    c = stage(M_top, SHOW)
    paths.append(pcs.render_eevee(frame("fp_countertop"), cam_world(M_top, "countertop"), tuple(c), hfov_deg=90.0,
                                  resolution=(1920, 1080)))
    bpy.data.objects.remove(counter, do_unlink=True)
    # 3. straight down (orthographic), then the same with the 1.3x fish NOT clamped
    M_far = pcs.xform(1.2, 0.0, pcs.DECK_Z, 180.0)
    stage(M_far, SHOW)
    paths.append(pcs.render_eevee(frame("top"), (1.2, 0.0, pcs.DECK_Z + 3.0), (1.2, 0.0, pcs.DECK_Z), ortho=0.75,
                                  resolution=(1280, 720)))
    stage(M_far, SHOW, clamp=False)
    paths.append(pcs.render_eevee(frame("big"), (1.2, 0.0, pcs.DECK_Z + 3.0), (1.2, 0.0, pcs.DECK_Z), ortho=0.75,
                                  resolution=(1280, 720)))
    # 4. the fills: 1, 2, 3 and 4 fish in both species orders, shop view; game pixels (1:1 crops around the cooler)
    fills = []
    for order in FILL_ORDERS:
        row = []
        for n in range(1, 5):
            mix = [(sp, 1.0) for sp in order[:n]]
            c = stage(M_shop, mix)
            name = "fill_%s_%d" % ("".join(sp[0] for sp in order), n)
            row.append((pcs.render_eevee(PREVIEW.parent / "anim_fish_cooler_cells" / (PREVIEW.stem + name + ".png"),
                                         cam_world(M_shop, "shop"), tuple(c), hfov_deg=90.0,
                                         resolution=(1920, 1080)), (960, 470)))
        fills.append(row)
    fill_sheet = fpp.zoom_sheet(PREVIEW.with_name(PREVIEW.stem.replace("_cooler", "") + "_cooler_fills.png"), fills,
                                crop=600, scale=1)
    # 5. the far views: 1 and 2 fish (both orders) from 1.5 m (top row) and 2.0 m (bottom row), the same standing eye
    #    and game camera; 360 px crops of game pixels, shown 2x (nearest) so the eyes can be judged
    far_rows = []
    for cam_name in ("far150", "far200"):
        row = []
        for order in FILL_ORDERS:
            for n in (1, 2):
                mix = [(sp, 1.0) for sp in order[:n]]
                c = stage(M_shop, mix)
                name = "_%s_%s_%d" % (cam_name, "".join(sp[0] for sp in order[:n]), n)
                row.append((pcs.render_eevee(PREVIEW.parent / "anim_fish_cooler_cells" / (PREVIEW.stem + name + ".png"),
                                             cam_world(M_shop, cam_name), tuple(c), hfov_deg=90.0,
                                             resolution=(1920, 1080)), (960, 540)))
        far_rows.append(row)
    far_sheet = fpp.zoom_sheet(PREVIEW.with_name(PREVIEW.stem.replace("_cooler", "") + "_cooler_far.png"), far_rows,
                               crop=360, scale=2)
    for o in fish:
        bpy.data.objects.remove(o, do_unlink=True)
    # 4. slot diagram (Workbench, cooler at the origin, screen up = Unreal +X = the cooler's front)
    for o in list(bpy.data.objects):
        if o.name.startswith("PV_Dock"):
            bpy.data.objects.remove(o, do_unlink=True)
    pcs.place_cooler(parts, Matrix.Identity(4), open_lid=True)
    parts["lid"].hide_render = True
    tinted = place_fish(slots, curls_by, [(sp, 1.0) for sp, _ in SHOW], Matrix.Identity(4), s_cap, tint=True)
    extra = []
    for i, sl in enumerate(slots):
        extra.append(add_text(str(i), (sl["label_xy"][0], sl["label_xy"][1], 0.45), 0.06, style.UI.INK))
    extra.append(af.add_polyline("PV_ArrowX", [(0, 0, 0.44), (0.12, 0, 0.44), (0.10, 0.015, 0.44), (0.12, 0, 0.44),
                                               (0.10, -0.015, 0.44)], "#C0392B", 0.003))
    extra.append(af.add_polyline("PV_ArrowY", [(0, 0, 0.44), (0, -0.12, 0.44), (0.015, -0.10, 0.44),
                                               (0, -0.12, 0.44), (-0.015, -0.10, 0.44)], "#2F4A5E", 0.003))
    extra.append(add_text("+X front", (0.15, 0.0, 0.44), 0.022, "#C0392B"))
    extra.append(add_text("+Y", (0.0, -0.145, 0.44), 0.022, "#2F4A5E"))
    rot = (0.0, 0.0, -math.pi / 2.0)                   # screen up = Unreal +X, screen right = Unreal +Y
    paths.append(af.render(frame("slots"), (0.0, 0.0, 3.0), rot, ortho=0.72, res=(960, 720),
                           bg=style.UI.PARCHMENT, label="Slots 0-3 (fill order), cap %.2f\nUnreal axes at Contents" % s_cap))
    for o in tinted + extra:
        bpy.data.objects.remove(o, do_unlink=True)
    pb.contact_sheet(paths, PREVIEW, cols=2, cell=(960, 540))
    return paths + [fill_sheet, far_sheet]


# ---------------------------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------------------------
def closed_lid_underside(parts):
    """Lowest point of the closed lid above the liner opening (cooler space)."""
    lid = parts["lid"]
    M = Matrix.Translation(cooler.HINGE)
    hx, hy = pcs._liner_half_np(np.array([cooler.Z_TOP]))
    zs = [(M @ v.co).z for v in lid.data.vertices
          if abs((M @ v.co).x) < hx[0] and abs((M @ v.co).y) < hy[0]]
    return min(zs)


def main():
    pb.reset_scene()
    bpy.context.scene.render.fps = fr.FPS
    fishes = [af.Fish(stem, asset, species) for stem, asset, species in af.SPECIES]
    curls = [Curled(f) for f in fishes]
    curls_by = {c.species: c for c in curls}
    fit = {c.species: dict(fit_2d(c.co), lie_cm={("right" if k == 1 else "left"): round(v * 100.0, 2)
                                                  for k, v in c.lie.items()}) for c in curls}
    log("fit %s" % fit)
    parts = cooler.build()
    lid_z = closed_lid_underside(parts)
    bme = parts["body"].data
    body_tree = BVHTree.FromPolygons([v.co.copy() for v in bme.vertices], [tuple(q.vertices) for q in bme.polygons])
    wall_tree = wall_polys(parts)
    for o in list(bpy.data.objects):
        if o.name.startswith(("SM_Cooler", "UCX_SM_Cooler")):
            bpy.data.objects.remove(o, do_unlink=True)
    log("liner floor z %.3f, closed lid underside z %.4f" % (cooler.FLOOR_Z, lid_z))
    bases = {side: [base_raster(c, side) for c in curls] for side in (1, -1)}
    log("base rasters done")
    layout = Layout(cooler.FLOOR_Z, lid_z)
    tried = []

    def run(s, readable):
        layout.vis_weight, layout.view_weight, layout.rule_weight = \
            (VIS_WEIGHT, VIEW_WEIGHT, RULE_WEIGHT) if readable else (0.0, 0.0, 0.0)
        d = design(layout, bases, curls, s, SEED)
        tried.append({"scale": s, "readability_rules": readable, "fits": d["fits"], "reads": d["reads"],
                      "stack_top_cm": round(d["stack_top_m"] * 100.0, 2),
                      "worst_liner_excess_mm": round(max(r[2] for r in d["rows"]) * 1000.0, 1),
                      "visible": d["visible"], "hidden_targets": d["hidden_targets"], "chord_deg": d["chord_deg"],
                      "crest_overlap_cells": d["crest_overlap_cells"]})
        log("scale %.2f: %s" % (s, tried[-1]))
        return d
    # 1. the geometric limit: the largest scale at which 4 fish fit at all (the stack only, no readability rules)
    fit_limit = None
    for s in ([] if os.environ.get("FISH_COOLER_SKIP_LIMIT") == "1" else LIMIT_PROBE):
        if run(s, False)["fits"]:
            fit_limit = s
            break
    # 2. the slots: designed at the display cap, stack AND readability (eyes, tails, rotation rule, top view)
    chosen = run(DISPLAY_CAP, True)
    if not (chosen["fits"] and chosen["reads"]) and os.environ.get("FISH_COOLER_EXPLORE") != "1":
        raise RuntimeError("no good layout at the display cap %.2f: %s" % (DISPLAY_CAP, tried[-1]))
    s_cap = DISPLAY_CAP
    searched = slots_from(chosen, curls)            # the front-frame layout the search found (for the record)
    # 3. turned to the back wall, then polished on the exact checks (PILE_TURN_DEG, POLISH_*)
    state, polished = polish(layout, chosen["variants"], bases, turn_state(chosen["state"]), s_cap, curls, curls_by,
                             lid_z, body_tree)
    slots = state_slots(layout, chosen["variants"], state, s_cap, curls)
    # exact checks: every species mix at S_CAP, 1.0 and 0.7, 32 random mixed sizes (plus the shown mix)
    names = [c.species for c in curls]
    worst, checks = exact_fit(slots, curls_by, names, s_cap, lid_z)
    shown = exact_check(slots, curls_by, [sp for sp, _ in SHOW], [min(sc, s_cap) for _, sc in SHOW], lid_z)
    big = exact_check(slots, curls_by, [sp for sp, _ in SHOW], [sc for _, sc in SHOW], lid_z)
    # ... and against the real (faceted) liner wall mesh, at the cap and at 0.7
    walls = [wall_check(slots, curls_by, names, s, wall_tree) for s in (s_cap, 0.7)]
    wall = {"wall_overlaps": sum(w["wall_overlaps"] for w in walls), "wall_min_mm": min(w["wall_min_mm"] for w in walls)}
    log("liner wall mesh: %s" % wall)
    rule = rule_report(layout, chosen["variants"], state)
    log("rotation rule (polished slots): %s" % rule)
    ok = worst["tri_overlaps"] == 0 and worst["liner_excess_mm"] <= FIT_LINER_MM         and worst["lid_gap_mm_min"] >= LID_MARGIN_M * 1000.0 and worst["below_floor_mm"] <= 0.5         and wall["wall_overlaps"] == 0 and wall["wall_min_mm"] >= WALL_MIN_MM
    # exact view check (real meshes, ray casts): every mix x every fill count, from each camera
    views = {name: {"hidden_max": 0, "cases": 0, "hidden_cases": []} for name, _c, _w in CAMS}
    for m in range(16):
        mix = [names[(m >> i) & 1] for i in range(4)]
        for n in range(1, 5):
            for name, cam, _w in CAMS:
                hid = view_check(slots[:n], curls_by, mix[:n], [s_cap] * n, body_tree, cam)
                v = views[name]
                v["cases"] += 1
                if any(hid):
                    v["hidden_max"] = max(v["hidden_max"], sum(hid))
                    v["hidden_cases"].append({"mix": "".join(sp[0] for sp in mix[:n]), "hidden": hid})
    for name, v in views.items():
        v["cases_all_visible"] = v["cases"] - len(v["hidden_cases"])
        log("view %s: %d / %d fills show every eye and tail" % (name, v["cases_all_visible"], v["cases"]))
    ok = ok and not views[CAMS[0][0]]["hidden_cases"]
    # the far views (report only): fills 1 and 2, every species mix, at the cap and at 0.7: eyes/tails hidden per fish
    # and the share of each fish's upper surface in view
    far = {}
    for name, cam in FAR_CAMS:
        rec = {"hidden": {}, "share": {}}
        for mix in (["Bonefish"], ["CoralSnapper"], ["Bonefish", "Bonefish"], ["Bonefish", "CoralSnapper"],
                    ["CoralSnapper", "Bonefish"], ["CoralSnapper", "CoralSnapper"]):
            n = len(mix)
            for s in (s_cap, 0.7):
                key = "%s@%.1f" % ("".join(sp[0] for sp in mix), s)
                rec["hidden"][key] = view_check(slots[:n], curls_by, mix, [s] * n, body_tree, cam)
                rec["share"][key] = [round(x, 3) for x in view_share(slots[:n], curls_by, mix, [s] * n, body_tree, cam)]
        far[name] = rec
        log("view %s (fills 1-2): upper surface in view %s; eyes/tails hidden %s" % (name, rec["share"], rec["hidden"]))
    # the game's own check (C++ Project.Catch.Display.EveryFishVisible): the smallest fish in each slot clears the
    # line of sight over the front rim from CPP_VIEW_D (deficit <= 0); a lone fish lies at the back (X < 0)
    cpp_sight = [round(cpp_sight_deficit_cm(sl["x"] * 100.0, (sl["bed_z"] - cooler.FLOOR_Z) * 100.0), 2) for sl in slots]
    log("C++ sight-line deficit per slot (cm, <= 0 = seen; smallest fish s %.3f): %s" % (S_MIN, cpp_sight))
    ok = ok and max(cpp_sight) <= 0.0 and slots[0]["x"] < 0.0 and rule["same_side_apart_min_deg"] >= ROT_APART_MIN
    log("exact worst %s ok=%s" % (worst, ok))
    table = slots_ue(slots, s_cap)
    for row in table:
        log("slot %s" % row)
    if os.environ.get("FISH_COOLER_WRITE_DT") == "1":
        write_dt(table)
    matches_dt = compare_dt(table)
    log("slots_ue vs DT_CoolerDisplay %s: %s" % ("/".join(DT_ROWS), matches_dt))
    fk.preview_setup()
    paths = [] if os.environ.get("FISH_COOLER_NO_RENDER") == "1" else render_all(slots, curls_by, s_cap)
    log("renders done")
    result = {
        "asset": "anim_fish_cooler", "preview": str(PREVIEW), "views": paths,
        "pose": {"head_deg": fr.CURLED_HEAD_DEG, "body_deg": fr.CURLED_BODY_DEG},
        "curled_fit": fit, "liner": {"floor_z_m": cooler.FLOOR_Z, "lid_underside_z_m": round(lid_z, 4),
                                     "margin_mm": MARGIN_M * 1000.0, "lid_margin_mm": LID_MARGIN_M * 1000.0},
        "scale_search": tried, "fit_limit_scale": fit_limit, "display_cap_scale": s_cap,
        "slots_ue": table, "slots_blender": slots, "matches_dt": matches_dt,
        "searched_front_slots_ue": slots_ue(searched, s_cap), "pile_turn_deg": PILE_TURN_DEG, "polish": polished,
        "cpp_sight_deficit_cm": cpp_sight, "cpp_min_scale": round(S_MIN, 4), "far_views": far,
        "liner_wall_mesh_check": wall,
        "lie_offset_cm": {c.species: round(c.lie[1] * 100.0, 2) for c in curls},
        "exact_checks_worst": worst, "exact_ok": ok, "view_checks": views,
        "rule_chord_deg_searched": chosen["chord_deg"], "rotation_rule": rule, "shown_mix_check": shown, "unclamped_1_3_check": big,
        "exact_checks": checks,
    }
    if not ok:
        raise RuntimeError("exact checks failed (previews rendered for inspection): %s" % worst)
    print("RESULT_JSON:" + json.dumps(result))


if __name__ == "__main__":
    main()

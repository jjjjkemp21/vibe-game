"""anim_fish_cooler: layout proof for A_Fish_Curled in the starter cooler (T-030) + the 4 display slots.

Preview/validation only: nothing here is exported (A_Fish_Curled itself is exported by anim_fish.py). The fish
rig and the pose come from art/lib/fishrig.py (curled()), the fish meshes from the model-artist's recipes (via
anim_fish.Fish), the cooler from art/recipes/sm_cooler_starter.py (build(), liner constants), the scene pieces from
art/recipes/preview_cooler_starter.py (dock, EEVEE renders). None of them is edited here.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fish_cooler.py
Result: Saved/AgentLogs/blender/anim_fish_cooler.result.json ("slots_ue" = the slot table in SK_Fish.anim.md).
Previews (Saved/AgentLogs/previews/):
  A_Fish_Curled_cooler.png        contact sheet of the frames below
  A_Fish_Curled_cooler_fp_counter.png  first person at the shop counter: the open cooler on the counter top (0.9 m),
                                  0.55 m ahead of the player standing at the counter (eye 1.65 m over the floor),
                                  the game's FP camera (90 deg, 1920x1080) looking down into it
  A_Fish_Curled_cooler_fp_floor.png    the cooler on the floor 0.6 m ahead of a standing player (eye to fish ~1.3 m)
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
  visibility ignored): 1.05 (2026-09-23). The display cap S_CAP = DISPLAY_CAP = 1.0 is a little under it on purpose:
  at 1.05 the lower fish are almost fully buried (the top view read as 2-3 fish), at 1.0 every fish shows at least
  VIS_FEASIBLE of its footprint from above. Slots are designed with every fish at S_CAP; a smaller fish sits in the
  same place on the same bed (it drops onto the bed below it).
- Exact checks (BVH triangle overlap, every vertex vs the liner and the lid) for all 16 species mixes at scales
  S_CAP, 1.0 and 0.7, plus 32 seeded random mixes where every fish has its own scale in 0.7 .. S_CAP, all with the
  game's rule for the height (below).

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
VIS_MIN, VIS_WEIGHT = 0.30, 1.00  # each fish should show >= 30 % of its footprint from above (4 fish read as 4)
VIS_FEASIBLE = 0.18             # a layout where any fish shows less than this is rejected
LIMIT_PROBE = (1.1, 1.05, 1.0)   # the fit limit (visibility ignored); 1.15+ is far off (stack 34-36 cm > lid 33)
DISPLAY_CAP = 1.0               # the slots are designed for fish up to this scale (see the module docstring)
YAW_STEP = 5                    # deg
SEED = 20260923
SHOW = [("Bonefish", 1.0), ("CoralSnapper", 1.0), ("CoralSnapper", 1.0), ("Bonefish", 1.3)]
SLOT_TINT = ["#E8C46A", "#3FA34D", "#3ED1C4", "#C0392B"]


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
    top = np.full((len(xs), len(ys)), np.nan)
    bot = np.full((len(xs), len(ys)), np.nan)
    down, up = Vector((0.0, 0.0, -1.0)), Vector((0.0, 0.0, 1.0))
    zt, zb = mx[2] + 0.05, mn[2] - 0.05
    for i, x in enumerate(xs):
        for j, y in enumerate(ys):
            h = tree.ray_cast(Vector((x, y, zt)), down, 1.0)
            if h[0] is not None:
                top[i, j] = h[0].z
                h2 = tree.ray_cast(Vector((x, y, zb)), up, 1.0)
                bot[i, j] = h2[0].z if h2[0] is not None else h[0].z
    return xs[0], ys[0], top, bot


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
        for ox, oy in ((-0.25, -0.25), (0.25, -0.25), (-0.25, 0.25), (0.25, 0.25), (0.0, 0.0)):
            Xs, Ys = X + ox * CELL, Y + oy * CELL
            bx = (c * Xs + sn * Ys) / s
            by = (-sn * Xs + c * Ys) / s
            for x0, y0, t, b in bases:
                ix = np.rint((bx - x0) / BASE_CELL).astype(int)
                iy = np.rint((by - y0) / BASE_CELL).astype(int)
                ok = (ix >= 0) & (ix < t.shape[0]) & (iy >= 0) & (iy < t.shape[1])
                tv = np.full(I.shape, np.nan)
                bv = np.full(I.shape, np.nan)
                tv[ok] = t[ix[ok], iy[ok]]
                bv[ok] = b[ix[ok], iy[ok]]
                top = np.where(np.isnan(tv), top, np.maximum(top, tv * s))
                bot = np.where(np.isnan(bv), bot, np.minimum(bot, bv * s))
        occ = np.isfinite(top)
        self.di, self.dj = I[occ], J[occ]
        self.top, self.bot = top[occ], bot[occ]
        self.top_max = float(self.top.max())
        self.side, self.yaw, self.s = side, yaw_deg, s


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
        self.vis_weight = VIS_WEIGHT

    def drop(self, placements, variants, want_owner=False):
        """placements: [(variant key, pi, pj)]. Returns (cost, [(origin z, top z, liner excess, visible share)]).
        Cost (m): the stack's top + VIS_WEIGHT x each slot's shortfall below VIS_MIN of its footprint seen from above
        + 200 x any liner or lid violation."""
        H = np.full((2 * self.NI + 1, 2 * self.NJ + 1), self.floor_z)
        owner = np.full(H.shape, -1)
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
        stack = max(r[1] for r in rows)
        if want_owner:
            return cost + stack, rows, owner
        return cost + stack, rows


def anneal(layout, variants, keys, n_slots, rng, iters):
    """Annealing over [(key, pi, pj)] x n_slots. keys: list of (side, yaw). Deterministic for a given rng."""
    state = []
    for k in range(n_slots):
        side, yaw = keys[rng.randrange(len(keys))]
        state.append(((side, yaw), rng.randint(-8, 8), rng.randint(-10, 10)))
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
            side = -side
        elif r < 0.9:
            pi += rng.randint(-3, 3)
            pj += rng.randint(-3, 3)
        else:
            k2 = rng.randrange(n_slots)
            new[k], new[k2] = new[k2], new[k]
            (side, yaw), pi, pj = new[k]
        new[k] = ((side, yaw), max(-20, min(20, pi)), max(-28, min(28, pj)))
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
    for restart in range(10):
        rng = random.Random(rng_seed + restart)
        c, st = anneal(layout, variants, keys, 4, rng, 10000)
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
    reads = min(r[3] for r in rows) >= VIS_FEASIBLE
    return {"scale": s, "cost": cost, "state": state, "rows": rows, "fits": fits, "reads": reads,
            "stack_top_m": max(r[1] for r in rows), "visible": [round(r[3], 2) for r in rows], "labels": labels}


# ---------------------------------------------------------------------------------------------------------------
# Slots, exact checks, Unreal conversion
# ---------------------------------------------------------------------------------------------------------------
def slots_from(des, curls):
    lie_max = max(cf.lie[side] for cf in curls for side in (1, -1))
    out = []
    for ((side, yaw), pi, pj), (z, top, ex, vis) in zip(des["state"], des["rows"]):
        bed = z - lie_max * des["scale"]           # the bed under this slot (lowest point of the envelope)
        if bed < cooler.FLOOR_Z + GAP_M:            # resting on the floor (the raster bottom is sampled): exact
            bed = cooler.FLOOR_Z
        out.append({"side": side, "yaw": yaw, "x": pi * CELL, "y": pj * CELL, "bed_z": bed, "origin_z_cap": z,
                    "visible_from_above": round(vis, 2)})
    for sl, lab in zip(out, des["labels"]):
        sl["label_xy"] = lab
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
        fish[:] = place_fish(slots, curls_by, mix, M, s_cap, clamp=clamp)
        pcs.place_cooler(parts, M, open_lid=True)
        return M @ Vector((0.0, 0.0, 0.17))

    # 1. at the shop counter: the open cooler on the counter top (0.9 m), the player standing at the counter (eye
    #    1.65 m over the deck) with the cooler 0.55 m ahead, looking down into it: the game's FP camera
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
    c = stage(pcs.xform(0.55, 0.0, counter_top, 180.0), SHOW)
    paths.append(pcs.render_eevee(frame("fp_counter"), (0.0, 0.0, 0.0), tuple(c), hfov_deg=90.0,
                                  resolution=(1920, 1080)))
    bpy.data.objects.remove(counter, do_unlink=True)
    # 2. on the floor in front of a standing player, ~1.3 m from the eye to the fish (0.6 m ahead)
    c = stage(pcs.xform(0.60, 0.0, pcs.DECK_Z, 180.0), SHOW)
    paths.append(pcs.render_eevee(frame("fp_floor"), (0.0, 0.0, 0.0), tuple(c), hfov_deg=90.0,
                                  resolution=(1920, 1080)))
    stage(pcs.xform(1.2, 0.0, pcs.DECK_Z, 180.0), SHOW)
    # 3. straight down (orthographic), then the same with the 1.3x fish NOT clamped
    paths.append(pcs.render_eevee(frame("top"), (1.2, 0.0, pcs.DECK_Z + 3.0), (1.2, 0.0, pcs.DECK_Z), ortho=0.75,
                                  resolution=(1280, 720)))
    stage(pcs.xform(1.2, 0.0, pcs.DECK_Z, 180.0), SHOW, clamp=False)
    paths.append(pcs.render_eevee(frame("big"), (1.2, 0.0, pcs.DECK_Z + 3.0), (1.2, 0.0, pcs.DECK_Z), ortho=0.75,
                                  resolution=(1280, 720)))
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
    return paths


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
    for o in list(bpy.data.objects):
        if o.name.startswith(("SM_Cooler", "UCX_SM_Cooler")):
            bpy.data.objects.remove(o, do_unlink=True)
    log("liner floor z %.3f, closed lid underside z %.4f" % (cooler.FLOOR_Z, lid_z))
    bases = {side: [base_raster(c, side) for c in curls] for side in (1, -1)}
    log("base rasters done")
    layout = Layout(cooler.FLOOR_Z, lid_z)
    tried = []

    def run(s, vis_weight):
        layout.vis_weight = vis_weight
        d = design(layout, bases, curls, s, SEED)
        tried.append({"scale": s, "visibility_weighted": vis_weight > 0.0, "fits": d["fits"], "reads": d["reads"],
                      "stack_top_cm": round(d["stack_top_m"] * 100.0, 2),
                      "worst_liner_excess_mm": round(max(r[2] for r in d["rows"]) * 1000.0, 1),
                      "visible": d["visible"]})
        log("scale %.2f: %s" % (s, tried[-1]))
        return d
    # 1. the geometric limit: the largest scale at which 4 fish fit at all (the stack only, visibility ignored)
    fit_limit = None
    for s in LIMIT_PROBE:
        if run(s, 0.0)["fits"]:
            fit_limit = s
            break
    # 2. the slots: designed at the display cap, stack AND visibility
    chosen = run(DISPLAY_CAP, VIS_WEIGHT)
    if not (chosen["fits"] and chosen["reads"]):
        raise RuntimeError("no good layout at the display cap %.2f: %s" % (DISPLAY_CAP, tried[-1]))
    s_cap = DISPLAY_CAP
    slots = slots_from(chosen, curls)
    # exact checks: every species mix at S_CAP, 1.0 and 0.7 (plus the shown mix)
    checks = []
    names = [c.species for c in curls]
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
    shown = exact_check(slots, curls_by, [sp for sp, _ in SHOW], [min(sc, s_cap) for _, sc in SHOW], lid_z)
    big = exact_check(slots, curls_by, [sp for sp, _ in SHOW], [sc for _, sc in SHOW], lid_z)
    worst = {"tri_overlaps": max(c["tri_overlaps"] for c in checks),
             "liner_excess_mm": max(c["liner_excess_mm"] for c in checks),
             "lid_gap_mm_min": min(c["lid_gap_mm"] for c in checks),
             "below_floor_mm": max(c["below_floor_mm"] for c in checks)}
    ok = worst["tri_overlaps"] == 0 and worst["liner_excess_mm"] <= 0.0 and worst["lid_gap_mm_min"] >= 0.0 \
        and worst["below_floor_mm"] <= 0.5
    log("exact worst %s ok=%s" % (worst, ok))
    table = slots_ue(slots, s_cap)
    for row in table:
        log("slot %s" % row)
    fk.preview_setup()
    paths = render_all(slots, curls_by, s_cap)
    log("renders done")
    result = {
        "asset": "anim_fish_cooler", "preview": str(PREVIEW), "views": paths,
        "pose": {"head_deg": fr.CURLED_HEAD_DEG, "posterior_deg": fr.CURLED_POST_DEG,
                 "posterior_share": fr.CURLED_POST_SHARE},
        "curled_fit": fit, "liner": {"floor_z_m": cooler.FLOOR_Z, "lid_underside_z_m": round(lid_z, 4),
                                     "margin_mm": MARGIN_M * 1000.0, "lid_margin_mm": LID_MARGIN_M * 1000.0},
        "scale_search": tried, "fit_limit_scale": fit_limit, "display_cap_scale": s_cap,
        "slots_ue": table, "slots_blender": slots,
        "lie_offset_cm": {c.species: round(c.lie[1] * 100.0, 2) for c in curls},
        "exact_checks_worst": worst, "exact_ok": ok, "shown_mix_check": shown, "unclamped_1_3_check": big,
        "exact_checks": checks,
    }
    if not ok:
        raise RuntimeError("exact checks failed (previews rendered for inspection): %s" % worst)
    print("RESULT_JSON:" + json.dumps(result))


if __name__ == "__main__":
    main()

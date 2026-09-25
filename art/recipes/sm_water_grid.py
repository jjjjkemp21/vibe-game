"""SM_WaterGrid_Near + SM_WaterGrid_Far (T-069d): the flat technical grid that M_LevelWater displaces (WPO waves) and
shades faceted. Nobody sees the mesh itself; what matters is vertex spacing, clean seams and the budget.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_water_grid.py

Layout (Blender meters, pivot = center, Z = 0, normals +Z, one slot M_LevelWater, no collision):
- Near: a square of 2 m cells, half-extent NEAR_HALF. Size = Palm Key land bounds (data/levels/L_PalmKey.json, all
  blocks except the sea/water groups and the separate Gull Key islet) + 60 m margin, measured about the pivot (the
  builder places the pivot at the sea center), rounded up to a multiple of 16 m.
  Every vertex is at an exact multiple of 2 m (built from integer indices). Every cell is split along ONE fixed
  diagonal: Unreal (-X,-Y) -> (+X,+Y) = Blender (-X,+Y) -> (+X,-Y) (Unreal Y = -Blender Y).
- Far: a square ring from NEAR_HALF out to +-400 m (the sea/water_deep plane), in bands whose cell size doubles.
  At every cell-size change (and on the near/far edge) the coarse cell connects to the fine midpoint vertex
  (a 3-triangle fan), so there is no T-junction anywhere; shared edge vertices are the same exact floats.
- Vertex color R = cell size / 64 m (clamped to 1); at a vertex shared by different cell sizes the SMALLEST one, so
  both sides of every seam carry the same value. UV0 = planar XY over each mesh (0..1).
"""
import json
import math
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "art" / "lib"))
sys.path.insert(0, str(REPO / "Content" / "Python"))

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import pipeline_blender as pb  # noqa: E402
from levels import layout as L  # noqa: E402

EXPORT = True  # False = gate A blockout only (preview + stats, no FBX)

LAYOUT = REPO / "data" / "levels" / "L_PalmKey.json"
LAND_EXCLUDE_GROUPS = {"sea", "water", "islet"}
MARGIN_M = 60.0
ROUND_M = 16
CELL_NEAR = 2
FAR_EDGE = 400
# Far bands (cell size m, outer half-extent m). Each band's width and full span divide by its cell size, so every
# coarse grid line lands on a fine grid line.
FAR_BANDS = [(4, 160), (8, 192), (16, 240), (32, 400)]
MAT_NAME = "M_LevelWater"
R_DIVISOR = 64.0
TRI_CAP = 60000

PREVIEW_DIR = REPO / "Saved" / "AgentLogs" / "previews"


# ---------------------------------------------------------------------------------------------------------------------
# Land bounds from the layout (Unreal cm, X north, Y east)
# ---------------------------------------------------------------------------------------------------------------------
def land_prims():
    prims = L.expand(L.load(str(LAYOUT)))["prims"]
    return [p for p in prims if p["group"] not in LAND_EXCLUDE_GROUPS]


def footprint_half(p):
    sx, sy, _ = p["size"]
    y = math.radians(p["yaw"])
    if p["shape"] in ("cone", "cylinder", "sphere"):
        a, b = sx / 2.0, sy / 2.0
        return math.hypot(a * math.cos(y), b * math.sin(y)), math.hypot(a * math.sin(y), b * math.cos(y))
    return (abs(sx / 2 * math.cos(y)) + abs(sy / 2 * math.sin(y)),
            abs(sx / 2 * math.sin(y)) + abs(sy / 2 * math.cos(y)))


def land_bounds_m(prims):
    xs, ys = [], []
    for p in prims:
        hx, hy = footprint_half(p)
        cx, cy, _ = p["center"]
        xs += [cx - hx, cx + hx]
        ys += [cy - hy, cy + hy]
    return [min(xs) / 100, max(xs) / 100, min(ys) / 100, max(ys) / 100]


def footprint_outline(p, n=48):
    """Footprint polygon in Unreal meters."""
    sx, sy, _ = p["size"]
    cx, cy, _ = p["center"]
    y = math.radians(p["yaw"])
    c, s = math.cos(y), math.sin(y)
    if p["shape"] in ("cone", "cylinder", "sphere"):
        pts = [(sx / 2 * math.cos(t), sy / 2 * math.sin(t)) for t in (2 * math.pi * i / n for i in range(n))]
    else:
        pts = [(-sx / 2, -sy / 2), (sx / 2, -sy / 2), (sx / 2, sy / 2), (-sx / 2, sy / 2)]
    return [((cx + x * c - yy * s) / 100, (cy + x * s + yy * c) / 100) for x, yy in pts]


PRIMS = land_prims()
LAND = land_bounds_m(PRIMS)
need = max(abs(LAND[0]), abs(LAND[1]), abs(LAND[2]), abs(LAND[3])) + MARGIN_M  # half-extent about the pivot
NEAR_SIZE = int(math.ceil(2 * need / ROUND_M) * ROUND_M)
NEAR_HALF = NEAR_SIZE // 2
assert NEAR_HALF % CELL_NEAR == 0
assert FAR_BANDS[-1][1] == FAR_EDGE


# ---------------------------------------------------------------------------------------------------------------------
# Geometry (integer meters, Blender axes)
# ---------------------------------------------------------------------------------------------------------------------
def near_cells():
    n = NEAR_SIZE // CELL_NEAR
    return [(-NEAR_HALF + i * CELL_NEAR, -NEAR_HALF + j * CELL_NEAR, CELL_NEAR) for j in range(n) for i in range(n)]


def far_cells():
    cells = []
    inner = NEAR_HALF
    for c, outer in FAR_BANDS:
        assert (outer - inner) % c == 0 and (2 * inner) % c == 0, (c, inner, outer)
        n = 2 * outer // c
        for j in range(n):
            for i in range(n):
                x0, y0 = -outer + i * c, -outer + j * c
                if -inner <= x0 and x0 + c <= inner and -inner <= y0 and y0 + c <= inner:
                    continue  # inside the hole
                cells.append((x0, y0, c))
        inner = outer
    return cells


NEAR = near_cells()
FAR = far_cells()
ALL_POINTS = set()
MIN_CELL = {}
for x0, y0, c in NEAR + FAR:
    for p in ((x0, y0), (x0 + c, y0), (x0 + c, y0 + c), (x0, y0 + c)):
        ALL_POINTS.add(p)
        MIN_CELL[p] = min(MIN_CELL.get(p, 999), c)


def edge_points(a, b):
    """Existing vertices strictly inside segment a-b (all vertices lie on the 2 m lattice)."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    g = math.gcd(abs(dx) // CELL_NEAR, abs(dy) // CELL_NEAR)
    out = []
    for k in range(1, g):
        p = (a[0] + dx * k // g, a[1] + dy * k // g)
        if p in ALL_POINTS:
            out.append(p)
    return out


def cell_triangles(x0, y0, c, near):
    a, b, cc, d = (x0, y0), (x0 + c, y0), (x0 + c, y0 + c), (x0, y0 + c)  # CCW from +Z
    if near:
        # Fixed diagonal b-d = Blender (+X,-Y)-(-X,+Y) = Unreal (+X,+Y)-(-X,-Y).
        return [(a, b, d), (b, cc, d)]
    corners = [a, b, cc, d]
    ring, split = [], []
    for k in range(4):
        p, q = corners[k], corners[(k + 1) % 4]
        mids = edge_points(p, q)
        split.append(bool(mids))
        ring.append(p)
        ring += mids
    # Fan apex: a corner whose two edges carry no extra vertex (prefer b, then d: same diagonal as the near rule).
    for k in (1, 3, 0, 2):
        if not split[k] and not split[(k - 1) % 4]:
            apex = corners[k]
            break
    else:
        raise RuntimeError("cell %s has no clean apex" % ((x0, y0, c),))
    i = ring.index(apex)
    ring = ring[i:] + ring[:i]
    return [(ring[0], ring[k], ring[k + 1]) for k in range(1, len(ring) - 1)]


def build_tris(cells, near):
    tris = []
    for x0, y0, c in cells:
        tris += cell_triangles(x0, y0, c, near)
    return tris


def make_mesh(name, tris, half):
    verts, index = [], {}
    faces = []
    for t in tris:
        f = []
        for p in t:
            if p not in index:
                index[p] = len(verts)
                verts.append((float(p[0]), float(p[1]), 0.0))
            f.append(index[p])
        faces.append(f)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    me.update()
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    me.materials.append(MAT)
    # UV0 planar XY over this mesh, 0..1
    uv = me.uv_layers.new(name="UVMap")
    lv = np.array([l.vertex_index for l in me.loops])
    co = np.array(verts)[lv]
    uv.data.foreach_set("uv", ((co[:, :2] + half) / (2.0 * half)).ravel())
    # Vertex color R = smallest touching cell size / 64 (sRGB-stored byte = value, exported as is)
    col = me.color_attributes.new(name="Col", type="BYTE_COLOR", domain="POINT")
    for i, v in enumerate(verts):
        r = min(1.0, MIN_CELL[(int(v[0]), int(v[1]))] / R_DIVISOR)
        col.data[i].color_srgb = (r, 0.0, 0.0, 1.0)
    return obj


args = pb.parse_args("SM_WaterGrid_Near", "Environment")
pb.reset_scene()
MAT = pb.make_material(MAT_NAME, (0.1, 0.35, 0.45))

near_tris = build_tris(NEAR, True)
far_tris = build_tris(FAR, False)
near_obj = make_mesh("SM_WaterGrid_Near", near_tris, NEAR_HALF)
far_obj = make_mesh("SM_WaterGrid_Far", far_tris, FAR_EDGE)


# ---------------------------------------------------------------------------------------------------------------------
# Checks (on the real Blender mesh data, float32)
# ---------------------------------------------------------------------------------------------------------------------
def mesh_edges(obj):
    me = obj.data
    co = [tuple(v.co) for v in me.vertices]
    count = {}
    for poly in me.polygons:
        vs = list(poly.vertices)
        for k in range(len(vs)):
            e = tuple(sorted((co[vs[k]], co[vs[(k + 1) % len(vs)]])))
            count[e] = count.get(e, 0) + 1
    return co, count


def checks():
    out = {}
    near_co, near_e = mesh_edges(near_obj)
    far_co, far_e = mesh_edges(far_obj)
    pts = {(round(x), round(y)) for x, y, _ in near_co} | {(round(x), round(y)) for x, y, _ in far_co}
    # T-junctions: any vertex (either mesh) strictly inside any triangle edge (either mesh)
    tj = 0
    for e in list(near_e) + list(far_e):
        a, b = (round(e[0][0]), round(e[0][1])), (round(e[1][0]), round(e[1][1]))
        dx, dy = b[0] - a[0], b[1] - a[1]
        g = math.gcd(abs(dx) // CELL_NEAR, abs(dy) // CELL_NEAR)
        tj += sum(1 for k in range(1, g) if (a[0] + dx * k // g, a[1] + dy * k // g) in pts)
    out["t_junctions"] = tj
    # Seam: near boundary edges must be exactly the far ring's inner boundary edges
    near_open = {e for e, n in near_e.items() if n == 1}
    far_open = {e for e, n in far_e.items() if n == 1}
    far_inner = {e for e in far_open if max(abs(c) for p in e for c in p[:2]) <= NEAR_HALF + 1e-6}
    out["seam_edges_near"] = len(near_open)
    out["seam_edges_far_inner"] = len(far_inner)
    out["seam_edges_matched_exactly"] = near_open == far_inner
    out["non_manifold_edges"] = sum(1 for d in (near_e, far_e) for n in d.values() if n > 2)
    # Max distance between shared-edge vertices (matched by lattice key)
    nb = {(round(v[0]), round(v[1])): v for v in near_co if max(abs(v[0]), abs(v[1])) >= NEAR_HALF - 1e-6}
    fb = {(round(v[0]), round(v[1])): v for v in far_co if max(abs(v[0]), abs(v[1])) <= NEAR_HALF + 1e-6}
    out["seam_vertices"] = [len(nb), len(fb)]
    out["seam_vertices_missing"] = len(set(nb) ^ set(fb))
    out["seam_max_distance_m"] = max(math.dist(nb[k], fb[k]) for k in nb if k in fb)
    # Near lattice: every vertex an exact multiple of 2 m
    out["near_off_lattice_vertices"] = sum(1 for x, y, z in near_co if x % 2.0 or y % 2.0 or z != 0.0)
    # Near diagonal rule, in Unreal axes (x, -y): each cell's diagonal runs (-X,-Y) -> (+X,+Y)
    good = bad = 0
    sample = []
    for e in near_e:
        (ax, ay, _), (bx, by, _) = e
        if ax != bx and ay != by:
            ua, ub = (ax, -ay), (bx, -by)
            lo, hi = (ua, ub) if ua[0] < ub[0] else (ub, ua)
            if hi[1] > lo[1]:
                good += 1
            else:
                bad += 1
            if len(sample) < 3 and abs(ax) < 3 and abs(ay) < 3:
                sample.append([[lo[0] * 100, lo[1] * 100], [hi[0] * 100, hi[1] * 100]])
    out["near_cells"] = len(NEAR)
    out["near_diagonals_unreal_minus_to_plus"] = good
    out["near_diagonals_wrong"] = bad
    out["near_diagonal_samples_unreal_cm"] = sample
    # Normals up
    out["faces_normal_not_up"] = sum(1 for o in (near_obj, far_obj) for p in o.data.polygons if p.normal.z < 0.999)
    # Degenerate triangles
    out["degenerate_tris"] = sum(1 for o in (near_obj, far_obj) for p in o.data.polygons if p.area < 1e-6)
    return out


CHK = checks()


# ---------------------------------------------------------------------------------------------------------------------
# Blockout preview: own line rasterizer, Unreal axes on the image (up = +X north, right = +Y east)
# ---------------------------------------------------------------------------------------------------------------------
def unreal_xy(p):
    return (p[0], -p[1])  # Blender -> Unreal


GLYPHS = {  # 4 x 6 stroke font (x right, y up)
    "U": [((0, 6), (0, 0)), ((0, 0), (4, 0)), ((4, 0), (4, 6))],
    "N": [((0, 0), (0, 6)), ((0, 6), (4, 0)), ((4, 0), (4, 6))],
    "R": [((0, 0), (0, 6)), ((0, 6), (4, 6)), ((4, 6), (4, 3)), ((4, 3), (0, 3)), ((0, 3), (4, 0))],
    "E": [((0, 0), (0, 6)), ((0, 6), (4, 6)), ((0, 3), (3, 3)), ((0, 0), (4, 0))],
    "A": [((0, 0), (2, 6)), ((2, 6), (4, 0)), ((1, 3), (3, 3))],
    "L": [((0, 6), (0, 0)), ((0, 0), (4, 0))],
    "P": [((0, 0), (0, 6)), ((0, 6), (4, 6)), ((4, 6), (4, 3)), ((4, 3), (0, 3))],
    "I": [((2, 0), (2, 6)), ((1, 0), (3, 0)), ((1, 6), (3, 6))],
    "G": [((4, 6), (0, 6)), ((0, 6), (0, 0)), ((0, 0), (4, 0)), ((4, 0), (4, 3)), ((4, 3), (2, 3))],
    "H": [((0, 0), (0, 6)), ((4, 0), (4, 6)), ((0, 3), (4, 3))],
    "T": [((0, 6), (4, 6)), ((2, 6), (2, 0))],
    "X": [((0, 0), (4, 6)), ((0, 6), (4, 0))],
    "Y": [((0, 6), (2, 3)), ((4, 6), (2, 3)), ((2, 3), (2, 0))],
    "+": [((0, 3), (4, 3)), ((2, 1), (2, 5))],
    "=": [((0, 2), (4, 2)), ((0, 4), (4, 4))],
}
AXES_CAPTION = "UP = UNREAL +X   RIGHT = UNREAL +Y"


class Canvas:
    def __init__(self, size, center, span):
        self.n = size
        self.cx, self.cy = center  # Unreal meters
        self.k = size / span
        self.img = np.full((size, size, 3), 0.96, dtype=np.float32)

    def px(self, ux, uy):  # Unreal (x north, y east) -> (row from top, col)
        col = (uy - self.cy) * self.k + self.n / 2
        row = self.n / 2 - (ux - self.cx) * self.k
        return row, col

    def line(self, a, b, rgb, width=1):
        r0, c0 = self.px(*a)
        r1, c1 = self.px(*b)
        self.line_px(r0, c0, r1, c1, rgb, width)

    def text(self, row, col, s, rgb, scale=5, width=2):
        """Stroke-font label; (row, col) = top-left pixel of the first glyph."""
        for ch in s:
            for (x0, y0), (x1, y1) in GLYPHS.get(ch, ()):
                self.line_px(row + (6 - y0) * scale, col + x0 * scale, row + (6 - y1) * scale, col + x1 * scale, rgb,
                             width)
            col += 6 * scale

    def line_px(self, r0, c0, r1, c1, rgb, width=1):
        steps = int(max(abs(r1 - r0), abs(c1 - c0)) * 2) + 2
        t = np.linspace(0.0, 1.0, steps)
        rr = np.round(r0 + (r1 - r0) * t).astype(int)
        cc = np.round(c0 + (c1 - c0) * t).astype(int)
        for dr in range(-(width // 2), width - width // 2):
            for dc in range(-(width // 2), width - width // 2):
                r, c = rr + dr, cc + dc
                m = (r >= 0) & (r < self.n) & (c >= 0) & (c < self.n)
                self.img[r[m], c[m]] = rgb

    def dot(self, p, rgb, rad=3):
        r, c = self.px(*p)
        r, c = int(round(r)), int(round(c))
        self.img[max(0, r - rad):r + rad + 1, max(0, c - rad):c + rad + 1] = rgb

    def save(self, path):
        img = bpy.data.images.new("wg_canvas", self.n, self.n, alpha=False)
        rgba = np.ones((self.n, self.n, 4), dtype=np.float32)
        rgba[:, :, :3] = self.img[::-1]  # Blender image rows start at the bottom
        img.pixels.foreach_set(rgba.ravel())
        img.filepath_raw = str(path)
        img.file_format = "PNG"
        img.save()
        bpy.data.images.remove(img)
        return str(path)


NEAR_RGB = (0.15, 0.35, 0.75)
FAR_RGB = (0.1, 0.55, 0.3)
LAND_RGB = (0.85, 0.45, 0.1)
BOUND_RGB = (0.8, 0.1, 0.1)
SEAM_RGB = (0.9, 0.1, 0.6)


def draw_wire(cv, tris, rgb, window=None):
    seen = set()
    for t in tris:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            e = (a, b) if a < b else (b, a)
            if e in seen:
                continue
            seen.add(e)
            if window:
                (x0, x1, y0, y1) = window
                if max(a[0], b[0]) < x0 or min(a[0], b[0]) > x1 or max(a[1], b[1]) < y0 or min(a[1], b[1]) > y1:
                    continue
            cv.line(unreal_xy(a), unreal_xy(b), rgb)


def blockout(path):
    paths = []
    # 1. Top-down, both meshes, land outlines + land bounds (+60 m margin)
    cv = Canvas(1536, (0, 0), 2 * FAR_EDGE + 20)
    draw_wire(cv, far_tris, FAR_RGB)
    draw_wire(cv, near_tris, NEAR_RGB)
    for p in PRIMS:
        poly = footprint_outline(p)
        for k in range(len(poly)):
            cv.line(poly[k], poly[(k + 1) % len(poly)], LAND_RGB, 2)
    x0, x1, y0, y1 = LAND
    for rect, w in (((x0, x1, y0, y1), 2), ((x0 - MARGIN_M, x1 + MARGIN_M, y0 - MARGIN_M, y1 + MARGIN_M), 2)):
        a, b, c, d = (rect[0], rect[2]), (rect[1], rect[2]), (rect[1], rect[3]), (rect[0], rect[3])
        for s, e in ((a, b), (b, c), (c, d), (d, a)):
            cv.line(s, e, BOUND_RGB, w)
    cv.text(10, 10, AXES_CAPTION, (0, 0, 0), scale=6, width=3)
    paths.append(cv.save(PREVIEW_DIR / "exp_watergrid_top.png"))
    # 2. Near/far corner close-up (Unreal +X,+Y corner = Blender (+H, -H)), 40 m window
    H = NEAR_HALF
    for name, center_b, span in (("corner", (H, -H), 40), ("ring_step", (192, -192), 72), ("diag", (H - 4, -(H - 4)), 14)):
        cv = Canvas(768, unreal_xy(center_b), span)
        win = (center_b[0] - span, center_b[0] + span, center_b[1] - span, center_b[1] + span)
        draw_wire(cv, far_tris, FAR_RGB, win)
        draw_wire(cv, near_tris, NEAR_RGB, win)
        for p in ALL_POINTS:
            if abs(p[0] - center_b[0]) <= span / 2 and abs(p[1] - center_b[1]) <= span / 2:
                cv.dot(unreal_xy(p), (0, 0, 0), 2 if span > 20 else 4)
        if name == "diag":
            # arrow-ish marker: Unreal +X (up) and +Y (right) axis ticks from the view's bottom-left
            ox, oy = cv.cx - span * 0.42, cv.cy - span * 0.42
            cv.line((ox, oy), (ox + 3, oy), BOUND_RGB, 3)   # +X north (up)
            cv.line((ox, oy), (ox, oy + 3), LAND_RGB, 3)    # +Y east (right)
            r, c = cv.px(ox + 3, oy)
            cv.text(int(r) - 15, int(c) + 12, "UNREAL +X", BOUND_RGB, scale=5, width=3)
            r, c = cv.px(ox, oy + 3)
            cv.text(int(r) - 50, int(c) - 40, "UNREAL +Y", LAND_RGB, scale=5, width=3)
        cv.text(10, 10, AXES_CAPTION, (0, 0, 0), scale=3, width=2)
        paths.append(cv.save(PREVIEW_DIR / ("exp_watergrid_%s.png" % name)))
    return pb.contact_sheet(paths, path, cols=2, cell=(1024, 1024), bg=(1, 1, 1))


def _fbx_find(elem, path):
    """All sub-elements along a path of ids (bytes) in an io_scene_fbx.parse_fbx tree."""
    found = [elem]
    for pid in path:
        found = [c for e in found for c in e.elems if c.id == pid]
    return found


def fbx_raw_colors(path):
    """Unique vertex color R values (rounded to 1/255) as written in the FBX file itself."""
    from io_scene_fbx import parse_fbx
    root, _ = parse_fbx.parse(str(path))
    rs = set()
    for colors in _fbx_find(root, [b"Objects", b"Geometry", b"LayerElementColor", b"Colors"]):
        arr = colors.props[0]
        rs |= {round(arr[i] * 255) for i in range(0, len(arr), 4)}
    return sorted(rs)


def reimport(path):
    before = set(bpy.data.objects)
    pb.ensure_fbx_exporter()
    bpy.ops.import_scene.fbx(filepath=str(path))
    objs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    assert len(objs) == 1, objs
    o = objs[0]
    mw = o.matrix_world
    # Blender world meters -> Unreal cm (x, -y, z); integer cm keys (all vertices are on a whole-meter lattice)
    co = [mw @ v.co for v in o.data.vertices]
    ue = [(v.x * 100.0, -v.y * 100.0, v.z * 100.0) for v in co]
    tris = [tuple(p.vertices) for p in o.data.polygons]
    names = (o.name, o.data.name, [m.name for m in o.data.materials])
    bpy.data.objects.remove(o, do_unlink=True)
    return ue, tris, names


def verify_fbx(near_path, far_path):
    out = {}
    n_ue, n_tris, n_names = reimport(near_path)
    f_ue, f_tris, f_names = reimport(far_path)
    out["names"] = {"near": n_names, "far": f_names}
    out["triangles"] = {"near": len(n_tris), "far": len(f_tris),
                        "all_triangles": all(len(t) == 3 for t in n_tris + f_tris)}
    for key, ue in (("near", n_ue), ("far", f_ue)):
        out["bounds_unreal_cm_" + key] = [[round(min(v[i] for v in ue), 3), round(max(v[i] for v in ue), 3)]
                                          for i in range(3)]
    # Diagonal rule on the re-imported near mesh, Unreal axes: every cell's diagonal (-X,-Y) -> (+X,+Y)
    cells = {}
    for t in n_tris:
        pts = [n_ue[i] for i in t]
        for k in range(3):
            a, b = pts[k], pts[(k + 1) % 3]
            if abs(a[0] - b[0]) > 1 and abs(a[1] - b[1]) > 1:
                lo, hi = (a, b) if a[0] < b[0] else (b, a)
                cells[(round(min(a[0], b[0])), round(min(a[1], b[1])))] = (
                    [round(lo[0], 3), round(lo[1], 3)], [round(hi[0], 3), round(hi[1], 3)], hi[1] > lo[1])
    ok = sum(1 for v in cells.values() if v[2])
    out["near_cells_with_diagonal"] = len(cells)
    out["near_diagonals_minus_to_plus_unreal"] = ok
    out["near_diagonals_wrong"] = len(cells) - ok
    out["near_diagonal_list_first_and_last_unreal_cm"] = [cells[k][:2] for k in (min(cells), max(cells))]
    # Seam on the re-imported files: near boundary vertices vs far inner vertices (Unreal cm)
    h = NEAR_HALF * 100.0
    nb = {(round(v[0]), round(v[1])): v for v in n_ue if max(abs(v[0]), abs(v[1])) >= h - 0.5}
    fb = {(round(v[0]), round(v[1])): v for v in f_ue if max(abs(v[0]), abs(v[1])) <= h + 0.5}
    out["seam_vertices"] = [len(nb), len(fb), len(set(nb) & set(fb))]
    out["seam_max_distance_cm"] = max(math.dist(nb[k], fb[k]) for k in nb if k in fb)
    out["near_lattice_max_error_cm"] = max(max(abs(v[0] / 200.0 - round(v[0] / 200.0)),
                                               abs(v[1] / 200.0 - round(v[1] / 200.0))) * 200.0 for v in n_ue)
    out["fbx_color_R_bytes"] = {"near": fbx_raw_colors(near_path), "far": fbx_raw_colors(far_path)}
    return out


near_n = len(near_obj.data.polygons)
far_n = len(far_obj.data.polygons)
stats = {
    "stage": "export" if EXPORT else "gate_A_blockout",
    "land_bounds_unreal_m": {"x": [round(LAND[0], 2), round(LAND[1], 2)], "y": [round(LAND[2], 2), round(LAND[3], 2)]},
    "land_margin_m": MARGIN_M,
    "near_size_m": NEAR_SIZE,
    "near_cell_m": CELL_NEAR,
    "far_bands_m": [{"cell": c, "outer_half": o} for c, o in FAR_BANDS],
    "triangles_near": near_n,
    "triangles_far": far_n,
    "triangles_total": near_n + far_n,
    "triangle_cap": TRI_CAP,
    "within_cap": near_n + far_n <= TRI_CAP,
    "bounds_near_m": [[-NEAR_HALF, NEAR_HALF], [-NEAR_HALF, NEAR_HALF], [0, 0]],
    "bounds_far_m": [[-FAR_EDGE, FAR_EDGE], [-FAR_EDGE, FAR_EDGE], [0, 0]],
    "vertex_color_R": {"rule": "min touching cell size / 64 m", **{str(c): round(c / R_DIVISOR, 5) for c in
                                                                  [CELL_NEAR] + [b[0] for b in FAR_BANDS]}},
    "material": MAT_NAME,
    "checks": CHK,
}

if EXPORT:
    near_args = args
    far_args = pb.parse_args("SM_WaterGrid_Far", "Environment")
    pb.export_fbx([near_obj], near_args.out)
    pb.export_fbx([far_obj], far_args.out)
    stats["exports"] = [near_args.out, far_args.out]
    stats["fbx_check"] = verify_fbx(near_args.out, far_args.out)

preview = blockout(PREVIEW_DIR / ("SM_WaterGrid.png" if EXPORT else "SM_WaterGrid_blockout.png"))
stats["preview"] = preview
print("RESULT_JSON:" + json.dumps(stats))

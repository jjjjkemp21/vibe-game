"""SM_Cooler_Starter + SM_Cooler_Starter_Lid: the beginner's beat-up fishing cooler (T-030, holds 4 fish).

Art pass after the designer review (Saved/AgentLogs/design/20260923-170000-cooler-review.md): a sun-faded sea-glass
body (off the water hues), an aged-white lid with 3 sun-bleached patches, a chipped lid corner, a grimy base band with
dirt creeping up the wall, thick rope-wrapped handles for the two-hand first-person carry, a weathered fish sticker with
a peeled corner, a strip of old tape over the lid's front edge and a darker liner. Two meshes so the lid can swing open.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_cooler_starter.py
Exports: art/export/Props/SM_Cooler_Starter.fbx and art/export/Props/SM_Cooler_Starter_Lid.fbx
Preview: Saved/AgentLogs/previews/SM_Cooler_Starter.png (contact sheet: closed 3/4, lid open, back/hinges, side with
handle, top with the lid open, handle close-up). Scene checks (fish capacity, first-person carry, 1.2 m and 10 m dock
views, colour variants): art/recipes/preview_cooler_starter.py.

Size (Blender 1 unit = 1 m): body shell 0.40 (X) x 0.56 (Y) x 0.33 (Z) at the rim, rope handles stick out ~5.5 cm on
each short side, closed height with the lid 0.392 m. Liner (inside) about 0.35 x 0.51 m at the floor (0.38 x 0.54 at
the lip), 0.28 m deep.

Axes (Unreal = Blender x100 cm with Y negated, default FBX import; docs/ART_STYLE.md facing rule):
- Front = Blender +X (latch, sticker, drain plug). Back = -X (hinges). Long axis = Y: the short sides with the
  rope handles face +Y and -Y, so a player holding both handles carries the box across the body.
- SM_Cooler_Starter: pivot at bottom center (origin on the floor, center of the footprint).
- SM_Cooler_Starter_Lid: pivot ON THE HINGE AXIS (back top edge, a line along Y). Attach it to the body's LidHinge
  socket with a zero relative transform. Open = rotate about the lid's local Y axis so the front rises: Unreal
  relative Pitch 0 (closed) .. about +100 (open; it clears the back wall).
- Body sockets (SOCKET_ empties -> Unreal mesh sockets): LidHinge (lid attach point), Handle_L / Handle_R (middle
  of each rope grip on the rope's center line: hand IK / carry targets; Handle_L = Blender +Y = Unreal -Y), Contents
  (liner floor center). Unchanged by the art pass; exact positions in RESULT_JSON "sockets".
Collision: a UCX_ convex hull in each FBX (body: tapered box without the handles; lid: box). Unreal imports them as
simple collision (auto-detected UCX_ prefix).
Budget: small prop <= 2000 triangles for body + lid together. No accent red (#FF4D3D is the bobber's).
Wear is flat colour (material slots) and faceted geometry only, no textures (ART_STYLE: flat colours, chunky shapes).
Decals (lid patches, sticker art, tape) are single-sided faces 1-1.5 mm off the surface, wound to face outward.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import meshkit as mk  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

ASSET = "SM_Cooler_Starter"
LID = "SM_Cooler_Starter_Lid"
BUDGET_KIND = "small_prop"

# Colour variants (sRGB hex). "seaglass" is the shipped one (chosen in the 10 m dock comparison,
# preview_cooler_starter.py); "white" (faded-white body, teal lid) is kept for that comparison only.
VARIANT = "seaglass"
VARIANTS = {
    "seaglass": {
        "M_Cooler_Body": "#648F7E",       # sun-faded sea-glass: the designer's #5E8C86 nudged greener (hue 172 -> 156 deg)
                                          # so it leaves the water's hue band (#3ED1C4 175, #0E6F7A 186 deg)
        "M_Cooler_Band": "#415F55",       # the raised stripe and the sticker's fish, deeper sea-glass
        "M_Cooler_Grime": "#4E4330",      # murky mud: the grimy base band
        "M_Cooler_Lid": "#E8DCC4",        # aged, warm white
        "M_Cooler_LidFaded": "#F3EDE0",   # sun-bleached facets on the lid top (a small step lighter)
    },
    "white": {
        "M_Cooler_Body": "#E3DCCB",
        "M_Cooler_Band": "#4A7F7B",
        "M_Cooler_Grime": "#4E4330",
        "M_Cooler_Lid": "#4A827E",
        "M_Cooler_LidFaded": "#6B9A94",
    },
}
SHARED = {
    "M_Cooler_Liner": "#B9B19E",          # darker, warm grey-beige inside so the open box reads with depth
    "M_Cooler_Dark": "#3E4A4F",           # slate: hinges, latch strap, drain plug, handle brackets
    "M_Cooler_Rope": style.TROPICAL.ROPE,
    "M_Cooler_Lashing": "#9E7B4B",        # the lashing turns at each end of the grips: rope in shadow
    "M_Cooler_Sticker": "#E6D6AE",        # faded sticker paper and old masking tape
}
PRESETS = {"M_Cooler_Rope": "wood", "M_Cooler_Lashing": "wood"}

BODY_SLOTS = ["M_Cooler_Body", "M_Cooler_Band", "M_Cooler_Liner", "M_Cooler_Dark", "M_Cooler_Rope",
              "M_Cooler_Sticker", "M_Cooler_Grime", "M_Cooler_Lashing"]
BODY, BAND, LINER, DARK, ROPE, STICKER, GRIME, LASH = range(len(BODY_SLOTS))
LID_SLOTS = ["M_Cooler_Lid", "M_Cooler_Dark", "M_Cooler_Liner", "M_Cooler_LidFaded", "M_Cooler_Sticker"]
L_LID, L_DARK, L_LINER, L_FADED, L_TAPE = range(len(LID_SLOTS))
L_CHIP = L_LINER            # the chip's broken faces show the duller inner plastic

# Shape
N = 28                      # ring points (7 per quarter)
EXP = 4.2                   # superellipse exponent: rounded-rectangle footprint
Z_TOP = 0.33                # body rim height
HX0, HY0 = 0.190, 0.270     # half sizes at the floor
HX1, HY1 = 0.200, 0.280     # half sizes at the rim (slight taper)
HINGE = Vector((-0.208, 0.0, 0.336))   # hinge axis (line along Y) in body space
LID_OVER = 0.006            # lid overhang past the body rim
LID_TOP = 0.392
FLOOR_Z = 0.050             # liner floor (Contents socket)
LINER_LIP = -0.012          # liner inset from the outer wall at the lip ...
LINER_FLOOR = -0.016        # ... and at the floor (thin stylized walls: every cm of liner counts for the fish)
HANDLE_Z = 0.215
ROPE_R = 0.018              # grip rope radius (1.5x the first pass): a 36 mm grip for the two-hand carry
LASH_R = 0.0215             # lashing turns stand proud of the grip
DECAL = 0.0012              # decal offset off the surface


def half(z):
    t = max(0.0, min(1.0, z / Z_TOP))
    return HX0 + (HX1 - HX0) * t, HY0 + (HY1 - HY0) * t


def se_x(y, hx, hy):
    """x of the superellipse footprint (half sizes hx, hy) at y (the front/back wall position)."""
    t = min(1.0, abs(y) / hy)
    return hx * (1.0 - t ** EXP) ** (1.0 / EXP)


def se_point(a, hx, hy):
    e = 2.0 / EXP
    c, s = math.cos(a), math.sin(a)
    return hx * math.copysign(abs(c) ** e, c), hy * math.copysign(abs(s) ** e, s)


def rr(z, dx=0.0, mat=BODY, hx=None, hy=None):
    """Rounded-rectangle ring at height z, grown by dx (negative = inset) from the body wall (or hx/hy)."""
    bx, by = half(z)
    hx = (bx if hx is None else hx) + dx
    hy = (by if hy is None else hy) + dx
    return mk.ring((0, 0, z), (1, 0, 0), (0, 1, 0), hx, hy, n=N, exponent=EXP, mat=mat)


def wavy_ring(zfun, mat):
    """Body-wall ring whose height varies around the box (same point layout as rr), for the grime line."""
    pts = []
    for k in range(N):
        a = 2.0 * math.pi * k / N
        z = zfun(a)
        x, y = se_point(a, *half(z))
        pts.append((x, y, z))
    return mk.ring((0, 0, 0), (1, 0, 0), (0, 1, 0), 1.0, n=N, mat=mat, points=pts)


def grime_line(a):
    """Top of the grime band: 4.2 cm plus dirt tongues creeping up the wall (deterministic)."""
    return (0.042 + 0.032 * max(0.0, math.cos(3.0 * a - 0.9)) ** 6
            + 0.012 * max(0.0, math.sin(5.0 * a + 1.3)) ** 4)


def box(mb, corners, mat):
    """Closed hexahedron from 8 corners: bottom 4 then the matching top 4 (normals are recalculated later)."""
    bm = mb.bm
    v = [bm.verts.new(Vector(c)) for c in corners]
    for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        bm.faces.new([v[i] for i in f]).material_index = mat


def aabox(mb, lo, hi, mat):
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    box(mb, [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], mat)


def cylinder(mb, center, axis, length, radius, mat, n=8):
    ref = (0, 0, 1) if abs(axis[2]) < 0.9 else (1, 0, 0)
    mb.lathe([(-length / 2, radius, mat), (length / 2, radius, mat)], center, axis, ref, n=n, phase=math.pi / n)


def decal(mb, pts, mat, outward):
    """One single-sided face (or a fan from its centroid when fan=True-shaped input: >4 points) facing `outward`."""
    bm = mb.bm
    vs = [bm.verts.new(Vector(p)) for p in pts]
    faces = []
    if len(vs) <= 4:
        faces.append(bm.faces.new(vs))
    else:
        c = bm.verts.new(sum((Vector(p) for p in pts), Vector()) / len(pts))
        for k in range(len(vs)):
            faces.append(bm.faces.new((c, vs[k], vs[(k + 1) % len(vs)])))
    for f in faces:
        f.material_index = mat
        f.normal_update()
        if f.normal.dot(Vector(outward)) < 0.0:
            f.normal_flip()
    return faces


def strip(mb, cols, mat, center):
    """Quad strip between two columns of points (a decal band such as tape), each face turned away from `center`."""
    bm = mb.bm
    a = [bm.verts.new(Vector(p)) for p in cols[0]]
    b = [bm.verts.new(Vector(p)) for p in cols[1]]
    for i in range(len(a) - 1):
        f = bm.faces.new((a[i], b[i], b[i + 1], a[i + 1]))
        f.material_index = mat
        f.normal_update()
        mid = sum((v.co for v in f.verts), Vector()) / 4.0
        if f.normal.dot(mid - Vector(center)) < 0.0:
            f.normal_flip()


# ---------------------------------------------------------------------------------------------------
# Body
# ---------------------------------------------------------------------------------------------------
def grip_path(side, x):
    """Center line of a rope grip (x along the grip, -0.10..0.10): sags 2.4 cm down and 1.6 cm out in the middle."""
    wall = half(HANDLE_Z)[1]
    s = math.sin(math.pi * (x + 0.100) / 0.200)
    return Vector((x, side * (wall + 0.020 + 0.016 * s), HANDLE_Z - 0.024 * s))


# Grip stations (x, radius, material of the band to the next station): plain rope where the fist goes, three lashing
# turns (proud rings in the darker lashing colour) next to each bracket, rope ends buried in the brackets.
_R, _L = ROPE_R, LASH_R
GRIP_STATIONS = [
    (-0.100, _R, ROPE),                                                         # buried in the bracket
    (-0.062, _R, LASH), (-0.058, _L, LASH), (-0.054, _R, LASH), (-0.050, _L, LASH), (-0.046, _R, LASH),
    (-0.042, _L, LASH),                                                         # 3 lashing turns
    (-0.038, _R, ROPE), (-0.019, _R, ROPE), (0.0, _R, ROPE), (0.019, _R, ROPE),  # plain grip (the fist)
    (0.038, _R, LASH), (0.042, _L, LASH), (0.046, _R, LASH), (0.050, _L, LASH), (0.054, _R, LASH),
    (0.058, _L, LASH),                                                          # 3 lashing turns
    (0.062, _R, ROPE), (0.100, _R, ROPE),
]


def build_handles(mb):
    handles = {}
    for side in (1, -1):
        wall = half(HANDLE_Z)[1]
        for bx in (-0.085, 0.085):  # slate brackets, sized up for the thicker rope
            y0, y1 = sorted((side * (wall - 0.004), side * (wall + 0.044)))
            aabox(mb, (bx - 0.024, y0, HANDLE_Z - 0.026), (bx + 0.024, y1, HANDLE_Z + 0.030), DARK)
        rings = []
        xs = [x for x, _r, _m in GRIP_STATIONS]
        for i, (x, r, m) in enumerate(GRIP_STATIONS):
            p = grip_path(side, x)
            tangent = grip_path(side, xs[min(i + 1, len(xs) - 1)]) - grip_path(side, xs[max(i - 1, 0)])
            _, u, v = mk.frame_from(tangent, (0, 0, 1))
            if r == LASH_R:  # lashing turns lean along the grip like a wound rope, not stacked washers
                u = mk.rotate_about(u, v, math.radians(18.0))
            rings.append(mk.ring(p, u, v, r, n=6, mat=m, phase=math.pi / 6))
        mb.loft(rings, cap_start="flat", cap_end="flat")
        handles["Handle_L" if side > 0 else "Handle_R"] = grip_path(side, 0.0)
    return handles


def build_sticker(mb):
    """Weathered fish sticker on the front wall, a little crooked, its upper-left corner (seen from the front)
    peeled and curling off. Paper = STICKER, art (a fish and a line of 'text') = BAND, 0.8 mm above the paper."""
    cz, cy, hw, hh, ang = 0.160, -0.100, 0.068, 0.047, math.radians(-6.0)
    peel = 0.030   # the peeled corner's cut

    def at(ly, lz, off):
        y = cy + ly * math.cos(ang) - lz * math.sin(ang)
        z = cz + ly * math.sin(ang) + lz * math.cos(ang)
        bx, by = half(z)
        return Vector((se_x(y, bx, by) + off, y, z))

    def top(ly):  # upper edge of the paper: the peeled corner cuts it on the left
        return hh - max(0.0, peel - (ly + hw))

    # the paper: a thin slab (inner face sunk into the wall, outer face 2.5 mm proud), split in 3 columns so it follows
    # the rounded front wall
    cols = [-hw, -hw + peel, 0.0, hw]
    bm = mb.bm
    for y0, y1 in zip(cols[:-1], cols[1:]):
        quad = [(y0, -hh), (y1, -hh), (y1, top(y1)), (y0, top(y0))]
        box(mb, [at(a, b, -0.003) for a, b in quad] + [at(a, b, 0.0025) for a, b in quad], STICKER)
    # the curled flap: a thin wedge standing off the wall along the cut line
    p0, p1 = at(-hw, hh - peel, 0.0025), at(-hw + peel, hh, 0.0025)
    apex = at(-hw + 0.004, hh + 0.004, 0.016)
    v = [bm.verts.new(p) for p in (p0, p1, apex, at(-hw + peel * 0.5, hh - peel * 0.5, 0.0))]
    for f in ((0, 1, 2), (1, 0, 3), (0, 2, 3), (2, 1, 3)):
        bm.faces.new([v[i] for i in f]).material_index = STICKER
    # the art (decals on the paper): fish body (8-point lens), tail, eye gap left out, and a 'text' bar below
    art = 0.0033
    fish = [at(0.030 * math.cos(t) - 0.004, 0.017 * math.sin(t) * (1.0 - 0.25 * math.cos(t)) + 0.010, art)
            for t in [2.0 * math.pi * k / 10 for k in range(10)]]
    art_faces = [(fish, BAND)]
    tail = [at(-0.030, 0.010, art), at(-0.052, 0.026, art), at(-0.046, 0.010, art), at(-0.052, -0.006, art)]
    art_faces.append((tail, BAND))
    text = [at(-0.045, -0.024, art), at(0.040, -0.024, art), at(0.040, -0.031, art), at(-0.045, -0.031, art)]
    art_faces.append((text, BAND))
    text2 = [at(-0.045, -0.037, art), at(0.012, -0.037, art), at(0.012, -0.043, art), at(-0.045, -0.043, art)]
    art_faces.append((text2, BAND))
    return art_faces


def build_body(mats, variant):
    mb = mk.MeshBuilder()
    rings = [
        rr(0.000, -0.014, GRIME),           # bottom chamfer: dirtiest
        rr(0.012, 0.0, GRIME),              # grimy base band up to a wavy dirt line
        wavy_ring(grime_line, BODY),
        rr(0.262, 0.0, BAND),               # raised stripe
        rr(0.266, 0.004, BAND),
        rr(0.288, 0.004, BAND),
        rr(0.292, 0.0, BODY),
        rr(0.322, 0.0, BODY),               # rim chamfer
        rr(Z_TOP, -0.006, LINER),           # rim top (flat)
        rr(Z_TOP, LINER_LIP, LINER),        # inner lip
        rr(FLOOR_Z, LINER_FLOOR, LINER),    # liner wall down to the floor
    ]
    mb.loft(rings, cap_start="flat", cap_end="flat")

    handles = build_handles(mb)

    # Hinge knuckles (body half) at the outer positions, with pads down the back wall.
    for y in (-0.19, 0.19):
        cylinder(mb, (HINGE.x, y, HINGE.z), (0, 1, 0), 0.05, 0.012, DARK)
        aabox(mb, (HINGE.x - 0.004, y - 0.025, 0.296), (-half(0.30)[0] + 0.004, y + 0.025, HINGE.z - 0.004), DARK)

    # Rubber latch strap on the front: runs up the wall to just above the lid's front edge, with a T-grip.
    xw = half(0.24)[0]
    xl = HX1 + LID_OVER
    box(mb, [(xw - 0.003, -0.022, 0.240), (xw + 0.007, -0.022, 0.240), (xw + 0.007, 0.022, 0.240),
             (xw - 0.003, 0.022, 0.240),
             (xl - 0.001, -0.022, 0.352), (xl + 0.009, -0.022, 0.352), (xl + 0.009, 0.022, 0.352),
             (xl - 0.001, 0.022, 0.352)], DARK)
    aabox(mb, (xl + 0.004, -0.032, 0.338), (xl + 0.016, 0.032, 0.354), DARK)

    # Drain plug, front bottom (sits in the grime band)
    cylinder(mb, (half(0.07)[0] + 0.006, 0.17, 0.07), (1, 0, 0), 0.016, 0.013, DARK)

    art = build_sticker(mb)

    # closed parts done: make their normals consistent, then add the single-sided decals facing +X
    bmesh.ops.recalc_face_normals(mb.bm, faces=mb.bm.faces[:])
    for pts, mat in art:
        decal(mb, pts, mat, (1, 0, 0))

    obj = mb.to_object(ASSET, [mats[k] for k in BODY_SLOTS], sharp_angle_deg=35.0, recalc_normals=False)
    return obj, handles


# ---------------------------------------------------------------------------------------------------
# Lid
# ---------------------------------------------------------------------------------------------------
CHIP_K = {16: 0.35, 17: 1.0, 18: 0.7}   # ring points at the back-right corner (-X, -Y; the carrier's right hand)
CHIP_RINGS = {1: (0.009, 0.007), 2: (0.017, 0.015)}   # ring (side top, chamfer top): (inward, down) in m


# Sun-bleached wear on the lid's top panel (flat at LID_TOP; the panel edge is the superellipse 0.144 x 0.224).
# Low-poly style: the faded areas are big straight-edged facets (3-4 sided shards, sharing edges like a cut gem) that are
# built into the panel's own triangulation, not free-form blobs or floating decals. Two touching shards on the sun side,
# one small shard near the back corner. (x, y) corners, counter-clockwise seen from above.
LID_FACETS = [
    [(0.030, -0.190), (0.112, -0.115), (0.085, 0.005), (-0.035, -0.060)],
    [(-0.035, -0.060), (0.085, 0.005), (0.010, 0.075)],
    [(-0.112, 0.105), (-0.040, 0.125), (-0.070, 0.190), (-0.108, 0.168)],
]


def build_lid_panel(mb, outer):
    """The lid's flat top: the panel ring `outer` (BMVerts) and the LID_FACETS shards, filled by one constrained
    Delaunay triangulation (all coplanar, shared edges); triangles inside a shard take the faded colour."""
    from mathutils.geometry import delaunay_2d_cdt
    bm = mb.bm
    pts = [Vector((v.co.x, v.co.y)) for v in outer]
    nb = len(pts)
    faces = [list(range(nb))]
    for facet in LID_FACETS:
        idx = []
        for p in facet:
            q = Vector(p)
            hit = next((k for k in range(nb, len(pts)) if (pts[k] - q).length < 1e-6), None)
            if hit is None:
                pts.append(q)
                hit = len(pts) - 1
            idx.append(hit)
        faces.append(idx)
    verts, _e, out_faces, orig_v, _oe, orig_f = delaunay_2d_cdt(pts, [], faces, 1, 1e-6)
    bmv = []
    for co, orig in zip(verts, orig_v):
        ring = [k for k in orig if k < nb]
        bmv.append(outer[ring[0]] if ring else bm.verts.new(Vector((co.x, co.y, LID_TOP))))
    for f, of in zip(out_faces, orig_f):
        face = bm.faces.new([bmv[k] for k in f])
        face.material_index = L_FADED if any(k >= 1 for k in of) else L_LID


def build_lid(mats):
    """Lid built in body space, then shifted so the hinge axis is the object origin."""
    mb = mk.MeshBuilder()
    hx, hy = HX1 + LID_OVER, HY1 + LID_OVER
    rings = [
        rr(Z_TOP, 0.0, L_LID, hx, hy),
        rr(0.366, 0.0, L_LID, hx, hy),
        rr(0.378, -0.012, L_LID, hx, hy),   # top edge chamfer
        rr(0.378, -0.050, L_LID, hx, hy),
        rr(LID_TOP, -0.062, L_LID, hx, hy), # raised molded panel
    ]
    vr, _ = mb.loft(rings, cap_start="flat", cap_end=None, cap_start_mat=L_LINER)
    build_lid_panel(mb, vr[-1])

    # Chipped corner: knock the top edge in and down at one corner; the broken faces take the dirty chip colour.
    moved = set()
    for ri, (din, ddown) in CHIP_RINGS.items():
        for k, f in CHIP_K.items():
            v = vr[ri][k]
            d = Vector((v.co.x, v.co.y, 0.0)).normalized()
            v.co -= d * (din * f)
            v.co.z -= ddown * f
            moved.add(v)
    for face in mb.bm.faces:
        if sum(1 for v in face.verts if v in moved) >= 3:   # the broken chamfer faces only
            face.material_index = L_CHIP

    # Hinge knuckles (lid half) between the body knuckles, with straps onto the lid top.
    for y in (-0.135, 0.135):
        cylinder(mb, (HINGE.x, y, HINGE.z), (0, 1, 0), 0.05, 0.012, L_DARK)
        aabox(mb, (HINGE.x - 0.002, y - 0.022, HINGE.z - 0.004), (HINGE.x + 0.045, y + 0.022, 0.371), L_DARK)
    # Latch catch on the lid front (the strap's T-grip hooks over it).
    aabox(mb, (hx - 0.002, -0.036, 0.356), (hx + 0.012, 0.036, 0.366), L_DARK)

    bmesh.ops.recalc_face_normals(mb.bm, faces=mb.bm.faces[:])

    # Old masking tape over the lid's front edge near the +Y corner, a little skewed.
    cols = []
    for y_top, y_bot in ((0.112, 0.118), (0.146, 0.153)):
        cols.append([
            (se_x(y_top, hx - 0.046, hy - 0.046), y_top, 0.378 + DECAL),
            (se_x(y_top, hx - 0.012, hy - 0.012) + 0.0008, y_top + 0.001, 0.378 + 0.0009),
            (se_x(y_bot, hx, hy) + 0.0010, y_bot - 0.001, 0.366 + 0.0006),
            (se_x(y_bot, hx, hy) + DECAL, y_bot, 0.341),
        ])
    strip(mb, cols, L_TAPE, (0.0, 0.0, 0.34))

    mb.transform(Matrix.Translation(-HINGE))
    return mb.to_object(LID, [mats[k] for k in LID_SLOTS], sharp_angle_deg=35.0, recalc_normals=False)


def ucx(name, corners):
    mb = mk.MeshBuilder()
    box(mb, corners, 0)
    obj = mb.to_object(name, [], smooth=False)
    obj.hide_render = True
    obj.display_type = "WIRE"
    return obj


def tapered_box(x0, y0, x1, y1, z0, z1):
    return [(-x0, -y0, z0), (x0, -y0, z0), (x0, y0, z0), (-x0, y0, z0),
            (-x1, -y1, z1), (x1, -y1, z1), (x1, y1, z1), (-x1, y1, z1)]


def make_materials(variant=VARIANT, prefix=""):
    """Materials for a colour variant. prefix != "" makes a separate set (preview comparisons only)."""
    colors = dict(SHARED)
    colors.update(VARIANTS[variant])
    return {name: style.make_material(prefix + name, hexc, PRESETS.get(name, "flat")) for name, hexc in colors.items()}


def build(variant=VARIANT, prefix=""):
    """Build the body (with sockets) and the lid (origin on its hinge, placed at the origin). Returns a dict with
    body, lid, handles (socket points), ucx_body, ucx_lid. prefix renames the objects/materials (previews)."""
    mats = make_materials(variant, prefix)
    body, handle_pts = build_body(mats, variant)
    lid = build_lid(mats)
    for o in (body, lid):
        mk.smart_uv(o)
    ucx_body = ucx("UCX_" + ASSET + "_00", tapered_box(HX0, HY0, HX1, HY1, 0.0, Z_TOP))
    lid_box = [(x - HINGE.x, y, z - HINGE.z) for x, y, z in
               tapered_box(HX1 + LID_OVER, HY1 + LID_OVER, HX1 + LID_OVER, HY1 + LID_OVER, Z_TOP, LID_TOP)]
    ucx_lid = ucx("UCX_" + LID + "_00", lid_box)
    pb.add_socket(body, "LidHinge", tuple(HINGE))
    for name, p in handle_pts.items():
        pb.add_socket(body, name, tuple(p))
    pb.add_socket(body, "Contents", (0.0, 0.0, FLOOR_Z))
    if prefix:
        for o in (body, lid, ucx_body, ucx_lid):
            o.name = prefix + o.name
    return {"body": body, "lid": lid, "handles": handle_pts, "ucx_body": ucx_body, "ucx_lid": ucx_lid}


def main():
    args = pb.parse_args(ASSET, "Props")
    pb.reset_scene()
    parts = build()
    body, lid, ucx_body, ucx_lid = parts["body"], parts["lid"], parts["ucx_body"], parts["ucx_lid"]

    # Export: each FBX holds its mesh + its UCX hull (+ sockets). Both objects sit at the origin while exporting.
    lid_out = str(Path(args.out).with_name(LID + ".fbx"))
    tris_body = pb.triangle_count([body])
    tris_lid = pb.triangle_count([lid])
    pb.export_fbx([body, ucx_body], args.out)
    pb.export_fbx([lid, ucx_lid], lid_out)

    # Preview: lid placed on its hinge (as the LidHinge socket will in Unreal).
    lid.location = HINGE
    ucx_lid.location = HINGE
    objs = [body, lid]
    bpy.context.scene.display.shading.show_specular_highlight = False
    bpy.context.scene.view_settings.view_transform = "Standard"   # palette hex reads true (no filmic darkening)
    pb.render_preview(objs, args.preview)
    base = Path(args.preview)
    sheet = [str(base.with_name(base.stem + "_34" + base.suffix))]
    Path(args.preview).replace(sheet[0])

    def view(name, loc, target=(0, 0, 0.19), **kw):
        sheet.append(pb.render_view(base.with_name(base.stem + "_" + name + base.suffix), loc, target, **kw))

    lid.rotation_euler = (0.0, -math.radians(100.0), 0.0)
    view("open", (0.95, -0.75, 0.85), (0, 0, 0.22))
    view("top_open", (0.0, 0.0, 2.0), (0, 0, 0), ortho_scale=0.95)
    lid.rotation_euler = (0.0, 0.0, 0.0)
    view("back", (-0.95, -0.70, 0.60), (0, 0, 0.2))
    view("side", (0.0, 1.6, 0.20), (0, 0, 0.2), ortho_scale=0.75)
    view("front", (1.6, 0.0, 0.20), (0, 0, 0.2), ortho_scale=0.75)
    hl = parts["handles"]["Handle_L"]
    view("handle", (hl.x + 0.30, hl.y + 0.45, hl.z + 0.22), tuple(hl), lens=85.0)
    pb.contact_sheet(sheet, args.preview, cols=2)

    ok, budget = style.check_budget(tris_body + tris_lid, BUDGET_KIND)
    pb.report(args, objs, extra={
        "exports": [args.out, lid_out],
        "variant": VARIANT,
        "triangles_body": tris_body,
        "triangles_lid": tris_lid,
        "budget": budget,
        "within_budget": ok,
        "hinge_axis_m": [round(c, 4) for c in HINGE],
        "lid_open": "rotate about local Y; Unreal relative Pitch 0..~100 opens (front rises)",
        "collision": ["UCX_" + ASSET + "_00", "UCX_" + LID + "_00"],
        "mesh_stats": {o.name: mk.mesh_stats(o) for o in objs},
    })


if __name__ == "__main__":
    main()

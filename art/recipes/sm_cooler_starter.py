"""SM_Cooler_Starter + SM_Cooler_Starter_Lid: the beginner's beat-up fishing cooler (T-030, holds 4 fish).

A faded sea-blue plastic box with a darker stripe and base band, an off-white lid, rope handles on the short sides,
a rubber latch strap on the front, a drain plug and a sun-bleached sticker. Two meshes so the lid can swing open.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_cooler_starter.py
Exports: art/export/Props/SM_Cooler_Starter.fbx and art/export/Props/SM_Cooler_Starter_Lid.fbx
Preview: Saved/AgentLogs/previews/SM_Cooler_Starter.png (contact sheet: closed 3/4, lid open, back/hinges, side with
handle, top with the lid open).

Size (Blender 1 unit = 1 m): body shell 0.40 (X) x 0.56 (Y) x 0.33 (Z) at the rim, rope handles stick out ~4 cm on
each short side, closed height with the lid 0.392 m. Liner (inside) about 0.34 x 0.50 x 0.28 m.

Axes (Unreal = Blender x100 cm with Y negated, default FBX import; docs/ART_STYLE.md facing rule):
- Front = Blender +X (latch, sticker, drain plug). Back = -X (hinges). Long axis = Y: the short sides with the
  rope handles face +Y and -Y, so a player holding both handles carries the box across the body.
- SM_Cooler_Starter: pivot at bottom center (origin on the floor, center of the footprint).
- SM_Cooler_Starter_Lid: pivot ON THE HINGE AXIS (back top edge, a line along Y). Attach it to the body's LidHinge
  socket with a zero relative transform. Open = rotate about the lid's local Y axis so the front rises: Unreal
  relative Pitch 0 (closed) .. about +100 (open; it clears the back wall).
- Body sockets (SOCKET_ empties -> Unreal mesh sockets): LidHinge (lid attach point), Handle_L / Handle_R (middle
  of each rope grip: hand IK / carry targets; Handle_L = Blender +Y = Unreal -Y), Contents (liner floor center).
  Exact positions in RESULT_JSON "sockets".
Collision: a UCX_ convex hull in each FBX (body: tapered box without the handles; lid: box). Unreal imports them as
simple collision (auto-detected UCX_ prefix).
Budget: small prop <= 2000 triangles for body + lid together. Materials: M_Cooler_Body, M_Cooler_Band, M_Cooler_Liner,
M_Cooler_Dark, M_Cooler_Rope, M_Cooler_Sticker, M_Cooler_Lid. No accent red (#FF4D3D is the bobber's).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import meshkit as mk  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

ASSET = "SM_Cooler_Starter"
LID = "SM_Cooler_Starter_Lid"
BUDGET_KIND = "small_prop"

# Colors (sRGB hex). Sun-faded sea-blue body, deeper sea-blue bands, off-white lid.
COLORS = {
    "M_Cooler_Body": ("#9ACFD3", "flat"),
    "M_Cooler_Band": ("#3F8793", "flat"),
    "M_Cooler_Liner": ("#D9D4C6", "flat"),
    "M_Cooler_Dark": ("#3E4A4F", "flat"),         # slate: hinges, latch strap, drain plug, handle brackets
    "M_Cooler_Rope": (style.TROPICAL.ROPE, "wood"),
    "M_Cooler_Sticker": (style.TROPICAL.SAND, "flat"),
    "M_Cooler_Lid": ("#EDE7D8", "flat"),
}
BODY, BAND, LINER, DARK, ROPE, STICKER = range(6)          # body mesh slots
L_LID, L_DARK, L_LINER = range(3)                          # lid mesh slots

# Shape
N = 28                      # ring points (7 per quarter)
EXP = 4.2                   # superellipse exponent: rounded-rectangle footprint
Z_TOP = 0.33                # body rim height
HX0, HY0 = 0.190, 0.270     # half sizes at the floor
HX1, HY1 = 0.200, 0.280     # half sizes at the rim (slight taper)
HINGE = Vector((-0.208, 0.0, 0.336))   # hinge axis (line along Y) in body space
LID_OVER = 0.006            # lid overhang past the body rim
LID_TOP = 0.392
HANDLE_Z = 0.215


def half(z):
    t = max(0.0, min(1.0, z / Z_TOP))
    return HX0 + (HX1 - HX0) * t, HY0 + (HY1 - HY0) * t


def rr(z, dx=0.0, mat=BODY, hx=None, hy=None):
    """Rounded-rectangle ring at height z, grown by dx (negative = inset) from the body wall (or hx/hy)."""
    bx, by = half(z)
    hx = (bx if hx is None else hx) + dx
    hy = (by if hy is None else hy) + dx
    return mk.ring((0, 0, z), (1, 0, 0), (0, 1, 0), hx, hy, n=N, exponent=EXP, mat=mat)


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


def build_body(mats):
    mb = mk.MeshBuilder()
    rings = [
        rr(0.000, -0.014, BAND),            # bottom chamfer
        rr(0.012, 0.0, BAND),               # scuffed base band
        rr(0.045, 0.0, BODY),
        rr(0.262, 0.0, BAND),               # raised stripe
        rr(0.266, 0.004, BAND),
        rr(0.288, 0.004, BAND),
        rr(0.292, 0.0, BODY),
        rr(0.322, 0.0, BODY),               # rim chamfer
        rr(Z_TOP, -0.006, LINER),           # rim top (flat)
        rr(Z_TOP, -0.022, LINER),           # inner lip
        rr(0.050, -0.030, LINER),           # liner wall down to the floor
    ]
    mb.loft(rings, cap_start="flat", cap_end="flat")

    # Rope handles: two slate brackets per short side with a sagging rope grip between them.
    handles = {}
    for side in (1, -1):
        wall = half(HANDLE_Z)[1]
        for bx in (-0.085, 0.085):
            y0, y1 = sorted((side * (wall - 0.004), side * (wall + 0.030)))
            aabox(mb, (bx - 0.018, y0, HANDLE_Z - 0.014), (bx + 0.018, y1, HANDLE_Z + 0.028), DARK)
        steps = 9
        pts = []
        for i in range(steps):
            t = i / (steps - 1)
            s = math.sin(math.pi * t)
            pts.append(Vector((-0.095 + 0.19 * t, side * (wall + 0.020 + 0.016 * s), HANDLE_Z - 0.024 * s)))
        rope_rings = []
        for i, p in enumerate(pts):
            _, u, v = mk.frame_from(pts[min(i + 1, steps - 1)] - pts[max(i - 1, 0)], (0, 0, 1))
            rope_rings.append(mk.ring(p, u, v, 0.012, n=6, mat=ROPE, phase=math.pi / 6))
        mb.loft(rope_rings, cap_start="flat", cap_end="flat")
        handles["Handle_L" if side > 0 else "Handle_R"] = pts[steps // 2]

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

    # Drain plug, front bottom
    cylinder(mb, (half(0.07)[0] + 0.006, 0.17, 0.07), (1, 0, 0), 0.016, 0.013, DARK)

    # Sun-bleached sticker on the front, a little crooked.
    cz, cy, hw, hh, ang = 0.165, -0.10, 0.065, 0.045, math.radians(-6.0)
    c2 = []
    for sy, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        ly, lz = sy * hw, sz * hh
        c2.append((cy + ly * math.cos(ang) - lz * math.sin(ang), cz + ly * math.sin(ang) + lz * math.cos(ang)))
    inner = [(half(z)[0] - 0.003, y, z) for y, z in c2]
    outer = [(half(z)[0] + 0.0025, y, z) for y, z in c2]
    box(mb, inner + outer, STICKER)

    obj = mb.to_object(ASSET, [mats[k] for k in ("M_Cooler_Body", "M_Cooler_Band", "M_Cooler_Liner", "M_Cooler_Dark",
                                                  "M_Cooler_Rope", "M_Cooler_Sticker")], sharp_angle_deg=35.0)
    return obj, handles


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
    mb.loft(rings, cap_start="flat", cap_end="flat", cap_start_mat=L_LINER)
    # Hinge knuckles (lid half) between the body knuckles, with straps onto the lid top.
    for y in (-0.135, 0.135):
        cylinder(mb, (HINGE.x, y, HINGE.z), (0, 1, 0), 0.05, 0.012, L_DARK)
        aabox(mb, (HINGE.x - 0.002, y - 0.022, HINGE.z - 0.004), (HINGE.x + 0.045, y + 0.022, 0.371), L_DARK)
    # Latch catch on the lid front (the strap's T-grip hooks over it).
    aabox(mb, (hx - 0.002, -0.036, 0.356), (hx + 0.012, 0.036, 0.366), L_DARK)
    mb.transform(Matrix.Translation(-HINGE))
    return mb.to_object(LID, [mats["M_Cooler_Lid"], mats["M_Cooler_Dark"], mats["M_Cooler_Liner"]],
                        sharp_angle_deg=35.0)


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


args = pb.parse_args(ASSET, "Props")
pb.reset_scene()
MATS = {name: style.make_material(name, hexc, preset) for name, (hexc, preset) in COLORS.items()}

body, handle_pts = build_body(MATS)
lid = build_lid(MATS)
for o in (body, lid):
    mk.smart_uv(o)

ucx_body = ucx("UCX_" + ASSET + "_00", tapered_box(HX0, HY0, HX1, HY1, 0.0, Z_TOP))
lid_box = [(x - HINGE.x, y, z - HINGE.z) for x, y, z in
           tapered_box(HX1 + LID_OVER, HY1 + LID_OVER, HX1 + LID_OVER, HY1 + LID_OVER, Z_TOP, LID_TOP)]
ucx_lid = ucx("UCX_" + LID + "_00", lid_box)

pb.add_socket(body, "LidHinge", tuple(HINGE))
for name, p in handle_pts.items():
    pb.add_socket(body, name, tuple(p))
pb.add_socket(body, "Contents", (0.0, 0.0, 0.050))

# Export: each FBX holds its mesh + its UCX hull (+ sockets). Both objects sit at the origin while exporting.
lid_out = str(Path(args.out).with_name(LID + ".fbx"))
tris_body = pb.triangle_count([body])
tris_lid = pb.triangle_count([lid])
pb.export_fbx([body, ucx_body], args.out)
pb.export_fbx([lid, ucx_lid], lid_out)

# Preview: lid placed on its hinge (as the LidHinge socket will in Unreal).
lid.location = HINGE
ucx_lid.location = HINGE
parts = [body, lid]
pb.render_preview(parts, args.preview)
base = Path(args.preview)
sheet = [str(base.with_name(base.stem + "_34" + base.suffix))]
Path(args.preview).replace(sheet[0])


def view(name, loc, target=(0, 0, 0.19), **kw):
    sheet.append(pb.render_view(base.with_name(base.stem + "_" + name + base.suffix), loc, target, **kw))


lid.rotation_euler = (0.0, -math.radians(100.0), 0.0)
view("open", (0.95, -0.75, 0.85), (0, 0, 0.22))
view("top_open", (0.0, 0.0, 2.0), (0, 0, 0), ortho_scale=0.95)
lid.rotation_euler = (0.0, 0.0, 0.0)
view("back", (-0.95, 0.70, 0.60), (0, 0, 0.2))
view("side", (0.0, 1.6, 0.20), (0, 0, 0.2), ortho_scale=0.75)
pb.contact_sheet(sheet, args.preview, cols=2)

ok, budget = style.check_budget(tris_body + tris_lid, BUDGET_KIND)
pb.report(args, parts, extra={
    "exports": [args.out, lid_out],
    "triangles_body": tris_body,
    "triangles_lid": tris_lid,
    "budget": budget,
    "within_budget": ok,
    "hinge_axis_m": [round(c, 4) for c in HINGE],
    "lid_open": "rotate about local Y; Unreal relative Pitch 0..~100 opens (front rises)",
    "collision": ["UCX_" + ASSET + "_00", "UCX_" + LID + "_00"],
})

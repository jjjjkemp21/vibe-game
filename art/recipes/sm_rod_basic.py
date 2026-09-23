"""SM_Rod_Basic: beginner spinning rod (placeholder, T-004/T-006): cork grips, reel seat, spinning reel with a
crank, 5 line guides + tip top, thin tapered blank. Total length 1.98 m.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_rod_basic.py
Export: art/export/Props/SM_Rod_Basic.fbx     Preview: Saved/AgentLogs/previews/SM_Rod_Basic.png (contact sheet:
3/4, side, handle close-up, reel from the crank side, tip, first-person framing over water)

Axes and pivot (Blender 1 unit = 1 m, Z up; Unreal = Blender * 100 cm with Y negated, default FBX import):
- Origin (0, 0, 0) = the GRIP POINT: center of the rear cork grip on the rod axis, where the right hand's fist
  closes around the rod (hand spans about x -0.045..+0.045; the reel stem sits just in front of the fist).
- Forward = Blender +X (= Unreal +X): the blank runs from the handle along +X to the tip. The butt is at -X.
- Up = +Z. The reel hangs BELOW the rod (-Z) and the line guides are on the underside, like a real spinning rod.
- The crank handle is on the LEFT side (Blender +Y = Unreal -Y): right hand holds the rod, left hand cranks.
- Sockets (SOCKET_ empties in the FBX; Unreal makes mesh sockets): LineTip = center of the tip-top ring (where the
  line leaves the rod), ReelLine = where the line leaves the spool, CrankKnob = center of the crank knob (left-hand
  IK target). Guide ring centers are listed in RESULT_JSON ("line_path_m") for routing a line cable.
- Crank axis: parallel to Y through (0.080, y, -0.066) m (rotate the crank part around it).

Animation-ready: the blank has 20 evenly spaced 8-sided rings (every ~7 cm) so a bone chain can bend it. Vertex
groups (all weights 1.0, each vertex in exactly one group): rod_handle (grips, reel seat, reel, butt), rod_blank
(blank + guides + tip top), rod_crank (crank boss, arm and knob). Call build() from a rig recipe to get the
object with its groups (the FBX export has no groups: FBX only stores weights with an armature).

Budget: small prop <= 2000 triangles. Materials: M_Rod_Cork (rope #C9A66B), M_Rod_Blank (dark lacquer #3A2A20,
as in the mood boards), M_Rod_Dark (slate #3E4A4F: reel, seat, butt cap, guide feet), M_Rod_Trim (off-white
#F5F1E6: line on the spool, guide rings, same as the fishing line).
Bevels: the style bevel is modelled into the lathe profiles (chamfer rings on the butt cap, hoods, spool lips);
no Bevel modifier, to stay inside the budget.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import meshkit as mk  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

ASSET = "SM_Rod_Basic"
BUDGET_KIND = "small_prop"
CORK, BLANK, DARK, TRIM = 0, 1, 2, 3
GROUPS = ["rod_handle", "rod_blank", "rod_crank"]
W_HANDLE, W_BLANK, W_CRANK = {"rod_handle": 1.0}, {"rod_blank": 1.0}, {"rod_crank": 1.0}

BUTT_X = -0.3235
BLANK_START = 0.238       # inside the winding check
TIP_X = 1.655
BLANK_R0, BLANK_R1 = 0.0068, 0.0024
BLANK_RINGS = 21          # 20 segments
REEL_Z = -0.066           # reel (spool) axis height
CRANK_AXIS_X = 0.080
# guides: (x, ring major radius, ring minor radius, leg height between blank and ring)
GUIDES = [(0.60, 0.0150, 0.0019, 0.018), (0.86, 0.0105, 0.0017, 0.012), (1.10, 0.0080, 0.0015, 0.009),
          (1.31, 0.0065, 0.0014, 0.007), (1.50, 0.0055, 0.0013, 0.006)]
TIPTOP = (1.648, 0.0042, 0.0013)


def blank_radius(x):
    t = max(0.0, min(1.0, (x - BLANK_START) / (TIP_X - BLANK_START)))
    return BLANK_R0 + (BLANK_R1 - BLANK_R0) * (t ** 0.85)


def strut(mb, a, b, ru, rv, mat, w, up_hint=(0, 1, 0), n=8, exponent=3.0, mid_scale=None):
    """Rounded-rectangle bar from a to b (flat end caps). ru/rv are half sizes across the bar."""
    a, b = Vector(a), Vector(b)
    t, u, v = mk.frame_from(b - a, up_hint)
    rings = [mk.ring(a, u, v, ru, rv, n=n, exponent=exponent, mat=mat, w=w)]
    if mid_scale:
        rings.append(mk.ring((a + b) / 2, u, v, ru * mid_scale, rv * mid_scale, n=n, exponent=exponent, mat=mat, w=w))
    rings.append(mk.ring(b, u, v, ru, rv, n=n, exponent=exponent, mat=mat, w=w))
    return mb.loft(rings, cap_start="flat", cap_end="flat")


def build_handle(mb):
    """Butt cap, rear cork grip, reel seat with hoods, fore grip, winding check: one lathe along +X, 12 sides.
    Each profile entry: (x, radius, material of the band that starts at this ring)."""
    D, C = DARK, CORK
    profile = [
        (BUTT_X, 0.0, D),
        (-0.3225, 0.0110, D), (-0.3205, 0.0165, D), (-0.3160, 0.0180, D),     # rounded butt cap
        (-0.3000, 0.0180, D), (-0.3000, 0.0172, C),                           # cap face -> cork
        (-0.2600, 0.0181, C), (-0.1700, 0.0190, C), (-0.0600, 0.0188, C),     # rear grip, slight swell
        (0.0200, 0.0181, C), (0.0500, 0.0174, C),
        (0.0500, 0.0166, D), (0.0620, 0.0166, D), (0.0640, 0.0142, D),       # rear hood, seat
        (0.1160, 0.0142, D), (0.1180, 0.0166, D), (0.1300, 0.0166, D),       # front hood
        (0.1300, 0.0155, C), (0.1800, 0.0150, C), (0.2320, 0.0132, C),       # fore grip
        (0.2320, 0.0110, D), (0.2440, 0.0098, D),                             # winding check
        (0.2450, 0.0, D),
    ]
    mb.lathe(profile, origin=(0, 0, 0), axis=(1, 0, 0), ref_up=(0, 0, 1), n=12, w=W_HANDLE)


def build_blank(mb):
    rings = []
    for i in range(BLANK_RINGS):
        x = BLANK_START + (TIP_X - 0.002 - BLANK_START) * i / (BLANK_RINGS - 1)
        r = blank_radius(x)
        rings.append(mk.ring((x, 0, 0), (0, 1, 0), (0, 0, 1), r, r, n=8, mat=BLANK, w=W_BLANK, phase=math.pi / 8))
    mb.loft(rings, cap_start="flat", cap_end=Vector((TIP_X, 0, 0)))


def build_guides(mb):
    """Guide rings under the blank (ring plane perpendicular to the rod), each on one slanted foot.
    Returns the ring centers (line path)."""
    centers = []
    for x, major, minor, leg in GUIDES:
        rb = blank_radius(x)
        zc = -(rb + leg + major)
        c = Vector((x, 0.0, zc))
        mb.torus(c, (1, 0, 0), major, minor, n_major=10 if major > 0.01 else 8, n_minor=3, mat=TRIM, w=W_BLANK)
        foot_top = Vector((x - leg * 0.9, 0.0, -rb * 0.5))
        strut(mb, foot_top, c + Vector((0, 0, major + minor * 0.5)), 0.0014, 0.0022, DARK, W_BLANK,
              up_hint=(1, 0, 0), n=4, exponent=2.0)
        centers.append(c)
    x, major, minor = TIPTOP
    rb = blank_radius(x)
    c = Vector((x, 0.0, -(rb + major + minor * 0.5)))
    mb.torus(c, (1, 0, 0), major, minor, n_major=8, n_minor=3, mat=TRIM, w=W_BLANK)
    # tip-top tube over the blank end
    strut(mb, (x - 0.007, 0, 0), (TIP_X + 0.0015, 0, 0), rb * 1.3, rb * 1.3, DARK, W_BLANK,
          up_hint=(0, 0, 1), n=8, exponent=2.0)
    centers.append(c)
    return centers


def build_reel(mb):
    """Spinning reel under the seat: foot/stem, gearbox + rotor + spool (one lathe along +X), crank on +Y."""
    D, T = DARK, TRIM
    # foot + stem: flares into a foot plate along X where it meets the reel seat
    stem_rings = [
        mk.ring((0.091, 0, -0.0125), (0, 1, 0), (1, 0, 0), 0.0060, 0.0185, n=8, exponent=3.0, mat=D, w=W_HANDLE),
        mk.ring((0.090, 0, -0.0175), (0, 1, 0), (1, 0, 0), 0.0055, 0.0120, n=8, exponent=3.0, mat=D, w=W_HANDLE),
        mk.ring((0.088, 0, -0.0240), (0, 1, 0), (1, 0, 0), 0.0045, 0.0065, n=8, exponent=3.0, mat=D, w=W_HANDLE),
        mk.ring((0.084, 0, -0.0500), (0, 1, 0), (1, 0, 0), 0.0045, 0.0070, n=8, exponent=3.0, mat=D, w=W_HANDLE),
    ]
    mb.loft(stem_rings, cap_start="flat", cap_end="flat")
    profile = [
        (0.0470, 0.0, D),
        (0.0510, 0.0130, D), (0.0620, 0.0200, D), (0.0800, 0.0220, D), (0.0960, 0.0200, D),   # gearbox
        (0.1000, 0.0190, D), (0.1120, 0.0245, D),                                            # rotor cup
        (0.1165, 0.0235, D), (0.1195, 0.0235, D),                                            # spool rear lip
        (0.1195, 0.0200, T), (0.1460, 0.0205, D),                                            # line on the spool
        (0.1460, 0.0228, D), (0.1505, 0.0228, D), (0.1550, 0.0130, D),                       # front lip, nose
        (0.1575, 0.0, D),
    ]
    mb.lathe(profile, origin=(0, 0, REEL_Z), axis=(1, 0, 0), ref_up=(0, 0, 1), n=10, w=W_HANDLE)
    # right-side cover cap (-Y)
    mb.lathe([(0.017, 0.0095, D), (0.0235, 0.0090, D), (0.0250, 0.0, D)], origin=(CRANK_AXIS_X, 0, REEL_Z),
             axis=(0, -1, 0), ref_up=(0, 0, 1), n=10, w=W_HANDLE)
    # crank: boss on +Y, arm down/back, cork knob pointing out along +Y
    axle = Vector((CRANK_AXIS_X, 0.0, REEL_Z))
    mb.lathe([(0.017, 0.0078, D), (0.0300, 0.0072, D), (0.0310, 0.0, D)], origin=axle, axis=(0, 1, 0),
             ref_up=(0, 0, 1), n=8, w=W_CRANK)
    arm_dir = Vector((-0.55, 0.0, -0.835)).normalized()
    arm_a = axle + Vector((0, 0.0335, 0))
    arm_b = arm_a + arm_dir * 0.042
    strut(mb, arm_a - arm_dir * 0.004, arm_b + arm_dir * 0.004, 0.0024, 0.0048, D, W_CRANK, up_hint=(0, 0, 1),
          n=8, exponent=3.0)
    knob_base = arm_b + Vector((0, 0.002, 0))
    mb.lathe([(0.0, 0.0042, CORK), (0.004, 0.0068, CORK), (0.020, 0.0076, CORK), (0.027, 0.0062, CORK),
              (0.0285, 0.0, CORK)], origin=knob_base, axis=(0, 1, 0), ref_up=(0, 0, 1), n=8, w=W_CRANK)
    knob_center = knob_base + Vector((0, 0.014, 0))
    reel_line = Vector((0.1480, 0.0, REEL_Z + 0.0205))
    return {"crank_axis_point_m": [round(c, 4) for c in axle], "crank_axis_dir": [0, 1, 0],
            "knob_center": knob_center, "reel_line": reel_line}


def build():
    """Build SM_Rod_Basic (+ sockets). Returns (object, info dict)."""
    mats = [
        style.make_material("M_Rod_Cork", style.TROPICAL.ROPE, "wood"),
        style.make_material("M_Rod_Blank", "#3A2A20", "wet", roughness=0.35),
        style.make_material("M_Rod_Dark", style.FOGGY.ROCK, "flat", roughness=0.6),
        style.make_material("M_Rod_Trim", "#F5F1E6", "flat", roughness=0.6),
    ]
    mb = mk.MeshBuilder(groups=GROUPS)
    build_handle(mb)
    build_blank(mb)
    guide_centers = build_guides(mb)
    reel = build_reel(mb)
    obj = mb.to_object(ASSET, mats, sharp_angle_deg=40.0)
    mk.smart_uv(obj)
    line_tip = guide_centers[-1]
    pb.add_socket(obj, "LineTip", tuple(line_tip))
    pb.add_socket(obj, "ReelLine", tuple(reel["reel_line"]))
    pb.add_socket(obj, "CrankKnob", tuple(reel["knob_center"]))
    info = {
        "pivot": "grip point: center of the rear cork grip on the rod axis (right hand)",
        "forward_axis": "+X (tip)", "up_axis": "+Z (reel and guides hang below, -Z)", "crank_side": "+Y (left)",
        "tip_m": [round(c, 4) for c in line_tip],
        "tip_unreal_cm": [round(line_tip.x * 100, 2), round(-line_tip.y * 100, 2), round(line_tip.z * 100, 2)],
        "line_path_m": [[round(c, 4) for c in reel["reel_line"]]] + [[round(c, 4) for c in g] for g in guide_centers],
        "crank_axis_point_m": reel["crank_axis_point_m"], "crank_axis_dir": reel["crank_axis_dir"],
        "blank_rings": BLANK_RINGS, "blank_x_range_m": [BLANK_START, TIP_X],
        "vertex_groups": GROUPS,
    }
    return obj, info


# ---------------------------------------------------------------------------------------------------
# Preview-only staging (never exported)
# ---------------------------------------------------------------------------------------------------
def stage_first_person(obj):
    """The rod as the player might hold it: grip at the right hand (0.45, -0.17, -0.24) m from the eye, tip raised
    35 deg and turned 10 deg left, over turquoise water 2.3 m below the eye (dock)."""
    water_mat = style.make_material("PV_Water", style.TROPICAL.SHALLOW_WATER, "water")
    bpy.ops.mesh.primitive_plane_add(size=400.0, location=(0.0, 0.0, -2.3))
    water = bpy.context.active_object
    water.data.materials.append(water_mat)
    inst = bpy.data.objects.new("PV_Rod", obj.data)
    bpy.context.scene.collection.objects.link(inst)
    inst.location = (0.45, -0.17, -0.24)
    inst.rotation_euler = (0.0, math.radians(-35.0), math.radians(10.0))
    obj.hide_render = True

    def cleanup():
        obj.hide_render = False
        for h in (water, inst):
            bpy.data.objects.remove(h, do_unlink=True)
    return cleanup


def main():
    args = pb.parse_args(ASSET, "Props")
    pb.reset_scene()
    obj, info = build()
    tris = pb.triangle_count([obj])
    ok, budget = style.check_budget(tris, BUDGET_KIND)
    views = [
        {"name": "side", "location": (0.67, -3.0, -0.03), "target": (0.67, 0.0, -0.03), "ortho_scale": 2.05,
         "resolution": (1536, 384)},
        {"name": "handle", "location": (-0.42, 0.30, 0.20), "target": (0.06, 0.0, -0.035), "lens": 35.0},
        {"name": "reel", "location": (0.08, 0.36, -0.10), "target": (0.09, 0.0, -0.05), "lens": 50.0},
        {"name": "tip", "location": (1.50, -0.14, 0.05), "target": (1.62, 0.0, -0.004), "lens": 50.0},
        {"name": "fp", "location": (0.0, 0.0, 0.0), "target": (1.0, 0.0, 0.0), "lens": pb.lens_for_hfov(90.0),
         "resolution": (1280, 720), "world_rgb": style.linear(style.TROPICAL.SKY_DAY), "view_transform": "Standard",
         "setup": lambda: stage_first_person(obj)},
    ]
    info.update({"budget_ok": ok, "budget": budget, "topology": mk.mesh_stats(obj),
                 "intended_length_m": round(TIP_X - BUTT_X, 3)})
    pb.finish(args, [obj], views=views, extra=info)


if __name__ == "__main__":
    main()

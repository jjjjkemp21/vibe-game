"""SM_Bobber: chunky red-and-white round bobber (placeholder, T-006). Pivot at the WATERLINE center.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_bobber.py
Export: art/export/Props/SM_Bobber.fbx     Preview: Saved/AgentLogs/previews/SM_Bobber.png (contact sheet)

Shape: one closed surface of revolution around Blender Z (16 sides): a 5 cm ball (red top half, off-white bottom
half), a red stem with a white push-button on top (the line clip) and a short white peg underneath (hook line).
Real size: 5 cm wide, about 7.7 cm tall.

Axes and pivot (Blender 1 unit = 1 m; Unreal = Blender * 100 cm with Y negated):
- Origin (0, 0, 0) = the waterline on the vertical axis: the object floats with its origin ON the water surface.
  The red/white seam (ball equator) sits 5 mm above the waterline, so a thin white band shows above the water
  under the red dome (the readable "red over white" look of the mood boards).
- Z up; rotationally symmetric, so there is no forward axis.
- Sockets (FBX empties, Unreal makes mesh sockets): LineAttach = top of the button (0, 0, 0.049) m, where the
  line from the rod tip attaches; HookLine = bottom of the peg (0, 0, -0.0275) m, where the hook line hangs.

Readability: at real size the ball is ~3 px wide at 15 m on a 1080p screen with a 90 deg FOV (see the
"readability" view in the preview). Gameplay should scale it up (about 3x, or scale with distance) and can add
a rim/emissive highlight; the mesh itself stays real-size so hand-held and close-up shots look right.

Budget: small prop <= 500 triangles (this asset's brief). Materials: M_Bobber_Red (accent #FF4D3D, wet),
M_Bobber_White (#F5F1E6, wet).
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

ASSET = "SM_Bobber"
BUDGET = 500
SIDES = 16

BALL_R = 0.025          # 5 cm ball
BALL_Z = 0.005          # ball center (= red/white seam) above the waterline
STEM_R = 0.0055
STEM_TOP = 0.041
BUTTON_R = 0.0088
BUTTON_TOP = 0.049
PEG_R = 0.0036
PEG_BOTTOM = -0.0275
WHITE_HEX = "#F5F1E6"   # same off-white as the mood-board bobber and line


def ball_point(theta_deg):
    """(z, r) on the ball at polar angle theta (0 = top)."""
    t = math.radians(theta_deg)
    return BALL_Z + BALL_R * math.cos(t), BALL_R * math.sin(t)


def build():
    """Build SM_Bobber and its sockets; returns the mesh object (usable by other recipes)."""
    red = style.make_material("M_Bobber_Red", style.TROPICAL.ACCENT, "wet", roughness=0.3)
    white = style.make_material("M_Bobber_White", WHITE_HEX, "wet", roughness=0.35)
    RED, WHITE = 0, 1

    peg_join = 180.0 - math.degrees(math.asin(PEG_R / BALL_R))
    stem_join = math.degrees(math.asin(STEM_R / BALL_R))
    profile = [
        (PEG_BOTTOM, 0.0, WHITE),                 # pole: peg tip (fan)
        (PEG_BOTTOM + 0.0007, PEG_R * 0.83, WHITE),  # small chamfer on the peg end
    ]
    # ball: bottom half white, top half red; 8 bands of about 20 degrees, a ring exactly on the seam
    for th in (peg_join, 150.0, 130.0, 110.0):
        z, r = ball_point(th)
        profile.append((z, r, WHITE))
    for th in (90.0, 70.0, 50.0, 30.0):
        z, r = ball_point(th)
        profile.append((z, r, RED))
    z, r = ball_point(stem_join)
    profile += [
        (z, r, RED),                              # stem leaves the ball
        (STEM_TOP, STEM_R, WHITE),                # button underside (flat annulus band)
        (STEM_TOP, BUTTON_R, WHITE),
        (BUTTON_TOP - 0.0015, BUTTON_R, WHITE),   # button side
        (BUTTON_TOP, BUTTON_R * 0.78, WHITE),     # chamfer ~2.5% of the button
        (BUTTON_TOP + 0.0004, 0.0, WHITE),        # pole: slightly domed top (fan)
    ]
    mb = mk.MeshBuilder()
    mb.lathe(profile, origin=(0, 0, 0), axis=(0, 0, 1), ref_up=(1, 0, 0), n=SIDES)
    obj = mb.to_object(ASSET, [red, white], sharp_angle_deg=50.0)
    mk.smart_uv(obj)
    pb.add_socket(obj, "LineAttach", (0.0, 0.0, BUTTON_TOP))
    pb.add_socket(obj, "HookLine", (0.0, 0.0, PEG_BOTTOM))
    return obj


# ---------------------------------------------------------------------------------------------------
# Preview-only staging (never exported)
# ---------------------------------------------------------------------------------------------------
def stage_waterline_marker():
    """Thin turquoise slab at z = 0 so the side view shows where the water surface cuts the bobber."""
    mat = style.make_material("PV_Water", style.TROPICAL.SHALLOW_WATER, "water")
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, -0.0006))
    slab = bpy.context.active_object
    slab.scale = (0.11, 0.004, 0.0012)
    slab.data.materials.append(mat)

    def cleanup():
        bpy.data.objects.remove(slab, do_unlink=True)
    return cleanup


def stage_readability(obj):
    """First-person shot from a dock (eye 2.3 m above the water): bobbers at 10, 15 and 20 m at real size (left
    column) and at 3x (right column) on flat turquoise water. Rendered at the pixel density of a 1920x1080 screen
    with a 90 deg horizontal FOV (768 px across 36 deg), so the dots are as big as they will be in game."""
    mat = style.make_material("PV_Water", style.TROPICAL.SHALLOW_WATER, "water")
    bpy.ops.mesh.primitive_plane_add(size=200.0, location=(0.0, 0.0, 0.0))
    water = bpy.context.active_object
    water.data.materials.append(mat)
    helpers = [water]
    for i, d in enumerate((10.0, 15.0, 20.0)):
        for scale, side in ((1.0, 1.0), (3.0, -1.0)):
            inst = bpy.data.objects.new("PV_Bobber_%d_%g" % (i, scale), obj.data)
            bpy.context.scene.collection.objects.link(inst)
            inst.location = (d, side * 0.09 * d, 0.0)
            inst.scale = (scale, scale, scale)
            helpers.append(inst)

    def cleanup():
        for h in helpers:
            bpy.data.objects.remove(h, do_unlink=True)
    return cleanup


def main():
    args = pb.parse_args(ASSET, "Props")
    pb.reset_scene()
    obj = build()
    tris = pb.triangle_count([obj])
    ok, budget = tris <= BUDGET, BUDGET
    z_mid = (PEG_BOTTOM + BUTTON_TOP) / 2
    views = [
        {"name": "side", "location": (0.0, -1.0, z_mid), "target": (0.0, 0.0, z_mid), "ortho_scale": 0.12,
         "setup": stage_waterline_marker},
        {"name": "top", "location": (0.0, 0.0, 1.0), "rotation": (0.0, 0.0, 0.0), "ortho_scale": 0.09},
        {"name": "readability", "location": (0.0, 0.0, 2.3), "target": (15.0, 0.0, 0.0),
         "lens": pb.lens_for_hfov(36.0), "resolution": (768, 432), "world_rgb": style.linear(style.TROPICAL.SKY_DAY),
         "light": "FLAT", "view_transform": "Standard", "setup": lambda: stage_readability(obj)},
    ]
    pb.finish(args, [obj], views=views, extra={
        "intended_size_m": [0.05, 0.05, round(BUTTON_TOP - PEG_BOTTOM, 4)],
        "pivot": "waterline center (0,0,0); ball seam 5 mm above the waterline",
        "budget_ok": ok, "budget": budget,
        "topology": mk.mesh_stats(obj),
        "readability_note": "real size ~3 px at 15 m (1080p, 90 deg FOV): scale ~3x in gameplay",
    })


if __name__ == "__main__":
    main()

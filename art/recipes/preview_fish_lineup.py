"""Preview only (no export): every fishkit species side by side for scale and readability (T-008).

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/preview_fish_lineup.py
Preview: Saved/AgentLogs/previews/Fish_Lineup.png (contact sheet):
  1. scale_side: orthographic side view, the fish at scale 1 (their ReferenceWeight size) beside the 1 m crate
     (SM_GoldenCrate size) on sand
  2. scale_34: the same from a 3/4 camera
  3. 5m_side: all fish side-on at 5 m, palette colors (top row) and black silhouettes (bottom row), on sky, at the
     game's pixel density (1920 px over a 90 deg FOV)
  4. 5m_water: all fish at the surface 5 m from a dock (eye 2.3 m above the water), broadside and turned 45 deg
Add a new species by appending its recipe module name to RECIPES (it must expose build() -> (obj, info)).
"""
import importlib.util
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "lib"))

import bpy  # noqa: E402
from mathutils import Matrix  # noqa: E402

import fishkit as fk  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

RECIPES = ["sm_fish_bonefish", "sm_fish_coralsnapper"]
OUT = pb.PREVIEW_ROOT / "Fish_Lineup.png"


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def stage_scale(objs):
    """1 m crate at the origin on sand; fish resting (lowest point on the ground) left and right of it."""
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))
    crate = bpy.context.active_object
    crate.name = "PV_Crate"
    crate.data.transform(Matrix.Translation((0.0, 0.0, 0.5)))
    bev = crate.modifiers.new(name="Bevel", type="BEVEL")
    bev.width, bev.segments = 0.03, 2
    crate.data.materials.append(style.make_material("PV_Crate", style.TROPICAL.WEATHERED_WOOD, "wood"))
    placements = []
    x_left, x_right = -0.55, 0.55
    for i, o in enumerate(objs):
        mn, mx = pb.world_bounds([o])
        half = (mx.x - mn.x) / 2.0
        if i % 2 == 0:
            x = x_left - 0.08 - half
            x_left = x - half
        else:
            x = x_right + 0.08 + half
            x_right = x + half
        placements.append((i, (x, 0.0, -mn.z), (0.0, 0.0, 0.0), False))
    cleanup = fk._stage(objs, placements, ground_hex=style.TROPICAL.SAND)

    def full_cleanup():
        cleanup()
        bpy.data.objects.remove(crate, do_unlink=True)
    return full_cleanup


def main():
    pb.reset_scene()
    fk.preview_setup()
    fish = {}
    objs = []
    for r in RECIPES:
        obj, info = load(r).build()
        objs.append(obj)
        fish[obj.name] = {"dimensions_m": [round(v, 4) for v in obj.dimensions], "length_m": info["length_m"],
                          "reference_weight_kg": info["reference_weight_kg"],
                          "implied_body_weight_kg": info["implied_body_weight_kg"],
                          "triangles": pb.triangle_count([obj]), "identifying_feature": info["identifying_feature"]}
    views = [
        {"name": "scale_side", "location": (0.0, -6.0, 0.5), "target": (0.0, 0.0, 0.5), "ortho_scale": 3.0,
         "resolution": (768, 432), "setup": lambda: stage_scale(objs)},
        {"name": "scale_34", "location": (0.35, -3.6, 1.35), "target": (0.0, 0.0, 0.3), "lens": 30.0,
         "resolution": (768, 432), "setup": lambda: stage_scale(objs)},
        fk.view_5m_side(objs),
        fk.view_5m_water(objs),
    ]
    paths = []
    for v in views:
        v = dict(v)
        name = v.pop("name")
        cleanup = v.pop("setup")()
        paths.append(pb.render_view(OUT.with_name(OUT.stem + "_" + name + OUT.suffix), **v))
        cleanup()
    pb.contact_sheet(paths, OUT, cols=2, cell=(768, 432))
    print("RESULT_JSON:" + json.dumps({"asset": "preview_fish_lineup", "preview": str(OUT), "views": paths,
                                       "fish": fish}))


if __name__ == "__main__":
    main()

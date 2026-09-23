"""SM_GoldenCrate: 1 m beveled crate used by the setup golden-path test. Pivot at bottom center.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_golden_crate.py
Expected in Unreal after import: box extent ~ (50, 50, 50) uu.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Matrix  # noqa: E402

import pipeline_blender as pb  # noqa: E402

ASSET = "SM_GoldenCrate"
args = pb.parse_args(ASSET, "Props")
pb.reset_scene()

bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))
crate = bpy.context.active_object
crate.name = ASSET
crate.data.name = ASSET
crate.data.transform(Matrix.Translation((0.0, 0.0, 0.5)))  # cube bottom sits on the origin

bevel = crate.modifiers.new(name="Bevel", type="BEVEL")
bevel.width = 0.03
bevel.segments = 2

crate.data.materials.append(pb.make_material("M_GoldenCrate", (0.55, 0.35, 0.15)))

pb.finish(args, [crate], extra={"intended_size_m": [1.0, 1.0, 1.0]})

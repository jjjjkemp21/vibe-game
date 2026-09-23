"""Shared helpers for Blender asset recipes (Blender 5.1+, 5.2 LTS expected).

Recipes run headless through tools/blender-run.ps1:
    blender --background --factory-startup --python-exit-code 1 --python <recipe> -- [--out X] [--preview Y] [--save-blend]

Conventions: metric, 1 Blender unit = 1 m (= 100 Unreal units), Z up, prop pivot at bottom center,
object + mesh named with the Unreal asset name (SM_...), materials named M_...
"""
import argparse
import json
import sys
from pathlib import Path

import bpy
from mathutils import Vector

REPO_ROOT = Path(__file__).resolve().parents[2]
EXPORT_ROOT = REPO_ROOT / "art" / "export"
PREVIEW_ROOT = REPO_ROOT / "Saved" / "AgentLogs" / "previews"
BLEND_ROOT = REPO_ROOT / "art" / "blend"


def parse_args(asset_name, category):
    """Parse recipe arguments (everything after '--')."""
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description="Blender asset recipe: " + asset_name)
    parser.add_argument("--out", default=str(EXPORT_ROOT / category / (asset_name + ".fbx")))
    parser.add_argument("--preview", default=str(PREVIEW_ROOT / (asset_name + ".png")))
    parser.add_argument("--save-blend", action="store_true")
    args = parser.parse_args(argv)
    args.asset_name = asset_name
    args.category = category
    return args


def reset_scene():
    """Remove all objects and unused data so the recipe starts from a clean, metric scene."""
    obj = bpy.context.object
    if obj is not None and obj.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.cameras, bpy.data.lights):
        for block in list(coll):
            if block.users == 0:
                coll.remove(block)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0


def make_material(name, rgb, roughness=0.6, metallic=0.0):
    """Simple PBR material. Sets the viewport color too (used by Workbench previews)."""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
    if hasattr(mat, "use_nodes") and not mat.use_nodes:
        mat.use_nodes = True
    tree = getattr(mat, "node_tree", None)
    bsdf = tree.nodes.get("Principled BSDF") if tree else None
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["Metallic"].default_value = metallic
    return mat


def ensure_fbx_exporter():
    try:
        bpy.ops.export_scene.fbx.get_rna_type()
    except Exception:
        import addon_utils
        addon_utils.enable("io_scene_fbx", default_set=True)


def select_only(objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


def export_fbx(objects, out_path):
    """Export objects as one FBX that Unreal imports at the right scale (1 m cube -> 50 uu box extent)."""
    ensure_fbx_exporter()
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    select_only(objects)
    bpy.ops.export_scene.fbx(
        filepath=str(out_path),
        use_selection=True,
        object_types={"MESH"},
        apply_unit_scale=True,
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        add_leaf_bones=False,
        bake_anim=False,
    )
    return str(out_path)


def world_bounds(objects):
    pts = []
    for o in objects:
        pts.extend(o.matrix_world @ Vector(corner) for corner in o.bound_box)
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return mn, mx


def render_preview(objects, out_path, resolution=768):
    """Quick Workbench render from a 3/4 view so the agent can LOOK at the result."""
    scene = bpy.context.scene
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    mn, mx = world_bounds(objects)
    center = (mn + mx) / 2.0
    size = max((mx - mn).length, 0.01)

    cam_data = bpy.data.cameras.new("PreviewCam")
    cam = bpy.data.objects.new("PreviewCam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = center + Vector((size * 1.1, -size * 1.4, size * 0.9))
    cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam

    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(out_path)
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    try:
        shading.show_cavity = True
    except Exception:
        pass
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)
    return str(out_path)


def triangle_count(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    tris = 0
    for o in objects:
        if o.type != "MESH":
            continue
        eval_obj = o.evaluated_get(depsgraph)
        mesh = eval_obj.to_mesh()
        mesh.calc_loop_triangles()
        tris += len(mesh.loop_triangles)
        eval_obj.to_mesh_clear()
    return tris


def report(args, objects, extra=None):
    """Print the RESULT_JSON line that tools/blender-run.ps1 captures."""
    mn, mx = world_bounds(objects)
    size = mx - mn
    data = {
        "asset": args.asset_name,
        "category": args.category,
        "export": args.out,
        "preview": args.preview,
        "dimensions_m": [round(size.x, 4), round(size.y, 4), round(size.z, 4)],
        "min_m": [round(mn.x, 4), round(mn.y, 4), round(mn.z, 4)],
        "triangles": triangle_count(objects),
        "materials": sorted({s.material.name for o in objects for s in o.material_slots if s.material}),
        "blender": bpy.app.version_string,
    }
    if extra:
        data.update(extra)
    print("RESULT_JSON:" + json.dumps(data))
    return data


def finish(args, objects, extra=None):
    """Standard recipe ending: export FBX, render preview, optionally save .blend, print RESULT_JSON."""
    export_fbx(objects, args.out)
    render_preview(objects, args.preview)
    if args.save_blend:
        BLEND_ROOT.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_ROOT / (args.asset_name + ".blend")))
    return report(args, objects, extra)

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


SOCKET_PREFIX = "SOCKET_"


def add_socket(parent, name, location, rotation=(0.0, 0.0, 0.0), size=0.02):
    """Unreal static-mesh socket: an empty named SOCKET_<name> parented to `parent` (location/rotation in the
    parent's local space, meters/radians). Unreal's FBX import turns it into a mesh socket named <name>.
    export_fbx exports these automatically with their parent; report() lists them in RESULT_JSON."""
    empty = bpy.data.objects.new(SOCKET_PREFIX + name, None)
    empty.empty_display_type = "ARROWS"
    empty.empty_display_size = size
    bpy.context.scene.collection.objects.link(empty)
    empty.parent = parent
    empty.location = location
    empty.rotation_euler = rotation
    return empty


def socket_children(objects):
    return [c for o in objects for c in o.children if c.type == "EMPTY" and c.name.startswith(SOCKET_PREFIX)]


def export_fbx(objects, out_path):
    """Export objects as one FBX that Unreal imports at the right scale (1 m cube -> 50 uu box extent).
    SOCKET_ empties parented to the objects are exported too (Unreal makes them mesh sockets)."""
    ensure_fbx_exporter()
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    sockets = socket_children(objects)
    select_only(list(objects) + sockets)
    bpy.ops.export_scene.fbx(
        filepath=str(out_path),
        use_selection=True,
        object_types={"MESH", "EMPTY"} if sockets else {"MESH"},
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


def lens_for_hfov(hfov_deg):
    """Focal length (mm, 36 mm sensor) for a horizontal field of view."""
    import math
    return 18.0 / math.tan(math.radians(hfov_deg) / 2.0)


def render_view(out_path, location, target=None, lens=50.0, ortho_scale=None, resolution=(768, 768), world_rgb=None,
                clip_start=0.01, rotation=None, light="STUDIO", view_transform=None):
    """Extra Workbench view from `location` looking at `target` (world meters), or with an explicit Euler
    `rotation` (radians). ortho_scale -> orthographic. lens is the focal length in mm on a 36 mm sensor
    (horizontal FOV 90 deg = lens 18, see lens_for_hfov). world_rgb (linear) sets the background color for this
    render only. light: STUDIO (shaded) or FLAT (pure palette colors, use with view_transform="Standard")."""
    scene = bpy.context.scene
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cam_data = bpy.data.cameras.new("ViewCam")
    cam_data.sensor_fit = "HORIZONTAL"
    cam_data.sensor_width = 36.0
    cam_data.clip_start = clip_start
    cam_data.clip_end = 1000.0
    if ortho_scale:
        cam_data.type = "ORTHO"
        cam_data.ortho_scale = ortho_scale
    else:
        cam_data.lens = lens
    cam = bpy.data.objects.new("ViewCam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = Vector(location)
    if rotation is not None:
        cam.rotation_euler = rotation
    else:
        cam.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x, scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(out_path)
    shading = scene.display.shading
    shading.light = light
    shading.color_type = "MATERIAL"
    try:
        shading.show_cavity = light != "FLAT"
    except Exception:
        pass
    old_world = None
    if world_rgb is not None:
        if scene.world is None:
            scene.world = bpy.data.worlds.new("PreviewWorld")
        old_world = tuple(scene.world.color)
        scene.world.color = world_rgb
    old_vt = scene.view_settings.view_transform
    if view_transform:
        scene.view_settings.view_transform = view_transform
    bpy.ops.render.render(write_still=True)
    scene.view_settings.view_transform = old_vt
    shading.light = "STUDIO"
    if old_world is not None:
        scene.world.color = old_world
    bpy.data.objects.remove(cam, do_unlink=True)
    return str(out_path)


def contact_sheet(paths, out_path, cols=2, cell=(768, 768), bg=(0.12, 0.12, 0.12)):
    """Paste images into a grid (each scaled to fit its cell, aspect kept). Returns out_path."""
    import numpy as np
    cw, ch = cell
    rows = (len(paths) + cols - 1) // cols
    sheet = np.empty((rows * ch, cols * cw, 4), dtype=np.float32)
    sheet[:] = (bg[0], bg[1], bg[2], 1.0)
    for i, p in enumerate(paths):
        img = bpy.data.images.load(str(p))
        w, h = img.size
        k = min(cw / w, ch / h)
        nw, nh = max(1, int(w * k)), max(1, int(h * k))
        img.scale(nw, nh)
        px = np.empty(nw * nh * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        px = px.reshape(nh, nw, 4)
        col, row = i % cols, rows - 1 - i // cols  # image rows start at the bottom
        x0 = col * cw + (cw - nw) // 2
        y0 = row * ch + (ch - nh) // 2
        sheet[y0:y0 + nh, x0:x0 + nw] = px
        bpy.data.images.remove(img)
    out_img = bpy.data.images.new("ContactSheet", cols * cw, rows * ch, alpha=False)
    out_img.pixels.foreach_set(sheet.ravel())
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_img.filepath_raw = str(out_path)
    out_img.file_format = "PNG"
    out_img.save()
    bpy.data.images.remove(out_img)
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
    sockets = socket_children(objects)
    if sockets:
        # Blender (x, y, z) m -> Unreal (x, -y, z) * 100 cm with the default FBX import (Force Front X Axis off).
        data["sockets"] = {
            s.name[len(SOCKET_PREFIX):]: {
                "blender_m": [round(c, 4) for c in s.location],
                "unreal_cm": [round(s.location.x * 100, 2), round(-s.location.y * 100, 2), round(s.location.z * 100, 2)],
            } for s in sockets}
    if extra:
        data.update(extra)
    print("RESULT_JSON:" + json.dumps(data))
    return data


def finish(args, objects, extra=None, views=None):
    """Standard recipe ending: export FBX, render preview, optionally save .blend, print RESULT_JSON.

    views: optional list of dicts with render_view() keyword arguments plus "name". Each view is rendered to
    <preview stem>_<name>.png and the main preview becomes a contact sheet: the default 3/4 view first, then the
    views in order (left to right, top to bottom, 2 columns). A view {"name": ..., "image": path} adds an image the
    recipe rendered itself (e.g. fp_preview.render_fp EEVEE frames) to the sheet as is."""
    export_fbx(objects, args.out)
    render_preview(objects, args.preview)
    if views:
        base = Path(args.preview)
        paths = [str(base.with_name(base.stem + "_34" + base.suffix))]
        Path(args.preview).replace(paths[0])
        for v in views:
            v = dict(v)
            name = v.pop("name")
            if "image" in v:
                paths.append(str(v["image"]))
                continue
            setup = v.pop("setup", None)  # optional callable staging preview-only helpers; returns a cleanup callable
            cleanup = setup() if setup else None
            paths.append(render_view(base.with_name(base.stem + "_" + name + base.suffix), **v))
            if cleanup:
                cleanup()
        contact_sheet(paths, args.preview, cols=2)
    if args.save_blend:
        BLEND_ROOT.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_ROOT / (args.asset_name + ".blend")))
    return report(args, objects, extra)

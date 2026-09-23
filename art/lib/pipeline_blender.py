"""Shared helpers for Blender asset recipes (Blender 5.1+, 5.2 LTS expected).

Recipes run headless through tools/blender-run.ps1:
    blender --background --factory-startup --python-exit-code 1 --python <recipe> -- [--out X] [--preview Y] [--save-blend]

Conventions: metric, 1 Blender unit = 1 m (= 100 Unreal units), Z up, prop pivot at bottom center,
object + mesh named with the Unreal asset name (SM_...), materials named M_...
Static meshes: export_fbx(). Rigged/animated assets (armature + skinned meshes + clips): export_skeletal_fbx(), which
writes centimeter FBX files with no scale on any bone (see the notes above SKELETAL_FBX_SETTINGS).
"""
import argparse
import json
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

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


# ---------------------------------------------------------------------------------------------------
# Skeletal export (armature + skinned meshes + baked actions), in CENTIMETERS
# ---------------------------------------------------------------------------------------------------
# Why: a meter FBX (UnitScaleFactor 100) makes Unreal's "Convert Scene Unit" put a local scale of 100 on the `root`
# bone while the child bones keep meter translations (found on SK_FPArms, T-004). Everything attached to a bone then
# inherits 100x. So skeletal files are written the way Unreal wants them: header UnitScaleFactor = 1.0 (cm), bone
# and mesh data in centimeter values, and no scale on any node (armature, mesh, bones, animation curves).
#
# How (recipes keep authoring in meters; nothing in the scene changes):
# 1. export_skeletal_fbx() makes temporary copies of the armature, its skinned meshes and the actions its NLA strips
#    and active action use, scaled x100 about the origin: bone rest heads/tails and vertices x100 (Armature/Mesh
#    .transform), object locations x100, pose-bone and object `location` keys x100. Rotations, rolls and scale keys are
#    untouched. The copies carry the originals' exact names (the originals are renamed for the moment).
# 2. The scene unit scale is set to 0.01 (1 Blender unit = 1 cm) for the export, with
#    apply_scale_options="FBX_SCALE_NONE" ("All Local"): FBX UnitScaleFactor is written as exactly 1.0, and the unit
#    factor on the object transforms is 100 * 0.01 = 1 (0.99999998 in doubles; the exporter's float32 transform matrix
#    rounds it to exactly 1.0, checked below). FBX_SCALE_ALL/UNITS would instead write UnitScaleFactor 0.99999998.
# 3. The copies are deleted, names and scene units restored, and the written file is checked with fbx_scale_report():
#    UnitScaleFactor == 1.0 and every Model node (Lcl Scaling) and every animated scale key within 1e-5 of 1.0.
# Unreal import: Convert Scene ON (axis), Convert Scene Unit OFF (the file is cm; ON is an exact no-op too), Force
# Front X Axis OFF, uniform scale 1.0.
SKELETAL_FBX_CM_PER_UNIT = 100.0
SKELETAL_FBX_SETTINGS = dict(
    apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0,
    axis_forward="-Z", axis_up="Y", use_mesh_modifiers=True, mesh_smooth_type="FACE",
    add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X",
    use_armature_deform_only=False, armature_nodetype="NULL",
)
SKELETAL_FBX_BAKE_SETTINGS = dict(
    bake_anim_use_all_bones=True, bake_anim_use_nla_strips=True, bake_anim_use_all_actions=False,
    bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
)
# Modifiers whose result does not depend on the mesh's size (anything else must be applied before a skeletal export)
_SCALE_FREE_MODIFIERS = {"ARMATURE", "TRIANGULATE", "EDGE_SPLIT", "WEIGHTED_NORMAL", "SUBSURF"}
_SCALE_TOLERANCE = 1e-5


def _is_location_path(data_path):
    return data_path == "location" or data_path.endswith(".location")


def _scaled_action_copy(action, k):
    """Copy of `action` with every `location` F-curve (object or pose bone) multiplied by k."""
    new = action.copy()
    for layer in new.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for fc in bag.fcurves:
                    if not _is_location_path(fc.data_path):
                        continue
                    for kp in fc.keyframe_points:
                        kp.co.y *= k
                        kp.handle_left.y *= k
                        kp.handle_right.y *= k
    return new


def _slot(action, identifier):
    return next(s for s in action.slots if s.identifier == identifier)


def _check_unit_scale(obj):
    s = obj.matrix_basis.to_scale()
    if max(abs(c - 1.0) for c in s) > _SCALE_TOLERANCE:
        raise ValueError("%s has object scale %s: apply scale before a skeletal export" % (obj.name, tuple(s)))


def export_skeletal_fbx(out_path, armature, meshes=(), bake_anim=False, **overrides):
    """Export `armature` (+ its skinned `meshes`) as one FBX in centimeters with no scale on any node (see the notes
    above SKELETAL_FBX_SETTINGS). bake_anim=True writes one take per unmuted NLA strip (take name = strip name), as
    Unreal wants for one clip per file: mute every other track before calling. overrides: extra/changed exporter
    keywords. The scene is left exactly as it was. Returns fbx_scale_report(out_path) (raises if the file is not cm or
    any node or scale key is off 1.0)."""
    ensure_fbx_exporter()
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if armature.type != "ARMATURE":
        raise ValueError(armature.name + " is not an armature")
    meshes = list(meshes)
    for o in [armature] + meshes:
        _check_unit_scale(o)
    for m in meshes:
        bad = [md.name for md in m.modifiers if md.type not in _SCALE_FREE_MODIFIERS]
        if bad:
            raise ValueError("%s: apply modifiers %s before a skeletal export (they depend on size)" % (m.name, bad))

    k = SKELETAL_FBX_CM_PER_UNIT
    S, Si = Matrix.Scale(k, 4), Matrix.Scale(1.0 / k, 4)
    scene = bpy.context.scene
    units = (scene.unit_settings.system, scene.unit_settings.scale_length)
    ad = armature.animation_data
    actions = []
    if ad is not None:
        if ad.action is not None:
            actions.append(ad.action)
        actions += [s.action for t in ad.nla_tracks for s in t.strips if s.action is not None]
    actions = list(dict.fromkeys(actions))
    originals = list(dict.fromkeys([armature, armature.data] + meshes + [m.data for m in meshes] + actions))
    names = {id_: id_.name for id_ in originals}
    made = []
    try:
        for id_ in originals:
            id_.name = names[id_] + "~m"
        act_map = {}
        for a in actions:
            c = _scaled_action_copy(a, k)
            c.name = names[a]
            act_map[a] = c
            made.append(c)
        arm_c = armature.copy()
        arm_c.name = names[armature]
        made.append(arm_c)
        arm_c.data = armature.data.copy()
        arm_c.data.name = names[armature.data]
        made.append(arm_c.data)
        scene.collection.objects.link(arm_c)
        arm_c.data.transform(S)
        arm_c.matrix_parent_inverse = S @ armature.matrix_parent_inverse @ Si
        arm_c.matrix_basis = S @ armature.matrix_basis @ Si
        cad = arm_c.animation_data
        if cad is not None:
            if cad.action is not None:
                slot = cad.action_slot.identifier if cad.action_slot else None
                cad.action = act_map[cad.action]
                if slot:
                    cad.action_slot = _slot(cad.action, slot)
            for t in cad.nla_tracks:
                for s in t.strips:
                    if s.action is None:
                        continue
                    keep = (s.frame_start, s.frame_end, s.action_frame_start, s.action_frame_end,
                            s.action_slot.identifier if s.action_slot else None)
                    s.action = act_map[s.action]
                    if keep[4]:
                        s.action_slot = _slot(s.action, keep[4])
                    if (s.frame_start, s.frame_end, s.action_frame_start, s.action_frame_end) != keep[:4]:
                        raise RuntimeError("NLA strip %s changed its range when its action was swapped" % s.name)
        objs = [arm_c]
        for m in meshes:
            mc = m.copy()
            mc.name = names[m]
            made.append(mc)
            mc.data = m.data.copy()
            mc.data.name = names[m.data]
            made.append(mc.data)
            scene.collection.objects.link(mc)
            mc.data.transform(S, shape_keys=True)
            if m.parent == armature:
                mc.parent = arm_c
            mc.matrix_parent_inverse = S @ m.matrix_parent_inverse @ Si
            mc.matrix_basis = S @ m.matrix_basis @ Si
            for md in mc.modifiers:
                if md.type == "ARMATURE" and md.object == armature:
                    md.object = arm_c
            objs.append(mc)
        scene.unit_settings.system = "METRIC"
        scene.unit_settings.scale_length = 1.0 / k
        bpy.context.view_layer.update()
        select_only(objs)
        kw = dict(SKELETAL_FBX_SETTINGS)
        kw.update(filepath=str(out_path), use_selection=True, object_types={o.type for o in objs}, bake_anim=bake_anim)
        if bake_anim:
            kw.update(SKELETAL_FBX_BAKE_SETTINGS)
        kw.update(overrides)
        bpy.ops.export_scene.fbx(**kw)
    finally:
        scene.unit_settings.system, scene.unit_settings.scale_length = units
        for id_ in made:
            if isinstance(id_, bpy.types.Object):
                bpy.data.objects.remove(id_, do_unlink=True)
        for id_ in made:
            if isinstance(id_, bpy.types.Armature):
                bpy.data.armatures.remove(id_)
            elif isinstance(id_, bpy.types.Mesh):
                bpy.data.meshes.remove(id_)
            elif isinstance(id_, bpy.types.Action):
                bpy.data.actions.remove(id_)
        for id_ in originals:
            id_.name = names[id_]
        bpy.context.view_layer.update()
    report_ = fbx_scale_report(out_path)
    if not report_["ok"]:
        raise RuntimeError("Skeletal FBX is not clean cm / scale 1: %s" % json.dumps(report_))
    return report_


def fbx_scale_report(path):
    """Read an FBX file (Blender's own parser) and report what Unreal will see for units and scale:
    unit_scale_factor (1.0 = cm), the largest deviation from 1.0 of any Model node's Lcl Scaling (bones, armature,
    meshes) and of any animated scale key (S curves), the largest |translation| of any node (cm magnitudes vs m),
    and the take names. ok = cm header and every scale within 1e-5 of 1.0."""
    ensure_fbx_exporter()
    from io_scene_fbx import parse_fbx

    root, _version = parse_fbx.parse(str(path))

    def child(e, id_):
        return next((c for c in e.elems if c.id == id_), None)

    def props70(e):
        p = child(e, b"Properties70")
        return {c.props[0]: c.props[4:] for c in p.elems} if p else {}

    gs = props70(child(root, b"GlobalSettings"))
    objects = child(root, b"Objects")
    node_dev, max_t, nodes = 0.0, 0.0, 0
    s_nodes, curves, stacks = set(), {}, []
    for e in objects.elems:
        if e.id == b"Model":
            nodes += 1
            p = props70(e)
            node_dev = max([node_dev] + [abs(v - 1.0) for v in p.get(b"Lcl Scaling", [1.0, 1.0, 1.0])])
            max_t = max([max_t] + [abs(v) for v in p.get(b"Lcl Translation", [0.0, 0.0, 0.0])])
        elif e.id == b"AnimationCurveNode" and e.props[1].split(b"\x00")[0] == b"S":
            s_nodes.add(e.props[0])
        elif e.id == b"AnimationCurve":
            kv = child(e, b"KeyValueFloat")
            curves[e.props[0]] = list(kv.props[0]) if kv else []
        elif e.id == b"AnimationStack":
            stacks.append(e.props[1].split(b"\x00")[0].decode())
    anim_dev, s_curves = 0.0, 0
    conns = child(root, b"Connections")
    for c in (conns.elems if conns else []):
        if c.props[0] == b"OP" and c.props[1] in curves and c.props[2] in s_nodes:
            s_curves += 1
            anim_dev = max([anim_dev] + [abs(v - 1.0) for v in curves[c.props[1]]])
    usf = gs.get(b"UnitScaleFactor", [None])[0]
    rep = {
        "file": str(path), "unit_scale_factor": usf, "original_unit_scale_factor": gs.get(b"OriginalUnitScaleFactor", [None])[0],
        "model_nodes": nodes, "max_node_scale_dev": node_dev, "scale_curves": s_curves, "max_anim_scale_dev": anim_dev,
        "max_node_translation": round(max_t, 4), "takes": stacks,
    }
    rep["ok"] = usf == 1.0 and node_dev <= _SCALE_TOLERANCE and anim_dev <= _SCALE_TOLERANCE
    return rep


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

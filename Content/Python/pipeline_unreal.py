"""Unreal-side pipeline helpers (Unreal Engine 5.8). This folder (Content/Python) is on the editor's sys.path.

Live editor (through the unreal-mcp Python execution tool):
    import importlib, pipeline_unreal as pu
    importlib.reload(pu)
    pu.import_static_mesh(r"C:/GameDev/<Project>/art/export/Props/SM_GoldenCrate.fbx", "/Game/Art/Props", "SM_GoldenCrate")

Headless (editor closed):
    tools/unreal-python.ps1 -Function import_static_mesh -ArgsJson '{"src_path": "...", "dest_dir": "/Game/Art/Props", "name": "SM_X"}'

If an API call fails, grep Intermediate/PythonStub/unreal.py for the correct name before guessing.
"""
import json
import os
import re

import unreal


def _log(msg):
    unreal.log("[pipeline] " + str(msg))


def _vec(v):
    return [round(float(v.x), 3), round(float(v.y), 3), round(float(v.z), 3)]


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _levels():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def import_static_mesh(src_path, dest_dir="/Game/Art/Props", name=None, replace=True):
    """Import one FBX/GLB as a static mesh, save it, and return its asset path and bounds (in uu = cm)."""
    src_path = os.path.abspath(src_path)
    if not os.path.isfile(src_path):
        raise FileNotFoundError(src_path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src_path)
    task.set_editor_property("destination_path", dest_dir)
    if name:
        task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", replace)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = [str(p) for p in (task.get_editor_property("imported_object_paths") or [])]
    mesh = None
    for path in imported:
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            mesh = asset
            break
    if mesh is None and name:
        guess = dest_dir.rstrip("/") + "/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(guess):
            asset = unreal.load_asset(guess)
            if isinstance(asset, unreal.StaticMesh):
                mesh = asset
    if mesh is None:
        raise RuntimeError("No StaticMesh imported from %s (imported: %s)" % (src_path, imported))

    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    bounds = mesh.get_bounds()
    result = {
        "source": src_path,
        "imported": imported,
        "mesh": mesh.get_path_name(),
        "box_extent": _vec(bounds.box_extent),
        "origin": _vec(bounds.origin),
    }
    _log(json.dumps(result))
    return result


def _asset_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _run_import(src_path, dest_dir, name=None, options=None, factory=None, replace=True, save=True):
    """Run one automated AssetImportTask; returns the imported object paths."""
    src_path = os.path.abspath(src_path)
    if not os.path.isfile(src_path):
        raise FileNotFoundError(src_path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src_path)
    task.set_editor_property("destination_path", dest_dir)
    if name:
        task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", replace)
    task.set_editor_property("save", save)
    if options is not None:
        task.set_editor_property("options", options)
    if factory is not None:
        task.set_editor_property("factory", factory)
    _asset_tools().import_asset_tasks([task])
    return [str(p) for p in (task.get_editor_property("imported_object_paths") or [])]


def _first_of(paths, cls, fallback_path=None):
    for path in paths:
        asset = unreal.load_asset(path)
        if isinstance(asset, cls):
            return asset
    if fallback_path and unreal.EditorAssetLibrary.does_asset_exist(fallback_path):
        asset = unreal.load_asset(fallback_path)
        if isinstance(asset, cls):
            return asset
    return None


def _save(*assets):
    for a in assets:
        if a is not None:
            unreal.EditorAssetLibrary.save_loaded_asset(a, only_if_is_dirty=False)


def _rename(asset, new_name):
    """Rename an asset in its folder (references are fixed up); returns the renamed asset."""
    if asset is None or asset.get_name() == new_name:
        return asset
    folder = asset.get_path_name().rsplit("/", 1)[0]
    new_path = folder + "/" + new_name
    if unreal.EditorAssetLibrary.does_asset_exist(new_path):
        raise RuntimeError("Cannot rename %s: %s already exists" % (asset.get_path_name(), new_path))
    if not unreal.EditorAssetLibrary.rename_asset(asset.get_path_name(), new_path):
        raise RuntimeError("rename_asset failed: %s -> %s" % (asset.get_path_name(), new_path))
    return unreal.load_asset(new_path)


def import_datatable(src_path, dest_path, row_struct):
    """Import a CSV/JSON DataTable source (repo text is the source of truth) into dest_path (/Game/Data/DT_X).

    row_struct: script path like "/Script/VibeGame.LureMovementRow" (or just "LureMovementRow" for this module).
    Returns the row names, the row struct and the table re-exported as CSV (to compare with the source)."""
    if "/" not in row_struct:
        row_struct = "/Script/VibeGame." + row_struct
    struct = unreal.find_object(None, row_struct) or unreal.load_object(None, row_struct)
    if struct is None:
        raise RuntimeError("Row struct not found: " + row_struct)
    folder, name = dest_path.rsplit("/", 1)
    settings = unreal.CSVImportSettings()
    settings.set_editor_property("import_row_struct", struct)
    settings.set_editor_property("import_type", unreal.CSVImportType.ECSV_DATA_TABLE)
    # UCSVImportFactory::FactoryCanImport only accepts .csv ("Unknown extension 'json'" otherwise); the engine's
    # ReimportDataTableFactory (a UCSVImportFactory subclass registered for .json) parses JSON sources the same way.
    if src_path.lower().endswith(".json"):
        factory = unreal.ReimportDataTableFactory()
    else:
        factory = unreal.CSVImportFactory()
    factory.set_editor_property("automated_import_settings", settings)
    imported = _run_import(src_path, folder, name, factory=factory)
    table = _first_of(imported, unreal.DataTable, dest_path)
    if table is None:
        raise RuntimeError("No DataTable imported from %s (imported: %s)" % (src_path, imported))
    _save(table)
    rows = [str(r) for r in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
    result = {
        "table": table.get_path_name(),
        "row_struct": table.get_editor_property("row_struct").get_path_name(),
        "rows": rows,
        "csv": unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table),
    }
    _log(json.dumps({"table": result["table"], "rows": rows}))
    return result


def reimport_table(dest_path, src_path):
    """Re-import an existing DataTable (dest_path, e.g. /Game/Data/DT_Catch) from its CSV/JSON source, keeping its row struct.
    Used by tools/integrate.ps1 in the batch lane, so a lane that changes a table's source (and maybe its row struct)
    lands with a matching binary asset. A table that does not exist yet needs import_datatable with its row struct."""
    table = unreal.load_asset(dest_path)
    if table is None or not isinstance(table, unreal.DataTable):
        raise RuntimeError("No existing DataTable at " + dest_path + " (new tables need import_datatable)")
    struct = table.get_editor_property("row_struct")
    if struct is None:
        raise RuntimeError(dest_path + " has no row struct")
    result = import_datatable(src_path, dest_path, struct.get_path_name())
    return {"table": result["table"], "row_struct": result["row_struct"], "rows": len(result["rows"])}


def _fbx_options(kind, skeleton=None, import_materials=False, update_ref_pose=False):
    """Legacy FBX importer options (used with unreal.FbxFactory, which bypasses Interchange).

    kind: "skeletal" (mesh + new or given skeleton, no animations, no physics asset), "animation" (animation only
    on `skeleton`, exported time range), or "static". All: Force Front X Axis off, uniform scale 1.0, normals imported,
    Convert Scene Unit ON: the file's declared unit is honored (Blender FBX with UnitScaleFactor 100 = meters is
    converted to cm; a file declared in cm is unchanged). Without it a meter file imports 100x too small.
    Caveat (T-004, SK_FPArms): for a meter-unit Blender skeleton the conversion lands as local scale 100 on the root
    bone (children keep meter translations). Sizes are right, but anything attached to a bone must keep world scale
    (AttachmentRule KEEP_WORLD for scale / C++ SnapToTargetNotIncludingScale). A cm-unit export avoids it.
    Replacing an existing asset re-imports with the settings stored on it: use reimport_interchange() then."""
    o = unreal.FbxImportUI()
    o.set_editor_property("automated_import_should_detect_type", False)
    o.set_editor_property("import_materials", import_materials)
    o.set_editor_property("import_textures", False)
    o.set_editor_property("create_physics_asset", False)
    if kind == "skeletal":
        t = unreal.FBXImportType.FBXIT_SKELETAL_MESH
        o.set_editor_property("import_mesh", True)
        o.set_editor_property("import_as_skeletal", True)
        o.set_editor_property("import_animations", False)
        data = o.get_editor_property("skeletal_mesh_import_data")
    elif kind == "animation":
        t = unreal.FBXImportType.FBXIT_ANIMATION
        o.set_editor_property("import_mesh", False)
        o.set_editor_property("import_as_skeletal", True)
        o.set_editor_property("import_animations", True)
        data = o.get_editor_property("anim_sequence_import_data")
        data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property("import_bone_tracks", True)
        data.set_editor_property("use_default_sample_rate", False)
        data.set_editor_property("remove_redundant_keys", False)
    elif kind == "static":
        t = unreal.FBXImportType.FBXIT_STATIC_MESH
        o.set_editor_property("import_mesh", True)
        o.set_editor_property("import_as_skeletal", False)
        o.set_editor_property("import_animations", False)
        data = o.get_editor_property("static_mesh_import_data")
        data.set_editor_property("combine_meshes", True)
    else:
        raise ValueError(kind)
    o.set_editor_property("mesh_type_to_import", t)
    o.set_editor_property("original_import_type", t)
    if skeleton is not None:
        o.set_editor_property("skeleton", skeleton)
    data.set_editor_property("force_front_x_axis", False)
    data.set_editor_property("import_uniform_scale", 1.0)
    data.set_editor_property("convert_scene", True)
    data.set_editor_property("convert_scene_unit", True)
    if kind == "skeletal":
        data.set_editor_property("update_skeleton_reference_pose", bool(update_ref_pose))
    if kind != "animation":
        data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    return o


def skeleton_report(skeletal_mesh_path):
    """Bones (skeleton order, parent, component-space head in cm) and bounds of a skeletal mesh."""
    mesh = unreal.load_asset(skeletal_mesh_path)
    skel = mesh.get_editor_property("skeleton")
    pose = skel.get_reference_pose()
    bones = []
    for b in pose.get_bone_names():
        parent = str(mesh.get_bone_parent(b))
        t = pose.get_ref_bone_pose(b, unreal.AnimPoseSpaces.WORLD)
        bones.append({"bone": str(b), "parent": "" if parent == "None" else parent, "head_cm": _vec(t.translation)})
    b = mesh.get_bounds()
    return {
        "mesh": mesh.get_path_name(),
        "skeleton": skel.get_path_name(),
        "bone_count": len(bones),
        "bones": bones,
        "bounds_extent": _vec(b.box_extent),
        "bounds_origin": _vec(b.origin),
        "bounds_size": [round(2 * float(v), 2) for v in (b.box_extent.x, b.box_extent.y, b.box_extent.z)],
        "physics_asset": str(mesh.get_editor_property("physics_asset")),
        "materials": [{"slot": str(m.material_slot_name), "material": m.material_interface.get_path_name() if m.material_interface else None}
                      for m in mesh.get_editor_property("materials")],
    }


def import_skeletal_mesh(src_path, dest_dir, name, skeleton_name=None, skeleton_path=None, import_materials=True):
    """Import an FBX skinned mesh (legacy FBX importer): no animations, no physics asset, Force Front X off, scale 1.

    New skeleton (skeleton_path None) is renamed to skeleton_name (e.g. SKEL_X). Returns skeleton_report()."""
    skeleton = unreal.load_asset(skeleton_path) if skeleton_path else None
    imported = _run_import(src_path, dest_dir, name,
                           options=_fbx_options("skeletal", skeleton, import_materials, update_ref_pose=skeleton is not None),
                           factory=unreal.FbxFactory())
    mesh = _first_of(imported, unreal.SkeletalMesh, dest_dir.rstrip("/") + "/" + name)
    if mesh is None:
        raise RuntimeError("No SkeletalMesh imported from %s (imported: %s)" % (src_path, imported))
    skel = mesh.get_editor_property("skeleton")
    if skeleton is None and skeleton_name:
        skel = _rename(skel, skeleton_name)
        mesh = unreal.load_asset(dest_dir.rstrip("/") + "/" + name)
    skel.set_skeleton_preview_mesh(mesh)
    _save(mesh, skel)
    result = skeleton_report(mesh.get_path_name())
    result["imported"] = imported
    _log(json.dumps({"mesh": result["mesh"], "skeleton": result["skeleton"], "bones": result["bone_count"], "size": result["bounds_size"]}))
    return result


def import_animation(src_path, dest_dir, name, skeleton_path, loop=None, additive_local_space=False):
    """Import one FBX take as AnimSequence `name` on an existing skeleton (legacy FBX importer, exported time range,
    file frame rate, root motion off). additive_local_space: Additive Anim Type Local Space + Skeleton Reference Pose."""
    skeleton = unreal.load_asset(skeleton_path)
    if skeleton is None:
        raise RuntimeError("Skeleton not found: " + skeleton_path)
    options = _fbx_options("animation", skeleton)
    options.set_editor_property("override_animation_name", name)
    imported = _run_import(src_path, dest_dir, name, options=options, factory=unreal.FbxFactory())
    anim = _first_of(imported, unreal.AnimSequence, dest_dir.rstrip("/") + "/" + name)
    if anim is None:
        raise RuntimeError("No AnimSequence imported from %s (imported: %s)" % (src_path, imported))
    anim = _rename(anim, name)
    anim.set_editor_property("enable_root_motion", False)
    if loop is not None:
        anim.set_editor_property("loop", loop)
    if additive_local_space:
        set_anim_additive(anim.get_path_name(), save=False)
    _save(anim)
    return anim_report(anim.get_path_name(), imported)


def reimport_interchange(asset_path, convert_scene_unit=True, update_skeleton_ref_pose=False):
    """Re-import an asset from its source file with the settings STORED on the asset (Interchange reimport path;
    replacing an existing asset through an AssetImportTask also takes this path and ignores new options).
    Forces the stored FBX translator setting convert_scene_unit (Blender meter files need True) and, for a skeletal
    mesh, update_skeleton_reference_pose; never creates a physics asset."""
    asset = unreal.load_asset(asset_path)
    data = asset.get_editor_property("asset_import_data")
    if not isinstance(data, unreal.InterchangeAssetImportData):
        raise RuntimeError("%s has %s import data, not Interchange" % (asset_path, data.get_class().get_name() if data else None))
    ts = data.get_translator_settings()
    if ts is not None:
        ts.set_editor_property("convert_scene_unit", bool(convert_scene_unit))
        data.set_translator_settings(ts)
    for p in data.get_pipelines():
        if isinstance(p, unreal.InterchangeGenericAssetsPipeline):
            mp = p.get_editor_property("mesh_pipeline")
            mp.set_editor_property("create_physics_asset", False)
            if update_skeleton_ref_pose:
                mp.set_editor_property("update_skeleton_reference_pose", True)
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    res = unreal.InterchangeManager.get_interchange_manager_scripted().reimport_asset(asset, params)
    asset = unreal.load_asset(asset_path)
    _save(asset)
    return [o.get_path_name() for o in (res or [])]


def set_anim_additive(anim_path, save=True):
    """Additive Anim Type = Local Space, Base Pose Type = Skeleton Reference Pose."""
    anim = unreal.load_asset(anim_path)
    anim.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_LOCAL_SPACE_BASE)
    anim.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_REF_POSE)
    if save:
        _save(anim)
    return anim_report(anim_path)


def anim_report(anim_path, imported=None):
    anim = unreal.load_asset(anim_path)
    lib = unreal.AnimationLibrary
    length = float(anim.get_play_length())
    frames = int(lib.get_num_frames(anim))
    out = {
        "anim": anim.get_path_name(),
        "skeleton": anim.get_editor_property("skeleton").get_path_name(),
        "length_s": round(length, 4),
        "frames": frames,
        "fps": round(frames / length, 3) if length > 0 else None,
        "loop": bool(anim.get_editor_property("loop")),
        "root_motion": bool(anim.get_editor_property("enable_root_motion")),
        "additive": str(anim.get_editor_property("additive_anim_type")),
        "base_pose": str(anim.get_editor_property("ref_pose_type")),
        "tracks": [str(n) for n in lib.get_animation_track_names(anim)],
    }
    if imported is not None:
        out["imported"] = imported
    return out


def static_mesh_sockets(mesh_path, names=()):
    """Sockets of a static mesh (untagged ones plus any in `names`): {name: location cm, rotation}."""
    mesh = unreal.load_asset(mesh_path)
    out = {}
    found = list(mesh.get_sockets_by_tag(""))
    for n in names:
        s = mesh.find_socket(n)
        if s is not None and s not in found:
            found.append(s)
    for s in found:
        r = s.get_editor_property("relative_rotation")
        out[str(s.get_editor_property("socket_name"))] = {
            "location": _vec(s.get_editor_property("relative_location")),
            "rotation_pyr": [round(r.pitch, 3), round(r.yaw, 3), round(r.roll, 3)],
        }
    return out


def ensure_static_mesh_sockets(mesh_path, sockets):
    """Add any missing sockets: sockets = {name: [x, y, z] cm}. Existing sockets are left alone. Returns the socket list."""
    mesh = unreal.load_asset(mesh_path)
    have = static_mesh_sockets(mesh_path, list(sockets.keys()))
    added = []
    for name, loc in sockets.items():
        if name in have:
            continue
        sock = unreal.StaticMeshSocket(mesh)
        sock.set_editor_property("socket_name", name)
        sock.set_editor_property("relative_location", unreal.Vector(*loc))
        mesh.add_socket(sock)
        added.append(name)
    if added:
        _save(mesh)
    return {"mesh": mesh_path, "added": added, "sockets": static_mesh_sockets(mesh_path, list(sockets.keys()))}


def import_static_mesh_fbx(src_path, dest_dir, name, import_materials=True):
    """Import an FBX static mesh with the legacy FBX importer (SOCKET_ empties become mesh sockets)."""
    imported = _run_import(src_path, dest_dir, name, options=_fbx_options("static", None, import_materials),
                           factory=unreal.FbxFactory())
    mesh = _first_of(imported, unreal.StaticMesh, dest_dir.rstrip("/") + "/" + name)
    if mesh is None:
        raise RuntimeError("No StaticMesh imported from %s (imported: %s)" % (src_path, imported))
    _save(mesh)
    b = mesh.get_bounds()
    return {
        "mesh": mesh.get_path_name(),
        "imported": imported,
        "box_extent": _vec(b.box_extent),
        "origin": _vec(b.origin),
        "sockets": static_mesh_sockets(mesh.get_path_name()),
        "materials": [m.material_interface.get_path_name() if m.material_interface else None
                      for m in mesh.get_editor_property("static_materials")],
    }


def list_level_actors():
    """Label, class and location of every actor in the current level."""
    out = []
    for a in _actors().get_all_level_actors():
        out.append({"label": a.get_actor_label(), "class": a.get_class().get_name(), "location": _vec(a.get_actor_location())})
    return out


def _spawn_class(cls, label, location, rotation=None):
    rot = rotation or unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0)
    actor = _actors().spawn_actor_from_class(cls, unreal.Vector(*location), rot)
    actor.set_actor_label(label)
    return actor


def _spawn_mesh(mesh_path, label, location, scale=None):
    mesh = unreal.load_asset(mesh_path)
    if mesh is None:
        raise RuntimeError("Cannot load " + mesh_path)
    actor = _actors().spawn_actor_from_object(mesh, unreal.Vector(*location))
    actor.set_actor_label(label)
    if scale:
        actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def frame_viewport(target=(0.0, 0.0, 50.0), distance=450.0, height=220.0):
    """Point the active level-editor viewport camera at target (use before taking a screenshot)."""
    t = unreal.Vector(*target)
    cam = unreal.Vector(t.x - distance * 0.8, t.y - distance * 0.6, t.z + height)
    rot = unreal.MathLibrary.find_look_at_rotation(cam, t)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(cam, rot)
    return {"camera": _vec(cam), "target": _vec(t)}


def view_from(camera, target):
    """Put the active level-editor viewport camera at `camera`, looking at `target` (both (x, y, z) in cm)."""
    cam = unreal.Vector(*camera)
    t = unreal.Vector(*target)
    rot = unreal.MathLibrary.find_look_at_rotation(cam, t)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(cam, rot)
    return {"camera": _vec(cam), "target": _vec(t)}


def take_screenshot(path, width=1280, height=720):
    """Queue a level-viewport screenshot to an absolute PNG path; the file appears a frame or two later.

    Needs a rendering viewport: the viewport is switched to realtime, and the editor must not be
    CPU-throttled in the background (bThrottleCPUWhenNotForeground=False in DefaultEditorPerProjectUserSettings.ini).
    """
    path = os.path.abspath(path).replace("\\", "/")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    les = _levels()
    les.editor_set_viewport_realtime(True)
    les.editor_invalidate_viewports()
    unreal.AutomationLibrary.take_high_res_screenshot(width, height, path, force_game_view=False)
    return {"screenshot": path, "note": "written on the next rendered frame; check the file before reading it"}


# ---------------- Anim Blueprint graphs (T-006: ABP_FPArms) ----------------
# How to edit an anim graph from Python (UE 5.8; the same C++ API Epic's BlueprintTools MCP toolset wraps):
#   abp, ed = anim_graph("/Game/.../ABP_X")                  ed = unreal.BlueprintGraphEditor for "AnimGraph"
#   ed.create_node_from_name(type_id, unreal.Vector2D(x, y), [])   spawns ANY action-menu entry. type_id is
#       "<menu category>|<menu name>" with the spaces removed, e.g. "Animation|Blends|BlendPoses(EFPArmsPose)",
#       "Animation|Sequences|Play'A_FPArms_Idle'" (list them: [s for s in ed.list_available_nodes([]) if "Blend" in s]).
#   ed.add_get_member_variable_node("ArmsPose")               getter for a C++ parent property (a fast-path binding)
#   _connect(src_node, "Pose", dst_node, "BlendPose_1")        link by INTERNAL pin names (see anim_graph_report)
#   set_anim_node(node, loop_animation=True, ...)              fields of the node's runtime struct (get/set "node")
#   compile_anim_blueprint(abp)                                status + every node error/warning text
# The one thing Python cannot do: expose per-enum pose pins on a "Blend Poses (<Enum>)" node. Its VisibleEnumEntries
# has no CPF_Edit (set_editor_property and ToolsetLibrary.set_object_properties refuse it) and ExposeEnumElementAsPin
# is protected C++, reachable only from the node's context menu ("Add pin for element"). Drive that menu with the
# unreal-mcp SlateInspectorToolset (recipe in fparms_abp_prepare), then continue in Python. Reading them back works:
# enum_blend_pins(node, default_entry) maps each exposed entry to its pin BY NAME (through a T3D export; see the
# "Reading hidden properties" section below). Pin order = exposure order, so never map pose pins by position.

FPARMS_DIR = "/Game/Art/Characters/FPArms"
FPARMS_ABP = FPARMS_DIR + "/ABP_FPArms"
FPARMS_SYNC_GROUP = "FPArmsBreath"
FPARMS_ENUM_BLEND = "Animation|Blends|BlendPoses(EFPArmsPose)"
# EFPArmsPose (Source/VibeGame/Character/FPArmsPose.h) in enum order: (entry, pin label on the node, clip).
# Idle (= 0) plays on the node's Default pin; the others get their own pin, exposed in this order, so pose pin N
# (BlendPose_N) = enum value N, and any value without a pin (a pose added later) falls back to Idle, never the bind pose.
FPARMS_POSES = [
    ("Idle", "Default", "A_FPArms_Idle"),
    ("HoldRod", "Hold Rod", "A_FPArms_HoldRod_Idle"),
    ("ProneHold", "Prone Hold", "A_FPArms_Prone_HoldRod_Idle"),
    ("ProneTuck", "Prone Tuck", "A_FPArms_Prone_TuckRod"),
    ("HoldFish", "Hold Fish", "A_FPArms_HoldFish_Idle"),         # A of the HoldFish size blend (FPARMS_HOLDFISH)
    ("CarryCooler", "Carry Cooler", "A_FPArms_CarryCooler_Idle"),
]
# T-030 HoldFish branch: Two Way Blend (tag HoldFishSize) of the small clip above (A) and the trophy clip (B),
# Alpha <- HoldFishSizeAlpha (C++: clamp((FishScale - HoldFishScaleSmall) / (Large - Small)), DT_Catch).
FPARMS_HOLDFISH = {"entry": "HoldFish", "large_clip": "A_FPArms_HoldFish_Large_Idle", "tag": "HoldFishSize",
                   "class": "AnimGraphNode_TwoWayBlend", "type_id": "Animation|Blends|TwoWayBlend",
                   "alpha": "HoldFishSizeAlpha"}


def _fparms_clips():
    """Every clip the arms graph plays, in layout-row order (the trophy HoldFish clip right under the small one)."""
    clips = []
    for entry, _, clip in FPARMS_POSES:
        clips.append(clip)
        if entry == FPARMS_HOLDFISH["entry"]:
            clips.append(FPARMS_HOLDFISH["large_clip"])
    return clips
FPARMS_NOTE = (
    "Arms pose (T-006). No logic here (CLAUDE.md rule 1): C++ UFPArmsAnimInstance sets ArmsPose from DT_Movement "
    "RodPoseStill / RodPoseMoving and ArmsPoseBlendTime from RodPoseBlendTime.\n"
    "Blend Poses (EFPArmsPose): Default pin = Idle (enum 0, and any pose without its own pin), then Hold Rod, "
    "Prone Hold, Prone Tuck, Hold Fish, Carry Cooler = pins 1-5. Standard Blend, Linear (the clearance in "
    "SK_FPArms.anim.md was measured with this). All players loop in sync group FPArmsBreath.\n"
    "Hold Fish (T-030) = Two Way Blend of HoldFish_Idle (A) and HoldFish_Large_Idle (B), Alpha <- HoldFishSizeAlpha "
    "(C++, from the fish's scale and DT_Catch HoldFishScaleSmall/Large). C++ picks Carry Cooler over Hold Fish over the "
    "rod rule.\n"
    "New pose: add the enum value in C++, right-click this node > Add pin for element, add a Play '<clip>' player, "
    "then extend FPARMS_POSES in Content/Python/pipeline_unreal.py and rerun fparms_abp_wire().")

_ANIM_NODE_KEYS = ["sequence", "loop_animation", "play_rate", "group_name", "group_role", "method", "slot_name",
                   "blend_time", "blend_type", "transition_type", "child_upate_mode", "blend_profile",
                   "custom_blend_curve", "active_enum_value", "active_value"]


def anim_graph(abp_path, graph_name="AnimGraph"):
    """(anim blueprint, unreal.BlueprintGraphEditor) for one graph of an Anim Blueprint."""
    abp = unreal.load_asset(abp_path)
    if abp is None:
        raise RuntimeError("No asset at " + abp_path)
    ed = unreal.BlueprintGraphEditor.get_graph_editor_by_name(abp, graph_name)
    if ed is None:
        raise RuntimeError("No graph %s in %s" % (graph_name, abp_path))
    return abp, ed


def _is_input(pin):
    return pin.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT


def _plain(value):
    """JSON-friendly form of a property value (objects as asset names, enums as names, floats rounded)."""
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        return round(value, 4)
    if isinstance(value, unreal.Name):
        return str(value)
    if isinstance(value, unreal.EnumBase):
        return value.name
    if isinstance(value, unreal.Object):
        return value.get_name()
    try:
        return [_plain(v) for v in value]
    except TypeError:
        return str(value)


def _pin(node, name, direction=None):
    """The pin with internal name `name` ('Pose', 'Source', 'Result', 'BlendPose_1', 'BlendTime_0', 'ActiveEnumValue',
    or the variable name on a getter). Raises with the node's pin list if it is missing."""
    names = []
    for p in node.list_all_pins():
        names.append(str(p.get_pin_name()))
        if names[-1] == name and (direction is None or p.get_pin_direction() == direction):
            return p
    raise RuntimeError("%s has no pin %r (pins: %s)" % (node.get_name(), name, names))


def _connect(src_node, src_pin, dst_node, dst_pin):
    """Link output src_pin -> input dst_pin, replacing whatever dst_pin had. True if a new link was made."""
    out_pin = _pin(src_node, src_pin, unreal.EdGraphPinDirection.EGPD_OUTPUT)
    in_pin = _pin(dst_node, dst_pin, unreal.EdGraphPinDirection.EGPD_INPUT)
    linked = list(in_pin.list_connected_pins())
    if len(linked) == 1 and linked[0].is_same_native_pin(out_pin):
        return False
    if linked:
        in_pin.break_pin_links()
    if not out_pin.try_create_connection(in_pin):
        raise RuntimeError("Could not link %s.%s -> %s.%s" % (src_node.get_name(), src_pin, dst_node.get_name(), dst_pin))
    return True


def set_anim_node(node, **values):
    """Set fields of an anim graph node's runtime struct (FAnimNode_*; e.g. sequence=, loop_animation=, group_name=,
    blend_type=) and write the struct back, so the node gets its normal PostEditChange handling."""
    struct = node.get_editor_property("node")
    for key, value in values.items():
        struct.set_editor_property(key, value)
    node.set_editor_property("node", struct)
    return struct


def _getter_var(node):
    """Variable name read by a K2Node_VariableGet (its value pin is named after the variable), else None."""
    if node.get_class().get_name() != "K2Node_VariableGet":
        return None
    outs = [p for p in node.list_all_pins() if not _is_input(p)]
    return str(outs[0].get_pin_name()) if outs else None


def _nodes_of(ed, class_name):
    return [n for n in ed.list_all_nodes() if n.get_class().get_name() == class_name]


def _getter(ed, variable):
    """The graph's getter node for `variable` (created if missing; extra copies are removed)."""
    found = [n for n in _nodes_of(ed, "K2Node_VariableGet") if _getter_var(n) == variable]
    for extra in found[1:]:
        ed.remove_nodes([extra])
    if found:
        return found[0]
    node = ed.add_get_member_variable_node(variable)
    if node is None:
        raise RuntimeError("add_get_member_variable_node(%r) failed (is it a property of the parent class?)" % variable)
    return node


def anim_graph_report(abp_path, graph_name="AnimGraph"):
    """Every node of an anim graph: class, title, position, pins (type, default or links) and key runtime settings."""
    abp, ed = anim_graph(abp_path, graph_name)
    nodes = []
    for n in ed.list_all_nodes():
        pos = n.get_node_pos()
        entry = {"name": n.get_name(), "class": n.get_class().get_name(),
                 "title": str(n.get_node_title()).replace("\n", " | "), "pos": [pos.x, pos.y], "pins": []}
        for p in n.list_all_pins():
            pin = {"pin": str(p.get_pin_name()), "dir": "in" if _is_input(p) else "out",
                   "type": str(p.get_pin_type_display_string())}
            links = ["%s.%s" % (c.get_owning_node().get_name(), c.get_pin_name()) for c in p.list_connected_pins()]
            if links:
                pin["links"] = links
            elif _is_input(p):
                pin["default"] = p.get_pin_value()
            entry["pins"].append(pin)
        try:
            struct = n.get_editor_property("node")
        except Exception:
            struct = None
        if struct is not None:
            settings = {}
            for key in _ANIM_NODE_KEYS:
                try:
                    settings[key] = _plain(struct.get_editor_property(key))
                except Exception:
                    pass
            entry["settings"] = settings
        nodes.append(entry)
    comments = [unreal.BlueprintEditorLibrary.get_comment_text(c) for c in ed.list_comment_nodes()]
    parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(abp)
    return {"abp": abp_path, "graph": graph_name, "parent": parent.get_name() if parent else None,
            "nodes": nodes, "comments": comments}


def compile_anim_blueprint(abp):
    """Compile; returns the status (BS_UP_TO_DATE = no errors and no warnings) and every node message in every graph."""
    ok = unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    status = abp.get_editor_property("status")
    problems = []
    for graph in unreal.BlueprintEditorLibrary.list_graphs(abp):
        ged = unreal.BlueprintGraphEditor.get_graph_editor(graph)
        for kind, nodes in (("error", ged.list_nodes_with_errors()), ("warning", ged.list_nodes_with_warnings()),
                            ("note", ged.list_nodes_with_notes())):
            for n in nodes:
                problems.append({"kind": kind, "graph": graph.get_name(), "node": n.get_name(),
                                 "msg": n.get_editor_property("error_msg")})
    return {"compiled": bool(ok), "status": status.name if hasattr(status, "name") else str(status),
            "clean": status == unreal.BlueprintStatus.BS_UP_TO_DATE and not problems, "problems": problems}


def _fparms_players(ed):
    """{clip name: Sequence Player node} for the clips already in the graph. A player whose Sequence pin is shown
    (ABP_Fish Curled) keeps its clip as that pin's default value, not in the node struct."""
    out = {}
    for n in _nodes_of(ed, "AnimGraphNode_SequencePlayer"):
        seq = n.get_editor_property("node").get_editor_property("sequence")
        if seq is not None:
            out.setdefault(seq.get_name(), n)
            continue
        for p in n.list_all_pins():
            if str(p.get_pin_name()) == "Sequence" and p.get_pin_value():
                out.setdefault(p.get_pin_value().rsplit(".", 1)[-1], n)
    return out


def _fparms_pose_pins(blend):
    return [str(p.get_pin_name()) for p in blend.list_all_pins() if str(p.get_pin_name()).startswith("BlendPose_")]


def fparms_abp_prepare(abp_path=FPARMS_ABP):
    """ABP_FPArms 4-pose graph, step 1 of 3 (T-006; spec SK_FPArms.anim.md "Wiring"). Idempotent.

    Removes the first version's 'Blend Poses by bool' and its Get bHoldingRod, makes one looping Sequence Player per
    FPARMS_POSES clip (sync group FPArmsBreath, can be leader) and one 'Blend Poses (EFPArmsPose)' node (Standard
    Blend, Linear, child update Default), laid out left to right. Does not compile or save.

    Step 2 (UI only, see the module note above): in the open ABP editor expose the enum pins in FPARMS_POSES order
    (Hold Rod, Prone Hold, Prone Tuck) with the unreal-mcp SlateInspectorToolset: Snapshot the ABP window, Click the
    node title "Blend Poses (EFPArmsPose)" with button="right", Snapshot again, Click the menu entry under "Add pin for
    element"; repeat (refs change after each rebuild). The node then shows Default / Hold Rod / Prone Hold / Prone Tuck
    Pose pins. Step 3: fparms_abp_wire().
    """
    abp, ed = anim_graph(abp_path)
    removed = []
    for n in list(ed.list_all_nodes()):
        cls = n.get_class().get_name()
        if cls == "AnimGraphNode_BlendListByBool" or (cls == "K2Node_VariableGet" and _getter_var(n) == "bHoldingRod"):
            removed.append(n.get_name())
            ed.remove_nodes([n])

    players = _fparms_players(ed)
    created = []
    for i, clip in enumerate(_fparms_clips()):
        node = players.get(clip)
        if node is None:
            node = ed.create_node_from_name("Animation|Sequences|Play'%s'" % clip, unreal.Vector2D(0.0, 0.0), [])
            if node is None:
                raise RuntimeError("No action-menu entry Play'%s' (is the clip imported on SKEL_FPArms?)" % clip)
            created.append(node.get_name())
        seq = unreal.load_asset("%s/%s" % (FPARMS_DIR, clip))
        set_anim_node(node, sequence=seq, loop_animation=True, play_rate=1.0, group_name=FPARMS_SYNC_GROUP,
                      group_role=unreal.AnimGroupRole.CAN_BE_LEADER, method=unreal.AnimSyncMethod.SYNC_GROUP)
        node.set_node_pos(unreal.IntPoint(0, -360 + 170 * i))

    hf = FPARMS_HOLDFISH
    size_blend, new = tagged_node(ed, hf["tag"], hf["class"], hf["type_id"])
    if new:
        created.append("%s (%s)" % (size_blend.get_name(), hf["tag"]))

    blends = _nodes_of(ed, "AnimGraphNode_BlendListByEnum")
    if len(blends) > 1:
        raise RuntimeError("More than one Blend Poses (enum) node: %s; remove the extra one" % [b.get_name() for b in blends])
    if blends:
        blend = blends[0]
    else:
        blend = ed.create_node_from_name(FPARMS_ENUM_BLEND, unreal.Vector2D(420.0, -200.0), [])
        if blend is None:
            raise RuntimeError("No action-menu entry " + FPARMS_ENUM_BLEND)
        created.append(blend.get_name())
    set_anim_node(blend, transition_type=unreal.BlendListTransitionType.STANDARD_BLEND,
                  blend_type=unreal.AlphaBlendOption.LINEAR, child_upate_mode=unreal.BlendListChildUpdateMode.DEFAULT)
    blend.set_node_pos(unreal.IntPoint(420, -200))
    unreal.BlueprintEditorLibrary.refresh_open_editors_for_blueprint(abp)
    pose_pins = _fparms_pose_pins(blend)
    return {"removed": removed, "created": created, "blend": blend.get_name(), "title": str(blend.get_node_title()),
            "pose_pins": pose_pins, "exposed_enum_pins": len(pose_pins) - 1,
            "next": "expose %s in the node's context menu, in that order, then fparms_abp_wire()"
                    % [label for _, label, _ in FPARMS_POSES[len(pose_pins):]]}


def _fparms_blend(ed):
    blends = _nodes_of(ed, "AnimGraphNode_BlendListByEnum")
    if len(blends) != 1:
        raise RuntimeError("Expected one Blend Poses (EFPArmsPose) node, found %d: run fparms_abp_prepare()" % len(blends))
    return blends[0]


def fparms_abp_wire(abp_path=FPARMS_ABP, save=True):
    """ABP_FPArms graph, last step (after fparms_abp_prepare and the UI step; T-006, T-028). Idempotent.

    Each FPARMS_POSES player -> its Blend Poses (EFPArmsPose) pin, mapped BY NAME (enum_blend_pins: pin order is the
    order the pins were exposed, not the enum order). The Hold Rod player goes through the rod-aim chain once
    fparms_abp_wire_rod_aim() has made it (aim offset -> Local to Component -> Two Bone IK -> Component to Local), so
    rerunning this keeps the chain. Get ArmsPose -> Active Enum Value, Get ArmsPoseBlendTime -> every Blend Time pin,
    blend -> Slot DefaultSlot -> Slot StanceAdditive -> Output Pose, plus the note comments. Compiles and (if clean and
    save=True) saves. Returns the compile result and the graph report.
    """
    abp, ed = anim_graph(abp_path)
    blend = _fparms_blend(ed)
    pins = enum_blend_pins(blend, FPARMS_POSES[0][0])
    labels = {entry: label for entry, label, _ in FPARMS_POSES}
    missing = [labels[entry] for entry, _, _ in FPARMS_POSES if entry not in pins]
    if missing:
        raise RuntimeError("Expose %s on %s first (right-click it > Add pin for element; step 2). Pins now: %s"
                           % (missing, blend.get_name(), pins))
    unknown = [entry for entry in pins if entry not in labels]
    if unknown:
        raise RuntimeError("%s has pins for %s, which FPARMS_POSES doesn't list: add (entry, label, clip) rows first"
                           % (blend.get_name(), unknown))
    players = _fparms_players(ed)
    missing = [clip for clip in _fparms_clips() if clip not in players]
    if missing:
        raise RuntimeError("No Sequence Player for %s: run fparms_abp_prepare()" % missing)
    hf = FPARMS_HOLDFISH
    size_blend = [n for n in ed.list_all_nodes() if _node_tag(n) == hf["tag"]]
    if len(size_blend) != 1:
        raise RuntimeError("Expected one node tagged %s, found %d: run fparms_abp_prepare()" % (hf["tag"], len(size_blend)))
    size_blend = size_blend[0]
    slots = {}
    for n in _nodes_of(ed, "AnimGraphNode_Slot"):
        slots[str(n.get_editor_property("node").get_editor_property("slot_name"))] = n
    for name in ("DefaultSlot", "StanceAdditive"):
        if name not in slots:
            raise RuntimeError("No Slot '%s' node in the graph" % name)
    roots = _nodes_of(ed, "AnimGraphNode_Root")
    if len(roots) != 1:
        raise RuntimeError("Expected one Output Pose node, found %d" % len(roots))
    chain = _fparms_rod_aim_chain(ed)  # None until fparms_abp_wire_rod_aim() made it

    links = 0
    for entry, _, clip in FPARMS_POSES:
        source = players[clip]
        if chain and entry == FPARMS_AIM_POSE:
            links += _fparms_link_rod_aim(ed, chain, source)
            source = chain["c2l"]
        if entry == hf["entry"]:
            links += _connect(source, "Pose", size_blend, "A")
            links += _connect(players[hf["large_clip"]], "Pose", size_blend, "B")
            links += _connect(_getter(ed, hf["alpha"]), hf["alpha"], size_blend, "Alpha")
            source = size_blend
        links += _connect(source, "Pose", blend, pins[entry])
    pose_get = _getter(ed, "ArmsPose")
    time_get = _getter(ed, "ArmsPoseBlendTime")
    links += _connect(pose_get, "ArmsPose", blend, "ActiveEnumValue")
    for pose_pin in pins.values():
        links += _connect(time_get, "ArmsPoseBlendTime", blend, pose_pin.replace("BlendPose_", "BlendTime_"))
    links += _connect(blend, "Pose", slots["DefaultSlot"], "Source")
    links += _connect(slots["DefaultSlot"], "Pose", slots["StanceAdditive"], "Source")
    links += _connect(slots["StanceAdditive"], "Pose", roots[0], "Result")

    # Layout, left to right (graph units): players | [rod-aim chain on the Hold Rod row] | getters | blend | slots | output.
    clip_rows = {clip: -360 + 170 * i for i, clip in enumerate(_fparms_clips())}
    rows = {entry: clip_rows[clip] for entry, _, clip in FPARMS_POSES}
    for clip, y in clip_rows.items():
        players[clip].set_node_pos(unreal.IntPoint(0, y))
    # HoldFish size blend right of the rod-aim note (which hangs under the Hold Rod row at x 420-820).
    y = rows[hf["entry"]]
    size_blend.set_node_pos(unreal.IntPoint(1100, y))
    _getter(ed, hf["alpha"]).set_node_pos(unreal.IntPoint(860, y + 190))
    blend_x = 420
    if chain:
        y = rows[FPARMS_AIM_POSE]
        for key, x in (("aim", 420), ("l2c", 760), ("ik", 1020), ("c2l", 1340)):
            chain[key].set_node_pos(unreal.IntPoint(x, y))
        _getter(ed, "RodAimYaw").set_node_pos(unreal.IntPoint(420, y + 190))
        _getter(ed, "RodAimPitch").set_node_pos(unreal.IntPoint(420, y + 270))
        blend_x = 1650
    time_get.set_node_pos(unreal.IntPoint(blend_x - 190, 290))
    pose_get.set_node_pos(unreal.IntPoint(blend_x - 190, 370))
    blend.set_node_pos(unreal.IntPoint(blend_x, -200))
    slots["DefaultSlot"].set_node_pos(unreal.IntPoint(blend_x + 380, -200))
    slots["StanceAdditive"].set_node_pos(unreal.IntPoint(blend_x + 680, -200))
    roots[0].set_node_pos(unreal.IntPoint(blend_x + 980, -200))
    # Notes (NOTE_WIDTH wide, text wraps downward): left of the players, and under the rod-aim chain's getters.
    note_graph(ed, "Arms pose (T-006)", FPARMS_NOTE, (-40 - NOTE_WIDTH, rows[FPARMS_POSES[0][0]]))
    if chain:
        y = rows[FPARMS_AIM_POSE]
        note_graph(ed, "Rod aim (T-028)", FPARMS_AIM_NOTE, (420, y + 370))

    # Refresh the open editor BEFORE compiling and saving: a refresh after the save marks the package dirty again.
    unreal.BlueprintEditorLibrary.refresh_open_editors_for_blueprint(abp)
    compiled = compile_anim_blueprint(abp)
    saved = False
    if save and compiled["clean"]:
        saved = unreal.EditorAssetLibrary.save_loaded_asset(abp, only_if_is_dirty=False)
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    return {"pins": pins, "rod_aim_chain": bool(chain), "new_links": links, "compile": compiled, "saved": bool(saved),
            "dirty_after_save": abp_path in dirty, "graph": anim_graph_report(abp_path)}


# ---------------- Reading hidden properties, pins by name, tagged nodes (T-028, T-029) ----------------
# 1. export_t3d(obj) returns an object's T3D text (what Ctrl+C in a graph puts on the clipboard). It is the way to READ
#    plain UPROPERTYs that set/get_editor_property and ToolsetLibrary refuse (no CPF_Edit): a Blend Poses (enum) node's
#    VisibleEnumEntries (enum_blend_entries / enum_blend_pins) or a blend space's runtime grid (blend_space_report).
# 2. Pose pins of a Blend Poses (enum) node follow the order they were exposed in, not the enum order. Always map them by
#    name with enum_blend_pins(node, default_entry), never by position.
# 3. tagged_node(ed, tag, ...) finds or creates a node by its Tag (unique per Anim Blueprint), so the builders stay
#    idempotent when a graph holds several nodes of one class (two IKs, two space conversions ...).
# 4. show_anim_pins(node, "PlayRate") edits show_pin_for_properties AND rebuilds the node, so the pin exists right away.
#    The array edit alone does not rebuild it (the node only rebuilds when the edit event names the inner bShowPin,
#    which Python can't send), and neither does a Blueprint compile. reconstruct_anim_node(node) is the rebuild.
# 5. Every graph node needs a NodeGuid. BlueprintGraphEditor.add_comment_node (UE 5.8) never sets one: the package then
#    warns "missing NodeGuid, this can cause deterministic cooking issues" on every load (look for it in a headless test
#    log). note_graph adds notes through the "Add Comment..." action instead. When you add a node some other new way,
#    check it with _has_node_guid(node).

_PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
T3D_DIR = os.path.join(_PROJECT_DIR, "Saved", "AgentLogs", "scratch", "t3d")
_ENUM_ENTRY_RE = re.compile(r'VisibleEnumEntries\((\d+)\)="?(?:\w+::)?(\w+)"?')


def export_t3d(obj, name=None):
    """T3D text of any object, also written to Saved/AgentLogs/scratch/t3d/<name>.t3d (git-ignored scratch)."""
    os.makedirs(T3D_DIR, exist_ok=True)
    path = os.path.join(T3D_DIR, (name or obj.get_name()) + ".t3d")
    task = unreal.AssetExportTask()
    for key, value in (("object", obj), ("exporter", unreal.ObjectExporterT3D()), ("filename", path),
                       ("automated", True), ("prompt", False), ("replace_identical", True)):
        task.set_editor_property(key, value)
    if not unreal.Exporter.run_asset_export_task(task) or not os.path.isfile(path):
        raise RuntimeError("T3D export of %s failed: %s" % (obj.get_path_name(), list(task.get_editor_property("errors"))))
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    return raw.decode("utf-8", errors="replace")


def enum_blend_entries(node):
    """Enum entries exposed as pose pins on a 'Blend Poses (<Enum>)' node, in PIN order: entry k plays on BlendPose_<k+1>
    and uses BlendTime_<k+1>; BlendPose_0 is the Default pin (enum value 0 and every entry without its own pin)."""
    graph = node.get_outer()
    owner = graph.get_outer() if graph is not None else None
    text = export_t3d(node, "%s_%s" % (owner.get_name() if owner is not None else "graph", node.get_name()))
    found = {}
    for index, entry in _ENUM_ENTRY_RE.findall(text):
        found[int(index)] = entry
    if sorted(found) != list(range(len(found))):
        raise RuntimeError("Unexpected VisibleEnumEntries indices in the T3D of %s: %s" % (node.get_name(), found))
    return [found[i] for i in range(len(found))]


def enum_blend_pins(node, default_entry):
    """{enum entry: pose pin} of a 'Blend Poses (<Enum>)' node, by name: default_entry (the enum's value 0) -> the
    Default pin BlendPose_0, each exposed entry -> its own pin. Its blend time pin: pin.replace("BlendPose_", "BlendTime_")."""
    entries = enum_blend_entries(node)
    if default_entry in entries:
        raise RuntimeError("%s has its own pin for %s, which the Default pin already plays: remove that pin"
                           % (node.get_name(), default_entry))
    pins = {default_entry: "BlendPose_0"}
    for k, entry in enumerate(entries):
        pins[entry] = "BlendPose_%d" % (k + 1)
    real = set(_fparms_pose_pins(node))
    lost = [pin for pin in pins.values() if pin not in real]
    if lost:
        raise RuntimeError("%s: the T3D maps %s but the node has no pins %s (compile, then retry)" % (node.get_name(), pins, lost))
    return pins


def menu_entries(ed, *needles):
    """Action-menu type_ids of this graph containing every needle (case-insensitive), for create_node_from_name."""
    lowered = [n.lower() for n in needles]
    return [s for s in ed.list_available_nodes([]) if all(n in s.lower() for n in lowered)]


def _node_tag(node):
    try:
        return str(node.get_editor_property("tag"))
    except Exception:  # not an anim graph node (getters, comments)
        return ""


def tagged_node(ed, tag, class_name, type_ids, pos=(0, 0)):
    """(node, created): the anim graph node whose Tag is `tag`, else a new one, tagged, from the first action-menu entry
    in type_ids (one type_id or a list of candidates) that makes a `class_name` node."""
    found = [n for n in ed.list_all_nodes() if _node_tag(n) == tag]
    if len(found) > 1:
        raise RuntimeError("Tag %s is on %d nodes %s: remove the extra ones" % (tag, len(found), [n.get_name() for n in found]))
    if found:
        if found[0].get_class().get_name() != class_name:
            raise RuntimeError("Tag %s is on a %s, expected %s" % (tag, found[0].get_class().get_name(), class_name))
        return found[0], False
    tried = []
    for type_id in ([type_ids] if isinstance(type_ids, str) else list(type_ids)):
        node = ed.create_node_from_name(type_id, unreal.Vector2D(float(pos[0]), float(pos[1])), [])
        if node is None:
            tried.append("%s: no such entry" % type_id)
            continue
        if node.get_class().get_name() != class_name:
            tried.append("%s: made a %s" % (type_id, node.get_class().get_name()))
            ed.remove_nodes([node])
            continue
        node.set_editor_property("tag", tag)
        return node, True
    raise RuntimeError("No action-menu entry of graph %s makes a %s: %s" % (ed.get_graph().get_name(), class_name, tried))


def reconstruct_anim_node(node):
    """Rebuild an anim graph node's pins, as the details panel does after a pin checkbox (links survive: pins are
    matched by name). Python has no ReconstructNode, but UAnimGraphNode_Base::PostEditChangeProperty rebuilds the node
    on an UpdateFunction edit, so re-set update_function to its own value with notify ALWAYS: a no-op edit that fires
    the event."""
    node.set_editor_property("update_function", node.get_editor_property("update_function"),
                             unreal.PropertyAccessChangeNotifyMode.ALWAYS)


def show_anim_pins(node, *properties):
    """Show optional pins (e.g. 'PlayRate', 'StartPosition' on a Sequence Player) and rebuild the node so they exist
    now. Returns the names newly shown; raises if the node has no such optional pin, or the pins are still missing."""
    options = list(node.get_editor_property("show_pin_for_properties"))
    names, changed = [], []
    for i, option in enumerate(options):
        name = str(option.get_editor_property("property_name"))
        names.append(name)
        if name in properties and not option.get_editor_property("show_pin"):
            option.set_editor_property("show_pin", True)
            options[i] = option
            changed.append(name)
    unknown = [p for p in properties if p not in names]
    if unknown:
        raise RuntimeError("%s has no optional pins %s (it has %s)" % (node.get_name(), unknown, names))
    if changed:
        node.set_editor_property("show_pin_for_properties", options)
    pin_names = lambda: {str(p.get_pin_name()) for p in node.list_all_pins()}
    if any(p not in pin_names() for p in properties):
        reconstruct_anim_node(node)
        missing = [p for p in properties if p not in pin_names()]
        if missing:
            raise RuntimeError("%s: pins %s still missing after the rebuild (pins: %s)" % (node.get_name(), missing, sorted(pin_names())))
    return changed


# The graph's right-click "Add Comment..." action as create_node_from_name names it ("<Category>|<Menu name>", spaces
# removed). While nodes are selected in the Blueprint's open editor it is "Add Comment to Selection" instead (and would
# wrap them), so _spawn_comment closes the editor first then.
_ADD_COMMENT_ACTION = "|AddComment..."
NOTE_WIDTH = 400  # graph units: a note is the engine's default 400 x 100 comment box (Python can't size a comment)


def _has_node_guid(node, name=None):
    """True when the graph node has a NodeGuid. The GUID is not a Python property; its T3D lists it only when set."""
    found = re.search(r"\bNodeGuid=([0-9A-Fa-f]{32})\b", export_t3d(node, name))
    return bool(found) and found.group(1).strip("0") != ""


def _spawn_comment(ed, top_left):
    """A new comment box (400 x 100, text "Comment") with its top-left corner at top_left, made like the graph's
    right-click "Add Comment...", which gives it a NodeGuid (BlueprintGraphEditor.add_comment_node does not, UE 5.8)."""
    center = unreal.Vector2D(float(top_left[0]) + NOTE_WIDTH / 2.0, float(top_left[1]) + 50.0)  # the action centers its box
    node = ed.create_node_from_name(_ADD_COMMENT_ACTION, center, [])
    if node is None:
        # Nodes are selected in the open editor (the action is "Add Comment to Selection" now): closing the editor clears
        # the selection; add the comment, then reopen the editor.
        blueprint = ed.get_graph().get_typed_outer(unreal.Blueprint)
        editors = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        was_open = editors.close_all_editors_for_asset(blueprint) > 0
        node = ed.create_node_from_name(_ADD_COMMENT_ACTION, center, [])
        if was_open:
            editors.open_editor_for_assets([blueprint])
    if not isinstance(node, unreal.EdGraphNode_Comment):
        actions = [a for a in ed.list_available_nodes([]) if "Comment" in a]
        raise RuntimeError("%s: the %r action made %r (comment actions here: %s)"
                           % (ed.get_graph().get_path_name(), _ADD_COMMENT_ACTION, node, actions))
    return node


def note_graph(ed, prefix, text, top_left):
    """Set the text of the graph's note (the comment whose text starts with `prefix`), or add the note with its top-left
    corner at top_left. A note is NOTE_WIDTH wide and its text wraps downward: leave free room below and right of it.
    Notes are made by _spawn_comment, never BlueprintGraphEditor.add_comment_node: in UE 5.8 that one leaves the comment
    without a NodeGuid, and every later load of the package warns "missing NodeGuid, this can cause deterministic cooking
    issues" (a test run shows it). An old note without a NodeGuid is replaced at top_left; a note that has one keeps its
    place and size (Python can't move or resize a comment)."""
    owner = ed.get_graph().get_typed_outer(unreal.Blueprint).get_name()
    keep = None
    for note in [c for c in ed.list_comment_nodes() if unreal.BlueprintEditorLibrary.get_comment_text(c).startswith(prefix)]:
        if keep is None and _has_node_guid(note, "%s_%s" % (owner, note.get_name())):
            keep = note
        else:
            ed.remove_comment_node(note)  # made the old way (no NodeGuid), or a duplicate
    if keep is None:
        keep = _spawn_comment(ed, top_left)
    unreal.BlueprintEditorLibrary.set_comment_text(keep, text)
    return keep


def _bone_ref(bone):
    ref = unreal.BoneReference()
    ref.set_editor_property("bone_name", bone)
    return ref


def _bone_target(bone):
    target = unreal.BoneSocketTarget()
    target.set_editor_property("bone_reference", _bone_ref(bone))
    target.set_editor_property("use_socket", False)
    return target


def _bone_of(target):
    return str(target.get_editor_property("bone_reference").get_editor_property("bone_name"))


def open_asset_editors(*assets):
    """Open the asset editors (Jimmy watches) and select the assets in the Content Browser."""
    objs = [unreal.load_asset(a) if isinstance(a, str) else a for a in assets]
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets(objs)
    unreal.EditorAssetLibrary.sync_browser_to_objects([o.get_path_name().split(".")[0] for o in objs])


_GRID_RE = re.compile(r"GridSamples\((\d+)\)=\((.*)\)")


def blend_space_report(bs):
    """Axes, samples and the runtime grid of a 2D blend space / aim offset (read from its T3D). 'grid' lists, per grid
    point (x, y), the sample that gets weight 1 there (None = a blend); 'grid_exact' counts the points played by exactly
    the sample placed on them. An empty grid = never resampled: the asset plays nothing (see fparms_rod_aim_offset)."""
    text = export_t3d(bs)
    lines = [l.strip() for l in text.splitlines()]
    axes = []
    for p in list(bs.get_editor_property("blend_parameters"))[:2]:
        axes.append({"name": p.get_editor_property("display_name"), "min": _plain(p.get_editor_property("min")),
                     "max": _plain(p.get_editor_property("max")), "grid": p.get_editor_property("grid_num"),
                     "snap": p.get_editor_property("snap_to_grid"), "wrap": p.get_editor_property("wrap_input")})
    samples = []
    for s in bs.get_editor_property("sample_data"):
        v = s.get_editor_property("sample_value")
        anim = s.get_editor_property("animation")
        samples.append([anim.get_name() if anim else None, _plain(v.x), _plain(v.y)])
    grid, exact = [], 0
    for line in lines:
        m = _GRID_RE.match(line)
        if not m:
            continue
        index = int(m.group(1))
        fields = dict(re.findall(r"(\w+\[\d\])=([-\d.]+)", m.group(2)))
        nx = axes[0]["grid"] + 1
        at = [round(axes[0]["min"] + (axes[0]["max"] - axes[0]["min"]) * (index % nx) / axes[0]["grid"], 3),
              round(axes[1]["min"] + (axes[1]["max"] - axes[1]["min"]) * (index // nx) / axes[1]["grid"], 3)]
        full = [int(fields["Indices[%d]" % k]) for k in range(3) if float(fields.get("Weights[%d]" % k, 0)) > 0.999]
        name = samples[full[0]][0] if full and full[0] < len(samples) else None
        exact += bool(full) and full[0] < len(samples) and samples[full[0]][1:] == at
        grid.append({"at": at, "sample": name})
    flag = re.search(r"bContainsRotationOffsetMeshSpaceSamples=(\w+)", text)
    return {"asset": bs.get_path_name().split(".")[0], "axes": axes, "use_grid": bs.get_editor_property("interpolate_using_grid"),
            "preview_base": _plain(bs.get_editor_property("preview_base_pose")), "samples": samples,
            "mesh_space_samples": flag.group(1) == "True" if flag else False, "grid": grid, "grid_exact": exact}


# ---------------- T-028 rod aim: AO_FPArms_RodAim + Two Bone IK on the Hold Rod branch of ABP_FPArms ----------------
# Spec: art/export/Characters/SK_FPArms.anim.md ("Rod aim offset", "Unreal import" step 4, "Wiring").
# Blend spaces: UBlendSpace::ResampleData() fills the runtime grid the asset PLAYS from; it isn't scriptable. The open
# blend space editor calls it whenever a property of the asset changes, so fparms_rod_aim_offset() opens the editor
# FIRST, then sets the axes and samples, and checks the grid in the T3D before saving. Set without the editor open, the
# asset saves with an empty grid and plays nothing (the arms would never move).

FPARMS_AO_NAME = "AO_FPArms_RodAim"
FPARMS_AO = FPARMS_DIR + "/" + FPARMS_AO_NAME
FPARMS_AIM_POSE = "HoldRod"                     # the EFPArmsPose branch the aim offset + IK sit on
FPARMS_AO_BASE = "A_FPArms_HoldRod_Idle"        # that branch's clip = the aim offset's preview base pose
# Axes, X then Y: (display name = the UFPArmsAnimInstance property that drives it, min, max, grid divisions).
FPARMS_AO_AXES = [("RodAimYaw", -1.0, 1.0, 2), ("RodAimPitch", -1.0, 1.0, 2)]
# (clip A_FPArms_RodAim_<suffix>, yaw, pitch). yaw +1 = rod tip to the player's RIGHT, pitch +1 = up (pulled back).
FPARMS_AO_SAMPLES = [("Center", 0, 0), ("Up", 0, 1), ("Down", 0, -1), ("Left", -1, 0), ("Right", 1, 0),
                     ("UpLeft", -1, 1), ("UpRight", 1, 1), ("DownLeft", -1, -1), ("DownRight", 1, -1)]
# The chain's nodes: (key, Tag, node class, action-menu entry; None = the aim offset's own entry, found at run time).
FPARMS_AIM_NODES = [
    ("aim", "RodAimOffset", "AnimGraphNode_RotationOffsetBlendSpace", None),
    ("l2c", "RodAimToComponent", "AnimGraphNode_LocalToComponentSpace", "Animation|ConvertSpaces|LocalToComponent"),
    ("ik", "RodAimLeftHandIK", "AnimGraphNode_TwoBoneIK", "Animation|SkeletalControls|TwoBoneIK"),
    ("c2l", "RodAimToLocal", "AnimGraphNode_ComponentToLocalSpace", "Animation|ConvertSpaces|ComponentToLocal"),
]
# Two Bone IK (left arm): hand_l follows the reel crank. Effector = bone hand_l_crank (a child of hand_r_rod, so it moves
# with the aimed rod) in Bone Space, rotation taken from it. The 9 clips put hand_l exactly on the crank (0.0 cm, 0.0 deg,
# checked on every pose), so the IK only acts between the poses. Joint target = lowerarm_l in Bone Space, zero offset =
# "keep the pose's elbow". The spec's "none" is NOT that in UE: an empty joint target is the component origin (the
# camera), which swings the elbow up and inward. No stretching (reach is 75-96 % of the arm).
FPARMS_IK = {"ik_bone": "hand_l", "effector": "hand_l_crank", "joint": "lowerarm_l"}
FPARMS_AIM_NOTE = (
    "Rod aim (T-028), on the Hold Rod branch only. C++ UFPArmsAnimInstance sets RodAimYaw (-1 tip left .. +1 right) and "
    "RodAimPitch (-1 dipped .. +1 pulled back) during a fight, 0 otherwise (0,0 = HoldRod_Idle exactly).\n"
    "AO_FPArms_RodAim: 9 Mesh Space additive poses on a 3 x 3 grid (2 divisions per axis), Use Grid on (bilinear, as "
    "the poses were authored). "
    "Two Bone IK: hand_l to hand_l_crank (Bone Space, rotation from the crank); joint target lowerarm_l (Bone Space) keeps "
    "the pose's elbow. Class default bRodAimOffsetInGraph = true: the fishing code no longer turns the rod itself.\n"
    "Rebuild: pipeline_unreal fparms_rod_aim_offset(), then fparms_abp_wire_rod_aim().")


def fparms_rod_aim_offset(save=True):
    """AO_FPArms_RodAim (T-028): an Aim Offset on SKEL_FPArms, axes RodAimYaw (X) / RodAimPitch (Y) -1..1 with 2 grid
    divisions, the 9 A_FPArms_RodAim_* Mesh Space additive poses on the grid points, Use Grid on. Creates or refreshes
    it (idempotent) with its editor open (see the section note), checks it (every sample valid, grid built) and saves."""
    bad, clips = [], []
    for suffix, x, y in FPARMS_AO_SAMPLES:
        clip = unreal.load_asset("%s/A_FPArms_RodAim_%s" % (FPARMS_DIR, suffix))
        if clip is None:
            bad.append("A_FPArms_RodAim_%s missing" % suffix)
            continue
        kind = clip.get_editor_property("additive_anim_type")
        if kind != unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE:
            bad.append("%s is %s, an aim offset needs Mesh Space additives" % (clip.get_name(), _plain(kind)))
        clips.append((clip, float(x), float(y)))
    if bad:
        raise RuntimeError("RodAim clips not ready (SK_FPArms.anim.md 'Unreal import' step 3): %s" % bad)

    created = False
    ao = unreal.load_asset(FPARMS_AO) if unreal.EditorAssetLibrary.does_asset_exist(FPARMS_AO) else None
    if ao is None:
        factory = unreal.AimOffsetBlendSpaceFactoryNew()
        factory.set_editor_property("target_skeleton", unreal.load_asset(FPARMS_DIR + "/SKEL_FPArms"))
        factory.set_editor_property("preview_skeletal_mesh", unreal.load_asset(FPARMS_DIR + "/SK_FPArms"))
        ao = unreal.AssetToolsHelpers.get_asset_tools().create_asset(FPARMS_AO_NAME, FPARMS_DIR, unreal.AimOffsetBlendSpace, factory)
        if ao is None:
            raise RuntimeError("Could not create " + FPARMS_AO)
        created = True
    open_asset_editors(ao)  # BEFORE the edits: the open editor resamples the grid on every property change

    params = list(ao.get_editor_property("blend_parameters"))
    for i, (name, low, high, grid) in enumerate(FPARMS_AO_AXES):
        p = params[i]
        for key, value in (("display_name", name), ("min", low), ("max", high), ("grid_num", grid),
                           ("snap_to_grid", True), ("wrap_input", False)):
            p.set_editor_property(key, value)
        params[i] = p
    ao.set_editor_property("blend_parameters", params)
    ao.set_editor_property("interpolate_using_grid", True)
    ao.set_editor_property("preview_base_pose", unreal.load_asset("%s/%s" % (FPARMS_DIR, FPARMS_AO_BASE)))
    samples = []
    for clip, x, y in clips:
        s = unreal.BlendSample()
        s.set_editor_property("animation", clip)
        s.set_editor_property("sample_value", unreal.Vector(x, y, 0.0))
        s.set_editor_property("rate_scale", 1.0)
        samples.append(s)
    ao.set_editor_property("sample_data", samples)

    report = blend_space_report(ao)
    # 3 x 3 grid points, each played by exactly the pose placed on it; Mesh Space samples (else the aim offset is invalid).
    ok = report["grid_exact"] == len(FPARMS_AO_SAMPLES) and report["mesh_space_samples"]
    saved = bool(save and ok and unreal.EditorAssetLibrary.save_loaded_asset(ao, only_if_is_dirty=False))
    return {"asset": FPARMS_AO, "created": created, "ok": ok, "saved": saved, "report": report}


def _fparms_rod_aim_chain(ed):
    """{key: node} of the rod-aim chain (FPARMS_AIM_NODES, found by Tag), or None if the graph has none of them."""
    tags = {_node_tag(n): n for n in ed.list_all_nodes()}
    chain = {key: tags.get(tag) for key, tag, _, _ in FPARMS_AIM_NODES}
    if not any(chain.values()):
        return None
    missing = [tag for key, tag, _, _ in FPARMS_AIM_NODES if chain[key] is None]
    if missing:
        raise RuntimeError("Rod-aim chain incomplete, no node tagged %s: run fparms_abp_wire_rod_aim()" % missing)
    return chain


def _fparms_link_rod_aim(ed, chain, player):
    """Hold Rod player -> aim offset (X <- RodAimYaw, Y <- RodAimPitch) -> Local to Component -> Two Bone IK ->
    Component to Local. Returns the number of new links; the caller links chain["c2l"] to the blend."""
    links = _connect(player, "Pose", chain["aim"], "BasePose")
    links += _connect(_getter(ed, "RodAimYaw"), "RodAimYaw", chain["aim"], "X")
    links += _connect(_getter(ed, "RodAimPitch"), "RodAimPitch", chain["aim"], "Y")
    links += _connect(chain["aim"], "Pose", chain["l2c"], "LocalPose")
    links += _connect(chain["l2c"], "ComponentPose", chain["ik"], "ComponentPose")
    links += _connect(chain["ik"], "Pose", chain["c2l"], "ComponentPose")
    return links


def _rod_aim_settings(chain):
    aim = chain["aim"].get_editor_property("node")
    ik = chain["ik"].get_editor_property("node")
    return {"aim": {"blend_space": _plain(aim.get_editor_property("blend_space")), "alpha": _plain(aim.get_editor_property("alpha"))},
            "ik": {"ik_bone": str(ik.get_editor_property("ik_bone").get_editor_property("bone_name")),
                   "effector": [_plain(ik.get_editor_property("effector_location_space")), _bone_of(ik.get_editor_property("effector_target"))],
                   "joint": [_plain(ik.get_editor_property("joint_target_location_space")), _bone_of(ik.get_editor_property("joint_target"))],
                   "take_rotation_from_effector_space": ik.get_editor_property("take_rotation_from_effector_space"),
                   "maintain_effector_rel_rot": ik.get_editor_property("maintain_effector_rel_rot"),
                   "allow_stretching": ik.get_editor_property("allow_stretching"), "alpha": _plain(ik.get_editor_property("alpha"))}}


def fparms_abp_wire_rod_aim(abp_path=FPARMS_ABP, save=True):
    """T-028 in ABP_FPArms (after fparms_rod_aim_offset). Idempotent. Creates the tagged rod-aim chain (FPARMS_AIM_NODES)
    on the Hold Rod branch, sets the aim offset (AO_FPArms_RodAim, alpha 1) and the Two Bone IK (FPARMS_IK), sets the class
    default bRodAimOffsetInGraph = true, then fparms_abp_wire() links, lays out, compiles and saves everything."""
    ao = unreal.load_asset(FPARMS_AO)
    if ao is None:
        raise RuntimeError("No %s: run fparms_rod_aim_offset() first" % FPARMS_AO)
    abp, ed = anim_graph(abp_path)
    created = []
    for key, tag, cls, type_id in FPARMS_AIM_NODES:
        if type_id is None:  # the aim offset's own entry (sets the asset and the axis pin names), else the generic player
            type_id = [s for s in menu_entries(ed, FPARMS_AO_NAME) if s.startswith("Animation|")]
            type_id.append("Animation|BlendSpaces|AimOffsetPlayer")
        node, new = tagged_node(ed, tag, cls, type_id)
        if new:
            created.append("%s (%s): %s" % (node.get_name(), tag, str(node.get_node_title()).replace("\n", " | ")))
    chain = _fparms_rod_aim_chain(ed)
    set_anim_node(chain["aim"], blend_space=ao, alpha=1.0)
    set_anim_node(chain["ik"], ik_bone=_bone_ref(FPARMS_IK["ik_bone"]),
                  effector_location_space=unreal.BoneControlSpace.BCS_BONE_SPACE, effector_target=_bone_target(FPARMS_IK["effector"]),
                  effector_location=unreal.Vector(0.0, 0.0, 0.0), take_rotation_from_effector_space=True,
                  maintain_effector_rel_rot=False, joint_target_location_space=unreal.BoneControlSpace.BCS_BONE_SPACE,
                  joint_target=_bone_target(FPARMS_IK["joint"]), joint_target_location=unreal.Vector(0.0, 0.0, 0.0),
                  allow_stretching=False, alpha=1.0)
    unreal.get_default_object(abp.generated_class()).set_editor_property("rod_aim_offset_in_graph", True)
    result = fparms_abp_wire(abp_path, save=save)
    abp = unreal.load_asset(abp_path)
    result["created"] = created
    result["settings"] = _rod_aim_settings(_fparms_rod_aim_chain(anim_graph(abp_path)[1]))
    result["cdo_rod_aim_offset_in_graph"] = unreal.get_default_object(abp.generated_class()).get_editor_property("rod_aim_offset_in_graph")
    return result


# ---------------- T-029 ABP_Fish (SK_Fish.anim.md "ABP_Fish"; FishAnimInstance.h) ----------------

FISH_DIR = "/Game/Art/Fish"
FISH_ABP_NAME = "ABP_Fish"
FISH_ABP = FISH_DIR + "/" + FISH_ABP_NAME
FISH_ENUM_BLEND = "Animation|Blends|BlendPoses(EFishAnimRole)"
# EFishAnimRole in enum order: (entry, pin label on the node, clip). SwimIdle (= 0) plays on the Default pin, which also
# plays every role without its own pin; the others get their own pins, mapped by name (enum_blend_pins).
FISH_ROLES = [
    ("SwimIdle", "Default", "A_Fish_Swim_Idle"),
    ("SwimFast", "Swim Fast", "A_Fish_Swim_Fast"),
    ("Thrash", "Thrash", "A_Fish_Hooked_Thrash"),
    ("Run", "Run", "A_Fish_Fight_Run"),
    ("Dive", "Dive", "A_Fish_Fight_Dive"),
    ("Dart", "Dart", "A_Fish_Fight_Dart"),
    ("Flop", "Flop", "A_Fish_Landed_Flop"),
    ("Curled", "Curled", "A_Fish_Curled"),
]
FISH_DART_CLIP = "A_Fish_Fight_Dart"            # its Start Position <- DartStartTime
FISH_CURLED_CLIP = "A_Fish_Curled"              # T-030f pose slot: Sequence <- DisplayPose, Start Position <- DisplayPoseTime
FISH_NODES = [  # (key, Tag, class, action-menu entry)
    ("ref", "FishRefPose", "AnimGraphNode_LocalRefPose", "Animation|Poses|LocalSpaceRefPose"),
    ("add", "FishAdditive", "AnimGraphNode_ApplyAdditive", "Animation|Blends|ApplyAdditive"),
    ("blend", "FishRoleBlend", "AnimGraphNode_BlendListByEnum", FISH_ENUM_BLEND),
]
FISH_NOTE = (
    "Fish (T-029). No logic here (CLAUDE.md rule 1): C++ UFishAnimInstance sets Role, PlayRate, Amplitude, RoleBlendTime "
    "and DartStartTime from the fight fish (or SetAnimState).\n"
    "Local Space Ref Pose (the PLAYING mesh's ref pose, so each species keeps its proportions) + Apply Additive (alpha = "
    "Amplitude) of Blend Poses (EFishAnimRole). Default pin = SwimIdle (enum 0, and any role without a pin). Clips = Local "
    "Space additives on A_Fish_Rest frame 0, all looping, Play Rate <- PlayRate, Dart Start Position <- DartStartTime, "
    "Child Update Mode = Reset Child On Activate (Thrash shakes first, Dart starts on its side).\n"
    "Curled (T-030f) is a pose slot for the cooler display: its player's Sequence <- DisplayPose, Start Position <- "
    "DisplayPoseTime (C++ SetHeldPose sets PlayRate 0, Amplitude 1, RoleBlendTime 0).\n"
    "New role: append it to EFishAnimRole in C++, right-click the blend > Add pin for element, add (entry, label, clip) to "
    "FISH_ROLES in Content/Python/pipeline_unreal.py, run fish_abp_prepare() then fish_abp_wire().")


def _players_by_clip(ed):
    """{clip name: Sequence Player node} (first player per clip)."""
    return _fparms_players(ed)


def _new_player(ed, clip, pos=(0, 0)):
    """A new Sequence Player for `clip` (additive clips have their own '(additive)' menu entry)."""
    for type_id in ("Animation|Sequences|Play'%s'" % clip, "Animation|Sequences|Play'%s'(additive)" % clip):
        node = ed.create_node_from_name(type_id, unreal.Vector2D(float(pos[0]), float(pos[1])), [])
        if node is not None:
            return node
    raise RuntimeError("No Sequence Player menu entry for %s (imported on this skeleton?); entries naming it: %s"
                       % (clip, menu_entries(ed, clip)[:6]))


def _fish_layout(ed, players, nodes, get=None):
    """Layout (graph units): getters | players | blend | ref pose + apply additive | output. prepare lays it out too, so
    no node sits on top of the blend when the UI step right-clicks its title."""
    for i, (_, _, clip) in enumerate(FISH_ROLES):
        players[clip].set_node_pos(unreal.IntPoint(0, -520 + 160 * i))
    nodes["blend"].set_node_pos(unreal.IntPoint(420, -330))
    nodes["ref"].set_node_pos(unreal.IntPoint(820, -520))
    nodes["add"].set_node_pos(unreal.IntPoint(1100, -400))
    for root in _nodes_of(ed, "AnimGraphNode_Root"):
        root.set_node_pos(unreal.IntPoint(1420, -400))
    for name, (x, y) in (("PlayRate", (-300, -60)), ("DartStartTime", (-300, 330)), ("Role", (230, 820)),
                         ("RoleBlendTime", (230, 900)), ("Amplitude", (860, -240)),
                         ("DisplayPose", (-300, 580)), ("DisplayPoseTime", (-300, 660))):
        if get and name in get:
            get[name].set_node_pos(unreal.IntPoint(x, y))


def fish_abp_prepare(abp_path=FISH_ABP):
    """ABP_Fish, step 1 of 3. Idempotent. Creates the Anim Blueprint if missing (parent UFishAnimInstance, skeleton
    SKEL_Fish, preview SK_Bonefish), one looping Sequence Player per FISH_ROLES clip with its Play Rate pin shown (and
    Start Position on the Dart player), the tagged Local Space Ref Pose, Apply Additive and Blend Poses (EFishAnimRole)
    (Standard Blend, Linear, Child Update Mode = Reset Child On Activate: UE 5.8's replacement for the deprecated
    "Reset Child on Activation" flag), lays them out (_fish_layout), compiles and opens the editor.

    Step 2 (UI, as for ABP_FPArms, see fparms_abp_prepare): expose the enum pins Swim Fast, Thrash, Run, Dive, Dart, Flop, Curled on
    the blend (right-click > Add pin for element; any order, fish_abp_wire maps them by name). Step 3: fish_abp_wire().
    """
    created = []
    if not unreal.EditorAssetLibrary.does_asset_exist(abp_path):
        factory = unreal.AnimBlueprintFactory()
        factory.set_editor_property("target_skeleton", unreal.load_asset(FISH_DIR + "/SKEL_Fish"))
        factory.set_editor_property("parent_class", unreal.FishAnimInstance)
        factory.set_editor_property("preview_skeletal_mesh", unreal.load_asset(FISH_DIR + "/SK_Bonefish"))
        folder, name = abp_path.rsplit("/", 1)
        if unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.AnimBlueprint, factory) is None:
            raise RuntimeError("Could not create " + abp_path)
        created.append(abp_path)
    abp, ed = anim_graph(abp_path)
    players = _players_by_clip(ed)
    shown = {}
    for _, _, clip in FISH_ROLES:
        node = players.get(clip)
        if node is None:
            node = _new_player(ed, clip)
            created.append(node.get_name())
        set_anim_node(node, sequence=unreal.load_asset("%s/%s" % (FISH_DIR, clip)),
                      loop_animation=(clip != FISH_CURLED_CLIP), play_rate=1.0)
        pins = {FISH_DART_CLIP: ("PlayRate", "StartPosition"),
                FISH_CURLED_CLIP: ("PlayRate", "StartPosition", "Sequence")}.get(clip, ("PlayRate",))
        shown[clip] = show_anim_pins(node, *pins)
    nodes = {}
    for key, tag, cls, type_id in FISH_NODES:
        nodes[key], new = tagged_node(ed, tag, cls, type_id)
        if new:
            created.append("%s (%s)" % (nodes[key].get_name(), tag))
    set_anim_node(nodes["blend"], transition_type=unreal.BlendListTransitionType.STANDARD_BLEND,
                  blend_type=unreal.AlphaBlendOption.LINEAR,
                  child_upate_mode=unreal.BlendListChildUpdateMode.RESET_CHILD_ON_ACTIVATE)  # 5.8: replaces bResetChildOnActivation
    _fish_layout(ed, _players_by_clip(ed), nodes)
    unreal.BlueprintEditorLibrary.refresh_open_editors_for_blueprint(abp)
    compiled = compile_anim_blueprint(abp)
    open_asset_editors(abp)
    exposed = enum_blend_entries(nodes["blend"])
    todo = [label for entry, label, _ in FISH_ROLES[1:] if entry not in exposed]
    return {"created": created, "pins_shown": shown, "blend": nodes["blend"].get_name(),
            "title": str(nodes["blend"].get_node_title()), "exposed": exposed, "compile": compiled,
            "next": ("expose %s via the blend's context menu, then fish_abp_wire()" % todo) if todo else "fish_abp_wire()"}


def fish_abp_wire(abp_path=FISH_ABP, save=True):
    """ABP_Fish, step 3 of 3 (after fish_abp_prepare and the UI step). Idempotent. Players -> their Blend Poses pins BY
    NAME, Get Role -> Active Enum Value, Get RoleBlendTime -> every Blend Time pin, Get PlayRate -> every player's Play
    Rate, Get DartStartTime -> the Dart player's Start Position; Local Space Ref Pose -> Apply Additive Base, blend ->
    Additive, Get Amplitude -> Alpha, -> Output Pose; note comment; compile; save if clean."""
    abp, ed = anim_graph(abp_path)
    nodes = {}
    for key, tag, cls, _ in FISH_NODES:
        found = [n for n in ed.list_all_nodes() if _node_tag(n) == tag]
        if len(found) != 1:
            raise RuntimeError("Need exactly one node tagged %s, found %d: run fish_abp_prepare()" % (tag, len(found)))
        nodes[key] = found[0]
    blend = nodes["blend"]
    pins = enum_blend_pins(blend, FISH_ROLES[0][0])
    labels = {entry: label for entry, label, _ in FISH_ROLES}
    missing = [labels[entry] for entry, _, _ in FISH_ROLES if entry not in pins]
    if missing:
        raise RuntimeError("Expose %s on %s first (right-click > Add pin for element). Pins now: %s" % (missing, blend.get_name(), pins))
    unknown = [entry for entry in pins if entry not in labels]
    if unknown:
        raise RuntimeError("%s has pins for %s, which FISH_ROLES doesn't list" % (blend.get_name(), unknown))
    players = _players_by_clip(ed)
    missing = [clip for _, _, clip in FISH_ROLES if clip not in players]
    if missing:
        raise RuntimeError("No Sequence Player for %s: run fish_abp_prepare()" % missing)
    roots = _nodes_of(ed, "AnimGraphNode_Root")
    if len(roots) != 1:
        raise RuntimeError("Expected one Output Pose node, found %d" % len(roots))

    get = {name: _getter(ed, name) for name in ("Role", "RoleBlendTime", "PlayRate", "DartStartTime", "Amplitude",
                                                "DisplayPose", "DisplayPoseTime")}
    links = 0
    for entry, _, clip in FISH_ROLES:
        links += _connect(players[clip], "Pose", blend, pins[entry])
        links += _connect(get["PlayRate"], "PlayRate", players[clip], "PlayRate")
    links += _connect(get["DartStartTime"], "DartStartTime", players[FISH_DART_CLIP], "StartPosition")
    links += _connect(get["DisplayPose"], "DisplayPose", players[FISH_CURLED_CLIP], "Sequence")
    links += _connect(get["DisplayPoseTime"], "DisplayPoseTime", players[FISH_CURLED_CLIP], "StartPosition")
    links += _connect(get["Role"], "Role", blend, "ActiveEnumValue")
    for pose_pin in pins.values():
        links += _connect(get["RoleBlendTime"], "RoleBlendTime", blend, pose_pin.replace("BlendPose_", "BlendTime_"))
    links += _connect(nodes["ref"], "Pose", nodes["add"], "Base")
    links += _connect(blend, "Pose", nodes["add"], "Additive")
    links += _connect(get["Amplitude"], "Amplitude", nodes["add"], "Alpha")
    links += _connect(nodes["add"], "Pose", roots[0], "Result")

    _fish_layout(ed, players, nodes, get)
    note_graph(ed, "Fish (T-029)", FISH_NOTE, (-340 - NOTE_WIDTH, -520))  # left of the getters, level with the first player

    unreal.BlueprintEditorLibrary.refresh_open_editors_for_blueprint(abp)
    compiled = compile_anim_blueprint(abp)
    saved = False
    if save and compiled["clean"]:
        saved = unreal.EditorAssetLibrary.save_loaded_asset(abp, only_if_is_dirty=False)
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    return {"pins": pins, "new_links": links, "compile": compiled, "saved": bool(saved),
            "dirty_after_save": abp_path in dirty, "graph": anim_graph_report(abp_path)}


def build_golden_level(level_path="/Game/Maps/Dev/L_GoldenPath", mesh_path="/Game/Art/Props/SM_GoldenCrate"):
    """Create (or rebuild) a small lit test level with a floor and the golden crate, then save it."""
    les = _levels()
    eas = _actors()
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        if not les.load_level(level_path):
            raise RuntimeError("load_level failed: " + level_path)
        for a in eas.get_all_level_actors():
            if a.get_actor_label().startswith("GP_"):
                eas.destroy_actor(a)
    elif not les.new_level(level_path):
        raise RuntimeError("new_level failed: " + level_path)

    _spawn_class(unreal.DirectionalLight, "GP_Sun", (0.0, 0.0, 600.0), unreal.Rotator(roll=0.0, pitch=-50.0, yaw=-35.0))
    _spawn_class(unreal.SkyAtmosphere, "GP_SkyAtmosphere", (0.0, 0.0, 0.0))
    sky = _spawn_class(unreal.SkyLight, "GP_SkyLight", (0.0, 0.0, 300.0))
    try:
        sky.light_component.set_editor_property("real_time_capture", True)
    except Exception as exc:  # property names can change between engine versions
        _log("SkyLight real_time_capture not set: %s" % exc)
    _spawn_mesh("/Engine/BasicShapes/Plane", "GP_Floor", (0.0, 0.0, 0.0), (20.0, 20.0, 1.0))
    _spawn_mesh(mesh_path, "GP_Crate", (0.0, 0.0, 0.0))

    if not les.save_current_level():
        raise RuntimeError("save_current_level failed")
    view = None
    try:
        view = frame_viewport((0.0, 0.0, 50.0))
    except Exception as exc:  # no viewport in headless commandlet runs
        _log("frame_viewport skipped: %s" % exc)
    result = {"level": level_path, "actors": [a["label"] for a in list_level_actors()], "viewport": view}
    _log(json.dumps(result))
    return result

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
# unreal-mcp SlateInspectorToolset (recipe in fparms_abp_prepare), then continue in Python.

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
]
FPARMS_NOTE = (
    "Arms pose (T-006). No logic here (CLAUDE.md rule 1): C++ UFPArmsAnimInstance sets ArmsPose from DT_Movement "
    "RodPoseStill / RodPoseMoving and ArmsPoseBlendTime from RodPoseBlendTime.\n"
    "Blend Poses (EFPArmsPose): Default pin = Idle (enum 0, and any pose without its own pin), then Hold Rod, "
    "Prone Hold, Prone Tuck = pins 1-3. Standard Blend, Linear (the clearance in SK_FPArms.anim.md was measured "
    "with this). All four players loop in sync group FPArmsBreath.\n"
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
    """{clip name: Sequence Player node} for the FP arms clips already in the graph."""
    out = {}
    for n in _nodes_of(ed, "AnimGraphNode_SequencePlayer"):
        seq = n.get_editor_property("node").get_editor_property("sequence")
        if seq is not None:
            out.setdefault(seq.get_name(), n)
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
    for i, (_, _, clip) in enumerate(FPARMS_POSES):
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


def fparms_abp_wire(abp_path=FPARMS_ABP, save=True):
    """ABP_FPArms 4-pose graph, step 3 of 3 (after fparms_abp_prepare and the UI step). Idempotent.

    Players -> Blend Poses (EFPArmsPose) pins 0-3 (FPARMS_POSES order), Get ArmsPose -> Active Enum Value, Get
    ArmsPoseBlendTime -> every Blend Time pin, blend -> Slot DefaultSlot -> Slot StanceAdditive -> Output Pose, plus a
    note comment. Compiles and (if clean and save=True) saves. Returns the compile result and the graph report.
    """
    abp, ed = anim_graph(abp_path)
    blends = _nodes_of(ed, "AnimGraphNode_BlendListByEnum")
    if len(blends) != 1:
        raise RuntimeError("Expected one Blend Poses (EFPArmsPose) node, found %d: run fparms_abp_prepare()" % len(blends))
    blend = blends[0]
    pose_pins = _fparms_pose_pins(blend)
    if len(pose_pins) != len(FPARMS_POSES):
        raise RuntimeError("%d pose pins on %s, need %d: expose %s in its context menu first (step 2)"
                           % (len(pose_pins), blend.get_name(), len(FPARMS_POSES),
                              [label for _, label, _ in FPARMS_POSES[len(pose_pins):]]))
    players = _fparms_players(ed)
    missing = [clip for _, _, clip in FPARMS_POSES if clip not in players]
    if missing:
        raise RuntimeError("No Sequence Player for %s: run fparms_abp_prepare()" % missing)
    slots = {}
    for n in _nodes_of(ed, "AnimGraphNode_Slot"):
        slots[str(n.get_editor_property("node").get_editor_property("slot_name"))] = n
    for name in ("DefaultSlot", "StanceAdditive"):
        if name not in slots:
            raise RuntimeError("No Slot '%s' node in the graph" % name)
    roots = _nodes_of(ed, "AnimGraphNode_Root")
    if len(roots) != 1:
        raise RuntimeError("Expected one Output Pose node, found %d" % len(roots))

    links = 0
    for i, (_, _, clip) in enumerate(FPARMS_POSES):
        links += _connect(players[clip], "Pose", blend, "BlendPose_%d" % i)
    pose_get = _getter(ed, "ArmsPose")
    time_get = _getter(ed, "ArmsPoseBlendTime")
    links += _connect(pose_get, "ArmsPose", blend, "ActiveEnumValue")
    for i in range(len(FPARMS_POSES)):
        links += _connect(time_get, "ArmsPoseBlendTime", blend, "BlendTime_%d" % i)
    links += _connect(blend, "Pose", slots["DefaultSlot"], "Source")
    links += _connect(slots["DefaultSlot"], "Pose", slots["StanceAdditive"], "Source")
    links += _connect(slots["StanceAdditive"], "Pose", roots[0], "Result")

    # Layout, left to right (graph units): players | getters | blend | slots | output.
    for i, (_, _, clip) in enumerate(FPARMS_POSES):
        players[clip].set_node_pos(unreal.IntPoint(0, -360 + 170 * i))
    time_get.set_node_pos(unreal.IntPoint(230, 290))
    pose_get.set_node_pos(unreal.IntPoint(230, 370))
    blend.set_node_pos(unreal.IntPoint(420, -200))
    slots["DefaultSlot"].set_node_pos(unreal.IntPoint(800, -200))
    slots["StanceAdditive"].set_node_pos(unreal.IntPoint(1100, -200))
    roots[0].set_node_pos(unreal.IntPoint(1400, -200))
    notes = [c for c in ed.list_comment_nodes() if unreal.BlueprintEditorLibrary.get_comment_text(c).startswith("Arms pose (T-006)")]
    if notes:
        unreal.BlueprintEditorLibrary.set_comment_text(notes[0], FPARMS_NOTE)
    else:
        ed.add_comment_node(FPARMS_NOTE, unreal.Vector2D(0.0, -620.0), unreal.Vector2D(1650.0, 190.0))

    # Refresh the open editor BEFORE compiling and saving: a refresh after the save marks the package dirty again.
    unreal.BlueprintEditorLibrary.refresh_open_editors_for_blueprint(abp)
    compiled = compile_anim_blueprint(abp)
    saved = False
    if save and compiled["clean"]:
        saved = unreal.EditorAssetLibrary.save_loaded_asset(abp, only_if_is_dirty=False)
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    return {"new_links": links, "compile": compiled, "saved": bool(saved), "dirty_after_save": abp_path in dirty,
            "graph": anim_graph_report(abp_path)}


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

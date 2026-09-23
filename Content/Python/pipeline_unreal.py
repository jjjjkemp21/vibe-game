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

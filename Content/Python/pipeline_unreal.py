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

"""Generic Unreal level builder for Lure layouts (T-005). Editor only (imports `unreal`).

Reads data/levels/<Level>.json through levels.layout (the same expansion the Blender preview renders), then creates
or reloads the level, removes every actor this layout made before (actor tag "LureLayout=<layout id>"), spawns
engine basic shapes / our meshes with palette material instances, lights, fog and gameplay markers, and saves.
Actors placed by hand (without that tag) are never touched.

Editor-operator (vibegame_tools.VibeGamePipelineTools -> run_python), from the repo's Content/Python on sys.path:
    import importlib, levels.layout, levels.build_level as bl
    importlib.reload(levels.layout); importlib.reload(bl)
    result = bl.build("data/levels/L_PalmKey.json")          # relative paths resolve against the repo root
    # screenshots that match the Blender previews:
    bl.frame_view("data/levels/L_PalmKey.json", "overview")  # or any view id from the layout's "views"

What gets spawned (all tagged "LureLayout", "LureLayout=<id>", "LureId=<element id>" plus the element's own tags):
- primitives -> StaticMeshActor with /Engine/BasicShapes/* scaled from the mesh's real bounds, MI_Lvl_<mat id>
- props      -> StaticMeshActor with our mesh (pivot bottom center); a basic-shape stand-in if the asset is missing
- lights     -> DirectionalLight (atmosphere sun), SkyAtmosphere, SkyLight (real-time capture), ExponentialHeightFog,
                PointLights (movable, no shadows); fog values from the layout's fog_presets
- markers    -> PlayerStart (player_start, PlayerStartTag), TriggerBox (zone, extent = size / 2) or TargetPoint
                (everything else). Marker data is written as actor tags "Key=Value" (see marker_tags()). A layout may
                map a marker type to a gameplay class later: "marker_classes": {"fishing_spot": "/Script/VibeGame.X"}.
- labels     -> TextRenderActor (editor aid; hidden in game unless the layout says "labels_in_game": true)

Materials: /Game/Materials/Level/M_LevelPalette (params Color, Roughness, Emissive) is created once;
MI_Lvl_<mat id> instances are created or updated from the layout's "materials" (sRGB hex -> linear).
Material ids are global across layouts: the same id must mean the same color.
"""
import math
import os

import unreal

from levels import layout as L

PALETTE_DIR = "/Game/Materials/Level"
PALETTE_PARENT = PALETTE_DIR + "/M_LevelPalette"
TAG_ALL = "LureLayout"
PLAYER_START_Z = 100.0  # PlayerStart is placed this far above the floor point (capsule center + a little)


def _log(msg):
    unreal.log("[build_level] " + str(msg))


def _warn(msg):
    unreal.log_warning("[build_level] " + str(msg))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _les():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def layout_tag(layout_id):
    return "LureLayout=" + layout_id


# ---------------------------------------------------------------------------------------------------------------------
# Level lifecycle
# ---------------------------------------------------------------------------------------------------------------------
def open_or_create_level(level_path):
    """Load the level if it exists, else create an empty (non-partitioned) one. Returns "loaded" or "created"."""
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        if not _les().load_level(level_path):
            raise RuntimeError("load_level failed: " + level_path)
        return "loaded"
    if not _les().new_level(level_path, False):
        raise RuntimeError("new_level failed: " + level_path)
    return "created"


def destroy_actors_with_tag(tag):
    """Destroy every level actor carrying `tag`; returns how many were removed."""
    name = unreal.Name(tag)
    doomed = [a for a in _eas().get_all_level_actors() if a.actor_has_tag(name)]
    if doomed:
        _eas().destroy_actors(doomed)
    return len(doomed)


def _world_settings():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    return world.get_world_settings()


# ---------------------------------------------------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------------------------------------------------
def _srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_to_linear_color(hex_str):
    h = hex_str.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    return unreal.LinearColor(_srgb_to_linear(r), _srgb_to_linear(g), _srgb_to_linear(b), 1.0)


def ensure_palette_parent(path=PALETTE_PARENT):
    """Opaque lit parent material: BaseColor = Color, Roughness = Roughness, Emissive = Color * Emissive."""
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    folder, name = path.rsplit("/", 1)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 220)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.8)
    emis = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 360)
    emis.set_editor_property("parameter_name", "Emissive")
    emis.set_editor_property("default_value", 0.0)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 360)
    mel.connect_material_expressions(color, "", mul, "A")
    mel.connect_material_expressions(emis, "", mul, "B")
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
    return mat


def ensure_palette_instance(mat_id, spec, parent):
    """MI_Lvl_<mat_id> under /Game/Materials/Level, created or updated from {"color", "roughness", "emissive"}."""
    name = "MI_Lvl_" + mat_id
    path = PALETTE_DIR + "/" + name
    mel = unreal.MaterialEditingLibrary
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mi = tools.create_asset(name, PALETTE_DIR, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_vector_parameter_value(mi, "Color", hex_to_linear_color(spec["color"]))
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(spec.get("roughness", 0.8)))
    mel.set_material_instance_scalar_parameter_value(mi, "Emissive", float(spec.get("emissive", 0.0)))
    mel.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
    return mi


def ensure_palette(layout):
    parent = ensure_palette_parent()
    return {mid: ensure_palette_instance(mid, spec, parent) for mid, spec in layout["materials"].items()}


# ---------------------------------------------------------------------------------------------------------------------
# Spawning helpers
# ---------------------------------------------------------------------------------------------------------------------
def _rot(yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def _vec(v):
    return unreal.Vector(float(v[0]), float(v[1]), float(v[2]))


def _finish_actor(actor, layout_id, elem_id, label, folder, tags):
    actor.set_actor_label(label)
    actor.set_folder_path(unreal.Name(folder))
    all_tags = [TAG_ALL, layout_tag(layout_id), "LureId=" + elem_id] + [str(t) for t in tags or []]
    actor.set_editor_property("tags", [unreal.Name(t) for t in all_tags])
    return actor


class _MeshCache(object):
    def __init__(self):
        self.meshes = {}
        self.missing = set()

    def get(self, path):
        if path not in self.meshes:
            asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
            if asset is None or not isinstance(asset, unreal.StaticMesh):
                self.missing.add(path)
                asset = None
            self.meshes[path] = asset
        return self.meshes[path]


def _configure_mesh_component(actor, mat, collision, shadow, visible):
    comp = actor.static_mesh_component
    if mat is not None:
        for i in range(max(1, comp.get_num_materials())):
            comp.set_material(i, mat)
    if collision == "none":
        comp.set_collision_profile_name(unreal.Name("NoCollision"))
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    if not shadow:
        comp.set_cast_shadow(False)
    if not visible:
        actor.set_actor_hidden_in_game(True)


def spawn_prim(prim, layout_id, mats, cache):
    """A basic-shape primitive placed by its bounding box (layout convention), or a prop mesh placed by its pivot."""
    rows = L.rot_rows(prim["yaw"], prim["pitch"], prim["roll"])
    rot = _rot(prim["yaw"], prim["pitch"], prim["roll"])
    if prim.get("mesh"):
        mesh = cache.get(prim["mesh"])
        if mesh is not None:
            actor = _eas().spawn_actor_from_object(mesh, _vec(prim["pivot"]), rot)
            actor.set_actor_scale3d(_vec(prim["scale"]))
            _configure_mesh_component(actor, None, prim["collision"], prim["shadow"], prim["visible"])
            return _finish_actor(actor, layout_id, prim["id"], prim["id"].replace("/", "."),
                                 "%s/%s" % (layout_id, prim.get("group") or "props"), prim["tags"])
        _warn("mesh %s missing for %s: spawning its basic-shape stand-in" % (prim["mesh"], prim["id"]))
    shape_path = L.ENGINE_SHAPES[prim["shape"]]
    mesh = cache.get(shape_path)
    if mesh is None:
        raise RuntimeError("Engine basic shape missing: " + shape_path)
    bounds = mesh.get_bounds()
    ext, org = bounds.box_extent, bounds.origin
    sx = prim["size"][0] / (2.0 * ext.x) if ext.x > 1e-3 else 1.0
    sy = prim["size"][1] / (2.0 * ext.y) if ext.y > 1e-3 else 1.0
    sz = prim["size"][2] / (2.0 * ext.z) if ext.z > 1e-3 else 1.0
    # Actor location so that the mesh's bounding-box center lands on prim["center"].
    off = L.rotate(rows, (org.x * sx, org.y * sy, org.z * sz))
    loc = L.sub(prim["center"], off)
    actor = _eas().spawn_actor_from_object(mesh, _vec(loc), rot)
    actor.set_actor_scale3d(unreal.Vector(sx, sy, sz))
    _configure_mesh_component(actor, mats.get(prim["mat"]), prim["collision"], prim["shadow"], prim["visible"])
    return _finish_actor(actor, layout_id, prim["id"], prim["id"].replace("/", "."),
                         "%s/%s" % (layout_id, prim.get("group") or prim.get("kind") or "blocks"), prim["tags"])


def spawn_light(light, layout, layout_id):
    t = light["type"]
    lid = light["id"]
    if t == "directional":
        r = light.get("rot", {})
        actor = _eas().spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000),
                                              _rot(r.get("yaw", 0), r.get("pitch", -50), r.get("roll", 0)))
        comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_intensity(float(light.get("intensity", 10.0)))
        comp.set_light_color(hex_to_linear_color(light.get("color", "#FFFFFF")), True)
        comp.set_atmosphere_sun_light(True)
    elif t == "sky_atmosphere":
        actor = _eas().spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    elif t == "sky_light":
        actor = _eas().spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 800))
        comp = actor.get_component_by_class(unreal.SkyLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_real_time_capture(bool(light.get("real_time_capture", True)))
    elif t == "height_fog":
        actor = _eas().spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
        comp = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
        preset = (layout.get("fog_presets") or {}).get(light.get("preset", ""), {})
        preset = dict(preset, **{k: v for k, v in light.items() if k in ("density", "height_falloff", "color",
                                                                        "start_distance")})
        if "density" in preset:
            comp.set_fog_density(float(preset["density"]))
        if "height_falloff" in preset:
            comp.set_fog_height_falloff(float(preset["height_falloff"]))
        if "color" in preset:
            comp.set_fog_inscattering_color(hex_to_linear_color(preset["color"]))
        if "start_distance" in preset:
            comp.set_start_distance(float(preset["start_distance"]))
    elif t == "point":
        actor = _eas().spawn_actor_from_class(unreal.PointLight, _vec(light["at"]))
        comp = actor.get_component_by_class(unreal.PointLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        if light.get("units", "candelas") == "candelas":
            comp.set_intensity_units(unreal.LightUnits.CANDELAS)
        comp.set_intensity(float(light.get("intensity", 8.0)))
        comp.set_attenuation_radius(float(light.get("radius", 1500.0)))
        comp.set_light_color(hex_to_linear_color(light.get("color", "#E8C46A")), True)
        comp.set_cast_shadows(bool(light.get("shadows", False)))
    else:
        _warn("unknown light type %r (%s)" % (t, lid))
        return None
    return _finish_actor(actor, layout_id, lid, "light." + lid.replace("/", "."), layout_id + "/lights",
                         ["Lure.Light"] + light.get("tags", []))


def _fmt(v):
    if isinstance(v, float):
        return ("%.2f" % v).rstrip("0").rstrip(".")
    if isinstance(v, (list, tuple)):
        return ",".join(_fmt(x) for x in v)
    return str(v)


def marker_tags(mk):
    """Actor tags that carry a marker's data ("Key=Value"), so C++ can read markers without the JSON:
    fishing_spot: Lure.FishingSpot, Spot, Habitat, Region, Radius, Hours (s-e;s-e), Levels, Luck, Danger, CastFrom
    patrol point: Lure.PatrolPoint, Patrol, Owner, Index, Closed, Speed
    sight_cone:   Lure.SightCone, Owner, Yaw, HalfAngle, Range, SweepMin, SweepMax
    zone:         Lure.Zone, ZoneType, Owner
    others:       Lure.<Type> plus Name/Role/Stance/Vs/Expect where present."""
    t = mk["type"]
    tags = []
    if t == "fishing_spot":
        tags += ["Lure.FishingSpot", "Spot=" + mk["id"], "Habitat=" + mk["habitat"], "Region=" + mk["region"],
                 "Radius=" + _fmt(float(mk["radius"])), "Hours=" + ";".join("%s-%s" % (a, b) for a, b in mk["hours"]),
                 "Levels=" + "-".join(str(x) for x in mk.get("level_band", [])), "Luck=" + _fmt(float(mk.get("luck", 0.0))),
                 "Danger=" + mk.get("danger", "none"), "CastFrom=" + _fmt([float(c) for c in mk["cast_from"]])]
    elif t == "sight_cone":
        tags += ["Lure.SightCone", "Owner=" + mk.get("owner", ""), "Yaw=" + _fmt(float(mk["yaw"])),
                 "HalfAngle=" + _fmt(float(mk.get("half_angle", 35))), "Range=" + _fmt(float(mk["range"]))]
        if mk.get("sweep"):
            tags += ["SweepMin=" + _fmt(float(mk["sweep"][0])), "SweepMax=" + _fmt(float(mk["sweep"][1]))]
    elif t == "zone":
        tags += ["Lure.Zone", "ZoneType=" + mk["zone_type"], "Owner=" + mk.get("owner", "")]
    elif t == "teleport":
        tags += ["Lure.Teleport", "Teleport=" + mk["id"]]
    elif t == "npc":
        tags += ["Lure.NPCSpot", "Role=" + mk.get("role", "")]
    elif t == "boat_mooring":
        tags += ["Lure.BoatMooring"]
    elif t == "landmark":
        tags += ["Lure.Landmark"]
    elif t in ("cover_test", "clearance_test"):
        tags += ["Lure.DesignTest", "Test=" + t]
        for k in ("stance", "vs", "expect", "expect_min", "expect_max"):
            if k in mk:
                tags.append("%s%s=%s" % (k[0].upper(), k[1:], _fmt(mk[k])))
    if mk.get("name"):
        tags.append("Name=" + mk["name"])
    return tags + list(mk.get("tags", []))


def _marker_class(layout, mtype, default_cls):
    path = (layout.get("marker_classes") or {}).get(mtype)
    if path:
        try:
            cls = unreal.load_class(None, path)
        except Exception:
            cls = None
        if cls is not None:
            return cls
        _warn("marker class %s for %s not found; using %s" % (path, mtype, default_cls.__name__))
    return default_cls


def spawn_label(text, at, layout_id, elem_id, size, yaw, in_game):
    actor = _eas().spawn_actor_from_class(unreal.TextRenderActor, _vec(at), _rot(yaw))
    comp = actor.text_render
    comp.set_text(unreal.Text(text))
    comp.set_world_size(float(size))
    comp.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
    comp.set_text_render_color(unreal.Color(r=43, g=42, b=38, a=255))
    if not in_game:
        actor.set_actor_hidden_in_game(True)
    return _finish_actor(actor, layout_id, elem_id, "label." + elem_id.replace("/", "."), layout_id + "/labels",
                         ["Lure.EditorLabel"])


def spawn_marker(mk, layout, layout_id, label_opts):
    t = mk["type"]
    folder = "%s/markers/%s" % (layout_id, t)
    spawned = []
    if t == "label":
        a = spawn_label(mk["text"], mk["at"], layout_id, mk["id"], mk.get("size", label_opts["size"]),
                        mk.get("yaw", label_opts["yaw"]), label_opts["in_game"])
        return [a]
    if t == "patrol":
        n = len(mk["points"])
        for i, p in enumerate(mk["points"]):
            nxt = mk["points"][(i + 1) % n]
            yaw = L.yaw_to(L.v3(p), L.v3(nxt))
            a = _eas().spawn_actor_from_class(_marker_class(layout, "patrol", unreal.TargetPoint), _vec(p), _rot(yaw))
            tags = ["Lure.PatrolPoint", "Patrol=" + mk["id"], "Owner=" + mk.get("owner", ""), "Index=%d" % i,
                    "Closed=" + ("true" if mk.get("closed", True) else "false"),
                    "Speed=" + _fmt(float(mk.get("speed_cm_s", 0)))]
            spawned.append(_finish_actor(a, layout_id, "%s/%02d" % (mk["id"], i), "%s.%02d" % (mk["id"], i), folder, tags))
        return spawned
    at = L.v3(mk["at"])
    yaw = float(mk.get("yaw", 0.0))
    if t == "player_start":
        cls = _marker_class(layout, t, unreal.PlayerStart)
        a = _eas().spawn_actor_from_class(cls, unreal.Vector(at[0], at[1], at[2] + PLAYER_START_Z), _rot(yaw))
        if mk.get("player_start_tag"):
            try:
                a.set_editor_property("player_start_tag", unreal.Name(mk["player_start_tag"]))
            except Exception as exc:
                _warn("player_start_tag not set on %s: %s" % (mk["id"], exc))
        tags = ["Lure.PlayerStart"] + list(mk.get("tags", []))
    elif t == "zone":
        cls = _marker_class(layout, t, unreal.TriggerBox)
        a = _eas().spawn_actor_from_class(cls, _vec(at), _rot(yaw))
        box = a.get_component_by_class(unreal.BoxComponent)
        if box is not None:
            s = mk["size"]
            box.set_box_extent(unreal.Vector(s[0] / 2.0, s[1] / 2.0, s[2] / 2.0))
        tags = marker_tags(mk)
    else:
        cls = _marker_class(layout, t, unreal.TargetPoint)
        a = _eas().spawn_actor_from_class(cls, _vec(at), _rot(yaw))
        tags = marker_tags(mk)
    spawned.append(_finish_actor(a, layout_id, mk["id"], mk["id"].replace("/", "."), folder, tags))
    name = mk.get("name")
    if name and mk.get("label", True) and t in ("fishing_spot", "npc", "landmark", "zone", "sight_cone", "boat_mooring",
                                                "player_start", "teleport"):
        text = name
        if t == "fishing_spot":
            text = "%s\n%s  L%s  %s" % (name, mk["habitat"], "-".join(str(x) for x in mk.get("level_band", [])),
                                        mk.get("time_label", ""))
        lab_at = L.v3(mk.get("label_at", at))
        spawned.append(spawn_label(text, (lab_at[0], lab_at[1], lab_at[2] + label_opts["lift"]), layout_id,
                                   mk["id"] + "/label", label_opts["size"], label_opts["yaw"], label_opts["in_game"]))
    return spawned


# ---------------------------------------------------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------------------------------------------------
def build(layout_path, level_path=None, save=True, frame=True, labels=True, label_yaw=180.0, label_size=None):
    """Create or rebuild the level described by layout_path. Returns a summary dict (JSON-friendly)."""
    lay = L.load(layout_path)
    ex = L.expand(lay)
    problems = L.validate(lay, ex)
    errors = [p for p in problems if p.startswith("ERROR")]
    if errors:
        raise RuntimeError("Layout %s has errors:\n%s" % (layout_path, "\n".join(errors)))
    layout_id = lay["id"]
    level_path = level_path or lay["level_path"]
    state = open_or_create_level(level_path)
    removed = destroy_actors_with_tag(layout_tag(layout_id))
    mats = ensure_palette(lay)
    cache = _MeshCache()
    counts = {"prims": 0, "props": 0, "lights": 0, "markers": 0, "labels": 0}
    for prim in ex["prims"]:
        spawn_prim(prim, layout_id, mats, cache)
        counts["props" if prim.get("mesh") else "prims"] += 1
    for light in ex["lights"]:
        if spawn_light(light, lay, layout_id) is not None:
            counts["lights"] += 1
    label_opts = {"size": label_size or float(lay.get("label_size", 120.0)), "yaw": float(label_yaw),
                  "in_game": bool(lay.get("labels_in_game", False)), "lift": float(lay.get("label_lift", 300.0))}
    for mk in ex["markers"]:
        if not labels and mk["type"] == "label":
            continue
        for a in spawn_marker(mk if labels else dict(mk, label=False), lay, layout_id, label_opts):
            if a.actor_has_tag(unreal.Name("Lure.EditorLabel")):
                counts["labels"] += 1
            else:
                counts["markers"] += 1
    ws = _world_settings()
    world_cfg = lay.get("world") or {}
    if "game_mode_override" in world_cfg and world_cfg["game_mode_override"] is None:
        try:
            ws.set_editor_property("default_game_mode", None)
        except Exception as exc:
            _warn("could not clear the game mode override: %s" % exc)
    saved = bool(_les().save_current_level()) if save else False
    view = None
    if frame:
        try:
            view = frame_view(layout_path, "overview")
        except Exception as exc:  # no viewport in commandlet runs
            _warn("frame_view skipped: %s" % exc)
    result = {"layout": layout_id, "level": level_path, "level_state": state, "removed_actors": removed,
              "spawned": counts, "missing_meshes": sorted(cache.missing), "warnings": [p for p in problems if p.startswith("WARN")],
              "materials": sorted("MI_Lvl_" + m for m in mats), "saved": saved, "view": view}
    _log(result)
    return result


def frame_view(layout_path, view_id="overview"):
    """Point the level viewport at a layout view (same eye, target and FOV as the Blender preview's eye_<id>.png),
    or at "overview": high above the south of the island looking north-down over the preview's overview map."""
    lay = L.load(layout_path)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    # A layout view named "overview" (rendered as eye_overview.png) wins over the map-based fallback.
    if view_id == "overview" and not any(x.get("id") == "overview" for x in lay.get("views", [])):
        maps = (lay.get("preview") or {}).get("maps") or [{"center": [0, 0, 0], "width": 10000}]
        c = L.v3(maps[0]["center"])
        span = float(maps[0]["width"])
        eye = (c[0] - span * 0.55, c[1], span * 0.55)
        target = (c[0] + span * 0.05, c[1], 0.0)
        fov = 90.0
    else:
        v = [x for x in lay.get("views", []) if x["id"] == view_id]
        if not v:
            raise KeyError("No view %r in %s" % (view_id, layout_path))
        v = v[0]
        if "eye_abs" in v:
            eye = L.v3(v["eye_abs"])
        else:
            f = L.v3(v["eye"])
            eye = (f[0], f[1], f[2] + L.eye_height(lay, v.get("stance", "stand")))
        target = L.v3(v["look_at"])
        fov = float(v.get("fov", 90.0))
    d = L.sub(target, eye)
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    ues.set_level_viewport_camera_info(_vec(eye), _rot(yaw, pitch, 0.0))
    try:
        les = _les()
        les.set_level_viewport_fov(fov, les.get_active_viewport_config_key())
    except Exception as exc:
        _warn("viewport FOV not set: %s" % exc)
    return {"view": view_id, "eye": [round(x, 1) for x in eye], "target": [round(x, 1) for x in target], "fov": fov}

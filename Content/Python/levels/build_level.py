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
                PointLights (movable, no shadows; "falloff": "soft" = no inverse-square hotspot) and an unbound
                PostProcessVolume ("post_process") with the fixed exposure. Exposure, fog, sky and sky-light values come
                from the layout's active time-of-day preset ("time_of_day" -> "time_of_day_presets"; see
                levels.layout.time_of_day). Fog colors are on-screen targets: converted through the fixed exposure and
                the engine's filmic tonemapper (levels.layout.on_screen_to_scene).
- markers    -> PlayerStart (player_start, PlayerStartTag), TriggerBox (zone, extent = size / 2) or TargetPoint
                (everything else). Marker data is written as actor tags "Key=Value" (see marker_tags()). A layout may
                map a marker type to a gameplay class later: "marker_classes": {"fishing_spot": "/Script/VibeGame.X"}.
                Any marker may carry "properties": {"PropName": value}; after spawning, each is applied with
                set_editor_property (CamelCase names are tried as given and as snake_case; strings go to FName
                properties as unreal.Name, lists of 3 numbers to vector properties). Unknown or rejected properties log
                a warning and the build continues. Generic: sell points, ladders, water volumes, ...
- water      -> (T-026, docs/specs/swimming.md) "water_volume" markers spawn ALureWaterVolume: location = the center of the
                water surface ("at"), yaw from the marker, then SetWaterSize(surface_half_size, water_depth) so the
                overlap box is rebuilt at once. "ladder" markers spawn ALureLadder (origin on the dock/rock face at the
                water line, +X out over the water), with "properties" such as MaxClimbHeight. Both classes are the
                defaults below and can be overridden in the layout's "marker_classes".
- water areas (T-027, docs/specs/fishing-water-rules.md) -> "water_area" markers spawn ALureWaterArea at "at" (a polygon
                without "at" sits at its centroid, an "everywhere" area at the origin) with the marker's yaw, then
                _configure_water_area() sets AreaId, DisplayName, Priority, Luck, the depth band, the habitat/region tags
                (SetAreaTags) and the outline (SetShapeCircle / SetShapeBox / SetShapePolygon with world X/Y points /
                SetShapeEverywhere). A "hot_spots" marker spawns ALureHotSpotSpawner (HotSpotTypes, MaxHotSpots, RandomSeed).
- labels     -> TextRenderActor (editor aid; hidden in game unless the layout says "labels_in_game": true)

Materials: /Game/Materials/Level/M_LevelPalette (params Color, Roughness, Emissive) is created once;
MI_Lvl_<mat id> instances are created or updated from the layout's "materials" (sRGB hex -> linear).
Material ids are global across layouts: the same id must mean the same values in every layout.
Water ids (ids starting with "water", or "water": true) get MI_Lvl_<id> under /Game/Materials/Level/M_LevelWater
instead (T-029): clear, depth-faded Thin Translucent water, built and versioned by this file (see "Water" below for
the model, the layout keys shallow_color / deep_color / tint_color / opacity_min / opacity_max / fade_depth /
tint_depth / specular / coverage / sort_priority, and the rule "one water surface per body of water").
"""
import math
import os

import unreal

from levels import layout as L

PALETTE_DIR = "/Game/Materials/Level"
PALETTE_PARENT = PALETTE_DIR + "/M_LevelPalette"
TAG_ALL = "LureLayout"
DEFAULT_MARKER_CLASSES = {
    "water_volume": "/Script/VibeGame.LureWaterVolume",
    "ladder": "/Script/VibeGame.LureLadder",
    "water_area": "/Script/VibeGame.LureWaterArea",
    "hot_spots": "/Script/VibeGame.LureHotSpotSpawner",
}
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


def _mi_matches(mi, parent, scalars, vectors, exact):
    """True when mi already has this parent and these scalar/vector overrides (exact: and no other overrides).
    Reads the instance's own override arrays, so a value inherited from the parent counts as missing."""
    if mi.get_editor_property("parent") != parent:
        return False
    have_s = {str(v.parameter_info.name): v.parameter_value for v in mi.get_editor_property("scalar_parameter_values")}
    have_v = {str(v.parameter_info.name): v.parameter_value for v in mi.get_editor_property("vector_parameter_values")}
    if exact and (set(have_s) != set(scalars) or set(have_v) != set(vectors)):
        return False
    def differs(a, b):  # stored values are float32: relative tolerance
        return abs(a - b) > 1e-5 * max(1.0, abs(b))
    for k, want in scalars.items():
        if k not in have_s or differs(have_s[k], want):
            return False
    for k, want in vectors.items():
        c = have_v.get(k)
        if c is None or any(differs(a, b) for a, b in ((c.r, want.r), (c.g, want.g), (c.b, want.b), (c.a, want.a))):
            return False
    return True


def ensure_palette_instance(mat_id, spec, parent):
    """MI_Lvl_<mat_id> under /Game/Materials/Level, created or updated from {"color", "roughness", "emissive"}."""
    name = "MI_Lvl_" + mat_id
    path = PALETTE_DIR + "/" + name
    mel = unreal.MaterialEditingLibrary
    scalars = {"Roughness": float(spec.get("roughness", 0.8)), "Emissive": float(spec.get("emissive", 0.0))}
    vectors = {"Color": hex_to_linear_color(spec["color"])}
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
        if _mi_matches(mi, parent, scalars, vectors, exact=False):
            return mi  # unchanged: don't re-save (keeps the .uasset out of git diffs)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mi = tools.create_asset(name, PALETTE_DIR, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    for k, v in vectors.items():
        mel.set_material_instance_vector_parameter_value(mi, k, v)
    for k, v in scalars.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
    return mi


# --- Water (T-029: "the water has to be clear to see the fish being reeled in") ---------------------------------------
# M_LevelWater is the parent of every water material id (is_water_material). Unlike M_LevelPalette it is REBUILT in
# place whenever WATER_GRAPH_VERSION differs from the version stamped on the asset (metadata tag LureWaterVersion):
# change the graph in _build_water_graph, bump the version, rebuild a level. Never edit the graph by hand in the editor.
#
# How it looks (the per-pixel model; all depths in cm):
#   depth = vertical water depth under this surface pixel, reconstructed from the opaque scene behind it
#           (seabed, fish, bobber, line: anything opaque) = (CameraZ - PixelZ) * (SceneDepth - PixelDepth) / PixelDepth.
#           Vertical, not along the view ray, so a fish 50 cm down reads the same from the dock or from far away.
#   f     = saturate(depth / FadeDepth)                 0 at the surface, 1 at FadeDepth and deeper
#   body  = lerp(Color, DeepColor, f)                   the water's own (in-scattered) colour
#   cover = lerp(OpacityMin, OpacityMax, f)             how much of the pixel is that body colour
#   tint  = TintColor ^ (depth / TintDepth)             coloured filter on what is seen through the water (per channel,
#                                                       Beer-Lambert): exactly TintColor at TintDepth; red fades first,
#                                                       as in real water. Keep TintColor bluer than Color: a filter
#                                                       with green > blue turns yellow sand green
#   Shading model Thin Translucent (Substrate: coloured transmittance): the seabed is MULTIPLIED by tint * (1 - cover)
#   and the lit body colour is added on top, so sand in the shallows turns turquoise instead of washing out to pale
#   khaki (plain alpha blending cannot remove red from sand). Sun glints and sky reflection are always at full
#   strength (Specular 0.25 = water's F0 0.02, Fresnel from the engine), so grazing views read as water, not glass.
#   Coverage scales the whole layer (0 = invisible): use it for a water prim lying on top of another water surface.
#
# Parameters (layout material keys in brackets; defaults in WATER_DEFAULTS):
#   Color [shallow_color, else color], DeepColor [deep_color, else color], TintColor [tint_color],
#   OpacityMin [opacity_min], OpacityMax [opacity_max], FadeDepth [fade_depth], TintDepth [tint_depth],
#   Roughness [roughness], Specular [specular], Coverage [coverage]. Colours are sRGB hex like the palette.
#   One water surface per body of water: two water prims stacked on each other blend twice (darker, greener, double
#   glints, visible seams at their edges). A prim that only marks an area on top of other water gets coverage 0.
# Per-prim: translucent sort priority [sort_priority] (default WATER_SORT_PRIORITY): water draws before other
#   translucency (bubbles, ripples, VFX at the surface), which then sorts on top of it.
# Engine rules this relies on (UE 5.8, Substrate on): Thin Translucent needs blend mode Translucent + lighting mode
# Surface ForwardShading + the ThinTranslucentMaterialOutput node (TransmittanceColor defaults to 0.5 grey if left
# unconnected) and must not render "After Motion Blur". Fog is computed per pixel: the sea is one 800 m quad, and
# per-vertex fog would fog it from its far corners.
# Known: faint light streaks radiating from the camera on deep water come from Lumen's translucency GI volume (a
# froxel grid lighting the body colour); r.Lumen.TranslucencyVolume.Enable 0 removes them (checked 2026-09-23). That
# is a project renderer setting (it changes GI for all translucency), so it is left for the lead to decide.
WATER_PARENT = PALETTE_DIR + "/M_LevelWater"
# Bump when _build_water_graph changes. Tuning WATER_DEFAULTS alone needs no bump: every build re-applies all
# parameters to the MI_Lvl_ instances; the master's own defaults (a copy of WATER_DEFAULTS at its last graph build)
# only show in the material editor preview.
WATER_GRAPH_VERSION = "4"
WATER_VERSION_TAG = "LureWaterVersion"
WATER_SORT_PRIORITY = -10
WATER_DEFAULTS = {             # tropical defaults; another region's water sets its own keys (e.g. murky: a brown
                               # tint_color, high opacity_min, short fade_depth)
    "tint_color": "#40E0F8",   # white sand seen through TintDepth of water: red gone, blue kept (bluer than Color,
                               # so yellow sand turns turquoise, not green)
    "opacity_min": 0.12,       # at the surface: sand and a fish at 0-1.5 m stay visible (a 50 cm-deep bonefish reads
                               # from the dock at 5 m and still shows at 10 m, the dock_end fight distance)
    "opacity_max": 0.92,       # at FadeDepth and deeper: the 8 m sea floor is only a hint
    "fade_depth": 450.0,
    "tint_depth": 200.0,       # NB grazing views tint and darken faster: Substrate's thin slab raises the
                               # transmittance to 1/cos(view angle), i.e. Beer-Lambert along the view ray
    "roughness": 0.3,
    "specular": 0.25,
    "coverage": 1.0,
}
_WATER_VECTORS = [("DeepColor", "deep_color"), ("TintColor", "tint_color")]
_WATER_SCALARS = [("OpacityMin", "opacity_min"), ("OpacityMax", "opacity_max"), ("FadeDepth", "fade_depth"),
                  ("TintDepth", "tint_depth"), ("Roughness", "roughness"), ("Specular", "specular"),
                  ("Coverage", "coverage")]


def is_water_material(mat_id, spec):
    """Water ids (T-029): ids starting with "water" or specs with "water": true use M_LevelWater."""
    return mat_id.startswith("water") or bool(spec.get("water"))


def _water_graph_version(mat):
    try:
        return unreal.EditorAssetLibrary.get_metadata_tag(mat, WATER_VERSION_TAG)
    except Exception:
        return ""


def _build_water_graph(mat):
    """Nodes and settings of M_LevelWater (see the model above). Positions only matter for reading it in the editor."""
    mel = unreal.MaterialEditingLibrary

    def node(cls, x, y, **props):
        e = mel.create_material_expression(mat, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def scalar(name, default, x, y, prio):
        return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=float(default),
                    group="Water", sort_priority=prio)

    def vector(name, hex_str, x, y, prio):
        return node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                    default_value=hex_to_linear_color(hex_str), group="Water", sort_priority=prio)

    def link(src, dst, pin, src_out=""):
        if not mel.connect_material_expressions(src, src_out, dst, pin):
            raise RuntimeError("M_LevelWater: could not connect %s -> %s.%s"
                               % (src.get_class().get_name(), dst.get_class().get_name(), pin))

    def op(cls, a, b, x, y):
        e = node(cls, x, y)
        link(a, e, "A")
        link(b, e, "B")
        return e

    def mask(src, x, y, r=False, g=False, b=False):
        e = node(unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
        link(src, e, "")
        return e

    def const(v, x, y):
        return node(unreal.MaterialExpressionConstant, x, y, r=float(v))

    d = WATER_DEFAULTS
    color = vector("Color", "#3ED1C4", -1500, -520, 0)       # master defaults only: instances always set both
    deep = vector("DeepColor", "#0A5560", -1500, -380, 1)
    tint_color = vector("TintColor", d["tint_color"], -1400, 380, 2)
    op_min = scalar("OpacityMin", d["opacity_min"], -700, 120, 3)
    op_max = scalar("OpacityMax", d["opacity_max"], -700, 200, 4)
    fade_depth = scalar("FadeDepth", d["fade_depth"], -1100, 260, 5)
    tint_depth = scalar("TintDepth", d["tint_depth"], -1100, 520, 6)
    rough = scalar("Roughness", d["roughness"], -400, -120, 7)
    spec = scalar("Specular", d["specular"], -400, -200, 8)
    coverage = scalar("Coverage", d["coverage"], -400, 640, 9)

    # depth = (CameraZ - PixelZ) * (SceneDepth - PixelDepth) / max(PixelDepth, 1)
    cam_z = mask(node(unreal.MaterialExpressionCameraPositionWS, -2500, -40), -2300, -40, b=True)
    pix_z = mask(node(unreal.MaterialExpressionWorldPosition, -2500, 60), -2300, 60, b=True)
    height = op(unreal.MaterialExpressionSubtract, cam_z, pix_z, -2100, 0)
    scene_depth = node(unreal.MaterialExpressionSceneDepth, -2500, 180)
    pixel_depth = node(unreal.MaterialExpressionPixelDepth, -2500, 300)
    behind = op(unreal.MaterialExpressionSubtract, scene_depth, pixel_depth, -2300, 200)
    safe_pd = op(unreal.MaterialExpressionMax, pixel_depth, const(1.0, -2500, 380), -2300, 320)
    ratio = op(unreal.MaterialExpressionDivide, behind, safe_pd, -2100, 240)
    depth = op(unreal.MaterialExpressionMultiply, height, ratio, -1900, 100)
    depth = op(unreal.MaterialExpressionMax, depth, const(0.0, -1900, 200), -1700, 120)  # camera below the surface

    # f = saturate(depth / FadeDepth)
    f = node(unreal.MaterialExpressionSaturate, -900, 160)
    link(op(unreal.MaterialExpressionDivide, depth, fade_depth, -1000, 160), f, "")

    # body colour and cover
    body = node(unreal.MaterialExpressionLinearInterpolate, -700, -440)
    link(color, body, "A")
    link(deep, body, "B")
    link(f, body, "Alpha")
    cover = node(unreal.MaterialExpressionLinearInterpolate, -450, 150)
    link(op_min, cover, "A")
    link(op_max, cover, "B")
    link(f, cover, "Alpha")

    # tint = max(TintColor, 0.01) ^ (depth / TintDepth)   (per channel; the floor keeps pow() away from 0^0)
    t_base = op(unreal.MaterialExpressionMax, mask(tint_color, -1150, 380, r=True, g=True, b=True),
                const(0.01, -1150, 460), -950, 420)
    tint_exp = op(unreal.MaterialExpressionDivide, depth, tint_depth, -900, 520)
    tint = node(unreal.MaterialExpressionPower, -400, 420)
    link(t_base, tint, "Base")
    link(tint_exp, tint, "Exp")
    tint_sat = node(unreal.MaterialExpressionSaturate, -250, 420)
    link(tint, tint_sat, "")

    thin = node(unreal.MaterialExpressionThinTranslucentMaterialOutput, 0, 500)
    link(tint_sat, thin, "TransmittanceColor")
    link(coverage, thin, "SurfaceCoverage")

    mel.connect_material_property(body, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(cover, "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_THIN_TRANSLUCENT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode",
                            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    mat.set_editor_property("use_translucency_vertex_fog", True)   # "Apply Fogging"
    mat.set_editor_property("compute_fog_per_pixel", True)
    mat.set_editor_property("two_sided", False)  # seen from above only; the shallow discs' bottom caps stay culled


def clear_material_graph(mat):
    """Delete every expression of a material. Do NOT use MaterialEditingLibrary.delete_all_material_expressions:
    in UE 5.8 it deletes while iterating the live expression array (MaterialEditingLibrary.cpp), so each call removes
    only about half the nodes and the rest stay behind as orphans (duplicate parameters). Delete a snapshot instead,
    repeated until the graph is empty."""
    mel = unreal.MaterialEditingLibrary
    for _ in range(8):
        exprs = list(mel.get_material_expressions(mat))
        if not exprs:
            return
        for e in exprs:
            mel.delete_material_expression(mat, e)
    raise RuntimeError("%s: could not clear the material graph" % mat.get_name())


def _check_unique_parameters(mat):
    """A graph rebuilt in place must hold each parameter once (orphans from an old graph would shadow defaults)."""
    exprs = unreal.MaterialEditingLibrary.get_material_expressions(mat)
    names = [str(e.get_editor_property("parameter_name")) for e in exprs
             if isinstance(e, (unreal.MaterialExpressionScalarParameter, unreal.MaterialExpressionVectorParameter))]
    dupes = sorted(set(n for n in names if names.count(n) > 1))
    if dupes:
        raise RuntimeError("%s: duplicate parameters %s (graph not cleared?)" % (mat.get_name(), dupes))
    return names


def ensure_water_parent(path=WATER_PARENT):
    """M_LevelWater, created or rebuilt in place when its stamped graph version is not WATER_GRAPH_VERSION."""
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    if eal.does_asset_exist(path):
        mat = unreal.load_asset(path)
        if _water_graph_version(mat) == WATER_GRAPH_VERSION:
            return mat
        # Each deleted node recompiles the half-removed old graph, so the log shows a few transient LogMaterial
        # "Failed to compile ... Missing ... input / requires the ThinTranslucentMaterial output node" warnings here.
        # Expected; what counts is the final recompile below (it raises on any error).
        _log("M_LevelWater: rebuilding graph (version %r -> %s); transient compile warnings while the old graph is "
             "removed are expected" % (_water_graph_version(mat), WATER_GRAPH_VERSION))
        clear_material_graph(mat)
    else:
        folder, name = path.rsplit("/", 1)
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material,
                                                                       unreal.MaterialFactoryNew())
    _build_water_graph(mat)
    _check_unique_parameters(mat)
    errors = mel.recompile_material(mat)
    if errors:
        raise RuntimeError("M_LevelWater failed to compile:\n" + "\n".join(str(e) for e in errors))
    mel.layout_material_expressions(mat)
    eal.set_metadata_tag(mat, WATER_VERSION_TAG, WATER_GRAPH_VERSION)
    eal.save_loaded_asset(mat, False)
    return mat


def water_params(spec):
    """M_LevelWater parameter values for a layout water material. Colours: Color (shallow tint) = "shallow_color",
    DeepColor = "deep_color", each falling back to "color" (so a water with only "color" is one colour that just
    gets less clear with depth; "color" stays what the Blender preview maps draw). Every other key from the spec,
    else WATER_DEFAULTS."""
    p = dict(WATER_DEFAULTS)
    p.update({k: v for k, v in spec.items() if k in WATER_DEFAULTS})
    p["color"] = spec.get("shallow_color", spec["color"])
    p["deep_color"] = spec.get("deep_color", spec["color"])
    return p


def ensure_water_instance(mat_id, spec, parent):
    """MI_Lvl_<mat_id> as a child of M_LevelWater (re-parented from M_LevelPalette if it was made before T-029)."""
    name = "MI_Lvl_" + mat_id
    path = PALETTE_DIR + "/" + name
    mel = unreal.MaterialEditingLibrary
    p = water_params(spec)
    vectors = {"Color": hex_to_linear_color(p["color"])}
    vectors.update({param: hex_to_linear_color(p[key]) for param, key in _WATER_VECTORS})
    scalars = {param: float(p[key]) for param, key in _WATER_SCALARS}
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
        if _mi_matches(mi, parent, scalars, vectors, exact=True):
            return mi  # unchanged: don't re-save (keeps the .uasset out of git diffs)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, PALETTE_DIR, unreal.MaterialInstanceConstant,
                                                                     unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    # Drop overrides the old palette parent left behind (Emissive), so the instance holds exactly the water params.
    mi.set_editor_property("scalar_parameter_values", [])
    mi.set_editor_property("vector_parameter_values", [])
    for param, v in vectors.items():
        mel.set_material_instance_vector_parameter_value(mi, param, v)
    for param, v in scalars.items():
        mel.set_material_instance_scalar_parameter_value(mi, param, v)
    mel.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
    return mi


def ensure_palette(layout):
    """Every layout material -> MI_Lvl_<id>: water ids under M_LevelWater, all others under M_LevelPalette."""
    parent = ensure_palette_parent()
    water_parent = None
    mats = {}
    for mid, spec in layout["materials"].items():
        if is_water_material(mid, spec):
            water_parent = water_parent or ensure_water_parent()
            mats[mid] = ensure_water_instance(mid, spec, water_parent)
        else:
            mats[mid] = ensure_palette_instance(mid, spec, parent)
    return mats


def water_sort_priorities(layout):
    """{water material id: translucent sort priority} for the prims that use them (see WATER_SORT_PRIORITY)."""
    return {mid: int(spec.get("sort_priority", WATER_SORT_PRIORITY))
            for mid, spec in layout["materials"].items() if is_water_material(mid, spec)}


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


def spawn_prim(prim, layout_id, mats, cache, sort_priorities=None):
    """A basic-shape primitive placed by its bounding box (layout convention), or a prop mesh placed by its pivot.
    sort_priorities: {material id: translucent sort priority} (water, see water_sort_priorities)."""
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
    if sort_priorities and prim["mat"] in sort_priorities:
        actor.static_mesh_component.set_translucent_sort_priority(sort_priorities[prim["mat"]])
    return _finish_actor(actor, layout_id, prim["id"], prim["id"].replace("/", "."),
                         "%s/%s" % (layout_id, prim.get("group") or prim.get("kind") or "blocks"), prim["tags"])


def _color3(v, default=(1.0, 1.0, 1.0)):
    """A LinearColor from a number (grey), an [r, g, b] list (linear) or a '#hex' (sRGB)."""
    if v is None:
        v = default
    if isinstance(v, str):
        return hex_to_linear_color(v)
    if isinstance(v, (int, float)):
        v = (v, v, v)
    return unreal.LinearColor(float(v[0]), float(v[1]), float(v[2]), 1.0)


def _fog_spec(light, layout, tod):
    """Fog values: a named fog_presets entry (light "preset"), else the time-of-day preset's "fog"; per-light overrides."""
    if light.get("preset"):
        spec = dict((layout.get("fog_presets") or {}).get(light["preset"], {}))
    else:
        spec = dict(tod.get("fog") or {})
    spec.update({k: v for k, v in light.items() if k in ("density", "height_falloff", "color", "start_distance",
                                                         "max_opacity", "sky_ambient")})
    return spec


def _apply_fixed_exposure(ppv, tod):
    """Manual exposure without the physical camera: exposure scale = 2^bias with bias = -EV100 (+ exposure_bias),
    see PostProcessEyeAdaptation.cpp CalculateManualAutoExposure (LensAttenuation 0.78 -> 1.0 cd/m2 = 1.0 at EV 0)."""
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_method", True)
    s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    s.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    s.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", -float(tod["exposure_ev100"]) + float(tod.get("exposure_bias", 0.0)))
    ppv.set_editor_property("settings", s)


def spawn_light(light, layout, layout_id):
    t = light["type"]
    lid = light["id"]
    tod = L.time_of_day(layout)
    if t == "post_process":
        # One unbound PostProcessVolume carrying the time-of-day preset's fixed exposure (ART_STYLE: exposure is fixed
        # per preset, not auto). Without an exposure in the preset, the volume is spawned but changes nothing.
        actor = _eas().spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
        actor.set_editor_property("unbound", True)
        actor.set_editor_property("priority", float(light.get("priority", 0.0)))
        if tod.get("exposure_ev100") is not None:
            _apply_fixed_exposure(actor, tod)
        else:
            _warn("post_process %s: time-of-day preset has no exposure_ev100; auto exposure stays on" % lid)
    elif t == "directional":
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
        comp = actor.get_component_by_class(unreal.SkyAtmosphereComponent)
        sky = dict(tod.get("sky") or {}, **(light.get("sky") or {}))
        if "luminance_factor" in sky:  # also scales what the real-time sky light captures (see sky_light intensity)
            comp.set_sky_luminance_factor(_color3(sky["luminance_factor"]))
        if "mie_scattering_scale" in sky:
            comp.set_mie_scattering_scale(float(sky["mie_scattering_scale"]))
        if "multi_scattering" in sky:
            comp.set_multi_scattering_factor(float(sky["multi_scattering"]))
    elif t == "sky_light":
        actor = _eas().spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 800))
        comp = actor.get_component_by_class(unreal.SkyLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_real_time_capture(bool(light.get("real_time_capture", True)))
        sl = dict(tod.get("sky_light") or {}, **(light.get("sky_light") or {}))
        if "intensity" in sl:
            comp.set_intensity(float(sl["intensity"]))
    elif t == "height_fog":
        actor = _eas().spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
        comp = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
        spec = _fog_spec(light, layout, tod)
        if "density" in spec:
            comp.set_fog_density(float(spec["density"]))
        if "height_falloff" in spec:
            comp.set_fog_height_falloff(float(spec["height_falloff"]))
        if "color" in spec:
            if tod.get("exposure_ev100") is not None:
                # The hex is the on-screen target: undo the tonemapper and the fixed exposure (fog is emissive-like).
                rgb = L.on_screen_to_scene(spec["color"], tod)
                comp.set_fog_inscattering_color(unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            else:
                comp.set_fog_inscattering_color(hex_to_linear_color(spec["color"]))
        if "start_distance" in spec:
            comp.set_start_distance(float(spec["start_distance"]))
        if "max_opacity" in spec:
            comp.set_fog_max_opacity(float(spec["max_opacity"]))
        if "sky_ambient" in spec:  # SkyAtmosphere light added on top of the fog color; 0 = the fog color alone
            comp.set_sky_atmosphere_ambient_contribution_color_scale(_color3(spec["sky_ambient"]))
    elif t == "point":
        actor = _eas().spawn_actor_from_class(unreal.PointLight, _vec(light["at"]))
        comp = actor.get_component_by_class(unreal.PointLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        if light.get("falloff", "inverse_square") == "soft":
            # Fill light without a hotspot: brightness ~ lux at the light, fading as (1 - (d/R)^2)^exponent to 0 at
            # the radius (DynamicLightingCommon.ush RadialAttenuation); intensity units don't apply in this mode.
            comp.set_use_inverse_squared_falloff(False)
            comp.set_light_falloff_exponent(float(light.get("falloff_exponent", 2.0)))
        elif light.get("units", "candelas") == "candelas":
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
    elif t == "water_volume":
        tags += ["Lure.Water", "Surface=" + _fmt(float(mk["at"][2])), "Depth=" + _fmt(float(mk["water_depth"]))]
    elif t == "ladder":
        tags += ["Lure.Ladder"]
    elif t == "water_area":
        # Informational: the game reads the actor's properties (set by _configure_water_area), not these tags.
        tags += ["Lure.WaterArea", "Area=" + mk["id"], "Habitat=" + str(mk.get("habitat", "")),
                 "Priority=%d" % int(mk.get("priority", 0))]
        if mk.get("region"):
            tags.append("Region=" + mk["region"])
    elif t == "hot_spots":
        tags += ["Lure.HotSpots"]
    elif t in ("cover_test", "clearance_test"):
        tags += ["Lure.DesignTest", "Test=" + t]
        for k in ("stance", "vs", "expect", "expect_min", "expect_max"):
            if k in mk:
                tags.append("%s%s=%s" % (k[0].upper(), k[1:], _fmt(mk[k])))
    if mk.get("name"):
        tags.append("Name=" + mk["name"])
    return tags + list(mk.get("tags", []))


def _marker_class(layout, mtype, default_cls):
    path = (layout.get("marker_classes") or {}).get(mtype) or DEFAULT_MARKER_CLASSES.get(mtype)
    if path:
        try:
            cls = unreal.load_class(None, path)
        except Exception:
            cls = None
        if cls is not None:
            return cls
        _warn("marker class %s for %s not found; using %s" % (path, mtype, default_cls.__name__))
    return default_cls


def _snake(name):
    out = []
    for i, ch in enumerate(name):
        if ch.isupper() and i and (not name[i - 1].isupper() or (i + 1 < len(name) and name[i + 1].islower())):
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


def _convert_prop(current, value):
    if isinstance(current, unreal.Name) and isinstance(value, str):
        return unreal.Name(value)
    if isinstance(current, unreal.Text) and isinstance(value, str):
        return unreal.Text(value)
    if isinstance(current, unreal.Vector2D) and isinstance(value, (list, tuple)) and len(value) == 2:
        return unreal.Vector2D(float(value[0]), float(value[1]))
    if isinstance(current, unreal.Vector) and isinstance(value, (list, tuple)) and len(value) == 3:
        return unreal.Vector(*[float(x) for x in value])
    if isinstance(current, float) and isinstance(value, (int, float)):
        return float(value)
    return value


def apply_properties(actor, props, elem_id):
    """Apply a layout element's "properties" to the spawned actor; warn (never raise) on unknown/rejected ones."""
    for key, value in (props or {}).items():
        done = False
        for name in dict.fromkeys((key, _snake(key))):
            try:
                current = actor.get_editor_property(name)
            except Exception:
                continue
            try:
                actor.set_editor_property(name, _convert_prop(current, value))
                done = True
            except Exception as exc:
                _warn("property %s=%r on %s rejected: %s" % (key, value, elem_id, exc))
                done = True
            break
        if not done:
            _warn("unknown property %s on %s (%s); skipped" % (key, elem_id, actor.get_class().get_name()))


def _size_water(actor, mk):
    """ALureWaterVolume: half size in X/Y and depth below the surface (the actor sits at the surface center)."""
    hs = mk["surface_half_size"]
    half = unreal.Vector2D(float(hs[0]), float(hs[1]))
    depth = float(mk["water_depth"])
    if hasattr(actor, "set_water_size"):
        actor.set_editor_property("surface_half_size", half)
        actor.set_editor_property("water_depth", depth)
        actor.set_water_size(half, depth)  # rebuilds the collision box now
    else:
        _warn("water %s: %s is not an ALureWaterVolume (no set_water_size); size not set"
              % (mk["id"], actor.get_class().get_name()))


def _set_props(actor, elem_id, pairs):
    for prop, value in pairs:
        try:
            actor.set_editor_property(prop, value)
        except Exception as exc:
            _warn("%s: property %s not set: %s" % (elem_id, prop, exc))


def _configure_water_area(actor, mk):
    """ALureWaterArea (T-027): id, name, priority, luck, depth band, habitat/region tags and the outline."""
    if not hasattr(actor, "set_area_tags"):
        _warn("water area %s: %s is not an ALureWaterArea (no set_area_tags); not configured"
              % (mk["id"], actor.get_class().get_name()))
        return
    depth = list(mk.get("depth") or [0, 0]) + [0, 0]
    _set_props(actor, mk["id"], (("area_id", unreal.Name(mk["id"])), ("display_name", str(mk.get("name", mk["id"]))),
                                 ("priority", int(mk.get("priority", 0))), ("luck", float(mk.get("luck", 0.0))),
                                 ("min_depth", float(depth[0])), ("max_depth", float(depth[1]))))
    if not actor.set_area_tags(unreal.Name(str(mk.get("habitat", ""))), unreal.Name(str(mk.get("region") or "None"))):
        _warn("water area %s: habitat %r or region %r is not a registered gameplay tag (Config/Tags/*.ini)"
              % (mk["id"], mk.get("habitat"), mk.get("region")))
    shape = mk.get("shape")
    if shape == "circle":
        actor.set_shape_circle(float(mk["radius"]))
    elif shape == "box":
        actor.set_shape_box(unreal.Vector2D(float(mk["size"][0]) / 2.0, float(mk["size"][1]) / 2.0))
    elif shape == "polygon":
        # World X/Y: the actor is already at its place and yaw, so the C++ side stores them in its own frame.
        count = actor.set_shape_polygon([unreal.Vector2D(float(p[0]), float(p[1])) for p in mk["points"]])
        if count != len(mk["points"]):
            _warn("water area %s: %d of %d polygon points stored" % (mk["id"], count, len(mk["points"])))
    elif shape == "everywhere":
        actor.set_shape_everywhere()
    else:
        _warn("water area %s: unknown shape %r (the layout validation should have stopped this)" % (mk["id"], shape))


def _configure_hot_spots(actor, mk):
    """ALureHotSpotSpawner (T-027): which DT_HotSpot rows spawn in this level, the cap and the seed."""
    pairs = [("hot_spot_types", [unreal.Name(t) for t in mk.get("types", [])])]
    if "max" in mk:
        pairs.append(("max_hot_spots", int(mk["max"])))
    if "seed" in mk:
        pairs.append(("random_seed", int(mk["seed"])))
    _set_props(actor, mk["id"], pairs)


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


def spawn_marker(mk, layout, layout_id):
    """The marker actor(s); text labels are spawned separately from levels.layout.labels() (see build)."""
    t = mk["type"]
    folder = "%s/markers/%s" % (layout_id, t)
    spawned = []
    if t == "label":
        return spawned
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
    if t == "water_volume":
        _size_water(a, mk)
    if t == "water_area":
        _configure_water_area(a, mk)
    if t == "hot_spots":
        _configure_hot_spots(a, mk)
    if mk.get("properties"):
        apply_properties(a, mk["properties"], mk["id"])
    spawned.append(_finish_actor(a, layout_id, mk["id"], mk["id"].replace("/", "."), folder, tags))
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
    sort_priorities = water_sort_priorities(lay)
    cache = _MeshCache()
    counts = {"prims": 0, "props": 0, "lights": 0, "markers": 0, "labels": 0}
    for prim in ex["prims"]:
        spawn_prim(prim, layout_id, mats, cache, sort_priorities)
        counts["props" if prim.get("mesh") else "prims"] += 1
    for light in ex["lights"]:
        if spawn_light(light, lay, layout_id) is not None:
            counts["lights"] += 1
    for mk in ex["markers"]:
        counts["markers"] += len(spawn_marker(mk, lay, layout_id))
    if labels:
        in_game = bool(lay.get("labels_in_game", False))
        for lab in L.labels(lay, ex["markers"], size=label_size, yaw=float(label_yaw)):
            spawn_label(lab["text"], lab["at"], layout_id, lab["id"], lab["size"], lab["yaw"], in_game)
            counts["labels"] += 1
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
              "materials": sorted("MI_Lvl_" + m for m in mats), "saved": saved, "view": view,
              "time_of_day": L.time_of_day(lay).get("id"), "exposure_ev100": L.time_of_day(lay).get("exposure_ev100")}
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

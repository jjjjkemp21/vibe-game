"""Lure level layout model: load, expand, validate and measure data/levels/<Level>.json.

Pure Python (no `unreal`, no `bpy`): the Unreal builder (Content/Python/levels/build_level.py) and the Blender preview
(art/recipes/preview_level_layout.py) both call expand(), so the preview shows exactly what the builder spawns.

Conventions (docs/levels/README-style summary; the full description is in each level's .md):
- Units cm, Unreal world axes: X north (forward), Y east (right), Z up. The water surface is Z = 0 unless the layout's
  "water_z" says otherwise.
- Rotation = Unreal rotator in degrees: yaw turns +X toward +Y (0 = north, 90 = east), pitch raises the nose (+X toward
  +Z), roll turns around X. Applied roll, then pitch, then yaw (Unreal's FRotator order).
- A primitive is placed by its BOUNDING BOX: "size" is the full box size in the piece's local axes and "at" is a
  point on that box chosen by "anchor": "bottom" (default, bottom center, like our props), "center" or "top" (top
  center: handy for terrain whose walkable top is what matters). Rotation turns the box around the anchor point.
- Shapes: box, cylinder (axis Z, round in X/Y), sphere, cone (apex up), plane (Z size 0). They map to the engine basic
  shapes /Engine/BasicShapes/{Cube,Cylinder,Sphere,Cone,Plane}; the builder reads each mesh's real bounds, so the
  JSON never depends on a mesh's pivot.
- Compound blocks ("kind"): stairs, ramp, pier, palm, lamp_post, beacon, stall. They expand to primitives with ids
  "<block id>/<part>" (and lamps/beacons also emit point lights).

- Water areas (T-027, docs/specs/fishing-water-rules.md): "water_area" markers give the water inside an outline (circle,
  box, polygon or everywhere) a habitat, region, luck, priority and depth band; water_area_at() is the same winner rule as
  the game (FLureWaterRules::FindAreaIndex). A "hot_spots" marker turns hot spots on for the level.

CLI (plain Python 3, from the repo root):  python Content/Python/levels/layout.py data/levels/L_PalmKey.json
prints counts, validation problems, fishing spots, water areas and the route table (distances and walk times).
"""
import json
import math
import os
import re

SCHEMA = "lure.level_layout/1"

ENGINE_SHAPES = {
    "box": "/Engine/BasicShapes/Cube",
    "cylinder": "/Engine/BasicShapes/Cylinder",
    "sphere": "/Engine/BasicShapes/Sphere",
    "cone": "/Engine/BasicShapes/Cone",
    "plane": "/Engine/BasicShapes/Plane",
}
ANCHORS = ("bottom", "center", "top")
MARKER_TYPES = (
    "player_start", "fishing_spot", "patrol", "sight_cone", "zone", "npc", "teleport", "boat_mooring",
    "cover_test", "clearance_test", "landmark", "label", "sell_point", "water_volume", "ladder",
    "water_area", "hot_spots",
)
# T-027 water areas (docs/specs/fishing-water-rules.md): the habitat of the water inside an outline.
WATER_AREA_SHAPES = ("circle", "box", "polygon", "everywhere")
LIGHT_TYPES = ("directional", "sky_atmosphere", "sky_light", "height_fog", "point", "post_process")
ZONE_TYPES = ("threat", "hazard", "crawl_gap", "quiet", "trigger", "area")
STANCES = ("stand", "crouch", "prone")

# Fallback metrics (docs/specs/movement-rules.md + DT_Movement.csv, lane eng1 0cac885). Layouts carry their own copy.
DEFAULT_METRICS = {
    "walk_cm_s": 350.0, "sprint_cm_s": 600.0, "crouch_cm_s": 180.0, "prone_cm_s": 90.0,
    "eye_stand": 165.0, "eye_crouch": 95.0, "eye_prone": 35.0,
    "height_stand": 180.0, "height_crouch": 110.0, "height_prone": 52.0,
    "clear_stand": 182.4, "clear_crouch": 112.4, "clear_prone": 54.4,
    "crawl_gap": 60.0, "max_step": 45.0, "stair_step_max": 20.0, "jump_apex_stand": 90.0,
    "walkable_deg": 44.765,
    # Swimming (T-026, docs/specs/swimming.md; DT_Movement rows Swim/SwimSprint): capsule center SurfaceFloatDepth under
    # the surface, feet 100 cm under it; edges up to ClimbMaxHeight above the water are Jump climbs; submerged tops from
    # swim_step_lowest (feet + 9) up are steps or climbs; ladders climb higher (their MaxClimbHeight).
    "swim_cm_s": 170.0, "swim_float_depth": 10.0, "climb_out_max": 60.0, "swim_step_lowest": -91.0,
}
LADDER_DEFAULTS = {"MaxClimbHeight": 300.0, "GrabZoneHalfSize": [40.0, 60.0, 120.0], "VisualHeightAboveWater": 80.0,
                   "VisualDepthBelowWater": 120.0}


# ---------------------------------------------------------------------------------------------------------------------
# Small vector math (tuples of 3 floats)
# ---------------------------------------------------------------------------------------------------------------------
def v3(v, z_default=0.0):
    v = list(v)
    if len(v) == 2:
        v.append(z_default)
    return (float(v[0]), float(v[1]), float(v[2]))


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def length(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def rot_rows(yaw=0.0, pitch=0.0, roll=0.0):
    """Unreal FRotationMatrix rows: the world directions of the local X, Y and Z axes (degrees in)."""
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sr, cr = math.sin(math.radians(roll)), math.cos(math.radians(roll))
    return (
        (cp * cy, cp * sy, sp),
        (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp),
        (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp),
    )


def rotate(rows, v):
    """Local vector -> world vector for a rotation given as rot_rows()."""
    return (
        v[0] * rows[0][0] + v[1] * rows[1][0] + v[2] * rows[2][0],
        v[0] * rows[0][1] + v[1] * rows[1][1] + v[2] * rows[2][1],
        v[0] * rows[0][2] + v[1] * rows[1][2] + v[2] * rows[2][2],
    )


def yaw_to(a, b):
    """Yaw in degrees from point a to point b (0 = +X north, 90 = +Y east)."""
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


# ---------------------------------------------------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------------------------------------------------
def repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))


def resolve_path(path):
    """Absolute path for a layout: absolute paths as is, relative ones against the repo root."""
    if os.path.isabs(path):
        return path
    return os.path.join(repo_root(), path)


def load(path):
    path = resolve_path(path)
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    data["_path"] = path
    return data


def metrics(layout):
    m = dict(DEFAULT_METRICS)
    m.update({k: float(v) for k, v in (layout.get("metrics") or {}).items() if isinstance(v, (int, float))})
    return m


def eye_height(layout, stance):
    return metrics(layout)["eye_" + stance]


# ---------------------------------------------------------------------------------------------------------------------
# Time of day, exposure and the engine's tonemapper (so palette hex values can be on-screen targets, ART_STYLE)
# ---------------------------------------------------------------------------------------------------------------------
def time_of_day(layout, preset_id=None):
    """The active time-of-day preset (layout["time_of_day"] names one of layout["time_of_day_presets"]), or {}.
    A preset holds: exposure_ev100 (fixed manual exposure: scene luminance 2^EV100 cd/m2 maps to 1.0 before the
    tonemapper, i.e. exposure scale = 2^-EV100), fog {density, height_falloff, color, start_distance, max_opacity,
    sky_ambient}, sky {luminance_factor, mie_scattering_scale, multi_scattering}, sky_light {intensity}."""
    presets = layout.get("time_of_day_presets") or {}
    pid = preset_id or layout.get("time_of_day")
    return dict(presets.get(pid) or {}, id=pid) if pid in presets else {}


def exposure_scale(preset):
    """Multiplier from scene luminance to the tonemapper input under a fixed exposure (1.0 when none is set)."""
    if not preset or preset.get("exposure_ev100") is None:
        return 1.0
    return 2.0 ** (-float(preset["exposure_ev100"]) + float(preset.get("exposure_bias", 0.0)))


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(c):
    c = min(1.0, max(0.0, c))
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055


def hex_to_linear(hex_str):
    h = hex_str.lstrip("#")
    return tuple(srgb_to_linear(int(h[i:i + 2], 16) / 255.0) for i in (0, 2, 4))


# Unreal's default filmic tonemapper (Engine/Shaders/Private/TonemapCommon.ush FilmToneMap, default post-process
# values slope 0.88, toe 0.55, shoulder 0.26, black clip 0, white clip 0.04). Applied per channel; the engine also
# converts to ACEScg and desaturates slightly, which this ignores (a few % off on saturated colors).
FILM = {"slope": 0.88, "toe": 0.55, "shoulder": 0.26, "black_clip": 0.0, "white_clip": 0.04}


def _film_constants(f=FILM):
    s, t, sh, b, w = f["slope"], f["toe"], f["shoulder"], f["black_clip"], f["white_clip"]
    toe_scale = 1.0 + b - t
    shoulder_scale = 1.0 + w - sh
    bt = (0.18 + b) / toe_scale - 1.0
    toe_match = math.log10(0.18) - 0.5 * math.log((1.0 + bt) / (1.0 - bt)) * (toe_scale / s)
    straight_match = (1.0 - t) / s - toe_match
    shoulder_match = sh / s - straight_match
    return s, b, w, toe_scale, shoulder_scale, toe_match, straight_match, shoulder_match


def ue_filmic(x, f=FILM):
    """Tonemapper input (exposed scene-linear) -> display-linear output, one channel."""
    s, b, w, ts, ss, tm, stm, shm = _film_constants(f)
    lg = math.log10(max(float(x), 1e-7))
    straight = s * (lg + stm)
    toe = -b + (2.0 * ts) / (1.0 + math.exp((-2.0 * s / ts) * (lg - tm))) if lg < tm else straight
    shoulder = (1.0 + w) - (2.0 * ss) / (1.0 + math.exp((2.0 * s / ss) * (lg - shm))) if lg > shm else straight
    t = min(1.0, max(0.0, (lg - tm) / (shm - tm)))
    if shm < tm:
        t = 1.0 - t
    t = (3.0 - 2.0 * t) * t * t
    return max(0.0, toe + (shoulder - toe) * t)


def ue_filmic_inverse(y, f=FILM):
    """Display-linear -> the exposed scene-linear value the tonemapper maps to it (bisection; y clamped below 1)."""
    y = min(max(float(y), 0.0), 0.995)
    if y <= 0.0:
        return 0.0
    lo, hi = 1e-6, 1e3
    for _ in range(100):
        mid = math.sqrt(lo * hi)
        if ue_filmic(mid, f) < y:
            lo = mid
        else:
            hi = mid
    return math.sqrt(lo * hi)


def fog_transmittance(fog, cam_z, ray_length, ray_dz, fog_height=0.0):
    """Unreal ExponentialHeightFog transmittance along one ray (HeightFogCommon.ush): density and height falloff are
    the component values (the renderer divides both by 1000, per cm, base-2 exponent); the first start_distance cm
    are fog-free. ray_length = distance to the surface (cm), ray_dz = its height change (cm). 1 = no fog."""
    dens = float(fog.get("density", 0.0)) / 1000.0
    fall = float(fog.get("height_falloff", 0.2)) / 1000.0
    start = float(fog.get("start_distance", 0.0))
    if dens <= 0.0 or ray_length <= start:
        return 1.0
    t0 = start / ray_length
    origin_z = cam_z + t0 * ray_dz
    length = (1.0 - t0) * ray_length
    dz = (1.0 - t0) * ray_dz
    origin = dens * 2.0 ** (-fall * (origin_z - fog_height))
    eff = fall * dz
    eff = eff if abs(eff) > 1e-7 else 0.001
    integral = origin * (1.0 - 2.0 ** (-eff)) / eff * length
    return max(1.0 - float(fog.get("max_opacity", 1.0)), min(1.0, 2.0 ** (-integral)))


def on_screen_to_scene(hex_str, preset):
    """Scene luminance (linear RGB) that shows as `hex_str` on screen under the preset's fixed exposure: used for
    emissive-like colors such as the height fog's inscattering, whose palette hex is an on-screen target."""
    k = exposure_scale(preset)
    return tuple(ue_filmic_inverse(c) / k for c in hex_to_linear(hex_str))


# ---------------------------------------------------------------------------------------------------------------------
# Expansion: blocks -> primitives
# ---------------------------------------------------------------------------------------------------------------------
def _prim(pid, shape, center, size, yaw=0.0, pitch=0.0, roll=0.0, mat="default", collision="block", shadow=True,
          group="", tags=None, zone="", visible=True, step_up=True):
    return {
        "id": pid, "shape": shape, "center": v3(center), "size": v3(size), "yaw": float(yaw), "pitch": float(pitch),
        "roll": float(roll), "mat": mat, "collision": collision, "shadow": bool(shadow), "group": group,
        "tags": list(tags or []), "zone": zone, "visible": bool(visible), "step_up": bool(step_up),
    }


def center_from_anchor(at, size, anchor, rows):
    at = v3(at)
    if anchor == "center":
        return at
    half = size[2] / 2.0
    offset = (0.0, 0.0, half if anchor == "bottom" else -half)
    return add(at, rotate(rows, offset))


def _common(block):
    return {
        "mat": block.get("mat", "default"),
        "collision": block.get("collision", "block"),
        "shadow": block.get("shadow", True),
        "group": block.get("group", ""),
        "tags": block.get("tags", []),
        "zone": block.get("zone", ""),
        "visible": block.get("visible", True),
        "step_up": block.get("step_up", True),  # false: characters cannot step up onto it (thin posts, rims)
    }


def _expand_prim(block):
    shape = block["shape"]
    size = v3(block["size"])
    yaw, pitch, roll = block.get("yaw", 0.0), block.get("pitch", 0.0), block.get("roll", 0.0)
    rows = rot_rows(yaw, pitch, roll)
    center = center_from_anchor(block["at"], size, block.get("anchor", "bottom"), rows)
    return [_prim(block["id"], shape, center, size, yaw, pitch, roll, **_common(block))]


def _expand_stairs(block):
    """Solid stairs climbing along `yaw` from `at` (bottom of the first riser, center of the width, floor Z).
    rise: total height; max_step: tallest allowed riser (<= 20 cm by the level rules); tread: depth per step;
    base: Z the step boxes fill down to (default at.z - 40); landing: extra depth added to the top step."""
    at = v3(block["at"])
    yaw = float(block.get("yaw", 0.0))
    rows = rot_rows(yaw)
    rise = float(block["rise"])
    max_step = float(block.get("max_step", 18.0))
    steps = max(1, int(math.ceil(rise / max_step - 1e-9)))
    h = rise / steps
    tread = float(block.get("tread", 30.0))
    width = float(block.get("width", 200.0))
    base = float(block.get("base", at[2] - 40.0))
    landing = float(block.get("landing", 0.0))
    common = _common(block)
    out = []
    for i in range(1, steps + 1):
        top = at[2] + i * h
        depth = tread + (landing if i == steps else 0.0)
        along = (i - 1) * tread + depth / 2.0
        c = add((at[0], at[1], (top + base) / 2.0), rotate(rows, (along, 0.0, 0.0)))
        out.append(_prim("%s/step_%02d" % (block["id"], i), "box", c, (depth, width, top - base), yaw, **common))
    out[0]["meta"] = {"steps": steps, "step_height": round(h, 2), "run": round(steps * tread + landing, 1)}
    return out


def ramp_geometry(frm, to, thickness):
    frm, to = v3(frm), v3(to)
    d = sub(to, frm)
    horiz = math.hypot(d[0], d[1])
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], horiz))
    rows = rot_rows(yaw, pitch)
    mid = mul(add(frm, to), 0.5)
    center = sub(mid, mul(rows[2], thickness / 2.0))
    return center, (length(d), 0.0, thickness), yaw, pitch


def _expand_ramp(block):
    """A sloped slab whose TOP surface centerline runs from `from` to `to` (x, y, z)."""
    thickness = float(block.get("thickness", 40.0))
    center, size, yaw, pitch = ramp_geometry(block["from"], block["to"], thickness)
    size = (size[0], float(block.get("width", 300.0)), thickness)
    p = _prim(block["id"], "box", center, size, yaw, pitch, **_common(block))
    p["meta"] = {"slope_deg": round(abs(pitch), 2), "length": round(size[0], 1)}
    return [p]


def _expand_pier(block):
    """Dock or jetty: a deck from `from` to `to` (x, y) with its top at `top`, plus round posts along both edges."""
    frm, to = v3(block["from"]), v3(block["to"])
    top = float(block["top"])
    deck = float(block.get("deck", 20.0))
    width = float(block.get("width", 300.0))
    yaw = yaw_to(frm, to)
    rows = rot_rows(yaw)
    run = math.hypot(to[0] - frm[0], to[1] - frm[1])
    mid = ((frm[0] + to[0]) / 2.0, (frm[1] + to[1]) / 2.0, top - deck / 2.0)
    common = _common(block)
    out = [_prim(block["id"] + "/deck", "box", mid, (run, width, deck), yaw, **common)]
    posts = block.get("posts")
    if posts:
        spacing = float(posts.get("spacing", 300.0))
        psize = float(posts.get("size", 30.0))
        bottom = float(posts.get("bottom", -300.0))
        above = float(posts.get("above", 35.0))
        pmat = posts.get("mat", common["mat"])
        count = max(1, int(round(run / spacing)))
        lateral = width / 2.0 + psize / 2.0
        # Posts stick up past the deck edge: never let the player step up and perch on a post top.
        pcommon = dict(common, mat=pmat, step_up=False)
        for i in range(count + 1):
            along = run * i / count
            for side, s in (("l", -1.0), ("r", 1.0)):
                base = add((frm[0], frm[1], bottom), rotate(rows, (along, s * lateral, 0.0)))
                hgt = top + above - bottom
                c = add(base, (0.0, 0.0, hgt / 2.0))
                out.append(_prim("%s/post_%02d%s" % (block["id"], i, side), "cylinder", c, (psize, psize, hgt), yaw,
                                 **pcommon))
    out[0]["meta"] = {"length": round(run, 1), "height_above_water": top}
    return out


def _expand_palm(block):
    """Greybox palm: leaning trunk (blocks), crown sphere and 6 drooping fronds (no collision)."""
    at = v3(block["at"])
    height = float(block.get("height", 700.0))
    lean = float(block.get("lean", 6.0))
    lean_yaw = float(block.get("lean_yaw", 0.0))
    trunk = float(block.get("trunk", 35.0))
    crown = float(block.get("crown", 450.0))
    rows = rot_rows(lean_yaw, -lean)
    center = add(at, rotate(rows, (0.0, 0.0, height / 2.0)))
    top = add(at, rotate(rows, (0.0, 0.0, height)))
    common = _common(block)
    trunk_mat = block.get("trunk_mat", "palm_trunk")
    leaf_mat = block.get("leaf_mat", "palm_green")
    crown_mat = block.get("crown_mat", "leaf_dark")
    out = [_prim(block["id"] + "/trunk", "cylinder", center, (trunk, trunk, height), lean_yaw, -lean,
                 **dict(common, mat=trunk_mat))]
    out.append(_prim(block["id"] + "/crown", "sphere", top, (crown * 0.28, crown * 0.28, crown * 0.2), lean_yaw,
                     **dict(common, mat=crown_mat, collision="none")))
    for k in range(6):
        fy = lean_yaw + 15.0 + 60.0 * k
        frows = rot_rows(fy, -24.0)
        c = add(top, rotate(frows, (crown * 0.26, 0.0, 0.0)))
        out.append(_prim("%s/frond_%d" % (block["id"], k), "box", c, (crown * 0.52, crown * 0.17, 10.0), fy, -24.0,
                         **dict(common, mat=leaf_mat, collision="none")))
    out[0]["meta"] = {"top": [round(t, 1) for t in top]}
    return out


def _light_from(block, pos, defaults):
    spec = dict(defaults)
    spec.update(block.get("light") or {})
    if spec.get("enabled", True) is False:
        return None
    return {"id": block["id"] + "/light", "type": "point", "at": list(pos), "intensity": spec.get("intensity", 8.0),
            "radius": spec.get("radius", 1500.0), "color": spec.get("color", "#E8C46A"),
            "shadows": spec.get("shadows", False), "units": spec.get("units", "candelas")}


def _expand_lamp_post(block):
    at = v3(block["at"])
    yaw = float(block.get("yaw", 0.0))
    rows = rot_rows(yaw)
    height = float(block.get("height", 320.0))
    common = _common(block)
    post_mat = block.get("post_mat", "wood_dark")
    lamp_mat = block.get("lamp_mat", "lantern")
    out = [
        _prim(block["id"] + "/post", "box", add(at, (0.0, 0.0, height / 2.0)), (18.0, 18.0, height), yaw,
              **dict(common, mat=post_mat)),
        _prim(block["id"] + "/arm", "box", add(at, rotate(rows, (38.0, 0.0, height - 8.0))), (76.0, 10.0, 10.0), yaw,
              **dict(common, mat=post_mat, collision="none")),
    ]
    lamp = add(at, rotate(rows, (64.0, 0.0, height - 36.0)))
    out.append(_prim(block["id"] + "/lamp", "box", lamp, (28.0, 28.0, 36.0), yaw,
                     **dict(common, mat=lamp_mat, collision="none", shadow=False)))
    light = _light_from(block, lamp, {"intensity": 10.0, "radius": 1600.0})
    return out, [light] if light else []


def _expand_beacon(block):
    """Landmark tower: base, tower with 2 accent bands, gallery, glowing lantern room, cone roof (+ a point light)."""
    at = v3(block["at"])
    height = float(block.get("height", 1100.0))
    common = _common(block)
    wall = block.get("wall_mat", "beacon_white")
    band = block.get("band_mat", "accent")
    lamp_mat = block.get("lamp_mat", "lantern")
    base_h, gal_h, lamp_h, roof_h = 120.0, 20.0, 200.0, 110.0
    tower_h = height - base_h - gal_h - lamp_h - roof_h
    z = at[2]
    out = [_prim(block["id"] + "/base", "cylinder", (at[0], at[1], z + base_h / 2), (380, 380, base_h),
                 **dict(common, mat="rock"))]
    z += base_h
    out.append(_prim(block["id"] + "/tower", "cylinder", (at[0], at[1], z + tower_h / 2), (260, 260, tower_h),
                     **dict(common, mat=wall)))
    for i, f in enumerate((0.38, 0.72)):
        out.append(_prim("%s/band_%d" % (block["id"], i), "cylinder", (at[0], at[1], z + tower_h * f),
                         (268, 268, 70), **dict(common, mat=band, collision="none")))
    z += tower_h
    out.append(_prim(block["id"] + "/gallery", "cylinder", (at[0], at[1], z + gal_h / 2), (340, 340, gal_h),
                     **dict(common, mat="wood_dark")))
    z += gal_h
    lamp_c = (at[0], at[1], z + lamp_h / 2)
    out.append(_prim(block["id"] + "/lamp", "cylinder", lamp_c, (180, 180, lamp_h),
                     **dict(common, mat=lamp_mat, shadow=False)))
    z += lamp_h
    out.append(_prim(block["id"] + "/roof", "cone", (at[0], at[1], z + roof_h / 2), (250, 250, roof_h),
                     **dict(common, mat=band)))
    out[0]["meta"] = {"top": round(z + roof_h, 1), "lamp": [round(c, 1) for c in lamp_c]}
    light = _light_from(block, lamp_c, {"intensity": 40.0, "radius": 6000.0})
    return out, [light] if light else []


def _expand_stall(block):
    """Open-front market stall (the dock NPC's shop). `at` = floor center (x, y, floor top Z); local +X = the front.
    size = [depth, width, wall height]. A platform extends `apron` cm in front so 2-4 players can stand at the counter."""
    at = v3(block["at"])
    yaw = float(block.get("yaw", 0.0))
    rows = rot_rows(yaw)
    depth, width, height = v3(block.get("size", (400, 500, 260)))
    apron = float(block.get("apron", 250.0))
    base = float(block.get("base", at[2] - 150.0))
    common = _common(block)
    wall = block.get("wall_mat", "wood")
    floor = block.get("floor_mat", "wood_dark")
    roof = block.get("roof_mat", "wood_dark")
    awning = block.get("awning_mat", "accent")

    def place(local, size, mat, pid, pitch=0.0, **kw):
        c = add(at, rotate(rows, local))
        return _prim("%s/%s" % (block["id"], pid), "box", c, size, yaw, pitch, **dict(common, mat=mat, **kw))

    plat_d = depth + apron + 40.0
    out = [
        place((apron / 2.0, 0.0, (base - at[2]) / 2.0), (plat_d, width + 60.0, at[2] - base), floor, "platform"),
        place((-depth / 2.0 + 10.0, 0.0, height / 2.0), (20.0, width, height), wall, "wall_back"),
        place((0.0, -width / 2.0 + 10.0, height / 2.0), (depth, 20.0, height), wall, "wall_l"),
        place((0.0, width / 2.0 - 10.0, height / 2.0), (depth, 20.0, height), wall, "wall_r"),
        place((depth / 2.0 - 30.0, 0.0, 50.0), (60.0, width - 40.0, 100.0), wall, "counter"),
        place((10.0, 0.0, height + 10.0), (depth + 120.0, width + 80.0, 20.0), roof, "roof"),
        place((depth / 2.0 + 110.0, 0.0, height - 25.0), (150.0, width + 80.0, 8.0), awning, "awning", pitch=-16.0,
              collision="none"),
    ]
    out[0]["meta"] = {"npc_local": [-depth / 4.0, 0.0, 0.0]}
    return out


def expand_prop(prop):
    """A placed mesh (our asset, pivot at bottom center): {"id", "mesh": "/Game/...", "at", "yaw", "scale",
    "preview": {"shape": "box", "size": [100, 100, 100], "mat": "wood"}}. The builder spawns the mesh at "at" with
    the rotation and scale; the Blender preview draws the stand-in shape (size is the unscaled mesh size)."""
    scale = v3(prop.get("scale", (1, 1, 1)))
    pv = prop.get("preview", {"shape": "box", "size": [100, 100, 100], "mat": "wood"})
    size = v3(pv["size"])
    size = (size[0] * scale[0], size[1] * scale[1], size[2] * scale[2])
    yaw, pitch, roll = prop.get("yaw", 0.0), prop.get("pitch", 0.0), prop.get("roll", 0.0)
    rows = rot_rows(yaw, pitch, roll)
    center = center_from_anchor(prop["at"], size, "bottom", rows)
    p = _prim(prop["id"], pv.get("shape", "box"), center, size, yaw, pitch, roll,
              **dict(_common(prop), mat=pv.get("mat", "default")))
    p["mesh"] = prop["mesh"]
    p["pivot"] = list(v3(prop["at"]))
    p["scale"] = list(scale)
    p["source"] = prop["id"]
    p["kind"] = "prop"
    return p


EXPANDERS = {
    "stairs": _expand_stairs,
    "ramp": _expand_ramp,
    "pier": _expand_pier,
    "palm": _expand_palm,
    "lamp_post": _expand_lamp_post,
    "beacon": _expand_beacon,
    "stall": _expand_stall,
}


def expand_block(block):
    """One block -> (primitives, lights)."""
    kind = block.get("kind", "prim")
    if kind == "prim":
        return _expand_prim(block), []
    res = EXPANDERS[kind](block)
    if isinstance(res, tuple):
        return res
    return res, []


def resolve_rel(layout):
    """Blocks and markers may say "rel": {"to": "<block id>", "offset": [x, y, z]} instead of an absolute "at":
    at = that block's "at" + its yaw applied to offset, and the item's yaw is added to the parent's yaw.
    Returns new lists (the layout itself is not modified)."""
    by_id = {}
    blocks, markers = [], []

    def resolve(item):
        rel = item.get("rel")
        if not rel:
            return item
        parent = by_id.get(rel["to"])
        if parent is None:
            raise KeyError("rel.to %r must name an earlier block" % rel["to"])
        pyaw = float(parent.get("yaw", 0.0))
        at = add(v3(parent["at"]), rotate(rot_rows(pyaw), v3(rel.get("offset", (0, 0, 0)))))
        out = dict(item)
        out["at"] = list(at)
        out["yaw"] = pyaw + float(item.get("yaw", 0.0))
        return out

    for b in layout.get("blocks", []):
        rb = resolve(b)
        blocks.append(rb)
        if "at" in rb:
            by_id[rb["id"]] = rb
    for mk in layout.get("markers", []):
        markers.append(resolve(mk))
    return blocks, markers


LABELLED_TYPES = ("fishing_spot", "npc", "landmark", "zone", "sight_cone", "boat_mooring", "player_start", "teleport",
                  "water_area")


def labels(layout, markers, size=None, yaw=180.0):
    """Every in-level text label the builder spawns (TextRenderActors), in one place so the preview draws the same:
    "label" markers, plus the name of each named marker of LABELLED_TYPES unless it says "label": false (placed at
    its "label_at" or "at", lifted by the layout's "label_lift"; facing its "label_yaw" or `yaw`).
    Returns [{"id", "text", "at", "size", "yaw"}]; `size` defaults to the layout's "label_size"."""
    size = float(size or layout.get("label_size", 120.0))
    lift = float(layout.get("label_lift", 300.0))
    out = []
    for mk in markers:
        t = mk["type"]
        if t == "label":
            out.append({"id": mk["id"], "text": mk["text"], "at": v3(mk["at"]), "size": float(mk.get("size", size)),
                        "yaw": float(mk.get("yaw", yaw))})
            continue
        name = mk.get("name")
        if not name or not mk.get("label", True) or t not in LABELLED_TYPES:
            continue
        text = name
        if t == "fishing_spot":
            text = "%s\n%s  L%s  %s" % (name, mk["habitat"], "-".join(str(x) for x in mk.get("level_band", [])),
                                        mk.get("time_label", ""))
        elif t == "water_area":
            depth = mk.get("depth") or [0, 0]
            band = "" if not (depth[0] or depth[1]) else "  %s-%s cm" % (depth[0], depth[1] or "any")
            text = "%s\n%s  P%s%s" % (name, mk.get("habitat", "?"), mk.get("priority", 0), band)
        at = v3(mk.get("label_at", mk["at"]))
        out.append({"id": mk["id"] + "/label", "text": text, "at": (at[0], at[1], at[2] + lift), "size": size,
                    "yaw": float(mk.get("label_yaw", yaw))})
    return out


def expand(layout):
    """Everything the builder spawns, flattened:
    {"prims": [...], "lights": [...], "markers": [...], "labels": [...]}
    Fishing spots also get an automatic teleport marker "tp_<spot id>" at their cast_from point (for Lure.Teleport and
    the playtester), unless the spot says "teleport": false."""
    prims, lights = [], []
    blocks, markers_in = resolve_rel(layout)
    for block in blocks:
        if block.get("disabled"):
            continue
        p, l = expand_block(block)
        for item in p:
            item["source"] = block["id"]
            item["kind"] = block.get("kind", "prim")
        prims.extend(p)
        lights.extend(l)
    for prop in layout.get("props", []):
        if prop.get("disabled"):
            continue
        prims.append(expand_prop(prop))
    lights.extend(layout.get("lights", []))
    markers = [m for m in markers_in if not m.get("disabled")]
    water_z = float(layout.get("water_z", 0.0))
    for i, m in enumerate(markers):
        # Water areas and the hot spot marker need no "at" in the JSON: a polygon sits at its centroid, the rest at the
        # origin on the water (the builder places the actor there; the outline is in world X/Y anyway).
        if m["type"] in ("water_area", "hot_spots") and "at" not in m:
            m = dict(m)
            if m["type"] == "water_area" and m.get("shape") == "polygon" and len(m.get("points") or []) >= 3:
                c = polygon_centroid(m["points"])
                m["at"] = [c[0], c[1], water_z]
            else:
                m["at"] = [0.0, 0.0, water_z]
            m["_auto_at"] = True
            markers[i] = m
    for m in list(markers):
        if m["type"] == "fishing_spot" and m.get("teleport", True) and m.get("cast_from"):
            cf = v3(m["cast_from"])
            markers.append({"id": "tp_" + m["id"], "type": "teleport", "at": list(cf), "yaw": yaw_to(cf, v3(m["at"])),
                            "name": m.get("name", m["id"]), "auto": True, "label": False})
    labels = []
    for m in markers:
        if m["type"] == "label":
            labels.append({"id": m["id"], "text": m["text"], "at": v3(m["at"]), "size": m.get("size", 120.0),
                           "yaw": m.get("yaw", 180.0)})
    return {"prims": prims, "lights": lights, "markers": markers, "labels": labels}


# ---------------------------------------------------------------------------------------------------------------------
# Lookups
# ---------------------------------------------------------------------------------------------------------------------
def marker_index(layout):
    """Markers by id, with "rel" placements resolved and the automatic spot teleports included."""
    return {m["id"]: m for m in expand(layout)["markers"]}


def point_of(layout, ref):
    """A route/view reference: a marker id, a zone id (its center) or a literal [x, y(, z)]."""
    if isinstance(ref, (list, tuple)):
        return v3(ref)
    m = marker_index(layout).get(ref)
    if m is not None:
        if m["type"] == "fishing_spot" and m.get("cast_from"):
            return v3(m["cast_from"])
        if "at" in m:
            return v3(m["at"])
        if m["type"] == "patrol":
            return v3(m["points"][0])
    for z in layout.get("zones", []):
        if z["id"] == ref:
            return v3(z["center"])
    raise KeyError("Unknown point reference: %r" % (ref,))


# ---------------------------------------------------------------------------------------------------------------------
# Routes: the flow of beats with distances and walk times
# ---------------------------------------------------------------------------------------------------------------------
def route_table(layout):
    """Legs of layout["route"]: 2D path length along from -> via... -> to, plus |dz| between the ends (a cheap
    stand-in for slopes and stairs), and the time at the leg's movement speed (metrics, cm/s)."""
    m = metrics(layout)
    speeds = {"walk": m["walk_cm_s"], "sprint": m["sprint_cm_s"], "crouch": m["crouch_cm_s"], "prone": m["prone_cm_s"]}
    out = []
    for leg in (layout.get("route") or {}).get("legs", []):
        pts = [point_of(layout, leg["from"])] + [v3(p) for p in leg.get("via", [])] + [point_of(layout, leg["to"])]
        d2 = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:]))
        dz = abs(pts[-1][2] - pts[0][2])
        dist = d2 + dz
        move = leg.get("move", "walk")
        seconds = dist / speeds[move]
        extra = leg.get("segments", [])  # [{"move": "prone", "length": 600}] parts at another speed, inside dist
        for seg in extra:
            seconds += seg["length"] / speeds[seg["move"]] - seg["length"] / speeds[move]
        out.append({"from": leg["from"], "to": leg["to"], "beat": leg.get("beat", ""), "move": move,
                    "distance_m": round(dist / 100.0, 1), "seconds": round(seconds, 1), "points": pts,
                    "optional": bool(leg.get("optional", False))})
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Water (T-026): water volumes, ladders and the "every water edge has a way out" check
# ---------------------------------------------------------------------------------------------------------------------
def ladder_props(mk):
    """A ladder marker's ALureLadder values: LADDER_DEFAULTS overridden by its "properties"."""
    out = dict(LADDER_DEFAULTS)
    out.update(mk.get("properties") or {})
    return out


def ladder_visual(mk, mat="default"):
    """The greybox board ALureLadder draws itself (LureLadder.cpp UpdateVisual: 6 cm x 50 cm, 3 cm off the face), as a
    non-colliding primitive for previews only (the builder spawns the actor, which makes its own board)."""
    p = ladder_props(mk)
    up, down = float(p["VisualHeightAboveWater"]), float(p["VisualDepthBelowWater"])
    rows = rot_rows(float(mk.get("yaw", 0.0)))
    c = add(v3(mk["at"]), rotate(rows, (3.0, 0.0, 0.5 * (up - down))))
    return _prim(mk["id"] + "/board", "box", c, (6.0, 50.0, max(up + down, 1.0)), float(mk.get("yaw", 0.0)), mat=mat,
                 collision="none")


def _local_xy(mk, x, y):
    at = v3(mk["at"])
    a = math.radians(-float(mk.get("yaw", 0.0)))
    dx, dy = x - at[0], y - at[1]
    return dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a)


def in_water_volume(wv, x, y):
    hx, hy = (float(v) for v in wv["surface_half_size"])
    lx, ly = _local_xy(wv, x, y)
    return abs(lx) <= hx and abs(ly) <= hy


def in_grab_zone(mk, x, y):
    """Same box as ALureLadder::IsInGrabZone, in plan (X from -5 to 2 * half X out from the face, |Y| <= half Y)."""
    p = ladder_props(mk)
    hx, hy = float(p["GrabZoneHalfSize"][0]), float(p["GrabZoneHalfSize"][1])
    lx, ly = _local_xy(mk, x, y)
    return -5.0 <= lx <= 2.0 * hx and abs(ly) <= hy


def water_exit_report(layout, top_fn, expanded=None):
    """Checks that every place a player can fall or step into the water has a way out within reach.

    top_fn(x, y) -> the highest colliding surface z (cm) at that point, or None (no ground). The Blender preview passes
    a ray cast against the collision geometry; the check itself is pure Python.

    Per water volume, over its "exit_check" regions (default: the volume grown by 4 m), on a grid of "cell" cm:
    - a cell inside the volume whose top is lower than swim_step_lowest (-91 cm under the surface) is DEEP (you swim);
    - any other cell is LOW (top at most climb_out_max = 60 cm above the water: wade, step or Jump-climb out) or HIGH;
    - EXITS: deep cells next to a LOW cell that belongs to land (a connected non-deep area of at least land_min_m2, so
      a lone reef head or rock you can climb onto does not count), and deep cells in a ladder's grab zone;
    - FALL-IN cells: deep cells next to a HIGH land cell (a dock, a jetty, a rock face: where you land when you fall).
    The swim distance from every fall-in cell to the nearest exit (8-way grid path through deep cells) must be at most
    max_swim_cm (default 1200 = 7 s at 170 cm/s). "no_fall_in": [{"center", "size", "yaw", "why"}] marks faces nobody
    can fall from (cliffs whose tops a player cannot reach); their cells are reported ("exempt") but do not fail. Ladders also check that the edge above them (30 cm behind the face) is
    at most their MaxClimbHeight above the water (and warn when it is low enough for a plain Jump climb).
    Returns {"volumes": [...], "ladders": [...], "problems": [...]} (problems start with ERROR or WARN)."""
    import heapq
    expanded = expanded or expand(layout)
    m = metrics(layout)
    lowest, climb = m["swim_step_lowest"], m["climb_out_max"]
    markers = expanded["markers"]
    volumes = [mk for mk in markers if mk["type"] == "water_volume"]
    ladders = [mk for mk in markers if mk["type"] == "ladder"]
    problems, vol_out, lad_out = [], [], []
    for lad in ladders:
        at = v3(lad["at"])
        behind = add(at, rotate(rot_rows(float(lad.get("yaw", 0.0))), (-30.0, 0.0, 0.0)))
        top = top_fn(behind[0], behind[1])
        edge = None if top is None else top - at[2]
        mx = float(ladder_props(lad)["MaxClimbHeight"])
        ok = edge is not None and edge <= mx
        lad_out.append({"id": lad["id"], "edge_cm": None if edge is None else round(edge, 1), "max_climb": mx, "ok": ok,
                        "at": list(at), "yaw": float(lad.get("yaw", 0.0))})
        if not ok:
            problems.append("ERROR ladder %s: edge above it is %s cm over the water, MaxClimbHeight %.0f"
                            % (lad["id"], "missing" if edge is None else "%.0f" % edge, mx))
        elif edge <= climb:
            problems.append("WARN ladder %s: edge is only %.0f cm over the water (a Jump climb already works there)"
                            % (lad["id"], edge))
        front = add(at, rotate(rot_rows(float(lad.get("yaw", 0.0))), (60.0, 0.0, 0.0)))
        ftop = top_fn(front[0], front[1])
        lad_out[-1]["front_top_cm"] = None if ftop is None else round(ftop - at[2], 1)
        if ftop is not None and ftop - at[2] >= lowest:
            problems.append("WARN ladder %s: the bottom 60 cm in front is at %.0f cm, wading depth (only swimmers use "
                            "ladders)" % (lad["id"], ftop - at[2]))
    n4 = [(1, 0), (-1, 0), (0, 1), (0, -1)]
    n8 = n4 + [(1, 1), (1, -1), (-1, 1), (-1, -1)]
    for wv in volumes:
        wat = v3(wv["at"])
        surf = wat[2]
        chk = dict(wv.get("exit_check") or {})
        cell = float(chk.get("cell", 100.0))
        max_swim = float(chk.get("max_swim_cm", 1200.0))
        land_min = float(chk.get("land_min_m2", 60.0))
        regions = chk.get("regions")
        if not regions:
            hx, hy = (float(v) for v in wv["surface_half_size"])
            regions = [{"center": [wat[0], wat[1]], "size": [2 * hx + 800.0, 2 * hy + 800.0]}]
        exempt_rects = [dict(r, at=[r["center"][0], r["center"][1], 0.0]) for r in chk.get("no_fall_in", [])]
        rep = {"id": wv["id"], "cell_cm": cell, "max_swim_cm": max_swim, "regions": [], "fall_in": [], "exits": 0,
               "fails": [], "worst_cm": 0.0, "worst_at": None, "stranded": 0, "exempt": 0}
        wet_ladders = [l for l in ladders if abs(v3(l["at"])[2] - surf) < 1.0]
        for reg in regions:
            cx, cy = float(reg["center"][0]), float(reg["center"][1])
            nx, ny = int(math.ceil(float(reg["size"][0]) / cell)), int(math.ceil(float(reg["size"][1]) / cell))
            x0, y0 = cx - nx * cell / 2.0 + cell / 2.0, cy - ny * cell / 2.0 + cell / 2.0
            kind = {}  # (i, j) -> "deep" | "low" | "high"; cells outside the water with no ground are left out
            for i in range(nx):
                for j in range(ny):
                    x, y = x0 + i * cell, y0 + j * cell
                    top = top_fn(x, y)
                    wet = in_water_volume(wv, x, y)
                    if top is None:
                        if wet:
                            kind[(i, j)] = "deep"
                        continue
                    h = top - surf
                    if wet and h < lowest:
                        kind[(i, j)] = "deep"
                    else:
                        kind[(i, j)] = "low" if h <= climb else "high"
            comp, sizes = {}, []  # land = connected non-deep areas of at least land_min m2 (4-way)
            for start, k in kind.items():
                if k == "deep" or start in comp:
                    continue
                cid = len(sizes)
                stack, n = [start], 0
                comp[start] = cid
                while stack:
                    ci, cj = stack.pop()
                    n += 1
                    for d in n4:
                        nb = (ci + d[0], cj + d[1])
                        if nb in kind and kind[nb] != "deep" and nb not in comp:
                            comp[nb] = cid
                            stack.append(nb)
                sizes.append(n)
            is_land = [n * cell * cell / 10000.0 >= land_min for n in sizes]

            def next_to(c, what):
                for d in n4:
                    nb = (c[0] + d[0], c[1] + d[1])
                    if kind.get(nb) == what and is_land[comp[nb]]:
                        return True
                return False

            dist, heap = {}, []
            for c, k in kind.items():
                if k != "deep":
                    continue
                x, y = x0 + c[0] * cell, y0 + c[1] * cell
                if next_to(c, "low") or any(in_grab_zone(l, x, y) for l in wet_ladders):
                    dist[c] = 0.0
                    heap.append((0.0, c))
                    rep["exits"] += 1
            heapq.heapify(heap)
            while heap:
                dc, c = heapq.heappop(heap)
                if dc > dist.get(c, 1e18):
                    continue
                for d in n8:
                    nb = (c[0] + d[0], c[1] + d[1])
                    if kind.get(nb) != "deep":
                        continue
                    if d[0] and d[1] and (kind.get((c[0] + d[0], c[1])) != "deep"
                                          or kind.get((c[0], c[1] + d[1])) != "deep"):
                        continue  # no squeezing diagonally past a corner
                    nd = dc + cell * (1.41421356 if (d[0] and d[1]) else 1.0)
                    if nd < dist.get(nb, 1e18):
                        dist[nb] = nd
                        heapq.heappush(heap, (nd, nb))
            for c, k in kind.items():
                if k != "deep" or not next_to(c, "high"):
                    continue
                x, y = x0 + c[0] * cell, y0 + c[1] * cell
                dd = dist.get(c)
                ok = dd is not None and dd <= max_swim
                exempt = any(abs(_local_xy(r, x, y)[0]) <= float(r["size"][0]) / 2.0
                             and abs(_local_xy(r, x, y)[1]) <= float(r["size"][1]) / 2.0 for r in exempt_rects)
                status = "ok" if ok else ("exempt" if exempt else "far")
                rep["fall_in"].append([round(x), round(y), None if dd is None else round(dd), status])
                if status == "exempt":
                    rep["exempt"] += 1
                    continue
                if dd is None:
                    rep["stranded"] += 1
                elif round(dd) >= rep["worst_cm"]:
                    rep["worst_cm"], rep["worst_at"] = round(dd), [round(x), round(y)]
                if not ok:
                    rep["fails"].append([round(x), round(y), None if dd is None else round(dd)])
            rep["regions"].append({"center": [cx, cy], "size": [nx * cell, ny * cell], "cells": len(kind)})
        if rep["fails"]:
            worst = sorted(rep["fails"], key=lambda f: -(f[2] if f[2] is not None else 1e18))[:6]
            problems.append("ERROR water %s: %d fall-in cell(s) without an exit within %.0f cm (worst: %s)"
                            % (wv["id"], len(rep["fails"]), max_swim,
                               "; ".join("(%d, %d) %s" % (f[0], f[1], "no exit" if f[2] is None else "%d cm" % f[2])
                                         for f in worst)))
        vol_out.append(rep)
    return {"volumes": vol_out, "ladders": lad_out, "problems": problems}


# ---------------------------------------------------------------------------------------------------------------------
# Water areas (T-027, docs/specs/fishing-water-rules.md): the same geometry and winner rule as the game
# (Source/VibeGame/Fishing/FishingWaterTypes.cpp FLureWaterAreaInfo, FishingWater.cpp FLureWaterRules::FindAreaIndex)
# ---------------------------------------------------------------------------------------------------------------------
def polygon_signed_area(points):
    """Shoelace area (cm^2; positive = counter-clockwise in X/Y)."""
    n = len(points)
    if n < 3:
        return 0.0
    s = 0.0
    for i in range(n):
        x1, y1 = float(points[i][0]), float(points[i][1])
        x2, y2 = float(points[(i + 1) % n][0]), float(points[(i + 1) % n][1])
        s += x1 * y2 - x2 * y1
    return 0.5 * s


def polygon_centroid(points):
    """Area centroid of a polygon (the vertex mean for a degenerate one)."""
    a = polygon_signed_area(points)
    n = len(points)
    if abs(a) < 1e-6:
        return (sum(float(p[0]) for p in points) / max(n, 1), sum(float(p[1]) for p in points) / max(n, 1))
    cx = cy = 0.0
    for i in range(n):
        x1, y1 = float(points[i][0]), float(points[i][1])
        x2, y2 = float(points[(i + 1) % n][0]), float(points[(i + 1) % n][1])
        cross = x1 * y2 - x2 * y1
        cx += (x1 + x2) * cross
        cy += (y1 + y2) * cross
    return (cx / (6.0 * a), cy / (6.0 * a))


def _segments_cross(p1, p2, q1, q2):
    """True if segments p1-p2 and q1-q2 intersect (touching counts)."""
    def orient(a, b, c):
        v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        return 0 if abs(v) < 1e-9 else (1 if v > 0 else -1)

    def on_seg(a, b, c):
        return min(a[0], b[0]) - 1e-9 <= c[0] <= max(a[0], b[0]) + 1e-9 and min(a[1], b[1]) - 1e-9 <= c[1] <= max(a[1], b[1]) + 1e-9

    o1, o2, o3, o4 = orient(p1, p2, q1), orient(p1, p2, q2), orient(q1, q2, p1), orient(q1, q2, p2)
    if o1 != o2 and o3 != o4:
        return True
    return ((o1 == 0 and on_seg(p1, p2, q1)) or (o2 == 0 and on_seg(p1, p2, q2)) or (o3 == 0 and on_seg(q1, q2, p1))
            or (o4 == 0 and on_seg(q1, q2, p2)))


def polygon_is_simple(points):
    """No two non-adjacent edges touch (the outline doesn't cross itself)."""
    pts = [(float(p[0]), float(p[1])) for p in points]
    n = len(pts)
    for i in range(n):
        a1, a2 = pts[i], pts[(i + 1) % n]
        for j in range(i + 1, n):
            if j == i or (j + 1) % n == i or (i + 1) % n == j:
                continue  # adjacent edges share a corner
            if _segments_cross(a1, a2, pts[j], pts[(j + 1) % n]):
                return False
    return True


def _area_frame(mk, x, y):
    """X/Y in a box area's own frame (turned by its yaw around its "at")."""
    at = v3(mk["at"])
    a = math.radians(float(mk.get("yaw", 0.0)))
    dx, dy = x - at[0], y - at[1]
    return dx * math.cos(a) + dy * math.sin(a), -dx * math.sin(a) + dy * math.cos(a)


def water_area_has_shape(mk):
    shape = mk.get("shape")
    if shape == "everywhere":
        return True
    if shape == "circle":
        return float(mk.get("radius", 0) or 0) > 0
    if shape == "box":
        s = mk.get("size") or [0, 0]
        return len(s) >= 2 and float(s[0]) > 0 and float(s[1]) > 0
    if shape == "polygon":
        pts = mk.get("points") or []
        return len(pts) >= 3 and abs(polygon_signed_area(pts)) > 1e-4
    return False


def water_area_contains(mk, x, y):
    """The area's outline contains (x, y): circles and boxes include the edge; polygons use the even-odd rule."""
    if not water_area_has_shape(mk):
        return False
    shape = mk["shape"]
    if shape == "everywhere":
        return True
    if shape == "circle":
        at = v3(mk["at"])
        r = float(mk["radius"])
        return (x - at[0]) ** 2 + (y - at[1]) ** 2 <= r * r
    if shape == "box":
        lx, ly = _area_frame(mk, x, y)
        return abs(lx) <= float(mk["size"][0]) / 2.0 and abs(ly) <= float(mk["size"][1]) / 2.0
    pts = mk["points"]
    inside = False
    n = len(pts)
    j = n - 1
    for i in range(n):
        ax, ay = float(pts[i][0]), float(pts[i][1])
        bx, by = float(pts[j][0]), float(pts[j][1])
        if (ay > y) != (by > y):
            cross_x = ax + (y - ay) * (bx - ax) / (by - ay)
            if x < cross_x:
                inside = not inside
        j = i
    return inside


def water_area_size(mk):
    """Surface area in cm^2 (the tie-break: smaller wins); everywhere = infinite."""
    if not water_area_has_shape(mk):
        return 0.0
    shape = mk["shape"]
    if shape == "everywhere":
        return float("inf")
    if shape == "circle":
        return math.pi * float(mk["radius"]) ** 2
    if shape == "box":
        return float(mk["size"][0]) * float(mk["size"][1])
    return abs(polygon_signed_area(mk["points"]))


def water_area_accepts_depth(mk, depth):
    """min <= depth < max (max 0 = no limit). depth None = any (for 2D questions)."""
    if depth is None:
        return True
    lo, hi = (list(mk.get("depth") or [0, 0]) + [0, 0])[:2]
    lo, hi = max(0.0, float(lo)), float(hi)
    return depth >= lo and (hi <= 0 or depth < hi)


def water_area_outline(mk, segments=64):
    """The outline as world X/Y points (a circle as a polygon); [] for everywhere. For previews and checks."""
    if not water_area_has_shape(mk) or mk["shape"] == "everywhere":
        return []
    shape = mk["shape"]
    if shape == "polygon":
        return [(float(p[0]), float(p[1])) for p in mk["points"]]
    at = v3(mk["at"])
    if shape == "circle":
        r = float(mk["radius"])
        return [(at[0] + r * math.cos(2 * math.pi * i / segments), at[1] + r * math.sin(2 * math.pi * i / segments))
                for i in range(segments)]
    hx, hy = float(mk["size"][0]) / 2.0, float(mk["size"][1]) / 2.0
    rows = rot_rows(float(mk.get("yaw", 0.0)))
    out = []
    for sx, sy in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        w = rotate(rows, (sx * hx, sy * hy, 0.0))
        out.append((at[0] + w[0], at[1] + w[1]))
    return out


def _fname_key(text):
    """Sort key like FName::LexicalLess: case-insensitive text, then a trailing _N number (natural: a_2 < a_10)."""
    m = re.match(r"^(.*)_(0|[1-9][0-9]*)$", str(text))
    if m:
        return (m.group(1).lower(), int(m.group(2)) + 1)
    return (str(text).lower(), 0)


def water_area_better(a, b):
    """a wins over b: higher priority, then the smaller area, then the lower id."""
    pa, pb = int(a.get("priority", 0)), int(b.get("priority", 0))
    if pa != pb:
        return pa > pb
    sa, sb = water_area_size(a), water_area_size(b)
    if sa != sb:
        return sa < sb
    return _fname_key(a["id"]) < _fname_key(b["id"])


def water_area_at(markers, x, y, depth=None):
    """The water_area marker that decides the water at (x, y) (and depth, if given), or None (default water)."""
    best = None
    for mk in markers:
        if mk.get("type") != "water_area" or not str(mk.get("habitat", "")).startswith("Habitat."):
            continue
        if not water_area_accepts_depth(mk, depth) or not water_area_contains(mk, x, y):
            continue
        if best is None or water_area_better(mk, best):
            best = mk
    return best


def _check_water_area(mk, raw_at, problems):
    """Validation of one water_area marker (see the spec's layout schema)."""
    mid = mk["id"]
    shape = mk.get("shape")
    if shape not in WATER_AREA_SHAPES:
        problems.append("ERROR water area %s: shape must be one of %s" % (mid, WATER_AREA_SHAPES))
        return
    if not str(mk.get("habitat", "")).startswith("Habitat."):
        problems.append("ERROR water area %s: habitat must be a Habitat.* tag" % mid)
    if mk.get("region") and not str(mk["region"]).startswith("Region."):
        problems.append("ERROR water area %s: region must be a Region.* tag (or left out)" % mid)
    if not isinstance(mk.get("priority", 0), int):
        problems.append("ERROR water area %s: priority must be an integer" % mid)
    try:
        if float(mk.get("luck", 0.0)) < 0:
            problems.append("ERROR water area %s: luck must be >= 0" % mid)
    except (TypeError, ValueError):
        problems.append("ERROR water area %s: luck must be a number" % mid)
    depth = mk.get("depth", [0, 0])
    if not (isinstance(depth, (list, tuple)) and len(depth) == 2 and all(isinstance(d, (int, float)) for d in depth)):
        problems.append("ERROR water area %s: depth must be [min, max] in cm (max 0 = no limit)" % mid)
    elif depth[0] < 0 or (depth[1] != 0 and depth[1] <= depth[0]):
        problems.append("ERROR water area %s: depth [%s, %s] needs min >= 0 and max 0 or > min" % (mid, depth[0], depth[1]))
    if shape in ("circle", "box") and not raw_at:
        problems.append("ERROR water area %s: a %s needs \"at\" (its center on the water)" % (mid, shape))
    if shape == "circle" and not float(mk.get("radius", 0) or 0) > 0:
        problems.append("ERROR water area %s: radius must be > 0" % mid)
    if shape == "box":
        s = mk.get("size")
        if not (isinstance(s, (list, tuple)) and len(s) == 2 and all(float(v) > 0 for v in s)):
            problems.append("ERROR water area %s: size must be [x, y] > 0 (the full box)" % mid)
    if shape == "polygon":
        pts = mk.get("points") or []
        if len(pts) < 3 or not all(isinstance(p, (list, tuple)) and len(p) >= 2 for p in pts):
            problems.append("ERROR water area %s: a polygon needs 3 or more [x, y] points" % mid)
        else:
            if any(math.hypot(float(pts[i][0]) - float(pts[(i + 1) % len(pts)][0]),
                              float(pts[i][1]) - float(pts[(i + 1) % len(pts)][1])) < 1.0 for i in range(len(pts))):
                problems.append("ERROR water area %s: two points in a row are the same (a zero-length edge)" % mid)
            elif not polygon_is_simple(pts):
                problems.append("ERROR water area %s: the polygon outline crosses itself" % mid)
            elif abs(polygon_signed_area(pts)) < 1.0:
                problems.append("ERROR water area %s: the polygon has no area" % mid)


def _water_area_overlaps(a, b, samples=24):
    """Rough 2D overlap test (bounds, then a sample grid inside the shared bounds); everywhere overlaps everything."""
    def bounds(mk):
        pts = water_area_outline(mk)
        if not pts:
            return None
        return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))
    ba, bb = bounds(a), bounds(b)
    if ba is None or bb is None:
        return water_area_has_shape(a) and water_area_has_shape(b)
    x0, y0, x1, y1 = max(ba[0], bb[0]), max(ba[1], bb[1]), min(ba[2], bb[2]), min(ba[3], bb[3])
    if x0 > x1 or y0 > y1:
        return False
    for i in range(samples):
        for j in range(samples):
            x = x0 + (x1 - x0) * (i + 0.5) / samples
            y = y0 + (y1 - y0) * (j + 0.5) / samples
            if water_area_contains(a, x, y) and water_area_contains(b, x, y):
                return True
    return False


def _check_water_areas_together(layout, markers, problems):
    """Cross-area warnings: equal-priority overlaps, areas off the water, spots whose habitat an area overrides."""
    areas = [mk for mk in markers if mk["type"] == "water_area" and mk.get("shape") in WATER_AREA_SHAPES]
    for i, a in enumerate(areas):
        for b in areas[i + 1:]:
            if int(a.get("priority", 0)) != int(b.get("priority", 0)):
                continue
            if a.get("depth") and b.get("depth") and list(a["depth"]) != list(b["depth"]):
                continue  # different depth bands never compete at one point (approximately)
            if _water_area_overlaps(a, b):
                problems.append("WARN water areas %s and %s overlap with the same priority %s: the smaller wins; say it with priorities"
                                % (a["id"], b["id"], a.get("priority", 0)))
    volumes = [mk for mk in markers if mk["type"] == "water_volume"]
    if volumes:
        for a in areas:
            if a["shape"] == "everywhere":
                continue
            c = polygon_centroid(a["points"]) if a["shape"] == "polygon" else v3(a["at"])[:2]
            if not any(in_water_volume(w, c[0], c[1]) for w in volumes):
                problems.append("WARN water area %s: its center (%.0f, %.0f) is not over a water volume" % (a["id"], c[0], c[1]))
    if areas:
        for s in markers:
            if s["type"] != "fishing_spot":
                continue
            at = v3(s["at"])
            win = water_area_at(markers, at[0], at[1])
            if win is not None and str(win.get("habitat")) != str(s.get("habitat")):
                problems.append("WARN spot %s (%s) lies in water area %s (%s): since T-027 the area decides the fish there"
                                % (s["id"], s.get("habitat"), win["id"], win.get("habitat")))


# ---------------------------------------------------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------------------------------------------------
def validate(layout, expanded=None):
    """Problems as strings (empty = OK). Errors start with 'ERROR', soft rule breaks with 'WARN'."""
    problems = []
    if layout.get("schema") != SCHEMA:
        problems.append("ERROR schema is %r, expected %r" % (layout.get("schema"), SCHEMA))
    for key in ("id", "level_path"):
        if not layout.get(key):
            problems.append("ERROR missing %r" % key)
    expanded = expanded or expand(layout)
    m = metrics(layout)
    mats = layout.get("materials", {})
    seen = set()
    for p in expanded["prims"]:
        if p["id"] in seen:
            problems.append("ERROR duplicate primitive id %s" % p["id"])
        seen.add(p["id"])
        if p["shape"] not in ENGINE_SHAPES:
            problems.append("ERROR %s: unknown shape %r" % (p["id"], p["shape"]))
        if p["mat"] not in mats:
            problems.append("ERROR %s: material %r is not in the layout's materials" % (p["id"], p["mat"]))
        sx, sy, sz = p["size"]
        if sx <= 0 or sy <= 0 or (sz <= 0 and p["shape"] != "plane"):
            problems.append("ERROR %s: size must be positive, got %s" % (p["id"], p["size"]))
        if p["collision"] not in ("block", "none"):
            problems.append("ERROR %s: collision must be block or none" % p["id"])
        meta = p.get("meta") or {}
        if "step_height" in meta and meta["step_height"] > m["stair_step_max"] + 1e-6:
            problems.append("WARN %s: stair step %.1f cm is above %.0f cm" % (p["source"], meta["step_height"],
                                                                            m["stair_step_max"]))
        if "slope_deg" in meta and meta["slope_deg"] > m["walkable_deg"] and "unwalkable" not in p["tags"]:
            problems.append("WARN %s: ramp slope %.1f deg is not walkable (tag it 'unwalkable' if intended)"
                            % (p["source"], meta["slope_deg"]))
    for block in layout.get("blocks", []):
        if block.get("kind") == "pier":
            top = float(block["top"]) - float(layout.get("water_z", 0.0))
            lo, hi = layout.get("pier_height_range", [50.0, 80.0])
            if not (lo <= top <= hi):
                problems.append("WARN %s: deck is %.0f cm above the water (rule %s-%s)" % (block["id"], top, lo, hi))
    ids = set()
    marker_ids = {x["id"] for x in expanded["markers"]}
    for mk in expanded["markers"]:
        if mk["id"] in ids:
            problems.append("ERROR duplicate marker id %s" % mk["id"])
        ids.add(mk["id"])
        t = mk.get("type")
        if t not in MARKER_TYPES:
            problems.append("ERROR marker %s: unknown type %r" % (mk["id"], t))
            continue
        if t == "fishing_spot":
            for key in ("habitat", "region", "radius", "cast_from", "hours"):
                if key not in mk:
                    problems.append("ERROR spot %s: missing %r" % (mk["id"], key))
            if not str(mk.get("habitat", "")).startswith("Habitat."):
                problems.append("ERROR spot %s: habitat must be a Habitat.* tag" % mk["id"])
            if not str(mk.get("region", "")).startswith("Region."):
                problems.append("ERROR spot %s: region must be a Region.* tag" % mk["id"])
        if t == "zone" and mk.get("zone_type") not in ZONE_TYPES:
            problems.append("ERROR zone %s: zone_type must be one of %s" % (mk["id"], ZONE_TYPES))
        if t == "zone" and mk.get("zone_type") == "crawl_gap":
            clear = float(mk["size"][2])
            if not (m["clear_prone"] < clear < m["clear_crouch"]):
                problems.append("ERROR crawl gap %s: clearance %.1f is not between prone %.1f and crouch %.1f"
                                % (mk["id"], clear, m["clear_prone"], m["clear_crouch"]))
        if t == "water_volume":
            hs = mk.get("surface_half_size")
            if not (isinstance(hs, (list, tuple)) and len(hs) == 2 and all(float(v) > 0 for v in hs)):
                problems.append("ERROR water %s: surface_half_size must be [half_x, half_y] > 0" % mk["id"])
                continue
            if float(mk.get("water_depth", 0)) <= 0:
                problems.append("ERROR water %s: water_depth must be > 0" % mk["id"])
            if mk.get("pitch") or mk.get("roll"):
                problems.append("ERROR water %s: keep pitch and roll 0 (the surface is flat)" % mk["id"])
            if abs(v3(mk["at"])[2] - float(layout.get("water_z", 0.0))) > 0.5:
                problems.append("WARN water %s: surface z %.1f is not the layout's water_z %.1f"
                                % (mk["id"], v3(mk["at"])[2], float(layout.get("water_z", 0.0))))
        if t == "ladder":
            wet = [w for w in expanded["markers"] if w["type"] == "water_volume"
                   and in_water_volume(w, v3(mk["at"])[0], v3(mk["at"])[1])]
            if not wet:
                problems.append("ERROR ladder %s: not over a water volume" % mk["id"])
            elif all(abs(v3(w["at"])[2] - v3(mk["at"])[2]) > 0.5 for w in wet):
                problems.append("ERROR ladder %s: origin z must be the water surface" % mk["id"])
        if t == "water_area":
            _check_water_area(mk, not mk.get("_auto_at"), problems)
        if t == "hot_spots":
            types = mk.get("types", [])
            if not (isinstance(types, list) and all(isinstance(x, str) and x for x in types)):
                problems.append("ERROR hot_spots %s: types must be a list of DT_HotSpot row names (empty = every row)" % mk["id"])
            if not isinstance(mk.get("max", 0), int) or mk.get("max", 0) < 0:
                problems.append("ERROR hot_spots %s: max must be an integer >= 0 (leave it out for the settings' MaxHotSpots)" % mk["id"])
            if not isinstance(mk.get("seed", 0), int):
                problems.append("ERROR hot_spots %s: seed must be an integer (0 = random)" % mk["id"])
        if t == "cover_test" and mk.get("stance") not in STANCES:
            problems.append("ERROR cover test %s: stance must be one of %s" % (mk["id"], STANCES))
        if t == "cover_test" and mk.get("vs") not in marker_ids:
            problems.append("ERROR cover test %s: vs %r is not a marker" % (mk["id"], mk.get("vs")))
    if sum(1 for mk in expanded["markers"] if mk.get("type") == "hot_spots") > 1:
        problems.append("ERROR more than one hot_spots marker: a level has one hot spot spawner")
    _check_water_areas_together(layout, expanded["markers"], problems)
    for v in layout.get("views", []):
        if v.get("stance") and v["stance"] not in STANCES:
            problems.append("ERROR view %s: stance must be one of %s" % (v["id"], STANCES))
    if layout.get("time_of_day") and not time_of_day(layout):
        problems.append("ERROR time_of_day %r is not in time_of_day_presets" % layout["time_of_day"])
    for light in expanded["lights"]:
        if light.get("type") not in LIGHT_TYPES:
            problems.append("ERROR light %s: type must be one of %s" % (light.get("id"), LIGHT_TYPES))
        if light.get("type") == "height_fog" and light.get("preset") \
                and light["preset"] not in (layout.get("fog_presets") or {}):
            problems.append("ERROR light %s: fog preset %r is not in fog_presets" % (light["id"], light["preset"]))
        if light.get("type") == "point" and light.get("falloff", "inverse_square") not in ("inverse_square", "soft"):
            problems.append("ERROR light %s: falloff must be inverse_square or soft" % light["id"])
    try:
        for leg in route_table(layout):
            pass
    except KeyError as exc:
        problems.append("ERROR route: %s" % exc)
    return problems


# ---------------------------------------------------------------------------------------------------------------------
# CLI summary
# ---------------------------------------------------------------------------------------------------------------------
def summary(layout):
    ex = expand(layout)
    lines = ["%s (%s): %d primitives, %d lights, %d markers" % (layout["id"], layout["level_path"], len(ex["prims"]),
                                                                  len(ex["lights"]), len(ex["markers"]))]
    probs = validate(layout, ex)
    lines.append("validation: %s" % ("OK" if not probs else "%d problem(s)" % len(probs)))
    lines.extend("  " + p for p in probs)
    tod = time_of_day(layout)
    if tod:
        fog = tod.get("fog") or {}
        lines.append("time of day: %s, exposure EV100 %s (scale %.3f), fog density %s from %s cm" % (
            tod["id"], tod.get("exposure_ev100"), exposure_scale(tod), fog.get("density"), fog.get("start_distance")))
    spots = [mk for mk in ex["markers"] if mk["type"] == "fishing_spot"]
    areas = [mk for mk in ex["markers"] if mk["type"] == "water_area"]
    if spots:
        lines.append("fishing spots%s:" % (" (legacy water areas: the level has no water_area yet)" if not areas else
                                           " (named casting places; the water areas decide the fish)"))
        for s in spots:
            lines.append("  %-14s %-22s r=%4.0f  hours=%s  levels=%s  danger=%s" % (
                s["id"], s["habitat"], s["radius"], s["hours"], s.get("level_band"), s.get("danger", "none")))
    if areas:
        lines.append("water areas (T-027; higher priority wins, then the smaller area):")
        for a in sorted(areas, key=lambda m: (-int(m.get("priority", 0)), water_area_size(m), _fname_key(m["id"]))):
            size = water_area_size(a)
            lines.append("  %-16s %-10s %-22s P%-4s luck %.2f depth %s  %s" % (
                a["id"], a.get("shape"), a.get("habitat"), a.get("priority", 0), float(a.get("luck", 0.0)),
                a.get("depth", [0, 0]), "everywhere" if size == float("inf") else "%.0f m2" % (size / 10000.0)))
    hot = [mk for mk in ex["markers"] if mk["type"] == "hot_spots"]
    lines.append("hot spots: %s" % ("types %s, max %s" % (hot[0].get("types") or "all rows", hot[0].get("max", "settings"))
                                    if hot else "off (no hot_spots marker)"))
    legs = route_table(layout)
    if legs:
        lines.append("route:")
        total_d = total_t = 0.0
        for leg in legs:
            tag = " (optional)" if leg["optional"] else ""
            lines.append("  %-14s -> %-14s %6.1f m %6.1f s  %s%s" % (leg["from"], leg["to"], leg["distance_m"],
                                                                     leg["seconds"], leg["move"], tag))
            if not leg["optional"]:
                total_d += leg["distance_m"]
                total_t += leg["seconds"]
        lines.append("  total (main loop): %.0f m, %.0f s of pure walking" % (total_d, total_t))
    return "\n".join(lines)


if __name__ == "__main__":
    import sys
    for arg in sys.argv[1:] or ["data/levels/L_PalmKey.json"]:
        print(summary(load(arg)))

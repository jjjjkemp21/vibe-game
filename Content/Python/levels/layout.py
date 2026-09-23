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

CLI (plain Python 3, from the repo root):  python Content/Python/levels/layout.py data/levels/L_PalmKey.json
prints counts, validation problems, fishing spots and the route table (distances and walk times).
"""
import json
import math
import os

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
    "cover_test", "clearance_test", "landmark", "label",
)
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
}


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
# Expansion: blocks -> primitives
# ---------------------------------------------------------------------------------------------------------------------
def _prim(pid, shape, center, size, yaw=0.0, pitch=0.0, roll=0.0, mat="default", collision="block", shadow=True,
          group="", tags=None, zone="", visible=True):
    return {
        "id": pid, "shape": shape, "center": v3(center), "size": v3(size), "yaw": float(yaw), "pitch": float(pitch),
        "roll": float(roll), "mat": mat, "collision": collision, "shadow": bool(shadow), "group": group,
        "tags": list(tags or []), "zone": zone, "visible": bool(visible),
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
        pcommon = dict(common, mat=pmat)
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
        if t == "cover_test" and mk.get("stance") not in STANCES:
            problems.append("ERROR cover test %s: stance must be one of %s" % (mk["id"], STANCES))
        if t == "cover_test" and mk.get("vs") not in marker_ids:
            problems.append("ERROR cover test %s: vs %r is not a marker" % (mk["id"], mk.get("vs")))
    for v in layout.get("views", []):
        if v.get("stance") and v["stance"] not in STANCES:
            problems.append("ERROR view %s: stance must be one of %s" % (v["id"], STANCES))
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
    spots = [mk for mk in ex["markers"] if mk["type"] == "fishing_spot"]
    if spots:
        lines.append("fishing spots:")
        for s in spots:
            lines.append("  %-14s %-22s r=%4.0f  hours=%s  levels=%s  danger=%s" % (
                s["id"], s["habitat"], s["radius"], s["hours"], s.get("level_band"), s.get("danger", "none")))
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

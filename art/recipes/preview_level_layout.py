"""Level layout preview (T-005): renders data/levels/<Level>.json in Blender, headless, for the level designer to LOOK at.

Run (all layouts in data/levels/):
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/preview_level_layout.py
One layout only (blender-run.ps1 has no extra-args switch, so use an environment variable):
    $env:LURE_LAYOUT = "data/levels/L_PalmKey.json"; tools/blender-run.ps1 -Recipe art/recipes/preview_level_layout.py

It builds the scene from Content/Python/levels/layout.py expand() (the same primitives the Unreal builder spawns), then:
- map_<id>.png:   orthographic top-down, north (+X) up, east (+Y) right, with a 1 m / 10 m grid, zone labels, fishing-spot
                  rings (dashed = where you cast from), sight cones (the sweep lighter, the resting cone darker), patrol
                  paths, crawl gaps, hazard/threat zones, player starts, cover-test results and the route.
                  Water is drawn SEE-THROUGH on the map (so shelves, reef and channels read); in the game greybox and in the
                  eye-height shots it is opaque, like the builder's palette water.
- eye_<id>.png:   perspective shots at eye height (stand 165 cm, crouch 95, prone 35 from data metrics), 90 deg FOV,
                  with red player proxies (true capsule sizes per stance) where a view asks for them. When the layout's
                  time-of-day preset has a fixed exposure (exposure_ev100), they use the "engine look": Cycles (GPU if
                  present) lit in Unreal units x exposure, the layout's point lights (soft falloff exact), Unreal's height
                  fog formula and filmic tonemapper, so palette hex values read as on-screen targets. Not simulated: the
                  SkyAtmosphere (the sky is the palette gradient preview.sky), Lumen specifics, local exposure. Layouts
                  with in-game labels (dev maps) also show the builder's TextRender labels.
- checks:         cover tests (a ray from a creature's eye to the player's eye point per stance, against colliding
                  geometry only; water never blocks) and clearance tests (ray up from a floor point). Results go to
                  RESULT_JSON and onto the map.
Outputs: Saved/AgentLogs/previews/levels/<Level>/ and a contact sheet Saved/AgentLogs/previews/levels/<Level>.png.
"""
import argparse
import glob
import json
import math
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "art" / "lib"))
sys.path.insert(0, str(REPO / "Content" / "Python"))

import bmesh  # noqa: E402
import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402

import style  # noqa: E402
from levels import layout as L  # noqa: E402

OUT_ROOT = REPO / "Saved" / "AgentLogs" / "previews" / "levels"
INK = "#2B2A26"
PARCHMENT = "#F3E9D2"
DANGER = "#C0392B"
SAFE = "#3FA34D"
ACCENT = style.TROPICAL.ACCENT
LANTERN = style.FOGGY.LANTERN
ROUTE = "#6A3FA0"  # route line on the map only (not a game color): purple reads on sand, grass and water


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--layout", action="append", default=[])
    p.add_argument("--out", default="")       # accepted for blender-run.ps1 compatibility (unused)
    p.add_argument("--preview", default="")   # contact sheet path when a single layout is rendered
    p.add_argument("--save-blend", action="store_true")
    p.add_argument("--quick", action="store_true", help="half resolution, fewer samples")
    a = p.parse_args(argv)
    if not a.layout:
        env = os.environ.get("LURE_LAYOUT", "").strip()
        a.layout = [s for s in env.split(";") if s] if env else sorted(glob.glob(str(REPO / "data" / "levels" / "*.json")))
    return a


# ---------------------------------------------------------------------------------------------------------------------
# Unreal (cm, left-handed, Y east) -> Blender (m, right-handed): x_b = x_u, y_b = -y_u, z_b = z_u; 1 m = 100 cm
# ---------------------------------------------------------------------------------------------------------------------
def to_b(p):
    return Vector((p[0] / 100.0, -p[1] / 100.0, p[2] / 100.0))


def rot_to_b(yaw, pitch, roll):
    rows = L.rot_rows(yaw, pitch, roll)
    f = (1.0, -1.0, 1.0)
    # Unreal column-vector matrix has the local axis images as columns; conjugate with F = diag(1, -1, 1).
    return Matrix([[f[i] * rows[j][i] * f[j] for j in range(3)] for i in range(3)])


# ---------------------------------------------------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------------------------------------------------
def unit_meshes():
    meshes = {}

    def from_bm(name, fill):
        me = bpy.data.meshes.new("unit_" + name)
        bm = bmesh.new()
        fill(bm)
        bm.to_mesh(me)
        bm.free()
        meshes[name] = me

    from_bm("box", lambda bm: bmesh.ops.create_cube(bm, size=1.0))
    from_bm("cylinder", lambda bm: bmesh.ops.create_cone(bm, cap_ends=True, segments=32, radius1=0.5, radius2=0.5,
                                                         depth=1.0))
    from_bm("cone", lambda bm: bmesh.ops.create_cone(bm, cap_ends=True, segments=32, radius1=0.5, radius2=0.0,
                                                     depth=1.0))
    from_bm("sphere", lambda bm: bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=0.5))

    me = bpy.data.meshes.new("unit_plane")
    me.from_pydata([(-0.5, -0.5, 0), (0.5, -0.5, 0), (0.5, 0.5, 0), (-0.5, 0.5, 0)], [], [(0, 1, 2, 3)])
    meshes["plane"] = me
    for name in ("cylinder", "cone", "sphere"):
        for poly in meshes[name].polygons:
            poly.use_smooth = False
    return meshes


def make_materials(layout):
    mats = {}
    for mid, spec in layout["materials"].items():
        m = bpy.data.materials.new("M_" + mid)
        m.use_nodes = True
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        rgba = style.hex_to_linear_rgba(spec["color"])
        m.diffuse_color = rgba
        bsdf.inputs["Base Color"].default_value = rgba
        bsdf.inputs["Roughness"].default_value = float(spec.get("roughness", 0.75))
        emissive = float(spec.get("emissive", 0.0))
        if emissive > 0:
            bsdf.inputs["Emission Color"].default_value = rgba
            bsdf.inputs["Emission Strength"].default_value = min(emissive, 8.0)
        m["lure_water"] = bool(spec.get("water", False))
        mats[mid] = m
    return mats


def set_water_alpha(mats, alpha):
    for m in mats.values():
        if not m.get("lure_water"):
            continue
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        bsdf.inputs["Alpha"].default_value = alpha
        m.surface_render_method = "BLENDED" if alpha < 0.999 else "DITHERED"
        try:
            m.use_transparency_overlap = False
        except Exception:
            pass


def build_scene(layout, ex):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    meshes = unit_meshes()
    mats = make_materials(layout)
    coll = bpy.data.collections.new("Layout")
    scene.collection.children.link(coll)
    objs = {}
    for p in ex["prims"]:
        if not p.get("visible", True):
            continue
        # One mesh datablock per object so each gets its own material slot.
        me = meshes[p["shape"]].copy()
        me.materials.append(mats[p["mat"]])
        o = bpy.data.objects.new(p["id"], me)
        sx, sy, sz = (c / 100.0 for c in p["size"])
        if p["shape"] == "plane":
            sz = 1.0
        o.matrix_world = (Matrix.Translation(to_b(p["center"])) @ rot_to_b(p["yaw"], p["pitch"], p["roll"]).to_4x4()
                          @ Matrix.Diagonal((sx, sy, sz, 1.0)))
        o["collision"] = p["collision"]
        o["water"] = bool(layout["materials"][p["mat"]].get("water", False))
        coll.objects.link(o)
        objs[p["id"]] = o
    bpy.context.view_layer.update()
    return scene, objs, mats


def collision_bvh(objs):
    verts, polys = [], []
    for o in objs.values():
        if o.get("collision") != "block":
            continue
        mw = o.matrix_world
        base = len(verts)
        verts.extend(mw @ v.co for v in o.data.vertices)
        polys.extend([base + i for i in poly.vertices] for poly in o.data.polygons)
    return BVHTree.FromPolygons(verts, polys, all_triangles=False)


# ---------------------------------------------------------------------------------------------------------------------
# Checks (rays against colliding geometry only)
# ---------------------------------------------------------------------------------------------------------------------
def segment_blocked(bvh, a_cm, b_cm):
    a, b = to_b(a_cm), to_b(b_cm)
    d = b - a
    dist = d.length
    hit = bvh.ray_cast(a, d.normalized(), dist - 0.02)
    return hit[0] is not None, (hit[0] if hit[0] is not None else None)


def cover_tests(layout, bvh):
    m = L.metrics(layout)
    idx = L.marker_index(layout)
    out = []
    for mk in idx.values():
        if mk["type"] != "cover_test":
            continue
        cone = idx[mk["vs"]]
        eye = L.v3(cone["at"])
        floor = L.v3(mk["at"])
        stance = mk["stance"]
        p_eye = (floor[0], floor[1], floor[2] + m["eye_" + stance])
        p_top = (floor[0], floor[1], floor[2] + m["height_" + stance] - 5.0)
        eye_blocked, _ = segment_blocked(bvh, eye, p_eye)
        top_blocked, _ = segment_blocked(bvh, eye, p_top)
        dist = L.length(L.sub(p_eye, eye))
        yaw = L.yaw_to(eye, p_eye)
        in_cone = _in_cone(cone, yaw) and dist <= float(cone["range"])
        hidden = eye_blocked
        result = "hidden" if hidden else "visible"
        ok = result == mk.get("expect", result)
        out.append({"id": mk["id"], "vs": mk["vs"], "stance": stance, "result": result, "expect": mk.get("expect"),
                    "pass": ok, "eye_point_blocked": eye_blocked, "capsule_top_blocked": top_blocked,
                    "distance_m": round(dist / 100.0, 1), "inside_sweep": in_cone, "at": list(floor)})
    return out


def _ang_diff(a, b):
    return (a - b + 180.0) % 360.0 - 180.0


def _in_cone(cone, yaw):
    half = float(cone.get("half_angle", 35.0))
    sweep = cone.get("sweep")
    if sweep:
        lo, hi = float(sweep[0]) - half, float(sweep[1]) + half
        mid = (lo + hi) / 2.0
        return abs(_ang_diff(yaw, mid)) <= (hi - lo) / 2.0
    return abs(_ang_diff(yaw, float(cone["yaw"]))) <= half


def clearance_tests(layout, bvh):
    out = []
    for mk in L.marker_index(layout).values():
        if mk["type"] != "clearance_test":
            continue
        floor = L.v3(mk["at"])
        start = to_b((floor[0], floor[1], floor[2] + 1.0))
        hit = bvh.ray_cast(start, Vector((0, 0, 1)), 20.0)
        # Also make sure the floor is where the layout says: ray down from 30 cm above.
        down = bvh.ray_cast(to_b((floor[0], floor[1], floor[2] + 30.0)), Vector((0, 0, -1)), 2.0)
        floor_z = (down[0].z * 100.0) if down[0] is not None else None
        clear = (hit[0].z * 100.0 - (floor_z if floor_z is not None else floor[2])) if hit[0] is not None else None
        lo, hi = mk.get("expect_min"), mk.get("expect_max")
        ok = clear is not None and (lo is None or clear >= lo - 0.05) and (hi is None or clear <= hi + 0.05)
        out.append({"id": mk["id"], "clearance_cm": None if clear is None else round(clear, 2),
                    "floor_z": None if floor_z is None else round(floor_z, 2), "expect_min": lo, "expect_max": hi,
                    "pass": ok, "note": mk.get("note", "")})
    return out


def ground_checks(layout, ex, bvh, tolerance=8.0):
    """Floor points that float above or sink into the colliding geometry (ray down from 40 cm above the point)."""
    pts = []
    for mk in ex["markers"]:
        if mk["type"] in ("player_start", "teleport", "npc", "cover_test", "clearance_test", "boat_mooring")                 and not mk.get("floating"):
            pts.append((mk["id"], L.v3(mk["at"])))
    for v in layout.get("views", []):
        if "eye" in v:
            pts.append(("view:" + v["id"], L.v3(v["eye"])))
    out = []
    for pid, p in pts:
        hit = bvh.ray_cast(to_b((p[0], p[1], p[2] + 40.0)), Vector((0, 0, -1)), 2.4)
        ground = None if hit[0] is None else hit[0].z * 100.0
        delta = None if ground is None else round(p[2] - ground, 1)
        if delta is None or abs(delta) > tolerance:
            out.append({"id": pid, "layout_z": p[2], "ground_z": None if ground is None else round(ground, 1),
                        "delta_cm": delta})
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Rendering helpers
# ---------------------------------------------------------------------------------------------------------------------
def _gradient_world(stops, strength=1.0):
    world = bpy.data.worlds.new("Sky")
    world.use_nodes = True
    nt = world.node_tree
    bg = nt.nodes.get("Background")
    coord = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    nt.links.new(coord.outputs["Generated"], sep.inputs["Vector"])
    nt.links.new(sep.outputs["Z"], ramp.inputs["Fac"])
    els = ramp.color_ramp.elements
    while len(els) < len(stops):
        els.new(0.5)
    for el, (pos, hx) in zip(els, stops):
        el.position = pos
        el.color = style.hex_to_linear_rgba(hx)
    nt.links.new(ramp.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = strength
    world.color = style.linear(stops[-1][1])
    return world


def _sun(scene, azimuth_deg, elevation_deg, strength, hex_color="#FFF4E0"):
    ld = bpy.data.lights.new("Sun", "SUN")
    ld.energy = strength
    ld.color = style.linear(hex_color)
    ld.angle = math.radians(2.0)
    o = bpy.data.objects.new("Sun", ld)
    scene.collection.objects.link(o)
    # Direction the light travels (from the sun toward the ground), azimuth in Unreal yaw (0 = from north).
    az, el = math.radians(azimuth_deg), math.radians(elevation_deg)
    to_sun = Vector((math.cos(az) * math.cos(el), -math.sin(az) * math.cos(el), math.sin(el)))
    o.rotation_euler = (-to_sun).to_track_quat("-Z", "Y").to_euler()
    return o


def _eevee(scene, samples):
    r = scene.render
    r.engine = "BLENDER_EEVEE"
    ee = scene.eevee
    for attr, val in (("taa_render_samples", samples), ("use_shadows", True), ("use_raytracing", False)):
        if hasattr(ee, attr):
            try:
                setattr(ee, attr, val)
            except Exception:
                pass
    vs = scene.view_settings
    vs.view_transform = "Standard"
    vs.look = "None"
    vs.exposure = 0.0


def _render(scene, path, res):
    r = scene.render
    r.resolution_x, r.resolution_y = res
    r.resolution_percentage = 100
    r.image_settings.file_format = "PNG"
    r.image_settings.color_mode = "RGBA" if r.film_transparent else "RGB"
    r.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    return str(path)


def load_rgba(path):
    img = bpy.data.images.load(str(path))
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[::-1].copy()  # row 0 = top


def save_rgba(arr, path):
    h, w = arr.shape[:2]
    img = bpy.data.images.new("out_" + Path(path).stem, w, h, alpha=True)
    img.pixels.foreach_set(np.ascontiguousarray(arr[::-1]).ravel())
    img.filepath_raw = str(path)
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    return str(path)


def srgb(hx):
    return np.array(style.hex_to_srgb(hx), dtype=np.float32)


# ---------------------------------------------------------------------------------------------------------------------
# Map canvas (numpy overlays in world cm)
# ---------------------------------------------------------------------------------------------------------------------
class Canvas:
    def __init__(self, img, center, width_cm, height_cm):
        self.img = img
        self.h, self.w = img.shape[:2]
        self.s = width_cm / self.w            # cm per pixel
        self.y0 = center[1] - width_cm / 2.0  # Unreal Y at the left edge
        self.x0 = center[0] + height_cm / 2.0  # Unreal X at the top edge

    def px(self, p):
        return ((p[1] - self.y0) / self.s, (self.x0 - p[0]) / self.s)

    def world(self, u, v):
        return (self.x0 - v * self.s, self.y0 + u * self.s)

    def _region(self, umin, umax, vmin, vmax):
        u0, u1 = max(0, int(math.floor(umin))), min(self.w, int(math.ceil(umax)) + 1)
        v0, v1 = max(0, int(math.floor(vmin))), min(self.h, int(math.ceil(vmax)) + 1)
        if u0 >= u1 or v0 >= v1:
            return None
        uu, vv = np.meshgrid(np.arange(u0, u1, dtype=np.float32) + 0.5, np.arange(v0, v1, dtype=np.float32) + 0.5)
        wx = self.x0 - vv * self.s
        wy = self.y0 + uu * self.s
        return (v0, v1, u0, u1), wx, wy

    def blend(self, sl, cover, hx, alpha):
        v0, v1, u0, u1 = sl
        a = np.clip(cover, 0.0, 1.0)[..., None] * alpha
        tgt = self.img[v0:v1, u0:u1, :3]
        self.img[v0:v1, u0:u1, :3] = tgt * (1.0 - a) + srgb(hx) * a

    def grid(self, step_cm, hx, alpha, width_px=1.0):
        for k in range(int(math.floor(self.y0 / step_cm)), int(math.ceil((self.y0 + self.w * self.s) / step_cm)) + 1):
            u = (k * step_cm - self.y0) / self.s
            self._vline(u, width_px, hx, alpha)
        x_bottom = self.x0 - self.h * self.s
        for k in range(int(math.floor(x_bottom / step_cm)), int(math.ceil(self.x0 / step_cm)) + 1):
            v = (self.x0 - k * step_cm) / self.s
            self._hline(v, width_px, hx, alpha)

    def _vline(self, u, width, hx, alpha):
        lo, hi = u - width / 2.0, u + width / 2.0
        for col in range(max(0, int(lo)), min(self.w, int(math.ceil(hi)))):
            cov = max(0.0, min(col + 1.0, hi) - max(float(col), lo))
            if cov > 0:
                self.blend((0, self.h, col, col + 1), np.full((self.h, 1), cov, np.float32), hx, alpha)

    def _hline(self, v, width, hx, alpha):
        lo, hi = v - width / 2.0, v + width / 2.0
        for row in range(max(0, int(lo)), min(self.h, int(math.ceil(hi)))):
            cov = max(0.0, min(row + 1.0, hi) - max(float(row), lo))
            if cov > 0:
                self.blend((row, row + 1, 0, self.w), np.full((1, self.w), cov, np.float32), hx, alpha)

    def disc(self, c, r_cm, hx, alpha, ring_px=0.0, dash=0):
        """Filled disc (ring_px=0) or ring of width ring_px; dash>0 draws a dashed ring with that many dashes."""
        pad = r_cm + 4 * self.s
        u0, v0 = self.px((c[0] + pad, c[1] - pad))
        u1, v1 = self.px((c[0] - pad, c[1] + pad))
        reg = self._region(u0, u1, v0, v1)
        if reg is None:
            return
        sl, wx, wy = reg
        d = np.hypot(wx - c[0], wy - c[1]) / self.s
        r = r_cm / self.s
        if ring_px <= 0:
            cover = np.clip(r - d + 0.5, 0, 1)
        else:
            cover = np.clip(ring_px / 2.0 + 0.5 - np.abs(d - r), 0, 1)
            if dash:
                ang = np.arctan2(wy - c[1], wx - c[0])
                cover = cover * ((np.floor((ang + math.pi) / (2 * math.pi) * dash * 2) % 2) == 0)
        self.blend(sl, cover, hx, alpha)

    def segment(self, a, b, width_px, hx, alpha, dash_cm=0.0):
        pad = (width_px + 2) * self.s
        u0, v0 = self.px((max(a[0], b[0]) + pad, min(a[1], b[1]) - pad))
        u1, v1 = self.px((min(a[0], b[0]) - pad, max(a[1], b[1]) + pad))
        reg = self._region(u0, u1, v0, v1)
        if reg is None:
            return
        sl, wx, wy = reg
        ax, ay, bx, by = a[0], a[1], b[0], b[1]
        dx, dy = bx - ax, by - ay
        ll = dx * dx + dy * dy
        t = np.clip(((wx - ax) * dx + (wy - ay) * dy) / ll, 0, 1) if ll > 0 else np.zeros_like(wx)
        d = np.hypot(wx - (ax + t * dx), wy - (ay + t * dy)) / self.s
        cover = np.clip(width_px / 2.0 + 0.5 - d, 0, 1)
        if dash_cm > 0 and ll > 0:
            cover = cover * ((np.floor(t * math.sqrt(ll) / dash_cm) % 2) == 0)
        self.blend(sl, cover, hx, alpha)

    def polyline(self, pts, width_px, hx, alpha, closed=False, dash_cm=0.0):
        seq = list(pts) + ([pts[0]] if closed else [])
        for a, b in zip(seq, seq[1:]):
            self.segment(a, b, width_px, hx, alpha, dash_cm)

    def sector(self, c, yaw_lo, yaw_hi, r_cm, hx, alpha):
        u0, v0 = self.px((c[0] + r_cm, c[1] - r_cm))
        u1, v1 = self.px((c[0] - r_cm, c[1] + r_cm))
        reg = self._region(u0, u1, v0, v1)
        if reg is None:
            return
        sl, wx, wy = reg
        d = np.hypot(wx - c[0], wy - c[1])
        ang = np.degrees(np.arctan2(wy - c[1], wx - c[0]))
        mid, half = (yaw_lo + yaw_hi) / 2.0, (yaw_hi - yaw_lo) / 2.0
        diff = np.abs((ang - mid + 180.0) % 360.0 - 180.0)
        cover = np.clip((r_cm - d) / self.s + 0.5, 0, 1) * np.clip((half - diff) * (d / self.s) * math.pi / 180 + 0.5, 0, 1)
        self.blend(sl, cover, hx, alpha)

    def rect(self, c, size_xy, yaw, hx, alpha, outline_px=0.0):
        rows = L.rot_rows(yaw)
        hx_, hy_ = size_xy[0] / 2.0, size_xy[1] / 2.0
        r = math.hypot(hx_, hy_) + 3 * self.s
        u0, v0 = self.px((c[0] + r, c[1] - r))
        u1, v1 = self.px((c[0] - r, c[1] + r))
        reg = self._region(u0, u1, v0, v1)
        if reg is None:
            return
        sl, wx, wy = reg
        lx = (wx - c[0]) * rows[0][0] + (wy - c[1]) * rows[0][1]
        ly = (wx - c[0]) * rows[1][0] + (wy - c[1]) * rows[1][1]
        ex = (hx_ - np.abs(lx)) / self.s
        ey = (hy_ - np.abs(ly)) / self.s
        inside = np.clip(np.minimum(ex, ey) + 0.5, 0, 1)
        if outline_px > 0:
            cover = inside * np.clip(outline_px + 0.5 - np.minimum(ex, ey), 0, 1)
        else:
            cover = inside
        self.blend(sl, cover, hx, alpha)

    def screen_rect(self, u, v, w, h, hx, alpha):
        u0, u1 = max(0, int(u - w / 2)), min(self.w, int(u + w / 2))
        v0, v1 = max(0, int(v - h / 2)), min(self.h, int(v + h / 2))
        if u0 < u1 and v0 < v1:
            self.blend((v0, v1, u0, u1), np.ones((v1 - v0, u1 - u0), np.float32), hx, alpha)


# ---------------------------------------------------------------------------------------------------------------------
# Top-down map
# ---------------------------------------------------------------------------------------------------------------------
def map_labels(layout, ex, legs, covers, clears):
    """(text, world point cm, size px, color hex, backing) for every map label."""
    labels = []
    opts = layout.get("preview", {})
    n_tp = [0]
    for z in layout.get("zones", []):
        labels.append((z["name"].upper(), L.v3(z.get("label_at", z["center"])), 30, INK, True))
    for mk in ex["markers"]:
        t = mk["type"]
        if t == "fishing_spot":
            lv = mk.get("level_band", [])
            txt = "%s  %s  L%s  %s" % (mk.get("name", mk["id"]), mk["habitat"].replace("Habitat.", ""),
                                       "-".join(str(x) for x in lv), mk.get("time_label", ""))
            at = L.v3(mk["at"])
            labels.append((txt, (at[0] - mk["radius"] - 250, at[1], 0), 17, "#7A1F12", True))
        elif t == "player_start" and mk.get("label", True):
            labels.append((mk.get("name", "START"), L.add(L.v3(mk["at"]), (-250, 0, 0)), 16, "#1E5E28", True))
        elif t in ("npc", "landmark", "teleport", "boat_mooring"):
            if t == "teleport" and mk.get("label") is False:
                continue
            stagger = 0.0
            if t == "teleport":  # alternate teleport labels north/south so neighbouring stations stay readable
                n_tp[0] += 1
                stagger = -300.0 if n_tp[0] % 2 == 0 else 0.0
            labels.append((mk.get("name", mk["id"]), L.add(L.v3(mk["at"]), (220 + stagger, 0, 0)), 16, INK, True))
        elif t == "sight_cone":
            labels.append((mk.get("name", mk["id"]), L.add(L.v3(mk["at"]), (-220, 0, 0)), 16, "#7A1F12", True))
        elif t == "zone" and mk.get("zone_type") == "crawl_gap":
            if opts.get("zone_labels", True):
                labels.append(("CRAWL %d cm" % round(mk["size"][2]), L.add(L.v3(mk["at"]), (0, 0, 0)), 16, "#5A1F7A",
                               True))
        elif t == "zone" and mk.get("name"):
            labels.append((mk["name"], L.v3(mk.get("label_at", mk["at"])), 15, "#7A1F12", True))
        elif t == "label":
            labels.append((mk["text"], L.v3(mk["at"]), int(mk.get("map_px", 15)), INK, True))
    groups = {}
    for c in covers:  # one multi-line label per test location
        groups.setdefault(tuple(round(v) for v in c["at"]), []).append(c)
    for at, cs in groups.items():
        lines = ["%s vs %s: %s%s" % (c["stance"], c["vs"].replace("shadow_", ""), c["result"], "" if c["pass"] else " FAIL")
                 for c in cs]
        ok = all(c["pass"] for c in cs)
        labels.append((chr(10).join(["cover test" + (" OK" if ok else " FAIL")] + lines), L.add(L.v3(at), (-420, 0, 0)), 13,
                       "#1E5E28" if ok else DANGER, True))
    idx = L.marker_index(layout)
    for c in (clears if opts.get("clearance_labels", True) else [c for c in clears if not c["pass"]]):
        mark = "OK" if c["pass"] else "FAIL"
        at = L.add(L.v3(idx[c["id"]]["at"]), (-120, 0, 0))
        labels.append(("%s cm %s" % (c["clearance_cm"], mark), at, 13, SAFE if c["pass"] else DANGER, True))
    return labels


def render_labels_pass(scene, cam, labels, canvas, res, path):
    """Render text objects (emission, no lighting) over a transparent film with the map camera."""
    layout_coll = bpy.data.collections.get("Layout")
    if layout_coll:
        layout_coll.hide_render = True
    tcoll = bpy.data.collections.new("Labels")
    scene.collection.children.link(tcoll)
    mats = {}
    placed = []
    for text, at, size_px, hx, backing in labels:
        if hx not in mats:
            m = bpy.data.materials.new("T_" + hx)
            m.use_nodes = True
            nt = m.node_tree
            for n in list(nt.nodes):
                nt.nodes.remove(n)
            out = nt.nodes.new("ShaderNodeOutputMaterial")
            em = nt.nodes.new("ShaderNodeEmission")
            em.inputs["Color"].default_value = style.hex_to_linear_rgba(hx)
            em.inputs["Strength"].default_value = 1.0
            nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
            mats[hx] = m
        cu = bpy.data.curves.new("lbl", "FONT")
        cu.body = text
        cu.align_x = "CENTER"
        cu.align_y = "CENTER"
        cu.size = size_px * canvas.s / 100.0
        o = bpy.data.objects.new("lbl", cu)
        o.data.materials.append(mats[hx])
        p = to_b((at[0], at[1], 0))
        o.location = (p.x, p.y, 150.0)
        o.rotation_euler = (0.0, 0.0, -math.pi / 2)
        tcoll.objects.link(o)
        placed.append((o, at, backing))
    bpy.context.view_layer.update()
    for o, at, backing in placed:
        if backing:
            u, v = canvas.px(at)
            w_px = o.dimensions.x * 100.0 / canvas.s + 10
            h_px = o.dimensions.y * 100.0 / canvas.s * 1.25 + 6
            canvas.screen_rect(u, v, w_px, h_px, PARCHMENT, 0.72)
    world = scene.world
    scene.render.film_transparent = True
    _render(scene, path, res)
    scene.render.film_transparent = False
    for o, _, _ in placed:
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.collections.remove(tcoll)
    if layout_coll:
        layout_coll.hide_render = False
    scene.world = world
    return load_rgba(path)


def draw_overlays(layout, ex, canvas, legs, covers, view):
    m = L.metrics(layout)
    px_per_m = 100.0 / canvas.s
    if px_per_m >= 3.0:
        canvas.grid(100.0, INK, 0.10 if px_per_m < 8 else 0.16, 1.0)
    canvas.grid(1000.0, INK, 0.30, 1.5 if px_per_m < 8 else 2.0)
    for mk in ex["markers"]:
        if mk["type"] == "zone":
            zt = mk["zone_type"]
            size = mk["size"]
            hx = {"threat": DANGER, "hazard": "#1B2440", "crawl_gap": "#8E3FB0", "quiet": SAFE}.get(zt, INK)
            fill = {"threat": 0.08, "hazard": 0.10, "crawl_gap": 0.45, "quiet": 0.08}.get(zt, 0.06)
            canvas.rect(L.v3(mk["at"]), size[:2], mk.get("yaw", 0.0), hx, fill)
            canvas.rect(L.v3(mk["at"]), size[:2], mk.get("yaw", 0.0), hx, 0.9, outline_px=2.0)
    for mk in ex["markers"]:
        if mk["type"] == "sight_cone":
            c = L.v3(mk["at"])
            half = float(mk.get("half_angle", 35.0))
            rng = float(mk["range"])
            sweep = mk.get("sweep")
            if sweep:
                canvas.sector(c, sweep[0] - half, sweep[1] + half, rng, DANGER, 0.13)
            canvas.sector(c, mk["yaw"] - half, mk["yaw"] + half, rng, DANGER, 0.20)
            for a in ((sweep[0] - half, sweep[1] + half) if sweep else (mk["yaw"] - half, mk["yaw"] + half)):
                end = (c[0] + rng * math.cos(math.radians(a)), c[1] + rng * math.sin(math.radians(a)))
                canvas.segment(c, end, 1.5, DANGER, 0.8)
            canvas.disc(c, 90, DANGER, 1.0)
    for mk in ex["markers"]:
        if mk["type"] == "patrol":
            pts = [L.v3(p) for p in mk["points"]]
            canvas.polyline(pts, 3.0, DANGER, 0.85, closed=mk.get("closed", True), dash_cm=150.0)
            for p in pts:
                canvas.disc(p, 45, DANGER, 0.9)
    for leg in legs:
        canvas.polyline(leg["points"], 2.5, ROUTE, 0.75 if not leg["optional"] else 0.45, dash_cm=120.0)
    for mk in ex["markers"]:
        t = mk["type"]
        if t == "fishing_spot":
            c = L.v3(mk["at"])
            canvas.disc(c, mk["radius"], ACCENT, 0.16)
            canvas.disc(c, mk["radius"], ACCENT, 0.95, ring_px=3.0)
            cf = L.v3(mk["cast_from"])
            canvas.disc(cf, float(mk.get("cast_from_radius", 250.0)), ACCENT, 0.9, ring_px=2.0, dash=12)
            canvas.segment(cf, c, 1.5, ACCENT, 0.7, dash_cm=60.0)
        elif t == "player_start":
            p = L.v3(mk["at"])
            canvas.disc(p, 45, SAFE, 1.0)
            yaw = math.radians(mk.get("yaw", 0.0))
            canvas.segment(p, (p[0] + 150 * math.cos(yaw), p[1] + 150 * math.sin(yaw)), 2.5, SAFE, 1.0)
        elif t in ("npc", "teleport", "boat_mooring", "landmark", "sell_point"):
            p = L.v3(mk["at"])
            hx = {"npc": "#2F5FA0", "teleport": "#1E5E28", "boat_mooring": "#8A5A3B", "landmark": INK,
                  "sell_point": "#C8961E"}[t]
            canvas.disc(p, 60, hx, 1.0)
            canvas.disc(p, 60, PARCHMENT, 1.0, ring_px=2.0)
    for c in covers:
        p = L.v3(c["at"])
        canvas.disc(p, 40, SAFE if c["pass"] else DANGER, 1.0)
    # Scale bar: 10 m, bottom left.
    u0, v0 = 40, canvas.h - 40
    bar_px = 1000.0 / canvas.s
    canvas.screen_rect(u0 + bar_px / 2, v0, bar_px + 16, 22, PARCHMENT, 0.85)
    canvas.screen_rect(u0 + bar_px / 2, v0, bar_px, 6, INK, 1.0)


def render_map(layout, ex, scene, view, legs, covers, clears, out_dir, quick):
    center = L.v3(view["center"])
    w_cm, h_cm = float(view["width"]), float(view["height"])
    ppm = float(view.get("px_per_m", 10.0)) * (0.5 if quick else 1.0)
    res = (int(round(w_cm / 100.0 * ppm)), int(round(h_cm / 100.0 * ppm)))
    cam_d = bpy.data.cameras.new("MapCam")
    cam_d.type = "ORTHO"
    cam_d.sensor_fit = "HORIZONTAL"
    cam_d.ortho_scale = w_cm / 100.0
    cam_d.clip_start = 1.0
    cam_d.clip_end = 400.0
    cam = bpy.data.objects.new("MapCam", cam_d)
    scene.collection.objects.link(cam)
    c = to_b(center)
    cam.location = (c.x, c.y, 200.0)
    cam.rotation_euler = (0.0, 0.0, -math.pi / 2)
    scene.camera = cam
    scene.world = _gradient_world([(0.0, "#10363C"), (1.0, "#10363C")])
    raw = out_dir / ("_raw_map_%s.png" % view["id"])
    _render(scene, raw, res)
    canvas = Canvas(load_rgba(raw), center, w_cm, h_cm)
    draw_overlays(layout, ex, canvas, legs, covers, view)
    labels = map_labels(layout, ex, legs, covers, clears)
    title = "%s  |  %s  |  grid 1 m / 10 m, north up  |  see-through water" % (layout["id"], view.get("title", view["id"]))
    tl_world = canvas.world(24 + len(title) * 5.2, 24)
    labels.append((title, (tl_world[0], tl_world[1], 0), 20, INK, True))
    lab = render_labels_pass(scene, cam, labels, canvas, res, out_dir / ("_raw_labels_%s.png" % view["id"]))
    a = lab[..., 3:4]
    canvas.img[..., :3] = canvas.img[..., :3] * (1 - a) + lab[..., :3] * a
    canvas.img[..., 3] = 1.0
    out = out_dir / ("map_%s.png" % view["id"])
    save_rgba(canvas.img, out)
    bpy.data.objects.remove(cam, do_unlink=True)
    for p in (raw, out_dir / ("_raw_labels_%s.png" % view["id"])):
        try:
            os.remove(p)
        except OSError:
            pass
    return str(out)


# ---------------------------------------------------------------------------------------------------------------------
# Eye-height perspectives
# ---------------------------------------------------------------------------------------------------------------------
def add_proxy(scene, floor_cm, stance, m, mat):
    """Player capsule proxy (radius from DT_Movement: 34 stand/crouch, 25 prone) standing at floor_cm."""
    height = m["height_" + stance]
    radius = 25.0 if stance == "prone" else 34.0
    objs = []
    me = bpy.data.meshes.new("proxy")
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=radius / 100, radius2=radius / 100,
                          depth=max(0.01, (height - 2 * radius) / 100))
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new("proxy_" + stance, me)
    o.location = to_b((floor_cm[0], floor_cm[1], floor_cm[2] + height / 2.0))
    o.data.materials.append(mat)
    scene.collection.objects.link(o)
    objs.append(o)
    for zc in (floor_cm[2] + radius, floor_cm[2] + height - radius):
        sm = bpy.data.meshes.new("proxy_s")
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=8, radius=radius / 100)
        bm.to_mesh(sm)
        bm.free()
        so = bpy.data.objects.new("proxy_s", sm)
        so.location = to_b((floor_cm[0], floor_cm[1], zc))
        so.data.materials.append(mat)
        scene.collection.objects.link(so)
        objs.append(so)
    return objs


# ---------------------------------------------------------------------------------------------------------------------
# Engine look for eye shots: the time-of-day preset's fixed exposure, Unreal's filmic tonemapper, its height fog and the
# layout's point lights (soft falloff exact, via a Cycles light node), rendered with Cycles so the sky light is occluded
# indoors and light bounces (a stand-in for Lumen). Approximations: no SkyAtmosphere (the sky is the palette gradient
# preview.sky; water mixes toward it by Fresnel), the sky light is a uniform ambient (preview.ambient_fraction of the sun's
# horizontal light), inverse-square lights ignore their attenuation radius.
# ---------------------------------------------------------------------------------------------------------------------
ATM_TAU = (0.061, 0.141, 0.272)  # Unreal default Earth atmosphere, vertical optical depth at sea level (R, G, B, + ozone)


def _film_np(x):
    s, b, w, ts, ss, tm, stm, shm = L._film_constants()
    lg = np.log10(np.maximum(x, 1e-7))
    straight = s * (lg + stm)
    toe = np.where(lg < tm, -b + (2.0 * ts) / (1.0 + np.exp((-2.0 * s / ts) * (lg - tm))), straight)
    shoulder = np.where(lg > shm, (1.0 + w) - (2.0 * ss) / (1.0 + np.exp(np.clip((2.0 * s / ss) * (lg - shm), -60, 60))),
                        straight)
    t = np.clip((lg - tm) / (shm - tm), 0.0, 1.0)
    if shm < tm:
        t = 1.0 - t
    t = (3.0 - 2.0 * t) * t * t
    return np.maximum(0.0, toe + (shoulder - toe) * t)


def _film_inverse_np(y):
    xs = np.logspace(-6, 3, 6000)
    ys = _film_np(xs)
    return np.interp(np.clip(y, 0.0, float(ys[-1])), ys, xs)


def _to_srgb_np(lin):
    lin = np.clip(lin, 0.0, 1.0)
    return np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1.0 / 2.4) - 0.055)


def _fog_np(fog, cam_z, length, dz):
    """Vectorized levels.layout.fog_transmittance (same formula, arrays of ray lengths / height changes in cm)."""
    dens = float(fog.get("density", 0.0)) / 1000.0
    fall = float(fog.get("height_falloff", 0.2)) / 1000.0
    start = float(fog.get("start_distance", 0.0))
    if dens <= 0.0:
        return np.ones_like(length)
    t0 = np.clip(start / np.maximum(length, 1e-3), 0.0, 1.0)
    origin = dens * np.power(2.0, -fall * (cam_z + t0 * dz))
    eff = fall * (1.0 - t0) * dz
    eff = np.where(np.abs(eff) > 1e-7, eff, 0.001)
    integral = origin * (1.0 - np.power(2.0, -np.clip(eff, -120, 120))) / eff * (1.0 - t0) * length
    trans = np.power(2.0, -np.clip(integral, 0.0, 120.0))
    return np.clip(trans, 1.0 - float(fog.get("max_opacity", 1.0)), 1.0)


_CYCLES_DEVICE = []


def _use_cycles(scene, samples):
    """Cycles on the GPU when there is one (OptiX / CUDA / HIP / oneAPI / Metal), else the CPU."""
    scene.render.engine = "CYCLES"
    if not _CYCLES_DEVICE:
        dev = "CPU"
        try:
            prefs = bpy.context.preferences.addons["cycles"].preferences
            for t in ("OPTIX", "CUDA", "HIP", "ONEAPI", "METAL"):
                try:
                    prefs.compute_device_type = t
                    prefs.get_devices()
                except Exception:
                    continue
                if any(d.type == t for d in prefs.devices):
                    for d in prefs.devices:
                        d.use = d.type == t
                    dev = t
                    break
        except Exception:
            pass
        _CYCLES_DEVICE.append(dev)
    scene.cycles.device = "CPU" if _CYCLES_DEVICE[0] == "CPU" else "GPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = samples > 4
    scene.cycles.max_bounces = 4
    scene.cycles.diffuse_bounces = 3
    scene.cycles.glossy_bounces = 2
    scene.cycles.transparent_max_bounces = 4


def _soft_falloff_nodes(ld, strength, radius_m, exponent):
    """Cycles light node tree: irradiance = strength x (1 - (d/R)^2)^exponent x cos, independent of 1/d^2 (Unreal's
    non-inverse-squared falloff). With lamp power 4 pi W and Light Falloff 'Constant', E = Strength (calibrated)."""
    ld.energy = 4.0 * math.pi
    ld.use_nodes = True
    nt = ld.node_tree
    em = nt.nodes.get("Emission")
    lf = nt.nodes.new("ShaderNodeLightFalloff")
    nt.links.new(lf.outputs["Constant"], em.inputs["Strength"])
    lp = nt.nodes.new("ShaderNodeLightPath")
    div = nt.nodes.new("ShaderNodeMath")
    div.operation = "DIVIDE"
    div.inputs[1].default_value = radius_m
    nt.links.new(lp.outputs["Ray Length"], div.inputs[0])
    sq = nt.nodes.new("ShaderNodeMath")
    sq.operation = "POWER"
    sq.inputs[1].default_value = 2.0
    nt.links.new(div.outputs[0], sq.inputs[0])
    one = nt.nodes.new("ShaderNodeMath")
    one.operation = "SUBTRACT"
    one.use_clamp = True
    one.inputs[0].default_value = 1.0
    nt.links.new(sq.outputs[0], one.inputs[1])
    pw = nt.nodes.new("ShaderNodeMath")
    pw.operation = "POWER"
    pw.inputs[1].default_value = exponent
    nt.links.new(one.outputs[0], pw.inputs[0])
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = "MULTIPLY"
    mul.inputs[1].default_value = strength
    nt.links.new(pw.outputs[0], mul.inputs[0])
    nt.links.new(mul.outputs[0], lf.inputs["Strength"])


def _water_reflection(mats, layout):
    """Water in the engine look: diffuse palette color plus a Fresnel (IOR 1.33) mix toward the target sky color
    (preview.sky, in exposed scene units), a stand-in for the engine's sky reflection. Done once per scene."""
    sky_hex = layout.get("preview", {}).get("sky", ["#CFEFF8", "#8FD3F0"])[-1]
    refl = [L.ue_filmic_inverse(c) for c in L.hex_to_linear(sky_hex)]
    for m in mats.values():
        nt = m.node_tree
        if not m.get("lure_water") or nt.nodes.get("LureRefl"):
            continue
        bsdf = nt.nodes.get("Principled BSDF")
        out = nt.nodes.get("Material Output")
        bsdf.inputs["Alpha"].default_value = 1.0
        bsdf.inputs["Specular IOR Level"].default_value = 0.0
        em = nt.nodes.new("ShaderNodeEmission")
        em.name = "LureRefl"
        em.inputs["Color"].default_value = (refl[0], refl[1], refl[2], 1.0)
        fr = nt.nodes.new("ShaderNodeFresnel")
        fr.inputs["IOR"].default_value = 1.33
        mix = nt.nodes.new("ShaderNodeMixShader")
        nt.links.new(fr.outputs["Fac"], mix.inputs["Fac"])
        nt.links.new(bsdf.outputs["BSDF"], mix.inputs[1])
        nt.links.new(em.outputs["Emission"], mix.inputs[2])
        nt.links.new(mix.outputs["Shader"], out.inputs["Surface"])


def _engine_lights(layout, scene, view, tod):
    """Sun (with atmosphere transmittance), uniform sky ambient and the layout's point lights, all in exposed units
    (Blender W/m2 = Unreal lux x exposure scale). Returns the objects to delete afterwards."""
    k = L.exposure_scale(tod)
    made = []
    ex_lights = L.expand(layout)["lights"]
    sun_l = next((x for x in ex_lights if x.get("type") == "directional"), None)
    e_sun_h = 0.0
    if sun_l:
        r = sun_l.get("rot", {})
        elev = max(1.0, -float(r.get("pitch", -50.0)))
        az = float(r.get("yaw", 0.0)) + 180.0
        trans = [math.exp(-t / math.sin(math.radians(elev))) for t in ATM_TAU]
        col = [c * t for c, t in zip(L.hex_to_linear(sun_l.get("color", "#FFFFFF")), trans)]
        peak = max(col)
        o = _sun(scene, az, elev, float(sun_l.get("intensity", 10.0)) * k * peak)
        o.data.color = [c / peak for c in col]
        made.append(o)
        lum = 0.2126 * col[0] + 0.7152 * col[1] + 0.0722 * col[2]
        e_sun_h = float(sun_l.get("intensity", 10.0)) * lum * math.sin(math.radians(elev))
    frac = float(view.get("ambient", layout.get("preview", {}).get("ambient_fraction", 0.15)))
    sky_hex = layout.get("preview", {}).get("sky", ["#CFEFF8", "#8FD3F0"])[-1]
    sky_lin = L.hex_to_linear(sky_hex)
    sky_lum = 0.2126 * sky_lin[0] + 0.7152 * sky_lin[1] + 0.0722 * sky_lin[2]
    amb = frac * e_sun_h * k / math.pi / sky_lum
    world = bpy.data.worlds.new("Ambient")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (sky_lin[0] * amb, sky_lin[1] * amb, sky_lin[2] * amb, 1.0)
    bg.inputs["Strength"].default_value = 1.0
    scene.world = world
    for pl in ex_lights:
        if pl.get("type") != "point":
            continue
        ld = bpy.data.lights.new("P_" + pl["id"], "POINT")
        radius_m = float(pl.get("radius", 1500.0)) / 100.0
        if pl.get("falloff", "inverse_square") == "soft":
            _soft_falloff_nodes(ld, float(pl.get("intensity", 8.0)) * k, radius_m,
                                float(pl.get("falloff_exponent", 2.0)))
            ld.shadow_soft_size = 0.1
        else:
            ld.energy = 4.0 * math.pi * float(pl.get("intensity", 8.0)) * k
            ld.shadow_soft_size = 0.05
        ld.color = L.hex_to_linear(pl.get("color", "#E8C46A"))
        ld.use_shadow = bool(pl.get("shadows", False))
        o = bpy.data.objects.new("P_" + pl["id"], ld)
        o.location = to_b(L.v3(pl["at"]))
        scene.collection.objects.link(o)
        made.append(o)
    return made


def _render_exr(scene, path, res):
    r = scene.render
    r.resolution_x, r.resolution_y = res
    r.resolution_percentage = 100
    r.image_settings.file_format = "OPEN_EXR"
    r.image_settings.color_depth = "32"
    r.image_settings.color_mode = "RGBA"
    r.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    return load_rgba(path)


def _engine_render(layout, scene, cam, eye_cm, tod, res, out_dir, tag, samples):
    """Render scene-linear (exposed) color and view distance, then sky, height fog and the filmic tonemapper."""
    engine = scene.render.engine
    scene.render.film_transparent = True
    _use_cycles(scene, samples)
    col = _render_exr(scene, out_dir / ("_raw_eye_%s.exr" % tag), res)
    _use_cycles(scene, 2)  # the distance pass: emission only, no denoising
    dmat = bpy.data.materials.new("ViewDistance")
    dmat.use_nodes = True
    nt = dmat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    cd = nt.nodes.new("ShaderNodeCameraData")
    nt.links.new(cd.outputs["View Distance"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    vl = scene.view_layers[0]
    vl.material_override = dmat
    dep = _render_exr(scene, out_dir / ("_raw_dist_%s.exr" % tag), res)
    vl.material_override = None
    bpy.data.materials.remove(dmat)
    scene.render.film_transparent = False
    scene.render.engine = engine
    for p in (out_dir / ("_raw_eye_%s.exr" % tag), out_dir / ("_raw_dist_%s.exr" % tag)):
        try:
            os.remove(p)
        except OSError:
            pass
    h, w = col.shape[:2]
    a = col[..., 3:4]
    safe_a = np.maximum(a, 1e-4)
    geo = col[..., :3] / safe_a
    dist_cm = dep[..., 0:1] / safe_a * 100.0
    # Per-pixel view direction (world, Blender axes; Z is up in both).
    f_px = cam.data.lens / cam.data.sensor_width * w
    uu, vv = np.meshgrid(np.arange(w, dtype=np.float32) + 0.5, np.arange(h, dtype=np.float32) + 0.5)
    d_cam = np.stack([(uu - w / 2.0) / f_px, (h / 2.0 - vv) / f_px, -np.ones_like(uu)], axis=-1)
    d_cam /= np.linalg.norm(d_cam, axis=-1, keepdims=True)
    rot = np.array(cam.matrix_world.to_3x3(), dtype=np.float32)
    d_world = d_cam @ rot.T
    dir_z = d_world[..., 2:3]
    fog = dict(tod.get("fog") or {})
    fog_col = _film_inverse_np(np.array(L.hex_to_linear(fog.get("color", "#8FD3F0")), np.float32))
    t_geo = _fog_np(fog, eye_cm[2], dist_cm, dist_cm * dir_z) if fog else np.ones_like(dist_cm)
    far = np.full_like(dist_cm, 1e9)
    t_sky = _fog_np(fog, eye_cm[2], far, far * dir_z) if fog else np.ones_like(dist_cm)
    sky = layout.get("preview", {}).get("sky", ["#CFEFF8", "#8FD3F0"])
    elev = np.degrees(np.arcsin(np.clip(dir_z, -1.0, 1.0)))
    fsky = np.clip(elev / 60.0, 0.0, 1.0)
    fsky = fsky * fsky * (3.0 - 2.0 * fsky)
    lo = np.array(L.hex_to_linear(sky[0]), np.float32)
    hi = np.array(L.hex_to_linear(sky[-1]), np.float32)
    sky_exposed = _film_inverse_np(lo * (1.0 - fsky) + hi * fsky)
    geo_f = geo * t_geo + fog_col * (1.0 - t_geo)
    sky_f = sky_exposed * t_sky + fog_col * (1.0 - t_sky)
    final = geo_f * a + sky_f * (1.0 - a)
    img = np.ones((h, w, 4), np.float32)
    img[..., :3] = _to_srgb_np(_film_np(final))
    return img


def _label_objects(layout, scene):
    """The builder's in-game TextRender labels (levels.layout.labels) as upright ink text, for layouts whose labels
    show in game (dev maps), so eye shots show where labels sit relative to the player. Returns the objects."""
    mat = bpy.data.materials.new("LabelInk")
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = style.hex_to_linear_rgba(INK)
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    made = []
    for lab in L.labels(layout, L.expand(layout)["markers"]):
        cu = bpy.data.curves.new("lbl3d", "FONT")
        cu.body = lab["text"]
        cu.align_x = "CENTER"
        cu.align_y = "CENTER"
        cu.size = lab["size"] / 100.0
        o = bpy.data.objects.new("lbl3d_" + lab["id"], cu)
        o.data.materials.append(mat)
        o.location = to_b(lab["at"])
        # Unreal TextRender faces its +X; Blender text faces +Z: stand it up (X +90 deg), then turn to the yaw.
        o.rotation_euler = (math.pi / 2.0, 0.0, math.radians(90.0 - lab["yaw"]))
        scene.collection.objects.link(o)
        made.append(o)
    return made


def render_eye(layout, scene, mats, view, out_dir, quick):
    m = L.metrics(layout)
    tod = L.time_of_day(layout)
    engine_look = tod.get("exposure_ev100") is not None and not view.get("classic_look")
    if "eye_abs" in view:
        eye = L.v3(view["eye_abs"])
    else:
        floor = L.v3(view["eye"])
        eye = (floor[0], floor[1], floor[2] + m["eye_" + view.get("stance", "stand")])
    target = L.v3(view["look_at"]) if "look_at" in view else None
    set_water_alpha(mats, 1.0)
    if engine_look:
        _water_reflection(mats, layout)
        lights = _engine_lights(layout, scene, view, tod)
    else:
        atmo = layout.get("preview", {}).get("sky", ["#CFEFF8", "#8FD3F0"])
        scene.world = _gradient_world([(0.5, atmo[0]), (1.0, atmo[1])])
        sun_spec = layout.get("preview", {}).get("sun", {"azimuth": 135.0, "elevation": 50.0, "strength": 4.0})
        lights = [_sun(scene, sun_spec["azimuth"], sun_spec["elevation"], sun_spec["strength"])]
    if layout.get("labels_in_game"):
        lights += _label_objects(layout, scene)
    pmat = bpy.data.materials.new("proxy")
    pmat.use_nodes = True
    pb_ = pmat.node_tree.nodes.get("Principled BSDF")
    pb_.inputs["Base Color"].default_value = style.hex_to_linear_rgba(ACCENT)
    proxies = []
    for pr in view.get("proxies", []):
        proxies += add_proxy(scene, L.v3(pr["at"]), pr["stance"], m, pmat)
    cam_d = bpy.data.cameras.new("EyeCam")
    cam_d.sensor_fit = "HORIZONTAL"
    cam_d.sensor_width = 36.0
    cam_d.lens = 18.0 / math.tan(math.radians(float(view.get("fov", 90.0))) / 2.0)
    cam_d.clip_start = 0.05
    cam_d.clip_end = 3000.0
    cam = bpy.data.objects.new("EyeCam", cam_d)
    scene.collection.objects.link(cam)
    cam.location = to_b(eye)
    if target is not None:
        cam.rotation_euler = (to_b(target) - to_b(eye)).to_track_quat("-Z", "Y").to_euler()
    else:
        yaw, pitch = float(view.get("yaw", 0.0)), float(view.get("pitch", 0.0))
        d = Vector((math.cos(math.radians(pitch)) * math.cos(math.radians(yaw)),
                    -math.cos(math.radians(pitch)) * math.sin(math.radians(yaw)), math.sin(math.radians(pitch))))
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    res = (960, 540) if quick else (1600, 900)
    if engine_look:
        img = _engine_render(layout, scene, cam, eye, tod, res, out_dir, view["id"], 16 if quick else 64)
    else:
        raw = out_dir / ("_raw_eye_%s.png" % view["id"])
        _render(scene, raw, res)
        img = load_rgba(raw)
        os.remove(raw)
    # Caption strip.
    canvas = Canvas(img, (0, 0, 0), 100.0 * img.shape[1], 100.0 * img.shape[0])
    cap = view.get("caption", view["id"])
    canvas.screen_rect(img.shape[1] / 2, 18, img.shape[1], 36, INK, 0.65)
    lab = _caption_pass(scene, cap, res, out_dir / ("_raw_cap_%s.png" % view["id"]))
    a = lab[..., 3:4]
    img[..., :3] = img[..., :3] * (1 - a) + lab[..., :3] * a
    out = out_dir / ("eye_%s.png" % view["id"])
    save_rgba(img, out)
    for o in proxies + [cam] + lights:
        bpy.data.objects.remove(o, do_unlink=True)
    set_water_alpha(mats, float(layout.get("preview", {}).get("map_water_alpha", 0.55)))
    return str(out)


def _caption_pass(scene, text, res, path):
    """White caption text at the top-left of an image (rendered with its own ortho camera, transparent film)."""
    layout_coll = bpy.data.collections.get("Layout")
    layout_coll.hide_render = True
    cam_d = bpy.data.cameras.new("CapCam")
    cam_d.type = "ORTHO"
    cam_d.sensor_fit = "HORIZONTAL"
    cam_d.ortho_scale = res[0] / 100.0
    cam = bpy.data.objects.new("CapCam", cam_d)
    scene.collection.objects.link(cam)
    cam.location = (10000.0, 10000.0, 10.0)
    cam.rotation_euler = (0, 0, 0)
    old_cam = scene.camera
    scene.camera = cam
    m = bpy.data.materials.new("CapText")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = style.hex_to_linear_rgba(PARCHMENT)
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    cu = bpy.data.curves.new("cap", "FONT")
    cu.body = text
    cu.size = 0.22
    cu.align_x = "LEFT"
    cu.align_y = "CENTER"
    o = bpy.data.objects.new("cap", cu)
    o.data.materials.append(m)
    o.location = (10000.0 - res[0] / 200.0 + 0.14, 10000.0 + res[1] / 200.0 - 0.18, 0.0)
    scene.collection.objects.link(o)
    scene.render.film_transparent = True
    _render(scene, path, res)
    scene.render.film_transparent = False
    lab = load_rgba(path)
    os.remove(path)
    bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.objects.remove(cam, do_unlink=True)
    scene.camera = old_cam
    layout_coll.hide_render = False
    return lab


# ---------------------------------------------------------------------------------------------------------------------
# Contact sheet
# ---------------------------------------------------------------------------------------------------------------------
def contact_sheet(paths, out_path, cell_w=1600):
    """Stack images into one sheet: the first (the overview map) full width, the rest two per row."""
    imgs = [load_rgba(p) for p in paths]

    def fit(img, w):
        h0, w0 = img.shape[:2]
        k = w / w0
        h = max(1, int(h0 * k))
        ys = np.clip((np.arange(h) / k).astype(int), 0, h0 - 1)
        xs = np.clip((np.arange(w) / k).astype(int), 0, w0 - 1)
        return img[ys][:, xs]

    rows = [fit(imgs[0], cell_w)]
    rest = imgs[1:]
    half = cell_w // 2
    for i in range(0, len(rest), 2):
        pair = [fit(im, half) for im in rest[i:i + 2]]
        hh = max(p.shape[0] for p in pair)
        row = np.zeros((hh, cell_w, 4), np.float32)
        row[..., :3] = srgb(INK)
        row[..., 3] = 1
        for j, p in enumerate(pair):
            row[:p.shape[0], j * half:j * half + p.shape[1]] = p
        rows.append(row)
    sheet = np.concatenate(rows, axis=0)
    return save_rgba(sheet, out_path)


# ---------------------------------------------------------------------------------------------------------------------
def run_layout(path, args):
    layout = L.load(path)
    ex = L.expand(layout)
    problems = L.validate(layout, ex)
    legs = L.route_table(layout)
    out_dir = OUT_ROOT / layout["id"]
    out_dir.mkdir(parents=True, exist_ok=True)
    scene, objs, mats = build_scene(layout, ex)
    set_water_alpha(mats, float(layout.get("preview", {}).get("map_water_alpha", 0.55)))
    bvh = collision_bvh(objs)
    covers = cover_tests(layout, bvh)
    clears = clearance_tests(layout, bvh)
    grounds = ground_checks(layout, ex, bvh)
    samples = 8 if args.quick else 24
    _eevee(scene, samples)
    sun_spec = layout.get("preview", {}).get("map_sun", {"azimuth": 150.0, "elevation": 62.0, "strength": 3.5})
    map_sun = _sun(scene, sun_spec["azimuth"], sun_spec["elevation"], sun_spec["strength"])
    maps = [render_map(layout, ex, scene, v, legs, covers, clears, out_dir, args.quick)
            for v in layout.get("preview", {}).get("maps", [])]
    bpy.data.objects.remove(map_sun, do_unlink=True)
    eyes = [render_eye(layout, scene, mats, v, out_dir, args.quick) for v in layout.get("views", [])]
    sheet_path = Path(args.preview) if (args.preview and len(args.layout) == 1) else OUT_ROOT / (layout["id"] + ".png")
    sheet = contact_sheet(maps + eyes, sheet_path) if (maps or eyes) else None
    if args.save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=str(out_dir / (layout["id"] + "_preview.blend")))
    return {
        "layout": str(path), "level_path": layout["level_path"], "primitives": len(ex["prims"]),
        "lights": len(ex["lights"]), "markers": len(ex["markers"]), "problems": problems,
        "maps": maps, "eyes": eyes, "sheet": sheet, "cover_tests": covers, "clearance_tests": clears,
        "ground_issues": grounds,
        "route": [{k: leg[k] for k in ("from", "to", "move", "distance_m", "seconds", "optional")} for leg in legs],
    }


def main():
    args = parse_args()
    results = {}
    failed = []
    for path in args.layout:
        res = run_layout(path, args)
        results[Path(path).stem] = res
        errors = [p for p in res["problems"] if p.startswith("ERROR")]
        bad_tests = [t["id"] for t in res["cover_tests"] + res["clearance_tests"] if not t["pass"]]
        for p in res["problems"]:
            print("[layout] %s: %s" % (Path(path).stem, p))
        for t in res["cover_tests"]:
            print("[cover] %s %s vs %s: %s (expect %s) %s" % (t["id"], t["stance"], t["vs"], t["result"], t["expect"],
                                                             "PASS" if t["pass"] else "FAIL"))
        for t in res["clearance_tests"]:
            print("[clear] %s: %s cm (%s..%s) %s" % (t["id"], t["clearance_cm"], t["expect_min"], t["expect_max"],
                                                      "PASS" if t["pass"] else "FAIL"))
        for g in res["ground_issues"]:
            print("[ground] WARN %s: layout z %s, ground z %s (delta %s cm)" % (g["id"], g["layout_z"], g["ground_z"],
                                                                         g["delta_cm"]))
        if errors or bad_tests:
            failed.append({"layout": Path(path).stem, "errors": errors, "failed_tests": bad_tests})
    print("RESULT_JSON:" + json.dumps({"asset": "preview_level_layout", "layouts": results, "failed": failed}))
    if failed:
        raise SystemExit("Layout problems: %s" % json.dumps(failed))


main()

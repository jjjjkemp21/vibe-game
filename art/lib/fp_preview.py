"""First-person readability previews on palette backdrops, shared by the FP recipes (rod, arms, animations).

Why: Workbench previews over a grey-teal plane hide both problems and wins (designer review B-S2). These frames show
FP items the way the player sees them, over the two backdrops that matter most for the vertical slice, lit like the
approved mood boards in art/reference/ (EEVEE, same sky/water/light recipes).

Camera = the game's FP camera: eye at the origin looking along +X (Blender = Unreal +X), Z up, 90 deg horizontal FOV,
1920x1080. Water 2.3 m below the eye (standing on a dock), 4 km across so the horizon sits on the center row.
Backdrops (docs/ART_STYLE.md palette):
    "day":  tropical day. Sky #8FD3F0 (paler at the horizon), water #3ED1C4 near -> #0E6F7A from ~26 m out,
            warm high sun from behind-right. Standard view transform, exposure -0.2 (as mood board A).
    "dusk": tropical dusk. Sky #FF9A5A at the horizon -> #1B2440 at 30 deg (the frame's top edge), dark water
            (palette water mixed with night), a low sunset sun from the front-right + a cool moon fill. AgX Punchy,
            exposure 0.3 (as mood board B).

Measuring: measure() renders a coverage mask of chosen objects (Workbench, flat white on black, 32x AA), integrates it
across the object at sample points (true on-screen width in pixels, anti-aliasing included) and, on each backdrop
frame, measures the width that actually differs from the local background plus the WCAG contrast ratio.

Preview-only: nothing here is exported. Every stage function returns a cleanup callable that removes what it added.
"""
import math
from pathlib import Path

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector

import pipeline_blender as pb
import style
from style import TROPICAL as T

FP_RESOLUTION = (1920, 1080)
FP_HFOV_DEG = 90.0
EYE_HEIGHT_M = 2.3
BACKDROPS = ("day", "dusk")


# ---------------------------------------------------------------------------------------------------
# Scene pieces
# ---------------------------------------------------------------------------------------------------
def _gradient_world(name, stops, strength):
    """World color by view elevation; stops: [(sin(elevation) 0..1, linear rgba)] (mood-board recipe)."""
    world = bpy.data.worlds.new(name)
    world.use_nodes = True
    nodes, links = world.node_tree.nodes, world.node_tree.links
    bg = nodes.get("Background")
    coord = nodes.new("ShaderNodeTexCoord")
    sep = nodes.new("ShaderNodeSeparateXYZ")
    ramp = nodes.new("ShaderNodeValToRGB")
    links.new(coord.outputs["Generated"], sep.inputs["Vector"])
    links.new(sep.outputs["Z"], ramp.inputs["Fac"])
    els = ramp.color_ramp.elements
    while len(els) < len(stops):
        els.new(0.5)
    for el, (pos, col) in zip(els, stops):
        el.position = pos
        el.color = col
    links.new(ramp.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = strength
    world.color = stops[0][1][:3]
    return world


def _water_material(name, near_rgba, far_rgba, near_m, far_m, roughness):
    """Render-only water: near color within near_m of the eye's foot point, far color beyond far_m."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes.get("Principled BSDF")
    bsdf.inputs["Roughness"].default_value = roughness
    coord = nodes.new("ShaderNodeTexCoord")
    length = nodes.new("ShaderNodeVectorMath")
    length.operation = "LENGTH"
    rng = nodes.new("ShaderNodeMapRange")
    rng.inputs["From Min"].default_value = near_m
    rng.inputs["From Max"].default_value = far_m
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = near_rgba
    ramp.color_ramp.elements[1].color = far_rgba
    links.new(coord.outputs["Object"], length.inputs[0])
    links.new(length.outputs["Value"], rng.inputs["Value"])
    links.new(rng.outputs["Result"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    mat.diffuse_color = far_rgba
    return mat


def _sun(name, from_azimuth_deg, elevation_deg, hex_color, strength, angle_deg):
    """Sun light coming FROM azimuth (0 = straight ahead along +X, 90 = left, +Y) and elevation, in degrees."""
    az, el = math.radians(from_azimuth_deg), math.radians(elevation_deg)
    toward_sun = Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))
    light = bpy.data.lights.new(name, "SUN")
    light.color = style.linear(hex_color)
    light.energy = strength
    light.angle = math.radians(angle_deg)
    obj = bpy.data.objects.new(name, light)
    bpy.context.scene.collection.objects.link(obj)
    obj.rotation_euler = (-toward_sun).to_track_quat("-Z", "Y").to_euler()
    return obj


def _backdrop_spec(kind):
    if kind == "day":
        return {
            "sky": [(0.0, style.mix_hex(T.SKY_DAY, "#FFFFFF", 0.5)), (0.12, style.hex_to_linear_rgba(T.SKY_DAY)),
                    (1.0, style.mix_hex(T.SKY_DAY, "#1F6FA8", 0.5))],
            "sky_strength": 1.0,
            "water": (style.hex_to_linear_rgba(T.SHALLOW_WATER), style.hex_to_linear_rgba(T.DEEP_WATER), 7.0, 26.0,
                      0.12),
            # 60 deg up: high enough that the FP rod's and arms' shadows fall below the frame
            "suns": [("PV_Sun", 200.0, 60.0, "#FFF1D6", 4.0, 2.0)],
            "view": ("Standard", "None", -0.2),
        }
    if kind == "dusk":
        return {
            # night is reached at sin(elev) 0.5 (30 deg: just above the FP frame's top edge), so the frame shows the
            # whole sunset -> night gradient without a hard edge
            "sky": [(0.0, style.hex_to_linear_rgba(T.SUNSET)), (0.06, style.mix_hex(T.SUNSET, T.NIGHT, 0.3)),
                    (0.20, style.mix_hex(T.SUNSET, T.NIGHT, 0.7)), (0.50, style.hex_to_linear_rgba(T.NIGHT)),
                    (1.0, style.mix_hex(T.NIGHT, "#000000", 0.4))],
            "sky_strength": 0.8,
            "water": (style.mix_hex(T.SHALLOW_WATER, T.NIGHT, 0.45), style.mix_hex(T.DEEP_WATER, T.NIGHT, 0.5), 7.0,
                      22.0, 0.08),
            "suns": [("PV_Sun", -55.0, 3.0, T.SUNSET, 1.6, 4.0), ("PV_MoonFill", 150.0, 35.0, "#9FB4E0", 0.35, 1.0)],
            "view": ("AgX", "AgX - Punchy", 0.3),
        }
    raise ValueError("Unknown backdrop %r; choose from %s" % (kind, BACKDROPS))


def stage_backdrop(kind):
    """Sky (world), water plane and lights for backdrop `kind`. Returns a cleanup callable."""
    spec = _backdrop_spec(kind)
    scene = bpy.context.scene
    old_world = scene.world
    world = _gradient_world("PV_FPWorld_" + kind, spec["sky"], spec["sky_strength"])
    scene.world = world
    near, far, near_m, far_m, rough = spec["water"]
    wmat = _water_material("PV_FPWater_" + kind, near, far, near_m, far_m, rough)
    me = bpy.data.meshes.new("PV_FPWater")
    s = 2000.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    me.materials.append(wmat)
    water = bpy.data.objects.new("PV_FPWater", me)
    scene.collection.objects.link(water)
    water.location = (0.0, 0.0, -EYE_HEIGHT_M)
    suns = [_sun(*s_) for s_ in spec["suns"]]

    def cleanup():
        scene.world = old_world
        for o in [water] + suns:
            data = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            if isinstance(data, bpy.types.Mesh):
                bpy.data.meshes.remove(data)
            elif data is not None:
                bpy.data.lights.remove(data)
        bpy.data.materials.remove(wmat)
        bpy.data.worlds.remove(world)
    return cleanup


def fp_camera(hfov_deg=FP_HFOV_DEG):
    """The FP camera at the eye (origin) looking along +X, Z up; becomes the scene camera. Returns (cam, cleanup)."""
    scene = bpy.context.scene
    data = bpy.data.cameras.new("PV_FPCam")
    data.sensor_fit = "HORIZONTAL"
    data.sensor_width = 36.0
    data.lens = pb.lens_for_hfov(hfov_deg)
    data.clip_start = 0.01
    data.clip_end = 5000.0
    cam = bpy.data.objects.new("PV_FPCam", data)
    scene.collection.objects.link(cam)
    cam.rotation_euler = Vector((1.0, 0.0, 0.0)).to_track_quat("-Z", "Y").to_euler()
    old_cam = scene.camera
    scene.camera = cam

    def cleanup():
        scene.camera = old_cam
        bpy.data.objects.remove(cam, do_unlink=True)
        bpy.data.cameras.remove(data)
    return cam, cleanup


def _set_view(scene, transform, look, exposure):
    vs = scene.view_settings
    try:
        vs.view_transform = transform
    except Exception:
        vs.view_transform = "Standard"
    try:
        vs.look = look
    except Exception:
        try:
            vs.look = look.split(" - ")[-1]
        except Exception:
            pass
    vs.exposure = exposure


def _render_settings_snapshot(scene):
    vs, r = scene.view_settings, scene.render
    return (r.engine, r.resolution_x, r.resolution_y, r.resolution_percentage, r.filepath, vs.view_transform, vs.look,
            vs.exposure, r.film_transparent, r.image_settings.file_format, r.image_settings.color_mode)


def _restore_render_settings(scene, snap):
    vs, r = scene.view_settings, scene.render
    (r.engine, r.resolution_x, r.resolution_y, r.resolution_percentage, r.filepath, vt, look, exposure,
     r.film_transparent, fmt, color_mode) = snap
    r.image_settings.file_format = fmt
    r.image_settings.color_mode = color_mode
    vs.view_transform = vt
    vs.look = look
    vs.exposure = exposure


# ---------------------------------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------------------------------
def render_fp(out_path, kind, resolution=FP_RESOLUTION, hfov_deg=FP_HFOV_DEG, samples=32):
    """EEVEE render of everything visible in the scene from the FP camera over backdrop `kind`. Returns the path."""
    scene = bpy.context.scene
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    snap = _render_settings_snapshot(scene)
    _cam, cam_cleanup = fp_camera(hfov_deg)
    bd_cleanup = stage_backdrop(kind)
    r = scene.render
    r.engine = "BLENDER_EEVEE"
    r.resolution_x, r.resolution_y = resolution
    r.resolution_percentage = 100
    r.film_transparent = False
    r.image_settings.file_format = "PNG"
    r.image_settings.color_mode = "RGB"
    r.filepath = str(out_path)
    ee = scene.eevee
    for attr, val in (("taa_render_samples", samples), ("use_raytracing", True), ("use_shadows", True)):
        if hasattr(ee, attr):
            try:
                setattr(ee, attr, val)
            except Exception:
                pass
    _set_view(scene, *_backdrop_spec(kind)["view"])
    try:
        bpy.ops.render.render(write_still=True)
    finally:
        bd_cleanup()
        cam_cleanup()
        _restore_render_settings(scene, snap)
    return str(out_path)


def render_mask(out_path, objects, resolution=FP_RESOLUTION, hfov_deg=FP_HFOV_DEG):
    """Coverage mask of `objects` only (Workbench, flat white on black, 32x AA, Standard view). Returns the path."""
    scene = bpy.context.scene
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    snap = _render_settings_snapshot(scene)
    keep = set(o.name for o in objects)
    hidden = {o.name: o.hide_render for o in scene.objects}
    for o in scene.objects:
        o.hide_render = o.name not in keep
    _cam, cam_cleanup = fp_camera(hfov_deg)
    shading, display = scene.display.shading, scene.display
    old_shading = (shading.light, shading.color_type, tuple(shading.single_color), shading.show_cavity,
                   shading.show_shadows, display.render_aa)
    old_world_color = tuple(scene.world.color) if scene.world else None
    if scene.world is None:
        scene.world = bpy.data.worlds.new("PV_MaskWorld")
    r = scene.render
    r.engine = "BLENDER_WORKBENCH"
    r.resolution_x, r.resolution_y = resolution
    r.resolution_percentage = 100
    r.film_transparent = False
    r.image_settings.file_format = "PNG"
    r.filepath = str(out_path)
    shading.light = "FLAT"
    shading.color_type = "SINGLE"
    shading.single_color = (1.0, 1.0, 1.0)
    shading.show_cavity = False
    shading.show_shadows = False
    display.render_aa = "32"
    scene.world.color = (0.0, 0.0, 0.0)
    _set_view(scene, "Standard", "None", 0.0)
    try:
        bpy.ops.render.render(write_still=True)
    finally:
        (shading.light, shading.color_type, shading.single_color, shading.show_cavity, shading.show_shadows,
         display.render_aa) = old_shading
        if old_world_color is not None:
            scene.world.color = old_world_color
        for o in scene.objects:
            if o.name in hidden:
                o.hide_render = hidden[o.name]
        cam_cleanup()
        _restore_render_settings(scene, snap)
    return str(out_path)


# ---------------------------------------------------------------------------------------------------
# Measuring
# ---------------------------------------------------------------------------------------------------
def _load_pixels(path):
    """Raw 8-bit PNG values (sRGB encoded, 0..1) as an (H, W, 3) numpy array, row 0 = BOTTOM of the image."""
    import numpy as np
    img = bpy.data.images.load(str(path), check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[:, :, :3].copy()


def _srgb_to_linear(a):
    import numpy as np
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def _bilinear(arr, fx, fy):
    """arr (H, W, C) sampled at continuous pixel coords (x right, y up; pixel centers at i + 0.5)."""
    import numpy as np
    h, w = arr.shape[:2]
    x, y = fx - 0.5, fy - 0.5
    x0 = np.clip(np.floor(x).astype(int), 0, w - 2)
    y0 = np.clip(np.floor(y).astype(int), 0, h - 2)
    tx = np.clip(x - x0, 0.0, 1.0)[:, None]
    ty = np.clip(y - y0, 0.0, 1.0)[:, None]
    a = arr[y0, x0] * (1 - tx) + arr[y0, x0 + 1] * tx
    b = arr[y0 + 1, x0] * (1 - tx) + arr[y0 + 1, x0 + 1] * tx
    return a * (1 - ty) + b * ty


def project(point, resolution=FP_RESOLUTION, hfov_deg=FP_HFOV_DEG):
    """World point -> (x px from the left, y px from the BOTTOM, depth m) in the FP camera."""
    scene = bpy.context.scene
    r = scene.render
    old = (r.resolution_x, r.resolution_y, r.resolution_percentage)
    r.resolution_x, r.resolution_y = resolution
    r.resolution_percentage = 100
    cam, cleanup = fp_camera(hfov_deg)
    bpy.context.view_layer.update()
    try:
        v = world_to_camera_view(scene, cam, Vector(point))
    finally:
        cleanup()
        r.resolution_x, r.resolution_y, r.resolution_percentage = old
    return v.x * resolution[0], v.y * resolution[1], v.z


def _relative_luminance(srgb):
    import numpy as np
    lin = _srgb_to_linear(np.asarray(srgb, dtype=np.float64))
    return float(0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2])


def _hex(srgb):
    return "#%02X%02X%02X" % tuple(int(round(max(0.0, min(1.0, c)) * 255)) for c in srgb)


def measure(samples, masks, frames, resolution=FP_RESOLUTION, hfov_deg=FP_HFOV_DEG, half_px=14.0, step=0.1):
    """On-screen widths at sample points along a thin part (e.g. a rod blank).

    samples: [{"name", "point": world Vector on the part's axis, "axis": world unit Vector along the part,
               "radius_m": radius there}]. masks: {name: render_mask() png}, e.g. the bare part and the whole
    object (the difference shows where other pieces overlap it on screen). frames: {backdrop: png path}.
    Per sample: screen position, depth, the analytic width (2 r f / depth), each mask width (integrated coverage
    across the part, anti-aliasing included) and per backdrop frame the visible width (normalized difference to
    the local background, integrated), the part's and the background's colors and their WCAG contrast ratio."""
    import numpy as np
    w, h = resolution
    f_px = (w / 2.0) / math.tan(math.radians(hfov_deg) / 2.0)
    mask_px = {k: _srgb_to_linear(_load_pixels(p)[:, :, 0:1]) for k, p in masks.items()}
    imgs = {k: _load_pixels(p) for k, p in frames.items()}
    t = np.arange(-half_px, half_px + 1e-6, step)
    out = []
    for s in samples:
        p, a = Vector(s["point"]), Vector(s["axis"]).normalized()
        x0, y0, depth = project(p, resolution, hfov_deg)
        x1, y1, _ = project(p + a * 0.02, resolution, hfov_deg)
        d = Vector((x1 - x0, y1 - y0)).normalized()
        n = Vector((-d.y, d.x))
        rec = {"name": s["name"], "screen_px": [round(x0, 1), round(h - y0, 1)], "depth_m": round(depth, 3),
               "screen_angle_deg": round(math.degrees(math.atan2(d.y, d.x)), 1),
               "analytic_px": round(2.0 * s["radius_m"] * f_px / depth, 2), "mask_px": {}}
        for mk_name, mask in mask_px.items():
            widths = []
            for k in (-1.0, 0.0, 1.0):                   # three parallel cuts 1 px apart along the part
                cx, cy = x0 + d.x * k, y0 + d.y * k
                cov = _bilinear(mask, cx + n.x * t, cy + n.y * t)[:, 0]
                widths.append(float(cov.sum() * step))
            rec["mask_px"][mk_name] = round(sum(widths) / len(widths), 2)
        for kind, img in imgs.items():
            cut = _bilinear(img, x0 + n.x * t, y0 + n.y * t)
            edge = int(round(3.0 / step))
            bg_l, bg_r = cut[:edge].mean(axis=0), cut[-edge:].mean(axis=0)
            frac = ((t + half_px) / (2.0 * half_px))[:, None]
            bg = bg_l * (1 - frac) + bg_r * frac
            diff = np.linalg.norm(cut - bg, axis=1)
            core = np.abs(t) <= 2.0
            i_max = int(np.argmax(np.where(core, diff, -1.0)))
            m = float(diff[i_max])
            vis = float(np.clip(diff / m, 0.0, 1.0).sum() * step) if m > 1e-3 else 0.0
            part, back = cut[i_max], bg[i_max]
            la, lb = _relative_luminance(part), _relative_luminance(back)
            rec[kind] = {"visible_px": round(vis, 2), "part": _hex(part), "background": _hex(back),
                         "contrast_ratio": round((max(la, lb) + 0.05) / (min(la, lb) + 0.05), 2),
                         "color_distance": round(m, 3)}
        out.append(rec)
    return out


def zoom_sheet(out_path, tiles, crop=144, scale=5):
    """Nearest-neighbor zoom crops (pixels stay visible): tiles = [[(png path, (x, y from top)), ...], ...] rows
    of crop x crop px regions centered on the given points, each scaled `scale` times. Returns out_path."""
    import numpy as np
    rows = []
    for row in tiles:
        cells = []
        for path, (cx, cy) in row:
            img = _load_pixels(path)[::-1]             # row 0 = top
            h, w = img.shape[:2]
            x0 = int(max(0, min(w - crop, round(cx - crop / 2))))
            y0 = int(max(0, min(h - crop, round(cy - crop / 2))))
            c = img[y0:y0 + crop, x0:x0 + crop]
            c = np.repeat(np.repeat(c, scale, axis=0), scale, axis=1)
            c[:, -2:] = 0.12                             # thin separators
            c[-2:, :] = 0.12
            cells.append(c)
        rows.append(np.concatenate(cells, axis=1))
    sheet = np.concatenate(rows, axis=0)[::-1]           # back to row 0 = bottom
    hh, ww = sheet.shape[:2]
    rgba = np.concatenate([sheet, np.ones((hh, ww, 1), dtype=np.float32)], axis=2)
    out_img = bpy.data.images.new("PV_Zoom", ww, hh, alpha=False)
    out_img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_img.filepath_raw = str(out_path)
    out_img.file_format = "PNG"
    out_img.save()
    bpy.data.images.remove(out_img)
    return str(out_path)

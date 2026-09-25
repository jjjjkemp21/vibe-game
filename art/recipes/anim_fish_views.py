"""anim_fish_views: the fish clips as the player sees them (S3: T-058, T-059, T-060; animation-artist).
No exports: previews and view numbers only. It builds both species on SKEL_Fish exactly like anim_fish.py (that recipe
is imported as a module; nothing is re-authored here) and plays each clip at the rate and amplitude the game will
give it (the S3 effort rule, SK_Fish.anim.md "Eng follow-ups (S3)").

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fish_views.py
Options (env): FISH_VIEWS = comma list of sheets (dock, tired, before, hang, strobe; default all);
FISH_VIEWS_CLIPS = comma list of short clip names for the dock sheets (e.g. Fight_Run,Swim_Tired).
RESULT_JSON: Saved/AgentLogs/blender/anim_fish_views.result.json (screen motion numbers per clip and view).

Previews (Saved/AgentLogs/previews/):
  SK_Fish_dock_<Clip>.png          from the dock: eye 2.3 m above the water (1.7 m eye on a 0.6 m dock), the game camera
                                   (90 deg, 1920x1080) aimed at the fish, EEVEE day backdrop (fp_preview) with a
                                   see-through water surface; the fish 30 cm deep at 5 m, 50 cm deep at 12 m. Each cell
                                   is the GAME-PIXEL crop around the fish (120x80 px at 5 m, 60x40 px at 12 m), zoomed
                                   x2 / x4 nearest-neighbour so the pixels stay honest. Columns = game time at the
                                   clip's in-game play rate; rows = species x view (behind = the fish faces away,
                                   side = its right side, front = coming toward the player), a tiring fish and, for
                                   Run / Swim_Fast / Dive, the clip at today's 0.5 floor ("TODAY's rule, reeling").
                                   RESULT_JSON numbers per row, keyed clip|species|distance|view|stamina|rate|note.
  SK_Fish_tired_vs_run.png         the same dock view, 5 m behind: Run fresh / Run tiring / Swim_Tired / Swim_Idle
  SK_Fish_exhausted_before_after.png  today's exhausted look (Swim_Idle at 0.5 x0.5, rolled 70 deg) vs Swim_Tired
                                   upright, from behind and from the front
  A_Fish_Hooked_Hang_fp.png        first person (fp_preview camera, 90 deg): the landed fish hanging nose-up from the
                                   drawn rod tip (FirstPersonScale 0.6) on the 40 cm hang line, its right side to the
                                   viewer; day and dusk, full frame + game-pixel crops of the key frames
  A_Fish_Hooked_Hang_strobe.png    midlines of every 2nd frame through each kick, player view and edge view: they all
                                   start at the hook (the Mouth does not move)

Conventions: Blender +X = the fish's nose (Unreal +X), +Y = the fish's LEFT (Unreal -Y), +Z up (anim_fish.py).
"""
import importlib.util
import json
import math
import os
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from bpy_extras.object_utils import world_to_camera_view  # noqa: E402
from mathutils import Euler, Matrix, Vector  # noqa: E402

import fp_preview as fp  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
PREVIEW_DIR = pb.PREVIEW_ROOT
TMP = PREVIEW_DIR / "anim_fish_views_cells"
T0 = time.time()


def _load_recipe(stem):
    spec = importlib.util.spec_from_file_location(stem + "_mod", str(REPO / "art" / "recipes" / (stem + ".py")))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


af = _load_recipe("anim_fish")
fr = af.fr

SHEETS = {s.strip() for s in os.environ.get("FISH_VIEWS", "dock,tired,before,hang,strobe").split(",") if s.strip()}
ONLY = {s.strip() for s in os.environ.get("FISH_VIEWS_CLIPS", "").split(",") if s.strip()}
W, H = fp.FP_RESOLUTION
HFOV = fp.FP_HFOV_DEG
F_PX = (W / 2.0) / math.tan(math.radians(HFOV) / 2.0)
EYE_H = fp.EYE_HEIGHT_M          # eye above the water surface (1.7 m eye on a 0.6 m dock)
GAME_FPS = 30.0


def log(msg):
    print("[anim_fish_views %6.1fs] %s" % (time.time() - T0, msg), flush=True)


def clip(name):
    return next(c for c in fr.CLIPS if c.name == name)


# ---------------------------------------------------------------------------------------------------------------
# How the game plays a clip (S3 effort rule; SK_Fish.anim.md "Eng follow-ups (S3)")
# ---------------------------------------------------------------------------------------------------------------
LOOK = {r["Name"]: r for r in json.loads((REPO / "data" / "tables" / "DT_FishSpecies.json").read_text("utf-8"))}
# role: (rate multiplier for a fresh fish, for a spent one); PlayRate = AnimRate x (RefWeight/Weight)^(1/6) x
# lerp(tired, fresh, stamina), no speed coupling while fighting. Amplitude = AnimAmplitude x lerp(0.75, 1, stamina).
EFFORT = {"Run": (1.0, 0.6), "SwimFast": (1.0, 0.6), "Dive": (1.0, 0.6), "Dart": (1.0, 0.7), "Thrash": (1.0, 0.7),
          "SwimIdle": (1.0, 0.8), "Tired": (1.0, 1.0), "Hang": (1.0, 1.0)}
TIRED_AMP_SHARE = 0.75


def rate_amp(species, role, stamina=1.0):
    look = LOOK[species]
    fresh, tired = EFFORT[role]
    rate = look["AnimRate"] * (tired + (fresh - tired) * stamina)
    amp = look["AnimAmplitude"]
    if role not in ("Tired", "Hang"):
        amp *= TIRED_AMP_SHARE + (1.0 - TIRED_AMP_SHARE) * stamina
    return rate, amp


def clip_frame(c, t, rate, start=0.0):
    """Clip frame at game time t (s) for a player started at `start` s into the clip (Unreal: looping player)."""
    f = start * GAME_FPS + t * GAME_FPS * rate
    return f % c.frames if c.loop else min(f, c.frames)


# ---------------------------------------------------------------------------------------------------------------
# Images (numpy; row 0 = the BOTTOM of the image, as Blender stores it)
# ---------------------------------------------------------------------------------------------------------------
def load_px(path):
    img = bpy.data.images.load(str(path), check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[:, :, :3].copy()


def save_px(arr, path):
    h, w = arr.shape[:2]
    rgba = np.concatenate([arr, np.ones((h, w, 1), dtype=np.float32)], axis=2)
    img = bpy.data.images.new("PV_Out", w, h, alpha=False)
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    img.filepath_raw = str(path)
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    return str(path)


def zoom(arr, k):
    return np.repeat(np.repeat(arr, k, axis=0), k, axis=1) if k > 1 else arr


def crop(arr, cx, cy_top, cw, ch):
    """cw x ch crop centered at (cx, cy measured from the TOP), clamped to the image."""
    h, w = arr.shape[:2]
    top = arr[::-1]
    x0 = int(max(0, min(w - cw, round(cx - cw / 2.0))))
    y0 = int(max(0, min(h - ch, round(cy_top - ch / 2.0))))
    return top[y0:y0 + ch, x0:x0 + cw][::-1].copy()


def grid(rows, path, pad=3, bg=0.08):
    """rows: [[cell array, ...], ...] (cells of one row share a height). Written top row first."""
    widths = [sum(c.shape[1] for c in r) + pad * (len(r) + 1) for r in rows]
    heights = [max(c.shape[0] for c in r) + pad for r in rows]
    out = np.full((sum(heights) + pad, max(widths), 3), bg, dtype=np.float32)
    y = out.shape[0]
    for r, rh in zip(rows, heights):
        y -= rh
        x = pad
        for c in r:
            out[y + (rh - pad - c.shape[0]):y + rh - pad, x:x + c.shape[1]] = c
            x += c.shape[1] + pad
    return save_px(out, path)


def label_cell(text, size, name):
    """A title cell (Workbench): parchment text on the ink plate, over ink; lines about 13 px high."""
    path = TMP / ("label_%s.png" % name)
    scene = bpy.context.scene
    cam_data = bpy.data.cameras.new("PV_LabelCam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 1.0
    cam = bpy.data.objects.new("PV_LabelCam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = (0.0, 0.0, 50.0)
    old_cam = scene.camera
    scene.camera = cam
    bpy.context.view_layer.update()
    staged = af.add_label(text, cam, size, rel_size=13.0 / size[1])
    scene.camera = old_cam
    bpy.data.objects.remove(cam, do_unlink=True)
    bpy.data.cameras.remove(cam_data)
    hidden = {o.name: o.hide_render for o in scene.objects if o not in staged}
    for o in scene.objects:
        if o.name in hidden:
            o.hide_render = True
    try:
        af.render(path, (0.0, 0.0, 50.0), Euler((0.0, 0.0, 0.0)), ortho=1.0, res=size, bg=style.UI.INK)
    finally:
        for o in scene.objects:
            if o.name in hidden:
                o.hide_render = hidden[o.name]
        af.remove_objects(staged)
    return load_px(path)


# ---------------------------------------------------------------------------------------------------------------
# Scene: the dock (see-through water) and the fish placement
# ---------------------------------------------------------------------------------------------------------------
# The water surface (an approximation of the game's clear Palm Key water, checked against the A2 playtest shot
# 03_fight_start: a fish 25-60 cm down reads as a darker, cyan-tinted shape on bright water). What lies under the
# surface is MULTIPLIED by WATER_TINT (light absorbed on the way up) and WATER_SURFACE of the water's own lit color is
# added on top (surface reflection + scattering). The bottom is an opaque copy of fp_preview's water BOTTOM_DEPTH down.
WATER_TINT = (0.52, 0.80, 0.80)
WATER_SURFACE = 0.16
BOTTOM_DEPTH = 2.5


def stage_dock(kind="day"):
    """fp_preview's sky, sun and water; its water plane becomes a see-through surface (no shadow) over an opaque copy
    (the bottom, BOTTOM_DEPTH below), so open water looks as in fp_preview and a fish under the surface is tinted."""
    scene = bpy.context.scene
    cleanup_bd = fp.stage_backdrop(kind)
    water = bpy.data.objects["PV_FPWater"]
    mat = water.data.materials[0]
    bottom_mat = mat.copy()
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes.get("Principled BSDF")
    out = next(n for n in nodes if n.type == "OUTPUT_MATERIAL")
    clear = nodes.new("ShaderNodeBsdfTransparent")
    clear.inputs["Color"].default_value = WATER_TINT + (1.0,)
    mix = nodes.new("ShaderNodeMixShader")
    mix.inputs["Fac"].default_value = WATER_SURFACE
    links.new(clear.outputs[0], mix.inputs[1])
    links.new(bsdf.outputs[0], mix.inputs[2])
    links.new(mix.outputs[0], out.inputs["Surface"])
    if hasattr(mat, "surface_render_method"):
        mat.surface_render_method = "BLENDED"
    elif hasattr(mat, "blend_method"):
        mat.blend_method = "BLEND"
    if hasattr(water, "visible_shadow"):
        water.visible_shadow = False
    me = water.data.copy()
    me.materials.clear()
    me.materials.append(bottom_mat)
    bottom = bpy.data.objects.new("PV_Bottom", me)
    scene.collection.objects.link(bottom)
    bottom.location = water.location + Vector((0.0, 0.0, -BOTTOM_DEPTH))
    view = fp._backdrop_spec(kind)["view"]

    def cleanup():
        bpy.data.objects.remove(bottom, do_unlink=True)
        bpy.data.meshes.remove(me)
        bpy.data.materials.remove(bottom_mat)
        cleanup_bd()
    return cleanup, view


def place(fish, pos, heading_deg=0.0, pitch_deg=0.0, roll_deg=0.0):
    """Armature (and its skinned mesh) at pos; nose along heading (deg about Z from +X, + = toward +Y), pitch (+ nose
    up), roll (+ = its RIGHT side down, the Unreal actor roll of ExhaustedRollDeg)."""
    R = (Matrix.Rotation(math.radians(heading_deg), 4, "Z") @ Matrix.Rotation(math.radians(-pitch_deg), 4, "Y")
         @ Matrix.Rotation(math.radians(roll_deg), 4, "X"))
    fish.arm.matrix_world = Matrix.Translation(Vector(pos)) @ R
    bpy.context.view_layer.update()


def unplace(fish):
    fish.arm.matrix_world = Matrix.Identity(4)
    af.pose_fish(fish, fr.Pose().quats())


class Cam:
    """A game camera at `eye` aimed at `target` (90 deg, 1920x1080); becomes the scene camera until close()."""

    def __init__(self, eye, target):
        scene = bpy.context.scene
        self.data = bpy.data.cameras.new("PV_ViewCam")
        self.data.sensor_fit = "HORIZONTAL"
        self.data.sensor_width = 36.0
        self.data.lens = pb.lens_for_hfov(HFOV)
        self.data.clip_start = 0.05
        self.data.clip_end = 5000.0
        self.obj = bpy.data.objects.new("PV_ViewCam", self.data)
        scene.collection.objects.link(self.obj)
        self.obj.location = Vector(eye)
        self.obj.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat("-Z", "Y").to_euler()
        self.old = scene.camera
        scene.camera = self.obj
        bpy.context.view_layer.update()

    def px(self, p):
        """World point -> (x px from the left, y px from the TOP) at 1920x1080."""
        scene = bpy.context.scene
        r = scene.render
        r.resolution_x, r.resolution_y, r.resolution_percentage = W, H, 100
        v = world_to_camera_view(scene, self.obj, Vector(p))
        return v.x * W, (1.0 - v.y) * H

    def close(self):
        bpy.context.scene.camera = self.old
        bpy.data.objects.remove(self.obj, do_unlink=True)
        bpy.data.cameras.remove(self.data)


def render_eevee(path, view, border=None, samples=16):
    """EEVEE render at 1920x1080 through the scene camera; border = (cx, cy_top, cw, ch) px renders and writes only that
    crop (the game's pixels, nothing resampled)."""
    scene = bpy.context.scene
    r = scene.render
    r.engine = "BLENDER_EEVEE"
    r.resolution_x, r.resolution_y, r.resolution_percentage = W, H, 100
    r.film_transparent = False
    r.image_settings.file_format = "PNG"
    r.image_settings.color_mode = "RGB"
    r.filepath = str(path)
    if border:
        cx, cy, cw, ch = border
        x0 = int(round(cx - cw / 2.0))
        y0 = int(round((H - cy) - ch / 2.0))              # Blender's border is measured from the bottom
        r.use_border, r.use_crop_to_border = True, True
        r.border_min_x, r.border_max_x = x0 / W, (x0 + cw) / W
        r.border_min_y, r.border_max_y = y0 / H, (y0 + ch) / H
    ee = scene.eevee
    for attr, val in (("taa_render_samples", samples), ("use_shadows", True)):
        if hasattr(ee, attr):
            try:
                setattr(ee, attr, val)
            except Exception:
                pass
    fp._set_view(scene, *view)
    try:
        bpy.ops.render.render(write_still=True)
    finally:
        r.use_border, r.use_crop_to_border = False, False
    img = load_px(path)
    if border and img.shape[:2] != (border[3], border[2]):     # guard: Blender rounds the border to whole pixels
        img = crop(np.pad(img, ((0, max(0, border[3] - img.shape[0])), (0, max(0, border[2] - img.shape[1])),
                                (0, 0)), mode="edge"), border[2] / 2.0, border[3] / 2.0, border[2], border[3])
    return img


# ---------------------------------------------------------------------------------------------------------------
# Screen motion (RESULT_JSON): where the tail tip, the mouth and the body go on screen, per game second
# ---------------------------------------------------------------------------------------------------------------
def tail_tip(fish):
    return af.skin_matrix(fish, "Tail") @ Vector((fish.geo.tip_x, 0.0, 0.0))


def mouth(fish):
    return fish.arm.matrix_world @ fish.arm.pose.bones["Mouth"].head


def screen_motion(fish, c, rate, amp, cam, seconds=3.0, start=0.0):
    """Tail tip / mouth screen speed (px per game second at 1080p) over `seconds` of game time, sampled per game frame,
    and the tail tip's excursion around its mean position (px)."""
    n = int(seconds * GAME_FPS)
    tails, mouths = [], []
    for k in range(n + 1):
        f = clip_frame(c, k / GAME_FPS, rate, start)
        af.pose_fish(fish, af.clip_quats(c, f, amp))
        tails.append(Vector(cam.px(tail_tip(fish)) + (0.0,)))
        mouths.append(Vector(cam.px(mouth(fish)) + (0.0,)))

    def speeds(pts):
        return [(b - a).length * GAME_FPS for a, b in zip(pts, pts[1:])]
    ts, ms = speeds(tails), speeds(mouths)
    mean_t = sum(tails, Vector()) / len(tails)
    ts_sorted = sorted(ts)
    return {"tail_px_s_mean": round(statistics.mean(ts), 1), "tail_px_s_p90": round(ts_sorted[int(0.9 * (len(ts) - 1))], 1),
            "mouth_px_s_mean": round(statistics.mean(ms), 1),
            "tail_excursion_px": round(max((p - mean_t).length for p in tails), 1)}


# ---------------------------------------------------------------------------------------------------------------
# Dock film strips
# ---------------------------------------------------------------------------------------------------------------
HEADING = {"behind": 20.0, "quarter": 55.0, "side": -90.0, "front": 160.0}   # the fish's nose vs the line of sight
VIEW_TEXT = {"behind": "behind", "quarter": "rear quarter", "side": "side", "front": "front"}
DIST = {5.0: (0.30, (120, 80), 2), 12.0: (0.50, (60, 40), 4)}     # distance: depth, crop px, zoom
COLS = 12
CELL = (240, 160)


class Row:
    def __init__(self, species, view, dist, stamina=1.0, clip_name=None, rate=None, amp=None, roll=0.0, pitch=0.0,
                 start=0.0, note=""):
        self.species, self.view, self.dist, self.stamina = species, view, dist, stamina
        self.clip_name, self.rate, self.amp, self.roll, self.pitch = clip_name, rate, amp, roll, pitch
        self.start, self.note = start, note


def film(fishes, rows, dt, name, title):
    """One sheet: per row, COLS cells at game times 0, dt, 2 dt, ... (row.start seconds into the clip)."""
    by = {f.species: f for f in fishes}
    labels = []
    for i, row in enumerate(rows):
        c = clip(row.clip_name)
        r0, a0 = rate_amp(row.species, c.role, row.stamina)
        row.rate = r0 if row.rate is None else row.rate
        row.amp = a0 if row.amp is None else row.amp
        depth = DIST[row.dist][0]
        text = "%s\n%s %.0f m, %s\nrate %.2f, alpha %.2f%s\n%.2f s per cell" % (
            af.short(c.name), row.species, row.dist, VIEW_TEXT[row.view], row.rate, row.amp,
            (", stamina %.1f" % row.stamina) if row.stamina < 1.0 else "", dt)
        if row.roll:
            text += "\nactor roll %.0f deg" % row.roll
        if row.note:
            text += "\n" + row.note
        if i == 0:
            text = title + "\n" + text
        labels.append(label_cell(text, CELL, "%s_%d" % (name, i)))
    cleanup, view = stage_dock("day")
    out_rows, numbers = [], {}
    try:
        for i, row in enumerate(rows):
            fish = by[row.species]
            c = clip(row.clip_name)
            depth, (cw, ch), k = DIST[row.dist]
            pos = (row.dist, 0.0, -EYE_H - depth)
            place(fish, pos, HEADING[row.view], row.pitch, row.roll)
            cam = Cam((0.0, 0.0, 0.0), pos)
            cells = [labels[i]]
            with af.solo_render(fishes, fish):
                for j in range(COLS):
                    f = clip_frame(c, j * dt, row.rate, row.start)
                    af.pose_fish(fish, af.clip_quats(c, f, row.amp))
                    img = render_eevee(TMP / ("%s_%d_%02d.png" % (name, i, j)), view, (W / 2.0, H / 2.0, cw, ch))
                    cells.append(zoom(img, k))
            # the rate and the note are part of the key: rows that differ only there (the fresh fish vs "TODAY's rule",
            # the tired fish from its stroke vs from its glide) must not overwrite each other
            key = "%s|%s|%.0fm|%s|stamina %.1f|rate %.2f" % (af.short(c.name), row.species, row.dist, row.view,
                                                              row.stamina, row.rate)
            if row.note:
                key += "|" + row.note
            numbers[key] = dict(screen_motion(fish, c, row.rate, row.amp, cam, start=row.start),
                                rate=round(row.rate, 3), alpha=round(row.amp, 3), roll_deg=row.roll)
            cam.close()
            unplace(fish)
            out_rows.append(cells)
            log("%s row %d %s" % (name, i, numbers[key]))
    finally:
        cleanup()
    return grid(out_rows, PREVIEW_DIR / ("%s.png" % name)), numbers


# Which rows each clip's dock sheet shows (the views the game produces; SK_Fish.anim.md "Seen from the dock").
FIGHT_ROWS = {
    "A_Fish_Fight_Run": [("Bonefish", "behind", 5.0), ("Bonefish", "side", 5.0), ("CoralSnapper", "behind", 5.0),
                         ("CoralSnapper", "side", 5.0), ("Bonefish", "behind", 12.0)],
    "A_Fish_Swim_Fast": [("Bonefish", "quarter", 5.0), ("Bonefish", "side", 5.0), ("CoralSnapper", "quarter", 5.0),
                         ("CoralSnapper", "side", 5.0), ("Bonefish", "quarter", 12.0)],
    "A_Fish_Fight_Dive": [("CoralSnapper", "behind", 5.0), ("CoralSnapper", "side", 5.0), ("Bonefish", "behind", 5.0),
                          ("Bonefish", "side", 5.0), ("CoralSnapper", "behind", 12.0)],
    "A_Fish_Fight_Dart": [("Bonefish", "side", 5.0), ("Bonefish", "behind", 5.0), ("CoralSnapper", "side", 5.0),
                          ("CoralSnapper", "behind", 5.0), ("Bonefish", "side", 12.0)],
    "A_Fish_Hooked_Thrash": [("Bonefish", "behind", 5.0), ("Bonefish", "side", 5.0), ("CoralSnapper", "behind", 5.0),
                             ("CoralSnapper", "side", 5.0), ("Bonefish", "behind", 12.0)],
    "A_Fish_Swim_Tired": [("Bonefish", "front", 5.0), ("Bonefish", "behind", 5.0), ("Bonefish", "side", 5.0),
                          ("CoralSnapper", "front", 5.0), ("CoralSnapper", "behind", 5.0), ("Bonefish", "front", 12.0)],
}
DIVE_PITCH = -20.0        # the diving fish's actor pitch (FacingRotation: climb angle, clamped to MaxPitchDeg 30)
# Film step for the fight clips: 2 game frames. A 1/12 s step sampled the 5-6 Hz head shakes near their zero crossings
# (they looked calm in the strip while the clip is not); 1/15 s spreads the samples over the shake's phase.
FIGHT_DT = 1.0 / 15.0
# Today's speed-coupled rule (T-029 FightFishVisual::ComputeAnimState) while the player reels a fresh fish: the fish's
# ground speed is reel drag, so the swim roles sit at MinPlayRate 0.5 (Saved/AgentLogs/tasks/S3-fish/
# gateA_playrate_table.md: Bonefish Run 0.50, Swim 0.50-0.60 letting it run; CoralSnapper Dive 0.59-0.73). One row per
# sheet shows the clip at 0.5, i.e. what the player sees if the effort rule (S3 eng follow-up 1) is not adopted.
TODAY_REEL_RATE = {"Run": 0.5, "SwimFast": 0.5, "Dive": 0.5}


def dock_sheets(fishes):
    out, numbers = {}, {}
    for name, spec in FIGHT_ROWS.items():
        if ONLY and af.short(name) not in ONLY:
            continue
        c = clip(name)
        pitch = DIVE_PITCH if c.role == "Dive" else 0.0
        rows = [Row(s, v, d, clip_name=name, pitch=pitch) for s, v, d in spec]
        if c.role == "Tired":
            dt = c.frames / GAME_FPS / rate_amp("Bonefish", c.role)[0] / COLS      # one loop of the Bonefish
            title = "DOCK VIEW, the exhausted fish (T-058)"
        else:
            rows.append(Row(spec[0][0], spec[0][1], 5.0, stamina=0.2, clip_name=name, pitch=pitch,
                            note="tiring fish"))
            if c.role in TODAY_REEL_RATE:
                rows.append(Row(spec[0][0], spec[0][1], 5.0, clip_name=name, pitch=pitch,
                                rate=TODAY_REEL_RATE[c.role], note="TODAY's rule, reeling"))
            dt = FIGHT_DT
            title = "DOCK VIEW, fighting fish (T-059)"
        path, nums = film(fishes, rows, dt, "SK_Fish_dock_" + af.short(name), title)
        out[name] = path
        numbers.update(nums)
    return out, numbers


def tired_vs_run(fishes):
    rows = [Row("Bonefish", "behind", 5.0, clip_name="A_Fish_Fight_Run", note="fresh"),
            Row("Bonefish", "behind", 5.0, stamina=0.3, clip_name="A_Fish_Fight_Run", note="tiring"),
            Row("Bonefish", "behind", 5.0, clip_name="A_Fish_Swim_Tired", start=26.0 / GAME_FPS,
                note="from its stroke (f26)"),
            Row("Bonefish", "behind", 5.0, clip_name="A_Fish_Swim_Tired", note="from its glide (f0)"),
            Row("Bonefish", "behind", 5.0, clip_name="A_Fish_Swim_Idle", note="Rest move (calm)")]
    return film(fishes, rows, FIGHT_DT, "SK_Fish_tired_vs_run", "TIRED vs RUN, 0.8 s of game time")


def exhausted_before_after(fishes):
    """Today's exhausted fish (T-029 data: SwimIdle at ExhaustedPlayRate 0.5, alpha x ExhaustedAmplitudeScale 0.5,
    actor roll ExhaustedRollDeg 70) vs S3 (Swim_Tired at the effort rule, alpha 1, roll 0 after T-058a)."""
    old_rate = 0.5
    old_amp = LOOK["Bonefish"]["AnimAmplitude"] * 0.5
    rows = []
    for v in ("behind", "front"):
        rows.append(Row("Bonefish", v, 5.0, clip_name="A_Fish_Swim_Idle", rate=old_rate, amp=old_amp, roll=70.0,
                        note="BEFORE (today's data)"))
        rows.append(Row("Bonefish", v, 5.0, clip_name="A_Fish_Swim_Tired", note="AFTER (S3, upright)"))
    return film(fishes, rows, 3.0 / COLS, "SK_Fish_exhausted_before_after", "EXHAUSTED FISH, before / after (T-058)")


# ---------------------------------------------------------------------------------------------------------------
# Hooked_Hang in first person
# ---------------------------------------------------------------------------------------------------------------
# HoldRod_Idle frame 0 (art/export/Characters/SK_FPArms.anim.md): hand_r_rod in camera space (Unreal cm, P / Y / R deg)
ROD_POSE_UE = ((42.9, 21.0, -23.0), (35.1, -4.8, 0.0))
FIRST_PERSON_SCALE = 0.6          # ALurePlayerCharacter: the rod is drawn scaled toward the eye; the line starts there
HANG_LINE_M = 0.40                # DT_Catch HangLineLength
HANG_KEYS = [0, 9, 12, 14, 16, 19, 24, 29, 44, 60, 80, 85, 109]


def stage_rod():
    """SM_Rod_Basic in the HoldRod_Idle pose, drawn at FirstPersonScale about the eye. Returns (drawn tip, cleanup)."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(REPO / "art" / "export" / "Props" / "SM_Rod_Basic.fbx"))
    new = [o for o in bpy.data.objects if o not in before]
    rod = next(o for o in new if o.type == "MESH" and o.parent is None)
    tip = next(o for o in new if o.name.split(".")[0] == pb.SOCKET_PREFIX + "LineTip")
    (x, y, z), (p, yaw, r) = ROD_POSE_UE
    R = (Matrix.Rotation(math.radians(-yaw), 4, "Z") @ Matrix.Rotation(math.radians(-p), 4, "Y")
         @ Matrix.Rotation(math.radians(r), 4, "X"))
    pivot = Matrix.Translation(Vector((x, -y, z)) / 100.0) @ R
    for o in new:
        if o.parent is None:
            o.matrix_world = Matrix.Scale(FIRST_PERSON_SCALE, 4) @ pivot @ o.matrix_world
    bpy.context.view_layer.update()
    drawn_tip = tip.matrix_world.translation.copy()

    def cleanup():
        for o in new:
            bpy.data.objects.remove(o, do_unlink=True)
    return drawn_tip, cleanup


def hang_matrix(fish, hook, eye):
    """UpdateHooked: nose (+X) straight up the line, the fish's RIGHT side (Blender -Y) turned to the viewer; the
    REFERENCE-pose Mouth sits on the hook."""
    up = Vector((0.0, 0.0, 1.0))
    to_viewer = Vector(eye) - Vector(hook)
    to_viewer -= up * to_viewer.dot(up)
    to_viewer.normalize()
    x, y = up, -to_viewer
    z = x.cross(y)
    R = Matrix((x, y, z)).transposed().to_4x4()
    m_rest = fish.arm.data.bones["Mouth"].head_local
    return Matrix.Translation(Vector(hook) - R.to_3x3() @ m_rest) @ R


def add_line(p0, p1, radius, hex_color, name="PV_HangLine"):
    return af.add_polyline(name, [Vector(p0), Vector(p1)], hex_color, radius=radius)


def hang_box(fishes, c, hook, eye):
    """Screen box (x0, y0, x1, y1 px from the top-left) that holds both species over the whole clip, + a margin."""
    cam = Cam(eye, eye + Vector((1.0, 0.0, 0.0)))
    xs, ys = [], []
    for fish in fishes:
        fish.arm.matrix_world = hang_matrix(fish, hook, eye)
        for f in range(0, c.frames, 2):
            af.pose_fish(fish, af.clip_quats(c, f))
            mw = fish.arm.matrix_world
            for p in fish.probe.coords()[::3]:
                x, y = cam.px(mw @ p)
                xs.append(x)
                ys.append(y)
        unplace(fish)
    hx, hy = cam.px(hook)
    cam.close()
    m = 16
    return (int(min(xs + [hx]) - m), int(min(ys + [hy]) - m - 30), int(max(xs + [hx]) + m), int(max(ys) + m))


def hang_fp_sheet(fishes):
    """First person, day and dusk: a full frame (Bonefish f14, the first coil) per backdrop + game-pixel crops of the
    key frames for both species (the same screen box in every cell, so the hook stays put across the row)."""
    c = clip("A_Fish_Hooked_Hang")
    drawn_tip, cleanup_rod = stage_rod()
    hook = drawn_tip - Vector((0.0, 0.0, HANG_LINE_M))
    eye = Vector((0.0, 0.0, 0.0))
    line = add_line(drawn_tip, hook, 0.0012, "#F5F1E6")
    cam = Cam(eye, eye + Vector((1.0, 0.0, 0.0)))
    tip_px, hook_px = cam.px(drawn_tip), cam.px(hook)
    cam.close()
    numbers = {"drawn_rod_tip_m": [round(v, 3) for v in drawn_tip], "hook_m": [round(v, 3) for v in hook],
               "drawn_rod_tip_screen_pct": [round(100.0 * tip_px[0] / W, 1), round(100.0 * tip_px[1] / H, 1)],
               "hook_screen_pct": [round(100.0 * hook_px[0] / W, 1), round(100.0 * hook_px[1] / H, 1)]}
    x0, y0, x1, y1 = hang_box(fishes, c, hook, eye)
    numbers["crop_box_px"] = [x0, y0, x1, y1]
    cw, ch = x1 - x0, y1 - y0
    rows = []
    try:
        for kind in fp.BACKDROPS:
            for fish in fishes:
                fish.arm.matrix_world = hang_matrix(fish, hook, eye)
                bpy.context.view_layer.update()
                rate, amp = rate_amp(fish.species, "Hang")
                cells = []
                with af.solo_render(fishes, fish):
                    for j, f in enumerate(HANG_KEYS):
                        af.pose_fish(fish, af.clip_quats(c, f, amp))
                        path = TMP / ("hang_fp_%s_%s_%02d.png" % (kind, fish.species, j))
                        fp.render_fp(path, kind, samples=16)
                        img = load_px(path)
                        if f == 14 and fish is fishes[0]:
                            save_px(img, PREVIEW_DIR / ("A_Fish_Hooked_Hang_fp_%s_full.png" % kind))
                        cells.append(crop(img, (x0 + x1) / 2.0, (y0 + y1) / 2.0, cw, ch))
                label = label_cell("Hooked_Hang\nFIRST PERSON\n%s, %s\nrate %.2f\nalpha %.2f\n\ncells: frames\n%s"
                                   % (fish.species, kind, rate, amp, "\n".join(
                                       ", ".join("f%d" % f for f in HANG_KEYS[i:i + 4]) for i in range(0, len(HANG_KEYS), 4))),
                                   (200, ch), "hangfp_%s_%s" % (kind, fish.species))
                rows.append([label] + cells)
                unplace(fish)
                log("hang fp %s %s" % (kind, fish.species))
    finally:
        af.remove_objects([line])
        cleanup_rod()
    numbers["full_frames"] = [str(PREVIEW_DIR / ("A_Fish_Hooked_Hang_fp_%s_full.png" % k)) for k in fp.BACKDROPS]
    return grid(rows, PREVIEW_DIR / "A_Fish_Hooked_Hang_fp.png"), numbers


def hang_mouth_world(fishes):
    """The Mouth in WORLD space while hanging, every frame, alpha 1 and 0.8 (Unreal's Apply Additive scales the
    rotations from the identity with a normalized lerp; a rotation and its inverse scale to inverses, so the head
    stays put at any alpha): the largest distance from the hook (mm)."""
    c = clip("A_Fish_Hooked_Hang")
    hook = Vector((1.0, 0.0, 0.3))
    out = {}
    for fish in fishes:
        fish.arm.matrix_world = hang_matrix(fish, hook, Vector((0.0, 0.0, 0.3)))
        worst = {}
        for amp in (1.0, 0.8):
            d = 0.0
            for f in range(c.frames + 1):
                q = c.pose(f).quats()
                q = {n: nlerp_identity(v, amp) for n, v in q.items()}
                af.pose_fish(fish, q)
                d = max(d, (mouth(fish) - hook).length * 1000.0)
            worst["alpha_%.1f" % amp] = round(d, 5)
        out[fish.species] = worst
        unplace(fish)
    return out


def nlerp_identity(q, a):
    """Unreal FTransform::BlendFromIdentityAndAccumulate: VectorLerpQuat(identity, q, a), normalized."""
    if a >= 0.9999:
        return q.copy()
    w = q.w if q.w >= 0.0 else -q.w
    s = 1.0 if q.w >= 0.0 else -1.0
    from mathutils import Quaternion
    r = Quaternion(((1.0 - a) + a * w, a * s * q.x, a * s * q.y, a * s * q.z))
    r.normalize()
    return r


def midline3d(fish):
    co = fish.probe.coords()
    pts = [co[fish.nose_vert]] + [sum((co[i] for i in r), Vector()) / len(r) for r in fish.probe.rings]
    tip = fish.arm.matrix_world.inverted() @ tail_tip(fish)
    return pts + [tip]


def hang_strobe(fishes):
    """Every 2nd frame of the first burst (f8-f36) and of the second (f74-f98): the midlines (old -> new: deep water ->
    reef) over the pale resting fish, in the player view (its right side, nose up) and the edge view (from its back).
    Ink dot = the hook (the reference Mouth), ink line = the fishing line: every midline starts on the hook."""
    import bmesh
    c = clip("A_Fish_Hooked_Hang")
    fish = fishes[0]
    res = (420, 560)
    m = fish.arm.data.bones["Mouth"].head_local.copy()
    phrases = [("first burst f8-f36", list(range(8, 37, 2))), ("second burst f74-f98", list(range(74, 99, 2)))]
    views = {"player view (its right side)": Vector((0.0, -1.0, 0.0)), "edge view (from its back)": Vector((0.0, 0.0, 1.0))}
    labels, cells = [], []
    ghost = bpy.data.objects.new("PV_Ghost", fish.mesh.data)
    bpy.context.scene.collection.objects.link(ghost)
    for slot in ghost.material_slots:
        slot.link = "OBJECT"
        slot.material = af.flat_mat("#C9DADB")
    ghost.location = (0.0, 0.0, 0.0)
    ring = bpy.data.meshes.new("PV_Hook")
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=8, radius=0.010)
    bm.to_mesh(ring)
    bm.free()
    ring.materials.append(af.flat_mat(style.UI.INK))
    hook_obj = bpy.data.objects.new("PV_Hook", ring)
    bpy.context.scene.collection.objects.link(hook_obj)
    hook_obj.location = m
    line = af.add_polyline("PV_Line", [m, m + Vector((0.5, 0.0, 0.0))], style.UI.INK, radius=0.0012)
    try:
        with af.solo_render(fishes, None):
            for vname, back in views.items():
                for pname, frames in phrases:
                    stage = []
                    for j, f in enumerate(frames):
                        af.pose_fish(fish, af.clip_quats(c, f))
                        stage.append(af.add_polyline("PV_Strobe", midline3d(fish), af.ramp(j / max(1, len(frames) - 1)),
                                                     radius=0.0022))
                    af.pose_fish(fish, fr.Pose().quats())
                    tgt = Vector((0.06, 0.0, 0.0))
                    loc = tgt + back * 3.0
                    ghost.location = -back * 0.3                  # behind the midlines (ortho: same size)
                    fwd = -back
                    up = Vector((1.0, 0.0, 0.0))
                    right = fwd.cross(up).normalized()
                    up = right.cross(fwd).normalized()
                    rot = Matrix((right, up, -fwd)).transposed().to_euler()
                    path = TMP / ("strobe_%s_%s.png" % (vname[:4], pname[:5]))
                    af.render(path, loc, rot, ortho=0.7, res=res, bg=af.BG_LIGHT, light="FLAT")
                    cells.append(load_px(path))
                    labels.append(label_cell("Hooked_Hang, %s\n%s, every 2nd frame\nmidlines old -> new (teal -> coral)"
                                             "\npale: the fish at rest; ink dot: the hook" % (vname, pname),
                                             (res[0], 72), "strobe_%s_%s" % (vname[:4], pname[:5])))
                    af.remove_objects(stage)
    finally:
        bpy.data.objects.remove(ghost, do_unlink=True)
        af.remove_objects([hook_obj, line])
    af.pose_fish(fish, fr.Pose().quats())
    return grid([labels, cells], PREVIEW_DIR / "A_Fish_Hooked_Hang_strobe.png")


# ---------------------------------------------------------------------------------------------------------------
def main():
    TMP.mkdir(parents=True, exist_ok=True)
    pb.reset_scene()
    scene = bpy.context.scene
    scene.render.fps = fr.FPS
    scene.render.fps_base = 1.0
    fishes = [af.Fish(stem, asset, species) for stem, asset, species in af.SPECIES]
    for f in fishes:
        fr.mute_all(f.arm)
    import fishkit as fk
    fk.preview_setup()
    result = {"sheets": {}, "numbers": {}, "effort_rule": {k: list(v) for k, v in EFFORT.items()},
              "water": {"tint": WATER_TINT, "surface": WATER_SURFACE, "bottom_m": BOTTOM_DEPTH},
              "eye_above_water_m": EYE_H,
              "rates": {s: {role: [round(x, 3) for x in rate_amp(s, role, st)] for role in EFFORT for st in (1.0,)}
                        for s in LOOK}}
    if "dock" in SHEETS:
        paths, nums = dock_sheets(fishes)
        result["sheets"]["dock"] = paths
        result["numbers"].update(nums)
    if "tired" in SHEETS:
        p, nums = tired_vs_run(fishes)
        result["sheets"]["tired_vs_run"] = p
        result["numbers"].update({"tired_vs_run|" + k: v for k, v in nums.items()})
    if "before" in SHEETS:
        p, nums = exhausted_before_after(fishes)
        result["sheets"]["exhausted_before_after"] = p
        result["numbers"].update({"before_after|" + k: v for k, v in nums.items()})
    if "hang" in SHEETS:
        p, nums = hang_fp_sheet(fishes)
        result["sheets"]["hang_fp"] = p
        result["hang_fp"] = nums
        result["hang_mouth_world_mm"] = hang_mouth_world(fishes)
    if "strobe" in SHEETS:
        result["sheets"]["hang_strobe"] = hang_strobe(fishes)
    log("done")
    print("RESULT_JSON:" + json.dumps(result))


if __name__ == "__main__":
    main()

"""Preview only (no export): SM_Cooler_Starter in the scenes the player sees it in (designer review 2026-09-23).

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/preview_cooler_starter.py
Preview: Saved/AgentLogs/previews/SM_Cooler_Scenes.png (contact sheet) + the single frames next to it:
  _fp_day / _fp_dusk  first person while carrying: SK_FPArms in A_FPArms_CarryCooler_Idle frame 0 (anim_fp_arms.py pose
                      solver, read only) with the cooler on the `cooler` bone; the game's FP camera (90 deg, 1920x1080)
  _capacity           open cooler on the dock with 4 fish laid in (2 bonefish + 2 coral snappers at their
                      ReferenceWeight size, packed by pack_fish: every vertex inside the liner and under the closed
                      lid, no fish intersecting another), a bonefish and a coral snapper beside it, the 1 m crate
                      behind; seen from a standing player
  _capacity_top       the same, straight down (orthographic): the fit inside the liner
  _near_closed / _near_open   from about 1.2 m (crouched at the shop counter height), closed and open
  _dock10m            10 m down the dock, both colour variants at the dock's end against the water (game pixel
                      density: 90 deg over 1920 px): LEFT = shipped "seaglass", RIGHT = "white" alternative
  _boat10m            the same pair from a boat 10 m off the dock's side, open water right behind them
  _dock10m_zoom       nearest-neighbour 4x crops of both 10 m frames (pixels as the game shows them)
  _fp_down30          the carry frame with the player looking 30 deg down (hands, grips and the whole lid)
Lighting: art/lib/fp_preview.py backdrops (EEVEE, palette sky and water, mood board A day / B dusk).
World: the eye is at the origin for the FP frame; the water plane sits 2.3 m below the origin; the dock deck top at
DECK_Z = -1.65 (a standing eye 1.65 m above the deck).
"""
import importlib.util
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "lib"))

import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import fp_preview as fpp  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

OUT = pb.PREVIEW_ROOT / "SM_Cooler_Scenes.png"
DECK_Z = -1.65
LID_OPEN = math.radians(100.0)


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


cooler = load("sm_cooler_starter")


def place_cooler(parts, matrix, open_lid=False):
    body, lid = parts["body"], parts["lid"]
    body.matrix_world = matrix
    lid.matrix_world = matrix @ Matrix.Translation(cooler.HINGE) @ Matrix.Rotation(
        -LID_OPEN if open_lid else 0.0, 4, "Y")
    for o in (parts["ucx_body"], parts["ucx_lid"]):
        o.hide_render = True
        o.hide_viewport = True


def xform(x, y, z, yaw_deg=0.0):
    return Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(yaw_deg), 4, "Z")


# ---------------------------------------------------------------------------------------------------
# Scene pieces
# ---------------------------------------------------------------------------------------------------
def build_dock(x0=-1.0, x1=10.9, half_w=1.0):
    """Plank deck (weathered wood), top at DECK_Z, with posts down into the water. One mesh."""
    bm = bmesh.new()
    x = x0
    while x < x1 - 0.05:
        w = min(0.18, x1 - x)
        bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation((x + w / 2, 0.0, DECK_Z - 0.02))
                              @ Matrix.Diagonal((w, 2 * half_w, 0.04, 1.0)))
        x += 0.20
    px = x0 + 1.0
    while px <= x1:
        for py in (-half_w + 0.08, half_w - 0.08):
            bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=0.11, radius2=0.11, depth=1.6,
                                  matrix=Matrix.Translation((min(px, x1 - 0.1), py, DECK_Z - 0.2 + 0.1)))
        px += 2.5
    me = bpy.data.meshes.new("PV_Dock")
    bm.to_mesh(me)
    bm.free()
    me.materials.append(style.make_material("PV_DockWood", style.TROPICAL.WEATHERED_WOOD, "wood"))
    obj = bpy.data.objects.new("PV_Dock", me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def build_crate(loc):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0))
    crate = bpy.context.active_object
    crate.name = "PV_Crate"
    crate.data.transform(Matrix.Translation((0.0, 0.0, 0.5)))
    bev = crate.modifiers.new(name="Bevel", type="BEVEL")
    bev.width, bev.segments = 0.03, 2
    crate.data.materials.append(style.make_material("PV_Crate", "#A26C45", "wood"))
    crate.location = loc
    return crate


def fish_objects():
    out = {}
    for stem in ("sm_fish_bonefish", "sm_fish_coralsnapper"):
        obj, info = load(stem).build()
        obj.hide_render = True
        out[obj.name] = obj
    return out


def lay_fish(src, parent_matrix, center_xy, z_bottom, yaw_deg, belly=1, tilt_deg=0.0):
    """A copy of fish `src` lying on its side (belly toward +/-Y before the yaw), yawed, nose tilted up by tilt_deg,
    its bounding box centered at center_xy and its lowest vertex at z_bottom, in `parent_matrix` space."""
    inst = bpy.data.objects.new("PV_" + src.name, src.data)
    bpy.context.scene.collection.objects.link(inst)
    rot = (Matrix.Rotation(math.radians(yaw_deg), 4, "Z") @ Matrix.Rotation(math.radians(-tilt_deg), 4, "Y")
           @ Matrix.Rotation(belly * math.pi / 2, 4, "X"))
    pts = [rot @ v.co for v in src.data.vertices]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    off = Vector((center_xy[0] - (mn.x + mx.x) / 2, center_xy[1] - (mn.y + mx.y) / 2, z_bottom - mn.z))
    inst.matrix_world = parent_matrix @ Matrix.Translation(off) @ rot
    return inst, (mn + off, mx + off)


def _fish_rot(yaw_deg, tilt_deg, belly):
    return (Matrix.Rotation(math.radians(yaw_deg), 4, "Z") @ Matrix.Rotation(math.radians(-tilt_deg), 4, "Y")
            @ Matrix.Rotation(belly * math.pi / 2, 4, "X"))


def pack_fish(fishes, parent_matrix, lid_clear=0.325):
    """Lay the fish into the liner one by one (fish at their ReferenceWeight size, lying on a side, rigid meshes).
    For each fish a deterministic grid search over height, heading (nose +Y or -Y, +/-40 deg), position and nose tilt
    (0-35 deg, tail down). A pose is VALID when every vertex is inside the liner (walls, floor, and under the closed
    lid at lid_clear). Among valid poses the one crossing the fewest triangles of the fish already placed wins
    (then: lowest, least tilted). If no pose is valid, the pose poking out least is used and reported.
    Returns the placements with the checks (for RESULT_JSON)."""
    import numpy as np
    from mathutils.bvhtree import BVHTree
    e = cooler.EXP
    out, trees = [], []
    for i, src in enumerate(fishes):
        base = np.array([v.co[:] for v in src.data.vertices])
        polys = [tuple(p.vertices) for p in src.data.polygons]
        belly = 1 if i % 2 == 0 else -1
        valid, least = [], None
        for head in (90.0, 270.0):
            for d in range(-40, 41, 5):
                for tilt in (0.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 35.0):
                    R = np.array(_fish_rot(head + d, tilt, belly).to_3x3())
                    P = base @ R.T
                    mn, mx = P.min(axis=0), P.max(axis=0)
                    for z0 in np.arange(cooler.FLOOR_Z, lid_clear - (mx[2] - mn[2]) + 1e-6, 0.02):
                        for cx in np.arange(-0.08, 0.0801, 0.02):
                            for cy in (-0.03, -0.015, 0.0, 0.015, 0.03):
                                off = np.array([cx - (mn[0] + mx[0]) / 2, cy - (mn[1] + mx[1]) / 2, z0 - mn[2]])
                                Q = P + off
                                hx, hy = _liner_half_np(Q[:, 2])
                                ex = (((np.abs(Q[:, 0]) / hx) ** e + (np.abs(Q[:, 1]) / hy) ** e).max() ** (1.0 / e)
                                     - 1.0) * float(hx.min())
                                c = (round(float(z0), 3), tilt, abs(cy), abs(d), head, d, float(cx), cy, off)
                                if ex <= 0.0:
                                    valid.append(c)
                                elif least is None or ex < least[0]:
                                    least = (ex, c)
        rec = {"fish": src.name, "valid_poses": len(valid)}
        best = None
        for c in sorted(valid, key=lambda c: c[:4])[:400]:
            M = Matrix.Translation(Vector(c[8].tolist())) @ _fish_rot(c[4] + c[5], c[1], belly)
            tree = BVHTree.FromPolygons([M @ v.co for v in src.data.vertices], polys)
            crossings = sum(len(tree.overlap(t)) for t in trees)
            if best is None or crossings < best[0]:
                best = (crossings, c, M, tree)
            if crossings == 0:
                break
        if best is None:
            c = least[1]
            M = Matrix.Translation(Vector(c[8].tolist())) @ _fish_rot(c[4] + c[5], c[1], belly)
            best = (None, c, M, BVHTree.FromPolygons([M @ v.co for v in src.data.vertices], polys))
            rec["pokes_out_mm"] = round(least[0] * 1000.0, 1)
        crossings, c, M, tree = best
        trees.append(tree)
        inst = bpy.data.objects.new("PV_" + src.name, src.data)
        bpy.context.scene.collection.objects.link(inst)
        inst.matrix_world = parent_matrix @ M
        pts = [M @ v.co for v in src.data.vertices]
        rec.update({"inside_liner": crossings is not None, "triangle_pairs_crossing_other_fish": crossings,
                    "yaw_deg": c[4] + c[5], "tilt_deg": c[1], "center_m": [round(c[6], 3), c[7]],
                    "z_bottom_m": c[0], "z_top_m": round(max(p.z for p in pts), 3)})
        out.append(rec)
    return out


def _liner_half_np(z):
    import numpy as np
    t = np.clip((z - cooler.FLOOR_Z) / (cooler.Z_TOP - cooler.FLOOR_Z), 0.0, 1.0)
    inset = cooler.LINER_FLOOR + (cooler.LINER_LIP - cooler.LINER_FLOOR) * t
    tz = np.clip(z / cooler.Z_TOP, 0.0, 1.0)
    hx = cooler.HX0 + (cooler.HX1 - cooler.HX0) * tz + inset
    hy = cooler.HY0 + (cooler.HY1 - cooler.HY0) * tz + inset
    return hx, hy


# ---------------------------------------------------------------------------------------------------
# Rendering (EEVEE over the fp_preview backdrops, free camera)
# ---------------------------------------------------------------------------------------------------
def render_eevee(out_path, location, target, hfov_deg=60.0, kind="day", resolution=(1280, 720), ortho=None,
                 samples=32):
    scene = bpy.context.scene
    out_path = Path(out_path)
    snap = fpp._render_settings_snapshot(scene)
    data = bpy.data.cameras.new("PV_Cam")
    data.sensor_fit = "HORIZONTAL"
    data.sensor_width = 36.0
    data.clip_start = 0.01
    data.clip_end = 5000.0
    if ortho:
        data.type = "ORTHO"
        data.ortho_scale = ortho
    else:
        data.lens = pb.lens_for_hfov(hfov_deg)
    cam = bpy.data.objects.new("PV_Cam", data)
    scene.collection.objects.link(cam)
    cam.location = Vector(location)
    d = Vector(target) - Vector(location)
    cam.rotation_euler = d.to_track_quat("-Z", "Z" if abs(d.normalized().z) > 0.999 else "Y").to_euler()
    if abs(d.normalized().z) > 0.999:
        cam.rotation_euler = (0.0, 0.0, math.pi / 2)   # straight down, world +X at the bottom of the frame
    scene.camera = cam
    bd_cleanup = fpp.stage_backdrop(kind)
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
    fpp._set_view(scene, *fpp._backdrop_spec(kind)["view"])
    try:
        bpy.ops.render.render(write_still=True)
    finally:
        bd_cleanup()
        bpy.data.objects.remove(cam, do_unlink=True)
        bpy.data.cameras.remove(data)
        fpp._restore_render_settings(scene, snap)
    return str(out_path)


def frame(name):
    return OUT.with_name(OUT.stem + "_" + name + OUT.suffix)


# ---------------------------------------------------------------------------------------------------
# Stages
# ---------------------------------------------------------------------------------------------------
def stage_fp():
    """SK_FPArms in CarryCooler_Idle frame 0 holding the cooler, from the FP camera. Resets the scene."""
    info = {}
    try:
        anim = load("anim_fp_arms")
        ctx = anim.setup()                                    # resets the scene, builds arms + rig
        P, _m = ctx["poser"].carry(0)
        anim.apply_basis(ctx["arm_obj"], anim.pose_to_basis(ctx["B"], P))
        M = P["cooler"].copy()
        info["source"] = "anim_fp_arms.py: A_FPArms_CarryCooler_Idle frame 0, arms shown"
    except Exception as exc:
        pb.reset_scene()
        M = Matrix.Translation((0.40, 0.0, -0.62))
        info["source"] = "fallback: cooler alone at COOLER_POS (%s: %s)" % (type(exc).__name__, exc)
    parts = cooler.build()
    place_cooler(parts, M)
    bpy.context.view_layer.update()
    paths = [fpp.render_fp(frame("fp_" + k), k) for k in ("day", "dusk")]
    # the player glancing down 30 deg at what they carry (same eye, same 90 deg lens)
    down = math.radians(30.0)
    paths.append(render_eevee(frame("fp_down30"), (0.0, 0.0, 0.0), (math.cos(down), 0.0, -math.sin(down)),
                              hfov_deg=90.0, resolution=(1920, 1080)))
    # where the grips are on screen (for the report)
    info["handles_px"] = {n: [round(c) for c in fpp.project(M @ p)[:2]] for n, p in parts["handles"].items()}
    return paths, info


def stage_world():
    """Dock, both colour variants, fish. Returns the list of frames."""
    pb.reset_scene()
    bpy.context.scene.display.shading.show_specular_highlight = False
    build_dock()
    fish = fish_objects()
    bone, snap = fish["SM_Bonefish"], fish["SM_CoralSnapper"]
    paths = []
    info = {}

    # --- capacity: open cooler 1.3 m ahead, its front toward the player; 4 fish in 2 layers -------------------
    A = cooler.build()
    Mc = xform(1.35, 0.0, DECK_Z, 180.0)
    place_cooler(A, Mc, open_lid=True)
    # cooler space: floor at z 0.05, liner ~0.343 x 0.503 at the floor, 0.37 x 0.53 at the lip, rim at 0.33
    placed = pack_fish([bone, snap, snap, bone], Mc)
    info["fish_in_cooler"] = placed
    # beside it on the deck: a bonefish and a coral snapper, and the 1 m crate behind
    lay_fish(bone, xform(1.35, 0.0, DECK_Z), (0.05, 0.62), 0.0, 8.0, 1)
    lay_fish(snap, xform(1.35, 0.0, DECK_Z), (0.05, -0.64), 0.0, -6.0, 1)
    build_crate((2.35, 0.55, DECK_Z))
    paths.append(render_eevee(frame("capacity"), (0.0, 0.25, 0.0), (1.4, 0.0, DECK_Z + 0.15), hfov_deg=60.0))
    paths.append(render_eevee(frame("capacity_top"), (1.35, 0.0, DECK_Z + 3.0), (1.35, 0.0, DECK_Z),
                              ortho=1.6, resolution=(1280, 720)))
    for o in list(bpy.data.objects):
        if o.name.startswith("PV_SM_") or o.name == "PV_Crate":
            bpy.data.objects.remove(o, do_unlink=True)

    # --- 1.2 m: crouched player (eye 0.95 m over the deck) 3/4 onto the cooler, closed and open ---------------
    Mn = xform(1.35, 0.0, DECK_Z, 205.0)
    for state, is_open in (("near_closed", False), ("near_open", True)):
        place_cooler(A, Mn, open_lid=is_open)
        paths.append(render_eevee(frame(state), (0.45, 0.35, DECK_Z + 0.95), (1.35, 0.0, DECK_Z + 0.2),
                                  hfov_deg=60.0))

    # --- 10 m down the dock: shipped (left) vs alternative (right) at the dock's end, water behind ----------------
    place_cooler(A, xform(10.35, 0.45, DECK_Z, 160.0))
    B = cooler.build("white", prefix="ALT_")
    place_cooler(B, xform(10.35, -0.45, DECK_Z, 200.0))
    eye = (0.0, 0.0, DECK_Z + 1.65)
    fr = render_eevee(frame("dock10m"), eye, (10.35, 0.0, DECK_Z + 0.4), hfov_deg=90.0, resolution=(1920, 1080))
    paths.append(fr)
    # zoom crop centered between the two coolers
    # the same pair from a boat 10 m off the dock's side (eye 1.4 m over the water): open water right behind them
    place_cooler(A, xform(9.72, 0.30, DECK_Z, 165.0))    # side by side along the dock for this one: LEFT seaglass
    place_cooler(B, xform(10.55, 0.30, DECK_Z, 195.0))
    fr2 = render_eevee(frame("boat10m"), (10.4, -9.8, -2.3 + 1.4), (10.13, 0.0, DECK_Z + 0.2), hfov_deg=90.0,
                       resolution=(1920, 1080))
    paths.append(fr2)
    zoom = fpp.zoom_sheet(frame("dock10m_zoom"), [[(fr, (960, 540)), (fr2, (960, 540))]], crop=200, scale=4)
    paths.append(zoom)
    return paths, info


def main():
    fp_paths, fp_info = stage_fp()
    world_paths, world_info = stage_world()
    paths = fp_paths + world_paths
    pb.contact_sheet(paths, OUT, cols=2, cell=(960, 540))
    print("RESULT_JSON:" + json.dumps({"asset": "preview_cooler_starter", "preview": str(OUT), "views": paths,
                                       "fp": fp_info, "world": world_info}))


if __name__ == "__main__":
    main()

"""anim_fish: the shared fish skeleton SKEL_Fish + the fish clips (T-008), built on the model-artist's mesh recipes
(sm_fish_bonefish.py, sm_fish_coralsnapper.py). The rig and every motion live in art/lib/fishrig.py; this recipe builds,
checks, exports and previews them.

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/anim_fish.py
Fast iteration (checks only: no exports, no previews): set $env:FISH_ANIM_QUICK='1' before the run; optionally
$env:FISH_ANIM_CLIPS='Fight_Dart,Landed_Flop' limits the checks and clip previews to those clips. Unset both for a
real run (the exports always contain every clip).

Exports (art/export/Fish/), all CENTIMETERS through pb.export_skeletal_fbx (UnitScaleFactor 1.0, no scale on any node):
  SK_Bonefish.fbx, SK_CoralSnapper.fbx  skinned mesh + the 11-bone SKEL_Fish skeleton in its bind pose, no animation
  A_Fish_Rest.fbx                        the straight fish (2 identical keys, frames 0-1): the ADDITIVE BASE POSE
  A_Fish_Swim_Idle, _Swim_Fast, _Hooked_Thrash, _Fight_Run, _Fight_Dive, _Fight_Dart, _Landed_Flop .fbx
                                         armature only, one take each, all loops, rotation-only, keyed on the Bonefish
                                         armature (the reference); imported in Unreal as Local Space additives on
                                         A_Fish_Rest frame 0, so every species keeps its own proportions
  A_Fish_Curled.fbx                      the same kind of file, a 1-frame pose (2 identical keys, frames 0-1): the
                                         iced catch lying curled in a cooler (T-030; layout proof and slot table:
                                         art/recipes/anim_fish_cooler.py)
  A_Fish_Swim_Tired, _Hooked_Hang .fbx   S3 (T-058 / T-060): the exhausted fish, upright; the landed fish dangling on
                                         the hook with its head locked (the Mouth never moves; never held at Grip)
Spec for Unreal: art/export/Fish/SK_Fish.anim.md. RESULT_JSON: Saved/AgentLogs/blender/anim_fish.result.json.
The clips seen from the player's camera (dock at 5 / 12 m, first person on the hook): art/recipes/anim_fish_views.py.
Previews (Saved/AgentLogs/previews/):
  SK_Fish_anim.png           overview: both species x every clip, 3/4 view from the concave side at each clip's
                             tightest bend (also the pinch check)
  A_Fish_<Clip>_anim.png     per clip: 8-11 key frames x (Bonefish top, side; CoralSnapper top, side at amplitude
                             0.8); A_Fish_Hooked_Hang adds the hanging views (nose up: its right side, its back)
  SK_Fish_seams.png          every loop across its seam: frames N-2, N-1, N (= 0), 1, 2 (Bonefish, top view)
  A_Fish_Curled_anim.png     the 1-frame pose, both species: lying on each side seen from above (the cooler view,
                             over the pale straight fish), from the back (it stays flat) and 3/4
  SK_Fish_strobe.png         midline strobes (top view) per clip + the ambient WPO wave for comparison
  SK_Fish_flop_dock.png      Landed_Flop lying on its right side on dock planks (low side view + top view)
  SK_Fish_tuck.png           pectorals as modeled vs tucked (front and bottom views)
  SK_Fish_weights.png        skin weights (color by bone) on both species
  SK_Fish_pec.png            pectoral close-ups (both sides) at rest and at the worst root-strip-strain frames

Nothing here edits the meshes: build() gives the mesh with its fishkit part groups; fishrig adds the armature and the
bone weights and renames the part groups Part_<name> (two of them clash with bone names).
Axes: Blender +X = nose (Unreal +X), +Y = the fish's LEFT (Unreal -Y), +Z up; origin = body center on the spine line.
"""
import importlib.util
import math
import os
import statistics
import sys
import time
from contextlib import contextmanager
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Euler, Matrix, Quaternion, Vector  # noqa: E402

import fishkit as fk  # noqa: E402
import fishrig as fr  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
RECIPES = REPO / "art" / "recipes"
CATEGORY = "Fish"
EXPORT_DIR = REPO / "art" / "export" / CATEGORY
PREVIEW_DIR = pb.PREVIEW_ROOT
# (mesh recipe, skeletal asset, species row name). Adding a species = its mesh recipe (fishkit) + one line here.
SPECIES = [("sm_fish_bonefish", "SK_Bonefish", "Bonefish"),
           ("sm_fish_coralsnapper", "SK_CoralSnapper", "CoralSnapper")]
REFERENCE = "Bonefish"            # the clips are keyed on this species' armature; its bind pose = A_Fish_Rest
PREVIEW_AMPLITUDE = {"Bonefish": 1.0, "CoralSnapper": 0.8}   # proposed DT_FishSpecies AnimAmplitude (previews only)
QUICK = os.environ.get("FISH_ANIM_QUICK", "") == "1"
ONLY = {s.strip() for s in os.environ.get("FISH_ANIM_CLIPS", "").split(",") if s.strip()}
# Acceptance numbers for the checks (RESULT_JSON "verdict")
LIMITS = {"ring_area_min": 0.80, "fold_min": 0.30, "pec_in_mm": 1.0, "flop_drop_mm": 1.0, "weight_sum_error": 1e-4,
          "max_influences": 3, "rest_skin_mm": 0.01, "keyed_error_mm": 0.01, "reimport_mm": 0.05,
          "additive_mm": 0.05, "seam_mm": 0.001, "hang_mouth_mm": 0.01, "anchor_mm": 0.01, "anchor_deg": 0.01}
T0 = time.time()


def log(msg):
    print("[anim_fish %6.1fs] %s" % (time.time() - T0, msg), flush=True)


def load_recipe(stem):
    spec = importlib.util.spec_from_file_location(stem, str(RECIPES / (stem + ".py")))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def short(clip_name):
    return clip_name.replace("A_Fish_", "")


def ue_cm(v):
    """Blender meters (+Y = the fish's left) -> Unreal component cm (+Y = the fish's right)."""
    return [round(v[0] * 100.0, 2), round(-v[1] * 100.0, 2), round(v[2] * 100.0, 2)]


# ---------------------------------------------------------------------------------------------------------------
# Species
# ---------------------------------------------------------------------------------------------------------------
class Fish:
    """One species: the model-artist's mesh (build()), renamed SK_*, rigged and skinned on SKEL_Fish."""

    def __init__(self, stem, asset, species):
        mod = load_recipe(stem)
        obj, info = mod.build()
        self.static_name = obj.name
        obj.name = asset
        obj.data.name = asset
        self.mesh, self.info, self.asset, self.species = obj, info, asset, species
        self.arm, self.geo, self.weights = fr.rig_species(obj, info, species, "Armature_" + species)
        for c in list(obj.children):          # SOCKET_Mouth became the Mouth bone (skeletal meshes use bones)
            if c.name.startswith(pb.SOCKET_PREFIX):
                bpy.data.objects.remove(c, do_unlink=True)
        self.rest_co = [v.co.copy() for v in obj.data.vertices]
        self.probe = fr.DeformProbe(obj)
        me = obj.data
        body = fr.group_members(obj, fr.part("Body"))
        self.nose_vert = max(body, key=lambda i: me.vertices[i].co.x)
        self.part_of = {}
        for g in obj.vertex_groups:
            if g.name.startswith(fr.PART_PREFIX):
                for i in fr.group_members(obj, g.name):
                    self.part_of[i] = g.name[len(fr.PART_PREFIX):]


def pose_fish(fish, quats):
    fr.apply_quats(fish.arm, quats)
    bpy.context.view_layer.update()


def clip_quats(clip, f, amplitude=1.0):
    return fr.personality(clip.pose(f).quats(), amplitude)


def max_dist_mm(a, b):
    return max((p - q).length for p, q in zip(a, b)) * 1000.0


def skin_matrix(fish, bone):
    """World transform that carries a point rigidly attached to `bone` from the rest pose to the current pose."""
    return fish.arm.matrix_world @ fish.arm.pose.bones[bone].matrix @ fish.arm.data.bones[bone].matrix_local.inverted()


# ---------------------------------------------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------------------------------------------
def rest_check(fish):
    """Skinned mesh at the rest pose vs the static mesh (mm): the skin must not move anything at rest."""
    pose_fish(fish, fr.Pose().quats())
    return round(max_dist_mm(fish.probe.coords(), fish.rest_co), 5)


def suggest_tuck(scans, limit_mm):
    """Most tucked angle (scanning from 0 down) at which every species' pectoral blade stays within limit_mm of the
    flank surface; stops at the first violation."""
    angles = [a for a, _p, _t in scans[0]]
    best = 0
    for k, a in enumerate(angles):
        if any(s[k][1] > limit_mm for s in scans):
            break
        best = a
    return best


def lie_pose():
    """The dock lie: straight fish, both pectorals tucked (Landed_Flop at rest)."""
    return fr.Pose().pecs(fr.TUCK_DEG, fr.TUCK_DEG)


def lie_min_y(fish):
    pose_fish(fish, lie_pose().quats())
    co = fish.probe.coords()
    i = min(range(len(co)), key=lambda k: co[k].y)
    return co[i].y, fish.part_of.get(i, "?")


def clip_metrics(fish, clip, amplitude=1.0, lie_y=None):
    """Every frame of `clip` on `fish`: deformation (ring area, folds, pectoral clearance), motion (largest vertex step
    per frame, largest second difference = a pop, the loop seam) and, for a lie reference, the drop below it."""
    n = clip.frames
    cos, rows = [], []
    for f in range(0, n + 1):
        pose_fish(fish, clip_quats(clip, f, amplitude))
        co = fish.probe.coords()
        cos.append(co)
        rows.append(fish.probe.measure(co))
    steps = [max_dist_mm(cos[i + 1], cos[i]) for i in range(n)]
    acc = []
    for i in range(n):                       # wrap: pose(n) == pose(0), so the frame before 0 is n - 1
        prev = cos[i - 1] if i > 0 else cos[n - 1]
        acc.append(max((a - 2.0 * b + c).length for a, b, c in zip(cos[i + 1], cos[i], prev)) * 1000.0)

    def arg(key, fn):
        k = fn(range(len(rows)), key=lambda j: rows[j][key])
        return k, rows[k][key]
    fa, area = arg("ring_area_min", min)
    ff, fold = arg("fold_min", min)
    fp, pen = arg("pec_in_mm", max)
    out = {
        "frames": n, "amplitude": amplitude,
        "ring_area_min": round(area, 3), "ring_area_frame": fa,
        "fold_min": round(fold, 3), "fold_frame": ff,
        "pec_in_mm": round(pen, 2), "pec_in_frame": fp,
        "pec_strip_strain_max": round(max(r["pec_strip_strain"] for r in rows), 3),
        "pec_strip_strain_frame": max(range(len(rows)), key=lambda j: rows[j]["pec_strip_strain"]),
        "pec_tip_mm_range": [round(min(r["pec_tip_mm"] for r in rows), 1),
                             round(max(r["pec_tip_mm"] for r in rows), 1)],
        "max_step_mm": round(max(steps), 2), "median_step_mm": round(statistics.median(steps), 2),
        "max_accel_mm": round(max(acc), 2), "max_accel_frame": max(range(n), key=lambda i: acc[i]),
        "median_accel_mm": round(statistics.median(acc), 2), "seam_accel_mm": round(acc[0], 2),
        "seam_mm": round(max_dist_mm(cos[n], cos[0]), 4),
    }
    if lie_y is not None:
        drops = [(lie_y - r["min_y"]) * 1000.0 for r in rows]
        k = max(range(len(drops)), key=lambda j: drops[j])
        out["drop_below_lie_mm"] = round(drops[k], 2)
        out["drop_frame"] = k
    return out, ff


def keyed_check(fish, clips):
    """The keyed actions (NLA, as exported) vs the pose functions, at a few frames per clip: max bone head error (mm)
    and rotation error (deg)."""
    arm = fish.arm
    err_mm, err_deg = 0.0, 0.0
    for clip in clips:
        n = clip.frames
        for f in sorted({0, 1, n // 3, n // 2, max(0, n - 1), n}):
            fr.solo(arm, clip.name, f)
            got = {p.name: p.matrix.copy() for p in arm.pose.bones}
            fr.mute_all(arm)
            pose_fish(fish, clip.pose(f).quats())
            for p in arm.pose.bones:
                a, b = got[p.name], p.matrix
                err_mm = max(err_mm, (a.translation - b.translation).length * 1000.0)
                err_deg = max(err_deg, math.degrees(a.to_quaternion().rotation_difference(b.to_quaternion()).angle))
    fr.mute_all(arm)
    return {"max_bone_error_mm": round(err_mm, 5), "max_bone_error_deg": round(err_deg, 5)}


def nlerp_alpha(quats, a):
    """Unreal's Apply Additive alpha (FTransform::BlendFromIdentityAndAccumulate): every local rotation lerped from the
    identity along the shorter arc, then normalized (personality() slerps for the previews; < 0.2 deg apart)."""
    if a >= 0.9999:
        return quats
    out = {}
    for n, q in quats.items():
        s = 1.0 if q.w >= 0.0 else -1.0
        r = Quaternion(((1.0 - a) + a * s * q.w, a * s * q.x, a * s * q.y, a * s * q.z))
        r.normalize()
        out[n] = r
    return out


def hang_check(fishes, clip):
    """A_Fish_Hooked_Hang: the Mouth (the hook) in component space on every frame at Apply Additive alpha 1.0 and 0.8
    (Unreal's nlerp): largest distance from its reference position, mm. The head is the chest's inverse and a rotation
    and its inverse scale to inverses, so it must stay 0 at any alpha."""
    out = {}
    for fish in fishes:
        rest = fish.arm.data.bones["Mouth"].head_local.copy()
        worst = {}
        for a in (1.0, 0.8):
            d = 0.0
            for f in range(clip.frames + 1):
                pose_fish(fish, nlerp_alpha(clip.pose(f).quats(), a))
                d = max(d, (fish.arm.pose.bones["Mouth"].head - rest).length * 1000.0)
            worst["alpha_%.1f" % a] = round(d, 5)
        out[fish.species] = worst
        pose_fish(fish, fr.Pose().quats())
    return out


def anchor_check(fishes, clips):
    """The anchored chest (every clip that can be held at Grip, i.e. all but the Hang): Grip's component position (mm)
    and the chest's spine axis (Spine_01 X, deg: roll keeps it, yaw or pitch would turn it) on every frame."""
    out = {}
    for fish in fishes:
        rest = fish.arm.data.bones["Grip"].head_local.copy()
        for clip in clips:
            if clip.role == "Hang":
                continue
            d, ang = 0.0, 0.0
            for f in range(clip.frames + 1):
                pose_fish(fish, clip.pose(f).quats())
                d = max(d, (fish.arm.pose.bones["Grip"].head - rest).length * 1000.0)
                ax = fish.arm.pose.bones["Spine_01"].matrix.to_3x3() @ Vector((1.0, 0.0, 0.0))
                ang = max(ang, math.degrees(ax.angle(Vector((1.0, 0.0, 0.0)))))
            out.setdefault(fish.species, {})[clip.name] = {"grip_mm": round(d, 5), "chest_axis_deg": round(ang, 5)}
        pose_fish(fish, fr.Pose().quats())
    return out


def tired_upright(clip):
    """A_Fish_Swim_Tired must read upright (T-058): the largest body roll (Spine_01, deg) over the clip."""
    return round(max(abs(math.degrees(clip.pose(f).roll)) for f in range(clip.frames + 1)), 3)


def body_centre(fish, co):
    """Volume centroid of the body (armature space): slices between neighbouring body rings, each with its mean area
    and its centroid at the mid-point of the two ring centroids (fins have next to no volume)."""
    cents = [sum((co[i] for i in r), Vector()) / len(r) for r in fish.probe.rings]
    areas = [fish.probe._area([co[i] for i in r]) for r in fish.probe.rings]
    num, den = Vector(), 0.0
    for c0, c1, a0, a1 in zip(cents, cents[1:], areas, areas[1:]):
        v = 0.5 * (a0 + a1) * (c1 - c0).length
        num += v * 0.5 * (c0 + c1)
        den += v
    return num / den


def hang_offsets(fishes, clip):
    """T-061 (the wiggle moves the line): while the head hangs still, where the body goes. Component space, Unreal cm
    and axes (+X = the nose = up the line, +Y = the fish's right = toward the viewer, +Z = its back), reference size,
    alpha 1, as offsets from the reference pose, every frame: the body centre (volume centroid), the Tail bone (J5,
    the tail root) and the tail tip (on the spine line). Scale by the fish's actor scale."""
    out = {"frames": clip.frames, "columns": ["frame", "centre_dx", "centre_dy", "centre_dz", "tail_dx", "tail_dy",
                                              "tail_dz", "tip_dx", "tip_dy", "tip_dz"]}
    for fish in fishes:
        def points():
            co = fish.probe.coords()
            return [body_centre(fish, co), fish.arm.pose.bones["Tail"].head.copy(),
                    skin_matrix(fish, "Tail") @ Vector((fish.geo.tip_x, 0.0, 0.0))]
        pose_fish(fish, fr.Pose().quats())
        rest = points()
        rows, peak = [], [0.0, 0.0, 0.0]
        for f in range(clip.frames + 1):
            pose_fish(fish, clip.pose(f).quats())
            row = [f]
            for k, (p, p0) in enumerate(zip(points(), rest)):
                d = ue_cm(p - p0)
                row += d
                peak[k] = max(peak[k], math.sqrt(sum(c * c for c in d)))
            rows.append(row)
        out[fish.species] = {"rest_ue_cm": {"centre": ue_cm(rest[0]), "tail": ue_cm(rest[1]), "tip": ue_cm(rest[2])},
                             "peak_offset_cm": {"centre": round(peak[0], 2), "tail": round(peak[1], 2),
                                                "tip": round(peak[2], 2)},
                             "offsets_ue_cm": rows}
        pose_fish(fish, fr.Pose().quats())
    return out


# ---------------------------------------------------------------------------------------------------------------
# Export + re-import checks
# ---------------------------------------------------------------------------------------------------------------
@contextmanager
def renamed(idb, name):
    old = idb.name
    idb.name = name
    if idb.name != name:
        idb.name = old
        raise RuntimeError("cannot rename %s to %s: the name is taken" % (old, name))
    try:
        yield
    finally:
        idb.name = old


def export_all(fishes, ref):
    """SK_<Species>.fbx per species (mesh + rig, bind pose) and one A_Fish_*.fbx per clip from the reference armature.
    Each armature is exported under the object name 'Armature', so Unreal's FBX importer drops that node and `root`
    becomes the skeleton root."""
    out, units = {}, {}
    scene = bpy.context.scene
    for fish in fishes:
        fr.mute_all(fish.arm)
        fr.rest(fish.arm)
        scene.frame_set(0)
        path = EXPORT_DIR / (fish.asset + ".fbx")
        with renamed(fish.arm, "Armature"):
            units[fish.asset] = pb.export_skeletal_fbx(path, fish.arm, [fish.mesh], bake_anim=False)
        out[fish.asset] = str(path)
    ad = ref.arm.animation_data
    for clip in [fr.REST_CLIP] + fr.CLIPS:
        for tr in ad.nla_tracks:
            tr.mute = tr.name != clip.name
        path = EXPORT_DIR / (clip.name + ".fbx")
        with renamed(ref.arm, "Armature"):
            units[clip.name] = pb.export_skeletal_fbx(path, ref.arm, [], bake_anim=True)
        out[clip.name] = str(path)
    fr.mute_all(ref.arm)
    fr.rest(ref.arm)
    scene.frame_set(0)
    return out, units


def snapshot():
    return {k: set(getattr(bpy.data, k)) for k in ("objects", "meshes", "armatures", "actions", "materials")}


def cleanup_since(snap):
    for o in set(bpy.data.objects) - snap["objects"]:
        bpy.data.objects.remove(o, do_unlink=True)
    for k in ("meshes", "armatures", "actions", "materials"):
        coll = getattr(bpy.data, k)
        for block in set(coll) - snap[k]:
            coll.remove(block)


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), anim_offset=0.0)
    new = [o for o in bpy.data.objects if o not in before]
    return (next((o for o in new if o.type == "ARMATURE"), None), next((o for o in new if o.type == "MESH"), None))


def scale_dev(M):
    return max(abs(c - 1.0) for c in M.to_scale())


def world_coords(obj):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    mw = obj.matrix_world
    co = [mw @ v.co for v in me.vertices]
    ev.to_mesh_clear()
    return co


def pose_locals(arm):
    """Parent-relative pose transforms (what FBX / Unreal store per bone), from the armature-space pose matrices."""
    P = {p.name: p.matrix.copy() for p in arm.pose.bones}
    return {p.name: (P[p.parent.name].inverted() @ P[p.name]) if p.parent else P[p.name] for p in arm.pose.bones}


def rest_locals(arm):
    return {b.name: (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local.copy()
            for b in arm.data.bones}


def apply_additive(base, rest, clip):
    """Unreal's Local Space additive, per bone: delta = clip * rest^-1 (rotation), clip - rest (translation), applied
    to the playing mesh's reference pose `base` (rotation delta * base, base + translation delta)."""
    out = {}
    for n, B in base.items():
        qd = clip[n].to_quaternion() @ rest[n].to_quaternion().inverted()
        t = B.translation + (clip[n].translation - rest[n].translation)
        out[n] = Matrix.Translation(t) @ (qd @ B.to_quaternion()).to_matrix().to_4x4()
    return out


def set_pose_locals(arm, L):
    R = rest_locals(arm)
    for p in arm.pose.bones:
        p.rotation_mode = "QUATERNION"
        p.matrix_basis = R[p.name].inverted() @ L[p.name]
    bpy.context.view_layer.update()


def reimport_check(exports, fishes, ref, clips):
    """Re-import every FBX (Blender's importer, scene in meters) and compare with the source:
    - SK files: bone heads/axes, rest bone scales, mesh bounds, the skinned rest mesh vertex by vertex;
    - clip files: take range, posed bone scales, the baked pose vs the pose function at sample frames;
    - ADDITIVE EMULATION (the whole Unreal chain): the imported A_Fish_Rest and clip give per-bone local deltas that are
      applied to each IMPORTED species' bind pose (apply_additive); the imported mesh, skinned by its imported weights,
      must match the source species posed directly (vertex by vertex, mm). For the CoralSnapper this proves the clips
      keyed on the Bonefish keep the snapper's own proportions."""
    scene = bpy.context.scene
    fps = scene.render.fps
    k = pb.SKELETAL_FBX_CM_PER_UNIT
    res = {"SK": {}, "clips": {}, "additive": {}}
    snap_all = snapshot()
    imported = {}
    for fish in fishes:
        fr.mute_all(fish.arm)
        pose_fish(fish, fr.Pose().quats())
        arm, mesh = import_fbx(exports[fish.asset])
        imported[fish.species] = (arm, mesh)
        mw = arm.matrix_world
        src = fish.arm.data.bones
        head_err = max((mw @ arm.data.bones[b.name].head_local - b.head_local).length for b in src)
        head_cm_err = max((arm.data.bones[b.name].head_local - b.head_local * k).length for b in src)
        rot_err = max(math.degrees((mw.to_3x3().normalized() @ arm.data.bones[b.name].matrix_local.to_3x3())
                                   .to_quaternion().rotation_difference(b.matrix_local.to_quaternion()).angle)
                      for b in src)
        co_i, co_s = world_coords(mesh), fish.probe.coords()
        res["SK"][fish.asset] = {
            "bones": len(arm.data.bones), "armature_object_scale": [round(c, 6) for c in mw.to_scale()],
            "max_bone_head_error_mm": round(head_err * 1000.0, 4),
            "bone_heads_cm_max_error_mm": round(head_cm_err * 10, 4),
            "max_bone_axis_error_deg": round(rot_err, 4),
            "rest_bone_scale_dev": max(scale_dev(b.matrix_local) for b in arm.data.bones),
            "verts": [len(co_i), len(co_s)],
            "rest_mesh_max_error_mm": round(max_dist_mm(co_i, co_s), 4) if len(co_i) == len(co_s) else None,
            "skin_groups": sorted(g.name for g in mesh.vertex_groups),
        }
    rest_arm, _m = import_fbx(exports[fr.REST_CLIP.name])
    scene.frame_set(0)
    L_rest = pose_locals(rest_arm)
    res["clips"][fr.REST_CLIP.name] = {"frame_range": [round(c, 2) for c in rest_arm.animation_data.action.frame_range]}
    for clip in clips:
        snap = snapshot()
        arm, _m = import_fbx(exports[clip.name])
        act = arm.animation_data.action if arm.animation_data else None
        entry = {"action": act.name if act else None,
                 "frame_range": [round(c, 2) for c in act.frame_range] if act else None,
                 "armature_object_scale": [round(c, 6) for c in arm.matrix_world.to_scale()]}
        n = clip.frames
        frames = sorted({0, n // 4, n // 2, (3 * n) // 4, n - 1, n})
        sdev, err, rot = 0.0, 0.0, 0.0
        add_err = {f.species: 0.0 for f in fishes}
        for f in frames:
            scene.frame_set(f)
            sdev = max([sdev] + [scale_dev(p.matrix) for p in arm.pose.bones])
            pose_fish(ref, clip.pose(f).quats())
            for p in arm.pose.bones:
                M = arm.matrix_world @ p.matrix
                S = ref.arm.pose.bones[p.name].matrix
                err = max(err, (M.translation - S.translation).length)
                rot = max(rot, math.degrees(M.to_quaternion().rotation_difference(S.to_quaternion()).angle))
            L_clip = pose_locals(arm)
            for fish in fishes:
                sk_arm, sk_mesh = imported[fish.species]
                set_pose_locals(sk_arm, apply_additive(rest_locals(sk_arm), L_rest, L_clip))
                pose_fish(fish, clip.pose(f).quats())
                add_err[fish.species] = max(add_err[fish.species],
                                            max_dist_mm(world_coords(sk_mesh), fish.probe.coords()))
        entry.update({"posed_bone_scale_dev": sdev, "max_pose_error_mm": round(err * 1000.0, 4),
                      "max_pose_error_deg": round(rot, 4), "frames_checked": frames})
        res["clips"][clip.name] = entry
        res["additive"][clip.name] = {s: round(v, 4) for s, v in add_err.items()}
        cleanup_since(snap)
    cleanup_since(snap_all)
    scene.render.fps = fps
    for fish in fishes:
        fr.mute_all(fish.arm)
        pose_fish(fish, fr.Pose().quats())
    return res


# ---------------------------------------------------------------------------------------------------------------
# Previews
# ---------------------------------------------------------------------------------------------------------------
CELL = (240, 180)
BG = "#0E6F7A"          # palette deep water: the silver bonefish and the coral snapper both read on it
BG_LIGHT = style.UI.PARCHMENT
ORTHO = 0.80
STRIP_FRAMES = {        # the key poses of each choreographed clip (the others: 8 even frames)
    "A_Fish_Swim_Fast": [0, 3, 5, 8, 13, 18, 23, 28],
    "A_Fish_Fight_Run": [0, 3, 6, 10, 18, 25, 27, 29, 37, 40],
    "A_Fish_Fight_Dive": [0, 2, 7, 12, 15, 22, 34, 41, 47, 52],
    "A_Fish_Fight_Dart": [0, 2, 4, 6, 8, 11, 22, 26],
    "A_Fish_Hooked_Thrash": [0, 4, 7, 10, 13, 20, 22, 25, 29, 65, 67, 70],
    "A_Fish_Landed_Flop": [0, 7, 10, 13, 16, 27, 50, 58],
    "A_Fish_Swim_Tired": [0, 14, 26, 36, 46, 56, 66, 72, 76, 84],
    "A_Fish_Hooked_Hang": [0, 9, 14, 16, 19, 24, 29, 44, 60, 80, 82, 85, 109],
}
STROBE_FRAMES = {       # one phrase per clip, bonefish at amplitude 1
    "A_Fish_Swim_Idle": list(range(0, 30, 3)),
    "A_Fish_Swim_Fast": list(range(0, 12, 1)),
    "A_Fish_Fight_Run": list(range(0, 10, 1)),
    "A_Fish_Fight_Dive": list(range(0, 15, 1)),
    "A_Fish_Fight_Dart": list(range(0, 19, 2)),
    "A_Fish_Hooked_Thrash": list(range(16, 41, 3)),
    "A_Fish_Landed_Flop": list(range(0, 21, 2)),
    "A_Fish_Swim_Tired": list(range(20, 64, 4)),
    "A_Fish_Hooked_Hang": list(range(8, 37, 2)),
}


def strip_frames(clip):
    return STRIP_FRAMES.get(clip.name) or [int(i * clip.frames / 8.0 + 0.5) for i in range(8)]


def flat_mat(color):
    """Viewport-color material for Workbench MATERIAL shading. color: '#RRGGBB' (sRGB) or linear (r, g, b, a)."""
    rgba = style.hex_to_linear_rgba(color) if isinstance(color, str) else tuple(color)
    name = "PV_C_%.3f_%.3f_%.3f" % tuple(rgba[:3])
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.diffuse_color = rgba
    return mat


def add_box(name, center, size, hex_color):
    me = bpy.data.meshes.new(name)
    hx, hy, hz = (s / 2.0 for s in size)
    v = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
         (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    me.from_pydata(v, [], f)
    me.materials.append(flat_mat(hex_color))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = center
    return ob


def remove_objects(objs):
    for o in objs:
        data = o.data
        bpy.data.objects.remove(o, do_unlink=True)
        if isinstance(data, bpy.types.Mesh):
            bpy.data.meshes.remove(data)
        elif isinstance(data, bpy.types.Curve):
            bpy.data.curves.remove(data)


def add_polyline(name, pts, color, radius=0.0025):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = 1
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for p, q in zip(sp.points, pts):
        p.co = (q[0], q[1], q[2], 1.0)
    cu.materials.append(flat_mat(color))
    ob = bpy.data.objects.new(name, cu)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def add_label(text, cam, res, rel_size=0.085):
    """Parchment text on an ink plate in the top-left corner of the camera view. Returns the objects to remove."""
    Mw = cam.matrix_world
    right, up, fwd = Mw.col[0].xyz.normalized(), Mw.col[1].xyz.normalized(), -Mw.col[2].xyz.normalized()
    d = 0.05
    cd = cam.data
    half_w = cd.ortho_scale / 2.0 if cd.type == "ORTHO" else d * 18.0 / cd.lens
    half_h = half_w * res[1] / res[0]
    size = half_h * 2.0 * rel_size
    R = Matrix((right, up, -fwd)).transposed().to_4x4()
    cu = bpy.data.curves.new("PV_LabelCurve", "FONT")
    cu.body = text
    cu.size = size
    cu.align_y = "TOP"
    txt = bpy.data.objects.new("PV_Label", cu)
    bpy.context.scene.collection.objects.link(txt)
    cu.materials.append(flat_mat(style.UI.PARCHMENT))
    corner = Mw.translation + fwd * d + right * (-half_w * 0.97) + up * (half_h * 0.95)
    txt.matrix_world = Matrix.Translation(corner) @ R
    bpy.context.view_layer.update()
    w, h = txt.dimensions.x, txt.dimensions.y
    pad = size * 0.3
    me = bpy.data.meshes.new("PV_LabelPlate")
    me.from_pydata([(-pad, pad, 0), (w + pad, pad, 0), (w + pad, -h - pad, 0), (-pad, -h - pad, 0)], [], [(0, 1, 2, 3)])
    me.materials.append(flat_mat(style.UI.INK))
    plate = bpy.data.objects.new("PV_LabelPlate", me)
    bpy.context.scene.collection.objects.link(plate)
    plate.matrix_world = Matrix.Translation(corner + fwd * (size * 0.05)) @ R
    return [txt, plate]


def look_at(loc, target):
    return (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()


def render(path, loc, rot, ortho=None, lens=50.0, res=CELL, bg=BG, label=None, light="STUDIO", color_type="MATERIAL"):
    scene = bpy.context.scene
    cd = bpy.data.cameras.new("PV_Cam")
    cd.sensor_fit = "HORIZONTAL"
    cd.sensor_width = 36.0
    cd.clip_start = 0.01
    cd.clip_end = 100.0
    if ortho:
        cd.type = "ORTHO"
        cd.ortho_scale = ortho
    else:
        cd.lens = lens
    cam = bpy.data.objects.new("PV_Cam", cd)
    scene.collection.objects.link(cam)
    cam.location = Vector(loc)
    cam.rotation_euler = rot
    scene.camera = cam
    bpy.context.view_layer.update()
    staged = add_label(label, cam, res) if label else []
    r = scene.render
    r.engine = "BLENDER_WORKBENCH"
    r.resolution_x, r.resolution_y = res
    r.resolution_percentage = 100
    r.image_settings.file_format = "PNG"
    r.image_settings.color_mode = "RGB"
    r.film_transparent = False
    r.filepath = str(path)
    sh = scene.display.shading
    sh.light = light
    sh.color_type = color_type
    sh.show_cavity = light != "FLAT"
    if scene.world is None:
        scene.world = bpy.data.worlds.new("PV_World")
    scene.world.color = style.linear(bg)
    vs = scene.view_settings
    vs.view_transform, vs.look, vs.exposure = "Standard", "None", 0.0
    bpy.ops.render.render(write_still=True)
    remove_objects(staged)
    bpy.data.objects.remove(cam, do_unlink=True)
    bpy.data.cameras.remove(cd)
    return str(path)


VIEWS = {   # name: (location, rotation); ortho views centered on the rest body center
    "top": ((0.0, 0.0, 3.0), Euler((0.0, 0.0, 0.0))),
    "side": ((0.0, -3.0, 0.0), Euler((math.pi / 2.0, 0.0, 0.0))),
}


def ref_lines(fish, view):
    """Ink reference line on the rest spine (behind or below the fish) and a tick at Grip (the still chest)."""
    gx = fish.geo.grip.x
    ink = style.UI.INK
    if view == "top":
        return [add_box("PV_Ref", (0.0, 0.0, -0.3), (0.9, 0.004, 0.001), ink),
                add_box("PV_RefTick", (gx, 0.0, -0.3), (0.004, 0.05, 0.001), ink)]
    return [add_box("PV_Ref", (0.0, 0.3, 0.0), (0.9, 0.001, 0.004), ink),
            add_box("PV_RefTick", (gx, 0.3, 0.0), (0.004, 0.001, 0.05), ink)]


@contextmanager
def solo_render(fishes, fish):
    hidden = [(f.mesh, f.mesh.hide_render) for f in fishes]
    for f in fishes:
        f.mesh.hide_render = f is not fish
    try:
        yield
    finally:
        for m, h in hidden:
            m.hide_render = h


# A_Fish_Hooked_Hang also shows the fish hanging (nose up on screen, as LureFishItem::UpdateHooked turns it): from its
# RIGHT side (Blender -Y; the side the game turns to the viewer) and from its back (+Z, the edge view). The fish stays
# at the armature origin; the camera is turned so +X (the nose, up the line) is up on screen. Ink ring = the hook
# (the reference-pose Mouth), ink line = the fishing line.
HANG_VIEWS = {"hang_player": (Vector((0.0, -3.0, 0.0)), Vector((0.0, 1.0, 0.0))),
              "hang_back": (Vector((0.0, 0.0, 3.0)), Vector((0.0, 0.0, -1.0)))}
HANG_CENTER_X = 0.02
HANG_ORTHO = 0.85


def hang_camera(view):
    """(location, rotation) of an ortho camera looking at the hanging fish with the nose up on screen."""
    offset, fwd = HANG_VIEWS[view]
    loc = offset + Vector((HANG_CENTER_X, 0.0, 0.0))
    up = Vector((1.0, 0.0, 0.0))
    right = fwd.cross(up).normalized()
    up = right.cross(fwd).normalized()
    return loc, Matrix((right, up, -fwd)).transposed().to_euler()


def hook_marks(fish):
    """Hook ring at the reference Mouth + the line up the fish's +X axis (the hang views)."""
    import bmesh
    m = fish.arm.data.bones["Mouth"].head_local.copy()
    me = bpy.data.meshes.new("PV_Hook")
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=0.009)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(flat_mat(style.UI.INK))
    ob = bpy.data.objects.new("PV_Hook", me)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = m
    return [ob, add_polyline("PV_Line", [m, m + Vector((0.6, 0.0, 0.0))], style.UI.INK, radius=0.0015)]


def clip_sheet(fishes, clip, tmp):
    cells = []
    rows = [(f, v) for f in fishes for v in ("top", "side")]
    if clip.role == "Hang":
        rows += [(f, "hang_player") for f in fishes] + [(fishes[0], "hang_back")]
    frames = strip_frames(clip)
    for fish, view in rows:
        amp = PREVIEW_AMPLITUDE[fish.species]
        hang = view in HANG_VIEWS
        stage = hook_marks(fish) if hang else ref_lines(fish, view)
        with solo_render(fishes, fish):
            for col, f in enumerate(frames):
                pose_fish(fish, clip_quats(clip, f, amp))
                text = "f%d" % f
                if col == 0:
                    text = "%s\n%s %s x%.1f f%d" % (short(clip.name), fish.species, view, amp, f)
                loc, rot = hang_camera(view) if hang else VIEWS[view]
                cells.append(render(tmp / ("%s_%s_%s_%02d.png" % (clip.name, fish.species, view, col)), loc, rot,
                                    ortho=HANG_ORTHO if hang else ORTHO, label=text,
                                    bg=style.TROPICAL.SKY_DAY if hang else BG))
        remove_objects(stage)
        pose_fish(fish, fr.Pose().quats())
    return pb.contact_sheet(cells, PREVIEW_DIR / ("%s_anim.png" % clip.name), cols=len(frames), cell=CELL)


def seam_sheet(fishes, clips, metrics, tmp):
    """Every loop across its seam (Bonefish, top view, amplitude 1): frames N-2, N-1, N (Unreal's last key, = frame 0),
    1, 2, labelled with the seam numbers from clip_metrics (seam distance, the seam's second difference vs the median)."""
    fish = next(f for f in fishes if f.species == REFERENCE)
    cells, cols = [], 5
    loops = [c for c in clips if c.loop and c.frames > 2]
    with solo_render(fishes, fish):
        for c in loops:
            n = c.frames
            m = metrics[fish.species][c.name]
            stage = ref_lines(fish, "top")
            for j, f in enumerate([n - 2, n - 1, n, 1, 2]):
                pose_fish(fish, clip_quats(c, f))
                if j == 0:
                    text = "%s seam\nf%d" % (short(c.name), f)
                elif f == n:
                    text = "f%d = f0 (last key)\nseam %.4f mm" % (f, m["seam_mm"])
                elif j == 3:
                    text = "f%d\nseam 2nd diff %.1f mm\n(median %.1f)" % (f, m["seam_accel_mm"], m["median_accel_mm"])
                else:
                    text = "f%d" % f
                loc, rot = VIEWS["top"]
                cells.append(render(tmp / ("seam_%s_%d.png" % (short(c.name), j)), loc, rot, ortho=ORTHO, label=text))
            remove_objects(stage)
    pose_fish(fish, fr.Pose().quats())
    return pb.contact_sheet(cells, PREVIEW_DIR / "SK_Fish_seams.png", cols=cols, cell=CELL)


def fit_2d(co):
    """A fish lying on its side, curled in its flank plane (fish X-Z): the smallest-area rectangle around its footprint
    (length x width, cm), its thickness (fish Y, cm) and its nose-to-tail X extent."""
    best = None
    for a10 in range(0, 1800, 5):
        a = math.radians(a10 / 10.0)
        c, s = math.cos(a), math.sin(a)
        u = [p.x * c + p.z * s for p in co]
        v = [-p.x * s + p.z * c for p in co]
        L, W = max(u) - min(u), max(v) - min(v)
        if L < W:
            L, W = W, L
        if best is None or L * W < best[0]:
            best = (L * W, L, W)
    return {"length_cm": round(best[1] * 100.0, 1), "width_cm": round(best[2] * 100.0, 1),
            "thickness_cm": round((max(p.y for p in co) - min(p.y for p in co)) * 100.0, 2),
            "x_extent_cm": round((max(p.x for p in co) - min(p.x for p in co)) * 100.0, 1)}


def curled_report(fishes, clip):
    """Fit numbers of the curled pose per species (RESULT_JSON curled): footprint, thickness and the lie offset on
    each side (the lowest point below the origin when lying on that side; equal to the straight fish's dock lie)."""
    out = {}
    for fish in fishes:
        pose_fish(fish, clip.pose(0).quats())
        co = fish.probe.coords()
        r = fit_2d(co)
        r["lie_offset_cm"] = {"right_side_down": round(-min(p.y for p in co) * 100.0, 2),
                              "left_side_down": round(max(p.y for p in co) * 100.0, 2)}
        out[fish.species] = r
        pose_fish(fish, fr.Pose().quats())
    return out


def curled_sheet(fishes, clip, tmp):
    """The 1-frame curled pose: per species, lying on its right side seen from above (= the fish seen from its left,
    +Y) over the pale straight fish, the same on its left side, from the back (flat = stacks), and 3/4."""
    cells = []
    res = (400, 300)
    for fish in fishes:
        with solo_render(fishes, fish):
            pose_fish(fish, clip.pose(0).quats())
            co = fish.probe.coords()
            cx = (max(p.x for p in co) + min(p.x for p in co)) / 2.0
            cz = (max(p.z for p in co) + min(p.z for p in co)) / 2.0
            ghosts = []                         # the straight fish (the mesh without its armature), pale, behind
            for gy in (-0.3, 0.3):
                g = bpy.data.objects.new("PV_Ghost", fish.mesh.data)
                bpy.context.scene.collection.objects.link(g)
                g.location = (0.0, gy, 0.0)
                for slot in g.material_slots:
                    slot.link = "OBJECT"
                    slot.material = flat_mat("#9FC3C6")
                ghosts.append(g)
            views = [("right side down, from above", (0.0, 3.0, cz + 0.07), Euler((math.pi / 2.0, 0.0, math.pi)), 0),
                     ("left side down, from above", (0.0, -3.0, cz + 0.07), Euler((math.pi / 2.0, 0.0, 0.0)), 1),
                     ("from the back (flat)", (0.0, -0.07, 3.0), Euler((0.0, 0.0, 0.0)), None)]
            for k, (name, loc, rot, gi) in enumerate(views):
                for j, g in enumerate(ghosts):
                    g.hide_render = j != gi
                text = "Curled %s\n%s" % (fish.species, name) + ("\n(pale: the straight fish)" if k == 0 else "")
                cells.append(render(tmp / ("curled_%s_%d.png" % (fish.species, k)), loc, rot, ortho=0.80, res=res,
                                    label=text))
            for g in ghosts:
                bpy.data.objects.remove(g, do_unlink=True)
            loc = (cx + 0.35, 0.55, cz + 0.45)
            cells.append(render(tmp / ("curled_%s_34.png" % fish.species), loc, look_at(loc, (cx, 0.0, cz)),
                                lens=45.0, res=res, label="Curled %s 3/4" % fish.species))
        pose_fish(fish, fr.Pose().quats())
    return pb.contact_sheet(cells, PREVIEW_DIR / ("%s_anim.png" % clip.name), cols=4, cell=res)


def midline(fish):
    """Current midline (top view): the nose pole, every body ring's centroid, the tail tip (on the Tail bone)."""
    co = fish.probe.coords()
    pts = [co[fish.nose_vert]] + [sum((co[i] for i in r), Vector()) / len(r) for r in fish.probe.rings]
    tip = skin_matrix(fish, "Tail") @ Vector((fish.geo.tip_x, 0.0, 0.0))
    return [Vector((p.x, p.y, 0.05)) for p in pts + [tip]]


def ramp(t):
    """Strobe color ramp from palette deep water to reef coral (old -> new frames)."""
    return style.mix_hex(style.TROPICAL.DEEP_WATER, style.TROPICAL.REEF, t)


def strobe_sheet(fishes, clips, tmp):
    fish = next(f for f in fishes if f.species == REFERENCE)
    cells = []
    res = (480, 360)
    loc, rot = VIEWS["top"]
    with solo_render(fishes, None):
        for clip in clips + [None]:
            if clip is None:        # the ambient WPO wave (MF_FishSwim formula) over the rest body, same scale
                frames = list(range(0, 30, 3))
                geo = fish.geo
                lines = []
                us = [i / 20.0 for i in range(21)]
                for j, f in enumerate(frames):
                    pts = [(geo.nose_x - u * geo.length, fr.wpo_lateral(u, f / 30.0, fr.IDLE_AMP) * geo.length, 0.05)
                           for u in us]
                    lines.append(add_polyline("PV_Strobe", pts, ramp(j / max(1, len(frames) - 1))))
                title = "Ambient WPO (MF_FishSwim) 1 Hz"
            else:
                frames = STROBE_FRAMES.get(clip.name, list(range(0, clip.frames, max(1, clip.frames // 10))))
                lines = []
                for j, f in enumerate(frames):
                    pose_fish(fish, clip_quats(clip, f))
                    lines.append(add_polyline("PV_Strobe", midline(fish), ramp(j / max(1, len(frames) - 1))))
                title = "%s  f%d-%d" % (short(clip.name), frames[0], frames[-1])
            stage = lines + [add_box("PV_Ref", (0.0, 0.0, 0.0), (0.9, 0.002, 0.001), style.UI.INK),
                             add_box("PV_RefTick", (fish.geo.grip.x, 0.0, 0.0), (0.003, 0.04, 0.001), style.UI.INK)]
            name = short(clip.name) if clip else "WPO"
            cells.append(render(tmp / ("strobe_%s.png" % name), loc, rot, ortho=0.7, res=res, bg=BG_LIGHT,
                                label=title, light="FLAT"))
            remove_objects(stage)
    pose_fish(fish, fr.Pose().quats())
    return pb.contact_sheet(cells, PREVIEW_DIR / "SK_Fish_strobe.png", cols=4, cell=res)


def flop_dock_sheet(fishes, flop, lies, tmp):
    """Landed_Flop lying on its right side on dock planks: the armature rolled +90 deg about X (fish -Y = right side
    down) and lifted so the lie pose's lowest point touches the plank top (z = 0)."""
    cells = []
    frames = strip_frames(flop)
    rows = [(f, "low") for f in fishes] + [(fishes[0], "top")]
    planks = [add_box("PV_Plank", (0.0, y, -0.03), (1.0, 0.13, 0.06), style.TROPICAL.WEATHERED_WOOD)
              for y in (-0.14, 0.0, 0.14)]
    for fish, view in rows:
        arm = fish.arm
        keep = (arm.location.copy(), arm.rotation_euler.copy())
        arm.rotation_euler = (math.pi / 2.0, 0.0, 0.0)
        arm.location = (0.0, 0.0, -lies[fish.species][0])
        with solo_render(fishes, fish):
            for col, f in enumerate(frames):
                pose_fish(fish, clip_quats(flop, f, 1.0))
                text = "f%d" % f
                if col == 0:
                    text = "Landed_Flop %s\n%s, right side down f%d" % (fish.species, view, f)
                if view == "low":
                    loc, rot = (0.0, -3.0, 0.07), Euler((math.pi / 2.0, 0.0, 0.0))
                else:
                    loc, rot = (0.0, 0.0, 3.0), Euler((0.0, 0.0, 0.0))
                cells.append(render(tmp / ("flop_%s_%s_%02d.png" % (fish.species, view, col)), loc, rot, ortho=0.75,
                                    label=text, bg=style.TROPICAL.SKY_DAY))
        arm.location, arm.rotation_euler = keep
        pose_fish(fish, fr.Pose().quats())
    remove_objects(planks)
    return pb.contact_sheet(cells, PREVIEW_DIR / "SK_Fish_flop_dock.png", cols=len(frames), cell=CELL)


def overview_sheet(fishes, clips, bend_frames, tmp):
    """3/4 view from the concave side at each clip's tightest bend (the fold_min frame): the pinch check."""
    cells = []
    res = (320, 240)
    for fish in fishes:
        amp = 1.0
        with solo_render(fishes, fish):
            for clip in clips:
                f = bend_frames[fish.species][clip.name]
                pose_fish(fish, clip_quats(clip, f, amp))
                head_yaw = clip.pose(f).yaw["Head"]
                side = 1.0 if head_yaw >= 0.0 else -1.0      # the concave side: head yawed toward it
                loc = (0.30, 0.78 * side, 0.42)
                cells.append(render(tmp / ("ov_%s_%s.png" % (fish.species, short(clip.name))), loc,
                                    look_at(loc, (0.0, 0.0, 0.0)), lens=45.0, res=res,
                                    label="%s %s f%d" % (short(clip.name), fish.species, f)))
        pose_fish(fish, fr.Pose().quats())
    return pb.contact_sheet(cells, PREVIEW_DIR / "SK_Fish_anim.png", cols=len(clips), cell=res)


def tuck_sheet(fishes, tmp):
    cells = []
    res = (320, 240)
    for fish in fishes:
        with solo_render(fishes, fish):
            for name, pec in (("as modeled", 0.0), ("tucked %d" % fr.TUCK_DEG, fr.TUCK_DEG)):
                pose_fish(fish, fr.Pose().pecs(pec, pec).quats())
                cells.append(render(tmp / ("tuck_%s_front_%d.png" % (fish.species, pec)), (2.0, 0.0, 0.0),
                                    look_at((2.0, 0.0, 0.0), (0.0, 0.0, 0.0)), ortho=0.36, res=res,
                                    label="%s front\npectorals %s" % (fish.species, name)))
            pose_fish(fish, lie_pose().quats())
            cells.append(render(tmp / ("tuck_%s_bottom.png" % fish.species), (0.0, 0.0, -2.0),
                                Euler((math.pi, 0.0, 0.0)), ortho=0.7, res=res,
                                label="%s from below, tucked" % fish.species))
        pose_fish(fish, fr.Pose().quats())
    return pb.contact_sheet(cells, PREVIEW_DIR / "SK_Fish_tuck.png", cols=3, cell=res)


def pec_sheet(fishes, clips, metrics, tmp):
    """Close-ups of both pectorals (left side from +Y, right side from -Y) at the rest pose and at each species' three
    worst root-strip-strain frames (RESULT_JSON pec_strip_strain_max): does the fin base still read as attached?"""
    cells = []
    res = (320, 240)
    for fish in fishes:
        worst = sorted(clips, key=lambda c: -metrics[fish.species][c.name]["pec_strip_strain_max"])[:3]
        shots = [(None, 0)] + [(c, metrics[fish.species][c.name]["pec_strip_strain_frame"]) for c in worst]
        c0 = fish.geo.pecs["L"]["center"]
        with solo_render(fishes, fish):
            for clip, f in shots:
                pose_fish(fish, clip_quats(clip, f) if clip else fr.Pose().quats())
                name = "rest" if clip is None else "%s f%d" % (short(clip.name), f)
                strain = 0.0 if clip is None else metrics[fish.species][clip.name]["pec_strip_strain_max"]
                for side, sy in (("L", 1.0), ("R", -1.0)):
                    loc = (c0.x - 0.03, sy * 1.0, c0.z + 0.01)
                    rot = look_at(loc, (c0.x - 0.03, 0.0, c0.z + 0.01))
                    cells.append(render(tmp / ("pec_%s_%s_%s.png" % (fish.species, name.replace(" ", "_"), side)),
                                        loc, rot, ortho=0.20, res=res,
                                        label="%s %s\npectoral %s strain %.2f" % (fish.species, name, side, strain)))
        pose_fish(fish, fr.Pose().quats())
    return pb.contact_sheet(cells, PREVIEW_DIR / "SK_Fish_pec.png", cols=8, cell=res)


BONE_HEX = {"Head": "#C0392B", "Spine_01": "#E8C46A", "Spine_02": "#3FA34D", "Spine_03": "#3ED1C4",
            "Spine_04": "#2F4A5E", "Tail": "#9B59B6", "Fin_Pectoral_L": "#FF9A5A", "Fin_Pectoral_R": "#FF9A5A"}


def weights_sheet(fishes, tmp):
    """Vertex color = the bone colors mixed by skin weight (a temporary color attribute, removed afterwards)."""
    cells = []
    res = (480, 270)
    scene = bpy.context.scene
    for fish in fishes:
        me = fish.mesh.data
        idx = {fish.mesh.vertex_groups[b].index: b for b in fr.DEFORM}
        attr = me.color_attributes.new("PV_Weights", "FLOAT_COLOR", "POINT")
        for v in me.vertices:
            c = Vector((0.0, 0.0, 0.0))
            for g in v.groups:
                if g.group in idx:
                    c += Vector(style.hex_to_linear_rgba(BONE_HEX[idx[g.group]])[:3]) * g.weight
            attr.data[v.index].color = (c.x, c.y, c.z, 1.0)
        me.color_attributes.active_color = attr
        with solo_render(fishes, fish):
            pose_fish(fish, fr.Pose().quats())
            for view in ("side", "top"):
                loc, rot = VIEWS[view]
                p = tmp / ("weights_%s_%s.png" % (fish.species, view))
                cells.append(render(p, loc, rot, ortho=0.7, res=res, bg=BG_LIGHT, light="FLAT", color_type="VERTEX",
                                    label="%s weights, %s\nHead red, chest yellow, Spine_02-04 green/cyan/navy,\n"
                                          "Tail purple, pectorals orange" % (fish.species, view)))
        me.color_attributes.remove(me.color_attributes["PV_Weights"])
    scene.display.shading.color_type = "MATERIAL"
    return pb.contact_sheet(cells, PREVIEW_DIR / "SK_Fish_weights.png", cols=2, cell=res)


def render_previews(fishes, clips, bend_frames, lies, metrics):
    tmp = PREVIEW_DIR / "anim_fish_cells"
    tmp.mkdir(parents=True, exist_ok=True)
    fk.preview_setup()
    out = {}
    for fish in fishes:
        fr.mute_all(fish.arm)
    out["overview"] = overview_sheet(fishes, clips, bend_frames, tmp)
    log("overview done")
    out["clips"] = {}
    for clip in clips:
        if clip.frames == 1:
            out["clips"][clip.name] = curled_sheet(fishes, clip, tmp)
        else:
            out["clips"][clip.name] = clip_sheet(fishes, clip, tmp)
        log("strip " + clip.name)
    out["strobe"] = strobe_sheet(fishes, [c for c in clips if c.frames > 1], tmp)
    out["seams"] = seam_sheet(fishes, clips, metrics, tmp)
    flop = next((c for c in clips if c.role == "Flop"), None)
    if flop is not None:
        out["flop_dock"] = flop_dock_sheet(fishes, flop, lies, tmp)
    out["tuck"] = tuck_sheet(fishes, tmp)
    out["pec"] = pec_sheet(fishes, clips, metrics, tmp)
    out["weights"] = weights_sheet(fishes, tmp)
    log("previews done")
    return out


# ---------------------------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------------------------
def species_report(fish, lie):
    b = fish.arm.data.bones
    mn, mx = pb.world_bounds([fish.mesh])
    return {
        "static_mesh": fish.static_name, "skeletal_mesh": fish.asset,
        "length_cm": round(fish.geo.length * 100.0, 2), "body_len_cm": round(fish.geo.body_len * 100.0, 2),
        "s_tail_tip": round(fish.geo.s_tip, 4),
        "bones_ue_cm": {x.name: {"parent": x.parent.name if x.parent else None, "deform": x.use_deform,
                                 "head": ue_cm(x.head_local)} for x in b},
        "mouth_ue_cm": ue_cm(b["Mouth"].head_local), "grip_ue_cm": ue_cm(b["Grip"].head_local),
        "pectoral_hinge_off_canon_deg": fish.weights.get("pectoral_hinge_off_canon_deg"),
        "weights": {k: v for k, v in fish.weights.items() if k != "pectoral_hinge_off_canon_deg"},
        "triangles": pb.triangle_count([fish.mesh]),
        "bounds_ue_cm": {"min": ue_cm((mn.x, mx.y, mn.z)), "max": ue_cm((mx.x, mn.y, mx.z))},
        "dock_lie": {"lowest_point_m": round(lie[0], 4), "lowest_part": lie[1],
                     "lie_offset_cm": round(-lie[0] * 100.0, 2)},
    }


VERIFY = [("A_Fish_Fight_Dart", 4), ("A_Fish_Swim_Idle", 15), ("A_Fish_Curled", 0), ("A_Fish_Hooked_Hang", 16),
          ("A_Fish_Swim_Tired", 36)]


def verify_points(fishes):
    """Numbers the editor-operator can check in Unreal: component-space bone heads (cm) of Mouth and Tail at a few clip
    frames, per species, at amplitude 1 (Apply Additive alpha 1 on the species' own reference pose). The two species
    must give DIFFERENT numbers (each keeps its proportions); equal numbers mean the additive base is wrong."""
    out = {}
    for fish in fishes:
        for name, f in VERIFY:
            clip = next(c for c in fr.CLIPS if c.name == name)
            pose_fish(fish, clip.pose(f).quats())
            for b in ("Mouth", "Tail"):
                out.setdefault(fish.species, {})["%s f%d %s" % (short(name), f, b)] = \
                    ue_cm(fish.arm.pose.bones[b].head)
        pose_fish(fish, fr.Pose().quats())
    return out


def verdict(checks):
    fails = [k for k, ok in checks.items() if not ok]
    return {"ok": not fails, "failed": fails}


def main():
    args = pb.parse_args("SK_Fish", CATEGORY)
    pb.reset_scene()
    scene = bpy.context.scene
    scene.render.fps = fr.FPS
    scene.render.fps_base = 1.0
    log("quick=%s only=%s tuck=%s" % (QUICK, sorted(ONLY), fr.TUCK_DEG))

    fishes = [Fish(stem, asset, species) for stem, asset, species in SPECIES]
    ref = next(f for f in fishes if f.species == REFERENCE)
    log("rigged: " + ", ".join("%s %s" % (f.species, f.weights) for f in fishes))
    for clip in [fr.REST_CLIP] + fr.CLIPS:
        fr.key_clip(ref.arm, clip)
    fr.mute_all(ref.arm)
    clips = [c for c in fr.CLIPS if not ONLY or short(c.name) in ONLY]

    # --- checks -------------------------------------------------------------------------------------------------
    rest = {f.species: rest_check(f) for f in fishes}
    scans = {f.species: fr.tuck_scan(f.arm, f.probe) for f in fishes}
    tuck_limit = 0.5
    tuck_suggested = suggest_tuck(list(scans.values()), tuck_limit)
    log("tuck scan: suggested %d deg (current %s)" % (tuck_suggested, fr.TUCK_DEG))
    lies = {f.species: lie_min_y(f) for f in fishes}
    metrics, bend_frames = {}, {}
    for fish in fishes:
        metrics[fish.species], bend_frames[fish.species] = {}, {}
        for clip in clips:
            lie_y = lies[fish.species][0] if clip.role == "Flop" else None
            m, ff = clip_metrics(fish, clip, 1.0, lie_y)
            if clip.role == "Flop" and PREVIEW_AMPLITUDE[fish.species] < 1.0:
                m2, _ = clip_metrics(fish, clip, PREVIEW_AMPLITUDE[fish.species], lie_y)
                m["drop_below_lie_mm_at_preview_amplitude"] = m2["drop_below_lie_mm"]
            metrics[fish.species][clip.name] = m
            bend_frames[fish.species][clip.name] = ff
        log("metrics %s done" % fish.species)
    keyed = keyed_check(ref, clips)
    log("keyed check %s" % keyed)
    anchors = anchor_check(fishes, clips)
    hang = next((c for c in clips if c.role == "Hang"), None)
    tired = next((c for c in clips if c.role == "Tired"), None)
    hang_mouth = hang_check(fishes, hang) if hang else None
    offsets = hang_offsets(fishes, hang) if hang else None
    lean = tired_upright(tired) if tired else None
    log("anchor, hang %s, tired lean %s" % (hang_mouth, lean))
    for fish in fishes:                      # back to the bind pose (bounds and the exports read the rest pose)
        fr.mute_all(fish.arm)
        pose_fish(fish, fr.Pose().quats())

    worst = lambda key, fn: fn(m[key] for s in metrics.values() for m in s.values())  # noqa: E731
    checks = {
        "weights": all(f.weights["max_weight_sum_error"] <= LIMITS["weight_sum_error"]
                       and f.weights["max_influences"] <= LIMITS["max_influences"] and f.weights["unweighted"] == 0
                       for f in fishes),
        "rest_skin": max(rest.values()) <= LIMITS["rest_skin_mm"],
        "tuck": fr.TUCK_DEG >= tuck_suggested,
        "ring_area": worst("ring_area_min", min) >= LIMITS["ring_area_min"],
        "fold": worst("fold_min", min) >= LIMITS["fold_min"],
        "pec_in": worst("pec_in_mm", max) <= LIMITS["pec_in_mm"],
        "seam": worst("seam_mm", max) <= LIMITS["seam_mm"],
        "flop_dock": all(m.get("drop_below_lie_mm", 0.0) <= LIMITS["flop_drop_mm"]
                         for s in metrics.values() for m in s.values()),
        "keyed": keyed["max_bone_error_mm"] <= LIMITS["keyed_error_mm"],
        "anchor": all(v["grip_mm"] <= LIMITS["anchor_mm"] and v["chest_axis_deg"] <= LIMITS["anchor_deg"]
                      for s in anchors.values() for v in s.values()),
    }
    if hang_mouth is not None:
        checks["hang_mouth"] = all(v <= LIMITS["hang_mouth_mm"] for s in hang_mouth.values() for v in s.values())
    if lean is not None:
        checks["tired_upright"] = lean <= fr.TIRED_MAX_LEAN_DEG

    extra = {
        "quick": QUICK, "clips_checked": [c.name for c in clips],
        "skeleton": "SKEL_Fish: %d bones, root = 'root' (the armature object is exported as 'Armature', which Unreal "
                    "drops)" % len(ref.arm.data.bones),
        "tuck_deg": fr.TUCK_DEG, "tuck_suggested_deg": tuck_suggested, "tuck_limit_mm": tuck_limit,
        "tuck_scan": {s: v for s, v in scans.items()},
        "rest_skin_error_mm": rest,
        "species": {f.species: species_report(f, lies[f.species]) for f in fishes},
        "verify_ue_cm": verify_points(fishes),
        "clips": [{"name": c.name, "frames": [0, c.frames], "seconds": round(c.seconds, 3), "loop": c.loop,
                   "role": c.role, "cycle_hz": c.cycle_hz, "notes": c.notes} for c in [fr.REST_CLIP] + fr.CLIPS],
        "metrics": metrics, "keyed_check": keyed, "limits": LIMITS,
        "anchor_check": anchors, "hang_mouth_mm": hang_mouth, "hang_offsets_t061": offsets,
        "tired_max_lean_deg": lean,
    }
    curled = next((c for c in clips if c.role == "Curled"), None)
    if curled is not None:
        extra["curled"] = curled_report(fishes, curled)
        log("curled %s" % extra["curled"])

    if not QUICK:
        exports, units = export_all(fishes, ref)
        log("exported %d files" % len(exports))
        check = reimport_check(exports, fishes, ref, [c for c in fr.CLIPS])
        log("reimport done")
        checks["reimport"] = all(v["max_bone_head_error_mm"] <= LIMITS["reimport_mm"]
                                 and v["rest_mesh_max_error_mm"] is not None
                                 and v["rest_mesh_max_error_mm"] <= LIMITS["reimport_mm"]
                                 and v["rest_bone_scale_dev"] <= 1e-4 for v in check["SK"].values()) and \
            all(v["max_pose_error_mm"] <= LIMITS["reimport_mm"] and v["posed_bone_scale_dev"] <= 1e-4
                for k, v in check["clips"].items() if k != fr.REST_CLIP.name)
        checks["additive"] = all(e <= LIMITS["additive_mm"] for v in check["additive"].values() for e in v.values())
        checks["fbx_units"] = all(u["ok"] for u in units.values())
        extra.update({"exports": exports, "fbx_units": units, "reimport_check": check})
        extra["previews"] = render_previews(fishes, clips, bend_frames, lies, metrics)
        args.out = exports[ref.asset]
        args.preview = extra["previews"]["overview"]
    extra["verdict"] = verdict(checks)
    extra["checks"] = checks
    for fish in fishes:
        fr.mute_all(fish.arm)
        pose_fish(fish, fr.Pose().quats())
    if args.save_blend:
        pb.BLEND_ROOT.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(pb.BLEND_ROOT / "anim_fish.blend"))
    log("verdict %s" % extra["verdict"])
    pb.report(args, [f.mesh for f in fishes], extra)


if __name__ == "__main__":
    main()

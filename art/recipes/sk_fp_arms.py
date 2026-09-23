"""SK_FPArms: stylized low-poly first-person arms (placeholder, T-004). MESH ONLY, NOT RIGGED: the
animation-artist builds the armature on top of build() (see "For the rig" below).

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sk_fp_arms.py
Export: art/export/Characters/SK_FPArms.fbx (static mesh, no armature yet)
Preview: Saved/AgentLogs/previews/SK_FPArms.png (contact sheet: 3/4, first-person camera view over water, top
and right-side orthographic views with the eye marker (red ball + arrow along +X), right hand with the edge loops
drawn as a wireframe, right palm and thumb)

Look: rolled olive shirt sleeves (upper arm to just below the elbow, a chunky two-roll cuff), bare forearms, chunky
mitten hands (one finger block + a separate thumb) that can close around a rod grip. One mesh, two materials:
- M_FPArms_Sleeve #7C8A63, a faded olive/sage fishing shirt: palm green (#3F8F4A) washed out towards sand
  (#F2D6A2) and greyed. Muted and mid-dark so it never competes with the red accent (#FF4D3D: bobber, UI) that
  sits near it on screen; its olive hue is clearly separate from the turquoise water the arms are usually seen
  against; and its value is well below the skin so sleeve and forearm read apart.
- M_FPArms_Skin #B98563, a neutral mid-tone tan (not pink, not orange, so it does not echo the reef coral).

AXES AND ORIGIN (Blender 1 unit = 1 m, Z up; Unreal = Blender * 100 cm with Y negated, default FBX import):
- Origin (0, 0, 0) = the CAMERA / EYE POINT. Attach the arms to the first-person camera with a zero relative
  transform: the mesh already sits where it must be relative to the eye.
- Forward = Blender +X = Unreal +X (the camera's view direction). The arms extend along +X.
- Left = Blender +Y (= Unreal -Y): the left arm (_l) is on +Y, the right arm (_r) on -Y. Up = +Z.
- Rest pose: arms reaching forward, elbows bent ~30 deg (pointing down and out), wrists nearly straight, hands
  open-ish: palms turned 40 deg from palm-down towards the midline, fingers curled 18 deg at the knuckles, thumbs
  spread out and forward. Shoulders at (-0.08, +-0.19, -0.23) m: 8 cm behind and 23 cm below the eye, out of
  view. Wrists at (0.48, +-0.17, -0.215) m. In this pose the hands show at the bottom of a 90 deg FOV view (wrists
  ~24 deg below the view axis); the sleeves stay out of view until the arms are raised (casting).

VERTEX GROUPS (future bones; weights blend 75/25, 50/50, 25/75 across the three loops at each joint, sum 1.0):
  upperarm_l, lowerarm_l, hand_l, thumb_l, fingers_l, upperarm_r, lowerarm_r, hand_r, thumb_r, fingers_r
Edge loops for bending: 3 at each elbow, 3 at each wrist (+ a gradual forearm twist), 3 at the finger base
(knuckles), 2 at the thumb base. 12 vertices around every arm/hand loop, 8 around the thumb; all quads except
the fan caps at the shoulder ends and fingertips. Each arm is one closed, manifold surface (2 islands total).

FOR THE RIG (animation-artist): the FBX has no weights (FBX stores weights only with an armature), so build the
rig on build():
    spec = importlib.util.spec_from_file_location("sk_fp_arms", "<repo>/art/recipes/sk_fp_arms.py")
    arms = importlib.util.module_from_spec(spec); spec.loader.exec_module(arms)
    obj, info = arms.build()          # mesh with the vertex groups above; info["bone_guides_m"] = head/tail
RESULT_JSON lists "bone_guides_m" (head and tail for every future bone, exact joint centers of the edge loops),
the hand frames and "rod_grip_point_m" (where SM_Rod_Basic's grip pivot sits when the right hand closes).

Budget: <= 3000 triangles for both arms (brief). No Bevel modifier (organic, smooth shaded; the cuff edge is kept
crisp with split normals instead).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import meshkit as mk  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

ASSET = "SK_FPArms"
BUDGET = 3000
SLEEVE_HEX = "#7C8A63"
SKIN_HEX = "#B98563"
SLEEVE, SKIN = 0, 1
N = 12                                   # vertices per ring

# Right-arm skeleton in camera space (the left arm is its mirror across Y = 0)
SHOULDER_R = Vector((-0.08, -0.19, -0.23))
WRIST_R = Vector((0.48, -0.17, -0.215))
UPPER_LEN, FORE_LEN = 0.31, 0.27
ELBOW_POLE = Vector((0.0, -0.35, -1.0))  # the elbow bends down and out
HAND_DIR = Vector((1.0, 0.10, 0.08)).normalized()
HAND_ROLL_DEG = 40.0                     # 0 = palm down, 90 = palm facing the midline (thumb up)
FINGER_CURL_DEG = 18.0
KNUCKLE_S = 0.106                        # wrist -> knuckle distance along the hand
UP = Vector((0.0, 0.0, 1.0))


def elbow_position(s, w):
    d = w - s
    dist = d.length
    dn = d.normalized()
    a = (dist * dist + UPPER_LEN ** 2 - FORE_LEN ** 2) / (2.0 * dist)
    h = math.sqrt(max(0.0, UPPER_LEN ** 2 - a * a))
    pole = ELBOW_POLE - dn * ELBOW_POLE.dot(dn)
    return s + dn * a + pole.normalized() * h


def elbow_bend_deg():
    """Bend at the elbow in the rest pose (0 = straight arm)."""
    e = elbow_position(SHOULDER_R, WRIST_R)
    return math.degrees((e - SHOULDER_R).angle(WRIST_R - e))


def rolled_frame(tangent, roll_deg):
    t, u, v = mk.frame_from(tangent, UP)
    c, s = math.cos(math.radians(roll_deg)), math.sin(math.radians(roll_deg))
    return t, (u * c + v * s).normalized(), (v * c - u * s).normalized()


def curl(vec, axis, deg):
    return mk.rotate_about(vec, axis, math.radians(deg))


def find_face(verts):
    faces = set(verts[0].link_faces)
    for v in verts[1:]:
        faces &= set(v.link_faces)
    return next(iter(faces))


def build_right_arm(sfx):
    """Build one arm in RIGHT-arm space with vertex groups named *_<sfx>.
    Returns (MeshBuilder, bone guides {bone: (head, tail)}, hand frame vectors, rod grip point)."""
    g_up, g_lo, g_ha, g_th, g_fi = ("upperarm_" + sfx, "lowerarm_" + sfx, "hand_" + sfx, "thumb_" + sfx,
                                    "fingers_" + sfx)
    mb = mk.MeshBuilder(groups=[g_up, g_lo, g_ha, g_th, g_fi])
    S, W = SHOULDER_R, WRIST_R
    E = elbow_position(S, W)
    d1, d2 = (E - S).normalized(), (W - E).normalized()
    hd = HAND_DIR
    rings, names = [], []

    def add(name, center, t, u, v, ru, rv=None, exp=2.0, mat=SLEEVE, w=None):
        rings.append(mk.ring(center, u, v, ru, rv, n=N, exponent=exp, mat=mat, w=w))
        names.append(name)

    # upper arm (sleeve): circular sections
    up1 = {g_up: 1.0}
    t, u, v = rolled_frame(d1, 0.0)
    add("A0", S - d1 * 0.030, t, u, v, 0.042, w=up1)
    add("A1", S - d1 * 0.005, t, u, v, 0.056, w=up1)
    add("A2", S + d1 * 0.100, t, u, v, 0.058, w=up1)
    add("A3", S + d1 * 0.200, t, u, v, 0.057, w=up1)
    # elbow: three loops, the middle one on the bisector
    add("E0", E - d1 * 0.045, t, u, v, 0.054, w={g_up: 0.75, g_lo: 0.25})
    tb, ub, vb = rolled_frame(d1 + d2, 0.0)
    add("E1", E, tb, ub, vb, 0.056, w={g_up: 0.5, g_lo: 0.5})
    lo1 = {g_lo: 1.0}

    def fore(tt, roll=None):
        roll = HAND_ROLL_DEG * max(0.0, (tt - 0.045)) / (FORE_LEN - 0.045) if roll is None else roll
        return (E + d2 * tt,) + rolled_frame(d2, roll)

    c, t, u, v = fore(0.045)
    add("E2", c, t, u, v, 0.053, w={g_up: 0.25, g_lo: 0.75})
    # rolled cuff: two rolls with a crease, then the cuff front face tucks in to the skin
    for name, tt, r in (("C0", 0.065, 0.058), ("C1", 0.078, 0.062), ("C2", 0.090, 0.057), ("C3", 0.102, 0.061),
                        ("C4", 0.114, 0.056)):
        c, t, u, v = fore(tt)
        add(name, c, t, u, v, r, w=lo1)
    # bare forearm (skin), widening side to side towards the wrist
    for name, tt, ru, rv, exp in (("T0", 0.116, 0.047, 0.046, 2.0), ("K1", 0.160, 0.047, 0.044, 2.0),
                                  ("K2", 0.215, 0.044, 0.036, 2.0)):
        c, t, u, v = fore(tt)
        add(name, c, t, u, v, ru, rv, exp, SKIN, lo1)
    c, t, u, v = fore(0.250)
    add("W0", c, t, u, v, 0.042, 0.032, 2.1, SKIN, {g_lo: 0.75, g_ha: 0.25})
    tw, uw, vw = rolled_frame(d2 + hd, HAND_ROLL_DEG)
    add("W1", W, tw, uw, vw, 0.041, 0.031, 2.3, SKIN, {g_lo: 0.5, g_ha: 0.5})
    # hand (palm): rounded-rectangle sections in the rolled hand frame
    th, uh, vh = rolled_frame(hd, HAND_ROLL_DEG)
    ha1 = {g_ha: 1.0}
    for name, s, ru, rv, exp, w in (("H0", 0.022, 0.047, 0.031, 2.5, {g_lo: 0.25, g_ha: 0.75}),
                                    ("H1", 0.050, 0.054, 0.032, 2.8, ha1),
                                    ("H2", 0.078, 0.056, 0.031, 2.8, ha1),
                                    ("N0", 0.094, 0.056, 0.030, 2.8, {g_ha: 0.75, g_fi: 0.25})):
        add(name, W + th * s, th, uh, vh, ru, rv, exp, SKIN, w)
    knuckle = W + th * KNUCKLE_S
    # knuckle loop on the half-curl bisector, then the finger block curled towards the palm (-v)
    tk, vk = curl(th, uh, FINGER_CURL_DEG / 2), curl(vh, uh, FINGER_CURL_DEG / 2)
    add("N1", knuckle, tk, uh, vk, 0.055, 0.029, 2.8, SKIN, {g_ha: 0.5, g_fi: 0.5})
    tf, vf = curl(th, uh, FINGER_CURL_DEG), curl(vh, uh, FINGER_CURL_DEG)
    fi1 = {g_fi: 1.0}
    for name, s, ru, rv, w in (("N2", 0.012, 0.054, 0.028, {g_ha: 0.25, g_fi: 0.75}), ("F1", 0.040, 0.053, 0.027, fi1),
                               ("F2", 0.066, 0.049, 0.025, fi1), ("F3", 0.083, 0.041, 0.022, fi1),
                               ("F4", 0.093, 0.028, 0.016, fi1)):
        add(name, knuckle + tf * s, tf, uh, vf, ru, rv, 2.8, SKIN, w)
    fingertip = knuckle + tf * 0.098
    vrings, poles = mb.loft(rings, cap_start=S - d1 * 0.045, cap_end=fingertip)
    ring_of = dict(zip(names, vrings))

    # thumb: grown out of a 2x2 face patch on the thumb side of the palm, slightly towards the palm side
    h0, h1, h2 = ring_of["H0"], ring_of["H1"], ring_of["H2"]
    mb.bm.normal_update()
    patch =[find_face([a[k], a[(k + 1) % N], b[(k + 1) % N], b[k]]) for a, b in ((h0, h1), (h1, h2)) for k in (10, 11)]
    n0 = sum((f.normal for f in patch), Vector()).normalized()
    pc = sum((v.co for f in patch for v in f.verts), Vector()) / 16.0

    def tdir(beta_deg, palm_deg):
        d = n0 * math.cos(math.radians(beta_deg)) + th * math.sin(math.radians(beta_deg))
        return (d - vh * math.tan(math.radians(palm_deg))).normalized()

    steps, offs = [], Vector()
    plan = [  # (segment length, spread forward deg, towards palm deg, scale along hand, scale across, scale normal,
        #    round: rectangular patch outline -> ellipse)
        # the spread ramps up loop by loop so every loop's front edge stays above the previous one (no fold)
        (0.009, 0.0, 0.0, 0.94, 1.02, 1.0, 0.0),    # thumb base loop 1 (parallel to the palm side)
        (0.018, 25.0, 6.0, 0.80, 1.18, 1.0, 1.0),   # thumb base loop 2 (joint)
        (0.020, 45.0, 10.0, 0.76, 1.12, 1.0, 1.0),
        (0.014, 55.0, 14.0, 0.66, 1.00, 1.0, 1.0),
        (0.010, 60.0, 16.0, 0.42, 0.66, 0.5, 1.0),  # rounded tip cap
    ]
    for seg, beta, palm, sa, sb, sn, rd in plan:
        d = tdir(beta, palm)
        offs = offs + d * seg
        steps.append({"offset": offs.copy(), "normal": d, "sa": sa, "sb": sb, "sn": sn, "round": rd,
                      "axis_hint": tuple(th)})
    thumb_w = [{g_ha: 0.5, g_th: 0.5}, {g_ha: 0.2, g_th: 0.8}, {g_th: 1.0}, {g_th: 1.0}, {g_th: 1.0}]
    mb.extrude_branch(patch, steps, thumb_w)

    thumb_head = pc - n0 * 0.012 + th * 0.004
    guides = {
        "upperarm": (S, E), "lowerarm": (E, W), "hand": (W, knuckle), "fingers": (knuckle, fingertip),
        "thumb": (thumb_head, pc + offs),
    }
    frames = {"hand_forward": th, "hand_thumb_side": uh, "hand_back": vh}
    # where a rod grip's axis sits when the fist closes: under the palm, mid-hand
    grip = W + th * 0.062 - vh * 0.040
    return mb, guides, frames, grip


def rnd(v):
    return [round(c, 4) for c in v]


def build():
    """Build SK_FPArms (both arms, one object, vertex groups). Returns (object, info)."""
    sleeve = style.make_material("M_FPArms_Sleeve", SLEEVE_HEX, "flat", roughness=0.85)
    skin = style.make_material("M_FPArms_Skin", SKIN_HEX, "flat", roughness=0.7)
    mirror = Matrix.Scale(-1.0, 4, (0.0, 1.0, 0.0))
    mb_r, guides_r, frames_r, grip_r = build_right_arm("r")
    arm_r = mb_r.to_object(ASSET, [sleeve, skin], sharp_angle_deg=60.0, sharp_materials={SLEEVE})
    mb_l, guides_l, frames_l, grip_l = build_right_arm("l")
    mb_l.transform(mirror, flip=True)
    arm_l = mb_l.to_object(ASSET + "_L", [sleeve, skin], sharp_angle_deg=60.0, sharp_materials={SLEEVE})
    pb.select_only([arm_r, arm_l])
    bpy.context.view_layer.objects.active = arm_r
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = ASSET
    obj.data.name = ASSET
    mk.smart_uv(obj)

    def mir(p):
        return Vector((p.x, -p.y, p.z))

    bone_guides = {}
    for bone, (head, tail) in guides_r.items():
        bone_guides[bone + "_r"] = {"head": rnd(head), "tail": rnd(tail)}
        bone_guides[bone + "_l"] = {"head": rnd(mir(head)), "tail": rnd(mir(tail))}
    info = {
        "origin": "camera / eye point (0,0,0); attach to the FP camera with a zero relative transform",
        "forward_axis": "+X (Blender) = +X (Unreal)", "left_axis": "+Y (Blender) = -Y (Unreal)", "up_axis": "+Z",
        "vertex_groups": sorted(g.name for g in obj.vertex_groups),
        "material_slots": [s.material.name for s in obj.material_slots],
        "bone_guides_m": bone_guides,
        "hand_frame_r": {k: rnd(v) for k, v in frames_r.items()},
        "rod_grip_point_m": {"r": rnd(grip_r), "l": rnd(mir(grip_r))},
        "rest_pose": {"elbow_bend_deg": round(elbow_bend_deg(), 1), "hand_roll_deg": HAND_ROLL_DEG,
                      "finger_curl_deg": FINGER_CURL_DEG, "thumb_spread_deg": "0 at the root ramping to 60 at the tip",
                      "wrist_below_view_axis_deg": round(math.degrees(math.atan2(-WRIST_R.z, WRIST_R.x)), 1)},
        "materials_hex": {"M_FPArms_Sleeve": SLEEVE_HEX, "M_FPArms_Skin": SKIN_HEX},
    }
    return obj, info


# ---------------------------------------------------------------------------------------------------
# Preview-only staging (never exported)
# ---------------------------------------------------------------------------------------------------
def stage_water():
    """Turquoise water 2.3 m below the eye (standing on a dock) for the first-person view."""
    wm = style.make_material("PV_Water", style.TROPICAL.SHALLOW_WATER, "water")
    bpy.ops.mesh.primitive_plane_add(size=400.0, location=(0.0, 0.0, -2.3))
    plane = bpy.context.active_object
    plane.data.materials.append(wm)

    def cleanup():
        bpy.data.objects.remove(plane, do_unlink=True)
    return cleanup


def stage_markers(obj, wire=False, eye=True):
    """Eye marker at the origin (+ a short +X arrow) and an optional wireframe overlay."""
    helpers = []
    if eye:
        mark = style.make_material("PV_Marker", style.TROPICAL.ACCENT, "flat")
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.012, segments=12, ring_count=6, location=(0, 0, 0))
        helpers.append(bpy.context.active_object)
        bpy.ops.mesh.primitive_cylinder_add(radius=0.004, depth=0.12, vertices=6, location=(0.06, 0, 0),
                                            rotation=(0, math.radians(90), 0))
        helpers.append(bpy.context.active_object)
        for h in helpers:
            h.data.materials.append(mark)
    if wire:
        ink = style.make_material("PV_Wire", style.UI.INK, "flat")
        w = bpy.data.objects.new("PV_Wire", obj.data.copy())
        bpy.context.scene.collection.objects.link(w)
        w.data.materials.clear()
        w.data.materials.append(ink)
        mod = w.modifiers.new("Wire", "WIREFRAME")
        mod.thickness = 0.0012
        mod.use_replace = True
        helpers.append(w)

    def cleanup():
        for h in helpers:
            bpy.data.objects.remove(h, do_unlink=True)
    return cleanup


def main():
    args = pb.parse_args(ASSET, "Characters")
    pb.reset_scene()
    obj, info = build()
    tris = pb.triangle_count([obj])
    wr = Vector(info["bone_guides_m"]["hand_r"]["head"])
    views = [
        {"name": "fp", "location": (0, 0, 0), "target": (1, 0, 0), "lens": pb.lens_for_hfov(90.0),
         "resolution": (1280, 720), "world_rgb": style.linear(style.TROPICAL.SKY_DAY), "view_transform": "Standard",
         "setup": lambda: stage_water()},
        {"name": "top", "location": (0.25, 0.0, 1.0), "rotation": (0.0, 0.0, math.radians(-90.0)), "ortho_scale": 1.0,
         "setup": lambda: stage_markers(obj)},
        {"name": "side", "location": (0.25, -2.0, -0.2), "target": (0.25, 0.0, -0.2), "ortho_scale": 1.0,
         "setup": lambda: stage_markers(obj, wire=True)},
        {"name": "hand", "location": tuple(wr + Vector((0.16, -0.30, 0.22))), "target": tuple(wr + Vector((0.09, 0.01, 0.0))),
         "lens": 50.0, "setup": lambda: stage_markers(obj, wire=True, eye=False)},
        {"name": "palm", "location": tuple(wr + Vector((0.30, 0.10, -0.25))), "target": tuple(wr + Vector((0.09, 0.01, 0.0))),
         "lens": 50.0, "setup": lambda: stage_markers(obj, eye=False)},
    ]
    info.update({"budget_ok": tris <= BUDGET, "budget": BUDGET, "topology": mk.mesh_stats(obj)})
    pb.finish(args, [obj], views=views, extra=info)


if __name__ == "__main__":
    main()

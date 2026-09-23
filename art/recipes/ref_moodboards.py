"""Mood-board reference renders for Lure (T-002). Not a game asset: nothing is exported to Unreal.

Builds four tiny low-poly vignettes with art/lib/style.py and renders them with EEVEE at 1280x720:
    art/reference/moodboard_a_tropical_day.png    tropical dock at day (turquoise water, sand, palm, dock, red bobber)
    art/reference/moodboard_b_tropical_dusk.png   the same dock at dusk, lantern lit, a big dark shadow in the water
    art/reference/moodboard_c_foggy_stacks.png    foggy / eerie sea stacks, lantern buoy, a wrong-colored light
    art/reference/moodboard_d_palette.png         palette swatch sheet (exact sRGB swatches, Standard view transform)
plus a 2x2 contact sheet as the runner's preview (Saved/AgentLogs/previews/ref_moodboards.png).

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/ref_moodboards.py
How it fits the asset runner: the runner's --out is read as the OUTPUT FOLDER for the reference PNGs (default
art/reference/), --preview is the contact sheet, and RESULT_JSON lists every image with its scene triangle count.
Deterministic: all scatter uses fixed seeds, so rerunning reproduces the same images (up to GPU sampling noise).
"""
import argparse
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import bpy  # noqa: E402
import json  # noqa: E402
from mathutils import Vector  # noqa: E402

import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402
from style import TROPICAL as T, FOGGY as F  # noqa: E402

ASSET = "ref_moodboards"
WIDTH, HEIGHT = 1280, 720


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--out", default=str(pb.REPO_ROOT / "art" / "reference"))
    p.add_argument("--preview", default=str(pb.PREVIEW_ROOT / (ASSET + ".png")))
    p.add_argument("--save-blend", action="store_true")
    a = p.parse_args(argv)
    out = Path(a.out)
    a.out_dir = out.parent if out.suffix else out  # tolerate a file path passed as --out
    return a


# ---------------------------------------------------------------------------------------------------
# Scene plumbing
# ---------------------------------------------------------------------------------------------------
def new_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    pb.reset_scene()
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = WIDTH
    scene.render.resolution_y = HEIGHT
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    ee = scene.eevee
    for attr, val in (("taa_render_samples", 48), ("use_raytracing", True), ("volumetric_end", 250.0),
                      ("volumetric_tile_size", "4"), ("use_volumetric_shadows", True), ("use_shadows", True)):
        if hasattr(ee, attr):
            try:
                setattr(ee, attr, val)
            except Exception:
                pass
    set_view(scene, "AgX", "AgX - Punchy")
    return scene


def set_view(scene, transform, look="None", exposure=0.0):
    vs = scene.view_settings
    try:
        vs.view_transform = transform
    except Exception:
        vs.view_transform = "Standard"
    try:
        vs.look = look
    except Exception:
        pass
    vs.exposure = exposure


def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def mesh_obj(name, verts, faces, mat=None, location=(0, 0, 0)):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = link(bpy.data.objects.new(name, me))
    ob.location = location
    if mat:
        ob.data.materials.append(mat)
    return ob


def prim(kind, name, mat=None, location=(0, 0, 0), scale=(1, 1, 1), rotation=(0, 0, 0), **kw):
    getattr(bpy.ops.mesh, "primitive_" + kind + "_add")(location=location, rotation=rotation, **kw)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = scale
    if mat:
        ob.data.materials.append(mat)
    return ob


def apply_scale(ob):
    pb.select_only([ob])
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)


def cylinder_between(name, a, b, radius, mat, verts=8):
    a, b = Vector(a), Vector(b)
    d = b - a
    ob = prim("cylinder", name, mat, location=(a + b) / 2, vertices=verts, radius=radius, depth=d.length)
    ob.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    return ob


def jitter(ob, rng, amount, z_amount=None):
    z_amount = amount if z_amount is None else z_amount
    for v in ob.data.vertices:
        v.co.x += rng.uniform(-amount, amount)
        v.co.y += rng.uniform(-amount, amount)
        v.co.z += rng.uniform(-z_amount, z_amount)


def camera(location, target, lens=35.0):
    cam = link(bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")))
    cam.data.lens = lens
    cam.data.clip_end = 1000.0
    cam.location = location
    cam.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = cam
    return cam


def sun(rotation_deg, hex_color, strength, angle_deg=2.0):
    s = link(bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN")))
    s.data.color = style.linear(hex_color)
    s.data.energy = strength
    s.data.angle = math.radians(angle_deg)
    s.rotation_euler = [math.radians(a) for a in rotation_deg]
    return s


def point_light(name, location, hex_color, watts, radius=0.1, shadow=True):
    lt = link(bpy.data.objects.new(name, bpy.data.lights.new(name, "POINT")))
    lt.data.use_shadow = shadow
    lt.data.color = style.linear(hex_color)
    lt.data.energy = watts
    lt.data.shadow_soft_size = radius
    lt.location = location
    return lt


def gradient_sky(stops, strength=1.0):
    """World color by view elevation. stops: list of (position 0=horizon..1=zenith, rgba linear).
    The world's Generated coordinate is the view direction, so Z is sin(elevation): 0 at the horizon."""
    world = bpy.data.worlds.new("Sky")
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nodes, links = nt.nodes, nt.links
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
    return world


def water_plane(mat, size=600.0):
    ob = prim("plane", "Water", mat, size=size)
    return ob


def water_material(name, shallow_rgba, deep_rgba, near, far, roughness=0.12):
    """Render-only water: shallow color near the island (object origin), deep color further out."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nodes, links = nt.nodes, nt.links
    bsdf = nodes.get("Principled BSDF")
    bsdf.inputs["Roughness"].default_value = roughness
    coord = nodes.new("ShaderNodeTexCoord")
    length = nodes.new("ShaderNodeVectorMath")
    length.operation = "LENGTH"
    rng = nodes.new("ShaderNodeMapRange")
    rng.inputs["From Min"].default_value = near
    rng.inputs["From Max"].default_value = far
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = shallow_rgba
    ramp.color_ramp.elements[1].color = deep_rgba
    links.new(coord.outputs["Object"], length.inputs[0])
    links.new(length.outputs["Value"], rng.inputs["Value"])
    links.new(rng.outputs["Result"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    mat.diffuse_color = deep_rgba
    return mat


def fog_volume(hex_color, density, size=(400, 400, 60), anisotropy=0.3):
    mat = bpy.data.materials.new("Fog")
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.remove(nodes.get("Principled BSDF"))
    vol = nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = style.hex_to_linear_rgba(hex_color)
    vol.inputs["Density"].default_value = density
    vol.inputs["Anisotropy"].default_value = anisotropy
    links.new(vol.outputs["Volume"], nodes.get("Material Output").inputs["Volume"])
    box = prim("cube", "FogVolume", mat, location=(0, 0, size[2] / 2 - 1.0), scale=(size[0] / 2, size[1] / 2, size[2] / 2))
    return box


def render(path):
    scene = bpy.context.scene
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    return str(path)


def scene_triangles():
    return pb.triangle_count([o for o in bpy.context.scene.objects if o.type == "MESH" and o.name != "FogVolume"])


# ---------------------------------------------------------------------------------------------------
# Props (tiny low-poly vignette pieces, chunky and beveled per style.py)
# ---------------------------------------------------------------------------------------------------
def palm(name, base, height, lean_dir, rng, mats, scale=1.0):
    """Curved 8-sided trunk + drooping leaf fans + coconuts."""
    trunk_mat, leaf_mat, leaf_dark_mat, nut_mat = mats
    base = Vector(base)
    lean = Vector((lean_dir[0], lean_dir[1], 0)).normalized()
    segs = 6
    pts = []
    for i in range(segs + 1):
        t = i / segs
        pts.append(base + lean * (1.4 * scale * t * t) + Vector((0, 0, height * t)))
    for i in range(segs):
        r = (0.2 - 0.07 * i / segs) * scale
        seg = cylinder_between("%s_Trunk%d" % (name, i), pts[i], pts[i + 1] + (pts[i + 1] - pts[i]) * 0.08, r, trunk_mat, 8)
        seg.scale = (1.0, 1.0, 1.0)
    top = pts[-1]
    n_leaves = 8
    for i in range(n_leaves):
        ang = i * 2 * math.pi / n_leaves + rng.uniform(-0.2, 0.2)
        length = rng.uniform(2.2, 2.8) * scale
        droop = rng.uniform(0.8, 1.3) * scale
        verts, faces = [], []
        steps = 5
        for s in range(steps + 1):
            t = s / steps
            w = (0.06 + 0.5 * math.sin(math.pi * min(1.0, t * 1.15))) * scale * 0.7
            x = length * t
            z = 0.5 * scale * t - droop * t * t
            verts += [(x, -w, z - 0.06 * scale), (x, 0.0, z + 0.04 * scale), (x, w, z - 0.06 * scale)]
        for s in range(steps):
            a = s * 3
            faces += [(a, a + 3, a + 4, a + 1), (a + 1, a + 4, a + 5, a + 2)]
        leaf = mesh_obj("%s_Leaf%d" % (name, i), verts, faces, leaf_mat if i % 2 == 0 else leaf_dark_mat, location=top)
        leaf.rotation_euler = (0, rng.uniform(-0.15, 0.1), ang)
    for i in range(3):
        a = i * 2.1
        prim("ico_sphere", "%s_Nut%d" % (name, i), nut_mat, location=top + Vector((math.cos(a) * 0.18, math.sin(a) * 0.18, -0.2)) * 1.0,
             subdivisions=1, radius=0.14 * scale)


def dock(origin, length, rng, wood_mat, rope_mat, post_mat):
    """Planks across a 1.6 m wide walkway running along -Y from `origin`, posts every 1.75 m."""
    ox, oy, deck_z = origin
    width = 1.6
    n = int(length / 0.27)
    for i in range(n):
        y = oy - i * 0.27
        board = prim("cube", "Plank%02d" % i, wood_mat, location=(ox + rng.uniform(-0.04, 0.04), y, deck_z + rng.uniform(-0.01, 0.01)),
                     scale=(width / 2, 0.11, 0.035), rotation=(0, rng.uniform(-0.02, 0.02), rng.uniform(-0.03, 0.03)))
        apply_scale(board)
        style.add_bevel(board)
    for side in (-1, 1):
        stringer = prim("cube", "Stringer%d" % side, post_mat, location=(ox + side * 0.6, oy - length / 2, deck_z - 0.1),
                        scale=(0.07, length / 2, 0.08))
        apply_scale(stringer)
        style.add_bevel(stringer)
    k = 0
    y = oy - 0.3
    while y > oy - length - 0.1:
        for side in (-1, 1):
            px = ox + side * (width / 2 + 0.08)
            post = prim("cylinder", "Post%02d" % k, post_mat, location=(px, y, deck_z - 0.55), vertices=8, radius=0.13, depth=1.9)
            style.add_bevel(post)
            rope = prim("torus", "Rope%02d" % k, rope_mat, location=(px, y, deck_z + 0.15), major_radius=0.15, minor_radius=0.045,
                        major_segments=10, minor_segments=5)
            k += 1
        y -= 1.75
    return (ox, oy - length, deck_z)


def bobber(location, radius, red_mat, white_mat):
    top = prim("uv_sphere", "BobberTop", red_mat, location=location, segments=12, ring_count=6, radius=radius)
    bpy.ops.object.shade_smooth()
    bottom = prim("uv_sphere", "BobberBottom", white_mat, location=Vector(location) - Vector((0, 0, radius * 0.55)),
                  segments=12, ring_count=6, radius=radius * 0.8)
    bpy.ops.object.shade_smooth()
    prim("cylinder", "BobberStem", red_mat, location=Vector(location) + Vector((0, 0, radius * 1.1)), vertices=6,
         radius=radius * 0.18, depth=radius * 0.9)
    return top


def island(name, location, radii, rng, sand_mat, subdiv=3):
    ob = prim("ico_sphere", name, sand_mat, location=location, subdivisions=subdiv, radius=1.0, scale=radii)
    apply_scale(ob)
    jitter(ob, rng, 0.25 * radii[0] / 6.0, 0.08)
    bpy.ops.object.shade_smooth()
    return ob


def rock(name, location, radius, rng, mat, squash=0.7):
    ob = prim("ico_sphere", name, mat, location=location, subdivisions=1, radius=radius,
              scale=(1.0, rng.uniform(0.8, 1.1), squash), rotation=(0, 0, rng.uniform(0, 6.28)))
    apply_scale(ob)
    jitter(ob, rng, radius * 0.18)
    style.shade_flat(ob)
    return ob


# ---------------------------------------------------------------------------------------------------
# (a) + (b) Tropical dock
# ---------------------------------------------------------------------------------------------------
def build_tropical(dusk):
    rng = random.Random(1701)
    mats = {
        "sand": style.make_material("M_Sand", T.SAND, "flat", roughness=0.9),
        "wood": style.make_material("M_Wood", T.WEATHERED_WOOD, "wood", grain=True),
        "post": style.make_material("M_WoodPost", "#6E4630", "wood"),
        "rope": style.make_material("M_Rope", T.ROPE, "wood"),
        "palm": style.make_material("M_PalmGreen", T.PALM_GREEN, "flat"),
        "leaf_dark": style.make_material("M_LeafDark", T.LEAF_DARK, "flat"),
        "trunk": style.make_material("M_PalmTrunk", "#9C7A52", "wood"),
        "nut": style.make_material("M_Coconut", "#5A3B26", "flat"),
        "reef": style.make_material("M_Reef", T.REEF, "wet"),
        "rock": style.make_material("M_WetRock", "#6F7F80", "wet", roughness=0.45),
        "red": style.make_material("M_Bobber", T.ACCENT, "wet", roughness=0.3),
        "white": style.make_material("M_BobberWhite", "#F5F1E6", "wet", roughness=0.35),
        "line": style.make_material("M_Line", "#F5F1E6", "flat", emission=0.3 if dusk else 0.0),
        "rod": style.make_material("M_Rod", "#3A2A20", "wet"),
    }
    if dusk:
        water = water_material("M_Water", style.mix_hex(T.SHALLOW_WATER, T.NIGHT, 0.45), style.mix_hex(T.DEEP_WATER, T.NIGHT, 0.5),
                               7.0, 22.0, roughness=0.08)
    else:
        water = water_material("M_Water", style.hex_to_linear_rgba(T.SHALLOW_WATER), style.hex_to_linear_rgba(T.DEEP_WATER), 7.0, 26.0)
    water_plane(water)

    island("Island", (0, 0, -0.55), (7.0, 6.0, 1.3), rng, mats["sand"])
    for i, (x, y) in enumerate(((-1.5, 1.2), (1.8, 2.5))):
        palm("Palm%d" % i, (x, y, 0.55), 5.2 - i * 0.9, (1.0 if i == 0 else -0.4, -0.5 if i == 0 else 0.6), rng,
             (mats["trunk"], mats["palm"], mats["leaf_dark"], mats["nut"]), scale=1.0 - i * 0.15)
    for i in range(4):  # bushes
        a = 0.8 + i * 1.3
        prim("ico_sphere", "Bush%d" % i, mats["leaf_dark"] if i % 2 else mats["palm"],
             location=(math.cos(a) * 3.0 - 1.0, math.sin(a) * 2.5 + 1.0, 0.45), subdivisions=1, radius=0.55 + 0.1 * i,
             scale=(1.0, 1.0, 0.7))
        style.shade_flat(bpy.context.active_object)
    for i in range(7):  # reef lumps breaking the surface around the shallows
        a = rng.uniform(0, 6.28)
        d = rng.uniform(7.8, 10.5)
        rock("Reef%d" % i, (math.cos(a) * d, math.sin(a) * d * 0.9, -0.15), rng.uniform(0.3, 0.6), rng, mats["reef"], squash=0.6)
    for i, (x, y, r) in enumerate(((-9.0, -5.0, 0.9), (-7.5, -6.5, 0.5), (11.0, 3.0, 1.1))):
        rock("Rock%d" % i, (x, y, 0.0), r, rng, mats["rock"])
    # distant islands on the horizon
    for i, (x, y, rx) in enumerate(((-70.0, 95.0, 14.0), (55.0, 130.0, 20.0), (-150.0, 60.0, 10.0))):
        island("FarIsland%d" % i, (x, y, -1.2), (rx, rx * 0.6, 2.8), rng, mats["sand"], subdiv=2)
        palm("FarPalm%d" % i, (x + 2, y, 1.2), 5.5, (1, 0.2), rng, (mats["trunk"], mats["palm"], mats["leaf_dark"], mats["nut"]))

    end = dock((2.4, -4.2, 0.62), 8.0, rng, mats["wood"], mats["rope"], mats["post"])
    # rod leaning on the dock end, line out to the red bobber
    rod_base = Vector((end[0] + 0.5, end[1] + 0.6, end[2] + 0.05))
    rod_tip = Vector((end[0] + 1.9, end[1] - 0.8, end[2] + 2.4))
    cylinder_between("Rod", rod_base, rod_tip, 0.025, mats["rod"], 6)
    bob = Vector((end[0] + 4.2, end[1] - 1.2, 0.08))
    cylinder_between("Line", rod_tip, bob + Vector((0, 0, 0.3)), 0.006, mats["line"], 4)
    bobber(bob, 0.2, mats["red"], mats["white"])
    # tackle crate on the dock
    crate = prim("cube", "Crate", mats["wood"], location=(end[0] - 0.35, end[1] + 1.3, end[2] + 0.3), scale=(0.3, 0.25, 0.25),
                 rotation=(0, 0, 0.3))
    apply_scale(crate)
    style.add_bevel(crate)

    if dusk:
        gradient_sky([(0.0, style.hex_to_linear_rgba(T.SUNSET)), (0.025, style.mix_hex(T.SUNSET, T.NIGHT, 0.5)),
                      (0.09, style.hex_to_linear_rgba(T.NIGHT)), (1.0, style.mix_hex(T.NIGHT, "#000000", 0.5))], strength=0.8)
        sun((86.0, 0, -70), T.SUNSET, 1.6, angle_deg=4.0)
        sun((35.0, 0, 150), "#9FB4E0", 0.35, angle_deg=1.0)  # cool moon fill so the water is not pure black
        lantern_mat = style.make_material("M_Lantern", "#FFB45A", "emissive", emission=2.0)
        post_top = Vector((end[0] - 0.9, end[1] + 0.3, end[2] + 1.0))
        cylinder_between("LanternPole", post_top - Vector((0, 0, 1.6)), post_top, 0.06, mats["post"], 6)
        prim("cube", "Lantern", lantern_mat, location=post_top + Vector((0, 0, 0.12)), scale=(0.1, 0.1, 0.14))
        point_light("LanternLight", post_top + Vector((0, 0, 0.12)), "#FFB35A", 120.0, 0.1, shadow=False)  # inside the lantern
        # the shadow: a huge dark body just under the surface at the drop-off, at the edge of the frame
        shadow_mat = bpy.data.materials.new("M_DeepShadow")
        shadow_mat.use_nodes = True
        b = shadow_mat.node_tree.nodes.get("Principled BSDF")
        b.inputs["Base Color"].default_value = style.hex_to_linear_rgba("#000000")
        b.inputs["Roughness"].default_value = 1.0
        if "Specular IOR Level" in b.inputs:
            b.inputs["Specular IOR Level"].default_value = 0.0
        b.inputs["Alpha"].default_value = 0.95
        if hasattr(shadow_mat, "surface_render_method"):
            shadow_mat.surface_render_method = "BLENDED"
        body_c = Vector((11.0, -13.0, 0.01))
        body = prim("uv_sphere", "ShadowBody", shadow_mat, location=body_c, segments=16, ring_count=8, radius=1.0,
                    scale=(5.5, 1.9, 0.01), rotation=(0, 0, math.radians(-25)))
        tail = mesh_obj("ShadowTail", [(0, 0, 0), (3.0, 1.9, 0), (2.4, 0, 0), (3.0, -1.9, 0)], [(0, 1, 2), (0, 2, 3)], shadow_mat,
                        location=body_c + Vector((4.6, -2.1, 0.0)))
        tail.rotation_euler = (0, 0, math.radians(-25))
        for s in (-1, 1):  # pectoral fins
            fin = mesh_obj("ShadowFin%d" % s, [(0, 0, 0), (-0.8, s * 2.6, 0), (1.3, s * 0.3, 0)], [(0, 1, 2)], shadow_mat,
                           location=body_c + Vector((-1.2, 0.0, 0.0)))
            fin.rotation_euler = (0, 0, math.radians(-25))
        set_view(bpy.context.scene, "AgX", "AgX - Punchy", exposure=0.3)
    else:
        gradient_sky([(0.0, style.mix_hex(T.SKY_DAY, "#FFFFFF", 0.5)), (0.12, style.hex_to_linear_rgba(T.SKY_DAY)),
                      (1.0, style.mix_hex(T.SKY_DAY, "#1F6FA8", 0.5))], strength=1.0)
        set_view(bpy.context.scene, "Standard", "None", exposure=-0.2)
        sun((42, 0, 35), "#FFF1D6", 4.0)

    camera((14.0, -21.0, 4.2), (1.5, -4.5, 0.6), lens=32.0)


# ---------------------------------------------------------------------------------------------------
# (c) Foggy / eerie sea stacks
# ---------------------------------------------------------------------------------------------------
def sea_stack(name, location, radius, height, rng, mat):
    ob = prim("cylinder", name, mat, location=(location[0], location[1], height / 2 - 1.0), vertices=7, radius=radius, depth=height)
    me = ob.data
    for v in me.vertices:
        t = (v.co.z + height / 2) / height
        taper = 1.0 - 0.35 * t
        v.co.x *= taper * rng.uniform(0.85, 1.15)
        v.co.y *= taper * rng.uniform(0.85, 1.15)
        v.co.z += rng.uniform(-0.1, 0.1) * height * (1 if t > 0.5 else 0)
    # a few horizontal cuts so the column breaks into chunky faceted ledges
    pb.select_only([ob])
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.subdivide(number_cuts=3)
    bpy.ops.object.mode_set(mode="OBJECT")
    for v in me.vertices:
        if -height / 2 + 0.01 < v.co.z < height / 2 - 0.01:
            k = 1.0 + rng.uniform(-0.12, 0.12)
            v.co.x *= k
            v.co.y *= k
    ob.rotation_euler = (rng.uniform(-0.04, 0.04), rng.uniform(-0.04, 0.04), rng.uniform(0, 6.28))
    style.shade_flat(ob)
    return ob


def build_foggy():
    rng = random.Random(4242)
    rock_mat = style.make_material("M_Rock", F.ROCK, "flat", roughness=0.8)
    wet_rock = style.make_material("M_WetRockDark", "#33403F", "wet", roughness=0.45)
    water = water_material("M_Water", style.hex_to_linear_rgba(F.WATER), style.mix_hex(F.WATER, "#000000", 0.35), 10.0, 70.0,
                           roughness=0.1)
    water_plane(water)
    stacks = [(-9.0, 28.0, 3.2, 17.0), (6.0, 40.0, 4.5, 24.0), (-24.0, 55.0, 5.0, 28.0), (22.0, 70.0, 6.0, 32.0),
              (-3.0, 85.0, 5.5, 22.0), (38.0, 45.0, 3.5, 14.0), (-40.0, 30.0, 3.0, 12.0), (14.0, 22.0, 1.6, 6.0)]
    for i, (x, y, r, h) in enumerate(stacks):
        sea_stack("Stack%d" % i, (x, y), r, h, rng, rock_mat)
        for j in range(3):
            a = rng.uniform(0, 6.28)
            rock("StackRock%d_%d" % (i, j), (x + math.cos(a) * r * 1.1, y + math.sin(a) * r * 1.1, -0.1), r * rng.uniform(0.25, 0.45),
                 rng, wet_rock, squash=0.6)
    # foreground: a leaning wooden marker post with a lantern, a few wet rocks
    wood = style.make_material("M_Wood", "#5C4332", "wood")
    lantern = style.make_material("M_Lantern", F.LANTERN, "emissive", emission=2.5)
    base = Vector((2.8, 8.0, -0.3))
    tip = base + Vector((-0.3, 0.2, 3.0))
    cylinder_between("MarkerPost", base, tip, 0.12, wood, 7)
    cylinder_between("MarkerArm", tip - Vector((0, 0, 0.2)), tip + Vector((0.7, 0, -0.1)), 0.05, wood, 6)
    prim("cube", "Lantern", lantern, location=tip + Vector((0.7, 0, -0.45)), scale=(0.13, 0.13, 0.17))
    point_light("LanternLight", tip + Vector((0.7, 0, -0.45)), F.LANTERN, 400.0, 0.15, shadow=False)  # inside the lantern
    for i in range(4):
        rock("NearRock%d" % i, (base.x + rng.uniform(-2, 2.5), base.y + rng.uniform(-1, 2), -0.2), rng.uniform(0.4, 0.9), rng, wet_rock)
    # a small rowboat silhouette mid-distance for scale
    hull_mat = style.make_material("M_Hull", "#3A2E27", "wood")
    hull = prim("cube", "Rowboat", hull_mat, location=(-6.0, 20.0, 0.1), scale=(1.8, 0.6, 0.3), rotation=(0, 0, 0.5))
    for v in hull.data.vertices:
        if v.co.z < 0:
            v.co.y *= 0.5
        if abs(v.co.x) > 0.9:
            v.co.y *= 0.4
            v.co.z += 0.15
    # the wrong light: a sickly green glow low at the foot of a far stack
    wrong = style.make_material("M_WrongLight", F.WRONG_LIGHT, "emissive", emission=40.0)
    prim("uv_sphere", "WrongLight", wrong, location=(-19.0, 50.0, 1.4), segments=8, ring_count=4, radius=0.35)
    point_light("WrongGlow", (-19.0, 49.0, 1.6), F.WRONG_LIGHT, 6000.0, 0.3)

    fog_volume(F.FOG, 0.018)
    gradient_sky([(0.0, style.hex_to_linear_rgba(F.FOG)), (1.0, style.mix_hex(F.FOG, F.ROCK, 0.6))], strength=0.6)
    sun((60, 0, 160), "#DDE6E6", 1.2, angle_deg=15.0)
    camera((0.0, -2.0, 2.2), (0.0, 40.0, 5.0), lens=28.0)
    set_view(bpy.context.scene, "AgX", "AgX - Punchy", exposure=0.4)


# ---------------------------------------------------------------------------------------------------
# (d) Palette sheet
# ---------------------------------------------------------------------------------------------------
def emission_material(name, hex_color):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.remove(nodes.get("Principled BSDF"))
    em = nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = style.hex_to_linear_rgba(hex_color)
    em.inputs["Strength"].default_value = 1.0
    links.new(em.outputs["Emission"], nodes.get("Material Output").inputs["Surface"])
    return mat


def text(body, location, size, mat, align="LEFT"):
    cu = bpy.data.curves.new("Txt", "FONT")
    cu.body = body
    cu.size = size
    cu.align_x = align
    ob = link(bpy.data.objects.new("Txt", cu))
    ob.location = location
    ob.data.materials.append(mat)
    return ob


def build_palette():
    scene = bpy.context.scene
    set_view(scene, "Standard", "None")  # exact sRGB swatches
    ink = emission_material("Ink", style.UI.INK)
    prim("plane", "Paper", emission_material("Paper", style.UI.PARCHMENT), location=(0, 0, -0.01), size=40)
    text("LURE  palette v1  (docs/ART_STYLE.md)", (-8.2, 4.05, 0), 0.42, ink)
    rows = [("Tropical", "tropical"), ("Foggy", "foggy"), ("Frozen", "frozen"), ("Murky", "murky"), ("UI", "ui")]
    y = 2.75
    for label, key in rows:
        text(label, (-8.2, y - 0.1, 0), 0.34, ink)
        x = -6.0
        for name, hx in style.palette(key):
            prim("plane", "SwEdge_" + name, ink, location=(x + 0.5, y, -0.005), size=1.04)
            prim("plane", "Sw_" + name, emission_material("Sw_" + key + name, hx), location=(x + 0.5, y, 0), size=1.0)
            text(name.lower().replace("_", " "), (x + 0.5, y - 0.72, 0), 0.13, ink, "CENTER")
            text(hx, (x + 0.5, y - 0.9, 0), 0.13, ink, "CENTER")
            x += 1.2
        y -= 1.62
    cam = link(bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = 17.6
    cam.location = (0.0, -0.05, 10.0)
    scene.camera = cam
    w = bpy.data.worlds.new("W")
    scene.world = w


# ---------------------------------------------------------------------------------------------------
def contact_sheet(paths, out):
    """2x2 grid of the renders at half size (the runner's preview image)."""
    import numpy as np
    hw, hh = WIDTH // 2, HEIGHT // 2
    sheet = np.zeros((HEIGHT, WIDTH, 4), dtype=np.float32)
    for i, p in enumerate(paths[:4]):
        img = bpy.data.images.load(p)
        img.scale(hw, hh)
        px = np.empty(hw * hh * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        px = px.reshape(hh, hw, 4)
        col, row = i % 2, 1 - i // 2  # pixel rows start at the bottom
        sheet[row * hh:(row + 1) * hh, col * hw:(col + 1) * hw] = px
        bpy.data.images.remove(img)
    out_img = bpy.data.images.new("ContactSheet", WIDTH, HEIGHT, alpha=False)
    out_img.pixels.foreach_set(sheet.ravel())
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    out_img.filepath_raw = str(out)
    out_img.file_format = "PNG"
    out_img.save()
    return str(out)


def main():
    args = parse_args()
    out_dir = Path(args.out_dir)
    jobs = [
        ("moodboard_a_tropical_day.png", lambda: build_tropical(False)),
        ("moodboard_b_tropical_dusk.png", lambda: build_tropical(True)),
        ("moodboard_c_foggy_stacks.png", build_foggy),
        ("moodboard_d_palette.png", build_palette),
    ]
    images, tris = [], {}
    for fname, build in jobs:
        new_scene()
        build()
        path = render(out_dir / fname)
        images.append(path)
        tris[fname] = scene_triangles()
        if args.save_blend:
            pb.BLEND_ROOT.mkdir(parents=True, exist_ok=True)
            bpy.ops.wm.save_as_mainfile(filepath=str(pb.BLEND_ROOT / (Path(fname).stem + ".blend")))
    preview = contact_sheet(images, args.preview)
    print("RESULT_JSON:" + json.dumps({
        "asset": ASSET,
        "category": "Reference",
        "export": None,
        "images": images,
        "preview": preview,
        "resolution": [WIDTH, HEIGHT],
        "scene_triangles": tris,
        "blender": bpy.app.version_string,
    }))


main()

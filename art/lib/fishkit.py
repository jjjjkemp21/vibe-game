"""Shared builder for Lure fish meshes (art/recipes/sm_fish_*.py): a faceted low-poly body lofted along X, plus
fins, eyes, sockets and preview staging. Every species is a parameter set on top of this kit, so all fish share
one axis / pivot / topology convention and one spine rig (T-008: animation-artist, parameterized swim).

CONVENTION (every fish built with this kit):
- Forward = Blender +X (= Unreal +X). Left = Blender +Y (= Unreal -Y). Up = +Z. Mirror-symmetric about Y = 0.
- Origin (pivot) = BODY CENTER: halfway between the nose tip and the tail-fin tips along X, on the spine line Z = 0,
  Y = 0. The spine line (the centers of the body rings) stays within a few mm of Z = 0.
- Size: at scale 1 the mesh is the species' ReferenceWeight size. Recipes author the fish at any design size; build()
  reads ReferenceWeight from data/tables/DT_FishSpecies.json and scales the whole fish uniformly (about the origin)
  so the closed body volume x 1.05 kg/l equals ReferenceWeight (reference_scale). Suggested game rule: scale the
  mesh uniformly by (Weight / ReferenceWeight) ^ (1/3) (length grows with the cube root of weight).
- Body: ONE closed loft of RING_SIDES-sided rings perpendicular to X (a vertex on the dorsal and the ventral
  midline), fan poles at the nose and at the peduncle end. Every ring is an edge loop where the spine can bend;
  rings are spaced evenly along the rear 2/3 of the body where the swim wave lives.
- Fins and eyes are separate closed islands overlapping the body (quads + fan tips). Long median fins (dorsal,
  anal) have a cross-section at every body ring they span, so they bend with the body. The caudal fin has a
  chord-wise edge loop (the dark/bright trailing-edge band) so it can flex too.
- Vertex groups (whole parts, weight 1.0; the FBX carries no weights, so rig on build()): Body, Eye_L, Eye_R,
  Fin_Dorsal, Fin_Caudal, Fin_Anal, Fin_Pectoral_L, Fin_Pectoral_R, Fin_Pelvic_L, Fin_Pelvic_R.
- Socket: SOCKET_Mouth at the nose tip (hook / line attach point).
- Shading: flat (faceted), no bevel modifier (the facets are the style; fins are chunky plates).
- Materials: slot order BACK, FLANK, BELLY, FIN, EDGE, EYE (species names M_<Species>_*, shared M_Fish_Eye).
"""
import json
import math

import bmesh  # noqa: F401  (kept for recipes that extend the kit)
import bpy
from mathutils import Matrix, Vector

import meshkit as mk
import pipeline_blender as pb
import style

RING_SIDES = 12
PARTS = ["Body", "Eye_L", "Eye_R", "Fin_Dorsal", "Fin_Caudal", "Fin_Anal", "Fin_Pectoral_L", "Fin_Pectoral_R",
         "Fin_Pelvic_L", "Fin_Pelvic_R"]
BACK, FLANK, BELLY, FIN, EDGE, EYE = range(6)
EYE_HEX = "#2B2A26"          # UI ink: pupils and silhouettes
WATER_DENSITY_KG_L = 1.05    # for the implied-weight check (fish are ~ seawater density)


def _sgnpow(x, p):
    return math.copysign(abs(x) ** p, x)


# ---------------------------------------------------------------------------------------------------
# Body
# ---------------------------------------------------------------------------------------------------
class Body:
    """Body profile. rows: [(s, top, bot, half_w, zc)] with s = 0 at the nose tip and s = 1 at the peduncle end
    (the tail fin root); top/bot = half-height above/below the ring center zc; half_w = half-width (Y).
    x_nose: X of the nose tip; length: nose tip to peduncle end (m); nose_z: Z of the nose tip (the mouth).
    gill: optional (s, step): a gill-cover edge made of two close rings, the front one `step` proud."""

    def __init__(self, rows, x_nose, length, nose_z=0.0, gill=None, sides=RING_SIDES, exponent=2.0, tail_cap=0.006):
        self.rows = sorted(rows)
        self.x_nose, self.length, self.nose_z = x_nose, length, nose_z
        self.gill, self.sides, self.exp, self.tail_cap = gill, sides, exponent, tail_cap
        self.ring_s = []

    def x(self, s):
        return self.x_nose - s * self.length

    def at(self, s):
        """(top, bot, half_w, zc) linearly interpolated at s (the nose tip is a point at nose_z)."""
        pts = [(0.0, 0.0, 0.0, 0.0, self.nose_z)] + self.rows
        s = max(0.0, min(pts[-1][0], s))
        for a, b in zip(pts, pts[1:]):
            if a[0] <= s <= b[0]:
                t = 0.0 if b[0] == a[0] else (s - a[0]) / (b[0] - a[0])
                return tuple(a[i] + (b[i] - a[i]) * t for i in range(1, 5))
        return pts[-1][1:]

    def top_z(self, s):
        top, _bot, _hw, zc = self.at(s)
        return zc + top

    def bot_z(self, s):
        _top, bot, _hw, zc = self.at(s)
        return zc - bot

    def surface(self, s, a, scale=1.0):
        """Point on the (smooth) body at s and ring angle a (0 = dorsal midline, pi/2 = left side +Y, pi = belly)."""
        top, bot, hw, zc = self.at(s)
        c, sn = math.cos(a), math.sin(a)
        p = 2.0 / self.exp
        r = top if c >= 0.0 else bot
        return Vector((self.x(s), hw * scale * _sgnpow(sn, p), zc + r * scale * _sgnpow(c, p)))

    def build(self, mb, sector_mat, group="Body"):
        """Loft the body into MeshBuilder mb. sector_mat(s, a_center) -> material index for a face whose ring
        sector is centered on angle a_center. Returns the ring s values (the body edge loops)."""
        n = self.sides
        specs = [(r[0], 1.0) for r in self.rows]
        if self.gill:
            gs, step = self.gill
            specs += [(gs, 1.0 + step), (gs + 0.012, 1.0 - step * 0.5)]
            specs = [sp for sp in specs if not (abs(sp[0] - gs) < 0.011 and sp[1] == 1.0)]
        specs.sort()
        self.ring_s = [s for s, _k in specs]
        bm = mb.bm
        rings = []
        for s, k in specs:
            vs = [bm.verts.new(self.surface(s, 2.0 * math.pi * j / n, k)) for j in range(n)]
            mb.set_weights(vs, {group: 1.0})
            rings.append(vs)
        mids = [(a + b) * 0.5 for a, b in zip([0.0] + self.ring_s, self.ring_s + [1.0])]
        for i in range(len(rings) - 1):
            a, b = rings[i], rings[i + 1]
            for j in range(n):
                f = bm.faces.new((a[j], a[(j + 1) % n], b[(j + 1) % n], b[j]))
                f.material_index = sector_mat(mids[i + 1], 2.0 * math.pi * (j + 0.5) / n)
        nose = bm.verts.new(Vector((self.x_nose, 0.0, self.nose_z)))
        _t, _b, _h, zc_end = self.at(1.0)
        tail = bm.verts.new(Vector((self.x(1.0) - self.tail_cap, 0.0, zc_end)))
        mb.set_weights([nose, tail], {group: 1.0})
        for j in range(n):
            ac = 2.0 * math.pi * (j + 0.5) / n
            f = bm.faces.new((nose, rings[0][(j + 1) % n], rings[0][j]))
            f.material_index = sector_mat(mids[0], ac)
            f = bm.faces.new((tail, rings[-1][j], rings[-1][(j + 1) % n]))
            f.material_index = sector_mat(1.0, ac)
        return self.ring_s


def countershade(back_cos=0.55, belly_cos=-0.55):
    """Default body coloring: BACK where the sector faces up (cos(angle) > back_cos), BELLY where it faces down,
    FLANK in between."""
    def fn(_s, a):
        c = math.cos(a)
        return BACK if c > back_cos else (BELLY if c < belly_cos else FLANK)
    return fn


# ---------------------------------------------------------------------------------------------------
# Fins
# ---------------------------------------------------------------------------------------------------
def fin_loft(mb, stations, normal, group, mat=FIN, t_root=0.004, t_edge=0.0015, edge_mat=None, edge_frac=0.7,
             start="flat", end="flat"):
    """A chunky fin plate. stations: [(A, B)] with A the root / leading point and B the tip / trailing point;
    consecutive stations are joined by quads (one cross-section per station = one edge loop). normal: the fin
    plane's normal (thickness direction). Thickness is t_root at A and t_edge at B (half-thickness each side).
    edge_mat: a second color on the B side of the plate, from edge_frac of the chord to B (an extra edge loop).
    start/end: "flat" (quad/hexagon cap) or a point (fan to a sharp tip)."""
    bm = mb.bm
    nrm = Vector(normal).normalized()
    rings, mats = [], None
    for A, B in stations:
        A, B = Vector(A), Vector(B)
        if edge_mat is None:
            pts = [A - nrm * t_root, B - nrm * t_edge, B + nrm * t_edge, A + nrm * t_root]
            mats = [mat, mat, mat, mat]
        else:
            M = A.lerp(B, edge_frac)
            tm = t_root + (t_edge - t_root) * edge_frac
            pts = [A - nrm * t_root, M - nrm * tm, B - nrm * t_edge, B + nrm * t_edge, M + nrm * tm, A + nrm * t_root]
            mats = [mat, edge_mat, edge_mat, edge_mat, mat, mat]
        vs = [bm.verts.new(p) for p in pts]
        mb.set_weights(vs, {group: 1.0})
        rings.append(vs)
    n = len(rings[0])
    for i in range(len(rings) - 1):
        a, b = rings[i], rings[i + 1]
        for k in range(n):
            f = bm.faces.new((a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]))
            f.material_index = mats[k]
    for cap, vs, first in ((start, rings[0], True), (end, rings[-1], False)):
        if isinstance(cap, str):
            f = bm.faces.new(list(reversed(vs)) if first else vs)
            f.material_index = mat
        else:
            pole = bm.verts.new(Vector(cap))
            mb.set_weights([pole], {group: 1.0})
            for k in range(n):
                tri = (pole, vs[(k + 1) % n], vs[k]) if first else (pole, vs[k], vs[(k + 1) % n])
                f = bm.faces.new(tri)
                f.material_index = mats[k]
    return rings


def median_stations(body, spec, top=True, inset=0.004):
    """Stations for a dorsal (top=True) or anal/ventral median fin: spec = [(s, height, lean)], the fin root sits
    `inset` inside the body midline at s, the fin edge is `height` out and `lean` further back (-X)."""
    out = []
    for s, h, lean in spec:
        if top:
            z0 = body.top_z(s)
            out.append((Vector((body.x(s), 0.0, z0 - inset)), Vector((body.x(s) - lean, 0.0, z0 + h))))
        else:
            z0 = body.bot_z(s)
            out.append((Vector((body.x(s), 0.0, z0 + inset)), Vector((body.x(s) - lean, 0.0, z0 - h))))
    return out


def caudal_stations(x_root, z0, root_half, span_half, length, fork, lobe_curve=1.0, levels=(0.8, 0.52)):
    """Tail fin from the upper lobe tip to the lower lobe tip. The leading edge runs straight up/down the peduncle
    (+-root_half) and then sweeps back to the lobe tips at (x_root - length, z0 +- span_half). The trailing edge is
    a V from the fork notch at x_root - length * (1 - fork) to the tips (fork = 0: square tail, 0.6: deep fork).
    Returns (stations top to bottom, upper tip, lower tip)."""
    tr = root_half / span_half
    ups = sorted(set([t for t in levels if t > tr + 0.02] + [tr]), reverse=True)
    ts = ups + [0.0] + [-t for t in reversed(ups)]
    st = []
    for t in ts:
        u = abs(t)
        lead = x_root if u <= tr else x_root - length * ((u - tr) / (1.0 - tr)) ** lobe_curve
        trail = x_root - length * (1.0 - fork) - length * fork * u
        z = z0 + t * span_half
        st.append((Vector((lead, 0.0, z)), Vector((trail, 0.0, z))))
    tip_hi = Vector((x_root - length, 0.0, z0 + span_half))
    tip_lo = Vector((x_root - length, 0.0, z0 - span_half))
    return st, tip_hi, tip_lo


def paired_fin(mb, root_a, root_b, direction, length, group, mat=FIN, chords=(1.0, 0.8, 0.5), fracs=(0.0, 0.4, 0.75),
               droop=0.0, t_root=0.003, t_edge=0.0012, edge_mat=None, edge_frac=0.7):
    """A paired (pectoral / pelvic) fin: root chord A-B on the body, the fin grows `length` along `direction` and
    ends in a tip. chords: chord scale per station; fracs: station position along the length. droop bends the fin
    edge down (Z) towards the tip. The plane normal is direction x chord."""
    ra, rb, d = Vector(root_a), Vector(root_b), Vector(direction).normalized()
    mid, half = (ra + rb) * 0.5, (ra - rb) * 0.5
    stations = []
    for c, f in zip(chords, fracs):
        ctr = mid + d * (length * f) + Vector((0.0, 0.0, -droop * f * f))
        stations.append((ctr + half * c, ctr - half * c))
    tip = mid + d * length + Vector((0.0, 0.0, -droop))
    normal = d.cross((ra - rb).normalized())
    return fin_loft(mb, stations, normal, group, mat=mat, t_root=t_root, t_edge=t_edge, edge_mat=edge_mat,
                    edge_frac=edge_frac, start="flat", end=tip)


def mirror_y(p):
    return Vector((p[0], -p[1], p[2]))


def eye(mb, center, axis, radius, group, white=BELLY, pupil=EYE, n=8):
    """Stylized eye: a low dome (white ring + dark pupil) grown out of the body along `axis`."""
    r = radius
    profile = [(-0.35 * r, r, white), (0.22 * r, 0.93 * r, white), (0.3 * r, 0.56 * r, pupil), (0.42 * r, 0.0, pupil)]
    return mb.lathe(profile, origin=center, axis=axis, ref_up=(0.0, 0.0, 1.0), n=n, w={group: 1.0})


def eye_pair(mb, body, s, a, radius, white=BELLY, pupil=EYE, sink=0.0):
    """Both eyes on the head at body station s, ring angle a (from the dorsal midline)."""
    p = body.surface(s, a)
    axis = Vector((0.0, math.sin(a), math.cos(a) * 0.35)).normalized()
    p = p - axis * sink
    eye(mb, p, axis, radius, "Eye_L", white, pupil)
    eye(mb, mirror_y(p), mirror_y(axis), radius, "Eye_R", white, pupil)


# ---------------------------------------------------------------------------------------------------
# Object, stats, handoff info
# ---------------------------------------------------------------------------------------------------
def materials(prefix, back, flank, belly, fin, edge, wet=0.3):
    """Slot list in kit order (BACK, FLANK, BELLY, FIN, EDGE, EYE). Fish are wet: roughness 0.25-0.45."""
    return [
        style.make_material("M_%s_Back" % prefix, back, "wet", roughness=wet + 0.05),
        style.make_material("M_%s_Flank" % prefix, flank, "wet", roughness=wet),
        style.make_material("M_%s_Belly" % prefix, belly, "wet", roughness=wet + 0.05),
        style.make_material("M_%s_Fin" % prefix, fin, "wet", roughness=0.4),
        style.make_material("M_%s_FinEdge" % prefix, edge, "wet", roughness=0.4),
        style.make_material("M_Fish_Eye", EYE_HEX, "wet", roughness=0.25),
    ]


def body_volume_l(body):
    """Volume of the closed body loft alone (liters), from a throwaway builder."""
    mb = mk.MeshBuilder(groups=["Body"])
    body.build(mb, lambda s, a: 0)
    vol = mb.bm.calc_volume(signed=False) * 1000.0
    mb.bm.free()
    return vol


SPECIES_TABLE = pb.REPO_ROOT / "data" / "tables" / "DT_FishSpecies.json"


def species_row(name):
    """The species' row from data/tables/DT_FishSpecies.json (the source of truth for weights). Raises if missing."""
    rows = json.loads(SPECIES_TABLE.read_text(encoding="utf-8"))
    for r in rows:
        if r.get("Name") == name:
            return r
    raise KeyError("No row %r in %s" % (name, SPECIES_TABLE))


def reference_scale(body, ref_weight_kg):
    """Uniform scale that makes the authored body weigh ref_weight_kg (volume x WATER_DENSITY_KG_L)."""
    return round((ref_weight_kg / (body_volume_l(body) * WATER_DENSITY_KG_L)) ** (1.0 / 3.0), 4)


def finish_object(mb, name, mats, mouth, scale=1.0):
    """Scale the built fish uniformly about the origin (reference_scale), write the object, UVs, SOCKET_Mouth."""
    if scale != 1.0:
        mb.transform(Matrix.Scale(scale, 4))
    obj = mb.to_object(name, mats, smooth=False)
    mk.smart_uv(obj)
    pb.add_socket(obj, "Mouth", tuple(Vector(mouth) * scale))
    return obj


def spine_info(body, obj, length, ref_weight, joints_s, tail_tip_x, scale=1.0, weight_range=None):
    """Handoff data for the animation-artist: edge loops, a suggested bone chain on the spine line, volume. All
    positions are final (scaled) meters; `length` and `tail_tip_x` are in design units like the body."""
    k = scale

    def r4(v):
        return round(v * k, 4)

    def sp(s):
        return [r4(body.x(s)), 0.0, r4(body.at(s)[3])]
    chain = ["Head"] + ["Spine_%02d" % (i + 1) for i in range(len(joints_s) - 1)] + ["Tail"]
    pts = [[r4(body.x_nose), 0.0, r4(body.nose_z)]] + [sp(s) for s in joints_s] + \
          [[r4(tail_tip_x), 0.0, r4(body.at(1.0)[3])]]
    bones = {name: {"head": pts[i], "tail": pts[i + 1]} for i, name in enumerate(chain)}
    vol = body_volume_l(body) * k ** 3
    mn, mx = pb.world_bounds([obj])
    extra = {}
    if weight_range:
        extra["length_range_cm"] = [round(100 * length * k * (w / ref_weight) ** (1.0 / 3.0), 1) for w in weight_range]
    return {
        "pivot": "body center: halfway nose tip -> tail tips on X, spine line Z=0, Y=0",
        "forward": "+X (nose), left = +Y, up = +Z",
        "length_m": r4(length),
        "reference_weight_kg": ref_weight,
        "reference_scale": k,
        **extra,
        "body_loops_x_m": [r4(body.x(s)) for s in body.ring_s],
        "body_loops_s": [round(s, 3) for s in body.ring_s],
        "suggested_bones_m": bones,
        "vertex_groups": [g.name for g in obj.vertex_groups],
        "body_volume_l": round(vol, 3),
        "implied_body_weight_kg": round(vol * WATER_DENSITY_KG_L, 2),
        "bounds_min_m": [round(v, 4) for v in mn],
        "bounds_max_m": [round(v, 4) for v in mx],
        "topology": mk.mesh_stats(obj),
    }


# ---------------------------------------------------------------------------------------------------
# Preview staging (never exported)
# ---------------------------------------------------------------------------------------------------
def instance(obj, location, rotation=(0.0, 0.0, 0.0), silhouette=None):
    """Preview copy sharing obj's mesh; silhouette = a material that overrides every slot."""
    inst = obj.copy()
    inst.name = "PV_" + obj.name
    bpy.context.scene.collection.objects.link(inst)
    inst.location = location
    inst.rotation_euler = rotation
    if silhouette is not None:
        for slot in inst.material_slots:
            slot.link = "OBJECT"
            slot.material = silhouette
    return inst


def _stage(objs, placements, water=False, ground_hex=None):
    """placements: [(obj index, location, rotation, silhouette bool)]. Hides the originals; returns cleanup."""
    sil = style.make_material("PV_Silhouette", EYE_HEX, "flat")
    made = []
    for i, loc, rot, black in placements:
        made.append(instance(objs[i], loc, rot, sil if black else None))
    if water or ground_hex:
        hexc = style.TROPICAL.SHALLOW_WATER if water else ground_hex
        mat = style.make_material("PV_Water" if water else "PV_Ground", hexc, "water" if water else "flat")
        bpy.ops.mesh.primitive_plane_add(size=80.0, location=(0.0, 0.0, 0.0))
        plane = bpy.context.active_object
        plane.data.materials.append(mat)
        made.append(plane)
    hidden = [(o, o.hide_render) for o in objs]
    for o in objs:
        o.hide_render = True

    def cleanup():
        for m in made:
            bpy.data.objects.remove(m, do_unlink=True)
        for o, h in hidden:
            o.hide_render = h
    return cleanup




def fp_lens():
    """36 deg horizontal FOV over 768 px = the pixel density of a 1920 px wide, 90 deg FOV game view."""
    return pb.lens_for_hfov(36.0)


def view_5m_side(objs, spacing=1.1):
    """Side-on at 5 m, game pixel density: top row palette colors, bottom row black silhouettes, on sky."""
    xs = [(i - (len(objs) - 1) / 2.0) * spacing for i in range(len(objs))]
    pl = [(i, (x, 0.0, 0.3), (0.0, 0.0, 0.0), False) for i, x in enumerate(xs)]
    pl += [(i, (x, 0.0, -0.3), (0.0, 0.0, 0.0), True) for i, x in enumerate(xs)]
    return {"name": "5m_side", "location": (0.0, -5.0, 0.0), "target": (0.0, 0.0, 0.0), "lens": fp_lens(),
            "resolution": (768, 432), "world_rgb": style.linear(style.TROPICAL.SKY_DAY), "light": "FLAT",
            "view_transform": "Standard", "setup": lambda: _stage(objs, pl)}


def view_5m_water(objs, spacing=0.75):
    """From a dock (eye 2.3 m above the water) at 5 m: each fish broadside and turned 45 deg, belly in the water."""
    k = len(objs) * 2
    xs = [(i - (k - 1) / 2.0) * spacing for i in range(k)]
    pl = []
    for i in range(len(objs)):
        pl.append((i, (xs[2 * i], 0.0, 0.03), (0.0, 0.0, 0.0), False))
        pl.append((i, (xs[2 * i + 1], 0.0, 0.03), (0.0, 0.0, math.radians(45.0)), False))
    return {"name": "5m_water", "location": (0.0, -5.0, 2.3), "target": (0.0, 0.0, 0.0), "lens": fp_lens(),
            "resolution": (768, 432), "world_rgb": style.linear(style.TROPICAL.SKY_DAY), "light": "FLAT",
            "view_transform": "Standard", "setup": lambda: _stage(objs, pl, water=True)}


def preview_setup():
    """Fish previews: Workbench studio light WITHOUT specular highlights. A fin plate or flank facing the camera
    otherwise mirrors the studio light and washes its palette color out (the tail's dark edge vanished)."""
    bpy.context.scene.display.shading.show_specular_highlight = False


def standard_views(obj, length):
    """Species preview: side (from -Y, nose right) and top orthographic, plus the two 5 m checks."""
    mn, mx = pb.world_bounds([obj])
    zc = (mn.z + mx.z) * 0.5
    ortho = length * 1.18
    return [
        {"name": "side", "location": (0.0, -3.0, zc), "target": (0.0, 0.0, zc), "ortho_scale": ortho,
         "resolution": (768, 432)},
        {"name": "top", "location": (0.0, 0.0, 3.0), "rotation": (0.0, 0.0, 0.0), "ortho_scale": ortho,
         "resolution": (768, 432)},
        view_5m_side([obj]),
        view_5m_water([obj]),
    ]

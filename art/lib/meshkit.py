"""Procedural mesh building blocks for Lure recipes (Blender 5.x): lofts, lathes, tori, weights, smoothing.

Everything builds into ONE bmesh through a MeshBuilder, so a recipe can combine parts, give every face a material
index and every vertex vertex-group weights, then write a single clean mesh object. Deterministic: no randomness.

Ring frames: a ring lies in the plane spanned by (u, v) around `center`; the loft direction T must satisfy
u x v = T for outward-facing normals (normals are recalculated at the end anyway, as a safety net).

Usage:
    mb = MeshBuilder(groups=["upperarm_r", ...])
    rings = mb.loft([ring(c, u, v, ru, rv, n=12, mat=0, w={"upperarm_r": 1.0}), ...], cap_start=Vector(...))
    mb.lathe(profile=[(x, r, mat), ...], origin, axis, ref, n=12)
    obj = mb.to_object("SM_Name", materials=[mat_a, mat_b], sharp_angle_deg=40)
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector


def superellipse_ring(center, u, v, ru, rv, n, exponent=2.0, phase=0.0):
    """n points around `center` in the (u, v) plane. exponent 2 = ellipse, 3-4 = rounded rectangle."""
    pts = []
    e = 2.0 / exponent
    for k in range(n):
        a = phase + 2.0 * math.pi * k / n
        c, s = math.cos(a), math.sin(a)
        cu = math.copysign(abs(c) ** e, c)
        sv = math.copysign(abs(s) ** e, s)
        pts.append(Vector(center) + Vector(u) * (ru * cu) + Vector(v) * (rv * sv))
    return pts


def ring(center, u, v, ru, rv=None, n=12, exponent=2.0, mat=0, w=None, phase=0.0, points=None):
    """Ring spec for MeshBuilder.loft. `mat` is the material of the band from this ring to the next one;
    `w` is {group: weight} for this ring's vertices; `points` overrides the generated shape."""
    return {"c": Vector(center), "u": Vector(u).normalized(), "v": Vector(v).normalized(), "ru": ru,
            "rv": ru if rv is None else rv, "n": n, "exp": exponent, "mat": mat, "w": w or {}, "phase": phase,
            "points": points}


def frame_from(tangent, up_hint):
    """(T, u, v) orthonormal with u x v = T; v is `up_hint` made perpendicular to T."""
    t = Vector(tangent).normalized()
    v = Vector(up_hint) - t * Vector(up_hint).dot(t)
    if v.length < 1e-6:
        v = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
        v = v - t * v.dot(t)
    v.normalize()
    u = v.cross(t)
    return t, u.normalized(), v


def rotate_about(vec, axis, angle):
    return Matrix.Rotation(angle, 3, Vector(axis).normalized()) @ Vector(vec)


class MeshBuilder:
    def __init__(self, groups=()):
        self.bm = bmesh.new()
        self.dl = self.bm.verts.layers.deform.verify()
        self.groups = list(groups)
        self._gindex = {g: i for i, g in enumerate(self.groups)}

    # -- weights -------------------------------------------------------------------------------------
    def set_weights(self, verts, weights):
        for v in verts:
            d = v[self.dl]
            d.clear()
            for g, wgt in weights.items():
                if wgt > 0.0:
                    d[self._gindex[g]] = float(wgt)

    # -- primitives ----------------------------------------------------------------------------------
    def _ring_points(self, r):
        if r["points"] is not None:
            return [Vector(p) for p in r["points"]]
        return superellipse_ring(r["c"], r["u"], r["v"], r["ru"], r["rv"], r["n"], r["exp"], r["phase"])

    def loft(self, rings, cap_start=None, cap_end=None, closed=False, cap_start_mat=None, cap_end_mat=None):
        """Quads between consecutive rings (all rings need the same n). cap_start/cap_end: a point for a fan cap
        (a pole vertex), or "flat" for an n-gon, or None for open. closed=True joins the last ring to the first
        (torus). Returns (list of ring vertex lists, [start pole vert or None, end pole vert or None])."""
        bm = self.bm
        vrings = []
        for r in rings:
            vs = [bm.verts.new(p) for p in self._ring_points(r)]
            self.set_weights(vs, r["w"])
            vrings.append(vs)
        n = len(vrings[0])
        pairs = list(range(len(vrings) - 1)) + ([len(vrings) - 1] if closed else [])
        for i in pairs:
            a, b = vrings[i], vrings[(i + 1) % len(vrings)]
            for k in range(n):
                f = bm.faces.new((a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]))
                f.material_index = rings[i]["mat"]
        poles = [None, None]
        for end, cap, idx, mat in ((0, cap_start, 0, cap_start_mat), (1, cap_end, -1, cap_end_mat)):
            if cap is None or closed:
                continue
            vs = vrings[idx]
            m = rings[idx]["mat"] if mat is None else mat
            if isinstance(cap, str) and cap == "flat":
                f = bm.faces.new(vs if end == 1 else list(reversed(vs)))
                f.material_index = m
            else:
                pole = bm.verts.new(Vector(cap))
                self.set_weights([pole], rings[idx]["w"])
                poles[end] = pole
                for k in range(n):
                    if end == 0:
                        f = bm.faces.new((pole, vs[(k + 1) % n], vs[k]))
                    else:
                        f = bm.faces.new((pole, vs[k], vs[(k + 1) % n]))
                    f.material_index = m
        return vrings, poles

    def lathe(self, profile, origin, axis, ref_up, n=12, w=None, phase=0.0):
        """Surface of revolution around `axis` through `origin`. profile: [(along, radius, mat), ...] ordered
        along the axis; a radius of 0 at either end becomes a pole (fan cap). `mat` = band to the next ring."""
        origin, axis = Vector(origin), Vector(axis).normalized()
        t, u, v = frame_from(axis, ref_up)
        body = [p for p in profile if p[1] > 1e-9]
        cap_s = origin + t * profile[0][0] if profile[0][1] <= 1e-9 else "flat"
        cap_e = origin + t * profile[-1][0] if profile[-1][1] <= 1e-9 else "flat"
        rings = [ring(origin + t * a, u, v, r, r, n=n, mat=m, w=w, phase=phase) for a, r, m in body]
        smat = profile[0][2]
        return self.loft(rings, cap_start=cap_s, cap_end=cap_e, cap_start_mat=smat, cap_end_mat=body[-1][2])

    def torus(self, center, axis, major, minor, n_major=10, n_minor=4, mat=0, w=None, ref_up=(0, 0, 1)):
        center = Vector(center)
        a, e1, e2 = frame_from(axis, ref_up)  # e1 x e2 = a
        rings = []
        for i in range(n_major):
            phi = 2.0 * math.pi * i / n_major
            radial = e1 * math.cos(phi) + e2 * math.sin(phi)
            rings.append(ring(center + radial * major, radial, -a, minor, minor, n=n_minor, mat=mat, w=w))
        return self.loft(rings, closed=True)

    def extrude_branch(self, faces, steps, weights_per_step=None):
        """Grow a limb (e.g. a thumb) out of a patch of faces. The patch is extruded once per step and the new
        vertices are placed as: center_k + R_k @ scale_k(local), where local is the vertex offset from the patch
        center in the patch frame (n0 = patch normal, a0 = first in-plane axis, b0 = n0 x a0).
        steps: [{"offset": Vector (from the patch center), "normal": Vector (new cap direction), "sa": scale along
        a0, "sb": scale along b0, "sn": scale along the normal, "round": 0..1 (blend the rectangular patch outline
        towards an ellipse)}, ...]. Returns the list of rings of new verts."""
        bm = self.bm
        bm.normal_update()
        faces = list(faces)
        center = sum((v.co for f in faces for v in f.verts), Vector()) / sum(len(f.verts) for f in faces)
        n0 = sum((f.normal for f in faces), Vector()).normalized()
        a0 = Vector(steps[0].get("axis_hint", (1, 0, 0)))
        a0 = (a0 - n0 * a0.dot(n0)).normalized()
        b0 = n0.cross(a0)
        cur_verts = {v for f in faces for v in f.verts}
        local = {v: Vector(((v.co - center).dot(a0), (v.co - center).dot(b0), (v.co - center).dot(n0)))
                 for v in cur_verts}
        # half extents of the patch in its plane (for "round": pull the section onto an ellipse)
        half_a = max(abs(lc.x) for lc in local.values()) or 1.0
        half_b = max(abs(lc.y) for lc in local.values()) or 1.0
        cur_faces = faces
        out_rings = []
        for si, st in enumerate(steps):
            ret = bmesh.ops.extrude_face_region(bm, geom=cur_faces)
            new_verts = [e for e in ret["geom"] if isinstance(e, bmesh.types.BMVert)]
            new_faces = [e for e in ret["geom"] if isinstance(e, bmesh.types.BMFace)]
            # match every new vertex to the old one it was copied from (same position), before deleting anything
            old = list(local)
            new_local = {}
            for nv in new_verts:
                best = min(old, key=lambda ov: (ov.co - nv.co).length_squared)
                new_local[nv] = local[best]
            # extrude_face_region keeps the original faces: remove them (their interior verts go with them)
            bmesh.ops.delete(bm, geom=cur_faces, context="FACES")
            # frame for this step: rotate (a0, b0, n0) so that n0 -> normal
            nk = Vector(st["normal"]).normalized()
            rot = n0.rotation_difference(nk).to_matrix()
            ak, bk = rot @ a0, rot @ b0
            ck = center + Vector(st["offset"])
            rnd = st.get("round", 0.0)
            for nv, lc in new_local.items():
                x, y = lc.x, lc.y
                e = math.hypot(x / half_a, y / half_b)
                if rnd > 0.0 and e > 0.5:  # boundary verts only (the cap's center vertex stays put)
                    x, y = x + (x / e - x) * rnd, y + (y / e - y) * rnd
                nv.co = ck + ak * (x * st["sa"]) + bk * (y * st["sb"]) + nk * (lc.z * st.get("sn", 1.0))
            if weights_per_step:
                self.set_weights(new_verts, weights_per_step[si])
            local = {v: lc for v, lc in new_local.items()}
            cur_faces = new_faces
            out_rings.append(new_verts)
        return out_rings

    # -- output --------------------------------------------------------------------------------------
    def transform(self, matrix, flip=False):
        bmesh.ops.transform(self.bm, matrix=matrix, verts=self.bm.verts[:])
        if flip:
            bmesh.ops.reverse_faces(self.bm, faces=self.bm.faces[:])

    def to_object(self, name, materials, sharp_angle_deg=None, smooth=True, recalc_normals=True, sharp_materials=None):
        """Write the bmesh to a new mesh object `name` (linked to the scene) with material slots and vertex
        groups. Faces are smooth-shaded; edges sharper than sharp_angle_deg are marked sharp (split normals),
        optionally only where a face with a material index in `sharp_materials` touches the edge."""
        bm = self.bm
        bm.normal_update()
        if recalc_normals:
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        for f in bm.faces:
            f.smooth = smooth
        if sharp_angle_deg is not None:
            lim = math.radians(sharp_angle_deg)
            for e in bm.edges:
                if len(e.link_faces) == 2:
                    sharp = e.link_faces[0].normal.angle(e.link_faces[1].normal, 0.0) >= lim
                    if sharp and sharp_materials is not None:
                        sharp = any(f.material_index in sharp_materials for f in e.link_faces)
                    e.smooth = not sharp
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        obj = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(obj)
        for g in self.groups:
            obj.vertex_groups.new(name=g)
        for m in materials:
            me.materials.append(m)
        return obj


def smart_uv(obj, angle_deg=66.0, margin=0.02):
    """One UV channel (UE needs one for lightmap generation; our flat-color materials don't sample it)."""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(angle_deg), island_margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")


def mesh_stats(obj):
    """Topology sanity: non-manifold edges, loose verts, islands."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    non_manifold = sum(1 for e in bm.edges if not e.is_manifold)
    loose = sum(1 for v in bm.verts if not v.link_faces)
    # islands
    seen, islands = set(), 0
    for v in bm.verts:
        if v.index in seen:
            continue
        islands += 1
        stack = [v]
        while stack:
            x = stack.pop()
            if x.index in seen:
                continue
            seen.add(x.index)
            stack.extend(e.other_vert(x) for e in x.link_edges)
    out = {"verts": len(bm.verts), "faces": len(bm.faces), "non_manifold_edges": non_manifold,
           "loose_verts": loose, "islands": islands}
    bm.free()
    return out

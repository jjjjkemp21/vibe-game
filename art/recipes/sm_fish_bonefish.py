"""SM_Bonefish: stylized low-poly bonefish, the shore/lagoon starter fish (T-008). MESH ONLY, NOT RIGGED: the
animation-artist builds the shared fish spine rig on top of build() (see "For the rig" below).

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_fish_bonefish.py
Export: art/export/Fish/SM_Bonefish.fbx (static mesh + SOCKET_Mouth)
Preview: Saved/AgentLogs/previews/SM_Bonefish.png (contact sheet: 3/4, side, top, 5 m side-on colors + black
silhouettes on sky, 5 m from a dock on the water, at the game's 1080p / 90 deg FOV pixel density)

Look (DT_FishSpecies "Bonefish": "a silver ghost of the sandy flats"): sleek torpedo body, pointed conical snout
over a low mouth, big eye, one swept dorsal fin. Faceted flat shading (12-sided rings), countershaded:
- M_Bonefish_Back  #6C8C8E  grey-teal back (rock #6B7275 tinted toward the shallow water)
- M_Bonefish_Flank #CDD7D9  bright silver flanks (low roughness, reads as wet silver)
- M_Bonefish_Belly #F5F1E6  off-white belly (the rod/line trim white), also the eye ring
- M_Bonefish_Fin   #A8B8BA  pale grey fins
- M_Bonefish_FinEdge #3E4A4F dark slate trailing edge of the tail
- M_Fish_Eye       #2B2A26  pupils (shared by all fish)
IDENTIFYING FEATURE: an oversized, deeply forked swallow tail (fork 62% deep, lobes spread 1.5x the body depth)
with a dark trailing edge: a crisp dark "V" at the back of a pale fish, readable side-on and from above.

Size (data-driven): authored at 48 cm; build() reads ReferenceWeight (1.5 kg) from DT_FishSpecies.json and scales the
fish x1.114 so the body volume (1.43 l x 1.05 kg/l) weighs 1.5 kg: 53.5 cm nose to tail tips at scale 1 (real
bonefish: ~52-55 cm at 1.5 kg). WeightMin..Max 0.5-4.5 kg -> 37-77 cm with the cube-root scale rule (fishkit.py).

Axes and pivot (fishkit convention): forward = +X (nose at x = +0.267), left = +Y, up = +Z; origin = body center
(halfway nose tip -> tail tips), on the spine line Z = 0. Unreal: +X forward, same pivot.

For the rig (animation-artist):
    spec = importlib.util.spec_from_file_location("sm_fish_bonefish", "<repo>/art/recipes/sm_fish_bonefish.py")
    fish = importlib.util.module_from_spec(spec); spec.loader.exec_module(fish)
    obj, info = fish.build()   # mesh with part vertex groups; info = RESULT_JSON "rig" block
Body edge loops (rings) at s = 0.035, 0.10, 0.18, gill 0.25/0.262, then every 0.09 from 0.33 to 1.0 (s = 0 nose tip,
1 = tail root; x = (0.24 - 0.37 s) x reference_scale). Suggested chain (RESULT_JSON rig.suggested_bones_m): Head (nose -> s 0.33),
Spine_01..04 with joints on the loops at s 0.51 / 0.69 / 0.86 / 1.0, Tail (tail root -> tail tips). The same s
values are used by every fishkit species, so one normalized spine rig fits all fish.

Budget: fish <= 3000 triangles (ART_STYLE.md).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

from mathutils import Vector  # noqa: E402

import fishkit as fk  # noqa: E402
import meshkit as mk  # noqa: E402
import pipeline_blender as pb  # noqa: E402
import style  # noqa: E402

ASSET = "SM_Bonefish"
SPECIES = "Bonefish"
ROW = fk.species_row(SPECIES)  # data/tables/DT_FishSpecies.json: ReferenceWeight, WeightMin/Max
REFERENCE_WEIGHT_KG = ROW["ReferenceWeight"]
DESIGN_LENGTH = 0.48     # authored nose tip to tail-lobe tips (m); build() rescales to the reference size
BODY_LEN = 0.37          # nose tip to tail root (peduncle end)
NOSE_Z = -0.010          # the snout tip sits a bit below the spine line (mouth under the snout)
X_NOSE = DESIGN_LENGTH / 2.0

# (s, top, bottom, half-width, center z): s = 0 nose tip, 1 = tail root
ROWS = [
    (0.035, 0.012, 0.010, 0.010, -0.009),
    (0.10, 0.027, 0.022, 0.020, -0.007),
    (0.18, 0.040, 0.035, 0.027, -0.005),
    (0.25, 0.048, 0.043, 0.031, -0.003),
    (0.33, 0.054, 0.051, 0.034, -0.001),
    (0.42, 0.056, 0.053, 0.034, 0.000),
    (0.51, 0.053, 0.049, 0.032, 0.001),
    (0.60, 0.046, 0.041, 0.028, 0.001),
    (0.69, 0.038, 0.032, 0.022, 0.001),
    (0.78, 0.028, 0.024, 0.016, 0.001),
    (0.86, 0.020, 0.017, 0.012, 0.001),
    (0.93, 0.016, 0.014, 0.010, 0.000),
    (1.00, 0.015, 0.013, 0.009, 0.000),
]
GILL = (0.25, 0.035)
JOINTS_S = [0.33, 0.51, 0.69, 0.86, 1.0]

# median fins: (s, height, lean back)
DORSAL = [(0.31, 0.006, 0.0), (0.33, 0.058, 0.022), (0.37, 0.047, 0.020), (0.42, 0.030, 0.012),
          (0.47, 0.016, 0.006), (0.51, 0.006, 0.002)]
ANAL = [(0.78, 0.004, 0.0), (0.80, 0.026, 0.012), (0.86, 0.014, 0.006), (0.90, 0.004, 0.002)]
# caudal (swallow tail): the identifying feature
TAIL = dict(root_half=0.017, span_half=0.078, length=0.12, fork=0.62, lobe_curve=1.0)
TAIL_X_ROOT = X_NOSE - BODY_LEN + 0.01

COLORS = dict(back="#6C8C8E", flank="#CDD7D9", belly="#F5F1E6", fin="#A8B8BA", edge="#3E4A4F")


def build():
    """Build SM_Bonefish (mesh object with part vertex groups and SOCKET_Mouth). Returns (obj, rig_info)."""
    mats = fk.materials(SPECIES, wet=0.25, **COLORS)
    body = fk.Body(ROWS, x_nose=X_NOSE, length=BODY_LEN, nose_z=NOSE_Z, gill=GILL)
    mb = mk.MeshBuilder(groups=fk.PARTS)
    body.build(mb, fk.countershade(back_cos=0.6, belly_cos=-0.8))

    fk.fin_loft(mb, fk.median_stations(body, DORSAL, top=True), (0, 1, 0), "Fin_Dorsal",
                t_root=0.004, t_edge=0.0015)
    st, hi, lo = fk.caudal_stations(TAIL_X_ROOT, 0.0, **TAIL)
    fk.fin_loft(mb, st, (0, 1, 0), "Fin_Caudal", t_root=0.0045, t_edge=0.0018, edge_mat=fk.EDGE, edge_frac=0.5,
                start=hi, end=lo)
    fk.fin_loft(mb, fk.median_stations(body, ANAL, top=False), (0, 1, 0), "Fin_Anal", t_root=0.0035, t_edge=0.0012)

    # pectorals: just behind the gill cover, low on the flank, swept back and out
    pa, pb_ = body.surface(0.275, math.radians(112)), body.surface(0.29, math.radians(132))
    pdir = Vector((-1.0, 0.55, -0.3))
    fk.paired_fin(mb, pa, pb_, pdir, 0.052, "Fin_Pectoral_L", chords=(1.0, 0.9, 0.55), droop=0.004)
    fk.paired_fin(mb, fk.mirror_y(pa), fk.mirror_y(pb_), fk.mirror_y(pdir), 0.052, "Fin_Pectoral_R",
                  chords=(1.0, 0.9, 0.55), droop=0.004)
    # pelvics: under the belly at mid-body, pointing back and down
    va, vb = body.surface(0.49, math.radians(146)), body.surface(0.50, math.radians(166))
    vdir = Vector((-1.0, 0.35, -0.55))
    fk.paired_fin(mb, va, vb, vdir, 0.036, "Fin_Pelvic_L", chords=(1.0, 0.85, 0.5))
    fk.paired_fin(mb, fk.mirror_y(va), fk.mirror_y(vb), fk.mirror_y(vdir), 0.036, "Fin_Pelvic_R",
                  chords=(1.0, 0.85, 0.5))

    fk.eye_pair(mb, body, 0.12, math.radians(58), 0.0105)
    k = fk.reference_scale(body, REFERENCE_WEIGHT_KG)  # body volume x 1.05 kg/l = ReferenceWeight
    obj = fk.finish_object(mb, ASSET, mats, mouth=(X_NOSE - 0.004, 0.0, NOSE_Z), scale=k)
    info = fk.spine_info(body, obj, DESIGN_LENGTH, REFERENCE_WEIGHT_KG, JOINTS_S, -DESIGN_LENGTH / 2.0, scale=k,
                         weight_range=(ROW["WeightMin"], ROW["WeightMax"]))
    info["identifying_feature"] = "deep swallow-forked tail (62% fork, lobes 1.5x body depth) with a dark trailing edge"
    return obj, info


def main():
    args = pb.parse_args(ASSET, "Fish")
    pb.reset_scene()
    fk.preview_setup()
    obj, info = build()
    tris = pb.triangle_count([obj])
    ok, budget = style.check_budget(tris, "fish")
    pb.finish(args, [obj], views=fk.standard_views(obj, info["length_m"]), extra={
        "budget_ok": ok, "budget": budget, "rig": info,
    })


if __name__ == "__main__":
    main()

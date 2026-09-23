"""SM_CoralSnapper: stylized low-poly reef snapper, the reef starter fish (T-008). MESH ONLY, NOT RIGGED: the
animation-artist builds the shared fish spine rig on top of build() (see "For the rig" below).

Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe art/recipes/sm_fish_coralsnapper.py
Export: art/export/Fish/SM_CoralSnapper.fbx (static mesh + SOCKET_Mouth)
Preview: Saved/AgentLogs/previews/SM_CoralSnapper.png (contact sheet: 3/4, side, top, 5 m side-on colors + black
silhouettes on sky, 5 m from a dock on the water, at the game's 1080p / 90 deg FOV pixel density)

Look (DT_FishSpecies "CoralSnapper": "brick-red and stubborn ... dives straight for the reef"): a deep, humpbacked
reef fish with a steep forehead, a low heavy jaw, a big eye high on the head, a shallow-forked tail and a pointed
anal fin. Faceted flat shading (12-sided rings), countershaded in warm reef tones. It avoids the accent red
#FF4D3D (reserved for the bobber): its reds are darker and browner (brick) or lighter and pinker (reef coral).
- M_CoralSnapper_Back    #A8452F  brick-red back (the journal's "brick-red"; value ~66%, far from the bright accent)
- M_CoralSnapper_Flank   #F28F6B  reef coral flanks (palette "reef")
- M_CoralSnapper_Belly   #F7E1CC  pale coral-cream belly, also the eye ring
- M_CoralSnapper_Fin     #C4543A  deep coral fins
- M_CoralSnapper_FinEdge #FF9A5A  sunset-orange fin edges (palette "sunset"): outlines the crest and the tail
- M_Fish_Eye             #2B2A26  pupils (shared by all fish)
IDENTIFYING FEATURE: a tall saw-tooth spiny crest: four big back-swept spines (up to 9 cm, over half the body
depth) with deep notches between them, ahead of a low soft dorsal. The jagged back line reads side-on at 5 m.

Size (data-driven): authored at 55 cm; build() reads ReferenceWeight (2.5 kg) from DT_FishSpecies.json and scales the
fish x1.023 so the body volume (2.38 l x 1.05 kg/l) weighs 2.5 kg: 56.3 cm nose to tail tips at scale 1 (real
snapper: ~55 cm at 2.5 kg). WeightMin..Max 0.8-7 kg -> 38.5-79 cm with the cube-root scale rule (fishkit.py).

Axes and pivot (fishkit convention): forward = +X (nose at x = +0.281), left = +Y, up = +Z; origin = body center
(halfway nose tip -> tail tips), on the spine line Z = 0. Unreal: +X forward, same pivot.

For the rig (animation-artist):
    spec = importlib.util.spec_from_file_location("sm_fish_coralsnapper", "<repo>/art/recipes/sm_fish_coralsnapper.py")
    fish = importlib.util.module_from_spec(spec); spec.loader.exec_module(fish)
    obj, info = fish.build()   # mesh with part vertex groups; info = RESULT_JSON "rig" block
Body edge loops at s = 0.04, 0.11, 0.19, gill 0.26/0.272, then every 0.09 from 0.33 to 1.0 (s = 0 nose tip, 1 = tail
root; x = (0.275 - 0.43 s) x reference_scale): the same normalized spine loops as SM_Bonefish, so one spine rig fits both. The crest has
its own cross-section every 0.04 s (it bends with the body).

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

ASSET = "SM_CoralSnapper"
SPECIES = "CoralSnapper"
ROW = fk.species_row(SPECIES)  # data/tables/DT_FishSpecies.json: ReferenceWeight, WeightMin/Max
REFERENCE_WEIGHT_KG = ROW["ReferenceWeight"]
DESIGN_LENGTH = 0.55     # authored nose tip to tail-lobe tips (m); build() rescales to the reference size
BODY_LEN = 0.43          # nose tip to tail root (peduncle end)
NOSE_Z = -0.024          # low, heavy jaw under a steep forehead
X_NOSE = DESIGN_LENGTH / 2.0

# (s, top, bottom, half-width, center z): s = 0 nose tip, 1 = tail root
ROWS = [
    (0.04, 0.022, 0.014, 0.013, -0.016),
    (0.11, 0.052, 0.034, 0.023, -0.011),
    (0.19, 0.078, 0.051, 0.032, -0.006),
    (0.26, 0.094, 0.064, 0.038, -0.003),
    (0.33, 0.101, 0.071, 0.040, 0.000),
    (0.42, 0.099, 0.071, 0.040, 0.001),
    (0.51, 0.090, 0.065, 0.037, 0.002),
    (0.60, 0.076, 0.056, 0.032, 0.003),
    (0.69, 0.060, 0.045, 0.026, 0.003),
    (0.78, 0.044, 0.033, 0.020, 0.003),
    (0.86, 0.031, 0.025, 0.015, 0.002),
    (0.93, 0.025, 0.021, 0.013, 0.002),
    (1.00, 0.025, 0.022, 0.012, 0.002),
]
GILL = (0.26, 0.03)
JOINTS_S = [0.33, 0.51, 0.69, 0.86, 1.0]

# the crest (identifying feature): spine tips (tall, leaning back) and notches, then the soft dorsal
DORSAL = [
    (0.22, 0.006, 0.0),
    (0.25, 0.085, 0.012), (0.29, 0.040, 0.0),    # spine 1, notch
    (0.33, 0.090, 0.012), (0.37, 0.042, 0.0),    # spine 2, notch
    (0.41, 0.080, 0.012), (0.45, 0.040, 0.0),    # spine 3, notch
    (0.49, 0.066, 0.010), (0.53, 0.034, 0.0),    # spine 4, notch
    (0.60, 0.040, 0.010), (0.69, 0.036, 0.012), (0.76, 0.020, 0.010), (0.79, 0.006, 0.003),  # soft dorsal
]
ANAL = [(0.68, 0.005, 0.0), (0.70, 0.050, 0.022), (0.76, 0.030, 0.010), (0.83, 0.006, 0.002)]
TAIL = dict(root_half=0.027, span_half=0.086, length=0.13, fork=0.22, lobe_curve=0.8)
TAIL_X_ROOT = X_NOSE - BODY_LEN + 0.01

COLORS = dict(back="#A8452F", flank="#F28F6B", belly="#F7E1CC", fin="#C4543A", edge="#FF9A5A")


def build():
    """Build SM_CoralSnapper (mesh object with part vertex groups and SOCKET_Mouth). Returns (obj, rig_info)."""
    mats = fk.materials(SPECIES, wet=0.3, **COLORS)
    body = fk.Body(ROWS, x_nose=X_NOSE, length=BODY_LEN, nose_z=NOSE_Z, gill=GILL)
    mb = mk.MeshBuilder(groups=fk.PARTS)
    body.build(mb, fk.countershade(back_cos=0.55, belly_cos=-0.6))

    fk.fin_loft(mb, fk.median_stations(body, DORSAL, top=True), (0, 1, 0), "Fin_Dorsal", t_root=0.0045,
                t_edge=0.0015, edge_mat=fk.EDGE, edge_frac=0.72)
    st, hi, lo = fk.caudal_stations(TAIL_X_ROOT, 0.002, **TAIL)
    fk.fin_loft(mb, st, (0, 1, 0), "Fin_Caudal", t_root=0.005, t_edge=0.0018, edge_mat=fk.EDGE, edge_frac=0.8,
                start=hi, end=lo)
    fk.fin_loft(mb, fk.median_stations(body, ANAL, top=False), (0, 1, 0), "Fin_Anal", t_root=0.004, t_edge=0.0015,
                edge_mat=fk.EDGE, edge_frac=0.75)

    pa, pb_ = body.surface(0.285, math.radians(115)), body.surface(0.30, math.radians(135))
    pdir = Vector((-1.0, 0.45, -0.32))
    fk.paired_fin(mb, pa, pb_, pdir, 0.075, "Fin_Pectoral_L", chords=(1.0, 0.85, 0.45), droop=0.006)
    fk.paired_fin(mb, fk.mirror_y(pa), fk.mirror_y(pb_), fk.mirror_y(pdir), 0.075, "Fin_Pectoral_R",
                  chords=(1.0, 0.85, 0.45), droop=0.006)
    va, vb = body.surface(0.31, math.radians(146)), body.surface(0.32, math.radians(166))
    vdir = Vector((-1.0, 0.3, -0.7))
    fk.paired_fin(mb, va, vb, vdir, 0.05, "Fin_Pelvic_L", chords=(1.0, 0.85, 0.5))
    fk.paired_fin(mb, fk.mirror_y(va), fk.mirror_y(vb), fk.mirror_y(vdir), 0.05, "Fin_Pelvic_R",
                  chords=(1.0, 0.85, 0.5))

    fk.eye_pair(mb, body, 0.13, math.radians(50), 0.012)
    k = fk.reference_scale(body, REFERENCE_WEIGHT_KG)  # body volume x 1.05 kg/l = ReferenceWeight
    obj = fk.finish_object(mb, ASSET, mats, mouth=(X_NOSE - 0.004, 0.0, NOSE_Z), scale=k)
    info = fk.spine_info(body, obj, DESIGN_LENGTH, REFERENCE_WEIGHT_KG, JOINTS_S, -DESIGN_LENGTH / 2.0, scale=k,
                         weight_range=(ROW["WeightMin"], ROW["WeightMax"]))
    info["identifying_feature"] = "tall saw-tooth spiny crest: 4 back-swept spines with deep notches, orange-edged"
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

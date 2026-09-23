"""Lure art style for Blender recipes: palette, material presets, bevel rule.

Source of truth: docs/ART_STYLE.md (v1, approved 2026-09-22). If that document changes, update this file.

Usage in a recipe (after putting art/lib on sys.path, like sm_golden_crate.py does):
    import style
    mat = style.make_material("M_DockPlank", style.TROPICAL.WEATHERED_WOOD, "wood")
    style.add_bevel(obj)                       # 2-3% of the smallest dimension, 2 segments
    rgba = style.hex_to_linear_rgba("#3ED1C4") # linear RGBA for Blender color sockets

Colors are stored as sRGB hex strings (what the art doc and paint tools use). Blender color sockets and
material.diffuse_color expect LINEAR values, so always convert with hex_to_linear_rgba / linear().
Only the material name, base color, roughness and metallic survive the FBX export to Unreal; the extra
procedural nodes some presets add (wood grain, emission) are for Blender renders only.
"""
import bpy

import pipeline_blender as pb


# ---------------------------------------------------------------------------------------------------
# Palette (sRGB hex, from docs/ART_STYLE.md)
# ---------------------------------------------------------------------------------------------------
class TROPICAL:
    """Tropical / sunny: beginner zone and vertical slice."""
    SHALLOW_WATER = "#3ED1C4"
    DEEP_WATER = "#0E6F7A"
    REEF = "#F28F6B"
    SAND = "#F2D6A2"
    PALM_GREEN = "#3F8F4A"
    LEAF_DARK = "#2A5E36"
    WEATHERED_WOOD = "#8A5A3B"
    ROPE = "#C9A66B"
    SKY_DAY = "#8FD3F0"
    SUNSET = "#FF9A5A"
    NIGHT = "#1B2440"
    ACCENT = "#FF4D3D"  # bobber, UI highlights


class FOGGY:
    """Foggy / eerie."""
    FOG = "#8A9A9C"
    ROCK = "#3E4A4F"
    WATER = "#2C3E40"
    LANTERN = "#E8C46A"
    WRONG_LIGHT = "#9DB36B"  # the accent for lights that should not be there


class FROZEN:
    """Frozen / cold."""
    SNOW = "#E6F2F5"
    ICE = "#9CC7D8"
    DEEP_ICE = "#2F4A5E"
    ACCENT = "#FFB347"


class MURKY:
    """Murky / gloomy."""
    WATER = "#3B3A24"
    MUD = "#4E4330"
    MOSS = "#5E6B3A"
    ACCENT = "#C7A43B"


class UI:
    PARCHMENT = "#F3E9D2"
    INK = "#2B2A26"
    DANGER = "#C0392B"
    SAFE = "#3FA34D"


REGIONS = {"tropical": TROPICAL, "foggy": FOGGY, "frozen": FROZEN, "murky": MURKY, "ui": UI}


def palette(region):
    """Ordered list of (NAME, hex) for a region key in REGIONS (e.g. "tropical")."""
    cls = REGIONS[region.lower()]
    return [(k, v) for k, v in vars(cls).items() if k.isupper()]


# ---------------------------------------------------------------------------------------------------
# Color conversion
# ---------------------------------------------------------------------------------------------------
def _srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_to_srgb(hex_str):
    """'#RRGGBB' -> (r, g, b) in 0..1, still sRGB encoded."""
    h = hex_str.lstrip("#")
    if len(h) != 6:
        raise ValueError("Expected #RRGGBB, got " + repr(hex_str))
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def hex_to_linear_rgba(hex_str, alpha=1.0):
    """'#RRGGBB' -> linear (r, g, b, a) for Blender color sockets and diffuse_color."""
    r, g, b = (_srgb_to_linear(c) for c in hex_to_srgb(hex_str))
    return (r, g, b, alpha)


def linear(hex_str):
    """'#RRGGBB' -> linear (r, g, b) tuple (the form pipeline_blender.make_material takes)."""
    return hex_to_linear_rgba(hex_str)[:3]


def mix_hex(hex_a, hex_b, t):
    """Blend two palette colors in linear space; returns linear (r, g, b, 1). t=0 -> a, t=1 -> b."""
    a, b = hex_to_linear_rgba(hex_a), hex_to_linear_rgba(hex_b)
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3)) + (1.0,)


# ---------------------------------------------------------------------------------------------------
# Material presets (roughness ranges from ART_STYLE.md "Materials")
# ---------------------------------------------------------------------------------------------------
PRESETS = {
    # name: (default roughness, (min, max) allowed range, metallic)
    "flat": (0.75, (0.6, 0.9), 0.0),    # default: flat palette color
    "wood": (0.85, (0.7, 0.9), 0.0),    # weathered wood, rope; optional grain strips in renders
    "wet": (0.35, (0.25, 0.45), 0.0),   # fish, wet rocks, bobber
    "water": (0.12, (0.05, 0.3), 0.0),  # stand-in water for Blender renders only (real water is Unreal)
    "emissive": (0.6, (0.4, 0.9), 0.0), # lanterns, glows; emission strength via `emission`
}


def make_material(name, hex_color, preset="flat", roughness=None, emission=0.0, grain=False):
    """Create (or refresh) material `name` with a palette color and a preset from PRESETS.

    roughness: optional override, clamped to the preset's allowed range.
    emission:  emission strength (used by the "emissive" preset; any preset accepts it).
    grain:     "wood" only: adds subtle procedural plank strips for Blender renders (not exported).
    Returns the bpy material. Name it M_<Something> for assets that go to Unreal.
    """
    if preset not in PRESETS:
        raise ValueError("Unknown preset %r; choose from %s" % (preset, sorted(PRESETS)))
    default_r, (r_min, r_max), metallic = PRESETS[preset]
    r = default_r if roughness is None else max(r_min, min(r_max, float(roughness)))
    mat = pb.make_material(name, linear(hex_color), roughness=r, metallic=metallic)
    tree = mat.node_tree
    bsdf = tree.nodes.get("Principled BSDF")
    if bsdf is None:
        return mat
    if preset == "emissive" and emission <= 0.0:
        emission = 3.0
    if emission > 0.0:
        bsdf.inputs["Emission Color"].default_value = hex_to_linear_rgba(hex_color)
        bsdf.inputs["Emission Strength"].default_value = emission
    if preset == "wood" and grain and not tree.nodes.get("LureGrain"):
        _add_wood_grain(tree, bsdf, hex_color)
    mat["lure_preset"] = preset
    return mat


def _add_wood_grain(tree, bsdf, hex_color):
    """Soft darker strips along local X (the plank length): a render-only stand-in for the wood strip texture."""
    nodes, links = tree.nodes, tree.links
    coord = nodes.new("ShaderNodeTexCoord")
    wave = nodes.new("ShaderNodeTexWave")
    wave.name = "LureGrain"
    wave.wave_type = "BANDS"
    wave.bands_direction = "Y"
    wave.inputs["Scale"].default_value = 6.0
    wave.inputs["Distortion"].default_value = 3.0
    wave.inputs["Detail"].default_value = 1.0
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = mix_hex(hex_color, "#000000", 0.25)
    ramp.color_ramp.elements[1].color = hex_to_linear_rgba(hex_color)
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[1].position = 0.65
    links.new(coord.outputs["Object"], wave.inputs["Vector"])
    links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])


# ---------------------------------------------------------------------------------------------------
# Bevel rule: about 2-3% of the object's smallest dimension, 1-2 segments
# ---------------------------------------------------------------------------------------------------
BEVEL_RATIO_MIN = 0.02
BEVEL_RATIO_MAX = 0.03
BEVEL_RATIO_DEFAULT = 0.025


def bevel_width(dimensions, ratio=BEVEL_RATIO_DEFAULT):
    """Bevel width in meters for an object of `dimensions` (x, y, z in m). Ratio clamped to 2-3%.
    Zero-size axes (planes) are ignored."""
    ratio = max(BEVEL_RATIO_MIN, min(BEVEL_RATIO_MAX, ratio))
    dims = [d for d in dimensions if d > 1e-6]
    return (min(dims) if dims else 0.0) * ratio


def add_bevel(obj, ratio=BEVEL_RATIO_DEFAULT, segments=2, angle_deg=30.0):
    """Add the house-style Bevel modifier to a mesh object (angle-limited, so curved faces stay clean).
    Uses obj.dimensions (object scale included). Returns the modifier."""
    segments = max(1, min(2, int(segments)))
    import math
    mod = obj.modifiers.new(name="Bevel", type="BEVEL")
    mod.width = bevel_width(tuple(obj.dimensions), ratio)
    mod.segments = segments
    mod.limit_method = "ANGLE"
    mod.angle_limit = math.radians(angle_deg)
    mod.use_clamp_overlap = True
    return mod


def shade_flat(obj):
    """Faceted low-poly look (rocks, terrain)."""
    for p in obj.data.polygons:
        p.use_smooth = False


def shade_smooth(obj):
    for p in obj.data.polygons:
        p.use_smooth = True


# ---------------------------------------------------------------------------------------------------
# Budgets (triangles) from ART_STYLE.md
# ---------------------------------------------------------------------------------------------------
BUDGETS = {
    "small_prop": 2000,
    "large_prop": 10000,
    "hero_prop": 30000,
    "fish": 3000,
    "boat": 8000,
    "island_module": 10000,
}
TEXTURE_SIZE_DEFAULT = 512
TEXTURE_SIZE_HERO = 1024


def check_budget(triangles, kind):
    """(ok, budget) for a triangle count against BUDGETS[kind]."""
    budget = BUDGETS[kind]
    return triangles <= budget, budget

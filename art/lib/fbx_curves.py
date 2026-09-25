"""fbx_curves: float animation curves for Unreal inside a skeletal clip FBX (T-062, SK_FPArms Cast clips).

Why this exists
- Unreal turns an ANIMATED, USER-DEFINED number property on a bone node of a clip FBX into an anim curve of the same
  name: the legacy importer (FbxFactory: FbxAnimUtils::ExtractAttributeCurves over every skeleton node, root
  included) and Interchange (FFbxScene custom attributes -> the anim sequence factory's curve names) both do it, and
  a double property always imports as a float curve (never an animation attribute / step curve). The curve name is
  the FBX AnimationCurveNode's name. Import options: legacy "Import Custom Attribute" (default on); "Do not import
  curves with only 0 values" skips an all-zero curve (every curve written here has a non-zero key, so either way).
- Blender's FBX exporter writes no animated custom properties, so recipes key the curve as a custom property on a
  pose bone in the clip's action (`pbone["HandL_Free"]`, F-curve `pose.bones["root"]["HandL_Free"]`) and, after
  pb.export_skeletal_fbx(), call add_action_curves() on the written file.
- Notifies can't travel in an FBX to Unreal: list them in the clip's .anim.md (name, frame, time); the editor-operator
  adds them after import.

What add_float_curves() writes (the exporter's own conventions, from a dump of its clip files):
  on the bone's Model   Properties70 P "<name>", "Number", "", "A+U", <first key>   (user-defined, animatable)
  AnimationCurveNode    "<name>" with P "d|<name>", "Number", "", "A", <first key>; OO to the take's AnimationLayer,
                        OP to the Model's property "<name>"
  AnimationCurve        one key per frame (KeyTime in FBX ticks, KeyValueFloat, linear key attributes copied from the
                        file's own curves), OP to the curve node's "d|<name>"
  Definitions           object counts updated
The file is parsed and rewritten with Blender's own io_scene_fbx parse_fbx / encode_bin (the exporter's writer).
Before changing anything the unchanged tree is re-encoded and compared with the file byte for byte (raises if the
round trip is not exact), so nothing else in the file can change. read_float_curves() reads them back for checks.
"""
import re
import tempfile
import zlib
from pathlib import Path

import pipeline_blender as pb

FBX_KTIME = 46186158000        # FBX ticks per second (io_scene_fbx.fbx_utils.FBX_KTIME)
_POSE_PROP = re.compile(r'^pose\.bones\["(?P<bone>(?:[^"\\]|\\.)+)"\]\["(?P<prop>(?:[^"\\]|\\.)+)"\]$')
_SEP = b"\x00\x01"


def _modules():
    pb.ensure_fbx_exporter()
    from io_scene_fbx import encode_bin, parse_fbx
    return parse_fbx, encode_bin


def _array_types():
    from io_scene_fbx import data_types
    return data_types.ARRAY_INT64, data_types.ARRAY_INT32, data_types.ARRAY_FLOAT32


def action_float_curves(action, slot=None):
    """{(bone, curve name): F-curve} for every custom-property F-curve on a pose bone in `action` (layered actions:
    all channelbags of the given slot, or of every slot)."""
    out = {}
    for layer in action.layers:
        for strip in layer.strips:
            bags = [strip.channelbag(slot)] if slot is not None else list(strip.channelbags)
            for bag in bags:
                if bag is None:
                    continue
                for fc in bag.fcurves:
                    m = _POSE_PROP.match(fc.data_path)
                    if m:
                        out[(m.group("bone"), m.group("prop"))] = fc
    return out


def add_action_curves(path, action, frame_start, frame_end, fps):
    """add_float_curves() with the pose-bone custom-property F-curves of `action`, sampled on every frame of the take
    (frame_start..frame_end). Returns {bone: [curve names]} ({} when the action has none; the file is untouched)."""
    by_bone = {}
    for (bone, name), fc in sorted(action_float_curves(action).items()):
        keys = [(f, float(fc.evaluate(f))) for f in range(int(frame_start), int(frame_end) + 1)]
        by_bone.setdefault(bone, {})[name] = keys
    for bone, curves in by_bone.items():
        add_float_curves(path, bone, curves, fps, frame_start)
    return {b: sorted(c) for b, c in by_bone.items()}


def _child(e, id_):
    return next((c for c in e.elems if c.id == id_), None)


def _writer_tree(elem, encode_bin):
    """parse_fbx element (namedtuple) -> encode_bin.FBXElem with the same properties and types."""
    out = encode_bin.FBXElem(elem.id)
    add = {ord("Z"): out.add_int8, ord("Y"): out.add_int16, ord("B"): out.add_bool, ord("C"): out.add_char,
           ord("I"): out.add_int32, ord("F"): out.add_float32, ord("D"): out.add_float64, ord("L"): out.add_int64,
           ord("R"): out.add_bytes, ord("S"): out.add_string, ord("f"): out.add_float32_array,
           ord("i"): out.add_int32_array, ord("d"): out.add_float64_array, ord("l"): out.add_int64_array,
           ord("b"): out.add_bool_array, ord("c"): out.add_byte_array}
    for v, t in zip(elem.props, elem.props_type):
        add[t](v)
    for c in elem.elems:
        out.elems.append(_writer_tree(c, encode_bin))
    return out


def _encode(root, version, encode_bin, path):
    tree = _writer_tree(root, encode_bin)
    encode_bin.write(str(path), tree, version)


def _new_id(used, key):
    i = 0x40000000 + (zlib.crc32(key) & 0x3FFFFFFF)
    while i in used:
        i += 1
    used.add(i)
    return i


class _El:
    """A mutable stand-in for parse_fbx's namedtuple elements (id, props, props_type, elems)."""
    __slots__ = ("id", "props", "props_type", "elems")

    def __init__(self, id_, props=(), types=b"", elems=()):
        self.id, self.props, self.props_type, self.elems = id_, list(props), bytearray(types), list(elems)


def _mutable(e):
    return _El(e.id, e.props, e.props_type, [_mutable(c) for c in e.elems])


def add_float_curves(path, bone, curves, fps, frame_start=0):
    """Add float curves to the bone `bone` of the (single-take) clip FBX at `path`, in place.
    curves: {name: [(frame, value), ...]} (frames on the take's timeline; keys are written linear)."""
    import array
    parse_fbx, encode_bin = _modules()
    t_i64, t_i32, t_f32 = _array_types()
    path = Path(path)
    parsed, version = parse_fbx.parse(str(path))
    with tempfile.TemporaryDirectory() as td:                    # the round trip must be exact before any change
        probe = Path(td) / "roundtrip.fbx"
        _encode(parsed, version, encode_bin, probe)
        if probe.read_bytes() != path.read_bytes():
            raise RuntimeError("%s: the FBX does not re-encode byte for byte; not touching it" % path.name)
    root = _mutable(parsed)
    objects, conns, defs = _child(root, b"Objects"), _child(root, b"Connections"), _child(root, b"Definitions")
    models = [e for e in objects.elems if e.id == b"Model" and e.props[1] == bone.encode() + _SEP + b"Model"]
    layers = [e for e in objects.elems if e.id == b"AnimationLayer"]
    ref_curve = next((e for e in objects.elems if e.id == b"AnimationCurve"), None)
    if len(models) != 1 or len(layers) != 1 or ref_curve is None:
        raise RuntimeError("%s: need one Model '%s', one AnimationLayer and a baked curve (found %d / %d / %s)"
                           % (path.name, bone, len(models), len(layers), ref_curve is not None))
    model, layer = models[0], layers[0]
    props70 = _child(model, b"Properties70")
    existing = {p.props[0] for p in props70.elems}
    used = {e.props[0] for e in objects.elems if e.props and isinstance(e.props[0], int)}
    flags = _child(ref_curve, b"KeyAttrFlags").props[0]
    attr = _child(ref_curve, b"KeyAttrDataFloat").props[0]
    for name, keys in curves.items():
        nb = name.encode()
        if nb in existing:
            raise RuntimeError("%s: bone %s already has a property %s" % (path.name, bone, name))
        keys = sorted((float(f), float(v)) for f, v in keys)
        v0 = keys[0][1]
        props70.elems.append(_El(b"P", [nb, b"Number", b"", b"A+U", v0], b"SSSSD"))
        cn_id = _new_id(used, b"curvenode:" + bone.encode() + b":" + nb)
        cv_id = _new_id(used, b"curve:" + bone.encode() + b":" + nb)
        cn = _El(b"AnimationCurveNode", [cn_id, nb + _SEP + b"AnimCurveNode", b""], b"LSS",
                 [_El(b"Properties70", elems=[_El(b"P", [b"d|" + nb, b"Number", b"", b"A", v0], b"SSSSD")])])
        times = array.array(t_i64, [int(round((f - frame_start) * FBX_KTIME / fps)) for f, _v in keys])
        values = array.array(t_f32, [v for _f, v in keys])
        cv = _El(b"AnimationCurve", [cv_id, _SEP + b"AnimCurve", b""], b"LSS", [
            _El(b"Default", [v0], b"D"), _El(b"KeyVer", [4008], b"I"),
            _El(b"KeyTime", [times], b"l"), _El(b"KeyValueFloat", [values], b"f"),
            _El(b"KeyAttrFlags", [array.array(t_i32, list(flags))], b"i"),
            _El(b"KeyAttrDataFloat", [array.array(t_f32, list(attr))], b"f"),
            _El(b"KeyAttrRefCount", [array.array(t_i32, [len(keys)])], b"i")])
        objects.elems += [cn, cv]
        conns.elems += [_El(b"C", [b"OO", cn_id, layer.props[0]], b"SLL"),
                        _El(b"C", [b"OP", cn_id, model.props[0], nb], b"SLLS"),
                        _El(b"C", [b"OP", cv_id, cn_id, b"d|" + nb], b"SLLS")]
        for d in defs.elems:
            if d.id == b"Count":
                d.props[0] += 2
            elif d.id == b"ObjectType" and d.props[0] in (b"AnimationCurveNode", b"AnimationCurve"):
                _child(d, b"Count").props[0] += 1
    _encode(root, version, encode_bin, path)


def read_float_curves(path, fps=30.0):
    """{bone: {name: {"keys": [(frame at `fps` from the take start, value)], "flags": str, "curve_node": name,
    "on_layer": bool}}} for every user-defined ('U') number property of a bone Model that is driven by a curve node."""
    parse_fbx, _enc = _modules()
    root, _v = parse_fbx.parse(str(path))
    objects, conns = _child(root, b"Objects"), _child(root, b"Connections")
    by_id = {e.props[0]: e for e in objects.elems if e.props and isinstance(e.props[0], int)}
    layers = {e.props[0] for e in objects.elems if e.id == b"AnimationLayer"}
    parent_of = {}
    for c in conns.elems:
        parent_of.setdefault(c.props[1], []).append((c.props[0], c.props[2], c.props[3] if len(c.props) > 3 else None))
    out = {}
    for e in objects.elems:
        if e.id != b"Model":
            continue
        p70 = _child(e, b"Properties70")
        for p in (p70.elems if p70 else []):
            if b"U" not in p.props[3]:
                continue
            name = p.props[0]
            cn = next((by_id[k] for k, links in parent_of.items() if k in by_id and by_id[k].id == b"AnimationCurveNode"
                       for kind, par, prop in links if kind == b"OP" and par == e.props[0] and prop == name), None)
            if cn is None:
                continue
            cv = next((by_id[k] for k, links in parent_of.items() if k in by_id and by_id[k].id == b"AnimationCurve"
                       for kind, par, prop in links if kind == b"OP" and par == cn.props[0] and prop == b"d|" + name),
                      None)
            keys = []
            if cv is not None:
                kt, kv = _child(cv, b"KeyTime").props[0], _child(cv, b"KeyValueFloat").props[0]
                keys = [(round(t * fps / FBX_KTIME, 4), round(float(v), 6)) for t, v in zip(kt, kv)]
            bone = e.props[1].split(_SEP)[0].decode()
            out.setdefault(bone, {})[name.decode()] = {
                "keys": keys, "flags": p.props[3].decode(), "curve_node": cn.props[1].split(_SEP)[0].decode(),
                "on_layer": any(kind == b"OO" and par in layers for kind, par, _p in parent_of.get(cn.props[0], []))}
    return out

"""Level layouts for Lure (T-005).

- layout.py:      pure-Python layout model (load, expand compounds, validate, routes). No `unreal` or `bpy` import,
                  so the Unreal builder and the Blender preview share it and always agree.
- build_level.py: the ONE generic Unreal builder (editor only; imports `unreal`).

Source of truth for each level: data/levels/<Level>.json. Plans: docs/levels/<Level>.md.
"""

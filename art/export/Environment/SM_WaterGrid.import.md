# SM_WaterGrid_Near + SM_WaterGrid_Far: import spec (T-069d)

Recipe: `art/recipes/sm_water_grid.py`. Files: `art/export/Environment/SM_WaterGrid_Near.fbx`, `SM_WaterGrid_Far.fbx`.
These are technical meshes: flat grids that M_LevelWater displaces (WPO waves) and shades faceted. Nobody sees the geometry itself.

## Import
- Destination `/Game/Art/Environment`, a NEW import (names `SM_WaterGrid_Near`, `SM_WaterGrid_Far`).
- Legacy FBX importer, `pipeline_unreal.import_static_mesh_fbx(src, "/Game/Art/Environment", name, import_materials=False)`
  (Convert Scene ON, Convert Scene Unit ON, Force Front X Axis OFF, uniform scale 1.0). That helper doesn't set the options
  below, so set them on `FbxStaticMeshImportData` (or fix them on the asset right after import):
  - `build_nanite` = False (Nanite OFF).
  - `auto_generate_collision` = False. Then `EditorStaticMeshLibrary.remove_collisions(mesh)` to be sure. There's no UCX in
    the file, so NO collision: gameplay water is the flat LureWaterVolume.
  - `vertex_color_import_option` = REPLACE (the default): the vertex colors carry data (see below).
  - Keep the triangulation as is (the file is all triangles, and the near diagonals are fixed on purpose). `remove_degenerates`
    doesn't matter (there are 0 degenerate triangles).
- Materials: don't import one. The file has one slot, `M_LevelWater`. build_level.py assigns its water material
  (`/Game/Materials/Level/M_LevelWater` or an MI under it) to slot 0.

## Geometry (Unreal axes, cm; pivot = center, Z = 0, normals +Z)
- Near: expected bounds X -14400..14400, Y -14400..14400, Z 0 (box extent about 14400, 14400, 0). 2 m cells on an exact 200 uu
  lattice, 41,472 triangles. Every cell is split along its (-X,-Y) -> (+X,+Y) diagonal (checked on the re-imported FBX,
  20,736/20,736). C++ wave-height interpolation relies on this.
- Far: a ring from +-14400 to +-40000 (box extent about 40000, 40000, 0), bands of 4/8/16/32 m cells, 5,892 triangles. It has an
  exact square hole matching the near edge. There are no T-junctions, and the shared seam vertices are bit-identical (distance 0 on the re-imported FBX).
- Total 47,364 triangles (cap 60k).
- **Bounds must be extended for the wave height:** a flat mesh has 0 Z extent, so set a positive/negative bounds extension (or
  the component's bounds scale) of at least the max wave amplitude plus the shore run-up. Otherwise the displaced water gets culled.
- UV0: planar XY, 0..1 over each mesh (separately per mesh).

## Vertex color
- R = cell size at the vertex / 64 m (clamped to 1). A vertex on a seam carries the SMALLER cell size in both meshes, so the fade is
  continuous. G = B = 0, A = 1.
- **R is read LINEAR (no sRGB decode) and is 8-bit quantized:** the bytes in the file are 2 m = 8/255 (0.0314), 4 m = 16/255,
  8 m = 32/255, 16 m = 64/255, 32 m = 128/255. The shader recovers the cell size as R * 64 m (2 m -> 2.008 m). For T-069c and T-069a.

## Placement and references
- Placed by build_level.py (T-069c) at the sea center, Z = water_z: both meshes at the same location, no rotation, scale 1.
  The builder places the pivot on a 200 uu world lattice.
- Referenced by: build_level.py (the T-069c water graph v5). There are no Blueprints or data rows.
- Facing: the mesh is symmetric apart from the diagonal direction. Check it in the editor with a wireframe view of a near cell
  (the diagonal runs from the cell's -X,-Y corner to its +X,+Y corner).

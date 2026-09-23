# Surface swimming (T-026): engineering contract

Jimmy, 2026-09-23: falling in means swimming, not getting caught. The slice swims at the surface only; diving comes later.
Code: `Source/VibeGame/Character/LureSwimMovement.cpp` (movement), `LureWaterVolume.*`, `LureLadder.*`,
`LurePlayerCharacterSwim.cpp`. Tests: `Project.Movement.Swim.*` (`Tests/Movement/LureSwimTest.cpp`).

## Rules
- **Water** is a physics volume with `bWaterVolume` (use `ALureWaterVolume`). The engine switches the character to its
  swimming mode when the capsule **center** enters the volume, and back to falling/walking when it leaves.
- **Tuning** is in DT_Movement: rows `Swim` and `SwimSprint` (speed, acceleration, eye height from the feet, camera blend
  time, noise multiplier for T-014) plus optional columns: `SurfaceFloatDepth` (capsule center below the surface, cm;
  0 on land rows) and the shared climb rule `ClimbMaxHeight` / `ClimbSpeed` (swimming: cm above the water; on land: cm
  above your takeoff, see movement-rules.md). Shipped: swim 170 / sprint-swim 290 cm/s, eyes 18 cm above the water,
  edges up to 60 cm.
- **The one surface rule:** while the row in use has `SurfaceFloatDepth > 0`, a critically damped spring holds the
  capsule center that far below the water surface (a plunge dips, then rises; no bobbing in and out of the water).
  A row with `SurfaceFloatDepth = 0` gets the engine's free 3D swimming, which is where diving will start.
- **Capsule:** swimming uses the Stand capsule (Swim rows' capsule columns equal Stand). With a 180 cm capsule you wade
  (walk) in water up to 90 cm deep and swim deeper than that.
- **Stances:** no crouch or prone in the water. Requests are refused, and the crouch/prone wishes are cleared in the
  predicted move (client and server), so you are standing when you get out. Sprint = sprint-swimming.
- **Getting out:** walk out where the seabed rises (beaches, ramps, shelves). Or press **Jump** facing an edge within
  45 cm: if its top is at most `ClimbMaxHeight` above the water (60 cm; 61 cm is refused) and there is room to stand,
  you climb up and onto it (`MOVE_Custom`, `ELureCustomMovementMode::ClimbOut`). Jump in open water does nothing.
- **Ladders** (`ALureLadder`) allow higher edges: a swimmer in the ladder's grab zone who presses Jump climbs to the
  edge above it (up to the ladder's `MaxClimbHeight`, default 300 cm), whichever way they face.
- **Fishing (T-006):** `ALurePlayerCharacter::IsSwimming()` is true from falling in until standing on land again (the
  climb included); `OnSwimStateChanged(bool)` fires on every machine when it changes. Swimming cancels fishing.
- **Networking:** the swim state is the engine's movement mode (predicted by the owner, simulated by the server from the
  same moves, `ReplicatedMovementMode` to other players). The climb is planned from the same move on both sides.
- **Arms (placeholder):** lowered out of view while swimming (`SwimArmsDrop`, `SwimArmsPitch`). Slot for the real clip:
  `ALurePlayerCharacter::SwimStrokeAnimation` (default `/Game/Art/Characters/FPArms/A_FPArms_SwimStroke`), looped in
  `DefaultSlot` while swimming once it exists; `UFPArmsAnimInstance::bSwimming` for the ABP graph.

## Level builder recipe
Water volume, one per body of water (one big box can cover the whole sea around an island):
- Class `ALureWaterVolume` (Python `unreal.LureWaterVolume`).
- **Location = the center of the water surface**, at exactly the water plane's height (L_PalmKey: z = 0). The box's top
  face is the surface the swimmer floats at, so never put it above or below the visible water.
- `surface_half_size` = half the size in X and Y (cm); `water_depth` = from the surface down to at least the deepest
  seabed (L_PalmKey: 900 for the -800 seabed; deeper is harmless). Yaw and scale work; keep pitch and roll 0.
- Everything walkable inside the box below the surface counts as water: keep dry floors (caves, paths) above z = 0.
- Every water area needs a way out: a beach or shelf that rises above the water, an edge <= 60 cm above the water, or a
  ladder. L_PalmKey: jetty 60 is fine; Dock 70, Point Ledge 70 and Mouth Rocks 80 need a ladder (or a lower edge).

```python
vol = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
    unreal.LureWaterVolume, unreal.Vector(cx, cy, 0.0))
vol.set_editor_property("surface_half_size", unreal.Vector2D(half_x, half_y))
vol.set_editor_property("water_depth", 900.0)
vol.set_water_size(unreal.Vector2D(half_x, half_y), 900.0)  # rebuilds the collision now (same values)
```

Ladder:
- Class `ALureLadder` (Python `unreal.LureLadder`). **Origin = where the ladder meets the water surface, on the face of
  the dock or rock.** Rotate so +X (the arrow) points away from the dock, out over the water.
- `max_climb_height` (default 300 cm) must be at least the edge height above the water. The grab zone reaches 80 cm out
  from the face, 120 cm wide (`grab_zone_half_size`). The greybox board has no collision.

# Surface swimming (T-026): engineering contract

Jimmy, 2026-09-23: falling in means swimming, not getting caught. The slice swims at the surface only; diving comes later.
Code: `Source/VibeGame/Character/LureSwimMovement.cpp` (movement), `LureWaterVolume.*`, `LureLadder.*`,
`LurePlayerCharacterSwim.cpp`. Tests: `Project.Movement.Swim.*` (`Tests/Movement/LureSwimTest.cpp`; networking:
`Tests/Movement/LureSwimNetTest.cpp`).

## Rules
- **Water** is a physics volume with `bWaterVolume` (use `ALureWaterVolume`). The engine switches the character to its
  swimming mode when the capsule **center** enters the volume, and back to falling/walking when it leaves.
- **Tuning** is in DT_Movement: rows `Swim` and `SwimSprint` (speed, acceleration, eye height from the feet, camera blend
  time, noise multiplier for T-014) plus optional columns: `SurfaceFloatDepth` (capsule center below the surface, cm;
  0 on land rows) and the shared climb rule `ClimbMaxHeight` / `ClimbSpeed` (swimming: cm above the water; on land: cm
  above your takeoff, see movement-rules.md). Shipped: swim 170 / sprint-swim 290 cm/s, eyes 18 cm above the water,
  edges up to 60 cm.
  Swim feel columns (optional; missing = the built-in default; T-026 review D3/D4): `SurfaceFloatSettleTime` (0.8 s,
  the plunge and rise), `SwimBrakingDeceleration` (600 cm/s2, coasting to a stop; read from the Swim row),
  `ClimbOutReach` (45 cm, how far in front an edge may be), `ClimbOutLowestTop` (-20 cm: edge tops lower than this
  below the surface are seabed, walk out there; but see "no dead band" below) and `ClimbOutSurfaceTolerance` (30 cm,
  see "only from the surface"). Rows validate them (settle >= 0.05 s, reach, braking and tolerance >= 0, lowest top <= 0).
- **The one surface rule:** while the row in use has `SurfaceFloatDepth > 0`, a critically damped spring holds the
  capsule center that far below the water surface (a plunge dips, then rises; no bobbing in and out of the water).
  A row with `SurfaceFloatDepth = 0` gets the engine's free 3D swimming, which is where diving will start.
- **Capsule:** swimming uses the Stand capsule (Swim rows' capsule columns equal Stand). With a 180 cm capsule you wade
  (walk) in water up to 90 cm deep and swim deeper than that.
- **Stances:** no crouch or prone in the water. Requests are refused, and the crouch/prone wishes are cleared in the
  predicted move (client and server), so you are standing when you get out. Sprint = sprint-swimming.
- **Wading (T-026 QA B2, lead rule):** a crouch or prone whose capsule center would be in the water (feet kept, so
  crouch in water deeper than about 57 cm, prone deeper than about 28 cm with the shipped capsules) is refused BEFORE
  anything changes: no swim event, no wish kept (`IsStanceTooDeepForWater`, checked by the request and by
  `CanCrouchInCurrentState` / `CanProneInCurrentState`, so the server refuses the same flags). The player sees the
  plain-text hint `ALurePlayerCharacter::GetStanceHintText()` ("Too deep to crouch here.", `StanceHintDuration` 2.5 s),
  also for crouch/prone requests while swimming. Known gap: walking crouched or prone from shallower into deeper water
  still enters swimming (stand up, in/out) once the current capsule center goes under.
- **Getting out:** walk out where the seabed rises (beaches, ramps, shelves). Or press **Jump** facing an edge within
  45 cm: if its top is at most `ClimbMaxHeight` above the water (60 cm; 61 cm is refused) and there is room to stand,
  you climb up and onto it (`MOVE_Custom`, `ELureCustomMovementMode::ClimbOut`). Jump in open water does nothing.
- **Stepping out, no dead band (T-026 QA B3/D3):** swimming (with input) into a submerged edge whose top is above the
  feet, at most `MaxStepHeight` (45 cm) up, and high enough that you stand there with the capsule center out of the
  water, steps you out onto it: a short `ClimbOut` climb at the row's `ClimbSpeed` (`FindStepOutPlan`; started inside the
  move, so it is predicted and corrected like any climb; nothing new travels). Lower tops are swum over. Jump climbs
  onto every top higher than feet + `MaxStepHeight` - 10 cm, whatever `ClimbOutLowestTop` says (it can only lower the
  climb range further). So every vertical shelf has a way out; with the shipped data (floating feet at -100) tops from
  -91 to -55 are steps, from -65 up Jump. A climb never reports more upward speed than `ClimbSpeed`, and the engine's
  small step over a deeper rock no longer turns its rise into speed (it used to throw swimmers up to 6 m out of the water).
- **Only from the surface (T-026 D2, lead rule for diving):** climbing out needs the head (capsule top) at most
  `ClimbOutSurfaceTolerance` (30 cm) below the water surface. A floating swimmer's head is always above it, so this only
  matters for rows with `SurfaceFloatDepth = 0`.
- **Teleports (T-026 QA B1):** a teleport (respawn, `Lure.Teleport`) during a climb ends it: `OnTeleported` drops the
  plan and picks the mode for the new place (swimming in water, else falling, which lands at once on ground). On the
  owning client of a server teleport, the correction brings the server's mode and no plan; no further corrections.
- **Ladders** (`ALureLadder`) allow higher edges: a swimmer in the ladder's grab zone who presses Jump climbs to the
  edge above it (up to the ladder's `MaxClimbHeight`, default 300 cm), whichever way they face.
- **Fishing (T-006):** `ALurePlayerCharacter::IsSwimming()` is true from falling in until standing on land again (the
  climb included); `OnSwimStateChanged(bool)` fires on every machine when it changes. Swimming cancels fishing.
- **Networking:** the swim state is the engine's movement mode (predicted by the owner, simulated by the server from the
  same moves, `ReplicatedMovementMode` to other players). Climbs: see "Networking contract" below.
- **Arms (placeholder):** lowered out of view while swimming (`SwimArmsDrop`, `SwimArmsPitch`). Slot for the real clip:
  `ALurePlayerCharacter::SwimStrokeAnimation` (default `/Game/Art/Characters/FPArms/A_FPArms_SwimStroke`), looped in
  `DefaultSlot` while swimming once it exists; `UFPArmsAnimInstance::bSwimming` for the ABP graph.

## Networking contract (T-026 netfix, review N0-N3; tests `Project.Movement.Swim.Net.*`)
- **Only the Jump flag travels.** In the water, `DoJump` (called by `CheckJumpInput` before the owning client saves the
  move) only checks that an edge is there and queues the climb; `PhysSwimming` starts it inside the move. The saved move
  keeps `FLAG_JumpPressed`, and the server plans its own climb from it (`FindClimbOutPlan` on the server's state), so a
  client can't force a climb the server wouldn't allow. Replays plan it again from the same flag. The jump climb onto a
  land ledge (`LedgeClimb`) already starts inside the move (`PhysFalling`).
- **Other players' copies** (simulated proxies) never plan a climb: during `ClimbOut` / `LedgeClimb` they move with the
  replicated velocity and keep the replicated mode until the server's next mode (Walking, or Falling if blocked).
- **Corrections carry the climb state** (`FLureMoveResponseDataContainer`, only in corrections; acks keep the engine's
  size): the server's plan while it climbs, and `TakeoffFeetHeight`. The owning client restores both after the engine
  applies the correction, so its replay continues the server's climb (it used to freeze). `TakeoffFeetHeight` is not
  restored from saved moves on purpose: every replay starts from a correction, and a saved value would come from the
  client's old, wrong path.
- **`OnSwimStateChanged` on the owning client** fires once the correction and its replay have settled, not for the
  modes in between (no in/out pair when a correction replays a climb the client had already finished).
- **Teleports** end a climb where `TeleportTo` runs (`OnTeleported`); the owning client follows through the normal
  correction (test `Project.Movement.Swim.Net.TeleportMidClimbEndsItOnServerAndOwner`). Nothing new travels.

Patterns for new movement features:
1. Anything triggered by an input flag (Jump, a future dive or ladder key) changes the movement mode inside the move
   (`PerformMovement` / `Phys*`), never in `DoJump`, `CheckJumpInput` or an input handler, or the flag never reaches the server.
2. Every `PhysCustom` sub-mode needs a `ROLE_SimulatedProxy` branch: the engine runs `PhysCustom` on other players'
   copies too (`SimulateMovement` -> `MoveSmooth`), and they have none of the owner's private state.
3. Every member that shapes a predicted move (plans, timers, reference heights) is either rebuilt from the saved move
   (its compressed flags) or carried in the correction (`FLureMoveResponseDataContainer`).
Tests reach movement internals through `FLureMovementTestAccess` (`Tests/Movement/LureMovementTestAccess.h`).

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

## Lead decisions on the T-026 open questions (2026-09-23)
1. The QA fixture edits (6 DT_Movement rows, "Swimm" for the unknown-row test) are reviewed by the independent qa-engineer before merge.
2. Swimming keeps the 180 cm standing capsule for the slice: wading up to about 90 cm, and no swimming under docks lower than about 80 cm. Revisit when diving is added.
3. The ledge pull-up needs forward held on the way down (standard mantle feel). This is on Jimmy's playtest list.
4. The overlap with T-006 (LurePlayerCharacter.*, LureMovementTypes.*, DT_Movement.csv, the arms code) is resolved by the lead at merge.

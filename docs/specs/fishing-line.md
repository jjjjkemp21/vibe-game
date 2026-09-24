# Physics fishing line (T-032)

Jimmy (2026-09-23): "make the fishing line a physics based object so the line dangles or floats or tensions realistically
instead of being just a curved line."

The line is **cosmetic**. Every machine simulates its own line from replicated state (rod tip, bobber or fish position,
fishing state, fight tension, water height). It changes no gameplay rule and replicates nothing.

## What you see

| Fishing state | Tension shown (DT_FishingLine) | Look |
|---|---|---|
| Casting | CastTension 0.35 | a little slack behind the flying bobber |
| Waiting | WaitTension 0 | droops from the rod tip and lies on the water up to the bobber |
| Biting | BiteTension 0.6 | pulls nearly straight as the bobber dips |
| Hooked, reel fight | the fight's `Tension01` (tension / line strength) | sags and floats when slack, straightens as tension rises, **exactly straight at the snap threshold (1)** |
| Hooked, no fight (AutoLandDelay debug) | HookedTension 0.8 | nearly straight |
| Snapped | (recoil) | the loose end whips back toward the rod, falls, and is gone after RecoilTime (0.8 s) |
| Landed fish hanging (T-030) | (hanging) | the fish swings like a pendulum under the rod tip |

Measured straightness (15 m line, rod tip 3 m up; `Project.Fishing.Line.Sim.StraightWhenTaut`): the largest distance from
straight at tension 0 / 0.25 / 0.5 / 0.75 / 0.9 / 1 is 215 / 141 / 82 / 30 / 7.6 / 0 cm.

## Parts

| Part | File | Role |
|---|---|---|
| `FLureFishingLineRow` | `Source/VibeGame/Fishing/FishingLineTypes.h` | DT_FishingLine row (all physics tuning) |
| `FLureFishingLineRules` | same | pure rules: tautness, target length, length follow, state tension, float amount |
| `FLureLineSim` | `Source/VibeGame/Fishing/FishingLineSim.h` | the rope solver: plain C++, no UObjects, allocation-free after `Init` |
| `ULureFishingLineComponent` | `Source/VibeGame/Fishing/LureFishingLineComponent.h` | owner API, own late tick, water lookup, drawing, snap, hanging actor |
| `ULureLineSegmentComponent` | same header | a spline mesh that never has a physics state (one per segment) |
| data | `data/tables/DT_FishingLine.csv` -> `/Game/Data/DT_FishingLine` | settings: `ULureFishingSettings::FishingLineTable` + `FishingLineRow` ("Default") |

Unchanged from T-006 and still used: segment count, pixel width and minimum width (DT_Fishing `LineSegments`,
`LinePixelWidth`, `LineMinWidth`); mesh, material and colour (Lure Fishing settings). The width rule keeps the line >= 2 px
at 1080p at every distance (designer B-S3), so it stays visible at 10-20 m.

## The simulation (FLureLineSim)

`Segments + 1` points; point 0 is the rod tip, the last is the end (bobber, fish). Each frame runs
`ceil(DeltaTime x SubstepRate)` sub-steps (120 Hz; at most MaxSubsteps 8, so a long hitch runs in slow motion instead of
exploding). Each sub-step:

1. **Verlet integration** (time-corrected for changing sub-step lengths): gravity, and air drag or water drag (on and under
   the water).
2. **Pinned ends** move linearly from last frame's position to this frame's, so a fast rod never tears the line.
3. **Iterations passes (6)** of:
   - one-sided distance constraints: a segment can go slack but never stretch (a line can't push, so a slack line can bunch
     up on the water);
   - **tethers**: no point farther from a pinned end than the line between them. With both ends pinned, each point is
     projected **exactly into the lens** where the two balls overlap, in one step. That is what makes a taut line exactly
     straight: projecting onto one ball and then the other (the naive way) converges so slowly when the balls only touch
     that gravity leaves a ~15 cm sag on a "fully taut" line. A free end only has the ball around the tip.
4. **Float**: points under `WaterZ + FloatHeight` rise toward it by `Float` (0..1) per sub-step and lose their vertical speed
   in proportion (no bounce). Float = FloatStrength x (1 - tautness), so a taut line cuts straight into the water. A hanging
   fish (heavy free end) does not float; a snapped end does.

Robustness: non-finite inputs are replaced by the last good ones; coordinates are clamped to +-1000 km and lengths to 1 km;
a pinned line is never shorter than the straight distance at either end of the frame; a tip or end that jumps farther than
TeleportDistance (15 m) in one frame resets the line; a non-finite state (never seen) resets it.

## Tension, slack and length

- `tautness = 1 - (1 - Tension01)^TautExponent` (Tension01 clamped 0..1; exponent 3).
- Target length = `chord x (1 + Slack x (1 - tautness))`; Slack = DT_FishingLine SlackShare (0.05) unless `SetSlack`.
- The line's length follows the target: **longer at once** (slack appears as fast as a fish swims at you), **shorter
  smoothly** (LengthResponse 6/s: it straightens over ~0.3-0.5 s), never shorter than the chord.
- Sag grows with the square root of the slack, so the line reads slack at low tension and snaps straight near the threshold.

## The rod tip (the line never lags or cuts through the rod)

The component ticks in **TG_PostUpdateWork**, after the engine updates cameras (`LevelTick.cpp`: `UpdateCameraManager`
runs before `TG_PostUpdateWork`) and animation. The fishing component binds `SetStartProvider(GetLineStart)`, so the line
asks for the rod tip **as drawn this frame** (first-person corrected) when it simulates. Before T-032 the start was read in
the fishing component's tick (TG_PrePhysics), a frame behind the camera: when the view turned, the line left the rod short of
its tip. The first point is pinned exactly there every frame.

## Water

The fishing component passes the water height it already has: `NetState.BobberRest.Z` when the bobber is on water
(`SetWaterSurfaceZ`). Without it (`ClearWaterSurfaceZ`, e.g. a bobber on land) the line looks it up itself with
`FLureFishingSpots::FindWaterSurfaceZ` (T-026 water volumes, then actors tagged `Lure.Water`, then the fallback sea level),
at its end, again only after the end moved WaterRefreshDistance (5 m). One water height per line.

## API for other lanes

```cpp
ULureFishingLineComponent* Line = Fishing->GetLine();   // null only if the line mesh is missing

// The fishing component already does this every frame while a line is out (UpdateVisuals):
Line->SetViewer(ViewLocation, Fov);
Line->SetWidthRule(Row.LinePixelWidth, Settings->LineReferenceScreenWidth, Row.LineMinWidth);
Line->SetTension(FLureFishingLineRules::StateTension(State, FightNet.bActive, FightNet.GetTension01(), Line->GetTuning()));
Line->SetWaterSurfaceZ(BobberRest.Z);                     // or ClearWaterSurfaceZ()
Line->SetEndpoints(GetLineStart(), BobberOrFishPoint);    // starts the line on the first call
Line->Hide();                                             // when the line comes in (a recoil or a hanging actor stays)

Line->Snap();                                             // HandleStateChanged calls it on a Snapped result
FVector End = Line->GetEndPoint();                        // where the line ends now (pinned or swinging)
FVector Up = Line->GetEndDirection();                     // unit vector from the end up the line
```

- **T-028 (fight)**: keep feeding `FightNet.GetTension01()` through `StateTension`; 1 = the snap threshold = straight. If the
  new fight has a notion of paid-out slack, `SetSlack(extra share)` shows it. The old `SetLine(..., Sag, ...)` still works
  (Sag becomes slack) so an older call site merges cleanly, but the tension path is the intended one.
- **T-029 (visible fish)**: put the fish where the line ends: `GetEndPoint()` (pinned to the bobber-on-the-fish in a fight),
  facing `-GetEndDirection()` (away from the rod).
- **T-030 (landed fish)**: replace the own pendulum with
  ```cpp
  Line->AttachEndActor(FishActor, HangLength /*cm*/, MouthOffset /*actor space*/, /*bOrientAlongLine*/ true);
  ...
  Line->DetachEndActor();   // grabbed into the hand: the line is gone (or back to its end if a line is out)
  ```
  The end lets go and swings under the rod tip; a longer line is reeled up smoothly, a shorter one drops. Each frame the
  actor is moved so `MouthOffset` sits on the end, +X up the line (head up), keeping its facing. `Hide()` does not remove
  a hanging actor. Rules: attach on **every** machine (it is cosmetic); the hanging actor must **not replicate its movement**
  and must not simulate physics; `AddEndVelocity(v)` makes it flop. Destroying the actor ends the line.

## Cost (Project.Fishing.Line.Sim.Cost, Component.AllocationsAndCost)

- Simulation: 2.9 us per 60 fps frame at 12 segments (6.1 at 24, 16 at 64). The whole component with 12 drawn segments:
  ~5 us on the game thread in a test world (no render proxies there; in a rendered game each segment update also sends one
  small render command).
- Nothing runs while no line is out: the tick is off (`Hide` turns it off; tests check it).
- Our code allocates nothing per frame (checked with the engine's game-thread allocation hook). `Init`/`Setup` allocate
  once.
- `ULureLineSegmentComponent` never creates a physics state. A plain spline mesh rebuilds its collision body on every
  update in worlds with trace collision (editor-created and test worlds: `UWorld::CreateWorld` enables it; PIE and packaged
  games don't), which cost ~1.2 ms per segment per frame.

## Data (DT_FishingLine)

One row per kind of line; "Default" today. Columns: SubstepRate, MaxSubsteps, Iterations, GravityScale, AirDrag, WaterDrag
(simulation); SlackShare, TautExponent, LengthResponse, Cast/Wait/Bite/HookedTension (tension); FloatStrength, FloatHeight,
WaterRefreshDistance (water); RecoilSpeed, RecoilTime, RecoilLengthShare (snap); HangEndMass, HangDrag (hanging);
TeleportDistance. Units and ranges: the header comments; `Validate` enforces them. The struct defaults are the "Default"
row (a test compares them). Missing asset (lanes, until the editor-operator imports it): the built-in row, logged once at
**Log** level (a warning would fail whichever test first draws a line). A bad row in an imported table: a warning.
Next step when line gear gets a look: a DT_Gear line column naming a DT_FishingLine row (e.g. a floating braid).

## Tests (Project.Fishing.Line.*)

Rules.TensionStraightensTheLine, Data.SourceRowsValid, Data.ValidateRejectsBadRows, Sim.SagsWhenSlack (catenary within
15 %), Sim.StraightWhenTaut, Sim.FloatsOnTheWater, Sim.LengthConserved, Sim.NoNaNAtExtremeInputs,
Sim.SubstepsAndTeleports, Sim.RecoilAndSwing, Sim.AllocationFreeSteadyState, Sim.Cost, Component.EndpointApi,
Component.HangingActorSwings, Component.SnapRecoil, Component.AllocationsAndCost, Fishing.CastWaitFightSnap (cast, wait,
fight on a weak line, snap and recoil in a real character), plus the T-006 Line.AtLeastTwoPixels.

## Known limits and follow-ups

- No ground collision: a slack line can pass through a dock or a beach between the rod and the water (a cheap height check
  at a few points is the likely fix if a playtest shows it).
- One water height per line (a line spanning two water levels floats at the end's).
- A player who turns away from the bobber sees the line run back from the tip over the rod (no wrapping around the tip).
- Other players' lines start from an estimated rod tip (no third-person rod yet; unchanged from T-006).
- `FLureFight::LineSag`, DT_FishFight `TautTension` and DT_Fishing `LineSag` no longer shape the line (only the old
  `SetLine` path reads a sag); T-028 can drop them.
- `/Game/Data/DT_FishingLine` must be imported by the editor-operator after the merge (until then the built-in row is used).

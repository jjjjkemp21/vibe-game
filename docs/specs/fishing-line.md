# Physics fishing line (T-032, T-032b)

Jimmy (2026-09-23): "make the fishing line a physics based object so the line dangles or floats or tensions realistically
instead of being just a curved line."

The line is **cosmetic**. Every machine simulates its own line from replicated state (rod tip, bobber or fish position,
fishing state, fight tension, water height). It changes no gameplay rule and replicates nothing.

T-032b (2026-09-24) added three things, each covered below: the line **collides** with what blocks a cast (it lies on the dock
and bends over its edges; "Collision"), a landed fish **hangs below the rod tip** (hang reel + swing guard; "Hanging actor"),
and the line **pulls straight under tension** (fight tension mapping, carried rest length, per-sub-step rest lerp;
"Tension, slack and length").

## What you see

| Fishing state | Tension shown (DT_FishingLine) | Look |
|---|---|---|
| Casting | CastTension 0.35 | a little slack behind the flying bobber |
| Waiting | WaitTension 0 | droops from the rod tip and lies on the water up to the bobber |
| Biting | BiteTension 0.6 | pulls nearly straight as the bobber dips |
| Hooked, reel fight | `FLureFight::LineTension` = clamp(`Tension01` / DT_FishFight `TautTension` 0.3, 0, 1) | sags and floats when slack, **straight once the fish pulls with 30 % of the line's strength** or more |
| Hooked, no fight (AutoLandDelay debug) | HookedTension 0.8 | nearly straight |
| Snapped | (recoil) | the loose end whips back toward the rod, falls, and is gone after RecoilTime (0.8 s) |
| Landed fish hanging (T-030) | (hanging) | the fish is reeled up under the rod tip and swings there like a pendulum, never over the tip |

In every state but hanging, the line lies on docks, posts and rocks and bends over their edges (none of it inside them).

Measured straightness (15 m line, rod tip 3 m up; `Project.Fishing.Line.Sim.StraightWhenTaut`): the largest distance from
straight at tension 0 / 0.25 / 0.5 / 0.75 / 0.9 / 1 is 215 / 141 / 82 / 30 / 7.6 / 0 cm.

## Parts

| Part | File | Role |
|---|---|---|
| `FLureFishingLineRow` | `Source/VibeGame/Fishing/FishingLineTypes.h` | DT_FishingLine row (all physics tuning) |
| `FLureFishingLineRules` | same | pure rules: tautness, target length, length follow, carry, hang reel, state tension, float amount |
| `FLureLineSim` | `Source/VibeGame/Fishing/FishingLineSim.h` | the rope solver: plain C++, no UObjects, allocation-free after `Init` |
| `FLureLineColliders` | same header | the solids a line can't pass through (planes of boxes/hulls, spheres, capsules), plain data |
| `ULureFishingLineComponent` | `Source/VibeGame/Fishing/LureFishingLineComponent.h` | owner API, own late tick, water lookup, collider gather, drawing, snap, hanging actor |
| `ULureLineSegmentComponent` | same header | a spline mesh that never has a physics state (one per segment) |
| `FLureFight::LineTension` | `Source/VibeGame/Fishing/FishFight.h` | a fight's tension as the line shows it (see "Tension, slack and length") |
| data | `data/tables/DT_FishingLine.csv` -> `/Game/Data/DT_FishingLine` | settings: `ULureFishingSettings::FishingLineTable` + `FishingLineRow` ("Default") |

Unchanged from T-006 and still used: segment count, pixel width and minimum width (DT_Fishing `LineSegments`,
`LinePixelWidth`, `LineMinWidth`); mesh, material and colour (Lure Fishing settings). The width rule keeps the line >= 2 px
at 1080p at every distance (designer B-S3), so it stays visible at 10-20 m.

## The simulation (FLureLineSim)

`Segments + 1` points; point 0 is the rod tip, the last is the end (bobber, fish). Each frame runs
`ceil(DeltaTime x SubstepRate)` sub-steps (120 Hz; at most MaxSubsteps 8, so a long hitch runs in slow motion instead of
exploding). Once per frame, before the sub-steps, the solids near the line are picked (see "Collision"). Each sub-step:

1. **Verlet integration** (time-corrected for changing sub-step lengths): gravity, and air drag or water drag (on and under
   the water). A hanging free end's upward speed is capped by the **swing guard** (see "Hanging actor").
2. **Pinned ends** move linearly from last frame's position to this frame's, so a fast rod never tears the line.
3. **Rest length** moves linearly from last frame's length to this frame's (`FLureLineSimInput::RestLength`); a pinned line
   is clamped to the straight distance between its ends in this sub-step (see "Tension, slack and length").
4. **Float**: points under `WaterZ + FloatHeight` rise toward it by `Float` (0..1) per sub-step and lose their vertical speed
   in proportion (no bounce). Float = FloatStrength x (1 - tautness), so a taut line cuts straight into the water. A hanging
   fish (heavy free end) does not float; a snapped end does. Float runs before the constraints and lifts at most
   240 cm/s, so floating never stretches the line: a slack line to a deep end (a diving fish) is pulled under near the end
   (T032-O2: segments stay within 0.5 % of their length at 4 m deep; before, 89 %).
5. **Iterations passes (6)** of:
   - one-sided distance constraints: a segment can go slack but never stretch (a line can't push, so a slack line can bunch
     up on the water);
   - **tethers**: no point farther from a pinned end than the line between them. With both ends pinned, each point is
     projected **exactly into the lens** where the two balls overlap, in one step. That is what makes a taut line exactly
     straight: projecting onto one ball and then the other (the naive way) converges so slowly when the balls only touch
     that gravity leaves a ~15 cm sag on a "fully taut" line. A free end only has the ball around the tip.
   - with solids near: **collision** (points, then segments; see "Collision"). One more point pass follows the last pass.
   A pinned line within 1e-6 of the straight distance counts as exactly that long (T032-B1).
6. With solids near: **contact friction** once (see "Collision").

Robustness: non-finite inputs are replaced by the last good ones; coordinates are clamped to +-1000 km and lengths to 1 km;
a pinned line is never shorter than the straight distance between its ends in any sub-step; a tip or end that jumps farther
than TeleportDistance (15 m) in one frame resets the line; a non-finite state (never seen) resets it.

## Tension, slack and length

- `tautness = 1 - (1 - Tension01)^TautExponent` (Tension01 clamped 0..1, +Inf = 1, NaN = 0; exponent 3).
- **Fight tension (T-032b)**: in a reel fight the fishing component passes `FLureFight::LineTension(Tension01, Tuning)` =
  `clamp(Tension01 / TautTension, 0, 1)` (DT_FishFight `TautTension` 0.3; NaN = 0, +Inf = 1, a bad TautTension counts as
  0.3) through `StateTension`, not the raw share of the line's strength. A fish pulling with 30 % of the strength or more
  pulls the line straight; below that it sags. (Before, the line was only straight at the snap threshold, so it sagged
  through most of a fight: playtest shots at 43-84 % tension.)
- Target length = `chord x (1 + Slack x (1 - tautness))`; Slack = DT_FishingLine SlackShare (0.05) unless `SetSlack`.
- **Carried length (T-032b, `CarryRestLength`)**: each frame, before following the target, a pinned line's length is carried
  to the ends' new distance with the same share of slack: `rest = chord x clamp(rest / last chord, 1, 2)`. So ends that
  close in (reeling, a fish swimming at you) leave no extra line behind, and ends that part don't pull the line straight.
  Skipped (length unchanged) on a new line (last chord < 1 cm, or 0 after starting, stopping, hanging or a recoil) and for
  non-finite input.
- The line's length then follows the target (`TightenRestLength`): **longer at once** (slack appears as fast as a fish swims
  at you), **shorter smoothly** (LengthResponse 6/s, exponential), never shorter than the chord. Toward a (nearly) straight
  target the line also straightens at least at a steady sag rate: with the full SlackShare it goes straight in
  **StraightenTime 0.4 s** (a 15 m line: 216 cm of sag -> straight at 0.38 s) and stops at the straight line without
  whipping past it. (An exponential alone slows down while the line is still slack, so a fast one whips up past the chord
  and sags back; the steady rate fades out by a target sag of 3 cm and eases in no harder than gravity, T032-O3.) Within
  1e-5 of the target the length is exactly the target, and a shrinking length always moves at least one float step
  (float steps would otherwise stall just above it and leave a visible sag at the snap threshold, T032-B1).
- **Per-sub-step rest lerp (T-032b)**: the sim reaches each frame's length linearly over the frame's sub-steps (from the
  last Step's length), and a pinned line is clamped per sub-step to the straight distance between its ends there. Before, one
  whole-frame clamp to the longer of the frame's two chords left a frame's worth of sag every frame the ends closed in, so a
  reeled line never pulled straight. The lerped length is >= the lerped chord >= the chord of the lerped ends (triangle
  inequality), so the per-sub-step clamp is only a safety net; a large one-frame change (-30 %) moves the line smoothly.
- Sag grows with the square root of the slack, so the line reads slack at low tension and snaps straight near the threshold.

## Collision (T-032b)

Jimmy's playtest shot 24: a cast from the dock edge left the middle of the line inside the deck. The line now lies on and
bends around **exactly what blocks a cast**.

**What is gathered** (`ULureFishingLineComponent::UpdateColliders` -> `AddCollidersOf`, into `FLureLineColliders`):
- One `OverlapMultiByChannel` on `FLureFishingSpots::CastChannel` over the line's box (tip, end, every point) grown by
  CollisionQueryMargin + CollisionRadius, ignoring the owner and a hanging actor. A component is kept only if
  `FLureFishingSpots::BlocksCast` accepts it (the rule the cast trace uses), so water, pawns and creatures are never solid.
- Its **simple collision** (`UBodySetup::AggGeom`), placed like the physics places it: box elements (any rotation and scale,
  exact), convex elements (as planes; a hull without cooked planes falls back to its box), sphere and capsule elements
  (scaled as the physics scales them: a sphere by its smallest axis scale, a capsule's radius by its largest XY scale).
  Instanced static meshes: only the overlapped instance (the overlap's item index). Skinned meshes are skipped.
- A body with **complex collision only** (`CTF_UseComplexAsSimple` or no simple elements): its world bounds box stands in,
  flagged `bBoundsOnly`, and is ignored while the rod tip or a pinned end is inside it (grown by CollisionRadius): a boat or
  an arch you stand in or fish through is far smaller than its box.
- **No body setup** (a landscape's heightfield): not collided.

**When it is gathered again**: when the line's box grown by min(CollisionQueryMargin / 2, 50 cm) + CollisionRadius leaves the
last query box, or every CollisionRefreshTime (0.2 s) while any gathered component is Movable (a boat). Nothing else
queries the world: a line resting still costs no query. Reserve covers about one pier (512 planes, 64 hulls, 32 rounded).

**Solve** (`FLureLineSim`):
- Once per Step, `FindNearSolids` keeps the solids whose bounds are within reach of the line's box (its points, the tip,
  a pinned end), reach = CollisionRadius + 0.5 cm touch band + 50 cm + how far the line can get this frame (ends' moves,
  change in length, the points' speed). At most 64 hulls/boxes and 64 spheres/capsules per Step (the first ones in the
  colliders' order); the rest are ignored that Step.
- Each solver pass ends with:
  1. `CollidePoints`: every free point outside every solid grown by CollisionRadius, judged along its path this sub-step
     (from `SubstepStart`), so a fast point can't pass through a thin plank. A point is pushed out through the face it came
     in by; the nearest face is used only if the point was within the touch band of it before and after (sliding along a
     face, over a seam or a 3 mm step). Rounded solids: bisection to the entry point. A path start or end within 1e-3 cm
     inside a face counts as on it.
  2. `CollideSegments`: a segment whose ends are both outside a solid but that dips into it (a V over an edge) is lifted
     over the edge it cuts, exactly (the V's bottom). Segments with an end inside are left to the point pass (a pinned end
     a few mm inside a solid used to throw its neighbour a whole segment).
- One more `CollidePoints` after the last pass: points have the last word, so a taut line wrapped over an edge stretches a
  little instead of cutting through it.
- Every collision move is **inelastic** (`AbsorbMove`): it cancels the speed into the solid and never adds speed along the
  move (no pop, no bounce, no hop over small steps).
- **Contact friction** (`ApplyContactVelocity`, once per sub-step): a free point touching a solid keeps
  `exp(-GroundFriction x h)` of its sliding speed, so line comes to rest on a dock instead of skating.

**Hanging lines don't collide**: the colliders are emptied while an actor hangs (`GetColliders()` is empty) and gathered
afresh when the line pins again. A landed fish can start under the deck (`ComputeBobberPose` puts the bobber at
Player + Dir x LineOut, LineOut <= LandDistance 150 cm at landing), and the hang reel would trap it there.

## Hanging actor (T-030, reel and swing guard T-032b)

- `AttachEndActor` frees the end at the actor's hook point. The line's length starts at the tip-to-hook distance and is
  **reeled** toward HangLength (`ReelInRestLength`): a shorter line drops to HangLength at once; a longer one shrinks at
  `min(HangReelSpeed, sqrt(2 x a x (rest - HangLength)))` cm/s with a = half of gravity (980 x GravityScale / 2; no easing
  without gravity). It arrives exactly at HangLength, slowing on the way, and the actor (which gravity slows faster than
  that) never overtakes the reel and flies past the rod tip.
- **Swing guard** (`FLureLineSimInput::MaxSwingDeg` = HangMaxSwingDeg 70): per sub-step the free end's upward speed is
  capped at what gravity lets it coast up to `rest x cos(HangMaxSwingDeg)` below the tip, plus what the tip itself rises
  this sub-step. Upward only: sideways and downward motion are untouched (a cap on the whole speed would also kill the
  sideways lag of a hanging fish behind a moving rod). A jerked, reeled or pitched rod never throws the fish over the tip.
- The swing settles in about 10 s (HangDrag 0.4), so `Component.HangingActorSwings` runs 840 frames (it was 360 before the
  reel took its ~3.6 s).
- **Side-on (T-043)**: attached with `bFaceViewer` (the catch link does, for every landed fish on the rod's line), the actor
  turns about the line so its right side (+Y) faces the viewer (`SetViewer`, else the local camera): each frame it closes
  `1 - exp(-dt / HangFaceTime)` of the angle (HangFaceTime 0.25 s; 0 = at once). The nose stays up the line and the mouth on
  the line's end. One rule for both hangs: `FLureFishingLineRules::SideOnHangRotation`, also used by the fallback pendulum
  (GiveFish with no line out). Cosmetic and local: each machine turns the fish toward its own camera.

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

// The fishing component already does this every frame while a line is out (UpdateVisuals), and the first two also while
// a fish hangs on it with no line out (T-034):
Line->SetViewer(ViewLocation, Fov);
Line->SetWidthRule(Row.LinePixelWidth, Settings->LineReferenceScreenWidth, Row.LineMinWidth);
Line->SetTension(FLureFishingLineRules::StateTension(State, FightNet.bActive,
    FLureFight::LineTension(FightNet.GetTension01(), GetFightTuning()), Line->GetTuning()));
Line->SetWaterSurfaceZ(BobberRest.Z);                     // or ClearWaterSurfaceZ()
Line->SetEndpoints(GetLineStart(), BobberOrFishPoint);    // starts the line on the first call
Line->Hide();                                             // when the line comes in (a recoil or a hanging actor stays)

Line->Snap();                                             // HandleStateChanged calls it on a Snapped result
FVector End = Line->GetEndPoint();                        // where the line ends now (pinned or swinging)
FVector Up = Line->GetEndDirection();                     // unit vector from the end up the line
```

- **T-028 (fight)**: keep feeding the fight tension through `FLureFight::LineTension` and `StateTension`; 1 = straight. If
  the fight has a notion of paid-out slack, `SetSlack(extra share)` shows it. The old `SetLine(..., Sag, ...)` still works
  (Sag becomes slack) so an older call site merges cleanly, but the tension path is the intended one.
- **T-029 (visible fish)**: put the fish where the line ends: `GetEndPoint()` (pinned to the bobber-on-the-fish in a fight),
  facing `-GetEndDirection()` (away from the rod).
- **T-030 (landed fish)**: replace the own pendulum with
  ```cpp
  Line->AttachEndActor(FishActor, HangLength /*cm*/, MouthOffset /*actor space*/, /*bOrientAlongLine*/ true, /*bFaceViewer*/ true);
  ...
  Line->DetachEndActor();   // grabbed into the hand: the line is gone (or back to its end if a line is out)
  ```
  The end lets go and swings under the rod tip; a longer line is reeled up at HangReelSpeed, a shorter one drops (see
  "Hanging actor"). Each frame the actor is moved so `MouthOffset` sits on the end, +X up the line (head up), keeping its
  facing, or with `bFaceViewer` turning its side to the viewer (T-043). `Hide()` does not remove a hanging actor. Rules: attach on **every** machine (it is cosmetic); the hanging actor
  must **not replicate its movement** and must not simulate physics; `AddEndVelocity(v)` makes it flop. Destroying the
  actor ends the line.
- **Viewer (T-034)**: `SetViewer` holds until the line stops (`Hide` of a pinned line, the end of a recoil, the hanging actor
  let go); a new line then draws for the local camera until someone sets the viewer again. Whoever draws a line keeps its
  viewer current **every frame it is drawn**: a stale viewer sizes the widths for the wrong distance (the GiveFish bug: the
  eye of the last cast 10 m away drew the hang line ~2.9 cm thick instead of ~0.3 cm).
- **Collision**: nothing to call. To make something solid for the line, give it simple collision that blocks
  `CastChannel` (the same thing that stops a cast). `GetColliders()` shows what the line gathered (tests, debugging).

## Cost (Project.Fishing.Line.Sim.Cost, Component.AllocationsAndCost, Collision.Allocations)

- Simulation: 2.9 us per 60 fps frame at 12 segments (6.1 at 24, 16 at 64). The whole component with 12 drawn segments:
  ~5 us on the game thread in a test world (no render proxies there; in a rendered game each segment update also sends one
  small render command).
- Collision: a line draped over 4 solids, 12 segments: ~7.6 us per 60 fps frame (the Collision.Allocations info line). The
  overlap query runs only when the line leaves its query box, or every 0.2 s near a Movable solid.
- Nothing runs while no line is out: the tick is off (`Hide` turns it off; tests check it).
- Our code allocates nothing per frame (checked with the engine's game-thread allocation hook), with or without solids.
  `Init`/`Setup` and the collider reserve allocate once; a gather frame only grows the reserve past about one pier.
- `ULureLineSegmentComponent` never creates a physics state. A plain spline mesh rebuilds its collision body on every
  update in worlds with trace collision (editor-created and test worlds: `UWorld::CreateWorld` enables it; PIE and packaged
  games don't), which cost ~1.2 ms per segment per frame.

## Data (DT_FishingLine)

One row per kind of line; "Default" today. Units, ranges and exact meaning: the header comments in `FishingLineTypes.h`;
`Validate` enforces them. The struct defaults are the "Default" row (a test compares them).

| Group | Column (Default) | Meaning |
|---|---|---|
| Simulation | SubstepRate 120 | sub-steps per second (30..1000) |
| | MaxSubsteps 8 | most sub-steps per frame; a longer frame runs in slow motion |
| | Iterations 6 | constraint passes per sub-step (stiffer, costlier) |
| | GravityScale 1 | x 980 cm/s2 |
| | AirDrag 2, WaterDrag 10 | share of speed lost per second in the air / on or under the water |
| Tension | SlackShare 0.05 | extra line (share of the chord) at no tension |
| | TautExponent 3 | tautness = 1 - (1 - Tension01)^TautExponent |
| | LengthResponse 6 | per second, how fast the length shrinks toward a tighter target |
| | StraightenTime 0.4 | s for a line with the full SlackShare to go straight at tension 1 |
| | CastTension 0.35, WaitTension 0, BiteTension 0.6, HookedTension 0.8 | tension shown per fishing state |
| Water | FloatStrength 0.5 | 0..1 per sub-step: how fast line under water rises (x (1 - tautness)) |
| | FloatHeight 0.4 | cm above the water the floating line rests |
| | WaterRefreshDistance 500 | cm the end moves before the water height is looked up again (no owner height) |
| Snap | RecoilSpeed 2500 | cm/s the snapped end flies back |
| | RecoilTime 0.8 | s the recoil plays |
| | RecoilLengthShare 0.3 | the snapped line shrinks to this share of its length |
| Hanging | HangEndMass 25 | a hanging actor weighs this many line points |
| | HangDrag 0.4 | share of its speed a hanging actor loses per second |
| | **HangReelSpeed 500** | cm/s, fastest reel-up of a hanging actor (slows at g/2 near HangLength) |
| | **HangMaxSwingDeg 70** | degrees (10..90), highest swing from straight under the tip (90 = level) |
| | HangFaceTime 0.25 | s (0..5), how fast a hanging fish turns its side to the viewer (T-043; 0 = at once) |
| Safety | TeleportDistance 1500 | cm; a tip or end jump farther than this in one frame resets the line |
| Collision | **CollisionRadius 1** | cm (0..50) the line keeps from solid surfaces (0 = on the surface) |
| | **GroundFriction 8** | share of its sliding speed line touching a solid loses per second |
| | **CollisionQueryMargin 300** | cm (>= 10) the query box reaches past the line's bounds |
| | **CollisionRefreshTime 0.2** | s (>= 0.02) between gathers while a Movable solid is near |

Bold = new in T-032b. Missing asset (lanes, until the editor-operator imports it): the built-in row, logged once at
**Log** level (a warning would fail whichever test first draws a line). A bad row in an imported table: a warning.
Next step when line gear gets a look: a DT_Gear line column naming a DT_FishingLine row (e.g. a floating braid).

## Tests (Project.Fishing.Line.*)

`Source/VibeGame/Tests/Fishing/FishingLineTest.cpp`: Rules.TensionStraightensTheLine, Data.SourceRowsValid,
Data.ValidateRejectsBadRows, Sim.SagsWhenSlack (catenary within 15 %), Sim.StraightWhenTaut, Sim.FloatsOnTheWater,
Sim.LengthConserved, Sim.NoNaNAtExtremeInputs, Sim.SubstepsAndTeleports, Sim.RecoilAndSwing, Sim.AllocationFreeSteadyState,
Sim.Cost, Component.EndpointApi, Component.HangingActorSwings, Component.SnapRecoil, Component.AllocationsAndCost,
Fishing.CastWaitFightSnap (cast, wait, fight on a weak line, snap and recoil in a real character), plus the T-006
Line.AtLeastTwoPixels.

`FishingLineFixesTest.cpp` (after QA, T032-B1, O1-O3): Fixes.FollowEndsExactlyOnTarget,
Fixes.SolverTautAFloatStepAboveTheChord, Fixes.StraightensWithinHalfASecond, Fixes.InfiniteTensionIsTaut,
Fixes.DeepEndDoesNotStretch (1 m and 4 m deep, and a fish diving to 4 m through the component).

T-032b (namespaces `LureLineCollisionTests`, `LureLineHangTests`, `LureLineTautTests`):
- `FishingLineCollisionTest.cpp`, `Project.Fishing.Line.Collision.*` (14): Colliders.BoxPlanes, Sim.DrapesOverBox,
  Sim.WrapsAroundPost, Sim.RoundedSolids, Sim.NoTunnel, Sim.SegmentCornerCut, Sim.SeamSlide, Sim.ContactFriction,
  Sim.NoDetachedSegments, Sim.PinnedEndInsideStaysCalm, Sim.BoundsOnlySkippedAroundTip, Component.CollidesWithDock,
  Component.HangingLineIgnoresSolids, Allocations (with the cost info line). They judge the line against solids built
  independently from the known shapes, with a control run without colliders that must show the problem.
  Component.CollidesWithDock (several solids, the line sliding over the deck's edge): points never inside (0.01 cm);
  segments keep at least half the CollisionRadius clear of the real surfaces every frame, and are within 0.1 cm of the
  grown shapes once settled. For a frame a segment can be ~0.12 cm into the grown deck: the lift's end resting on the deck
  slides along it instead of lifting, so one pass clears only part of an edge cut (it converges over the passes).
- `FishingLineHangTest.cpp`, `Project.Fishing.Line.Hang.*`: Rules.ReelInRestLength, Sim.SwingGuard,
  Component.LandedFishBelowTip.
- `FishingLineTautTest.cpp`, `Project.Fishing.Line.Taut.*`: Rules.CarryRestLength, Sim.ClosingEndsStayStraight,
  Sim.RestLerpNoPop, Component.StraightensUnderTension (43 % and 84 % of the strength).
- `FishingLineFastTurnTest.cpp`, `Project.Fishing.Line.FastTurn.*` (T-041c): Slack540, Slack1080, Taut1080. End pinned on
  the water 12 m out, tip swept on a 150 cm arc (200 cm up) for 0.3 s then held 1 s, dt 1/60, built-in row: no segment
  points back along the chord and no segments cross in XY on any frame; stretch <= 3 %.

`FishingLineGiveFishHangTest.cpp` (T-034), `Project.Fishing.Line.GiveFishHang.WidthMatchesRule`: cast, reel in, walk 10 m,
land a fish with no line out (GiveFish's path); every point's width = the width rule for the eye now (10 %), and each drawn
segment is that thick.

`FishingLineHangNoStubTest.cpp` (T-041b), `Project.Fishing.Line.Hang.NoStubPastMouth`: a fish hung on the rod's line
(cast, reel in, land), 6 s of reel-up, swing and view sweeps; no line point and no sampled point of a drawn segment lies past
the mouth into the fish by more than 1 cm (measured: 0.000 cm).

`FishingLineHangSideOnTest.cpp` (T-043), `Project.Fishing.Line.Hang.SideOn.*`: Rule (smoothing share, snap at 0, nose up the
line, viewer on the axis, bad input), ComponentViewers (9 viewers around and above a hanging actor: side within 3 deg after 2 s,
measured 0.03; the first frame turns only part of the way; mouth on the end 0.000 cm; without bFaceViewer the side stays),
RealCatch (cast, reel in, land on another player's rod; the local viewer stands at 4 spots on the dock, two of them on opposite
sides, so a fixed facing can't pass: settled side error <= 5 deg, measured 0.13; mouth within 1 cm of the end, measured 0.04).

QA's own suite: Project.Fishing.Line.QA.* (`QAFishingLineTest.cpp`).

## Known limits and follow-ups

- Collision (T-032b):
  - A slab crossed straight through by a whole segment in one step (both ends outside, only after a reset or a teleport)
    is not lifted; thin-plank tunnelling by fast points is handled by the path check.
  - Landscapes (heightfields, no body setup) are not collided: a line can pass through terrain.
  - Complex-only meshes collide as their bounds box, skipped while the tip or a pinned end is inside it.
  - Movable solids (boats) are gathered again every 0.2 s, so a fast boat can pass through a line between gathers.
  - Hanging lines don't collide (a landed fish can swing through a dock edge).
  - No self-collision; tapered capsules (skeletal bodies) are ignored; skinned meshes are skipped.
  - At most 64 hulls/boxes and 64 spheres/capsules near the line per Step (the first ones in the colliders' order).
  - Rotated box elements under non-uniform scale are exact here, while the physics approximates them, so the line and a
    cast can disagree slightly on such a box.
- One water height per line (a line spanning two water levels floats at the end's).
- A player who turns away from the bobber sees the line run back from the tip over the rod (no wrapping around the tip).
- "Ghost loop for ~1 s during a fast turn" (A2 playtest, T-041c) is not the simulation: `Project.Fishing.Line.FastTurn.*`
  measures 0 loop frames of 78 (0.3 s turn + 1 s hold) at 540 and 1080 deg/s, slack (WaitTension) and taut (HookedTension);
  worst stretch 1.26 % (slack, 1080 deg/s), 0.16 % otherwise. Hypothesis: a render artefact, TSR (temporal upscaling/AA)
  history ghosting on the 1-3 px spline-mesh segments that are reshaped every frame while the camera turns fast. No render
  settings changed. Editor check to schedule: in PIE, cast, flick the view 180 deg and back, and compare frames with
  `r.AntiAliasingMethod 0` / `2` (FXAA) against TSR (4); check whether the reshaped segments output motion vectors
  (`r.Velocity.EnableVertexDeformation`, `r.Velocity.ForceOutput`) and whether the smear follows the old line position (history) or the sim's current points
  (`ShowDebug`/debug draw of `GetPoints`).
- Other players' lines start from an estimated rod tip (no third-person rod yet; unchanged from T-006).
- `FLureFight::LineSag` and DT_Fishing `LineSag` no longer shape the line (only the old `SetLine` path reads a sag);
  T-028 can drop them. DT_FishFight `TautTension` shapes it again, through `FLureFight::LineTension` (T-032b).
- `/Game/Data/DT_FishingLine` must be reimported by the editor-operator after the merge (T-032b added 6 columns, T-043
  HangFaceTime; until then the built-in row is used).

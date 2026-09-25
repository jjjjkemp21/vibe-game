# Fighting fish visual (T-029)

The fish you see on the line during the reel fight. Cosmetic only: it never changes the fight (T-007/T-028 rules).
Clips and numbers: `art/export/Fish/SK_Fish.anim.md`. Tuning: `data/tables/DT_FishVisual.json` (row `Default`).

## Pieces

| Piece | File | Job |
|---|---|---|
| `FFightFishViewAdapter` | `Source/VibeGame/Fishing/FightFishViewAdapter.*` | The ONLY visual code that reads `FLureFightNetState`, `FLureFishingNetState` and `ULureFishingComponent`. Produces `FFightFishView`. |
| `FFightFishView`, `FFishVisualRow`, `FFightFishVisual` | `Source/VibeGame/Fish/FightFishVisual.*` | Plain view numbers, the DT_FishVisual row, pure rules (clip state, scale, placement, facing). |
| `ALureFightFish` | `Source/VibeGame/Fish/LureFightFish.*` | Local, non-replicated actor: skeletal mesh, moves from the view, computes the clip state. |
| `ULureFightFishSubsystem` | `Source/VibeGame/Fish/LureFightFishSubsystem.*` | Tickable world subsystem (not on a dedicated server). Spawns one fish per new fight, removes it at the end, fires the landed hand-off. |
| `UFishAnimInstance` | `Source/VibeGame/Fish/FishAnimInstance.*` | Parent of ABP_Fish; pulls `Role, PlayRate, Amplitude, RoleBlendTime, DartStartTime` from its `ALureFightFish`. |
| `ULureFishVisualSettings` | `Source/VibeGame/Fish/FishVisualSettings.*` | Project Settings > Game > Lure Fish Visuals: DT_FishVisual, ABP_Fish class, fallback mesh, optional actor class. |

When T-028 (or later work) changes the fight state, only `FightFishViewAdapter.cpp` needs to follow. T-028 (merged into
the adapter's lane): the rod turns the fish through `SideDeg` and `Tension`, which the adapter already reads; the rod's own
state (`RodPitch`, `RodYaw`, `ReelStep`, `RunSide`) is not the fish's and is not read (test `Project.FishVisual.Adapter.FollowsRodSteeredFight`).

## Rules

- **Spawn / despawn** (every machine, from replicated state): fighting = `FightNet.bActive` and the line is `Hooked`. A new
  `FightId` spawns a fish; the end is Landed (`Outcome` or `LastResult` Landed) or Escaped (anything else: snapped,
  thrown hook, spooled, reeled in, cancelled). Escaped: swims away (`EscapeSpeed`, sinking) for `EscapeTime`, then removed.
- **Mesh**: DT_FishSpecies `SkeletalMesh` (new optional column), else `Mesh` if it is a skeletal mesh, else the settings'
  `FallbackMesh` (SK_Bonefish); none = nothing drawn. `AnimAmplitude` / `AnimRate` columns (optional, default 1).
- **Scale**: `clamp((Weight / ReferenceWeight)^(1/3), MinScale, MaxScale)`.
- **Placement**: line end = player + (direction to the bobber's rest point turned by `SideDeg`) x `LineOut`. The fish's
  mouth is its point nearest the rod (T-048, lead 2026-09-24; replaces "body toward the player", which made the bobber
  look hooked to the tail): the `Mouth` bone sits right under the line end (and the bobber, which is drawn at the same
  XY), the fish faces the player from there and its body lies away from the player; its center is
  `SurfaceDepth + min(DepthShare x Depth, MaxShownDepth)` under the surface, at least `FloorClearance` above the bottom.
  Smoothing moves only that line point (`AuthoritySmoothTime` on the server/standalone, `ProxySmoothTime` elsewhere; jumps
  over `SnapDistance` snap), and it never trails the line end by more than `MouthMaxLagCm` (5 cm) horizontally; the body
  pivots around the mouth (`RotationSmoothTime`). While the mouth swims faster than `MinFacingSpeed` the body swings
  sideways by `RunSwingDeg x min(1, speed / RunSwingFullSpeed)` (35 deg, full at 200 cm/s): the head turns toward the side
  it swims to (straight out or in: the side it already leans to), so it reads as pulling away; the end that leads the swim
  follows a climb or dive (pitch up to `MaxPitchDeg`). The yaw is always kept within `RunSwingDeg` (max 80) of mouth ->
  player, so seen from above no body point is nearer the player than the mouth (tests `Project.FishVisual.MouthOnLine.*`).
  A tired fish does not swing. An escaping fish (after the fight) faces its swim when faster than `MinFacingSpeed`, else
  away from the player. Tired fish roll `ExhaustedRollDeg`, which is 0: **Jimmy, 2026-09-24: a tired fish stays upright with a
  calm swim, never on its side** (T-058a set it to 0, test `Project.FishVisual.ExhaustedUpright`; the column stays for a small
  roll with a later calm clip, S3).
- **Clips** (`EFishAnimRole`): first `HookSetThrashTime` s Thrash; then `MoveRoles[MoveId]` (unknown = `UnknownMoveRole`);
  tired = SwimIdle at `ExhaustedPlayRate`, alpha x `ExhaustedAmplitudeScale`; Landed = Flop at alpha 1; escaping = SwimFast.
  Sulk plays SwimIdle (T-059a: a sulking fish holds). **While fighting (not tired), effort, not speed (T-059a, art S3 gate
  A)**: every role plays at `AnimRate x (ReferenceWeight / Weight)^OtherRateWeightExponent x lerp(TiredRate, FreshRate,
  stamina)` (`RoleStaminaRates`: Run/SwimFast/Dive 1.0/0.6, Dart/Thrash 1.0/0.7, SwimIdle 1.0/0.8; a role not listed = 1)
  with alpha `AnimAmplitude x lerp(TiredAmplitudeScale 0.75, FreshAmplitudeScale 1, stamina)`; the fish's speed (reeling
  it in) never changes it. Stamina = `FLureFightNetState::Stamina` through `FFightFishView::Stamina01` (the adapter's one
  line) and `FFightFishAnimInput::Stamina01` (default 1: the held/landed fish keep their rule). Only the escape swim
  (roles in `RoleTailBeats`) plays at `AnimRate x Speed / (StrideBodyLengths x BodyLength x Hz)` clamped to
  `[MinPlayRate, MaxPlayRate]`; the flop at the weight rate. Tests `Project.FishVisual.PlayRateByStamina.*`. A dart starts at
  0 (turning to the fish's left) or `DartRightStartTime` (right).

## Hand-off to T-030 (the landed fish)

`ULureFightFishSubsystem::OnFightFishLanded` (BlueprintAssignable) / `OnFightFishLandedNative`:
`(ULureFishingComponent* Fishing, ALureFightFish* Fish, const FFishInstance& Landed)`, on every machine, when the fight
ends Landed. `Fish` is at the line's end playing Landed_Flop at alpha 1. Right after the event it is destroyed, unless a
listener calls `KeepLandedFish(Fish)` during the event: then it is the listener's (e.g. reuse it as the hanging fish:
attach it at `GetMouthLocation()`, keep its mesh, scale and flop). Or spawn your own item at `Fish`'s transform and let it
go. The server's gameplay landing is still `ULureFishingComponent::OnFishLanded` (XP, the fish record).

## ABP_Fish (editor-operator)

Blueprint `/Game/Art/Fish/ABP_Fish`, parent class `UFishAnimInstance`, skeleton `SKEL_Fish`. The graph is in the class
comment of `FishAnimInstance.h` (same as SK_Fish.anim.md "ABP_Fish"). Nothing else: no event graph logic.

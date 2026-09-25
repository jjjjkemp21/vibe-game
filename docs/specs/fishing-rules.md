# Fishing rules (T-006: cast, bobber, bite, hook), unreal-engineer decisions 2026-09-23

Binding for the implementation (`Source/VibeGame/Fishing/`) and the tests (`Project.Fishing.*`). Every feel number is data:
`data/tables/DT_Fishing.csv` (row `Default`; a gear profile later picks another row), per-stance rod rules in
`data/tables/DT_Movement.csv`, world/asset settings in Project Settings > Game > Lure Fishing (`Config/DefaultGame.ini`).
The lead may overrule any of these; a change is a data edit unless marked (code).

## Flow and authority
- States (replicated, server-written): Idle -> Casting -> Waiting -> Biting -> Hooked -> (placeholder land) -> Idle.
  Charging is local to the owning client (HUD "Cast power N%"); on release the client sends `ServerCast(charge, aim yaw)`;
  the server clamps the charge and decides everything else (rules, landing point, spot, nibbles, bite time, fish roll, hook
  window, hit/miss). Clients draw the bobber and line from the replicated `NetState` (+ `HookedFish`, `LastLandedFish`).
- The fish that bites is rolled at bite time by the one pipeline (`FFishRoll::PickSpecies` + `FFishRoll::Roll`), seeded from
  a server RNG. It stays on the server until hooked (then it replicates as `HookedFish`).
- Remote players get `HookLatencyGrace` (0.15 s) extra hook time on the server; the host and standalone get none.

## Cast
- Distance = MinCastDistance + (Max - Min) * charge^ChargeExponent, horizontal from the eye, along the view yaw. Charge =
  hold time / ChargeTime (clamped). Flight time = distance / CastSpeed, clamped to [CastFlightTimeMin, Max]; arc apex =
  CastArcHeightRatio * distance.
- Landing: in front of anything solid on the way; on the water surface if the ground there is not above it (LandTolerance
  2 cm), otherwise on land (a dock, a beach, a rock): the bobber lies there, nothing bites, a press reels in.
- "Solid" (fishing-loop playtest fix, 2026-09-23): the cast traces use the `LureCast` trace channel
  (`ECC_GameTraceChannel1`, Config/DefaultEngine.ini, default Block; the Trigger/OverlapAll/Pawn profiles ignore it) and
  `FLureFishingSpots::TraceCast`, which passes through anything that isn't solid level geometry: every volume and
  trigger actor (AVolume, ATriggerBase, e.g. the design zones shark_zone and shadow_zone, whatever their collision
  profile), pawns, and overlap-only components (they block none of WorldStatic/WorldDynamic/Pawn/PhysicsBody/Vehicle).
  To let casts pass a solid prop, set its LureCast response to Ignore. Tests: `Project.Fishing.CastTrace.*`.
- Water surface: an engine water physics volume (T-026), else the top of an actor tagged `Lure.Water`, else `FallbackWaterZ`
  (0 = the layouts' `water_z`). Level-designer: tagging the water planes `Lure.Water` is optional today.
- No casting while: no rod in hand, swimming, climbing, in the air, in a DT_Movement row with `CanFish = False` (Sprint), or moving
  (speed > RodMoveSpeedIn) in a row whose moving pose is `ProneTuck` (prone crawl). Prone and still casts.
- A line out comes in on its own (result ReeledIn, or Lost with a hooked fish, a fight in progress included) when you start sprinting, swim, climb, crawl prone
  (the rod tucks), or get farther than MaxLineLength from the bobber (not during a fight; reel-fight-rules.md). Jumping keeps the line (a jump that ends in a pull-up does not).
- Swimming and climbing (T-026 merge, review D1; tests `Project.Fishing.Rules.ClimbingIsBusy`, `Project.Fishing.Swim.*`,
  `Project.Fishing.Climb.*`):
  - "Swimming" is `ALurePlayerCharacter::IsSwimming()`: from falling in until standing on land again, the climb out of the
    water (`ClimbOut`) included. Reason `Swimming`. The rod is put away (`IsRodInHand` false), so the arms show `Idle`.
  - Any `MOVE_Custom` mode (today the land pull-up `LedgeClimb`, and `ClimbOut`) is busy: no cast, a line out comes in.
    Reason `Climbing` ("climbing") unless it is the climb out, which reports `Swimming`. The rod stays in hand on a pull-up.
  - Fishing reads the state every tick on the server; it never listens to `OnSwimStateChanged`. So a cast made right after
    climbing out stays out even if the owner's settled swim event arrives late (swimming.md, networking contract).
  - The Swim rows in DT_Movement keep the default rod columns (`CanFish = True`): the swimming rule above blocks fishing in
    the water whatever the row says (code, not data).

## Where fish bite (T-027: fish anywhere; replaces the spot rule)
- Superseded by **docs/specs/fishing-water-rules.md** (Jimmy's playtest, 2026-09-23): every body of water can be fished.
  The water area under the bobber (layout "water_area" markers -> ALureWaterArea) gives the habitat, region and luck;
  depth bands pick areas; uncovered water uses ULureWaterSettings::DefaultWaterHabitat (Habitat.Shore); water shallower
  than MinBiteDepth never bites; a habitat with no species at this hour uses the gap fallback habitats (logged); hot spots
  add luck, size, value and sooner bites. "Nothing is biting here" is gone: the HUD names the water and, if nothing can
  bite, the reason (too shallow, wrong bait, no fish).
- Fishing spot markers (`Lure.FishingSpot` + `Key=Value` tags, L_PalmKey.md section 11) are still parsed: a level with
  no water area reads them as circle areas (migration), otherwise they are only named casting places (teleports, labels).
- The bite context: time of day = `DefaultTimeOfDayHours` (16) until T-013, bait = the hook's (DT_Gear) or the setting
  `DefaultBait`, luck = water area luck + gear luck + hot spot luck.

## Bite, hook, miss
- Wait: random in [BiteWaitMin, BiteWaitMax] after landing. Before the bite, [NibblesMin, NibblesMax] nibbles about
  NibbleInterval apart: the bobber tips (NibbleTiltDeg) so its white half shows (designer B-S4). Nibbles are tells only.
- Bite: the bobber is pulled under by BiteDipDepth (>= its scaled 4.9 cm top, so the red disappears; B-S4) and tugs; the
  bite sound (setting, optional) plays at the bobber and the owner's controller rumbles (BiteRumbleIntensity/Duration).
- Hook: a press inside [bite, bite + HookWindow (+ grace)] hooks. After the window closes the bite is a miss: the rolled fish
  is gone for good. Then `MissEndsCast` decides: False (default) = the bobber stays and a new bite (a new roll) may come
  after RebiteWait; True = the line comes in.
- Early press (before a bite, nibbles included): `EarlyHook` = ReelIn (default: press to reel in and recast), Ignore, or
  Spook (the bite is pushed back SpookDelay). On land or with nothing biting, a press always reels in.
  **Changed by fight-v2.md 3.4 (T-055, Jimmy 2026-09-24): no instant reel-in.** Holding Cast retrieves the bobber across
  the water at RetrieveSpeed; the cast ends when it reaches the rod or the water's edge. Forced reel-ins stay instant.
- Hooked: the reel fight (T-007) runs until landed, snapped or the hook is thrown: see docs/specs/reel-fight-rules.md.
  Debug only: AutoLandDelay > 0 skips the fight and lands the fish after that many seconds (shipped: 0).

## Controls (runtime Enhanced Input, `ULureInputSubsystem::GetInputActionByName`)
- `Cast` (LMB, gamepad RT): hold to charge, release to cast; while the line is out a press hooks (or reels in early).
- `Hook`: the same hook press as its own action, no default key (a key can be added in the settings). One key drives one
  action, so the QA "no key bound to two actions" rule holds.

## Rod, bobber, line (cosmetic)
- Rod: SM_Rod_Basic on SK_FPArms' `hand_r_rod` bone (SnapToTargetNotIncludingScale + absolute scale: world scale 1 whatever
  the bones' scale, so it works before and after the arms re-export), first-person primitive, owner only.
- Rod pose per stance and motion (DT_Movement): Stand/Sprint/Crouch = HoldRod; Prone still = ProneHold, Prone moving =
  ProneTuck (tuck at once, untuck after RodStillDelay); prone ArmsPitchFollowUp 0 (the arms stay down when looking up);
  RodHoldClearance 130 cm: a still prone player facing a wall keeps the tuck unless a line is out.
- Bobber: SM_Bobber at BobberScale (3x), pivot on the water surface, bobs and wobbles.
- Line: a simulated rope (T-032, docs/specs/fishing-line.md: sags and floats when slack, straight at the snap threshold,
  recoils when it snaps) drawn as spline-mesh segments from the rod's `LineTip` (moved to where the first-person rod is
  drawn) to the bobber's `LineAttach`. Width per point = LinePixelWidth (2.5 px) at the
  viewer's distance on a 1920-wide view (never under 2 px at 1080p; designer B-S3). Other players see the line from an
  estimated rod tip (RodTipOffsetFromEye) until they get a visible body and rod.
- Cast motion: placeholder rod swing (CastSwing* columns). Setting `CastMontage`/`HookMontage` (arms montages from the
  animation-artist) replaces it without code.
- HUD: plain white text lines (ALureHUD): cast power, "BITE! Click/RT to hook!", hooked/caught/missed, "Can't cast: ...".

## Lead decisions on the T-006 open questions (2026-09-23)
1. ~~No fishing spot in range = no bite~~ Superseded by T-027 (Jimmy, 2026-09-23): every body of water can be fished (fishing-water-rules.md). The OffSpotHabitat setting is replaced by ULureWaterSettings::DefaultWaterHabitat.
2. Bait: every hook uses Bait.Shrimp until gear exists (T-010/T-012); the default bait is a setting, not code.
3. Crawling prone, sprinting or swimming while the line is out reels it in automatically (no blocked movement). Jimmy can change this after his playtest.
4. A hooked fish is landed after 1.5 s (placeholder) until the reel fight (T-007) replaces it. (Superseded by T-007: docs/specs/reel-fight-rules.md.)
5. Other players see the line start from an estimated rod tip until a third-person rod exists (after the slice).

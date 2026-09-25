# Reel fight rules (T-007, rod steering T-028), unreal-engineer 2026-09-23

The reel fight between "hooked" and "landed". Code: `Source/VibeGame/Fishing/FishFight.{h,cpp}` (pure simulation; the
formulas are also in the `FishFight.h` header comment), `FishFightTypes.{h,cpp}` (gear, patterns, tuning, replicated
state), wired into `ULureFishingComponent`. Tests: `Project.Fishing.Fight.*` (`Source/VibeGame/Tests/FishFightTest.cpp`).
Rod steering and reel speed (T-028): section "Rod steering" below; tests `Project.Fishing.Fight.Rod.*`
(`Tests/FishFight/RodFightTest.cpp`); balance model `tools/balance/reel_fight_model.py`.
All numbers are PLACEHOLDER until Jimmy's playtest A.

**Fight v2 (T-052, 2026-09-24): `fight-v2.md` changes this fight**: reel / hold / let line out (T-054), a limited spool
that snaps the line past 100 % tension instead of the instant `Spooled` loss (T-053), landing only a tired fish with a
last-run bolt (T-057), the retrieve after a cast (T-055) and the bobber dip (T-056). Where they differ, fight-v2.md wins.

## Decisions
- **Hold to reel.** While a fish is on, holding Cast (LMB / gamepad RT) reels; releasing lets the fish run against the
  drag. The owning client sends `ServerSetReeling(bool)` (reliable) and, since T-028, its rod aim and reel step
  (`ServerSetFightInput`, below); the server simulates and decides every outcome.
- **Server-authoritative, deterministic.** The server runs a fixed-step sim (`SimRate` steps/s, at most 30 steps per frame
  after a hitch). The fight seed is derived from the fish record's seed (`FightSeed = HashCombine(Seed, 7)`), so the same
  fish + gear + tuning + inputs replays the same fight. Clients get `FightNet` (replicated): tension, line strength, slack
  level, stamina, line out, spool, current move id, snap/slack progress, outcome. HUD and visuals read only that.
- **The fish's difficulty comes from its FFishInstance final stats** (T-008): Strength -> pull, Speed -> swim speed,
  Stamina -> how long it fights, Aggression -> how often it makes its aggressive moves. The stat tags are DT_FishFight
  columns. The fish-level vs player-level hook (`UFishSettings::LevelScaling`) multiplies the pull (`ApplyLevelScaling`).
  `DifficultyRating` shortens rest moves (`RestDifficultyExponent`). PlayerLevel is 1 until T-010 sets it.
- **Fight patterns are data** (DT_FightPattern, one row per kind of fish, row name = the species' `FightPatternId`).
  A pattern is a list of moves (pull, speed, away/side/down share, duration range, pick weight, aggression weight, rest).
  Shipped: `Run` (Bonefish), `Dive` (Coral Snapper), `Dart` (no species yet; its charge toward you slackens the line).
  A new pattern or a new move is a data row; a missing/invalid pattern uses a built-in generic pattern plus one warning.
- **Four outcomes** (first that applies each step): Landed (fish within `LandDistance`; with a dock edge in the way, only
  once lifted over it: "Dock edges (T-047)"), Spooled (fish took more line than
  `SpoolLength`: line breaks), Snapped (tension above `LineStrength` longer than `SnapGraceTime`), ThrewHook (tension below
  `SlackShare` x base pull longer than `SlackGraceTime` x `HookSecurity`). Timers reset when the condition stops.
  "Longer than" is counted in whole fixed steps: over for exactly the grace holds, one step more ends it (T007-B1).
  Landed fires `OnFishLanded` (+ native delegate; the cooler hook for T-010) and logs `Catch:` on LogLureFish. Snapped,
  Spooled and ThrewHook lose the fish; the line comes in (state back to Idle) with a plain-text HUD message.
- **Swimming and climbing during a fight (T-007 x T-026 merge, 2026-09-23):** the fishing line rules still apply while a
  fish is on (fishing-rules.md: sprinting, swimming, climbing, a tucked rod), except the bobber distance rule (the fight
  has its own: the spool). Falling in (swimming, the climb out included) or a ledge pull-up mid-fight cuts the line: the
  fish is lost (result `Lost`, reason `Swimming` / `Climbing`), nothing lands (no `OnFishLanded`, no cooler, no XP), the
  state goes back to Idle. No auto-reel. Tests: `Project.Fishing.Fight.SwimClimb.*`
  (`Tests/FishFight/FightSwimClimbTest.cpp`).
- **Gear** (DT_Gear, one row per item, three slots: Rod, Line, Hook-with-bait). Rod: RodPower, ReelSpeed, Drag,
  CastDistanceMultiplier. Line: LineStrength, SpoolLength. Hook: HookSecurity, BaitTag (which species bite), Luck.
  **Balance rule (lead, 2026-09-23):** the reel's effective drag never exceeds what the line holds:
  `Drag = min(rod Drag, LineStrength x DragLineCap)` (DT_FishFight `DragLineCap`, optional column, default 0.9), applied
  to the resolved loadout. So upgrading the rod before the line never makes a fish harder (Rod_Reef on Line_Mono: drag 9).
  The equipped `Loadout` (row names per slot) is replicated and server-written; `ULureFishingSettings::DefaultLoadout`
  is the starter kit (Rod_Starter, Line_Mono, Hook_Shrimp). The shop (T-012) and saves will change it. Missing table or
  row = built-in starter item for that slot (a test keeps the built-ins equal to DT_Gear.csv).
- **Placeholder visuals:** the rod pitches toward the fish with tension (`RodTensionPitchDeg`) and shakes while over the
  line's strength (`RodShakeDeg`); the line is taut above `TautTension` of the strength and sags below it; the bobber
  follows the fish (sinks `DiveBobberShare` of its depth). Optional arms montage slots (reel loop, hooked, landed) stay
  empty until the animation-artist delivers clips; a skeletal rod for a real bend comes later.
- **HUD (placeholder text, per the UI rule):** tension bar `Tension [#####-----] 50%`, the fish's move label, stamina,
  line out.
- **Debug:** DT_Fishing `AutoLandDelay` > 0 skips the fight and lands after that many seconds (the T-006 placeholder).
  Shipped value is 0 (the fight runs).

## The step (summary; exact formulas in FishFight.h)
1. Move: when a move ends, pick the next by weight `max(0, Weight + Aggression x AggressionWeight)`; duration random in
   [DurationMin, DurationMax] (rest moves x `DifficultyRating ^ -RestDifficultyExponent`). An exhausted fish stops moving.
2. Fish: `StaminaFactor = TiredPull + (1 - TiredPull) x Stamina`; `Pull = BasePull x Move.Pull x StaminaFactor`;
   `Speed = BaseSpeed x Move.Speed x StaminaFactor`. BasePull = Strength x PullPerStrength x LevelMultiplier;
   BaseSpeed = Speed x SpeedPerStat; stamina pool = Stamina x StaminaPerStat (tension-seconds).
3. Line: reeling gains `ReelSpeed x clamp(1 - Pull/RodPower, 0, 1)` and the fish takes line only when it out-pulls the
   rod; not reeling, the drag gives line from `DragHold x Drag` and freely at `Drag`. A fish swimming toward you shortens it.
4. Tension eases (rise/fall time constants) toward: reeling `Pull x ReelStrain + RodPower x ReelLoad`; not reeling
   `min(Pull, Drag)`. So cranking against a hard run spikes tension, and letting it run caps tension at the drag.
5. Stamina drains by tension x dt; regains `StaminaRecovery` per second while slack. Exhausted at `ExhaustedStamina`.
6. Outcome checks (above).

## Tuning columns
- **DT_FishFight** (`data/tables/DT_FishFight.csv`, row `Default`, struct `LureFishFightRow`): StrengthStat, StaminaStat,
  SpeedStat, AggressionStat (tags), PullPerStrength 0.25, SpeedPerStat 5, StaminaPerStat 5 (T-049, lead decision 2026-09-24; was 3, round 1 18), ApplyLevelScaling True,
  RestDifficultyExponent 1.0, TiredPull 0.3, ExhaustedStamina 0.02, StaminaRecovery 0.04, ReelStrain 1.3, ReelLoad 0.15,
  DragHold 0.5, TensionRiseTime 0.12, TensionFallTime 0.25, SnapGraceTime 0.6, SlackShare 0.35, SlackGraceTime 2.5,
  LandDistance 150, SimRate 60, MaxDepth 300, DepthRecovery 80, MaxSideDeg 50, DiveBobberShare 0.1,
  RodTensionPitchDeg 25, RodShakeDeg 2.5, TautTension 0.3, DragLineCap 0.9 (optional, in (0, 1]).
  The four stat columns must be tags under `Fish.Stat.*`. Dock edges (T-047, optional, "Dock edges" below):
  EdgeClearance 25, EdgeProbeDepth 10, EdgeProbeHeight 100, EdgeQueryInterval 0.1, EdgeLiftClearance 80, EdgeTopInset 10,
  EdgeMaxLift 300, EdgeLandHold 0.5.
- **DT_Gear** (`data/tables/DT_Gear.csv`, struct `LureGearRow`): Rod_Starter (power 8, reel 180 (T-050; was 120), drag 5),
  Rod_Reef (16, 225 (was 150), 12, cast x1.15, 250 coins), Line_Mono (strength 10, spool 40 m), Line_Braid (22, 60 m, 180 coins),
  Hook_Shrimp (security 1.0, shrimp), Hook_Squid (1.6, squid, luck 0.5, 60 coins).
- **DT_FightPattern** (`data/tables/DT_FightPattern.json`, struct `LureFightPatternRow`): Run, Dive, Dart (moves above).
  Run pattern since T-049: OpeningMove `Shake` (Weight 0: only the opening; 1.6-2.2 s, Pull 1.2, Speed 0.4, Away 0.5, no side);
  `Run` Weight 3, AggressionWeight 0.08, 1.0-1.6 s, Pull 2.8, Speed 0.7, Side 0.3; `Swim` Pull 2.0; `Rest` Pull 1.5.
- Table pointers: `ULureFishingSettings` (GearTable, FightPatternTable, FishFightTable, FishFightRow, DefaultLoadout).

## Fishing-loop tune (2026-09-23, unreal-engineer): holding reel through a Run is a real risk
Playtest (Saved/AgentLogs/playtest/20260923-141005-fishing-loop): holding reel the whole time landed a Rare 2.04 kg
Bonefish in 9.1 s with no risk, while careful play (release during runs) took 15.6 s. That taught the wrong lesson.

**The model.** The time to land is mostly the line to reel back (about 850 cm at 80-110 cm/s) plus line lost to runs;
stamina drains by tension x time, so a held line (high tension) always tires a fish fastest. Data can't make careful
play faster than a hold that survives, so the tune makes careful play *clearly safer* and keeps it inside the fight
length. Only the Run pattern's `Run` move changed: a short, hard burst (Pull 1.6 -> 2.4 of the base pull, Speed 1.8 ->
0.7, 1.5-3 s -> 0.8-1.5 s, Weight 2 -> 1.2). Reeling during a Run: tension = 2.4 x BasePull x StaminaFactor x
ReelStrain 1.3 + RodPower 8 x ReelLoad 0.15. The starter line (10) is crossed when BasePull x StaminaFactor > 2.8, so a
typical Common (1.5 kg, BasePull 2.5) peaks at ~85 % of the line and lands, while a Rare (level 2 = pull x1.35) or a
2.5 kg+ bonefish stays over the line longer than SnapGraceTime (0.6 s) and snaps it. Easing off caps the tension at the
drag (5), and the slower, shorter Run takes only ~0.5-1.5 m of line. DT_FishFight, DT_Gear, the species stats and the
Dive/Dart patterns are unchanged, so every Coral Snapper number is unchanged.

**Targets and results** (Python transcription of FLureFight::Step with the same data; 300 roll-like bonefish with
rarity, weight skew and modifiers; starter kit; the fight starts 10 m out; "careful" = the tests' tension watcher:
reel below 70 %, ease off above 90 %, 0.3 s reactions; "run-aware" = eases off for every "running!", reels the rest,
0.3 s reactions):

| Target | Before | After |
|---|---|---|
| Holding reel through a Run gets near or over the snap mark (starter gear) | 5 % of bonefish snapped; median peak 69 % | 42 % snap; median peak 97 %; a typical 1.5 kg Common peaks at 85 % and lands; the Rare 2.04 kg and any 3 kg+ always snap |
| Careful play is faster OR clearly safer | careful 0 % lost; run-aware 0.7 % lost but 2.2x slower (median 21.7 s vs 9.7 s held) | clearly safer: careful 0 % and run-aware 0 % lost vs 42 % for holding reel |
| Fights about 8-16 s | careful median 10.0 s (p90 14.5); run-aware median 21.7 s (p90 39.1) | careful median 10.6 s (p10 8.4, p90 14.9); run-aware median 15.1 s (p90 22.4: the 3-4.5 kg fish); held and landed 9.0 s |
| The playtest's Rare 2.04 kg bonefish | held: lands in 11.6 s; careful 14.4 s | held: snaps (100 %); careful lands in 13.3 s; run-aware 16.5 s |
| The snapper stays harder | - | unchanged: starter kit, holding snaps 81 % (reference 2.5 kg: 100 %); careful median 17.3 s (reference 15.7 s) vs 10.6 s for bonefish |
| Bonefish on the reef kit (braid 22) | lands, held 6.7 s | lands, held 6.7 s; the heaviest Rare Feisty bonefish peaks at ~100 % of the braid without snapping |

Pinned by `Project.Fishing.Fight.BonefishRunPunishesHoldReel` (real data and roll pipeline: the Rare 2.04 kg and a 3 kg
Common snap when held and land carefully in 8-16 s; a 1.5 kg Common peaks at 80-100 % and lands; 200 rolled bonefish:
hold snaps 25-60 %, careful and run-aware lose at most 2 %, careful p10 >= 7.5 s and p90 <= 16 s, run-aware median
<= 16 s; the reference snapper snaps when held and a careful snapper fight takes longer than a careful bonefish fight).

## T-049 + T-050: an even fight and a faster reel (2026-09-24, unreal-engineer)
Jimmy (A2 playtest): "the fish fights strong immediately and almost breaks my rod; after less than a second it calms down and
the fight is easy" and "reel in speed is way too slow; the speed of the battle should be dictated by the tension management
(letting a fish run so it doesn't snap your line increases fight time)". Data only (no rule changed).

**The cause, from a sim trace** (1.5 kg Common bonefish, starter kit, well-played player below, 18 m out; old tune):
the pattern opened with its Run (2.4x pull at full stamina): reeling from the hook set took the bar to 81 % of the line in
0.3 s (a 2.0 kg: 92 %, a 2.5 kg: 102 %, while a 0.3 s reaction can't ease off in time). The stamina pool (Stamina 14 x
StaminaPerStat 3 = 42 tension-seconds) drained at ~8/s: the fish had spent a quarter of it in the first second and half by 3 s,
so its pull (x TiredPull + (1 - TiredPull) x stamina) sank; then the Run ended into a Rest of 0.3x pull: the fish's pull fell
by 92 % within a second, and every later Run was weaker. After that the fight was the crank: 1650 cm at ~110 cm/s (87 % of
the 15.8 s fight), almost the same for every seed (15.5-16.5 s) and for a timid player (17.1 s).

**The tune.** Pressure ramps in: the Run pattern opens with `Shake` (weight 0, so only the opening: 1.2x pull for 1.6-2.2 s), the
bar starts near 50 %. The fish keeps pulling between runs (Swim 1.0 -> 2.0, Rest 0.3 -> 1.5) and runs often and short (Run Weight
1.2 -> 3, 0.8-1.5 s -> 1.0-1.6 s, Pull 2.4 -> 2.8). StaminaPerStat 3 -> 5 (all species). The reel is
50 % faster: Rod_Starter ReelSpeed 120 -> 180, Rod_Reef 150 -> 225 (the reel steps can't do it: the default step must be speed 1).

**Lead decision 2026-09-24 (S1).** Jimmy called A2's 15.8 s "way too slow" and asked for an even fight with beginner fish easy.
Round 1 had StaminaPerStat 18 (fish kept their stamina: a 16 s well-played fight, but big and rare bonefish took up to 50 s and
the 7 kg snapper needed the reef kit); the rework-1 sweep found no tune that kept every old and new bound (the trade-off table in
Saved/AgentLogs/tasks/T-049/report.md). The lead picked StaminaPerStat 5: well-played beginner bonefish ~12.5 s, big/rare
bonefish careful p90 ~17 s. Consequences accepted: the 7 kg level-3 snapper needs the reef kit on careful play (a gear-progression
outcome; reverses the lead's 2026-09-23 call "careful lands it with either kit"); careful p10 >= 6 s (was 7.5 s: T-050's faster
reel shortens small-fish fights). Stamina/tension-driven pacing moves to Sprint 2's fight design (T-052, notes below).

**Well-played** (the tests' policy, `Tests/Fishing/FightEvenTest.cpp`): reacts 0.3 s late; every 0.3 s reads the bar: reels below
85 % (room for the reaction and the tension's rise before 100 %), eases off at 85 %+, reels again under 60 %; steers the rod
against every sideways run; rod level, default reel step. Fights start 18 m out (MaxCastDistance; 8 of Jimmy's 14 A2 fights).

Final numbers (C++ tests, lane eng5 report tests/20260924-213656; 1.5 kg Common unless noted, 30 seeds):

| Measure | Old tune | Now (SPS 5) | Test (bound) |
|---|---|---|---|
| Peak bar in the first 1.5 s (1.0 / 1.5 / 2.0 kg Commons) | 91 % | 56 % | `Even.HookSetRampsIn` (<= 75 %) |
| Largest drop of the fish's pull within 1 s, before exhaustion | 92 % | 60 % (1.0 kg, seed 6, 8.0 s) | `Even.NoCliff` (<= 60 %) |
| Reference snapper (2.5 kg, level 3): hold at the fastest step / well-played | - | snaps 30 of 30 / lands 30 (median 20.7 s) | `Even.ToughFishNeedsTensionManagement` |
| Well-played landing time from 18 m | median 15.8 s (15.5-16.5) | median 12.5 s (11.8-13.6) | `LandTime.WellPlayedBeginnerBonefish` (11-15, all 9-18) |
| Cranking the 16.5 m in at the default step | 13.8 s | 9.2 s | `LandTime.TensionSetsThePace` (faster than old) |
| Bold (95 % / 80 %) vs well-played | 15.8 / 15.8 s | 12.3 / 12.5 s | same (bold <= well-played, nobody lost) |
| 7 kg level-3 snapper, careful, starter / reef kit (8 seeds) | lands / lands | snaps at 1.5 s 8/8 / lands 12.5-14.2 s | `Fight.GearDecidesOutcome` (reef lands, starter snaps) |
| Rare 2.04 kg + 3 kg Common bonefish: held / careful (6 seeds each) | - | snap / land in 8-16 s | `Fight.BonefishRunPunishesHoldReel` |
| 200 rolled bonefish (10 m): hold snaps / careful lost / run-aware lost | - | 84 (42 %) / 0 / 24 | same (25-60 %, <= 2 %, run-aware <= 60 % of hold) |
| ... careful p10 / p90; run-aware median | - | 6.7 s / 17.4 s; 14.2 s | same (p10 >= 6, p90 <= 22; <= 16) |
| 200 rolled bonefish: lost by steer-only / hold; skilled | - | 47 / 76 (62 %); 0, median 7.6 s | `Fight.Rod.SkilledPlayBeatsHolding` (<= 75 %; skilled faster than hold every seed) |

**Handed to S2 (T-052): stamina/tension-driven pacing.** Not tests now; today's numbers are printed as info lines by the LandTime tests.
- Timid (eases at 60 %, reels under 40 %) takes >= 1.3x well-played: today x1.10 (13.8 vs 12.5 s; round 1 x1.40, old x1.08).
- "Pressure lasts": the fish keeps >= 0.5 stamina 5 s into the well-played fight: today >= 0.22 (round 1 0.76, old 0.49).
- The crank is <= 60 % of the well-played fight: today 73 % (9.2 of 12.5 s; round 1 57 %, old 87 %).
- Run-aware play (eases off for every run, no bar watching) loses <= 2 %: today 24 of 200 (12 %).
- Steering alone loses <= 60 % of holding's losses: today 62 % (47 of 76).
Why S2: all five need a fish that keeps pulling through the fight, and StaminaPerStat is global: the same staying power makes big and
rare bonefish long fights and the 7 kg snapper impossible on the starter kit. S2's fight design needs a per-species or per-pattern lever.

## What the numbers do today (before T-049; see the T-049 section above for the current tune)
- Bonefish (0.5-4.5 kg) on the starter kit: small ones (under ~2 kg Common) land even when you hold reel (8-10 s);
  bigger or rarer ones snap it if you hold reel through a Run (see the fishing-loop tune above).
- A reference Coral Snapper snaps the starter line in under a second if you hold reel from the hook; easing off during
  dives lands it (~15 s). The reef kit (Rod_Reef + Line_Braid + Hook_Squid) lands it just holding reel (~8 s).
- A 7 kg snapper snaps the starter line even for a careful player (0.3 s reactions); the reef kit lands it (~12 s).
  Holding reel non-stop snaps even the reef kit on a 7 kg snapper: big fish always need some easing off.
- Never reeling: the fish throws the hook (~20-35 s), or a big bonefish sometimes takes the whole spool.
- Tests pin this: `Project.Fishing.Fight.GearDecidesOutcome` (weak gear snaps, right gear lands, both for a hold-reel
  player and a scripted careful player), plus data validation, determinism, grace timers, authority and replication.

## Rod steering and reel speed (T-028, 2026-09-23)
Jimmy's playtest: "pull back on the rod or aim it down to raise or lower tension, with the mouse; if the fish runs left, angle
the rod up and to the right; a way to set the reel speed." His decision: once a fish is hooked the MOUSE STEERS THE ROD instead
of the view, and the camera gently follows the rod and the fish until it is landed or lost. Lead: hold click/trigger to reel,
the mouse wheel / bumpers set the reel speed in steps, shown in the HUD text.

**Decisions**
- **Input routing.** `ALurePlayerCharacter::DoLook` hands the Look action's degrees to `ULureFishingComponent::ConsumeLookInput`
  while `IsSteeringRod()` (the owning client, a fish on, the fight running); the view does not turn then. The Look action is
  unchanged (mouse: degrees per count; right stick: degrees per second), so the gamepad steers the rod at the look rate and the
  playtest driver's injected Look steers it too. The rod stays where you leave it (no spring back). Reel speed: two new Enhanced
  Input actions, `ReelFaster` (mouse wheel up, right bumper) and `ReelSlower` (wheel down, left bumper), keys in
  `ULureFishingSettings` (`ReelFasterKeys`, `ReelSlowerKeys`); they work only while a fish is on. The step is kept from fight
  to fight (a reel keeps its setting); the rod starts level and centered in every fight.
- **The aim** (`FLureRodAim`, `LureRodControl.h`): look degrees accumulate, clamped to `RodAimUpDeg` / `RodAimDownDeg` /
  `RodAimSideDeg`, and become RodPitch (-1 dipped toward the fish .. +1 pulled back/up) and RodYaw (-1 left .. +1 right).
  The yaw is relative to the LINE (player to fish), not the camera, so the fight needs no camera and stays deterministic.
- **The neutral rod is the T-007 fight, bit for bit**: level, centered, at the default step (speed 1) every factor is exactly 1
  (`Project.Fishing.Fight.Rod.NeutralRodIsTheT007Fight`). A player who never touches the mouse or the wheel fights exactly
  as before, and every T-007 test and number above still holds.
- **What the rod does** (each step; exact formulas in `FishFight.h`, `FLureFight::RodFactors`):
  - Pitch pressure `P = 1 + p x PitchBackPressure` (back) or `1 + p x PitchDipPressure` (dipped). It scales the tension you
    apply (reeling: `(Pull x ReelStrain + RodPower x ReelLoad x ReelStepLoad) x P`). The rod's power (line gained, line
    taken while you reel) is `R = 1 + p x PitchBackPressure` (back) or `1 + p x PitchDipPower` (dipped; T-028b): **pull back =
    pressure, dip = relief**. A dipped rod gives slack to save the line but barely works the fish (PitchDipPower 0.8 >
    PitchDipPressure 0.5), so dipping is no longer a way to reel in fast (see T-028b below). Letting it run: `min(Pull x P, Drag)`: **the drag still caps a running fish**, so letting it run
    never snaps the line whatever the rod does.
  - Run direction: a move's `Side` share x its random side, when `|Side| >= SideMinShare`: the fish runs LEFT or RIGHT
    (dives, rests, straight runs and a tired fish have no side). Side score `S = -RodYaw x RunDir`: +1 = rod fully against
    the run, -1 = fully with it.
  - Against the run (S > 0) the fish is **turned**: its move's clock runs `1 + S x SideTurnRate` times as fast (the run ends
    sooner), it pulls `1 - S x SideTurnPull` as hard, and it tires `1 + S x SideDrain` times as fast. Both ways the rod's
    power is `x (1 + S x SideLeverage)`: **with the run you lose ground** (you gain less, it takes more line). Cosmetic: a
    turned fish swings back toward the middle.
  - Reel speed steps: `ReelSteps` evenly from `ReelSpeedMin` to `ReelSpeedMax` x the rod's ReelSpeed; the cranking load
    (`RodPower x ReelLoad`) x `max(0, 1 + (speed - 1) x ReelLoadPerSpeed)`: **fast gains line but builds tension**.
- **Networking.** The owner sends `ServerSetFightInput(FightId, pitch, yaw, step)`: 4 bytes, **unreliable**, at most every
  `FightInputSendSeconds` (0.05 s) when the aim changes, at once for a step change or a new fight, and again every
  `FightInputResendSeconds` (0.25 s) so a lost packet is repaired. Axes are packed in a byte with 127 = exactly 0, so the
  neutral rod stays exact on the wire. The server ignores packets with no fight on or from another fight (FightId) and clamps
  the rest (`FLureFight::SanitizeInput`); the tension, line and outcome are only ever its own simulation. `ServerSetReeling`
  stays reliable (a release must never be lost). `FightNet` carries RodPitch, RodYaw, ReelStep and RunSide back: other players
  draw this player's rod tip and line from it (eased by `RodAimBlendTime`), and the HUD shows the run hint.
- **Camera** (owner): each frame the control rotation eases (`CameraFollowTime`, the short way round) toward the fish from the
  eye, turned by `CameraRodYawShare` / `CameraRodPitchShare` of the rod's aim. When the fish is landed or lost, the mouse turns
  the view again from wherever the camera is (no snap).
- **Rod on screen** (T-075b, owner, cosmetic): the first-person arms and the rod are children of the camera, so the camera's
  turn never moves them on screen; their own side swing does (the aim offset's Right pose holds the rod ~47 deg right, past a
  90 deg view's 45). The owner's arms (and the placeholder rod turn) get the eased aim with the yaw capped so
  aim x RodAimSideDeg (RodAimLookYawDeg for the placeholder) <= `CameraMaxRodYawDeg` (35; 0 = no cap). The fight, the HUD,
  the network and other players' copies keep the full aim. The edge lift (T-047) is part of the fight: same camera, same cap.
- **Arms**: `UFPArmsAnimInstance::RodAimPitch` / `RodAimYaw` (-1..1, eased) drive an aim offset `AO_FPArms_RodAim` (base
  `A_FPArms_HoldRod_Idle`, poses `A_FPArms_RodAim_{Center, Up, Down, Left, Right, UpLeft, UpRight, DownLeft, DownRight}`; yaw +1
  = tip right, pitch +1 = pulled back). Until it is wired the fishing component turns the rod mesh itself (`RodAimLook*Deg`);
  setting `bRodAimOffsetInGraph` in ABP_FPArms' class defaults hands that over to the arms.
- **HUD** (placeholder text): `Rod: back-right  (turning it)` / `(same way: losing line)`, `Reel 2/3 (wheel or LB/RB)`,
  `Fish runs LEFT: pull right`.

**Tuning columns** (DT_FishFight, all optional: a CSV without them imports with these values): RodAimUpDeg 35, RodAimDownDeg
35, RodAimSideDeg 45, PitchBackPressure 0.3, PitchDipPressure 0.5 (< 0.95), PitchDipPower 0.8 (< 0.95; T-028b), SideMinShare 0.15, SideLeverage 0.5 (< 0.95),
SideTurnRate 1.0, SideTurnPull 0.2 (< 0.95), SideDrain 1.5, ReelSteps 3 (1-9), ReelDefaultStep 2 (1-based; its speed must
be 1, Validate checks), ReelSpeedMin 0.5, ReelSpeedMax 1.5, ReelLoadPerSpeed 1.5 (steps: speed 0.5 / 1 / 1.5, cranking load x0.25 / x1 /
x1.75), CameraFollowTime 0.35, CameraRodYawShare 0.5, CameraRodPitchShare 0.35, RodAimLookPitchDeg 20, RodAimLookYawDeg 25,
RodAimBlendTime 0.1. DT_FightPattern and DT_Gear are unchanged.

**Why the reel step adds cranking load, not a multiple of the whole tension**: a first tune multiplied the whole reeling
tension by the step. Then any fast reeling became a gamble at every move change (a dive under a fast reel overshot far past the
line before a 0.3 s reaction could help: skilled play lost 21-46 % of snappers vs 7 % for careful play). Cranking faster
loads the rod, it doesn't make the fish pull harder; with the load model skilled play is as safe as careful play on snappers.

**Targets and results** (starter kit, the fight 10 m out; model = `tools/balance/reel_fight_model.py`, 300 fish rolled like
the pipeline; C++ = `Project.Fishing.Fight.Rod.SkilledPlayBeatsHolding`, 200 real rolls; players react in 0.3 s; "skilled" =
steers against every sideways run, eases on hard moves (rod half dipped, slowest reel), dips fully above 85 %, pumps (rod 60 %
back, fastest reel) while the fish rests or is tired and reels fast through a gentle swim until the fish has once overpowered
the line, with the careful watcher on the reel button):

| Target | Model | C++ test (pinned) |
|---|---|---|
| Holding reel is unchanged (T-007 tune) | 42 % of bonefish lost, landed median 9.0 s | 82 of 200 lost (test: 25-60 %) |
| SAFER: skilled play loses (almost) nothing | 0 % bonefish lost | 0 of 200 (test: <= 2 %) |
| FASTER: skilled vs holding on the fish holding lands | median time x0.74 (p90 x0.92) | x0.73 (test: <= 0.85) |
| Reference fish | 1.5 kg Common: skilled 6.8 s vs hold 9.6 s; Rare 2.04 kg (the playtest fish): hold snaps, skilled 9.8 s vs careful 13.4 s | 6 seeds each: skilled faster than hold / than careful |
| Steering against runs alone helps | 14 % lost (vs 42 %), faster on the same fish (x0.89 in C++) | 36 vs 82 lost (test: <= 60 % of hold's losses) |
| Steering WITH the run loses ground | same losses, x1.16 slower | x1.17 (test: >= 1.05) |
| Rod held back / fastest reel held: riskier | 62 % / 51 % lost | 140 / 100 of 200 (test: more than hold) |
| The snapper stays harder, skilled play no less safe | careful 7 % lost (17.6 s), skilled 6 % (15.7 s) | 100 rolls: skilled 5, careful 6 lost; skilled snapper 15.0 s vs bonefish 8.5 s |
| Reef kit bonefish | hold 6.7 s, skilled 5.2 s (x0.77) | - |

## T-028b: follow-ups of the T-028 senior QA (2026-09-23; QA report Saved/AgentLogs/qa/20260923-185837-T028.md)
- **O1, rod-down posture closed.** Rod fully dipped + fastest reel + reel held + rod against the run matched skilled play
  without watching the bar: the dip's tension relief cancelled the fast reel's extra load, and the dip cost only the same 0.5
  of the rod's power. New column `PitchDipPower` 0.8 (the rod's power share lost when fully dipped; tension relief still
  `PitchDipPressure`). Data only: `PitchDipPower = PitchDipPressure` restores the T-028 rod. The neutral rod is unchanged.
  Tests: `Project.Fishing.Fight.Rod.T028b.DipIsReliefNotReeling`, `.DippedFastPostureLosesToSkilledPlay`.
- **O2, one slack rule** (`FLureFight::IsSlack`): slack = NOT reeling and tension < `SlackShare` x base pull. It drives the
  thrown-hook timer, the stamina recovery and the HUD. Reeling always takes up slack (whatever the rod and the reel step), so
  reeling with the rod dipped at the slowest step never throws the hook, and the HUD never shows "Reeling." next to "Slack
  line". Tests: `.OneSlackRule`, `.HudNeverContradictsItself`.
- **O4 / O5.** A teleport (`APawn::TeleportTo`, e.g. `Lure.Teleport`; not a `bIsATest` probe) ends a fight: the fish is lost,
  reason `Teleported` ("The fish got away (teleported)."). Unpossessing a pawn (a respawn, a pawn switch) ends its fight:
  reason `Unpossessed` ("the player left"). A line with no fish on is not affected. Tests: `.TeleportEndsTheFight`,
  `.PawnSwitchEndsTheOldFight`.
- **O6, server rate limit on reel-step changes** (`ULureFishingSettings`, a token bucket): up to `FightReelStepBurst` (4)
  changes at once, then `FightReelStepsPerSecond` (10; 0 = no limit). A change over the limit waits and applies when the
  limit allows; the latest step asked for wins; asking for the step in use drops a waiting change. The aim is never held back.
  Test: `.ServerRateLimitsReelSteps` (and `Rod.QA.Reel.WheelSpamClampsAndSendsTheLast`, updated for the wait).
- **O7.** `FFishDataValidator::ValidateCsvSource(Csv, RowStruct, Table)` checks a CSV source's raw text: unknown columns, the
  cell count, True/False for bools, plain numbers in number cells (whole numbers in int cells). The engine's importer
  silently turns 'fast' into 0 or '45deg' into 45. Test: `.TextInANumberCellFailsValidation` (shipped DT_FishFight and DT_Gear pass).
- **O8.** `Validate` requires the default reel step's speed to be 1 (`.DefaultReelStepMustBeSpeedOne`).
- Renamed: `Project.Fishing.QA.Net.OnlyChargeAndYawCrossTheWire` -> `Project.Fishing.QA.Net.ServerRpcsTakeOnlyPlainNumbers`.

**O1 numbers** (starter kit, lost / landed median; players react in 0.3 s; "advice" = QA's skilled player: rod against the
run and a little back during a run, ease off at 90 % of the bar, reel again under 60 %; "posture" = rod fully dipped, fastest
reel, reel held, rod against the run). Acceptance: the posture loses >= 3x as many fish or takes >= 30 % longer; skilled play
within about 10 % of before.

| | Before (T-028) | After (T-028b) |
|---|---|---|
| C++ bonefish 200 rolls (seed 31000): advice | 0 lost, 8.5 s | 0 lost, 8.5 s |
| C++ bonefish: posture | 0 lost, 7.8 s | 7 lost, 12.7 s (x1.51) |
| C++ snapper 100 rolls (seed 34000): advice | 5 lost, 18.1 s | 4 lost, 18.5 s |
| C++ snapper: posture | - | 87 lost, 40.0 s (x2.17) |
| Model bonefish 200 rolled: advice / posture | 0 lost 9.4 s / 1 lost 8.3 s | 0 lost 9.4 s / 3 lost 13.7 s |
| Model snapper 200 rolled: advice / posture | 12 lost 16.4 s / 145 lost 12.5 s | 9 lost 16.3 s / 165 lost 31.0 s |

## The fish stays put (T-045, 2026-09-24)
Jimmy (A2 playtest): "WHEN a player walks around while reeling in a fish, THEN the bobber + fish follow the players movements.
... If the player moves away, the bobber and fish should remain in the same place and only be reeled towards the player's
direction". Before, the fish was drawn at `player + direction x LineOut`, so it moved with the player.
- **The server holds the fish's world position** (water plane XY; its depth stays the cosmetic `Depth`). `FLureFightState`
  keeps `LineOut` and `SideDeg` measured from `Anchor` (the player's XY the fight last saw) and `BaseDir` (the line's
  direction at the hook); `FLureFight::FishLocation` = `Anchor + (BaseDir turned by SideDeg) x LineOut`. The fight starts
  with the fish at the bobber (`PlaceFish`).
- **Only the fish's own swimming and the reel move it.** The step is unchanged: a run or the reel changes `LineOut` (along
  the line), the swing changes `SideDeg` (across it), both about the player's current position. So **reeling pulls the
  fish toward where the player is now**. "The player" is the pawn's location (`GetActorLocation`), not the rod tip: the
  start distance and `LandDistance` were always measured from the pawn, and the server has no rod mesh.
- **Walking** (`FLureFight::MovePlayer`, each server update before the steps): when the pawn moved, `LineOut` and `SideDeg`
  are measured again from the new place, so the fish stays where it is. `LineOut` is always the real horizontal distance:
  walking away pays out line with no extra tension; walking toward the fish shortens the line (the simplest consistent
  rule: no slack is stored or drawn). The tension, stamina and moves never depend on where the player is. Consequences:
  walking to within `LandDistance` of the fish lands it (Sprint 2's T-057 makes landing need a tired fish), and walking
  away past `SpoolLength` spools the line (fight-v2.md rule 11: walked-out line is off the spool).
- **The swing limit** (`MaxSideDeg`) holds the fish's own swing only. A player who walks round the fish can leave it past
  the limit: it stays where it is (never pulled back), and only its swing back toward the middle is free. A still player's
  fish is always inside the limit, so for a still player the fight is the pre-T-045 fight **bit for bit** (every T-007 and
  T-028 number above holds).
- **Replication**: `FLureFightNetState::FishLocation` (`FVector_NetQuantize10`, Z = the water surface = `BobberRest.Z`).
  The server publishes it already rounded the way the wire rounds it (0.1 cm steps), so its `FightNet` equals every
  client's copy bit for bit and the host draws exactly what clients draw.
  Every machine draws the bobber (`ComputeBobberPose`), the fish visual (`FFightFishViewAdapter`: `LineEnd`) and the line's
  end there, and the owner's camera follows it; a client's own pawn position no longer matters. A late joiner gets it
  with the rest of `FightNet`. `LineOut` and `SideDeg` still replicate (HUD, rod hint). The owner leaving mid-fight ends
  the fight as before (T-028b O5).
- QA tests that pinned the old "fish at player + direction x LineOut" contract and need updating (qa-engineer):
  `Fishing.Fight.QA.Replication.ProxyFollowsServerFight` ("bobber on the fish, LineOut from their player": copies now
  draw it at `FishLocation`, whoever stands where), `Fishing.Fight.QA.Component.FightSuspendsBobberDistanceRule` (walks
  ~50 m away: now past `SpoolLength`, so Spooled; walk to between MaxLineLength 2600 and SpoolLength 4000 cm instead),
  `FishVisual.QA.Adapter.EveryStateAndEnding` (hand-built state without `FishLocation`).
- Tests: `Project.Fishing.Fight.Anchor.*` (`Tests/FishFight/FightAnchorTest.cpp`): a still player is the old fight bit for
  bit; walking 5 m to the side and 5 m back leaves a still fish exactly put and a swimming fish moving only by its own
  swim (compared with the same seed and a still player); the reel pulls toward the moved player; the world repro (bobber,
  replicated location and fish visual stay put while the angler walks, then it is reeled in and lands); host and clients
  (the owner's copy and another player's copy standing elsewhere, a late joiner, the owner leaving).

## Dock edges (T-047, 2026-09-24)
Jimmy (A2 playtest): "WHEN player reels in a fish while standing on a dock, AND the player moves backwards, THEN the fish
will magically phase thru the dock along with the fishing line. Instead, have proper physics where the fish instead is
lifted out of the water at the EDGE of the dock, where the fishing line can not phase thru".
- **The server looks for an edge** (`FLureFightEdgeQuery::Find`, `Fishing/FightEdge.h`; the fight's only world query):
  a capsule of radius `EdgeClearance` spanning the water line (`EdgeProbeDepth` under the surface to `EdgeProbeHeight` over
  it, surface = `BobberRest.Z`) is swept from the fish toward the pawn on the cast channel. What stops a cast stops the fish
  (`FLureFishingSpots::BlocksCast`: zones, triggers, pawns, overlap-only shapes never do): a dock face, pilings, a boat,
  the shore where the ground comes within `EdgeProbeDepth` of the surface, a deck lower than `EdgeProbeHeight` over the
  water (higher decks: the fish swims in under them). The edge is a wall in world XY (`FLureFightEdge`: the point where the
  fish touches it, `EdgeClearance` out from the face, and the face's horizontal normal; a nearly flat hit such as the
  shore's slope is taken square to the path). Its top: a trace straight down `EdgeTopInset` past the face;
  `LandLift = top - surface + EdgeLiftClearance`, at most `EdgeMaxLift` (no top found under `EdgeMaxLift`: `EdgeMaxLift`).
- **When**: every `EdgeQueryInterval` s (never more often than the fight's steps) while the fish is in the water; a lifted
  fish keeps the edge it was lifted over (a look from over the deck would start inside it). `EdgeClearance 0` = no looks.
- **The pure fight** (`FLureFight::SetEdge / EdgeBlocks / EdgeLineOut / MoveOnTheLine`, `FLureFightState::Edge / Lift /
  LiftHeld`): while the edge stands between the fish and the player, the line change of step 3 runs along one path: on the
  water out to the edge, up the edge (`Lift` 0 to `LandLift`), then in over the dock at that height. So reeling brings the
  fish in to the edge (never behind it), lifts it **straight up** there, then carries it in over the top; a run takes it the
  same way back (down the edge first). A lifted fish is never slack (its weight keeps the line taut), doesn't swing or dive;
  a swing that would take a fish on the water behind the edge doesn't happen. With no edge (or none on the fish's bearing)
  the fight is the T-045 fight bit for bit.
- **Landing at an edge**: over the top (`Lift >= LandLift`) and within `LandDistance`, for longer than `EdgeLandHold`
  (every machine's smoothed fish has risen by then). `LandDistance` on the water doesn't apply: the fish can't come in under
  the dock. Why carried in over the top instead of landed at the edge: the T-030 hang starts where the fish is, and a hang
  started at the edge with the angler far back swings down through the deck (the hanging line doesn't collide; a 2D model of
  the hang reel put the fish 13-120 cm under the deck for anglers 6-8 m back, above it up to about 4 m). Why
  `EdgeLiftClearance` 80: a landed fish flips head up on the line (T-030) and a Common bonefish's mesh then reaches ~57 cm
  below its mouth; with 40 its tail went ~36 cm into the deck for the first 0.1 s of the hang (the hang reel lifts it
  quickly). Measured in `DockEdge.World.WalkBackWhileReeling` (info line "hang: ... mesh bounds").
- **Every machine**: `FLureFightNetState::Lift` replicates. The bobber (`ComputeBobberPose`) and the fish visual
  (`FFightFishViewAdapter`: `WaterZ` and `LineEnd` raised by `Lift`, kept after the fight so a landed fish is handed on
  where it was lifted to) rise with it. The line collides with the dock while the fish is on (T-032b), so it bends over the
  edge instead of cutting through it.
- **Edge cases**: a player who walks round the edge's line while the fish is over the deck leaves it up (it drops back
  only over water); a fish found behind a new edge is put back in front of it (a jump outward, rare); a fish that snaps the
  line while lifted swims away from where it was (the escape swim sinks it; open: drop it into the water first).
- Tests: `Project.Fishing.Fight.DockEdge.*` (`Tests/FishFight/FightDockEdgeTest.cpp`): Sim (stops, goes straight up, comes
  in over the top, lands; the path step by step; the swing stops at the edge), Data (columns, defaults, validation, optional),
  Query (dock face, angle, touching, open water, trigger zone, piling, deck on posts and under a high deck, beach, off),
  World (Jimmy's repro: reel and walk 2.5 m back on the QA dock; the real bonefish from the dock's end with a client's copy;
  no line point inside the dock during the fight, the lift and the hang).

## Open questions (for Jimmy after playtest A)
1. Snap speed: holding reel on a snapper with starter gear snaps the line in under a second. Too punishing, or the right
   "you need better gear" signal? (Knob: SnapGraceTime.)
2. Should a lost fish (snap, spool, thrown hook) cost anything (lost bait/lure, line length), or just the fish?
3. Is one button (hold to reel) enough, or does he want a separate "ease off / let it run" action or a drag setting he
   can change mid-fight?
4. Should the tension bar warn (colour/flash) near the snap point, or stay plain text until he directs the UI?
5. Fight length: 8-15 s for starter fish. Longer and more tense, or shorter?
6. (T-028) Rod steering feel: how far the mouse must move for a full rod swing (RodAimSideDeg / RodAimUpDeg), how much the
   camera follows the rod (CameraRodYawShare), and whether the rod should drift back to level on its own.
7. (T-028) Is "Fish runs LEFT: pull right" on the HUD enough to read a run, or should the run show more (bigger swing,
   splash) once the fish is visible (T-029)?

## Merge notes
- QA tests expecting AutoLandDelay 1.5 (T-006 placeholder landing) must set AutoLandDelay > 0 in their fixture.
- Test helpers must keep transient fixture UObjects alive (TStrongObjectPtr or rooted): `UWorld::Tick` runs
  `ConditionalCollectGarbage`, so a long full-suite run can collect a raw-pointer table mid-test (seen in T-007 in
  `Project.Movement.QA.Eye.MidTransitionIsBetween`; fixed in `QAMovementTestUtils.cpp` and `FishingTest.cpp`).
- After the merge the editor-operator imports `DT_Gear.csv` (LureGearRow), `DT_FightPattern.json` (LureFightPatternRow),
  `DT_FishFight.csv` (LureFishFightRow) to `/Game/Data/`, and re-imports `DT_Fishing.csv` (AutoLandDelay 0).
- T-028: re-import `DT_FishFight.csv` (22 new optional columns; an old asset still works with the defaults). Wire the aim
  offset `AO_FPArms_RodAim` in ABP_FPArms (see "Arms" above) and set its class default `bRodAimOffsetInGraph` = true.
  Tests pinning the old contract, updated: `Project.Fishing.Fight.ServerAuthority` (5 server RPCs),
  `Project.Fishing.QA.Net.OnlyChargeAndYawCrossTheWire` (byte parameters are plain numbers; renamed in T-028b to
  `Project.Fishing.QA.Net.ServerRpcsTakeOnlyPlainNumbers`),
  `Project.Movement.QA.Input.AllSixActionsResolveByName` (ReelFaster, ReelSlower).
- T-049/T-050: re-import `DT_FishFight.csv` (StaminaPerStat 5), `DT_FightPattern.json` (Run pattern: Shake opening, pulls),
  `DT_Gear.csv` (ReelSpeed 180 / 225) and `DT_FishVisual.json` (MoveRoles Shake -> Thrash). Tests updated (reasons in each):
  `Fight.GearDecidesOutcome` (the 7 kg snapper needs the reef kit: lead decision 2026-09-24), `Fight.BonefishRunPunishesHoldReel`
  (careful p10 >= 6 s / p90 <= 22 s, run-aware "clearly fewer than holding"), `Fight.Rod.SkilledPlayBeatsHolding` (steering-alone
  share <= 75 %), `Fight.PatternsDrivePullDeterministically` (the opening is Shake), `Fight.RodAndLineFollowTension` (compares with
  the line's own LineTension rule). No QA test changed.
- T-028b: re-import `DT_FishFight.csv` (new optional column PitchDipPower; an old asset uses the default 0.8). Tests updated
  for the new contract: the QA rod oracle (`Rod.QA.Sim.StepMatchesSpecWithRodInput`: dip power, one slack rule, O8-valid
  random tuning), the T-007 oracle (`Fight.QA.Sim.StepMatchesSpecFormulas`: one slack rule), `Rod.QA.Timers.SlackWholeStepsWhenTheRodDips` (reeling dipped
  never throws), `Rod.QA.Data.*` (PitchDipPower range), `Rod.PitchScalesTension` (dip power) and `Rod.QA.Reel.WheelSpamClampsAndSendsTheLast`
  (the rate limit). The rod oracle's side knife-edge skip now ignores straight moves (Side 0 never runs, whatever
  SideMinShare): with SideMinShare 0 it skipped every straight step (33 skipped of 139,736 now).

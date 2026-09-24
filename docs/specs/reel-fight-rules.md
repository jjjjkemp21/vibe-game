# Reel fight rules (T-007, rod steering T-028), unreal-engineer 2026-09-23

The reel fight between "hooked" and "landed". Code: `Source/VibeGame/Fishing/FishFight.{h,cpp}` (pure simulation; the
formulas are also in the `FishFight.h` header comment), `FishFightTypes.{h,cpp}` (gear, patterns, tuning, replicated
state), wired into `ULureFishingComponent`. Tests: `Project.Fishing.Fight.*` (`Source/VibeGame/Tests/FishFightTest.cpp`).
Rod steering and reel speed (T-028): section "Rod steering" below; tests `Project.Fishing.Fight.Rod.*`
(`Tests/FishFight/RodFightTest.cpp`); balance model `tools/balance/reel_fight_model.py`.
All numbers are PLACEHOLDER until Jimmy's playtest A.

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
- **Four outcomes** (first that applies each step): Landed (fish within `LandDistance`), Spooled (fish took more line than
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
  SpeedStat, AggressionStat (tags), PullPerStrength 0.25, SpeedPerStat 5, StaminaPerStat 3, ApplyLevelScaling True,
  RestDifficultyExponent 1.0, TiredPull 0.3, ExhaustedStamina 0.02, StaminaRecovery 0.04, ReelStrain 1.3, ReelLoad 0.15,
  DragHold 0.5, TensionRiseTime 0.12, TensionFallTime 0.25, SnapGraceTime 0.6, SlackShare 0.35, SlackGraceTime 2.5,
  LandDistance 150, SimRate 60, MaxDepth 300, DepthRecovery 80, MaxSideDeg 50, DiveBobberShare 0.1,
  RodTensionPitchDeg 25, RodShakeDeg 2.5, TautTension 0.3, DragLineCap 0.9 (optional, in (0, 1]).
  The four stat columns must be tags under `Fish.Stat.*`.
- **DT_Gear** (`data/tables/DT_Gear.csv`, struct `LureGearRow`): Rod_Starter (power 8, reel 120, drag 5),
  Rod_Reef (16, 150, 12, cast x1.15, 250 coins), Line_Mono (strength 10, spool 40 m), Line_Braid (22, 60 m, 180 coins),
  Hook_Shrimp (security 1.0, shrimp), Hook_Squid (1.6, squid, luck 0.5, 60 coins).
- **DT_FightPattern** (`data/tables/DT_FightPattern.json`, struct `LureFightPatternRow`): Run, Dive, Dart (moves above).
  Run's `Run` move after the fishing-loop tune: Weight 1.2, AggressionWeight 0.08, 0.8-1.5 s, Pull 2.4, Speed 0.7.
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

## What the numbers do today (Python prototype + tests)
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
    apply (reeling: `(Pull x ReelStrain + RodPower x ReelLoad x ReelStepLoad) x P`) and the rod's power (line gained, line
    taken while you reel). Letting it run: `min(Pull x P, Drag)`: **the drag still caps a running fish**, so letting it run
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
- **Arms**: `UFPArmsAnimInstance::RodAimPitch` / `RodAimYaw` (-1..1, eased) drive an aim offset `AO_FPArms_RodAim` (base
  `A_FPArms_HoldRod_Idle`, poses `A_FPArms_RodAim_{Center, Up, Down, Left, Right, UpLeft, UpRight, DownLeft, DownRight}`; yaw +1
  = tip right, pitch +1 = pulled back). Until it is wired the fishing component turns the rod mesh itself (`RodAimLook*Deg`);
  setting `bRodAimOffsetInGraph` in ABP_FPArms' class defaults hands that over to the arms.
- **HUD** (placeholder text): `Rod: back-right  (turning it)` / `(same way: losing line)`, `Reel 2/3 (wheel or LB/RB)`,
  `Fish runs LEFT: pull right`.

**Tuning columns** (DT_FishFight, all optional: a CSV without them imports with these values): RodAimUpDeg 35, RodAimDownDeg
35, RodAimSideDeg 45, PitchBackPressure 0.3, PitchDipPressure 0.5 (< 0.95), SideMinShare 0.15, SideLeverage 0.5 (< 0.95),
SideTurnRate 1.0, SideTurnPull 0.2 (< 0.95), SideDrain 1.5, ReelSteps 3 (1-9), ReelDefaultStep 2 (1-based; its speed should
be 1), ReelSpeedMin 0.5, ReelSpeedMax 1.5, ReelLoadPerSpeed 1.5 (steps: speed 0.5 / 1 / 1.5, cranking load x0.25 / x1 /
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
  `Project.Fishing.QA.Net.OnlyChargeAndYawCrossTheWire` (byte parameters are plain numbers),
  `Project.Movement.QA.Input.AllSixActionsResolveByName` (ReelFaster, ReelSlower).

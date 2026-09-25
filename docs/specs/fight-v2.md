# Fight v2: line length, reel in / let out, tension-driven fight length, retrieve, bobber dip (T-052): design spec
Status: accepted by the lead 2026-09-24 (questions 2-4 decided by the lead as recommended; 1 is with Jimmy). Owner: design. Engineering owns section 7.
Builds on: `reel-fight-rules.md` (T-007/T-028/T-028b fight; this spec changes only what it names), `fishing-rules.md`
(cast, bite, hook), `fishing-line.md` (the cosmetic line). Sprint 1 comes first and this spec keeps what it delivers:
T-049 (an even fight, with no near-snap spike right after the hook), T-050 (a faster reel from data; target: a well-played
beginner Bonefish lands in about 15-20 s), T-045 (the bobber and fish stay put while you walk), T-046 (some line tension
after a cast), T-047 (the fish and line never pass through the dock) and T-048 (hooked at the mouth). If a Sprint 1
result conflicts with a rule here, the Sprint 1 rule wins and this spec is updated.

Jimmy's words (A2 playtest, 2026-09-24, `Playtest/2026-09-24 A2 build playtest feedback.txt`):
- "Fishing line length should be limited, if a player lets a fish "run", then they will only have a limited amount of
  fishing line length before it runs out and their tension exceeds 100% and the line snaps"
- "Player should have the ability to reel in, or let line out"
- "the speed of the battle should be dictated by the tension management (ex: allowing a fish to run as to not snap your
  line increases fight time). I want to have the battles feel intense for tougher fish in the future"
- "AFTER a user has cast, instead of allowing them to instantly reel in their line, it should reel in realistically and
  user sees the bobber moving towards them"
- "During a fish fight, the bobber can dip under water which is how real fishing works"

## 1. Goal
The fight becomes a real tension game with three choices: reel in, hold, or let line out. Letting a fish run keeps your
line safe, but it uses up the spool and makes the fight longer. Cranking tires the fish fast but risks a snap. The fight
lasts as long as your tension management makes it, so tougher fish (more stamina, more pull) mean longer, more intense
fights. The cast and the bobber behave like real tackle: the bobber is reeled back across the water, and it goes under
when the fish pulls. Pillars: 1 (fishing first), 7 (every number is data), 4 (server-authoritative, readable to friends).

## 2. Player experience
1. You cast and the bobber lands 15 m out. To try another spot you hold click: the bobber skims back toward you across
   the water, and the mouse wheel sets how fast. Let go and it stops and floats. A fish can bite where it stopped. Hold
   until the bobber reaches your feet or the dock edge and it lifts out of the water to the rod tip. Now you can cast again.
2. A fish bites and you hook it. The HUD text shows `Tension 45%`, `Line left 18 m` and what you are doing: `Reeling`,
   `Holding` or `Letting line out`.
3. The fish runs. The tension climbs and the bobber is dragged under the surface. You hold right click / LT to let line
   out: the tension drops to a gentle pull and the fish takes line, so `Line left` counts down.
4. The fish rests. The bobber pops back up. You reel and get line back, and the fish tires while you keep the tension up.
5. If you let it run too long, the HUD shows `LOW LINE: 4 m left`. When the spool is empty the fish still pulls, the
   tension goes past 100% (`Tension 124%`), and after a moment the line snaps: "Out of line! The line snapped."
6. You bring the fish close while it still has fight in it, and it bolts away again (a last run). You only land it once
   it is tired. A beginner Bonefish played well takes about 15-20 s. A tougher fish takes longer and keeps the tension
   high more often.
7. With friends, each player's fight runs on the server. Everyone sees each bobber go under and pop up, each bobber being
   retrieved, and (in debug text) each player's line left.

## 3. Rules
Terms: `LineOut` = the line between the rod and the fish (cm; the fight sim's LineOut). `SpoolLength` = the equipped
line's capacity (DT_Gear). `LineLeft = SpoolLength - LineOut`. `Tension%` = Tension / LineStrength x 100 (the HUD number;
a snap happens above 100 %). `P` = the rod's pitch pressure (T-028). Everything below that is not named stays as in
reel-fight-rules.md.

### 3.1 Reel in, hold, let out (T-054)
1. A fish is on, so there are three reel modes. **Reel**: hold Cast (LMB / RT). **Let out**: hold the new LetOut action
   (RMB / LT by default; keys in `ULureFishingSettings.LetOutKeys`). **Hold**: neither is held. If both are held, Let out
   wins, because a safety input must never be blocked.
2. Reel is the existing reeling branch, unchanged. Hold is the existing "not reeling" branch, unchanged: the drag holds
   (`DragHold x Drag`) and gives line at `Drag`.
3. Let out works like Hold with a loose drag. `LetOutDrag = Drag x LetOutDragShare`, and it replaces `Drag` in both of the
   Hold formulas: tension target = `min(Pull x P, LetOutDrag)`, and line taken =
   `Va x clamp((Pull / LetOutDrag - DragHold) / (1 - DragHold), 0, 1)`. A running fish takes line almost freely and the
   tension drops to a gentle pull.
4. Let out never pays out line by itself. With no fish pulling, the line just goes slack, and the existing slack rule
   (`IsSlack`: not reeling and tension below SlackShare x base pull) runs the thrown-hook timer. Let out counts as "not
   reeling".
5. The reel-speed steps (wheel / bumpers) and rod steering (T-028) work as before. They also work during a retrieve
   (3.4). Let out does nothing when no fish is on.
6. The HUD shows the mode: `Reeling`, `Holding` or `Letting line out`.

### 3.2 Limited line (T-053)
7. LineOut never grows past SpoolLength. When a step would take it past, LineOut stops at SpoolLength and the spool is
   **empty**.
8. While the spool is empty, the drag gives no line, so the tension targets lose their drag cap. Hold and Let out:
   target = `Pull x P`. Reel is unchanged. Also, while the fish swims away (`Va > 0`), the target is at least
   `LineStrength x OutOfLineTension`. So a running fish on an empty spool always pushes the tension past 100 %, whatever
   the input.
9. The existing snap rule decides the end: tension above LineStrength for longer than SnapGraceTime -> result Snapped,
   reason **OutOfLine** (HUD: "Out of line! The line snapped."). This replaces today's instant `Spooled` loss.
   Counterplay inside the grace: the run ends (a rest, or a run turned early by steering against it, T-028), and the
   timer resets as it does today.
10. The HUD shows `Line left N m` (whole meters, rounded down) during a fight, and `LOW LINE: N m left` once LineLeft is
    below `LowLineWarning`. The tension % is never clamped at 100 in the HUD.
11. The spool counts all the line between the rod and the fish, however it got there. If the player walks away from a
    hooked fish (with T-045 the fish stays put in the world), that distance is line off the spool too.
12. Validation: every Line row has `SpoolLength >= MaxCastDistance x the largest rod CastDistanceMultiplier +
    LowLineWarning`. A full cast never starts a fight already in the warning zone.

### 3.3 Fight length follows tension management (T-057)
13. **Landing needs a tired fish.** The fish is landed when `LineOut <= LandDistance` **and** `Stamina <= LandStamina`.
14. While `Stamina > LandStamina`, the reel cannot bring the fish closer than LandDistance: reel gain stops there. When the
    fish reaches LandDistance with stamina above LandStamina, it **bolts**: its pattern's `BoltMove` starts in that step
    (a fresh move clock and duration). It bolts at most once every `BoltCooldown` s. Between bolts it makes its normal
    moves. A bolt's tension eases in through TensionRiseTime like any other move, so it adds no spike (T-049 holds).
15. Stamina still drains by tension x time (reel-fight-rules.md step 5). So **the fight lasts as long as it takes the
    player's tension to empty the fish's stamina pool**:
    - held tension near the line's limit (reeling, rod back) tires the fish fastest, but risks the snap;
    - Hold caps the tension at the drag;
    - Let out drops it to `LetOutDrag`, so the fish barely tires, a slack line lets it recover (StaminaRecovery), and all
      the line it took must be reeled back.
    Letting a fish run lengthens the fight three ways, as Jimmy asked.
16. Tougher fish are more intense through data alone: a higher Stamina stat means a bigger pool (a longer fight), and a
    higher Strength stat means more pull (more time near the line's limit and more line lost to runs). A new tough species
    is a DT_FishSpecies row plus, optionally, a DT_FightPattern row. No code.
17. The stamina pool is retuned so the fight length comes from the pool, not from the line left to reel. The start value
    is in section 4, and the balance targets in 3.6 decide the final value (on top of T-050's reel speed).

### 3.4 Retrieve after a cast: no instant reel-in (T-055)
18. With the line out and no fish on (Waiting or nibbles; the bobber on water), holding Cast **retrieves**. The server
    moves the bobber's rest point horizontally straight toward the rod tip at `RetrieveSpeed x the reel step's speed`
    (the T-028 steps: 0.5 / 1 / 1.5), staying on the water surface. Releasing stops it, and the bobber floats where it
    is. Fishing goes on from there, with the water area under the bobber deciding the bite as usual.
19. While the bobber moves, no nibble or bite starts: the bite-wait clock pauses, and it continues when the bobber stops.
    A press during a bite (Biting) always hooks, as today, and never starts a retrieve. With `EarlyHook = ReelIn` (the
    default), a press during nibbles starts a retrieve instead of ending the cast. `Spook` and `Ignore` are unchanged.
20. The cast ends (state Idle, a new cast allowed) when the bobber is within `RetrieveEndDistance` of the rod tip
    (horizontal), or when its next step would leave the water (a beach, a dock edge, a rock). Then it lifts out of the
    water and swings up to the rod tip over `RetrieveLiftTime` s, and the line reels up with it. It never passes through
    the dock or the ground (T-047's collision).
21. A bobber lying on land lifts and comes back to the rod tip in `RetrieveLiftTime` s when Cast is held. It is not
    dragged over the ground.
22. During a retrieve the line pulls straight at `RetrieveTension` (cosmetic, DT_FishingLine). The moving bobber is the
    source point for the T-069 water ripples (the water spec). Until T-069 lands, nothing extra is drawn.
23. Forced reel-ins stay instant, as today: sprint, swim, climb, prone crawl, walking past MaxLineLength, a lost fish.

### 3.5 The bobber dips during a fight (T-056)
24. In a fight the bobber rides the line in front of the fish, where T-048 put it. Its depth under the surface is
    `D = min(BobberMaxDip, max(DiveBobberShare x FishDepth, BobberMaxDip x s))`, where
    `s = clamp((Tension% / 100 - BobberDipStart) / (BobberDipFull - BobberDipStart), 0, 1)`.
    So a hard pull or a dive drags it under, and a rest lets it float.
25. The depth eases toward D: deeper in `BobberDipTime`, shallower in `BobberPopTime` (time constants). An exhausted fish,
    or a slack line, leaves the bobber on the surface, bobbing as in the wait.
26. When the bobber comes back up from deeper than `BobberSplashDepth`, the optional pop cue plays (a sound and a small
    splash in settings; empty = none). T-069 adds a ripple ring at that point.
27. It is cosmetic. Every machine computes D from the replicated fight state (tension, line strength, fish depth), so
    there is no new replication. In clear shallows the dipped bobber stays visible under the surface.

### 3.6 Balance targets (T-057 pins these; reel-fight-rules.md defines the bots)
Starter kit. The fight starts 10 m out unless noted. Rolled fish like the pipeline (rarity, weight, modifiers), bot
reactions 0.3 s. The **let-it-run** bot is new: it holds Let out whenever the tension is over 50 %, and otherwise reels.

| # | Target |
|---|---|
| B1 | Skilled bot, Common Bonefish 1-2 kg: median land time within [FightTargetMin, FightTargetMax] = **15-20 s** (the same target as T-050; placeholder until Jimmy's number); p90 <= 1.5 x the median |
| B2 | Skilled and careful bots lose <= 2 % of all rolled Bonefish |
| B3 | The let-it-run bot's median land time is >= 1.3 x the skilled bot's on the same fish seeds |
| B4 | A bot that holds Let out all fight and never reels never lands a Common Bonefish from 18 m out and loses >= 50 % of them to OutOfLine |
| B5 | Holding reel non-stop loses more Bonefish than skilled play (the T-007 lesson stays) |
| B6 | Reference Coral Snapper on the reef kit, skilled bot: median time >= 1.5 x the Common Bonefish's, and time above 70 % tension >= 2 x the Bonefish's share |
| B7 | Doubling a species' Stamina stat (data only) makes the skilled bot's median fight >= 1.6 x as long |

## 4. Tuning data
New columns are optional, so an old asset imports with these defaults. **NEW** = new column. The start values are
proposals, and T-057's balance run sets the finals (recorded in reel-fight-rules.md, with the model results).

| Table | Row | Column | Start | Unit | Why |
|---|---|---|---|---|---|
| DT_FishFight | Default | LetOutDragShare **NEW** | 0.2 | share of Drag | starter drag 5 -> 1: a Bonefish run (pull ~2.5) takes line almost freely at a gentle tension |
| DT_FishFight | Default | OutOfLineTension **NEW** | 1.3 | x LineStrength | an empty spool on a run crosses 100 % in ~0.2 s (TensionRiseTime 0.12), so the snap follows ~0.8 s after (SnapGraceTime 0.6) |
| DT_FishFight | Default | LowLineWarning **NEW** | 500 | cm | ~5 s of Bonefish swim left: time to steer against the run or reel during a rest |
| DT_FishFight | Default | LandStamina **NEW** | 0.2 | share of pool | at 20 % the pull is ~0.44 of fresh (TiredPull 0.3): calm enough to lift out |
| DT_FishFight | Default | BoltCooldown **NEW** | 3.0 | s | one last run per approach; the player has time to recover line between bolts |
| DT_FishFight | Default | StaminaPerStat | 3 -> 6 | tension-s per stat | Common Bonefish pool 14 x 6 = 84; a skilled average of ~5 tension empties it in ~17 s (B1). The final value comes from B1 with T-050's reel |
| DT_FishFight | Default | BobberDipStart **NEW** | 0.35 | share of LineStrength | just past TautTension 0.3: the bobber goes under once the line pulls straight |
| DT_FishFight | Default | BobberDipFull **NEW** | 0.85 | share | fully under near the danger zone: a visible warning next to the HUD number |
| DT_FishFight | Default | BobberMaxDip **NEW** | 60 | cm | the 3x bobber (~15 cm) is clearly under, and still visible in clear shallows |
| DT_FishFight | Default | BobberDipTime **NEW** | 0.12 | s | snaps under with the pull (matches TensionRiseTime) |
| DT_FishFight | Default | BobberPopTime **NEW** | 0.3 | s | floats up a little slower, which reads as buoyancy |
| DT_FishFight | Default | BobberSplashDepth **NEW** | 15 | cm | a pop cue only after a real dip |
| DT_FishFight | Default | DiveBobberShare | 0.1 | share | unchanged: a dive still pulls the bobber under |
| DT_FightPattern | every row | BoltMove **NEW** | empty | move id | empty = the pattern's highest-Pull non-rest move (Run: Run, Dive: Dive, Dart: Dart) |
| DT_Gear | Line_Mono | SpoolLength | 4000 -> 3000 | cm | a max cast (18 m) leaves 12 m to run: letting a fish run from a long cast is a real risk (B4) |
| DT_Gear | Line_Braid | SpoolLength | 6000 | cm | unchanged: upgrading the line buys run room |
| DT_Fishing | Default | RetrieveSpeed **NEW** | 350 | cm/s | walk speed: a full 18 m cast comes back in ~5 s at the default step (~3.4 s at the fast step) |
| DT_Fishing | Default | RetrieveEndDistance **NEW** | 150 | cm | the bobber is at your feet or the dock edge |
| DT_Fishing | Default | RetrieveLiftTime **NEW** | 0.4 | s | a quick lift out of the water to the rod tip, not a teleport |
| DT_FishingLine | Default | RetrieveTension **NEW** | 0.5 | tautness | the line runs nearly straight to a bobber being reeled |
| ULureFishingSettings | - | LetOutKeys **NEW** | RMB, Gamepad_LeftTrigger | keys | both free today; mirrors reel on LMB/RT |
| ULureFishingSettings | - | BobberPopSound / BobberPopSplash **NEW** | empty | asset | optional cue, off until art/audio supply one |

## 5. Acceptance criteria
Line length (T-053)
- AC1 [auto] Given a fight with LineOut = SpoolLength - 1 cm and a fish swimming away, when steps run, then LineOut
  never exceeds SpoolLength.
- AC2 [auto] Given an empty spool and a fish with Va > 0, for each input (Reel, Hold, Let out) and a neutral rod: the
  tension exceeds LineStrength within 0.3 s, and the fight ends Snapped with reason OutOfLine SnapGraceTime after the
  crossing (+-1 step).
- AC3 [auto] Given an empty spool, when the fish starts a rest move before the grace ends, then no snap happens and the
  snap timer is 0 once the tension is back under LineStrength.
- AC4 [auto] The HUD text shows `Line left N m` = floor(LineLeft / 100) and, below LowLineWarning, `LOW LINE`. The tension
  text shows values above 100 % unclamped (e.g. 124 %).
- AC5 [auto] Gear is data: with the same seed and inputs, Line_Braid (6000) reaches the empty spool 3000 cm of LineOut
  later than Line_Mono (3000), with no code change.
- AC6 [auto] Data validation fails a Line row whose SpoolLength < MaxCastDistance x the largest CastDistanceMultiplier +
  LowLineWarning.

Reel in / let out (T-054)
- AC7 [auto] Same seed, a running fish: with Let out held, LineOut grows faster than with nothing held, and the tension
  settles at min(Pull x P, Drag x LetOutDragShare) (+-1 %).
- AC8 [auto] Cast and LetOut both held = the Let out result, bit for bit.
- AC9 [auto] A resting fish with Let out held throws the hook after SlackGraceTime x HookSecurity (+-1 step), the same as
  Hold.
- AC10 [auto] With no fish on, LetOut changes nothing (state, bobber, line).
- AC11 [auto] The `LetOut` action resolves by name with RMB and Gamepad_LeftTrigger. No key is bound to two actions (the
  existing QA rule), and T-051's debug menu lists it.
- AC12 [auto] The HUD shows `Reeling`, `Holding` or `Letting line out` to match the mode on the server.

Fight length (T-057)
- AC13 [auto] A fish with Stamina > LandStamina is never landed, and LineOut never drops below LandDistance while it is.
- AC14 [auto] When the fish reaches LandDistance with Stamina > LandStamina and no bolt in the last BoltCooldown s, the
  pattern's BoltMove (or, if empty, the highest-Pull non-rest move) starts in that step.
- AC15 [auto] A fish with Stamina <= LandStamina at LineOut <= LandDistance is Landed in that step.
- AC16 [auto] Balance targets B1-B7 (3.6) hold in the balance model and in C++ bot tests on the shipped data.
- AC17 [play] The playtester lands 3 Bonefish with skilled play: each takes 12-25 s, with at least one bolt seen and one
  run let out. The HUD screenshots show the line left dropping during the let-out.

Retrieve (T-055)
- AC18 [auto] Given the bobber on water 1500 cm from the rod tip, when Cast is held for 1.0 s at the default step, then
  the horizontal distance is 1500 - RetrieveSpeed (+-5 cm) and the state is Waiting.
- AC19 [auto] A one-frame press never ends the cast: afterwards the bobber is on the water, the state is Waiting, and it
  is at most RetrieveSpeed x 1.5 x frame time closer.
- AC20 [auto] Holding until the bobber is within RetrieveEndDistance ends the cast (Idle) RetrieveLiftTime later, and a
  new cast is accepted.
- AC21 [auto] No nibble or bite starts while the bobber moves. With a retrieve in the middle, the bite comes after the
  rolled wait counted in still time only (+-1 frame).
- AC22 [auto] A press during a bite hooks as before and never moves the bobber.
- AC23 [auto] Retrieving toward a dock edge (the dev dock map), the cast ends at the edge and the bobber never goes
  inside the dock's collision.
- AC24 [play] Cast 15 m or more, then hold click: screenshots at 0, 1 and 2 s show the bobber closer each time, with the
  line leading to it. Release: it floats and a bite can still come.

Bobber dip (T-056)
- AC25 [auto] The pure bobber-depth rule: Tension% <= BobberDipStart x 100 with a surface fish -> D = 0; Tension% >=
  BobberDipFull x 100 -> D = BobberMaxDip; a diving fish -> D >= DiveBobberShare x depth. Eased depth reaches 95 % of D
  within 3 x BobberDipTime (down) or 3 x BobberPopTime (up).
- AC26 [auto] An exhausted fish: the bobber is at the surface (D = 0) within 3 x BobberPopTime.
- AC27 [play] During a Bonefish run, a screenshot shows the bobber's red top below the water surface and the bobber still
  visible in the shallows. During a rest, a screenshot shows it floating.

Multiplayer (listen server + 1 client)
- AC28 [auto] A client's Let out press and release reach the server reliably. With a lost-packet simulation, a release
  is never lost. The server's sim alone decides LineOut, tension and the outcome, and the client's "Line left" equals
  the server's within one replication update.
- AC29 [auto] A client cannot move its bobber: the retrieve runs on the server from the reel input. The bobber never
  moves faster than RetrieveSpeed x ReelSpeedMax, and it stays within 5 cm of the server's position once smoothing
  settles.
- AC30 [auto] On the other player's machine the dipped bobber's depth equals the rule in AC25 applied to the replicated
  fight state (+-1 cm once settled).
- AC31 [play] In 2-player PIE, player 2's screenshots show player 1's bobber under the water during a run, and player 1's
  retrieved bobber moving steadily toward player 1 (no visible jumps).

## 6. Out of scope
- A drag dial the player sets mid-fight (Let out covers "let line out"; a drag setting could come with gear upgrades, T-012+).
- Losing line or tackle after a snap, spool refills, line wear (open question 2 in reel-fight-rules.md stays open).
- Lines crossing or tangling between friends. Two anglers' fights are independent.
- Lifting the fish out at the dock edge (T-047, Sprint 1). Fish animation and upright tired fish (T-058/T-059, Sprint 3).
- Water ripples from the bobber and line (T-069; the water spec defines the ripple source this spec points to).
- A retrieve animation for forced reel-ins (they stay instant, rule 23).

## 7. Contract (engineering)
To be written by engineering (the T-053/T-054/T-057 owner) when the code lands: code paths, the reel-mode RPC
(reliable, like `ServerSetReeling`), the OutOfLine reason vs the old `Spooled` outcome, test names, and the balance model
results. Design notes for the engineers:
- T-054, T-053 and T-057 all change `FLureFight::Step` (line and tension branches, outcome checks). Build them in that
  order in one lane: 3.1 before 3.2, and 3.3 last because it retunes on top of both.
- Keep the neutral-rod rule (reel-fight-rules.md): with Let out never held, a spool never emptied and LandStamina = 1
  (so any fish lands), the fight equals the Sprint 1 fight bit for bit.
- T-055 is in the fishing component's Waiting state (bite clock, bobber rest point), not the fight sim. T-056 is
  the cosmetic bobber pose.

## 8. Open questions for Jimmy (through the lead)
Lead, 2026-09-24: 2, 3 and 4 are decided as recommended, so they won't go to Jimmy. Only 1 is still open.
1. What land time do you want for a well-played beginner Bonefish? Recommendation: 15-20 s (B1). The lead has already
   asked, and the answer replaces FightTargetMin/Max.
2. Is holding right click (left trigger) to let line out OK? Recommendation: yes. Both inputs are free, and they mirror
   hold-left-click-to-reel, so the three choices need only two fingers.
3. A full-length cast takes about 5 s to reel back at the normal reel speed (about 3.4 s on the fastest). Recommendation:
   keep that. It reads as real reeling without being tedious, and the wheel makes it faster.
4. A fish that still has fight left makes a last run when you bring it close, so you only land it once it's tired.
   Recommendation: yes. This is what makes the fight length depend on how you manage tension, and it gives every fight
   a dramatic end.

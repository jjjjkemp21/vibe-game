# Day/night and living water (T-067; builds T-068 = T-013 and T-069): design spec
Status: review (design, 2026-09-24). Owner: design. Engineering owns section 7.

Jimmy's words (A2 playtest, 2026-09-24, `Playtest/2026-09-24 A2 build playtest feedback.txt`):
- "Add the day/night cycle"
- "make a water animation. Near or touching the coast there should be waves, with them crashing on the beach. The water
  will run up the sand, then pull back as the next wave rolls in like real life. Also for water in the ocean or behind the
  wave line, it should have animations as well. If a bobber or line is traveling thru the water, it should create ripple
  effects. Not particle, but water itself should be reactive"

GAME_DESIGN.md already asks for a compressed day (about 20 real minutes) in which every time of day brings its own fish
and dangers: dawn-only and night-only fish, alert creatures and rolling fog at night, calmer water at noon. T-013's
acceptance: about 20 real minutes per day from data; dawn, day, dusk and night lighting presets in the palette; bite
tables react to the time; screenshots of all 4 times checked.

## 1. Goal
The island breathes. The sun crosses the sky and the day turns to dusk and night, which changes what bites and how the
island feels (pillars 2, 3, 6). The sea moves: swells offshore and waves that break and run up the beach. The water
answers the player: a bobber, a line or a fish leaves real ripples in the surface, so reading the water becomes part of
fishing (pillars 1, 6). Everything is data: a new region's sky, fog and waves are new rows (pillar 7).

## 2. Player experience
1. You arrive in the morning light. The sea rolls in gentle, faceted swells, and the bobber rides them.
2. At the beach, waves build as they come in, crest white, and break. A thin sheet of water runs up the sand, leaves a
   dark wet band, and slides back as the next wave arrives, about every 7 s.
3. You cast. The bobber lands with a ring of ripples. At a nibble a small ring spreads, and at the bite a bigger one.
   When you reel it back (fight-v2.md 3.4) it leaves a V of ripples behind it. During a fight the line cuts the surface
   and the fish's thrashing churns it.
4. Around noon the water is calmest. By dusk the sky and water turn orange. At night it is dark: the moon gives a cool
   light, the fog rolls in, the dock lantern, the shop and the Beacon glow, and night fish bite.
5. With friends: everyone sees the same time of day, the same swell at the same place, and each other's ripples.

## 3. Rules

### 3.1 The clock (T-068)
1. The server owns the time of day, `Hour` in [0, 24). It moves at `24 / (DayLengthMinutes x 60)` game hours per real
   second (data), so one full day takes DayLengthMinutes. A session starts at `StartHour`. Saving the time is out of
   scope; T-015 decides.
2. Clients never run their own clock. The server replicates the hour at a reference server time plus the rate, and each
   client computes the same hour from the synced server world time (no per-tick replication).
3. **Phases** come from data. `DT_DayCycle` gives the start hour of Dawn, Day, Dusk and Night. Gameplay reads the hour,
   or the phase where a system needs a name. The bite context uses the clock's hour: it replaces the fixed
   `DefaultTimeOfDayHours` (16) in fishing-water-rules.md, and species time windows (DT_FishSpecies) work unchanged.
4. **Real time per phase is data.** Each phase has `RealMinutes`, and the clock runs each phase at its own rate, so the
   day lasts the sum of the four (20 min): Dawn 2, Day 9, Dusk 2, Night 7. Nights last long enough to fish the night
   fish and short enough that a 15-25 min slice session sees a whole cycle. Rule 1's rate is the average.
5. The HUD shows a plain-text clock, for example `06:40 Dawn` (placeholder UI).
6. Server-only dev commands for playtests and screenshots: `Lure.Time.Set <hour>`, `Lure.Time.Scale <x>`
   (0 = frozen) and `Lure.Time.Phase <Dawn|Day|Dusk|Night>`. Every client follows them.

### 3.2 Look per time of day (T-068)
7. Each (region, phase) is a row in `DT_TimeOfDay`: sun pitch, sun colour and intensity, moon (a second, dim directional
   light) colour and intensity, the SkyAtmosphere luminance factors, sky light intensity, height fog (colour, density,
   falloff, start distance), the fixed exposure (EV100), the water colours (shallow, deep, foam), `WaveScale`, and the
   night lights' intensity. It is the same set of values as the layout's `time_of_day_presets` today (see migration,
   rule 9).
8. Between phase anchors every value blends linearly, and colours blend in linear colour. The sun's yaw turns steadily
   with the hour: it rises in the east and sets in the west (level +X east). Exposure stays fixed per phase and is
   blended, never auto exposure (ART_STYLE.md).
9. **Migration:** the layout keeps `time_of_day` as the region and phase shown in the build preview. The preset values
   move from `L_PalmKey.json` to `data/tables/DT_TimeOfDay.json` (rows `Tropical_Dawn`, `Tropical_Day`, `Tropical_Dusk`,
   `Tropical_Night`), and the builder reads them from there. There is one source of truth.
10. **Night lights:** any light the builder places with the tag `Lure.NightLight` follows the row's `NightLightIntensity`
    share: it is off by day and on at dusk and night. The dock lantern, the shop, the Beacon bands and the cave fill get
    the tag. Wayfinding (docs/teams/design.md §4): a landmark is lit and visible from every spawn at night.
11. Palette targets (ART_STYLE.md): the day sky samples within about 10 % of `#8FD3F0`, the dusk sky near the horizon of
    `#FF9A5A`, the night sky of `#1B2440`. Night stays dark, lit only by the lamps, the Beacon and the moon.

### 3.3 Waves: open sea and shore (T-069)
12. **One wave function, shared by the looks and the cosmetic code.** The surface height is `H(x, y, t) = WaterZ +
    WaveScale x (Ocean(x, y, t) x OceanMask + Shore(x, y, t))`, computed identically in C++ (a pure function) and in the
    water material. `t` is the synced server world time, so every machine shows the same crest at the same place.
13. **Gameplay water stays flat.** Swimming, cast landing, bite depth, the water areas and every rule read `WaterZ` as
    today. Waves are cosmetic. Only the bobber's float, the floating line points (fishing-line.md "Water") and the fish
    shown at the surface ride `H`. Caps: `|H - WaterZ| <= MaxWaveHeight`.
14. **Ocean** = the sum of the row's `OceanWaves` (2-4 directional Gerstner waves: Height, Length, DirectionDeg, Speed,
    Steepness). The wave lengths are long and the heights small, for gentle faceted swells. `OceanMask` fades the
    waves out inside the shore band, where the shore waves take over.
15. **Shore** uses the level's **shore field**, a baked texture over the level:
    - R = distance to the waterline (cm, up to ShoreBandWidth);
    - G and B = the direction toward the shore;
    - A = beach (1) or cliff/dock (0).
    It is baked headless from the layout JSON (the same terrain heights as the previews) and rebuilt with the level.
16. A breaking wave travels toward the shore along the field's direction at `ShoreWaveSpeed`, one every
    `ShoreWavePeriod` s (+-`ShoreWaveJitter` per stretch of beach, so waves don't arrive in one straight line). It
    grows as it nears the beach (up to `BreakerHeight`), shows a hard-edged white crest from `BreakDistance`, and breaks
    there, leaving foam.
17. **Swash:** after the break, the water surface near the waterline rises by `SwashHeight` and falls back over the
    rest of the period. On a beach that makes the water edge run up the sand and pull back (about 2 m on the
    ~6 deg Palm Key beach). A foam line follows the moving edge, and the sand material darkens a wet band up to the
    last run-up, which dries over `WetSandDryTime` s. At cliffs, rocks and dock posts (A = 0) the wave only breaks as
    foam, with no run-up.
18. Stylized, never photoreal: flat-shaded facets (face normals), foam as hard-edged bands in the palette's white
    (`#F5F1E6`), colours from DT_TimeOfDay. No photo textures (ART_STYLE.md).
19. `WaveScale` comes from the time-of-day row (calmer at noon, livelier at night). A future storm is just a higher
    WaveScale.

### 3.4 Reactive ripples (T-069)
20. **The water surface itself ripples; no particles.** A height-field ripple simulation (a wave equation on render
    targets) runs on each machine in a square of `RippleAreaSize` centred on the local camera. The square is snapped to
    whole texels, so the ripples never swim. The water material reads it for its normals, a small height offset near
    the camera, and a thin ring highlight.
21. **Sources** (each machine, from replicated state; all players, not only your own). Every source kind is a data row
    with a strength, a radius and, for moving sources, a minimum speed:
    - a moving bobber (the retrieve, the fight: a V wake behind it);
    - the cast landing (a splash ring);
    - the nibble tip and the bite dip (rings: the bite ring is a readable tell);
    - line points on the surface that are moving;
    - a hooked fish at the surface (thrash);
    - a swimmer;
    - a fish or item dropped into water;
    - the bobber popping up after a fight dip (fight-v2.md rule 26);
    - hot spots (optional: a periodic ring gives "rippling water", fishing-water-rules.md).
    At most `RippleMaxSources` a step, nearest first.
22. Ripples spread at `RippleSpeed` and fade out over `RippleFadeTime`. They are stylized: the shading steps in
    `RippleBands` bands, so a ring reads as crisp low-poly rings, not noisy realism. They add to the waves, and the
    swash and shore foam do not erase them.
23. Ripples are cosmetic. They never change noise, bites or any rule, and they are never replicated: each machine
    simulates its own from the sources it already has.

### 3.5 Recommended approach (research, UE 5.8 as installed)
What UE 5.8 ships (checked in `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins`):

| Option | What it gives | Against our look and pipeline |
|---|---|---|
| **Water plugin** (Experimental, v0.1) | Ocean, lake and river water bodies on splines, water zones with a water-info texture (`WaterTerrainComponent` for non-landscape ground), a quadtree water mesh, Gerstner waves with shore *dampening*, buoyancy | Experimental. Its waves only fade near shore, with no break and no run-up. Its material is photoreal single-layer water, so we would restyle it anyway. It replaces M_LevelWater (the T-029 clear water) and would need the swim volume (T-026) and the fishing water rules (T-027) reworked. Adds the editor-heavy spline setup |
| **WaterAdvanced** (Experimental) + **NiagaraFluids** (Beta) | Shallow-water fluid sims (`Grid2D_SW_WaterBody`, river whitewater), collisions through Niagara data channels, an FFT ocean patch | Real interactive water, but a GPU fluid sim tuned for realism. It needs the Water plugin's bodies. Heavy, experimental, and hard to test headless |
| **Particles / decals only** | Cheap splashes | Rejected: Jimmy said "not particle" |
| **Our own (recommended)** | M_LevelWater extended: a grid water mesh with the shared wave function (rules 12-19), a baked shore field, and a render-target ripple sim (rules 20-23), all built by the level builder and driven from data | Full palette and facet control, no experimental plugin, fits the builder-built material pipeline and the layout JSON. Gameplay water stays flat and deterministic. We maintain it ourselves, and the run-up is a shaped illusion, not a fluid |

**Recommendation: our own approach.** It is the only option that meets all three of Jimmy's asks (waves that break,
water that runs up the sand, a reactive surface) in our stylized look, without an experimental plugin. SkyAtmosphere,
the directional sun, SkyLight real-time capture and height fog are already in L_PalmKey and become the day/night rig.
The SunPosition plugin (real latitude and date) isn't needed: our sun follows the data clock.

**Cost** (estimates; the lead confirms the schedule):
- GPU on the dev PC at 1080p: ripple sim + water shading <= 0.5 ms. A 512^2 sim over 40 m is ~8 cm per texel; three
  render targets ping-pong at 60 Hz.
- Water grid <= 60k triangles: 2 m spacing near the island, coarse far away.
- Game thread: the day/night rig and the wave constants <= 0.1 ms. The SkyLight recapture is time-sliced.
- Work: 2 engineering tasks each for T-068 and T-069, 2 level-designer tasks, 1 small art mesh task, and 2 editor
  bookings (build materials and the level; screenshots). See section 7.

## 4. Tuning data
Start values with reasons. **NEW** tables are JSON sources in `data/tables/`, and a region is a new row.

| Table | Row | Column | Start | Unit | Why |
|---|---|---|---|---|---|
| DT_DayCycle **NEW** | Default | DayLengthMinutes | 20 | min | GAME_DESIGN.md: about 20 real minutes per day |
| DT_DayCycle | Default | StartHour | 8.0 | h | start in the morning light, so the first catch comes by day |
| DT_DayCycle | Default | DawnStart / DayStart / DuskStart / NightStart | 5 / 7 / 17 / 19 | h | the tropics, even day and night |
| DT_DayCycle | Default | DawnMinutes / DayMinutes / DuskMinutes / NightMinutes | 2 / 9 / 2 / 7 | min | 20 in total; nights long enough for night fish |
| DT_TimeOfDay **NEW** | Tropical_Day | from `tropical_day` in L_PalmKey.json | EV 1.0, fog #8FD3F0 0.05 | - | the approved day look, moved as is |
| DT_TimeOfDay | Tropical_Dusk / Tropical_Night | from the layout proposals | EV 0 / -3, fog #FF9A5A / #1B2440 | - | the T-013 proposals already in the layout |
| DT_TimeOfDay | Tropical_Dawn | new | EV 0.5, fog #F6C7A0, sun 8 deg | - | a soft peach between night and day (level-designer tunes it from previews) |
| DT_TimeOfDay | every row | WaveScale | Day 0.7 (noon calm), Dawn 0.9, Dusk 1.0, Night 1.2 | x | GAME_DESIGN.md: calmer water at noon |
| DT_TimeOfDay | every row | NightLightIntensity | 0 / 0.3 / 1 / 1 (Day / Dawn / Dusk / Night) | share | lamps on from dusk |
| DT_Water **NEW** | Tropical | OceanWaves | (12 cm, 1800 cm, 20 deg, 250 cm/s, 0.3), (7, 900, 65, 180, 0.3), (4, 450, -25, 120, 0.2) | list | gentle, long faceted swells; the bobber's bob stays readable |
| DT_Water | Tropical | MaxWaveHeight | 35 | cm | a cap on any cosmetic offset (shore included) |
| DT_Water | Tropical | ShoreBandWidth | 2500 | cm | 25 m of approach: the wave builds visibly from the dock |
| DT_Water | Tropical | ShoreWavePeriod / ShoreWaveJitter | 7 / 1.5 | s | a calm tropical beach rhythm |
| DT_Water | Tropical | ShoreWaveSpeed | 300 | cm/s | a wave crosses the band in ~8 s |
| DT_Water | Tropical | BreakerHeight / BreakDistance | 25 / 400 | cm | small, readable breakers 4 m from the waterline |
| DT_Water | Tropical | SwashHeight | 20 | cm | ~2 m run-up on the ~6 deg beach |
| DT_Water | Tropical | WetSandDryTime | 6 | s | the wet band fades just before the next wave |
| DT_Water | Tropical | RippleAreaSize / RippleResolution | 4000 / 512 | cm / texels | covers a full 18 m cast; ~8 cm texels |
| DT_Water | Tropical | RippleSpeed / RippleFadeTime / RippleBands | 60 / 2.0 / 3 | cm/s / s / count | small, slow rings that clear in 2 s |
| DT_Water | Tropical | RippleSimRate / RippleMaxSources | 60 / 32 | Hz / count | enough for 4 players' bobbers, lines and fish |
| DT_WaterRipple **NEW** | BobberMove, CastLand, Nibble, Bite, LineMove, FishThrash, Swimmer, Drop, BobberPop, HotSpot | Strength, Radius, MinSpeed, Interval | per row (Bite 1.0/30 cm, Nibble 0.4/20, BobberMove 0.6/20 min 20 cm/s ...) | - | the bite ring is the strongest tell; each source is a row |

## 5. Acceptance criteria
Clock (T-068)
- AC1 [auto] With DayLengthMinutes 20, the clock advances 24 h in 1200 s (+-1 s) of real time, and each phase lasts its
  RealMinutes (+-1 s).
- AC2 [auto] A new DT_DayCycle value (DayLengthMinutes 10) halves the day with no code change.
- AC3 [auto] The bite context's hour equals the clock's hour. A night-only test species bites only between NightStart and
  DawnStart.
- AC4 [auto] `Lure.Time.Set 21` on the server puts every client at 21:00 (+-1 game-minute) within one replication update.
  A client can't set the time.
- AC5 [auto] Multiplayer: a client that joins mid-day shows the server's hour (+-1 game-minute). Two clients never differ
  by more than 1 game-minute.
- AC6 [auto] The HUD text shows `HH:MM <Phase>` that matches the clock.

Look (T-068)
- AC7 [auto] The blend between two DT_TimeOfDay rows at the midpoint is the linear mean (colours in linear space) of
  each value, and exposure is never auto.
- AC8 [auto] Lights tagged `Lure.NightLight` have intensity 0 at 12:00 and full intensity at 22:00.
- AC9 [play] Screenshots from the dock at 06:00, 12:00, 18:00 and 23:00 (`Lure.Time.Set`): the sky samples are within
  about 10 % of the palette targets (rule 11), the night is dark with the lantern, the shop and the Beacon lit, and
  `designer-low` APPROVES the four shots against ART_STYLE.md.

Waves (T-069)
- AC10 [auto] The pure wave function is deterministic. `|H - WaterZ| <= MaxWaveHeight` over a 1000-point x 600 s sweep.
  H at the waterline oscillates with ShoreWavePeriod (+-ShoreWaveJitter).
- AC11 [auto] Gameplay reads flat water: with waves on, the cast landing, swim entry, bite depth and the water areas give
  the same results as with WaveScale 0.
- AC12 [auto] The shore field bake from L_PalmKey.json is reproducible (the same file gives the same PNG, byte for byte).
  Every beach cell within 1 m of the waterline has A = 1, and the direction points toward land.
- AC13 [play] A 20 s capture (screenshots every 1 s) from the beach: at least 2 waves crest white and break, and the
  water edge visibly runs up the sand (>= 1 m) and pulls back. A wet band shows and fades.
- AC14 [play] Offshore, from the dock, the sea shows moving faceted swells. The bobber rides them, with no visible gap
  (<= 3 cm) between the bobber and the surface.
- AC15 [auto] Multiplayer: the wave height at a point computed on a client and on the server, from the synced time,
  differs by <= 1 cm.

Ripples (T-069)
- AC16 [auto] Each DT_WaterRipple source kind injects in the sim when its event happens (a test hook counts the
  injections by kind). There are no particle systems in the ripple path.
- AC17 [auto] One impulse spreads at RippleSpeed (+-15 %) and falls below 5 % of its peak within RippleFadeTime.
- AC18 [play] Screenshots: a V wake behind a retrieved bobber, a ring at the bite, churned water at a hooked fish, and
  rings around a swimmer.
- AC19 [play] Multiplayer: in 2-player PIE, player 2's screenshot shows the ripples behind player 1's retrieved bobber.

Budget
- AC20 [play] With ripples active and 4 bobbers in view, `stat GPU` shows the water + ripple sim at <= 0.5 ms at 1080p
  on the dev PC. The water grid is <= 60k triangles.

## 6. Out of scope
- Weather, storms and rain (WaveScale leaves room). Tides. Saving the time of day (T-015 decides).
- Underwater rendering and caustics (diving is out of scope).
- Boat wakes (the boat comes later; it becomes a DT_WaterRipple row plus wake noise).
- Creature behaviour by time of day (the shark and shadow tasks read the phase later). Sleeping or skipping time.
- The Water, WaterAdvanced and NiagaraFluids plugins (not adopted; see 3.5).

## 7. Contract (engineering) and task split
Engineering writes the contract (code paths, tests, networking) when the code lands. The design split (board
S5 #52; levels and dependencies are set there):

| Task | Level | What | Needs |
|---|---|---|---|
| T-068a | eng mid | The clock: a server-owned time (GameState), phases, DT_DayCycle, the bite context hour, the HUD clock text, `Lure.Time.*`, tests AC1-6 | - |
| T-068b | eng mid | The sky rig actor: applies the blended DT_TimeOfDay row to the sun, moon, SkyAtmosphere, SkyLight, fog, exposure, night lights and the water colours (MPC_Water); tests AC7-8 | T-068a |
| T-068c | design, level-designer mid | Migrate the presets to DT_TimeOfDay.json (4 Tropical rows, dawn tuned from previews), the builder places the rig and tags night lights, the plan updated; then an editor booking for the rebuild + AC9 shots | T-068b |
| T-069a | eng senior | The wave function (C++ pure + the HLSL snippet, one formula), DT_Water, MPC_Water feeding with the synced time, the bobber, floating line and surface fish ride H; tests AC10-11, AC15 | - |
| T-069b | eng senior | The ripple sim: render-target subsystem centred on the camera, sources from DT_WaterRipple and replicated state, the sim and splat materials created by a Python function in pipeline_unreal.py; tests AC16-17 | T-069a (MPC names) |
| T-069c | design, level-designer senior | Water graph v5 in build_level.py (grid mesh, waves, facets, shore foam, swash, wet sand, ripple normals) + the headless shore-field bake from the layout (AC12); editor booking for the rebuild + AC13, AC14, AC18-20 shots | T-069a, T-069b, T-069d |
| T-069d | art, model-artist junior | SM_WaterGrid tiles (flat technical grid, 2 m near and a coarse far ring, <= 60k triangles total, pivot at the center) | - |

Shared interface (the C++ and builder tasks agree on it first; engineering confirms it in this section): the material
parameter collection `MPC_Water` (WaterZ, WaveTime, WaveScale, the ocean wave params, the shore params, the ripple area
origin and size, the water colours) and the render targets `RT_WaterRipple_*`.

## 8. Open questions for Jimmy (through the lead)
1. Day length split: 20 min = dawn 2, day 9, dusk 2, night 7. Recommendation: yes. A 15-25 min session sees a full
   cycle, and night is long enough to fish the night-only fish.
2. Start of a session: morning (08:00) every time, until saves decide. Recommendation: yes. The first catch comes by
   day, and the night comes about 10 minutes in.

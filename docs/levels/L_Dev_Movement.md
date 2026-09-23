# L_Dev_Movement: movement test course (T-004 A28 / T-005)

Owner: level-designer. Status: v1.1 (2026-09-23): v1 built in the editor (7ca48e4); v1.1 adds the 50 cm gap and the
180 cm pillar from the T-004 playtester checklist, moves the labels off the player's view and fixes the exposure;
previews checked, rebuild pending.
Source of truth: `data/levels/L_Dev_Movement.json`; build with `Content/Python/levels/build_level.py` into
`/Game/Maps/Dev/L_Dev_Movement`; preview with `art/recipes/preview_level_layout.py`.

## Intent
A flat, labelled course where the playtester and QA can check every movement rule from docs/specs/movement-rules.md in
under a minute per station. Every test sits exactly on or next to a threshold from DT_Movement, so a wrong value in data
or code shows up as a pass or fail you can see. The map has no World Settings game-mode override (the project default
ALureGameMode runs). Labels are visible in game.

## Metrics (DT_Movement, lane eng1 0cac885; clearance = capsule height + 2.4 cm floor gap)
Stand 182.4, crouch 112.4, prone 54.4 cm clearance; eye 165 / 95 / 35; step 45; jump apex 90 cm standing (74 crouched,
99 sprinting) at gravity 980; walkable 44.8 deg. If the data changes, edit the test heights in the JSON (they are
written as literal numbers, with this derivation in the notes).

## Stations (teleport markers `tp_T0`..`tp_T8`, TargetPoints tagged `Lure.Teleport`, all facing into the test)
The course runs west to east along +Y, 8 m apart. You face north (+X) into each test, except T8, which faces east.

| Station | Where (teleport) | Test | Expected |
|---|---|---|---|
| T0 Start + speed lane | (0, 0) | 20 m lane with a line every 1 m (red every 5 m); 1 m crate at (3, -3) m and a free-standing **180 cm pillar** (80 x 80 cm) at (3, -6) m for scale and peeking around cover; PlayerStart at (-200, 0) | Walk 20 m in 5.7 s, sprint 3.3 s, crouch 11.1 s, prone 22.2 s; the pillar looks a little taller than your eye (165) and hides you standing |
| T1 Crawl gaps | (0, 800) | four 3 m tunnels, 1.4 m wide, clearance **50** (red, west) / **59 / 60 / 61 cm** | Prone passes 59/60/61 and is blocked at 50 (under prone's 54.4); crouch and stand blocked; standing up inside is blocked and queued, then completes on exit |
| T2 Stand ceilings | (0, 1700) | 4 m bays with ceilings at **181.4** (red, 1 cm under stand) and **185.4** (green, 3 cm over) | Can't walk into 181.4 standing (crouch in, stand-up stays queued until you leave); walk freely under 185.4 |
| T3 Crouch ceilings | (0, 2500) | bays at **111.4** (red, 1 cm under crouch) and **115.4** (green, 3 cm over) | 111.4 needs prone; 115.4 fits crouch; uncrouch under both is blocked |
| T4 Dock edge | (100, 3300) | 7 m dock over a 12 x 8 m pool; deck at 0, water at **-60** (dock 60 cm above the water), pool floor -250, 23 deg exit ramp at the north-east corner | Walk and crouch off the edge (allowed); prone at the edge shows the water (prone fishing); climb out by the ramp |
| T5 Stairs + ledge | (0, 4100) | 10 steps of **18 cm** (tread 30) to a 180 cm platform | Walk up without jumping; walk off the platform crouched (allowed) |
| T6 Ramps | (0, 4900) | 15 deg and 30 deg (green) and 46 deg (red, over 44.8) up to a 150 cm platform | Walk up 15 and 30; slide off 46 |
| T7 Ledges | (0, 6250) | blocks 45 (step), 50, 80, 90, 100, 120 cm | Walk onto 45; jump onto 50 and 80; 90 is marginal (apex 90); 100 and 120 fail |
| T8 Wall + corner | (700, 6900), facing east | 4 m wall and a corner | Prone against the wall and in the corner, then stand up: no penetration or pop (target-radius headroom check) |

All clearances were measured in the preview (a ray from the floor up, against colliding geometry): 50.0, 59.0, 60.0,
61.0, 181.4, 185.4, 111.4 and 115.4 cm, all PASS.

## Labels (in game)
Station names float at 320 cm over the front of each test (T4 over the dock, T8 on the wall facing the station);
values (gap and ceiling heights, ramp angles, ledge heights) at 240 cm over their element; the lane distances stand
beside the lane. Nothing sits at head height in front of a wall, so no label draws over the first-person arms, and
at T1 the four values sit over their own tunnels (the zone labels are off). `eye_t1_station.png` and
`eye_crawl_prone.png` show both.

## Light
Same fixed exposure (EV100 1.0 via an unbound PostProcessVolume), sky and sky light as L_PalmKey's day preset, so
stance and arms shots look the same on both maps; almost no fog.

## Playtester notes
- Teleport with `Lure.Teleport tp_T1` etc. (T-025), or read each marker's location (tags `Lure.Teleport`, `Teleport=tp_Tn`).
- Shots to take: T1 prone at the 60 cm mouth (rod tucked while moving, B-M2), T4 prone at the dock edge with the rod
  over the water (B-M3), and T2 and T3 crouched under the red ceilings.
- Falling into the pool: climb out by the ramp at the north-east corner.

## Performance
78 primitives, 2 props, 5 lights (incl. the PostProcessVolume), about 50 markers and labels. Trivial.

## Tune later
Add stations when new movement rules land: mantle, if the 90 cm apex feels low; swim, if T-017 adds swimming.

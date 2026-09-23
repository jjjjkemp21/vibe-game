# L_PalmKey: vertical slice island (T-005 greybox)

Owner: level-designer. Status: greybox v1.1 (2026-09-23). v1 built in the editor (7ca48e4); v1.1 = designer review
follow-up (Saved/AgentLogs/design/20260923-013000-palmkey-greybox.md: M1, M2, S1-S4), previews checked, rebuild pending.
Source of truth: `data/levels/L_PalmKey.json` (layout) -> `Content/Python/levels/build_level.py` (Unreal) and
`art/recipes/preview_level_layout.py` (Blender preview). Both expand the JSON through `Content/Python/levels/layout.py`,
so the preview shows exactly what the builder spawns. Edit the JSON, never the built level by hand.

## 1. Intent and player experience
A small, readable tropical key you can learn in one session. It feels welcoming by day (a busy dock, a sunny beach, a reef)
and uneasy at the edges. The interior has a grass hill, the palm grove and the Tall Palm. The dock is safe and lit at night.
Danger has a direction: the reef shark works the east reef edge, and the shadow watches from the deep water at the lagoon
mouth in the north-west. A secret is visible but out of reach: the ridge hides a cove you only get into by crawling. The
boat goal sits on the horizon from the dock: Gull Key islet, 210 m to the south-east.

Every area has one purpose, and every walk between points of interest takes under 30 s at walk speed. Something tall is
always in view: the Beacon (lit), the Tall Palm and the dock lantern.

## 2. Metrics used (from data)
DT_Movement.csv (lane eng1, 0cac885) and engine defaults. A copy lives in the layout's `metrics`.

| Item | Value | Design use |
|---|---|---|
| Walk / sprint / crouch / prone | 350 / 600 / 180 / 90 cm/s | Walk times below; prone crawl 6 m = 6.7 s |
| Eye stand / crouch / prone | 165 / 95 / 35 cm (from the feet) | Previews, cover rules |
| Capsule height stand / crouch / prone | 180 / 110 / 52 cm (radius 34 / 34 / 25) | |
| Clearance (height + 2.4 cm floor gap) | 182.4 / 112.4 / 54.4 cm | Crawl-only gap = 60 cm (between 54.4 and 112.4) |
| Step height | 45 cm | Every terrain step is 40 cm or less |
| Jump apex (420 cm/s, gravity 980) | stand 90 cm, crouch 74 cm (380), sprint 99 cm (440) | Walls meant to stop you are 120 cm or more; 80 cm low cover can be jumped |
| Walkable slope | 44.8 deg | Terrain 4-13 deg, spit flanks at most 23 deg, ramps 23 deg |
| Stairs | 18 cm or less per step, 30 cm tread | Point stairs 17.5 / 17.9 cm |
| Docks and jetties | 50-80 cm above the water | Dock 70, jetty 60, Point Ledge 70, Mouth Rocks 80 |

Note for design: with a 90 cm jump apex, you can't jump onto the 1 m crate (SM_GoldenCrate) from standing. That's worth a
feel check in Jimmy's first playtest. The dev map has 80/90/100 ledges to test it.

## 3. Layout at a glance (X north, Y east, 1 m = 100 cm)
The main island is about 80 x 75 m, plus the spit, the ridge and cove, and the headland. The playable area is about 140 x 170 m.

| Zone | Where | Purpose |
|---|---|---|
| The Dock | south, dock deck X -38..-62 m | Spawn and respawn, NPC shop and requests, first casts, boat mooring (T-018). Safe, lit at night |
| Palm Beach | south-east lobe | Easy shore casting under the Tall Palm |
| Palm Grove (hill) | centre, grass hill +4 m | Orientation: the Beacon and the Tall Palm are visible from the top |
| Reef Shallows | east, reef flat at -60 with coral heads just above the water | Safe reef fishing from the shore |
| Jetty | east, 43 m long, 3 m wide, end platform 6 x 7 m | Best reef fish, next to the shark |
| Rocky Point | north-east headland, top +3 m, Beacon 11 m | Landmark, view, stairs down to the Point Ledge (deep drop, dawn fish) |
| The Ridge | north, rock wall +6.5 m | Hides the cove and blocks the skyline, with the cave mouth and a bent palm |
| Crawl Cave | through the ridge, 14 m | Crouch 2 m, crawl 6 m (60 cm), crouch 2 m |
| Hidden Cove | north of the ridge, enclosed by rock arms | Secret beach, better rarity odds, the "find the cove" request |
| The Lagoon | west, shallow (-90) inside the spit | Calm fishing on the safe south shore |
| Mouth Rocks | 32 m rock arm on the lagoon's north side, +80 ledge with an 80 cm lip | Night-only fish; prone fishing behind cover while the shadow watches |
| The Spit | sand arc enclosing the lagoon (west) | Beached hull (cover), a lone palm |
| Gull Key | islet 210 m south-east of the dock | Boat goal (T-018), visible on the horizon from the dock |

## 4. Flow and beats (the main loop)
Times are straight path lengths along the route's waypoints (plus height change) at the data's speeds. `python
Content/Python/levels/layout.py data/levels/L_PalmKey.json` recomputes them.

| # | Leg | Beat | Distance | Time |
|---|---|---|---|---|
| 1 | Spawn -> NPC | Meet the dock NPC, take request 1 | 11 m | 3 s |
| 2 | NPC -> Dock End | First casts off the dock (safe, lit) | 39 m | 11 s |
| 3 | Dock End -> Palm Beach | Shore casting under the Tall Palm | 49 m | 14 s |
| 4 | Palm Beach -> Reef Flats | Reef fish from the shore (safe) | 23 m | 6 s |
| 5 | Reef Flats -> Jetty End | Better fish; the shark's water | 56 m | 16 s |
| 6 | Jetty End -> Point Ledge | Up to the Beacon, down to the ledge (dawn fish) | 102 m | 29 s |
| 7 | Point Ledge -> Cave mouth | Find the cave (request 3) | 91 m | 26 s |
| 8 | Cave -> Hidden Cove | Crouch 4 m + crawl 6 m | 24 m | 13 s |
| 9 | Cove -> Mouth Rocks | Back through the cave; night fishing on the arm | 65 m | 25 s |
| 10 | Mouth Rocks -> Lagoon Flats | The lagoon's safe side | 60 m | 17 s |
| 11 | Lagoon Flats -> NPC | Sell, upgrade, level up | 29 m | 8 s |
| (12) | NPC -> Boat mooring | Boat unlock (end of the slice) | 45 m | 13 s |

The loop is 547 m, about 2 min 50 s of pure walking. With 30 s fishing cycles, 3-5 casts per spot, the shop and two
time-of-day windows (dawn at the Point, night at the Mouth), that fills the 15-25 minute slice. Straight-line distance
from spawn: Dock End 28 m, Palm Beach 32 m, Lagoon Flats 35 m, Reef Flats 42 m, cave 61 m, Beacon 65 m, Jetty End 80 m.
Paths are 3 m or wider for 2-4 players: dock 4 m, jetty 3 m, arm 5 m, point stairs 4 m, ledge stairs 3 m. The only narrow
part is the deliberate crawl: 1.6 m wide, room for two prone players side by side.

## 5. Fishing spots
Every spot is a marker with a habitat tag, region `Region.Tropical.PalmKey`, a radius on the water and a cast-from point
(2.5 m radius, room for 2-4 players). `hours` means when the spot matters (for info only); bites are still decided by
each species' time windows. A teleport `tp_<spot>` is created automatically at each cast-from point.

| Spot | Habitat tag | Levels | When it matters | Danger | Species it should serve (proposal for T-009) |
|---|---|---|---|---|---|
| Dock End | Habitat.Shore | 1-2 | any (lit at night) | none | Bonefish, Sand Grunt |
| Palm Beach | Habitat.Shore | 1-2 | any | none | Bonefish, Sand Grunt |
| Reef Flats | Habitat.Reef | 2-3 | any; snapper 15-09 | none (shark can't enter -60) | Coral Snapper |
| Jetty End | Habitat.Reef.Edge | 3-5 | any; shark alert at dusk and night | shark | Coral Snapper, Blue Trevally |
| Point Ledge | Habitat.DeepDrop | 4-6 | dawn 05-07 | none | Sunrise Tarpon (dawn only), Blue Trevally |
| Lagoon Flats | Habitat.Lagoon | 1-3 | day | shadow only if very loud | Bonefish, Sand Grunt |
| Lagoon Mouth | Habitat.Lagoon.Mouth | 4-5 | night 20-05 | shadow | Moon Eel (night only) + lagoon fish |
| Hidden Cove | Habitat.Shore.Cove, luck +0.5 | 2-4 | any | none | Shore fish at better rarity odds |
| Gull Key Drop (boat) | Habitat.DeepDrop, Region.Tropical | 5-7 | any | none | Blue Trevally and bigger deep fish |

Six-species proposal (2 exist, 4 new, all data rows): Bonefish (L1, Shore+Lagoon, 05-19), Coral Snapper (L3, Reef,
15-09), Sand Grunt (L1, Shore+Lagoon, any time: gives beginners bites at night off the lit dock), Blue Trevally (L5,
DeepDrop+Reef.Edge, day), Sunrise Tarpon (L4, DeepDrop+Shore.Cove, 05-07, dawn only), Moon Eel (L4, Lagoon.Mouth,
20-05, night only).

**Hierarchical habitat tags (engineers: register them, no code needed).** The bite picker matches a spot's tag with its
parents (FishRoll `MatchesAnyOrEmpty` + `HasTag`). So a spot tagged `Habitat.Lagoon.Mouth` serves every `Habitat.Lagoon`
fish plus mouth-only fish. New tags for `Config/Tags/FishTags.ini`: `Habitat.Lagoon.Mouth`, `Habitat.Reef.Edge`,
`Habitat.Shore.Cove`. `Habitat.DeepDrop` and `Region.Tropical.PalmKey` already exist in lane eng2.
**Spot luck:** the Hidden Cove carries `luck: 0.5`. It should add to the rarity roll's Luck input (fish-system-rules
stage 3) when a bite comes from that spot (T-006).

## 6. Threats, cover and sight lines
### Reef shark (T-015)
- Patrol `shark_patrol`: a closed loop of 8 points at Z -80, 44 x 18 m, east of the jetty end, all in deep water. It
  never crosses the jetty and can't enter the -60 reef flat, so the Reef Flats spot is the safe reef choice.
- It passes 2 m off the jetty platform's east edge. That's where it can knock a player in (outer 6 m of the jetty) and
  steal fish at Jetty End. The threat zone `shark_zone` is 52 x 26 m.
- Quiet approach: walk, don't run, on the planks (wood should be a loud surface for T-014); crouch-walk the last 10 m.

### The shadow at the lagoon's edge (T-016)
- Stir zone `shadow_zone` (44 x 44 m around the mouth): very loud noise here wakes it.
- Search points (sight_cone markers; eye 120 cm above the water, half angle 25 deg; the sweep is where it turns its head):
  S1 north of the arm, at (20, -55) m, sweep 150-205 deg, range 35 m.
  S2 outside the mouth, at (18, -70) m, sweep 95-150 deg, range 40 m.
  Both look in from the deep north-west water. The lagoon's south shore (Lagoon Flats) is out of range: the safe side.
- **Cover rule (low cover, 80 cm lip on the arm):** prone behind it is hidden from both search points. A crouching player
  is seen from S2; at the tip's south edge they are hidden from S1 only because the 5 m arm also blocks the line.
  Standing players are seen. Prone is the only stance that is safe from both.
- **Prone fishing near danger:** at the arm tip, lie prone at the south edge. The lip covers the north and west (the
  shadow's side), and you cast south-west into the inner mouth, where the night-only fish comes in. The fishing
  direction and the threat direction differ on purpose: cover can't hide you from a direction you are looking into.
- Verified by the preview's ray tests (creature eye -> player eye point, against colliding geometry only):
  prone vs S1 hidden, prone vs S2 hidden, crouch vs S1 hidden, crouch vs S2 visible, stand vs S2 visible, and crouch behind
  boulder 1 on the south shore hidden. All PASS. The shot `eye_shadow_s2.png` shows it.
- Cover types placed (tags `Lure.Cover`, `Cover=Low|Mid|High`): Low (80 cm, hides prone) is the arm lip, the log and the
  beached hull. Mid (120-150 cm, hides crouch) is the lagoon boulders and the root rocks. High (200 cm or more, hides all)
  is the ridge and the arm-root rock.
- Quiet approach to the Mouth: walk from the grove down the north sand to the arm root (behind the root rocks), then
  crouch or prone along the lip side of the arm. The open approach is wading in the lagoon (loud, visible).

## 7. Crawl routes
| Route | Where | Sections | Checked |
|---|---|---|---|
| Crawl Cave (the only way into the Hidden Cove) | through the ridge at Y -15 m, X 28..42 m, floor +180 | mouth 2 m (300 cm clear), crouch 2 m (135 cm, 2.4 m wide), crawl 6 m (60 cm, 1.6 m wide), crouch 2 m (135), exit 2 m (300) | clearance ray tests 300 / 135 / 60 / 135, all PASS |

Readability (designer M1): the three low ceilings are `rock` (lighter) while walls and the mouth/exit frames stay
`rock_dark`, so each ceiling drop reads as a lighter band before you reach it and the portal stays dark from outside.
Three shadowless fill lights (`cave/fill_in` X 30.5 m, `cave/fill_mid` X 35 m, `cave/fill_out` X 39.5 m; color
#8FD3F0, radius 4-4.5 m) sit just under the ceilings. They use the soft falloff (no inverse-square hotspot): about
12 / 10 / 12 lux-equivalent at the light, 0.83-0.94 of that on the walls. Target on screen: walls in the
#2E3A40-#3E4A4F luminance range (the preview gives ~#1C4860: right value, bluer hue because of the sky-blue light),
ceilings a lighter band (~#4A89A0).

The cove is sealed by rock arms (+6 m) that run into deep water (seabed -800). The north mound was shrunk so you can't
wade round the arms, and the headland is walled off from it by the ridge (+6.2-6.5 m).

## 8. Landmarks and wayfinding
- The Beacon (Rocky Point, top +14 m, lit lantern and point light): visible from spawn over the grass hill (see
  `eye_spawn.png`), from the jetty and from the lagoon.
- The first frame (spawn yaw 23, level): Beacon just right of center, Tall Palm and the jetty over reef water on the
  right, the dark cave mouth at the left edge. The dock NPC and shop are 11 m to the left (95 deg), so they can't share
  the frame with the Beacon (121 deg apart). Moving the start doesn't help: south of the dock root the reef water drops
  behind the sand rise, east or north pushes the cave mouth out of frame. Yaw 20 -> 23 adds more jetty and reef water
  on the right while keeping the cave mouth in (designer S4, lead: keep Beacon-first).
- The Tall Palm (16 m, leaning south-east): marks Palm Beach and the way east.
- The dock lantern (on the dock head) and the dock root lamp: the night beacon home.
- The ridge skyline with the dark cave mouth and a bent palm next to it (`lm_cave`).
- The shop awning (accent red) at the dock root; Gull Key's two palms on the horizon from the dock head (`eye_dock_head.png`).

## 9. Respawn
Four PlayerStarts at the dock root (1.5 m apart, co-op 2-4), `PlayerStartTag = Respawn.Dock`, tags `Lure.Respawn`,
`Respawn=Dock`. Spawn and respawn are the same place, facing north-north-east, yaw 23 (Beacon ahead, Tall Palm and
jetty water right, shop 11 m left). No other respawn in the slice.

## 10. Water and hazards
The water planes never collide. Shallow shelves (-60 to -200, turquoise overlay discs) can be waded; the seabed
elsewhere is -800 with no way out. Until T-017 decides whether falling into the water means swimming or getting caught,
a player in deep water is stuck; the playtester uses teleports (`Lure.Teleport <id>`). Suggested T-017 rule: water
deeper than 1.5 m under the capsule counts as fallen in, so you respawn at the dock.

## 10b. Light, exposure and palette (designer S1-S3, ART_STYLE 2026-09-23)
The layout's `time_of_day` names the active preset in `time_of_day_presets` (T-013 will drive presets from data;
dusk and night hold proposals only). `tropical_day`:
- **Fixed exposure** `exposure_ev100` 1.0: the builder spawns one unbound PostProcessVolume with manual exposure, no
  physical camera, bias = -EV100 (exposure scale 0.5). With the 10 lux sun (x0.84 through the atmosphere at 50 deg)
  plus sky fill, sunlit horizontal surfaces land at about their palette hex after Unreal's filmic tonemapper, which
  keeps mid tones and compresses highlights (preview samples: sand #E0D2A2 for #F2D6A2, grass #468A3B for #3F7A42,
  rock #7C8189 for #6B7275). Night and caves are no longer brightened by auto exposure. +0.25 EV = darker.
- **Haze**: fog density 0.03 from 30 m (was 0.006 from 40 m), color #8FD3F0 as an on-screen target (the builder
  divides out the exposure and the tonemapper), SkyAtmosphere contribution 0. By the engine's fog formula
  (`levels.layout.fog_transmittance`): Gull Key at 210 m 22% fog (readable), sea at 400 m 41%, sky 6 deg up 60%,
  10 deg 42%, 20 deg 22%; the Beacon from spawn 4%.
- **Sky**: SkyAtmosphere luminance x1.8 / 2.1 / 2.4 lifts the steel-blue zenith (~#3E607C in the v1 PIE shot) toward
  #8FD3F0 (estimated ~#6095B9); the real-time sky light captures that too, so its intensity 0.45 keeps the shadow
  fill about where it was. First guesses, to check in the rebuild screenshot.
- **Palette**: `rock` #6B7275 (M2), `grass` #3F7A42 (S3, fronds keep `palm_green`), `water_deep` #0A5560 and both
  waters at roughness 0.3 (S2: less sky reflection, deep reads different from shallow).

## 11. Marker contract (for engineers)
Until gameplay classes exist, markers are TargetPoints / TriggerBoxes / PlayerStarts whose tags carry the data:
- `Lure.FishingSpot` + `Spot=`, `Habitat=`, `Region=`, `Radius=` (cm), `Hours=20-5`, `Levels=4-5`, `Luck=`, `Danger=`,
  `CastFrom=x,y,z`
- `Lure.PatrolPoint` + `Patrol=`, `Owner=reef_shark`, `Index=`, `Closed=`, `Speed=`
- `Lure.SightCone` + `Owner=lagoon_shadow`, `Yaw=`, `HalfAngle=`, `Range=`, `SweepMin=`, `SweepMax=` (actor at the eye point)
- `Lure.Zone` (TriggerBox, extent = size / 2) + `ZoneType=threat|crawl_gap|...`, `Owner=`
- `Lure.Teleport` + `Teleport=<id>` (for `Lure.Teleport`); `Lure.NPCSpot`, `Lure.BoatMooring`, `Lure.Landmark`,
  `Lure.Cover` + `Cover=Low|Mid|High`, `Lure.QuestTarget` + `Quest=find_hidden_cove` (the cove stash crate)
- every spawned actor: `LureLayout`, `LureLayout=L_PalmKey`, `LureId=<element id>` (the rebuild key)
When a class exists (e.g. ALureFishingSpot), map it in the JSON: `"marker_classes": {"fishing_spot": "/Script/VibeGame.LureFishingSpot"}`.
The builder then spawns that class; the tags stay.

## 12. Performance notes
About 470 actors (365 basic-shape primitives: 144 of them palm parts; 5 props; 8 lights; about 90 markers and labels).
Everything is static except 1 movable sun, a real-time sky light and 7 point lights with shadows off (4 lamps, 3 cave
fills), plus 1 unbound PostProcessVolume. That is cheap for
greybox. The water is 1 plane plus 7 thin discs. For T-020, replace palms with one mesh (ISM/HISM), merge the ridge and
point rocks into island module meshes, and give terrain real meshes. Keep total actors under about 300 after the art
pass. Performance budget capture: T-023.

## 13. Tune in playtests
- Walk times (legs 6-7 are about 26-29 s: if they feel long, move the point stairs or the cave closer to the grove)
- Spot radii and cast distances (inner mouth 6-11 m from the tip; Jetty End 10-18 m)
- Shadow: eye height 120, cones 25 deg, ranges 35-40 m, sweeps, stir-zone size
- Arm lip height 80 cm (crouch exposure), arm width 5 m
- Shark: loop distance to the jetty (2 m), speed 250 cm/s, which surfaces are loud
- Crawl length 6 m; cave width 1.6 m
- Dock 70 / jetty 60 cm above the water (prone view of the bobber)
- Islet distance 210 m (boat trip about 20 s at 10 m/s)
- Grass hill height (+4 m): it hides the lagoon from spawn on purpose. Is the reveal good or confusing?
- Exposure EV (1.0), fog density (0.03), sky luminance factor and sky light intensity: sample the zenith, sand and
  Gull Key in the rebuild screenshots
- Cave fill intensity (12 / 10 / 12) and color: if the walls read too teal, move the color toward #B8D8E6

## 14. Build and preview
- Preview (Blender, headless): `powershell -NoProfile -ExecutionPolicy Bypass -File tools/blender-run.ps1 -Recipe
  art/recipes/preview_level_layout.py`. It writes `Saved/AgentLogs/previews/levels/L_PalmKey/` (map_overview,
  map_north, eye_spawn, eye_cave_mouth, eye_cave_crouch, eye_dock_head, eye_mouth_prone, eye_shadow_s2, eye_cave_crawl,
  eye_jetty) and a contact sheet `Saved/AgentLogs/previews/levels/L_PalmKey.png`. It fails if any cover or clearance
  test fails. Eye shots use the "engine look" (Cycles, the preset's exposure, Unreal's fog formula and filmic
  tonemapper, the cave fill lights). It can't show the SkyAtmosphere (the sky is the palette gradient), Lumen or local
  exposure, so the in-engine zenith and shade values need the rebuild screenshot.
- Build (editor-operator, run_python):
  `import importlib, levels.layout, levels.build_level as bl; importlib.reload(levels.layout); importlib.reload(bl); result = bl.build("data/levels/L_PalmKey.json")`
  then `bl.frame_view("data/levels/L_PalmKey.json", "<view id>")` for screenshots matching the preview shots.

## 15. Open questions (for the lead / Jimmy)
- Water: swim or "caught" when you fall in? (GAME_DESIGN open question; decides the deep-water hazard.)
- Jump apex 90 cm: should you be able to hop onto a 1 m crate?
- Grass hill blocking the lagoon from spawn: is the reveal good, or should the lagoon be visible from spawn?

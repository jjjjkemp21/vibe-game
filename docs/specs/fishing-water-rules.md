# Fishing water rules (T-027: fish anywhere + hot spots), unreal-engineer decisions 2026-09-23

Jimmy's playtest (2026-09-23): "ALL bodies of water should have the ability to catch fish, depending on the body of water or
area it could catch different types. If I cast just slightly outside a fishing zone, it catches no fish, which is not clear
to a user. It is also not clear where fishing zones are. 'Bubbling' or 'rippling' water indicating higher value fish WITHIN
a valid fishing body of water."

Binding for `Source/VibeGame/Fishing/FishingWater*.{h,cpp}`, `LureWaterArea`, `LureHotSpot*`, `LureWaterSettings`, the
pre-bite part of `ULureFishingComponent` and the tests `Project.Fishing.Water.*`. It replaces the "Spots" section of
fishing-rules.md (the spot rule is gone). Every number is data: `ULureWaterSettings` (Config/DefaultGame.ini
`[/Script/VibeGame.LureWaterSettings]`), DT_HotSpot (`data/tables/DT_HotSpot.json`) and the level layouts
(`data/levels/*.json`). The lead may overrule any of these; a change is a data edit unless marked (code).

## 1. Summary
- Every body of water can be fished. Where the bobber lands on water, the **water area** under it gives the habitat, the
  region and some luck; the **depth** under the bobber can pick a different area (depth bands); water that no area covers
  is **default water** (`DefaultWaterHabitat`, Habitat.Shore). The species pick is unchanged: FFishRoll::PickSpecies with
  that habitat, region, the time, weather and bait.
- **Hot spots** (bubbling / rippling water) appear in valid water per DT_HotSpot, last a few minutes, wander slowly, and give
  a cast that lands in them better odds: more rarity luck, bigger fish, more value, sooner bites.
- The HUD (placeholder text) says which water the bobber is in, when it is in a hot spot, and why nothing bites if so.
  "Nothing is biting here" is gone.

## 2. Water areas (the habitat model)
- **Source of truth:** `"water_area"` markers in the level layout (schema in section 9). The level builder spawns one
  `ALureWaterArea` per marker (a plain level actor, not replicated: every machine loads it with the map).
- **Shapes** (2D, a column from the surface down; actor scale is ignored):
  - `circle`: center + radius;
  - `box`: center + size (x, y), turned by the marker's yaw;
  - `polygon`: 3+ world X/Y points in order (concave is fine; even-odd rule; it must not cross itself);
  - `everywhere`: all water (a level's default water; usually with a low priority and maybe a depth band).
- **Fields:** id, name (HUD), habitat (Habitat.*), region (Region.*, optional: else the settings' DefaultRegion), priority
  (int), luck (>= 0, added to the rarity luck), depth band [min, max) in cm (max 0 = no limit).
- **Which area wins** at a point: among areas with a valid habitat and a usable shape that contain the point and whose
  depth band contains the water depth there: the **highest priority**; on a tie the **smaller area**; then the **lower id**
  (FName::LexicalLess). An area with an unregistered habitat or no usable shape never wins (the layout validation reports
  it), so it can never create a dead zone.
- **Default water:** no area wins -> `ULureWaterSettings::DefaultWaterHabitat` (Habitat.Shore), the default region, luck 0.
  A level can override it with its own `everywhere` area(s).
- **Legacy fishing spots (migration):** a level with **no** water area at all reads its `fishing_spot` markers
  (Lure.FishingSpot + Key=Value tags, as before) as circle areas (habitat, region, luck, radius; priority
  `LegacySpotPriority` 0). As soon as a level has one water area, the markers are ignored for fishing (they stay as named
  casting places: teleports `tp_<spot>`, labels). This keeps the built L_PalmKey playable until the level-designer paints
  areas and the editor-operator rebuilds. Switch: `bLegacySpotsWhenNoAreas`. Deprecated: remove it when every layout has
  water areas.

## 3. Depth
- Depth = water surface height - the first solid ground straight below the bobber (`FLureFishingSpots::TraceCast`, so
  volumes, triggers and pawns are ignored), capped at `DepthProbe` (5000 cm; no ground within it counts as that deep).
- Depth selects areas (depth bands) and hot spot water (their MinDepth/MaxDepth).
- **Too shallow:** nothing bites where the depth is below `MinBiteDepth` (15 cm). The HUD says so at once ("Too shallow
  here: cast into deeper water."). This also covers flat floors at the fallback sea level in dev maps.

## 4. The bite (server)
When the bobber lands (`LandBobber`) and at every bite check (`TryBite`), `FLureWaterRules::DecideBite` runs in this order:
1. Not on water -> `NotWater` (the bobber lies on land; the HUD says "landed on land").
2. Depth < MinBiteDepth -> `TooShallow`.
3. **Habitat:** the water's own habitat if some species of it can bite at this hour, region and weather **with the right
   bait** (bait is ignored for this test). Otherwise it is a **data gap** (T-009 fills the species in): the bite uses the
   first habitat of `GapFallbackHabitats` (Habitat.Shore, Habitat.Reef) that has one, and a Warning is logged once per
   habitat and clock hour ("Data gap: no species of Habitat.DeepDrop can bite at 16:00 ... uses the fallback habitat
   Habitat.Shore"). If no fallback has one either -> `NoSpecies` (logged once; the HUD says "No fish live in this water
   right now." after NoBiteHintDelay). An empty GapFallbackHabitats list turns the safety net off.
   Why a fallback: Jimmy wants no dead water; the design says every water area has a species at every hour "once the fish
   batch fills in". Until then (DeepDrop has no species; the reef has none 09-15; the shore none 19-05) the fallback keeps
   every water fishable at every hour with the two starter species, and the validation test lists the gaps.
4. **Bait:** FFishRoll::PickSpecies with the real bait. No species -> `WrongBait` ("Nothing here takes your bait (Squid)
   right now. Try other water or bait.", after NoBiteHintDelay). Bait never triggers the gap fallback: it is the player's
   choice.
5. Otherwise a bite can come: the usual wait, nibbles and bite (fishing-rules.md), rolled by FFishRoll::PickSpecies + Roll.
- `NoSpecies` and `WrongBait` are checked again every BiteWaitMax seconds (the clock moves on); `TooShallow` and `NotWater`
  never change during a cast.
- **The roll context** (`MakeBiteContext`): habitat (step 3), region = the area's or DefaultRegion, luck = area luck + gear
  luck + hot spot luck (non-finite terms count 0; the roll clamps to [0, MaxLuck]), SizeBonus / ValueMultiplier from the
  hot spot, time, weather, bait, and the server seed. `GetLastRollContext()` shows what the last check or bite used.
- **Replicated:** `FLureFishingNetState.SpotId` = the area id (None = default water), `bNoFishHere`, and
  `FLureFishingNetState.Water` = { HotSpotType, NoBiteReason }. The server keeps the full `FLureWaterContext`
  (`GetWaterContext()`) and the hot spot bonus (`GetHotSpotBonus()`).

## 5. Hot spots
- **Types are rows of DT_HotSpot** (`FLureHotSpotRow`; row name = type id). Shipped: `Bubbles` (feeding fish: luck 1.5,
  size 0.15, value x1.25, bites 30 % sooner, any water >= 60 cm) and `Ripples` (big fish moving: size 0.35, luck 0.5,
  reef/lagoon/deep water >= 90 cm). PLACEHOLDER numbers for Jimmy's feel feedback. Columns:
  - Spawning: `SpawnInterval` (mean s between spawns per area while under the cap; 0 = never), `MaxPerArea`, `MinSpacing`
    (cm to every other hot spot), `Radius`, `LifetimeMin/Max` (s), `DriftSpeed` (cm/s), `DriftRange` (cm), `MinDepth`,
    `MaxDepth` (0 = none), `AllowedHabitats` (hierarchical; empty = any water).
  - Bonus: `LuckBonus` (added to the rarity luck), `SizeBonus` (0..1: the natural weight roll moves that share of the way to
    the species' WeightMax), `ValueMultiplier` (sell value), `BiteWaitScale` (< 1 = sooner bites, rebites too).
  - Look: `VisualColor` (placeholder tint), `VisualClass` (a ULureHotSpotVisualComponent Blueprint child; None = the C++
    placeholder), `HudText`, `DisplayName`.
  - The struct defaults are the Bubbles row: without the imported table the game uses them (one Warning).
- **Spawner:** a level gets hot spots only from its `"hot_spots"` layout marker (`ALureHotSpotSpawner`; test maps have none,
  so tests stay deterministic). Server only (a non-replicated level actor has authority on clients too, so it checks the
  net mode). Every `HotSpotCheckInterval` (1 s; the first check counts as `HotSpotPrewarmSeconds` = 45 s so a level starts
  with some): for each row (sorted) and each water area (sorted by id, the default water last, and only if some water is
  default): if the area's habitat is allowed and it has fewer than MaxPerArea of that row, spawn with chance
  `1 - exp(-seconds / SpawnInterval)`. A spawn tries `HotSpotSpawnTries` (12) random points: inside the area's outline, or
  within `OpenWaterSpawnRadius` (30 m) of a random player for unbounded water (default water, `everywhere` areas). A
  point is good when: it is fishable water (a water surface and no solid ground above it, docks included); its winning area
  is that area; the row allows its habitat and depth, and it is at least MinBiteDepth deep; it is MinSpacing from every live
  hot spot; and 8 points on the circle it may wander over (DriftRange + Radius / 2) are fishable water of an allowed habitat
  on the same water level. At most `MaxHotSpots` (16, or the marker's `max`) live at once. Seeded RNG (the marker's `seed`,
  0 = random).
- **Drift** (`FLureWaterRules::DriftOffset`): a smooth wander that starts at the anchor (where it was placed): two sine
  waves with a seeded frequency ratio (0.62-0.92), turned by a seeded angle; the distance from the anchor never exceeds
  DriftRange and the RMS speed equals DriftSpeed. A pure function of (range, speed, seed, age): every machine computes the
  same center from the synchronized server time.
- **Lifetime:** a random time in [LifetimeMin, LifetimeMax]; the server destroys it at its end time. The visual grows in and
  fades out over `HotSpotFadeSeconds` (1.5 s).
- **A cast that lands in one** (the bobber touches the water within Radius of its center at that moment; several: the one
  whose center is nearest relative to its radius) keeps that hot spot's bonus for the whole cast (every bite and rebite),
  even if the hot spot drifts away or ends. Decided on the server when the bobber lands.
- **Replication:** `ALureHotSpot` replicates one `FLureHotSpotState` (type, area, anchor quantized to 0.1 cm on the server
  too, radius, drift, seed, spawn and end time, tint, visual class); always relevant; no movement replication. The bonus is
  server-only. Clients show the HUD line from `NetState.Water.HotSpotType`.
- **Look (swappable):** `ULureHotSpotVisualComponent`. The C++ placeholder draws 2 rings of foam dots that spread from a
  quarter of the radius to the full radius every 2.4 s, sinking as they spread, plus 7 bubbles that rise and pop (engine
  basic shapes, tinted, no collision, no shadows). Real VFX: an editor-operator makes a thin Blueprint child (a Niagara
  system, `bDrawPlaceholder` false, asset references and defaults only) and names it in the row's `VisualClass`.

## 6. Roll inputs added to the fish core (fish-system-rules.md, "T-027 additions")
- `FFishRollContext::SizeBonus` (default 0): stage 2, natural rolls only: `Weight += (WeightMax - Weight) * clamp(SizeBonus,
  0, 1)`, before the difficulty-stat scaling (bigger fish fight harder). Skipped at 0, so every roll without a bonus is
  bit-identical to before. Ignored with a forced weight fraction. Non-finite: ignored with a Warning.
- `FFishRollContext::ValueMultiplier` (default 1): stage 6, multiplies the value with the rarity and modifier multipliers.
  Skipped at 1. Non-finite or <= 0: ignored with a Warning.
- Rarity is boosted through the existing Luck input; the one roll pipeline stays the only place a fish is made.

## 7. HUD (placeholder text, `ULureFishingComponent::GetStatusText`, while the bobber waits on water)
- `"<HudText>"` when in a hot spot, e.g. "Bubbling water: better fish here".
- `"Water: <area name>"`, or "Water: open water" for default water.
- The no-bite reason: TooShallow at once; NoSpecies and WrongBait after the profile's NoBiteHintDelay.

## 8. Settings (`ULureWaterSettings`, Project Settings > Game > Lure Water)
DefaultWaterHabitat (Habitat.Shore), GapFallbackHabitats (Habitat.Shore, Habitat.Reef), MinBiteDepth (15), DepthProbe
(5000), bLegacySpotsWhenNoAreas (true), LegacySpotPriority (0), HotSpotTable (/Game/Data/DT_HotSpot), HotSpotCheckInterval
(1), HotSpotPrewarmSeconds (45), MaxHotSpots (16), OpenWaterSpawnRadius (3000), HotSpotSpawnTries (12), HotSpotFadeSeconds
(1.5). `ULureFishingSettings::OffSpotHabitat` is removed (DefaultWaterHabitat replaces it).

## 9. Layout schema (for the level-designer)
Markers in `data/levels/<Level>.json` "markers"; the builder maps them to classes (defaults in build_level.py; override in
the layout's "marker_classes"). Validation: `python Content/Python/levels/layout.py data/levels/<Level>.json`.

```json
{"type": "water_area", "id": "reef_flats", "name": "Reef Flats", "habitat": "Habitat.Reef",
 "region": "Region.Tropical.PalmKey", "priority": 10, "luck": 0.0, "depth": [0, 0],
 "shape": "circle", "at": [-1500, 5400, 0], "radius": 900}
{"type": "water_area", "id": "lagoon", "name": "Lagoon", "habitat": "Habitat.Lagoon", "priority": 5,
 "shape": "polygon", "points": [[-2500, -3000], [1500, -3200], [1800, -6500], [-2800, -6200]]}
{"type": "water_area", "id": "dock_shelf", "name": "Dock Shelf", "habitat": "Habitat.Shore", "priority": 5,
 "shape": "box", "at": [-6800, 600, 0], "size": [2400, 1600], "yaw": 0}
{"type": "water_area", "id": "sea_shallows", "name": "Shallows", "habitat": "Habitat.Shore", "priority": -100,
 "shape": "everywhere", "depth": [0, 300]}
{"type": "water_area", "id": "sea_deep", "name": "Deep water", "habitat": "Habitat.DeepDrop", "priority": -100,
 "shape": "everywhere", "depth": [300, 0]}
{"type": "hot_spots", "id": "hot_spots", "at": [0, 0, 0], "types": ["Bubbles", "Ripples"], "max": 12, "seed": 0}
```
- `at` is the water surface point (z = the layout's water_z). Polygons without `at` get their centroid; `everywhere` areas
  get [0, 0, water_z]. `yaw` turns boxes (and polygons are given in world X/Y, so the builder stores them relative to the
  actor). `depth` defaults to [0, 0] (any depth). `luck` defaults to 0, `priority` to 0, `region` to the default region.
- Rules the validator checks: shape known; habitat `Habitat.*` and region `Region.*`; radius/size > 0; polygon >= 3
  points, no zero-length edge, not self-crossing; depth min >= 0 and max 0 or > min; luck >= 0; WARN when two areas with the
  same priority overlap (the smaller wins, but say it with priorities), when an area's center is not over a water volume,
  and when a fishing_spot's center lies in an area with a different habitat (the area decides now).
- Every habitat a level uses must have a species at every hour (bait ignored); the C++ test
  `Project.Fishing.Water.Data.HabitatsHaveSpeciesAllDay` lists the gaps (T-009) and fails only if some water would be
  dead at some hour even with the gap fallback.
- Painting advice: cover the shore shallows, reef, lagoon, lagoon mouth, cove and deep drops as the plan's section 5 says;
  give small special areas (the cove, the lagoon mouth) higher priorities than the big ones around them; end with one or two
  `everywhere` areas for the open sea (e.g. Shore shallower than 3 m, DeepDrop beyond, once T-009 adds deep fish; until
  then DeepDrop water uses the gap fallback). Keep the `fishing_spot` markers as named casting places (teleports).

## 10. Dev commands (non-Shipping; `Dev/LureWaterDevCommands.h`)
- `Lure.Water.Show [Seconds]`: draws every water area (outline colored by habitat, label with habitat and priority) and hot
  spot (current circle, wander circle) for Seconds (20) and lists them in the log (LogLureDev).
- `Lure.Water.Probe [Distance]`: area, habitat, depth and hot spot of the water Distance cm (1000) in front of the player.
- `Lure.HotSpot.Spawn [Type] [Distance] [Lifetime]`: server/standalone: a hot spot in front of the player (ignores the
  habitat and area rules, not the water check).
- `Lure.HotSpot.Clear`: removes every hot spot.
- In the editor, water areas draw their outline (an editor-only line component, hidden in game).

## 11. Tests (`Source/VibeGame/Tests/Fishing/FishingWater*Test.cpp`, `Project.Fishing.Water.*`)
Area shapes and the priority rule (pure), default water, depth bands, the bite decision and its reasons, the gap fallback
(and bait never triggering it), the size/value roll inputs (bit-identical without a bonus), hot spot bonuses reaching the
roll with a fixed seed, drift bounds and speed, spawner caps/lifetime/validity per data, replication (a real listen server
with a client), builder setters of ALureWaterArea, and data validation of DT_HotSpot and the layouts' water areas.

## 12. Open for later
- A hot spot could run out after N catches (`MaxCatches`), gear could add a size bonus, weather could spawn more hot spots:
  all would be new columns, not code paths.
- Far areas (e.g. the boat-only Gull Key drop) take part of MaxHotSpots. If that leaves the island short in playtests, add a
  "only within N m of a player" column or setting (data) rather than special cases.
- The Blender layout preview (art/recipes/preview_level_layout.py) should draw water areas and hot spot markers (level-designer).

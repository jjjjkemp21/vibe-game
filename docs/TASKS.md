# Tasks

Format: `- [ ] T-### Title (owner) - acceptance: ...`  Owners: lead, unreal-engineer, blender-artist, editor-operator, qa-tester.
Last processed playtest folder: (none)

## Now: foundation (after setup)
- [ ] T-001 Fill docs/GAME_DESIGN.md with Jimmy (lead) - acceptance: every section has an answer Jimmy confirmed; vertical slice defined. Status: draft v1 written from the interview, waiting for Jimmy's OK.
- [ ] T-002 Fill docs/ART_STYLE.md with Jimmy (lead + blender-artist) - acceptance: palette, budgets, 3+ reference images, art/lib/style.py created. Status: direction, palette and budgets drafted; still to do: our own mood-board renders in art/reference/ (no copied Dredge art) and art/lib/style.py (palette + material presets for recipes).

## Next: vertical slice "Palm Key" (Lure, tropical beginner zone, solo, multiplayer-ready code)
Goal: 15-25 minutes of polished solo play proving fishing + exploration + tension is fun (see "Vertical slice definition" in GAME_DESIGN.md).
All tuning (movement speeds, gear stats, fish, XP curve, noise radii, shark behavior, time of day) lives in DataTables with CSV/JSON sources in the repo. All gameplay code is server-authoritative and replicated, so co-op can follow the slice without rewrites.

### Milestone A: "I can move and fish off a dock" (first Jimmy playtest)
- [ ] T-003 Playtest feedback key F8 (unreal-engineer + editor-operator) - acceptance: spec in the playtest-feedback skill met (screenshot, pause, one-line note, Saved/Playtest/<timestamp>/ with screenshot.png + note.json with level, location, rotation, game time, avg FPS over 5 s, commit id; Esc cancels; compiled out of Shipping); automation test for note writing passes.
- [ ] T-004 First-person character (unreal-engineer + editor-operator) - acceptance: first-person camera with placeholder arms; walk, run, jump, crouch and prone with smooth camera height and capsule changes; prone blocks jumping and fits under a 60 cm gap; speeds and heights in DT_Movement; replicated; automation tests for stance heights and speeds; screenshot of each stance checked.
- [ ] T-005 Palm Key greybox (editor-operator + blender-artist) - acceptance: L_PalmKey in /Game/Maps with island, dock, beach, reef shallows, jetty, rocky point, a crawl-only cave to a hidden cove, a water plane and a respawn point at the dock; walkable end to end in PIE; overview screenshot checked.
- [ ] T-006 Casting, bobber, bite and hook (unreal-engineer) - acceptance: hold to charge and aim a cast; bobber floats and bobs; fishing spots decide which fish can bite; the bite is readable (bobber dips, sound, controller rumble); hook window timing in data; a miss loses the bite; automation tests for cast distance and hook window.
- [ ] T-007 Reel fight, line tension and gear (unreal-engineer) - acceptance: hold to reel; fish fight patterns (dart, dive, run) from data; tension meter; line snaps when tension stays above line strength; rod power, line strength and hook security from DT_Gear change the outcome in tests (an under-geared fish snaps weak line; the right gear lands it).
- [ ] T-008 Fish data + first 2 fish (unreal-engineer + blender-artist) - acceptance: DT_Fish (level, weight range, strength, fight pattern, habitat, time-of-day window, bait); 2 fish meshes within budget with a swim wiggle; landed fish shown in hand with name and weight; previews checked. **Jimmy playtest A.**

### Milestone B: "Fishing is a game" (progression)
- [ ] T-009 Remaining 4 fish (blender-artist + unreal-engineer) - acceptance: 6 species total across shore, reef and deep drop; at least one dawn-only and one night-only fish; each readable in silhouette; data rows complete.
- [ ] T-010 Cooler, selling, money, XP and levels (unreal-engineer) - acceptance: limited cooler slots; sell at the dock; XP curve and level-scaling rules in data (fish above your level escape easily); automation tests for XP and level-ups and for selling.
- [ ] T-011 HUD and fish journal (unreal-engineer + editor-operator) - acceptance: HUD shows tension, noise, cooler, money, level and clock; journal lists caught species with record sizes; readable at 1080p; screenshots checked.
- [ ] T-012 Dock NPC: shop and 3 requests (unreal-engineer + blender-artist) - acceptance: shop sells 2 rods, 2 lines and 2 hook/bait sets from data; request system is data-driven with 3 requests (catch X, deliver Y, find the hidden cove); rewards pay out; tests for request completion.
- [ ] T-013 Day/night cycle, sky and water (editor-operator + unreal-engineer) - acceptance: about 20 real minutes per day (from data); lighting presets for dawn, day, dusk and night in the palette; bite tables react to time of day; screenshots of all 4 times checked. **Jimmy playtest B.**

### Milestone C: "Something is listening" (tension)
- [ ] T-014 Noise system (unreal-engineer) - acceptance: noise from actions (run, jump, splash, later boat wake) plus microphone volume (Unreal audio capture) with a proximity radius that grows with loudness; mic on/off and sensitivity settings; noise meter on the HUD; automation tests with simulated inputs.
- [ ] T-015 Reef shark (unreal-engineer + blender-artist) - acceptance: patrols the reef; gets curious and approaches when noise reaches it; circles, can steal a hooked fish and can knock a player on the jetty into the water; shark mesh with swim animation; behavior values in data; bot test sees each state.
- [ ] T-016 The shadow at the lagoon's edge (unreal-engineer + blender-artist + editor-operator) - acceptance: stirs only when noise near the edge passes a high threshold; searches along sight lines; crouch or prone behind cover hides you; being seen means caught; a big silhouette, fog and a sound stinger sell it; tuned so a quiet player never triggers it by accident.
- [ ] T-017 Getting caught, respawn and repairs (unreal-engineer) - acceptance: caught means a fade to respawn at the region's respawn point, the cooler emptied, and gear wear that costs money to repair at the dock; levels, journal and money kept; tests for the loss rules. **Jimmy playtest C.**

### Milestone D: "Out on the water" (slice complete)
- [ ] T-018 Basic boat (unreal-engineer + blender-artist) - acceptance: unlocked at a player level (from data) via the dock NPC; drivable in first person; the wake adds noise with speed; docking; one second fishing islet reachable; boat mesh within budget; bot drives a waypoint route.
- [ ] T-019 Save/load, pause and settings menu (unreal-engineer) - acceptance: progress (level, money, gear, journal, requests, boat unlock) saves at the dock and on quit, and loads; pause menu with mic, sensitivity, mouse and volume settings; save round-trip automation test.
- [ ] T-020 Art pass (blender-artist + editor-operator) - acceptance: greybox replaced by recipe-built island modules, dock, props and palms in the ART_STYLE palette and budgets; orientation rule recorded in ART_STYLE.md; before/after screenshots checked.
- [ ] T-021 Audio pass (lead + editor-operator) - acceptance: ambience per time of day, reel, splash, bite and danger stingers, all license-clean (CC0 or made by us) and listed with sources in docs/.
- [ ] T-022 Bot playthrough functional test (unreal-engineer + qa-tester) - acceptance: headless run walks waypoints on L_PalmKey, casts at a spot, lands a fish, sells it, and fails on stuck or unreachable objectives.
- [ ] T-023 Performance capture script + budget (unreal-engineer + qa-tester) - acceptance: one command records frame timings on L_PalmKey; budget (e.g. 60 fps at 1080p on this PC) written in GAME_DESIGN.md and met.
- [ ] T-024 Slice polish from playtests (lead + team) - acceptance: all open playtest notes triaged; Jimmy plays start to boat unlock and says it's fun. **Jimmy playtest D.**

## Later (after the slice)
- Online co-op 2-4 (shared boat, proximity voice chat, noise from all players).
- Boat upgrades and the respawn-point boat; the foggy, frozen and murky regions; the Kraken.
- Third-person bodies for other players (was T-006: Game Animation Sample import; needs runbook phase C10).

## Done

# Game design (source of truth)

Status: v1 APPROVED by Jimmy on 2026-09-22 (interview, 14 questions). Keep it current as playtests change things.
Working title: **Lure**

## One-sentence pitch
A first-person, open-world fishing sandbox for 1-4 friends: sail between sunny, foggy, frozen and murky regions, master your gear to land ever bigger fish, and keep your voice down, because noise wakes the things that live in the deep.

## Genre, camera, controls
- First-person 3D. (The project started from the Third Person template; the camera and character are converted to first person, with visible arms holding the rod.)
- Movement: walk, run, jump, crouch, go prone.
- Keyboard + mouse and gamepad.
- Co-op: online 2-4 players, sharing the world and the boat. The first playable level is solo, and all code is multiplayer-ready (server-authoritative) from day one. Co-op is the first milestone after the vertical slice.

## Core loop
- Every 30 seconds the player: picks a spot, casts, waits and reads the bobber, hooks the bite, and fights the fish with the reel while managing line tension. Or moves (runs, climbs, crawls) to reach a better spot, staying quiet near danger.
- Every 5 minutes the player: fills the cooler, sells the catch at a dock, spends money on better rods, line and hooks, levels up, logs new species in the journal, and takes or finishes an NPC request. Time of day moves on and changes what bites and what hunts.
- A session ends when: the player chooses to stop. There is no final goal: progress is player level, gear, the fish journal and reaching harder regions. Progress saves at docks and on quit.

## Player verbs and abilities
- Move: walk, run (loud), jump, crouch (quieter, lower), prone (quietest, hides from sight lines, fits through low gaps).
- Fish: cast (hold to charge distance, aim), wait and watch the bobber, hook (timed input when it bites), reel (hold to reel; ease off when tension spikes), let the fish run, and land it.
- Gear matters (mix of arcade and realistic):
  - Rod: power (how hard you can pull) and cast distance.
  - Line: strength (breaks if tension goes over it for too long).
  - Hook / bait: which species will bite, and how securely they stay hooked.
  - Each fish has a level, weight, strength and fight pattern (darting, diving, sudden runs). Fish above your level or gear are possible but very hard: they snap weak line.
- Collect: fish go into the cooler (limited space); sell at docks; each new species and record size is logged in the journal.
- Talk: NPCs at docks run the shop and give requests (catch X, deliver Y, find Z) for money, XP and gear.
- Boat (unlocked a bit into the beginner zone): a basic boat to reach other islands and fishing spots within the zone. Speed makes a wake, and wake makes noise.
- Noise: every player gives off noise from:
  - real microphone volume (talking, laughing, yelling)
  - actions: running, jumping into water, splashing, boat speed and wake
  Noise works by proximity: creatures hear noise within a radius, and louder noise reaches farther. The mic can be switched off in settings, and then only actions count.
- Hide and counter: crouch or prone to stay out of a creature's sight line and to dodge certain boss attacks.
- Prone fishing (Jimmy, 2026-09-22): you can fish while lying prone (e.g. flat at a dock or rock edge while something searches). The rod is tucked while you crawl and comes out when you stop; cast, reel and land all work prone.

## Fish system (built to scale to many species)
Jimmy wants many species eventually, each catch varying in difficulty and value (for example weight and rarity). The code is data-driven from day one, so adding a species, a rarity tier or a modifier is a data edit, not a code change:
- **Species** (DT_FishSpecies, one row per species): name, habitat and region tags, time-of-day and weather windows, bait/hook tags, base level, weight range and size distribution, base stats (strength, stamina, speed, fight pattern), base value, mesh and look, which rarities and modifiers it can roll, and its journal info.
- **Rarity tiers** (DT_FishRarity, rows, not a fixed list): e.g. Common, Uncommon, Rare, Epic, Legendary. Each has a roll chance and multipliers for value, XP and difficulty, plus a visual cue (sheen or glow color). New tiers can be added as rows.
- **Modifiers** (DT_FishModifier, rows): traits a single catch can roll, such as Heavy, Feisty, Giant, Albino, Scarred, Glowing or Night-born. Each changes stats through a list of (stat, add or multiply, amount) entries, affects value, and has conditions (which species, regions, times) and a roll chance. A catch can carry several modifiers.
- **Stats are open-ended**: fish stats are a named-stat map (gameplay tags such as Fish.Stat.Strength and Fish.Stat.Stamina), so new stats can be introduced in data and read by the systems that care.
- **Every catch is an instance**: rolling a bite produces a saved, network-replicated record (species, rarity, modifiers, rolled weight, final stats and value, random seed). The cooler, selling, journal records, trophies, requests and save files all use that record, so new variables are carried everywhere automatically.
- **One roll pipeline**: species base values, then weight scaling, then rarity, then modifiers, then the final stats and value. It is a single tested function, so balancing is predictable and covered by automation tests.
- The vertical slice ships with 6 species, 3 rarity tiers and a few modifiers, running on this full system.

## Win / lose conditions
- No final win. Goals are self-chosen: level up, complete the journal, finish requests, reach and survive tougher regions.
- Level scaling instead of locked doors: every region is reachable from the start, but an under-levelled player can't hook or hold the fish there and won't survive the dangers, so beginners level up in the beginner zone first.
- Getting caught (the shark rams you into the water, a boss catches you): you respawn at the region's respawn point. Unsold fish in the cooler are lost, and gear may be damaged (repairs cost money). Levels, journal and money are kept.
- Respawn points: set places in each region (docks, camps). At higher level, players can buy a boat that works as a mobile respawn point.

## World, setting, tone
- An open-world ocean split into regions, reached by boat, each with its own mood, fish and dangers:
  - Tropical & sunny (beginner zone): lagoons, palm islands, reefs, docks. Welcoming by day, uneasy after dark.
  - Foggy & eerie: mist, rocky sea stacks, strange lights, things that watch.
  - Frozen & cold: ice sheets, fishing holes, thin ice, blizzards.
  - Murky & gloomy: swamp or bayou, dark water, stilt shacks.
- Day/night cycle: every time of day brings its own rewards and challenges (dawn-only and night-only fish, more alert creatures and rolling fog at night, calmer water at noon).
- Tone: a fun, satisfying fishing game plus open-world mystery and exploration, plus horror tension. Fun with friends: shouting to a friend about your catch can wake something.
- Threats scale with regions. The long-term headline boss is the Kraken, woken when players are too loud or leave too much wake. Boss fights use hiding, going prone and staying quiet as counters.

## Art direction summary (details in ART_STYLE.md)
Stylized low-poly, inspired by Dredge but not copying it: clean chunky shapes, bold colors per region, and atmosphere doing the heavy lifting (fog, light, water color, sky). Sunny regions look inviting; eerie ones rely on fog, silhouettes and a limited palette.

## Vertical slice definition
What one short, polished, playable level must contain to prove the game is fun (target: 15-25 minutes of play, solo):
- **Beginner island in the tropical zone** ("Palm Key"): a dock with an NPC shop and quest giver, a beach, reef shallows, a jetty, a rocky point, a small cave or hidden cove reachable by crouching or crawling, and a respawn point at the dock.
- **First-person movement**: walk, run, jump, crouch, prone, with good feel.
- **Fishing done well** (top priority): cast, bobber, bite, hook timing, tension reel-in fight, and line snaps. Gear (rod, line, hook) visibly changes what you can land.
- **6 fish species** across shore, reef and deep-drop spots, with different levels and fight patterns; at least one dawn-only and one night-only fish.
- **Progression**: cooler, sell at dock, shop with a few gear upgrades, XP and player level, fish journal with record sizes, and 3 short NPC requests.
- **Day/night cycle** (compressed, about 20 real minutes per day).
- **Tension**: a noise meter (actions + microphone, by proximity). A reef shark that gets curious when you're loud or fast near the water: it circles, steals fish off the line, and can knock you in. At the lagoon's edge, a huge dark shadow stirs if you get very loud; you must hide or go prone out of its sight line or get caught. Getting caught means losing the cooler and respawning at the dock.
- **Boat unlock** at the end of the slice: a basic boat (unlocked at a player level) to reach one second fishing spot or islet in the beginner zone.
- **Save/load** of progress, a pause menu with settings (mic on/off, mic sensitivity), and the F8 feedback key for playtests.

## Out of scope for now
- Online co-op (first milestone after the slice; code is built ready for it).
- The foggy, frozen and murky regions; the real Kraken fight and other bosses.
- Boat upgrades and the respawn-point boat; bigger fleets.
- Story and lore beyond hints; voice chat between players (comes with co-op, proximity-based).
- Custom character models (first-person arms only for now); a large fish roster; crafting.

## Open questions for Jimmy
- Level cap and pacing: how many hours should the beginner zone last? (Proposal: about 1-2 hours before the next region is comfortable.)
- Should the fish journal show hints (silhouettes, where and when a fish bites) or stay a mystery until caught?
- Should dropping into the water let you swim, or does falling in always count as "caught"?

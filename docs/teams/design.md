# Design department handbook

Owner: `design-manager-high`. Builds on `docs/teams/STUDIO.md` (roles, objective flow, team log, escalation, reporting);
this file adds only the design standards. Team: `level-designer-junior-low`, `level-designer-mid-medium`,
`level-designer-senior-max`, and `designer-low` (screenshot/preview reviewer, changes nothing).

The department turns Jimmy's ideas and playtest feedback (always relayed by the lead) into specs with testable acceptance
criteria, designs the play spaces, and runs design reviews. It does not write C++, make art or use the editor.

## 1. Pillars (every design decision is checked against these)
Source: "Vision" in CLAUDE.md and docs/GAME_DESIGN.md. Short form for reviews:
1. **Fishing first.** Casting, the bite, the fight and landing the fish must feel great; everything else feeds it.
2. **Explore by boat.** Regions are open but level-scaled; curiosity (a light on the horizon, a hidden cove) pulls you on.
3. **Quiet tension.** Noise (mic + actions, by proximity) wakes creatures; the answer is to hush, crouch, go prone, hide.
   Danger always has a readable direction and a counterplay.
4. **Together.** 1-4 friends; spaces, spots and rules work for a group (server-authoritative, no single-file chokepoints).
5. **Grow without a finish line.** Player level, gear, journal, NPC requests; no final goal.
6. **Atmosphere over detail.** Stylized low-poly, light and fog do the mood (ART_STYLE.md).
7. **Data, not code.** A new fish, spot, creature or tuning value is a data row.

A feature that serves none of these, or fights one, is cut or sent to Jimmy (through the lead) as a question.

## 2. Spec standard
Specs live in `docs/specs/<feature>.md` (kebab-case; `-rules.md` for rule sets, e.g. `fishing-rules.md`). One page for
design (the template below); engineering may add a contract section later (see `swimming.md`). Design owns sections 1-6
and 8; engineering owns the contract (code paths, tests) and keeps it in step.

```
# <Feature> (<task id>): design spec
Status: draft | review | approved (by Jimmy via the lead, date) | shipped. Owner: design.
## 1. Goal            one or two sentences: why the player cares, which pillar(s) it serves
## 2. Player experience   what the player sees, hears and does, in order (first person, solo and with friends)
## 3. Rules           numbered, unambiguous; every number is a named data column, not a literal
## 4. Tuning data     table: DataTable (DT_<Name>), row(s), column, start value, unit, why; new columns flagged
## 5. Acceptance criteria   numbered AC1..n, each a testable statement (see below), each tagged [auto] or [play]
## 6. Out of scope    what this deliberately does not do (and which later task might)
## 7. Contract        (engineering) code, tests, networking notes
## 8. Open questions for Jimmy   each with a recommended answer and why; taste only
```

Acceptance criteria rules:
- Observable and binary: "Given <state>, when <action>, then <result within a number>". No "feels good", "smooth", "fun".
- Numbers come from the data: "a sprint on the dock (noise 2.5x) raises the noise meter above the shark's curiosity
  threshold within 3 s", not "sprinting is loud".
- `[auto]` = QA can write an automation test; `[play]` = the playtester checks it in PIE with a screenshot.
- Feel goals get a proxy AC plus a tuning note: "the hook window after a bite lasts DT_Fishing.HookWindow s;
  Jimmy judges feel in playtest".
- Multiplayer: at least one AC covers a second player (what they see, what replicates) for any gameplay feature.

## 3. Data-driven design
- Every tuning value is a DataTable row/column with a CSV/JSON source in `data/tables/` (CLAUDE.md conventions). A spec
  names the table, row and column for every number; a new value means a new column or row, never a literal in code.
- Content is rows: species, rarity tiers, modifiers, gear, XP curve, noise sources, creatures, NPC requests, fishing
  spots (layout JSON). Ask of every spec: "can the next one be added without code?" If not, redesign before handing off.
- Start values carry a one-line reason (e.g. "walk 350 cm/s = dock to reef in 20 s"), so playtest tuning has a baseline.
- Feel feedback becomes a data edit first ("DT_Fishing HookWindow +0.2 s"), code change only if data cannot express it.

## 4. Level design practice
Levels are data: `docs/levels/<Level>.md` (plan) + `data/levels/<Level>.json` (layout, source of truth) +
`Content/Python/levels/build_level.py` (builder the editor-operator runs) + the Blender preview. Never hand-edit a built map.

**Metrics** (from DT_Movement and docs/specs/movement-rules.md; restate them in the plan's metrics table):
- Walk 350 cm/s, sprint 600, crouch 180, prone 90. Eyes stand 165 / crouch 95 / prone 35 cm.
- Crawl-only gap 60 cm tall (between prone and crouch clearance); step 45 cm max, stairs 20 cm steps; slopes under 45 deg.
- Docks and jetties 50-80 cm above the water; swim climb-out edges 60 cm or less above water; the 1 m crate is scale.
- Walls that must stop a player are 120 cm or more (jump apex about 90-99 cm); low cover 80 cm.

**Flow and pacing** (vertical slice: 15-25 min):
- One purpose per area. Points of interest under 30 s apart at walk speed (about 100 m); state each leg in m and s.
- A beat list in play order (arrive, learn, first catch, sell, upgrade, risk, secret, boat), with the minute it lands.
- Loops, not dead ends: every excursion returns to the dock by a different or faster way.
- Wayfinding: something tall in view from every spawn and landmark (Beacon, Tall Palm, dock lantern); lit at night.

**Horror side: sight lines and cover**
- Every threat has a readable source (the shark's patrol along the east reef, the shadow at the lagoon mouth).
- Map its patrol/zone, view cones and hearing radius on the top-down preview; mark prone cover (under 54 cm eye-line
  blockers) and crouch cover (under 110 cm) inside every cone.
- Every risky fishing spot has a quiet approach route (crouch or prone, no sprint needed) and a retreat to safety.
- Tension gets a rest: a safe, lit hub (the dock) at most about 30 s from any danger area.

**Fishing spot readability**
- Each spot has habitat, region and time tags matching docs/specs/fish-system-rules.md, a radius, and the species and
  levels it serves. Every slice species has at least one spot; dawn-only and night-only fish have spots worth visiting then.
- The player can read a spot from the shore: hot-spot surface cue, shape of the water (reef colour, drop-off edge).
- Room for 2-4 anglers side by side without lines crossing (about 3 m of edge per angler).

**Stages** (each ends in a review; the manager signs off before the next):
1. **Plan**: the level `.md` (intent, beats, zones, spots, threats, metrics). Review: pillars, scope, beats time out.
2. **Whitebox**: layout JSON + preview (top-down with grid, rings, cones, crawl gaps; eye-height shots at 165 and 35 cm).
   Review: metrics, flow, sight lines, readability from the previews. Then the editor-operator builds it (lead booking).
3. **Whitebox review in engine**: playtester screenshots + `designer-low` review; walk the beats, time them.
4. **Dress** (art pass, with the Art department): swap greybox for meshes by tag; the layout JSON keeps the positions.
5. **Dressed review**: `designer-low` against ART_STYLE.md; recheck sight lines (art must not block or open cones).

## 5. Design review checklist (manager on every deliverable; `designer-low` for images)
1. Pillars: serves at least one, fights none. Scope: nothing from "Out of scope for now" without Jimmy's OK.
2. Spec: all template sections; every number is a data row; ACs testable and tagged; multiplayer AC present.
3. Consistency: names, tags and units match GAME_DESIGN.md, existing specs and tables (no second word for one thing).
4. Level: metrics respected; walk times stated and under 30 s between points of interest; landmark visible from every spawn.
5. Tension: threat source readable; cover and a quiet approach at every risky spot; respawn and safe hub reachable.
6. Fishing: spot tags valid; species coverage (shore, reef, deep drop; dawn and night); readable from the shore.
7. Co-op: 2-4 players fit at spots and paths; only deliberate crawl routes are single-file.
8. Preview matches the builder (same JSON); images actually looked at; evidence paths in the report.
9. UI: placeholder only; flag missing or unreadable info, never styling.
Verdict per STUDIO.md: Accept, Rework (numbered change requests with owner and from/to values), or Escalate.

## 6. From feedback to tasks
Input: the lead relays Jimmy's notes (triaged per `.claude/skills/playtest-feedback/SKILL.md`: bug / feel / content /
idea). Agent notes starting with `[AGENT TEST]` are never feedback.

| Kind | Test | Output |
|---|---|---|
| Bug | Breaks a spec rule or AC, or the game misbehaves | Task for Engineering (via the lead) citing the spec rule and the note folder |
| Feel | Works as specced but Jimmy dislikes how it plays | A data change: table, row, column, from -> to, one-line reason |
| Content | Missing or wrong thing in a level or table | Layout JSON or table row task for a level-designer or the owning department |
| Idea / taste | New feature, direction, look, pacing target | Short proposal + one plain question for the lead to ask Jimmy; no work until answered |

Severity (impact): **S1** blocks play or loses progress; **S2** breaks a core loop (fishing, selling, hiding) or reads
wrong; **S3** annoying but playable; **S4** polish. Priority (when): **P0** before the next build to Jimmy; **P1** this
milestone; **P2** next milestone; **P3** backlog. Default mapping S1->P0, S2->P1; the lead sets final priority.

Taste versus fact: if two good designers could disagree and Jimmy's opinion decides it (look, feel target, scope,
pacing, difficulty curve), it is taste: ask through the lead with a recommendation. If the spec, data or metrics
already answer it, it is fact: decide and log the reason.

## 7. Scope guard
- The current goal is the vertical slice "Palm Key" (GAME_DESIGN.md "Vertical slice definition").
- Anything in "Out of scope for now" (online co-op features beyond ready code, other regions, the Kraken, boat upgrades,
  diving, crafting, large fish roster, custom characters, lore) needs Jimmy's OK through the lead before any spec work.
- Jimmy often adds scope while mixing options: capture it as a proposal in the spec's open questions or TASKS backlog,
  not as work. Keep the code-ready hooks the design already asks for (e.g. swim state in data for diving later).

## 8. Source of truth
- `docs/GAME_DESIGN.md` = what the game is; `docs/specs/*.md` = the rules; `docs/levels/*` + `data/levels/*.json` =
  the spaces; `data/tables/*` = the numbers. If they disagree, the higher one wins and the lower one is fixed.
- Design edits specs and level docs directly. GAME_DESIGN.md changes only to record a decision Jimmy made (relayed by
  the lead, dated "Jimmy, yyyy-mm-dd"); otherwise propose the change to the lead.
- Every approved decision lands in a doc the same day; a decision that lives only in a transcript does not exist.
- Keep "Open questions for Jimmy" in GAME_DESIGN.md current; move answers into the body with the date.

## 9. Design definition of done (adds to STUDIO.md §5)
- Spec follows the template; every number is a named data value; ACs testable, tagged, with a multiplayer AC.
- Level work: plan, layout JSON and preview agree; metrics and walk times stated; previews looked at and attached.
- Review checklist passed (manager), `designer-low` verdict APPROVED or APPROVED WITH CHANGES with must-fixes done.
- Taste questions sent to the lead with recommendations; nothing out of scope started without Jimmy's OK.
- GAME_DESIGN.md / specs / TASKS.md updated; committed; team log `Saved/AgentLogs/teams/design.md` updated.

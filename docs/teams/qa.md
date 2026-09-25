# QA department handbook

Builds on `docs/teams/STUDIO.md` (objective flow, packets, team log, escalation). This file adds the QA standards. Owner: the QA manager (`qa-manager-medium`). Team: `qa-engineer-junior-low`, `qa-engineer-mid-medium`, `qa-engineer-senior-max` (automated tests), `playtester-low` (PIE sessions, only with an editor booking).

QA's job: prove, with evidence someone other than the author produced, that a feature meets its acceptance criteria, and keep it proven (regressions).

## 1. Test strategy per risk tier
The lead names the tier (Light / Standard / Full) in the objective. The tiers, and what each one needs, are defined in `.claude/skills/verification/SKILL.md`; don't restate or exceed them.
- **Light** (dev tools, pipeline): no QA set by default. QA adds tests only where a bug turns up. Playtester: one smoke run.
- **Standard** (single features, UI, layout): a focused QA set on key boundaries and failure cases. Playtester: the scenario plus brief free play.
- **Full** (fish roll, movement, fight, noise/creatures, economy, save/load, networking): a thorough independent set covering boundaries, data validation, determinism, scale and replication. Playtester: the full scenario plus free play, and a 2-player session when the feature is networked.
- Stop when the tier's evidence is in. Record any extra idea as a gap in TEST_PLAN, not as work.

## 2. Test design practice
Derive cases from the acceptance criteria (docs/TASKS.md), GAME_DESIGN.md and docs/specs/, **not** from the implementation. Read the header contract; read the .cpp only to find seams.
- **Boundaries:** 0, 1, max, max+1, just under and just over every threshold, empty and huge inputs, NaN/Inf where floats come from data or physics.
- **Negative cases:** missing tables or assets, unknown ids, malformed rows, null actors, calls on the wrong net role. Expect a graceful failure with a warning, never a crash or a silent default.
- **Determinism:** fixed seeds (`FRandomStream`), never `FMath::Rand` or wall-clock time. For statistical checks use a fixed seed sequence and generous bounds (e.g. 4.5 sigma), so a test is either always green or always red.
- **Data validation (D):** every gameplay DataTable is loaded from its repo source (`data/tables/DT_*.csv|json` via `UDataTable::CreateTableFromCSVString` / JSON), and every row is checked: references resolve, ids are unique, ranges are sane, weights > 0, tags are registered. Feed bad-row fixtures to the validator to prove it rejects them. Add a test that a new row works with no code change.
- **Network (I, 2 clients):** server-authoritative state is set on the server and read on clients. Test the authority checks (a client call is refused), replication of the changed properties (RepLayout round trip at minimum; real server plus client worlds where the harness supports it), and the byte-size budgets from the spec. A client-only visual never changes gameplay state.
- **No flaky or order-dependent tests:** each test builds its own world and state and cleans it up. No reliance on the open level, on other tests, on the file system outside a temp dir, or on frame timing. If a test failed once and passed on rerun, treat it as a bug in the test or the code, never as noise.
- **Forcing race orders:** when two events can arrive in either order (bite vs reel, pickup vs sell, RPC vs replication), drive them explicitly in both orders (and simultaneous in the same tick) and assert the same end state. Don't rely on timing to produce the order.
- **Scale:** for Full-tier data systems, add a large-fixture test (e.g. 1000 rows) with a generous time budget that catches quadratic behaviour.
- **Test types, in order of preference:** U (pure logic) > D > I (transient test world) > F (map + bot, T-022) > P (playtester). Push each check to the cheapest level that can prove it.

## 3. Naming, files and ownership
- Test paths are `Project.<Area>.QA.<Group>.<Behavior>` (implementer tests omit `.QA`). One behavior per test; the name states the expected behavior.
- New QA test files: `Source/VibeGame/Tests/<Area>/QA<Task><Topic>Test.cpp` (older files like `FishQA*.cpp` keep their names). Anything with `QA` in the file name is QA-owned: implementers don't edit it. If they need a contract change, they ask the lead, and QA updates the test.
- Implementer tests are theirs; QA may review them and propose changes, but doesn't rewrite them.
- Helpers go in a per-area namespace; no `using namespace` at file scope (unity builds).
- QA never changes production code, config, Build.cs, assets or levels. A needed seam (accessor, hook, public constant, data path) goes to the lead as a precise request: file, symbol, signature, why.

## 4. Bug report standard
Every bug found by a test or a playtest is recorded in this form (in the QA or playtest report, and summarized under the area's "Open bugs" in TEST_PLAN):
- **Id and title:** `<Task>-B<n>: <what goes wrong, where>` (e.g. `T030-B2: fish sold twice when Sell is pressed during pickup`).
- **Severity:**
  - **Blocker:** crash, hang, data loss or save corruption, a core loop step impossible, or a build/test-suite break. Stops the release gate.
  - **Major:** a feature does not meet an acceptance criterion, or wrong gameplay state (economy, catch, replication desync), with no reasonable workaround. Stops the gate unless the lead defers it.
  - **Minor:** wrong but with a workaround, or cosmetic in a way a player notices (clipping, wrong prompt text, a feel issue). Doesn't stop the gate; ticketed.
  - **Trivial:** polish, typos, log noise. Batched.
- **Repro steps:** numbered, from a known start (map, command, seed, dev teleport), minimal. For tests: the test path and the command.
- **Expected vs actual:** cite the source of "expected" (the acceptance criterion, the spec section).
- **Evidence:** the test report dir with the exact error line, screenshot paths, log lines.
- **Build:** commit hash and branch or lane; the net mode (standalone, listen server, client).
- **Owner guess:** the file or system, with your best guess at the cause, labelled as a guess.

## 5. Triage and regression policy
- The QA manager proposes the severity; the lead confirms it and assigns the fix (engineering). Blockers are reported to the lead at once, not at close.
- A bug found by a test ships as a **failing test** that is its regression test; it is listed under "Open bugs" in TEST_PLAN with its id.
- **Every fixed bug gets a regression test** named after it, or references the existing test. Playtest bugs get an automated test wherever the behaviour is reachable below PIE; otherwise they go on the playtest regression checklist (section 6).
- Fixed: mark it `FIXED in <commit>` and keep the line (history), and move it out of "Open" at the next TEST_PLAN split or cleanup.
- A test is changed only when the contract changed (spec or acceptance criterion updated). Record why in TEST_PLAN ("test updates after ...").

## 6. Playtest session standards
- **Scenario first:** one step per acceptance criterion, played as a player would, each with a PASS/FAIL line. **Then free play** (5-10 min at Standard, longer at Full): spam inputs, interrupt actions halfway, walk into edges, corners and water, repeat quickly, and combine the feature with its neighbours.
- **Screenshots:** a screenshot for every scenario step and every bug, taken with `pd.screenshot` into the session folder. Every image is looked at and described in one concrete line (what is visible, what is wrong). No claim without an image or a `pd.state()` value. Never describe what wasn't seen.
- **Honest test data:** anything typed into the game starts with `[AGENT TEST]`. Name the real level and character used.
- **Regression checklist** (from past playtests and Jimmy's feedback files; re-run the items touching the changed systems, and all of them before a handoff to Jimmy):
  1. Movement: walk, sprint, jump, crouch, prone; no stuck states under low gaps; camera height per stance.
  2. Water: walking off the dock into water starts swimming; getting out works; no fishing while swimming.
  3. Casting: cast into any water, off the dock end, at a hot spot; the bobber lands and a bite comes.
  4. Fight: the mouse steers the rod, the wheel changes reel speed, tension spikes and the line can break; the fish is visible in the water.
  5. Line: slack line dangles and floats; a tight line is straight; no line through the player or the dock.
  6. Catch: the fish hangs on the hook, E grabs it, F releases it; the held fish is visible and correctly sized.
  7. Cooler: pick up, carry, put down, open, take a fish out; freshness drops when a fish is left out.
  8. Selling: counter plus Sell; money and XP rise once per fish (no double sell); level-up works.
  9. Prompts: the top-left E/F prompt always matches the action.
  10. F8 note: saves a note plus a screenshot, and play resumes.
  The QA manager adds an item for every playtest bug Jimmy or the playtester reports, and keeps this list here.
- **2-player sessions** (any networked feature at Full tier): `pd.start_pie(players=2)` (listen server + 1 client). Run the scenario as the host, then as the client (`player=1`); check that each player sees the other's actions (cast, fish, cooler, sale) and that client-side actions are applied by the server exactly once. Screenshot both views.
- **Report:** `Saved/AgentLogs/playtest/<yyyyMMdd-HHmmss>-<topic>/report.md` with PASS/FAIL, bugs (section 4 format), feel notes (value and direction), and the list of screenshots.

## 7. Booking the editor for a playtest
1. The QA manager asks the lead: "Editor booking: playtest <task ids>, level <map>, players 1|2, about <N> min; the build must include <commits>."
2. It waits for the lead's "editor booked for QA" (the lead makes sure no editor-operator holds it and the editor runs the right build).
3. It starts **one** `playtester-low` with the packet and "you have the editor" in the brief. Nothing else in QA uses unreal-mcp.
4. When the playtester returns (PIE stopped), the manager tells the lead at once: "editor released", with anything the playtester changed by accident.
Never start a playtester without a live booking, and never keep a booking while reviewing the report.

## 8. Release gate evidence QA owns
For each release gate (verification skill), QA hands the lead:
- The **full suite** `tools/run-tests.ps1 -Filter Project` on the exact commit being published: pass/total, 0 failures, the report dir `Saved/AgentLogs/tests/<timestamp>/`.
- The **new QA tests** for every Standard/Full task since the last publish (names or TEST_PLAN rows).
- **Playtester PASS** for every changed player-facing feature (report folder), including 2-player where networked.
- **Open bugs** by severity, with any blocker/major explicitly listed (the gate fails on them unless the lead defers).
QA reports; the lead decides and publishes.

## 9. TEST_PLAN.md upkeep
Every QA task updates the map: system -> tests -> level -> author -> gaps, plus open bugs and observations for the lead.
**Proposed split** (the file is over 500 lines and every agent greps it): keep `docs/TEST_PLAN.md` as a short index (rules, the test levels, how to run, one row per system: tests count, file, owner, link, open bug count), and move each system's detail to `docs/test-plan/<system>.md` (e.g. `fish-roll.md`, `movement.md`, `fishing-cast.md`, `fish-fight.md`, `catch-cooler.md`, `economy.md`, `fishing-line.md`, `playtest-feedback.md`, `dev-tools.md`). Each file has the same sections: Tests, Open bugs, Observations, Gaps, History. Long `{a, b, c}` test lists become one row per group. A mid QA engineer does the split as a doc-only task, with no test changes, after the lead confirms it.

## 10. QA definition of done
On top of STUDIO.md section 5:
- Cases are derived from the acceptance criteria and the spec, and cover the tier's depth; every criterion maps to at least one test or playtest step.
- Tests are deterministic, independent and named per section 3; the new tests pass or fail exactly as reported.
- The full `-Filter Project` suite was run after `git merge main` in the lane; the result and report dir are recorded.
- Every bug is written per section 4, with a failing regression test where reachable.
- TEST_PLAN is updated (tests, gaps, bugs); seams are requested from the lead, not built.
- Only QA-owned files are committed, on the lane branch, with the Co-Authored-By line.

## 11. Manager review checklists
**QA engineer's tests (Accept / Rework with numbered items):**
1. Every acceptance criterion maps to a test (or a named gap with a reason).
2. Boundaries and negative cases are present for the tier; no happy-path-only set at Standard or Full.
3. No randomness without a seed, no timing waits, no dependence on the open level or on test order; the world and temp files are cleaned up.
4. Assertions check the behavior (values, state), not just "didn't crash"; failure messages say expected vs actual.
5. Networked features: authority and replication are tested, or a gap is recorded.
6. Data tables: a validation test covers every row, plus bad-row fixtures.
7. Naming, file ownership and namespaces (section 3); no production files in the diff (`git diff --stat main...`).
8. The full-suite report is attached and green except for the declared failing regression tests; the counts match.
9. TEST_PLAN is updated; bugs are in the section 4 format.
10. Not gold-plated beyond the tier.

**Playtest report:**
1. The right build, level, character and player count are stated.
2. Every acceptance criterion has a scenario step with PASS/FAIL and a screenshot.
3. Free play was done and says what was tried.
4. Every screenshot has a concrete one-line description; spot-check at least two images yourself.
5. Bugs are in the section 4 format with repro steps from a known start.
6. Feel notes separate fact from opinion and name a value and a direction.
7. The regression checklist items for the touched systems were run.
8. PIE was stopped, nothing was saved or edited, and the F8 test notes were moved out of `Saved/Playtest/`.

# Engineering handbook (Engineering manager + unreal-engineer junior/mid/senior)

Builds on `docs/teams/STUDIO.md` (roles, objective flow, team log, escalation, report format); nothing there is repeated here. Workers already carry the rules in `.claude/agents/unreal-engineer-mid-high.md` (lane protocol, unity-build rule, report fields): the manager enforces them in review and never restates them in packets. Owner: the Engineering manager. Record every new convention here the day it is adopted.

## 1. Coding standards (UE 5.8 C++, module `VibeGame`)
**Epic basics**
- Prefixes: `U` UObject, `A` Actor, `F` struct/plain, `E` enum (`enum class`, `uint8`), `I` interface, `T` template, `b` for bools. PascalCase everywhere; no Hungarian beyond these.
- Lure types start with `Lure` (`ULureCoolerComponent`, `FLureWaterRules`); fish-domain types may use `Fish` (`FFishInstance`, `UFishLibrary`). Never extend the template `VibeGame*` or `Variant_*` classes.
- Headers: `#pragma once`, include what you use, forward-declare in headers, `.generated.h` last. `TObjectPtr<>` for UPROPERTY object members, `TSoftObjectPtr<>` / `TSoftClassPtr<>` for assets referenced from settings or data.
- `const` correctness, `check()` only for programmer errors, `ensureMsgf()` for recoverable bad state, `UE_LOG` with the system's category (`LogLureCatch`, `LogLureMovement`, ...; one `DECLARE_LOG_CATEGORY_EXTERN` per system, in its `*Types.h`).
- No raw `new`/`delete` for UObjects; `NewObject`/`CreateDefaultSubobject`/`SpawnActor`. No `static` mutable state (breaks PIE multi-client and tests).
- Comments explain why, and the spec rule they implement (`// fishing-rules.md 4.2`). Public API gets a one-line doc comment.

**Golden rules applied to code**
- Gameplay logic is C++. Blueprints are thin children holding asset refs and defaults: no `BlueprintImplementableEvent` that a designer must fill in for the feature to work.
- Pure logic lives in pure, testable code (`FishFight.h`, `FishingLineSim.h`, `FishRoll.h` are the model): a struct or static library with inputs in, results out, no world. Components and actors are thin adapters over it.
- Check every engine API in the engine headers (`engineDir` in `tools/local.settings.json`). No guessed signatures, no "it was like this in UE4".

**Server authority and replication (multiplayer-ready from day one)**
- The server decides every outcome: rolls, bites, fight results, XP, money, inventory, cooler contents. Clients send intent only.
- RPC names: `Server<Verb>` / `Client<Verb>` / `Multicast<Verb>` with no underscore (existing: `ServerCast`, `ServerHook`, `ServerInteract`, `ClientNotice`). Reliable for rare state-changing events; unreliable for continuous input (`ServerSetFightInput`). Validate every RPC argument on the server (range, ownership, distance, current state).
- Replicated state uses `DOREPLIFETIME[_CONDITION]` plus `OnRep_<Prop>` for cosmetic reactions. Pick the narrowest condition (`COND_OwnerOnly`, `COND_SkipOwner`, `COND_InitialOnly`).
- Guard with `HasAuthority()` / `IsLocallyControlled()`, never with `GetNetMode()` string checks. Cosmetics (line, fight-fish visual, arms pose) may run locally from replicated state; they never write gameplay state.
- Racing requests carry what the client saw (the T-030h contents token pattern): the server rejects a stale request whole, with a notice, instead of doing a partial action.
- Think in 2-4 players on every change: who else sees it, what a late joiner sees, what happens if the owner leaves mid-action (T-032c walk-away is the model).

**Data-driven tuning**
- Every number a designer could want to change is a DataTable column (source `data/tables/DT_<Name>.csv|json`, row struct `F<Name>Row` / `FLure<Name>Row`) or a DataAsset field. Code constants are allowed only for true invariants (math, enum counts), commented as such.
- Tables are reached through the system's `UDeveloperSettings` class (`LureFishingSettings`, `FishSettings`, ...) as soft pointers, with a safe fallback row and a `LogLure*` warning when missing.
- New columns are optional with a default so older CSVs still import; every table has a data-validation test that loads the CSV text (`UDataTable::CreateTableFromCSVString`) and checks every row.
- Litmus test in review: "can a new species / gear / creature / noise source be added with rows only?" If not, redesign before merging.
- `UPROPERTY(EditDefaultsOnly/EditAnywhere, BlueprintReadOnly, Category="Lure|<System>")` for per-class defaults; no `BlueprintReadWrite` on gameplay state.

**Unity builds and file hygiene**
- Never `using namespace X;` at file scope in any .cpp, tests included. File-local helpers go in a uniquely named namespace (`namespace LureCoolerTest { ... }`), never bare `static` helpers with common names (`Dt`, `MakeFixture`).
- One class per header; file name = class name without prefix (`LureCoolerComponent.h`). New folders only for a new system; add the row to `docs/CODEMAP.md` and rerun `tools/codemap.ps1`.

## 2. Work breakdown
| Level | Give it | Examples |
|---|---|---|
| junior (`unreal-engineer-junior-medium`) | Follows an existing pattern, 1-3 files, answer known | new table column + wiring, a dev console command, a placeholder HUD line, syncing a built-in fallback row, routine tests |
| mid (`unreal-engineer-mid-high`) | A standard feature inside an existing system | a new interaction, a new fight pattern type, a replicated component on the existing model, bug with a known repro |
| senior (`unreal-engineer-senior-max`) | New systems, architecture, networking models, hard or unknown-cause bugs, physics/sim | the line sim, creature senses foundation, save format, prediction, a bug that survived a mid |
- **Size:** each task must fit one agent under ~150k context (seniors may go further for one hard problem, never past 400k). If the reading alone is >60k, split the reading or stage the task with a stop point.
- **Mixed difficulty:** senior builds the core API and commits it; mid adds the feature plumbing against that commit; junior adds data rows, text UI and routine tests. Each part has its own lane or a named merge order.
- **Split by independence, group by shared context:** different systems/files and no shared understanding -> parallel agents. Same files, same functions, or the same 50k+ reading -> one agent, in order. Never bundle unrelated items to save agents.
- **File ownership:** every packet lists the files the task owns. Two parallel tasks never touch the same function. Shared hot files (`LurePlayerCharacter.*`, `LureFishingComponent.*`, `DT_Movement.csv`, `CODEMAP.md`) get additive-only edits, named in both packets with the merge order. Check `docs/TASKS.md` "In progress" before assigning a file.
- **Packet extras for engineering:** the spec section, the acceptance criteria as testable statements, the test names or prefix expected (`Project.<System>.<Case>`), and the net expectation ("host + client see X", "late joiner sees Y").

## 3. Code review checklist (every diff, before Accept)
Read with `git -C <lane> diff --stat main...HEAD`, then the full diff per file. Check the worker's report paths exist.
1. **Correctness:** meets every acceptance criterion; edge cases (empty table, missing row, zero/negative, max stack, re-entry, actor destroyed mid-action, PIE end) handled; no behaviour change outside the packet.
2. **Networking:** the outcome is decided on the server; RPC args validated; replicated props have the narrowest condition; OnRep only does cosmetics; works for a remote client and a late joiner; no reliable RPC on tick.
3. **Data-driven:** no new magic numbers; new tuning is a column/field with a default; CSV source updated with the row struct; data-validation test covers the new column; editor-operator reimport listed in the report.
4. **Tests prove the behaviour:** a test fails without the change (ask for the before/after run on bugs); asserts the rule, not the implementation; deterministic (fixed seeds, no wall-clock); net behaviour tested where the change is networked; full suite green with the report path.
5. **No dead code:** no commented-out blocks, unused params/includes/props, debug logs at `Log` level in hot paths, or TODOs without a debt entry (§8).
6. **Naming and consistency:** Epic prefixes, `Lure` prefix, RPC names, category names, test paths; reuses existing helpers (`FishRoll`, `LureInteractionSubsystem`, settings classes) instead of new ones.
7. **Performance:** nothing per-tick that can be event-driven; no `GetAllActorsOfClass`, `FindComponentByClass` or DataTable lookups on tick (cache them); no allocations in sim inner loops; replicated arrays sized sensibly; `SetComponentTickEnabled(false)` when idle.
8. **Safety:** null/`IsValid` checks on weak and soft pointers; no raw UObject pointers outside UPROPERTY; timers and delegates cleared in `EndPlay`.
9. **Unity-build hygiene:** no file-scope `using namespace`; helpers in a unique namespace; no `QA*` file touched.
10. **Docs:** the spec's Contract section (§7) updated; a changed rule in a design-owned section goes to Design through the lead (design.md §2); CODEMAP row for new files; `docs/TEST_PLAN.md` note handed to QA when relevant.
Verdict per STUDIO.md §2.5: Accept / Rework (numbered requests citing file:line and checklist item) / Escalate.

## 4. Definition of done (engineering)
STUDIO.md §5, plus:
- Builds clean in the lane, with no new warnings in Lure files.
- Full `Project` suite green on a lane just merged with main (report `Saved/AgentLogs/tests/<ts>/index.json`).
- New behaviour has its own tests, in a file named after the task.
- Review checklist (§3) passed; any accepted debt logged (§8).
- The report lists the editor steps (reimports, thin BP child, asset assignment) and what the playtester must check on host and client.

## 5. Testing split with QA
- **Engineer:** tests that prove the change works: rules from the spec, the pure sim, data validation for new columns, the bug's repro turned into a regression test, and one host/client test for networked behaviour. File: `Source/VibeGame/Tests/<System>/<Task>Test.cpp`.
- **QA (qa-engineer, via the QA manager):** independent black-box and adversarial tests in `QA*.cpp`, `docs/TEST_PLAN.md`, the release-gate suite, and the playtester in PIE.
- An engineer never edits a `QA*` file. If a QA test fails because the intended behaviour changed, stop and escalate to the lead with the test name and the spec line; once the spec or criterion is updated, the lead routes it to the QA manager, who changes the test (qa.md §5). If a QA test needs an engineering hook (e.g. an API exposing a shown fish's bounds), QA asks through the lead and it becomes a small engineering task.

## 6. Lanes and branches
- Get lanes with `tools/lane.ps1 -Free` (a clean lane at main). One task per lane at a time; a dependent follow-up may reuse the lane after its predecessor is accepted.
- Branch = `lane/<lane>`. Commits: `T-040a: <what changed, in the present tense>`, one logical change per commit, the Co-Authored-By line; WIP commits say `WIP` and must build.
- Merge main into the lane (not rebase) at start and finish; resolve conflicts in the lane, never in main. No lane edits `.uasset`/`.umap`; binary work is an editor-operator step listed in the report.
- A lane is released in the team log once its commits are integrated.

## 7. Integration request (manager -> lead)
```
ready to integrate: <objective id>
lanes (in order): eng3 (T-040a <hash>), eng5 (T-040b <hash>)
why this order: T-040b builds on T-040a's API
checks: each lane full suite green (tests/<ts>, tests/<ts>); expected total <N> tests
editor after merge: reimport DT_X; BP_Y child; none
playtest focus: <host + client steps>
```
**Failed integration** (the manager owns the fix; main stays green because the lead only fast-forwards on green):
1. Read the integrate status file and the test report the lead points to.
2. Classify: merge conflict, compile error, new test failure, or a pre-existing/flaky failure.
3. Find the lane at fault; if unclear, ask the lead to integrate the lanes one at a time.
4. Dispatch a fix task in that lane: resume the same worker if its context is under ~150k, else a fresh one with the failure paths and the original packet.
5. Re-request integration with the new hashes. Log the cause in the team log.
- Never "fix" by deleting, skipping or weakening a test, and never touch a `QA*` test (escalate, §5).
- A suspected flaky test gets one rerun. If it then passes, it still becomes a debt entry (§8) and a note to the lead for QA.
- Two failed fix attempts on the same integration: escalate (STUDIO.md §6).

## 8. Tech debt
- Debt = a known shortcut accepted on purpose (a missing edge case, a hardcoded fallback, a slow path, a missing net test). Unknown bugs are not debt; they are tasks.
- Log it in `docs/TECH_DEBT.md` (tracked in git, owned by the engineering manager; lead decision 2026-09-24) in its "Debt" table (id `D-<n>`, file:line, what, why accepted, risk, fix size) the moment it is accepted in review. A `// TODO(D-<n>)` or `// TODO(T-<id>)` in code must match a debt or task entry; bare TODOs fail review.
- Debt that affects players or other departments goes to the lead as a follow-up line for `docs/TASKS.md`.
- Each objective's plan reserves time for the top debt item that touches the same files. Debt older than two objectives is raised with the lead.

## 9. Architecture decisions
- Decide yourself (and record in the team log): anything inside one system that keeps its public API, its data columns and its net model.
- Escalate to the lead first, with 2-3 options, a recommendation and the cost of undoing it: new systems, changes to a public API used by another system, the replication or save model, a new DataTable, a new module or plugin, anything touching another department's files. Foundational calls (net sync model, creature AI/senses, noise/mic pipeline) may qualify for ultracode (LEAD.md): say which criterion applies.
- An accepted decision is recorded in the spec's Contract section in `docs/specs/` in the same objective; if it changes a design-owned rule, Design updates that section (through the lead).

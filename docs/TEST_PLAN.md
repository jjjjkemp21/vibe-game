# Test plan (owned by qa-engineer)

Map of each system to the tests that prove it, plus known gaps. Keep it current with every task.
Run everything: `tools/run-tests.ps1 -Filter Project` (prefix match on `Project.*`; the editor must be closed).
Test levels: U = unit (pure logic/data), D = data validation, I = integration (test world), F = functional/scenario (map + bot), P = playtester (PIE, manual-agent).

| System | Test path(s) | Level | Written by | Gaps |
|---|---|---|---|---|
| Pipeline: golden crate import | Project.GoldenPath.CrateImported | I | setup | none |
| Engine smoke: all maps enter PIE | Project.Maps.PIE (engine-provided) | F | Epic | none |
| Playtest feedback F8 (T-003) | Project.Playtest.NoteWriting, .GitCommit, .FpsAverage, .SubsystemDefaults | U | unreal-engineer | in-game flow (F8, pause, Enter, Esc, screenshot content) covered only by the playtester; qa-engineer to add independent edge cases (non-ASCII text, very long note, unwritable folder) |

## Rules
- Every new behavior gets at least one test written by someone other than its implementer (qa-engineer).
- Every gameplay DataTable gets a data-validation test (D) when it is created.
- Every bug fixed gets a regression test named after the bug.

# Test plan (owned by qa-engineer)

Map of each system to the tests that prove it, plus known gaps. Keep it current with every task.
Run everything: `tools/run-tests.ps1 -Filter Project` (prefix match on `Project.*`; the editor must be closed).
Test levels: U = unit (pure logic/data), D = data validation, I = integration (test world), F = functional/scenario (map + bot), P = playtester (PIE, manual-agent).

| System | Test path(s) | Level | Written by | Gaps |
|---|---|---|---|---|
| Pipeline: golden crate import | Project.GoldenPath.CrateImported | I | setup | none |
| Engine smoke: all maps enter PIE | Project.Maps.PIE (engine-provided) | F | Epic | none |
| Playtest feedback F8 (T-003): implementer tests | Project.Playtest.NoteWriting, .GitCommit, .FpsAverage, .SubsystemDefaults | U | unreal-engineer | see QA row |
| Playtest feedback F8 (T-003): independent tests (`Tests/PlaytestFeedbackQATest.cpp`, 41 tests) | Project.Playtest.QA. Text.{Empty, VeryLong, NonAsciiAndEmoji, QuotesBackslashesNewlines}; Json.{SchemaFieldsAndTypes, NoScreenshotNotReferenced, DefaultNoteIsValid, LargeWorldCoordinates, NonFiniteNumbersStayValid, FileIsUtf8}; Screenshot.PixelsMatchCapture; Folder.{NameFormat, NamesSortChronologically, SameSecondCollisions, NeverWritesIntoExistingFolder, CreatesMissingRoot, RelativeRootGivesAbsoluteFolder, RootWithTrailingSeparator, DefaultRootIsSavedPlaytest}; Failure.{RootIsAFile, RootUnderAFile, InvalidCharactersInRoot, EmptyRoot, PixelCountMismatch}; Fps.{NoSamplesIsZero, SingleSample, WindowExactlyFilled, WindowBoundary, CustomWindowHonored, NonPositiveWindowIsClamped, ZeroDeltaFrameIsFinite, HitchLowersAverage, HugeSpikeIsFiniteAndAgesOut, LongSessionStaysAccurate}; Git.{UnbornBranchIsUnknown, CrlfLineEndings, PackedRefsExactBranchMatch, LooseRefWinsOverPacked, MalformedRepoIsUnknown, WorktreeGitFileIsSafe}; Config.DefaultsMatchSpec | U | qa-engineer | (1) In-game flow, playtester (P): F8 opens the box in PIE and does NOT trigger the editor's own F8 "Possess or Eject Player"; screenshot taken before the box appears; game paused while typing; Enter saves to the real Saved/Playtest; Esc writes nothing; game resumes either way. (2) Shipping compile-out: not unit-testable, covered by the implementer's Shipping build. (3) Subsystem integration (BeginFeedback with no viewport, never pausing on network clients) not tested yet; add when co-op starts. (4) Text trimming happens in the UI (SubmitNote), not covered. (5) In a lane worktree with a branch checked out the commit is "unknown" (commondir not followed). The spec allows this; the test documents it. |

### Open bugs (failing tests; each test is the regression test for its bug)
Found 2026-09-22 by qa-engineer, report `Saved/AgentLogs/tests/20260922-221822/`. Owner: unreal-engineer (`Playtest/PlaytestNoteWriter.cpp`).
- T003-B1 `Json.NonFiniteNumbersStayValid`: a NaN/Inf location, game time or FPS is written as `nan`/`inf`, so note.json is invalid JSON (Python `json`, PowerShell `ConvertFrom-Json` and jq reject it; the engine reader accepts it).
- T003-B2 `Failure.PixelCountMismatch`: the note folder is created before the pixels are validated, so a rejected write leaves an empty note folder. SubmitNote then drops the typed note.
- T003-B3 `Failure.EmptyRoot`: an empty root resolves against the process dir, and the note was written into `Engine/Binaries/Win64`.
- T003-B4 `Git.MalformedRepoIsUnknown`: non-hash HEAD/ref content (`this is not a commit`, `not-a-hash`, `abc123`) is returned as the commit id instead of "unknown".

## Rules
- Every new behavior gets at least one test written by someone other than its implementer (qa-engineer).
- Every gameplay DataTable gets a data-validation test (D) when it is created.
- Every bug fixed gets a regression test named after the bug.

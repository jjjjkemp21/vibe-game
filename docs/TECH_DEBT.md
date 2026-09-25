# Tech debt register (owned by the engineering manager; see docs/teams/engineering.md section 8)

Known shortcuts accepted on purpose. Unknown bugs are tasks, not debt. Code TODOs must name a `D-<n>` or a `T-<id>`.

| Id | File:line | What | Why accepted | Risk | Fix size | Logged |
|---|---|---|---|---|---|---|
| D-1 | Source/VibeGame/Tests/Level/LevelPalmKeyHotSpotTest.cpp (FLurePalmKeyHotSpotArrival) | The arrival test still takes ~11 s (was 16-21 s): 108 map copies (~4 s) and the spawner's own checks (~4.3 s). | P3; getting under 5 s needs a spawner/hot-spot save-restore or a reset hook in production code, not worth it now (T-039). | Low: suite time only. | Mid, ~1 task | 2026-09-24 (BL, T-039) |

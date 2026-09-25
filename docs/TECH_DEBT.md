# Tech debt register (owned by the engineering manager; see docs/teams/engineering.md section 8)

Known shortcuts accepted on purpose. Unknown bugs are tasks, not debt. Code TODOs must name a `D-<n>` or a `T-<id>`.

| Id | File:line | What | Why accepted | Risk | Fix size | Logged |
|---|---|---|---|---|---|---|
| D-1 | Source/VibeGame/Tests/Level/LevelPalmKeyHotSpotTest.cpp (FLurePalmKeyHotSpotArrival) | The arrival test still takes ~11 s (was 16-21 s): 108 map copies (~4 s) and the spawner's own checks (~4.3 s). | P3; getting under 5 s needs a spawner/hot-spot save-restore or a reset hook in production code, not worth it now (T-039). | Low: suite time only. | Mid, ~1 task | 2026-09-24 (BL, T-039) |
| D-2 | Source/VibeGame/Catch/LureFishItem.cpp (LureFishItemPrivate::FallFromHand) | The fish drop repeats ResolveLanding's land-or-water rule (ground above water + LandTolerance) with its own downward trace from hand height, so the drop and the cast can drift apart if S1 changes that rule. | S4/T-066 had to stay out of Fishing/ (S1's files, T-071 changes ResolveLanding the same day). | Low: both read the same LandTolerance; a drift would show as a dock-edge fish released vs lying. | Junior, ~1 task: after T-071, one shared rule in FishingSpots (a start-height/ceiling parameter) called by both | 2026-09-24 (S4, T-066) |

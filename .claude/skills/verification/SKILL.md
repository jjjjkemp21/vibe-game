---
name: verification
description: Definition of done and evidence rules for this game project - which checks to run after C++, asset, level, or gameplay changes (build, automation tests, previews, screenshots, performance) and how to report results to Jimmy. Use before declaring ANY task complete, when writing tests, and when reporting progress.
---
# Verification: done means evidence

| Change | Minimum evidence before "done" |
|---|---|
| C++ code | `tools/build.ps1` succeeded (or Live Coding succeeded for body-only edits) + relevant tests pass; new behavior gets a new test |
| Blender asset | recipe run succeeded + preview PNG looked at + dimensions and triangles within budget |
| Imported asset | import result with a sane `box_extent` + editor screenshot looked at |
| Level / layout | level saved + screenshots from gameplay-relevant angles looked at |
| Gameplay feel | build + tests + a one-line note for Jimmy on what to try in the next playtest |
| Performance-sensitive | frame timing captured before and after (e.g. `stat unit`, CSV profiler) |

## Looking at images
Say concretely what you see. Check for: default/checker materials, objects floating or sunk into the floor, wrong scale (compare with the 1 m golden crate or the mannequin), black or blown-out lighting, clipping, missing objects.

## Writing tests
- Automation test paths start with `Project.`; one behavior per test; deterministic; no reliance on the currently open level.
- Prefer testing data and logic directly (spawn in a test world or call functions) over timing-based checks.
- Planned (see docs/TASKS.md): a bot playthrough functional test that walks the player along waypoints and fails on stuck/unreachable objectives, and a performance capture script with budgets.

## Reporting
- To the lead: PASS/FAIL per check with evidence paths (status JSON, test report dir, screenshot path).
- To Jimmy: plain language, short. What changed, what to try, what you need from him. No code.
- Commit after verification with a message that states what was verified.

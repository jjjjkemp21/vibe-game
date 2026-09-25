# Studio operating model (all managers follow this; Jimmy approved 2026-09-24)

Lure is run like a small professional game studio. Jimmy is the client and creative owner. The **lead** (the main Claude session) is the producer and the only one who talks to Jimmy. Four **department managers** each own one discipline and run their own team of junior, mid and senior agents. Each discipline's handbook (`docs/teams/<dept>.md`) adds its own professional standards on top of this file.

```
Jimmy
  |  (only the lead talks to Jimmy)
Lead / producer: priorities, objectives on the board, editor booking, integration to main, push, release gate
  |-- Design manager       -> level-designer, designer (review)
  |-- Engineering manager  -> unreal-engineer junior/mid/senior
  |-- Art manager          -> model-artist, animation-artist junior/mid/senior
  '-- QA manager           -> qa-engineer junior/mid/senior, playtester (with an editor booking)
```

## 1. Who owns what
| Owner | Owns | Never does |
|---|---|---|
| Lead | Talking to Jimmy. Priorities, objectives (`obj`) and questions for Jimmy (`q`) on the board; the milestone acceptance in docs/TASKS.md. Objectives for each manager. The editor (one user at a time). Merging into main (`tools/integrate.ps1`). Pushing. The release gate verdict. Tiny one-off tasks. | Deep domain work a manager owns |
| Manager | Its discipline's plan, work breakdown, dispatch, reviews, quality bar, team log and handbook. Its lanes while they are in use. | Talking to Jimmy. Merging into main. Using the editor without a booking. Doing the workers' job. Starting agents outside its team (one exception: §3, designer-low for art previews). |
| Worker | One task packet, done to the handbook's standard, with evidence | Starting agents (workers have no Agent tool). Working outside the packet. |

## 2. How an objective flows
1. **Intake.** The lead gives the manager an objective: a goal, the acceptance criteria, the priority, a deadline or order, and links. The manager confirms back in 5 lines or fewer: its understanding, its plan outline, the risks, and any questions (see §6).
2. **Plan (work breakdown).**
   - Split the objective into tasks. Each task fits one agent: expected context under ~150k, with seniors allowed more for single hard problems.
   - Group pieces that share files or reading. Split independent pieces so they run in parallel.
   - Give each task an id (the objective id plus a letter, e.g. `T-040a`), a level (junior, mid or senior, by difficulty; see LEAD.md), a lane, its file ownership, and its dependencies.
   - Two parallel tasks never edit the same function. If they must share a file, say so in both packets and name the merge order.
   - Put every task on the board (section 8): `new --kind task --parent <obj> --key <id> --level --needs --files`. A `claim` overlap warning means re-split or name the merge order.
3. **Task packet.** For each task, write `Saved/AgentLogs/tasks/<id>/brief.md` using the brief template in LEAD.md: goal, where, decided already, parallel work, anything non-standard, report limit. Start the worker with a 2-line prompt: "Task <id> (board #<n>): read Saved/AgentLogs/tasks/<id>/brief.md; write your report to Saved/AgentLogs/tasks/<id>/report.md." Then `set <id> status=doing agent=<agent id> lane=<lane> packet=Saved/AgentLogs/tasks/<id>`. Pass paths, not pasted text. `report.md` is the short summary; it links to the worker's evidence folders (qa/, playtest/<ts>/, design/, previews). Get lanes with `tools/lane.ps1 -Free`. Run workers in the background.
4. **Monitor.** Stay event-driven: you are woken when a worker finishes. Don't poll or sleep-loop. Check the context size with `tools/lead-check.ps1` when a worker has run long. Hand-off rule: send a handoff request past 250k, or 400k for seniors, and restart the task from the handoff with a fresh agent.
5. **Review.** Every result is reviewed against the handbook's review checklist and the acceptance criteria before you accept it.
   - The verdict is Accept, Rework (send back with specific, numbered change requests), or Escalate.
   - Rework goes to the same agent if its context is small; otherwise to a fresh agent with the review attached.
   - Never accept a task without evidence: build and test results, previews or screenshots you actually looked at.
6. **Ready to integrate.** When a lane's tasks are accepted, `set <id> status=merge` for each, then tell the lead "ready to integrate: lanes X, Y, merge order, expected checks". The lead runs `tools/integrate.ps1` (merge, build, full tests, fast-forward main; it marks the lane's `merge` items `done`) and tells you the result. If an integration fails, you own the fix.
7. **Close.** Report to the lead in 10 lines or fewer: done or not per acceptance criterion, commits, the evidence paths, what the editor-operator or playtester must do, open risks and follow-ups. Update the team log.

## 3. Shared resources (booked through the lead)
- **The editor** (unreal-mcp): one user at a time across the whole studio. Ask the lead for a booking with the purpose and the expected length. While you hold it, only your editor-operator or playtester may use it. Release it as soon as you're done.
- **designer-low for art previews:** the art manager may start it directly for preview reviews before import (no shared resource involved).
- **Main branch, push, release gate:** the lead only.
- **Builds:** lanes build in parallel, and `-WaitMutex` serializes the compiler. Don't start a build you don't need.

## 4. Team log (continuity across sessions)
Every agent stops when Jimmy exits. Task status lives on the board (section 8), not in the log. The manager keeps `Saved/AgentLogs/teams/<dept>.md` for what the board can't hold: the objective's plan, decisions and their reasons, open questions and risks. Update it after every plan, review and close. A fresh manager resumes from `board.py resume` + `board.py tree <obj>` + this log + the task packets. The lead reads the board and your log, not your transcript.

## 5. Standards every department shares
- Follow CLAUDE.md (golden rules, units, naming, data-driven, multiplayer-ready) and the specs in docs/specs/.
- Definition of done: meets the acceptance criteria, reviewed, evidence attached, docs updated (spec, CODEMAP when files are added, TEST_PLAN for tests), committed on the lane with a clear message.
- Consistency: reuse existing patterns, helpers and names before inventing new ones. Record any new convention in your handbook.
- Efficiency: the right level for each task (never senior for junior work); lean packets; no duplicate reading across tasks when one agent can carry the context.
- Honesty: report failures and partial results as they are, with the evidence.
- One severity scale studio-wide: blocker / major / minor / trivial (definitions in docs/teams/qa.md). Priority P0-P3 (docs/teams/design.md) is proposed by managers and set by the lead.
- Progress: start every Bash/PowerShell description and every short text line with `[NN%]` (Jimmy).

## 6. Questions and escalation
- Ask the lead, never Jimmy. Batch your questions, each with your recommended answer and why.
- The lead answers what it can decide. Taste, scope, priorities, or anything the lead is not confident about goes to Jimmy, one plain question at a time (Jimmy, 2026-09-24). Carry on with the rest of the plan while you wait.
- Escalate at once: a blocker, a failure of the same task after 2 tries, a change to the scope or schedule, anything that touches another department's files, or anything risky or irreversible (deleting, history, big refactors).

## 7. Reporting format to the lead (10 lines or fewer)
`[NN%] <objective id>: <one-line status>`, then: done so far (ids and commits), in flight (ids, agents), blockers and questions (with your recommendation), what's needed from the lead (an editor booking, integration, a decision), and the path to your team log.

## 8. The board (the single source of task status; spec docs/tools/board.md)
- Always call the main copy: `python C:/GameDev/VibeGame/tools/board.py <cmd>` (from lanes too). Add `--as <dept>-mgr` on manager writes.
- Read: `ls [--dept D] [--status S]`, `next --dept D` (what to dispatch now), `show <id|key>`, `tree <obj>`, `resume` (after a restart), `conflicts`.
- Write only when something changes: `new`, `set <id> k=v ...` (status moves are checked; an error prints the command that works), `note <id> "..."`, `claim`, and `batch` for many writes in one go.
- Flow: `todo -> ready -> doing -> review -> merge -> done` (or `blocked` with a note, `dropped` with a note). Workers only `set <id> status=review note="..."` when they finish (and `pct=` at real milestones). Managers move the rest; the lead's integration marks `done`.
- The board holds pointers (packet, handoff, commit, evidence); the text stays in files.


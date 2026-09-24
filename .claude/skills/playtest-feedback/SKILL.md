---
name: playtest-feedback
description: How Jimmy's playtest feedback is captured and turned into work - the in-game feedback key (F8) specification, where notes and screenshots land (Saved/Playtest), and how to triage notes into docs/TASKS.md. Use whenever Jimmy talks about playtesting, feel, fun, or bugs he saw, and when building or changing the feedback capture feature.
---
# Playtest feedback

## Feedback key specification (task T-003; build it early in the vertical slice)
- Development builds and Play-In-Editor only (compile out of Shipping).
- Press F8 during play (always: even if the game viewport isn't focused, and it must never reach the editor's own F8 eject): capture a screenshot first, then pause and show a note box in the lower third (a 3-line wrapping field; Enter saves, Shift+Enter = newline). Empty text + Enter = a screenshot-only bookmark. After saving, show a palette toast for ~7 s ("Note saved. Thanks!").
- Enter saves a folder `Saved/Playtest/<yyyyMMdd-HHmmss>/` with `screenshot.png` and `note.json`: text, level name, player location and rotation, camera location and rotation, game time, average FPS over the last 5 seconds, build/commit id if available. Escape cancels. The game resumes either way.
- Implementation: C++ (a small subsystem or player-controller component plus a minimal UMG widget created in C++ or a thin WBP child). Cover the file writing with an automation test.

## Agent test notes are never real feedback
- Agents (playtester, QA) that exercise F8 must type text starting with `[AGENT TEST]` and obviously dummy filler, NEVER realistic made-up feedback about game content (it confuses Jimmy and pollutes triage).
- After an agent session, the agent moves its note folders out of `Saved/Playtest/` into its report folder (`Saved/AgentLogs/playtest/<run>/notes/`). `Saved/Playtest/` holds only Jimmy's notes.
- Triage skips any note whose text starts with `[AGENT TEST]` and flags any other note that wasn't from a Jimmy session.

## Jimmy's written feedback file
- Per playtest round the lead makes `Playtest/<yyyy-MM-dd> <milestone> playtest feedback.txt` (tracked in git): one section per feature with what to try, plus a controls reminder. Jimmy writes in it while he plays.
- The lead reads it in full when Jimmy says he's done playing, and triages it with the F8 notes below. Commit his edits as they are ("Jimmy's A2 playtest notes"). Agents never edit these files.

## Triage loop (run when Jimmy says he played, or when new folders exist)
0. Read the current `Playtest/*feedback.txt` file.
1. List folders in `Saved/Playtest/` newer than the last triage (record the last processed folder in `docs/TASKS.md`).
2. For each note: look at the screenshot, read the note and context. Classify: bug / feel / content / idea.
3. Add or update a task in `docs/TASKS.md` referencing the note folder. For "feel" notes, propose a concrete tuning change (which data value, from what to what) and explain it in one sentence.
4. Reply to Jimmy in plain language: what you understood, what you will change, and what to try in the next session. Ask at most one question.

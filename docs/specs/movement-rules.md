# First-person movement rules (T-004), lead decisions 2026-09-22

Answers to the qa-engineer's T-004 test-design questions (Saved/AgentLogs/qa/eng1-T004-test-design.md, sections 2 and 5-6). Binding for the implementation and the tests. Feel defaults are data (DT_Movement or settings), so Jimmy's playtest feedback is a data edit.

## Decisions
- **A19, missing or invalid table:** log a Warning (not an Error) and use built-in fallback rows. In lanes the asset never exists; an Error would fail every test.
- **A21, input actions by name:** a static BlueprintCallable lookup (Python-callable) returns the SAME UInputAction object the mapping context uses, plus access to the default mapping context. The playtester and T-025 drive input through it (no /Game/Input assets for these actions).
- **A7, EyeHeight:** measured from the feet (floor contact) in the data. Convert internally to the engine's capsule-relative eye height.
- **A1, sprint by stance:** from crouch, sprint stands you up first if there's headroom, then sprints; from prone, sprint is ignored. The server enforces both.
- **A4/A5, controls (feel defaults, on Jimmy's playtest list):** Sprint = hold; Crouch = toggle; Prone = toggle. Keep them switchable via settings. A blocked stand-up (low ceiling) keeps the current stance and stays queued; it completes automatically when there's room, and any other stance input cancels the queue.
- **A12, level-design rule:** crawl-only gaps sit between the prone and crouch clearances; the standard crawl gap is 60 cm. T-005 and the dev map follow it.
- **A13/A14, jumping:** no jumping while prone is a hard rule in code (and a data check). Jumping from crouch defaults to ALLOWED (playful exploration; CanJump per stance in DT_Movement; on Jimmy's playtest list). Walking off ledges while crouched is allowed (docks).
- **A18, other players:** approve a cheap placeholder body visible to others only (OwnerNoSee), e.g. an engine basic-shape capsule/cylinder in the palette's sleeve color, so 2-player PIE shows other players. A real body comes later.
- **A28, dev map:** `/Game/Maps/Dev/L_Dev_Movement` (editor-operator, after the merge): floor, the 60/61/59 cm crawl gaps, ceilings 1 cm below and 3 cm above the stand and crouch clearances, a ledge, stairs, a ramp. It uses ALureGameMode (no world-settings override).

- **Prone fishing (Jimmy, 2026-09-22):** allowed. Arms/rod pose by stance + motion: prone and moving = tucked rod (never clips the 60 cm crawl ceiling or walls); prone and still = prone hold pose (casting/reeling allowed; wired in T-006/T-007). The selection is data-driven per stance.

## Engine pitfalls to handle (each has a QA test)
- Un-crouch restores the class's built-in capsule height, not DT_Movement's Stand height: apply the table's stand height explicitly.
- The engine raises half-height to at least the radius, which can make prone too tall for 60 cm: pick the prone radius and half-height so the total height <= 55 cm.
- The stand-up check keeps the current radius: check headroom with the TARGET stance's radius as well as its height.
- By default you can't jump or walk off ledges while crouched: set these deliberately per the decisions above.
- Crouching moves the character (mesh/capsule offset): smooth the camera so it doesn't pop.
- The eye point the AI uses (GetActorEyesViewPoint / BaseEyeHeight) must follow the stance, so a prone player can hide from sight lines later (T-016).

## Seams the tests need (S1-S13 in the QA design, section 2)
Stance and IsSprinting getters; the public request functions the input handlers call; a public or testable UpdateFromCompressedFlags path; a table-injection function (apply rows) or a settings pointer read at BeginPlay; access to the resolved rows and the fallback rows; camera/arms accessors; the arms mesh as a settable property; the replicated stance property names and OnRep; the fallback log category and warning text.

## T-004 playtest fixes (lead decisions 2026-09-23; report Saved/AgentLogs/playtest/20260923-003511-T004-movement)
- **B1, one climb rule (DT_Movement `ClimbMaxHeight`, land rows 100 cm):** a jump gets you onto ledges up to 100 cm
  above where you jumped; 120 cm and higher always stops you (1 m crates are climbable: on Jimmy's playtest list).
  A landing that lifts the feet onto a ledge's edge is refused (no hanging perched below a ledge top); instead, on the
  way down, pressing toward a ledge whose top is within the rule and within the capsule radius above the feet pulls you
  up onto it (`ClimbSpeed`, 400 cm/s; MOVE_Custom LedgeClimb, predicted). Swimming uses the same columns from the water
  surface (60 cm). Tests: `Project.Movement.Climb.*`.
- **B2, getting up next to walls:** `MaxStanceNudge` 14 cm, and never less than sqrt(2) x the radius difference + 1 cm
  (a corner), so prone flush against a wall or in a corner can always stand or crouch.
- **B3, prone arms:** DT_Movement `ArmsPullBack` (Prone 12 cm) moves the arms toward the eye so the hands stay out of a
  wall the prone capsule touches and beyond the near clip.
- **Feel (data):** Stand bob 1.5 / 1.0, Sprint bob 3.0 / 1.8 / pitch 1.5; `ExitTransitionTime` (Prone 0.42 s) times the
  camera when leaving a state (getting up from prone). Sprint-into-prone braking unchanged (Jimmy's call).
- **Crawling off a ledge:** while falling with the prone wish kept, the capsule grows upward from the feet and the camera
  stays at the prone eye height, then you land prone (no 90 cm rise and 130 cm drop).

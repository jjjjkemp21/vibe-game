// Lure: the reel fight simulation (T-007). Pure: no world, no UObject creation, no global RNG, so the server runs it and
// tests drive it step by step. Rules and numbers: docs/specs/reel-fight-rules.md; tuning in DT_FishFight, DT_FightPattern, DT_Gear.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Fish/FishInstance.h"
#include "Fish/FishTypes.h"
#include "Fishing/FishFightTypes.h"

/** What the fight reads from one fish: its FFishInstance final stats through DT_FishFight, plus the level hook. */
struct FLureFightFish
{
	/** Pull at full stamina in a Pull = 1 move, tension units: strength stat x PullPerStrength x LevelMultiplier. */
	float BasePull = 1.f;
	/** Swim speed at full stamina in a Speed = 1 move, cm/s: speed stat x SpeedPerStat. */
	float BaseSpeed = 0.f;
	/** Tension-seconds of work before the fish is spent: stamina stat x StaminaPerStat. */
	float StaminaPool = 1.f;
	/** The aggression stat (extra pick weight for the pattern's aggressive moves). */
	float Aggression = 0.f;
	/** Rest move durations are multiplied by this: DifficultyRating ^ -RestDifficultyExponent. */
	float RestScale = 1.f;
	/** UFishSettings::LevelScaling for (fish level, player level); 1 if DT_FishFight ApplyLevelScaling is off. */
	float LevelMultiplier = 1.f;
	/** The instance's DifficultyRating (1 = an average fish of its species). */
	float DifficultyRating = 1.f;
};

/**
 *  The player's input to one step. The defaults are the neutral rod (level, centered, the default reel step): with them the
 *  fight is exactly the T-007 fight. The server clamps whatever a client sends (FLureFight::SanitizeInput).
 */
struct FLureFightInput
{
	bool bReeling = false;

	/** T-028: rod pitch, -1 = fully dipped toward the fish, 0 = level, +1 = pulled fully back/up. */
	float RodPitch = 0.f;

	/** T-028: rod yaw relative to the line, -1 = fully left, 0 = centered, +1 = fully right. */
	float RodYaw = 0.f;

	/** T-028: reel speed step, 0-based; INDEX_NONE = the tuning's default step (ReelDefaultStep). */
	int32 ReelStep = INDEX_NONE;
};

/**
 *  What the rod input does in one step (FLureFight::RodFactors). Every field is a multiplier (Side excepted); the defaults are
 *  the neutral rod, and multiplying by them changes nothing (so the T-007 formulas are the neutral case, bit for bit).
 */
struct FLureRodFactors
{
	/** Pitch pressure: x the tension target (and x the rod's power through Power). 1 + p x PitchBackPressure, or 1 + p x PitchDipPressure. */
	float Pressure = 1.f;

	/** Side score: +1 = rod fully against the fish's sideways run, -1 = fully with it, 0 = centered or no sideways run. */
	float Side = 0.f;

	/**
	 *  x the rod's power (line gain and line taken while reeling): PitchPower x (1 + Side x SideLeverage). T-028b: pulled back the
	 *  rod's power follows the pressure; dipped it drops by PitchDipPower (more than the tension's relief), so a dipped rod relieves
	 *  the line but barely works the fish.
	 */
	float Power = 1.f;

	/** Reel step: x the rod's ReelSpeed, and x the rod's cranking load (RodPower x ReelLoad) in the reeling tension. */
	float ReelSpeed = 1.f;
	float ReelLoad = 1.f;

	/** Turning (only against the run): x the fish's pull, x the speed of its move's clock, x its stamina drain. */
	float Pull = 1.f;
	float MoveClock = 1.f;
	float Drain = 1.f;
};

/** One fight in progress (server). Begin() fills it; Step()/Advance() move it on. */
struct FLureFightState
{
	// ---- Setup (copied in, so the state is self-contained) ----
	FLureFightFish Fish;
	FLureGearStats Gear;
	FLureFishFightRow Tuning;
	FLureFightPatternRow Pattern;
	FName PatternId;
	FRandomStream Rng;

	// ---- Simulation ----
	/** Horizontal distance of the fish from the player, cm. */
	float LineOut = 0.f;
	/** Line tension, tension units. */
	float Tension = 0.f;
	/** 0..1. */
	float Stamina = 1.f;
	/** Seconds the tension has stayed above the line's strength / below the slack tension (reset when it stops). */
	float OverTime = 0.f;
	float SlackTime = 0.f;
	/** Cosmetic: depth (cm) and sideways swing (degrees). */
	float Depth = 0.f;
	float SideDeg = 0.f;
	/** Pull and swim speed of the last step. */
	float Pull = 0.f;
	float Speed = 0.f;
	/** Seconds simulated, and the unsimulated remainder of Advance(). */
	float Elapsed = 0.f;
	float Accumulator = 0.f;
	int32 Steps = 0;
	/** Current move (index into Pattern.Moves), INDEX_NONE when exhausted. */
	int32 MoveIndex = INDEX_NONE;
	float MoveTimeLeft = 0.f;
	float SideSign = 1.f;
	bool bExhausted = false;
	bool bReeling = false;
	/** T-028: the rod input of the last step (clamped; ReelStep resolved to a real step) and what it did. */
	float RodPitch = 0.f;
	float RodYaw = 0.f;
	int32 ReelStep = 0;
	/** The fish's sideways direction in the last step (-1 left, 0 none, +1 right) and the side score the rod got against it. */
	int32 RunDir = 0;
	float Side = 0.f;
	ELureFightOutcome Outcome = ELureFightOutcome::None;

	bool IsOver() const { return Outcome != ELureFightOutcome::None; }
	/** The move now (null when exhausted). */
	const FLureFightMove* GetMove() const { return Pattern.Moves.IsValidIndex(MoveIndex) ? &Pattern.Moves[MoveIndex] : nullptr; }
	FName GetMoveId() const { const FLureFightMove* Move = GetMove(); return Move ? Move->Id : NAME_None; }
};

/**
 *  The fight, one fixed step (1 / SimRate s) at a time. The rod input (T-028) gives the factors of FLureRodFactors (RodFactors):
 *   Pressure P  = 1 + p x PitchBackPressure (p >= 0) or 1 + p x PitchDipPressure (p < 0), p = RodPitch in [-1, 1]
 *   RunDir      = sign(Move.Side x SideSign) when |Move.Side| >= SideMinShare, else 0 (also 0 when exhausted)
 *   Side S      = clamp(-RodYaw x RunDir, -1, 1)   (+1 = rod fully against the sideways run, -1 = fully with it), S+ = max(0, S)
 *   Rod power R = 1 + p x PitchBackPressure (p >= 0) or 1 + p x PitchDipPower (p < 0)   (T-028b: dipped, the rod barely works the fish)
 *   Power       = R x (1 + S x SideLeverage)          ReelSpeed/ReelLoad from the reel step (ReelStepSpeed, ReelStepLoad)
 *   Turning     = against the run only: Pull x (1 - S+ x SideTurnPull), MoveClock x (1 + S+ x SideTurnRate), Drain x (1 + S+ x SideDrain)
 *  With the neutral rod (p = 0, RodYaw = 0, the default step of speed 1) every factor is 1 and this is the T-007 fight exactly.
 *   1. Move: the move's clock runs MoveClock (of the current move) x dt; when its time is up, pick the next one:
 *      weight_i = max(0, Weight_i + Aggression x AggressionWeight_i), one draw from the fight's RNG (FFishRoll::PickWeightedIndex);
 *      duration random in [Min, Max] (x RestScale for Rest moves); a RandomSide move draws its side. An exhausted fish makes no moves.
 *   2. Fish: StaminaFactor = TiredPull + (1 - TiredPull) x Stamina.
 *        Pull  = BasePull x Move.Pull x StaminaFactor x Turning.Pull   (exhausted: BasePull x TiredPull)
 *        Speed = BaseSpeed x Move.Speed x StaminaFactor          (exhausted: 0); away speed Va = Speed x Move.Away
 *   3. Line (LineOut changes by Taken - Gain per second), RodPower' = RodPower x Power, ReelSpeed' = ReelSpeed x ReelStepSpeed:
 *        reeling:  Gain  = ReelSpeed' x clamp(1 - Pull / RodPower', 0, 1)
 *                  Taken = Va x clamp(Pull / RodPower' - 1, 0, 1)          (Va <= 0: Taken = Va, the fish comes toward you)
 *        not:      Gain  = 0
 *                  Taken = Va x clamp((Pull / Drag - DragHold) / (1 - DragHold), 0, 1)   (Va <= 0: Taken = Va)
 *   4. Tension eases toward its target with time constant TensionRiseTime (rising) / TensionFallTime (falling):
 *        reeling:  Target = (Pull x ReelStrain + RodPower x ReelLoad x ReelStepLoad) x P
 *        not:      Target = min(Pull x P, Drag)      (letting it run never goes over the drag, whatever the rod does)
 *   5. Stamina: the fish spends Tension x dt x Turning.Drain of its pool; while the line is slack (IsSlack) it regains
 *      StaminaRecovery x pool per second. At ExhaustedStamina it is exhausted for good.
 *      Cosmetic: the sideways swing SideDeg moves by Speed x Side x SideSign x dt / radius x (1 - 2 x S+) (a turned fish swings back).
 *   6. Outcome (first that applies): LineOut <= LandDistance -> Landed; LineOut > SpoolLength -> Spooled;
 *      Tension > LineStrength for longer than SnapGraceTime -> Snapped; the line slack (IsSlack) for longer than
 *      SlackGraceTime x HookSecurity -> ThrewHook. The timers reset when their condition stops.
 *  Slack (one rule for the hook timer, the stamina recovery and the HUD; T-028b): NOT reeling and Tension < SlackTension
 *  (= SlackShare x BasePull). Reeling always takes up slack, whatever the rod and the reel step do.
 *  Same fish, gear, tuning, pattern, seed and inputs = the same fight on every run.
 */
struct FLureFight
{
	/** Reads the fish's stats (tags from Tuning), the level hook (Scaling, fish level vs PlayerLevel) and DifficultyRating. */
	static FLureFightFish MakeFish(const FFishInstance& Fish, const FLureFishFightRow& Tuning, int32 PlayerLevel, const FFishLevelScaling& Scaling);

	static float StaminaFactor(const FLureFishFightRow& Tuning, float Stamina01);

	/** Pull of the fish in Move (null = exhausted). */
	static float FishPull(const FLureFightFish& Fish, const FLureFightMove* Move, float Stamina01, const FLureFishFightRow& Tuning);

	/** Swim speed of the fish in Move (null = exhausted: 0), cm/s. */
	static float FishSpeed(const FLureFightFish& Fish, const FLureFightMove* Move, float Stamina01, const FLureFishFightRow& Tuning);

	/** The tension the line heads for (step 4). Rod = the rod input's factors (default: the neutral rod). */
	static float TargetTension(float Pull, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning, const FLureRodFactors& Rod = FLureRodFactors());

	/** Line reeled in per second (step 3), cm/s. */
	static float LineGainSpeed(float Pull, bool bReeling, const FLureGearStats& Gear, const FLureRodFactors& Rod = FLureRodFactors());

	/** Line the fish takes per second (step 3; negative when it swims toward you), cm/s. AwaySpeed = Speed x Move.Away. */
	static float LineTakenSpeed(float Pull, float AwaySpeed, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning,
		const FLureRodFactors& Rod = FLureRodFactors());

	// ---- Rod steering (T-028) ----

	/** Clamps a client's rod input: pitch and yaw into [-1, 1] (non-finite = 0), the reel step to a real step (INDEX_NONE = default). */
	static FLureFightInput SanitizeInput(const FLureFightInput& Input, const FLureFishFightRow& Tuning);

	/** Pitch pressure P for a rod pitch in [-1, 1] (non-finite = level). */
	static float PitchPressure(float RodPitch, const FLureFishFightRow& Tuning);

	/** T-028b: the pitch's share of the rod's power: 1 + p x PitchBackPressure (back) or 1 + p x PitchDipPower (dipped). */
	static float PitchPower(float RodPitch, const FLureFishFightRow& Tuning);

	/** The fish's sideways direction in Move with its random side: -1 left, +1 right, 0 none (below SideMinShare, or no move). */
	static int32 RunDirection(const FLureFightMove* Move, float SideSign, const FLureFishFightRow& Tuning);

	/** Side score of a rod yaw against a run direction: clamp(-RodYaw x RunDir, -1, 1). */
	static float SideScore(float RodYaw, int32 RunDir);

	/** Number of reel steps (ReelSteps, at least 1) and the default one, 0-based. */
	static int32 NumReelSteps(const FLureFishFightRow& Tuning);
	static int32 DefaultReelStep(const FLureFishFightRow& Tuning);

	/** A reel step clamped into range; INDEX_NONE (or any negative) = the default step. */
	static int32 ClampReelStep(int32 Step, const FLureFishFightRow& Tuning);

	/** Speed of a step (x the rod's ReelSpeed): ReelSpeedMin .. ReelSpeedMax evenly; a single step is 1. */
	static float ReelStepSpeed(int32 Step, const FLureFishFightRow& Tuning);

	/** Cranking load of a step (x RodPower x ReelLoad): max(0, 1 + (speed - 1) x ReelLoadPerSpeed). */
	static float ReelStepLoad(int32 Step, const FLureFishFightRow& Tuning);

	/** Everything the rod input does in a step while the fish makes Move (null = exhausted) with its SideSign. */
	static FLureRodFactors RodFactors(const FLureFightInput& Input, const FLureFightMove* Move, float SideSign, const FLureFishFightRow& Tuning);

	/** One easing step of the tension toward Target. */
	static float EaseTension(float Current, float Target, float DeltaTime, const FLureFishFightRow& Tuning);

	/** Below this the line is slack: SlackShare x BasePull (at least 0.01). */
	static float SlackTension(const FLureFightFish& Fish, const FLureFishFightRow& Tuning);

	/** T-028b: the one slack rule: not reeling and the tension below SlackTension (reeling always takes up slack). */
	static bool IsSlack(float Tension, bool bReeling, const FLureFightFish& Fish, const FLureFishFightRow& Tuning);

	/** Seconds of slack before the fish throws the hook: SlackGraceTime x HookSecurity. */
	static float SlackGrace(const FLureGearStats& Gear, const FLureFishFightRow& Tuning);

	/** Pick weight of a move for a fish with this aggression: max(0, Weight + Aggression x AggressionWeight). */
	static float MoveWeight(const FLureFightMove& Move, float Aggression);

	/** The move a uniform draw U in [0, 1) picks (INDEX_NONE if no move has weight). */
	static int32 PickMove(const FLureFightPatternRow& Pattern, float Aggression, float U);

	/** Seed of a fish's fight: HashCombine(uint32(FishSeed), 7), so a fish record replays the same fight. */
	static int32 FightSeed(int32 FishSeed);

	/** Seconds per fixed step (1 / SimRate). */
	static float StepSeconds(const FLureFishFightRow& Tuning);

	/** Starts a fight: StartLineOut away, full stamina, no tension, the pattern's OpeningMove (or a random pick). */
	static void Begin(FLureFightState& Out, const FLureFightFish& Fish, const FLureFightPatternRow& Pattern, FName PatternId,
		const FLureGearStats& Gear, const FLureFishFightRow& Tuning, int32 Seed, float StartLineOut);

	/** One fixed step. Returns the outcome (None = still on). After an outcome it does nothing. */
	static ELureFightOutcome Step(FLureFightState& State, const FLureFightInput& Input);

	/** Runs the whole fixed steps that fit in DeltaTime (+ the remainder carried over; at most MaxStepsPerAdvance). */
	static ELureFightOutcome Advance(FLureFightState& State, const FLureFightInput& Input, float DeltaTime);

	/** A long hitch never simulates more than this many steps at once (the rest is dropped). */
	static constexpr int32 MaxStepsPerAdvance = 30;

	// ---- Cosmetic helpers (every machine) ----

	/** Placeholder rod bend: pitch toward the fish (negative = tip forward/down), plus a shake while over the line's strength. */
	static float RodPitch(float Tension01, float TimeSeconds, const FLureFishFightRow& Tuning);

	/** Line sag while a fish is on: BaseSag x clamp(1 - Tension01 / TautTension, 0, 1) (taut under load, sagging when slack). */
	static float LineSag(float BaseSag, float Tension01, const FLureFishFightRow& Tuning);
};

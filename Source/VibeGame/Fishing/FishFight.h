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

/** The player's input to one step. */
struct FLureFightInput
{
	bool bReeling = false;
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
	ELureFightOutcome Outcome = ELureFightOutcome::None;

	bool IsOver() const { return Outcome != ELureFightOutcome::None; }
	/** The move now (null when exhausted). */
	const FLureFightMove* GetMove() const { return Pattern.Moves.IsValidIndex(MoveIndex) ? &Pattern.Moves[MoveIndex] : nullptr; }
	FName GetMoveId() const { const FLureFightMove* Move = GetMove(); return Move ? Move->Id : NAME_None; }
};

/**
 *  The fight, one fixed step (1 / SimRate s) at a time:
 *   1. Move: when the current move's time is up, pick the next one: weight_i = max(0, Weight_i + Aggression x AggressionWeight_i),
 *      one draw from the fight's RNG (FFishRoll::PickWeightedIndex); duration random in [Min, Max] (x RestScale for Rest moves).
 *      An exhausted fish makes no moves.
 *   2. Fish: StaminaFactor = TiredPull + (1 - TiredPull) x Stamina.
 *        Pull  = BasePull x Move.Pull x StaminaFactor            (exhausted: BasePull x TiredPull)
 *        Speed = BaseSpeed x Move.Speed x StaminaFactor          (exhausted: 0); away speed Va = Speed x Move.Away
 *   3. Line (LineOut changes by Taken - Gain per second):
 *        reeling:  Gain  = ReelSpeed x clamp(1 - Pull / RodPower, 0, 1)
 *                  Taken = Va x clamp(Pull / RodPower - 1, 0, 1)          (Va <= 0: Taken = Va, the fish comes toward you)
 *        not:      Gain  = 0
 *                  Taken = Va x clamp((Pull / Drag - DragHold) / (1 - DragHold), 0, 1)   (Va <= 0: Taken = Va)
 *   4. Tension eases toward its target with time constant TensionRiseTime (rising) / TensionFallTime (falling):
 *        reeling:  Target = Pull x ReelStrain + RodPower x ReelLoad
 *        not:      Target = min(Pull, Drag)
 *   5. Stamina: the fish spends Tension x dt of its pool; while the line is slack it regains StaminaRecovery x pool per second.
 *      At ExhaustedStamina it is exhausted for good.
 *   6. Outcome (first that applies): LineOut <= LandDistance -> Landed; LineOut > SpoolLength -> Spooled;
 *      Tension > LineStrength for longer than SnapGraceTime -> Snapped; Tension < SlackTension (= SlackShare x BasePull) for
 *      longer than SlackGraceTime x HookSecurity -> ThrewHook. The timers reset when their condition stops.
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

	/** The tension the line heads for (step 4). */
	static float TargetTension(float Pull, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning);

	/** Line reeled in per second (step 3), cm/s. */
	static float LineGainSpeed(float Pull, bool bReeling, const FLureGearStats& Gear);

	/** Line the fish takes per second (step 3; negative when it swims toward you), cm/s. AwaySpeed = Speed x Move.Away. */
	static float LineTakenSpeed(float Pull, float AwaySpeed, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning);

	/** One easing step of the tension toward Target. */
	static float EaseTension(float Current, float Target, float DeltaTime, const FLureFishFightRow& Tuning);

	/** Below this the line is slack: SlackShare x BasePull (at least 0.01). */
	static float SlackTension(const FLureFightFish& Fish, const FLureFishFightRow& Tuning);

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

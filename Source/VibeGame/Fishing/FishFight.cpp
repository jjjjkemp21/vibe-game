// Lure: the reel fight simulation (T-007).

#include "Fishing/FishFight.h"
#include "Fish/FishRoll.h"
#include "Templates/TypeHash.h"

namespace LureFishFightPrivate
{
	float Finite(float Value, float Fallback = 0.f)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	/** A rod axis value in [-1, 1]; anything non-finite is the neutral 0. */
	float ClampUnit(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.f, 1.f) : 0.f;
	}

	/**
	 *  "Longer than Grace": the timers grow by whole fixed steps, so count those steps (float sums drift: 36 x 1/60 is
	 *  0.600000083 > 0.6) and compare with the grace in steps (a tiny tolerance absorbs the grace's own float rounding).
	 *  Over for exactly the grace holds; one step more ends it.
	 */
	bool IsLongerThanGrace(float Time, float Grace, const FLureFishFightRow& Tuning)
	{
		const double Rate = static_cast<double>(FMath::Clamp(Tuning.SimRate, 10, 240));
		const double StepsOver = FMath::RoundToDouble(static_cast<double>(Time) * Rate);
		return StepsOver > static_cast<double>(FMath::Max(0.f, Grace)) * Rate + 1.0e-4;
	}

	void StartMove(FLureFightState& State, int32 Index)
	{
		State.MoveIndex = State.Pattern.Moves.IsValidIndex(Index) ? Index : INDEX_NONE;
		const float Step = FLureFight::StepSeconds(State.Tuning);
		if (State.MoveIndex == INDEX_NONE)
		{
			State.MoveTimeLeft = 1.f;
			return;
		}
		const FLureFightMove& Move = State.Pattern.Moves[State.MoveIndex];
		const float Min = FMath::Max(Step, Finite(Move.DurationMin, 1.f));
		const float Max = FMath::Max(Min, Finite(Move.DurationMax, Min));
		float Duration = Min + (Max - Min) * State.Rng.GetFraction();
		if (Move.Rest)
		{
			Duration *= State.Fish.RestScale;
		}
		State.MoveTimeLeft = FMath::Max(Step, Duration);
		State.SideSign = Move.RandomSide ? (State.Rng.GetFraction() < 0.5f ? -1.f : 1.f) : 1.f;
	}
}

FLureFightFish FLureFight::MakeFish(const FFishInstance& Fish, const FLureFishFightRow& Tuning, int32 PlayerLevel, const FFishLevelScaling& Scaling)
{
	using LureFishFightPrivate::Finite;
	auto Stat = [&Fish](const FGameplayTag& Tag)
	{
		return Tag.IsValid() ? FMath::Max(0.f, Finite(Fish.GetStat(Tag, 0.f))) : 0.f;
	};
	FLureFightFish Out;
	Out.LevelMultiplier = Tuning.ApplyLevelScaling ? Finite(Scaling.GetMultiplier(Fish.Level, PlayerLevel), 1.f) : 1.f;
	Out.BasePull = FMath::Max(0.05f, Stat(Tuning.StrengthStat) * FMath::Max(0.f, Finite(Tuning.PullPerStrength)) * Out.LevelMultiplier);
	Out.BaseSpeed = Stat(Tuning.SpeedStat) * FMath::Max(0.f, Finite(Tuning.SpeedPerStat));
	Out.StaminaPool = FMath::Max(0.5f, Stat(Tuning.StaminaStat) * FMath::Max(0.f, Finite(Tuning.StaminaPerStat)));
	Out.Aggression = Stat(Tuning.AggressionStat);
	Out.DifficultyRating = (FMath::IsFinite(Fish.DifficultyRating) && Fish.DifficultyRating > 0.f) ? Fish.DifficultyRating : 1.f;
	const float Exponent = FMath::Max(0.f, Finite(Tuning.RestDifficultyExponent));
	Out.RestScale = FMath::Clamp(FMath::Pow(FMath::Max(0.25f, Out.DifficultyRating), -Exponent), 0.25f, 4.f);
	return Out;
}

float FLureFight::StaminaFactor(const FLureFishFightRow& Tuning, float Stamina01)
{
	const float Tired = FMath::Clamp(LureFishFightPrivate::Finite(Tuning.TiredPull), 0.f, 1.f);
	return Tired + (1.f - Tired) * FMath::Clamp(LureFishFightPrivate::Finite(Stamina01), 0.f, 1.f);
}

float FLureFight::FishPull(const FLureFightFish& Fish, const FLureFightMove* Move, float Stamina01, const FLureFishFightRow& Tuning)
{
	if (!Move)
	{
		return Fish.BasePull * FMath::Clamp(LureFishFightPrivate::Finite(Tuning.TiredPull), 0.f, 1.f);
	}
	return Fish.BasePull * FMath::Max(0.f, LureFishFightPrivate::Finite(Move->Pull)) * StaminaFactor(Tuning, Stamina01);
}

float FLureFight::FishSpeed(const FLureFightFish& Fish, const FLureFightMove* Move, float Stamina01, const FLureFishFightRow& Tuning)
{
	if (!Move)
	{
		return 0.f;
	}
	return Fish.BaseSpeed * FMath::Max(0.f, LureFishFightPrivate::Finite(Move->Speed)) * StaminaFactor(Tuning, Stamina01);
}

float FLureFight::TargetTension(float Pull, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning, const FLureRodFactors& Rod)
{
	using LureFishFightPrivate::Finite;
	const float SafePull = FMath::Max(0.f, Finite(Pull));
	const float Pressure = FMath::Max(0.f, Finite(Rod.Pressure, 1.f));
	if (bReeling)
	{
		// The rod's cranking load grows with the reel step; the rod's angle scales everything you apply (T-028).
		return (SafePull * FMath::Max(0.f, Tuning.ReelStrain) + FMath::Max(0.f, Gear.RodPower) * FMath::Max(0.f, Tuning.ReelLoad) * FMath::Max(0.f, Finite(Rod.ReelLoad, 1.f)))
			* Pressure;
	}
	// Letting it run: the drag slips at its setting, whatever the rod does (so letting it run never snaps the line).
	return FMath::Min(SafePull * Pressure, FMath::Max(0.f, Gear.Drag));
}

float FLureFight::LineGainSpeed(float Pull, bool bReeling, const FLureGearStats& Gear, const FLureRodFactors& Rod)
{
	const float Power = Gear.RodPower * FMath::Max(0.f, LureFishFightPrivate::Finite(Rod.Power, 1.f));
	const float Speed = Gear.ReelSpeed * FMath::Max(0.f, LureFishFightPrivate::Finite(Rod.ReelSpeed, 1.f));
	if (!bReeling || !(Power > 0.f) || !(Speed > 0.f))
	{
		return 0.f;
	}
	return Speed * FMath::Clamp(1.f - FMath::Max(0.f, Pull) / Power, 0.f, 1.f);
}

float FLureFight::LineTakenSpeed(float Pull, float AwaySpeed, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning, const FLureRodFactors& Rod)
{
	const float Away = LureFishFightPrivate::Finite(AwaySpeed);
	if (Away <= 0.f)
	{
		return Away; // holding still, or swimming toward the player: the line shortens by itself
	}
	const float SafePull = FMath::Max(0.f, LureFishFightPrivate::Finite(Pull));
	if (bReeling)
	{
		// Only a fish that out-pulls the rod takes line while you crank (fully at twice the rod's power). The rod's angle and
		// side pressure change that power (T-028).
		const float Power = Gear.RodPower * FMath::Max(0.f, LureFishFightPrivate::Finite(Rod.Power, 1.f));
		return Power > 0.f ? Away * FMath::Clamp(SafePull / Power - 1.f, 0.f, 1.f) : Away;
	}
	if (!(Gear.Drag > 0.f))
	{
		return Away; // no drag: free spool
	}
	const float Hold = FMath::Clamp(Tuning.DragHold, 0.f, 0.99f);
	return Away * FMath::Clamp((SafePull / Gear.Drag - Hold) / (1.f - Hold), 0.f, 1.f);
}

float FLureFight::EaseTension(float Current, float Target, float DeltaTime, const FLureFishFightRow& Tuning)
{
	const float Tau = Target > Current ? Tuning.TensionRiseTime : Tuning.TensionFallTime;
	if (!(Tau > 0.f) || !(DeltaTime > 0.f))
	{
		return DeltaTime > 0.f ? Target : Current;
	}
	return Current + (Target - Current) * (1.f - FMath::Exp(-DeltaTime / Tau));
}

float FLureFight::SlackTension(const FLureFightFish& Fish, const FLureFishFightRow& Tuning)
{
	return FMath::Max(0.01f, FMath::Max(0.f, LureFishFightPrivate::Finite(Tuning.SlackShare)) * Fish.BasePull);
}

bool FLureFight::IsSlack(float Tension, bool bReeling, const FLureFightFish& Fish, const FLureFishFightRow& Tuning)
{
	return !bReeling && Tension < SlackTension(Fish, Tuning);
}

float FLureFight::SlackGrace(const FLureGearStats& Gear, const FLureFishFightRow& Tuning)
{
	return FMath::Max(0.f, Tuning.SlackGraceTime) * FMath::Max(0.f, LureFishFightPrivate::Finite(Gear.HookSecurity));
}

// ---- Rod steering (T-028) ----

FLureFightInput FLureFight::SanitizeInput(const FLureFightInput& Input, const FLureFishFightRow& Tuning)
{
	FLureFightInput Out;
	Out.bReeling = Input.bReeling;
	Out.RodPitch = LureFishFightPrivate::ClampUnit(Input.RodPitch);
	Out.RodYaw = LureFishFightPrivate::ClampUnit(Input.RodYaw);
	Out.ReelStep = ClampReelStep(Input.ReelStep, Tuning);
	return Out;
}

float FLureFight::PitchPressure(float RodPitch, const FLureFishFightRow& Tuning)
{
	const float Pitch = LureFishFightPrivate::ClampUnit(RodPitch);
	return Pitch >= 0.f
		? 1.f + Pitch * FMath::Max(0.f, LureFishFightPrivate::Finite(Tuning.PitchBackPressure))
		: 1.f + Pitch * FMath::Clamp(LureFishFightPrivate::Finite(Tuning.PitchDipPressure), 0.f, 0.95f);
}

float FLureFight::PitchPower(float RodPitch, const FLureFishFightRow& Tuning)
{
	const float Pitch = LureFishFightPrivate::ClampUnit(RodPitch);
	return Pitch >= 0.f
		? 1.f + Pitch * FMath::Max(0.f, LureFishFightPrivate::Finite(Tuning.PitchBackPressure))
		: 1.f + Pitch * FMath::Clamp(LureFishFightPrivate::Finite(Tuning.PitchDipPower), 0.f, 0.95f);
}

int32 FLureFight::RunDirection(const FLureFightMove* Move, float SideSign, const FLureFishFightRow& Tuning)
{
	if (!Move)
	{
		return 0;
	}
	const float Lateral = FMath::Clamp(LureFishFightPrivate::Finite(Move->Side), -1.f, 1.f) * (SideSign < 0.f ? -1.f : 1.f);
	if (Lateral == 0.f || FMath::Abs(Lateral) < FMath::Max(0.f, LureFishFightPrivate::Finite(Tuning.SideMinShare)))
	{
		return 0;
	}
	return Lateral > 0.f ? 1 : -1;
}

float FLureFight::SideScore(float RodYaw, int32 RunDir)
{
	return FMath::Clamp(-LureFishFightPrivate::ClampUnit(RodYaw) * static_cast<float>(FMath::Clamp(RunDir, -1, 1)), -1.f, 1.f);
}

int32 FLureFight::NumReelSteps(const FLureFishFightRow& Tuning)
{
	return FMath::Clamp(Tuning.ReelSteps, 1, 9);
}

int32 FLureFight::DefaultReelStep(const FLureFishFightRow& Tuning)
{
	return FMath::Clamp(Tuning.ReelDefaultStep - 1, 0, NumReelSteps(Tuning) - 1);
}

int32 FLureFight::ClampReelStep(int32 Step, const FLureFishFightRow& Tuning)
{
	return Step < 0 ? DefaultReelStep(Tuning) : FMath::Min(Step, NumReelSteps(Tuning) - 1);
}

float FLureFight::ReelStepSpeed(int32 Step, const FLureFishFightRow& Tuning)
{
	const int32 Num = NumReelSteps(Tuning);
	if (Num <= 1)
	{
		return 1.f;
	}
	const float Min = FMath::Max(0.05f, LureFishFightPrivate::Finite(Tuning.ReelSpeedMin, 1.f));
	const float Max = FMath::Max(Min, LureFishFightPrivate::Finite(Tuning.ReelSpeedMax, Min));
	return Min + (Max - Min) * static_cast<float>(ClampReelStep(Step, Tuning)) / static_cast<float>(Num - 1);
}

float FLureFight::ReelStepLoad(int32 Step, const FLureFishFightRow& Tuning)
{
	return FMath::Max(0.f, 1.f + (ReelStepSpeed(Step, Tuning) - 1.f) * FMath::Max(0.f, LureFishFightPrivate::Finite(Tuning.ReelLoadPerSpeed)));
}

FLureRodFactors FLureFight::RodFactors(const FLureFightInput& Input, const FLureFightMove* Move, float SideSign, const FLureFishFightRow& Tuning)
{
	using LureFishFightPrivate::Finite;
	FLureRodFactors Out;
	Out.Pressure = PitchPressure(Input.RodPitch, Tuning);
	Out.Side = SideScore(Input.RodYaw, RunDirection(Move, SideSign, Tuning));
	Out.Power = PitchPower(Input.RodPitch, Tuning) * (1.f + Out.Side * FMath::Clamp(Finite(Tuning.SideLeverage), 0.f, 0.95f));
	Out.ReelSpeed = ReelStepSpeed(Input.ReelStep, Tuning);
	Out.ReelLoad = ReelStepLoad(Input.ReelStep, Tuning);
	const float Against = FMath::Max(0.f, Out.Side);
	Out.Pull = 1.f - Against * FMath::Clamp(Finite(Tuning.SideTurnPull), 0.f, 0.95f);
	Out.MoveClock = 1.f + Against * FMath::Max(0.f, Finite(Tuning.SideTurnRate));
	Out.Drain = 1.f + Against * FMath::Max(0.f, Finite(Tuning.SideDrain));
	return Out;
}

float FLureFight::MoveWeight(const FLureFightMove& Move, float Aggression)
{
	const float Weight = LureFishFightPrivate::Finite(Move.Weight) + FMath::Max(0.f, LureFishFightPrivate::Finite(Aggression)) * LureFishFightPrivate::Finite(Move.AggressionWeight);
	return FMath::Max(0.f, Weight);
}

int32 FLureFight::PickMove(const FLureFightPatternRow& Pattern, float Aggression, float U)
{
	TArray<float, TInlineAllocator<8>> Weights;
	for (const FLureFightMove& Move : Pattern.Moves)
	{
		Weights.Add(MoveWeight(Move, Aggression));
	}
	return FFishRoll::PickWeightedIndex(Weights, U);
}

int32 FLureFight::FightSeed(int32 FishSeed)
{
	return static_cast<int32>(HashCombine(static_cast<uint32>(FishSeed), 7u));
}

float FLureFight::StepSeconds(const FLureFishFightRow& Tuning)
{
	return 1.f / static_cast<float>(FMath::Clamp(Tuning.SimRate, 10, 240));
}

void FLureFight::Begin(FLureFightState& Out, const FLureFightFish& Fish, const FLureFightPatternRow& Pattern, FName PatternId,
	const FLureGearStats& Gear, const FLureFishFightRow& Tuning, int32 Seed, float StartLineOut)
{
	// Built in a fresh state first, so the inputs may be parts of Out (e.g. restarting a fight with its own pattern).
	FLureFightState Fresh;
	Fresh.Fish = Fish;
	Fresh.Gear = Gear;
	Fresh.Tuning = Tuning;
	Fresh.Pattern = Pattern;
	Fresh.PatternId = PatternId;
	Fresh.Rng.Initialize(Seed);
	Fresh.LineOut = FMath::Max(0.f, LureFishFightPrivate::Finite(StartLineOut));
	Fresh.Stamina = 1.f;
	const int32 Opening = Fresh.Pattern.FindMove(Fresh.Pattern.OpeningMove);
	LureFishFightPrivate::StartMove(Fresh, Opening != INDEX_NONE ? Opening : PickMove(Fresh.Pattern, Fresh.Fish.Aggression, Fresh.Rng.GetFraction()));
	Fresh.Pull = FishPull(Fresh.Fish, Fresh.GetMove(), Fresh.Stamina, Fresh.Tuning);
	Fresh.ReelStep = DefaultReelStep(Fresh.Tuning);
	Fresh.RunDir = RunDirection(Fresh.GetMove(), Fresh.SideSign, Fresh.Tuning);
	Out = MoveTemp(Fresh);
}

ELureFightOutcome FLureFight::Step(FLureFightState& State, const FLureFightInput& Input)
{
	if (State.IsOver())
	{
		return State.Outcome;
	}
	const FLureFishFightRow& Tuning = State.Tuning;
	const FLureGearStats& Gear = State.Gear;
	const float Dt = StepSeconds(Tuning);
	State.Elapsed += Dt;
	++State.Steps;
	// The rod input (T-028), clamped: the server simulates whatever a client sent within the rod's range.
	const FLureFightInput Rod = SanitizeInput(Input, Tuning);
	State.bReeling = Rod.bReeling;
	State.RodPitch = Rod.RodPitch;
	State.RodYaw = Rod.RodYaw;
	State.ReelStep = Rod.ReelStep;

	// 1. Move. Side pressure against the current move's sideways run turns the fish: its clock runs faster.
	if (!State.bExhausted)
	{
		State.MoveTimeLeft -= Dt * RodFactors(Rod, State.GetMove(), State.SideSign, Tuning).MoveClock;
		if (State.MoveTimeLeft <= 0.f)
		{
			LureFishFightPrivate::StartMove(State, PickMove(State.Pattern, State.Fish.Aggression, State.Rng.GetFraction()));
		}
	}
	const FLureFightMove* Move = State.bExhausted ? nullptr : State.GetMove();
	const FLureRodFactors Factors = RodFactors(Rod, Move, State.SideSign, Tuning);
	State.RunDir = RunDirection(Move, State.SideSign, Tuning);
	State.Side = Factors.Side;

	// 2. Fish.
	const float Pull = FishPull(State.Fish, Move, State.Stamina, Tuning) * Factors.Pull;
	const float Speed = FishSpeed(State.Fish, Move, State.Stamina, Tuning);
	const float AwaySpeed = Move ? Speed * FMath::Clamp(Move->Away, -1.f, 1.f) : 0.f;
	State.Pull = Pull;
	State.Speed = Speed;

	// 3. Line.
	const float Gain = LineGainSpeed(Pull, Rod.bReeling, Gear, Factors);
	const float Taken = LineTakenSpeed(Pull, AwaySpeed, Rod.bReeling, Gear, Tuning, Factors);
	State.LineOut = FMath::Max(0.f, State.LineOut + (Taken - Gain) * Dt);

	// 4. Tension.
	State.Tension = FMath::Max(0.f, EaseTension(State.Tension, TargetTension(Pull, Rod.bReeling, Gear, Tuning, Factors), Dt, Tuning));

	// 5. Stamina (a fish being turned tires faster). One slack rule (IsSlack) for the recovery and the hook timer (T-028b).
	const bool bSlack = IsSlack(State.Tension, Rod.bReeling, State.Fish, Tuning);
	const float Pool = FMath::Max(0.01f, State.Fish.StaminaPool);
	float Energy = State.Stamina * Pool - State.Tension * Dt * Factors.Drain;
	if (bSlack)
	{
		Energy += Pool * FMath::Max(0.f, Tuning.StaminaRecovery) * Dt;
	}
	State.Stamina = FMath::Clamp(Energy / Pool, 0.f, 1.f);
	if (!State.bExhausted && State.Stamina <= Tuning.ExhaustedStamina)
	{
		State.bExhausted = true;
		State.MoveIndex = INDEX_NONE;
	}

	// Cosmetic: depth and swing (a fish turned by side pressure swings back toward the middle).
	const float SideBefore = State.SideDeg;
	if (Move)
	{
		State.Depth += Speed * FMath::Clamp(Move->Down, -1.f, 1.f) * Dt;
		const float Radius = FMath::Max(100.f, State.LineOut);
		State.SideDeg += FMath::RadiansToDegrees(Speed * FMath::Clamp(Move->Side, -1.f, 1.f) * State.SideSign * Dt / Radius) * (1.f - 2.f * FMath::Max(0.f, Factors.Side));
	}
	if (!Move || Move->Down <= 0.f)
	{
		State.Depth -= FMath::Max(0.f, Tuning.DepthRecovery) * Dt;
	}
	State.Depth = FMath::Clamp(State.Depth, 0.f, FMath::Max(0.f, Tuning.MaxDepth));
	// The swing limit holds the fish's own swing (T-045): a player who walked round the fish may leave it past the limit, and
	// then it stays where it is (never pulled back in) and only its swing back toward the middle is free. A still player's
	// fish is always within the limit, so this is the old clamp.
	State.SideDeg = FMath::Clamp(State.SideDeg, FMath::Min(-Tuning.MaxSideDeg, SideBefore), FMath::Max(Tuning.MaxSideDeg, SideBefore));

	// 6. Outcome.
	if (State.LineOut <= Tuning.LandDistance)
	{
		State.Outcome = ELureFightOutcome::Landed;
		return State.Outcome;
	}
	if (Gear.SpoolLength > 0.f && State.LineOut > Gear.SpoolLength)
	{
		State.Outcome = ELureFightOutcome::Spooled;
		return State.Outcome;
	}
	if (State.Tension > Gear.LineStrength)
	{
		State.OverTime += Dt;
		if (LureFishFightPrivate::IsLongerThanGrace(State.OverTime, Tuning.SnapGraceTime, Tuning))
		{
			State.Outcome = ELureFightOutcome::Snapped;
			return State.Outcome;
		}
	}
	else
	{
		State.OverTime = 0.f;
	}
	if (bSlack)
	{
		State.SlackTime += Dt;
		if (LureFishFightPrivate::IsLongerThanGrace(State.SlackTime, SlackGrace(Gear, Tuning), Tuning))
		{
			State.Outcome = ELureFightOutcome::ThrewHook;
			return State.Outcome;
		}
	}
	else
	{
		State.SlackTime = 0.f;
	}
	return ELureFightOutcome::None;
}

ELureFightOutcome FLureFight::Advance(FLureFightState& State, const FLureFightInput& Input, float DeltaTime)
{
	if (State.IsOver())
	{
		return State.Outcome;
	}
	const float Dt = StepSeconds(State.Tuning);
	State.Accumulator += FMath::Max(0.f, LureFishFightPrivate::Finite(DeltaTime));
	int32 Budget = MaxStepsPerAdvance;
	// Small epsilon: a frame of exactly one step (1/60 s at 60 Hz) runs one step despite float rounding.
	while (State.Accumulator + 1.0e-5f >= Dt && Budget-- > 0)
	{
		State.Accumulator = FMath::Max(0.f, State.Accumulator - Dt);
		if (Step(State, Input) != ELureFightOutcome::None)
		{
			break;
		}
	}
	if (Budget < 0)
	{
		State.Accumulator = 0.f; // a long hitch: drop the backlog rather than spiral
	}
	return State.Outcome;
}

// ---- The fish in the world (T-045) ----

void FLureFight::PlaceFish(FLureFightState& State, const FVector2D& PlayerXY, const FVector2D& FishXY, const FVector2D& FallbackDir)
{
	auto Finite2 = [](const FVector2D& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y); };
	State.Anchor = Finite2(PlayerXY) ? PlayerXY : FVector2D::ZeroVector;
	const FVector2D Fish = Finite2(FishXY) ? FishXY : State.Anchor;
	const FVector2D Offset = Fish - State.Anchor;
	const double Distance = Offset.Size();
	FVector2D Direction = Distance > UE_KINDA_SMALL_NUMBER ? Offset / Distance : (Finite2(FallbackDir) ? FallbackDir.GetSafeNormal() : FVector2D::ZeroVector);
	State.BaseDir = Direction.IsNearlyZero() ? FVector2D(1.0, 0.0) : Direction;
	State.LineOut = static_cast<float>(Distance);
	State.SideDeg = 0.f;
}

FVector2D FLureFight::FishLocation(const FLureFightState& State)
{
	return State.Anchor + State.BaseDir.GetRotated(static_cast<double>(State.SideDeg)) * static_cast<double>(FMath::Max(0.f, State.LineOut));
}

void FLureFight::MovePlayer(FLureFightState& State, const FVector2D& PlayerXY)
{
	if (PlayerXY == State.Anchor || !FMath::IsFinite(PlayerXY.X) || !FMath::IsFinite(PlayerXY.Y))
	{
		return; // a still player: nothing changes (bit for bit)
	}
	const FVector2D Fish = FishLocation(State);
	State.Anchor = PlayerXY;
	const FVector2D Offset = Fish - PlayerXY;
	const double Distance = Offset.Size();
	State.LineOut = static_cast<float>(Distance);
	if (Distance > UE_KINDA_SMALL_NUMBER)
	{
		// The fish's bearing from the player now, from BaseDir (+ = turned the way FVector2D::GetRotated turns: toward the right).
		const FVector2D Direction = Offset / Distance;
		State.SideDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(FVector2D::CrossProduct(State.BaseDir, Direction), FVector2D::DotProduct(State.BaseDir, Direction))));
	}
}

float FLureFight::RodPitch(float Tension01, float TimeSeconds, const FLureFishFightRow& Tuning)
{
	const float Load = FMath::Clamp(LureFishFightPrivate::Finite(Tension01), 0.f, 1.5f);
	float Pitch = -Tuning.RodTensionPitchDeg * FMath::Min(Load, 1.f);
	if (Load > 1.f)
	{
		Pitch += Tuning.RodShakeDeg * FMath::Sin(LureFishFightPrivate::Finite(TimeSeconds) * 45.f);
	}
	return Pitch;
}

float FLureFight::LineSag(float BaseSag, float Tension01, const FLureFishFightRow& Tuning)
{
	const float Taut = FMath::Max(0.01f, Tuning.TautTension);
	return FMath::Max(0.f, BaseSag) * FMath::Clamp(1.f - FMath::Max(0.f, LureFishFightPrivate::Finite(Tension01)) / Taut, 0.f, 1.f);
}

float FLureFight::LineTension(float Tension01, const FLureFishFightRow& Tuning)
{
	if (FMath::IsNaN(Tension01))
	{
		return 0.f;
	}
	const float Taut = FMath::IsFinite(Tuning.TautTension) ? FMath::Clamp(Tuning.TautTension, 0.01f, 1.f) : 0.3f;
	return Tension01 >= Taut ? 1.f : FMath::Clamp(Tension01 / Taut, 0.f, 1.f); // +Inf >= Taut: 1
}

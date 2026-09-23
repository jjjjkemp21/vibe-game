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

float FLureFight::TargetTension(float Pull, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning)
{
	const float SafePull = FMath::Max(0.f, LureFishFightPrivate::Finite(Pull));
	if (bReeling)
	{
		return SafePull * FMath::Max(0.f, Tuning.ReelStrain) + FMath::Max(0.f, Gear.RodPower) * FMath::Max(0.f, Tuning.ReelLoad);
	}
	return FMath::Min(SafePull, FMath::Max(0.f, Gear.Drag));
}

float FLureFight::LineGainSpeed(float Pull, bool bReeling, const FLureGearStats& Gear)
{
	if (!bReeling || !(Gear.RodPower > 0.f) || !(Gear.ReelSpeed > 0.f))
	{
		return 0.f;
	}
	return Gear.ReelSpeed * FMath::Clamp(1.f - FMath::Max(0.f, Pull) / Gear.RodPower, 0.f, 1.f);
}

float FLureFight::LineTakenSpeed(float Pull, float AwaySpeed, bool bReeling, const FLureGearStats& Gear, const FLureFishFightRow& Tuning)
{
	const float Away = LureFishFightPrivate::Finite(AwaySpeed);
	if (Away <= 0.f)
	{
		return Away; // holding still, or swimming toward the player: the line shortens by itself
	}
	const float SafePull = FMath::Max(0.f, LureFishFightPrivate::Finite(Pull));
	if (bReeling)
	{
		// Only a fish that out-pulls the rod takes line while you crank (fully at twice the rod's power).
		return Gear.RodPower > 0.f ? Away * FMath::Clamp(SafePull / Gear.RodPower - 1.f, 0.f, 1.f) : Away;
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

float FLureFight::SlackGrace(const FLureGearStats& Gear, const FLureFishFightRow& Tuning)
{
	return FMath::Max(0.f, Tuning.SlackGraceTime) * FMath::Max(0.f, LureFishFightPrivate::Finite(Gear.HookSecurity));
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
	State.bReeling = Input.bReeling;

	// 1. Move.
	if (!State.bExhausted)
	{
		State.MoveTimeLeft -= Dt;
		if (State.MoveTimeLeft <= 0.f)
		{
			LureFishFightPrivate::StartMove(State, PickMove(State.Pattern, State.Fish.Aggression, State.Rng.GetFraction()));
		}
	}
	const FLureFightMove* Move = State.bExhausted ? nullptr : State.GetMove();

	// 2. Fish.
	const float Pull = FishPull(State.Fish, Move, State.Stamina, Tuning);
	const float Speed = FishSpeed(State.Fish, Move, State.Stamina, Tuning);
	const float AwaySpeed = Move ? Speed * FMath::Clamp(Move->Away, -1.f, 1.f) : 0.f;
	State.Pull = Pull;
	State.Speed = Speed;

	// 3. Line.
	const float Gain = LineGainSpeed(Pull, Input.bReeling, Gear);
	const float Taken = LineTakenSpeed(Pull, AwaySpeed, Input.bReeling, Gear, Tuning);
	State.LineOut = FMath::Max(0.f, State.LineOut + (Taken - Gain) * Dt);

	// 4. Tension.
	State.Tension = FMath::Max(0.f, EaseTension(State.Tension, TargetTension(Pull, Input.bReeling, Gear, Tuning), Dt, Tuning));

	// 5. Stamina.
	const float Slack = SlackTension(State.Fish, Tuning);
	const float Pool = FMath::Max(0.01f, State.Fish.StaminaPool);
	float Energy = State.Stamina * Pool - State.Tension * Dt;
	if (State.Tension < Slack)
	{
		Energy += Pool * FMath::Max(0.f, Tuning.StaminaRecovery) * Dt;
	}
	State.Stamina = FMath::Clamp(Energy / Pool, 0.f, 1.f);
	if (!State.bExhausted && State.Stamina <= Tuning.ExhaustedStamina)
	{
		State.bExhausted = true;
		State.MoveIndex = INDEX_NONE;
	}

	// Cosmetic: depth and swing.
	if (Move)
	{
		State.Depth += Speed * FMath::Clamp(Move->Down, -1.f, 1.f) * Dt;
		const float Radius = FMath::Max(100.f, State.LineOut);
		State.SideDeg += FMath::RadiansToDegrees(Speed * FMath::Clamp(Move->Side, -1.f, 1.f) * State.SideSign * Dt / Radius);
	}
	if (!Move || Move->Down <= 0.f)
	{
		State.Depth -= FMath::Max(0.f, Tuning.DepthRecovery) * Dt;
	}
	State.Depth = FMath::Clamp(State.Depth, 0.f, FMath::Max(0.f, Tuning.MaxDepth));
	State.SideDeg = FMath::Clamp(State.SideDeg, -Tuning.MaxSideDeg, Tuning.MaxSideDeg);

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
		if (State.OverTime > Tuning.SnapGraceTime)
		{
			State.Outcome = ELureFightOutcome::Snapped;
			return State.Outcome;
		}
	}
	else
	{
		State.OverTime = 0.f;
	}
	if (State.Tension < Slack)
	{
		State.SlackTime += Dt;
		if (State.SlackTime > SlackGrace(Gear, Tuning))
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

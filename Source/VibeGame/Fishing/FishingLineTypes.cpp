// Lure: physics fishing line, tuning row and pure rules (T-032).

#include "Fishing/FishingLineTypes.h"

const TCHAR* FLureFishingLineRules::FallbackMarker = TEXT("using the built-in fishing line tuning");

bool FLureFishingLineRow::Validate(FString& OutProblem) const
{
	const float Values[] = { SubstepRate, GravityScale, AirDrag, WaterDrag, SlackShare, TautExponent, LengthResponse, CastTension,
		WaitTension, BiteTension, HookedTension, FloatStrength, FloatHeight, WaterRefreshDistance, RecoilSpeed, RecoilTime,
		RecoilLengthShare, HangEndMass, HangDrag, TeleportDistance };
	for (const float Value : Values)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			OutProblem = TEXT("every value must be a finite number >= 0");
			return false;
		}
	}
	auto Fail = [&OutProblem](const FString& Problem)
	{
		OutProblem = Problem;
		return false;
	};
	if (SubstepRate < 30.f || SubstepRate > 1000.f)
	{
		return Fail(FString::Printf(TEXT("SubstepRate %.1f must be in [30, 1000]"), SubstepRate));
	}
	if (MaxSubsteps < 1 || MaxSubsteps > 32 || Iterations < 1 || Iterations > 32)
	{
		return Fail(FString::Printf(TEXT("MaxSubsteps %d and Iterations %d must be in [1, 32]"), MaxSubsteps, Iterations));
	}
	if (SlackShare > 1.f || RecoilLengthShare > 1.f || FloatStrength > 1.f)
	{
		return Fail(TEXT("SlackShare, RecoilLengthShare and FloatStrength must be in [0, 1]"));
	}
	if (CastTension > 1.f || WaitTension > 1.f || BiteTension > 1.f || HookedTension > 1.f)
	{
		return Fail(TEXT("the state tensions (Cast/Wait/Bite/HookedTension) must be in [0, 1]"));
	}
	if (TautExponent < 0.1f || TautExponent > 10.f)
	{
		return Fail(FString::Printf(TEXT("TautExponent %.2f must be in [0.1, 10]"), TautExponent));
	}
	if (LengthResponse < 0.1f || RecoilTime < 0.05f || HangEndMass < 1.f || WaterRefreshDistance < 1.f || TeleportDistance < 1.f)
	{
		return Fail(TEXT("LengthResponse >= 0.1, RecoilTime >= 0.05, HangEndMass >= 1, WaterRefreshDistance >= 1 and TeleportDistance >= 1"));
	}
	return true;
}

float FLureFishingLineRules::Tautness(float Tension01, const FLureFishingLineRow& Row)
{
	const float Tension = FMath::IsFinite(Tension01) ? FMath::Clamp(Tension01, 0.f, 1.f) : 0.f;
	const float Exponent = FMath::IsFinite(Row.TautExponent) ? FMath::Clamp(Row.TautExponent, 0.1f, 10.f) : 3.f;
	return FMath::Clamp(1.f - FMath::Pow(1.f - Tension, Exponent), 0.f, 1.f);
}

float FLureFishingLineRules::TargetRestLength(float Chord, float Tension01, float Slack, const FLureFishingLineRow& Row)
{
	const float SafeChord = FMath::IsFinite(Chord) ? FMath::Max(0.f, Chord) : 0.f;
	const float Share = (FMath::IsFinite(Slack) && Slack >= 0.f) ? FMath::Min(Slack, 1.f)
		: (FMath::IsFinite(Row.SlackShare) ? FMath::Clamp(Row.SlackShare, 0.f, 1.f) : 0.f);
	return SafeChord * (1.f + Share * (1.f - Tautness(Tension01, Row)));
}

float FLureFishingLineRules::FollowRestLength(float Current, float Target, float MinLength, float DeltaTime, const FLureFishingLineRow& Row)
{
	const float Floor = FMath::IsFinite(MinLength) ? FMath::Max(0.f, MinLength) : 0.f;
	const float SafeTarget = FMath::IsFinite(Target) ? FMath::Max(Floor, Target) : Floor;
	if (!FMath::IsFinite(Current) || Current <= SafeTarget)
	{
		return SafeTarget; // slack appears at once (or a first frame)
	}
	const float Dt = FMath::IsFinite(DeltaTime) ? FMath::Max(0.f, DeltaTime) : 0.f;
	const float Rate = FMath::IsFinite(Row.LengthResponse) ? FMath::Max(0.1f, Row.LengthResponse) : 6.f;
	const float Alpha = 1.f - FMath::Exp(-Rate * Dt);
	return FMath::Max(SafeTarget, Current + (SafeTarget - Current) * Alpha);
}

float FLureFishingLineRules::StateTension(ELureFishingState State, bool bFightActive, float FightTension01, const FLureFishingLineRow& Row)
{
	switch (State)
	{
	case ELureFishingState::Casting: return Row.CastTension;
	case ELureFishingState::Waiting: return Row.WaitTension;
	case ELureFishingState::Biting: return Row.BiteTension;
	case ELureFishingState::Hooked:
		if (bFightActive)
		{
			return FMath::IsFinite(FightTension01) ? FMath::Clamp(FightTension01, 0.f, 1.f) : 0.f;
		}
		return Row.HookedTension;
	case ELureFishingState::Idle:
	default: return 0.f;
	}
}

float FLureFishingLineRules::FloatAmount(float Tension01, const FLureFishingLineRow& Row)
{
	const float Strength = FMath::IsFinite(Row.FloatStrength) ? FMath::Clamp(Row.FloatStrength, 0.f, 1.f) : 0.f;
	return Strength * (1.f - Tautness(Tension01, Row));
}

FLureFishingLineRow FLureFishingLineRules::GetFallbackRow()
{
	// The struct defaults ARE the shipped DT_FishingLine "Default" row (a test checks they match).
	return FLureFishingLineRow();
}

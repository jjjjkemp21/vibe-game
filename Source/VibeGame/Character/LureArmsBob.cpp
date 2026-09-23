// Lure: procedural first-person arms bob and look sway (T-004).

#include "Character/LureArmsBob.h"
#include "Character/LureMovementTypes.h"

float FLureArmsBob::GetStepRate(const FLureMovementRow& Row)
{
	if (Row.BobStepRate > 0.f)
	{
		return Row.BobStepRate;
	}
	return FMath::Clamp(1.2f + 0.0025f * Row.MaxSpeed, 1.f, 3.f);
}

FTransform FLureArmsBob::Step(FLureArmsBobState& State, const FLureMovementRow& Row, const FLureArmsMotionSettings& Settings,
	float HorizontalSpeed, bool bOnGround, bool bHoldingRod, const FVector2D& LookRateDegPerSec, float DeltaTime)
{
	const float Dt = (FMath::IsFinite(DeltaTime) && DeltaTime > 0.f) ? DeltaTime : 0.f;

	// Strength follows speed (0..1) and fades smoothly on start, stop and in the air.
	const float SpeedRatio = Row.MaxSpeed > 0.f && FMath::IsFinite(HorizontalSpeed) ? FMath::Clamp(HorizontalSpeed / Row.MaxSpeed, 0.f, 1.f) : 0.f;
	State.Amplitude = FMath::FInterpTo(State.Amplitude, bOnGround ? SpeedRatio : 0.f, Dt, Settings.BobBlendSpeed);

	// Steps per second never reach 0, so the phase keeps moving while the strength fades.
	const float StepsPerSecond = GetStepRate(Row) * (0.5f + 0.5f * SpeedRatio);
	State.Phase = FMath::Fmod(State.Phase + UE_PI * StepsPerSecond * Dt, 2.f * UE_PI);

	const float PerStep = 0.5f * (1.f - FMath::Cos(2.f * State.Phase)); // 0..1, once per footstep
	const float PerStride = FMath::Sin(State.Phase);                    // -1..1, once per left+right stride
	const float Strength = State.Amplitude * (bHoldingRod ? Settings.BobHoldRodScale : 1.f);

	const FVector Offset(
		-Row.BobForward * Strength * PerStep,
		Row.BobLateral * Strength * PerStride,
		-Row.BobVertical * Strength * PerStep);

	FRotator Rotation(
		-Row.BobPitch * Strength * PerStep,	// pitch: nod down per step
		Row.BobYaw * Strength * PerStride,	// yaw
		Row.BobRoll * Strength * PerStride);	// roll

	// Look sway: the arms lag the view a little (opposite the turn), clamped, eased.
	const float MaxSway = Settings.LookSwayMaxDeg;
	const float TargetYaw = FMath::Clamp(-LookRateDegPerSec.X * Settings.LookSwayPerDegPerSec, -MaxSway, MaxSway);
	const float TargetPitch = FMath::Clamp(-LookRateDegPerSec.Y * Settings.LookSwayPerDegPerSec, -MaxSway, MaxSway);
	State.Sway.Yaw = FMath::FInterpTo(State.Sway.Yaw, FMath::IsFinite(TargetYaw) ? TargetYaw : 0.f, Dt, Settings.LookSwaySpeed);
	State.Sway.Pitch = FMath::FInterpTo(State.Sway.Pitch, FMath::IsFinite(TargetPitch) ? TargetPitch : 0.f, Dt, Settings.LookSwaySpeed);

	Rotation += State.Sway;
	return FTransform(Rotation, Offset);
}

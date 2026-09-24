// Lure: rod steering during the reel fight (T-028), owner-side helpers.

#include "Fishing/LureRodControl.h"

namespace LureRodControlPrivate
{
	float Finite(float Value, float Fallback = 0.f)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	float Unit(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.f, 1.f) : 0.f;
	}

	/** The rod's range in look degrees (at least 1 degree, so a bad row never divides by zero). */
	float UpDeg(const FLureFishFightRow& Tuning) { return FMath::Max(1.f, Finite(Tuning.RodAimUpDeg, 35.f)); }
	float DownDeg(const FLureFishFightRow& Tuning) { return FMath::Max(1.f, Finite(Tuning.RodAimDownDeg, 35.f)); }
	float SideDeg(const FLureFishFightRow& Tuning) { return FMath::Max(1.f, Finite(Tuning.RodAimSideDeg, 45.f)); }

	/** One exponential easing factor for DeltaTime with time constant Tau (Tau <= 0: all the way). */
	float EaseAlpha(float DeltaTime, float Tau)
	{
		if (!(DeltaTime > 0.f) || !FMath::IsFinite(DeltaTime))
		{
			return 0.f;
		}
		return (Tau > 0.f && FMath::IsFinite(Tau)) ? 1.f - FMath::Exp(-DeltaTime / Tau) : 1.f;
	}
}

// ---- FLureRodAim ----

void FLureRodAim::AddLookInput(float YawDegrees, float PitchDegrees, const FLureFishFightRow& Tuning)
{
	using namespace LureRodControlPrivate;
	PitchDeg = FMath::Clamp(PitchDeg + Finite(PitchDegrees), -DownDeg(Tuning), UpDeg(Tuning));
	YawDeg = FMath::Clamp(YawDeg + Finite(YawDegrees), -SideDeg(Tuning), SideDeg(Tuning));
}

float FLureRodAim::GetPitch01(const FLureFishFightRow& Tuning) const
{
	using namespace LureRodControlPrivate;
	return Unit(PitchDeg >= 0.f ? PitchDeg / UpDeg(Tuning) : PitchDeg / DownDeg(Tuning));
}

float FLureRodAim::GetYaw01(const FLureFishFightRow& Tuning) const
{
	using namespace LureRodControlPrivate;
	return Unit(YawDeg / SideDeg(Tuning));
}

// ---- FLureRodControl ----

uint8 FLureRodControl::PackAxis(float Value01)
{
	return static_cast<uint8>(127 + FMath::Clamp(FMath::RoundToInt(LureRodControlPrivate::Unit(Value01) * 127.f), -127, 127));
}

float FLureRodControl::UnpackAxis(uint8 Packed)
{
	return FMath::Clamp((static_cast<float>(Packed) - 127.f) / 127.f, -1.f, 1.f);
}

float FLureRodControl::PitchDegrees(float Pitch01, const FLureFishFightRow& Tuning)
{
	using namespace LureRodControlPrivate;
	const float Pitch = Unit(Pitch01);
	return Pitch >= 0.f ? Pitch * UpDeg(Tuning) : Pitch * DownDeg(Tuning);
}

float FLureRodControl::YawDegrees(float Yaw01, const FLureFishFightRow& Tuning)
{
	return LureRodControlPrivate::Unit(Yaw01) * LureRodControlPrivate::SideDeg(Tuning);
}

FRotator FLureRodControl::CameraTarget(const FVector& Eye, const FVector& Fish, float Pitch01, float Yaw01, const FLureFishFightRow& Tuning)
{
	using LureRodControlPrivate::Finite;
	const FVector ToFish = Fish - Eye;
	FRotator Target = ToFish.IsNearlyZero() ? FRotator::ZeroRotator : ToFish.Rotation();
	Target.Yaw += YawDegrees(Yaw01, Tuning) * FMath::Clamp(Finite(Tuning.CameraRodYawShare), 0.f, 1.f);
	Target.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Target.Pitch) + PitchDegrees(Pitch01, Tuning) * FMath::Clamp(Finite(Tuning.CameraRodPitchShare), 0.f, 1.f), -80.f, 80.f);
	Target.Yaw = FRotator::NormalizeAxis(Target.Yaw);
	Target.Roll = 0.f;
	return Target;
}

FRotator FLureRodControl::CameraStep(const FRotator& Current, const FRotator& Target, float DeltaTime, const FLureFishFightRow& Tuning)
{
	const float Alpha = LureRodControlPrivate::EaseAlpha(DeltaTime, Tuning.CameraFollowTime);
	const FRotator From = Current.GetNormalized();
	const FRotator To = Target.GetNormalized();
	FRotator Out;
	Out.Pitch = FMath::Clamp(From.Pitch + (To.Pitch - From.Pitch) * Alpha, -89.f, 89.f);
	Out.Yaw = FRotator::NormalizeAxis(From.Yaw + FRotator::NormalizeAxis(To.Yaw - From.Yaw) * Alpha);
	Out.Roll = 0.f;
	return Out;
}

FVector2D FLureRodControl::EaseAim(const FVector2D& Current, const FVector2D& Target, float DeltaTime, float TimeConstant)
{
	const float Alpha = LureRodControlPrivate::EaseAlpha(DeltaTime, TimeConstant);
	return Current + (Target - Current) * Alpha;
}

FRotator FLureRodControl::RodLook(float Pitch01, float Yaw01, const FLureFishFightRow& Tuning)
{
	using namespace LureRodControlPrivate;
	return FRotator(Unit(Pitch01) * FMath::Max(0.f, Finite(Tuning.RodAimLookPitchDeg)), Unit(Yaw01) * FMath::Max(0.f, Finite(Tuning.RodAimLookYawDeg)), 0.f);
}

FString FLureRodControl::DescribeRod(float Pitch01, float Yaw01)
{
	constexpr float Zone = 1.f / 3.f;
	const float Pitch = LureRodControlPrivate::Unit(Pitch01);
	const float Yaw = LureRodControlPrivate::Unit(Yaw01);
	FString Text = Pitch > Zone ? TEXT("back") : (Pitch < -Zone ? TEXT("dipped") : TEXT("level"));
	if (Yaw > Zone)
	{
		Text += TEXT("-right");
	}
	else if (Yaw < -Zone)
	{
		Text += TEXT("-left");
	}
	return Text;
}

FString FLureRodControl::ReelText(int32 Step, int32 NumSteps)
{
	const int32 Num = FMath::Max(1, NumSteps);
	return FString::Printf(TEXT("Reel %d/%d (wheel or LB/RB)"), FMath::Clamp(Step, 0, Num - 1) + 1, Num);
}

FString FLureRodControl::RunHint(ELureFightRunSide Side)
{
	switch (Side)
	{
	case ELureFightRunSide::Left: return TEXT("Fish runs LEFT: pull right");
	case ELureFightRunSide::Right: return TEXT("Fish runs RIGHT: pull left");
	case ELureFightRunSide::None:
	default: return FString();
	}
}

ELureFightRunSide FLureRodControl::RunSideFromDirection(int32 RunDir)
{
	return RunDir < 0 ? ELureFightRunSide::Left : (RunDir > 0 ? ELureFightRunSide::Right : ELureFightRunSide::None);
}

int32 FLureRodControl::DirectionFromRunSide(ELureFightRunSide Side)
{
	return Side == ELureFightRunSide::Left ? -1 : (Side == ELureFightRunSide::Right ? 1 : 0);
}

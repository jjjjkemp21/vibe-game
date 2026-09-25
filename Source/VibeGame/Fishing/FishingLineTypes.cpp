// Lure: physics fishing line, tuning row and pure rules (T-032).

#include "Fishing/FishingLineTypes.h"
#include <cmath>

const TCHAR* FLureFishingLineRules::FallbackMarker = TEXT("using the built-in fishing line tuning");

namespace LureFishingLineTypesPrivate
{
	/**
	 *  A shrinking line length this close to its target (share of the target) is exactly the target. Without it the float
	 *  follow stalls a few float steps above the target, and the solver turns any excess length into visible sag (T032-B1).
	 */
	constexpr float FollowSnapShare = 1.0e-5f;

	/** Sag at the middle of a line (1 + x^2) x Chord long, per unit x and cm of chord (shallow parabola: sqrt(3/8 x excess)). */
	constexpr double SagPerX = 0.61237243569579452; // sqrt(3 / 8)

	/**
	 *  The steady straightening of TightenRestLength is for (nearly) straight targets: full at a straight target, fading out
	 *  by a target sag of this many cm. A line that stops at a clearly slack shape after a fast pull would whip past it and
	 *  sag back; the exponential follow eases into those.
	 */
	constexpr double SteadyBlendSag = 3.0;

	/**
	 *  Toward a slightly slack target the steady straightening eases out no harder than gravity (x EaseSag / target sag, since
	 *  a nearly straight line has little room to fly past its shape): the line doesn't overshoot and sag back.
	 */
	constexpr double EaseSag = 5.0;

	/**
	 *  Next (a double) as the float length between Target and Current: exactly Target within FollowSnapShare of it, and at
	 *  least one float step shorter than Current when it should shrink (float steps would otherwise stall above the target).
	 */
	float ShrinkStep(double Next, float Current, float Target)
	{
		if (Next - Target <= FollowSnapShare * Target)
		{
			return Target;
		}
		float Out = static_cast<float>(FMath::Clamp(Next, static_cast<double>(Target), static_cast<double>(Current)));
		if (Out >= Current && Next < Current)
		{
			Out = FMath::Max(Target, std::nextafter(Current, Target));
		}
		return Out;
	}

	/** A tension 0..1: NaN = slack (0), anything else clamped (+Inf = 1, fully taut). */
	float SafeTension(float Tension01)
	{
		return FMath::IsNaN(Tension01) ? 0.f : FMath::Clamp(Tension01, 0.f, 1.f);
	}
}

bool FLureFishingLineRow::Validate(FString& OutProblem) const
{
	const float Values[] = { SubstepRate, GravityScale, AirDrag, WaterDrag, SlackShare, TautExponent, LengthResponse, StraightenTime, CastTension,
		WaitTension, BiteTension, HookedTension, FloatStrength, FloatHeight, WaterRefreshDistance, RecoilSpeed, RecoilTime,
		RecoilLengthShare, HangEndMass, HangDrag, HangReelSpeed, HangMaxSwingDeg, HangFaceTime, TeleportDistance, CollisionRadius,
		GroundFriction, CollisionQueryMargin, CollisionRefreshTime };
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
	if (StraightenTime < 0.05f || StraightenTime > 10.f)
	{
		return Fail(FString::Printf(TEXT("StraightenTime %.3f must be in [0.05, 10] s"), StraightenTime));
	}
	if (LengthResponse < 0.1f || RecoilTime < 0.05f || HangEndMass < 1.f || WaterRefreshDistance < 1.f || TeleportDistance < 1.f)
	{
		return Fail(TEXT("LengthResponse >= 0.1, RecoilTime >= 0.05, HangEndMass >= 1, WaterRefreshDistance >= 1 and TeleportDistance >= 1"));
	}
	if (HangReelSpeed < 1.f)
	{
		return Fail(FString::Printf(TEXT("HangReelSpeed %.2f must be >= 1 cm/s"), HangReelSpeed));
	}
	if (HangMaxSwingDeg < 10.f || HangMaxSwingDeg > 90.f)
	{
		return Fail(FString::Printf(TEXT("HangMaxSwingDeg %.2f must be in [10, 90] degrees"), HangMaxSwingDeg));
	}
	if (HangFaceTime > 5.f)
	{
		return Fail(FString::Printf(TEXT("HangFaceTime %.3f must be in [0, 5] s"), HangFaceTime));
	}
	if (CollisionRadius > 50.f)
	{
		return Fail(FString::Printf(TEXT("CollisionRadius %.2f must be in [0, 50] cm"), CollisionRadius));
	}
	if (CollisionQueryMargin < 10.f || CollisionRefreshTime < 0.02f)
	{
		return Fail(TEXT("CollisionQueryMargin >= 10 cm and CollisionRefreshTime >= 0.02 s"));
	}
	return true;
}

float FLureFishingLineRules::Tautness(float Tension01, const FLureFishingLineRow& Row)
{
	const float Tension = LureFishingLineTypesPrivate::SafeTension(Tension01);
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
	const double Alpha = 1.0 - FMath::Exp(-static_cast<double>(Rate) * Dt);
	return LureFishingLineTypesPrivate::ShrinkStep(Current + (static_cast<double>(SafeTarget) - Current) * Alpha, Current, SafeTarget);
}

float FLureFishingLineRules::TightenRestLength(float Current, float Target, float Chord, float DeltaTime, const FLureFishingLineRow& Row)
{
	using namespace LureFishingLineTypesPrivate;
	const float SafeChord = FMath::IsFinite(Chord) ? FMath::Max(0.f, Chord) : 0.f;
	const float SafeTarget = FMath::IsFinite(Target) ? FMath::Max(SafeChord, Target) : SafeChord;
	const float Exponential = FollowRestLength(Current, SafeTarget, SafeChord, DeltaTime, Row);
	if (!FMath::IsFinite(Current) || Current <= SafeTarget || SafeChord < 1.f || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f)
	{
		return Exponential; // slack appears at once; a line too short to sag; no time
	}

	// Steady straightening, in x = sqrt(length / chord - 1) (the sag is about x x chord x SagPerX): x falls at a steady rate,
	// so a line with the full SlackShare goes straight in StraightenTime s and the straight line stops it (no whip). An
	// exponential alone slows down while the line is still slack: the line flies on past the chord and sags back (T032-O3).
	const double Dt = DeltaTime;
	const double C = SafeChord;
	const double X = FMath::Sqrt(FMath::Max(0.0, Current / C - 1.0));
	const double TargetX = FMath::Sqrt(FMath::Max(0.0, SafeTarget / C - 1.0));
	const double TargetSag = TargetX * SagPerX * C;
	const double Time = FMath::IsFinite(Row.StraightenTime) ? FMath::Clamp(static_cast<double>(Row.StraightenTime), 0.05, 10.0) : 0.4;
	const double FullSlack = FMath::IsFinite(Row.SlackShare) ? FMath::Clamp(static_cast<double>(Row.SlackShare), 0.01, 1.0) : 0.05;
	double Speed = FMath::Sqrt(FullSlack) / Time * FMath::Max(0.0, 1.0 - TargetSag / SteadyBlendSag);
	const double Gravity = 980.0 * (FMath::IsFinite(Row.GravityScale) ? FMath::Clamp(static_cast<double>(Row.GravityScale), 0.0, 10.0) : 1.0);
	if (TargetSag > 0.0 && Gravity > 0.0)
	{
		const double Deceleration = Gravity / (SagPerX * C) * FMath::Max(1.0, EaseSag / TargetSag); // x per s^2
		Speed = FMath::Min(Speed, FMath::Sqrt(2.0 * Deceleration * (X - TargetX)));
	}
	if (Speed <= 0.0)
	{
		return Exponential;
	}
	const double NextX = FMath::Max(TargetX, X - Speed * Dt);
	const float Steady = ShrinkStep(C * (1.0 + NextX * NextX), Current, SafeTarget);
	return FMath::Min(Exponential, Steady);
}

float FLureFishingLineRules::CarryRestLength(float Current, float LastChord, float Chord)
{
	if (!FMath::IsFinite(Current) || !FMath::IsFinite(LastChord) || !FMath::IsFinite(Chord) || LastChord < 1.f || Chord < 0.f)
	{
		return Current;
	}
	const double Share = FMath::Clamp(static_cast<double>(Current) / LastChord, 1.0, 2.0);
	return static_cast<float>(Chord * Share);
}

float FLureFishingLineRules::ReelInRestLength(float Current, float Target, float DeltaTime, const FLureFishingLineRow& Row)
{
	const float SafeTarget = FMath::IsFinite(Target) ? FMath::Max(0.f, Target) : 0.f;
	if (!FMath::IsFinite(Current) || Current <= SafeTarget)
	{
		return SafeTarget; // a shorter line drops at once (or a first frame)
	}
	const double Dt = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0, static_cast<double>(DeltaTime)) : 0.0;
	double Speed = FMath::IsFinite(Row.HangReelSpeed) ? FMath::Max(1.0, static_cast<double>(Row.HangReelSpeed)) : 500.0;
	const double Gravity = 980.0 * (FMath::IsFinite(Row.GravityScale) ? FMath::Clamp(static_cast<double>(Row.GravityScale), 0.0, 10.0) : 1.0);
	if (Gravity > 0.0)
	{
		// Slows at half of gravity toward the end: the actor (slowed by gravity at twice that) never overtakes the reel.
		Speed = FMath::Min(Speed, FMath::Sqrt(2.0 * (0.5 * Gravity) * (static_cast<double>(Current) - SafeTarget)));
	}
	return LureFishingLineTypesPrivate::ShrinkStep(static_cast<double>(Current) - Speed * Dt, Current, SafeTarget);
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
			return LureFishingLineTypesPrivate::SafeTension(FightTension01);
		}
		return Row.HookedTension;
	case ELureFishingState::Idle:
	default: return 0.f;
	}
}

FQuat FLureFishingLineRules::SideOnHangRotation(const FQuat& Current, const FVector& Up, const FVector& HangPoint, const FVector& ViewLocation,
	float DeltaTime, float FaceTime)
{
	FVector Axis = Up;
	if (Axis.ContainsNaN() || !Axis.Normalize())
	{
		Axis = FVector::UpVector;
	}
	// The side it shows now, square to the line (it never spins about another axis).
	FVector Side = Current.ContainsNaN() ? FVector::RightVector : Current.GetAxisY();
	Side -= Axis * FVector::DotProduct(Side, Axis);
	if (!Side.Normalize())
	{
		FVector Other;
		Axis.FindBestAxisVectors(Side, Other);
	}
	// Its right side (+Y) toward whoever looks at it: the viewer's direction square to the line.
	FVector ToViewer = ViewLocation - HangPoint;
	if (!ToViewer.ContainsNaN())
	{
		ToViewer -= Axis * FVector::DotProduct(ToViewer, Axis);
		if (ToViewer.Normalize() && FMath::IsFinite(DeltaTime) && DeltaTime > 0.f)
		{
			const double Angle = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(Side, ToViewer), Axis), FVector::DotProduct(Side, ToViewer));
			const double Share = (FMath::IsFinite(FaceTime) && FaceTime > 0.f) ? 1.0 - FMath::Exp(-static_cast<double>(DeltaTime) / FaceTime) : 1.0;
			Side = FQuat(Axis, Angle * Share).RotateVector(Side);
		}
	}
	return FRotationMatrix::MakeFromXY(Axis, Side).ToQuat();
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

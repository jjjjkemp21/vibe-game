// Lure: procedural first-person arms bob and look sway (T-004). Spec: art/export/Characters/SK_FPArms.anim.md.

#pragma once

#include "CoreMinimal.h"
#include "LureArmsBob.generated.h"

struct FLureMovementRow;

/** Global arms-motion tuning (per-stance bob amplitudes are DT_Movement columns). Lives in ULureCharacterSettings. */
USTRUCT(BlueprintType)
struct FLureArmsMotionSettings
{
	GENERATED_BODY()

	/** How fast the bob fades in and out when you start, stop or leave the ground (FInterpTo speed, 1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Motion", meta=(ClampMin="0"))
	float BobBlendSpeed = 8.f;

	/** Bob multiplier while holding the rod (steadier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Motion", meta=(ClampMin="0"))
	float BobHoldRodScale = 0.7f;

	/** Arms lag behind the view: degrees of sway per degree/second of turning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Motion", meta=(ClampMin="0"))
	float LookSwayPerDegPerSec = 0.02f;

	/** Largest look sway, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Motion", meta=(ClampMin="0"))
	float LookSwayMaxDeg = 2.5f;

	/** How fast the sway follows the view (FInterpTo speed, 1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Motion", meta=(ClampMin="0"))
	float LookSwaySpeed = 10.f;

	/** Play rate of A_FPArms_StanceDip when landing from a jump or fall (0 = no dip). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Motion", meta=(ClampMin="0"))
	float LandingDipPlayRate = 1.f;
};

/** Running state of one character's arms motion (client-side cosmetic, never replicated). */
struct FLureArmsBobState
{
	/** 0..1 bob strength (fades with speed and ground contact). */
	float Amplitude = 0.f;

	/** Step phase in radians, wrapped to [0, 2 PI); PI per footstep. */
	float Phase = 0.f;

	/** Look sway (pitch and yaw), degrees. */
	FRotator Sway = FRotator::ZeroRotator;
};

/** Pure bob math (no world access), so it's unit-testable. */
struct FLureArmsBob
{
	/** Steps per second at MaxSpeed: the row's BobStepRate, or clamp(1.2 + 0.0025 * MaxSpeed, 1, 3) when it is 0. */
	static float GetStepRate(const FLureMovementRow& Row);

	/**
	 *  Advances State by DeltaTime and returns the arms' offset relative to the camera (location in cm, rotation in degrees).
	 *  HorizontalSpeed in cm/s; LookRateDegPerSec = view turn rate (X = yaw, Y = pitch), deg/s.
	 */
	static FTransform Step(FLureArmsBobState& State, const FLureMovementRow& Row, const FLureArmsMotionSettings& Settings,
		float HorizontalSpeed, bool bOnGround, bool bHoldingRod, const FVector2D& LookRateDegPerSec, float DeltaTime);
};

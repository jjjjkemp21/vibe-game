// Lure: first-person arms pose selection (T-006).

#include "Character/FPArmsPose.h"
#include "Character/LureMovementTypes.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

EFPArmsPose FLureRodPose::Step(FLureRodPoseState& State, const FLureMovementRow& Row, const FLureRodPoseInput& Input)
{
	const float Dt = (FMath::IsFinite(Input.DeltaTime) && Input.DeltaTime > 0.f) ? Input.DeltaTime : 0.f;
	const float Speed = FMath::IsFinite(Input.Speed2D) ? Input.Speed2D : 0.f;
	const bool bInput = FMath::IsFinite(Input.MoveInput) && Input.MoveInput > MoveInputThreshold;

	// Moving: switch in at once (tuck immediately); switch out only after RodStillDelay (a short pause doesn't flick the rod out).
	if (!State.bMoving)
	{
		if (Speed > Row.RodMoveSpeedIn || bInput)
		{
			State.bMoving = true;
			State.StillTimer = 0.f;
		}
	}
	else
	{
		State.StillTimer = (Speed < Row.RodMoveSpeedOut && !bInput) ? State.StillTimer + Dt : 0.f;
		if (State.StillTimer >= Row.RodStillDelay)
		{
			State.bMoving = false;
			State.StillTimer = 0.f;
		}
	}

	// Arms pitch follow-up eases with the pose blend (stance changes).
	const float TargetFollowUp = FMath::Clamp(Row.ArmsPitchFollowUp, 0.f, 1.f);
	if (!State.bInitialized)
	{
		State.PitchFollowUp = TargetFollowUp;
		State.bInitialized = true;
	}
	else if (Row.RodPoseBlendTime > 0.f)
	{
		State.PitchFollowUp = FMath::FInterpConstantTo(State.PitchFollowUp, TargetFollowUp, Dt, 1.f / Row.RodPoseBlendTime);
	}
	else
	{
		State.PitchFollowUp = TargetFollowUp;
	}

	if (!Input.bHoldingRod)
	{
		return EFPArmsPose::Idle;
	}
	EFPArmsPose Pose = State.bMoving ? Row.RodPoseMoving : Row.RodPoseStill;
	if (Pose == EFPArmsPose::ProneHold && !State.bMoving && Row.RodHoldClearance > 0.f && Input.bHoldBlocked && !Input.bLineOut)
	{
		Pose = EFPArmsPose::ProneTuck; // the hold would cut into a wall (crawl cave); keep the rod tucked
	}
	return Pose;
}

float FLureRodPose::ArmsCounterPitch(float PitchFollowUp, float CameraPitchDegrees)
{
	if (!FMath::IsFinite(PitchFollowUp) || !FMath::IsFinite(CameraPitchDegrees))
	{
		return 0.f;
	}
	const float Pitch = FRotator::NormalizeAxis(CameraPitchDegrees);
	return -(1.f - FMath::Clamp(PitchFollowUp, 0.f, 1.f)) * FMath::Max(0.f, Pitch);
}

bool FLureRodPose::TraceHoldClearance(const UWorld* World, const FVector& Eye, const FRotator& View, float Distance, const AActor* IgnoreActor)
{
	if (!World || !(Distance > 0.f))
	{
		return false;
	}
	const FVector Forward = FRotator(0.f, View.Yaw, 0.f).Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureRodHoldClearance), false, IgnoreActor);
	const FCollisionObjectQueryParams Objects(ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic));
	FHitResult Hit;
	return World->SweepSingleByObjectType(Hit, Eye, Eye + Forward * Distance, FQuat::Identity, Objects, FCollisionShape::MakeSphere(ClearanceProbeRadius), Params);
}

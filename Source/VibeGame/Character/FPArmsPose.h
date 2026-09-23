// Lure: first-person arms pose selection (T-006). Spec: art/export/Characters/SK_FPArms.anim.md ("Switch rule").

#pragma once

#include "CoreMinimal.h"
#include "FPArmsPose.generated.h"

class AActor;
class UWorld;
struct FLureMovementRow;

/**
 *  Which arms loop ABP_FPArms plays (Blend Poses by EFPArmsPose, one Sequence Player each, sync group FPArmsBreath):
 *  Idle = A_FPArms_Idle, HoldRod = A_FPArms_HoldRod_Idle, ProneHold = A_FPArms_Prone_HoldRod_Idle, ProneTuck = A_FPArms_Prone_TuckRod.
 *  DT_Movement's RodPoseStill / RodPoseMoving columns pick the pose per stance (CSV cells use these names).
 */
UENUM(BlueprintType)
enum class EFPArmsPose : uint8
{
	Idle = 0,
	HoldRod = 1,
	ProneHold = 2,
	ProneTuck = 3
};

/** Running state of the rod pose switch (owning client, cosmetic, never replicated). */
struct FLureRodPoseState
{
	/** Moving per the hysteresis (RodMoveSpeedIn / RodMoveSpeedOut / RodStillDelay). */
	bool bMoving = false;

	/** Seconds spent slow and without input while bMoving (switches back to the still pose at RodStillDelay). */
	float StillTimer = 0.f;

	/** Smoothed ArmsPitchFollowUp (eases to the row's value over RodPoseBlendTime). */
	float PitchFollowUp = 1.f;

	bool bInitialized = false;
};

/** What the switch reads each frame. */
struct FLureRodPoseInput
{
	/** The rod is in hand (false = Idle). */
	bool bHoldingRod = false;

	/** Horizontal speed, cm/s. */
	float Speed2D = 0.f;

	/** Size of the movement input, 0..1 (pushing against a wall still counts as moving). */
	float MoveInput = 0.f;

	/** The prone hold would hit something within the row's RodHoldClearance (see TraceHoldClearance). */
	bool bHoldBlocked = false;

	/** A fishing line is out: a blocked prone hold stays out (you are fishing) instead of tucking. */
	bool bLineOut = false;

	float DeltaTime = 0.f;
};

/** Pure rod-pose rules (no world access except TraceHoldClearance), so they are unit-testable. */
struct FLureRodPose
{
	/** Movement input above this counts as "pushing" (spec: MovementInput.Size2D() > 0.2). */
	static constexpr float MoveInputThreshold = 0.2f;

	/** Radius of the prone-hold clearance probe, cm (spec: 5 cm sphere). */
	static constexpr float ClearanceProbeRadius = 5.f;

	/**
	 *  Advances State and returns the pose (spec pseudo-code):
	 *    not moving -> moving at once when Speed2D > RodMoveSpeedIn or there is move input;
	 *    moving -> still after RodStillDelay seconds below RodMoveSpeedOut with no input;
	 *    Pose = !bHoldingRod ? Idle : (moving ? RodPoseMoving : RodPoseStill);
	 *    a still ProneHold becomes ProneTuck when bHoldBlocked, RodHoldClearance > 0 and no line is out.
	 *  Also eases State.PitchFollowUp toward Row.ArmsPitchFollowUp over Row.RodPoseBlendTime.
	 */
	static EFPArmsPose Step(FLureRodPoseState& State, const FLureMovementRow& Row, const FLureRodPoseInput& Input);

	/** A tucked rod can't fish (casting and a line out are blocked while the pose would be a tuck). */
	static bool IsTucked(EFPArmsPose Pose) { return Pose == EFPArmsPose::ProneTuck; }

	/** Extra arms pitch (degrees) that keeps the arms down when looking up: -(1 - FollowUp) * max(0, CameraPitch). */
	static float ArmsCounterPitch(float PitchFollowUp, float CameraPitchDegrees);

	/**
	 *  True if a 5 cm sphere from Eye along the view's horizontal forward hits something within Distance cm
	 *  (the prone hold's rod would cut into a wall). Distance <= 0 or no world = false.
	 */
	static bool TraceHoldClearance(const UWorld* World, const FVector& Eye, const FRotator& View, float Distance, const AActor* IgnoreActor);
};

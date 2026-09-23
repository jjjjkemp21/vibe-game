// Lure: movement data types (T-004). Tuning lives in data/tables/DT_Movement.csv -> /Game/Data/DT_Movement.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Misc/EnumRange.h"
#include "Character/FPArmsPose.h"
#include "LureMovementTypes.generated.h"

/**
 *  Log category for Lure movement (stance, DT_Movement resolution, input setup).
 *  A missing or invalid DT_Movement logs ONE Warning per resolve that always contains the text
 *  "using built-in fallback rows" (see FLureMovementData::FallbackWarningMarker).
 */
DECLARE_LOG_CATEGORY_EXTERN(LogLureMovement, Log, All);

/** Body posture. Decides the collision capsule and the eye height. Sprinting is a Stand modifier (see ELureMovementState). */
UENUM(BlueprintType)
enum class ELureStance : uint8
{
	Stand,
	Crouch,
	Prone
};

/** The active DT_Movement row. One row per value; the row name is the value name (Stand, Sprint, Crouch, Prone, Swim, SwimSprint). */
UENUM(BlueprintType)
enum class ELureMovementState : uint8
{
	Stand,
	Sprint,
	Crouch,
	Prone,
	/** In the water (T-026). Swimming uses the Stand capsule; Sprint maps to SwimSprint. */
	Swim,
	SwimSprint,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(ELureMovementState, ELureMovementState::Count)

/**
 *  One row of DT_Movement (source: data/tables/DT_Movement.csv). Units are cm, cm/s, cm/s^2 and seconds.
 *  The column names in the CSV are the property names below.
 *  Sprint uses the Stand capsule; keep its capsule and EyeHeight equal to Stand (a data test checks this).
 *  Swim and SwimSprint also use the Stand capsule (keep their capsule columns equal to Stand); their EyeHeight is the
 *  swimming camera height above the feet; SurfaceFloatDepth and ClimbMaxHeight tune floating and climbing out (docs/specs/swimming.md).
 */
USTRUCT(BlueprintType)
struct FLureMovementRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Top ground (and air) speed in this state, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float MaxSpeed = 350.f;

	/** Acceleration toward MaxSpeed, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float MaxAcceleration = 2048.f;

	/** Collision capsule half height, cm (full height = 2x). Must be >= CapsuleRadius (the engine would raise it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float CapsuleHalfHeight = 90.f;

	/** Collision capsule radius, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float CapsuleRadius = 34.f;

	/** Eye (camera) height measured from the FEET (floor contact), cm. Keep it at least 10 cm below the capsule top (2 x CapsuleHalfHeight). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float EyeHeight = 165.f;

	/** Seconds the camera takes to ease to this row's EyeHeight when entering this state (0 = snap). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float TransitionTime = 0.25f;

	/** Loudness multiplier for movement noise (T-014). Stand = 1.0 is the baseline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float NoiseMultiplier = 1.f;

	/** Jump launch speed, cm/s (only used when CanJump). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float JumpZVelocity = 420.f;

	/** Whether jumping is allowed in this state. Prone can never jump, whatever the data says. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement")
	bool CanJump = true;

	// ---- First-person arms motion (cosmetic, client-side; spec art/export/Characters/SK_FPArms.anim.md) ----

	/** Arms bob steps per second at MaxSpeed. 0 = derive from MaxSpeed: clamp(1.2 + 0.0025 * MaxSpeed, 1, 3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float BobStepRate = 0.f;

	/** Arms dip per footstep, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float BobVertical = 0.8f;

	/** Arms side sway per stride (left + right step), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float BobLateral = 0.6f;

	/** Arms roll per stride, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float BobRoll = 0.6f;

	/** Arms nod down per footstep, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float BobPitch = 0.4f;

	/** Arms yaw per stride (prone crawl), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float BobYaw = 0.f;

	/** Arms push per footstep (prone crawl), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float BobForward = 0.f;

	/** Play rate of the A_FPArms_StanceDip additive when entering this stance (0 = no dip). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0"))
	float StanceDipPlayRate = 1.f;

	// ---- Optional CSV columns (missing = 0): the climb rule, water, arms offset, camera exit time ----

	/**
	 *  The one climb rule: the highest edge Jump gets you onto, cm (0 = no climb rule: no pull-up or climb out, and
	 *  landings are the engine's, never refused). On land it is measured from your feet
	 *  where you jumped: a landing on a higher ledge is refused, and a jump that reaches a ledge within it pulls you up.
	 *  Swimming, it is measured from the water surface (Jump climbs out). A ladder can allow more in the water.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Climb", meta=(ClampMin="0", DataTableImportOptional="true"))
	float ClimbMaxHeight = 0.f;

	/** Speed of that climb (up the edge, then onto it), cm/s. Needed when ClimbMaxHeight is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Climb", meta=(ClampMin="0", DataTableImportOptional="true"))
	float ClimbSpeed = 0.f;

	/**
	 *  Surface swimming (Swim rows): the capsule center floats this far below the water surface, cm, so the eyes are
	 *  EyeHeight - CapsuleHalfHeight - SurfaceFloatDepth above the water. 0 = no surface float (land rows; free 3D
	 *  swimming such as diving later). This is the only "stay at the surface" rule in the code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0", DataTableImportOptional="true"))
	float SurfaceFloatDepth = 0.f;

	/** The first-person arms sit this much closer to the eye in this state, cm (e.g. prone, so the hands stay out of a wall the capsule touches). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arms Bob", meta=(ClampMin="0", DataTableImportOptional="true"))
	float ArmsPullBack = 0.f;

	/** Seconds the camera takes to reach the next state's eye height when LEAVING this state (0 = the next row's TransitionTime). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0", DataTableImportOptional="true"))
	float ExitTransitionTime = 0.f;

	// ---- Optional swim-feel columns (missing = the built-in default below; used on the Swim rows) ----

	/**
	 *  Climbing out: the lowest edge top Jump climbs onto, cm relative to the water surface (<= 0). Lower tops count as
	 *  the seabed (walk out there instead). Keep it at or below the height a floating swimmer can step up to, or a shelf
	 *  between the two becomes a wall from the water.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Climb", meta=(ClampMax="0", DataTableImportOptional="true"))
	float ClimbOutLowestTop = -20.f;

	/**
	 *  Climbing out needs you at the surface: the head (capsule top) may be at most this far below the water surface, cm
	 *  (T-026 D2, for diving rows with SurfaceFloatDepth 0; a floating swimmer's head is always above the water).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Climb", meta=(ClampMin="0", DataTableImportOptional="true"))
	float ClimbOutSurfaceTolerance = 30.f;

	/** Climbing out: how far in front of the body an edge may be for Jump to climb onto it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Climb", meta=(ClampMin="0", DataTableImportOptional="true"))
	float ClimbOutReach = 45.f;

	/** Surface swimming: seconds the body takes to settle at the float depth after falling in (the plunge and rise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0.05", DataTableImportOptional="true"))
	float SurfaceFloatSettleTime = 0.8f;

	/** Swimming: how hard you coast to a stop with no input, cm/s^2 (0 = glide on). Read from the Swim row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0", DataTableImportOptional="true"))
	float SwimBrakingDeceleration = 600.f;

	// ---- Rod pose and fishing (T-006; spec SK_FPArms.anim.md "Switch rule"). Import-optional: old CSV fixtures keep these defaults. ----

	/** Arms pose while holding the rod and still (HoldRod; ProneHold when prone). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(DataTableImportOptional))
	EFPArmsPose RodPoseStill = EFPArmsPose::HoldRod;

	/** Arms pose while holding the rod and moving (HoldRod; ProneTuck when prone: a tucked rod can't fish). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(DataTableImportOptional))
	EFPArmsPose RodPoseMoving = EFPArmsPose::HoldRod;

	/** cm/s: faster than this counts as moving (switches to RodPoseMoving at once). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(ClampMin="0", DataTableImportOptional))
	float RodMoveSpeedIn = 15.f;

	/** cm/s: slower than this (and no move input) counts toward still. Must be <= RodMoveSpeedIn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(ClampMin="0", DataTableImportOptional))
	float RodMoveSpeedOut = 5.f;

	/** Seconds below RodMoveSpeedOut before switching back to the still pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(ClampMin="0", DataTableImportOptional))
	float RodStillDelay = 0.3f;

	/** Seconds of crossfade into this row's poses (ABP_FPArms Blend Time pins bind to it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(ClampMin="0", DataTableImportOptional))
	float RodPoseBlendTime = 0.3f;

	/** Share (0..1) of the camera's UPWARD pitch the arms follow (0 when prone: the arms stay on the ground when you look up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(ClampMin="0", ClampMax="1", DataTableImportOptional))
	float ArmsPitchFollowUp = 1.f;

	/** Prone hold wall check, cm: keep the tuck while still if something is this close ahead (0 = off). Never while a line is out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(ClampMin="0", DataTableImportOptional))
	float RodHoldClearance = 0.f;

	/** Whether you can cast (and keep a line out) in this state. Sprint = false. Moving in a tucked pose also blocks fishing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod Pose", meta=(DataTableImportOptional))
	bool CanFish = true;

	/** Runtime sanity check (finite, positive, HalfHeight >= Radius, eye inside the capsule, ...). Returns false and a reason if the row is unusable. */
	bool Validate(FString& OutProblem) const;
};

/** Static helpers for DT_Movement: row names, built-in fallback rows and row resolution. Pure (no logging), so tests can call them directly. */
struct FLureMovementData
{
	static constexpr int32 NumStates = static_cast<int32>(ELureMovementState::Count);
	static constexpr uint8 AllStatesMask = (1u << NumStates) - 1u;

	/** Substring present in every fallback warning (for AddExpectedMessage in tests). */
	static const TCHAR* FallbackWarningMarker;

	/** Row name of a state in DT_Movement: "Stand", "Sprint", "Crouch", "Prone", "Swim", "SwimSprint". */
	static FName GetRowName(ELureMovementState State);

	/** The row used when DT_Movement is missing or a row is invalid (same values as the shipped CSV at the time of writing). */
	static FLureMovementRow GetFallbackRow(ELureMovementState State);

	/** The DT_Movement row that holds a stance's capsule and eye height (Sprint is a Stand modifier). */
	static ELureMovementState ToMovementState(ELureStance Stance);

	/**
	 *  Fills OutRows (NumStates entries, indexed by ELureMovementState) from Table. Missing, invalid or unreadable rows
	 *  use the fallback row. Returns a bit mask (1 << State) of the rows that fell back; OutProblems says why
	 *  (also lists unknown extra rows, which are ignored). Table may be null.
	 */
	static uint8 ResolveRows(const UDataTable* Table, TArray<FLureMovementRow>& OutRows, TArray<FString>& OutProblems);

	/** The one warning line logged for a resolve with problems. */
	static FString FormatResolveWarning(const FString& TableName, const TArray<FString>& Problems, uint8 FallbackMask);
};

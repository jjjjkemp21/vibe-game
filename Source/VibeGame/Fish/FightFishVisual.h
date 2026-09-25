// Lure: the fish you see fighting on the line (T-029). Data row, the view the visual reads, and the pure rules.
// Source of truth for the tuning: data/tables/DT_FishVisual.json -> /Game/Data/DT_FishVisual (FFishVisualRow, row Default).
// Spec: docs/specs/fight-fish-visual.md. Clips and numbers: art/export/Fish/SK_Fish.anim.md.
//
// Layering (so fight changes stay in one place):
//   FLureFightNetState + FLureFishingNetState (T-007/T-028, replicated)
//     -> FFightFishViewAdapter (Fishing/FightFishViewAdapter.h: the ONLY visual code that reads the fight structs)
//     -> FFightFishView (below: plain numbers)
//     -> FFightFishVisual (pure rules) / ALureFightFish (the actor) / ULureFightFishSubsystem (spawn and despawn).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/FishInstance.h"
#include "FightFishVisual.generated.h"

struct FFishSpeciesRow;

/** One fight move id -> the clip role it plays (DT_FishVisual MoveRoles). */
USTRUCT(BlueprintType)
struct FFishMoveAnimRole
{
	GENERATED_BODY()

	/** DT_FightPattern move id (e.g. Run, Sulk). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual")
	FName MoveId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual")
	EFishAnimRole Role = EFishAnimRole::SwimFast;
};

/** A swim role's tail beat (DT_FishVisual RoleTailBeats): roles listed here get a speed-matched play rate. */
USTRUCT(BlueprintType)
struct FFishRoleTailBeat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual")
	EFishAnimRole Role = EFishAnimRole::SwimIdle;

	/** Tail beats per second of the clip at rate 1 (SK_Fish.anim.md "Tail beat"), > 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual", meta=(ClampMin="0.01"))
	float Hz = 1.f;
};

/** T-059a: a role's play-rate factor from the fish's stamina while it fights (DT_FishVisual RoleStaminaRates). */
USTRUCT(BlueprintType)
struct FFishRoleStaminaRate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual")
	EFishAnimRole Role = EFishAnimRole::SwimFast;

	/** Rate factor at full stamina (1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual", meta=(ClampMin="0"))
	float FreshRate = 1.f;

	/** Rate factor at stamina 0 (a spent fish beats slower). In between: linear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual", meta=(ClampMin="0"))
	float TiredRate = 1.f;
};

/**
 *  DT_FishVisual row (row Default): how the fighting fish looks and moves. Cosmetic only: every machine computes it from
 *  replicated state; nothing here changes the fight.
 */
USTRUCT(BlueprintType)
struct FFishVisualRow : public FTableRowBase
{
	GENERATED_BODY()

	// ---- Clips ----

	/** Move id -> role. A move not listed plays UnknownMoveRole (a new move needs only a row here, no code). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips")
	TArray<FFishMoveAnimRole> MoveRoles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips")
	EFishAnimRole UnknownMoveRole = EFishAnimRole::SwimFast;

	/** Speed-matched roles and their clip tail beat: only the escape swim uses them (T-059a); other roles, and the landed flop, play at AnimRate x (ReferenceWeight / Weight)^OtherRateWeightExponent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips")
	TArray<FFishRoleTailBeat> RoleTailBeats;

	/**
	 *  T-059a: while it fights (not tired), a clip plays at AnimRate x (ReferenceWeight / Weight)^OtherRateWeightExponent x
	 *  lerp(TiredRate, FreshRate, stamina): effort, not speed (reeling a spent fish in never speeds its tail up). A role not
	 *  listed: factor 1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(DataTableImportOptional))
	TArray<FFishRoleStaminaRate> RoleStaminaRates;

	/** T-059a: while it fights (not tired), alpha = AnimAmplitude x lerp(TiredAmplitudeScale, FreshAmplitudeScale, stamina). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0", ClampMax="1", DataTableImportOptional))
	float FreshAmplitudeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0", ClampMax="1", DataTableImportOptional))
	float TiredAmplitudeScale = 0.75f;

	/** Swim roles: PlayRate = AnimRate x Speed / (StrideBodyLengths x BodyLength x Hz), clamped to [MinPlayRate, MaxPlayRate]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0.01"))
	float StrideBodyLengths = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0"))
	float MinPlayRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0"))
	float MaxPlayRate = 2.f;

	/** Non-swim roles: a heavier fish moves slower. 1/6 = twice as heavy, 11 % slower. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0"))
	float OtherRateWeightExponent = 0.1667f;

	/** Blend time between roles, seconds (ABP_Fish Blend Time pins). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0"))
	float RoleBlendTime = 0.2f;

	/** Seconds of Thrash right after the hook set, before the fight's moves show. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0"))
	float HookSetThrashTime = 1.f;

	/** Dart player's start position when the dart turns to the fish's right, seconds (0 = to its left). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0"))
	float DartRightStartTime = 0.6f;

	/** A tired fish: SwimIdle at this rate and AnimAmplitude x ExhaustedAmplitudeScale, rolled ExhaustedRollDeg (0 = upright; Jimmy 2026-09-24: never on its side). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0"))
	float ExhaustedPlayRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="0", ClampMax="1"))
	float ExhaustedAmplitudeScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Clips", meta=(ClampMin="-180", ClampMax="180"))
	float ExhaustedRollDeg = 0.f;

	// ---- Size ----

	/** Scale = clamp((Weight / ReferenceWeight)^(1/3), MinScale, MaxScale) (the fishkit rule; clamps are a safety net). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size", meta=(ClampMin="0.01"))
	float MinScale = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size", meta=(ClampMin="0.01"))
	float MaxScale = 4.f;

	/** Body length, cm, when the mesh has no bounds (the fishkit fish are about 55 cm at reference weight). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size", meta=(ClampMin="1"))
	float DefaultBodyLengthCm = 55.f;

	/** Bone at the nose where the line ends (the fish is placed so this bone sits at the line's end). None = the body center. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size")
	FName MouthBone = TEXT("Mouth");

	// ---- Placement ----

	/** Depth of the fish's center under the water surface when the fight's Depth is 0, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float SurfaceDepth = 25.f;

	/** Share of the fight's cosmetic Depth the fish shows (1 = all of it), capped at MaxShownDepth cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float DepthShare = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float MaxShownDepth = 180.f;

	/** The fish stays this far above the bottom (a downward trace from the surface), cm. < 0 = no trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement")
	float FloorClearance = 10.f;

	/** Position smoothing time constant, seconds: on the machine with authority, and on the others (proxies, owning clients). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float AuthoritySmoothTime = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float ProxySmoothTime = 0.2f;

	/** A target farther than this from the fish is snapped to (spawn, a teleport), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float SnapDistance = 800.f;

	/** Rotation smoothing time constant, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float RotationSmoothTime = 0.12f;

	/** Escaping fish: below this speed it faces away from the player instead of along its swim. Fighting fish: below it no body swing and no pitch, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0"))
	float MinFacingSpeed = 20.f;

	/**
	 *  T-048: a fighting fish faces the rod (its mouth is its point nearest the player, the body away from the player). While
	 *  it swims, its body swings sideways up to this angle (head toward the side it swims to), so it reads as pulling away,
	 *  degrees. At most 80: the body never gets between the line and the mouth seen from above.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0", ClampMax="80", DataTableImportOptional))
	float RunSwingDeg = 35.f;

	/** Swim speed at which the body swing is full (RunSwingDeg); slower = proportionally less, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="1", DataTableImportOptional))
	float RunSwingFullSpeed = 200.f;

	/** The smoothing never lets the fish's mouth trail the line end (the bobber) by more than this, horizontally, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0", DataTableImportOptional))
	float MouthMaxLagCm = 5.f;

	/** Most nose-up / nose-down while rising or diving, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(ClampMin="0", ClampMax="89"))
	float MaxPitchDeg = 30.f;

	// ---- After the fight ----

	/** A lost fish (snapped, thrown hook, reeled in) swims away from the player for this long, then disappears, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="After", meta=(ClampMin="0"))
	float EscapeTime = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="After", meta=(ClampMin="0"))
	float EscapeSpeed = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="After", meta=(ClampMin="0"))
	float EscapeSinkSpeed = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish Visual", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;

	/** Finite and in range, unique MoveRoles ids, unique RoleTailBeats roles with Hz > 0, Min <= Max. */
	bool Validate(FString& OutProblem) const;

	/** The built-in tuning (= the shipped DT_FishVisual Default row; a test checks). */
	static FFishVisualRow GetFallbackRow();
};

/** How a fight ended, as the visual sees it. */
enum class EFightFishEnd : uint8
{
	/** Still on (or nothing happened). */
	None,
	/** Reeled in: hand-off to the landed fish (T-030). */
	Landed,
	/** Snapped, thrown hook, spooled, reeled in or cancelled: the fish swims away. */
	Escaped
};

/** What the fish visual needs from one player's fight: plain numbers, no fight structs (FFightFishViewAdapter fills it). */
struct FFightFishView
{
	/** A fish is on and the server runs the fight. */
	bool bFighting = false;
	/** Changes with every new fight (a new fish = a new visual). */
	uint8 FightId = 0;
	/** When not fighting: how the last fight ended. */
	EFightFishEnd End = EFightFishEnd::None;

	/** The fight's current move (None = tired) and whether the fish is exhausted. */
	FName MoveId;
	bool bExhausted = false;
	/** Cosmetic depth of the fish, cm (0 = at the surface). */
	float DepthCm = 0.f;
	/** Tension / line strength. */
	float Tension01 = 0.f;
	/** The fish's stamina 0..1 (T-059a: the clip rate and alpha follow it). */
	float Stamina01 = 1.f;

	/** The player (fight center), the end of the line in the water (XY; its Z is the water surface) and the surface height. */
	FVector PlayerLocation = FVector::ZeroVector;
	FVector LineEnd = FVector::ZeroVector;
	float WaterZ = 0.f;

	/** This machine simulates the fight (server / standalone). Proxies and owning clients smooth more. */
	bool bHasAuthority = false;

	/** The hooked fish (its species and weight pick the mesh and the scale; empty when not fighting). */
	FFishInstance Fish;
};

/** What the fish is doing, for the clip choice. */
enum class EFightFishPhase : uint8
{
	Fighting,
	Landed,
	Escaping
};

/** Inputs of FFightFishVisual::ComputeAnimState (the actor fills it each update). */
struct FFightFishAnimInput
{
	EFightFishPhase Phase = EFightFishPhase::Fighting;
	FName MoveId;
	bool bExhausted = false;
	/** Seconds since this fight's visual started (the hook set). */
	float SecondsSinceHook = 0.f;
	/** The fish actor's speed, cm/s, and its body length at its scale, cm. */
	float SpeedCmS = 0.f;
	float BodyLengthCm = 55.f;
	float WeightKg = 1.f;
	float ReferenceWeightKg = 1.f;
	/** Species look (DT_FishSpecies AnimRate, AnimAmplitude). */
	float AnimRate = 1.f;
	float AnimAmplitude = 1.f;
	/** The dart goes to the fish's right (else its left). */
	bool bDartRight = false;
	/** The fish's stamina 0..1 while it fights (T-059a). 1 = fresh (callers outside the fight keep the default). */
	float Stamina01 = 1.f;
};

/** The fish visual's rules (pure; tests call them directly). */
struct FFightFishVisual
{
	/** clamp((WeightKg / ReferenceWeightKg)^(1/3), MinScale, MaxScale); 1 if either weight is not positive and finite. */
	static float WeightScale(float WeightKg, float ReferenceWeightKg, const FFishVisualRow& Row);

	/** The role a fight move plays (Row.MoveRoles; UnknownMoveRole if not listed). */
	static EFishAnimRole RoleForMove(const FFishVisualRow& Row, FName MoveId);

	/** Tail beat of a speed-matched role, Hz; 0 = not speed-matched. */
	static float TailBeatHz(const FFishVisualRow& Row, EFishAnimRole Role);

	/** Stamina rate factor of a role: lerp(TiredRate, FreshRate, Stamina01) from Row.RoleStaminaRates; 1 if not listed. */
	static float StaminaRate(const FFishVisualRow& Row, EFishAnimRole Role, float Stamina01);

	/**
	 *  The clip state:
	 *    Landed -> Flop, alpha 1, "other" rate.   Escaping -> SwimFast, speed-matched rate.
	 *    Fighting: the first HookSetThrashTime s -> Thrash; exhausted -> SwimIdle at ExhaustedPlayRate, alpha x ExhaustedAmplitudeScale;
	 *    else RoleForMove(MoveId). Not exhausted (T-059a, effort not speed): rate AnimRate x weight factor x StaminaRate,
	 *    alpha AnimAmplitude x lerp(TiredAmplitudeScale, FreshAmplitudeScale, stamina).
	 *  Speed-matched rate (escape): AnimRate x Speed / (StrideBodyLengths x BodyLength x Hz) clamped to [MinPlayRate, MaxPlayRate].
	 *  Weight factor ("other" rate): (ReferenceWeight / Weight)^OtherRateWeightExponent. Alpha 0..1; Flop always 1.
	 */
	static FFishAnimState ComputeAnimState(const FFishVisualRow& Row, const FFightFishAnimInput& In);

	/** Depth of the fish's center under the surface, cm: SurfaceDepth + min(DepthShare x Depth, MaxShownDepth). */
	static float ShownDepth(const FFishVisualRow& Row, float FightDepthCm);

	/**
	 *  Where the fish's center goes: CenterForMouth(MouthTarget(...), Forward, MouthOffsetCm): the line end, MouthOffsetCm back
	 *  along Forward's horizontal part (so the nose is over the line end, pitched or not), at WaterZ - ShownDepth; never lower
	 *  than FloorZ + FloorClearance (FloorZ = -BIG_NUMBER: no floor), never above the surface.
	 */
	static FVector TargetLocation(const FFishVisualRow& Row, const FFightFishView& View, const FVector& Forward, float MouthOffsetCm, float FloorZ);

	/** The fight's line point: XY = the line end (where the mouth goes), Z = the fish's center height (as TargetLocation). */
	static FVector MouthTarget(const FFishVisualRow& Row, const FFightFishView& View, float FloorZ);

	/** The fish's center for a line point (XY of the mouth, Z of the center): XY - Forward.XY x MouthOffsetCm (Forward normalized in 3D), same Z. */
	static FVector CenterForMouth(const FVector& MouthPoint, const FVector& Forward, float MouthOffsetCm);

	/**
	 *  One smoothing step of the line point toward Target: exponential (SmoothAlpha(DeltaTime, SmoothTime)), then pulled so it
	 *  never trails Target by more than MouthMaxLagCm horizontally (the mouth stays under the bobber). Z is only smoothed.
	 */
	static FVector StepMouth(const FFishVisualRow& Row, const FVector& MouthPoint, const FVector& Target, float DeltaTime, float SmoothTime);

	/**
	 *  T-048, the fighting fish's facing: toward the player from its mouth (the body away from the rod), level. While the mouth
	 *  moves faster than MinFacingSpeed the body swings sideways: RunSwingDeg x min(1, speed / RunSwingFullSpeed), the head
	 *  toward the side it swims to (straight in or out: the side it already leans to), and the end that leads the swim follows
	 *  the climb or dive (pitch clamped to MaxPitchDeg). Tired: no swing, roll ExhaustedRollDeg (0 = upright).
	 */
	static FRotator FightFacing(const FFishVisualRow& Row, const FVector& MouthVelocity, const FVector& MouthLocation, const FVector& PlayerLocation,
		const FRotator& Current, bool bExhausted);

	/** Rotation with its yaw kept within RunSwingDeg (clamped to [0, 80]) of the direction mouth -> player; pitch and roll kept. */
	static FRotator ClampToLine(const FFishVisualRow& Row, const FRotator& Rotation, const FVector& MouthLocation, const FVector& PlayerLocation);

	/** Escaping fish's facing: along Velocity when faster than MinFacingSpeed (pitch from the climb, clamped), else away from the player, level. */
	static FRotator FacingRotation(const FFishVisualRow& Row, const FVector& Velocity, const FVector& FishLocation, const FVector& PlayerLocation,
		const FRotator& Current, bool bExhausted);

	/**
	 *  One step of a lost fish's escape: Location + (Direction x EscapeSpeed + down x EscapeSinkSpeed) x DeltaTime, but never
	 *  lower than FloorZ + FloorClearance (FloorZ = -BIG_NUMBER: no floor; FloorClearance < 0: no clamp) and, when the floor
	 *  clamp lifts it, never above WaterZ. A fish already under the floor is lifted to it.
	 */
	static FVector EscapeStep(const FFishVisualRow& Row, const FVector& Location, const FVector& Direction, float DeltaTime, float WaterZ, float FloorZ);

	/** Exponential smoothing factor for one step: 1 - exp(-DeltaTime / TimeConstant) (1 when the constant is 0). */
	static float SmoothAlpha(float DeltaTime, float TimeConstant);

	/** The species' look numbers (AnimAmplitude 0..1, AnimRate > 0, finite). OutProblem says what is wrong. */
	static bool ValidateSpeciesLook(const FFishSpeciesRow& Species, FString& OutProblem);
};

// Lure: the fish you see fighting on the line (T-029). A local, non-replicated actor that every machine spawns from the
// replicated fight (ULureFightFishSubsystem). Cosmetic only. Spec: docs/specs/fight-fish-visual.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/FishInstance.h"
#include "LureFightFish.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimInstance;

/** What a fighting fish is set up with (the subsystem fills it from DT_FishSpecies, DT_FishVisual and the settings). */
struct FLureFightFishSetup
{
	FFishVisualRow Row;
	FFishInstance Fish;
	/**
	 *  Species ReferenceWeight (kg; scale 1 at this weight), AnimRate and AnimAmplitude. Without a species row: 0 / 1 / 1,
	 *  and a ReferenceWeight of 0 (or a missing one) means scale 1 and the unscaled play rate (T029-B1).
	 */
	float ReferenceWeightKg = 0.f;
	float AnimRate = 1.f;
	float AnimAmplitude = 1.f;
	/** May be null (nothing drawn; the actor still moves and computes its clip state). */
	USkeletalMesh* Mesh = nullptr;
	UClass* AnimClass = nullptr;
	uint8 FightId = 0;
};

/**
 *  One hooked fish in the water. Set up once (Setup), then moved every frame from the fight's view (ApplyView): its mouth at
 *  the line's end (never more than MouthMaxLagCm behind it), just under the surface, deeper on dives, smoothed (more on
 *  machines without authority), facing the rod with its body away from the player, swung sideways while it swims (T-048).
 *  Its clip state (GetAnimState) is what ABP_Fish plays (UFishAnimInstance pulls it). After the fight: Landed plays the flop
 *  and waits for the hand-off (ULureFightFishSubsystem::OnFightFishLanded); Escaped swims away and is removed.
 */
UCLASS(Blueprintable, NotPlaceable)
class ALureFightFish : public AActor
{
	GENERATED_BODY()

public:

	ALureFightFish();

	void Setup(const FLureFightFishSetup& InSetup);

	/** One frame of the fight (DeltaTime > 0). */
	void ApplyView(const FFightFishView& View, float DeltaTime);

	/** The fight ended: Landed (flop at the line's end) or Escaped (swims away from AwayFrom, then IsFinished). */
	void BeginEnd(EFightFishEnd End, const FVector& AwayFrom);

	/** One frame after the fight (escape swim). */
	void TickAfterFight(float DeltaTime);

	/** An escaping fish has swum long enough (EscapeTime) and can be removed. */
	bool IsFinished() const;

	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	FFishAnimState GetAnimState() const { return AnimState; }

	/** Uniform scale from the fish's weight: (Weight / ReferenceWeight)^(1/3). */
	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	float GetFishScale() const { return FishScale; }

	/** The fish record (species, weight, rarity...). */
	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	FFishInstance GetFish() const { return Setup_.Fish; }

	/** Where the line ends on the fish: its MouthBone (or the body center), world space. For the line (T-032) and T-030. */
	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	FVector GetMouthLocation() const;

	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	USkeletalMeshComponent* GetMesh() const { return Mesh; }

	EFightFishPhase GetPhase() const { return Phase; }
	uint8 GetFightId() const { return Setup_.FightId; }
	FVector GetFishVelocity() const { return Velocity; }
	/** Where the fight wants the fish this frame (before smoothing). */
	FVector GetLastTarget() const { return LastTarget; }
	/** Body length at the current scale, cm, and the nose offset from the center at scale 1, cm. */
	float GetBodyLengthCm() const { return BodyLengthCm * FishScale; }
	float GetMouthOffsetCm() const { return MouthOffsetCm; }
	/** Seconds since Setup. */
	float GetSeconds() const { return Seconds; }
	const FFishVisualRow& GetRow() const { return Setup_.Row; }

private:

	UPROPERTY(VisibleAnywhere, Category="Lure|Fish")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	FLureFightFishSetup Setup_;
	FFishAnimState AnimState;
	EFightFishPhase Phase = EFightFishPhase::Fighting;
	float FishScale = 1.f;
	float BodyLengthCm = 55.f;
	float MouthOffsetCm = 0.f;
	float Seconds = 0.f;
	float EndSeconds = 0.f;
	FVector Velocity = FVector::ZeroVector;
	FVector LastTarget = FVector::ZeroVector;
	/** T-048: the smoothed line point while fighting (XY = the mouth, Z = the center height; FFightFishVisual::MouthTarget). */
	FVector MouthPoint = FVector::ZeroVector;
	FVector EscapeDirection = FVector::ForwardVector;
	/** The water surface of the last fight view (the escape's floor trace starts under it). */
	float LastWaterZ = 0.f;
	bool bHasWaterZ = false;
	bool bPlaced = false;
	bool bDartRight = false;
	bool bExhausted = false;
	/** The fight's stamina 0..1 (T-059a: the clip rate and alpha). */
	float Stamina01 = 1.f;
	EFishAnimRole LastRole = EFishAnimRole::SwimIdle;

	/** Bottom height under a point (FLureFishingSpots::TraceCast from just under the surface down past the shown depth and past BelowZ), or -BIG_NUMBER. */
	float TraceFloorZ(const FVector& At, float WaterZ, float BelowZ = UE_BIG_NUMBER) const;
	void UpdateAnim(EFightFishPhase InPhase, FName MoveId, const FVector& TurnToward);
	void MoveTo(const FVector& NewLocation, const FVector& PlayerLocation, float DeltaTime, float SmoothTime, bool bSnap);
};

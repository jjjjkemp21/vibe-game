// Lure: first-person arms anim instance (T-004). Parent class of ABP_FPArms (graph only, no logic).

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Character/LureMovementTypes.h"
#include "FPArmsAnimInstance.generated.h"

/**
 *  C++ parent of ABP_FPArms (skeleton SKEL_FPArms). The Blueprint child holds only the graph from
 *  SK_FPArms.anim.md: A_FPArms_Idle + A_FPArms_HoldRod_Idle -> Blend Poses by bool (bHoldingRod, 0.2 s)
 *  -> Slot DefaultSlot -> Slot StanceAdditive -> Output. ALurePlayerCharacter plays A_FPArms_StanceDip in the
 *  StanceAdditive slot on stance changes and landings.
 */
UCLASS(Transient, Blueprintable, BlueprintType)
class UFPArmsAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	/** True while the player holds the rod (set by the fishing code, T-006; false until then). */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	bool bHoldingRod = false;

	/** The owner's current stance (optional use in the graph). */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	ELureStance Stance = ELureStance::Stand;

	/** True while the owner is in the water (T-026; optional use in the graph, e.g. a swim pose). */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	bool bSwimming = false;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};

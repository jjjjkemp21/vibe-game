// Lure: first-person arms anim instance (T-004). Parent class of ABP_FPArms (graph only, no logic).

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Character/LureMovementTypes.h"
#include "FPArmsAnimInstance.generated.h"

/**
 *  C++ parent of ABP_FPArms (skeleton SKEL_FPArms). The Blueprint child holds only the graph from
 *  SK_FPArms.anim.md: four Sequence Players (A_FPArms_Idle, A_FPArms_HoldRod_Idle, A_FPArms_Prone_HoldRod_Idle,
 *  A_FPArms_Prone_TuckRod; sync group FPArmsBreath) -> Blend Poses by EFPArmsPose (ArmsPose; Standard Blend, Linear,
 *  every Blend Time bound to ArmsPoseBlendTime) -> Slot DefaultSlot -> Slot StanceAdditive -> Output.
 *  (The first version blended two players by bHoldingRod; that still works until the graph is rebuilt.)
 *  ALurePlayerCharacter plays A_FPArms_StanceDip in the StanceAdditive slot on stance changes and landings;
 *  fishing plays its cast/hook montages (if set) in DefaultSlot.
 */
UCLASS(Transient, Blueprintable, BlueprintType)
class UFPArmsAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	/** True while the player holds the rod (set by the fishing code, T-006; false until then). */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	bool bHoldingRod = false;

	/** Which loop to play (T-006): Idle, HoldRod, ProneHold, ProneTuck, from DT_Movement by stance and motion. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	EFPArmsPose ArmsPose = EFPArmsPose::Idle;

	/** Crossfade time for ArmsPose changes, seconds (bind every Blend Time pin of the pose blend to it). */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	float ArmsPoseBlendTime = 0.3f;

	/** The owner's current stance (optional use in the graph). */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	ELureStance Stance = ELureStance::Stand;

	/** True while the owner is in the water (T-026; optional use in the graph, e.g. a swim pose). */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Arms")
	bool bSwimming = false;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};

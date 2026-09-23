// Lure: swimming types (T-026). Spec: docs/specs/swimming.md.

#pragma once

#include "CoreMinimal.h"
#include "LureSwimTypes.generated.h"

/** ULureCharacterMovementComponent's MOVE_Custom sub-modes (CustomMovementMode). */
UENUM(BlueprintType)
enum class ELureCustomMovementMode : uint8
{
	None = 0 UMETA(Hidden),
	/** Pulling yourself out of the water onto an edge or up a ladder (a short predicted move along a planned path). */
	ClimbOut = 1
};

/**
 *  A planned climb out of the water. Worked out from the same state on the owning client and the server when Jump is
 *  pressed in the water, so the climb is predicted like a jump. World space, cm.
 */
USTRUCT(BlueprintType)
struct FLureClimbOutPlan
{
	GENERATED_BODY()

	/** Capsule center when the climb starts. */
	UPROPERTY(BlueprintReadOnly, Category="Lure|Swim")
	FVector Start = FVector::ZeroVector;

	/** Capsule center after rising straight up along the edge (same XY as Start). */
	UPROPERTY(BlueprintReadOnly, Category="Lure|Swim")
	FVector RiseTo = FVector::ZeroVector;

	/** Capsule center standing on the edge, where the climb ends. */
	UPROPERTY(BlueprintReadOnly, Category="Lure|Swim")
	FVector Target = FVector::ZeroVector;

	/** Height of the edge's top above the water surface, cm. */
	UPROPERTY(BlueprintReadOnly, Category="Lure|Swim")
	float LedgeHeight = 0.f;

	/** Climb speed along the path, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category="Lure|Swim")
	float Speed = 0.f;

	/** True if a ladder allowed this climb (the edge is higher than the row's ClimbOutMaxHeight). */
	UPROPERTY(BlueprintReadOnly, Category="Lure|Swim")
	bool bUsesLadder = false;
};

/** Fired when the player goes into the water (true) or stands on land again after swimming (false). Fishing listens (T-006). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLureSwimStateChangedSignature, bool, bSwimming);

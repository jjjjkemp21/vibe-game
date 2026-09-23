// Lure: ladder out of the water (T-026). Spec: docs/specs/swimming.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LureLadder.generated.h"

class UStaticMeshComponent;

/**
 *  A way out of the water where the edge is too high to climb (docks and rocks above DT_Movement's ClimbOutMaxHeight).
 *  A swimmer inside the grab zone who presses Jump climbs straight up to the edge above the ladder, as high as
 *  MaxClimbHeight above the water, whichever way they face.
 *
 *  Placement: the actor's origin is where the ladder meets the WATER SURFACE, on the face of the dock or rock; +X
 *  (forward, the arrow) points AWAY from the dock, out over the water. The grab zone is a box in front of it.
 *  The mesh is a greybox board (no collision); a thin Blueprint child may swap it.
 */
UCLASS(Blueprintable)
class ALureLadder : public AActor
{
	GENERATED_BODY()

public:

	ALureLadder();

	/** Highest edge above the water surface this ladder climbs to, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Ladder", meta=(ClampMin="0"))
	float MaxClimbHeight = 300.f;

	/** Half size of the grab zone, cm: X = how far out over the water it reaches (from the dock face), Y = half its width, Z = half its height around the water line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Ladder", meta=(ClampMin="1"))
	FVector GrabZoneHalfSize = FVector(40.0, 60.0, 120.0);

	/** Climb speed on this ladder, cm/s (0 = the swim row's ClimbOutSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Ladder", meta=(ClampMin="0"))
	float ClimbSpeed = 0.f;

	/** Greybox size: height above and depth below the water line, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Ladder", meta=(ClampMin="0"))
	float VisualHeightAboveWater = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Ladder", meta=(ClampMin="0"))
	float VisualDepthBelowWater = 120.f;

	/** True if a swimmer whose capsule center is at Point can use this ladder. */
	UFUNCTION(BlueprintPure, Category="Lure|Ladder")
	bool IsInGrabZone(const FVector& Point) const;

	/** Horizontal direction from the water toward the dock (the way the climber goes). */
	UFUNCTION(BlueprintPure, Category="Lure|Ladder")
	FVector GetClimbDirection() const;

	UFUNCTION(BlueprintPure, Category="Lure|Ladder")
	UStaticMeshComponent* GetLadderMesh() const { return LadderMesh; }

	virtual void OnConstruction(const FTransform& Transform) override;

private:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> LadderMesh;

	void UpdateVisual();
};

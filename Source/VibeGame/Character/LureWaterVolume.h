// Lure: water volume (T-026). The level builder places these over the water; spec docs/specs/swimming.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "LureWaterVolume.generated.h"

class UBoxComponent;

/**
 *  A box of water: a physics volume with bWaterVolume set, so characters switch to the engine's swimming mode when
 *  their capsule center is inside it (ULureCharacterMovementComponent then floats them at the surface, from DT_Movement).
 *
 *  The shape comes from two properties, not from a BSP brush, so scripts can place it exactly:
 *  - the actor's location is the CENTER OF THE WATER SURFACE (the top face of the box), e.g. the water plane's location;
 *  - SurfaceHalfSize = half the size in X and Y (cm), WaterDepth = how far the box reaches below the surface (cm).
 *  Actor yaw and scale apply as usual (keep pitch and roll at 0: the surface is flat).
 *
 *  The collision is an overlap-only box (profile OverlapAllDynamic): it never blocks traces, the camera or the bobber.
 *  An editor-only wireframe box shows the extent in the level editor.
 */
UCLASS(Blueprintable)
class ALureWaterVolume : public APhysicsVolume
{
	GENERATED_BODY()

public:

	ALureWaterVolume(const FObjectInitializer& ObjectInitializer);

	/** Half the water's size along the actor's X and Y, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water", meta=(ClampMin="1"))
	FVector2D SurfaceHalfSize = FVector2D(5000.0, 5000.0);

	/** How far the water reaches below the surface, cm. Make it reach the deepest seabed (going below it is harmless). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water", meta=(ClampMin="1"))
	float WaterDepth = 1000.f;

	/** Sets the size and rebuilds the collision at once (for scripts; editing the properties in the editor does the same). */
	UFUNCTION(BlueprintCallable, Category="Lure|Water")
	void SetWaterSize(FVector2D NewSurfaceHalfSize, float NewWaterDepth);

	/** World height of the water surface (the box's top face), cm. */
	UFUNCTION(BlueprintPure, Category="Lure|Water")
	float GetSurfaceHeight() const;

	/** True if Point is inside the water box (on or below the surface, above the bottom). */
	UFUNCTION(BlueprintPure, Category="Lure|Water")
	bool IsPointInWater(const FVector& Point) const;

	/** Rebuilds the box collision from SurfaceHalfSize and WaterDepth. */
	void RebuildWaterBody();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;

private:

#if WITH_EDITORONLY_DATA
	/** Editor-only wireframe of the water box (no collision, hidden in game). */
	UPROPERTY()
	TObjectPtr<UBoxComponent> EditorPreview;
#endif
};

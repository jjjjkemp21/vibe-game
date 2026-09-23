// Lure: water volume (T-026). The level builder places these over the water; spec docs/specs/swimming.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "LureWaterVolume.generated.h"

/**
 *  A box of water: a physics volume with bWaterVolume set, so characters switch to the engine's swimming mode when
 *  their capsule center is inside it (ULureCharacterMovementComponent then floats them at the surface, from DT_Movement).
 *
 *  The shape comes from two properties, so scripts can place it exactly:
 *  - the actor's location is the CENTER OF THE WATER SURFACE (the top face of the box), e.g. the water plane's location;
 *  - SurfaceHalfSize = half the size in X and Y (cm), WaterDepth = how far the box reaches below the surface (cm).
 *  Actor yaw and scale apply as usual (keep pitch and roll at 0 and the scale positive: the surface is flat).
 *
 *  One shape, two views of it (T-026 crash fix; docs/specs/swimming.md, "Water volume and the editor"):
 *  - Collision, every build: a transient box body on the brush component, overlap only (profile OverlapAllDynamic), so it
 *    never blocks traces, the camera or the bobber. The engine's physics-volume check, FindWaterLine and
 *    ULureCharacterMovementComponent::IsPointInWater all query this body. It is rebuilt whenever the components
 *    register, so it is never saved.
 *  - Editor brush, editor builds: like every volume, it keeps a BSP brush (the editor's brush code asserts on a volume
 *    without one), and the brush's 6 polygons are rewritten to the same box. So bounds, the wireframe, selection and the
 *    editor's own brush rebuilds all see the water box. Brush Settings and geometry-mode edits are overwritten:
 *    resize with SurfaceHalfSize and WaterDepth (or SetWaterSize).
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

	/**
	 *  Sets the size and rebuilds the collision (and the editor brush) at once. For scripts and runtime code; editing the
	 *  properties in the editor does the same. Safe in editor worlds and at runtime (no editor-only calls).
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Water")
	void SetWaterSize(FVector2D NewSurfaceHalfSize, float NewWaterDepth);

	/** World height of the water surface (the box's top face), cm. */
	UFUNCTION(BlueprintPure, Category="Lure|Water")
	float GetSurfaceHeight() const;

	/** True if Point is inside the water box (on or below the surface, above the bottom). */
	UFUNCTION(BlueprintPure, Category="Lure|Water")
	bool IsPointInWater(const FVector& Point) const;

	/**
	 *  Half size of the water box in actor space, cm: X and Y from SurfaceHalfSize, Z = WaterDepth / 2. Sizes are used
	 *  clamped to 1 cm .. 100 km (NaN or infinite: 1 cm), so bad data never reaches the physics engine. The box's top face
	 *  is at the actor's origin, so its center is HalfExtent.Z below it.
	 */
	FVector GetWaterBoxHalfExtent() const;

	/** Rebuilds the collision box, and in the editor the brush, from SurfaceHalfSize and WaterDepth. */
	void RebuildWaterBody();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;

	/**
	 *  The editor calls this at the end of every brush rebuild (FBSPOps::csgPrepMovingBrush: placing from the class
	 *  or a script, paste, undo/redo, Build), right after it replaced the collision with the brush's own convex hulls,
	 *  which our never-cooked body can't hold. Puts the water box back. (ANavModifierVolume uses the same hook.)
	 */
	virtual void RebuildNavigationData() override;

private:

#if WITH_EDITOR
	/**
	 *  Makes the editor brush the water box: rewrites its polygons when they differ, and creates the brush in editor
	 *  worlds when it is missing (spawned without an actor factory). Game worlds don't need one: without a brush the
	 *  bounds come from the collision body. Never removes the brush.
	 */
	void SyncEditorBrush(const FVector& HalfExtent);
#endif
};

// Lure: the look of a hot spot (T-027). The C++ class is a minimal, plainly visible placeholder (foam rings that spread and
// bubbles that pop, from engine basic shapes). Real VFX: a thin Blueprint child (asset references and defaults only, no
// graph logic) that adds a Niagara system and sets bDrawPlaceholder false, named in the DT_HotSpot row's VisualClass.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Fishing/FishingWaterTypes.h"
#include "LureHotSpotVisualComponent.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

/**
 *  Draws one hot spot on every machine except a dedicated server. ALureHotSpot creates it (class from the row) and calls
 *  InitFromHotSpot with the replicated state; it follows the actor (which drifts) and scales with the hot spot's fade.
 *  Placeholder: RingCount rings of DotsPerRing foam dots that spread from a quarter of the radius to the full radius
 *  every RingPeriod seconds, sinking as they spread, plus BubbleCount bubbles that rise and pop in the middle. Tint =
 *  the row's VisualColor (the material's ColorParameter).
 */
UCLASS(ClassGroup=(Lure), Blueprintable, meta=(BlueprintSpawnableComponent))
class ULureHotSpotVisualComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	ULureHotSpotVisualComponent();

	/** Draw the C++ placeholder (a VFX child sets this false). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder")
	bool bDrawPlaceholder = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder")
	TSoftObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder")
	TSoftObjectPtr<UMaterialInterface> PlaceholderMaterial;

	/** Vector parameter of the material that gets the tint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder")
	FName ColorParameter = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="0", ClampMax="4"))
	int32 RingCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="3", ClampMax="64"))
	int32 DotsPerRing = 18;

	/** Width of a foam dot at the full radius, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="1"))
	float DotSize = 22.f;

	/** Height of a fresh foam dot, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="1"))
	float DotHeight = 9.f;

	/** Seconds for a ring to spread out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="0.1"))
	float RingPeriod = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="0", ClampMax="32"))
	int32 BubbleCount = 7;

	/** Bubble diameter at its biggest, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="1"))
	float BubbleSize = 18.f;

	/** Seconds from a bubble rising to it popping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="0.1"))
	float BubblePeriod = 1.4f;

	/** Bubbles appear within this share of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder", meta=(ClampMin="0", ClampMax="1"))
	float BubbleSpread = 0.5f;

	/** Height of the effect above the water surface, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placeholder")
	float HeightAboveWater = 2.f;

	/** The hot spot's state (radius, tint, seed, times). Called by ALureHotSpot when the state is known. */
	virtual void InitFromHotSpot(const FLureHotSpotState& InState);

	/** The state it was built from (tests, VFX children). */
	const FLureHotSpotState& GetHotSpotState() const { return HotSpot; }

	/** The placeholder parts (tests): rings then bubbles; empty if not drawn. */
	TArray<UInstancedStaticMeshComponent*> GetPlaceholderParts() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void OnUnregister() override;

private:

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Rings;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Bubbles;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FLureHotSpotState HotSpot;
	TArray<FVector2D> BubbleSpots;
	TArray<float> BubblePhases;
	bool bBuilt = false;

	void BuildPlaceholder();
	void DestroyPlaceholder();
	void UpdatePlaceholder(double Time, float Fade);
	UInstancedStaticMeshComponent* MakePart(UStaticMesh* Mesh);
};

// Lure: a painted water area (T-027). The level builder places these from the layouts' "water_area" markers
// (Content/Python/levels/build_level.py); rules: docs/specs/fishing-water-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/FishingWaterTypes.h"
#include "LureWaterArea.generated.h"

class ULineBatchComponent;

/**
 *  A water area: the habitat, region and luck of the water inside its outline (and optionally a depth band). Every body
 *  of water can be fished; areas only decide which fish live where. Where areas overlap, the higher Priority wins (then
 *  the smaller area, then the lower id). Water outside every area uses ULureWaterSettings::DefaultWaterHabitat.
 *
 *  The outline is 2D (a column from the surface down): Circle (Radius), Box (BoxHalfSize, turned by the actor's yaw),
 *  Polygon (PolygonPoints in the actor's local X/Y, turned by its yaw) or Everywhere. Actor scale is ignored.
 *  Scripts set everything through the Set* functions (the level builder does), so tags and polygons are converted once.
 *  Not replicated: it is level data that every machine loads with the map. The editor draws the outline (an editor-only
 *  line component, hidden in game); in PIE the dev command Lure.Water.Show draws every area.
 */
UCLASS(Blueprintable)
class ALureWaterArea : public AActor
{
	GENERATED_BODY()

public:

	ALureWaterArea();

	/** Id (the layout's "id"). Empty = the actor's name. Hot spots count per area id. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area")
	FName AreaId;

	/** Name shown on the HUD ("Water: Reef Flats"). Empty = the id. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area")
	FString DisplayName;

	/** Habitat of this water (which species live here). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area", meta=(Categories="Habitat"))
	FGameplayTag HabitatTag;

	/** Region (empty = ULureFishingSettings::DefaultRegion). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area", meta=(Categories="Region"))
	FGameplayTag RegionTag;

	/** Higher wins where areas overlap. The level's default water ("everywhere") usually has the lowest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area")
	int32 Priority = 0;

	/** Added to the rarity luck of bites in this water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area", meta=(ClampMin="0"))
	float Luck = 0.f;

	/** Depth band, cm: the area only counts where MinDepth <= water depth < MaxDepth (MaxDepth 0 = no limit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area", meta=(ClampMin="0"))
	float MinDepth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area", meta=(ClampMin="0"))
	float MaxDepth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area|Shape")
	ELureWaterAreaShape Shape = ELureWaterAreaShape::Circle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area|Shape", meta=(ClampMin="1", EditCondition="Shape == ELureWaterAreaShape::Circle", EditConditionHides))
	float Radius = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area|Shape", meta=(EditCondition="Shape == ELureWaterAreaShape::Box", EditConditionHides))
	FVector2D BoxHalfSize = FVector2D(1000.0, 1000.0);

	/** Outline points in the actor's local X/Y (cm), in order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Water Area|Shape", meta=(EditCondition="Shape == ELureWaterAreaShape::Polygon", EditConditionHides))
	TArray<FVector2D> PolygonPoints;

	/** Sets the habitat and region from tag names. False (and a warning) if a name is not a registered tag; Region None = none. */
	UFUNCTION(BlueprintCallable, Category="Lure|Water Area")
	bool SetAreaTags(FName Habitat, FName Region);

	UFUNCTION(BlueprintCallable, Category="Lure|Water Area")
	void SetShapeCircle(float InRadius);

	UFUNCTION(BlueprintCallable, Category="Lure|Water Area")
	void SetShapeBox(FVector2D HalfSize);

	/** Polygon from WORLD X/Y points (converted to the actor's local frame, so place and turn the actor first). Returns the point count. */
	UFUNCTION(BlueprintCallable, Category="Lure|Water Area")
	int32 SetShapePolygon(const TArray<FVector2D>& WorldPoints);

	UFUNCTION(BlueprintCallable, Category="Lure|Water Area")
	void SetShapeEverywhere();

	/** The area in world space (what the fishing rules use). */
	UFUNCTION(BlueprintPure, Category="Lure|Water Area")
	FLureWaterAreaInfo GetWaterArea() const;

	/** World points of the outline at the water (a circle as 64 segments; empty for Everywhere). For drawing only. */
	TArray<FVector> GetOutlinePoints(float Z) const;

	/** A color per habitat (the same habitat always gets the same color), for the outlines. */
	static FLinearColor GetHabitatColor(const FGameplayTag& Habitat);

	/** Redraws the editor outline. */
	void RefreshOutline();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostLoad() override;

private:

	UPROPERTY(VisibleAnywhere, Category="Lure|Water Area")
	TObjectPtr<USceneComponent> Root;

	/** Editor-only outline (hidden in game). */
	UPROPERTY(VisibleAnywhere, Category="Lure|Water Area")
	TObjectPtr<ULineBatchComponent> Outline;
};

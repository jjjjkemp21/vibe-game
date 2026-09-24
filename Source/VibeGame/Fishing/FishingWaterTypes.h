// Lure: water areas and hot spots (T-027 "fish anywhere + hot spots"). Rules: docs/specs/fishing-water-rules.md.
// Data: water areas come from the level layouts (data/levels/*.json "water_area" markers -> ALureWaterArea actors);
// hot spot types are DT_HotSpot rows (data/tables/DT_HotSpot.json, row struct FLureHotSpotRow).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/NetSerialization.h"
#include "GameplayTagContainer.h"
#include "FishingWaterTypes.generated.h"

class ULureHotSpotVisualComponent;

/** Log category for water areas, depth, the bite habitat and hot spots. */
DECLARE_LOG_CATEGORY_EXTERN(LogLureWater, Log, All);

/** Log category for the hot spot spawner (ALureHotSpotSpawner): `Log LogLureHotSpot Verbose` in the console logs every spawn
 *  check (what each row and area did, why candidate points failed); VeryVerbose adds each pair's reason. */
DECLARE_LOG_CATEGORY_EXTERN(LogLureHotSpot, Log, All);

/** The outline of a water area on the water (2D, X/Y; every area is a column from the surface down). */
UENUM(BlueprintType)
enum class ELureWaterAreaShape : uint8
{
	/** Center + Radius. */
	Circle,
	/** Center + half size in the area's own X/Y, turned by its yaw. */
	Box,
	/** Three or more points (world X/Y), any simple outline (concave is fine). */
	Polygon,
	/** All water. Used for a level's default water (low priority), optionally limited by a depth band. */
	Everywhere
};

/** Where the bobber's water context came from. */
UENUM(BlueprintType)
enum class ELureWaterSource : uint8
{
	/** Not water: the bobber is on land. */
	None,
	/** A painted water area (ALureWaterArea, from a layout "water_area"). */
	Area,
	/** A fishing_spot marker read as a circle area (migration only: levels that have no water area at all). */
	LegacySpot,
	/** Water that no area covers: ULureWaterSettings::DefaultWaterHabitat. */
	Default
};

/** Why nothing can bite at the bobber right now (None = something can). */
UENUM(BlueprintType)
enum class ELureNoBiteReason : uint8
{
	None,
	/** The bobber is not on water (a dock, a beach, a rock). */
	NotWater,
	/** The water under the bobber is shallower than ULureWaterSettings::MinBiteDepth. */
	TooShallow,
	/** No species lives in this water at this time, even with the gap fallback habitats (a data gap), or no fish tables. */
	NoSpecies,
	/** Species live here at this time, but none of them takes the bait on the hook. */
	WrongBait
};

/**
 *  One water area in world space: where it is, which habitat and region its water has, and how it ranks against
 *  overlapping areas. Built from an ALureWaterArea actor (GetWaterArea) or a legacy fishing_spot marker.
 *  Pure data plus pure geometry, so the priority rule is unit-testable without a world.
 */
USTRUCT(BlueprintType)
struct FLureWaterAreaInfo
{
	GENERATED_BODY()

	/** Id (layout "id"; an actor without one uses its object name). Hot spots count per area id. */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	FName AreaId;

	/** Name for the HUD ("Reef Flats"). Empty = the id. */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	FString DisplayName;

	/** Habitat of the water (Habitat.*). Invalid = the area is skipped (never wins; a validation problem). */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	FGameplayTag HabitatTag;

	/** Region (Region.*). Invalid = ULureFishingSettings::DefaultRegion. */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	FGameplayTag RegionTag;

	/** Overlaps: the higher priority wins; on a tie the smaller area, then the lower id (FName::LexicalLess). */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	int32 Priority = 0;

	/** Added to the bite's rarity luck (fish-system-rules stage 3). */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	float Luck = 0.f;

	/** Depth band in cm: the area only counts where MinDepth <= depth < MaxDepth. MaxDepth <= 0 = no upper limit. */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	float MinDepth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	float MaxDepth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	ELureWaterAreaShape Shape = ELureWaterAreaShape::Everywhere;

	/** World X/Y of the circle or box center (the actor's location; for polygons only the label point). */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	FVector2D Center = FVector2D::ZeroVector;

	/** Box: turns the box's X/Y axes (degrees, like an actor yaw). */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	float YawDegrees = 0.f;

	/** Circle radius, cm. */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	float Radius = 0.f;

	/** Box half size along its own X/Y, cm. */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	FVector2D HalfSize = FVector2D::ZeroVector;

	/** Polygon outline, world X/Y, in order (either winding). */
	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	TArray<FVector2D> Polygon;

	UPROPERTY(BlueprintReadOnly, Category="Water Area")
	ELureWaterSource Source = ELureWaterSource::Area;

	/** A usable outline: radius > 0, half size > 0, >= 3 polygon points with a non-zero area; Everywhere always. */
	bool HasShape() const;

	/** The outline contains XY (edges count for circles and boxes; polygons use the even-odd rule). */
	bool Contains(const FVector2D& XY) const;

	/** MinDepth <= Depth < MaxDepth (MaxDepth <= 0: no upper limit). Non-finite depth never matches. */
	bool AcceptsDepth(float DepthCm) const;

	/** Surface area in cm^2 (the tie-break); Everywhere = infinite, no shape = 0. */
	double GetSize() const;

	/** 2D bounds of the outline (IsValid = false for Everywhere or no shape). */
	FBox2D GetBounds() const;

	/** DisplayName, else the id. */
	FString GetLabel() const;
};

/** The water the bobber rests on (server): which area, habitat, region, depth and area luck. */
USTRUCT(BlueprintType)
struct FLureWaterContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Water")
	bool bOnWater = false;

	UPROPERTY(BlueprintReadOnly, Category="Water")
	ELureWaterSource Source = ELureWaterSource::None;

	/** The winning area (None = default water, or land). */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	FName AreaId;

	UPROPERTY(BlueprintReadOnly, Category="Water")
	FString AreaName;

	/** The water's habitat (the area's, or the default water habitat). The bite may use a gap fallback instead (see the spec). */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	FGameplayTag HabitatTag;

	/** The area's region; invalid = the settings' DefaultRegion. */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	FGameplayTag RegionTag;

	/** The area's luck (0 for default water). */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float Luck = 0.f;

	/** Water depth under the bobber, cm (the surface down to the first solid ground; ULureWaterSettings::DepthProbe if none). */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float DepthCm = 0.f;

	/** The water surface height, cm. */
	UPROPERTY(BlueprintReadOnly, Category="Water")
	float WaterZ = 0.f;
};

/** What a hot spot adds to the bites of a cast that landed in it (copied from its DT_HotSpot row at spawn). */
USTRUCT(BlueprintType)
struct FLureHotSpotBonus
{
	GENERATED_BODY()

	/** DT_HotSpot row (None = no hot spot). */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	FName TypeId;

	/** Added to the rarity luck. */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	float LuckBonus = 0.f;

	/** FFishRollContext::SizeBonus: each natural weight roll moves this share of the way to the species' WeightMax. */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	float SizeBonus = 0.f;

	/** FFishRollContext::ValueMultiplier (1 = none). */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	float ValueMultiplier = 1.f;

	/** Multiplies the wait until a bite (and a rebite after a miss): < 1 = bites come sooner. */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	float BiteWaitScale = 1.f;

	bool IsActive() const { return !TypeId.IsNone(); }
};

/**
 *  The water part of the replicated fishing state (inside FLureFishingNetState): what the HUD needs on every machine.
 *  Written by the server when the bobber lands and at every bite check.
 */
USTRUCT(BlueprintType)
struct FLureBobberWater
{
	GENERATED_BODY()

	/** The hot spot the bobber landed in (DT_HotSpot row), None = none. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	FName HotSpotType;

	/** Why nothing can bite here now (with FLureFishingNetState::bNoFishHere). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	ELureNoBiteReason NoBiteReason = ELureNoBiteReason::None;
};

/**
 *  One hot spot type of DT_HotSpot (source data/tables/DT_HotSpot.json; row name = type id, e.g. "Bubbles").
 *  The struct defaults ARE the shipped "Bubbles" row: without the table the game uses them (one warning), and a test
 *  checks they match. Units: cm, seconds.
 */
USTRUCT(BlueprintType)
struct FLureHotSpotRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Name ("Bubbling water"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hot Spot")
	FText DisplayName = FText::FromString(TEXT("Bubbling water"));

	/** Placeholder HUD line while the bobber is in one ("Bubbling water: better fish here"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hot Spot")
	FText HudText = FText::FromString(TEXT("Bubbling water: better fish here"));

	// ---- Spawning ----

	/** Mean seconds between spawns of this type in one area while the area is under MaxPerArea (0 = never spawns). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float SpawnInterval = 40.f;

	/** Most hot spots of this type alive at once in one water area (the level's default water counts as one area). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	int32 MaxPerArea = 1;

	/** A new hot spot keeps at least this far from every other one (any type), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float MinSpacing = 1500.f;

	/** Size on the water: a bobber within this distance of its center is in it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="1"))
	float Radius = 250.f;

	/** It lasts a random time in [LifetimeMin, LifetimeMax] seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="1"))
	float LifetimeMin = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="1"))
	float LifetimeMax = 240.f;

	/** It wanders at about this speed (cm/s) ... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float DriftSpeed = 15.f;

	/** ... never farther than this from where it appeared, cm (0 = it stays put). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float DriftRange = 400.f;

	/** Only where the water is at least this deep, cm ... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float MinDepth = 60.f;

	/** ... and shallower than this (0 = no limit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float MaxDepth = 0.f;

	/** Water habitats it appears in (hierarchical: Habitat.Reef also allows Habitat.Reef.Edge). Empty = any water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(Categories="Habitat"))
	TArray<FGameplayTag> AllowedHabitats;

	// ---- Bonuses for a cast that lands in it ----

	/** Added to the rarity luck (rarer fish). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bonus", meta=(ClampMin="0"))
	float LuckBonus = 1.5f;

	/** Bigger fish: each natural weight roll moves this share (0..1) of the way to the species' WeightMax. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bonus", meta=(ClampMin="0", ClampMax="1"))
	float SizeBonus = 0.15f;

	/** More valuable fish: multiplies the rolled sell value (1 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bonus", meta=(ClampMin="0.01"))
	float ValueMultiplier = 1.25f;

	/** Multiplies the wait for a bite (< 1 = the fish bite sooner). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bonus", meta=(ClampMin="0.01"))
	float BiteWaitScale = 0.7f;

	// ---- Look (placeholder until the editor-operator's VFX) ----

	/** Tint of the placeholder visual (and a hint for real VFX). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look")
	FLinearColor VisualColor = FLinearColor(0.94f, 0.97f, 0.97f, 1.f);

	/** The visual component class (a thin Blueprint child of ULureHotSpotVisualComponent with real VFX). None = the C++ placeholder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look")
	TSoftClassPtr<ULureHotSpotVisualComponent> VisualClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hot Spot", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;

	/** Every problem with the row's numbers and tags (empty = usable). RowId names the row in the messages. */
	TArray<FString> Validate(FName RowId) const;

	/** It may appear in water of this habitat and depth (AllowedHabitats hierarchical; the depth band). */
	bool AllowsWater(const FGameplayTag& Habitat, float DepthCm) const;
};

/** A hot spot as every machine sees it (replicated once; the drift is a pure function of time, so nothing else is sent). */
USTRUCT(BlueprintType)
struct FLureHotSpotState
{
	GENERATED_BODY()

	/** DT_HotSpot row. */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	FName TypeId;

	/** The water area it belongs to (None = the level's default water). */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	FName AreaId;

	/** Where it appeared (water surface), quantized to 0.1 cm on the server too, so every machine drifts from the same point. */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	FVector_NetQuantize10 Anchor;

	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	float Radius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	float DriftRange = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	float DriftSpeed = 0.f;

	/** Drift phases (FLureWaterRules::DriftOffset). */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	int32 Seed = 0;

	/** Server time (FLureWaterQuery::GetTime) it appeared and ends. */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	double SpawnTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	double EndTime = 0.0;

	/** Placeholder tint. */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	FColor Color = FColor::White;

	/** The visual component class (None = the C++ placeholder). */
	UPROPERTY(BlueprintReadOnly, Category="Hot Spot")
	TSoftClassPtr<ULureHotSpotVisualComponent> VisualClass;
};

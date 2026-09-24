// Lure: water and hot spot settings (T-027). Project Settings > Game > Lure Water; stored in Config/DefaultGame.ini.
// Rules: docs/specs/fishing-water-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "LureWaterSettings.generated.h"

class UDataTable;

/**
 *  World rules for "fish anywhere" and hot spots in [/Script/VibeGame.LureWaterSettings] (DefaultGame.ini). Per-level
 *  data (water areas, which hot spot types spawn) lives in the level layouts; hot spot types are DT_HotSpot rows.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Lure Water"))
class ULureWaterSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULureWaterSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ---- Water areas and the bite ----

	/** Habitat of water that no water area covers (a gameplay tag name). Every body of water can be fished. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Water")
	FName DefaultWaterHabitat;

	/**
	 *  Data-gap safety net: when no species of the water's habitat can bite at this hour (bait ignored), the bite uses the
	 *  first habitat of this list that has one, and logs the gap once. Empty = no fallback (nothing bites in a gap).
	 *  Bait never triggers it: wrong bait is the player's choice (the HUD says so).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Water")
	TArray<FName> GapFallbackHabitats;

	/** Nothing bites in water shallower than this, cm (the HUD says "too shallow"). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0"))
	float MinBiteDepth = 15.f;

	/** Depth is measured down to this far below the surface; no ground within it counts as this deep, cm. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="1"))
	float DepthProbe = 5000.f;

	/**
	 *  Migration: a level with no water area at all reads its fishing_spot markers (Lure.FishingSpot) as circle areas, so
	 *  levels built before the water-area pass keep their habitats. As soon as a level has one water area, markers are ignored.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Water")
	bool bLegacySpotsWhenNoAreas = true;

	/** Priority of those legacy spot areas. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Water")
	int32 LegacySpotPriority = 0;

	// ---- Hot spots ----

	/** Hot spot types (row struct LureHotSpotRow; source data/tables/DT_HotSpot.json). Missing = the built-in "Bubbles" row + one warning. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hot Spots", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureHotSpotRow"))
	TSoftObjectPtr<UDataTable> HotSpotTable;

	/** Seconds between spawn checks of a level's hot spot spawner. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hot Spots", meta=(ClampMin="0.1"))
	float HotSpotCheckInterval = 1.f;

	/** The first check after the level starts counts as this many seconds, so some hot spots are there at once. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hot Spots", meta=(ClampMin="0"))
	float HotSpotPrewarmSeconds = 45.f;

	/** Most hot spots alive at once in a level (a spawner may lower it). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hot Spots", meta=(ClampMin="0"))
	int32 MaxHotSpots = 16;

	/** Unbounded water (the default water, "everywhere" areas) gets hot spots within this distance of a player, cm. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hot Spots", meta=(ClampMin="0"))
	float OpenWaterSpawnRadius = 3000.f;

	/** Random points tried per spawn before giving up until the next check. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hot Spots", meta=(ClampMin="1"))
	int32 HotSpotSpawnTries = 12;

	/** The visual grows in and fades out over this many seconds at the start and end of a hot spot's life. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Hot Spots", meta=(ClampMin="0"))
	float HotSpotFadeSeconds = 1.5f;

	/** DefaultWaterHabitat as a tag (invalid if unregistered). */
	FGameplayTag GetDefaultWaterHabitatTag() const;

	/** GapFallbackHabitats as tags (unregistered names are skipped). */
	TArray<FGameplayTag> GetGapFallbackTags() const;
};

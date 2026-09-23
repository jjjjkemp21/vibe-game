// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Fish/FishTypes.h"
#include "FishSettings.generated.h"

class UDataTable;
struct FFishTables;

/**
 *  Project Settings > Game > Fish System, saved in [/Script/VibeGame.FishSettings] in DefaultGame.ini.
 *  Points at the four fish DataTables (imported from data/tables/*.json) and holds the luck/XP tuning and the level hook.
 *  Only the settings-based wrappers read this; the core (FFishRoll) takes everything as parameters.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Fish System"))
class UFishSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UFishSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** /Game/Data/DT_FishSpecies (row struct FishSpeciesRow), source data/tables/DT_FishSpecies.json */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Tables")
	TSoftObjectPtr<UDataTable> SpeciesTable;

	/** /Game/Data/DT_FishRarity (row struct FishRarityRow), source data/tables/DT_FishRarity.json */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Tables")
	TSoftObjectPtr<UDataTable> RarityTable;

	/** /Game/Data/DT_FishModifier (row struct FishModifierRow), source data/tables/DT_FishModifier.json */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Tables")
	TSoftObjectPtr<UDataTable> ModifierTable;

	/** /Game/Data/DT_FishStat (row struct FishStatRow), source data/tables/DT_FishStat.json */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Tables")
	TSoftObjectPtr<UDataTable> StatTable;

	/** Luck and XP tuning passed to the roll */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Roll")
	FFishRollTuning RollTuning;

	/** Fish level vs player level difficulty (read by the reel fight, T-007, and escapes, T-010) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Level Scaling")
	FFishLevelScaling LevelScaling;

	/**
	 *  Loads the four tables synchronously into OutTables (with RollTuning). Game thread only.
	 *  Graceful: a missing or wrong table returns false with a readable OutError (it checks the package exists first,
	 *  so no load errors are logged); tables that did load are still set. The caller keeps the tables referenced if it
	 *  holds OutTables across frames.
	 */
	bool LoadTables(FFishTables& OutTables, FString& OutError) const;
};

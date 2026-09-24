// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Fish/FishTypes.h"
#include "FishInstance.generated.h"

/**
 *  One caught (or hooked) fish: the record produced by the roll pipeline (FFishRoll::Roll).
 *  The cooler, selling, journal records, trophies, requests and save files all carry this record.
 *
 *  Replication-friendly: only value UPROPERTYs and arrays (no TMap/TSet, no UObject pointers, no FText), so it
 *  replicates as a member of a replicated actor/component or inside an array. Rolls run on the server only.
 *  SaveGame-friendly: every field is UPROPERTY(SaveGame). The record is authoritative: loading never recomputes it,
 *  so later data rebalancing never changes a fish the player already has. An id that no longer exists in the tables
 *  keeps its record (UI shows "Unknown").
 */
USTRUCT(BlueprintType)
struct FFishInstance
{
	GENERATED_BODY()

	/** DT_FishSpecies row name (None = empty slot, not a fish) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	FName SpeciesId;

	/** DT_FishRarity row name */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	FName RarityId;

	/** DT_FishModifier row names, in FName::LexicalLess order, no duplicates */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	TArray<FName> ModifierIds;

	/** Final weight in kg (after modifiers and clamping). Same as GetStat(Fish.Stat.Weight). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	float WeightKg = 0.0f;

	/** Species BaseLevel + rarity LevelBonus (>= 1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	int32 Level = 0;

	/** Every stat registered in DT_FishStat (including Fish.Stat.Weight), final values, sorted by tag name (FName::LexicalLess) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	TArray<FFishStatValue> Stats;

	/** Sell value in coins (>= 1 for a rolled fish) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	int32 Value = 0;

	/** XP for landing it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	int32 Xp = 0;

	/** UI hint: mean over difficulty stats of (final value / species base value); 1.0 = an average fish of its species */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	float DifficultyRating = 0.0f;

	/** Seed of the roll: same tables + context + seed = the same fish */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Fish")
	int32 Seed = 0;

	/** Stat value by exact tag match, or Default if the fish doesn't have it */
	float GetStat(const FGameplayTag& StatTag, float Default = 0.0f) const;

	/** True if the stat is present (exact tag match) */
	bool HasStat(const FGameplayTag& StatTag) const;

	bool HasModifier(FName ModifierId) const { return !ModifierId.IsNone() && ModifierIds.Contains(ModifierId); }

	/** True for a rolled fish (has a species); a default instance is an empty slot */
	bool IsValid() const { return !SpeciesId.IsNone(); }

	/** One-line summary for logs */
	FString ToString() const;
};

/**
 *  Input of the roll and the bite picker. Filled on the server by the water (area, hot spot) + gear + player (T-006, T-027).
 *  Roll uses: SpeciesId, Seed, Luck, SizeBonus, ValueMultiplier, RegionTag, TimeOfDayHours, WeatherTag (modifier
 *  conditions) and the Forced* overrides.
 *  PickSpecies uses: Seed, RegionTag, HabitatTag, TimeOfDayHours, WeatherTag, BaitTag.
 *  The same Seed can be used for both (they use different sub-streams).
 */
USTRUCT(BlueprintType)
struct FFishRollContext
{
	GENERATED_BODY()

	/** Species to roll (from PickSpecies) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish")
	FName SpeciesId;

	/** Any int32 is valid (0 is not special). Comes from a server RNG at bite time (FFishRoll::MakeRandomSeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish")
	int32 Seed = 0;

	/** >= 0; clamped to [0, FFishRollTuning::MaxLuck], NaN counts as 0. Only affects rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish")
	float Luck = 0.0f;

	/**
	 *  Bigger fish (T-027 hot spots): the natural weight roll moves this share of the way to the species' WeightMax
	 *  (0 = none, 1 = always the max). Clamped to [0, 1]; NaN counts as 0. Ignored with bForceWeightFraction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish", meta=(ClampMin="0", ClampMax="1"))
	float SizeBonus = 0.0f;

	/** More valuable fish (T-027 hot spots): multiplies the value with the rarity and modifier multipliers (1 = none; must be > 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish", meta=(ClampMin="0.01"))
	float ValueMultiplier = 1.0f;

	/** Where the player is (e.g. Region.Tropical.PalmKey) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish", meta=(Categories="Region"))
	FGameplayTag RegionTag;

	/** Habitat of the fishing spot (e.g. Habitat.Reef) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish", meta=(Categories="Habitat"))
	FGameplayTag HabitatTag;

	/** Time of day in hours [0, 24); other values are wrapped into range (24.0 -> 0.0), NaN counts as 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish")
	float TimeOfDayHours = 12.0f;

	/** Current weather (optional) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish", meta=(Categories="Weather"))
	FGameplayTag WeatherTag;

	/** Bait/hook on the line (optional) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish", meta=(Categories="Bait,Hook"))
	FGameplayTag BaitTag;

	/** Forced overrides (tests, dev command Lure.GiveFish). Use this rarity instead of rolling (None = roll). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish|Forced")
	FName ForcedRarityId;

	/** Use ForcedModifierIds instead of rolling (skips chance, conditions, groups and MaxModifiers; order and duplicates are normalized) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish|Forced")
	bool bForceModifiers = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish|Forced")
	TArray<FName> ForcedModifierIds;

	/** Use ForcedWeightFraction instead of the weight roll */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish|Forced")
	bool bForceWeightFraction = false;

	/** Position in the species range (0 = WeightMin, 1 = WeightMax, no skew): Weight = WeightMin + (WeightMax - WeightMin) * Fraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fish|Forced", meta=(ClampMin="0", ClampMax="1"))
	float ForcedWeightFraction = 0.0f;
};

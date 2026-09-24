// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"
#include "FishTypes.generated.h"

class UStreamableRenderAsset;
class USkeletalMesh;

/** Fish system log. Data problems are Warnings (never check/ensure); "nothing bites here" is not logged. */
DECLARE_LOG_CATEGORY_EXTERN(LogLureFish, Log, All);

/**
 *  Fish system data model (T-008). Rules: docs/specs/fish-system-rules.md. Pipeline: FishRoll.h.
 *
 *  Four DataTables, JSON sources in the repo, imported by the editor-operator (row name = id):
 *    data/tables/DT_FishStat.json     -> /Game/Data/DT_FishStat     (FFishStatRow)     the stat registry
 *    data/tables/DT_FishSpecies.json  -> /Game/Data/DT_FishSpecies  (FFishSpeciesRow)
 *    data/tables/DT_FishRarity.json   -> /Game/Data/DT_FishRarity   (FFishRarityRow)
 *    data/tables/DT_FishModifier.json -> /Game/Data/DT_FishModifier (FFishModifierRow)
 *  A new species, rarity tier, modifier or stat is a new row (plus, for a new tag, a line in Config/Tags/FishTags.ini).
 *
 *  JSON format: an array of objects; "Name" = row name; one key per property. Every property must be present except
 *  DevComment (the import reports missing and unknown keys, so typos are caught). Tags are strings
 *  ("Fish.Stat.Strength"), tag lists are arrays of strings, enums are names ("Multiply"), colors are
 *  {"R":..,"G":..,"B":..,"A":..}, an empty id is "None", an empty asset reference is "None".
 *  Tag lists are TArray<FGameplayTag> (not FGameplayTagContainer) so an unknown tag string is reported by the import
 *  instead of being dropped silently.
 *
 *  The only tag the code knows by name is Fish.Stat.Weight (the rolled weight, in kg). Every tag is registered in
 *  Config/Tags/FishTags.ini, none in C++.
 */

/** How a stat mod combines. Within a step, all Adds are applied first, then all Multiplies (as a product). */
UENUM(BlueprintType)
enum class EFishStatModOp : uint8
{
	/** Stat = Stat + Value */
	Add,
	/** Stat = Stat * Value (Value must be > 0) */
	Multiply
};

/** One stat value {Tag, Value}. Stats are open-ended tags under Fish.Stat, registered in DT_FishStat. A TArray of these replaces a TMap so it replicates. */
USTRUCT(BlueprintType)
struct FFishStatValue
{
	GENERATED_BODY()

	FFishStatValue() = default;
	FFishStatValue(const FGameplayTag& InTag, float InValue) : Tag(InTag), Value(InValue) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Fish", meta=(Categories="Fish.Stat"))
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Fish")
	float Value = 0.0f;
};

/** One stat change {StatTag, Op, Value}, used by rarity tiers and modifiers. Fish.Stat.Weight changes the weight (kg). */
USTRUCT(BlueprintType)
struct FFishStatMod
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(Categories="Fish.Stat"))
	FGameplayTag StatTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish")
	EFishStatModOp Op = EFishStatModOp::Add;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish")
	float Value = 0.0f;
};

/**
 *  A time-of-day window in hours, half-open [StartHour, EndHour). StartHour > EndHour wraps midnight
 *  (e.g. 20 -> 4 covers 20:00..03:59). Hours are 0..24; use 0 -> 24 for all day. StartHour == EndHour is invalid.
 */
USTRUCT(BlueprintType)
struct FFishTimeWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0", ClampMax="24"))
	float StartHour = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0", ClampMax="24"))
	float EndHour = 24.0f;

	/** Hours must already be normalized to [0, 24) (see FFishRoll::NormalizeHours) */
	bool Contains(float Hours) const
	{
		return StartHour <= EndHour
			? (Hours >= StartHour && Hours < EndHour)
			: (Hours >= StartHour || Hours < EndHour);
	}
};

/**
 *  DT_FishStat row: the stat registry. Every stat used anywhere must have a row. Row name = short id (e.g. "Strength").
 *  Every instance carries every registered stat (Default unless the species or a mod sets it), clamped to [Min, Max].
 */
USTRUCT(BlueprintType)
struct FFishStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The stat tag (Fish.Stat.*) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stat", meta=(Categories="Fish.Stat"))
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stat")
	FText DisplayName;

	/** Final clamp range */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stat")
	float Min = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stat")
	float Max = 1000.0f;

	/** Value for species that don't list this stat in BaseStats */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stat")
	float Default = 0.0f;

	/** Makes the fight harder: scales with weight (WeightStatExponent) and counts in DifficultyRating */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stat")
	bool bIsDifficultyStat = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stat", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;
};

/**
 *  DT_FishSpecies row: one fish species. Row name = species id.
 */
USTRUCT(BlueprintType)
struct FFishSpeciesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Name shown to the player */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species")
	FText DisplayName;

	/** Journal entry text */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species", meta=(MultiLine=true))
	FText JournalText;

	/** Fish level = BaseLevel + the rarity's LevelBonus */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species", meta=(ClampMin="1"))
	int32 BaseLevel = 1;

	/** Lightest natural roll (kg), > 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size", meta=(ClampMin="0.001"))
	float WeightMin = 0.5f;

	/** Heaviest natural roll (kg), >= WeightMin. Modifiers (Giant) may go past it, up to the Weight stat's Max. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size", meta=(ClampMin="0.001"))
	float WeightMax = 2.0f;

	/** Weight = WeightMin + (WeightMax - WeightMin) * U ^ SizeSkew. > 1: most fish are small and big ones rare; 1: uniform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size", meta=(ClampMin="0.01"))
	float SizeSkew = 1.0f;

	/** Weight (kg) at which the weight scaling factor is 1.0 (BaseStats apply as written) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size", meta=(ClampMin="0.001"))
	float ReferenceWeight = 1.0f;

	/** Difficulty stats scale by (Weight / ReferenceWeight) ^ WeightStatExponent: heavier = stronger. 0 = no scaling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Size")
	float WeightStatExponent = 0.5f;

	/** Stats at the reference weight (tags registered in DT_FishStat; not Fish.Stat.Weight). Unlisted stats take the stat's Default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	TArray<FFishStatValue> BaseStats;

	/** Value = BaseValuePerKg * FinalWeight * rarity and modifier value multipliers */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Value", meta=(ClampMin="0"))
	float BaseValuePerKg = 1.0f;

	/** Abundance: relative chance to be the one that bites among eligible species */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Where and when", meta=(ClampMin="0"))
	float BiteWeight = 1.0f;

	/** Fishing spot habitats (Habitat.*), any match, hierarchical. Empty = any habitat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Where and when", meta=(Categories="Habitat"))
	TArray<FGameplayTag> HabitatTags;

	/** Regions (Region.*), any match, hierarchical (Region.Tropical matches a context of Region.Tropical.PalmKey). Empty = any region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Where and when", meta=(Categories="Region"))
	TArray<FGameplayTag> RegionTags;

	/** When it bites (any window). Empty = any time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Where and when")
	TArray<FFishTimeWindow> TimeWindows;

	/** Weather it bites in (Weather.*), any match. Empty = any weather. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Where and when", meta=(Categories="Weather"))
	TArray<FGameplayTag> WeatherTags;

	/** Bait/hook tags it takes (Bait.*, Hook.*): any-of, hierarchical (the gear's Bait.Worm.Night satisfies Bait.Worm). Empty = any bait or none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Where and when", meta=(Categories="Bait,Hook"))
	TArray<FGameplayTag> AcceptedBait;

	/** Family and trait tags (Fish.Family.*, Fish.Trait.*), for modifier conditions and journal grouping */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species", meta=(Categories="Fish.Family,Fish.Trait"))
	TArray<FGameplayTag> SpeciesTags;

	/** DT_FishRarity ids it can roll. Empty = all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rolls")
	TArray<FName> AllowedRarities;

	/** DT_FishModifier ids it can roll (still subject to each modifier's conditions). Empty = all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rolls")
	TArray<FName> AllowedModifiers;

	/** Most modifiers one catch can carry */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rolls", meta=(ClampMin="0"))
	int32 MaxModifiers = 2;

	/** Fight pattern id for the reel fight (T-007 data) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fight")
	FName FightPatternId;

	/** Mesh (static or skeletal) for the fish in hand and in the journal */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look")
	TSoftObjectPtr<UStreamableRenderAsset> Mesh;

	/**
	 *  Skinned fish on SKEL_Fish (T-029): the fish you see fighting in the water, playing ABP_Fish. None = Mesh if that is a
	 *  skeletal mesh, else ULureFishVisualSettings::FallbackMesh. Optional column.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(DataTableImportOptional))
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Additive alpha of the swim/fight clips, 0..1 (1 = the clips as authored; Landed_Flop always plays at 1). Optional column. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0", ClampMax="1", DataTableImportOptional))
	float AnimAmplitude = 1.0f;

	/** Play-rate multiplier of the swim/fight clips (> 0; 1 = as authored). Optional column. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0.01", DataTableImportOptional))
	float AnimRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Species", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;
};

/**
 *  DT_FishRarity row: one rarity tier. Row name = rarity id. Tiers are rows, not a fixed list.
 */
USTRUCT(BlueprintType)
struct FFishRarityRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rarity")
	FText DisplayName;

	/** Explicit order, 0 = most common. Unique. Luck favors higher ranks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rarity", meta=(ClampMin="0"))
	int32 Rank = 0;

	/** Relative roll weight; 0 = disabled (never rolls, even with luck). Among enabled tiers it must not increase with Rank. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rarity", meta=(ClampMin="0"))
	float RollWeight = 0.0f;

	/** Sell value multiplier; must strictly increase with Rank */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects", meta=(ClampMin="0"))
	float ValueMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects", meta=(ClampMin="0"))
	float XpMultiplier = 1.0f;

	/** Added to the species BaseLevel */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 LevelBonus = 0;

	/** Stat changes (Adds, then Multiplies), applied after the weight step and before modifiers. This is how rarity raises difficulty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	TArray<FFishStatMod> StatMods;

	/** Visual cue: sheen or glow color (alpha = strength, 0 = none) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look")
	FLinearColor CueColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rarity", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;
};

/**
 *  DT_FishModifier row: a trait one catch can roll (Heavy, Feisty, Giant, Albino...). Row name = modifier id.
 */
USTRUCT(BlueprintType)
struct FFishModifierRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modifier")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modifier", meta=(MultiLine=true))
	FText Description;

	/** Independent chance (0..1) when eligible. Luck does not affect it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roll", meta=(ClampMin="0", ClampMax="1"))
	float RollChance = 0.0f;

	/** At most one modifier per group (e.g. "Size" for Heavy/Giant). If several roll, one is kept by a pick weighted by RollChance. None = no group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roll")
	FName ExclusivityGroup;

	/** Stat changes. All Adds of all kept modifiers are applied first, then all Multiplies (product). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	TArray<FFishStatMod> StatMods;

	/** Sell value multiplier (value-only modifiers such as Albino are fine) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects", meta=(ClampMin="0"))
	float ValueMultiplier = 1.0f;

	/** Condition: only these species ids. Empty = any species. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Conditions")
	TArray<FName> SpeciesIds;

	/** Condition: the species has any of these family/trait tags (hierarchical). Empty = any. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Conditions", meta=(Categories="Fish.Family,Fish.Trait"))
	TArray<FGameplayTag> SpeciesTags;

	/** Condition: the context region matches any of these (hierarchical). Empty = anywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Conditions", meta=(Categories="Region"))
	TArray<FGameplayTag> RegionTags;

	/** Condition: the context time is inside any window. Empty = any time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Conditions")
	TArray<FFishTimeWindow> TimeWindows;

	/** Condition: the context weather matches any of these. Empty = any weather. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Conditions", meta=(Categories="Weather"))
	TArray<FGameplayTag> WeatherTags;

	/** Look change, e.g. Albino (alpha = strength, 0 = none) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look")
	FLinearColor TintColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modifier", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;
};

/**
 *  Roll tuning that isn't per-row data (UFishSettings; the core takes it as a parameter inside FFishTables).
 */
USTRUCT(BlueprintType)
struct FFishRollTuning
{
	GENERATED_BODY()

	/** Rarity luck: weight' = RollWeight * (1 + Luck * Rank * LuckRankFactor) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Luck", meta=(ClampMin="0"))
	float LuckRankFactor = 0.25f;

	/** Luck is clamped to [0, MaxLuck] (NaN counts as 0) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Luck", meta=(ClampMin="0"))
	float MaxLuck = 10.0f;

	/** XP curve until T-010: BaseXp(Level) = XpBase + XpPerLevel * (Level - 1); Xp = round-half-up(BaseXp * rarity XpMultiplier) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="XP", meta=(ClampMin="0"))
	float XpBase = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="XP", meta=(ClampMin="0"))
	float XpPerLevel = 5.0f;
};

/**
 *  Level hook (for T-007 fight difficulty and T-010 "fish above your level escape easily"):
 *    d = FishLevel - PlayerLevel
 *    LevelDifficultyMultiplier = clamp(1 + max(0, d) * OverLevelFactor - max(0, -d) * UnderLevelFactor, MinMultiplier, MaxMultiplier)
 *  Equal levels give 1.0. With the defaults: 1 level above = 1.35, 4 above = 2.4, 3 below = 0.7, 5+ below = 0.5.
 */
USTRUCT(BlueprintType)
struct FFishLevelScaling
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level Scaling", meta=(ClampMin="0"))
	float OverLevelFactor = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level Scaling", meta=(ClampMin="0"))
	float UnderLevelFactor = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level Scaling", meta=(ClampMin="0"))
	float MinMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level Scaling", meta=(ClampMin="0"))
	float MaxMultiplier = 5.0f;

	/** The formula above; finite and within [MinMultiplier, MaxMultiplier] for any int32 levels */
	float GetMultiplier(int32 FishLevel, int32 PlayerLevel) const;
};

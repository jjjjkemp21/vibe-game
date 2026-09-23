// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/RandomStream.h"
#include "Fish/FishTypes.h"
#include "Fish/FishInstance.h"
#include "FishRoll.generated.h"

class UDataTable;

/**
 *  Everything the core reads: the four fish tables plus tuning. Passed in explicitly (dependency injection), so the
 *  core never reads settings, subsystems, GWorld or the global RNG. The game fills it from UFishSettings::LoadTables;
 *  tests build it from the JSON sources.
 */
struct FFishTables
{
	/** DT_FishSpecies (FFishSpeciesRow) */
	const UDataTable* Species = nullptr;
	/** DT_FishRarity (FFishRarityRow) */
	const UDataTable* Rarities = nullptr;
	/** DT_FishModifier (FFishModifierRow); null counts as empty (with a warning) */
	const UDataTable* Modifiers = nullptr;
	/** DT_FishStat (FFishStatRow), must contain Fish.Stat.Weight */
	const UDataTable* Stats = nullptr;
	/** Luck and XP tuning */
	FFishRollTuning Tuning;

	const FFishSpeciesRow* FindSpecies(FName Id) const;
	const FFishRarityRow* FindRarity(FName Id) const;
	const FFishModifierRow* FindModifier(FName Id) const;
	/** Stat row by tag (exact match), or null */
	const FFishStatRow* FindStat(const FGameplayTag& Tag) const;
};

/** RNG sub-stream ids: stage stream = FRandomStream(HashCombine(uint32(Seed), StageId)) */
enum class EFishRollStage : uint32
{
	Weight = 1,
	Rarity = 2,
	Modifiers = 3,
	Bite = 4
};

/**
 *  THE fish roll pipeline (one function, FFishRoll::Roll) plus the bite picker and pure helpers.
 *  Pure and read-only on the tables: no UObject creation, no global state, safe off the game thread.
 *  Failure contract: functions return bool; data problems log a Warning on LogLureFish (never check/ensure);
 *  no output is ever NaN or Inf. On failure the out instance is a default (invalid) instance.
 *
 *  Roll(Tables, Context, OutFish), in order:
 *   1. Species base: every DT_FishStat row starts at its Default; the species BaseStats override those values.
 *   2. Weight: U = weight stream FRand() in [0, 1).
 *        Weight = WeightMin + (WeightMax - WeightMin) * U ^ SizeSkew                      (SampleWeight)
 *        or, forced: Weight = WeightMin + (WeightMax - WeightMin) * ForcedWeightFraction.
 *      Every difficulty stat (DT_FishStat bIsDifficultyStat) *= (Weight / ReferenceWeight) ^ WeightStatExponent.
 *      Fish.Stat.Weight = Weight. The base roll stays in [WeightMin, WeightMax].
 *   3. Rarity: candidates = the species' AllowedRarities (empty = every row), sorted by Rank then id.
 *        Luck = clamp(Context.Luck, 0, MaxLuck) (NaN -> 0 with a warning)
 *        w_i = RollWeight_i * (1 + Luck * Rank_i * LuckRankFactor)   (negative or non-finite RollWeight counts as 0)
 *      One draw U from the rarity stream; PickWeightedIndex(w, U). Tiers with RollWeight 0 never roll. If every w_i is 0
 *      the roll fails (warning). Then the rarity's StatMods (Adds, then Multiplies), then Level = BaseLevel + LevelBonus.
 *   4. Modifiers: every DT_FishModifier row in FName::LexicalLess order draws one U from the modifier stream (always,
 *      eligible or not, so eligibility never shifts other rows' draws). A row hits if it is eligible and U < RollChance.
 *      Eligible = allowed by the species (AllowedModifiers empty = all) and every condition passes (SpeciesIds,
 *      SpeciesTags, RegionTags, TimeWindows, WeatherTags; empty = any). Then, for each ExclusivityGroup (LexicalLess
 *      order) with 2+ hits, one draw U picks the one kept, weighted by RollChance (PickWeightedIndex). Then the first
 *      MaxModifiers hits in LexicalLess order are kept. Then ALL Adds of the kept modifiers (in order), then ALL
 *      Multiplies (product). Modifiers may push Weight past WeightMax.
 *   5. Clamp: every stat to its DT_FishStat [Min, Max]. WeightKg = Fish.Stat.Weight.
 *   6. Value = max(1, round-half-up(BaseValuePerKg * WeightKg * Rarity.ValueMultiplier * product of modifier
 *              ValueMultipliers)), round-half-up(x) = floor(x + 0.5).
 *      Xp = round-half-up((XpBase + XpPerLevel * (Level - 1)) * Rarity.XpMultiplier).
 *      DifficultyRating = mean over difficulty stats with a base > 0 of (final value / species base value); 1 if none.
 *      Stats sorted by tag name (FName::LexicalLess).
 *  Stat mod guards: a mod on a stat with no DT_FishStat row, a non-finite value, or a Multiply <= 0 is skipped with a
 *  warning. Example: base Strength 10, Feisty {+5, x2}, Other {+3} -> (10 + 5 + 3) * 2 = 36 (never 33).
 *
 *  Warnings, two kinds:
 *   - Data warnings (a bad row: the guards above, an invalid RollWeight/RollChance, an unknown allowed rarity, a species
 *     with an invalid weight range, no Weight stat row, a species that can roll no rarity, ...) are logged ONCE per
 *     (table object, row, problem) per session (RememberDataWarning), so one bad row can't flood the log. The roll
 *     behaves the same every time (skip, count as 0, or fail); only the log line is deduplicated.
 *   - Caller warnings (missing tables, unknown species, NaN luck or ForcedWeightFraction, an unknown forced id) are
 *     logged on every call.
 *  Forced overrides are for tests and debug tools (T-025 Lure.GiveFish): an unknown ForcedRarityId, or any unknown id in
 *  ForcedModifierIds, fails the whole roll with a Warning naming the id (no partial fish, the known ids are not kept).
 *  The data validator (FFishDataValidator) is the place that reports every data problem; the roll only guards.
 */
struct FFishRoll
{
	/** Rolls one fish of Context.SpeciesId. Server-side only in multiplayer. Optional trace = the numbers at each step. */
	static bool Roll(const FFishTables& Tables, const FFishRollContext& Context, FFishInstance& OutFish, TArray<FString>* OutTrace = nullptr);

	/**
	 *  Which species bites. Eligible: BiteWeight > 0 and region, habitat, time windows, weather and bait all match
	 *  (each list empty = any; tag lists match hierarchically, any-of). Species are taken in table row order, weighted
	 *  by BiteWeight, one draw U from the bite stream (PickWeightedIndex). The player level does not filter.
	 *  @return false with no warning if nothing can bite (normal gameplay); false with a warning if the table is missing
	 */
	static bool PickSpecies(const FFishTables& Tables, const FFishRollContext& Context, FName& OutSpeciesId, TArray<FString>* OutTrace = nullptr);

	/**
	 *  Weighted pick: the first index i with U * Total < cumulative weight, where non-positive and non-finite weights
	 *  count as 0 and are never returned. U is clamped to [0, 1). INDEX_NONE if no weight is > 0.
	 */
	static int32 PickWeightedIndex(TConstArrayView<float> Weights, float U);

	/** WeightMin + (WeightMax - WeightMin) * U ^ SizeSkew, with U clamped to [0, 1] */
	static float SampleWeight(const FFishSpeciesRow& Species, float U);

	/** (Weight / ReferenceWeight) ^ WeightStatExponent; 1 at the reference weight */
	static float WeightStatFactor(const FFishSpeciesRow& Species, float WeightKg);

	/** Hours wrapped into [0, 24) (24 -> 0, -1 -> 23); NaN/Inf -> 0 */
	static float NormalizeHours(float Hours);

	/** True if Windows is empty or any window contains the (normalized) hour */
	static bool IsInTimeWindows(TConstArrayView<FFishTimeWindow> Windows, float Hours);

	/** Bite eligibility of one species (see PickSpecies); OutReason names the failed check */
	static bool IsSpeciesEligible(const FFishSpeciesRow& Species, const FFishRollContext& Context, FString* OutReason = nullptr);

	/** Modifier conditions and the species allow-list (ignores chance, groups and the cap) */
	static bool IsModifierEligible(FName ModifierId, const FFishModifierRow& Modifier, FName SpeciesId, const FFishSpeciesRow& Species,
		const FFishRollContext& Context, FString* OutReason = nullptr);

	/** Step 3 candidates and their luck-adjusted weights, sorted by Rank then id */
	static TArray<TPair<FName, float>> GetRarityWeights(const FFishTables& Tables, const FFishSpeciesRow& Species, float Luck);

	/** round-half-up((XpBase + XpPerLevel * (Level - 1)) * XpMultiplier), >= 0 */
	static int32 ComputeXp(const FFishRollTuning& Tuning, int32 Level, float XpMultiplier);

	/** The level hook: Scaling.GetMultiplier(FishLevel, PlayerLevel) (formula on FFishLevelScaling) */
	static float LevelDifficultyMultiplier(int32 FishLevel, int32 PlayerLevel, const FFishLevelScaling& Scaling);

	/** Seed of a stage's sub-stream: HashCombine(uint32(Seed), uint32(Stage)) */
	static uint32 StageSeed(int32 Seed, EFishRollStage Stage);

	/** FRandomStream(StageSeed(Seed, Stage)) */
	static FRandomStream MakeStageStream(int32 Seed, EFishRollStage Stage);

	/** A fresh random seed for a new bite (server) */
	static int32 MakeRandomSeed();

	/** The one stat tag the code knows by name: "Fish.Stat.Weight" */
	static FName WeightStatName();

	/** How many data-warning keys are remembered before the set is emptied (bounded memory) */
	static constexpr int32 DataWarningCapacity = 1024;

	/**
	 *  The once-per-session filter for data warnings. Returns true the first time a (table object, row, problem) is seen
	 *  (the caller logs it), false after that. Problem is stable text (no per-roll numbers). Keys use the table object's
	 *  identity (FObjectKey), so a new table object (e.g. a test fixture) warns again. At DataWarningCapacity keys the set
	 *  is emptied, so a problem may log once more but is never hidden for good. Thread-safe.
	 */
	static bool RememberDataWarning(const UDataTable* Table, FName RowId, const FString& Problem);

	/** Forgets every remembered data warning (tests, debug tools) */
	static void ResetDataWarnings();

	/** Number of remembered data-warning keys (<= DataWarningCapacity) */
	static int32 NumDataWarningsRemembered();
};

/**
 *  Blueprint access: settings-based wrappers over the core, instance helpers and the level hook.
 */
UCLASS()
class UFishLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Loads the tables from UFishSettings and rolls (FFishRoll::Roll). False with a warning if the tables are missing. */
	UFUNCTION(BlueprintCallable, Category="Fish")
	static bool RollFishFromSettings(const FFishRollContext& Context, FFishInstance& OutFish);

	/** Loads the tables from UFishSettings and picks a species (FFishRoll::PickSpecies) */
	UFUNCTION(BlueprintCallable, Category="Fish")
	static bool PickSpeciesFromSettings(const FFishRollContext& Context, FName& OutSpeciesId);

	/** Stat value (exact tag match), or Default */
	UFUNCTION(BlueprintPure, Category="Fish")
	static float GetFishStat(const FFishInstance& Fish, FGameplayTag Stat, float Default = 0.0f);

	UFUNCTION(BlueprintPure, Category="Fish")
	static bool FishHasModifier(const FFishInstance& Fish, FName ModifierId);

	/** Level hook with UFishSettings::LevelScaling (1 = same level, > 1 = the fish is above the player) */
	UFUNCTION(BlueprintPure, Category="Fish|Level Scaling")
	static float GetLevelDifficultyMultiplier(int32 FishLevel, int32 PlayerLevel);

	UFUNCTION(BlueprintCallable, Category="Fish")
	static int32 MakeRandomFishSeed();

	UFUNCTION(BlueprintPure, Category="Fish")
	static FString FishToString(const FFishInstance& Fish);
};

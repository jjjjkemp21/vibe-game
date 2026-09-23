// Lure: cooler, selling, money, XP and levels (T-010). Rules: docs/specs/progression-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Fish/FishInstance.h"
#include "LureProgressionTypes.generated.h"

/** Progression log: data problems are Warnings (never check/ensure); a client calling a server-only function is a Warning. */
DECLARE_LOG_CATEGORY_EXTERN(LogLureProgression, Log, All);

/**
 *  Progression data (T-010). Three DataTables, CSV sources in the repo, imported by the editor-operator (row name = id):
 *    data/tables/DT_PlayerLevel.csv -> /Game/Data/DT_PlayerLevel (FPlayerLevelRow)  the player XP curve
 *    data/tables/DT_Cooler.csv      -> /Game/Data/DT_Cooler      (FCoolerRow)       cooler sizes (upgrades are rows)
 *    data/tables/DT_FishMarket.csv  -> /Game/Data/DT_FishMarket  (FFishMarketRow)   buyers (a dock or an NPC) and their price multiplier
 *  The level-gap rule (fish above your level are harder) is not a new table: it is UFishSettings.LevelScaling
 *  (FFishLevelScaling, DefaultGame.ini), reused through FFishRoll::LevelDifficultyMultiplier.
 */

/** DT_PlayerLevel row: one player level. Levels are 1..N with no gaps; the row with the highest Level is the cap. */
USTRUCT(BlueprintType)
struct FPlayerLevelRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 1-based level; every level 1..N has exactly one row */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level", meta=(ClampMin="1"))
	int32 Level = 1;

	/** XP from the start of this level to the next (> 0), and 0 on the last level (the cap). Must not decrease with Level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level", meta=(ClampMin="0"))
	int32 XpToNext = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Level", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;
};

/** DT_Cooler row: a cooler size. Every player starts with ULureProgressionSettings::DefaultCoolerId; upgrades switch the row. */
USTRUCT(BlueprintType)
struct FCoolerRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cooler")
	FText DisplayName;

	/** How many fish it holds, 1..FLureProgressionData::MaxCoolerSlots */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cooler", meta=(ClampMin="1"))
	int32 Slots = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cooler", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;
};

/** DT_FishMarket row: a buyer (a dock, later an NPC). A sell point names its row (ALureSellPoint::MarketId). */
USTRUCT(BlueprintType)
struct FFishMarketRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Market")
	FText DisplayName;

	/** Price = round-half-up(fish Value * SellMultiplier). > 0 and <= FLureProgressionData::MaxSellMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Market", meta=(ClampMin="0.01"))
	float SellMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Market", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;
};

/**
 *  Everything T-019 save/load needs from T-010, in one struct (every field SaveGame). The records are authoritative:
 *  loading never re-rolls or re-prices a fish. Apply with ULureProgressionComponent::ApplySaveData (server).
 */
USTRUCT(BlueprintType)
struct FLureProgressSaveData
{
	GENERATED_BODY()

	/** Bump when the layout changes (T-019 migrates old saves) */
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Progression")
	int32 Version = CurrentVersion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Progression")
	int32 Money = 0;

	/** All XP ever earned (the level is derived from it, see FLureLevelCurve) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Progression")
	int32 TotalXp = 0;

	/** Level at save time: a floor on load, so a steeper curve never takes levels away */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Progression")
	int32 Level = 1;

	/** DT_Cooler row (None = the default cooler) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Progression")
	FName CoolerId;

	/** Unsold fish, in cooler order */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Progression")
	TArray<FFishInstance> CoolerFish;
};

/** Where a player stands on the XP curve (for the HUD and T-011) */
USTRUCT(BlueprintType)
struct FLureLevelProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 Level = 1;

	/** XP earned since this level started (0 at the cap is not special: XP past the cap keeps counting here) */
	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 XpIntoLevel = 0;

	/** XP this level needs to reach the next one; 0 at the max level */
	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 XpForNextLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category="Progression")
	bool bIsMaxLevel = false;

	/** XpIntoLevel / XpForNextLevel in [0, 1]; 1 at the max level */
	UPROPERTY(BlueprintReadOnly, Category="Progression")
	float Fraction = 0.0f;
};

/** What happened when a fish was landed (ULureProgressionComponent::HandleFishLanded) */
USTRUCT(BlueprintType)
struct FLureFishLandedResult
{
	GENERATED_BODY()

	/** False for an invalid fish or when not called on the server: then nothing changed */
	UPROPERTY(BlueprintReadOnly, Category="Progression")
	bool bAccepted = false;

	/** True if the fish went into the cooler; false = the cooler was full (the fish is released, the XP still counts) */
	UPROPERTY(BlueprintReadOnly, Category="Progression")
	bool bStoredInCooler = false;

	/** Cooler slot it went into (INDEX_NONE if not stored) */
	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 CoolerSlot = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 XpGained = 0;

	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 LevelsGained = 0;

	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 NewLevel = 1;
};

/** What a sale paid (ALureSellPoint::SellAll / SellOne) */
USTRUCT(BlueprintType)
struct FLureSaleResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 FishSold = 0;

	UPROPERTY(BlueprintReadOnly, Category="Progression")
	int32 MoneyEarned = 0;
};

/**
 *  The player XP curve built from DT_PlayerLevel. Pure, no UObjects after Build.
 *  Level L starts at XpAtLevelStart(L) = sum of XpToNext for levels 1..L-1. The level for a total XP is the highest level
 *  whose start is <= that XP, capped at MaxLevel. XP beyond the cap keeps counting (a raised cap levels players up later).
 *  An empty curve (no table) is level 1 forever.
 */
struct FLureLevelCurve
{
	/** XpToNext per level: [0] = level 1. The last entry belongs to the max level (0 in valid data, ignored anyway). */
	TArray<int32> XpToNext;

	/** Builds from a DT_PlayerLevel table (rows sorted by Level). Rows that break the rules are reported and skipped. */
	static FLureLevelCurve FromTable(const UDataTable* Table, TArray<FString>* OutProblems = nullptr);

	/** From explicit per-level values (tests). */
	static FLureLevelCurve FromValues(TConstArrayView<int32> InXpToNext);

	bool IsEmpty() const { return XpToNext.Num() == 0; }

	/** Highest level (>= 1) */
	int32 GetMaxLevel() const { return FMath::Max(1, XpToNext.Num()); }

	/** Total XP at which Level starts (level 1 = 0); clamped to [1, MaxLevel] */
	int64 GetXpAtLevelStart(int32 Level) const;

	/** Level reached with TotalXp (negative counts as 0) */
	int32 GetLevelForXp(int64 TotalXp) const;

	/** Progress for TotalXp at CurrentLevel (a loaded level may sit above what the XP alone gives) */
	FLureLevelProgress GetProgress(int64 TotalXp, int32 CurrentLevel) const;
};

/** Pure progression rules (no world, no UObjects created). Sell prices reuse the fish's rolled Value (FFishRoll step 6). */
struct FLureProgressionRules
{
	/**
	 *  Sell price of one fish: max(1, round-half-up(Fish.Value * SellMultiplier)), round-half-up(x) = floor(x + 0.5), so a
	 *  real fish always pays something (like the roll's Value minimum of 1). 0 for an invalid fish (no species), a Value <= 0,
	 *  or a multiplier that is not finite and > 0. Saturates at MAX_int32.
	 */
	static int32 GetSellPrice(const FFishInstance& Fish, float SellMultiplier);

	/** Sum of GetSellPrice over the fish (each fish rounded on its own, exactly what selling them one by one pays), saturating */
	static int32 GetSellTotal(TConstArrayView<FFishInstance> Fish, float SellMultiplier);

	/** A + B clamped to [0, MAX_int32] (money and XP never wrap) */
	static int32 SaturatingAdd(int32 A, int32 B);
};

/**
 *  Data validation of the progression tables (reused on editor reimport later). Each returns the problems (empty = OK).
 */
struct FLureProgressionData
{
	/** Largest cooler a row may declare */
	static constexpr int32 MaxCoolerSlots = 100;
	/** Largest buyer multiplier a row may declare */
	static constexpr float MaxSellMultiplier = 10.0f;

	/** Row struct, at least one row, Levels 1..N unique and contiguous, XpToNext > 0 below the cap and 0 on the cap, non-decreasing */
	static TArray<FString> ValidatePlayerLevelTable(const UDataTable* Table);

	/** Row struct, at least one row, Slots in [1, MaxCoolerSlots], DefaultCoolerId (if not None) exists */
	static TArray<FString> ValidateCoolerTable(const UDataTable* Table, FName DefaultCoolerId = NAME_None);

	/** Row struct, at least one row, SellMultiplier finite in (0, MaxSellMultiplier], DefaultMarketId (if not None) exists */
	static TArray<FString> ValidateMarketTable(const UDataTable* Table, FName DefaultMarketId = NAME_None);

	/**
	 *  Checks a CSV source before import (the engine may read text in a number cell as 0): a Name column first, unique
	 *  non-empty row names, every column is a property of RowStruct, every required property has a column, and every
	 *  int/float cell is a number (FCString::IsNumeric).
	 */
	static TArray<FString> ValidateCsvSource(const FString& Csv, const UScriptStruct* RowStruct, const FString& TableName);
};

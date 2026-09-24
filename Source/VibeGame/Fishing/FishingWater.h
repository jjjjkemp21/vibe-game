// Lure: "fish anywhere" rules and world lookups (T-027). Rules: docs/specs/fishing-water-rules.md.
//
// FLureWaterRules is pure (no world, no settings reads unless named *FromSettings), so the priority rule, the bite
// decision and the hot spot drift are unit-testable. FLureWaterQuery does the world lookups (areas, depth, hot spots).

#pragma once

#include "CoreMinimal.h"
#include "Fish/FishInstance.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/FishingWaterTypes.h"

class ALureHotSpot;
class UDataTable;
class UWorld;
struct FFishTables;
struct FLureCastLanding;

/** The tunables of the bite decision (from ULureWaterSettings; tests pass their own). */
struct FLureBiteRules
{
	/** Nothing bites in water shallower than this, cm. */
	float MinBiteDepth = 15.f;

	/** Tried in order when the water's habitat has no species at this hour (bait ignored). */
	TArray<FGameplayTag> GapFallbackHabitats;

	static FLureBiteRules FromSettings();
};

/** What one bite check decided (FLureWaterRules::DecideBite). */
struct FLureBiteDecision
{
	ELureNoBiteReason Reason = ELureNoBiteReason::None;

	/** The fish-roll context of the bite (filled whenever the water is deep enough, even if nothing bites). */
	FFishRollContext Context;

	/** The habitat the bite uses: the water's, or a gap fallback habitat. */
	FGameplayTag BiteHabitat;

	/** A gap fallback habitat was used. */
	bool bUsedFallback = false;

	bool CanBite() const { return Reason == ELureNoBiteReason::None; }
};

struct FLureWaterRules
{
	// ---- Water areas ----

	/** A wins over B where both apply: higher Priority, then the smaller area (GetSize), then the lower AreaId (LexicalLess). */
	static bool IsBetterArea(const FLureWaterAreaInfo& A, const FLureWaterAreaInfo& B);

	/**
	 *  The area that decides the water at XY with this depth: among areas that have a shape, a valid habitat, contain XY and
	 *  accept the depth, the best by IsBetterArea (a full tie keeps the earlier one). INDEX_NONE = default water.
	 */
	static int32 FindAreaIndex(TConstArrayView<FLureWaterAreaInfo> Areas, const FVector2D& XY, float DepthCm);

	/** The water context at XY: the winning area's id, name, habitat, region and luck, or the default water habitat. */
	static FLureWaterContext MakeWaterContext(TConstArrayView<FLureWaterAreaInfo> Areas, const FVector2D& XY, float WaterZ, float DepthCm,
		const FGameplayTag& DefaultHabitat);

	/** A legacy fishing_spot marker as a circle area (migration fallback). */
	static FLureWaterAreaInfo AreaFromLegacySpot(const FLureFishingSpot& Spot, int32 Priority);

	// ---- The bite ----

	/** Some species of this habitat and region can bite at this hour and weather with the right bait (bait ignored). */
	static bool HasSpeciesIgnoringBait(const FFishTables& Tables, const FGameplayTag& Habitat, const FGameplayTag& Region, float Hours,
		const FGameplayTag& Weather);

	/**
	 *  The habitat a bite uses: the water's own if HasSpeciesIgnoringBait, else the first fallback that has one (a data gap:
	 *  logged once per habitat and hour), else the water's own (nothing bites; logged once). bOutFallback / bOutAny say which.
	 */
	static FGameplayTag ResolveBiteHabitat(const FFishTables& Tables, const FGameplayTag& WaterHabitat, const FGameplayTag& Region, float Hours,
		const FGameplayTag& Weather, TConstArrayView<FGameplayTag> Fallbacks, bool& bOutFallback, bool& bOutAny);

	/**
	 *  The roll context of a bite: the habitat given, the water's region (else the environment's default region), luck =
	 *  area luck + gear luck + hot spot luck (non-finite terms count 0), the hot spot's SizeBonus and ValueMultiplier, the
	 *  environment's time, weather and bait, and Seed.
	 */
	static FFishRollContext MakeBiteContext(const FLureWaterContext& Water, const FGameplayTag& Habitat, const FLureHotSpotBonus& HotSpot,
		const FLureFishingEnvironment& Environment, int32 Seed);

	/**
	 *  Can something bite here now, and with which context? In order: not on water -> NotWater; shallower than MinBiteDepth
	 *  -> TooShallow; no species of the water's habitat (or a fallback) at this hour -> NoSpecies; species here but none
	 *  takes the bait -> WrongBait; else None (FFishRoll::PickSpecies would pick one with Decision.Context).
	 */
	static FLureBiteDecision DecideBite(const FFishTables& Tables, const FLureWaterContext& Water, const FLureHotSpotBonus& HotSpot,
		const FLureFishingEnvironment& Environment, const FLureBiteRules& Rules, int32 Seed);

	// ---- Placeholder HUD text ----

	/** The line for a no-bite reason ("Too shallow here: cast into deeper water.") or empty. Bait names the bait for WrongBait. */
	static FString NoBiteText(ELureNoBiteReason Reason, const FGameplayTag& Bait);

	/** Whether a reason's text shows at once (TooShallow) or after the profile's NoBiteHintDelay (NoSpecies, WrongBait). */
	static bool ShowsAtOnce(ELureNoBiteReason Reason);

	/** "Water: Reef Flats" (the area's name), "Water: open water" (default water). */
	static FString WaterLine(const FString& AreaName);

	// ---- Hot spots ----

	/**
	 *  Drift of a hot spot Age seconds after it appeared: a smooth wander from its anchor (offset 0 at age 0; two sine waves
	 *  with a seeded frequency ratio, turned by a seeded angle) whose distance from the anchor never exceeds DriftRange and
	 *  whose RMS speed is DriftSpeed. Zero range or speed = no motion. Deterministic: the same inputs give the same offset on
	 *  every machine.
	 */
	static FVector2D DriftOffset(float DriftRange, float DriftSpeed, int32 Seed, double Age);

	/** Chance that a spawn happens in one check of Seconds: 1 - exp(-Seconds / SpawnInterval) (0 when the interval is 0). */
	static float SpawnChance(const FLureHotSpotRow& Row, float Seconds);

	/** LifetimeMin + (LifetimeMax - LifetimeMin) * U. */
	static float LifetimeFromRoll(const FLureHotSpotRow& Row, float U);

	/** The bonus a cast gets from a hot spot of this row. */
	static FLureHotSpotBonus BonusFromRow(FName TypeId, const FLureHotSpotRow& Row);

	/** The built-in row used when DT_HotSpot is missing (the struct defaults = the shipped "Bubbles" row). */
	static FName FallbackHotSpotType();

	// ---- Data rule (tests, validation) ----

	/**
	 *  Hours (sampled every StepHours from 0) at which no species of the habitat and region can bite (bait ignored), and,
	 *  when Fallbacks is not empty, not even through a fallback. Returned as "HH:MM-HH:MM" ranges (end exclusive).
	 */
	static TArray<FString> FindGapHours(const FFishTables& Tables, const FGameplayTag& Habitat, const FGameplayTag& Region,
		TConstArrayView<FGameplayTag> Fallbacks, float StepHours = 0.25f);
};

/** World lookups for the water model (server for the bite; every machine for the HUD and hot spot visuals). */
struct FLureWaterQuery
{
	/** The server-synchronized time (GameState server time, else the world's time). */
	static double GetTime(const UWorld* World);

	/**
	 *  Every water area of the world: ALureWaterArea actors (sorted by id), or, if there are none and the setting allows,
	 *  the legacy fishing_spot markers as circle areas. bOutLegacy says which.
	 */
	static TArray<FLureWaterAreaInfo> GatherAreas(const UWorld* World, bool* bOutLegacy = nullptr);

	/** Depth of the water at XY: the surface WaterZ down to the first solid ground (FLureFishingSpots::TraceCast), else Probe. */
	static float MeasureDepth(const UWorld* World, const FVector2D& XY, float WaterZ, float Probe);

	/**
	 *  Is there fishable water at XY (a water surface, and no solid ground above it within LandTolerance)? Gives the
	 *  surface and the depth. Used by the hot spot spawner and the dev commands.
	 */
	static bool ProbeWater(const UWorld* World, const FVector2D& XY, float& OutWaterZ, float& OutDepth);

	/** The water context of a bobber rest point (bOnWater false = land: an empty context). */
	static FLureWaterContext DescribeWater(const UWorld* World, const FVector& Rest, bool bOnWater, float WaterZ);

	/** The active hot spot containing XY at Time (several: the one whose center is nearest relative to its radius), or null. */
	static ALureHotSpot* FindHotSpotAt(const UWorld* World, const FVector2D& XY, double Time);

	/** The bonus of FindHotSpotAt (server; an inactive bonus if there is none). */
	static FLureHotSpotBonus FindHotSpotBonusAt(const UWorld* World, const FVector2D& XY, double Time);

	/** Display name of the area with this id (ALureWaterArea actors; else the id; None = "open water"). For the HUD. */
	static FString GetAreaDisplayName(const UWorld* World, FName AreaId);

	/** DT_HotSpot from the settings if imported (null + one warning if not). */
	static const UDataTable* LoadHotSpotTable();

	/** The row TypeId of Table, or of the built-in fallback when Table is null (only its FallbackHotSpotType). */
	static bool FindHotSpotRow(const UDataTable* Table, FName TypeId, FLureHotSpotRow& OutRow);

	/** Every row of Table sorted by name (the built-in fallback row when Table is null). */
	static TArray<TPair<FName, FLureHotSpotRow>> GetHotSpotRows(const UDataTable* Table);

	/** The HUD line of a hot spot type (the row's HudText; a generic line if the row is unknown). */
	static FString HotSpotHudText(FName TypeId);
};

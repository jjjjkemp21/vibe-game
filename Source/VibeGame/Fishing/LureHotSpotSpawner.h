// Lure: spawns hot spots in a level's water (T-027). The level builder places one from the layout's "hot_spots" marker.
// Rules: docs/specs/fishing-water-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/FishingWaterTypes.h"
#include "Math/RandomStream.h"
#include "LureHotSpotSpawner.generated.h"

class ALureHotSpot;
class UDataTable;

/** Why a candidate spawn point was turned down. Counted per check (the spawner's Verbose log, GetLastStepStats). */
enum class ELureHotSpotReject : uint8
{
	None,          // a good point
	NoSample,      // no candidate inside the area within a player's reach (a thin area, or only a sliver of it in reach)
	NotNearPlayer, // farther than HotSpotNearPlayerRadius from every player
	NoWater,       // no water surface there, or ground above it (land, a dock, a jetty)
	OtherArea,     // another water area wins at the point
	RowWater,      // the row doesn't allow the habitat or depth there, or it is shallower than MinBiteDepth
	TooClose,      // within MinSpacing of a live hot spot
	WanderLand,    // part of its wander area is not water
	WanderShallow, // part of its wander area is shallower than MinBiteDepth
	WanderLevel,   // part of its wander area is on another water level
	WanderHabitat, // part of its wander area is a habitat the row doesn't allow
	DriftShort,    // its drift path touches land before LifetimeMin
	Count
};

/** Rejected candidate points by reason. */
struct FLureHotSpotRejects
{
	int32 Counts[static_cast<int32>(ELureHotSpotReject::Count)] = {};

	void Add(ELureHotSpotReject Reason) { ++Counts[static_cast<int32>(Reason)]; }
	int32 Get(ELureHotSpotReject Reason) const { return Counts[static_cast<int32>(Reason)]; }
	void Append(const FLureHotSpotRejects& Other);
	int32 Total() const;

	/** "no water 7, other area 3" (every reason with a count, in enum order), or "none". */
	FString ToString() const;

	static const TCHAR* ReasonName(ELureHotSpotReject Reason);
};

/**
 *  What one spawn check (SpawnStep) did. Every (row, area) pair lands in exactly one bucket: WrongWater, Full, LevelFull,
 *  OutOfReach, NothingBanked, Rolls or DueRetries. The spawner logs it (LogLureHotSpot, Verbose) and tests read it.
 */
struct FLureHotSpotStepStats
{
	float Seconds = 0.f;
	int32 Players = 0;
	int32 Rows = 0;          // rows that can spawn here (HotSpotTypes applied; SpawnInterval > 0 and MaxPerArea > 0)
	int32 Areas = 0;         // water areas, plus 1 for the default water when the level has some
	int32 LiveBefore = 0;
	int32 Max = 0;
	int32 Spawned = 0;
	int32 WrongWater = 0;    // the area has no shape, or a habitat the row doesn't allow: never
	int32 Full = 0;          // MaxPerArea of the row live in the area
	int32 LevelFull = 0;     // the level is at its max: waits, banking
	int32 OutOfReach = 0;    // nobody can reach the area now: waits, banking
	int32 NothingBanked = 0; // in reach, but no time banked yet (a 0 s check)
	int32 Rolls = 0;         // rolled its banked time
	int32 RollsPassed = 0;
	int32 DueRetries = 0;    // retried a due spawn without a roll
	int32 NoPoint = 0;       // passed rolls and due retries that found no good point
	FLureHotSpotRejects Rejects;

	/** One line for the log. */
	FString ToString() const;
};

/** One (row, area) pair's spawn clock (see ALureHotSpotSpawner). */
struct FLureHotSpotClock
{
	/** Seconds the pair has waited since its last roll, at most max(the check's seconds, HotSpotPrewarmSeconds). */
	float Bank = 0.f;

	/** > 0: a passed roll found no good point, and the pair's next checks with a player in reach retry it without a roll. */
	int32 DueRetries = 0;
};

/**
 *  Server-only logic (it does nothing on clients; the hot spots themselves replicate). A level without a spawner has no
 *  hot spots (test maps stay deterministic).
 *
 *  Every ULureWaterSettings::HotSpotCheckInterval seconds (the level's first check counts as HotSpotPrewarmSeconds), for
 *  each DT_HotSpot row of HotSpotTypes (sorted by name) and each water area (sorted by id; the level's default water counts
 *  as one more area, last) whose habitat the row allows, the (row, area) pair's clock (FLureHotSpotClock) runs:
 *   - full (MaxPerArea hot spots of the row live in the area): the clock resets; it starts again when one ends;
 *   - otherwise it banks the check's seconds, up to max(the check, HotSpotPrewarmSeconds);
 *   - it waits, keeping its bank, while the level is at MaxHotSpots or while nobody can reach the area: a bounded area
 *     needs a player within HotSpotNearPlayerRadius of its outline (with the radius 0, or no player in the world, every
 *     bounded area counts as reachable); unbounded water (the default water, "everywhere" areas) needs a player;
 *   - otherwise it rolls its whole bank, chance 1 - exp(-bank / SpawnInterval), and the bank empties.
 *  So the level start counts as HotSpotPrewarmSeconds for every area, whenever a player first comes near it, and water
 *  nobody was near has been waiting (up to that long) when a player arrives (T-027c).
 *
 *  A passed roll tries HotSpotSpawnTries random points: in the part of the area within a player's reach (bounded areas; the
 *  whole outline when the near rule is off or nobody is in the world), or within OpenWaterSpawnRadius of a random player
 *  (unbounded water). A point is good if it is within HotSpotNearPlayerRadius of some player (when there are players and
 *  the radius is > 0), fishable water (not land) whose winning area is that area, whose habitat and depth the row allows
 *  (and at least MinBiteDepth), MinSpacing away from every live hot spot, and its whole wander area (the center plus 3 rings
 *  out to DriftRange + Radius / 2) is fishable water of an allowed habitat too. Then its actual drift path is walked over
 *  its lifetime: the disc (center + Radius) must stay on fishable water; at the first step where it would touch land the hot
 *  spot's life ends before it (it stops there and fades out), and a candidate left shorter than LifetimeMin is rejected.
 *  If no point is good the spawn stays due: the pair's next DueRetryChecks checks with a player in reach retry it without a
 *  roll, then it is dropped. At most MaxHotSpots live at once.
 *
 *  Diagnosing "no hot spots" in a level: `Log LogLureHotSpot Verbose` (console) logs one line per check (players, what each
 *  pair did, rejected points by reason) and a line per spawn and per failed search; VeryVerbose adds every pair's reason.
 */
UCLASS(Blueprintable)
class ALureHotSpotSpawner : public AActor
{
	GENERATED_BODY()

public:

	ALureHotSpotSpawner();

	/** Checks a due spawn (a passed roll that found no good point) is retried for, without a roll, before it is dropped. */
	static constexpr int32 DueRetryChecks = 3;

	/** Random candidates drawn in the part of a bounded area within a player's reach per try (rejection sampling). */
	static constexpr int32 WindowSamples = 16;

	/** DT_HotSpot rows that spawn in this level (empty = every row). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Hot Spots")
	TArray<FName> HotSpotTypes;

	/** Most hot spots alive at once in this level (< 0 = ULureWaterSettings::MaxHotSpots). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Hot Spots")
	int32 MaxHotSpots = -1;

	/** Seed of the spawn rolls (0 = a random seed at BeginPlay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Hot Spots")
	int32 RandomSeed = 0;

	/** The hot spot class spawned (a thin Blueprint child may set defaults). None = ALureHotSpot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Hot Spots")
	TSubclassOf<ALureHotSpot> HotSpotClass;

	/** Server: runs the spawn check now as if Seconds had passed; returns how many hot spots it spawned. Tests call this. */
	int32 SpawnStep(float Seconds);

	/** What the last SpawnStep did. */
	const FLureHotSpotStepStats& GetLastStepStats() const { return LastStats; }

	/** Tests: the seconds a (row, area) pair has banked (AreaId None = the default water). */
	float GetBankedSeconds(FName TypeId, FName AreaId) const;

	/** Tests: the retries left of a due spawn of a (row, area) pair (0 = nothing due). */
	int32 GetDueRetries(FName TypeId, FName AreaId) const;

	/** Tests: use this table instead of the settings' DT_HotSpot (null = the built-in row). */
	void SetHotSpotTable(const UDataTable* Table);

	/** Tests: reseed the spawn rolls. */
	void SetRandomSeed(int32 Seed);

	/** Tests: stop (or resume) the automatic checks in Tick; SpawnStep still works. */
	void SetAutoSpawn(bool bEnabled) { bAutoSpawn = bEnabled; }

	/** The live hot spots of this world (any spawner or the dev command), in a stable order. */
	static TArray<ALureHotSpot*> GetLiveHotSpots(const UWorld* World, double Time);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:

	FRandomStream Rng;
	bool bSeeded = false;
	bool bPrewarmed = false;
	bool bAutoSpawn = true;
	bool bLoggedNotTicking = false;
	bool bWarnedNoRows = false;
	float Accumulated = 0.f;

	/** Per (row, area id) pair; area id None = the default water. */
	TMap<TPair<FName, FName>, FLureHotSpotClock> Clocks;

	FLureHotSpotStepStats LastStats;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> TableOverride;

	/** The settings' DT_HotSpot once loaded (kept referenced for the level's lifetime). */
	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> LoadedTable;

	bool bUseTableOverride = false;

	/** One spawn attempt of Row in Area (null = the default water) near Reachers (see SamplePoint). Counts every rejected
	 *  candidate in OutRejects. */
	ALureHotSpot* TrySpawn(FName TypeId, const FLureHotSpotRow& Row, const FLureWaterAreaInfo* Area, TConstArrayView<FLureWaterAreaInfo> Areas,
		const FGameplayTag& DefaultHabitat, const TArray<FVector2D>& Players, const TArray<FVector2D>& Reachers, TArray<ALureHotSpot*>& Live, double Now,
		FLureHotSpotRejects& OutRejects);

	/**
	 *  A random candidate point: for a bounded Area, inside its outline and within HotSpotNearPlayerRadius of a random one of
	 *  Reachers (anywhere in the outline if Reachers is empty); for unbounded water, within OpenWaterSpawnRadius of a random
	 *  one of Reachers. False if there was nowhere to try.
	 */
	bool SamplePoint(const FLureWaterAreaInfo* Area, const TArray<FVector2D>& Reachers, FVector2D& OutXY);

	/** The water rules for a candidate point (see the class comment): None if it is good, else the first rule it breaks. */
	ELureHotSpotReject CheckPoint(const FVector2D& XY, FName AreaId, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
		const FGameplayTag& DefaultHabitat, const TArray<ALureHotSpot*>& Live, double Now, float& OutWaterZ) const;

	/** One point of a hot spot's wander area or disc: None if it is fishable water at least MinBiteDepth deep, on the water
	 *  level WaterZ, of a habitat the row allows; else the Wander* reason. */
	ELureHotSpotReject CheckFishableAt(const FVector2D& Point, float WaterZ, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
		const FGameplayTag& DefaultHabitat) const;

	/**
	 *  Walks the drift path of a hot spot (Row, Seed) anchored at Anchor for Lifetime seconds; returns how long its disc
	 *  (center + Radius) stays on fishable water: Lifetime, or the time of the last safe step before it would touch land.
	 */
	float SafeLifetime(const FVector2D& Anchor, float WaterZ, const FLureHotSpotRow& Row, int32 Seed, float Lifetime,
		TConstArrayView<FLureWaterAreaInfo> Areas, const FGameplayTag& DefaultHabitat) const;

	/** X/Y of every player's pawn (player controllers first; else every ALurePlayerCharacter, e.g. in tests). */
	TArray<FVector2D> GetPlayerLocations() const;

	void EnsureSeeded();
};

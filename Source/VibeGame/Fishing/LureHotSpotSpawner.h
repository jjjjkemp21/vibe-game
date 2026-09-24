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

/**
 *  Server-only logic (it does nothing on clients; the hot spots themselves replicate). A level without a spawner has no
 *  hot spots (test maps stay deterministic).
 *
 *  Every ULureWaterSettings::HotSpotCheckInterval seconds, for each DT_HotSpot row of HotSpotTypes (sorted by name) and
 *  each water area (sorted by id; the level's default water counts as one more area, last): if the area's habitat is
 *  allowed and fewer than MaxPerArea hot spots of that row live in it, a spawn happens with chance
 *  1 - exp(-seconds / SpawnInterval). A spawn tries HotSpotSpawnTries random points: inside the area's outline (bounded
 *  areas), or within OpenWaterSpawnRadius of a random player (the default water and "everywhere" areas). A point is good
 *  if it is within HotSpotNearPlayerRadius of some player (when there are players and the radius is > 0), fishable water
 *  (not land) whose winning area is that area, whose habitat and depth the row allows (and at least MinBiteDepth),
 *  MinSpacing away from every live hot spot, and its whole wander area (the center plus 3 rings out to DriftRange +
 *  Radius / 2) is fishable water of an allowed habitat too. Then its actual drift path is walked over its lifetime: the
 *  disc (center + Radius) must stay on fishable water; at the first step where it would touch land the hot spot's life
 *  ends before it (it stops there and fades out), and a candidate left shorter than LifetimeMin is rejected. The first
 *  check counts as HotSpotPrewarmSeconds, so a level starts with some hot spots. At most MaxHotSpots live at once.
 */
UCLASS(Blueprintable)
class ALureHotSpotSpawner : public AActor
{
	GENERATED_BODY()

public:

	ALureHotSpotSpawner();

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
	float Accumulated = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> TableOverride;

	/** The settings' DT_HotSpot once loaded (kept referenced for the level's lifetime). */
	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> LoadedTable;

	bool bUseTableOverride = false;

	/** One spawn attempt of Row in Area (null = the default water). */
	ALureHotSpot* TrySpawn(FName TypeId, const FLureHotSpotRow& Row, const FLureWaterAreaInfo* Area, TConstArrayView<FLureWaterAreaInfo> Areas,
		const FGameplayTag& DefaultHabitat, const TArray<FVector2D>& PlayerXY, TArray<ALureHotSpot*>& Live, double Now);

	/** A random candidate point in Area (or around a player for unbounded water). False if there is nowhere to try. */
	bool SamplePoint(const FLureWaterAreaInfo* Area, const TArray<FVector2D>& PlayerXY, FVector2D& OutXY);

	/** The water rules for a candidate point (see the class comment). */
	bool IsGoodPoint(const FVector2D& XY, FName AreaId, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
		const FGameplayTag& DefaultHabitat, const TArray<ALureHotSpot*>& Live, double Now, float& OutWaterZ) const;

	/** One point of a hot spot's wander area or disc: fishable water at least MinBiteDepth deep, on the water level WaterZ,
	 *  of a habitat the row allows. */
	bool IsFishableAt(const FVector2D& Point, float WaterZ, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
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

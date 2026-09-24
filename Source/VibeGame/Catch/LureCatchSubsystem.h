// Lure: per-world catch handling data and item registry (T-030). Rules: docs/specs/catch-handling-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Fish/FishRoll.h"
#include "LureCatchSubsystem.generated.h"

class ALureCarryableItem;
class ALureCoolerActor;
class APawn;
class APlayerState;
class UDataTable;
class UStreamableRenderAsset;

/**
 *  One per world: the catch tuning (DT_Catch row) and freshness rows (DT_Freshness), resolved on first use per world (a new
 *  PIE session reads re-imported tables), the fish tables for names and looks, and the list of carryable items (fish,
 *  coolers) so the hands can find what they hold without walking every actor.
 *  Tests inject data here (SetTuning, SetFreshnessTable, SetCoolerTable, SetFishTables) before spawning items.
 */
UCLASS()
class ULureCatchSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/** The subsystem of WorldContext's world (null without a world) */
	static ULureCatchSubsystem* Get(const UObject* WorldContext);

	/** The tuning for WorldContext's world, or the built-in row without a world */
	static const FLureCatchRow& GetTuningFor(const UObject* WorldContext);

	// ---- Data ----

	/** The DT_Catch row in use (built-in row + one warning if the table or row is missing or invalid) */
	const FLureCatchRow& GetTuning();

	/** The freshness row for a species (its own row, else the default row, else the built-in row) */
	FLureFreshnessRow GetFreshnessRow(FName SpeciesId);

	/** How the fish inside an open cooler of CoolerId are shown (its DT_CoolerDisplay row, else the built-in layout) */
	FLureCoolerDisplayRow GetCoolerDisplayRow(FName CoolerId);

	/** Server world time (FLureFreshness::GetServerTime) */
	double GetServerTime() const;

	/** Name shown for a species (its DisplayName; the id if the tables don't know it) */
	FText GetSpeciesDisplayName(FName SpeciesId);

	/** Name shown for a rarity (its DisplayName; the id if unknown) */
	FText GetRarityDisplayName(FName RarityId);

	/** Reference weight of a species, kg (the size its mesh is modeled at); 0 if unknown */
	float GetSpeciesReferenceWeight(FName SpeciesId);

	/** The species' mesh: its DT_FishSpecies Mesh, else the first existing ULureCatchSettings::FishMeshPaths asset; null = none */
	UStreamableRenderAsset* LoadSpeciesMesh(FName SpeciesId);

	// ---- Test and tool injection (this world only) ----

	void SetTuning(const FLureCatchRow& Row);

	/** Null = the built-in freshness row for every species */
	void SetFreshnessTable(const UDataTable* Table);

	/** DT_Cooler for coolers in this world (null = no table: FallbackCoolerSlots). Coolers spawned later get it at spawn. */
	void SetCoolerTable(const UDataTable* Table);

	/** The injected DT_Cooler (bOutInjected false = use the settings' table) */
	const UDataTable* GetCoolerTableOverride(bool& bOutInjected) const;

	/** Null = the built-in display layout for every cooler */
	void SetCoolerDisplayTable(const UDataTable* Table);

	/** Fish tables for names, reference weights and meshes (default: UFishSettings::LoadTables on first use) */
	void SetFishTables(const FFishTables& InTables);

	// ---- T-029 seam (optional): the landed fight fish becomes the hanging fish item's look ----

	/**
	 *  Offers a landed fish visual (T-029's ALureFightFish, kept alive with ULureFightFishSubsystem::KeepLandedFish) to the
	 *  fish item of the same catch (Fish.Seed + SpeciesId + RarityId) on this machine: adopted at once if that item exists,
	 *  else when it arrives (a client may see the landing before the item replicates). Destroyed after LandedVisualTimeout
	 *  seconds if nobody claims it. Visual must stop moving itself once offered (the item drives it).
	 */
	void OfferLandedVisual(AActor* Visual, const FFishInstance& Fish);

	/** The fish item side: the offered visual for Fish (removed from the offers), or null */
	AActor* ClaimLandedVisual(const FFishInstance& Fish);

	/** Seconds an offered visual waits for its item */
	static constexpr float LandedVisualTimeout = 5.0f;

	// ---- Items ----

	void RegisterItem(ALureCarryableItem* Item);
	void UnregisterItem(ALureCarryableItem* Item);

	/** Registered items that are still valid */
	TArray<ALureCarryableItem*> GetItems() const;

	/** Coolers that were made for PlayerState (its saved coolers and its starter cooler) */
	TArray<ALureCoolerActor*> GetCoolersOwnedBy(const APlayerState* PlayerState) const;

private:

	bool bTuningResolved = false;
	FLureCatchRow Tuning;

	bool bFreshnessResolved = false;
	bool bFreshnessInjected = false;
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> FreshnessTable;
	TSet<FName> WarnedFreshness;

	bool bDisplayResolved = false;
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> DisplayTable;

	bool bCoolerTableInjected = false;
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> CoolerTable;

	bool bFishTablesResolved = false;
	FFishTables FishTables;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> FishTableRefs;
	const FFishTables& GetFishTables();

	TArray<TWeakObjectPtr<ALureCarryableItem>> Items;

	struct FLandedVisualOffer
	{
		TWeakObjectPtr<AActor> Visual;
		int32 Seed = 0;
		FName SpeciesId;
		FName RarityId;
		double Time = 0.0;
	};
	TArray<FLandedVisualOffer> LandedOffers;
	FTimerHandle LandedOfferTimer;
	void PurgeLandedOffers();
};

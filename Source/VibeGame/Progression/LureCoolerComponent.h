// Lure: the player's cooler (T-010). Rules: docs/specs/progression-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fish/FishInstance.h"
#include "LureCoolerComponent.generated.h"

class UDataTable;
class ULureCoolerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLureCoolerChangedSignature, ULureCoolerComponent*, Cooler);

/**
 *  The cooler: a limited number of slots holding caught fish (FFishInstance records, never re-rolled).
 *
 *  Lives on the PlayerState (ALurePlayerState), not the pawn: it survives respawn and a change of pawn (the boat later),
 *  and the "caught" rule (T-017) empties it explicitly with Clear().
 *  Size: the DT_Cooler row CoolerId (default ULureProgressionSettings::DefaultCoolerId); upgrades call SetCoolerId.
 *  Server-authoritative: every mutator returns false / 0 on a client (with a Warning) and changes nothing. The fish list
 *  replicates to the owning player only; CoolerId and Capacity replicate to everyone.
 *  Fish are kept in the order they were added, with no gaps (slot = index).
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureCoolerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureCoolerComponent();

	// ---- Server-only changes ----

	/** Adds a fish at the end. False if not the server, the fish is invalid (no species), or the cooler is full. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	bool AddFish(const FFishInstance& Fish);

	/** AddFish that also returns the slot index it went into (INDEX_NONE if refused) */
	bool AddFishToSlot(const FFishInstance& Fish, int32& OutSlot);

	/** Removes the fish in SlotIndex (later fish move up one slot). False if not the server or the slot is empty. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	bool RemoveFish(int32 SlotIndex, FFishInstance& OutFish);

	/** Empties the cooler (T-017: getting caught loses unsold fish). Returns how many fish were removed (0 on a client). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	int32 Clear();

	/** Removes and returns every fish (selling); empty on a client */
	TArray<FFishInstance> TakeAll();

	/**
	 *  Switches to another DT_Cooler row (upgrade). False if not the server or the row does not exist. A smaller cooler
	 *  keeps every fish already inside (nothing is deleted); AddFish refuses until there is room again.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	bool SetCoolerId(FName NewCoolerId);

	/** Save/load (server): sets the cooler row and the fish as saved (invalid records dropped, never trimmed to capacity) */
	void RestoreState(FName InCoolerId, const TArray<FFishInstance>& InFish);

	// ---- Reading (server and owning client) ----

	/** The fish, slot = index */
	const TArray<FFishInstance>& GetFish() const { return StoredFish; }

	/** Blueprint copy of GetFish */
	UFUNCTION(BlueprintPure, Category="Lure|Cooler", meta=(DisplayName="Get Fish"))
	TArray<FFishInstance> GetAllFish() const { return StoredFish; }

	/** False if SlotIndex is empty */
	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	bool GetFishAt(int32 SlotIndex, FFishInstance& OutFish) const;

	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	int32 GetNumFish() const { return StoredFish.Num(); }

	/** Slots of the current cooler (>= 1) */
	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	int32 GetCapacity() const;

	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	int32 GetFreeSlots() const { return FMath::Max(0, GetCapacity() - StoredFish.Num()); }

	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	bool IsFull() const { return StoredFish.Num() >= GetCapacity(); }

	/** DT_Cooler row in use */
	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	FName GetCoolerId() const { return CoolerId; }

	/** Uses Table instead of the settings' DT_Cooler (tests, tools). Re-resolves the capacity on the server. */
	void SetCoolerTable(const UDataTable* Table);

	/** Slots of a DT_Cooler row. An unknown row uses the default row (settings DefaultCoolerId, Warning);
	 *  FallbackCoolerSlots only if DT_Cooler itself (or its default row) is missing. */
	int32 ResolveSlots(FName InCoolerId) const;

	/** The row to use for InCoolerId: None, or a row DT_Cooler doesn't have, becomes DefaultCoolerId (if that row exists).
	 *  Without a table the id is kept as is. */
	FName ResolveCoolerId(FName InCoolerId) const;

	/** After any change (server) and after replication (client) */
	UPROPERTY(BlueprintAssignable, Category="Lure|Cooler")
	FLureCoolerChangedSignature OnCoolerChanged;

	UFUNCTION()
	void OnRep_StoredFish();

	UFUNCTION()
	void OnRep_CoolerSize();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;

private:

	/** Caught fish, slot = index */
	UPROPERTY(ReplicatedUsing=OnRep_StoredFish)
	TArray<FFishInstance> StoredFish;

	UPROPERTY(ReplicatedUsing=OnRep_CoolerSize)
	FName CoolerId;

	/** Resolved on the server from DT_Cooler and replicated (clients don't need the table) */
	UPROPERTY(ReplicatedUsing=OnRep_CoolerSize)
	int32 Capacity = 0;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> CoolerTable;

	bool bTableInjected = false;

	bool CheckServer(const TCHAR* What) const;
	const UDataTable* GetCoolerTable() const;
	void EnsureCapacity();
	void NotifyChanged();
};

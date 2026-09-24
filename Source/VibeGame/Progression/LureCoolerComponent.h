// Lure: a cooler's storage (T-010 slots; T-030 on the physical cooler, records with freshness).
// Rules: docs/specs/catch-handling-rules.md "The cooler", progression-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Catch/LureCatchTypes.h"
#include "LureCoolerComponent.generated.h"

class UDataTable;
class ULureCoolerComponent;
struct FCoolerRow;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLureCoolerChangedSignature, ULureCoolerComponent*, Cooler);

/**
 *  The slots of a container of fish: FLureCaughtFish records (the roll's FFishInstance + freshness), never re-rolled.
 *
 *  Lives on ALureCoolerActor (T-030; a boat hold can reuse it later). Size: the DT_Cooler row CoolerId (default
 *  ULureProgressionSettings::DefaultCoolerId). Freshness inside: every record spoils at the storage's DecayRate (the owner
 *  sets it from the lid: the row's OpenDecayRate or ClosedDecayRate); a changed rate re-anchors every record at that moment.
 *  Server-authoritative: every mutator returns false / 0 on a client (with a Warning) and changes nothing. Everything
 *  replicates to everyone (a cooler is a shared world object). Fish are kept in the order they were added, with no gaps.
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureCoolerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureCoolerComponent();

	// ---- Server-only changes ----

	/** Adds a fish at the end, re-anchored now at DecayRate. False if not the server, the fish is invalid (no species), or it is full. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	bool AddFish(const FLureCaughtFish& Fish);

	/** AddFish that also returns the slot index it went into (INDEX_NONE if refused) */
	bool AddFishToSlot(const FLureCaughtFish& Fish, int32& OutSlot);

	/** Removes the fish in SlotIndex (later fish move up one slot), its record re-anchored now. False if not the server or the slot is empty. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	bool RemoveFish(int32 SlotIndex, FLureCaughtFish& OutFish);

	/** Removes the last fish put in (the top of the pile) */
	bool RemoveLastFish(FLureCaughtFish& OutFish);

	/** Empties it. Returns how many fish were removed (0 on a client). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	int32 Clear();

	/** Removes and returns every fish (re-anchored now); empty on a client */
	TArray<FLureCaughtFish> TakeAll();

	/**
	 *  Switches to another DT_Cooler row (upgrade). False if not the server or the row does not exist. A smaller cooler
	 *  keeps every fish already inside (nothing is deleted); AddFish refuses until there is room again.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Cooler")
	bool SetCoolerId(FName NewCoolerId);

	/** Spoiling speed inside from now on (negative or NaN = 0): every record is re-anchored now */
	void SetDecayRate(float NewRate);

	/** Save/load (server): the row, the rate and the fish as saved (invalid records dropped, never trimmed to capacity), anchored now */
	void RestoreState(FName InCoolerId, const TArray<FLureCaughtFish>& InFish, float InDecayRate);

	// ---- Reading (every machine) ----

	/** The fish, slot = index */
	const TArray<FLureCaughtFish>& GetFish() const { return StoredFish; }

	UFUNCTION(BlueprintPure, Category="Lure|Cooler", meta=(DisplayName="Get Fish"))
	TArray<FLureCaughtFish> GetAllFish() const { return StoredFish; }

	/** False if SlotIndex is empty */
	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	bool GetFishAt(int32 SlotIndex, FLureCaughtFish& OutFish) const;

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

	/** Spoiling speed inside now */
	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	float GetDecayRate() const { return DecayRate; }

	/** The DT_Cooler row of CoolerId (null without the table or row: then the built-in values apply, see FindRowOrDefault) */
	const FCoolerRow* FindRow() const;

	/** The row of CoolerId, else the built-in values (Slots = FallbackCoolerSlots, open 1, closed 0, carry 1, no meshes) */
	FCoolerRow FindRowOrDefault() const;

	/** Uses Table instead of the settings' DT_Cooler (tests, tools). Re-resolves the capacity on the server. */
	void SetCoolerTable(const UDataTable* Table);

	/** Slots of a DT_Cooler row. An unknown row uses the default row (settings DefaultCoolerId, Warning);
	 *  FallbackCoolerSlots only if DT_Cooler itself (or its default row) is missing. */
	int32 ResolveSlots(FName InCoolerId) const;

	/** The row to use for InCoolerId: None, or a row DT_Cooler doesn't have, becomes DefaultCoolerId (if that row exists).
	 *  Without a table the id is kept as is. */
	FName ResolveCoolerId(FName InCoolerId) const;

	/** After any change (server) and after replication (clients): contents, size, row */
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
	TArray<FLureCaughtFish> StoredFish;

	UPROPERTY(ReplicatedUsing=OnRep_CoolerSize)
	FName CoolerId;

	/** Resolved on the server from DT_Cooler and replicated (clients don't need the table) */
	UPROPERTY(ReplicatedUsing=OnRep_CoolerSize)
	int32 Capacity = 0;

	/** Spoiling speed of the records inside (server-written; clients read it from the records' own anchors) */
	UPROPERTY(Replicated)
	float DecayRate = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> CoolerTable;

	bool bTableInjected = false;

	bool CheckServer(const TCHAR* What) const;
	const UDataTable* GetCoolerTable() const;
	void EnsureCapacity();
	void NotifyChanged();
	double GetNow() const;
};

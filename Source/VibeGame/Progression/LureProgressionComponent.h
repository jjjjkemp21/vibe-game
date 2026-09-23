// Lure: money, XP and levels (T-010). Rules: docs/specs/progression-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fish/FishInstance.h"
#include "Progression/LureProgressionTypes.h"
#include "LureProgressionComponent.generated.h"

class UDataTable;
class ULureCoolerComponent;
class ULureProgressionComponent;
struct FFishLevelScaling;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLureLevelChangedSignature, ULureProgressionComponent*, Progression, int32, OldLevel, int32, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLureMoneyChangedSignature, ULureProgressionComponent*, Progression, int32, NewMoney, int32, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLureXpChangedSignature, ULureProgressionComponent*, Progression, int32, NewTotalXp, int32, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLureFishLandedSignature, ULureProgressionComponent*, Progression, const FFishInstance&, Fish, const FLureFishLandedResult&, Result);

/**
 *  A player's money, XP and level, plus the flows that touch the cooler (landing a fish, selling, save data).
 *
 *  Lives on the PlayerState next to ULureCoolerComponent (ALurePlayerState): kept on respawn and when caught.
 *  XP: TotalXp counts every XP ever earned; Level comes from DT_PlayerLevel (FLureLevelCurve) and never goes down.
 *  Server-authoritative: every mutator returns false / 0 on a client (with a Warning) and changes nothing.
 *  Replicated to everyone: Money, TotalXp, Level (OnRep events on clients).
 *  Events: OnLevelUp fires once per XP gain that crosses levels (a multi-level jump is ONE event, OldLevel -> NewLevel)
 *  on the server, and on clients when the replicated Level rises after BeginPlay. OnLevelChanged fires on every change
 *  (including loading a save, which never fires OnLevelUp on the server).
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureProgressionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureProgressionComponent();

	// ---- Server-only changes ----

	/**
	 *  THE entry point when a fish is landed (T-007 reel fight, T-025 Lure.GiveFish): gives the fish's XP (Fish.Xp, from
	 *  the roll) and puts it in the cooler if there is room. A full cooler releases the fish but the XP still counts.
	 *  Nothing happens for an invalid fish or on a client (bAccepted false). Fires OnFishLanded on the server.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Progression")
	FLureFishLandedResult HandleFishLanded(const FFishInstance& Fish);

	/** Adds Amount (> 0) coins, saturating at MAX_int32. False if not the server or Amount <= 0. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Progression")
	bool AddMoney(int32 Amount);

	/** Spends Amount (> 0) coins (shop T-012, repairs T-017). False, and nothing spent, if there is not enough money. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Progression")
	bool SpendMoney(int32 Amount);

	/** Adds Amount (> 0) XP and levels up as far as it reaches. Returns the levels gained (0 on a client). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Progression")
	int32 AddXp(int32 Amount);

	/** Sells every fish in the cooler at SellMultiplier (a sell point's market). Pays FLureProgressionRules::GetSellTotal. */
	FLureSaleResult SellAllFish(float SellMultiplier);

	/** Sells the fish in SlotIndex at SellMultiplier. Nothing sold if the slot is empty. */
	FLureSaleResult SellOneFish(int32 SlotIndex, float SellMultiplier);

	// ---- Save / load (T-019) ----

	/** Money, XP, level and the cooler (row + fish) */
	UFUNCTION(BlueprintCallable, Category="Lure|Progression")
	FLureProgressSaveData GetSaveData() const;

	/**
	 *  Server: restores a save. Level = max(saved Level, the level the XP gives), capped at the curve's max level (kept as
	 *  saved when there is no curve). Fires OnMoneyChanged, OnXpChanged and OnLevelChanged, never OnLevelUp.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Progression")
	bool ApplySaveData(const FLureProgressSaveData& Data);

	// ---- Level scaling (T-007 fight: "fish above your level escape easily") ----

	/**
	 *  Difficulty multiplier for a fish of FishLevel against this player's level: FFishRoll::LevelDifficultyMultiplier with
	 *  UFishSettings::LevelScaling (1 = same level, > 1 = the fish is above the player; data in DefaultGame.ini).
	 */
	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	float GetFishDifficultyMultiplier(int32 FishLevel) const;

	/** Same with explicit scaling (tests, tools) */
	float GetFishDifficultyMultiplierWith(int32 FishLevel, const FFishLevelScaling& Scaling) const;

	// ---- Reading ----

	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	int32 GetMoney() const { return Money; }

	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	int32 GetTotalXp() const { return TotalXp; }

	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	int32 GetLevel() const { return Level; }

	/** XP into the level, XP needed for the next, max level flag */
	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	FLureLevelProgress GetLevelProgress() const;

	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	int32 GetMaxLevel() const { return GetLevelCurve().GetMaxLevel(); }

	/** The cooler on the same actor (null if there is none) */
	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	ULureCoolerComponent* GetCooler() const;

	/** Placeholder HUD line (T-011 replaces it): "Money 120   Level 3 (XP 40/110)   Cooler 5/8" */
	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	FString GetStatusText() const;

	// ---- Data ----

	/** Uses Table instead of the settings' DT_PlayerLevel (tests, tools). Call before BeginPlay or re-check the level. */
	void SetLevelTable(const UDataTable* Table);

	/** The XP curve in use (built on first use from the injected or settings table; empty = level 1 forever) */
	const FLureLevelCurve& GetLevelCurve() const;

	// ---- Events ----

	UPROPERTY(BlueprintAssignable, Category="Lure|Progression")
	FLureLevelChangedSignature OnLevelUp;

	UPROPERTY(BlueprintAssignable, Category="Lure|Progression")
	FLureLevelChangedSignature OnLevelChanged;

	UPROPERTY(BlueprintAssignable, Category="Lure|Progression")
	FLureMoneyChangedSignature OnMoneyChanged;

	UPROPERTY(BlueprintAssignable, Category="Lure|Progression")
	FLureXpChangedSignature OnXpChanged;

	/** Server only: after HandleFishLanded accepted a fish (T-011 journal can listen here) */
	UPROPERTY(BlueprintAssignable, Category="Lure|Progression")
	FLureFishLandedSignature OnFishLanded;

	// ---- Replication (the OnReps are public so tests can drive the client path) ----

	UFUNCTION()
	void OnRep_Money(int32 OldMoney);

	UFUNCTION()
	void OnRep_TotalXp(int32 OldTotalXp);

	UFUNCTION()
	void OnRep_Level(int32 OldLevel);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;

private:

	UPROPERTY(ReplicatedUsing=OnRep_Money)
	int32 Money = 0;

	UPROPERTY(ReplicatedUsing=OnRep_TotalXp)
	int32 TotalXp = 0;

	UPROPERTY(ReplicatedUsing=OnRep_Level)
	int32 Level = 1;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> LevelTable;

	bool bTableInjected = false;
	bool bSaveApplied = false;
	mutable bool bCurveBuilt = false;
	mutable FLureLevelCurve Curve;

	bool CheckServer(const TCHAR* What) const;
	void SetMoneyInternal(int32 NewMoney);
	void ForceNetUpdate();
};

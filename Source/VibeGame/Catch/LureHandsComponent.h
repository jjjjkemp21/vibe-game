// Lure: what a player holds (T-030): a fish in a hand, a cooler in both, a landed fish on the hook.
// Rules: docs/specs/catch-handling-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Character/FPArmsPose.h"
#include "LureHandsComponent.generated.h"

class ALureCarryableItem;
class ALureCoolerActor;
class ALureFishItem;
class APawn;

/**
 *  On the player's pawn (ALurePlayerCharacter). Caches what the pawn holds from the items' replicated Hold (the items are
 *  the truth): refreshed directly on the server and from the items' OnReps on clients, so every machine agrees.
 *  - Hands full (a fish or a cooler): the rod is stowed (fishing reads IsRodStowedFor), the arms pose is the item's
 *    (HoldFish, CarryCooler), the move speed is the item's (a cooler's CarrySpeedMultiplier).
 *  - A fish on the hook: casting is refused until it is grabbed or let go (HasFishOnHookFor).
 *  Server: every change goes through the Authority* functions. The server also empties the hands when the player falls
 *  into the water (items go to the last dry ground spot), puts a carried cooler down when the player goes prone, and
 *  frees everything when the pawn leaves the world.
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureHandsComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureHandsComponent();

	/** The hands of Pawn (null if it has none) */
	static ULureHandsComponent* Get(const AActor* Pawn);

	// ---- State (every machine) ----

	/** The item in the hand(s), or null */
	UFUNCTION(BlueprintPure, Category="Lure|Hands")
	ALureCarryableItem* GetHeldItem() const;

	UFUNCTION(BlueprintPure, Category="Lure|Hands")
	ALureFishItem* GetHeldFish() const;

	UFUNCTION(BlueprintPure, Category="Lure|Hands")
	ALureCoolerActor* GetCarriedCooler() const;

	/** The landed fish hanging on this player's hook, or null */
	UFUNCTION(BlueprintPure, Category="Lure|Hands")
	ALureFishItem* GetHangingFish() const;

	UFUNCTION(BlueprintPure, Category="Lure|Hands")
	bool IsHoldingSomething() const { return GetHeldItem() != nullptr; }

	/** The rod is put away: the hands hold something */
	bool IsRodStowed() const { return IsHoldingSomething(); }

	/** The arms pose of the held item (HoldFish, CarryCooler); false = the rod pose rules decide */
	bool GetArmsPoseOverride(EFPArmsPose& OutPose) const;

	/** Move speed multiplier on land (1 with empty hands) */
	float GetMoveSpeedMultiplier() const;

	/** Where the hanging fish's line starts: the fishing component's line start (the rod tip), else in front of the eye */
	FVector GetHangPivot() const;

	/** For other systems that only have the pawn (fishing): its hands hold something */
	static bool IsRodStowedFor(const AActor* Pawn);

	/** For other systems that only have the pawn (fishing): a landed fish hangs on its hook */
	static bool HasFishOnHookFor(const AActor* Pawn);

	/** The hands can take an item: not while swimming (or climbing out); the water empties them anyway */
	bool CanHoldItems() const;

	/** Lying prone (a cooler can't be carried crawling) */
	bool IsPawnProne() const;

	/** Placeholder HUD line: "Holding: Bonefish (Rare), 2.04 kg, 45 coins, fresh 87%" (empty with empty hands) */
	FString GetHeldText() const;

	// ---- Server ----

	/** Puts a free item (or this player's hanging fish) in the hand(s). False if the hands are full or can't hold (swimming), a two-handed item meets a hanging fish, or the item is held by someone else. */
	bool AuthorityTakeInHand(ALureCarryableItem* Item);

	/** Hangs Fish on this player's hook (a fish already hanging drops off first). False if not the server or the fish is held by someone. */
	bool AuthorityHangFish(ALureFishItem* Fish);

	/** Empties the hand(s) and returns the item (the caller places, stores or destroys it); null with empty hands */
	ALureCarryableItem* AuthorityReleaseHeld();

	/** Takes the hanging fish off the hook and returns it (the caller places or destroys it); null if none */
	ALureFishItem* AuthorityReleaseHanging();

	/**
	 *  Frees everything (falling in, leaving, getting caught): fish in the hand or on the hook are lost when bLoseFish (caught),
	 *  else they drop at the last dry ground spot; a carried cooler is always put down there. Returns the fish lost.
	 */
	int32 AuthorityDropEverything(bool bLoseFish);

	/** The last place this player stood on dry ground (server; false before the first) */
	bool GetLastDryGround(FVector& OutLocation) const;

	/** Tests and tools: set the last dry ground spot */
	void SetLastDryGround(const FVector& Location);

	/** Server -> the owning player's machine: a placeholder notice ("Released the Bonefish") in their HUD notices */
	UFUNCTION(Client, Reliable)
	void ClientNotice(const FString& Text);

	// ---- Item bookkeeping (the items call this) ----

	/** Re-reads which registered items this pawn holds (their replicated Hold) */
	void RefreshFromItems();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	TWeakObjectPtr<ALureCarryableItem> HeldItem;
	TWeakObjectPtr<ALureFishItem> HangingFish;

	bool bHasDryGround = false;
	FVector LastDryGround = FVector::ZeroVector;
	double NextDryGroundSample = 0.0;

	APawn* GetPawn() const;
	bool CheckServer(const TCHAR* What) const;
	void UpdateDryGround();
	void ApplyForcedDrops();
	/** Puts a fish (held, hanging or free) down lying at Location */
	void PutFishAt(ALureFishItem* Fish, const FVector& Location) const;
	/** Where forced drops go: the last dry ground spot, else the pawn's feet */
	FVector GetForcedDropSpot() const;
};

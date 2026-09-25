// Lure: the shop's sell counter (T-030; replaces T-010's ALureSellPoint). Rules: docs/specs/catch-handling-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/LureInteractable.h"
#include "Progression/LureProgressionTypes.h"
#include "LureSellCounter.generated.h"

class ALureFishItem;
class APawn;
class UBoxComponent;
class UDataTable;
class USceneComponent;

/**
 *  Placed on a shop counter top (the level's sell_point marker class): origin = the center of the counter top, +X toward the
 *  customers, CounterHalfSize = the top area (half depth, half length, half the height above the top that counts).
 *  Fish that land inside that area (placed with Interact, or dropped) are on the counter: they are part of it (not picked
 *  up one by one; Alt Interact takes the last one back) and keep spoiling. Interact with empty hands sells them all:
 *  price = FLureFreshness::GetSellPrice (rolled Value x freshness x the DT_FishMarket row's SellMultiplier), paid to the
 *  seller, with the "Sold N fish for X coins" notice. Server-authoritative; the fish on it are items (they replicate).
 */
UCLASS(Blueprintable)
class ALureSellCounter : public AActor, public ILureInteractable
{
	GENERATED_BODY()

public:

	ALureSellCounter();

	/** DT_FishMarket row of this buyer; None = ULureProgressionSettings::DefaultMarketId. (The four settings replicate once, for
	 *  counters spawned at runtime; a counter placed in the level has them from the map on every machine.) */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Sell Counter")
	FName MarketId;

	/** The counter top area that holds fish: X = half depth, Y = half length, Z = half the height above the top, cm */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Sell Counter")
	FVector CounterHalfSize = FVector(30.0f, 230.0f, 20.0f);

	/** Players within this distance (cm, from the counter's origin to the pawn) can use it */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Sell Counter", meta=(ClampMin="50", Units="cm"))
	float InteractionRadius = 300.0f;

	/** Fish put on the counter lie this far apart along it, cm */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Sell Counter", meta=(ClampMin="10", Units="cm"))
	float FishSpacing = 45.0f;

	/** The market row this counter uses (MarketId or the default) */
	UFUNCTION(BlueprintPure, Category="Sell Counter")
	FName GetEffectiveMarketId() const;

	/** The market's SellMultiplier; 1 (with a Warning, once) if the table or row is missing or the value is invalid */
	UFUNCTION(BlueprintPure, Category="Sell Counter")
	float GetSellMultiplier() const;

	/** Uses Table instead of the settings' DT_FishMarket (tests, tools) */
	void SetMarketTable(const UDataTable* Table);

	/** Point lies on the counter top (inside the area) */
	bool ContainsPoint(const FVector& Point) const;

	/** The counter whose top holds Location, or null */
	static ALureSellCounter* FindCounterAt(const UWorld* World, const FVector& Location);

	/** The fish lying on this counter (every machine) */
	TArray<ALureFishItem*> GetFishOnCounter() const;

	/**
	 *  T-030h: a fingerprint of the fish on the counter (their records, order-free; never 0). Every machine computes the same
	 *  value from the same replicated fish, so the one a client's Sell prompt was drawn from can be checked on the server.
	 */
	int32 GetContentsToken() const;

	/** What selling everything on it would pay now */
	UFUNCTION(BlueprintPure, Category="Sell Counter")
	int32 QuoteAll() const;

	/** The next free spot along the counter (world): where Interact puts a fish. Centre first, then outwards; full = centre. */
	FTransform GetPlacementSpot() const;

	/** Local Y offsets of the counter's fish spots in fill order: centre first, then alternating outwards (+, -), all inside
	 *  +-HalfLengthY (an even count starts with the two middle spots). Spacing is clamped to >= 10; at least one spot (0). */
	static TArray<float> GetSpotOffsets(float HalfLengthY, float Spacing);

	// ---- Server ----

	/** Sells every fish on the counter to Seller (in range + slack, with progression): pays, removes the fish, notice. */
	FLureSaleResult AuthoritySell(APawn* Seller);

	/** Puts Seller's held fish on the counter at the next free spot */
	bool AuthorityPlaceFish(APawn* Seller, ALureFishItem* Fish);

	/** The last fish put on the counter goes back into Pawn's empty hand */
	bool AuthorityTakeBack(APawn* Pawn);

	// ---- ILureInteractable ----

	virtual FVector GetInteractionLocation() const override { return GetActorLocation(); }
	virtual float GetInteractionRadius() const override { return InteractionRadius; }
	virtual float GetFocusAngle(const FVector& ViewLocation, const FVector& ViewDirection) const override;
	virtual double GetFocusHitDistance(const FVector& ViewLocation, const FVector& ViewDirection) const override;
	virtual bool CanInteract(const APawn* Pawn) const override;
	virtual FLureInteraction GetInteraction(const APawn* Pawn, ELureInteractKey Key) const override;
	/** T-030h: Sell = GetContentsToken (the server sells only the fish the seller's prompt showed); other verbs 0 */
	virtual int32 GetInteractionStateToken(const APawn* Pawn, ELureInteractVerb Verb) const override;
	virtual bool PerformInteraction(APawn* Pawn, ELureInteractVerb Verb) override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Root;

	/** Editor view of the counter area (no collision, hidden in game) */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> AreaPreview;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MarketTable;

	bool bMarketTableInjected = false;

	/** Fish placed here, oldest first (server; take-back order) */
	TArray<TWeakObjectPtr<ALureFishItem>> PlacedOrder;

	mutable bool bWarnedMarket = false;
	void WarnMarketOnce(const FString& Message) const;
};

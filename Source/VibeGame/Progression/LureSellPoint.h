// Lure: a place to sell the cooler (T-010).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/LureInteractable.h"
#include "Progression/LureProgressionTypes.h"
#include "LureSellPoint.generated.h"

class APawn;
class UDataTable;
class USceneComponent;
class USphereComponent;

/**
 *  Placeable sell point (the Palm Key dock; later NPC buyers). A player inside InteractionRadius sees the prompt
 *  "[E] Sell N fish (X coins)" and the Interact key sells the whole cooler. Prices: FLureProgressionRules::GetSellPrice
 *  = round-half-up(fish Value * the DT_FishMarket row's SellMultiplier), so the fish's rolled Value is the only pricing.
 *  Server-authoritative: SellAll / SellOne run on the server (reached through ULureInteractionComponent's server RPC).
 *  The sphere only shows the radius in the editor (no collision, hidden in game).
 */
UCLASS(Blueprintable)
class ALureSellPoint : public AActor, public ILureInteractable
{
	GENERATED_BODY()

public:

	ALureSellPoint();

	/** DT_FishMarket row of this buyer; None = ULureProgressionSettings::DefaultMarketId */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sell Point")
	FName MarketId;

	/** Players within this distance (cm, from the actor's location to the pawn's) can sell here */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sell Point", meta=(ClampMin="50", Units="cm"))
	float InteractionRadius = 300.0f;

	/** The market row this point uses (MarketId or the default) */
	UFUNCTION(BlueprintPure, Category="Sell Point")
	FName GetEffectiveMarketId() const;

	/** The market's SellMultiplier; 1 (with a Warning) if the table or row is missing or the value is invalid */
	UFUNCTION(BlueprintPure, Category="Sell Point")
	float GetSellMultiplier() const;

	/** Server: sells Seller's whole cooler. Nothing happens for a pawn out of range (radius + ServerRangeSlack) or without progression. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Sell Point")
	FLureSaleResult SellAll(APawn* Seller);

	/** Server: sells one cooler slot of Seller (same checks as SellAll) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Sell Point")
	FLureSaleResult SellOne(APawn* Seller, int32 SlotIndex);

	/** What SellAll would pay Seller right now */
	UFUNCTION(BlueprintPure, Category="Sell Point")
	int32 QuoteAll(const APawn* Seller) const;

	/** Uses Table instead of the settings' DT_FishMarket (tests, tools) */
	void SetMarketTable(const UDataTable* Table);

	// ILureInteractable
	virtual FVector GetInteractionLocation() const override { return GetActorLocation(); }
	virtual float GetInteractionRadius() const override { return InteractionRadius; }
	virtual bool CanInteract(const APawn* Pawn) const override;
	virtual FText GetInteractionPrompt(const APawn* Pawn) const override;
	virtual bool Interact(APawn* Pawn, int32 Option) override;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Root;

	/** Editor view of InteractionRadius */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USphereComponent> RadiusPreview;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MarketTable;

	bool bMarketTableInjected = false;

	/** Market problems are logged once per sell point (the HUD quotes prices every frame) */
	mutable bool bWarnedMarket = false;
	void WarnMarketOnce(const FString& Message) const;
};

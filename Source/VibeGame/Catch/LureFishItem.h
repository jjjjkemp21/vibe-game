// Lure: a caught fish as a physical item (T-030): on the hook, in a hand, or lying on the ground or the sell counter.
// Rules: docs/specs/catch-handling-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Catch/LureCarryableItem.h"
#include "LureFishItem.generated.h"

class ALureFishItem;
class ALureSellCounter;
class ULureFishingLineComponent;

/** A fish item started (bHooked true) or stopped hanging on a hook, on this machine (every machine: server, owner, others) */
DECLARE_MULTICAST_DELEGATE_TwoParams(FLureFishHookedChanged, ALureFishItem* /*Fish*/, bool /*bHooked*/);
/** A fish item adopted a landed fight fish as its look (T-029 seam), on this machine; VisualWorld = where that fish was just before */
DECLARE_MULTICAST_DELEGATE_TwoParams(FLureFishVisualAdopted, ALureFishItem* /*Fish*/, const FTransform& /*VisualWorld*/);
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 *  A catch in the world: the FLureCaughtFish record (the roll's FFishInstance + freshness) and its look: the species mesh
 *  (DT_FishSpecies Mesh, else /Game/Art/Fish/SK_<Species> or SM_<Species>, else a placeholder shape) scaled by
 *  (Weight / ReferenceWeight)^(1/3).
 *  - Hook: hangs by its mouth ("Mouth" bone or socket) from the holder's hang pivot on a damped pendulum, with a short line.
 *  - Hand: its "Grip" in the hand (ULureCatchSettings sockets and offsets).
 *  - Free: lying on its side at the placement; on a sell counter it belongs to the counter (not picked up one by one).
 *  Outside a cooler it always spoils at rate 1. Into a cooler, only the record goes (the item is removed).
 */
UCLASS(Blueprintable)
class ALureFishItem : public ALureCarryableItem
{
	GENERATED_BODY()

public:

	ALureFishItem();

	/** Server: spawns a free fish item at Transform (ULureCatchSettings::FishItemClass). Null without a world or on a client. */
	static ALureFishItem* SpawnFish(UWorld* World, const FLureCaughtFish& InCatch, const FTransform& Transform);

	// ---- The record ----

	const FLureCaughtFish& GetCatch() const { return Catch; }

	UFUNCTION(BlueprintPure, Category="Lure|Fish Item")
	FFishInstance GetFish() const { return Catch.Fish; }

	/** Server: sets the record (spoiling at rate 1: it is out of a cooler) */
	void AuthoritySetCatch(const FLureCaughtFish& InCatch);

	/** The record with its exposure sampled now (what a cooler or a sale takes) */
	FLureCaughtFish GetCatchNow() const;

	// ---- Freshness now (every machine) ----

	UFUNCTION(BlueprintPure, Category="Lure|Fish Item")
	float GetExposureSeconds() const;

	/** 1 = fresh, 0 = spoiled (DT_Freshness) */
	UFUNCTION(BlueprintPure, Category="Lure|Fish Item")
	float GetFreshness01() const;

	UFUNCTION(BlueprintPure, Category="Lure|Fish Item")
	float GetValueShare() const;

	/** What it is worth now at a x1 buyer */
	UFUNCTION(BlueprintPure, Category="Lure|Fish Item")
	int32 GetCurrentValue() const;

	/** "Bonefish (Rare), 2.04 kg, 45 coins, fresh 87%" */
	FString GetDescription() const;

	// ---- The sell counter ----

	/** The counter it lies on (null if none) */
	ALureSellCounter* GetCounter() const { return Counter; }

	void AuthoritySetCounter(ALureSellCounter* InCounter);

	// ---- Server: letting go ----

	/**
	 *  Lets go of the fish (from a hand, the hook or the ground): it falls toward Direction2D for Distance cm from StartXY,
	 *  starting at Origin, and lands by the cast landing rules (FLureFishingSpots::ResolveLanding). On land it lies there (on a
	 *  counter if it landed on one); on water it is released: removed, and Pawn's player sees "Released the <fish>". True = on land.
	 */
	bool AuthorityDrop(APawn* Pawn, const FVector& Origin, const FVector2D& StartXY, const FVector2D& Direction2D, float Distance);

	/** Releases it into the water: removed at once, Pawn's player notified */
	void AuthorityRelease(APawn* Pawn);

	// ---- Look ----

	/** (Weight / ReferenceWeight)^(1/3), 1 for an unknown species */
	float GetWeightScale() const { return WeightScale; }

	/** The mouth (line attach) and grip (hand) points in actor space, scaled */
	FVector GetMouthOffset() const { return MouthOffset * WeightScale; }
	FVector GetGripOffset() const { return GripOffset * WeightScale; }

	/** How high its origin sits above the ground when it lies on its side, cm */
	float GetLieHeight() const { return LieHeight * WeightScale; }

	const FLureHangPendulum& GetPendulum() const { return Pendulum; }

	ULureFishingLineComponent* GetHangLine() const { return HangLine; }

	/** True once the species mesh (or the placeholder) is set up on this machine */
	bool HasLook() const { return bLookReady; }

	/** The skeletal or static mesh in use (null on a dedicated server) */
	UPrimitiveComponent* GetFishMesh() const;

	// ---- T-029 seam (optional): a landed fight fish as this item's look ----

	/**
	 *  Uses Visual (T-029's landed fight fish: the same species mesh at the same size, still playing its pose) as the look
	 *  from now on: this item's own meshes hide, Visual follows the item in every state and is destroyed with it. A hanging
	 *  fish starts its swing from where Visual is, so the hand-off is smooth. False on a dedicated server or for a bad actor.
	 *  Offers made before this item exists wait in ULureCatchSubsystem::OfferLandedVisual (claimed in EnsureLook).
	 */
	bool AdoptVisual(AActor* Visual);

	AActor* GetAdoptedVisual() const { return AdoptedVisual; }

	/** Fires after AdoptVisual on every rendering machine: where T-032's hanging line re-seats its end (ULureCatchLinkSubsystem) */
	static FLureFishVisualAdopted OnLandedVisualAdopted;

	// ---- T-032 seam (optional): an external hang driver (the physics line's end) ----

	/**
	 *  While hooked, Driver moves this actor instead of the built-in pendulum, e.g. T-032's line after
	 *  AttachEndActor(this, GetHangLineLength(), GetMouthOffset()): the pendulum and the short line stay off on this machine
	 *  and the item leaves its attachment alone. Cleared automatically when the fish leaves the hook (the next hanging fish
	 *  starts with the pendulum until a driver is set again). Null = the pendulum. Local to this machine.
	 */
	void SetExternalHangDriver(UObject* Driver);

	UObject* GetExternalHangDriver() const { return ExternalHangDriver.Get(); }

	/** The line length a hanging fish hangs on (DT_Catch HangLineLength), cm */
	float GetHangLineLength() const;

	/** Fires on every machine when a fish item starts or stops hanging: where T-032's line attaches (AttachEndActor) */
	static FLureFishHookedChanged OnHookedChanged;

	// ---- ALureCarryableItem ----

	virtual bool GetHoldPose(EFPArmsPose& OutPose) const override;
	virtual FText GetItemName() const override;

	// ---- ILureInteractable ----

	virtual bool CanInteract(const APawn* Pawn) const override;
	virtual FLureInteraction GetInteraction(const APawn* Pawn, ELureInteractKey Key) const override;
	virtual bool PerformInteraction(APawn* Pawn, ELureInteractVerb Verb) override;
	virtual FVector GetInteractionLocation() const override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:

	UPROPERTY(ReplicatedUsing=OnRep_Catch, BlueprintReadOnly, Category="Lure|Fish Item")
	FLureCaughtFish Catch;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Lure|Fish Item")
	TObjectPtr<ALureSellCounter> Counter;

	UFUNCTION()
	void OnRep_Catch();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> StaticFish;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> SkeletalFish;

	UPROPERTY(Transient)
	TObjectPtr<ULureFishingLineComponent> HangLine;

	/** T-029's landed fight fish drawn instead of the own meshes (local to this machine) */
	UPROPERTY(Transient)
	TObjectPtr<AActor> AdoptedVisual;

	virtual bool GetFirstPersonAttachment(const APawn* Holder, USceneComponent*& OutParent, FName& OutSocket, FTransform& OutRelative) const override;
	virtual FTransform GetThirdPersonAttachment() const override;
	virtual FTransform GetRestVisualTransform() const override;
	virtual void UpdateHooked(float DeltaSeconds) override;
	virtual void OnHoldChanged(const FLureItemHold& OldHold) override;
	virtual bool IsHookedExternallyDriven() const override { return ExternalHangDriver.IsValid(); }

private:

	FName LookSpecies;
	bool bLookReady = false;
	float WeightScale = 1.0f;
	/** Unscaled mesh-space points */
	FVector MouthOffset = FVector(25.0f, 0.0f, 0.0f);
	FVector GripOffset = FVector(5.0f, 0.0f, 0.0f);
	float LieHeight = 5.5f;

	FLureHangPendulum Pendulum;

	/** T-029 seam: where the adopted landed fish's mouth was, for the first swing when the item gets hooked after adopting it */
	FVector SwingStart = FVector::ZeroVector;
	bool bHasSwingStart = false;

	/** T-032 seam: the physics line (or anything) that moves the hanging fish on this machine */
	TWeakObjectPtr<UObject> ExternalHangDriver;

	void EnsureLook();
	void EnsureHangLine();
	void GetViewer(FVector& OutLocation, float& OutFovDeg) const;
	double GetNow() const;
};

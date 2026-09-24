// Lure: a physical item a player can hold (T-030: fish, coolers; later bait, gear, boat cargo).
// Rules: docs/specs/catch-handling-rules.md "Items and the carry model".

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Catch/LureCatchTypes.h"
#include "Character/FPArmsPose.h"
#include "Interaction/LureInteractable.h"
#include "LureCarryableItem.generated.h"

class APawn;
class UPrimitiveComponent;
class USceneComponent;

/**
 *  Base of every physical item. Replicated state (server-written):
 *    Hold      who holds it and how (None = free, Hand, Hook) - the single source of truth; ULureHandsComponent only caches it;
 *    Placement where a free item rests (the actor location at once) and where its mesh flies from.
 *  Presentation is local to each machine: held by the local player = attached to their first-person arms and drawn as a
 *  first-person primitive; held by someone else = attached to their body at a third-person offset (ULureCatchSettings);
 *  free = at the placement (the mesh flies there along a short arc). Movement is not replicated: machines place it themselves.
 *  Subclasses answer: hold kind, arms pose, carry speed, name, interactions (GetInteraction / PerformInteraction).
 */
UCLASS(Abstract, Blueprintable)
class ALureCarryableItem : public AActor, public ILureInteractable
{
	GENERATED_BODY()

public:

	ALureCarryableItem();

	// ---- What kind of item (subclasses) ----

	/** One hand (a fish) or both (a cooler) */
	virtual ELureHoldKind GetHoldKind() const { return ELureHoldKind::OneHand; }

	/** The arms pose while it is in a hand; false = keep the rod pose */
	virtual bool GetHoldPose(EFPArmsPose& OutPose) const { return false; }

	/** Move speed multiplier of the player carrying it (on land) */
	virtual float GetCarrySpeedMultiplier() const { return 1.0f; }

	/** Name for prompts: "Bonefish", "Starter cooler" */
	virtual FText GetItemName() const;

	// ---- State (every machine) ----

	const FLureItemHold& GetHold() const { return Hold; }

	UFUNCTION(BlueprintPure, Category="Lure|Item")
	APawn* GetHolder() const { return Hold.Holder; }

	UFUNCTION(BlueprintPure, Category="Lure|Item")
	ELureHoldMode GetHoldMode() const { return Hold.IsHeld() ? Hold.Mode : ELureHoldMode::None; }

	/** Nobody holds it: it stands or lies in the world */
	UFUNCTION(BlueprintPure, Category="Lure|Item")
	bool IsFree() const { return !Hold.IsHeld(); }

	/** Pawn holds it in Mode */
	bool IsHeldBy(const APawn* Pawn, ELureHoldMode Mode) const;

	const FLureItemPlacement& GetPlacement() const { return Placement; }

	/** The machine's local player holds it (first-person presentation) */
	bool IsHeldByLocalPlayer() const;

	// ---- Server ----

	/** Sets who holds it (a null holder or Mode None = free where it is). Refreshes both holders' hands, collision and the look. */
	void AuthoritySetHold(APawn* NewHolder, ELureHoldMode NewMode);

	/** Frees it at Location / Rotation (the actor moves there now); bAnimate = its mesh flies there from From */
	void AuthorityPlace(const FVector& Location, const FRotator& Rotation, const FVector& From, bool bAnimate);

	// ---- ILureInteractable ----

	/** Held or hooked: the holder's location (always in their reach); free: the actor location */
	virtual FVector GetInteractionLocation() const override;

	/** DT_Catch ReachDistance */
	virtual float GetInteractionRadius() const override;

	virtual float GetFocusRadius() const override { return 25.0f; }

	/** No verbs by default (subclasses offer theirs); the base must be constructible (UObject class default objects) */
	virtual FLureInteraction GetInteraction(const APawn* Pawn, ELureInteractKey Key) const override { return FLureInteraction(); }
	virtual bool PerformInteraction(APawn* Pawn, ELureInteractVerb Verb) override { return false; }

	// ---- AActor ----

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Items don't use the engine's movement or attachment replication (Hold and Placement replicate instead) */
	virtual void GatherCurrentMovement() override;
	virtual void OnRep_AttachmentReplication() override;

	/** Where the drawn item is now (the visual root: the drop flight moves it), world */
	FTransform GetVisualTransform() const;

	/** Seconds left of the drop flight on this machine (0 = at rest) */
	float GetFlightTimeLeft() const;

protected:

	UPROPERTY(ReplicatedUsing=OnRep_Hold, BlueprintReadOnly, Category="Lure|Item")
	FLureItemHold Hold;

	UPROPERTY(ReplicatedUsing=OnRep_Placement, BlueprintReadOnly, Category="Lure|Item")
	FLureItemPlacement Placement;

	UFUNCTION()
	void OnRep_Hold(const FLureItemHold& OldHold);

	UFUNCTION()
	void OnRep_Placement();

	/** The actor's gameplay transform */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> ItemRoot;

	/** Everything drawn hangs under this: the drop flight moves it, free items offset it (a fish lies on its side) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> VisualRoot;

	// ---- Subclass hooks ----

	/** After the hold changed (every machine, after the presentation): collision, lids, lines */
	virtual void OnHoldChanged(const FLureItemHold& OldHold) {}

	/** First-person attachment for a local holder: parent component, socket and the actor's relative transform. False = the camera. */
	virtual bool GetFirstPersonAttachment(const APawn* Holder, USceneComponent*& OutParent, FName& OutSocket, FTransform& OutRelative) const { return false; }

	/** The actor's transform relative to the holder's root (capsule center) as other players see it */
	virtual FTransform GetThirdPersonAttachment() const { return FTransform::Identity; }

	/** Relative transform of the visual root while free (e.g. a fish lying on its side) */
	virtual FTransform GetRestVisualTransform() const { return FTransform::Identity; }

	/** Per-frame presentation while hooked (fish): place the actor. Rendering machines only. */
	virtual void UpdateHooked(float DeltaSeconds) {}

	/** While hooked, something else moves this actor (fish: T-032's physics line): the presentation leaves its attachment alone */
	virtual bool IsHookedExternallyDriven() const { return false; }

	/** Per-frame presentation for subclasses (lids), rendering machines only */
	virtual void UpdatePresentation(float DeltaSeconds) {}

	/** Attaches, detaches and switches first-person drawing for this machine from Hold and Placement */
	void RefreshPresentation();

	/** Draw every primitive of the item as a first-person primitive (the local holder) or as a world one */
	void SetFirstPersonRendering(bool bFirstPerson);

	/** True on machines that draw (not a dedicated server) */
	bool IsRenderingMachine() const;

	bool bFirstPersonRendering = false;

	/** Subclasses: while drawn first person, the item's primitives cast no shadow (restored when it leaves the hand) */
	bool bNoShadowInFirstPerson = false;

private:

	/** Primitives whose shadow SetFirstPersonRendering turned off (bNoShadowInFirstPerson), to turn back on */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> FirstPersonShadowOff;

	/** Drop flight on this machine */
	double FlightStartTime = -1.0;
	FVector FlightFrom = FVector::ZeroVector;
	/** Where this machine drew the item when its holder let go (the owner's first-person hand), and in which frame */
	FVector LocalReleaseFrom = FVector::ZeroVector;
	uint64 LocalReleaseFrame = MAX_uint64;
	uint8 LastPlaceId = 0;
	bool bPlacementSeen = false;

	void BeginFlightIfNew();
	void UpdateFlight();
	double GetLocalTime() const;
	void NotifyHolders(const APawn* OldHolder) const;
};

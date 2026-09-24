// Lure: the physical cooler (T-030). Rules: docs/specs/catch-handling-rules.md "The cooler".

#pragma once

#include "CoreMinimal.h"
#include "Catch/LureCarryableItem.h"
#include "Progression/LureProgressionTypes.h"
#include "LureCoolerActor.generated.h"

class ALureFishItem;
class APlayerState;
class UBoxComponent;
class ULureCoolerComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 *  A cooler in the world: a DT_Cooler row (size, meshes, freshness speeds, carry speed) and its contents
 *  (ULureCoolerComponent, FLureCaughtFish records, replicated to everyone). Shared by every player (owner rules in the spec);
 *  it remembers the player it was made for (OwningPlayerState, CoolerGuid) for saves only.
 *  - Lid: open or closed (replicated); the contents spoil at the row's OpenDecayRate or ClosedDecayRate.
 *  - Carry: both hands (CarryCooler pose), the carrier moves at CarrySpeedMultiplier, no collision while carried.
 *  - Put down: in front of the carrier on dry, walkable ground where the box fits; forced put-downs (prone, water, caught,
 *    leaving) go to the carrier's last dry ground spot.
 *  - Look: the row's BodyMesh / LidMesh (lid on the body's LidHinge socket), else a placeholder box of the same size.
 *    The fish inside show while the lid is open, curled and stacked by DT_CoolerDisplay (cosmetic only).
 */
UCLASS(Blueprintable)
class ALureCoolerActor : public ALureCarryableItem
{
	GENERATED_BODY()

public:

	ALureCoolerActor();

	/**
	 *  Server: spawns a free cooler of CoolerId (None or unknown = the default row) standing at Transform (pivot = bottom
	 *  center), made for OwnerState (may be null). Guid: its save identity (invalid = a new one). Null on a client.
	 */
	static ALureCoolerActor* SpawnCooler(UWorld* World, FName CoolerId, const FTransform& Transform, APlayerState* OwnerState,
		const FGuid& Guid = FGuid(), bool bStarter = false);

	// ---- State (every machine) ----

	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	ULureCoolerComponent* GetStorage() const { return Storage; }

	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	FName GetCoolerId() const;

	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	bool IsLidOpen() const { return bLidOpen; }

	UFUNCTION(BlueprintPure, Category="Lure|Cooler")
	APlayerState* GetOwningPlayerState() const { return OwningPlayerState; }

	const FGuid& GetCoolerGuid() const { return CoolerGuid; }

	/** The automatic starter cooler (a loaded save with its own coolers replaces it) */
	bool IsStarter() const { return bStarter; }

	/** The DT_Cooler row in use (built-in values when the table or row is missing) */
	const FCoolerRow& GetRow() const;

	/** Spoiling speed inside right now (the row's open or closed rate) */
	float GetDecayRate() const;

	int32 GetNumFish() const;
	int32 GetCapacity() const;
	bool IsFull() const;

	/** "Starter cooler (3/4, closed)" */
	FString GetSummary() const;

	// ---- Server ----

	/** Opens or closes the lid (not while carried): the contents switch to the open or closed spoiling speed now */
	bool AuthoritySetLidOpen(bool bOpen);

	/** Pawn's held fish goes in (record only, spoiling at the cooler's speed; the item is removed). A closed lid stays closed. */
	bool AuthorityPutFishIn(APawn* Pawn, ALureFishItem* Fish);

	/** The top fish comes out into Pawn's empty hand (the lid must be open). Null if it can't. */
	ALureFishItem* AuthorityTakeFishOut(APawn* Pawn);

	/** Pawn picks it up in both hands (hands empty, no fish on the hook; the lid closes) */
	bool AuthorityPickUp(APawn* Pawn);

	/** The carrier puts it down in front of them (FindPutDownSpot). False (OutProblem says why) if there is no room: they keep it. */
	bool AuthorityPutDown(FText* OutProblem = nullptr);

	/** Puts it down on the floor under Spot without the room check (forced: prone, water, caught, leaving) */
	void AuthorityPutDownAt(const FVector& Spot, float Yaw);

	/** Save/load: the row, the lid, the contents (exposure as saved, re-anchored now), the place and the identity */
	void AuthorityRestore(const FLureCoolerSaveData& Data);

	void AuthoritySetOwningPlayerState(APlayerState* InOwner);

	/** For a save: identity, row, where it stands (a carried cooler: its carrier's last dry ground), lid, contents with the exposure now */
	FLureCoolerSaveData GetSaveData() const;

	/** Where a put-down in front of Carrier would go (floor, dry, walkable, room for the box). False = no room. */
	bool FindPutDownSpot(const APawn* Carrier, FTransform& OutTransform) const;

	// ---- Display (rendering machines) ----

	/** Fish shown inside right now (lid open, up to the display slots) */
	int32 GetNumDisplayedFish() const;

	/** The lid's current angle on this machine, degrees (0 = closed) */
	float GetLidPitch() const { return LidPitch; }

	/** Half size and center of the gameplay box (pivot space), cm */
	FVector GetBoxHalfExtent() const { return BoxHalfExtent; }
	FVector GetBoxCenter() const { return BoxCenter; }

	UBoxComponent* GetCollisionBox() const { return CollisionBox; }

	// ---- ALureCarryableItem ----

	virtual ELureHoldKind GetHoldKind() const override { return ELureHoldKind::TwoHands; }
	virtual bool GetHoldPose(EFPArmsPose& OutPose) const override;
	virtual float GetCarrySpeedMultiplier() const override;
	virtual FText GetItemName() const override;

	// ---- ILureInteractable ----

	virtual bool CanInteract(const APawn* Pawn) const override;
	virtual FLureInteraction GetInteraction(const APawn* Pawn, ELureInteractKey Key) const override;
	virtual bool PerformInteraction(APawn* Pawn, ELureInteractVerb Verb) override;
	virtual FVector GetInteractionLocation() const override;
	virtual float GetFocusRadius() const override { return 35.0f; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

protected:

	UPROPERTY(ReplicatedUsing=OnRep_Lid, BlueprintReadOnly, Category="Lure|Cooler")
	bool bLidOpen = false;

	/** Bumped when a fish goes into a closed cooler: the lid opens and shuts around it (cosmetic) */
	UPROPERTY(ReplicatedUsing=OnRep_Lid)
	uint8 LidPulseId = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Lure|Cooler")
	TObjectPtr<APlayerState> OwningPlayerState;

	UPROPERTY(Replicated)
	FGuid CoolerGuid;

	UPROPERTY(Replicated)
	bool bStarter = false;

	UFUNCTION()
	void OnRep_Lid();

	UFUNCTION()
	void OnStorageChanged(ULureCoolerComponent* Changed);

	/** Gameplay collision (blocks players when it stands; off while carried; never stops casts) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** The hinge: on the body's LidHinge socket (placeholder: the back top edge) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> LidPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> LidMesh;

	/** Where the shown fish lie: the body's Contents socket (placeholder: 5 cm above the floor of the box) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> ContentsRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<ULureCoolerComponent> Storage;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> DisplayFish;

	virtual bool GetFirstPersonAttachment(const APawn* Holder, USceneComponent*& OutParent, FName& OutSocket, FTransform& OutRelative) const override;
	virtual FTransform GetThirdPersonAttachment() const override;
	virtual void OnHoldChanged(const FLureItemHold& OldHold) override;
	virtual void UpdatePresentation(float DeltaSeconds) override;

private:

	FName LookCoolerId;
	bool bLookResolved = false;
	bool bPlaceholderLook = true;
	FCoolerRow CachedRow;
	FVector BoxHalfExtent = FVector(22.0f, 32.0f, 19.6f);
	FVector BoxCenter = FVector(0.0f, 0.0f, 19.6f);

	float LidPitch = 0.0f;
	float LidPulseTimeLeft = 0.0f;
	uint8 LastLidPulseId = 0;
	bool bDisplayVisible = false;
	TArray<FName> DisplaySpecies;

	void RefreshLook();
	void RefreshDisplay();
	void SetDisplayVisible(bool bVisible);
	void ApplyCollision();
	/** The floor under Above (solid level geometry, LureCast channel), within MaxFall */
	bool FindFloor(const FVector& Above, float MaxFall, FHitResult& OutHit, const AActor* IgnoreActor) const;
};

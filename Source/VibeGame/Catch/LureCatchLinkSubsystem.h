// Lure: the glue between T-030's fish items, T-029's landed fight fish and T-032's physics line. Rules:
// docs/specs/catch-handling-rules.md ("Landing": the T-029 and T-032 seams).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LureCatchLinkSubsystem.generated.h"

class AActor;
class ALureFightFish;
class ALureFishItem;
class ULureFishingComponent;
class ULureFishingLineComponent;
struct FFishInstance;

/**
 *  Per world, on every machine that draws (not a dedicated server); all of it is cosmetic:
 *  - T-029: when a fight ends Landed, the landed fight fish becomes the hanging fish item's look
 *    (ULureFightFishSubsystem::KeepLandedFish, then ULureCatchSubsystem::OfferLandedVisual). It is kept when an item adopts it
 *    at once, or on a network client (the item may replicate after the fight's end); else T-029 removes it as before.
 *  - T-032: a fish item that starts hanging hangs on its holder's physics line (AttachEndActor + SetExternalHangDriver);
 *    leaving the hook lets it go (DetachEndActor). Without a line on this machine the item keeps its own pendulum.
 *  - Same frame: the line moves the fish in its own update (TG_PostUpdateWork), but T-029 places the fight fish later
 *    (TG_LastDemotable). When the item adopts the landed fish (inside that later event, or when the item replicates), the
 *    item is put where the landed fish is now and the line is laid again to its mouth, so the line end is this frame's
 *    fish mouth, never last frame's bobber point.
 */
UCLASS()
class ULureCatchLinkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	static ULureCatchLinkSubsystem* Get(const UObject* WorldContext);

	/** The physics line Fish hangs on here (null = none: the fish's own pendulum) */
	ULureFishingLineComponent* GetLineFor(const ALureFishItem* Fish) const;

	// ---- The handlers (public for tests) ----

	/** T-029 OnFightFishLandedNative listener */
	void HandleFightFishLanded(ULureFishingComponent* Fishing, ALureFightFish* Fish, const FFishInstance& Landed);

	/** ALureFishItem::OnHookedChanged listener (this world's items only) */
	void HandleHookedChanged(ALureFishItem* Fish, bool bHooked);

	/** ALureFishItem::OnLandedVisualAdopted listener (this world's items only) */
	void HandleVisualAdopted(ALureFishItem* Fish, const FTransform& VisualWorld);

	// ---- UWorldSubsystem ----

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:

	FDelegateHandle LandedHandle;
	FDelegateHandle HookedHandle;
	FDelegateHandle AdoptedHandle;

	/** Hanging fish -> the line they hang on (to let go of the right line when the fish leaves the hook) */
	TMap<TWeakObjectPtr<ALureFishItem>, TWeakObjectPtr<ULureFishingLineComponent>> Hanging;

	bool IsDrawingWorld() const;

	/** Puts Fish at Where (its mouth where the landed fish's mouth is) and lays Line again from the rod tip to that mouth */
	void SeatOnLine(ALureFishItem& Fish, ULureFishingLineComponent& Line, const FTransform& Where);
};

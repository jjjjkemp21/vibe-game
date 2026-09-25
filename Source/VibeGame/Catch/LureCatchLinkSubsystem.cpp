// Lure: the glue between T-030's fish items, T-029's landed fight fish and T-032's physics line. See the header.

#include "Catch/LureCatchLinkSubsystem.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureFishItem.h"
#include "Engine/World.h"
#include "Fish/LureFightFish.h"
#include "Fish/LureFightFishSubsystem.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/SubsystemCollection.h"

ULureCatchLinkSubsystem* ULureCatchLinkSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<ULureCatchLinkSubsystem>() : nullptr;
}

bool ULureCatchLinkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULureCatchLinkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Not created on a dedicated server (nothing is drawn there): then only the line glue is bound, and it does nothing.
	if (ULureFightFishSubsystem* Visuals = Collection.InitializeDependency<ULureFightFishSubsystem>())
	{
		LandedHandle = Visuals->OnFightFishLandedNative.AddUObject(this, &ULureCatchLinkSubsystem::HandleFightFishLanded);
	}
	HookedHandle = ALureFishItem::OnHookedChanged.AddUObject(this, &ULureCatchLinkSubsystem::HandleHookedChanged);
	AdoptedHandle = ALureFishItem::OnLandedVisualAdopted.AddUObject(this, &ULureCatchLinkSubsystem::HandleVisualAdopted);
}

void ULureCatchLinkSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (ULureFightFishSubsystem* Visuals = World->GetSubsystem<ULureFightFishSubsystem>())
		{
			Visuals->OnFightFishLandedNative.Remove(LandedHandle);
		}
	}
	ALureFishItem::OnHookedChanged.Remove(HookedHandle);
	ALureFishItem::OnLandedVisualAdopted.Remove(AdoptedHandle);
	LandedHandle.Reset();
	HookedHandle.Reset();
	AdoptedHandle.Reset();
	Hanging.Reset();
	Super::Deinitialize();
}

bool ULureCatchLinkSubsystem::IsDrawingWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_DedicatedServer;
}

ULureFishingLineComponent* ULureCatchLinkSubsystem::GetLineFor(const ALureFishItem* Fish) const
{
	const TWeakObjectPtr<ULureFishingLineComponent>* Line = Fish ? Hanging.Find(Fish) : nullptr;
	return (Line && Line->IsValid() && Fish->GetExternalHangDriver() == Line->Get()) ? Line->Get() : nullptr;
}

// ---- T-029: the landed fight fish becomes the hanging fish's look ----

void ULureCatchLinkSubsystem::HandleFightFishLanded(ULureFishingComponent* Fishing, ALureFightFish* Fish, const FFishInstance& Landed)
{
	UWorld* World = GetWorld();
	ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(this);
	ULureFightFishSubsystem* Visuals = World ? World->GetSubsystem<ULureFightFishSubsystem>() : nullptr;
	if (!IsValid(Fish) || !Catch || !Visuals || !IsDrawingWorld())
	{
		return;
	}
	// The server spawned and hung the item earlier this frame (LandFish runs in TG_PostUpdateWork, this event in
	// TG_LastDemotable), so here it is adopted at once (HandleVisualAdopted seats the line). A client may get the item later.
	Catch->OfferLandedVisual(Fish, Landed);
	const ALureFishItem* Adopter = Cast<ALureFishItem>(Fish->GetAttachParentActor());
	if ((Adopter && Adopter->GetAdoptedVisual() == Fish) || World->GetNetMode() == NM_Client)
	{
		Visuals->KeepLandedFish(Fish);
	}
	else
	{
		Catch->ClaimLandedVisual(Landed); // no item here will take it (e.g. a pawn without hands): T-029 removes it as before
	}
}

// ---- T-032: the hanging fish hangs on the physics line ----

void ULureCatchLinkSubsystem::HandleHookedChanged(ALureFishItem* Fish, bool bHooked)
{
	if (!Fish || Fish->GetWorld() != GetWorld() || !IsDrawingWorld())
	{
		return;
	}
	if (!bHooked)
	{
		// Grabbed, let go or dropped: the line lets go of it (the item already cleared its driver).
		TWeakObjectPtr<ULureFishingLineComponent> Line;
		if (Hanging.RemoveAndCopyValue(Fish, Line) && Line.IsValid() && Line->GetEndActor() == Fish)
		{
			Line->DetachEndActor();
		}
		return;
	}
	const APawn* Holder = Fish->GetHolder();
	const ULureFishingComponent* Fishing = Holder ? Holder->FindComponentByClass<ULureFishingComponent>() : nullptr;
	ULureFishingLineComponent* Line = Fishing ? Fishing->GetLine() : nullptr;
	if (!Line)
	{
		return; // no line drawn for this angler here: the item's own pendulum and short line
	}
	for (auto It = Hanging.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent(); // a hanging fish that was removed without leaving the hook first
		}
	}
	Hanging.Add(Fish, Line);
	const FLureHangPendulum& Swing = Fish->GetPendulum();
	if (Swing.bInitialized)
	{
		// It adopted the landed fish before it was hung: start where that fish's mouth was (T-030's swing start).
		const FQuat Rotation = Fish->GetActorQuat();
		SeatOnLine(*Fish, *Line, FTransform(Rotation, Swing.Bob - Rotation.RotateVector(Fish->GetMouthOffset())));
	}
	else
	{
		// A line that is out lets go of its end there (the bobber); no line out: laid from the rod tip to the fish.
		Line->AttachEndActor(Fish, Fish->GetHangLineLength(), Fish->GetMouthOffset(), /*bOrientAlongLine*/ true, /*bFaceViewer*/ true); // T-043: side-on
	}
	Fish->SetExternalHangDriver(Line);
}

void ULureCatchLinkSubsystem::HandleVisualAdopted(ALureFishItem* Fish, const FTransform& VisualWorld)
{
	if (!Fish || Fish->GetWorld() != GetWorld() || !IsDrawingWorld())
	{
		return;
	}
	ULureFishingLineComponent* Line = GetLineFor(Fish);
	if (Line && Line->GetEndActor() == Fish)
	{
		// Read after T-029 moved the fish this frame: the hanging fish starts exactly where the landed fish is drawn.
		SeatOnLine(*Fish, *Line, FTransform(VisualWorld.GetRotation(), VisualWorld.GetLocation()));
	}
}

void ULureCatchLinkSubsystem::SeatOnLine(ALureFishItem& Fish, ULureFishingLineComponent& Line, const FTransform& Where)
{
	Fish.SetActorLocationAndRotation(Where.GetLocation(), Where.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	// Lay the line again, now: from the rod tip to the mouth (a longer line is reeled up to the hang length smoothly).
	// Hide first so letting go ends the line instead of pinning it back to the bobber; no line is out while a fish hangs
	// (casting is Busy), and if one were, its owner sets it again next frame.
	Line.Hide();
	Line.DetachEndActor();
	Line.AttachEndActor(&Fish, Fish.GetHangLineLength(), Fish.GetMouthOffset(), /*bOrientAlongLine*/ true, /*bFaceViewer*/ true); // T-043: side-on
}

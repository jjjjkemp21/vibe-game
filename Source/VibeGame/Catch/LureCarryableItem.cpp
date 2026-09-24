// Lure: a physical item a player can hold (T-030).

#include "Catch/LureCarryableItem.h"
#include "Camera/CameraComponent.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/LureInteractionSubsystem.h"
#include "Net/UnrealNetwork.h"

ALureCarryableItem::ALureCarryableItem()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the pawn, its camera and the first-person arms have moved: the hanging fish and the flight use this frame's rod tip.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = true;
	SetReplicatingMovement(false); // Hold and Placement replicate; every machine places the item itself
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(2.0f);

	ItemRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ItemRoot"));
	ItemRoot->SetMobility(EComponentMobility::Movable);
	ItemRoot->SetUsingAbsoluteScale(true); // world scale 1 whatever the bone it hangs on carries
	RootComponent = ItemRoot;

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(ItemRoot);
	VisualRoot->SetMobility(EComponentMobility::Movable);
}

void ALureCarryableItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALureCarryableItem, Hold);
	DOREPLIFETIME(ALureCarryableItem, Placement);
}

void ALureCarryableItem::GatherCurrentMovement()
{
	// Items replicate Hold and Placement themselves; the engine's movement and attachment replication would fight the
	// per-machine presentation (a local holder draws the item on their first-person arms, others on the body).
}

void ALureCarryableItem::OnRep_AttachmentReplication()
{
	RefreshPresentation();
}

FText ALureCarryableItem::GetItemName() const
{
	return FText::FromString(GetClass()->GetName());
}

bool ALureCarryableItem::IsHeldBy(const APawn* Pawn, ELureHoldMode Mode) const
{
	return Pawn && Hold.IsHeld() && Hold.Holder == Pawn && Hold.Mode == Mode;
}

bool ALureCarryableItem::IsHeldByLocalPlayer() const
{
	const APawn* Holder = Hold.IsHeld() ? Hold.Holder.Get() : nullptr;
	return Holder && Holder->IsLocallyControlled();
}

bool ALureCarryableItem::IsRenderingMachine() const
{
	return GetNetMode() != NM_DedicatedServer;
}

double ALureCarryableItem::GetLocalTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void ALureCarryableItem::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && Placement.PlaceId == 0 && !Hold.IsHeld())
	{
		// Spawned free: the spawn transform is where it rests.
		Placement.Location = GetActorLocation();
		Placement.Rotation = GetActorRotation();
		Placement.From = GetActorLocation();
		Placement.bAnimate = false;
		Placement.PlaceId = 1;
	}
	LastPlaceId = Placement.PlaceId;
	bPlacementSeen = true;

	if (ULureInteractionSubsystem* Interaction = ULureInteractionSubsystem::Get(this))
	{
		Interaction->Register(this);
	}
	if (ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(this))
	{
		Catch->RegisterItem(this);
	}
	RefreshPresentation();
	NotifyHolders(nullptr);
}

void ALureCarryableItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULureInteractionSubsystem* Interaction = ULureInteractionSubsystem::Get(this))
	{
		Interaction->Unregister(this);
	}
	if (ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(this))
	{
		Catch->UnregisterItem(this);
	}
	// The holder's hands forget this item (it is unregistered now, so a refresh no longer finds it).
	const APawn* Holder = Hold.Holder;
	Hold = FLureItemHold();
	NotifyHolders(Holder);
	Super::EndPlay(EndPlayReason);
}

void ALureCarryableItem::NotifyHolders(const APawn* OldHolder) const
{
	if (ULureHandsComponent* OldHands = ULureHandsComponent::Get(OldHolder))
	{
		OldHands->RefreshFromItems();
	}
	const APawn* NewHolder = Hold.Holder;
	if (NewHolder && NewHolder != OldHolder)
	{
		if (ULureHandsComponent* NewHands = ULureHandsComponent::Get(NewHolder))
		{
			NewHands->RefreshFromItems();
		}
	}
}

// ---- Server ----

void ALureCarryableItem::AuthoritySetHold(APawn* NewHolder, ELureHoldMode NewMode)
{
	if (!HasAuthority())
	{
		UE_LOG(LogLureCatch, Warning, TEXT("%s: AuthoritySetHold is server-only; ignored on this client."), *GetName());
		return;
	}
	if (!NewHolder || NewMode == ELureHoldMode::None)
	{
		NewHolder = nullptr;
		NewMode = ELureHoldMode::None;
	}
	const FLureItemHold Old = Hold;
	Hold.Holder = NewHolder;
	Hold.Mode = NewMode;
	ForceNetUpdate();
	RefreshPresentation();
	OnHoldChanged(Old);
	NotifyHolders(Old.Holder);
}

void ALureCarryableItem::AuthorityPlace(const FVector& Location, const FRotator& Rotation, const FVector& From, bool bAnimate)
{
	if (!HasAuthority())
	{
		UE_LOG(LogLureCatch, Warning, TEXT("%s: AuthorityPlace is server-only; ignored on this client."), *GetName());
		return;
	}
	// The placement first, so letting go below moves the item straight to its rest point.
	Placement.Location = Location;
	Placement.Rotation = Rotation;
	Placement.From = From;
	Placement.bAnimate = bAnimate;
	Placement.PlaceId = static_cast<uint8>(Placement.PlaceId == MAX_uint8 ? 1 : Placement.PlaceId + 1);
	ForceNetUpdate();
	if (Hold.IsHeld())
	{
		AuthoritySetHold(nullptr, ELureHoldMode::None);
	}
	else
	{
		RefreshPresentation();
	}
	BeginFlightIfNew();
}

// ---- Replication ----

void ALureCarryableItem::OnRep_Hold(const FLureItemHold& OldHold)
{
	if (OldHold.IsHeld() && !Hold.IsHeld() && VisualRoot)
	{
		// Let go: the drop flight (OnRep_Placement, same update) starts where this machine drew it, not the server's estimate.
		LocalReleaseFrom = VisualRoot->GetComponentLocation();
		LocalReleaseFrame = GFrameCounter;
	}
	RefreshPresentation();
	OnHoldChanged(OldHold);
	NotifyHolders(OldHold.Holder);
}

void ALureCarryableItem::OnRep_Placement()
{
	RefreshPresentation();
	BeginFlightIfNew();
}

// ---- Presentation ----

FVector ALureCarryableItem::GetInteractionLocation() const
{
	const APawn* Holder = Hold.IsHeld() ? Hold.Holder.Get() : nullptr;
	return Holder ? Holder->GetActorLocation() : GetActorLocation();
}

float ALureCarryableItem::GetInteractionRadius() const
{
	return ULureCatchSubsystem::GetTuningFor(this).ReachDistance;
}

void ALureCarryableItem::SetFirstPersonRendering(bool bFirstPerson)
{
	if (bFirstPersonRendering == bFirstPerson)
	{
		return;
	}
	bFirstPersonRendering = bFirstPerson;
	// The item's own primitives and those of actors attached to it (e.g. an adopted landed fish visual, T-029 seam).
	TArray<AActor*> Actors = { this };
	GetAttachedActors(Actors, /*bResetArray*/ false, /*bRecursivelyIncludeAttachedActors*/ true);
	for (AActor* Actor : Actors)
	{
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			Primitive->SetFirstPersonPrimitiveType(bFirstPerson ? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None);
		}
	}
}

void ALureCarryableItem::RefreshPresentation()
{
	if (!ItemRoot || !VisualRoot)
	{
		return;
	}
	APawn* Holder = Hold.IsHeld() ? Hold.Holder.Get() : nullptr;
	if (Holder && Holder->GetRootComponent() && Hold.Mode == ELureHoldMode::Hand)
	{
		FlightStartTime = -1.0;
		USceneComponent* Parent = nullptr;
		FName Socket = NAME_None;
		FTransform Relative = FTransform::Identity;
		const bool bFirstPerson = IsRenderingMachine() && Holder->IsLocallyControlled();
		if (bFirstPerson && !GetFirstPersonAttachment(Holder, Parent, Socket, Relative))
		{
			Parent = nullptr;
		}
		if (!Parent)
		{
			Parent = Holder->GetRootComponent();
			Socket = NAME_None;
			Relative = GetThirdPersonAttachment();
		}
		if (ItemRoot->GetAttachParent() != Parent || ItemRoot->GetAttachSocketName() != Socket)
		{
			ItemRoot->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		}
		ItemRoot->SetRelativeTransform(Relative);
		VisualRoot->SetRelativeTransform(FTransform::Identity);
		SetFirstPersonRendering(bFirstPerson && Parent != Holder->GetRootComponent());
		return;
	}
	if (Holder && Holder->GetRootComponent() && Hold.Mode == ELureHoldMode::Hook)
	{
		FlightStartTime = -1.0;
		// Follows its holder (the server's relevancy); rendering machines place it on the line every frame (UpdateHooked),
		// unless an external driver (T-032's physics line) holds it: then its attachment is that driver's.
		if (!IsHookedExternallyDriven() && ItemRoot->GetAttachParent() != Holder->GetRootComponent())
		{
			ItemRoot->AttachToComponent(Holder->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		}
		VisualRoot->SetRelativeTransform(FTransform::Identity);
		SetFirstPersonRendering(false);
		return;
	}

	// Free (or the holder isn't known on this machine yet): at the placement.
	if (ItemRoot->GetAttachParent())
	{
		ItemRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	SetFirstPersonRendering(false);
	if (Placement.PlaceId != 0)
	{
		SetActorLocationAndRotation(Placement.Location, Placement.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (FlightStartTime < 0.0)
	{
		VisualRoot->SetRelativeTransform(GetRestVisualTransform());
	}
}

void ALureCarryableItem::BeginFlightIfNew()
{
	if (Placement.PlaceId == LastPlaceId && bPlacementSeen)
	{
		return;
	}
	LastPlaceId = Placement.PlaceId;
	bPlacementSeen = true;
	const float ArcTime = ULureCatchSubsystem::GetTuningFor(this).DropArcTime;
	if (!Placement.bAnimate || !(ArcTime > 0.0f) || !IsRenderingMachine() || Hold.IsHeld())
	{
		FlightStartTime = -1.0;
		VisualRoot->SetRelativeTransform(GetRestVisualTransform());
		return;
	}
	FlightStartTime = GetLocalTime();
	FlightFrom = (LocalReleaseFrame == GFrameCounter) ? LocalReleaseFrom : FVector(Placement.From);
	UpdateFlight();
}

float ALureCarryableItem::GetFlightTimeLeft() const
{
	if (FlightStartTime < 0.0)
	{
		return 0.0f;
	}
	const float ArcTime = ULureCatchSubsystem::GetTuningFor(this).DropArcTime;
	return FMath::Max(0.0f, ArcTime - static_cast<float>(GetLocalTime() - FlightStartTime));
}

void ALureCarryableItem::UpdateFlight()
{
	if (FlightStartTime < 0.0)
	{
		return;
	}
	const float ArcTime = FMath::Max(0.01f, ULureCatchSubsystem::GetTuningFor(this).DropArcTime);
	const float Alpha = FMath::Clamp(static_cast<float>((GetLocalTime() - FlightStartTime) / ArcTime), 0.0f, 1.0f);
	const FTransform Rest = GetRestVisualTransform() * GetActorTransform();
	if (Alpha >= 1.0f)
	{
		FlightStartTime = -1.0;
		VisualRoot->SetRelativeTransform(GetRestVisualTransform());
		return;
	}
	// A short toss: a straight line plus a small arc (higher for longer drops), easing into the rest point.
	const FVector To = Rest.GetLocation();
	const double Distance = FVector::Dist(FlightFrom, To);
	const double ArcHeight = 10.0 + 0.2 * FMath::Min(Distance, 300.0);
	FVector Point = FMath::Lerp(FlightFrom, To, static_cast<double>(Alpha));
	Point.Z += 4.0 * ArcHeight * Alpha * (1.0f - Alpha);
	VisualRoot->SetWorldLocationAndRotation(Point, Rest.GetRotation());
}

FTransform ALureCarryableItem::GetVisualTransform() const
{
	return VisualRoot ? VisualRoot->GetComponentTransform() : GetActorTransform();
}

void ALureCarryableItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsRenderingMachine())
	{
		return;
	}
	if (Hold.IsHeld() && Hold.Mode == ELureHoldMode::Hook && Hold.Holder)
	{
		UpdateHooked(DeltaSeconds);
	}
	else if (!Hold.IsHeld())
	{
		UpdateFlight();
	}
	UpdatePresentation(DeltaSeconds);
}

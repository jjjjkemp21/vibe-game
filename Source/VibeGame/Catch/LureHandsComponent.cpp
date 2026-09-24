// Lure: what a player holds (T-030).

#include "Catch/LureHandsComponent.h"
#include "Catch/LureCarryableItem.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"

namespace LureHandsPrivate
{
	/** Seconds between two samples of the last dry ground spot (server) */
	constexpr double DryGroundSampleInterval = 0.2;
}

ULureHandsComponent::ULureHandsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true); // the owner notice RPC; the state itself lives on the items
}

ULureHandsComponent* ULureHandsComponent::Get(const AActor* Pawn)
{
	return Pawn ? Pawn->FindComponentByClass<ULureHandsComponent>() : nullptr;
}

APawn* ULureHandsComponent::GetPawn() const
{
	return Cast<APawn>(GetOwner());
}

bool ULureHandsComponent::CheckServer(const TCHAR* What) const
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		return true;
	}
	UE_LOG(LogLureCatch, Warning, TEXT("%s: %s is server-only; ignored on this client."), *GetPathNameSafe(this), What);
	return false;
}

void ULureHandsComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshFromItems(); // items that replicated before this pawn did
	const AActor* Owner = GetOwner();
	SetComponentTickEnabled(Owner && Owner->HasAuthority()); // dry ground and forced drops are server rules
}

void ULureHandsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The pawn leaves the world (destroyed, a respawn, the player left): nothing it held is lost.
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld))
	{
		AuthorityDropEverything(/*bLoseFish*/ false);
	}
	Super::EndPlay(EndPlayReason);
}

// ---- State ----

ALureCarryableItem* ULureHandsComponent::GetHeldItem() const
{
	ALureCarryableItem* Item = HeldItem.Get();
	return (IsValid(Item) && Item->IsHeldBy(GetPawn(), ELureHoldMode::Hand)) ? Item : nullptr;
}

ALureFishItem* ULureHandsComponent::GetHeldFish() const
{
	return Cast<ALureFishItem>(GetHeldItem());
}

ALureCoolerActor* ULureHandsComponent::GetCarriedCooler() const
{
	return Cast<ALureCoolerActor>(GetHeldItem());
}

ALureFishItem* ULureHandsComponent::GetHangingFish() const
{
	ALureFishItem* Fish = HangingFish.Get();
	return (IsValid(Fish) && Fish->IsHeldBy(GetPawn(), ELureHoldMode::Hook)) ? Fish : nullptr;
}

bool ULureHandsComponent::GetArmsPoseOverride(EFPArmsPose& OutPose) const
{
	const ALureCarryableItem* Item = GetHeldItem();
	return Item && Item->GetHoldPose(OutPose);
}

float ULureHandsComponent::GetMoveSpeedMultiplier() const
{
	const ALureCarryableItem* Item = GetHeldItem();
	if (!Item)
	{
		return 1.0f;
	}
	const float Multiplier = Item->GetCarrySpeedMultiplier();
	return (FMath::IsFinite(Multiplier) && Multiplier > 0.0f) ? FMath::Clamp(Multiplier, 0.05f, 2.0f) : 1.0f;
}

FVector ULureHandsComponent::GetHangPivot() const
{
	const APawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return FVector::ZeroVector;
	}
	if (const ULureFishingComponent* Fishing = Pawn->FindComponentByClass<ULureFishingComponent>())
	{
		return Fishing->GetLineStart(); // the rod tip where it is drawn (owner), an estimate from the eye (others)
	}
	return Pawn->GetPawnViewLocation() + FRotator(0.0f, Pawn->GetViewRotation().Yaw, 0.0f).RotateVector(GetDefault<ULureFishingSettings>()->RodTipOffsetFromEye);
}

bool ULureHandsComponent::IsRodStowedFor(const AActor* Pawn)
{
	const ULureHandsComponent* Hands = Get(Pawn);
	return Hands && Hands->IsRodStowed();
}

bool ULureHandsComponent::HasFishOnHookFor(const AActor* Pawn)
{
	const ULureHandsComponent* Hands = Get(Pawn);
	return Hands && Hands->GetHangingFish() != nullptr;
}

bool ULureHandsComponent::CanHoldItems() const
{
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner());
	return !Lure || !Lure->IsSwimming();
}

bool ULureHandsComponent::IsPawnProne() const
{
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner());
	return Lure && Lure->IsProne();
}

FString ULureHandsComponent::GetHeldText() const
{
	if (const ALureFishItem* Fish = GetHeldFish())
	{
		return FString::Printf(TEXT("Holding: %s"), *Fish->GetDescription());
	}
	if (const ALureCoolerActor* Cooler = GetCarriedCooler())
	{
		return FString::Printf(TEXT("Carrying: %s"), *Cooler->GetSummary());
	}
	if (const ALureCarryableItem* Item = GetHeldItem())
	{
		return FString::Printf(TEXT("Holding: %s"), *Item->GetItemName().ToString());
	}
	if (const ALureFishItem* Hanging = GetHangingFish())
	{
		return FString::Printf(TEXT("On the hook: %s"), *Hanging->GetDescription());
	}
	return FString();
}

void ULureHandsComponent::RefreshFromItems()
{
	HeldItem.Reset();
	HangingFish.Reset();
	const APawn* Pawn = GetPawn();
	const ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(this);
	if (!Pawn || !Catch)
	{
		return;
	}
	for (ALureCarryableItem* Item : Catch->GetItems())
	{
		if (!Item->GetHold().IsHeld() || Item->GetHolder() != Pawn)
		{
			continue;
		}
		if (Item->GetHold().Mode == ELureHoldMode::Hand && !HeldItem.IsValid())
		{
			HeldItem = Item;
		}
		else if (Item->GetHold().Mode == ELureHoldMode::Hook && !HangingFish.IsValid())
		{
			HangingFish = Cast<ALureFishItem>(Item);
		}
	}
}

// ---- Server ----

bool ULureHandsComponent::AuthorityTakeInHand(ALureCarryableItem* Item)
{
	APawn* Pawn = GetPawn();
	if (!CheckServer(TEXT("AuthorityTakeInHand")) || !Pawn || !IsValid(Item))
	{
		return false;
	}
	if (GetHeldItem() || !CanHoldItems())
	{
		return false; // one item at a time; not while swimming
	}
	const bool bOwnHanging = Item->IsHeldBy(Pawn, ELureHoldMode::Hook);
	if (!Item->IsFree() && !bOwnHanging)
	{
		return false; // someone else holds it (or it hangs on someone else's hook)
	}
	if (GetHangingFish() && !bOwnHanging)
	{
		return false; // take the fish off your hook first
	}
	if (ALureFishItem* Fish = Cast<ALureFishItem>(Item))
	{
		Fish->AuthoritySetCounter(nullptr);
	}
	Item->AuthoritySetHold(Pawn, ELureHoldMode::Hand); // refreshes this cache through NotifyHolders
	RefreshFromItems();
	UE_LOG(LogLureCatch, Log, TEXT("%s took %s in hand."), *GetNameSafe(Pawn), *Item->GetItemName().ToString());
	return true;
}

bool ULureHandsComponent::AuthorityHangFish(ALureFishItem* Fish)
{
	APawn* Pawn = GetPawn();
	if (!CheckServer(TEXT("AuthorityHangFish")) || !Pawn || !IsValid(Fish) || !Fish->IsFree())
	{
		return false;
	}
	if (ALureFishItem* Old = GetHangingFish())
	{
		// Only reachable through debug tools (casting is refused while a fish hangs): the older fish drops off below the hook.
		const FVector Below = GetHangPivot() - FVector::UpVector * ULureCatchSubsystem::GetTuningFor(this).HangLineLength;
		Old->AuthorityDrop(Pawn, Below, FVector2D(Below.X, Below.Y), FVector2D(Pawn->GetActorForwardVector()), 0.0f);
	}
	Fish->AuthoritySetHold(Pawn, ELureHoldMode::Hook);
	RefreshFromItems();
	return true;
}

ALureCarryableItem* ULureHandsComponent::AuthorityReleaseHeld()
{
	ALureCarryableItem* Item = GetHeldItem();
	if (!CheckServer(TEXT("AuthorityReleaseHeld")) || !Item)
	{
		return nullptr;
	}
	Item->AuthoritySetHold(nullptr, ELureHoldMode::None);
	RefreshFromItems();
	return Item;
}

ALureFishItem* ULureHandsComponent::AuthorityReleaseHanging()
{
	ALureFishItem* Fish = GetHangingFish();
	if (!CheckServer(TEXT("AuthorityReleaseHanging")) || !Fish)
	{
		return nullptr;
	}
	Fish->AuthoritySetHold(nullptr, ELureHoldMode::None);
	RefreshFromItems();
	return Fish;
}

FVector ULureHandsComponent::GetForcedDropSpot() const
{
	FVector Spot;
	if (GetLastDryGround(Spot))
	{
		return Spot;
	}
	const APawn* Pawn = GetPawn();
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			return Character->GetActorLocation() - FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	return Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
}

void ULureHandsComponent::PutFishAt(ALureFishItem* Fish, const FVector& Location) const
{
	if (!IsValid(Fish))
	{
		return;
	}
	const APawn* Pawn = GetPawn();
	const float Yaw = Pawn ? static_cast<float>(Pawn->GetActorRotation().Yaw) + 90.0f : 0.0f;
	Fish->AuthorityPlace(Location, FRotator(0.0f, Yaw, 0.0f), Fish->GetVisualTransform().GetLocation(), /*bAnimate*/ true);
	Fish->AuthoritySetCounter(nullptr);
}

int32 ULureHandsComponent::AuthorityDropEverything(bool bLoseFish)
{
	APawn* Pawn = GetPawn();
	if (!CheckServer(TEXT("AuthorityDropEverything")) || !Pawn)
	{
		return 0;
	}
	int32 Lost = 0;
	const FVector Spot = GetForcedDropSpot();
	for (ALureFishItem* Fish : { GetHangingFish(), GetHeldFish() })
	{
		if (!Fish)
		{
			continue;
		}
		if (bLoseFish)
		{
			UE_LOG(LogLureCatch, Log, TEXT("%s lost %s."), *GetNameSafe(Pawn), *Fish->GetFish().SpeciesId.ToString());
			Fish->AuthoritySetHold(nullptr, ELureHoldMode::None);
			Fish->Destroy();
			++Lost;
		}
		else
		{
			PutFishAt(Fish, Spot);
		}
	}
	if (ALureCoolerActor* Cooler = GetCarriedCooler())
	{
		Cooler->AuthorityPutDownAt(Spot, ALureCoolerActor::GetYawFacing(Spot, Pawn)); // its front toward you
	}
	else if (ALureCarryableItem* Other = GetHeldItem())
	{
		Other->AuthorityPlace(Spot, FRotator(0.0f, Pawn->GetActorRotation().Yaw, 0.0f), Other->GetVisualTransform().GetLocation(), true);
	}
	RefreshFromItems();
	return Lost;
}

bool ULureHandsComponent::GetLastDryGround(FVector& OutLocation) const
{
	OutLocation = LastDryGround;
	return bHasDryGround;
}

void ULureHandsComponent::SetLastDryGround(const FVector& Location)
{
	LastDryGround = Location;
	bHasDryGround = true;
}

void ULureHandsComponent::UpdateDryGround()
{
	const UWorld* World = GetWorld();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!World || !Movement || !Movement->IsMovingOnGround())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now < NextDryGroundSample)
	{
		return;
	}
	NextDryGroundSample = Now + LureHandsPrivate::DryGroundSampleInterval;
	if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(Character); Lure && Lure->IsSwimming())
	{
		return;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Feet = Character->GetActorLocation() - FVector::UpVector * (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f);
	// Wading counts as wet: water volumes (T-026), and the fallback sea level the fishing rules use.
	if (const ULureCharacterMovementComponent* LureMovement = Cast<ULureCharacterMovementComponent>(Movement); LureMovement && LureMovement->IsPointInWater(Feet + FVector::UpVector * 2.0f))
	{
		return;
	}
	const ULureFishingSettings* Fishing = GetDefault<ULureFishingSettings>();
	if (Fishing->bUseFallbackWaterZ && Feet.Z <= Fishing->FallbackWaterZ + Fishing->LandTolerance)
	{
		return;
	}
	SetLastDryGround(Feet);
}

void ULureHandsComponent::ApplyForcedDrops()
{
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner());
	if (!Lure)
	{
		return;
	}
	if (Lure->IsSwimming() && (GetHeldItem() || GetHangingFish()))
	{
		UE_LOG(LogLureCatch, Log, TEXT("%s fell into the water: what was in hand goes to the last dry ground spot."), *GetNameSafe(Lure));
		AuthorityDropEverything(/*bLoseFish*/ false);
		return;
	}
	if (Lure->IsProne())
	{
		if (ALureCoolerActor* Cooler = GetCarriedCooler())
		{
			// Hide first, come back for it: a cooler can't be carried crawling.
			if (!Cooler->AuthorityPutDown())
			{
				const FVector Spot = GetForcedDropSpot();
				Cooler->AuthorityPutDownAt(Spot, ALureCoolerActor::GetYawFacing(Spot, Lure)); // its front toward you
			}
			RefreshFromItems();
		}
	}
}

void ULureHandsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	UpdateDryGround();
	ApplyForcedDrops();
}

void ULureHandsComponent::ClientNotice_Implementation(const FString& Text)
{
	if (ULureProgressionComponent* Progression = ULureProgressionLibrary::GetProgression(GetOwner()))
	{
		if (Progression->IsLocalPlayerProgression())
		{
			Progression->AddNotice(Text);
		}
	}
}

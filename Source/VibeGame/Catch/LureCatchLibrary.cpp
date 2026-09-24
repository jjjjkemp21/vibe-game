// Lure: catch handling entry points for other systems (T-030).

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/FishingSpots.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionSettings.h"

namespace LureCatchLibraryPrivate
{
	APawn* ResolvePawn(AActor* Context)
	{
		if (APawn* Pawn = Cast<APawn>(Context))
		{
			return Pawn;
		}
		if (const AController* Controller = Cast<AController>(Context))
		{
			return Controller->GetPawn();
		}
		return nullptr;
	}
}

FLureFishLandedResult ULureCatchLibrary::HandleFishLanded(AActor* Context, const FFishInstance& Fish)
{
	FLureFishLandedResult Result;
	if (!Context || !Context->HasAuthority())
	{
		UE_LOG(LogLureCatch, Warning, TEXT("HandleFishLanded is server-only (context %s); ignored."), *GetNameSafe(Context));
		return Result;
	}
	if (!Fish.IsValid())
	{
		UE_LOG(LogLureCatch, Warning, TEXT("HandleFishLanded: %s got an invalid fish (no species); ignored."), *GetNameSafe(Context));
		return Result;
	}

	// XP now (T-010 rule), and the progression's OnFishLanded (the journal, T-011).
	if (ULureProgressionComponent* Progression = ULureProgressionLibrary::GetProgression(Context))
	{
		Result = Progression->HandleFishLanded(Fish);
	}
	else
	{
		UE_LOG(LogLureCatch, Log, TEXT("%s has no player progression: no XP for the landed fish."), *GetNameSafe(Context));
	}

	// The fish on the hook (T-030): it hangs at the line's end until grabbed or let go. Freshness time zero is now.
	APawn* Pawn = LureCatchLibraryPrivate::ResolvePawn(Context);
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	UWorld* World = Context->GetWorld();
	if (Hands && World)
	{
		const FVector Below = Hands->GetHangPivot() - FVector::UpVector * ULureCatchSubsystem::GetTuningFor(World).HangLineLength;
		ALureFishItem* Item = ALureFishItem::SpawnFish(World, FLureCaughtFish::Landed(Fish, FLureFreshness::GetServerTime(World)), FTransform(Below));
		if (Item && Hands->AuthorityHangFish(Item))
		{
			Result.bOnHook = true;
			Result.FishItem = Item;
		}
		else if (Item)
		{
			Item->Destroy();
		}
	}
	else
	{
		UE_LOG(LogLureCatch, Log, TEXT("%s has no hands: the landed fish is not kept."), *GetNameSafe(Context));
	}
	Result.bAccepted = Result.bAccepted || Result.bOnHook;
	return Result;
}

int32 ULureCatchLibrary::HandlePlayerCaught(AActor* Context)
{
	APawn* Pawn = LureCatchLibraryPrivate::ResolvePawn(Context);
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!Hands || !Pawn->HasAuthority())
	{
		return 0;
	}
	const int32 Lost = Hands->AuthorityDropEverything(/*bLoseFish*/ true);
	UE_LOG(LogLureCatch, Log, TEXT("%s was caught: %d fish lost (coolers stay where they are)."), *GetNameSafe(Pawn), Lost);
	return Lost;
}

TArray<ALureCoolerActor*> ULureCatchLibrary::GetOwnedCoolers(const APlayerState* PlayerState)
{
	const ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(PlayerState);
	return Subsystem ? Subsystem->GetCoolersOwnedBy(PlayerState) : TArray<ALureCoolerActor*>();
}

ALureCoolerActor* ULureCatchLibrary::EnsureStarterCooler(APlayerState* PlayerState, const AActor* StartSpot)
{
	UWorld* World = PlayerState ? PlayerState->GetWorld() : nullptr;
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	if (!World || !PlayerState->HasAuthority() || !Settings->bSpawnStarterCooler || GetOwnedCoolers(PlayerState).Num() > 0)
	{
		return nullptr;
	}

	// Where: a tagged spot in the level (players' coolers in a row), else next to the player's start.
	TArray<const AActor*> Spots;
	if (!Settings->CoolerSpawnTag.IsNone())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsValid(*It) && It->ActorHasTag(Settings->CoolerSpawnTag))
			{
				Spots.Add(*It);
			}
		}
	}
	FTransform Spot;
	if (Spots.Num() > 0)
	{
		int32 Starters = 0;
		if (const ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
		{
			for (ALureCarryableItem* Item : Subsystem->GetItems())
			{
				const ALureCoolerActor* Cooler = Cast<ALureCoolerActor>(Item);
				Starters += (Cooler && Cooler->IsStarter()) ? 1 : 0;
			}
		}
		const AActor* Marker = Spots[Starters % Spots.Num()];
		const float Along = Settings->StarterCoolerSpacing * static_cast<float>(Starters / Spots.Num());
		Spot = FTransform(FRotator(0.0f, Marker->GetActorRotation().Yaw, 0.0f), Marker->GetActorTransform().TransformPositionNoScale(FVector(0.0f, Along, 0.0f)));
	}
	else if (StartSpot)
	{
		const FVector Location = StartSpot->GetActorTransform().TransformPositionNoScale(Settings->StarterCoolerOffset);
		// Its front (+X, the latch) toward the player's start (the offset is to the side too, so not just the start's yaw + 180).
		Spot = FTransform(FRotator(0.0f, ALureCoolerActor::GetYawFacing(Location, StartSpot), 0.0f), Location);
	}
	else
	{
		return nullptr;
	}

	// On the floor below (solid level geometry).
	FHitResult Floor;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureStarterCooler), false);
	if (FLureFishingSpots::TraceCast(World, Floor, Spot.GetLocation() + FVector::UpVector * 100.0f, Spot.GetLocation() - FVector::UpVector * 600.0f, Params))
	{
		Spot.SetLocation(Floor.ImpactPoint);
	}
	ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(World, GetDefault<ULureProgressionSettings>()->DefaultCoolerId, Spot, PlayerState, FGuid(), /*bStarter*/ true);
	UE_LOG(LogLureCatch, Log, TEXT("Starter cooler %s for %s at %s."), *GetNameSafe(Cooler), *GetNameSafe(PlayerState), *Spot.GetLocation().ToCompactString());
	return Cooler;
}

// ---- Save / load ----

TArray<FLureCoolerSaveData> ULureCatchLibrary::GetCoolerSaveData(const APlayerState* PlayerState)
{
	TArray<FLureCoolerSaveData> Data;
	for (const ALureCoolerActor* Cooler : GetOwnedCoolers(PlayerState))
	{
		Data.Add(Cooler->GetSaveData());
	}
	return Data;
}

int32 ULureCatchLibrary::ApplyCoolerSaveData(APlayerState* PlayerState, const TArray<FLureCoolerSaveData>& Coolers)
{
	UWorld* World = PlayerState ? PlayerState->GetWorld() : nullptr;
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World);
	if (!World || !Subsystem || !PlayerState->HasAuthority())
	{
		return 0;
	}
	TSet<FGuid> Saved;
	int32 Applied = 0;
	for (const FLureCoolerSaveData& Data : Coolers)
	{
		ALureCoolerActor* Existing = nullptr;
		if (Data.CoolerGuid.IsValid())
		{
			for (ALureCarryableItem* Item : Subsystem->GetItems())
			{
				ALureCoolerActor* Cooler = Cast<ALureCoolerActor>(Item);
				if (Cooler && Cooler->GetCoolerGuid() == Data.CoolerGuid)
				{
					Existing = Cooler;
					break;
				}
			}
		}
		ALureCoolerActor* Target = Existing ? Existing
			: ALureCoolerActor::SpawnCooler(World, Data.CoolerId, FTransform(Data.Rotation, Data.Location), PlayerState, Data.CoolerGuid, Data.bStarter);
		if (!Target)
		{
			continue;
		}
		if (Existing && !Existing->IsFree())
		{
			// Someone carries it (a reconnect while a friend holds it): leave it with them, only the owner is updated.
			Existing->AuthoritySetOwningPlayerState(PlayerState);
		}
		else
		{
			Target->AuthorityRestore(Data);
			Target->AuthoritySetOwningPlayerState(PlayerState);
		}
		Saved.Add(Target->GetCoolerGuid());
		++Applied;
	}
	if (Coolers.Num() > 0)
	{
		// The automatic starter cooler handed out before the save arrived: gone if empty (never deletes a fish).
		for (ALureCoolerActor* Cooler : GetOwnedCoolers(PlayerState))
		{
			if (!Saved.Contains(Cooler->GetCoolerGuid()) && Cooler->IsStarter() && Cooler->IsFree() && Cooler->GetNumFish() == 0)
			{
				Cooler->Destroy();
			}
		}
	}
	return Applied;
}

FLurePlayerSaveData ULureCatchLibrary::GetPlayerSaveData(const APlayerState* PlayerState)
{
	FLurePlayerSaveData Data;
	if (const ULureProgressionComponent* Progression = ULureProgressionLibrary::GetProgression(PlayerState))
	{
		Data.Progress = Progression->GetSaveData();
	}
	Data.Coolers = GetCoolerSaveData(PlayerState);
	return Data;
}

bool ULureCatchLibrary::ApplyPlayerSaveData(APlayerState* PlayerState, const FLurePlayerSaveData& Data)
{
	ULureProgressionComponent* Progression = ULureProgressionLibrary::GetProgression(PlayerState);
	if (!Progression || !Progression->ApplySaveData(Data.Progress))
	{
		return false;
	}
	ApplyCoolerSaveData(PlayerState, Data.Coolers);
	return true;
}

// ---- HUD ----

TArray<FString> ULureCatchLibrary::GetPlaceholderLines(const APlayerController* PlayerController)
{
	TArray<FString> Lines;
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn))
	{
		const FString Held = Hands->GetHeldText();
		if (!Held.IsEmpty())
		{
			Lines.Add(Held);
		}
	}
	return Lines;
}

FString ULureCatchLibrary::GetOwnCoolerStatus(const APlayerState* PlayerState)
{
	const TArray<ALureCoolerActor*> Coolers = GetOwnedCoolers(PlayerState);
	if (Coolers.Num() == 0)
	{
		return FString();
	}
	return FString::Printf(TEXT("Cooler %d/%d"), Coolers[0]->GetNumFish(), Coolers[0]->GetCapacity());
}

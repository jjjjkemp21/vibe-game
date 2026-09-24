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
#include "GameFramework/WorldSettings.h"
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

	/** A real start spot: not null and not the world settings actor the engine falls back to on a map with no PlayerStart */
	bool IsPlayerStartSpot(const AActor* StartSpot)
	{
		return IsValid(StartSpot) && !StartSpot->IsA<AWorldSettings>();
	}

	/** The first player's pawn (the first player controller with one), else PlayerState's own pawn */
	APawn* FirstPlayerPawn(UWorld* World, const APlayerState* PlayerState)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			if (Controller && IsValid(Controller->GetPawn()))
			{
				return Controller->GetPawn();
			}
		}
		return PlayerState ? PlayerState->GetPawn() : nullptr;
	}

	/**
	 *  The starter-cooler row (one rule for the Lure.CoolerSpawn marker and the player-start fallback): slot i stands
	 *  StarterCoolerSpacing * i along Frame's +Y from Base (in Frame). From FirstIndex on, the first slot with no cooler
	 *  standing within half a spacing of it (2D) is used, so two players' starter coolers never stand inside each other.
	 */
	FVector StarterRowSlot(const UWorld* World, const FTransform& Frame, const FVector& Base, int32 FirstIndex)
	{
		const float Spacing = GetDefault<ULureCatchSettings>()->StarterCoolerSpacing;
		auto SlotAt = [&](int32 Index) { return Frame.TransformPositionNoScale(Base + FVector(0.0f, Spacing * static_cast<float>(Index), 0.0f)); };
		if (Spacing <= KINDA_SMALL_NUMBER)
		{
			return SlotAt(FirstIndex);
		}
		TArray<FVector> Taken;
		if (const ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
		{
			for (const ALureCarryableItem* Item : Subsystem->GetItems())
			{
				if (IsValid(Item) && !Item->IsActorBeingDestroyed() && Item->IsA<ALureCoolerActor>())
				{
					Taken.Add(Item->GetActorLocation());
				}
			}
		}
		const double Clear = FMath::Square(0.5 * static_cast<double>(Spacing));
		// Each cooler blocks at most two slots, so a free one is always found within this many.
		const int32 Last = FirstIndex + 2 * Taken.Num();
		for (int32 Index = FirstIndex; Index < Last; ++Index)
		{
			const FVector Slot = SlotAt(Index);
			if (!Taken.ContainsByPredicate([&Slot, Clear](const FVector& Other) { return FVector::DistSquared2D(Slot, Other) < Clear; }))
			{
				return Slot;
			}
		}
		return SlotAt(Last);
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

	// Where: a tagged spot in the level (players' coolers in a row), else next to the player's start, else next to the first
	// player's pawn (T-030i).
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
		const FVector Location = LureCatchLibraryPrivate::StarterRowSlot(World, Marker->GetActorTransform(), FVector::ZeroVector, Starters / Spots.Num());
		Spot = FTransform(FRotator(0.0f, Marker->GetActorRotation().Yaw, 0.0f), Location);
	}
	else if (LureCatchLibraryPrivate::IsPlayerStartSpot(StartSpot))
	{
		// Players sharing one start (a map with one PlayerStart): the same row rule, starting at StarterCoolerOffset.
		const FVector Location = LureCatchLibraryPrivate::StarterRowSlot(World, StartSpot->GetActorTransform(), Settings->StarterCoolerOffset, 0);
		// Its front (+X, the latch) toward the player's start (the offset is to the side too, so not just the start's yaw + 180).
		Spot = FTransform(FRotator(0.0f, ALureCoolerActor::GetYawFacing(Location, StartSpot), 0.0f), Location);
	}
	else if (const APawn* Anchor = LureCatchLibraryPrivate::FirstPlayerPawn(World, PlayerState))
	{
		// T-030i: no marker and no player start (a map without a PlayerStart: the engine passes its world settings, at the
		// origin): the same row next to the first player's pawn, in its yaw frame, front toward that pawn.
		const FTransform Frame(FRotator(0.0f, Anchor->GetActorRotation().Yaw, 0.0f), Anchor->GetActorLocation());
		const FVector Location = LureCatchLibraryPrivate::StarterRowSlot(World, Frame, Settings->StarterCoolerOffset, 0);
		Spot = FTransform(FRotator(0.0f, ALureCoolerActor::GetYawFacing(Location, Anchor), 0.0f), Location);
		UE_LOG(LogLureCatch, Warning, TEXT("No %s marker and no player start: %s's starter cooler goes next to %s (add a PlayerStart or a %s marker to the level)."),
			*Settings->CoolerSpawnTag.ToString(), *GetNameSafe(PlayerState), *GetNameSafe(Anchor), *Settings->CoolerSpawnTag.ToString());
	}
	else
	{
		UE_LOG(LogLureCatch, Warning, TEXT("No %s marker, no player start and no player pawn: no starter cooler for %s."),
			*Settings->CoolerSpawnTag.ToString(), *GetNameSafe(PlayerState));
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
		if (Existing)
		{
			// Already in the world (a mid-session reconnect or a re-apply): the live cooler wins. Its saved fish are NOT
			// restored: fish taken out since the save still exist (hand, counter, ground), so restoring would copy them.
			// Only the owner is updated (a reconnect brings a new player state).
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

FString ULureCatchLibrary::GetCoolerStatus(const APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return FString();
	}
	const ULureHandsComponent* Hands = ULureHandsComponent::Get(PlayerController->GetPawn());
	if (const ALureCoolerActor* Carried = Hands ? Hands->GetCarriedCooler() : nullptr)
	{
		// One count on screen: the cooler in your hands, whoever it belongs to.
		return FString::Printf(TEXT("Cooler %d/%d"), Carried->GetNumFish(), Carried->GetCapacity());
	}
	return GetOwnCoolerStatus(PlayerController->PlayerState);
}

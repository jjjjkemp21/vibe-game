// Lure: progression entry points for other systems (T-010).

#include "Progression/LureProgressionLibrary.h"
#include "Catch/LureCatchLibrary.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Interaction/LureInteractionComponent.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

APlayerState* ULureProgressionLibrary::FindPlayerState(const AActor* Context)
{
	const AActor* Current = Context;
	for (int32 Depth = 0; Current && Depth < 8; ++Depth)
	{
		if (const APlayerState* PlayerState = Cast<APlayerState>(Current))
		{
			return const_cast<APlayerState*>(PlayerState);
		}
		if (const APawn* Pawn = Cast<APawn>(Current))
		{
			if (APlayerState* PlayerState = Pawn->GetPlayerState())
			{
				return PlayerState;
			}
		}
		if (const AController* Controller = Cast<AController>(Current))
		{
			if (Controller->PlayerState)
			{
				return Controller->PlayerState;
			}
		}
		Current = Current->GetOwner();
	}
	return nullptr;
}

ULureProgressionComponent* ULureProgressionLibrary::GetProgression(const AActor* Context)
{
	const APlayerState* PlayerState = FindPlayerState(Context);
	return PlayerState ? PlayerState->FindComponentByClass<ULureProgressionComponent>() : nullptr;
}

FLureFishLandedResult ULureProgressionLibrary::HandleFishLanded(AActor* Context, const FFishInstance& Fish)
{
	if (ULureProgressionComponent* Progression = GetProgression(Context))
	{
		return Progression->HandleFishLanded(Fish);
	}
	UE_LOG(LogLureProgression, Warning, TEXT("HandleFishLanded: %s has no player state with a ULureProgressionComponent (is the game mode ALureGameMode?)."), *GetNameSafe(Context));
	return FLureFishLandedResult();
}

float ULureProgressionLibrary::GetFishDifficultyMultiplier(const AActor* Context, int32 FishLevel)
{
	if (const ULureProgressionComponent* Progression = GetProgression(Context))
	{
		return Progression->GetFishDifficultyMultiplier(FishLevel);
	}
	return FFishRoll::LevelDifficultyMultiplier(FishLevel, /*PlayerLevel*/ 1, GetDefault<UFishSettings>()->LevelScaling);
}

TArray<FString> ULureProgressionLibrary::GetPlaceholderStatusLines(const APlayerController* PlayerController)
{
	TArray<FString> Lines;
	if (!PlayerController)
	{
		return Lines;
	}
	if (const ULureProgressionComponent* Progression = GetProgression(PlayerController))
	{
		const FString Cooler = ULureCatchLibrary::GetOwnCoolerStatus(FindPlayerState(PlayerController));
		Lines.Add(Cooler.IsEmpty() ? Progression->GetStatusText() : Progression->GetStatusText() + TEXT("   ") + Cooler);
	}
	Lines.Append(ULureCatchLibrary::GetPlaceholderLines(PlayerController)); // what the hands hold (T-030)
	const APawn* Pawn = PlayerController->GetPawn();
	if (const ULureInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<ULureInteractionComponent>() : nullptr)
	{
		const FString Prompt = Interaction->GetPromptText();
		if (!Prompt.IsEmpty())
		{
			Lines.Add(Prompt);
		}
	}
	return Lines;
}

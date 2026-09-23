// Lure: progression entry points for other systems (T-010).

#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Interaction/LureInteractionComponent.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/Controller.h"
#include "GameFramework/HUD.h"
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

ULureCoolerComponent* ULureProgressionLibrary::GetCooler(const AActor* Context)
{
	const APlayerState* PlayerState = FindPlayerState(Context);
	return PlayerState ? PlayerState->FindComponentByClass<ULureCoolerComponent>() : nullptr;
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

int32 ULureProgressionLibrary::HandlePlayerCaught(AActor* Context)
{
	ULureCoolerComponent* Cooler = GetCooler(Context);
	const int32 Lost = Cooler ? Cooler->Clear() : 0;
	if (Lost > 0)
	{
		UE_LOG(LogLureProgression, Log, TEXT("%s was caught: %d unsold fish lost."), *GetNameSafe(Context), Lost);
	}
	return Lost;
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
		Lines.Add(Progression->GetStatusText());
	}
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

namespace LureProgressionHud
{
	/** Plain white text, top-left (placeholder UI: no styling until Jimmy directs the UI). */
	static void Draw(AHUD* HUD, UCanvas* Canvas)
	{
		if (!HUD || !Canvas || !GEngine || !GetDefault<ULureProgressionSettings>()->bShowPlaceholderText)
		{
			return;
		}
		const TArray<FString> Lines = ULureProgressionLibrary::GetPlaceholderStatusLines(HUD->GetOwningPlayerController());
		UFont* Font = GEngine->GetMediumFont();
		if (!Font)
		{
			return;
		}
		float Y = 24.0f;
		for (const FString& Line : Lines)
		{
			Canvas->SetDrawColor(FColor::White);
			Canvas->DrawText(Font, Line, 24.0f, Y);
			float Width = 0.0f;
			float Height = 0.0f;
			Canvas->StrLen(Font, Line, Width, Height);
			Y += Height + 4.0f;
		}
	}
}

void ULureProgressionLibrary::RegisterPlaceholderHud()
{
	static bool bRegistered = false;
	if (!bRegistered)
	{
		bRegistered = true;
		AHUD::OnHUDPostRender.AddStatic(&LureProgressionHud::Draw);
	}
}

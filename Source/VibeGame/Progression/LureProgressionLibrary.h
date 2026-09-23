// Lure: progression entry points for other systems (T-010).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Fish/FishInstance.h"
#include "Progression/LureProgressionTypes.h"
#include "LureProgressionLibrary.generated.h"

class APlayerController;
class APlayerState;
class ULureCoolerComponent;
class ULureProgressionComponent;

/**
 *  Finds a player's progression from any actor that belongs to them (pawn, controller, player state, or an actor they
 *  own), so other systems don't need to know where it lives:
 *    T-007 reel fight, when a fish lands (server):   ULureProgressionLibrary::HandleFishLanded(Pawn, Fish)
 *    T-007 fight difficulty by level gap:            ULureProgressionLibrary::GetFishDifficultyMultiplier(Pawn, Fish.Level)
 *    T-025 Lure.GiveFish (server):                   HandleFishLanded(Pawn, Fish)   (XP + cooler; GetCooler(Pawn)->AddFish for cooler only)
 *    T-017 caught (server):                          ULureProgressionLibrary::HandlePlayerCaught(Pawn)
 *    T-019 save/load (server):                       GetProgression(PlayerState)->GetSaveData() / ApplySaveData(Data)
 */
UCLASS()
class ULureProgressionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The player state of Context: itself, a pawn's or controller's player state, or its owner's (a few levels up) */
	static APlayerState* FindPlayerState(const AActor* Context);

	UFUNCTION(BlueprintPure, Category="Lure|Progression", meta=(DefaultToSelf="Context"))
	static ULureProgressionComponent* GetProgression(const AActor* Context);

	UFUNCTION(BlueprintPure, Category="Lure|Progression", meta=(DefaultToSelf="Context"))
	static ULureCoolerComponent* GetCooler(const AActor* Context);

	/** Server: XP + cooler for a landed fish (ULureProgressionComponent::HandleFishLanded). bAccepted false if no progression was found. */
	UFUNCTION(BlueprintCallable, Category="Lure|Progression", meta=(DefaultToSelf="Context"))
	static FLureFishLandedResult HandleFishLanded(AActor* Context, const FFishInstance& Fish);

	/** Level-gap difficulty for a fish of FishLevel against Context's player (level 1 if no progression is found) */
	UFUNCTION(BlueprintPure, Category="Lure|Progression", meta=(DefaultToSelf="Context"))
	static float GetFishDifficultyMultiplier(const AActor* Context, int32 FishLevel);

	/**
	 *  Server: the T-010 part of getting caught: the cooler is emptied (unsold fish lost); money, XP and level stay.
	 *  Returns the number of fish lost. T-017 adds respawn and gear wear around it.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Progression", meta=(DefaultToSelf="Context"))
	static int32 HandlePlayerCaught(AActor* Context);

	/** Placeholder HUD lines for a local player: the status line and, near an interactable, its prompt (drawn by ALureHUD) */
	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	static TArray<FString> GetPlaceholderStatusLines(const APlayerController* PlayerController);
};

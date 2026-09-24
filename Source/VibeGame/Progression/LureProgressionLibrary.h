// Lure: progression entry points for other systems (T-010).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Fish/FishInstance.h"
#include "Progression/LureProgressionTypes.h"
#include "LureProgressionLibrary.generated.h"

class APlayerController;
class APlayerState;
class ULureProgressionComponent;

/**
 *  Finds a player's progression from any actor that belongs to them (pawn, controller, player state, or an actor they
 *  own), so other systems don't need to know where it lives:
 *    Landing a fish (server):                        ULureCatchLibrary::HandleFishLanded(Pawn, Fish) (XP here + the fish on the hook, T-030)
 *    Only the landing XP (server):                   ULureProgressionLibrary::HandleFishLanded(Pawn, Fish)
 *    T-007 fight difficulty by level gap:            ULureProgressionLibrary::GetFishDifficultyMultiplier(Pawn, Fish.Level)
 *    T-017 caught (server):                          ULureCatchLibrary::HandlePlayerCaught(Pawn) (money, XP and level are kept)
 *    T-019 save/load (server):                       ULureCatchLibrary::GetPlayerSaveData / ApplyPlayerSaveData (progression + coolers)
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

	/** Server: the landing XP (ULureProgressionComponent::HandleFishLanded). bAccepted false if no progression was found. The whole landing is ULureCatchLibrary::HandleFishLanded. */
	UFUNCTION(BlueprintCallable, Category="Lure|Progression", meta=(DefaultToSelf="Context"))
	static FLureFishLandedResult HandleFishLanded(AActor* Context, const FFishInstance& Fish);

	/** Level-gap difficulty for a fish of FishLevel against Context's player (level 1 if no progression is found) */
	UFUNCTION(BlueprintPure, Category="Lure|Progression", meta=(DefaultToSelf="Context"))
	static float GetFishDifficultyMultiplier(const AActor* Context, int32 FishLevel);

	/** Placeholder HUD lines for a local player: the status line (with "Cooler 3/4"), what the hands hold, and the use-key prompt (drawn by ALureHUD) */
	UFUNCTION(BlueprintPure, Category="Lure|Progression")
	static TArray<FString> GetPlaceholderStatusLines(const APlayerController* PlayerController);
};

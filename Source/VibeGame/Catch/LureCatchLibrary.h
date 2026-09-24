// Lure: catch handling entry points for other systems (T-030). Rules: docs/specs/catch-handling-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Catch/LureCatchTypes.h"
#include "Fish/FishInstance.h"
#include "Progression/LureProgressionTypes.h"
#include "LureCatchLibrary.generated.h"

class ALureCoolerActor;
class APlayerController;
class APlayerState;

/**
 *  What other systems call (server unless noted):
 *    Landing (the fight, T-007/T-028; Lure.GiveFish, T-025):  ULureCatchLibrary::HandleFishLanded(Pawn, Fish)
 *    Getting caught (T-017):                                   ULureCatchLibrary::HandlePlayerCaught(Pawn)
 *    A player's first spawn (ALureGameMode):                   ULureCatchLibrary::EnsureStarterCooler(PlayerState, StartSpot)
 *    Save/load (T-019):                                        GetPlayerSaveData / ApplyPlayerSaveData (progression + coolers)
 *    Placeholder HUD (every machine):                          GetPlaceholderLines, GetOwnCoolerStatus
 */
UCLASS()
class ULureCatchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 *  THE landing entry point: the XP now (ULureProgressionComponent::HandleFishLanded, T-010 rule: on landing, never on
	 *  selling) and the fish item on Context's hook (Context = the pawn, or its controller). bAccepted false for an invalid
	 *  fish, on a client, or when there is neither progression nor hands. Fresh from this moment (freshness time zero).
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Catch", meta=(DefaultToSelf="Context"))
	static FLureFishLandedResult HandleFishLanded(AActor* Context, const FFishInstance& Fish);

	/**
	 *  The catch part of getting caught (T-017): the fish in Context's hand and on its hook are lost; a carried cooler is put
	 *  down at the last dry ground spot; coolers elsewhere stay. Money, XP and level are kept. Returns the fish lost.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Catch", meta=(DefaultToSelf="Context"))
	static int32 HandlePlayerCaught(AActor* Context);

	/**
	 *  A starter cooler (ULureProgressionSettings DefaultCoolerId) for PlayerState if it owns none and
	 *  ULureCatchSettings bSpawnStarterCooler is on: at an actor tagged CoolerSpawnTag (players' coolers StarterCoolerSpacing
	 *  apart along its +Y), else at StarterCoolerOffset in StartSpot's frame (players sharing one start: the same row along
	 *  its +Y; slots with a cooler already in them are skipped), else (no marker, and StartSpot null or the world settings
	 *  the engine uses on a map with no PlayerStart) the same row next to the first player's pawn, with a warning. Set on
	 *  the floor below. Null otherwise.
	 */
	static ALureCoolerActor* EnsureStarterCooler(APlayerState* PlayerState, const AActor* StartSpot);

	/** The coolers made for PlayerState (every machine: OwningPlayerState replicates) */
	static TArray<ALureCoolerActor*> GetOwnedCoolers(const APlayerState* PlayerState);

	// ---- Save / load (T-019; world items, never copied by reconnects or travel) ----

	/** Every cooler PlayerState owns: identity, row, place, lid, contents with their exposure now */
	static TArray<FLureCoolerSaveData> GetCoolerSaveData(const APlayerState* PlayerState);

	/**
	 *  Restores PlayerState's coolers: a cooler with the saved guid still in the world wins (a reconnect or re-apply: only
	 *  its owner is set, its saved fish are never restored, so no fish is ever copied), a missing one is spawned with its fish; the empty automatic starter cooler is removed when the save has coolers of its own. Returns the coolers applied.
	 */
	static int32 ApplyCoolerSaveData(APlayerState* PlayerState, const TArray<FLureCoolerSaveData>& Coolers);

	/** Progression (money, XP, level) + coolers, for T-019 */
	static FLurePlayerSaveData GetPlayerSaveData(const APlayerState* PlayerState);

	static bool ApplyPlayerSaveData(APlayerState* PlayerState, const FLurePlayerSaveData& Data);

	// ---- Placeholder HUD (every machine) ----

	/** "Holding: Bonefish (Rare), 2.04 kg, 45 coins, fresh 87%" or "Carrying: Starter cooler (3/4, closed)" (empty hands: none) */
	static TArray<FString> GetPlaceholderLines(const APlayerController* PlayerController);

	/** "Cooler 3/4" for the player's own cooler (empty if they have none) */
	static FString GetOwnCoolerStatus(const APlayerState* PlayerState);

	/**
	 *  The status line's cooler count (T-030g): the cooler this player carries (anyone's: the count matches the "Carrying:"
	 *  line), else their own cooler (GetOwnCoolerStatus). Empty if neither.
	 */
	static FString GetCoolerStatus(const APlayerController* PlayerController);
};

// Lure: game mode (T-004).

#include "Game/LureGameMode.h"
#include "Catch/LureCatchLibrary.h"
#include "Game/LureGameState.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Character/LurePlayerCharacter.h"
#include "Game/LureHUD.h"

ALureGameMode::ALureGameMode()
{
	GameStateClass = ALureGameState::StaticClass(); // the day/night clock (T-068a)
	PlayerStateClass = ALurePlayerState::StaticClass(); // money, XP and level (T-010); coolers are world actors (T-030)
	DefaultPawnClass = ALurePlayerCharacter::StaticClass();
	HUDClass = ALureHUD::StaticClass(); // placeholder text HUD (fishing prompts, T-006)
}

void ALureGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
	// T-030: the starter cooler, once per player (it checks the coolers the player already owns).
	if (NewPlayer && NewPlayer->IsPlayerController() && NewPlayer->GetPawn() && NewPlayer->PlayerState)
	{
		ULureCatchLibrary::EnsureStarterCooler(NewPlayer->PlayerState, StartSpot);
	}
}

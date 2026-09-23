// Lure: game mode (T-004).

#include "Game/LureGameMode.h"
#include "Game/LurePlayerState.h"
#include "Character/LurePlayerCharacter.h"
#include "Game/LureHUD.h"

ALureGameMode::ALureGameMode()
{
	PlayerStateClass = ALurePlayerState::StaticClass(); // money, XP, level and the cooler (T-010)
	DefaultPawnClass = ALurePlayerCharacter::StaticClass();
	HUDClass = ALureHUD::StaticClass(); // placeholder text HUD (fishing prompts, T-006)
}

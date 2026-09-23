// Lure: game mode (T-004).

#include "Game/LureGameMode.h"
#include "Character/LurePlayerCharacter.h"

ALureGameMode::ALureGameMode()
{
	DefaultPawnClass = ALurePlayerCharacter::StaticClass();
}

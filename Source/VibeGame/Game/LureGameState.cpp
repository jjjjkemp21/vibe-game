// Lure: game state (T-068a).

#include "Game/LureGameState.h"
#include "Environment/LureDayClockComponent.h"

ALureGameState::ALureGameState()
{
	DayClock = CreateDefaultSubobject<ULureDayClockComponent>(TEXT("DayClock"));
}

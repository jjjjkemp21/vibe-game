// Lure: game state (T-068a). Holds world-wide replicated state: the day/night clock.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "LureGameState.generated.h"

class ULureDayClockComponent;

/** Lure's game state (ALureGameMode's GameStateClass). Always relevant, so its components reach every client. */
UCLASS()
class ALureGameState : public AGameStateBase
{
	GENERATED_BODY()

public:

	ALureGameState();

	/** The server-owned time of day (T-068a). */
	ULureDayClockComponent* GetDayClock() const { return DayClock; }

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Lure|DayNight")
	TObjectPtr<ULureDayClockComponent> DayClock;
};

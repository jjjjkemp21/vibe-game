// Lure: game mode (T-004).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LureGameMode.generated.h"

/**
 *  Lure's game mode: spawns ALurePlayerCharacter for every player (server-authoritative; works for listen servers).
 *  It is the project's global default game mode (Config/DefaultEngine.ini); maps without a World Settings override use it.
 *  T-030: a player's first spawn at a player start also gives them a starter cooler (ULureCatchLibrary::EnsureStarterCooler;
 *  respawns don't, and a loaded save replaces it).
 */
UCLASS()
class ALureGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	ALureGameMode();

	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
};

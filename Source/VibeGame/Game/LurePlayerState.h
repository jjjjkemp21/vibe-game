// Lure: player state (T-010).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "LurePlayerState.generated.h"

class ULureProgressionComponent;

/**
 *  Lure's player state (ALureGameMode::PlayerStateClass): holds what belongs to the player rather than the body.
 *  The PlayerState outlives the pawn, so money, XP and level survive respawn and a change of pawn (boat).
 *  The cooler is a physical world object (ALureCoolerActor, T-030) that remembers this player state for saves.
 *  Seamless travel and reconnects carry the progression over (CopyProperties / OverrideWith through the save struct);
 *  world items (coolers, fish) never travel that way, so nothing is duplicated.
 */
UCLASS()
class ALurePlayerState : public APlayerState
{
	GENERATED_BODY()

public:

	ALurePlayerState(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category="Lure|Player")
	ULureProgressionComponent* GetProgression() const { return Progression; }

protected:

	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<ULureProgressionComponent> Progression;
};

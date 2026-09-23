// Lure: player state (T-010).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "LurePlayerState.generated.h"

class ULureCoolerComponent;
class ULureProgressionComponent;

/**
 *  Lure's player state (ALureGameMode::PlayerStateClass): holds what belongs to the player rather than the body.
 *  The PlayerState outlives the pawn, so money, XP, level and the cooler survive respawn and a change of pawn (boat).
 *  Getting caught empties the cooler explicitly (ULureProgressionLibrary::HandlePlayerCaught, T-017).
 *  Seamless travel and reconnects carry the progression over (CopyProperties / OverrideWith through the save struct).
 */
UCLASS()
class ALurePlayerState : public APlayerState
{
	GENERATED_BODY()

public:

	ALurePlayerState(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category="Lure|Player")
	ULureCoolerComponent* GetCooler() const { return Cooler; }

	UFUNCTION(BlueprintPure, Category="Lure|Player")
	ULureProgressionComponent* GetProgression() const { return Progression; }

protected:

	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<ULureCoolerComponent> Cooler;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<ULureProgressionComponent> Progression;
};

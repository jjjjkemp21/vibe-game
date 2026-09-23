// Lure: player state (T-010).

#include "Game/LurePlayerState.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"

ALurePlayerState::ALurePlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Cooler = CreateDefaultSubobject<ULureCoolerComponent>(TEXT("Cooler"));
	Progression = CreateDefaultSubobject<ULureProgressionComponent>(TEXT("Progression"));
}

void ALurePlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Seamless travel / inactive player state: the new player state gets this player's progression.
	const ALurePlayerState* Target = Cast<ALurePlayerState>(PlayerState);
	if (Target && Target->Progression && Progression)
	{
		Target->Progression->ApplySaveData(Progression->GetSaveData());
	}
}

void ALurePlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);

	// Reconnect: the old (inactive) player state's progression wins.
	const ALurePlayerState* Old = Cast<ALurePlayerState>(PlayerState);
	if (Old && Old->Progression && Progression)
	{
		Progression->ApplySaveData(Old->Progression->GetSaveData());
	}
}

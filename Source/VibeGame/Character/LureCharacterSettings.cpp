// Lure: character settings (T-004).

#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Engine/DataTable.h"

ULureCharacterSettings::ULureCharacterSettings()
{
	// Defaults mirror Config/DefaultGame.ini, which is the place to change them.
	MovementTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Movement.DT_Movement")));

	MoveForwardKeys = { EKeys::W, EKeys::Up };
	MoveBackwardKeys = { EKeys::S, EKeys::Down };
	MoveLeftKeys = { EKeys::A, EKeys::Left };
	MoveRightKeys = { EKeys::D, EKeys::Right };
	MoveStickKeys = { EKeys::Gamepad_Left2D };
	LookMouseKeys = { EKeys::Mouse2D };
	LookStickKeys = { EKeys::Gamepad_Right2D };
	JumpKeys = { EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom };
	SprintKeys = { EKeys::LeftShift, EKeys::Gamepad_LeftThumbstick };
	CrouchKeys = { EKeys::C, EKeys::LeftControl, EKeys::Gamepad_FaceButton_Right };
	ProneKeys = { EKeys::Z, EKeys::Gamepad_DPad_Down };
}

#if WITH_EDITOR
void ULureCharacterSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Key or look changes apply to the shared mapping context right away (same action objects).
	if (ULureInputSubsystem* Input = ULureInputSubsystem::Get())
	{
		Input->RebuildMappings();
	}
}
#endif

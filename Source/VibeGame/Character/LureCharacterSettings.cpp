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
	InteractKeys = { EKeys::E, EKeys::Gamepad_FaceButton_Left };
	AltInteractKeys = { EKeys::F, EKeys::Gamepad_FaceButton_Top };
	DebugMenuKeys = { EKeys::F6 }; // T-051
	MouseSensitivityDownKeys = { EKeys::Hyphen, EKeys::Subtract };
	MouseSensitivityUpKeys = { EKeys::Equals, EKeys::Add };
}

FLureMouseSensitivityTuning FLureMouseSensitivityTuning::Sanitized() const
{
	auto Finite = [](float Value, float Fallback) { return FMath::IsFinite(Value) ? Value : Fallback; };
	FLureMouseSensitivityTuning Out;
	Out.Min = FMath::Max(0.01f, Finite(Min, 0.1f));
	Out.Max = FMath::Max(Out.Min, Finite(Max, 5.f));
	Out.Default = FMath::Clamp(Finite(Default, 1.f), Out.Min, Out.Max);
	Out.Step = Finite(Step, 0.1f) > 0.f ? Step : 0.1f;
	return Out;
}

float FLureMouseSensitivityTuning::Clamp(float Value) const
{
	return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Default;
}

float FLureMouseSensitivityTuning::StepFrom(float Current, int32 Steps) const
{
	const float Base = Clamp(Current);
	// Snap to the step grid so repeated presses never drift (1.2000001) and a press always moves at least one step.
	const float Snapped = Step > 0.f ? FMath::RoundToFloat(Base / Step + static_cast<float>(Steps)) * Step : Base;
	return Clamp(Snapped);
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

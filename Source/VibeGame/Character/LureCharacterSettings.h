// Lure: character settings (T-004). Project Settings > Game > Lure Character; stored in Config/DefaultGame.ini.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "Character/LureArmsBob.h"
#include "LureCharacterSettings.generated.h"

class UDataTable;

/**
 *  T-051: the player's mouse sensitivity (a multiplier on mouse look, both axes; the gamepad stick is not scaled).
 *  The value itself is a per-machine preference (ULureUserSettings, GameUserSettings.ini); these are its data limits.
 */
USTRUCT(BlueprintType)
struct FLureMouseSensitivityTuning
{
	GENERATED_BODY()

	/** Used until the player changes it, and when the saved value is missing or unreadable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0.01"))
	float Default = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0.01"))
	float Min = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0.01"))
	float Max = 5.f;

	/** One press of the debug menu's sensitivity keys changes it by this much. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0.001"))
	float Step = 0.1f;

	/** A usable copy: Min >= 0.01, Max >= Min, Default inside [Min, Max], Step > 0 (bad data never breaks the look). */
	FLureMouseSensitivityTuning Sanitized() const;

	/** Value clamped to [Min, Max] (Default when not finite). Call on a Sanitized() tuning. */
	float Clamp(float Value) const;

	/** Current moved by Steps steps (negative = lower), snapped to the step grid and clamped. Call on a Sanitized() tuning. */
	float StepFrom(float Current, int32 Steps) const;
};

/**
 *  Data and control settings for ALurePlayerCharacter, in [/Script/VibeGame.LureCharacterSettings] (DefaultGame.ini).
 *  Movement tuning itself is in DT_Movement (one row per Stand, Sprint, Crouch, Prone).
 *  Default keys: WASD/arrows + mouse, Space jump, Left Shift sprint (hold), C or Left Ctrl crouch (toggle), Z prone (toggle);
 *  gamepad: left stick move, right stick look, A jump, L3 sprint, B crouch, D-pad down prone. F6 debug menu (T-051); F8 stays free for playtest notes.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Lure Character"))
class ULureCharacterSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULureCharacterSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Movement tuning table (row struct LureMovementRow; source data/tables/DT_Movement.csv). Missing = built-in fallback rows + one warning. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureMovementRow"))
	TSoftObjectPtr<UDataTable> MovementTable;

	/** First-person arms bob / look sway / landing dip (the per-stance amplitudes are DT_Movement columns). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="First Person")
	FLureArmsMotionSettings ArmsMotion;

	/** Sprint key: false = hold to sprint, true = press to toggle (toggled sprint ends when you stop moving). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls")
	bool bSprintIsToggle = false;

	/** Crouch key: true = press to toggle, false = hold to crouch. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls")
	bool bCrouchIsToggle = true;

	/** Prone key: true = press to toggle, false = hold to stay prone. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls")
	bool bProneIsToggle = true;

	/** Toggled sprint switches off after this many seconds without movement input. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls", meta=(ClampMin="0"))
	float SprintToggleStopDelay = 0.3f;

	/** Mouse look: degrees of turn per mouse count. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Look", meta=(ClampMin="0.001"))
	float MouseDegreesPerCount = 0.07f;

	/** Gamepad look speed at full stick, degrees per second (X = yaw, Y = pitch). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Look")
	FVector2D GamepadLookRate = FVector2D(150.f, 110.f);

	/** T-051: mouse sensitivity limits (default, min, max, step). The player's own value is saved per machine (ULureUserSettings). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Look")
	FLureMouseSensitivityTuning MouseSensitivity;

	/** Invert vertical look (mouse and gamepad). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Look")
	bool bInvertLookY = false;

	/** Stick dead zones (0..1). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Look", meta=(ClampMin="0", ClampMax="0.9"))
	float GamepadMoveDeadZone = 0.2f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Look", meta=(ClampMin="0", ClampMax="0.9"))
	float GamepadLookDeadZone = 0.15f;

	/** Move (Axis2D: X = right, Y = forward). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> MoveForwardKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> MoveBackwardKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> MoveLeftKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> MoveRightKeys;

	/** 2D sticks that move (e.g. Gamepad_Left2D). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> MoveStickKeys;

	/** Look (Axis2D, degrees per frame: X = yaw, Y = pitch up). Mouse axes (e.g. Mouse2D). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> LookMouseKeys;

	/** 2D sticks that look (e.g. Gamepad_Right2D). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> LookStickKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> JumpKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> SprintKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> CrouchKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> ProneKeys;

	/** Interact: the main verb on what you look at (grab a fish, open the cooler, put in, take out, sell; T-010/T-030); later NPCs */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> InteractKeys;

	/** Alt Interact: the second verb (pick up / put down the cooler, close it, drop or release a fish; T-030) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> AltInteractKeys;

	/** T-051: opens / closes the debug menu (every key bind + mouse sensitivity). F6: F1 is the engine's wireframe debug key. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> DebugMenuKeys;

	/** T-051: lower / raise the mouse sensitivity one step while the debug menu is open (they do nothing when it is closed). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> MouseSensitivityDownKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls|Keys")
	TArray<FKey> MouseSensitivityUpKeys;
};

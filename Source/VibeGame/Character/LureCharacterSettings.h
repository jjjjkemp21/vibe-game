// Lure: character settings (T-004). Project Settings > Game > Lure Character; stored in Config/DefaultGame.ini.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "Character/LureArmsBob.h"
#include "LureCharacterSettings.generated.h"

class UDataTable;

/**
 *  Data and control settings for ALurePlayerCharacter, in [/Script/VibeGame.LureCharacterSettings] (DefaultGame.ini).
 *  Movement tuning itself is in DT_Movement (one row per Stand, Sprint, Crouch, Prone).
 *  Default keys: WASD/arrows + mouse, Space jump, Left Shift sprint (hold), C or Left Ctrl crouch (toggle), Z prone (toggle);
 *  gamepad: left stick move, right stick look, A jump, L3 sprint, B crouch, D-pad down prone. F8 stays free for playtest notes.
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
};

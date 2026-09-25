// Lure: the player's own preferences on this machine (T-051: mouse sensitivity). Local only, never replicated.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Character/LureCharacterSettings.h"
#include "LureUserSettings.generated.h"

class FConfigFile;

/**
 *  Per-machine player preferences, saved in the user's GameUserSettings.ini (Saved/Config/<Platform>/GameUserSettings.ini,
 *  section [/Script/VibeGame.LureUserSettings]). An engine subsystem: one value for the whole machine, shared by every
 *  local player, loaded at engine start (before ULureInputSubsystem builds the mappings) and never part of gameplay
 *  state, so nothing here replicates.
 *  Why config and not a SaveGame slot: it is a plain preference like the engine's own GameUserSettings (resolution,
 *  quality), readable and editable by hand, and needs no async load before the first mouse move.
 *  Limits (default, min, max, step) are data: ULureCharacterSettings::MouseSensitivity.
 */
UCLASS()
class ULureUserSettings : public UEngineSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** The engine's instance (null before engine init or after shutdown). */
	static ULureUserSettings* Get();

	/** The mouse sensitivity in use (a multiplier on mouse look, both axes). */
	UFUNCTION(BlueprintPure, Category="Lure|Settings")
	float GetMouseSensitivity() const { return MouseSensitivity; }

	/** Sets the mouse sensitivity (clamped to the data limits), applies it to the mouse look at once and saves it. Returns the value in use. */
	UFUNCTION(BlueprintCallable, Category="Lure|Settings")
	float SetMouseSensitivity(float Value);

	/** Moves the mouse sensitivity by Steps data steps (negative = lower); see SetMouseSensitivity. Returns the value in use. */
	UFUNCTION(BlueprintCallable, Category="Lure|Settings")
	float StepMouseSensitivity(int32 Steps);

	/** The data limits, sanitized (ULureCharacterSettings::MouseSensitivity). */
	static FLureMouseSensitivityTuning GetTuning();

	/** Config section and key of the saved value. */
	static const TCHAR* ConfigSection;
	static const TCHAR* MouseSensitivityKey;

	/** A saved value as text -> the sensitivity to use: missing (null), unreadable or not finite -> Tuning.Default; otherwise clamped. */
	static float ParseSavedMouseSensitivity(const FString* SavedText, const FLureMouseSensitivityTuning& Tuning);

	/** The text written for Value (round-trips through ParseSavedMouseSensitivity). */
	static FString FormatMouseSensitivity(float Value);

	/** Reads the saved value from File (ParseSavedMouseSensitivity rules). Tests use their own FConfigFile, never the user's. */
	static float LoadMouseSensitivity(const FConfigFile& File, const FLureMouseSensitivityTuning& Tuning);

	/** Writes Value into File (the caller writes the file to disk). */
	static void SaveMouseSensitivity(FConfigFile& File, float Value);

private:

	/** Reads the user's GameUserSettings.ini. */
	void LoadFromUserConfig();

	/** Writes the user's GameUserSettings.ini. */
	void SaveToUserConfig() const;

	float MouseSensitivity = 1.f;
};

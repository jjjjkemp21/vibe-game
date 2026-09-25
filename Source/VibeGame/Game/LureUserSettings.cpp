// Lure: the player's own preferences on this machine (T-051: mouse sensitivity). Local only, never replicated.

#include "Game/LureUserSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DefaultValueHelper.h"
#include "VibeGame.h"

const TCHAR* ULureUserSettings::ConfigSection = TEXT("/Script/VibeGame.LureUserSettings");
const TCHAR* ULureUserSettings::MouseSensitivityKey = TEXT("MouseSensitivity");

void ULureUserSettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadFromUserConfig();
}

ULureUserSettings* ULureUserSettings::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<ULureUserSettings>() : nullptr;
}

FLureMouseSensitivityTuning ULureUserSettings::GetTuning()
{
	return GetDefault<ULureCharacterSettings>()->MouseSensitivity.Sanitized();
}

float ULureUserSettings::SetMouseSensitivity(float Value)
{
	const float Applied = GetTuning().Clamp(Value);
	if (Applied != MouseSensitivity)
	{
		MouseSensitivity = Applied;
		SaveToUserConfig();
		// The mouse look scale lives in the shared mapping context: rebuild it so the new value is felt on the next mouse move.
		if (ULureInputSubsystem* Input = ULureInputSubsystem::Get())
		{
			Input->RebuildMappings();
		}
		UE_LOG(LogVibeGame, Log, TEXT("Mouse sensitivity %.2f (saved in GameUserSettings.ini)"), MouseSensitivity);
	}
	return MouseSensitivity;
}

float ULureUserSettings::StepMouseSensitivity(int32 Steps)
{
	return SetMouseSensitivity(GetTuning().StepFrom(MouseSensitivity, Steps));
}

float ULureUserSettings::ParseSavedMouseSensitivity(const FString* SavedText, const FLureMouseSensitivityTuning& Tuning)
{
	if (!SavedText)
	{
		return Tuning.Default;
	}
	float Parsed = 0.f;
	if (!FDefaultValueHelper::ParseFloat(SavedText->TrimStartAndEnd(), Parsed) || !FMath::IsFinite(Parsed))
	{
		return Tuning.Default;
	}
	return Tuning.Clamp(Parsed);
}

FString ULureUserSettings::FormatMouseSensitivity(float Value)
{
	return FString::Printf(TEXT("%.4f"), Value);
}

float ULureUserSettings::LoadMouseSensitivity(const FConfigFile& File, const FLureMouseSensitivityTuning& Tuning)
{
	FString Text;
	const bool bFound = File.GetString(ConfigSection, MouseSensitivityKey, Text);
	return ParseSavedMouseSensitivity(bFound ? &Text : nullptr, Tuning);
}

void ULureUserSettings::SaveMouseSensitivity(FConfigFile& File, float Value)
{
	File.SetString(ConfigSection, MouseSensitivityKey, *FormatMouseSensitivity(Value));
}

void ULureUserSettings::LoadFromUserConfig()
{
	const FLureMouseSensitivityTuning Tuning = GetTuning();
	FString Text;
	const bool bFound = GConfig && GConfig->GetString(ConfigSection, MouseSensitivityKey, Text, GGameUserSettingsIni);
	MouseSensitivity = ParseSavedMouseSensitivity(bFound ? &Text : nullptr, Tuning);
	if (bFound && MouseSensitivity == Tuning.Default && !Text.TrimStartAndEnd().IsNumeric())
	{
		UE_LOG(LogVibeGame, Warning, TEXT("GameUserSettings.ini: MouseSensitivity '%s' is not a number; using the default %.2f."), *Text, Tuning.Default);
	}
}

void ULureUserSettings::SaveToUserConfig() const
{
	if (!GConfig || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}
	GConfig->SetString(ConfigSection, MouseSensitivityKey, *FormatMouseSensitivity(MouseSensitivity), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

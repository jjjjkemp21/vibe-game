// Lure: the debug menu (T-051): every key bind plus the mouse sensitivity, as plain text drawn by ALureHUD.

#include "Dev/LureDebugMenu.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Game/LureUserSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Playtest/PlaytestFeedbackSubsystem.h"
#include "Playtest/PlaytestNoteWriter.h"

namespace LureDebugMenuPrivate
{
	/** The keys Context maps to Action, keyboard/mouse or gamepad only, comma separated. */
	FString DescribeActionKeys(const UInputMappingContext* Context, const UInputAction* Action, bool bGamepad)
	{
		TArray<FString> Keys;
		if (Context && Action)
		{
			for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
			{
				if (Mapping.Action == Action && Mapping.Key.IsGamepadKey() == bGamepad)
				{
					Keys.AddUnique(ULureDebugMenu::KeyText(Mapping.Key));
				}
			}
		}
		return FString::Join(Keys, TEXT(", "));
	}
}

ULureDebugMenu* ULureDebugMenu::Get(const APlayerController* PlayerController)
{
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<ULureDebugMenu>() : nullptr;
}

void ULureDebugMenu::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
}

float ULureDebugMenu::AdjustMouseSensitivity(int32 Steps)
{
	ULureUserSettings* UserSettings = ULureUserSettings::Get();
	if (!UserSettings)
	{
		return 1.f;
	}
	if (bOpen && Steps != 0)
	{
		return UserSettings->StepMouseSensitivity(Steps);
	}
	return UserSettings->GetMouseSensitivity();
}

TArray<FString> ULureDebugMenu::GetLines() const
{
	const ULureUserSettings* UserSettings = ULureUserSettings::Get();
	return BuildLines(ULureInputSubsystem::GetDefaultMappingContext(), UserSettings ? UserSettings->GetMouseSensitivity() : 1.f, ULureUserSettings::GetTuning());
}

FString ULureDebugMenu::GetHintLine()
{
	const FString Keys = DescribeKeys(ULureInputSubsystem::GetDefaultMappingContext(), FLureInputActionNames::DebugMenu, false);
	return Keys.IsEmpty() ? FString() : FString::Printf(TEXT("%s: debug menu (key binds, mouse sensitivity)"), *Keys);
}

void ULureDebugMenu::BindInput(UEnhancedInputComponent& Input, const APlayerController* PlayerController)
{
	ULureDebugMenu* Menu = Get(PlayerController);
	UInputAction* ToggleAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::DebugMenu);
	UInputAction* DownAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::MouseSensitivityDown);
	UInputAction* UpAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::MouseSensitivityUp);
	if (!Menu || !ToggleAction || !DownAction || !UpAction)
	{
		return;
	}
	Input.BindAction(ToggleAction, ETriggerEvent::Started, Menu, &ULureDebugMenu::HandleToggle);
	Input.BindAction(DownAction, ETriggerEvent::Started, Menu, &ULureDebugMenu::HandleSensitivityDown);
	Input.BindAction(UpAction, ETriggerEvent::Started, Menu, &ULureDebugMenu::HandleSensitivityUp);
}

TArray<FString> ULureDebugMenu::BuildLines(const UInputMappingContext* Context, float Sensitivity, const FLureMouseSensitivityTuning& Tuning)
{
	TArray<FString> Lines;
	const FString ToggleKeys = DescribeKeys(Context, FLureInputActionNames::DebugMenu, false);
	Lines.Add(FString::Printf(TEXT("DEBUG MENU (%s closes)"), ToggleKeys.IsEmpty() ? TEXT("no key") : *ToggleKeys));
	Lines.Add(FString::Printf(TEXT("Mouse sensitivity: %.2fx   lower: %s   higher: %s   (%.2f to %.2f, saved on this PC)"),
		Sensitivity,
		*DescribeKeys(Context, FLureInputActionNames::MouseSensitivityDown, false),
		*DescribeKeys(Context, FLureInputActionNames::MouseSensitivityUp, false),
		Tuning.Min, Tuning.Max));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Key binds:"));
	Lines.Append(BuildBindLines(Context));
	Lines.Append(BuildExtraBindLines());
	return Lines;
}

TArray<FString> ULureDebugMenu::BuildBindLines(const UInputMappingContext* Context)
{
	// Lure's own actions in their usual order, then any other action the context maps (added without a name here).
	TArray<TPair<const UInputAction*, FString>> Entries;
	TArray<FName> Names = FLureInputActionNames::All();
	Names.Append(FLureInputActionNames::Menu());
	for (const FName& Name : Names)
	{
		if (const UInputAction* Action = ULureInputSubsystem::GetInputActionByName(Name))
		{
			Entries.Emplace(Action, ActionText(Name));
		}
	}
	if (Context)
	{
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			const UInputAction* Action = Mapping.Action.Get();
			if (Action && !Entries.ContainsByPredicate([Action](const TPair<const UInputAction*, FString>& Entry) { return Entry.Key == Action; }))
			{
				const FName Known = ULureInputSubsystem::GetInputActionName(Action);
				Entries.Emplace(Action, ActionText(Known.IsNone() ? Action->GetFName() : Known));
			}
		}
	}

	TArray<FString> Lines;
	for (const TPair<const UInputAction*, FString>& Entry : Entries)
	{
		const FString Keys = LureDebugMenuPrivate::DescribeActionKeys(Context, Entry.Key, false);
		const FString Pad = LureDebugMenuPrivate::DescribeActionKeys(Context, Entry.Key, true);
		FString Line = Entry.Value + TEXT(": ") + (Keys.IsEmpty() ? TEXT("(no key)") : Keys);
		if (!Pad.IsEmpty())
		{
			Line += TEXT("   pad: ") + Pad;
		}
		Lines.Add(Line);
	}
	return Lines;
}

TArray<FString> ULureDebugMenu::BuildExtraBindLines()
{
	TArray<FString> Lines;
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	const FKey NoteKey = GetDefault<UPlaytestFeedbackSubsystem>()->FeedbackKey;
	if (NoteKey.IsValid())
	{
		Lines.Add(FString::Printf(TEXT("Playtest note: %s"), *KeyText(NoteKey)));
	}
#endif
	TArray<FString> ConsoleKeys;
	for (const FKey& Key : GetDefault<UInputSettings>()->ConsoleKeys)
	{
		if (Key.IsValid())
		{
			ConsoleKeys.AddUnique(KeyText(Key));
		}
	}
	if (ConsoleKeys.Num() > 0)
	{
		Lines.Add(TEXT("Console: ") + FString::Join(ConsoleKeys, TEXT(", ")));
	}
#if WITH_EDITOR
	if (GIsEditor)
	{
		Lines.Add(TEXT("Free the mouse (editor play only): Shift+F1"));
	}
#endif
	return Lines;
}

FString ULureDebugMenu::DescribeKeys(const UInputMappingContext* Context, FName ActionName, bool bGamepad)
{
	return LureDebugMenuPrivate::DescribeActionKeys(Context, ULureInputSubsystem::GetInputActionByName(ActionName), bGamepad);
}

FString ULureDebugMenu::KeyText(const FKey& Key)
{
	return Key.GetDisplayName(/*bLongDisplayName*/ false).ToString();
}

FString ULureDebugMenu::ActionText(FName ActionName)
{
	return FName::NameToDisplayString(ActionName.ToString(), /*bIsBool*/ false);
}

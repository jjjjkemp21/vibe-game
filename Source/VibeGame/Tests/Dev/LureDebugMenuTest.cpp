// Lure T-051: the debug menu (every key bind + mouse sensitivity) and the saved per-machine mouse sensitivity.
// Tests never write the user's GameUserSettings.ini: saving is tested on their own FConfigFile and a file under the
// automation transient folder.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/ActorComponent.h"
#include "Dev/LureDebugMenu.h"
#include "EnhancedActionKeyMapping.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/LureUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Playtest/PlaytestFeedbackSubsystem.h"
#include "Playtest/PlaytestNoteWriter.h"
#include "Subsystems/EngineSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace LureDebugMenuTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Lure's action objects by name (gameplay + menu), as ULureInputSubsystem keeps them. */
	TMap<FName, TObjectPtr<UInputAction>> ActionMap()
	{
		TMap<FName, TObjectPtr<UInputAction>> Map;
		TArray<FName> Names = FLureInputActionNames::All();
		Names.Append(FLureInputActionNames::Menu());
		for (const FName& Name : Names)
		{
			if (UInputAction* Action = ULureInputSubsystem::GetInputActionByName(Name))
			{
				Map.Add(Name, Action);
			}
		}
		return Map;
	}

	/** A fresh transient context built like the live one, with the given mouse sensitivity. */
	UInputMappingContext* BuildContext(float MouseSensitivity)
	{
		UInputMappingContext* Context = NewObject<UInputMappingContext>(GetTransientPackage(), NAME_None, RF_Transient);
		ULureInputSubsystem::BuildMappings(*Context, ActionMap(), *GetDefault<ULureCharacterSettings>(), MouseSensitivity);
		return Context;
	}

	/** Runs Key's modifiers for Action in Context over Raw (the value the character's look handler receives). */
	bool ModifiedValue(const UInputMappingContext* Context, const UInputAction* Action, const FKey& Key, const FVector& Raw, FVector& Out)
	{
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == Action && Mapping.Key == Key)
			{
				FInputActionValue Value(EInputActionValueType::Axis2D, Raw);
				for (const UInputModifier* Modifier : Mapping.Modifiers)
				{
					if (Modifier)
					{
						Value = Modifier->ModifyRaw(nullptr, Value, 1.f / 60.f);
					}
				}
				Out = Value.Get<FVector>();
				return true;
			}
		}
		return false;
	}

	/** The line of Lines that starts with "<Action>: " (null if none). */
	const FString* FindActionLine(const TArray<FString>& Lines, const FString& ActionText)
	{
		const FString Prefix = ActionText + TEXT(": ");
		return Lines.FindByPredicate([&Prefix](const FString& Line) { return Line.StartsWith(Prefix); });
	}

	bool HasCppNetProperty(const UClass* Class)
	{
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Net))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDebugMenuListsEveryBind, "Project.Dev.DebugMenu.ListsEveryBind", LureDebugMenuTest::Flags)
bool FLureDebugMenuListsEveryBind::RunTest(const FString& Parameters)
{
	using namespace LureDebugMenuTest;
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	if (!TestNotNull(TEXT("the live mapping context"), Context) || !TestTrue(TEXT("it maps keys"), Context->GetMappings().Num() > 0))
	{
		return false;
	}
	const TArray<FString> Lines = ULureDebugMenu::BuildLines(Context, 1.f, ULureUserSettings::GetTuning());

	// (a) Every mapping of the live context: the action's line lists the key (iterated, not hardcoded).
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		const FName Name = ULureInputSubsystem::GetInputActionName(Mapping.Action.Get());
		const FString ActionText = ULureDebugMenu::ActionText(Name.IsNone() ? GetFNameSafe(Mapping.Action.Get()) : Name);
		const FString* Line = FindActionLine(Lines, ActionText);
		if (TestNotNull(FString::Printf(TEXT("a line for %s"), *ActionText), Line))
		{
			TestTrue(FString::Printf(TEXT("%s lists %s: '%s'"), *ActionText, *ULureDebugMenu::KeyText(Mapping.Key), **Line), Line->Contains(ULureDebugMenu::KeyText(Mapping.Key)));
		}
	}

	// The toggle key: in the title and in its own bind line.
	const FString ToggleKey = ULureDebugMenu::KeyText(EKeys::F6);
	TestTrue(TEXT("the default toggle key is F6"), GetDefault<ULureCharacterSettings>()->DebugMenuKeys.Contains(EKeys::F6));
	TestTrue(TEXT("the title names the toggle key"), Lines.Num() > 0 && Lines[0].Contains(ToggleKey));
	const FString* ToggleLine = FindActionLine(Lines, ULureDebugMenu::ActionText(FLureInputActionNames::DebugMenu));
	TestTrue(TEXT("the Debug Menu bind line lists F6"), ToggleLine && ToggleLine->Contains(ToggleKey));
	TestTrue(TEXT("the closed-menu hint names F6"), ULureDebugMenu::GetHintLine().StartsWith(ToggleKey + TEXT(":")));

	// Keys handled outside the mapping context: F8 (playtest note) and the console key.
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	const FString NoteKey = ULureDebugMenu::KeyText(GetDefault<UPlaytestFeedbackSubsystem>()->FeedbackKey);
	TestTrue(TEXT("the playtest note key is listed"), Lines.ContainsByPredicate([&NoteKey](const FString& Line) { return Line.StartsWith(TEXT("Playtest note: ")) && Line.Contains(NoteKey); }));
#endif
	TestTrue(TEXT("the console key is listed"), Lines.ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("Console: ")); }));

	// Every Lure action has a line, even without a key (Hook).
	TArray<FName> Names = FLureInputActionNames::All();
	Names.Append(FLureInputActionNames::Menu());
	for (const FName& Name : Names)
	{
		TestNotNull(FString::Printf(TEXT("a line for %s"), *Name.ToString()), FindActionLine(Lines, ULureDebugMenu::ActionText(Name)));
	}

	// A new bind shows up with no menu change: an extra action mapped in a copy of the context gets its own line.
	UInputMappingContext* Copy = BuildContext(1.f);
	UInputAction* Extra = NewObject<UInputAction>(GetTransientPackage(), TEXT("IA_Test_DebugMenuExtra"), RF_Transient);
	Extra->ValueType = EInputActionValueType::Boolean;
	Copy->MapKey(Extra, EKeys::K);
	const TArray<FString> CopyLines = ULureDebugMenu::BuildBindLines(Copy);
	const FString* ExtraLine = FindActionLine(CopyLines, ULureDebugMenu::ActionText(Extra->GetFName()));
	TestTrue(TEXT("an unknown mapped action is listed with its key"), ExtraLine && ExtraLine->Contains(ULureDebugMenu::KeyText(EKeys::K)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDebugMenuKeysAreFree, "Project.Dev.DebugMenu.KeysAreFree", LureDebugMenuTest::Flags)
bool FLureDebugMenuKeysAreFree::RunTest(const FString& Parameters)
{
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	const ULureCharacterSettings* Settings = GetDefault<ULureCharacterSettings>();
	if (!TestNotNull(TEXT("the live mapping context"), Context))
	{
		return false;
	}
	TArray<FKey> MenuKeys = Settings->DebugMenuKeys;
	MenuKeys.Append(Settings->MouseSensitivityDownKeys);
	MenuKeys.Append(Settings->MouseSensitivityUpKeys);
	TestTrue(TEXT("the menu has keys"), Settings->DebugMenuKeys.Num() > 0 && Settings->MouseSensitivityDownKeys.Num() > 0 && Settings->MouseSensitivityUpKeys.Num() > 0);
	for (const FKey& Key : MenuKeys)
	{
		int32 Actions = 0;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			Actions += Mapping.Key == Key ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("%s drives exactly one action (the menu's)"), *Key.ToString()), Actions, 1);
		TestFalse(FString::Printf(TEXT("%s is not the playtest note key"), *Key.ToString()), Key == EKeys::F8);
		// Unmodified engine debug keys (F1 wireframe, F9 screenshot, ...) would fire as well.
		for (const FKeyBind& Bind : GetDefault<UPlayerInput>()->DebugExecBindings)
		{
			const bool bPlainPress = !Bind.Control && !Bind.Shift && !Bind.Alt && !Bind.Cmd;
			TestFalse(FString::Printf(TEXT("%s is not an engine debug key (%s)"), *Key.ToString(), *Bind.Command), bPlainPress && Bind.Key == Key);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDebugMenuSensitivityStepsAndClamps, "Project.Dev.DebugMenu.SensitivityStepsAndClamps", LureDebugMenuTest::Flags)
bool FLureDebugMenuSensitivityStepsAndClamps::RunTest(const FString& Parameters)
{
	// (b) The data limits (Project Settings > Lure Character > Mouse Sensitivity).
	const FLureMouseSensitivityTuning Data = ULureUserSettings::GetTuning();
	TestTrue(TEXT("data: 0 < Min <= Default <= Max, Step > 0"), Data.Min > 0.f && Data.Min <= Data.Default && Data.Default <= Data.Max && Data.Step > 0.f);
	TestNearlyEqual(TEXT("one step up from the default"), Data.StepFrom(Data.Default, 1), FMath::Min(Data.Max, Data.Default + Data.Step), 1.0e-4f);
	TestNearlyEqual(TEXT("one step down from the default"), Data.StepFrom(Data.Default, -1), FMath::Max(Data.Min, Data.Default - Data.Step), 1.0e-4f);
	TestNearlyEqual(TEXT("steps up stop at Max"), Data.StepFrom(Data.Default, 10000), Data.Max, 1.0e-4f);
	TestNearlyEqual(TEXT("steps down stop at Min"), Data.StepFrom(Data.Default, -10000), Data.Min, 1.0e-4f);

	FLureMouseSensitivityTuning Custom;
	Custom.Default = 1.f;
	Custom.Min = 0.5f;
	Custom.Max = 2.f;
	Custom.Step = 0.25f;
	Custom = Custom.Sanitized();
	TestNearlyEqual(TEXT("custom: 1 + 1 step = 1.25"), Custom.StepFrom(1.f, 1), 1.25f, 1.0e-5f);
	TestNearlyEqual(TEXT("custom: 1 - 2 steps = 0.5"), Custom.StepFrom(1.f, -2), 0.5f, 1.0e-5f);
	TestNearlyEqual(TEXT("custom: clamps to Max 2"), Custom.StepFrom(1.75f, 3), 2.f, 1.0e-5f);
	TestNearlyEqual(TEXT("custom: clamps to Min 0.5"), Custom.StepFrom(0.5f, -1), 0.5f, 1.0e-5f);
	float Value = 1.f;
	for (int32 Press = 0; Press < 4; ++Press)
	{
		Value = Custom.StepFrom(Value, 1);
	}
	TestNearlyEqual(TEXT("custom: four presses land exactly on 2 (no drift)"), Value, 2.f, 1.0e-6f);
	TestNearlyEqual(TEXT("Clamp above Max"), Custom.Clamp(9.f), 2.f, 1.0e-6f);
	TestNearlyEqual(TEXT("Clamp NaN -> Default"), Custom.Clamp(NAN), 1.f, 1.0e-6f);

	// Bad data never breaks the look.
	FLureMouseSensitivityTuning Bad;
	Bad.Default = 50.f;
	Bad.Min = -1.f;
	Bad.Max = -5.f;
	Bad.Step = 0.f;
	const FLureMouseSensitivityTuning Fixed = Bad.Sanitized();
	TestTrue(TEXT("bad data: Min > 0, Max >= Min, Default in range, Step > 0"), Fixed.Min > 0.f && Fixed.Max >= Fixed.Min && Fixed.Default >= Fixed.Min && Fixed.Default <= Fixed.Max && Fixed.Step > 0.f);

	// The menu only changes the value while it is open (a closed menu never touches the saved setting).
	// A local player subsystem needs a local player outer (a bare one here: no controller, no viewport).
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
	ULureDebugMenu* Menu = NewObject<ULureDebugMenu>(LocalPlayer, NAME_None, RF_Transient);
	const ULureUserSettings* UserSettings = ULureUserSettings::Get();
	if (TestNotNull(TEXT("ULureUserSettings runs"), UserSettings))
	{
		const float Before = UserSettings->GetMouseSensitivity();
		TestFalse(TEXT("the menu starts closed"), Menu->IsOpen());
		TestEqual(TEXT("closed: + does nothing"), Menu->AdjustMouseSensitivity(1), Before);
		TestEqual(TEXT("... the setting is unchanged"), UserSettings->GetMouseSensitivity(), Before);
	}
	Menu->Toggle();
	TestTrue(TEXT("Toggle opens it"), Menu->IsOpen());
	Menu->Toggle();
	TestFalse(TEXT("Toggle closes it"), Menu->IsOpen());

	// The sensitivity line shows the value and the keys.
	const TArray<FString> Lines = ULureDebugMenu::BuildLines(ULureInputSubsystem::GetDefaultMappingContext(), 1.5f, Data);
	TestTrue(TEXT("the menu shows the sensitivity"), Lines.ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("Mouse sensitivity: 1.50x")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDebugMenuSensitivitySaveLoad, "Project.Dev.DebugMenu.SensitivitySaveLoad", LureDebugMenuTest::Flags)
bool FLureDebugMenuSensitivitySaveLoad::RunTest(const FString& Parameters)
{
	// (c) Round trip in a config file of our own (never the user's GameUserSettings.ini).
	const FLureMouseSensitivityTuning Tuning = ULureUserSettings::GetTuning();
	const float Saved = Tuning.StepFrom(Tuning.Default, 3);
	FConfigFile File;
	TestNearlyEqual(TEXT("missing value -> default"), ULureUserSettings::LoadMouseSensitivity(File, Tuning), Tuning.Default, 1.0e-6f);
	ULureUserSettings::SaveMouseSensitivity(File, Saved);
	TestNearlyEqual(TEXT("in memory: the saved value comes back"), ULureUserSettings::LoadMouseSensitivity(File, Tuning), Saved, 1.0e-4f);

	// Through a real file on disk.
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir() / TEXT("LureUserSettingsTest.ini"));
	IFileManager::Get().Delete(*Path, false, true, true);
	TestTrue(TEXT("the test ini is written"), File.Write(Path, /*bDoRemoteWrite*/ false));
	FConfigFile Reloaded;
	Reloaded.Read(Path);
	TestNearlyEqual(TEXT("on disk: the saved value comes back"), ULureUserSettings::LoadMouseSensitivity(Reloaded, Tuning), Saved, 1.0e-4f);

	// A hand-edited or corrupt file falls back to the default; an out-of-range number is clamped.
	auto LoadText = [&Tuning](const FString& Text)
	{
		const FString Ini = FString::Printf(TEXT("[%s]\n%s=%s\n"), ULureUserSettings::ConfigSection, ULureUserSettings::MouseSensitivityKey, *Text);
		const FString CorruptPath = FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir() / TEXT("LureUserSettingsCorrupt.ini"));
		FFileHelper::SaveStringToFile(Ini, *CorruptPath);
		FConfigFile Corrupt;
		Corrupt.Read(CorruptPath);
		IFileManager::Get().Delete(*CorruptPath, false, true, true);
		return ULureUserSettings::LoadMouseSensitivity(Corrupt, Tuning);
	};
	TestNearlyEqual(TEXT("corrupt 'abc' -> default"), LoadText(TEXT("abc")), Tuning.Default, 1.0e-6f);
	TestNearlyEqual(TEXT("empty -> default"), LoadText(TEXT("")), Tuning.Default, 1.0e-6f);
	TestNearlyEqual(TEXT("'1.5x' -> default"), LoadText(TEXT("1.5x")), Tuning.Default, 1.0e-6f);
	TestNearlyEqual(TEXT("far above Max -> Max"), LoadText(TEXT("9999")), Tuning.Max, 1.0e-6f);
	TestNearlyEqual(TEXT("negative -> Min"), LoadText(TEXT("-3")), Tuning.Min, 1.0e-6f);
	TestNearlyEqual(TEXT("a valid hand-edited value is kept"), LoadText(ULureUserSettings::FormatMouseSensitivity(Tuning.Min)), Tuning.Min, 1.0e-4f);
	TestNearlyEqual(TEXT("null text -> default"), ULureUserSettings::ParseSavedMouseSensitivity(nullptr, Tuning), Tuning.Default, 1.0e-6f);

	IFileManager::Get().Delete(*Path, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDebugMenuLookScalesWithSensitivity, "Project.Dev.DebugMenu.LookScalesWithSensitivity", LureDebugMenuTest::Flags)
bool FLureDebugMenuLookScalesWithSensitivity::RunTest(const FString& Parameters)
{
	using namespace LureDebugMenuTest;
	// (d) The same mouse delta through the mouse mapping built at 1x and at 2x, into the character's look handler.
	const UInputAction* Look = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Look);
	const TArray<FKey>& MouseKeys = GetDefault<ULureCharacterSettings>()->LookMouseKeys;
	const TArray<FKey>& StickKeys = GetDefault<ULureCharacterSettings>()->LookStickKeys;
	if (!TestNotNull(TEXT("Look action"), Look) || !TestTrue(TEXT("a mouse look key and a stick look key"), MouseKeys.Num() > 0 && StickKeys.Num() > 0))
	{
		return false;
	}
	const UInputMappingContext* At1 = BuildContext(1.f);
	const UInputMappingContext* At2 = BuildContext(2.f);
	const FVector MouseDelta(12.f, -5.f, 0.f);
	FVector Look1, Look2;
	if (!TestTrue(TEXT("the mouse look mapping exists at 1x and 2x"), ModifiedValue(At1, Look, MouseKeys[0], MouseDelta, Look1) && ModifiedValue(At2, Look, MouseKeys[0], MouseDelta, Look2)))
	{
		return false;
	}
	TestTrue(TEXT("1x: a real turn"), !FMath::IsNearlyZero(Look1.X) && !FMath::IsNearlyZero(Look1.Y));
	TestNearlyEqual(TEXT("2x doubles the yaw value"), Look2.X, 2.0 * Look1.X, 1.0e-4);
	TestNearlyEqual(TEXT("2x doubles the pitch value"), Look2.Y, 2.0 * Look1.Y, 1.0e-4);
	FVector Stick1, Stick2;
	if (ModifiedValue(At1, Look, StickKeys[0], FVector(0.8f, 0.5f, 0.f), Stick1) && ModifiedValue(At2, Look, StickKeys[0], FVector(0.8f, 0.5f, 0.f), Stick2))
	{
		TestTrue(TEXT("the gamepad stick is not scaled (mouse sensitivity only)"), Stick1.Equals(Stick2, 1.0e-5));
	}
	FVector BadValue;
	ModifiedValue(BuildContext(-3.f), Look, MouseKeys[0], MouseDelta, BadValue);
	TestTrue(TEXT("a bad sensitivity (<= 0) builds like 1x"), BadValue.Equals(Look1, 1.0e-5));

	// Into the character's look handler: 2x the yaw for the same mouse delta.
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
	{
		AddError(TEXT("the test world could not be created"));
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	FString Csv;
	if (!FFileHelper::LoadFileToString(Csv, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv"))))
	{
		AddError(TEXT("data/tables/DT_Movement.csv is missing"));
		return false;
	}
	TStrongObjectPtr<UDataTable> MovementTable(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
	MovementTable->RowStruct = FLureMovementRow::StaticStruct();
	MovementTable->CreateTableFromCSVString(Csv);
	const FTransform Transform(FRotator::ZeroRotator, FVector(0.f, 0.f, 200.f));
	ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("character"), Character) || !Character->GetLureMovement())
	{
		return false;
	}
	Character->GetLureMovement()->ApplyMovementTable(MovementTable.Get());
	Character->FinishSpawning(Transform);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	TestNull(TEXT("no local player here: no debug menu (and none on servers / remote controllers)"), ULureDebugMenu::Get(Controller));

	Controller->RotationInput = FRotator::ZeroRotator;
	Character->DoLook(Look1.X, Look1.Y);
	const FRotator Turn1 = Controller->RotationInput;
	Controller->RotationInput = FRotator::ZeroRotator;
	Character->DoLook(Look2.X, Look2.Y);
	const FRotator Turn2 = Controller->RotationInput;
	Controller->RotationInput = FRotator::ZeroRotator;
	TestTrue(TEXT("1x turns the view"), !FMath::IsNearlyZero(Turn1.Yaw));
	TestNearlyEqual(TEXT("2x sensitivity: 2x yaw for the same mouse delta"), Turn2.Yaw, 2.0 * Turn1.Yaw, 1.0e-4);
	TestNearlyEqual(TEXT("... and 2x pitch"), Turn2.Pitch, 2.0 * Turn1.Pitch, 1.0e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDebugMenuLocalOnly, "Project.Dev.DebugMenu.LocalOnly", LureDebugMenuTest::Flags)
bool FLureDebugMenuLocalOnly::RunTest(const FString& Parameters)
{
	using namespace LureDebugMenuTest;
	// The sensitivity and the menu live only in local objects: an engine subsystem and a local player subsystem, never
	// actors, with no replicated properties. Changing the sensitivity only rebuilds the local mapping context.
	TestFalse(TEXT("ULureUserSettings has no replicated property"), HasCppNetProperty(ULureUserSettings::StaticClass()));
	TestFalse(TEXT("ULureDebugMenu has no replicated property"), HasCppNetProperty(ULureDebugMenu::StaticClass()));
	TestTrue(TEXT("ULureUserSettings is an engine subsystem (one per machine)"), ULureUserSettings::StaticClass()->IsChildOf(UEngineSubsystem::StaticClass()));
	TestTrue(TEXT("ULureDebugMenu is a local player subsystem (client side only)"), ULureDebugMenu::StaticClass()->IsChildOf(ULocalPlayerSubsystem::StaticClass()));
	TestFalse(TEXT("ULureUserSettings is not an actor or component"), ULureUserSettings::StaticClass()->IsChildOf(AActor::StaticClass()) || ULureUserSettings::StaticClass()->IsChildOf(UActorComponent::StaticClass()));
	TestNull(TEXT("no controller: no menu"), ULureDebugMenu::Get(nullptr));
	TestTrue(TEXT("the menu actions are not gameplay actions (GetInputActionNames unchanged)"),
		!ULureInputSubsystem::GetInputActionNames().Contains(FLureInputActionNames::DebugMenu) && ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::DebugMenu) != nullptr);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

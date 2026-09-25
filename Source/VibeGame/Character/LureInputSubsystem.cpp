// Lure: runtime Enhanced Input actions and mapping context (T-004).

#include "Character/LureInputSubsystem.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureMovementTypes.h"
#include "Fishing/LureFishingSettings.h"
#include "Game/LureUserSettings.h"
#include "Engine/Engine.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputLibrary.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

const FName FLureInputActionNames::Move(TEXT("Move"));
const FName FLureInputActionNames::Look(TEXT("Look"));
const FName FLureInputActionNames::Jump(TEXT("Jump"));
const FName FLureInputActionNames::Interact(TEXT("Interact"));
const FName FLureInputActionNames::AltInteract(TEXT("AltInteract"));
const FName FLureInputActionNames::Sprint(TEXT("Sprint"));
const FName FLureInputActionNames::Crouch(TEXT("Crouch"));
const FName FLureInputActionNames::Prone(TEXT("Prone"));
const FName FLureInputActionNames::Cast(TEXT("Cast"));
const FName FLureInputActionNames::Hook(TEXT("Hook"));
const FName FLureInputActionNames::ReelFaster(TEXT("ReelFaster"));
const FName FLureInputActionNames::ReelSlower(TEXT("ReelSlower"));
const FName FLureInputActionNames::DebugMenu(TEXT("DebugMenu"));
const FName FLureInputActionNames::MouseSensitivityDown(TEXT("MouseSensitivityDown"));
const FName FLureInputActionNames::MouseSensitivityUp(TEXT("MouseSensitivityUp"));

TArray<FName> FLureInputActionNames::All()
{
	return { Move, Look, Jump, Sprint, Crouch, Prone, Interact, AltInteract, Cast, Hook, ReelFaster, ReelSlower };
}

TArray<FName> FLureInputActionNames::Menu()
{
	return { DebugMenu, MouseSensitivityDown, MouseSensitivityUp };
}

void ULureInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<ULureUserSettings>(); // T-051: the saved mouse sensitivity is loaded before the first mappings

	CreateActions();
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Lure_Default"), RF_Transient);
	BuildMappings(*MappingContext, Actions, *GetDefault<ULureCharacterSettings>(), GetCurrentMouseSensitivity());
}

void ULureInputSubsystem::Deinitialize()
{
	Actions.Reset();
	MappingContext = nullptr;
	Super::Deinitialize();
}

ULureInputSubsystem* ULureInputSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<ULureInputSubsystem>() : nullptr;
}

UInputAction* ULureInputSubsystem::GetInputActionByName(FName ActionName)
{
	const ULureInputSubsystem* Subsystem = Get();
	return Subsystem ? Subsystem->Actions.FindRef(ActionName).Get() : nullptr;
}

UInputMappingContext* ULureInputSubsystem::GetDefaultMappingContext()
{
	const ULureInputSubsystem* Subsystem = Get();
	return Subsystem ? Subsystem->MappingContext.Get() : nullptr;
}

TArray<FName> ULureInputSubsystem::GetInputActionNames()
{
	return FLureInputActionNames::All();
}

FName ULureInputSubsystem::GetInputActionName(const UInputAction* Action)
{
	const ULureInputSubsystem* Subsystem = Get();
	if (!Subsystem || !Action)
	{
		return NAME_None;
	}
	for (const TPair<FName, TObjectPtr<UInputAction>>& Pair : Subsystem->Actions)
	{
		if (Pair.Value == Action)
		{
			return Pair.Key;
		}
	}
	return NAME_None;
}

float ULureInputSubsystem::GetCurrentMouseSensitivity()
{
	const ULureUserSettings* UserSettings = ULureUserSettings::Get();
	return UserSettings ? UserSettings->GetMouseSensitivity() : 1.f;
}

void ULureInputSubsystem::CreateActions()
{
	struct FActionSpec
	{
		FName Name;
		EInputActionValueType Type;
	};
	const FActionSpec Specs[] = {
		{ FLureInputActionNames::Move, EInputActionValueType::Axis2D },
		{ FLureInputActionNames::Look, EInputActionValueType::Axis2D },
		{ FLureInputActionNames::Jump, EInputActionValueType::Boolean },
		{ FLureInputActionNames::Interact, EInputActionValueType::Boolean },
		{ FLureInputActionNames::AltInteract, EInputActionValueType::Boolean },
		{ FLureInputActionNames::Sprint, EInputActionValueType::Boolean },
		{ FLureInputActionNames::Crouch, EInputActionValueType::Boolean },
		{ FLureInputActionNames::Prone, EInputActionValueType::Boolean },
		{ FLureInputActionNames::Cast, EInputActionValueType::Boolean },
		{ FLureInputActionNames::Hook, EInputActionValueType::Boolean },
		{ FLureInputActionNames::ReelFaster, EInputActionValueType::Boolean },
		{ FLureInputActionNames::ReelSlower, EInputActionValueType::Boolean },
		{ FLureInputActionNames::DebugMenu, EInputActionValueType::Boolean }, // T-051
		{ FLureInputActionNames::MouseSensitivityDown, EInputActionValueType::Boolean },
		{ FLureInputActionNames::MouseSensitivityUp, EInputActionValueType::Boolean },
	};

	Actions.Reset();
	for (const FActionSpec& Spec : Specs)
	{
		UInputAction* Action = NewObject<UInputAction>(this, *FString::Printf(TEXT("IA_Lure_%s"), *Spec.Name.ToString()), RF_Transient);
		Action->ValueType = Spec.Type;
		Actions.Add(Spec.Name, Action);
	}
}

void ULureInputSubsystem::RebuildMappings()
{
	if (!MappingContext)
	{
		return;
	}
	MappingContext->UnmapAll();
	BuildMappings(*MappingContext, Actions, *GetDefault<ULureCharacterSettings>(), GetCurrentMouseSensitivity());
	UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(MappingContext);
}

void ULureInputSubsystem::BuildMappings(UInputMappingContext& Context, const TMap<FName, TObjectPtr<UInputAction>>& InActions, const ULureCharacterSettings& Settings, float MouseSensitivity)
{
	auto MakeSwizzle = [&Context]()
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(&Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;	// a 1D key drives Y (forward)
		return Swizzle;
	};
	auto MakeNegate = [&Context]()
	{
		return NewObject<UInputModifierNegate>(&Context);
	};
	auto MakeDeadZone = [&Context](float LowerThreshold)
	{
		UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(&Context);
		DeadZone->LowerThreshold = FMath::Clamp(LowerThreshold, 0.f, 0.9f);
		DeadZone->UpperThreshold = 1.f;
		return DeadZone;
	};
	auto MakeScalar = [&Context](const FVector& Scale)
	{
		UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(&Context);
		Scalar->Scalar = Scale;
		return Scalar;
	};

	auto Map = [&Context, &InActions](FName ActionName, const TArray<FKey>& Keys, TFunctionRef<TArray<UInputModifier*>()> MakeModifiers)
	{
		const UInputAction* Action = InActions.FindRef(ActionName).Get();
		if (!Action)
		{
			return;
		}
		for (const FKey& Key : Keys)
		{
			if (!Key.IsValid())
			{
				UE_LOG(LogLureMovement, Warning, TEXT("Input: key '%s' for %s is not a valid key name; skipped."), *Key.GetFName().ToString(), *ActionName.ToString());
				continue;
			}
			FEnhancedActionKeyMapping& Mapping = Context.MapKey(Action, Key);
			for (UInputModifier* Modifier : MakeModifiers())
			{
				Mapping.Modifiers.Add(Modifier);
			}
		}
	};

	const float PitchSign = Settings.bInvertLookY ? -1.f : 1.f;
	// T-051: the player's mouse sensitivity multiplies the mouse look scale (both axes); a bad value falls back to 1.
	const float Sensitivity = FMath::IsFinite(MouseSensitivity) && MouseSensitivity > 0.f ? MouseSensitivity : 1.f;
	const float MouseScale = Settings.MouseDegreesPerCount * Sensitivity;

	// Move: 1D keys become a 2D vector (X = right, Y = forward).
	Map(FLureInputActionNames::Move, Settings.MoveForwardKeys, [&]() { return TArray<UInputModifier*>{ MakeSwizzle() }; });
	Map(FLureInputActionNames::Move, Settings.MoveBackwardKeys, [&]() { return TArray<UInputModifier*>{ MakeSwizzle(), MakeNegate() }; });
	Map(FLureInputActionNames::Move, Settings.MoveLeftKeys, [&]() { return TArray<UInputModifier*>{ MakeNegate() }; });
	Map(FLureInputActionNames::Move, Settings.MoveRightKeys, [&]() { return TArray<UInputModifier*>{}; });
	Map(FLureInputActionNames::Move, Settings.MoveStickKeys, [&]() { return TArray<UInputModifier*>{ MakeDeadZone(Settings.GamepadMoveDeadZone) }; });

	// Look: the action value is degrees this frame (mouse counts x degrees per count; stick x degrees per second x delta time).
	Map(FLureInputActionNames::Look, Settings.LookMouseKeys, [&]() { return TArray<UInputModifier*>{ MakeScalar(FVector(MouseScale, MouseScale * PitchSign, 1.f)) }; });
	Map(FLureInputActionNames::Look, Settings.LookStickKeys, [&]()
	{
		return TArray<UInputModifier*>{
			MakeDeadZone(Settings.GamepadLookDeadZone),
			MakeScalar(FVector(Settings.GamepadLookRate.X, Settings.GamepadLookRate.Y * PitchSign, 1.f)),
			NewObject<UInputModifierScaleByDeltaTime>(&Context) };
	});

	// Buttons.
	auto NoModifiers = []() { return TArray<UInputModifier*>{}; };
	Map(FLureInputActionNames::Jump, Settings.JumpKeys, NoModifiers);
	Map(FLureInputActionNames::Interact, Settings.InteractKeys, NoModifiers); // T-010
	Map(FLureInputActionNames::AltInteract, Settings.AltInteractKeys, NoModifiers); // T-030
	Map(FLureInputActionNames::Sprint, Settings.SprintKeys, NoModifiers);
	Map(FLureInputActionNames::Crouch, Settings.CrouchKeys, NoModifiers);
	Map(FLureInputActionNames::Prone, Settings.ProneKeys, NoModifiers);

	// Fishing (T-006): keys live in the fishing settings.
	const ULureFishingSettings* Fishing = GetDefault<ULureFishingSettings>();
	Map(FLureInputActionNames::Cast, Fishing->CastKeys, NoModifiers);
	Map(FLureInputActionNames::Hook, Fishing->HookKeys, NoModifiers);
	// Reel speed steps while a fish is on (T-028); the look input steers the rod then (ALurePlayerCharacter::DoLook).
	Map(FLureInputActionNames::ReelFaster, Fishing->ReelFasterKeys, NoModifiers);
	Map(FLureInputActionNames::ReelSlower, Fishing->ReelSlowerKeys, NoModifiers);

	// Debug menu (T-051): open / close, and the sensitivity keys it uses while open.
	Map(FLureInputActionNames::DebugMenu, Settings.DebugMenuKeys, NoModifiers);
	Map(FLureInputActionNames::MouseSensitivityDown, Settings.MouseSensitivityDownKeys, NoModifiers);
	Map(FLureInputActionNames::MouseSensitivityUp, Settings.MouseSensitivityUpKeys, NoModifiers);
}

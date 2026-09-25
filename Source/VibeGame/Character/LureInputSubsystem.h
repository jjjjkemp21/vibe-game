// Lure: runtime Enhanced Input actions and mapping context (T-004).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "LureInputSubsystem.generated.h"

class UInputAction;
class UInputMappingContext;
class ULureCharacterSettings;

/** Names of Lure's input actions (GetInputActionByName). */
struct FLureInputActionNames
{
	static const FName Move;	// Axis2D: X = right, Y = forward
	static const FName Look;	// Axis2D: degrees this frame, X = yaw right, Y = pitch up
	static const FName Jump;	// Boolean
	static const FName Interact;	// Boolean: the main verb on what you look at (T-010, T-030; later NPCs)
	static const FName AltInteract;	// Boolean: the second verb (T-030: pick up / put down the cooler, close it, drop a fish)
	static const FName Sprint;	// Boolean
	static const FName Crouch;	// Boolean
	static const FName Prone;	// Boolean
	static const FName Cast;	// Boolean: the fishing button (T-006): hold + release casts; a press while the line is out hooks
	static const FName Hook;	// Boolean: hook only (no default key; the Cast button hooks too)
	static const FName ReelFaster;	// Boolean: T-028, one reel speed step faster while a fish is on (mouse wheel up, right bumper)
	static const FName ReelSlower;	// Boolean: T-028, one reel speed step slower (mouse wheel down, left bumper)

	// T-051 debug menu actions (not gameplay: listed by Menu(), not All() / GetInputActionNames()).
	static const FName DebugMenu;	// Boolean: open / close the debug menu (F6)
	static const FName MouseSensitivityDown;	// Boolean: one sensitivity step lower while the menu is open (- / numpad -)
	static const FName MouseSensitivityUp;	// Boolean: one sensitivity step higher while the menu is open (= / numpad +)

	/** The gameplay actions. */
	static TArray<FName> All();

	/** The debug menu's own actions (T-051). */
	static TArray<FName> Menu();
};

/**
 *  Owns Lure's input actions and default mapping context. They are created in C++ at engine start (no .uasset),
 *  live as long as the engine, and are shared by every local player and every ALurePlayerCharacter, so a lookup by
 *  name always returns the same object the mapping context maps and the character binds.
 *  Keys come from ULureCharacterSettings (DefaultGame.ini).
 *
 *  Python (playtester / T-025):
 *    action = unreal.LureInputSubsystem.get_input_action_by_name("Sprint")
 *    then inject it with the local player's EnhancedInputLocalPlayerSubsystem (inject_input_for_action or
 *    start_continuous_input_injection_for_action). Look values are degrees per frame; Move values are -1..1.
 */
UCLASS()
class ULureInputSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The engine's instance (null before engine init or after shutdown). */
	static ULureInputSubsystem* Get();

	/** The input action for Move, Look, Jump, Sprint, Crouch, Prone, Interact, AltInteract, Cast, Hook, ReelFaster, ReelSlower
	 *  or a debug menu action (DebugMenu, MouseSensitivityDown, MouseSensitivityUp); null for any other name. Same object every call. */
	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	static UInputAction* GetInputActionByName(FName ActionName);

	/** The default mapping context (added to each local player by ALurePlayerCharacter when possessed). */
	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	static UInputMappingContext* GetDefaultMappingContext();

	/** The name Action is registered under (reverse of GetInputActionByName); NAME_None if it is not one of the Lure actions. */
	static FName GetInputActionName(const UInputAction* Action);

	/** The gameplay action names (the debug menu actions are FLureInputActionNames::Menu()). */
	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	static TArray<FName> GetInputActionNames();

	/** Rebuilds the key mappings from ULureCharacterSettings (same action objects) and refreshes active players. */
	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	void RebuildMappings();

	/** Fills Context with the key mappings Settings describes for Actions (keys + modifiers). Used by RebuildMappings; public for tests.
	 *  MouseSensitivity (T-051) multiplies the mouse look scale on both axes (the gamepad stick is not scaled). */
	static void BuildMappings(UInputMappingContext& Context, const TMap<FName, TObjectPtr<UInputAction>>& InActions, const ULureCharacterSettings& Settings, float MouseSensitivity = 1.f);

	/** The player's saved mouse sensitivity (ULureUserSettings), or 1 when it is not running. */
	static float GetCurrentMouseSensitivity();

	/** Priority the character uses when it adds the mapping context. */
	static constexpr int32 MappingPriority = 0;

private:

	void CreateActions();

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UInputAction>> Actions;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;
};

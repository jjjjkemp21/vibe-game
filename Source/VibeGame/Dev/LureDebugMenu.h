// Lure: the debug menu (T-051): every key bind plus the mouse sensitivity, as plain text drawn by ALureHUD.
// Placeholder UI on purpose (CLAUDE.md, 2026-09-22): Jimmy will direct a real settings UI later.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "LureDebugMenu.generated.h"

class APlayerController;
class UEnhancedInputComponent;
class UInputMappingContext;
struct FLureMouseSensitivityTuning;

/**
 *  One per local player (client side only, nothing replicates). F6 (ULureCharacterSettings::DebugMenuKeys) opens and
 *  closes it; while open, the sensitivity keys (- / =) step the mouse sensitivity (ULureUserSettings, saved per machine).
 *  All other input keeps working while it is open, so the new sensitivity can be felt right away.
 *  The bind list is generated from the live mapping context (ULureInputSubsystem) plus the keys handled outside it
 *  (F8 playtest note, the console key), so a new bind shows up without touching the menu.
 *
 *  Python (playtester): unreal.SubsystemBlueprintLibrary.get_local_player_subsystem(pc, unreal.LureDebugMenu)
 *  then set_open(True), get_lines(), adjust_mouse_sensitivity(1).
 */
UCLASS()
class ULureDebugMenu : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:

	/** The menu of PlayerController's local player (null for a remote or server-side controller). */
	static ULureDebugMenu* Get(const APlayerController* PlayerController);

	UFUNCTION(BlueprintPure, Category="Lure|Debug Menu")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintCallable, Category="Lure|Debug Menu")
	void SetOpen(bool bInOpen);

	UFUNCTION(BlueprintCallable, Category="Lure|Debug Menu")
	void Toggle() { SetOpen(!bOpen); }

	/** Steps the mouse sensitivity by Steps (negative = lower) while the menu is open (does nothing when closed). Returns the value in use. */
	UFUNCTION(BlueprintCallable, Category="Lure|Debug Menu")
	float AdjustMouseSensitivity(int32 Steps);

	/** The menu text, one entry per line (from the live mapping context and the saved sensitivity). */
	UFUNCTION(BlueprintCallable, Category="Lure|Debug Menu")
	TArray<FString> GetLines() const;

	/** The one-line hint drawn while the menu is closed ("F6: debug menu ..."). */
	UFUNCTION(BlueprintCallable, Category="Lure|Debug Menu")
	static FString GetHintLine();

	/** Binds the menu actions (toggle, sensitivity down / up) on Input to PlayerController's menu. Called by ALurePlayerCharacter. */
	static void BindInput(UEnhancedInputComponent& Input, const APlayerController* PlayerController);

	/** The menu lines for Context and a sensitivity: title, sensitivity line, one line per action ("Name: keys   pad: keys"), then the keys handled outside the context. Public for tests. */
	static TArray<FString> BuildLines(const UInputMappingContext* Context, float Sensitivity, const FLureMouseSensitivityTuning& Tuning);

	/** "Action name: keys" lines for every action in Context (Lure actions in their usual order first, then any other mapped action). */
	static TArray<FString> BuildBindLines(const UInputMappingContext* Context);

	/** Keys handled outside the mapping context, as "Name: key" lines (F8 playtest note, console key; editor play: Shift+F1 frees the mouse). */
	static TArray<FString> BuildExtraBindLines();

	/** The keys Context maps to ActionName, comma separated ("" if none). */
	static FString DescribeKeys(const UInputMappingContext* Context, FName ActionName, bool bGamepad);

	/** How a key reads in the menu (its short display name). */
	static FString KeyText(const FKey& Key);

	/** How an action name reads in the menu ("ReelFaster" -> "Reel Faster"). */
	static FString ActionText(FName ActionName);

private:

	void HandleToggle() { Toggle(); }
	void HandleSensitivityDown() { AdjustMouseSensitivity(-1); }
	void HandleSensitivityUp() { AdjustMouseSensitivity(1); }

	bool bOpen = false;
};

// Lure: placeholder HUD (T-006). Plain text only until Jimmy directs the UI (CLAUDE.md, 2026-09-22).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LureHUD.generated.h"

/**
 *  Draws the local player's status lines (fishing: cast power, bite/hook prompt, results; a refused stance: "Too deep
 *  to crouch here.") as plain white text,
 *  centered in the lower part of the screen. No styling on purpose.
 */
UCLASS()
class ALureHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

	/** The text lines drawn this frame for Pawn (empty = nothing). Public for tests and the playtester. */
	static FString GetStatusText(const APawn* Pawn);
};

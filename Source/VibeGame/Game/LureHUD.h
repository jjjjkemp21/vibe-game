// Lure: placeholder HUD (T-006). Plain text only until Jimmy directs the UI (CLAUDE.md, 2026-09-22).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LureHUD.generated.h"

/**
 *  Draws the local player's status lines as plain white text: progression (money, level, XP, cooler, the interact
 *  prompt) top-left, with the short notices ("Level up! Level 2", "Sold 2 fish for 48 coins") right under them; fishing
 *  (cast power, bite/hook prompt, fight, results) and a refused stance ("Too deep to crouch here.") in the lower-left,
 *  so the centre of the view (line, bobber, water) stays clear. No styling on purpose.
 */
UCLASS()
class ALureHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

	/** The text lines drawn this frame for Pawn (empty = nothing). Public for tests and the playtester. */
	static FString GetStatusText(const APawn* Pawn);

	/** The notices still showing for PlayerController's own player (ULureProgressionComponent::GetNoticeLines) */
	static TArray<FString> GetNoticeLines(const APlayerController* PlayerController);

	/** Screen margin (px) of the top-left and lower-left text blocks */
	static constexpr float Margin = 24.f;
	/** The lower-left block's last line ends this far (px) above the bottom edge */
	static constexpr float BottomMargin = 48.f;
};

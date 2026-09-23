// Lure: placeholder HUD (T-006; stance hint T-026).

#include "Game/LureHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Character/LurePlayerCharacter.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/Pawn.h"

FString ALureHUD::GetStatusText(const APawn* Pawn)
{
	TArray<FString> Lines;
	const ULureFishingComponent* Fishing = Pawn ? Pawn->FindComponentByClass<ULureFishingComponent>() : nullptr;
	if (Fishing)
	{
		const FString FishingText = Fishing->GetStatusText();
		if (!FishingText.IsEmpty())
		{
			Lines.Add(FishingText);
		}
	}
	// A refused crouch/prone (T-026 B2): "Too deep to crouch here."
	if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(Pawn))
	{
		const FString StanceHint = Lure->GetStanceHintText();
		if (!StanceHint.IsEmpty())
		{
			Lines.Add(StanceHint);
		}
	}
	return FString::Join(Lines, TEXT("\n"));
}

void ALureHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GEngine)
	{
		return;
	}
	const FString Text = GetStatusText(GetOwningPawn());
	if (Text.IsEmpty())
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
	float Y = Canvas->ClipY * 0.68f;
	for (const FString& Line : Lines)
	{
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Line, Width, Height, Font);
		DrawText(Line, FLinearColor::White, (Canvas->ClipX - Width) * 0.5f, Y, Font);
		Y += Height + 4.f;
	}
}

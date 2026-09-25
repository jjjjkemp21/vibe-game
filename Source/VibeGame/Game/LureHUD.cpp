// Lure: placeholder HUD (T-006; stance hint T-026; notices and lower-left layout after the fishing-loop playtest).

#include "Game/LureHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Character/LurePlayerCharacter.h"
#include "Dev/LureDebugMenu.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionSettings.h"

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

TArray<FString> ALureHUD::GetNoticeLines(const APlayerController* PlayerController)
{
	const ULureProgressionComponent* Progression = PlayerController ? ULureProgressionLibrary::GetProgression(PlayerController) : nullptr;
	return Progression ? Progression->GetNoticeLines() : TArray<FString>();
}

void ALureHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GEngine)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();

	DrawDebugMenu(); // T-051, top-right

	// Progression (T-010): money, level, XP, cooler and the interact prompt, top-left; the notices right under them.
	float TopY = Margin;
	auto DrawTopLeft = [this, Font, &TopY](const FString& Line)
	{
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Line, Width, Height, Font);
		DrawText(Line, FLinearColor::White, Margin, TopY, Font);
		TopY += Height + 4.f;
	};
	if (GetDefault<ULureProgressionSettings>()->bShowPlaceholderText)
	{
		for (const FString& Line : ULureProgressionLibrary::GetPlaceholderStatusLines(GetOwningPlayerController()))
		{
			DrawTopLeft(Line);
		}
	}
	const TArray<FString> Notices = GetNoticeLines(GetOwningPlayerController());
	if (Notices.Num() > 0)
	{
		TopY += 8.f; // a gap under the status block
		for (const FString& Line : Notices)
		{
			DrawTopLeft(Line);
		}
	}

	// Fishing (T-006/T-007) and the stance hint: lower-left, bottom-anchored, clear of the centre of the view.
	const FString Text = GetStatusText(GetOwningPawn());
	if (Text.IsEmpty())
	{
		return;
	}
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
	TArray<float> Heights;
	float BlockHeight = 0.f;
	for (const FString& Line : Lines)
	{
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Line, Width, Height, Font);
		Heights.Add(Height);
		BlockHeight += Height + 4.f;
	}
	// Never above the top-left block, even on a very short screen.
	float Y = FMath::Max(TopY + 8.f, Canvas->ClipY - BottomMargin - BlockHeight);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawText(Lines[Index], FLinearColor::White, Margin, Y, Font);
		Y += Heights[Index] + 4.f;
	}
}

void ALureHUD::DrawDebugMenu()
{
	const ULureDebugMenu* Menu = ULureDebugMenu::Get(GetOwningPlayerController());
	if (!Menu || !Canvas || !GEngine)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	if (!Menu->IsOpen())
	{
		const FString Hint = ULureDebugMenu::GetHintLine();
		if (!Hint.IsEmpty())
		{
			float Width = 0.f;
			float Height = 0.f;
			GetTextSize(Hint, Width, Height, Font);
			DrawText(Hint, FLinearColor(1.f, 1.f, 1.f, 0.7f), FMath::Max(Margin, Canvas->ClipX - Margin - Width), Margin, Font);
		}
		return;
	}

	// Plain placeholder: a dark box with white lines (no styling until Jimmy directs the UI).
	const TArray<FString> Lines = Menu->GetLines();
	constexpr float Padding = 10.f;
	constexpr float LineGap = 2.f;
	float BoxWidth = 0.f;
	float BoxHeight = 0.f;
	TArray<float> Heights;
	for (const FString& Line : Lines)
	{
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Line.IsEmpty() ? FString(TEXT(" ")) : Line, Width, Height, Font);
		Heights.Add(Height);
		BoxWidth = FMath::Max(BoxWidth, Width);
		BoxHeight += Height + LineGap;
	}
	BoxWidth += 2.f * Padding;
	BoxHeight += 2.f * Padding;
	const float X = FMath::Max(Margin, Canvas->ClipX - Margin - BoxWidth);
	const float Y = Margin;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.75f), X, Y, BoxWidth, BoxHeight);
	float LineY = Y + Padding;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawText(Lines[Index], FLinearColor::White, X + Padding, LineY, Font);
		LineY += Heights[Index] + LineGap;
	}
}

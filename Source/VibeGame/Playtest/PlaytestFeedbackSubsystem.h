// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "PlaytestFeedbackSubsystem.generated.h"

class FPlaytestFeedbackInputProcessor;
class FPlaytestFpsTracker;
class SPlaytestNoteWidget;
struct FKeyEvent;

/**
 *  Playtest feedback key (development builds and PIE only; does nothing in Shipping).
 *  Press the feedback key (F8) during play: a screenshot is captured, the game pauses and a
 *  one-line note box appears. Enter saves Saved/Playtest/<yyyyMMdd-HHmmss>/ with screenshot.png
 *  and note.json, Escape cancels. The game resumes either way.
 *  Input is read through a Slate input preprocessor, so it works with any pawn or player controller.
 *  Settings live in [/Script/VibeGame.PlaytestFeedbackSubsystem] in DefaultGame.ini.
 */
UCLASS(Config=Game)
class UPlaytestFeedbackSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	UPlaytestFeedbackSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Key that opens the note box */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Playtest")
	FKey FeedbackKey;

	/** Window for the average FPS written to the note, in seconds */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Playtest", meta=(ClampMin="0.5"))
	float FpsWindowSeconds = 5.0f;

	/** How long to wait for the screenshot before opening the note box without one, in seconds */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Playtest", meta=(ClampMin="0.1"))
	float ScreenshotTimeoutSeconds = 1.0f;

	/** Pause the game while the note box is open (never pauses on network clients) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Playtest")
	bool bPauseWhileTyping = true;

	/** Starts the feedback flow (screenshot, pause, note box). Returns false if it could not start. */
	UFUNCTION(BlueprintCallable, Category="Playtest")
	bool BeginFeedback();

	/** True while capturing or while the note box is open */
	UFUNCTION(BlueprintPure, Category="Playtest")
	bool IsFeedbackActive() const { return State != EFeedbackState::Idle; }

	/** Called by the input preprocessor. Returns true if the key was consumed. */
	bool HandleKeyDown(const FKeyEvent& InKeyEvent);

protected:

	enum class EFeedbackState : uint8
	{
		Idle,
		Capturing,
		Typing
	};

	EFeedbackState State = EFeedbackState::Idle;

	bool Tick(float DeltaTime);

	/** True if this game instance's viewport currently has keyboard focus */
	bool IsGameViewportFocused() const;

	void CaptureContext();
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
	void OnHDRScreenshotCaptured(int32 Width, int32 Height, const TArray<FLinearColor>& Colors);
	void UnbindScreenshotDelegates();
	void OpenNoteBox();
	void SubmitNote();
	void CloseNoteBox();

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle ScreenshotHandle;
	FDelegateHandle HDRScreenshotHandle;

	TSharedPtr<FPlaytestFeedbackInputProcessor> InputProcessor;
	TSharedPtr<FPlaytestFpsTracker> FpsTracker;
	TSharedPtr<SPlaytestNoteWidget> NoteWidget;

	/** Context captured at the moment the key was pressed (opaque, see .cpp) */
	TSharedPtr<struct FPlaytestNoteData> PendingNote;

	TArray<FColor> PendingPixels;
	int32 PendingWidth = 0;
	int32 PendingHeight = 0;

	double CaptureStartTime = 0.0;
	bool bPausedByUs = false;
	bool bPrevShowMouseCursor = false;
	FString CachedCommit;
};

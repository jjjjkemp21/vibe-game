// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "PlaytestFeedbackSubsystem.generated.h"

class FPlaytestFpsTracker;
class SPlaytestNoteWidget;
class SWidget;
struct FKeyEvent;

/**
 *  Playtest feedback key (development builds and PIE only; compiled out of Shipping).
 *
 *  Press the feedback key (F8) during play: a screenshot is captured, the game pauses (never on network clients) and a
 *  note box appears at the bottom center of the screen (lower third). It has a 3-line wrapping text field:
 *  Enter saves (never inserts a new line), Shift+Enter adds a new line, Escape cancels. The game resumes either way.
 *  Enter writes Saved/Playtest/<yyyyMMdd-HHmmss>/ (local time) with screenshot.png and note.json (text, level, pawn
 *  location/rotation, camera location/rotation, game time, average FPS, commit, UTC "timestamp"), then shows a toast
 *  where the box was ("Note saved. Thanks!" for 7 s, or the failure reason for 12 s).
 *
 *  Empty text + Enter still saves the folder: a screenshot-only bookmark ("Screenshot saved (no text)"). Lead decision.
 *
 *  F8 capture (see FPlaytestFeedbackRules for the pure rules and their tests):
 *  - F8 opens the note whenever this game instance's PIE session or standalone game is running, the player is not
 *    ejected (PIE switched to simulate: then F8 is the editor's "Possess or Eject Player" again) and keyboard focus is
 *    not in an editable text widget (or the game console). Viewport focus is NOT required: if the game viewport does
 *    not have keyboard focus (right after Play or Alt+P), it is focused first, then the note opens.
 *  - The key is read by one shared Slate input preprocessor, which runs before any widget or editor command, and a
 *    captured F8 is always consumed, so the editor's F8 never runs.
 *  - With several PIE instances (clients / listen server), exactly one handles F8: an instance already showing its
 *    note box; else the one whose viewport has keyboard focus; else the one that had it most recently; else the primary
 *    PIE instance; else the lowest PIE instance number.
 *
 *  If PIE or the game ends while the note box is open with text in it, the note is saved during teardown (with the
 *  screenshot already captured; without it when the whole application is exiting) and logged at Warning.
 *  All key handling runs on the game thread (Slate input).
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

	/** How long the "Note saved" toast stays on screen (real time, fades out over the last second) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Playtest", meta=(ClampMin="1.0"))
	float ToastSeconds = 7.0f;

	/** How long the "Note NOT saved" toast stays on screen (real time, fades out over the last second) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Playtest", meta=(ClampMin="1.0"))
	float FailureToastSeconds = 12.0f;

	/** Starts the feedback flow (screenshot, pause, note box). Returns false if it could not start. */
	UFUNCTION(BlueprintCallable, Category="Playtest")
	bool BeginFeedback();

	/** True while capturing or while the note box is open */
	UFUNCTION(BlueprintPure, Category="Playtest")
	bool IsFeedbackActive() const { return State != EFeedbackState::Idle; }

	/**
	 *  Called by the shared input preprocessor for every key down (game thread). Routes the key to exactly one instance:
	 *  the one with an open note box, else the one chosen by FPlaytestFeedbackRules::ChooseHandler. Returns true if consumed.
	 */
	static bool RouteKeyDown(const FKeyEvent& InKeyEvent);

	/** Handles a key as the chosen instance (see RouteKeyDown). Returns true if the key was consumed. */
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

	/** True if this game instance's viewport (or a widget inside it) currently has keyboard focus */
	bool IsGameViewportFocused() const;

	/** This game instance's PIE session or standalone game is running (game world that has begun play, has a viewport) */
	bool IsSessionRunning() const;

	/** Editor: the player is ejected (this PIE viewport is simulating) */
	bool IsEjected() const;

	/** Keyboard focus of that Slate user is in an editable text widget, or the game console is open */
	bool IsTypingInText(uint32 UserIndex) const;

	/** Gives this game instance's viewport the keyboard focus (and activates its window), like clicking into it */
	void FocusGameViewport();

	void CaptureContext();
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
	void OnHDRScreenshotCaptured(int32 Width, int32 Height, const TArray<FLinearColor>& Colors);
	void UnbindScreenshotDelegates();
	void OpenNoteBox();
	void SubmitNote();
	void CloseNoteBox();

	/** Shows the confirmation toast where the note box was (replaces any toast still showing) */
	void ShowToast(const FString& Title, const FString& Detail, bool bFailure);
	void HideToast();

	/** Deinitialize: saves a typed note if the box is still open (file IO only, never touches the world) */
	void SaveNoteOnTeardown();

	/** Deinitialize: removes our widgets from the viewport without touching the world or player controller */
	void RemoveWidgetsForTeardown();

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle ScreenshotHandle;
	FDelegateHandle HDRScreenshotHandle;

	TSharedPtr<FPlaytestFpsTracker> FpsTracker;
	TSharedPtr<SPlaytestNoteWidget> NoteWidget;
	TSharedPtr<SWidget> ToastWidget;

	/** Context captured at the moment the key was pressed (opaque, see .cpp) */
	TSharedPtr<struct FPlaytestNoteData> PendingNote;

	TArray<FColor> PendingPixels;
	int32 PendingWidth = 0;
	int32 PendingHeight = 0;

	double CaptureStartTime = 0.0;
	double ToastEndTime = 0.0;

	/** When this viewport last had keyboard focus (shared counter across instances; 0 = never) */
	uint64 LastFocusSerial = 0;

	bool bPausedByUs = false;
	bool bPrevShowMouseCursor = false;
	FString CachedCommit;
};

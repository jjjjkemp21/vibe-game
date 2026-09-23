// Copyright Epic Games, Inc. All Rights Reserved.

#include "Playtest/PlaytestFeedbackSubsystem.h"
#include "Playtest/PlaytestNoteWriter.h"
#include "VibeGame.h"

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SViewport.h"
#include "Styling/CoreStyle.h"

/** Minimal one-line note box, built in C++ with Slate */
class SPlaytestNoteWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SPlaytestNoteWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(900.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.85f))
				.Padding(16.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.ColorAndOpacity(FLinearColor::White)
						.Text(NSLOCTEXT("PlaytestFeedback", "Title", "Playtest note  (Enter = save, Esc = cancel)"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(TextBox, SEditableTextBox)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
						.HintText(NSLOCTEXT("PlaytestFeedback", "Hint", "What did you notice?"))
						.MaximumLength(1000)
					]
				]
			]
		];
	}

	FString GetNoteText() const
	{
		return TextBox.IsValid() ? TextBox->GetText().ToString() : FString();
	}

	TSharedPtr<SWidget> GetFocusTarget() const
	{
		return TextBox;
	}

private:

	TSharedPtr<SEditableTextBox> TextBox;
};

/** Slate input preprocessor: sees keys before any widget or editor command, independent of pawn/controller */
class FPlaytestFeedbackInputProcessor : public IInputProcessor
{
public:

	explicit FPlaytestFeedbackInputProcessor(UPlaytestFeedbackSubsystem* InOwner)
		: Owner(InOwner)
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		UPlaytestFeedbackSubsystem* Subsystem = Owner.Get();
		return Subsystem ? Subsystem->HandleKeyDown(InKeyEvent) : false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("PlaytestFeedback"); }

private:

	TWeakObjectPtr<UPlaytestFeedbackSubsystem> Owner;
};

#endif // VIBEGAME_WITH_PLAYTEST_FEEDBACK

UPlaytestFeedbackSubsystem::UPlaytestFeedbackSubsystem()
{
	FeedbackKey = EKeys::F8;
}

bool UPlaytestFeedbackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer() && !IsRunningCommandlet();
#else
	return false;
#endif
}

void UPlaytestFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	FpsTracker = MakeShared<FPlaytestFpsTracker>(FpsWindowSeconds);
	CachedCommit = FPlaytestNoteWriter::ReadGitCommit(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPlaytestFeedbackSubsystem::Tick));

	if (FSlateApplication::IsInitialized())
	{
		InputProcessor = MakeShared<FPlaytestFeedbackInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}

	UE_LOG(LogVibeGame, Log, TEXT("Playtest feedback ready: press %s during play (commit %s)"), *FeedbackKey.ToString(), *CachedCommit);
#endif
}

void UPlaytestFeedbackSubsystem::Deinitialize()
{
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	UnbindScreenshotDelegates();

	if (NoteWidget.IsValid())
	{
		CloseNoteBox();
	}

	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();

	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();

	State = EFeedbackState::Idle;
#endif

	Super::Deinitialize();
}

bool UPlaytestFeedbackSubsystem::HandleKeyDown(const FKeyEvent& InKeyEvent)
{
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	const FKey Key = InKeyEvent.GetKey();

	switch (State)
	{
	case EFeedbackState::Idle:
		if (Key == FeedbackKey && !InKeyEvent.IsRepeat() && IsGameViewportFocused())
		{
			return BeginFeedback();
		}
		return false;

	case EFeedbackState::Capturing:
		// swallow the feedback key while the screenshot is pending
		return Key == FeedbackKey;

	case EFeedbackState::Typing:
		if (Key == EKeys::Enter)
		{
			SubmitNote();
			return true;
		}
		if (Key == EKeys::Escape)
		{
			// consumed here so Escape does not also stop PIE
			UE_LOG(LogVibeGame, Log, TEXT("Playtest note cancelled"));
			CloseNoteBox();
			return true;
		}
		return Key == FeedbackKey;
	}
#endif
	return false;
}

bool UPlaytestFeedbackSubsystem::BeginFeedback()
{
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	UGameInstance* GameInstance = GetGameInstance();
	if (State != EFeedbackState::Idle || !GameInstance || !GameInstance->GetGameViewportClient() || !GameInstance->GetWorld())
	{
		return false;
	}

	CaptureContext();

	// screenshot first (scene only, no UI), delivered in memory through the viewport delegate
	PendingPixels.Reset();
	PendingWidth = PendingHeight = 0;
	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UPlaytestFeedbackSubsystem::OnScreenshotCaptured);
	HDRScreenshotHandle = UGameViewportClient::OnHDRScreenshotCaptured().AddUObject(this, &UPlaytestFeedbackSubsystem::OnHDRScreenshotCaptured);
	FScreenshotRequest::RequestScreenshot(/*bInShowUI*/ false, /*bInRestrictToGameViewport*/ true);

	CaptureStartTime = FPlatformTime::Seconds();
	State = EFeedbackState::Capturing;
	return true;
#else
	return false;
#endif
}

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

bool UPlaytestFeedbackSubsystem::Tick(float DeltaTime)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;

	// rolling FPS over real frame time, only while actually playing
	if (FpsTracker.IsValid() && World && State == EFeedbackState::Idle && !World->IsPaused())
	{
		FpsTracker->SetWindowSeconds(FpsWindowSeconds);
		FpsTracker->AddFrame(FPlatformTime::Seconds(), FApp::GetDeltaTime());
	}

	// screenshot never arrived: open the note box without it
	if (State == EFeedbackState::Capturing && FPlatformTime::Seconds() - CaptureStartTime > ScreenshotTimeoutSeconds)
	{
		UE_LOG(LogVibeGame, Warning, TEXT("Playtest screenshot timed out; the note will be saved without screenshot.png"));
		UnbindScreenshotDelegates();
		OpenNoteBox();
	}

	return true;
}

bool UPlaytestFeedbackSubsystem::IsGameViewportFocused() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!ViewportClient)
	{
		return false;
	}

	const TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
	return ViewportWidget.IsValid() && ViewportWidget->HasAnyUserFocusOrFocusedDescendants();
}

void UPlaytestFeedbackSubsystem::CaptureContext()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance->GetWorld();

	PendingNote = MakeShared<FPlaytestNoteData>();
	FPlaytestNoteData& Note = *PendingNote;

	Note.Timestamp = FDateTime::Now();
	Note.LevelName = UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString*/ true);
	Note.GameTimeSeconds = World->GetTimeSeconds();
	Note.RealTimeSeconds = World->GetRealTimeSeconds();
	Note.AverageFps = FpsTracker.IsValid() ? FpsTracker->GetAverageFps() : 0.0f;
	Note.Commit = CachedCommit;
	Note.BuildConfiguration = LexToString(FApp::GetBuildConfiguration());
	Note.NetMode = ToString(World->GetNetMode());

	if (APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World))
	{
		Note.Rotation = PC->GetControlRotation();
		if (const APawn* Pawn = PC->GetPawn())
		{
			Note.Location = Pawn->GetActorLocation();
		}
		else
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			Note.Location = ViewLocation;
			Note.Rotation = ViewRotation;
		}
	}
}

void UPlaytestFeedbackSubsystem::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	if (State != EFeedbackState::Capturing)
	{
		return;
	}

	PendingWidth = Width;
	PendingHeight = Height;
	PendingPixels = Colors;

	UnbindScreenshotDelegates();
	OpenNoteBox();
}

void UPlaytestFeedbackSubsystem::OnHDRScreenshotCaptured(int32 Width, int32 Height, const TArray<FLinearColor>& Colors)
{
	if (State != EFeedbackState::Capturing)
	{
		return;
	}

	TArray<FColor> Converted;
	Converted.Reserve(Colors.Num());
	for (const FLinearColor& Color : Colors)
	{
		Converted.Add(Color.ToFColor(/*bSRGB*/ true));
	}
	OnScreenshotCaptured(Width, Height, Converted);
}

void UPlaytestFeedbackSubsystem::UnbindScreenshotDelegates()
{
	if (ScreenshotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
		ScreenshotHandle.Reset();
	}
	if (HDRScreenshotHandle.IsValid())
	{
		UGameViewportClient::OnHDRScreenshotCaptured().Remove(HDRScreenshotHandle);
		HDRScreenshotHandle.Reset();
	}
}

void UPlaytestFeedbackSubsystem::OpenNoteBox()
{
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!ViewportClient || !World)
	{
		State = EFeedbackState::Idle;
		return;
	}

	// pause locally; a network client never pauses (that would pause the server for everyone)
	bPausedByUs = false;
	if (bPauseWhileTyping && World->GetNetMode() != NM_Client && !UGameplayStatics::IsGamePaused(World))
	{
		bPausedByUs = UGameplayStatics::SetGamePaused(World, true);
	}

	NoteWidget = SNew(SPlaytestNoteWidget);
	ViewportClient->AddViewportWidgetContent(NoteWidget.ToSharedRef(), /*ZOrder*/ 10000);

	if (APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World))
	{
		bPrevShowMouseCursor = PC->bShowMouseCursor;
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(NoteWidget->GetFocusTarget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(NoteWidget->GetFocusTarget(), EFocusCause::SetDirectly);
	}

	State = EFeedbackState::Typing;
}

void UPlaytestFeedbackSubsystem::SubmitNote()
{
	if (PendingNote.IsValid() && NoteWidget.IsValid())
	{
		PendingNote->Text = NoteWidget->GetNoteText().TrimStartAndEnd();

		FString Folder, Error;
		if (FPlaytestNoteWriter::WriteNote(FPlaytestNoteWriter::GetDefaultRootDir(), *PendingNote, PendingWidth, PendingHeight, PendingPixels, Folder, Error))
		{
			UE_LOG(LogVibeGame, Log, TEXT("Playtest note saved: %s"), *Folder);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Green, FString::Printf(TEXT("Playtest note saved: %s"), *FPaths::GetCleanFilename(Folder)));
			}
		}
		else
		{
			UE_LOG(LogVibeGame, Error, TEXT("Playtest note failed: %s"), *Error);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red, FString::Printf(TEXT("Playtest note failed: %s"), *Error));
			}
		}
	}

	CloseNoteBox();
}

void UPlaytestFeedbackSubsystem::CloseNoteBox()
{
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;

	if (NoteWidget.IsValid() && ViewportClient)
	{
		ViewportClient->RemoveViewportWidgetContent(NoteWidget.ToSharedRef());
	}
	NoteWidget.Reset();

	if (World)
	{
		if (APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World))
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->SetShowMouseCursor(bPrevShowMouseCursor);
		}

		if (bPausedByUs)
		{
			UGameplayStatics::SetGamePaused(World, false);
		}
	}
	bPausedByUs = false;

	if (FSlateApplication::IsInitialized() && ViewportClient)
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}

	PendingNote.Reset();
	PendingPixels.Empty();
	PendingWidth = PendingHeight = 0;
	State = EFeedbackState::Idle;
}

#else // !VIBEGAME_WITH_PLAYTEST_FEEDBACK

bool UPlaytestFeedbackSubsystem::Tick(float DeltaTime) { return false; }
bool UPlaytestFeedbackSubsystem::IsGameViewportFocused() const { return false; }
void UPlaytestFeedbackSubsystem::CaptureContext() {}
void UPlaytestFeedbackSubsystem::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors) {}
void UPlaytestFeedbackSubsystem::OnHDRScreenshotCaptured(int32 Width, int32 Height, const TArray<FLinearColor>& Colors) {}
void UPlaytestFeedbackSubsystem::UnbindScreenshotDelegates() {}
void UPlaytestFeedbackSubsystem::OpenNoteBox() {}
void UPlaytestFeedbackSubsystem::SubmitNote() {}
void UPlaytestFeedbackSubsystem::CloseNoteBox() {}

#endif // VIBEGAME_WITH_PLAYTEST_FEEDBACK

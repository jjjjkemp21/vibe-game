// Copyright Epic Games, Inc. All Rights Reserved.

#include "Playtest/PlaytestFeedbackSubsystem.h"
#include "Playtest/PlaytestNoteWriter.h"
#include "Playtest/PlaytestFeedbackRules.h"
#include "VibeGame.h"

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
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
#include "Widgets/SNullWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

namespace PlaytestFeedbackUi
{
	/** UI palette from docs/ART_STYLE.md (hex values are sRGB) */
	static FLinearColor FromHex(const TCHAR* Hex, const float Alpha = 1.0f)
	{
		return FLinearColor(FColor::FromHex(Hex)).CopyWithNewOpacity(Alpha);
	}
	static FLinearColor Ink(const float Alpha = 1.0f) { return FromHex(TEXT("2B2A26"), Alpha); }
	static FLinearColor Parchment(const float Alpha = 1.0f) { return FromHex(TEXT("F3E9D2"), Alpha); }
	static FLinearColor Accent() { return FromHex(TEXT("FF4D3D")); }
	static FLinearColor Safe() { return FromHex(TEXT("3FA34D")); }
	static FLinearColor Danger() { return FromHex(TEXT("C0392B")); }

	// Layout (designer review A-M2/A-M3/A-S1). Sizes are Slate units: pixels at 1080p, scaled with the viewport by the
	// game UI DPI curve like all viewport content.
	/** Bottom edge of the box and of the toast, as a fraction of the viewport height (1080p: box y ~825-1015, crosshair and bobber band clear) */
	constexpr float BottomEdgeFraction = 0.94f;
	constexpr float BoxWidth = 900.0f;
	constexpr float ToastMinWidth = 420.0f;
	constexpr int32 TitleFontSize = 18;
	constexpr int32 FieldFontSize = 18;
	constexpr int32 HintFontSize = 14;
	constexpr int32 ToastTitleFontSize = 20;
	constexpr int32 ToastDetailFontSize = 14;
	constexpr int32 FieldVisibleLines = 3;
	constexpr int32 MaxNoteLength = 2000;
	constexpr double ToastFadeSeconds = 1.0;

	/** Brushes and the text field style; built once on first use (game thread) and kept for the program's lifetime */
	struct FStyle
	{
		/** Ink panel at 90% with a 2 px parchment border at 60% (holds up over dark scenes) */
		FSlateRoundedBoxBrush PanelBrush;
		/** Ink toast panel at 90% */
		FSlateColorBrush ToastBrush;
		/** White, tinted per use (the toast's left bar) */
		FSlateColorBrush BarBrush;
		/** Parchment field, ink text, accent focus ring */
		FEditableTextBoxStyle FieldStyle;

		FStyle()
			: PanelBrush(Ink(0.9f), 4.0f, Parchment(0.6f), 2.0f)
			, ToastBrush(Ink(0.9f))
			, BarBrush(FLinearColor::White)
		{
			FieldStyle = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
			const FSlateRoundedBoxBrush Normal(Parchment(), 3.0f, Ink(0.35f), 1.0f);
			const FSlateRoundedBoxBrush Hovered(Parchment(), 3.0f, Ink(0.6f), 1.0f);
			const FSlateRoundedBoxBrush Focused(Parchment(), 3.0f, Accent(), 2.0f);
			FieldStyle
				.SetBackgroundImageNormal(Normal)
				.SetBackgroundImageHovered(Hovered)
				.SetBackgroundImageFocused(Focused)
				.SetBackgroundImageReadOnly(Normal)
				.SetPadding(FMargin(10.0f, 6.0f))
				.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FieldFontSize))
				.SetForegroundColor(FSlateColor(Ink()))
				.SetFocusedForegroundColor(FSlateColor(Ink()))
				.SetReadOnlyForegroundColor(FSlateColor(Ink()))
				.SetBackgroundColor(FSlateColor(FLinearColor::White));
			FieldStyle.TextStyle.SetColorAndOpacity(FSlateColor::UseForeground());
		}
	};

	static const FStyle& GetStyle()
	{
		static const FStyle Style;
		return Style;
	}

	/** Height of one text line for a font, in Slate units */
	static float MeasureLineHeight(const FSlateFontInfo& Font)
	{
		if (FSlateApplication::IsInitialized())
		{
			if (const FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
			{
				const float Height = Renderer->GetFontMeasureService()->GetMaxCharacterHeight(Font, 1.0f);
				if (Height > 0.0f)
				{
					return Height;
				}
			}
		}
		return Font.Size * 1.6f;
	}

	/** Places Content bottom-center with its bottom edge at BottomEdgeFraction of the viewport height; the rest lets clicks through */
	static TSharedRef<SWidget> PlaceInLowerThird(const TSharedRef<SWidget>& Content)
	{
		return SNew(SVerticalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SVerticalBox::Slot()
			.FillHeight(BottomEdgeFraction)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			[
				Content
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f - BottomEdgeFraction)
			[
				SNullWidget::NullWidget
			];
	}

	/** The confirmation toast: ink panel, 4 px safe/danger bar on the left, a large title and an optional small detail line */
	static TSharedRef<SWidget> MakeToast(const FString& Title, const FString& Detail, const bool bFailure)
	{
		const FStyle& Style = GetStyle();

		TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", ToastTitleFontSize))
				.ColorAndOpacity(Parchment())
				.AutoWrapText(true)
				.Text(FText::FromString(Title))
			];
		if (!Detail.IsEmpty())
		{
			Lines->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", ToastDetailFontSize))
				.ColorAndOpacity(Parchment(0.7f))
				.AutoWrapText(true)
				.Text(FText::FromString(Detail))
			];
		}

		TSharedRef<SWidget> Toast = PlaceInLowerThird(
			SNew(SBox)
			.MinDesiredWidth(ToastMinWidth)
			.MaxDesiredWidth(BoxWidth)
			[
				SNew(SBorder)
				.BorderImage(&Style.ToastBrush)
				.Padding(0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(4.0f)
						[
							SNew(SImage)
							.Image(&Style.BarBrush)
							.ColorAndOpacity(bFailure ? Danger() : Safe())
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(FMargin(16.0f, 10.0f, 20.0f, 12.0f))
					[
						Lines
					]
				]
			]);
		Toast->SetVisibility(EVisibility::HitTestInvisible);
		return Toast;
	}
}

/** The note box: bottom-center panel with a title, a 3-line wrapping field and the key hints, built in C++ with Slate */
class SPlaytestNoteWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SPlaytestNoteWidget)
		: _bGamePaused(false)
	{}
		/** Shows "(game paused)" in the key hints */
		SLATE_ARGUMENT(bool, bGamePaused)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		using namespace PlaytestFeedbackUi;
		const FStyle& Style = GetStyle();
		const FSlateFontInfo FieldFont = FCoreStyle::GetDefaultFontStyle("Regular", FieldFontSize);

		// 3 visible lines (plus the field padding and its 2 px focus ring); longer notes wrap and scroll inside the field
		const float FieldHeight = FieldVisibleLines * MeasureLineHeight(FieldFont) + Style.FieldStyle.Padding.GetTotalSpaceAlong<Orient_Vertical>() + 4.0f;

		FString Hints = TEXT("Enter = save      Shift+Enter = new line      Esc = cancel");
		if (InArgs._bGamePaused)
		{
			Hints += TEXT("      (game paused)");
		}

		ChildSlot
		[
			PlaceInLowerThird(
				SNew(SBox)
				.WidthOverride(BoxWidth)
				[
					SNew(SBorder)
					.BorderImage(&Style.PanelBrush)
					.Padding(FMargin(18.0f, 14.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", TitleFontSize))
							.ColorAndOpacity(Parchment())
							.Text(NSLOCTEXT("PlaytestFeedback", "Title", "Playtest note"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.HeightOverride(FieldHeight)
							[
								SAssignNew(TextBox, SMultiLineEditableTextBox)
								.Style(&Style.FieldStyle)
								.Font(FieldFont)
								.ForegroundColor(Ink())
								.FocusedForegroundColor(Ink())
								.HintText(NSLOCTEXT("PlaytestFeedback", "Hint", "What did you notice?"))
								.AutoWrapText(true)
								.ModiferKeyForNewLine(EModifierKey::Shift)
								.MaximumLength(MaxNoteLength)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", HintFontSize))
							.ColorAndOpacity(Parchment(0.7f))
							.Text(FText::FromString(Hints))
						]
					]
				])
		];
	}

	/** The raw field text (Slate line endings; see FPlaytestFeedbackRules::NormalizeNoteText) */
	FString GetNoteText() const
	{
		return TextBox.IsValid() ? TextBox->GetText().ToString() : FString();
	}

	TSharedPtr<SWidget> GetFocusTarget() const
	{
		return TextBox;
	}

private:

	TSharedPtr<SMultiLineEditableTextBox> TextBox;
};

namespace PlaytestFeedbackPrivate
{
	/** Slate input preprocessor: sees keys before any widget or editor command, independent of pawn/controller */
	class FInputProcessor : public IInputProcessor
	{
	public:

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			return UPlaytestFeedbackSubsystem::RouteKeyDown(InKeyEvent);
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("PlaytestFeedback"); }
	};

	/** Every live subsystem: one per game instance (each PIE client/server, or the standalone game). Game thread only. */
	static TArray<TWeakObjectPtr<UPlaytestFeedbackSubsystem>> GLiveSubsystems;

	/** One shared preprocessor for all instances, registered while any subsystem lives, so exactly one instance decides */
	static TSharedPtr<IInputProcessor> GInputProcessor;

	/** Grows every frame some game viewport has keyboard focus; the biggest LastFocusSerial is the most recently focused */
	static uint64 GFocusSerial = 0;
}

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
	using namespace PlaytestFeedbackPrivate;

	FpsTracker = MakeShared<FPlaytestFpsTracker>(FpsWindowSeconds);
	CachedCommit = FPlaytestNoteWriter::ReadGitCommit(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPlaytestFeedbackSubsystem::Tick));

	GLiveSubsystems.RemoveAll([](const TWeakObjectPtr<UPlaytestFeedbackSubsystem>& Entry) { return !Entry.IsValid(); });
	GLiveSubsystems.AddUnique(this);
	if (!GInputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		GInputProcessor = MakeShared<FInputProcessor>();
		FSlateApplication::Get().RegisterInputPreProcessor(GInputProcessor);
	}

	UE_LOG(LogVibeGame, Log, TEXT("Playtest feedback ready: press %s during play (commit %s)"), *FeedbackKey.ToString(), *CachedCommit);
#endif
}

void UPlaytestFeedbackSubsystem::Deinitialize()
{
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	using namespace PlaytestFeedbackPrivate;

	UnbindScreenshotDelegates();

	// the game is ending: keep what the player typed, then take our widgets down without touching the world
	SaveNoteOnTeardown();
	RemoveWidgetsForTeardown();

	GLiveSubsystems.RemoveAll([this](const TWeakObjectPtr<UPlaytestFeedbackSubsystem>& Entry) { return !Entry.IsValid() || Entry.Get() == this; });
	if (GLiveSubsystems.Num() == 0 && GInputProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(GInputProcessor);
		}
		GInputProcessor.Reset();
	}

	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();

	State = EFeedbackState::Idle;
#endif

	Super::Deinitialize();
}

bool UPlaytestFeedbackSubsystem::RouteKeyDown(const FKeyEvent& InKeyEvent)
{
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	using namespace PlaytestFeedbackPrivate;
	if (!IsInGameThread())
	{
		return false;
	}

	TArray<UPlaytestFeedbackSubsystem*, TInlineAllocator<4>> Live;
	for (const TWeakObjectPtr<UPlaytestFeedbackSubsystem>& Entry : GLiveSubsystems)
	{
		if (UPlaytestFeedbackSubsystem* Subsystem = Entry.Get())
		{
			Live.Add(Subsystem);
		}
	}

	// an open note box (or a pending capture) gets every key first: Enter, Shift+Enter, Escape, F8
	for (UPlaytestFeedbackSubsystem* Subsystem : Live)
	{
		if (Subsystem->State != EFeedbackState::Idle)
		{
			return Subsystem->HandleKeyDown(InKeyEvent);
		}
	}

	// otherwise only the feedback key matters
	const FKey Key = InKeyEvent.GetKey();
	if (!Live.ContainsByPredicate([&Key](const UPlaytestFeedbackSubsystem* Subsystem) { return Subsystem->FeedbackKey == Key; }))
	{
		return false;
	}

	// exactly one instance decides: focused, else most recently focused, else primary / first PIE instance
	TArray<FPlaytestFeedbackCandidate, TInlineAllocator<4>> Candidates;
	for (const UPlaytestFeedbackSubsystem* Subsystem : Live)
	{
		FPlaytestFeedbackCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.bSessionRunning = Subsystem->IsSessionRunning();
		Candidate.bFeedbackActive = Subsystem->State != EFeedbackState::Idle;
		Candidate.bViewportFocused = Subsystem->IsGameViewportFocused();
		Candidate.LastFocusSerial = Subsystem->LastFocusSerial;
		const UGameInstance* GameInstance = Subsystem->GetGameInstance();
		if (const FWorldContext* WorldContext = GameInstance ? GameInstance->GetWorldContext() : nullptr)
		{
			Candidate.bIsPrimaryPIE = WorldContext->bIsPrimaryPIEInstance;
			Candidate.PIEInstance = WorldContext->PIEInstance;
		}
	}

	const int32 Chosen = FPlaytestFeedbackRules::ChooseHandler(Candidates);
	return Chosen != INDEX_NONE && Live[Chosen]->HandleKeyDown(InKeyEvent);
#else
	return false;
#endif
}

bool UPlaytestFeedbackSubsystem::HandleKeyDown(const FKeyEvent& InKeyEvent)
{
#if VIBEGAME_WITH_PLAYTEST_FEEDBACK
	const FKey Key = InKeyEvent.GetKey();

	switch (State)
	{
	case EFeedbackState::Idle:
	{
		if (Key != FeedbackKey)
		{
			return false;
		}

		FPlaytestFeedbackKeyContext Context;
		Context.bIsFeedbackKey = true;
		Context.bIsRepeat = InKeyEvent.IsRepeat();
		Context.bSessionRunning = IsSessionRunning();
		Context.bEjected = IsEjected();
		Context.bTypingInText = IsTypingInText(InKeyEvent.GetUserIndex());

		switch (FPlaytestFeedbackRules::DecideFeedbackKey(Context))
		{
		case EPlaytestFeedbackKeyAction::PassThrough:
			return false;

		case EPlaytestFeedbackKeyAction::Consume:
			return true;

		case EPlaytestFeedbackKeyAction::OpenNote:
			// never let the editor see this F8 (its "Possess or Eject Player" would eject the player)
			if (!IsGameViewportFocused())
			{
				FocusGameViewport();
			}
			if (!BeginFeedback())
			{
				UE_LOG(LogVibeGame, Warning, TEXT("Playtest feedback: %s was pressed but the note could not start (no viewport or world)"), *FeedbackKey.ToString());
			}
			return true;
		}
		return false;
	}

	case EFeedbackState::Capturing:
		// swallow the feedback key while the screenshot is pending
		return Key == FeedbackKey;

	case EFeedbackState::Typing:
		switch (FPlaytestFeedbackRules::DecideNoteBoxKey(Key, InKeyEvent.IsShiftDown(), FeedbackKey))
		{
		case EPlaytestNoteBoxKeyAction::Submit:
			SubmitNote();
			return true;

		case EPlaytestNoteBoxKeyAction::Cancel:
			// consumed here so Escape does not also stop PIE
			UE_LOG(LogVibeGame, Log, TEXT("Playtest note cancelled"));
			CloseNoteBox();
			return true;

		case EPlaytestNoteBoxKeyAction::Consume:
			return true;

		case EPlaytestNoteBoxKeyAction::PassThrough:
			// typing, and Shift+Enter: the field inserts the new line itself
			return false;
		}
		return false;
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

	// remember when this viewport last had keyboard focus (decides which PIE instance handles the feedback key)
	if (IsGameViewportFocused())
	{
		LastFocusSerial = ++PlaytestFeedbackPrivate::GFocusSerial;
	}

	// screenshot never arrived: open the note box without it
	if (State == EFeedbackState::Capturing && FPlatformTime::Seconds() - CaptureStartTime > ScreenshotTimeoutSeconds)
	{
		UE_LOG(LogVibeGame, Warning, TEXT("Playtest screenshot timed out; the note will be saved without screenshot.png"));
		UnbindScreenshotDelegates();
		OpenNoteBox();
	}

	// toast: fade out over the last second (real time, so it also runs while paused), then remove it
	if (ToastWidget.IsValid())
	{
		const double Remaining = ToastEndTime - FPlatformTime::Seconds();
		if (Remaining <= 0.0)
		{
			HideToast();
		}
		else
		{
			ToastWidget->SetRenderOpacity(static_cast<float>(FMath::Clamp(Remaining / PlaytestFeedbackUi::ToastFadeSeconds, 0.0, 1.0)));
		}
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

bool UPlaytestFeedbackSubsystem::IsSessionRunning() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World && World->IsGameWorld() && World->HasBegunPlay() && !World->bIsTearingDown && GameInstance->GetGameViewportClient() != nullptr;
}

bool UPlaytestFeedbackSubsystem::IsEjected() const
{
	// set by the editor when PIE toggles to simulate (UEditorEngine::ToggleBetweenPIEandSIE); always false outside the editor
	const UGameInstance* GameInstance = GetGameInstance();
	const UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	return ViewportClient && ViewportClient->IsSimulateInEditorViewport();
}

bool UPlaytestFeedbackSubsystem::IsTypingInText(const uint32 UserIndex) const
{
	if (FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(UserIndex);
		if (Focused.IsValid() && FPlaytestFeedbackRules::IsTextEntryWidgetType(Focused->GetType()))
		{
			return true;
		}
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	return ViewportClient && ViewportClient->ViewportConsole && ViewportClient->ViewportConsole->ConsoleActive();
}

void UPlaytestFeedbackSubsystem::FocusGameViewport()
{
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
	if (!ViewportWidget.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// the same two steps the editor uses to hand a PIE viewport the keyboard (UEditorEngine::GiveFocusToLastClientPIEViewport):
	// make it the registered game viewport (this also activates its window), then focus it
	FSlateApplication& Slate = FSlateApplication::Get();
	Slate.RegisterGameViewport(ViewportWidget.ToSharedRef());
	Slate.SetAllUserFocusToGameViewport();
	UE_LOG(LogVibeGame, Log, TEXT("Playtest feedback: the game viewport did not have keyboard focus; focused it before opening the note"));
}

void UPlaytestFeedbackSubsystem::CaptureContext()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance->GetWorld();

	PendingNote = MakeShared<FPlaytestNoteData>();
	FPlaytestNoteData& Note = *PendingNote;

	// folder name in local time, note.json "timestamp" in UTC, both from one clock reading
	FPlaytestNoteWriter::StampNow(Note);
	Note.LevelName = UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString*/ true);
	Note.GameTimeSeconds = World->GetTimeSeconds();
	Note.RealTimeSeconds = World->GetRealTimeSeconds();
	Note.AverageFps = FpsTracker.IsValid() ? FpsTracker->GetAverageFps() : 0.0f;
	Note.Commit = CachedCommit;
	Note.BuildConfiguration = LexToString(FApp::GetBuildConfiguration());
	Note.NetMode = ToString(World->GetNetMode());

	if (APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World))
	{
		// the view: what the player was looking at (pitch included)
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		if (PC->PlayerCameraManager)
		{
			ViewLocation = PC->PlayerCameraManager->GetCameraLocation();
			ViewRotation = PC->PlayerCameraManager->GetCameraRotation();
		}
		else
		{
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
		Note.CameraLocation = ViewLocation;
		Note.CameraRotation = ViewRotation;

		Note.Rotation = PC->GetControlRotation();
		if (const APawn* Pawn = PC->GetPawn())
		{
			Note.Location = Pawn->GetActorLocation();
		}
		else
		{
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

	// a toast still showing from the previous note would sit under the box
	HideToast();

	NoteWidget = SNew(SPlaytestNoteWidget).bGamePaused(UGameplayStatics::IsGamePaused(World));
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
	FPlaytestConfirmation Confirmation;
	bool bHasConfirmation = false;

	if (NoteWidget.IsValid())
	{
		// an empty text still saves the folder: a screenshot-only bookmark
		const FString Text = FPlaytestFeedbackRules::NormalizeNoteText(NoteWidget->GetNoteText());
		bHasConfirmation = true;

		if (!PendingNote.IsValid())
		{
			// never silently lose what the player typed: keep it in the log
			UE_LOG(LogVibeGame, Warning, TEXT("Playtest note NOT saved (no captured context). Note text: %s"), *Text);
			Confirmation = FPlaytestFeedbackRules::MakeConfirmation(false, Text, false, FString(), FString(), TEXT("no captured context"));
		}
		else
		{
			PendingNote->Text = Text;

			FString Folder, Error, WriteWarning;
			const bool bSaved = FPlaytestNoteWriter::WriteNote(FPlaytestNoteWriter::GetDefaultRootDir(), *PendingNote, PendingWidth, PendingHeight, PendingPixels, Folder, Error, &WriteWarning);
			if (bSaved)
			{
				if (WriteWarning.IsEmpty())
				{
					UE_LOG(LogVibeGame, Log, TEXT("Playtest note saved: %s"), *Folder);
				}
				else
				{
					UE_LOG(LogVibeGame, Warning, TEXT("Playtest note saved with problems: %s (%s)"), *Folder, *WriteWarning);
				}
			}
			else
			{
				// never silently lose what the player typed: the full note survives in the log
				UE_LOG(LogVibeGame, Error, TEXT("Playtest note failed: %s"), *Error);
				UE_LOG(LogVibeGame, Warning, TEXT("Playtest note NOT saved. Note text: %s"), *PendingNote->Text);
				UE_LOG(LogVibeGame, Warning, TEXT("Playtest note NOT saved. Full note.json:\n%s"), *FPlaytestNoteWriter::ToJsonString(*PendingNote, /*bHasScreenshot*/ false));
			}

			const bool bHasScreenshot = bSaved && FPaths::FileExists(Folder / TEXT("screenshot.png"));
			Confirmation = FPlaytestFeedbackRules::MakeConfirmation(bSaved, Text, bHasScreenshot, FPaths::GetCleanFilename(Folder), WriteWarning, Error);
		}
	}

	CloseNoteBox();

	if (bHasConfirmation)
	{
		ShowToast(Confirmation.Title, Confirmation.Detail, Confirmation.bFailure);
	}
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

void UPlaytestFeedbackSubsystem::ShowToast(const FString& Title, const FString& Detail, const bool bFailure)
{
	HideToast();

	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!ViewportClient)
	{
		// nowhere to show it; the log already has the outcome
		return;
	}

	ToastWidget = PlaytestFeedbackUi::MakeToast(Title, Detail, bFailure);
	ViewportClient->AddViewportWidgetContent(ToastWidget.ToSharedRef(), /*ZOrder*/ 9999);
	ToastEndTime = FPlatformTime::Seconds() + FMath::Max(1.0f, bFailure ? FailureToastSeconds : ToastSeconds);
}

void UPlaytestFeedbackSubsystem::HideToast()
{
	if (ToastWidget.IsValid())
	{
		UGameInstance* GameInstance = GetGameInstance();
		UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
		if (ViewportClient)
		{
			ViewportClient->RemoveViewportWidgetContent(ToastWidget.ToSharedRef());
		}
	}
	ToastWidget.Reset();
	ToastEndTime = 0.0;
}

void UPlaytestFeedbackSubsystem::SaveNoteOnTeardown()
{
	const bool bNoteBoxOpen = State == EFeedbackState::Typing && NoteWidget.IsValid();
	const FString RawText = bNoteBoxOpen ? NoteWidget->GetNoteText() : FString();
	if (!FPlaytestFeedbackRules::ShouldSaveOnTeardown(bNoteBoxOpen, RawText))
	{
		return;
	}

	const FString Text = FPlaytestFeedbackRules::NormalizeNoteText(RawText);
	if (!PendingNote.IsValid())
	{
		UE_LOG(LogVibeGame, Warning, TEXT("Playtest note NOT saved: the game ended while the note box was open (no captured context). Note text: %s"), *Text);
		return;
	}
	PendingNote->Text = Text;

	// file IO only (safe during teardown). The captured screenshot is kept when PIE or the game ends; when the whole
	// application is exiting, the PNG step is skipped and only the text and context are saved.
	const bool bKeepScreenshot = !IsEngineExitRequested();
	const TArray<FColor> NoPixels;
	FString Folder, Error, WriteWarning;
	if (FPlaytestNoteWriter::WriteNote(FPlaytestNoteWriter::GetDefaultRootDir(), *PendingNote, PendingWidth, PendingHeight, bKeepScreenshot ? PendingPixels : NoPixels, Folder, Error, &WriteWarning))
	{
		const FString Problems = WriteWarning.IsEmpty() ? FString() : FString::Printf(TEXT(" (warnings: %s)"), *WriteWarning);
		UE_LOG(LogVibeGame, Warning, TEXT("Playtest note saved when the game ended with the note box open: %s%s"), *Folder, *Problems);
	}
	else
	{
		UE_LOG(LogVibeGame, Warning, TEXT("Playtest note NOT saved when the game ended (%s). Note text: %s"), *Error, *Text);
		UE_LOG(LogVibeGame, Warning, TEXT("Playtest note NOT saved. Full note.json:\n%s"), *FPlaytestNoteWriter::ToJsonString(*PendingNote, /*bHasScreenshot*/ false));
	}
}

void UPlaytestFeedbackSubsystem::RemoveWidgetsForTeardown()
{
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (IsValid(ViewportClient))
	{
		if (NoteWidget.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(NoteWidget.ToSharedRef());
		}
		if (ToastWidget.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(ToastWidget.ToSharedRef());
		}
	}
	NoteWidget.Reset();
	ToastWidget.Reset();

	PendingNote.Reset();
	PendingPixels.Empty();
	PendingWidth = PendingHeight = 0;
	bPausedByUs = false;
	State = EFeedbackState::Idle;
}

#else // !VIBEGAME_WITH_PLAYTEST_FEEDBACK

bool UPlaytestFeedbackSubsystem::Tick(float DeltaTime) { return false; }
bool UPlaytestFeedbackSubsystem::IsGameViewportFocused() const { return false; }
bool UPlaytestFeedbackSubsystem::IsSessionRunning() const { return false; }
bool UPlaytestFeedbackSubsystem::IsEjected() const { return false; }
bool UPlaytestFeedbackSubsystem::IsTypingInText(uint32 UserIndex) const { return false; }
void UPlaytestFeedbackSubsystem::FocusGameViewport() {}
void UPlaytestFeedbackSubsystem::CaptureContext() {}
void UPlaytestFeedbackSubsystem::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors) {}
void UPlaytestFeedbackSubsystem::OnHDRScreenshotCaptured(int32 Width, int32 Height, const TArray<FLinearColor>& Colors) {}
void UPlaytestFeedbackSubsystem::UnbindScreenshotDelegates() {}
void UPlaytestFeedbackSubsystem::OpenNoteBox() {}
void UPlaytestFeedbackSubsystem::SubmitNote() {}
void UPlaytestFeedbackSubsystem::CloseNoteBox() {}
void UPlaytestFeedbackSubsystem::ShowToast(const FString& Title, const FString& Detail, bool bFailure) {}
void UPlaytestFeedbackSubsystem::HideToast() {}
void UPlaytestFeedbackSubsystem::SaveNoteOnTeardown() {}
void UPlaytestFeedbackSubsystem::RemoveWidgetsForTeardown() {}

#endif // VIBEGAME_WITH_PLAYTEST_FEEDBACK

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Playtest/PlaytestFeedbackRules.h"

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

EPlaytestFeedbackKeyAction FPlaytestFeedbackRules::DecideFeedbackKey(const FPlaytestFeedbackKeyContext& Context)
{
	if (!Context.bIsFeedbackKey || !Context.bSessionRunning || Context.bEjected || Context.bTypingInText)
	{
		return EPlaytestFeedbackKeyAction::PassThrough;
	}
	return Context.bIsRepeat ? EPlaytestFeedbackKeyAction::Consume : EPlaytestFeedbackKeyAction::OpenNote;
}

EPlaytestNoteBoxKeyAction FPlaytestFeedbackRules::DecideNoteBoxKey(const FKey& Key, const bool bShiftDown, const FKey& FeedbackKey)
{
	if (Key == EKeys::Enter)
	{
		return bShiftDown ? EPlaytestNoteBoxKeyAction::PassThrough : EPlaytestNoteBoxKeyAction::Submit;
	}
	if (Key == EKeys::Escape)
	{
		return EPlaytestNoteBoxKeyAction::Cancel;
	}
	if (Key.IsValid() && Key == FeedbackKey)
	{
		return EPlaytestNoteBoxKeyAction::Consume;
	}
	return EPlaytestNoteBoxKeyAction::PassThrough;
}

int32 FPlaytestFeedbackRules::ChooseHandler(TConstArrayView<FPlaytestFeedbackCandidate> Candidates)
{
	// 1. an instance that is capturing or typing owns the key (it swallows it)
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].bFeedbackActive)
		{
			return Index;
		}
	}

	// 2. the viewport that has keyboard focus
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].bSessionRunning && Candidates[Index].bViewportFocused)
		{
			return Index;
		}
	}

	// 3. the viewport that had focus most recently
	int32 Best = INDEX_NONE;
	uint64 BestSerial = 0;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].bSessionRunning && Candidates[Index].LastFocusSerial > BestSerial)
		{
			Best = Index;
			BestSerial = Candidates[Index].LastFocusSerial;
		}
	}
	if (Best != INDEX_NONE)
	{
		return Best;
	}

	// 4. the primary PIE instance
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].bSessionRunning && Candidates[Index].bIsPrimaryPIE)
		{
			return Index;
		}
	}

	// 5. the lowest PIE instance number (ties: the first in the list)
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].bSessionRunning && (Best == INDEX_NONE || Candidates[Index].PIEInstance < Candidates[Best].PIEInstance))
		{
			Best = Index;
		}
	}
	return Best;
}

bool FPlaytestFeedbackRules::IsTextEntryWidgetType(const FName& WidgetType)
{
	static const FName TextEntryTypes[] = {
		FName(TEXT("SEditableText")),
		FName(TEXT("SMultiLineEditableText")),
		FName(TEXT("SEditableTextBox")),
		FName(TEXT("SMultiLineEditableTextBox"))
	};
	for (const FName& Type : TextEntryTypes)
	{
		if (WidgetType == Type)
		{
			return true;
		}
	}
	return false;
}

FString FPlaytestFeedbackRules::NormalizeNoteText(const FString& RawText)
{
	FString Text = RawText.Replace(TEXT("\r\n"), TEXT("\n"));
	Text.ReplaceCharInline(TEXT('\r'), TEXT('\n'));
	Text.TrimStartAndEndInline();
	return Text;
}

bool FPlaytestFeedbackRules::ShouldSaveOnTeardown(const bool bNoteBoxOpen, const FString& RawText)
{
	return bNoteBoxOpen && !NormalizeNoteText(RawText).IsEmpty();
}

FPlaytestConfirmation FPlaytestFeedbackRules::MakeConfirmation(const bool bSaved, const FString& SavedText, const bool bHasScreenshot, const FString& FolderName, const FString& Warning, const FString& Error)
{
	FPlaytestConfirmation Confirmation;
	if (!bSaved)
	{
		FString Reason = Error.TrimStartAndEnd();
		while (Reason.RemoveFromEnd(TEXT(".")))
		{
		}
		if (Reason.IsEmpty())
		{
			Reason = TEXT("unknown error");
		}
		Confirmation.Title = FString::Printf(TEXT("Note NOT saved: %s. Your text is in the log."), *Reason);
		Confirmation.bFailure = true;
		return Confirmation;
	}

	if (!SavedText.IsEmpty())
	{
		Confirmation.Title = TEXT("Note saved. Thanks!");
	}
	else if (bHasScreenshot)
	{
		Confirmation.Title = TEXT("Screenshot saved (no text)");
	}
	else
	{
		Confirmation.Title = TEXT("Note saved (no text, no screenshot)");
	}

	Confirmation.Detail = FolderName;
	if (!Warning.IsEmpty())
	{
		Confirmation.Detail += TEXT("  (saved with warnings, see the log)");
	}
	return Confirmation;
}

#endif // VIBEGAME_WITH_PLAYTEST_FEEDBACK

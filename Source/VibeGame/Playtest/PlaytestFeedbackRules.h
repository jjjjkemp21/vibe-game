// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Playtest/PlaytestNoteWriter.h"

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

/** What a press of the feedback key (F8) does */
enum class EPlaytestFeedbackKeyAction : uint8
{
	/** Not ours: the game or the editor handles the key (e.g. the editor's F8 "Possess or Eject Player") */
	PassThrough,
	/** Ours, but nothing opens (a held key repeating): swallowed so the editor's F8 never runs */
	Consume,
	/** Open the note box (focusing the game viewport first if needed) and swallow the key */
	OpenNote
};

/** What a key does while the note box is open */
enum class EPlaytestNoteBoxKeyAction : uint8
{
	/** Goes to the text box: typing, arrows, Shift+Enter (new line), ... */
	PassThrough,
	/** Enter without Shift: save the note (Enter never inserts a new line) */
	Submit,
	/** Escape: close without saving (swallowed so it does not also stop PIE) */
	Cancel,
	/** The feedback key again: swallowed, does nothing */
	Consume
};

/** Everything the "should F8 be captured" decision depends on; filled by the subsystem at key time (or by a test) */
struct FPlaytestFeedbackKeyContext
{
	/** The key is the configured feedback key (F8) */
	bool bIsFeedbackKey = false;

	/** The key event is an auto-repeat of a held key */
	bool bIsRepeat = false;

	/** This game instance's PIE session or standalone game is running (game world that has begun play, has a viewport, not tearing down) */
	bool bSessionRunning = false;

	/** Editor only: the player is ejected (PIE switched to simulate). F8 then belongs to the editor so it can possess again. */
	bool bEjected = false;

	/** Keyboard focus is in an editable text widget (editor field, editor console, game UI text box) or the game console is open */
	bool bTypingInText = false;
};

/** One running game instance (PIE instance or the standalone game), as seen when choosing who handles F8 */
struct FPlaytestFeedbackCandidate
{
	/** Same meaning as FPlaytestFeedbackKeyContext::bSessionRunning */
	bool bSessionRunning = false;

	/** Capturing the screenshot or showing the note box */
	bool bFeedbackActive = false;

	/** Its game viewport (or a widget inside it) has keyboard focus right now */
	bool bViewportFocused = false;

	/** When its viewport last had focus: 0 = never, a bigger number = more recent */
	uint64 LastFocusSerial = 0;

	/** FWorldContext::bIsPrimaryPIEInstance */
	bool bIsPrimaryPIE = false;

	/** FWorldContext::PIEInstance (INDEX_NONE outside PIE) */
	int32 PIEInstance = INDEX_NONE;
};

/** The words of the on-screen confirmation (toast) shown after Enter */
struct FPlaytestConfirmation
{
	/** First line, large */
	FString Title;

	/** Second line, small and dimmer (the folder name); may be empty */
	FString Detail;

	/** Failure style (danger bar, shown longer) instead of the saved style */
	bool bFailure = false;
};

/**
 *  Pure decision rules for the feedback key and its confirmation, free of Slate and world state so automation tests
 *  can call them. The subsystem gathers the facts (focus, PIE state, ...) and asks these functions what to do.
 */
class FPlaytestFeedbackRules
{
public:

	/**
	 *  Should this instance capture the feedback key, and open the note?
	 *  Captured when: it is the feedback key, the session is running, the player is not ejected and nobody is typing
	 *  in a text field. A captured repeat is only swallowed (Consume); a captured first press opens the note.
	 *  Viewport focus is deliberately NOT a condition: an unfocused game viewport is focused first (see the subsystem).
	 */
	static EPlaytestFeedbackKeyAction DecideFeedbackKey(const FPlaytestFeedbackKeyContext& Context);

	/** Key handling while the note box is open: Enter saves, Shift+Enter is a new line, Escape cancels, the feedback key is swallowed */
	static EPlaytestNoteBoxKeyAction DecideNoteBoxKey(const FKey& Key, bool bShiftDown, const FKey& FeedbackKey);

	/**
	 *  Which instance handles the feedback key when several run (PIE with clients/listen server). Exactly one, in this order:
	 *  1. an instance that is already capturing or showing its note box (it swallows the key);
	 *  2. the running instance whose viewport has keyboard focus;
	 *  3. the running instance whose viewport had focus most recently;
	 *  4. the primary PIE instance;
	 *  5. the lowest PIE instance number (standalone counts as lowest), then the first in the list.
	 *  @return index into Candidates, or INDEX_NONE if no instance is running
	 */
	static int32 ChooseHandler(TConstArrayView<FPlaytestFeedbackCandidate> Candidates);

	/** True for the Slate widget types that receive typed text (SEditableText, SMultiLineEditableText and their boxes) */
	static bool IsTextEntryWidgetType(const FName& WidgetType);

	/** The note text as saved: line endings become "\n" (Slate uses "\r\n" on Windows), leading/trailing whitespace is trimmed */
	static FString NormalizeNoteText(const FString& RawText);

	/** When the game ends with the note box open: save it only if the player typed something (an empty box is dropped) */
	static bool ShouldSaveOnTeardown(bool bNoteBoxOpen, const FString& RawText);

	/**
	 *  The confirmation after Enter.
	 *  Saved: "Note saved. Thanks!"; empty text: "Screenshot saved (no text)" (a screenshot-only bookmark); the folder name below.
	 *  Failed: "Note NOT saved: <reason>. Your text is in the log." in the failure style.
	 *  @param bSaved      WriteNote succeeded
	 *  @param SavedText   the normalized note text
	 *  @param bHasScreenshot screenshot.png exists in the folder
	 *  @param FolderName  the note folder name (yyyyMMdd-HHmmss)
	 *  @param Warning     WriteNote's non-fatal warning (empty if none)
	 *  @param Error       WriteNote's error (used when bSaved is false)
	 */
	static FPlaytestConfirmation MakeConfirmation(bool bSaved, const FString& SavedText, bool bHasScreenshot, const FString& FolderName, const FString& Warning, const FString& Error);
};

#endif // VIBEGAME_WITH_PLAYTEST_FEEDBACK

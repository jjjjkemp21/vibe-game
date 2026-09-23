// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Playtest feedback (F8 note key) exists in development builds and PIE only. */
#define VIBEGAME_WITH_PLAYTEST_FEEDBACK (!UE_BUILD_SHIPPING)

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

/**
 *  Everything recorded for one playtest note (the note.json content).
 *  Plain data so it can be filled by the game or by a test.
 */
struct FPlaytestNoteData
{
	/** The one-line note typed by the player */
	FString Text;

	/** Current level name without the PIE prefix */
	FString LevelName;

	/** Player pawn location (or camera location when there is no pawn) */
	FVector Location = FVector::ZeroVector;

	/** Player view (control) rotation */
	FRotator Rotation = FRotator::ZeroRotator;

	/** World time in seconds (excludes pauses, includes time dilation) */
	double GameTimeSeconds = 0.0;

	/** Real time in seconds since the world started */
	double RealTimeSeconds = 0.0;

	/** Average frames per second over the tracker window (5 s by default) */
	float AverageFps = 0.0f;

	/** Git commit id or "unknown" */
	FString Commit = TEXT("unknown");

	/** Build configuration, e.g. "Development" */
	FString BuildConfiguration;

	/** Net mode of the world, e.g. "Standalone", "Client" */
	FString NetMode;

	/** Local time the note was captured (used for the folder name) */
	FDateTime Timestamp;
};

/**
 *  Pure file logic for playtest notes: folder naming, note.json, screenshot.png, commit lookup.
 *  No world or running game needed, so automation tests can call it directly.
 */
class FPlaytestNoteWriter
{
public:

	/** Default root folder for notes: <Project>/Saved/Playtest */
	static FString GetDefaultRootDir();

	/** Folder name for a timestamp: yyyyMMdd-HHmmss */
	static FString MakeFolderName(const FDateTime& Timestamp);

	/** Serializes the note to the note.json text (strict JSON: NaN/Inf numbers are written as null) */
	static FString ToJsonString(const FPlaytestNoteData& Note, bool bHasScreenshot);

	/**
	 *  Writes <RootDir>/<yyyyMMdd-HHmmss>/note.json and, if pixels are given, screenshot.png.
	 *  If the folder already exists a numeric suffix is added (-2, -3, ...).
	 *  The note text matters more than the image: an invalid screenshot, or one that cannot be written, is dropped
	 *  (note.json then says "screenshot": "") and the call still succeeds with a warning.
	 *  On failure nothing is left behind: inputs are checked before anything is created, and folders this call
	 *  created are removed again.
	 *  @param RootDir must not be empty or blank (rejected); relative paths are resolved to absolute ones
	 *  @param Pixels may be empty (no screenshot written); otherwise must hold Width * Height colors
	 *  @param OutWarning if given, receives non-fatal problems on success (screenshot dropped, NaN/Inf written as null), empty if none
	 *  @return true if note.json was written; OutFolder receives the absolute folder path. false: OutError says why.
	 */
	static bool WriteNote(const FString& RootDir, const FPlaytestNoteData& Note, int32 Width, int32 Height, const TArray<FColor>& Pixels, FString& OutFolder, FString& OutError, FString* OutWarning = nullptr);

	/** True for a full git object id: exactly 40 (SHA-1) or 64 (SHA-256) hex characters */
	static bool IsCommitHash(const FString& Value);

	/** Reads the current commit id from <RepoRoot>/.git without running git. Returns a full commit id or "unknown", never other text. */
	static FString ReadGitCommit(const FString& RepoRoot);
};

/** Rolling average of frames per second over a time window. */
class FPlaytestFpsTracker
{
public:

	explicit FPlaytestFpsTracker(double InWindowSeconds = 5.0)
		: WindowSeconds(InWindowSeconds)
	{
	}

	/** Records one frame that ended at NowSeconds and took DeltaSeconds */
	void AddFrame(double NowSeconds, double DeltaSeconds);

	/** Average FPS over the frames inside the window (0 if none) */
	float GetAverageFps() const;

	void SetWindowSeconds(double InWindowSeconds) { WindowSeconds = FMath::Max(0.1, InWindowSeconds); }

	void Reset() { Frames.Reset(); }

private:

	struct FFrameSample
	{
		double EndTime;
		double Delta;
	};

	TArray<FFrameSample> Frames;

	double WindowSeconds;
};

#endif // VIBEGAME_WITH_PLAYTEST_FEEDBACK

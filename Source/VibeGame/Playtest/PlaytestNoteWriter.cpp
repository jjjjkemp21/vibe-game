// Copyright Epic Games, Inc. All Rights Reserved.

#include "Playtest/PlaytestNoteWriter.h"

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace PlaytestNoteWriterPrivate
{
	/** Sets a number field; NaN/Inf become JSON null (JSON has no token for them) and the field path is recorded */
	static void SetNumberOrNull(FJsonObject& Object, const TCHAR* Name, const double Value, const TCHAR* FieldPath, TArray<FString>& OutNonFinite)
	{
		if (FMath::IsFinite(Value))
		{
			Object.SetNumberField(Name, Value);
		}
		else
		{
			Object.SetField(Name, MakeShared<FJsonValueNull>());
			OutNonFinite.Add(FieldPath);
		}
	}

	/** Builds the note.json text; OutNonFinite lists the fields that were NaN/Inf and written as null */
	static FString BuildNoteJson(const FPlaytestNoteData& Note, const bool bHasScreenshot, TArray<FString>& OutNonFinite)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("text"), Note.Text);
		Root->SetStringField(TEXT("level"), Note.LevelName);

		TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
		SetNumberOrNull(*Location, TEXT("x"), Note.Location.X, TEXT("location.x"), OutNonFinite);
		SetNumberOrNull(*Location, TEXT("y"), Note.Location.Y, TEXT("location.y"), OutNonFinite);
		SetNumberOrNull(*Location, TEXT("z"), Note.Location.Z, TEXT("location.z"), OutNonFinite);
		Root->SetObjectField(TEXT("location"), Location);

		TSharedRef<FJsonObject> Rotation = MakeShared<FJsonObject>();
		SetNumberOrNull(*Rotation, TEXT("pitch"), Note.Rotation.Pitch, TEXT("rotation.pitch"), OutNonFinite);
		SetNumberOrNull(*Rotation, TEXT("yaw"), Note.Rotation.Yaw, TEXT("rotation.yaw"), OutNonFinite);
		SetNumberOrNull(*Rotation, TEXT("roll"), Note.Rotation.Roll, TEXT("rotation.roll"), OutNonFinite);
		Root->SetObjectField(TEXT("rotation"), Rotation);

		// the view: what the player was looking at (pitch included), next to the pawn fields above
		TSharedRef<FJsonObject> CameraLocation = MakeShared<FJsonObject>();
		SetNumberOrNull(*CameraLocation, TEXT("x"), Note.CameraLocation.X, TEXT("camera.location.x"), OutNonFinite);
		SetNumberOrNull(*CameraLocation, TEXT("y"), Note.CameraLocation.Y, TEXT("camera.location.y"), OutNonFinite);
		SetNumberOrNull(*CameraLocation, TEXT("z"), Note.CameraLocation.Z, TEXT("camera.location.z"), OutNonFinite);
		TSharedRef<FJsonObject> CameraRotation = MakeShared<FJsonObject>();
		SetNumberOrNull(*CameraRotation, TEXT("pitch"), Note.CameraRotation.Pitch, TEXT("camera.rotation.pitch"), OutNonFinite);
		SetNumberOrNull(*CameraRotation, TEXT("yaw"), Note.CameraRotation.Yaw, TEXT("camera.rotation.yaw"), OutNonFinite);
		SetNumberOrNull(*CameraRotation, TEXT("roll"), Note.CameraRotation.Roll, TEXT("camera.rotation.roll"), OutNonFinite);
		TSharedRef<FJsonObject> Camera = MakeShared<FJsonObject>();
		Camera->SetObjectField(TEXT("location"), CameraLocation);
		Camera->SetObjectField(TEXT("rotation"), CameraRotation);
		Root->SetObjectField(TEXT("camera"), Camera);

		SetNumberOrNull(*Root, TEXT("gameTime"), Note.GameTimeSeconds, TEXT("gameTime"), OutNonFinite);
		SetNumberOrNull(*Root, TEXT("realTime"), Note.RealTimeSeconds, TEXT("realTime"), OutNonFinite);
		SetNumberOrNull(*Root, TEXT("avgFps"), Note.AverageFps, TEXT("avgFps"), OutNonFinite);
		Root->SetStringField(TEXT("commit"), Note.Commit);
		Root->SetStringField(TEXT("buildConfiguration"), Note.BuildConfiguration);
		Root->SetStringField(TEXT("netMode"), Note.NetMode);
		Root->SetStringField(TEXT("timestamp"), FPlaytestNoteWriter::GetUtcTimestampText(Note));
		Root->SetStringField(TEXT("screenshot"), bHasScreenshot ? TEXT("screenshot.png") : TEXT(""));

		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	/** Folder and its ancestors that do not exist yet, deepest first (what MakeDirectory(Tree) would create) */
	static TArray<FString> FindMissingDirectories(const FString& Folder)
	{
		IFileManager& FileManager = IFileManager::Get();
		TArray<FString> Missing;
		FString Dir = Folder;
		while (!Dir.IsEmpty() && !FileManager.DirectoryExists(*Dir) && !FileManager.FileExists(*Dir) && Missing.Num() < 128)
		{
			Missing.Add(Dir);
			const FString Parent = FPaths::GetPath(Dir);
			if (Parent == Dir)
			{
				break;
			}
			Dir = Parent;
		}
		return Missing;
	}

	/** Removes the given directories deepest first, only if empty (never touches anything someone else put there) */
	static void RemoveEmptyDirectories(const TArray<FString>& DeepestFirst)
	{
		IFileManager& FileManager = IFileManager::Get();
		for (const FString& Dir : DeepestFirst)
		{
			FileManager.DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ false);
		}
	}

	/** Loads a small text file, trimmed; false if it cannot be read */
	static bool LoadTrimmed(const FString& Path, FString& OutText)
	{
		OutText.Reset();
		if (!FFileHelper::LoadFileToString(OutText, *Path))
		{
			return false;
		}
		OutText.TrimStartAndEndInline();
		return true;
	}

	/** A path written inside a git file ("gitdir: ...", "commondir"): absolute as is, relative against BaseDir; normalized */
	static FString ResolveGitPath(const FString& BaseDir, const FString& PathInFile)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(BaseDir), PathInFile);
	}

	/** Refs git keeps per worktree (in the worktree's own git dir); all other refs live in the common dir */
	static bool IsPerWorktreeRef(const FString& Ref)
	{
		return Ref.StartsWith(TEXT("refs/worktree/")) || Ref.StartsWith(TEXT("refs/bisect/")) || Ref.StartsWith(TEXT("refs/rewritten/"));
	}
}

FString FPlaytestNoteWriter::GetDefaultRootDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Playtest"));
}

FString FPlaytestNoteWriter::MakeFolderName(const FDateTime& Timestamp)
{
	return Timestamp.ToString(TEXT("%Y%m%d-%H%M%S"));
}

FString FPlaytestNoteWriter::MakeUtcTimestamp(const FDateTime& Utc)
{
	// FDateTime::ToIso8601 is "%Y-%m-%dT%H:%M:%S.%sZ" and assumes the value is already UTC
	return Utc.ToIso8601();
}

FTimespan FPlaytestNoteWriter::GetLocalUtcOffset()
{
	// two clock reads a few microseconds apart; real offsets are whole minutes (UTC-12:00 ... UTC+14:00, some :30/:45)
	const FTimespan Raw = FDateTime::Now() - FDateTime::UtcNow();
	return FTimespan::FromMinutes(FMath::RoundToDouble(Raw.GetTotalMinutes()));
}

void FPlaytestNoteWriter::StampNow(FPlaytestNoteData& Note)
{
	Note.TimestampUtc = FDateTime::UtcNow();
	Note.Timestamp = Note.TimestampUtc + GetLocalUtcOffset();
}

FString FPlaytestNoteWriter::GetUtcTimestampText(const FPlaytestNoteData& Note)
{
	if (Note.TimestampUtc.GetTicks() > 0)
	{
		return MakeUtcTimestamp(Note.TimestampUtc);
	}
	if (Note.Timestamp.GetTicks() > 0)
	{
		const FTimespan Offset = GetLocalUtcOffset();
		// stay inside FDateTime's valid range (a year-1 or year-9999 local time plus the offset)
		if (Note.Timestamp - FDateTime::MinValue() >= Offset && FDateTime::MaxValue() - Note.Timestamp >= -Offset)
		{
			return MakeUtcTimestamp(Note.Timestamp - Offset);
		}
	}
	return FString();
}

FString FPlaytestNoteWriter::ToJsonString(const FPlaytestNoteData& Note, bool bHasScreenshot)
{
	TArray<FString> NonFinite;
	return PlaytestNoteWriterPrivate::BuildNoteJson(Note, bHasScreenshot, NonFinite);
}

bool FPlaytestNoteWriter::WriteNote(const FString& RootDir, const FPlaytestNoteData& Note, int32 Width, int32 Height, const TArray<FColor>& Pixels, FString& OutFolder, FString& OutError, FString* OutWarning)
{
	using namespace PlaytestNoteWriterPrivate;
	IFileManager& FileManager = IFileManager::Get();

	OutFolder.Reset();
	OutError.Reset();
	TArray<FString> Warnings;
	ON_SCOPE_EXIT
	{
		if (OutWarning)
		{
			*OutWarning = FString::Join(Warnings, TEXT("; "));
		}
	};

	// 1. validate the inputs before anything is created
	// an empty root would resolve against the process folder (Engine/Binaries/Win64): a caller bug, never a place for notes
	if (RootDir.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("The playtest note root folder is empty");
		return false;
	}

	// the screenshot is optional: a bad one is dropped (with a warning) so the typed note is still saved
	TArray64<uint8> Png;
	if (Pixels.Num() > 0)
	{
		if (Width <= 0 || Height <= 0 || static_cast<int64>(Width) * static_cast<int64>(Height) != Pixels.Num())
		{
			Warnings.Add(FString::Printf(TEXT("screenshot dropped: size %dx%d does not match %d pixels"), Width, Height, Pixels.Num()));
		}
		else
		{
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			if (Png.Num() == 0)
			{
				Warnings.Add(TEXT("screenshot dropped: PNG compression failed"));
			}
		}
	}

	// 2. pick a unique folder and create it (remembering which folders this call creates, for cleanup)
	const FString BaseName = MakeFolderName(Note.Timestamp);
	FString Folder = FPaths::ConvertRelativePathToFull(RootDir / BaseName);
	for (int32 Suffix = 2; FileManager.DirectoryExists(*Folder) || FileManager.FileExists(*Folder); ++Suffix)
	{
		Folder = FPaths::ConvertRelativePathToFull(RootDir / FString::Printf(TEXT("%s-%d"), *BaseName, Suffix));
	}

	const TArray<FString> CreatedDirs = FindMissingDirectories(Folder);
	if (!FileManager.MakeDirectory(*Folder, /*Tree*/ true))
	{
		RemoveEmptyDirectories(CreatedDirs);
		OutError = FString::Printf(TEXT("Could not create folder %s"), *Folder);
		return false;
	}

	// 3. screenshot first, so note.json can say whether it exists; a failed write only drops the image
	bool bHasScreenshot = false;
	if (Png.Num() > 0)
	{
		const FString PngPath = Folder / TEXT("screenshot.png");
		if (FFileHelper::SaveArrayToFile(Png, *PngPath))
		{
			bHasScreenshot = true;
		}
		else
		{
			FileManager.Delete(*PngPath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
			Warnings.Add(TEXT("screenshot dropped: could not write screenshot.png"));
		}
	}

	// 4. note.json is the note: if it cannot be written, remove everything this call created
	TArray<FString> NonFinite;
	const FString Json = BuildNoteJson(Note, bHasScreenshot, NonFinite);
	if (!FFileHelper::SaveStringToFile(Json, *(Folder / TEXT("note.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		FileManager.DeleteDirectory(*Folder, /*RequireExists*/ false, /*Tree*/ true);
		RemoveEmptyDirectories(CreatedDirs);
		Warnings.Reset();
		OutError = FString::Printf(TEXT("Could not write %s"), *(Folder / TEXT("note.json")));
		return false;
	}

	if (NonFinite.Num() > 0)
	{
		Warnings.Add(FString::Printf(TEXT("non-finite numbers written as null: %s"), *FString::Join(NonFinite, TEXT(", "))));
	}

	OutFolder = Folder;
	return true;
}

bool FPlaytestNoteWriter::IsCommitHash(const FString& Value)
{
	if (Value.Len() != 40 && Value.Len() != 64)
	{
		return false;
	}
	for (const TCHAR Char : Value)
	{
		if (!FChar::IsHexDigit(Char))
		{
			return false;
		}
	}
	return true;
}

FString FPlaytestNoteWriter::ReadGitCommit(const FString& RepoRoot)
{
	using namespace PlaytestNoteWriterPrivate;
	const FString Unknown = TEXT("unknown");
	FString GitDir = RepoRoot / TEXT(".git");

	// 1. the git dir: <repo>/.git is a directory, or (worktrees, submodules) a file "gitdir: <path>"
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*GitDir))
	{
		FString GitFile;
		if (!LoadTrimmed(GitDir, GitFile) || !GitFile.RemoveFromStart(TEXT("gitdir:")))
		{
			return Unknown;
		}
		GitFile.TrimStartAndEndInline();
		if (GitFile.IsEmpty())
		{
			return Unknown;
		}
		GitDir = ResolveGitPath(RepoRoot, GitFile);
	}

	// 2. the common dir: a worktree's git dir names the main .git in "commondir"; shared refs and packed-refs live there
	FString CommonDir = GitDir;
	FString CommonDirFile;
	if (LoadTrimmed(GitDir / TEXT("commondir"), CommonDirFile) && !CommonDirFile.IsEmpty())
	{
		CommonDir = ResolveGitPath(GitDir, CommonDirFile);
	}

	// 3. HEAD always lives in the (worktree's own) git dir
	FString Head;
	if (!LoadTrimmed(GitDir / TEXT("HEAD"), Head))
	{
		return Unknown;
	}

	// detached HEAD holds the hash directly; anything else that is not "ref: ..." is garbage
	if (!Head.StartsWith(TEXT("ref:")))
	{
		return IsCommitHash(Head) ? Head : Unknown;
	}

	// symbolic HEAD: only follow real refs inside the git dirs
	FString Ref = Head.RightChop(4);
	Ref.TrimStartAndEndInline();
	if (!Ref.StartsWith(TEXT("refs/")) || Ref.Contains(TEXT("..")) || Ref.Contains(TEXT("\\")))
	{
		return Unknown;
	}

	// 4. a loose ref wins over packed-refs; if it exists but holds no valid hash the ref is broken (never report a stale packed id)
	const bool bPerWorktree = IsPerWorktreeRef(Ref);
	FString Hash;
	if (LoadTrimmed((bPerWorktree ? GitDir : CommonDir) / Ref, Hash))
	{
		return IsCommitHash(Hash) ? Hash : Unknown;
	}
	if (bPerWorktree)
	{
		// per-worktree refs are never in the shared packed-refs
		return Unknown;
	}

	// 5. fall back to packed refs in the common dir: lines of "<hash> <ref>" (peeled "^<hash>" lines and "#" comments never match)
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *(CommonDir / TEXT("packed-refs"))))
	{
		for (const FString& Line : Lines)
		{
			FString LineHash, LineRef;
			if (!Line.StartsWith(TEXT("#")) && Line.Split(TEXT(" "), &LineHash, &LineRef) && LineRef.TrimStartAndEnd() == Ref)
			{
				LineHash.TrimStartAndEndInline();
				return IsCommitHash(LineHash) ? LineHash : Unknown;
			}
		}
	}

	return Unknown;
}

void FPlaytestFpsTracker::AddFrame(double NowSeconds, double DeltaSeconds)
{
	if (DeltaSeconds > 0.0)
	{
		Frames.Add({ NowSeconds, DeltaSeconds });
	}

	// drop frames that ended before the window
	const double Cutoff = NowSeconds - WindowSeconds;
	int32 FirstKept = 0;
	while (FirstKept < Frames.Num() && Frames[FirstKept].EndTime < Cutoff)
	{
		++FirstKept;
	}
	if (FirstKept > 0)
	{
		Frames.RemoveAt(0, FirstKept, EAllowShrinking::No);
	}
}

float FPlaytestFpsTracker::GetAverageFps() const
{
	double TotalTime = 0.0;
	for (const FFrameSample& Frame : Frames)
	{
		TotalTime += Frame.Delta;
	}
	return TotalTime > 0.0 ? static_cast<float>(Frames.Num() / TotalTime) : 0.0f;
}

#endif // VIBEGAME_WITH_PLAYTEST_FEEDBACK

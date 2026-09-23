// Copyright Epic Games, Inc. All Rights Reserved.

#include "Playtest/PlaytestNoteWriter.h"

#if VIBEGAME_WITH_PLAYTEST_FEEDBACK

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/PrettyJsonPrintPolicy.h"

FString FPlaytestNoteWriter::GetDefaultRootDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Playtest"));
}

FString FPlaytestNoteWriter::MakeFolderName(const FDateTime& Timestamp)
{
	return Timestamp.ToString(TEXT("%Y%m%d-%H%M%S"));
}

FString FPlaytestNoteWriter::ToJsonString(const FPlaytestNoteData& Note, bool bHasScreenshot)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("text"), Note.Text);
	Root->SetStringField(TEXT("level"), Note.LevelName);

	TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
	Location->SetNumberField(TEXT("x"), Note.Location.X);
	Location->SetNumberField(TEXT("y"), Note.Location.Y);
	Location->SetNumberField(TEXT("z"), Note.Location.Z);
	Root->SetObjectField(TEXT("location"), Location);

	TSharedRef<FJsonObject> Rotation = MakeShared<FJsonObject>();
	Rotation->SetNumberField(TEXT("pitch"), Note.Rotation.Pitch);
	Rotation->SetNumberField(TEXT("yaw"), Note.Rotation.Yaw);
	Rotation->SetNumberField(TEXT("roll"), Note.Rotation.Roll);
	Root->SetObjectField(TEXT("rotation"), Rotation);

	Root->SetNumberField(TEXT("gameTime"), Note.GameTimeSeconds);
	Root->SetNumberField(TEXT("realTime"), Note.RealTimeSeconds);
	Root->SetNumberField(TEXT("avgFps"), Note.AverageFps);
	Root->SetStringField(TEXT("commit"), Note.Commit);
	Root->SetStringField(TEXT("buildConfiguration"), Note.BuildConfiguration);
	Root->SetStringField(TEXT("netMode"), Note.NetMode);
	Root->SetStringField(TEXT("timestamp"), Note.Timestamp.ToIso8601());
	Root->SetStringField(TEXT("screenshot"), bHasScreenshot ? TEXT("screenshot.png") : TEXT(""));

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool FPlaytestNoteWriter::WriteNote(const FString& RootDir, const FPlaytestNoteData& Note, int32 Width, int32 Height, const TArray<FColor>& Pixels, FString& OutFolder, FString& OutError)
{
	IFileManager& FileManager = IFileManager::Get();

	// pick a unique folder
	const FString BaseName = MakeFolderName(Note.Timestamp);
	FString Folder = FPaths::ConvertRelativePathToFull(RootDir / BaseName);
	for (int32 Suffix = 2; FileManager.DirectoryExists(*Folder); ++Suffix)
	{
		Folder = FPaths::ConvertRelativePathToFull(RootDir / FString::Printf(TEXT("%s-%d"), *BaseName, Suffix));
	}

	if (!FileManager.MakeDirectory(*Folder, /*Tree*/ true))
	{
		OutError = FString::Printf(TEXT("Could not create folder %s"), *Folder);
		return false;
	}

	// screenshot first, so note.json can say whether it exists
	bool bHasScreenshot = false;
	if (Pixels.Num() > 0)
	{
		if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
		{
			OutError = FString::Printf(TEXT("Screenshot size mismatch: %dx%d with %d pixels"), Width, Height, Pixels.Num());
			return false;
		}

		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		if (Png.Num() == 0 || !FFileHelper::SaveArrayToFile(Png, *(Folder / TEXT("screenshot.png"))))
		{
			OutError = TEXT("Could not write screenshot.png");
			return false;
		}
		bHasScreenshot = true;
	}

	const FString Json = ToJsonString(Note, bHasScreenshot);
	if (!FFileHelper::SaveStringToFile(Json, *(Folder / TEXT("note.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = TEXT("Could not write note.json");
		return false;
	}

	OutFolder = Folder;
	return true;
}

FString FPlaytestNoteWriter::ReadGitCommit(const FString& RepoRoot)
{
	const FString Unknown = TEXT("unknown");
	FString GitDir = RepoRoot / TEXT(".git");

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*GitDir))
	{
		// worktrees and submodules use a ".git" file containing "gitdir: <path>"
		FString GitFile;
		if (!FFileHelper::LoadFileToString(GitFile, *GitDir))
		{
			return Unknown;
		}
		GitFile.TrimStartAndEndInline();
		if (!GitFile.RemoveFromStart(TEXT("gitdir:")))
		{
			return Unknown;
		}
		GitFile.TrimStartAndEndInline();
		GitDir = FPaths::IsRelative(GitFile) ? RepoRoot / GitFile : GitFile;
	}

	FString Head;
	if (!FFileHelper::LoadFileToString(Head, *(GitDir / TEXT("HEAD"))))
	{
		return Unknown;
	}
	Head.TrimStartAndEndInline();

	// detached HEAD holds the hash directly
	if (!Head.StartsWith(TEXT("ref:")))
	{
		return Head.IsEmpty() ? Unknown : Head;
	}

	FString Ref = Head.RightChop(4);
	Ref.TrimStartAndEndInline();

	FString Hash;
	if (FFileHelper::LoadFileToString(Hash, *(GitDir / Ref)))
	{
		Hash.TrimStartAndEndInline();
		if (!Hash.IsEmpty())
		{
			return Hash;
		}
	}

	// fall back to packed refs: lines of "<hash> <ref>"
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *(GitDir / TEXT("packed-refs"))))
	{
		for (const FString& Line : Lines)
		{
			FString LineHash, LineRef;
			if (!Line.StartsWith(TEXT("#")) && Line.Split(TEXT(" "), &LineHash, &LineRef) && LineRef.TrimStartAndEnd() == Ref)
			{
				return LineHash.TrimStartAndEnd();
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

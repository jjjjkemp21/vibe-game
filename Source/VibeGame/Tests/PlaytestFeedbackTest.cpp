#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Playtest/PlaytestNoteWriter.h"
#include "Playtest/PlaytestFeedbackRules.h"
#include "Playtest/PlaytestFeedbackSubsystem.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && VIBEGAME_WITH_PLAYTEST_FEEDBACK

namespace PlaytestFeedbackTest
{
	/** Unique temp folder under Saved/Automation (never Saved/Playtest) */
	static FString MakeTempDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir() / TEXT("PlaytestFeedback") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	/** Writes the note without a screenshot and returns the parsed note.json (null on failure) */
	static TSharedPtr<FJsonObject> WriteAndParse(FAutomationTestBase& Test, const FString& RootDir, const FPlaytestNoteData& Note, FString* OutFolder = nullptr, FString* OutWarning = nullptr)
	{
		FString Folder, Error, Warning;
		const bool bWritten = FPlaytestNoteWriter::WriteNote(RootDir, Note, 0, 0, TArray<FColor>(), Folder, Error, &Warning);
		if (!Test.TestTrue(FString::Printf(TEXT("WriteNote succeeds (%s)"), *Error), bWritten))
		{
			return nullptr;
		}
		if (OutFolder)
		{
			*OutFolder = Folder;
		}
		if (OutWarning)
		{
			*OutWarning = Warning;
		}
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root))
		{
			Test.AddError(TEXT("note.json could not be read back or parsed"));
			return nullptr;
		}
		return Root;
	}

	static bool NearlyEqualTime(const FDateTime& A, const FDateTime& B, const double ToleranceSeconds)
	{
		return FMath::Abs((A - B).GetTotalSeconds()) <= ToleranceSeconds;
	}

	/** A running PIE/standalone candidate with the given PIE instance number */
	static FPlaytestFeedbackCandidate Running(const int32 PIEInstance)
	{
		FPlaytestFeedbackCandidate Candidate;
		Candidate.bSessionRunning = true;
		Candidate.PIEInstance = PIEInstance;
		return Candidate;
	}

	static const TCHAR* ToText(const EPlaytestFeedbackKeyAction Action)
	{
		switch (Action)
		{
		case EPlaytestFeedbackKeyAction::PassThrough: return TEXT("PassThrough");
		case EPlaytestFeedbackKeyAction::Consume: return TEXT("Consume");
		case EPlaytestFeedbackKeyAction::OpenNote: return TEXT("OpenNote");
		}
		return TEXT("?");
	}

	static const TCHAR* ToText(const EPlaytestNoteBoxKeyAction Action)
	{
		switch (Action)
		{
		case EPlaytestNoteBoxKeyAction::PassThrough: return TEXT("PassThrough");
		case EPlaytestNoteBoxKeyAction::Submit: return TEXT("Submit");
		case EPlaytestNoteBoxKeyAction::Cancel: return TEXT("Cancel");
		case EPlaytestNoteBoxKeyAction::Consume: return TEXT("Consume");
		}
		return TEXT("?");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestNoteWritingTest,
	"Project.Playtest.NoteWriting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestNoteWritingTest::RunTest(const FString& Parameters)
{
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();

	FPlaytestNoteData Note;
	Note.Text = TEXT("The dock feels too slippery \"here\"");
	Note.LevelName = TEXT("L_TestLevel");
	Note.Location = FVector(100.5, -200.25, 50.0);
	Note.Rotation = FRotator(-10.0, 90.0, 0.0);
	Note.GameTimeSeconds = 12.5;
	Note.RealTimeSeconds = 13.0;
	Note.AverageFps = 59.5f;
	Note.Commit = TEXT("abc123def");
	Note.BuildConfiguration = TEXT("Development");
	Note.NetMode = TEXT("Standalone");
	Note.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9);

	// fake 4x2 screenshot
	const int32 Width = 4;
	const int32 Height = 2;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(255, 128, 0, 255), Width * Height);

	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(RootDir, Note, Width, Height, Pixels, Folder, Error);
	TestTrue(FString::Printf(TEXT("WriteNote succeeds (%s)"), *Error), bWritten);

	TestEqual(TEXT("Folder name is yyyyMMdd-HHmmss"), FPaths::GetCleanFilename(Folder), FString(TEXT("20260922-210509")));
	TestTrue(TEXT("Folder exists"), IFileManager::Get().DirectoryExists(*Folder));

	// screenshot.png is a real PNG
	TArray<uint8> PngBytes;
	TestTrue(TEXT("screenshot.png exists"), FFileHelper::LoadFileToArray(PngBytes, *(Folder / TEXT("screenshot.png"))));
	TestTrue(TEXT("screenshot.png has the PNG signature"), PngBytes.Num() > 8 && PngBytes[0] == 0x89 && PngBytes[1] == 'P' && PngBytes[2] == 'N' && PngBytes[3] == 'G');

	// note.json fields
	FString Json;
	TestTrue(TEXT("note.json exists"), FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json"))));
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("note.json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());

	if (Root.IsValid())
	{
		TestEqual(TEXT("text"), Root->GetStringField(TEXT("text")), Note.Text);
		TestEqual(TEXT("level"), Root->GetStringField(TEXT("level")), Note.LevelName);
		TestEqual(TEXT("commit"), Root->GetStringField(TEXT("commit")), Note.Commit);
		TestEqual(TEXT("screenshot"), Root->GetStringField(TEXT("screenshot")), FString(TEXT("screenshot.png")));
		TestTrue(TEXT("gameTime"), FMath::IsNearlyEqual(Root->GetNumberField(TEXT("gameTime")), 12.5));
		TestTrue(TEXT("avgFps"), FMath::IsNearlyEqual(Root->GetNumberField(TEXT("avgFps")), 59.5, 0.01));

		const TSharedPtr<FJsonObject>* Location = nullptr;
		if (TestTrue(TEXT("location object"), Root->TryGetObjectField(TEXT("location"), Location)))
		{
			TestTrue(TEXT("location.x"), FMath::IsNearlyEqual((*Location)->GetNumberField(TEXT("x")), 100.5));
			TestTrue(TEXT("location.y"), FMath::IsNearlyEqual((*Location)->GetNumberField(TEXT("y")), -200.25));
			TestTrue(TEXT("location.z"), FMath::IsNearlyEqual((*Location)->GetNumberField(TEXT("z")), 50.0));
		}

		const TSharedPtr<FJsonObject>* Rotation = nullptr;
		if (TestTrue(TEXT("rotation object"), Root->TryGetObjectField(TEXT("rotation"), Rotation)))
		{
			TestTrue(TEXT("rotation.pitch"), FMath::IsNearlyEqual((*Rotation)->GetNumberField(TEXT("pitch")), -10.0));
			TestTrue(TEXT("rotation.yaw"), FMath::IsNearlyEqual((*Rotation)->GetNumberField(TEXT("yaw")), 90.0));
			TestTrue(TEXT("rotation.roll"), FMath::IsNearlyEqual((*Rotation)->GetNumberField(TEXT("roll")), 0.0));
		}
	}

	// same timestamp again gets a unique folder; no pixels means no screenshot
	FString SecondFolder;
	TestTrue(TEXT("Second note without screenshot succeeds"), FPlaytestNoteWriter::WriteNote(RootDir, Note, 0, 0, TArray<FColor>(), SecondFolder, Error));
	TestEqual(TEXT("Second folder gets a suffix"), FPaths::GetCleanFilename(SecondFolder), FString(TEXT("20260922-210509-2")));
	TestFalse(TEXT("No screenshot.png without pixels"), FPaths::FileExists(SecondFolder / TEXT("screenshot.png")));
	TestTrue(TEXT("note.json written without pixels"), FPaths::FileExists(SecondFolder / TEXT("note.json")));

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	TestFalse(TEXT("Temp folder cleaned up"), IFileManager::Get().DirectoryExists(*RootDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestGitCommitTest,
	"Project.Playtest.GitCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestGitCommitTest::RunTest(const FString& Parameters)
{
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();
	const FString Hash = TEXT("0123456789abcdef0123456789abcdef01234567");
	const FString PackedHash = TEXT("fedcba9876543210fedcba9876543210fedcba98");

	TestEqual(TEXT("No .git gives unknown"), FPlaytestNoteWriter::ReadGitCommit(RootDir / TEXT("NoRepo")), FString(TEXT("unknown")));

	// loose ref
	const FString LooseRepo = RootDir / TEXT("Loose");
	FFileHelper::SaveStringToFile(TEXT("ref: refs/heads/main\n"), *(LooseRepo / TEXT(".git/HEAD")));
	FFileHelper::SaveStringToFile(Hash + TEXT("\n"), *(LooseRepo / TEXT(".git/refs/heads/main")));
	TestEqual(TEXT("Loose ref"), FPlaytestNoteWriter::ReadGitCommit(LooseRepo), Hash);

	// packed ref
	const FString PackedRepo = RootDir / TEXT("Packed");
	FFileHelper::SaveStringToFile(TEXT("ref: refs/heads/main\n"), *(PackedRepo / TEXT(".git/HEAD")));
	FFileHelper::SaveStringToFile(TEXT("# pack-refs with: peeled fully-peeled sorted\n") + PackedHash + TEXT(" refs/heads/main\n"), *(PackedRepo / TEXT(".git/packed-refs")));
	TestEqual(TEXT("Packed ref"), FPlaytestNoteWriter::ReadGitCommit(PackedRepo), PackedHash);

	// detached head
	const FString DetachedRepo = RootDir / TEXT("Detached");
	FFileHelper::SaveStringToFile(Hash + TEXT("\n"), *(DetachedRepo / TEXT(".git/HEAD")));
	TestEqual(TEXT("Detached HEAD"), FPlaytestNoteWriter::ReadGitCommit(DetachedRepo), Hash);

	// the real project repo resolves to a 40-char hash
	const FString ProjectCommit = FPlaytestNoteWriter::ReadGitCommit(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	TestEqual(TEXT("Project commit is a 40-char hash"), ProjectCommit.Len(), 40);

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestFpsAverageTest,
	"Project.Playtest.FpsAverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestFpsAverageTest::RunTest(const FString& Parameters)
{
	FPlaytestFpsTracker Tracker(5.0);
	TestEqual(TEXT("Empty tracker is 0 FPS"), Tracker.GetAverageFps(), 0.0f);

	// 5 s at 30 FPS, then 5 s at 60 FPS: only the last 5 s should count
	double Now = 0.0;
	for (int32 i = 0; i < 150; ++i)
	{
		Now += 1.0 / 30.0;
		Tracker.AddFrame(Now, 1.0 / 30.0);
	}
	TestTrue(TEXT("30 FPS phase averages ~30"), FMath::IsNearlyEqual(Tracker.GetAverageFps(), 30.0f, 0.5f));

	for (int32 i = 0; i < 300; ++i)
	{
		Now += 1.0 / 60.0;
		Tracker.AddFrame(Now, 1.0 / 60.0);
	}
	TestTrue(FString::Printf(TEXT("After 5 s at 60 FPS the average is ~60 (got %.2f)"), Tracker.GetAverageFps()), FMath::IsNearlyEqual(Tracker.GetAverageFps(), 60.0f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestBlankRootRejectedTest,
	"Project.Playtest.BlankRootRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestBlankRootRejectedTest::RunTest(const FString& Parameters)
{
	// T003-B3: an empty or blank root must be rejected, never resolved against the engine binaries folder
	FPlaytestNoteData Note;
	Note.Text = TEXT("blank root");
	Note.Timestamp = FDateTime(2002, 3, 4, 5, 6, 7);
	const FString Stray = FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::BaseDir()) / TEXT("20020304-050607"));

	for (const TCHAR* Root : { TEXT(""), TEXT("   "), TEXT("\t \r\n") })
	{
		const FString Shown = FString(Root).ReplaceCharWithEscapedChar();
		FString Folder, Error, Warning;
		const bool bWritten = FPlaytestNoteWriter::WriteNote(Root, Note, 0, 0, TArray<FColor>(), Folder, Error, &Warning);
		TestFalse(FString::Printf(TEXT("Root '%s' is rejected"), *Shown), bWritten);
		TestFalse(FString::Printf(TEXT("Root '%s': OutError explains why"), *Shown), Error.IsEmpty());
		TestTrue(FString::Printf(TEXT("Root '%s': no folder is reported"), *Shown), Folder.IsEmpty());
		TestFalse(FString::Printf(TEXT("Root '%s': nothing was written next to the executable"), *Shown), IFileManager::Get().DirectoryExists(*Stray));
		if (bWritten && !Folder.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*Folder, /*RequireExists*/ false, /*Tree*/ true);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestNonFiniteWrittenAsNullTest,
	"Project.Playtest.NonFiniteWrittenAsNull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestNonFiniteWrittenAsNullTest::RunTest(const FString& Parameters)
{
	// T003-B1: NaN/Inf are written as JSON null (strict JSON for Python/PowerShell/jq) and reported as one warning
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();

	FPlaytestNoteData Note;
	Note.Text = TEXT("fell out of the world");
	// members set directly: FVector/FRotator constructors run NaN diagnostics in Debug builds
	Note.Location.X = std::numeric_limits<double>::quiet_NaN();
	Note.Location.Y = 25.5;
	Note.Location.Z = 10.0;
	Note.Rotation.Pitch = 5.0;
	Note.Rotation.Yaw = std::numeric_limits<double>::infinity();
	Note.GameTimeSeconds = std::numeric_limits<double>::infinity();
	Note.AverageFps = -std::numeric_limits<float>::infinity();
	Note.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9);

	FString Folder, Error, Warning;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(RootDir, Note, 0, 0, TArray<FColor>(), Folder, Error, &Warning);
	if (TestTrue(FString::Printf(TEXT("WriteNote succeeds (%s)"), *Error), bWritten))
	{
		for (const TCHAR* Field : { TEXT("location.x"), TEXT("rotation.yaw"), TEXT("gameTime"), TEXT("avgFps") })
		{
			TestTrue(FString::Printf(TEXT("Warning names %s (got '%s')"), Field, *Warning), Warning.Contains(Field));
		}
		TestFalse(TEXT("Warning does not name finite fields"), Warning.Contains(TEXT("location.y")));

		FString Json;
		TSharedPtr<FJsonObject> Root;
		TestTrue(TEXT("note.json exists"), FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json"))));
		if (TestTrue(TEXT("note.json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
		{
			const TSharedPtr<FJsonObject> Location = Root->GetObjectField(TEXT("location"));
			const TSharedPtr<FJsonObject> Rotation = Root->GetObjectField(TEXT("rotation"));
			const auto IsNull = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
			{
				const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Name) : nullptr;
				return Value.IsValid() && Value->Type == EJson::Null;
			};
			TestTrue(TEXT("location.x (NaN) is null"), IsNull(Location, TEXT("x")));
			TestTrue(TEXT("rotation.yaw (+Inf) is null"), IsNull(Rotation, TEXT("yaw")));
			TestTrue(TEXT("gameTime (+Inf) is null"), IsNull(Root, TEXT("gameTime")));
			TestTrue(TEXT("avgFps (-Inf) is null"), IsNull(Root, TEXT("avgFps")));
			TestTrue(TEXT("location.y stays a number"), Location.IsValid() && FMath::IsNearlyEqual(Location->GetNumberField(TEXT("y")), 25.5));
			TestEqual(TEXT("text is kept"), Root->GetStringField(TEXT("text")), Note.Text);
		}
	}

	// a normal note has no warning
	FPlaytestNoteData Normal = Note;
	Normal.Location = FVector(1.0, 2.0, 3.0);
	Normal.Rotation = FRotator::ZeroRotator;
	Normal.GameTimeSeconds = 12.0;
	Normal.AverageFps = 60.0f;
	FString NormalFolder, NormalError, NormalWarning = TEXT("stale");
	TestTrue(TEXT("Normal note is written"), FPlaytestNoteWriter::WriteNote(RootDir, Normal, 0, 0, TArray<FColor>(), NormalFolder, NormalError, &NormalWarning));
	TestTrue(FString::Printf(TEXT("Normal note has no warning (got '%s')"), *NormalWarning), NormalWarning.IsEmpty());

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestBadScreenshotStillSavesNoteTest,
	"Project.Playtest.BadScreenshotStillSavesNote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestBadScreenshotStillSavesNoteTest::RunTest(const FString& Parameters)
{
	// T003-B2: the typed note matters more than the image: a bad screenshot is dropped, note.json is still saved
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();

	FPlaytestNoteData Note;
	Note.Text = TEXT("keep this text");
	Note.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9);
	TArray<FColor> Pixels;
	Pixels.Init(FColor::Red, 5); // 4x2 needs 8

	FString Folder, Error, Warning;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(RootDir, Note, 4, 2, Pixels, Folder, Error, &Warning);
	if (TestTrue(FString::Printf(TEXT("WriteNote succeeds without the screenshot (%s)"), *Error), bWritten))
	{
		TestTrue(FString::Printf(TEXT("Warning says the screenshot was dropped (got '%s')"), *Warning), Warning.Contains(TEXT("screenshot")));
		TestFalse(TEXT("No screenshot.png"), FPaths::FileExists(Folder / TEXT("screenshot.png")));

		FString Json;
		TSharedPtr<FJsonObject> Root;
		TestTrue(TEXT("note.json exists"), FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json"))));
		if (TestTrue(TEXT("note.json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
		{
			TestEqual(TEXT("text is kept"), Root->GetStringField(TEXT("text")), Note.Text);
			TestEqual(TEXT("screenshot is empty"), Root->GetStringField(TEXT("screenshot")), FString());
		}
	}

	TArray<FString> Entries;
	IFileManager::Get().FindFiles(Entries, *(RootDir / TEXT("*")), /*Files*/ true, /*Directories*/ true);
	TestEqual(TEXT("Exactly one note folder"), Entries.Num(), 1);

	// a valid screenshot gives no warning
	TArray<FColor> Good;
	Good.Init(FColor::Blue, 8);
	FString GoodFolder, GoodError, GoodWarning = TEXT("stale");
	TestTrue(TEXT("Valid screenshot note is written"), FPlaytestNoteWriter::WriteNote(RootDir, Note, 4, 2, Good, GoodFolder, GoodError, &GoodWarning));
	TestTrue(TEXT("Valid screenshot is saved"), FPaths::FileExists(GoodFolder / TEXT("screenshot.png")));
	TestTrue(FString::Printf(TEXT("Valid screenshot gives no warning (got '%s')"), *GoodWarning), GoodWarning.IsEmpty());

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestGitCommitValidationTest,
	"Project.Playtest.GitCommitValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestGitCommitValidationTest::RunTest(const FString& Parameters)
{
	// T003-B4: only a full commit id (40 or 64 hex) or "unknown", never file garbage or a stale id
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();
	const FString Unknown = TEXT("unknown");
	const FString Sha1 = TEXT("0123456789abcdef0123456789abcdef01234567");
	const FString Sha256 = Sha1 + TEXT("89abcdef0123456789abcdef");

	TestTrue(TEXT("40 hex is a hash"), FPlaytestNoteWriter::IsCommitHash(Sha1));
	TestTrue(TEXT("64 hex is a hash"), FPlaytestNoteWriter::IsCommitHash(Sha256));
	TestTrue(TEXT("Upper-case hex is a hash"), FPlaytestNoteWriter::IsCommitHash(Sha1.ToUpper()));
	TestFalse(TEXT("39 hex is not a hash"), FPlaytestNoteWriter::IsCommitHash(Sha1.LeftChop(1)));
	TestFalse(TEXT("41 hex is not a hash"), FPlaytestNoteWriter::IsCommitHash(Sha1 + TEXT("0")));
	TestFalse(TEXT("Non-hex character is not a hash"), FPlaytestNoteWriter::IsCommitHash(Sha1.LeftChop(1) + TEXT("g")));
	TestFalse(TEXT("Empty is not a hash"), FPlaytestNoteWriter::IsCommitHash(FString()));

	const auto MakeRepo = [&RootDir](const TCHAR* Name, const FString& Head, const TCHAR* LooseMain, const TCHAR* Packed)
	{
		const FString Repo = RootDir / Name;
		FFileHelper::SaveStringToFile(Head, *(Repo / TEXT(".git/HEAD")));
		IFileManager::Get().MakeDirectory(*(Repo / TEXT(".git/refs/heads")), /*Tree*/ true);
		if (LooseMain)
		{
			FFileHelper::SaveStringToFile(LooseMain, *(Repo / TEXT(".git/refs/heads/main")));
		}
		if (Packed)
		{
			FFileHelper::SaveStringToFile(Packed, *(Repo / TEXT(".git/packed-refs")));
		}
		return Repo;
	};

	const FString PackedMain = TEXT("# pack-refs with: peeled fully-peeled sorted\n") + Sha1 + TEXT(" refs/heads/main\n");
	TestEqual(TEXT("SHA-256 detached HEAD"), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("Sha256Detached"), Sha256 + TEXT("\n"), nullptr, nullptr)), Sha256);
	TestEqual(TEXT("SHA-256 loose ref"), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("Sha256Loose"), TEXT("ref: refs/heads/main\n"), *(Sha256 + TEXT("\n")), nullptr)), Sha256);
	TestEqual(TEXT("Broken loose ref does not fall back to a stale packed id"), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("BrokenLoose"), TEXT("ref: refs/heads/main\n"), TEXT("garbage\n"), *PackedMain)), Unknown);
	TestEqual(TEXT("Packed entry with a garbage hash"), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("BadPacked"), TEXT("ref: refs/heads/main\n"), nullptr, TEXT("nothex refs/heads/main\n"))), Unknown);
	TestEqual(TEXT("Short detached HEAD"), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("ShortDetached"), TEXT("abc123\n"), nullptr, nullptr)), Unknown);
	TestEqual(TEXT("HEAD ref outside refs/"), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("OutsideRefs"), TEXT("ref: HEAD\n"), nullptr, nullptr)), Unknown);
	TestEqual(TEXT("HEAD ref escaping with .."), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("DotDotRef"), TEXT("ref: refs/../../secret\n"), nullptr, nullptr)), Unknown);
	TestEqual(TEXT("Packed ref still resolves"), FPlaytestNoteWriter::ReadGitCommit(MakeRepo(TEXT("PackedOk"), TEXT("ref: refs/heads/main\n"), nullptr, *PackedMain)), Sha1);

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestSubsystemDefaultsTest,
	"Project.Playtest.SubsystemDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestSubsystemDefaultsTest::RunTest(const FString& Parameters)
{
	const UPlaytestFeedbackSubsystem* Defaults = GetDefault<UPlaytestFeedbackSubsystem>();
	TestTrue(TEXT("Feedback key is F8"), Defaults->FeedbackKey == EKeys::F8);
	TestTrue(TEXT("FPS window is 5 s"), FMath::IsNearlyEqual(Defaults->FpsWindowSeconds, 5.0f));
	TestTrue(TEXT("Saved toast shows 7 s"), FMath::IsNearlyEqual(Defaults->ToastSeconds, 7.0f));
	TestTrue(TEXT("Failure toast shows 12 s"), FMath::IsNearlyEqual(Defaults->FailureToastSeconds, 12.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestUtcTimestampTest,
	"Project.Playtest.UtcTimestamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestUtcTimestampTest::RunTest(const FString& Parameters)
{
	// round 2: note.json "timestamp" is real UTC (ISO 8601 with Z); the folder name stays local time
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();

	// format
	TestEqual(TEXT("ISO 8601 UTC with milliseconds and Z"), FPlaytestNoteWriter::MakeUtcTimestamp(FDateTime(2026, 9, 23, 2, 5, 9, 250)), FString(TEXT("2026-09-23T02:05:09.250Z")));
	TestEqual(TEXT("Leading zeros are kept"), FPlaytestNoteWriter::MakeUtcTimestamp(FDateTime(2026, 1, 2, 3, 4, 5, 6)), FString(TEXT("2026-01-02T03:04:05.006Z")));

	// the machine's offset: whole minutes, within the real time zone range
	const FTimespan Offset = FPlaytestNoteWriter::GetLocalUtcOffset();
	TestEqual(TEXT("Offset is whole minutes"), Offset.GetTicks() % ETimespan::TicksPerMinute, static_cast<int64>(0));
	TestTrue(FString::Printf(TEXT("Offset %s is within UTC-12:00..UTC+14:00"), *Offset.ToString()), Offset >= FTimespan::FromHours(-12.0) && Offset <= FTimespan::FromHours(14.0));
	TestTrue(TEXT("Offset matches Now - UtcNow"), FMath::Abs((FDateTime::Now() - FDateTime::UtcNow() - Offset).GetTotalSeconds()) < 1.0);
	AddInfo(FString::Printf(TEXT("Local UTC offset on this machine: %s"), *Offset.ToString()));

	// StampNow: one instant, UTC and local exactly the offset apart
	FPlaytestNoteData Live;
	const FDateTime Before = FDateTime::UtcNow();
	FPlaytestNoteWriter::StampNow(Live);
	const FDateTime After = FDateTime::UtcNow();
	TestTrue(TEXT("TimestampUtc is UtcNow"), Live.TimestampUtc >= Before && Live.TimestampUtc <= After);
	TestTrue(TEXT("Timestamp is local time (Now)"), PlaytestFeedbackTest::NearlyEqualTime(Live.Timestamp, FDateTime::Now(), 5.0));
	TestEqual(TEXT("Local - UTC is exactly the offset"), (Live.Timestamp - Live.TimestampUtc).GetTicks(), FPlaytestNoteWriter::GetLocalUtcOffset().GetTicks());

	// a live-stamped note: the folder is local, the json timestamp is UTC (parsed back, it is the offset away from local)
	Live.Text = TEXT("utc check");
	FString LiveFolder;
	if (const TSharedPtr<FJsonObject> Root = PlaytestFeedbackTest::WriteAndParse(*this, RootDir, Live, &LiveFolder))
	{
		const FString Stamp = Root->GetStringField(TEXT("timestamp"));
		TestTrue(FString::Printf(TEXT("timestamp ends with Z (%s)"), *Stamp), Stamp.EndsWith(TEXT("Z")));
		FDateTime Parsed;
		if (TestTrue(FString::Printf(TEXT("timestamp parses as ISO 8601 (%s)"), *Stamp), FDateTime::ParseIso8601(*Stamp, Parsed)))
		{
			TestTrue(TEXT("Parsed timestamp is the UTC instant (ms precision)"), PlaytestFeedbackTest::NearlyEqualTime(Parsed, Live.TimestampUtc, 0.001));
			TestTrue(TEXT("Local (folder) time - json timestamp = the offset"), FMath::Abs((Live.Timestamp - Parsed - Offset).GetTotalSeconds()) <= 0.001);
		}
		TestTrue(FString::Printf(TEXT("Folder name is local time (%s)"), *FPaths::GetCleanFilename(LiveFolder)), FPaths::GetCleanFilename(LiveFolder).StartsWith(FPlaytestNoteWriter::MakeFolderName(Live.Timestamp)));
	}

	// explicit values: folder from the local time, json from the UTC time (5 h apart, across midnight)
	FPlaytestNoteData Fixed;
	Fixed.Text = TEXT("fixed times");
	Fixed.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9);
	Fixed.TimestampUtc = FDateTime(2026, 9, 23, 2, 5, 9, 250);
	FString FixedFolder;
	if (const TSharedPtr<FJsonObject> Root = PlaytestFeedbackTest::WriteAndParse(*this, RootDir, Fixed, &FixedFolder))
	{
		TestEqual(TEXT("Folder name uses the local time"), FPaths::GetCleanFilename(FixedFolder), FString(TEXT("20260922-210509")));
		TestEqual(TEXT("json timestamp uses the UTC time"), Root->GetStringField(TEXT("timestamp")), FString(TEXT("2026-09-23T02:05:09.250Z")));
	}

	// only the local time set (older callers): the UTC value is derived with the current offset
	FPlaytestNoteData LocalOnly;
	LocalOnly.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9);
	TestEqual(TEXT("Derived UTC timestamp"), FPlaytestNoteWriter::GetUtcTimestampText(LocalOnly), FPlaytestNoteWriter::MakeUtcTimestamp(LocalOnly.Timestamp - Offset));

	// nothing set: empty (never a fake year-1 date, never an out-of-range FDateTime)
	TestEqual(TEXT("No timestamps gives an empty timestamp"), FPlaytestNoteWriter::GetUtcTimestampText(FPlaytestNoteData()), FString());

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestCameraInNoteTest,
	"Project.Playtest.CameraInNote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestCameraInNoteTest::RunTest(const FString& Parameters)
{
	// designer A-S2: note.json records the view (camera location and rotation, pitch included) next to the pawn fields
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();

	FPlaytestNoteData Note;
	Note.Text = TEXT("I couldn't see the bobber");
	Note.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9);
	Note.Location = FVector(100.0, 200.0, 90.0);
	Note.Rotation = FRotator(0.0, 45.0, 0.0);
	Note.CameraLocation = FVector(110.5, 205.25, 160.0);
	Note.CameraRotation = FRotator(-22.5, 47.0, 1.5);

	if (const TSharedPtr<FJsonObject> Root = PlaytestFeedbackTest::WriteAndParse(*this, RootDir, Note))
	{
		const TSharedPtr<FJsonObject>* Camera = nullptr;
		if (TestTrue(TEXT("camera object"), Root->TryGetObjectField(TEXT("camera"), Camera)))
		{
			const TSharedPtr<FJsonObject>* Location = nullptr;
			if (TestTrue(TEXT("camera.location object"), (*Camera)->TryGetObjectField(TEXT("location"), Location)))
			{
				TestTrue(TEXT("camera.location.x"), FMath::IsNearlyEqual((*Location)->GetNumberField(TEXT("x")), 110.5));
				TestTrue(TEXT("camera.location.y"), FMath::IsNearlyEqual((*Location)->GetNumberField(TEXT("y")), 205.25));
				TestTrue(TEXT("camera.location.z"), FMath::IsNearlyEqual((*Location)->GetNumberField(TEXT("z")), 160.0));
			}
			const TSharedPtr<FJsonObject>* Rotation = nullptr;
			if (TestTrue(TEXT("camera.rotation object"), (*Camera)->TryGetObjectField(TEXT("rotation"), Rotation)))
			{
				TestTrue(TEXT("camera.rotation.pitch (looking down)"), FMath::IsNearlyEqual((*Rotation)->GetNumberField(TEXT("pitch")), -22.5));
				TestTrue(TEXT("camera.rotation.yaw"), FMath::IsNearlyEqual((*Rotation)->GetNumberField(TEXT("yaw")), 47.0));
				TestTrue(TEXT("camera.rotation.roll"), FMath::IsNearlyEqual((*Rotation)->GetNumberField(TEXT("roll")), 1.5));
			}
		}

		// the pawn fields stay as they were
		const TSharedPtr<FJsonObject>* PawnLocation = nullptr;
		if (TestTrue(TEXT("location object kept"), Root->TryGetObjectField(TEXT("location"), PawnLocation)))
		{
			TestTrue(TEXT("location.z is the pawn's"), FMath::IsNearlyEqual((*PawnLocation)->GetNumberField(TEXT("z")), 90.0));
		}
		const TSharedPtr<FJsonObject>* PawnRotation = nullptr;
		if (TestTrue(TEXT("rotation object kept"), Root->TryGetObjectField(TEXT("rotation"), PawnRotation)))
		{
			TestTrue(TEXT("rotation.yaw is the pawn's"), FMath::IsNearlyEqual((*PawnRotation)->GetNumberField(TEXT("yaw")), 45.0));
		}
	}

	// a non-finite camera value is written as null and named in the warning (like the other numbers)
	FPlaytestNoteData Broken = Note;
	Broken.CameraRotation.Pitch = std::numeric_limits<double>::quiet_NaN();
	FString Warning;
	if (const TSharedPtr<FJsonObject> Root = PlaytestFeedbackTest::WriteAndParse(*this, RootDir, Broken, nullptr, &Warning))
	{
		TestTrue(FString::Printf(TEXT("Warning names camera.rotation.pitch (got '%s')"), *Warning), Warning.Contains(TEXT("camera.rotation.pitch")));
		const TSharedPtr<FJsonObject> Camera = Root->GetObjectField(TEXT("camera"));
		const TSharedPtr<FJsonObject> Rotation = Camera.IsValid() ? Camera->GetObjectField(TEXT("rotation")) : nullptr;
		const TSharedPtr<FJsonValue> Pitch = Rotation.IsValid() ? Rotation->TryGetField(TEXT("pitch")) : nullptr;
		TestTrue(TEXT("camera.rotation.pitch (NaN) is null"), Pitch.IsValid() && Pitch->Type == EJson::Null);
	}

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestGitWorktreeTest,
	"Project.Playtest.GitWorktree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestGitWorktreeTest::RunTest(const FString& Parameters)
{
	// B5: a lane is a git worktree: <lane>/.git is a FILE "gitdir: <main>/.git/worktrees/<name>"; HEAD is there,
	// branch refs and packed-refs are in the common dir named by "commondir" (usually "../..")
	const FString RootDir = PlaytestFeedbackTest::MakeTempDir();
	const FString Unknown = TEXT("unknown");
	const FString HashLane = FString::ChrN(40, TEXT('a'));
	const FString HashPacked = FString::ChrN(40, TEXT('b'));
	const FString HashDetached = FString::ChrN(40, TEXT('c'));
	const FString HashBisectWorktree = FString::ChrN(40, TEXT('d'));
	const FString HashBisectCommon = FString::ChrN(40, TEXT('e'));
	const FString HashSha256 = FString::ChrN(64, TEXT('f'));

	const auto Write = [](const FString& Path, const FString& Content)
	{
		FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceAnsi);
	};

	// the main repository (common dir)
	const FString MainGit = RootDir / TEXT("Main/.git");
	Write(MainGit / TEXT("HEAD"), TEXT("ref: refs/heads/main\n"));
	Write(MainGit / TEXT("refs/heads/lane/eng2"), HashLane + TEXT("\n"));
	Write(MainGit / TEXT("refs/heads/lane/broken"), TEXT("not-a-hash\n"));
	Write(MainGit / TEXT("refs/heads/lane/sha256"), HashSha256 + TEXT("\n"));
	Write(MainGit / TEXT("refs/bisect/bad"), HashBisectCommon + TEXT("\n"));
	Write(MainGit / TEXT("packed-refs"), TEXT("# pack-refs with: peeled fully-peeled sorted\n")
		+ HashPacked + TEXT(" refs/heads/lane/packed\n")
		+ HashPacked + TEXT(" refs/heads/lane/broken\n"));

	// makes <RootDir>/<Name> a worktree: .git file -> GitFileTarget, and <main>/.git/worktrees/<Name>/{HEAD, commondir}
	const auto MakeWorktree = [&](const TCHAR* Name, const FString& GitFileLine, const FString& Head, const TCHAR* CommonDirLine)
	{
		const FString Repo = RootDir / Name;
		const FString WorktreeGitDir = MainGit / TEXT("worktrees") / Name;
		Write(Repo / TEXT(".git"), GitFileLine);
		Write(WorktreeGitDir / TEXT("HEAD"), Head);
		if (CommonDirLine)
		{
			Write(WorktreeGitDir / TEXT("commondir"), CommonDirLine);
		}
		return Repo;
	};
	const auto AbsoluteGitFile = [&](const TCHAR* Name) { return FString::Printf(TEXT("gitdir: %s\n"), *(MainGit / TEXT("worktrees") / Name)); };

	// branch checked out (like lane/eng2): loose ref in the common dir, absolute gitdir, relative commondir
	TestEqual(TEXT("Worktree, loose branch ref via commondir"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Loose"), AbsoluteGitFile(TEXT("Loose")), TEXT("ref: refs/heads/lane/eng2\n"), TEXT("../..\n"))), HashLane);

	// packed branch, RELATIVE gitdir (relative to the worktree root), CRLF everywhere
	TestEqual(TEXT("Worktree, packed branch ref, relative gitdir, CRLF"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Packed"), TEXT("gitdir: ../Main/.git/worktrees/Packed\r\n"), TEXT("ref: refs/heads/lane/packed\r\n"), TEXT("../..\r\n"))), HashPacked);

	// detached HEAD in the worktree's own git dir
	TestEqual(TEXT("Worktree, detached HEAD"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Detached"), AbsoluteGitFile(TEXT("Detached")), HashDetached + TEXT("\n"), TEXT("../..\n"))), HashDetached);

	// ABSOLUTE commondir, written with backslashes (Windows style)
	const FString AbsoluteCommon = FPaths::ConvertRelativePathToFull(MainGit).Replace(TEXT("/"), TEXT("\\")) + TEXT("\n");
	TestEqual(TEXT("Worktree, absolute commondir with backslashes"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("AbsCommon"), AbsoluteGitFile(TEXT("AbsCommon")), TEXT("ref: refs/heads/lane/eng2\n"), *AbsoluteCommon)), HashLane);

	// SHA-256 repositories work through a worktree too
	TestEqual(TEXT("Worktree, SHA-256 branch"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Sha256"), AbsoluteGitFile(TEXT("Sha256")), TEXT("ref: refs/heads/lane/sha256\n"), TEXT("../..\n"))), HashSha256);

	// per-worktree refs (refs/bisect/...) are read from the worktree's git dir, not the common dir
	const FString BisectRepo = MakeWorktree(TEXT("Bisect"), AbsoluteGitFile(TEXT("Bisect")), TEXT("ref: refs/bisect/bad\n"), TEXT("../..\n"));
	Write(MainGit / TEXT("worktrees/Bisect/refs/bisect/bad"), HashBisectWorktree + TEXT("\n"));
	TestEqual(TEXT("Worktree, per-worktree ref stays in the worktree git dir"), FPlaytestNoteWriter::ReadGitCommit(BisectRepo), HashBisectWorktree);

	// B4 validation still holds through a worktree
	TestEqual(TEXT("Worktree, broken loose ref does not fall back to a stale packed id"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Broken"), AbsoluteGitFile(TEXT("Broken")), TEXT("ref: refs/heads/lane/broken\n"), TEXT("../..\n"))), Unknown);
	TestEqual(TEXT("Worktree, commondir pointing nowhere"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("NoCommon"), AbsoluteGitFile(TEXT("NoCommon")), TEXT("ref: refs/heads/lane/eng2\n"), TEXT("../../nowhere\n"))), Unknown);
	TestEqual(TEXT("Worktree, HEAD ref escaping with .."),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Escape"), AbsoluteGitFile(TEXT("Escape")), TEXT("ref: refs/../../HEAD\n"), TEXT("../..\n"))), Unknown);
	TestEqual(TEXT("Worktree, garbage HEAD"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Garbage"), AbsoluteGitFile(TEXT("Garbage")), TEXT("not a commit\n"), TEXT("../..\n"))), Unknown);
	TestEqual(TEXT(".git file without a gitdir line"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("NoGitdir"), TEXT("hello\n"), TEXT("ref: refs/heads/lane/eng2\n"), TEXT("../..\n"))), Unknown);
	TestEqual(TEXT(".git file with an empty gitdir"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("EmptyGitdir"), TEXT("gitdir:   \n"), TEXT("ref: refs/heads/lane/eng2\n"), TEXT("../..\n"))), Unknown);
	TestEqual(TEXT(".git file pointing at a missing git dir"),
		FPlaytestNoteWriter::ReadGitCommit(MakeWorktree(TEXT("Missing"), FString::Printf(TEXT("gitdir: %s\n"), *(RootDir / TEXT("does/not/exist"))), TEXT("ref: refs/heads/lane/eng2\n"), TEXT("../..\n"))), Unknown);

	// a normal repository (no commondir) is unchanged
	TestEqual(TEXT("Main repository without commondir"), FPlaytestNoteWriter::ReadGitCommit(RootDir / TEXT("Main")), Unknown); // refs/heads/main was never written
	Write(MainGit / TEXT("refs/heads/main"), HashLane + TEXT("\n"));
	TestEqual(TEXT("Main repository loose ref"), FPlaytestNoteWriter::ReadGitCommit(RootDir / TEXT("Main")), HashLane);

	IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestFeedbackKeyCaptureTest,
	"Project.Playtest.FeedbackKeyCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestFeedbackKeyCaptureTest::RunTest(const FString& Parameters)
{
	using PlaytestFeedbackTest::ToText;

	// round 2 bug 1: F8 opens the note whenever the session runs, with or without viewport focus; never reaches the editor
	FPlaytestFeedbackKeyContext Base;
	Base.bIsFeedbackKey = true;
	Base.bSessionRunning = true;

	struct FCase
	{
		const TCHAR* Name;
		FPlaytestFeedbackKeyContext Context;
		EPlaytestFeedbackKeyAction Expected;
	};
	auto With = [&Base](TFunctionRef<void(FPlaytestFeedbackKeyContext&)> Change)
	{
		FPlaytestFeedbackKeyContext Context = Base;
		Change(Context);
		return Context;
	};
	const FCase Cases[] = {
		{ TEXT("F8 while playing opens the note (viewport focus is not a condition)"), Base, EPlaytestFeedbackKeyAction::OpenNote },
		{ TEXT("Held F8 repeating is swallowed, opens nothing"), With([](FPlaytestFeedbackKeyContext& C) { C.bIsRepeat = true; }), EPlaytestFeedbackKeyAction::Consume },
		{ TEXT("Other keys pass through"), With([](FPlaytestFeedbackKeyContext& C) { C.bIsFeedbackKey = false; }), EPlaytestFeedbackKeyAction::PassThrough },
		{ TEXT("No running session: F8 passes through"), With([](FPlaytestFeedbackKeyContext& C) { C.bSessionRunning = false; }), EPlaytestFeedbackKeyAction::PassThrough },
		{ TEXT("Ejected (simulating): F8 goes back to the editor to possess"), With([](FPlaytestFeedbackKeyContext& C) { C.bEjected = true; }), EPlaytestFeedbackKeyAction::PassThrough },
		{ TEXT("Typing in a text field: F8 passes through"), With([](FPlaytestFeedbackKeyContext& C) { C.bTypingInText = true; }), EPlaytestFeedbackKeyAction::PassThrough },
		{ TEXT("Typing + repeat: passes through"), With([](FPlaytestFeedbackKeyContext& C) { C.bTypingInText = true; C.bIsRepeat = true; }), EPlaytestFeedbackKeyAction::PassThrough },
		{ TEXT("Ejected + repeat: passes through"), With([](FPlaytestFeedbackKeyContext& C) { C.bEjected = true; C.bIsRepeat = true; }), EPlaytestFeedbackKeyAction::PassThrough },
	};
	for (const FCase& Case : Cases)
	{
		const EPlaytestFeedbackKeyAction Actual = FPlaytestFeedbackRules::DecideFeedbackKey(Case.Context);
		TestTrue(FString::Printf(TEXT("%s (expected %s, got %s)"), Case.Name, ToText(Case.Expected), ToText(Actual)), Actual == Case.Expected);
	}

	// note box keys: Enter saves (never a new line), Shift+Enter is a new line, Esc cancels, F8 is swallowed
	const FKey F8 = EKeys::F8;
	struct FBoxCase
	{
		const TCHAR* Name;
		FKey Key;
		bool bShift;
		EPlaytestNoteBoxKeyAction Expected;
	};
	const FBoxCase BoxCases[] = {
		{ TEXT("Enter saves"), EKeys::Enter, false, EPlaytestNoteBoxKeyAction::Submit },
		{ TEXT("Shift+Enter goes to the field (new line)"), EKeys::Enter, true, EPlaytestNoteBoxKeyAction::PassThrough },
		{ TEXT("Escape cancels"), EKeys::Escape, false, EPlaytestNoteBoxKeyAction::Cancel },
		{ TEXT("Shift+Escape cancels"), EKeys::Escape, true, EPlaytestNoteBoxKeyAction::Cancel },
		{ TEXT("F8 is swallowed"), EKeys::F8, false, EPlaytestNoteBoxKeyAction::Consume },
		{ TEXT("Letters go to the field"), EKeys::A, false, EPlaytestNoteBoxKeyAction::PassThrough },
		{ TEXT("Arrows go to the field"), EKeys::Up, false, EPlaytestNoteBoxKeyAction::PassThrough },
	};
	for (const FBoxCase& Case : BoxCases)
	{
		const EPlaytestNoteBoxKeyAction Actual = FPlaytestFeedbackRules::DecideNoteBoxKey(Case.Key, Case.bShift, F8);
		TestTrue(FString::Printf(TEXT("%s (expected %s, got %s)"), Case.Name, ToText(Case.Expected), ToText(Actual)), Actual == Case.Expected);
	}
	TestTrue(TEXT("An invalid key with an invalid feedback key is not swallowed"), FPlaytestFeedbackRules::DecideNoteBoxKey(EKeys::Invalid, false, EKeys::Invalid) == EPlaytestNoteBoxKeyAction::PassThrough);

	// "typing in text" detection by focused widget type
	for (const TCHAR* Type : { TEXT("SEditableText"), TEXT("SMultiLineEditableText"), TEXT("SEditableTextBox"), TEXT("SMultiLineEditableTextBox") })
	{
		TestTrue(FString::Printf(TEXT("%s is a text entry widget"), Type), FPlaytestFeedbackRules::IsTextEntryWidgetType(FName(Type)));
	}
	for (const TCHAR* Type : { TEXT("SViewport"), TEXT("SButton"), TEXT("SLevelViewport"), TEXT("STextBlock"), TEXT("None") })
	{
		TestFalse(FString::Printf(TEXT("%s is not a text entry widget"), Type), FPlaytestFeedbackRules::IsTextEntryWidgetType(FName(Type)));
	}
	TestFalse(TEXT("NAME_None is not a text entry widget"), FPlaytestFeedbackRules::IsTextEntryWidgetType(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestFeedbackKeyHandlerTest,
	"Project.Playtest.FeedbackKeyHandler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestFeedbackKeyHandlerTest::RunTest(const FString& Parameters)
{
	using PlaytestFeedbackTest::Running;

	// several PIE instances (server + clients): exactly one handles F8
	TestEqual(TEXT("No instances: nobody"), FPlaytestFeedbackRules::ChooseHandler({}), static_cast<int32>(INDEX_NONE));

	{
		FPlaytestFeedbackCandidate Stopped;
		TestEqual(TEXT("No running instance: nobody"), FPlaytestFeedbackRules::ChooseHandler({ Stopped, Stopped }), static_cast<int32>(INDEX_NONE));
	}
	TestEqual(TEXT("Standalone game: itself"), FPlaytestFeedbackRules::ChooseHandler({ Running(INDEX_NONE) }), 0);

	{
		FPlaytestFeedbackCandidate A = Running(0), B = Running(1), C = Running(2);
		B.bViewportFocused = true;
		A.LastFocusSerial = 50;
		TestEqual(TEXT("The focused viewport wins over one focused more recently in the past"), FPlaytestFeedbackRules::ChooseHandler({ A, B, C }), 1);
	}
	{
		FPlaytestFeedbackCandidate A = Running(0), B = Running(1), C = Running(2);
		A.LastFocusSerial = 5;
		C.LastFocusSerial = 9;
		B.bIsPrimaryPIE = true;
		TestEqual(TEXT("Nobody focused: the most recently focused wins over the primary"), FPlaytestFeedbackRules::ChooseHandler({ A, B, C }), 2);
	}
	{
		FPlaytestFeedbackCandidate A = Running(0), B = Running(1);
		B.bIsPrimaryPIE = true;
		TestEqual(TEXT("Never focused: the primary PIE instance"), FPlaytestFeedbackRules::ChooseHandler({ A, B }), 1);
	}
	{
		FPlaytestFeedbackCandidate A = Running(2), B = Running(1), C = Running(3);
		TestEqual(TEXT("No primary: the lowest PIE instance number"), FPlaytestFeedbackRules::ChooseHandler({ A, B, C }), 1);
	}
	{
		FPlaytestFeedbackCandidate A = Running(1), B = Running(1);
		TestEqual(TEXT("Tie: the first in the list"), FPlaytestFeedbackRules::ChooseHandler({ A, B }), 0);
	}
	{
		FPlaytestFeedbackCandidate A = Running(0), B = Running(1);
		A.bFeedbackActive = true;
		B.bViewportFocused = true;
		TestEqual(TEXT("An instance with its note box open keeps the key"), FPlaytestFeedbackRules::ChooseHandler({ A, B }), 0);
	}
	{
		FPlaytestFeedbackCandidate A = Running(0), B = Running(1);
		A.bSessionRunning = false;
		A.bViewportFocused = true;
		A.LastFocusSerial = 100;
		A.bIsPrimaryPIE = true;
		TestEqual(TEXT("A stopped instance is never chosen, even focused or primary"), FPlaytestFeedbackRules::ChooseHandler({ A, B }), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestNoteTextAndConfirmationTest,
	"Project.Playtest.NoteTextAndConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaytestNoteTextAndConfirmationTest::RunTest(const FString& Parameters)
{
	// the 3-line field: Slate joins lines with "\r\n" on Windows; notes are saved with "\n" and trimmed
	TestEqual(TEXT("CRLF and CR become LF, outer whitespace trimmed"), FPlaytestFeedbackRules::NormalizeNoteText(TEXT("  line one\r\nline two\rline three\n  ")), FString(TEXT("line one\nline two\nline three")));
	TestEqual(TEXT("Inner blank lines are kept"), FPlaytestFeedbackRules::NormalizeNoteText(TEXT("a\r\n\r\nb")), FString(TEXT("a\n\nb")));
	TestEqual(TEXT("Only whitespace becomes empty"), FPlaytestFeedbackRules::NormalizeNoteText(TEXT(" \r\n\t ")), FString());

	// teardown: save only an open box with real text
	TestTrue(TEXT("Open box with text is saved on teardown"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(true, TEXT("the dock wobbles")));
	TestFalse(TEXT("Open box with only whitespace is not saved on teardown"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(true, TEXT("  \r\n ")));
	TestFalse(TEXT("Closed box is not saved on teardown"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(false, TEXT("text")));

	// confirmation toast wording (designer A-M3)
	const FPlaytestConfirmation Saved = FPlaytestFeedbackRules::MakeConfirmation(true, TEXT("hello"), true, TEXT("20260922-210509"), FString(), FString());
	TestEqual(TEXT("Saved title"), Saved.Title, FString(TEXT("Note saved. Thanks!")));
	TestEqual(TEXT("Saved detail is the folder"), Saved.Detail, FString(TEXT("20260922-210509")));
	TestFalse(TEXT("Saved is not a failure"), Saved.bFailure);

	const FPlaytestConfirmation Bookmark = FPlaytestFeedbackRules::MakeConfirmation(true, FString(), true, TEXT("20260922-210509"), FString(), FString());
	TestEqual(TEXT("Empty text: screenshot bookmark"), Bookmark.Title, FString(TEXT("Screenshot saved (no text)")));
	TestFalse(TEXT("Bookmark is not a failure"), Bookmark.bFailure);

	const FPlaytestConfirmation Bare = FPlaytestFeedbackRules::MakeConfirmation(true, FString(), false, TEXT("20260922-210509"), TEXT("screenshot dropped"), FString());
	TestEqual(TEXT("Empty text and no screenshot"), Bare.Title, FString(TEXT("Note saved (no text, no screenshot)")));
	TestTrue(TEXT("Warnings are mentioned in the detail"), Bare.Detail.StartsWith(TEXT("20260922-210509")) && Bare.Detail.Contains(TEXT("warnings")));

	const FPlaytestConfirmation Failed = FPlaytestFeedbackRules::MakeConfirmation(false, TEXT("hello"), false, FString(), FString(), TEXT("Could not write C:/x/note.json."));
	TestEqual(TEXT("Failure title with the reason"), Failed.Title, FString(TEXT("Note NOT saved: Could not write C:/x/note.json. Your text is in the log.")));
	TestTrue(TEXT("Failure style"), Failed.bFailure);
	TestTrue(TEXT("Failure has no folder line"), Failed.Detail.IsEmpty());

	const FPlaytestConfirmation Unknown = FPlaytestFeedbackRules::MakeConfirmation(false, TEXT("hello"), false, FString(), FString(), FString());
	TestEqual(TEXT("Failure without a reason"), Unknown.Title, FString(TEXT("Note NOT saved: unknown error. Your text is in the log.")));
	return true;
}

#endif

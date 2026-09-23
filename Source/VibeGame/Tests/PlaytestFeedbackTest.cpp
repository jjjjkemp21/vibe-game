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
	return true;
}

#endif

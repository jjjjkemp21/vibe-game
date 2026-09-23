#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Playtest/PlaytestNoteWriter.h"
#include "Playtest/PlaytestFeedbackSubsystem.h"

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

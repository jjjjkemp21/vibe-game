// QA-owned, independent tests for T-003 (playtest feedback key F8), written by the qa-engineer.
// Cases come from the spec (.claude/skills/playtest-feedback/SKILL.md), the T-003 acceptance line in docs/TASKS.md
// and the public contracts in PlaytestNoteWriter.h / PlaytestFeedbackSubsystem.h (black-box).
// Every file goes to a unique temp folder under Saved/Automation/Tmp/PlaytestFeedbackQA (never Saved/Playtest)
// and is deleted when the test ends.
// Not covered here: the Shipping compile-out (covered by the implementer's Shipping build) and the in-game flow
// (F8 -> screenshot -> pause -> note box -> Enter saves / Esc cancels -> game resumes), covered by the playtester in PIE.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Playtest/PlaytestNoteWriter.h"
#include "Playtest/PlaytestFeedbackSubsystem.h"
#include "Playtest/PlaytestFeedbackRules.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && VIBEGAME_WITH_PLAYTEST_FEEDBACK

#define PLAYTEST_QA_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace PlaytestQA
{
	/** Unique temp folder under Saved/Automation/Tmp (never Saved/Playtest); deleted with its content when the test ends */
	struct FScopedTempDir
	{
		FString Path;

		FScopedTempDir()
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir() / TEXT("PlaytestFeedbackQA") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
			IFileManager::Get().MakeDirectory(*Path, /*Tree*/ true);
		}

		~FScopedTempDir()
		{
			IFileManager::Get().DeleteDirectory(*Path, /*RequireExists*/ false, /*Tree*/ true);
		}

		FScopedTempDir(const FScopedTempDir&) = delete;
		FScopedTempDir& operator=(const FScopedTempDir&) = delete;
	};

	/** A fully filled note; every numeric field has a distinct value so swapped fields are caught */
	static FPlaytestNoteData MakeNote(const FString& Text)
	{
		FPlaytestNoteData Note;
		Note.Text = Text;
		Note.LevelName = TEXT("L_PalmKey");
		Note.Location = FVector(1234.5, -678.25, 90.125);
		Note.Rotation = FRotator(-12.5, 135.25, 2.75);
		Note.GameTimeSeconds = 321.75;
		Note.RealTimeSeconds = 400.5;
		Note.AverageFps = 57.25f;
		Note.Commit = TEXT("0123456789abcdef0123456789abcdef01234567");
		Note.BuildConfiguration = TEXT("Development");
		Note.NetMode = TEXT("Standalone");
		Note.Timestamp = FDateTime(2026, 9, 22, 14, 3, 7);
		return Note;
	}

	/** Builds a string from Unicode code points (supplementary planes become UTF-16 surrogate pairs) */
	static FString FromCodePoints(const TArray<uint32>& CodePoints)
	{
		FString Out;
		for (const uint32 CodePoint : CodePoints)
		{
			if constexpr (sizeof(TCHAR) == 2)
			{
				if (CodePoint > 0xFFFF)
				{
					const uint32 Offset = CodePoint - 0x10000;
					Out.AppendChar(static_cast<TCHAR>(0xD800 + (Offset >> 10)));
					Out.AppendChar(static_cast<TCHAR>(0xDC00 + (Offset & 0x3FF)));
					continue;
				}
			}
			Out.AppendChar(static_cast<TCHAR>(CodePoint));
		}
		return Out;
	}

	/** Names of the files and folders directly inside Dir (sorted) */
	static TArray<FString> ListEntries(const FString& Dir)
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(Dir / TEXT("*")), /*Files*/ true, /*Directories*/ true);
		Names.Sort();
		return Names;
	}

	/** Full, normalized, case-insensitive path comparison (trailing separators and slash direction ignored) */
	static bool SamePath(const FString& A, const FString& B)
	{
		FString FullA = FPaths::ConvertRelativePathToFull(A);
		FString FullB = FPaths::ConvertRelativePathToFull(B);
		FPaths::NormalizeDirectoryName(FullA);
		FPaths::NormalizeDirectoryName(FullB);
		FPaths::RemoveDuplicateSlashes(FullA);
		FPaths::RemoveDuplicateSlashes(FullB);
		return FullA.Equals(FullB, ESearchCase::IgnoreCase);
	}

	/** Folder name of a note folder path (tolerates a trailing separator) */
	static FString FolderName(const FString& Folder)
	{
		FString Normalized = Folder;
		FPaths::NormalizeDirectoryName(Normalized);
		return FPaths::GetCleanFilename(Normalized);
	}

	/** Parent of a note folder path (tolerates a trailing separator) */
	static FString FolderParent(const FString& Folder)
	{
		FString Normalized = Folder;
		FPaths::NormalizeDirectoryName(Normalized);
		return FPaths::GetPath(Normalized);
	}

	static int32 FirstDifference(const FString& A, const FString& B)
	{
		const int32 Common = FMath::Min(A.Len(), B.Len());
		for (int32 Index = 0; Index < Common; ++Index)
		{
			if (A[Index] != B[Index])
			{
				return Index;
			}
		}
		return A.Len() == B.Len() ? INDEX_NONE : Common;
	}

	static FString DescribeDifference(const FString& Actual, const FString& Expected)
	{
		const int32 Diff = FirstDifference(Actual, Expected);
		if (Diff == INDEX_NONE)
		{
			return TEXT("identical");
		}
		return FString::Printf(TEXT("first difference at index %d: got U+%04X, expected U+%04X (lengths %d vs %d)"),
			Diff,
			Diff < Actual.Len() ? static_cast<uint32>(Actual[Diff]) : 0u,
			Diff < Expected.Len() ? static_cast<uint32>(Expected[Diff]) : 0u,
			Actual.Len(), Expected.Len());
	}

	/**
	 *  Minimal RFC 8259 validator. Stricter than the engine reader on purpose (no raw control characters inside strings,
	 *  no NaN/Infinity tokens, no trailing commas or garbage), so note.json also loads in Python, PowerShell or jq.
	 */
	class FStrictJson
	{
	public:

		static bool Validate(const FString& Text, FString& OutError)
		{
			FStrictJson Parser(Text);
			Parser.SkipWhitespace();
			bool bValid = Parser.ParseValue(0);
			if (bValid)
			{
				Parser.SkipWhitespace();
				if (Parser.Pos != Parser.Len)
				{
					bValid = Parser.Fail(TEXT("unexpected data after the top-level value"));
				}
			}
			OutError = Parser.Error;
			return bValid;
		}

	private:

		explicit FStrictJson(const FString& InSource)
			: Source(InSource)
			, Len(InSource.Len())
		{
		}

		const FString& Source;
		int32 Len = 0;
		int32 Pos = 0;
		FString Error;

		bool Fail(const TCHAR* Why)
		{
			if (Error.IsEmpty())
			{
				const int32 From = FMath::Max(0, Pos - 20);
				const FString Context = Source.Mid(From, 40).ReplaceCharWithEscapedChar();
				Error = FString::Printf(TEXT("%s at offset %d, near \"%s\""), Why, Pos, *Context);
			}
			return false;
		}

		TCHAR Peek() const
		{
			return Pos < Len ? Source[Pos] : TCHAR(0);
		}

		static bool IsDigit(const TCHAR C)
		{
			return C >= TEXT('0') && C <= TEXT('9');
		}

		static bool IsHex(const TCHAR C)
		{
			return IsDigit(C) || (C >= TEXT('a') && C <= TEXT('f')) || (C >= TEXT('A') && C <= TEXT('F'));
		}

		void SkipWhitespace()
		{
			while (Pos < Len)
			{
				const TCHAR C = Source[Pos];
				if (C != TEXT(' ') && C != TEXT('\t') && C != TEXT('\n') && C != TEXT('\r'))
				{
					break;
				}
				++Pos;
			}
		}

		bool ParseValue(const int32 Depth)
		{
			if (Depth > 64)
			{
				return Fail(TEXT("nesting too deep"));
			}
			SkipWhitespace();
			const TCHAR C = Peek();
			if (C == TEXT('{')) { return ParseObject(Depth); }
			if (C == TEXT('[')) { return ParseArray(Depth); }
			if (C == TEXT('"')) { return ParseString(); }
			if (C == TEXT('t')) { return ParseLiteral(TEXT("true")); }
			if (C == TEXT('f')) { return ParseLiteral(TEXT("false")); }
			if (C == TEXT('n')) { return ParseLiteral(TEXT("null")); }
			if (C == TEXT('-') || IsDigit(C)) { return ParseNumber(); }
			return Fail(TEXT("unexpected character (not a JSON value)"));
		}

		bool ParseLiteral(const TCHAR* Word)
		{
			const int32 WordLen = FCString::Strlen(Word);
			if (Pos + WordLen <= Len && FCString::Strncmp(*Source + Pos, Word, WordLen) == 0)
			{
				Pos += WordLen;
				return true;
			}
			return Fail(TEXT("invalid literal"));
		}

		bool ParseNumber()
		{
			if (Peek() == TEXT('-'))
			{
				++Pos;
			}
			if (Peek() == TEXT('0'))
			{
				++Pos;
			}
			else if (IsDigit(Peek()))
			{
				while (IsDigit(Peek())) { ++Pos; }
			}
			else
			{
				return Fail(TEXT("invalid number"));
			}
			if (Peek() == TEXT('.'))
			{
				++Pos;
				if (!IsDigit(Peek()))
				{
					return Fail(TEXT("invalid number (no digits after '.')"));
				}
				while (IsDigit(Peek())) { ++Pos; }
			}
			if (Peek() == TEXT('e') || Peek() == TEXT('E'))
			{
				++Pos;
				if (Peek() == TEXT('+') || Peek() == TEXT('-'))
				{
					++Pos;
				}
				if (!IsDigit(Peek()))
				{
					return Fail(TEXT("invalid number (bad exponent)"));
				}
				while (IsDigit(Peek())) { ++Pos; }
			}
			return true;
		}

		bool ParseString()
		{
			++Pos; // opening quote
			while (Pos < Len)
			{
				const TCHAR C = Source[Pos];
				if (C == TEXT('"'))
				{
					++Pos;
					return true;
				}
				if (C < 0x20)
				{
					return Fail(TEXT("raw control character inside a string (must be escaped)"));
				}
				if (C == TEXT('\\'))
				{
					++Pos;
					const TCHAR Escaped = Peek();
					if (Escaped == TEXT('u'))
					{
						++Pos;
						for (int32 Index = 0; Index < 4; ++Index)
						{
							if (!IsHex(Peek()))
							{
								return Fail(TEXT("invalid \\u escape"));
							}
							++Pos;
						}
						continue;
					}
					if (Escaped == TEXT('"') || Escaped == TEXT('\\') || Escaped == TEXT('/') || Escaped == TEXT('b') ||
						Escaped == TEXT('f') || Escaped == TEXT('n') || Escaped == TEXT('r') || Escaped == TEXT('t'))
					{
						++Pos;
						continue;
					}
					return Fail(TEXT("invalid escape sequence"));
				}
				++Pos;
			}
			return Fail(TEXT("unterminated string"));
		}

		bool ParseObject(const int32 Depth)
		{
			++Pos; // {
			SkipWhitespace();
			if (Peek() == TEXT('}'))
			{
				++Pos;
				return true;
			}
			for (;;)
			{
				SkipWhitespace();
				if (Peek() != TEXT('"'))
				{
					return Fail(TEXT("expected a string key"));
				}
				if (!ParseString())
				{
					return false;
				}
				SkipWhitespace();
				if (Peek() != TEXT(':'))
				{
					return Fail(TEXT("expected ':'"));
				}
				++Pos;
				if (!ParseValue(Depth + 1))
				{
					return false;
				}
				SkipWhitespace();
				if (Peek() == TEXT(','))
				{
					++Pos;
					continue;
				}
				if (Peek() == TEXT('}'))
				{
					++Pos;
					return true;
				}
				return Fail(TEXT("expected ',' or '}'"));
			}
		}

		bool ParseArray(const int32 Depth)
		{
			++Pos; // [
			SkipWhitespace();
			if (Peek() == TEXT(']'))
			{
				++Pos;
				return true;
			}
			for (;;)
			{
				if (!ParseValue(Depth + 1))
				{
					return false;
				}
				SkipWhitespace();
				if (Peek() == TEXT(','))
				{
					++Pos;
					continue;
				}
				if (Peek() == TEXT(']'))
				{
					++Pos;
					return true;
				}
				return Fail(TEXT("expected ',' or ']'"));
			}
		}
	};

	/** Checks the text is strict JSON and parses it into an object (null + test errors otherwise) */
	static TSharedPtr<FJsonObject> ParseStrict(FAutomationTestBase& Test, const FString& What, const FString& Json)
	{
		FString StrictError;
		const bool bStrict = FStrictJson::Validate(Json, StrictError);
		Test.TestTrue(FString::Printf(TEXT("%s: note.json is valid RFC 8259 JSON (%s)"), *What, bStrict ? TEXT("ok") : *StrictError), bStrict);

		TSharedPtr<FJsonObject> Root;
		const bool bParsed = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid();
		Test.TestTrue(FString::Printf(TEXT("%s: note.json parses as a JSON object"), *What), bParsed);
		return bParsed ? Root : nullptr;
	}

	/** Writes the note without a screenshot and returns the parsed note.json (null + test errors on failure) */
	static TSharedPtr<FJsonObject> WriteAndParse(FAutomationTestBase& Test, const FString& What, const FString& RootDir, const FPlaytestNoteData& Note)
	{
		FString Folder, Error;
		const bool bWritten = FPlaytestNoteWriter::WriteNote(RootDir, Note, 0, 0, TArray<FColor>(), Folder, Error);
		if (!Test.TestTrue(FString::Printf(TEXT("%s: WriteNote succeeds (error: '%s')"), *What, *Error), bWritten))
		{
			return nullptr;
		}
		FString Json;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: note.json can be read back"), *What), FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")))))
		{
			return nullptr;
		}
		return ParseStrict(Test, What, Json);
	}

	static const TCHAR* JsonTypeName(const EJson Type)
	{
		switch (Type)
		{
		case EJson::Null: return TEXT("null");
		case EJson::String: return TEXT("string");
		case EJson::Number: return TEXT("number");
		case EJson::Boolean: return TEXT("boolean");
		case EJson::Array: return TEXT("array");
		case EJson::Object: return TEXT("object");
		default: return TEXT("none");
		}
	}

	/** Expects Object.Name to exist with the given JSON type; returns the value or null */
	static TSharedPtr<FJsonValue> ExpectField(FAutomationTestBase& Test, const FString& What, const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const EJson Type)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Name) : nullptr;
		const bool bMatches = Value.IsValid() && Value->Type == Type;
		Test.TestTrue(FString::Printf(TEXT("%s: field '%s' is a %s (got %s)"), *What, Name, JsonTypeName(Type), Value.IsValid() ? JsonTypeName(Value->Type) : TEXT("missing field")), bMatches);
		return bMatches ? Value : nullptr;
	}

	static void ExpectNumber(FAutomationTestBase& Test, const FString& What, const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const double Expected, const double Tolerance)
	{
		if (const TSharedPtr<FJsonValue> Value = ExpectField(Test, What, Object, Name, EJson::Number))
		{
			const double Actual = Value->AsNumber();
			Test.TestTrue(FString::Printf(TEXT("%s: %s = %.6f (expected %.6f +/- %g)"), *What, Name, Actual, Expected, Tolerance), FMath::IsNearlyEqual(Actual, Expected, Tolerance));
		}
	}

	static void ExpectString(FAutomationTestBase& Test, const FString& What, const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const FString& Expected)
	{
		if (const TSharedPtr<FJsonValue> Value = ExpectField(Test, What, Object, Name, EJson::String))
		{
			const FString Actual = Value->AsString();
			Test.TestTrue(FString::Printf(TEXT("%s: %s round-trips exactly (%s)"), *What, Name, *DescribeDifference(Actual, Expected)), Actual.Equals(Expected, ESearchCase::CaseSensitive));
		}
	}

	/** Returns the object value of Object.Name (null + test error if missing or not an object) */
	static TSharedPtr<FJsonObject> ExpectObject(FAutomationTestBase& Test, const FString& What, const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		const TSharedPtr<FJsonValue> Value = ExpectField(Test, What, Object, Name, EJson::Object);
		return Value.IsValid() ? Value->AsObject() : nullptr;
	}

	/** Field is a number or null (never a missing field, a string or an invalid token) */
	static void ExpectNumberOrNull(FAutomationTestBase& Test, const FString& What, const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Name) : nullptr;
		const bool bOk = Value.IsValid() && (Value->Type == EJson::Number || Value->Type == EJson::Null);
		Test.TestTrue(FString::Printf(TEXT("%s: field '%s' is a number or null (got %s)"), *What, Name, Value.IsValid() ? JsonTypeName(Value->Type) : TEXT("missing field")), bOk);
		if (bOk && Value->Type == EJson::Number)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: field '%s' is finite"), *What, Name), FMath::IsFinite(Value->AsNumber()));
		}
	}

	/** True if note.json points at screenshot.png */
	static bool ClaimsScreenshot(const TSharedPtr<FJsonObject>& Root)
	{
		FString Shot;
		return Root.IsValid() && Root->TryGetStringField(TEXT("screenshot"), Shot) && Shot == TEXT("screenshot.png");
	}

	/** Writes Text as a note and expects note.json to be strict JSON whose "text" is exactly Text */
	static void ExpectTextRoundTrip(FAutomationTestBase& Test, const FString& What, const FString& RootDir, const FString& Text)
	{
		const TSharedPtr<FJsonObject> Root = WriteAndParse(Test, What, RootDir, MakeNote(Text));
		if (Root.IsValid())
		{
			ExpectString(Test, What, Root, TEXT("text"), Text);
		}
	}

	/** Deterministic long text (ASCII, quotes, backslashes, tabs, newlines, Latin-1 and CJK), exactly TargetLen UTF-16 units */
	static FString MakeLongText(const int32 TargetLen)
	{
		FString Out;
		Out.Reserve(TargetLen);
		for (int32 Index = 0; ; ++Index)
		{
			FString Chunk = FString::Printf(TEXT("Note %05d: \"quoted\" back\\slash\ttab /slash "), Index);
			Chunk += FromCodePoints({ 0xE9, 0x9B5A });
			Chunk.AppendChar(TEXT('\n'));
			if (Out.Len() + Chunk.Len() > TargetLen)
			{
				break;
			}
			Out += Chunk;
		}
		while (Out.Len() < TargetLen)
		{
			Out.AppendChar(TEXT('x'));
		}
		return Out;
	}

	/** Checks bytes are well-formed UTF-8 (no overlong forms, no encoded surrogates / CESU-8, nothing above U+10FFFF) */
	static bool IsValidUtf8(const TArray<uint8>& Bytes, const int32 Start, int32& OutBadOffset)
	{
		int32 Index = Start;
		const int32 Num = Bytes.Num();
		while (Index < Num)
		{
			const uint8 Lead = Bytes[Index];
			if (Lead < 0x80)
			{
				++Index;
				continue;
			}
			int32 Extra = 0;
			uint32 CodePoint = 0;
			if (Lead >= 0xC2 && Lead <= 0xDF) { Extra = 1; CodePoint = Lead & 0x1F; }
			else if ((Lead & 0xF0) == 0xE0) { Extra = 2; CodePoint = Lead & 0x0F; }
			else if (Lead >= 0xF0 && Lead <= 0xF4) { Extra = 3; CodePoint = Lead & 0x07; }
			else { OutBadOffset = Index; return false; }

			if (Index + Extra >= Num)
			{
				OutBadOffset = Index;
				return false;
			}
			for (int32 K = 1; K <= Extra; ++K)
			{
				const uint8 Continuation = Bytes[Index + K];
				if ((Continuation & 0xC0) != 0x80)
				{
					OutBadOffset = Index + K;
					return false;
				}
				CodePoint = (CodePoint << 6) | (Continuation & 0x3F);
			}
			const bool bOverlong = (Extra == 2 && CodePoint < 0x800) || (Extra == 3 && CodePoint < 0x10000);
			const bool bSurrogate = CodePoint >= 0xD800 && CodePoint <= 0xDFFF;
			if (bOverlong || bSurrogate || CodePoint > 0x10FFFF)
			{
				OutBadOffset = Index;
				return false;
			}
			Index += Extra + 1;
		}
		return true;
	}

	/**
	 *  Invariant for invalid input that WriteNote may either reject or accept:
	 *  rejected -> OutError explains it and no partial note folder is left; accepted -> a complete, self-consistent note.
	 */
	static void ExpectCleanOutcome(FAutomationTestBase& Test, const FString& What, const FString& RootDir, const bool bWritten, const FString& Folder, const FString& Error, const FString& ExpectedText)
	{
		if (!bWritten)
		{
			Test.TestFalse(FString::Printf(TEXT("%s: a rejected write explains why (OutError is set)"), *What), Error.IsEmpty());
			const TArray<FString> Left = ListEntries(RootDir);
			Test.TestEqual(FString::Printf(TEXT("%s: a rejected write leaves no partial note folder (found: %s)"), *What, *FString::Join(Left, TEXT(", "))), Left.Num(), 0);
			return;
		}

		FString Json;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: an accepted write produced note.json"), *What), FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")))))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Root = ParseStrict(Test, What, Json);
		if (!Root.IsValid())
		{
			return;
		}
		ExpectString(Test, What, Root, TEXT("text"), ExpectedText);

		const FString PngPath = Folder / TEXT("screenshot.png");
		const bool bHasPng = FPaths::FileExists(PngPath);
		const bool bClaimsPng = ClaimsScreenshot(Root);
		Test.TestTrue(FString::Printf(TEXT("%s: note.json references screenshot.png only when the file exists (claims=%d, exists=%d)"), *What, bClaimsPng ? 1 : 0, bHasPng ? 1 : 0), bClaimsPng == bHasPng);
		if (bHasPng)
		{
			FImage Image;
			const bool bDecodes = FImageUtils::LoadImage(*PngPath, Image) && Image.SizeX > 0 && Image.SizeY > 0;
			Test.TestTrue(FString::Printf(TEXT("%s: the screenshot.png that was written decodes to a non-empty image"), *What), bDecodes);
		}
	}

	static void WriteAscii(const FString& Path, const FString& Content)
	{
		FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceAnsi);
	}

	/** ReadGitCommit contract: "unknown" or a full commit id (40 hex for SHA-1 repos, 64 for SHA-256 repos) */
	static bool IsHashOrUnknown(const FString& Value)
	{
		if (Value == TEXT("unknown"))
		{
			return true;
		}
		if (Value.Len() != 40 && Value.Len() != 64)
		{
			return false;
		}
		for (const TCHAR C : Value)
		{
			if (!FChar::IsHexDigit(C))
			{
				return false;
			}
		}
		return true;
	}

	/** Feeds Count frames of DeltaSeconds each, advancing Now (use binary fractions like 1/64 so time stays exact) */
	static void Feed(FPlaytestFpsTracker& Tracker, double& Now, const int32 Count, const double DeltaSeconds)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Now += DeltaSeconds;
			Tracker.AddFrame(Now, DeltaSeconds);
		}
	}
}

/** Failure-path tests: the product may log its own errors (LogVibeGame) on purpose; the assertions check the return value and the disk. */
class FPlaytestQAFailurePathTestBase : public FAutomationTestBase
{
public:

	FPlaytestQAFailurePathTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.AddUnique(TEXT("LogVibeGame"));
		return Categories;
	}
};

// ---------------------------------------------------------------------------------------------------------------------
// Note text edge cases: the text must survive the trip to disk exactly, and note.json must stay valid JSON.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATextEmptyTest, "Project.Playtest.QA.Text.Empty", PLAYTEST_QA_FLAGS)

bool FPlaytestQATextEmptyTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	// Enter on an empty box still saves (the screenshot and context are useful); text must be "" (a string), not missing or null
	PlaytestQA::ExpectTextRoundTrip(*this, TEXT("Empty note"), Temp.Path, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATextVeryLongTest, "Project.Playtest.QA.Text.VeryLong", PLAYTEST_QA_FLAGS)

bool FPlaytestQATextVeryLongTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	for (const int32 Length : { 5000, 70000 })
	{
		const FString Text = PlaytestQA::MakeLongText(Length);
		TestEqual(FString::Printf(TEXT("Generated text has %d characters"), Length), Text.Len(), Length);
		PlaytestQA::ExpectTextRoundTrip(*this, FString::Printf(TEXT("%d-character note"), Length), Temp.Path, Text);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATextNonAsciiTest, "Project.Playtest.QA.Text.NonAsciiAndEmoji", PLAYTEST_QA_FLAGS)

bool FPlaytestQATextNonAsciiTest::RunTest(const FString& Parameters)
{
	using PlaytestQA::FromCodePoints;
	PlaytestQA::FScopedTempDir Temp;

	struct FCase
	{
		const TCHAR* Name;
		FString Text;
	};
	const FCase Cases[] = {
		// Latin-1 range (breaks if written as ANSI / code page)
		{ TEXT("Latin-1"), TEXT("Caf") + FromCodePoints({ 0xE9 }) + TEXT(" cr") + FromCodePoints({ 0xE8 }) + TEXT("me, ni") + FromCodePoints({ 0xF1 }) + TEXT("o, ") + FromCodePoints({ 0xFC }) + TEXT("ber, 25") + FromCodePoints({ 0xB0 }) + TEXT("C") },
		// CJK, Cyrillic, Arabic (right-to-left)
		{ TEXT("CJK/Cyrillic/Arabic"), FromCodePoints({ 0x91E3, 0x308A, 0x3067, 0x9B5A }) + TEXT(" / ") + FromCodePoints({ 0x0440, 0x044B, 0x0431, 0x0430 }) + TEXT(" / ") + FromCodePoints({ 0x0633, 0x0645, 0x0643 }) },
		// Emoji outside the BMP (surrogate pairs), a ZWJ sequence and a skin-tone modifier
		{ TEXT("Emoji"), TEXT("Caught one ") + FromCodePoints({ 0x1F3A3, 0x1F41F }) + TEXT(" then a shark ") + FromCodePoints({ 0x1F988 }) + TEXT(" family ") + FromCodePoints({ 0x1F468, 0x200D, 0x1F467 }) + TEXT(" ok ") + FromCodePoints({ 0x1F44D, 0x1F3FD }) },
		// Combining mark, no-break space, zero-width space, U+FEFF inside the text, replacement char, highest code point U+10FFFF
		{ TEXT("Special Unicode"), TEXT("e") + FromCodePoints({ 0x0301 }) + TEXT(" nbsp") + FromCodePoints({ 0xA0 }) + TEXT("zwsp") + FromCodePoints({ 0x200B }) + TEXT("bom") + FromCodePoints({ 0xFEFF }) + TEXT("repl") + FromCodePoints({ 0xFFFD }) + TEXT("max") + FromCodePoints({ 0x10FFFF }) },
	};
	for (const FCase& Case : Cases)
	{
		PlaytestQA::ExpectTextRoundTrip(*this, Case.Name, Temp.Path, Case.Text);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATextEscapingTest, "Project.Playtest.QA.Text.QuotesBackslashesNewlines", PLAYTEST_QA_FLAGS)

bool FPlaytestQATextEscapingTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;

	FString Mixed = TEXT("  leading spaces, \"double quotes\", 'single', back\\slash, C:\\Saved\\Playtest\\, \\\" escaped-looking, \\n literal backslash-n, ")
		TEXT("line1\nline2\r\nline3\rline4\ttab, slash / and </script>, braces {}[]:, ");
	Mixed.AppendChar(TCHAR(0x7F));
	Mixed += PlaytestQA::FromCodePoints({ 0x2028, 0x2029 });
	Mixed += TEXT(" ends with a backslash \\");

	FString AllControls;
	for (TCHAR C = 0x01; C < 0x20; ++C)
	{
		AllControls.AppendChar(C);
	}

	struct FCase
	{
		const TCHAR* Name;
		FString Text;
	};
	const FCase Cases[] = {
		{ TEXT("Mixed quotes/backslashes/newlines"), Mixed },
		{ TEXT("Only a double quote"), TEXT("\"") },
		{ TEXT("Only a backslash"), TEXT("\\") },
		{ TEXT("Backslash then quote"), TEXT("\\\"") },
		{ TEXT("Literal \\u0041 is not an escape"), TEXT("\\u0041 stays six characters") },
		{ TEXT("Only spaces"), TEXT("   ") },
		{ TEXT("Only a newline"), TEXT("\n") },
		{ TEXT("Only CRLF"), TEXT("\r\n") },
		{ TEXT("All control characters U+0001..U+001F"), AllControls },
	};
	for (const FCase& Case : Cases)
	{
		PlaytestQA::ExpectTextRoundTrip(*this, Case.Name, Temp.Path, Case.Text);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// note.json schema: text, level, location, rotation, game time, average FPS, commit, screenshot.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAJsonSchemaTest, "Project.Playtest.QA.Json.SchemaFieldsAndTypes", PLAYTEST_QA_FLAGS)

bool FPlaytestQAJsonSchemaTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("The jetty railing floats above the planks"));

	TArray<FColor> Pixels;
	Pixels.Init(FColor(10, 20, 30, 255), 4 * 2);
	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(Temp.Path, Note, 4, 2, Pixels, Folder, Error);
	if (!TestTrue(FString::Printf(TEXT("WriteNote succeeds (error: '%s')"), *Error), bWritten))
	{
		return true;
	}

	FString Json;
	if (!TestTrue(TEXT("note.json exists"), FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")))))
	{
		return true;
	}
	const FString What = TEXT("note.json");
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, What, Json);
	if (!Root.IsValid())
	{
		return true;
	}

	PlaytestQA::ExpectString(*this, What, Root, TEXT("text"), Note.Text);
	PlaytestQA::ExpectString(*this, What, Root, TEXT("level"), Note.LevelName);
	PlaytestQA::ExpectString(*this, What, Root, TEXT("commit"), Note.Commit);
	PlaytestQA::ExpectString(*this, What, Root, TEXT("screenshot"), FString(TEXT("screenshot.png")));
	TestTrue(TEXT("The referenced screenshot.png exists next to note.json"), FPaths::FileExists(Folder / TEXT("screenshot.png")));

	// game time, not real time (the note stores both; the spec asks for game time)
	PlaytestQA::ExpectNumber(*this, What, Root, TEXT("gameTime"), Note.GameTimeSeconds, 1e-3);
	PlaytestQA::ExpectNumber(*this, What, Root, TEXT("avgFps"), Note.AverageFps, 1e-2);

	const TSharedPtr<FJsonObject> Location = PlaytestQA::ExpectObject(*this, What, Root, TEXT("location"));
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("x"), Note.Location.X, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("y"), Note.Location.Y, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("z"), Note.Location.Z, 1e-3);

	const TSharedPtr<FJsonObject> Rotation = PlaytestQA::ExpectObject(*this, What, Root, TEXT("rotation"));
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("pitch"), Note.Rotation.Pitch, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("yaw"), Note.Rotation.Yaw, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("roll"), Note.Rotation.Roll, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAJsonNoScreenshotTest, "Project.Playtest.QA.Json.NoScreenshotNotReferenced", PLAYTEST_QA_FLAGS)

bool FPlaytestQAJsonNoScreenshotTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("screenshot timed out"));

	// serializer: without a screenshot, note.json must not point at screenshot.png (null, "" or no field are all fine)
	const TSharedPtr<FJsonObject> Serialized = PlaytestQA::ParseStrict(*this, TEXT("ToJsonString(no screenshot)"), FPlaytestNoteWriter::ToJsonString(Note, /*bHasScreenshot*/ false));
	TestFalse(TEXT("ToJsonString without a screenshot does not reference screenshot.png"), PlaytestQA::ClaimsScreenshot(Serialized));

	// writer: no pixels -> no screenshot.png on disk and note.json does not reference one
	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(Temp.Path, Note, 0, 0, TArray<FColor>(), Folder, Error);
	if (!TestTrue(FString::Printf(TEXT("WriteNote without pixels succeeds (error: '%s')"), *Error), bWritten))
	{
		return true;
	}
	TestFalse(TEXT("No screenshot.png is written without pixels"), FPaths::FileExists(Folder / TEXT("screenshot.png")));
	FString Json;
	TestTrue(TEXT("note.json exists"), FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json"))));
	const TSharedPtr<FJsonObject> Written = PlaytestQA::ParseStrict(*this, TEXT("note.json(no screenshot)"), Json);
	TestFalse(TEXT("note.json written without pixels does not reference screenshot.png"), PlaytestQA::ClaimsScreenshot(Written));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAJsonDefaultNoteTest, "Project.Playtest.QA.Json.DefaultNoteIsValid", PLAYTEST_QA_FLAGS)

bool FPlaytestQAJsonDefaultNoteTest::RunTest(const FString& Parameters)
{
	// a note with nothing filled in (no level, no pawn, no commit) must still serialize to a complete, valid note.json
	const FString What = TEXT("default note");
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, What, FPlaytestNoteWriter::ToJsonString(FPlaytestNoteData(), /*bHasScreenshot*/ false));
	if (!Root.IsValid())
	{
		return true;
	}
	PlaytestQA::ExpectString(*this, What, Root, TEXT("text"), FString());
	PlaytestQA::ExpectField(*this, What, Root, TEXT("level"), EJson::String);
	PlaytestQA::ExpectString(*this, What, Root, TEXT("commit"), FString(TEXT("unknown")));
	PlaytestQA::ExpectNumber(*this, What, Root, TEXT("gameTime"), 0.0, 1e-9);
	PlaytestQA::ExpectNumber(*this, What, Root, TEXT("avgFps"), 0.0, 1e-9);
	const TSharedPtr<FJsonObject> Location = PlaytestQA::ExpectObject(*this, What, Root, TEXT("location"));
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("x"), 0.0, 1e-9);
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("y"), 0.0, 1e-9);
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("z"), 0.0, 1e-9);
	const TSharedPtr<FJsonObject> Rotation = PlaytestQA::ExpectObject(*this, What, Root, TEXT("rotation"));
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("pitch"), 0.0, 1e-9);
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("yaw"), 0.0, 1e-9);
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("roll"), 0.0, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAJsonLargeCoordinatesTest, "Project.Playtest.QA.Json.LargeWorldCoordinates", PLAYTEST_QA_FLAGS)

bool FPlaytestQAJsonLargeCoordinatesTest::RunTest(const FString& Parameters)
{
	// open world reached by boat: positions tens of km from the origin, long sessions, extreme view angles
	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("far out at sea"));
	Note.Location = FVector(2000000.25, -1500000.5, -50000.75);
	Note.Rotation = FRotator(-89.75, -179.5, 45.5);
	Note.GameTimeSeconds = 3.0 * 86400.0 + 0.5;
	Note.AverageFps = 0.5f;

	const FString What = TEXT("large values");
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, What, FPlaytestNoteWriter::ToJsonString(Note, /*bHasScreenshot*/ true));
	if (!Root.IsValid())
	{
		return true;
	}
	const TSharedPtr<FJsonObject> Location = PlaytestQA::ExpectObject(*this, What, Root, TEXT("location"));
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("x"), Note.Location.X, 1.0);
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("y"), Note.Location.Y, 1.0);
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("z"), Note.Location.Z, 1.0);
	const TSharedPtr<FJsonObject> Rotation = PlaytestQA::ExpectObject(*this, What, Root, TEXT("rotation"));
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("pitch"), Note.Rotation.Pitch, 1e-2);
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("yaw"), Note.Rotation.Yaw, 1e-2);
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("roll"), Note.Rotation.Roll, 1e-2);
	PlaytestQA::ExpectNumber(*this, What, Root, TEXT("gameTime"), Note.GameTimeSeconds, 1e-2);
	PlaytestQA::ExpectNumber(*this, What, Root, TEXT("avgFps"), Note.AverageFps, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAJsonNonFiniteTest, "Project.Playtest.QA.Json.NonFiniteNumbersStayValid", PLAYTEST_QA_FLAGS)

bool FPlaytestQAJsonNonFiniteTest::RunTest(const FString& Parameters)
{
	// a player who fell out of the world (NaN position) or a bad FPS sample must not corrupt the note Jimmy typed
	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("I fell through the world"));
	Note.Location.X = std::numeric_limits<double>::quiet_NaN();
	Note.GameTimeSeconds = std::numeric_limits<double>::infinity();
	Note.AverageFps = std::numeric_limits<float>::infinity();

	const FString What = TEXT("non-finite numbers");
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, What, FPlaytestNoteWriter::ToJsonString(Note, /*bHasScreenshot*/ false));
	if (!Root.IsValid())
	{
		return true;
	}
	PlaytestQA::ExpectString(*this, What, Root, TEXT("text"), Note.Text);
	PlaytestQA::ExpectNumberOrNull(*this, What, Root, TEXT("gameTime"));
	PlaytestQA::ExpectNumberOrNull(*this, What, Root, TEXT("avgFps"));
	const TSharedPtr<FJsonObject> Location = PlaytestQA::ExpectObject(*this, What, Root, TEXT("location"));
	PlaytestQA::ExpectNumberOrNull(*this, TEXT("location"), Location, TEXT("x"));
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("y"), Note.Location.Y, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAJsonUtf8Test, "Project.Playtest.QA.Json.FileIsUtf8", PLAYTEST_QA_FLAGS)

bool FPlaytestQAJsonUtf8Test::RunTest(const FString& Parameters)
{
	// JSON files exchanged between tools must be UTF-8 (RFC 8259 section 8.1); triage reads them with Python/PowerShell/jq
	PlaytestQA::FScopedTempDir Temp;
	const FString Text = TEXT("Caf") + PlaytestQA::FromCodePoints({ 0xE9, 0x20, 0x9B5A, 0x20, 0x1F3A3 }) + TEXT(" \"end\"");

	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(Temp.Path, PlaytestQA::MakeNote(Text), 0, 0, TArray<FColor>(), Folder, Error);
	if (!TestTrue(FString::Printf(TEXT("WriteNote succeeds (error: '%s')"), *Error), bWritten))
	{
		return true;
	}

	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("note.json can be read as bytes"), FFileHelper::LoadFileToArray(Bytes, *(Folder / TEXT("note.json")))))
	{
		return true;
	}
	const bool bUtf16Bom = Bytes.Num() >= 2 && ((Bytes[0] == 0xFF && Bytes[1] == 0xFE) || (Bytes[0] == 0xFE && Bytes[1] == 0xFF));
	TestFalse(TEXT("note.json is not UTF-16 (no UTF-16 byte order mark)"), bUtf16Bom);
	TestFalse(TEXT("note.json has no NUL bytes (UTF-16 or binary content)"), Bytes.Contains(0));

	int32 Start = 0;
	if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
	{
		Start = 3;
		AddInfo(TEXT("note.json starts with a UTF-8 BOM (tolerated; Python's json module needs encoding='utf-8-sig' for it)"));
	}
	int32 BadOffset = INDEX_NONE;
	if (!TestTrue(TEXT("note.json bytes are well-formed UTF-8 (not ANSI/code page, not CESU-8)"), PlaytestQA::IsValidUtf8(Bytes, Start, BadOffset)))
	{
		AddError(FString::Printf(TEXT("First invalid UTF-8 byte at offset %d (0x%02X)"), BadOffset, BadOffset >= 0 && BadOffset < Bytes.Num() ? Bytes[BadOffset] : 0));
		return true;
	}

	FString Decoded;
	Decoded.AppendChars(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData() + Start), Bytes.Num() - Start);
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, TEXT("note.json decoded as UTF-8"), Decoded);
	if (Root.IsValid())
	{
		PlaytestQA::ExpectString(*this, TEXT("note.json decoded as UTF-8"), Root, TEXT("text"), Text);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// screenshot.png content
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAScreenshotPixelsTest, "Project.Playtest.QA.Screenshot.PixelsMatchCapture", PLAYTEST_QA_FLAGS)

bool FPlaytestQAScreenshotPixelsTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;

	// odd, non-square size; red grows with x, green with y, blue falls with x: catches R/B swaps, flips and stride bugs
	const int32 Width = 37;
	const int32 Height = 21;
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			Pixels[Y * Width + X] = FColor(static_cast<uint8>(X * 7), static_cast<uint8>(Y * 12), static_cast<uint8>(255 - X * 3), 255);
		}
	}

	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(Temp.Path, PlaytestQA::MakeNote(TEXT("pixel check")), Width, Height, Pixels, Folder, Error);
	if (!TestTrue(FString::Printf(TEXT("WriteNote succeeds (error: '%s')"), *Error), bWritten))
	{
		return true;
	}

	FImage Image;
	if (!TestTrue(TEXT("screenshot.png decodes as an image"), FImageUtils::LoadImage(*(Folder / TEXT("screenshot.png")), Image)))
	{
		return true;
	}
	TestEqual(TEXT("screenshot.png width"), Image.SizeX, Width);
	TestEqual(TEXT("screenshot.png height"), Image.SizeY, Height);
	if (!TestTrue(FString::Printf(TEXT("screenshot.png decodes to 8-bit color (raw format %d)"), static_cast<int32>(Image.Format)), Image.Format == ERawImageFormat::BGRA8)
		|| Image.SizeX != Width || Image.SizeY != Height)
	{
		return true;
	}

	const TArrayView64<FColor> Decoded = Image.AsBGRA8();
	int32 Mismatches = 0;
	int32 FirstBad = INDEX_NONE;
	bool bAllOpaque = true;
	for (int32 Index = 0; Index < Width * Height; ++Index)
	{
		const FColor& Got = Decoded[Index];
		const FColor& Want = Pixels[Index];
		if (Got.R != Want.R || Got.G != Want.G || Got.B != Want.B)
		{
			++Mismatches;
			if (FirstBad == INDEX_NONE)
			{
				FirstBad = Index;
			}
		}
		bAllOpaque &= (Got.A == 255);
	}
	if (Mismatches > 0)
	{
		const FColor& Got = Decoded[FirstBad];
		const FColor& Want = Pixels[FirstBad];
		AddError(FString::Printf(TEXT("screenshot.png differs from the captured pixels in %d of %d pixels; first at x=%d y=%d: got RGB(%d,%d,%d), expected RGB(%d,%d,%d) (R and B swapped = channel-order bug; G reversed = vertical flip)"),
			Mismatches, Width * Height, FirstBad % Width, FirstBad / Width, Got.R, Got.G, Got.B, Want.R, Want.G, Want.B));
	}
	TestTrue(TEXT("Opaque captured pixels stay opaque in screenshot.png"), bAllOpaque);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Folder naming: Saved/Playtest/<yyyyMMdd-HHmmss>/, unique per note.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderNameFormatTest, "Project.Playtest.QA.Folder.NameFormat", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderNameFormatTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		FDateTime Stamp;
		const TCHAR* Expected;
		const TCHAR* Why;
	};
	const FCase Cases[] = {
		{ FDateTime(2026, 1, 2, 3, 4, 5), TEXT("20260102-030405"), TEXT("every field zero-padded") },
		{ FDateTime(2026, 12, 31, 23, 59, 59), TEXT("20261231-235959"), TEXT("largest field values, 24-hour clock") },
		{ FDateTime(2027, 1, 1, 0, 0, 0), TEXT("20270101-000000"), TEXT("midnight is 00, not 12 or 24") },
		{ FDateTime(2026, 9, 22, 13, 0, 0), TEXT("20260922-130000"), TEXT("afternoon is 13, not 01") },
		{ FDateTime(2028, 2, 29, 12, 30, 0), TEXT("20280229-123000"), TEXT("leap day") },
		{ FDateTime(2026, 9, 22, 21, 5, 9, 999), TEXT("20260922-210509"), TEXT("milliseconds dropped, not rounded up") },
	};
	for (const FCase& Case : Cases)
	{
		const FString Name = FPlaytestNoteWriter::MakeFolderName(Case.Stamp);
		TestEqual(FString::Printf(TEXT("MakeFolderName: %s"), Case.Why), Name, FString(Case.Expected));

		bool bShape = Name.Len() == 15 && Name[8] == TEXT('-');
		for (int32 Index = 0; Index < Name.Len() && bShape; ++Index)
		{
			bShape = Index == 8 || FChar::IsDigit(Name[Index]);
		}
		TestTrue(FString::Printf(TEXT("'%s' has the shape yyyyMMdd-HHmmss (8 digits, '-', 6 digits)"), *Name), bShape);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderSortTest, "Project.Playtest.QA.Folder.NamesSortChronologically", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderSortTest::RunTest(const FString& Parameters)
{
	// triage finds "folders newer than the last processed one" by name, so name order must equal time order
	const FDateTime Stamps[] = {
		FDateTime(2026, 9, 22, 9, 59, 59),
		FDateTime(2026, 9, 22, 10, 0, 0),
		FDateTime(2026, 9, 22, 23, 59, 59),
		FDateTime(2026, 9, 23, 0, 0, 0),
		FDateTime(2026, 10, 1, 0, 0, 0),
		FDateTime(2027, 1, 1, 0, 0, 0),
	};
	for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(Stamps); ++Index)
	{
		const FString Earlier = FPlaytestNoteWriter::MakeFolderName(Stamps[Index]);
		const FString Later = FPlaytestNoteWriter::MakeFolderName(Stamps[Index + 1]);
		TestTrue(FString::Printf(TEXT("'%s' sorts before '%s'"), *Earlier, *Later), Earlier.Compare(Later, ESearchCase::CaseSensitive) < 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderCollisionTest, "Project.Playtest.QA.Folder.SameSecondCollisions", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderCollisionTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const FDateTime Stamp(2026, 9, 22, 21, 5, 9);
	const TCHAR* ExpectedNames[] = { TEXT("20260922-210509"), TEXT("20260922-210509-2"), TEXT("20260922-210509-3"), TEXT("20260922-210509-4") };

	// four notes in the same second; the third carries a screenshot
	TArray<FString> Folders;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedNames); ++Index)
	{
		FPlaytestNoteData Note = PlaytestQA::MakeNote(FString::Printf(TEXT("note number %d"), Index + 1));
		Note.Timestamp = Stamp;
		TArray<FColor> Pixels;
		if (Index == 2)
		{
			Pixels.Init(FColor::Green, 2 * 2);
		}
		FString Folder, Error;
		const bool bWritten = FPlaytestNoteWriter::WriteNote(Temp.Path, Note, Pixels.Num() > 0 ? 2 : 0, Pixels.Num() > 0 ? 2 : 0, Pixels, Folder, Error);
		TestTrue(FString::Printf(TEXT("Note %d in the same second is written (error: '%s')"), Index + 1, *Error), bWritten);
		TestEqual(FString::Printf(TEXT("Note %d folder name"), Index + 1), PlaytestQA::FolderName(Folder), FString(ExpectedNames[Index]));
		TestTrue(FString::Printf(TEXT("Note %d folder is inside the root"), Index + 1), PlaytestQA::SamePath(PlaytestQA::FolderParent(Folder), Temp.Path));
		Folders.Add(Folder);
	}

	// nothing was overwritten: every folder still holds its own note, the screenshot is only in the third
	for (int32 Index = 0; Index < Folders.Num(); ++Index)
	{
		FString Json;
		TestTrue(FString::Printf(TEXT("Note %d note.json exists"), Index + 1), FFileHelper::LoadFileToString(Json, *(Folders[Index] / TEXT("note.json"))));
		const FString What = FString::Printf(TEXT("note %d"), Index + 1);
		const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, What, Json);
		if (Root.IsValid())
		{
			PlaytestQA::ExpectString(*this, What, Root, TEXT("text"), FString::Printf(TEXT("note number %d"), Index + 1));
		}
		TestEqual(FString::Printf(TEXT("Note %d has a screenshot.png only if it was given pixels"), Index + 1), FPaths::FileExists(Folders[Index] / TEXT("screenshot.png")) ? 1 : 0, Index == 2 ? 1 : 0);
	}

	// the next second gets the plain name again
	FPlaytestNoteData Next = PlaytestQA::MakeNote(TEXT("next second"));
	Next.Timestamp = Stamp + FTimespan::FromSeconds(1.0);
	FString NextFolder, NextError;
	TestTrue(TEXT("A note one second later is written"), FPlaytestNoteWriter::WriteNote(Temp.Path, Next, 0, 0, TArray<FColor>(), NextFolder, NextError));
	TestEqual(TEXT("A note one second later has no suffix"), PlaytestQA::FolderName(NextFolder), FString(TEXT("20260922-210510")));

	const TArray<FString> Entries = PlaytestQA::ListEntries(Temp.Path);
	TestEqual(FString::Printf(TEXT("Exactly five note folders exist (%s)"), *FString::Join(Entries, TEXT(", "))), Entries.Num(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderExistingTest, "Project.Playtest.QA.Folder.NeverWritesIntoExistingFolder", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderExistingTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	// an older note in the base folder and an empty "-2" folder already exist (e.g. a crash mid-write, or a copy)
	const FString BaseFolder = Temp.Path / TEXT("20260922-210509");
	const FString SecondFolder = Temp.Path / TEXT("20260922-210509-2");
	PlaytestQA::WriteAscii(BaseFolder / TEXT("note.json"), TEXT("SENTINEL"));
	IFileManager::Get().MakeDirectory(*SecondFolder, /*Tree*/ true);

	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("new note"));
	Note.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9);
	FString Folder, Error;
	TestTrue(FString::Printf(TEXT("WriteNote succeeds (error: '%s')"), *Error), FPlaytestNoteWriter::WriteNote(Temp.Path, Note, 0, 0, TArray<FColor>(), Folder, Error));
	TestEqual(TEXT("The new note goes to the first free suffix"), PlaytestQA::FolderName(Folder), FString(TEXT("20260922-210509-3")));

	FString Sentinel;
	FFileHelper::LoadFileToString(Sentinel, *(BaseFolder / TEXT("note.json")));
	TestEqual(TEXT("The existing note.json is untouched"), Sentinel, FString(TEXT("SENTINEL")));
	TestEqual(TEXT("The existing empty folder stays empty"), PlaytestQA::ListEntries(SecondFolder).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderMissingRootTest, "Project.Playtest.QA.Folder.CreatesMissingRoot", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderMissingRootTest::RunTest(const FString& Parameters)
{
	// first note ever: Saved/Playtest (and even its parents) may not exist yet
	PlaytestQA::FScopedTempDir Temp;
	const FString Root = Temp.Path / TEXT("not/created/yet/Playtest");
	TestFalse(TEXT("Precondition: the root does not exist"), IFileManager::Get().DirectoryExists(*Root));

	FString Folder, Error;
	TestTrue(FString::Printf(TEXT("WriteNote creates the missing root (error: '%s')"), *Error), FPlaytestNoteWriter::WriteNote(Root, PlaytestQA::MakeNote(TEXT("first note")), 0, 0, TArray<FColor>(), Folder, Error));
	TestTrue(TEXT("note.json exists in the new root"), FPaths::FileExists(Folder / TEXT("note.json")));
	TestTrue(TEXT("The note folder is inside the requested root"), PlaytestQA::SamePath(PlaytestQA::FolderParent(Folder), Root));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderRelativeRootTest, "Project.Playtest.QA.Folder.RelativeRootGivesAbsoluteFolder", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderRelativeRootTest::RunTest(const FString& Parameters)
{
	// engine paths like FPaths::ProjectSavedDir() are often relative; OutFolder is documented as absolute
	PlaytestQA::FScopedTempDir Temp;
	const FString FullRoot = Temp.Path / TEXT("Relative");
	// relative paths resolve against the process base dir (what FPaths::ConvertRelativePathToFull uses)
	FString ProcessBaseDir = FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::BaseDir()));
	if (!ProcessBaseDir.EndsWith(TEXT("/")))
	{
		ProcessBaseDir += TEXT("/");
	}
	FString RelativeRoot = FullRoot;
	const bool bMadeRelative = FPaths::MakePathRelativeTo(RelativeRoot, *ProcessBaseDir) && FPaths::IsRelative(RelativeRoot);
	if (!TestTrue(FString::Printf(TEXT("Precondition: '%s' is a relative path"), *RelativeRoot), bMadeRelative))
	{
		return true;
	}

	FString Folder, Error;
	TestTrue(FString::Printf(TEXT("WriteNote accepts a relative root (error: '%s')"), *Error), FPlaytestNoteWriter::WriteNote(RelativeRoot, PlaytestQA::MakeNote(TEXT("relative")), 0, 0, TArray<FColor>(), Folder, Error));
	TestFalse(FString::Printf(TEXT("OutFolder '%s' is absolute"), *Folder), FPaths::IsRelative(Folder));
	TestTrue(TEXT("The note landed in the same place the relative root points to"), PlaytestQA::SamePath(PlaytestQA::FolderParent(Folder), FullRoot));
	TestTrue(TEXT("note.json exists"), FPaths::FileExists(Folder / TEXT("note.json")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderTrailingSeparatorTest, "Project.Playtest.QA.Folder.RootWithTrailingSeparator", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderTrailingSeparatorTest::RunTest(const FString& Parameters)
{
	// Windows-style root with backslashes and a trailing separator
	PlaytestQA::FScopedTempDir Temp;
	const FString Root = (Temp.Path / TEXT("Notes")).Replace(TEXT("/"), TEXT("\\")) + TEXT("\\");

	FString Folder, Error;
	TestTrue(FString::Printf(TEXT("WriteNote accepts '%s' (error: '%s')"), *Root, *Error), FPlaytestNoteWriter::WriteNote(Root, PlaytestQA::MakeNote(TEXT("backslashes")), 0, 0, TArray<FColor>(), Folder, Error));
	TestTrue(TEXT("The note folder is directly inside the root"), PlaytestQA::SamePath(PlaytestQA::FolderParent(Folder), Temp.Path / TEXT("Notes")));
	TestTrue(TEXT("note.json exists"), FPaths::FileExists(Folder / TEXT("note.json")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFolderDefaultRootTest, "Project.Playtest.QA.Folder.DefaultRootIsSavedPlaytest", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFolderDefaultRootTest::RunTest(const FString& Parameters)
{
	// spec: notes land in <Project>/Saved/Playtest (read-only check; nothing is written there)
	const FString Actual = FPlaytestNoteWriter::GetDefaultRootDir();
	const FString Expected = FPaths::ProjectSavedDir() / TEXT("Playtest");
	TestTrue(FString::Printf(TEXT("Default root '%s' is <Project>/Saved/Playtest ('%s')"), *Actual, *FPaths::ConvertRelativePathToFull(Expected)), PlaytestQA::SamePath(Actual, Expected));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Failure paths: invalid roots and invalid pixel buffers must fail cleanly (false + error), never crash or leave debris.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPlaytestQAFailureRootIsFileTest, FPlaytestQAFailurePathTestBase, "Project.Playtest.QA.Failure.RootIsAFile", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFailureRootIsFileTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const FString Blocker = Temp.Path / TEXT("blocker.txt");
	PlaytestQA::WriteAscii(Blocker, TEXT("not a folder"));

	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(Blocker, PlaytestQA::MakeNote(TEXT("root is a file")), 0, 0, TArray<FColor>(), Folder, Error);
	TestFalse(TEXT("WriteNote into a root that is a file returns false"), bWritten);
	TestFalse(TEXT("OutError explains the failure"), Error.IsEmpty());
	FString Content;
	FFileHelper::LoadFileToString(Content, *Blocker);
	TestEqual(TEXT("The blocking file is untouched"), Content, FString(TEXT("not a folder")));
	TestEqual(TEXT("Nothing else was created next to it"), PlaytestQA::ListEntries(Temp.Path).Num(), 1);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPlaytestQAFailureRootUnderFileTest, FPlaytestQAFailurePathTestBase, "Project.Playtest.QA.Failure.RootUnderAFile", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFailureRootUnderFileTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const FString Blocker = Temp.Path / TEXT("Saved");
	PlaytestQA::WriteAscii(Blocker, TEXT("a file where a folder should be"));

	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(Blocker / TEXT("Playtest"), PlaytestQA::MakeNote(TEXT("root under a file")), 0, 0, TArray<FColor>(), Folder, Error);
	TestFalse(TEXT("WriteNote into a root whose parent is a file returns false"), bWritten);
	TestFalse(TEXT("OutError explains the failure"), Error.IsEmpty());
	TestEqual(TEXT("Nothing else was created"), PlaytestQA::ListEntries(Temp.Path).Num(), 1);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPlaytestQAFailureInvalidCharsTest, FPlaytestQAFailurePathTestBase, "Project.Playtest.QA.Failure.InvalidCharactersInRoot", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFailureInvalidCharsTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const FString Root = Temp.Path / TEXT("bad<>|?*\"name") / TEXT("Playtest");

	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(Root, PlaytestQA::MakeNote(TEXT("invalid path")), 0, 0, TArray<FColor>(), Folder, Error);
	TestFalse(TEXT("WriteNote into a path with characters Windows forbids returns false"), bWritten);
	TestFalse(TEXT("OutError explains the failure"), Error.IsEmpty());
	const TArray<FString> Entries = PlaytestQA::ListEntries(Temp.Path);
	TestEqual(FString::Printf(TEXT("No partial folders were created (%s)"), *FString::Join(Entries, TEXT(", "))), Entries.Num(), 0);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPlaytestQAFailureEmptyRootTest, FPlaytestQAFailurePathTestBase, "Project.Playtest.QA.Failure.EmptyRoot", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFailureEmptyRootTest::RunTest(const FString& Parameters)
{
	// an empty root is a caller bug: it must be rejected, not resolved against the process directory or a drive root
	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("empty root"));
	Note.Timestamp = FDateTime(2001, 2, 3, 4, 5, 6); // unique name so a stray folder can be found and removed safely
	FString Folder, Error;
	const bool bWritten = FPlaytestNoteWriter::WriteNote(FString(), Note, 0, 0, TArray<FColor>(), Folder, Error);
	if (bWritten)
	{
		AddError(FString::Printf(TEXT("WriteNote accepted an empty root and wrote the note to '%s'"), *Folder));
		if (PlaytestQA::FolderName(Folder).StartsWith(TEXT("20010203-040506")))
		{
			IFileManager::Get().DeleteDirectory(*Folder, /*RequireExists*/ false, /*Tree*/ true);
		}
	}
	else
	{
		TestFalse(TEXT("OutError explains why an empty root is rejected"), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPlaytestQAFailurePixelMismatchTest, FPlaytestQAFailurePathTestBase, "Project.Playtest.QA.Failure.PixelCountMismatch", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFailurePixelMismatchTest::RunTest(const FString& Parameters)
{
	// header contract: pixels are empty or exactly Width * Height. Anything else must not crash and must not produce
	// a half-written note: either reject (false + error, no folder) or save a complete note (no broken screenshot reference).
	PlaytestQA::FScopedTempDir Temp;
	struct FCase
	{
		const TCHAR* Name;
		int32 Width;
		int32 Height;
		int32 NumPixels;
	};
	const FCase Cases[] = {
		{ TEXT("TooFewPixels"), 4, 2, 5 },
		{ TEXT("TooManyPixels"), 4, 2, 9 },
		{ TEXT("ZeroSizeWithPixels"), 0, 0, 8 },
		{ TEXT("NegativeSize"), -4, -2, 8 },
	};
	for (const FCase& Case : Cases)
	{
		const FString Root = Temp.Path / Case.Name;
		TArray<FColor> Pixels;
		Pixels.Init(FColor::Red, Case.NumPixels);
		const FPlaytestNoteData Note = PlaytestQA::MakeNote(FString::Printf(TEXT("pixel case %s"), Case.Name));
		FString Folder, Error;
		const bool bWritten = FPlaytestNoteWriter::WriteNote(Root, Note, Case.Width, Case.Height, Pixels, Folder, Error);
		PlaytestQA::ExpectCleanOutcome(*this, Case.Name, Root, bWritten, Folder, Error, Note.Text);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Average FPS over the last 5 seconds.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsNoSamplesTest, "Project.Playtest.QA.Fps.NoSamplesIsZero", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsNoSamplesTest::RunTest(const FString& Parameters)
{
	FPlaytestFpsTracker Tracker;
	TestEqual(TEXT("A new tracker reports 0 FPS"), Tracker.GetAverageFps(), 0.0f);

	double Now = 0.0;
	PlaytestQA::Feed(Tracker, Now, 320, 1.0 / 64.0);
	Tracker.Reset();
	TestEqual(TEXT("After Reset the tracker reports 0 FPS again"), Tracker.GetAverageFps(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsSingleSampleTest, "Project.Playtest.QA.Fps.SingleSample", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsSingleSampleTest::RunTest(const FString& Parameters)
{
	{
		FPlaytestFpsTracker Tracker;
		Tracker.AddFrame(1000.0, 1.0 / 64.0);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("One 1/64 s frame averages 64 FPS (got %.3f)"), Fps), FMath::IsNearlyEqual(Fps, 64.0f, 0.5f));
	}
	{
		FPlaytestFpsTracker Tracker;
		Tracker.AddFrame(0.5, 0.5);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("One 0.5 s frame averages 2 FPS (got %.3f)"), Fps), FMath::IsNearlyEqual(Fps, 2.0f, 0.05f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsWindowFilledTest, "Project.Playtest.QA.Fps.WindowExactlyFilled", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsWindowFilledTest::RunTest(const FString& Parameters)
{
	// exactly 5.0 s of 64 FPS frames: the whole history is inside the window
	FPlaytestFpsTracker Tracker;
	double Now = 0.0;
	PlaytestQA::Feed(Tracker, Now, 320, 1.0 / 64.0);
	TestTrue(TEXT("Precondition: exactly 5 s were fed"), Now == 5.0);
	const float Fps = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("5.0 s at 64 FPS averages 64 (got %.3f)"), Fps), FMath::IsNearlyEqual(Fps, 64.0f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsWindowBoundaryTest, "Project.Playtest.QA.Fps.WindowBoundary", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsWindowBoundaryTest::RunTest(const FString& Parameters)
{
	// default window (spec: last 5 s): 10 s at 16 FPS, then 64 FPS; check just under, at and just over 5 s of new frames
	FPlaytestFpsTracker Tracker;
	double Now = 0.0;
	PlaytestQA::Feed(Tracker, Now, 160, 1.0 / 16.0);

	PlaytestQA::Feed(Tracker, Now, 256, 1.0 / 64.0); // 4 s of new frames: 1 s of old frames is still inside the window
	const float After4 = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("After 4 s at 64 FPS the older 16 FPS second still counts (16 < avg < 63, got %.3f)"), After4), After4 > 16.0f && After4 < 63.0f);

	PlaytestQA::Feed(Tracker, Now, 64, 1.0 / 64.0); // exactly 5 s of new frames
	const float After5 = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("After exactly 5 s at 64 FPS only the new frames count (64 +/- 1.5, got %.3f)"), After5), FMath::IsNearlyEqual(After5, 64.0f, 1.5f));

	PlaytestQA::Feed(Tracker, Now, 64, 1.0 / 64.0); // 6 s of new frames
	const float After6 = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("After 6 s at 64 FPS the old frames are gone (64 +/- 0.5, got %.3f)"), After6), FMath::IsNearlyEqual(After6, 64.0f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsCustomWindowTest, "Project.Playtest.QA.Fps.CustomWindowHonored", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsCustomWindowTest::RunTest(const FString& Parameters)
{
	// FpsWindowSeconds is configurable, so the tracker must honor other windows too
	{
		FPlaytestFpsTracker Tracker(1.0);
		double Now = 0.0;
		PlaytestQA::Feed(Tracker, Now, 32, 1.0 / 16.0);
		PlaytestQA::Feed(Tracker, Now, 96, 1.0 / 64.0);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("1 s window (constructor): 1.5 s at 64 FPS after 2 s at 16 FPS averages 64 (got %.3f)"), Fps), FMath::IsNearlyEqual(Fps, 64.0f, 0.5f));
	}
	{
		FPlaytestFpsTracker Tracker;
		Tracker.SetWindowSeconds(1.0);
		double Now = 0.0;
		PlaytestQA::Feed(Tracker, Now, 32, 1.0 / 16.0);
		PlaytestQA::Feed(Tracker, Now, 96, 1.0 / 64.0);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("1 s window (SetWindowSeconds): averages 64 (got %.3f)"), Fps), FMath::IsNearlyEqual(Fps, 64.0f, 0.5f));
	}
	{
		FPlaytestFpsTracker Tracker;
		Tracker.SetWindowSeconds(10.0);
		double Now = 0.0;
		PlaytestQA::Feed(Tracker, Now, 80, 1.0 / 16.0);
		PlaytestQA::Feed(Tracker, Now, 320, 1.0 / 64.0);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("10 s window: the 16 FPS half still counts (16 < avg < 60, got %.3f)"), Fps), Fps > 16.0f && Fps < 60.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsNonPositiveWindowTest, "Project.Playtest.QA.Fps.NonPositiveWindowIsClamped", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsNonPositiveWindowTest::RunTest(const FString& Parameters)
{
	for (const double Window : { 0.0, -5.0 })
	{
		FPlaytestFpsTracker Tracker;
		Tracker.SetWindowSeconds(Window);
		double Now = 0.0;
		PlaytestQA::Feed(Tracker, Now, 64, 1.0 / 64.0);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("Window %.1f s is clamped to a small positive window: finite, ~64 FPS (got %.3f)"), Window, Fps), FMath::IsFinite(Fps) && FMath::IsNearlyEqual(Fps, 64.0f, 2.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsZeroDeltaTest, "Project.Playtest.QA.Fps.ZeroDeltaFrameIsFinite", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsZeroDeltaTest::RunTest(const FString& Parameters)
{
	// a 0 s frame (first tick, clamped delta) must never turn avgFps into inf/NaN, which would corrupt note.json
	{
		FPlaytestFpsTracker Tracker;
		Tracker.AddFrame(1.0, 0.0);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("Only a zero-length frame: finite and >= 0 (got %f)"), Fps), FMath::IsFinite(Fps) && Fps >= 0.0f);
	}
	{
		FPlaytestFpsTracker Tracker;
		double Now = 0.0;
		PlaytestQA::Feed(Tracker, Now, 320, 1.0 / 64.0);
		Tracker.AddFrame(Now, 0.0);
		const float Fps = Tracker.GetAverageFps();
		TestTrue(FString::Printf(TEXT("A zero-length frame among 5 s at 64 FPS: finite and ~64 (got %f)"), Fps), FMath::IsFinite(Fps) && FMath::IsNearlyEqual(Fps, 64.0f, 1.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsHitchTest, "Project.Playtest.QA.Fps.HitchLowersAverage", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsHitchTest::RunTest(const FString& Parameters)
{
	// average FPS = frames / seconds. A 2 s freeze inside the window must show: 192 frames + 1 hitch frame in 5 s = ~38.6 FPS.
	// (Averaging per-frame FPS values would report ~63.7 and hide the freeze Jimmy is reporting.)
	FPlaytestFpsTracker Tracker;
	double Now = 0.0;
	PlaytestQA::Feed(Tracker, Now, 320, 1.0 / 64.0);
	Now += 2.0;
	Tracker.AddFrame(Now, 2.0);
	const float Fps = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("3 s at 64 FPS + a 2 s hitch averages ~38.6 FPS over the last 5 s (35..42, got %.3f)"), Fps), Fps >= 35.0f && Fps <= 42.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsHugeSpikeTest, "Project.Playtest.QA.Fps.HugeSpikeIsFiniteAndAgesOut", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsHugeSpikeTest::RunTest(const FString& Parameters)
{
	// a debugger break or a suspended laptop: one absurdly long frame
	FPlaytestFpsTracker Tracker;
	double Now = 0.0;
	PlaytestQA::Feed(Tracker, Now, 320, 1.0 / 64.0);
	Now += 1.0e9;
	Tracker.AddFrame(Now, 1.0e9);
	const float DuringSpike = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("Right after a 1e9 s frame: finite and in [0, 1) FPS (got %g)"), DuringSpike), FMath::IsFinite(DuringSpike) && DuringSpike >= 0.0f && DuringSpike < 1.0f);

	PlaytestQA::Feed(Tracker, Now, 384, 1.0 / 64.0); // 6 s of normal frames
	const float Recovered = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("6 s later the spike has left the window (64 +/- 0.5, got %.3f)"), Recovered), FMath::IsNearlyEqual(Recovered, 64.0f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAFpsLongSessionTest, "Project.Playtest.QA.Fps.LongSessionStaysAccurate", PLAYTEST_QA_FLAGS)

bool FPlaytestQAFpsLongSessionTest::RunTest(const FString& Parameters)
{
	// one hour of play, then a drop: the average must follow the last 5 s, not the session
	FPlaytestFpsTracker Tracker;
	double Now = 0.0;
	PlaytestQA::Feed(Tracker, Now, 3600 * 64, 1.0 / 64.0);
	const float AfterHour = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("After 1 h at 64 FPS the average is 64 (got %.3f)"), AfterHour), FMath::IsNearlyEqual(AfterHour, 64.0f, 0.5f));

	PlaytestQA::Feed(Tracker, Now, 96, 1.0 / 16.0); // 6 s at 16 FPS
	const float AfterDrop = Tracker.GetAverageFps();
	TestTrue(FString::Printf(TEXT("6 s after dropping to 16 FPS the average is 16 (got %.3f)"), AfterDrop), FMath::IsNearlyEqual(AfterDrop, 16.0f, 0.5f));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Commit id: "if available" -> a real commit id or "unknown", never stale or garbage.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitUnbornTest, "Project.Playtest.QA.Git.UnbornBranchIsUnknown", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitUnbornTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const FString Repo = Temp.Path / TEXT("Unborn");
	PlaytestQA::WriteAscii(Repo / TEXT(".git/HEAD"), TEXT("ref: refs/heads/main\n"));
	IFileManager::Get().MakeDirectory(*(Repo / TEXT(".git/refs/heads")), /*Tree*/ true);
	TestEqual(TEXT("A repo with no commits yet gives 'unknown'"), FPlaytestNoteWriter::ReadGitCommit(Repo), FString(TEXT("unknown")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitCrlfTest, "Project.Playtest.QA.Git.CrlfLineEndings", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitCrlfTest::RunTest(const FString& Parameters)
{
	// Windows tools can leave CRLF in .git files; the commit id must not carry a stray '\r'
	PlaytestQA::FScopedTempDir Temp;
	const FString HashA = FString::ChrN(40, TEXT('a'));
	const FString HashB = FString::ChrN(40, TEXT('b'));
	const FString HashC = FString::ChrN(40, TEXT('c'));

	const FString Loose = Temp.Path / TEXT("Loose");
	PlaytestQA::WriteAscii(Loose / TEXT(".git/HEAD"), TEXT("ref: refs/heads/main\r\n"));
	PlaytestQA::WriteAscii(Loose / TEXT(".git/refs/heads/main"), HashA + TEXT("\r\n"));
	TestEqual(TEXT("Loose ref with CRLF"), FPlaytestNoteWriter::ReadGitCommit(Loose), HashA);

	const FString Detached = Temp.Path / TEXT("Detached");
	PlaytestQA::WriteAscii(Detached / TEXT(".git/HEAD"), HashB + TEXT("\r\n"));
	TestEqual(TEXT("Detached HEAD with CRLF"), FPlaytestNoteWriter::ReadGitCommit(Detached), HashB);

	const FString Packed = Temp.Path / TEXT("Packed");
	PlaytestQA::WriteAscii(Packed / TEXT(".git/HEAD"), TEXT("ref: refs/heads/main\r\n"));
	PlaytestQA::WriteAscii(Packed / TEXT(".git/packed-refs"), TEXT("# pack-refs with: peeled fully-peeled sorted\r\n") + HashC + TEXT(" refs/heads/main\r\n"));
	TestEqual(TEXT("Packed ref with CRLF"), FPlaytestNoteWriter::ReadGitCommit(Packed), HashC);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitPackedExactTest, "Project.Playtest.QA.Git.PackedRefsExactBranchMatch", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitPackedExactTest::RunTest(const FString& Parameters)
{
	// the branch name must match exactly: not a branch it is a prefix of, not a remote or tag, not a peeled line
	PlaytestQA::FScopedTempDir Temp;
	const FString Repo = Temp.Path / TEXT("Packed");
	const FString OldMain = FString::ChrN(40, TEXT('a'));
	const FString Main = FString::ChrN(40, TEXT('b'));
	const FString Tag = FString::ChrN(40, TEXT('c'));
	const FString Peeled = FString::ChrN(40, TEXT('d'));
	const FString Remote = FString::ChrN(40, TEXT('e'));
	PlaytestQA::WriteAscii(Repo / TEXT(".git/HEAD"), TEXT("ref: refs/heads/main\n"));
	PlaytestQA::WriteAscii(Repo / TEXT(".git/packed-refs"),
		TEXT("# pack-refs with: peeled fully-peeled sorted\n")
		+ OldMain + TEXT(" refs/heads/main-old\n")
		+ Remote + TEXT(" refs/remotes/origin/main\n")
		+ Tag + TEXT(" refs/tags/v1\n")
		+ TEXT("^") + Peeled + TEXT("\n")
		+ Main + TEXT(" refs/heads/main\n"));
	TestEqual(TEXT("packed-refs: refs/heads/main resolves to its own line"), FPlaytestNoteWriter::ReadGitCommit(Repo), Main);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitLooseWinsTest, "Project.Playtest.QA.Git.LooseRefWinsOverPacked", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitLooseWinsTest::RunTest(const FString& Parameters)
{
	// normal state after 'git gc' plus a new commit: packed-refs holds the old id, the loose ref the current one
	PlaytestQA::FScopedTempDir Temp;
	const FString Repo = Temp.Path / TEXT("Both");
	const FString Stale = FString::ChrN(40, TEXT('a'));
	const FString Current = FString::ChrN(40, TEXT('b'));
	PlaytestQA::WriteAscii(Repo / TEXT(".git/HEAD"), TEXT("ref: refs/heads/main\n"));
	PlaytestQA::WriteAscii(Repo / TEXT(".git/packed-refs"), TEXT("# pack-refs with: peeled fully-peeled sorted\n") + Stale + TEXT(" refs/heads/main\n"));
	PlaytestQA::WriteAscii(Repo / TEXT(".git/refs/heads/main"), Current + TEXT("\n"));
	TestEqual(TEXT("The loose ref (current commit) wins over the stale packed entry"), FPlaytestNoteWriter::ReadGitCommit(Repo), Current);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitMalformedTest, "Project.Playtest.QA.Git.MalformedRepoIsUnknown", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitMalformedTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Head;    // nullptr = no HEAD file
		const TCHAR* MainRef; // nullptr = no refs/heads/main file
	};
	const FCase Cases[] = {
		{ TEXT("NoHeadFile"), nullptr, nullptr },
		{ TEXT("EmptyHead"), TEXT(""), nullptr },
		{ TEXT("RefWithoutName"), TEXT("ref: \n"), nullptr },
		{ TEXT("GarbageHead"), TEXT("this is not a commit\n"), nullptr },
		{ TEXT("GarbageRef"), TEXT("ref: refs/heads/main\n"), TEXT("not-a-hash\n") },
		{ TEXT("ShortRef"), TEXT("ref: refs/heads/main\n"), TEXT("abc123\n") },
		{ TEXT("EmptyRef"), TEXT("ref: refs/heads/main\n"), TEXT("") },
	};
	for (const FCase& Case : Cases)
	{
		const FString Repo = Temp.Path / Case.Name;
		IFileManager::Get().MakeDirectory(*(Repo / TEXT(".git/refs/heads")), /*Tree*/ true);
		if (Case.Head)
		{
			PlaytestQA::WriteAscii(Repo / TEXT(".git/HEAD"), Case.Head);
		}
		if (Case.MainRef)
		{
			PlaytestQA::WriteAscii(Repo / TEXT(".git/refs/heads/main"), Case.MainRef);
		}
		const FString Result = FPlaytestNoteWriter::ReadGitCommit(Repo);
		TestTrue(FString::Printf(TEXT("%s: result '%s' is 'unknown' or a full commit id"), Case.Name, *Result), PlaytestQA::IsHashOrUnknown(Result));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitWorktreeTest, "Project.Playtest.QA.Git.WorktreeGitFileIsSafe", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitWorktreeTest::RunTest(const FString& Parameters)
{
	// in a git worktree (the C++ lanes are worktrees) .git is a FILE pointing at <main>/.git/worktrees/<name>;
	// branch refs live in the main (common) git dir, found through the "commondir" file. Since round 2 (B5) both
	// cases must give the real commit id: a note from a lane build must not record "unknown".
	PlaytestQA::FScopedTempDir Temp;
	const FString MainGitDir = Temp.Path / TEXT("MainRepo/.git");
	const FString DetachedHash = FString::ChrN(40, TEXT('f'));
	const FString BranchHash = FString::ChrN(40, TEXT('e'));
	PlaytestQA::WriteAscii(MainGitDir / TEXT("refs/heads/lane/eng1"), BranchHash + TEXT("\n"));

	// detached HEAD in the worktree git dir
	const FString DetachedRepo = Temp.Path / TEXT("WorktreeDetached");
	const FString DetachedGitDir = MainGitDir / TEXT("worktrees/wt1");
	PlaytestQA::WriteAscii(DetachedRepo / TEXT(".git"), FString::Printf(TEXT("gitdir: %s\n"), *DetachedGitDir));
	PlaytestQA::WriteAscii(DetachedGitDir / TEXT("HEAD"), DetachedHash + TEXT("\n"));
	PlaytestQA::WriteAscii(DetachedGitDir / TEXT("commondir"), TEXT("../..\n"));
	const FString DetachedResult = FPlaytestNoteWriter::ReadGitCommit(DetachedRepo);
	TestEqual(TEXT("Worktree, detached HEAD: the commit in the worktree's own HEAD"), DetachedResult, DetachedHash);

	// branch checked out in the worktree (like lane/eng1): the ref is only in the common dir
	const FString BranchRepo = Temp.Path / TEXT("WorktreeBranch");
	const FString BranchGitDir = MainGitDir / TEXT("worktrees/eng1");
	PlaytestQA::WriteAscii(BranchRepo / TEXT(".git"), FString::Printf(TEXT("gitdir: %s\n"), *BranchGitDir));
	PlaytestQA::WriteAscii(BranchGitDir / TEXT("HEAD"), TEXT("ref: refs/heads/lane/eng1\n"));
	PlaytestQA::WriteAscii(BranchGitDir / TEXT("commondir"), TEXT("../..\n"));
	const FString BranchResult = FPlaytestNoteWriter::ReadGitCommit(BranchRepo);
	TestEqual(TEXT("Worktree, branch HEAD: the branch commit from the common dir (commondir followed)"), BranchResult, BranchHash);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Settings defaults match the spec.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAConfigDefaultsTest, "Project.Playtest.QA.Config.DefaultsMatchSpec", PLAYTEST_QA_FLAGS)

bool FPlaytestQAConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UPlaytestFeedbackSubsystem* Defaults = GetDefault<UPlaytestFeedbackSubsystem>();
	if (!TestNotNull(TEXT("Subsystem class defaults exist"), Defaults))
	{
		return true;
	}
	TestTrue(FString::Printf(TEXT("Feedback key is F8 (got %s)"), *Defaults->FeedbackKey.ToString()), Defaults->FeedbackKey == EKeys::F8);
	TestTrue(TEXT("The game pauses while the note box is open"), Defaults->bPauseWhileTyping);
	TestTrue(FString::Printf(TEXT("FPS window is 5 s (got %.2f)"), Defaults->FpsWindowSeconds), FMath::IsNearlyEqual(Defaults->FpsWindowSeconds, 5.0f));
	TestTrue(FString::Printf(TEXT("Screenshot timeout is positive (got %.2f)"), Defaults->ScreenshotTimeoutSeconds), Defaults->ScreenshotTimeoutSeconds > 0.0f);
	return true;
}

// =====================================================================================================================
// Round 2 (4f56d15): UTC timestamp, camera block, multi-line text, F8 routing rules, confirmation toast, worktrees.
// =====================================================================================================================

namespace PlaytestQA
{
	static const TCHAR* KeyActionName(const EPlaytestFeedbackKeyAction Action)
	{
		switch (Action)
		{
		case EPlaytestFeedbackKeyAction::PassThrough: return TEXT("PassThrough");
		case EPlaytestFeedbackKeyAction::Consume: return TEXT("Consume");
		case EPlaytestFeedbackKeyAction::OpenNote: return TEXT("OpenNote");
		default: return TEXT("?");
		}
	}

	static const TCHAR* BoxActionName(const EPlaytestNoteBoxKeyAction Action)
	{
		switch (Action)
		{
		case EPlaytestNoteBoxKeyAction::PassThrough: return TEXT("PassThrough");
		case EPlaytestNoteBoxKeyAction::Submit: return TEXT("Submit");
		case EPlaytestNoteBoxKeyAction::Cancel: return TEXT("Cancel");
		case EPlaytestNoteBoxKeyAction::Consume: return TEXT("Consume");
		default: return TEXT("?");
		}
	}

	/** F8 during normal play: feedback key, first press, session running, not ejected, nobody typing */
	static FPlaytestFeedbackKeyContext PlayingF8()
	{
		FPlaytestFeedbackKeyContext Context;
		Context.bIsFeedbackKey = true;
		Context.bIsRepeat = false;
		Context.bSessionRunning = true;
		Context.bEjected = false;
		Context.bTypingInText = false;
		return Context;
	}

	static void ExpectKeyAction(FAutomationTestBase& Test, const FString& What, const FPlaytestFeedbackKeyContext& Context, const EPlaytestFeedbackKeyAction Expected)
	{
		const EPlaytestFeedbackKeyAction Actual = FPlaytestFeedbackRules::DecideFeedbackKey(Context);
		Test.TestTrue(FString::Printf(TEXT("%s: expected %s, got %s"), *What, KeyActionName(Expected), KeyActionName(Actual)), Actual == Expected);
	}

	static FPlaytestFeedbackCandidate Candidate(const bool bRunning, const int32 PIEInstance, const uint64 LastFocusSerial = 0, const bool bFocused = false, const bool bPrimary = false, const bool bActive = false)
	{
		FPlaytestFeedbackCandidate Out;
		Out.bSessionRunning = bRunning;
		Out.PIEInstance = PIEInstance;
		Out.LastFocusSerial = LastFocusSerial;
		Out.bViewportFocused = bFocused;
		Out.bIsPrimaryPIE = bPrimary;
		Out.bFeedbackActive = bActive;
		return Out;
	}

	static void ExpectHandler(FAutomationTestBase& Test, const FString& What, const TArray<FPlaytestFeedbackCandidate>& Candidates, const int32 ExpectedIndex)
	{
		const int32 Actual = FPlaytestFeedbackRules::ChooseHandler(Candidates);
		Test.TestEqual(FString::Printf(TEXT("%s: chosen index"), *What), Actual, ExpectedIndex);
	}

	/** Deterministic multi-line note of exactly Length characters: LF line breaks, no leading/trailing whitespace */
	static FString MakeMultiLineText(const int32 Length)
	{
		FString Out;
		for (int32 Line = 1; Out.Len() < Length; ++Line)
		{
			Out += FString::Printf(TEXT("Line %03d: the jetty rail \"floats\" a bit, see C:\\path %s\n"), Line, *FromCodePoints({ 0xE9 }));
		}
		Out.LeftInline(Length);
		Out[Length - 1] = TEXT('x'); // never end on whitespace or a line break
		return Out;
	}

	/** Expects Object.Name to be JSON null */
	static void ExpectNull(FAutomationTestBase& Test, const FString& What, const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		ExpectField(Test, What, Object, Name, EJson::Null);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// UTC timestamp: note.json "timestamp" is UTC (ISO 8601, ms, Z); the folder name stays local time.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATimestampFormatTest, "Project.Playtest.QA.Timestamp.UtcFormat", PLAYTEST_QA_FLAGS)

bool FPlaytestQATimestampFormatTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		FDateTime Utc;
		const TCHAR* Expected;
		const TCHAR* Why;
	};
	const FCase Cases[] = {
		{ FDateTime(2026, 9, 23, 2, 5, 9, 250), TEXT("2026-09-23T02:05:09.250Z"), TEXT("documented example") },
		{ FDateTime(2027, 1, 1, 0, 0, 0, 0), TEXT("2027-01-01T00:00:00.000Z"), TEXT("midnight, milliseconds written as .000") },
		{ FDateTime(2026, 12, 31, 23, 59, 59, 999), TEXT("2026-12-31T23:59:59.999Z"), TEXT("largest values, .999 not rounded up") },
		{ FDateTime(2028, 2, 29, 13, 7, 3, 7), TEXT("2028-02-29T13:07:03.007Z"), TEXT("leap day, 24-hour clock, zero-padded milliseconds") },
	};
	for (const FCase& Case : Cases)
	{
		const FString Text = FPlaytestNoteWriter::MakeUtcTimestamp(Case.Utc);
		TestEqual(FString::Printf(TEXT("MakeUtcTimestamp: %s"), Case.Why), Text, FString(Case.Expected));

		FDateTime Parsed;
		const bool bParsed = FDateTime::ParseIso8601(*Text, Parsed);
		TestTrue(FString::Printf(TEXT("'%s' parses back as ISO 8601 to the same UTC instant"), *Text), bParsed && Parsed == Case.Utc);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATimestampStampNowTest, "Project.Playtest.QA.Timestamp.StampNowIsOneInstant", PLAYTEST_QA_FLAGS)

bool FPlaytestQATimestampStampNowTest::RunTest(const FString& Parameters)
{
	const FDateTime UtcBefore = FDateTime::UtcNow();
	FPlaytestNoteData Note;
	FPlaytestNoteWriter::StampNow(Note);
	const FTimespan Offset = FPlaytestNoteWriter::GetLocalUtcOffset();

	TestTrue(FString::Printf(TEXT("TimestampUtc is the UTC clock now (%s vs %s)"), *Note.TimestampUtc.ToIso8601(), *UtcBefore.ToIso8601()), FMath::Abs((Note.TimestampUtc - UtcBefore).GetTotalSeconds()) < 5.0);
	TestTrue(FString::Printf(TEXT("Timestamp is the local clock now (%s)"), *Note.Timestamp.ToString()), FMath::Abs((Note.Timestamp - FDateTime::Now()).GetTotalSeconds()) < 5.0);
	TestTrue(FString::Printf(TEXT("Local minus UTC equals GetLocalUtcOffset exactly (one clock reading): %.3f h vs %.3f h"), (Note.Timestamp - Note.TimestampUtc).GetTotalHours(), Offset.GetTotalHours()),
		(Note.Timestamp - Note.TimestampUtc) == Offset);
	TestTrue(FString::Printf(TEXT("The offset is whole minutes (%lld ticks)"), Offset.GetTicks()), Offset.GetTicks() % ETimespan::TicksPerMinute == 0);
	TestTrue(FString::Printf(TEXT("The offset is a real time zone, -12 h..+14 h (%.2f h)"), Offset.GetTotalHours()), Offset.GetTotalHours() >= -12.0 && Offset.GetTotalHours() <= 14.0);
	const FTimespan OsOffset = FDateTime::Now() - FDateTime::UtcNow();
	TestTrue(FString::Printf(TEXT("The offset matches the OS clocks within a minute (%.3f h vs %.3f h)"), Offset.GetTotalHours(), OsOffset.GetTotalHours()), FMath::Abs((OsOffset - Offset).GetTotalSeconds()) < 60.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATimestampFolderVsJsonTest, "Project.Playtest.QA.Timestamp.FolderLocalJsonUtc", PLAYTEST_QA_FLAGS)

bool FPlaytestQATimestampFolderVsJsonTest::RunTest(const FString& Parameters)
{
	// the folder is named in local time, note.json "timestamp" is the same instant in UTC, even across a date change
	struct FCase
	{
		FDateTime Local;
		FDateTime Utc;
		const TCHAR* Folder;
		const TCHAR* Json;
		const TCHAR* Why;
	};
	const FCase Cases[] = {
		{ FDateTime(2026, 9, 22, 21, 5, 9, 250), FDateTime(2026, 9, 23, 4, 5, 9, 250), TEXT("20260922-210509"), TEXT("2026-09-23T04:05:09.250Z"), TEXT("UTC-7: UTC is already the next day") },
		{ FDateTime(2027, 1, 1, 5, 0, 0, 0), FDateTime(2026, 12, 31, 23, 15, 0, 0), TEXT("20270101-050000"), TEXT("2026-12-31T23:15:00.000Z"), TEXT("UTC+5:45: UTC is still the previous year") },
		{ FDateTime(2026, 9, 22, 14, 3, 7, 0), FDateTime(2026, 9, 22, 14, 3, 7, 0), TEXT("20260922-140307"), TEXT("2026-09-22T14:03:07.000Z"), TEXT("UTC+0: both the same") },
	};
	for (const FCase& Case : Cases)
	{
		FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("timestamp check"));
		Note.Timestamp = Case.Local;
		Note.TimestampUtc = Case.Utc;
		TestEqual(FString::Printf(TEXT("%s: folder name is local time"), Case.Why), FPlaytestNoteWriter::MakeFolderName(Note.Timestamp), FString(Case.Folder));
		TestEqual(FString::Printf(TEXT("%s: GetUtcTimestampText uses TimestampUtc"), Case.Why), FPlaytestNoteWriter::GetUtcTimestampText(Note), FString(Case.Json));
		const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, Case.Why, FPlaytestNoteWriter::ToJsonString(Note, false));
		PlaytestQA::ExpectString(*this, Case.Why, Root, TEXT("timestamp"), FString(Case.Json));
	}

	// and the same on disk: folder local, note.json UTC
	PlaytestQA::FScopedTempDir Temp;
	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("on disk"));
	Note.Timestamp = Cases[0].Local;
	Note.TimestampUtc = Cases[0].Utc;
	FString Folder, Error;
	TestTrue(FString::Printf(TEXT("WriteNote succeeds (error: '%s')"), *Error), FPlaytestNoteWriter::WriteNote(Temp.Path, Note, 0, 0, TArray<FColor>(), Folder, Error));
	TestEqual(TEXT("On disk: the folder is named in local time"), PlaytestQA::FolderName(Folder), FString(Cases[0].Folder));
	FString Json;
	FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")));
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, TEXT("on disk"), Json);
	PlaytestQA::ExpectString(*this, TEXT("on disk"), Root, TEXT("timestamp"), FString(Cases[0].Json));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATimestampDerivedTest, "Project.Playtest.QA.Timestamp.DerivedWhenUtcUnset", PLAYTEST_QA_FLAGS)

bool FPlaytestQATimestampDerivedTest::RunTest(const FString& Parameters)
{
	// header contract: TimestampUtc unset -> derived from Timestamp with the machine's offset; both unset -> ""
	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("no utc"));
	Note.Timestamp = FDateTime(2026, 9, 22, 21, 5, 9, 250);
	Note.TimestampUtc = FDateTime();
	const FString Expected = FPlaytestNoteWriter::MakeUtcTimestamp(Note.Timestamp - FPlaytestNoteWriter::GetLocalUtcOffset());
	TestEqual(TEXT("Only local time set: UTC is local minus the machine offset"), FPlaytestNoteWriter::GetUtcTimestampText(Note), Expected);
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, TEXT("only local"), FPlaytestNoteWriter::ToJsonString(Note, false));
	PlaytestQA::ExpectString(*this, TEXT("only local"), Root, TEXT("timestamp"), Expected);

	const FPlaytestNoteData Empty = FPlaytestNoteData();
	TestEqual(TEXT("Neither time set: empty text"), FPlaytestNoteWriter::GetUtcTimestampText(Empty), FString());
	const TSharedPtr<FJsonObject> EmptyRoot = PlaytestQA::ParseStrict(*this, TEXT("no times"), FPlaytestNoteWriter::ToJsonString(Empty, false));
	PlaytestQA::ExpectString(*this, TEXT("no times"), EmptyRoot, TEXT("timestamp"), FString());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Camera block: camera {location {x,y,z}, rotation {pitch,yaw,roll}}, NaN/Inf written as null.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQACameraSchemaTest, "Project.Playtest.QA.Json.CameraBlockSchema", PLAYTEST_QA_FLAGS)

bool FPlaytestQACameraSchemaTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("look at the rock"));
	// every camera value differs from the player values, so a copied or swapped block is caught
	Note.CameraLocation = FVector(1100.5, -640.25, 250.75);
	Note.CameraRotation = FRotator(-35.5, 140.25, 1.5);

	FString Folder, Error, Warning;
	if (!TestTrue(FString::Printf(TEXT("WriteNote succeeds (error: '%s')"), *Error), FPlaytestNoteWriter::WriteNote(Temp.Path, Note, 0, 0, TArray<FColor>(), Folder, Error, &Warning)))
	{
		return true;
	}
	TestTrue(FString::Printf(TEXT("A clean note produces no warning (got '%s')"), *Warning), Warning.IsEmpty());
	FString Json;
	FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")));
	const FString What = TEXT("note.json");
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, What, Json);
	if (!Root.IsValid())
	{
		return true;
	}

	const TSharedPtr<FJsonObject> Camera = PlaytestQA::ExpectObject(*this, What, Root, TEXT("camera"));
	const TSharedPtr<FJsonObject> CameraLocation = PlaytestQA::ExpectObject(*this, TEXT("camera"), Camera, TEXT("location"));
	PlaytestQA::ExpectNumber(*this, TEXT("camera.location"), CameraLocation, TEXT("x"), Note.CameraLocation.X, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("camera.location"), CameraLocation, TEXT("y"), Note.CameraLocation.Y, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("camera.location"), CameraLocation, TEXT("z"), Note.CameraLocation.Z, 1e-3);
	const TSharedPtr<FJsonObject> CameraRotation = PlaytestQA::ExpectObject(*this, TEXT("camera"), Camera, TEXT("rotation"));
	PlaytestQA::ExpectNumber(*this, TEXT("camera.rotation"), CameraRotation, TEXT("pitch"), Note.CameraRotation.Pitch, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("camera.rotation"), CameraRotation, TEXT("yaw"), Note.CameraRotation.Yaw, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("camera.rotation"), CameraRotation, TEXT("roll"), Note.CameraRotation.Roll, 1e-3);

	// the player pose is still its own
	const TSharedPtr<FJsonObject> Location = PlaytestQA::ExpectObject(*this, What, Root, TEXT("location"));
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("x"), Note.Location.X, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("location"), Location, TEXT("z"), Note.Location.Z, 1e-3);
	const TSharedPtr<FJsonObject> Rotation = PlaytestQA::ExpectObject(*this, What, Root, TEXT("rotation"));
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("pitch"), Note.Rotation.Pitch, 1e-3);
	PlaytestQA::ExpectNumber(*this, TEXT("rotation"), Rotation, TEXT("yaw"), Note.Rotation.Yaw, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQACameraNonFiniteTest, "Project.Playtest.QA.Json.CameraNonFiniteIsNull", PLAYTEST_QA_FLAGS)

bool FPlaytestQACameraNonFiniteTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	FPlaytestNoteData Note = PlaytestQA::MakeNote(TEXT("camera went weird"));
	Note.CameraLocation.X = NaN;
	Note.CameraLocation.Y = 5.0;
	Note.CameraLocation.Z = Inf;
	Note.CameraRotation.Pitch = -Inf;
	Note.CameraRotation.Yaw = 90.0;
	Note.CameraRotation.Roll = NaN;

	FString Folder, Error, Warning;
	if (!TestTrue(FString::Printf(TEXT("WriteNote still saves the note (error: '%s')"), *Error), FPlaytestNoteWriter::WriteNote(Temp.Path, Note, 0, 0, TArray<FColor>(), Folder, Error, &Warning)))
	{
		return true;
	}
	TestFalse(TEXT("A warning reports the NaN/Inf values written as null"), Warning.IsEmpty());
	FString Json;
	FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")));
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, TEXT("non-finite camera"), Json);
	if (!Root.IsValid())
	{
		return true;
	}
	PlaytestQA::ExpectString(*this, TEXT("non-finite camera"), Root, TEXT("text"), Note.Text);
	const TSharedPtr<FJsonObject> Camera = PlaytestQA::ExpectObject(*this, TEXT("note.json"), Root, TEXT("camera"));
	const TSharedPtr<FJsonObject> CameraLocation = PlaytestQA::ExpectObject(*this, TEXT("camera"), Camera, TEXT("location"));
	PlaytestQA::ExpectNull(*this, TEXT("camera.location (NaN)"), CameraLocation, TEXT("x"));
	PlaytestQA::ExpectNumber(*this, TEXT("camera.location"), CameraLocation, TEXT("y"), 5.0, 1e-9);
	PlaytestQA::ExpectNull(*this, TEXT("camera.location (+Inf)"), CameraLocation, TEXT("z"));
	const TSharedPtr<FJsonObject> CameraRotation = PlaytestQA::ExpectObject(*this, TEXT("camera"), Camera, TEXT("rotation"));
	PlaytestQA::ExpectNull(*this, TEXT("camera.rotation (-Inf)"), CameraRotation, TEXT("pitch"));
	PlaytestQA::ExpectNumber(*this, TEXT("camera.rotation"), CameraRotation, TEXT("yaw"), 90.0, 1e-9);
	PlaytestQA::ExpectNull(*this, TEXT("camera.rotation (NaN)"), CameraRotation, TEXT("roll"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQADefaultRound2FieldsTest, "Project.Playtest.QA.Json.DefaultNoteHasCameraAndTimestamp", PLAYTEST_QA_FLAGS)

bool FPlaytestQADefaultRound2FieldsTest::RunTest(const FString& Parameters)
{
	const FString What = TEXT("default note");
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, What, FPlaytestNoteWriter::ToJsonString(FPlaytestNoteData(), false));
	const TSharedPtr<FJsonObject> Camera = PlaytestQA::ExpectObject(*this, What, Root, TEXT("camera"));
	const TSharedPtr<FJsonObject> CameraLocation = PlaytestQA::ExpectObject(*this, TEXT("camera"), Camera, TEXT("location"));
	const TSharedPtr<FJsonObject> CameraRotation = PlaytestQA::ExpectObject(*this, TEXT("camera"), Camera, TEXT("rotation"));
	for (const TCHAR* Axis : { TEXT("x"), TEXT("y"), TEXT("z") })
	{
		PlaytestQA::ExpectNumber(*this, TEXT("camera.location"), CameraLocation, Axis, 0.0, 1e-9);
	}
	for (const TCHAR* Axis : { TEXT("pitch"), TEXT("yaw"), TEXT("roll") })
	{
		PlaytestQA::ExpectNumber(*this, TEXT("camera.rotation"), CameraRotation, Axis, 0.0, 1e-9);
	}
	PlaytestQA::ExpectString(*this, What, Root, TEXT("timestamp"), FString());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Multi-line note text: Slate CRLF -> "\n", ends trimmed, up to 2000 characters, newlines survive note.json.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQANormalizeTextTest, "Project.Playtest.QA.Text.NormalizeLineEndings", PLAYTEST_QA_FLAGS)

bool FPlaytestQANormalizeTextTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		FString Raw;
		FString Expected;
		const TCHAR* Why;
	};
	const FString Accents = PlaytestQA::FromCodePoints({ 0xE9, 0x20, 0x1F3A3 });
	const FCase Cases[] = {
		{ TEXT("line one\r\nline two"), TEXT("line one\nline two"), TEXT("CRLF (Slate on Windows) becomes LF") },
		{ TEXT("line one\nline two"), TEXT("line one\nline two"), TEXT("LF stays LF") },
		{ TEXT("a\r\nb\nc\r\n\r\nd"), TEXT("a\nb\nc\n\nd"), TEXT("mixed endings; a blank line inside the note is kept") },
		{ TEXT("\r\n  hello there \t\r\n"), TEXT("hello there"), TEXT("leading/trailing spaces, tabs and line breaks are trimmed") },
		{ TEXT("keep   inner  spaces\tand tabs"), TEXT("keep   inner  spaces\tand tabs"), TEXT("inner whitespace is kept") },
		{ TEXT("spaces at a line end   \r\nnext"), TEXT("spaces at a line end   \nnext"), TEXT("only the ends of the whole note are trimmed, not each line") },
		{ TEXT(""), TEXT(""), TEXT("empty stays empty") },
		{ TEXT(" \r\n\t \r\n"), TEXT(""), TEXT("whitespace-only becomes empty") },
		{ Accents + TEXT("\r\nok"), Accents + TEXT("\nok"), TEXT("non-ASCII text is untouched") },
	};
	for (const FCase& Case : Cases)
	{
		const FString Actual = FPlaytestFeedbackRules::NormalizeNoteText(Case.Raw);
		TestTrue(FString::Printf(TEXT("NormalizeNoteText: %s (%s)"), Case.Why, *PlaytestQA::DescribeDifference(Actual, Case.Expected)), Actual.Equals(Case.Expected, ESearchCase::CaseSensitive));
		TestFalse(FString::Printf(TEXT("NormalizeNoteText: %s: no CR left"), Case.Why), Actual.Contains(TEXT("\r")));
		TestTrue(FString::Printf(TEXT("NormalizeNoteText: %s: normalizing twice changes nothing"), Case.Why), FPlaytestFeedbackRules::NormalizeNoteText(Actual).Equals(Actual, ESearchCase::CaseSensitive));
	}
	// informational: a lone CR (old Mac line ending) never comes from Slate
	AddInfo(FString::Printf(TEXT("Lone CR 'a\\rb' normalizes to '%s'"), *FPlaytestFeedbackRules::NormalizeNoteText(TEXT("a\rb")).ReplaceCharWithEscapedChar()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAMultiLineJsonTest, "Project.Playtest.QA.Text.MultiLineJsonRoundTrip", PLAYTEST_QA_FLAGS)

bool FPlaytestQAMultiLineJsonTest::RunTest(const FString& Parameters)
{
	// what the box gives (CRLF) -> what is saved ("\n") -> note.json escapes it as \n and reads back identical
	PlaytestQA::FScopedTempDir Temp;
	const FString Raw = TEXT("First line\r\nSecond line with \"quotes\"\r\n\r\nFourth after a blank line\r\n");
	const FString Saved = FPlaytestFeedbackRules::NormalizeNoteText(Raw);
	TestEqual(TEXT("Saved text"), Saved, FString(TEXT("First line\nSecond line with \"quotes\"\n\nFourth after a blank line")));

	FString Folder, Error;
	if (!TestTrue(FString::Printf(TEXT("WriteNote succeeds (error: '%s')"), *Error), FPlaytestNoteWriter::WriteNote(Temp.Path, PlaytestQA::MakeNote(Saved), 0, 0, TArray<FColor>(), Folder, Error)))
	{
		return true;
	}
	FString Json;
	FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")));
	TestTrue(TEXT("note.json stores the line break as the escape \\n"), Json.Contains(TEXT("First line\\nSecond line")));
	TestTrue(TEXT("note.json keeps the blank line as \\n\\n"), Json.Contains(TEXT("\\\"quotes\\\"\\n\\nFourth")));
	TestFalse(TEXT("note.json has no \\r escape (no CR was saved)"), Json.Contains(TEXT("\\r")));
	const TSharedPtr<FJsonObject> Root = PlaytestQA::ParseStrict(*this, TEXT("multi-line"), Json);
	PlaytestQA::ExpectString(*this, TEXT("multi-line"), Root, TEXT("text"), Saved);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAMaxLengthTest, "Project.Playtest.QA.Text.MaxLengthBoundary", PLAYTEST_QA_FLAGS)

bool FPlaytestQAMaxLengthTest::RunTest(const FString& Parameters)
{
	// the note box caps input at 2000 characters; nothing after the box may shorten or alter a note around that size
	PlaytestQA::FScopedTempDir Temp;
	for (const int32 Length : { 1999, 2000, 2001 })
	{
		const FString Text = PlaytestQA::MakeMultiLineText(Length);
		TestEqual(FString::Printf(TEXT("%d chars: generated length"), Length), Text.Len(), Length);
		const FString Saved = FPlaytestFeedbackRules::NormalizeNoteText(Text);
		TestTrue(FString::Printf(TEXT("%d chars: an already-normalized note is unchanged (%s)"), Length, *PlaytestQA::DescribeDifference(Saved, Text)), Saved.Equals(Text, ESearchCase::CaseSensitive));
		PlaytestQA::ExpectTextRoundTrip(*this, FString::Printf(TEXT("%d-char multi-line note"), Length), Temp.Path, Saved);
	}

	// a full 2000-character box as Slate returns it (CRLF): each CRLF becomes one "\n", nothing else changes
	const FString Lf = PlaytestQA::MakeMultiLineText(2000);
	const FString Crlf = Lf.Replace(TEXT("\n"), TEXT("\r\n"));
	int32 LineBreaks = 0;
	for (const TCHAR C : Lf)
	{
		LineBreaks += (C == TEXT('\n')) ? 1 : 0;
	}
	TestEqual(TEXT("CRLF version is longer by one char per line break"), Crlf.Len(), 2000 + LineBreaks);
	TestTrue(TEXT("The CRLF box text normalizes to the LF text exactly"), FPlaytestFeedbackRules::NormalizeNoteText(Crlf).Equals(Lf, ESearchCase::CaseSensitive));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// F8 decision rules (FPlaytestFeedbackRules::DecideFeedbackKey): captured vs passed to the editor/game.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQARulesOpenTest, "Project.Playtest.QA.Rules.F8OpensNoteInPlay", PLAYTEST_QA_FLAGS)

bool FPlaytestQARulesOpenTest::RunTest(const FString& Parameters)
{
	PlaytestQA::ExpectKeyAction(*this, TEXT("F8 during play"), PlaytestQA::PlayingF8(), EPlaytestFeedbackKeyAction::OpenNote);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQARulesEjectedTest, "Project.Playtest.QA.Rules.F8GoesToEditorWhenEjected", PLAYTEST_QA_FLAGS)

bool FPlaytestQARulesEjectedTest::RunTest(const FString& Parameters)
{
	// ejected (simulate): F8 is the editor's "Possess" again, so it must pass through, first press or held
	FPlaytestFeedbackKeyContext Context = PlaytestQA::PlayingF8();
	Context.bEjected = true;
	PlaytestQA::ExpectKeyAction(*this, TEXT("F8 while ejected"), Context, EPlaytestFeedbackKeyAction::PassThrough);
	Context.bIsRepeat = true;
	PlaytestQA::ExpectKeyAction(*this, TEXT("Held F8 while ejected"), Context, EPlaytestFeedbackKeyAction::PassThrough);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQARulesTypingTest, "Project.Playtest.QA.Rules.F8PassesThroughWhileTyping", PLAYTEST_QA_FLAGS)

bool FPlaytestQARulesTypingTest::RunTest(const FString& Parameters)
{
	// focus in a text field (editor field, editor console, game UI) or the game console open: F8 is left alone
	FPlaytestFeedbackKeyContext Context = PlaytestQA::PlayingF8();
	Context.bTypingInText = true;
	PlaytestQA::ExpectKeyAction(*this, TEXT("F8 while typing in a text field / console open"), Context, EPlaytestFeedbackKeyAction::PassThrough);
	Context.bIsRepeat = true;
	PlaytestQA::ExpectKeyAction(*this, TEXT("Held F8 while typing"), Context, EPlaytestFeedbackKeyAction::PassThrough);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQARulesNotRunningTest, "Project.Playtest.QA.Rules.F8PassesThroughWhenNotPlaying", PLAYTEST_QA_FLAGS)

bool FPlaytestQARulesNotRunningTest::RunTest(const FString& Parameters)
{
	// no running session (editor idle, loading, tearing down): F8 is not ours
	FPlaytestFeedbackKeyContext Context = PlaytestQA::PlayingF8();
	Context.bSessionRunning = false;
	PlaytestQA::ExpectKeyAction(*this, TEXT("F8 with no running session"), Context, EPlaytestFeedbackKeyAction::PassThrough);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQARulesRepeatTest, "Project.Playtest.QA.Rules.HeldF8IsSwallowed", PLAYTEST_QA_FLAGS)

bool FPlaytestQARulesRepeatTest::RunTest(const FString& Parameters)
{
	// holding F8 during play: repeats neither open a second note nor leak to the editor's eject
	FPlaytestFeedbackKeyContext Context = PlaytestQA::PlayingF8();
	Context.bIsRepeat = true;
	PlaytestQA::ExpectKeyAction(*this, TEXT("Held (repeating) F8 during play"), Context, EPlaytestFeedbackKeyAction::Consume);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQARulesTableTest, "Project.Playtest.QA.Rules.FeedbackKeyAllCombinations", PLAYTEST_QA_FLAGS)

bool FPlaytestQARulesTableTest::RunTest(const FString& Parameters)
{
	// all 32 combinations against the documented rule:
	// captured = feedback key AND running AND NOT ejected AND NOT typing; captured repeat -> Consume, captured press -> OpenNote
	for (int32 Bits = 0; Bits < 32; ++Bits)
	{
		FPlaytestFeedbackKeyContext Context;
		Context.bIsFeedbackKey = (Bits & 1) != 0;
		Context.bIsRepeat = (Bits & 2) != 0;
		Context.bSessionRunning = (Bits & 4) != 0;
		Context.bEjected = (Bits & 8) != 0;
		Context.bTypingInText = (Bits & 16) != 0;
		const bool bCaptured = Context.bIsFeedbackKey && Context.bSessionRunning && !Context.bEjected && !Context.bTypingInText;
		const EPlaytestFeedbackKeyAction Expected = !bCaptured ? EPlaytestFeedbackKeyAction::PassThrough
			: (Context.bIsRepeat ? EPlaytestFeedbackKeyAction::Consume : EPlaytestFeedbackKeyAction::OpenNote);
		PlaytestQA::ExpectKeyAction(*this, FString::Printf(TEXT("key=%d repeat=%d running=%d ejected=%d typing=%d"),
			Context.bIsFeedbackKey ? 1 : 0, Context.bIsRepeat ? 1 : 0, Context.bSessionRunning ? 1 : 0, Context.bEjected ? 1 : 0, Context.bTypingInText ? 1 : 0), Context, Expected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQARulesNoteBoxTest, "Project.Playtest.QA.Rules.NoteBoxKeys", PLAYTEST_QA_FLAGS)

bool FPlaytestQARulesNoteBoxTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		FKey Key;
		bool bShift;
		FKey FeedbackKey;
		EPlaytestNoteBoxKeyAction Expected;
		const TCHAR* Why;
	};
	const FCase Cases[] = {
		{ EKeys::Enter, false, EKeys::F8, EPlaytestNoteBoxKeyAction::Submit, TEXT("Enter saves") },
		{ EKeys::Enter, true, EKeys::F8, EPlaytestNoteBoxKeyAction::PassThrough, TEXT("Shift+Enter is a new line (goes to the text box)") },
		{ EKeys::Escape, false, EKeys::F8, EPlaytestNoteBoxKeyAction::Cancel, TEXT("Esc cancels") },
		{ EKeys::Escape, true, EKeys::F8, EPlaytestNoteBoxKeyAction::Cancel, TEXT("Shift+Esc also cancels") },
		{ EKeys::F8, false, EKeys::F8, EPlaytestNoteBoxKeyAction::Consume, TEXT("F8 again is swallowed") },
		{ EKeys::F8, true, EKeys::F8, EPlaytestNoteBoxKeyAction::Consume, TEXT("Shift+F8 is swallowed") },
		{ EKeys::A, false, EKeys::F8, EPlaytestNoteBoxKeyAction::PassThrough, TEXT("letters are typed") },
		{ EKeys::BackSpace, false, EKeys::F8, EPlaytestNoteBoxKeyAction::PassThrough, TEXT("Backspace edits") },
		{ EKeys::Left, true, EKeys::F8, EPlaytestNoteBoxKeyAction::PassThrough, TEXT("Shift+arrow selects") },
		{ EKeys::F9, false, EKeys::F9, EPlaytestNoteBoxKeyAction::Consume, TEXT("a rebound feedback key (F9) is the one swallowed") },
		{ EKeys::F8, false, EKeys::F9, EPlaytestNoteBoxKeyAction::PassThrough, TEXT("with F9 as the feedback key, F8 is an ordinary key") },
	};
	for (const FCase& Case : Cases)
	{
		const EPlaytestNoteBoxKeyAction Actual = FPlaytestFeedbackRules::DecideNoteBoxKey(Case.Key, Case.bShift, Case.FeedbackKey);
		TestTrue(FString::Printf(TEXT("%s: expected %s, got %s"), Case.Why, PlaytestQA::BoxActionName(Case.Expected), PlaytestQA::BoxActionName(Actual)), Actual == Case.Expected);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Which game instance handles F8 (FPlaytestFeedbackRules::ChooseHandler)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAHandlerOrderTest, "Project.Playtest.QA.Rules.HandlerOrder", PLAYTEST_QA_FLAGS)

bool FPlaytestQAHandlerOrderTest::RunTest(const FString& Parameters)
{
	using PlaytestQA::Candidate;
	// Candidate(running, PIE, lastFocus, focused, primary, active)
	PlaytestQA::ExpectHandler(*this, TEXT("No instances"), {}, INDEX_NONE);
	PlaytestQA::ExpectHandler(*this, TEXT("Nothing running (even if focused, primary, recently focused)"), { Candidate(false, 0, 9, true, true) }, INDEX_NONE);
	PlaytestQA::ExpectHandler(*this, TEXT("One running standalone game"), { Candidate(true, INDEX_NONE) }, 0);

	// 1. an instance with its note box open/capturing wins over focus, recency and primary
	PlaytestQA::ExpectHandler(*this, TEXT("1: active note box beats a focused primary instance"),
		{ Candidate(true, 0, 9, true, true), Candidate(true, 1, 0, false, false, true) }, 1);

	// 2. focused running viewport
	PlaytestQA::ExpectHandler(*this, TEXT("2: the focused viewport beats a more recently focused one"),
		{ Candidate(true, 0, 9), Candidate(true, 1, 3, true) }, 1);
	PlaytestQA::ExpectHandler(*this, TEXT("2: a focused viewport that is not running is skipped"),
		{ Candidate(false, 0, 9, true), Candidate(true, 1, 2) }, 1);

	// 3. most recently focused running viewport
	PlaytestQA::ExpectHandler(*this, TEXT("3: most recently focused wins"),
		{ Candidate(true, 0, 3), Candidate(true, 1, 7), Candidate(true, 2, 0) }, 1);
	PlaytestQA::ExpectHandler(*this, TEXT("3: recent focus beats primary"),
		{ Candidate(true, 0, 0, false, true), Candidate(true, 1, 4) }, 1);
	PlaytestQA::ExpectHandler(*this, TEXT("3: a recently focused instance that is not running is skipped"),
		{ Candidate(true, 0, 2), Candidate(false, 1, 9) }, 0);

	// 4. primary PIE instance (no focus history)
	PlaytestQA::ExpectHandler(*this, TEXT("4: primary beats a lower PIE number"),
		{ Candidate(true, 0), Candidate(true, 1, 0, false, true) }, 1);

	// 5. lowest PIE instance, standalone counts as lowest, then list order
	PlaytestQA::ExpectHandler(*this, TEXT("5: lowest PIE instance"),
		{ Candidate(true, 2), Candidate(true, 1), Candidate(true, 3) }, 1);
	PlaytestQA::ExpectHandler(*this, TEXT("5: standalone (INDEX_NONE) counts as lowest"),
		{ Candidate(true, 0), Candidate(true, INDEX_NONE) }, 1);
	PlaytestQA::ExpectHandler(*this, TEXT("5: a tie goes to the first in the list"),
		{ Candidate(true, 1), Candidate(true, 1) }, 0);
	PlaytestQA::ExpectHandler(*this, TEXT("5: instances that are not running are skipped"),
		{ Candidate(false, 0), Candidate(true, 2) }, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAHandlerPermutationTest, "Project.Playtest.QA.Rules.HandlerIndependentOfListOrder", PLAYTEST_QA_FLAGS)

bool FPlaytestQAHandlerPermutationTest::RunTest(const FString& Parameters)
{
	// the same set of instances must pick the same instance in whatever order the engine lists them
	using PlaytestQA::Candidate;
	struct FScenario
	{
		const TCHAR* Name;
		TArray<FPlaytestFeedbackCandidate> Candidates; // PIEInstance values are unique and identify the expected pick
		int32 ExpectedPIE;
	};
	const FScenario Scenarios[] = {
		{ TEXT("rule 1 (active)"), { Candidate(true, 0, 5, true, true), Candidate(true, 1, 9), Candidate(true, 2, 0, false, false, true), Candidate(true, 3) }, 2 },
		{ TEXT("rule 2 (focused)"), { Candidate(true, 0, 5, false, true), Candidate(true, 1, 9), Candidate(true, 2, 1, true), Candidate(false, 3, 12, true) }, 2 },
		{ TEXT("rule 3 (recent)"), { Candidate(true, 2, 3), Candidate(true, 1, 7), Candidate(false, 0, 9, true, true), Candidate(true, 3) }, 1 },
		{ TEXT("rule 4 (primary)"), { Candidate(true, 0), Candidate(true, 2, 0, false, true), Candidate(true, 1), Candidate(true, 3) }, 2 },
		{ TEXT("rule 5 (lowest)"), { Candidate(true, 3), Candidate(true, 1), Candidate(false, 0), Candidate(true, 2) }, 1 },
	};
	for (const FScenario& Scenario : Scenarios)
	{
		int32 Mismatches = 0;
		int32 Checked = 0;
		// all 24 orders of the 4 candidates
		for (int32 A = 0; A < 4; ++A)
		for (int32 B = 0; B < 4; ++B)
		for (int32 C = 0; C < 4; ++C)
		for (int32 D = 0; D < 4; ++D)
		{
			if (A == B || A == C || A == D || B == C || B == D || C == D)
			{
				continue;
			}
			const TArray<FPlaytestFeedbackCandidate> Ordered = { Scenario.Candidates[A], Scenario.Candidates[B], Scenario.Candidates[C], Scenario.Candidates[D] };
			const int32 Chosen = FPlaytestFeedbackRules::ChooseHandler(Ordered);
			++Checked;
			if (!Ordered.IsValidIndex(Chosen) || Ordered[Chosen].PIEInstance != Scenario.ExpectedPIE)
			{
				++Mismatches;
			}
		}
		TestEqual(FString::Printf(TEXT("%s: orders checked"), Scenario.Name), Checked, 24);
		TestEqual(FString::Printf(TEXT("%s: orders that picked a different instance than PIE %d"), Scenario.Name, Scenario.ExpectedPIE), Mismatches, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATextWidgetsTest, "Project.Playtest.QA.Rules.TextEntryWidgets", PLAYTEST_QA_FLAGS)

bool FPlaytestQATextWidgetsTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Type : { TEXT("SEditableText"), TEXT("SMultiLineEditableText"), TEXT("SEditableTextBox"), TEXT("SMultiLineEditableTextBox") })
	{
		TestTrue(FString::Printf(TEXT("%s receives typed text (F8 must not be stolen)"), Type), FPlaytestFeedbackRules::IsTextEntryWidgetType(FName(Type)));
	}
	for (const TCHAR* Type : { TEXT("SViewport"), TEXT("SGameLayerManager"), TEXT("SButton"), TEXT("SCheckBox"), TEXT("SWindow"), TEXT("STextBlock") })
	{
		TestFalse(FString::Printf(TEXT("%s is not a text field"), Type), FPlaytestFeedbackRules::IsTextEntryWidgetType(FName(Type)));
	}
	TestFalse(TEXT("No widget (NAME_None) is not a text field"), FPlaytestFeedbackRules::IsTextEntryWidgetType(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQATeardownTest, "Project.Playtest.QA.Rules.TeardownSave", PLAYTEST_QA_FLAGS)

bool FPlaytestQATeardownTest::RunTest(const FString& Parameters)
{
	// the game ends with the box open: keep what was typed, drop an empty box
	TestFalse(TEXT("Box not open: nothing to save"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(false, TEXT("typed text")));
	TestFalse(TEXT("Open but empty: dropped"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(true, FString()));
	TestFalse(TEXT("Open with only spaces/line breaks: empty once normalized, dropped"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(true, TEXT(" \r\n\t ")));
	TestTrue(TEXT("Open with one character: saved"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(true, TEXT("x")));
	TestTrue(TEXT("Open with multi-line text: saved"), FPlaytestFeedbackRules::ShouldSaveOnTeardown(true, TEXT("\r\n  the boat sank\r\nhere  \r\n")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Confirmation toast words (FPlaytestFeedbackRules::MakeConfirmation)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAConfirmationTest, "Project.Playtest.QA.Rules.Confirmation", PLAYTEST_QA_FLAGS)

bool FPlaytestQAConfirmationTest::RunTest(const FString& Parameters)
{
	const FPlaytestConfirmation Saved = FPlaytestFeedbackRules::MakeConfirmation(true, TEXT("the dock is slippery"), true, TEXT("20260922-210509"), FString(), FString());
	TestEqual(TEXT("Saved note: title"), Saved.Title, FString(TEXT("Note saved. Thanks!")));
	TestTrue(FString::Printf(TEXT("Saved note: the folder name is shown (detail '%s')"), *Saved.Detail), Saved.Detail.Contains(TEXT("20260922-210509")));
	TestFalse(TEXT("Saved note: saved style"), Saved.bFailure);

	const FPlaytestConfirmation Bookmark = FPlaytestFeedbackRules::MakeConfirmation(true, FString(), true, TEXT("20260922-210510"), FString(), FString());
	TestEqual(TEXT("Empty text with a screenshot: title"), Bookmark.Title, FString(TEXT("Screenshot saved (no text)")));
	TestTrue(FString::Printf(TEXT("Empty text: the folder name is shown (detail '%s')"), *Bookmark.Detail), Bookmark.Detail.Contains(TEXT("20260922-210510")));
	TestFalse(TEXT("Empty text: saved style"), Bookmark.bFailure);

	const FPlaytestConfirmation Warned = FPlaytestFeedbackRules::MakeConfirmation(true, TEXT("text"), false, TEXT("20260922-210511"), TEXT("screenshot dropped"), FString());
	TestTrue(FString::Printf(TEXT("Saved with a warning: still reported as saved (title '%s')"), *Warned.Title), Warned.Title.StartsWith(TEXT("Note saved")));
	TestFalse(TEXT("Saved with a warning: saved style"), Warned.bFailure);
	AddInfo(FString::Printf(TEXT("Saved with a warning shows: '%s' / '%s'"), *Warned.Title, *Warned.Detail));

	const FPlaytestConfirmation Failed = FPlaytestFeedbackRules::MakeConfirmation(false, TEXT("some text"), false, FString(), FString(), TEXT("disk full"));
	TestEqual(TEXT("Failed: title names the reason and where the text went"), Failed.Title, FString(TEXT("Note NOT saved: disk full. Your text is in the log.")));
	TestTrue(TEXT("Failed: failure style"), Failed.bFailure);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAConfirmationTruthTest, "Project.Playtest.QA.Rules.ConfirmationNeverClaimsMissingScreenshot", PLAYTEST_QA_FLAGS)

bool FPlaytestQAConfirmationTruthTest::RunTest(const FString& Parameters)
{
	// screenshot timed out (no screenshot.png) and Enter on an empty box: the toast must not say a screenshot was saved
	const FPlaytestConfirmation Confirmation = FPlaytestFeedbackRules::MakeConfirmation(true, FString(), /*bHasScreenshot*/ false, TEXT("20260922-210512"), TEXT("screenshot timed out"), FString());
	TestFalse(FString::Printf(TEXT("Toast without a screenshot does not claim one (title '%s')"), *Confirmation.Title), Confirmation.Title.Contains(TEXT("Screenshot saved")));
	TestFalse(TEXT("It is still a saved note (note.json with the context), not a failure"), Confirmation.bFailure);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Screenshot contract since B2: an invalid screenshot is dropped with a warning, the note is still saved.
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAScreenshotDroppedTest, "Project.Playtest.QA.Screenshot.InvalidPixelsDroppedWithWarning", PLAYTEST_QA_FLAGS)

bool FPlaytestQAScreenshotDroppedTest::RunTest(const FString& Parameters)
{
	PlaytestQA::FScopedTempDir Temp;
	struct FCase
	{
		const TCHAR* Name;
		int32 Width;
		int32 Height;
		int32 NumPixels;
		bool bValid;
	};
	const FCase Cases[] = {
		{ TEXT("Valid2x2"), 2, 2, 4, true },
		{ TEXT("TooFewPixels"), 4, 2, 5, false },
		{ TEXT("ZeroSizeWithPixels"), 0, 0, 8, false },
		{ TEXT("NegativeSize"), -4, -2, 8, false },
	};
	for (const FCase& Case : Cases)
	{
		const FString Root = Temp.Path / Case.Name;
		TArray<FColor> Pixels;
		Pixels.Init(FColor::Blue, Case.NumPixels);
		const FPlaytestNoteData Note = PlaytestQA::MakeNote(FString::Printf(TEXT("screenshot case %s"), Case.Name));
		FString Folder, Error, Warning;
		const bool bWritten = FPlaytestNoteWriter::WriteNote(Root, Note, Case.Width, Case.Height, Pixels, Folder, Error, &Warning);
		if (!TestTrue(FString::Printf(TEXT("%s: the note is saved (error: '%s')"), Case.Name, *Error), bWritten))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: warning only when the screenshot is dropped (warning '%s')"), Case.Name, *Warning), Warning.IsEmpty() ? 0 : 1, Case.bValid ? 0 : 1);
		TestEqual(FString::Printf(TEXT("%s: screenshot.png exists only for valid pixels"), Case.Name), FPaths::FileExists(Folder / TEXT("screenshot.png")) ? 1 : 0, Case.bValid ? 1 : 0);
		FString Json;
		FFileHelper::LoadFileToString(Json, *(Folder / TEXT("note.json")));
		const TSharedPtr<FJsonObject> Root2 = PlaytestQA::ParseStrict(*this, Case.Name, Json);
		PlaytestQA::ExpectString(*this, Case.Name, Root2, TEXT("text"), Note.Text);
		PlaytestQA::ExpectString(*this, Case.Name, Root2, TEXT("screenshot"), Case.bValid ? FString(TEXT("screenshot.png")) : FString());
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Commit id: more worktree layouts (B5) and the IsCommitHash rule (B4)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitWorktreeRelativeTest, "Project.Playtest.QA.Git.WorktreeRelativeGitdirAndPackedCommon", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitWorktreeRelativeTest::RunTest(const FString& Parameters)
{
	// .git file with a RELATIVE gitdir, commondir with an ABSOLUTE path, the branch only in the common packed-refs
	PlaytestQA::FScopedTempDir Temp;
	const FString MainGitDir = Temp.Path / TEXT("Main/.git");
	const FString Packed = FString::ChrN(40, TEXT('c'));
	const FString Other = FString::ChrN(40, TEXT('d'));
	PlaytestQA::WriteAscii(MainGitDir / TEXT("packed-refs"), TEXT("# pack-refs with: peeled fully-peeled sorted\n") + Other + TEXT(" refs/heads/main\n") + Packed + TEXT(" refs/heads/lane/qa1\n"));
	const FString WorktreeGitDir = MainGitDir / TEXT("worktrees/qa1");
	PlaytestQA::WriteAscii(WorktreeGitDir / TEXT("HEAD"), TEXT("ref: refs/heads/lane/qa1\n"));
	PlaytestQA::WriteAscii(WorktreeGitDir / TEXT("commondir"), MainGitDir + TEXT("\n"));
	const FString Repo = Temp.Path / TEXT("Lanes/qa1");
	PlaytestQA::WriteAscii(Repo / TEXT(".git"), TEXT("gitdir: ../../Main/.git/worktrees/qa1\n"));
	TestEqual(TEXT("Relative gitdir + absolute commondir + packed branch ref"), FPlaytestNoteWriter::ReadGitCommit(Repo), Packed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitWorktreeLocalRefsTest, "Project.Playtest.QA.Git.WorktreePerWorktreeRefsStayLocal", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitWorktreeLocalRefsTest::RunTest(const FString& Parameters)
{
	// documented like git: refs/bisect/ (and refs/worktree/, refs/rewritten/) belong to the worktree, not the common dir
	PlaytestQA::FScopedTempDir Temp;
	const FString MainGitDir = Temp.Path / TEXT("Main/.git");
	const FString WorktreeGitDir = MainGitDir / TEXT("worktrees/wt");
	const FString Local = FString::ChrN(40, TEXT('a'));
	const FString Shared = FString::ChrN(40, TEXT('b'));
	PlaytestQA::WriteAscii(MainGitDir / TEXT("refs/bisect/bad"), Shared + TEXT("\n"));
	PlaytestQA::WriteAscii(WorktreeGitDir / TEXT("refs/bisect/bad"), Local + TEXT("\n"));
	PlaytestQA::WriteAscii(WorktreeGitDir / TEXT("HEAD"), TEXT("ref: refs/bisect/bad\n"));
	PlaytestQA::WriteAscii(WorktreeGitDir / TEXT("commondir"), TEXT("../..\n"));
	const FString Repo = Temp.Path / TEXT("Wt");
	PlaytestQA::WriteAscii(Repo / TEXT(".git"), FString::Printf(TEXT("gitdir: %s\n"), *WorktreeGitDir));
	TestEqual(TEXT("refs/bisect/bad is read from the worktree's own git dir"), FPlaytestNoteWriter::ReadGitCommit(Repo), Local);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitWorktreeBrokenTest, "Project.Playtest.QA.Git.WorktreeBrokenIsUnknown", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitWorktreeBrokenTest::RunTest(const FString& Parameters)
{
	// a lane deleted or moved behind git's back: never a crash, never text that is not a commit
	PlaytestQA::FScopedTempDir Temp;
	struct FCase
	{
		const TCHAR* Name;
		FString GitFile;
	};
	const FString Missing = Temp.Path / TEXT("does/not/exist/.git/worktrees/gone");
	const FCase Cases[] = {
		{ TEXT("GitdirMissing"), FString::Printf(TEXT("gitdir: %s\n"), *Missing) },
		{ TEXT("NoGitdirPrefix"), TEXT("hello\n") },
		{ TEXT("EmptyGitFile"), FString() },
		{ TEXT("GitdirWithoutPath"), TEXT("gitdir: \n") },
	};
	for (const FCase& Case : Cases)
	{
		const FString Repo = Temp.Path / Case.Name;
		PlaytestQA::WriteAscii(Repo / TEXT(".git"), Case.GitFile);
		TestEqual(FString::Printf(TEXT("%s: 'unknown'"), Case.Name), FPlaytestNoteWriter::ReadGitCommit(Repo), FString(TEXT("unknown")));
	}

	// commondir points nowhere: the branch cannot be resolved
	const FString WorktreeGitDir = Temp.Path / TEXT("Main/.git/worktrees/wt");
	PlaytestQA::WriteAscii(WorktreeGitDir / TEXT("HEAD"), TEXT("ref: refs/heads/lane/x\n"));
	PlaytestQA::WriteAscii(WorktreeGitDir / TEXT("commondir"), Missing + TEXT("\n"));
	const FString Repo = Temp.Path / TEXT("CommondirMissing");
	PlaytestQA::WriteAscii(Repo / TEXT(".git"), FString::Printf(TEXT("gitdir: %s\n"), *WorktreeGitDir));
	TestEqual(TEXT("CommondirMissing: 'unknown'"), FPlaytestNoteWriter::ReadGitCommit(Repo), FString(TEXT("unknown")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAGitIsCommitHashTest, "Project.Playtest.QA.Git.IsCommitHashBoundaries", PLAYTEST_QA_FLAGS)

bool FPlaytestQAGitIsCommitHashTest::RunTest(const FString& Parameters)
{
	const FString Mixed40 = TEXT("0123456789abcdef0123456789abcdef01234567");
	TestTrue(TEXT("40 hex (SHA-1)"), FPlaytestNoteWriter::IsCommitHash(Mixed40));
	TestTrue(TEXT("64 hex (SHA-256)"), FPlaytestNoteWriter::IsCommitHash(FString::ChrN(64, TEXT('f'))));
	struct FCase
	{
		FString Value;
		const TCHAR* Why;
	};
	const FCase Rejected[] = {
		{ FString(), TEXT("empty") },
		{ FString::ChrN(39, TEXT('a')), TEXT("39 chars") },
		{ FString::ChrN(41, TEXT('a')), TEXT("41 chars") },
		{ FString::ChrN(63, TEXT('a')), TEXT("63 chars") },
		{ FString::ChrN(65, TEXT('a')), TEXT("65 chars") },
		{ FString::ChrN(39, TEXT('a')) + TEXT("g"), TEXT("40 chars with a non-hex 'g'") },
		{ Mixed40 + TEXT("\n"), TEXT("trailing line break") },
		{ TEXT(" ") + FString::ChrN(39, TEXT('a')), TEXT("leading space within 40 chars") },
		{ TEXT("unknown"), TEXT("the 'unknown' marker") },
	};
	for (const FCase& Case : Rejected)
	{
		TestFalse(FString::Printf(TEXT("Not a commit id: %s"), Case.Why), FPlaytestNoteWriter::IsCommitHash(Case.Value));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestQAConfigToastTest, "Project.Playtest.QA.Config.ToastDurations", PLAYTEST_QA_FLAGS)

bool FPlaytestQAConfigToastTest::RunTest(const FString& Parameters)
{
	// spec: the toast shows for ~7 s; a failure is shown longer
	const UPlaytestFeedbackSubsystem* Defaults = GetDefault<UPlaytestFeedbackSubsystem>();
	if (!TestNotNull(TEXT("Subsystem class defaults exist"), Defaults))
	{
		return true;
	}
	TestTrue(FString::Printf(TEXT("Saved toast is about 7 s (5..10, got %.2f)"), Defaults->ToastSeconds), Defaults->ToastSeconds >= 5.0f && Defaults->ToastSeconds <= 10.0f);
	TestTrue(FString::Printf(TEXT("Failure toast stays longer than the saved toast (%.2f > %.2f)"), Defaults->FailureToastSeconds, Defaults->ToastSeconds), Defaults->FailureToastSeconds > Defaults->ToastSeconds);
	return true;
}

#undef PLAYTEST_QA_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS && VIBEGAME_WITH_PLAYTEST_FEEDBACK

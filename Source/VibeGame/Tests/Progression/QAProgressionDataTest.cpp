// Lure T-010 independent QA tests (qa-engineer): data validation of the shipped progression tables
// (data/tables/DT_PlayerLevel.csv, DT_Cooler.csv, DT_FishMarket.csv) with QA's own rules from docs/specs/progression-rules.md,
// independent of FLureProgressionData's validators. Project.Progression.QA.Data.*

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/Pawn.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Progression/LureProgressionTypes.h"
#include "Progression/LureSellPoint.h"

namespace QAProgressionData
{
	/** One CSV source parsed raw: header + rows of cells */
	struct FSheet
	{
		FString File;
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;

		int32 Col(const TCHAR* Name) const { return Header.IndexOfByKey(FString(Name)); }
		const FString& Cell(int32 Row, int32 Col) const { return Rows[Row][Col]; }
	};

	bool Load(FAutomationTestBase& Test, const TCHAR* File, FSheet& Out)
	{
		FString Text;
		if (!QAP::ReadSource(Test, File, Text))
		{
			return false;
		}
		TArray<TArray<FString>> All = QAP::ParseCsv(Text);
		if (!Test.TestTrue(FString::Printf(TEXT("%s has a header and at least one row"), File), All.Num() >= 2))
		{
			return false;
		}
		Out.File = File;
		Out.Header = All[0];
		for (FString& Column : Out.Header)
		{
			Column.TrimStartAndEndInline();
		}
		All.RemoveAt(0);
		Out.Rows = MoveTemp(All);
		bool bShapeOk = true;
		for (int32 Row = 0; Row < Out.Rows.Num(); ++Row)
		{
			bShapeOk &= Test.TestEqual(FString::Printf(TEXT("%s row %d has as many cells as the header"), File, Row + 1), Out.Rows[Row].Num(), Out.Header.Num());
		}
		return bShapeOk;
	}

	struct FLevelRow
	{
		FString Name;
		int32 Level = 0;
		int32 XpToNext = 0;
	};

	/** Levels sorted by Level; false (with errors) if a cell is not a whole number */
	bool LoadLevels(FAutomationTestBase& Test, TArray<FLevelRow>& Out)
	{
		FSheet Sheet;
		if (!Load(Test, TEXT("DT_PlayerLevel.csv"), Sheet))
		{
			return false;
		}
		const int32 LevelCol = Sheet.Col(TEXT("Level"));
		const int32 XpCol = Sheet.Col(TEXT("XpToNext"));
		if (!Test.TestTrue(TEXT("DT_PlayerLevel has Level and XpToNext columns"), LevelCol != INDEX_NONE && XpCol != INDEX_NONE))
		{
			return false;
		}
		for (int32 Row = 0; Row < Sheet.Rows.Num(); ++Row)
		{
			if (!QAP::IsWholeNumber(Sheet.Cell(Row, LevelCol)) || !QAP::IsWholeNumber(Sheet.Cell(Row, XpCol)))
			{
				Test.AddError(FString::Printf(TEXT("DT_PlayerLevel row %s: Level/XpToNext must be whole numbers"), *Sheet.Cell(Row, 0)));
				return false;
			}
			FLevelRow Level;
			Level.Name = Sheet.Cell(Row, 0);
			Level.Level = FCString::Atoi(*Sheet.Cell(Row, LevelCol));
			Level.XpToNext = FCString::Atoi(*Sheet.Cell(Row, XpCol));
			Out.Add(Level);
		}
		Out.Sort([](const FLevelRow& A, const FLevelRow& B) { return A.Level < B.Level; });
		return true;
	}

	struct FSpec
	{
		const TCHAR* File;
		UScriptStruct* Row;
		TArray<const TCHAR*> Required;
		TArray<const TCHAR*> WholeNumberColumns;
		TArray<const TCHAR*> DecimalColumns;
	};

	TArray<FSpec> Specs()
	{
		return {
			{ TEXT("DT_PlayerLevel.csv"), FPlayerLevelRow::StaticStruct(), { TEXT("Level"), TEXT("XpToNext") }, { TEXT("Level"), TEXT("XpToNext") }, {} },
			{ TEXT("DT_Cooler.csv"), FCoolerRow::StaticStruct(), { TEXT("DisplayName"), TEXT("Slots") }, { TEXT("Slots") }, {} },
			{ TEXT("DT_FishMarket.csv"), FFishMarketRow::StaticStruct(), { TEXT("DisplayName"), TEXT("SellMultiplier") }, {}, { TEXT("SellMultiplier") } },
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataSourcesImportCleanly, "Project.Progression.QA.Data.SourcesImportCleanly", QAP::Flags)
bool FQAProgDataSourcesImportCleanly::RunTest(const FString& Parameters)
{
	for (const QAProgressionData::FSpec& Spec : QAProgressionData::Specs())
	{
		FString Csv;
		if (!QAP::ReadSource(*this, Spec.File, Csv))
		{
			continue;
		}
		TArray<FString> Problems;
		const TStrongObjectPtr<UDataTable> Table = QAP::MakeTable(Spec.Row, Csv, &Problems);
		TestEqual(FString::Printf(TEXT("%s imports with no problems (%s)"), Spec.File, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		TestTrue(FString::Printf(TEXT("%s has rows"), Spec.File), Table->GetRowMap().Num() > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataRawNoDuplicateRowNames, "Project.Progression.QA.Data.RawNoDuplicateOrEmptyRowNames", QAP::Flags)
bool FQAProgDataRawNoDuplicateRowNames::RunTest(const FString& Parameters)
{
	// The engine import keeps only one of two rows with the same name; a raw scan catches the silently lost row.
	for (const QAProgressionData::FSpec& Spec : QAProgressionData::Specs())
	{
		QAProgressionData::FSheet Sheet;
		if (!QAProgressionData::Load(*this, Spec.File, Sheet))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: the first column is Name"), Spec.File), Sheet.Header[0], FString(TEXT("Name")));
		TSet<FString> Seen;
		for (int32 Row = 0; Row < Sheet.Rows.Num(); ++Row)
		{
			const FString Name = Sheet.Cell(Row, 0).TrimStartAndEnd();
			TestFalse(FString::Printf(TEXT("%s row %d has a name"), Spec.File, Row + 1), Name.IsEmpty());
			bool bAlready = false;
			Seen.Add(Name.ToLower(), &bAlready); // FName row names are case-insensitive
			TestFalse(FString::Printf(TEXT("%s: row name '%s' appears once"), Spec.File, *Name), bAlready);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataRawColumnsMatchRowStructs, "Project.Progression.QA.Data.RawColumnsMatchRowStructs", QAP::Flags)
bool FQAProgDataRawColumnsMatchRowStructs::RunTest(const FString& Parameters)
{
	for (const QAProgressionData::FSpec& Spec : QAProgressionData::Specs())
	{
		QAProgressionData::FSheet Sheet;
		if (!QAProgressionData::Load(*this, Spec.File, Sheet))
		{
			continue;
		}
		for (int32 Col = 1; Col < Sheet.Header.Num(); ++Col)
		{
			TestNotNull(FString::Printf(TEXT("%s: column '%s' is a property of %s"), Spec.File, *Sheet.Header[Col], *Spec.Row->GetName()),
				Spec.Row->FindPropertyByName(FName(*Sheet.Header[Col])));
		}
		for (const TCHAR* Required : Spec.Required)
		{
			TestTrue(FString::Printf(TEXT("%s has the column %s"), Spec.File, Required), Sheet.Col(Required) != INDEX_NONE);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataRawNumericCells, "Project.Progression.QA.Data.RawNumericCellsAreNumbers", QAP::Flags)
bool FQAProgDataRawNumericCells::RunTest(const FString& Parameters)
{
	// The engine CSV import reads text in a number cell as 0 without a problem (T004-Q2); scan the raw text.
	for (const QAProgressionData::FSpec& Spec : QAProgressionData::Specs())
	{
		QAProgressionData::FSheet Sheet;
		if (!QAProgressionData::Load(*this, Spec.File, Sheet))
		{
			continue;
		}
		for (const TCHAR* Column : Spec.WholeNumberColumns)
		{
			const int32 Col = Sheet.Col(Column);
			for (int32 Row = 0; Col != INDEX_NONE && Row < Sheet.Rows.Num(); ++Row)
			{
				TestTrue(FString::Printf(TEXT("%s %s.%s '%s' is a whole number"), Spec.File, *Sheet.Cell(Row, 0), Column, *Sheet.Cell(Row, Col)),
					QAP::IsWholeNumber(Sheet.Cell(Row, Col)));
			}
		}
		for (const TCHAR* Column : Spec.DecimalColumns)
		{
			const int32 Col = Sheet.Col(Column);
			for (int32 Row = 0; Col != INDEX_NONE && Row < Sheet.Rows.Num(); ++Row)
			{
				TestTrue(FString::Printf(TEXT("%s %s.%s '%s' is a number"), Spec.File, *Sheet.Cell(Row, 0), Column, *Sheet.Cell(Row, Col)),
					QAP::IsDecimalNumber(Sheet.Cell(Row, Col)));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataLevelsContiguous, "Project.Progression.QA.Data.PlayerLevel.LevelsContiguousFromOne", QAP::Flags)
bool FQAProgDataLevelsContiguous::RunTest(const FString& Parameters)
{
	TArray<QAProgressionData::FLevelRow> Levels;
	if (!QAProgressionData::LoadLevels(*this, Levels))
	{
		return false;
	}
	TestTrue(TEXT("more than one level"), Levels.Num() > 1);
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("sorted position %d holds level %d (row %s)"), Index, Index + 1, *Levels[Index].Name), Levels[Index].Level, Index + 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataXpToNextRule, "Project.Progression.QA.Data.PlayerLevel.XpToNextPositiveBelowCapZeroAtCap", QAP::Flags)
bool FQAProgDataXpToNextRule::RunTest(const FString& Parameters)
{
	TArray<QAProgressionData::FLevelRow> Levels;
	if (!QAProgressionData::LoadLevels(*this, Levels) || Levels.Num() == 0)
	{
		return false;
	}
	for (int32 Index = 0; Index + 1 < Levels.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("level %d XpToNext %d > 0"), Levels[Index].Level, Levels[Index].XpToNext), Levels[Index].XpToNext > 0);
	}
	TestEqual(TEXT("the last level (the cap) has XpToNext 0"), Levels.Last().XpToNext, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataCurveNeverEasier, "Project.Progression.QA.Data.PlayerLevel.CurveNeverGetsEasierAndFits", QAP::Flags)
bool FQAProgDataCurveNeverEasier::RunTest(const FString& Parameters)
{
	TArray<QAProgressionData::FLevelRow> Levels;
	if (!QAProgressionData::LoadLevels(*this, Levels) || Levels.Num() < 2)
	{
		return false;
	}
	int64 Total = 0;
	for (int32 Index = 0; Index + 1 < Levels.Num(); ++Index)
	{
		Total += Levels[Index].XpToNext;
		if (Index > 0)
		{
			TestTrue(FString::Printf(TEXT("level %d needs at least as much as level %d (%d >= %d)"), Levels[Index].Level, Levels[Index - 1].Level,
				Levels[Index].XpToNext, Levels[Index - 1].XpToNext), Levels[Index].XpToNext >= Levels[Index - 1].XpToNext);
		}
	}
	// XP is an int32 that saturates: the cap must be far below it, or players would stall short of it.
	TestTrue(FString::Printf(TEXT("XP to reach the cap (%lld) is far below MAX_int32"), Total), Total > 0 && Total < MAX_int32 / 1000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataCoolerRows, "Project.Progression.QA.Data.Cooler.RowsSaneAndDefaultExists", QAP::Flags)
bool FQAProgDataCoolerRows::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!QAP::ReadSource(*this, TEXT("DT_Cooler.csv"), Csv))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Table = QAP::MakeTableChecked(*this, FCoolerRow::StaticStruct(), Csv, TEXT("DT_Cooler.csv"));
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FCoolerRow& Row = *reinterpret_cast<const FCoolerRow*>(Pair.Value);
		TestTrue(FString::Printf(TEXT("%s: Slots %d in 1..100"), *Pair.Key.ToString(), Row.Slots), Row.Slots >= 1 && Row.Slots <= 100);
		TestFalse(FString::Printf(TEXT("%s: has a DisplayName"), *Pair.Key.ToString()), Row.DisplayName.IsEmptyOrWhitespace());
	}
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	TestFalse(TEXT("settings DefaultCoolerId is set"), Settings->DefaultCoolerId.IsNone());
	TestNotNull(FString::Printf(TEXT("settings DefaultCoolerId '%s' is a DT_Cooler row"), *Settings->DefaultCoolerId.ToString()),
		Table->FindRow<FCoolerRow>(Settings->DefaultCoolerId, TEXT("QA"), false));
	TestTrue(TEXT("settings FallbackCoolerSlots >= 1"), Settings->FallbackCoolerSlots >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataMarketRows, "Project.Progression.QA.Data.Market.RowsSaneAndDefaultAndDockExist", QAP::Flags)
bool FQAProgDataMarketRows::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!QAP::ReadSource(*this, TEXT("DT_FishMarket.csv"), Csv))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Table = QAP::MakeTableChecked(*this, FFishMarketRow::StaticStruct(), Csv, TEXT("DT_FishMarket.csv"));
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FFishMarketRow& Row = *reinterpret_cast<const FFishMarketRow*>(Pair.Value);
		TestTrue(FString::Printf(TEXT("%s: SellMultiplier %g is finite, > 0 and <= 10"), *Pair.Key.ToString(), Row.SellMultiplier),
			FMath::IsFinite(Row.SellMultiplier) && Row.SellMultiplier > 0.0f && Row.SellMultiplier <= 10.0f);
		TestFalse(FString::Printf(TEXT("%s: has a DisplayName"), *Pair.Key.ToString()), Row.DisplayName.IsEmptyOrWhitespace());
	}
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	TestNotNull(FString::Printf(TEXT("settings DefaultMarketId '%s' is a DT_FishMarket row"), *Settings->DefaultMarketId.ToString()),
		Table->FindRow<FFishMarketRow>(Settings->DefaultMarketId, TEXT("QA"), false));
	TestNotNull(TEXT("the Palm Key dock market row exists (the L_PalmKey sell point uses MarketId PalmKeyDock)"),
		Table->FindRow<FFishMarketRow>(TEXT("PalmKeyDock"), TEXT("QA"), false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgDataNewRowsNeedNoCode, "Project.Progression.QA.Data.NewRowsNeedNoCode", QAP::Flags)
bool FQAProgDataNewRowsNeedNoCode::RunTest(const FString& Parameters)
{
	// A designer adds a level 11, a bigger cooler and a new buyer as rows only; the game uses them with no code change.
	FString LevelCsv;
	FString CoolerCsv;
	FString MarketCsv;
	if (!QAP::ReadSource(*this, TEXT("DT_PlayerLevel.csv"), LevelCsv) || !QAP::ReadSource(*this, TEXT("DT_Cooler.csv"), CoolerCsv)
		|| !QAP::ReadSource(*this, TEXT("DT_FishMarket.csv"), MarketCsv))
	{
		return false;
	}
	TArray<QAProgressionData::FLevelRow> Levels;
	if (!QAProgressionData::LoadLevels(*this, Levels) || Levels.Num() == 0)
	{
		return false;
	}
	const QAProgressionData::FLevelRow Cap = Levels.Last();
	const FString OldCapLine = FString::Printf(TEXT("%s,%d,0,"), *Cap.Name, Cap.Level);
	const FString NewCapLine = FString::Printf(TEXT("%s,%d,777,"), *Cap.Name, Cap.Level);
	if (!TestTrue(FString::Printf(TEXT("the cap row starts with '%s'"), *OldCapLine), LevelCsv.Contains(OldCapLine)))
	{
		return false;
	}
	LevelCsv = LevelCsv.Replace(*OldCapLine, *NewCapLine);
	auto AppendLine = [](FString& Csv, const FString& Line)
	{
		if (!Csv.EndsWith(TEXT("\n")))
		{
			Csv += TEXT("\n");
		}
		Csv += Line + TEXT("\n");
	};
	AppendLine(LevelCsv, FString::Printf(TEXT("QA_NewCap,%d,0,\"qa: a raised cap\""), Cap.Level + 1));
	AppendLine(CoolerCsv, TEXT("QA_Huge,\"QA huge cooler\",30,"));
	AppendLine(MarketCsv, TEXT("QA_NightDock,\"QA night dock\",2.0,"));

	QAP::FEnv Env;
	if (!Env.Init(*this, LevelCsv, CoolerCsv, MarketCsv))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(50.0f, 0.0f, 0.0f));
	ALureSellPoint* Night = Env.SpawnSellPoint(*this, FVector::ZeroVector, TEXT("QA_NightDock"));
	if (!Player.IsValid() || !Player.Pawn || !Night)
	{
		return false;
	}
	int64 XpToNewCap = 777;
	for (int32 Index = 0; Index + 1 < Levels.Num(); ++Index)
	{
		XpToNewCap += Levels[Index].XpToNext;
	}
	TestEqual(TEXT("the new max level"), Player.Progression->GetMaxLevel(), Cap.Level + 1);
	Player.Progression->AddXp(static_cast<int32>(XpToNewCap - 1));
	TestEqual(TEXT("one XP short of the new level stays at the old cap"), Player.Progression->GetLevel(), Cap.Level);
	Player.Progression->AddXp(1);
	TestEqual(TEXT("the new level is reached with no code change"), Player.Progression->GetLevel(), Cap.Level + 1);

	TestTrue(TEXT("switch to the new cooler row"), Player.Cooler->SetCoolerId(TEXT("QA_Huge")));
	TestEqual(TEXT("the new cooler has 30 slots"), Player.Cooler->GetCapacity(), 30);

	TestEqual(TEXT("the new buyer's multiplier"), Night->GetSellMultiplier(), 2.0f, 1e-6f);
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_Snapper"), 7, 1));
	TestEqual(TEXT("the new buyer pays 7 x 2 = 14"), Night->SellAll(Player.Pawn).MoneyEarned, 14);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

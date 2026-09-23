// Lure T-010 tests (unreal-engineer): data validation of DT_PlayerLevel, DT_Cooler, DT_FishMarket (Project.Progression.Data.*).

#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Progression/LureProgressionSettings.h"
#include "Progression/LureProgressionTypes.h"

namespace ProgressionDataTest
{
	bool AnyContains(const TArray<FString>& Problems, const TCHAR* Needle)
	{
		return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
	}

	void TestReports(FAutomationTestBase& Test, const FString& Label, const TArray<FString>& Problems, const TCHAR* Needle)
	{
		Test.TestTrue(FString::Printf(TEXT("%s is reported (looking for '%s' in %d problems: %s)"), *Label, Needle, Problems.Num(),
			*FString::Join(Problems, TEXT(" | "))), AnyContains(Problems, Needle));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionDataShippedTablesValid, "Project.Progression.Data.ShippedTablesValid", LPT::Flags)
bool FProgressionDataShippedTablesValid::RunTest(const FString& Parameters)
{
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	struct FSpec
	{
		const TCHAR* File;
		UScriptStruct* Row;
	};
	const FSpec Specs[] = {
		{ TEXT("DT_PlayerLevel.csv"), FPlayerLevelRow::StaticStruct() },
		{ TEXT("DT_Cooler.csv"), FCoolerRow::StaticStruct() },
		{ TEXT("DT_FishMarket.csv"), FFishMarketRow::StaticStruct() },
	};
	for (const FSpec& Spec : Specs)
	{
		FString Csv;
		if (!LPT::LoadSource(*this, Spec.File, Csv))
		{
			continue;
		}
		const TArray<FString> CsvProblems = FLureProgressionData::ValidateCsvSource(Csv, Spec.Row, Spec.File);
		TestEqual(FString::Printf(TEXT("%s source problems: %s"), Spec.File, *FString::Join(CsvProblems, TEXT(" | "))), CsvProblems.Num(), 0);

		TArray<FString> ImportProblems;
		const TStrongObjectPtr<UDataTable> Table = LPT::MakeTable(Spec.Row, Csv, &ImportProblems);
		TestEqual(FString::Printf(TEXT("%s import problems: %s"), Spec.File, *FString::Join(ImportProblems, TEXT(" | "))), ImportProblems.Num(), 0);
		TestTrue(FString::Printf(TEXT("%s has rows"), Spec.File), Table->GetRowMap().Num() > 0);

		TArray<FString> Problems;
		if (Spec.Row == FPlayerLevelRow::StaticStruct())
		{
			Problems = FLureProgressionData::ValidatePlayerLevelTable(Table.Get());
		}
		else if (Spec.Row == FCoolerRow::StaticStruct())
		{
			Problems = FLureProgressionData::ValidateCoolerTable(Table.Get(), Settings->DefaultCoolerId);
		}
		else
		{
			Problems = FLureProgressionData::ValidateMarketTable(Table.Get(), Settings->DefaultMarketId);
		}
		TestEqual(FString::Printf(TEXT("%s validation problems: %s"), Spec.File, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionDataSettingsPointAtTables, "Project.Progression.Data.SettingsPointAtTables", LPT::Flags)
bool FProgressionDataSettingsPointAtTables::RunTest(const FString& Parameters)
{
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	TestEqual(TEXT("DT_PlayerLevel path"), Settings->PlayerLevelTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_PlayerLevel.DT_PlayerLevel")));
	TestEqual(TEXT("DT_Cooler path"), Settings->CoolerTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_Cooler.DT_Cooler")));
	TestEqual(TEXT("DT_FishMarket path"), Settings->MarketTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_FishMarket.DT_FishMarket")));
	TestFalse(TEXT("a default cooler is set"), Settings->DefaultCoolerId.IsNone());
	TestFalse(TEXT("a default market is set"), Settings->DefaultMarketId.IsNone());
	TestTrue(TEXT("the fallback cooler has room"), Settings->FallbackCoolerSlots >= 1);
	TestTrue(TEXT("starting money is not negative"), Settings->StartingMoney >= 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionDataLevelValidatorCatchesBadRows, "Project.Progression.Data.LevelValidatorCatchesBadRows", LPT::Flags)
bool FProgressionDataLevelValidatorCatchesBadRows::RunTest(const FString& Parameters)
{
	using namespace ProgressionDataTest;
	auto Validate = [](const FString& Csv)
	{
		const TStrongObjectPtr<UDataTable> Table = LPT::MakeTable(FPlayerLevelRow::StaticStruct(), Csv); // bad fixtures may also have import problems
		return FLureProgressionData::ValidatePlayerLevelTable(Table.Get());
	};

	TestEqual(TEXT("the fixture curve is valid"), Validate(LPT::FixtureLevelCsv()).Num(), 0);
	TestEqual(TEXT("a single capped level is valid"), Validate(LPT::LevelCsv({ 0 })).Num(), 0);

	TestReports(*this, TEXT("a gap in the levels"), Validate(TEXT("Name,Level,XpToNext,DevComment\nA,1,100,\nB,2,100,\nC,4,0,\n")), TEXT("level 3 is missing"));
	TestReports(*this, TEXT("two rows with one level"), Validate(TEXT("Name,Level,XpToNext,DevComment\nA,1,100,\nB,1,100,\nC,2,0,\n")), TEXT("both declare level 1"));
	TestReports(*this, TEXT("level 0"), Validate(TEXT("Name,Level,XpToNext,DevComment\nA,0,100,\nB,1,0,\n")), TEXT("must be >= 1"));
	TestReports(*this, TEXT("XpToNext 0 below the cap"), Validate(LPT::LevelCsv({ 100, 0, 200, 0 })), TEXT("must be > 0 below the last level"));
	TestReports(*this, TEXT("a last level with XpToNext"), Validate(LPT::LevelCsv({ 100, 200 })), TEXT("XpToNext must be 0"));
	TestReports(*this, TEXT("a curve that gets easier"), Validate(LPT::LevelCsv({ 200, 100, 0 })), TEXT("must not get easier"));
	TestReports(*this, TEXT("an empty table"), Validate(TEXT("Name,Level,XpToNext,DevComment\n")), TEXT("no rows"));
	TestReports(*this, TEXT("no table"), FLureProgressionData::ValidatePlayerLevelTable(nullptr), TEXT("no table"));

	const TStrongObjectPtr<UDataTable> Cooler = LPT::MakeTable(*this, FCoolerRow::StaticStruct(), LPT::FixtureCoolerCsv());
	TestReports(*this, TEXT("a table with another row struct"), FLureProgressionData::ValidatePlayerLevelTable(Cooler.Get()), TEXT("row struct"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionDataCoolerAndMarketValidatorsCatchBadRows, "Project.Progression.Data.CoolerAndMarketValidatorsCatchBadRows", LPT::Flags)
bool FProgressionDataCoolerAndMarketValidatorsCatchBadRows::RunTest(const FString& Parameters)
{
	using namespace ProgressionDataTest;
	auto Cooler = [](const FString& Csv, FName Default = NAME_None)
	{
		const TStrongObjectPtr<UDataTable> Table = LPT::MakeTable(FCoolerRow::StaticStruct(), Csv);
		return FLureProgressionData::ValidateCoolerTable(Table.Get(), Default);
	};
	auto Market = [](const FString& Csv, FName Default = NAME_None)
	{
		const TStrongObjectPtr<UDataTable> Table = LPT::MakeTable(FFishMarketRow::StaticStruct(), Csv);
		return FLureProgressionData::ValidateMarketTable(Table.Get(), Default);
	};

	TestEqual(TEXT("the fixture coolers are valid"), Cooler(LPT::FixtureCoolerCsv(), TEXT("Basic")).Num(), 0);
	TestReports(*this, TEXT("a cooler with 0 slots"), Cooler(TEXT("Name,DisplayName,Slots,DevComment\nA,\"A\",0,\n")), TEXT("Slots 0"));
	TestReports(*this, TEXT("a cooler over the slot cap"), Cooler(TEXT("Name,DisplayName,Slots,DevComment\nA,\"A\",101,\n")), TEXT("Slots 101"));
	TestReports(*this, TEXT("a cooler without a name"), Cooler(TEXT("Name,DisplayName,Slots,DevComment\nA,\"\",4,\n")), TEXT("DisplayName is empty"));
	TestReports(*this, TEXT("a missing default cooler"), Cooler(LPT::FixtureCoolerCsv(), TEXT("Nope")), TEXT("default cooler 'Nope'"));

	TestEqual(TEXT("the fixture markets are valid"), Market(LPT::FixtureMarketCsv(), TEXT("Default")).Num(), 0);
	TestReports(*this, TEXT("a zero multiplier"), Market(TEXT("Name,DisplayName,SellMultiplier,DevComment\nA,\"A\",0,\n")), TEXT("SellMultiplier 0"));
	TestReports(*this, TEXT("a negative multiplier"), Market(TEXT("Name,DisplayName,SellMultiplier,DevComment\nA,\"A\",-1,\n")), TEXT("SellMultiplier -1"));
	TestReports(*this, TEXT("a huge multiplier"), Market(TEXT("Name,DisplayName,SellMultiplier,DevComment\nA,\"A\",11,\n")), TEXT("SellMultiplier 11"));
	TestReports(*this, TEXT("a missing default market"), Market(LPT::FixtureMarketCsv(), TEXT("Nope")), TEXT("default market 'Nope'"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionDataCsvSourceChecks, "Project.Progression.Data.CsvSourceChecks", LPT::Flags)
bool FProgressionDataCsvSourceChecks::RunTest(const FString& Parameters)
{
	using namespace ProgressionDataTest;
	UScriptStruct* Row = FPlayerLevelRow::StaticStruct();
	TestEqual(TEXT("the fixture source is clean"), FLureProgressionData::ValidateCsvSource(LPT::FixtureLevelCsv(), Row, TEXT("Fixture")).Num(), 0);
	TestEqual(TEXT("DevComment is optional"), FLureProgressionData::ValidateCsvSource(TEXT("Name,Level,XpToNext\nA,1,0\n"), Row, TEXT("Fixture")).Num(), 0);

	TestReports(*this, TEXT("text in a number cell"), FLureProgressionData::ValidateCsvSource(TEXT("Name,Level,XpToNext\nA,1,lots\n"), Row, TEXT("Fixture")), TEXT("'lots' is not a number"));
	TestReports(*this, TEXT("an empty number cell"), FLureProgressionData::ValidateCsvSource(TEXT("Name,Level,XpToNext\nA,1,\n"), Row, TEXT("Fixture")), TEXT("is not a number"));
	TestReports(*this, TEXT("a fraction in a whole-number cell"), FLureProgressionData::ValidateCsvSource(TEXT("Name,Level,XpToNext\nA,1,2.5\n"), Row, TEXT("Fixture")), TEXT("whole number"));
	TestReports(*this, TEXT("an unknown column"), FLureProgressionData::ValidateCsvSource(TEXT("Name,Level,XpToNext,Bonus\nA,1,0,3\n"), Row, TEXT("Fixture")), TEXT("'Bonus' is not a property"));
	TestReports(*this, TEXT("a missing column"), FLureProgressionData::ValidateCsvSource(TEXT("Name,Level\nA,1\n"), Row, TEXT("Fixture")), TEXT("required property XpToNext"));
	TestReports(*this, TEXT("a duplicate row name"), FLureProgressionData::ValidateCsvSource(TEXT("Name,Level,XpToNext\nA,1,10\nA,2,0\n"), Row, TEXT("Fixture")), TEXT("appears twice"));
	TestReports(*this, TEXT("a float column with text"), FLureProgressionData::ValidateCsvSource(TEXT("Name,DisplayName,SellMultiplier\nA,\"A\",cheap\n"), FFishMarketRow::StaticStruct(), TEXT("Fixture")), TEXT("'cheap' is not a number"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

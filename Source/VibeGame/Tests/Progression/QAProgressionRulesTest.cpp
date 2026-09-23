// Lure T-010 independent QA tests (qa-engineer): pure rules (sell price, totals, saturating money/XP, the XP curve).
// Project.Progression.QA.Rules.*

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Math/RandomStream.h"
#include "Progression/LureProgressionTypes.h"

namespace QAProgressionRules
{
	/** The spec's formula, computed independently: max(1, floor(Value x Multiplier + 0.5)) for a real fish */
	int64 Expected(int32 Value, double Multiplier)
	{
		return FMath::Max<int64>(1, static_cast<int64>(FMath::FloorToDouble(static_cast<double>(Value) * Multiplier + 0.5)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgRulesPriceRoundHalfUp, "Project.Progression.QA.Rules.Price.RoundHalfUpNotBankers", QAP::Flags)
bool FQAProgRulesPriceRoundHalfUp::RunTest(const FString& Parameters)
{
	// Exactly representable multipliers, so the .5 cases are real ties.
	struct FCase { int32 Value; float Multiplier; int32 Price; };
	const FCase Cases[] = {
		{ 5, 0.5f, 3 },    // 2.5 -> 3 (banker's rounding would give 2)
		{ 9, 0.5f, 5 },    // 4.5 -> 5 (banker's: 4)
		{ 3, 0.5f, 2 },    // 1.5 -> 2
		{ 10, 0.25f, 3 },  // 2.5 -> 3
		{ 9, 0.25f, 2 },   // 2.25 -> 2
		{ 11, 0.25f, 3 },  // 2.75 -> 3
		{ 2, 1.25f, 3 },   // 2.5 -> 3
		{ 6, 0.75f, 5 },   // 4.5 -> 5
		{ 1, 0.25f, 1 },   // 0.25 -> 0, but a real fish pays at least 1
		{ 1, 0.5f, 1 },    // 0.5 -> 1
		{ 1, 10.0f, 10 },
		{ 1000, 1.0f, 1000 },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%d x %g"), Case.Value, Case.Multiplier),
			FLureProgressionRules::GetSellPrice(QAP::Fish(TEXT("QA_Fish"), Case.Value, 0), Case.Multiplier), Case.Price);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgRulesPriceFormulaAndMonotone, "Project.Progression.QA.Rules.Price.FormulaMinimumAndMonotone", QAP::Flags)
bool FQAProgRulesPriceFormulaAndMonotone::RunTest(const FString& Parameters)
{
	const float Exact[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 10.0f };
	const float Inexact[] = { 0.01f, 0.1f, 0.3f, 0.7f, 1.1f, 2.3f, 9.99f };
	int32 Mismatches = 0;
	int32 NotMonotone = 0;
	int32 BelowOne = 0;
	for (const float Multiplier : Exact)
	{
		for (int32 Value = 1; Value <= 3000; ++Value)
		{
			const int32 Price = FLureProgressionRules::GetSellPrice(QAP::Fish(TEXT("QA_Fish"), Value, 0), Multiplier);
			if (Price != QAProgressionRules::Expected(Value, Multiplier) && ++Mismatches <= 5)
			{
				AddError(FString::Printf(TEXT("%d x %g paid %d, expected %lld"), Value, Multiplier, Price, QAProgressionRules::Expected(Value, Multiplier)));
			}
		}
	}
	for (const float Multiplier : Inexact)
	{
		int32 Previous = 0;
		for (int32 Value = 1; Value <= 3000; ++Value)
		{
			const int32 Price = FLureProgressionRules::GetSellPrice(QAP::Fish(TEXT("QA_Fish"), Value, 0), Multiplier);
			BelowOne += Price < 1 ? 1 : 0;
			NotMonotone += Price < Previous ? 1 : 0;
			// Within one coin of the exact formula (float rounding at a .5 tie may go either way)
			if (FMath::Abs(Price - QAProgressionRules::Expected(Value, Multiplier)) > 1 && ++Mismatches <= 5)
			{
				AddError(FString::Printf(TEXT("%d x %g paid %d, expected about %lld"), Value, Multiplier, Price, QAProgressionRules::Expected(Value, Multiplier)));
			}
			Previous = Price;
		}
	}
	TestEqual(TEXT("prices match max(1, floor(Value x m + 0.5))"), Mismatches, 0);
	TestEqual(TEXT("a real fish always pays at least 1"), BelowOne, 0);
	TestEqual(TEXT("a higher Value never pays less"), NotMonotone, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgRulesPriceOnlyValue, "Project.Progression.QA.Rules.Price.DependsOnlyOnRolledValue", QAP::Flags)
bool FQAProgRulesPriceOnlyValue::RunTest(const FString& Parameters)
{
	// Species, weight, rarity and modifiers are already inside Value (roll step 6): no second pricing formula.
	FFishInstance Plain = QAP::Fish(TEXT("QA_Minnow"), 37, 5);
	FFishInstance Fancy = QAP::Fish(TEXT("QA_Marlin"), 37, 900, 42);
	Fancy.RarityId = TEXT("Legendary");
	Fancy.WeightKg = 250.0f;
	Fancy.Level = 9;
	Fancy.DifficultyRating = 7.5f;
	Fancy.ModifierIds = { TEXT("Albino"), TEXT("Giant") };
	for (const float Multiplier : { 0.5f, 1.0f, 1.25f, 3.0f })
	{
		TestEqual(FString::Printf(TEXT("same Value, same price at x%g"), Multiplier),
			FLureProgressionRules::GetSellPrice(Fancy, Multiplier), FLureProgressionRules::GetSellPrice(Plain, Multiplier));
	}
	Fancy.Value = 38;
	TestTrue(TEXT("one more Value coin pays more at x1"), FLureProgressionRules::GetSellPrice(Fancy, 1.0f) > FLureProgressionRules::GetSellPrice(Plain, 1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgRulesTotalIsSumAndOrderFree, "Project.Progression.QA.Rules.Total.SumOfSinglesAndOrderFree", QAP::Flags)
bool FQAProgRulesTotalIsSumAndOrderFree::RunTest(const FString& Parameters)
{
	FRandomStream Random(20260923);
	int32 Bad = 0;
	for (int32 Trial = 0; Trial < 300; ++Trial)
	{
		const float Multiplier = Random.FRandRange(0.05f, 10.0f);
		const int32 Count = Random.RandRange(0, 20);
		TArray<FFishInstance> Catch;
		int64 Sum = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Catch.Add(QAP::Fish(TEXT("QA_Fish"), Random.RandRange(1, 5000), 1, Index));
			Sum += FLureProgressionRules::GetSellPrice(Catch.Last(), Multiplier);
		}
		const int32 Total = FLureProgressionRules::GetSellTotal(Catch, Multiplier);
		TArray<FFishInstance> Shuffled = Catch;
		for (int32 Index = Shuffled.Num() - 1; Index > 0; --Index)
		{
			Shuffled.Swap(Index, Random.RandRange(0, Index));
		}
		if ((Total != Sum || FLureProgressionRules::GetSellTotal(Shuffled, Multiplier) != Total) && ++Bad <= 5)
		{
			AddError(FString::Printf(TEXT("trial %d: %d fish x%g total %d, sum of singles %lld"), Trial, Count, Multiplier, Total, Sum));
		}
	}
	TestEqual(TEXT("300 seeded catches: total = sum of per-fish prices, in any order"), Bad, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgRulesSaturatingExtremes, "Project.Progression.QA.Rules.SaturatingAddExtremes", QAP::Flags)
bool FQAProgRulesSaturatingExtremes::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("MAX + MAX = MAX"), FLureProgressionRules::SaturatingAdd(MAX_int32, MAX_int32), MAX_int32);
	TestEqual(TEXT("MAX + 1 = MAX"), FLureProgressionRules::SaturatingAdd(MAX_int32, 1), MAX_int32);
	TestEqual(TEXT("MAX + 0 = MAX"), FLureProgressionRules::SaturatingAdd(MAX_int32, 0), MAX_int32);
	TestEqual(TEXT("MIN + MIN = 0 (never wraps to positive)"), FLureProgressionRules::SaturatingAdd(MIN_int32, MIN_int32), 0);
	TestEqual(TEXT("0 + MIN = 0"), FLureProgressionRules::SaturatingAdd(0, MIN_int32), 0);
	TestEqual(TEXT("MAX + MIN = 0 (clamped from -1)"), FLureProgressionRules::SaturatingAdd(MAX_int32, MIN_int32), 0);
	TestEqual(TEXT("5 - 5 = 0"), FLureProgressionRules::SaturatingAdd(5, -5), 0);
	TestEqual(TEXT("2 + 3 = 5"), FLureProgressionRules::SaturatingAdd(2, 3), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgRulesShippedCurveStarts, "Project.Progression.QA.Rules.Curve.ShippedStartsMatchSummedCsv", QAP::Flags)
bool FQAProgRulesShippedCurveStarts::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!QAP::ReadSource(*this, TEXT("DT_PlayerLevel.csv"), Csv))
	{
		return false;
	}
	// Independent sums from the raw CSV
	TArray<TArray<FString>> Rows = QAP::ParseCsv(Csv);
	if (!TestTrue(TEXT("rows"), Rows.Num() >= 2))
	{
		return false;
	}
	const int32 LevelCol = Rows[0].IndexOfByKey(FString(TEXT("Level")));
	const int32 XpCol = Rows[0].IndexOfByKey(FString(TEXT("XpToNext")));
	TMap<int32, int32> XpToNextByLevel;
	for (int32 Row = 1; Row < Rows.Num(); ++Row)
	{
		XpToNextByLevel.Add(FCString::Atoi(*Rows[Row][LevelCol]), FCString::Atoi(*Rows[Row][XpCol]));
	}
	const int32 MaxLevel = XpToNextByLevel.Num();
	const TStrongObjectPtr<UDataTable> Table = QAP::MakeTableChecked(*this, FPlayerLevelRow::StaticStruct(), Csv, TEXT("DT_PlayerLevel.csv"));
	const FLureLevelCurve Curve = FLureLevelCurve::FromTable(Table.Get());
	TestEqual(TEXT("max level = number of rows"), Curve.GetMaxLevel(), MaxLevel);
	int64 Start = 0;
	for (int32 Level = 1; Level <= MaxLevel; ++Level)
	{
		TestEqual(FString::Printf(TEXT("level %d starts at %lld XP"), Level, Start), Curve.GetXpAtLevelStart(Level), Start);
		TestEqual(FString::Printf(TEXT("%lld XP is level %d"), Start, Level), Curve.GetLevelForXp(Start), Level);
		if (Level > 1)
		{
			TestEqual(FString::Printf(TEXT("%lld XP is still level %d"), Start - 1, Level - 1), Curve.GetLevelForXp(Start - 1), Level - 1);
		}
		Start += XpToNextByLevel.FindRef(Level);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgRulesCurveMonotoneAndBounded, "Project.Progression.QA.Rules.Curve.MonotoneAndBounded", QAP::Flags)
bool FQAProgRulesCurveMonotoneAndBounded::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Table = QAP::MakeTableChecked(*this, FPlayerLevelRow::StaticStruct(), QAP::QALevelsCsv(), TEXT("QA levels"));
	const FLureLevelCurve Curve = FLureLevelCurve::FromTable(Table.Get());
	TestEqual(TEXT("QA curve cap"), Curve.GetMaxLevel(), 5);
	int32 Previous = 1;
	int32 Bad = 0;
	for (int64 Xp = 0; Xp <= 1000; ++Xp)
	{
		const int32 Level = Curve.GetLevelForXp(Xp);
		Bad += (Level < Previous || Level < 1 || Level > 5) ? 1 : 0;
		Previous = Level;
		const FLureLevelProgress Progress = Curve.GetProgress(Xp, Level);
		Bad += (Progress.Fraction < 0.0f || Progress.Fraction > 1.0f || Progress.XpIntoLevel < 0) ? 1 : 0;
	}
	TestEqual(TEXT("level never drops as XP rises, stays in 1..cap, progress fraction in [0, 1]"), Bad, 0);
	TestEqual(TEXT("MIN_int64 XP is level 1"), Curve.GetLevelForXp(MIN_int64), 1);
	TestEqual(TEXT("MAX_int64 XP is the cap"), Curve.GetLevelForXp(MAX_int64), 5);
	TestEqual(TEXT("MAX_int32 XP is the cap"), Curve.GetLevelForXp(MAX_int32), 5);
	TestEqual(TEXT("level 0 start clamps to level 1"), Curve.GetXpAtLevelStart(0), int64(0));
	TestEqual(TEXT("negative level start clamps to level 1"), Curve.GetXpAtLevelStart(-3), int64(0));
	const FLureLevelProgress AtCap = Curve.GetProgress(MAX_int32, 5);
	TestTrue(TEXT("progress at MAX_int32 XP: max level, fraction 1"), AtCap.bIsMaxLevel && FMath::IsNearlyEqual(AtCap.Fraction, 1.0f));
	TestTrue(TEXT("progress at MAX_int32 XP: XpIntoLevel does not wrap"), AtCap.XpIntoLevel >= 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

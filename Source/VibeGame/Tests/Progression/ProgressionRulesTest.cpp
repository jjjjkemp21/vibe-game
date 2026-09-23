// Lure T-010 tests (unreal-engineer): pure rules. XP curve (Project.Progression.Level.*), sell math (Project.Progression.Sell.*).

#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Misc/FileHelper.h"
#include "Progression/LureProgressionTypes.h"
#include "UObject/Package.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelCurveMath, "Project.Progression.Level.CurveMath", LPT::Flags)
bool FProgressionLevelCurveMath::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Table = LPT::MakeTable(*this, FPlayerLevelRow::StaticStruct(), LPT::FixtureLevelCsv());
	TArray<FString> Problems;
	const FLureLevelCurve Curve = FLureLevelCurve::FromTable(Table.Get(), &Problems);
	TestEqual(TEXT("no curve problems"), Problems.Num(), 0);
	TestEqual(TEXT("max level"), Curve.GetMaxLevel(), 4);

	TestEqual(TEXT("level 1 starts at 0"), Curve.GetXpAtLevelStart(1), int64(0));
	TestEqual(TEXT("level 2 starts at 100"), Curve.GetXpAtLevelStart(2), int64(100));
	TestEqual(TEXT("level 3 starts at 250"), Curve.GetXpAtLevelStart(3), int64(250));
	TestEqual(TEXT("level 4 starts at 450"), Curve.GetXpAtLevelStart(4), int64(450));
	TestEqual(TEXT("levels past the cap clamp"), Curve.GetXpAtLevelStart(9), int64(450));

	struct FCase { int64 Xp; int32 Level; };
	const FCase Cases[] = { { -5, 1 }, { 0, 1 }, { 99, 1 }, { 100, 2 }, { 249, 2 }, { 250, 3 }, { 449, 3 }, { 450, 4 }, { 100000, 4 } };
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("XP %lld -> level"), Case.Xp), Curve.GetLevelForXp(Case.Xp), Case.Level);
	}

	const FLureLevelProgress Mid = Curve.GetProgress(130, 2);
	TestEqual(TEXT("130 XP: level 2"), Mid.Level, 2);
	TestEqual(TEXT("130 XP: 30 into level 2"), Mid.XpIntoLevel, 30);
	TestEqual(TEXT("130 XP: level 2 needs 150"), Mid.XpForNextLevel, 150);
	TestEqual(TEXT("130 XP: fraction 0.2"), Mid.Fraction, 0.2f, 1e-5f);
	TestFalse(TEXT("130 XP: not max"), Mid.bIsMaxLevel);

	const FLureLevelProgress Cap = Curve.GetProgress(500, 1);
	TestEqual(TEXT("500 XP: level 4 even if the stored level lags"), Cap.Level, 4);
	TestTrue(TEXT("500 XP: max level"), Cap.bIsMaxLevel);
	TestEqual(TEXT("500 XP: 50 past the cap start"), Cap.XpIntoLevel, 50);
	TestEqual(TEXT("500 XP: nothing to reach"), Cap.XpForNextLevel, 0);

	const FLureLevelProgress Floor = Curve.GetProgress(10, 3);
	TestEqual(TEXT("a loaded level above the XP is kept"), Floor.Level, 3);
	TestEqual(TEXT("... with 0 into it"), Floor.XpIntoLevel, 0);

	const FLureLevelCurve Stopped = FLureLevelCurve::FromValues({ 100, 0, 50 });
	TestEqual(TEXT("a 0 below the end stops the curve there"), Stopped.GetMaxLevel(), 2);
	const FLureLevelCurve Empty;
	TestTrue(TEXT("no table = empty curve"), Empty.IsEmpty());
	TestEqual(TEXT("an empty curve is level 1 forever"), Empty.GetLevelForXp(1000000), 1);

	TArray<FString> GapProblems;
	const TStrongObjectPtr<UDataTable> Gap = LPT::MakeTable(FPlayerLevelRow::StaticStruct(), TEXT("Name,Level,XpToNext\nA,1,100\nB,2,100\nC,4,0\n"));
	const FLureLevelCurve GapCurve = FLureLevelCurve::FromTable(Gap.Get(), &GapProblems);
	TestEqual(TEXT("a gap stops the curve before it"), GapCurve.GetMaxLevel(), 2);
	TestTrue(TEXT("the gap is reported"), GapProblems.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelShippedCurve, "Project.Progression.Level.ShippedCurve", LPT::Flags)
bool FProgressionLevelShippedCurve::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Table = LPT::ShippedTable(*this, FPlayerLevelRow::StaticStruct(), TEXT("DT_PlayerLevel.csv"));
	if (!Table.IsValid())
	{
		return false;
	}
	TArray<FString> Problems;
	const FLureLevelCurve Curve = FLureLevelCurve::FromTable(Table.Get(), &Problems);
	TestEqual(TEXT("no curve problems"), Problems.Num(), 0);
	TestEqual(TEXT("every row is on the curve"), Curve.GetMaxLevel(), Table->GetRowMap().Num());
	TestTrue(TEXT("there is more than one level"), Curve.GetMaxLevel() > 1);
	for (int32 Level = 2; Level <= Curve.GetMaxLevel(); ++Level)
	{
		TestTrue(FString::Printf(TEXT("level %d starts later than level %d"), Level, Level - 1), Curve.GetXpAtLevelStart(Level) > Curve.GetXpAtLevelStart(Level - 1));
		TestEqual(FString::Printf(TEXT("level %d is reached exactly at its start"), Level), Curve.GetLevelForXp(Curve.GetXpAtLevelStart(Level)), Level);
		TestEqual(FString::Printf(TEXT("one XP short stays at level %d"), Level - 1), Curve.GetLevelForXp(Curve.GetXpAtLevelStart(Level) - 1), Level - 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelDifficultyByLevelGapMath, "Project.Progression.Level.DifficultyByLevelGapMath", LPT::Flags)
bool FProgressionLevelDifficultyByLevelGapMath::RunTest(const FString& Parameters)
{
	// The level-gap rule is the fish system's hook (reused, not duplicated). Its shape as T-007 relies on it:
	FFishLevelScaling Scaling;
	Scaling.OverLevelFactor = 0.4f;
	Scaling.UnderLevelFactor = 0.1f;
	Scaling.MinMultiplier = 0.5f;
	Scaling.MaxMultiplier = 3.0f;
	TestEqual(TEXT("same level = 1"), FFishRoll::LevelDifficultyMultiplier(5, 5, Scaling), 1.0f, 1e-5f);
	TestEqual(TEXT("1 above = 1 + OverLevelFactor"), FFishRoll::LevelDifficultyMultiplier(6, 5, Scaling), 1.4f, 1e-5f);
	TestEqual(TEXT("3 below = 1 - 3 x UnderLevelFactor"), FFishRoll::LevelDifficultyMultiplier(2, 5, Scaling), 0.7f, 1e-5f);
	TestEqual(TEXT("far above clamps to MaxMultiplier"), FFishRoll::LevelDifficultyMultiplier(50, 1, Scaling), 3.0f, 1e-5f);
	TestEqual(TEXT("far below clamps to MinMultiplier"), FFishRoll::LevelDifficultyMultiplier(1, 50, Scaling), 0.5f, 1e-5f);
	float Previous = 0.0f;
	for (int32 FishLevel = 1; FishLevel <= 12; ++FishLevel)
	{
		const float Multiplier = FFishRoll::LevelDifficultyMultiplier(FishLevel, 5, Scaling);
		TestTrue(FString::Printf(TEXT("fish level %d is at least as hard as %d"), FishLevel, FishLevel - 1), Multiplier >= Previous);
		Previous = Multiplier;
	}

	// The shipped data (DefaultGame.ini [/Script/VibeGame.FishSettings] LevelScaling) makes fish above your level harder.
	const FFishLevelScaling& Shipped = GetDefault<UFishSettings>()->LevelScaling;
	TestTrue(TEXT("shipped: 1 level above is harder"), FFishRoll::LevelDifficultyMultiplier(2, 1, Shipped) > 1.0f);
	TestTrue(TEXT("shipped: 3 levels above is at least 2x"), FFishRoll::LevelDifficultyMultiplier(4, 1, Shipped) >= 2.0f);
	TestTrue(TEXT("shipped: below your level is not harder"), FFishRoll::LevelDifficultyMultiplier(1, 4, Shipped) <= 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionSellPriceMath, "Project.Progression.Sell.PriceMath", LPT::Flags)
bool FProgressionSellPriceMath::RunTest(const FString& Parameters)
{
	auto Price = [](int32 Value, float Multiplier) { return FLureProgressionRules::GetSellPrice(LPT::MakeFish(TEXT("A"), Value, 0), Multiplier); };
	TestEqual(TEXT("x1 pays the rolled Value"), Price(37, 1.0f), 37);
	TestEqual(TEXT("10 x 1.25 = 12.5 rounds half up to 13"), Price(10, 1.25f), 13);
	TestEqual(TEXT("7 x 0.5 = 3.5 rounds half up to 4"), Price(7, 0.5f), 4);
	TestEqual(TEXT("9 x 0.3 = 2.7 rounds to 3"), Price(9, 0.3f), 3);
	TestEqual(TEXT("1 x 0.4 = 0.4 still pays the minimum 1"), Price(1, 0.4f), 1);
	TestEqual(TEXT("multiplier 0 pays nothing"), Price(10, 0.0f), 0);
	TestEqual(TEXT("a negative multiplier pays nothing"), Price(10, -2.0f), 0);
	TestEqual(TEXT("a NaN multiplier pays nothing"), Price(10, std::numeric_limits<float>::quiet_NaN()), 0);
	TestEqual(TEXT("an infinite multiplier pays nothing"), Price(10, std::numeric_limits<float>::infinity()), 0);
	TestEqual(TEXT("Value 0 pays nothing"), Price(0, 1.0f), 0);
	TestEqual(TEXT("a negative Value pays nothing"), Price(-5, 1.0f), 0);
	TestEqual(TEXT("huge prices saturate"), Price(MAX_int32, 10.0f), MAX_int32);
	TestEqual(TEXT("an invalid fish (no species) pays nothing"), FLureProgressionRules::GetSellPrice(FFishInstance(), 1.0f), 0);

	// Totals round each fish on its own (what selling them one by one pays): 3 x round(5 x 1.1 = 5.5) = 18, not round(16.5) = 17.
	const TArray<FFishInstance> Three = { LPT::MakeFish(TEXT("A"), 5, 0), LPT::MakeFish(TEXT("B"), 5, 0), LPT::MakeFish(TEXT("C"), 5, 0) };
	TestEqual(TEXT("total = sum of the per-fish prices"), FLureProgressionRules::GetSellTotal(Three, 1.1f), 18);
	TestEqual(TEXT("no fish, no money"), FLureProgressionRules::GetSellTotal({}, 1.0f), 0);
	const TArray<FFishInstance> Big = { LPT::MakeFish(TEXT("A"), MAX_int32, 0), LPT::MakeFish(TEXT("B"), MAX_int32, 0) };
	TestEqual(TEXT("totals saturate"), FLureProgressionRules::GetSellTotal(Big, 1.0f), MAX_int32);

	TestEqual(TEXT("saturating add"), FLureProgressionRules::SaturatingAdd(MAX_int32 - 1, 5), MAX_int32);
	TestEqual(TEXT("saturating add never goes below 0"), FLureProgressionRules::SaturatingAdd(3, -10), 0);
	return true;
}

namespace ProgressionRulesTest
{
	TStrongObjectPtr<UDataTable> LoadFishJson(FAutomationTestBase& Test, UScriptStruct* RowStruct, const TCHAR* File)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *LPT::SourcePath(File)))
		{
			Test.AddError(FString::Printf(TEXT("Can't read %s"), File));
			return TStrongObjectPtr<UDataTable>();
		}
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = RowStruct;
		for (const FString& Problem : Table->CreateTableFromJSONString(Json))
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), File, *Problem));
		}
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionSellUsesRolledValue, "Project.Progression.Sell.UsesRolledValue", LPT::Flags)
bool FProgressionSellUsesRolledValue::RunTest(const FString& Parameters)
{
	using namespace ProgressionRulesTest;
	// Real fish from the one roll pipeline (data/tables JSON): the sell price is the fish's Value x the market multiplier.
	const TStrongObjectPtr<UDataTable> Species = LoadFishJson(*this, FFishSpeciesRow::StaticStruct(), TEXT("DT_FishSpecies.json"));
	const TStrongObjectPtr<UDataTable> Rarities = LoadFishJson(*this, FFishRarityRow::StaticStruct(), TEXT("DT_FishRarity.json"));
	const TStrongObjectPtr<UDataTable> Modifiers = LoadFishJson(*this, FFishModifierRow::StaticStruct(), TEXT("DT_FishModifier.json"));
	const TStrongObjectPtr<UDataTable> Stats = LoadFishJson(*this, FFishStatRow::StaticStruct(), TEXT("DT_FishStat.json"));
	const TStrongObjectPtr<UDataTable> Market = LPT::ShippedTable(*this, FFishMarketRow::StaticStruct(), TEXT("DT_FishMarket.csv"));
	if (!Species.IsValid() || !Rarities.IsValid() || !Modifiers.IsValid() || !Stats.IsValid() || !Market.IsValid())
	{
		return false;
	}
	FFishTables Tables;
	Tables.Species = Species.Get();
	Tables.Rarities = Rarities.Get();
	Tables.Modifiers = Modifiers.Get();
	Tables.Stats = Stats.Get();

	auto Roll = [&](FName SpeciesId, FName RarityId, float WeightFraction, FFishInstance& Out)
	{
		FFishRollContext Context;
		Context.SpeciesId = SpeciesId;
		Context.Seed = 1234;
		Context.ForcedRarityId = RarityId;
		Context.bForceModifiers = true; // no modifiers
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = WeightFraction;
		return FFishRoll::Roll(Tables, Context, Out);
	};

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Row : Species->GetRowMap())
	{
		FFishInstance Common;
		FFishInstance Rare;
		if (!TestTrue(FString::Printf(TEXT("%s rolls Common"), *Row.Key.ToString()), Roll(Row.Key, TEXT("Common"), 0.5f, Common))
			|| !TestTrue(FString::Printf(TEXT("%s rolls Rare"), *Row.Key.ToString()), Roll(Row.Key, TEXT("Rare"), 0.5f, Rare)))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: x1 sells for the rolled Value"), *Row.Key.ToString()), FLureProgressionRules::GetSellPrice(Common, 1.0f), Common.Value);
		TestTrue(FString::Printf(TEXT("%s: Rare sells for more than Common at the same weight"), *Row.Key.ToString()),
			FLureProgressionRules::GetSellPrice(Rare, 1.0f) > FLureProgressionRules::GetSellPrice(Common, 1.0f));

		FFishInstance Heavy;
		if (Roll(Row.Key, TEXT("Common"), 1.0f, Heavy))
		{
			TestTrue(FString::Printf(TEXT("%s: heavier sells for at least as much"), *Row.Key.ToString()),
				FLureProgressionRules::GetSellPrice(Heavy, 1.0f) >= FLureProgressionRules::GetSellPrice(Common, 1.0f));
		}
		for (const TPair<FName, uint8*>& MarketRow : Market->GetRowMap())
		{
			const float Multiplier = reinterpret_cast<const FFishMarketRow*>(MarketRow.Value)->SellMultiplier;
			const int32 Expected = FMath::Max(1, static_cast<int32>(FMath::FloorToDouble(static_cast<double>(Rare.Value) * Multiplier + 0.5)));
			TestEqual(FString::Printf(TEXT("%s at %s"), *Row.Key.ToString(), *MarketRow.Key.ToString()), FLureProgressionRules::GetSellPrice(Rare, Multiplier), Expected);
		}
		++Checked;
	}
	TestTrue(TEXT("at least one species was checked"), Checked > 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

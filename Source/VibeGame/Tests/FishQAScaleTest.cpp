// QA-owned scale tests for T-008: "many species" works (1,000 generated species through the real JSON import, the
// validator, the bite picker and the roll), with correctness checks and performance budgets.
// Budgets (test design QA-105): generous ceilings meant to catch accidental O(N^2) work (per-roll table parsing or
// sorting, lookups in nested loops) before the roster grows, not to benchmark; a machine-independent ratio check
// (1,000 vs 100 species) catches quadratic scaling. Every timing is the best of 3 (another lane may be building).

#include "FishQATestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"

namespace FishQA_Scale
{
	using namespace FishQA;

	static FString F(float X)
	{
		return FString::SanitizeFloat(X);
	}

	/** Species i: unique weight range [i+1, i+1.5] (so a wrong-row lookup is visible), habitat H(i % 10), bait A */
	static FString GeneratedSpeciesJson(int32 Count)
	{
		TArray<FString> Rows;
		Rows.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			Rows.Add(FString::Printf(TEXT("{\"Name\":\"Gen_%04d\",\"DisplayName\":\"Generated %04d\",\"JournalText\":\"QA scale species\",\"BaseLevel\":%d,")
				TEXT("\"WeightMin\":%s,\"WeightMax\":%s,\"SizeSkew\":1.3,\"ReferenceWeight\":%s,\"WeightStatExponent\":0.5,")
				TEXT("\"BaseStats\":[{\"Tag\":\"Fish.Stat.Strength\",\"Value\":10},{\"Tag\":\"Fish.Stat.Stamina\",\"Value\":20},{\"Tag\":\"Fish.Stat.Speed\",\"Value\":5}],")
				TEXT("\"BaseValuePerKg\":3,\"BiteWeight\":1,\"HabitatTags\":[\"Test.Fish.Habitat.H%d\"],\"RegionTags\":[],\"TimeWindows\":[],\"WeatherTags\":[],")
				TEXT("\"AcceptedBait\":[\"Test.Fish.Bait.A\"],\"SpeciesTags\":[],\"AllowedRarities\":[],\"AllowedModifiers\":[],\"MaxModifiers\":2,\"FightPatternId\":\"Run\",\"Mesh\":\"None\"}"),
				i, i, 1 + i % 60, *F(i + 1.0f), *F(i + 1.5f), *F(i + 1.0f), i % 10));
		}
		return TEXT("[") + FString::Join(Rows, TEXT(",\n")) + TEXT("]");
	}

	/** Generated species + the real rarity, modifier and stat sources (the Weight stat's Max raised to 5000 kg for the big species) */
	static bool MakeScaleTables(FAutomationTestBase& Test, int32 Count, FTables& T, double* OutImportSeconds = nullptr)
	{
		FSourceTexts Texts;
		if (!ReadRealSources(Test, Texts))
		{
			return false;
		}
		if (!ImportJsonChecked(Test, T.Stats, FFishStatRow::StaticStruct(), Texts.Stats, TEXT("DT_FishStat")))
		{
			return false;
		}
		T.Tuning = GetDefault<UFishSettings>()->RollTuning;
		FFishStatRow* WeightStat = Row<FFishStatRow>(T.Stats, TEXT("Weight"));
		if (!Test.TestNotNull(TEXT("DT_FishStat has a Weight row"), WeightStat))
		{
			return false;
		}
		WeightStat->Max = 5000.0f;
		const FString SpeciesJson = GeneratedSpeciesJson(Count);
		const double Start = FPlatformTime::Seconds();
		const bool bOk = ImportJsonChecked(Test, T.Species, FFishSpeciesRow::StaticStruct(), SpeciesJson, TEXT("generated species"));
		if (OutImportSeconds)
		{
			*OutImportSeconds = FPlatformTime::Seconds() - Start;
		}
		return bOk
			&& ImportJsonChecked(Test, T.Rarities, FFishRarityRow::StaticStruct(), Texts.Rarities, TEXT("DT_FishRarity"))
			&& ImportJsonChecked(Test, T.Modifiers, FFishModifierRow::StaticStruct(), Texts.Modifiers, TEXT("DT_FishModifier"));
	}

	static FFishRollContext Bite(int32 Habitat, int32 Seed)
	{
		FFishRollContext Context = Ctx(NAME_None, Seed);
		Context.HabitatTag = Tag(*FString::Printf(TEXT("Test.Fish.Habitat.H%d"), Habitat));
		Context.BaitTag = Tag(TEXT("Test.Fish.Bait.A"));
		return Context;
	}

	/** Seconds for Ops bite picks + rolls (best of 3) */
	static double TimePickAndRoll(const FFishTables& Tables, int32 Ops)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			const double Start = FPlatformTime::Seconds();
			for (int32 i = 0; i < Ops; ++i)
			{
				FFishRollContext Context = Bite(i % 10, i + Repeat * Ops);
				FName Picked;
				if (FFishRoll::PickSpecies(Tables, Context, Picked))
				{
					Context.SpeciesId = Picked;
					FFishInstance Fish;
					FFishRoll::Roll(Tables, Context, Fish);
				}
			}
			Best = FMath::Min(Best, FPlatformTime::Seconds() - Start);
		}
		return Best;
	}

	/** QA-104: 1,000 generated species import, validate, bite only where they live, cover the whole roster and roll their own weights */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAScaleCorrectTest, "Project.Fish.QA.Scale.ThousandSpeciesCorrect", FISH_QA_FLAGS)
	bool FFishQAScaleCorrectTest::RunTest(const FString& Parameters)
	{
		FTables T;
		if (!MakeScaleTables(*this, 1000, T))
		{
			return false;
		}
		TestEqual(TEXT("1000 species imported"), T.Species->GetRowMap().Num(), 1000);
		for (const FString& Problem : FFishDataValidator::Validate(T.Get()))
		{
			AddError(TEXT("generated data: ") + Problem);
		}

		TSet<FName> Seen;
		int32 WrongHabitat = 0, WrongWeight = 0, Failed = 0;
		for (int32 Habitat = 0; Habitat < 10; ++Habitat)
		{
			for (int32 Seed = 1; Seed <= 3000; ++Seed)
			{
				FFishRollContext Context = Bite(Habitat, Seed + Habitat * 100000);
				FName Picked;
				if (!FFishRoll::PickSpecies(T.Get(), Context, Picked))
				{
					++Failed;
					continue;
				}
				Seen.Add(Picked);
				const int32 Index = FCString::Atoi(*Picked.ToString().RightChop(4));
				if (Index % 10 != Habitat && ++WrongHabitat <= 3)
				{
					AddError(FString::Printf(TEXT("habitat H%d picked %s"), Habitat, *Picked.ToString()));
				}
				Context.SpeciesId = Picked;
				Context.bForceModifiers = true; // base weight
				FFishInstance Fish;
				if (!FFishRoll::Roll(T.Get(), Context, Fish) || !AllFinite(Fish))
				{
					++Failed;
					continue;
				}
				if ((Fish.WeightKg < Index + 1.0f || Fish.WeightKg > Index + 1.5f) && ++WrongWeight <= 3)
				{
					AddError(FString::Printf(TEXT("%s rolled %g kg, outside its own [%d, %g]"), *Picked.ToString(), Fish.WeightKg, Index + 1, Index + 1.5f));
				}
			}
		}
		TestEqual(TEXT("failed picks or rolls"), Failed, 0);
		TestEqual(TEXT("picks outside the spot's habitat"), WrongHabitat, 0);
		TestEqual(TEXT("weights outside the picked species' own range"), WrongWeight, 0);
		TestEqual(TEXT("every one of the 1000 species bit at least once"), Seen.Num(), 1000);
		return true;
	}

	/** QA-105: budgets at 1,000 species (Development editor, -nullrhi): import <= 5 s, validate <= 2 s, 10k pick+roll <= 2 s */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAScalePerfTest, "Project.Fish.QA.Scale.ThousandSpeciesPerformance", FISH_QA_FLAGS)
	bool FFishQAScalePerfTest::RunTest(const FString& Parameters)
	{
		double ImportBest = TNumericLimits<double>::Max();
		FTables T;
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			double Import = 0.0;
			if (!MakeScaleTables(*this, 1000, T, &Import))
			{
				return false;
			}
			ImportBest = FMath::Min(ImportBest, Import);
		}
		double ValidateBest = TNumericLimits<double>::Max();
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			const double Start = FPlatformTime::Seconds();
			FFishDataValidator::Validate(T.Get());
			ValidateBest = FMath::Min(ValidateBest, FPlatformTime::Seconds() - Start);
		}
		const double Ops = TimePickAndRoll(T.Get(), 10000);
		AddInfo(FString::Printf(TEXT("1000 species: import %.3f s, validate %.3f s, 10k pick+roll %.3f s (%.1f us/op)"), ImportBest, ValidateBest, Ops, Ops * 1e6 / 10000));
		TestTrue(FString::Printf(TEXT("import 1000 species <= 5 s (%.3f s)"), ImportBest), ImportBest <= 5.0);
		TestTrue(FString::Printf(TEXT("validate 1000 species <= 2 s (%.3f s)"), ValidateBest), ValidateBest <= 2.0);
		TestTrue(FString::Printf(TEXT("10k pick+roll <= 2 s (%.3f s)"), Ops), Ops <= 2.0);
		return true;
	}

	/** QA-106 (P2): pick+roll time per op grows at most ~linearly with the roster (1000 vs 100 species ratio <= 30) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAScaleLinearTest, "Project.Fish.QA.Scale.LinearNotQuadratic", FISH_QA_FLAGS)
	bool FFishQAScaleLinearTest::RunTest(const FString& Parameters)
	{
		FTables Small, Large;
		if (!MakeScaleTables(*this, 100, Small) || !MakeScaleTables(*this, 1000, Large))
		{
			return false;
		}
		const double SmallTime = TimePickAndRoll(Small.Get(), 10000);
		const double LargeTime = TimePickAndRoll(Large.Get(), 10000);
		const double Ratio = LargeTime / FMath::Max(SmallTime, 1e-6);
		AddInfo(FString::Printf(TEXT("10k pick+roll: 100 species %.3f s, 1000 species %.3f s, ratio %.2f"), SmallTime, LargeTime, Ratio));
		TestTrue(FString::Printf(TEXT("10x the species costs <= 30x the time (ratio %.2f)"), Ratio), Ratio <= 30.0);
		return true;
	}

	/** QA-107 (P2): 200 modifiers in 20 groups: 10k rolls <= 2 s and groups stay exclusive */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAScaleModifiersTest, "Project.Fish.QA.Scale.ManyModifiersPerformance", FISH_QA_FLAGS)
	bool FFishQAScaleModifiersTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->MaxModifiers = 200;
		for (int32 i = 0; i < 200; ++i)
		{
			T.Modifiers->AddRow(FName(*FString::Printf(TEXT("Trait_%d"), i)), MakeModifier(0.3f, FName(*FString::Printf(TEXT("Group_%d"), i % 20)), { AddMod(TEXT("Fish.Stat.Strength"), 0.5f) }));
		}
		double Best = TNumericLimits<double>::Max();
		int32 Violations = 0;
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			const double Start = FPlatformTime::Seconds();
			for (int32 Seed = 1; Seed <= 10000; ++Seed)
			{
				FFishInstance Fish;
				FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), Seed), Fish);
				if (Repeat == 0)
				{
					Violations += Fish.ModifierIds.Num() <= 20 ? 0 : 1;
				}
			}
			Best = FMath::Min(Best, FPlatformTime::Seconds() - Start);
		}
		AddInfo(FString::Printf(TEXT("200 modifiers: 10k rolls %.3f s"), Best));
		TestEqual(TEXT("never more than one modifier per group (<= 20)"), Violations, 0);
		TestTrue(FString::Printf(TEXT("10k rolls with 200 modifiers <= 2 s (%.3f s)"), Best), Best <= 2.0);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

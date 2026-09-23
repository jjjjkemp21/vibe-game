// QA-owned, independent tests for T-008 modifiers (stacking order, exclusivity, conditions, cap), stat-mod guards and
// clamping. Written by the qa-engineer from docs/specs/fish-system-rules.md and Fish/FishRoll.h (black-box).
// Fixture FX-ID: species QA_Fixed fixed at 2 kg = reference weight (weight factor exactly 1), Strength 10,
// Stamina 20, Speed 4, 10 coins/kg, one neutral tier. Numbers are exactly representable, so checks are exact.

#include "FishQATestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <algorithm>
#include <limits>

namespace FishQA_Mod
{
	using namespace FishQA;

	static FFishInstance RollId(const FTables& T, int32 Seed = 1, float Hours = 10.0f)
	{
		FFishRollContext Context = Ctx(TEXT("QA_Fixed"), Seed);
		Context.TimeOfDayHours = Hours;
		FFishInstance Fish;
		FFishRoll::Roll(T.Get(), Context, Fish);
		return Fish;
	}

	static float Str(const FFishInstance& Fish) { return StatOf(Fish, TEXT("Fish.Stat.Strength")); }
	static float Sta(const FFishInstance& Fish) { return StatOf(Fish, TEXT("Fish.Stat.Stamina")); }
	static float Spd(const FFishInstance& Fish) { return StatOf(Fish, TEXT("Fish.Stat.Speed")); }

	/** Frequency of each listed modifier over N seeds (and of "none of them") */
	static TMap<FName, int32> CountMods(const FTables& T, int32 N, int32& OutNone, int32& OutBoth, FName A, FName B)
	{
		TMap<FName, int32> Counts;
		OutNone = 0;
		OutBoth = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			const FFishInstance Fish = RollId(T, Seed);
			for (const FName& Id : Fish.ModifierIds)
			{
				Counts.FindOrAdd(Id)++;
			}
			OutNone += Fish.ModifierIds.Num() == 0 ? 1 : 0;
			OutBoth += (Fish.HasModifier(A) && Fish.HasModifier(B)) ? 1 : 0;
		}
		return Counts;
	}

	// ================================================================================================================
	// Stacking (QA-31..QA-36)
	// ================================================================================================================

	/** QA-31: Adds sum; other stats untouched */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModAddsTest, "Project.Fish.QA.Modifier.AddsSum", FISH_QA_FLAGS)
	bool FFishQAModAddsTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_A1"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 5.0f) }));
		T.Modifiers->AddRow(TEXT("QA_A2"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 3.0f) }));
		const FFishInstance Fish = RollId(T);
		TestEqual(TEXT("10 + 5 + 3"), Str(Fish), 18.0f);
		TestEqual(TEXT("Stamina untouched"), Sta(Fish), 20.0f);
		TestEqual(TEXT("Speed untouched"), Spd(Fish), 4.0f);
		return true;
	}

	/** QA-32: Multiplies compound as a product (x2 then x1.5 = x3, not x2.5) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModMultipliesTest, "Project.Fish.QA.Modifier.MultipliesCompound", FISH_QA_FLAGS)
	bool FFishQAModMultipliesTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_M1"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 2.0f) }));
		T.Modifiers->AddRow(TEXT("QA_M2"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 1.5f) }));
		TestEqual(TEXT("10 * 2 * 1.5"), Str(RollId(T)), 30.0f);
		return true;
	}

	/** QA-33: all Adds (of every kept modifier) before all Multiplies, whatever the row or entry order */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModOrderTest, "Project.Fish.QA.Modifier.AddsBeforeMultiplies", FISH_QA_FLAGS)
	bool FFishQAModOrderTest::RunTest(const FString& Parameters)
	{
		{
			FTables T;
			MakeIdentity(T);
			T.Modifiers->AddRow(TEXT("A_Mul"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 2.0f) }));
			T.Modifiers->AddRow(TEXT("B_Add"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 5.0f) }));
			TestEqual(TEXT("multiply row first by name: (10 + 5) * 2"), Str(RollId(T)), 30.0f);
		}
		{
			FTables T;
			MakeIdentity(T);
			T.Modifiers->AddRow(TEXT("B_Mul"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 2.0f) }));
			T.Modifiers->AddRow(TEXT("A_Add"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 5.0f) }));
			TestEqual(TEXT("add row first by name: (10 + 5) * 2"), Str(RollId(T)), 30.0f);
		}
		{
			FTables T;
			MakeIdentity(T);
			T.Modifiers->AddRow(TEXT("QA_Both"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 2.0f), AddMod(TEXT("Fish.Stat.Strength"), 5.0f) }));
			TestEqual(TEXT("one modifier listing [x2, +5]: (10 + 5) * 2"), Str(RollId(T)), 30.0f);
		}
		{
			// The worked example in FishRoll.h: Feisty {+5, x2}, Other {+3} -> (10 + 5 + 3) * 2 = 36, never 33
			FTables T;
			MakeIdentity(T);
			T.Modifiers->AddRow(TEXT("Feisty"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 5.0f), MulMod(TEXT("Fish.Stat.Strength"), 2.0f) }));
			T.Modifiers->AddRow(TEXT("Other"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 3.0f) }));
			TestEqual(TEXT("(10 + 5 + 3) * 2"), Str(RollId(T)), 36.0f);
		}
		return true;
	}

	/** QA-34: the table's row insertion order never changes the result (rows run in lexical name order) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModRowOrderTest, "Project.Fish.QA.Modifier.RowOrderInvariant", FISH_QA_FLAGS)
	bool FFishQAModRowOrderTest::RunTest(const FString& Parameters)
	{
		struct FSet { const TCHAR* Name; float A1, A2, M1, M2; };
		for (const FSet& Set : { FSet{ TEXT("representable"), 0.5f, 0.25f, 2.0f, 1.5f }, FSet{ TEXT("non-representable"), 0.1f, 0.7f, 1.1f, 1.3f } })
		{
			const TPair<FName, FFishModifierRow> Rows[] = {
				TPair<FName, FFishModifierRow>(FName(TEXT("QA_1")), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), Set.A1), MulMod(TEXT("Fish.Stat.Stamina"), Set.M2) }, 1.1f)),
				TPair<FName, FFishModifierRow>(FName(TEXT("QA_2")), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), Set.A2) }, 1.2f)),
				TPair<FName, FFishModifierRow>(FName(TEXT("QA_3")), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), Set.M1), AddMod(TEXT("Fish.Stat.Stamina"), Set.A1) })),
				TPair<FName, FFishModifierRow>(FName(TEXT("QA_4")), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), Set.M2), MulMod(TEXT("Fish.Stat.Speed"), Set.M1) }, 1.3f)),
			};
			int32 Order[] = { 0, 1, 2, 3 };
			FFishInstance Reference;
			bool bFirst = true;
			int32 Permutations = 0;
			int32 Mismatches = 0;
			do
			{
				FTables T;
				MakeIdentity(T);
				for (int32 i : Order)
				{
					T.Modifiers->AddRow(Rows[i].Key, Rows[i].Value);
				}
				const FFishInstance Fish = RollId(T, 9);
				if (bFirst)
				{
					Reference = Fish;
					bFirst = false;
				}
				Mismatches += Same(Fish, Reference) ? 0 : 1;
				++Permutations;
			}
			while (std::next_permutation(Order, Order + 4));
			TestEqual(TEXT("24 permutations tried"), Permutations, 24);
			TestEqual(FString::Printf(TEXT("%s: every insertion order gives the identical fish"), Set.Name), Mismatches, 0);
			const float Expected = (10.0f + Set.A1 + Set.A2) * Set.M1 * Set.M2;
			TestTrue(FString::Printf(TEXT("%s: Strength (10 + %g + %g) * %g * %g = %g (got %g)"), Set.Name, Set.A1, Set.A2, Set.M1, Set.M2, Expected, Str(Reference)), FMath::IsNearlyEqual(Str(Reference), Expected, 1e-4f));
		}
		return true;
	}

	/** QA-35: one modifier changing several stats changes each independently */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModMultiStatTest, "Project.Fish.QA.Modifier.MultipleStatsIndependent", FISH_QA_FLAGS)
	bool FFishQAModMultiStatTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_Multi"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 5.0f), MulMod(TEXT("Fish.Stat.Stamina"), 2.0f) }));
		const FFishInstance Fish = RollId(T);
		TestEqual(TEXT("Strength 10 + 5"), Str(Fish), 15.0f);
		TestEqual(TEXT("Stamina 20 * 2"), Sta(Fish), 40.0f);
		TestEqual(TEXT("Speed untouched"), Spd(Fish), 4.0f);
		TestEqual(TEXT("Weight untouched"), Fish.WeightKg, 2.0f);
		return true;
	}

	/** QA-36: a stat added as data (tag + DT_FishStat row) works from a modifier; a stat with no DT_FishStat row is skipped with a warning */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModNewStatTest, "Project.Fish.QA.Modifier.NewStatViaData", FISH_QA_FLAGS)
	bool FFishQAModNewStatTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_Extra"), MakeModifier(1.0f, NAME_None, { AddMod(QAStat, 7.0f) }));
		FFishInstance Fish = RollId(T);
		TestEqual(TEXT("QA stat (default 0) + 7"), StatOf(Fish, QAStat), 7.0f);

		FTables Mul;
		MakeIdentity(Mul);
		Mul.Modifiers->AddRow(TEXT("QA_ExtraMul"), MakeModifier(1.0f, NAME_None, { MulMod(QAStat, 3.0f) }));
		TestEqual(TEXT("QA stat (default 0) * 3 = 0"), StatOf(RollId(Mul), QAStat), 0.0f);

		// Fish.Stat.Glow is a registered tag but has no row in this fixture's stat table
		FLogCapture Log;
		FTables NoRow;
		MakeIdentity(NoRow);
		NoRow.Modifiers->AddRow(TEXT("QA_Glow"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Glow"), 5.0f), AddMod(TEXT("Fish.Stat.Strength"), 1.0f) }));
		Fish = RollId(NoRow);
		TestTrue(TEXT("roll still succeeds"), Fish.IsValid());
		TestFalse(TEXT("unregistered-in-DT_FishStat stat is not added"), Fish.HasStat(Tag(TEXT("Fish.Stat.Glow"))));
		TestEqual(TEXT("the valid mod of the same modifier still applies"), Str(Fish), 11.0f);
		TestTrue(TEXT("skipped mod warns"), Log.NumWarnings() > 0);
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	// ================================================================================================================
	// Chances, duplicates, exclusivity, conditions, cap (QA-37..QA-49)
	// ================================================================================================================

	/** QA-37: chance 0 never, chance 1 always */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModChanceEdgesTest, "Project.Fish.QA.Modifier.ChanceZeroNeverOneAlways", FISH_QA_FLAGS)
	bool FFishQAModChanceEdgesTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_Never"), MakeModifier(0.0f));
		T.Modifiers->AddRow(TEXT("QA_Always"), MakeModifier(1.0f));
		int32 None = 0, Both = 0;
		const TMap<FName, int32> Counts = CountMods(T, 10000, None, Both, TEXT("QA_Never"), TEXT("QA_Always"));
		TestEqual(TEXT("chance 0: never"), Counts.FindRef(TEXT("QA_Never")), 0);
		TestEqual(TEXT("chance 1: always"), Counts.FindRef(TEXT("QA_Always")), 10000);
		return true;
	}

	/** QA-38: independent chances roll at their odds and independently (100k rolls, 4.5 sigma) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModChanceOddsTest, "Project.Fish.QA.Modifier.ChanceMatchesOdds", FISH_QA_FLAGS)
	bool FFishQAModChanceOddsTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_Quarter"), MakeModifier(0.25f));
		T.Modifiers->AddRow(TEXT("QA_SixTenths"), MakeModifier(0.6f));
		const int32 N = 100000;
		int32 None = 0, Both = 0;
		const TMap<FName, int32> Counts = CountMods(T, N, None, Both, TEXT("QA_Quarter"), TEXT("QA_SixTenths"));
		const double Quarter = double(Counts.FindRef(TEXT("QA_Quarter"))) / N;
		const double Six = double(Counts.FindRef(TEXT("QA_SixTenths"))) / N;
		TestTrue(FString::Printf(TEXT("chance 0.25 observed %.4f"), Quarter), WithinSigma(Quarter, 0.25, N));
		TestTrue(FString::Printf(TEXT("chance 0.6 observed %.4f"), Six), WithinSigma(Six, 0.6, N));
		TestTrue(FString::Printf(TEXT("both together %.4f ~ 0.15 (independent)"), double(Both) / N), WithinSigma(double(Both) / N, 0.15, N));
		TestTrue(FString::Printf(TEXT("neither %.4f ~ 0.30"), double(None) / N), WithinSigma(double(None) / N, 0.30, N));
		return true;
	}

	/** QA-39: no duplicate modifier ids, ids in lexical order, unique stat tags */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModNoDuplicatesTest, "Project.Fish.QA.Modifier.NoDuplicates", FISH_QA_FLAGS)
	bool FFishQAModNoDuplicatesTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		for (const TCHAR* Name : { TEXT("QA_F"), TEXT("QA_B"), TEXT("QA_D"), TEXT("QA_A"), TEXT("QA_E"), TEXT("QA_C") })
		{
			T.Modifiers->AddRow(Name, MakeModifier(0.7f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 1.0f) }));
		}
		int32 Bad = 0;
		for (int32 Seed = 1; Seed <= 20000; ++Seed)
		{
			const FFishInstance Fish = RollId(T, Seed);
			TSet<FName> Unique(Fish.ModifierIds);
			bool bSorted = true;
			for (int32 i = 1; i < Fish.ModifierIds.Num(); ++i)
			{
				bSorted &= Fish.ModifierIds[i - 1].LexicalLess(Fish.ModifierIds[i]);
			}
			TSet<FGameplayTag> Tags;
			for (const FFishStatValue& Stat : Fish.Stats)
			{
				Tags.Add(Stat.Tag);
			}
			const bool bOk = Unique.Num() == Fish.ModifierIds.Num() && bSorted && Tags.Num() == Fish.Stats.Num()
				&& Str(Fish) == 10.0f + Fish.ModifierIds.Num();
			if (!bOk && ++Bad <= 3)
			{
				AddError(FString::Printf(TEXT("seed %d: %s"), Seed, *Describe(Fish)));
			}
		}
		TestEqual(TEXT("catches with duplicates, unsorted ids, duplicate stats or a wrong stack"), Bad, 0);
		return true;
	}

	/** QA-40: several chance-1 modifiers in one group: exactly one is kept, picked weighted by RollChance (equal -> 1/3 each) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModGroupOneTest, "Project.Fish.QA.Modifier.ExclusiveGroupAtMostOne", FISH_QA_FLAGS)
	bool FFishQAModGroupOneTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_GA"), MakeModifier(1.0f, TEXT("G")));
		T.Modifiers->AddRow(TEXT("QA_GB"), MakeModifier(1.0f, TEXT("G")));
		T.Modifiers->AddRow(TEXT("QA_GC"), MakeModifier(1.0f, TEXT("G")));
		const int32 N = 60000;
		TMap<FName, int32> Counts;
		int32 NotOne = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			const FFishInstance Fish = RollId(T, Seed);
			NotOne += Fish.ModifierIds.Num() == 1 ? 0 : 1;
			for (const FName& Id : Fish.ModifierIds)
			{
				Counts.FindOrAdd(Id)++;
			}
			if (Seed <= 200)
			{
				TestTrue(TEXT("the winner is deterministic per seed"), Same(Fish, RollId(T, Seed)));
			}
		}
		TestEqual(TEXT("exactly one group member per catch"), NotOne, 0);
		CheckOdds(*this, TEXT("equal chances in one group"), { TEXT("QA_GA"), TEXT("QA_GB"), TEXT("QA_GC") },
			{ Counts.FindRef(TEXT("QA_GA")), Counts.FindRef(TEXT("QA_GB")), Counts.FindRef(TEXT("QA_GC")) }, { 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 }, N);
		return true;
	}

	/** QA-41: partial chances in one group: never both; odds follow "independent hits, then a pick weighted by RollChance" */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModGroupPartialTest, "Project.Fish.QA.Modifier.ExclusiveGroupPartialChance", FISH_QA_FLAGS)
	bool FFishQAModGroupPartialTest::RunTest(const FString& Parameters)
	{
		struct FCase { float A, B; };
		for (const FCase& Case : { FCase{ 0.5f, 0.5f }, FCase{ 0.8f, 0.2f } })
		{
			FTables T;
			MakeIdentity(T);
			T.Modifiers->AddRow(TEXT("QA_GA"), MakeModifier(Case.A, TEXT("G")));
			T.Modifiers->AddRow(TEXT("QA_GB"), MakeModifier(Case.B, TEXT("G")));
			const int32 N = 100000;
			int32 None = 0, Both = 0;
			const TMap<FName, int32> Counts = CountMods(T, N, None, Both, TEXT("QA_GA"), TEXT("QA_GB"));
			TestEqual(FString::Printf(TEXT("{%g,%g}: never both"), Case.A, Case.B), Both, 0);
			const double A = Case.A, B = Case.B;
			const double PBoth = A * B;
			const double PA = A * (1.0 - B) + PBoth * A / (A + B);
			const double PB = B * (1.0 - A) + PBoth * B / (A + B);
			const double PNone = (1.0 - A) * (1.0 - B);
			CheckOdds(*this, FString::Printf(TEXT("group {%g,%g}"), Case.A, Case.B), { TEXT("A"), TEXT("B"), TEXT("none") },
				{ Counts.FindRef(TEXT("QA_GA")), Counts.FindRef(TEXT("QA_GB")), None }, { PA, PB, PNone }, N);
		}
		return true;
	}

	/** QA-42: modifiers in different groups coexist */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModGroupsCoexistTest, "Project.Fish.QA.Modifier.DifferentGroupsCoexist", FISH_QA_FLAGS)
	bool FFishQAModGroupsCoexistTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_G1"), MakeModifier(1.0f, TEXT("G1")));
		T.Modifiers->AddRow(TEXT("QA_G2"), MakeModifier(1.0f, TEXT("G2")));
		int32 None = 0, Both = 0;
		CountMods(T, 5000, None, Both, TEXT("QA_G1"), TEXT("QA_G2"));
		TestEqual(TEXT("both in every catch"), Both, 5000);
		return true;
	}

	/** QA-43: modifiers without a group (None) are not exclusive with each other */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModUngroupedTest, "Project.Fish.QA.Modifier.UngroupedNotExclusive", FISH_QA_FLAGS)
	bool FFishQAModUngroupedTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_U1"), MakeModifier(1.0f, NAME_None));
		T.Modifiers->AddRow(TEXT("QA_U2"), MakeModifier(1.0f, FName(TEXT("None"))));
		int32 None = 0, Both = 0;
		CountMods(T, 5000, None, Both, TEXT("QA_U1"), TEXT("QA_U2"));
		TestEqual(TEXT("both in every catch"), Both, 5000);
		return true;
	}

	/** QA-44: an ineligible group member never blocks an eligible one */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModIneligibleGroupTest, "Project.Fish.QA.Modifier.IneligibleDoesNotBlockGroup", FISH_QA_FLAGS)
	bool FFishQAModIneligibleGroupTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishModifierRow Elsewhere = MakeModifier(1.0f, TEXT("G"));
		Elsewhere.SpeciesIds = { TEXT("QA_OtherSpecies") };
		T.Modifiers->AddRow(TEXT("QA_A_Elsewhere"), Elsewhere);
		T.Modifiers->AddRow(TEXT("QA_B_Here"), MakeModifier(1.0f, TEXT("G")));
		int32 None = 0, Both = 0;
		const TMap<FName, int32> Counts = CountMods(T, 5000, None, Both, TEXT("QA_A_Elsewhere"), TEXT("QA_B_Here"));
		TestEqual(TEXT("ineligible member never"), Counts.FindRef(TEXT("QA_A_Elsewhere")), 0);
		TestEqual(TEXT("eligible member always"), Counts.FindRef(TEXT("QA_B_Here")), 5000);
		return true;
	}

	/** QA-45: species conditions (ids and family tags, hierarchical) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModSpeciesConditionTest, "Project.Fish.QA.Modifier.SpeciesConditionRespected", FISH_QA_FLAGS)
	bool FFishQAModSpeciesConditionTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishSpeciesRow Sub = MakeSpecies();
		Sub.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F1.Sub")) };
		T.Species->AddRow(TEXT("QA_Sub"), Sub);
		FFishSpeciesRow Parent = MakeSpecies();
		Parent.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F1")) };
		T.Species->AddRow(TEXT("QA_Parent"), Parent);
		FFishSpeciesRow Other = MakeSpecies();
		Other.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F2")) };
		T.Species->AddRow(TEXT("QA_Other"), Other);

		FFishModifierRow ById = MakeModifier(1.0f);
		ById.SpeciesIds = { TEXT("QA_Sub") };
		T.Modifiers->AddRow(TEXT("QA_ById"), ById);
		FFishModifierRow ByFamily = MakeModifier(1.0f);
		ByFamily.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F1")) };
		T.Modifiers->AddRow(TEXT("QA_ByFamily"), ByFamily);
		FFishModifierRow BySubFamily = MakeModifier(1.0f);
		BySubFamily.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F1.Sub")) };
		T.Modifiers->AddRow(TEXT("QA_BySubFamily"), BySubFamily);

		auto Mods = [&T](FName SpeciesId)
		{
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), Ctx(SpeciesId, 1), Fish);
			return Fish;
		};
		const FFishInstance SubFish = Mods(TEXT("QA_Sub"));
		const FFishInstance ParentFish = Mods(TEXT("QA_Parent"));
		const FFishInstance OtherFish = Mods(TEXT("QA_Other"));
		TestTrue(TEXT("id condition: QA_Sub gets it"), SubFish.HasModifier(TEXT("QA_ById")));
		TestFalse(TEXT("id condition: QA_Parent doesn't"), ParentFish.HasModifier(TEXT("QA_ById")));
		TestTrue(TEXT("family F1 matches a species tagged F1.Sub (hierarchical)"), SubFish.HasModifier(TEXT("QA_ByFamily")));
		TestTrue(TEXT("family F1 matches a species tagged F1"), ParentFish.HasModifier(TEXT("QA_ByFamily")));
		TestFalse(TEXT("family F1 doesn't match F2"), OtherFish.HasModifier(TEXT("QA_ByFamily")));
		TestTrue(TEXT("family F1.Sub matches F1.Sub"), SubFish.HasModifier(TEXT("QA_BySubFamily")));
		TestFalse(TEXT("family F1.Sub does NOT match a species tagged only F1 (the condition is more specific)"), ParentFish.HasModifier(TEXT("QA_BySubFamily")));
		return true;
	}

	/** QA-46: region, time and weather conditions (hierarchical tags, half-open wrapping windows) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModContextConditionsTest, "Project.Fish.QA.Modifier.RegionTimeWeatherConditions", FISH_QA_FLAGS)
	bool FFishQAModContextConditionsTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishModifierRow Night = MakeModifier(1.0f);
		Night.TimeWindows = { FFishTimeWindow() };
		Night.TimeWindows[0].StartHour = 20.0f;
		Night.TimeWindows[0].EndHour = 4.0f;
		T.Modifiers->AddRow(TEXT("QA_Night"), Night);
		FFishModifierRow Region = MakeModifier(1.0f);
		Region.RegionTags = { Tag(TEXT("Test.Fish.Region.R1")) };
		T.Modifiers->AddRow(TEXT("QA_Region"), Region);
		FFishModifierRow Rain = MakeModifier(1.0f);
		Rain.WeatherTags = { Tag(TEXT("Test.Fish.Weather.Rain")) };
		T.Modifiers->AddRow(TEXT("QA_Rain"), Rain);
		const FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));

		auto Has = [&T](FName Modifier, float Hours, const TCHAR* RegionTag, const TCHAR* Weather)
		{
			FFishRollContext Context = Ctx(TEXT("QA_Fixed"), 1);
			Context.TimeOfDayHours = Hours;
			Context.RegionTag = *RegionTag ? Tag(RegionTag) : FGameplayTag();
			Context.WeatherTag = *Weather ? Tag(Weather) : FGameplayTag();
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), Context, Fish);
			return Fish.HasModifier(Modifier);
		};
		struct FTime { float Hours; bool bIn; };
		for (const FTime& Time : { FTime{ 20.0f, true }, FTime{ 23.99f, true }, FTime{ 0.0f, true }, FTime{ 3.99f, true }, FTime{ 4.0f, false },
			FTime{ 12.0f, false }, FTime{ 19.99f, false }, FTime{ 24.0f, true }, FTime{ -2.0f, true }, FTime{ 44.0f, true } })
		{
			TestEqual(FString::Printf(TEXT("night window [20, 4) at %g"), Time.Hours), Has(TEXT("QA_Night"), Time.Hours, TEXT("Test.Fish.Region.R1"), TEXT("")), Time.bIn);
		}
		TestTrue(TEXT("region R1 matches context R1.Sub"), Has(TEXT("QA_Region"), 10.0f, TEXT("Test.Fish.Region.R1.Sub"), TEXT("")));
		TestTrue(TEXT("region R1 matches context R1"), Has(TEXT("QA_Region"), 10.0f, TEXT("Test.Fish.Region.R1"), TEXT("")));
		TestFalse(TEXT("region R1 doesn't match R2"), Has(TEXT("QA_Region"), 10.0f, TEXT("Test.Fish.Region.R2"), TEXT("")));
		TestFalse(TEXT("region R1 doesn't match no region"), Has(TEXT("QA_Region"), 10.0f, TEXT(""), TEXT("")));
		TestTrue(TEXT("weather Rain matches Rain"), Has(TEXT("QA_Rain"), 10.0f, TEXT(""), TEXT("Test.Fish.Weather.Rain")));
		TestFalse(TEXT("weather Rain doesn't match Sun"), Has(TEXT("QA_Rain"), 10.0f, TEXT(""), TEXT("Test.Fish.Weather.Sun")));
		TestFalse(TEXT("weather Rain doesn't match no weather"), Has(TEXT("QA_Rain"), 10.0f, TEXT(""), TEXT("")));

		// IsModifierEligible agrees with the documented rules (oracle) on a grid
		int32 Disagreements = 0;
		for (const FName& Id : SortedRowNames<FFishModifierRow>(T.Modifiers.Get()))
		{
			const FFishModifierRow* Modifier = Row<FFishModifierRow>(T.Modifiers, Id);
			for (float Hours = -1.0f; Hours <= 25.0f; Hours += 0.5f)
			{
				for (const TCHAR* RegionTag : { TEXT("Test.Fish.Region.R1.Sub"), TEXT("Test.Fish.Region.R2"), TEXT("") })
				{
					for (const TCHAR* Weather : { TEXT("Test.Fish.Weather.Rain"), TEXT("Test.Fish.Weather.Sun"), TEXT("") })
					{
						FFishRollContext Context = Ctx(TEXT("QA_Fixed"), 1);
						Context.TimeOfDayHours = Hours;
						Context.RegionTag = *RegionTag ? Tag(RegionTag) : FGameplayTag();
						Context.WeatherTag = *Weather ? Tag(Weather) : FGameplayTag();
						const bool bActual = FFishRoll::IsModifierEligible(Id, *Modifier, TEXT("QA_Fixed"), *Species, Context);
						const bool bExpected = OracleModifierEligible(Id, *Modifier, TEXT("QA_Fixed"), *Species, Context);
						if (bActual != bExpected && ++Disagreements <= 3)
						{
							AddError(FString::Printf(TEXT("IsModifierEligible(%s) at %g, %s, %s = %d, spec says %d"), *Id.ToString(), Hours, RegionTag, Weather, bActual, bExpected));
						}
					}
				}
			}
		}
		TestEqual(TEXT("IsModifierEligible disagreements with the spec"), Disagreements, 0);
		return true;
	}

	/** QA-47: the species allow-list and the modifier's own conditions both apply */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModAllowListTest, "Project.Fish.QA.Modifier.SpeciesAllowListIntersects", FISH_QA_FLAGS)
	bool FFishQAModAllowListTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_A"), MakeModifier(1.0f));
		T.Modifiers->AddRow(TEXT("QA_B"), MakeModifier(1.0f));
		Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->AllowedModifiers = { TEXT("QA_A") };
		int32 None = 0, Both = 0;
		const TMap<FName, int32> Counts = CountMods(T, 2000, None, Both, TEXT("QA_A"), TEXT("QA_B"));
		TestEqual(TEXT("allowed A always"), Counts.FindRef(TEXT("QA_A")), 2000);
		TestEqual(TEXT("not allowed B never"), Counts.FindRef(TEXT("QA_B")), 0);
		return true;
	}

	/** QA-48: MaxModifiers keeps the first N hits in lexical (natural-number) order; 0 = none */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModCapTest, "Project.Fish.QA.Modifier.CountCapRespected", FISH_QA_FLAGS)
	bool FFishQAModCapTest::RunTest(const FString& Parameters)
	{
		{
			FTables T;
			MakeIdentity(T);
			for (int32 i = 10; i >= 1; --i)
			{
				T.Modifiers->AddRow(FName(*FString::Printf(TEXT("QA_M%02d"), i)), MakeModifier(1.0f));
			}
			Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->MaxModifiers = 3;
			const TArray<FName> Expected = { TEXT("QA_M01"), TEXT("QA_M02"), TEXT("QA_M03") };
			int32 Bad = 0;
			for (int32 Seed = 1; Seed <= 500; ++Seed)
			{
				Bad += RollId(T, Seed).ModifierIds == Expected ? 0 : 1;
			}
			TestEqual(TEXT("cap 3 keeps QA_M01..QA_M03"), Bad, 0);
		}
		{
			FTables T;
			MakeIdentity(T);
			for (const TCHAR* Name : { TEXT("Mod_10"), TEXT("Mod_2"), TEXT("Mod_1") })
			{
				T.Modifiers->AddRow(Name, MakeModifier(1.0f));
			}
			Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->MaxModifiers = 2;
			const FFishInstance Fish = RollId(T);
			const TArray<FName> Expected = { TEXT("Mod_1"), TEXT("Mod_2") };
			TestTrue(FString::Printf(TEXT("natural order Mod_1 < Mod_2 < Mod_10 (got [%s])"), *ModsKey(Fish)), Fish.ModifierIds == Expected);
			Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->MaxModifiers = 0;
			TestEqual(TEXT("MaxModifiers 0 = no modifiers"), RollId(T).ModifierIds.Num(), 0);
		}
		return true;
	}

	/** QA-49: the rarity step (its Adds, then Multiplies) is finished before any modifier */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAModRarityFirstTest, "Project.Fish.QA.Modifier.RarityAppliesBeforeModifiers", FISH_QA_FLAGS)
	bool FFishQAModRarityFirstTest::RunTest(const FString& Parameters)
	{
		{
			FTables T;
			MakeIdentity(T);
			Row<FFishRarityRow>(T.Rarities, TEXT("QA_Neutral"))->StatMods = { MulMod(TEXT("Fish.Stat.Strength"), 2.0f) };
			T.Modifiers->AddRow(TEXT("QA_Add"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 5.0f) }));
			TestEqual(TEXT("rarity x2 then modifier +5: 10 * 2 + 5"), Str(RollId(T)), 25.0f);
		}
		{
			FTables T;
			MakeIdentity(T);
			Row<FFishRarityRow>(T.Rarities, TEXT("QA_Neutral"))->StatMods = { AddMod(TEXT("Fish.Stat.Strength"), 4.0f) };
			T.Modifiers->AddRow(TEXT("QA_Mul"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 2.0f) }));
			TestEqual(TEXT("rarity +4 then modifier x2: (10 + 4) * 2"), Str(RollId(T)), 28.0f);
		}
		{
			FTables T;
			MakeIdentity(T);
			Row<FFishRarityRow>(T.Rarities, TEXT("QA_Neutral"))->StatMods = { MulMod(TEXT("Fish.Stat.Strength"), 3.0f), AddMod(TEXT("Fish.Stat.Strength"), 2.0f) };
			TestEqual(TEXT("rarity [x3, +2] alone: (10 + 2) * 3"), Str(RollId(T)), 36.0f);
		}
		return true;
	}

	// ================================================================================================================
	// Guards (QA-50..QA-54) and clamping (QA-55, QA-56)
	// ================================================================================================================

	static void CheckSkipped(FAutomationTestBase& Test, const TCHAR* What, const FFishStatMod& Bad)
	{
		FLogCapture Log;
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_Bad"), MakeModifier(1.0f, NAME_None, { Bad, AddMod(TEXT("Fish.Stat.Stamina"), 1.0f) }));
		const FFishInstance Fish = RollId(T);
		Test.TestTrue(FString::Printf(TEXT("%s: the roll still succeeds"), What), Fish.IsValid());
		Test.TestEqual(FString::Printf(TEXT("%s: the bad mod is skipped (Strength stays 10)"), What), Str(Fish), 10.0f);
		Test.TestEqual(FString::Printf(TEXT("%s: the other mod still applies"), What), Sta(Fish), 21.0f);
		Test.TestTrue(FString::Printf(TEXT("%s: all finite"), What), AllFinite(Fish));
		Test.TestTrue(FString::Printf(TEXT("%s: value >= 1"), What), Fish.Value >= 1);
		Test.TestTrue(FString::Printf(TEXT("%s: warns"), What), Log.NumWarnings() > 0);
		Test.TestEqual(FString::Printf(TEXT("%s: no errors"), What), Log.NumErrors(), 0);
	}

	/** QA-50: Multiply by 0 is skipped with a warning */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAGuardZeroTest, "Project.Fish.QA.Guard.MultiplyByZeroSkipped", FISH_QA_FLAGS)
	bool FFishQAGuardZeroTest::RunTest(const FString& Parameters)
	{
		CheckSkipped(*this, TEXT("x0"), MulMod(TEXT("Fish.Stat.Strength"), 0.0f));
		return true;
	}

	/** QA-51: a negative Multiply is skipped with a warning */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAGuardNegativeTest, "Project.Fish.QA.Guard.NegativeMultiplySkipped", FISH_QA_FLAGS)
	bool FFishQAGuardNegativeTest::RunTest(const FString& Parameters)
	{
		CheckSkipped(*this, TEXT("x-1"), MulMod(TEXT("Fish.Stat.Strength"), -1.0f));
		return true;
	}

	/** QA-54a: non-finite mod values are skipped with a warning */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAGuardNonFiniteModTest, "Project.Fish.QA.Guard.NonFiniteModSkipped", FISH_QA_FLAGS)
	bool FFishQAGuardNonFiniteModTest::RunTest(const FString& Parameters)
	{
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();
		CheckSkipped(*this, TEXT("+NaN"), AddMod(TEXT("Fish.Stat.Strength"), NaN));
		CheckSkipped(*this, TEXT("xNaN"), MulMod(TEXT("Fish.Stat.Strength"), NaN));
		CheckSkipped(*this, TEXT("+Inf"), AddMod(TEXT("Fish.Stat.Strength"), Inf));
		CheckSkipped(*this, TEXT("xInf"), MulMod(TEXT("Fish.Stat.Strength"), Inf));
		return true;
	}

	/** QA-52: a large negative Add clamps to the stat's Min */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAGuardNegativeAddTest, "Project.Fish.QA.Guard.LargeNegativeAddClampsToFloor", FISH_QA_FLAGS)
	bool FFishQAGuardNegativeAddTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_Drain"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), -1000.0f) }));
		const FFishInstance Fish = RollId(T);
		TestEqual(TEXT("10 - 1000 clamps to Min 0"), Str(Fish), 0.0f);
		TestTrue(TEXT("value >= 1"), Fish.Value >= 1);
		TestTrue(TEXT("difficulty rating finite and >= 0"), FMath::IsFinite(Fish.DifficultyRating) && Fish.DifficultyRating >= 0.0f);
		return true;
	}

	/** QA-53: huge Multiplies clamp to the stat's Max; weight clamps to the Weight stat; value stays finite and >= 1 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAGuardHugeTest, "Project.Fish.QA.Guard.HugeMultiplyStaysFinite", FISH_QA_FLAGS)
	bool FFishQAGuardHugeTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		T.Modifiers->AddRow(TEXT("QA_Huge1"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 1e30f), MulMod(TEXT("Fish.Stat.Weight"), 1e30f) }));
		T.Modifiers->AddRow(TEXT("QA_Huge2"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 1e30f), MulMod(TEXT("Fish.Stat.Weight"), 1e30f) }));
		const FFishInstance Fish = RollId(T);
		TestTrue(TEXT("all finite"), AllFinite(Fish));
		TestEqual(TEXT("Strength clamps to Max 1000"), Str(Fish), 1000.0f);
		TestEqual(TEXT("Weight clamps to the Weight stat Max 10000"), Fish.WeightKg, 10000.0f);
		TestEqual(TEXT("value = 10 coins/kg * 10000 kg"), Fish.Value, 100000);
		return true;
	}

	/** QA-54b: non-finite or nonsense numbers in species/rarity rows never leak NaN/Inf or a value < 1 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAGuardNonFiniteRowsTest, "Project.Fish.QA.Guard.NonFiniteRowValues", FISH_QA_FLAGS)
	bool FFishQAGuardNonFiniteRowsTest::RunTest(const FString& Parameters)
	{
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		FLogCapture Log;
		struct FCase { const TCHAR* Name; TFunction<void(FTables&)> Break; };
		const TArray<FCase> Cases = {
			{ TEXT("NaN base stat"), [NaN](FTables& T) { Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->BaseStats[0].Value = NaN; } },
			{ TEXT("NaN BaseValuePerKg"), [NaN](FTables& T) { Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->BaseValuePerKg = NaN; } },
			{ TEXT("NaN rarity ValueMultiplier"), [NaN](FTables& T) { Row<FFishRarityRow>(T.Rarities, TEXT("QA_Neutral"))->ValueMultiplier = NaN; } },
			{ TEXT("NaN rarity XpMultiplier"), [NaN](FTables& T) { Row<FFishRarityRow>(T.Rarities, TEXT("QA_Neutral"))->XpMultiplier = NaN; } },
			{ TEXT("NaN modifier ValueMultiplier"), [NaN](FTables& T) { T.Modifiers->AddRow(TEXT("QA_NaNValue"), MakeModifier(1.0f, NAME_None, {}, NaN)); } },
			{ TEXT("NaN WeightStatExponent"), [NaN](FTables& T) { Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->WeightStatExponent = NaN; } },
			{ TEXT("NaN SizeSkew"), [NaN](FTables& T) { FFishSpeciesRow* S = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed")); S->WeightMax = 4.0f; S->SizeSkew = NaN; } },
			{ TEXT("ReferenceWeight 0"), [](FTables& T) { Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->ReferenceWeight = 0.0f; } },
			{ TEXT("ReferenceWeight -1"), [](FTables& T) { Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->ReferenceWeight = -1.0f; } },
			{ TEXT("NaN stat Min/Max"), [NaN](FTables& T) { FFishStatRow* S = T.Stats->FindRow<FFishStatRow>(TEXT("Strength"), TEXT("QA")); S->Min = NaN; S->Max = NaN; } },
		};
		for (const FCase& Case : Cases)
		{
			FTables T;
			MakeIdentity(T);
			Case.Break(T);
			for (int32 Seed = 1; Seed <= 50; ++Seed)
			{
				FFishInstance Fish;
				const bool bOk = FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), Seed), Fish);
				if (!(AllFinite(Fish) && (!bOk || (Fish.Value >= 1 && Fish.Xp >= 0 && Fish.WeightKg > 0.0f))))
				{
					AddError(FString::Printf(TEXT("%s: seed %d gave %s"), Case.Name, Seed, *Describe(Fish)));
					break;
				}
			}
		}
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** New (P2): a value past int32 range saturates instead of wrapping negative */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAGuardValueOverflowTest, "Project.Fish.QA.Guard.ValueOverflowSaturates", FISH_QA_FLAGS)
	bool FFishQAGuardValueOverflowTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		Species->WeightMin = 1000.0f;
		Species->WeightMax = 1000.0f;
		Species->ReferenceWeight = 1000.0f;
		Species->BaseValuePerKg = 1e9f;
		Row<FFishRarityRow>(T.Rarities, TEXT("QA_Neutral"))->ValueMultiplier = 12.0f;
		const FFishInstance Fish = RollId(T);
		AddInfo(FString::Printf(TEXT("1e9 coins/kg * 1000 kg * 12 -> Value %d"), Fish.Value));
		TestTrue(FString::Printf(TEXT("value never wraps negative or to 0 (got %d)"), Fish.Value), Fish.Value >= 1);
		TestEqual(TEXT("value saturates at INT32_MAX"), Fish.Value, std::numeric_limits<int32>::max());
		return true;
	}

	/** QA-55: clamping happens once, after all mods (no clamp between Adds and Multiplies or after the rarity step) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAClampRangesTest, "Project.Fish.QA.Clamp.StatRanges", FISH_QA_FLAGS)
	bool FFishQAClampRangesTest::RunTest(const FString& Parameters)
	{
		auto Make = [](FTables& T)
		{
			MakeIdentity(T);
			FFishStatRow* Strength = T.Stats->FindRow<FFishStatRow>(TEXT("Strength"), TEXT("QA"));
			Strength->Min = 5.0f;
			Strength->Max = 50.0f;
			Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->BaseStats[0].Value = 40.0f;
		};
		{
			FTables T;
			Make(T);
			T.Modifiers->AddRow(TEXT("QA_Up"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 20.0f) }));
			T.Modifiers->AddRow(TEXT("QA_Half"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 0.5f) }));
			TestEqual(TEXT("(40 + 20) * 0.5 = 30 (no clamp to 50 between Adds and Multiplies)"), Str(RollId(T)), 30.0f);
		}
		{
			FTables T;
			Make(T);
			Row<FFishRarityRow>(T.Rarities, TEXT("QA_Neutral"))->StatMods = { AddMod(TEXT("Fish.Stat.Strength"), 20.0f) };
			T.Modifiers->AddRow(TEXT("QA_Half"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 0.5f) }));
			TestEqual(TEXT("rarity +20 (60), modifier x0.5 = 30 (no clamp after the rarity step)"), Str(RollId(T)), 30.0f);
		}
		{
			FTables T;
			Make(T);
			T.Modifiers->AddRow(TEXT("QA_Up"), MakeModifier(1.0f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 100.0f) }));
			TestEqual(TEXT("140 clamps to Max 50"), Str(RollId(T)), 50.0f);
		}
		{
			FTables T;
			Make(T);
			T.Modifiers->AddRow(TEXT("QA_Down"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 0.01f) }));
			TestEqual(TEXT("0.4 clamps to Min 5"), Str(RollId(T)), 5.0f);
		}
		{
			FTables T;
			Make(T);
			Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->BaseStats[0].Value = 1.0f;
			TestEqual(TEXT("a base below Min clamps up to 5"), Str(RollId(T)), 5.0f);
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

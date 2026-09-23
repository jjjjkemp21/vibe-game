// QA-owned, independent tests for the T-008 roll pipeline: determinism, seeds, stage isolation, the documented
// formula (independent oracle), rarity odds and luck, and the weight roll. Written by the qa-engineer from
// docs/specs/fish-system-rules.md and the contract comments in Fish/FishRoll.h (black-box).
// Statistical method (test design section 3): fixed sequential seeds (deterministic, never flaky), 4.5 sigma per
// category plus chi-square at alpha 1e-4, hard zero checks for impossible outcomes.

#include "FishQATestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include <limits>

namespace FishQA_Roll
{
	using namespace FishQA;

	/** Rarity rows R0..Rn-1 with the given roll weights, Rank = index, value multiplier 1 + index */
	static void SetRarities(FTables& T, const TArray<float>& Weights)
	{
		ResetRarities(T);
		for (int32 i = 0; i < Weights.Num(); ++i)
		{
			T.Rarities->AddRow(FName(*FString::Printf(TEXT("R%d"), i)), MakeRarity(i, Weights[i], 1.0f + i));
		}
	}

	/** Rolls N sequential seeds (1..N) and counts rarity ids; modifiers are forced empty (rarity doesn't depend on them) */
	static TMap<FName, int32> CountRarities(const FFishTables& Tables, FName SpeciesId, int32 N, float Luck, int32& OutFailures)
	{
		TMap<FName, int32> Counts;
		OutFailures = 0;
		FFishRollContext Context = Ctx(SpeciesId, 0);
		Context.Luck = Luck;
		Context.bForceModifiers = true;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			Context.Seed = Seed;
			FFishInstance Fish;
			if (FFishRoll::Roll(Tables, Context, Fish))
			{
				Counts.FindOrAdd(Fish.RarityId)++;
			}
			else
			{
				++OutFailures;
			}
		}
		return Counts;
	}

	/** Expected p per rarity row of Tables for Species at Luck, from the documented formula (not from the code) */
	static void ExpectedRarityOdds(const FFishTables& Tables, const FFishSpeciesRow& Species, float Luck, TArray<FName>& OutIds, TArray<double>& OutProbs)
	{
		OutIds.Reset();
		OutProbs.Reset();
		const float L = FMath::IsNaN(Luck) ? 0.0f : FMath::Clamp(Luck, 0.0f, Tables.Tuning.MaxLuck);
		double Total = 0.0;
		TArray<double> Weights;
		Tables.Rarities->ForeachRow<FFishRarityRow>(TEXT("QA"), [&](const FName& Id, const FFishRarityRow& Rarity)
		{
			const bool bAllowed = Species.AllowedRarities.Num() == 0 || Species.AllowedRarities.Contains(Id);
			const double W = (bAllowed && FMath::IsFinite(Rarity.RollWeight) && Rarity.RollWeight > 0.0f)
				? double(Rarity.RollWeight) * (1.0 + double(L) * Rarity.Rank * Tables.Tuning.LuckRankFactor) : 0.0;
			OutIds.Add(Id);
			Weights.Add(W);
			Total += W;
		});
		for (double W : Weights)
		{
			OutProbs.Add(Total > 0.0 ? W / Total : 0.0);
		}
	}

	static bool CheckRarityOdds(FAutomationTestBase& Test, const FString& What, const FFishTables& Tables, FName SpeciesId, int32 N, float Luck)
	{
		const FFishSpeciesRow* Species = Tables.Species->FindRow<FFishSpeciesRow>(SpeciesId, TEXT("QA"), false);
		if (!Species)
		{
			Test.AddError(What + TEXT(": species missing"));
			return false;
		}
		TArray<FName> Ids;
		TArray<double> Probs;
		ExpectedRarityOdds(Tables, *Species, Luck, Ids, Probs);
		int32 Failures = 0;
		const TMap<FName, int32> Counts = CountRarities(Tables, SpeciesId, N, Luck, Failures);
		Test.TestEqual(What + TEXT(": every roll succeeds"), Failures, 0);
		TArray<FString> Names;
		TArray<int32> Observed;
		int32 Accounted = 0;
		for (const FName& Id : Ids)
		{
			Names.Add(Id.ToString());
			Observed.Add(Counts.FindRef(Id));
			Accounted += Counts.FindRef(Id);
		}
		Test.TestEqual(What + TEXT(": every roll is one of the table's tiers"), Accounted, N - Failures);
		return CheckOdds(Test, What, Names, Observed, Probs, N);
	}

	static int32 RankOf(const FFishTables& Tables, FName RarityId)
	{
		const FFishRarityRow* Rarity = Tables.Rarities->FindRow<FFishRarityRow>(RarityId, TEXT("QA"), false);
		return Rarity ? Rarity->Rank : -1;
	}

	// ================================================================================================================
	// Roll: determinism, seeds, ordering (QA-01..QA-12)
	// ================================================================================================================

	/** QA-01: the same tables + context + seed give the identical instance, also after re-importing the tables */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollSameSeedTest, "Project.Fish.QA.Roll.SameSeedSameInstance", FISH_QA_FLAGS)
	bool FFishQARollSameSeedTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FTables Again;
		if (!LoadReal(*this, Again))
		{
			return false;
		}
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			for (int32 Seed : { 12345, 7, 99999 })
			{
				const FFishRollContext Context = Ctx(SpeciesId, Seed);
				FFishInstance A, B, C;
				TestTrue(TEXT("roll A"), FFishRoll::Roll(Real.Get(), Context, A));
				TestTrue(TEXT("roll B"), FFishRoll::Roll(Real.Get(), Context, B));
				TestTrue(TEXT("roll C (re-imported tables)"), FFishRoll::Roll(Again.Get(), Context, C));
				TestTrue(FString::Printf(TEXT("%s seed %d: A == B"), *SpeciesId.ToString(), Seed), Same(A, B));
				TestTrue(FString::Printf(TEXT("%s seed %d: A == C (fresh table objects)"), *SpeciesId.ToString(), Seed), Same(A, C));
				TestTrue(TEXT("valid"), A.IsValid());
				TestEqual(TEXT("species id stored"), A.SpeciesId, SpeciesId);
				TestEqual(TEXT("seed stored"), A.Seed, Seed);
				TestTrue(TEXT("all finite"), AllFinite(A));
			}
		}
		return true;
	}

	/** QA-02: every int32 seed is valid and deterministic; 0 is not a magic "random" seed */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollSeedEdgesTest, "Project.Fish.QA.Roll.SeedEdgeValues", FISH_QA_FLAGS)
	bool FFishQARollSeedEdgesTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const int32 Seeds[] = { 0, 1, -1, std::numeric_limits<int32>::max(), std::numeric_limits<int32>::min() };
		TArray<FFishInstance> Firsts;
		for (int32 Seed : Seeds)
		{
			const FFishRollContext Context = Ctx(TEXT("Bonefish"), Seed);
			FFishInstance A, B;
			TestTrue(FString::Printf(TEXT("seed %d rolls"), Seed), FFishRoll::Roll(Real.Get(), Context, A) && FFishRoll::Roll(Real.Get(), Context, B));
			TestTrue(FString::Printf(TEXT("seed %d is deterministic"), Seed), Same(A, B));
			TestEqual(FString::Printf(TEXT("seed %d stored"), Seed), A.Seed, Seed);
			TestTrue(TEXT("finite"), AllFinite(A));
			Firsts.Add(A);
		}
		int32 Distinct = 0;
		for (int32 i = 1; i < Firsts.Num(); ++i)
		{
			Distinct += Same(Firsts[0], Firsts[i]) ? 0 : 1;
		}
		TestTrue(TEXT("the 5 edge seeds don't all give the same fish"), Distinct > 0);
		return true;
	}

	/** QA-03: the seed actually drives the roll (1000 seeds give >= 990 distinct weights) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollSeedsVaryTest, "Project.Fish.QA.Roll.DifferentSeedsVary", FISH_QA_FLAGS)
	bool FFishQARollSeedsVaryTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TSet<float> Weights;
		for (int32 Seed = 1; Seed <= 1000; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(Real.Get(), Ctx(TEXT("Bonefish"), Seed), Fish);
			Weights.Add(Fish.WeightKg);
		}
		TestTrue(FString::Printf(TEXT(">= 990 distinct weights over 1000 seeds (got %d)"), Weights.Num()), Weights.Num() >= 990);
		return true;
	}

	/** QA-04: results per seed don't depend on the order of rolls in a batch or on other calls in between */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollBatchOrderTest, "Project.Fish.QA.Roll.BatchOrderIndependent", FISH_QA_FLAGS)
	bool FFishQARollBatchOrderTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const FFishTables Tables = Real.Get();
		const int32 N = 2000;
		auto RollSeed = [&Tables](int32 Seed)
		{
			FFishRollContext Context = Ctx(TEXT("Bonefish"), Seed);
			Context.Luck = 2.0f;
			FFishInstance Fish;
			FFishRoll::Roll(Tables, Context, Fish);
			return Fish;
		};

		TArray<FFishInstance> Forward;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			Forward.Add(RollSeed(Seed));
		}

		int32 Mismatches = 0;
		for (int32 Seed = N; Seed >= 1; --Seed)
		{
			Mismatches += Same(RollSeed(Seed), Forward[Seed - 1]) ? 0 : 1;
		}
		TestEqual(TEXT("reversed order gives the same fish per seed"), Mismatches, 0);

		TArray<int32> Order;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			Order.Add(Seed);
		}
		FRandomStream Shuffle(777);
		for (int32 i = Order.Num() - 1; i > 0; --i)
		{
			Order.Swap(i, Shuffle.RandRange(0, i));
		}
		Mismatches = 0;
		for (int32 Seed : Order)
		{
			Mismatches += Same(RollSeed(Seed), Forward[Seed - 1]) ? 0 : 1;
		}
		TestEqual(TEXT("shuffled order gives the same fish per seed"), Mismatches, 0);

		Mismatches = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			// other calls in between: another species, the bite picker, and (rarely) a failing roll
			FFishInstance Other;
			FFishRoll::Roll(Tables, Ctx(TEXT("CoralSnapper"), Seed + 99991), Other);
			FFishRollContext Bite = Ctx(NAME_None, Seed * 3);
			Bite.HabitatTag = Tag(TEXT("Habitat.Reef"));
			Bite.BaitTag = Tag(TEXT("Bait.Shrimp"));
			FName Picked;
			FFishRoll::PickSpecies(Tables, Bite, Picked);
			if (Seed % 400 == 0)
			{
				FFishInstance Failed;
				FFishRoll::Roll(Tables, Ctx(TEXT("QA_NoSuchSpecies"), Seed), Failed);
			}
			Mismatches += Same(RollSeed(Seed), Forward[Seed - 1]) ? 0 : 1;
		}
		TestEqual(TEXT("interleaved with other calls gives the same fish per seed"), Mismatches, 0);
		return true;
	}

	/** QA-05: the roll never reads the global RNG */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollGlobalRandomTest, "Project.Fish.QA.Roll.IgnoresGlobalRandom", FISH_QA_FLAGS)
	bool FFishQARollGlobalRandomTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TArray<FFishInstance> Before;
		FMath::RandInit(1);
		FMath::SRandInit(1);
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(Real.Get(), Ctx(Seed % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), Seed), Fish);
			Before.Add(Fish);
		}
		FMath::RandInit(424242);
		FMath::SRandInit(99);
		int32 Mismatches = 0;
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			volatile int32 Noise = FMath::Rand() + int32(FMath::FRand() * 10.0f) + int32(FMath::SRand() * 10.0f);
			(void)Noise;
			FFishInstance Fish;
			FFishRoll::Roll(Real.Get(), Ctx(Seed % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), Seed), Fish);
			Mismatches += Same(Fish, Before[Seed - 1]) ? 0 : 1;
		}
		TestEqual(TEXT("reseeding and consuming the global RNG changes nothing"), Mismatches, 0);
		return true;
	}

	/** QA-06: the stored seed + the same context reproduces the catch */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollRerollTest, "Project.Fish.QA.Roll.RerollFromStoredSeed", FISH_QA_FLAGS)
	bool FFishQARollRerollTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		int32 Mismatches = 0;
		for (int32 i = 0; i < 500; ++i)
		{
			FFishRollContext Context = Ctx(i % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), 1000003 * i + 17);
			Context.Luck = float(i % 7);
			Context.TimeOfDayHours = float(i % 24) + 0.5f;
			FFishInstance Fish;
			FFishRoll::Roll(Real.Get(), Context, Fish);

			FFishRollContext Again = Context;
			Again.SpeciesId = Fish.SpeciesId;
			Again.Seed = Fish.Seed;
			FFishInstance Rerolled;
			FFishRoll::Roll(Real.Get(), Again, Rerolled);
			Mismatches += Same(Fish, Rerolled) ? 0 : 1;
		}
		TestEqual(TEXT("reroll from the stored species + seed matches"), Mismatches, 0);
		return true;
	}

	/** QA-07: valid starter data rolls and bites without a single LogLureFish warning or error */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollSilentTest, "Project.Fish.QA.Roll.ValidDataIsSilent", FISH_QA_FLAGS)
	bool FFishQARollSilentTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FLogCapture Log;
		int32 Failures = 0;
		for (int32 Seed = 1; Seed <= 10000; ++Seed)
		{
			FFishRollContext Context = Ctx(Seed % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), Seed);
			Context.Luck = float(Seed % 11);
			Context.TimeOfDayHours = float(Seed % 48) * 0.5f;
			FFishInstance Fish;
			Failures += FFishRoll::Roll(Real.Get(), Context, Fish) ? 0 : 1;
		}
		const TCHAR* Habitats[] = { TEXT("Habitat.Shore"), TEXT("Habitat.Reef"), TEXT("Habitat.Lagoon"), TEXT("Habitat.DeepDrop") };
		const TCHAR* Baits[] = { TEXT("Bait.Worm"), TEXT("Bait.Shrimp"), TEXT("Bait.Squid"), TEXT("Bait.Lure") };
		for (int32 i = 0; i < 1000; ++i)
		{
			FFishRollContext Bite = Ctx(NAME_None, i);
			Bite.HabitatTag = Tag(Habitats[i % 4]);
			Bite.BaitTag = Tag(Baits[(i / 4) % 4]);
			Bite.TimeOfDayHours = float(i % 24);
			FName Picked;
			FFishRoll::PickSpecies(Real.Get(), Bite, Picked);
		}
		TestEqual(TEXT("all 10,000 rolls succeed"), Failures, 0);
		TestEqual(FString::Printf(TEXT("no LogLureFish warnings on valid data (first: %s)"), *Log.First()), Log.NumWarnings(), 0);
		TestEqual(TEXT("no LogLureFish errors on valid data"), Log.NumErrors(), 0);
		return true;
	}

	/** QA-08: an unknown or None species fails gracefully: false, default out instance, a warning, no error */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollUnknownSpeciesTest, "Project.Fish.QA.Roll.UnknownSpeciesFailsGracefully", FISH_QA_FLAGS)
	bool FFishQARollUnknownSpeciesTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FLogCapture Log;
		for (const FName& SpeciesId : { FName(TEXT("QA_NoSuchSpecies")), FName(NAME_None) })
		{
			FFishInstance Fish;
			FFishRoll::Roll(Real.Get(), Ctx(TEXT("Bonefish"), 5), Fish); // pre-filled, must be reset on failure
			TestTrue(TEXT("pre-filled instance is a fish"), Fish.IsValid());
			TestFalse(FString::Printf(TEXT("roll of '%s' fails"), *SpeciesId.ToString()), FFishRoll::Roll(Real.Get(), Ctx(SpeciesId, 5), Fish));
			TestTrue(TEXT("out instance is reset to a default (empty) instance"), Same(Fish, FFishInstance()));
		}
		TestTrue(TEXT("a LogLureFish warning names the unknown species"), Log.AnyWarningContains(TEXT("QA_NoSuchSpecies")));
		TestEqual(TEXT("no LogLureFish errors"), Log.NumErrors(), 0);
		return true;
	}

	/** QA-09: missing or empty tables: graceful failures, except an empty or null modifier table, which is valid */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollMissingTablesTest, "Project.Fish.QA.Roll.MissingTablesGraceful", FISH_QA_FLAGS)
	bool FFishQARollMissingTablesTest::RunTest(const FString& Parameters)
	{
		FTables Id;
		MakeIdentity(Id);
		const FFishRollContext Context = Ctx(TEXT("QA_Fixed"), 3);
		FLogCapture Log;

		auto Expect = [this, &Log, &Context](const FString& What, const FFishTables& Tables, bool bExpectOk, bool bExpectWarning)
		{
			Log.Reset();
			FFishInstance Fish;
			const bool bOk = FFishRoll::Roll(Tables, Context, Fish);
			TestEqual(What + TEXT(": result"), bOk, bExpectOk);
			TestTrue(What + TEXT(": finite"), AllFinite(Fish));
			if (!bOk)
			{
				TestTrue(What + TEXT(": default instance on failure"), Same(Fish, FFishInstance()));
			}
			if (bExpectWarning)
			{
				TestTrue(What + TEXT(": warns"), Log.NumWarnings() > 0);
			}
			else
			{
				TestEqual(What + TEXT(": silent"), Log.NumWarnings(), 0);
			}
			TestEqual(What + TEXT(": no errors"), Log.NumErrors(), 0);
		};

		FFishTables Tables = Id.Get();
		Tables.Species = nullptr;
		Expect(TEXT("null species table"), Tables, false, true);
		Tables = Id.Get();
		Tables.Rarities = nullptr;
		Expect(TEXT("null rarity table"), Tables, false, true);
		Tables = Id.Get();
		Tables.Stats = nullptr;
		Expect(TEXT("null stat table"), Tables, false, true);
		Tables = Id.Get();
		Tables.Modifiers = nullptr;
		Expect(TEXT("null modifier table (counts as empty, with a warning)"), Tables, true, true);

		Expect(TEXT("empty modifier table (valid content)"), Id.Get(), true, false);

		FTables EmptyRarity;
		MakeIdentity(EmptyRarity);
		ResetRarities(EmptyRarity);
		Expect(TEXT("empty rarity table"), EmptyRarity.Get(), false, true);

		FTables EmptySpecies;
		MakeIdentity(EmptySpecies);
		EmptySpecies.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct()));
		Expect(TEXT("empty species table"), EmptySpecies.Get(), false, true);

		// A stat table without the Weight row: graceful either way (no crash, no NaN), with a warning
		FTables NoWeight;
		MakeIdentity(NoWeight);
		NoWeight.Stats->RemoveRow(TEXT("Weight"));
		Log.Reset();
		FFishInstance Fish;
		FFishRoll::Roll(NoWeight.Get(), Context, Fish);
		TestTrue(TEXT("no Weight stat row: finite"), AllFinite(Fish));
		TestTrue(TEXT("no Weight stat row: warns"), Log.NumWarnings() > 0);
		TestEqual(TEXT("no Weight stat row: no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** Runs Roll and the independent oracle on many contexts; reports the first differences */
	static void CompareWithOracle(FAutomationTestBase& Test, const FString& What, const FFishTables& Tables, const TArray<FFishRollContext>& Contexts)
	{
		int32 Mismatches = 0;
		int32 Compared = 0;
		for (const FFishRollContext& Context : Contexts)
		{
			FFishInstance Actual;
			FOracleOut Expected;
			const bool bActual = FFishRoll::Roll(Tables, Context, Actual);
			const bool bExpected = OracleRoll(Tables, Context, Expected);
			++Compared;
			FString Diff;
			if (bActual != bExpected)
			{
				Diff = FString::Printf(TEXT("result %d vs oracle %d"), bActual, bExpected);
			}
			else if (bActual)
			{
				Diff = DiffAgainstOracle(Actual, Expected);
			}
			if (!Diff.IsEmpty())
			{
				if (++Mismatches <= 6)
				{
					Test.AddError(FString::Printf(TEXT("%s: %s seed %d luck %.1f t %.1f: %s"), *What, *Context.SpeciesId.ToString(), Context.Seed, Context.Luck, Context.TimeOfDayHours, *Diff));
				}
			}
		}
		Test.AddInfo(FString::Printf(TEXT("%s: %d rolls compared with the spec oracle, %d mismatches"), *What, Compared, Mismatches));
		Test.TestEqual(What + TEXT(": mismatches against the documented formula"), Mismatches, 0);
	}

	/** New (oracle): every field of Roll() on the starter data equals an independent implementation of the documented pipeline */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollOracleRealTest, "Project.Fish.QA.Roll.MatchesSpecFormula_Real", FISH_QA_FLAGS)
	bool FFishQARollOracleRealTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TArray<FFishRollContext> Contexts;
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			for (float Luck : { 0.0f, 4.0f, 10.0f })
			{
				for (float Hours : { 10.0f, 22.0f })
				{
					for (int32 Seed = 1; Seed <= 1500; ++Seed)
					{
						FFishRollContext Context = Ctx(SpeciesId, Seed * 7919 - 50000);
						Context.Luck = Luck;
						Context.TimeOfDayHours = Hours;
						Contexts.Add(Context);
					}
				}
			}
		}
		CompareWithOracle(*this, TEXT("starter data"), Real.Get(), Contexts);
		return true;
	}

	/**
	 *  Rich fixture for the oracle: allow-lists, family/region/time/weather conditions, three exclusivity groups,
	 *  the MaxModifiers cap, natural numeric names (Mod_2 < Mod_10), rarity StatMods and LevelBonus, a clamped stat.
	 */
	static void MakeOracleFixture(FTables& T)
	{
		MakeStandardStats(T);
		T.Stats->FindRow<FFishStatRow>(TEXT("Speed"), TEXT("QA"))->Max = 30.0f; // clamps are exercised

		T.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct()));
		FFishSpeciesRow S1 = MakeSpecies(1.0f, 9.0f);
		S1.SizeSkew = 1.5f;
		S1.ReferenceWeight = 3.0f;
		S1.WeightStatExponent = 0.7f;
		S1.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F1.Sub")) };
		S1.MaxModifiers = 3;
		T.Species->AddRow(TEXT("S1"), S1);
		FFishSpeciesRow S2 = MakeSpecies(0.5f, 2.0f);
		S2.SizeSkew = 0.6f;
		S2.ReferenceWeight = 1.0f;
		S2.WeightStatExponent = 0.0f;
		S2.BaseLevel = 12;
		S2.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F2")) };
		S2.AllowedRarities = { TEXT("R0"), TEXT("R2"), TEXT("R3") };
		S2.AllowedModifiers = { TEXT("M_A"), TEXT("M_B"), TEXT("M_G1"), TEXT("M_G2"), TEXT("M_Night"), TEXT("M_Only2") };
		S2.MaxModifiers = 2;
		T.Species->AddRow(TEXT("S2"), S2);

		ResetRarities(T);
		T.Rarities->AddRow(TEXT("R0"), MakeRarity(0, 50.0f, 1.0f, 1.0f, 0));
		FFishRarityRow R1 = MakeRarity(1, 30.0f, 1.5f, 1.25f, 0);
		R1.StatMods = { MulMod(TEXT("Fish.Stat.Strength"), 1.2f), AddMod(TEXT("Fish.Stat.Stamina"), 3.0f) };
		T.Rarities->AddRow(TEXT("R1"), R1);
		FFishRarityRow R2 = MakeRarity(2, 15.0f, 2.5f, 1.75f, 1);
		R2.StatMods = { MulMod(TEXT("Fish.Stat.Speed"), 1.5f) };
		T.Rarities->AddRow(TEXT("R2"), R2);
		T.Rarities->AddRow(TEXT("R3"), MakeRarity(3, 0.0f, 5.0f, 3.0f, 2));
		FFishRarityRow R4 = MakeRarity(4, 5.0f, 8.0f, 4.0f, 2);
		R4.StatMods = { AddMod(TEXT("Fish.Stat.Strength"), 10.0f), MulMod(TEXT("Fish.Stat.Strength"), 1.1f) };
		T.Rarities->AddRow(TEXT("R4"), R4);

		ResetModifiers(T);
		T.Modifiers->AddRow(TEXT("M_A"), MakeModifier(0.4f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 2.0f), MulMod(TEXT("Fish.Stat.Speed"), 1.25f) }, 1.1f));
		T.Modifiers->AddRow(TEXT("M_B"), MakeModifier(0.3f, NAME_None, { MulMod(TEXT("Fish.Stat.Stamina"), 1.5f) }, 1.2f));
		T.Modifiers->AddRow(TEXT("M_G1"), MakeModifier(0.5f, TEXT("G"), { MulMod(TEXT("Fish.Stat.Weight"), 1.3f) }, 1.3f));
		T.Modifiers->AddRow(TEXT("M_G2"), MakeModifier(0.25f, TEXT("G"), { MulMod(TEXT("Fish.Stat.Weight"), 1.1f), MulMod(TEXT("Fish.Stat.Strength"), 1.05f) }, 1.1f));
		T.Modifiers->AddRow(TEXT("M_G3"), MakeModifier(0.35f, TEXT("G"), { AddMod(TEXT("Fish.Stat.Strength"), 1.0f) }));
		T.Modifiers->AddRow(TEXT("M_H"), MakeModifier(0.6f, TEXT("H"), { AddMod(QAStat, 7.0f) }));
		T.Modifiers->AddRow(TEXT("M_H2"), MakeModifier(0.6f, TEXT("H"), { AddMod(QAStat, 3.0f) }));
		FFishModifierRow Night = MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 4.0f) });
		Night.TimeWindows = { FFishTimeWindow() };
		Night.TimeWindows[0].StartHour = 20.0f;
		Night.TimeWindows[0].EndHour = 4.0f;
		T.Modifiers->AddRow(TEXT("M_Night"), Night);
		FFishModifierRow Region = MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Stamina"), 1.0f) });
		Region.RegionTags = { Tag(TEXT("Test.Fish.Region.R1")) };
		T.Modifiers->AddRow(TEXT("M_Region"), Region);
		FFishModifierRow Family = MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Speed"), 2.0f) });
		Family.SpeciesTags = { Tag(TEXT("Test.Fish.Family.F1")) };
		T.Modifiers->AddRow(TEXT("M_Fam"), Family);
		FFishModifierRow Only2 = MakeModifier(0.5f, NAME_None, {}, 2.0f);
		Only2.SpeciesIds = { TEXT("S2") };
		T.Modifiers->AddRow(TEXT("M_Only2"), Only2);
		FFishModifierRow Rain = MakeModifier(0.5f, NAME_None, { MulMod(TEXT("Fish.Stat.Strength"), 1.1f) });
		Rain.WeatherTags = { Tag(TEXT("Test.Fish.Weather.Rain")) };
		T.Modifiers->AddRow(TEXT("M_Rain"), Rain);
		T.Modifiers->AddRow(TEXT("Mod_10"), MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Speed"), 0.5f) }));
		T.Modifiers->AddRow(TEXT("Mod_2"), MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Speed"), 0.25f) }));
		T.Tuning = FFishRollTuning();
	}

	/** New (oracle): the documented pipeline on a rich fixture (conditions, groups, cap, rarity mods, clamps, forced overrides) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollOracleFixtureTest, "Project.Fish.QA.Roll.MatchesSpecFormula_Fixture", FISH_QA_FLAGS)
	bool FFishQARollOracleFixtureTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeOracleFixture(T);
		TArray<FFishRollContext> Contexts;
		for (const TCHAR* SpeciesId : { TEXT("S1"), TEXT("S2") })
		{
			for (float Hours : { 10.0f, 22.0f })
			{
				for (const TCHAR* RegionTag : { TEXT("Test.Fish.Region.R1.Sub"), TEXT("Test.Fish.Region.R2") })
				{
					for (const TCHAR* Weather : { TEXT("Test.Fish.Weather.Rain"), TEXT("") })
					{
						for (float Luck : { 0.0f, 5.0f })
						{
							for (int32 Seed = 1; Seed <= 300; ++Seed)
							{
								FFishRollContext Context = Ctx(SpeciesId, Seed);
								Context.TimeOfDayHours = Hours;
								Context.RegionTag = Tag(RegionTag);
								Context.WeatherTag = *Weather ? Tag(Weather) : FGameplayTag();
								Context.Luck = Luck;
								Contexts.Add(Context);
							}
						}
					}
				}
			}
		}
		// Forced overrides: a 0-weight tier, duplicate/unordered forced modifiers, a forced weight fraction
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			FFishRollContext Context = Ctx(Seed % 2 ? TEXT("S1") : TEXT("S2"), Seed);
			Context.ForcedRarityId = TEXT("R3");
			Context.bForceModifiers = true;
			Context.ForcedModifierIds = { TEXT("M_B"), TEXT("M_A"), TEXT("M_A"), TEXT("M_G1"), TEXT("M_G2") };
			Context.bForceWeightFraction = true;
			Context.ForcedWeightFraction = float(Seed % 5) * 0.25f;
			Contexts.Add(Context);
		}
		CompareWithOracle(*this, TEXT("rich fixture"), T.Get(), Contexts);
		return true;
	}

	/** QA-11: stages use separate sub-streams: a new modifier row keeps weight and rarity; luck keeps weight and modifiers; conditions don't shift other rows */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollStageIsolationTest, "Project.Fish.QA.Roll.StageIsolation", FISH_QA_FLAGS)
	bool FFishQARollStageIsolationTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FTables Extra;
		if (!LoadReal(*this, Extra))
		{
			return false;
		}
		// Lexically first, so every existing modifier row's draw shifts; weight and rarity must not
		Extra.Modifiers->AddRow(TEXT("AAA_QA_Extra"), MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Aggression"), 1.0f) }));

		int32 WeightOrRarityChanged = 0;
		int32 LuckChangedWeightOrMods = 0;
		for (int32 Seed = 1; Seed <= 3000; ++Seed)
		{
			FFishRollContext Context = Ctx(Seed % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), Seed);
			Context.bForceModifiers = false;
			FFishRollContext BaseOnly = Context;
			BaseOnly.bForceModifiers = true; // base weight without Heavy/Giant

			FFishInstance A, B;
			FFishRoll::Roll(Real.Get(), BaseOnly, A);
			FFishRoll::Roll(Extra.Get(), BaseOnly, B);
			WeightOrRarityChanged += (A.WeightKg == B.WeightKg && A.RarityId == B.RarityId) ? 0 : 1;

			FFishInstance Lucky0, Lucky10;
			FFishRollContext L0 = Context;
			FFishRollContext L10 = Context;
			L10.Luck = 10.0f;
			FFishRoll::Roll(Real.Get(), L0, Lucky0);
			FFishRoll::Roll(Real.Get(), L10, Lucky10);
			LuckChangedWeightOrMods += (Lucky0.WeightKg == Lucky10.WeightKg && Lucky0.ModifierIds == Lucky10.ModifierIds) ? 0 : 1;
		}
		TestEqual(TEXT("adding a modifier row never changes weight or rarity"), WeightOrRarityChanged, 0);
		TestEqual(TEXT("changing luck never changes weight or modifiers"), LuckChangedWeightOrMods, 0);

		// A time condition on one row must not shift another row's result
		FTables T;
		MakeIdentity(T);
		FFishModifierRow Night = MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 1.0f) });
		Night.TimeWindows = { FFishTimeWindow() };
		Night.TimeWindows[0].StartHour = 20.0f;
		Night.TimeWindows[0].EndHour = 4.0f;
		T.Modifiers->AddRow(TEXT("A_Night"), Night);
		T.Modifiers->AddRow(TEXT("B_Any"), MakeModifier(0.5f));
		T.Modifiers->AddRow(TEXT("C_Any"), MakeModifier(0.3f));
		int32 Shifted = 0;
		for (int32 Seed = 1; Seed <= 5000; ++Seed)
		{
			FFishRollContext Day = Ctx(TEXT("QA_Fixed"), Seed);
			Day.TimeOfDayHours = 12.0f;
			FFishRollContext NightTime = Day;
			NightTime.TimeOfDayHours = 22.0f;
			FFishInstance D, N;
			FFishRoll::Roll(T.Get(), Day, D);
			FFishRoll::Roll(T.Get(), NightTime, N);
			TestFalse(TEXT("A_Night never by day"), D.HasModifier(TEXT("A_Night")));
			Shifted += (D.HasModifier(TEXT("B_Any")) == N.HasModifier(TEXT("B_Any")) && D.HasModifier(TEXT("C_Any")) == N.HasModifier(TEXT("C_Any"))) ? 0 : 1;
		}
		TestEqual(TEXT("an ineligible row doesn't shift the other rows' draws"), Shifted, 0);
		return true;
	}

	/** QA-12: the core is documented as thread-safe: parallel rolls equal serial rolls */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollParallelTest, "Project.Fish.QA.Roll.ParallelMatchesSerial", FISH_QA_FLAGS)
	bool FFishQARollParallelTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const FFishTables Tables = Real.Get();
		const int32 N = 8000;
		auto MakeContext = [](int32 i)
		{
			FFishRollContext Context = Ctx(i % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), i * 31 + 5);
			Context.Luck = float(i % 5);
			return Context;
		};
		TArray<FFishInstance> Serial;
		Serial.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			FFishRoll::Roll(Tables, MakeContext(i), Serial[i]);
		}
		TArray<FFishInstance> Parallel;
		Parallel.SetNum(N);
		ParallelFor(N, [&](int32 i)
		{
			FFishRoll::Roll(Tables, MakeContext(i), Parallel[i]);
		});
		int32 Mismatches = 0;
		for (int32 i = 0; i < N; ++i)
		{
			Mismatches += Same(Serial[i], Parallel[i]) ? 0 : 1;
		}
		TestEqual(TEXT("parallel rolls equal serial rolls"), Mismatches, 0);
		return true;
	}

	/** New: forced overrides (tests, Lure.GiveFish): a 0-weight tier can be forced, weight fraction is linear, forced modifiers are normalized */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollForcedTest, "Project.Fish.QA.Roll.ForcedOverrides", FISH_QA_FLAGS)
	bool FFishQARollForcedTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const FFishSpeciesRow* Bonefish = Row<FFishSpeciesRow>(Real.Species, TEXT("Bonefish"));
		const FFishRarityRow* Legendary = Row<FFishRarityRow>(Real.Rarities, TEXT("Legendary"));
		if (!TestNotNull(TEXT("Bonefish row"), Bonefish) || !TestNotNull(TEXT("Legendary row"), Legendary))
		{
			return false;
		}

		FFishRollContext Context = Ctx(TEXT("Bonefish"), 11);
		Context.ForcedRarityId = TEXT("Legendary");
		Context.bForceModifiers = true;
		FFishInstance Fish;
		TestTrue(TEXT("forced Legendary (RollWeight 0) rolls"), FFishRoll::Roll(Real.Get(), Context, Fish));
		TestEqual(TEXT("forced rarity is used"), Fish.RarityId, FName(TEXT("Legendary")));
		TestEqual(TEXT("level = BaseLevel + LevelBonus"), Fish.Level, Bonefish->BaseLevel + Legendary->LevelBonus);

		for (float Fraction : { 0.0f, 0.25f, 0.5f, 1.0f })
		{
			FFishRollContext Weighed = Ctx(TEXT("Bonefish"), 3);
			Weighed.bForceModifiers = true;
			Weighed.bForceWeightFraction = true;
			Weighed.ForcedWeightFraction = Fraction;
			FFishInstance W;
			FFishRoll::Roll(Real.Get(), Weighed, W);
			const float Expected = Bonefish->WeightMin + (Bonefish->WeightMax - Bonefish->WeightMin) * Fraction; // linear, no SizeSkew
			TestTrue(FString::Printf(TEXT("forced fraction %.2f gives %.4f kg (got %.4f)"), Fraction, Expected, W.WeightKg), FMath::IsNearlyEqual(W.WeightKg, Expected, 1e-4f));
		}

		FFishRollContext Mods = Ctx(TEXT("Bonefish"), 4);
		Mods.bForceModifiers = true;
		Mods.ForcedModifierIds = { TEXT("Heavy"), TEXT("Giant"), TEXT("Heavy"), TEXT("Albino") };
		FFishInstance WithMods;
		FFishRoll::Roll(Real.Get(), Mods, WithMods);
		const TArray<FName> ExpectedMods = { TEXT("Albino"), TEXT("Giant"), TEXT("Heavy") };
		TestTrue(FString::Printf(TEXT("forced modifiers sorted, deduplicated, group rule skipped (got [%s])"), *ModsKey(WithMods)), WithMods.ModifierIds == ExpectedMods);

		FFishRollContext NoMods = Ctx(TEXT("Bonefish"), 4);
		NoMods.bForceModifiers = true;
		FFishInstance Plain;
		FFishRoll::Roll(Real.Get(), NoMods, Plain);
		TestEqual(TEXT("forced empty modifier list = no modifiers"), Plain.ModifierIds.Num(), 0);

		// Unknown ids: graceful (warning), and the unknown id is never stored in the record
		FLogCapture Log;
		FFishRollContext BadRarity = Ctx(TEXT("Bonefish"), 5);
		BadRarity.ForcedRarityId = TEXT("QA_NoSuchRarity");
		FFishInstance Bad;
		const bool bBadOk = FFishRoll::Roll(Real.Get(), BadRarity, Bad);
		TestTrue(TEXT("unknown forced rarity: warns"), Log.NumWarnings() > 0);
		TestFalse(TEXT("unknown forced rarity: never stored"), bBadOk && Bad.RarityId == FName(TEXT("QA_NoSuchRarity")));
		// The spec doesn't say whether an unknown forced modifier drops just that id or fails the roll; both are graceful.
		// Observed contract (lane eng2 3567875): the roll fails with a warning naming the id.
		Log.Reset();
		FFishRollContext BadMod = Ctx(TEXT("Bonefish"), 5);
		BadMod.bForceModifiers = true;
		BadMod.ForcedModifierIds = { TEXT("QA_NoSuchModifier"), TEXT("Feisty") };
		FFishInstance BadModFish;
		const bool bBadModOk = FFishRoll::Roll(Real.Get(), BadMod, BadModFish);
		TestFalse(TEXT("unknown forced modifier is never stored"), BadModFish.HasModifier(TEXT("QA_NoSuchModifier")));
		if (bBadModOk)
		{
			TestTrue(TEXT("roll kept going: the known forced modifier is kept"), BadModFish.HasModifier(TEXT("Feisty")));
		}
		else
		{
			TestTrue(TEXT("roll failed: default instance"), Same(BadModFish, FFishInstance()));
		}
		TestTrue(TEXT("unknown forced modifier: a warning names it"), Log.AnyWarningContains(TEXT("QA_NoSuchModifier")));
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** New: StageSeed/MakeStageStream follow the documented derivation, and the 4 stage streams differ */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollStageSeedTest, "Project.Fish.QA.Roll.StageSeedsMatchSpec", FISH_QA_FLAGS)
	bool FFishQARollStageSeedTest::RunTest(const FString& Parameters)
	{
		const EFishRollStage Stages[] = { EFishRollStage::Weight, EFishRollStage::Rarity, EFishRollStage::Modifiers, EFishRollStage::Bite };
		TestEqual(TEXT("stage ids"), uint32(EFishRollStage::Weight) * 1000 + uint32(EFishRollStage::Rarity) * 100 + uint32(EFishRollStage::Modifiers) * 10 + uint32(EFishRollStage::Bite), 1234u);
		for (int32 Seed : { 0, 1, -1, 12345, std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() })
		{
			TSet<float> FirstDraws;
			for (EFishRollStage Stage : Stages)
			{
				const uint32 Expected = HashCombine(uint32(Seed), uint32(Stage));
				TestEqual(FString::Printf(TEXT("StageSeed(%d, %u)"), Seed, uint32(Stage)), FFishRoll::StageSeed(Seed, Stage), Expected);
				const FRandomStream Actual = FFishRoll::MakeStageStream(Seed, Stage);
				const FRandomStream Reference{ int32(Expected) };
				bool bSame = true;
				for (int32 i = 0; i < 5; ++i)
				{
					bSame &= Actual.FRand() == Reference.FRand();
				}
				TestTrue(TEXT("MakeStageStream == FRandomStream(StageSeed)"), bSame);
				FirstDraws.Add(FFishRoll::MakeStageStream(Seed, Stage).FRand());
			}
			TestTrue(FString::Printf(TEXT("seed %d: the 4 stage streams start differently"), Seed), FirstDraws.Num() >= 3);
		}
		return true;
	}

	/** New (P2): the server seed source varies */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARollRandomSeedTest, "Project.Fish.QA.Roll.MakeRandomSeedVaries", FISH_QA_FLAGS)
	bool FFishQARollRandomSeedTest::RunTest(const FString& Parameters)
	{
		TSet<int32> Seeds;
		for (int32 i = 0; i < 200; ++i)
		{
			Seeds.Add(FFishRoll::MakeRandomSeed());
		}
		TestTrue(FString::Printf(TEXT(">= 190 distinct seeds in 200 calls (got %d)"), Seeds.Num()), Seeds.Num() >= 190);
		return true;
	}

	// ================================================================================================================
	// Rarity (QA-13..QA-24)
	// ================================================================================================================

	/** QA-13: starter data rarity odds over 100k sequential seeds per species match RollWeight / sum (0-weight tiers never) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityRealOddsTest, "Project.Fish.QA.Rarity.OddsMatchWeights_Real", FISH_QA_FLAGS)
	bool FFishQARarityRealOddsTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			CheckRarityOdds(*this, SpeciesId.ToString(), Real.Get(), SpeciesId, 100000, 0.0f);
		}
		return true;
	}

	/** QA-14: fixture weight sets (integers, awkward floats, tiny 1e-6, huge 1.5e9) roll at their documented odds */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityFixtureOddsTest, "Project.Fish.QA.Rarity.OddsMatchWeights_Fixtures", FISH_QA_FLAGS)
	bool FFishQARarityFixtureOddsTest::RunTest(const FString& Parameters)
	{
		struct FCase { const TCHAR* Name; TArray<float> Weights; int32 N; };
		const TArray<FCase> Cases = {
			{ TEXT("main {70,25,5,0,0}"), { 70.0f, 25.0f, 5.0f, 0.0f, 0.0f }, 100000 },
			{ TEXT("floats {0.1,0.2,0.3,0.4}"), { 0.1f, 0.2f, 0.3f, 0.4f }, 100000 },
			{ TEXT("tiny {1e-6,1e-6}"), { 1e-6f, 1e-6f }, 50000 },
			{ TEXT("huge {1.5e9,1.5e9}"), { 1.5e9f, 1.5e9f }, 50000 },
		};
		for (const FCase& Case : Cases)
		{
			FTables T;
			MakeIdentity(T);
			SetRarities(T, Case.Weights);
			CheckRarityOdds(*this, Case.Name, T.Get(), TEXT("QA_Fixed"), Case.N, 0.0f);
		}
		return true;
	}

	/** S4 pure helper: exact boundaries of PickWeightedIndex (half-open intervals, clamping, never a 0-weight index) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityPickBoundariesTest, "Project.Fish.QA.Rarity.PickWeightedIndexBoundaries", FISH_QA_FLAGS)
	bool FFishQARarityPickBoundariesTest::RunTest(const FString& Parameters)
	{
		const float JustBelowOne = 1.0f - 1.0f / 16777216.0f; // 1 - 2^-24
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();
		auto Pick = [](TArray<float> Weights, float U) { return FFishRoll::PickWeightedIndex(Weights, U); };

		TestEqual(TEXT("{1,1} U=0"), Pick({ 1, 1 }, 0.0f), 0);
		TestEqual(TEXT("{1,1} U=0.4999"), Pick({ 1, 1 }, 0.4999f), 0);
		TestEqual(TEXT("{1,1} U=0.5 is the start of the second interval"), Pick({ 1, 1 }, 0.5f), 1);
		TestEqual(TEXT("{1,1} U=1-2^-24"), Pick({ 1, 1 }, JustBelowOne), 1);
		TestEqual(TEXT("{1,1} U=1 is clamped below 1"), Pick({ 1, 1 }, 1.0f), 1);
		TestEqual(TEXT("{1,1} U=7 is clamped"), Pick({ 1, 1 }, 7.0f), 1);
		TestEqual(TEXT("{1,1} U=-3 is clamped"), Pick({ 1, 1 }, -3.0f), 0);
		for (float U : { 0.0f, 0.3f, 0.999f, JustBelowOne, 1.0f })
		{
			TestEqual(FString::Printf(TEXT("{0,0,5,0} U=%.7f picks the only positive weight"), U), Pick({ 0, 0, 5, 0 }, U), 2);
			TestEqual(FString::Printf(TEXT("{1,0} U=%.7f never returns the trailing 0"), U), Pick({ 1, 0 }, U), 0);
			TestEqual(FString::Printf(TEXT("{-1,2} U=%.7f skips the negative weight"), U), Pick({ -1, 2 }, U), 1);
			TestEqual(FString::Printf(TEXT("{NaN,3} U=%.7f skips NaN"), U), Pick({ NaN, 3 }, U), 1);
			TestEqual(FString::Printf(TEXT("{Inf,1} U=%.7f treats Inf as 0"), U), Pick({ Inf, 1 }, U), 1);
		}
		TestEqual(TEXT("empty -> INDEX_NONE"), Pick({}, 0.5f), int32(INDEX_NONE));
		TestEqual(TEXT("{0,0} -> INDEX_NONE"), Pick({ 0, 0 }, 0.5f), int32(INDEX_NONE));
		TestEqual(TEXT("{-1,NaN,Inf} -> INDEX_NONE"), Pick({ -1, NaN, Inf }, 0.5f), int32(INDEX_NONE));
		TestEqual(TEXT("tiny {1e-6,1e-6} U=0.25"), Pick({ 1e-6f, 1e-6f }, 0.25f), 0);
		TestEqual(TEXT("tiny {1e-6,1e-6} U=0.75"), Pick({ 1e-6f, 1e-6f }, 0.75f), 1);
		TestEqual(TEXT("huge {1.5e9,1.5e9} U=0.25"), Pick({ 1.5e9f, 1.5e9f }, 0.25f), 0);
		TestEqual(TEXT("huge {1.5e9,1.5e9} U=0.75"), Pick({ 1.5e9f, 1.5e9f }, 0.75f), 1);
		const int32 NanPick = Pick({ 1, 0, 2 }, NaN);
		TestTrue(FString::Printf(TEXT("U=NaN never returns a 0-weight index (got %d)"), NanPick), NanPick == INDEX_NONE || NanPick == 0 || NanPick == 2);

		// A fine sweep: the index never decreases with U, and each index covers weight/total of [0,1)
		const TArray<float> Weights = { 0.1f, 0.2f, 0.3f, 0.4f };
		TArray<int32> Hits;
		Hits.SetNumZeroed(4);
		int32 Previous = 0;
		bool bMonotone = true;
		const int32 Steps = 100000;
		for (int32 i = 0; i < Steps; ++i)
		{
			const int32 Index = Pick(Weights, (i + 0.5f) / Steps);
			bMonotone &= Index >= Previous;
			Previous = Index;
			if (Index >= 0)
			{
				Hits[Index]++;
			}
		}
		TestTrue(TEXT("index is non-decreasing in U"), bMonotone);
		for (int32 i = 0; i < 4; ++i)
		{
			TestTrue(FString::Printf(TEXT("index %d covers %.4f of [0,1) (expected %.4f)"), i, double(Hits[i]) / Steps, Weights[i]), FMath::Abs(double(Hits[i]) / Steps - Weights[i]) < 2e-4);
		}
		return true;
	}

	/** QA-15: 0-weight tiers never roll, wherever they sit in the table and at any luck */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityZeroWeightTest, "Project.Fish.QA.Rarity.ZeroWeightNeverRolls", FISH_QA_FLAGS)
	bool FFishQARarityZeroWeightTest::RunTest(const FString& Parameters)
	{
		const TArray<TArray<float>> Layouts = {
			{ 0.0f, 0.0f, 70.0f, 25.0f, 5.0f },
			{ 70.0f, 0.0f, 25.0f, 0.0f, 5.0f },
			{ 70.0f, 25.0f, 5.0f, 0.0f, 0.0f },
		};
		for (const TArray<float>& Layout : Layouts)
		{
			FTables T;
			MakeIdentity(T);
			SetRarities(T, Layout);
			for (float Luck : { 0.0f, 10.0f, 1e6f })
			{
				int32 Failures = 0;
				const TMap<FName, int32> Counts = CountRarities(T.Get(), TEXT("QA_Fixed"), 20000, Luck, Failures);
				TestEqual(TEXT("all rolls succeed"), Failures, 0);
				for (int32 i = 0; i < Layout.Num(); ++i)
				{
					if (Layout[i] == 0.0f)
					{
						TestEqual(FString::Printf(TEXT("layout %s luck %g: 0-weight R%d never rolls"),
							*FString::JoinBy(Layout, TEXT(","), [](float W) { return FString::SanitizeFloat(W); }), Luck, i),
							Counts.FindRef(FName(*FString::Printf(TEXT("R%d"), i))), 0);
					}
				}
			}
		}
		// The starter data at maximum luck: Epic and Legendary stay disabled
		FTables Real;
		if (LoadReal(*this, Real))
		{
			int32 Failures = 0;
			const TMap<FName, int32> Counts = CountRarities(Real.Get(), TEXT("CoralSnapper"), 50000, 1e6f, Failures);
			TestEqual(TEXT("starter data, huge luck: no Epic"), Counts.FindRef(TEXT("Epic")), 0);
			TestEqual(TEXT("starter data, huge luck: no Legendary"), Counts.FindRef(TEXT("Legendary")), 0);
		}
		return true;
	}

	/** QA-16: a single enabled tier always rolls */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARaritySingleTierTest, "Project.Fish.QA.Rarity.SingleTierAlwaysRolls", FISH_QA_FLAGS)
	bool FFishQARaritySingleTierTest::RunTest(const FString& Parameters)
	{
		for (const TArray<float>& Layout : { TArray<float>{ 3.0f }, TArray<float>{ 0.0f, 0.0f, 3.0f, 0.0f, 0.0f } })
		{
			FTables T;
			MakeIdentity(T);
			SetRarities(T, Layout);
			const FName Only = Layout.Num() == 1 ? FName(TEXT("R0")) : FName(TEXT("R2"));
			for (float Luck : { 0.0f, 10.0f })
			{
				int32 Failures = 0;
				const TMap<FName, int32> Counts = CountRarities(T.Get(), TEXT("QA_Fixed"), 5000, Luck, Failures);
				TestEqual(FString::Printf(TEXT("%d tiers, luck %g: always %s"), Layout.Num(), Luck, *Only.ToString()), Counts.FindRef(Only), 5000);
			}
		}
		return true;
	}

	/** QA-17: all candidate weights 0 -> the roll fails gracefully */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityAllZeroTest, "Project.Fish.QA.Rarity.AllZeroWeightsGraceful", FISH_QA_FLAGS)
	bool FFishQARarityAllZeroTest::RunTest(const FString& Parameters)
	{
		FLogCapture Log;
		FTables T;
		MakeIdentity(T);
		SetRarities(T, { 0.0f, 0.0f, 0.0f });
		FFishInstance Fish;
		TestFalse(TEXT("all tiers 0: roll fails"), FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), 1), Fish));
		TestTrue(TEXT("all tiers 0: default instance"), Same(Fish, FFishInstance()));
		TestTrue(TEXT("all tiers 0: warns"), Log.NumWarnings() > 0);

		FTables Allowed;
		MakeIdentity(Allowed);
		SetRarities(Allowed, { 0.0f, 0.0f, 9.0f });
		Row<FFishSpeciesRow>(Allowed.Species, TEXT("QA_Fixed"))->AllowedRarities = { TEXT("R0"), TEXT("R1") };
		Log.Reset();
		TestFalse(TEXT("species allows only 0-weight tiers: roll fails"), FFishRoll::Roll(Allowed.Get(), Ctx(TEXT("QA_Fixed"), 1), Fish));
		TestTrue(TEXT("species allows only 0-weight tiers: warns"), Log.NumWarnings() > 0);
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** QA-18: negative and non-finite RollWeights count as 0; the others renormalize */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityNegativeWeightTest, "Project.Fish.QA.Rarity.NegativeWeightTreatedAsZero", FISH_QA_FLAGS)
	bool FFishQARarityNegativeWeightTest::RunTest(const FString& Parameters)
	{
		for (float Bad : { -5.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
		{
			FTables T;
			MakeIdentity(T);
			SetRarities(T, { 50.0f, Bad, 50.0f });
			TArray<FString> Names = { TEXT("R0"), TEXT("R1"), TEXT("R2") };
			int32 Failures = 0;
			const TMap<FName, int32> Counts = CountRarities(T.Get(), TEXT("QA_Fixed"), 50000, 0.0f, Failures);
			TestEqual(TEXT("rolls succeed"), Failures, 0);
			CheckOdds(*this, FString::Printf(TEXT("{50,%s,50}"), *FString::SanitizeFloat(Bad)), Names,
				{ Counts.FindRef(TEXT("R0")), Counts.FindRef(TEXT("R1")), Counts.FindRef(TEXT("R2")) }, { 0.5, 0.0, 0.5 }, 50000);
		}
		return true;
	}

	/** QA-19: a species allow-list restricts and renormalizes the tiers; empty = all; an unknown listed id never rolls */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityAllowListTest, "Project.Fish.QA.Rarity.SpeciesAllowListRespected", FISH_QA_FLAGS)
	bool FFishQARarityAllowListTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		SetRarities(T, { 70.0f, 25.0f, 5.0f });
		FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		Species->AllowedRarities = { TEXT("R0"), TEXT("R2") };
		int32 Failures = 0;
		TMap<FName, int32> Counts = CountRarities(T.Get(), TEXT("QA_Fixed"), 50000, 0.0f, Failures);
		CheckOdds(*this, TEXT("allow {R0,R2}"), { TEXT("R0"), TEXT("R1"), TEXT("R2") },
			{ Counts.FindRef(TEXT("R0")), Counts.FindRef(TEXT("R1")), Counts.FindRef(TEXT("R2")) }, { 70.0 / 75.0, 0.0, 5.0 / 75.0 }, 50000);

		Species->AllowedRarities.Reset();
		Counts = CountRarities(T.Get(), TEXT("QA_Fixed"), 50000, 0.0f, Failures);
		CheckOdds(*this, TEXT("empty allow-list = all"), { TEXT("R0"), TEXT("R1"), TEXT("R2") },
			{ Counts.FindRef(TEXT("R0")), Counts.FindRef(TEXT("R1")), Counts.FindRef(TEXT("R2")) }, { 0.70, 0.25, 0.05 }, 50000);

		Species->AllowedRarities = { TEXT("R1"), TEXT("QA_NoSuchTier") };
		Counts = CountRarities(T.Get(), TEXT("QA_Fixed"), 5000, 0.0f, Failures);
		TestEqual(TEXT("allow {R1, unknown}: every roll is R1"), Counts.FindRef(TEXT("R1")), 5000);
		TestEqual(TEXT("unknown id never rolls"), Counts.FindRef(TEXT("QA_NoSuchTier")), 0);
		return true;
	}

	/** QA-20: luck follows w' = w * (1 + Luck * Rank * LuckRankFactor): odds match at each level and the rarest tier rises */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityLuckFormulaTest, "Project.Fish.QA.Rarity.LuckMatchesFormula", FISH_QA_FLAGS)
	bool FFishQARarityLuckFormulaTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		SetRarities(T, { 70.0f, 25.0f, 5.0f, 0.0f });
		const FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		double PreviousRare = -1.0;
		double PreviousMeanRank = -1.0;
		const int32 N = 50000;
		for (float Luck : { 0.0f, 2.0f, 5.0f, 10.0f })
		{
			CheckRarityOdds(*this, FString::Printf(TEXT("luck %g"), Luck), T.Get(), TEXT("QA_Fixed"), N, Luck);

			// GetRarityWeights reports the same luck-adjusted weights, sorted by Rank
			const TArray<TPair<FName, float>> Weights = FFishRoll::GetRarityWeights(T.Get(), *Species, Luck);
			TestEqual(TEXT("GetRarityWeights lists every candidate"), Weights.Num(), 4);
			for (int32 i = 0; i < Weights.Num(); ++i)
			{
				const FFishRarityRow* Rarity = Row<FFishRarityRow>(T.Rarities, Weights[i].Key);
				const float Expected = Rarity->RollWeight * (1.0f + Luck * Rarity->Rank * T.Tuning.LuckRankFactor);
				TestTrue(FString::Printf(TEXT("GetRarityWeights %s at luck %g = %.4f"), *Weights[i].Key.ToString(), Luck, Expected), FMath::IsNearlyEqual(Weights[i].Value, Expected, 1e-3f));
				TestEqual(TEXT("sorted by rank"), Rarity->Rank, i);
			}

			int32 Failures = 0;
			const TMap<FName, int32> Counts = CountRarities(T.Get(), TEXT("QA_Fixed"), N, Luck, Failures);
			const double Rare = double(Counts.FindRef(TEXT("R2"))) / N;
			const double MeanRank = (Counts.FindRef(TEXT("R1")) + 2.0 * Counts.FindRef(TEXT("R2"))) / N;
			if (PreviousRare >= 0.0)
			{
				TestTrue(FString::Printf(TEXT("luck %g: P(rarest) rises (%.4f -> %.4f)"), Luck, PreviousRare, Rare), Rare > PreviousRare);
				TestTrue(FString::Printf(TEXT("luck %g: mean rank rises (%.4f -> %.4f)"), Luck, PreviousMeanRank, MeanRank), MeanRank > PreviousMeanRank);
			}
			PreviousRare = Rare;
			PreviousMeanRank = MeanRank;
		}
		return true;
	}

	/** QA-21: extreme luck is clamped (1e6 == MaxLuck, negative == 0, NaN == 0 with a warning) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityLuckExtremesTest, "Project.Fish.QA.Rarity.LuckExtremesSafe", FISH_QA_FLAGS)
	bool FFishQARarityLuckExtremesTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const float MaxLuck = Real.Tuning.MaxLuck;
		int32 HugeMismatch = 0, NegativeMismatch = 0, NaNMismatch = 0, InfBad = 0;
		FLogCapture Log;
		for (int32 Seed = 1; Seed <= 3000; ++Seed)
		{
			auto RollLuck = [&](float Luck)
			{
				FFishRollContext Context = Ctx(TEXT("CoralSnapper"), Seed);
				Context.Luck = Luck;
				FFishInstance Fish;
				FFishRoll::Roll(Real.Get(), Context, Fish);
				return Fish;
			};
			const FFishInstance AtZero = RollLuck(0.0f);
			const FFishInstance AtMax = RollLuck(MaxLuck);
			HugeMismatch += Same(RollLuck(1e6f), AtMax) ? 0 : 1;
			NegativeMismatch += Same(RollLuck(-5.0f), AtZero) ? 0 : 1;
			NaNMismatch += Same(RollLuck(std::numeric_limits<float>::quiet_NaN()), AtZero) ? 0 : 1;
			const FFishInstance AtInf = RollLuck(std::numeric_limits<float>::infinity());
			InfBad += (AtInf.IsValid() && AllFinite(AtInf) && (Same(AtInf, AtMax) || Same(AtInf, AtZero))) ? 0 : 1;
		}
		TestEqual(TEXT("luck 1e6 == MaxLuck"), HugeMismatch, 0);
		TestEqual(TEXT("luck -5 == 0"), NegativeMismatch, 0);
		TestEqual(TEXT("luck NaN == 0"), NaNMismatch, 0);
		TestEqual(TEXT("luck +Inf is valid and equals MaxLuck or 0"), InfBad, 0);
		TestTrue(TEXT("NaN luck warns"), Log.NumWarnings() > 0);
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** QA-22: for the same seed, more luck never gives a lower tier (follows from one draw over rank-sorted tiers) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityLuckPathwiseTest, "Project.Fish.QA.Rarity.LuckIsPathwiseMonotone", FISH_QA_FLAGS)
	bool FFishQARarityLuckPathwiseTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const FFishTables Tables = Real.Get();
		int32 Violations = 0;
		for (int32 Seed = 1; Seed <= 30000; ++Seed)
		{
			int32 PreviousRank = -1;
			for (float Luck : { 0.0f, 2.0f, 5.0f, 10.0f })
			{
				FFishRollContext Context = Ctx(TEXT("Bonefish"), Seed);
				Context.Luck = Luck;
				Context.bForceModifiers = true;
				FFishInstance Fish;
				FFishRoll::Roll(Tables, Context, Fish);
				const int32 Rank = RankOf(Tables, Fish.RarityId);
				if (Rank < PreviousRank && ++Violations <= 3)
				{
					AddError(FString::Printf(TEXT("seed %d: luck %g gave rank %d after rank %d"), Seed, Luck, Rank, PreviousRank));
				}
				PreviousRank = Rank;
			}
		}
		TestEqual(TEXT("seeds where more luck gave a lower tier"), Violations, 0);
		return true;
	}

	/** QA-23: luck changes only the rarity: same seed -> same modifiers and weight at luck 0 and 10 (real data) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityLuckScopeTest, "Project.Fish.QA.Rarity.LuckDoesNotChangeModifiers", FISH_QA_FLAGS)
	bool FFishQARarityLuckScopeTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		int32 Changed = 0;
		TMap<FString, int32> ModsAt0, ModsAt10;
		const int32 N = 20000;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FFishRollContext Context = Ctx(TEXT("Bonefish"), Seed);
			FFishInstance A, B;
			FFishRoll::Roll(Real.Get(), Context, A);
			Context.Luck = 10.0f;
			FFishRoll::Roll(Real.Get(), Context, B);
			Changed += (A.ModifierIds == B.ModifierIds) ? 0 : 1;
			for (const FName& Id : A.ModifierIds) ModsAt0.FindOrAdd(Id.ToString())++;
			for (const FName& Id : B.ModifierIds) ModsAt10.FindOrAdd(Id.ToString())++;
		}
		TestEqual(TEXT("modifier set per seed is the same at luck 0 and 10"), Changed, 0);
		for (const TPair<FString, int32>& Pair : ModsAt0)
		{
			TestEqual(FString::Printf(TEXT("%s frequency unchanged by luck"), *Pair.Key), ModsAt10.FindRef(Pair.Key), Pair.Value);
		}
		return true;
	}

	/** QA-24: with sequential seeds, the weight draw is independent of the rarity and modifier draws */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQARarityIndependenceTest, "Project.Fish.QA.Rarity.IndependentOfWeightAcrossSequentialSeeds", FISH_QA_FLAGS)
	bool FFishQARarityIndependenceTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const FFishTables Tables = Real.Get();
		const FFishSpeciesRow* Species = Row<FFishSpeciesRow>(Real.Species, TEXT("Bonefish"));
		const int32 N = 100000;
		TMap<int32, double> SumU;
		TMap<int32, int32> CountU;
		double SumAll = 0.0, SumRank = 0.0, SumRankSq = 0.0, SumUSq = 0.0, SumRankU = 0.0;
		double FeistyU = 0.0, OtherU = 0.0;
		int32 FeistyN = 0, OtherN = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FFishRollContext Context = Ctx(TEXT("Bonefish"), Seed);
			FFishInstance Fish;
			FFishRoll::Roll(Tables, Context, Fish);
			FFishRollContext BaseOnly = Context;
			BaseOnly.bForceModifiers = true;
			FFishInstance Base;
			FFishRoll::Roll(Tables, BaseOnly, Base);
			// invert the documented weight curve to recover the uniform draw
			const double Norm = FMath::Clamp((double(Base.WeightKg) - Species->WeightMin) / (Species->WeightMax - Species->WeightMin), 0.0, 1.0);
			const double U = FMath::Pow(Norm, 1.0 / Species->SizeSkew);
			const int32 Rank = RankOf(Tables, Fish.RarityId);
			SumU.FindOrAdd(Rank) += U;
			CountU.FindOrAdd(Rank)++;
			SumAll += U;
			SumUSq += U * U;
			SumRank += Rank;
			SumRankSq += double(Rank) * Rank;
			SumRankU += Rank * U;
			if (Fish.HasModifier(TEXT("Feisty")))
			{
				FeistyU += U;
				++FeistyN;
			}
			else
			{
				OtherU += U;
				++OtherN;
			}
		}
		const double SdU = 1.0 / FMath::Sqrt(12.0);
		TestTrue(FString::Printf(TEXT("overall mean U %.4f ~ 0.5"), SumAll / N), FMath::Abs(SumAll / N - 0.5) <= 4.5 * SdU / FMath::Sqrt(double(N)));
		for (const TPair<int32, int32>& Pair : CountU)
		{
			const double Mean = SumU[Pair.Key] / Pair.Value;
			const double Tol = 4.5 * SdU / FMath::Sqrt(double(Pair.Value));
			TestTrue(FString::Printf(TEXT("rank %d (n=%d): mean weight draw %.4f within %.4f of 0.5"), Pair.Key, Pair.Value, Mean, Tol), FMath::Abs(Mean - 0.5) <= Tol);
		}
		const double Cov = SumRankU / N - (SumRank / N) * (SumAll / N);
		const double VarRank = SumRankSq / N - FMath::Square(SumRank / N);
		const double VarU = SumUSq / N - FMath::Square(SumAll / N);
		const double R = Cov / FMath::Sqrt(FMath::Max(VarRank * VarU, 1e-12));
		TestTrue(FString::Printf(TEXT("|corr(rank, weight draw)| = %.4f <= %.4f"), FMath::Abs(R), 4.5 / FMath::Sqrt(double(N))), FMath::Abs(R) <= 4.5 / FMath::Sqrt(double(N)));
		if (FeistyN > 0 && OtherN > 0)
		{
			const double Tol = 4.5 * SdU / FMath::Sqrt(double(FeistyN));
			TestTrue(FString::Printf(TEXT("Feisty catches (n=%d): mean weight draw %.4f within %.4f of 0.5"), FeistyN, FeistyU / FeistyN, Tol), FMath::Abs(FeistyU / FeistyN - 0.5) <= Tol);
		}
		return true;
	}

	// ================================================================================================================
	// Weight (QA-25..QA-30) and the pure weight helpers
	// ================================================================================================================

	/** QA-25: the base weight roll never leaves [WeightMin, WeightMax] and equals the Weight stat */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAWeightRangeTest, "Project.Fish.QA.Weight.WithinSpeciesRange", FISH_QA_FLAGS)
	bool FFishQAWeightRangeTest::RunTest(const FString& Parameters)
	{
		auto CheckRange = [this](const FString& What, const FFishTables& Tables, FName SpeciesId, int32 N)
		{
			const FFishSpeciesRow* Species = Tables.Species->FindRow<FFishSpeciesRow>(SpeciesId, TEXT("QA"), false);
			int32 Outside = 0, Mismatch = 0, Failed = 0;
			float Lo = TNumericLimits<float>::Max(), Hi = -TNumericLimits<float>::Max();
			for (int32 Seed = 1; Seed <= N; ++Seed)
			{
				FFishRollContext Context = Ctx(SpeciesId, Seed);
				Context.bForceModifiers = true; // base roll only
				FFishInstance Fish;
				if (!FFishRoll::Roll(Tables, Context, Fish))
				{
					++Failed;
					continue;
				}
				Outside += (FMath::IsFinite(Fish.WeightKg) && Fish.WeightKg >= Species->WeightMin && Fish.WeightKg <= Species->WeightMax && Fish.WeightKg > 0.0f) ? 0 : 1;
				Mismatch += Fish.GetStat(Tag(TEXT("Fish.Stat.Weight")), -1.0f) == Fish.WeightKg ? 0 : 1;
				Lo = FMath::Min(Lo, Fish.WeightKg);
				Hi = FMath::Max(Hi, Fish.WeightKg);
			}
			TestEqual(What + TEXT(": rolls succeed"), Failed, 0);
			TestEqual(FString::Printf(TEXT("%s: weights outside [%g, %g] (seen %g..%g)"), *What, Species->WeightMin, Species->WeightMax, Lo, Hi), Outside, 0);
			TestEqual(What + TEXT(": WeightKg == GetStat(Fish.Stat.Weight)"), Mismatch, 0);
		};

		FTables Real;
		if (LoadReal(*this, Real))
		{
			for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
			{
				CheckRange(SpeciesId.ToString(), Real.Get(), SpeciesId, 100000);
			}
		}
		struct FRangeCase { float Min, Max, Skew; };
		for (const FRangeCase& Case : { FRangeCase{ 0.05f, 0.1f, 1.0f }, FRangeCase{ 1.0f, 1000.0f, 1.0f }, FRangeCase{ 2.5f, 2.5f, 1.0f }, FRangeCase{ 1.0f, 5.0f, 0.3f }, FRangeCase{ 1.0f, 5.0f, 4.0f } })
		{
			FTables T;
			MakeIdentity(T);
			FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
			Species->WeightMin = Case.Min;
			Species->WeightMax = Case.Max;
			Species->SizeSkew = Case.Skew;
			CheckRange(FString::Printf(TEXT("fixture [%g, %g] skew %g"), Case.Min, Case.Max, Case.Skew), T.Get(), TEXT("QA_Fixed"), 30000);
		}
		return true;
	}

	/** QA-26: the base roll reaches both ends of the range (within 1% of the range) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAWeightCoverageTest, "Project.Fish.QA.Weight.CoversRange", FISH_QA_FLAGS)
	bool FFishQAWeightCoverageTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			const FFishSpeciesRow* Species = Row<FFishSpeciesRow>(Real.Species, SpeciesId);
			float Lo = TNumericLimits<float>::Max(), Hi = -TNumericLimits<float>::Max();
			for (int32 Seed = 1; Seed <= 100000; ++Seed)
			{
				FFishRollContext Context = Ctx(SpeciesId, Seed);
				Context.bForceModifiers = true;
				FFishInstance Fish;
				FFishRoll::Roll(Real.Get(), Context, Fish);
				Lo = FMath::Min(Lo, Fish.WeightKg);
				Hi = FMath::Max(Hi, Fish.WeightKg);
			}
			const float Range = Species->WeightMax - Species->WeightMin;
			TestTrue(FString::Printf(TEXT("%s: lightest %g within 1%% of min %g"), *SpeciesId.ToString(), Lo, Species->WeightMin), Lo <= Species->WeightMin + 0.01f * Range);
			TestTrue(FString::Printf(TEXT("%s: heaviest %g within 1%% of max %g"), *SpeciesId.ToString(), Hi, Species->WeightMax), Hi >= Species->WeightMax - 0.01f * Range);
		}
		return true;
	}

	/** QA-27: the weight distribution matches the documented CDF ((w - min) / range) ^ (1 / SizeSkew) (KS, alpha 1e-4) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAWeightDistributionTest, "Project.Fish.QA.Weight.DistributionMatchesSpec", FISH_QA_FLAGS)
	bool FFishQAWeightDistributionTest::RunTest(const FString& Parameters)
	{
		auto CheckKs = [this](const FString& What, const FFishTables& Tables, FName SpeciesId, int32 N)
		{
			const FFishSpeciesRow* Species = Tables.Species->FindRow<FFishSpeciesRow>(SpeciesId, TEXT("QA"), false);
			TArray<double> Samples;
			Samples.Reserve(N);
			double Sum = 0.0;
			for (int32 Seed = 1; Seed <= N; ++Seed)
			{
				FFishRollContext Context = Ctx(SpeciesId, Seed);
				Context.bForceModifiers = true;
				FFishInstance Fish;
				FFishRoll::Roll(Tables, Context, Fish);
				Samples.Add(Fish.WeightKg);
				Sum += Fish.WeightKg;
			}
			Samples.Sort();
			const double Range = Species->WeightMax - Species->WeightMin;
			double D = 0.0;
			for (int32 i = 0; i < N; ++i)
			{
				const double F = FMath::Pow(FMath::Clamp((Samples[i] - Species->WeightMin) / Range, 0.0, 1.0), 1.0 / Species->SizeSkew);
				D = FMath::Max(D, FMath::Max(double(i + 1) / N - F, F - double(i) / N));
			}
			const double Critical = FMath::Sqrt(FMath::Loge(2.0 / 1e-4) / (2.0 * N));
			TestTrue(FString::Printf(TEXT("%s: KS D = %.5f <= %.5f"), *What, D, Critical), D <= Critical);
			if (Species->SizeSkew == 1.0f)
			{
				const double Mean = Sum / N;
				const double Expected = Species->WeightMin + 0.5 * Range;
				const double Tol = 4.5 * Range / FMath::Sqrt(12.0) / FMath::Sqrt(double(N));
				TestTrue(FString::Printf(TEXT("%s: uniform mean %.4f within %.4f of %.4f"), *What, Mean, Tol, Expected), FMath::Abs(Mean - Expected) <= Tol);
			}
		};

		FTables Real;
		if (LoadReal(*this, Real))
		{
			for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
			{
				CheckKs(SpeciesId.ToString(), Real.Get(), SpeciesId, 100000);
			}
		}
		for (float Skew : { 1.0f, 0.5f, 3.0f })
		{
			FTables T;
			MakeIdentity(T);
			FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
			Species->WeightMin = 1.0f;
			Species->WeightMax = 11.0f;
			Species->SizeSkew = Skew;
			CheckKs(FString::Printf(TEXT("fixture skew %g"), Skew), T.Get(), TEXT("QA_Fixed"), 50000);
		}
		return true;
	}

	/** QA-28: WeightMin == WeightMax gives exactly that weight and finite stats and value (no divide by zero) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAWeightFixedTest, "Project.Fish.QA.Weight.MinEqualsMaxIsFinite", FISH_QA_FLAGS)
	bool FFishQAWeightFixedTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		Species->WeightMin = 2.5f;
		Species->WeightMax = 2.5f;
		Species->ReferenceWeight = 1.0f;
		int32 Bad = 0;
		for (int32 Seed = 1; Seed <= 1000; ++Seed)
		{
			FFishInstance Fish;
			const bool bOk = FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), Seed), Fish);
			Bad += (bOk && Fish.WeightKg == 2.5f && AllFinite(Fish) && Fish.Value == 25) ? 0 : 1;
		}
		TestEqual(TEXT("fixed 2.5 kg: weight exactly 2.5, finite, value round(10 * 2.5) = 25"), Bad, 0);
		FFishInstance Fish;
		FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), 1), Fish);
		TestTrue(TEXT("difficulty stats scaled by (2.5 / 1)^0.5"), FMath::IsNearlyEqual(StatOf(Fish, TEXT("Fish.Stat.Strength")), 10.0f * FMath::Sqrt(2.5f), 1e-3f));
		return true;
	}

	/** QA-29: a bad range at runtime (min > max, min 0 or negative) never produces a weight outside the range or <= 0 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAWeightInvalidRangeTest, "Project.Fish.QA.Weight.InvalidRangeGraceful", FISH_QA_FLAGS)
	bool FFishQAWeightInvalidRangeTest::RunTest(const FString& Parameters)
	{
		FLogCapture Log;
		struct FBad { float Min, Max; };
		for (const FBad& Case : { FBad{ 5.0f, 2.0f }, FBad{ 0.0f, 1.0f }, FBad{ -1.0f, 1.0f } })
		{
			FTables T;
			MakeIdentity(T);
			FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
			Species->WeightMin = Case.Min;
			Species->WeightMax = Case.Max;
			Species->ReferenceWeight = 1.0f;
			int32 Bad = 0;
			for (int32 Seed = 1; Seed <= 2000; ++Seed)
			{
				FFishInstance Fish;
				if (FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), Seed), Fish))
				{
					const bool bInRange = Fish.WeightKg >= FMath::Min(Case.Min, Case.Max) - 1e-4f && Fish.WeightKg <= FMath::Max(Case.Min, Case.Max) + 1e-4f;
					Bad += (AllFinite(Fish) && Fish.WeightKg > 0.0f && (bInRange || Case.Min <= 0.0f) && Fish.Value >= 1) ? 0 : 1;
				}
			}
			TestEqual(FString::Printf(TEXT("range [%g, %g]: every successful roll is finite, > 0 and in range"), Case.Min, Case.Max), Bad, 0);
		}
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** QA-30: weight modifiers multiply the final weight, may pass WeightMax (record fish), are clamped by the Weight stat, and drive value */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAWeightModifierTest, "Project.Fish.QA.Weight.ModifierWeightRule", FISH_QA_FLAGS)
	bool FFishQAWeightModifierTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		Species->WeightMin = 1.0f;
		Species->WeightMax = 2.0f;
		Species->ReferenceWeight = 1.0f;
		T.Modifiers->AddRow(TEXT("QA_Giant"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Weight"), 3.0f) }));
		FFishRollContext Context = Ctx(TEXT("QA_Fixed"), 1);
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = 1.0f;
		FFishInstance Fish;
		FFishRoll::Roll(T.Get(), Context, Fish);
		TestTrue(TEXT("x3 at WeightMax 2 -> 6 kg (past WeightMax)"), FMath::IsNearlyEqual(Fish.WeightKg, 6.0f, 1e-4f));
		TestEqual(TEXT("value uses the final weight: round(10 * 6) = 60"), Fish.Value, 60);

		// Clamp at the Weight stat's Max
		FTables Big;
		MakeIdentity(Big);
		MakeStandardStats(Big, 1000.0f);
		FFishSpeciesRow* Heavy = Row<FFishSpeciesRow>(Big.Species, TEXT("QA_Fixed"));
		Heavy->WeightMin = 900.0f;
		Heavy->WeightMax = 900.0f;
		Heavy->ReferenceWeight = 900.0f;
		Big.Modifiers->AddRow(TEXT("QA_Giant"), MakeModifier(1.0f, NAME_None, { MulMod(TEXT("Fish.Stat.Weight"), 3.0f) }));
		FFishInstance Clamped;
		FFishRoll::Roll(Big.Get(), Ctx(TEXT("QA_Fixed"), 1), Clamped);
		TestTrue(FString::Printf(TEXT("900 x 3 clamps to the Weight stat Max 1000 (got %g)"), Clamped.WeightKg), FMath::IsNearlyEqual(Clamped.WeightKg, 1000.0f, 1e-2f));
		TestEqual(TEXT("value from the clamped weight: 10 * 1000"), Clamped.Value, 10000);

		// Starter data: forced Giant on a max-size Bonefish passes WeightMax
		FTables Real;
		if (LoadReal(*this, Real))
		{
			const FFishSpeciesRow* Bonefish = Row<FFishSpeciesRow>(Real.Species, TEXT("Bonefish"));
			FFishRollContext Giant = Ctx(TEXT("Bonefish"), 2);
			Giant.ForcedRarityId = TEXT("Common");
			Giant.bForceModifiers = true;
			Giant.ForcedModifierIds = { TEXT("Giant") };
			Giant.bForceWeightFraction = true;
			Giant.ForcedWeightFraction = 1.0f;
			FFishInstance Record;
			FFishRoll::Roll(Real.Get(), Giant, Record);
			TestTrue(FString::Printf(TEXT("Giant Bonefish %g kg > WeightMax %g"), Record.WeightKg, Bonefish->WeightMax), Record.WeightKg > Bonefish->WeightMax);
		}
		return true;
	}

	/** S4 pure helpers: SampleWeight hits the ends exactly, follows U^SizeSkew and clamps U; WeightStatFactor is 1 at the reference */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAWeightHelpersTest, "Project.Fish.QA.Weight.SampleWeightEdges", FISH_QA_FLAGS)
	bool FFishQAWeightHelpersTest::RunTest(const FString& Parameters)
	{
		FFishSpeciesRow Species = MakeSpecies(1.0f, 5.0f);
		Species.SizeSkew = 2.0f;
		TestEqual(TEXT("U=0 -> WeightMin exactly"), FFishRoll::SampleWeight(Species, 0.0f), 1.0f);
		TestEqual(TEXT("U=1 -> WeightMax exactly"), FFishRoll::SampleWeight(Species, 1.0f), 5.0f);
		TestEqual(TEXT("U=-1 clamps to WeightMin"), FFishRoll::SampleWeight(Species, -1.0f), 1.0f);
		TestEqual(TEXT("U=2 clamps to WeightMax"), FFishRoll::SampleWeight(Species, 2.0f), 5.0f);
		TestTrue(TEXT("U=0.25, skew 2 -> 1 + 4 * 0.0625 = 1.25"), FMath::IsNearlyEqual(FFishRoll::SampleWeight(Species, 0.25f), 1.25f, 1e-5f));
		const float NaNWeight = FFishRoll::SampleWeight(Species, std::numeric_limits<float>::quiet_NaN());
		TestTrue(FString::Printf(TEXT("U=NaN stays finite and in range (got %g)"), NaNWeight), FMath::IsFinite(NaNWeight) && NaNWeight >= 1.0f && NaNWeight <= 5.0f);
		float Previous = 0.0f;
		bool bMonotone = true;
		for (int32 i = 0; i <= 1000; ++i)
		{
			const float W = FFishRoll::SampleWeight(Species, i / 1000.0f);
			bMonotone &= W >= Previous;
			Previous = W;
		}
		TestTrue(TEXT("SampleWeight is non-decreasing in U"), bMonotone);

		Species.ReferenceWeight = 2.0f;
		Species.WeightStatExponent = 0.5f;
		TestEqual(TEXT("factor 1 at the reference weight"), FFishRoll::WeightStatFactor(Species, 2.0f), 1.0f);
		TestTrue(TEXT("factor (8 / 2)^0.5 = 2"), FMath::IsNearlyEqual(FFishRoll::WeightStatFactor(Species, 8.0f), 2.0f, 1e-5f));
		Species.WeightStatExponent = 0.0f;
		TestEqual(TEXT("exponent 0 -> factor 1"), FFishRoll::WeightStatFactor(Species, 8.0f), 1.0f);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

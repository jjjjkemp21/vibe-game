// QA-owned, independent tests for T-008 value/XP/difficulty monotonicity and the FFishInstance API.
// Written by the qa-engineer from docs/specs/fish-system-rules.md, Fish/FishRoll.h and Fish/FishInstance.h (black-box).
// Jimmy's requirement: rarity raises value, weight raises value, and the record carries everything.

#include "FishQATestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FishQA_Value
{
	using namespace FishQA;

	/** Rarity ids of a table sorted by Rank */
	static TArray<FName> RaritiesByRank(const FTables& T)
	{
		TArray<FName> Ids;
		T.Rarities->GetRowMap().GetKeys(Ids);
		Ids.Sort([&T](const FName& A, const FName& B)
		{
			return Row<FFishRarityRow>(T.Rarities, A)->Rank < Row<FFishRarityRow>(T.Rarities, B)->Rank;
		});
		return Ids;
	}

	static FFishInstance RollForced(const FTables& T, FName SpeciesId, FName RarityId, float Fraction, int32 Seed = 1)
	{
		FFishRollContext Context = Ctx(SpeciesId, Seed);
		Context.ForcedRarityId = RarityId;
		Context.bForceModifiers = true;
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = Fraction;
		FFishInstance Fish;
		FFishRoll::Roll(T.Get(), Context, Fish);
		return Fish;
	}

	// ================================================================================================================
	// Value, XP and difficulty monotonicity (QA-57..QA-62)
	// ================================================================================================================

	/** QA-57: at the same weight and modifiers, a higher rarity is worth strictly more (starter data, every tier incl. staged ones) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAValueRarityTest, "Project.Fish.QA.Value.RarityUpValueUp", FISH_QA_FLAGS)
	bool FFishQAValueRarityTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const TArray<FName> Tiers = RaritiesByRank(Real);
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			for (float Fraction : { 0.0f, 0.5f, 1.0f })
			{
				int32 Previous = -1;
				for (const FName& Tier : Tiers)
				{
					const FFishInstance Fish = RollForced(Real, SpeciesId, Tier, Fraction);
					TestTrue(FString::Printf(TEXT("%s %s at %.1f: value %d > %d"), *SpeciesId.ToString(), *Tier.ToString(), Fraction, Fish.Value, Previous), Fish.Value > Previous);
					Previous = Fish.Value;
				}
			}
		}
		// Fixture with ascending multipliers: value ratio equals the multiplier ratio exactly (value is linear in it)
		FTables T;
		MakeIdentity(T);
		ResetRarities(T);
		const float Multipliers[] = { 1.0f, 1.5f, 2.5f, 5.0f };
		for (int32 i = 0; i < 4; ++i)
		{
			T.Rarities->AddRow(FName(*FString::Printf(TEXT("R%d"), i)), MakeRarity(i, 10.0f - i, Multipliers[i]));
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const FFishInstance Fish = RollForced(T, TEXT("QA_Fixed"), FName(*FString::Printf(TEXT("R%d"), i)), 0.0f);
			TestEqual(FString::Printf(TEXT("R%d: value = round(10 * 2 * %g)"), i, Multipliers[i]), Fish.Value, int32(FMath::FloorToDouble(20.0 * Multipliers[i] + 0.5)));
		}
		return true;
	}

	/** QA-58: at the same rarity and modifiers, value never decreases with weight, and the heaviest is worth more than the lightest */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAValueWeightTest, "Project.Fish.QA.Value.WeightUpValueUp", FISH_QA_FLAGS)
	bool FFishQAValueWeightTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			for (const FName& Tier : RaritiesByRank(Real))
			{
				int32 Previous = 0;
				int32 First = 0;
				bool bMonotone = true;
				for (int32 Step = 0; Step <= 100; ++Step)
				{
					const FFishInstance Fish = RollForced(Real, SpeciesId, Tier, Step / 100.0f);
					if (Step == 0)
					{
						First = Fish.Value;
					}
					bMonotone &= Fish.Value >= Previous;
					Previous = Fish.Value;
				}
				TestTrue(FString::Printf(TEXT("%s %s: value non-decreasing in weight"), *SpeciesId.ToString(), *Tier.ToString()), bMonotone);
				TestTrue(FString::Printf(TEXT("%s %s: heaviest (%d) worth more than lightest (%d)"), *SpeciesId.ToString(), *Tier.ToString(), Previous, First), Previous > First);
			}
		}
		return true;
	}

	/** QA-59: over 100k natural rolls, a catch that is at least as rare AND at least as heavy (same species and modifiers) is never worth less */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAValueDominanceTest, "Project.Fish.QA.Value.DominanceOnRealData", FISH_QA_FLAGS)
	bool FFishQAValueDominanceTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		struct FEntry { float Weight; int32 Rank; int32 Value; int32 Xp; int32 Seed; };
		TMap<FString, TArray<FEntry>> Groups;
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			for (int32 Seed = 1; Seed <= 50000; ++Seed)
			{
				FFishRollContext Context = Ctx(SpeciesId, Seed);
				Context.Luck = float(Seed % 3) * 5.0f; // reach all enabled tiers often
				FFishInstance Fish;
				FFishRoll::Roll(Real.Get(), Context, Fish);
				Groups.FindOrAdd(SpeciesId.ToString() + TEXT("/") + ModsKey(Fish)).Add({ Fish.WeightKg, Row<FFishRarityRow>(Real.Rarities, Fish.RarityId)->Rank, Fish.Value, Fish.Xp, Seed });
			}
		}
		int32 Violations = 0;
		for (TPair<FString, TArray<FEntry>>& Group : Groups)
		{
			Group.Value.Sort([](const FEntry& A, const FEntry& B) { return A.Weight != B.Weight ? A.Weight < B.Weight : A.Rank < B.Rank; });
			TMap<int32, int32> MaxValueAtRank; // running max over lighter-or-equal catches, per rank
			for (const FEntry& Entry : Group.Value)
			{
				int32 Dominated = 0;
				for (const TPair<int32, int32>& Pair : MaxValueAtRank)
				{
					if (Pair.Key <= Entry.Rank)
					{
						Dominated = FMath::Max(Dominated, Pair.Value);
					}
				}
				if (Entry.Value < Dominated && ++Violations <= 5)
				{
					AddError(FString::Printf(TEXT("%s seed %d: %.3f kg rank %d worth %d < %d of a lighter, not rarer catch"), *Group.Key, Entry.Seed, Entry.Weight, Entry.Rank, Entry.Value, Dominated));
				}
				int32& Max = MaxValueAtRank.FindOrAdd(Entry.Rank);
				Max = FMath::Max(Max, Entry.Value);
			}
		}
		TestEqual(TEXT("dominance violations (value)"), Violations, 0);
		return true;
	}

	/** QA-60: value, XP and stats depend only on the rolled record, not on the seed */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAValueRecordOnlyTest, "Project.Fish.QA.Value.DependsOnlyOnRecord", FISH_QA_FLAGS)
	bool FFishQAValueRecordOnlyTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T); // fixed weight
		ResetRarities(T);
		T.Rarities->AddRow(TEXT("R0"), MakeRarity(0, 60.0f, 1.0f));
		T.Rarities->AddRow(TEXT("R1"), MakeRarity(1, 40.0f, 2.0f, 1.5f, 1));
		T.Modifiers->AddRow(TEXT("QA_A"), MakeModifier(0.5f, NAME_None, { AddMod(TEXT("Fish.Stat.Strength"), 3.0f) }, 1.5f));
		T.Modifiers->AddRow(TEXT("QA_B"), MakeModifier(0.5f, NAME_None, { MulMod(TEXT("Fish.Stat.Stamina"), 1.5f) }, 1.25f));
		TMap<FString, FFishInstance> FirstOfKind;
		int32 Mismatches = 0;
		for (int32 Seed = 1; Seed <= 5000; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), Seed), Fish);
			const FString Key = Fish.RarityId.ToString() + TEXT("/") + ModsKey(Fish);
			if (FFishInstance* First = FirstOfKind.Find(Key))
			{
				FFishInstance Copy = Fish;
				Copy.Seed = First->Seed;
				Mismatches += Same(Copy, *First) ? 0 : 1;
			}
			else
			{
				FirstOfKind.Add(Key, Fish);
			}
		}
		TestEqual(TEXT("kinds seen (2 tiers x 4 modifier sets)"), FirstOfKind.Num(), 8);
		TestEqual(TEXT("same record -> same value, XP and stats"), Mismatches, 0);
		return true;
	}

	/** QA-61 (P2, starter content): at the same weight, difficulty stats and DifficultyRating never drop as rarity rises */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAValueDifficultyTest, "Project.Fish.QA.Value.DifficultyRisesWithRarity", FISH_QA_FLAGS)
	bool FFishQAValueDifficultyTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TArray<FGameplayTag> DifficultyStats;
		Real.Stats->ForeachRow<FFishStatRow>(TEXT("QA"), [&](const FName&, const FFishStatRow& Stat)
		{
			if (Stat.bIsDifficultyStat)
			{
				DifficultyStats.Add(Stat.Tag);
			}
		});
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			FFishInstance Previous;
			bool bFirst = true;
			for (const FName& Tier : RaritiesByRank(Real))
			{
				const FFishInstance Fish = RollForced(Real, SpeciesId, Tier, 0.5f);
				if (!bFirst)
				{
					for (const FGameplayTag& Stat : DifficultyStats)
					{
						TestTrue(FString::Printf(TEXT("%s %s: %s %g >= %g"), *SpeciesId.ToString(), *Tier.ToString(), *Stat.ToString(), Fish.GetStat(Stat), Previous.GetStat(Stat)), Fish.GetStat(Stat) >= Previous.GetStat(Stat));
					}
					TestTrue(FString::Printf(TEXT("%s %s: DifficultyRating %g >= %g"), *SpeciesId.ToString(), *Tier.ToString(), Fish.DifficultyRating, Previous.DifficultyRating), Fish.DifficultyRating >= Previous.DifficultyRating);
				}
				Previous = Fish;
				bFirst = false;
			}
		}
		return true;
	}

	/** QA-62 (P2): XP follows round-half-up((XpBase + XpPerLevel * (Level - 1)) * XpMultiplier) and never drops as rarity rises */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAValueXpTest, "Project.Fish.QA.Value.XpFormulaAndMonotone", FISH_QA_FLAGS)
	bool FFishQAValueXpTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			const FFishSpeciesRow* Species = Row<FFishSpeciesRow>(Real.Species, SpeciesId);
			int32 Previous = -1;
			for (const FName& Tier : RaritiesByRank(Real))
			{
				const FFishRarityRow* Rarity = Row<FFishRarityRow>(Real.Rarities, Tier);
				const FFishInstance Fish = RollForced(Real, SpeciesId, Tier, 0.3f);
				const int32 Level = Species->BaseLevel + Rarity->LevelBonus;
				const int32 Expected = int32(FMath::FloorToDouble((double(Real.Tuning.XpBase) + double(Real.Tuning.XpPerLevel) * (Level - 1)) * Rarity->XpMultiplier + 0.5));
				TestEqual(FString::Printf(TEXT("%s %s: XP"), *SpeciesId.ToString(), *Tier.ToString()), Fish.Xp, Expected);
				TestEqual(TEXT("ComputeXp agrees"), FFishRoll::ComputeXp(Real.Tuning, Level, Rarity->XpMultiplier), Expected);
				TestTrue(FString::Printf(TEXT("%s %s: XP %d >= %d"), *SpeciesId.ToString(), *Tier.ToString(), Fish.Xp, Previous), Fish.Xp >= Previous);
				Previous = Fish.Xp;
			}
		}
		TestEqual(TEXT("ComputeXp never negative"), FFishRoll::ComputeXp(Real.Tuning, -50, 1.0f) >= 0, true);
		return true;
	}

	/** QA-56: value is round-half-up (not banker's rounding) with a minimum of 1 coin */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAValueRoundingTest, "Project.Fish.QA.Value.RoundsHalfUpMinimumOne", FISH_QA_FLAGS)
	bool FFishQAValueRoundingTest::RunTest(const FString& Parameters)
	{
		struct FCase { float Weight; float PerKg; int32 Expected; };
		for (const FCase& Case : { FCase{ 2.5f, 1.0f, 3 }, FCase{ 3.5f, 1.0f, 4 }, FCase{ 4.5f, 1.0f, 5 }, FCase{ 1.25f, 2.0f, 3 },
			FCase{ 2.25f, 1.0f, 2 }, FCase{ 0.5f, 1.0f, 1 }, FCase{ 0.25f, 1.0f, 1 }, FCase{ 0.01f, 1.0f, 1 }, FCase{ 3.0f, 0.0f, 1 } })
		{
			FTables T;
			MakeIdentity(T);
			FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
			Species->WeightMin = Case.Weight;
			Species->WeightMax = Case.Weight;
			Species->ReferenceWeight = Case.Weight;
			Species->BaseValuePerKg = Case.PerKg;
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), 1), Fish);
			TestEqual(FString::Printf(TEXT("%g kg x %g coins/kg"), Case.Weight, Case.PerKg), Fish.Value, Case.Expected);
		}
		return true;
	}

	// ================================================================================================================
	// Instance API (QA-63..QA-69)
	// ================================================================================================================

	/** QA-63: GetStat is an exact tag match with a caller default; HasStat; the Blueprint wrapper agrees */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAInstanceGetStatTest, "Project.Fish.QA.Instance.GetStat", FISH_QA_FLAGS)
	bool FFishQAInstanceGetStatTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FFishInstance Fish;
		FFishRoll::Roll(Real.Get(), Ctx(TEXT("CoralSnapper"), 77), Fish);
		for (const FFishStatValue& Stat : Fish.Stats)
		{
			TestEqual(FString::Printf(TEXT("GetStat(%s)"), *Stat.Tag.ToString()), Fish.GetStat(Stat.Tag), Stat.Value);
			TestTrue(TEXT("HasStat"), Fish.HasStat(Stat.Tag));
			TestEqual(TEXT("UFishLibrary::GetFishStat agrees"), UFishLibrary::GetFishStat(Fish, Stat.Tag, -1.0f), Stat.Value);
		}
		TestEqual(TEXT("parent tag Fish.Stat doesn't match children (exact match)"), Fish.GetStat(Tag(TEXT("Fish.Stat")), -7.0f), -7.0f);
		TestFalse(TEXT("HasStat(Fish.Stat) is false"), Fish.HasStat(Tag(TEXT("Fish.Stat"))));
		TestEqual(TEXT("a registered stat the fish lacks returns the default"), Fish.GetStat(Tag(QAStat), 3.5f), 3.5f);
		TestEqual(TEXT("default parameter is 0"), Fish.GetStat(Tag(QAStat)), 0.0f);
		TestEqual(TEXT("an empty tag returns the default"), Fish.GetStat(FGameplayTag(), 2.0f), 2.0f);
		TestFalse(TEXT("HasStat(empty) is false"), Fish.HasStat(FGameplayTag()));
		TestEqual(TEXT("GetStat(Weight) == WeightKg"), Fish.GetStat(Tag(TEXT("Fish.Stat.Weight"))), Fish.WeightKg);
		return true;
	}

	/** QA-64: HasModifier; the Blueprint wrapper agrees */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAInstanceHasModifierTest, "Project.Fish.QA.Instance.HasModifier", FISH_QA_FLAGS)
	bool FFishQAInstanceHasModifierTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FFishRollContext Context = Ctx(TEXT("Bonefish"), 1);
		Context.bForceModifiers = true;
		Context.ForcedModifierIds = { TEXT("Feisty"), TEXT("Albino") };
		FFishInstance Fish;
		FFishRoll::Roll(Real.Get(), Context, Fish);
		TestTrue(TEXT("has Feisty"), Fish.HasModifier(TEXT("Feisty")));
		TestTrue(TEXT("has Albino"), Fish.HasModifier(TEXT("Albino")));
		TestTrue(TEXT("FName is case-insensitive: albino"), Fish.HasModifier(TEXT("albino")));
		TestFalse(TEXT("not Giant"), Fish.HasModifier(TEXT("Giant")));
		TestFalse(TEXT("not None"), Fish.HasModifier(NAME_None));
		TestTrue(TEXT("UFishLibrary::FishHasModifier agrees (true)"), UFishLibrary::FishHasModifier(Fish, TEXT("Feisty")));
		TestFalse(TEXT("UFishLibrary::FishHasModifier agrees (false)"), UFishLibrary::FishHasModifier(Fish, TEXT("Heavy")));
		return true;
	}

	/** QA-65: a default instance is an empty slot, never a fish */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAInstanceDefaultTest, "Project.Fish.QA.Instance.DefaultIsEmpty", FISH_QA_FLAGS)
	bool FFishQAInstanceDefaultTest::RunTest(const FString& Parameters)
	{
		const FFishInstance Empty;
		TestFalse(TEXT("not valid"), Empty.IsValid());
		TestEqual(TEXT("no species"), Empty.SpeciesId, FName(NAME_None));
		TestEqual(TEXT("value 0"), Empty.Value, 0);
		TestEqual(TEXT("xp 0"), Empty.Xp, 0);
		TestEqual(TEXT("no stats"), Empty.Stats.Num(), 0);
		TestEqual(TEXT("GetStat default"), Empty.GetStat(Tag(TEXT("Fish.Stat.Strength"))), 0.0f);
		TestFalse(TEXT("HasModifier false"), Empty.HasModifier(TEXT("Feisty")));
		TestFalse(TEXT("ToString doesn't crash and isn't empty"), Empty.ToString().IsEmpty());
		TestFalse(TEXT("FishToString doesn't crash"), UFishLibrary::FishToString(Empty).IsEmpty());
		return true;
	}

	/** QA-66: every registered stat appears exactly once, sorted by tag name (lexical), nothing else */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAInstanceStatsTest, "Project.Fish.QA.Instance.StatsCoverRegistry", FISH_QA_FLAGS)
	bool FFishQAInstanceStatsTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TArray<FName> Expected;
		Real.Stats->ForeachRow<FFishStatRow>(TEXT("QA"), [&](const FName&, const FFishStatRow& Stat) { Expected.Add(Stat.Tag.GetTagName()); });
		Expected.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		int32 Bad = 0;
		for (int32 Seed = 1; Seed <= 2000; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(Real.Get(), Ctx(Seed % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), Seed), Fish);
			TArray<FName> Actual;
			for (const FFishStatValue& Stat : Fish.Stats)
			{
				Actual.Add(Stat.Tag.GetTagName());
			}
			if (Actual != Expected && ++Bad <= 2)
			{
				AddError(FString::Printf(TEXT("seed %d stats [%s], expected [%s]"), Seed,
					*FString::JoinBy(Actual, TEXT(","), [](const FName& N) { return N.ToString(); }),
					*FString::JoinBy(Expected, TEXT(","), [](const FName& N) { return N.ToString(); })));
			}
		}
		TestEqual(TEXT("catches whose stats aren't exactly the sorted registry"), Bad, 0);
		return true;
	}

	/** QA-67: the identity pipeline (reference weight, neutral tier, no modifiers) returns the species base exactly */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAInstanceIdentityTest, "Project.Fish.QA.Instance.IdentityPipeline", FISH_QA_FLAGS)
	bool FFishQAInstanceIdentityTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishInstance Fish;
		TestTrue(TEXT("rolls"), FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), 42), Fish));
		TestEqual(TEXT("species"), Fish.SpeciesId, FName(TEXT("QA_Fixed")));
		TestEqual(TEXT("rarity"), Fish.RarityId, FName(TEXT("QA_Neutral")));
		TestEqual(TEXT("no modifiers"), Fish.ModifierIds.Num(), 0);
		TestEqual(TEXT("weight 2 kg"), Fish.WeightKg, 2.0f);
		TestEqual(TEXT("Strength = base 10"), Fish.GetStat(Tag(TEXT("Fish.Stat.Strength"))), 10.0f);
		TestEqual(TEXT("Stamina = base 20"), Fish.GetStat(Tag(TEXT("Fish.Stat.Stamina"))), 20.0f);
		TestEqual(TEXT("Speed = base 4"), Fish.GetStat(Tag(TEXT("Fish.Stat.Speed"))), 4.0f);
		TestEqual(TEXT("unlisted stat = its Default"), Fish.GetStat(Tag(QAStat), -1.0f), 0.0f);
		TestEqual(TEXT("value = round(10 coins/kg * 2 kg)"), Fish.Value, 20);
		TestEqual(TEXT("level = BaseLevel"), Fish.Level, 5);
		TestEqual(TEXT("xp = round(10 + 5 * (5 - 1))"), Fish.Xp, 30);
		TestEqual(TEXT("difficulty rating 1 for an average fish"), Fish.DifficultyRating, 1.0f);
		TestEqual(TEXT("seed stored"), Fish.Seed, 42);
		return true;
	}

	/** QA-68 (P2): a record whose ids no longer exist keeps working from its stored data */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAInstanceDanglingTest, "Project.Fish.QA.Instance.DanglingIdsGraceful", FISH_QA_FLAGS)
	bool FFishQAInstanceDanglingTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FFishInstance Fish;
		FFishRoll::Roll(Real.Get(), Ctx(TEXT("Bonefish"), 3), Fish);
		const int32 Value = Fish.Value;
		const float Strength = Fish.GetStat(Tag(TEXT("Fish.Stat.Strength")));
		Fish.SpeciesId = TEXT("QA_DeletedSpecies");
		Fish.RarityId = TEXT("QA_DeletedTier");
		Fish.ModifierIds = { TEXT("QA_DeletedTrait") };
		const FFishTables Tables = Real.Get();
		TestNull(TEXT("FindSpecies(unknown) is null"), Tables.FindSpecies(Fish.SpeciesId));
		TestNull(TEXT("FindRarity(unknown) is null"), Tables.FindRarity(Fish.RarityId));
		TestNull(TEXT("FindModifier(unknown) is null"), Tables.FindModifier(Fish.ModifierIds[0]));
		TestTrue(TEXT("still a fish"), Fish.IsValid());
		TestEqual(TEXT("stored stat still readable"), Fish.GetStat(Tag(TEXT("Fish.Stat.Strength"))), Strength);
		TestTrue(TEXT("HasModifier on the stored id"), Fish.HasModifier(TEXT("QA_DeletedTrait")));
		TestEqual(TEXT("stored value unchanged"), Fish.Value, Value);
		TestFalse(TEXT("ToString works"), Fish.ToString().IsEmpty());
		TestNotNull(TEXT("FindSpecies(known) works"), Tables.FindSpecies(TEXT("Bonefish")));
		TestNotNull(TEXT("FindStat(Weight) works"), Tables.FindStat(Tag(TEXT("Fish.Stat.Weight"))));
		return true;
	}

	/** QA-69: Level = BaseLevel + the rarity's LevelBonus, >= 1 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAInstanceLevelTest, "Project.Fish.QA.Instance.LevelRule", FISH_QA_FLAGS)
	bool FFishQAInstanceLevelTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		for (const FName& SpeciesId : SortedRowNames<FFishSpeciesRow>(Real.Species.Get()))
		{
			for (const FName& Tier : RaritiesByRank(Real))
			{
				const FFishInstance Fish = RollForced(Real, SpeciesId, Tier, 0.5f);
				const int32 Expected = Row<FFishSpeciesRow>(Real.Species, SpeciesId)->BaseLevel + Row<FFishRarityRow>(Real.Rarities, Tier)->LevelBonus;
				TestEqual(FString::Printf(TEXT("%s %s level"), *SpeciesId.ToString(), *Tier.ToString()), Fish.Level, Expected);
				TestTrue(TEXT("level >= 1"), Fish.Level >= 1);
			}
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

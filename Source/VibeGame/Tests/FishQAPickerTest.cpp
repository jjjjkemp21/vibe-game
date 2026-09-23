// QA-owned, independent tests for the T-008 bite picker (which species bites) and the level-difference hook.
// Written by the qa-engineer from docs/specs/fish-system-rules.md ("Bite picker", "Level") and Fish/FishRoll.h.

#include "FishQATestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

namespace FishQA_Picker
{
	using namespace FishQA;

	static FFishTimeWindow Window(float Start, float End)
	{
		FFishTimeWindow W;
		W.StartHour = Start;
		W.EndHour = End;
		return W;
	}

	/**
	 *  FX-PICK: P_Shore (H0, 6-18, bait A), P_Reef (H1, any time, bait B), P_Dawn (H0, 5-7, bait A),
	 *  P_Night (H0, 20-4 wrapping, bait A or B), P_Deep (H2, any time, bait B, level 50).
	 */
	static void MakePickFixture(FTables& T)
	{
		MakeIdentity(T);
		T.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct()));
		auto Add = [&T](const TCHAR* Name, const TCHAR* Habitat, TArray<FFishTimeWindow> Windows, TArray<FGameplayTag> Bait, int32 Level = 1)
		{
			FFishSpeciesRow Species = MakeSpecies(1.0f, 2.0f);
			Species.HabitatTags = { Tag(Habitat) };
			Species.TimeWindows = MoveTemp(Windows);
			Species.AcceptedBait = MoveTemp(Bait);
			Species.BaseLevel = Level;
			T.Species->AddRow(Name, Species);
		};
		const FGameplayTag A = Tag(TEXT("Test.Fish.Bait.A"));
		const FGameplayTag B = Tag(TEXT("Test.Fish.Bait.B"));
		Add(TEXT("P_Shore"), TEXT("Test.Fish.Habitat.H0"), { Window(6.0f, 18.0f) }, { A });
		Add(TEXT("P_Reef"), TEXT("Test.Fish.Habitat.H1"), {}, { B });
		Add(TEXT("P_Dawn"), TEXT("Test.Fish.Habitat.H0"), { Window(5.0f, 7.0f) }, { A });
		Add(TEXT("P_Night"), TEXT("Test.Fish.Habitat.H0"), { Window(20.0f, 4.0f) }, { A, B });
		Add(TEXT("P_Deep"), TEXT("Test.Fish.Habitat.H2"), {}, { B }, 50);
	}

	static FFishRollContext BiteContext(const TCHAR* Habitat, float Hours, const TCHAR* Bait, int32 Seed)
	{
		FFishRollContext Context = Ctx(NAME_None, Seed);
		Context.HabitatTag = *Habitat ? Tag(Habitat) : FGameplayTag();
		Context.TimeOfDayHours = Hours;
		Context.BaitTag = *Bait ? Tag(Bait) : FGameplayTag();
		return Context;
	}

	static bool Pick(const FTables& T, const FFishRollContext& Context, FName& Out)
	{
		return FFishRoll::PickSpecies(T.Get(), Context, Out);
	}

	/** QA-70: over a habitat x hour x bait grid, every pick is eligible, every eligible species shows up, and IsSpeciesEligible agrees with the spec */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerEligibleTest, "Project.Fish.QA.Picker.OnlyEligibleSpecies", FISH_QA_FLAGS)
	bool FFishQAPickerEligibleTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakePickFixture(T);
		const TArray<FName> SpeciesIds = SortedRowNames<FFishSpeciesRow>(T.Species.Get());
		int32 BadPicks = 0, Missing = 0, WrongResult = 0, EligibilityDisagreements = 0, Cells = 0;
		for (const TCHAR* Habitat : { TEXT("Test.Fish.Habitat.H0"), TEXT("Test.Fish.Habitat.H1"), TEXT("Test.Fish.Habitat.H2"), TEXT("Test.Fish.Habitat.H3"), TEXT("") })
		{
			for (int32 HalfHour = 0; HalfHour < 48; ++HalfHour)
			{
				for (const TCHAR* Bait : { TEXT("Test.Fish.Bait.A"), TEXT("Test.Fish.Bait.A.Child"), TEXT("Test.Fish.Bait.B"), TEXT("") })
				{
					++Cells;
					const float Hours = HalfHour * 0.5f;
					TSet<FName> Eligible;
					for (const FName& Id : SpeciesIds)
					{
						const FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, Id);
						const FFishRollContext Context = BiteContext(Habitat, Hours, Bait, 0);
						const bool bExpected = OracleSpeciesEligible(*Species, Context);
						if (bExpected)
						{
							Eligible.Add(Id);
						}
						if (FFishRoll::IsSpeciesEligible(*Species, Context) != bExpected && ++EligibilityDisagreements <= 3)
						{
							AddError(FString::Printf(TEXT("IsSpeciesEligible(%s) at %s %g %s disagrees with the spec (%d)"), *Id.ToString(), Habitat, Hours, Bait, bExpected));
						}
					}
					TSet<FName> Seen;
					for (int32 Seed = 1; Seed <= 200; ++Seed)
					{
						FName Picked;
						const bool bOk = Pick(T, BiteContext(Habitat, Hours, Bait, Seed * 7 + HalfHour), Picked);
						if (bOk != (Eligible.Num() > 0) || (!bOk && !Picked.IsNone()))
						{
							++WrongResult;
						}
						if (bOk)
						{
							if (!Eligible.Contains(Picked) && ++BadPicks <= 3)
							{
								AddError(FString::Printf(TEXT("picked ineligible %s at %s %g %s"), *Picked.ToString(), Habitat, Hours, Bait));
							}
							Seen.Add(Picked);
						}
					}
					Missing += Eligible.Num() - Eligible.Intersect(Seen).Num();
				}
			}
		}
		TestEqual(TEXT("ineligible picks"), BadPicks, 0);
		TestEqual(TEXT("eligible species never picked in 200 tries"), Missing, 0);
		TestEqual(TEXT("picks whose result/out id contradicts eligibility"), WrongResult, 0);
		TestEqual(TEXT("IsSpeciesEligible disagreements"), EligibilityDisagreements, 0);
		AddInfo(FString::Printf(TEXT("%d grid cells x 200 picks"), Cells));
		return true;
	}

	/** QA-71/72/73: wrong habitat, wrong time or wrong bait -> nothing bites, no warning (normal gameplay) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerNothingBitesTest, "Project.Fish.QA.Picker.WrongHabitatTimeOrBaitGivesNone", FISH_QA_FLAGS)
	bool FFishQAPickerNothingBitesTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakePickFixture(T);
		FLogCapture Log;
		struct FCase { const TCHAR* What; const TCHAR* Habitat; float Hours; const TCHAR* Bait; };
		for (const FCase& Case : {
			FCase{ TEXT("habitat nobody uses"), TEXT("Test.Fish.Habitat.H3"), 12.0f, TEXT("Test.Fish.Bait.A") },
			FCase{ TEXT("H0 at 19:00 (between Shore and Night)"), TEXT("Test.Fish.Habitat.H0"), 19.0f, TEXT("Test.Fish.Bait.A") },
			FCase{ TEXT("H0 at 18:00 (Shore window end is exclusive)"), TEXT("Test.Fish.Habitat.H0"), 18.0f, TEXT("Test.Fish.Bait.A") },
			FCase{ TEXT("H1 with bait A (Reef needs B)"), TEXT("Test.Fish.Habitat.H1"), 12.0f, TEXT("Test.Fish.Bait.A") },
			FCase{ TEXT("H1 with no bait"), TEXT("Test.Fish.Habitat.H1"), 12.0f, TEXT("") },
			FCase{ TEXT("H0 at noon with bait B (only Night takes B there)"), TEXT("Test.Fish.Habitat.H0"), 12.0f, TEXT("Test.Fish.Bait.B") } })
		{
			for (int32 Seed = 1; Seed <= 50; ++Seed)
			{
				FName Picked = TEXT("Sentinel");
				const bool bOk = Pick(T, BiteContext(Case.Habitat, Case.Hours, Case.Bait, Seed), Picked);
				if (bOk || !Picked.IsNone())
				{
					AddError(FString::Printf(TEXT("%s: expected nothing, got %d '%s'"), Case.What, bOk, *Picked.ToString()));
					break;
				}
			}
		}
		TestEqual(TEXT("no warnings for 'nothing bites here'"), Log.NumWarnings(), 0);
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** QA-74: empty or missing species table, zero bite weights and empty contexts are graceful */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerEmptyTest, "Project.Fish.QA.Picker.EmptyCandidatesGraceful", FISH_QA_FLAGS)
	bool FFishQAPickerEmptyTest::RunTest(const FString& Parameters)
	{
		FLogCapture Log;
		FTables T;
		MakePickFixture(T);
		FName Picked;

		FFishTables Missing = T.Get();
		Missing.Species = nullptr;
		TestFalse(TEXT("missing species table: false"), FFishRoll::PickSpecies(Missing, BiteContext(TEXT("Test.Fish.Habitat.H0"), 12.0f, TEXT("Test.Fish.Bait.A"), 1), Picked));
		TestTrue(TEXT("missing species table: warns"), Log.NumWarnings() > 0);

		FTables Empty;
		MakeIdentity(Empty);
		Empty.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct()));
		TestFalse(TEXT("empty species table: false"), Pick(Empty, BiteContext(TEXT("Test.Fish.Habitat.H0"), 12.0f, TEXT("Test.Fish.Bait.A"), 1), Picked));
		TestTrue(TEXT("empty species table: None"), Picked.IsNone());

		FTables Zero;
		MakePickFixture(Zero);
		for (const FName& Id : SortedRowNames<FFishSpeciesRow>(Zero.Species.Get()))
		{
			Row<FFishSpeciesRow>(Zero.Species, Id)->BiteWeight = 0.0f;
		}
		TestFalse(TEXT("all bite weights 0: nothing bites"), Pick(Zero, BiteContext(TEXT("Test.Fish.Habitat.H0"), 12.0f, TEXT("Test.Fish.Bait.A"), 1), Picked));

		// A species with no habitat/region/time/bait lists bites anywhere, even with an empty context
		FTables Anywhere;
		MakeIdentity(Anywhere);
		TestTrue(TEXT("no lists = any: bites with an empty context"), Pick(Anywhere, BiteContext(TEXT(""), 3.0f, TEXT(""), 1), Picked));
		TestEqual(TEXT("it's QA_Fixed"), Picked, FName(TEXT("QA_Fixed")));
		// ...but a species that lists habitats doesn't bite at a spot with no habitat
		TestFalse(TEXT("listed habitat vs no context habitat: nothing"), Pick(T, BiteContext(TEXT(""), 12.0f, TEXT("Test.Fish.Bait.A"), 1), Picked));
		TestEqual(TEXT("no errors"), Log.NumErrors(), 0);
		return true;
	}

	/** QA-75: time windows are half-open [Start, End), wrap midnight, and hours are normalized into [0, 24) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerTimeTest, "Project.Fish.QA.Picker.TimeWindowBoundaries", FISH_QA_FLAGS)
	bool FFishQAPickerTimeTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		struct FTime { float Hours; bool bIn; };
		Species->TimeWindows = { Window(5.0f, 7.0f) };
		for (const FTime& Time : { FTime{ 5.0f, true }, FTime{ 6.99f, true }, FTime{ 4.99f, false }, FTime{ 7.0f, false }, FTime{ 29.0f, true }, FTime{ -18.5f, true } })
		{
			FName Picked;
			TestEqual(FString::Printf(TEXT("dawn [5, 7) at %g"), Time.Hours), Pick(T, BiteContext(TEXT(""), Time.Hours, TEXT(""), 1), Picked), Time.bIn);
		}
		Species->TimeWindows = { Window(20.0f, 4.0f) };
		for (const FTime& Time : { FTime{ 20.0f, true }, FTime{ 23.99f, true }, FTime{ 0.0f, true }, FTime{ 3.99f, true }, FTime{ 4.0f, false },
			FTime{ 12.0f, false }, FTime{ 19.99f, false }, FTime{ 24.0f, true }, FTime{ -1.0f, true } })
		{
			FName Picked;
			TestEqual(FString::Printf(TEXT("night [20, 4) at %g"), Time.Hours), Pick(T, BiteContext(TEXT(""), Time.Hours, TEXT(""), 1), Picked), Time.bIn);
		}
		Species->TimeWindows = { Window(0.0f, 24.0f) };
		for (float Hours : { 0.0f, 12.0f, 23.999f, 24.0f })
		{
			FName Picked;
			TestTrue(FString::Printf(TEXT("all day [0, 24) at %g"), Hours), Pick(T, BiteContext(TEXT(""), Hours, TEXT(""), 1), Picked));
		}
		Species->TimeWindows = { Window(22.0f, 23.0f), Window(1.0f, 2.0f) };
		FName Picked;
		TestTrue(TEXT("second window counts (any window)"), Pick(T, BiteContext(TEXT(""), 1.5f, TEXT(""), 1), Picked));
		TestFalse(TEXT("between two windows"), Pick(T, BiteContext(TEXT(""), 12.0f, TEXT(""), 1), Picked));

		// Pure helpers
		TestEqual(TEXT("NormalizeHours(24) = 0"), FFishRoll::NormalizeHours(24.0f), 0.0f);
		TestEqual(TEXT("NormalizeHours(-1) = 23"), FFishRoll::NormalizeHours(-1.0f), 23.0f);
		TestEqual(TEXT("NormalizeHours(25) = 1"), FFishRoll::NormalizeHours(25.0f), 1.0f);
		TestEqual(TEXT("NormalizeHours(48) = 0"), FFishRoll::NormalizeHours(48.0f), 0.0f);
		TestEqual(TEXT("NormalizeHours(NaN) = 0"), FFishRoll::NormalizeHours(std::numeric_limits<float>::quiet_NaN()), 0.0f);
		TestEqual(TEXT("NormalizeHours(Inf) = 0"), FFishRoll::NormalizeHours(std::numeric_limits<float>::infinity()), 0.0f);
		const float Tiny = FFishRoll::NormalizeHours(-1e-7f);
		TestTrue(FString::Printf(TEXT("NormalizeHours(-1e-7) is in [0, 24) (got %.8f)"), Tiny), Tiny >= 0.0f && Tiny < 24.0f);
		const TArray<FFishTimeWindow> NoWindows;
		TestTrue(TEXT("IsInTimeWindows(empty) = any time"), FFishRoll::IsInTimeWindows(NoWindows, 3.0f));
		const TArray<FFishTimeWindow> Night = { Window(20.0f, 4.0f) };
		TestTrue(TEXT("IsInTimeWindows(night, 24) normalizes to 0"), FFishRoll::IsInTimeWindows(Night, 24.0f));
		TestFalse(TEXT("IsInTimeWindows(night, 4)"), FFishRoll::IsInTimeWindows(Night, 4.0f));
		return true;
	}

	/** QA-76: bait is any-of and hierarchical (the gear's child tag satisfies a species' parent tag, not the reverse); empty = any or none */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerBaitTest, "Project.Fish.QA.Picker.BaitMatchSemantics", FISH_QA_FLAGS)
	bool FFishQAPickerBaitTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		auto Bites = [&T](const TCHAR* Bait)
		{
			FName Picked;
			return Pick(T, BiteContext(TEXT(""), 12.0f, Bait, 1), Picked);
		};
		Species->AcceptedBait = { Tag(TEXT("Test.Fish.Bait.A")) };
		TestTrue(TEXT("accepts A: A bites"), Bites(TEXT("Test.Fish.Bait.A")));
		TestTrue(TEXT("accepts A: A.Child bites (hierarchical)"), Bites(TEXT("Test.Fish.Bait.A.Child")));
		TestFalse(TEXT("accepts A: B doesn't"), Bites(TEXT("Test.Fish.Bait.B")));
		TestFalse(TEXT("accepts A: no bait doesn't"), Bites(TEXT("")));
		Species->AcceptedBait = { Tag(TEXT("Test.Fish.Bait.A.Child")) };
		TestFalse(TEXT("accepts A.Child: the parent A doesn't"), Bites(TEXT("Test.Fish.Bait.A")));
		TestTrue(TEXT("accepts A.Child: A.Child bites"), Bites(TEXT("Test.Fish.Bait.A.Child")));
		Species->AcceptedBait = { Tag(TEXT("Test.Fish.Bait.B")), Tag(TEXT("Test.Fish.Bait.A")) };
		TestTrue(TEXT("accepts {B, A}: A bites (any-of)"), Bites(TEXT("Test.Fish.Bait.A")));
		TestTrue(TEXT("accepts {B, A}: B bites (any-of)"), Bites(TEXT("Test.Fish.Bait.B")));
		Species->AcceptedBait.Reset();
		TestTrue(TEXT("accepts anything: A bites"), Bites(TEXT("Test.Fish.Bait.A")));
		TestTrue(TEXT("accepts anything: no bait bites"), Bites(TEXT("")));
		return true;
	}

	/** New (Q19/Q32 answered): region (hierarchical) and weather filters; a listed weather needs that weather */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerRegionWeatherTest, "Project.Fish.QA.Picker.RegionAndWeather", FISH_QA_FLAGS)
	bool FFishQAPickerRegionWeatherTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		FFishSpeciesRow* Species = Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"));
		Species->RegionTags = { Tag(TEXT("Test.Fish.Region.R1")) };
		Species->WeatherTags = { Tag(TEXT("Test.Fish.Weather.Rain")) };
		auto Bites = [&T](const TCHAR* Region, const TCHAR* Weather)
		{
			FFishRollContext Context = BiteContext(TEXT(""), 12.0f, TEXT(""), 1);
			Context.RegionTag = *Region ? Tag(Region) : FGameplayTag();
			Context.WeatherTag = *Weather ? Tag(Weather) : FGameplayTag();
			FName Picked;
			return Pick(T, Context, Picked);
		};
		TestTrue(TEXT("R1 + Rain"), Bites(TEXT("Test.Fish.Region.R1"), TEXT("Test.Fish.Weather.Rain")));
		TestTrue(TEXT("R1.Sub + Rain (region is hierarchical)"), Bites(TEXT("Test.Fish.Region.R1.Sub"), TEXT("Test.Fish.Weather.Rain")));
		TestFalse(TEXT("R2 + Rain"), Bites(TEXT("Test.Fish.Region.R2"), TEXT("Test.Fish.Weather.Rain")));
		TestFalse(TEXT("no region + Rain"), Bites(TEXT(""), TEXT("Test.Fish.Weather.Rain")));
		TestFalse(TEXT("R1 + Sun"), Bites(TEXT("Test.Fish.Region.R1"), TEXT("Test.Fish.Weather.Sun")));
		TestFalse(TEXT("R1 + no weather"), Bites(TEXT("Test.Fish.Region.R1"), TEXT("")));
		Species->WeatherTags.Reset();
		TestTrue(TEXT("no weather list: any weather"), Bites(TEXT("Test.Fish.Region.R1"), TEXT("Test.Fish.Weather.Sun")));
		TestTrue(TEXT("no weather list: no weather"), Bites(TEXT("Test.Fish.Region.R1"), TEXT("")));
		return true;
	}

	/** QA-77: among eligible species, picks follow BiteWeight (100k picks, 4.5 sigma + chi-square) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerOddsTest, "Project.Fish.QA.Picker.OddsFollowBiteWeights", FISH_QA_FLAGS)
	bool FFishQAPickerOddsTest::RunTest(const FString& Parameters)
	{
		for (const TArray<float>& Weights : { TArray<float>{ 1.0f, 2.0f, 7.0f }, TArray<float>{ 1.0f, 1.0f, 1.0f }, TArray<float>{ 0.8f, 0.0f, 1.0f } })
		{
			FTables T;
			MakeIdentity(T);
			T.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct()));
			double Total = 0.0;
			for (int32 i = 0; i < Weights.Num(); ++i)
			{
				FFishSpeciesRow Species = MakeSpecies();
				Species.BiteWeight = Weights[i];
				T.Species->AddRow(FName(*FString::Printf(TEXT("S%d"), i)), Species);
				Total += Weights[i];
			}
			const int32 N = 100000;
			TArray<int32> Counts;
			Counts.SetNumZeroed(Weights.Num());
			for (int32 Seed = 1; Seed <= N; ++Seed)
			{
				FName Picked;
				if (Pick(T, BiteContext(TEXT(""), 12.0f, TEXT(""), Seed), Picked))
				{
					const int32 Index = FCString::Atoi(*Picked.ToString().RightChop(1));
					Counts[Index]++;
				}
			}
			TArray<double> Probs;
			TArray<FString> Names;
			for (int32 i = 0; i < Weights.Num(); ++i)
			{
				Probs.Add(Weights[i] / Total);
				Names.Add(FString::Printf(TEXT("S%d"), i));
			}
			CheckOdds(*this, FString::Printf(TEXT("bite weights {%s}"), *FString::JoinBy(Weights, TEXT(","), [](float W) { return FString::SanitizeFloat(W); })), Names, Counts, Probs, N);
		}
		return true;
	}

	/** QA-78: the same context + seed picks the same species, whatever the batch order */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerDeterministicTest, "Project.Fish.QA.Picker.DeterministicAndOrderIndependent", FISH_QA_FLAGS)
	bool FFishQAPickerDeterministicTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakePickFixture(T);
		const int32 N = 3000;
		TArray<FName> Forward;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FName Picked;
			Pick(T, BiteContext(TEXT("Test.Fish.Habitat.H0"), 6.0f, TEXT("Test.Fish.Bait.A"), Seed), Picked);
			Forward.Add(Picked);
		}
		int32 Mismatches = 0;
		TSet<FName> Distinct;
		for (int32 Seed = N; Seed >= 1; --Seed)
		{
			FName Other;
			Pick(T, BiteContext(TEXT("Test.Fish.Habitat.H1"), 1.0f, TEXT("Test.Fish.Bait.B"), Seed + 5), Other);
			FFishInstance Noise;
			FFishRoll::Roll(T.Get(), Ctx(TEXT("P_Reef"), Seed), Noise);
			FName Picked;
			Pick(T, BiteContext(TEXT("Test.Fish.Habitat.H0"), 6.0f, TEXT("Test.Fish.Bait.A"), Seed), Picked);
			Mismatches += Picked == Forward[Seed - 1] ? 0 : 1;
			Distinct.Add(Picked);
		}
		TestEqual(TEXT("reversed and interleaved: same species per seed"), Mismatches, 0);
		TestEqual(TEXT("both eligible species (Shore, Dawn) appear at 06:00"), Distinct.Num(), 2);
		return true;
	}

	/** P2 content check: the starter fish bite where and when their rows say */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAPickerStarterTest, "Project.Fish.QA.Picker.StarterDataWhereAndWhen", FISH_QA_FLAGS)
	bool FFishQAPickerStarterTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		auto Picks = [&Real](const TCHAR* Habitat, float Hours, const TCHAR* Bait)
		{
			TSet<FName> Seen;
			for (int32 Seed = 1; Seed <= 300; ++Seed)
			{
				FFishRollContext Context = BiteContext(Habitat, Hours, Bait, Seed);
				Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
				FName Picked;
				if (FFishRoll::PickSpecies(Real.Get(), Context, Picked))
				{
					Seen.Add(Picked);
				}
			}
			return Seen;
		};
		TestTrue(TEXT("shore, 10:00, worm -> Bonefish"), Picks(TEXT("Habitat.Shore"), 10.0f, TEXT("Bait.Worm")).Contains(TEXT("Bonefish")));
		TestEqual(TEXT("shore, 20:00 -> nothing (Bonefish bites 5-19)"), Picks(TEXT("Habitat.Shore"), 20.0f, TEXT("Bait.Worm")).Num(), 0);
		TestTrue(TEXT("reef, 22:00, shrimp -> Coral Snapper"), Picks(TEXT("Habitat.Reef"), 22.0f, TEXT("Bait.Shrimp")).Contains(TEXT("CoralSnapper")));
		TestEqual(TEXT("reef, 12:00 -> nothing (Coral Snapper bites 15-9)"), Picks(TEXT("Habitat.Reef"), 12.0f, TEXT("Bait.Shrimp")).Num(), 0);
		TestEqual(TEXT("reef, 22:00, worm -> nothing (wrong bait)"), Picks(TEXT("Habitat.Reef"), 22.0f, TEXT("Bait.Worm")).Num(), 0);
		FFishRollContext Foggy = BiteContext(TEXT("Habitat.Shore"), 10.0f, TEXT("Bait.Worm"), 1);
		Foggy.RegionTag = Tag(TEXT("Region.Foggy"));
		FName Picked;
		TestFalse(TEXT("shore in the foggy region -> nothing (tropical fish)"), FFishRoll::PickSpecies(Real.Get(), Foggy, Picked));
		return true;
	}

	// ================================================================================================================
	// Level hook (QA-80..QA-84)
	// ================================================================================================================

	/** QA-80: equal levels -> 1.0; above -> 1 + d * Over; below -> 1 - d * Under; clamped */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQALevelFormulaTest, "Project.Fish.QA.Level.SignAndNeutral", FISH_QA_FLAGS)
	bool FFishQALevelFormulaTest::RunTest(const FString& Parameters)
	{
		const FFishLevelScaling Scaling; // documented defaults: Over 0.35, Under 0.1, Min 0.5, Max 5
		TestEqual(TEXT("equal levels"), FFishRoll::LevelDifficultyMultiplier(7, 7, Scaling), 1.0f);
		TestEqual(TEXT("1 above = 1.35"), FFishRoll::LevelDifficultyMultiplier(8, 7, Scaling), 1.35f);
		TestEqual(TEXT("4 above = 2.4"), FFishRoll::LevelDifficultyMultiplier(11, 7, Scaling), 2.4f);
		TestEqual(TEXT("3 below = 0.7"), FFishRoll::LevelDifficultyMultiplier(4, 7, Scaling), 0.7f);
		TestEqual(TEXT("5 below = 0.5 (clamped)"), FFishRoll::LevelDifficultyMultiplier(2, 7, Scaling), 0.5f);
		TestEqual(TEXT("20 above = 5 (clamped)"), FFishRoll::LevelDifficultyMultiplier(27, 7, Scaling), 5.0f);
		TestEqual(TEXT("GetMultiplier is the same hook"), Scaling.GetMultiplier(11, 7), FFishRoll::LevelDifficultyMultiplier(11, 7, Scaling));
		return true;
	}

	/** QA-81: non-decreasing in fish level, non-increasing in player level */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQALevelMonotoneTest, "Project.Fish.QA.Level.Monotone", FISH_QA_FLAGS)
	bool FFishQALevelMonotoneTest::RunTest(const FString& Parameters)
	{
		const FFishLevelScaling Scaling;
		int32 Violations = 0;
		for (int32 Player : { 1, 10, 50 })
		{
			float Previous = -1.0f;
			for (int32 Fish = 1; Fish <= 100; ++Fish)
			{
				const float M = FFishRoll::LevelDifficultyMultiplier(Fish, Player, Scaling);
				Violations += M >= Previous ? 0 : 1;
				Previous = M;
			}
		}
		for (int32 Fish : { 1, 10, 50 })
		{
			float Previous = TNumericLimits<float>::Max();
			for (int32 Player = 1; Player <= 100; ++Player)
			{
				const float M = FFishRoll::LevelDifficultyMultiplier(Fish, Player, Scaling);
				Violations += M <= Previous ? 0 : 1;
				Previous = M;
			}
		}
		TestEqual(TEXT("monotonicity violations"), Violations, 0);
		return true;
	}

	/** QA-82: any int32 levels (including overflow of the difference) stay finite and inside [Min, Max] */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQALevelExtremesTest, "Project.Fish.QA.Level.ExtremesBounded", FISH_QA_FLAGS)
	bool FFishQALevelExtremesTest::RunTest(const FString& Parameters)
	{
		const FFishLevelScaling Scaling;
		const int32 Max = std::numeric_limits<int32>::max();
		const int32 Min = std::numeric_limits<int32>::min();
		TestEqual(TEXT("fish INT32_MAX vs player INT32_MIN -> MaxMultiplier (no overflow wrap)"), FFishRoll::LevelDifficultyMultiplier(Max, Min, Scaling), Scaling.MaxMultiplier);
		TestEqual(TEXT("fish INT32_MIN vs player INT32_MAX -> MinMultiplier (no overflow wrap)"), FFishRoll::LevelDifficultyMultiplier(Min, Max, Scaling), Scaling.MinMultiplier);
		TestEqual(TEXT("+1000 -> Max"), FFishRoll::LevelDifficultyMultiplier(1001, 1, Scaling), Scaling.MaxMultiplier);
		TestEqual(TEXT("-1000 -> Min"), FFishRoll::LevelDifficultyMultiplier(1, 1001, Scaling), Scaling.MinMultiplier);
		for (int32 A : { Min, -1, 0, 1, Max })
		{
			for (int32 B : { Min, -1, 0, 1, Max })
			{
				const float M = FFishRoll::LevelDifficultyMultiplier(A, B, Scaling);
				TestTrue(FString::Printf(TEXT("(%d, %d) finite, in range, > 0"), A, B), FMath::IsFinite(M) && M >= Scaling.MinMultiplier && M <= Scaling.MaxMultiplier && M > 0.0f);
			}
		}
		return true;
	}

	/** QA-83: depends only on the level difference */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQALevelShiftTest, "Project.Fish.QA.Level.ShiftInvariant", FISH_QA_FLAGS)
	bool FFishQALevelShiftTest::RunTest(const FString& Parameters)
	{
		const FFishLevelScaling Scaling;
		for (int32 D = -12; D <= 12; ++D)
		{
			const float Base = FFishRoll::LevelDifficultyMultiplier(20 + D, 20, Scaling);
			TestEqual(FString::Printf(TEXT("d=%d at 5"), D), FFishRoll::LevelDifficultyMultiplier(5 + D, 5, Scaling), Base);
			TestEqual(FString::Printf(TEXT("d=%d at 60"), D), FFishRoll::LevelDifficultyMultiplier(60 + D, 60, Scaling), Base);
		}
		return true;
	}

	/** QA-84: the hook follows its data (factors and clamps), and the Blueprint wrapper uses the project settings */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQALevelDataTest, "Project.Fish.QA.Level.SettingsDriveTheHook", FISH_QA_FLAGS)
	bool FFishQALevelDataTest::RunTest(const FString& Parameters)
	{
		FFishLevelScaling Custom;
		Custom.OverLevelFactor = 1.0f;
		Custom.UnderLevelFactor = 0.25f;
		Custom.MinMultiplier = 0.1f;
		Custom.MaxMultiplier = 3.0f;
		TestEqual(TEXT("custom: 1 above = 2"), FFishRoll::LevelDifficultyMultiplier(6, 5, Custom), 2.0f);
		TestEqual(TEXT("custom: 5 above = 3 (clamped)"), FFishRoll::LevelDifficultyMultiplier(10, 5, Custom), 3.0f);
		TestEqual(TEXT("custom: 2 below = 0.5"), FFishRoll::LevelDifficultyMultiplier(3, 5, Custom), 0.5f);
		TestEqual(TEXT("custom: 10 below = 0.1 (clamped)"), FFishRoll::LevelDifficultyMultiplier(1, 11, Custom), 0.1f);
		const FFishLevelScaling& Project = GetDefault<UFishSettings>()->LevelScaling;
		for (int32 D = -6; D <= 6; ++D)
		{
			TestEqual(FString::Printf(TEXT("UFishLibrary uses UFishSettings::LevelScaling (d=%d)"), D),
				UFishLibrary::GetLevelDifficultyMultiplier(10 + D, 10), FFishRoll::LevelDifficultyMultiplier(10 + D, 10, Project));
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

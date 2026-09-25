// Lure T-049 + T-050 (Jimmy's A2 playtest, 2026-09-24): an even fight and a faster reel. Project.Fishing.Fight.Even.* and
// Project.Fishing.Fight.LandTime.*. Pure fight sims (FLureFight, fixed 60 Hz steps, fixed seeds) on the text sources in
// data/tables/ and fish from the one roll pipeline. Rules and numbers: docs/specs/reel-fight-rules.md ("T-049 / T-050").
//
// "Well-played" (one policy for every test here): the player watches the tension bar and reacts 0.3 s late (the tests'
// usual reaction time). Every 0.3 s: while reeling, keep reeling below 85 % of the line and ease off (let it run on the drag)
// at 85 % or more; once eased off, reel again under 60 %. 85 % leaves room for the reaction and the tension's rise before
// 100 %; 60 % is "the bar is clearly back in the safe half". The rod stays level at the default reel step and is steered
// against every sideways run 0.3 s after the fish turns (GAME_DESIGN: "if the fish runs left, angle the rod to the right").
// Fights start 18 m out: DT_Fishing MaxCastDistance, where 8 of Jimmy's 14 A2 fights started.
//
// Each test also plays the old numbers (the tune before T-049/T-050, rebuilt from the shipped data below) to show that it
// catches what Jimmy felt.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Fishing/FishFight.h"
#include "Fishing/FishFightTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/FishQATestHelpers.h"
#include "UObject/StrongObjectPtr.h"

// Everything stays inside this namespace (no file-scope using-directive: unity builds share translation units).
namespace LureFightEvenTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Where the fight starts: a full cast (DT_Fishing MaxCastDistance 1800 cm). */
	constexpr float StartLineOut = 1800.f;
	constexpr float Reaction = 0.3f;
	constexpr int32 NumSeeds = 30;

	/** A starter-kit bonefish at the species' ReferenceWeight (its stats are exactly DT_FishSpecies' base stats). */
	constexpr float BonefishReferenceKg = 1.5f;

	bool Import(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const TCHAR* FileName)
	{
		FString Text;
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
		if (!Test.TestTrue(FString::Printf(TEXT("data/tables/%s loads"), FileName), FFileHelper::LoadFileToString(Text, *Path)))
		{
			return false;
		}
		Out.Reset(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Out->RowStruct = RowStruct;
		const TArray<FString> Problems = FString(FileName).EndsWith(TEXT(".json")) ? Out->CreateTableFromJSONString(Text) : Out->CreateTableFromCSVString(Text);
		return Test.TestEqual(FString::Printf(TEXT("%s import problems (%s)"), FileName, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
	}

	/** One tune: fight tuning, the starter kit and the patterns. */
	struct FTune
	{
		FLureFishFightRow Tuning;
		FLureGearStats Starter;
		TMap<FName, FLureFightPatternRow> Patterns;
	};

	/** The shipped tune from the text sources. */
	bool LoadShipped(FAutomationTestBase& Test, FTune& Out)
	{
		TStrongObjectPtr<UDataTable> Gear, Patterns, Fight;
		if (!Import(Test, Gear, FLureGearRow::StaticStruct(), TEXT("DT_Gear.csv"))
			|| !Import(Test, Patterns, FLureFightPatternRow::StaticStruct(), TEXT("DT_FightPattern.json"))
			|| !Import(Test, Fight, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight.csv")))
		{
			return false;
		}
		const FLureFishFightRow* Row = Fight->FindRow<FLureFishFightRow>(TEXT("Default"), TEXT("test"), false);
		if (!Test.TestNotNull(TEXT("DT_FishFight row Default"), Row))
		{
			return false;
		}
		Out.Tuning = *Row;
		FLureGearLoadout Loadout;
		Loadout.Rod = TEXT("Rod_Starter");
		Loadout.Line = TEXT("Line_Mono");
		Loadout.Hook = TEXT("Hook_Shrimp");
		Out.Starter = FLureGear::Resolve(Gear.Get(), Loadout);
		FLureGear::ApplyDragLineCap(Out.Starter, Out.Tuning.DragLineCap);
		Patterns->ForeachRow<FLureFightPatternRow>(TEXT("test"), [&Out](const FName& Name, const FLureFightPatternRow& Pattern) { Out.Patterns.Add(Name, Pattern); });
		return Test.TestTrue(TEXT("the Run and Dive patterns load"), Out.Patterns.Contains(TEXT("Run")) && Out.Patterns.Contains(TEXT("Dive")));
	}

	FLureFightMove* FindMove(FLureFightPatternRow& Pattern, const TCHAR* Id)
	{
		const int32 Index = Pattern.FindMove(FName(Id));
		return Pattern.Moves.IsValidIndex(Index) ? &Pattern.Moves[Index] : nullptr;
	}

	/**
	 *  The tune before T-049/T-050 (the fishing-loop tune of 2026-09-23): StaminaPerStat 3, Rod_Starter ReelSpeed 120, and the
	 *  Run pattern opening with its Run (Weight 1.2, 0.8-1.5 s, Pull 2.4), Swim Pull 1.0, Rest Pull 0.3. Everything else as shipped.
	 */
	bool MakeOldTune(FAutomationTestBase& Test, const FTune& Shipped, FTune& Out)
	{
		Out = Shipped;
		Out.Tuning.StaminaPerStat = 3.f;
		Out.Starter.ReelSpeed = 120.f;
		FLureFightPatternRow& Run = Out.Patterns.FindChecked(TEXT("Run"));
		Run.OpeningMove = TEXT("Run");
		FLureFightMove* RunMove = FindMove(Run, TEXT("Run"));
		FLureFightMove* Swim = FindMove(Run, TEXT("Swim"));
		FLureFightMove* Rest = FindMove(Run, TEXT("Rest"));
		if (!Test.TestTrue(TEXT("the Run pattern has Run, Swim and Rest"), RunMove && Swim && Rest))
		{
			return false;
		}
		RunMove->Weight = 1.2f;
		RunMove->DurationMin = 0.8f;
		RunMove->DurationMax = 1.5f;
		RunMove->Pull = 2.4f;
		Swim->Pull = 1.f;
		Rest->Pull = 0.3f;
		return true;
	}

	/** A fish from the real roll pipeline: forced rarity, no modifiers, a weight in kg. */
	bool RollFish(FAutomationTestBase& Test, const FishQA::FTables& Tables, FName Species, FName Rarity, float Kg, int32 Seed, FFishInstance& Out)
	{
		const FFishSpeciesRow* Row = Tables.Species->FindRow<FFishSpeciesRow>(Species, TEXT("test"), false);
		if (!Test.TestNotNull(FString::Printf(TEXT("species %s"), *Species.ToString()), Row))
		{
			return false;
		}
		FFishRollContext Context;
		Context.SpeciesId = Species;
		Context.Seed = Seed;
		Context.ForcedRarityId = Rarity;
		Context.bForceModifiers = true;
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = (Kg - Row->WeightMin) / FMath::Max(0.01f, Row->WeightMax - Row->WeightMin);
		Context.RegionTag = FGameplayTag::RequestGameplayTag(TEXT("Region.Tropical.PalmKey"), false);
		Context.TimeOfDayHours = 12.f;
		return Test.TestTrue(FString::Printf(TEXT("roll a %.2f kg %s %s"), Kg, *Rarity.ToString(), *Species.ToString()), FFishRoll::Roll(Tables.Get(), Context, Out));
	}

	/** How a scripted player plays: the well-played watcher by default (see the file comment). */
	struct FPolicy
	{
		float EaseAbove = 0.85f;
		float ReelBelow = 0.6f;
		bool bSteer = true;
		bool bHoldReel = false;
		int32 ReelStep = INDEX_NONE;
	};

	FPolicy Hold(int32 Step = INDEX_NONE)
	{
		FPolicy Out;
		Out.bHoldReel = true;
		Out.bSteer = false;
		Out.ReelStep = Step;
		return Out;
	}

	struct FRun
	{
		ELureFightOutcome Outcome = ELureFightOutcome::None;
		float Elapsed = 0.f;
		/** Highest tension / line strength in the first 1.5 s, and over the fight. */
		float PeakOpening01 = 0.f;
		float Peak01 = 0.f;
		/** The largest drop of the fish's own pull (its move x its stamina, before the rod turns it) within any 1 s, as a share. */
		float WorstDrop = 0.f;
		float WorstDropAt = 0.f;
		/** Time spent easing off (not reeling). */
		float EasedSeconds = 0.f;
		/** The fish's stamina 5 s into the fight (1 if it ended sooner). */
		float StaminaAt5 = 1.f;
	};

	FString OutcomeName(ELureFightOutcome Outcome)
	{
		return StaticEnum<ELureFightOutcome>()->GetNameStringByValue(static_cast<int64>(Outcome));
	}

	FRun Play(const FTune& Tune, const FFishInstance& Fish, FName PatternId, int32 Seed, const FPolicy& Policy, float Start = StartLineOut)
	{
		FLureFightState S;
		const FLureFightPatternRow* Pattern = Tune.Patterns.Find(PatternId);
		FLureFight::Begin(S, FLureFight::MakeFish(Fish, Tune.Tuning, 1, GetDefault<UFishSettings>()->LevelScaling),
			Pattern ? *Pattern : FLureFightPatternRow::GetFallbackPattern(), PatternId, Tune.Starter, Tune.Tuning, Seed, Start);
		const float Dt = FLureFight::StepSeconds(S.Tuning);
		const int32 Window = FMath::Max(1, FMath::RoundToInt(1.f / Dt));
		FRun Out;
		bool bReel = true;
		float SinceBar = 1000.f;
		float SinceTurn = 0.f;
		int32 SeenKey = TNumericLimits<int32>::Min();
		int32 SeenDir = 0;
		TArray<float> Pulls;
		while (!S.IsOver() && S.Elapsed < 120.f)
		{
			// What the player sees 0.3 s after it happens: the fish's sideways direction, and the tension bar every 0.3 s.
			const int32 Key = (S.bExhausted ? -1 : S.MoveIndex) * 4 + (S.RunDir + 1);
			if (Key != SeenKey)
			{
				SeenKey = Key;
				SinceTurn = 0.f;
			}
			SinceTurn += Dt;
			if (SinceTurn >= Reaction - 1.0e-4f)
			{
				SeenDir = S.RunDir;
			}
			SinceBar += Dt;
			if (SinceBar >= Reaction - 1.0e-4f)
			{
				SinceBar = 0.f;
				const float Bar = S.Tension / S.Gear.LineStrength;
				bReel = bReel ? Bar < Policy.EaseAbove : Bar < Policy.ReelBelow;
			}
			FLureFightInput Input;
			Input.bReeling = Policy.bHoldReel || bReel;
			Input.RodYaw = Policy.bSteer ? -static_cast<float>(SeenDir) : 0.f;
			Input.ReelStep = Policy.ReelStep;
			FLureFight::Step(S, Input);

			const float Tension01 = S.Tension / S.Gear.LineStrength;
			Out.Peak01 = FMath::Max(Out.Peak01, Tension01);
			if (S.Elapsed <= 1.5f + 1.0e-4f)
			{
				Out.PeakOpening01 = FMath::Max(Out.PeakOpening01, Tension01);
			}
			Out.EasedSeconds += Input.bReeling ? 0.f : Dt;
			if (S.Elapsed <= 5.f + 1.0e-4f)
			{
				Out.StaminaAt5 = S.Stamina;
			}
			if (!S.bExhausted)
			{
				const float Pull = FLureFight::FishPull(S.Fish, S.GetMove(), S.Stamina, S.Tuning);
				Pulls.Add(Pull);
				float Max = 0.f;
				for (int32 Index = FMath::Max(0, Pulls.Num() - 1 - Window); Index < Pulls.Num(); ++Index)
				{
					Max = FMath::Max(Max, Pulls[Index]);
				}
				const float Drop = Max > 0.f ? 1.f - Pull / Max : 0.f;
				if (Drop > Out.WorstDrop)
				{
					Out.WorstDrop = Drop;
					Out.WorstDropAt = S.Elapsed;
				}
			}
		}
		Out.Outcome = S.Outcome;
		Out.Elapsed = S.Elapsed;
		return Out;
	}

	float Median(TArray<float> Values)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		Values.Sort();
		return Values.Num() % 2 ? Values[Values.Num() / 2] : 0.5f * (Values[Values.Num() / 2 - 1] + Values[Values.Num() / 2]);
	}

	/** Shared setup: the shipped tune, the old tune and the fish tables. */
	struct FSetup
	{
		FTune Shipped;
		FTune Old;
		FishQA::FTables Fish;

		bool Load(FAutomationTestBase& Test)
		{
			return LoadShipped(Test, Shipped) && MakeOldTune(Test, Shipped, Old) && FishQA::LoadReal(Test, Fish);
		}
	};

	/** Beginner bonefish for the evenness tests: Commons of 1.0, 1.5 (the reference) and 2.0 kg. */
	bool BeginnerBonefish(FAutomationTestBase& Test, const FSetup& Setup, TArray<FFishInstance>& Out)
	{
		int32 Seed = 40;
		for (const float Kg : { 1.f, BonefishReferenceKg, 2.f })
		{
			FFishInstance Fish;
			if (!RollFish(Test, Setup.Fish, TEXT("Bonefish"), TEXT("Common"), Kg, ++Seed, Fish))
			{
				return false;
			}
			Out.Add(Fish);
		}
		return true;
	}

	/** Worst values of a tune over the beginner bonefish x NumSeeds fights. */
	struct FWorst
	{
		float PeakOpening01 = 0.f;
		float Drop = 0.f;
		FString DropWhere;
		int32 Lost = 0;
	};

	FWorst Worst(const FTune& Tune, const TArray<FFishInstance>& Fish)
	{
		FWorst Out;
		for (const FFishInstance& One : Fish)
		{
			for (int32 Seed = 1; Seed <= NumSeeds; ++Seed)
			{
				const FRun Run = Play(Tune, One, TEXT("Run"), Seed, FPolicy());
				Out.PeakOpening01 = FMath::Max(Out.PeakOpening01, Run.PeakOpening01);
				Out.Lost += Run.Outcome == ELureFightOutcome::Landed ? 0 : 1;
				if (Run.WorstDrop > Out.Drop)
				{
					Out.Drop = Run.WorstDrop;
					Out.DropWhere = FString::Printf(TEXT("%.2f kg, seed %d, at %.1f s"), One.WeightKg, Seed, Run.WorstDropAt);
				}
			}
		}
		return Out;
	}

	/** Median landing time of the reference bonefish over NumSeeds fights (and the range, losses). */
	struct FLandTimes
	{
		float Median = 0.f;
		float Min = 0.f;
		float Max = 0.f;
		int32 Lost = 0;
	};

	FLandTimes LandTimes(const FTune& Tune, const FFishInstance& Fish, const FPolicy& Policy)
	{
		TArray<float> Times;
		FLandTimes Out;
		for (int32 Seed = 1; Seed <= NumSeeds; ++Seed)
		{
			const FRun Run = Play(Tune, Fish, TEXT("Run"), Seed, Policy);
			if (Run.Outcome == ELureFightOutcome::Landed)
			{
				Times.Add(Run.Elapsed);
			}
			else
			{
				++Out.Lost;
			}
		}
		Times.Sort();
		Out.Median = Median(Times);
		Out.Min = Times.Num() ? Times[0] : 0.f;
		Out.Max = Times.Num() ? Times.Last() : 0.f;
		return Out;
	}

	// =================================================================================================================
	// T-049: an even fight
	// =================================================================================================================

	/** (a) The hook set ramps in: in the first 1.5 s the well-played beginner bonefish stays at or below 75 % of the line. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightEvenHookSet, "Project.Fishing.Fight.Even.HookSetRampsIn", Flags)
	bool FFightEvenHookSet::RunTest(const FString& Parameters)
	{
		FSetup Setup;
		TArray<FFishInstance> Fish;
		if (!Setup.Load(*this) || !BeginnerBonefish(*this, Setup, Fish))
		{
			return false;
		}
		const FWorst New = Worst(Setup.Shipped, Fish);
		const FWorst Old = Worst(Setup.Old, Fish);
		AddInfo(FString::Printf(TEXT("peak tension in the first 1.5 s (1.0/1.5/2.0 kg Commons x %d seeds): shipped %.0f %%, old tune %.0f %%"),
			NumSeeds, 100.f * New.PeakOpening01, 100.f * Old.PeakOpening01));
		TestTrue(FString::Printf(TEXT("the first 1.5 s stay at or below 75 %% of the line (%.0f %%)"), 100.f * New.PeakOpening01), New.PeakOpening01 <= 0.75f);
		TestTrue(FString::Printf(TEXT("the old tune spiked past it at the hook set (%.0f %%): the test catches Jimmy's 'almost breaks my rod'"), 100.f * Old.PeakOpening01),
			Old.PeakOpening01 > 0.75f);
		TestEqual(TEXT("well-played beginner bonefish all land"), New.Lost, 0);
		return true;
	}

	/**
	 *  (b) No cliff: before the fish is exhausted its own pull (move x stamina, before the rod turns it) never falls by more
	 *  than 60 % within 1 s. A fish that goes from a hard run to a near-limp rest in a moment is what "after less than a second it
	 *  will then calm down" felt like; the old tune dropped by about 90 %.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightEvenNoCliff, "Project.Fishing.Fight.Even.NoCliff", Flags)
	bool FFightEvenNoCliff::RunTest(const FString& Parameters)
	{
		FSetup Setup;
		TArray<FFishInstance> Fish;
		if (!Setup.Load(*this) || !BeginnerBonefish(*this, Setup, Fish))
		{
			return false;
		}
		const FWorst New = Worst(Setup.Shipped, Fish);
		const FWorst Old = Worst(Setup.Old, Fish);
		AddInfo(FString::Printf(TEXT("largest 1 s drop of the fish's pull: shipped %.0f %% (%s), old tune %.0f %% (%s)"),
			100.f * New.Drop, *New.DropWhere, 100.f * Old.Drop, *Old.DropWhere));
		TestTrue(FString::Printf(TEXT("the pull never drops by more than 60 %% within 1 s (%.0f %%, %s)"), 100.f * New.Drop, *New.DropWhere), New.Drop <= 0.6f);
		TestTrue(FString::Printf(TEXT("the old tune had the cliff (%.0f %%)"), 100.f * Old.Drop), Old.Drop > 0.6f);
		return true;
	}

	/**
	 *  Tough fish stay tough: the reference Coral Snapper (Common 2.5 kg, level 3) snaps the starter line when you hold reel at the
	 *  fastest step, every time; the well-played player lands most of them (tension management matters, it is not luck).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightEvenToughFish, "Project.Fishing.Fight.Even.ToughFishNeedsTensionManagement", Flags)
	bool FFightEvenToughFish::RunTest(const FString& Parameters)
	{
		FSetup Setup;
		FFishInstance Snapper;
		if (!Setup.Load(*this) || !RollFish(*this, Setup.Fish, TEXT("CoralSnapper"), TEXT("Common"), 2.5f, 21, Snapper))
		{
			return false;
		}
		const int32 Fastest = FLureFight::NumReelSteps(Setup.Shipped.Tuning) - 1;
		int32 HeldSnaps = 0, WellLanded = 0;
		TArray<float> WellTimes;
		for (int32 Seed = 1; Seed <= NumSeeds; ++Seed)
		{
			HeldSnaps += Play(Setup.Shipped, Snapper, TEXT("Dive"), Seed, Hold(Fastest)).Outcome == ELureFightOutcome::Snapped ? 1 : 0;
			const FRun Well = Play(Setup.Shipped, Snapper, TEXT("Dive"), Seed, FPolicy());
			if (Well.Outcome == ELureFightOutcome::Landed)
			{
				++WellLanded;
				WellTimes.Add(Well.Elapsed);
			}
		}
		AddInfo(FString::Printf(TEXT("reference snapper, %d seeds: hold at the fastest step snaps %d; well-played lands %d (median %.1f s)"),
			NumSeeds, HeldSnaps, WellLanded, Median(WellTimes)));
		TestEqual(TEXT("holding reel at the fastest step snaps the starter line on every snapper"), HeldSnaps, NumSeeds);
		TestTrue(FString::Printf(TEXT("well-played lands at least 80 %% of them (%d of %d)"), WellLanded, NumSeeds), WellLanded * 5 >= NumSeeds * 4);
		return true;
	}

	// =================================================================================================================
	// T-050: a faster reel; the fight's length comes from the tension
	// =================================================================================================================

	/**
	 *  The well-played reference bonefish from a full cast lands in 11-15 s (median over the seeds), every fight in 9-18 s.
	 *  Lead decision 2026-09-24 (S1): Jimmy called A2's 15.8 s "way too slow" and wants beginner fish easy; the target is ~12-13 s
	 *  (was 15-20 s, every fight 12-25 s, with StaminaPerStat 18).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightLandTimeWellPlayed, "Project.Fishing.Fight.LandTime.WellPlayedBeginnerBonefish", Flags)
	bool FFightLandTimeWellPlayed::RunTest(const FString& Parameters)
	{
		FSetup Setup;
		FFishInstance Fish;
		if (!Setup.Load(*this) || !RollFish(*this, Setup.Fish, TEXT("Bonefish"), TEXT("Common"), BonefishReferenceKg, 41, Fish))
		{
			return false;
		}
		const FLandTimes New = LandTimes(Setup.Shipped, Fish, FPolicy());
		const FLandTimes Old = LandTimes(Setup.Old, Fish, FPolicy());
		AddInfo(FString::Printf(TEXT("1.5 kg Common from 18 m, %d seeds, well-played: shipped median %.1f s (%.1f-%.1f, lost %d); old tune median %.1f s (%.1f-%.1f, lost %d)"),
			NumSeeds, New.Median, New.Min, New.Max, New.Lost, Old.Median, Old.Min, Old.Max, Old.Lost));
		// Handed to S2 (T-052): "pressure lasts" (the fish keeps half its stamina 5 s in) is reported here, not tested.
		float StaminaAt5Min = 1.f;
		for (int32 Seed = 1; Seed <= NumSeeds; ++Seed)
		{
			StaminaAt5Min = FMath::Min(StaminaAt5Min, Play(Setup.Shipped, Fish, TEXT("Run"), Seed, FPolicy()).StaminaAt5);
		}
		AddInfo(FString::Printf(TEXT("S2 note (T-052): stamina 5 s into the well-played fight >= %.2f"), StaminaAt5Min));
		TestEqual(TEXT("well-played lands every one"), New.Lost, 0);
		TestTrue(FString::Printf(TEXT("median landing time 11-15 s (%.1f s)"), New.Median), New.Median >= 11.f && New.Median <= 15.f);
		TestTrue(FString::Printf(TEXT("every fight lands in 9-18 s (%.1f-%.1f s)"), New.Min, New.Max), New.Min >= 9.f && New.Max <= 18.f);
		return true;
	}

	/**
	 *  Jimmy: "the speed of the battle should be dictated by the tension management (allowing a fish to run as to not snap your
	 *  line increases fight time)". (1) The reel is faster than the old one (T-050: ReelSpeed +50 %), so cranking the whole distance
	 *  in takes less time. (2) A bold player (eases at 95 %, reels under 80 %) is no slower than well-played, and well-played and
	 *  timid players (ease at 60 %, reel under 40 %) land every one.
	 *  Lead decision 2026-09-24 (S1, StaminaPerStat 5): "the crank is at most 60 % of the fight" and "timid takes >= 1.3x" are
	 *  handed to Sprint 2's fight design (T-052); both need a fish that keeps pulling (stamina/tension-driven pacing). Today's
	 *  numbers are in docs/specs/reel-fight-rules.md ("Handed to S2 (T-052)") and in this test's info line.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightLandTimeTension, "Project.Fishing.Fight.LandTime.TensionSetsThePace", Flags)
	bool FFightLandTimeTension::RunTest(const FString& Parameters)
	{
		FSetup Setup;
		FFishInstance Fish;
		if (!Setup.Load(*this) || !RollFish(*this, Setup.Fish, TEXT("Bonefish"), TEXT("Common"), BonefishReferenceKg, 41, Fish))
		{
			return false;
		}
		FPolicy Timid;
		Timid.EaseAbove = 0.6f;
		Timid.ReelBelow = 0.4f;
		FPolicy Bold;
		Bold.EaseAbove = 0.95f;
		Bold.ReelBelow = 0.8f;
		struct FPace { float Crank = 0.f; float Well = 0.f; float Timid = 0.f; float Bold = 0.f; int32 Lost = 0; };
		auto Pace = [&](const FTune& Tune)
		{
			FPace Out;
			const float Speed = Tune.Starter.ReelSpeed * FLureFight::ReelStepSpeed(FLureFight::DefaultReelStep(Tune.Tuning), Tune.Tuning);
			Out.Crank = (StartLineOut - Tune.Tuning.LandDistance) / FMath::Max(1.f, Speed);
			const FLandTimes Well = LandTimes(Tune, Fish, FPolicy());
			const FLandTimes TimidTimes = LandTimes(Tune, Fish, Timid);
			const FLandTimes BoldTimes = LandTimes(Tune, Fish, Bold);
			Out.Well = Well.Median;
			Out.Timid = TimidTimes.Median;
			Out.Bold = BoldTimes.Median;
			Out.Lost = Well.Lost + TimidTimes.Lost;
			return Out;
		};
		const FPace New = Pace(Setup.Shipped);
		const FPace Old = Pace(Setup.Old);
		AddInfo(FString::Printf(TEXT("1.5 kg Common from 18 m (medians): shipped crank %.1f s, well-played %.1f s, timid %.1f s, bold %.1f s; old tune crank %.1f s, well-played %.1f s, timid %.1f s, bold %.1f s"),
			New.Crank, New.Well, New.Timid, New.Bold, Old.Crank, Old.Well, Old.Timid, Old.Bold));
		AddInfo(FString::Printf(TEXT("S2 notes (T-052): crank share %.0f %% (old %.0f %%), timid x%.2f (old x%.2f)"), 100.f * New.Crank / FMath::Max(0.01f, New.Well),
			100.f * Old.Crank / FMath::Max(0.01f, Old.Well), New.Timid / FMath::Max(0.01f, New.Well), Old.Timid / FMath::Max(0.01f, Old.Well)));
		TestTrue(FString::Printf(TEXT("the faster reel cranks the distance in quicker than the old one (%.1f vs %.1f s)"), New.Crank, Old.Crank), New.Crank < Old.Crank);
		TestTrue(FString::Printf(TEXT("bold is no slower (%.1f vs %.1f s)"), New.Bold, New.Well), New.Bold <= New.Well);
		TestEqual(TEXT("well-played and timid players land every one"), New.Lost, 0);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

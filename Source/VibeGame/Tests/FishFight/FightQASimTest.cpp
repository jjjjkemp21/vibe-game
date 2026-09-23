// QA-owned independent tests for the pure reel fight simulation (T-007): Project.Fishing.Fight.QA.{Math,Snap,Slack,Spool,Outcome,Sim,Gear}.*
// Written by the qa-engineer from docs/specs/reel-fight-rules.md and the formula comment in Fishing/FishFight.h (black-box).
// No world: FLureFight is pure, so every test drives it step by step. Timing fixtures use instant tension easing so the time
// spent over or under a threshold is an exact whole number of fixed steps.

#include "FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

namespace LureFightQA
{
	// =================================================================================================================
	// Math at the edges: 0 drag, a locked (huge) drag, drag = line strength, rod power 0, the formula switch points
	// =================================================================================================================

	/** Drag 0: letting the fish run is a free spool (no tension, the fish takes line at its full swim speed), so it throws the hook. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAMathZeroDrag, "Project.Fishing.Fight.QA.Math.ZeroDragFreeSpool", Flags)
	bool FLureFightQAMathZeroDrag::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureGearStats Free = MakeGear(8.f, 120.f, 0.f, 10.f, 100000.f, 1.f);
		for (const float Pull : { 0.5f, 5.f, 50.f, 5000.f })
		{
			TestEqual(FString::Printf(TEXT("drag 0, pull %.1f, not reeling: target tension = min(Pull, Drag) = 0"), Pull), FLureFight::TargetTension(Pull, false, Free, T), 0.f);
			TestEqual(FString::Printf(TEXT("drag 0, pull %.1f, not reeling: the fish takes line at its full away speed"), Pull), FLureFight::LineTakenSpeed(Pull, 100.f, false, Free, T), 100.f);
		}
		const float ZeroPull = FLureFight::LineTakenSpeed(0.f, 100.f, false, Free, T);
		TestTrue(FString::Printf(TEXT("drag 0, no pull: the line taken is finite and within [0, away speed] (%f)"), ZeroPull), FMath::IsFinite(ZeroPull) && ZeroPull >= 0.f && ZeroPull <= 100.f);
		TestNearlyEqual(TEXT("drag 0 does not change reeling: target = Pull x ReelStrain + RodPower x ReelLoad"), FLureFight::TargetTension(5.f, true, Free, T), 5.f * T.ReelStrain + 8.f * T.ReelLoad, 1.0e-4f);

		// A fish swimming straight away at 100 cm/s (a never-tiring pull of 4), never reeled, on a free spool.
		FLureFightState S = SteadyFight(T, 4.f, MakeMove(TEXT("Away"), 1.f, 1.f, 1.f), Free, 1000.f, 100.f);
		bool bFinite = true;
		float MaxTension = 0.f;
		while (!S.IsOver() && S.Steps < 60 * 60)
		{
			FLureFight::Step(S, Input(false));
			bFinite &= AllFinite(S);
			MaxTension = FMath::Max(MaxTension, S.Tension);
		}
		const float Grace = T.SlackGraceTime * Free.HookSecurity;
		TestTrue(TEXT("every step finite"), bFinite);
		TestEqual(TEXT("free spool: the line never loads (tension stays 0)"), MaxTension, 0.f);
		TestEqual(TEXT("free spool, never reeled: the fish throws the hook"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::ThrewHook));
		TestTrue(FString::Printf(TEXT("... after the slack grace %.2f s (%.3f s)"), Grace, S.Elapsed), S.Elapsed >= Grace - 2.f * WorldDt && S.Elapsed <= Grace + 2.f * WorldDt);
		TestNearlyEqual(TEXT("... while the line ran out at the full swim speed"), S.LineOut, 1000.f + 100.f * S.Elapsed, 0.5f);
		return true;
	}

	/** A locked drag (huge, or FLT_MAX) never gives line: letting it run holds the fish at its full pull, which snaps a weaker line. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAMathLockedDrag, "Project.Fishing.Fight.QA.Math.MaxDragLocksTheLine", Flags)
	bool FLureFightQAMathLockedDrag::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		for (const float BigDrag : { 1.0e6f, std::numeric_limits<float>::max() })
		{
			const FLureGearStats Locked = MakeGear(8.f, 120.f, BigDrag, 10.f, 100000.f, 1.f);
			const FString Label = FString::Printf(TEXT("drag %g"), BigDrag);
			TestEqual(Label + TEXT(": not reeling, the target tension is the full pull"), FLureFight::TargetTension(20.f, false, Locked, T), 20.f);
			TestEqual(Label + TEXT(": the fish can't take line"), FLureFight::LineTakenSpeed(20.f, 100.f, false, Locked, T), 0.f);
			TestTrue(Label + TEXT(": finite"), FMath::IsFinite(FLureFight::LineTakenSpeed(1.0e6f, 1.0e4f, false, Locked, T)) && FMath::IsFinite(FLureFight::TargetTension(1.0e6f, false, Locked, T)));

			// A never-tiring fish pulling 20 on a 10 line swims away at 100 cm/s: the line snaps and never goes out.
			FLureFightState Strong = SteadyFight(T, 20.f, MakeMove(TEXT("Away"), 1.f, 1.f, 1.f), Locked, 1000.f, 100.f);
			StepUntilOver(Strong, false, 60 * 60);
			TestEqual(Label + TEXT(": a pull over the line's strength snaps it"), OutcomeName(Strong.Outcome), OutcomeName(ELureFightOutcome::Snapped));
			TestEqual(Label + TEXT(": ... and the line never went out"), Strong.LineOut, 1000.f);
			TestTrue(FString::Printf(TEXT("%s: ... within the grace plus the tension rise (%.2f s)"), *Label, Strong.Elapsed), Strong.Elapsed < T.SnapGraceTime + 1.f);

			// Pulling 8 on the 10 line: held for a minute, never snaps, never loses line.
			FLureFightState Weak = SteadyFight(T, 8.f, MakeMove(TEXT("Away"), 1.f, 1.f, 1.f), Locked, 1000.f, 100.f);
			StepUntilOver(Weak, false, 60 * 60);
			TestEqual(Label + TEXT(": a pull under the line's strength is held (1 min)"), OutcomeName(Weak.Outcome), OutcomeName(ELureFightOutcome::None));
			TestEqual(Label + TEXT(": ... the line stays where it was"), Weak.LineOut, 1000.f);
			TestNearlyEqual(Label + TEXT(": ... at the fish's pull"), Weak.Tension, 8.f, 1.0e-3f);
		}
		return true;
	}

	/** Letting it run caps the tension at the drag: a drag exactly at the line's strength never snaps it ("above" the strength snaps). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAMathDragAtStrength, "Project.Fishing.Fight.QA.Math.DragAtLineStrengthNeverSnaps", Flags)
	bool FLureFightQAMathDragAtStrength::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& Shipped = *Data.Tuning();
		const FLureGearStats AtStrength = MakeGear(8.f, 120.f, 10.f, 10.f, 100000.f, 1.f);
		for (const bool bInstant : { false, true })
		{
			const FLureFishFightRow Tuning = bInstant ? InstantTuning(Shipped, 60) : Shipped;
			const FString Label = bInstant ? TEXT("instant tension") : TEXT("shipped easing");
			FLureFightState S = SteadyFight(Tuning, 25.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), AtStrength, 1000.f);
			float MaxTension = 0.f;
			while (!S.IsOver() && S.Steps < 60 * 60)
			{
				FLureFight::Step(S, Input(false));
				MaxTension = FMath::Max(MaxTension, S.Tension);
			}
			TestEqual(Label + TEXT(": drag = line strength, pull 25, not reeling: held for a minute"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::None));
			TestTrue(FString::Printf(TEXT("%s: the tension never goes above the line's strength (max %.6f)"), *Label, MaxTension), MaxTension <= AtStrength.LineStrength);
		}
		const FLureGearStats Above = MakeGear(8.f, 120.f, 10.01f, 10.f, 100000.f, 1.f);
		FLureFightState S = SteadyFight(InstantTuning(Shipped, 60), 25.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), Above, 1000.f);
		StepUntilOver(S, false, 60 * 60);
		TestEqual(TEXT("drag 10.01 on a 10 line: the tension is above the strength, so it snaps"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		return true;
	}

	/** Rod power 0 (a bad row that slipped past the data): no line is ever gained, every number stays finite, and DT_Gear refuses such rows. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAMathZeroRodPower, "Project.Fishing.Fight.QA.Math.ZeroRodPowerIsSafe", Flags)
	bool FLureFightQAMathZeroRodPower::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureGearStats NoPower = MakeGear(0.f, 120.f, 5.f, 10.f, 100000.f, 1.f);
		for (const float Pull : { 0.f, 1.0e-6f, 3.f, 1.0e6f })
		{
			const FString Label = FString::Printf(TEXT("rod power 0, pull %g"), Pull);
			const float Gain = FLureFight::LineGainSpeed(Pull, true, NoPower);
			const float Taken = FLureFight::LineTakenSpeed(Pull, 100.f, true, NoPower, T);
			const float Target = FLureFight::TargetTension(Pull, true, NoPower, T);
			TestEqual(Label + TEXT(": reeling gains no line"), Gain, 0.f);
			TestTrue(FString::Printf(TEXT("%s: the line taken while reeling is finite and within [0, 100] (%f)"), *Label, Taken), FMath::IsFinite(Taken) && Taken >= 0.f && Taken <= 100.f);
			TestNearlyEqual(Label + TEXT(": reeling target = Pull x ReelStrain (no rod load)"), Target, Pull * T.ReelStrain, FMath::Max(1.0e-4f, 1.0e-6f * Pull));
		}
		TestEqual(TEXT("rod power 0: a fish pulling at all takes line at full speed even while you crank"), FLureFight::LineTakenSpeed(3.f, 100.f, true, NoPower, T), 100.f);

		// Reeling a still fish with no rod power never brings it in.
		FLureFightState S = SteadyFight(T, 2.f, MakeMove(TEXT("Still"), 1.f, 0.f, 0.f), NoPower, 1000.f);
		bool bFinite = true;
		for (int32 Step = 0; Step < 600 && !S.IsOver(); ++Step)
		{
			FLureFight::Step(S, Input(true));
			bFinite &= AllFinite(S);
		}
		TestTrue(TEXT("rod power 0: every step finite"), bFinite);
		TestEqual(TEXT("rod power 0: 10 s of reeling gains nothing"), S.LineOut, 1000.f);

		// DT_Gear: a rod with power 0 or below is refused; the loadout falls back to the built-in starter rod.
		const FString Csv = TEXT("Name,Slot,DisplayName,Price,RodPower,ReelSpeed,Drag,CastDistanceMultiplier,LineStrength,SpoolLength,HookSecurity,BaitTag,Luck,DevComment\n")
			TEXT("Rod_Zero,Rod,Zero,0,0,120,5,1.0,0,0,0,None,0,qa\n")
			TEXT("Rod_Negative,Rod,Negative,0,-3,120,5,1.0,0,0,0,None,0,qa\n");
		TStrongObjectPtr<UDataTable> Gear;
		if (!MakeTableChecked(*this, Gear, FLureGearRow::StaticStruct(), Csv, false, TEXT("fixture DT_Gear")))
		{
			return false;
		}
		const FLureGearRow BuiltIn = FLureGear::GetFallbackItem(ELureGearSlot::Rod);
		for (const TCHAR* Id : { TEXT("Rod_Zero"), TEXT("Rod_Negative") })
		{
			FString Problem;
			TestFalse(FString::Printf(TEXT("%s can't be equipped"), Id), FLureGear::CanEquip(Gear.Get(), ELureGearSlot::Rod, Id, &Problem));
			FLureGearLoadout Loadout;
			Loadout.Rod = Id;
			TArray<FString> Problems;
			const FLureGearStats Stats = FLureGear::Resolve(Gear.Get(), Loadout, &Problems);
			TestTrue(FString::Printf(TEXT("%s: the loadout uses the built-in rod"), Id), Stats.bUsedFallback && Stats.RodId.IsNone() && Stats.RodPower == BuiltIn.RodPower && Stats.RodPower > 0.f);
			TestTrue(FString::Printf(TEXT("%s: the problem is reported"), Id), Problems.ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("Rod")); }));
		}
		FLureGearRow NaNRod = BuiltIn;
		NaNRod.RodPower = std::numeric_limits<float>::quiet_NaN();
		FString Problem;
		TestFalse(TEXT("a rod with a NaN power is invalid"), NaNRod.Validate(Problem));
		return true;
	}

	/** The documented switch points of step 3 and 4 (spec formulas restated with exact numbers). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAMathSwitchPoints, "Project.Fishing.Fight.QA.Math.FormulaSwitchPoints", Flags)
	bool FLureFightQAMathSwitchPoints::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		FLureFishFightRow T = *Data.Tuning();
		T.DragHold = 0.5f;
		T.ReelStrain = 1.25f;
		T.ReelLoad = 0.25f;
		const FLureGearStats G = MakeGear(8.f, 120.f, 4.f, 10.f, 100000.f, 1.f);
		// Reeling: Gain = ReelSpeed x clamp(1 - Pull / RodPower, 0, 1); Taken = Va x clamp(Pull / RodPower - 1, 0, 1).
		TestNearlyEqual(TEXT("reeling, pull 0: full reel speed"), FLureFight::LineGainSpeed(0.f, true, G), 120.f, 1.0e-4f);
		TestNearlyEqual(TEXT("reeling, pull = half the rod's power: half the reel speed"), FLureFight::LineGainSpeed(4.f, true, G), 60.f, 1.0e-4f);
		TestNearlyEqual(TEXT("reeling, pull = rod power: no gain"), FLureFight::LineGainSpeed(8.f, true, G), 0.f, 1.0e-4f);
		TestNearlyEqual(TEXT("reeling, pull = rod power: no line taken either (stalemate)"), FLureFight::LineTakenSpeed(8.f, 100.f, true, G, T), 0.f, 1.0e-4f);
		TestNearlyEqual(TEXT("reeling, pull = 1.5 x rod power: half the away speed"), FLureFight::LineTakenSpeed(12.f, 100.f, true, G, T), 50.f, 1.0e-3f);
		TestNearlyEqual(TEXT("reeling, pull = 2 x rod power: the full away speed"), FLureFight::LineTakenSpeed(16.f, 100.f, true, G, T), 100.f, 1.0e-3f);
		TestNearlyEqual(TEXT("reeling, pull = 3 x rod power: still the full away speed (clamped)"), FLureFight::LineTakenSpeed(24.f, 100.f, true, G, T), 100.f, 1.0e-3f);
		// Not reeling: Taken = Va x clamp((Pull / Drag - DragHold) / (1 - DragHold), 0, 1).
		TestNearlyEqual(TEXT("not reeling, pull = DragHold x Drag: the drag holds"), FLureFight::LineTakenSpeed(2.f, 100.f, false, G, T), 0.f, 1.0e-3f);
		TestNearlyEqual(TEXT("not reeling, pull half way to the drag: half speed"), FLureFight::LineTakenSpeed(3.f, 100.f, false, G, T), 50.f, 1.0e-3f);
		TestNearlyEqual(TEXT("not reeling, pull = drag: full speed"), FLureFight::LineTakenSpeed(4.f, 100.f, false, G, T), 100.f, 1.0e-3f);
		TestNearlyEqual(TEXT("not reeling, pull = 2 x drag: full speed (clamped)"), FLureFight::LineTakenSpeed(8.f, 100.f, false, G, T), 100.f, 1.0e-3f);
		FLureFishFightRow NoHold = T;
		NoHold.DragHold = 0.f;
		TestNearlyEqual(TEXT("DragHold 0: the drag gives line in proportion from the first pull"), FLureFight::LineTakenSpeed(2.f, 100.f, false, G, NoHold), 50.f, 1.0e-3f);
		FLureFishFightRow Steep = T;
		Steep.DragHold = 0.9f;
		TestNearlyEqual(TEXT("DragHold 0.9: nothing at 0.9 x drag"), FLureFight::LineTakenSpeed(3.6f, 100.f, false, G, Steep), 0.f, 0.05f);
		TestNearlyEqual(TEXT("DragHold 0.9: half at 0.95 x drag"), FLureFight::LineTakenSpeed(3.8f, 100.f, false, G, Steep), 50.f, 0.1f);
		// Toward you: the line shortens by the swim, whatever the input or the pull.
		for (const bool bReel : { false, true })
		{
			for (const float Pull : { 0.f, 4.f, 30.f })
			{
				TestNearlyEqual(FString::Printf(TEXT("swimming toward you (reeling %d, pull %.0f): taken = the negative away speed"), bReel, Pull), FLureFight::LineTakenSpeed(Pull, -80.f, bReel, G, T), -80.f, 1.0e-4f);
			}
		}
		// Step 4 targets.
		TestNearlyEqual(TEXT("reeling target at pull 0 = RodPower x ReelLoad"), FLureFight::TargetTension(0.f, true, G, T), 2.f, 1.0e-4f);
		TestNearlyEqual(TEXT("reeling target at pull 4 = 4 x 1.25 + 8 x 0.25"), FLureFight::TargetTension(4.f, true, G, T), 7.f, 1.0e-4f);
		TestNearlyEqual(TEXT("not reeling target at pull 3 = the pull"), FLureFight::TargetTension(3.f, false, G, T), 3.f, 1.0e-4f);
		TestNearlyEqual(TEXT("not reeling target at pull 100 = the drag"), FLureFight::TargetTension(100.f, false, G, T), 4.f, 1.0e-4f);
		// Easing: exact exponential, rise and fall time constants, 0 = instant, no time = no change.
		FLureFishFightRow E = T;
		E.TensionRiseTime = 0.2f;
		E.TensionFallTime = 0.5f;
		TestNearlyEqual(TEXT("rise: 1 - e^-(dt / rise)"), FLureFight::EaseTension(2.f, 12.f, 0.1f, E), 2.f + 10.f * (1.f - FMath::Exp(-0.5f)), 1.0e-4f);
		TestNearlyEqual(TEXT("fall: 1 - e^-(dt / fall)"), FLureFight::EaseTension(12.f, 2.f, 0.1f, E), 12.f - 10.f * (1.f - FMath::Exp(-0.2f)), 1.0e-4f);
		TestNearlyEqual(TEXT("no time passes: no change"), FLureFight::EaseTension(3.f, 12.f, 0.f, E), 3.f, 1.0e-6f);
		E.TensionFallTime = 0.f;
		TestNearlyEqual(TEXT("fall time 0: instant"), FLureFight::EaseTension(12.f, 2.f, 0.1f, E), 2.f, 1.0e-6f);
		return true;
	}

	/** Fish records with missing, NaN, infinite, negative or huge stats give a finite fight fish, and the fight stays finite. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAMathDegenerateFish, "Project.Fishing.Fight.QA.Math.DegenerateFishIsSafe", Flags)
	bool FLureFightQAMathDegenerateFish::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FFishLevelScaling& Scaling = GetDefault<UFishSettings>()->LevelScaling;
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();

		auto Make = [&](const TCHAR* Name, TArray<float> Values, float Difficulty, int32 Level)
		{
			FFishInstance Fish;
			Fish.SpeciesId = Name;
			Fish.Level = Level;
			Fish.DifficultyRating = Difficulty;
			const FGameplayTag Tags[] = { T.StrengthStat, T.StaminaStat, T.SpeedStat, T.AggressionStat };
			for (int32 Index = 0; Index < Values.Num() && Index < 4; ++Index)
			{
				Fish.Stats.Add(FFishStatValue(Tags[Index], Values[Index]));
			}
			return Fish;
		};
		const TArray<FFishInstance> Fishes = {
			Make(TEXT("QA_NoStats"), {}, 0.f, 0),
			Make(TEXT("QA_NaN"), { NaN, NaN, NaN, NaN }, NaN, 1),
			Make(TEXT("QA_Inf"), { Inf, Inf, Inf, Inf }, Inf, MAX_int32),
			Make(TEXT("QA_Negative"), { -10.f, -5.f, -3.f, -1.f }, -2.f, MIN_int32),
			Make(TEXT("QA_StatMax"), { 1000.f, 1000.f, 1000.f, 1000.f }, 100.f, 60),
			Make(TEXT("QA_Huge"), { 1.0e6f, 1.0e6f, 1.0e6f, 1.0e6f }, 1.0e6f, 1000),
		};
		for (const FFishInstance& Fish : Fishes)
		{
			const FString Name = Fish.SpeciesId.ToString();
			const FLureFightFish F = FLureFight::MakeFish(Fish, T, 1, Scaling);
			TestTrue(Name + TEXT(": base pull finite and > 0"), FMath::IsFinite(F.BasePull) && F.BasePull > 0.f);
			TestTrue(Name + TEXT(": base speed finite and >= 0"), FMath::IsFinite(F.BaseSpeed) && F.BaseSpeed >= 0.f);
			TestTrue(Name + TEXT(": stamina pool finite and > 0"), FMath::IsFinite(F.StaminaPool) && F.StaminaPool > 0.f);
			TestTrue(Name + TEXT(": aggression finite and >= 0"), FMath::IsFinite(F.Aggression) && F.Aggression >= 0.f);
			TestTrue(Name + TEXT(": rest scale finite and > 0"), FMath::IsFinite(F.RestScale) && F.RestScale > 0.f);
			TestTrue(Name + TEXT(": level multiplier finite and > 0"), FMath::IsFinite(F.LevelMultiplier) && F.LevelMultiplier > 0.f);
			for (const EPlayer Player : { EPlayer::Hold, EPlayer::Never })
			{
				FLureFightState S;
				FLureFight::Begin(S, F, FLureFightPatternRow::GetFallbackPattern(), NAME_None, Data.Starter(), T, 5, 1200.f);
				bool bFinite = AllFinite(S);
				bool bReel = Player == EPlayer::Hold;
				while (!S.IsOver() && S.Elapsed < 120.f)
				{
					FLureFight::Step(S, Input(bReel));
					bFinite &= AllFinite(S);
				}
				TestTrue(FString::Printf(TEXT("%s (%s): every step of the fight is finite (%s after %.1f s)"), *Name, bReel ? TEXT("hold") : TEXT("never"),
					*OutcomeName(S.Outcome), S.Elapsed), bFinite);
			}
		}
		const FLureFightFish Empty = FLureFight::MakeFish(Fishes[0], T, 1, Scaling);
		TestEqual(TEXT("no speed stat: the fish does not swim"), Empty.BaseSpeed, 0.f);
		TestEqual(TEXT("no aggression stat: aggression 0"), Empty.Aggression, 0.f);
		return true;
	}

	// =================================================================================================================
	// Snap: "tension above LineStrength longer than SnapGraceTime"
	// =================================================================================================================

	/**
	 *  The boundary: over the line's strength for EXACTLY SnapGraceTime is not "longer than" it, so the line still holds;
	 *  one fixed step more and it snaps. Grace values here are whole numbers of steps at their SimRate (first case = the shipped data).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASnapExactlyAtGrace, "Project.Fishing.Fight.QA.Snap.ExactlyAtGraceHolds", Flags)
	bool FLureFightQASnapExactlyAtGrace::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& Shipped = *Data.Tuning();
		struct FCase { int32 SimRate; float Grace; };
		const FCase Cases[] = { { Shipped.SimRate, Shipped.SnapGraceTime }, { 60, 1.f }, { 10, 0.5f }, { 20, 0.5f }, { 120, 0.25f }, { 240, 0.6f } };
		for (const FCase& Case : Cases)
		{
			const float StepsExact = Case.Grace * static_cast<float>(Case.SimRate);
			const int32 N = FMath::RoundToInt(StepsExact);
			if (!TestTrue(FString::Printf(TEXT("fixture: grace %.3f s is a whole number of steps at %d Hz"), Case.Grace, Case.SimRate), FMath::Abs(StepsExact - static_cast<float>(N)) < 1.0e-3f && N > 0))
			{
				continue;
			}
			FLureFishFightRow Tuning = InstantTuning(Shipped, Case.SimRate);
			Tuning.SnapGraceTime = Case.Grace;
			// A never-tiring pull of 20 on a 10 line, a locked drag, not reeling: over the strength from the first step on.
			FLureFightState S = SteadyFight(Tuning, 20.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), MakeGear(8.f, 120.f, 30.f, 10.f, 100000.f, 1.f), 1000.f);
			for (int32 Step = 0; Step < N && !S.IsOver(); ++Step)
			{
				FLureFight::Step(S, Input(false));
			}
			const FString Label = FString::Printf(TEXT("SimRate %d, SnapGraceTime %.2f s"), Case.SimRate, Case.Grace);
			TestFalse(FString::Printf(TEXT("%s: over the line's strength for exactly the grace (%d steps, OverTime %.9f) is not longer than the grace: the line holds (it %s after %d steps)"),
				*Label, N, S.OverTime, S.IsOver() ? TEXT("snapped") : TEXT("held"), S.Steps), S.IsOver());
			if (!S.IsOver())
			{
				FLureFight::Step(S, Input(false));
			}
			TestEqual(Label + TEXT(": one step later it snaps"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Snapped));
			TestEqual(Label + TEXT(": ... on step N + 1"), S.Steps, N + 1);
		}
		return true;
	}

	/** Dipping under the line's strength for a single step resets the snap timer. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASnapTimerResets, "Project.Fishing.Fight.QA.Snap.TimerResetsWhenTensionDrops", Flags)
	bool FLureFightQASnapTimerResets::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow Tuning = InstantTuning(*Data.Tuning(), 60);
		const int32 N = FMath::RoundToInt(Tuning.SnapGraceTime * 60.f);
		// Pull 8, rod power 8, drag 5, line 10: reeling targets 8 x ReelStrain + 8 x ReelLoad (over the line), letting it run targets 5 (under).
		const FLureGearStats G = MakeGear(8.f, 120.f, 5.f, 10.f, 100000.f, 1.f);
		if (!TestTrue(TEXT("fixture: reeling is over the line, letting it run is under"), FLureFight::TargetTension(8.f, true, G, Tuning) > 10.f && FLureFight::TargetTension(8.f, false, G, Tuning) < 10.f))
		{
			return false;
		}
		FLureFightState S = SteadyFight(Tuning, 8.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), G, 1000.f);
		for (int32 Round = 0; Round < 4; ++Round)
		{
			for (int32 Step = 0; Step < N - 1; ++Step)
			{
				FLureFight::Step(S, Input(true));
			}
			TestFalse(FString::Printf(TEXT("round %d: over for grace - 1 step: still on"), Round), S.IsOver());
			TestTrue(FString::Printf(TEXT("round %d: the snap timer is counting (%.4f s)"), Round, S.OverTime), S.OverTime > 0.f);
			FLureFight::Step(S, Input(false));
			TestEqual(FString::Printf(TEXT("round %d: one step under the strength resets the timer"), Round), S.OverTime, 0.f);
		}
		TestEqual(TEXT("4 rounds of almost-the-grace never snap"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::None));
		StepUntilOver(S, true, 10 * 60);
		TestEqual(TEXT("reeling on without a break snaps it"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		return true;
	}

	/** SnapGraceTime 0 (valid data: >= 0): the first step over the line's strength snaps it. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASnapZeroGrace, "Project.Fishing.Fight.QA.Snap.ZeroGraceSnapsOnFirstStepOver", Flags)
	bool FLureFightQASnapZeroGrace::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		FLureFishFightRow Tuning = InstantTuning(*Data.Tuning(), 60);
		Tuning.SnapGraceTime = 0.f;
		FString Problem;
		TestTrue(TEXT("SnapGraceTime 0 is valid data: ") + Problem, Tuning.Validate(Problem));
		FLureFightState S = SteadyFight(Tuning, 20.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), MakeGear(8.f, 120.f, 30.f, 10.f, 100000.f, 1.f), 1000.f);
		FLureFight::Step(S, Input(false));
		TestEqual(TEXT("grace 0: snaps on the first step over the strength"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		FLureFightState Under = SteadyFight(Tuning, 20.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), MakeGear(8.f, 120.f, 10.f, 10.f, 100000.f, 1.f), 1000.f);
		StepUntilOver(Under, false, 600);
		TestEqual(TEXT("grace 0: a tension AT the strength (drag = strength) never snaps"), OutcomeName(Under.Outcome), OutcomeName(ELureFightOutcome::None));
		return true;
	}

	// =================================================================================================================
	// Slack: "tension below SlackShare x BasePull longer than SlackGraceTime x HookSecurity" throws the hook
	// =================================================================================================================

	/** Slack for EXACTLY SlackGraceTime x HookSecurity keeps the fish on; one step more throws the hook (every DT_Gear hook + fixtures). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASlackExactlyAtGrace, "Project.Fishing.Fight.QA.Slack.ExactlyAtGraceHolds", Flags)
	bool FLureFightQASlackExactlyAtGrace::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& Shipped = *Data.Tuning();
		struct FCase { FString Name; int32 SimRate; float Grace; float Security; };
		TArray<FCase> Cases;
		for (const TPair<FName, uint8*>& Pair : Data.Gear->GetRowMap())
		{
			const FLureGearRow& Row = *reinterpret_cast<const FLureGearRow*>(Pair.Value);
			if (Row.Slot == ELureGearSlot::Hook)
			{
				Cases.Add({ Pair.Key.ToString(), Shipped.SimRate, Shipped.SlackGraceTime, Row.HookSecurity });
			}
		}
		Cases.Add({ TEXT("fixture"), 60, 0.5f, 1.f });
		Cases.Add({ TEXT("fixture"), 10, 0.5f, 1.f });
		Cases.Add({ TEXT("fixture"), 30, 1.f, 2.f });
		for (const FCase& Case : Cases)
		{
			FLureFishFightRow Tuning = InstantTuning(Shipped, Case.SimRate);
			Tuning.SlackGraceTime = Case.Grace;
			const FLureGearStats G = MakeGear(8.f, 120.f, 0.f, 100.f, 100000.f, Case.Security);
			const float Grace = FLureFight::SlackGrace(G, Tuning);
			const float StepsExact = Grace * static_cast<float>(Case.SimRate);
			const int32 N = FMath::RoundToInt(StepsExact);
			const FString Label = FString::Printf(TEXT("%s: SimRate %d, SlackGraceTime %.2f x HookSecurity %.2f = %.3f s"), *Case.Name, Case.SimRate, Case.Grace, Case.Security, Grace);
			TestNearlyEqual(Label + TEXT(": grace = SlackGraceTime x HookSecurity"), Grace, Case.Grace * Case.Security, 1.0e-5f);
			if (FMath::Abs(StepsExact - static_cast<float>(N)) > 1.0e-3f || N <= 0)
			{
				AddInfo(Label + TEXT(": not a whole number of steps, boundary check skipped"));
				continue;
			}
			// A never-tiring fish pulling 4 on a free spool (drag 0), not reeling: slack from the first step on.
			FLureFightState S = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), G, 1000.f);
			for (int32 Step = 0; Step < N && !S.IsOver(); ++Step)
			{
				FLureFight::Step(S, Input(false));
			}
			TestFalse(FString::Printf(TEXT("%s: slack for exactly the grace (%d steps, SlackTime %.9f) is not longer than it: the fish stays on (it %s after %d steps)"),
				*Label, N, S.SlackTime, S.IsOver() ? *OutcomeName(S.Outcome) : TEXT("stayed on"), S.Steps), S.IsOver());
			if (!S.IsOver())
			{
				FLureFight::Step(S, Input(false));
			}
			TestEqual(Label + TEXT(": one step later it throws the hook"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::ThrewHook));
			TestEqual(Label + TEXT(": ... on step N + 1"), S.Steps, N + 1);
		}
		return true;
	}

	/** One taut step resets the slack timer. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASlackTautStepResets, "Project.Fishing.Fight.QA.Slack.TautStepResetsTimer", Flags)
	bool FLureFightQASlackTautStepResets::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow Tuning = InstantTuning(*Data.Tuning(), 60);
		const FLureGearStats G = MakeGear(8.f, 120.f, 0.f, 100.f, 100000.f, 1.f);
		const int32 N = FMath::RoundToInt(FLureFight::SlackGrace(G, Tuning) * 60.f);
		FLureFightState S = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), G, 1000.f);
		for (int32 Round = 0; Round < 3; ++Round)
		{
			for (int32 Step = 0; Step < N - 1; ++Step)
			{
				FLureFight::Step(S, Input(false));
			}
			TestFalse(FString::Printf(TEXT("round %d: slack for grace - 1 step: still on"), Round), S.IsOver());
			FLureFight::Step(S, Input(true));
			TestEqual(FString::Printf(TEXT("round %d: one reeled (taut) step resets the slack timer"), Round), S.SlackTime, 0.f);
		}
		TestEqual(TEXT("still on after 3 near-misses"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::None));
		return true;
	}

	/** "Slack below SlackShare x BasePull": a tension exactly at the threshold is not slack; just under it is. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASlackAtThreshold, "Project.Fishing.Fight.QA.Slack.AtThresholdIsNotSlack", Flags)
	bool FLureFightQASlackAtThreshold::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow Tuning = InstantTuning(*Data.Tuning(), 60);
		const FLureFightFish Fish = MakeFightFish(4.f, 0.f, 1.0e9f);
		const float Threshold = FLureFight::SlackTension(Fish, Tuning);
		TestNearlyEqual(TEXT("slack threshold = SlackShare x BasePull"), Threshold, Tuning.SlackShare * 4.f, 1.0e-5f);
		// Not reeling with the drag set to the threshold: the tension sits exactly on it (pull 4 > drag).
		FLureFightState At = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), MakeGear(8.f, 120.f, Threshold, 100.f, 100000.f, 1.f), 1000.f);
		StepUntilOver(At, false, 20 * 60);
		TestEqual(TEXT("tension exactly at the slack threshold: not slack, the fish stays on for 20 s"), OutcomeName(At.Outcome), OutcomeName(ELureFightOutcome::None));
		TestEqual(TEXT("... the slack timer never ran"), At.SlackTime, 0.f);
		FLureFightState Under = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), MakeGear(8.f, 120.f, Threshold * 0.999f, 100.f, 100000.f, 1.f), 1000.f);
		StepUntilOver(Under, false, 20 * 60);
		TestEqual(TEXT("just under the threshold: slack, the fish throws the hook"), OutcomeName(Under.Outcome), OutcomeName(ELureFightOutcome::ThrewHook));
		return true;
	}

	// =================================================================================================================
	// Spool: "LineOut > SpoolLength -> Spooled (the fish took more line than the spool holds)"
	// =================================================================================================================

	/** A fish that runs and is let run takes the whole spool; one that out-pulls the rod twice over takes it even while you crank. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASpoolWholeSpool, "Project.Fishing.Fight.QA.Spool.FishTakesWholeSpool", Flags)
	bool FLureFightQASpoolWholeSpool::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		struct FCase { const TCHAR* Name; bool bReel; FLureGearStats Gear; };
		const FCase Cases[] = {
			// Pull 6 > drag 5: the drag gives line at the full 300 cm/s; a strong line and a secure hook so only the spool can end it.
			{ TEXT("let run"), false, MakeGear(8.f, 120.f, 5.f, 100.f, 3000.f, 100.f) },
			// Pull 6 = 3 x rod power 2: the fish takes line at full speed even while you reel.
			{ TEXT("reeling a fish that out-pulls the rod"), true, MakeGear(2.f, 120.f, 5.f, 100.f, 3000.f, 100.f) },
		};
		for (const FCase& Case : Cases)
		{
			FLureFightState S = SteadyFight(T, 6.f, MakeMove(TEXT("Run"), 1.f, 1.f, 1.f), Case.Gear, 1000.f, 300.f);
			bool bNeverOverSpoolWhileOn = true;
			float LastOn = S.LineOut;
			while (!S.IsOver() && S.Steps < 60 * 60)
			{
				FLureFight::Step(S, Input(Case.bReel));
				if (!S.IsOver())
				{
					bNeverOverSpoolWhileOn &= S.LineOut <= Case.Gear.SpoolLength;
					LastOn = S.LineOut;
				}
			}
			const FString Label = Case.Name;
			TestEqual(Label + TEXT(": the fish takes the whole spool"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Spooled));
			TestTrue(Label + TEXT(": the fight went on while the line out was within the spool"), bNeverOverSpoolWhileOn);
			TestTrue(FString::Printf(TEXT("%s: it ended on the first step past the spool (%.2f -> %.2f cm, spool %.0f)"), *Label, LastOn, S.LineOut, Case.Gear.SpoolLength),
				S.LineOut > Case.Gear.SpoolLength && LastOn <= Case.Gear.SpoolLength && S.LineOut - Case.Gear.SpoolLength <= 300.f / 60.f + 0.01f);
			TestNearlyEqual(Label + TEXT(": at the full swim speed (2000 cm at 300 cm/s)"), S.Elapsed, 2000.f / 300.f, 2.f * WorldDt);
		}
		return true;
	}

	/** Exactly the spool's length out is not "more than the spool holds"; a hook beyond the spool ends on the first step. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASpoolBoundary, "Project.Fishing.Fight.QA.Spool.BoundaryIsMoreThanTheSpool", Flags)
	bool FLureFightQASpoolBoundary::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow Tuning = InstantTuning(*Data.Tuning(), 60);
		const FLureGearStats G = MakeGear(8.f, 120.f, 5.f, 100.f, 3000.f, 100.f);
		FLureFightState At = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Still"), 1.f, 0.f, 0.f), G, 3000.f);
		StepUntilOver(At, false, 60);
		TestEqual(TEXT("exactly the spool's length out (still fish): the fight goes on"), OutcomeName(At.Outcome), OutcomeName(ELureFightOutcome::None));
		FLureFightState Past = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Still"), 1.f, 0.f, 0.f), G, 3000.5f);
		FLureFight::Step(Past, Input(false));
		TestEqual(TEXT("half a centimetre more: spooled on the first step"), OutcomeName(Past.Outcome), OutcomeName(ELureFightOutcome::Spooled));
		FLureFightState Beyond = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Still"), 1.f, 0.f, 0.f), G, 5000.f);
		FLureFight::Step(Beyond, Input(true));
		TestEqual(TEXT("hooked beyond the spool: spooled on the first step even while reeling"), OutcomeName(Beyond.Outcome), OutcomeName(ELureFightOutcome::Spooled));
		return true;
	}

	// =================================================================================================================
	// Outcomes: order and finality
	// =================================================================================================================

	/** "Four outcomes (first that applies each step)": Landed, then Spooled, then Snapped, then ThrewHook. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAOutcomeOrder, "Project.Fishing.Fight.QA.Outcome.FirstThatAppliesWins", Flags)
	bool FLureFightQAOutcomeOrder::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		FLureFishFightRow Tuning = InstantTuning(*Data.Tuning(), 60);
		Tuning.LandDistance = 150.f;
		const FLureFightMove Still = MakeMove(TEXT("Still"), 1.f, 0.f, 0.f);
		{
			// Inside the landing distance and past a (too short) spool at once.
			FLureFightState S = SteadyFight(Tuning, 4.f, Still, MakeGear(8.f, 120.f, 5.f, 100.f, 100.f, 100.f), 120.f);
			FLureFight::Step(S, Input(false));
			TestEqual(TEXT("landed beats spooled"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Landed));
		}
		{
			// Inside the landing distance while the snap timer is long past its grace.
			FLureFightState S = SteadyFight(Tuning, 20.f, Still, MakeGear(8.f, 120.f, 30.f, 10.f, 100000.f, 100.f), 100.f);
			S.OverTime = 10.f;
			FLureFight::Step(S, Input(false));
			TestEqual(TEXT("landed beats snapped"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Landed));
		}
		{
			FLureFightState S = SteadyFight(Tuning, 20.f, Still, MakeGear(8.f, 120.f, 30.f, 10.f, 3000.f, 100.f), 5000.f);
			S.OverTime = 10.f;
			FLureFight::Step(S, Input(false));
			TestEqual(TEXT("spooled beats snapped"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Spooled));
		}
		{
			// A fish with a base pull of 100 (slack below 35) on a 10 line, drag locked at 20: over the strength AND slack.
			FLureFightState S = SteadyFight(Tuning, 100.f, Still, MakeGear(8.f, 120.f, 20.f, 10.f, 100000.f, 1.f), 1000.f);
			S.OverTime = 10.f;
			S.SlackTime = 10.f;
			FLureFight::Step(S, Input(false));
			TestEqual(TEXT("snapped beats a thrown hook"), OutcomeName(S.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		}
		return true;
	}

	/** "Landed (fish within LandDistance)": exactly at LandDistance is landed, a hair beyond is not. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAOutcomeLandBoundary, "Project.Fishing.Fight.QA.Outcome.LandedAtExactlyLandDistance", Flags)
	bool FLureFightQAOutcomeLandBoundary::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow Tuning = InstantTuning(*Data.Tuning(), 60);
		const FLureGearStats G = MakeGear(8.f, 120.f, 5.f, 100.f, 100000.f, 100.f);
		FLureFightState At = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Still"), 1.f, 0.f, 0.f), G, Tuning.LandDistance);
		FLureFight::Step(At, Input(false));
		TestEqual(FString::Printf(TEXT("line out = LandDistance (%.0f cm): landed"), Tuning.LandDistance), OutcomeName(At.Outcome), OutcomeName(ELureFightOutcome::Landed));
		FLureFightState Beyond = SteadyFight(Tuning, 4.f, MakeMove(TEXT("Still"), 1.f, 0.f, 0.f), G, Tuning.LandDistance + 0.5f);
		StepUntilOver(Beyond, false, 60);
		TestEqual(TEXT("half a centimetre farther (still fish, not reeling): not landed"), OutcomeName(Beyond.Outcome), OutcomeName(ELureFightOutcome::None));
		return true;
	}

	/** "After an outcome it does nothing": Step and Advance leave a finished fight exactly as it ended. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAOutcomeFrozen, "Project.Fishing.Fight.QA.Outcome.FrozenAfterTheEnd", Flags)
	bool FLureFightQAOutcomeFrozen::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		struct FCase { const TCHAR* Name; float Pull; float Start; FLureGearStats Gear; bool bReel; };
		const FCase Cases[] = {
			{ TEXT("landed"), 1.f, 400.f, MakeGear(8.f, 120.f, 5.f, 100.f, 100000.f, 100.f), true },
			{ TEXT("snapped"), 20.f, 1000.f, MakeGear(8.f, 120.f, 30.f, 10.f, 100000.f, 100.f), true },
		};
		for (const FCase& Case : Cases)
		{
			FLureFightState S = SteadyFight(T, Case.Pull, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), Case.Gear, Case.Start);
			StepUntilOver(S, Case.bReel, 60 * 60);
			if (!TestTrue(FString::Printf(TEXT("%s: the fight ended"), Case.Name), S.IsOver()))
			{
				continue;
			}
			const FLureFightState Before = S;
			for (int32 Step = 0; Step < 10; ++Step)
			{
				TestEqual(FString::Printf(TEXT("%s: Step returns the outcome"), Case.Name), OutcomeName(FLureFight::Step(S, Input(!Case.bReel))), OutcomeName(Before.Outcome));
			}
			FLureFight::Advance(S, Input(true), 1.f);
			const bool bSame = S.Steps == Before.Steps && S.Elapsed == Before.Elapsed && S.LineOut == Before.LineOut && S.Tension == Before.Tension
				&& S.Stamina == Before.Stamina && S.OverTime == Before.OverTime && S.SlackTime == Before.SlackTime && S.MoveIndex == Before.MoveIndex
				&& S.Outcome == Before.Outcome && S.Accumulator == Before.Accumulator;
			TestTrue(FString::Printf(TEXT("%s: nothing moves after the end (steps %d -> %d)"), Case.Name, Before.Steps, S.Steps), bSame);
		}
		return true;
	}

	// =================================================================================================================
	// The whole step against an independent transcription of the documented formulas (FishFight.h steps 1-6)
	// =================================================================================================================

	namespace Oracle
	{
		struct FPre
		{
			float LineOut, Tension, Stamina, OverTime, SlackTime, MoveTimeLeft;
			int32 MoveIndex, Steps;
			bool bExhausted;
		};

		inline FPre Snapshot(const FLureFightState& S)
		{
			return { S.LineOut, S.Tension, S.Stamina, S.OverTime, S.SlackTime, S.MoveTimeLeft, S.MoveIndex, S.Steps, S.bExhausted };
		}

		inline double Clamp01(double X) { return FMath::Clamp(X, 0.0, 1.0); }

		inline bool Near(double A, double B) { return FMath::Abs(A - B) <= 1.0e-4 * FMath::Max(1.0, FMath::Abs(B)); }

		enum class EResult : uint8 { Match, Skipped, Mismatch };

		/** Checks one step (Pre -> State) against the spec. The move the step used is read from the pattern (a new move's pick is the RNG's business). */
		inline EResult Check(const FPre& Pre, const FLureFightState& State, bool bReel, FString& OutWhy)
		{
			const FLureFishFightRow& T = State.Tuning;
			const FLureGearStats& G = State.Gear;
			const FLureFightFish& F = State.Fish;
			const float Dt = 1.f / static_cast<float>(FMath::Clamp(T.SimRate, 10, 240));

			// 1. The move used this step: the same one unless its time ran out (then the newly picked one, if the fish did not tire out in this very step).
			const FLureFightMove* Move = nullptr;
			if (!Pre.bExhausted)
			{
				const float Left = Pre.MoveTimeLeft - Dt;
				if (Left <= 0.f)
				{
					if (State.bExhausted)
					{
						return EResult::Skipped; // the new pick was cleared by the exhaustion: not observable
					}
					Move = State.GetMove();
				}
				else
				{
					Move = State.Pattern.Moves.IsValidIndex(Pre.MoveIndex) ? &State.Pattern.Moves[Pre.MoveIndex] : nullptr;
				}
			}
			// 2. Fish.
			const double SF = T.TiredPull + (1.0 - T.TiredPull) * Clamp01(Pre.Stamina);
			const double Pull = Move ? F.BasePull * Move->Pull * SF : F.BasePull * T.TiredPull;
			const double Speed = Move ? F.BaseSpeed * Move->Speed * SF : 0.0;
			const double Va = Move ? Speed * Move->Away : 0.0;
			// 3. Line.
			double Gain = 0.0;
			double Taken = Va;
			double Target = 0.0;
			if (bReel)
			{
				Gain = G.ReelSpeed * Clamp01(1.0 - Pull / G.RodPower);
				if (Va > 0.0)
				{
					Taken = Va * Clamp01(Pull / G.RodPower - 1.0);
				}
				Target = Pull * T.ReelStrain + G.RodPower * T.ReelLoad;
			}
			else
			{
				if (Va > 0.0)
				{
					Taken = G.Drag > 0.f ? Va * Clamp01((Pull / G.Drag - T.DragHold) / (1.0 - T.DragHold)) : Va;
				}
				Target = FMath::Min(Pull, static_cast<double>(G.Drag));
			}
			const double LineOut = FMath::Max(0.0, Pre.LineOut + (Taken - Gain) * Dt);
			// 4. Tension.
			const double Tau = Target > Pre.Tension ? T.TensionRiseTime : T.TensionFallTime;
			const double Tension = Tau > 0.0 ? Pre.Tension + (Target - Pre.Tension) * (1.0 - FMath::Exp(-Dt / Tau)) : Target;
			// 5. Stamina.
			const double Slack = FMath::Max(0.01, static_cast<double>(T.SlackShare) * F.BasePull);
			const double Pool = F.StaminaPool;
			if (Near(Tension, Slack))
			{
				return EResult::Skipped;
			}
			const bool bSlack = Tension < Slack;
			const double Energy = Pre.Stamina * Pool - Tension * Dt + (bSlack ? Pool * T.StaminaRecovery * Dt : 0.0);
			const double Stamina = FMath::Clamp(Energy / Pool, 0.0, 1.0);
			if (!Pre.bExhausted && Near(Stamina, T.ExhaustedStamina))
			{
				return EResult::Skipped;
			}
			const bool bExhausted = Pre.bExhausted || Stamina <= T.ExhaustedStamina;
			// 6. Outcome.
			if (Near(LineOut, T.LandDistance) || Near(LineOut, G.SpoolLength) || Near(Tension, G.LineStrength))
			{
				return EResult::Skipped;
			}
			ELureFightOutcome Outcome = ELureFightOutcome::None;
			float Over = Pre.OverTime;
			float SlackTime = Pre.SlackTime;
			bool bTimers = true;
			// "Longer than the grace" counts whole steps (B1): a timer of N steps is over the grace only when N > Grace / Dt.
			auto LongerThan = [Dt](double Time, double Grace)
			{
				return FMath::RoundToDouble(Time / Dt) > FMath::Max(0.0, Grace) / Dt + 1.0e-4;
			};
			if (LineOut <= T.LandDistance)
			{
				Outcome = ELureFightOutcome::Landed;
				bTimers = false;
			}
			else if (G.SpoolLength > 0.f && LineOut > G.SpoolLength)
			{
				Outcome = ELureFightOutcome::Spooled;
				bTimers = false;
			}
			else
			{
				Over = Tension > G.LineStrength ? Pre.OverTime + Dt : 0.f;
				if (LongerThan(Over, T.SnapGraceTime))
				{
					Outcome = ELureFightOutcome::Snapped;
				}
				else
				{
					SlackTime = bSlack ? Pre.SlackTime + Dt : 0.f;
					if (LongerThan(SlackTime, static_cast<double>(T.SlackGraceTime) * G.HookSecurity))
					{
						Outcome = ELureFightOutcome::ThrewHook;
					}
				}
			}

			auto Off = [&OutWhy](const TCHAR* Field, double Expected, double Actual)
			{
				if (FMath::Abs(Expected - Actual) > 1.0e-4 * FMath::Max(1.0, FMath::Abs(Expected)))
				{
					OutWhy += FString::Printf(TEXT(" %s expected %.6f got %.6f;"), Field, Expected, Actual);
				}
			};
			Off(TEXT("Pull"), Pull, State.Pull);
			Off(TEXT("Speed"), Speed, State.Speed);
			Off(TEXT("LineOut"), LineOut, State.LineOut);
			Off(TEXT("Tension"), Tension, State.Tension);
			Off(TEXT("Stamina"), Stamina, State.Stamina);
			if (State.bExhausted != bExhausted)
			{
				OutWhy += FString::Printf(TEXT(" exhausted expected %d got %d;"), bExhausted, State.bExhausted);
			}
			if (State.Outcome != Outcome)
			{
				OutWhy += FString::Printf(TEXT(" outcome expected %s got %s;"), *OutcomeName(Outcome), *OutcomeName(State.Outcome));
			}
			if (bTimers && Outcome == ELureFightOutcome::None)
			{
				Off(TEXT("OverTime"), Over, State.OverTime);
				Off(TEXT("SlackTime"), SlackTime, State.SlackTime);
			}
			if (State.Steps != Pre.Steps + 1)
			{
				OutWhy += TEXT(" Steps did not advance by one;");
			}
			if (State.bExhausted && State.MoveIndex != INDEX_NONE)
			{
				OutWhy += TEXT(" an exhausted fish still has a move;");
			}
			if (Pre.bExhausted && State.Speed != 0.f)
			{
				OutWhy += TEXT(" an exhausted fish still swims;");
			}
			return OutWhy.IsEmpty() ? EResult::Match : EResult::Mismatch;
		}

		inline FLureFightPatternRow RandomPattern(FRandomStream& R)
		{
			FLureFightPatternRow Pattern;
			const int32 Count = 1 + R.RandHelper(5);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FLureFightMove Move;
				Move.Id = FName(*FString::Printf(TEXT("M%d"), Index));
				Move.Label = FText::FromName(Move.Id);
				Move.Weight = Index == 0 ? R.FRandRange(0.5f, 4.f) : R.FRandRange(0.f, 4.f);
				Move.AggressionWeight = R.FRandRange(0.f, 0.2f);
				Move.DurationMin = R.FRandRange(0.01f, 2.f);
				Move.DurationMax = Move.DurationMin + R.FRandRange(0.f, 2.f);
				Move.Pull = R.FRandRange(0.f, 2.5f);
				Move.Speed = R.FRandRange(0.f, 2.5f);
				Move.Away = R.FRandRange(-1.f, 1.f);
				Move.Side = R.FRandRange(-1.f, 1.f);
				Move.RandomSide = R.FRand() < 0.5f;
				Move.Down = R.FRandRange(-1.f, 1.f);
				Move.Rest = R.FRand() < 0.3f;
				Pattern.Moves.Add(Move);
			}
			Pattern.OpeningMove = R.FRand() < 0.5f ? Pattern.Moves[R.RandHelper(Count)].Id : NAME_None;
			return Pattern;
		}
	}

	/**
	 *  Every step of ~180 fights (random fish, gear, tuning within the validated ranges, shipped and random patterns, random
	 *  reel input, plus real rolled fish on the shipped data) matches an independent transcription of FishFight.h steps 2-6.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASimOracle, "Project.Fishing.Fight.QA.Sim.StepMatchesSpecFormulas", Flags)
	bool FLureFightQASimOracle::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const FLureFishFightRow& Shipped = *Data.Tuning();
		const FFishLevelScaling& Scaling = GetDefault<UFishSettings>()->LevelScaling;

		struct FFixture
		{
			FString Name;
			FLureFightFish Fish;
			FLureFightPatternRow Pattern;
			FLureGearStats Gear;
			FLureFishFightRow Tuning;
			int32 Seed = 0;
			float Start = 1000.f;
		};
		TArray<FFixture> Fixtures;

		// Real fish from the one roll pipeline on the shipped data.
		{
			FFishInstance Snapper;
			FFishInstance Bonefish;
			FFishInstance BigSnapper;
			if (!RollFish(*this, Fish, TEXT("CoralSnapper"), TEXT("Common"), (2.5f - 0.8f) / (7.f - 0.8f), 21, Snapper)
				|| !RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Uncommon"), 0.6f, 22, Bonefish)
				|| !RollFish(*this, Fish, TEXT("CoralSnapper"), TEXT("Rare"), 1.f, 23, BigSnapper, { TEXT("Feisty") }))
			{
				return false;
			}
			const FLureGearStats Kits[] = { Data.Starter(), Data.Resolve(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Squid")), Data.Resolve(TEXT("Rod_Starter"), TEXT("Line_Braid"), TEXT("Hook_Shrimp")) };
			const TPair<const FFishInstance*, FName> Real[] = { { &Snapper, TEXT("Dive") }, { &Bonefish, TEXT("Run") }, { &BigSnapper, TEXT("Dive") }, { &Bonefish, TEXT("Dart") } };
			int32 Seed = 100;
			for (const TPair<const FFishInstance*, FName>& Pair : Real)
			{
				for (const FLureGearStats& Kit : Kits)
				{
					const FLureFightPatternRow* Pattern = Data.Pattern(Pair.Value);
					if (!TestNotNull(TEXT("shipped pattern ") + Pair.Value.ToString(), Pattern))
					{
						return false;
					}
					Fixtures.Add({ FString::Printf(TEXT("%s/%s/%s"), *Pair.Key->SpeciesId.ToString(), *Pair.Value.ToString(), *Kit.LineId.ToString()),
						FLureFight::MakeFish(*Pair.Key, Shipped, 1, Scaling), *Pattern, Kit, Shipped, ++Seed, 1100.f });
				}
			}
		}
		// Random fixtures (fixed seed: deterministic).
		FRandomStream R(20260923);
		const FName ShippedPatterns[] = { TEXT("Run"), TEXT("Dive"), TEXT("Dart") };
		const int32 Rates[] = { 10, 30, 60, 90, 120, 240 };
		for (int32 Index = 0; Index < 160; ++Index)
		{
			FFixture X;
			X.Name = FString::Printf(TEXT("random %d"), Index);
			X.Tuning = Shipped;
			if (Index % 4 != 0)
			{
				X.Tuning.TiredPull = R.FRandRange(0.f, 1.f);
				X.Tuning.ExhaustedStamina = R.FRandRange(0.f, 0.3f);
				X.Tuning.StaminaRecovery = R.FRandRange(0.f, 0.2f);
				X.Tuning.ReelStrain = R.FRandRange(0.5f, 2.5f);
				X.Tuning.ReelLoad = R.FRandRange(0.f, 0.5f);
				X.Tuning.DragHold = R.FRandRange(0.f, 0.95f);
				X.Tuning.TensionRiseTime = R.FRand() < 0.15f ? 0.f : R.FRandRange(0.02f, 0.6f);
				X.Tuning.TensionFallTime = R.FRand() < 0.15f ? 0.f : R.FRandRange(0.02f, 0.6f);
				X.Tuning.SnapGraceTime = R.FRandRange(0.f, 1.5f);
				X.Tuning.SlackShare = R.FRandRange(0.f, 0.8f);
				X.Tuning.SlackGraceTime = R.FRandRange(0.2f, 4.f);
				X.Tuning.LandDistance = R.FRandRange(50.f, 300.f);
				X.Tuning.SimRate = Rates[R.RandHelper(static_cast<int32>(UE_ARRAY_COUNT(Rates)))];
			}
			X.Gear = MakeGear(R.FRandRange(1.f, 30.f), R.FRandRange(0.f, 300.f), R.FRand() < 0.1f ? 0.f : R.FRandRange(0.5f, 30.f), R.FRandRange(2.f, 40.f),
				R.FRandRange(800.f, 8000.f), R.FRandRange(0.2f, 3.f));
			X.Fish = MakeFightFish(R.FRandRange(0.05f, 20.f), R.FRandRange(0.f, 300.f), R.FRandRange(0.5f, 200.f), R.FRandRange(0.f, 60.f), R.FRandRange(0.25f, 4.f));
			const int32 PatternPick = R.RandHelper(5);
			if (PatternPick < 3)
			{
				X.Pattern = *Data.Pattern(ShippedPatterns[PatternPick]);
			}
			else if (PatternPick == 3)
			{
				X.Pattern = FLureFightPatternRow::GetFallbackPattern();
			}
			else
			{
				X.Pattern = Oracle::RandomPattern(R);
			}
			X.Seed = static_cast<int32>(R.GetUnsignedInt());
			X.Start = R.FRand() < 0.05f ? R.FRandRange(0.f, X.Tuning.LandDistance) : R.FRandRange(X.Tuning.LandDistance + 10.f, 3000.f);
			Fixtures.Add(MoveTemp(X));
		}

		int32 Checked = 0;
		int32 Skipped = 0;
		int32 Failed = 0;
		TMap<ELureFightOutcome, int32> Outcomes;
		FRandomStream InputStream(4242);
		for (const FFixture& X : Fixtures)
		{
			FLureFightState S;
			FLureFight::Begin(S, X.Fish, X.Pattern, TEXT("QA"), X.Gear, X.Tuning, X.Seed, X.Start);
			bool bReel = InputStream.FRand() < 0.5f;
			while (!S.IsOver() && S.Steps < 2400)
			{
				if (InputStream.FRand() < 1.f / 30.f)
				{
					bReel = !bReel;
				}
				const Oracle::FPre Pre = Oracle::Snapshot(S);
				FLureFight::Step(S, Input(bReel));
				FString Why;
				const Oracle::EResult Result = Oracle::Check(Pre, S, bReel, Why);
				if (Result == Oracle::EResult::Skipped)
				{
					++Skipped;
					continue;
				}
				++Checked;
				if (Result == Oracle::EResult::Mismatch)
				{
					++Failed;
					if (Failed <= 10)
					{
						AddError(FString::Printf(TEXT("%s, step %d (reeling %d, move %s):%s"), *X.Name, S.Steps, bReel, *S.GetMoveId().ToString(), *Why));
					}
					break; // one report per fixture
				}
			}
			Outcomes.FindOrAdd(S.Outcome)++;
		}
		AddInfo(FString::Printf(TEXT("%d fixtures, %d steps checked, %d skipped at a threshold, outcomes: landed %d, snapped %d, spooled %d, threw hook %d, still on %d"),
			Fixtures.Num(), Checked, Skipped, Outcomes.FindRef(ELureFightOutcome::Landed), Outcomes.FindRef(ELureFightOutcome::Snapped), Outcomes.FindRef(ELureFightOutcome::Spooled),
			Outcomes.FindRef(ELureFightOutcome::ThrewHook), Outcomes.FindRef(ELureFightOutcome::None)));
		TestEqual(TEXT("fixtures whose steps don't match the documented formulas"), Failed, 0);
		TestTrue(FString::Printf(TEXT("the check covers many steps (%d)"), Checked), Checked > 50000);
		TestTrue(FString::Printf(TEXT("threshold ties are rare (%d of %d)"), Skipped, Checked + Skipped), Skipped * 20 < Checked + Skipped);
		for (const ELureFightOutcome Outcome : { ELureFightOutcome::Landed, ELureFightOutcome::Snapped, ELureFightOutcome::Spooled, ELureFightOutcome::ThrewHook })
		{
			TestTrue(TEXT("the fixtures reach every outcome: ") + OutcomeName(Outcome), Outcomes.FindRef(Outcome) > 0);
		}
		return true;
	}

	// =================================================================================================================
	// Moves: pick weights, durations, exhaustion
	// =================================================================================================================

	/** "weight_i = max(0, Weight_i + Aggression x AggressionWeight_i)": pick shares over a uniform grid of draws. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASimMovePick, "Project.Fishing.Fight.QA.Sim.MovePickFollowsWeights", Flags)
	bool FLureFightQASimMovePick::RunTest(const FString& Parameters)
	{
		FLureFightPatternRow Pattern = MakePattern({
			MakeMove(TEXT("A"), 1.f, 1.f, 1.f, 1.f, false, 3.f, 0.f),
			MakeMove(TEXT("B"), 1.f, 1.f, 1.f, 1.f, false, 0.f, 2.f),
			MakeMove(TEXT("C"), 1.f, 1.f, 1.f, 1.f, false, 1.5f, 0.f),
			MakeMove(TEXT("D"), 1.f, 1.f, 1.f, 1.f, false, 0.5f, 0.1f) }, NAME_None);
		constexpr int32 Grid = 20000;
		for (const float Aggression : { 0.f, 1.f, 7.5f })
		{
			TArray<double> Weights;
			double Total = 0.0;
			for (const FLureFightMove& Move : Pattern.Moves)
			{
				const double W = FMath::Max(0.0, static_cast<double>(Move.Weight) + Aggression * Move.AggressionWeight);
				TestNearlyEqual(FString::Printf(TEXT("aggression %.1f: MoveWeight(%s)"), Aggression, *Move.Id.ToString()), FLureFight::MoveWeight(Move, Aggression), static_cast<float>(W), 1.0e-5f);
				Weights.Add(W);
				Total += W;
			}
			TArray<int32> Counts;
			Counts.SetNumZeroed(Pattern.Moves.Num());
			for (int32 K = 0; K < Grid; ++K)
			{
				const int32 Index = FLureFight::PickMove(Pattern, Aggression, (static_cast<float>(K) + 0.5f) / Grid);
				if (TestTrue(TEXT("a move is picked"), Pattern.Moves.IsValidIndex(Index)))
				{
					++Counts[Index];
				}
			}
			for (int32 Index = 0; Index < Pattern.Moves.Num(); ++Index)
			{
				const double Share = static_cast<double>(Counts[Index]) / Grid;
				const double Expected = Weights[Index] / Total;
				TestTrue(FString::Printf(TEXT("aggression %.1f: %s picked %.4f of the time, weight share %.4f"), Aggression, *Pattern.Moves[Index].Id.ToString(), Share, Expected),
					FMath::Abs(Share - Expected) <= 1.0e-3);
				if (Weights[Index] == 0.0)
				{
					TestEqual(FString::Printf(TEXT("aggression %.1f: a zero-weight move is never picked"), Aggression), Counts[Index], 0);
				}
			}
		}
		FLureFightPatternRow Dead = Pattern;
		for (FLureFightMove& Move : Dead.Moves)
		{
			Move.Weight = 0.f;
			Move.AggressionWeight = 0.f;
		}
		TestEqual(TEXT("no weight anywhere: no move"), FLureFight::PickMove(Dead, 5.f, 0.5f), static_cast<int32>(INDEX_NONE));
		return true;
	}

	/** Durations are within [DurationMin, DurationMax] (rest moves x RestScale), both ends are reached, and an exhausted fish never moves again. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASimDurations, "Project.Fishing.Fight.QA.Sim.MoveDurationsAndExhaustion", Flags)
	bool FLureFightQASimDurations::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const float Dt = FLureFight::StepSeconds(T);
		FLureFightMove Dash = MakeMove(TEXT("Dash"), 1.f, 0.5f, 0.f);
		Dash.DurationMin = 0.2f;
		Dash.DurationMax = 0.6f;
		FLureFightMove Pause = MakeMove(TEXT("Pause"), 0.5f, 0.f, 0.f, 1.f, true);
		Pause.DurationMin = 1.f;
		Pause.DurationMax = 2.f;
		const FLureFightMove Tiny = MakeMove(TEXT("Tiny"), 1.f, 0.f, 0.f, 0.001f);
		const FLureFightPatternRow Pattern = MakePattern({ Dash, Pause, Tiny }, TEXT("Pause"));
		const float RestScale = 0.5f;
		FLureFightState S;
		FLureFight::Begin(S, MakeFightFish(2.f, 100.f, 1.0e9f, 0.f, RestScale), Pattern, TEXT("QA"), MakeGear(8.f, 120.f, 5.f, 1.0e9f, 1.0e12f, 1.0e6f), T, 77, 1.0e6f);
		TestEqual(TEXT("the fight opens with the pattern's OpeningMove"), S.GetMoveId(), FName(TEXT("Pause")));
		TestTrue(FString::Printf(TEXT("the opening rest lasts [1, 2] x RestScale 0.5 (%.3f s)"), S.MoveTimeLeft), S.MoveTimeLeft >= 0.5f - 1.0e-4f && S.MoveTimeLeft <= 1.f + 1.0e-4f);
		TMap<FName, TArray<float>> Durations;
		for (int32 Step = 0; Step < 60000 && !S.IsOver(); ++Step)
		{
			const float Before = S.MoveTimeLeft;
			FLureFight::Step(S, Input(false));
			if (S.MoveTimeLeft > Before - Dt + 1.0e-6f)
			{
				Durations.FindOrAdd(S.GetMoveId()).Add(S.MoveTimeLeft);
			}
		}
		TestFalse(TEXT("the endless fixture never ended"), S.IsOver());
		struct FRange { FName Id; float Lo; float Hi; };
		const FRange Ranges[] = { { TEXT("Dash"), 0.2f, 0.6f }, { TEXT("Pause"), 0.5f, 1.f }, { TEXT("Tiny"), Dt, Dt } };
		for (const FRange& Range : Ranges)
		{
			const TArray<float>* List = Durations.Find(Range.Id);
			if (!TestTrue(FString::Printf(TEXT("%s was picked often (%d)"), *Range.Id.ToString(), List ? List->Num() : 0), List && List->Num() > 200))
			{
				continue;
			}
			float Lo = TNumericLimits<float>::Max();
			float Hi = 0.f;
			for (const float D : *List)
			{
				Lo = FMath::Min(Lo, D);
				Hi = FMath::Max(Hi, D);
			}
			TestTrue(FString::Printf(TEXT("%s: every duration within [%.3f, %.3f] (seen %.4f..%.4f)"), *Range.Id.ToString(), Range.Lo, Range.Hi, Lo, Hi), Lo >= Range.Lo - 1.0e-4f && Hi <= Range.Hi + 1.0e-4f);
			const float Span = Range.Hi - Range.Lo;
			TestTrue(FString::Printf(TEXT("%s: the durations cover the range (seen %.4f..%.4f)"), *Range.Id.ToString(), Lo, Hi), Lo <= Range.Lo + 0.05f * Span + 1.0e-4f && Hi >= Range.Hi - 0.05f * Span - 1.0e-4f);
		}

		// A fish tires out while reeled, then stays exhausted even as its stamina recovers on a slack line (pool 50: the recovery,
		// 50 x StaminaRecovery per second, outweighs the drain of its tired pull on the loose line).
		FLureFightState E;
		FLureFight::Begin(E, MakeFightFish(5.f, 100.f, 50.f), FLureFightPatternRow::GetFallbackPattern(), NAME_None, MakeGear(8.f, 120.f, 5.f, 1.0e9f, 1.0e12f, 1.0e6f), T, 3, 1.0e6f);
		for (int32 Step = 0; Step < 60 * 60 && !E.bExhausted; ++Step)
		{
			FLureFight::Step(E, Input(true));
		}
		if (!TestTrue(TEXT("the reeled fish tires out"), E.bExhausted))
		{
			return false;
		}
		bool bStill = true;
		float MaxStamina = E.Stamina;
		for (int32 Step = 0; Step < 60 * 60 && !E.IsOver(); ++Step)
		{
			FLureFight::Step(E, Input(false));
			bStill &= E.bExhausted && E.MoveIndex == INDEX_NONE && E.Speed == 0.f && FMath::IsNearlyEqual(E.Pull, 5.f * T.TiredPull, 1.0e-4f);
			MaxStamina = FMath::Max(MaxStamina, E.Stamina);
		}
		TestTrue(FString::Printf(TEXT("exhausted for good: no move, no swim, pull = BasePull x TiredPull (stamina recovered to %.2f)"), MaxStamina), bStill);
		TestTrue(TEXT("... although the stamina did recover above the exhausted level"), MaxStamina > T.ExhaustedStamina);
		return true;
	}

	// =================================================================================================================
	// Fixed step: frame-rate independence, hitches, determinism
	// =================================================================================================================

	/** "Server-authoritative, deterministic ... fixed-step": the same fight advanced at 30, 60, 144 fps or random frame times is identical. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASimFrameRate, "Project.Fishing.Fight.QA.Sim.FrameRateIndependent", Flags)
	bool FLureFightQASimFrameRate::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Snapper;
		if (!RollFish(*this, Fish, TEXT("CoralSnapper"), TEXT("Common"), 0.4f, 31, Snapper))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureFightFish F = FLureFight::MakeFish(Snapper, T, 1, GetDefault<UFishSettings>()->LevelScaling);
		const FLureGearStats Braid = Data.Resolve(TEXT("Rod_Starter"), TEXT("Line_Braid"), TEXT("Hook_Shrimp"));
		for (const bool bReel : { true, false })
		{
			FLureFightState Reference;
			FLureFight::Begin(Reference, F, *Data.Pattern(TEXT("Dive")), TEXT("Dive"), Braid, T, FLureFight::FightSeed(Snapper.Seed), 1100.f);
			const FLureFightState Fresh = Reference;
			StepUntilOver(Reference, bReel, 180 * 60);
			if (!TestTrue(FString::Printf(TEXT("reeling %d: the reference fight ends (%s)"), bReel, *OutcomeName(Reference.Outcome)), Reference.IsOver()))
			{
				continue;
			}
			FRandomStream Frames(77);
			for (int32 Mode = 0; Mode < 4; ++Mode)
			{
				FLureFightState S = Fresh;
				int32 FrameCount = 0;
				while (!S.IsOver() && FrameCount < 200000)
				{
					const float FrameDt = Mode == 0 ? 1.f / 30.f : Mode == 1 ? 1.f / 144.f : Mode == 2 ? 1.f / 60.f : Frames.FRandRange(0.001f, 0.2f);
					FLureFight::Advance(S, Input(bReel), FrameDt);
					++FrameCount;
				}
				const TCHAR* ModeName = Mode == 0 ? TEXT("30 fps") : Mode == 1 ? TEXT("144 fps") : Mode == 2 ? TEXT("60 fps") : TEXT("random frame times");
				const bool bSame = S.Outcome == Reference.Outcome && S.Steps == Reference.Steps && S.Tension == Reference.Tension && S.LineOut == Reference.LineOut
					&& S.Stamina == Reference.Stamina && S.OverTime == Reference.OverTime && S.SlackTime == Reference.SlackTime && S.MoveIndex == Reference.MoveIndex;
				TestTrue(FString::Printf(TEXT("reeling %d, %s: the same fight (%s at step %d vs %s at step %d)"), bReel, ModeName, *OutcomeName(S.Outcome), S.Steps,
					*OutcomeName(Reference.Outcome), Reference.Steps), bSame);
			}
		}
		return true;
	}

	/** "At most 30 steps per frame after a hitch" (the rest is dropped); bad frame times simulate nothing; a remainder carries over. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASimHitch, "Project.Fishing.Fight.QA.Sim.HitchIsBounded", Flags)
	bool FLureFightQASimHitch::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const float Dt = FLureFight::StepSeconds(T);
		TestNearlyEqual(TEXT("one step = 1 / SimRate"), Dt, 1.f / static_cast<float>(T.SimRate), 1.0e-7f);
		const FLureGearStats Endless = MakeGear(8.f, 120.f, 5.f, 1.0e9f, 1.0e12f, 1.0e6f);
		auto Fresh = [&]() { return SteadyFight(T, 3.f, MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f), Endless, 1.0e6f); };

		FLureFightState S = Fresh();
		FLureFight::Advance(S, Input(true), 10.f);
		TestEqual(TEXT("a 10 s hitch simulates at most MaxStepsPerAdvance steps"), S.Steps, FLureFight::MaxStepsPerAdvance);
		TestEqual(TEXT("... and drops the backlog"), S.Accumulator, 0.f);
		FLureFight::Advance(S, Input(true), 0.f);
		TestEqual(TEXT("... so the next frame does not catch up"), S.Steps, FLureFight::MaxStepsPerAdvance);

		for (const float Bad : { std::numeric_limits<float>::quiet_NaN(), -1.f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() })
		{
			FLureFightState B = Fresh();
			FLureFight::Advance(B, Input(true), Bad);
			TestTrue(FString::Printf(TEXT("frame time %f: at most MaxStepsPerAdvance steps, finite state"), Bad), B.Steps <= FLureFight::MaxStepsPerAdvance && AllFinite(B) && FMath::IsFinite(B.Accumulator));
			if (!(Bad > 0.f))
			{
				TestEqual(FString::Printf(TEXT("frame time %f: nothing simulated"), Bad), B.Steps, 0);
			}
		}
		FLureFightState H = Fresh();
		FLureFight::Advance(H, Input(true), 0.5f * Dt);
		TestEqual(TEXT("half a step: nothing yet"), H.Steps, 0);
		FLureFight::Advance(H, Input(true), 0.5f * Dt);
		TestEqual(TEXT("two halves: one step (the remainder carries over)"), H.Steps, 1);
		FLureFightState Exact = Fresh();
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			FLureFight::Advance(Exact, Input(true), Dt);
		}
		TestEqual(TEXT("600 frames of exactly one step: 600 steps (no drift)"), Exact.Steps, 600);
		return true;
	}

	/** The fight only uses its own RNG (seeded by FightSeed of the fish record): the global RNG changes nothing. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQASimGlobalRandom, "Project.Fishing.Fight.QA.Sim.IgnoresGlobalRandom", Flags)
	bool FLureFightQASimGlobalRandom::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureFightFish F = MakeFightFish(4.f, 150.f, 60.f, 20.f, 0.8f);
		const FLureGearStats Endless = MakeGear(8.f, 120.f, 5.f, 1.0e9f, 1.0e12f, 1.0e6f);
		auto Trace = [&](int32 GlobalSeed)
		{
			FMath::RandInit(GlobalSeed);
			FMath::SRandInit(GlobalSeed);
			FLureFightState S;
			FLureFight::Begin(S, F, *Data.Pattern(TEXT("Dart")), TEXT("Dart"), Endless, T, FLureFight::FightSeed(555), 1.0e6f);
			TArray<float> Out;
			for (int32 Step = 0; Step < 1200; ++Step)
			{
				(void)FMath::Rand();
				(void)FMath::FRand();
				FLureFight::Step(S, Input((Step / 45) % 2 == 0));
				Out.Add(S.Tension);
				Out.Add(S.LineOut);
				Out.Add(static_cast<float>(S.MoveIndex));
				Out.Add(S.SideDeg);
			}
			return Out;
		};
		TestTrue(TEXT("global RNG seeded 1 vs 98765: the same fight"), Trace(1) == Trace(98765));
		FLureFightState S;
		FLureFight::Begin(S, F, *Data.Pattern(TEXT("Dart")), TEXT("Dart"), Endless, T, FLureFight::FightSeed(555), 1.0e6f);
		TestEqual(TEXT("the fight's RNG is seeded with the seed given to Begin"), S.Rng.GetInitialSeed(), FLureFight::FightSeed(555));
		TSet<int32> Seeds;
		for (int32 FishSeed = 0; FishSeed < 1000; ++FishSeed)
		{
			Seeds.Add(FLureFight::FightSeed(FishSeed));
		}
		TestTrue(FString::Printf(TEXT("different fish records get different fights (%d distinct seeds of 1000)"), Seeds.Num()), Seeds.Num() >= 999);
		return true;
	}

	// =================================================================================================================
	// Acceptance, one gear stat at a time: rod power, line strength and hook security each change the outcome
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAGearEachStat, "Project.Fishing.Fight.QA.Gear.EachStatChangesTheOutcome", Flags)
	bool FLureFightQAGearEachStat::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureGearStats Starter = Data.Starter();

		// Line strength only: the reference Coral Snapper (Common, 2.5 kg, level 3 vs player 1), Dive, starter rod and hook, hold reel.
		{
			FFishInstance Snapper;
			if (!RollFish(*this, Fish, TEXT("CoralSnapper"), TEXT("Common"), (2.5f - 0.8f) / (7.f - 0.8f), 21, Snapper))
			{
				return false;
			}
			const FLureFightFish F = FLureFight::MakeFish(Snapper, T, 1, GetDefault<UFishSettings>()->LevelScaling);
			const FLureGearStats Mono = Data.Resolve(Starter.RodId, TEXT("Line_Mono"), Starter.HookId);
			const FLureGearStats Braid = Data.Resolve(Starter.RodId, TEXT("Line_Braid"), Starter.HookId);
			TestTrue(TEXT("fixture: only the line differs"), Mono.RodPower == Braid.RodPower && Mono.Drag == Braid.Drag && Mono.HookSecurity == Braid.HookSecurity && Mono.LineStrength < Braid.LineStrength);
			for (int32 Seed = 1; Seed <= 6; ++Seed)
			{
				FLureFightState Weak;
				FLureFightState Strong;
				FLureFight::Begin(Weak, F, *Data.Pattern(TEXT("Dive")), TEXT("Dive"), Mono, T, Seed, 1100.f);
				FLureFight::Begin(Strong, F, *Data.Pattern(TEXT("Dive")), TEXT("Dive"), Braid, T, Seed, 1100.f);
				RunPlayer(Weak, EPlayer::Hold);
				RunPlayer(Strong, EPlayer::Hold);
				TestEqual(FString::Printf(TEXT("line only, seed %d: Line_Mono snaps"), Seed), OutcomeName(Weak.Outcome), OutcomeName(ELureFightOutcome::Snapped));
				TestEqual(FString::Printf(TEXT("line only, seed %d: Line_Braid lands it (%.1f s)"), Seed, Strong.Elapsed), OutcomeName(Strong.Outcome), OutcomeName(ELureFightOutcome::Landed));
			}
		}

		// Rod power only: a never-tiring fish pulling 9 swims away; strong line, long-ish spool; hold reel.
		{
			const FLureFightFish F = MakeFightFish(9.f, 200.f, 1.0e9f);
			const FLureFightPatternRow Pattern = MakePattern({ MakeMove(TEXT("Away"), 1.f, 1.f, 1.f) }, TEXT("Away"));
			FLureGearStats Weak = Starter;
			Weak.LineStrength = 40.f;
			Weak.SpoolLength = 1600.f;
			FLureGearStats Strong = Weak;
			Strong.RodPower = 2.f * Weak.RodPower;
			FLureFightState A;
			FLureFightState B;
			FLureFight::Begin(A, F, Pattern, TEXT("QA"), Weak, T, 1, 1000.f);
			FLureFight::Begin(B, F, Pattern, TEXT("QA"), Strong, T, 1, 1000.f);
			RunPlayer(A, EPlayer::Hold, 120.f);
			RunPlayer(B, EPlayer::Hold, 120.f);
			TestEqual(FString::Printf(TEXT("rod power only: power %.0f can't reel in a fish pulling 9, it takes the spool"), Weak.RodPower), OutcomeName(A.Outcome), OutcomeName(ELureFightOutcome::Spooled));
			TestEqual(FString::Printf(TEXT("rod power only: power %.0f lands it (%.1f s)"), Strong.RodPower, B.Elapsed), OutcomeName(B.Outcome), OutcomeName(ELureFightOutcome::Landed));
		}

		// Hook security only: a fish that pauses (no pull) for 3 s right after the hook, then swims; the player lets it be, reels from 3.5 s.
		{
			const FLureFightFish F = MakeFightFish(2.f, 60.f, 1.0e9f);
			FLureFightMove Pause = MakeMove(TEXT("Pause"), 0.f, 0.f, 0.f, 3.f);
			const FLureFightPatternRow Pattern = MakePattern({ Pause, MakeMove(TEXT("Swim"), 1.f, 1.f, 0.5f, 1000.f, false, 1.f) }, TEXT("Pause"));
			const FLureGearStats Shrimp = Data.Resolve(Starter.RodId, Starter.LineId, TEXT("Hook_Shrimp"));
			const FLureGearStats Squid = Data.Resolve(Starter.RodId, Starter.LineId, TEXT("Hook_Squid"));
			TestTrue(TEXT("fixture: the two hooks' slack grace brackets the 3 s pause"), FLureFight::SlackGrace(Shrimp, T) < 3.f && FLureFight::SlackGrace(Squid, T) > 3.2f);
			FLureFightPatternRow OnlyPauseFirst = Pattern;
			OnlyPauseFirst.Moves[0].Weight = 0.f; // the pause only opens the fight
			for (const FLureGearStats* Kit : { &Shrimp, &Squid })
			{
				FLureFightState S;
				FLureFight::Begin(S, F, OnlyPauseFirst, TEXT("QA"), *Kit, T, 1, 1000.f);
				while (!S.IsOver() && S.Elapsed < 120.f)
				{
					FLureFight::Step(S, Input(S.Elapsed >= 3.5f));
				}
				const bool bSecure = Kit == &Squid;
				TestEqual(FString::Printf(TEXT("hook security only: %s (%.1f) -> %s after %.1f s"), *Kit->HookId.ToString(), Kit->HookSecurity, *OutcomeName(S.Outcome), S.Elapsed),
					OutcomeName(S.Outcome), OutcomeName(bSecure ? ELureFightOutcome::Landed : ELureFightOutcome::ThrewHook));
			}
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

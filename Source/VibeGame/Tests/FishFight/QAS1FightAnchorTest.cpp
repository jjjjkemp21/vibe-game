// QA-A3a (qa-engineer, 2026-09-24): independent A3 gate tests for T-045 "the hooked fish stays put while its angler walks".
// Derived from docs/specs/reel-fight-rules.md "The fish stays put (T-045)": LineOut is always the real horizontal distance
// (walking toward the fish shortens it, no slack stored); walking to within LandDistance lands it; walking away past the
// spool spools it; the swing limit holds only the fish's own swing (walking round leaves it past the limit, never pulled
// back). Plus a failure case: a non-finite player position is ignored. Pure sim (no world), fixed seeds, fixed dt.
// Project.Fishing.Fight.QA.S1.*   Everything lives in namespace LureQAS1FightAnchor (unity builds: no file-scope using).

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace LureQAS1FightAnchor
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	inline FLureGearStats Kit(const LureFightQA::FFightTables& Data)
	{
		FLureGearStats K = Data.Starter();
		FLureGear::ApplyDragLineCap(K, Data.Tuning()->DragLineCap);
		return K;
	}

	/** A fish that holds still on the line (pulls, never swims): only the player can change the distance. */
	inline FLureFightPatternRow HoldingFish()
	{
		return LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f));
	}

	inline FLureFightState MakeFight(const LureFightQA::FFightTables& Data, int32 Seed, const FVector2D& Player, const FVector2D& Fish)
	{
		FLureFightState State;
		FLureFight::Begin(State, LureFightQA::MakeFightFish(2.5f, 60.f, 60.f), HoldingFish(), TEXT("QAS1Anchor"), Kit(Data), *Data.Tuning(), Seed,
			static_cast<float>(FVector2D::Distance(Player, Fish)));
		FLureFight::PlaceFish(State, Player, Fish);
		return State;
	}

	/** Everything a step decides, bit for bit (used to prove an ignored input changed nothing). */
	inline bool SameState(const FLureFightState& A, const FLureFightState& B)
	{
		return A.Tension == B.Tension && A.Stamina == B.Stamina && A.Pull == B.Pull && A.LineOut == B.LineOut && A.SideDeg == B.SideDeg
			&& A.Anchor == B.Anchor && A.Outcome == B.Outcome && A.Steps == B.Steps && FLureFight::FishLocation(A) == FLureFight::FishLocation(B);
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** Walking toward the fish (no reel) shortens LineOut to the real distance each step; the fish stays put; no slack is stored. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1WalkToward, "Project.Fishing.Fight.QA.S1.WalkTowardShortensTheLine", Flags)
	bool FQAS1WalkToward::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FVector2D Start(0.0, 0.0);
		const FVector2D Fish(1200.0, 300.0);
		const FVector2D Dir = (Fish - Start).GetSafeNormal();
		const double StopAt = Data.Tuning()->LandDistance + 50.0; // stay clear of the landing rule
		FLureFightState State = MakeFight(Data, 31, Start, Fish);
		const double StartDistance = FVector2D::Distance(Start, Fish);
		double MaxLineErr = 0.0;
		double MaxDrift = 0.0;
		bool bNeverGrew = true;
		float Prev = State.LineOut;
		const int32 Steps = 360; // 6 s at 60 Hz
		for (int32 Step = 1; Step <= Steps && !State.IsOver(); ++Step)
		{
			const double Walked = (StartDistance - StopAt) * static_cast<double>(Step) / Steps;
			const FVector2D Now = Start + Dir * Walked;
			FLureFight::MovePlayer(State, Now);
			FLureFight::Step(State, LureRodQA::In(false));
			MaxLineErr = FMath::Max(MaxLineErr, FMath::Abs(static_cast<double>(State.LineOut) - FVector2D::Distance(Now, Fish)));
			MaxDrift = FMath::Max(MaxDrift, FVector2D::Distance(FLureFight::FishLocation(State), Fish));
			bNeverGrew &= State.LineOut <= Prev + 1.0e-3f;
			Prev = State.LineOut;
		}
		AddInfo(FString::Printf(TEXT("walked %.0f cm toward the fish; LineOut %.1f cm at the end; drift %.4f cm; line error %.4f cm"),
			StartDistance - StopAt, State.LineOut, MaxDrift, MaxLineErr));
		TestFalse(TEXT("the fight is still on (stopped short of LandDistance)"), State.IsOver());
		TestTrue(FString::Printf(TEXT("LineOut is the real distance every step (error %.4f cm)"), MaxLineErr), MaxLineErr < 0.01);
		TestTrue(FString::Printf(TEXT("the fish stays put (drift %.4f cm)"), MaxDrift), MaxDrift < 0.01);
		TestTrue(TEXT("walking toward the fish never lengthened the line"), bNeverGrew);
		TestTrue(TEXT("LineOut ended at the stop distance"), FMath::IsNearlyEqual(static_cast<double>(State.LineOut), StopAt, 0.01));
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** Boundary: walking to just outside LandDistance keeps the fight on; just inside lands it (no reel pressed). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1WalkInLands, "Project.Fishing.Fight.QA.S1.WalkInToLandDistanceLands", Flags)
	bool FQAS1WalkInLands::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const double Land = Data.Tuning()->LandDistance;
		const FVector2D Fish(1000.0, 0.0);
		FLureFightState State = MakeFight(Data, 32, FVector2D::ZeroVector, Fish);
		FLureFight::Step(State, LureRodQA::In(false));
		FLureFight::MovePlayer(State, Fish - FVector2D(Land + 1.0, 0.0));
		FLureFight::Step(State, LureRodQA::In(false));
		TestEqual(TEXT("1 cm outside LandDistance: not landed"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::None));
		FLureFight::MovePlayer(State, Fish - FVector2D(Land - 1.0, 0.0));
		FLureFight::Step(State, LureRodQA::In(false));
		TestEqual(TEXT("1 cm inside LandDistance: landed"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Landed));
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** Boundary: walking away to just under the spool keeps the fight on; just past it spools the line. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1WalkOutSpools, "Project.Fishing.Fight.QA.S1.WalkOutPastTheSpoolSpools", Flags)
	bool FQAS1WalkOutSpools::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const double Spool = Kit(Data).SpoolLength;
		if (!TestTrue(TEXT("the starter kit has a spool"), Spool > 0.0))
		{
			return false;
		}
		const FVector2D Fish(1000.0, 0.0);
		FLureFightState State = MakeFight(Data, 33, FVector2D::ZeroVector, Fish);
		FLureFight::Step(State, LureRodQA::In(false));
		const float TensionBefore = State.Tension;
		FLureFight::MovePlayer(State, Fish - FVector2D(Spool - 1.0, 0.0));
		FLureFight::Step(State, LureRodQA::In(false));
		TestEqual(TEXT("1 cm under the spool: the fight is on"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::None));
		AddInfo(FString::Printf(TEXT("spool %.0f cm; tension %.4f before the walk, %.4f after"), Spool, TensionBefore, State.Tension));
		FLureFight::MovePlayer(State, Fish - FVector2D(Spool + 1.0, 0.0));
		FLureFight::Step(State, LureRodQA::In(false));
		TestEqual(TEXT("1 cm past the spool: spooled"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Spooled));
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** Walking round the fish past MaxSideDeg: it stays where it is (never pulled back to the limit), with and without steps. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1WalkRound, "Project.Fishing.Fight.QA.S1.WalkRoundPastTheSwingLimitStaysPut", Flags)
	bool FQAS1WalkRound::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FVector2D Fish(1000.0, 0.0);
		const FVector2D Start(0.0, 0.0);
		const double Radius = 1000.0;
		const double Limit = Data.Tuning()->MaxSideDeg;
		const double SweepDeg = Limit + 40.0; // well past the swing limit
		FLureFightState State = MakeFight(Data, 34, Start, Fish);
		double MaxDrift = 0.0;
		const int32 Steps = 600;
		FVector2D Now = Start;
		for (int32 Step = 1; Step <= Steps + 120 && !State.IsOver(); ++Step)
		{
			const double A = FMath::DegreesToRadians(SweepDeg * FMath::Min(1.0, static_cast<double>(Step) / Steps));
			Now = Fish + FVector2D(-FMath::Cos(A), FMath::Sin(A)) * Radius; // around the fish at a constant radius
			FLureFight::MovePlayer(State, Now);
			FLureFight::Step(State, LureRodQA::In(false));
			MaxDrift = FMath::Max(MaxDrift, FVector2D::Distance(FLureFight::FishLocation(State), Fish));
		}
		// The bearing player -> fish, measured against the hook direction (+X), by the test itself.
		const FVector2D Bearing = (Fish - Now).GetSafeNormal();
		const double BearingDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Bearing.X, -1.0, 1.0)));
		AddInfo(FString::Printf(TEXT("walked %.0f deg round the fish; bearing %.1f deg off the hook line (limit %.0f); fight SideDeg %.1f; drift %.4f cm"),
			SweepDeg, BearingDeg, Limit, State.SideDeg, MaxDrift));
		TestFalse(TEXT("the fight is still on"), State.IsOver());
		TestTrue(TEXT("precondition: the player ended past the swing limit"), BearingDeg > Limit + 10.0);
		TestTrue(FString::Printf(TEXT("the fish was never pulled back to the limit (drift %.4f cm)"), MaxDrift), MaxDrift < 0.01);
		TestTrue(TEXT("LineOut is still the real distance"), FMath::IsNearlyEqual(static_cast<double>(State.LineOut), FVector2D::Distance(Now, Fish), 0.01));
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** Failure case: a NaN or infinite player position (bad pawn data) is ignored: the fight is unchanged bit for bit. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1NonFinite, "Project.Fishing.Fight.QA.S1.NonFinitePlayerPositionIsIgnored", Flags)
	bool FQAS1NonFinite::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FVector2D Player(100.0, -50.0);
		const FVector2D Fish(1300.0, 400.0);
		FLureFightState Ref = MakeFight(Data, 35, Player, Fish);
		FLureFightState Bad = MakeFight(Data, 35, Player, Fish);
		const double NaN = static_cast<double>(LureRodQA::QNaN());
		const double Inf = static_cast<double>(LureRodQA::QInf());
		const FVector2D BadInputs[] = { FVector2D(NaN, 0.0), FVector2D(0.0, NaN), FVector2D(Inf, 0.0), FVector2D(0.0, -Inf) };
		int32 Mismatches = 0;
		for (int32 Step = 0; Step < 240 && !Ref.IsOver(); ++Step)
		{
			FLureFight::MovePlayer(Bad, BadInputs[Step % 4]);
			const FLureFightInput Input = LureRodQA::In((Step / 60) % 2 == 0);
			FLureFight::Step(Ref, Input);
			FLureFight::Step(Bad, Input);
			Mismatches += SameState(Ref, Bad) ? 0 : 1;
		}
		TestEqual(TEXT("steps where a non-finite player position changed the fight"), Mismatches, 0);
		TestTrue(TEXT("the fish location stayed finite"), !FLureFight::FishLocation(Bad).ContainsNaN());
		return true;
	}
}

#endif

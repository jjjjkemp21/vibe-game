// Lure T-041c: does the line loop during a fast turn? (implementer's tests). Project.Fishing.Line.FastTurn.*
// A2 playtest: "the line smears into a ghost loop for about 1 s during a fast turn". These tests drive FLureLineSim the way
// ULureFishingLineComponent::Simulate does in Pinned mode (CarryRestLength, TightenRestLength, FloatAmount; the built-in row),
// with no world: the end is pinned on the water 12 m out and the rod tip sweeps a 150 cm-radius arc around the player at a
// fast turn (540 deg/s) and a mouse flick (1080 deg/s) for 0.3 s, then holds still for 1 s.
// A loop = a segment pointing back along the chord (dot(P[i+1] - P[i], End - Tip) < 0) or two segments crossing in the XY
// projection. Measured (T-041c): no loop frame at either rate, slack or taut, so the ghost loop is not the sim (see
// docs/specs/fishing-line.md "Known limits"); these tests guard it.
// Spec: docs/specs/fishing-line.md. Tuning: the built-in row (= data/tables/DT_FishingLine.csv).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"

namespace LureLineFastTurnTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameDt = 1.f / 60.f;
	constexpr int32 NumSegments = 12;
	/** The player turns about (0, 0); the rod tip is this far out and this high over the water (Z = 0). */
	constexpr double TipRadius = 150.0;
	constexpr double TipZ = 200.0;
	/** The bobber, pinned on the water 12 m in front of the player. */
	const FVector EndPoint(1200.0, 0.0, 0.0);
	constexpr float SettleSeconds = 2.f;
	constexpr float TurnSeconds = 0.3f;
	constexpr float HoldSeconds = 1.f;

	struct FTurnResult
	{
		int32 Frames = 0;
		/** Frames (turn + hold) with any loop, with a back-pointing segment, with an XY crossing. */
		int32 LoopFrames = 0;
		int32 BackFrames = 0;
		int32 CrossFrames = 0;
		int32 FirstLoopFrame = INDEX_NONE;
		int32 LastLoopFrame = INDEX_NONE;
		/** Most negative dot(segment, chord direction), cm (0 = never pointed back). */
		double WorstBack = 0.0;
		/** Worst stretch, polyline / rest length - 1 (the other line tests allow 3 % while an end moves fast). */
		double MaxStretch = 0.0;
		bool bFinite = true;
		bool bSettledLoopFree = true;
	};

	FVector TipAt(double AngleDeg)
	{
		const double Rad = FMath::DegreesToRadians(AngleDeg);
		return FVector(TipRadius * FMath::Cos(Rad), TipRadius * FMath::Sin(Rad), TipZ);
	}

	/** 2D proper intersection of segments A-B and C-D (touching ends do not count). */
	bool CrossXY(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		auto Orient = [](const FVector& P, const FVector& Q, const FVector& R)
		{
			return (Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X);
		};
		const double O1 = Orient(A, B, C);
		const double O2 = Orient(A, B, D);
		const double O3 = Orient(C, D, A);
		const double O4 = Orient(C, D, B);
		return ((O1 > 0.0 && O2 < 0.0) || (O1 < 0.0 && O2 > 0.0)) && ((O3 > 0.0 && O4 < 0.0) || (O3 < 0.0 && O4 > 0.0));
	}

	/** Checks one frame's points: back-pointing segments (worst dot, cm) and XY crossings of non-neighbour segments. */
	void CheckFrame(const TArray<FVector>& Points, bool& bOutBack, bool& bOutCross, double& InOutWorstBack)
	{
		bOutBack = false;
		bOutCross = false;
		const FVector ChordDir = (Points.Last() - Points[0]).GetSafeNormal();
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const double Dot = FVector::DotProduct(Points[Index + 1] - Points[Index], ChordDir);
			if (Dot < 0.0)
			{
				bOutBack = true;
				InOutWorstBack = FMath::Min(InOutWorstBack, Dot);
			}
			for (int32 Other = Index + 2; Other + 1 < Points.Num(); ++Other)
			{
				if (CrossXY(Points[Index], Points[Index + 1], Points[Other], Points[Other + 1]))
				{
					bOutCross = true;
				}
			}
		}
	}

	/** Settles the line, sweeps the tip DegPerSecond for TurnSeconds, holds HoldSeconds; checks every turn and hold frame. */
	FTurnResult RunTurn(double DegPerSecond, float Tension01)
	{
		const FLureFishingLineRow Row = FLureFishingLineRules::GetFallbackRow();
		FLureLineSim Sim;
		Sim.Init(NumSegments);

		FTurnResult Result;
		float RestLength = 0.f;
		float LastChord = 0.f;
		bool bFirst = true;
		double Angle = 0.0;

		// The component's Pinned-mode frame (ULureFishingLineComponent::Simulate) with the tip at Tip.
		auto Frame = [&](const FVector& Tip)
		{
			FLureLineSimInput In;
			In.Start = Tip;
			In.End = EndPoint;
			const float Chord = static_cast<float>(FVector::Dist(In.Start, In.End));
			const float Target = FLureFishingLineRules::TargetRestLength(Chord, Tension01, -1.f, Row);
			RestLength = FLureFishingLineRules::CarryRestLength(RestLength, LastChord, Chord);
			RestLength = FLureFishingLineRules::TightenRestLength(RestLength, Target, Chord, FrameDt, Row);
			LastChord = Chord;
			In.Float = FLureFishingLineRules::FloatAmount(Tension01, Row);
			In.RestLength = RestLength;
			In.bHasWater = true;
			In.WaterZ = 0.f;
			if (bFirst)
			{
				Sim.Reset(In);
				bFirst = false;
			}
			Sim.Step(FrameDt, In, Row);
		};

		const int32 SettleFrames = FMath::RoundToInt(SettleSeconds / FrameDt);
		for (int32 Index = 0; Index < SettleFrames; ++Index)
		{
			Frame(TipAt(0.0));
		}
		{
			bool bBack = false;
			bool bCross = false;
			double Ignored = 0.0;
			CheckFrame(Sim.GetPoints(), bBack, bCross, Ignored);
			Result.bSettledLoopFree = !bBack && !bCross;
		}

		const int32 TurnFrames = FMath::RoundToInt(TurnSeconds / FrameDt);
		const int32 HoldFrames = FMath::RoundToInt(HoldSeconds / FrameDt);
		for (int32 Index = 0; Index < TurnFrames + HoldFrames; ++Index)
		{
			if (Index < TurnFrames)
			{
				Angle += DegPerSecond * FrameDt;
			}
			Frame(TipAt(Angle));
			++Result.Frames;
			Result.bFinite &= Sim.IsFinite();
			Result.MaxStretch = FMath::Max(Result.MaxStretch, Sim.GetPolylineLength() / Sim.GetRestLength() - 1.0);

			bool bBack = false;
			bool bCross = false;
			CheckFrame(Sim.GetPoints(), bBack, bCross, Result.WorstBack);
			Result.BackFrames += bBack ? 1 : 0;
			Result.CrossFrames += bCross ? 1 : 0;
			if (bBack || bCross)
			{
				++Result.LoopFrames;
				Result.FirstLoopFrame = Result.FirstLoopFrame == INDEX_NONE ? Index : Result.FirstLoopFrame;
				Result.LastLoopFrame = Index;
			}
		}
		return Result;
	}

	/** Runs one turn, reports its numbers, and fails on any loop frame. */
	void CheckTurn(FAutomationTestBase& Test, double DegPerSecond, float Tension01)
	{
		const FTurnResult Result = RunTurn(DegPerSecond, Tension01);
		Test.AddInfo(FString::Printf(
			TEXT("%.0f deg/s, tension %.2f: %d frames, loop frames %d (back %d, XY crossings %d; first %d, last %d), worst back %.3f cm, worst stretch %.2f %%"),
			DegPerSecond, Tension01, Result.Frames, Result.LoopFrames, Result.BackFrames, Result.CrossFrames, Result.FirstLoopFrame,
			Result.LastLoopFrame, Result.WorstBack, Result.MaxStretch * 100.0));
		Test.TestTrue(TEXT("the settled line has no loop before the turn"), Result.bSettledLoopFree);
		Test.TestTrue(TEXT("every point stays finite"), Result.bFinite);
		Test.TestEqual(TEXT("no frame with a segment pointing back along the chord"), Result.BackFrames, 0);
		Test.TestEqual(TEXT("no frame with segments crossing in the XY projection"), Result.CrossFrames, 0);
		Test.TestTrue(TEXT("the line stretches at most 3 % while the tip sweeps"), Result.MaxStretch <= 0.03);
	}

	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFastTurnSlack540Test, "Project.Fishing.Line.FastTurn.Slack540", Flags)
	bool FLureLineFastTurnSlack540Test::RunTest(const FString& Parameters)
	{
		// A floating bobber (WaitTension): the slackest line the player turns with.
		CheckTurn(*this, 540.0, FLureFishingLineRules::GetFallbackRow().WaitTension);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFastTurnSlack1080Test, "Project.Fishing.Line.FastTurn.Slack1080", Flags)
	bool FLureLineFastTurnSlack1080Test::RunTest(const FString& Parameters)
	{
		// The worst mouse flick: 324 deg in 0.3 s, ~47 cm of tip travel per frame.
		CheckTurn(*this, 1080.0, FLureFishingLineRules::GetFallbackRow().WaitTension);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFastTurnTaut1080Test, "Project.Fishing.Line.FastTurn.Taut1080", Flags)
	bool FLureLineFastTurnTaut1080Test::RunTest(const FString& Parameters)
	{
		// A hooked fish (HookedTension): a nearly straight line whipped round by the flick.
		CheckTurn(*this, 1080.0, FLureFishingLineRules::GetFallbackRow().HookedTension);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

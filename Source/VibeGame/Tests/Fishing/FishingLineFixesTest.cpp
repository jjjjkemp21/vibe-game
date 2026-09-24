// Lure T-032 fixes after QA (Saved/AgentLogs/qa/20260923-174000-T032.md): B1 exactly straight at the snap threshold through
// the length follow, O1 +Inf tension = taut, O2 a slack line to a deep end doesn't stretch, O3 a 15 m line straightens in
// ~0.4 s without whipping (TightenRestLength). Project.Fishing.Line.Fixes.* Spec: docs/specs/fishing-line.md.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include <cmath>
#include <limits>

namespace LureFishingLineFixesTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameDt = 1.f / 60.f;

	FLureFishingLineRow ShippedRow()
	{
		return FLureFishingLineRules::GetFallbackRow();
	}

	int32 FramesFor(float Seconds, float StepDt = FrameDt)
	{
		return FMath::CeilToInt32(Seconds / StepDt);
	}

	/** Largest distance of any point from the straight segment between the first and the last point, cm. */
	double OffStraight(const TArray<FVector>& Points)
	{
		double Max = 0.0;
		for (const FVector& Point : Points)
		{
			Max = FMath::Max(Max, static_cast<double>(FMath::PointDistToSegment(Point, Points[0], Points.Last())));
		}
		return Max;
	}

	/** Worst segment stretch over SegmentLength (0.03 = 3 % too long; <= 0 = none stretched). */
	double WorstStretch(const TArray<FVector>& Points, double SegmentLength)
	{
		double Worst = -1.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Worst = FMath::Max(Worst, FVector::Dist(Points[Index - 1], Points[Index]) / SegmentLength - 1.0);
		}
		return Worst;
	}

	FLureLineSimInput PinnedLine(const FVector& Start, const FVector& End, float RestLength)
	{
		FLureLineSimInput In;
		In.Start = Start;
		In.End = End;
		In.RestLength = RestLength;
		return In;
	}

	/** A transient game world; lines are simulated (no mesh) and driven frame by frame. */
	struct FFixLineWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			return World != nullptr;
		}

		ULureFishingLineComponent* AddLine(int32 Segments)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			if (!Owner)
			{
				return nullptr;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			ULureFishingLineComponent* Line = NewObject<ULureFishingLineComponent>(Owner, NAME_None, RF_Transient);
			Line->SetupAttachment(Root);
			Line->RegisterComponent();
			Line->Setup(nullptr, nullptr, FLinearColor::White, Segments);
			Line->SetTuning(ShippedRow());
			return Line;
		}
	};

	void HoldLine(ULureFishingLineComponent* Line, const FVector& Tip, const FVector& End, int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Line->SetEndpoints(Tip, End);
			Line->UpdateLine(FrameDt);
		}
	}

	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFixFollowTest, "Project.Fishing.Line.Fixes.FollowEndsExactlyOnTarget", Flags)
	bool FLureLineFixFollowTest::RunTest(const FString& Parameters)
	{
		// B1: the shrinking length reaches its target exactly (no stall a few float steps above it), at any frame rate.
		const FLureFishingLineRow Row = ShippedRow();
		for (const float StepDt : { 1.f / 20.f, 1.f / 60.f, 1.f / 240.f, 1.f / 1000.f })
		{
			for (const float Target : { 50.f, 1500.f, 1578.7654f, 3000.1f, 20000.f })
			{
				float Length = Target * 1.05f;
				int32 Frames = 0;
				bool bFalls = true;
				while (Length != Target && Frames < 100000)
				{
					const float Next = FLureFishingLineRules::FollowRestLength(Length, Target, Target, StepDt, Row);
					bFalls &= Next <= Length && Next >= Target;
					Length = Next;
					++Frames;
				}
				const FString Label = FString::Printf(TEXT("target %.4f cm at %.0f fps"), Target, 1.f / StepDt);
				TestTrue(FString::Printf(TEXT("%s: reaches the target exactly (after %.3f s)"), *Label, Frames * StepDt), Length == Target);
				TestTrue(Label + TEXT(": the length only falls, never below the target"), bFalls);
			}
		}
		TestEqual(TEXT("a hair (5e-6) above the target is the target"), FLureFishingLineRules::FollowRestLength(1500.f * (1.f + 5.0e-6f), 1500.f, 0.f, FrameDt, Row), 1500.f);
		const float Visible = 1500.f * 1.001f;
		TestTrue(TEXT("a visible slack (0.1 %) is not snapped away in one frame"), FLureFishingLineRules::FollowRestLength(Visible, 1500.f, 0.f, FrameDt, Row) > 1500.5f);
		TestEqual(TEXT("slack still appears at once"), FLureFishingLineRules::FollowRestLength(1500.f, 1575.f, 1500.f, FrameDt, Row), 1575.f);

		// O3: a pinned line tightens at least at a steady sag rate toward a straight target: full slack -> exactly the chord in
		// StraightenTime s at any frame rate; toward a still-slack target it eases in and still arrives exactly, in finite time.
		for (const float StepDt : { 1.f / 20.f, 1.f / 60.f, 1.f / 240.f, 1.f / 1000.f })
		{
			for (const float Chord : { 50.f, 1529.7f, 2980.f })
			{
				for (const float Tension : { 1.f, 0.9f, 0.6f })
				{
					const float Target = FLureFishingLineRules::TargetRestLength(Chord, Tension, -1.f, Row);
					float Length = FLureFishingLineRules::TargetRestLength(Chord, 0.f, -1.f, Row);
					int32 Frames = 0;
					bool bFalls = true;
					while (Length != Target && Frames < 100000)
					{
						const float Next = FLureFishingLineRules::TightenRestLength(Length, Target, Chord, StepDt, Row);
						bFalls &= Next <= Length && Next >= Target;
						Length = Next;
						++Frames;
					}
					const float Seconds = Frames * StepDt;
					const FString Label = FString::Printf(TEXT("tighten %.1f cm to tension %.1f at %.0f fps"), Chord, Tension, 1.f / StepDt);
					TestTrue(FString::Printf(TEXT("%s: reaches the target exactly (after %.3f s)"), *Label, Seconds), Length == Target);
					TestTrue(Label + TEXT(": the length only falls, never below the target"), bFalls);
					if (Tension == 1.f)
					{
						TestTrue(FString::Printf(TEXT("%s: full slack to straight within StraightenTime %.2f s (%.3f s)"), *Label, Row.StraightenTime, Seconds),
							Seconds <= Row.StraightenTime + StepDt + 0.01f && Seconds >= 0.5f * Row.StraightenTime);
					}
					else
					{
						TestTrue(FString::Printf(TEXT("%s: a slack target within 2 s (%.3f s)"), *Label, Seconds), Seconds <= 2.f);
					}
				}
			}
		}
		TestEqual(TEXT("tighten: slack appears at once"), FLureFishingLineRules::TightenRestLength(1500.f, 1575.f, 1500.f, FrameDt, Row), 1575.f);
		TestEqual(TEXT("tighten: never below the chord"), FLureFishingLineRules::TightenRestLength(1575.f, 1400.f, 1500.f, 10.f, Row), 1500.f);
		TestEqual(TEXT("tighten: a NaN length recovers to the target"), FLureFishingLineRules::TightenRestLength(std::numeric_limits<float>::quiet_NaN(), 1510.f, 1500.f, FrameDt, Row), 1510.f);
		TestEqual(TEXT("tighten: no time, no change"), FLureFishingLineRules::TightenRestLength(1575.f, 1500.f, 1500.f, 0.f, Row), 1575.f);
		TestEqual(TEXT("tighten: a line too short to sag follows like FollowRestLength"), FLureFishingLineRules::TightenRestLength(5.f, 0.f, 0.f, FrameDt, Row),
			FLureFishingLineRules::FollowRestLength(5.f, 0.f, 0.f, FrameDt, Row));

		// O1: an infinite tension is fully taut, not slack.
		const float Inf = std::numeric_limits<float>::infinity();
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		TestEqual(TEXT("Tautness(+Inf) = 1"), FLureFishingLineRules::Tautness(Inf, Row), 1.f);
		TestEqual(TEXT("Tautness(-Inf) = 0"), FLureFishingLineRules::Tautness(-Inf, Row), 0.f);
		TestEqual(TEXT("Tautness(NaN) = 0"), FLureFishingLineRules::Tautness(NaN, Row), 0.f);
		TestEqual(TEXT("TargetRestLength at +Inf tension = the chord"), FLureFishingLineRules::TargetRestLength(1500.f, Inf, -1.f, Row), 1500.f);
		TestEqual(TEXT("StateTension: a fight at +Inf = 1"), FLureFishingLineRules::StateTension(ELureFishingState::Hooked, true, Inf, Row), 1.f);
		TestEqual(TEXT("StateTension: a fight at NaN = 0"), FLureFishingLineRules::StateTension(ELureFishingState::Hooked, true, NaN, Row), 0.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFixSolverTautTest, "Project.Fishing.Line.Fixes.SolverTautAFloatStepAboveTheChord", Flags)
	bool FLureLineFixSolverTautTest::RunTest(const FString& Parameters)
	{
		// B1: a rest length that is the chord rounded to float, or a few float steps above it, lies exactly straight; a real
		// (visible) excess still sags.
		const FLureFishingLineRow Row = ShippedRow();
		const FVector Tip(0.f, 0.f, 1000.f);
		const FVector Ends[] = { FVector(1500.f, 0.f, 1000.f), FVector(1500.f, 200.f, 550.f), FVector(-2000.f, 2200.f, 500.f), FVector(37.3f, 11.1f, 975.9f) };
		for (const FVector& End : Ends)
		{
			const double Chord = FVector::Dist(Tip, End);
			for (const int32 Ulps : { 0, 1, 5, 8 })
			{
				float Rest = static_cast<float>(Chord);
				for (int32 Ulp = 0; Ulp < Ulps; ++Ulp)
				{
					Rest = std::nextafter(Rest, 1.0e9f);
				}
				FLureLineSim Sim;
				Sim.Init(12);
				for (int32 Frame = 0; Frame < FramesFor(3.f); ++Frame)
				{
					Sim.Step(FrameDt, PinnedLine(Tip, End, Rest), Row);
				}
				TestTrue(FString::Printf(TEXT("chord %.4f + %d float steps (%.6f cm): straight (%.4f cm off)"), Chord, Ulps, Rest - Chord, OffStraight(Sim.GetPoints())),
					OffStraight(Sim.GetPoints()) < 0.01);
			}
		}
		FLureLineSim Slack;
		Slack.Init(12);
		for (int32 Frame = 0; Frame < FramesFor(3.f); ++Frame)
		{
			Slack.Step(FrameDt, PinnedLine(Tip, Ends[0], 1500.f * 1.0001f), Row);
		}
		TestTrue(FString::Printf(TEXT("0.15 cm of real slack on 15 m still sags (%.2f cm)"), OffStraight(Slack.GetPoints())), OffStraight(Slack.GetPoints()) > 1.0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFixStraightenTest, "Project.Fishing.Line.Fixes.StraightensWithinHalfASecond", Flags)
	bool FLureLineFixStraightenTest::RunTest(const FString& Parameters)
	{
		// O3 + B1 through the component: slack at tension 0 (the sag look is kept), then at the snap threshold the line
		// straightens without a pop, is within 1 cm of straight at 0.5 s and exactly straight right after.
		FFixLineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		struct FCase
		{
			const TCHAR* Name;
			FVector Tip;
			FVector End;
		};
		const FCase Cases[] = {
			{ TEXT("15 m level"), FVector(0.f, 0.f, 1000.f), FVector(1500.f, 0.f, 1000.f) },
			{ TEXT("15 m from 3 m up"), FVector(0.f, 0.f, 1300.f), FVector(1500.f, 0.f, 1000.f) },
			{ TEXT("30 m"), FVector(0.f, 0.f, 500.f), FVector(-2000.f, 2200.f, 300.f) },
		};
		for (const FCase& Case : Cases)
		{
			ULureFishingLineComponent* Line = W.AddLine(12);
			if (!TestNotNull(TEXT("a line"), Line))
			{
				return false;
			}
			Line->SetWaterSurfaceZ(-100000.f);
			Line->SetTension(0.f);
			HoldLine(Line, Case.Tip, Case.End, FramesFor(4.f));
			const double Slack = OffStraight(Line->GetPoints());
			const double Chord = FVector::Dist(Case.Tip, Case.End);
			TestTrue(FString::Printf(TEXT("%s: slack at no tension (%.1f cm of sag, > 6 %% of the length)"), Case.Name, Slack), Slack > 0.06 * Chord);

			Line->SetTension(1.f);
			HoldLine(Line, Case.Tip, Case.End, 1);
			const double FirstFrame = OffStraight(Line->GetPoints());
			TestTrue(FString::Printf(TEXT("%s: no pop (one frame later %.1f of %.1f cm)"), Case.Name, FirstFrame, Slack), FirstFrame > 0.5 * Slack);
			FString Timeline;
			double AtQuarter = 0.0;
			double AtHalf = 0.0;
			double WorstRise = 0.0;
			double Previous = FirstFrame;
			for (int32 Frame = 2; Frame <= FramesFor(0.5f); ++Frame)
			{
				HoldLine(Line, Case.Tip, Case.End, 1);
				WorstRise = FMath::Max(WorstRise, OffStraight(Line->GetPoints()) - Previous);
				Previous = OffStraight(Line->GetPoints());
				if (Frame % 5 == 0)
				{
					Timeline += FString::Printf(TEXT(" %.2fs:%.2f"), Frame * FrameDt, OffStraight(Line->GetPoints()));
				}
				if (Frame == FramesFor(0.25f))
				{
					AtQuarter = OffStraight(Line->GetPoints());
				}
			}
			AtHalf = OffStraight(Line->GetPoints());
			AddInfo(FString::Printf(TEXT("%s: slack %.1f cm; cm off straight after full tension:%s"), Case.Name, Slack, *Timeline));
			TestTrue(FString::Printf(TEXT("%s: visibly straightening at 0.25 s (%.1f cm, still > 0)"), Case.Name, AtQuarter), AtQuarter < 0.5 * Slack && AtQuarter > 0.0);
			TestTrue(FString::Printf(TEXT("%s: no whip: the sag never grows while straightening (worst %.2f cm per frame)"), Case.Name, WorstRise), WorstRise <= 0.5);
			TestTrue(FString::Printf(TEXT("%s: straight at 0.5 s (%.3f cm off)"), Case.Name, AtHalf), AtHalf < 1.0);
			HoldLine(Line, Case.Tip, Case.End, FramesFor(0.1f));
			TestTrue(FString::Printf(TEXT("%s: exactly straight at 0.6 s (%.4f cm off)"), Case.Name, OffStraight(Line->GetPoints())), OffStraight(Line->GetPoints()) < 0.01);
			TestEqual(FString::Printf(TEXT("%s: the length is exactly the target"), Case.Name), Line->GetRestLength(), Line->GetTargetRestLength());
			Line->Hide();
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFixInfTensionTest, "Project.Fishing.Line.Fixes.InfiniteTensionIsTaut", Flags)
	bool FLureLineFixInfTensionTest::RunTest(const FString& Parameters)
	{
		// O1: SetTension(+Inf) clamps to 1 (fully taut); -Inf to 0; NaN = slack.
		FFixLineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ULureFishingLineComponent* Line = W.AddLine(12);
		if (!TestNotNull(TEXT("a line"), Line))
		{
			return false;
		}
		const float Inf = std::numeric_limits<float>::infinity();
		Line->SetTension(Inf);
		TestEqual(TEXT("+Inf tension = 1"), Line->GetTension(), 1.f);
		Line->SetTension(-Inf);
		TestEqual(TEXT("-Inf tension = 0"), Line->GetTension(), 0.f);
		Line->SetTension(0.7f);
		Line->SetTension(std::numeric_limits<float>::quiet_NaN());
		TestEqual(TEXT("NaN tension = 0"), Line->GetTension(), 0.f);

		const FVector Tip(0.f, 0.f, 1000.f);
		const FVector End(1500.f, 0.f, 1000.f);
		Line->SetWaterSurfaceZ(-100000.f);
		HoldLine(Line, Tip, End, FramesFor(2.f));
		Line->SetTension(Inf);
		HoldLine(Line, Tip, End, FramesFor(2.f));
		TestTrue(FString::Printf(TEXT("at +Inf tension the line is exactly straight (%.4f cm off)"), OffStraight(Line->GetPoints())), OffStraight(Line->GetPoints()) < 0.01);
		Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFixDeepEndTest, "Project.Fishing.Line.Fixes.DeepEndDoesNotStretch", Flags)
	bool FLureLineFixDeepEndTest::RunTest(const FString& Parameters)
	{
		// O2: a slack line on the water to an end deep under it (a diving fish) never stretches: every segment stays within
		// 2 % of its rest length in every frame, at 1 m and 4 m deep (and 30 cm / 2.5 m), slack and half tension. Where there
		// is slack the line still floats on the water.
		const FLureFishingLineRow Row = ShippedRow();
		const FVector Rod(0.f, 0.f, 270.f);
		const double SurfaceZ = Row.FloatHeight;
		FString Record;
		for (const float Tension : { 0.f, 0.5f })
		{
			for (const float Depth : { 30.f, 100.f, 250.f, 400.f })
			{
				const FVector Fish(1500.f, 0.f, -Depth);
				FLureLineSim Sim;
				Sim.Init(12);
				FLureLineSimInput In = PinnedLine(Rod, Fish, FLureFishingLineRules::TargetRestLength(static_cast<float>(FVector::Dist(Rod, Fish)), Tension, -1.f, Row));
				In.bHasWater = true;
				In.WaterZ = 0.f;
				In.Float = FLureFishingLineRules::FloatAmount(Tension, Row);
				double Worst = -1.0;
				for (int32 Frame = 0; Frame < FramesFor(4.f); ++Frame)
				{
					Sim.Step(FrameDt, In, Row);
					Worst = FMath::Max(Worst, WorstStretch(Sim.GetPoints(), Sim.GetSegmentLength()));
				}
				int32 Floating = 0;
				const TArray<FVector>& P = Sim.GetPoints();
				for (int32 Index = 1; Index + 1 < P.Num(); ++Index)
				{
					Floating += FMath::Abs(P[Index].Z - SurfaceZ) < 2.0 ? 1 : 0;
				}
				const FString Label = FString::Printf(TEXT("tension %.1f, end %.0f cm deep"), Tension, Depth);
				Record += FString::Printf(TEXT(" %s: worst stretch %.3f %%, %d points on the water;"), *Label, Worst * 100.0, Floating);
				TestTrue(FString::Printf(TEXT("%s: no segment stretched over 2 %% (worst %.3f %%)"), *Label, Worst * 100.0), Worst <= 0.02);
				TestTrue(Label + TEXT(": pinned at both ends"), P[0].Equals(Rod, 0.01) && P.Last().Equals(Fish, 0.01));
				TestTrue(Label + TEXT(": finite"), Sim.IsFinite());
				if (Tension == 0.f && Depth <= 100.f)
				{
					TestTrue(FString::Printf(TEXT("%s: the slack line still floats (%d points on the water)"), *Label, Floating), Floating >= 2);
				}
			}
		}
		AddInfo(TEXT("deep ends:") + Record);

		// The same through the component, the fish diving from the surface to 4 m and back.
		FFixLineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ULureFishingLineComponent* Line = W.AddLine(12);
		if (!TestNotNull(TEXT("a line"), Line))
		{
			return false;
		}
		Line->SetWaterSurfaceZ(0.f);
		Line->SetTension(0.f);
		double Worst = -1.0;
		for (int32 Frame = 0; Frame < FramesFor(8.f); ++Frame)
		{
			const double Dive = 0.5 - 0.5 * FMath::Cos(Frame * FrameDt * UE_PI / 4.0); // 0 -> 1 -> 0 over 8 s
			Line->SetEndpoints(Rod, FVector(1500.f, 0.f, -400.0 * Dive));
			Line->UpdateLine(FrameDt);
			Worst = FMath::Max(Worst, WorstStretch(Line->GetPoints(), Line->GetRestLength() / Line->GetNumSegments()));
		}
		TestTrue(FString::Printf(TEXT("component, a fish diving to 4 m and back: no segment stretched over 2 %% (worst %.3f %%)"), Worst * 100.0), Worst <= 0.02);
		Line->Hide();
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

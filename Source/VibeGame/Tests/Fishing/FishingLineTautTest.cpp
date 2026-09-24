// Lure T-032b part C: the line pulls straight under tension (implementer's tests). Project.Fishing.Line.Taut.*
// A pinned line's length is carried to its ends' new distance each frame (FLureFishingLineRules::CarryRestLength, then
// TightenRestLength) and FLureLineSim::Step reaches the frame's length over its sub-steps (clamped per sub-step to the
// straight distance), so ends that close in leave no extra line behind.
// Spec: docs/specs/fishing-line.md. Tuning: the built-in row (= data/tables/DT_FishingLine.csv).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Fishing/FishFight.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include <limits>

namespace LureLineTautTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameDt = 1.f / 60.f;

	FLureFishingLineRow TautRow()
	{
		return FLureFishingLineRules::GetFallbackRow();
	}

	/** Largest distance of any point from the straight segment between the first and the last point, cm. */
	double Sag(const TArray<FVector>& Points)
	{
		double Max = 0.0;
		for (const FVector& Point : Points)
		{
			Max = FMath::Max(Max, static_cast<double>(FMath::PointDistToSegment(Point, Points[0], Points.Last())));
		}
		return Max;
	}

	double PolylineLength(const TArray<FVector>& Points)
	{
		double Length = 0.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += FVector::Dist(Points[Index - 1], Points[Index]);
		}
		return Length;
	}

	/** A transient game world with one plain actor that owns a line component (no mesh: nothing drawn). */
	struct FTautWorld
	{
		FTestWorldWrapper Wrapper;
		ULureFishingLineComponent* Line = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Owner = Wrapper.GetTestWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			if (!Test.TestNotNull(TEXT("the line owner spawns"), Owner))
			{
				return false;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("TautTestLine"), RF_Transient);
			Line->SetupAttachment(Root);
			Line->RegisterComponent();
			Line->Setup(nullptr, nullptr, FLinearColor::White, 12);
			Line->SetTuning(TautRow());
			Line->SetWaterSurfaceZ(0.f);
			return true;
		}

		void Tick(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(FrameDt);
			}
		}
	};

	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineTautCarryRuleTest, "Project.Fishing.Line.Taut.Rules.CarryRestLength", Flags)
	bool FLureLineTautCarryRuleTest::RunTest(const FString& Parameters)
	{
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		TestNearlyEqual(TEXT("the same share of slack at the new distance"), FLureFishingLineRules::CarryRestLength(1100.f, 1000.f, 500.f), 550.f, 0.001f);
		TestNearlyEqual(TEXT("ends that part keep the share too"), FLureFishingLineRules::CarryRestLength(1100.f, 1000.f, 2000.f), 2200.f, 0.01f);
		TestNearlyEqual(TEXT("share clamped to 2"), FLureFishingLineRules::CarryRestLength(3000.f, 1000.f, 500.f), 1000.f, 0.001f);
		TestNearlyEqual(TEXT("share clamped to 1 (never shorter than the distance)"), FLureFishingLineRules::CarryRestLength(900.f, 1000.f, 500.f), 500.f, 0.001f);
		TestEqual(TEXT("LastChord < 1 cm (a new line): unchanged"), FLureFishingLineRules::CarryRestLength(1234.f, 0.5f, 500.f), 1234.f);
		TestEqual(TEXT("LastChord 0: unchanged"), FLureFishingLineRules::CarryRestLength(1234.f, 0.f, 500.f), 1234.f);
		TestEqual(TEXT("Chord NaN: unchanged"), FLureFishingLineRules::CarryRestLength(1234.f, 1000.f, NaN), 1234.f);
		TestEqual(TEXT("LastChord NaN: unchanged"), FLureFishingLineRules::CarryRestLength(1234.f, NaN, 500.f), 1234.f);
		TestTrue(TEXT("Current NaN: unchanged (NaN)"), FMath::IsNaN(FLureFishingLineRules::CarryRestLength(NaN, 1000.f, 500.f)));
		TestEqual(TEXT("Chord < 0: unchanged"), FLureFishingLineRules::CarryRestLength(1234.f, 1000.f, -5.f), 1234.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineTautStraightensTest, "Project.Fishing.Line.Taut.Component.StraightensUnderTension", Flags)
	bool FLureLineTautStraightensTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = TautRow();
		const FLureFishFightRow Fight; // the fight's defaults (TautTension)
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Fish(1500.f, 0.f, 0.f);
		const double Span = FVector::Dist(Tip, Fish);

		for (const float FightTension : {0.43f, 0.84f})
		{
			FTautWorld W;
			if (!W.Create(*this))
			{
				return false;
			}
			// A slack waiting line, then a fish pulls with FightTension of the line's strength.
			W.Line->SetTension(0.f);
			W.Line->SetEndpoints(Tip, Fish);
			for (int32 Frame = 0; Frame < 180; ++Frame)
			{
				W.Line->SetEndpoints(Tip, Fish);
				W.Tick(1);
			}
			const double SlackSag = Sag(W.Line->GetPoints());
			const float ShownTension = FLureFight::LineTension(FightTension, Fight);
			W.Line->SetTension(ShownTension);
			const int32 Frames = FMath::CeilToInt32((Row.StraightenTime + 0.1f) / FrameDt);
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				W.Line->SetEndpoints(Tip, Fish);
				W.Tick(1);
			}
			const double TautSag = Sag(W.Line->GetPoints());
			AddInfo(FString::Printf(TEXT("StraightensUnderTension T=%.2f (line %.2f): sag %.1f -> %.2f cm over a %.0f cm span"), FightTension, ShownTension, SlackSag, TautSag, Span));
			TestTrue(FString::Printf(TEXT("T=%.2f: the waiting line sags (%.1f cm)"), FightTension, SlackSag), SlackSag > 0.02 * Span);
			TestTrue(FString::Printf(TEXT("T=%.2f: pulled straight after StraightenTime (sag %.2f cm < 2%% of %.0f)"), FightTension, TautSag, Span), TautSag < 0.02 * Span);

			// Carry: the fish is reeled toward the rod at 300 cm/s for 2 s. The line stays straight and never stretches.
			double MaxSag = 0.0;
			bool bNoStretch = true;
			bool bPinned = true;
			for (int32 Frame = 1; Frame <= 120; ++Frame)
			{
				const FVector End = Fish + (Tip - Fish).GetSafeNormal() * (300.0 * Frame * FrameDt);
				W.Line->SetEndpoints(Tip, End);
				W.Tick(1);
				const TArray<FVector>& Points = W.Line->GetPoints();
				MaxSag = FMath::Max(MaxSag, Sag(Points));
				bNoStretch &= PolylineLength(Points) <= W.Line->GetRestLength() * 1.001 + 0.5;
				bPinned &= Points.Last().Equals(End, 0.01);
			}
			const double NearSpan = FVector::Dist(Tip, W.Line->GetPoints().Last());
			AddInfo(FString::Printf(TEXT("Carry T=%.2f: max sag %.2f cm while reeled from %.0f to %.0f cm"), FightTension, MaxSag, Span, NearSpan));
			TestTrue(FString::Printf(TEXT("T=%.2f: reeled in, it stays straight (max sag %.2f cm)"), FightTension, MaxSag), MaxSag < 0.02 * NearSpan);
			TestTrue(FString::Printf(TEXT("T=%.2f: ... and never stretches"), FightTension), bNoStretch);
			TestTrue(FString::Printf(TEXT("T=%.2f: ... its end on the fish"), FightTension), bPinned);
			W.Line->Hide();
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineTautClosingSimTest, "Project.Fishing.Line.Taut.Sim.ClosingEndsStayStraight", Flags)
	bool FLureLineTautClosingSimTest::RunTest(const FString& Parameters)
	{
		// Ends closing in at 300 cm/s with the length = the distance every frame: the length is reached over the frame's
		// sub-steps, so no frame of extra line is left to sag (a whole-frame clamp to the longer chord left ~5 cm per frame).
		const FLureFishingLineRow Row = TautRow();
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Far(1500.f, 0.f, 0.f);
		FLureLineSim Sim;
		Sim.Init(16);
		FLureLineSimInput In;
		In.Start = Tip;
		In.End = Far;
		In.RestLength = static_cast<float>(FVector::Dist(Tip, Far));
		Sim.Reset(In);
		double MaxSag = 0.0;
		for (int32 Frame = 1; Frame <= 90; ++Frame)
		{
			In.End = Far + (Tip - Far).GetSafeNormal() * (300.0 * Frame * FrameDt);
			In.RestLength = static_cast<float>(FVector::Dist(Tip, In.End));
			Sim.Step(FrameDt, In, Row);
			MaxSag = FMath::Max(MaxSag, Sag(Sim.GetPoints()));
		}
		AddInfo(FString::Printf(TEXT("ClosingEndsStayStraight: max sag %.3f cm"), MaxSag));
		TestTrue(FString::Printf(TEXT("the line stays straight while its ends close in (max sag %.3f cm)"), MaxSag), MaxSag < 1.0);
		TestTrue(TEXT("finite"), Sim.IsFinite());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineTautRestLerpTest, "Project.Fishing.Line.Taut.Sim.RestLerpNoPop", Flags)
	bool FLureLineTautRestLerpTest::RunTest(const FString& Parameters)
	{
		// A slack line (43% longer than the distance) is cut to the distance in one frame: it ends that frame straight, never
		// longer than its length, and doesn't whip past the straight line or bounce afterwards.
		const FLureFishingLineRow Row = TautRow();
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector End(1500.f, 0.f, 0.f);
		const double Span = FVector::Dist(Tip, End);
		FLureLineSim Sim;
		Sim.Init(16);
		FLureLineSimInput In;
		In.Start = Tip;
		In.End = End;
		In.RestLength = static_cast<float>(Span * 1.43);
		Sim.Reset(In);
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			Sim.Step(FrameDt, In, Row);
		}
		const int32 Mid = Sim.GetNumSegments() / 2;
		const FVector SlackMid = Sim.GetPoints()[Mid];
		const double SlackSag = Sag(Sim.GetPoints());
		TestTrue(FString::Printf(TEXT("the slack line sags (%.1f cm)"), SlackSag), SlackSag > 0.1 * Span);

		In.RestLength = static_cast<float>(Span);
		Sim.Step(FrameDt, In, Row);
		const double RestAfter = Sim.GetRestLength();
		TestNearlyEqual(TEXT("the frame ends at the new length"), RestAfter, Span, 0.01);
		TestTrue(TEXT("... never longer than it"), PolylineLength(Sim.GetPoints()) <= RestAfter * 1.001);
		const double MidMove = FVector::Dist(Sim.GetPoints()[Mid], SlackMid);
		TestTrue(FString::Printf(TEXT("the midpoint moves at most the sag it loses (%.1f of %.1f cm)"), MidMove, SlackSag), MidMove <= SlackSag * 1.05);

		double WorstLater = 0.0;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Sim.Step(FrameDt, In, Row);
			WorstLater = FMath::Max(WorstLater, Sag(Sim.GetPoints()));
		}
		AddInfo(FString::Printf(TEXT("RestLerpNoPop: sag %.1f cm, midpoint moved %.1f cm in the cut frame, later worst sag %.3f cm"), SlackSag, MidMove, WorstLater));
		TestTrue(FString::Printf(TEXT("it stays straight afterwards, no bounce (worst sag %.3f cm)"), WorstLater), WorstLater < 1.0);
		TestTrue(TEXT("finite"), Sim.IsFinite());
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

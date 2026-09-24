// Lure T-032b part B: a landed fish hangs BELOW the rod tip (implementer's tests). Project.Fishing.Line.Hang.*
// The hanging line is reeled in by FLureFishingLineRules::ReelInRestLength (HangReelSpeed, slowing at half of gravity) and the
// sim's swing guard (FLureLineSimInput::MaxSwingDeg = HangMaxSwingDeg) keeps a free end from rising over the tip.
// Spec: docs/specs/fishing-line.md. Tuning: the built-in row (= data/tables/DT_FishingLine.csv).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include <limits>

namespace LureLineHangTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameDt = 1.f / 60.f;

	FLureFishingLineRow HangRow()
	{
		return FLureFishingLineRules::GetFallbackRow();
	}

	/** A line hanging straight down Length cm under Tip with the row's hanging-fish mass and drag. */
	FLureLineSimInput HangInput(const FVector& Tip, float Length, float MaxSwingDeg)
	{
		const FLureFishingLineRow Row = HangRow();
		FLureLineSimInput In;
		In.Start = Tip;
		In.End = Tip - FVector(0.f, 0.f, Length);
		In.bFreeEnd = true;
		In.EndMass = Row.HangEndMass;
		In.EndDrag = Row.HangDrag;
		In.RestLength = Length;
		In.MaxSwingDeg = MaxSwingDeg;
		return In;
	}

	/** A transient game world with one plain actor that owns a line component (no mesh: nothing drawn). */
	struct FHangWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ULureFishingLineComponent* Line = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			AActor* Owner = SpawnMovable(FVector::ZeroVector);
			if (!Test.TestNotNull(TEXT("the line owner spawns"), Owner))
			{
				return false;
			}
			Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("HangTestLine"), RF_Transient);
			Line->SetupAttachment(Owner->GetRootComponent());
			Line->RegisterComponent();
			Line->Setup(nullptr, nullptr, FLinearColor::White, 12);
			Line->SetTuning(HangRow());
			return true;
		}

		AActor* SpawnMovable(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			if (Actor)
			{
				USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
				Actor->SetRootComponent(Root);
				Root->RegisterComponent();
				Root->SetWorldLocation(Location);
			}
			return Actor;
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

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineHangReelRuleTest, "Project.Fishing.Line.Hang.Rules.ReelInRestLength", Flags)
	bool FLureLineHangReelRuleTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = HangRow();
		TestEqual(TEXT("shipped HangReelSpeed is 500 cm/s"), Row.HangReelSpeed, 500.f);

		// Far from the target: capped at HangReelSpeed.
		TestNearlyEqual(TEXT("far away: HangReelSpeed x dt"), FLureFishingLineRules::ReelInRestLength(1500.f, 100.f, 0.1f, Row), 1450.f, 0.01f);
		// Near the target: slows as sqrt(2 x (g / 2) x remaining).
		const float Near = FLureFishingLineRules::ReelInRestLength(150.f, 100.f, 0.01f, Row);
		TestNearlyEqual(TEXT("near: sqrt(980 x 50) x dt"), Near, 150.f - FMath::Sqrt(980.f * 50.f) * 0.01f, 0.01f);
		// Never below the target; a shorter line jumps to it.
		TestEqual(TEXT("never below the target"), FLureFishingLineRules::ReelInRestLength(101.f, 100.f, 1.f, Row), 100.f);
		TestEqual(TEXT("a shorter line drops to the target at once"), FLureFishingLineRules::ReelInRestLength(50.f, 100.f, FrameDt, Row), 100.f);
		// No time or bad time: unchanged; a bad current length: the target.
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		TestEqual(TEXT("dt 0: unchanged"), FLureFishingLineRules::ReelInRestLength(800.f, 100.f, 0.f, Row), 800.f);
		TestEqual(TEXT("dt < 0: unchanged"), FLureFishingLineRules::ReelInRestLength(800.f, 100.f, -1.f, Row), 800.f);
		TestEqual(TEXT("dt NaN: unchanged"), FLureFishingLineRules::ReelInRestLength(800.f, 100.f, NaN, Row), 800.f);
		TestEqual(TEXT("current NaN: the target"), FLureFishingLineRules::ReelInRestLength(NaN, 100.f, FrameDt, Row), 100.f);
		TestTrue(TEXT("target NaN: finite"), FMath::IsFinite(FLureFishingLineRules::ReelInRestLength(800.f, NaN, FrameDt, Row)));

		// A full reel from 15 m at 60 fps: monotonic, never faster than the cap, arrives exactly, in about 3.4 s.
		float Rest = 1500.f;
		int32 Frames = 0;
		bool bCapped = true;
		bool bSlowsDown = true;
		float LastDrop = Row.HangReelSpeed * FrameDt;
		while (Rest > 100.f && Frames < 1000)
		{
			const float Next = FLureFishingLineRules::ReelInRestLength(Rest, 100.f, FrameDt, Row);
			const float Drop = Rest - Next;
			bCapped &= Drop <= Row.HangReelSpeed * FrameDt + 0.01f && Drop > 0.f;
			if (Rest - 100.f < 200.f && Next > 100.f)
			{
				bSlowsDown &= Drop <= LastDrop + 0.01f; // decelerating near the end
			}
			LastDrop = Drop;
			Rest = Next;
			++Frames;
		}
		TestTrue(TEXT("every frame shrinks, at most HangReelSpeed"), bCapped);
		TestTrue(TEXT("slows down toward the end"), bSlowsDown);
		TestEqual(TEXT("arrives exactly at the target"), Rest, 100.f);
		const float Seconds = Frames * FrameDt;
		TestTrue(FString::Printf(TEXT("15 m to 1 m takes about 3.4 s (%.2f s)"), Seconds), Seconds > 3.f && Seconds < 4.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineHangSwingGuardTest, "Project.Fishing.Line.Hang.Sim.SwingGuard", Flags)
	bool FLureLineHangSwingGuardTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = HangRow();
		const FVector Tip(0.f, 0.f, 500.f);
		const float Length = 100.f;
		const double TopZ = Tip.Z - Length * FMath::Cos(FMath::DegreesToRadians(Row.HangMaxSwingDeg));

		// A hard kick straight up: without the guard the end flies over the tip; with it, it tops out at the swing limit.
		auto MaxRise = [&](float MaxSwingDeg, const FVector& Kick, double& OutMaxX) -> double
		{
			FLureLineSim Sim;
			Sim.Init(12);
			const FLureLineSimInput In = HangInput(Tip, Length, MaxSwingDeg);
			Sim.Reset(In);
			for (int32 Frame = 0; Frame < 60; ++Frame)
			{
				Sim.Step(FrameDt, In, Row);
			}
			Sim.AddEndVelocity(Kick);
			double MaxZ = -1.0e9;
			OutMaxX = 0.0;
			for (int32 Frame = 0; Frame < 180; ++Frame)
			{
				Sim.Step(FrameDt, In, Row);
				MaxZ = FMath::Max(MaxZ, Sim.GetEnd().Z);
				OutMaxX = FMath::Max(OutMaxX, FMath::Abs(Sim.GetEnd().X));
			}
			TestTrue(TEXT("the line stays finite"), Sim.IsFinite());
			TestTrue(TEXT("the end never stretches the line"), FVector::Dist(Sim.GetStart(), Sim.GetEnd()) <= Length * 1.005);
			return MaxZ;
		};

		double MaxX = 0.0;
		const double Unguarded = MaxRise(0.f, FVector(0.f, 0.f, 3000.f), MaxX);
		TestTrue(FString::Printf(TEXT("control: without the guard a kick throws the end over the tip (top %.1f, tip %.1f)"), Unguarded, Tip.Z), Unguarded > Tip.Z);
		const double Guarded = MaxRise(Row.HangMaxSwingDeg, FVector(0.f, 0.f, 3000.f), MaxX);
		TestTrue(FString::Printf(TEXT("with the guard it never rises over the swing limit (top %.1f, limit %.1f)"), Guarded, TopZ), Guarded <= TopZ + 2.0);
		AddInfo(FString::Printf(TEXT("SwingGuard: unguarded top %.1f, guarded top %.1f, limit %.1f"), Unguarded, Guarded, TopZ));

		// Sideways motion is untouched: a sideways kick still swings the end out.
		double Unguarded2X = 0.0;
		MaxRise(0.f, FVector(300.f, 0.f, 0.f), Unguarded2X);
		double GuardedX = 0.0;
		MaxRise(Row.HangMaxSwingDeg, FVector(300.f, 0.f, 0.f), GuardedX);
		TestTrue(FString::Printf(TEXT("a sideways kick still swings it out (%.1f cm, unguarded %.1f cm)"), GuardedX, Unguarded2X), GuardedX > 40.0 && GuardedX >= Unguarded2X - 3.0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineHangLandedTest, "Project.Fishing.Line.Hang.Component.LandedFishBelowTip", Flags)
	bool FLureLineHangLandedTest::RunTest(const FString& Parameters)
	{
		FHangWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		// Landing as in the game (ALureFishItem::SeatOnLine): no line out, the fish seated on a line from the rod tip to its
		// mouth 16 m out on the water, reeled to HangLength. The rod tip flicks up 60 cm in one frame as the rod unbends.
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Mouth(30.f, 0.f, 0.f);
		const float HangLength = 100.f;
		W.Line->SetWaterSurfaceZ(0.f);
		AActor* Fish = W.SpawnMovable(FVector(1600.f, 0.f, 0.f) - Mouth);
		W.Line->SetEndpoints(Tip, FVector(1600.f, 0.f, 0.f));
		W.Line->Hide();
		W.Line->AttachEndActor(Fish, HangLength, Mouth);
		TestEqual(TEXT("mode Hanging"), static_cast<int32>(W.Line->GetMode()), static_cast<int32>(ELureLineMode::Hanging));

		const FVector Flicked = Tip + FVector(0.f, 0.f, 60.f);
		double WorstAbove = -1.0e9; // highest end Z minus tip Z, over every frame
		int32 WorstFrame = -1;
		for (int32 Frame = 0; Frame < 840; ++Frame)
		{
			W.Line->SetEndpoints(Frame == 0 ? Tip : Flicked, FVector(1600.f, 0.f, 0.f));
			W.Tick(1);
			const double Above = W.Line->GetEndPoint().Z - W.Line->GetStartPoint().Z;
			if (Above > WorstAbove)
			{
				WorstAbove = Above;
				WorstFrame = Frame;
			}
		}
		TestTrue(FString::Printf(TEXT("the fish stays below the rod tip every frame (highest %.1f cm vs the tip, frame %d)"), WorstAbove, WorstFrame), WorstAbove < 0.0);
		const FVector End = W.Line->GetEndPoint();
		const FVector Expected = Flicked - FVector(0.f, 0.f, HangLength);
		TestTrue(FString::Printf(TEXT("after 14 s it hangs HangLength under the tip (end %s, expected %s)"), *End.ToString(), *Expected.ToString()), End.Equals(Expected, 4.0));
		TestNearlyEqual(TEXT("the line is reeled to HangLength"), W.Line->GetRestLength(), HangLength, 0.01f);
		AddInfo(FString::Printf(TEXT("LandedFishBelowTip: highest %.1f cm vs the tip at frame %d; end %s"), WorstAbove, WorstFrame, *End.ToString()));

		// For the record (not asserted): the same landing on the sim alone with the pre-T-032b hanging line (exponential
		// FollowRestLength, no swing guard) and with the current one (ReelInRestLength + HangMaxSwingDeg).
		const FLureFishingLineRow Row = HangRow();
		auto Highest = [&](bool bOldPath) -> double
		{
			FLureLineSim Sim;
			Sim.Init(12);
			FLureLineSimInput In;
			In.Start = Tip;
			In.End = FVector(1600.f, 0.f, 0.f);
			In.bFreeEnd = true;
			In.EndMass = Row.HangEndMass;
			In.EndDrag = Row.HangDrag;
			In.bHasWater = true;
			In.WaterZ = 0.f;
			In.Float = Row.FloatStrength;
			In.MaxSwingDeg = bOldPath ? 0.f : Row.HangMaxSwingDeg;
			float Rest = static_cast<float>(FVector::Dist(In.Start, In.End));
			In.RestLength = Rest;
			Sim.Reset(In);
			double Max = -1.0e9;
			for (int32 Frame = 0; Frame < 600; ++Frame)
			{
				In.Start = Frame == 0 ? Tip : Flicked;
				Rest = bOldPath ? FLureFishingLineRules::FollowRestLength(Rest, HangLength, 0.f, FrameDt, Row)
					: FLureFishingLineRules::ReelInRestLength(Rest, HangLength, FrameDt, Row);
				In.RestLength = Rest;
				Sim.Step(FrameDt, In, Row);
				Max = FMath::Max(Max, Sim.GetEnd().Z - Sim.GetStart().Z);
			}
			return Max;
		};
		AddInfo(FString::Printf(TEXT("LandedFishBelowTip sim replica: old path highest %.1f cm vs the tip, new path %.1f cm"), Highest(true), Highest(false)));
		W.Line->DetachEndActor();
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

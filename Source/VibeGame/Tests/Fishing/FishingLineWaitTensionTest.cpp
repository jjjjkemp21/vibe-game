// T-046 (unreal-engineer): after a cast the waiting line keeps some tension: it runs in a gentle, mostly lifted curve from the
// rod tip to the bobber instead of lying slack on the water. Jimmy (A2 playtest): "the fishing line has 0 tension and is laying
// on the water". Cause: DT_FishingLine WaitTension 0. Project.Fishing.Line.WaitTension.*
// Spec: docs/specs/fishing-line.md ("Tension, slack and length"). Tuning: the built-in row (= data/tables/DT_FishingLine.csv).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

namespace LureLineWaitTensionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameDt = 1.f / 60.f;

	/** The rod tip 150 cm above the water (a player standing on a low dock or in a boat). */
	constexpr float TipHeight = 150.f;
	/** The bobber's line attach point, just over the water. */
	constexpr float BobberTop = 3.f;
	/**
	 *  At most this share of the line's inner points lie on or under the water: mostly lifted. (Old WaitTension 0: 82-91 %. The
	 *  brief's example 25 % needs about tension 0.6 at 15 m, the bite's tension; WaitTension 0.55 measures 27 / 36 / 45 % at 10 /
	 *  12.5 / 15 m.)
	 */
	constexpr double MaxShareOnWater = 0.5;
	/** At most this sag below the straight rod-to-bobber line, as a share of that distance (0.55: ~4.7 %; old 0: ~9-12 %). */
	constexpr double MaxSagShare = 0.06;

	struct FResult
	{
		double ShareOnWater = 1.0;
		double Sag = 0.0;
		double Chord = 0.0;
		double EndDrift = 0.0;
	};

	/** A waiting line (pure line simulation in a test world, fixed dt, no mesh) with Tension, the bobber Distance out; settled 5 s. */
	bool Settle(FAutomationTestBase& Test, float Tension, float Distance, FResult& Out)
	{
		FTestWorldWrapper Wrapper;
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
		ULureFishingLineComponent* Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("WaitTensionTestLine"), RF_Transient);
		Line->SetupAttachment(Root);
		Line->RegisterComponent();
		Line->Setup(nullptr, nullptr, FLinearColor::White, 12); // DT_Fishing LineSegments
		const FLureFishingLineRow Row = FLureFishingLineRules::GetFallbackRow();
		Line->SetTuning(Row);
		Line->SetWaterSurfaceZ(0.f);

		const FVector Tip(0.f, 0.f, TipHeight);
		const FVector Bobber(Distance, 0.f, BobberTop);
		Line->SetTension(Tension);
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			Line->SetEndpoints(Tip, Bobber);
			Wrapper.TickTestWorld(FrameDt);
		}
		const TArray<FVector>& Points = Line->GetPoints();
		if (!Test.TestTrue(TEXT("the line is simulated"), Points.Num() >= 3))
		{
			return false;
		}
		int32 OnWater = 0;
		double Sag = 0.0;
		for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
		{
			if (Points[Index].Z <= Row.FloatHeight + 1.0)
			{
				++OnWater;
			}
			Sag = FMath::Max(Sag, static_cast<double>(FMath::PointDistToSegment(Points[Index], Tip, Bobber)));
		}
		Out.ShareOnWater = static_cast<double>(OnWater) / (Points.Num() - 2);
		Out.Sag = Sag;
		Out.Chord = FVector::Dist(Tip, Bobber);
		Out.EndDrift = FVector::Dist(Line->GetEndPoint(), Bobber);
		return true;
	}

	bool Lifted(const FResult& R)
	{
		return R.ShareOnWater <= MaxShareOnWater && R.Sag <= MaxSagShare * R.Chord;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaitLineLifted, "Project.Fishing.Line.WaitTension.LineLiftedToTheBobber", Flags)
	bool FWaitLineLifted::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = FLureFishingLineRules::GetFallbackRow();
		TestTrue(FString::Printf(TEXT("WaitTension %.2f is below BiteTension %.2f (a bite still reads as a change)"), Row.WaitTension, Row.BiteTension),
			Row.WaitTension < Row.BiteTension);
		TestTrue(FString::Printf(TEXT("WaitTension %.2f <= CastTension %.2f"), Row.WaitTension, Row.CastTension), Row.WaitTension <= Row.CastTension);

		for (const float Distance : {1000.f, 1250.f, 1500.f})
		{
			FResult R;
			if (!Settle(*this, Row.WaitTension, Distance, R))
			{
				return false;
			}
			TestTrue(FString::Printf(TEXT("%.0f m out, WaitTension %.2f: at most %.0f %% of the line on the water (%.0f %%)"), Distance / 100.f, Row.WaitTension,
				100.0 * MaxShareOnWater, 100.0 * R.ShareOnWater), R.ShareOnWater <= MaxShareOnWater);
			TestTrue(FString::Printf(TEXT("%.0f m out: sags at most %.0f %% of the distance (%.1f cm of %.0f cm)"), Distance / 100.f, 100.0 * MaxSagShare, R.Sag, R.Chord),
				R.Sag <= MaxSagShare * R.Chord);
			TestTrue(FString::Printf(TEXT("%.0f m out: the line does not move the bobber (its end stays %.3f cm from where it landed)"), Distance / 100.f, R.EndDrift),
				R.EndDrift <= 0.1);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaitLineSlackBefore, "Project.Fishing.Line.WaitTension.OldZeroTensionFails", Flags)
	bool FWaitLineSlackBefore::RunTest(const FString& Parameters)
	{
		// The check tells the two apart: the old WaitTension 0 lies on the water (Jimmy's bug).
		FResult R;
		if (!Settle(*this, 0.f, 1250.f, R))
		{
			return false;
		}
		TestFalse(FString::Printf(TEXT("WaitTension 0 fails the lifted-line check (%.0f %% on the water, sag %.1f cm)"), 100.0 * R.ShareOnWater, R.Sag), Lifted(R));

		// Numbers per tension (tuning aid; info only).
		for (const float Tension : {0.5f, 0.55f, 0.6f})
		{
			for (const float Distance : {1000.f, 1250.f, 1500.f})
			{
				FResult T;
				if (Settle(*this, Tension, Distance, T))
				{
					AddInfo(FString::Printf(TEXT("tension %.2f, %.0f m: %.0f %% on the water, sag %.1f cm (%.1f %%)"), Tension, Distance / 100.f,
						100.0 * T.ShareOnWater, T.Sag, 100.0 * T.Sag / T.Chord));
				}
			}
		}
		return true;
	}
}

#endif

// T-041b (unreal-engineer): the line of a hanging fish ends at its mouth; nothing of it is drawn past the mouth into or
// through the fish. Project.Fishing.Line.Hang.NoStub*  Spec: docs/specs/fishing-line.md ("Hanging actor").

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureFishItem.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SplineMeshComponent.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/PlayerController.h"

namespace LureLineHangNoStubTest
{
	/** How far P lies past the mouth, measured from the mouth along the fish's body (away from the rod), cm */
	double PastMouth(const FVector& P, const FVector& Mouth, const FVector& IntoFish)
	{
		return FVector::DotProduct(P - Mouth, IntoFish);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHangNoStubPastMouth, "Project.Fishing.Line.Hang.NoStubPastMouth", LCT::Flags)
	bool FHangNoStubPastMouth::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(-500.0f, 0.0f, LCT::DockTop));
		ULureFishingComponent* Fishing = Player ? Player->GetFishing() : nullptr;
		APlayerController* PC = LCT::ControllerOf(Player);
		if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("fishing"), Fishing) || !TestNotNull(TEXT("controller"), PC))
		{
			return false;
		}
		W.Tick(20);
		TestTrue(TEXT("cast out to sea"), Fishing->AuthorityCast(0.6f, 180.0f));
		W.Tick(120);
		Fishing->AuthorityReelIn();
		W.Tick(10);
		ALureFishItem* Item = W.Land(Player, LCT::MakeFish(TEXT("Bonefish"), 15, 6, 2.28f, 3));
		const ULureFishingLineComponent* Line = Fishing->GetLine();
		if (!TestNotNull(TEXT("a fish on the hook"), Item) || !TestTrue(TEXT("the rod's physics line holds it"), Line && Line->GetEndActor() == Item))
		{
			return false;
		}

		// 6 s: the reel-up and swing, then the view swept left and right (the rod tip moves, the fish swings behind it).
		double WorstPoint = -1.0e9;
		double WorstDrawn = -1.0e9;
		int32 WorstFrame = -1;
		int32 Checked = 0;
		for (int32 Frame = 0; Frame < 360; ++Frame)
		{
			if (Frame >= 180)
			{
				const float Yaw = 180.0f + 40.0f * FMath::Sin(2.0f * UE_PI * (Frame - 180) / 90.0f);
				PC->SetControlRotation(FRotator(-10.0f, Yaw, 0.0f));
			}
			W.Tick(1);
			const FVector Mouth = Item->GetActorTransform().TransformPosition(Item->GetMouthOffset());
			const FVector IntoFish = -Line->GetEndDirection();
			const TArray<FVector>& Points = Line->GetPoints();
			for (const FVector& P : Points)
			{
				const double Past = PastMouth(P, Mouth, IntoFish);
				if (Past > WorstPoint)
				{
					WorstPoint = Past;
					WorstFrame = Frame;
				}
			}
			// The drawn curves (each segment's Hermite spline, sampled), not only the points.
			TInlineComponentArray<USplineMeshComponent*> Segments;
			Player->GetComponents(Segments);
			for (const USplineMeshComponent* Segment : Segments)
			{
				if (!Segment->IsVisible())
				{
					continue;
				}
				const FTransform& ToWorld = Segment->GetComponentTransform();
				for (int32 Step = 0; Step <= 16; ++Step)
				{
					const FVector Local = FMath::CubicInterp(Segment->GetStartPosition(), Segment->GetStartTangent(), Segment->GetEndPosition(),
						Segment->GetEndTangent(), Step / 16.0f);
					WorstDrawn = FMath::Max(WorstDrawn, PastMouth(ToWorld.TransformPosition(Local), Mouth, IntoFish));
				}
				++Checked;
			}
		}
		TestTrue(FString::Printf(TEXT("no line point lies past the mouth into the fish by more than 1 cm (worst %.3f cm, frame %d)"), WorstPoint, WorstFrame),
			WorstPoint <= 1.0);
		TestTrue(FString::Printf(TEXT("no drawn segment reaches past the mouth by more than 1 cm (worst %.3f cm, %d segment-frames)"), WorstDrawn, Checked),
			Checked > 0 && WorstDrawn <= 1.0);
		AddInfo(FString::Printf(TEXT("NoStubPastMouth: points worst %.3f cm, drawn worst %.3f cm past the mouth"), WorstPoint, WorstDrawn));
		return true;
	}
}

#endif

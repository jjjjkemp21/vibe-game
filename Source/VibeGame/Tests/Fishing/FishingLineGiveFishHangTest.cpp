// T-034 (unreal-engineer): a fish landed with no line out (Lure.GiveFish) hangs from a line as thin as a real catch's line.
// The cause was a stale viewer: the rod's line kept the eye position of the last frame it was out, so after walking away its
// widths were sized for a viewer metres off (a 10 m walk: ~2.6 cm instead of ~0.3 cm).
// Project.Fishing.Line.GiveFishHang.*  Spec: docs/specs/fishing-line.md ("Hanging actor").

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Catch/LureCatchLibrary.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/PlayerController.h"

namespace LureGiveFishHangTest
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGiveFishHangWidth, "Project.Fishing.Line.GiveFishHang.WidthMatchesRule", LCT::Flags)
	bool FGiveFishHangWidth::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		// Cast from the dock's -X end out to sea, then reel in: the rod's line was out (and drawn for the eye there).
		ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(-500.0f, 0.0f, LCT::DockTop));
		ULureFishingComponent* Fishing = Player ? Player->GetFishing() : nullptr;
		if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("fishing"), Fishing))
		{
			return false;
		}
		W.Tick(20);
		TestTrue(TEXT("cast out to sea"), Fishing->AuthorityCast(0.6f, 180.0f));
		W.Tick(120);
		TestTrue(TEXT("... the rod's line is out and drawn"), Fishing->GetLine() && Fishing->GetLine()->IsLineVisible());
		Fishing->AuthorityReelIn();
		W.Tick(10);
		TestEqual(TEXT("reeled in"), static_cast<int32>(Fishing->GetFishingState()), static_cast<int32>(ELureFishingState::Idle));

		// Walk 10 m away, then GiveFish's path: HandleFishLanded with no line out.
		Player->SetActorLocation(Player->GetActorLocation() + FVector(1000.0f, 0.0f, 0.0f));
		W.Tick(20);
		ALureFishItem* Item = W.Land(Player, LCT::MakeFish(TEXT("Bonefish"), 15, 6, 2.28f, 3));
		if (!TestNotNull(TEXT("a fish on the hook"), Item))
		{
			return false;
		}
		W.Tick(30);

		const ULureFishingLineComponent* Line = Fishing->GetLine();
		if (!TestTrue(TEXT("the rod's physics line holds the fish (T-032)"), Line && Line->GetEndActor() == Item))
		{
			return false;
		}
		const TArray<FVector>& Points = Line->GetPoints();
		const TArray<float>& Widths = Line->GetWidths();
		TestTrue(TEXT("the line is drawn"), Line->IsLineVisible() && Points.Num() >= 2 && Widths.Num() == Points.Num());

		// The viewer now: the player's eye (the camera this frame), as for a line that is out.
		const FVector Viewer = Player->GetFirstPersonCamera()->GetComponentLocation();
		float Fov = 90.0f;
		const APlayerController* PC = LCT::ControllerOf(Player);
		if (PC && PC->PlayerCameraManager)
		{
			Fov = PC->PlayerCameraManager->GetCameraCacheView().FOV;
		}
		const FLureFishingRow& Row = Fishing->GetProfile();
		const float RefWidth = GetDefault<ULureFishingSettings>()->LineReferenceScreenWidth;
		for (int32 Index = 0; Index < Points.Num() && Index < Widths.Num(); ++Index)
		{
			const float Expected = FLureFishingRules::LineWidthAtDistance(Row.LinePixelWidth, static_cast<float>(FVector::Dist(Viewer, Points[Index])), Fov, RefWidth, Row.LineMinWidth);
			TestTrue(FString::Printf(TEXT("point %d: width %.3f cm = the width rule %.3f cm (10 %%)"), Index, Widths[Index], Expected),
				FMath::Abs(Widths[Index] - Expected) <= 0.1f * Expected);
		}

		// What is drawn: each segment's cross-section (spline scale x the mesh's diameter x the component scale) is that width.
		TInlineComponentArray<ULureLineSegmentComponent*> Segments;
		Line->GetOwner()->GetComponents(Segments);
		int32 Visible = 0;
		for (const ULureLineSegmentComponent* Segment : Segments)
		{
			if (!Segment->IsVisible() || !Segment->GetStaticMesh())
			{
				continue;
			}
			++Visible;
			const FBoxSphereBounds MeshBounds = Segment->GetStaticMesh()->GetBounds();
			const double Diameter = 2.0 * FMath::Max(MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y);
			const FVector Scale = Segment->GetComponentScale();
			const double Drawn = Segment->GetStartScale().X * Diameter * FMath::Max(Scale.X, Scale.Y);
			const FVector Start = Segment->GetComponentTransform().TransformPosition(Segment->GetStartPosition());
			float Nearest = MAX_flt;
			float WidthThere = 0.f;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const float D = static_cast<float>(FVector::Dist(Points[Index], Start));
				if (D < Nearest)
				{
					Nearest = D;
					WidthThere = Widths[Index];
				}
			}
			TestTrue(FString::Printf(TEXT("%s is drawn %.3f cm thick = its point's width %.3f cm (10 %%)"), *Segment->GetName(), Drawn, WidthThere),
				FMath::Abs(Drawn - WidthThere) <= 0.1 * WidthThere);
		}
		TestTrue(FString::Printf(TEXT("segments drawn (%d)"), Visible), Visible > 0);
		return true;
	}
}

#endif

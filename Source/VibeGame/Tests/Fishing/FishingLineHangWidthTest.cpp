// T-044 (unreal-engineer): a landed fish dangling on the rod's line for a minute keeps a thin line. Jimmy (A2 playtest,
// build ceb4ac7): "the longer it dangles, the thicker the fishing line gets". A real catch (cast, reel fight, land), then 60 s
// of fixed-dt frames while the player walks along the dock and looks around: every drawn point's width stays the width rule
// for the eye this frame (FLureFishingRules::LineWidthAtDistance, 10 %) and the line does not get thicker over time.
// Project.Fishing.Line.Hang.WidthOverTime  Spec: docs/specs/fishing-line.md ("Hanging actor").

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/FishVisual/FightFishVisualQATestUtils.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Catch/LureCatchLinkSubsystem.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/StaticMesh.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/PlayerController.h"

namespace LureLineHangWidthTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHangWidthOverTime, "Project.Fishing.Line.Hang.WidthOverTime", Flags)
	bool FHangWidthOverTime::RunTest(const FString& Parameters)
	{
		// The T-029 real-fight scene (shipped fight/fish tables) plus the shipped DT_Catch row, as Project.Catch.Link.* uses.
		LureFightFishQA::FRealScene S;
		TStrongObjectPtr<UDataTable> CatchTable;
		FString CatchText;
		if (!S.Init(*this) || !LureFightQA::ReadSource(*this, TEXT("DT_Catch.csv"), CatchText)
			|| !LureFightQA::MakeTableChecked(*this, CatchTable, FLureCatchRow::StaticStruct(), CatchText, false, TEXT("DT_Catch.csv")))
		{
			return false;
		}
		const FLureCatchRow* CatchRow = CatchTable->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("HangWidthTest"), false);
		ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(S.World.World);
		ULureHandsComponent* Hands = ULureHandsComponent::Get(S.Character);
		if (!TestNotNull(TEXT("DT_Catch.csv Default row"), CatchRow) || !TestNotNull(TEXT("catch subsystem"), Catch) || !TestNotNull(TEXT("hands"), Hands))
		{
			return false;
		}
		Catch->SetTuning(*CatchRow);
		Catch->SetFreshnessTable(nullptr);
		Catch->SetFishTables(S.Fish.Get());

		// A local player looks through the character's camera (the eye the line is sized for).
		APlayerController* PC = S.World.World->SpawnActor<APlayerController>();
		if (!TestNotNull(TEXT("player controller"), PC))
		{
			return false;
		}
		PC->SetAsLocalPlayerController();
		PC->Possess(S.Character);
		S.World.Tick(10);

		// The real catch: cast, hook, reel the fight in until the fish is landed.
		if (!LureFightQA::CastAndWait(*this, S.World, S.Fishing) || !TestTrue(TEXT("hooked"), S.Fishing->AuthorityHookFish(S.Bonefish)))
		{
			return false;
		}
		S.Fishing->AuthoritySetReeling(true);
		for (int32 Frame = 0; Frame < 40 * 60 && S.Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			S.World.Tick(1);
		}
		S.Fishing->AuthoritySetReeling(false);
		if (!TestEqual(TEXT("the fight is landed"), LureFightQA::ResultName(S.Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed)))
		{
			return false;
		}
		ALureFishItem* Item = Hands->GetHangingFish();
		const ULureFishingLineComponent* Line = S.Fishing->GetLine();
		if (!TestNotNull(TEXT("a fish hangs on the hook"), Item) || !TestTrue(TEXT("the rod's physics line holds it"), Line && Line->GetEndActor() == Item))
		{
			return false;
		}

		const FLureFishingRow& Row = S.Fishing->GetProfile();
		const float RefWidth = GetDefault<ULureFishingSettings>()->LineReferenceScreenWidth;
		const float Dt = 1.0f / 60.0f;
		const int32 Frames = 60 * 60;
		const int32 Settle = 60; // the reel-up after landing
		double WorstRatio = 0.0;
		int32 WorstFrame = -1;
		float WorstWidth = 0.f;
		float WorstExpected = 0.f;
		double WorstDrawnRatio = 0.0;
		int32 DrawnChecked = 0;
		double FirstSecondMax = 0.0;
		double LastSecondMax = 0.0;
		float Fov = 90.f;
		FVector PrevViewer = S.Character->GetFirstPersonCamera()->GetComponentLocation();
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			// Walk slowly along the dock toward -X (9 m in the minute) and look around.
			S.Character->SetActorLocation(S.Character->GetActorLocation() + FVector(-15.0f * Dt, 0.0, 0.0));
			const float Yaw = 180.0f + 60.0f * FMath::Sin(2.0f * UE_PI * Frame / 600.0f);
			PC->SetControlRotation(FRotator(-10.0f, Yaw, 0.0f));
			S.World.Tick(1, Dt);
			if (Line->GetEndActor() != Item)
			{
				AddError(FString::Printf(TEXT("the fish stopped hanging on the line at frame %d"), Frame));
				return false;
			}
			// The eye the line is sized for: the camera this frame, or where it was a frame ago (the fishing step may read the camera
			// before this frame's look moved it; ~1 m from the eye a few cm of camera motion is several % of the width).
			const FVector Viewer = S.Character->GetFirstPersonCamera()->GetComponentLocation();
			const FVector LastViewer = PrevViewer;
			PrevViewer = Viewer;
			if (PC->PlayerCameraManager)
			{
				Fov = PC->PlayerCameraManager->GetCameraCacheView().FOV;
			}
			if (Frame < Settle)
			{
				continue;
			}
			const TArray<FVector>& Points = Line->GetPoints();
			const TArray<float>& Widths = Line->GetWidths();
			if (!TestTrue(TEXT("the line is drawn"), Line->IsLineVisible() && Points.Num() >= 2 && Widths.Num() == Points.Num()))
			{
				return false;
			}
			double MaxWidth = 0.0;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const float Expected = FLureFishingRules::LineWidthAtDistance(Row.LinePixelWidth, static_cast<float>(FVector::Dist(Viewer, Points[Index])), Fov, RefWidth, Row.LineMinWidth);
				const float ExpectedLast = FLureFishingRules::LineWidthAtDistance(Row.LinePixelWidth, static_cast<float>(FVector::Dist(LastViewer, Points[Index])), Fov, RefWidth, Row.LineMinWidth);
				const double Ratio = FMath::Min(FMath::Abs(Widths[Index] - Expected) / FMath::Max(1.0e-4f, Expected),
					FMath::Abs(Widths[Index] - ExpectedLast) / FMath::Max(1.0e-4f, ExpectedLast));
				if (Ratio > WorstRatio)
				{
					WorstRatio = Ratio;
					WorstFrame = Frame;
					WorstWidth = Widths[Index];
					WorstExpected = Expected;
				}
				MaxWidth = FMath::Max(MaxWidth, static_cast<double>(Widths[Index]));
			}
			if (Frame < Settle + 60)
			{
				FirstSecondMax = FMath::Max(FirstSecondMax, MaxWidth);
			}
			if (Frame >= Frames - 60)
			{
				LastSecondMax = FMath::Max(LastSecondMax, MaxWidth);
			}

			// What is drawn: each visible segment's start cross-section = the width at its start point.
			TInlineComponentArray<ULureLineSegmentComponent*> Segments;
			S.Character->GetComponents(Segments);
			for (const ULureLineSegmentComponent* Segment : Segments)
			{
				if (!Segment->IsVisible() || !Segment->GetStaticMesh())
				{
					continue;
				}
				const FBoxSphereBounds MeshBounds = Segment->GetStaticMesh()->GetBounds();
				const double Diameter = 2.0 * FMath::Max(MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y);
				const FVector Scale = Segment->GetComponentScale();
				const double Drawn = Segment->GetStartScale().X * Diameter * FMath::Max(Scale.X, Scale.Y);
				const FVector Start = Segment->GetComponentTransform().TransformPosition(Segment->GetStartPosition());
				double Nearest = MAX_dbl;
				float WidthThere = 0.f;
				for (int32 Index = 0; Index < Points.Num(); ++Index)
				{
					const double D = FVector::Dist(Points[Index], Start);
					if (D < Nearest)
					{
						Nearest = D;
						WidthThere = Widths[Index];
					}
				}
				WorstDrawnRatio = FMath::Max(WorstDrawnRatio, FMath::Abs(Drawn - WidthThere) / FMath::Max(1.0e-4f, WidthThere));
				++DrawnChecked;
			}
		}

		TestTrue(FString::Printf(TEXT("every point's width is the width rule for the eye (this or the last frame's camera) within 10 %% (worst %.1f %%: %.3f cm vs %.3f cm, frame %d)"),
			100.0 * WorstRatio, WorstWidth, WorstExpected, WorstFrame), WorstFrame < 0 || WorstRatio <= 0.1);
		TestTrue(FString::Printf(TEXT("every drawn segment is its point's width within 10 %% (worst %.1f %%, %d segment-frames)"), 100.0 * WorstDrawnRatio, DrawnChecked),
			DrawnChecked > 0 && WorstDrawnRatio <= 0.1);
		TestTrue(FString::Printf(TEXT("the line does not get thicker over the minute (widest %.3f cm in the first second, %.3f cm in the last)"), FirstSecondMax, LastSecondMax),
			FirstSecondMax > 0.0 && LastSecondMax <= 1.1 * FirstSecondMax);
		AddInfo(FString::Printf(TEXT("WidthOverTime: worst rule error %.2f %% (frame %d), drawn error %.2f %%, widest %.3f -> %.3f cm"),
			100.0 * WorstRatio, WorstFrame, 100.0 * WorstDrawnRatio, FirstSecondMax, LastSecondMax));
		PC->UnPossess();
		return true;
	}
}

#endif

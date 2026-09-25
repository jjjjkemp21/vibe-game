// T-043 (unreal-engineer): a fish hanging on the rod's physics line turns about the line so its side faces the viewer's camera
// (smoothed by DT_FishingLine HangFaceTime), the same rule as the fallback pendulum; its mouth stays on the line's end.
// Project.Fishing.Line.Hang.SideOn.*  Spec: docs/specs/fishing-line.md ("Hanging actor").

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Catch/LureFishItem.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include <limits>

namespace LureLineHangSideOnTest
{
	constexpr float FrameDt = 1.0f / 60.0f;

	/** Degrees between the fish's side (+Y) and the direction from its mouth to the viewer, both square to the line (Up). */
	double SideErrorDeg(const FQuat& Fish, const FVector& Up, const FVector& Mouth, const FVector& Viewer)
	{
		const FVector Axis = Up.GetSafeNormal();
		FVector Side = Fish.GetAxisY();
		Side -= Axis * FVector::DotProduct(Side, Axis);
		FVector ToViewer = Viewer - Mouth;
		ToViewer -= Axis * FVector::DotProduct(ToViewer, Axis);
		if (!Side.Normalize() || !ToViewer.Normalize())
		{
			return 180.0;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Side, ToViewer), -1.0, 1.0)));
	}

	/** Degrees between the fish's nose (+X) and the line's direction up from the end. */
	double NoseErrorDeg(const FQuat& Fish, const FVector& Up)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Fish.GetAxisX(), Up.GetSafeNormal()), -1.0, 1.0)));
	}

	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSideOnRule, "Project.Fishing.Line.Hang.SideOn.Rule", LCT::Flags)
	bool FSideOnRule::RunTest(const FString& Parameters)
	{
		const FVector Up = FVector::UpVector;
		const FVector Hang(0.0, 0.0, 100.0);
		const FVector Viewer(0.0, 300.0, 150.0); // on the +Y side
		const FQuat Facing = FRotationMatrix::MakeFromXY(Up, FVector::ForwardVector).ToQuat(); // side (+Y) toward +X: 90 deg off

		const FQuat Snap = FLureFishingLineRules::SideOnHangRotation(Facing, Up, Hang, Viewer, FrameDt, 0.0f);
		TestTrue(TEXT("FaceTime 0: side-on at once"), SideErrorDeg(Snap, Up, Hang, Viewer) < 0.01);
		TestTrue(TEXT("the nose points up the line"), NoseErrorDeg(Snap, Up) < 0.01);

		// Smoothed: 1 - exp(-dt / FaceTime) of the angle per call.
		const float FaceTime = 0.25f;
		const FQuat Step = FLureFishingLineRules::SideOnHangRotation(Facing, Up, Hang, Viewer, FrameDt, FaceTime);
		const double Expected = 90.0 * FMath::Exp(-FrameDt / FaceTime);
		TestNearlyEqual(TEXT("one frame closes 1 - exp(-dt / FaceTime) of the angle"), SideErrorDeg(Step, Up, Hang, Viewer), Expected, 0.01);
		FQuat Settled = Facing;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			Settled = FLureFishingLineRules::SideOnHangRotation(Settled, Up, Hang, Viewer, FrameDt, FaceTime);
		}
		TestTrue(TEXT("settled after 3 s (< 0.1 deg, 12 time constants)"), SideErrorDeg(Settled, Up, Hang, Viewer) < 0.1);

		// No time: no turn (the nose still goes up the line). The viewer on the line's axis: the side it had.
		const FQuat Tilted = FRotationMatrix::MakeFromXY(FVector(0.3, 0.0, 1.0).GetSafeNormal(), FVector::ForwardVector).ToQuat();
		const FQuat NoTime = FLureFishingLineRules::SideOnHangRotation(Tilted, Up, Hang, Viewer, 0.0f, FaceTime);
		TestTrue(TEXT("dt 0: nose up the line"), NoseErrorDeg(NoTime, Up) < 0.01);
		TestTrue(TEXT("dt 0: the side is not turned"), FVector::DotProduct(NoTime.GetAxisY(), FVector::ForwardVector) > 0.999);
		const FQuat OnAxis = FLureFishingLineRules::SideOnHangRotation(Facing, Up, Hang, Hang + FVector(0.0, 0.0, 500.0), FrameDt, 0.0f);
		TestTrue(TEXT("viewer on the line's axis: keeps its side"), FVector::DotProduct(OnAxis.GetAxisY(), Facing.GetAxisY()) > 0.999);

		// Bad input: always a finite rotation.
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const FQuat Bad = FLureFishingLineRules::SideOnHangRotation(Facing, FVector(NaN), FVector(NaN), FVector(NaN), NaN, NaN);
		TestFalse(TEXT("NaN inputs: finite"), Bad.ContainsNaN());
		const FQuat Zero = FLureFishingLineRules::SideOnHangRotation(Facing, FVector::ZeroVector, Hang, Viewer, FrameDt, 0.0f);
		TestTrue(TEXT("zero Up: +Z"), NoseErrorDeg(Zero, FVector::UpVector) < 0.01);
		return true;
	}

	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSideOnComponentViewers, "Project.Fishing.Line.Hang.SideOn.ComponentViewers", LCT::Flags)
	bool FSideOnComponentViewers::RunTest(const FString& Parameters)
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(this);
			return false;
		}
		UWorld* World = Wrapper.GetTestWorld();
		auto Spawn = [World](const FVector& Location, const FRotator& Rotation)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation, Location), Params);
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Root->SetMobility(EComponentMobility::Movable);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocationAndRotation(Location, Rotation);
			return Actor;
		};
		AActor* Owner = Spawn(FVector::ZeroVector, FRotator::ZeroRotator);
		ULureFishingLineComponent* Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("SideOnLine"), RF_Transient);
		Line->SetupAttachment(Owner->GetRootComponent());
		Line->RegisterComponent();
		Line->Setup(nullptr, nullptr, FLinearColor::White, 12);
		Line->SetTuning(FLureFishingLineRules::GetFallbackRow());

		const FVector Tip(0.0, 0.0, 300.0);
		const FVector Mouth(20.0, 0.0, 0.0); // actor space: the nose, 20 cm ahead of the pivot
		AActor* Fish = Spawn(Tip - FVector(0.0, 0.0, 120.0), FRotator(0.0f, 0.0f, 0.0f));
		Line->SetEndpoints(Tip, Tip - FVector(0.0, 0.0, 100.0));
		Line->AttachEndActor(Fish, 100.0f, Mouth, /*bOrientAlongLine*/ true, /*bFaceViewer*/ true);

		// Eight viewers around the fish at eye height (and one high above it), 2 s each.
		double WorstSide = 0.0;
		double WorstMouth = 0.0;
		for (int32 Spot = 0; Spot < 9; ++Spot)
		{
			const double Yaw = Spot * 45.0 + 10.0;
			const FVector Viewer = Spot < 8 ? Tip + FVector(FMath::Cos(FMath::DegreesToRadians(Yaw)) * 400.0, FMath::Sin(FMath::DegreesToRadians(Yaw)) * 400.0, -120.0)
				: Tip + FVector(60.0, 30.0, 400.0);
			double FirstFrameError = -1.0;
			double BeforeError = -1.0;
			for (int32 Frame = 0; Frame < 120; ++Frame)
			{
				if (Frame == 0)
				{
					BeforeError = SideErrorDeg(Fish->GetActorQuat(), Line->GetEndDirection(), Line->GetEndPoint(), Viewer);
				}
				Line->SetEndpoints(Tip, Tip - FVector(0.0, 0.0, 100.0)); // the owner keeps feeding its ends (they wait while an actor hangs)
				Line->SetViewer(Viewer, 90.0f);
				Line->UpdateLine(FrameDt);
				const FVector MouthWorld = Fish->GetActorTransform().TransformPosition(Mouth);
				WorstMouth = FMath::Max(WorstMouth, FVector::Dist(MouthWorld, Line->GetEndPoint()));
				if (Frame == 0)
				{
					FirstFrameError = SideErrorDeg(Fish->GetActorQuat(), Line->GetEndDirection(), Line->GetEndPoint(), Viewer);
				}
			}
			const double Error = SideErrorDeg(Fish->GetActorQuat(), Line->GetEndDirection(), Line->GetEndPoint(), Viewer);
			WorstSide = FMath::Max(WorstSide, Error);
			TestTrue(FString::Printf(TEXT("viewer %d: side-on within 3 deg after 2 s (%.2f deg)"), Spot, Error), Error <= 3.0);
			if (BeforeError > 20.0)
			{
				TestTrue(FString::Printf(TEXT("viewer %d: turns smoothly, not at once (%.1f -> %.1f deg in one frame)"), Spot, BeforeError, FirstFrameError),
					FirstFrameError > 0.5 * BeforeError);
			}
			TestTrue(FString::Printf(TEXT("viewer %d: the nose points up the line"), Spot), NoseErrorDeg(Fish->GetActorQuat(), Line->GetEndDirection()) < 0.5);
		}
		TestTrue(FString::Printf(TEXT("the mouth stays on the line's end (worst %.4f cm)"), WorstMouth), WorstMouth <= 1.0);

		// Without bFaceViewer (the old attach): the side stays where it was.
		const FQuat Before = Fish->GetActorQuat();
		Line->DetachEndActor();
		Line->AttachEndActor(Fish, 100.0f, Mouth, /*bOrientAlongLine*/ true);
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Line->SetViewer(Tip + FVector(-400.0, 0.0, -120.0), 90.0f);
			Line->UpdateLine(FrameDt);
		}
		TestTrue(TEXT("without bFaceViewer the actor keeps its side"), FVector::DotProduct(Fish->GetActorQuat().GetAxisY(), Before.GetAxisY()) > 0.99);
		AddInfo(FString::Printf(TEXT("SideOn.ComponentViewers: worst side error %.2f deg after 2 s, mouth worst %.4f cm"), WorstSide, WorstMouth));
		return true;
	}

	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSideOnRealCatch, "Project.Fishing.Line.Hang.SideOn.RealCatch", LCT::Flags)
	bool FSideOnRealCatch::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		// The viewer is this machine's player (spawned first: the local camera); the angler is another player, so the viewer
		// can stand anywhere around the fish.
		ALurePlayerCharacter* Viewer = W.SpawnPlayer(*this, FVector(-200.0f, 0.0f, LCT::DockTop));
		ALurePlayerCharacter* Angler = W.SpawnPlayer(*this, FVector(-500.0f, 0.0f, LCT::DockTop), /*bLocal*/ false);
		ULureFishingComponent* Fishing = Angler ? Angler->GetFishing() : nullptr;
		APlayerController* ViewerPC = LCT::ControllerOf(Viewer);
		if (!TestNotNull(TEXT("viewer"), Viewer) || !TestNotNull(TEXT("angler fishing"), Fishing) || !TestNotNull(TEXT("viewer controller"), ViewerPC)
			|| !TestTrue(TEXT("the viewer's controller is the local one"), W.World->GetFirstPlayerController() == ViewerPC))
		{
			return false;
		}
		LCT::PlaceAt(Angler, FVector(-500.0f, 0.0f, LCT::DockTop), 180.0f);
		W.Tick(20);
		TestTrue(TEXT("cast out to sea"), Fishing->AuthorityCast(0.6f, 180.0f));
		W.Tick(120);
		Fishing->AuthorityReelIn();
		W.Tick(10);
		ALureFishItem* Item = W.Land(Angler, LCT::MakeFish(TEXT("Bonefish"), 15, 6, 2.28f, 3));
		const ULureFishingLineComponent* Line = Fishing->GetLine();
		if (!TestNotNull(TEXT("a fish on the hook"), Item) || !TestTrue(TEXT("the rod's physics line holds it"), Line && Line->GetEndActor() == Item))
		{
			return false;
		}
		W.Tick(240); // the reel-up (~3.6 s) and most of the swing

		// Viewer spots on the dock around the hanging fish: behind the angler, both sides, and a diagonal.
		const FVector Spots[] = { FVector(-250.0f, 0.0f, LCT::DockTop), FVector(-520.0f, 280.0f, LCT::DockTop),
			FVector(-520.0f, -280.0f, LCT::DockTop), FVector(-300.0f, -220.0f, LCT::DockTop) };
		double WorstMouth = 0.0;
		double WorstSide = 0.0;
		for (int32 Spot = 0; Spot < UE_ARRAY_COUNT(Spots); ++Spot)
		{
			LCT::PlaceAt(Viewer, Spots[Spot], 0.0f);
			const FVector FishNow = Line->GetEndPoint();
			double SpotWorst = 0.0;
			for (int32 Frame = 0; Frame < 180; ++Frame)
			{
				LCT::LookAt(Viewer, FishNow);
				W.Tick(1);
				const FVector Mouth = Item->GetActorTransform().TransformPosition(Item->GetMouthOffset());
				WorstMouth = FMath::Max(WorstMouth, FVector::Dist(Mouth, Line->GetEndPoint()));
				if (Frame >= 150) // settled: the last 0.5 s at this spot
				{
					const FVector Eye = ViewerPC->PlayerCameraManager ? ViewerPC->PlayerCameraManager->GetCameraCacheView().Location
						: Viewer->GetFirstPersonCamera()->GetComponentLocation();
					SpotWorst = FMath::Max(SpotWorst, SideErrorDeg(Item->GetActorQuat(), Line->GetEndDirection(), Line->GetEndPoint(), Eye));
				}
			}
			WorstSide = FMath::Max(WorstSide, SpotWorst);
			TestTrue(FString::Printf(TEXT("spot %d: the fish's side faces the viewer within 5 deg (worst %.2f deg)"), Spot, SpotWorst), SpotWorst <= 5.0);
		}
		TestTrue(FString::Printf(TEXT("the mouth stays within 1 cm of the line's end (worst %.4f cm)"), WorstMouth), WorstMouth <= 1.0);
		AddInfo(FString::Printf(TEXT("SideOn.RealCatch: worst side error %.2f deg (settled), mouth worst %.4f cm"), WorstSide, WorstMouth));
		return true;
	}
}

#endif

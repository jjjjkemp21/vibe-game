// Lure T-004 QA (qa-engineer): eye height / camera transitions and the procedural arms bob.
// Project.Movement.QA.Eye.*, .ArmsBob.*  (QA design group E; bob cases from art/export/Characters/SK_FPArms.anim.md)
// Fixture A eye heights (from the feet): Stand 158, Sprint 158, Crouch 88, Prone 33; TransitionTime of the TARGET row (A8): 0.20 / 0.25 / 0.40.

#include "Tests/Movement/QAMovementTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/LureArmsBob.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

namespace QAMovementEye
{
	struct FSeries
	{
		float StartEye = 0.f;
		float TargetEye = 0.f;
		float TransitionTime = 0.f;
		TArray<float> Eye;		// camera above the feet, one sample per frame (index 0 = before the request)
		TArray<float> CameraZ;	// camera world Z, same frames
	};

	/** Spawns (Table), settles in From, requests To and records Frames frames at DeltaTime. */
	bool Record(FAutomationTestBase& Test, const UDataTable* Table, ELureStance From, ELureStance To, int32 Frames, float DeltaTime, FSeries& Out,
		TFunction<void(QAM::FWorld&, ALurePlayerCharacter*, int32)> PerFrame = nullptr)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = World.Spawn(Test, FVector::ZeroVector, Table);
		if (!Character)
		{
			return false;
		}
		World.Tick(QAM::SettleFrames);
		if (From != ELureStance::Stand && !Test.TestTrue(TEXT("reached the start stance"), QAM::EnterStance(World, Character, From, 60)))
		{
			return false;
		}
		World.Tick(30);
		const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Out.StartEye = QAM::EyeAboveFeet(Character);
		Out.TargetEye = Movement->GetMovementRow(QAM::StateOf(To)).EyeHeight;
		Out.TransitionTime = Movement->GetMovementRow(QAM::StateOf(To)).TransitionTime;
		Out.Eye.Add(Out.StartEye);
		Out.CameraZ.Add(QAM::CameraZ(Character));
		Character->RequestStance(To);
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			World.Tick(1, DeltaTime);
			Out.Eye.Add(QAM::EyeAboveFeet(Character));
			Out.CameraZ.Add(QAM::CameraZ(Character));
			if (PerFrame)
			{
				PerFrame(World, Character, Frame);
			}
		}
		return true;
	}

	struct FCase
	{
		ELureStance From;
		ELureStance To;
	};
	const FCase Cases[] = {
		{ ELureStance::Stand, ELureStance::Crouch },
		{ ELureStance::Crouch, ELureStance::Prone },
		{ ELureStance::Prone, ELureStance::Stand },
		{ ELureStance::Stand, ELureStance::Prone },
	};

	FString CaseName(const FCase& Case)
	{
		return QAM::StanceName(Case.From) + TEXT("->") + QAM::StanceName(Case.To);
	}

	int32 FramesFor(float Seconds, float DeltaTime)
	{
		return FMath::Max(1, FMath::RoundToInt(Seconds / DeltaTime));
	}
}

// =====================================================================================================================
// E: eye height and camera transitions
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeSettledEyePerStance, "Project.Movement.QA.Eye.SettledEyePerStance", QAMovement::Flags)
bool FQAMoveEyeSettledEyePerStance::RunTest(const FString& Parameters)
{
	const TPair<ELureMovementState, float> Cases[] = {
		{ ELureMovementState::Stand, 158.f }, { ELureMovementState::Sprint, 158.f }, { ELureMovementState::Crouch, 88.f }, { ELureMovementState::Prone, 33.f } };
	for (const TPair<ELureMovementState, float>& Case : Cases)
	{
		QAM::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, QAM::FixtureA(*this));
		if (!Character)
		{
			return false;
		}
		World.Tick(QAM::SettleFrames);
		if (Case.Key == ELureMovementState::Sprint)
		{
			Character->SetSprintRequested(true);
			World.TickMoving(Character, 40);
		}
		else
		{
			Character->RequestStance(Case.Key == ELureMovementState::Crouch ? ELureStance::Crouch : Case.Key == ELureMovementState::Prone ? ELureStance::Prone : ELureStance::Stand);
			World.Tick(40); // > the longest TransitionTime + 0.1 s
		}
		const FString Label = QAM::StateName(Case.Key);
		TestNearlyEqual(Label + TEXT(": camera height above the feet == EyeHeight"), QAM::EyeAboveFeet(Character), Case.Value, 0.5f);
		TestNearlyEqual(Label + TEXT(": GetCurrentEyeHeight agrees with the camera"), Character->GetCurrentEyeHeight(), QAM::EyeAboveFeet(Character), 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeMidTransitionIsBetween, "Project.Movement.QA.Eye.MidTransitionIsBetween", QAMovement::Flags)
bool FQAMoveEyeMidTransitionIsBetween::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::FixtureA(*this);
	for (const QAMovementEye::FCase& Case : QAMovementEye::Cases)
	{
		QAMovementEye::FSeries S;
		const TArray<FLureMovementRow> Rows = QAM::Resolve(Table);
		const int32 Half = QAMovementEye::FramesFor(0.5f * QAM::RowOf(Rows, QAM::StateOf(Case.To)).TransitionTime, QAM::Dt);
		if (!QAMovementEye::Record(*this, Table, Case.From, Case.To, Half, QAM::Dt, S))
		{
			return false;
		}
		const float Fraction = (S.Eye.Last() - S.StartEye) / (S.TargetEye - S.StartEye);
		TestTrue(FString::Printf(TEXT("%s: at T/2 the camera is part-way (%.0f%% of the way, want 5-98%%)"), *QAMovementEye::CaseName(Case), Fraction * 100.f), Fraction >= 0.05f && Fraction <= 0.98f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeReachesTargetInTime, "Project.Movement.QA.Eye.ReachesTargetInTime", QAMovement::Flags)
bool FQAMoveEyeReachesTargetInTime::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::FixtureA(*this);
	for (const QAMovementEye::FCase& Case : QAMovementEye::Cases)
	{
		const TArray<FLureMovementRow> Rows = QAM::Resolve(Table);
		const int32 Frames = FMath::CeilToInt(QAM::RowOf(Rows, QAM::StateOf(Case.To)).TransitionTime / QAM::Dt) + 2;
		QAMovementEye::FSeries S;
		if (!QAMovementEye::Record(*this, Table, Case.From, Case.To, Frames, QAM::Dt, S))
		{
			return false;
		}
		TestNearlyEqual(FString::Printf(TEXT("%s: at the target within TransitionTime + 2 frames (target row's time, A8)"), *QAMovementEye::CaseName(Case)), S.Eye.Last(), S.TargetEye, 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeNoOvershoot, "Project.Movement.QA.Eye.NoOvershoot", QAMovement::Flags)
bool FQAMoveEyeNoOvershoot::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::FixtureA(*this);
	for (const QAMovementEye::FCase& Case : QAMovementEye::Cases)
	{
		QAMovementEye::FSeries S;
		if (!QAMovementEye::Record(*this, Table, Case.From, Case.To, 40, QAM::Dt, S))
		{
			return false;
		}
		const float Direction = FMath::Sign(S.TargetEye - S.StartEye);
		int32 Backwards = 0;
		int32 Beyond = 0;
		for (int32 Index = 1; Index < S.Eye.Num(); ++Index)
		{
			Backwards += ((S.Eye[Index] - S.Eye[Index - 1]) * Direction < -0.01f) ? 1 : 0;
			Beyond += ((S.Eye[Index] - S.TargetEye) * Direction > 0.01f) ? 1 : 0;
		}
		TestEqual(QAMovementEye::CaseName(Case) + TEXT(": frames moving away from the target"), Backwards, 0);
		TestEqual(QAMovementEye::CaseName(Case) + TEXT(": frames past the target"), Beyond, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeNoPopWhenCapsuleResizes, "Project.Movement.QA.Eye.NoPopWhenCapsuleResizes", QAMovement::Flags)
bool FQAMoveEyeNoPopWhenCapsuleResizes::RunTest(const FString& Parameters)
{
	// The capsule resizes instantly; the camera must not jump with it. Bound: 0.4 x |dEye| per frame at 60 fps
	// (a pop of the half-height difference or a snap to the target is always above it with Fixture A).
	UDataTable* Table = QAM::FixtureA(*this);
	for (const QAMovementEye::FCase& Case : QAMovementEye::Cases)
	{
		QAMovementEye::FSeries S;
		if (!QAMovementEye::Record(*this, Table, Case.From, Case.To, 40, QAM::Dt, S))
		{
			return false;
		}
		const float Bound = 0.4f * FMath::Abs(S.TargetEye - S.StartEye);
		float Worst = 0.f;
		for (int32 Index = 1; Index < S.CameraZ.Num(); ++Index)
		{
			Worst = FMath::Max(Worst, FMath::Abs(S.CameraZ[Index] - S.CameraZ[Index - 1]));
		}
		TestTrue(FString::Printf(TEXT("%s: largest camera jump in one frame %.2f cm <= %.2f"), *QAMovementEye::CaseName(Case), Worst, Bound), Worst <= Bound);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeInterruptReversesSmoothly, "Project.Movement.QA.Eye.InterruptReversesSmoothly", QAMovement::Flags)
bool FQAMoveEyeInterruptReversesSmoothly::RunTest(const FString& Parameters)
{
	QAMovementEye::FSeries S;
	const int32 Half = QAMovementEye::FramesFor(0.125f, QAM::Dt); // half of Crouch's 0.25 s
	const bool bOk = QAMovementEye::Record(*this, QAM::FixtureA(*this), ELureStance::Stand, ELureStance::Crouch, 50, QAM::Dt, S,
		[Half](QAM::FWorld&, ALurePlayerCharacter* Character, int32 Frame)
		{
			if (Frame == Half)
			{
				Character->RequestStance(ELureStance::Stand);
			}
		});
	if (!bOk)
	{
		return false;
	}
	const float Bound = 0.4f * FMath::Abs(158.f - 88.f);
	float Worst = 0.f;
	for (int32 Index = 1; Index < S.CameraZ.Num(); ++Index)
	{
		Worst = FMath::Max(Worst, FMath::Abs(S.CameraZ[Index] - S.CameraZ[Index - 1]));
	}
	TestTrue(FString::Printf(TEXT("no jump when reversing mid-way (%.2f cm <= %.2f)"), Worst, Bound), Worst <= Bound);
	TestTrue(TEXT("it had started going down before the reversal"), S.Eye[Half] < 157.f);
	TestNearlyEqual(TEXT("ends back at the standing eye height"), S.Eye.Last(), 158.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeZeroTransitionTimeSnaps, "Project.Movement.QA.Eye.ZeroTransitionTimeSnaps", QAMovement::Flags)
bool FQAMoveEyeZeroTransitionTimeSnaps::RunTest(const FString& Parameters)
{
	// Fixture B: Prone.TransitionTime = 0 (division by zero guard).
	QAMovementEye::FSeries S;
	if (!QAMovementEye::Record(*this, QAM::FixtureB(*this), ELureStance::Stand, ELureStance::Prone, 3, QAM::Dt, S))
	{
		return false;
	}
	TestTrue(TEXT("finite"), FMath::IsFinite(S.Eye[1]) && FMath::IsFinite(S.Eye.Last()));
	TestNearlyEqual(TEXT("at the prone eye height on the first frame"), S.Eye[1], 30.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeFrameRateIndependent, "Project.Movement.QA.Eye.FrameRateIndependent", QAMovement::Flags)
bool FQAMoveEyeFrameRateIndependent::RunTest(const FString& Parameters)
{
	// Stand->Crouch (0.25 s) at 30 and 120 fps, compared at the same time t = 4/30 = 16/120 s.
	UDataTable* Table = QAM::FixtureA(*this);
	QAMovementEye::FSeries Slow;
	QAMovementEye::FSeries Fast;
	if (!QAMovementEye::Record(*this, Table, ELureStance::Stand, ELureStance::Crouch, 10, 1.f / 30.f, Slow)
		|| !QAMovementEye::Record(*this, Table, ELureStance::Stand, ELureStance::Crouch, 32, 1.f / 120.f, Fast))
	{
		return false;
	}
	const float Tolerance = 0.03f * FMath::Abs(Slow.TargetEye - Slow.StartEye);
	TestNearlyEqual(FString::Printf(TEXT("same height at t = 0.133 s at 30 and 120 fps (%.2f vs %.2f)"), Slow.Eye[4], Fast.Eye[16]), Slow.Eye[4], Fast.Eye[16], Tolerance);
	TestNearlyEqual(TEXT("30 fps reaches the target by T + 2 frames"), Slow.Eye[10], Slow.TargetEye, 0.5f);
	TestNearlyEqual(TEXT("120 fps reaches the target by T + 2 frames"), Fast.Eye[32], Fast.TargetEye, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeDeterministic, "Project.Movement.QA.Eye.Deterministic", QAMovement::Flags)
bool FQAMoveEyeDeterministic::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::FixtureA(*this);
	QAMovementEye::FSeries A;
	QAMovementEye::FSeries B;
	if (!QAMovementEye::Record(*this, Table, ELureStance::Stand, ELureStance::Prone, 40, QAM::Dt, A)
		|| !QAMovementEye::Record(*this, Table, ELureStance::Stand, ELureStance::Prone, 40, QAM::Dt, B))
	{
		return false;
	}
	float Worst = 0.f;
	for (int32 Index = 0; Index < FMath::Min(A.Eye.Num(), B.Eye.Num()); ++Index)
	{
		Worst = FMath::Max(Worst, FMath::Abs(A.Eye[Index] - B.Eye[Index]));
	}
	TestTrue(FString::Printf(TEXT("two identical runs match (worst difference %.5f cm)"), Worst), Worst <= 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeSprintDoesNotMoveCamera, "Project.Movement.QA.Eye.SprintDoesNotMoveCamera", QAMovement::Flags)
bool FQAMoveEyeSprintDoesNotMoveCamera::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	World.TickMoving(Character, 20);
	float Lowest = TNumericLimits<float>::Max();
	float Highest = TNumericLimits<float>::Lowest();
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		if (Frame == 0 || Frame == 30 || Frame == 60)
		{
			Character->SetSprintRequested(Frame != 30);
		}
		World.TickMoving(Character, 1);
		Lowest = FMath::Min(Lowest, QAM::EyeAboveFeet(Character));
		Highest = FMath::Max(Highest, QAM::EyeAboveFeet(Character));
	}
	TestTrue(FString::Printf(TEXT("toggling sprint keeps the eye at 158 (%.2f..%.2f)"), Lowest, Highest), FMath::Abs(Lowest - 158.f) <= 0.5f && FMath::Abs(Highest - 158.f) <= 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeHitchClamped, "Project.Movement.QA.Eye.HitchClamped", QAMovement::Flags)
bool FQAMoveEyeHitchClamped::RunTest(const FString& Parameters)
{
	QAMovementEye::FSeries S;
	const bool bOk = QAMovementEye::Record(*this, QAM::FixtureA(*this), ELureStance::Stand, ELureStance::Crouch, 4, QAM::Dt, S,
		[](QAM::FWorld& World, ALurePlayerCharacter* Character, int32 Frame)
		{
			if (Frame == 3)
			{
				World.Tick(1, 0.5f); // a half-second hitch mid-blend
			}
		});
	if (!bOk)
	{
		return false;
	}
	TestTrue(TEXT("finite after the hitch"), FMath::IsFinite(S.Eye.Last()));
	TestNearlyEqual(TEXT("lands exactly on the target after a long frame"), S.Eye.Last(), S.TargetEye, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyePawnViewLocationMatchesCamera, "Project.Movement.QA.Eye.PawnViewLocationMatchesCamera", QAMovement::Flags)
bool FQAMoveEyePawnViewLocationMatchesCamera::RunTest(const FString& Parameters)
{
	// AI sight lines (T-016 hiding) use the pawn's eyes: they must be where the player's camera is, low when prone.
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	UDataTable* Table = QAM::FixtureA(*this);
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, Table);
	if (!Character)
	{
		return false;
	}
	World.Tick(QAM::SettleFrames);
	const TArray<FLureMovementRow> Rows = QAM::Resolve(Table);
	for (const ELureStance Stance : { ELureStance::Stand, ELureStance::Crouch, ELureStance::Prone })
	{
		QAM::EnterStance(World, Character, Stance, 40);
		const FString Label = QAM::StanceName(Stance);
		const FVector Camera = Character->GetFirstPersonCamera()->GetComponentLocation();
		FVector EyesLocation;
		FRotator EyesRotation;
		Character->GetActorEyesViewPoint(EyesLocation, EyesRotation);
		TestTrue(Label + TEXT(": GetPawnViewLocation is the camera"), Character->GetPawnViewLocation().Equals(Camera, 1.0));
		TestTrue(Label + TEXT(": GetActorEyesViewPoint (AI perception) is the camera"), EyesLocation.Equals(Camera, 1.0));
		TestNearlyEqual(Label + TEXT(": eyes above the feet == EyeHeight"), static_cast<float>(EyesLocation.Z) - QAM::FeetZ(Character), QAM::RowOf(Rows, QAM::StateOf(Stance)).EyeHeight, 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeNearClipNeverInsideCeiling, "Project.Movement.QA.Eye.NearClipNeverInsideCeiling", QAMovement::Flags)
bool FQAMoveEyeNearClipNeverInsideCeiling::RunTest(const FString& Parameters)
{
	// Go prone right at the mouth of the 60 cm gap and crawl in while the camera is still easing down (A9):
	// the near-clip sphere around the camera must never be inside the slab (no seeing through the ceiling).
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	UDataTable* Table = QAM::ShippedTable(*this);
	const float SlabMinX = 100.f;
	const float StandRadius = QAM::RowOf(QAM::Resolve(Table), ELureMovementState::Stand).CapsuleRadius;
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector(SlabMinX - StandRadius - 1.f, 0.f, 0.f), Table);
	if (!Character)
	{
		return false;
	}
	World.Tick(QAM::SettleFrames);
	World.AddSlab(QAM::CrawlGap, SlabMinX, SlabMinX + 300.f);
	World.Tick(1);
	Character->RequestStance(ELureStance::Prone);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(QAMovementNearClip), false, Character);
	int32 InsideFrames = 0;
	float WorstZ = 0.f;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		World.TickMoving(Character, 1);
		const FVector Camera = Character->GetFirstPersonCamera()->GetComponentLocation();
		if (World.World->OverlapBlockingTestByChannel(Camera, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(QAM::NearClip - 1.f), Params))
		{
			++InsideFrames;
			WorstZ = FMath::Max(WorstZ, static_cast<float>(Camera.Z));
		}
	}
	TestTrue(FString::Printf(TEXT("crawled under the slab (x %.1f)"), Character->GetActorLocation().X), Character->GetActorLocation().X > SlabMinX + 50.f);
	TestEqual(FString::Printf(TEXT("frames with the camera's near-clip sphere inside geometry (worst camera z %.1f)"), WorstZ), InsideFrames, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveEyeCameraSteadyWhileWalking, "Project.Movement.QA.Eye.CameraSteadyWhileWalking", QAMovement::Flags)
bool FQAMoveEyeCameraSteadyWhileWalking::RunTest(const FString& Parameters)
{
	// SK_FPArms.anim.md: the bob is on the arms; the camera itself stays steady (no camera bob by default).
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	World.TickMoving(Character, 30);
	const FVector Relative = Character->GetFirstPersonCamera()->GetRelativeLocation();
	float Worst = 0.f;
	Character->SetSprintRequested(true);
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		World.TickMoving(Character, 1);
		Worst = FMath::Max(Worst, static_cast<float>(FVector::Dist(Character->GetFirstPersonCamera()->GetRelativeLocation(), Relative)));
	}
	TestTrue(FString::Printf(TEXT("camera offset constant while walking and sprinting (moved %.3f cm)"), Worst), Worst <= 0.05f);
	return true;
}

// =====================================================================================================================
// ArmsBob: FLureArmsBob::Step, black-box from the spec's formula block (SK_FPArms.anim.md)
// =====================================================================================================================

namespace QAMovementBob
{
	FLureMovementRow Row(float MaxSpeed = 350.f)
	{
		FLureMovementRow R;
		R.MaxSpeed = MaxSpeed;
		R.BobStepRate = 0.f;
		R.BobVertical = 0.8f;
		R.BobLateral = 0.6f;
		R.BobRoll = 0.6f;
		R.BobPitch = 0.4f;
		R.BobYaw = 1.5f;
		R.BobForward = 1.2f;
		return R;
	}

	FLureArmsMotionSettings Settings()
	{
		FLureArmsMotionSettings S;
		S.BobBlendSpeed = 8.f;
		S.BobHoldRodScale = 0.7f;
		S.LookSwayPerDegPerSec = 0.02f;
		S.LookSwayMaxDeg = 2.5f;
		S.LookSwaySpeed = 10.f;
		return S;
	}

	struct FPeaks
	{
		float MaxAbsZ = 0.f;
		float MaxZ = -1e9f;
		float MaxAbsY = 0.f;
		float MaxAbsX = 0.f;
		float MaxX = -1e9f;
		float MaxAbsPitch = 0.f;
		float MaxPitch = -1e9f;
		float MaxAbsRoll = 0.f;
		float MaxAbsYaw = 0.f;
		bool bFinite = true;

		void Add(const FTransform& T)
		{
			const FVector L = T.GetLocation();
			const FRotator R = T.Rotator();
			bFinite &= !L.ContainsNaN() && !R.ContainsNaN();
			MaxAbsZ = FMath::Max(MaxAbsZ, FMath::Abs(static_cast<float>(L.Z)));
			MaxZ = FMath::Max(MaxZ, static_cast<float>(L.Z));
			MaxAbsY = FMath::Max(MaxAbsY, FMath::Abs(static_cast<float>(L.Y)));
			MaxAbsX = FMath::Max(MaxAbsX, FMath::Abs(static_cast<float>(L.X)));
			MaxX = FMath::Max(MaxX, static_cast<float>(L.X));
			MaxAbsPitch = FMath::Max(MaxAbsPitch, FMath::Abs(static_cast<float>(R.Pitch)));
			MaxPitch = FMath::Max(MaxPitch, static_cast<float>(R.Pitch));
			MaxAbsRoll = FMath::Max(MaxAbsRoll, FMath::Abs(static_cast<float>(R.Roll)));
			MaxAbsYaw = FMath::Max(MaxAbsYaw, FMath::Abs(static_cast<float>(R.Yaw)));
		}
	};

	/** Runs Steps steps; Peaks from step SkipSteps on. */
	FTransform Run(FLureArmsBobState& State, const FLureMovementRow& R, int32 Steps, float Speed, bool bOnGround, bool bRod, const FVector2D& Look, float DeltaTime, FPeaks* Peaks = nullptr, int32 SkipSteps = 0)
	{
		const FLureArmsMotionSettings S = Settings();
		FTransform Last = FTransform::Identity;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Last = FLureArmsBob::Step(State, R, S, Speed, bOnGround, bRod, Look, DeltaTime);
			if (Peaks && Step >= SkipSteps)
			{
				Peaks->Add(Last);
			}
		}
		return Last;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobStepRateFormula, "Project.Movement.QA.ArmsBob.StepRateFormula", QAMovement::Flags)
bool FQAMoveArmsBobStepRateFormula::RunTest(const FString& Parameters)
{
	// BobStepRate 0 = clamp(1.2 + 0.0025 * MaxSpeed, 1, 3); otherwise the row's value.
	TestNearlyEqual(TEXT("350 cm/s -> 2.075"), FLureArmsBob::GetStepRate(QAMovementBob::Row(350.f)), 2.075f, 1e-4f);
	TestNearlyEqual(TEXT("90 cm/s -> 1.425"), FLureArmsBob::GetStepRate(QAMovementBob::Row(90.f)), 1.425f, 1e-4f);
	TestNearlyEqual(TEXT("2000 cm/s clamps to 3"), FLureArmsBob::GetStepRate(QAMovementBob::Row(2000.f)), 3.f, 1e-4f);
	FLureMovementRow Explicit = QAMovementBob::Row(350.f);
	Explicit.BobStepRate = 1.7f;
	TestNearlyEqual(TEXT("an explicit BobStepRate wins"), FLureArmsBob::GetStepRate(Explicit), 1.7f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobStandingStillHasNoBob, "Project.Movement.QA.ArmsBob.StandingStillHasNoBob", QAMovement::Flags)
bool FQAMoveArmsBobStandingStillHasNoBob::RunTest(const FString& Parameters)
{
	FLureArmsBobState State;
	QAMovementBob::FPeaks Peaks;
	QAMovementBob::Run(State, QAMovementBob::Row(), 120, 0.f, true, false, FVector2D::ZeroVector, QAM::Dt, &Peaks);
	TestTrue(TEXT("finite"), Peaks.bFinite);
	TestTrue(FString::Printf(TEXT("no offset when not moving (max |z| %.4f, |y| %.4f)"), Peaks.MaxAbsZ, Peaks.MaxAbsY), Peaks.MaxAbsZ < 1e-3f && Peaks.MaxAbsY < 1e-3f && Peaks.MaxAbsX < 1e-3f);
	TestTrue(TEXT("no rotation when not moving"), Peaks.MaxAbsPitch < 1e-3f && Peaks.MaxAbsRoll < 1e-3f && Peaks.MaxAbsYaw < 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobBoundedByAmplitudes, "Project.Movement.QA.ArmsBob.BoundedByAmplitudes", QAMovement::Flags)
bool FQAMoveArmsBobBoundedByAmplitudes::RunTest(const FString& Parameters)
{
	const FLureMovementRow R = QAMovementBob::Row();
	FLureArmsBobState State;
	QAMovementBob::FPeaks Peaks;
	QAMovementBob::Run(State, R, 600, R.MaxSpeed, true, false, FVector2D::ZeroVector, QAM::Dt, &Peaks);
	const float E = 1e-3f;
	TestTrue(TEXT("finite"), Peaks.bFinite);
	TestTrue(FString::Printf(TEXT("vertical within BobVertical (%.3f <= %.3f) and only down"), Peaks.MaxAbsZ, R.BobVertical), Peaks.MaxAbsZ <= R.BobVertical + E && Peaks.MaxZ <= E);
	TestTrue(FString::Printf(TEXT("lateral within BobLateral (%.3f <= %.3f)"), Peaks.MaxAbsY, R.BobLateral), Peaks.MaxAbsY <= R.BobLateral + E);
	TestTrue(FString::Printf(TEXT("forward push within BobForward (%.3f <= %.3f) and only back"), Peaks.MaxAbsX, R.BobForward), Peaks.MaxAbsX <= R.BobForward + E && Peaks.MaxX <= E);
	TestTrue(FString::Printf(TEXT("pitch within BobPitch (%.3f <= %.3f) and only nodding down"), Peaks.MaxAbsPitch, R.BobPitch), Peaks.MaxAbsPitch <= R.BobPitch + E && Peaks.MaxPitch <= E);
	TestTrue(FString::Printf(TEXT("roll within BobRoll (%.3f <= %.3f)"), Peaks.MaxAbsRoll, R.BobRoll), Peaks.MaxAbsRoll <= R.BobRoll + E);
	TestTrue(FString::Printf(TEXT("yaw within BobYaw (%.3f <= %.3f)"), Peaks.MaxAbsYaw, R.BobYaw), Peaks.MaxAbsYaw <= R.BobYaw + E);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobFullSpeedReachesFullAmplitude, "Project.Movement.QA.ArmsBob.FullSpeedReachesFullAmplitude", QAMovement::Flags)
bool FQAMoveArmsBobFullSpeedReachesFullAmplitude::RunTest(const FString& Parameters)
{
	const FLureMovementRow R = QAMovementBob::Row();
	FLureArmsBobState State;
	QAMovementBob::FPeaks Peaks;
	QAMovementBob::Run(State, R, 240, R.MaxSpeed, true, false, FVector2D::ZeroVector, QAM::Dt, &Peaks, 180); // the last second of 4 s
	TestTrue(FString::Printf(TEXT("vertical dip reaches >= 90%% of BobVertical (%.3f)"), Peaks.MaxAbsZ), Peaks.MaxAbsZ >= 0.9f * R.BobVertical);
	TestTrue(FString::Printf(TEXT("side sway reaches >= 90%% of BobLateral (%.3f)"), Peaks.MaxAbsY), Peaks.MaxAbsY >= 0.9f * R.BobLateral);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobAirborneFadesOut, "Project.Movement.QA.ArmsBob.AirborneFadesOut", QAMovement::Flags)
bool FQAMoveArmsBobAirborneFadesOut::RunTest(const FString& Parameters)
{
	const FLureMovementRow R = QAMovementBob::Row();
	FLureArmsBobState State;
	QAMovementBob::Run(State, R, 120, R.MaxSpeed, true, false, FVector2D::ZeroVector, QAM::Dt);
	QAMovementBob::FPeaks Peaks;
	QAMovementBob::Run(State, R, 120, R.MaxSpeed, false, false, FVector2D::ZeroVector, QAM::Dt, &Peaks, 90); // the last 0.5 s of 2 s in the air
	TestTrue(FString::Printf(TEXT("bob fades out in the air (max |z| %.4f)"), Peaks.MaxAbsZ), Peaks.MaxAbsZ < 0.02f * R.BobVertical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobHoldingRodIsSteadier, "Project.Movement.QA.ArmsBob.HoldingRodIsSteadier", QAMovement::Flags)
bool FQAMoveArmsBobHoldingRodIsSteadier::RunTest(const FString& Parameters)
{
	const FLureMovementRow R = QAMovementBob::Row();
	FLureArmsBobState Free;
	FLureArmsBobState Rod;
	QAMovementBob::FPeaks FreePeaks;
	QAMovementBob::FPeaks RodPeaks;
	QAMovementBob::Run(Free, R, 240, R.MaxSpeed, true, false, FVector2D::ZeroVector, QAM::Dt, &FreePeaks, 120);
	QAMovementBob::Run(Rod, R, 240, R.MaxSpeed, true, true, FVector2D::ZeroVector, QAM::Dt, &RodPeaks, 120);
	const float Ratio = FreePeaks.MaxAbsZ > 0.f ? RodPeaks.MaxAbsZ / FreePeaks.MaxAbsZ : 0.f;
	TestNearlyEqual(FString::Printf(TEXT("bob with the rod is BobHoldRodScale (0.7) of the free bob (ratio %.3f)"), Ratio), Ratio, 0.7f, 0.02f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobLookSwayLagsAndIsClamped, "Project.Movement.QA.ArmsBob.LookSwayLagsAndIsClamped", QAMovement::Flags)
bool FQAMoveArmsBobLookSwayLagsAndIsClamped::RunTest(const FString& Parameters)
{
	const FLureMovementRow R = QAMovementBob::Row();
	FLureArmsBobState State;
	const FTransform Turning = QAMovementBob::Run(State, R, 120, 0.f, true, false, FVector2D(1000.f, 1000.f), QAM::Dt);
	const FRotator Sway = Turning.Rotator();
	TestTrue(FString::Printf(TEXT("yaw sway is clamped to LookSwayMaxDeg (%.3f)"), Sway.Yaw), FMath::Abs(Sway.Yaw) <= 2.5f + 1e-3f);
	TestTrue(FString::Printf(TEXT("pitch sway is clamped to LookSwayMaxDeg (%.3f)"), Sway.Pitch), FMath::Abs(Sway.Pitch) <= 2.5f + 1e-3f);
	TestTrue(FString::Printf(TEXT("arms lag the turn (yaw sway %.3f is opposite to a right turn, near the clamp)"), Sway.Yaw), Sway.Yaw < -2.4f);
	const FTransform Stopped = QAMovementBob::Run(State, R, 120, 0.f, true, false, FVector2D::ZeroVector, QAM::Dt);
	TestTrue(FString::Printf(TEXT("sway settles back when the view stops (yaw %.4f)"), Stopped.Rotator().Yaw), FMath::Abs(Stopped.Rotator().Yaw) < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobDeterministic, "Project.Movement.QA.ArmsBob.Deterministic", QAMovement::Flags)
bool FQAMoveArmsBobDeterministic::RunTest(const FString& Parameters)
{
	const FLureMovementRow R = QAMovementBob::Row();
	FLureArmsBobState A;
	FLureArmsBobState B;
	const FTransform TA = QAMovementBob::Run(A, R, 300, 200.f, true, false, FVector2D(30.f, -10.f), QAM::Dt);
	const FTransform TB = QAMovementBob::Run(B, R, 300, 200.f, true, false, FVector2D(30.f, -10.f), QAM::Dt);
	TestTrue(TEXT("same inputs, same arms offset"), TA.Equals(TB, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveArmsBobSafeWithOddDeltaTimes, "Project.Movement.QA.ArmsBob.SafeWithOddDeltaTimes", QAMovement::Flags)
bool FQAMoveArmsBobSafeWithOddDeltaTimes::RunTest(const FString& Parameters)
{
	const FLureMovementRow R = QAMovementBob::Row();
	FLureArmsBobState State;
	QAMovementBob::FPeaks Peaks;
	QAMovementBob::Run(State, R, 30, R.MaxSpeed, true, false, FVector2D(50.f, 0.f), QAM::Dt, &Peaks);
	QAMovementBob::Run(State, R, 3, R.MaxSpeed, true, false, FVector2D(50.f, 0.f), 0.f, &Peaks);	// paused frames
	QAMovementBob::Run(State, R, 3, R.MaxSpeed, true, false, FVector2D(50.f, 0.f), 1.f, &Peaks);	// 1 s hitches
	TestTrue(TEXT("finite with dt = 0 and dt = 1 s"), Peaks.bFinite);
	TestTrue(TEXT("still within the vertical amplitude"), Peaks.MaxAbsZ <= R.BobVertical + 1e-3f);
	TestTrue(FString::Printf(TEXT("phase stays wrapped in [0, 2 PI) (%.4f)"), State.Phase), State.Phase >= 0.f && State.Phase < 2.f * UE_PI);
	QAMovementBob::Run(State, R, 20000, R.MaxSpeed, true, false, FVector2D::ZeroVector, QAM::Dt);
	TestTrue(FString::Printf(TEXT("phase wrapped after 20000 steps (%.4f)"), State.Phase), State.Phase >= 0.f && State.Phase < 2.f * UE_PI);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

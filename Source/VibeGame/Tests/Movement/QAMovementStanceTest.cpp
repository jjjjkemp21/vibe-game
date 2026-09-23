// Lure T-004 QA (qa-engineer): stance transitions, headroom boundaries, the 60 cm gap and jump rules.
// Project.Movement.QA.Stance.*, .Headroom.*, .Gap.*, .Jump.*  (QA design groups T, G, H, J)
// Decisions: docs/specs/movement-rules.md (A1 crouch+sprint stands up, prone ignores sprint; A4 blocked stand-up stays queued;
// A6 no prone in the air; A13 never jump prone; A14 crouch jump allowed; A16 walk off ledges crouched).

#include "Tests/Movement/QAMovementTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"

namespace QAMovementStance
{
	ALurePlayerCharacter* SpawnSettled(FAutomationTestBase& Test, QAM::FWorld& World, const UDataTable* Table, const FVector& Feet = FVector::ZeroVector)
	{
		ALurePlayerCharacter* Character = World.Spawn(Test, Feet, Table);
		if (Character)
		{
			World.Tick(QAM::SettleFrames);
		}
		return Character;
	}

	/** The invariants of every allowed stance change on open ground (QA design 1.6 a-e). */
	void CheckInvariants(FAutomationTestBase& Test, const ALurePlayerCharacter* Character, ELureStance Expected, float FeetBefore, const FString& Label)
	{
		const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Test.TestEqual(Label + TEXT(": stance"), QAM::StanceName(Character->GetStance()), QAM::StanceName(Expected));
		QAM::TestCapsule(Test, Character, Movement->GetMovementRow(QAM::StateOf(Expected)), Label);
		Test.TestNearlyEqual(Label + TEXT(": max speed from the row in use"), QAM::MaxSpeedNow(Character), Movement->GetMovementRow(Movement->GetMovementState()).MaxSpeed, 0.01f);
		Test.TestFalse(Label + TEXT(": no penetration"), QAM::IsPenetrating(Character));
		Test.TestTrue(FString::Printf(TEXT("%s: feet planted (moved %.2f cm)"), *Label, QAM::FeetZ(Character) - FeetBefore), FMath::Abs(QAM::FeetZ(Character) - FeetBefore) <= QAM::FeetTolerance);
		Test.TestTrue(Label + TEXT(": still on the ground"), QAM::IsOnGround(Character));
	}

	/** Spawn, change to From, then request To; checks the invariants. Returns the character. */
	ALurePlayerCharacter* Transition(FAutomationTestBase& Test, QAM::FWorld& World, ELureStance From, ELureStance To, const FString& Label)
	{
		ALurePlayerCharacter* Character = SpawnSettled(Test, World, QAM::FixtureA(Test));
		if (!Character)
		{
			return nullptr;
		}
		const float Feet = QAM::FeetZ(Character);
		if (From != ELureStance::Stand && !Test.TestTrue(Label + TEXT(": reached the start stance"), QAM::EnterStance(World, Character, From)))
		{
			return nullptr;
		}
		Character->RequestStance(To);
		World.Tick(QAM::SettleFrames);
		CheckInvariants(Test, Character, To, Feet, Label);
		return Character;
	}

	/** Lower stance on open floor, then a ceiling of GapHeight lowered over it (no overlap), ready for a stand-up request. */
	ALurePlayerCharacter* UnderCeiling(FAutomationTestBase& Test, QAM::FWorld& World, const UDataTable* Table, ELureStance Lower, float GapHeight, AActor** OutSlab = nullptr)
	{
		ALurePlayerCharacter* Character = SpawnSettled(Test, World, Table);
		// 40 frames: past the longest eye blend, so "the camera doesn't move" is measured from a settled camera.
		if (!Character || !Test.TestTrue(TEXT("reached the lower stance"), QAM::EnterStance(World, Character, Lower, 40)))
		{
			return nullptr;
		}
		AActor* Slab = World.AddSlab(GapHeight, -300.f, 300.f);
		if (OutSlab)
		{
			*OutSlab = Slab;
		}
		World.Tick(2);
		Test.TestFalse(TEXT("setup: the ceiling does not touch the lower stance"), QAM::IsPenetrating(Character));
		return Character;
	}

	void CheckBlocked(FAutomationTestBase& Test, const ALurePlayerCharacter* Character, ELureStance Stays, float EyeBefore, const FString& Label)
	{
		Test.TestEqual(Label + TEXT(": stays in its stance"), QAM::StanceName(Character->GetStance()), QAM::StanceName(Stays));
		QAM::TestCapsule(Test, Character, Character->GetLureMovement()->GetMovementRow(QAM::StateOf(Stays)), Label);
		Test.TestFalse(Label + TEXT(": no penetration"), QAM::IsPenetrating(Character));
		Test.TestNearlyEqual(Label + TEXT(": the camera doesn't bob up and down"), QAM::EyeAboveFeet(Character), EyeBefore, 0.5f);
	}

	/** Sweeps the character's current capsule horizontally to X; true if it got there without a blocking hit. */
	bool SweepTo(ALurePlayerCharacter* Character, float X, FHitResult& OutHit)
	{
		const FVector Start = Character->GetActorLocation();
		Character->SetActorLocation(FVector(X, Start.Y, Start.Z), /*bSweep*/ true, &OutHit);
		return !OutHit.bBlockingHit && FMath::IsNearlyEqual(Character->GetActorLocation().X, X, 0.1f);
	}

	constexpr float SlabMinX = 100.f;
	constexpr float SlabMaxX = 300.f;
}

// =====================================================================================================================
// T: stance transition matrix (Fixture A; open ground)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceStandToSprint, "Project.Movement.QA.Stance.StandToSprint", QAMovement::Flags)
bool FQAMoveStanceStandToSprint::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	const float Feet = QAM::FeetZ(Character);
	Character->SetSprintRequested(true);
	World.TickMoving(Character, QAM::SettleFrames);
	TestTrue(TEXT("sprinting while moving"), Character->IsSprinting());
	TestNearlyEqual(TEXT("Sprint max speed"), QAM::MaxSpeedNow(Character), 577.f, 0.01f);
	QAMovementStance::CheckInvariants(*this, Character, ELureStance::Stand, Feet, TEXT("Stand->Sprint"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceStandToCrouch, "Project.Movement.QA.Stance.StandToCrouch", QAMovement::Flags)
bool FQAMoveStanceStandToCrouch::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	return World.Create(*this) && QAMovementStance::Transition(*this, World, ELureStance::Stand, ELureStance::Crouch, TEXT("Stand->Crouch")) != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceStandToProne, "Project.Movement.QA.Stance.StandToProne", QAMovement::Flags)
bool FQAMoveStanceStandToProne::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	return World.Create(*this) && QAMovementStance::Transition(*this, World, ELureStance::Stand, ELureStance::Prone, TEXT("Stand->Prone")) != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceSprintToStand, "Project.Movement.QA.Stance.SprintToStand", QAMovement::Flags)
bool FQAMoveStanceSprintToStand::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	const float Feet = QAM::FeetZ(Character);
	Character->SetSprintRequested(true);
	World.TickMoving(Character, QAM::SettleFrames);
	Character->SetSprintRequested(false);
	World.TickMoving(Character, 5);
	TestFalse(TEXT("sprint released"), Character->IsSprinting());
	TestNearlyEqual(TEXT("Stand max speed"), QAM::MaxSpeedNow(Character), 311.f, 0.01f);
	QAMovementStance::CheckInvariants(*this, Character, ELureStance::Stand, Feet, TEXT("Sprint->Stand"));
	return true;
}

namespace QAMovementStance
{
	/** Sprinting, then Lower: sprint is cancelled; standing up again with sprint still held resumes it (A3). */
	bool SprintIntoLowerStance(FAutomationTestBase& Test, ELureStance Lower)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = SpawnSettled(Test, World, QAM::FixtureA(Test));
		if (!Character)
		{
			return false;
		}
		const float Feet = QAM::FeetZ(Character);
		const FString Label = FString::Printf(TEXT("Sprint->%s"), *QAM::StanceName(Lower));
		Character->SetSprintRequested(true);
		World.TickMoving(Character, QAM::SettleFrames);
		Character->RequestStance(Lower);
		World.TickMoving(Character, QAM::SettleFrames);
		Test.TestFalse(Label + TEXT(": not sprinting"), Character->IsSprinting());
		CheckInvariants(Test, Character, Lower, Feet, Label);
		Character->RequestStance(ELureStance::Stand);
		World.TickMoving(Character, QAM::SettleFrames);
		Test.TestTrue(Label + TEXT(": standing again with sprint held resumes sprinting (A3)"), Character->IsSprinting());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceSprintToCrouchCancelsSprint, "Project.Movement.QA.Stance.SprintToCrouchCancelsSprint", QAMovement::Flags)
bool FQAMoveStanceSprintToCrouchCancelsSprint::RunTest(const FString& Parameters)
{
	return QAMovementStance::SprintIntoLowerStance(*this, ELureStance::Crouch);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceSprintToProneCancelsSprint, "Project.Movement.QA.Stance.SprintToProneCancelsSprint", QAMovement::Flags)
bool FQAMoveStanceSprintToProneCancelsSprint::RunTest(const FString& Parameters)
{
	return QAMovementStance::SprintIntoLowerStance(*this, ELureStance::Prone);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceCrouchToStandRestoresTableHeight, "Project.Movement.QA.Stance.CrouchToStandRestoresTableHeight", QAMovement::Flags)
bool FQAMoveStanceCrouchToStandRestoresTableHeight::RunTest(const FString& Parameters)
{
	// The engine's UnCrouch restores the class-default capsule; the data's Stand row must win.
	const float ClassDefault = GetDefault<ALurePlayerCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	TestFalse(FString::Printf(TEXT("fixture Stand half height 87 differs from the class default %.1f (so this test can tell)"), ClassDefault), FMath::IsNearlyEqual(ClassDefault, 87.f));
	QAM::FWorld World;
	ALurePlayerCharacter* Character = World.Create(*this) ? QAMovementStance::Transition(*this, World, ELureStance::Crouch, ELureStance::Stand, TEXT("Crouch->Stand")) : nullptr;
	if (!Character)
	{
		return false;
	}
	float Radius = 0.f;
	float HalfHeight = 0.f;
	QAM::GetCapsule(Character, Radius, HalfHeight);
	TestNearlyEqual(TEXT("standing half height is the table's 87, not the class default"), HalfHeight, 87.f, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceCrouchPlusSprintStandsAndSprints, "Project.Movement.QA.Stance.CrouchPlusSprintStandsAndSprints", QAMovement::Flags)
bool FQAMoveStanceCrouchPlusSprintStandsAndSprints::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character || !TestTrue(TEXT("crouched"), QAM::EnterStance(World, Character, ELureStance::Crouch)))
	{
		return false;
	}
	const float Feet = QAM::FeetZ(Character);
	Character->SetSprintRequested(true);
	World.TickMoving(Character, 15);
	TestTrue(TEXT("sprint from crouch stands up and sprints (A1)"), Character->IsSprinting());
	TestNearlyEqual(TEXT("Sprint max speed"), QAM::MaxSpeedNow(Character), 577.f, 0.01f);
	QAMovementStance::CheckInvariants(*this, Character, ELureStance::Stand, Feet, TEXT("Crouch+Sprint"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceCrouchToProne, "Project.Movement.QA.Stance.CrouchToProne", QAMovement::Flags)
bool FQAMoveStanceCrouchToProne::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	return World.Create(*this) && QAMovementStance::Transition(*this, World, ELureStance::Crouch, ELureStance::Prone, TEXT("Crouch->Prone")) != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceProneToStand, "Project.Movement.QA.Stance.ProneToStand", QAMovement::Flags)
bool FQAMoveStanceProneToStand::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	return World.Create(*this) && QAMovementStance::Transition(*this, World, ELureStance::Prone, ELureStance::Stand, TEXT("Prone->Stand")) != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStancePronePlusSprintIgnored, "Project.Movement.QA.Stance.PronePlusSprintIgnored", QAMovement::Flags)
bool FQAMoveStancePronePlusSprintIgnored::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character || !TestTrue(TEXT("prone"), QAM::EnterStance(World, Character, ELureStance::Prone)))
	{
		return false;
	}
	Character->SetSprintRequested(true);
	World.TickMoving(Character, 30);
	TestEqual(TEXT("still prone (A1: sprint is ignored while prone)"), QAM::StanceName(Character->GetStance()), QAM::StanceName(ELureStance::Prone));
	TestEqual(TEXT("still wants prone"), QAM::StanceName(Character->GetRequestedStance()), QAM::StanceName(ELureStance::Prone));
	TestFalse(TEXT("not sprinting"), Character->IsSprinting());
	TestNearlyEqual(TEXT("Prone max speed"), QAM::MaxSpeedNow(Character), 89.f, 0.01f);
	TestTrue(TEXT("never faster than prone speed"), QAM::HorizontalSpeed(Character) <= 89.f * 1.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceProneToCrouch, "Project.Movement.QA.Stance.ProneToCrouch", QAMovement::Flags)
bool FQAMoveStanceProneToCrouch::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	return World.Create(*this) && QAMovementStance::Transition(*this, World, ELureStance::Prone, ELureStance::Crouch, TEXT("Prone->Crouch")) != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceRequestCurrentStanceIsIdempotent, "Project.Movement.QA.Stance.RequestCurrentStanceIsIdempotent", QAMovement::Flags)
bool FQAMoveStanceRequestCurrentStanceIsIdempotent::RunTest(const FString& Parameters)
{
	for (const ELureStance Stance : { ELureStance::Stand, ELureStance::Crouch, ELureStance::Prone })
	{
		QAM::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
		if (!Character)
		{
			return false;
		}
		QAM::EnterStance(World, Character, Stance, 40);
		const FVector Location = Character->GetActorLocation();
		const float Eye = QAM::EyeAboveFeet(Character);
		Character->RequestStance(Stance);
		World.Tick(QAM::SettleFrames);
		const FString Label = QAM::StanceName(Stance);
		TestEqual(Label + TEXT(": same stance"), QAM::StanceName(Character->GetStance()), Label);
		TestTrue(Label + TEXT(": did not move"), Character->GetActorLocation().Equals(Location, 0.1));
		TestNearlyEqual(Label + TEXT(": same eye height"), QAM::EyeAboveFeet(Character), Eye, 0.1f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceSameFrameConflictingRequestsDeterministic, "Project.Movement.QA.Stance.SameFrameConflictingRequestsDeterministic", QAMovement::Flags)
bool FQAMoveStanceSameFrameConflictingRequestsDeterministic::RunTest(const FString& Parameters)
{
	// The last request in a frame wins (A5), and the result is the same every run.
	auto Run = [this](ELureStance First, ELureStance Second) -> ELureStance
	{
		QAM::FWorld World;
		if (!World.Create(*this))
		{
			return ELureStance::Stand;
		}
		ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
		if (!Character)
		{
			return ELureStance::Stand;
		}
		Character->RequestStance(First);
		Character->RequestStance(Second);
		World.Tick(QAM::SettleFrames);
		return Character->GetStance();
	};
	for (int32 Run1 = 0; Run1 < 2; ++Run1)
	{
		TestEqual(TEXT("crouch then prone -> Prone"), QAM::StanceName(Run(ELureStance::Crouch, ELureStance::Prone)), FString(TEXT("Prone")));
		TestEqual(TEXT("prone then crouch -> Crouch"), QAM::StanceName(Run(ELureStance::Prone, ELureStance::Crouch)), FString(TEXT("Crouch")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceRapidToggleSpam, "Project.Movement.QA.Stance.RapidToggleSpam", QAMovement::Flags)
bool FQAMoveStanceRapidToggleSpam::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	FRandomStream Random(1234);
	const float StartFeet = QAM::FeetZ(Character);
	int32 BadCapsule = 0;
	int32 Penetrations = 0;
	int32 Airborne = 0;
	int32 NonFinite = 0;
	float WorstDrift = 0.f;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		switch (Random.RandRange(0, 7))
		{
		case 0: Character->RequestStance(ELureStance::Stand); break;
		case 1: Character->RequestStance(ELureStance::Crouch); break;
		case 2: Character->RequestStance(ELureStance::Prone); break;
		case 3: Character->ToggleCrouch(); break;
		case 4: Character->ToggleProne(); break;
		case 5: Character->SetSprintRequested(true); break;
		case 6: Character->SetSprintRequested(false); break;
		default: break;
		}
		World.TickMoving(Character, 1, FVector(1.f, Random.FRandRange(-0.5f, 0.5f), 0.f).GetSafeNormal());
		BadCapsule += QAM::CapsuleMatches(Character, Movement->GetMovementRow(QAM::StateOf(Character->GetStance()))) ? 0 : 1;
		Penetrations += QAM::IsPenetrating(Character) ? 1 : 0;
		Airborne += QAM::IsOnGround(Character) ? 0 : 1;
		NonFinite += FMath::IsFinite(QAM::EyeAboveFeet(Character)) ? 0 : 1;
		WorstDrift = FMath::Max(WorstDrift, FMath::Abs(QAM::FeetZ(Character) - StartFeet));
	}
	TestEqual(TEXT("frames where the capsule didn't match the reported stance"), BadCapsule, 0);
	TestEqual(TEXT("frames with penetration"), Penetrations, 0);
	TestEqual(TEXT("frames off the ground"), Airborne, 0);
	TestEqual(TEXT("frames with a NaN eye height"), NonFinite, 0);
	TestTrue(FString::Printf(TEXT("feet drift %.2f cm < %.1f"), WorstDrift, QAM::FeetTolerance), WorstDrift < QAM::FeetTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceProneWhileFallingRejected, "Project.Movement.QA.Stance.ProneWhileFallingRejected", QAMovement::Flags)
bool FQAMoveStanceProneWhileFallingRejected::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this, /*bFloor*/ false))
	{
		return false;
	}
	World.AddLedgeFloors(200.f, 300.f);
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	if (!TestTrue(TEXT("walks off the ledge"), World.TickUntil([Character]() { return QAM::IsFalling(Character); }, 180, Character)))
	{
		return false;
	}
	Character->RequestStance(ELureStance::Prone);
	TestFalse(TEXT("prone is refused in the air (A6)"), Character->GetRequestedStance() == ELureStance::Prone);
	bool bProneInAir = false;
	World.TickUntil([Character, &bProneInAir]()
	{
		bProneInAir |= Character->IsProne() && QAM::IsFalling(Character);
		return QAM::IsOnGround(Character);
	}, 240);
	World.Tick(QAM::SettleFrames);
	TestFalse(TEXT("never prone while falling"), bProneInAir);
	TestTrue(TEXT("landed on the lower floor"), QAM::IsOnGround(Character) && QAM::FeetZ(Character) < -290.f);
	TestEqual(TEXT("the refused request is not queued: still standing after landing"), QAM::StanceName(Character->GetStance()), FString(TEXT("Stand")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceCrouchWalksOffLedge, "Project.Movement.QA.Stance.CrouchWalksOffLedge", QAMovement::Flags)
bool FQAMoveStanceCrouchWalksOffLedge::RunTest(const FString& Parameters)
{
	// A16: crouched players can walk off a dock edge (no invisible wall).
	QAM::FWorld World;
	if (!World.Create(*this, /*bFloor*/ false))
	{
		return false;
	}
	World.AddLedgeFloors(200.f, 300.f);
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character || !TestTrue(TEXT("crouched"), QAM::EnterStance(World, Character, ELureStance::Crouch)))
	{
		return false;
	}
	const bool bDown = World.TickUntil([Character]() { return QAM::IsOnGround(Character) && QAM::FeetZ(Character) < -290.f; }, 480, Character);
	TestTrue(FString::Printf(TEXT("walked off and landed below (x %.1f, feet %.1f)"), Character->GetActorLocation().X, QAM::FeetZ(Character)), bDown);
	TestFalse(TEXT("no penetration after landing"), QAM::IsPenetrating(Character));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceProneWalksOffLedge, "Project.Movement.QA.Stance.ProneWalksOffLedge", QAMovement::Flags)
bool FQAMoveStanceProneWalksOffLedge::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this, /*bFloor*/ false))
	{
		return false;
	}
	World.AddLedgeFloors(200.f, 300.f);
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character || !TestTrue(TEXT("prone"), QAM::EnterStance(World, Character, ELureStance::Prone)))
	{
		return false;
	}
	bool bProneInAir = false;
	int32 ProneInAirFrames = 0;
	int32 FallingFrames = 0;
	int32 Penetrations = 0;
	FString Trace;
	int32 Frame = 0;
	const bool bDown = World.TickUntil([&]()
	{
		const bool bFalling = QAM::IsFalling(Character);
		const bool bProneNow = Character->IsProne();
		FallingFrames += bFalling ? 1 : 0;
		if (bProneNow && bFalling)
		{
			bProneInAir = true;
			if (++ProneInAirFrames <= 6)
			{
				Trace += FString::Printf(TEXT(" [frame %d x %.1f feet %.1f vz %.0f]"), Frame, Character->GetActorLocation().X, QAM::FeetZ(Character), Character->GetVelocity().Z);
			}
		}
		Penetrations += QAM::IsPenetrating(Character) ? 1 : 0;
		++Frame;
		return QAM::IsOnGround(Character) && QAM::FeetZ(Character) < -290.f;
	}, 600, Character);
	TestTrue(FString::Printf(TEXT("crawled off and landed below (x %.1f, feet %.1f)"), Character->GetActorLocation().X, QAM::FeetZ(Character)), bDown);
	// Prone ends when you crawl off a ledge; the in-air stand-up may wait a few frames until the wider capsule clears the ledge
	// corner (QA finding 2026-09-22: 3 frames with Fixture A). Contract: <= 0.1 s, never inside geometry.
	if (bProneInAir)
	{
		AddInfo(FString::Printf(TEXT("prone in %d of %d falling frames before standing up in the air:%s"), ProneInAirFrames, FallingFrames, *Trace));
	}
	TestTrue(FString::Printf(TEXT("prone ends within 0.1 s of leaving the ground (%d frames prone in the air)"), ProneInAirFrames), ProneInAirFrames <= 6);
	TestTrue(TEXT("stood up in the air at some point"), FallingFrames > ProneInAirFrames);
	TestEqual(TEXT("frames with penetration"), Penetrations, 0);
	World.Tick(QAM::SettleFrames);
	TestEqual(TEXT("the prone toggle is still on after landing"), QAM::StanceName(Character->GetStance()), FString(TEXT("Prone")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveStanceToggleKeysFollowDecisions, "Project.Movement.QA.Stance.ToggleKeysFollowDecisions", QAMovement::Flags)
bool FQAMoveStanceToggleKeysFollowDecisions::RunTest(const FString& Parameters)
{
	// A4/A5: crouch and prone keys toggle; the other stance key switches directly.
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	struct FStep
	{
		bool bCrouchKey;
		const TCHAR* Expected;
	};
	const FStep Steps[] = {
		{ true, TEXT("Crouch") },	// Stand  + C -> Crouch
		{ true, TEXT("Stand") },	// Crouch + C -> Stand
		{ false, TEXT("Prone") },	// Stand  + Z -> Prone
		{ false, TEXT("Stand") },	// Prone  + Z -> Stand
		{ true, TEXT("Crouch") },	// Stand  + C -> Crouch
		{ false, TEXT("Prone") },	// Crouch + Z -> Prone
		{ true, TEXT("Crouch") },	// Prone  + C -> Crouch
	};
	for (const FStep& Step : Steps)
	{
		const FString Before = QAM::StanceName(Character->GetStance());
		if (Step.bCrouchKey)
		{
			Character->ToggleCrouch();
		}
		else
		{
			Character->ToggleProne();
		}
		World.Tick(QAM::SettleFrames);
		TestEqual(FString::Printf(TEXT("%s + %s key"), *Before, Step.bCrouchKey ? TEXT("crouch") : TEXT("prone")), QAM::StanceName(Character->GetStance()), FString(Step.Expected));
	}
	return true;
}

// =====================================================================================================================
// G: headroom boundaries (Fixture A: Clear(Stand) = 174, Clear(Crouch) = 106)
// =====================================================================================================================

namespace QAMovementStance
{
	/** Lower stance under a ceiling at Gap; request Higher; Expect = the stance it must end in. */
	bool HeadroomCase(FAutomationTestBase& Test, ELureStance Lower, ELureStance Higher, float Gap, ELureStance Expect)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = UnderCeiling(Test, World, QAM::FixtureA(Test), Lower, Gap);
		if (!Character)
		{
			return false;
		}
		const float Feet = QAM::FeetZ(Character);
		const float Eye = QAM::EyeAboveFeet(Character);
		Character->RequestStance(Higher);
		World.Tick(15);
		const FString Label = FString::Printf(TEXT("%s->%s under %.0f cm"), *QAM::StanceName(Lower), *QAM::StanceName(Higher), Gap);
		if (Expect == Lower)
		{
			CheckBlocked(Test, Character, Lower, Eye, Label);
		}
		else
		{
			CheckInvariants(Test, Character, Expect, Feet, Label);
		}
		return true;
	}

	float ClearA(ELureMovementState State)
	{
		return QAM::Clear(QAM::RowOf(QAM::Resolve(QAM::MakeTable(QAM::MakeCsv(QAM::FixtureARows()))), State));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomCrouchToStandBlockedJustBelow, "Project.Movement.QA.Headroom.CrouchToStandBlockedJustBelow", QAMovement::Flags)
bool FQAMoveHeadroomCrouchToStandBlockedJustBelow::RunTest(const FString& Parameters)
{
	return QAMovementStance::HeadroomCase(*this, ELureStance::Crouch, ELureStance::Stand, QAMovementStance::ClearA(ELureMovementState::Stand) - QAM::BlockMargin, ELureStance::Crouch);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomCrouchToStandAllowedJustAbove, "Project.Movement.QA.Headroom.CrouchToStandAllowedJustAbove", QAMovement::Flags)
bool FQAMoveHeadroomCrouchToStandAllowedJustAbove::RunTest(const FString& Parameters)
{
	return QAMovementStance::HeadroomCase(*this, ELureStance::Crouch, ELureStance::Stand, QAMovementStance::ClearA(ELureMovementState::Stand) + QAM::PassMargin, ELureStance::Stand);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomProneToStandBlockedJustBelow, "Project.Movement.QA.Headroom.ProneToStandBlockedJustBelow", QAMovement::Flags)
bool FQAMoveHeadroomProneToStandBlockedJustBelow::RunTest(const FString& Parameters)
{
	// Crouch would fit (173 > 106) but there is no partial rise (A4).
	return QAMovementStance::HeadroomCase(*this, ELureStance::Prone, ELureStance::Stand, QAMovementStance::ClearA(ELureMovementState::Stand) - QAM::BlockMargin, ELureStance::Prone);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomProneToCrouchBlockedJustBelow, "Project.Movement.QA.Headroom.ProneToCrouchBlockedJustBelow", QAMovement::Flags)
bool FQAMoveHeadroomProneToCrouchBlockedJustBelow::RunTest(const FString& Parameters)
{
	return QAMovementStance::HeadroomCase(*this, ELureStance::Prone, ELureStance::Crouch, QAMovementStance::ClearA(ELureMovementState::Crouch) - QAM::BlockMargin, ELureStance::Prone);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomProneToCrouchAllowedJustAbove, "Project.Movement.QA.Headroom.ProneToCrouchAllowedJustAbove", QAMovement::Flags)
bool FQAMoveHeadroomProneToCrouchAllowedJustAbove::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::UnderCeiling(*this, World, QAM::FixtureA(*this), ELureStance::Prone, QAMovementStance::ClearA(ELureMovementState::Crouch) + QAM::PassMargin);
	if (!Character)
	{
		return false;
	}
	const float Feet = QAM::FeetZ(Character);
	Character->RequestStance(ELureStance::Crouch);
	World.Tick(40);
	QAMovementStance::CheckInvariants(*this, Character, ELureStance::Crouch, Feet, TEXT("Prone->Crouch under 109 cm"));
	const float Eye = QAM::EyeAboveFeet(Character);
	Character->RequestStance(ELureStance::Stand);
	World.Tick(15);
	QAMovementStance::CheckBlocked(*this, Character, ELureStance::Crouch, Eye, TEXT("then Crouch->Stand under 109 cm"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomProneToStandAllowedJustAbove, "Project.Movement.QA.Headroom.ProneToStandAllowedJustAbove", QAMovement::Flags)
bool FQAMoveHeadroomProneToStandAllowedJustAbove::RunTest(const FString& Parameters)
{
	return QAMovementStance::HeadroomCase(*this, ELureStance::Prone, ELureStance::Stand, QAMovementStance::ClearA(ELureMovementState::Stand) + QAM::PassMargin, ELureStance::Stand);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomUnder60cmSlabCannotRise, "Project.Movement.QA.Headroom.Under60cmSlabCannotRise", QAMovement::Flags)
bool FQAMoveHeadroomUnder60cmSlabCannotRise::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::UnderCeiling(*this, World, QAM::ShippedTable(*this), ELureStance::Prone, QAM::CrawlGap);
	if (!Character)
	{
		return false;
	}
	const float Eye = QAM::EyeAboveFeet(Character);
	for (const ELureStance Target : { ELureStance::Crouch, ELureStance::Stand })
	{
		Character->RequestStance(Target);
		World.Tick(15);
		QAMovementStance::CheckBlocked(*this, Character, ELureStance::Prone, Eye, FString::Printf(TEXT("under the 60 cm slab, %s request"), *QAM::StanceName(Target)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomQueuedStandCompletesWhenCeilingRemoved, "Project.Movement.QA.Headroom.QueuedStandCompletesWhenCeilingRemoved", QAMovement::Flags)
bool FQAMoveHeadroomQueuedStandCompletesWhenCeilingRemoved::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	AActor* Slab = nullptr;
	ALurePlayerCharacter* Character = QAMovementStance::UnderCeiling(*this, World, QAM::FixtureA(*this), ELureStance::Crouch, QAMovementStance::ClearA(ELureMovementState::Stand) - QAM::BlockMargin, &Slab);
	if (!Character || !Slab)
	{
		return false;
	}
	const float Feet = QAM::FeetZ(Character);
	Character->RequestStance(ELureStance::Stand);
	World.Tick(15);
	TestEqual(TEXT("blocked: still crouched"), QAM::StanceName(Character->GetStance()), FString(TEXT("Crouch")));
	TestEqual(TEXT("the stand-up stays queued (A4)"), QAM::StanceName(Character->GetRequestedStance()), FString(TEXT("Stand")));
	Slab->Destroy();
	World.Tick(15);
	QAMovementStance::CheckInvariants(*this, Character, ELureStance::Stand, Feet, TEXT("after the ceiling is gone"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomOtherStanceInputCancelsQueuedStand, "Project.Movement.QA.Headroom.OtherStanceInputCancelsQueuedStand", QAMovement::Flags)
bool FQAMoveHeadroomOtherStanceInputCancelsQueuedStand::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	AActor* Slab = nullptr;
	ALurePlayerCharacter* Character = QAMovementStance::UnderCeiling(*this, World, QAM::FixtureA(*this), ELureStance::Crouch, QAMovementStance::ClearA(ELureMovementState::Stand) - QAM::BlockMargin, &Slab);
	if (!Character || !Slab)
	{
		return false;
	}
	Character->RequestStance(ELureStance::Stand); // blocked, queued
	World.Tick(5);
	Character->RequestStance(ELureStance::Prone); // any other stance input cancels the queue (A4)
	World.Tick(QAM::SettleFrames);
	TestEqual(TEXT("went prone"), QAM::StanceName(Character->GetStance()), FString(TEXT("Prone")));
	Slab->Destroy();
	World.Tick(15);
	TestEqual(TEXT("stays prone once the ceiling is gone (queue cancelled)"), QAM::StanceName(Character->GetStance()), FString(TEXT("Prone")));
	return true;
}

namespace QAMovementStance
{
	/** Prone on open floor, walls placed next to it, then stand: never ends up inside a wall. */
	bool StandNextToWalls(FAutomationTestBase& Test, const UDataTable* Table, float FaceX, float FaceY, TOptional<bool> ExpectStand)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = SpawnSettled(Test, World, Table);
		if (!Character || !Test.TestTrue(TEXT("prone"), QAM::EnterStance(World, Character, ELureStance::Prone)))
		{
			return false;
		}
		const FVector Axis = Character->GetActorLocation();
		World.AddWallFacingMinusX(Axis.X + FaceX);
		if (FaceY > 0.f)
		{
			World.AddWallFacingMinusY(Axis.Y + FaceY);
		}
		World.Tick(2);
		Test.TestFalse(TEXT("setup: the prone capsule doesn't touch the walls"), QAM::IsPenetrating(Character));
		Character->RequestStance(ELureStance::Stand);
		World.Tick(15);
		const float Pushed = static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), Axis));
		const bool bStood = Character->GetStance() == ELureStance::Stand;
		Test.AddInfo(FString::Printf(TEXT("walls at x+%.1f%s: %s, pushed %.2f cm (MaxStanceNudge %.1f)"), FaceX, FaceY > 0.f ? *FString::Printf(TEXT(", y+%.1f"), FaceY) : TEXT(""),
			bStood ? TEXT("stood up") : TEXT("stayed prone"), Pushed, Character->GetLureMovement()->MaxStanceNudge));
		Test.TestFalse(TEXT("no penetration after the stand-up attempt"), QAM::IsPenetrating(Character));
		if (bStood)
		{
			Test.TestTrue(TEXT("a stand-up push stays within MaxStanceNudge"), Pushed <= Character->GetLureMovement()->MaxStanceNudge + 0.5f);
		}
		if (ExpectStand.IsSet())
		{
			Test.TestEqual(TEXT("expected outcome"), bStood, ExpectStand.GetValue());
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomWiderStandCapsuleNextToWall, "Project.Movement.QA.Headroom.WiderStandCapsuleNextToWall", QAMovement::Flags)
bool FQAMoveHeadroomWiderStandCapsuleNextToWall::RunTest(const FString& Parameters)
{
	// Fixture A: prone radius 25, stand radius 33; the wall at 29 cm is inside the standing capsule.
	return QAMovementStance::StandNextToWalls(*this, QAM::FixtureA(*this), 29.f, 0.f, TOptional<bool>());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomStandUpInACorner, "Project.Movement.QA.Headroom.StandUpInACorner", QAMovement::Flags)
bool FQAMoveHeadroomStandUpInACorner::RunTest(const FString& Parameters)
{
	return QAMovementStance::StandNextToWalls(*this, QAM::FixtureA(*this), 29.f, 29.f, TOptional<bool>());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomNudgeLimitDecidesStandUp, "Project.Movement.QA.Headroom.NudgeLimitDecidesStandUp", QAMovement::Flags)
bool FQAMoveHeadroomNudgeLimitDecidesStandUp::RunTest(const FString& Parameters)
{
	// Shipped data: a wall right against the prone capsule. The stand-up needs a push of (stand radius - wall distance):
	// within MaxStanceNudge it stands, beyond it it stays prone. Both without penetration.
	UDataTable* Table = QAM::ShippedTable(*this);
	if (!Table)
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = QAM::Resolve(Table);
	const float ProneRadius = QAM::RowOf(Rows, ELureMovementState::Prone).CapsuleRadius;
	const float StandRadius = QAM::RowOf(Rows, ELureMovementState::Stand).CapsuleRadius;
	const float Nudge = GetDefault<ULureCharacterMovementComponent>()->MaxStanceNudge;
	const float Near = ProneRadius + 0.5f;								// needs StandRadius - Near
	const float Within = FMath::Max(ProneRadius + 0.5f, StandRadius - Nudge + 1.f);	// needs Nudge - 1
	AddInfo(FString::Printf(TEXT("push needed at %.1f cm: %.1f; at %.1f cm: %.1f; MaxStanceNudge %.1f"), Near, StandRadius - Near, Within, StandRadius - Within, Nudge));
	QAMovementStance::StandNextToWalls(*this, Table, Within, 0.f, TOptional<bool>(true));
	QAMovementStance::StandNextToWalls(*this, Table, Near, 0.f, TOptional<bool>(StandRadius - Near <= Nudge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveHeadroomSprintFromCrouchUnderCeilingBlocked, "Project.Movement.QA.Headroom.SprintFromCrouchUnderCeilingBlocked", QAMovement::Flags)
bool FQAMoveHeadroomSprintFromCrouchUnderCeilingBlocked::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::UnderCeiling(*this, World, QAM::FixtureA(*this), ELureStance::Crouch, QAMovementStance::ClearA(ELureMovementState::Stand) - QAM::BlockMargin);
	if (!Character)
	{
		return false;
	}
	Character->SetSprintRequested(true);
	World.TickMoving(Character, 30); // 0.5 s at crouch speed stays under the 6 m slab
	TestEqual(TEXT("still crouched under the ceiling"), QAM::StanceName(Character->GetStance()), FString(TEXT("Crouch")));
	TestFalse(TEXT("not sprinting"), Character->IsSprinting());
	TestNearlyEqual(TEXT("crouch speed"), QAM::MaxSpeedNow(Character), 173.f, 0.01f);
	TestFalse(TEXT("no penetration"), QAM::IsPenetrating(Character));
	return true;
}

// =====================================================================================================================
// H: the 60 cm crawl gap (shipped data unless noted). Slab from x = 100 to 300.
// =====================================================================================================================

namespace QAMovementStance
{
	/** Stance at the origin, then a slab of GapHeight ahead. */
	ALurePlayerCharacter* BeforeGap(FAutomationTestBase& Test, QAM::FWorld& World, const UDataTable* Table, ELureStance Stance, float GapHeight)
	{
		ALurePlayerCharacter* Character = SpawnSettled(Test, World, Table);
		if (!Character || (Stance != ELureStance::Stand && !Test.TestTrue(TEXT("reached the stance"), QAM::EnterStance(World, Character, Stance))))
		{
			return nullptr;
		}
		World.AddSlab(GapHeight, SlabMinX, SlabMaxX);
		World.Tick(2);
		return Character;
	}

	bool SweepGap(FAutomationTestBase& Test, const UDataTable* Table, ELureStance Stance, float GapHeight, bool bExpectFits)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = BeforeGap(Test, World, Table, Stance, GapHeight);
		if (!Character)
		{
			return false;
		}
		FHitResult Hit;
		const bool bFits = SweepTo(Character, 200.f, Hit);
		const FString Label = FString::Printf(TEXT("%s under a %.1f cm gap"), *QAM::StanceName(Stance), GapHeight);
		Test.TestEqual(Label + (bExpectFits ? TEXT(": fits") : TEXT(": is blocked")), bFits, bExpectFits);
		if (!bExpectFits)
		{
			// A rounded capsule top can slide a little under a low edge before it stops, but its axis never gets under the slab.
			Test.TestTrue(FString::Printf(TEXT("%s: stopped before the slab (axis x %.1f < %.1f)"), *Label, Character->GetActorLocation().X, SlabMinX), Character->GetActorLocation().X < SlabMinX);
		}
		return true;
	}

	bool WalkThroughGap(FAutomationTestBase& Test, const UDataTable* Table, ELureStance Stance, float GapHeight, bool bExpectThrough, int32 MaxFrames)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = BeforeGap(Test, World, Table, Stance, GapHeight);
		if (!Character)
		{
			return false;
		}
		const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float StartFeet = QAM::FeetZ(Character);
		float MinFeet = StartFeet;
		float MaxFeet = StartFeet;
		bool bFell = false;
		int32 Penetrations = 0;
		const bool bThrough = World.TickUntil([&]()
		{
			MinFeet = FMath::Min(MinFeet, QAM::FeetZ(Character));
			MaxFeet = FMath::Max(MaxFeet, QAM::FeetZ(Character));
			bFell |= QAM::IsFalling(Character);
			Penetrations += QAM::IsPenetrating(Character) ? 1 : 0;
			return Character->GetActorLocation().X > SlabMaxX + Radius;
		}, MaxFrames, Character);
		const FString Label = FString::Printf(TEXT("%s walking into a %.1f cm gap"), *QAM::StanceName(Stance), GapHeight);
		Test.TestEqual(FString::Printf(TEXT("%s: %s (x %.1f)"), *Label, bExpectThrough ? TEXT("comes out the far side") : TEXT("stops at the edge"), Character->GetActorLocation().X), bThrough, bExpectThrough);
		if (!bExpectThrough)
		{
			Test.TestTrue(Label + TEXT(": never got under the slab"), Character->GetActorLocation().X < SlabMinX - Radius + 1.f);
		}
		Test.TestFalse(Label + TEXT(": never falling"), bFell);
		Test.TestTrue(FString::Printf(TEXT("%s: feet stay level (%.2f cm)"), *Label, MaxFeet - MinFeet), MaxFeet - MinFeet < QAM::FeetTolerance);
		Test.TestEqual(Label + TEXT(": frames with penetration"), Penetrations, 0);
		Test.TestEqual(Label + TEXT(": stance unchanged"), QAM::StanceName(Character->GetStance()), QAM::StanceName(Stance));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapProneSweepsUnder60cm, "Project.Movement.QA.Gap.ProneSweepsUnder60cm", QAMovement::Flags)
bool FQAMoveGapProneSweepsUnder60cm::RunTest(const FString& Parameters)
{
	return QAMovementStance::SweepGap(*this, QAM::ShippedTable(*this), ELureStance::Prone, 60.f, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapProneSweepsUnder61cm, "Project.Movement.QA.Gap.ProneSweepsUnder61cm", QAMovement::Flags)
bool FQAMoveGapProneSweepsUnder61cm::RunTest(const FString& Parameters)
{
	return QAMovementStance::SweepGap(*this, QAM::ShippedTable(*this), ELureStance::Prone, 61.f, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapProneAt59cmMatchesData, "Project.Movement.QA.Gap.ProneAt59cmMatchesData", QAMovement::Flags)
bool FQAMoveGapProneAt59cmMatchesData::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::ShippedTable(*this);
	const float Needed = QAM::Clear(QAM::RowOf(QAM::Resolve(Table), ELureMovementState::Prone)) + QAM::MaxFloorDist;
	AddInfo(FString::Printf(TEXT("prone needs %.1f cm, so a 59 cm gap should %s"), Needed, Needed <= 59.f ? TEXT("fit") : TEXT("block")));
	return QAMovementStance::SweepGap(*this, Table, ELureStance::Prone, 59.f, Needed <= 59.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapProneBlockedBelowItsHeight, "Project.Movement.QA.Gap.ProneBlockedBelowItsHeight", QAMovement::Flags)
bool FQAMoveGapProneBlockedBelowItsHeight::RunTest(const FString& Parameters)
{
	// Sanity for the sweep method: a gap 1 cm lower than the prone capsule must block.
	UDataTable* Table = QAM::ShippedTable(*this);
	const float Gap = QAM::Clear(QAM::RowOf(QAM::Resolve(Table), ELureMovementState::Prone)) - QAM::BlockMargin;
	return QAMovementStance::SweepGap(*this, Table, ELureStance::Prone, Gap, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapProneWalksThrough60cm, "Project.Movement.QA.Gap.ProneWalksThrough60cm", QAMovement::Flags)
bool FQAMoveGapProneWalksThrough60cm::RunTest(const FString& Parameters)
{
	return QAMovementStance::WalkThroughGap(*this, QAM::ShippedTable(*this), ELureStance::Prone, 60.f, true, 600);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapCrouchSweepBlockedBy60cm, "Project.Movement.QA.Gap.CrouchSweepBlockedBy60cm", QAMovement::Flags)
bool FQAMoveGapCrouchSweepBlockedBy60cm::RunTest(const FString& Parameters)
{
	return QAMovementStance::SweepGap(*this, QAM::ShippedTable(*this), ELureStance::Crouch, 60.f, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapCrouchWalkStopsAt60cm, "Project.Movement.QA.Gap.CrouchWalkStopsAt60cm", QAMovement::Flags)
bool FQAMoveGapCrouchWalkStopsAt60cm::RunTest(const FString& Parameters)
{
	return QAMovementStance::WalkThroughGap(*this, QAM::ShippedTable(*this), ELureStance::Crouch, 60.f, false, 240);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapStandBlockedCrouchPassesCrouchGap, "Project.Movement.QA.Gap.StandBlockedCrouchPassesCrouchGap", QAMovement::Flags)
bool FQAMoveGapStandBlockedCrouchPassesCrouchGap::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::ShippedTable(*this);
	const float Gap = QAM::Clear(QAM::RowOf(QAM::Resolve(Table), ELureMovementState::Crouch)) + QAM::PassMargin;
	QAMovementStance::SweepGap(*this, Table, ELureStance::Stand, Gap, false);
	QAMovementStance::SweepGap(*this, Table, ELureStance::Crouch, Gap, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapStandRequestInsideGapThenExit, "Project.Movement.QA.Gap.StandRequestInsideGapThenExit", QAMovement::Flags)
bool FQAMoveGapStandRequestInsideGapThenExit::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	UDataTable* Table = QAM::ShippedTable(*this);
	ALurePlayerCharacter* Character = QAMovementStance::BeforeGap(*this, World, Table, ELureStance::Prone, QAM::CrawlGap);
	FHitResult Hit;
	if (!Character || !TestTrue(TEXT("crawled into the gap"), QAMovementStance::SweepTo(Character, 200.f, Hit)))
	{
		return false;
	}
	World.Tick(2);
	Character->RequestStance(ELureStance::Stand);
	World.Tick(5);
	TestEqual(TEXT("inside the gap: still prone"), QAM::StanceName(Character->GetStance()), FString(TEXT("Prone")));
	const float StandRadius = QAM::RowOf(QAM::Resolve(Table), ELureMovementState::Stand).CapsuleRadius;
	int32 Penetrations = 0;
	const bool bStood = World.TickUntil([&]()
	{
		Penetrations += QAM::IsPenetrating(Character) ? 1 : 0;
		return Character->GetStance() == ELureStance::Stand;
	}, 600, Character);
	TestTrue(FString::Printf(TEXT("stands up by itself after crawling out (A4 queue; x %.1f)"), Character->GetActorLocation().X), bStood);
	TestTrue(TEXT("not before the whole standing capsule is clear of the slab"), Character->GetActorLocation().X >= QAMovementStance::SlabMaxX + StandRadius - 1.f);
	TestEqual(TEXT("frames with penetration"), Penetrations, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveGapFixtureAProneFits60cm, "Project.Movement.QA.Gap.FixtureAProneFits60cm", QAMovement::Flags)
bool FQAMoveGapFixtureAProneFits60cm::RunTest(const FString& Parameters)
{
	// Same walk with other valid prone numbers (half height 27): separates a data problem from a code problem.
	return QAMovementStance::WalkThroughGap(*this, QAM::FixtureA(*this), ELureStance::Prone, 60.f, true, 600);
}

// =====================================================================================================================
// J: jump rules
// =====================================================================================================================

namespace QAMovementStance
{
	/** Presses jump every frame for Frames; returns true if the character ever left the ground. */
	bool TryJumping(QAM::FWorld& World, ALurePlayerCharacter* Character, int32 Frames, float& OutMaxRise)
	{
		const float StartZ = Character->GetActorLocation().Z;
		bool bAirborne = false;
		OutMaxRise = 0.f;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Character->Jump();
			World.Tick(1);
			Character->StopJumping();
			bAirborne |= QAM::IsFalling(Character);
			OutMaxRise = FMath::Max(OutMaxRise, static_cast<float>(Character->GetActorLocation().Z - StartZ));
		}
		return bAirborne;
	}

	bool CheckJump(FAutomationTestBase& Test, const UDataTable* Table, ELureStance Stance, bool bExpectJump)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = SpawnSettled(Test, World, Table);
		if (!Character || (Stance != ELureStance::Stand && !Test.TestTrue(TEXT("reached the stance"), QAM::EnterStance(World, Character, Stance))))
		{
			return false;
		}
		const FString Label = QAM::StanceName(Stance);
		Test.TestEqual(Label + TEXT(": CanJump()"), Character->CanJump(), bExpectJump);
		float Rise = 0.f;
		const bool bJumped = TryJumping(World, Character, bExpectJump ? 10 : 30, Rise);
		Test.TestEqual(FString::Printf(TEXT("%s: %s (rose %.1f cm)"), *Label, bExpectJump ? TEXT("jumps") : TEXT("does not jump"), Rise), bJumped, bExpectJump);
		if (bExpectJump)
		{
			Test.TestTrue(Label + TEXT(": rises more than 5 cm"), Rise > 5.f);
		}
		else
		{
			Test.TestTrue(Label + TEXT(": no hop at all"), Rise < 0.5f);
			Test.TestEqual(Label + TEXT(": stance unchanged"), QAM::StanceName(Character->GetStance()), Label);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpStandCanJump, "Project.Movement.QA.Jump.StandCanJump", QAMovement::Flags)
bool FQAMoveJumpStandCanJump::RunTest(const FString& Parameters)
{
	return QAMovementStance::CheckJump(*this, QAM::FixtureA(*this), ELureStance::Stand, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpSprintCanJump, "Project.Movement.QA.Jump.SprintCanJump", QAMovement::Flags)
bool FQAMoveJumpSprintCanJump::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	Character->SetSprintRequested(true);
	World.TickMoving(Character, 30);
	TestTrue(TEXT("sprinting"), Character->IsSprinting());
	TestTrue(TEXT("CanJump while sprinting"), Character->CanJump());
	Character->Jump();
	bool bAirborne = false;
	float Fastest = 0.f;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		World.TickMoving(Character, 1);
		Character->StopJumping();
		bAirborne |= QAM::IsFalling(Character);
		Fastest = FMath::Max(Fastest, QAM::HorizontalSpeed(Character));
	}
	TestTrue(TEXT("sprint jump leaves the ground"), bAirborne);
	TestTrue(FString::Printf(TEXT("no speed boost from the jump (%.1f <= 577 + 1%%)"), Fastest), Fastest <= 577.f * 1.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpCrouchJumpFollowsData, "Project.Movement.QA.Jump.CrouchJumpFollowsData", QAMovement::Flags)
bool FQAMoveJumpCrouchJumpFollowsData::RunTest(const FString& Parameters)
{
	// Fixture A: Crouch.CanJump = false; Fixture B: true (the engine refuses every crouched jump unless the code lets the data decide).
	QAMovementStance::CheckJump(*this, QAM::FixtureA(*this), ELureStance::Crouch, false);
	QAMovementStance::CheckJump(*this, QAM::FixtureB(*this), ELureStance::Crouch, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpShippedCrouchCanJump, "Project.Movement.QA.Jump.ShippedCrouchCanJump", QAMovement::Flags)
bool FQAMoveJumpShippedCrouchCanJump::RunTest(const FString& Parameters)
{
	// A14: crouch jump is allowed in the shipped tuning.
	return QAMovementStance::CheckJump(*this, QAM::ShippedTable(*this), ELureStance::Crouch, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpProneCannotJump, "Project.Movement.QA.Jump.ProneCannotJump", QAMovement::Flags)
bool FQAMoveJumpProneCannotJump::RunTest(const FString& Parameters)
{
	return QAMovementStance::CheckJump(*this, QAM::ShippedTable(*this), ELureStance::Prone, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpProneCannotJumpEvenIfDataSaysSo, "Project.Movement.QA.Jump.ProneCannotJumpEvenIfDataSaysSo", QAMovement::Flags)
bool FQAMoveJumpProneCannotJumpEvenIfDataSaysSo::RunTest(const FString& Parameters)
{
	// A13: a hard rule in code, whatever DT_Movement says.
	TArray<QAM::FRowSpec> Rows = QAM::FixtureARows();
	QAM::FRowSpec* Prone = QAM::FindSpec(Rows, TEXT("Prone"));
	Prone->bCanJump = true;
	Prone->JumpZ = 300.f;
	UDataTable* Table = QAM::MakeTable(*this, Rows);
	uint8 Mask = 0;
	QAM::Resolve(Table, &Mask);
	TestEqual(TEXT("the data row itself is accepted (the rule is in code, not a fallback)"), static_cast<int32>(Mask), 0);
	return QAMovementStance::CheckJump(*this, Table, ELureStance::Prone, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpPressWhileProneKeepsStance, "Project.Movement.QA.Jump.PressWhileProneKeepsStance", QAMovement::Flags)
bool FQAMoveJumpPressWhileProneKeepsStance::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character || !TestTrue(TEXT("prone"), QAM::EnterStance(World, Character, ELureStance::Prone)))
	{
		return false;
	}
	Character->DoJumpStart();
	World.Tick(5);
	Character->DoJumpEnd();
	World.Tick(5);
	TestEqual(TEXT("still prone (A15: a jump press does nothing)"), QAM::StanceName(Character->GetStance()), FString(TEXT("Prone")));
	TestEqual(TEXT("still wants prone"), QAM::StanceName(Character->GetRequestedStance()), FString(TEXT("Prone")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpNoProneMidAir, "Project.Movement.QA.Jump.NoProneMidAir", QAMovement::Flags)
bool FQAMoveJumpNoProneMidAir::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	Character->Jump();
	World.Tick(3);
	Character->StopJumping();
	if (!TestTrue(TEXT("airborne"), QAM::IsFalling(Character)))
	{
		return false;
	}
	Character->RequestStance(ELureStance::Prone);
	TestFalse(TEXT("prone request refused in the air"), Character->GetRequestedStance() == ELureStance::Prone);
	bool bProneInAir = false;
	World.TickUntil([Character, &bProneInAir]()
	{
		bProneInAir |= Character->IsProne() && QAM::IsFalling(Character);
		return QAM::IsOnGround(Character);
	}, 180);
	World.Tick(QAM::SettleFrames);
	TestFalse(TEXT("never prone in the air"), bProneInAir);
	TestEqual(TEXT("standing after landing (not queued)"), QAM::StanceName(Character->GetStance()), FString(TEXT("Stand")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpAndProneSameFrame, "Project.Movement.QA.Jump.JumpAndProneSameFrame", QAMovement::Flags)
bool FQAMoveJumpAndProneSameFrame::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	Character->Jump();
	Character->RequestStance(ELureStance::Prone);
	bool bProneInAir = false;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		World.Tick(1);
		Character->StopJumping();
		bProneInAir |= Character->IsProne() && QAM::IsFalling(Character);
	}
	AddInfo(FString::Printf(TEXT("jump + prone in one frame ends %s"), *QAM::StanceName(Character->GetStance())));
	TestFalse(TEXT("never prone in the air"), bProneInAir);
	TestTrue(TEXT("back on the ground"), QAM::IsOnGround(Character));
	TestFalse(TEXT("no penetration"), QAM::IsPenetrating(Character));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveJumpStanceKeptThroughLanding, "Project.Movement.QA.Jump.StanceKeptThroughLanding", QAMovement::Flags)
bool FQAMoveJumpStanceKeptThroughLanding::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementStance::SpawnSettled(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	Character->SetSprintRequested(true);
	World.TickMoving(Character, 20);
	Character->Jump();
	World.TickMoving(Character, 3);
	Character->StopJumping();
	World.TickUntil([Character]() { return QAM::IsOnGround(Character); }, 180, Character);
	World.TickMoving(Character, 5);
	TestEqual(TEXT("standing after the sprint jump"), QAM::StanceName(Character->GetStance()), FString(TEXT("Stand")));
	TestTrue(TEXT("still sprinting after landing (sprint held)"), Character->IsSprinting());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

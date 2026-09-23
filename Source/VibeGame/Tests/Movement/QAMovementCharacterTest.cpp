// Lure T-004 QA (qa-engineer): character composition and speeds.
// Project.Movement.QA.Character.*, .Speed.*  (QA design groups D, S)

#include "Tests/Movement/QAMovementTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Camera/CameraComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/PackageName.h"

namespace QAMovementCharacter
{
	/** Spawns on the floor with Table and puts the character in State (Sprint = standing + sprint wish + moving). */
	ALurePlayerCharacter* SpawnInState(FAutomationTestBase& Test, QAM::FWorld& World, const UDataTable* Table, ELureMovementState State, const FVector& Feet = FVector::ZeroVector)
	{
		ALurePlayerCharacter* Character = World.Spawn(Test, Feet, Table);
		if (!Character)
		{
			return nullptr;
		}
		World.Tick(QAM::SettleFrames);
		switch (State)
		{
		case ELureMovementState::Sprint:
			Character->SetSprintRequested(true);
			World.TickMoving(Character, QAM::SettleFrames);
			break;
		case ELureMovementState::Crouch:
			QAM::EnterStance(World, Character, ELureStance::Crouch);
			break;
		case ELureMovementState::Prone:
			QAM::EnterStance(World, Character, ELureStance::Prone);
			break;
		default:
			break;
		}
		return Character;
	}

	bool IsAttachedUnder(const USceneComponent* Child, const USceneComponent* Ancestor)
	{
		for (const USceneComponent* Parent = Child ? Child->GetAttachParent() : nullptr; Parent; Parent = Parent->GetAttachParent())
		{
			if (Parent == Ancestor)
			{
				return true;
			}
		}
		return false;
	}
}

// =====================================================================================================================
// D: character composition
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterCameraIsFirstPerson, "Project.Movement.QA.Character.CameraIsFirstPerson", QAMovement::Flags)
bool FQAMoveCharacterCameraIsFirstPerson::RunTest(const FString& Parameters)
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
	const UCameraComponent* Camera = Character->GetFirstPersonCamera();
	if (!TestNotNull(TEXT("first-person camera"), Camera))
	{
		return false;
	}
	TestTrue(TEXT("camera follows the control rotation (mouse look)"), Camera->bUsePawnControlRotation);
	TestTrue(TEXT("camera rides on the capsule"), QAMovementCharacter::IsAttachedUnder(Camera, Character->GetCapsuleComponent()));
	TArray<USpringArmComponent*> Booms;
	Character->GetComponents(Booms);
	for (const USpringArmComponent* Boom : Booms)
	{
		TestFalse(FString::Printf(TEXT("no third-person boom (%s arm length %.1f)"), *Boom->GetName(), Boom->TargetArmLength), Boom->TargetArmLength > 0.f);
	}
	// The eye must stay inside the collision capsule so the near plane can't poke through walls (prone radius is the tightest).
	const FVector2D Offset(Camera->GetComponentLocation() - Character->GetCapsuleComponent()->GetComponentLocation());
	const float ProneRadius = Character->GetLureMovement()->GetMovementRow(ELureMovementState::Prone).CapsuleRadius;
	TestTrue(FString::Printf(TEXT("camera %.1f cm off the capsule axis + near clip fits inside the prone radius %.1f"), Offset.Size(), ProneRadius), Offset.Size() + QAM::NearClip <= ProneRadius);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterRotationSetupIsFirstPerson, "Project.Movement.QA.Character.RotationSetupIsFirstPerson", QAMovement::Flags)
bool FQAMoveCharacterRotationSetupIsFirstPerson::RunTest(const FString& Parameters)
{
	const ALurePlayerCharacter* Defaults = GetDefault<ALurePlayerCharacter>();
	TestTrue(TEXT("body yaw follows the view"), Defaults->bUseControllerRotationYaw);
	TestFalse(TEXT("body does not pitch with the view"), Defaults->bUseControllerRotationPitch);
	TestFalse(TEXT("body does not roll with the view"), Defaults->bUseControllerRotationRoll);
	TestFalse(TEXT("no orient-to-movement (strafing must not turn the view)"), Defaults->GetCharacterMovement()->bOrientRotationToMovement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterStandEyeHeightFromData, "Project.Movement.QA.Character.StandEyeHeightFromData", QAMovement::Flags)
bool FQAMoveCharacterStandEyeHeightFromData::RunTest(const FString& Parameters)
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
	TestNearlyEqual(TEXT("camera 158 cm above the feet (Fixture A Stand.EyeHeight)"), QAM::EyeAboveFeet(Character), 158.f, 0.5f);
	TestNearlyEqual(TEXT("GetCurrentEyeHeight agrees"), Character->GetCurrentEyeHeight(), 158.f, 0.5f);
	QAM::TestCapsule(*this, Character, QAM::RowOf(QAM::Resolve(QAM::FixtureA(*this)), ELureMovementState::Stand), TEXT("spawned standing"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterArmsOwnerOnlyAndAttachedToCamera, "Project.Movement.QA.Character.ArmsOwnerOnlyAndAttachedToCamera", QAMovement::Flags)
bool FQAMoveCharacterArmsOwnerOnlyAndAttachedToCamera::RunTest(const FString& Parameters)
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
	const USkeletalMeshComponent* Arms = Character->GetFirstPersonArms();
	if (!TestNotNull(TEXT("FirstPersonArms"), Arms))
	{
		return false;
	}
	TestTrue(TEXT("only the owner sees the arms"), Arms->bOnlyOwnerSee);
	TestTrue(TEXT("arms follow the camera (pitch)"), QAMovementCharacter::IsAttachedUnder(Arms, Character->GetFirstPersonCamera()));
	TestEqual(TEXT("arms have no collision (never block the 60 cm gap or sweeps)"), static_cast<int32>(Arms->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
	TestFalse(TEXT("arms cast no shadow"), Arms->CastShadow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterArmsSoftRefPath, "Project.Movement.QA.Character.ArmsSoftRefPath", QAMovement::Flags)
bool FQAMoveCharacterArmsSoftRefPath::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("arms mesh path (SK_FPArms.anim.md)"), GetDefault<ALurePlayerCharacter>()->FirstPersonArmsMesh.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Art/Characters/FPArms/SK_FPArms.SK_FPArms")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterMissingArmsAssetIsNullSafe, "Project.Movement.QA.Character.MissingArmsAssetIsNullSafe", QAMovement::Flags)
bool FQAMoveCharacterMissingArmsAssetIsNullSafe::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, QAM::FixtureA(*this), true, [](ALurePlayerCharacter& C)
	{
		C.FirstPersonArmsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/QA/Nope/SK_Missing.SK_Missing")));
		C.FirstPersonArmsAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/QA/Nope/ABP_Missing.ABP_Missing_C")));
		C.StanceDipAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/QA/Nope/A_Missing.A_Missing")));
	});
	if (!Character)
	{
		return false;
	}
	World.Tick(30);
	TestNull(TEXT("no arms mesh when the asset is missing"), Character->GetFirstPersonArms()->GetSkeletalMeshAsset());

	// A local player drives it (arms bob and stance dips run for local players only).
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("player controller"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.TickMoving(Character, 20);
	QAM::EnterStance(World, Character, ELureStance::Crouch);
	QAM::EnterStance(World, Character, ELureStance::Prone);
	QAM::EnterStance(World, Character, ELureStance::Stand);
	TestFalse(TEXT("PlayStanceDip is a no-op without the clip"), Character->PlayStanceDip(1.f));
	Controller->UnPossess();
	World.Tick(5);
	Character->Destroy();
	World.Tick(5);
	return true; // any Error log or crash fails the test
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterArmsLoadWhenAssetPresent, "Project.Movement.QA.Character.ArmsLoadWhenAssetPresent", QAMovement::Flags)
bool FQAMoveCharacterArmsLoadWhenAssetPresent::RunTest(const FString& Parameters)
{
	const FSoftObjectPath Path = GetDefault<ALurePlayerCharacter>()->FirstPersonArmsMesh.ToSoftObjectPath();
	if (!FPackageName::DoesPackageExist(Path.GetLongPackageName()))
	{
		AddInfo(FString::Printf(TEXT("%s is not imported in this checkout: nothing to load."), *Path.ToString()));
		return true;
	}
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
	World.TickUntil([Character]() { return Character->GetFirstPersonArms()->GetSkeletalMeshAsset() != nullptr; }, 60);
	const USkeletalMesh* Loaded = Cast<USkeletalMesh>(Path.TryLoad());
	TestNotNull(TEXT("SK_FPArms loads as a skeletal mesh"), Loaded);
	TestTrue(TEXT("the arms component shows SK_FPArms"), Loaded && Character->GetFirstPersonArms()->GetSkeletalMeshAsset() == Loaded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterPlaceholderBodyFollowsStance, "Project.Movement.QA.Character.PlaceholderBodyFollowsStance", QAMovement::Flags)
bool FQAMoveCharacterPlaceholderBodyFollowsStance::RunTest(const FString& Parameters)
{
	// A18: other players see an olive cylinder the size of the capsule; its owner never does.
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
	const UStaticMeshComponent* Body = Character->GetPlaceholderBody();
	if (!TestNotNull(TEXT("placeholder body"), Body))
	{
		return false;
	}
	TestTrue(TEXT("owner never sees the body"), Body->bOwnerNoSee);
	TestFalse(TEXT("body is not owner-only"), Body->bOnlyOwnerSee);
	TestTrue(TEXT("body is visible to others"), Body->IsVisible());
	TestNotNull(TEXT("body has a mesh"), Body->GetStaticMesh().Get());
	TestEqual(TEXT("body has no collision"), static_cast<int32>(Body->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
	TestTrue(TEXT("third-person mesh slot hidden from its owner"), Character->GetMesh()->bOwnerNoSee);

	const TArray<FLureMovementRow> Rows = QAM::Resolve(Table);
	for (const ELureStance Stance : { ELureStance::Stand, ELureStance::Crouch, ELureStance::Prone, ELureStance::Stand })
	{
		QAM::EnterStance(World, Character, Stance);
		const FLureMovementRow& Row = QAM::RowOf(Rows, QAM::StateOf(Stance));
		const FBox Bounds = Body->Bounds.GetBox();
		const FString Label = QAM::StanceName(Stance);
		TestNearlyEqual(Label + TEXT(": body height == capsule height"), static_cast<float>(Bounds.GetSize().Z), QAM::Clear(Row), 1.f);
		TestNearlyEqual(Label + TEXT(": body bottom at the feet"), static_cast<float>(Bounds.Min.Z), QAM::FeetZ(Character), 1.f);
		TestNearlyEqual(Label + TEXT(": body width == capsule width"), static_cast<float>(Bounds.GetSize().X), 2.f * Row.CapsuleRadius, 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterNonLocalPossessionIsSafe, "Project.Movement.QA.Character.NonLocalPossessionIsSafe", QAMovement::Flags)
bool FQAMoveCharacterNonLocalPossessionIsSafe::RunTest(const FString& Parameters)
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

	// A remote player's pawn on a server: a PlayerController without a local player.
	APlayerController* Remote = World.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("remote player controller"), Remote))
	{
		return false;
	}
	Remote->Possess(Character);
	World.Tick(10);
	TestTrue(TEXT("possessed by the remote controller"), Character->GetController() == Remote);
	AddInfo(FString::Printf(TEXT("input component after non-local possession: %s"), Character->InputComponent ? *Character->InputComponent->GetClass()->GetName() : TEXT("none")));
	Remote->UnPossess();
	World.Tick(2);

	// An AI controller (local on the server): stances still work.
	AAIController* AI = World.World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("AI controller"), AI))
	{
		return false;
	}
	AI->Possess(Character);
	World.Tick(5);
	TestTrue(TEXT("AI-possessed character goes prone"), QAM::EnterStance(World, Character, ELureStance::Prone));
	AI->UnPossess();
	World.Tick(2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterTwoCharactersIndependent, "Project.Movement.QA.Character.TwoCharactersIndependent", QAMovement::Flags)
bool FQAMoveCharacterTwoCharactersIndependent::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	UDataTable* Table = QAM::FixtureA(*this);
	ALurePlayerCharacter* A = World.Spawn(*this, FVector(0.f, 0.f, 0.f), Table);
	ALurePlayerCharacter* B = World.Spawn(*this, FVector(0.f, 600.f, 0.f), Table);
	if (!A || !B)
	{
		return false;
	}
	World.Tick(QAM::SettleFrames);
	A->RequestStance(ELureStance::Prone);
	A->SetSprintRequested(true);
	World.TickMoving(A, 30);
	TestEqual(TEXT("A is prone"), A->GetStance(), ELureStance::Prone);
	TestEqual(TEXT("B still stands"), B->GetStance(), ELureStance::Stand);
	TestFalse(TEXT("B is not sprinting"), B->IsSprinting());
	TestFalse(TEXT("B has no sprint wish"), B->GetLureMovement()->IsSprintRequested());
	QAM::TestCapsule(*this, B, QAM::RowOf(QAM::Resolve(Table), ELureMovementState::Stand), TEXT("B"));
	TestNearlyEqual(TEXT("B max speed = Stand"), QAM::MaxSpeedNow(B), 311.f, 0.01f);
	TestNearlyEqual(TEXT("B eye = Stand"), QAM::EyeAboveFeet(B), 158.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterDestroyMidTransitionIsSafe, "Project.Movement.QA.Character.DestroyMidTransitionIsSafe", QAMovement::Flags)
bool FQAMoveCharacterDestroyMidTransitionIsSafe::RunTest(const FString& Parameters)
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
	Character->RequestStance(ELureStance::Prone);
	World.Tick(12); // Prone TransitionTime 0.4 s: mid-blend
	TestTrue(TEXT("camera is mid-transition"), QAM::EyeAboveFeet(Character) > 34.f);
	TestTrue(TEXT("destroyed"), Character->Destroy());
	World.Tick(30);
	return true; // a crash or an Error log fails the test
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveCharacterReplicationFlags, "Project.Movement.QA.Character.ReplicationFlags", QAMovement::Flags)
bool FQAMoveCharacterReplicationFlags::RunTest(const FString& Parameters)
{
	const ALurePlayerCharacter* Defaults = GetDefault<ALurePlayerCharacter>();
	TestTrue(TEXT("character replicates"), Defaults->GetIsReplicated());
	TestTrue(TEXT("character replicates movement"), Defaults->IsReplicatingMovement());
	TestFalse(TEXT("arms are cosmetic (not replicated)"), Defaults->GetFirstPersonArms()->GetIsReplicated());
	return true;
}

// =====================================================================================================================
// S: speeds (Fixture A: Stand 311, Sprint 577, Crouch 173, Prone 89)
// =====================================================================================================================

namespace QAMovementCharacter
{
	bool CheckMaxSpeed(FAutomationTestBase& Test, ELureMovementState State, float Expected)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = SpawnInState(Test, World, QAM::FixtureA(Test), State);
		if (!Character)
		{
			return false;
		}
		Test.TestEqual(TEXT("row in use"), static_cast<int32>(Character->GetLureMovement()->GetMovementState()), static_cast<int32>(State));
		Test.TestNearlyEqual(FString::Printf(TEXT("%s GetMaxSpeed from DT_Movement"), *QAM::StateName(State)), QAM::MaxSpeedNow(Character), Expected, 0.01f);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedStandMaxSpeedFromData, "Project.Movement.QA.Speed.StandMaxSpeedFromData", QAMovement::Flags)
bool FQAMoveSpeedStandMaxSpeedFromData::RunTest(const FString& Parameters)
{
	return QAMovementCharacter::CheckMaxSpeed(*this, ELureMovementState::Stand, 311.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedSprintMaxSpeedFromData, "Project.Movement.QA.Speed.SprintMaxSpeedFromData", QAMovement::Flags)
bool FQAMoveSpeedSprintMaxSpeedFromData::RunTest(const FString& Parameters)
{
	return QAMovementCharacter::CheckMaxSpeed(*this, ELureMovementState::Sprint, 577.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedCrouchMaxSpeedFromData, "Project.Movement.QA.Speed.CrouchMaxSpeedFromData", QAMovement::Flags)
bool FQAMoveSpeedCrouchMaxSpeedFromData::RunTest(const FString& Parameters)
{
	return QAMovementCharacter::CheckMaxSpeed(*this, ELureMovementState::Crouch, 173.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedProneMaxSpeedFromData, "Project.Movement.QA.Speed.ProneMaxSpeedFromData", QAMovement::Flags)
bool FQAMoveSpeedProneMaxSpeedFromData::RunTest(const FString& Parameters)
{
	return QAMovementCharacter::CheckMaxSpeed(*this, ELureMovementState::Prone, 89.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedReachedSpeedMatchesMax, "Project.Movement.QA.Speed.ReachedSpeedMatchesMax", QAMovement::Flags)
bool FQAMoveSpeedReachedSpeedMatchesMax::RunTest(const FString& Parameters)
{
	const TPair<ELureMovementState, float> Cases[] = {
		{ ELureMovementState::Stand, 311.f }, { ELureMovementState::Sprint, 577.f }, { ELureMovementState::Crouch, 173.f }, { ELureMovementState::Prone, 89.f } };
	for (const TPair<ELureMovementState, float>& Case : Cases)
	{
		QAM::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = QAMovementCharacter::SpawnInState(*this, World, QAM::FixtureA(*this), Case.Key);
		if (!Character)
		{
			return false;
		}
		float Highest = 0.f;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			World.TickMoving(Character, 1);
			Highest = FMath::Max(Highest, QAM::HorizontalSpeed(Character));
		}
		const FString Label = QAM::StateName(Case.Key);
		TestNearlyEqual(Label + TEXT(": speed after 2 s of full input"), QAM::HorizontalSpeed(Character), Case.Value, Case.Value * 0.01f);
		TestTrue(FString::Printf(TEXT("%s: never faster than MaxSpeed + 1%% (peak %.1f)"), *Label, Highest), Highest <= Case.Value * 1.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedSprintReleaseReturnsToWalk, "Project.Movement.QA.Speed.SprintReleaseReturnsToWalk", QAMovement::Flags)
bool FQAMoveSpeedSprintReleaseReturnsToWalk::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementCharacter::SpawnInState(*this, World, QAM::FixtureA(*this), ELureMovementState::Sprint);
	if (!Character)
	{
		return false;
	}
	World.TickMoving(Character, 60);
	TestTrue(TEXT("sprinting at speed"), QAM::HorizontalSpeed(Character) > 500.f);
	Character->SetSprintRequested(false);
	World.TickMoving(Character, 1);
	TestFalse(TEXT("sprint ends at once"), Character->IsSprinting());
	TestNearlyEqual(TEXT("max speed back to Stand at once"), QAM::MaxSpeedNow(Character), 311.f, 0.01f);
	World.TickMoving(Character, 60);
	TestTrue(FString::Printf(TEXT("slowed to walk speed within 1 s (%.1f)"), QAM::HorizontalSpeed(Character)), QAM::HorizontalSpeed(Character) <= 311.f * 1.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedSwappingTableChangesSpeeds, "Project.Movement.QA.Speed.SwappingTableChangesSpeeds", QAMovement::Flags)
bool FQAMoveSpeedSwappingTableChangesSpeeds::RunTest(const FString& Parameters)
{
	const TPair<ELureMovementState, float> Cases[] = {
		{ ELureMovementState::Stand, 402.f }, { ELureMovementState::Sprint, 650.f }, { ELureMovementState::Crouch, 150.f }, { ELureMovementState::Prone, 70.f } };
	for (const TPair<ELureMovementState, float>& Case : Cases)
	{
		QAM::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = QAMovementCharacter::SpawnInState(*this, World, QAM::FixtureB(*this), Case.Key);
		if (!Character)
		{
			return false;
		}
		TestNearlyEqual(FString::Printf(TEXT("Fixture B %s max speed"), *QAM::StateName(Case.Key)), QAM::MaxSpeedNow(Character), Case.Value, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedSprintHeldWhileStandingStill, "Project.Movement.QA.Speed.SprintHeldWhileStandingStill", QAMovement::Flags)
bool FQAMoveSpeedSprintHeldWhileStandingStill::RunTest(const FString& Parameters)
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
	Character->SetSprintRequested(true);
	World.Tick(30);
	TestTrue(TEXT("not moving"), QAM::HorizontalSpeed(Character) < 1.f);
	TestFalse(TEXT("holding sprint while standing still is not sprinting (A2)"), Character->IsSprinting());
	TestNearlyEqual(TEXT("noise multiplier stays at the Stand value"), Character->GetLureMovement()->GetStanceNoiseMultiplier(), 1.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveSpeedNoiseMultiplierPerStance, "Project.Movement.QA.Speed.NoiseMultiplierPerStance", QAMovement::Flags)
bool FQAMoveSpeedNoiseMultiplierPerStance::RunTest(const FString& Parameters)
{
	const TPair<ELureMovementState, float> Cases[] = {
		{ ELureMovementState::Stand, 1.0f }, { ELureMovementState::Sprint, 1.7f }, { ELureMovementState::Crouch, 0.5f }, { ELureMovementState::Prone, 0.2f } };
	for (const TPair<ELureMovementState, float>& Case : Cases)
	{
		QAM::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = QAMovementCharacter::SpawnInState(*this, World, QAM::FixtureA(*this), Case.Key);
		if (!Character)
		{
			return false;
		}
		TestNearlyEqual(FString::Printf(TEXT("%s noise multiplier"), *QAM::StateName(Case.Key)), Character->GetLureMovement()->GetStanceNoiseMultiplier(), Case.Value, 0.001f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure: tests for the T-004 movement playtest fixes (report Saved/AgentLogs/playtest/20260923-003511-T004-movement):
// B1 the climb rule, B2 getting up next to walls, B3 prone arms in walls, getting up from prone, crawling off a ledge.
// Tables come from data/tables/DT_Movement.csv (fixtures edit rows in memory).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GCObjectScopeGuard.h"

namespace LurePlaytestFixTest
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** T-004 playtest: the prone hands sit this far in front of the eye (measured in game), cm. */
	constexpr float ProneHandReach = 27.6f;
	/** Default camera near clip plane, cm. */
	constexpr float NearClip = 10.f;

	UDataTable* ShippedTable(FAutomationTestBase& Test)
	{
		FString Csv;
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv"));
		if (!Test.TestTrue(TEXT("data/tables/DT_Movement.csv loads"), FFileHelper::LoadFileToString(Csv, *Path)))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureMovementRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		Test.TestEqual(FString::Printf(TEXT("CSV import problems (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		return Table;
	}

	FLureMovementRow* EditRow(UDataTable* Table, ELureMovementState State)
	{
		return Table ? Table->FindRow<FLureMovementRow>(FLureMovementData::GetRowName(State), TEXT("LurePlaytestFixTest"), false) : nullptr;
	}

	FLureMovementRow RowOf(const UDataTable* Table, ELureMovementState State)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Table, Rows, Problems);
		return Rows[static_cast<int32>(State)];
	}

	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test, bool bFloor = true)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			if (bFloor)
			{
				AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
			}
			return World != nullptr;
		}

		AActor* AddBox(const FVector& Center, const FVector& Extent, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
			Box->SetMobility(EComponentMobility::Static);
			Box->SetBoxExtent(Extent, false);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->SetRelativeLocation_Direct(Center);
			Box->SetRelativeRotation_Direct(Rotation);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			return Actor;
		}

		/** A block (crate, ledge) of Height on the floor, its near face at x = FaceX, 6 m deep. Bevel > 0 chamfers the top front edge at 45 degrees. */
		void AddBlock(float FaceX, float Height, float Bevel = 0.f)
		{
			const float Depth = 600.f;
			if (Bevel <= 0.f)
			{
				AddBox(FVector(FaceX + 0.5f * Depth, 0.f, 0.5f * Height), FVector(0.5f * Depth, 300.f, 0.5f * Height));
				return;
			}
			AddBox(FVector(FaceX + Bevel + 0.5f * (Depth - Bevel), 0.f, 0.5f * Height), FVector(0.5f * (Depth - Bevel), 300.f, 0.5f * Height));
			AddBox(FVector(FaceX + 0.5f * Bevel, 0.f, 0.5f * (Height - Bevel)), FVector(0.5f * Bevel, 300.f, 0.5f * (Height - Bevel)));
			const float Half = Bevel / UE_SQRT_2; // a square turned 45 degrees: its corners Bevel from the center, one side is the chamfer
			AddBox(FVector(FaceX + Bevel, 0.f, Height - Bevel), FVector(Half, 300.f, Half), FRotator(45.f, 0.f, 0.f));
		}

		ALurePlayerCharacter* Spawn(const FVector& Feet, const UDataTable* Table, float Yaw = 0.f)
		{
			const float HalfHeight = RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight;
			const FTransform Transform(FRotator(0.f, Yaw, 0.f), Feet + FVector(0.f, 0.f, HalfHeight + 2.15f));
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character)
			{
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Table);
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			Character->FinishSpawning(Transform);
			return Character;
		}

		void Tick(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(Dt);
			}
		}

		void TickMoving(ALurePlayerCharacter* Character, int32 Frames, const FVector& Direction = FVector::ForwardVector)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Character->AddMovementInput(Direction, 1.f, true);
				Wrapper.TickTestWorld(Dt);
			}
		}
	};

	float FeetZ(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetCapsuleComponent()->GetComponentLocation().Z) - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	float CameraZ(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetFirstPersonCamera()->GetComponentLocation().Z);
	}

	bool IsPenetrating(const ALurePlayerCharacter* Character)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LurePlaytestFixOverlap), false, Character);
		return Character->GetWorld()->OverlapBlockingTestByChannel(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(), ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
	}

	struct FJumpCase
	{
		const TCHAR* Label;
		float Height;
		float Bevel;
		bool bSprint;
		bool bCrouch;
		bool bExpectOnTop;
	};

	/** Walks (or sprints) at a block, jumps next to it while holding forward, and checks where the character ends up. */
	bool RunJumpCase(FAutomationTestBase& Test, UDataTable* Table, const FJumpCase& Case)
	{
		FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		const float FaceX = 100.f;
		World.AddBlock(FaceX, Case.Height, Case.Bevel);
		const float Radius = RowOf(Table, ELureMovementState::Stand).CapsuleRadius;
		ALurePlayerCharacter* Character = World.Spawn(FVector(Case.bSprint ? FaceX - 500.f : FaceX - Radius - 12.f, 0.f, 0.f), Table);
		if (!Test.TestNotNull(TEXT("character spawns"), Character))
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		World.Tick(10);
		const FString Label = Case.Label;
		if (Case.bCrouch)
		{
			Character->RequestStance(ELureStance::Crouch);
			World.Tick(20);
			Test.TestEqual(Label + TEXT(": crouched"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Crouch));
		}
		if (Case.bSprint)
		{
			// Run up and jump about 45 cm before the face.
			Character->SetSprintRequested(true);
			for (int32 Frame = 0; Frame < 200 && Character->GetActorLocation().X < FaceX - Radius - 45.f; ++Frame)
			{
				World.TickMoving(Character, 1);
			}
			Test.TestTrue(Label + TEXT(": sprinting at the jump"), Character->IsSprinting());
		}
		else
		{
			World.TickMoving(Character, 3);
		}

		Character->Jump();
		bool bLedgeClimb = false;
		int32 GroundFrames = 0;
		for (int32 Frame = 0; Frame < 180 && GroundFrames < 20; ++Frame)
		{
			// Like a player: keep pressing into the ledge through the jump and for a moment after landing (a landing on the
			// ledge's corner then steps up), then let go.
			GroundFrames = (Movement->IsMovingOnGround() && Frame > 5) ? GroundFrames + 1 : 0;
			Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
			World.Tick(1);
			bLedgeClimb |= Movement->IsLedgeClimbing();
		}
		Character->SetSprintRequested(false);
		World.Tick(20);

		const bool bOnTop = Movement->IsMovingOnGround() && FMath::Abs(FeetZ(Character) - Case.Height) <= 3.f && Character->GetActorLocation().X > FaceX;
		Test.AddInfo(FString::Printf(TEXT("%s: feet %.1f, x %.1f, %s%s"), *Label, FeetZ(Character), Character->GetActorLocation().X,
			bOnTop ? TEXT("on top") : TEXT("below"), bLedgeClimb ? TEXT(" (pulled up by the jump climb)") : TEXT("")));
		Test.TestEqual(Label + TEXT(": ends up on top of the block"), bOnTop, Case.bExpectOnTop);
		if (!Case.bExpectOnTop)
		{
			Test.TestTrue(Label + TEXT(": back on the floor in front of it"), Movement->IsMovingOnGround() && FeetZ(Character) < 5.f && Character->GetActorLocation().X < FaceX);
		}
		Test.TestFalse(Label + TEXT(": no penetration"), IsPenetrating(Character));
		if (Case.bCrouch && Case.bExpectOnTop)
		{
			Test.TestEqual(Label + TEXT(": still crouched on top"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Crouch));
		}
		return true;
	}

	/** Goes prone on open floor, puts walls flush against the prone capsule, then asks for Target. */
	bool GetUpNextToWalls(FAutomationTestBase& Test, UDataTable* Table, bool bCorner, ELureStance Target, const FString& Label)
	{
		FWorld World;
		if (!World.Create(Test))
		{
			return false;
		}
		ALurePlayerCharacter* Character = World.Spawn(FVector::ZeroVector, Table);
		if (!Test.TestNotNull(TEXT("character spawns"), Character))
		{
			return false;
		}
		World.Tick(10);
		Character->RequestStance(ELureStance::Prone);
		World.Tick(30);
		if (!Test.TestEqual(Label + TEXT(": prone"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Prone)))
		{
			return false;
		}
		const float ProneRadius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const FVector Axis = Character->GetActorLocation();
		const float Gap = 0.2f; // flush: the walls touch the prone capsule
		World.AddBox(FVector(Axis.X + ProneRadius + Gap + 50.f, 0.f, 300.f), FVector(50.f, 500.f, 300.f));
		if (bCorner)
		{
			World.AddBox(FVector(0.f, Axis.Y + ProneRadius + Gap + 50.f, 300.f), FVector(500.f, 50.f, 300.f));
		}
		World.Tick(2);
		Test.TestFalse(Label + TEXT(": setup: the walls don't cut into the prone capsule"), IsPenetrating(Character));

		Character->RequestStance(Target);
		World.Tick(20);
		const float NewRadius = RowOf(Table, Target == ELureStance::Crouch ? ELureMovementState::Crouch : ELureMovementState::Stand).CapsuleRadius;
		const float Pushed = static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), Axis));
		Test.TestEqual(Label + TEXT(": got up"), static_cast<int32>(Character->GetStance()), static_cast<int32>(Target));
		Test.TestFalse(Label + TEXT(": no penetration"), IsPenetrating(Character));
		Test.TestTrue(FString::Printf(TEXT("%s: pushed %.2f cm, within the nudge limit"), *Label, Pushed),
			Pushed <= Character->GetLureMovement()->GetStanceNudgeLimit(NewRadius, ProneRadius) + 0.5f);
		return true;
	}
}

using namespace LurePlaytestFixTest;

// ---------------------------------------------------------------------------------------------------------------------
// B1: one climb rule
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureClimbRuleTest, "Project.Movement.Climb.JumpOntoLedgesUpToClimbMaxHeight", LurePlaytestFixTest::Flags)

bool FLureClimbRuleTest::RunTest(const FString& Parameters)
{
	UDataTable* Shipped = ShippedTable(*this);
	FGCObjectScopeGuard KeepShipped(Shipped); // test worlds come and go (and collect garbage) while it is in use
	if (!Shipped)
	{
		return false;
	}
	TestNearlyEqual(TEXT("the shipped rule: 100 cm"), RowOf(Shipped, ELureMovementState::Stand).ClimbMaxHeight, 100.f);

	const FJumpCase Cases[] = {
		{ TEXT("stand jump, 100 cm block"), 100.f, 0.f, false, false, true },
		{ TEXT("stand jump, 100 cm crate with a bevelled edge"), 100.f, 10.f, false, false, true },
		{ TEXT("sprint jump, 100 cm block"), 100.f, 0.f, true, false, true },
		{ TEXT("crouch jump, 100 cm block"), 100.f, 0.f, false, true, true },
		{ TEXT("stand jump, 60 cm block"), 60.f, 0.f, false, false, true },
		{ TEXT("stand jump, 120 cm block"), 120.f, 0.f, false, false, false },
		{ TEXT("sprint jump, 120 cm block"), 120.f, 0.f, true, false, false },
		{ TEXT("sprint jump, 105 cm block"), 105.f, 0.f, true, false, false },
	};
	for (const FJumpCase& Case : Cases)
	{
		RunJumpCase(*this, Shipped, Case);
	}

	// The number is data: an 80 cm rule refuses the 100 cm block and allows an 80 cm one.
	UDataTable* Fixture = ShippedTable(*this);
	FGCObjectScopeGuard KeepFixture(Fixture); // test worlds come and go (and collect garbage) while it is in use
	for (const ELureMovementState State : { ELureMovementState::Stand, ELureMovementState::Sprint, ELureMovementState::Crouch })
	{
		EditRow(Fixture, State)->ClimbMaxHeight = 80.f;
	}
	RunJumpCase(*this, Fixture, { TEXT("rule 80: 100 cm block"), 100.f, 0.f, false, false, false });
	RunJumpCase(*this, Fixture, { TEXT("rule 80: 80 cm block"), 80.f, 0.f, false, false, true });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureJumpClimbAssistTest, "Project.Movement.Climb.JumpClimbPullsUpOntoAReachedLedge", LurePlaytestFixTest::Flags)

bool FLureJumpClimbAssistTest::RunTest(const FString& Parameters)
{
	// The pull-up for ledges the capsule can't land on by itself: after a jump, on the way down, pressing into the face
	// of a ledge whose top is above the feet but within reach (the capsule radius) and within the climb rule.
	struct FCase
	{
		const TCHAR* Label;
		float TopAboveFeet;
		float InputSign;	// +1 into the ledge, -1 away
		bool bJumped;
		bool bExpectClimb;
	};
	const FCase Cases[] = {
		{ TEXT("top 20 cm above the feet, pressing in"), 20.f, 1.f, true, true },
		{ TEXT("top 30 cm above the feet, pressing in"), 30.f, 1.f, true, true },
		{ TEXT("top 40 cm above the feet: out of reach"), 40.f, 1.f, true, false },
		{ TEXT("pressing away"), 20.f, -1.f, true, false },
		{ TEXT("falling without a jump"), 20.f, 1.f, false, false },
	};
	for (const FCase& Case : Cases)
	{
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
		FWorld World;
		if (!Table || !World.Create(*this))
		{
			return false;
		}
		const float FaceX = 100.f;
		const float LedgeTop = 100.f;
		World.AddBlock(FaceX, LedgeTop);
		const FLureMovementRow Stand = RowOf(Table, ELureMovementState::Stand);
		ALurePlayerCharacter* Character = World.Spawn(FVector(FaceX - Stand.CapsuleRadius - 1.f, 0.f, 0.f), Table);
		if (!TestNotNull(TEXT("character spawns"), Character))
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		World.Tick(10);

		// Put it in the air against the face, on its way down.
		const float Feet = LedgeTop - Case.TopAboveFeet;
		Character->SetActorLocation(FVector(FaceX - Stand.CapsuleRadius - 1.f, 0.f, Feet + Stand.CapsuleHalfHeight));
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = FVector(0.f, 0.f, -20.f);
		Character->JumpCurrentCount = Case.bJumped ? 1 : 0;

		bool bClimbed = false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Character->AddMovementInput(FVector::ForwardVector * Case.InputSign, 1.f, true);
			World.Tick(1);
			bClimbed |= Movement->IsLedgeClimbing();
			if (Movement->IsMovingOnGround())
			{
				break;
			}
		}
		World.Tick(20);
		const bool bOnTop = Movement->IsMovingOnGround() && FMath::Abs(FeetZ(Character) - LedgeTop) <= 3.f;
		TestEqual(FString::Printf(TEXT("%s: pulled up"), Case.Label), bClimbed, Case.bExpectClimb);
		if (Case.bExpectClimb)
		{
			TestTrue(FString::Printf(TEXT("%s: standing on the ledge (feet %.1f)"), Case.Label, FeetZ(Character)), bOnTop);
			TestTrue(FString::Printf(TEXT("%s: past the edge"), Case.Label), Character->GetActorLocation().X > FaceX);
		}
		TestFalse(FString::Printf(TEXT("%s: no penetration"), Case.Label), IsPenetrating(Character));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// B2: getting up from prone flush against walls
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureProneAgainstWallsTest, "Project.Movement.Stance.ProneFlushAgainstWallsCanGetUp", LurePlaytestFixTest::Flags)

bool FLureProneAgainstWallsTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	if (!Table)
	{
		return false;
	}
	const float StandRadius = RowOf(Table, ELureMovementState::Stand).CapsuleRadius;
	const float ProneRadius = RowOf(Table, ELureMovementState::Prone).CapsuleRadius;
	const float Limit = GetDefault<ULureCharacterMovementComponent>()->GetStanceNudgeLimit(StandRadius, ProneRadius);
	TestTrue(FString::Printf(TEXT("the nudge limit (%.1f cm) covers a corner (%.1f cm)"), Limit, UE_SQRT_2 * (StandRadius - ProneRadius)), Limit >= UE_SQRT_2 * (StandRadius - ProneRadius) + 0.5f);
	TestTrue(TEXT("the limit follows the data (a wider stand capsule raises it)"),
		GetDefault<ULureCharacterMovementComponent>()->GetStanceNudgeLimit(StandRadius + 20.f, ProneRadius) >= UE_SQRT_2 * (StandRadius + 20.f - ProneRadius));

	GetUpNextToWalls(*this, Table, false, ELureStance::Stand, TEXT("wall, stand"));
	GetUpNextToWalls(*this, Table, false, ELureStance::Crouch, TEXT("wall, crouch"));
	GetUpNextToWalls(*this, Table, true, ELureStance::Stand, TEXT("corner, stand"));
	GetUpNextToWalls(*this, Table, true, ELureStance::Crouch, TEXT("corner, crouch"));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// B3: prone arms stay out of walls
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureProneArmsTest, "Project.Movement.Camera.ProneArmsPulledBackFromWalls", LurePlaytestFixTest::Flags)

bool FLureProneArmsTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FWorld World;
	if (!Table || !World.Create(*this))
	{
		return false;
	}
	const FLureMovementRow Prone = RowOf(Table, ELureMovementState::Prone);
	ALurePlayerCharacter* Player = World.Spawn(FVector::ZeroVector, Table);
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("character and controller spawn"), Player) || !Controller)
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);

	// The rule: a wall touching the prone capsule is ProneRadius from the eye. The hands (27.6 cm ahead in the playtest)
	// must end in front of it with 2 cm to spare, and stay beyond the near clip plane, with or without the camera's
	// first-person scale applied to the pull-back.
	for (const float Scale : { Player->GetFirstPersonCamera()->FirstPersonScale, 1.f })
	{
		const float Hands = ProneHandReach - Prone.ArmsPullBack * Scale;
		TestTrue(FString::Printf(TEXT("scale %.1f: hands %.1f cm ahead, a wall is %.1f cm away"), Scale, Hands, Prone.CapsuleRadius), Hands <= Prone.CapsuleRadius - 2.f);
		TestTrue(FString::Printf(TEXT("scale %.1f: hands %.1f cm ahead, beyond the %.0f cm near clip"), Scale, Hands, NearClip), Hands >= NearClip + 2.f);
	}

	World.Tick(20);
	TestNearlyEqual(TEXT("standing: arms in place"), static_cast<float>(Player->GetArmsBobOffset().GetLocation().X), RowOf(Table, ELureMovementState::Stand).ArmsPullBack, 0.2f);
	Player->RequestStance(ELureStance::Prone);
	World.Tick(90);
	TestEqual(TEXT("prone"), static_cast<int32>(Player->GetStance()), static_cast<int32>(ELureStance::Prone));
	TestNearlyEqual(TEXT("prone: arms pulled back by the row's ArmsPullBack"), static_cast<float>(Player->GetArmsBobOffset().GetLocation().X), -Prone.ArmsPullBack, 0.2f);
	Player->RequestStance(ELureStance::Stand);
	World.Tick(90);
	TestNearlyEqual(TEXT("standing again: arms back"), static_cast<float>(Player->GetArmsBobOffset().GetLocation().X), 0.f, 0.2f);
	Controller->UnPossess();
	World.Tick(2);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Feel: getting up from prone; crawling off a ledge
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureProneExitTimeTest, "Project.Movement.Camera.GettingUpFromProneUsesExitTime", LurePlaytestFixTest::Flags)

bool FLureProneExitTimeTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Label;
		float ExitTime; // < 0: the shipped value
	};
	for (const FCase& Case : { FCase{ TEXT("shipped"), -1.f }, FCase{ TEXT("exit time 0 = the Stand row's time"), 0.f } })
	{
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
		FWorld World;
		if (!Table || !World.Create(*this))
		{
			return false;
		}
		if (Case.ExitTime >= 0.f)
		{
			EditRow(Table, ELureMovementState::Prone)->ExitTransitionTime = Case.ExitTime;
		}
		const FLureMovementRow Prone = RowOf(Table, ELureMovementState::Prone);
		const FLureMovementRow Stand = RowOf(Table, ELureMovementState::Stand);
		const float Duration = Prone.ExitTransitionTime > 0.f ? Prone.ExitTransitionTime : Stand.TransitionTime;
		ALurePlayerCharacter* Character = World.Spawn(FVector::ZeroVector, Table);
		if (!TestNotNull(TEXT("character spawns"), Character))
		{
			return false;
		}
		World.Tick(10);
		Character->RequestStance(ELureStance::Prone);
		World.Tick(60);

		// Going down still takes the Prone row's TransitionTime.
		TestNearlyEqual(FString::Printf(TEXT("%s: prone eye"), Case.Label), Character->GetCurrentEyeHeight(), Prone.EyeHeight, 0.5f);

		Character->RequestStance(ELureStance::Stand);
		TArray<float> Eye;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			World.Tick(1);
			Eye.Add(Character->GetCurrentEyeHeight());
		}
		const int32 Done = FMath::CeilToInt(Duration / Dt) + 2;
		const int32 Early = FMath::FloorToInt(0.6f * Duration / Dt);
		AddInfo(FString::Printf(TEXT("%s: getting up takes %.2f s"), Case.Label, Duration));
		TestTrue(FString::Printf(TEXT("%s: still rising at %.2f s (%.1f cm)"), Case.Label, Early * Dt, Eye[Early]), Eye[Early] < Stand.EyeHeight - 5.f);
		TestNearlyEqual(FString::Printf(TEXT("%s: at the stand eye height after %.2f s"), Case.Label, Done * Dt), Eye[Done], Stand.EyeHeight, 0.5f);
		float LargestStep = 0.f;
		for (int32 Index = 1; Index < Eye.Num(); ++Index)
		{
			LargestStep = FMath::Max(LargestStep, Eye[Index] - Eye[Index - 1]);
		}
		TestTrue(FString::Printf(TEXT("%s: no pop (largest frame step %.1f cm)"), Case.Label, LargestStep), LargestStep <= 2.f * (Stand.EyeHeight - Prone.EyeHeight) * Dt / Duration * 1.6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureProneLedgeFallTest, "Project.Movement.Camera.CrawlingOffALedgeKeepsTheProneEyeHeight", LurePlaytestFixTest::Flags)

bool FLureProneLedgeFallTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FWorld World;
	if (!Table || !World.Create(*this, /*bFloor*/ false))
	{
		return false;
	}
	// A 180 cm ledge (the playtest's case): upper floor for x < 200, lower floor 180 cm down beyond it.
	const float EdgeX = 200.f;
	const float Drop = 180.f;
	World.AddBox(FVector(-1400.f, 0.f, -50.f), FVector(1600.f, 3000.f, 50.f));
	World.AddBox(FVector(EdgeX + 1500.f, 0.f, -Drop - 50.f), FVector(1500.f, 3000.f, 50.f));
	const FLureMovementRow Prone = RowOf(Table, ELureMovementState::Prone);

	ALurePlayerCharacter* Character = World.Spawn(FVector::ZeroVector, Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	World.Tick(10);
	Character->RequestStance(ELureStance::Prone);
	World.Tick(40);
	TestEqual(TEXT("prone"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Prone));

	float PreviousCamera = CameraZ(Character);
	float LargestRise = 0.f;
	float WorstEyeOffset = 0.f;
	int32 FallingFrames = 0;
	bool bLanded = false;
	for (int32 Frame = 0; Frame < 600 && !bLanded; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
		const float Camera = CameraZ(Character);
		if (Movement->IsFalling())
		{
			++FallingFrames;
			LargestRise = FMath::Max(LargestRise, Camera - PreviousCamera);
			WorstEyeOffset = FMath::Max(WorstEyeOffset, FMath::Abs((Camera - FeetZ(Character)) - Prone.EyeHeight));
		}
		bLanded = FallingFrames > 0 && Movement->IsMovingOnGround() && FeetZ(Character) < -Drop + 5.f;
		PreviousCamera = Camera;
	}
	TestTrue(TEXT("crawled off and landed below"), bLanded);
	TestTrue(TEXT("fell for a while"), FallingFrames > 5);
	TestTrue(FString::Printf(TEXT("the camera never rises during the fall (largest rise %.2f cm)"), LargestRise), LargestRise <= 0.5f);
	TestTrue(FString::Printf(TEXT("the camera stays at the prone eye height above the feet (worst %.2f cm off)"), WorstEyeOffset), WorstEyeOffset <= 1.f);
	World.Tick(40);
	TestEqual(TEXT("prone again after landing"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Prone));
	TestNearlyEqual(TEXT("prone eye after landing"), CameraZ(Character) - FeetZ(Character), Prone.EyeHeight, 0.5f);
	TestFalse(TEXT("no penetration"), IsPenetrating(Character));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

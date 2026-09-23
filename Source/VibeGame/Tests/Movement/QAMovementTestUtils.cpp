// Lure T-004 QA (qa-engineer): shared helpers for Project.Movement.QA.* tests.

#include "Tests/Movement/QAMovementTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace QAMovement
{
	TArray<FRowSpec> FixtureARows()
	{
		TArray<FRowSpec> Rows;
		Rows.Add({ TEXT("Stand"), 311.f, 2048.f, 87.f, 33.f, 158.f, 0.20f, 1.0f, 420.f, true });
		Rows.Add({ TEXT("Sprint"), 577.f, 2048.f, 87.f, 33.f, 158.f, 0.20f, 1.7f, 440.f, true });
		Rows.Add({ TEXT("Crouch"), 173.f, 2048.f, 53.f, 33.f, 88.f, 0.25f, 0.5f, 380.f, false });
		Rows.Add({ TEXT("Prone"), 89.f, 2048.f, 27.f, 25.f, 33.f, 0.40f, 0.2f, 0.f, false });
		// T-026 swim rows (Stand capsule; the optional Water columns stay 0 in QA fixtures).
		Rows.Add({ TEXT("Swim"), 151.f, 800.f, 87.f, 33.f, 112.f, 0.30f, 1.4f, 0.f, false });
		Rows.Add({ TEXT("SwimSprint"), 263.f, 900.f, 87.f, 33.f, 112.f, 0.30f, 2.6f, 0.f, false });
		return Rows;
	}

	TArray<FRowSpec> FixtureBRows()
	{
		TArray<FRowSpec> Rows;
		Rows.Add({ TEXT("Stand"), 402.f, 2048.f, 92.f, 35.f, 170.f, 0.10f, 1.0f, 420.f, true });
		Rows.Add({ TEXT("Sprint"), 650.f, 2048.f, 92.f, 35.f, 170.f, 0.10f, 2.0f, 440.f, true });
		Rows.Add({ TEXT("Crouch"), 150.f, 2048.f, 48.f, 35.f, 80.f, 0.30f, 0.4f, 380.f, true });
		Rows.Add({ TEXT("Prone"), 70.f, 2048.f, 28.f, 24.f, 30.f, 0.0f, 0.1f, 0.f, false });
		Rows.Add({ TEXT("Swim"), 140.f, 700.f, 92.f, 35.f, 120.f, 0.20f, 1.3f, 0.f, false });
		Rows.Add({ TEXT("SwimSprint"), 250.f, 700.f, 92.f, 35.f, 120.f, 0.20f, 2.2f, 0.f, false });
		return Rows;
	}

	FRowSpec* FindSpec(TArray<FRowSpec>& Rows, const TCHAR* Name)
	{
		return Rows.FindByPredicate([Name](const FRowSpec& Spec) { return Spec.Name == Name; });
	}

	FString MakeCsv(const TArray<FRowSpec>& Rows)
	{
		FString Csv = TEXT("Name,MaxSpeed,MaxAcceleration,CapsuleHalfHeight,CapsuleRadius,EyeHeight,TransitionTime,NoiseMultiplier,JumpZVelocity,CanJump,BobStepRate,BobVertical,BobLateral,BobRoll,BobPitch,BobYaw,BobForward,StanceDipPlayRate\n");
		for (const FRowSpec& Row : Rows)
		{
			Csv += FString::Printf(TEXT("%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%s,0,0.8,0.6,0.6,0.4,0,0,1.0\n"),
				*Row.Name, Row.MaxSpeed, Row.MaxAcceleration, Row.HalfHeight, Row.Radius, Row.EyeHeight, Row.TransitionTime, Row.Noise, Row.JumpZ,
				Row.bCanJump ? TEXT("True") : TEXT("False"));
		}
		return Csv;
	}

	FString ShippedCsvPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv"));
	}

	bool LoadShippedCsv(FString& OutCsv)
	{
		return FFileHelper::LoadFileToString(OutCsv, *ShippedCsvPath());
	}

	UDataTable* MakeTable(const FString& Csv, TArray<FString>* OutProblems)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureMovementRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		if (OutProblems)
		{
			*OutProblems = Problems;
		}
		return Table;
	}

	UDataTable* MakeTable(FAutomationTestBase& Test, const TArray<FRowSpec>& Rows)
	{
		TArray<FString> Problems;
		UDataTable* Table = MakeTable(MakeCsv(Rows), &Problems);
		if (Problems.Num() > 0)
		{
			Test.AddError(FString::Printf(TEXT("QA fixture CSV import problems: %s"), *FString::Join(Problems, TEXT(" | "))));
		}
		return Table;
	}

	UDataTable* ShippedTable(FAutomationTestBase& Test)
	{
		FString Csv;
		if (!LoadShippedCsv(Csv))
		{
			Test.AddError(FString::Printf(TEXT("Cannot read %s"), *ShippedCsvPath()));
			return nullptr;
		}
		TArray<FString> Problems;
		UDataTable* Table = MakeTable(Csv, &Problems);
		if (Problems.Num() > 0)
		{
			Test.AddError(FString::Printf(TEXT("DT_Movement.csv import problems: %s"), *FString::Join(Problems, TEXT(" | "))));
		}
		return Table;
	}

	UDataTable* FixtureA(FAutomationTestBase& Test)
	{
		return MakeTable(Test, FixtureARows());
	}

	UDataTable* FixtureB(FAutomationTestBase& Test)
	{
		return MakeTable(Test, FixtureBRows());
	}

	TArray<FLureMovementRow> Resolve(const UDataTable* Table, uint8* OutFallbackMask, TArray<FString>* OutProblems)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		const uint8 Mask = FLureMovementData::ResolveRows(Table, Rows, Problems);
		if (OutFallbackMask)
		{
			*OutFallbackMask = Mask;
		}
		if (OutProblems)
		{
			*OutProblems = Problems;
		}
		return Rows;
	}

	const FLureMovementRow& RowOf(const TArray<FLureMovementRow>& Rows, ELureMovementState State)
	{
		return Rows[static_cast<int32>(State)];
	}

	ELureMovementState StateOf(ELureStance Stance)
	{
		switch (Stance)
		{
		case ELureStance::Crouch: return ELureMovementState::Crouch;
		case ELureStance::Prone: return ELureMovementState::Prone;
		default: return ELureMovementState::Stand;
		}
	}

	FString StanceName(ELureStance Stance)
	{
		switch (Stance)
		{
		case ELureStance::Crouch: return TEXT("Crouch");
		case ELureStance::Prone: return TEXT("Prone");
		default: return TEXT("Stand");
		}
	}

	FString StateName(ELureMovementState State)
	{
		switch (State)
		{
		case ELureMovementState::Sprint: return TEXT("Sprint");
		case ELureMovementState::Crouch: return TEXT("Crouch");
		case ELureMovementState::Prone: return TEXT("Prone");
		case ELureMovementState::Swim: return TEXT("Swim");
		case ELureMovementState::SwimSprint: return TEXT("SwimSprint");
		default: return TEXT("Stand");
		}
	}

	bool RowsEqual(const FLureMovementRow& A, const FLureMovementRow& B)
	{
		return FLureMovementRow::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
	}

	TArray<FString> CheckRule(ERule Rule, const TArray<FLureMovementRow>& Rows)
	{
		TArray<FString> Broken;
		if (Rows.Num() != FLureMovementData::NumStates)
		{
			Broken.Add(FString::Printf(TEXT("expected %d rows, got %d"), FLureMovementData::NumStates, Rows.Num()));
			return Broken;
		}
		const FLureMovementRow& Stand = RowOf(Rows, ELureMovementState::Stand);
		const FLureMovementRow& Sprint = RowOf(Rows, ELureMovementState::Sprint);
		const FLureMovementRow& Crouch = RowOf(Rows, ELureMovementState::Crouch);
		const FLureMovementRow& Prone = RowOf(Rows, ELureMovementState::Prone);
		auto Each = [&Rows, &Broken](TFunctionRef<bool(const FLureMovementRow&)> Ok, const TCHAR* What)
		{
			for (int32 Index = 0; Index < Rows.Num(); ++Index)
			{
				if (!Ok(Rows[Index]))
				{
					Broken.Add(FString::Printf(TEXT("%s: %s"), *StateName(static_cast<ELureMovementState>(Index)), What));
				}
			}
		};
		auto Check = [&Broken](bool bOk, const FString& What)
		{
			if (!bOk)
			{
				Broken.Add(What);
			}
		};

		switch (Rule)
		{
		case ERule::SpeedsPositiveAndSane:
			Each([](const FLureMovementRow& R) { return FMath::IsFinite(R.MaxSpeed) && R.MaxSpeed >= 50.f && R.MaxSpeed <= 2000.f; }, TEXT("MaxSpeed must be finite and in [50, 2000] cm/s"));
			break;
		case ERule::SpeedOrdering:
			Check(Sprint.MaxSpeed > Stand.MaxSpeed && Stand.MaxSpeed > Crouch.MaxSpeed && Crouch.MaxSpeed > Prone.MaxSpeed,
				FString::Printf(TEXT("speeds must be Sprint > Stand > Crouch > Prone (got %.1f, %.1f, %.1f, %.1f)"), Sprint.MaxSpeed, Stand.MaxSpeed, Crouch.MaxSpeed, Prone.MaxSpeed));
			break;
		case ERule::CapsuleHeightOrdering:
			Check(Stand.CapsuleHalfHeight > Crouch.CapsuleHalfHeight && Crouch.CapsuleHalfHeight > Prone.CapsuleHalfHeight,
				FString::Printf(TEXT("half heights must be Stand > Crouch > Prone (got %.1f, %.1f, %.1f)"), Stand.CapsuleHalfHeight, Crouch.CapsuleHalfHeight, Prone.CapsuleHalfHeight));
			Check(FMath::IsNearlyEqual(Sprint.CapsuleHalfHeight, Stand.CapsuleHalfHeight) && FMath::IsNearlyEqual(Sprint.CapsuleRadius, Stand.CapsuleRadius),
				TEXT("Sprint capsule must equal the Stand capsule"));
			break;
		case ERule::EyeHeightOrdering:
			Check(Stand.EyeHeight > Crouch.EyeHeight && Crouch.EyeHeight > Prone.EyeHeight,
				FString::Printf(TEXT("eye heights must be Stand > Crouch > Prone (got %.1f, %.1f, %.1f)"), Stand.EyeHeight, Crouch.EyeHeight, Prone.EyeHeight));
			Check(FMath::IsNearlyEqual(Sprint.EyeHeight, Stand.EyeHeight), TEXT("Sprint EyeHeight must equal Stand EyeHeight"));
			break;
		case ERule::RadiusValid:
			Each([](const FLureMovementRow& R) { return R.CapsuleRadius > 0.f && R.CapsuleRadius <= R.CapsuleHalfHeight; }, TEXT("0 < CapsuleRadius <= CapsuleHalfHeight (the engine raises the half height to the radius)"));
			Each([](const FLureMovementRow& R) { return R.CapsuleRadius >= 15.f && R.CapsuleRadius <= 60.f; }, TEXT("CapsuleRadius in [15, 60] cm"));
			break;
		case ERule::EyeInsideCapsule:
			Each([](const FLureMovementRow& R) { return R.EyeHeight > 0.f && R.EyeHeight <= 2.f * R.CapsuleHalfHeight - NearClip; }, TEXT("0 < EyeHeight <= 2 x CapsuleHalfHeight - 10 (near clip)"));
			break;
		case ERule::ProneFits60cmGap:
			Check(2.f * Prone.CapsuleHalfHeight + MaxFloorDist <= CrawlGap,
				FString::Printf(TEXT("prone needs %.1f cm (2 x %.1f + %.1f floor gap) but the crawl gap is %.0f cm"), 2.f * Prone.CapsuleHalfHeight + MaxFloorDist, Prone.CapsuleHalfHeight, MaxFloorDist, CrawlGap));
			break;
		case ERule::CrouchDoesNotFit60cmGap:
			Check(2.f * Crouch.CapsuleHalfHeight > CrawlGap, FString::Printf(TEXT("crouch (2 x %.1f cm) must NOT fit the %.0f cm crawl gap"), Crouch.CapsuleHalfHeight, CrawlGap));
			break;
		case ERule::StandHeightHumanScale:
			Check(2.f * Stand.CapsuleHalfHeight >= 150.f && 2.f * Stand.CapsuleHalfHeight <= 200.f, FString::Printf(TEXT("standing height %.1f cm must be in [150, 200]"), 2.f * Stand.CapsuleHalfHeight));
			break;
		case ERule::TransitionTimeRange:
			Each([](const FLureMovementRow& R) { return FMath::IsFinite(R.TransitionTime) && R.TransitionTime >= 0.f && R.TransitionTime <= 1.f; }, TEXT("TransitionTime must be finite and in [0, 1] s"));
			break;
		case ERule::NoiseMultiplierOrdering:
			Each([](const FLureMovementRow& R) { return FMath::IsFinite(R.NoiseMultiplier) && R.NoiseMultiplier >= 0.f; }, TEXT("NoiseMultiplier must be finite and >= 0"));
			Check(Sprint.NoiseMultiplier > Stand.NoiseMultiplier && Stand.NoiseMultiplier > Crouch.NoiseMultiplier && Crouch.NoiseMultiplier > Prone.NoiseMultiplier,
				FString::Printf(TEXT("noise must be Sprint > Stand > Crouch > Prone (got %.2f, %.2f, %.2f, %.2f)"), Sprint.NoiseMultiplier, Stand.NoiseMultiplier, Crouch.NoiseMultiplier, Prone.NoiseMultiplier));
			Check(FMath::IsNearlyEqual(Stand.NoiseMultiplier, 1.f), TEXT("Stand NoiseMultiplier must be the 1.0 baseline"));
			break;
		case ERule::JumpFlags:
			Check(!Prone.CanJump, TEXT("Prone.CanJump must be false (hard rule)"));
			Check(Stand.CanJump && Sprint.CanJump, TEXT("Stand and Sprint must be able to jump"));
			Check(Crouch.CanJump, TEXT("Crouch.CanJump must be true (lead decision A14: crouch jump allowed)"));
			break;
		default:
			break;
		}
		return Broken;
	}

	TArray<FString> CheckAllRules(const TArray<FLureMovementRow>& Rows)
	{
		TArray<FString> Broken;
		for (uint8 Rule = 0; Rule < static_cast<uint8>(ERule::Count); ++Rule)
		{
			Broken.Append(CheckRule(static_cast<ERule>(Rule), Rows));
		}
		return Broken;
	}

	// ---- World ----

	bool FWorld::Create(FAutomationTestBase& Test, bool bFloor)
	{
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(&Test);
			Test.AddError(TEXT("QA: the test world could not be created"));
			return false;
		}
		World = Wrapper.GetTestWorld();
		if (!World)
		{
			Test.AddError(TEXT("QA: no test world"));
			return false;
		}
		if (bFloor)
		{
			AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
		}
		return true;
	}

	AActor* FWorld::AddBox(const FVector& Center, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
		Box->SetMobility(EComponentMobility::Static); // static geometry, like a level (moves combine only on static bases)
		Box->SetBoxExtent(Extent, false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetRelativeLocation_Direct(Center);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		return Actor;
	}

	AActor* FWorld::AddSlab(float GapHeight, float MinX, float MaxX)
	{
		return AddBox(FVector(0.5f * (MinX + MaxX), 0.f, GapHeight + 50.f), FVector(0.5f * (MaxX - MinX), 500.f, 50.f));
	}

	AActor* FWorld::AddWallFacingMinusX(float FaceX)
	{
		return AddBox(FVector(FaceX + 50.f, 0.f, 300.f), FVector(50.f, 500.f, 300.f));
	}

	AActor* FWorld::AddWallFacingMinusY(float FaceY)
	{
		return AddBox(FVector(0.f, FaceY + 50.f, 300.f), FVector(500.f, 50.f, 300.f));
	}

	void FWorld::AddLedgeFloors(float EdgeX, float Drop)
	{
		const float MinX = -3000.f;
		const float MaxX = 3000.f;
		AddBox(FVector(0.5f * (MinX + EdgeX), 0.f, -50.f), FVector(0.5f * (EdgeX - MinX), 3000.f, 50.f));
		AddBox(FVector(0.5f * (EdgeX + MaxX), 0.f, -Drop - 50.f), FVector(0.5f * (MaxX - EdgeX), 3000.f, 50.f));
	}

	ALurePlayerCharacter* FWorld::Spawn(FAutomationTestBase& Test, const FVector& Feet, const UDataTable* Table, bool bApplyTable, TFunction<void(ALurePlayerCharacter&)> PreFinish)
	{
		const float StandHalfHeight = RowOf(Resolve(bApplyTable ? Table : nullptr), ELureMovementState::Stand).CapsuleHalfHeight;
		const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, StandHalfHeight + 2.15f));
		ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Character || !Character->GetLureMovement())
		{
			Test.AddError(TEXT("QA: ALurePlayerCharacter did not spawn with a ULureCharacterMovementComponent"));
			return nullptr;
		}
		if (bApplyTable)
		{
			Character->GetLureMovement()->ApplyMovementTable(Table);
		}
		Character->GetLureMovement()->bRunPhysicsWithNoController = true;
		if (PreFinish)
		{
			PreFinish(*Character);
		}
		Character->FinishSpawning(Transform);
		return Character;
	}

	void FWorld::Tick(int32 Frames, float DeltaTime)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Wrapper.TickTestWorld(DeltaTime);
		}
	}

	void FWorld::TickMoving(ALurePlayerCharacter* Character, int32 Frames, const FVector& Direction, float DeltaTime)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			if (Character)
			{
				Character->AddMovementInput(Direction, 1.f, /*bForce*/ true);
			}
			Wrapper.TickTestWorld(DeltaTime);
		}
	}

	bool FWorld::TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, ALurePlayerCharacter* MoveCharacter, float DeltaTime)
	{
		for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
		{
			if (Predicate())
			{
				return true;
			}
			TickMoving(MoveCharacter, 1, FVector::ForwardVector, DeltaTime);
		}
		return Predicate();
	}

	// ---- Measurements ----

	float FeetZ(const ALurePlayerCharacter* Character)
	{
		return Character->GetCapsuleComponent()->GetComponentLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	float CameraZ(const ALurePlayerCharacter* Character)
	{
		return Character->GetFirstPersonCamera()->GetComponentLocation().Z;
	}

	float EyeAboveFeet(const ALurePlayerCharacter* Character)
	{
		return CameraZ(Character) - FeetZ(Character);
	}

	float HorizontalSpeed(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetVelocity().Size2D());
	}

	float MaxSpeedNow(const ALurePlayerCharacter* Character)
	{
		return Character->GetLureMovement()->GetMaxSpeed();
	}

	bool IsFalling(const ALurePlayerCharacter* Character)
	{
		return Character->GetLureMovement()->IsFalling();
	}

	bool IsOnGround(const ALurePlayerCharacter* Character)
	{
		return Character->GetLureMovement()->IsMovingOnGround();
	}

	bool IsPenetrating(const ALurePlayerCharacter* Character)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QAMovementOverlap), false, Character);
		return Character->GetWorld()->OverlapBlockingTestByChannel(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(), ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
	}

	void GetCapsule(const ALurePlayerCharacter* Character, float& OutRadius, float& OutHalfHeight)
	{
		Character->GetCapsuleComponent()->GetUnscaledCapsuleSize(OutRadius, OutHalfHeight);
	}

	bool CapsuleMatches(const ALurePlayerCharacter* Character, const FLureMovementRow& Row, float Tolerance)
	{
		float Radius = 0.f;
		float HalfHeight = 0.f;
		GetCapsule(Character, Radius, HalfHeight);
		return FMath::IsNearlyEqual(Radius, Row.CapsuleRadius, Tolerance) && FMath::IsNearlyEqual(HalfHeight, Row.CapsuleHalfHeight, Tolerance);
	}

	void TestCapsule(FAutomationTestBase& Test, const ALurePlayerCharacter* Character, const FLureMovementRow& Row, const FString& Label)
	{
		float Radius = 0.f;
		float HalfHeight = 0.f;
		GetCapsule(Character, Radius, HalfHeight);
		Test.TestNearlyEqual(Label + TEXT(": capsule half height"), HalfHeight, Row.CapsuleHalfHeight, 0.05f);
		Test.TestNearlyEqual(Label + TEXT(": capsule radius"), Radius, Row.CapsuleRadius, 0.05f);
	}

	bool EnterStance(FWorld& World, ALurePlayerCharacter* Character, ELureStance Stance, int32 Frames)
	{
		Character->RequestStance(Stance);
		World.Tick(Frames);
		return Character->GetStance() == Stance;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

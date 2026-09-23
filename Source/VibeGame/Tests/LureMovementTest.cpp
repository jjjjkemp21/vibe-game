// Lure T-004: first-person movement tests (implementer's tests; QA adds Project.Movement.QA.*).
// Tables are built from data/tables/DT_Movement.csv (never the binary asset). Worlds are transient FTestWorldWrapper game worlds.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Game/LureGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Playtest/PlaytestFeedbackSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

namespace LureMovementTest
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FString CsvPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv"));
	}

	/** Header of a full DT_Movement CSV (every FLureMovementRow column). */
	const TCHAR* CsvHeader = TEXT("Name,MaxSpeed,MaxAcceleration,CapsuleHalfHeight,CapsuleRadius,EyeHeight,TransitionTime,NoiseMultiplier,JumpZVelocity,CanJump,BobStepRate,BobVertical,BobLateral,BobRoll,BobPitch,BobYaw,BobForward,StanceDipPlayRate\n");

	/** Arms-bob tail for fixture rows (spec defaults for a walk). */
	const TCHAR* BobTail = TEXT(",0,0.8,0.6,0.6,0.4,0,0,1.0\n");

	UDataTable* MakeTable(const FString& Csv, TArray<FString>& OutProblems)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureMovementRow::StaticStruct();
		OutProblems = Table->CreateTableFromCSVString(Csv);
		return Table;
	}

	/** The shipped DT_Movement.csv as a transient table (fails the test if it doesn't load cleanly). */
	UDataTable* LoadShippedTable(FAutomationTestBase& Test)
	{
		FString Csv;
		if (!Test.TestTrue(TEXT("data/tables/DT_Movement.csv loads"), FFileHelper::LoadFileToString(Csv, *CsvPath())))
		{
			return nullptr;
		}
		TArray<FString> Problems;
		UDataTable* Table = MakeTable(Csv, Problems);
		Test.TestEqual(FString::Printf(TEXT("CSV import problems (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		return Table;
	}

	TArray<FLureMovementRow> ResolvedRows(const UDataTable* Table)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Table, Rows, Problems);
		return Rows;
	}

	const FLureMovementRow& Row(const TArray<FLureMovementRow>& Rows, ELureMovementState State)
	{
		return Rows[static_cast<int32>(State)];
	}

	/** Design rules every movement table (and the fallback rows) must follow. Returns the broken rules. */
	TArray<FString> ValidateDesignRules(const TArray<FLureMovementRow>& Rows)
	{
		TArray<FString> Errors;
		auto Check = [&Errors](bool bOk, const FString& What) { if (!bOk) { Errors.Add(What); } };

		for (ELureMovementState State : TEnumRange<ELureMovementState>())
		{
			const FLureMovementRow& R = Row(Rows, State);
			const FString Name = FLureMovementData::GetRowName(State).ToString();
			FString Problem;
			Check(R.Validate(Problem), Name + TEXT(": ") + Problem);
			Check(R.MaxSpeed >= 50.f && R.MaxSpeed <= 2000.f, Name + TEXT(": MaxSpeed in [50, 2000] cm/s"));
			Check(R.CapsuleRadius >= 15.f && R.CapsuleRadius <= 60.f, Name + TEXT(": CapsuleRadius in [15, 60] cm"));
			Check(R.EyeHeight <= 2.f * R.CapsuleHalfHeight - 10.f, Name + TEXT(": EyeHeight at least 10 cm (near clip) below the capsule top"));
			Check(R.TransitionTime >= 0.f && R.TransitionTime <= 1.f, Name + TEXT(": TransitionTime in [0, 1] s"));
		}

		const FLureMovementRow& Stand = Row(Rows, ELureMovementState::Stand);
		const FLureMovementRow& Sprint = Row(Rows, ELureMovementState::Sprint);
		const FLureMovementRow& Crouch = Row(Rows, ELureMovementState::Crouch);
		const FLureMovementRow& Prone = Row(Rows, ELureMovementState::Prone);
		const float MaxFloorDist = 2.4f; // UCharacterMovementComponent::MAX_FLOOR_DIST: the capsule hovers up to this far above the floor

		Check(Sprint.MaxSpeed > Stand.MaxSpeed && Stand.MaxSpeed > Crouch.MaxSpeed && Crouch.MaxSpeed > Prone.MaxSpeed, TEXT("speeds: Sprint > Stand > Crouch > Prone"));
		Check(Stand.CapsuleHalfHeight > Crouch.CapsuleHalfHeight && Crouch.CapsuleHalfHeight > Prone.CapsuleHalfHeight, TEXT("heights: Stand > Crouch > Prone"));
		Check(Stand.EyeHeight > Crouch.EyeHeight && Crouch.EyeHeight > Prone.EyeHeight, TEXT("eye heights: Stand > Crouch > Prone"));
		Check(FMath::IsNearlyEqual(Sprint.CapsuleHalfHeight, Stand.CapsuleHalfHeight) && FMath::IsNearlyEqual(Sprint.CapsuleRadius, Stand.CapsuleRadius), TEXT("Sprint capsule == Stand capsule"));
		Check(FMath::IsNearlyEqual(Sprint.EyeHeight, Stand.EyeHeight), TEXT("Sprint EyeHeight == Stand EyeHeight"));
		Check(2.f * Stand.CapsuleHalfHeight >= 150.f && 2.f * Stand.CapsuleHalfHeight <= 200.f, TEXT("standing height in [150, 200] cm"));
		Check(2.f * Prone.CapsuleHalfHeight <= 55.f, TEXT("prone total height <= 55 cm"));
		Check(2.f * Prone.CapsuleHalfHeight + MaxFloorDist <= 60.f, TEXT("prone fits the 60 cm crawl gap"));
		Check(2.f * Crouch.CapsuleHalfHeight > 60.f, TEXT("crouch does NOT fit the 60 cm crawl gap"));
		Check(!Prone.CanJump, TEXT("Prone.CanJump is false"));
		Check(Stand.CanJump && Sprint.CanJump, TEXT("Stand and Sprint can jump"));
		Check(FMath::IsNearlyEqual(Stand.NoiseMultiplier, 1.f), TEXT("Stand noise multiplier is the 1.0 baseline"));
		Check(Sprint.NoiseMultiplier > Stand.NoiseMultiplier && Stand.NoiseMultiplier > Crouch.NoiseMultiplier && Crouch.NoiseMultiplier > Prone.NoiseMultiplier, TEXT("noise: Sprint > Stand > Crouch > Prone"));
		return Errors;
	}

	/** Transient game world with a floor (top at z = 0) and helpers. */
	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
			return World != nullptr;
		}

		AActor* AddBox(const FVector& Center, const FVector& Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
			Box->SetBoxExtent(Extent, false);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(Center);
			return Actor;
		}

		/** Ceiling slab whose underside is GapHeight above the floor, spanning X in [MinX, MaxX]. */
		AActor* AddSlab(float GapHeight, float MinX, float MaxX)
		{
			return AddBox(FVector(0.5f * (MinX + MaxX), 0.f, GapHeight + 50.f), FVector(0.5f * (MaxX - MinX), 500.f, 50.f));
		}

		/** Spawns the character standing with its feet at FeetLocation; Table is applied before BeginPlay (null = fallback rows). */
		ALurePlayerCharacter* SpawnCharacter(const FVector& FeetLocation, const UDataTable* Table)
		{
			const float HalfHeight = Row(ResolvedRows(Table), ELureMovementState::Stand).CapsuleHalfHeight;
			const FTransform Transform(FRotator::ZeroRotator, FeetLocation + FVector(0.f, 0.f, HalfHeight + 2.15f));
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character)
			{
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Table);
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			Character->FinishSpawning(Transform);
			return Character;
		}

		void Tick(int32 Frames, float DeltaTime = Dt)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(DeltaTime);
			}
		}
	};

	float FeetZ(const ALurePlayerCharacter* Character)
	{
		return Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	float CameraZ(const ALurePlayerCharacter* Character)
	{
		return Character->GetFirstPersonCamera()->GetComponentLocation().Z;
	}

	bool IsPenetrating(const ALurePlayerCharacter* Character)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LureMovementTestOverlap), false, Character);
		return Character->GetWorld()->OverlapBlockingTestByChannel(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(), ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
	}

	void CheckCapsule(FAutomationTestBase& Test, const ALurePlayerCharacter* Character, const FLureMovementRow& Expected, const FString& Label)
	{
		float Radius = 0.f;
		float HalfHeight = 0.f;
		Character->GetCapsuleComponent()->GetUnscaledCapsuleSize(Radius, HalfHeight);
		Test.TestNearlyEqual(Label + TEXT(": capsule half height"), HalfHeight, Expected.CapsuleHalfHeight, 0.05f);
		Test.TestNearlyEqual(Label + TEXT(": capsule radius"), Radius, Expected.CapsuleRadius, 0.05f);
	}
}

using namespace LureMovementTest;

// ---------------------------------------------------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementCsvParsesTest, "Project.Movement.Data.CsvParsesIntoRows", LureMovementTest::TestFlags)

bool FLureMovementCsvParsesTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadShippedTable(*this);
	if (!Table)
	{
		return false;
	}

	TArray<FName> Names = Table->GetRowNames();
	TestEqual(TEXT("exactly 4 rows"), Names.Num(), FLureMovementData::NumStates);
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		const FName Name = FLureMovementData::GetRowName(State);
		TestNotNull(FString::Printf(TEXT("row %s exists"), *Name.ToString()), Table->FindRow<FLureMovementRow>(Name, TEXT("test"), false));
	}

	TArray<FLureMovementRow> Rows;
	TArray<FString> Problems;
	const uint8 FallbackMask = FLureMovementData::ResolveRows(Table, Rows, Problems);
	TestEqual(TEXT("no row falls back"), static_cast<int32>(FallbackMask), 0);
	TestEqual(FString::Printf(TEXT("no resolve problems (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);

	// A few literal values, so a column that silently fails to import is caught.
	TestNearlyEqual(TEXT("Stand.MaxSpeed"), Row(Rows, ELureMovementState::Stand).MaxSpeed, 350.f);
	TestNearlyEqual(TEXT("Sprint.MaxSpeed"), Row(Rows, ELureMovementState::Sprint).MaxSpeed, 600.f);
	TestNearlyEqual(TEXT("Crouch.CapsuleHalfHeight"), Row(Rows, ELureMovementState::Crouch).CapsuleHalfHeight, 55.f);
	TestNearlyEqual(TEXT("Prone.CapsuleRadius"), Row(Rows, ELureMovementState::Prone).CapsuleRadius, 25.f);
	TestNearlyEqual(TEXT("Prone.EyeHeight"), Row(Rows, ELureMovementState::Prone).EyeHeight, 35.f);
	TestNearlyEqual(TEXT("Prone.TransitionTime"), Row(Rows, ELureMovementState::Prone).TransitionTime, 0.45f);
	TestNearlyEqual(TEXT("Sprint.NoiseMultiplier"), Row(Rows, ELureMovementState::Sprint).NoiseMultiplier, 2.5f);
	TestNearlyEqual(TEXT("Crouch.JumpZVelocity"), Row(Rows, ELureMovementState::Crouch).JumpZVelocity, 380.f);
	TestTrue(TEXT("Crouch.CanJump"), Row(Rows, ELureMovementState::Crouch).CanJump);
	TestFalse(TEXT("Prone.CanJump"), Row(Rows, ELureMovementState::Prone).CanJump);

	// Arms bob columns (SK_FPArms.anim.md defaults; step rate derived from MaxSpeed).
	TestNearlyEqual(TEXT("Stand.BobStepRate (0 = derive)"), Row(Rows, ELureMovementState::Stand).BobStepRate, 0.f);
	TestNearlyEqual(TEXT("Stand.BobVertical"), Row(Rows, ELureMovementState::Stand).BobVertical, 0.8f);
	TestNearlyEqual(TEXT("Sprint.BobForward"), Row(Rows, ELureMovementState::Sprint).BobForward, 0.3f);
	TestNearlyEqual(TEXT("Crouch.BobLateral"), Row(Rows, ELureMovementState::Crouch).BobLateral, 0.9f);
	TestNearlyEqual(TEXT("Prone.BobRoll"), Row(Rows, ELureMovementState::Prone).BobRoll, 2.0f);
	TestNearlyEqual(TEXT("Prone.BobYaw"), Row(Rows, ELureMovementState::Prone).BobYaw, 1.5f);
	TestNearlyEqual(TEXT("Prone.StanceDipPlayRate"), Row(Rows, ELureMovementState::Prone).StanceDipPlayRate, 0.85f);

	// Informational only (tuning must stay a data edit): note where the CSV has drifted from the built-in fallback rows.
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		const FLureMovementRow& Csv = Row(Rows, State);
		const FLureMovementRow Fallback = FLureMovementData::GetFallbackRow(State);
		const FString Name = FLureMovementData::GetRowName(State).ToString();
		const bool bSame =
			FMath::IsNearlyEqual(Csv.MaxSpeed, Fallback.MaxSpeed) && FMath::IsNearlyEqual(Csv.MaxAcceleration, Fallback.MaxAcceleration)
			&& FMath::IsNearlyEqual(Csv.CapsuleHalfHeight, Fallback.CapsuleHalfHeight) && FMath::IsNearlyEqual(Csv.CapsuleRadius, Fallback.CapsuleRadius)
			&& FMath::IsNearlyEqual(Csv.EyeHeight, Fallback.EyeHeight) && FMath::IsNearlyEqual(Csv.TransitionTime, Fallback.TransitionTime)
			&& FMath::IsNearlyEqual(Csv.NoiseMultiplier, Fallback.NoiseMultiplier) && FMath::IsNearlyEqual(Csv.JumpZVelocity, Fallback.JumpZVelocity)
			&& Csv.CanJump == Fallback.CanJump && FMath::IsNearlyEqual(Csv.BobStepRate, Fallback.BobStepRate)
			&& FMath::IsNearlyEqual(Csv.BobVertical, Fallback.BobVertical) && FMath::IsNearlyEqual(Csv.BobLateral, Fallback.BobLateral)
			&& FMath::IsNearlyEqual(Csv.BobRoll, Fallback.BobRoll) && FMath::IsNearlyEqual(Csv.BobPitch, Fallback.BobPitch)
			&& FMath::IsNearlyEqual(Csv.BobYaw, Fallback.BobYaw) && FMath::IsNearlyEqual(Csv.BobForward, Fallback.BobForward)
			&& FMath::IsNearlyEqual(Csv.StanceDipPlayRate, Fallback.StanceDipPlayRate);
		if (!bSame)
		{
			AddInfo(Name + TEXT(": the CSV differs from the built-in fallback row (update FLureMovementData::GetFallbackRow when convenient)."));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementRowsValidTest, "Project.Movement.Data.EveryRowValid", LureMovementTest::TestFlags)

bool FLureMovementRowsValidTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadShippedTable(*this);
	if (!Table)
	{
		return false;
	}
	for (const FString& Error : ValidateDesignRules(ResolvedRows(Table)))
	{
		AddError(TEXT("DT_Movement.csv: ") + Error);
	}

	// Numbers for level design (T-005): the lowest gap each stance can pass.
	const TArray<FLureMovementRow> Rows = ResolvedRows(Table);
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		AddInfo(FString::Printf(TEXT("%s passes gaps of %.1f cm and more"), *FLureMovementData::GetRowName(State).ToString(), 2.f * Row(Rows, State).CapsuleHalfHeight + 2.4f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementFallbackRowsValidTest, "Project.Movement.Data.FallbackRowsValid", LureMovementTest::TestFlags)

bool FLureMovementFallbackRowsValidTest::RunTest(const FString& Parameters)
{
	for (const FString& Error : ValidateDesignRules(ResolvedRows(nullptr)))
	{
		AddError(TEXT("built-in fallback rows: ") + Error);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementFallbackTest, "Project.Movement.Data.FallbackWhenTableMissing", LureMovementTest::TestFlags)

bool FLureMovementFallbackTest::RunTest(const FString& Parameters)
{
	// 1. No table: every state falls back.
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		const uint8 Mask = FLureMovementData::ResolveRows(nullptr, Rows, Problems);
		TestEqual(TEXT("null table: all rows fall back"), static_cast<int32>(Mask), static_cast<int32>(FLureMovementData::AllStatesMask));
		TestTrue(TEXT("null table: a problem is reported"), Problems.Num() > 0);
		for (ELureMovementState State : TEnumRange<ELureMovementState>())
		{
			TestNearlyEqual(TEXT("null table: fallback MaxSpeed"), Row(Rows, State).MaxSpeed, FLureMovementData::GetFallbackRow(State).MaxSpeed);
			TestNearlyEqual(TEXT("null table: fallback CapsuleHalfHeight"), Row(Rows, State).CapsuleHalfHeight, FLureMovementData::GetFallbackRow(State).CapsuleHalfHeight);
		}
	}

	// 2. Wrong row struct: everything falls back, no crash, no Error from the data table code.
	{
		UDataTable* WrongTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		WrongTable->RowStruct = FTableRowBase::StaticStruct();
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		TestEqual(TEXT("wrong struct: all rows fall back"), static_cast<int32>(FLureMovementData::ResolveRows(WrongTable, Rows, Problems)), static_cast<int32>(FLureMovementData::AllStatesMask));
	}

	// 3. Per-row fallback: Prone missing, Crouch invalid (MaxSpeed 0), Stand and Sprint used as given.
	{
		TArray<FString> ImportProblems;
		UDataTable* Partial = MakeTable(FString(CsvHeader)
			+ TEXT("Stand,311,2000,87,33,158,0.2,1.0,400,True") + BobTail
			+ TEXT("Sprint,577,2000,87,33,158,0.2,1.7,400,True") + BobTail
			+ TEXT("Crouch,0,1500,53,33,88,0.25,0.5,300,True") + BobTail, ImportProblems);
		TestEqual(TEXT("partial fixture imports cleanly"), ImportProblems.Num(), 0);
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		const uint8 Mask = FLureMovementData::ResolveRows(Partial, Rows, Problems);
		const uint8 Expected = (1u << static_cast<int32>(ELureMovementState::Crouch)) | (1u << static_cast<int32>(ELureMovementState::Prone));
		TestEqual(TEXT("partial table: Crouch and Prone fall back"), static_cast<int32>(Mask), static_cast<int32>(Expected));
		TestNearlyEqual(TEXT("partial table: Stand from the table"), Row(Rows, ELureMovementState::Stand).MaxSpeed, 311.f);
		TestNearlyEqual(TEXT("partial table: Sprint from the table"), Row(Rows, ELureMovementState::Sprint).MaxSpeed, 577.f);
		TestNearlyEqual(TEXT("partial table: Crouch from the fallback"), Row(Rows, ELureMovementState::Crouch).MaxSpeed, FLureMovementData::GetFallbackRow(ELureMovementState::Crouch).MaxSpeed);
		TestNearlyEqual(TEXT("partial table: Prone from the fallback"), Row(Rows, ELureMovementState::Prone).MaxSpeed, FLureMovementData::GetFallbackRow(ELureMovementState::Prone).MaxSpeed);
		const FString Warning = FLureMovementData::FormatResolveWarning(TEXT("DT_Test"), Problems, Mask);
		TestTrue(TEXT("warning text has the marker"), Warning.Contains(FLureMovementData::FallbackWarningMarker));
		TestTrue(TEXT("warning names Prone"), Warning.Contains(TEXT("Prone")));
	}

	// 4. A character without a table runs on the fallback rows and logs exactly one warning.
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	AddExpectedMessagePlain(FLureMovementData::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	ALurePlayerCharacter* Character = World.SpawnCharacter(FVector::ZeroVector, nullptr);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	World.Tick(10);
	const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		TestTrue(TEXT("character uses the fallback row"), Movement->IsUsingFallbackRow(State));
	}
	TestNearlyEqual(TEXT("fallback stand speed in use"), Movement->GetMaxSpeed(), FLureMovementData::GetFallbackRow(ELureMovementState::Stand).MaxSpeed);
	CheckCapsule(*this, Character, FLureMovementData::GetFallbackRow(ELureMovementState::Stand), TEXT("fallback stand"));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stances
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementStanceTest, "Project.Movement.Stance.HeightsAndSpeedsPerStance", LureMovementTest::TestFlags)

bool FLureMovementStanceTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadShippedTable(*this);
	FWorld World;
	if (!Table || !World.Create(*this))
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = ResolvedRows(Table);
	ALurePlayerCharacter* Character = World.SpawnCharacter(FVector::ZeroVector, Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	World.Tick(20);
	const float StartFeetZ = FeetZ(Character);

	auto CheckSettled = [&](ELureStance Stance, ELureMovementState State, const TCHAR* Label)
	{
		const FLureMovementRow& Expected = Row(Rows, State);
		TestEqual(FString::Printf(TEXT("%s: stance"), Label), static_cast<int32>(Character->GetStance()), static_cast<int32>(Stance));
		TestEqual(FString::Printf(TEXT("%s: movement state"), Label), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(State));
		CheckCapsule(*this, Character, Expected, Label);
		TestNearlyEqual(FString::Printf(TEXT("%s: max speed"), Label), Movement->GetMaxSpeed(), Expected.MaxSpeed);
		TestNearlyEqual(FString::Printf(TEXT("%s: noise multiplier"), Label), Movement->GetStanceNoiseMultiplier(), Expected.NoiseMultiplier);
		TestNearlyEqual(FString::Printf(TEXT("%s: eye height"), Label), Character->GetCurrentEyeHeight(), Expected.EyeHeight, 0.5f);
		TestNearlyEqual(FString::Printf(TEXT("%s: camera above the feet"), Label), CameraZ(Character) - FeetZ(Character), Expected.EyeHeight, 0.5f);
		TestNearlyEqual(FString::Printf(TEXT("%s: AI eye point = camera"), Label), static_cast<float>(Character->GetPawnViewLocation().Z), CameraZ(Character), 1.f);
		TestNearlyEqual(FString::Printf(TEXT("%s: feet planted"), Label), FeetZ(Character), StartFeetZ, 2.5f);
		TestTrue(FString::Printf(TEXT("%s: on the ground"), Label), Movement->IsMovingOnGround());
		TestFalse(FString::Printf(TEXT("%s: no penetration"), Label), IsPenetrating(Character));
	};

	// Changes stance, then checks every frame that the camera eases (no pop, monotonic) and settles at the new row.
	auto Transition = [&](ELureStance Stance, const TCHAR* Label)
	{
		const float FromCamera = CameraZ(Character) - StartFeetZ;
		const float ToEye = Row(Rows, FLureMovementData::ToMovementState(Stance)).EyeHeight;
		const float EyeDelta = FMath::Abs(ToEye - FromCamera);
		Character->RequestStance(Stance);
		float PreviousCamera = CameraZ(Character);
		float LargestStep = 0.f;
		bool bMonotonic = true;
		for (int32 Frame = 0; Frame < 45; ++Frame)
		{
			World.Tick(1);
			const float Camera = CameraZ(Character);
			const float Step = Camera - PreviousCamera;
			LargestStep = FMath::Max(LargestStep, FMath::Abs(Step));
			bMonotonic &= (ToEye >= FromCamera) ? (Step >= -0.01f) : (Step <= 0.01f);
			PreviousCamera = Camera;
		}
		TestTrue(FString::Printf(TEXT("%s: camera never pops (largest frame step %.2f cm of %.1f)"), Label, LargestStep, EyeDelta), LargestStep <= 0.4f * EyeDelta + 0.01f);
		TestTrue(FString::Printf(TEXT("%s: camera moves one way only"), Label), bMonotonic);
	};

	CheckSettled(ELureStance::Stand, ELureMovementState::Stand, TEXT("Stand"));
	Transition(ELureStance::Crouch, TEXT("Stand->Crouch"));
	CheckSettled(ELureStance::Crouch, ELureMovementState::Crouch, TEXT("Crouch"));
	Transition(ELureStance::Prone, TEXT("Crouch->Prone"));
	CheckSettled(ELureStance::Prone, ELureMovementState::Prone, TEXT("Prone"));
	Transition(ELureStance::Crouch, TEXT("Prone->Crouch"));
	CheckSettled(ELureStance::Crouch, ELureMovementState::Crouch, TEXT("Crouch again"));
	Transition(ELureStance::Stand, TEXT("Crouch->Stand"));
	CheckSettled(ELureStance::Stand, ELureMovementState::Stand, TEXT("Stand (the table's height, not the class default)"));
	Transition(ELureStance::Prone, TEXT("Stand->Prone"));
	CheckSettled(ELureStance::Prone, ELureMovementState::Prone, TEXT("Prone again"));
	Transition(ELureStance::Stand, TEXT("Prone->Stand"));
	CheckSettled(ELureStance::Stand, ELureMovementState::Stand, TEXT("Stand again"));

	// Sprint: only with movement input; reaches the Sprint row's speed; the camera doesn't move.
	Character->SetSprintRequested(true);
	World.Tick(5);
	TestFalse(TEXT("sprint held without moving is not sprinting"), Character->IsSprinting());
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	TestTrue(TEXT("sprinting while moving"), Character->IsSprinting());
	TestNearlyEqual(TEXT("sprint max speed"), Movement->GetMaxSpeed(), Row(Rows, ELureMovementState::Sprint).MaxSpeed);
	TestNearlyEqual(TEXT("sprint speed reached (1%)"), static_cast<float>(Movement->Velocity.Size2D()), Row(Rows, ELureMovementState::Sprint).MaxSpeed, 0.01f * Row(Rows, ELureMovementState::Sprint).MaxSpeed);
	TestNearlyEqual(TEXT("sprint keeps the stand eye height"), Character->GetCurrentEyeHeight(), Row(Rows, ELureMovementState::Stand).EyeHeight, 0.5f);

	// Crouch while sprint is held: crouch wins (no sprinting while crouched).
	Character->RequestStance(ELureStance::Crouch);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	TestEqual(TEXT("crouch while sprint held: crouched"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Crouch));
	TestFalse(TEXT("crouch while sprint held: not sprinting"), Character->IsSprinting());
	TestNearlyEqual(TEXT("crouch while sprint held: crouch speed"), Movement->GetMaxSpeed(), Row(Rows, ELureMovementState::Crouch).MaxSpeed);

	// Pressing sprint while crouched stands up and sprints (lead decision A1).
	Character->SetSprintRequested(false);
	Character->SetSprintRequested(true);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	TestEqual(TEXT("sprint from crouch: standing"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Stand));
	TestTrue(TEXT("sprint from crouch: sprinting"), Character->IsSprinting());

	// Sprint while prone is ignored.
	Character->SetSprintRequested(false);
	Character->RequestStance(ELureStance::Prone);
	World.Tick(10);
	Character->SetSprintRequested(true);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	TestEqual(TEXT("sprint while prone: still prone"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Prone));
	TestNearlyEqual(TEXT("sprint while prone: prone speed"), Movement->GetMaxSpeed(), Row(Rows, ELureMovementState::Prone).MaxSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementProneJumpTest, "Project.Movement.Stance.ProneBlocksJump", LureMovementTest::TestFlags)

bool FLureMovementProneJumpTest::RunTest(const FString& Parameters)
{
	// A table that (wrongly) allows prone jumps: the hard rule must still win.
	TArray<FString> ImportProblems;
	UDataTable* Table = MakeTable(FString(CsvHeader)
		+ TEXT("Stand,350,2048,90,34,165,0.25,1.0,420,True") + BobTail
		+ TEXT("Sprint,600,2048,90,34,165,0.25,2.5,440,True") + BobTail
		+ TEXT("Crouch,180,1600,55,34,95,0.2,0.5,380,True") + BobTail
		+ TEXT("Prone,90,1200,26,25,35,0.45,0.2,300,True") + BobTail, ImportProblems);
	TestEqual(TEXT("fixture imports cleanly"), ImportProblems.Num(), 0);

	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.SpawnCharacter(FVector::ZeroVector, Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	World.Tick(20);

	// Control: standing jumps.
	TestTrue(TEXT("stand can jump"), Character->CanJump());
	const float GroundZ = Character->GetActorLocation().Z;
	Character->Jump();
	World.Tick(10);
	TestTrue(TEXT("stand jump leaves the ground"), Movement->IsFalling() && Character->GetActorLocation().Z > GroundZ + 5.f);
	Character->StopJumping();
	World.Tick(120);
	TestTrue(TEXT("landed again"), Movement->IsMovingOnGround());

	// Crouch jumps are allowed by default (lead decision A14).
	Character->RequestStance(ELureStance::Crouch);
	World.Tick(10);
	TestTrue(TEXT("crouch can jump (CanJump in data)"), Character->CanJump());

	// Prone never jumps.
	Character->RequestStance(ELureStance::Prone);
	World.Tick(30);
	TestTrue(TEXT("prone"), Character->IsProne());
	TestFalse(TEXT("prone: CanJump is false even though the table says True"), Character->CanJump());
	const float ProneZ = Character->GetActorLocation().Z;
	bool bEverFell = false;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Character->Jump();
		World.Tick(1);
		bEverFell |= Movement->IsFalling();
	}
	Character->StopJumping();
	TestFalse(TEXT("prone: never leaves the ground"), bEverFell);
	TestNearlyEqual(TEXT("prone: no hop"), static_cast<float>(Character->GetActorLocation().Z), static_cast<float>(ProneZ), 0.5f);
	TestTrue(TEXT("prone: still prone after pressing jump"), Character->IsProne());

	// Prone can't be entered in the air (refused, not queued).
	Character->RequestStance(ELureStance::Stand);
	World.Tick(40);
	Character->Jump();
	World.Tick(5);
	TestTrue(TEXT("airborne"), Movement->IsFalling());
	Character->RequestStance(ELureStance::Prone);
	World.Tick(3);
	TestFalse(TEXT("no prone in the air"), Character->IsProne());
	Character->StopJumping();
	World.Tick(120);
	TestFalse(TEXT("the in-air prone request was dropped"), Character->IsProne());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementLowCeilingTest, "Project.Movement.Stance.StandBlockedUnderLowCeiling", LureMovementTest::TestFlags)

bool FLureMovementLowCeilingTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadShippedTable(*this);
	FWorld World;
	if (!Table || !World.Create(*this))
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = ResolvedRows(Table);
	ALurePlayerCharacter* Character = World.SpawnCharacter(FVector::ZeroVector, Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	World.Tick(20);

	Character->RequestStance(ELureStance::Prone);
	World.Tick(40);
	TestTrue(TEXT("prone in the open"), Character->IsProne());

	// A 70 cm ceiling right over the prone player.
	AActor* Ceiling = World.AddSlab(70.f, -300.f, 300.f);
	World.Tick(2);
	TestFalse(TEXT("pure query: can't stand here"), Movement->CanEnterStance(ELureStance::Stand));
	TestFalse(TEXT("pure query: can't crouch here"), Movement->CanEnterStance(ELureStance::Crouch));

	Character->RequestStance(ELureStance::Stand);
	World.Tick(20);
	TestTrue(TEXT("stand request under 70 cm: still prone"), Character->IsProne());
	CheckCapsule(*this, Character, Row(Rows, ELureMovementState::Prone), TEXT("stand blocked"));
	TestFalse(TEXT("stand blocked: no penetration"), IsPenetrating(Character));
	TestEqual(TEXT("stand stays queued"), static_cast<int32>(Character->GetRequestedStance()), static_cast<int32>(ELureStance::Stand));

	Character->RequestStance(ELureStance::Crouch);
	World.Tick(20);
	TestTrue(TEXT("crouch request under 70 cm: still prone"), Character->IsProne());
	CheckCapsule(*this, Character, Row(Rows, ELureMovementState::Prone), TEXT("crouch blocked"));

	// The queued stand completes once there is room.
	Character->RequestStance(ELureStance::Stand);
	World.Tick(5);
	Ceiling->Destroy();
	World.Tick(20);
	TestFalse(TEXT("no ceiling: the queued stand completes"), Character->IsProne());
	TestEqual(TEXT("no ceiling: standing"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Stand));
	CheckCapsule(*this, Character, Row(Rows, ELureMovementState::Stand), TEXT("stood up"));
	TestFalse(TEXT("stood up: no penetration"), IsPenetrating(Character));

	// Crouched under a ceiling just below standing height: stand is blocked too.
	Character->RequestStance(ELureStance::Crouch);
	World.Tick(20);
	AActor* LowRoof = World.AddSlab(2.f * Row(Rows, ELureMovementState::Stand).CapsuleHalfHeight - 1.f, -300.f, 300.f);
	Character->RequestStance(ELureStance::Stand);
	World.Tick(20);
	TestEqual(TEXT("crouched under a roof 1 cm too low: still crouched"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Crouch));
	LowRoof->Destroy();
	World.Tick(20);
	TestEqual(TEXT("roof gone: stands"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Stand));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementCrawlGapTest, "Project.Movement.Stance.ProneFitsUnder60cmGap", LureMovementTest::TestFlags)

bool FLureMovementCrawlGapTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadShippedTable(*this);
	FWorld World;
	if (!Table || !World.Create(*this))
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = ResolvedRows(Table);
	const float SlabMinX = 100.f;
	const float SlabMaxX = 300.f;
	World.AddSlab(60.f, SlabMinX, SlabMaxX);

	ALurePlayerCharacter* Character = World.SpawnCharacter(FVector::ZeroVector, Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	World.Tick(20);

	// Crouched: the gap blocks.
	Character->RequestStance(ELureStance::Crouch);
	World.Tick(20);
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	const float CrouchRadius = Row(Rows, ELureMovementState::Crouch).CapsuleRadius;
	TestTrue(FString::Printf(TEXT("crouch stops at the gap (x = %.1f)"), Character->GetActorLocation().X), Character->GetActorLocation().X < SlabMinX - CrouchRadius + 1.f);

	// Prone: go down right at the mouth and crawl through; the camera never looks through the slab.
	Character->RequestStance(ELureStance::Prone);
	const float ProneFeetZ = FeetZ(Character);
	bool bEverFell = false;
	bool bCameraBelowSlab = true;
	float WorstCameraTop = 0.f;
	for (int32 Frame = 0; Frame < 900 && Character->GetActorLocation().X < SlabMaxX + 40.f; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
		bEverFell |= Movement->IsFalling();
		const FVector Camera = Character->GetFirstPersonCamera()->GetComponentLocation();
		if (Camera.X > SlabMinX && Camera.X < SlabMaxX)
		{
			const float CameraTop = Camera.Z + 10.f; // near clip plane
			WorstCameraTop = FMath::Max(WorstCameraTop, CameraTop);
			bCameraBelowSlab &= CameraTop < 60.f;
		}
	}
	TestTrue(FString::Printf(TEXT("prone crawls through the 60 cm gap (x = %.1f)"), Character->GetActorLocation().X), Character->GetActorLocation().X > SlabMaxX + Row(Rows, ELureMovementState::Prone).CapsuleRadius);
	TestFalse(TEXT("never falls while crawling"), bEverFell);
	TestNearlyEqual(TEXT("feet stay on the floor"), FeetZ(Character), ProneFeetZ, 2.5f);
	TestTrue(FString::Printf(TEXT("camera stays under the slab (worst camera + near clip = %.1f cm)"), WorstCameraTop), bCameraBelowSlab);
	TestFalse(TEXT("no penetration after the gap"), IsPenetrating(Character));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Camera blend
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementEyeBlendTest, "Project.Movement.Camera.EyeHeightBlend", LureMovementTest::TestFlags)

bool FLureMovementEyeBlendTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("start"), ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.f, 0.4f), 165.f);
	TestNearlyEqual(TEXT("half way"), ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.2f, 0.4f), 100.f);
	TestNearlyEqual(TEXT("end"), ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.4f, 0.4f), 35.f);
	TestNearlyEqual(TEXT("past the end (hitch)"), ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 5.f, 0.4f), 35.f);
	TestNearlyEqual(TEXT("zero time snaps"), ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.f, 0.f), 35.f);
	TestNearlyEqual(TEXT("negative time snaps"), ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.f, -1.f), 35.f);
	TestNearlyEqual(TEXT("NaN time snaps"), ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.f, NAN), 35.f);

	// Monotonic, eased at both ends, and the same at any frame rate (time-based).
	float Previous = 165.f;
	bool bMonotonic = true;
	for (int32 Step = 1; Step <= 40; ++Step)
	{
		const float Value = ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.01f * Step, 0.4f);
		bMonotonic &= Value <= Previous + KINDA_SMALL_NUMBER;
		Previous = Value;
	}
	TestTrue(TEXT("monotonic"), bMonotonic);
	const float FirstStep = 165.f - ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.01f, 0.4f);
	const float MiddleStep = ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.19f, 0.4f) - ALurePlayerCharacter::EvaluateEyeBlend(165.f, 35.f, 0.2f, 0.4f);
	TestTrue(TEXT("eases in (first step smaller than the middle one)"), FirstStep < MiddleStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementArmsBobMathTest, "Project.Movement.Camera.ArmsBobMath", LureMovementTest::TestFlags)

bool FLureMovementArmsBobMathTest::RunTest(const FString& Parameters)
{
	const FLureArmsMotionSettings Settings;
	const FLureMovementRow Walk = FLureMovementData::GetFallbackRow(ELureMovementState::Stand);
	const FLureMovementRow Crawl = FLureMovementData::GetFallbackRow(ELureMovementState::Prone);
	const FVector2D NoLook = FVector2D::ZeroVector;

	// Step rate: derived from MaxSpeed when 0, clamped to [1, 3]; an explicit value wins.
	TestNearlyEqual(TEXT("derived step rate at 350 cm/s"), FLureArmsBob::GetStepRate(Walk), 1.2f + 0.0025f * 350.f);
	FLureMovementRow Custom = Walk;
	Custom.MaxSpeed = 5000.f;
	TestNearlyEqual(TEXT("derived step rate clamps at 3"), FLureArmsBob::GetStepRate(Custom), 3.f);
	Custom.BobStepRate = 2.5f;
	TestNearlyEqual(TEXT("explicit step rate"), FLureArmsBob::GetStepRate(Custom), 2.5f);

	// Standing still: no bob.
	FLureArmsBobState Still;
	FTransform Pose = FTransform::Identity;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Pose = FLureArmsBob::Step(Still, Walk, Settings, 0.f, true, false, NoLook, Dt);
	}
	TestTrue(TEXT("still: no offset"), Pose.GetLocation().IsNearlyZero(0.01));
	TestTrue(TEXT("still: no rotation"), Pose.Rotator().IsNearlyZero(0.01));

	// Walking at MaxSpeed: full strength, offsets within the row's amplitudes (and actually reaching them).
	auto Walk4Seconds = [&](FLureArmsBobState& State, bool bHoldingRod, float& OutMinZ, float& OutMaxAbsY, float& OutMaxAbsRoll, float& OutMaxZ)
	{
		OutMinZ = 0.f;
		OutMaxAbsY = 0.f;
		OutMaxAbsRoll = 0.f;
		OutMaxZ = -1.f;
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			const FTransform Step = FLureArmsBob::Step(State, Walk, Settings, Walk.MaxSpeed, true, bHoldingRod, NoLook, Dt);
			if (Frame >= 120)
			{
				OutMinZ = FMath::Min(OutMinZ, static_cast<float>(Step.GetLocation().Z));
				OutMaxZ = FMath::Max(OutMaxZ, static_cast<float>(Step.GetLocation().Z));
				OutMaxAbsY = FMath::Max(OutMaxAbsY, FMath::Abs(static_cast<float>(Step.GetLocation().Y)));
				OutMaxAbsRoll = FMath::Max(OutMaxAbsRoll, FMath::Abs(static_cast<float>(Step.Rotator().Roll)));
			}
		}
	};
	FLureArmsBobState Walking;
	float MinZ, MaxAbsY, MaxAbsRoll, MaxZ;
	Walk4Seconds(Walking, false, MinZ, MaxAbsY, MaxAbsRoll, MaxZ);
	TestTrue(FString::Printf(TEXT("walking: full strength (%.3f)"), Walking.Amplitude), Walking.Amplitude > 0.99f);
	TestTrue(FString::Printf(TEXT("walking: dips to about BobVertical (%.2f of %.2f)"), -MinZ, Walk.BobVertical), -MinZ > 0.9f * Walk.BobVertical && -MinZ <= Walk.BobVertical + 0.01f);
	TestTrue(TEXT("walking: never rises above rest"), MaxZ <= 0.01f);
	TestTrue(FString::Printf(TEXT("walking: sways about BobLateral (%.2f of %.2f)"), MaxAbsY, Walk.BobLateral), MaxAbsY > 0.9f * Walk.BobLateral && MaxAbsY <= Walk.BobLateral + 0.01f);
	TestTrue(FString::Printf(TEXT("walking: rolls about BobRoll (%.2f of %.2f)"), MaxAbsRoll, Walk.BobRoll), MaxAbsRoll > 0.9f * Walk.BobRoll && MaxAbsRoll <= Walk.BobRoll + 0.02f);

	// Holding the rod: BobHoldRodScale.
	FLureArmsBobState WithRod;
	Walk4Seconds(WithRod, true, MinZ, MaxAbsY, MaxAbsRoll, MaxZ);
	TestNearlyEqual(TEXT("rod in hand: dip scaled by BobHoldRodScale"), -MinZ, Settings.BobHoldRodScale * Walk.BobVertical, 0.05f * Walk.BobVertical);

	// In the air: the bob fades out.
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		FLureArmsBob::Step(Walking, Walk, Settings, Walk.MaxSpeed, false, false, NoLook, Dt);
	}
	TestTrue(FString::Printf(TEXT("airborne: fades out (%.3f)"), Walking.Amplitude), Walking.Amplitude < 0.01f);

	// Half speed: half strength.
	FLureArmsBobState HalfSpeed;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		FLureArmsBob::Step(HalfSpeed, Walk, Settings, 0.5f * Walk.MaxSpeed, true, false, NoLook, Dt);
	}
	TestNearlyEqual(TEXT("half speed: half strength"), HalfSpeed.Amplitude, 0.5f, 0.01f);

	// Prone crawl adds yaw and a forward push.
	FLureArmsBobState Crawling;
	float MaxAbsYaw = 0.f;
	float MinX = 0.f;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		const FTransform Step = FLureArmsBob::Step(Crawling, Crawl, Settings, Crawl.MaxSpeed, true, false, NoLook, Dt);
		MaxAbsYaw = FMath::Max(MaxAbsYaw, FMath::Abs(static_cast<float>(Step.Rotator().Yaw)));
		MinX = FMath::Min(MinX, static_cast<float>(Step.GetLocation().X));
	}
	TestTrue(TEXT("crawl: yaws"), MaxAbsYaw > 0.5f * Crawl.BobYaw);
	TestTrue(TEXT("crawl: pushes forward and back"), -MinX > 0.5f * Crawl.BobForward);

	// Look sway: lags the turn, clamped.
	FLureArmsBobState Look;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Pose = FLureArmsBob::Step(Look, Walk, Settings, 0.f, true, false, FVector2D(1000.f, 0.f), Dt);
	}
	TestNearlyEqual(TEXT("fast turn right: yaw sway clamped to -LookSwayMaxDeg"), static_cast<float>(Pose.Rotator().Yaw), -Settings.LookSwayMaxDeg, 0.05f);
	FLureArmsBobState LookUp;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Pose = FLureArmsBob::Step(LookUp, Walk, Settings, 0.f, true, false, FVector2D(0.f, 50.f), Dt);
	}
	TestNearlyEqual(TEXT("looking up at 50 deg/s: pitch sway -1 deg"), static_cast<float>(Pose.Rotator().Pitch), -50.f * Settings.LookSwayPerDegPerSec, 0.05f);

	// Deterministic, and bad inputs stay finite.
	FLureArmsBobState A;
	FLureArmsBobState B;
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		const FTransform PoseA = FLureArmsBob::Step(A, Walk, Settings, 300.f, true, false, FVector2D(20.f, -5.f), Dt);
		const FTransform PoseB = FLureArmsBob::Step(B, Walk, Settings, 300.f, true, false, FVector2D(20.f, -5.f), Dt);
		if (!PoseA.Equals(PoseB, 1e-6))
		{
			AddError(TEXT("bob is not deterministic"));
			break;
		}
	}
	FLureArmsBobState Bad;
	Pose = FLureArmsBob::Step(Bad, Walk, Settings, NAN, true, false, FVector2D(0.f, 0.f), NAN);
	TestFalse(TEXT("NaN inputs give a finite pose"), Pose.ContainsNaN());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementArmsBobWorldTest, "Project.Movement.Camera.ArmsBobWhileWalking", LureMovementTest::TestFlags)

bool FLureMovementArmsBobWorldTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadShippedTable(*this);
	FWorld World;
	if (!Table || !World.Create(*this))
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = ResolvedRows(Table);
	ALurePlayerCharacter* Player = World.SpawnCharacter(FVector::ZeroVector, Table);
	ALurePlayerCharacter* Other = World.SpawnCharacter(FVector(0.f, 400.f, 0.f), Table);
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("characters and controller spawn"), Player) || !Other || !Controller)
	{
		return false;
	}
	// UE 5.8: without a net driver, a PlayerController counts as local only with a LocalPlayer (or this flag, which SetPlayer sets).
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	World.Tick(20);
	TestTrue(TEXT("player is locally controlled"), Player->IsLocallyControlled());

	TestTrue(TEXT("standing still: arms at rest"), Player->GetArmsBobOffset().GetLocation().IsNearlyZero(0.05));

	float LargestOffset = 0.f;
	bool bCameraSteady = true;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Player->AddMovementInput(FVector::ForwardVector, 1.f, true);
		Other->AddMovementInput(FVector::ForwardVector, 1.f, true);
		World.Tick(1);
		LargestOffset = FMath::Max(LargestOffset, static_cast<float>(Player->GetArmsBobOffset().GetLocation().Size()));
		const FVector CameraRelative = Player->GetFirstPersonCamera()->GetRelativeLocation();
		const float ExpectedZ = Player->GetCurrentEyeHeight() - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		bCameraSteady &= FMath::IsNearlyZero(CameraRelative.X, 0.001) && FMath::IsNearlyZero(CameraRelative.Y, 0.001) && FMath::IsNearlyEqual(CameraRelative.Z, ExpectedZ, 0.01);
	}
	TestTrue(FString::Printf(TEXT("walking: the arms bob (largest offset %.2f cm)"), LargestOffset), LargestOffset > 0.3f);
	TestTrue(TEXT("walking: the camera itself doesn't bob"), bCameraSteady);
	TestTrue(TEXT("not locally controlled: no bob (cosmetic, owner only)"), Other->GetArmsBobOffset().Equals(FTransform::Identity, 1e-4));

	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		World.Tick(1);
	}
	TestTrue(FString::Printf(TEXT("stopped: arms back at rest (%.3f cm)"), Player->GetArmsBobOffset().GetLocation().Size()), Player->GetArmsBobOffset().GetLocation().IsNearlyZero(0.05));

	// Stance changes and landings with no arms animation imported are safe no-ops.
	TestFalse(TEXT("no anim instance: the stance dip is skipped"), Player->PlayStanceDip(1.f));
	Player->RequestStance(ELureStance::Crouch);
	World.Tick(20);
	Player->RequestStance(ELureStance::Stand);
	World.Tick(20);
	TestEqual(TEXT("back to stand"), static_cast<int32>(Player->GetStance()), static_cast<int32>(ELureStance::Stand));

	Controller->UnPossess();
	World.Tick(2);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Prediction flags
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementCompressedFlagsTest, "Project.Movement.Net.CompressedFlags", LureMovementTest::TestFlags)

bool FLureMovementCompressedFlagsTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadShippedTable(*this);
	FWorld World;
	if (!Table || !World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.SpawnCharacter(FVector::ZeroVector, Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	World.Tick(10);

	FNetworkPredictionData_Client_Character* ClientData = Movement->GetPredictionData_Client_Character();
	if (!TestNotNull(TEXT("client prediction data"), ClientData))
	{
		return false;
	}

	auto FlagsFor = [&](bool bSprint, bool bProne, bool bCrouch)
	{
		Movement->SetSprintRequested(bSprint);
		Movement->SetProneRequested(bProne);
		Movement->bWantsToCrouch = bCrouch;
		FSavedMovePtr Move = ClientData->AllocateNewMove();
		Move->SetMoveFor(Character, Dt, FVector::ZeroVector, *ClientData);
		return Move->GetCompressedFlags();
	};

	const uint8 None = FlagsFor(false, false, false);
	const uint8 Sprint = FlagsFor(true, false, false);
	const uint8 Prone = FlagsFor(false, true, false);
	const uint8 Crouch = FlagsFor(false, false, true);
	TestEqual(TEXT("sprint = FLAG_Custom_0"), static_cast<int32>(Sprint ^ None), static_cast<int32>(FSavedMove_Character::FLAG_Custom_0));
	TestEqual(TEXT("prone = FLAG_Custom_1"), static_cast<int32>(Prone ^ None), static_cast<int32>(FSavedMove_Character::FLAG_Custom_1));
	TestEqual(TEXT("crouch = engine FLAG_WantsToCrouch"), static_cast<int32>(Crouch ^ None), static_cast<int32>(FSavedMove_Character::FLAG_WantsToCrouch));

	// Server side: the flags restore the wishes (all combinations).
	for (int32 Bits = 0; Bits < 8; ++Bits)
	{
		const bool bSprint = (Bits & 1) != 0;
		const bool bProne = (Bits & 2) != 0;
		const bool bCrouch = (Bits & 4) != 0;
		const uint8 Flags = FlagsFor(bSprint, bProne, bCrouch);
		Movement->SetSprintRequested(!bSprint);
		Movement->SetProneRequested(!bProne);
		Movement->bWantsToCrouch = !bCrouch;
		Movement->UpdateFromCompressedFlags(Flags);
		TestEqual(FString::Printf(TEXT("round trip %d: sprint"), Bits), Movement->IsSprintRequested(), bSprint);
		TestEqual(FString::Printf(TEXT("round trip %d: prone"), Bits), Movement->IsProneRequested(), bProne);
		TestEqual(FString::Printf(TEXT("round trip %d: crouch"), Bits), Movement->IsCrouchRequested(), bCrouch);
	}

	// Moves that differ in sprint or prone never combine; Clear drops the custom bits.
	Movement->SetSprintRequested(true);
	Movement->SetProneRequested(false);
	Movement->bWantsToCrouch = false;
	FSavedMovePtr SprintMove = ClientData->AllocateNewMove();
	SprintMove->SetMoveFor(Character, Dt, FVector::ZeroVector, *ClientData);
	Movement->SetSprintRequested(false);
	FSavedMovePtr WalkMove = ClientData->AllocateNewMove();
	WalkMove->SetMoveFor(Character, Dt, FVector::ZeroVector, *ClientData);
	TestFalse(TEXT("sprint and walk moves don't combine"), SprintMove->CanCombineWith(WalkMove, Character, 1.f));
	SprintMove->Clear();
	TestEqual(TEXT("Clear drops the custom bits"), static_cast<int32>(SprintMove->GetCompressedFlags() & (FSavedMove_Character::FLAG_Custom_0 | FSavedMove_Character::FLAG_Custom_1)), 0);

	// Replication setup of the prone posture.
	const FProperty* ProneProperty = ALurePlayerCharacter::StaticClass()->FindPropertyByName(TEXT("bIsProne"));
	if (TestNotNull(TEXT("bIsProne property"), ProneProperty))
	{
		TestTrue(TEXT("bIsProne is replicated"), ProneProperty->HasAnyPropertyFlags(CPF_Net));
		TestTrue(TEXT("bIsProne has a RepNotify"), ProneProperty->HasAnyPropertyFlags(CPF_RepNotify));
		TestEqual(TEXT("RepNotify is OnRep_IsProne"), ProneProperty->RepNotifyFunc, FName(TEXT("OnRep_IsProne")));
	}

	// A simulated proxy applies the replicated prone state to its capsule.
	Movement->SetProneRequested(false);
	Movement->bWantsToCrouch = false;
	World.Tick(5);
	Character->SetRole(ROLE_SimulatedProxy);
	Character->SetIsProne(true);
	Character->OnRep_IsProne();
	CheckCapsule(*this, Character, Row(ResolvedRows(Table), ELureMovementState::Prone), TEXT("proxy prone"));
	Character->SetIsProne(false);
	Character->OnRep_IsProne();
	CheckCapsule(*this, Character, Row(ResolvedRows(Table), ELureMovementState::Stand), TEXT("proxy stand"));
	Character->SetRole(ROLE_Authority);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementInputTest, "Project.Movement.Input.ActionsAndMappings", LureMovementTest::TestFlags)

bool FLureMovementInputTest::RunTest(const FString& Parameters)
{
	struct FExpectedAction
	{
		FName Name;
		EInputActionValueType Type;
	};
	const FExpectedAction Expected[] = {
		{ TEXT("Move"), EInputActionValueType::Axis2D },
		{ TEXT("Look"), EInputActionValueType::Axis2D },
		{ TEXT("Jump"), EInputActionValueType::Boolean },
		{ TEXT("Sprint"), EInputActionValueType::Boolean },
		{ TEXT("Crouch"), EInputActionValueType::Boolean },
		{ TEXT("Prone"), EInputActionValueType::Boolean },
	};

	UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	if (!TestNotNull(TEXT("default mapping context"), Context))
	{
		return false;
	}

	const FKey PlaytestKey = GetDefault<UPlaytestFeedbackSubsystem>()->FeedbackKey;
	TMap<FKey, FName> KeyOwner;
	for (const FExpectedAction& Action : Expected)
	{
		UInputAction* Found = ULureInputSubsystem::GetInputActionByName(Action.Name);
		if (!TestNotNull(FString::Printf(TEXT("%s action exists"), *Action.Name.ToString()), Found))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s value type"), *Action.Name.ToString()), static_cast<int32>(Found->ValueType), static_cast<int32>(Action.Type));
		TestTrue(FString::Printf(TEXT("%s lookup is stable"), *Action.Name.ToString()), ULureInputSubsystem::GetInputActionByName(Action.Name) == Found);

		bool bHasKeyboardOrMouse = false;
		bool bHasGamepad = false;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action != Found)
			{
				continue;
			}
			bHasGamepad |= Mapping.Key.IsGamepadKey();
			bHasKeyboardOrMouse |= !Mapping.Key.IsGamepadKey();
			TestFalse(FString::Printf(TEXT("%s is not on the playtest key %s"), *Action.Name.ToString(), *PlaytestKey.ToString()), Mapping.Key == PlaytestKey);
			if (const FName* Other = KeyOwner.Find(Mapping.Key))
			{
				TestEqual(FString::Printf(TEXT("key %s drives one action only"), *Mapping.Key.ToString()), *Other, Action.Name);
			}
			KeyOwner.Add(Mapping.Key, Action.Name);
		}
		TestTrue(FString::Printf(TEXT("%s has a keyboard/mouse key"), *Action.Name.ToString()), bHasKeyboardOrMouse);
		TestTrue(FString::Printf(TEXT("%s has a gamepad key"), *Action.Name.ToString()), bHasGamepad);
	}

	TestNull(TEXT("unknown name gives null"), ULureInputSubsystem::GetInputActionByName(TEXT("Fly")));
	TestNull(TEXT("None gives null"), ULureInputSubsystem::GetInputActionByName(NAME_None));

	// Default keys the design and the playtester rely on.
	auto IsMapped = [&](FName ActionName, const FKey& Key)
	{
		const UInputAction* Action = ULureInputSubsystem::GetInputActionByName(ActionName);
		return Context->GetMappings().ContainsByPredicate([&](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action && Mapping.Key == Key; });
	};
	TestTrue(TEXT("W moves"), IsMapped(TEXT("Move"), EKeys::W));
	TestTrue(TEXT("mouse looks"), IsMapped(TEXT("Look"), EKeys::Mouse2D));
	TestTrue(TEXT("Space jumps"), IsMapped(TEXT("Jump"), EKeys::SpaceBar));
	TestTrue(TEXT("Left Shift sprints"), IsMapped(TEXT("Sprint"), EKeys::LeftShift));
	TestTrue(TEXT("C crouches"), IsMapped(TEXT("Crouch"), EKeys::C));
	TestTrue(TEXT("Left Ctrl crouches"), IsMapped(TEXT("Crouch"), EKeys::LeftControl));
	TestTrue(TEXT("Z goes prone"), IsMapped(TEXT("Prone"), EKeys::Z));

	const ULureCharacterSettings* Settings = GetDefault<ULureCharacterSettings>();
	TestFalse(TEXT("sprint is hold by default"), Settings->bSprintIsToggle);
	TestTrue(TEXT("crouch is toggle by default"), Settings->bCrouchIsToggle);
	TestTrue(TEXT("prone is toggle by default"), Settings->bProneIsToggle);

	// Python/Blueprint can call the lookup without an instance.
	const UFunction* Lookup = ULureInputSubsystem::StaticClass()->FindFunctionByName(TEXT("GetInputActionByName"));
	if (TestNotNull(TEXT("GetInputActionByName is a UFUNCTION"), Lookup))
	{
		TestTrue(TEXT("GetInputActionByName is static and BlueprintCallable"), Lookup->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintCallable));
	}

	// The actions survive garbage collection (they are owned by the engine subsystem).
	UInputAction* Before = ULureInputSubsystem::GetInputActionByName(TEXT("Sprint"));
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("same Sprint action after GC"), IsValid(Before) && ULureInputSubsystem::GetInputActionByName(TEXT("Sprint")) == Before);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup: game mode, settings, character composition
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureMovementSetupTest, "Project.Movement.Setup.GameModeAndCharacter", LureMovementTest::TestFlags)

bool FLureMovementSetupTest::RunTest(const FString& Parameters)
{
	// Global default game mode and its pawn.
	const FString GameModePath = UGameMapsSettings::GetGlobalDefaultGameMode();
	const UClass* GameModeClass = LoadClass<AGameModeBase>(nullptr, *GameModePath);
	TestTrue(FString::Printf(TEXT("global default game mode is ALureGameMode (%s)"), *GameModePath), GameModeClass && GameModeClass->IsChildOf(ALureGameMode::StaticClass()));
	TestTrue(TEXT("ALureGameMode spawns ALurePlayerCharacter"), GetDefault<ALureGameMode>()->DefaultPawnClass == ALurePlayerCharacter::StaticClass());

	// Settings point at the data table.
	TestEqual(TEXT("settings: DT_Movement path"), GetDefault<ULureCharacterSettings>()->MovementTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_Movement.DT_Movement")));

	// Character defaults.
	const ALurePlayerCharacter* Defaults = GetDefault<ALurePlayerCharacter>();
	TestTrue(TEXT("movement component is ULureCharacterMovementComponent"), Defaults->GetLureMovement() != nullptr);
	TestTrue(TEXT("body yaw follows the controller"), Defaults->bUseControllerRotationYaw);
	TestFalse(TEXT("no orient-to-movement"), Defaults->GetCharacterMovement()->bOrientRotationToMovement);
	TestTrue(TEXT("walking off ledges while crouched is allowed"), Defaults->GetCharacterMovement()->bCanWalkOffLedgesWhenCrouching);
	TestTrue(TEXT("replicates"), Defaults->GetIsReplicated() && Defaults->IsReplicatingMovement());
	// Asset paths from art/export/Characters/SK_FPArms.anim.md (imported by the editor-operator after the merge).
	TestEqual(TEXT("arms soft path"), Defaults->FirstPersonArmsMesh.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Art/Characters/FPArms/SK_FPArms.SK_FPArms")));
	TestEqual(TEXT("arms anim class path"), Defaults->FirstPersonArmsAnimClass.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Art/Characters/FPArms/ABP_FPArms.ABP_FPArms_C")));
	TestEqual(TEXT("stance dip path"), Defaults->StanceDipAnimation.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Art/Characters/FPArms/A_FPArms_StanceDip.A_FPArms_StanceDip")));
	TestEqual(TEXT("stance dip slot"), Defaults->StanceAdditiveSlot, FName(TEXT("StanceAdditive")));

	// A spawned character: camera, arms and body setup, and null-safe arms when the asset is missing.
	UDataTable* Table = LoadShippedTable(*this);
	FWorld World;
	if (!Table || !World.Create(*this))
	{
		return false;
	}
	const FTransform Transform(FRotator::ZeroRotator, FVector(0.f, 0.f, 95.f));
	ALurePlayerCharacter* Character = World.World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	Character->FirstPersonArmsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Nope/SK_Missing.SK_Missing")));
	Character->GetLureMovement()->ApplyMovementTable(Table);
	Character->FinishSpawning(Transform);
	World.Tick(30);

	const UCameraComponent* Camera = Character->GetFirstPersonCamera();
	if (TestNotNull(TEXT("camera"), Camera))
	{
		TestTrue(TEXT("camera uses the control rotation"), Camera->bUsePawnControlRotation);
		TestTrue(TEXT("camera is attached to the capsule"), Camera->GetAttachParent() == Character->GetCapsuleComponent());
		TestTrue(TEXT("first-person FOV on, 90 deg"), Camera->bEnableFirstPersonFieldOfView && FMath::IsNearlyEqual(Camera->FirstPersonFieldOfView, 90.f));
		TestTrue(TEXT("first-person scale on, 0.6"), Camera->bEnableFirstPersonScale && FMath::IsNearlyEqual(Camera->FirstPersonScale, 0.6f));
	}
	const USkeletalMeshComponent* Arms = Character->GetFirstPersonArms();
	if (TestNotNull(TEXT("arms component"), Arms))
	{
		TestTrue(TEXT("arms: only the owner sees them"), Arms->bOnlyOwnerSee);
		TestTrue(TEXT("arms: first-person primitive"), Arms->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson);
		TestTrue(TEXT("arms: always tick pose"), Arms->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
		TestTrue(TEXT("arms: attached to the camera"), Arms->GetAttachParent() == Camera);
		TestEqual(TEXT("arms: no collision"), Arms->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("arms: no shadow"), Arms->CastShadow);
		TestFalse(TEXT("arms: not replicated"), Arms->GetIsReplicated());
		TestNull(TEXT("arms: missing asset leaves no mesh (null-safe)"), Arms->GetSkeletalMeshAsset());
	}
	const UStaticMeshComponent* Body = Character->GetPlaceholderBody();
	if (TestNotNull(TEXT("placeholder body"), Body))
	{
		TestTrue(TEXT("body: hidden from its owner"), Body->bOwnerNoSee);
		TestEqual(TEXT("body: no collision"), Body->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestNotNull(TEXT("body: engine cylinder mesh"), Body->GetStaticMesh().Get());
	}
	TestTrue(TEXT("third-person mesh slot hidden from its owner"), Character->GetMesh()->bOwnerNoSee);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

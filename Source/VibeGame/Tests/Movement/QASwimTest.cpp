// Lure T-026 QA (qa-engineer): independent tests for surface swimming and the T-004 playtest fixes.
// Written black-box from docs/specs/swimming.md, docs/specs/movement-rules.md and the T-026/T-004 acceptance lines, plus
// the test gaps T0 and T2-T5 of the T-026 review (Saved/AgentLogs/review/20260923-T026-ultracode-review-partial.md in main).
// Paths: Project.Movement.QA.Swim.*, Project.Movement.QA.Climb.*, Project.Movement.QA.Camera.ArmsPullBackFollowsData.
// Tables: data/tables/DT_Movement.csv loaded as text (rows edited in memory for fixtures), never the binary asset.
// Worlds: transient game worlds stepped at 60 Hz; the water surface is z = 0 unless a test says otherwise.
// Everything lives in namespace QASwim (no file-scope using-directive: unity builds merge test files).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureLadder.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Character/LureWaterVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Tests/Movement/LureMovementTestAccess.h"
#include "Tests/Movement/LureSwimTestListener.h"

namespace QASwim
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Seabed of the deep test sea, cm. */
	constexpr float SeabedZ = -600.f;
	/** A walking capsule hovers 1.9-2.4 cm above its floor; spawns use this gap. */
	constexpr float SpawnFloorGap = 2.15f;

	// ---- Tables ----

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
		return Table ? Table->FindRow<FLureMovementRow>(FLureMovementData::GetRowName(State), TEXT("QASwim"), false) : nullptr;
	}

	/** Sets a value on both swim rows (Swim and SwimSprint). */
	void EditSwimRows(UDataTable* Table, TFunctionRef<void(FLureMovementRow&)> Edit)
	{
		for (const ELureMovementState State : { ELureMovementState::Swim, ELureMovementState::SwimSprint })
		{
			if (FLureMovementRow* Row = EditRow(Table, State))
			{
				Edit(*Row);
			}
		}
	}

	FLureMovementRow RowOf(const UDataTable* Table, ELureMovementState State, uint8* OutFallbackMask = nullptr)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		const uint8 Mask = FLureMovementData::ResolveRows(Table, Rows, Problems);
		if (OutFallbackMask)
		{
			*OutFallbackMask = Mask;
		}
		return Rows[static_cast<int32>(State)];
	}

	// ---- Swim events ----

	/** Records OnSwimStateChanged for one character (rooted listener, released when this goes out of scope). */
	struct FSwimEvents
	{
		ULureSwimTestListener* Listener = nullptr;

		explicit FSwimEvents(ALurePlayerCharacter* Character)
		{
			Listener = NewObject<ULureSwimTestListener>();
			Listener->AddToRoot();
			if (Character)
			{
				Character->OnSwimStateChanged.AddDynamic(Listener, &ULureSwimTestListener::OnSwimStateChanged);
			}
		}
		~FSwimEvents()
		{
			Listener->RemoveFromRoot();
		}
		FSwimEvents(const FSwimEvents&) = delete;
		FSwimEvents& operator=(const FSwimEvents&) = delete;

		FString Text() const
		{
			TArray<FString> Parts;
			for (const bool bIn : Listener->Events)
			{
				Parts.Add(bIn ? TEXT("in") : TEXT("out"));
			}
			return FString::Join(Parts, TEXT(","));
		}
		int32 Num() const { return Listener->Events.Num(); }
	};

	// ---- Measurements ----

	float CenterZ(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetCapsuleComponent()->GetComponentLocation().Z);
	}

	float FeetZ(const ALurePlayerCharacter* Character)
	{
		return CenterZ(Character) - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	float X(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetActorLocation().X);
	}

	bool IsPenetrating(const ALurePlayerCharacter* Character)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QASwimOverlap), false, Character);
		return Character->GetWorld()->OverlapBlockingTestByChannel(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(), ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
	}

	FString ModeText(const ULureCharacterMovementComponent* Movement)
	{
		if (Movement->IsClimbingOut())
		{
			return TEXT("ClimbOut");
		}
		if (Movement->IsLedgeClimbing())
		{
			return TEXT("LedgeClimb");
		}
		switch (Movement->MovementMode.GetValue())
		{
		case MOVE_Walking: return TEXT("Walking");
		case MOVE_Falling: return TEXT("Falling");
		case MOVE_Swimming: return TEXT("Swimming");
		case MOVE_Custom: return TEXT("Custom");
		default: return TEXT("Other");
		}
	}

	FString StanceText(ELureStance Stance)
	{
		switch (Stance)
		{
		case ELureStance::Crouch: return TEXT("Crouch");
		case ELureStance::Prone: return TEXT("Prone");
		default: return TEXT("Stand");
		}
	}

	// ---- The test world ----

	struct FSea
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		/** An empty game world (add geometry and water yourself). */
		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			return World != nullptr;
		}

		/** The standard sea: seabed at SeabedZ and one big water volume, surface z = 0. */
		bool CreateSea(FAutomationTestBase& Test, float Seabed = SeabedZ)
		{
			if (!Create(Test))
			{
				return false;
			}
			AddBox(FVector(0.f, 0.f, Seabed - 50.f), FVector(5000.f, 5000.f, 50.f));
			return Test.TestNotNull(TEXT("water volume spawns"), AddWater(FVector::ZeroVector, FVector2D(4500.0, 4500.0), FMath::Max(800.f, -Seabed + 200.f)));
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

		/** A dock (solid from the seabed up) whose water-side face is at x = FaceX, extending 500 cm toward +X, top at Height. */
		AActor* AddDock(float FaceX, float Height, float Seabed = SeabedZ)
		{
			return AddBox(FVector(FaceX + 250.f, 0.f, 0.5f * (Height + Seabed)), FVector(250.f, 500.f, 0.5f * (Height - Seabed)));
		}

		ALureWaterVolume* AddWater(const FVector& SurfaceCenter, const FVector2D& HalfSize, float Depth)
		{
			const FTransform Transform(FRotator::ZeroRotator, SurfaceCenter);
			ALureWaterVolume* Volume = World->SpawnActorDeferred<ALureWaterVolume>(ALureWaterVolume::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Volume)
			{
				return nullptr;
			}
			Volume->SurfaceHalfSize = HalfSize;
			Volume->WaterDepth = Depth;
			Volume->FinishSpawning(Transform);
			return Volume;
		}

		ALureLadder* AddLadder(const FVector& Location, float Yaw, float MaxClimbHeight, float ClimbSpeed = 0.f)
		{
			const FTransform Transform(FRotator(0.f, Yaw, 0.f), Location);
			ALureLadder* Ladder = World->SpawnActorDeferred<ALureLadder>(ALureLadder::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Ladder)
			{
				return nullptr;
			}
			Ladder->MaxClimbHeight = MaxClimbHeight;
			Ladder->ClimbSpeed = ClimbSpeed;
			Ladder->FinishSpawning(Transform);
			return Ladder;
		}

		/** Spawns the character with its capsule center at Center; the table is applied before BeginPlay. */
		ALurePlayerCharacter* Spawn(FAutomationTestBase& Test, const FVector& Center, const UDataTable* Table, float Yaw = 0.f,
			TFunction<void(ALurePlayerCharacter&)> PreFinish = nullptr)
		{
			const FTransform Transform(FRotator(0.f, Yaw, 0.f), Center);
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Test.TestNotNull(TEXT("character spawns"), Character))
			{
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Table);
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			if (PreFinish)
			{
				PreFinish(*Character);
			}
			Character->FinishSpawning(Transform);
			return Character;
		}

		/** Spawns the character standing with its feet at Feet. */
		ALurePlayerCharacter* SpawnOnFeet(FAutomationTestBase& Test, const FVector& Feet, const UDataTable* Table, float Yaw = 0.f)
		{
			const float HalfHeight = RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight;
			return Spawn(Test, Feet + FVector(0.f, 0.f, HalfHeight + SpawnFloorGap), Table, Yaw);
		}

		/** 1 m above the water at (X, Y): falls in and floats. */
		ALurePlayerCharacter* SpawnSwimmer(FAutomationTestBase& Test, float InX, float InY, const UDataTable* Table, float Yaw = 0.f, int32 SettleFrames = 180)
		{
			ALurePlayerCharacter* Character = Spawn(Test, FVector(InX, InY, 190.f), Table, Yaw);
			if (!Character)
			{
				return nullptr;
			}
			Tick(SettleFrames);
			if (!Test.TestTrue(TEXT("setup: fell in and swims"), Character->GetLureMovement()->IsSwimming()))
			{
				return nullptr;
			}
			return Character;
		}

		void Tick(int32 Frames, float DeltaTime = Dt)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(DeltaTime);
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

		/** Ticks (with optional input) until Predicate holds or MaxFrames pass. */
		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, ALurePlayerCharacter* MoveCharacter = nullptr, const FVector& Direction = FVector::ForwardVector)
		{
			for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
			{
				if (Predicate())
				{
					return true;
				}
				if (MoveCharacter)
				{
					MoveCharacter->AddMovementInput(Direction, 1.f, true);
				}
				Wrapper.TickTestWorld(Dt);
			}
			return Predicate();
		}
	};

	/** Common checks for "in the water now, settled": swimming, standing, Stand capsule, floating at the row's depth, no penetration. */
	void TestSettledSwimmer(FAutomationTestBase& Test, const ALurePlayerCharacter* Character, const UDataTable* Table, const FString& Label)
	{
		const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		const FLureMovementRow Stand = RowOf(Table, ELureMovementState::Stand);
		const FLureMovementRow Swim = RowOf(Table, ELureMovementState::Swim);
		Test.TestTrue(Label + TEXT(": swimming"), Movement->IsSwimming());
		Test.TestEqual(Label + TEXT(": stance Stand in the water"), StanceText(Character->GetStance()), FString(TEXT("Stand")));
		Test.TestEqual(Label + TEXT(": no crouch/prone wish left"), StanceText(Character->GetRequestedStance()), FString(TEXT("Stand")));
		float Radius = 0.f;
		float HalfHeight = 0.f;
		Character->GetCapsuleComponent()->GetUnscaledCapsuleSize(Radius, HalfHeight);
		Test.TestNearlyEqual(Label + TEXT(": Stand capsule half height"), HalfHeight, Stand.CapsuleHalfHeight, 0.05f);
		Test.TestNearlyEqual(Label + TEXT(": Stand capsule radius"), Radius, Stand.CapsuleRadius, 0.05f);
		Test.TestNearlyEqual(Label + TEXT(": floats at the row's depth"), CenterZ(Character), -Swim.SurfaceFloatDepth, 1.f);
		Test.TestNearlyEqual(Label + TEXT(": swim eye height"), Character->GetCurrentEyeHeight(), Swim.EyeHeight, 0.5f);
		Test.TestFalse(Label + TEXT(": no penetration"), IsPenetrating(Character));
	}

	/** Swims toward the face (Direction) until pressed against it, then Jump; returns true if it ended standing on land. */
	bool SwimToFaceAndJump(FSea& Sea, ALurePlayerCharacter* Character, const FVector& Direction, int32 ApproachFrames = 240)
	{
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Sea.TickMoving(Character, ApproachFrames, Direction);
		Sea.Tick(30); // coast to a stop against the face
		Character->Jump();
		const bool bOut = Sea.TickUntil([&]() { return Movement->IsMovingOnGround(); }, 240);
		Character->StopJumping();
		Sea.Tick(20);
		return bOut && Movement->IsMovingOnGround();
	}
}

namespace QASwim
{

// =====================================================================================================================
// Entering the water: every stance, sprinting, a jump at the edge, falling from height (review T3)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimEnterFromEveryStance, "Project.Movement.QA.Swim.Enter.FromEveryStanceOffAnEdge", QASwim::Flags)

bool FQASwimEnterFromEveryStance::RunTest(const FString& Parameters)
{
	// Walk / sprint / crouch-walk / crawl off a jetty into deep water: you swim, standing (Stand capsule, no crouch or prone
	// wish kept), exactly one "in" event, floating at the Swim row's depth. Then turn round and press Jump at the jetty:
	// a 30 cm jetty lets you out (standing, one "out"), a 100 cm one doesn't (over the 60 cm rule).
	struct FCase
	{
		const TCHAR* Label;
		ELureStance Stance;
		bool bSprint;
		float EdgeHeight;
	};
	const FCase Cases[] = {
		{ TEXT("walk, 30 cm jetty"), ELureStance::Stand, false, 30.f },
		{ TEXT("sprint, 30 cm jetty"), ELureStance::Stand, true, 30.f },
		{ TEXT("crouch, 30 cm jetty"), ELureStance::Crouch, false, 30.f },
		{ TEXT("prone, 30 cm jetty"), ELureStance::Prone, false, 30.f },
		{ TEXT("walk, 100 cm dock"), ELureStance::Stand, false, 100.f },
		{ TEXT("sprint, 100 cm dock"), ELureStance::Stand, true, 100.f },
		{ TEXT("crouch, 100 cm dock"), ELureStance::Crouch, false, 100.f },
		{ TEXT("prone, 100 cm dock"), ELureStance::Prone, false, 100.f },
	};
	for (const FCase& Case : Cases)
	{
		const FString Label = Case.Label;
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this))
		{
			return false;
		}
		// The jetty: face at x = 0, land toward -X, water toward +X.
		Sea.AddBox(FVector(-500.f, 0.f, 0.5f * (Case.EdgeHeight + SeabedZ)), FVector(500.f, 600.f, 0.5f * (Case.EdgeHeight - SeabedZ)));
		ALurePlayerCharacter* Character = Sea.SpawnOnFeet(*this, FVector(-150.f, 0.f, Case.EdgeHeight), Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Sea.Tick(10);
		if (Case.Stance != ELureStance::Stand)
		{
			Character->RequestStance(Case.Stance);
			Sea.Tick(45);
			TestEqual(Label + TEXT(": setup stance"), StanceText(Character->GetStance()), StanceText(Case.Stance));
		}
		Character->SetSprintRequested(Case.bSprint);
		FSwimEvents Events(Character);

		TestTrue(Label + TEXT(": goes off the edge into the water"), Sea.TickUntil([&]() { return Movement->IsSwimming(); }, 900, Character, FVector::ForwardVector));
		Sea.TickMoving(Character, 30, FVector::ForwardVector);
		TestEqual(Label + TEXT(": row while moving in the water"), static_cast<int32>(Movement->GetMovementState()),
			static_cast<int32>(Case.bSprint ? ELureMovementState::SwimSprint : ELureMovementState::Swim));
		Character->SetSprintRequested(false);
		Sea.Tick(180);
		TestSettledSwimmer(*this, Character, Table, Label);
		TestEqual(Label + TEXT(": events after settling"), Events.Text(), FString(TEXT("in")));

		// Leaving again: face the jetty and press Jump.
		Character->SetActorRotation(FRotator(0.f, 180.f, 0.f));
		const bool bOut = SwimToFaceAndJump(Sea, Character, FVector::BackwardVector);
		const bool bExpectOut = Case.EdgeHeight <= RowOf(Table, ELureMovementState::Swim).ClimbMaxHeight;
		TestEqual(Label + TEXT(": Jump at the jetty gets you out"), bOut, bExpectOut);
		if (bExpectOut)
		{
			TestNearlyEqual(Label + TEXT(": feet on the jetty"), FeetZ(Character), Case.EdgeHeight, 3.f);
			TestEqual(Label + TEXT(": standing on land"), StanceText(Character->GetStance()), FString(TEXT("Stand")));
			TestEqual(Label + TEXT(": events: in, out"), Events.Text(), FString(TEXT("in,out")));
		}
		else
		{
			TestTrue(Label + TEXT(": still swimming"), Movement->IsSwimming());
			TestEqual(Label + TEXT(": events: in"), Events.Text(), FString(TEXT("in")));
		}
		TestFalse(Label + TEXT(": no penetration at the end"), IsPenetrating(Character));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimEnterJumpFromEdge, "Project.Movement.QA.Swim.Enter.JumpFromTheEdge", QASwim::Flags)

bool FQASwimEnterJumpFromEdge::RunTest(const FString& Parameters)
{
	// Standing or crouched right at a 30 cm jetty's edge, Jump while pushing toward the water: a normal jump, then a
	// swim. No ledge climb anywhere, one "in" event.
	for (const ELureStance Stance : { ELureStance::Stand, ELureStance::Crouch })
	{
		const FString Label = StanceText(Stance) + TEXT(" jump");
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this))
		{
			return false;
		}
		const float EdgeHeight = 30.f;
		Sea.AddBox(FVector(-500.f, 0.f, 0.5f * (EdgeHeight + SeabedZ)), FVector(500.f, 600.f, 0.5f * (EdgeHeight - SeabedZ)));
		const float Radius = RowOf(Table, ELureMovementState::Stand).CapsuleRadius;
		ALurePlayerCharacter* Character = Sea.SpawnOnFeet(*this, FVector(-Radius - 6.f, 0.f, EdgeHeight), Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Sea.Tick(10);
		if (Stance == ELureStance::Crouch)
		{
			Character->RequestStance(Stance);
			Sea.Tick(30);
		}
		FSwimEvents Events(Character);
		Character->Jump();
		bool bWentUp = false;
		bool bClimb = false;
		for (int32 Frame = 0; Frame < 240 && !Movement->IsSwimming(); ++Frame)
		{
			Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
			Sea.Tick(1);
			bWentUp |= FeetZ(Character) > EdgeHeight + 20.f;
			bClimb |= Movement->IsClimbing();
		}
		Character->StopJumping();
		TestTrue(Label + TEXT(": a real jump (rose above the jetty)"), bWentUp);
		TestFalse(Label + TEXT(": no climb on the way"), bClimb);
		TestTrue(Label + TEXT(": in the water"), Movement->IsSwimming());
		TestTrue(Label + TEXT(": past the edge"), X(Character) > 0.f);
		Sea.Tick(180);
		TestSettledSwimmer(*this, Character, Table, Label);
		TestEqual(Label + TEXT(": events"), Events.Text(), FString(TEXT("in")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimEnterFallFromHeight, "Project.Movement.QA.Swim.Enter.FallFromHeightSettlesWithOneEvent", QASwim::Flags)

bool FQASwimEnterFallFromHeight::RunTest(const FString& Parameters)
{
	// Review T3: the "entered the water once" check must hold AFTER the plunge settles. A 10 m fall into deep and into
	// shallow water, with the shipped settle time and the fastest and a slow one (data): after the first swimming frame
	// the character never leaves the water (no bobbing out), never hits the deep seabed, never penetrates the shallow one,
	// and ends floating at the row's depth with exactly one "in" event.
	struct FCase
	{
		const TCHAR* Label;
		float Seabed;
		float SettleTime; // <= 0: shipped
	};
	const FCase Cases[] = {
		{ TEXT("deep, shipped settle"), SeabedZ, 0.f },
		{ TEXT("deep, settle 0.05 s (the minimum)"), SeabedZ, 0.05f },
		{ TEXT("deep, settle 3 s"), SeabedZ, 3.f },
		{ TEXT("shallow (seabed -150), shipped settle"), -150.f, 0.f },
		{ TEXT("shallow (seabed -150), settle 3 s"), -150.f, 3.f },
	};
	for (const FCase& Case : Cases)
	{
		const FString Label = Case.Label;
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this, Case.Seabed))
		{
			return false;
		}
		if (Case.SettleTime > 0.f)
		{
			EditSwimRows(Table, [&Case](FLureMovementRow& Row) { Row.SurfaceFloatSettleTime = Case.SettleTime; });
		}
		const FLureMovementRow Swim = RowOf(Table, ELureMovementState::Swim);
		const float HalfHeight = RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight;
		ALurePlayerCharacter* Character = Sea.Spawn(*this, FVector(0.f, 0.f, 1000.f), Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		FSwimEvents Events(Character);
		TestTrue(Label + TEXT(": falls in"), Sea.TickUntil([&]() { return Movement->IsSwimming(); }, 300));

		const int32 Frames = FMath::CeilToInt((2.f * Swim.SurfaceFloatSettleTime + 2.f) / Dt);
		int32 OutOfWaterFrames = 0;
		int32 PenetratingFrames = 0;
		float Lowest = CenterZ(Character);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Sea.Tick(1);
			OutOfWaterFrames += Character->IsSwimming() ? 0 : 1;
			PenetratingFrames += IsPenetrating(Character) ? 1 : 0;
			Lowest = FMath::Min(Lowest, CenterZ(Character));
		}
		TestEqual(Label + TEXT(": frames out of the water after falling in (bobbing out)"), OutOfWaterFrames, 0);
		TestEqual(Label + TEXT(": frames penetrating the seabed"), PenetratingFrames, 0);
		TestTrue(FString::Printf(TEXT("%s: never reaches below the seabed (lowest center %.1f)"), *Label, Lowest), Lowest >= Case.Seabed + HalfHeight - 1.f);
		AddInfo(FString::Printf(TEXT("%s: the plunge's lowest capsule center %.1f cm"), *Label, Lowest));
		TestNearlyEqual(Label + TEXT(": floating at the row's depth after settling"), CenterZ(Character), -Swim.SurfaceFloatDepth, 1.f);
		TestTrue(FString::Printf(TEXT("%s: at rest (vz %.2f)"), *Label, Character->GetVelocity().Z), FMath::Abs(Character->GetVelocity().Z) < 2.f);
		TestEqual(Label + TEXT(": exactly one event after settling"), Events.Text(), FString(TEXT("in")));
	}
	return true;
}

// =====================================================================================================================
// Wading vs swimming (the 90 cm threshold of the 180 cm capsule)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimDepthThreshold, "Project.Movement.QA.Swim.Depth.WadingVsSwimmingThreshold", QASwim::Flags)

bool FQASwimDepthThreshold::RunTest(const FString& Parameters)
{
	// swimming.md: "with a 180 cm capsule you wade (walk) in water up to 90 cm deep and swim deeper than that" (the
	// capsule center, 90 cm + the floor gap above the feet, decides). Flat seabeds at 60 and 88 cm: walking, Stand row,
	// no swim event while walking about. 96 and 150 cm: swimming (Swim row), never walking.
	struct FCase
	{
		float Depth;
		bool bSwim;
	};
	for (const FCase& Case : { FCase{ 60.f, false }, FCase{ 88.f, false }, FCase{ 96.f, true }, FCase{ 150.f, true } })
	{
		const FString Label = FString::Printf(TEXT("water %.0f cm deep"), Case.Depth);
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this, -Case.Depth))
		{
			return false;
		}
		ALurePlayerCharacter* Character = Sea.SpawnOnFeet(*this, FVector(0.f, 0.f, -Case.Depth), Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Sea.Tick(90);
		FSwimEvents Events(Character);
		int32 WrongFrames = 0;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			Character->SetSprintRequested(Frame >= 90);
			Character->AddMovementInput(Frame < 90 ? FVector::ForwardVector : FVector::RightVector, 1.f, true);
			Sea.Tick(1);
			WrongFrames += (Case.bSwim ? Movement->IsSwimming() : (Movement->IsMovingOnGround() && !Character->IsSwimming())) ? 0 : 1;
		}
		Character->SetSprintRequested(false);
		Sea.Tick(60);
		TestEqual(FString::Printf(TEXT("%s: frames not %s"), *Label, Case.bSwim ? TEXT("swimming") : TEXT("wading (walking)")), WrongFrames, 0);
		TestEqual(Label + TEXT(": row at rest"), static_cast<int32>(Movement->GetMovementState()),
			static_cast<int32>(Case.bSwim ? ELureMovementState::Swim : ELureMovementState::Stand));
		TestEqual(Label + TEXT(": no swim events while moving about"), Events.Text(), FString());
		TestFalse(Label + TEXT(": no penetration"), IsPenetrating(Character));
		if (Case.bSwim && Case.Depth > 110.f)
		{
			TestNearlyEqual(Label + TEXT(": floats at the row's depth"), CenterZ(Character), -RowOf(Table, ELureMovementState::Swim).SurfaceFloatDepth, 1.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimWadingStance, "Project.Movement.QA.Swim.Depth.CrouchOrProneWhileWadingNoSwimFlicker", QASwim::Flags)

bool FQASwimWadingStance::RunTest(const FString& Parameters)
{
	// Wading is walking, not swimming. Crouching in 60 cm water or going prone in 30 cm water lowers the capsule center
	// below the surface. Whatever the rule (crouch allowed, or refused), the swim state must not flicker: no
	// OnSwimStateChanged pair (T-006 cancels fishing on it), and the character ends in a steady, non-penetrating state.
	// Prone in 10 cm water (center above the surface) is ordinary prone.
	struct FCase
	{
		float Depth;
		ELureStance Stance;
	};
	for (const FCase& Case : { FCase{ 60.f, ELureStance::Crouch }, FCase{ 30.f, ELureStance::Prone }, FCase{ 10.f, ELureStance::Prone } })
	{
		const FString Label = FString::Printf(TEXT("%s in %.0f cm water"), *StanceText(Case.Stance), Case.Depth);
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this, -Case.Depth))
		{
			return false;
		}
		ALurePlayerCharacter* Character = Sea.SpawnOnFeet(*this, FVector(0.f, 0.f, -Case.Depth), Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Sea.Tick(60);
		TestTrue(Label + TEXT(": setup: wading"), Movement->IsMovingOnGround() && !Character->IsSwimming());
		FSwimEvents Events(Character);
		Character->RequestStance(Case.Stance);
		TArray<FString> Trace;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Sea.Tick(1);
			const FString State = ModeText(Movement) + TEXT("/") + StanceText(Character->GetStance());
			if (Trace.Num() == 0 || !Trace.Last().EndsWith(State))
			{
				Trace.Add(FString::Printf(TEXT("f%d %s"), Frame, *State));
			}
		}
		AddInfo(Label + TEXT(": ") + FString::Join(Trace, TEXT(" -> ")));
		TestEqual(Label + TEXT(": no swim-state events (no in/out blip)"), Events.Text(), FString());
		const FString EndState = ModeText(Movement) + TEXT("/") + StanceText(Character->GetStance());
		Sea.Tick(30);
		TestEqual(Label + TEXT(": steady at the end"), ModeText(Movement) + TEXT("/") + StanceText(Character->GetStance()), EndState);
		TestFalse(Label + TEXT(": no penetration"), IsPenetrating(Character));
		if (Case.Depth <= 10.f)
		{
			TestEqual(Label + TEXT(": ordinary prone in a puddle"), StanceText(Character->GetStance()), FString(TEXT("Prone")));
		}
	}
	return true;
}

// =====================================================================================================================
// Two water volumes
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimTwoVolumes, "Project.Movement.QA.Swim.Volumes.OverlappingAndTouchingKeepSwimming", QASwim::Flags)

bool FQASwimTwoVolumes::RunTest(const FString& Parameters)
{
	// Level builders may cover the sea with several boxes (one per body of water). Swimming from one into the other,
	// through an overlap or across a seam where they touch, must not drop you out of the water: no events, no height
	// change, the surface is found all the way.
	struct FCase
	{
		const TCHAR* Label;
		float AMaxX;	// volume A spans [-2400, AMaxX]
		float BMinX;	// volume B spans [BMinX, 2400]
	};
	const FCase Cases[] = { { TEXT("overlapping by 400 cm"), 200.f, -200.f }, { TEXT("touching at x = 0"), 0.f, 0.f } };
	for (const FCase& Case : Cases)
	{
		const FString Label = Case.Label;
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.Create(*this))
		{
			return false;
		}
		Sea.AddBox(FVector(0.f, 0.f, SeabedZ - 50.f), FVector(5000.f, 5000.f, 50.f));
		const float AMinX = -2400.f;
		const float BMaxX = 2400.f;
		ALureWaterVolume* A = Sea.AddWater(FVector(0.5f * (AMinX + Case.AMaxX), 0.f, 0.f), FVector2D(0.5f * (Case.AMaxX - AMinX), 1000.0), 800.f);
		ALureWaterVolume* B = Sea.AddWater(FVector(0.5f * (Case.BMinX + BMaxX), 0.f, 0.f), FVector2D(0.5f * (BMaxX - Case.BMinX), 1000.0), 800.f);
		if (!TestNotNull(TEXT("volume A"), A) || !TestNotNull(TEXT("volume B"), B))
		{
			return false;
		}
		ALurePlayerCharacter* Character = Sea.SpawnSwimmer(*this, -1500.f, 0.f, Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		const float Depth = RowOf(Table, ELureMovementState::Swim).SurfaceFloatDepth;
		FSwimEvents Events(Character);
		int32 DryFrames = 0;
		int32 NoSurfaceFrames = 0;
		float WorstHeightError = 0.f;
		for (int32 Frame = 0; Frame < 1500 && X(Character) < 1500.f; ++Frame)
		{
			Character->SetSprintRequested(true);
			Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
			Sea.Tick(1);
			DryFrames += Movement->IsSwimming() ? 0 : 1;
			float SurfaceZ = 1.f;
			NoSurfaceFrames += (Movement->GetWaterSurfaceHeight(SurfaceZ) && FMath::IsNearlyZero(SurfaceZ, 0.01f)) ? 0 : 1;
			WorstHeightError = FMath::Max(WorstHeightError, FMath::Abs(CenterZ(Character) + Depth));
		}
		Character->SetSprintRequested(false);
		TestTrue(Label + TEXT(": crossed into volume B"), X(Character) >= 1500.f);
		TestEqual(Label + TEXT(": frames out of the water"), DryFrames, 0);
		TestEqual(Label + TEXT(": frames without the surface at z = 0"), NoSurfaceFrames, 0);
		TestTrue(FString::Printf(TEXT("%s: height steady across the volumes (worst %.2f cm off)"), *Label, WorstHeightError), WorstHeightError < 1.f);
		TestEqual(Label + TEXT(": no swim events"), Events.Text(), FString());
	}
	return true;
}

// =====================================================================================================================
// Ladders: the grab zone, the heights, the climb, the top and leaving the zone
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimLadderZone, "Project.Movement.QA.Swim.Ladder.GrabZoneBoundaries", QASwim::Flags)

bool FQASwimLadderZone::RunTest(const FString& Parameters)
{
	// Contract (LureLadder.h / swimming.md): the zone reaches 2 x GrabZoneHalfSize.X out over the water from the dock
	// face (default 80 cm), GrabZoneHalfSize.Y to each side (120 cm wide) and GrabZoneHalfSize.Z around the water line;
	// it follows the ladder's yaw; the climb direction is toward the dock (-forward).
	FSea Sea;
	if (!Sea.Create(*this))
	{
		return false;
	}
	for (const float Yaw : { 0.f, 90.f, 217.f })
	{
		ALureLadder* Ladder = Sea.AddLadder(FVector(500.f, 200.f, 30.f), Yaw, 300.f);
		if (!TestNotNull(TEXT("ladder spawns"), Ladder))
		{
			return false;
		}
		const FVector Half = Ladder->GrabZoneHalfSize;
		const FString Label = FString::Printf(TEXT("yaw %.0f"), Yaw);
		TestNearlyEqual(Label + TEXT(": default reach 80 cm out"), static_cast<float>(2.0 * Half.X), 80.f);
		TestNearlyEqual(Label + TEXT(": default width 120 cm"), static_cast<float>(2.0 * Half.Y), 120.f);
		const FTransform T = Ladder->GetActorTransform();
		auto In = [&T, Ladder](double LX, double LY, double LZ) { return Ladder->IsInGrabZone(T.TransformPosition(FVector(LX, LY, LZ))); };
		TestTrue(Label + TEXT(": in front, middle"), In(Half.X, 0.0, 0.0));
		TestTrue(Label + TEXT(": 1 cm short of the reach"), In(2.0 * Half.X - 1.0, 0.0, 0.0));
		TestFalse(Label + TEXT(": 1 cm past the reach"), In(2.0 * Half.X + 1.0, 0.0, 0.0));
		TestTrue(Label + TEXT(": touching the face"), In(0.0, 0.0, 0.0));
		TestFalse(Label + TEXT(": well behind the face (inside the dock)"), In(-20.0, 0.0, 0.0));
		TestTrue(Label + TEXT(": 1 cm inside each side"), In(Half.X, Half.Y - 1.0, 0.0) && In(Half.X, -Half.Y + 1.0, 0.0));
		TestFalse(Label + TEXT(": 1 cm outside a side"), In(Half.X, Half.Y + 1.0, 0.0));
		TestFalse(Label + TEXT(": 1 cm outside the other side"), In(Half.X, -Half.Y - 1.0, 0.0));
		TestTrue(Label + TEXT(": 1 cm inside top and bottom"), In(Half.X, 0.0, Half.Z - 1.0) && In(Half.X, 0.0, -Half.Z + 1.0));
		TestFalse(Label + TEXT(": above the zone"), In(Half.X, 0.0, Half.Z + 1.0));
		TestFalse(Label + TEXT(": below the zone"), In(Half.X, 0.0, -Half.Z - 1.0));
		const FVector Expected = -FVector(Ladder->GetActorForwardVector().X, Ladder->GetActorForwardVector().Y, 0.0).GetSafeNormal();
		TestTrue(Label + TEXT(": climbs toward the dock (-forward)"), Ladder->GetClimbDirection().Equals(Expected, 1e-4));
		TestTrue(Label + TEXT(": the climb direction is horizontal"), FMath::IsNearlyZero(Ladder->GetClimbDirection().Z, 1e-6));
		Ladder->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimLadderHeights, "Project.Movement.QA.Swim.Ladder.ClimbHeightBoundaries", QASwim::Flags)

bool FQASwimLadderHeights::RunTest(const FString& Parameters)
{
	// A ladder allows edges up to its MaxClimbHeight above the water (inclusive) and never less than the Swim row's own
	// rule; a blocked landing spot (no room to stand) is refused.
	struct FCase
	{
		const TCHAR* Label;
		float DockHeight;
		float LadderMax;
		bool bPillar;			// a tall pillar on the dock where you would stand
		bool bExpectOut;
		bool bExpectLadderFlag;
	};
	const FCase Cases[] = {
		{ TEXT("dock 300, ladder 300 (at the limit)"), 300.f, 300.f, false, true, true },
		{ TEXT("dock 301, ladder 300"), 301.f, 300.f, false, false, false },
		{ TEXT("dock 150, ladder 100"), 150.f, 100.f, false, false, false },
		{ TEXT("dock 55, ladder 40 (the row's 60 cm rule still applies)"), 55.f, 40.f, false, true, false },
		{ TEXT("dock 150, ladder 300, a pillar where you would stand"), 150.f, 300.f, true, false, false },
	};
	for (const FCase& Case : Cases)
	{
		const FString Label = Case.Label;
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this))
		{
			return false;
		}
		const float FaceX = 100.f;
		Sea.AddDock(FaceX, Case.DockHeight);
		if (Case.bPillar)
		{
			Sea.AddBox(FVector(FaceX + 70.f, 0.f, Case.DockHeight + 500.f), FVector(50.f, 200.f, 500.f));
		}
		ALureLadder* Ladder = Sea.AddLadder(FVector(FaceX, 0.f, 0.f), 180.f, Case.LadderMax);
		// In the zone, facing along the dock (the ladder decides the direction).
		ALurePlayerCharacter* Character = Sea.SpawnSwimmer(*this, FaceX - 34.f - 15.f, 0.f, Table, 90.f);
		if (!Ladder || !Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		TestTrue(Label + TEXT(": setup: in the grab zone"), Ladder->IsInGrabZone(Character->GetActorLocation()));
		FLureClimbPlan Plan;
		const bool bPlan = Movement->FindClimbOutPlan(Plan);
		TestEqual(Label + TEXT(": plan"), bPlan, Case.bExpectOut);
		if (bPlan)
		{
			TestEqual(Label + TEXT(": plan uses the ladder"), Plan.bUsesLadder, Case.bExpectLadderFlag);
			TestNearlyEqual(Label + TEXT(": plan edge height"), Plan.LedgeHeight, Case.DockHeight, 0.5f);
		}
		FSwimEvents Events(Character);
		Character->Jump();
		const bool bOut = Sea.TickUntil([&]() { return Movement->IsMovingOnGround(); }, 300);
		Character->StopJumping();
		Sea.Tick(20);
		TestEqual(Label + TEXT(": standing on the dock after Jump"), bOut, Case.bExpectOut);
		if (Case.bExpectOut)
		{
			TestNearlyEqual(Label + TEXT(": feet on the dock top"), FeetZ(Character), Case.DockHeight, 3.f);
			TestEqual(Label + TEXT(": events"), Events.Text(), FString(TEXT("out")));
		}
		else
		{
			TestTrue(Label + TEXT(": still swimming"), Movement->IsSwimming());
			TestTrue(Label + TEXT(": still in front of the dock"), X(Character) < FaceX);
			TestEqual(Label + TEXT(": no events"), Events.Text(), FString());
		}
		TestFalse(Label + TEXT(": no penetration"), IsPenetrating(Character));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimLadderTrip, "Project.Movement.QA.Swim.Ladder.EnterClimbTopAndBackIn", QASwim::Flags)

bool FQASwimLadderTrip::RunTest(const FString& Parameters)
{
	// A swimmer outside the zone can't climb (Jump does nothing). Swimming into the zone (facing away from the dock)
	// makes a 200 cm dock climbable. During the whole climb IsSwimming stays true and no event fires; on top: walking,
	// Stand row, one "out". Walking back off the top drops you in again ("in"), floating normally; swimming away out of
	// the zone brings back the row's own climb limit.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.CreateSea(*this))
	{
		return false;
	}
	const float FaceX = 100.f;
	const float DockHeight = 200.f;
	const FLureMovementRow Swim = RowOf(Table, ELureMovementState::Swim);
	Sea.AddDock(FaceX, DockHeight);
	ALureLadder* Ladder = Sea.AddLadder(FVector(FaceX, 0.f, 0.f), 180.f, 300.f);
	ALurePlayerCharacter* Character = Sea.SpawnSwimmer(*this, FaceX - 250.f, 0.f, Table, 180.f);
	if (!Ladder || !Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	FSwimEvents Events(Character);

	// 1. Outside the zone.
	TestFalse(TEXT("outside: not in the zone"), Ladder->IsInGrabZone(Character->GetActorLocation()));
	TestNearlyEqual(TEXT("outside: the row's limit"), Movement->GetClimbOutMaxHeight(), Swim.ClimbMaxHeight);
	Character->Jump();
	Sea.Tick(30);
	Character->StopJumping();
	TestTrue(TEXT("outside: Jump does nothing"), Movement->IsSwimming() && !Movement->IsClimbingOut());

	// 2. Into the zone (swimming backwards toward the dock, still facing away from it).
	TestTrue(TEXT("swims into the zone"), Sea.TickUntil([&]() { return Ladder->IsInGrabZone(Character->GetActorLocation()) && X(Character) > FaceX - 60.f; }, 600, Character, FVector::ForwardVector));
	Sea.Tick(30);
	TestNearlyEqual(TEXT("in the zone: the ladder's limit"), Movement->GetClimbOutMaxHeight(), Ladder->MaxClimbHeight);

	// 3. The climb.
	Character->Jump();
	int32 Frames = 0;
	int32 DryFramesBeforeLand = 0;
	bool bClimbSeen = false;
	for (; Frames < 300 && !Movement->IsMovingOnGround(); ++Frames)
	{
		Sea.Tick(1);
		bClimbSeen |= Movement->IsClimbingOut();
		DryFramesBeforeLand += (Character->IsSwimming() || Movement->IsMovingOnGround()) ? 0 : 1;
		if (Frames == 10)
		{
			TestEqual(TEXT("mid-climb: no event yet"), Events.Text(), FString());
			Character->RequestStance(ELureStance::Crouch);
			TestEqual(TEXT("mid-climb: a crouch request is refused"), StanceText(Character->GetRequestedStance()), FString(TEXT("Stand")));
		}
	}
	Character->StopJumping();
	Sea.Tick(20);
	TestTrue(TEXT("the climb ran"), bClimbSeen);
	TestEqual(TEXT("frames neither in the water nor on land during the climb"), DryFramesBeforeLand, 0);
	TestTrue(TEXT("on the dock"), Movement->IsMovingOnGround() && X(Character) > FaceX);
	TestNearlyEqual(TEXT("feet on the dock top"), FeetZ(Character), DockHeight, 3.f);
	TestEqual(TEXT("row Stand on the dock"), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(ELureMovementState::Stand));
	TestEqual(TEXT("events after the climb"), Events.Text(), FString(TEXT("out")));
	const float Path = (DockHeight + RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight + 2.f + Swim.SurfaceFloatDepth) + 60.f;
	AddInfo(FString::Printf(TEXT("ladder climb took %d frames (%.2f s) for about %.0f cm at %.0f cm/s"), Frames, Frames * Dt, Path, Swim.ClimbSpeed));
	TestTrue(TEXT("the climb takes about path / ClimbSpeed (not instant, not stuck)"), Frames * Dt > 0.5f * Path / Swim.ClimbSpeed && Frames * Dt < 2.f * Path / Swim.ClimbSpeed);

	// 4. Back off the top into the water.
	TestTrue(TEXT("walks off the top into the water"), Sea.TickUntil([&]() { return Movement->IsSwimming(); }, 300, Character, FVector::BackwardVector));
	Sea.TickMoving(Character, 60, FVector::BackwardVector);
	Sea.Tick(180);
	TestSettledSwimmer(*this, Character, Table, TEXT("back in"));
	TestEqual(TEXT("events: out, in"), Events.Text(), FString(TEXT("out,in")));
	TestFalse(TEXT("away from the ladder: out of the zone"), Ladder->IsInGrabZone(Character->GetActorLocation()));
	TestNearlyEqual(TEXT("away from the ladder: the row's limit again"), Movement->GetClimbOutMaxHeight(), Swim.ClimbMaxHeight);
	FLureClimbPlan Plan;
	TestFalse(TEXT("away from the ladder: no climb"), Movement->FindClimbOutPlan(Plan));
	return true;
}

// =====================================================================================================================
// Leaving the water: the stances and sprint work again on land
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimLeaveStances, "Project.Movement.QA.Swim.Leave.StancesAndSprintWorkAgainOnLand", QASwim::Flags)

bool FQASwimLeaveStances::RunTest(const FString& Parameters)
{
	// Crouch/prone are refused in the water (and during the climb out), but the wishes must not stay blocked on land:
	// after a climb out with sprint held, sprinting on the dock uses the Sprint row, and crouch then prone work.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.CreateSea(*this))
	{
		return false;
	}
	const float FaceX = 100.f;
	Sea.AddDock(FaceX, 50.f);
	ALurePlayerCharacter* Character = Sea.SpawnSwimmer(*this, FaceX - 54.f, 0.f, Table);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	Character->SetSprintRequested(true);
	Character->RequestStance(ELureStance::Prone);
	TestEqual(TEXT("prone refused in the water"), StanceText(Character->GetRequestedStance()), FString(TEXT("Stand")));
	Character->Jump();
	TestTrue(TEXT("climbs out"), Sea.TickUntil([&]() { return Movement->IsMovingOnGround(); }, 240));
	Character->StopJumping();
	Sea.TickMoving(Character, 25, FVector::ForwardVector); // the dock is 5 m long
	TestTrue(FString::Printf(TEXT("on the dock (feet %.1f, x %.1f)"), FeetZ(Character), X(Character)), Movement->IsMovingOnGround() && FMath::IsNearlyEqual(FeetZ(Character), 50.f, 3.f));
	TestEqual(TEXT("sprint held: Sprint row on land"), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(ELureMovementState::Sprint));
	Character->SetSprintRequested(false);
	Sea.Tick(30);
	Character->RequestStance(ELureStance::Crouch);
	Sea.Tick(30);
	TestEqual(TEXT("crouch works on land"), StanceText(Character->GetStance()), FString(TEXT("Crouch")));
	Character->RequestStance(ELureStance::Prone);
	Sea.Tick(45);
	TestEqual(TEXT("prone works on land"), StanceText(Character->GetStance()), FString(TEXT("Prone")));
	TestFalse(TEXT("no penetration"), IsPenetrating(Character));
	return true;
}

// =====================================================================================================================
// Teleport / respawn while swimming (Lure.Teleport uses APawn::TeleportTo)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimTeleport, "Project.Movement.QA.Swim.Teleport.BetweenWaterAndLand", QASwim::Flags)

bool FQASwimTeleport::RunTest(const FString& Parameters)
{
	// Swimmer -> dry land: walking, Stand row, "out". Land -> open water: swimming, "in". Water -> other water: nothing.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.CreateSea(*this))
	{
		return false;
	}
	const float LandTop = 30.f;
	Sea.AddBox(FVector(3000.f, 0.f, 0.5f * (LandTop + SeabedZ)), FVector(800.f, 800.f, 0.5f * (LandTop - SeabedZ)));
	const float HalfHeight = RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight;
	ALurePlayerCharacter* Character = Sea.SpawnSwimmer(*this, -1000.f, 0.f, Table);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	FSwimEvents Events(Character);

	TestTrue(TEXT("teleport to land accepted"), Character->TeleportTo(FVector(3000.f, 0.f, LandTop + HalfHeight + 5.f), FRotator::ZeroRotator));
	Sea.Tick(60);
	TestTrue(TEXT("on land: walking"), Movement->IsMovingOnGround());
	TestFalse(TEXT("on land: not swimming"), Character->IsSwimming());
	TestEqual(TEXT("on land: row Stand"), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(ELureMovementState::Stand));
	TestNearlyEqual(TEXT("on land: stand eye height"), Character->GetCurrentEyeHeight(), RowOf(Table, ELureMovementState::Stand).EyeHeight, 0.5f);
	TestEqual(TEXT("events: out"), Events.Text(), FString(TEXT("out")));

	TestTrue(TEXT("teleport into open water accepted"), Character->TeleportTo(FVector(-1500.f, 500.f, -10.f), FRotator::ZeroRotator));
	Sea.Tick(120);
	TestSettledSwimmer(*this, Character, Table, TEXT("teleported into water"));
	TestEqual(TEXT("events: out, in"), Events.Text(), FString(TEXT("out,in")));

	TestTrue(TEXT("teleport water -> water accepted"), Character->TeleportTo(FVector(-500.f, -1500.f, -10.f), FRotator::ZeroRotator));
	Sea.Tick(60);
	TestTrue(TEXT("still swimming"), Movement->IsSwimming());
	TestEqual(TEXT("events unchanged"), Events.Text(), FString(TEXT("out,in")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimTeleportMidClimb, "Project.Movement.QA.Swim.Teleport.MidClimbEndsTheClimb", QASwim::Flags)

bool FQASwimTeleportMidClimb::RunTest(const FString& Parameters)
{
	// A teleport (respawn, Lure.Teleport) while climbing out must not keep the old climb: the character stays where it
	// was sent (not dragged back toward the dock), stands on the ground there and is out of the water ("in,out").
	// The land pull-up (LedgeClimb) version is Project.Movement.QA.Climb.TeleportMidPullUpEndsIt.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.CreateSea(*this))
	{
		return false;
	}
	const float FaceX = 100.f;
	const float LandTop = 30.f;
	const FVector LandXY(3000.f, 1500.f, 0.f);
	Sea.AddDock(FaceX, 60.f);
	Sea.AddBox(FVector(LandXY.X, LandXY.Y, 0.5f * (LandTop + SeabedZ)), FVector(800.f, 800.f, 0.5f * (LandTop - SeabedZ)));
	const float HalfHeight = RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight;
	ALurePlayerCharacter* Character = Sea.Spawn(*this, FVector(FaceX - 54.f, 0.f, 190.f), Table);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	FSwimEvents Events(Character);
	Sea.Tick(180);
	Character->Jump();
	TestTrue(TEXT("the climb starts"), Sea.TickUntil([&]() { return Movement->IsClimbingOut(); }, 30));
	Sea.Tick(8);
	Character->StopJumping();
	TestTrue(TEXT("setup: mid-climb"), Movement->IsClimbingOut());

	const FVector Destination(LandXY.X, LandXY.Y, LandTop + HalfHeight + 5.f);
	TestTrue(TEXT("teleport accepted"), Character->TeleportTo(Destination, FRotator::ZeroRotator));
	Sea.Tick(90);
	const float Drift = static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), Destination));
	TestTrue(FString::Printf(TEXT("stays where it was sent (moved %.1f cm, now at %s)"), Drift, *Character->GetActorLocation().ToCompactString()), Drift < 20.f);
	TestFalse(TEXT("the climb ended"), Movement->IsClimbingOut());
	TestTrue(FString::Printf(TEXT("standing on the ground there (mode %s)"), *ModeText(Movement)), Movement->IsMovingOnGround());
	TestFalse(TEXT("out of the water"), Character->IsSwimming());
	TestEqual(TEXT("events: in, out"), Events.Text(), FString(TEXT("in,out")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimTeleportMidLedgeClimb, "Project.Movement.QA.Climb.TeleportMidPullUpEndsIt", QASwim::Flags)

bool FQASwimTeleportMidLedgeClimb::RunTest(const FString& Parameters)
{
	// The land version: a teleport in the middle of a jump pull-up onto a 1 m crate (LedgeClimb) must end the pull-up.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.Create(*this))
	{
		return false;
	}
	Sea.AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
	const float FaceX = 100.f;
	Sea.AddBox(FVector(FaceX + 100.f, 0.f, 50.f), FVector(100.f, 300.f, 50.f)); // a 100 cm crate
	const FLureMovementRow Stand = RowOf(Table, ELureMovementState::Stand);
	ALurePlayerCharacter* Character = Sea.SpawnOnFeet(*this, FVector(FaceX - Stand.CapsuleRadius - 12.f, 0.f, 0.f), Table);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	Sea.Tick(20);
	Sea.TickMoving(Character, 3);
	Character->Jump();
	bool bLedge = false;
	for (int32 Frame = 0; Frame < 120 && !bLedge; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
		Sea.Tick(1);
		bLedge = Movement->IsLedgeClimbing();
	}
	Character->StopJumping();
	if (!TestTrue(TEXT("setup: a ledge pull-up started"), bLedge))
	{
		return false;
	}
	Sea.Tick(2);
	const FVector Destination(-1500.f, 800.f, Stand.CapsuleHalfHeight + 5.f);
	TestTrue(TEXT("teleport accepted"), Character->TeleportTo(Destination, FRotator::ZeroRotator));
	Sea.Tick(90);
	const float Drift = static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), Destination));
	TestTrue(FString::Printf(TEXT("stays where it was sent (moved %.1f cm, now at %s)"), Drift, *Character->GetActorLocation().ToCompactString()), Drift < 20.f);
	TestFalse(TEXT("the pull-up ended"), Movement->IsLedgeClimbing());
	TestTrue(FString::Printf(TEXT("on the ground there (mode %s)"), *ModeText(Movement)), Movement->IsMovingOnGround());
	return true;
}

// =====================================================================================================================
// OnSwimStateChanged: once per real change over a whole trip
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimEventsTrip, "Project.Movement.QA.Swim.Events.OncePerRealChangeOverATrip", QASwim::Flags)

bool FQASwimEventsTrip::RunTest(const FString& Parameters)
{
	// Fall in (in); in the water: sprint on/off, Jump in open water, crouch/prone requests, pressing into a dock too high
	// to climb (nothing); climb out at a 50 cm jetty (out, only once standing); walk back off it (in); swim to a beach
	// and walk out (out). Exactly "in,out,in,out", each at the moment it really happens.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.CreateSea(*this))
	{
		return false;
	}
	const float FaceX = 100.f;
	Sea.AddDock(FaceX, 50.f);
	// A high wall to the side (y > 700) to press into: 150 cm, not climbable.
	Sea.AddBox(FVector(-600.f, 950.f, 0.5f * (150.f + SeabedZ)), FVector(400.f, 250.f, 0.5f * (150.f - SeabedZ)));
	// A 12 degree beach rising toward -X; its surface crosses the water line at x = -1800.
	const float Pitch = -12.f;
	const float Sin = FMath::Sin(FMath::DegreesToRadians(Pitch));
	const float Cos = FMath::Cos(FMath::DegreesToRadians(Pitch));
	Sea.AddBox(FVector(-1800.f + 50.f * Sin, 0.f, -50.f * Cos), FVector(1500.f, 600.f, 50.f), FRotator(Pitch, 0.f, 0.f));

	ALurePlayerCharacter* Character = Sea.Spawn(*this, FVector(-600.f, 0.f, 300.f), Table);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	FSwimEvents Events(Character);
	Sea.Tick(240);
	TestEqual(TEXT("1. fell in"), Events.Text(), FString(TEXT("in")));

	// 2. Things that are not a change.
	Character->SetSprintRequested(true);
	Sea.TickMoving(Character, 60, FVector::RightVector);
	Character->SetSprintRequested(false);
	Sea.TickMoving(Character, 30, FVector::LeftVector);
	Character->Jump();
	Sea.Tick(20);
	Character->StopJumping();
	Character->RequestStance(ELureStance::Crouch);
	Character->RequestStance(ELureStance::Prone);
	Character->ToggleCrouch();
	Sea.Tick(20);
	Character->SetActorRotation(FRotator(0.f, 90.f, 0.f));
	Sea.TickMoving(Character, 240, FVector::RightVector); // into the 150 cm wall
	Character->Jump();
	Sea.Tick(30);
	Character->StopJumping();
	TestTrue(TEXT("2. the high wall is not climbable"), Movement->IsSwimming());
	TestEqual(TEXT("2. nothing else fired"), Events.Text(), FString(TEXT("in")));

	// 3. Climb out at the jetty.
	Character->SetActorRotation(FRotator::ZeroRotator);
	Sea.TickMoving(Character, 120, FVector::LeftVector); // back off the wall
	TestTrue(TEXT("3. swims to the jetty"), Sea.TickUntil([&]() { return X(Character) > FaceX - 60.f; }, 600, Character, FVector::ForwardVector));
	Sea.TickMoving(Character, 20, FVector::ForwardVector);
	Sea.Tick(20);
	Character->Jump();
	bool bOutTooEarly = false;
	for (int32 Frame = 0; Frame < 240 && !Movement->IsMovingOnGround(); ++Frame)
	{
		Sea.Tick(1);
		bOutTooEarly |= Events.Num() > 1 && !Movement->IsMovingOnGround();
	}
	Character->StopJumping();
	Sea.TickMoving(Character, 30, FVector::ForwardVector);
	TestFalse(TEXT("3. 'out' only once standing on the jetty"), bOutTooEarly);
	TestTrue(TEXT("3. on the jetty"), Movement->IsMovingOnGround() && FMath::IsNearlyEqual(FeetZ(Character), 50.f, 3.f));
	TestEqual(TEXT("3. climbed out"), Events.Text(), FString(TEXT("in,out")));

	// 4. Walk back off it.
	TestTrue(TEXT("4. walks back into the water"), Sea.TickUntil([&]() { return Movement->IsSwimming(); }, 300, Character, FVector::BackwardVector));
	Sea.Tick(120);
	TestEqual(TEXT("4. back in"), Events.Text(), FString(TEXT("in,out,in")));

	// 5. Swim to the beach and walk out.
	TestTrue(TEXT("5. walks out at the beach"), Sea.TickUntil([&]() { return Movement->IsMovingOnGround() && !Character->IsSwimming(); }, 1500, Character, FVector::BackwardVector));
	Sea.TickMoving(Character, 120, FVector::BackwardVector);
	TestTrue(TEXT("5. feet above the water"), FeetZ(Character) > 0.f);
	TestEqual(TEXT("5. the whole trip"), Events.Text(), FString(TEXT("in,out,in,out")));
	return true;
}

// =====================================================================================================================
// Review T2: SurfaceFloatDepth = 0 is the engine's free swimming (where diving will start)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimFreeSwimRow, "Project.Movement.QA.Swim.DiveRow.SurfaceFloatDepthZeroIsFreeSwimming", QASwim::Flags)

bool FQASwimFreeSwimRow::RunTest(const FString& Parameters)
{
	// swimming.md: "A row with SurfaceFloatDepth = 0 gets the engine's free 3D swimming". The row must be accepted (not
	// replaced by a fallback row that floats), the surface rule must be off, and nothing may pull the body back to the
	// surface: after a 1 m drop it sinks with its entry speed and stays down. Horizontal swimming still uses the row.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.CreateSea(*this))
	{
		return false;
	}
	EditSwimRows(Table, [](FLureMovementRow& Row) { Row.SurfaceFloatDepth = 0.f; });
	uint8 Mask = 0xFF;
	const FLureMovementRow Swim = RowOf(Table, ELureMovementState::Swim, &Mask);
	TestEqual(TEXT("no row falls back (0 is a legal float depth)"), static_cast<int32>(Mask), 0);
	TestEqual(TEXT("the resolved Swim row keeps depth 0"), Swim.SurfaceFloatDepth, 0.f);

	ALurePlayerCharacter* Character = Sea.Spawn(*this, FVector(0.f, 0.f, 190.f), Table);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	FSwimEvents Events(Character);
	TestTrue(TEXT("falls in"), Sea.TickUntil([&]() { return Movement->IsSwimming(); }, 120));
	TestEqual(TEXT("engine swimming mode"), static_cast<int32>(Movement->MovementMode), static_cast<int32>(MOVE_Swimming));
	TestFalse(TEXT("no surface rule for this row"), Movement->ShouldFloatAtSurface());
	Sea.Tick(120);
	const float After2s = CenterZ(Character);
	TestTrue(FString::Printf(TEXT("sank well below the float depth the surface rule would hold (center %.1f)"), After2s), After2s < -50.f);
	float Highest = After2s;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		Sea.Tick(1);
		Highest = FMath::Max(Highest, CenterZ(Character));
	}
	TestTrue(FString::Printf(TEXT("not pulled back up (highest %.1f)"), Highest), Highest < After2s + 5.f);
	TestTrue(TEXT("still swimming (under water)"), Movement->IsSwimming());
	TestFalse(TEXT("no penetration"), IsPenetrating(Character));

	Sea.TickMoving(Character, 180, FVector::ForwardVector);
	TestNearlyEqual(TEXT("swims at the row's speed"), static_cast<float>(Character->GetVelocity().Size2D()), Swim.MaxSpeed, 0.05f * Swim.MaxSpeed);
	Character->RequestStance(ELureStance::Prone);
	TestEqual(TEXT("no prone under water either"), StanceText(Character->GetRequestedStance()), FString(TEXT("Stand")));
	TestEqual(TEXT("one event"), Events.Text(), FString(TEXT("in")));
	return true;
}

// =====================================================================================================================
// Regression (a60e4a5): no dead band between stepping onto a submerged shelf and climbing out onto it
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimShelfNoDeadBand, "Project.Movement.QA.Swim.Leave.NoShelfDeadBand", QASwim::Flags)

bool FQASwimShelfNoDeadBand::RunTest(const FString& Parameters)
{
	// Before a60e4a5 a vertical shelf with its top in (-55, -20) cm was too high to step onto and too low to climb: an
	// invisible wall. Every shelf top from -91 cm (below the floating feet) up to the climb range must now be either
	// stepped onto (swim into it) or climbed (Jump), and never leave the swimmer stuck or launched out of the water.
	UDataTable* Shipped = ShippedTable(*this);
	FGCObjectScopeGuard KeepShipped(Shipped);
	if (!Shipped)
	{
		return false;
	}
	const FLureMovementRow Swim = RowOf(Shipped, ELureMovementState::Swim);
	const FLureMovementRow Stand = RowOf(Shipped, ELureMovementState::Stand);
	const float StepHeight = GetDefault<ULureCharacterMovementComponent>()->MaxStepHeight;
	const float FloatingFeet = -(Swim.SurfaceFloatDepth + Stand.CapsuleHalfHeight);
	AddInfo(FString::Printf(TEXT("floating feet %.0f, MaxStepHeight %.0f, ClimbOutLowestTop %.0f"), FloatingFeet, StepHeight, Swim.ClimbOutLowestTop));

	const float Tops[] = { -91.f, -85.f, -75.f, -65.f, -58.f, -55.f, -52.f, -48.f, -44.f, -40.f, -35.f, -30.f, -25.f, -20.f, -15.f };
	for (const float Top : Tops)
	{
		const FString Label = FString::Printf(TEXT("shelf top %.0f cm"), Top);
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this))
		{
			return false;
		}
		const float FaceX = 100.f;
		Sea.AddDock(FaceX, Top);
		ALurePlayerCharacter* Character = Sea.SpawnSwimmer(*this, FaceX - 200.f, 0.f, Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		float PeakFeet = -TNumericLimits<float>::Max();
		float PeakVz = 0.f;
		auto Track = [&]()
		{
			PeakFeet = FMath::Max(PeakFeet, FeetZ(Character));
			PeakVz = FMath::Max(PeakVz, static_cast<float>(Character->GetVelocity().Z));
		};
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
			Sea.Tick(1);
			Track();
			if (Movement->IsMovingOnGround() && !Character->IsSwimming() && X(Character) > FaceX + 30.f)
			{
				break; // stepped onto the shelf: stop before walking off its far side
			}
		}
		const bool bWalkedOut = Movement->IsMovingOnGround() && !Character->IsSwimming();
		const float WalkPeakFeet = PeakFeet;
		const float WalkPeakVz = PeakVz;
		bool bJumpedOut = false;
		if (!bWalkedOut)
		{
			Sea.Tick(20);
			Character->Jump();
			for (int32 Frame = 0; Frame < 240 && !bJumpedOut; ++Frame)
			{
				Sea.Tick(1);
				Track();
				bJumpedOut = Movement->IsMovingOnGround();
			}
			Character->StopJumping();
			Sea.Tick(20);
		}
		const FString Got = bWalkedOut ? TEXT("walk") : (bJumpedOut ? TEXT("jump") : TEXT("stuck"));
		AddInfo(FString::Printf(TEXT("%s: %s; end %s feet %.1f x %.1f, peak feet %.1f, peak vz %.0f"), *Label, *Got, *ModeText(Movement), FeetZ(Character), X(Character), PeakFeet, PeakVz));
		TestTrue(Label + TEXT(": not stuck (steps onto it or climbs it)"), Got != TEXT("stuck"));
		if (Got == TEXT("stuck"))
		{
			continue;
		}
		TestNearlyEqual(Label + TEXT(": feet on the shelf"), FeetZ(Character), Top, 3.f);
		TestTrue(Label + TEXT(": past its edge"), X(Character) > FaceX);
		if (bWalkedOut)
		{
			// Same no-launch bounds as Leave.StepOntoSubmergedShelfNoLaunch.
			TestTrue(FString::Printf(TEXT("%s: not launched on the step (feet peak %.1f)"), *Label, WalkPeakFeet), WalkPeakFeet < Top + 20.f);
			TestTrue(FString::Printf(TEXT("%s: no upward burst on the step (vz %.0f)"), *Label, WalkPeakVz), WalkPeakVz <= 300.f);
		}
		else
		{
			// A climb rises at most ClimbSpeed and ends on the top, never thrown clear above it.
			TestTrue(FString::Printf(TEXT("%s: climb not launched (feet peak %.1f)"), *Label, PeakFeet), PeakFeet < Top + 60.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimShelfStepUp, "Project.Movement.QA.Swim.Leave.StepOntoSubmergedShelfNoLaunch", QASwim::Flags)

bool FQASwimShelfStepUp::RunTest(const FString& Parameters)
{
	// Swimming into a vertical submerged shelf whose top is within MaxStepHeight of the floating feet (L_PalmKey's reef
	// flat is at -60 cm, the lagoon floor at -90): the swimmer steps onto it and wades out. It must never be thrown up
	// out of the water (a step of a few cm in one frame is not a jump): feet never more than 20 cm above the shelf top,
	// no upward speed above 300 cm/s, and it ends walking on the shelf, still over it.
	for (const float Top : { -58.f, -60.f, -65.f, -70.f, -75.f, -80.f, -85.f, -90.f })
	{
		const FString Label = FString::Printf(TEXT("shelf top %.0f cm"), Top);
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.CreateSea(*this))
		{
			return false;
		}
		const float FaceX = 100.f;
		Sea.AddDock(FaceX, Top); // 5 m of shelf, then deep water again
		ALurePlayerCharacter* Character = Sea.SpawnSwimmer(*this, FaceX - 200.f, 0.f, Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		float HighestFeet = -1000.f;
		float FastestUp = 0.f;
		bool bOnShelf = false;
		for (int32 Frame = 0; Frame < 300 && !bOnShelf; ++Frame)
		{
			Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
			Sea.Tick(1);
			HighestFeet = FMath::Max(HighestFeet, FeetZ(Character));
			FastestUp = FMath::Max(FastestUp, static_cast<float>(Character->GetVelocity().Z));
			bOnShelf = Movement->IsMovingOnGround() && X(Character) > FaceX + 40.f;
		}
		Sea.Tick(30);
		TestTrue(FString::Printf(TEXT("%s: never thrown up (highest feet %.1f)"), *Label, HighestFeet), HighestFeet <= Top + 20.f);
		TestTrue(FString::Printf(TEXT("%s: no launch (fastest upward speed %.0f cm/s)"), *Label, FastestUp), FastestUp <= 300.f);
		TestTrue(FString::Printf(TEXT("%s: walking on the shelf (mode %s, feet %.1f, x %.1f)"), *Label, *ModeText(Movement), FeetZ(Character), X(Character)),
			Movement->IsMovingOnGround() && FMath::Abs(FeetZ(Character) - Top) <= 3.f && X(Character) > FaceX && X(Character) < FaceX + 500.f);
		TestFalse(Label + TEXT(": not swimming there"), Character->IsSwimming());
	}
	return true;
}

// =====================================================================================================================
// Review T4: with a swim-stroke clip, the stroke really plays in the arms' DefaultSlot
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQASwimStrokeClip, "Project.Movement.QA.Swim.Arms.StrokeClipPlaysInDefaultSlot", QASwim::Flags)

bool FQASwimStrokeClip::RunTest(const FString& Parameters)
{
	// The real A_FPArms_SwimStroke doesn't exist yet, so an existing full-pose arms clip (A_FPArms_Idle) stands in for it.
	// In the water: the placeholder lowering is off (alpha 0), a montage plays, and ABP_FPArms actually blends the
	// DefaultSlot in (weight > 0.9; 0 if the graph has no DefaultSlot node). Out of the water the stroke stops.
	const FSoftObjectPath StandIn(TEXT("/Game/Art/Characters/FPArms/A_FPArms_Idle.A_FPArms_Idle"));
	if (!TestTrue(TEXT("stand-in clip A_FPArms_Idle exists"), FPackageName::DoesPackageExist(StandIn.GetLongPackageName())))
	{
		return false;
	}
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.CreateSea(*this))
	{
		return false;
	}
	const float LandTop = 30.f;
	Sea.AddBox(FVector(3000.f, 0.f, 0.5f * (LandTop + SeabedZ)), FVector(800.f, 800.f, 0.5f * (LandTop - SeabedZ)));
	ALurePlayerCharacter* Player = Sea.Spawn(*this, FVector(0.f, 0.f, 190.f), Table, 0.f, [&StandIn](ALurePlayerCharacter& Character)
	{
		Character.SwimStrokeAnimation = TSoftObjectPtr<UAnimSequenceBase>(StandIn);
	});
	APlayerController* Controller = Sea.World->SpawnActor<APlayerController>();
	if (!Player || !TestNotNull(TEXT("controller spawns"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	Sea.Tick(240);
	TestTrue(TEXT("swimming"), Player->IsSwimming());
	UAnimInstance* Anim = Player->GetFirstPersonArms() ? Player->GetFirstPersonArms()->GetAnimInstance() : nullptr;
	if (TestNotNull(TEXT("the arms have ABP_FPArms (SK_FPArms + ABP imported)"), Anim))
	{
		const FName Slot(TEXT("DefaultSlot"));
		TestEqual(TEXT("placeholder lowering off while the stroke plays"), Player->GetSwimArmsAlpha(), 0.f);
		TestTrue(TEXT("a stroke montage is playing"), Anim->IsAnyMontagePlaying());
		TestTrue(FString::Printf(TEXT("the ABP blends DefaultSlot in (weight %.2f)"), Anim->GetSlotMontageGlobalWeight(Slot)), Anim->GetSlotMontageGlobalWeight(Slot) > 0.9f);

		TestTrue(TEXT("teleport onto land"), Player->TeleportTo(FVector(3000.f, 0.f, LandTop + 100.f), FRotator::ZeroRotator));
		Sea.Tick(60);
		TestFalse(TEXT("on land"), Player->IsSwimming());
		TestTrue(FString::Printf(TEXT("the stroke stopped on land (DefaultSlot weight %.2f)"), Anim->GetSlotMontageGlobalWeight(Slot)), Anim->GetSlotMontageGlobalWeight(Slot) < 0.1f);
	}
	Controller->UnPossess();
	Sea.Tick(2);
	return true;
}

// =====================================================================================================================
// Review T0: the climb rule refuses edge landings (no perching below a ledge top)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAClimbEdgeLandingRefused, "Project.Movement.QA.Climb.EdgeLandingRefusedWhereTheEngineAccepts", QASwim::Flags)

bool FQAClimbEdgeLandingRefused::RunTest(const FString& Parameters)
{
	// movement-rules.md B1: "A landing that lifts the feet onto a ledge's edge is refused (no hanging perched below a
	// ledge top)". Sweep the Stand capsule straight down onto a 100 cm block's top edge at many offsets (takeoff at the
	// floor, so the 100 cm height rule allows it). Wherever the engine's own IsValidLandingSpot would accept a contact that
	// lifts the feet, ours must refuse it; and there must be such offsets, or this test proves nothing. Landing fully on
	// the top is accepted.
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.Create(*this))
	{
		return false;
	}
	const FLureMovementRow Stand = RowOf(Table, ELureMovementState::Stand);
	TestNearlyEqual(TEXT("shipped rule 100 cm"), Stand.ClimbMaxHeight, 100.f);
	const float FaceX = 100.f;
	const float Top = 100.f;
	Sea.AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
	Sea.AddBox(FVector(FaceX + 300.f, 0.f, 0.5f * Top), FVector(300.f, 300.f, 0.5f * Top));
	ALurePlayerCharacter* Character = Sea.SpawnOnFeet(*this, FVector(-1500.f, 0.f, 0.f), Table);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	Sea.Tick(5);
	Movement->SetMovementMode(MOVE_Falling);
	FLureMovementTestAccess::SetTakeoffFeetHeight(*Movement, 0.f);
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(QAClimbEdgeLanding), false, Character);

	int32 EngineAcceptsLift = 0;
	int32 WronglyAccepted = 0;
	int32 FlatRefused = 0;
	TArray<FString> Notes;
	for (float Offset = -Radius + 0.25f; Offset <= Radius + 6.f; Offset += 0.5f)
	{
		const FVector Start(FaceX + Offset, 0.f, Top + HalfHeight + 60.f);
		FHitResult Hit;
		if (!Sea.World->SweepSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 200.f), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), Params) || Hit.bStartPenetrating)
		{
			continue;
		}
		const float Feet = static_cast<float>(Hit.Location.Z) - HalfHeight;
		const float Lift = static_cast<float>(Hit.ImpactPoint.Z) - Feet;
		const bool bOurs = Movement->IsValidLandingSpot(Hit.Location, Hit);
		const bool bEngine = Movement->UCharacterMovementComponent::IsValidLandingSpot(Hit.Location, Hit);
		if (Lift > 3.f)
		{
			if (bEngine)
			{
				++EngineAcceptsLift;
				if (bOurs)
				{
					++WronglyAccepted;
					Notes.Add(FString::Printf(TEXT("offset %.2f lift %.1f"), Offset, Lift));
				}
			}
		}
		else if (Offset >= Radius && !bOurs)
		{
			++FlatRefused;
		}
	}
	AddInfo(FString::Printf(TEXT("edge contacts the engine alone would accept (feet lifted > 3 cm): %d"), EngineAcceptsLift));
	TestTrue(TEXT("the engine accepts some edge contacts (else the rule is untested)"), EngineAcceptsLift > 0);
	TestEqual(TEXT("edge contacts accepted by the climb rule (") + FString::Join(Notes, TEXT("; ")) + TEXT(")"), WronglyAccepted, 0);
	TestEqual(TEXT("flat landings on the top refused"), FlatRefused, 0);
	return true;
}

	/** True if the character stands (walking) with its feet between the floor and a ledge top: perched on the edge. */
	bool IsPerched(const ALurePlayerCharacter* Character, float LedgeTop)
	{
		const float Feet = FeetZ(Character);
		return Character->GetLureMovement()->IsMovingOnGround() && Feet > 5.f && Feet < LedgeTop - 3.f;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAClimbNeverPerched, "Project.Movement.QA.Climb.NeverPerchedBelowALedgeTop", QASwim::Flags)

bool FQAClimbNeverPerched::RunTest(const FString& Parameters)
{
	// Behavior of the same rule, frame by frame, at a 100 cm block (a 1 m crate): the character is never walking with its
	// feet below the top and above the floor. (a) Falling against the face without a jump, pushing in: ends on the floor
	// in front. (b) A standing jump that lets go of the stick at the apex: ends on the floor. (c) Stand / crouch / sprint
	// jumps holding forward: the apex (90 / 74 / 99 cm) is below the top, so they must end on top THROUGH the pull-up.
	struct FCase
	{
		const TCHAR* Label;
		int32 Kind;			// 0 = falling without a jump, 1 = jump released at the apex, 2 = jump holding forward
		ELureStance Stance;
		bool bSprint;
		bool bExpectOnTop;
		bool bRequirePullUp;	// the apex is clearly below the top (sprint's 99 cm is within the 2.5 cm landing tolerance)
	};
	const FCase Cases[] = {
		{ TEXT("falling without a jump, pushing in"), 0, ELureStance::Stand, false, false, false },
		{ TEXT("stand jump, let go at the apex"), 1, ELureStance::Stand, false, false, false },
		{ TEXT("stand jump, holding forward"), 2, ELureStance::Stand, false, true, true },
		{ TEXT("crouch jump, holding forward"), 2, ELureStance::Crouch, false, true, true },
		{ TEXT("sprint jump, holding forward"), 2, ELureStance::Stand, true, true, false },
	};
	for (const FCase& Case : Cases)
	{
		const FString Label = Case.Label;
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FSea Sea;
		if (!Table || !Sea.Create(*this))
		{
			return false;
		}
		const float FaceX = 100.f;
		const float Top = 100.f;
		Sea.AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
		Sea.AddBox(FVector(FaceX + 300.f, 0.f, 0.5f * Top), FVector(300.f, 300.f, 0.5f * Top));
		const FLureMovementRow Stand = RowOf(Table, ELureMovementState::Stand);
		const float StartX = Case.bSprint ? FaceX - 500.f : FaceX - Stand.CapsuleRadius - 12.f;
		ALurePlayerCharacter* Character = Sea.SpawnOnFeet(*this, FVector(StartX, 0.f, 0.f), Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Sea.Tick(10);
		if (Case.Stance == ELureStance::Crouch)
		{
			Character->RequestStance(ELureStance::Crouch);
			Sea.Tick(20);
		}

		if (Case.Kind == 0)
		{
			Character->SetActorLocation(FVector(FaceX - Stand.CapsuleRadius - 1.f, 0.f, Top - 20.f + Stand.CapsuleHalfHeight));
			Movement->SetMovementMode(MOVE_Falling);
			Movement->Velocity = FVector(0.f, 0.f, -20.f);
			Character->JumpCurrentCount = 0;
		}
		else
		{
			if (Case.bSprint)
			{
				Character->SetSprintRequested(true);
				for (int32 Frame = 0; Frame < 200 && X(Character) < FaceX - Stand.CapsuleRadius - 45.f; ++Frame)
				{
					Sea.TickMoving(Character, 1);
				}
			}
			else
			{
				Sea.TickMoving(Character, 3);
			}
			Character->Jump();
		}

		bool bPerched = false;
		float PerchedFeet = 0.f;
		bool bLedgeClimb = false;
		bool bReleased = false;
		int32 GroundFrames = 0;
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			// Like a player: let go at the apex (case b), or a moment after landing anywhere (don't walk off the far side).
			bReleased |= Case.Kind == 1 && Frame > 3 && Character->GetVelocity().Z <= 0.f;
			GroundFrames = (Movement->IsMovingOnGround() && Frame > 5) ? GroundFrames + 1 : 0;
			bReleased |= GroundFrames >= 10;
			if (!bReleased)
			{
				Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
			}
			Sea.Tick(1);
			bLedgeClimb |= Movement->IsLedgeClimbing();
			if (IsPerched(Character, Top))
			{
				bPerched = true;
				PerchedFeet = FeetZ(Character);
			}
		}
		Character->StopJumping();
		Character->SetSprintRequested(false);
		Sea.Tick(20);
		TestFalse(FString::Printf(TEXT("%s: never perched on the edge (feet %.1f)"), *Label, PerchedFeet), bPerched);
		const bool bOnTop = Movement->IsMovingOnGround() && FMath::Abs(FeetZ(Character) - Top) <= 3.f && X(Character) > FaceX;
		TestEqual(FString::Printf(TEXT("%s: on top (feet %.1f, x %.1f)"), *Label, FeetZ(Character), X(Character)), bOnTop, Case.bExpectOnTop);
		if (Case.bRequirePullUp)
		{
			TestTrue(Label + TEXT(": got there through the pull-up (the apex is below the top)"), bLedgeClimb);
		}
		else if (Case.bExpectOnTop)
		{
			AddInfo(Label + (bLedgeClimb ? TEXT(": pulled up") : TEXT(": landed on top by itself")));
		}
		else
		{
			TestFalse(Label + TEXT(": no pull-up"), bLedgeClimb);
			TestTrue(FString::Printf(TEXT("%s: back on the floor in front (feet %.1f, x %.1f)"), *Label, FeetZ(Character), X(Character)),
				Movement->IsMovingOnGround() && FeetZ(Character) < 5.f && X(Character) < FaceX);
		}
		TestFalse(Label + TEXT(": no penetration"), IsPenetrating(Character));
	}
	return true;
}

// =====================================================================================================================
// Review T5: ArmsPullBack moves the arms toward the eye (-X) for every row, Stand included
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQACameraArmsPullBack, "Project.Movement.QA.Camera.ArmsPullBackFollowsData", QASwim::Flags)

bool FQACameraArmsPullBack::RunTest(const FString& Parameters)
{
	// A feel edit such as Stand ArmsPullBack = 4 (a pure data change) must pull the standing arms 4 cm back (-X), like
	// Prone's 12 (fixture: Stand 4, Prone 9, both unlike the shipped values).
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FSea Sea;
	if (!Table || !Sea.Create(*this))
	{
		return false;
	}
	EditRow(Table, ELureMovementState::Stand)->ArmsPullBack = 4.f;
	EditRow(Table, ELureMovementState::Prone)->ArmsPullBack = 9.f;
	uint8 Mask = 0xFF;
	RowOf(Table, ELureMovementState::Stand, &Mask);
	TestEqual(TEXT("fixture rows valid"), static_cast<int32>(Mask), 0);
	Sea.AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
	ALurePlayerCharacter* Player = Sea.SpawnOnFeet(*this, FVector::ZeroVector, Table);
	APlayerController* Controller = Sea.World->SpawnActor<APlayerController>();
	if (!Player || !TestNotNull(TEXT("controller spawns"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	Sea.Tick(60);
	TestNearlyEqual(TEXT("standing: pulled back by Stand.ArmsPullBack"), static_cast<float>(Player->GetArmsBobOffset().GetLocation().X), -4.f, 0.2f);
	Player->RequestStance(ELureStance::Prone);
	Sea.Tick(90);
	TestNearlyEqual(TEXT("prone: pulled back by Prone.ArmsPullBack"), static_cast<float>(Player->GetArmsBobOffset().GetLocation().X), -9.f, 0.2f);
	Player->RequestStance(ELureStance::Stand);
	Sea.Tick(90);
	TestNearlyEqual(TEXT("standing again"), static_cast<float>(Player->GetArmsBobOffset().GetLocation().X), -4.f, 0.2f);
	Controller->UnPossess();
	Sea.Tick(2);
	return true;
}

} // namespace QASwim

#endif // WITH_DEV_AUTOMATION_TESTS

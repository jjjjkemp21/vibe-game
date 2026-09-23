// Lure T-026: surface swimming tests (implementer's tests, Project.Movement.Swim.*). Spec: docs/specs/swimming.md.
// Tables come from data/tables/DT_Movement.csv (fixtures = that table with rows edited in memory, so new columns never
// break them). Worlds are transient game worlds with an ALureWaterVolume whose surface is at z = 0.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureLadder.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Character/LureWaterVolume.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Tests/Movement/LureSwimTestListener.h"
#include "Tests/Movement/LureMovementTestAccess.h"

namespace LureSwimTest
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Seabed of the test pool (deep water), cm. */
	constexpr float SeabedZ = -600.f;

	FString CsvPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv"));
	}

	/** data/tables/DT_Movement.csv as a transient table; missing file or import problems fail the test. */
	UDataTable* ShippedTable(FAutomationTestBase& Test)
	{
		FString Csv;
		if (!Test.TestTrue(TEXT("data/tables/DT_Movement.csv loads"), FFileHelper::LoadFileToString(Csv, *CsvPath())))
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
		return Table ? Table->FindRow<FLureMovementRow>(FLureMovementData::GetRowName(State), TEXT("LureSwimTest"), false) : nullptr;
	}

	TArray<FLureMovementRow> Resolve(const UDataTable* Table)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Table, Rows, Problems);
		return Rows;
	}

	const FLureMovementRow& RowOf(const TArray<FLureMovementRow>& Rows, ELureMovementState State)
	{
		return Rows[static_cast<int32>(State)];
	}

	/** The swim design rules (docs/specs/swimming.md). Returns the broken ones. */
	TArray<FString> CheckSwimRules(const TArray<FLureMovementRow>& Rows)
	{
		TArray<FString> Broken;
		auto Check = [&Broken](bool bOk, const FString& What) { if (!bOk) { Broken.Add(What); } };
		const FLureMovementRow& Stand = RowOf(Rows, ELureMovementState::Stand);
		const FLureMovementRow& Sprint = RowOf(Rows, ELureMovementState::Sprint);
		const FLureMovementRow& Swim = RowOf(Rows, ELureMovementState::Swim);
		const FLureMovementRow& SwimSprint = RowOf(Rows, ELureMovementState::SwimSprint);

		for (const ELureMovementState State : { ELureMovementState::Swim, ELureMovementState::SwimSprint })
		{
			const FLureMovementRow& R = RowOf(Rows, State);
			const FString Name = FLureMovementData::GetRowName(State).ToString();
			FString Problem;
			Check(R.Validate(Problem), Name + TEXT(": ") + Problem);
			Check(FMath::IsNearlyEqual(R.CapsuleHalfHeight, Stand.CapsuleHalfHeight) && FMath::IsNearlyEqual(R.CapsuleRadius, Stand.CapsuleRadius),
				Name + TEXT(": the swimming capsule is the Stand capsule"));
			Check(R.SurfaceFloatDepth > 0.f, Name + TEXT(": SurfaceFloatDepth > 0 (the slice swims at the surface)"));
			Check(R.SurfaceFloatDepth < R.CapsuleHalfHeight, Name + TEXT(": SurfaceFloatDepth < CapsuleHalfHeight"));
			const float EyeAboveWater = R.EyeHeight - R.CapsuleHalfHeight - R.SurfaceFloatDepth;
			Check(EyeAboveWater >= 5.f && EyeAboveWater <= 40.f, FString::Printf(TEXT("%s: eyes %.1f cm above the water, want [5, 40]"), *Name, EyeAboveWater));
			Check(R.ClimbMaxHeight >= 30.f && R.ClimbMaxHeight <= 100.f, Name + TEXT(": ClimbMaxHeight in [30, 100] cm"));
			Check(R.ClimbSpeed >= 50.f && R.ClimbSpeed <= 1000.f, Name + TEXT(": ClimbSpeed in [50, 1000] cm/s"));
			Check(!R.CanJump, Name + TEXT(": CanJump is false (Jump climbs out instead)"));
			Check(R.NoiseMultiplier >= Stand.NoiseMultiplier, Name + TEXT(": swimming is at least as loud as walking (splashy)"));
		}
		Check(Swim.MaxSpeed < Stand.MaxSpeed, TEXT("swimming is slower than walking"));
		Check(SwimSprint.MaxSpeed > Swim.MaxSpeed && SwimSprint.MaxSpeed < Sprint.MaxSpeed, TEXT("Swim < SwimSprint < Sprint speeds"));
		Check(SwimSprint.NoiseMultiplier > Swim.NoiseMultiplier, TEXT("sprint-swimming is louder than swimming"));
		Check(FMath::IsNearlyEqual(Swim.ClimbMaxHeight, SwimSprint.ClimbMaxHeight), TEXT("same climb-out height while sprint-swimming"));

		for (const ELureMovementState State : { ELureMovementState::Stand, ELureMovementState::Sprint, ELureMovementState::Crouch, ELureMovementState::Prone })
		{
			const FLureMovementRow& R = RowOf(Rows, State);
			Check(R.SurfaceFloatDepth == 0.f, FLureMovementData::GetRowName(State).ToString() + TEXT(": land rows don't float (SurfaceFloatDepth 0)"));
		}
		return Broken;
	}

	/** Transient game world: seabed, water (surface z = 0), docks, ramps, ladders. */
	struct FPool
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ALureWaterVolume* Water = nullptr;

		bool Create(FAutomationTestBase& Test, bool bSeabed = true)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			if (bSeabed)
			{
				AddBox(FVector(0.f, 0.f, SeabedZ - 50.f), FVector(4000.f, 4000.f, 50.f));
			}
			Water = AddWater(FVector::ZeroVector, FVector2D(3500.0, 3500.0), 800.f);
			Tick(1);
			return Test.TestNotNull(TEXT("water volume spawns"), Water);
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

		/** A dock whose water-side face is at x = FaceX (it extends toward +X) and whose top is Height above the water. */
		AActor* AddDock(float FaceX, float Height)
		{
			const float Bottom = SeabedZ;
			return AddBox(FVector(FaceX + 250.f, 0.f, 0.5f * (Height + Bottom)), FVector(250.f, 500.f, 0.5f * (Height - Bottom)));
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

		ALureLadder* AddLadder(const FVector& Location, float Yaw, float MaxClimbHeight)
		{
			const FTransform Transform(FRotator(0.f, Yaw, 0.f), Location);
			ALureLadder* Ladder = World->SpawnActorDeferred<ALureLadder>(ALureLadder::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Ladder)
			{
				return nullptr;
			}
			Ladder->MaxClimbHeight = MaxClimbHeight;
			Ladder->FinishSpawning(Transform);
			return Ladder;
		}

		/** Spawns the character with its capsule center at Center (table applied before BeginPlay). */
		ALurePlayerCharacter* Spawn(const FVector& Center, const UDataTable* Table, float Yaw = 0.f)
		{
			const FTransform Transform(FRotator(0.f, Yaw, 0.f), Center);
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

		/** Spawns the character 1 m above the water at XY and lets it fall in and settle. */
		ALurePlayerCharacter* SpawnSwimming(FAutomationTestBase& Test, const FVector2D& XY, const UDataTable* Table, float Yaw = 0.f, int32 SettleFrames = 180)
		{
			ALurePlayerCharacter* Character = Spawn(FVector(XY.X, XY.Y, 190.f), Table, Yaw);
			if (!Test.TestNotNull(TEXT("character spawns"), Character))
			{
				return nullptr;
			}
			Tick(SettleFrames);
			Test.TestTrue(TEXT("fell in: swimming"), Character->GetLureMovement()->IsSwimming());
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
				Character->AddMovementInput(Direction, 1.f, /*bForce*/ true);
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

	float CenterZ(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetCapsuleComponent()->GetComponentLocation().Z);
	}

	float FeetZ(const ALurePlayerCharacter* Character)
	{
		return CenterZ(Character) - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	float CameraZ(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetFirstPersonCamera()->GetComponentLocation().Z);
	}

	float HorizontalSpeed(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetVelocity().Size2D());
	}

	/** OnSwimStateChanged calls as text ("in,out"), for readable test messages. */
	FString EventsText(const TArray<bool>& Events)
	{
		TArray<FString> Parts;
		for (const bool bIn : Events)
		{
			Parts.Add(bIn ? TEXT("in") : TEXT("out"));
		}
		return FString::Join(Parts, TEXT(","));
	}
}

// The tests live inside the helper namespace (not behind a file-scope "using namespace"): unity builds merge test files,
// and a using-directive would leak into the next file and make same-named helpers there ambiguous.
namespace LureSwimTest
{

// ---------------------------------------------------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimDataRulesTest, "Project.Movement.Swim.Data.SwimRowsFollowTheRules", LureSwimTest::Flags)

bool FLureSwimDataRulesTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	if (!Table)
	{
		return false;
	}
	for (const ELureMovementState State : { ELureMovementState::Swim, ELureMovementState::SwimSprint })
	{
		TestNotNull(FString::Printf(TEXT("row %s exists"), *FLureMovementData::GetRowName(State).ToString()), EditRow(Table, State));
	}
	TArray<FLureMovementRow> Rows;
	TArray<FString> Problems;
	const uint8 Mask = FLureMovementData::ResolveRows(Table, Rows, Problems);
	TestEqual(TEXT("no row falls back"), static_cast<int32>(Mask), 0);

	for (const FString& Broken : CheckSwimRules(Rows))
	{
		AddError(TEXT("DT_Movement.csv: ") + Broken);
	}
	for (const FString& Broken : CheckSwimRules(Resolve(nullptr)))
	{
		AddError(TEXT("built-in fallback rows: ") + Broken);
	}

	// Literal values (a column that silently fails to import is caught), and the level-design numbers.
	const FLureMovementRow& Swim = RowOf(Rows, ELureMovementState::Swim);
	TestNearlyEqual(TEXT("Swim.MaxSpeed"), Swim.MaxSpeed, 170.f);
	TestNearlyEqual(TEXT("SwimSprint.MaxSpeed"), RowOf(Rows, ELureMovementState::SwimSprint).MaxSpeed, 290.f);
	TestNearlyEqual(TEXT("Swim.SurfaceFloatDepth"), Swim.SurfaceFloatDepth, 10.f);
	TestNearlyEqual(TEXT("Swim.ClimbMaxHeight (the 60 cm edge rule)"), Swim.ClimbMaxHeight, 60.f);
	TestNearlyEqual(TEXT("Swim.ClimbSpeed"), Swim.ClimbSpeed, 300.f);
	AddInfo(FString::Printf(TEXT("Swimming: eyes %.0f cm above the water; edges up to %.0f cm climbable; wading until the water is %.0f cm deep."),
		Swim.EyeHeight - Swim.CapsuleHalfHeight - Swim.SurfaceFloatDepth, Swim.ClimbMaxHeight, Swim.CapsuleHalfHeight));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimFloatMathTest, "Project.Movement.Swim.FloatSpringMath", LureSwimTest::Flags)

bool FLureSwimFloatMathTest::RunTest(const FString& Parameters)
{
	const float SettleTime = 0.8f;
	for (const float Step : { 1.f / 120.f, 1.f / 60.f, 1.f / 30.f, 0.1f })
	{
		// From rest, 50 cm below the target: rises without overshooting, within ~3% after SettleTime.
		float Z = -50.f;
		float V = 0.f;
		bool bOvershoot = false;
		for (float Time = 0.f; Time < SettleTime - 0.5f * Step; Time += Step)
		{
			V = ULureCharacterMovementComponent::ComputeSurfaceFloatVelocity(Z, V, 0.f, SettleTime, Step);
			Z += V * Step;
			bOvershoot |= Z > 0.01f;
		}
		TestFalse(FString::Printf(TEXT("dt %.3f: no overshoot"), Step), bOvershoot);
		// The implicit step is close to exact at 60+ fps (~2%); long frames only settle a little slower (never unstable).
		const float Allowed = (Step <= 1.f / 60.f + UE_KINDA_SMALL_NUMBER) ? 0.03f : 0.08f;
		TestTrue(FString::Printf(TEXT("dt %.3f: settled within %.0f%% after the settle time (left %.2f cm)"), Step, 100.f * Allowed, -Z), FMath::Abs(Z) <= Allowed * 50.f + 0.01f);

		// A plunge at -800 cm/s: bounded dip, then back up and steady.
		Z = 0.f;
		V = -800.f;
		float Lowest = 0.f;
		for (float Time = 0.f; Time < 4.f; Time += Step)
		{
			V = ULureCharacterMovementComponent::ComputeSurfaceFloatVelocity(Z, V, 0.f, SettleTime, Step);
			Z += V * Step;
			Lowest = FMath::Min(Lowest, Z);
		}
		TestTrue(FString::Printf(TEXT("dt %.3f: plunge dips less than 1 m (%.1f cm)"), Step, -Lowest), Lowest > -100.f);
		TestTrue(FString::Printf(TEXT("dt %.3f: back at the float height (%.3f cm)"), Step, Z), FMath::Abs(Z) < 0.5f && FMath::Abs(V) < 1.f);
	}
	TestEqual(TEXT("zero frame time keeps the speed"), ULureCharacterMovementComponent::ComputeSurfaceFloatVelocity(0.f, 12.f, 10.f, SettleTime, 0.f), 12.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimWaterVolumeTest, "Project.Movement.Swim.WaterVolumeShape", LureSwimTest::Flags)

bool FLureSwimWaterVolumeTest::RunTest(const FString& Parameters)
{
	FPool Pool;
	if (!Pool.Create(*this, /*bSeabed*/ false))
	{
		return false;
	}
	ALureWaterVolume* Volume = Pool.AddWater(FVector(1000.f, 2000.f, 50.f), FVector2D(300.0, 400.0), 500.f);
	if (!TestNotNull(TEXT("volume spawns"), Volume))
	{
		return false;
	}
	Pool.Tick(1);
	TestTrue(TEXT("it is water"), Volume->bWaterVolume);
	TestNearlyEqual(TEXT("surface = the actor's height"), Volume->GetSurfaceHeight(), 50.f);
	TestTrue(TEXT("just under the surface is water"), Volume->IsPointInWater(FVector(1000.f, 2000.f, 49.f)));
	TestFalse(TEXT("just above the surface is not"), Volume->IsPointInWater(FVector(1000.f, 2000.f, 51.f)));
	TestTrue(TEXT("near the bottom is water"), Volume->IsPointInWater(FVector(1290.f, 2390.f, -449.f)));
	TestFalse(TEXT("below the bottom is not"), Volume->IsPointInWater(FVector(1000.f, 2000.f, -451.f)));
	TestFalse(TEXT("outside in X is not"), Volume->IsPointInWater(FVector(1301.f, 2000.f, 0.f)));

	// The engine's volume test uses the collision body (what the character's physics-volume check uses).
	TestTrue(TEXT("collision: inside"), Volume->EncompassesPoint(FVector(1000.f, 2000.f, 40.f)));
	TestFalse(TEXT("collision: above the surface"), Volume->EncompassesPoint(FVector(1000.f, 2000.f, 60.f)));
	TestFalse(TEXT("collision: below the bottom"), Volume->EncompassesPoint(FVector(1000.f, 2000.f, -460.f)));
	const FBoxSphereBounds Bounds = Volume->GetBrushComponent()->Bounds;
	TestNearlyEqual(TEXT("bounds top = surface"), static_cast<float>(Bounds.Origin.Z + Bounds.BoxExtent.Z), 50.f, 0.1f);
	TestNearlyEqual(TEXT("bounds bottom = surface - depth"), static_cast<float>(Bounds.Origin.Z - Bounds.BoxExtent.Z), -450.f, 0.1f);

	// Overlap only: it never blocks a trace (camera, bobber, AI sight).
	FHitResult Hit;
	TestFalse(TEXT("does not block visibility traces"), Pool.World->LineTraceSingleByChannel(Hit, FVector(1000.f, 2000.f, 200.f), FVector(1000.f, 2000.f, -200.f), ECC_Visibility));
	TestEqual(TEXT("profile"), Volume->GetBrushComponent()->GetCollisionProfileName(), FName(TEXT("OverlapAllDynamic")));

	// Resizing from a script rebuilds the collision.
	Volume->SetWaterSize(FVector2D(100.0, 100.0), 50.f);
	Pool.Tick(1);
	TestFalse(TEXT("resized: the old corner is dry"), Volume->EncompassesPoint(FVector(1290.f, 2390.f, 0.f)));
	TestTrue(TEXT("resized: the middle is wet"), Volume->EncompassesPoint(FVector(1000.f, 2000.f, 20.f)));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Entering, floating, speeds
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimEnterTest, "Project.Movement.Swim.EnteringWaterSwitchesToSwimming", LureSwimTest::Flags)

bool FLureSwimEnterTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FPool Pool;
	if (!Table || !Pool.Create(*this))
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = Resolve(Table);
	const FLureMovementRow& Swim = RowOf(Rows, ELureMovementState::Swim);
	const FLureMovementRow& Stand = RowOf(Rows, ELureMovementState::Stand);

	ALurePlayerCharacter* Character = Pool.Spawn(FVector(0.f, 0.f, 190.f), Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureSwimTestListener* Listener = NewObject<ULureSwimTestListener>();
	FGCObjectScopeGuard KeepListener(Listener);
	Character->OnSwimStateChanged.AddDynamic(Listener, &ULureSwimTestListener::OnSwimStateChanged);
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	Pool.Tick(2);
	TestFalse(TEXT("in the air: not swimming yet"), Character->IsSwimming());

	TestTrue(TEXT("falls in and swims"), Pool.TickUntil([&]() { return Movement->IsSwimming(); }, 120));
	TestEqual(TEXT("engine swimming mode"), static_cast<int32>(Movement->MovementMode), static_cast<int32>(MOVE_Swimming));
	TestTrue(TEXT("IsSwimming (character)"), Character->IsSwimming());
	TestEqual(TEXT("event: entered the water once"), EventsText(Listener->Events), FString(TEXT("in")));
	TestEqual(TEXT("row in use: Swim"), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(ELureMovementState::Swim));
	TestTrue(TEXT("the surface rule applies"), Movement->ShouldFloatAtSurface());
	float SurfaceZ = -1.f;
	TestTrue(TEXT("water surface found"), Movement->GetWaterSurfaceHeight(SurfaceZ));
	TestNearlyEqual(TEXT("water surface at z = 0"), SurfaceZ, 0.f, 0.01f);

	Pool.Tick(180);
	TestTrue(TEXT("still swimming after settling"), Movement->IsSwimming());
	TestEqual(TEXT("stance Stand in the water"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Stand));
	float Radius = 0.f;
	float HalfHeight = 0.f;
	Character->GetCapsuleComponent()->GetUnscaledCapsuleSize(Radius, HalfHeight);
	TestNearlyEqual(TEXT("Stand capsule half height"), HalfHeight, Stand.CapsuleHalfHeight, 0.05f);
	TestNearlyEqual(TEXT("noise multiplier from the Swim row"), Movement->GetStanceNoiseMultiplier(), Swim.NoiseMultiplier);
	TestNearlyEqual(TEXT("eye height from the Swim row"), Character->GetCurrentEyeHeight(), Swim.EyeHeight, 0.5f);
	TestNearlyEqual(TEXT("camera = feet + swim eye height"), CameraZ(Character) - FeetZ(Character), Swim.EyeHeight, 0.5f);
	TestTrue(TEXT("camera above the water"), CameraZ(Character) > SurfaceZ);
	TestTrue(TEXT("AI eye point = camera"), FMath::IsNearlyEqual(static_cast<float>(Character->GetPawnViewLocation().Z), CameraZ(Character), 1.f));

	// No jumping out of open water (no edge to climb): still swimming, no launch.
	Character->Jump();
	Pool.Tick(30);
	TestTrue(TEXT("Jump in open water: still swimming"), Movement->IsSwimming());
	TestTrue(TEXT("Jump in open water: no launch"), CenterZ(Character) < SurfaceZ);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimFloatTest, "Project.Movement.Swim.FloatHoldsAtDataDepth", LureSwimTest::Flags)

bool FLureSwimFloatTest::RunTest(const FString& Parameters)
{
	// The float depth and the eye height come from the row: two different tables give two different heights.
	for (const float Depth : { 10.f, 30.f })
	{
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
		FPool Pool;
		if (!Table || !Pool.Create(*this))
		{
			return false;
		}
		for (const ELureMovementState State : { ELureMovementState::Swim, ELureMovementState::SwimSprint })
		{
			FLureMovementRow* Row = EditRow(Table, State);
			Row->SurfaceFloatDepth = Depth;
			Row->EyeHeight = 90.f + Depth + 22.f; // eyes 22 cm above the water
		}
		ALurePlayerCharacter* Character = Pool.SpawnSwimming(*this, FVector2D::ZeroVector, Table, 0.f, 240);
		if (!Character)
		{
			return false;
		}
		const FString Label = FString::Printf(TEXT("depth %.0f"), Depth);
		TestNearlyEqual(Label + TEXT(": capsule center at surface - SurfaceFloatDepth"), CenterZ(Character), -Depth, 0.5f);
		TestNearlyEqual(Label + TEXT(": not bobbing"), static_cast<float>(Character->GetVelocity().Z), 0.f, 1.f);
		TestNearlyEqual(Label + TEXT(": eyes 22 cm above the water"), CameraZ(Character), 22.f, 1.f);

		// Swimming forward (and sprint-swimming) keeps the height.
		float Lowest = CenterZ(Character);
		float Highest = Lowest;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			Character->SetSprintRequested(Frame >= 90);
			Character->AddMovementInput(FVector::ForwardVector, 1.f, true);
			Pool.Tick(1);
			Lowest = FMath::Min(Lowest, CenterZ(Character));
			Highest = FMath::Max(Highest, CenterZ(Character));
		}
		TestTrue(FString::Printf(TEXT("%s: height steady while swimming (%.2f..%.2f)"), *Label, Lowest, Highest), Highest - Lowest < 1.f && FMath::Abs(Highest + Depth) < 1.f);
		TestTrue(Label + TEXT(": still swimming"), Character->GetLureMovement()->IsSwimming());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimSpeedTest, "Project.Movement.Swim.SpeedsFromData", LureSwimTest::Flags)

bool FLureSwimSpeedTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Label;
		float Swim;
		float SwimSprint;
		float SwimNoise;
		float SprintNoise;
	};
	// The shipped values, then a fixture unlike them (proves the numbers are read from the table).
	const FCase Cases[] = { { TEXT("shipped"), 0.f, 0.f, 0.f, 0.f }, { TEXT("fixture"), 137.f, 263.f, 1.3f, 2.7f } };
	for (const FCase& Case : Cases)
	{
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
		FPool Pool;
		if (!Table || !Pool.Create(*this))
		{
			return false;
		}
		if (Case.Swim > 0.f)
		{
			EditRow(Table, ELureMovementState::Swim)->MaxSpeed = Case.Swim;
			EditRow(Table, ELureMovementState::Swim)->NoiseMultiplier = Case.SwimNoise;
			EditRow(Table, ELureMovementState::SwimSprint)->MaxSpeed = Case.SwimSprint;
			EditRow(Table, ELureMovementState::SwimSprint)->NoiseMultiplier = Case.SprintNoise;
		}
		const TArray<FLureMovementRow> Rows = Resolve(Table);
		const FLureMovementRow& Swim = RowOf(Rows, ELureMovementState::Swim);
		const FLureMovementRow& SwimSprint = RowOf(Rows, ELureMovementState::SwimSprint);

		ALurePlayerCharacter* Character = Pool.SpawnSwimming(*this, FVector2D(-2000.0, 0.0), Table);
		if (!Character)
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();

		Pool.TickMoving(Character, 180);
		TestNearlyEqual(FString::Printf(TEXT("%s: swim max speed"), Case.Label), Movement->GetMaxSpeed(), Swim.MaxSpeed);
		TestNearlyEqual(FString::Printf(TEXT("%s: swim speed reached (1%%)"), Case.Label), HorizontalSpeed(Character), Swim.MaxSpeed, 0.01f * Swim.MaxSpeed);
		TestEqual(FString::Printf(TEXT("%s: row Swim"), Case.Label), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(ELureMovementState::Swim));
		TestNearlyEqual(FString::Printf(TEXT("%s: swim noise"), Case.Label), Movement->GetStanceNoiseMultiplier(), Swim.NoiseMultiplier);

		Character->SetSprintRequested(true);
		Pool.TickMoving(Character, 180);
		TestTrue(FString::Printf(TEXT("%s: sprint maps to sprint-swimming"), Case.Label), Character->IsSprinting());
		TestEqual(FString::Printf(TEXT("%s: row SwimSprint"), Case.Label), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(ELureMovementState::SwimSprint));
		TestNearlyEqual(FString::Printf(TEXT("%s: sprint-swim speed reached (1%%)"), Case.Label), HorizontalSpeed(Character), SwimSprint.MaxSpeed, 0.01f * SwimSprint.MaxSpeed);
		TestNearlyEqual(FString::Printf(TEXT("%s: sprint-swim noise"), Case.Label), Movement->GetStanceNoiseMultiplier(), SwimSprint.NoiseMultiplier);
		TestNearlyEqual(FString::Printf(TEXT("%s: same eye height while sprint-swimming"), Case.Label), Character->GetCurrentEyeHeight(), SwimSprint.EyeHeight, 0.5f);

		// Letting go: you coast to a stop.
		Character->SetSprintRequested(false);
		Pool.Tick(240);
		TestTrue(FString::Printf(TEXT("%s: stops without input (%.1f cm/s)"), Case.Label, HorizontalSpeed(Character)), HorizontalSpeed(Character) < 5.f);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stances in water
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNoStancesTest, "Project.Movement.Swim.NoCrouchOrProneInWater", LureSwimTest::Flags)

bool FLureSwimNoStancesTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FPool Pool;
	if (!Table || !Pool.Create(*this))
	{
		return false;
	}
	// A platform at the water's edge (top 100 cm above the water, face at x = 0, the water toward -X).
	Pool.AddBox(FVector(500.f, 0.f, 0.5f * (100.f + SeabedZ)), FVector(500.f, 500.f, 0.5f * (100.f - SeabedZ)));

	// 1. Requests in the water are refused (not queued).
	ALurePlayerCharacter* Swimmer = Pool.SpawnSwimming(*this, FVector2D(-1500.0, 0.0), Table);
	if (!Swimmer)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Swimmer->GetLureMovement();
	for (const ELureStance Stance : { ELureStance::Crouch, ELureStance::Prone })
	{
		Swimmer->RequestStance(Stance);
		TestEqual(TEXT("request refused at once"), static_cast<int32>(Swimmer->GetRequestedStance()), static_cast<int32>(ELureStance::Stand));
		Pool.Tick(20);
		TestEqual(TEXT("still standing"), static_cast<int32>(Swimmer->GetStance()), static_cast<int32>(ELureStance::Stand));
	}
	Swimmer->ToggleCrouch();
	Swimmer->ToggleProne();
	Pool.Tick(20);
	TestEqual(TEXT("toggles do nothing in the water"), static_cast<int32>(Swimmer->GetStance()), static_cast<int32>(ELureStance::Stand));

	// 2. The server ignores crouch/prone flags from a client's moves while the character swims.
	Movement->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_WantsToCrouch | FSavedMove_Lure::FLAG_Prone);
	TestTrue(TEXT("flags arrive as wishes"), Movement->IsCrouchRequested() && Movement->IsProneRequested());
	Pool.Tick(1);
	TestFalse(TEXT("server: crouch wish dropped in the water"), Movement->IsCrouchRequested());
	TestFalse(TEXT("server: prone wish dropped in the water"), Movement->IsProneRequested());
	Pool.Tick(20);
	TestEqual(TEXT("server: still standing"), static_cast<int32>(Swimmer->GetStance()), static_cast<int32>(ELureStance::Stand));
	TestTrue(TEXT("server: still swimming"), Movement->IsSwimming());

	// 3. Walking off the platform crouched: in the water you stand up, and stay standing.
	ALurePlayerCharacter* Walker = Pool.Spawn(FVector(300.f, 300.f, 100.f + 92.2f), Table, 180.f);
	if (!TestNotNull(TEXT("walker spawns"), Walker))
	{
		return false;
	}
	Pool.Tick(20);
	Walker->RequestStance(ELureStance::Crouch);
	Pool.Tick(20);
	TestEqual(TEXT("crouched on the platform"), static_cast<int32>(Walker->GetStance()), static_cast<int32>(ELureStance::Crouch));
	TestTrue(TEXT("walks off the edge into the water"), Pool.TickUntil([&]() { return Walker->GetLureMovement()->IsSwimming(); }, 240, Walker, FVector::BackwardVector));
	Pool.Tick(30);
	TestEqual(TEXT("stood up in the water"), static_cast<int32>(Walker->GetStance()), static_cast<int32>(ELureStance::Stand));
	TestEqual(TEXT("the crouch wish is gone"), static_cast<int32>(Walker->GetRequestedStance()), static_cast<int32>(ELureStance::Stand));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Getting out
// ---------------------------------------------------------------------------------------------------------------------

	/** Swims next to a dock of EdgeHeight, presses Jump, and reports whether the character ended up standing on it. */
	bool TryClimbOut(FAutomationTestBase& Test, UDataTable* Table, float EdgeHeight, bool bExpectOut, const FString& Label)
	{
		FPool Pool;
		if (!Pool.Create(Test))
		{
			return false;
		}
		const float FaceX = 100.f;
		Pool.AddDock(FaceX, EdgeHeight);
		ALurePlayerCharacter* Character = Pool.SpawnSwimming(Test, FVector2D(FaceX - 34.0 - 20.0, 0.0), Table);
		if (!Character)
		{
			return false;
		}
		ULureSwimTestListener* Listener = NewObject<ULureSwimTestListener>();
		FGCObjectScopeGuard KeepListener(Listener);
		Character->OnSwimStateChanged.AddDynamic(Listener, &ULureSwimTestListener::OnSwimStateChanged);
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();

		FLureClimbPlan Plan;
		const bool bPlan = Movement->FindClimbOutPlan(Plan);
		Test.TestEqual(Label + TEXT(": a climb is possible"), bPlan, bExpectOut);
		if (bPlan)
		{
			Test.TestNearlyEqual(Label + TEXT(": the edge's height above the water"), Plan.LedgeHeight, EdgeHeight, 0.5f);
		}

		Character->Jump();
		bool bClimbSeen = false;
		for (int32 Frame = 0; Frame < 150; ++Frame)
		{
			Pool.Tick(1);
			bClimbSeen |= Movement->IsClimbingOut();
			Test.TestTrue(Label + TEXT(": in the water until standing on land"), Character->IsSwimming() || Movement->IsMovingOnGround());
			if (Movement->IsMovingOnGround())
			{
				break;
			}
		}
		Pool.Tick(30);

		if (bExpectOut)
		{
			Test.TestTrue(Label + TEXT(": the climb ran"), bClimbSeen);
			Test.TestTrue(Label + TEXT(": walking on the dock"), Movement->IsMovingOnGround());
			Test.TestFalse(Label + TEXT(": not swimming"), Character->IsSwimming());
			Test.TestNearlyEqual(Label + TEXT(": feet on the dock top"), FeetZ(Character), EdgeHeight, 3.f);
			Test.TestTrue(Label + TEXT(": on the dock, past its edge"), Character->GetActorLocation().X > FaceX);
			Test.TestEqual(Label + TEXT(": standing"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Stand));
			Test.TestNearlyEqual(Label + TEXT(": stand eye height back"), Character->GetCurrentEyeHeight(), RowOf(Resolve(Table), ELureMovementState::Stand).EyeHeight, 0.5f);
			Test.TestEqual(Label + TEXT(": event: out of the water"), EventsText(Listener->Events), FString(TEXT("out")));
		}
		else
		{
			Test.TestFalse(Label + TEXT(": no climb"), bClimbSeen);
			Test.TestTrue(Label + TEXT(": still swimming"), Movement->IsSwimming());
			Test.TestTrue(Label + TEXT(": still in front of the dock"), Character->GetActorLocation().X < FaceX);
			Test.TestEqual(Label + TEXT(": no event"), Listener->Events.Num(), 0);
		}
		return true;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimClimbOutTest, "Project.Movement.Swim.ClimbOutAtEdgeHeightFromData", LureSwimTest::Flags)

bool FLureSwimClimbOutTest::RunTest(const FString& Parameters)
{
	UDataTable* Shipped = ShippedTable(*this);
	FGCObjectScopeGuard KeepShipped(Shipped); // test worlds come and go (and collect garbage) while it is in use
	if (!Shipped)
	{
		return false;
	}
	const float MaxHeight = RowOf(Resolve(Shipped), ELureMovementState::Swim).ClimbMaxHeight;
	TestNearlyEqual(TEXT("shipped rule: 60 cm"), MaxHeight, 60.f);
	TryClimbOut(*this, Shipped, MaxHeight, true, TEXT("60 cm edge"));
	TryClimbOut(*this, Shipped, MaxHeight + 1.f, false, TEXT("61 cm edge"));
	TryClimbOut(*this, Shipped, 25.f, true, TEXT("25 cm edge"));

	// Another number in the table moves the limit.
	UDataTable* Fixture = ShippedTable(*this);
	FGCObjectScopeGuard KeepFixture(Fixture); // test worlds come and go (and collect garbage) while it is in use
	for (const ELureMovementState State : { ELureMovementState::Swim, ELureMovementState::SwimSprint })
	{
		EditRow(Fixture, State)->ClimbMaxHeight = 40.f;
	}
	TryClimbOut(*this, Fixture, 40.f, true, TEXT("fixture 40: 40 cm edge"));
	TryClimbOut(*this, Fixture, 41.f, false, TEXT("fixture 40: 41 cm edge"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimLadderTest, "Project.Movement.Swim.LadderClimbsHighEdge", LureSwimTest::Flags)

bool FLureSwimLadderTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FPool Pool;
	if (!Table || !Pool.Create(*this))
	{
		return false;
	}
	const float FaceX = 100.f;
	const float DockHeight = 150.f;
	Pool.AddDock(FaceX, DockHeight);
	// Facing along the dock (not at it): the ladder decides the direction.
	ALurePlayerCharacter* Character = Pool.SpawnSwimming(*this, FVector2D(FaceX - 34.0 - 15.0, 0.0), Table, 90.f);
	if (!Character)
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	FLureClimbPlan Plan;
	TestFalse(TEXT("no ladder: a 150 cm dock can't be climbed"), Movement->FindClimbOutPlan(Plan));

	// The ladder sits on the dock face at the water line, +X pointing out over the water (yaw 180 here).
	ALureLadder* Ladder = Pool.AddLadder(FVector(FaceX, 0.f, 0.f), 180.f, 300.f);
	if (!TestNotNull(TEXT("ladder spawns"), Ladder))
	{
		return false;
	}
	Pool.Tick(1);
	TestTrue(TEXT("the swimmer is in the ladder's grab zone"), Ladder->IsInGrabZone(Character->GetActorLocation()));
	TestNearlyEqual(TEXT("the ladder raises the climb height"), Movement->GetClimbOutMaxHeight(), 300.f);
	TestTrue(TEXT("with the ladder: a climb is possible"), Movement->FindClimbOutPlan(Plan) && Plan.bUsesLadder);

	Character->Jump();
	TestTrue(TEXT("climbs out"), Pool.TickUntil([&]() { return Movement->IsMovingOnGround(); }, 240));
	Pool.Tick(20);
	TestNearlyEqual(TEXT("feet on the high dock"), FeetZ(Character), DockHeight, 3.f);
	TestTrue(TEXT("on the dock"), Character->GetActorLocation().X > FaceX);
	TestFalse(TEXT("out of the water"), Character->IsSwimming());
	TestFalse(TEXT("a swimmer outside the zone is not on the ladder"), Ladder->IsInGrabZone(FVector(FaceX - 300.f, 0.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimBeachTest, "Project.Movement.Swim.WalkOutAtBeachRestoresWalkingAndStand", LureSwimTest::Flags)

bool FLureSwimBeachTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FPool Pool;
	if (!Table || !Pool.Create(*this))
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = Resolve(Table);
	// A 12 degree beach: its surface crosses the water line at x ~ 0 and rises toward +X.
	const float Pitch = 12.f;
	const float HalfThickness = 50.f;
	const float Cos = FMath::Cos(FMath::DegreesToRadians(Pitch));
	Pool.AddBox(FVector(0.f, 0.f, -HalfThickness * Cos), FVector(1500.f, 600.f, HalfThickness), FRotator(Pitch, 0.f, 0.f));

	// Start on the beach, crouch, and walk backwards into the sea.
	ALurePlayerCharacter* Character = Pool.Spawn(FVector(600.f, 0.f, 600.f * FMath::Tan(FMath::DegreesToRadians(Pitch)) + 100.f), Table);
	if (!TestNotNull(TEXT("character spawns"), Character))
	{
		return false;
	}
	ULureSwimTestListener* Listener = NewObject<ULureSwimTestListener>();
	FGCObjectScopeGuard KeepListener(Listener);
	Character->OnSwimStateChanged.AddDynamic(Listener, &ULureSwimTestListener::OnSwimStateChanged);
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	Pool.Tick(30);
	TestTrue(TEXT("standing on the beach"), Movement->IsMovingOnGround());
	Character->RequestStance(ELureStance::Crouch);
	Pool.Tick(20);
	TestTrue(TEXT("wades in and swims"), Pool.TickUntil([&]() { return Movement->IsSwimming(); }, 900, Character, FVector::BackwardVector));
	Pool.TickMoving(Character, 240, FVector::BackwardVector);
	TestTrue(TEXT("swimming in deep water"), Movement->IsSwimming());
	TestEqual(TEXT("the crouch wish was dropped in the water"), static_cast<int32>(Character->GetRequestedStance()), static_cast<int32>(ELureStance::Stand));

	// Swim back and walk up the beach.
	TestTrue(TEXT("walks out at the beach"), Pool.TickUntil([&]() { return Movement->IsMovingOnGround() && !Character->IsSwimming(); }, 900, Character, FVector::ForwardVector));
	Pool.TickMoving(Character, 120, FVector::ForwardVector);
	TestTrue(TEXT("walking on the beach"), Movement->IsMovingOnGround());
	TestTrue(TEXT("feet above the water"), FeetZ(Character) > 0.f);
	TestEqual(TEXT("Stand restored"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Stand));
	TestEqual(TEXT("row Stand again"), static_cast<int32>(Movement->GetMovementState()), static_cast<int32>(ELureMovementState::Stand));
	Pool.Tick(30);
	TestNearlyEqual(TEXT("stand eye height back"), Character->GetCurrentEyeHeight(), RowOf(Rows, ELureMovementState::Stand).EyeHeight, 0.5f);
	TestNearlyEqual(TEXT("walking speed limit back"), Movement->GetMaxSpeed(), RowOf(Rows, ELureMovementState::Stand).MaxSpeed);
	TestTrue(TEXT("events: in, then out"), Listener->Events.Num() >= 2 && Listener->Events[0] && !Listener->Events.Last());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Networking and arms
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimReplicationTest, "Project.Movement.Swim.SwimStateReplicates", LureSwimTest::Flags)

bool FLureSwimReplicationTest::RunTest(const FString& Parameters)
{
	// The swim state is the engine's movement mode: the owning client and the server simulate it from the same moves,
	// and other players get ACharacter::ReplicatedMovementMode (COND_SimulatedOnly).
	ALurePlayerCharacter::StaticClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Lifetime;
	GetDefault<ALurePlayerCharacter>()->GetLifetimeReplicatedProps(Lifetime);
	const FProperty* ModeProperty = FindFProperty<FProperty>(ACharacter::StaticClass(), TEXT("ReplicatedMovementMode"));
	if (TestNotNull(TEXT("ReplicatedMovementMode property"), ModeProperty))
	{
		const FLifetimeProperty* Entry = Lifetime.FindByPredicate([ModeProperty](const FLifetimeProperty& P) { return P.RepIndex == ModeProperty->RepIndex; });
		TestTrue(TEXT("the movement mode is replicated"), Entry != nullptr);
		if (Entry)
		{
			TestEqual(TEXT("to other players (simulated only)"), static_cast<int32>(Entry->Condition), static_cast<int32>(COND_SimulatedOnly));
		}
	}

	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FPool Pool;
	if (!Table || !Pool.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Server = Pool.SpawnSwimming(*this, FVector2D::ZeroVector, Table);
	ALurePlayerCharacter* Proxy = Pool.Spawn(FVector(500.f, 2000.f, 400.f), Table);
	if (!Server || !TestNotNull(TEXT("proxy spawns"), Proxy))
	{
		return false;
	}
	Pool.Tick(2);
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();
	ULureCharacterMovementComponent* ProxyMovement = Proxy->GetLureMovement();

	// What the server sends.
	const uint8 Packed = ServerMovement->PackNetworkMovementMode();
	TEnumAsByte<EMovementMode> Mode;
	uint8 CustomMode = 0;
	TEnumAsByte<EMovementMode> GroundMode;
	ServerMovement->UnpackNetworkMovementMode(Packed, Mode, CustomMode, GroundMode);
	TestEqual(TEXT("packed mode: swimming"), static_cast<int32>(Mode.GetValue()), static_cast<int32>(MOVE_Swimming));

	// What another player's copy does with it.
	ULureSwimTestListener* Listener = NewObject<ULureSwimTestListener>();
	FGCObjectScopeGuard KeepListener(Listener);
	Proxy->OnSwimStateChanged.AddDynamic(Listener, &ULureSwimTestListener::OnSwimStateChanged);
	Proxy->SetRole(ROLE_SimulatedProxy);
	ProxyMovement->ApplyNetworkMovementMode(Packed);
	TestTrue(TEXT("proxy: swimming"), Proxy->IsSwimming());
	TestEqual(TEXT("proxy: row Swim (eye height, noise)"), static_cast<int32>(ProxyMovement->GetMovementState()), static_cast<int32>(ELureMovementState::Swim));
	TestEqual(TEXT("proxy: stance Stand"), static_cast<int32>(Proxy->GetStance()), static_cast<int32>(ELureStance::Stand));

	// The climb out travels the same way (custom mode) and still counts as "in the water".
	uint8 ClimbPacked = 0;
	{
		TGuardValue<TEnumAsByte<EMovementMode>> ModeGuard(ServerMovement->MovementMode, TEnumAsByte<EMovementMode>(MOVE_Custom));
		TGuardValue<uint8> CustomGuard(ServerMovement->CustomMovementMode, static_cast<uint8>(ELureCustomMovementMode::ClimbOut));
		ClimbPacked = ServerMovement->PackNetworkMovementMode();
	}
	ProxyMovement->ApplyNetworkMovementMode(ClimbPacked);
	TestTrue(TEXT("proxy: climbing out"), ProxyMovement->IsClimbingOut());
	TestTrue(TEXT("proxy: climbing out counts as in the water"), Proxy->IsSwimming());

	// Back on land.
	uint8 WalkPacked = 0;
	{
		TGuardValue<TEnumAsByte<EMovementMode>> ModeGuard(ServerMovement->MovementMode, TEnumAsByte<EMovementMode>(MOVE_Walking));
		WalkPacked = ServerMovement->PackNetworkMovementMode();
	}
	ProxyMovement->ApplyNetworkMovementMode(WalkPacked);
	TestFalse(TEXT("proxy: out of the water"), Proxy->IsSwimming());
	TestEqual(TEXT("proxy events: in, out"), EventsText(Listener->Events), FString(TEXT("in,out")));
	Proxy->SetRole(ROLE_Authority);

	// The sprint wish travels in the moves (FLAG_Custom_0): the server sprint-swims from the client's flags.
	ServerMovement->UpdateFromCompressedFlags(FSavedMove_Lure::FLAG_Sprint);
	Pool.TickMoving(Server, 120);
	TestEqual(TEXT("server: sprint flag -> SwimSprint"), static_cast<int32>(ServerMovement->GetMovementState()), static_cast<int32>(ELureMovementState::SwimSprint));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimArmsTest, "Project.Movement.Swim.ArmsLoweredWhileSwimming", LureSwimTest::Flags)

bool FLureSwimArmsTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table); // test worlds come and go (and collect garbage) while it is in use
	FPool Pool;
	if (!Table || !Pool.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Player = Pool.Spawn(FVector(0.f, 0.f, 190.f), Table);
	APlayerController* Controller = Pool.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("character and controller spawn"), Player) || !Controller)
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	TestFalse(TEXT("a swim-stroke slot exists (for the animation-artist)"), Player->SwimStrokeAnimation.IsNull());
	Pool.Tick(240);
	TestTrue(TEXT("swimming"), Player->IsSwimming());

	const bool bStrokeClip = Player->GetFirstPersonArms() && Player->GetFirstPersonArms()->GetAnimInstance()
		&& FPackageName::DoesPackageExist(Player->SwimStrokeAnimation.ToSoftObjectPath().GetLongPackageName());
	if (bStrokeClip)
	{
		AddInfo(TEXT("The swim-stroke clip is imported: the arms play it instead of lowering."));
	}
	else
	{
		TestTrue(FString::Printf(TEXT("placeholder: arms lowered (alpha %.2f)"), Player->GetSwimArmsAlpha()), Player->GetSwimArmsAlpha() > 0.95f);
		TestTrue(FString::Printf(TEXT("placeholder: arms dropped out of view (%.1f cm)"), Player->GetArmsBobOffset().GetLocation().Z),
			Player->GetArmsBobOffset().GetLocation().Z < -0.9f * Player->SwimArmsDrop);
	}
	Controller->UnPossess();
	Pool.Tick(2);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Review fixes D0 / D3 / D4: the climb rule's 0, the lowest climb-out edge and the swim feel are data
// ---------------------------------------------------------------------------------------------------------------------

	/** The CSV text without the named columns (an older table, or a new row that leaves the optional columns out). */
	FString WithoutColumns(const FString& Csv, const TArray<FString>& Drop)
	{
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		TArray<FString> Header;
		if (Lines.Num() > 0)
		{
			Lines[0].ParseIntoArray(Header, TEXT(","), false);
		}
		TArray<FString> Out;
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT(","), false);
			TArray<FString> Kept;
			for (int32 Index = 0; Index < Cells.Num(); ++Index)
			{
				if (!Header.IsValidIndex(Index) || !Drop.Contains(Header[Index].TrimStartAndEnd()))
				{
					Kept.Add(Cells[Index]);
				}
			}
			Out.Add(FString::Join(Kept, TEXT(",")));
		}
		return FString::Join(Out, TEXT("\n")) + TEXT("\n");
	}

	/** The shipped CSV minus Drop, as a transient table. */
	UDataTable* ShippedTableWithout(FAutomationTestBase& Test, const TArray<FString>& Drop)
	{
		FString Csv;
		if (!Test.TestTrue(TEXT("data/tables/DT_Movement.csv loads"), FFileHelper::LoadFileToString(Csv, *CsvPath())))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureMovementRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(WithoutColumns(Csv, Drop));
		Test.TestEqual(FString::Printf(TEXT("CSV (without %s) import problems (%s)"), *FString::Join(Drop, TEXT("/")), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		return Table;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimOptionalColumnsTest, "Project.Movement.Swim.Data.OptionalFeelColumnsDefaultAndValidate", LureSwimTest::Flags)

bool FLureSwimOptionalColumnsTest::RunTest(const FString& Parameters)
{
	// The built-in defaults are the values the code used before they became data (T-026 review D3/D4).
	const FLureMovementRow Defaults;
	TestNearlyEqual(TEXT("default ClimbOutLowestTop"), Defaults.ClimbOutLowestTop, -20.f);
	TestNearlyEqual(TEXT("default ClimbOutReach"), Defaults.ClimbOutReach, 45.f);
	TestNearlyEqual(TEXT("default SurfaceFloatSettleTime"), Defaults.SurfaceFloatSettleTime, 0.8f);
	TestNearlyEqual(TEXT("default SwimBrakingDeceleration"), Defaults.SwimBrakingDeceleration, 600.f);

	// The shipped table carries them (same values) on every row.
	UDataTable* Shipped = ShippedTable(*this);
	FGCObjectScopeGuard KeepShipped(Shipped);
	if (!Shipped)
	{
		return false;
	}
	for (const ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		const FLureMovementRow* Row = EditRow(Shipped, State);
		const FString Name = FLureMovementData::GetRowName(State).ToString();
		if (!TestNotNull(Name + TEXT(" exists"), Row))
		{
			continue;
		}
		TestNearlyEqual(Name + TEXT(".ClimbOutLowestTop"), Row->ClimbOutLowestTop, -20.f);
		TestNearlyEqual(Name + TEXT(".ClimbOutReach"), Row->ClimbOutReach, 45.f);
		TestNearlyEqual(Name + TEXT(".SurfaceFloatSettleTime"), Row->SurfaceFloatSettleTime, 0.8f);
		TestNearlyEqual(Name + TEXT(".SwimBrakingDeceleration"), Row->SwimBrakingDeceleration, 600.f);
	}

	// A table without the new columns (or without any optional column) resolves every row, with the defaults.
	const TArray<FString> NewColumns = { TEXT("ClimbOutLowestTop"), TEXT("ClimbOutReach"), TEXT("SurfaceFloatSettleTime"), TEXT("SwimBrakingDeceleration") };
	TArray<FString> AllOptional = NewColumns;
	AllOptional.Append({ TEXT("ClimbMaxHeight"), TEXT("ClimbSpeed"), TEXT("SurfaceFloatDepth"), TEXT("ArmsPullBack"), TEXT("ExitTransitionTime") });
	const TArray<FString>* Drops[] = { &NewColumns, &AllOptional };
	for (const TArray<FString>* Drop : Drops)
	{
		UDataTable* Older = ShippedTableWithout(*this, *Drop);
		FGCObjectScopeGuard KeepOlder(Older);
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		const uint8 Mask = FLureMovementData::ResolveRows(Older, Rows, Problems);
		const FString Label = FString::Printf(TEXT("without %d optional columns"), Drop->Num());
		TestEqual(Label + TEXT(": no row falls back (") + FString::Join(Problems, TEXT("; ")) + TEXT(")"), static_cast<int32>(Mask), 0);
		const FLureMovementRow& Swim = RowOf(Rows, ELureMovementState::Swim);
		TestNearlyEqual(Label + TEXT(": Swim.ClimbOutLowestTop default"), Swim.ClimbOutLowestTop, -20.f);
		TestNearlyEqual(Label + TEXT(": Swim.ClimbOutReach default"), Swim.ClimbOutReach, 45.f);
		TestNearlyEqual(Label + TEXT(": Swim.SurfaceFloatSettleTime default"), Swim.SurfaceFloatSettleTime, 0.8f);
		TestNearlyEqual(Label + TEXT(": Swim.SwimBrakingDeceleration default"), Swim.SwimBrakingDeceleration, 600.f);
	}

	// Validation.
	struct FBad
	{
		const TCHAR* Label;
		TFunction<void(FLureMovementRow&)> Edit;
	};
	const FBad BadRows[] = {
		{ TEXT("ClimbOutLowestTop above the water"), [](FLureMovementRow& R) { R.ClimbOutLowestTop = 5.f; } },
		{ TEXT("negative ClimbOutReach"), [](FLureMovementRow& R) { R.ClimbOutReach = -1.f; } },
		{ TEXT("SurfaceFloatSettleTime 0"), [](FLureMovementRow& R) { R.SurfaceFloatSettleTime = 0.f; } },
		{ TEXT("SurfaceFloatSettleTime 0.01"), [](FLureMovementRow& R) { R.SurfaceFloatSettleTime = 0.01f; } },
		{ TEXT("negative SwimBrakingDeceleration"), [](FLureMovementRow& R) { R.SwimBrakingDeceleration = -10.f; } },
		{ TEXT("NaN ClimbOutReach"), [](FLureMovementRow& R) { R.ClimbOutReach = std::numeric_limits<float>::quiet_NaN(); } },
	};
	const FLureMovementRow Swim = FLureMovementData::GetFallbackRow(ELureMovementState::Swim);
	FString Problem;
	TestTrue(TEXT("the fallback Swim row is valid"), Swim.Validate(Problem));
	for (const FBad& Bad : BadRows)
	{
		FLureMovementRow Row = Swim;
		Bad.Edit(Row);
		FString Why;
		TestFalse(FString::Printf(TEXT("%s is refused"), Bad.Label), Row.Validate(Why));
	}
	FLureMovementRow Edge = Swim;
	Edge.ClimbOutLowestTop = 0.f;
	Edge.ClimbOutReach = 0.f;
	Edge.SwimBrakingDeceleration = 0.f;
	Edge.SurfaceFloatSettleTime = 0.05f;
	TestTrue(TEXT("the limits themselves are allowed"), Edge.Validate(Problem));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimFeelFromDataTest, "Project.Movement.Swim.FeelFromData", LureSwimTest::Flags)

bool FLureSwimFeelFromDataTest::RunTest(const FString& Parameters)
{
	// Braking: the engine's swimming braking follows the Swim row.
	for (const float Braking : { 600.f, 150.f })
	{
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FPool Pool;
		if (!Table || !Pool.Create(*this))
		{
			return false;
		}
		EditRow(Table, ELureMovementState::Swim)->SwimBrakingDeceleration = Braking;
		EditRow(Table, ELureMovementState::SwimSprint)->SwimBrakingDeceleration = Braking;
		ALurePlayerCharacter* Character = Pool.SpawnSwimming(*this, FVector2D::ZeroVector, Table);
		if (!Character)
		{
			return false;
		}
		TestNearlyEqual(FString::Printf(TEXT("BrakingDecelerationSwimming = row %.0f"), Braking), Character->GetLureMovement()->BrakingDecelerationSwimming, Braking);
	}

	// Settle time: half a second after falling in, a quick settle is much closer to the float height than a slow one.
	float Error[2] = { 0.f, 0.f };
	const float SettleTimes[2] = { 0.3f, 3.f };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UDataTable* Table = ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FPool Pool;
		if (!Table || !Pool.Create(*this))
		{
			return false;
		}
		EditRow(Table, ELureMovementState::Swim)->SurfaceFloatSettleTime = SettleTimes[Index];
		EditRow(Table, ELureMovementState::SwimSprint)->SurfaceFloatSettleTime = SettleTimes[Index];
		const float Depth = EditRow(Table, ELureMovementState::Swim)->SurfaceFloatDepth;
		ALurePlayerCharacter* Character = Pool.Spawn(FVector(0.f, 0.f, 190.f), Table);
		if (!TestNotNull(TEXT("character spawns"), Character))
		{
			return false;
		}
		TestTrue(TEXT("falls in"), Pool.TickUntil([&]() { return Character->GetLureMovement()->IsSwimming(); }, 120));
		Pool.Tick(30);
		Error[Index] = FMath::Abs(CenterZ(Character) + Depth);
	}
	TestTrue(FString::Printf(TEXT("settle 0.3 s is closer to the float height after 0.5 s than 3 s (%.1f vs %.1f cm)"), Error[0], Error[1]), Error[0] + 2.f < Error[1]);

	// Reach: a swimmer 20 cm from the dock face climbs out with the shipped 45 cm (ClimbOutAtEdgeHeightFromData), not with 10 cm.
	UDataTable* Short = ShippedTable(*this);
	FGCObjectScopeGuard KeepShort(Short);
	if (!Short)
	{
		return false;
	}
	EditRow(Short, ELureMovementState::Swim)->ClimbOutReach = 10.f;
	EditRow(Short, ELureMovementState::SwimSprint)->ClimbOutReach = 10.f;
	TryClimbOut(*this, Short, 40.f, false, TEXT("reach 10: 40 cm edge 20 cm away"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimLowestTopTest, "Project.Movement.Swim.ClimbOutLowestTopFromData", LureSwimTest::Flags)

bool FLureSwimLowestTopTest::RunTest(const FString& Parameters)
{
	// A submerged shelf 40 cm below the water, too high to step onto from the floating feet (-100 cm): the shipped
	// -20 cm rule calls it seabed; a table with ClimbOutLowestTop -60 lets Jump climb onto it.
	UDataTable* Shipped = ShippedTable(*this);
	FGCObjectScopeGuard KeepShipped(Shipped);
	UDataTable* Deep = ShippedTable(*this);
	FGCObjectScopeGuard KeepDeep(Deep);
	if (!Shipped || !Deep)
	{
		return false;
	}
	EditRow(Deep, ELureMovementState::Swim)->ClimbOutLowestTop = -60.f;
	EditRow(Deep, ELureMovementState::SwimSprint)->ClimbOutLowestTop = -60.f;
	TryClimbOut(*this, Shipped, -40.f, false, TEXT("lowest -20: shelf at -40"));
	TryClimbOut(*this, Deep, -40.f, true, TEXT("lowest -60: shelf at -40"));
	TryClimbOut(*this, Deep, 30.f, true, TEXT("lowest -60: 30 cm edge still works"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureNoClimbRuleLandingTest, "Project.Movement.Climb.NoClimbRuleLandsOnSlopes", LureSwimTest::Flags)

bool FLureNoClimbRuleLandingTest::RunTest(const FString& Parameters)
{
	// T-026 review D0: a land row with ClimbMaxHeight 0 (or the column missing) has no climb rule, so a landing that
	// lifts the feet on a 30 deg slope (walking up out of the water, a jump onto a dune) is the engine's: accepted.
	// With the shipped 100 cm rule the same contact is refused when it is far above the takeoff, and accepted near it.
	struct FCase
	{
		const TCHAR* Label;
		bool bNoRule;			// land rows without the climb columns
		float TakeoffBelowFeet;	// cm
		bool bExpectValid;
	};
	const FCase Cases[] = {
		{ TEXT("no climb rule, takeoff 50 cm below"), true, 50.f, true },
		{ TEXT("no climb rule, takeoff 300 cm below"), true, 300.f, true },
		{ TEXT("rule 100, takeoff 50 cm below"), false, 50.f, true },
		{ TEXT("rule 100, takeoff 300 cm below"), false, 300.f, false },
	};
	for (const FCase& Case : Cases)
	{
		UDataTable* Table = Case.bNoRule ? ShippedTableWithout(*this, { TEXT("ClimbMaxHeight"), TEXT("ClimbSpeed") }) : ShippedTable(*this);
		FGCObjectScopeGuard KeepTable(Table);
		FPool Pool;
		if (!Table || !Pool.Create(*this, /*bSeabed*/ false))
		{
			return false;
		}
		const FLureMovementRow Stand = RowOf(Resolve(Table), ELureMovementState::Stand);
		TestNearlyEqual(FString(Case.Label) + TEXT(": Stand.ClimbMaxHeight"), Stand.ClimbMaxHeight, Case.bNoRule ? 0.f : 100.f);

		// A 30 deg slope well outside the water.
		const FVector SlopeCenter(6000.f, 0.f, 0.f);
		Pool.AddBox(SlopeCenter, FVector(600.f, 400.f, 50.f), FRotator(30.f, 0.f, 0.f));
		ALurePlayerCharacter* Character = Pool.Spawn(SlopeCenter + FVector(0.f, 0.f, 400.f), Table);
		if (!TestNotNull(TEXT("character spawns"), Character))
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();

		// Where the capsule touches the slope coming straight down.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LureNoClimbRuleLanding), false, Character);
		const FVector Start = Capsule->GetComponentLocation();
		const bool bHit = Pool.World->SweepSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 1000.f), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
		if (!TestTrue(FString(Case.Label) + TEXT(": the sweep hits the slope"), bHit && Hit.bBlockingHit && !Hit.bStartPenetrating))
		{
			return false;
		}
		const float Feet = static_cast<float>(Hit.Location.Z) - Capsule->GetScaledCapsuleHalfHeight();
		const float Lift = static_cast<float>(Hit.ImpactPoint.Z) - Feet;
		TestTrue(FString::Printf(TEXT("%s: the contact lifts the feet (%.1f cm)"), Case.Label, Lift), Lift > 3.f);

		Movement->SetMovementMode(MOVE_Falling);
		FLureMovementTestAccess::SetTakeoffFeetHeight(*Movement, Feet - Case.TakeoffBelowFeet);
		TestEqual(FString::Printf(TEXT("%s: landing accepted"), Case.Label), Movement->IsValidLandingSpot(Hit.Location, Hit), Case.bExpectValid);
	}
	return true;
}

} // namespace LureSwimTest

#endif // WITH_DEV_AUTOMATION_TESTS

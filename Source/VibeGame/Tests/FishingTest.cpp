// Lure T-006: casting, bobber, bite and hook (implementer's tests; QA adds its own). Project.Fishing.*
// Tables come from the text sources in data/tables/ (never the binary assets). Worlds are transient FTestWorldWrapper game worlds:
// a 1 m high dock (top z = 100) and a Lure.Water surface at z = 0 around it.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/FPArmsAnimInstance.h"
#include "Character/FPArmsPose.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Game/LureGameMode.h"
#include "Game/LureHUD.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/FishQATestHelpers.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace LureFishingTest
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	/** Dock top (the character stands at its +X edge and casts along +X over the water). */
	constexpr float DockTop = 100.f;
	constexpr float DockEdgeX = 400.f;
	const FVector StandAt(350.f, 0.f, DockTop);

	FString SourcePath(const TCHAR* FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
	}

	UDataTable* ImportCsv(FAutomationTestBase& Test, UScriptStruct* RowStruct, const TCHAR* FileName, FString* OutCsv = nullptr)
	{
		FString Csv;
		if (!Test.TestTrue(FString::Printf(TEXT("data/tables/%s loads"), FileName), FFileHelper::LoadFileToString(Csv, *SourcePath(FileName))))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = RowStruct;
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		Test.TestEqual(FString::Printf(TEXT("%s import problems (%s)"), FileName, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		if (OutCsv)
		{
			*OutCsv = Csv;
		}
		return Table;
	}

	UDataTable* MovementTable(FAutomationTestBase& Test)
	{
		return ImportCsv(Test, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"));
	}

	UDataTable* FishingTable(FAutomationTestBase& Test)
	{
		return ImportCsv(Test, FLureFishingRow::StaticStruct(), TEXT("DT_Fishing.csv"));
	}

	TArray<FLureMovementRow> MovementRows(const UDataTable* Table)
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

	/** A fast, deterministic profile for flow tests: bite 0.2 s after landing, no nibbles, stays hooked. */
	FLureFishingRow QuickProfile(float HookWindow = 0.8f)
	{
		FLureFishingRow Row = FLureFishingRules::GetFallbackRow();
		Row.BiteWaitMin = 0.2f;
		Row.BiteWaitMax = 0.2f;
		Row.RebiteWaitMin = 0.5f;
		Row.RebiteWaitMax = 0.5f;
		Row.NibblesMin = 0;
		Row.NibblesMax = 0;
		Row.HookWindow = HookWindow;
		Row.AutoLandDelay = 0.f;
		return Row;
	}

	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), false);
	}

	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		UDataTable* Movement = nullptr;

		bool Create(FAutomationTestBase& Test, bool bWater = true)
		{
			Movement = MovementTable(Test);
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			// The dock: 8 x 8 m, 1 m above the water.
			AddBox(FVector(0.f, 0.f, DockTop * 0.5f), FVector(DockEdgeX, DockEdgeX, DockTop * 0.5f));
			if (bWater)
			{
				AddWater(0.f);
			}
			return World != nullptr && Movement != nullptr;
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

		/** A non-colliding water surface tagged Lure.Water whose top is at SurfaceZ. */
		AActor* AddWater(float SurfaceZ)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Water"));
			Box->SetBoxExtent(FVector(20000.f, 20000.f, 50.f), false);
			Box->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(FVector(0.f, 0.f, SurfaceZ - 50.f));
			Actor->Tags.Add(GetDefault<ULureFishingSettings>()->WaterTag);
			return Actor;
		}

		/** A fishing spot marker like the level builder places (TargetPoint-style actor with Key=Value tags). */
		AActor* AddSpot(const FVector& Location, const TArray<FString>& KeyValues)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocation(Location);
			Actor->Tags.Add(TEXT("LureLayout"));
			Actor->Tags.Add(GetDefault<ULureFishingSettings>()->FishingSpotTag);
			for (const FString& KeyValue : KeyValues)
			{
				Actor->Tags.Add(FName(*KeyValue));
			}
			return Actor;
		}

		ALurePlayerCharacter* Spawn(const FVector& Feet, TFunction<void(ALurePlayerCharacter&)> PreFinish = nullptr)
		{
			const float HalfHeight = RowOf(MovementRows(Movement), ELureMovementState::Stand).CapsuleHalfHeight;
			const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, HalfHeight + 2.15f));
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character)
			{
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Movement);
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			if (PreFinish)
			{
				PreFinish(*Character);
			}
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

		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames)
		{
			for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
			{
				if (Predicate())
				{
					return true;
				}
				Wrapper.TickTestWorld(Dt);
			}
			return Predicate();
		}
	};

	/** Fishing component of a spawned character, with the real fish tables and a fixed seed. */
	ULureFishingComponent* SetUpFishing(ALurePlayerCharacter* Character, const FishQA::FTables& Tables, const FLureFishingRow& Profile, float Hours = 12.f, int32 Seed = 1234)
	{
		ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
		if (Fishing)
		{
			Fishing->SetFishingProfile(Profile);
			Fishing->SetFishTables(Tables.Get());
			Fishing->SetRandomSeed(Seed);
			Fishing->TimeOfDayOverride = Hours;
		}
		return Fishing;
	}

	FString StateName(ELureFishingState State)
	{
		return StaticEnum<ELureFishingState>()->GetNameStringByValue(static_cast<int64>(State));
	}

	FString ResultName(ELureFishingResult Result)
	{
		return StaticEnum<ELureFishingResult>()->GetNameStringByValue(static_cast<int64>(Result));
	}

	FString BlockName(ELureCastBlock Block)
	{
		return StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Block));
	}

	FString PoseName(EFPArmsPose Pose)
	{
		return StaticEnum<EFPArmsPose>()->GetNameStringByValue(static_cast<int64>(Pose));
	}

	/** Casts along +X from StandAt and ticks until the bobber floats (Waiting). */
	bool CastAndLand(FAutomationTestBase& Test, FWorld& World, ULureFishingComponent* Fishing, float Charge = 0.5f)
	{
		if (!Test.TestTrue(TEXT("cast starts"), Fishing && Fishing->AuthorityCast(Charge, 0.f)))
		{
			return false;
		}
		return Test.TestTrue(TEXT("bobber lands (Waiting)"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	}
}

using namespace LureFishingTest;

// =====================================================================================================================
// Cast distance and charge
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingCastDistanceFromCharge, "Project.Fishing.Cast.DistanceFromCharge", LureFishingTest::TestFlags)
bool FLureFishingCastDistanceFromCharge::RunTest(const FString& Parameters)
{
	const UDataTable* Table = FishingTable(*this);
	const FLureFishingRow* Shipped = Table ? Table->FindRow<FLureFishingRow>(TEXT("Default"), TEXT("test"), false) : nullptr;
	if (!TestNotNull(TEXT("DT_Fishing Default row"), Shipped))
	{
		return false;
	}
	const FLureFishingRow& Row = *Shipped;
	TestNearlyEqual(TEXT("no charge = MinCastDistance"), FLureFishingRules::CastDistance(Row, 0.f), Row.MinCastDistance, 0.01f);
	TestNearlyEqual(TEXT("full charge = MaxCastDistance"), FLureFishingRules::CastDistance(Row, 1.f), Row.MaxCastDistance, 0.01f);
	TestNearlyEqual(TEXT("half charge (exponent 1) = halfway"), FLureFishingRules::CastDistance(Row, 0.5f), 0.5f * (Row.MinCastDistance + Row.MaxCastDistance), 0.01f);
	TestNearlyEqual(TEXT("charge above 1 clamps"), FLureFishingRules::CastDistance(Row, 3.f), Row.MaxCastDistance, 0.01f);
	TestNearlyEqual(TEXT("negative charge clamps"), FLureFishingRules::CastDistance(Row, -1.f), Row.MinCastDistance, 0.01f);
	TestNearlyEqual(TEXT("NaN charge = no charge"), FLureFishingRules::CastDistance(Row, std::numeric_limits<float>::quiet_NaN()), Row.MinCastDistance, 0.01f);
	float Previous = -1.f;
	bool bMonotonic = true;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const float Distance = FLureFishingRules::CastDistance(Row, Step / 20.f);
		bMonotonic &= Distance >= Previous;
		Previous = Distance;
	}
	TestTrue(TEXT("more charge never casts shorter"), bMonotonic);

	// Charge from holding: ChargeTime from data.
	TestNearlyEqual(TEXT("half the charge time = half charge"), FLureFishingRules::ChargeFromHoldTime(Row, 0.5f * Row.ChargeTime), 0.5f, 0.001f);
	TestNearlyEqual(TEXT("holding longer stays full"), FLureFishingRules::ChargeFromHoldTime(Row, 10.f * Row.ChargeTime), 1.f, 0.001f);
	TestNearlyEqual(TEXT("a tap is no charge"), FLureFishingRules::ChargeFromHoldTime(Row, 0.f), 0.f, 0.001f);

	// Other data, other numbers (the curve and range are data).
	FLureFishingRow Other = Row;
	Other.MinCastDistance = 500.f;
	Other.MaxCastDistance = 2500.f;
	Other.ChargeExponent = 2.f;
	Other.ChargeTime = 2.f;
	TestNearlyEqual(TEXT("fixture: exponent 2 at half charge = a quarter of the range"), FLureFishingRules::CastDistance(Other, 0.5f), 1000.f, 0.01f);
	TestNearlyEqual(TEXT("fixture: ChargeTime 2 s, 1 s held = half charge"), FLureFishingRules::ChargeFromHoldTime(Other, 1.f), 0.5f, 0.001f);

	// Flight time from speed, clamped.
	TestNearlyEqual(TEXT("flight time = distance / speed"), FLureFishingRules::CastFlightTime(Row, Row.CastSpeed), FMath::Clamp(1.f, Row.CastFlightTimeMin, Row.CastFlightTimeMax), 0.001f);
	TestNearlyEqual(TEXT("short casts use the minimum flight time"), FLureFishingRules::CastFlightTime(Row, 1.f), Row.CastFlightTimeMin, 0.001f);
	TestNearlyEqual(TEXT("long casts use the maximum flight time"), FLureFishingRules::CastFlightTime(Row, 1.0e6f), Row.CastFlightTimeMax, 0.001f);
	const FVector From(0.f), To(1000.f, 0.f, 0.f);
	TestTrue(TEXT("arc starts at the rod"), FLureFishingRules::CastArcPoint(From, To, 200.f, 0.f).Equals(From, 0.01));
	TestTrue(TEXT("arc ends at the landing point"), FLureFishingRules::CastArcPoint(From, To, 200.f, 1.f).Equals(To, 0.01));
	TestNearlyEqual(TEXT("arc apex in the middle"), static_cast<float>(FLureFishingRules::CastArcPoint(From, To, 200.f, 0.5f).Z), 200.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingCastLandsAtChargedDistance, "Project.Fishing.Cast.LandsAtChargedDistance", LureFishingTest::TestFlags)
bool FLureFishingCastLandsAtChargedDistance::RunTest(const FString& Parameters)
{
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!TestNotNull(TEXT("character with a fishing component"), Fishing))
	{
		return false;
	}
	World.Tick(10);
	const FLureFishingRow Profile = QuickProfile();
	Fishing->SetFishingProfile(Profile);

	for (const float Charge : { 0.f, 0.5f, 1.f })
	{
		const FVector Eye = Character->GetPawnViewLocation();
		TestTrue(FString::Printf(TEXT("charge %.1f: cast starts"), Charge), Fishing->AuthorityCast(Charge, 0.f));
		const FLureFishingNetState& State = Fishing->GetNetState();
		const float Expected = FLureFishingRules::CastDistance(Profile, Charge);
		const float Actual = static_cast<float>(FVector::Dist2D(Eye, State.BobberRest));
		TestNearlyEqual(FString::Printf(TEXT("charge %.1f: lands %.0f cm out (got %.1f)"), Charge, Expected, Actual), Actual, Expected, 1.f);
		TestTrue(FString::Printf(TEXT("charge %.1f: straight ahead (+X)"), Charge), FMath::Abs(State.BobberRest.Y) < 1.f && State.BobberRest.X > Eye.X);
		TestTrue(FString::Printf(TEXT("charge %.1f: on the water"), Charge), State.bOnWater);
		TestNearlyEqual(FString::Printf(TEXT("charge %.1f: rests on the water surface"), Charge), static_cast<float>(State.BobberRest.Z), 0.f, 0.5f);
		TestEqual(TEXT("state Casting"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Casting));
		TestNearlyEqual(TEXT("flight time from data"), State.FlightTime, FLureFishingRules::CastFlightTime(Profile, static_cast<float>(FVector::Dist(State.CastOrigin, State.BobberRest))), 0.001f);
		Fishing->AuthorityReelIn();
		TestEqual(TEXT("reeled in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
	}

	// Aim: the yaw picks the direction.
	const FVector Eye = Character->GetPawnViewLocation();
	TestTrue(TEXT("cast aimed at yaw 30"), Fishing->AuthorityCast(1.f, 30.f));
	const FVector Direction = (Fishing->GetNetState().BobberRest - Eye).GetSafeNormal2D();
	TestNearlyEqual(TEXT("aim yaw 30 degrees"), static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X))), 30.f, 0.5f);
	Fishing->AuthorityReelIn();

	// A cast onto a platform lands on land: nothing will bite there.
	World.AddBox(FVector(1400.f, 0.f, 25.f), FVector(300.f, 300.f, 25.f)); // a rock 50 cm above the water, 11-17 m out
	TestTrue(TEXT("cast at the rock"), Fishing->AuthorityCast(0.5f, 0.f));
	TestFalse(TEXT("on the rock: not on water"), Fishing->GetNetState().bOnWater);
	TestNearlyEqual(TEXT("rests on the rock top"), static_cast<float>(Fishing->GetNetState().BobberRest.Z), 50.f, 1.f);
	TestTrue(TEXT("lands"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	TestTrue(TEXT("no bite scheduled on land"), Fishing->GetScheduledBiteTime() < 0.0);
	TestTrue(TEXT("HUD: the bobber is on land"), Fishing->GetStatusText().Contains(TEXT("on land")));
	Fishing->PressHook();
	TestEqual(TEXT("a press on land reels in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));

	// A wall in the way: the bobber lands in front of it.
	World.AddBox(FVector(1000.f, 0.f, 300.f), FVector(20.f, 600.f, 400.f));
	TestTrue(TEXT("cast into a wall"), Fishing->AuthorityCast(1.f, 0.f));
	TestTrue(TEXT("stops before the wall"), Fishing->GetNetState().BobberRest.X < 980.f);
	return true;
}

// =====================================================================================================================
// Bite and hook window
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingHookWindowFromData, "Project.Fishing.Hook.WindowFromData", LureFishingTest::TestFlags)
bool FLureFishingHookWindowFromData::RunTest(const FString& Parameters)
{
	// Pure rule.
	FLureFishingRow Row = QuickProfile(0.8f);
	TestTrue(TEXT("at the bite"), FLureFishingRules::IsInHookWindow(Row, 10.0, 10.0, 0.f));
	TestTrue(TEXT("just inside the window"), FLureFishingRules::IsInHookWindow(Row, 10.0, 10.79, 0.f));
	TestFalse(TEXT("just after the window"), FLureFishingRules::IsInHookWindow(Row, 10.0, 10.81, 0.f));
	TestTrue(TEXT("remote grace extends it"), FLureFishingRules::IsInHookWindow(Row, 10.0, 10.9, 0.15f));
	TestFalse(TEXT("before the bite"), FLureFishingRules::IsInHookWindow(Row, 10.0, 9.9, 0.f));

	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	// Two different windows from data: hooking just inside works, and the bite is lost just after.
	for (const float Window : { 0.5f, 1.2f })
	{
		for (const bool bHookInTime : { true, false })
		{
			FWorld World;
			if (!World.Create(*this))
			{
				return false;
			}
			World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200") });
			ALurePlayerCharacter* Character = World.Spawn(StandAt);
			ULureFishingComponent* Fishing = SetUpFishing(Character, Tables, QuickProfile(Window));
			if (!TestNotNull(TEXT("fishing"), Fishing) || !CastAndLand(*this, World, Fishing))
			{
				return false;
			}
			const FString Label = FString::Printf(TEXT("window %.1f s, %s"), Window, bHookInTime ? TEXT("hook in time") : TEXT("no hook"));
			if (!TestTrue(Label + TEXT(": a fish bites"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120)))
			{
				continue;
			}
			const double BiteStart = Fishing->GetNetState().StateStartTime;
			TestTrue(Label + TEXT(": the server holds a rolled fish"), Fishing->GetPendingFish().IsValid());
			TestEqual(Label + TEXT(": no grace in standalone"), Fishing->GetHookGrace(), 0.f);
			if (bHookInTime)
			{
				World.TickUntil([Fishing, BiteStart, Window]() { return Fishing->GetFishingTime() - BiteStart >= Window - 0.1f; }, 240);
				TestEqual(Label + TEXT(": still biting just before the window closes"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Biting));
				const FFishInstance Pending = Fishing->GetPendingFish();
				Fishing->AuthorityHook();
				TestEqual(Label + TEXT(": hooked"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
				TestEqual(Label + TEXT(": result Hooked"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Hooked));
				TestEqual(Label + TEXT(": the hooked fish is the one that bit"), Fishing->GetHookedFish().Seed, Pending.Seed);
				TestEqual(Label + TEXT(": same species"), Fishing->GetHookedFish().SpeciesId, Pending.SpeciesId);
				TestTrue(Label + TEXT(": HUD shows the hooked fish"), Fishing->GetStatusText().Contains(TEXT("Hooked:")));
			}
			else
			{
				World.TickUntil([Fishing]() { return Fishing->GetFishingState() != ELureFishingState::Biting; }, 240);
				const double Elapsed = Fishing->GetFishingTime() - BiteStart;
				TestTrue(FString::Printf(TEXT("%s: the window lasted the data's %.2f s (closed after %.3f s)"), *Label, Window, Elapsed), Elapsed >= Window - 0.001 && Elapsed <= Window + 2.5 * Dt);
				TestEqual(Label + TEXT(": result Missed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Missed));
				TestTrue(Label + TEXT(": HUD says missed"), Fishing->GetStatusText().Contains(TEXT("Missed")));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingMissLosesTheBite, "Project.Fishing.Hook.MissLosesTheBite", LureFishingTest::TestFlags)
bool FLureFishingMissLosesTheBite::RunTest(const FString& Parameters)
{
	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	for (const bool bMissEndsCast : { false, true })
	{
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200") });
		ALurePlayerCharacter* Character = World.Spawn(StandAt);
		FLureFishingRow Profile = QuickProfile(0.4f);
		Profile.MissEndsCast = bMissEndsCast;
		ULureFishingComponent* Fishing = SetUpFishing(Character, Tables, Profile);
		if (!Fishing || !CastAndLand(*this, World, Fishing))
		{
			return false;
		}
		const FString Label = bMissEndsCast ? TEXT("MissEndsCast") : TEXT("keep waiting");
		if (!TestTrue(Label + TEXT(": bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120)))
		{
			return false;
		}
		const int32 FirstSeed = Fishing->GetPendingFish().Seed;
		World.TickUntil([Fishing]() { return Fishing->GetFishingState() != ELureFishingState::Biting; }, 120);
		TestEqual(Label + TEXT(": missed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Missed));
		TestFalse(Label + TEXT(": the missed fish is gone on the server"), Fishing->GetPendingFish().IsValid());
		TestFalse(Label + TEXT(": nothing is hooked"), Fishing->GetHookedFish().IsValid());

		// A late press can't bring the fish back.
		Fishing->AuthorityHook();
		TestNotEqual(Label + TEXT(": a late press does not hook"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
		TestFalse(Label + TEXT(": still no fish after the late press"), Fishing->GetHookedFish().IsValid());

		if (bMissEndsCast)
		{
			TestEqual(Label + TEXT(": the line came in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
		}
		else
		{
			// The late press was an early hook for the NEXT bite (EarlyHook = ReelIn): recast and wait for a new bite instead.
			Fishing->AuthorityReelIn();
			CastAndLand(*this, World, Fishing);
			TestTrue(Label + TEXT(": a new bite comes"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120));
			TestNotEqual(Label + TEXT(": the new bite is a new roll (new seed)"), Fishing->GetPendingFish().Seed, FirstSeed);

			// And with the bobber left in the water after a miss, a rebite comes after RebiteWait.
			World.TickUntil([Fishing]() { return Fishing->GetFishingState() != ELureFishingState::Biting; }, 120);
			TestEqual(Label + TEXT(": waiting again after the miss"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
			TestTrue(Label + TEXT(": a rebite is scheduled"), Fishing->GetScheduledBiteTime() > Fishing->GetFishingTime());
			TestTrue(Label + TEXT(": the rebite comes"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingEarlyHookRules, "Project.Fishing.Hook.EarlyHookRules", LureFishingTest::TestFlags)
bool FLureFishingEarlyHookRules::RunTest(const FString& Parameters)
{
	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	for (const ELureEarlyHookRule Rule : { ELureEarlyHookRule::ReelIn, ELureEarlyHookRule::Ignore, ELureEarlyHookRule::Spook })
	{
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200") });
		FLureFishingRow Profile = QuickProfile();
		Profile.BiteWaitMin = Profile.BiteWaitMax = 1.f;
		Profile.EarlyHook = Rule;
		Profile.SpookDelay = 3.f;
		ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt), Tables, Profile);
		if (!Fishing || !CastAndLand(*this, World, Fishing))
		{
			return false;
		}
		const FString Label = StaticEnum<ELureEarlyHookRule>()->GetNameStringByValue(static_cast<int64>(Rule));
		const double BiteBefore = Fishing->GetScheduledBiteTime();
		Fishing->AuthorityHook();
		switch (Rule)
		{
		case ELureEarlyHookRule::ReelIn:
			TestEqual(Label + TEXT(": an early press reels in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
			break;
		case ELureEarlyHookRule::Ignore:
			TestEqual(Label + TEXT(": nothing happens"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
			TestEqual(Label + TEXT(": bite time unchanged"), Fishing->GetScheduledBiteTime(), BiteBefore);
			break;
		case ELureEarlyHookRule::Spook:
			TestEqual(Label + TEXT(": still waiting"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
			TestTrue(Label + TEXT(": the bite comes at least SpookDelay later"), Fishing->GetScheduledBiteTime() >= Fishing->GetFishingTime() + 3.0 - 0.001);
			TestEqual(Label + TEXT(": result Spooked"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Spooked));
			break;
		}
	}
	return true;
}

// =====================================================================================================================
// Rules: sprinting, swimming, prone
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingNoFishingWhileSprintingOrSwimming, "Project.Fishing.Rules.NoFishingWhileSprintingOrSwimming", LureFishingTest::TestFlags)
bool FLureFishingNoFishingWhileSprintingOrSwimming::RunTest(const FString& Parameters)
{
	// Pure: the shipped DT_Movement rows.
	const TArray<FLureMovementRow> Rows = MovementRows(MovementTable(*this));
	FLureCastConditions Still;
	TestEqual(TEXT("standing still can cast"), BlockName(FLureFishingRules::GetCastBlock(Still, RowOf(Rows, ELureMovementState::Stand))), BlockName(ELureCastBlock::None));
	TestEqual(TEXT("crouched can cast"), BlockName(FLureFishingRules::GetCastBlock(Still, RowOf(Rows, ELureMovementState::Crouch))), BlockName(ELureCastBlock::None));
	FLureCastConditions Walking = Still;
	Walking.Speed2D = 300.f;
	TestEqual(TEXT("walking can cast"), BlockName(FLureFishingRules::GetCastBlock(Walking, RowOf(Rows, ELureMovementState::Stand))), BlockName(ELureCastBlock::None));
	TestEqual(TEXT("sprinting can't (Sprint row CanFish = False)"), BlockName(FLureFishingRules::GetCastBlock(Walking, RowOf(Rows, ELureMovementState::Sprint))), BlockName(ELureCastBlock::Sprinting));
	FLureCastConditions Swimming = Still;
	Swimming.bSwimming = true;
	TestEqual(TEXT("swimming can't"), BlockName(FLureFishingRules::GetCastBlock(Swimming, RowOf(Rows, ELureMovementState::Stand))), BlockName(ELureCastBlock::Swimming));
	FLureCastConditions Falling = Still;
	Falling.bFalling = true;
	TestEqual(TEXT("in the air can't"), BlockName(FLureFishingRules::GetCastBlock(Falling, RowOf(Rows, ELureMovementState::Stand))), BlockName(ELureCastBlock::InAir));
	FLureCastConditions NoRod = Still;
	NoRod.bHasRod = false;
	TestEqual(TEXT("no rod can't"), BlockName(FLureFishingRules::GetCastBlock(NoRod, RowOf(Rows, ELureMovementState::Stand))), BlockName(ELureCastBlock::NoRod));
	const FLureFishingRow Profile = FLureFishingRules::GetFallbackRow();
	TestEqual(TEXT("a line out comes in when sprinting"), BlockName(FLureFishingRules::GetLineCancel(Walking, RowOf(Rows, ELureMovementState::Sprint), Profile, 100.f)), BlockName(ELureCastBlock::Sprinting));
	TestEqual(TEXT("a line out comes in when swimming"), BlockName(FLureFishingRules::GetLineCancel(Swimming, RowOf(Rows, ELureMovementState::Stand), Profile, 100.f)), BlockName(ELureCastBlock::Swimming));
	TestEqual(TEXT("a jump keeps the line out"), BlockName(FLureFishingRules::GetLineCancel(Falling, RowOf(Rows, ELureMovementState::Stand), Profile, 100.f)), BlockName(ELureCastBlock::None));
	TestEqual(TEXT("too far from the bobber"), BlockName(FLureFishingRules::GetLineCancel(Still, RowOf(Rows, ELureMovementState::Stand), Profile, Profile.MaxLineLength + 1.f)), BlockName(ELureCastBlock::TooFar));

	// In a world: the server refuses and cancels.
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(FVector(0.f, 0.f, DockTop));
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!TestNotNull(TEXT("fishing"), Fishing))
	{
		return false;
	}
	Fishing->SetFishingProfile(QuickProfile());
	World.Tick(10);
	Character->SetSprintRequested(true);
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Character->AddMovementInput(FVector::RightVector, 1.f, true);
		World.Tick(1);
	}
	TestTrue(TEXT("sprinting"), Character->IsSprinting());
	TestEqual(TEXT("cast block while sprinting"), BlockName(Fishing->GetCastBlock()), BlockName(ELureCastBlock::Sprinting));
	TestFalse(TEXT("the server refuses a cast while sprinting"), Fishing->AuthorityCast(1.f, 0.f));
	TestEqual(TEXT("result Refused"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Refused));
	TestEqual(TEXT("reason Sprinting"), BlockName(Fishing->GetNetState().ResultReason), BlockName(ELureCastBlock::Sprinting));
	TestTrue(TEXT("HUD: can't cast while sprinting"), Fishing->GetStatusText().Contains(TEXT("sprinting")));
	Character->SetSprintRequested(false);
	World.Tick(60);

	// A line out comes in when you start to sprint.
	Character->SetActorLocation(StandAt + FVector(0.f, 0.f, Character->GetSimpleCollisionHalfHeight() + 2.f));
	World.Tick(10);
	TestTrue(TEXT("cast when walking stops"), Fishing->AuthorityCast(0.5f, 0.f));
	Character->SetSprintRequested(true);
	for (int32 Frame = 0; Frame < 20 && Fishing->IsLineOut(); ++Frame)
	{
		Character->AddMovementInput(-FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	TestFalse(TEXT("sprinting brings the line in"), Fishing->IsLineOut());
	TestEqual(TEXT("reeled in because of sprinting"), BlockName(Fishing->GetNetState().ResultReason), BlockName(ELureCastBlock::Sprinting));
	Character->SetSprintRequested(false);
	World.Tick(30);

	// Swimming (stub: the engine's swimming mode; T-026 adds water volumes).
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Swimming);
	TestEqual(TEXT("cast block while swimming"), BlockName(Fishing->GetCastBlock()), BlockName(ELureCastBlock::Swimming));
	TestFalse(TEXT("no rod in hand while swimming"), Fishing->IsRodInHand());
	TestFalse(TEXT("the server refuses a cast while swimming"), Fishing->AuthorityCast(1.f, 0.f));
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingProneStillCastsProneMovingDoesNot, "Project.Fishing.Rules.ProneStillCastsProneMovingDoesNot", LureFishingTest::TestFlags)
bool FLureFishingProneStillCastsProneMovingDoesNot::RunTest(const FString& Parameters)
{
	const TArray<FLureMovementRow> Rows = MovementRows(MovementTable(*this));
	const FLureMovementRow& Prone = RowOf(Rows, ELureMovementState::Prone);
	FLureCastConditions Conditions;
	TestEqual(TEXT("pure: prone and still can cast"), BlockName(FLureFishingRules::GetCastBlock(Conditions, Prone)), BlockName(ELureCastBlock::None));
	Conditions.Speed2D = Prone.RodMoveSpeedIn + 5.f;
	TestEqual(TEXT("pure: prone and crawling can't (tucked rod)"), BlockName(FLureFishingRules::GetCastBlock(Conditions, Prone)), BlockName(ELureCastBlock::RodTucked));
	TestEqual(TEXT("pure: crouch-walking can"), BlockName(FLureFishingRules::GetCastBlock(Conditions, RowOf(Rows, ELureMovementState::Crouch))), BlockName(ELureCastBlock::None));

	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!TestNotNull(TEXT("fishing"), Fishing))
	{
		return false;
	}
	Fishing->SetFishingProfile(QuickProfile());
	World.Tick(10);
	Character->RequestStance(ELureStance::Prone);
	World.Tick(40);
	if (!TestEqual(TEXT("prone"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Prone)))
	{
		return false;
	}
	TestEqual(TEXT("prone and still: no block"), BlockName(Fishing->GetCastBlock()), BlockName(ELureCastBlock::None));
	TestTrue(TEXT("prone and still: the server accepts a cast"), Fishing->AuthorityCast(0.5f, 0.f));
	World.Tick(30);
	TestTrue(TEXT("the line stays out while lying still"), Fishing->IsLineOut());

	// Crawling with the line out: the rod tucks, so the line comes in.
	for (int32 Frame = 0; Frame < 30 && Fishing->IsLineOut(); ++Frame)
	{
		Character->AddMovementInput(-FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	TestFalse(TEXT("crawling brings the line in"), Fishing->IsLineOut());
	TestEqual(TEXT("reason: crawling (rod tucked)"), BlockName(Fishing->GetNetState().ResultReason), BlockName(ELureCastBlock::RodTucked));

	// Crawling: no cast.
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Character->AddMovementInput(-FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	Character->AddMovementInput(-FVector::ForwardVector, 1.f, true);
	World.Tick(1);
	TestTrue(TEXT("crawling"), Character->GetVelocity().Size2D() > Prone.RodMoveSpeedIn);
	TestEqual(TEXT("crawling: blocked"), BlockName(Fishing->GetCastBlock()), BlockName(ELureCastBlock::RodTucked));
	TestFalse(TEXT("crawling: the server refuses"), Fishing->AuthorityCast(0.5f, 0.f));
	return true;
}

// =====================================================================================================================
// Fishing spots: marker tags, habitat and luck into the roll
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingSpotTagsParse, "Project.Fishing.Spot.TagsParse", LureFishingTest::TestFlags)
bool FLureFishingSpotTagsParse::RunTest(const FString& Parameters)
{
	// Exactly what Content/Python/levels/build_level.py marker_tags() writes for the Hidden Cove (L_PalmKey.md section 11).
	const TArray<FName> Tags = { TEXT("LureLayout"), TEXT("LureLayout=L_PalmKey"), TEXT("LureId=hidden_cove"), TEXT("Lure.FishingSpot"), TEXT("Spot=hidden_cove"),
		TEXT("Habitat=Habitat.Shore.Cove"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=500"), TEXT("Hours=0-24"), TEXT("Levels=2-4"),
		TEXT("Luck=0.5"), TEXT("Danger=none"), TEXT("CastFrom=4900,-1600,80"), TEXT("Name=Hidden Cove") };
	FLureFishingSpot Spot;
	TArray<FString> Problems;
	TestTrue(TEXT("parses"), FLureFishingSpots::ParseSpotTags(Tags, TEXT("Lure.FishingSpot"), Spot, &Problems));
	TestEqual(FString::Printf(TEXT("no problems (%s)"), *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
	TestEqual(TEXT("Spot"), Spot.SpotId, FName(TEXT("hidden_cove")));
	TestEqual(TEXT("Name"), Spot.DisplayName, FString(TEXT("Hidden Cove")));
	TestTrue(TEXT("Habitat"), Spot.HabitatTag == Tag(TEXT("Habitat.Shore.Cove")) && Spot.HabitatTag.IsValid());
	TestTrue(TEXT("Region"), Spot.RegionTag == Tag(TEXT("Region.Tropical.PalmKey")) && Spot.RegionTag.IsValid());
	TestNearlyEqual(TEXT("Radius"), Spot.Radius, 500.f, 0.001f);
	TestNearlyEqual(TEXT("Luck"), Spot.Luck, 0.5f, 0.001f);
	TestTrue(TEXT("Hours 0-24"), Spot.Hours.Num() == 1 && FMath::IsNearlyEqual(Spot.Hours[0].StartHour, 0.f) && FMath::IsNearlyEqual(Spot.Hours[0].EndHour, 24.f));
	TestTrue(TEXT("Levels 2-4"), Spot.LevelMin == 2 && Spot.LevelMax == 4);
	TestEqual(TEXT("Danger"), Spot.Danger, FName(TEXT("none")));
	TestTrue(TEXT("CastFrom"), Spot.CastFrom.Equals(FVector(4900.f, -1600.f, 80.f), 0.01));

	FLureFishingSpot Night;
	TestTrue(TEXT("two hour windows"), FLureFishingSpots::ParseSpotTags({ TEXT("Lure.FishingSpot"), TEXT("Radius=450"), TEXT("Hours=20-5;6-7"), TEXT("Levels=") }, TEXT("Lure.FishingSpot"), Night)
		&& Night.Hours.Num() == 2 && FMath::IsNearlyEqual(Night.Hours[0].StartHour, 20.f) && FMath::IsNearlyEqual(Night.Hours[1].EndHour, 7.f));
	TestNearlyEqual(TEXT("no Luck tag = 0"), Night.Luck, 0.f, 0.f);

	FLureFishingSpot Bad;
	TestFalse(TEXT("not tagged Lure.FishingSpot: not a spot"), FLureFishingSpots::ParseSpotTags({ TEXT("Radius=500"), TEXT("Habitat=Habitat.Shore") }, TEXT("Lure.FishingSpot"), Bad));
	TArray<FString> NoRadius;
	TestFalse(TEXT("no Radius: not a spot"), FLureFishingSpots::ParseSpotTags({ TEXT("Lure.FishingSpot"), TEXT("Habitat=Habitat.Shore") }, TEXT("Lure.FishingSpot"), Bad, &NoRadius));
	TestTrue(TEXT("no Radius is reported"), NoRadius.Num() > 0);
	TArray<FString> Soft;
	TestTrue(TEXT("an unregistered habitat still parses"), FLureFishingSpots::ParseSpotTags({ TEXT("Lure.FishingSpot"), TEXT("Radius=100"), TEXT("Habitat=Habitat.NotARealTag"), TEXT("Luck=lots") }, TEXT("Lure.FishingSpot"), Bad, &Soft));
	TestFalse(TEXT("... with no habitat"), Bad.HabitatTag.IsValid());
	TestEqual(TEXT("... and two problems reported (habitat, luck)"), Soft.Num(), 2);
	TestTrue(TEXT("keys are case-insensitive"), FLureFishingSpots::ParseSpotTags({ TEXT("Lure.FishingSpot"), TEXT("radius=100"), TEXT("LUCK=1.5") }, TEXT("Lure.FishingSpot"), Bad) && FMath::IsNearlyEqual(Bad.Luck, 1.5f));

	// In a world: the spot you are deepest in wins; outside every radius there is none.
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(1000.f, 0.f, 0.f), { TEXT("Spot=big"), TEXT("Habitat=Habitat.Reef"), TEXT("Radius=800") });
	World.AddSpot(FVector(1300.f, 0.f, 0.f), { TEXT("Spot=small"), TEXT("Habitat=Habitat.Reef.Edge"), TEXT("Radius=200") });
	const FName SpotTag = GetDefault<ULureFishingSettings>()->FishingSpotTag;
	FLureFishingSpot Found;
	TestTrue(TEXT("inside the big spot"), FLureFishingSpots::FindSpotAt(World.World, FVector(700.f, 0.f, 0.f), SpotTag, Found) && Found.SpotId == TEXT("big"));
	TestTrue(TEXT("deep inside the small spot (also inside the big one)"), FLureFishingSpots::FindSpotAt(World.World, FVector(1290.f, 10.f, 0.f), SpotTag, Found) && Found.SpotId == TEXT("small"));
	TestFalse(TEXT("outside both"), FLureFishingSpots::FindSpotAt(World.World, FVector(3000.f, 0.f, 0.f), SpotTag, Found));
	TestEqual(TEXT("two markers gathered"), FLureFishingSpots::GatherSpots(World.World, SpotTag).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingSpotHabitatAndLuckReachTheRoll, "Project.Fishing.Spot.HabitatAndLuckReachTheRoll", LureFishingTest::TestFlags)
bool FLureFishingSpotHabitatAndLuckReachTheRoll::RunTest(const FString& Parameters)
{
	// Pure: the context is the spot's habitat and region, and its luck plus gear luck.
	FLureFishingSpot Spot;
	Spot.Radius = 500.f;
	Spot.HabitatTag = Tag(TEXT("Habitat.Shore.Cove"));
	Spot.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
	Spot.Luck = 0.5f;
	FLureFishingEnvironment Environment;
	Environment.TimeOfDayHours = 6.f;
	Environment.GearLuck = 0.25f;
	Environment.DefaultRegionTag = Tag(TEXT("Region.Tropical"));
	const FFishRollContext Context = FLureFishingRules::MakeRollContext(&Spot, Environment, 77);
	TestTrue(TEXT("habitat from the spot"), Context.HabitatTag == Spot.HabitatTag);
	TestTrue(TEXT("region from the spot"), Context.RegionTag == Spot.RegionTag);
	TestNearlyEqual(TEXT("luck = spot 0.5 + gear 0.25"), Context.Luck, 0.75f, 0.0001f);
	TestNearlyEqual(TEXT("time of day"), Context.TimeOfDayHours, 6.f, 0.0001f);
	TestEqual(TEXT("seed"), Context.Seed, 77);
	const FFishRollContext OffSpot = FLureFishingRules::MakeRollContext(nullptr, Environment, 1);
	TestFalse(TEXT("no spot: no habitat (nothing bites off-spot by default)"), OffSpot.HabitatTag.IsValid());
	TestTrue(TEXT("no spot: the default region"), OffSpot.RegionTag == Environment.DefaultRegionTag);
	TestNearlyEqual(TEXT("no spot: gear luck only"), OffSpot.Luck, 0.25f, 0.0001f);
	TestFalse(TEXT("no spot, no off-spot habitat: no bites"), FLureFishingRules::CanHaveBites(nullptr, Environment));
	Environment.OffSpotHabitatTag = Tag(TEXT("Habitat.Shore"));
	TestTrue(TEXT("an off-spot habitat in the settings allows bites"), FLureFishingRules::CanHaveBites(nullptr, Environment));

	// Luck changes the rarity roll (same seed, more luck -> rarer tiers more often).
	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FLureFishingSpot Shore;
	Shore.Radius = 500.f;
	Shore.HabitatTag = Tag(TEXT("Habitat.Shore"));
	Shore.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
	FLureFishingEnvironment Noon;
	Noon.TimeOfDayHours = 12.f;
	Noon.BaitTag = Tag(TEXT("Bait.Shrimp"));
	int32 RareNoLuck = 0;
	int32 RareLucky = 0;
	int32 Differ = 0;
	for (int32 Seed = 0; Seed < 2000; ++Seed)
	{
		Shore.Luck = 0.f;
		FFishInstance Plain;
		FLureFishingRules::DecideBite(Tables.Get(), FLureFishingRules::MakeRollContext(&Shore, Noon, Seed), Plain);
		Shore.Luck = 4.f;
		FFishInstance Lucky;
		FLureFishingRules::DecideBite(Tables.Get(), FLureFishingRules::MakeRollContext(&Shore, Noon, Seed), Lucky);
		RareNoLuck += Plain.RarityId != TEXT("Common") ? 1 : 0;
		RareLucky += Lucky.RarityId != TEXT("Common") ? 1 : 0;
		Differ += Plain.RarityId != Lucky.RarityId ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("spot luck raises rarity (%d vs %d of 2000 non-common)"), RareLucky, RareNoLuck), RareLucky > RareNoLuck + 100);
	TestTrue(FString::Printf(TEXT("with the same seed, luck changes the rarity of some bites (%d of 2000)"), Differ), Differ > 0);

	// In a world: markers decide the habitat and luck of the bite the server rolls.
	struct FCase
	{
		const TCHAR* Habitat;
		float Hours;
		float Luck;
		const TCHAR* ExpectSpecies; // nullptr = nothing bites
	};
	const FCase Cases[] = {
		{ TEXT("Habitat.Shore.Cove"), 12.f, 0.5f, TEXT("Bonefish") },   // cove = a Shore child: shore fish, with the cove's luck
		{ TEXT("Habitat.Reef"), 20.f, 0.f, TEXT("CoralSnapper") },       // reef at night: the snapper (15-09)
		{ TEXT("Habitat.Reef"), 12.f, 0.f, nullptr },                    // reef at noon: nothing fits
	};
	for (const FCase& Case : Cases)
	{
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=case"), FString(TEXT("Habitat=")) + Case.Habitat, TEXT("Region=Region.Tropical.PalmKey"),
			TEXT("Radius=1200"), FString::Printf(TEXT("Luck=%.2f"), Case.Luck) });
		ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt), Tables, QuickProfile(), Case.Hours);
		if (!Fishing || !CastAndLand(*this, World, Fishing))
		{
			return false;
		}
		const FString Label = FString::Printf(TEXT("%s at %.0f:00"), Case.Habitat, Case.Hours);
		TestTrue(Label + TEXT(": the bobber is in the marker's spot"), Fishing->HasCurrentSpot() && Fishing->GetNetState().SpotId == TEXT("case"));
		World.Tick(30); // past the 0.2 s bite wait
		const FFishRollContext& Rolled = Fishing->GetLastRollContext();
		TestTrue(Label + TEXT(": the roll used the marker's habitat"), Rolled.HabitatTag == Tag(Case.Habitat));
		TestTrue(Label + TEXT(": ... and region"), Rolled.RegionTag == Tag(TEXT("Region.Tropical.PalmKey")));
		TestNearlyEqual(Label + TEXT(": ... and luck"), Rolled.Luck, Case.Luck, 0.001f);
		if (Case.ExpectSpecies)
		{
			TestEqual(Label + TEXT(": biting"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Biting));
			TestEqual(Label + TEXT(": species"), Fishing->GetPendingFish().SpeciesId, FName(Case.ExpectSpecies));
			// The bite is the one roll pipeline: the same context rolls the same fish.
			FFishInstance Again;
			TestTrue(Label + TEXT(": re-roll"), FLureFishingRules::DecideBite(Tables.Get(), Rolled, Again));
			TestTrue(Label + TEXT(": same fish from the same context"), Again.Seed == Fishing->GetPendingFish().Seed && Again.RarityId == Fishing->GetPendingFish().RarityId
				&& FMath::IsNearlyEqual(Again.WeightKg, Fishing->GetPendingFish().WeightKg));
		}
		else
		{
			TestEqual(Label + TEXT(": nothing bites"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
			TestTrue(Label + TEXT(": nothing-here flag"), Fishing->GetNetState().bNoFishHere);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingNoSpotNoBite, "Project.Fishing.Spot.NoSpotNoBite", LureFishingTest::TestFlags)
bool FLureFishingNoSpotNoBite::RunTest(const FString& Parameters)
{
	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(5000.f, 5000.f, 0.f), { TEXT("Spot=far_away"), TEXT("Habitat=Habitat.Shore"), TEXT("Radius=300") });
	FLureFishingRow Profile = QuickProfile();
	Profile.NoBiteHintDelay = 1.f;
	ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt), Tables, Profile);
	if (!Fishing || !CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestFalse(TEXT("open water, no spot"), Fishing->HasCurrentSpot());
	TestTrue(TEXT("nothing-here flag"), Fishing->GetNetState().bNoFishHere);
	TestTrue(TEXT("no bite scheduled"), Fishing->GetScheduledBiteTime() < 0.0);
	TestTrue(TEXT("no hint before NoBiteHintDelay"), Fishing->GetStatusText().IsEmpty());
	World.Tick(90);
	TestEqual(TEXT("still waiting, no bite"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
	TestTrue(TEXT("HUD: nothing is biting here"), Fishing->GetStatusText().Contains(TEXT("Nothing is biting here")));
	Fishing->AuthorityHook();
	TestEqual(TEXT("a press reels in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
	return true;
}

// =====================================================================================================================
// Networking: the server decides, clients follow the replicated state
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingCastStateIsReplicated, "Project.Fishing.Net.CastStateIsReplicated", LureFishingTest::TestFlags)
bool FLureFishingCastStateIsReplicated::RunTest(const FString& Parameters)
{
	// Reflection: what goes over the wire.
	UClass* Class = ULureFishingComponent::StaticClass();
	const FProperty* NetStateProperty = Class->FindPropertyByName(TEXT("NetState"));
	if (!TestNotNull(TEXT("NetState property"), NetStateProperty))
	{
		return false;
	}
	TestTrue(TEXT("NetState replicates"), NetStateProperty->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("NetState has a RepNotify"), NetStateProperty->HasAnyPropertyFlags(CPF_RepNotify) && NetStateProperty->RepNotifyFunc == TEXT("OnRep_NetState"));
	for (const TCHAR* Name : { TEXT("HookedFish"), TEXT("LastLandedFish") })
	{
		const FProperty* Property = Class->FindPropertyByName(Name);
		TestTrue(FString::Printf(TEXT("%s replicates"), Name), Property && Property->HasAnyPropertyFlags(CPF_Net));
	}
	for (const TCHAR* Name : { TEXT("ServerCast"), TEXT("ServerHook"), TEXT("ServerReelIn") })
	{
		const UFunction* Function = Class->FindFunctionByName(Name);
		TestTrue(FString::Printf(TEXT("%s is a reliable server RPC"), Name), Function && Function->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
	}
	TestTrue(TEXT("the fishing component replicates"), GetDefault<ULureFishingComponent>()->GetIsReplicated());
	const ALurePlayerCharacter* Defaults = GetDefault<ALurePlayerCharacter>();
	TestTrue(TEXT("every player character has one"), Defaults->GetFishing() != nullptr && Defaults->GetFishing()->GetIsReplicated());

	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* ServerCharacter = World.Spawn(StandAt);
	ALurePlayerCharacter* ClientCharacter = World.Spawn(StandAt + FVector(0.f, 200.f, 0.f));
	ULureFishingComponent* Server = ServerCharacter ? ServerCharacter->GetFishing() : nullptr;
	ULureFishingComponent* Client = ClientCharacter ? ClientCharacter->GetFishing() : nullptr;
	if (!TestNotNull(TEXT("server fishing"), Server) || !TestNotNull(TEXT("client fishing"), Client))
	{
		return false;
	}
	World.Tick(10);
	Server->SetFishingProfile(QuickProfile());
	Client->SetFishingProfile(QuickProfile());

	// A client's copy of a character has no authority: its requests only ask the server (without a connection they go nowhere).
	ClientCharacter->SetRole(ROLE_AutonomousProxy);
	Client->PressCast();
	Client->ReleaseCast();
	TestEqual(TEXT("client: a cast request does not change the state by itself"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Idle));
	TestFalse(TEXT("client: can't decide a cast"), Client->AuthorityCast(1.f, 0.f));
	TestEqual(TEXT("client: still idle"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Idle));

	// The server decides and its state replicates (copy the replicated properties + RepNotify, as the net driver does).
	TestTrue(TEXT("server: cast"), Server->AuthorityCast(0.5f, 0.f));
	auto Replicate = [Server, Client]()
	{
		const FLureFishingNetState Previous = Client->GetNetState();
		for (TFieldIterator<FProperty> It(ULureFishingComponent::StaticClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Net) && It->GetOwnerClass() == ULureFishingComponent::StaticClass())
			{
				It->CopyCompleteValue_InContainer(Client, Server);
			}
		}
		UFunction* OnRep = Client->FindFunction(TEXT("OnRep_NetState"));
		struct { FLureFishingNetState PreviousState; } Params{ Previous };
		Client->ProcessEvent(OnRep, &Params);
	};
	Replicate();
	TestEqual(TEXT("client: follows the server's Casting"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Casting));
	TestTrue(TEXT("client: the same landing point"), Client->GetNetState().BobberRest.Equals(Server->GetNetState().BobberRest, 0.01));
	TestTrue(TEXT("client: draws the bobber where the server's is"), Client->GetBobberLocation().Equals(Server->GetBobberLocation(), 0.5));

	// Time passes: only the server moves on (landing); the client waits for the next update.
	World.TickUntil([Server]() { return Server->GetFishingState() == ELureFishingState::Waiting; }, 240);
	TestEqual(TEXT("server: landed"), StateName(Server->GetFishingState()), StateName(ELureFishingState::Waiting));
	TestEqual(TEXT("client: does not land on its own"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Casting));
	Replicate();
	TestEqual(TEXT("client: waiting after the update"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Waiting));
	Server->AuthorityReelIn();
	Replicate();
	TestEqual(TEXT("client: idle after the server reels in"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Idle));
	ClientCharacter->SetRole(ROLE_Authority);
	return true;
}

// =====================================================================================================================
// Arms pose, line and data
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingArmsPoseByStanceAndMotion, "Project.Fishing.ArmsPose.ByStanceAndMotion", LureFishingTest::TestFlags)
bool FLureFishingArmsPoseByStanceAndMotion::RunTest(const FString& Parameters)
{
	const TArray<FLureMovementRow> Rows = MovementRows(MovementTable(*this));
	const FLureMovementRow& Stand = RowOf(Rows, ELureMovementState::Stand);
	const FLureMovementRow& Prone = RowOf(Rows, ELureMovementState::Prone);
	auto Step = [](FLureRodPoseState& State, const FLureMovementRow& Row, float Speed, float Input = 0.f, bool bHolding = true, bool bBlocked = false, bool bLineOut = false, float DeltaTime = 1.f / 60.f)
	{
		FLureRodPoseInput In;
		In.bHoldingRod = bHolding;
		In.Speed2D = Speed;
		In.MoveInput = Input;
		In.bHoldBlocked = bBlocked;
		In.bLineOut = bLineOut;
		In.DeltaTime = DeltaTime;
		return FLureRodPose::Step(State, Row, In);
	};

	FLureRodPoseState State;
	TestEqual(TEXT("no rod: Idle"), PoseName(Step(State, Stand, 0.f, 0.f, false)), PoseName(EFPArmsPose::Idle));
	TestEqual(TEXT("standing, still: HoldRod"), PoseName(Step(State, Stand, 0.f)), PoseName(EFPArmsPose::HoldRod));
	TestEqual(TEXT("standing, walking: HoldRod"), PoseName(Step(State, Stand, 300.f)), PoseName(EFPArmsPose::HoldRod));
	TestEqual(TEXT("crouched: HoldRod"), PoseName(Step(State, RowOf(Rows, ELureMovementState::Crouch), 0.f)), PoseName(EFPArmsPose::HoldRod));

	FLureRodPoseState ProneState;
	TestEqual(TEXT("prone, still: ProneHold"), PoseName(Step(ProneState, Prone, 0.f)), PoseName(EFPArmsPose::ProneHold));
	TestEqual(TEXT("prone, crawling: ProneTuck at once"), PoseName(Step(ProneState, Prone, Prone.RodMoveSpeedIn + 1.f)), PoseName(EFPArmsPose::ProneTuck));
	TestEqual(TEXT("prone, pushing a wall (input, no speed): still tucked"), PoseName(Step(ProneState, Prone, 0.f, 1.f)), PoseName(EFPArmsPose::ProneTuck));
	// Stopping: the tuck stays for RodStillDelay, then the hold comes back.
	float Elapsed = 0.f;
	EFPArmsPose Pose = EFPArmsPose::ProneTuck;
	while (Pose == EFPArmsPose::ProneTuck && Elapsed < 2.f)
	{
		Pose = Step(ProneState, Prone, 0.f);
		Elapsed += 1.f / 60.f;
	}
	TestNearlyEqual(FString::Printf(TEXT("untucks after RodStillDelay %.2f s (took %.3f s)"), Prone.RodStillDelay, Elapsed), Elapsed, Prone.RodStillDelay, 2.f / 60.f);
	TestEqual(TEXT("prone, stopped: ProneHold"), PoseName(Pose), PoseName(EFPArmsPose::ProneHold));
	TestEqual(TEXT("slow drift below RodMoveSpeedIn stays still"), PoseName(Step(ProneState, Prone, Prone.RodMoveSpeedIn - 1.f)), PoseName(EFPArmsPose::ProneHold));
	TestEqual(TEXT("a wall ahead (no line out): ProneTuck"), PoseName(Step(ProneState, Prone, 0.f, 0.f, true, true, false)), PoseName(EFPArmsPose::ProneTuck));
	TestEqual(TEXT("a wall ahead while fishing: ProneHold"), PoseName(Step(ProneState, Prone, 0.f, 0.f, true, true, true)), PoseName(EFPArmsPose::ProneHold));

	// Arms follow up-pitch: prone 0 (arms stay down), standing 1.
	TestNearlyEqual(TEXT("prone looking up 30: arms counter-pitch -30"), FLureRodPose::ArmsCounterPitch(Prone.ArmsPitchFollowUp, 30.f), -30.f, 0.001f);
	TestNearlyEqual(TEXT("standing looking up: no counter-pitch"), FLureRodPose::ArmsCounterPitch(Stand.ArmsPitchFollowUp, 30.f), 0.f, 0.001f);
	TestNearlyEqual(TEXT("prone looking down: follows fully"), FLureRodPose::ArmsCounterPitch(Prone.ArmsPitchFollowUp, -20.f), 0.f, 0.001f);
	TestNearlyEqual(TEXT("pitch 330 (= -30, looking down): no counter-pitch"), FLureRodPose::ArmsCounterPitch(0.f, 330.f), 0.f, 0.001f);
	FLureRodPoseState Blend;
	Step(Blend, Stand, 0.f);
	Step(Blend, Prone, 0.f, 0.f, true, false, false, 0.5f * Prone.RodPoseBlendTime);
	TestNearlyEqual(TEXT("follow-up eases over RodPoseBlendTime (half way)"), Blend.PitchFollowUp, 0.5f, 0.01f);

	// On a possessed character: the anim instance values come from the character.
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("character"), Character) || !TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.Tick(10);
	TestTrue(TEXT("the rod is in hand by default"), Character->IsHoldingRod());
	TestEqual(TEXT("standing: HoldRod"), PoseName(Character->GetArmsPose()), PoseName(EFPArmsPose::HoldRod));
	Character->RequestStance(ELureStance::Prone);
	World.Tick(40);
	TestEqual(TEXT("prone still: ProneHold"), PoseName(Character->GetArmsPose()), PoseName(EFPArmsPose::ProneHold));
	TestNearlyEqual(TEXT("blend time from the Prone row"), Character->GetArmsPoseBlendTime(), Prone.RodPoseBlendTime, 0.001f);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Character->AddMovementInput(-FVector::ForwardVector, 1.f, true);
		World.Tick(1);
	}
	TestEqual(TEXT("crawling: ProneTuck"), PoseName(Character->GetArmsPose()), PoseName(EFPArmsPose::ProneTuck));
	if (const UFPArmsAnimInstance* Anim = Cast<UFPArmsAnimInstance>(Character->GetFirstPersonArms()->GetAnimInstance()))
	{
		World.Tick(1);
		TestEqual(TEXT("anim instance reads the pose"), PoseName(Anim->ArmsPose), PoseName(Character->GetArmsPose()));
	}
	else
	{
		AddInfo(TEXT("ABP_FPArms is not a UFPArmsAnimInstance here (not imported): anim-instance check skipped."));
	}
	Controller->UnPossess();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingLineAtLeastTwoPixels, "Project.Fishing.Line.AtLeastTwoPixels", LureFishingTest::TestFlags)
bool FLureFishingLineAtLeastTwoPixels::RunTest(const FString& Parameters)
{
	const FLureFishingRow Row = FLureFishingRules::GetFallbackRow();
	const float RefWidth = GetDefault<ULureFishingSettings>()->LineReferenceScreenWidth;
	TestNearlyEqual(TEXT("1080p reference"), RefWidth, 1920.f, 0.f);
	bool bAllWide = true;
	float Thinnest = TNumericLimits<float>::Max();
	for (float Distance = 20.f; Distance <= 8000.f; Distance *= 1.25f)
	{
		for (const float Fov : { 70.f, 90.f, 110.f })
		{
			const float Width = FLureFishingRules::LineWidthAtDistance(Row.LinePixelWidth, Distance, Fov, RefWidth, Row.LineMinWidth);
			const float Pixels = FLureFishingRules::LinePixelsAtDistance(Width, Distance, Fov, RefWidth);
			Thinnest = FMath::Min(Thinnest, Pixels);
			bAllWide &= Pixels >= 2.f - 0.001f;
		}
	}
	TestTrue(FString::Printf(TEXT("the line is >= 2 px at 1080p from 20 cm to 80 m (thinnest %.2f px)"), Thinnest), bAllWide);
	TestNearlyEqual(TEXT("90 deg, 10 m, 2 px = 2.08 cm"), FLureFishingRules::LineWidthAtDistance(2.f, 1000.f, 90.f, 1920.f, 0.f), 2.0833f, 0.001f);

	TArray<FVector> Points;
	FLureFishingRules::ComputeLinePoints(FVector(0.f, 0.f, 200.f), FVector(1000.f, 0.f, 0.f), 0.1f, 10, Points);
	TestEqual(TEXT("segments + 1 points"), Points.Num(), 11);
	TestTrue(TEXT("starts at the rod tip"), Points[0].Equals(FVector(0.f, 0.f, 200.f), 0.01));
	TestTrue(TEXT("ends at the bobber"), Points.Last().Equals(FVector(1000.f, 0.f, 0.f), 0.01));
	const float Length = static_cast<float>(FVector::Dist(FVector(0.f, 0.f, 200.f), FVector(1000.f, 0.f, 0.f)));
	TestNearlyEqual(TEXT("sags LineSag x length in the middle"), static_cast<float>(100.f - Points[5].Z), 0.1f * Length, 0.1f);

	// In a world: a cast line is drawn with >= 2 px widths seen from the player's eye.
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!Fishing)
	{
		return false;
	}
	Fishing->SetFishingProfile(QuickProfile());
	World.Tick(5);
	TestTrue(TEXT("cast"), Fishing->AuthorityCast(1.f, 0.f));
	World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240);
	World.Tick(2);
	const ULureFishingLineComponent* Line = Fishing->GetLine();
	if (!TestNotNull(TEXT("a line component while the line is out"), Line))
	{
		return false;
	}
	TestTrue(TEXT("the line is visible"), Line->IsLineVisible());
	TestEqual(TEXT("segments from data"), Line->GetNumSegments(), QuickProfile().LineSegments);
	const FVector Eye = Character->GetPawnViewLocation();
	bool bWide = true;
	for (int32 Index = 0; Index < Line->GetPoints().Num(); ++Index)
	{
		bWide &= FLureFishingRules::LinePixelsAtDistance(Line->GetWidths()[Index], static_cast<float>(FVector::Dist(Eye, Line->GetPoints()[Index])), 90.f, RefWidth) >= 2.f - 0.01f;
	}
	TestTrue(TEXT("every drawn point is >= 2 px wide from the eye"), bWide);
	TestTrue(TEXT("the line ends at the bobber"), FVector::Dist(Line->GetPoints().Last(), Fishing->GetBobberLocation()) < 60.f);
	if (const UStaticMeshComponent* Bobber = Fishing->GetBobberMesh())
	{
		TestTrue(TEXT("the bobber is shown"), Bobber->IsVisible());
		TestNearlyEqual(TEXT("bobber readability scale from data"), static_cast<float>(Bobber->GetComponentScale().X), QuickProfile().BobberScale, 0.001f);
		TestTrue(TEXT("the line ends at the bobber's LineAttach socket"), !Bobber->DoesSocketExist(TEXT("LineAttach"))
			|| Line->GetPoints().Last().Equals(Bobber->GetSocketLocation(TEXT("LineAttach")), 0.5));
	}
	else
	{
		AddInfo(TEXT("SM_Bobber is not imported here: bobber mesh checks skipped."));
	}
	Fishing->AuthorityReelIn();
	World.Tick(1);
	TestFalse(TEXT("no line when idle"), Line->IsLineVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingBobberTells, "Project.Fishing.Bobber.BiteTells", LureFishingTest::TestFlags)
bool FLureFishingBobberTells::RunTest(const FString& Parameters)
{
	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200") });
	FLureFishingRow Profile = QuickProfile(1.f);
	Profile.BiteWaitMin = Profile.BiteWaitMax = 4.f;
	Profile.NibblesMin = Profile.NibblesMax = 2;
	ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt), Tables, Profile);
	if (!Fishing || !CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	const float RestZ = static_cast<float>(Fishing->GetNetState().BobberRest.Z);
	float Lowest = TNumericLimits<float>::Max();
	float Highest = -TNumericLimits<float>::Max();
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		World.Tick(1);
		Lowest = FMath::Min(Lowest, static_cast<float>(Fishing->GetBobberLocation().Z));
		Highest = FMath::Max(Highest, static_cast<float>(Fishing->GetBobberLocation().Z));
	}
	TestTrue(FString::Printf(TEXT("waiting: floats and bobs within +-BobberBobAmplitude (%.2f..%.2f)"), Lowest - RestZ, Highest - RestZ),
		Lowest >= RestZ - Profile.BobberBobAmplitude - 0.01f && Highest <= RestZ + Profile.BobberBobAmplitude + 0.01f && Highest - Lowest > 0.1f);
	const uint8 NibblesBefore = Fishing->GetNetState().NibbleId;
	TestTrue(TEXT("a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 400));
	TestEqual(TEXT("2 nibbles came before the bite (data)"), static_cast<int32>(Fishing->GetNetState().NibbleId - NibblesBefore), 2);
	World.Tick(6);
	TestTrue(FString::Printf(TEXT("biting: pulled under at least the red top (%.1f cm, top at %.1f cm)"), RestZ - Fishing->GetBobberLocation().Z, 4.9f * Profile.BobberScale),
		RestZ - static_cast<float>(Fishing->GetBobberLocation().Z) >= 0.8f * Profile.BiteDipDepth);
	TestTrue(TEXT("HUD: bite prompt"), Fishing->GetStatusText().Contains(TEXT("BITE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingFullFlow, "Project.Fishing.Flow.CastBiteHookLand", LureFishingTest::TestFlags)
bool FLureFishingFullFlow::RunTest(const FString& Parameters)
{
	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200"), TEXT("Name=Test Shore") });
	FLureFishingRow Profile = QuickProfile();
	Profile.AutoLandDelay = 0.5f;
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	ULureFishingComponent* Fishing = SetUpFishing(Character, Tables, Profile);
	if (!Fishing || !CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestTrue(TEXT("bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120));
	Fishing->PressCast(); // the cast button hooks while the line is out
	TestEqual(TEXT("hooked with the cast button"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
	const FFishInstance Hooked = Fishing->GetHookedFish();
	TestTrue(TEXT("a real fish is on"), Hooked.IsValid() && Hooked.WeightKg > 0.f && Hooked.Value >= 1);
	TestTrue(TEXT("landed after AutoLandDelay (placeholder until the reel)"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 120));
	TestEqual(TEXT("result Landed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
	TestEqual(TEXT("the landed fish is the hooked one"), Fishing->GetLastLandedFish().Seed, Hooked.Seed);
	TestTrue(TEXT("HUD: caught"), Fishing->GetStatusText().Contains(TEXT("Caught:")));
	TestTrue(TEXT("HUD text reaches the placeholder HUD"), ALureHUD::GetStatusText(Character).Contains(TEXT("Caught:")));
	TestTrue(TEXT("the game mode uses the placeholder HUD"), GetDefault<ALureGameMode>()->HUDClass == ALureHUD::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingInputActions, "Project.Fishing.Input.CastAndHookActions", LureFishingTest::TestFlags)
bool FLureFishingInputActions::RunTest(const FString& Parameters)
{
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	UInputAction* CastAction = ULureInputSubsystem::GetInputActionByName(TEXT("Cast"));
	UInputAction* HookAction = ULureInputSubsystem::GetInputActionByName(TEXT("Hook"));
	if (!TestNotNull(TEXT("mapping context"), Context) || !TestNotNull(TEXT("Cast action by name"), CastAction) || !TestNotNull(TEXT("Hook action by name"), HookAction))
	{
		return false;
	}
	TestEqual(TEXT("Cast is a button"), static_cast<int32>(CastAction->ValueType), static_cast<int32>(EInputActionValueType::Boolean));
	TestEqual(TEXT("Hook is a button"), static_cast<int32>(HookAction->ValueType), static_cast<int32>(EInputActionValueType::Boolean));
	TestTrue(TEXT("names are listed"), ULureInputSubsystem::GetInputActionNames().Contains(TEXT("Cast")) && ULureInputSubsystem::GetInputActionNames().Contains(TEXT("Hook")));
	auto IsMapped = [Context](const UInputAction* Action, const FKey& Key)
	{
		return Context->GetMappings().ContainsByPredicate([Action, &Key](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action && Mapping.Key == Key; });
	};
	TestTrue(TEXT("Cast = left mouse button"), IsMapped(CastAction, EKeys::LeftMouseButton));
	TestTrue(TEXT("Cast = gamepad right trigger"), IsMapped(CastAction, EKeys::Gamepad_RightTrigger));

	// A possessed character binds them, and the bindings drive the cast.
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!Character || !Controller)
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.Tick(10);
	Character->GetFishing()->SetFishingProfile(QuickProfile());
	const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Character->InputComponent);
	if (!TestNotNull(TEXT("enhanced input component"), Input))
	{
		return false;
	}
	struct FInstance : public FInputActionInstance
	{
		FInstance(const UInputAction* Action, ETriggerEvent Event) : FInputActionInstance(Action) { TriggerEvent = Event; Value = FInputActionValue(Event != ETriggerEvent::Completed); }
	};
	auto Fire = [Input](const UInputAction* Action, ETriggerEvent Event)
	{
		int32 Ran = 0;
		const FInstance Instance(Action, Event);
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				Binding->Execute(Instance);
				++Ran;
			}
		}
		return Ran;
	};
	TestTrue(TEXT("Cast press is bound"), Fire(CastAction, ETriggerEvent::Started) > 0);
	TestTrue(TEXT("charging"), Character->GetFishing()->IsCharging());
	World.Tick(30);
	TestTrue(TEXT("charge grows while held"), Character->GetFishing()->GetCharge() > 0.3f);
	TestTrue(TEXT("HUD: cast power"), Character->GetFishing()->GetStatusText().Contains(TEXT("Cast power")));
	TestTrue(TEXT("Cast release is bound"), Fire(CastAction, ETriggerEvent::Completed) > 0);
	TestEqual(TEXT("released: the server (standalone) started the cast"), StateName(Character->GetFishing()->GetFishingState()), StateName(ELureFishingState::Casting));
	TestTrue(TEXT("Hook press is bound"), Input->GetActionEventBindings().ContainsByPredicate([HookAction](const TUniquePtr<FEnhancedInputActionEventBinding>& Binding)
		{ return Binding && Binding->GetAction() == HookAction && Binding->GetTriggerEvent() == ETriggerEvent::Started; }));
	Controller->UnPossess();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingRodOnHandBone, "Project.Fishing.Rod.OnHandBoneWithWorldScale", LureFishingTest::TestFlags)
bool FLureFishingRodOnHandBone::RunTest(const FString& Parameters)
{
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!Character || !Controller)
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.Tick(10);
	const USkeletalMeshComponent* Arms = Character->GetFirstPersonArms();
	const UStaticMeshComponent* Rod = Character->GetFishing()->GetRodMesh();
	if (!Arms || !Arms->GetSkeletalMeshAsset() || !Rod)
	{
		AddInfo(TEXT("SK_FPArms or SM_Rod_Basic is not imported here: rod attach checks skipped."));
		return true;
	}
	const FName Bone = GetDefault<ULureFishingSettings>()->RodAttachBone;
	TestTrue(TEXT("rod attached to the arms"), Rod->GetAttachParent() == Arms);
	TestEqual(TEXT("... at hand_r_rod"), Rod->GetAttachSocketName(), Bone);
	TestTrue(TEXT("rod at the bone"), Rod->GetComponentLocation().Equals(Arms->GetSocketLocation(Bone), 0.1));
	TestTrue(FString::Printf(TEXT("rod keeps world scale 1 (bone scale %s)"), *Arms->GetSocketTransform(Bone).GetScale3D().ToString()), Rod->GetComponentScale().Equals(FVector::OneVector, 0.001));
	TestTrue(TEXT("rod: owner only"), Rod->bOnlyOwnerSee);
	TestTrue(TEXT("rod: first-person primitive"), Rod->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson);
	TestTrue(TEXT("rod: no collision"), Rod->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("rod visible (in hand)"), Rod->IsVisible());
	// The line starts at the rod's LineTip, moved to where the first-person rod is drawn (scaled toward the eye).
	if (Rod->DoesSocketExist(GetDefault<ULureFishingSettings>()->RodLineSocket))
	{
		const FVector Tip = Rod->GetSocketLocation(GetDefault<ULureFishingSettings>()->RodLineSocket);
		const FVector Eye = Character->GetFirstPersonCamera()->GetComponentLocation();
		const FVector Start = Character->GetFishing()->GetLineStart();
		TestTrue(TEXT("line start is between the eye and the tip (first-person scale)"), FVector::Dist(Eye, Start) < FVector::Dist(Eye, Tip) + 0.1);
		TestNearlyEqual(TEXT("... on the eye-tip ray scaled by FirstPersonScale"), static_cast<float>(FVector::Dist(Eye, Start)),
			static_cast<float>(FVector::Dist(Eye, Tip)) * Character->GetFirstPersonCamera()->FirstPersonScale, 2.f);
	}
	Controller->UnPossess();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingDataTables, "Project.Fishing.Data.TablesValid", LureFishingTest::TestFlags)
bool FLureFishingDataTables::RunTest(const FString& Parameters)
{
	// DT_Fishing: imports cleanly, every row validates, design limits hold, the built-in profile is the shipped Default row.
	FString Csv;
	const UDataTable* Fishing = ImportCsv(*this, FLureFishingRow::StaticStruct(), TEXT("DT_Fishing.csv"), &Csv);
	if (!Fishing)
	{
		return false;
	}
	const FLureFishingRow* Default = Fishing->FindRow<FLureFishingRow>(TEXT("Default"), TEXT("test"), false);
	TestNotNull(TEXT("row Default (the settings' DefaultProfileRow)"), Default);
	TestEqual(TEXT("settings point at Default"), GetDefault<ULureFishingSettings>()->DefaultProfileRow, FName(TEXT("Default")));
	for (const TPair<FName, uint8*>& Pair : Fishing->GetRowMap())
	{
		const FLureFishingRow& Row = *reinterpret_cast<const FLureFishingRow*>(Pair.Value);
		const FString Name = Pair.Key.ToString();
		FString Problem;
		TestTrue(Name + TEXT(" validates: ") + Problem, Row.Validate(Problem));
		TestTrue(Name + TEXT(": line >= 2 px (designer B-S3)"), Row.LinePixelWidth >= 2.f);
		TestTrue(Name + TEXT(": bobber readability scale 2-5x (ART_STYLE ~3x)"), Row.BobberScale >= 2.f && Row.BobberScale <= 5.f);
		TestTrue(Name + TEXT(": the bite pulls the whole 4.9 cm top under (B-S4)"), Row.BiteDipDepth >= 4.9f * Row.BobberScale);
		TestTrue(Name + TEXT(": min cast < max cast"), Row.MinCastDistance < Row.MaxCastDistance);
		TestTrue(Name + TEXT(": the line reaches past the longest cast"), Row.MaxLineLength > Row.MaxCastDistance);
		TestTrue(Name + TEXT(": hook window 0.3-2 s"), Row.HookWindow >= 0.3f && Row.HookWindow <= 2.f);
		TestTrue(Name + TEXT(": nibble tip shows the white half (>= 45 deg)"), Row.NibbleTiltDeg >= 45.f);
	}
	if (Default)
	{
		const FLureFishingRow BuiltIn = FLureFishingRules::GetFallbackRow();
		TestTrue(TEXT("the built-in profile equals the shipped Default row (update FLureFishingRow defaults with the CSV)"),
			FLureFishingRow::StaticStruct()->CompareScriptStruct(Default, &BuiltIn, PPF_None));
	}
	// Raw cells: numbers are numbers, bools True/False, enums known names (the importer turns bad text into 0 silently).
	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines, true);
	TArray<FString> Header;
	Lines[0].ParseIntoArray(Header, TEXT(","), false);
	for (int32 Line = 1; Line < Lines.Num(); ++Line)
	{
		TArray<FString> Cells;
		Lines[Line].ParseIntoArray(Cells, TEXT(","), false);
		TestEqual(FString::Printf(TEXT("line %d has one cell per column"), Line + 1), Cells.Num(), Header.Num());
		for (int32 Column = 1; Column < FMath::Min(Cells.Num(), Header.Num()); ++Column)
		{
			const FProperty* Property = FLureFishingRow::StaticStruct()->FindPropertyByName(FName(*Header[Column].TrimStartAndEnd()));
			const FString Cell = Cells[Column].TrimStartAndEnd();
			const FString Where = FString::Printf(TEXT("%s.%s = '%s'"), *Cells[0], *Header[Column], *Cell);
			if (!TestNotNull(Where + TEXT(": known column"), Property))
			{
				continue;
			}
			if (CastField<FBoolProperty>(Property))
			{
				TestTrue(Where + TEXT(" is True/False"), Cell.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Cell.Equals(TEXT("False"), ESearchCase::IgnoreCase));
			}
			else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
			{
				TestTrue(Where + TEXT(" is a known name"), Enum->GetEnum()->GetIndexByNameString(Cell) != INDEX_NONE);
			}
			else
			{
				TestTrue(Where + TEXT(" is a number"), !Cell.IsEmpty() && FCString::IsNumeric(*Cell));
			}
		}
	}
	TArray<FString> Missing;
	for (TFieldIterator<FProperty> It(FLureFishingRow::StaticStruct()); It; ++It)
	{
		if (!Header.Contains(It->GetName()))
		{
			Missing.Add(It->GetName());
		}
	}
	TestEqual(FString::Printf(TEXT("every FLureFishingRow field is a CSV column (missing: %s)"), *FString::Join(Missing, TEXT(", "))), Missing.Num(), 0);

	// A missing table falls back to the built-in profile (with one warning).
	FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Component = World.Spawn(StandAt)->GetFishing();
	Component->ApplyFishingTable(nullptr);
	TestTrue(TEXT("no table: fallback profile"), Component->IsUsingFallbackProfile());
	Component->ApplyFishingTable(Fishing);
	TestFalse(TEXT("the CSV table: its Default row"), Component->IsUsingFallbackProfile());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingMovementRodColumns, "Project.Fishing.Data.MovementRodColumns", LureFishingTest::TestFlags)
bool FLureFishingMovementRodColumns::RunTest(const FString& Parameters)
{
	FString Csv;
	const UDataTable* Table = ImportCsv(*this, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"), &Csv);
	if (!Table)
	{
		return false;
	}
	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines, true);
	TArray<FString> Header;
	Lines[0].ParseIntoArray(Header, TEXT(","), false);
	for (FString& Column : Header)
	{
		Column.TrimStartAndEndInline();
	}
	TArray<FString> Missing;
	for (TFieldIterator<FProperty> It(FLureMovementRow::StaticStruct()); It; ++It)
	{
		if (!Header.Contains(It->GetName()))
		{
			Missing.Add(It->GetName());
		}
	}
	TestEqual(FString::Printf(TEXT("every FLureMovementRow field (rod pose too) is a DT_Movement.csv column (missing: %s)"), *FString::Join(Missing, TEXT(", "))), Missing.Num(), 0);

	const TArray<FLureMovementRow> Rows = MovementRows(Table);
	for (const ELureMovementState State : { ELureMovementState::Stand, ELureMovementState::Sprint, ELureMovementState::Crouch })
	{
		const FLureMovementRow& Row = RowOf(Rows, State);
		const FString Name = FLureMovementData::GetRowName(State).ToString();
		TestEqual(Name + TEXT(": still pose HoldRod"), PoseName(Row.RodPoseStill), PoseName(EFPArmsPose::HoldRod));
		TestEqual(Name + TEXT(": moving pose HoldRod"), PoseName(Row.RodPoseMoving), PoseName(EFPArmsPose::HoldRod));
		TestNearlyEqual(Name + TEXT(": arms follow the view up"), Row.ArmsPitchFollowUp, 1.f, 0.001f);
		TestEqual(Name + TEXT(": CanFish"), Row.CanFish, State != ELureMovementState::Sprint);
	}
	const FLureMovementRow& Prone = RowOf(Rows, ELureMovementState::Prone);
	TestEqual(TEXT("Prone: still = ProneHold"), PoseName(Prone.RodPoseStill), PoseName(EFPArmsPose::ProneHold));
	TestEqual(TEXT("Prone: moving = ProneTuck"), PoseName(Prone.RodPoseMoving), PoseName(EFPArmsPose::ProneTuck));
	TestNearlyEqual(TEXT("Prone: arms stay down when looking up"), Prone.ArmsPitchFollowUp, 0.f, 0.001f);
	TestTrue(TEXT("Prone: wall check on"), Prone.RodHoldClearance > 0.f);
	TestTrue(TEXT("Prone: CanFish (prone fishing, Jimmy 2026-09-22)"), Prone.CanFish);
	for (const FLureMovementRow& Row : Rows)
	{
		TestTrue(TEXT("RodMoveSpeedOut <= RodMoveSpeedIn"), Row.RodMoveSpeedOut <= Row.RodMoveSpeedIn);
	}
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		const FLureMovementRow Fallback = FLureMovementData::GetFallbackRow(State);
		const FLureMovementRow& Row = RowOf(Rows, State);
		const FString Name = FLureMovementData::GetRowName(State).ToString();
		TestTrue(Name + TEXT(": built-in rod pose values match the CSV"), Fallback.RodPoseStill == Row.RodPoseStill && Fallback.RodPoseMoving == Row.RodPoseMoving
			&& FMath::IsNearlyEqual(Fallback.RodMoveSpeedIn, Row.RodMoveSpeedIn) && FMath::IsNearlyEqual(Fallback.RodMoveSpeedOut, Row.RodMoveSpeedOut)
			&& FMath::IsNearlyEqual(Fallback.RodStillDelay, Row.RodStillDelay) && FMath::IsNearlyEqual(Fallback.RodPoseBlendTime, Row.RodPoseBlendTime)
			&& FMath::IsNearlyEqual(Fallback.ArmsPitchFollowUp, Row.ArmsPitchFollowUp) && FMath::IsNearlyEqual(Fallback.RodHoldClearance, Row.RodHoldClearance)
			&& Fallback.CanFish == Row.CanFish);
	}
	FLureMovementRow Bad = RowOf(Rows, ELureMovementState::Stand);
	Bad.RodMoveSpeedOut = Bad.RodMoveSpeedIn + 1.f;
	FString Problem;
	TestFalse(TEXT("a row with RodMoveSpeedOut > RodMoveSpeedIn is invalid"), Bad.Validate(Problem));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

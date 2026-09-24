// Lure T-027 QA (qa-engineer): hot spots - spawn rate, caps, lifetime, prewarm, where they may appear and wander, drift,
// the bonus of a cast that lands in one (luck, size, value, sooner bites) for the whole cast, replication to clients and the
// dev console commands on a client. Project.Fishing.Water.QA.{HotSpot,Net2P}.* - spec: docs/specs/fishing-water-rules.md
// section 5; design: GAME_DESIGN.md "Hot spots". Black-box: expectations come from the spec and the header contracts
// (Fishing/LureHotSpot*.h, Fishing/FishingWater.h, Dev/LureWaterDevCommands.h), never from the .cpp files.

#include "Tests/Fishing/QAFishingWaterTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Dev/LureWaterDevCommands.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Fishing/LureHotSpotSpawner.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_EDITOR
#include "Tests/NetTestHelpers.h"
#endif

namespace LureWaterQA
{
namespace QAHot
{
	/** A spawner test row with explicit numbers (not the shipped values). */
	FLureHotSpotRow Row(float SpawnInterval, int32 MaxPerArea, std::initializer_list<const TCHAR*> Habitats = {}, float MinDepth = 60.f)
	{
		FLureHotSpotRow Out;
		Out.SpawnInterval = SpawnInterval;
		Out.MaxPerArea = MaxPerArea;
		Out.MinSpacing = 300.f;
		Out.Radius = 150.f;
		Out.LifetimeMin = 20.f;
		Out.LifetimeMax = 30.f;
		Out.DriftSpeed = 0.f;
		Out.DriftRange = 0.f;
		Out.MinDepth = MinDepth;
		Out.MaxDepth = 0.f;
		Out.AllowedHabitats.Reset();
		for (const TCHAR* Habitat : Habitats)
		{
			Out.AllowedHabitats.Add(Tag(Habitat));
		}
		return Out;
	}

	/** A bonus row: explicit, easy-to-spot numbers. */
	FLureHotSpotRow BonusRow(float Radius, float Luck, float Size, float Value, float WaitScale)
	{
		FLureHotSpotRow Out;
		Out.Radius = Radius;
		Out.DriftRange = 0.f;
		Out.DriftSpeed = 0.f;
		Out.MinDepth = 0.f;
		Out.LuckBonus = Luck;
		Out.SizeBonus = Size;
		Out.ValueMultiplier = Value;
		Out.BiteWaitScale = WaitScale;
		return Out;
	}

	UDataTable* Table(const TArray<TPair<FName, FLureHotSpotRow>>& Rows)
	{
		UDataTable* Out = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Out->RowStruct = FLureHotSpotRow::StaticStruct();
		for (const TPair<FName, FLureHotSpotRow>& Entry : Rows)
		{
			Out->AddRow(Entry.Key, Entry.Value);
		}
		return Out;
	}

	ALureHotSpotSpawner* Spawner(UWorld* World, const UDataTable* HotSpots, int32 Seed, bool bAuto = false)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureHotSpotSpawner* Out = World->SpawnActor<ALureHotSpotSpawner>(ALureHotSpotSpawner::StaticClass(), FTransform::Identity, Params);
		if (Out)
		{
			Out->SetAutoSpawn(bAuto);
			Out->SetHotSpotTable(HotSpots);
			Out->SetRandomSeed(Seed);
		}
		return Out;
	}

	ALureWaterArea* CircleArea(UWorld* World, const TCHAR* Id, const TCHAR* Habitat, const FVector2D& Center, float Radius, int32 Priority = 0)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureWaterArea* Area = World->SpawnActor<ALureWaterArea>(ALureWaterArea::StaticClass(), FTransform(FVector(Center.X, Center.Y, 0.0)), Params);
		if (Area)
		{
			Area->AreaId = FName(Id);
			Area->Priority = Priority;
			Area->SetAreaTags(FName(Habitat), NAME_None);
			Area->SetShapeCircle(Radius);
		}
		return Area;
	}

	AActor* Box(UWorld* World, const FVector& Center, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Shape = NewObject<UBoxComponent>(Actor, NAME_None);
		Shape->SetMobility(EComponentMobility::Static);
		Shape->SetBoxExtent(Extent, false);
		Shape->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Shape->SetRelativeLocation_Direct(Center);
		Actor->SetRootComponent(Shape);
		Shape->RegisterComponent();
		return Actor;
	}

	/** The sea 5 m deep over x in [-1000, 11000], y in [-6000, 6000] (the sea surface is z = 0). */
	void DeepSea(UWorld* World)
	{
		Box(World, FVector(5000.f, 0.f, -550.f), FVector(6000.f, 6000.f, 50.f));
	}

	TArray<ALureHotSpot*> Live(UWorld* World)
	{
		return ALureHotSpotSpawner::GetLiveHotSpots(World, FLureWaterQuery::GetTime(World));
	}

	int32 Clear(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ALureHotSpot> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				It->Destroy();
				++Count;
			}
		}
		return Count;
	}

	ALureHotSpot* SpawnAt(UWorld* World, FName Type, const FLureHotSpotRow& HotRow, const FVector2D& XY, float Lifetime, int32 Seed = 11)
	{
		return ALureHotSpot::SpawnHotSpot(World, nullptr, Type, HotRow, NAME_None, FVector(XY.X, XY.Y, 0.0), Seed, FLureWaterQuery::GetTime(World), Lifetime);
	}

	/** Casts along +X at Charge and waits for the bobber to rest. */
	bool Cast(FAutomationTestBase& Test, QAFishing::FScene& Scene, ULureFishingComponent* Fishing, float Charge = 0.5f)
	{
		if (!Test.TestTrue(TEXT("QA: the cast starts"), Fishing->AuthorityCast(Charge, 0.f)))
		{
			return false;
		}
		return Test.TestTrue(TEXT("QA: the bobber lands"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	}

	void ReelIn(QAFishing::FScene& Scene, ULureFishingComponent* Fishing)
	{
		Fishing->AuthorityReelIn();
		Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 300);
		Scene.Tick(3);
	}

	/** What one cast in a scene produced (the wait, the water, the bite). */
	struct FCastResult
	{
		bool bOk = false;
		FVector Rest = FVector::ZeroVector;
		double Wait = -1.0;
		FLureHotSpotBonus Bonus;
		FName NetHotSpot;
		FString Status;
		FFishRollContext Context;
		FFishInstance Fish;
	};

	/** A fresh scene with the dock; optionally a hot spot (TypeId, Row) at the charge-0.5 landing point (x = 1300). One cast to the bite. */
	FCastResult CastOnce(FAutomationTestBase& Test, const FLureFishingRow& Profile, int32 Seed, const FLureHotSpotRow* HotRow)
	{
		FCastResult Out;
		QAFishing::FScene Scene;
		if (!Scene.Create(Test))
		{
			return Out;
		}
		ULureFishingComponent* Fishing = Scene.SetUpFishing(Test, Scene.Spawn(Test), Profile, 12.f, Seed);
		if (!Fishing)
		{
			return Out;
		}
		if (HotRow && !Test.TestNotNull(TEXT("QA: hot spot spawned"), SpawnAt(Scene.World, TEXT("Bubbles"), *HotRow, FVector2D(1300.0, 0.0), 120.f)))
		{
			return Out;
		}
		if (!Cast(Test, Scene, Fishing))
		{
			return Out;
		}
		Out.Rest = FVector(Fishing->GetNetState().BobberRest);
		Out.Wait = Fishing->GetScheduledBiteTime() - Fishing->GetNetState().StateStartTime;
		Out.Bonus = Fishing->GetHotSpotBonus();
		Out.NetHotSpot = Fishing->GetNetState().Water.HotSpotType;
		Scene.Tick(1);
		Out.Status = Fishing->GetStatusText();
		if (!Test.TestTrue(TEXT("QA: a bite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 600)))
		{
			return Out;
		}
		Out.Context = Fishing->GetLastRollContext();
		Out.Fish = Fishing->GetPendingFish();
		Out.bOk = true;
		return Out;
	}

#if WITH_EDITOR
	/** A player character for a client's controller on the server (the QA net harness pattern). */
	ALurePlayerCharacter* SpawnPlayer(UWorld* World, APlayerController* Controller, const FVector& Feet, const UDataTable* Movement)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Movement, Rows, Problems);
		const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, QAFishing::RowOf(Rows, ELureMovementState::Stand).CapsuleHalfHeight + 2.15f));
		ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Character)
		{
			Character->GetLureMovement()->ApplyMovementTable(Movement);
			Character->FinishSpawning(Transform);
			if (Controller)
			{
				Controller->Possess(Character);
			}
		}
		return Character;
	}

	void ExpectNetHarnessNoise(FAutomationTestBase& Test)
	{
		Test.AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		Test.AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		Test.AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		Test.AddExpectedMessagePlain(TEXT("is not imported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	}
#endif
}

// =====================================================================================================================
// Pure rules
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnChance, "Project.Fishing.Water.QA.HotSpot.SpawnChanceAndLifetimeRoll", Flags)
bool FQAHotSpotSpawnChance::RunTest(const FString& Parameters)
{
	// Spec 5: a spawn happens with chance 1 - exp(-seconds / SpawnInterval) (0 = never); the lifetime is LifetimeMin + (Max - Min) x U.
	FLureHotSpotRow Row = QAHot::Row(40.f, 1);
	TestEqual(TEXT("0 s: never"), FLureWaterRules::SpawnChance(Row, 0.f), 0.f);
	TestNearlyEqual(TEXT("one interval: 1 - 1/e"), FLureWaterRules::SpawnChance(Row, 40.f), 1.f - FMath::Exp(-1.f), 1.0e-5f);
	TestNearlyEqual(TEXT("1 s of 40"), FLureWaterRules::SpawnChance(Row, 1.f), 1.f - FMath::Exp(-1.f / 40.f), 1.0e-6f);
	TestNearlyEqual(TEXT("the 45 s prewarm"), FLureWaterRules::SpawnChance(Row, 45.f), 1.f - FMath::Exp(-45.f / 40.f), 1.0e-5f);
	const float Long = FLureWaterRules::SpawnChance(Row, 1.0e6f);
	TestTrue(TEXT("very long: ~1, never above"), Long <= 1.f && Long > 0.999f);
	float Previous = 0.f;
	bool bMonotone = true;
	for (float Seconds = 0.5f; Seconds < 400.f; Seconds += 0.5f)
	{
		const float Chance = FLureWaterRules::SpawnChance(Row, Seconds);
		bMonotone &= Chance >= Previous;
		Previous = Chance;
	}
	TestTrue(TEXT("grows with time"), bMonotone);
	Row.SpawnInterval = 0.f;
	TestEqual(TEXT("SpawnInterval 0: never"), FLureWaterRules::SpawnChance(Row, 1000.f), 0.f);
	TestEqual(TEXT("two independent 1 s checks = one 2 s check (1-p)^2"), 1.f - FMath::Square(1.f - FLureWaterRules::SpawnChance(QAHot::Row(40.f, 1), 1.f)),
		FLureWaterRules::SpawnChance(QAHot::Row(40.f, 1), 2.f), 1.0e-5f);

	FLureHotSpotRow Life = QAHot::Row(40.f, 1);
	Life.LifetimeMin = 90.f;
	Life.LifetimeMax = 180.f;
	TestEqual(TEXT("U 0: LifetimeMin"), FLureWaterRules::LifetimeFromRoll(Life, 0.f), 90.f);
	TestEqual(TEXT("U 1: LifetimeMax"), FLureWaterRules::LifetimeFromRoll(Life, 1.f), 180.f);
	TestEqual(TEXT("U 0.5: the middle"), FLureWaterRules::LifetimeFromRoll(Life, 0.5f), 135.f);
	Life.LifetimeMax = 90.f;
	TestEqual(TEXT("Min == Max: fixed"), FLureWaterRules::LifetimeFromRoll(Life, 0.73f), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotDriftPure, "Project.Fishing.Water.QA.HotSpot.DriftDeterministicBoundedSmooth", Flags)
bool FQAHotSpotDriftPure::RunTest(const FString& Parameters)
{
	// Spec 5 "Drift": starts at the anchor, never farther than DriftRange, RMS speed = DriftSpeed, a smooth wander, a pure function
	// of (range, speed, seed, age) so every machine gets the same center; zero range or speed = no motion. Also after days of
	// server uptime (large ages).
	struct FCase { float Range; float Speed; };
	for (const FCase& Case : { FCase{ 400.f, 15.f }, FCase{ 600.f, 25.f }, FCase{ 100.f, 60.f } })
	{
		for (const int32 Seed : { 1, -7, 424242, MAX_int32, MIN_int32 })
		{
			const FString Label = FString::Printf(TEXT("range %.0f speed %.0f seed %d"), Case.Range, Case.Speed, Seed);
			TestTrue(Label + TEXT(": starts at the anchor"), FLureWaterRules::DriftOffset(Case.Range, Case.Speed, Seed, 0.0).Size() < 1.0e-3);
			double SumSquares = 0.0;
			double MaxStep = 0.0;
			double MaxDistance = 0.0;
			int32 Steps = 0;
			constexpr double Step = 1.0 / 60.0;
			FVector2D Previous = FLureWaterRules::DriftOffset(Case.Range, Case.Speed, Seed, 0.0);
			for (double Age = Step; Age < 1200.0; Age += Step)
			{
				const FVector2D Offset = FLureWaterRules::DriftOffset(Case.Range, Case.Speed, Seed, Age);
				const double Moved = FVector2D::Distance(Offset, Previous);
				SumSquares += FMath::Square(Moved / Step);
				MaxStep = FMath::Max(MaxStep, Moved);
				MaxDistance = FMath::Max(MaxDistance, Offset.Size());
				Previous = Offset;
				++Steps;
			}
			const double Rms = FMath::Sqrt(SumSquares / Steps);
			TestTrue(FString::Printf(TEXT("%s: never farther than the range (max %.2f)"), *Label, MaxDistance), MaxDistance <= Case.Range + 1.0e-2);
			TestTrue(FString::Printf(TEXT("%s: uses most of its range (max %.1f)"), *Label, MaxDistance), MaxDistance >= 0.5 * Case.Range);
			TestTrue(FString::Printf(TEXT("%s: RMS speed %.2f ~ %.0f"), *Label, Rms, Case.Speed), FMath::Abs(Rms - Case.Speed) <= 0.15 * Case.Speed);
			TestTrue(FString::Printf(TEXT("%s: smooth (max %.3f cm per frame)"), *Label, MaxStep), MaxStep <= 4.0 * Case.Speed * Step);
			// The same inputs give the same offset, bit for bit (every machine).
			const FVector2D A = FLureWaterRules::DriftOffset(Case.Range, Case.Speed, Seed, 123.456);
			const FVector2D B = FLureWaterRules::DriftOffset(Case.Range, Case.Speed, Seed, 123.456);
			TestTrue(Label + TEXT(": deterministic"), A.X == B.X && A.Y == B.Y);
			// Days of server uptime: still bounded and smooth (double time).
			for (const double Big : { 86400.0, 7.0 * 86400.0, 30.0 * 86400.0 })
			{
				const FVector2D P = FLureWaterRules::DriftOffset(Case.Range, Case.Speed, Seed, Big);
				const FVector2D Q = FLureWaterRules::DriftOffset(Case.Range, Case.Speed, Seed, Big + Step);
				TestTrue(FString::Printf(TEXT("%s at %.0f s: finite, bounded, smooth"), *Label, Big), !P.ContainsNaN() && P.Size() <= Case.Range + 1.0e-2
					&& FVector2D::Distance(P, Q) <= 4.0 * Case.Speed * Step + 1.0e-3);
			}
		}
	}
	TestFalse(TEXT("different seeds wander differently"), FLureWaterRules::DriftOffset(400.f, 15.f, 1, 30.0).Equals(FLureWaterRules::DriftOffset(400.f, 15.f, 2, 30.0), 1.0));
	for (const double Age : { 0.0, 5.0, 77.7, 5000.0 })
	{
		TestTrue(FString::Printf(TEXT("range 0 at %.1f s: no motion"), Age), FLureWaterRules::DriftOffset(0.f, 25.f, 9, Age).IsNearlyZero(1.0e-4));
		TestTrue(FString::Printf(TEXT("speed 0 at %.1f s: no motion"), Age), FLureWaterRules::DriftOffset(400.f, 0.f, 9, Age).IsNearlyZero(1.0e-4));
	}
	return true;
}

// =====================================================================================================================
// The spawner (server)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnRate, "Project.Fishing.Water.QA.HotSpot.SpawnRateFollowsSpawnInterval", Flags)
bool FQAHotSpotSpawnRate::RunTest(const FString& Parameters)
{
	// Spec 5: while an area is under its cap, a row spawns with chance 1 - exp(-seconds / SpawnInterval) per check. 400 checks of
	// 20 s with SpawnInterval 40 (p = 0.393): about 157 spawns (+-4 sigma); a 0 s check never spawns; SpawnInterval 0 never does.
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QAHot::DeepSea(Scene.World);
	QAHot::CircleArea(Scene.World, TEXT("qa_reef"), TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 3000.f);
	const TStrongObjectPtr<UDataTable> Rows(QAHot::Table({ { TEXT("QA_Rate"), QAHot::Row(40.f, 1) }, { TEXT("QA_Never"), QAHot::Row(0.f, 5) } }));
	ALureHotSpotSpawner* Spawner = QAHot::Spawner(Scene.World, Rows.Get(), 2027);
	if (!TestNotNull(TEXT("spawner"), Spawner))
	{
		return false;
	}
	int32 ZeroSeconds = 0;
	for (int32 Check = 0; Check < 50; ++Check)
	{
		ZeroSeconds += Spawner->SpawnStep(0.f);
	}
	TestEqual(TEXT("50 checks of 0 s: no spawn"), ZeroSeconds, 0);
	int32 Spawned = 0;
	constexpr int32 Checks = 400;
	for (int32 Check = 0; Check < Checks; ++Check)
	{
		Spawned += Spawner->SpawnStep(20.f);
		for (ALureHotSpot* HotSpot : QAHot::Live(Scene.World))
		{
			TestEqual(TEXT("only the rate row spawns (SpawnInterval 0 never does)"), HotSpot->GetState().TypeId, FName(TEXT("QA_Rate")));
		}
		QAHot::Clear(Scene.World);
	}
	const double P = 1.0 - FMath::Exp(-0.5);
	const double Mean = Checks * P;
	const double Sigma = FMath::Sqrt(Checks * P * (1.0 - P));
	AddInfo(FString::Printf(TEXT("%d spawns in %d checks (expected %.1f +- %.1f)"), Spawned, Checks, Mean, Sigma));
	TestTrue(FString::Printf(TEXT("spawn count %d within 4 sigma of %.1f"), Spawned, Mean), FMath::Abs(Spawned - Mean) <= 4.0 * Sigma);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotCaps, "Project.Fishing.Water.QA.HotSpot.MaxPerAreaAndLevelCap", Flags)
bool FQAHotSpotCaps::RunTest(const FString& Parameters)
{
	// Spec 5: at most MaxPerArea of a row live in one area (each row counts on its own), at most the spawner's max in the level,
	// HotSpotTypes limits the rows, and unbounded default water gets none without a player nearby.
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QAHot::DeepSea(Scene.World);
	QAHot::CircleArea(Scene.World, TEXT("qa_a"), TEXT("Habitat.Reef"), FVector2D(3000.0, -3000.0), 2000.f);
	QAHot::CircleArea(Scene.World, TEXT("qa_b"), TEXT("Habitat.Lagoon"), FVector2D(7000.0, 3000.0), 2000.f);
	const TStrongObjectPtr<UDataTable> Rows(QAHot::Table({ { TEXT("QA_Three"), QAHot::Row(0.01f, 3) }, { TEXT("QA_Two"), QAHot::Row(0.01f, 2) } }));
	ALureHotSpotSpawner* Spawner = QAHot::Spawner(Scene.World, Rows.Get(), 99);
	if (!TestNotNull(TEXT("spawner"), Spawner))
	{
		return false;
	}
	Spawner->MaxHotSpots = 100;
	for (int32 Check = 0; Check < 30; ++Check)
	{
		Spawner->SpawnStep(100.f);
	}
	TMap<FString, int32> Counts;
	for (ALureHotSpot* HotSpot : QAHot::Live(Scene.World))
	{
		Counts.FindOrAdd(HotSpot->GetState().AreaId.ToString() + TEXT("/") + HotSpot->GetState().TypeId.ToString())++;
	}
	TestEqual(TEXT("qa_a / QA_Three: 3"), Counts.FindRef(TEXT("qa_a/QA_Three")), 3);
	TestEqual(TEXT("qa_a / QA_Two: 2"), Counts.FindRef(TEXT("qa_a/QA_Two")), 2);
	TestEqual(TEXT("qa_b / QA_Three: 3"), Counts.FindRef(TEXT("qa_b/QA_Three")), 3);
	TestEqual(TEXT("qa_b / QA_Two: 2"), Counts.FindRef(TEXT("qa_b/QA_Two")), 2);
	TestEqual(TEXT("none in the default water (no player to spawn near)"), Counts.FindRef(TEXT("None/QA_Three")) + Counts.FindRef(TEXT("None/QA_Two")), 0);
	TestEqual(TEXT("10 in all"), QAHot::Live(Scene.World).Num(), 10);
	TArray<ALureHotSpot*> All = QAHot::Live(Scene.World);
	for (int32 I = 0; I < All.Num(); ++I)
	{
		for (int32 J = I + 1; J < All.Num(); ++J)
		{
			const double Distance = FVector::Dist2D(FVector(All[I]->GetState().Anchor), FVector(All[J]->GetState().Anchor));
			TestTrue(FString::Printf(TEXT("MinSpacing 300 between hot spots %d and %d (%.0f cm)"), I, J, Distance), Distance >= 300.0 - 0.5);
		}
	}

	QAHot::Clear(Scene.World);
	Spawner->MaxHotSpots = 4;
	for (int32 Check = 0; Check < 30; ++Check)
	{
		Spawner->SpawnStep(100.f);
	}
	TestEqual(TEXT("the spawner's max (4) caps the level"), QAHot::Live(Scene.World).Num(), 4);
	QAHot::Clear(Scene.World);
	Spawner->MaxHotSpots = 0;
	TestEqual(TEXT("max 0: none"), Spawner->SpawnStep(1000.f), 0);
	Spawner->MaxHotSpots = 100;
	Spawner->HotSpotTypes = { TEXT("QA_Two") };
	for (int32 Check = 0; Check < 30; ++Check)
	{
		Spawner->SpawnStep(100.f);
	}
	bool bOnlyTwo = QAHot::Live(Scene.World).Num() == 4;
	for (ALureHotSpot* HotSpot : QAHot::Live(Scene.World))
	{
		bOnlyTwo &= HotSpot->GetState().TypeId == TEXT("QA_Two");
	}
	TestTrue(TEXT("HotSpotTypes = [QA_Two]: only QA_Two, 2 per area"), bOnlyTwo);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotLifetime, "Project.Fishing.Water.QA.HotSpot.LifetimeWithinRowThenGone", Flags)
bool FQAHotSpotLifetime::RunTest(const FString& Parameters)
{
	// Spec 5 "Lifetime": each lasts a random time in [LifetimeMin, LifetimeMax]; it is active from its spawn time until (not at)
	// its end time; the server destroys it then, and nothing finds it any more.
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QAHot::DeepSea(Scene.World);
	QAHot::CircleArea(Scene.World, TEXT("qa_a"), TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 4000.f);
	FLureHotSpotRow Row = QAHot::Row(0.01f, 12);
	Row.LifetimeMin = 5.f;
	Row.LifetimeMax = 9.f;
	const TStrongObjectPtr<UDataTable> Rows(QAHot::Table({ { TEXT("QA_Life"), Row } }));
	ALureHotSpotSpawner* Spawner = QAHot::Spawner(Scene.World, Rows.Get(), 5);
	for (int32 Check = 0; Check < 12; ++Check)
	{
		Spawner->SpawnStep(100.f);
	}
	TArray<TWeakObjectPtr<ALureHotSpot>> Spawned;
	double LastEnd = 0.0;
	for (ALureHotSpot* HotSpot : QAHot::Live(Scene.World))
	{
		const FLureHotSpotState& State = HotSpot->GetState();
		const double Life = State.EndTime - State.SpawnTime;
		TestTrue(FString::Printf(TEXT("%s lives %.3f s, within [5, 9]"), *HotSpot->GetName(), Life), Life >= 5.0 - 1.0e-4 && Life <= 9.0 + 1.0e-4);
		TestTrue(TEXT("active at its spawn time"), HotSpot->IsActiveAt(State.SpawnTime));
		TestTrue(TEXT("active just before its end"), HotSpot->IsActiveAt(State.EndTime - 0.001));
		TestFalse(TEXT("not active at its end time"), HotSpot->IsActiveAt(State.EndTime));
		TestFalse(TEXT("does not contain its center at its end time"), HotSpot->ContainsAt(FVector2D(FVector(State.Anchor)), State.EndTime));
		LastEnd = FMath::Max(LastEnd, State.EndTime);
		Spawned.Add(HotSpot);
	}
	TestTrue(TEXT("several spawned"), Spawned.Num() >= 3);
	Scene.AdvanceTo(LastEnd + 0.1);
	Scene.Tick(2);
	int32 Remaining = 0;
	for (const TWeakObjectPtr<ALureHotSpot>& HotSpot : Spawned)
	{
		Remaining += HotSpot.IsValid() && !HotSpot->IsActorBeingDestroyed() ? 1 : 0;
	}
	TestEqual(TEXT("all destroyed by the server after their end"), Remaining, 0);
	TestEqual(TEXT("none live"), QAHot::Live(Scene.World).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotPrewarm, "Project.Fishing.Water.QA.HotSpot.PrewarmFirstCheck", Flags)
bool FQAHotSpotPrewarm::RunTest(const FString& Parameters)
{
	// Spec 5: the first automatic check counts as HotSpotPrewarmSeconds, so a level starts with hot spots. SpawnInterval 1000 s:
	// with a huge prewarm the first check spawns at once; with prewarm 0 the first second almost never does (fixed seed).
	int32 Results[2] = { -1, -1 };
	const float Prewarms[2] = { 1.0e6f, 0.f };
	for (int32 Case = 0; Case < 2; ++Case)
	{
		QAFishing::FScene Scene;
		if (!Scene.Create(*this, /*bDock*/ false))
		{
			return false;
		}
		QAHot::DeepSea(Scene.World);
		QAHot::CircleArea(Scene.World, TEXT("qa_a"), TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 3000.f);
		const TStrongObjectPtr<UDataTable> Rows(QAHot::Table({ { TEXT("QA_Slow"), QAHot::Row(1000.f, 1) } }));
		float Saved = GetMutableDefault<ULureWaterSettings>()->HotSpotPrewarmSeconds;
		GetMutableDefault<ULureWaterSettings>()->HotSpotPrewarmSeconds = Prewarms[Case];
		ALureHotSpotSpawner* Spawner = QAHot::Spawner(Scene.World, Rows.Get(), 31, /*bAuto*/ true);
		Scene.Tick(60 + 30); // 1.5 s: the first check (and maybe a second)
		Results[Case] = QAHot::Live(Scene.World).Num();
		GetMutableDefault<ULureWaterSettings>()->HotSpotPrewarmSeconds = Saved;
		TestNotNull(TEXT("spawner"), Spawner);
	}
	TestEqual(TEXT("huge prewarm: the first check spawns"), Results[0], 1);
	TestEqual(TEXT("prewarm 0: nothing in the first 1.5 s"), Results[1], 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotOnlyInValidWater, "Project.Fishing.Water.QA.HotSpot.OnlyInValidWater", Flags)
bool FQAHotSpotOnlyInValidWater::RunTest(const FString& Parameters)
{
	// Acceptance: hot spots appear in valid water. Spec 5: the point is fishable water (no land above it), its winning area is the
	// area it spawns for, the row allows that habitat and depth (and at least MinBiteDepth), and its wander circle is fishable water
	// of an allowed habitat. The reef area has an island, a 10 cm sandbar, a 40 cm shelf (under the row's 60 cm), and a lagoon
	// (another habitat, higher priority) cut out of it. An area over land never gets one.
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	UWorld* World = Scene.World;
	QAHot::DeepSea(World);
	QAHot::Box(World, FVector(5000.f, 0.f, 20.f), FVector(700.f, 700.f, 20.f));        // island 40 cm high
	QAHot::Box(World, FVector(3000.f, 1800.f, -60.f), FVector(600.f, 600.f, 50.f));    // sandbar, 10 cm deep
	QAHot::Box(World, FVector(7000.f, -1800.f, -90.f), FVector(700.f, 700.f, 50.f));   // shelf, 40 cm deep
	QAHot::Box(World, FVector(-6000.f, -6000.f, 100.f), FVector(1500.f, 1500.f, 100.f)); // a land plateau
	QAHot::CircleArea(World, TEXT("qa_reef"), TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 3500.f, 0);
	QAHot::CircleArea(World, TEXT("qa_lagoon"), TEXT("Habitat.Lagoon"), FVector2D(5000.0, 2600.0), 900.f, 5);
	QAHot::CircleArea(World, TEXT("qa_on_land"), TEXT("Habitat.Reef"), FVector2D(-6000.0, -6000.0), 1000.f, 0);
	FLureHotSpotRow Reef = QAHot::Row(0.01f, 40, { TEXT("Habitat.Reef") }, 60.f);
	Reef.DriftRange = 150.f;
	Reef.DriftSpeed = 10.f;
	Reef.MinSpacing = 200.f;
	const TStrongObjectPtr<UDataTable> Rows(QAHot::Table({ { TEXT("QA_Reef"), Reef } }));
	ALureHotSpotSpawner* Spawner = QAHot::Spawner(World, Rows.Get(), 777);
	Spawner->MaxHotSpots = 200;
	const TArray<FLureWaterAreaInfo> Areas = FLureWaterQuery::GatherAreas(World);
	int32 Checked = 0;
	int32 Bad = 0;
	for (int32 Round = 0; Round < 20 && Bad < 10; ++Round)
	{
		for (int32 Check = 0; Check < 10; ++Check)
		{
			Spawner->SpawnStep(100.f); // at most one per row and area per check
		}
		for (ALureHotSpot* HotSpot : QAHot::Live(World))
		{
			const FLureHotSpotState& State = HotSpot->GetState();
			const FVector2D XY(State.Anchor.X, State.Anchor.Y);
			float WaterZ = 0.f, Depth = 0.f;
			const bool bWater = FLureWaterQuery::ProbeWater(World, XY, WaterZ, Depth);
			const int32 Index = FLureWaterRules::FindAreaIndex(Areas, XY, Depth);
			const FString Winner = Index == INDEX_NONE ? FString(TEXT("default")) : Areas[Index].AreaId.ToString();
			bool bRing = true;
			const double Ring = Reef.DriftRange + 0.5 * Reef.Radius;
			for (int32 Step = 0; Step < 8; ++Step)
			{
				const double Angle = UE_DOUBLE_TWO_PI * Step / 8.0;
				float RingZ = 0.f, RingDepth = 0.f;
				bRing &= FLureWaterQuery::ProbeWater(World, XY + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Ring * 0.999, RingZ, RingDepth) && RingDepth >= 15.f;
			}
			const bool bOk = bWater && Depth >= 60.f && State.AreaId == TEXT("qa_reef") && Winner == TEXT("qa_reef") && FMath::Abs(State.Anchor.Z - WaterZ) <= 1.0 && bRing;
			if (!bOk)
			{
				++Bad;
				AddError(FString::Printf(TEXT("hot spot at (%.0f, %.0f): water %d, depth %.0f, area %s, winner %s, anchor z %.1f vs %.1f, wander circle on water %d"),
					XY.X, XY.Y, bWater ? 1 : 0, Depth, *State.AreaId.ToString(), *Winner, State.Anchor.Z, WaterZ, bRing ? 1 : 0));
			}
			++Checked;
		}
		QAHot::Clear(World);
	}
	AddInfo(FString::Printf(TEXT("%d hot spots checked"), Checked));
	TestTrue(TEXT("enough hot spots to be meaningful"), Checked >= 100);

	// A lagoon-only row: only in the lagoon cut-out.
	FLureHotSpotRow Lagoon = QAHot::Row(0.01f, 10, { TEXT("Habitat.Lagoon") }, 60.f);
	Lagoon.MinSpacing = 100.f;
	const TStrongObjectPtr<UDataTable> LagoonRows(QAHot::Table({ { TEXT("QA_Lagoon"), Lagoon } }));
	ALureHotSpotSpawner* LagoonSpawner = QAHot::Spawner(World, LagoonRows.Get(), 778);
	for (int32 Round = 0; Round < 10; ++Round)
	{
		LagoonSpawner->SpawnStep(100.f);
	}
	int32 InLagoon = 0;
	for (ALureHotSpot* HotSpot : QAHot::Live(World))
	{
		const bool bIn = HotSpot->GetState().AreaId == TEXT("qa_lagoon") && FVector::Dist2D(FVector(HotSpot->GetState().Anchor), FVector(5000.0, 2600.0, 0.0)) <= 900.0;
		InLagoon += bIn ? 1 : 0;
		TestTrue(TEXT("a lagoon hot spot is in the lagoon"), bIn);
	}
	TestTrue(TEXT("lagoon hot spots spawned"), InLagoon >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotDriftAvoidsRocks, "Project.Fishing.Water.QA.HotSpot.DriftNeverCrossesLand", Flags)
bool FQAHotSpotDriftAvoidsRocks::RunTest(const FString& Parameters)
{
	// Acceptance "hot spots appear in valid water" over their whole life, not only where they appear: a reef with 1.5 m rocks
	// (above the water) every 9 m. Every spawned hot spot's center is sampled every 0.25 s of its life: it must always be over
	// fishable water (a cast into it must be able to bite). The spec's spawn check probes 8 points on the wander circle; a rock
	// between those points but inside the wander disc is what this looks for.
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	UWorld* World = Scene.World;
	QAHot::DeepSea(World);
	for (int32 X = 0; X < 9; ++X)
	{
		for (int32 Y = 0; Y < 9; ++Y)
		{
			QAHot::Box(World, FVector(1400.f + X * 900.f, -3600.f + Y * 900.f, 0.f), FVector(75.f, 75.f, 50.f)); // top 50 cm above the sea
		}
	}
	QAHot::CircleArea(World, TEXT("qa_rocky"), TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 3000.f);
	FLureHotSpotRow Row = QAHot::Row(0.01f, 30, {}, 60.f);
	Row.DriftRange = 500.f;
	Row.DriftSpeed = 40.f;
	Row.Radius = 100.f;
	Row.MinSpacing = 400.f;
	Row.LifetimeMin = Row.LifetimeMax = 60.f;
	const TStrongObjectPtr<UDataTable> Rows(QAHot::Table({ { TEXT("QA_Drift"), Row } }));
	ALureHotSpotSpawner* Spawner = QAHot::Spawner(World, Rows.Get(), 4040);
	Spawner->MaxHotSpots = 200;
	int32 Checked = 0;
	int32 OverLand = 0;
	TArray<FString> Examples;
	for (int32 Round = 0; Round < 6; ++Round)
	{
		for (int32 Check = 0; Check < 30; ++Check)
		{
			Spawner->SpawnStep(100.f);
		}
		for (ALureHotSpot* HotSpot : QAHot::Live(World))
		{
			++Checked;
			const FLureHotSpotState& State = HotSpot->GetState();
			for (double Time = State.SpawnTime; Time < State.EndTime; Time += 0.25)
			{
				const FVector Center = HotSpot->GetCenterAt(Time);
				float WaterZ = 0.f, Depth = 0.f;
				if (!FLureWaterQuery::ProbeWater(World, FVector2D(Center.X, Center.Y), WaterZ, Depth))
				{
					++OverLand;
					if (Examples.Num() < 5)
					{
						Examples.Add(FString::Printf(TEXT("anchor (%.0f, %.0f) seed %d: center (%.0f, %.0f) is over a rock %.1f s in"), State.Anchor.X, State.Anchor.Y, State.Seed,
							Center.X, Center.Y, Time - State.SpawnTime));
					}
					break;
				}
			}
		}
		QAHot::Clear(World);
	}
	AddInfo(FString::Printf(TEXT("%d drifting hot spots checked over their 60 s life"), Checked));
	TestTrue(TEXT("enough hot spots to be meaningful"), Checked >= 30);
	TestEqual(FString::Printf(TEXT("hot spots whose center wanders over land: %s"), *FString::Join(Examples, TEXT("; "))), OverLand, 0);
	return true;
}

// =====================================================================================================================
// A cast in a hot spot
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotBonusReachesTheBite, "Project.Fishing.Water.QA.HotSpot.BonusReachesTheBite", Flags)
bool FQAHotSpotBonusReachesTheBite::RunTest(const FString& Parameters)
{
	// Spec 4/5: a cast that lands in a hot spot gets its row's bonus: luck (added), SizeBonus, ValueMultiplier and the bite wait
	// x BiteWaitScale. Same seed with and without the hot spot: the wait halves, the same fish seed and species, and the fish is
	// exactly the no-hot-spot context rolled with the bonus (the one roll pipeline). The HUD and the replicated state name it.
	AddExpectedMessagePlain(TEXT("is not imported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FLureFishingRow Shipped;
	if (!QAFishing::ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	const FLureFishingRow Profile = QAFishing::FlowProfile(Shipped, 2.f);
	const FLureHotSpotRow Hot = QAHot::BonusRow(600.f, 2.f, 0.5f, 2.f, 0.5f);
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	for (const int32 Seed : { 777, 31337, -5 })
	{
		const FString Label = FString::Printf(TEXT("seed %d"), Seed);
		const QAHot::FCastResult Plain = QAHot::CastOnce(*this, Profile, Seed, nullptr);
		const QAHot::FCastResult Boosted = QAHot::CastOnce(*this, Profile, Seed, &Hot);
		if (!Plain.bOk || !Boosted.bOk)
		{
			return false;
		}
		TestTrue(Label + TEXT(": same landing"), Plain.Rest.Equals(Boosted.Rest, 1.0));
		TestFalse(Label + TEXT(": no hot spot: no bonus"), Plain.Bonus.IsActive() || !Plain.NetHotSpot.IsNone());
		TestEqual(Label + TEXT(": the server keeps the bonus"), Boosted.Bonus.TypeId, FName(TEXT("Bubbles")));
		TestEqual(Label + TEXT(": the replicated state names the hot spot"), Boosted.NetHotSpot, FName(TEXT("Bubbles")));
		TestTrue(Label + TEXT(": the HUD line (the row's HudText)"), Boosted.Status.Contains(FLureWaterQuery::HotSpotHudText(TEXT("Bubbles"))) && !FLureWaterQuery::HotSpotHudText(TEXT("Bubbles")).IsEmpty());
		TestFalse(Label + TEXT(": no hot spot line without one"), Plain.Status.Contains(FLureWaterQuery::HotSpotHudText(TEXT("Bubbles"))));
		TestNearlyEqual(Label + TEXT(": the wait halves (BiteWaitScale 0.5)"), Boosted.Wait, Plain.Wait * 0.5, 0.02);
		TestNearlyEqual(Label + TEXT(": luck +2"), Boosted.Context.Luck, Plain.Context.Luck + 2.f, 1.0e-4f);
		TestEqual(Label + TEXT(": SizeBonus"), Boosted.Context.SizeBonus, 0.5f);
		TestEqual(Label + TEXT(": ValueMultiplier"), Boosted.Context.ValueMultiplier, 2.f);
		TestTrue(Label + TEXT(": no hot spot: neutral size and value"), Plain.Context.SizeBonus == 0.f && Plain.Context.ValueMultiplier == 1.f);
		TestTrue(Label + TEXT(": same habitat, region, time, bait"), Boosted.Context.HabitatTag == Plain.Context.HabitatTag && Boosted.Context.RegionTag == Plain.Context.RegionTag
			&& Boosted.Context.TimeOfDayHours == Plain.Context.TimeOfDayHours && Boosted.Context.BaitTag == Plain.Context.BaitTag);
		TestEqual(Label + TEXT(": the same fish seed"), Boosted.Fish.Seed, Plain.Fish.Seed);
		TestEqual(Label + TEXT(": the same species"), Boosted.Fish.SpeciesId, Plain.Fish.SpeciesId);
		TestTrue(Label + TEXT(": at least as heavy"), Boosted.Fish.WeightKg + 1.0e-4f >= Plain.Fish.WeightKg);
		// The fish is exactly the plain context rolled with the bonus.
		FFishRollContext Expected = Plain.Context;
		Expected.Luck += 2.f;
		Expected.SizeBonus = 0.5f;
		Expected.ValueMultiplier = 2.f;
		FFishInstance Again;
		TestTrue(Label + TEXT(": re-roll"), FLureFishingRules::DecideBite(Fish.Get(), Expected, Again));
		TestEqual(FString::Printf(TEXT("%s: the bite = the bonus roll (differs: %s)"), *Label,
			*FString::Join(QAFishing::DifferentFields(FFishInstance::StaticStruct(), &Again, &Boosted.Fish), TEXT(", "))),
			QAFishing::DifferentFields(FFishInstance::StaticStruct(), &Again, &Boosted.Fish).Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotBonusForWholeCast, "Project.Fishing.Water.QA.HotSpot.BonusKeptForWholeCast", Flags)
bool FQAHotSpotBonusForWholeCast::RunTest(const FString& Parameters)
{
	// Spec 5: a cast that lands in a hot spot keeps its bonus for the whole cast (every bite and rebite), even when the hot spot
	// ends before the bite; the next cast decides again (no hot spot: no bonus). The edge is the radius around the center at
	// landing: 60 cm inside counts, 60 cm outside does not.
	AddExpectedMessagePlain(TEXT("is not imported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FLureFishingRow Shipped;
	if (!QAFishing::ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	FLureFishingRow Profile = QAFishing::FlowProfile(Shipped, 3.f, 0.3f);
	Profile.RebiteWaitMin = Profile.RebiteWaitMax = 2.f;
	Profile.MissEndsCast = false;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), Profile, 12.f, 2468);
	const FLureHotSpotRow Hot = QAHot::BonusRow(500.f, 3.f, 0.25f, 1.5f, 0.5f);
	TWeakObjectPtr<ALureHotSpot> Short = QAHot::SpawnAt(Scene.World, TEXT("Bubbles"), Hot, FVector2D(1300.0, 0.0), 0.5f);
	if (!Fishing || !TestTrue(TEXT("hot spot"), Short.IsValid()) || !QAHot::Cast(*this, Scene, Fishing))
	{
		return false;
	}
	const FVector Rest(Fishing->GetNetState().BobberRest);
	TestEqual(TEXT("landed in it: bonus"), Fishing->GetHotSpotBonus().TypeId, FName(TEXT("Bubbles")));
	TestTrue(TEXT("the hot spot ends before the bite"), Scene.TickUntil([&Short]() { return !Short.IsValid() || Short->IsActorBeingDestroyed(); }, 120));
	TestEqual(TEXT("... the cast keeps the bonus"), Fishing->GetHotSpotBonus().TypeId, FName(TEXT("Bubbles")));
	TestTrue(TEXT("first bite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 600));
	TestNearlyEqual(TEXT("first bite: hot spot luck"), Fishing->GetLastRollContext().Luck, 3.f, 1.0e-4f);
	TestTrue(TEXT("first bite: size and value"), Fishing->GetLastRollContext().SizeBonus == 0.25f && Fishing->GetLastRollContext().ValueMultiplier == 1.5f);
	// Miss it (no hook press): the bobber stays, a rebite comes.
	TestTrue(TEXT("the bite is missed"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 120));
	const double MissTime = Fishing->GetNetState().StateStartTime;
	TestNearlyEqual(TEXT("the rebite wait is scaled too (2 s x 0.5)"), Fishing->GetScheduledBiteTime() - MissTime, 1.0, 0.05);
	TestTrue(TEXT("rebite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 600));
	TestNearlyEqual(TEXT("rebite: still the hot spot luck"), Fishing->GetLastRollContext().Luck, 3.f, 1.0e-4f);
	TestEqual(TEXT("rebite: still named"), Fishing->GetNetState().Water.HotSpotType, FName(TEXT("Bubbles")));
	QAHot::ReelIn(Scene, Fishing);

	// The next cast, same place, no hot spot: no bonus.
	if (!QAHot::Cast(*this, Scene, Fishing))
	{
		return false;
	}
	TestTrue(TEXT("same landing"), FVector(Fishing->GetNetState().BobberRest).Equals(Rest, 2.0));
	TestFalse(TEXT("next cast: no bonus"), Fishing->GetHotSpotBonus().IsActive());
	TestTrue(TEXT("next cast: nothing named"), Fishing->GetNetState().Water.HotSpotType.IsNone());
	TestTrue(TEXT("next cast bites"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 600));
	TestNearlyEqual(TEXT("next cast: luck 0"), Fishing->GetLastRollContext().Luck, 0.f, 1.0e-4f);
	TestTrue(TEXT("next cast: neutral size and value"), Fishing->GetLastRollContext().SizeBonus == 0.f && Fishing->GetLastRollContext().ValueMultiplier == 1.f);
	QAHot::ReelIn(Scene, Fishing);

	// The edge: radius 500 around the center.
	for (const double Offset : { 440.0, 560.0 })
	{
		QAHot::Clear(Scene.World);
		Scene.Tick(2);
		QAHot::SpawnAt(Scene.World, TEXT("Bubbles"), Hot, FVector2D(Rest.X + Offset, Rest.Y), 60.f);
		if (!QAHot::Cast(*this, Scene, Fishing))
		{
			return false;
		}
		const double Distance = FVector::Dist2D(FVector(Fishing->GetNetState().BobberRest), FVector(Rest.X + Offset, Rest.Y, 0.0));
		TestEqual(FString::Printf(TEXT("landing %.0f cm from the center (radius 500): %s"), Distance, Offset < 500.0 ? TEXT("in") : TEXT("out")),
			Fishing->GetHotSpotBonus().IsActive(), Offset < 500.0);
		QAHot::ReelIn(Scene, Fishing);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotOverlapPick, "Project.Fishing.Water.QA.HotSpot.OverlapNearestRelativeToRadius", Flags)
bool FQAHotSpotOverlapPick::RunTest(const FString& Parameters)
{
	// Spec 5: when a point is in several hot spots, the one whose center is nearest relative to its radius wins; an ended one
	// never counts; the bonus is that hot spot's.
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	UWorld* World = Scene.World;
	const FVector2D P(2000.0, 1000.0);
	ALureHotSpot* Wide = QAHot::SpawnAt(World, TEXT("QA_Wide"), QAHot::BonusRow(600.f, 1.f, 0.f, 1.f, 1.f), P + FVector2D(300.0, 0.0), 60.f);  // 0.5 of its radius
	ALureHotSpot* Tight = QAHot::SpawnAt(World, TEXT("QA_Tight"), QAHot::BonusRow(200.f, 2.f, 0.f, 1.f, 1.f), P + FVector2D(0.0, 150.0), 60.f); // 0.75 of its radius
	if (!Wide || !Tight)
	{
		AddError(TEXT("hot spots did not spawn"));
		return false;
	}
	const double Now = FLureWaterQuery::GetTime(World);
	TestTrue(TEXT("both contain the point"), Wide->ContainsAt(P, Now) && Tight->ContainsAt(P, Now));
	TestTrue(TEXT("0.5 of a radius beats 0.75 (though the tight one is nearer in cm)"), FLureWaterQuery::FindHotSpotAt(World, P, Now) == Wide);
	TestEqual(TEXT("... and its bonus"), FLureWaterQuery::FindHotSpotBonusAt(World, P, Now).TypeId, FName(TEXT("QA_Wide")));
	const FVector2D Q = P + FVector2D(0.0, 140.0); // 10 cm from the tight center (0.05), 330 cm from the wide one (0.55)
	TestTrue(TEXT("near the tight center: the tight one"), FLureWaterQuery::FindHotSpotAt(World, Q, Now) == Tight);
	TestNull(TEXT("outside both: none"), FLureWaterQuery::FindHotSpotAt(World, P + FVector2D(2000.0, 0.0), Now));
	TestFalse(TEXT("outside both: an inactive bonus"), FLureWaterQuery::FindHotSpotBonusAt(World, P + FVector2D(2000.0, 0.0), Now).IsActive());
	TestNull(TEXT("after their end: none"), FLureWaterQuery::FindHotSpotAt(World, P, Now + 61.0));
	return true;
}

// =====================================================================================================================
// Server and clients (real in-process net drivers)
// =====================================================================================================================

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotNet2PReplication, "Project.Fishing.Water.QA.Net2P.SpawnerHotSpotsAndBobberWaterReachClients", Flags)
bool FQAHotSpotNet2PReplication::RunTest(const FString& Parameters)
{
	// Spec 5 "Replication": the server's spawner makes the hot spots; each client gets the same state and computes the same drifting
	// center at the same server time; the bonus stays on the server; when the server ends one it goes on every client. A player
	// casts into a hot spot on the server: the owner and the other player get the hot spot type in the replicated fishing state,
	// and the owner's HUD shows the row's line.
	QAHot::ExpectNetHarnessNoise(*this);
	FString MovementCsv;
	FishQA::FTables Fish;
	FLureFishingRow Shipped;
	if (!QAFishing::LoadMovementCsv(*this, MovementCsv) || !FishQA::LoadReal(*this, Fish) || !QAFishing::ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Movement(QAFishing::MakeTableChecked(*this, FLureMovementRow::StaticStruct(), MovementCsv, TEXT("DT_Movement")));
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* Server = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("harness: server"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()))
	{
		return false;
	}
	for (int32 Client = 0; Client < 2; ++Client)
	{
		if (!TestTrue(FString::Printf(TEXT("harness: client %d"), Client), Worlds.CreateAndConnectClient()))
		{
			return false;
		}
	}
	QAHot::Box(Server, FVector(0.f, 0.f, 50.f), FVector(400.f, 400.f, 50.f));
	for (UE::Net::FTestWorldInstance& Client : Worlds.Clients)
	{
		QAHot::Box(Client.GetWorld(), FVector(0.f, 0.f, 50.f), FVector(400.f, 400.f, 50.f)); // each machine loads the level geometry
	}
	QAHot::CircleArea(Server, TEXT("qa_net_reef"), TEXT("Habitat.Reef"), FVector2D(4000.0, 3000.0), 1500.f);
	FLureHotSpotRow Row = QAHot::Row(0.01f, 3, {}, 0.f);
	Row.DriftRange = 300.f;
	Row.DriftSpeed = 30.f;
	Row.LifetimeMin = Row.LifetimeMax = 6.f;
	const TStrongObjectPtr<UDataTable> Rows(QAHot::Table({ { TEXT("QA_Net"), Row } }));
	ALureHotSpotSpawner* Spawner = QAHot::Spawner(Server, Rows.Get(), 60606);
	int32 Spawned = 0;
	for (int32 Check = 0; Spawner && Check < 5; ++Check)
	{
		Spawned += Spawner->SpawnStep(100.f); // at most one per row and area per check
	}
	TArray<ALureHotSpot*> ServerSpots = QAHot::Live(Server);
	const int32 InArea = ServerSpots.FilterByPredicate([](const ALureHotSpot* Spot) { return Spot->GetState().AreaId == TEXT("qa_net_reef"); }).Num();
	TestEqual(TEXT("the server spawner fills the area to MaxPerArea (3)"), InArea, 3);
	AddInfo(FString::Printf(TEXT("%d spawned (the rest in default water near the players' controllers)"), Spawned));
	auto OnClient = [&Worlds](ALureHotSpot* ServerSpot, int32 Client) -> ALureHotSpot*
	{
		if (!Worlds.IsServerObjectReplicated(ServerSpot) || !Worlds.DoesReplicatedObjectExistOnClient(ServerSpot, static_cast<uint32>(Client)))
		{
			return nullptr;
		}
		return Cast<ALureHotSpot>(Worlds.FindReplicatedObjectOnClient(ServerSpot, static_cast<uint32>(Client)));
	};
	const bool bArrived = Worlds.TickAllUntil([&]()
	{
		for (ALureHotSpot* Spot : ServerSpots)
		{
			for (int32 Client = 0; Client < 2; ++Client)
			{
				const ALureHotSpot* Copy = OnClient(Spot, Client);
				if (!Copy || Copy->GetState().EndTime <= 0.0)
				{
					return false;
				}
			}
		}
		return true;
	}, Dt, 300);
	if (!TestTrue(TEXT("every hot spot reaches both clients"), bArrived))
	{
		return false;
	}
	for (ALureHotSpot* Spot : ServerSpots)
	{
		const FLureHotSpotState& A = Spot->GetState();
		for (int32 Client = 0; Client < 2; ++Client)
		{
			ALureHotSpot* Copy = OnClient(Spot, Client);
			const FLureHotSpotState& B = Copy->GetState();
			const FString Label = FString::Printf(TEXT("%s on client %d"), *Spot->GetName(), Client);
			TestTrue(Label + TEXT(": same state"), A.TypeId == B.TypeId && A.AreaId == B.AreaId && A.Seed == B.Seed && A.Radius == B.Radius && A.DriftRange == B.DriftRange
				&& A.DriftSpeed == B.DriftSpeed && A.SpawnTime == B.SpawnTime && A.EndTime == B.EndTime && A.Color == B.Color && A.VisualClass == B.VisualClass
				&& FVector(A.Anchor).Equals(FVector(B.Anchor), 0.001));
			for (const double Offset : { 0.0, 0.5, 2.25, 5.9 })
			{
				TestTrue(FString::Printf(TEXT("%s: the same center %.2f s in"), *Label, Offset), Spot->GetCenterAt(A.SpawnTime + Offset).Equals(Copy->GetCenterAt(A.SpawnTime + Offset), 0.01));
			}
			TestTrue(Label + TEXT(": the server's anchor is on the server-quantized grid (0.1 cm)"), FMath::IsNearlyEqual(A.Anchor.X * 10.0, FMath::RoundToDouble(A.Anchor.X * 10.0), 1.0e-3)
				&& FMath::IsNearlyEqual(A.Anchor.Y * 10.0, FMath::RoundToDouble(A.Anchor.Y * 10.0), 1.0e-3));
			TestFalse(Label + TEXT(": no bonus on the client (server-only)"), Copy->GetBonus().IsActive());
			TestTrue(Label + TEXT(": the server has the bonus"), Spot->GetBonus().IsActive());
		}
	}
	TestEqual(TEXT("a client spawner never spawns"), QAHot::Spawner(Worlds.Clients[0].GetWorld(), Rows.Get(), 1)->SpawnStep(1000.f), 0);

	// A player casts into a hot spot on the server.
	ALurePlayerCharacter* Players[2] = {
		QAHot::SpawnPlayer(Server, Worlds.GetServerPlayerControllerOfClient(0), FVector(250.f, -100.f, 100.f), Movement.Get()),
		QAHot::SpawnPlayer(Server, Worlds.GetServerPlayerControllerOfClient(1), FVector(250.f, 100.f, 100.f), Movement.Get()) };
	if (!Players[0] || !Players[1])
	{
		AddError(TEXT("players did not spawn"));
		return false;
	}
	auto PlayerOn = [&Worlds](ALurePlayerCharacter* ServerCharacter, int32 Client) -> ALurePlayerCharacter*
	{
		if (!Worlds.DoesReplicatedObjectExistOnClient(ServerCharacter, static_cast<uint32>(Client)))
		{
			return nullptr;
		}
		return Cast<ALurePlayerCharacter>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(ServerCharacter), static_cast<uint32>(Client)));
	};
	if (!TestTrue(TEXT("both clients see player 0"), Worlds.TickAllUntil([&]() { return PlayerOn(Players[0], 0) && PlayerOn(Players[0], 1) && PlayerOn(Players[0], 0)->GetFishing()
		&& PlayerOn(Players[0], 1)->GetFishing(); }, Dt, 600)))
	{
		return false;
	}
	FLureFishingRow Profile = QAFishing::FlowProfile(Shipped, 30.f);
	ULureFishingComponent* ServerFishing = Players[0]->GetFishing();
	ServerFishing->SetFishingProfile(Profile);
	ServerFishing->SetFishTables(Fish.Get());
	ServerFishing->SetRandomSeed(5150);
	ServerFishing->TimeOfDayOverride = 12.f;
	ULureFishingComponent* Owner = PlayerOn(Players[0], 0)->GetFishing();
	ULureFishingComponent* Other = PlayerOn(Players[0], 1)->GetFishing();
	Owner->SetFishingProfile(Profile);
	Other->SetFishingProfile(Profile);
	Worlds.TickAll(60);
	FLureHotSpotRow Bubbles = QAHot::BonusRow(700.f, 1.f, 0.2f, 1.2f, 1.f);
	Bubbles.DriftRange = 0.f;
	ALureHotSpot* Target = ALureHotSpot::SpawnHotSpot(Server, nullptr, TEXT("Bubbles"), Bubbles, NAME_None,
		FVector(Players[0]->GetActorLocation().X + 1050.0, Players[0]->GetActorLocation().Y, 0.0), 3, FLureWaterQuery::GetTime(Server), 60.f);
	if (!TestNotNull(TEXT("a hot spot in front of player 0"), Target) || !TestTrue(TEXT("the server casts"), ServerFishing->AuthorityCast(0.5f, 0.f)))
	{
		return false;
	}
	const bool bSeen = Worlds.TickAllUntil([&]()
	{
		return ServerFishing->GetFishingState() == ELureFishingState::Waiting && Owner->GetNetState().Water.HotSpotType == TEXT("Bubbles")
			&& Other->GetNetState().Water.HotSpotType == TEXT("Bubbles");
	}, Dt, 300);
	TestEqual(TEXT("the server's cast landed in it"), ServerFishing->GetHotSpotBonus().TypeId, FName(TEXT("Bubbles")));
	TestTrue(TEXT("the owner and the other player get the hot spot type"), bSeen);
	TestTrue(TEXT("the owner's HUD shows the row's line"), Owner->GetStatusText().Contains(FLureWaterQuery::HotSpotHudText(TEXT("Bubbles"))));
	TestFalse(TEXT("the client never holds the bonus itself"), Owner->GetHotSpotBonus().IsActive());

	// The spawner's hot spots end on the server (6 s): gone on both clients.
	TArray<TWeakObjectPtr<ALureHotSpot>> Copies;
	for (ALureHotSpot* Spot : ServerSpots)
	{
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Copies.Add(OnClient(Spot, Client));
		}
	}
	const bool bGone = Worlds.TickAllUntil([&Copies]()
	{
		for (const TWeakObjectPtr<ALureHotSpot>& Copy : Copies)
		{
			if (Copy.IsValid() && !Copy->IsActorBeingDestroyed())
			{
				return false;
			}
		}
		return true;
	}, Dt, 900);
	TestTrue(TEXT("ended hot spots disappear on every client"), bGone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotNet2PDevCommandsOnClient, "Project.Fishing.Water.QA.Net2P.DevCommandsOnClientAreSafe", Flags)
bool FQAHotSpotNet2PDevCommandsOnClient::RunTest(const FString& Parameters)
{
	// Spec 10: Lure.HotSpot.Spawn/Clear are server/standalone only; Lure.Water.Show/Probe read. Typed on a client (with or without
	// a pawn, with odd arguments), none may crash, spawn a client-only hot spot, or destroy the client's replicated copy (that would
	// desync it from the server). On the dedicated server (no local player) they don't crash either.
	QAHot::ExpectNetHarnessNoise(*this);
	FString MovementCsv;
	if (!QAFishing::LoadMovementCsv(*this, MovementCsv))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Movement(QAFishing::MakeTableChecked(*this, FLureMovementRow::StaticStruct(), MovementCsv, TEXT("DT_Movement")));
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* Server = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("harness: server"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()) || !TestTrue(TEXT("harness: client"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	UWorld* Client = Worlds.Clients[0].GetWorld();
	QAHot::Box(Server, FVector(0.f, 0.f, 50.f), FVector(400.f, 400.f, 50.f));
	QAHot::Box(Client, FVector(0.f, 0.f, 50.f), FVector(400.f, 400.f, 50.f));
	ALureHotSpot* ServerSpot = QAHot::SpawnAt(Server, TEXT("Bubbles"), QAHot::BonusRow(300.f, 1.f, 0.f, 1.f, 1.f), FVector2D(1500.0, 0.0), 120.f);
	ALureHotSpot* Copy = nullptr;
	const bool bArrived = Worlds.TickAllUntil([&]()
	{
		if (!Worlds.IsServerObjectReplicated(ServerSpot) || !Worlds.DoesReplicatedObjectExistOnClient(ServerSpot, 0))
		{
			return false;
		}
		Copy = Cast<ALureHotSpot>(Worlds.FindReplicatedObjectOnClient(ServerSpot, 0));
		return Copy != nullptr;
	}, Dt, 300);
	if (!TestTrue(TEXT("the hot spot is on the client"), bArrived))
	{
		return false;
	}
	const TArray<const TCHAR*> Commands = {
		TEXT("Lure.Water.Show"), TEXT("Lure.Water.Show 0"), TEXT("Lure.Water.Show -5"), TEXT("Lure.Water.Show abc"),
		TEXT("Lure.Water.Probe"), TEXT("Lure.Water.Probe 100000"), TEXT("Lure.Water.Probe -1"),
		TEXT("Lure.HotSpot.Spawn"), TEXT("Lure.HotSpot.Spawn Bubbles 1000 30"), TEXT("Lure.HotSpot.Spawn QA_NoSuchType"), TEXT("Lure.HotSpot.Spawn Bubbles -1000 -3"),
		TEXT("Lure.HotSpot.Spawn Bubbles nan inf"), TEXT("Lure.HotSpot.Clear"), TEXT("Lure.HotSpot.Clear extra args") };
	auto RunAll = [&](UWorld* World, const TCHAR* Where)
	{
		for (const TCHAR* Command : Commands)
		{
			const bool bHandled = IConsoleManager::Get().ProcessUserConsoleInput(Command, *GLog, World);
			TestTrue(FString::Printf(TEXT("%s: '%s' is a known command"), Where, Command), bHandled);
			Worlds.TickAll(2);
		}
	};
	const int32 ClientBefore = QAHot::Live(Client).Num();
	RunAll(Client, TEXT("client without a pawn"));
	TestEqual(TEXT("client without a pawn: no client-only hot spot"), QAHot::Live(Client).Num(), ClientBefore);
	TestTrue(TEXT("... and the replicated copy is still there"), IsValid(Copy) && !Copy->IsActorBeingDestroyed());
	TestEqual(TEXT("... and the server still has exactly its one"), QAHot::Live(Server).Num(), 1);

	// With a pawn for the client's player.
	ALurePlayerCharacter* Player = QAHot::SpawnPlayer(Server, Worlds.GetServerPlayerControllerOfClient(0), FVector(250.f, 0.f, 100.f), Movement.Get());
	Worlds.TickAllUntil([&]() { return Player && Worlds.DoesReplicatedObjectExistOnClient(Player, 0); }, Dt, 600);
	Worlds.TickAll(30);
	RunAll(Client, TEXT("client with a pawn"));
	TestEqual(TEXT("client with a pawn: no client-only hot spot"), QAHot::Live(Client).Num(), ClientBefore);
	TestTrue(TEXT("... the replicated copy survives Lure.HotSpot.Clear on the client"), IsValid(Copy) && !Copy->IsActorBeingDestroyed());
	FString Message;
	TestNull(TEXT("SpawnHotSpotInFront on a client: refused"), FLureWaterDevCommands::SpawnHotSpotInFront(Client, NAME_None, 1000.f, 30.f, Message));
	TestFalse(TEXT("... with a message"), Message.IsEmpty());
	TestEqual(TEXT("ClearHotSpots on a client: 0"), FLureWaterDevCommands::ClearHotSpots(Client), 0);
	TestTrue(TEXT("Show on a client lists the replicated hot spot"), FLureWaterDevCommands::DescribeWater(Client, 0.f).Contains(TEXT("1 hot spot")));
	FLureWaterDevCommands::ProbeInFront(Client, 1000.f);

	// The dedicated server has no local player: the commands fail gracefully.
	RunAll(Server, TEXT("dedicated server"));
	FLureWaterDevCommands::SpawnHotSpotInFront(Server, NAME_None, 1000.f, 30.f, Message); // may use a remote player's pawn: server authority, fine
	Worlds.TickAll(5);
	TestTrue(TEXT("server: still running after every command"), IsValid(Server) && Server->GetNetDriver() != nullptr);
	return true;
}

#endif // WITH_EDITOR

} // namespace LureWaterQA

#endif // WITH_DEV_AUTOMATION_TESTS

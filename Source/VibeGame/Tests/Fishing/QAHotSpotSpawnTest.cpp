// Lure T-027c QA (qa-engineer): the hot-spot spawner's per-(row, area) clock - it banks waiting time (capped at
// HotSpotPrewarmSeconds) while nobody can reach an area, rolls the whole bank when a player arrives, draws candidates only
// within a player's reach, retries a failed placement for DueRetryChecks checks, is server-only and seeded.
// Project.Fishing.Water.QA.HotSpotSpawn.* - spec: docs/specs/fishing-water-rules.md sections 5 and 8. Black-box: expectations
// come from the spec and the header contract (Fishing/LureHotSpotSpawner.h), never from the .cpp.
// Everything lives in namespace LureWaterQA::QASpawn (unity builds merge test files: no file-scope using-directives).

#include "Tests/Fishing/QAFishingWaterTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fishing/LureFishingSettings.h"
#include "Fishing/LureHotSpotSpawner.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_EDITOR
#include "Tests/NetTestHelpers.h"
#endif

namespace LureWaterQA
{
namespace QASpawn
{
	/** Pins every spawn setting to its spec value (section 8) for one test, restores the project's values after. */
	struct FSettingsGuard
	{
		float Near, Prewarm, OpenRadius, Check;
		int32 Tries, Max;

		FSettingsGuard()
		{
			ULureWaterSettings* S = GetMutableDefault<ULureWaterSettings>();
			Near = S->HotSpotNearPlayerRadius;
			Prewarm = S->HotSpotPrewarmSeconds;
			OpenRadius = S->OpenWaterSpawnRadius;
			Check = S->HotSpotCheckInterval;
			Tries = S->HotSpotSpawnTries;
			Max = S->MaxHotSpots;
			S->HotSpotNearPlayerRadius = 2500.f;
			S->HotSpotPrewarmSeconds = 45.f;
			S->OpenWaterSpawnRadius = 1800.f;
			S->HotSpotCheckInterval = 1.f;
			S->HotSpotSpawnTries = 12;
			S->MaxHotSpots = 16;
		}

		~FSettingsGuard()
		{
			ULureWaterSettings* S = GetMutableDefault<ULureWaterSettings>();
			S->HotSpotNearPlayerRadius = Near;
			S->HotSpotPrewarmSeconds = Prewarm;
			S->OpenWaterSpawnRadius = OpenRadius;
			S->HotSpotCheckInterval = Check;
			S->HotSpotSpawnTries = Tries;
			S->MaxHotSpots = Max;
		}
	};

	/** A spawner test row: explicit numbers, no drift, small disc (not the shipped values). */
	inline FLureHotSpotRow SpawnRow(float SpawnInterval, int32 MaxPerArea, std::initializer_list<const TCHAR*> Habitats)
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
		Out.MinDepth = 60.f;
		Out.MaxDepth = 0.f;
		Out.AllowedHabitats.Reset();
		for (const TCHAR* Habitat : Habitats)
		{
			Out.AllowedHabitats.Add(Tag(Habitat));
		}
		return Out;
	}

	inline UDataTable* RowTable(const TArray<TPair<FName, FLureHotSpotRow>>& Rows)
	{
		UDataTable* Out = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Out->RowStruct = FLureHotSpotRow::StaticStruct();
		for (const TPair<FName, FLureHotSpotRow>& Entry : Rows)
		{
			Out->AddRow(Entry.Key, Entry.Value);
		}
		return Out;
	}

	inline ALureHotSpotSpawner* MakeSpawner(UWorld* World, const UDataTable* Rows, int32 Seed, bool bAuto = false)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureHotSpotSpawner* Out = World->SpawnActor<ALureHotSpotSpawner>(ALureHotSpotSpawner::StaticClass(), FTransform::Identity, Params);
		if (Out)
		{
			Out->SetAutoSpawn(bAuto);
			Out->SetHotSpotTable(Rows);
			Out->SetRandomSeed(Seed);
		}
		return Out;
	}

	inline ALureWaterArea* Circle(UWorld* World, const TCHAR* Id, const TCHAR* Habitat, const FVector2D& Center, float Radius, int32 Priority = 0)
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

	/** An unbounded ("everywhere") area: no outline, so its reach needs a player (spec 5). */
	inline ALureWaterArea* Everywhere(UWorld* World, const TCHAR* Id, const TCHAR* Habitat, int32 Priority)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureWaterArea* Area = World->SpawnActor<ALureWaterArea>(ALureWaterArea::StaticClass(), FTransform::Identity, Params);
		if (Area)
		{
			Area->AreaId = FName(Id);
			Area->Priority = Priority;
			Area->SetAreaTags(FName(Habitat), NAME_None);
			Area->SetShapeEverywhere();
		}
		return Area;
	}

	inline AActor* Block(UWorld* World, const FVector& Center, const FVector& Extent)
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

	/** The sea floor 5 m under the fallback sea surface (z = 0) over x in [-11000, 11000], y in [-11000, 11000]. */
	inline void Seabed(UWorld* World)
	{
		Block(World, FVector(0.f, 0.f, -550.f), FVector(11000.f, 11000.f, 50.f));
	}

	/** Land: a block from the seabed to 40 cm above the sea, HalfXY wide, centred at XY. */
	inline AActor* Island(UWorld* World, const FVector2D& XY, float HalfXY)
	{
		return Block(World, FVector(XY.X, XY.Y, -230.f), FVector(HalfXY, HalfXY, 270.f));
	}

	inline TArray<ALureHotSpot*> LiveSpots(UWorld* World)
	{
		return ALureHotSpotSpawner::GetLiveHotSpots(World, FLureWaterQuery::GetTime(World));
	}

	inline void ClearSpots(UWorld* World)
	{
		for (TActorIterator<ALureHotSpot> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				It->Destroy();
			}
		}
	}

	inline void Move(ALurePlayerCharacter* Player, double X, double Y)
	{
		Player->SetActorLocation(FVector(X, Y, 100.0), false, nullptr, ETeleportType::TeleportPhysics);
	}

	inline double Dist2D(const ALureHotSpot* Spot, const ALurePlayerCharacter* Player)
	{
		return FVector2D::Distance(FVector2D(Spot->GetState().Anchor.X, Spot->GetState().Anchor.Y), FVector2D(Player->GetActorLocation()));
	}

	/** One line per live hot spot (type, area, anchor to 1 cm, drift seed, lifetime to 1 ms), in the stable live order. */
	inline FString Fingerprint(UWorld* World)
	{
		FString Out;
		for (const ALureHotSpot* Spot : LiveSpots(World))
		{
			const FLureHotSpotState& S = Spot->GetState();
			Out += FString::Printf(TEXT("%s|%s|%.0f,%.0f|%d|%.3f\n"), *S.TypeId.ToString(), *S.AreaId.ToString(), S.Anchor.X, S.Anchor.Y, S.Seed,
				S.EndTime - S.SpawnTime);
		}
		return Out;
	}

	inline const TCHAR* ReefRow = TEXT("QA_Reef");
	inline const TCHAR* ReefArea = TEXT("qa_reef");
}

// =====================================================================================================================
// Settings (spec section 8)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnSettings, "Project.Fishing.Water.QA.HotSpotSpawn.SettingsMatchSpec", Flags)
bool FQAHotSpotSpawnSettings::RunTest(const FString& Parameters)
{
	// Spec 8: the shipped project values (config included) of the spawn tuning, and the retry limit (spec 5: "the pair's next 3
	// checks with a player in reach retry it").
	const ULureWaterSettings* S = GetDefault<ULureWaterSettings>();
	TestEqual(TEXT("HotSpotCheckInterval 1 s"), S->HotSpotCheckInterval, 1.f);
	TestEqual(TEXT("HotSpotPrewarmSeconds 45 s (the bank cap)"), S->HotSpotPrewarmSeconds, 45.f);
	TestEqual(TEXT("MaxHotSpots 16"), S->MaxHotSpots, 16);
	TestEqual(TEXT("OpenWaterSpawnRadius 1800 cm"), S->OpenWaterSpawnRadius, 1800.f);
	TestEqual(TEXT("HotSpotNearPlayerRadius 2500 cm"), S->HotSpotNearPlayerRadius, 2500.f);
	TestEqual(TEXT("HotSpotSpawnTries 12"), S->HotSpotSpawnTries, 12);
	TestEqual(TEXT("DueRetryChecks 3"), ALureHotSpotSpawner::DueRetryChecks, 3);
	TestTrue(TEXT("open-water spawns stay inside the near-player reach"), S->OpenWaterSpawnRadius <= S->HotSpotNearPlayerRadius);
	return true;
}

// =====================================================================================================================
// The bank (accrual) and its cap
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnBankCap, "Project.Fishing.Water.QA.HotSpotSpawn.BankCapsAtPrewarmWhileOutOfReach", Flags)
bool FQAHotSpotSpawnBankCap::RunTest(const FString& Parameters)
{
	// Spec 5: while nobody can reach an area the pair waits, banking each check's seconds up to max(check, HotSpotPrewarmSeconds);
	// nothing spawns meanwhile. Boundaries 44/45/46 one-second checks; 200 checks stays at 45; one 100 s check banks 100 (the
	// check itself is the cap), a second one does not add; a different HotSpotPrewarmSeconds moves the cap.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QASpawn::Seabed(Scene.World);
	QASpawn::Circle(Scene.World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(6000.0, 0.0), 1000.f);
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({ { QASpawn::ReefRow, QASpawn::SpawnRow(0.01f, 3, { TEXT("Habitat.Reef") }) } }));
	ALurePlayerCharacter* Player = Scene.Spawn(*this, FVector(-5000.f, 0.f, 100.f)); // 100 m from the outline (x = 5000)
	ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 101);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("spawner"), Spawner))
	{
		return false;
	}
	const FName Row(QASpawn::ReefRow), Area(QASpawn::ReefArea);
	TestEqual(TEXT("a new spawner has banked nothing"), Spawner->GetBankedSeconds(Row, Area), 0.f);

	int32 Spawned = 0, OutOfReachChecks = 0, Rolls = 0;
	TMap<int32, float> BankAt;
	for (int32 Check = 1; Check <= 200; ++Check)
	{
		Spawned += Spawner->SpawnStep(1.f);
		OutOfReachChecks += Spawner->GetLastStepStats().OutOfReach;
		Rolls += Spawner->GetLastStepStats().Rolls;
		if (Check == 1 || Check == 10 || Check == 44 || Check == 45 || Check == 46 || Check == 200)
		{
			BankAt.Add(Check, Spawner->GetBankedSeconds(Row, Area));
		}
	}
	TestEqual(TEXT("out of reach: nothing spawned in 200 checks"), Spawned, 0);
	TestEqual(TEXT("out of reach: no roll in 200 checks"), Rolls, 0);
	TestEqual(TEXT("out of reach: the pair waits (OutOfReach) every check"), OutOfReachChecks, 200);
	TestNearlyEqual(TEXT("bank after 1 check = 1 s"), BankAt.FindRef(1), 1.f, 1e-3f);
	TestNearlyEqual(TEXT("bank after 10 checks = 10 s"), BankAt.FindRef(10), 10.f, 1e-3f);
	TestNearlyEqual(TEXT("bank after 44 checks = 44 s (just under the cap)"), BankAt.FindRef(44), 44.f, 1e-3f);
	TestNearlyEqual(TEXT("bank after 45 checks = 45 s (the cap)"), BankAt.FindRef(45), 45.f, 1e-3f);
	TestNearlyEqual(TEXT("bank after 46 checks stays 45 s (just over)"), BankAt.FindRef(46), 45.f, 1e-3f);
	TestNearlyEqual(TEXT("bank after 200 checks stays 45 s"), BankAt.FindRef(200), 45.f, 1e-3f);

	// A check longer than the prewarm is itself the cap: max(check, prewarm).
	ALureHotSpotSpawner* Long = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 102);
	Long->SpawnStep(100.f);
	TestNearlyEqual(TEXT("one 100 s check banks 100 s (cap = max(check, 45))"), Long->GetBankedSeconds(Row, Area), 100.f, 1e-3f);
	Long->SpawnStep(100.f);
	TestNearlyEqual(TEXT("a second 100 s check does not add (cap 100)"), Long->GetBankedSeconds(Row, Area), 100.f, 1e-3f);
	TestEqual(TEXT("still nothing spawned"), QASpawn::LiveSpots(Scene.World).Num(), 0);

	// The cap is the setting, not a constant.
	GetMutableDefault<ULureWaterSettings>()->HotSpotPrewarmSeconds = 10.f;
	ALureHotSpotSpawner* Short = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 103);
	for (int32 Check = 0; Check < 30; ++Check)
	{
		Short->SpawnStep(1.f);
	}
	TestNearlyEqual(TEXT("HotSpotPrewarmSeconds 10: the bank caps at 10 s"), Short->GetBankedSeconds(Row, Area), 10.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnBankWaitsFull, "Project.Fishing.Water.QA.HotSpotSpawn.FullAreaResetsLevelFullBanks", Flags)
bool FQAHotSpotSpawnBankWaitsFull::RunTest(const FString& Parameters)
{
	// Spec 5: a pair whose area holds MaxPerArea of its row resets its clock (nothing banks while full) and starts again when one
	// ends; a pair waiting because the level is at its max keeps banking (up to the cap) and rolls when there is room.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QASpawn::Seabed(Scene.World);
	QASpawn::Circle(Scene.World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(6000.0, 0.0), 1500.f);
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({ { QASpawn::ReefRow, QASpawn::SpawnRow(0.001f, 1, { TEXT("Habitat.Reef") }) } }));
	ALurePlayerCharacter* Player = Scene.Spawn(*this, FVector(5000.f, 0.f, 100.f)); // inside the area
	ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 202);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("spawner"), Spawner))
	{
		return false;
	}
	const FName Row(QASpawn::ReefRow), Area(QASpawn::ReefArea);

	// Level full: MaxHotSpots 0 on the spawner.
	Spawner->MaxHotSpots = 0;
	int32 LevelFull = 0, Spawned = 0;
	for (int32 Check = 0; Check < 60; ++Check)
	{
		Spawned += Spawner->SpawnStep(1.f);
		LevelFull += Spawner->GetLastStepStats().LevelFull;
	}
	TestEqual(TEXT("level at its max: nothing spawns"), Spawned, 0);
	TestEqual(TEXT("level at its max: the pair waits (LevelFull) every check"), LevelFull, 60);
	TestNearlyEqual(TEXT("level at its max: the pair banks up to the cap (45 s)"), Spawner->GetBankedSeconds(Row, Area), 45.f, 1e-3f);

	// Room again: it rolls its bank and (chance ~1) spawns.
	Spawner->MaxHotSpots = 10;
	const int32 First = Spawner->SpawnStep(1.f);
	TestEqual(TEXT("room again: the pair rolls"), Spawner->GetLastStepStats().Rolls, 1);
	TestEqual(TEXT("room again: one hot spot"), First, 1);
	TestEqual(TEXT("after the roll the bank is empty"), Spawner->GetBankedSeconds(Row, Area), 0.f);

	// Full (MaxPerArea 1): no banking, however long.
	int32 FullChecks = 0;
	float MaxBank = 0.f;
	for (int32 Check = 0; Check < 20; ++Check)
	{
		Spawner->SpawnStep(1.f);
		FullChecks += Spawner->GetLastStepStats().Full;
		MaxBank = FMath::Max(MaxBank, Spawner->GetBankedSeconds(Row, Area));
	}
	TestEqual(TEXT("area full: the pair is Full every check"), FullChecks, 20);
	TestEqual(TEXT("area full: the clock stays reset (0 s banked)"), MaxBank, 0.f);

	// One ends: the clock starts again from 0 (a 1 s bank, not 20 s).
	QASpawn::ClearSpots(Scene.World);
	Spawner->MaxHotSpots = 0; // hold it at LevelFull so the bank can be read without a roll
	Spawner->SpawnStep(1.f);
	TestNearlyEqual(TEXT("after the hot spot ends the clock restarts from zero (1 check = 1 s)"), Spawner->GetBankedSeconds(Row, Area), 1.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnBurstCapped, "Project.Fishing.Water.QA.HotSpotSpawn.ArrivalBurstIsCappedAtPrewarm", Flags)
bool FQAHotSpotSpawnBurstCapped::RunTest(const FString& Parameters)
{
	// Spec 5: on arrival the pair rolls its whole bank once, chance 1 - exp(-bank / SpawnInterval). With SpawnInterval 60 s:
	// after 150 s away the bank is capped at 45 s -> p = 0.528 (uncapped 150 s would be 0.918); after 20 s away -> p = 0.283.
	// 200 fixed seeds per group; RollsPassed counts passed rolls whether or not a point is then found.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QASpawn::Seabed(Scene.World);
	QASpawn::Circle(Scene.World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(6000.0, 0.0), 1000.f);
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({ { QASpawn::ReefRow, QASpawn::SpawnRow(60.f, 3, { TEXT("Habitat.Reef") }) } }));
	ALurePlayerCharacter* Player = Scene.Spawn(*this, FVector(0.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	const FName Row(QASpawn::ReefRow), Area(QASpawn::ReefArea);
	constexpr int32 Trials = 200;

	auto Group = [&](int32 WaitChecks, int32 SeedBase, int32& OutBankErrors) -> int32
	{
		int32 Passed = 0;
		for (int32 Trial = 0; Trial < Trials; ++Trial)
		{
			QASpawn::Move(Player, 0.0, 0.0); // 50 m from the outline
			ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(Scene.World, Rows.Get(), SeedBase + Trial);
			for (int32 Check = 0; Check < WaitChecks; ++Check)
			{
				Spawner->SpawnStep(1.f);
			}
			const float Expected = FMath::Min<float>(WaitChecks, 45.f);
			if (!FMath::IsNearlyEqual(Spawner->GetBankedSeconds(Row, Area), Expected, 1e-3f))
			{
				++OutBankErrors;
			}
			QASpawn::Move(Player, 4500.0, 0.0); // 5 m from the outline: in reach
			Spawner->SpawnStep(1.f);
			const FLureHotSpotStepStats& Stats = Spawner->GetLastStepStats();
			if (Stats.Rolls != 1 || Spawner->GetBankedSeconds(Row, Area) != 0.f)
			{
				++OutBankErrors; // exactly one roll of the whole bank, which empties it
			}
			Passed += Stats.RollsPassed;
			QASpawn::ClearSpots(Scene.World);
			Spawner->Destroy();
		}
		return Passed;
	};
	int32 LongErrors = 0, ShortErrors = 0;
	const int32 LongPassed = Group(150, 10000, LongErrors);
	const int32 ShortPassed = Group(20, 20000, ShortErrors);
	const double LongRate = double(LongPassed) / Trials, ShortRate = double(ShortPassed) / Trials;
	AddInfo(FString::Printf(TEXT("arrival pass rate after 150 s away %.3f (expect 0.528), after 20 s away %.3f (expect 0.283)"), LongRate, ShortRate));
	TestEqual(TEXT("150 s away: every trial banked exactly 45 s and rolled it once"), LongErrors, 0);
	TestEqual(TEXT("20 s away: every trial banked exactly 20 s and rolled it once"), ShortErrors, 0);
	TestTrue(TEXT("150 s away: the burst chance is the capped 45 s one (0.40..0.66), not 150 s (0.92)"), LongRate >= 0.40 && LongRate <= 0.66);
	TestTrue(TEXT("20 s away: the burst chance is the 20 s one (0.17..0.40)"), ShortRate >= 0.17 && ShortRate <= 0.40);
	return true;
}

// =====================================================================================================================
// Reach
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnReachBoundary, "Project.Fishing.Water.QA.HotSpotSpawn.ReachBoundaryAndArrival", Flags)
bool FQAHotSpotSpawnReachBoundary::RunTest(const FString& Parameters)
{
	// Spec 5: a bounded area is reachable when a player is within HotSpotNearPlayerRadius (2500) of its outline. 2510 cm: it waits
	// (OutOfReach), nothing spawns, however long; 2490 cm: it rolls. When the player then walks up, hot spots appear within
	// 2500 cm of that player.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QASpawn::Seabed(Scene.World);
	QASpawn::Circle(Scene.World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(6000.0, 0.0), 1000.f); // outline at x = 5000
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({ { QASpawn::ReefRow, QASpawn::SpawnRow(0.001f, 3, { TEXT("Habitat.Reef") }) } }));
	ALurePlayerCharacter* Player = Scene.Spawn(*this, FVector(5000.f - 2510.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	const FName Row(QASpawn::ReefRow), Area(QASpawn::ReefArea);

	ALureHotSpotSpawner* Out = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 301);
	int32 Spawned = 0, OutOfReach = 0, Rolls = 0;
	for (int32 Check = 0; Check < 60; ++Check)
	{
		Spawned += Out->SpawnStep(1.f);
		OutOfReach += Out->GetLastStepStats().OutOfReach;
		Rolls += Out->GetLastStepStats().Rolls;
	}
	TestEqual(TEXT("2510 cm from the outline: 60 checks OutOfReach"), OutOfReach, 60);
	TestEqual(TEXT("2510 cm from the outline: no roll"), Rolls, 0);
	TestEqual(TEXT("2510 cm from the outline: nothing spawned"), Spawned, 0);
	TestEqual(TEXT("the stats count the player"), Out->GetLastStepStats().Players, 1);
	Out->Destroy();

	QASpawn::Move(Player, 5000.0 - 2490.0, 0.0);
	ALureHotSpotSpawner* In = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 302);
	In->SpawnStep(1.f);
	TestEqual(TEXT("2490 cm from the outline: in reach (not OutOfReach)"), In->GetLastStepStats().OutOfReach, 0);
	TestEqual(TEXT("2490 cm from the outline: the pair rolls"), In->GetLastStepStats().Rolls, 1);
	for (const ALureHotSpot* Spot : QASpawn::LiveSpots(Scene.World))
	{
		TestTrue(TEXT("a sliver spawn is still within 2500 cm of the player"), QASpawn::Dist2D(Spot, Player) <= 2500.0 + 0.2);
	}
	QASpawn::ClearSpots(Scene.World);
	In->Destroy();

	// Wait out of reach, then arrive: spawns (MaxPerArea 3) appear near the arriving player.
	QASpawn::Move(Player, 0.0, 0.0);
	ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 303);
	for (int32 Check = 0; Check < 30; ++Check)
	{
		Spawner->SpawnStep(1.f);
	}
	TestEqual(TEXT("away: none"), QASpawn::LiveSpots(Scene.World).Num(), 0);
	QASpawn::Move(Player, 4200.0, 0.0); // 8 m from the outline
	for (int32 Check = 0; Check < 10; ++Check)
	{
		Spawner->SpawnStep(1.f);
	}
	const TArray<ALureHotSpot*> Live = QASpawn::LiveSpots(Scene.World);
	TestEqual(TEXT("arrived: the area fills to MaxPerArea (3)"), Live.Num(), 3);
	for (const ALureHotSpot* Spot : Live)
	{
		TestEqual(TEXT("in the reef area"), Spot->GetState().AreaId, Area);
		TestTrue(TEXT("within 2500 cm of the arriving player"), QASpawn::Dist2D(Spot, Player) <= 2500.0 + 0.2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnNoPlayers, "Project.Fishing.Water.QA.HotSpotSpawn.NoPlayersUnboundedWaits", Flags)
bool FQAHotSpotSpawnNoPlayers::RunTest(const FString& Parameters)
{
	// Spec 5: with no player in the world the near rule is skipped for bounded areas (they spawn anywhere in their outline), but
	// unbounded water ("everywhere" areas, the default water) needs a player: it waits and banks, nothing spawns there. When a
	// player appears, the unbounded pair spawns within OpenWaterSpawnRadius (1800) of them.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	QASpawn::Seabed(Scene.World);
	QASpawn::Circle(Scene.World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(6000.0, 0.0), 1500.f);
	QASpawn::Everywhere(Scene.World, TEXT("qa_open"), TEXT("Habitat.Lagoon"), -5);
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({
		{ QASpawn::ReefRow, QASpawn::SpawnRow(0.001f, 2, { TEXT("Habitat.Reef") }) },
		{ TEXT("QA_Open"), QASpawn::SpawnRow(0.001f, 2, { TEXT("Habitat.Lagoon") }) } }));
	ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(Scene.World, Rows.Get(), 401);
	if (!TestNotNull(TEXT("spawner"), Spawner))
	{
		return false;
	}
	for (int32 Check = 0; Check < 20; ++Check)
	{
		Spawner->SpawnStep(1.f);
	}
	if (!TestEqual(TEXT("setup: the stats see no player"), Spawner->GetLastStepStats().Players, 0))
	{
		return false;
	}
	auto CountIn = [&Scene](FName AreaId)
	{
		return QASpawn::LiveSpots(Scene.World).FilterByPredicate([AreaId](const ALureHotSpot* Spot) { return Spot->GetState().AreaId == AreaId; }).Num();
	};
	TestEqual(TEXT("no players: the bounded area still fills (2)"), CountIn(FName(QASpawn::ReefArea)), 2);
	TestEqual(TEXT("no players: nothing in the unbounded area"), CountIn(TEXT("qa_open")), 0);
	TestNearlyEqual(TEXT("no players: the unbounded pair keeps banking (20 checks = 20 s)"), Spawner->GetBankedSeconds(TEXT("QA_Open"), TEXT("qa_open")), 20.f, 1e-3f);

	ALurePlayerCharacter* Player = Scene.Spawn(*this, FVector(-3000.f, 2000.f, 100.f)); // far from the reef, on open water
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	for (int32 Check = 0; Check < 10; ++Check)
	{
		Spawner->SpawnStep(1.f);
	}
	TestEqual(TEXT("a player on open water: the unbounded area fills (2)"), CountIn(TEXT("qa_open")), 2);
	for (const ALureHotSpot* Spot : QASpawn::LiveSpots(Scene.World))
	{
		if (Spot->GetState().AreaId == TEXT("qa_open"))
		{
			TestTrue(TEXT("open-water spawn within OpenWaterSpawnRadius (1800 cm) of the player"), QASpawn::Dist2D(Spot, Player) <= 1800.0 + 0.2);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnPointsValid, "Project.Fishing.Water.QA.HotSpotSpawn.EveryPointInReachOnFishableWater", Flags)
bool FQAHotSpotSpawnPointsValid::RunTest(const FString& Parameters)
{
	// Spec 5: every spawned point is within HotSpotNearPlayerRadius of some player, on fishable water (a water surface, no ground
	// above it) at least MinBiteDepth and the row's MinDepth deep, and its winning area is the hot spot's area; unbounded water
	// spawns within OpenWaterSpawnRadius of a player. Two players at opposite sides of a big reef, islands inside their reach,
	// an unbounded lagoon around it; 6 rounds of 10 checks, cleared between rounds.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	UWorld* World = Scene.World;
	QASpawn::Seabed(World);
	QASpawn::Circle(World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 4000.f);
	QASpawn::Everywhere(World, TEXT("qa_open"), TEXT("Habitat.Lagoon"), -5);
	QASpawn::Island(World, FVector2D(2600.0, -2000.0), 500.f);
	QASpawn::Island(World, FVector2D(1600.0, -4200.0), 400.f);
	QASpawn::Island(World, FVector2D(8000.0, 3600.0), 500.f);
	FLureHotSpotRow Reef = QASpawn::SpawnRow(0.5f, 5, { TEXT("Habitat.Reef") });
	Reef.DriftRange = 200.f;
	Reef.DriftSpeed = 10.f;
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({
		{ QASpawn::ReefRow, Reef },
		{ TEXT("QA_Open"), QASpawn::SpawnRow(0.5f, 4, { TEXT("Habitat.Lagoon") }) } }));
	ALurePlayerCharacter* Players[2] = { Scene.Spawn(*this, FVector(1500.f, -3000.f, 100.f)), Scene.Spawn(*this, FVector(8500.f, 3000.f, 100.f)) };
	ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(World, Rows.Get(), 501);
	if (!TestNotNull(TEXT("player 0"), Players[0]) || !TestNotNull(TEXT("player 1"), Players[1]) || !TestNotNull(TEXT("spawner"), Spawner))
	{
		return false;
	}
	Spawner->MaxHotSpots = 50;
	const float MinBite = GetDefault<ULureWaterSettings>()->MinBiteDepth;
	const TArray<FLureWaterAreaInfo> Areas = FLureWaterQuery::GatherAreas(World);
	int32 Checked = 0, OutOfReach = 0, NotWater = 0, Shallow = 0, WrongArea = 0, OpenFar = 0;
	int32 NearPlayer[2] = { 0, 0 };
	for (int32 Round = 0; Round < 6; ++Round)
	{
		for (int32 Check = 0; Check < 10; ++Check)
		{
			Spawner->SpawnStep(1.f);
		}
		for (const ALureHotSpot* Spot : QASpawn::LiveSpots(World))
		{
			++Checked;
			const FVector2D XY(Spot->GetState().Anchor.X, Spot->GetState().Anchor.Y);
			const double D0 = QASpawn::Dist2D(Spot, Players[0]), D1 = QASpawn::Dist2D(Spot, Players[1]);
			const double Nearest = FMath::Min(D0, D1);
			NearPlayer[D0 <= D1 ? 0 : 1]++;
			OutOfReach += Nearest > 2500.0 + 0.2 ? 1 : 0;
			float WaterZ = 0.f, Depth = 0.f;
			if (!FLureWaterQuery::ProbeWater(World, XY, WaterZ, Depth))
			{
				++NotWater;
				AddError(FString::Printf(TEXT("hot spot at (%.0f, %.0f) is not on water"), XY.X, XY.Y));
				continue;
			}
			Shallow += (Depth < MinBite || Depth < 60.f) ? 1 : 0;
			const int32 Winner = FLureWaterRules::FindAreaIndex(Areas, XY, Depth);
			const FName WinnerId = Winner == INDEX_NONE ? NAME_None : Areas[Winner].AreaId;
			if (WinnerId != Spot->GetState().AreaId)
			{
				++WrongArea;
				AddError(FString::Printf(TEXT("hot spot of area %s at (%.0f, %.0f): the winning area there is %s"), *Spot->GetState().AreaId.ToString(), XY.X, XY.Y,
					*WinnerId.ToString()));
			}
			if (Spot->GetState().AreaId == TEXT("qa_open") && Nearest > 1800.0 + 0.2)
			{
				++OpenFar;
			}
		}
		QASpawn::ClearSpots(World);
	}
	AddInfo(FString::Printf(TEXT("%d hot spots checked (%d nearest player 0, %d nearest player 1)"), Checked, NearPlayer[0], NearPlayer[1]));
	TestTrue(TEXT("enough spawns to mean something (>= 30)"), Checked >= 30);
	TestEqual(TEXT("every point within 2500 cm of a player"), OutOfReach, 0);
	TestEqual(TEXT("every point on water (none on the islands)"), NotWater, 0);
	TestEqual(TEXT("every point at least MinBiteDepth and the row's MinDepth deep"), Shallow, 0);
	TestEqual(TEXT("every point's winning area is its area"), WrongArea, 0);
	TestEqual(TEXT("every open-water point within 1800 cm of a player"), OpenFar, 0);
	return true;
}

// =====================================================================================================================
// A passed roll that finds no point: the due retries
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnRetryLimit, "Project.Fishing.Water.QA.HotSpotSpawn.FailedPlacementRetriedThreeChecksThenDropped", Flags)
bool FQAHotSpotSpawnRetryLimit::RunTest(const FString& Parameters)
{
	// Spec 5: a passed roll whose HotSpotSpawnTries points are all bad stays due: the pair's next 3 checks with a player in reach
	// retry it without a roll, then it is dropped (the next check rolls again). Checks out of reach don't use up retries. A
	// retry that finds a point spawns without a roll.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	UWorld* World = Scene.World;
	QASpawn::Seabed(World);
	QASpawn::Circle(World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(3000.0, 0.0), 500.f);
	AActor* Land = QASpawn::Island(World, FVector2D(3000.0, 0.0), 700.f); // the whole area is land
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({ { QASpawn::ReefRow, QASpawn::SpawnRow(0.001f, 1, { TEXT("Habitat.Reef") }) } }));
	ALurePlayerCharacter* Player = Scene.Spawn(*this, FVector(1000.f, 0.f, 100.f)); // 15 m from the outline
	ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(World, Rows.Get(), 601);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("spawner"), Spawner))
	{
		return false;
	}
	const FName Row(QASpawn::ReefRow), Area(QASpawn::ReefArea);
	auto Stats = [Spawner]() -> const FLureHotSpotStepStats& { return Spawner->GetLastStepStats(); };

	// Check 1: the roll passes (chance ~1) but every point is land.
	TestEqual(TEXT("check 1: nothing spawned"), Spawner->SpawnStep(1.f), 0);
	TestEqual(TEXT("check 1: one roll"), Stats().Rolls, 1);
	TestEqual(TEXT("check 1: it passed"), Stats().RollsPassed, 1);
	TestEqual(TEXT("check 1: no good point"), Stats().NoPoint, 1);
	TestTrue(TEXT("check 1: rejects were counted"), Stats().Rejects.Total() > 0);
	TestEqual(TEXT("check 1: the spawn is due for 3 retries"), Spawner->GetDueRetries(Row, Area), 3);

	// Checks 2-4: retries, no rolls.
	for (int32 Retry = 1; Retry <= 3; ++Retry)
	{
		TestEqual(FString::Printf(TEXT("retry %d: nothing spawned"), Retry), Spawner->SpawnStep(1.f), 0);
		TestEqual(FString::Printf(TEXT("retry %d: a due retry"), Retry), Stats().DueRetries, 1);
		TestEqual(FString::Printf(TEXT("retry %d: no roll"), Retry), Stats().Rolls, 0);
		TestEqual(FString::Printf(TEXT("retry %d: no point"), Retry), Stats().NoPoint, 1);
		TestEqual(FString::Printf(TEXT("retry %d: retries left"), Retry), Spawner->GetDueRetries(Row, Area), 3 - Retry);
	}
	// Check 5: the due spawn was dropped; the pair is back to rolling (not a 4th retry).
	Spawner->SpawnStep(1.f);
	TestEqual(TEXT("check 5: no 4th retry"), Stats().DueRetries, 0);
	TestEqual(TEXT("check 5: a fresh roll"), Stats().Rolls, 1);
	TestEqual(TEXT("check 5: it failed again: due for 3 retries"), Spawner->GetDueRetries(Row, Area), 3);

	// Out of reach: retries are kept, not spent.
	QASpawn::Move(Player, -6000.0, 0.0);
	for (int32 Check = 0; Check < 5; ++Check)
	{
		Spawner->SpawnStep(1.f);
		TestEqual(TEXT("away: the pair waits"), Stats().OutOfReach, 1);
		TestEqual(TEXT("away: no retry"), Stats().DueRetries, 0);
	}
	TestEqual(TEXT("away: the 3 retries are kept"), Spawner->GetDueRetries(Row, Area), 3);

	// Back, and the land is gone: the first retry spawns without a roll.
	QASpawn::Move(Player, 1000.0, 0.0);
	Land->Destroy();
	TestEqual(TEXT("back, water now: the retry spawns one"), Spawner->SpawnStep(1.f), 1);
	TestEqual(TEXT("back: it was a due retry"), Stats().DueRetries, 1);
	TestEqual(TEXT("back: without a roll"), Stats().Rolls, 0);
	TestEqual(TEXT("back: nothing due any more"), Spawner->GetDueRetries(Row, Area), 0);
	const TArray<ALureHotSpot*> Live = QASpawn::LiveSpots(World);
	if (TestEqual(TEXT("one live hot spot"), Live.Num(), 1))
	{
		TestEqual(TEXT("in the reef"), Live[0]->GetState().AreaId, Area);
	}
	return true;
}

// =====================================================================================================================
// Determinism
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnDeterministic, "Project.Fishing.Water.QA.HotSpotSpawn.DeterministicWithSeed", Flags)
bool FQAHotSpotSpawnDeterministic::RunTest(const FString& Parameters)
{
	// Spec 5 "Seeded RNG": the same seed in the same world and the same checks gives the same hot spots (type, area, anchor,
	// drift seed, lifetime) and the same step stats, including the arrival burst after a wait; another seed gives others.
	QASpawn::FSettingsGuard Guard;
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	UWorld* World = Scene.World;
	QASpawn::Seabed(World);
	QASpawn::Circle(World, QASpawn::ReefArea, TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 3000.f);
	QASpawn::Everywhere(World, TEXT("qa_open"), TEXT("Habitat.Lagoon"), -5);
	QASpawn::Island(World, FVector2D(3500.0, 800.0), 400.f);
	FLureHotSpotRow Reef = QASpawn::SpawnRow(8.f, 4, { TEXT("Habitat.Reef") });
	Reef.DriftRange = 250.f;
	Reef.DriftSpeed = 15.f;
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({
		{ QASpawn::ReefRow, Reef },
		{ TEXT("QA_Open"), QASpawn::SpawnRow(12.f, 3, { TEXT("Habitat.Lagoon") }) } }));
	ALurePlayerCharacter* Player = Scene.Spawn(*this, FVector(-6000.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	auto Run = [&](int32 Seed) -> FString
	{
		QASpawn::ClearSpots(World);
		QASpawn::Move(Player, -6000.0, 0.0);
		ALureHotSpotSpawner* Spawner = QASpawn::MakeSpawner(World, Rows.Get(), Seed);
		Spawner->MaxHotSpots = 50;
		FString Log;
		for (int32 Check = 0; Check < 60; ++Check)
		{
			if (Check == 30)
			{
				QASpawn::Move(Player, 3000.0, -1500.0); // arrive at the reef after 30 s
			}
			const int32 Spawned = Spawner->SpawnStep(1.f);
			const FLureHotSpotStepStats& S = Spawner->GetLastStepStats();
			Log += FString::Printf(TEXT("%d:%d r%d p%d d%d n%d;"), Check, Spawned, S.Rolls, S.RollsPassed, S.DueRetries, S.NoPoint);
		}
		Log += TEXT("\n") + QASpawn::Fingerprint(World);
		Spawner->Destroy();
		QASpawn::ClearSpots(World);
		return Log;
	};
	const FString A = Run(7331);
	const FString B = Run(7331);
	const FString C = Run(7332);
	TestTrue(TEXT("the run spawned hot spots"), A.Contains(QASpawn::ReefRow));
	TestEqual(TEXT("same seed: identical checks and hot spots"), B, A);
	TestNotEqual(TEXT("another seed: different hot spots"), C, A);
	if (B != A)
	{
		AddInfo(TEXT("run A:\n") + A);
		AddInfo(TEXT("run B:\n") + B);
	}
	return true;
}

// =====================================================================================================================
// Server only
// =====================================================================================================================

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAHotSpotSpawnClientNever, "Project.Fishing.Water.QA.HotSpotSpawn.ClientSpawnerNeverSpawns", Flags)
bool FQAHotSpotSpawnClientNever::RunTest(const FString& Parameters)
{
	// Spec 5 "Spawner: Server only (a non-replicated level actor has authority on clients too, so it checks the net mode)". A
	// spawner in a connected client's world (NM_Client) with water, a reef around the client's pawn, a certain-pass row, and both
	// SpawnStep and automatic Tick checks spawns nothing and makes no local hot spot. Control: the same setup on the server spawns.
	AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("is not imported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	QASpawn::FSettingsGuard Guard;
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* Server = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("harness: server"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver())
		|| !TestTrue(TEXT("harness: client"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	UWorld* Client = Worlds.Clients[0].GetWorld();
	if (!TestNotNull(TEXT("client world"), Client) || !TestEqual(TEXT("the client world is NM_Client"), static_cast<int32>(Client->GetNetMode()), static_cast<int32>(NM_Client)))
	{
		return false;
	}
	Worlds.TickAll(30);
	auto PawnXY = [](UWorld* World)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		return Pawn ? FVector2D(Pawn->GetActorLocation()) : FVector2D::ZeroVector;
	};
	const TStrongObjectPtr<UDataTable> Rows(QASpawn::RowTable({ { QASpawn::ReefRow, QASpawn::SpawnRow(0.001f, 3, { TEXT("Habitat.Reef") }) } }));
	auto Setup = [&](UWorld* World) -> ALureHotSpotSpawner*
	{
		const FVector2D Near = PawnXY(World) + FVector2D(1500.0, 0.0);
		QASpawn::Seabed(World);
		QASpawn::Circle(World, QASpawn::ReefArea, TEXT("Habitat.Reef"), Near, 1200.f);
		return QASpawn::MakeSpawner(World, Rows.Get(), 707, /*bAuto*/ true);
	};
	ALureHotSpotSpawner* ClientSpawner = Setup(Client);
	if (!TestNotNull(TEXT("client spawner"), ClientSpawner))
	{
		return false;
	}
	int32 ClientSpawned = 0;
	for (int32 Check = 0; Check < 50; ++Check)
	{
		ClientSpawned += ClientSpawner->SpawnStep(1.f);
	}
	ClientSpawned += ClientSpawner->SpawnStep(1000.f);
	Worlds.TickAll(180); // 3 s of automatic checks (and the level-start check)
	int32 LocalOnClient = 0;
	for (TActorIterator<ALureHotSpot> It(Client); It; ++It)
	{
		LocalOnClient += It->HasAuthority() ? 1 : 0;
	}
	TestEqual(TEXT("client: SpawnStep spawns nothing"), ClientSpawned, 0);
	TestEqual(TEXT("client: no hot spot was made locally (Tick included)"), LocalOnClient, 0);

	// Control: the same setup on the server does spawn (so the client result is not an empty setup).
	ALureHotSpotSpawner* ServerSpawner = Setup(Server);
	int32 ServerSpawned = 0;
	for (int32 Check = 0; ServerSpawner && Check < 5; ++Check)
	{
		ServerSpawned += ServerSpawner->SpawnStep(1.f);
	}
	TestTrue(TEXT("control: the server spawner spawns in the same setup"), ServerSpawned > 0);
	return true;
}

#endif // WITH_EDITOR
}

#endif // WITH_DEV_AUTOMATION_TESTS

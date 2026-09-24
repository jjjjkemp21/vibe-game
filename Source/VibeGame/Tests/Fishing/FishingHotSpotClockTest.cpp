// Lure T-027c (unreal-engineer): the hot spot spawner's clock and its sampling within reach (docs/specs/fishing-water-rules.md
// section 5): a (row, area) pair banks its waiting time while nobody can reach the area and rolls all of it when a player
// arrives; a passed roll that found no good point stays due for a few checks; candidates are drawn only in the part of a
// bounded area within a player's reach; every check explains itself (step stats, LogLureHotSpot Verbose).
// The same map-level behavior on the real Palm Key map: Tests/Level/LevelPalmKeyHotSpotTest.cpp.

#include "Tests/Fishing/FishingWaterTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fishing/LureHotSpot.h"
#include "Fishing/LureHotSpotSpawner.h"
#include "Fishing/LureHotSpotVisualComponent.h"
#include "Fishing/LureWaterArea.h"
#include "Fishing/LureWaterSettings.h"
#include "Misc/ScopeExit.h"
#include "Tests/Fishing/QAFishingTestUtils.h"

namespace HotSpotClockTest
{
	/** A still hot spot row (no drift) of one habitat (nullptr = any water). */
	FLureHotSpotRow MakeRow(float SpawnInterval, const TCHAR* Habitat)
	{
		FLureHotSpotRow Row;
		Row.SpawnInterval = SpawnInterval;
		Row.MaxPerArea = 1;
		Row.MinSpacing = 300.f;
		Row.Radius = 200.f;
		Row.LifetimeMin = 100.f;
		Row.LifetimeMax = 100.f;
		Row.DriftSpeed = 0.f;
		Row.DriftRange = 0.f;
		Row.MinDepth = 60.f;
		Row.MaxDepth = 0.f;
		Row.AllowedHabitats.Reset();
		if (Habitat)
		{
			Row.AllowedHabitats.Add(LureWaterTest::Tag(Habitat));
		}
		return Row;
	}

	UDataTable* MakeTable(const TArray<TPair<FName, FLureHotSpotRow>>& Rows)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureHotSpotRow::StaticStruct();
		for (const TPair<FName, FLureHotSpotRow>& Row : Rows)
		{
			Table->AddRow(Row.Key, Row.Value);
		}
		return Table;
	}

	/** A spawner driven by SpawnStep only (no automatic checks). */
	ALureHotSpotSpawner* MakeSpawner(LureWaterTest::FWaterWorld& World, const UDataTable* Table, int32 Seed)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureHotSpotSpawner* Spawner = World.World->SpawnActor<ALureHotSpotSpawner>(ALureHotSpotSpawner::StaticClass(), FTransform::Identity, Params);
		if (Spawner)
		{
			Spawner->SetAutoSpawn(false);
			Spawner->SetHotSpotTable(Table);
			Spawner->SetRandomSeed(Seed);
			Spawner->MaxHotSpots = 50;
		}
		return Spawner;
	}

	TArray<ALureHotSpot*> Live(const LureWaterTest::FWaterWorld& World)
	{
		return ALureHotSpotSpawner::GetLiveHotSpots(World.World, World.Now());
	}

	void ClearHotSpots(LureWaterTest::FWaterWorld& World)
	{
		for (ALureHotSpot* HotSpot : Live(World))
		{
			HotSpot->Destroy();
		}
	}

	void MovePlayer(ALurePlayerCharacter* Player, const FVector2D& XY)
	{
		Player->SetActorLocation(FVector(XY.X, XY.Y, Player->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
	}

	/** Every pair is in exactly one bucket. */
	int32 Buckets(const FLureHotSpotStepStats& Stats)
	{
		return Stats.WrongWater + Stats.Full + Stats.LevelFull + Stats.OutOfReach + Stats.NothingBanked + Stats.Rolls + Stats.DueRetries;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotClockBanks, "Project.Fishing.Water.HotSpot.ClockBanksWhileNobodyIsNear", LureWaterTest::Flags)
bool FLureHotSpotClockBanks::RunTest(const FString& Parameters)
{
	// T-027c: a bounded area nobody can reach banks its waiting time without rolling, up to max(the check, 45 s =
	// HotSpotPrewarmSeconds); the first check with a player in reach rolls all of it, even a 0 s check (which alone never
	// spawns). Over 200 seeds, arriving at a full bank spawns with chance 1 - exp(-45 / 40) = 0.675.
	using namespace HotSpotClockTest;
	LureWaterTest::TScopedSetting<ULureWaterSettings, float> Near(&ULureWaterSettings::HotSpotNearPlayerRadius, 2500.f);
	LureWaterTest::TScopedSetting<ULureWaterSettings, float> Prewarm(&ULureWaterSettings::HotSpotPrewarmSeconds, 45.f);
	LureWaterTest::FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSeabed(-500.f, FVector2D(5000.0, 0.0), 6000.f);
	World.AddCircleArea(TEXT("qa_far"), TEXT("Habitat.Reef"), FVector2D(9000.0, 0.0), 1000.f);
	const TStrongObjectPtr<UDataTable> Table(MakeTable({ { TEXT("QA_Reef"), MakeRow(40.f, TEXT("Habitat.Reef")) } }));
	ALurePlayerCharacter* Player = World.Spawn(*this);
	ALureHotSpotSpawner* Spawner = MakeSpawner(World, Table.Get(), 1);
	if (!TestNotNull(TEXT("the player"), Player) || !TestNotNull(TEXT("the spawner"), Spawner))
	{
		return false;
	}
	const FVector2D Dock(250.0, 0.0);      // 7.75 m beyond reach of the area's edge
	const FVector2D NearArea(7500.0, 0.0); // 5 m from its edge
	const FName Reef(TEXT("QA_Reef"));
	const FName Far(TEXT("qa_far"));

	TestEqual(TEXT("the level start, nobody near: nothing"), Spawner->SpawnStep(45.f), 0);
	const FLureHotSpotStepStats Start = Spawner->GetLastStepStats();
	TestTrue(TEXT("... out of reach, not rolled"), Start.OutOfReach == 1 && Start.Rolls == 0);
	TestEqual(TEXT("... the default water: a habitat the row doesn't allow"), Start.WrongWater, 1);
	TestEqual(TEXT("... every pair in one bucket (1 row x 2 areas)"), Buckets(Start), 2);
	TestEqual(TEXT("... 45 s banked"), Spawner->GetBankedSeconds(Reef, Far), 45.f);
	for (int32 Check = 0; Check < 30; ++Check)
	{
		Spawner->SpawnStep(1.f);
	}
	TestEqual(TEXT("30 more checks of 1 s: still 45 s (the cap)"), Spawner->GetBankedSeconds(Reef, Far), 45.f);
	Spawner->SpawnStep(100.f);
	TestEqual(TEXT("a 100 s check counts in full"), Spawner->GetBankedSeconds(Reef, Far), 100.f);
	Spawner->SpawnStep(1.f);
	TestEqual(TEXT("... then back to the cap"), Spawner->GetBankedSeconds(Reef, Far), 45.f);
	TestEqual(TEXT("nothing spawned while nobody was near"), Live(World).Num(), 0);

	// Arrivals: a 0 s check with a player in reach rolls the banked 45 s.
	constexpr int32 Seeds = 200;
	int32 Spawned = 0;
	int32 RolledOnce = 0;
	int32 InReach = 0;
	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		ALureHotSpotSpawner* Fresh = MakeSpawner(World, Table.Get(), Seed);
		MovePlayer(Player, Dock);
		Fresh->SpawnStep(45.f);
		MovePlayer(Player, NearArea);
		const int32 Now = Fresh->SpawnStep(0.f);
		RolledOnce += Fresh->GetLastStepStats().Rolls == 1 ? 1 : 0;
		Spawned += Now;
		for (const ALureHotSpot* HotSpot : Live(World))
		{
			InReach += FVector2D::Distance(FVector2D(HotSpot->GetState().Anchor.X, HotSpot->GetState().Anchor.Y), NearArea) <= 2500.0 + 0.1 ? 1 : 0;
		}
		TestEqual(TEXT("the roll takes the whole bank"), Fresh->GetBankedSeconds(Reef, Far), 0.f);
		Fresh->Destroy();
		ClearHotSpots(World);
	}
	const double P = 1.0 - FMath::Exp(-45.0 / 40.0);
	const double Mean = Seeds * P;
	const double Sigma = FMath::Sqrt(Seeds * P * (1.0 - P));
	AddInfo(FString::Printf(TEXT("%d spawns in %d arrivals (expected %.1f +- %.1f)"), Spawned, Seeds, Mean, Sigma));
	TestEqual(TEXT("every arrival rolled once (a 0 s check)"), RolledOnce, Seeds);
	TestTrue(FString::Printf(TEXT("arrival spawns %d within 4 sigma of %.1f"), Spawned, Mean), FMath::Abs(Spawned - Mean) <= 4.0 * Sigma);
	TestEqual(TEXT("each within reach of the player"), InReach, Spawned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotClockDue, "Project.Fishing.Water.HotSpot.ClockKeepsADueSpawn", LureWaterTest::Flags)
bool FLureHotSpotClockDue::RunTest(const FString& Parameters)
{
	// T-027c: a passed roll that finds no good point stays due: the next DueRetryChecks (3) checks with a player in reach
	// try again without a roll, and it spawns as soon as a point is good. After 3 failed retries it is dropped and the clock
	// rolls again. (Here another hot spot sits in the middle of a small area and the row's MinSpacing covers all of it, until
	// that hot spot ends; the near rule is off.)
	using namespace HotSpotClockTest;
	LureWaterTest::TScopedSetting<ULureWaterSettings, float> Near(&ULureWaterSettings::HotSpotNearPlayerRadius, 0.f);
	LureWaterTest::FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSeabed(-500.f, FVector2D(5000.0, 0.0), 6000.f);
	const FVector2D Middle(6000.0, 3000.0);
	World.AddCircleArea(TEXT("qa_small"), TEXT("Habitat.Reef"), Middle, 600.f);
	FLureHotSpotRow Reef = MakeRow(0.01f, TEXT("Habitat.Reef"));
	Reef.MinSpacing = 2000.f; // more than the area's diameter
	const TStrongObjectPtr<UDataTable> Table(MakeTable({ { TEXT("QA_Reef"), Reef } }));
	ALureHotSpotSpawner* Spawner = MakeSpawner(World, Table.Get(), 7);
	ALureHotSpot* Blocker = ALureHotSpot::SpawnHotSpot(World.World, nullptr, TEXT("QA_Other"), MakeRow(10.f, nullptr), NAME_None, FVector(Middle.X, Middle.Y, 0.0), 1,
		World.Now(), 1000.f);
	if (!TestNotNull(TEXT("the spawner"), Spawner) || !TestNotNull(TEXT("the other hot spot"), Blocker))
	{
		return false;
	}
	const FName Type(TEXT("QA_Reef"));
	const FName Small(TEXT("qa_small"));
	const int32 Tries = FMath::Max(1, GetDefault<ULureWaterSettings>()->HotSpotSpawnTries);

	TestEqual(TEXT("check 1: nothing (every point too close to the other hot spot)"), Spawner->SpawnStep(1.f), 0);
	FLureHotSpotStepStats Stats = Spawner->GetLastStepStats();
	TestTrue(TEXT("check 1: rolled and passed, no good point"), Stats.Rolls == 1 && Stats.RollsPassed == 1 && Stats.NoPoint == 1);
	TestEqual(TEXT("check 1: every try too close"), Stats.Rejects.Get(ELureHotSpotReject::TooClose), Tries);
	TestEqual(TEXT("check 1: due for 3 checks"), Spawner->GetDueRetries(Type, Small), ALureHotSpotSpawner::DueRetryChecks);
	for (int32 Retry = 1; Retry <= ALureHotSpotSpawner::DueRetryChecks; ++Retry)
	{
		Spawner->SpawnStep(1.f);
		Stats = Spawner->GetLastStepStats();
		TestTrue(FString::Printf(TEXT("retry %d: no roll, a due retry that fails"), Retry), Stats.Rolls == 0 && Stats.DueRetries == 1 && Stats.NoPoint == 1);
		TestEqual(FString::Printf(TEXT("retry %d: retries left"), Retry), Spawner->GetDueRetries(Type, Small), ALureHotSpotSpawner::DueRetryChecks - Retry);
	}
	Spawner->SpawnStep(1.f);
	Stats = Spawner->GetLastStepStats();
	TestTrue(TEXT("after 3 failed retries it was dropped: the clock rolls again"), Stats.Rolls == 1 && Stats.DueRetries == 0);
	TestEqual(TEXT("... and that spawn is due again"), Spawner->GetDueRetries(Type, Small), ALureHotSpotSpawner::DueRetryChecks);

	Blocker->Destroy(); // it ends: the area is free
	TestEqual(TEXT("the due spawn happens at the next check"), Spawner->SpawnStep(1.f), 1);
	Stats = Spawner->GetLastStepStats();
	TestTrue(TEXT("... as a retry, without a roll"), Stats.DueRetries == 1 && Stats.Rolls == 0 && Stats.Spawned == 1);
	TestEqual(TEXT("... nothing due any more"), Spawner->GetDueRetries(Type, Small), 0);
	const TArray<ALureHotSpot*> HotSpots = Live(World);
	TestTrue(TEXT("... in the area"), HotSpots.Num() == 1 && HotSpots[0]->GetState().AreaId == Small);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotSamplesInReach, "Project.Fishing.Water.HotSpot.SpawnsInTheReachablePartOfABigArea", LureWaterTest::Flags)
bool FLureHotSpotSamplesInReach::RunTest(const FString& Parameters)
{
	// T-027c: candidates are drawn only in the part of a bounded area within a player's reach. A player at the edge of a
	// 60 x 60 m area (about 5 % of it within 25 m): a passed roll places a hot spot at the first check for 20 of 20 seeds
	// (drawing over the whole area found a point in reach about half the time), always inside the area and within reach.
	using namespace HotSpotClockTest;
	LureWaterTest::TScopedSetting<ULureWaterSettings, float> Near(&ULureWaterSettings::HotSpotNearPlayerRadius, 2500.f);
	LureWaterTest::FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSeabed(-500.f, FVector2D(5000.0, 0.0), 6000.f);
	World.AddArea(TEXT("qa_big"), TEXT("Habitat.Reef"), FVector(5000.0, 0.0, 0.0), 0.f, 0, [](ALureWaterArea& Area) { Area.SetShapeBox(FVector2D(3000.0, 3000.0)); });
	const TStrongObjectPtr<UDataTable> Table(MakeTable({ { TEXT("QA_Reef"), MakeRow(0.01f, TEXT("Habitat.Reef")) } }));
	ALurePlayerCharacter* Player = World.Spawn(*this); // on the dock at x 2.5 m: the area starts at x 20 m
	if (!TestNotNull(TEXT("the player"), Player))
	{
		return false;
	}
	const FVector2D PlayerXY(Player->GetActorLocation().X, Player->GetActorLocation().Y);
	constexpr int32 Seeds = 20;
	int32 FirstCheck = 0;
	int32 Good = 0;
	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		ALureHotSpotSpawner* Spawner = MakeSpawner(World, Table.Get(), Seed);
		FirstCheck += Spawner->SpawnStep(1.f);
		for (const ALureHotSpot* HotSpot : Live(World))
		{
			const FVector2D XY(HotSpot->GetState().Anchor.X, HotSpot->GetState().Anchor.Y);
			Good += (FVector2D::Distance(XY, PlayerXY) <= 2500.0 + 0.1 && FMath::Abs(XY.X - 5000.0) <= 3000.0 && FMath::Abs(XY.Y) <= 3000.0) ? 1 : 0;
		}
		Spawner->Destroy();
		ClearHotSpots(World);
	}
	TestEqual(TEXT("a hot spot at the first check"), FirstCheck, Seeds);
	TestEqual(TEXT("each inside the area and within reach"), Good, FirstCheck);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotStepExplains, "Project.Fishing.Water.HotSpot.StepStatsAndVerboseLog", LureWaterTest::Flags)
bool FLureHotSpotStepExplains::RunTest(const FString& Parameters)
{
	// T-027c: a check explains itself: every (row, area) pair is in exactly one bucket of GetLastStepStats, and with
	// LogLureHotSpot at Verbose every check logs one summary line (so "why no hot spots?" is one console command away).
	// A HotSpotTypes list that names no row warns once.
	using namespace HotSpotClockTest;
	LureWaterTest::TScopedSetting<ULureWaterSettings, float> Near(&ULureWaterSettings::HotSpotNearPlayerRadius, 2500.f);
	LureWaterTest::FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSeabed(-500.f, FVector2D(5000.0, 0.0), 6000.f);
	World.AddCircleArea(TEXT("qa_far"), TEXT("Habitat.Reef"), FVector2D(9000.0, 0.0), 1000.f);
	World.AddCircleArea(TEXT("qa_lagoon"), TEXT("Habitat.Lagoon"), FVector2D(5000.0, 4800.0), 1000.f);
	const TStrongObjectPtr<UDataTable> Table(MakeTable({ { TEXT("QA_Any"), MakeRow(1000.f, nullptr) }, { TEXT("QA_Reef"), MakeRow(1000.f, TEXT("Habitat.Reef")) },
		{ TEXT("QA_Never"), MakeRow(0.f, nullptr) } }));
	ALurePlayerCharacter* Player = World.Spawn(*this); // on the dock: both areas out of reach, the default water around it
	ALureHotSpotSpawner* Spawner = MakeSpawner(World, Table.Get(), 3);
	if (!TestNotNull(TEXT("the player"), Player) || !TestNotNull(TEXT("the spawner"), Spawner))
	{
		return false;
	}
	const ELogVerbosity::Type SavedVerbosity = LogLureHotSpot.GetVerbosity();
	LogLureHotSpot.SetVerbosity(ELogVerbosity::Verbose);
	ON_SCOPE_EXIT { LogLureHotSpot.SetVerbosity(SavedVerbosity); };
	{
		QAFishing::FLogCapture Capture(TEXT("LogLureHotSpot"));
		Spawner->SpawnStep(1.f);
		const FLureHotSpotStepStats Stats = Spawner->GetLastStepStats();
		TestEqual(TEXT("rows that can spawn (QA_Never has no interval)"), Stats.Rows, 2);
		TestEqual(TEXT("areas: 2 + the default water"), Stats.Areas, 3);
		TestEqual(TEXT("every pair in exactly one bucket"), Buckets(Stats), Stats.Rows * Stats.Areas);
		TestEqual(TEXT("out of reach: QA_Any and QA_Reef in qa_far, QA_Any in qa_lagoon"), Stats.OutOfReach, 3);
		TestEqual(TEXT("wrong water: QA_Reef in the lagoon and the default (shore) water"), Stats.WrongWater, 2);
		TestEqual(TEXT("QA_Any in the default water, next to the player, rolled"), Stats.Rolls, 1);
		GLog->Flush(); // lines logged on this thread may still be queued for the output devices
		TestEqual(TEXT("one Verbose summary line per check"), Capture.Count(ELogVerbosity::Verbose, TEXT("out of reach 3")), 1);
	}
	Spawner->HotSpotTypes = { TEXT("QA_Nope") };
	AddExpectedMessagePlain(TEXT("name no row"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("types that name no row: nothing"), Spawner->SpawnStep(1.f), 0);
	TestEqual(TEXT("... again (warned once)"), Spawner->SpawnStep(1.f), 0);
	TestEqual(TEXT("... no row"), Spawner->GetLastStepStats().Rows, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

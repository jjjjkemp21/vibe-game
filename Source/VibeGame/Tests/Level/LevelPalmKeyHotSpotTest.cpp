// Lure T-027c (unreal-engineer): hot spots on the real Palm Key map (Project.Level.PalmKey.HotSpots.*).
//
// Why these exist: in PIE on L_PalmKey no hot spot appeared for minutes, while every Project.Fishing.Water.* test passed
// (each of those builds its own little world). These load the built map itself (LureMapTest::FMapWorld): the level's own
// spawner with its real DT_HotSpot, the water volume, the water areas, the docks and rocks, all ticking the way PIE ticks
// them (the spawner's own Tick: the level-start check, then one check a second), with a player where players stand.
//
// What they hold (docs/specs/fishing-water-rules.md section 5): soon after the level starts, and soon after a player
// arrives at any cast point of the layout, a hot spot is within reach (HotSpotNearPlayerRadius) in most sessions. The
// rolls are random, so each test runs many fixed seeds and asserts a share set well below what the rules give (every
// seed's result is in the report): a real regression fails, bad luck does not.
//
// The T-027c cause, for the record: the level-start check (worth HotSpotPrewarmSeconds) was spent while everyone stood at
// the start, where Palm Key has only a sliver of hot spot water within reach; candidate points were drawn over a whole
// area and thrown away when out of reach, so even a passed roll rarely found a point. After that every area only had its
// 1 s chances (Bubbles: 2.5 %), so a player waited minutes.

#include "Tests/Level/LureMapTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Fishing/FishingWater.h"
#include "Fishing/FishingWaterTypes.h"
#include "Fishing/LureHotSpot.h"
#include "Fishing/LureHotSpotSpawner.h"
#include "Fishing/LureHotSpotVisualComponent.h"
#include "Fishing/LureWaterSettings.h"
#include "GameFramework/PhysicsVolume.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LurePalmKeyHotSpots
{
	const TCHAR* const MapPath = TEXT("/Game/Maps/L_PalmKey");
	const TCHAR* const LayoutPath = TEXT("data/levels/L_PalmKey.json");

	/** The layout's markers (data/levels/L_PalmKey.json: the source of truth the map is built from). */
	TArray<TSharedPtr<FJsonObject>> LoadMarkers(FAutomationTestBase& Test, const TCHAR* Type)
	{
		TArray<TSharedPtr<FJsonObject>> Markers;
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectDir() / LayoutPath)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)
			|| !Root.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s does not load"), LayoutPath));
			return Markers;
		}
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Root->TryGetArrayField(TEXT("markers"), Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString MarkerType;
				if (Value.IsValid() && Value->Type == EJson::Object && Value->AsObject()->TryGetStringField(TEXT("type"), MarkerType) && MarkerType == Type)
				{
					Markers.Add(Value->AsObject());
				}
			}
		}
		return Markers;
	}

	struct FCastPoint
	{
		FString Id;
		FVector Feet = FVector::ZeroVector;
	};

	/** Where a player stands to fish each fishing spot of the layout (its cast_from). */
	TArray<FCastPoint> LoadCastPoints(FAutomationTestBase& Test)
	{
		TArray<FCastPoint> Points;
		for (const TSharedPtr<FJsonObject>& Marker : LoadMarkers(Test, TEXT("fishing_spot")))
		{
			const TArray<TSharedPtr<FJsonValue>>* CastFrom = nullptr;
			FString Id;
			if (Marker->TryGetStringField(TEXT("id"), Id) && Marker->TryGetArrayField(TEXT("cast_from"), CastFrom) && CastFrom->Num() == 3)
			{
				Points.Add({ Id, FVector((*CastFrom)[0]->AsNumber(), (*CastFrom)[1]->AsNumber(), (*CastFrom)[2]->AsNumber()) });
			}
		}
		return Points;
	}

	/** The reach these tests hold hot spots to: the spawner's own near-player rule. */
	double Reach()
	{
		return static_cast<double>(GetDefault<ULureWaterSettings>()->HotSpotNearPlayerRadius);
	}

	/** A live hot spot whose center is within reach of XY now, or null. */
	const ALureHotSpot* HotSpotInReach(const UWorld* World, const FVector2D& XY)
	{
		const double Now = FLureWaterQuery::GetTime(World);
		for (const ALureHotSpot* HotSpot : ALureHotSpotSpawner::GetLiveHotSpots(World, Now))
		{
			const FVector Center = HotSpot->GetCenterAt(Now);
			if (FVector2D::Distance(FVector2D(Center.X, Center.Y), XY) <= Reach() + 1.0)
			{
				return HotSpot;
			}
		}
		return nullptr;
	}

	/** One play session: the map, its spawner reseeded (the marker's seed 0 means random), and a player standing at the start
	 *  (ps_2) before the first tick, as in PIE. */
	struct FSession
	{
		LureMapTest::FMapWorld Map;
		ALureHotSpotSpawner* Spawner = nullptr;
		ALurePlayerCharacter* Player = nullptr;

		bool Start(FAutomationTestBase& Test, int32 Seed)
		{
			if (!(Reach() > 0.0))
			{
				Test.AddError(TEXT("these tests hold hot spots to HotSpotNearPlayerRadius, which is 0 (off)"));
				return false;
			}
			if (!Map.Create(Test, MapPath))
			{
				return false;
			}
			const TArray<ALureHotSpotSpawner*> Spawners = Map.FindActors<ALureHotSpotSpawner>();
			APlayerStart* PlayerStart = Map.FindPlayerStart();
			if (Spawners.Num() != 1 || !PlayerStart)
			{
				Test.AddError(FString::Printf(TEXT("L_PalmKey has %d hot spot spawners (want 1, from the layout's hot_spots marker) and %s player start"),
					Spawners.Num(), PlayerStart ? TEXT("a") : TEXT("no")));
				return false;
			}
			Spawner = Spawners[0];
			Spawner->SetRandomSeed(Seed);
			const FVector Feet = PlayerStart->GetActorLocation() - FVector(0.0, 0.0, PlayerStart->GetSimpleCollisionHalfHeight());
			Player = Map.SpawnPlayer(Test, Feet, static_cast<float>(PlayerStart->GetActorRotation().Yaw));
			return Player != nullptr;
		}

		FVector2D PlayerXY() const
		{
			return FVector2D(Player->GetActorLocation().X, Player->GetActorLocation().Y);
		}

		/** Ticks for up to Seconds (0.25 s steps; the spawner checks once a second). Returns the seconds until a hot spot was
		 *  within reach of the player (0 = one already was), or -1 if none was. */
		double TickUntilHotSpotInReach(double Seconds)
		{
			constexpr float Step = 0.25f;
			for (double Time = 0.0;; Time += Step)
			{
				if (HotSpotInReach(Map.World, PlayerXY()))
				{
					return Time;
				}
				if (Time >= Seconds - 1.0e-6)
				{
					return -1.0;
				}
				Map.Tick(Step, Step);
			}
		}
	};

	FString Describe(double Seconds)
	{
		return Seconds >= 0.0 ? FString::Printf(TEXT("%.0f"), Seconds) : FString(TEXT("-"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLurePalmKeyHotSpotSetup, "Project.Level.PalmKey.HotSpots.MapMatchesItsLayout", LureMapTest::Flags)
bool FLurePalmKeyHotSpotSetup::RunTest(const FString& Parameters)
{
	// The built map carries what the hot spots need, as its layout says: one spawner from the hot_spots marker (its types, max
	// and seed), the imported DT_HotSpot (not the built-in fallback row) with a row for every type, a water volume, and every
	// water_area of the layout. If this fails, rebuild the level (editor-operator) or re-import DT_HotSpot.
	const TArray<TSharedPtr<FJsonObject>> Markers = LurePalmKeyHotSpots::LoadMarkers(*this, TEXT("hot_spots"));
	if (!TestEqual(TEXT("the layout has one hot_spots marker"), Markers.Num(), 1))
	{
		return false;
	}
	LureMapTest::FMapWorld Map;
	if (!Map.Create(*this, LurePalmKeyHotSpots::MapPath))
	{
		return false;
	}
	const TArray<ALureHotSpotSpawner*> Spawners = Map.FindActors<ALureHotSpotSpawner>();
	if (!TestEqual(TEXT("one ALureHotSpotSpawner in the map"), Spawners.Num(), 1))
	{
		return false;
	}
	const ALureHotSpotSpawner* Spawner = Spawners[0];
	TArray<FString> LayoutTypes;
	Markers[0]->TryGetStringArrayField(TEXT("types"), LayoutTypes);
	TArray<FString> MapTypes;
	for (const FName& Type : Spawner->HotSpotTypes)
	{
		MapTypes.Add(Type.ToString());
	}
	TestEqual(TEXT("the spawner's types are the marker's"), FString::Join(MapTypes, TEXT(",")), FString::Join(LayoutTypes, TEXT(",")));
	TestEqual(TEXT("the spawner's max is the marker's"), Spawner->MaxHotSpots, static_cast<int32>(Markers[0]->GetNumberField(TEXT("max"))));
	TestEqual(TEXT("the spawner's seed is the marker's"), Spawner->RandomSeed, static_cast<int32>(Markers[0]->GetNumberField(TEXT("seed"))));
	TestTrue(TEXT("the spawner ticks"), Spawner->PrimaryActorTick.bCanEverTick);

	const UDataTable* Table = FLureWaterQuery::LoadHotSpotTable();
	TestNotNull(TEXT("DT_HotSpot is imported (not the built-in fallback)"), Table);
	TSet<FString> Rows;
	for (const TPair<FName, FLureHotSpotRow>& Row : FLureWaterQuery::GetHotSpotRows(Table))
	{
		Rows.Add(Row.Key.ToString());
	}
	for (const FString& Type : LayoutTypes)
	{
		TestTrue(FString::Printf(TEXT("DT_HotSpot has a row for %s"), *Type), Rows.Contains(Type));
	}

	bool bWaterVolume = false;
	for (const APhysicsVolume* Volume : Map.FindActors<APhysicsVolume>())
	{
		bWaterVolume |= Volume->bWaterVolume;
	}
	TestTrue(TEXT("a water volume"), bWaterVolume);
	TSet<FName> MapAreas;
	for (const FLureWaterAreaInfo& Area : FLureWaterQuery::GatherAreas(Map.World))
	{
		MapAreas.Add(Area.AreaId);
	}
	for (const TSharedPtr<FJsonObject>& Area : LurePalmKeyHotSpots::LoadMarkers(*this, TEXT("water_area")))
	{
		const FString Id = Area->GetStringField(TEXT("id"));
		TestTrue(FString::Printf(TEXT("the map has the water area %s"), *Id), MapAreas.Contains(FName(*Id)));
	}
	Map.Release();
	return LureMapTest::TestNoMapCopiesLeft(*this, LurePalmKeyHotSpots::MapPath);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLurePalmKeyHotSpotStart, "Project.Level.PalmKey.HotSpots.WithinReachOfTheStartInTheFirstMinute", LureMapTest::Flags)
bool FLurePalmKeyHotSpotStart::RunTest(const FString& Parameters)
{
	// The T-027c repro: a player spawns at the start and waits there. The level starts as if HotSpotPrewarmSeconds (45 s) had
	// passed, and the dock water's edge is 21 m from the start: in most sessions a hot spot is within reach in the first
	// minute (the rules give about 9 in 10; before the fix about 1 in 5).
	constexpr int32 Seeds = 20;
	constexpr int32 Need = 15;
	int32 Hits = 0;
	TArray<FString> Results;
	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		LurePalmKeyHotSpots::FSession Session;
		if (!Session.Start(*this, Seed))
		{
			return false;
		}
		const double Seconds = Session.TickUntilHotSpotInReach(60.0);
		Hits += Seconds >= 0.0 ? 1 : 0;
		Results.Add(LurePalmKeyHotSpots::Describe(Seconds));
	}
	AddInfo(FString::Printf(TEXT("seconds until a hot spot was within reach of the start, seeds 1-%d (- = none in 60 s): %s"), Seeds, *FString::Join(Results, TEXT(" "))));
	TestTrue(FString::Printf(TEXT("a hot spot within reach of the start in the first minute: %d of %d seeds (need %d)"), Hits, Seeds, Need), Hits >= Need);
	return LureMapTest::TestNoMapCopiesLeft(*this, LurePalmKeyHotSpots::MapPath);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLurePalmKeyHotSpotArrival, "Project.Level.PalmKey.HotSpots.WithinReachSoonAfterArrivingAtACastPoint", LureMapTest::Flags)
bool FLurePalmKeyHotSpotArrival::RunTest(const FString& Parameters)
{
	// A player spends the first 30 s at the start (the level-start check happens there), then arrives at one of the layout's
	// cast points (each fishing spot's cast_from; every one, so a new spot is covered too). Water nobody was near has been
	// waiting: in most arrivals a hot spot is within reach in the first 20 s.
	constexpr int32 Seeds = 12;
	constexpr double NeedShare = 0.7;
	const TArray<LurePalmKeyHotSpots::FCastPoint> Points = LurePalmKeyHotSpots::LoadCastPoints(*this);
	if (!TestTrue(TEXT("the layout has cast points"), Points.Num() > 0))
	{
		return false;
	}
	int32 Hits = 0;
	int32 Arrivals = 0;
	for (const LurePalmKeyHotSpots::FCastPoint& Point : Points)
	{
		int32 PointHits = 0;
		TArray<FString> Results;
		for (int32 Seed = 1; Seed <= Seeds; ++Seed)
		{
			LurePalmKeyHotSpots::FSession Session;
			if (!Session.Start(*this, Seed))
			{
				return false;
			}
			Session.Map.Tick(30.0, 0.25f);
			LureMapTest::FMapWorld::TeleportPlayer(Session.Player, Point.Feet);
			const double Seconds = Session.TickUntilHotSpotInReach(20.0);
			PointHits += Seconds >= 0.0 ? 1 : 0;
			Results.Add(LurePalmKeyHotSpots::Describe(Seconds));
		}
		AddInfo(FString::Printf(TEXT("%s: %d of %d seeds; seconds after arrival: %s"), *Point.Id, PointHits, Seeds, *FString::Join(Results, TEXT(" "))));
		Hits += PointHits;
		Arrivals += Seeds;
	}
	const int32 Need = FMath::CeilToInt32(NeedShare * Arrivals);
	TestTrue(FString::Printf(TEXT("a hot spot within reach in the first 20 s after arriving: %d of %d arrivals (need %d)"), Hits, Arrivals, Need), Hits >= Need);
	return LureMapTest::TestNoMapCopiesLeft(*this, LurePalmKeyHotSpots::MapPath);
}

#endif // WITH_DEV_AUTOMATION_TESTS

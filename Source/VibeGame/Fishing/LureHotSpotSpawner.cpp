// Lure: spawns hot spots in a level's water (T-027). Rules: docs/specs/fishing-water-rules.md.

#include "Fishing/LureHotSpotSpawner.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishingWater.h"
#include "Fishing/LureHotSpot.h"
#include "Fishing/LureWaterSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace LureHotSpotSpawnerPrivate
{
	bool AllowsHabitat(const FLureHotSpotRow& Row, const FGameplayTag& Habitat)
	{
		if (!Habitat.IsValid())
		{
			return false;
		}
		if (Row.AllowedHabitats.Num() == 0)
		{
			return true;
		}
		for (const FGameplayTag& Allowed : Row.AllowedHabitats)
		{
			if (Allowed.IsValid() && Habitat.MatchesTag(Allowed))
			{
				return true;
			}
		}
		return false;
	}
}

ALureHotSpotSpawner::ALureHotSpotSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // server logic only: the hot spots replicate themselves
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;
}

void ALureHotSpotSpawner::EnsureSeeded()
{
	if (!bSeeded)
	{
		Rng.Initialize(RandomSeed != 0 ? RandomSeed : FFishRoll::MakeRandomSeed());
		bSeeded = true;
	}
}

void ALureHotSpotSpawner::SetRandomSeed(int32 Seed)
{
	Rng.Initialize(Seed);
	bSeeded = true;
}

void ALureHotSpotSpawner::SetHotSpotTable(const UDataTable* Table)
{
	TableOverride = Table;
	bUseTableOverride = true;
}

void ALureHotSpotSpawner::BeginPlay()
{
	Super::BeginPlay();
	EnsureSeeded();
}

void ALureHotSpotSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A level actor that doesn't replicate has authority on every machine: only the server (or standalone) spawns.
	if (!bAutoSpawn || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	if (!bPrewarmed)
	{
		bPrewarmed = true; // the first tick: every level actor is there now
		Accumulated = 0.f;
		SpawnStep(FMath::Max(0.f, Settings->HotSpotPrewarmSeconds));
		return;
	}
	Accumulated += FMath::Max(0.f, DeltaSeconds);
	if (Accumulated >= FMath::Max(0.1f, Settings->HotSpotCheckInterval))
	{
		const float Seconds = Accumulated;
		Accumulated = 0.f;
		SpawnStep(Seconds);
	}
}

TArray<ALureHotSpot*> ALureHotSpotSpawner::GetLiveHotSpots(const UWorld* World, double Time)
{
	TArray<ALureHotSpot*> Live;
	if (!World)
	{
		return Live;
	}
	for (TActorIterator<ALureHotSpot> It(const_cast<UWorld*>(World)); It; ++It)
	{
		ALureHotSpot* HotSpot = *It;
		if (IsValid(HotSpot) && !HotSpot->IsActorBeingDestroyed() && HotSpot->GetState().EndTime > Time)
		{
			Live.Add(HotSpot);
		}
	}
	Live.Sort([](const ALureHotSpot& A, const ALureHotSpot& B)
	{
		return A.GetState().SpawnTime != B.GetState().SpawnTime ? A.GetState().SpawnTime < B.GetState().SpawnTime : A.GetFName().LexicalLess(B.GetFName());
	});
	return Live;
}

TArray<FVector2D> ALureHotSpotSpawner::GetPlayerLocations() const
{
	TArray<FVector2D> Locations;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Locations;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr)
		{
			Locations.Add(FVector2D(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y));
		}
	}
	if (Locations.Num() == 0)
	{
		for (TActorIterator<ALurePlayerCharacter> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Locations.Add(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
			}
		}
		Locations.Sort([](const FVector2D& A, const FVector2D& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	}
	return Locations;
}

int32 ALureHotSpotSpawner::SpawnStep(float Seconds)
{
	using namespace LureHotSpotSpawnerPrivate;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return 0;
	}
	EnsureSeeded();
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	const double Now = FLureWaterQuery::GetTime(World);
	if (!bUseTableOverride && !LoadedTable)
	{
		LoadedTable = FLureWaterQuery::LoadHotSpotTable();
	}
	const UDataTable* Table = bUseTableOverride ? TableOverride.Get() : LoadedTable.Get();
	TArray<TPair<FName, FLureHotSpotRow>> Rows = FLureWaterQuery::GetHotSpotRows(Table);
	if (HotSpotTypes.Num() > 0)
	{
		Rows.RemoveAll([this](const TPair<FName, FLureHotSpotRow>& Row) { return !HotSpotTypes.Contains(Row.Key); });
	}
	const TArray<FLureWaterAreaInfo> Areas = FLureWaterQuery::GatherAreas(World);
	const FGameplayTag DefaultHabitat = Settings->GetDefaultWaterHabitatTag();
	// The default water only exists if no "everywhere" area without a depth band covers it.
	const bool bDefaultWater = !Areas.ContainsByPredicate([](const FLureWaterAreaInfo& Area)
	{
		return Area.Shape == ELureWaterAreaShape::Everywhere && Area.HabitatTag.IsValid() && Area.MinDepth <= 0.f && !(Area.MaxDepth > 0.f);
	});
	const TArray<FVector2D> Players = GetPlayerLocations();
	TArray<ALureHotSpot*> Live = GetLiveHotSpots(World, Now);
	const int32 Max = MaxHotSpots >= 0 ? MaxHotSpots : Settings->MaxHotSpots;

	int32 Spawned = 0;
	for (const TPair<FName, FLureHotSpotRow>& Entry : Rows)
	{
		const FLureHotSpotRow& Row = Entry.Value;
		const float Chance = FLureWaterRules::SpawnChance(Row, Seconds);
		if (Chance <= 0.f || Row.MaxPerArea <= 0)
		{
			continue;
		}
		for (int32 Index = 0; Index <= Areas.Num(); ++Index)
		{
			if (Live.Num() >= Max)
			{
				return Spawned;
			}
			const FLureWaterAreaInfo* Area = Index < Areas.Num() ? &Areas[Index] : nullptr;
			if (!Area && !bDefaultWater)
			{
				continue;
			}
			const FGameplayTag Habitat = Area ? Area->HabitatTag : DefaultHabitat;
			if ((Area && !Area->HasShape()) || !AllowsHabitat(Row, Habitat))
			{
				continue;
			}
			const FName AreaId = Area ? Area->AreaId : NAME_None;
			int32 Count = 0;
			for (const ALureHotSpot* HotSpot : Live)
			{
				Count += (HotSpot->GetState().TypeId == Entry.Key && HotSpot->GetState().AreaId == AreaId) ? 1 : 0;
			}
			if (Count >= Row.MaxPerArea || Rng.FRand() >= Chance)
			{
				continue;
			}
			if (ALureHotSpot* HotSpot = TrySpawn(Entry.Key, Row, Area, Areas, DefaultHabitat, Players, Live, Now))
			{
				Live.Add(HotSpot);
				++Spawned;
			}
		}
	}
	return Spawned;
}

ALureHotSpot* ALureHotSpotSpawner::TrySpawn(FName TypeId, const FLureHotSpotRow& Row, const FLureWaterAreaInfo* Area, TConstArrayView<FLureWaterAreaInfo> Areas,
	const FGameplayTag& DefaultHabitat, const TArray<FVector2D>& PlayerXY, TArray<ALureHotSpot*>& Live, double Now)
{
	const int32 Tries = FMath::Max(1, GetDefault<ULureWaterSettings>()->HotSpotSpawnTries);
	const FName AreaId = Area ? Area->AreaId : NAME_None;
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		FVector2D XY;
		if (!SamplePoint(Area, PlayerXY, XY))
		{
			return nullptr;
		}
		float WaterZ = 0.f;
		if (!IsGoodPoint(XY, AreaId, Row, Areas, DefaultHabitat, Live, Now, WaterZ))
		{
			continue;
		}
		const float Lifetime = FLureWaterRules::LifetimeFromRoll(Row, Rng.FRand());
		const int32 Seed = static_cast<int32>(Rng.GetUnsignedInt());
		return ALureHotSpot::SpawnHotSpot(GetWorld(), HotSpotClass, TypeId, Row, AreaId, FVector(XY.X, XY.Y, WaterZ), Seed, Now, Lifetime);
	}
	UE_LOG(LogLureWater, Verbose, TEXT("Hot spots: no good point for %s in %s after %d tries."), *TypeId.ToString(),
		AreaId.IsNone() ? TEXT("the default water") : *AreaId.ToString(), Tries);
	return nullptr;
}

bool ALureHotSpotSpawner::SamplePoint(const FLureWaterAreaInfo* Area, const TArray<FVector2D>& PlayerXY, FVector2D& OutXY)
{
	if (!Area || Area->Shape == ELureWaterAreaShape::Everywhere)
	{
		if (PlayerXY.Num() == 0)
		{
			return false; // unbounded water: only near players
		}
		const FVector2D& Player = PlayerXY[Rng.RandHelper(PlayerXY.Num())];
		const double Distance = FMath::Max(0.f, GetDefault<ULureWaterSettings>()->OpenWaterSpawnRadius) * FMath::Sqrt(Rng.FRand());
		const double Angle = Rng.FRand() * UE_DOUBLE_TWO_PI;
		OutXY = Player + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
		return true;
	}
	switch (Area->Shape)
	{
	case ELureWaterAreaShape::Circle:
	{
		const double Distance = Area->Radius * FMath::Sqrt(Rng.FRand());
		const double Angle = Rng.FRand() * UE_DOUBLE_TWO_PI;
		OutXY = Area->Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
		return true;
	}
	case ELureWaterAreaShape::Box:
	{
		const FVector2D Local((2.0 * Rng.FRand() - 1.0) * Area->HalfSize.X, (2.0 * Rng.FRand() - 1.0) * Area->HalfSize.Y);
		const double Radians = FMath::DegreesToRadians(static_cast<double>(Area->YawDegrees));
		const double Cos = FMath::Cos(Radians);
		const double Sin = FMath::Sin(Radians);
		OutXY = Area->Center + FVector2D(Local.X * Cos - Local.Y * Sin, Local.X * Sin + Local.Y * Cos);
		return true;
	}
	case ELureWaterAreaShape::Polygon:
	{
		const FBox2D Bounds = Area->GetBounds();
		if (!Bounds.bIsValid)
		{
			return false;
		}
		for (int32 Try = 0; Try < 16; ++Try)
		{
			const FVector2D Point(FMath::Lerp(Bounds.Min.X, Bounds.Max.X, static_cast<double>(Rng.FRand())),
				FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, static_cast<double>(Rng.FRand())));
			if (Area->Contains(Point))
			{
				OutXY = Point;
				return true;
			}
		}
		return false;
	}
	default:
		return false;
	}
}

bool ALureHotSpotSpawner::IsGoodPoint(const FVector2D& XY, FName AreaId, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
	const FGameplayTag& DefaultHabitat, const TArray<ALureHotSpot*>& Live, double Now, float& OutWaterZ) const
{
	using namespace LureHotSpotSpawnerPrivate;
	const UWorld* World = GetWorld();
	const float MinBiteDepth = FMath::Max(0.f, GetDefault<ULureWaterSettings>()->MinBiteDepth);
	float Depth = 0.f;
	if (!FLureWaterQuery::ProbeWater(World, XY, OutWaterZ, Depth))
	{
		return false; // no water, or land
	}
	const int32 Index = FLureWaterRules::FindAreaIndex(Areas, XY, Depth);
	if ((Index != INDEX_NONE ? Areas[Index].AreaId : NAME_None) != AreaId)
	{
		return false; // another area wins here
	}
	const FGameplayTag Habitat = Index != INDEX_NONE ? Areas[Index].HabitatTag : DefaultHabitat;
	if (!AllowsHabitat(Row, Habitat) || !Row.AllowsWater(Habitat, Depth) || Depth < MinBiteDepth)
	{
		return false;
	}
	for (const ALureHotSpot* Other : Live)
	{
		const FVector Center = Other->GetCenterAt(Now);
		if (FVector2D::Distance(XY, FVector2D(Center.X, Center.Y)) < Row.MinSpacing)
		{
			return false;
		}
	}
	// Where it will wander: fishable water of an allowed habitat on the same water level all the way round.
	const double Ring = static_cast<double>(Row.DriftRange) + 0.5 * Row.Radius;
	if (Ring >= 1.0)
	{
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Step / 8.0;
			const FVector2D Point = XY + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Ring;
			float PointZ = 0.f;
			float PointDepth = 0.f;
			if (!FLureWaterQuery::ProbeWater(World, Point, PointZ, PointDepth) || PointDepth < MinBiteDepth || FMath::Abs(PointZ - OutWaterZ) > 1.f)
			{
				return false;
			}
			const int32 PointIndex = FLureWaterRules::FindAreaIndex(Areas, Point, PointDepth);
			if (!AllowsHabitat(Row, PointIndex != INDEX_NONE ? Areas[PointIndex].HabitatTag : DefaultHabitat))
			{
				return false;
			}
		}
	}
	return true;
}

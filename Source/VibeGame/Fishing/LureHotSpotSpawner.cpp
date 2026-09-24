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

	/** XY is within Radius of some player (Radius <= 0 or no players: the rule is off). */
	bool IsNearAPlayer(const FVector2D& XY, const TArray<FVector2D>& PlayerXY, float Radius)
	{
		if (!(Radius > 0.f) || PlayerXY.Num() == 0)
		{
			return true;
		}
		const double RadiusSquared = static_cast<double>(Radius) * static_cast<double>(Radius);
		return PlayerXY.ContainsByPredicate([&XY, RadiusSquared](const FVector2D& Player) { return FVector2D::DistSquared(XY, Player) <= RadiusSquared; });
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
	using namespace LureHotSpotSpawnerPrivate;
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	const int32 Tries = FMath::Max(1, Settings->HotSpotSpawnTries);
	const FName AreaId = Area ? Area->AreaId : NAME_None;
	const float NearPlayer = Settings->HotSpotNearPlayerRadius;
	if (Area && Area->Shape != ELureWaterAreaShape::Everywhere && NearPlayer > 0.f && PlayerXY.Num() > 0)
	{
		// A bounded area nobody is near: don't try (it would take a cap slot out of every player's reach).
		const FBox2D Bounds = Area->GetBounds();
		const double NearSquared = static_cast<double>(NearPlayer) * static_cast<double>(NearPlayer);
		if (Bounds.bIsValid && !PlayerXY.ContainsByPredicate([&Bounds, NearSquared](const FVector2D& Player) { return Bounds.ComputeSquaredDistanceToPoint(Player) <= NearSquared; }))
		{
			return nullptr;
		}
	}
	const float MinLifetime = FLureWaterRules::LifetimeFromRoll(Row, 0.f);
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		FVector2D XY;
		if (!SamplePoint(Area, PlayerXY, XY))
		{
			return nullptr;
		}
		if (!IsNearAPlayer(XY, PlayerXY, NearPlayer))
		{
			continue;
		}
		float WaterZ = 0.f;
		if (!IsGoodPoint(XY, AreaId, Row, Areas, DefaultHabitat, Live, Now, WaterZ))
		{
			continue;
		}
		const float Lifetime = FLureWaterRules::LifetimeFromRoll(Row, Rng.FRand());
		const int32 Seed = static_cast<int32>(Rng.GetUnsignedInt());
		// Walk its real drift path: it stops (its life ends, it fades out) before its disc would touch land.
		const float Safe = SafeLifetime(XY, WaterZ, Row, Seed, Lifetime, Areas, DefaultHabitat);
		if (Safe < MinLifetime)
		{
			continue;
		}
		return ALureHotSpot::SpawnHotSpot(GetWorld(), HotSpotClass, TypeId, Row, AreaId, FVector(XY.X, XY.Y, WaterZ), Seed, Now, FMath::Min(Lifetime, Safe));
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
	// Its whole wander area: the center (above) and 3 rings (1/3, 2/3 and all of DriftRange + Radius / 2), 8 points
	// each. A rock inside the area but between the points is caught by the drift path walk (SafeLifetime).
	const double Outer = static_cast<double>(Row.DriftRange) + 0.5 * Row.Radius;
	if (Outer >= 1.0)
	{
		for (int32 RingIndex = 1; RingIndex <= 3; ++RingIndex)
		{
			const double Ring = Outer * RingIndex / 3.0;
			const int32 Points = 8;
			for (int32 Step = 0; Step < Points; ++Step)
			{
				const double Angle = UE_DOUBLE_TWO_PI * (Step + (RingIndex == 2 ? 0.5 : 0.0)) / Points; // the middle ring turned half a step
				if (!IsFishableAt(XY + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Ring, OutWaterZ, Row, Areas, DefaultHabitat))
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool ALureHotSpotSpawner::IsFishableAt(const FVector2D& Point, float WaterZ, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
	const FGameplayTag& DefaultHabitat) const
{
	using namespace LureHotSpotSpawnerPrivate;
	const float MinBiteDepth = FMath::Max(0.f, GetDefault<ULureWaterSettings>()->MinBiteDepth);
	float PointZ = 0.f;
	float PointDepth = 0.f;
	if (!FLureWaterQuery::ProbeWater(GetWorld(), Point, PointZ, PointDepth) || PointDepth < MinBiteDepth || FMath::Abs(PointZ - WaterZ) > 1.f)
	{
		return false;
	}
	const int32 PointIndex = FLureWaterRules::FindAreaIndex(Areas, Point, PointDepth);
	return AllowsHabitat(Row, PointIndex != INDEX_NONE ? Areas[PointIndex].HabitatTag : DefaultHabitat);
}

float ALureHotSpotSpawner::SafeLifetime(const FVector2D& Anchor, float WaterZ, const FLureHotSpotRow& Row, int32 Seed, float Lifetime,
	TConstArrayView<FLureWaterAreaInfo> Areas, const FGameplayTag& DefaultHabitat) const
{
	// The same numbers ALureHotSpot::SpawnHotSpot keeps in its state.
	const double Radius = FMath::Max(1.0, FMath::IsFinite(Row.Radius) ? static_cast<double>(Row.Radius) : 1.0);
	const float Range = FMath::IsFinite(Row.DriftRange) ? FMath::Max(0.f, Row.DriftRange) : 0.f;
	const float Speed = FMath::IsFinite(Row.DriftSpeed) ? FMath::Max(0.f, Row.DriftSpeed) : 0.f;
	if (!(Range > 0.f && Speed > 0.f && Lifetime > 0.f))
	{
		return Lifetime; // it stays at its anchor, which IsGoodPoint checked
	}
	// The disc is the center and 8 points on its edge, probed whenever the center has moved a quarter radius since the last
	// probe. Time steps are short enough that the center never moves more than that between two steps (its peak speed is at
	// most sqrt(2) x the RMS DriftSpeed, FLureWaterRules::DriftOffset).
	const double Spacing = FMath::Max(5.0, 0.25 * Radius);
	const double MaxStep = Spacing / (UE_DOUBLE_SQRT_2 * Speed);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt32(Lifetime / MaxStep), 1, 4096);
	const double Dt = static_cast<double>(Lifetime) / Steps;
	auto DiscIsFishable = [&](const FVector2D& Center)
	{
		if (!IsFishableAt(Center, WaterZ, Row, Areas, DefaultHabitat))
		{
			return false;
		}
		for (int32 Point = 0; Point < 8; ++Point)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Point / 8.0;
			if (!IsFishableAt(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius, WaterZ, Row, Areas, DefaultHabitat))
			{
				return false;
			}
		}
		return true;
	};
	FVector2D LastProbed = Anchor;
	bool bProbed = false;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const double Age = Step * Dt;
		const FVector2D Center = Anchor + FLureWaterRules::DriftOffset(Range, Speed, Seed, Age);
		if (bProbed && FVector2D::DistSquared(Center, LastProbed) < Spacing * Spacing)
		{
			continue;
		}
		if (!DiscIsFishable(Center))
		{
			return static_cast<float>(FMath::Max(0.0, Age - Dt)); // the last step before it would touch land
		}
		LastProbed = Center;
		bProbed = true;
	}
	return Lifetime;
}

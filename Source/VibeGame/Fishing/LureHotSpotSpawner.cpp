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

	/** Squared distance from XY to a bounded area's outline (0 inside it). */
	double SquaredDistanceToArea(const FLureWaterAreaInfo& Area, const FVector2D& XY)
	{
		switch (Area.Shape)
		{
		case ELureWaterAreaShape::Circle:
		{
			const double Outside = FMath::Max(0.0, FVector2D::Distance(XY, Area.Center) - static_cast<double>(Area.Radius));
			return Outside * Outside;
		}
		case ELureWaterAreaShape::Box:
		{
			// Into the box's frame (the inverse of SamplePoint's rotation).
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Area.YawDegrees));
			const double Cos = FMath::Cos(Radians);
			const double Sin = FMath::Sin(Radians);
			const FVector2D Offset = XY - Area.Center;
			const FVector2D Local(Offset.X * Cos + Offset.Y * Sin, -Offset.X * Sin + Offset.Y * Cos);
			const FVector2D Outside(FMath::Max(0.0, FMath::Abs(Local.X) - Area.HalfSize.X), FMath::Max(0.0, FMath::Abs(Local.Y) - Area.HalfSize.Y));
			return Outside.SizeSquared();
		}
		case ELureWaterAreaShape::Polygon:
		{
			if (Area.Contains(XY))
			{
				return 0.0;
			}
			double Best = TNumericLimits<double>::Max();
			for (int32 I = 0, J = Area.Polygon.Num() - 1; I < Area.Polygon.Num(); J = I++)
			{
				Best = FMath::Min(Best, FVector2D::DistSquared(XY, FMath::ClosestPointOnSegment2D(XY, Area.Polygon[J], Area.Polygon[I])));
			}
			return Best;
		}
		default:
			return 0.0; // everywhere
		}
	}

	/**
	 *  Can anyone reach Area (null = the default water) now? Unbounded water needs a player (its points are drawn around
	 *  one): OutReachers = every player. A bounded area needs a player within Near of its outline: OutReachers = those; with
	 *  the rule off (Near <= 0) or no player in the world it is reachable as a whole (OutReachers empty).
	 */
	bool CanReach(const FLureWaterAreaInfo* Area, const TArray<FVector2D>& Players, float Near, TArray<FVector2D>& OutReachers)
	{
		OutReachers.Reset();
		if (!Area || Area->Shape == ELureWaterAreaShape::Everywhere)
		{
			OutReachers = Players;
			return Players.Num() > 0;
		}
		if (!(Near > 0.f) || Players.Num() == 0)
		{
			return true;
		}
		const double NearSquared = static_cast<double>(Near) * static_cast<double>(Near);
		for (const FVector2D& Player : Players)
		{
			if (SquaredDistanceToArea(*Area, Player) <= NearSquared)
			{
				OutReachers.Add(Player);
			}
		}
		return OutReachers.Num() > 0;
	}

	FString AreaName(const FLureWaterAreaInfo* Area)
	{
		return Area ? Area->AreaId.ToString() : FString(TEXT("the default water"));
	}

	double NearestPlayerDistance(const FVector2D& XY, const TArray<FVector2D>& Players)
	{
		double Best = -1.0;
		for (const FVector2D& Player : Players)
		{
			const double Distance = FVector2D::Distance(XY, Player);
			Best = Best < 0.0 ? Distance : FMath::Min(Best, Distance);
		}
		return Best;
	}
}

// ---- FLureHotSpotRejects / FLureHotSpotStepStats ----

void FLureHotSpotRejects::Append(const FLureHotSpotRejects& Other)
{
	for (int32 Index = 0; Index < static_cast<int32>(ELureHotSpotReject::Count); ++Index)
	{
		Counts[Index] += Other.Counts[Index];
	}
}

int32 FLureHotSpotRejects::Total() const
{
	int32 Sum = 0;
	for (const int32 Count : Counts)
	{
		Sum += Count;
	}
	return Sum;
}

FString FLureHotSpotRejects::ToString() const
{
	TArray<FString> Parts;
	for (int32 Index = 0; Index < static_cast<int32>(ELureHotSpotReject::Count); ++Index)
	{
		if (Counts[Index] > 0)
		{
			Parts.Add(FString::Printf(TEXT("%s %d"), ReasonName(static_cast<ELureHotSpotReject>(Index)), Counts[Index]));
		}
	}
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("none"));
}

const TCHAR* FLureHotSpotRejects::ReasonName(ELureHotSpotReject Reason)
{
	switch (Reason)
	{
	case ELureHotSpotReject::None: return TEXT("good");
	case ELureHotSpotReject::NoSample: return TEXT("no sample in reach");
	case ELureHotSpotReject::NotNearPlayer: return TEXT("not near a player");
	case ELureHotSpotReject::NoWater: return TEXT("no water");
	case ELureHotSpotReject::OtherArea: return TEXT("other area");
	case ELureHotSpotReject::RowWater: return TEXT("habitat or depth");
	case ELureHotSpotReject::TooClose: return TEXT("too close");
	case ELureHotSpotReject::WanderLand: return TEXT("wander area on land");
	case ELureHotSpotReject::WanderShallow: return TEXT("wander area shallow");
	case ELureHotSpotReject::WanderLevel: return TEXT("wander area other level");
	case ELureHotSpotReject::WanderHabitat: return TEXT("wander area habitat");
	case ELureHotSpotReject::DriftShort: return TEXT("drift hits land");
	default: return TEXT("?");
	}
}

FString FLureHotSpotStepStats::ToString() const
{
	return FString::Printf(TEXT("check of %.1f s, %d player(s), %d row(s) x %d area(s), live %d/%d: spawned %d | rolled %d (passed %d), due retries %d, ")
		TEXT("no point %d | out of reach %d, full %d, level full %d, wrong water %d, nothing banked %d | rejected points: %s"),
		Seconds, Players, Rows, Areas, LiveBefore, Max, Spawned, Rolls, RollsPassed, DueRetries, NoPoint, OutOfReach, Full, LevelFull, WrongWater,
		NothingBanked, *Rejects.ToString());
}

// ---- ALureHotSpotSpawner ----

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

float ALureHotSpotSpawner::GetBankedSeconds(FName TypeId, FName AreaId) const
{
	const FLureHotSpotClock* Clock = Clocks.Find(MakeTuple(TypeId, AreaId));
	return Clock ? Clock->Bank : 0.f;
}

int32 ALureHotSpotSpawner::GetDueRetries(FName TypeId, FName AreaId) const
{
	const FLureHotSpotClock* Clock = Clocks.Find(MakeTuple(TypeId, AreaId));
	return Clock ? Clock->DueRetries : 0;
}

void ALureHotSpotSpawner::BeginPlay()
{
	Super::BeginPlay();
	EnsureSeeded();
}

void ALureHotSpotSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	// A level actor that doesn't replicate has authority on every machine: only the server (or standalone) spawns.
	if (!World || World->GetNetMode() == NM_Client || !bAutoSpawn)
	{
		if (!bLoggedNotTicking)
		{
			bLoggedNotTicking = true;
			UE_LOG(LogLureHotSpot, Verbose, TEXT("%s: no automatic spawn checks (%s)."), *GetName(),
				!World ? TEXT("no world") : World->GetNetMode() == NM_Client ? TEXT("a client: the server spawns, hot spots replicate") : TEXT("SetAutoSpawn(false)"));
		}
		return;
	}
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	if (!bPrewarmed)
	{
		bPrewarmed = true; // the first tick: every level actor is there now
		Accumulated = 0.f;
		const float Prewarm = FMath::Max(0.f, Settings->HotSpotPrewarmSeconds);
		UE_LOG(LogLureHotSpot, Verbose, TEXT("%s: the level's first check counts as %.0f s (HotSpotPrewarmSeconds)."), *GetName(), Prewarm);
		SpawnStep(Prewarm);
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
	LastStats = FLureHotSpotStepStats();
	const float Step = FMath::IsFinite(Seconds) ? FMath::Max(0.f, Seconds) : 0.f;
	LastStats.Seconds = Step;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogLureHotSpot, Verbose, TEXT("%s: no spawn check (%s)."), *GetName(), World ? TEXT("a client: the server spawns") : TEXT("no world"));
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
		if (Rows.Num() == 0 && !bWarnedNoRows)
		{
			bWarnedNoRows = true; // a data problem (a misspelled type in the layout's hot_spots marker): say it once
			UE_LOG(LogLureHotSpot, Warning, TEXT("%s: its HotSpotTypes (%s) name no row of %s: no hot spots in this level."), *GetName(),
				*FString::JoinBy(HotSpotTypes, TEXT(", "), [](const FName& Type) { return Type.ToString(); }), Table ? *Table->GetPathName() : TEXT("the built-in row"));
		}
	}
	// A row that can never spawn takes no part (and no roll).
	Rows.RemoveAll([](const TPair<FName, FLureHotSpotRow>& Row) { return !(Row.Value.SpawnInterval > 0.f) || Row.Value.MaxPerArea <= 0; });
	LastStats.Rows = Rows.Num();
	if (Rows.Num() == 0)
	{
		UE_LOG(LogLureHotSpot, Verbose, TEXT("%s: no row can spawn (HotSpotTypes, SpawnInterval > 0 and MaxPerArea > 0)."), *GetName());
		return 0;
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
	const float Near = Settings->HotSpotNearPlayerRadius;
	const float BankCap = FMath::Max(Step, FMath::Max(0.f, Settings->HotSpotPrewarmSeconds));
	LastStats.Players = Players.Num();
	LastStats.Areas = Areas.Num() + (bDefaultWater ? 1 : 0);
	LastStats.LiveBefore = Live.Num();
	LastStats.Max = Max;

	int32 Spawned = 0;
	TArray<FVector2D> Reachers;
	for (const TPair<FName, FLureHotSpotRow>& Entry : Rows)
	{
		const FLureHotSpotRow& Row = Entry.Value;
		for (int32 Index = 0; Index <= Areas.Num(); ++Index)
		{
			const FLureWaterAreaInfo* Area = Index < Areas.Num() ? &Areas[Index] : nullptr;
			if (!Area && !bDefaultWater)
			{
				continue; // every bit of water belongs to an area: the level has no default water
			}
			if ((Area && !Area->HasShape()) || !AllowsHabitat(Row, Area ? Area->HabitatTag : DefaultHabitat))
			{
				++LastStats.WrongWater;
				UE_LOG(LogLureHotSpot, VeryVerbose, TEXT("%s in %s: never (no shape, or a habitat the row doesn't allow)."), *Entry.Key.ToString(), *AreaName(Area));
				continue;
			}
			const FName AreaId = Area ? Area->AreaId : NAME_None;
			FLureHotSpotClock& Clock = Clocks.FindOrAdd(MakeTuple(Entry.Key, AreaId));
			int32 Count = 0;
			for (const ALureHotSpot* HotSpot : Live)
			{
				Count += (HotSpot->GetState().TypeId == Entry.Key && HotSpot->GetState().AreaId == AreaId) ? 1 : 0;
			}
			if (Count >= Row.MaxPerArea)
			{
				Clock = FLureHotSpotClock(); // its waiting starts again when one ends
				++LastStats.Full;
				UE_LOG(LogLureHotSpot, VeryVerbose, TEXT("%s in %s: full (%d)."), *Entry.Key.ToString(), *AreaName(Area), Count);
				continue;
			}
			if (Clock.DueRetries <= 0)
			{
				Clock.Bank = FMath::Min(Clock.Bank + Step, BankCap);
			}
			if (Live.Num() >= Max)
			{
				++LastStats.LevelFull;
				UE_LOG(LogLureHotSpot, VeryVerbose, TEXT("%s in %s: the level is full (%d/%d); %.0f s banked."), *Entry.Key.ToString(), *AreaName(Area), Live.Num(), Max, Clock.Bank);
				continue;
			}
			if (!CanReach(Area, Players, Near, Reachers))
			{
				++LastStats.OutOfReach;
				UE_LOG(LogLureHotSpot, VeryVerbose, TEXT("%s in %s: nobody within reach; %.0f s banked."), *Entry.Key.ToString(), *AreaName(Area), Clock.Bank);
				continue;
			}
			const bool bRetry = Clock.DueRetries > 0;
			if (bRetry)
			{
				--Clock.DueRetries;
				++LastStats.DueRetries;
			}
			else
			{
				const float Banked = Clock.Bank;
				const float Chance = FLureWaterRules::SpawnChance(Row, Banked);
				if (!(Chance > 0.f))
				{
					++LastStats.NothingBanked;
					continue;
				}
				++LastStats.Rolls;
				Clock.Bank = 0.f;
				if (Rng.FRand() >= Chance)
				{
					UE_LOG(LogLureHotSpot, VeryVerbose, TEXT("%s in %s: the roll for %.1f s (%.1f %%) failed."), *Entry.Key.ToString(), *AreaName(Area), Banked, 100.f * Chance);
					continue;
				}
				++LastStats.RollsPassed;
			}
			FLureHotSpotRejects Rejects;
			ALureHotSpot* HotSpot = TrySpawn(Entry.Key, Row, Area, Areas, DefaultHabitat, Players, Reachers, Live, Now, Rejects);
			LastStats.Rejects.Append(Rejects);
			if (HotSpot)
			{
				Live.Add(HotSpot);
				++Spawned;
				Clock.DueRetries = 0;
				UE_LOG(LogLureHotSpot, Verbose, TEXT("%s: spawned %s in %s at (%.0f, %.0f), %.0f cm from the nearest player, for %.0f s."), *GetName(),
					*Entry.Key.ToString(), *AreaName(Area), HotSpot->GetState().Anchor.X, HotSpot->GetState().Anchor.Y,
					NearestPlayerDistance(FVector2D(HotSpot->GetState().Anchor.X, HotSpot->GetState().Anchor.Y), Players),
					HotSpot->GetState().EndTime - HotSpot->GetState().SpawnTime);
			}
			else
			{
				++LastStats.NoPoint;
				if (!bRetry)
				{
					Clock.DueRetries = DueRetryChecks;
				}
				UE_LOG(LogLureHotSpot, Verbose, TEXT("%s: no good point for %s in %s after %d tries (%s); %s."), *GetName(), *Entry.Key.ToString(), *AreaName(Area),
					FMath::Max(1, Settings->HotSpotSpawnTries), *Rejects.ToString(),
					*(Clock.DueRetries > 0 ? FString::Printf(TEXT("still due for %d more check(s) in reach"), Clock.DueRetries) : FString(TEXT("dropped"))));
			}
		}
	}
	LastStats.Spawned = Spawned;
	UE_LOG(LogLureHotSpot, Verbose, TEXT("%s: %s"), *GetName(), *LastStats.ToString());
	return Spawned;
}

ALureHotSpot* ALureHotSpotSpawner::TrySpawn(FName TypeId, const FLureHotSpotRow& Row, const FLureWaterAreaInfo* Area, TConstArrayView<FLureWaterAreaInfo> Areas,
	const FGameplayTag& DefaultHabitat, const TArray<FVector2D>& Players, const TArray<FVector2D>& Reachers, TArray<ALureHotSpot*>& Live, double Now,
	FLureHotSpotRejects& OutRejects)
{
	using namespace LureHotSpotSpawnerPrivate;
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	const int32 Tries = FMath::Max(1, Settings->HotSpotSpawnTries);
	const FName AreaId = Area ? Area->AreaId : NAME_None;
	const float MinLifetime = FLureWaterRules::LifetimeFromRoll(Row, 0.f);
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		FVector2D XY;
		if (!SamplePoint(Area, Reachers, XY))
		{
			OutRejects.Add(ELureHotSpotReject::NoSample);
			continue;
		}
		if (!IsNearAPlayer(XY, Players, Settings->HotSpotNearPlayerRadius))
		{
			OutRejects.Add(ELureHotSpotReject::NotNearPlayer);
			continue;
		}
		float WaterZ = 0.f;
		const ELureHotSpotReject Reason = CheckPoint(XY, AreaId, Row, Areas, DefaultHabitat, Live, Now, WaterZ);
		if (Reason != ELureHotSpotReject::None)
		{
			OutRejects.Add(Reason);
			continue;
		}
		const float Lifetime = FLureWaterRules::LifetimeFromRoll(Row, Rng.FRand());
		const int32 Seed = static_cast<int32>(Rng.GetUnsignedInt());
		// Walk its real drift path: it stops (its life ends, it fades out) before its disc would touch land.
		const float Safe = SafeLifetime(XY, WaterZ, Row, Seed, Lifetime, Areas, DefaultHabitat);
		if (Safe < MinLifetime)
		{
			OutRejects.Add(ELureHotSpotReject::DriftShort);
			continue;
		}
		return ALureHotSpot::SpawnHotSpot(GetWorld(), HotSpotClass, TypeId, Row, AreaId, FVector(XY.X, XY.Y, WaterZ), Seed, Now, FMath::Min(Lifetime, Safe));
	}
	return nullptr;
}

bool ALureHotSpotSpawner::SamplePoint(const FLureWaterAreaInfo* Area, const TArray<FVector2D>& Reachers, FVector2D& OutXY)
{
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	if (!Area || Area->Shape == ELureWaterAreaShape::Everywhere)
	{
		if (Reachers.Num() == 0)
		{
			return false; // unbounded water: only near players
		}
		const FVector2D& Player = Reachers[Rng.RandHelper(Reachers.Num())];
		const double Distance = FMath::Max(0.f, Settings->OpenWaterSpawnRadius) * FMath::Sqrt(Rng.FRand());
		const double Angle = Rng.FRand() * UE_DOUBLE_TWO_PI;
		OutXY = Player + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
		return true;
	}
	const double Near = Settings->HotSpotNearPlayerRadius;
	if (Reachers.Num() > 0 && Near > 0.0)
	{
		// Only the part of the area within one player's reach (T-027c): drawing over the whole outline and throwing away
		// the points out of reach wasted nearly every try when a player stood at the edge of a big area.
		const FVector2D& Player = Reachers[Rng.RandHelper(Reachers.Num())];
		const FBox2D Bounds = Area->GetBounds();
		if (!Bounds.bIsValid)
		{
			return false;
		}
		const FBox2D Window = Bounds.Overlap(FBox2D(Player - FVector2D(Near, Near), Player + FVector2D(Near, Near)));
		if (!Window.bIsValid)
		{
			return false;
		}
		for (int32 Sample = 0; Sample < WindowSamples; ++Sample)
		{
			const FVector2D Point(FMath::Lerp(Window.Min.X, Window.Max.X, static_cast<double>(Rng.FRand())),
				FMath::Lerp(Window.Min.Y, Window.Max.Y, static_cast<double>(Rng.FRand())));
			if (FVector2D::DistSquared(Point, Player) <= Near * Near && Area->Contains(Point))
			{
				OutXY = Point;
				return true;
			}
		}
		return false;
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
		for (int32 Sample = 0; Sample < WindowSamples; ++Sample)
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

ELureHotSpotReject ALureHotSpotSpawner::CheckPoint(const FVector2D& XY, FName AreaId, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
	const FGameplayTag& DefaultHabitat, const TArray<ALureHotSpot*>& Live, double Now, float& OutWaterZ) const
{
	using namespace LureHotSpotSpawnerPrivate;
	const UWorld* World = GetWorld();
	const float MinBiteDepth = FMath::Max(0.f, GetDefault<ULureWaterSettings>()->MinBiteDepth);
	float Depth = 0.f;
	if (!FLureWaterQuery::ProbeWater(World, XY, OutWaterZ, Depth))
	{
		return ELureHotSpotReject::NoWater; // no water, or land
	}
	const int32 Index = FLureWaterRules::FindAreaIndex(Areas, XY, Depth);
	if ((Index != INDEX_NONE ? Areas[Index].AreaId : NAME_None) != AreaId)
	{
		return ELureHotSpotReject::OtherArea;
	}
	const FGameplayTag Habitat = Index != INDEX_NONE ? Areas[Index].HabitatTag : DefaultHabitat;
	if (!AllowsHabitat(Row, Habitat) || !Row.AllowsWater(Habitat, Depth) || Depth < MinBiteDepth)
	{
		return ELureHotSpotReject::RowWater;
	}
	for (const ALureHotSpot* Other : Live)
	{
		const FVector Center = Other->GetCenterAt(Now);
		if (FVector2D::Distance(XY, FVector2D(Center.X, Center.Y)) < Row.MinSpacing)
		{
			return ELureHotSpotReject::TooClose;
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
				const ELureHotSpotReject Reason = CheckFishableAt(XY + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Ring, OutWaterZ, Row, Areas, DefaultHabitat);
				if (Reason != ELureHotSpotReject::None)
				{
					return Reason;
				}
			}
		}
	}
	return ELureHotSpotReject::None;
}

ELureHotSpotReject ALureHotSpotSpawner::CheckFishableAt(const FVector2D& Point, float WaterZ, const FLureHotSpotRow& Row, TConstArrayView<FLureWaterAreaInfo> Areas,
	const FGameplayTag& DefaultHabitat) const
{
	using namespace LureHotSpotSpawnerPrivate;
	const float MinBiteDepth = FMath::Max(0.f, GetDefault<ULureWaterSettings>()->MinBiteDepth);
	float PointZ = 0.f;
	float PointDepth = 0.f;
	if (!FLureWaterQuery::ProbeWater(GetWorld(), Point, PointZ, PointDepth))
	{
		return ELureHotSpotReject::WanderLand;
	}
	if (PointDepth < MinBiteDepth)
	{
		return ELureHotSpotReject::WanderShallow;
	}
	if (FMath::Abs(PointZ - WaterZ) > 1.f)
	{
		return ELureHotSpotReject::WanderLevel;
	}
	const int32 PointIndex = FLureWaterRules::FindAreaIndex(Areas, Point, PointDepth);
	return AllowsHabitat(Row, PointIndex != INDEX_NONE ? Areas[PointIndex].HabitatTag : DefaultHabitat) ? ELureHotSpotReject::None : ELureHotSpotReject::WanderHabitat;
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
		return Lifetime; // it stays at its anchor, which CheckPoint checked
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
		if (CheckFishableAt(Center, WaterZ, Row, Areas, DefaultHabitat) != ELureHotSpotReject::None)
		{
			return false;
		}
		for (int32 Point = 0; Point < 8; ++Point)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Point / 8.0;
			if (CheckFishableAt(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius, WaterZ, Row, Areas, DefaultHabitat) != ELureHotSpotReject::None)
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

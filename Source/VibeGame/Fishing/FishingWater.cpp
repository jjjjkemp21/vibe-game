// Lure: "fish anywhere" rules and world lookups (T-027). Rules: docs/specs/fishing-water-rules.md.

#include "Fishing/FishingWater.h"
#include "CollisionQueryParams.h"
#include "Engine/DataTable.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/LureFishingSettings.h"
#include "Fishing/LureHotSpot.h"
#include "Fishing/LureWaterArea.h"
#include "Fishing/LureWaterSettings.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

namespace LureWaterPrivate
{
	float FiniteOr(float Value, float Fallback)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	/** "Bait.Squid" -> "Squid" (the last part of a tag), "no bait" for none. */
	FString ShortTagName(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return TEXT("no bait");
		}
		FString Name = Tag.GetTagName().ToString();
		int32 Dot = INDEX_NONE;
		if (Name.FindLastChar(TEXT('.'), Dot))
		{
			Name.RightChopInline(Dot + 1);
		}
		return Name;
	}

	FString HourText(float Hours)
	{
		const int32 Minutes = FMath::RoundToInt(FMath::Clamp(Hours, 0.f, 24.f) * 60.f);
		return FString::Printf(TEXT("%02d:%02d"), Minutes / 60, Minutes % 60);
	}

	bool IsSpeciesTable(const UDataTable* Table)
	{
		return Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(FFishSpeciesRow::StaticStruct());
	}

	bool IsHotSpotTable(const UDataTable* Table)
	{
		return Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(FLureHotSpotRow::StaticStruct());
	}
}

FLureBiteRules FLureBiteRules::FromSettings()
{
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	FLureBiteRules Rules;
	Rules.MinBiteDepth = LureWaterPrivate::FiniteOr(Settings->MinBiteDepth, 0.f);
	Rules.GapFallbackHabitats = Settings->GetGapFallbackTags();
	return Rules;
}

// ---- Water areas ----

bool FLureWaterRules::IsBetterArea(const FLureWaterAreaInfo& A, const FLureWaterAreaInfo& B)
{
	if (A.Priority != B.Priority)
	{
		return A.Priority > B.Priority;
	}
	const double SizeA = A.GetSize();
	const double SizeB = B.GetSize();
	if (SizeA != SizeB)
	{
		return SizeA < SizeB;
	}
	return A.AreaId.LexicalLess(B.AreaId);
}

int32 FLureWaterRules::FindAreaIndex(TConstArrayView<FLureWaterAreaInfo> Areas, const FVector2D& XY, float DepthCm)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Areas.Num(); ++Index)
	{
		const FLureWaterAreaInfo& Area = Areas[Index];
		if (!Area.HabitatTag.IsValid() || !Area.AcceptsDepth(DepthCm) || !Area.Contains(XY))
		{
			continue;
		}
		if (Best == INDEX_NONE || IsBetterArea(Area, Areas[Best]))
		{
			Best = Index;
		}
	}
	return Best;
}

FLureWaterContext FLureWaterRules::MakeWaterContext(TConstArrayView<FLureWaterAreaInfo> Areas, const FVector2D& XY, float WaterZ, float DepthCm,
	const FGameplayTag& DefaultHabitat)
{
	FLureWaterContext Context;
	Context.bOnWater = true;
	Context.WaterZ = WaterZ;
	Context.DepthCm = FMath::IsFinite(DepthCm) ? FMath::Max(0.f, DepthCm) : 0.f;
	const int32 Index = FindAreaIndex(Areas, XY, Context.DepthCm);
	if (Index != INDEX_NONE)
	{
		const FLureWaterAreaInfo& Area = Areas[Index];
		Context.Source = Area.Source;
		Context.AreaId = Area.AreaId;
		Context.AreaName = Area.GetLabel();
		Context.HabitatTag = Area.HabitatTag;
		Context.RegionTag = Area.RegionTag;
		Context.Luck = FMath::IsFinite(Area.Luck) ? FMath::Max(0.f, Area.Luck) : 0.f;
	}
	else
	{
		Context.Source = ELureWaterSource::Default;
		Context.HabitatTag = DefaultHabitat;
	}
	return Context;
}

FLureWaterAreaInfo FLureWaterRules::AreaFromLegacySpot(const FLureFishingSpot& Spot, int32 Priority)
{
	FLureWaterAreaInfo Area;
	Area.AreaId = Spot.SpotId;
	Area.DisplayName = Spot.DisplayName;
	Area.HabitatTag = Spot.HabitatTag;
	Area.RegionTag = Spot.RegionTag;
	Area.Priority = Priority;
	Area.Luck = Spot.Luck;
	Area.Shape = ELureWaterAreaShape::Circle;
	Area.Center = FVector2D(Spot.Location.X, Spot.Location.Y);
	Area.Radius = Spot.Radius;
	Area.Source = ELureWaterSource::LegacySpot;
	return Area;
}

// ---- The bite ----

bool FLureWaterRules::HasSpeciesIgnoringBait(const FFishTables& Tables, const FGameplayTag& Habitat, const FGameplayTag& Region, float Hours,
	const FGameplayTag& Weather)
{
	if (!LureWaterPrivate::IsSpeciesTable(Tables.Species))
	{
		return false;
	}
	FFishRollContext Context;
	Context.HabitatTag = Habitat;
	Context.RegionTag = Region;
	Context.TimeOfDayHours = Hours;
	Context.WeatherTag = Weather;
	for (const TPair<FName, uint8*>& Pair : Tables.Species->GetRowMap())
	{
		const FFishSpeciesRow& Species = *reinterpret_cast<const FFishSpeciesRow*>(Pair.Value);
		// "With the right bait": offer each bait it takes (an empty list takes any bait, or none).
		if (Species.AcceptedBait.Num() == 0)
		{
			Context.BaitTag = FGameplayTag();
			if (FFishRoll::IsSpeciesEligible(Species, Context))
			{
				return true;
			}
			continue;
		}
		for (const FGameplayTag& Bait : Species.AcceptedBait)
		{
			if (!Bait.IsValid())
			{
				continue;
			}
			Context.BaitTag = Bait;
			if (FFishRoll::IsSpeciesEligible(Species, Context))
			{
				return true;
			}
		}
	}
	return false;
}

FGameplayTag FLureWaterRules::ResolveBiteHabitat(const FFishTables& Tables, const FGameplayTag& WaterHabitat, const FGameplayTag& Region, float Hours,
	const FGameplayTag& Weather, TConstArrayView<FGameplayTag> Fallbacks, bool& bOutFallback, bool& bOutAny)
{
	using namespace LureWaterPrivate;
	bOutFallback = false;
	bOutAny = true;
	if (HasSpeciesIgnoringBait(Tables, WaterHabitat, Region, Hours, Weather))
	{
		return WaterHabitat;
	}
	const int32 Hour = FMath::Clamp(FMath::FloorToInt(FFishRoll::NormalizeHours(Hours)), 0, 23);
	const FName HabitatName = WaterHabitat.IsValid() ? WaterHabitat.GetTagName() : FName(TEXT("NoHabitat"));
	for (const FGameplayTag& Fallback : Fallbacks)
	{
		if (Fallback.IsValid() && Fallback != WaterHabitat && HasSpeciesIgnoringBait(Tables, Fallback, Region, Hours, Weather))
		{
			bOutFallback = true;
			// A data gap (T-009 adds species): logged once per habitat and hour of the clock, never per bite.
			if (FFishRoll::RememberDataWarning(Tables.Species, HabitatName, FString::Printf(TEXT("water gap %02d"), Hour)))
			{
				UE_LOG(LogLureWater, Warning, TEXT("Data gap: no species of %s can bite at %s (region %s); bites there use the fallback habitat %s until the species data fills in (T-009)."),
					*WaterHabitat.ToString(), *HourText(FFishRoll::NormalizeHours(Hours)), *Region.ToString(), *Fallback.ToString());
			}
			return Fallback;
		}
	}
	bOutAny = false;
	if (IsSpeciesTable(Tables.Species) && FFishRoll::RememberDataWarning(Tables.Species, HabitatName, FString::Printf(TEXT("water dead %02d"), Hour)))
	{
		UE_LOG(LogLureWater, Warning, TEXT("Data gap: no species of %s can bite at %s (region %s), and no gap fallback habitat has one: nothing bites there. Add a species or a GapFallbackHabitats entry."),
			*WaterHabitat.ToString(), *HourText(FFishRoll::NormalizeHours(Hours)), *Region.ToString());
	}
	return WaterHabitat;
}

FFishRollContext FLureWaterRules::MakeBiteContext(const FLureWaterContext& Water, const FGameplayTag& Habitat, const FLureHotSpotBonus& HotSpot,
	const FLureFishingEnvironment& Environment, int32 Seed)
{
	using namespace LureWaterPrivate;
	FFishRollContext Context;
	Context.Seed = Seed;
	Context.TimeOfDayHours = Environment.TimeOfDayHours;
	Context.WeatherTag = Environment.WeatherTag;
	Context.BaitTag = Environment.BaitTag;
	Context.HabitatTag = Habitat;
	Context.RegionTag = Water.RegionTag.IsValid() ? Water.RegionTag : Environment.DefaultRegionTag;
	Context.Luck = FiniteOr(Water.Luck, 0.f) + FiniteOr(Environment.GearLuck, 0.f) + (HotSpot.IsActive() ? FiniteOr(HotSpot.LuckBonus, 0.f) : 0.f); // the roll clamps
	if (HotSpot.IsActive())
	{
		Context.SizeBonus = FMath::Clamp(FiniteOr(HotSpot.SizeBonus, 0.f), 0.f, 1.f);
		const float Value = FiniteOr(HotSpot.ValueMultiplier, 1.f);
		Context.ValueMultiplier = Value > 0.f ? Value : 1.f;
	}
	return Context;
}

FLureBiteDecision FLureWaterRules::DecideBite(const FFishTables& Tables, const FLureWaterContext& Water, const FLureHotSpotBonus& HotSpot,
	const FLureFishingEnvironment& Environment, const FLureBiteRules& Rules, int32 Seed)
{
	FLureBiteDecision Decision;
	Decision.BiteHabitat = Water.HabitatTag;
	if (!Water.bOnWater)
	{
		Decision.Reason = ELureNoBiteReason::NotWater;
		return Decision;
	}
	Decision.Context = MakeBiteContext(Water, Water.HabitatTag, HotSpot, Environment, Seed);
	if (!(Water.DepthCm >= Rules.MinBiteDepth))
	{
		Decision.Reason = ELureNoBiteReason::TooShallow;
		return Decision;
	}
	const FGameplayTag Region = Decision.Context.RegionTag;
	bool bAny = false;
	Decision.BiteHabitat = ResolveBiteHabitat(Tables, Water.HabitatTag, Region, Environment.TimeOfDayHours, Environment.WeatherTag,
		Rules.GapFallbackHabitats, Decision.bUsedFallback, bAny);
	Decision.Context.HabitatTag = Decision.BiteHabitat;
	if (!bAny)
	{
		Decision.Reason = ELureNoBiteReason::NoSpecies;
		return Decision;
	}
	FName SpeciesId;
	if (!FFishRoll::PickSpecies(Tables, Decision.Context, SpeciesId))
	{
		Decision.Reason = ELureNoBiteReason::WrongBait; // species live here now (checked above), but none takes this bait
	}
	return Decision;
}

// ---- Placeholder HUD text ----

FString FLureWaterRules::NoBiteText(ELureNoBiteReason Reason, const FGameplayTag& Bait)
{
	switch (Reason)
	{
	case ELureNoBiteReason::TooShallow:
		return TEXT("Too shallow here: cast into deeper water.");
	case ELureNoBiteReason::WrongBait:
		return FString::Printf(TEXT("Nothing here takes your bait (%s) right now. Try other water or bait."), *LureWaterPrivate::ShortTagName(Bait));
	case ELureNoBiteReason::NoSpecies:
		return TEXT("No fish live in this water right now.");
	default:
		return FString();
	}
}

bool FLureWaterRules::ShowsAtOnce(ELureNoBiteReason Reason)
{
	return Reason == ELureNoBiteReason::TooShallow;
}

FString FLureWaterRules::WaterLine(const FString& AreaName)
{
	return FString::Printf(TEXT("Water: %s"), AreaName.IsEmpty() ? TEXT("open water") : *AreaName);
}

// ---- Hot spots ----

FVector2D FLureWaterRules::DriftOffset(float DriftRange, float DriftSpeed, int32 Seed, double Age)
{
	if (!(FMath::IsFinite(DriftRange) && DriftRange > 0.f && FMath::IsFinite(DriftSpeed) && DriftSpeed > 0.f && FMath::IsFinite(Age)))
	{
		return FVector2D::ZeroVector;
	}
	FRandomStream Stream(Seed);
	const double Ratio = 0.62 + 0.3 * Stream.FRand(); // the second wave's frequency: never a simple ratio, so the path doesn't repeat soon
	const double Turn = Stream.FRand() * UE_DOUBLE_TWO_PI; // which way it sets off
	// Amplitude per axis A = Range / sqrt(2): |offset| <= Range. RMS speed of x = A sin(w t), y = A sin(r w t) is
	// A w sqrt((1 + r^2) / 2); solve for w so it equals DriftSpeed. Both waves start at 0: it appears at its anchor.
	const double Amplitude = static_cast<double>(DriftRange) / UE_DOUBLE_SQRT_2;
	const double Omega = static_cast<double>(DriftSpeed) / (Amplitude * FMath::Sqrt((1.0 + Ratio * Ratio) * 0.5));
	const double X = Amplitude * FMath::Sin(Omega * Age);
	const double Y = Amplitude * FMath::Sin(Ratio * Omega * Age);
	const double Cos = FMath::Cos(Turn);
	const double Sin = FMath::Sin(Turn);
	return FVector2D(X * Cos - Y * Sin, X * Sin + Y * Cos);
}

float FLureWaterRules::SpawnChance(const FLureHotSpotRow& Row, float Seconds)
{
	if (!(FMath::IsFinite(Row.SpawnInterval) && Row.SpawnInterval > 0.f && FMath::IsFinite(Seconds) && Seconds > 0.f))
	{
		return 0.f;
	}
	return 1.f - FMath::Exp(-Seconds / Row.SpawnInterval);
}

float FLureWaterRules::LifetimeFromRoll(const FLureHotSpotRow& Row, float U)
{
	const float Min = FMath::Max(1.f, LureWaterPrivate::FiniteOr(Row.LifetimeMin, 1.f));
	const float Max = FMath::Max(Min, LureWaterPrivate::FiniteOr(Row.LifetimeMax, Min));
	return Min + (Max - Min) * FMath::Clamp(LureWaterPrivate::FiniteOr(U, 0.f), 0.f, 1.f);
}

FLureHotSpotBonus FLureWaterRules::BonusFromRow(FName TypeId, const FLureHotSpotRow& Row)
{
	using namespace LureWaterPrivate;
	FLureHotSpotBonus Bonus;
	Bonus.TypeId = TypeId;
	Bonus.LuckBonus = FMath::Max(0.f, FiniteOr(Row.LuckBonus, 0.f));
	Bonus.SizeBonus = FMath::Clamp(FiniteOr(Row.SizeBonus, 0.f), 0.f, 1.f);
	Bonus.ValueMultiplier = FiniteOr(Row.ValueMultiplier, 1.f) > 0.f ? Row.ValueMultiplier : 1.f;
	Bonus.BiteWaitScale = FiniteOr(Row.BiteWaitScale, 1.f) > 0.f ? Row.BiteWaitScale : 1.f;
	return Bonus;
}

FName FLureWaterRules::FallbackHotSpotType()
{
	return TEXT("Bubbles");
}

// ---- Data rule ----

TArray<FString> FLureWaterRules::FindGapHours(const FFishTables& Tables, const FGameplayTag& Habitat, const FGameplayTag& Region,
	TConstArrayView<FGameplayTag> Fallbacks, float StepHours)
{
	using namespace LureWaterPrivate;
	TArray<FString> Ranges;
	const float Step = (FMath::IsFinite(StepHours) && StepHours >= 1.f / 60.f) ? StepHours : 0.25f;
	const int32 Samples = FMath::Max(1, FMath::CeilToInt(24.f / Step));
	int32 RangeStart = INDEX_NONE;
	for (int32 Sample = 0; Sample <= Samples; ++Sample)
	{
		bool bGap = false;
		if (Sample < Samples)
		{
			const float Hours = Sample * Step;
			bGap = !HasSpeciesIgnoringBait(Tables, Habitat, Region, Hours, FGameplayTag());
			if (bGap)
			{
				for (const FGameplayTag& Fallback : Fallbacks)
				{
					if (Fallback.IsValid() && HasSpeciesIgnoringBait(Tables, Fallback, Region, Hours, FGameplayTag()))
					{
						bGap = false;
						break;
					}
				}
			}
		}
		if (bGap && RangeStart == INDEX_NONE)
		{
			RangeStart = Sample;
		}
		else if (!bGap && RangeStart != INDEX_NONE)
		{
			Ranges.Add(HourText(RangeStart * Step) + TEXT("-") + HourText(FMath::Min(24.f, Sample * Step)));
			RangeStart = INDEX_NONE;
		}
	}
	return Ranges;
}

// ---- World lookups ----

double FLureWaterQuery::GetTime(const UWorld* World)
{
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

TArray<FLureWaterAreaInfo> FLureWaterQuery::GatherAreas(const UWorld* World, bool* bOutLegacy)
{
	TArray<FLureWaterAreaInfo> Areas;
	if (bOutLegacy)
	{
		*bOutLegacy = false;
	}
	if (!World)
	{
		return Areas;
	}
	for (TActorIterator<ALureWaterArea> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			Areas.Add(It->GetWaterArea());
		}
	}
	if (Areas.Num() == 0 && GetDefault<ULureWaterSettings>()->bLegacySpotsWhenNoAreas)
	{
		const int32 Priority = GetDefault<ULureWaterSettings>()->LegacySpotPriority;
		for (const FLureFishingSpot& Spot : FLureFishingSpots::GatherSpots(World, GetDefault<ULureFishingSettings>()->FishingSpotTag))
		{
			Areas.Add(FLureWaterRules::AreaFromLegacySpot(Spot, Priority));
		}
		if (bOutLegacy)
		{
			*bOutLegacy = Areas.Num() > 0;
		}
	}
	// A stable order (actor iteration order is not): by id, then priority, then center.
	Areas.StableSort([](const FLureWaterAreaInfo& A, const FLureWaterAreaInfo& B)
	{
		if (A.AreaId != B.AreaId)
		{
			return A.AreaId.LexicalLess(B.AreaId);
		}
		if (A.Priority != B.Priority)
		{
			return A.Priority > B.Priority;
		}
		return A.Center.X != B.Center.X ? A.Center.X < B.Center.X : A.Center.Y < B.Center.Y;
	});
	return Areas;
}

float FLureWaterQuery::MeasureDepth(const UWorld* World, const FVector2D& XY, float WaterZ, float Probe)
{
	const float MaxDepth = (FMath::IsFinite(Probe) && Probe > 0.f) ? Probe : 5000.f;
	if (!World || !FMath::IsFinite(WaterZ))
	{
		return MaxDepth;
	}
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureWaterDepth), false);
	FHitResult Hit;
	if (FLureFishingSpots::TraceCast(World, Hit, FVector(XY.X, XY.Y, WaterZ + 1.0), FVector(XY.X, XY.Y, WaterZ - MaxDepth), Params))
	{
		return FMath::Clamp(WaterZ - static_cast<float>(Hit.ImpactPoint.Z), 0.f, MaxDepth);
	}
	return MaxDepth;
}

bool FLureWaterQuery::ProbeWater(const UWorld* World, const FVector2D& XY, float& OutWaterZ, float& OutDepth)
{
	OutWaterZ = 0.f;
	OutDepth = 0.f;
	if (!World)
	{
		return false;
	}
	const ULureFishingSettings& Fishing = *GetDefault<ULureFishingSettings>();
	float WaterZ = 0.f;
	if (!FLureFishingSpots::FindWaterSurfaceZ(World, XY, Fishing, WaterZ))
	{
		return false;
	}
	const float Probe = FMath::Max(1.f, GetDefault<ULureWaterSettings>()->DepthProbe);
	// From well above the water: a dock or a jetty over the water is land here too.
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureWaterProbe), false);
	FHitResult Hit;
	float Depth = Probe;
	if (FLureFishingSpots::TraceCast(World, Hit, FVector(XY.X, XY.Y, WaterZ + 1000.0), FVector(XY.X, XY.Y, WaterZ - Probe), Params))
	{
		const float GroundZ = static_cast<float>(Hit.ImpactPoint.Z);
		if (GroundZ > WaterZ + Fishing.LandTolerance)
		{
			return false; // land
		}
		Depth = FMath::Clamp(WaterZ - GroundZ, 0.f, Probe);
	}
	OutWaterZ = WaterZ;
	OutDepth = Depth;
	return true;
}

FLureWaterContext FLureWaterQuery::DescribeWater(const UWorld* World, const FVector& Rest, bool bOnWater, float WaterZ)
{
	if (!bOnWater)
	{
		return FLureWaterContext();
	}
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	const FVector2D XY(Rest.X, Rest.Y);
	const float Depth = MeasureDepth(World, XY, WaterZ, Settings->DepthProbe);
	const TArray<FLureWaterAreaInfo> Areas = GatherAreas(World);
	return FLureWaterRules::MakeWaterContext(Areas, XY, WaterZ, Depth, Settings->GetDefaultWaterHabitatTag());
}

ALureHotSpot* FLureWaterQuery::FindHotSpotAt(const UWorld* World, const FVector2D& XY, double Time)
{
	if (!World)
	{
		return nullptr;
	}
	ALureHotSpot* Best = nullptr;
	double BestRatio = TNumericLimits<double>::Max();
	for (TActorIterator<ALureHotSpot> It(const_cast<UWorld*>(World)); It; ++It)
	{
		ALureHotSpot* HotSpot = *It;
		if (!IsValid(HotSpot) || !HotSpot->ContainsAt(XY, Time))
		{
			continue;
		}
		const FVector Center = HotSpot->GetCenterAt(Time);
		const double Ratio = FVector2D::Distance(XY, FVector2D(Center.X, Center.Y)) / FMath::Max(1.0, static_cast<double>(HotSpot->GetState().Radius));
		if (Ratio < BestRatio)
		{
			BestRatio = Ratio;
			Best = HotSpot;
		}
	}
	return Best;
}

FLureHotSpotBonus FLureWaterQuery::FindHotSpotBonusAt(const UWorld* World, const FVector2D& XY, double Time)
{
	const ALureHotSpot* HotSpot = FindHotSpotAt(World, XY, Time);
	return HotSpot ? HotSpot->GetBonus() : FLureHotSpotBonus();
}

FString FLureWaterQuery::GetAreaDisplayName(const UWorld* World, FName AreaId)
{
	if (AreaId.IsNone())
	{
		return FString();
	}
	if (World)
	{
		bool bAnyArea = false;
		for (TActorIterator<ALureWaterArea> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (IsValid(*It))
			{
				bAnyArea = true;
				const FLureWaterAreaInfo Area = It->GetWaterArea();
				if (Area.AreaId == AreaId)
				{
					return Area.GetLabel();
				}
			}
		}
		// A level still on legacy fishing spots: the marker's Name=.
		if (!bAnyArea && GetDefault<ULureWaterSettings>()->bLegacySpotsWhenNoAreas)
		{
			for (const FLureFishingSpot& Spot : FLureFishingSpots::GatherSpots(World, GetDefault<ULureFishingSettings>()->FishingSpotTag))
			{
				if (Spot.SpotId == AreaId && !Spot.DisplayName.IsEmpty())
				{
					return Spot.DisplayName;
				}
			}
		}
	}
	return AreaId.ToString();
}

const UDataTable* FLureWaterQuery::LoadHotSpotTable()
{
	using namespace LureWaterPrivate;
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	if (Settings->HotSpotTable.IsNull())
	{
		return nullptr;
	}
	if (const UDataTable* Loaded = Settings->HotSpotTable.Get())
	{
		return IsHotSpotTable(Loaded) ? Loaded : nullptr;
	}
	// A missing package is looked for again at most every 10 s (the HUD may ask every frame; the asset may be imported
	// meanwhile). An existing one is (re)loaded at once; the spawner keeps it referenced.
	static double LastMissing = -1000.0;
	static bool bWarned = false;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMissing < 10.0)
	{
		return nullptr;
	}
	const FString Package = Settings->HotSpotTable.ToSoftObjectPath().GetLongPackageName();
	if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
	{
		LastMissing = Now;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogLureWater, Warning, TEXT("Hot spots: '%s' is not imported (source data/tables/DT_HotSpot.json); using the built-in '%s' row."),
				*Settings->HotSpotTable.ToString(), *FLureWaterRules::FallbackHotSpotType().ToString());
		}
		return nullptr;
	}
	const UDataTable* Table = Settings->HotSpotTable.LoadSynchronous();
	return IsHotSpotTable(Table) ? Table : nullptr;
}

bool FLureWaterQuery::FindHotSpotRow(const UDataTable* Table, FName TypeId, FLureHotSpotRow& OutRow)
{
	if (LureWaterPrivate::IsHotSpotTable(Table))
	{
		if (const uint8* Row = Table->FindRowUnchecked(TypeId))
		{
			OutRow = *reinterpret_cast<const FLureHotSpotRow*>(Row);
			return true;
		}
		return false;
	}
	if (!Table && TypeId == FLureWaterRules::FallbackHotSpotType())
	{
		OutRow = FLureHotSpotRow();
		return true;
	}
	return false;
}

TArray<TPair<FName, FLureHotSpotRow>> FLureWaterQuery::GetHotSpotRows(const UDataTable* Table)
{
	TArray<TPair<FName, FLureHotSpotRow>> Rows;
	if (LureWaterPrivate::IsHotSpotTable(Table))
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			Rows.Emplace(Pair.Key, *reinterpret_cast<const FLureHotSpotRow*>(Pair.Value));
		}
		Rows.Sort([](const TPair<FName, FLureHotSpotRow>& A, const TPair<FName, FLureHotSpotRow>& B) { return A.Key.LexicalLess(B.Key); });
	}
	else if (!Table)
	{
		Rows.Emplace(FLureWaterRules::FallbackHotSpotType(), FLureHotSpotRow());
	}
	return Rows;
}

FString FLureWaterQuery::HotSpotHudText(FName TypeId)
{
	if (TypeId.IsNone())
	{
		return FString();
	}
	FLureHotSpotRow Row;
	if (FindHotSpotRow(LoadHotSpotTable(), TypeId, Row) && !Row.HudText.IsEmpty())
	{
		return Row.HudText.ToString();
	}
	return TEXT("Hot spot: better fish here");
}

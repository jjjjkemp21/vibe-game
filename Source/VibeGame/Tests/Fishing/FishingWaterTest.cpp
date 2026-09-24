// Lure T-027 (unreal-engineer): fish anywhere - water areas, depth, the bite decision, the fish-core roll inputs and the data
// rules. Project.Fishing.Water.{Area,Bite,Roll,Data,World}.* - spec: docs/specs/fishing-water-rules.md.

#include "Tests/Fishing/FishingWaterTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "Fish/FishDataValidator.h"
#include "Fishing/LureHotSpotVisualComponent.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LureWaterTest
{

namespace WaterLocal
{
	/** Captures one log category's lines while in scope. */
	struct FLogLines : public FOutputDevice
	{
		FName Category;
		TArray<FString> Lines;
		FCriticalSection Lock;
		explicit FLogLines(FName InCategory) : Category(InCategory) { GLog->AddOutputDevice(this); }
		virtual ~FLogLines() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& InCategory) override
		{
			if (InCategory == Category)
			{
				FScopeLock Scope(&Lock);
				Lines.Add(Message);
			}
		}
		int32 Count(const TCHAR* Contains)
		{
			GLog->Flush(); // lines logged on this thread may still be queued for the output devices
			FScopeLock Scope(&Lock);
			return Lines.FilterByPredicate([Contains](const FString& Line) { return Line.Contains(Contains); }).Num();
		}
	};

	FLureFishingEnvironment Environment(float Hours, const TCHAR* Bait = TEXT("Bait.Shrimp"))
	{
		FLureFishingEnvironment Env;
		Env.TimeOfDayHours = Hours;
		Env.BaitTag = Tag(Bait);
		Env.DefaultRegionTag = Tag(TEXT("Region.Tropical"));
		return Env;
	}

	FLureWaterContext Water(const TCHAR* Habitat, float Depth = 500.f, const TCHAR* Region = nullptr, float Luck = 0.f)
	{
		FLureWaterContext Context;
		Context.bOnWater = true;
		Context.Source = ELureWaterSource::Area;
		Context.AreaId = TEXT("qa_water");
		Context.HabitatTag = Tag(Habitat);
		Context.RegionTag = Region ? Tag(Region) : FGameplayTag();
		Context.Luck = Luck;
		Context.DepthCm = Depth;
		return Context;
	}

	FLureBiteRules Rules(std::initializer_list<const TCHAR*> Fallbacks, float MinDepth = 15.f)
	{
		FLureBiteRules Out;
		Out.MinBiteDepth = MinDepth;
		for (const TCHAR* Name : Fallbacks)
		{
			Out.GapFallbackHabitats.Add(Tag(Name));
		}
		return Out;
	}

	FString ReasonName(ELureNoBiteReason Reason)
	{
		return StaticEnum<ELureNoBiteReason>()->GetNameStringByValue(static_cast<int64>(Reason));
	}

	/** A layout "water_area" marker as the builder configures ALureWaterArea (the spec's schema), for the data tests. */
	bool ParseLayoutArea(const TSharedPtr<FJsonObject>& Marker, FLureWaterAreaInfo& Out, TArray<FString>& Problems)
	{
		Out = FLureWaterAreaInfo();
		FString Id, Habitat, Region, Shape;
		if (!Marker->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
		{
			Problems.Add(TEXT("a water_area without an id"));
			return false;
		}
		Out.AreaId = FName(*Id);
		Marker->TryGetStringField(TEXT("name"), Out.DisplayName);
		auto Fail = [&Problems, &Id](const FString& Text) { Problems.Add(FString::Printf(TEXT("water area %s: %s"), *Id, *Text)); };
		const FGameplayTag HabitatRoot = Tag(TEXT("Habitat"));
		const FGameplayTag RegionRoot = Tag(TEXT("Region"));
		if (!Marker->TryGetStringField(TEXT("habitat"), Habitat) || !Tag(*Habitat).IsValid() || !Tag(*Habitat).MatchesTag(HabitatRoot))
		{
			Fail(FString::Printf(TEXT("habitat '%s' is not a registered Habitat.* tag"), *Habitat));
		}
		Out.HabitatTag = Tag(*Habitat);
		if (Marker->TryGetStringField(TEXT("region"), Region) && !Region.IsEmpty())
		{
			if (!Tag(*Region).IsValid() || !Tag(*Region).MatchesTag(RegionRoot))
			{
				Fail(FString::Printf(TEXT("region '%s' is not a registered Region.* tag"), *Region));
			}
			Out.RegionTag = Tag(*Region);
		}
		double Number = 0.0;
		if (Marker->TryGetNumberField(TEXT("priority"), Number))
		{
			Out.Priority = static_cast<int32>(Number);
		}
		if (Marker->TryGetNumberField(TEXT("luck"), Number))
		{
			Out.Luck = static_cast<float>(Number);
			if (Number < 0.0)
			{
				Fail(TEXT("luck must be >= 0"));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Marker->TryGetArrayField(TEXT("depth"), Array))
		{
			if (Array->Num() != 2)
			{
				Fail(TEXT("depth must be [min, max]"));
			}
			else
			{
				Out.MinDepth = static_cast<float>((*Array)[0]->AsNumber());
				Out.MaxDepth = static_cast<float>((*Array)[1]->AsNumber());
				if (Out.MinDepth < 0.f || (Out.MaxDepth != 0.f && Out.MaxDepth <= Out.MinDepth))
				{
					Fail(TEXT("depth needs min >= 0 and max 0 or > min"));
				}
			}
		}
		FVector2D At = FVector2D::ZeroVector;
		const bool bAt = Marker->TryGetArrayField(TEXT("at"), Array) && Array->Num() >= 2;
		if (bAt)
		{
			At = FVector2D((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber());
		}
		Out.Center = At;
		Marker->TryGetStringField(TEXT("shape"), Shape);
		if (Shape == TEXT("circle"))
		{
			Out.Shape = ELureWaterAreaShape::Circle;
			Marker->TryGetNumberField(TEXT("radius"), Number);
			Out.Radius = static_cast<float>(Number);
			if (!bAt)
			{
				Fail(TEXT("a circle needs \"at\""));
			}
		}
		else if (Shape == TEXT("box"))
		{
			Out.Shape = ELureWaterAreaShape::Box;
			if (Marker->TryGetArrayField(TEXT("size"), Array) && Array->Num() == 2)
			{
				Out.HalfSize = FVector2D((*Array)[0]->AsNumber() * 0.5, (*Array)[1]->AsNumber() * 0.5);
			}
			if (Marker->TryGetNumberField(TEXT("yaw"), Number))
			{
				Out.YawDegrees = static_cast<float>(Number);
			}
			if (!bAt)
			{
				Fail(TEXT("a box needs \"at\""));
			}
		}
		else if (Shape == TEXT("polygon"))
		{
			Out.Shape = ELureWaterAreaShape::Polygon;
			if (Marker->TryGetArrayField(TEXT("points"), Array))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Array)
				{
					const TArray<TSharedPtr<FJsonValue>>& Point = Value->AsArray();
					if (Point.Num() >= 2)
					{
						Out.Polygon.Add(FVector2D(Point[0]->AsNumber(), Point[1]->AsNumber()));
					}
				}
			}
		}
		else if (Shape == TEXT("everywhere"))
		{
			Out.Shape = ELureWaterAreaShape::Everywhere;
		}
		else
		{
			Fail(FString::Printf(TEXT("unknown shape '%s'"), *Shape));
			return false;
		}
		if (!Out.HasShape())
		{
			Fail(TEXT("no usable outline (radius/size > 0, 3+ points with an area)"));
		}
		return true;
	}

	/** Every "markers" entry of every data/levels/*.json of Type, with its file name. */
	TArray<TPair<FString, TSharedPtr<FJsonObject>>> LayoutMarkers(FAutomationTestBase& Test, const TCHAR* Type, TArray<FString>* OutFiles = nullptr)
	{
		TArray<TPair<FString, TSharedPtr<FJsonObject>>> Out;
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(FPaths::ProjectDir() / TEXT("data/levels/*.json")), true, false);
		Files.Sort();
		if (OutFiles)
		{
			*OutFiles = Files;
		}
		for (const FString& File : Files)
		{
			FString Text;
			TSharedPtr<FJsonObject> Root;
			if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("data/levels") / File))
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
			{
				Test.AddError(TEXT("can't read data/levels/") + File);
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!Root->TryGetArrayField(TEXT("markers"), List))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Marker = Value->AsObject();
				FString MarkerType;
				if (Marker.IsValid() && Marker->TryGetStringField(TEXT("type"), MarkerType) && MarkerType == Type)
				{
					Out.Emplace(File, Marker);
				}
			}
		}
		return Out;
	}
}

// =====================================================================================================================
// Water areas (pure)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterAreaShapesContain, "Project.Fishing.Water.Area.ShapesContain", LureWaterTest::Flags)
bool FLureWaterAreaShapesContain::RunTest(const FString& Parameters)
{
	// Circles and boxes count their edge; boxes turn with their yaw; polygons use the even-odd rule (concave is fine);
	// Everywhere contains all; an area without a usable outline contains nothing.
	const FLureWaterAreaInfo Round = Circle(TEXT("round"), TEXT("Habitat.Reef"), FVector2D(0.0, 0.0), 100.f);
	TestTrue(TEXT("circle: the edge counts"), Round.Contains(FVector2D(100.0, 0.0)));
	TestFalse(TEXT("circle: just outside"), Round.Contains(FVector2D(100.01, 0.0)));
	TestTrue(TEXT("circle: (70, 70) is 99 cm out"), Round.Contains(FVector2D(70.0, 70.0)));
	TestFalse(TEXT("circle: (71, 71) is 100.4 cm out"), Round.Contains(FVector2D(71.0, 71.0)));
	TestNearlyEqual(TEXT("circle size"), Round.GetSize(), UE_DOUBLE_PI * 100.0 * 100.0, 1.0e-6);

	// Half size 200 x 50 turned 90 degrees: 50 along world X, 200 along world Y.
	const FLureWaterAreaInfo Turned = Box(TEXT("turned"), TEXT("Habitat.Reef"), FVector2D(1000.0, 0.0), FVector2D(200.0, 50.0), 90.f);
	TestTrue(TEXT("box (yaw 90): 190 along world Y is inside"), Turned.Contains(FVector2D(1000.0, 190.0)));
	TestFalse(TEXT("box (yaw 90): 60 along world X is outside"), Turned.Contains(FVector2D(1060.0, 0.0)));
	TestTrue(TEXT("box (yaw 90): 40 along world X is inside"), Turned.Contains(FVector2D(1040.0, 0.0)));
	TestTrue(TEXT("box: the edge counts"), Turned.Contains(FVector2D(1050.0 - 1.0e-6, 200.0 - 1.0e-6)));
	TestNearlyEqual(TEXT("box size"), Turned.GetSize(), 4.0 * 200.0 * 50.0, 1.0e-6);
	const FBox2D TurnedBounds = Turned.GetBounds();
	TestTrue(TEXT("box bounds follow the yaw"), TurnedBounds.bIsValid && FMath::IsNearlyEqual(TurnedBounds.Max.X, 1050.0, 0.01) && FMath::IsNearlyEqual(TurnedBounds.Max.Y, 200.0, 0.01));

	// A U shape (concave): the notch between the arms is outside.
	FLureWaterAreaInfo U = MakeArea(TEXT("u"), TEXT("Habitat.Lagoon"), ELureWaterAreaShape::Polygon);
	U.Polygon = { FVector2D(0, 0), FVector2D(300, 0), FVector2D(300, 300), FVector2D(200, 300), FVector2D(200, 100), FVector2D(100, 100), FVector2D(100, 300), FVector2D(0, 300) };
	TestTrue(TEXT("polygon: the base"), U.Contains(FVector2D(150.0, 50.0)));
	TestTrue(TEXT("polygon: the left arm"), U.Contains(FVector2D(50.0, 250.0)));
	TestTrue(TEXT("polygon: the right arm"), U.Contains(FVector2D(250.0, 250.0)));
	TestFalse(TEXT("polygon: the notch (concave) is outside"), U.Contains(FVector2D(150.0, 250.0)));
	TestFalse(TEXT("polygon: outside"), U.Contains(FVector2D(350.0, 50.0)));
	TestNearlyEqual(TEXT("polygon size (shoelace)"), U.GetSize(), 300.0 * 300.0 - 100.0 * 200.0, 1.0e-6);
	FLureWaterAreaInfo Reversed = U;
	Algo::Reverse(Reversed.Polygon);
	TestTrue(TEXT("polygon: either winding"), Reversed.Contains(FVector2D(150.0, 50.0)) && !Reversed.Contains(FVector2D(150.0, 250.0)) && FMath::IsNearlyEqual(Reversed.GetSize(), U.GetSize()));

	const FLureWaterAreaInfo All = Everywhere(TEXT("all"), TEXT("Habitat.Shore"), -100);
	TestTrue(TEXT("everywhere contains far points"), All.Contains(FVector2D(-1.0e6, 3.0e6)));
	TestTrue(TEXT("everywhere is the biggest"), All.GetSize() > U.GetSize() && All.GetSize() > Round.GetSize());
	TestFalse(TEXT("everywhere has no bounds"), All.GetBounds().bIsValid);

	// No usable outline: never contains anything.
	TestFalse(TEXT("a radius-0 circle has no shape"), Circle(TEXT("dot"), TEXT("Habitat.Reef"), FVector2D::ZeroVector, 0.f).Contains(FVector2D::ZeroVector));
	FLureWaterAreaInfo Line = MakeArea(TEXT("line"), TEXT("Habitat.Reef"), ELureWaterAreaShape::Polygon);
	Line.Polygon = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(200, 0) };
	TestFalse(TEXT("a polygon without area has no shape"), Line.HasShape());
	FLureWaterAreaInfo Two = Line;
	Two.Polygon.SetNum(2);
	TestFalse(TEXT("a polygon of 2 points has no shape"), Two.HasShape() || Two.Contains(FVector2D(50.0, 0.0)));
	TestFalse(TEXT("a zero box has no shape"), Box(TEXT("flat"), TEXT("Habitat.Reef"), FVector2D::ZeroVector, FVector2D(10.0, 0.0), 0.f).HasShape());
	TestFalse(TEXT("NaN points are never inside"), Round.Contains(FVector2D(std::numeric_limits<double>::quiet_NaN(), 0.0)));

	// Depth bands: [min, max), max 0 = no limit.
	FLureWaterAreaInfo Band = Everywhere(TEXT("band"), TEXT("Habitat.DeepDrop"), 0, 100.f, 300.f);
	TestFalse(TEXT("depth 99 < min 100"), Band.AcceptsDepth(99.f));
	TestTrue(TEXT("depth 100 = min"), Band.AcceptsDepth(100.f));
	TestTrue(TEXT("depth 299.9"), Band.AcceptsDepth(299.9f));
	TestFalse(TEXT("depth 300 = max (exclusive)"), Band.AcceptsDepth(300.f));
	TestFalse(TEXT("NaN depth"), Band.AcceptsDepth(std::numeric_limits<float>::quiet_NaN()));
	Band.MaxDepth = 0.f;
	TestTrue(TEXT("max 0 = no upper limit"), Band.AcceptsDepth(1.0e6f));
	TestEqual(TEXT("label = the name, else the id"), Circle(TEXT("reef_flats"), TEXT("Habitat.Reef"), FVector2D::ZeroVector, 1.f).GetLabel(), FString(TEXT("reef_flats")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterAreaPriorityWins, "Project.Fishing.Water.Area.PriorityWins", LureWaterTest::Flags)
bool FLureWaterAreaPriorityWins::RunTest(const FString& Parameters)
{
	// Overlapping areas: the higher priority wins; on a tie the smaller area; then the lower id (FName::LexicalLess, natural
	// numbers). Areas with an invalid habitat or no outline never win. Depth bands filter. The order of the list never matters.
	const FVector2D Point(100.0, 0.0);
	TArray<FLureWaterAreaInfo> Areas = {
		Circle(TEXT("lagoon"), TEXT("Habitat.Lagoon"), FVector2D(0.0, 0.0), 2000.f, 0),
		Box(TEXT("reef"), TEXT("Habitat.Reef"), FVector2D(100.0, 0.0), FVector2D(300.0, 300.0), 0.f, 0),
	};
	auto Winner = [](TConstArrayView<FLureWaterAreaInfo> List, const FVector2D& XY, float Depth = 500.f) -> FName
	{
		const int32 Index = FLureWaterRules::FindAreaIndex(List, XY, Depth);
		return Index == INDEX_NONE ? NAME_None : List[Index].AreaId;
	};
	TestEqual(TEXT("same priority: the smaller area wins"), Winner(Areas, Point), FName(TEXT("reef")));
	Areas[0].Priority = 1;
	TestEqual(TEXT("a higher priority beats a smaller area"), Winner(Areas, Point), FName(TEXT("lagoon")));
	TestEqual(TEXT("outside the small area: the big one"), Winner(Areas, FVector2D(1500.0, 0.0)), FName(TEXT("lagoon")));
	TestEqual(TEXT("outside both: default water"), Winner(Areas, FVector2D(5000.0, 0.0)), FName(NAME_None));

	// Full ties: the lower id, with natural numbers (area_2 before area_10).
	const TArray<FLureWaterAreaInfo> Twins = { Circle(TEXT("b_area"), TEXT("Habitat.Reef"), Point, 50.f), Circle(TEXT("a_area"), TEXT("Habitat.Shore"), Point, 50.f) };
	TestEqual(TEXT("identical areas: the lower id"), Winner(Twins, Point), FName(TEXT("a_area")));
	const TArray<FLureWaterAreaInfo> Numbered = { Circle(TEXT("area_10"), TEXT("Habitat.Reef"), Point, 50.f), Circle(TEXT("area_2"), TEXT("Habitat.Shore"), Point, 50.f) };
	TestEqual(TEXT("natural number order: area_2 before area_10"), Winner(Numbered, Point), FName(TEXT("area_2")));

	// Broken areas never win, even with a huge priority (no dead zones from bad data).
	TArray<FLureWaterAreaInfo> WithBroken = Areas;
	FLureWaterAreaInfo NoHabitat = Circle(TEXT("broken_tag"), TEXT("Habitat.QA_NotRegistered"), Point, 500.f, 1000);
	FLureWaterAreaInfo NoShape = Circle(TEXT("broken_shape"), TEXT("Habitat.Reef"), Point, 0.f, 1000);
	WithBroken.Add(NoHabitat);
	WithBroken.Add(NoShape);
	TestFalse(TEXT("QA precondition: the unregistered habitat is invalid"), NoHabitat.HabitatTag.IsValid());
	TestEqual(TEXT("an area without a valid habitat or outline never wins"), Winner(WithBroken, Point), FName(TEXT("lagoon")));

	// Depth bands: shallows and deep water as two "everywhere" areas.
	const TArray<FLureWaterAreaInfo> Sea = { Everywhere(TEXT("shallows"), TEXT("Habitat.Shore"), -100, 0.f, 300.f), Everywhere(TEXT("deep"), TEXT("Habitat.DeepDrop"), -100, 300.f, 0.f) };
	TestEqual(TEXT("50 cm deep: the shallows"), Winner(Sea, Point, 50.f), FName(TEXT("shallows")));
	TestEqual(TEXT("300 cm deep: the deep"), Winner(Sea, Point, 300.f), FName(TEXT("deep")));
	TestEqual(TEXT("5 km deep: the deep"), Winner(Sea, Point, 500000.f), FName(TEXT("deep")));

	// Any order of the list gives the same winner.
	TArray<FLureWaterAreaInfo> All = WithBroken;
	All.Append(Sea);
	FRandomStream Shuffle(7);
	for (int32 Round = 0; Round < 20; ++Round)
	{
		for (int32 Index = All.Num() - 1; Index > 0; --Index)
		{
			All.Swap(Index, Shuffle.RandHelper(Index + 1));
		}
		if (Winner(All, Point) != FName(TEXT("lagoon")) || Winner(All, FVector2D(9000.0, 9000.0), 50.f) != FName(TEXT("shallows")))
		{
			AddError(FString::Printf(TEXT("the winner depends on the list order (round %d)"), Round));
			break;
		}
	}

	// The water context: the winner's id, name, habitat, region and luck, or the default water.
	FLureWaterAreaInfo Lucky = Circle(TEXT("cove"), TEXT("Habitat.Shore.Cove"), FVector2D(0.0, 0.0), 500.f, 5);
	Lucky.DisplayName = TEXT("Hidden Cove");
	Lucky.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
	Lucky.Luck = 0.5f;
	FLureWaterAreaInfo Unlucky = Circle(TEXT("minus"), TEXT("Habitat.Reef"), FVector2D(3000.0, 0.0), 500.f, 5);
	Unlucky.Luck = -2.f;
	const TArray<FLureWaterAreaInfo> Two = { Lucky, Unlucky };
	const FLureWaterContext In = FLureWaterRules::MakeWaterContext(Two, FVector2D(10.0, 0.0), -60.f, 123.f, Tag(TEXT("Habitat.Shore")));
	TestTrue(TEXT("context: on water from an area"), In.bOnWater && In.Source == ELureWaterSource::Area);
	TestEqual(TEXT("context: area id"), In.AreaId, FName(TEXT("cove")));
	TestEqual(TEXT("context: area name"), In.AreaName, FString(TEXT("Hidden Cove")));
	TestTrue(TEXT("context: habitat and region"), In.HabitatTag == Tag(TEXT("Habitat.Shore.Cove")) && In.RegionTag == Tag(TEXT("Region.Tropical.PalmKey")));
	TestNearlyEqual(TEXT("context: luck, depth, surface"), In.Luck + In.DepthCm + In.WaterZ, 0.5f + 123.f - 60.f, 1.0e-4f);
	TestNearlyEqual(TEXT("negative area luck counts as 0"), FLureWaterRules::MakeWaterContext(Two, FVector2D(3000.0, 0.0), 0.f, 100.f, FGameplayTag()).Luck, 0.f, 0.f);
	const FLureWaterContext Open = FLureWaterRules::MakeWaterContext(Two, FVector2D(9000.0, 0.0), 0.f, 100.f, Tag(TEXT("Habitat.Shore")));
	TestTrue(TEXT("default water: the default habitat, no id, no region, no luck"), Open.Source == ELureWaterSource::Default && Open.AreaId.IsNone()
		&& Open.HabitatTag == Tag(TEXT("Habitat.Shore")) && !Open.RegionTag.IsValid() && Open.Luck == 0.f);
	return true;
}

// =====================================================================================================================
// Legacy spots and the area actor
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterAreaLegacySpots, "Project.Fishing.Water.Area.LegacySpotsOnlyWithoutAreas", LureWaterTest::Flags)
bool FLureWaterAreaLegacySpots::RunTest(const FString& Parameters)
{
	// Migration: a level with no water area reads its fishing_spot markers as circle areas; one water area turns them off.
	FLureFishingSpot Spot;
	Spot.SpotId = TEXT("reef_flats");
	Spot.DisplayName = TEXT("Reef Flats");
	Spot.HabitatTag = Tag(TEXT("Habitat.Reef"));
	Spot.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
	Spot.Radius = 700.f;
	Spot.Luck = 0.25f;
	Spot.Location = FVector(-1500.f, 5400.f, 0.f);
	const FLureWaterAreaInfo Area = FLureWaterRules::AreaFromLegacySpot(Spot, 3);
	TestTrue(TEXT("legacy area: a circle at the marker with its radius"), Area.Shape == ELureWaterAreaShape::Circle && Area.Center.Equals(FVector2D(-1500.0, 5400.0))
		&& Area.Radius == 700.f);
	TestTrue(TEXT("legacy area: id, name, habitat, region, luck, priority, source"), Area.AreaId == TEXT("reef_flats") && Area.DisplayName == TEXT("Reef Flats")
		&& Area.HabitatTag == Spot.HabitatTag && Area.RegionTag == Spot.RegionTag && Area.Luck == 0.25f && Area.Priority == 3 && Area.Source == ELureWaterSource::LegacySpot);

	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(HalfCastX, 0.f, 0.f), { TEXT("Spot=zeta"), TEXT("Habitat=Habitat.Reef"), TEXT("Radius=800") });
	World.AddSpot(FVector(-3000.f, 0.f, 0.f), { TEXT("Spot=alpha"), TEXT("Habitat=Habitat.Lagoon"), TEXT("Radius=500") });
	bool bLegacy = false;
	TArray<FLureWaterAreaInfo> Gathered = FLureWaterQuery::GatherAreas(World.World, &bLegacy);
	TestTrue(TEXT("no water areas: the two spots, as legacy areas"), bLegacy && Gathered.Num() == 2);
	TestTrue(TEXT("... sorted by id"), Gathered.Num() == 2 && Gathered[0].AreaId == TEXT("alpha") && Gathered[1].AreaId == TEXT("zeta"));
	{
		const TScopedSetting<ULureWaterSettings, bool> Off(&ULureWaterSettings::bLegacySpotsWhenNoAreas, false);
		TestEqual(TEXT("the switch off: no legacy areas"), FLureWaterQuery::GatherAreas(World.World).Num(), 0);
	}
	World.AddCircleArea(TEXT("far_area"), TEXT("Habitat.Shore"), FVector2D(-20000.0, 0.0), 500.f);
	Gathered = FLureWaterQuery::GatherAreas(World.World, &bLegacy);
	TestTrue(TEXT("one water area: the spot markers are ignored"), !bLegacy && Gathered.Num() == 1 && Gathered[0].AreaId == TEXT("far_area"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterAreaActorSetters, "Project.Fishing.Water.Area.ActorSetters", LureWaterTest::Flags)
bool FLureWaterAreaActorSetters::RunTest(const FString& Parameters)
{
	// The functions the level builder calls: tags from names, and outlines in world space (a polygon from world points on a
	// turned actor comes back as the same world points).
	FWaterWorld World;
	if (!World.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	AddExpectedMessagePlain(TEXT("is not a registered gameplay tag"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALureWaterArea* Area = World.World->SpawnActor<ALureWaterArea>(ALureWaterArea::StaticClass(), FTransform(FRotator(0.f, 30.f, 0.f), FVector(1000.f, 500.f, 0.f)), Params);
	if (!TestNotNull(TEXT("the area actor spawns"), Area))
	{
		return false;
	}
	TestTrue(TEXT("SetAreaTags with registered names"), Area->SetAreaTags(TEXT("Habitat.Reef.Edge"), TEXT("Region.Tropical.PalmKey")));
	TestTrue(TEXT("... sets both tags"), Area->HabitatTag == Tag(TEXT("Habitat.Reef.Edge")) && Area->RegionTag == Tag(TEXT("Region.Tropical.PalmKey")));
	TestTrue(TEXT("Region None = no region"), Area->SetAreaTags(TEXT("Habitat.Reef"), NAME_None) && !Area->RegionTag.IsValid());
	TestFalse(TEXT("an unregistered habitat is refused"), Area->SetAreaTags(TEXT("Habitat.QA_Nope"), NAME_None));
	TestFalse(TEXT("... and leaves no habitat (the area never wins)"), Area->HabitatTag.IsValid());
	TestFalse(TEXT("an unregistered region is refused"), Area->SetAreaTags(TEXT("Habitat.Reef"), TEXT("Region.QA_Nowhere")));
	Area->SetAreaTags(TEXT("Habitat.Reef"), NAME_None);

	const TArray<FVector2D> WorldPoints = { FVector2D(800.0, 300.0), FVector2D(1400.0, 350.0), FVector2D(1300.0, 900.0), FVector2D(900.0, 800.0) };
	TestEqual(TEXT("SetShapePolygon stores every point"), Area->SetShapePolygon(WorldPoints), WorldPoints.Num());
	TestFalse(TEXT("... in the actor's frame (not the world points)"), Area->PolygonPoints[0].Equals(WorldPoints[0], 1.0));
	const FLureWaterAreaInfo Poly = Area->GetWaterArea();
	bool bSame = Poly.Polygon.Num() == WorldPoints.Num();
	for (int32 Index = 0; bSame && Index < WorldPoints.Num(); ++Index)
	{
		bSame = Poly.Polygon[Index].Equals(WorldPoints[Index], 0.01);
	}
	TestTrue(TEXT("GetWaterArea gives the world points back"), bSame);
	TestTrue(TEXT("... and contains their middle"), Poly.Contains(FVector2D(1100.0, 600.0)));
	TestEqual(TEXT("outline points = the polygon"), Area->GetOutlinePoints(0.f).Num(), WorldPoints.Num());

	Area->SetShapeBox(FVector2D(400.0, 100.0));
	const FLureWaterAreaInfo Turned = Area->GetWaterArea();
	TestTrue(TEXT("box: the actor's yaw turns it"), Turned.Shape == ELureWaterAreaShape::Box && FMath::IsNearlyEqual(Turned.YawDegrees, 30.f, 0.01f));
	const FVector2D AlongX = FVector2D(1000.0, 500.0) + FVector2D(FMath::Cos(FMath::DegreesToRadians(30.0)), FMath::Sin(FMath::DegreesToRadians(30.0))) * 390.0;
	TestTrue(TEXT("box: 390 cm along its own X is inside"), Turned.Contains(AlongX));
	TestFalse(TEXT("box: 390 cm along world X is not (it is turned)"), Turned.Contains(FVector2D(1390.0, 500.0 + 150.0)));

	Area->SetShapeCircle(250.f);
	TestTrue(TEXT("circle"), Area->GetWaterArea().Contains(FVector2D(1000.0, 740.0)) && !Area->GetWaterArea().Contains(FVector2D(1000.0, 760.0)));
	TestEqual(TEXT("circle outline: 64 points"), Area->GetOutlinePoints(0.f).Num(), 64);
	Area->SetShapeEverywhere();
	TestTrue(TEXT("everywhere"), Area->GetWaterArea().Contains(FVector2D(-50000.0, 12345.0)) && Area->GetOutlinePoints(0.f).Num() == 0);
	TestEqual(TEXT("no AreaId: the actor's name"), Area->GetWaterArea().AreaId, Area->GetFName());
	Area->AreaId = TEXT("qa_area");
	TestEqual(TEXT("AreaId set: the id"), Area->GetWaterArea().AreaId, FName(TEXT("qa_area")));
	TestFalse(TEXT("the area is level data: not replicated"), Area->GetIsReplicated());
	return true;
}

// =====================================================================================================================
// The bite decision (pure, real starter data)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterBiteReasons, "Project.Fishing.Water.Bite.DecideReasons", LureWaterTest::Flags)
bool FLureWaterBiteReasons::RunTest(const FString& Parameters)
{
	// NotWater, TooShallow, NoSpecies (a data gap without a fallback), WrongBait (never fixed by the fallback), or a bite.
	using namespace WaterLocal;
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FFishTables Tables = Fish.Get();
	const FLureHotSpotBonus None;
	struct FCase
	{
		const TCHAR* What;
		FLureWaterContext Water;
		FLureFishingEnvironment Env;
		FLureBiteRules Rules;
		ELureNoBiteReason Expect;
		const TCHAR* BiteHabitat;
		bool bFallback;
	};
	FLureWaterContext Land;
	const FCase Cases[] = {
		{ TEXT("on land"), Land, Environment(12.f), Rules({}), ELureNoBiteReason::NotWater, TEXT(""), false },
		{ TEXT("10 cm deep"), Water(TEXT("Habitat.Shore"), 10.f), Environment(12.f), Rules({}), ELureNoBiteReason::TooShallow, TEXT("Habitat.Shore"), false },
		{ TEXT("exactly MinBiteDepth deep"), Water(TEXT("Habitat.Shore"), 15.f), Environment(12.f), Rules({}), ELureNoBiteReason::None, TEXT("Habitat.Shore"), false },
		{ TEXT("shore at noon, shrimp"), Water(TEXT("Habitat.Shore")), Environment(12.f), Rules({}), ELureNoBiteReason::None, TEXT("Habitat.Shore"), false },
		{ TEXT("shore at noon, squid (the bonefish won't take it)"), Water(TEXT("Habitat.Shore")), Environment(12.f, TEXT("Bait.Squid")), Rules({ TEXT("Habitat.Reef") }),
			ELureNoBiteReason::WrongBait, TEXT("Habitat.Shore"), false },
		{ TEXT("reef at noon, no fallback"), Water(TEXT("Habitat.Reef")), Environment(12.f), Rules({}), ELureNoBiteReason::NoSpecies, TEXT("Habitat.Reef"), false },
		{ TEXT("reef at noon, fallback shore"), Water(TEXT("Habitat.Reef")), Environment(12.f), Rules({ TEXT("Habitat.Shore"), TEXT("Habitat.Reef") }),
			ELureNoBiteReason::None, TEXT("Habitat.Shore"), true },
		{ TEXT("reef at noon, fallback shore, squid"), Water(TEXT("Habitat.Reef")), Environment(12.f, TEXT("Bait.Squid")), Rules({ TEXT("Habitat.Shore") }),
			ELureNoBiteReason::WrongBait, TEXT("Habitat.Shore"), true },
		{ TEXT("reef at 20:00"), Water(TEXT("Habitat.Reef")), Environment(20.f), Rules({ TEXT("Habitat.Shore") }), ELureNoBiteReason::None, TEXT("Habitat.Reef"), false },
		{ TEXT("reef edge (a Reef child) at 20:00"), Water(TEXT("Habitat.Reef.Edge")), Environment(20.f), Rules({}), ELureNoBiteReason::None, TEXT("Habitat.Reef.Edge"), false },
		{ TEXT("deep drop at 16:00, fallbacks"), Water(TEXT("Habitat.DeepDrop")), Environment(16.f), Rules({ TEXT("Habitat.Shore"), TEXT("Habitat.Reef") }),
			ELureNoBiteReason::None, TEXT("Habitat.Shore"), true },
		{ TEXT("deep drop at 23:00: the shore is empty, the second fallback"), Water(TEXT("Habitat.DeepDrop")), Environment(23.f), Rules({ TEXT("Habitat.Shore"), TEXT("Habitat.Reef") }),
			ELureNoBiteReason::None, TEXT("Habitat.Reef"), true },
		{ TEXT("deep drop at 12:00, only a reef fallback (empty at noon)"), Water(TEXT("Habitat.DeepDrop")), Environment(12.f), Rules({ TEXT("Habitat.Reef") }),
			ELureNoBiteReason::NoSpecies, TEXT("Habitat.DeepDrop"), false },
		{ TEXT("the wrong region"), Water(TEXT("Habitat.Shore"), 500.f, TEXT("Region.Frozen")), Environment(12.f), Rules({}), ELureNoBiteReason::NoSpecies, TEXT("Habitat.Shore"), false },
	};
	FFishRoll::ResetDataWarnings();
	for (const FCase& Case : Cases)
	{
		const FLureBiteDecision Decision = FLureWaterRules::DecideBite(Tables, Case.Water, None, Case.Env, Case.Rules, 99);
		TestEqual(FString::Printf(TEXT("%s: reason"), Case.What), ReasonName(Decision.Reason), ReasonName(Case.Expect));
		if (Case.Expect != ELureNoBiteReason::NotWater)
		{
			TestTrue(FString::Printf(TEXT("%s: bite habitat %s (got %s)"), Case.What, Case.BiteHabitat, *Decision.BiteHabitat.ToString()), Decision.BiteHabitat == Tag(Case.BiteHabitat));
			TestTrue(FString::Printf(TEXT("%s: the context carries it"), Case.What), Decision.Context.HabitatTag == Decision.BiteHabitat);
		}
		TestEqual(FString::Printf(TEXT("%s: fallback used"), Case.What), Decision.bUsedFallback, Case.bFallback);
		if (Decision.CanBite())
		{
			FName Species;
			TestTrue(FString::Printf(TEXT("%s: PickSpecies agrees"), Case.What), FFishRoll::PickSpecies(Tables, Decision.Context, Species));
		}
	}

	// Missing tables: nothing bites, no crash.
	const FLureBiteDecision NoTables = FLureWaterRules::DecideBite(FFishTables(), Water(TEXT("Habitat.Shore")), None, Environment(12.f), Rules({ TEXT("Habitat.Shore") }), 1);
	TestEqual(TEXT("no fish tables: NoSpecies"), ReasonName(NoTables.Reason), ReasonName(ELureNoBiteReason::NoSpecies));

	// The gap is logged once per habitat and clock hour, not per bite.
	FFishRoll::ResetDataWarnings();
	{
		FLogLines Log(LogLureWater.GetCategoryName());
		for (int32 Bite = 0; Bite < 5; ++Bite)
		{
			FLureWaterRules::DecideBite(Tables, Water(TEXT("Habitat.DeepDrop")), None, Environment(16.2f), Rules({ TEXT("Habitat.Shore") }), Bite);
		}
		TestEqual(TEXT("5 bites in one hour of a data gap: one log line"), Log.Count(TEXT("Data gap: no species of Habitat.DeepDrop")), 1);
		FLureWaterRules::DecideBite(Tables, Water(TEXT("Habitat.DeepDrop")), None, Environment(17.1f), Rules({ TEXT("Habitat.Shore") }), 9);
		TestEqual(TEXT("... and one more the next hour"), Log.Count(TEXT("Data gap: no species of Habitat.DeepDrop")), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterBiteContextSums, "Project.Fishing.Water.Bite.ContextFromWaterAndHotSpot", LureWaterTest::Flags)
bool FLureWaterBiteContextSums::RunTest(const FString& Parameters)
{
	// Luck = area + gear + hot spot (non-finite terms 0); the hot spot's size and value go to the roll; region: the area's, else
	// the default; time, weather, bait and seed from the environment.
	using namespace WaterLocal;
	FLureFishingEnvironment Env = Environment(21.5f, TEXT("Bait.Squid"));
	Env.GearLuck = 0.25f;
	Env.WeatherTag = Tag(TEXT("Weather.Rain"));
	FLureHotSpotBonus Hot;
	Hot.TypeId = TEXT("Bubbles");
	Hot.LuckBonus = 1.5f;
	Hot.SizeBonus = 0.3f;
	Hot.ValueMultiplier = 1.4f;
	const FLureWaterContext Reef = Water(TEXT("Habitat.Reef"), 400.f, TEXT("Region.Tropical.PalmKey"), 0.5f);
	const FFishRollContext Context = FLureWaterRules::MakeBiteContext(Reef, Tag(TEXT("Habitat.Shore")), Hot, Env, -77);
	TestTrue(TEXT("the habitat given (a bite habitat may be a fallback)"), Context.HabitatTag == Tag(TEXT("Habitat.Shore")));
	TestTrue(TEXT("the area's region"), Context.RegionTag == Tag(TEXT("Region.Tropical.PalmKey")));
	TestNearlyEqual(TEXT("luck = area 0.5 + gear 0.25 + hot spot 1.5"), Context.Luck, 2.25f, 1.0e-5f);
	TestNearlyEqual(TEXT("size bonus"), Context.SizeBonus, 0.3f, 0.f);
	TestNearlyEqual(TEXT("value multiplier"), Context.ValueMultiplier, 1.4f, 0.f);
	TestTrue(TEXT("time, weather, bait, seed"), FMath::IsNearlyEqual(Context.TimeOfDayHours, 21.5f) && Context.WeatherTag == Env.WeatherTag && Context.BaitTag == Env.BaitTag && Context.Seed == -77);
	TestTrue(TEXT("no species forced"), Context.SpeciesId.IsNone() && Context.ForcedRarityId.IsNone() && !Context.bForceModifiers && !Context.bForceWeightFraction);

	const FFishRollContext Plain = FLureWaterRules::MakeBiteContext(Water(TEXT("Habitat.Shore")), Tag(TEXT("Habitat.Shore")), FLureHotSpotBonus(), Env, 1);
	TestTrue(TEXT("no region: the default region"), Plain.RegionTag == Tag(TEXT("Region.Tropical")));
	TestTrue(TEXT("no hot spot: no size or value bonus, gear luck only"), Plain.SizeBonus == 0.f && Plain.ValueMultiplier == 1.f && Plain.Luck == 0.25f);
	FLureHotSpotBonus Inactive = Hot;
	Inactive.TypeId = NAME_None;
	const FFishRollContext Ignored = FLureWaterRules::MakeBiteContext(Water(TEXT("Habitat.Shore")), Tag(TEXT("Habitat.Shore")), Inactive, Env, 1);
	TestTrue(TEXT("a bonus without a type is no hot spot"), Ignored.SizeBonus == 0.f && Ignored.ValueMultiplier == 1.f && Ignored.Luck == 0.25f);

	const float NaN = std::numeric_limits<float>::quiet_NaN();
	FLureHotSpotBonus Broken = Hot;
	Broken.LuckBonus = NaN;
	Broken.SizeBonus = 7.f;
	Broken.ValueMultiplier = -3.f;
	FLureWaterContext NaNArea = Reef;
	NaNArea.Luck = NaN;
	Env.GearLuck = NaN;
	const FFishRollContext Guarded = FLureWaterRules::MakeBiteContext(NaNArea, Tag(TEXT("Habitat.Reef")), Broken, Env, 1);
	TestTrue(TEXT("NaN luck terms count 0"), Guarded.Luck == 0.f);
	TestTrue(TEXT("size bonus clamped to 1, a bad value multiplier counts 1"), Guarded.SizeBonus == 1.f && Guarded.ValueMultiplier == 1.f);
	return true;
}

// =====================================================================================================================
// The roll inputs added to the fish core (fish-system-rules.md, T-027 additions)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterRollSizeAndValue, "Project.Fishing.Water.Roll.SizeAndValueInputs", LureWaterTest::Flags)
bool FLureWaterRollSizeAndValue::RunTest(const FString& Parameters)
{
	// SizeBonus moves the natural weight roll toward WeightMax (W + (Max - W) * B), before the difficulty scaling; forced weights
	// ignore it; ValueMultiplier multiplies the value. 0 and 1 leave the fish exactly as without them. Rarity and modifiers
	// (their own RNG streams) don't change.
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FFishTables Tables = Fish.Get();
	AddExpectedMessagePlain(TEXT("SizeBonus is not finite"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("ValueMultiplier"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
	auto Roll = [&Tables](FName Species, int32 Seed, float Size, float Value, bool bNoMods = true)
	{
		FFishRollContext Context;
		Context.SpeciesId = Species;
		Context.Seed = Seed;
		Context.RegionTag = Tag(TEXT("Region.Tropical"));
		Context.SizeBonus = Size;
		Context.ValueMultiplier = Value;
		Context.bForceModifiers = bNoMods; // no Heavy/Giant: the weight is the natural roll
		Context.ForcedRarityId = TEXT("Common");
		FFishInstance Out;
		FFishRoll::Roll(Tables, Context, Out);
		return Out;
	};
	int32 Checked = 0;
	for (const FName Species : { FName(TEXT("Bonefish")), FName(TEXT("CoralSnapper")) })
	{
		const FFishSpeciesRow* Row = Tables.FindSpecies(Species);
		if (!TestNotNull(TEXT("species row"), Row))
		{
			return false;
		}
		for (int32 Seed = -50; Seed < 50; ++Seed)
		{
			const FFishInstance Base = Roll(Species, Seed, 0.f, 1.f);
			const FFishInstance Again = Roll(Species, Seed, 0.f, 1.f);
			const FFishInstance Bigger = Roll(Species, Seed, 0.4f, 1.f);
			const FFishInstance Max = Roll(Species, Seed, 1.f, 1.f);
			const FFishInstance Over = Roll(Species, Seed, 3.f, 1.f);
			const FFishInstance Negative = Roll(Species, Seed, -1.f, 1.f);
			const float Expected = Base.WeightKg + (Row->WeightMax - Base.WeightKg) * 0.4f;
			if (!FMath::IsNearlyEqual(Bigger.WeightKg, Expected, 1.0e-3f) || !FMath::IsNearlyEqual(Max.WeightKg, Row->WeightMax, 1.0e-4f)
				|| Over.WeightKg != Max.WeightKg || Negative.WeightKg != Base.WeightKg || Again.WeightKg != Base.WeightKg || Bigger.Value < Base.Value
				|| Bigger.RarityId != Base.RarityId || Bigger.Level != Base.Level)
			{
				AddError(FString::Printf(TEXT("%s seed %d: base %.4f, bonus 0.4 %.4f (expected %.4f), bonus 1 %.4f (max %.4f), bonus 3 %.4f, bonus -1 %.4f"),
					*Species.ToString(), Seed, Base.WeightKg, Bigger.WeightKg, Expected, Max.WeightKg, Row->WeightMax, Over.WeightKg, Negative.WeightKg));
				break;
			}
			// Bigger fish fight harder: difficulty stats scale with (W / ReferenceWeight) ^ exponent.
			const FGameplayTag Strength = Tag(TEXT("Fish.Stat.Strength"));
			const float Ratio = Bigger.GetStat(Strength) / FMath::Max(1.0e-6f, Base.GetStat(Strength));
			const float ExpectedRatio = FMath::Pow(Bigger.WeightKg / Base.WeightKg, Row->WeightStatExponent);
			if (!FMath::IsNearlyEqual(Ratio, ExpectedRatio, 2.0e-3f))
			{
				AddError(FString::Printf(TEXT("%s seed %d: strength ratio %.5f, expected (weight ratio)^exp %.5f"), *Species.ToString(), Seed, Ratio, ExpectedRatio));
				break;
			}
			++Checked;
		}
	}
	TestEqual(TEXT("200 rolls checked"), Checked, 200);

	// Rolls with modifiers: the rarity and modifier results don't move with the size bonus.
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		FFishRollContext Context;
		Context.SpeciesId = TEXT("Bonefish");
		Context.Seed = Seed;
		Context.Luck = 2.f;
		FFishInstance A;
		FFishInstance B;
		FFishRoll::Roll(Tables, Context, A);
		Context.SizeBonus = 0.5f;
		FFishRoll::Roll(Tables, Context, B);
		if (A.RarityId != B.RarityId || A.ModifierIds != B.ModifierIds || B.WeightKg < A.WeightKg)
		{
			AddError(FString::Printf(TEXT("seed %d: the size bonus changed the rarity or modifiers, or made the fish smaller"), Seed));
			break;
		}
	}

	// A forced weight fraction ignores the size bonus.
	FFishRollContext Forced;
	Forced.SpeciesId = TEXT("Bonefish");
	Forced.bForceWeightFraction = true;
	Forced.ForcedWeightFraction = 0.25f;
	Forced.bForceModifiers = true;
	FFishInstance ForcedPlain;
	FFishInstance ForcedBonus;
	FFishRoll::Roll(Tables, Forced, ForcedPlain);
	Forced.SizeBonus = 0.9f;
	FFishRoll::Roll(Tables, Forced, ForcedBonus);
	TestEqual(TEXT("forced weight: the size bonus is ignored"), ForcedBonus.WeightKg, ForcedPlain.WeightKg);

	// Value: round-half-up(BaseValuePerKg * W * rarity (1) * mods (none) * ValueMultiplier), at least 1.
	const FFishSpeciesRow* Bonefish = Tables.FindSpecies(TEXT("Bonefish"));
	for (const float Multiplier : { 2.f, 0.5f, 1.37f })
	{
		for (int32 Seed = 0; Seed < 30; ++Seed)
		{
			const FFishInstance Fish2 = Roll(TEXT("Bonefish"), Seed, 0.f, Multiplier);
			const int32 Expected = FMath::Max(1, static_cast<int32>(FMath::FloorToDouble(static_cast<double>(Bonefish->BaseValuePerKg) * Fish2.WeightKg * Multiplier + 0.5)));
			if (Fish2.Value != Expected)
			{
				AddError(FString::Printf(TEXT("value x%.2f, seed %d: %d, expected %d"), Multiplier, Seed, Fish2.Value, Expected));
				break;
			}
		}
	}
	const FFishInstance One = Roll(TEXT("Bonefish"), 5, 0.f, 1.f);
	TestEqual(TEXT("ValueMultiplier 0 counts as 1 (warning)"), Roll(TEXT("Bonefish"), 5, 0.f, 0.f).Value, One.Value);
	TestEqual(TEXT("ValueMultiplier -2 counts as 1 (warning)"), Roll(TEXT("Bonefish"), 5, 0.f, -2.f).Value, One.Value);
	TestEqual(TEXT("ValueMultiplier NaN counts as 1 (warning)"), Roll(TEXT("Bonefish"), 5, 0.f, std::numeric_limits<float>::quiet_NaN()).Value, One.Value);
	TestEqual(TEXT("SizeBonus NaN counts as 0 (warning)"), Roll(TEXT("Bonefish"), 5, std::numeric_limits<float>::quiet_NaN(), 1.f).WeightKg, One.WeightKg);

	// Deterministic: the same seed and bonuses give the same fish, field by field.
	const FFishInstance X = Roll(TEXT("CoralSnapper"), 1234, 0.35f, 1.25f, false);
	const FFishInstance Y = Roll(TEXT("CoralSnapper"), 1234, 0.35f, 1.25f, false);
	const TArray<FString> Diff = DiffFields(FFishInstance::StaticStruct(), &X, &Y);
	TestEqual(FString::Printf(TEXT("same seed and bonuses: the same fish (differs in %s)"), *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
	return true;
}

// =====================================================================================================================
// Data rules
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterDataHotSpotTable, "Project.Fishing.Water.Data.HotSpotTableValid", LureWaterTest::Flags)
bool FLureWaterDataHotSpotTable::RunTest(const FString& Parameters)
{
	// data/tables/DT_HotSpot.json imports cleanly, every row is valid, and the struct defaults (the built-in fallback) are the
	// shipped "Bubbles" row.
	const FString Json = LoadText(TEXT("data/tables/DT_HotSpot.json"));
	if (!TestFalse(TEXT("data/tables/DT_HotSpot.json loads"), Json.IsEmpty()))
	{
		return false;
	}
	TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
	Table->RowStruct = FLureHotSpotRow::StaticStruct();
	const TArray<FString> ImportProblems = Table->CreateTableFromJSONString(Json);
	TestEqual(FString::Printf(TEXT("import problems (%s)"), *FString::Join(ImportProblems, TEXT(" | "))), ImportProblems.Num(), 0);
	const TArray<FString> SourceProblems = FFishDataValidator::ValidateJsonSource(Json, FLureHotSpotRow::StaticStruct(), TEXT("DT_HotSpot"));
	TestEqual(FString::Printf(TEXT("source problems (%s)"), *FString::Join(SourceProblems, TEXT(" | "))), SourceProblems.Num(), 0);
	const TArray<TPair<FName, FLureHotSpotRow>> Rows = FLureWaterQuery::GetHotSpotRows(Table.Get());
	TestTrue(TEXT("the shipped types: Bubbles and Ripples"), Rows.Num() >= 2 && Rows.ContainsByPredicate([](const TPair<FName, FLureHotSpotRow>& R) { return R.Key == TEXT("Bubbles"); })
		&& Rows.ContainsByPredicate([](const TPair<FName, FLureHotSpotRow>& R) { return R.Key == TEXT("Ripples"); }));
	for (const TPair<FName, FLureHotSpotRow>& Row : Rows)
	{
		const TArray<FString> Problems = Row.Value.Validate(Row.Key);
		TestEqual(FString::Printf(TEXT("%s: valid (%s)"), *Row.Key.ToString(), *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
		TestTrue(FString::Printf(TEXT("%s: a real bonus (luck, size or value)"), *Row.Key.ToString()), Row.Value.LuckBonus > 0.f || Row.Value.SizeBonus > 0.f || Row.Value.ValueMultiplier > 1.f);
		TestTrue(FString::Printf(TEXT("%s: it lasts minutes, as designed"), *Row.Key.ToString()), Row.Value.LifetimeMax >= 60.f);
		TestTrue(FString::Printf(TEXT("%s: no visual class yet, or a class that exists"), *Row.Key.ToString()), Row.Value.VisualClass.IsNull() || Row.Value.VisualClass.LoadSynchronous() != nullptr);
	}
	FLureHotSpotRow Shipped;
	if (TestTrue(TEXT("Bubbles row"), FLureWaterQuery::FindHotSpotRow(Table.Get(), TEXT("Bubbles"), Shipped)))
	{
		const FLureHotSpotRow Defaults;
		TArray<FString> Diff = DiffFields(FLureHotSpotRow::StaticStruct(), &Defaults, &Shipped);
		Diff.RemoveAll([&Defaults, &Shipped](const FString& Name)
		{
			// Texts: compare the strings (an imported FText is not culture-invariant like FText::FromString).
			return (Name == TEXT("DisplayName") && Defaults.DisplayName.ToString() == Shipped.DisplayName.ToString())
				|| (Name == TEXT("HudText") && Defaults.HudText.ToString() == Shipped.HudText.ToString()) || Name == TEXT("DevComment");
		});
		TestEqual(FString::Printf(TEXT("the struct defaults are the shipped Bubbles row (differs in: %s)"), *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
	}
	const TArray<TPair<FName, FLureHotSpotRow>> BuiltIn = FLureWaterQuery::GetHotSpotRows(nullptr);
	TestTrue(TEXT("without the table: the built-in Bubbles row"), BuiltIn.Num() == 1 && BuiltIn[0].Key == FLureWaterRules::FallbackHotSpotType());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterDataLayoutAreas, "Project.Fishing.Water.Data.LayoutAreasValid", LureWaterTest::Flags)
bool FLureWaterDataLayoutAreas::RunTest(const FString& Parameters)
{
	// Every "water_area" in data/levels/*.json parses like the builder configures it (registered tags, a usable outline, a
	// valid depth band); every "hot_spots" marker names existing DT_HotSpot rows. The spec's example markers parse and resolve.
	using namespace WaterLocal;
	int32 Areas = 0;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : LayoutMarkers(*this, TEXT("water_area")))
	{
		++Areas;
		FLureWaterAreaInfo Area;
		TArray<FString> Problems;
		ParseLayoutArea(Entry.Value, Area, Problems);
		TestEqual(FString::Printf(TEXT("%s: %s (%s)"), *Entry.Key, *Area.AreaId.ToString(), *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
	}
	TSet<FName> Rows;
	{
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = FLureHotSpotRow::StaticStruct();
		Table->CreateTableFromJSONString(LoadText(TEXT("data/tables/DT_HotSpot.json")));
		for (const TPair<FName, FLureHotSpotRow>& Row : FLureWaterQuery::GetHotSpotRows(Table.Get()))
		{
			Rows.Add(Row.Key);
		}
	}
	int32 Spawners = 0;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : LayoutMarkers(*this, TEXT("hot_spots")))
	{
		++Spawners;
		const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
		if (Entry.Value->TryGetArrayField(TEXT("types"), Types))
		{
			for (const TSharedPtr<FJsonValue>& Type : *Types)
			{
				TestTrue(FString::Printf(TEXT("%s: hot spot type %s is a DT_HotSpot row"), *Entry.Key, *Type->AsString()), Rows.Contains(FName(*Type->AsString())));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("layouts: %d water areas, %d hot spot spawners"), Areas, Spawners));

	// The spec's example (docs/specs/fishing-water-rules.md section 9): parse and resolve like the game.
	const FString Sample = TEXT("[")
		TEXT("{\"type\":\"water_area\",\"id\":\"reef_flats\",\"name\":\"Reef Flats\",\"habitat\":\"Habitat.Reef\",\"region\":\"Region.Tropical.PalmKey\",\"priority\":10,\"luck\":0.0,\"depth\":[0,0],\"shape\":\"circle\",\"at\":[-1500,5400,0],\"radius\":900},")
		TEXT("{\"type\":\"water_area\",\"id\":\"lagoon\",\"name\":\"Lagoon\",\"habitat\":\"Habitat.Lagoon\",\"priority\":5,\"shape\":\"polygon\",\"points\":[[-2500,-3000],[1500,-3200],[1800,-6500],[-2800,-6200]]},")
		TEXT("{\"type\":\"water_area\",\"id\":\"dock_shelf\",\"name\":\"Dock Shelf\",\"habitat\":\"Habitat.Shore\",\"priority\":5,\"shape\":\"box\",\"at\":[-6800,600,0],\"size\":[2400,1600],\"yaw\":0},")
		TEXT("{\"type\":\"water_area\",\"id\":\"sea_shallows\",\"name\":\"Shallows\",\"habitat\":\"Habitat.Shore\",\"priority\":-100,\"shape\":\"everywhere\",\"depth\":[0,300]},")
		TEXT("{\"type\":\"water_area\",\"id\":\"sea_deep\",\"name\":\"Deep water\",\"habitat\":\"Habitat.DeepDrop\",\"priority\":-100,\"shape\":\"everywhere\",\"depth\":[300,0]}")
		TEXT("]");
	TArray<TSharedPtr<FJsonValue>> Parsed;
	if (!TestTrue(TEXT("the sample parses as JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Sample), Parsed)))
	{
		return false;
	}
	TArray<FLureWaterAreaInfo> SampleAreas;
	for (const TSharedPtr<FJsonValue>& Value : Parsed)
	{
		FLureWaterAreaInfo Area;
		TArray<FString> Problems;
		ParseLayoutArea(Value->AsObject(), Area, Problems);
		TestEqual(FString::Printf(TEXT("sample %s parses (%s)"), *Area.AreaId.ToString(), *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
		SampleAreas.Add(Area);
	}
	auto At = [&SampleAreas](double X, double Y, float Depth) -> FName
	{
		const int32 Index = FLureWaterRules::FindAreaIndex(SampleAreas, FVector2D(X, Y), Depth);
		return Index == INDEX_NONE ? NAME_None : SampleAreas[Index].AreaId;
	};
	TestEqual(TEXT("sample: the reef flats"), At(-1500.0, 5400.0, 100.f), FName(TEXT("reef_flats")));
	TestEqual(TEXT("sample: the lagoon polygon"), At(0.0, -4500.0, 90.f), FName(TEXT("lagoon")));
	TestEqual(TEXT("sample: the dock shelf box"), At(-6800.0, 1300.0, 200.f), FName(TEXT("dock_shelf")));
	TestEqual(TEXT("sample: open shallows"), At(20000.0, 0.0, 120.f), FName(TEXT("sea_shallows")));
	TestEqual(TEXT("sample: open deep water"), At(20000.0, 0.0, 800.f), FName(TEXT("sea_deep")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterDataHabitatsAllDay, "Project.Fishing.Water.Data.HabitatsHaveSpeciesAllDay", LureWaterTest::Flags)
bool FLureWaterDataHabitatsAllDay::RunTest(const FString& Parameters)
{
	// The data rule: every habitat a level uses (its water areas; without areas its legacy spots; plus the default water) has a
	// species that can bite at every hour (bait ignored). Gaps are listed for T-009; with the settings' gap fallback habitats no
	// water may be dead at any hour (hard).
	using namespace WaterLocal;
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FFishTables Tables = Fish.Get();
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	const TArray<FGameplayTag> Fallbacks = Settings->GetGapFallbackTags();
	const FGameplayTag DefaultRegion = Tag(*GetDefault<ULureFishingSettings>()->DefaultRegion.ToString());
	TestTrue(TEXT("the default water habitat is registered"), Settings->GetDefaultWaterHabitatTag().IsValid());
	TestEqual(TEXT("every gap fallback habitat is registered"), Fallbacks.Num(), Settings->GapFallbackHabitats.Num());

	TArray<FString> Files;
	const TArray<TPair<FString, TSharedPtr<FJsonObject>>> AreaMarkers = LayoutMarkers(*this, TEXT("water_area"), &Files);
	const TArray<TPair<FString, TSharedPtr<FJsonObject>>> SpotMarkers = LayoutMarkers(*this, TEXT("fishing_spot"));
	TArray<FString> GapLines;
	for (const FString& File : Files)
	{
		// (habitat, region) pairs this level uses.
		TArray<TPair<FGameplayTag, FGameplayTag>> Used;
		auto Use = [&Used, &DefaultRegion](const FGameplayTag& Habitat, const FGameplayTag& Region)
		{
			const TPair<FGameplayTag, FGameplayTag> Pair(Habitat, Region.IsValid() ? Region : DefaultRegion);
			if (Habitat.IsValid() && !Used.Contains(Pair))
			{
				Used.Add(Pair);
			}
		};
		bool bAreas = false;
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : AreaMarkers)
		{
			if (Entry.Key == File)
			{
				FLureWaterAreaInfo Area;
				TArray<FString> Problems;
				ParseLayoutArea(Entry.Value, Area, Problems);
				Use(Area.HabitatTag, Area.RegionTag);
				bAreas = true;
			}
		}
		if (!bAreas)
		{
			for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : SpotMarkers)
			{
				FString Habitat, Region;
				if (Entry.Key == File && Entry.Value->TryGetStringField(TEXT("habitat"), Habitat))
				{
					Entry.Value->TryGetStringField(TEXT("region"), Region);
					Use(Tag(*Habitat), Tag(*Region));
				}
			}
		}
		Use(Settings->GetDefaultWaterHabitatTag(), FGameplayTag());
		for (const TPair<FGameplayTag, FGameplayTag>& Pair : Used)
		{
			const TArray<FString> Gaps = FLureWaterRules::FindGapHours(Tables, Pair.Key, Pair.Value, {}, 0.25f);
			if (Gaps.Num() > 0)
			{
				GapLines.Add(FString::Printf(TEXT("%s: %s (%s) has no species at %s"), *File, *Pair.Key.ToString(), *Pair.Value.ToString(), *FString::Join(Gaps, TEXT(", "))));
			}
			const TArray<FString> Dead = FLureWaterRules::FindGapHours(Tables, Pair.Key, Pair.Value, Fallbacks, 0.25f);
			TestEqual(FString::Printf(TEXT("%s: %s water is never dead, gap fallbacks included (dead at %s)"), *File, *Pair.Key.ToString(), *FString::Join(Dead, TEXT(", "))),
				Dead.Num(), 0);
		}
	}
	AddInfo(GapLines.Num() == 0 ? FString(TEXT("No habitat gaps: every habitat has a species at every hour."))
		: FString::Printf(TEXT("Habitat gaps for the species batch (T-009); the gap fallback covers them meanwhile:\n%s"), *FString::Join(GapLines, TEXT("\n"))));

	// The rule itself, on a known case: the reef has no species 09:00-15:00, the shore none 19:00-05:00.
	const TArray<FString> Reef = FLureWaterRules::FindGapHours(Tables, Tag(TEXT("Habitat.Reef")), DefaultRegion, {}, 0.25f);
	TestTrue(FString::Printf(TEXT("the reef's gap is 09:00-15:00 (%s)"), *FString::Join(Reef, TEXT(", "))), Reef.Num() == 1 && Reef[0] == TEXT("09:00-15:00"));
	const TArray<FString> Shore = FLureWaterRules::FindGapHours(Tables, Tag(TEXT("Habitat.Shore")), DefaultRegion, {}, 0.25f);
	TestTrue(FString::Printf(TEXT("the shore's gaps are 00:00-05:00 and 19:00-24:00 (%s)"), *FString::Join(Shore, TEXT(", "))),
		Shore.Num() == 2 && Shore[0] == TEXT("00:00-05:00") && Shore[1] == TEXT("19:00-24:00"));
	const TArray<FGameplayTag> Both = { Tag(TEXT("Habitat.Shore")), Tag(TEXT("Habitat.Reef")) };
	TestEqual(TEXT("with both as fallbacks the reef is never dead"), FLureWaterRules::FindGapHours(Tables, Tag(TEXT("Habitat.Reef")), DefaultRegion, Both, 0.25f).Num(), 0);
	return true;
}

// =====================================================================================================================
// In a world: casting anywhere, depth, overlaps, reasons on the HUD
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterWorldCastAnywhere, "Project.Fishing.Water.World.CastAnywhereFindsHabitat", LureWaterTest::Flags)
bool FLureWaterWorldCastAnywhere::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1); // the gap fallback at work (T-009 adds species)
	// Any cast onto water finds a habitat: default water with no areas, an area where there is one; the bite follows it.
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, World.Spawn(*this), QuickProfile(0.5f), 20.f);
	if (!Fishing)
	{
		return false;
	}
	World.Tick(10);
	for (const float Charge : { 0.f, 0.5f, 1.f })
	{
		const FString Label = FString::Printf(TEXT("charge %.1f, no areas"), Charge);
		if (!CastAndLand(*this, World, Fishing, Charge))
		{
			return false;
		}
		const FLureWaterContext& Water = Fishing->GetWaterContext();
		TestTrue(Label + TEXT(": on water, default water"), Water.bOnWater && Water.Source == ELureWaterSource::Default && Water.AreaId.IsNone());
		TestTrue(Label + TEXT(": the default habitat"), Water.HabitatTag == GetDefault<ULureWaterSettings>()->GetDefaultWaterHabitatTag());
		TestTrue(Label + TEXT(": no seabed: as deep as the probe"), FMath::IsNearlyEqual(Water.DepthCm, GetDefault<ULureWaterSettings>()->DepthProbe, 1.f));
		TestTrue(Label + TEXT(": a bite is coming"), !Fishing->GetNetState().bNoFishHere && Fishing->GetScheduledBiteTime() >= 0.0);
		TestTrue(Label + TEXT(": HUD 'Water: open water'"), Fishing->GetStatusText().Contains(TEXT("Water: open water")));
		Fishing->AuthorityReelIn();
		World.Tick(2);
	}

	// A reef area around the charge-0.5 landing (with a region and luck): the bite is a reef fish at 20:00.
	World.AddCircleArea(TEXT("qa_reef"), TEXT("Habitat.Reef"), FVector2D(HalfCastX, 0.0), 500.f, 0, TEXT("Region.Tropical.PalmKey"), 0.75f);
	if (!CastAndLand(*this, World, Fishing, 0.5f))
	{
		return false;
	}
	const FLureWaterContext& Reef = Fishing->GetWaterContext();
	TestTrue(TEXT("in the reef area"), Reef.Source == ELureWaterSource::Area && Reef.AreaId == TEXT("qa_reef") && Reef.HabitatTag == Tag(TEXT("Habitat.Reef")));
	TestEqual(TEXT("replicated: SpotId = the area id"), Fishing->GetNetState().SpotId, FName(TEXT("qa_reef")));
	TestTrue(TEXT("HasCurrentSpot in a named area"), Fishing->HasCurrentSpot());
	TestTrue(TEXT("HUD: the area's name"), Fishing->GetStatusText().Contains(TEXT("Water: qa_reef water")));
	if (TestTrue(TEXT("a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120)))
	{
		const FFishRollContext& Rolled = Fishing->GetLastRollContext();
		TestTrue(TEXT("rolled with the area's habitat and region"), Rolled.HabitatTag == Tag(TEXT("Habitat.Reef")) && Rolled.RegionTag == Tag(TEXT("Region.Tropical.PalmKey")));
		TestNearlyEqual(TEXT("... and its luck"), Rolled.Luck, 0.75f, 1.0e-4f);
		TestEqual(TEXT("a reef fish"), Fishing->GetPendingFish().SpeciesId, FName(TEXT("CoralSnapper")));
		FName Species;
		FFishInstance Expected;
		FFishRollContext Context = Rolled;
		FFishRoll::PickSpecies(World.Fish.Get(), Rolled, Species);
		Context.SpeciesId = Species;
		FFishRoll::Roll(World.Fish.Get(), Context, Expected);
		const TArray<FString> Diff = DiffFields(FFishInstance::StaticStruct(), &Fishing->GetPendingFish(), &Expected);
		TestEqual(FString::Printf(TEXT("the bite is PickSpecies + Roll of that context (differs in %s)"), *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterWorldDepth, "Project.Fishing.Water.World.DepthBandsAndShallows", LureWaterTest::Flags)
bool FLureWaterWorldDepth::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1); // the gap fallback at work (T-009 adds species)
	// The depth under the bobber picks depth-banded areas; water shallower than MinBiteDepth never bites and says so at once.
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	AActor* Seabed = World.AddSeabed(-10.f); // 10 cm deep from x = 500 to 9500
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, World.Spawn(*this), QuickProfile(0.5f), 16.f);
	if (!Fishing)
	{
		return false;
	}
	World.Tick(10);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestNearlyEqual(TEXT("10 cm deep"), Fishing->GetWaterContext().DepthCm, 10.f, 0.5f);
	TestTrue(TEXT("too shallow: nothing can bite, no bite scheduled"), Fishing->GetNetState().bNoFishHere && Fishing->GetScheduledBiteTime() < 0.0);
	TestEqual(TEXT("the reason is replicated"), static_cast<int32>(Fishing->GetNetState().Water.NoBiteReason), static_cast<int32>(ELureNoBiteReason::TooShallow));
	TestTrue(TEXT("HUD: too shallow, at once"), Fishing->GetStatusText().Contains(TEXT("Too shallow")));
	World.Tick(120);
	TestEqual(TEXT("2 s later: still no bite"), static_cast<int32>(Fishing->GetFishingState()), static_cast<int32>(ELureFishingState::Waiting));
	Fishing->AuthorityReelIn();
	World.Tick(2);

	// Deeper: 400 cm. Shallows [0, 300) and deep [300, ...) as two everywhere areas: the deep one wins; at 16:00 DeepDrop has no
	// species, so the bite uses the gap fallback (the water's habitat stays DeepDrop).
	Seabed->Destroy();
	World.AddSeabed(-400.f);
	World.AddArea(TEXT("qa_shallows"), TEXT("Habitat.Shore"), FVector::ZeroVector, 0.f, -100, [](ALureWaterArea& Area) { Area.SetShapeEverywhere(); Area.MaxDepth = 300.f; });
	World.AddArea(TEXT("qa_deep"), TEXT("Habitat.DeepDrop"), FVector::ZeroVector, 0.f, -100, [](ALureWaterArea& Area) { Area.SetShapeEverywhere(); Area.MinDepth = 300.f; });
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestNearlyEqual(TEXT("400 cm deep"), Fishing->GetWaterContext().DepthCm, 400.f, 0.5f);
	TestEqual(TEXT("the deep band wins"), Fishing->GetWaterContext().AreaId, FName(TEXT("qa_deep")));
	if (TestTrue(TEXT("a bite (gap fallback)"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120)))
	{
		TestTrue(TEXT("the water stays DeepDrop"), Fishing->GetWaterContext().HabitatTag == Tag(TEXT("Habitat.DeepDrop")));
		TestTrue(TEXT("... the bite used the first fallback that has a fish at 16:00 (Shore)"), Fishing->GetLastRollContext().HabitatTag == Tag(TEXT("Habitat.Shore")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterWorldOverlap, "Project.Fishing.Water.World.OverlapPriority", LureWaterTest::Flags)
bool FLureWaterWorldOverlap::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1); // the gap fallback at work (T-009 adds species)
	// Two areas over the landing point: the higher priority decides; swap the priorities and the other one does.
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALureWaterArea* Lagoon = World.AddArea(TEXT("qa_lagoon"), TEXT("Habitat.Lagoon"), FVector(HalfCastX, 0.f, 0.f), 0.f, 0,
		[](ALureWaterArea& Area) { Area.SetShapeBox(FVector2D(3000.0, 3000.0)); });
	ALureWaterArea* Reef = World.AddCircleArea(TEXT("qa_reef"), TEXT("Habitat.Reef"), FVector2D(HalfCastX, 0.0), 400.f, 5);
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, World.Spawn(*this), QuickProfile(5.f), 20.f);
	if (!Fishing || !Lagoon || !Reef)
	{
		return false;
	}
	World.Tick(10);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestEqual(TEXT("priority 5 reef over priority 0 lagoon"), Fishing->GetWaterContext().AreaId, FName(TEXT("qa_reef")));
	Fishing->AuthorityReelIn();
	World.Tick(2);
	Lagoon->Priority = 9;
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestEqual(TEXT("priority 9 lagoon over priority 5 reef (areas are read at every cast)"), Fishing->GetWaterContext().AreaId, FName(TEXT("qa_lagoon")));
	TestTrue(TEXT("... with the lagoon's habitat"), Fishing->GetWaterContext().HabitatTag == Tag(TEXT("Habitat.Lagoon")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterWorldLegacy, "Project.Fishing.Water.World.LegacySpotUntilAreasExist", LureWaterTest::Flags)
bool FLureWaterWorldLegacy::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1); // the gap fallback at work (T-009 adds species)
	// A level built before the water-area pass keeps its spot habitats; once it has a water area, the spot markers are ignored.
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(HalfCastX, 0.f, 0.f), { TEXT("Spot=qa_spot"), TEXT("Name=QA Spot"), TEXT("Habitat=Habitat.Reef"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=600"), TEXT("Luck=0.5") });
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, World.Spawn(*this), QuickProfile(5.f), 20.f);
	if (!Fishing)
	{
		return false;
	}
	World.Tick(10);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	const FLureWaterContext& Spot = Fishing->GetWaterContext();
	TestTrue(TEXT("the spot marker, as a legacy area"), Spot.Source == ELureWaterSource::LegacySpot && Spot.AreaId == TEXT("qa_spot") && Spot.HabitatTag == Tag(TEXT("Habitat.Reef")));
	TestNearlyEqual(TEXT("... with its luck"), Spot.Luck, 0.5f, 1.0e-5f);
	TestTrue(TEXT("HUD: the marker's name"), Fishing->GetStatusText().Contains(TEXT("Water: QA Spot")));
	Fishing->AuthorityReelIn();
	World.Tick(2);
	World.AddCircleArea(TEXT("qa_elsewhere"), TEXT("Habitat.Lagoon"), FVector2D(-30000.0, 0.0), 500.f);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestTrue(TEXT("a water area exists: the marker is ignored, this is default water"), Fishing->GetWaterContext().Source == ELureWaterSource::Default
		&& Fishing->GetNetState().SpotId.IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterWorldWrongBait, "Project.Fishing.Water.World.WrongBaitSaysSo", LureWaterTest::Flags)
bool FLureWaterWorldWrongBait::RunTest(const FString& Parameters)
{
	// Water with fish, but none takes the bait: no bite, the replicated reason, and the HUD names the bait after the hint delay.
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, World.Spawn(*this), QuickProfile(0.5f), 12.f);
	if (!Fishing)
	{
		return false;
	}
	Fishing->BaitTag = Tag(TEXT("Bait.Squid"));
	World.Tick(10);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestTrue(TEXT("nothing takes squid in shore water at noon"), Fishing->GetNetState().bNoFishHere);
	TestEqual(TEXT("reason: WrongBait"), static_cast<int32>(Fishing->GetNetState().Water.NoBiteReason), static_cast<int32>(ELureNoBiteReason::WrongBait));
	TestFalse(TEXT("no hint before the hint delay"), Fishing->GetStatusText().Contains(TEXT("takes your bait")));
	World.Tick(90);
	TestTrue(TEXT("HUD: 'Nothing here takes your bait (Squid)'"), Fishing->GetStatusText().Contains(TEXT("Nothing here takes your bait (Squid)")));
	TestFalse(TEXT("never the old 'Nothing is biting here'"), Fishing->GetStatusText().Contains(TEXT("Nothing is biting")));
	TestEqual(TEXT("no bite"), static_cast<int32>(Fishing->GetFishingState()), static_cast<int32>(ELureFishingState::Waiting));
	Fishing->BaitTag = Tag(TEXT("Bait.Shrimp"));
	TestTrue(TEXT("with shrimp, the next check bites (checked every BiteWaitMax)"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 900));
	return true;
}

} // namespace LureWaterTest

#endif // WITH_DEV_AUTOMATION_TESTS

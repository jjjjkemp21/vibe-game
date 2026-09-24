// Lure T-027 QA (qa-engineer): "fish anywhere" - water areas, depth, the bite decision, the gap fallback, the new roll inputs,
// the legacy fishing_spot fallback and data validation of DT_HotSpot and the layouts' water areas.
// Project.Fishing.Water.QA.{Area,Bite,Gap,Roll,World,Data}.* - spec: docs/specs/fishing-water-rules.md; design: GAME_DESIGN.md
// "Fish: Anywhere, Hot spots"; acceptance: the T-027 line in docs/TASKS.md. Black-box: expectations come from the spec and the
// header contracts (Fishing/FishingWater*.h, Fish/FishInstance.h), never from the .cpp files.

#include "Tests/Fishing/QAFishingWaterTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Tests/Fishing/QAFishingTestUtils.h"
#include "UObject/UnrealType.h"

namespace LureWaterQA
{
namespace WaterLocal
{
	/** Scoped change of a ULureWaterSettings member (restored at scope end). */
	template <typename TValue>
	struct TScopedWater
	{
		TValue ULureWaterSettings::* Member;
		TValue Saved;
		TScopedWater(TValue ULureWaterSettings::* InMember, const TValue& Value)
			: Member(InMember)
		{
			ULureWaterSettings* Settings = GetMutableDefault<ULureWaterSettings>();
			Saved = Settings->*Member;
			Settings->*Member = Value;
		}
		~TScopedWater()
		{
			GetMutableDefault<ULureWaterSettings>()->*Member = Saved;
		}
	};

	/** One stable number per fish (every field that the roll produces). */
	uint32 FishHash(const FFishInstance& Fish)
	{
		uint32 Hash = GetTypeHash(Fish.SpeciesId.ToString());
		Hash = HashCombine(Hash, GetTypeHash(Fish.RarityId.ToString()));
		for (const FName& Id : Fish.ModifierIds)
		{
			Hash = HashCombine(Hash, GetTypeHash(Id.ToString()));
		}
		Hash = HashCombine(Hash, GetTypeHash(Fish.WeightKg));
		Hash = HashCombine(Hash, GetTypeHash(Fish.Level));
		for (const FFishStatValue& Stat : Fish.Stats)
		{
			Hash = HashCombine(Hash, GetTypeHash(Stat.Tag.GetTagName().ToString()));
			Hash = HashCombine(Hash, GetTypeHash(Stat.Value));
		}
		Hash = HashCombine(Hash, GetTypeHash(Fish.Value));
		Hash = HashCombine(Hash, GetTypeHash(Fish.Xp));
		Hash = HashCombine(Hash, GetTypeHash(Fish.DifficultyRating));
		Hash = HashCombine(Hash, GetTypeHash(Fish.Seed));
		return Hash;
	}

	FFishRollContext RollContext(FName Species, int32 Seed, float Luck = 0.f)
	{
		FFishRollContext Context;
		Context.SpeciesId = Species;
		Context.Seed = Seed;
		Context.Luck = Luck;
		Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
		return Context;
	}

	/** A plain Common fish with no modifiers (so weight and value follow the spec formulas exactly). */
	FFishRollContext PlainContext(FName Species, int32 Seed)
	{
		FFishRollContext Context = RollContext(Species, Seed);
		Context.ForcedRarityId = TEXT("Common");
		Context.bForceModifiers = true;
		return Context;
	}

	/** Casts along +X with Charge and ticks until the bobber waits on the water (or land). */
	bool CastAndWait(FAutomationTestBase& Test, QAFishing::FScene& Scene, ULureFishingComponent* Fishing, float Charge = 0.5f)
	{
		if (!Test.TestTrue(TEXT("QA: the cast starts"), Fishing && Fishing->AuthorityCast(Charge, 0.f)))
		{
			return false;
		}
		return Test.TestTrue(TEXT("QA: the bobber lands (Waiting)"),
			Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	}

	/** A deep seabed box under the landing zone (top at TopZ; the sea is z = 0). */
	void AddSeabed(QAFishing::FScene& Scene, float TopZ)
	{
		Scene.AddBox(FVector(2500.f, 0.f, TopZ - 50.f), FVector(2000.f, 2000.f, 50.f));
	}

	ALureWaterArea* SpawnArea(UWorld* World, const TCHAR* Id, const TCHAR* Habitat, const FVector& Location, float Yaw, int32 Priority)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureWaterArea* Area = World->SpawnActor<ALureWaterArea>(ALureWaterArea::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), Location), Params);
		if (Area)
		{
			Area->AreaId = FName(Id);
			Area->Priority = Priority;
			Area->SetAreaTags(FName(Habitat), NAME_None);
		}
		return Area;
	}

	/** Segment AB crosses segment CD (proper crossing or touching away from shared endpoints). */
	bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		auto Orient = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R)
		{
			const double V = (Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X);
			return V > 1.0e-9 ? 1 : (V < -1.0e-9 ? -1 : 0);
		};
		const int32 O1 = Orient(A, B, C), O2 = Orient(A, B, D), O3 = Orient(C, D, A), O4 = Orient(C, D, B);
		return O1 * O2 < 0 && O3 * O4 < 0;
	}

	/**
	 *  The water_area schema of fishing-water-rules.md section 9, checked independently of layout.py. Returns the problems of
	 *  one marker; fills OutArea (world space) for the geometry checks.
	 */
	TArray<FString> CheckWaterAreaMarker(const TSharedPtr<FJsonObject>& Marker, FLureWaterAreaInfo& OutArea)
	{
		TArray<FString> Problems;
		FString Id, Shape, Habitat, Region;
		if (!Marker->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
		{
			Problems.Add(TEXT("no id"));
		}
		OutArea = FLureWaterAreaInfo();
		OutArea.AreaId = FName(*Id);
		if (!Marker->TryGetStringField(TEXT("habitat"), Habitat) || !Habitat.StartsWith(TEXT("Habitat.")) || !Tag(*Habitat).IsValid())
		{
			Problems.Add(FString::Printf(TEXT("habitat '%s' is not a registered Habitat.* tag"), *Habitat));
		}
		OutArea.HabitatTag = Tag(*Habitat);
		if (Marker->TryGetStringField(TEXT("region"), Region) && (!Region.StartsWith(TEXT("Region.")) || !Tag(*Region).IsValid()))
		{
			Problems.Add(FString::Printf(TEXT("region '%s' is not a registered Region.* tag"), *Region));
		}
		double Number = 0.0;
		if (Marker->TryGetNumberField(TEXT("priority"), Number) && Number != FMath::FloorToDouble(Number))
		{
			Problems.Add(TEXT("priority is not an integer"));
		}
		OutArea.Priority = static_cast<int32>(Number);
		if (Marker->TryGetNumberField(TEXT("luck"), Number) && !(Number >= 0.0))
		{
			Problems.Add(TEXT("luck < 0"));
		}
		const TArray<TSharedPtr<FJsonValue>>* Depth = nullptr;
		if (Marker->TryGetArrayField(TEXT("depth"), Depth))
		{
			if (Depth->Num() != 2)
			{
				Problems.Add(TEXT("depth is not [min, max]"));
			}
			else
			{
				const double Min = (*Depth)[0]->AsNumber();
				const double Max = (*Depth)[1]->AsNumber();
				if (!(Min >= 0.0) || !(Max == 0.0 || Max > Min))
				{
					Problems.Add(FString::Printf(TEXT("depth [%g, %g]: min must be >= 0 and max 0 or > min"), Min, Max));
				}
				OutArea.MinDepth = static_cast<float>(Min);
				OutArea.MaxDepth = static_cast<float>(Max);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
		const bool bAt = Marker->TryGetArrayField(TEXT("at"), At) && At->Num() >= 2;
		if (bAt)
		{
			OutArea.Center = FVector2D((*At)[0]->AsNumber(), (*At)[1]->AsNumber());
		}
		Marker->TryGetStringField(TEXT("shape"), Shape);
		if (Shape == TEXT("circle"))
		{
			OutArea.Shape = ELureWaterAreaShape::Circle;
			double Radius = 0.0;
			if (!Marker->TryGetNumberField(TEXT("radius"), Radius) || !(Radius > 0.0))
			{
				Problems.Add(TEXT("circle radius must be > 0"));
			}
			if (!bAt)
			{
				Problems.Add(TEXT("circle without 'at'"));
			}
			OutArea.Radius = static_cast<float>(Radius);
		}
		else if (Shape == TEXT("box"))
		{
			OutArea.Shape = ELureWaterAreaShape::Box;
			const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
			if (!Marker->TryGetArrayField(TEXT("size"), Size) || Size->Num() != 2 || !((*Size)[0]->AsNumber() > 0.0) || !((*Size)[1]->AsNumber() > 0.0))
			{
				Problems.Add(TEXT("box size must be [x > 0, y > 0]"));
			}
			else
			{
				OutArea.HalfSize = FVector2D((*Size)[0]->AsNumber() * 0.5, (*Size)[1]->AsNumber() * 0.5);
			}
			if (!bAt)
			{
				Problems.Add(TEXT("box without 'at'"));
			}
			double Yaw = 0.0;
			Marker->TryGetNumberField(TEXT("yaw"), Yaw);
			OutArea.YawDegrees = static_cast<float>(Yaw);
		}
		else if (Shape == TEXT("polygon"))
		{
			OutArea.Shape = ELureWaterAreaShape::Polygon;
			const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
			if (!Marker->TryGetArrayField(TEXT("points"), Points) || Points->Num() < 3)
			{
				Problems.Add(TEXT("polygon needs >= 3 points"));
			}
			else
			{
				for (const TSharedPtr<FJsonValue>& Point : *Points)
				{
					const TArray<TSharedPtr<FJsonValue>>& XY = Point->AsArray();
					OutArea.Polygon.Add(XY.Num() >= 2 ? FVector2D(XY[0]->AsNumber(), XY[1]->AsNumber()) : FVector2D::ZeroVector);
				}
				const int32 N = OutArea.Polygon.Num();
				for (int32 I = 0; I < N; ++I)
				{
					if (FVector2D::Distance(OutArea.Polygon[I], OutArea.Polygon[(I + 1) % N]) < 1.0)
					{
						Problems.Add(FString::Printf(TEXT("polygon edge %d has zero length"), I));
					}
					for (int32 J = I + 2; J < N; ++J)
					{
						if ((J + 1) % N == I)
						{
							continue; // neighbours share a point
						}
						if (SegmentsCross(OutArea.Polygon[I], OutArea.Polygon[(I + 1) % N], OutArea.Polygon[J], OutArea.Polygon[(J + 1) % N]))
						{
							Problems.Add(FString::Printf(TEXT("polygon crosses itself (edges %d and %d)"), I, J));
						}
					}
				}
			}
		}
		else if (Shape == TEXT("everywhere"))
		{
			OutArea.Shape = ELureWaterAreaShape::Everywhere;
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("unknown shape '%s'"), *Shape));
		}
		if (Problems.Num() == 0 && !OutArea.HasShape())
		{
			Problems.Add(TEXT("the game sees no usable outline (FLureWaterAreaInfo::HasShape is false)"));
		}
		return Problems;
	}

	/** Every problem of a layout's water_area and hot_spots markers (ids unique, schema, types known, one spawner). */
	TArray<FString> CheckLayoutWater(const TSharedPtr<FJsonObject>& Root, const TSet<FName>& HotSpotTypes, TArray<FLureWaterAreaInfo>* OutAreas = nullptr)
	{
		TArray<FString> Problems;
		TSet<FString> Ids;
		for (const TSharedPtr<FJsonObject>& Marker : MarkersOfType(Root, TEXT("water_area")))
		{
			FLureWaterAreaInfo Area;
			const FString Id = Marker->GetStringField(TEXT("id"));
			for (const FString& Problem : CheckWaterAreaMarker(Marker, Area))
			{
				Problems.Add(FString::Printf(TEXT("water_area '%s': %s"), *Id, *Problem));
			}
			bool bDuplicate = false;
			Ids.Add(Id, &bDuplicate);
			if (bDuplicate)
			{
				Problems.Add(FString::Printf(TEXT("water_area id '%s' is used twice"), *Id));
			}
			if (OutAreas)
			{
				OutAreas->Add(Area);
			}
		}
		const TArray<TSharedPtr<FJsonObject>> Spawners = MarkersOfType(Root, TEXT("hot_spots"));
		if (Spawners.Num() > 1)
		{
			Problems.Add(FString::Printf(TEXT("%d hot_spots markers (one spawner per level)"), Spawners.Num()));
		}
		for (const TSharedPtr<FJsonObject>& Marker : Spawners)
		{
			const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
			if (Marker->TryGetArrayField(TEXT("types"), Types))
			{
				for (const TSharedPtr<FJsonValue>& Type : *Types)
				{
					if (!HotSpotTypes.Contains(FName(*Type->AsString())))
					{
						Problems.Add(FString::Printf(TEXT("hot_spots type '%s' is not a DT_HotSpot row"), *Type->AsString()));
					}
				}
			}
			double Max = 0.0;
			if (Marker->TryGetNumberField(TEXT("max"), Max) && !(Max >= 0.0))
			{
				Problems.Add(TEXT("hot_spots max < 0"));
			}
		}
		return Problems;
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root);
		return Root;
	}
}

// =====================================================================================================================
// Which area wins (pure)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaHigherPriorityWins, "Project.Fishing.Water.QA.Area.HigherPriorityWins", Flags)
bool FQAWaterAreaHigherPriorityWins::RunTest(const FString& Parameters)
{
	// Spec 2: among the areas that contain the point, the highest priority wins, even if it is the bigger one; the array order never matters.
	const FLureWaterAreaInfo Big = QACircle(TEXT("qa_big"), TEXT("Habitat.Lagoon"), 0.0, 0.0, 2000.f, 10);
	const FLureWaterAreaInfo Small = QACircle(TEXT("qa_small"), TEXT("Habitat.Reef"), 500.0, 0.0, 300.f, 5);
	const TArray<FLureWaterAreaInfo> AB = { Big, Small };
	const TArray<FLureWaterAreaInfo> BA = { Small, Big };
	TestEqual(TEXT("in both: the higher priority (the big one) wins"), WinnerAt(AB, 500.0, 0.0), FString(TEXT("qa_big")));
	TestEqual(TEXT("... in either array order"), WinnerAt(BA, 500.0, 0.0), FString(TEXT("qa_big")));
	FLureWaterAreaInfo Negative = Small;
	Negative.Priority = -1000;
	TestEqual(TEXT("a negative priority still wins where it is alone"), WinnerAt({ Negative }, 500.0, 0.0), FString(TEXT("qa_small")));
	FLureWaterAreaInfo Top = Small;
	Top.Priority = 11;
	TestEqual(TEXT("priority 11 beats 10 (one step)"), WinnerAt({ Big, Top }, 500.0, 0.0), FString(TEXT("qa_small")));
	TestTrue(TEXT("IsBetterArea agrees: priority first"), FLureWaterRules::IsBetterArea(Top, Big) && !FLureWaterRules::IsBetterArea(Big, Top));
	TestEqual(TEXT("outside every area: default water"), WinnerAt(AB, 5000.0, 5000.0), FString(TEXT("default")));
	TestEqual(TEXT("no areas at all: default water"), WinnerAt(TArray<FLureWaterAreaInfo>(), 0.0, 0.0), FString(TEXT("default")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaSmallerWinsTie, "Project.Fishing.Water.QA.Area.SmallerAreaWinsPriorityTie", Flags)
bool FQAWaterAreaSmallerWinsTie::RunTest(const FString& Parameters)
{
	// Spec 2: on a priority tie the smaller area wins (nested and overlapping), across shape types; "everywhere" is infinitely big.
	const FLureWaterAreaInfo Outer = QACircle(TEXT("qa_outer"), TEXT("Habitat.Lagoon"), 0.0, 0.0, 3000.f, 0);
	const FLureWaterAreaInfo Inner = QACircle(TEXT("qa_inner"), TEXT("Habitat.Lagoon.Mouth"), 0.0, 0.0, 1000.f, 0);
	const FLureWaterAreaInfo Core = QABox(TEXT("qa_core"), TEXT("Habitat.Reef"), 0.0, 0.0, 200.0, 200.0, 0.f, 0);
	const FLureWaterAreaInfo Sea = QAEverywhere(TEXT("qa_sea"), TEXT("Habitat.Shore"), 0);
	const TArray<FLureWaterAreaInfo> Areas = { Sea, Outer, Core, Inner };
	TestEqual(TEXT("three nested areas + everywhere, the centre: the smallest (the 4 m box)"), WinnerAt(Areas, 0.0, 0.0), FString(TEXT("qa_core")));
	TestEqual(TEXT("in the inner circle only"), WinnerAt(Areas, 600.0, 0.0), FString(TEXT("qa_inner")));
	TestEqual(TEXT("in the outer ring only"), WinnerAt(Areas, 2000.0, 0.0), FString(TEXT("qa_outer")));
	TestEqual(TEXT("outside the circles: the everywhere area (not default water)"), WinnerAt(Areas, 9000.0, 0.0), FString(TEXT("qa_sea")));
	TestTrue(TEXT("sizes: box 400x400 = 160000 cm^2"), FMath::IsNearlyEqual(Core.GetSize(), 160000.0, 1.0));
	TestTrue(TEXT("sizes: circle r1000 = pi x 1e6"), FMath::IsNearlyEqual(Inner.GetSize(), UE_DOUBLE_PI * 1.0e6, 1.0e3));
	TestTrue(TEXT("everywhere is bigger than any bounded area"), Sea.GetSize() > Outer.GetSize() && FLureWaterRules::IsBetterArea(Outer, Sea));

	// Two partly overlapping circles of the same priority: the overlap goes to the smaller one, the rest to each.
	const FLureWaterAreaInfo Left = QACircle(TEXT("qa_left"), TEXT("Habitat.Shore"), -500.0, 0.0, 800.f, 3);
	const FLureWaterAreaInfo Right = QACircle(TEXT("qa_right"), TEXT("Habitat.Reef"), 500.0, 0.0, 700.f, 3);
	TestEqual(TEXT("overlap: the smaller circle"), WinnerAt({ Left, Right }, 0.0, 0.0), FString(TEXT("qa_right")));
	TestEqual(TEXT("left only"), WinnerAt({ Left, Right }, -1000.0, 0.0), FString(TEXT("qa_left")));
	// A higher-priority big area around a lower-priority small one: priority beats size.
	FLureWaterAreaInfo Cove = Inner;
	Cove.Priority = -1;
	TestEqual(TEXT("a smaller area with a lower priority loses"), WinnerAt({ Outer, Cove }, 0.0, 0.0), FString(TEXT("qa_outer")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaLowerIdWinsFullTie, "Project.Fishing.Water.QA.Area.LowerIdWinsFullTie", Flags)
bool FQAWaterAreaLowerIdWinsFullTie::RunTest(const FString& Parameters)
{
	// Spec 2: same priority and the same size: the lower id (FName::LexicalLess) wins, whatever the array order (deterministic on
	// every machine).
	const FLureWaterAreaInfo A = QACircle(TEXT("qa_alpha"), TEXT("Habitat.Shore"), 0.0, 0.0, 500.f, 2);
	const FLureWaterAreaInfo B = QACircle(TEXT("qa_beta"), TEXT("Habitat.Reef"), 100.0, 0.0, 500.f, 2);
	const FLureWaterAreaInfo C = QACircle(TEXT("qa_gamma"), TEXT("Habitat.Lagoon"), 50.0, 50.0, 500.f, 2);
	TArray<FLureWaterAreaInfo> Areas = { C, B, A };
	TestEqual(TEXT("equal circles: the lower id"), WinnerAt({ B, A }, 50.0, 0.0), FString(TEXT("qa_alpha")));
	TestEqual(TEXT("... in the other order"), WinnerAt({ A, B }, 50.0, 0.0), FString(TEXT("qa_alpha")));
	Algo::Reverse(Areas);
	TestEqual(TEXT("a third equal circle joins (reversed order): still the lowest id"), WinnerAt(Areas, 50.0, 0.0), FString(TEXT("qa_alpha")));
	TestTrue(TEXT("IsBetterArea: alpha before beta, not the reverse"), FLureWaterRules::IsBetterArea(A, B) && !FLureWaterRules::IsBetterArea(B, A));
	TestFalse(TEXT("IsBetterArea is irreflexive (an area is not better than itself)"), FLureWaterRules::IsBetterArea(A, A));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaBrokenAreasNeverWin, "Project.Fishing.Water.QA.Area.BrokenAreasNeverWin", Flags)
bool FQAWaterAreaBrokenAreasNeverWin::RunTest(const FString& Parameters)
{
	// Spec 2: an area with an unregistered habitat or no usable shape never wins, so bad data can't make a dead zone: the water
	// under it is whatever it would be without it.
	const FLureWaterAreaInfo Good = QACircle(TEXT("qa_good"), TEXT("Habitat.Shore"), 0.0, 0.0, 2000.f, 0);
	TArray<FLureWaterAreaInfo> Broken;
	Broken.Add(QACircle(TEXT("qa_bad_habitat"), TEXT("Habitat.QA_NotRegistered"), 0.0, 0.0, 800.f, 100));
	Broken.Add(QACircle(TEXT("qa_zero_radius"), TEXT("Habitat.Reef"), 0.0, 0.0, 0.f, 100));
	Broken.Add(QACircle(TEXT("qa_negative_radius"), TEXT("Habitat.Reef"), 0.0, 0.0, -500.f, 100));
	Broken.Add(QACircle(TEXT("qa_nan_radius"), TEXT("Habitat.Reef"), 0.0, 0.0, NaN(), 100));
	Broken.Add(QABox(TEXT("qa_flat_box"), TEXT("Habitat.Reef"), 0.0, 0.0, 500.0, 0.0, 0.f, 100));
	Broken.Add(QAPolygon(TEXT("qa_two_points"), TEXT("Habitat.Reef"), { FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0) }, 100));
	Broken.Add(QAPolygon(TEXT("qa_collinear"), TEXT("Habitat.Reef"), { FVector2D(-500.0, 0.0), FVector2D(0.0, 0.0), FVector2D(500.0, 0.0) }, 100));
	FLureWaterAreaInfo NoHabitat = QAEverywhere(TEXT("qa_no_habitat"), TEXT("Habitat.Reef"), 100);
	NoHabitat.HabitatTag = FGameplayTag();
	Broken.Add(NoHabitat);
	for (const FLureWaterAreaInfo& Bad : Broken)
	{
		const FString Label = Bad.AreaId.ToString();
		TestEqual(Label + TEXT(" over a good area (priority 100 vs 0): the good area decides"), WinnerAt({ Bad, Good }, 0.0, 0.0), FString(TEXT("qa_good")));
		TestEqual(Label + TEXT(" alone: default water"), WinnerAt({ Bad }, 0.0, 0.0), FString(TEXT("default")));
	}
	for (int32 Index = 1; Index < Broken.Num() - 1; ++Index)
	{
		TestFalse(Broken[Index].AreaId.ToString() + TEXT(": HasShape is false"), Broken[Index].HasShape());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaPolygonEdgeCases, "Project.Fishing.Water.QA.Area.PolygonEdgeCases", Flags)
bool FQAWaterAreaPolygonEdgeCases::RunTest(const FString& Parameters)
{
	// Spec 2: polygons are 3+ world X/Y points in order, concave is fine (even-odd rule), either winding.
	// A "U" (a bay with a notch in the top middle): 0..3000 x 0..2000 minus the notch 1000..2000 x 1000..2000.
	const TArray<FVector2D> U = { FVector2D(0.0, 0.0), FVector2D(3000.0, 0.0), FVector2D(3000.0, 2000.0), FVector2D(2000.0, 2000.0),
		FVector2D(2000.0, 1000.0), FVector2D(1000.0, 1000.0), FVector2D(1000.0, 2000.0), FVector2D(0.0, 2000.0) };
	TArray<FVector2D> Reversed = U;
	Algo::Reverse(Reversed);
	const TArray<FVector2D>* Windings[] = { &U, &Reversed };
	for (const TArray<FVector2D>* Points : Windings)
	{
		const FLureWaterAreaInfo Poly = QAPolygon(TEXT("qa_u"), TEXT("Habitat.Lagoon"), *Points, 0);
		const FString Winding = Points == &U ? TEXT("counter-clockwise") : TEXT("clockwise");
		TestTrue(Winding + TEXT(": usable"), Poly.HasShape());
		TestTrue(Winding + TEXT(": the base is inside"), Poly.Contains(FVector2D(1500.0, 500.0)));
		TestTrue(Winding + TEXT(": the left arm is inside"), Poly.Contains(FVector2D(500.0, 1500.0)));
		TestTrue(Winding + TEXT(": the right arm is inside"), Poly.Contains(FVector2D(2500.0, 1900.0)));
		TestFalse(Winding + TEXT(": the notch is outside (concave)"), Poly.Contains(FVector2D(1500.0, 1500.0)));
		TestFalse(Winding + TEXT(": beside the polygon is outside"), Poly.Contains(FVector2D(-10.0, 1000.0)));
		TestFalse(Winding + TEXT(": far away is outside"), Poly.Contains(FVector2D(1.0e6, 1.0e6)));
		TestTrue(Winding + TEXT(": its size is positive and right (6e6 - 1e6 cm^2)"), FMath::IsNearlyEqual(Poly.GetSize(), 5.0e6, 1.0));
		const FBox2D Bounds = Poly.GetBounds();
		TestTrue(Winding + TEXT(": bounds 0..3000 x 0..2000"), Bounds.bIsValid && Bounds.Min.Equals(FVector2D(0.0, 0.0)) && Bounds.Max.Equals(FVector2D(3000.0, 2000.0)));
		// Points on the same horizontal line as vertices (the ray-cast corner cases of even-odd).
		TestTrue(Winding + TEXT(": level with the notch bottom, inside the base"), Poly.Contains(FVector2D(500.0, 1000.0)));
		TestFalse(Winding + TEXT(": level with the notch bottom, outside on the left"), Poly.Contains(FVector2D(-500.0, 1000.0)));
		TestFalse(Winding + TEXT(": level with the top vertices, outside on the right"), Poly.Contains(FVector2D(3500.0, 2000.0)));
	}
	// Nested in a priority rule: a polygon beats an overlapping everywhere area of the same priority.
	const TArray<FLureWaterAreaInfo> Areas = { QAEverywhere(TEXT("qa_sea"), TEXT("Habitat.Shore"), 0), QAPolygon(TEXT("qa_u"), TEXT("Habitat.Lagoon"), U, 0) };
	TestEqual(TEXT("in the U: the polygon"), WinnerAt(Areas, 1500.0, 500.0), FString(TEXT("qa_u")));
	TestEqual(TEXT("in the notch: the sea"), WinnerAt(Areas, 1500.0, 1500.0), FString(TEXT("qa_sea")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaBoxYawCircleEdge, "Project.Fishing.Water.QA.Area.BoxYawAndCircleEdge", Flags)
bool FQAWaterAreaBoxYawCircleEdge::RunTest(const FString& Parameters)
{
	// Spec 2: box = center + size turned by yaw; circle and box edges count (FLureWaterAreaInfo::Contains).
	const FLureWaterAreaInfo Long = QABox(TEXT("qa_long"), TEXT("Habitat.Shore"), 1000.0, 1000.0, 1000.0, 100.0, 0.f);
	TestTrue(TEXT("yaw 0: along X"), Long.Contains(FVector2D(1900.0, 1000.0)));
	TestFalse(TEXT("yaw 0: not along Y"), Long.Contains(FVector2D(1000.0, 1900.0)));
	FLureWaterAreaInfo Turned = Long;
	Turned.YawDegrees = 90.f;
	TestTrue(TEXT("yaw 90: along Y"), Turned.Contains(FVector2D(1000.0, 1900.0)));
	TestFalse(TEXT("yaw 90: not along X"), Turned.Contains(FVector2D(1900.0, 1000.0)));
	Turned.YawDegrees = 45.f;
	const double D = 900.0 / FMath::Sqrt(2.0);
	TestTrue(TEXT("yaw 45: along the diagonal"), Turned.Contains(FVector2D(1000.0 + D, 1000.0 + D)));
	TestFalse(TEXT("yaw 45: not along the other diagonal"), Turned.Contains(FVector2D(1000.0 + D, 1000.0 - D)));
	TestFalse(TEXT("yaw 45: the unturned corner is outside"), Turned.Contains(FVector2D(1950.0, 1090.0)));
	TestTrue(TEXT("a box edge counts"), Long.Contains(FVector2D(2000.0, 1000.0)) && Long.Contains(FVector2D(1000.0, 1100.0)));
	TestFalse(TEXT("just past a box edge"), Long.Contains(FVector2D(2000.5, 1000.0)));

	const FLureWaterAreaInfo Circle = QACircle(TEXT("qa_circle"), TEXT("Habitat.Reef"), -300.0, 400.0, 500.f);
	TestTrue(TEXT("a circle edge counts"), Circle.Contains(FVector2D(-300.0 + 300.0, 400.0 + 400.0)));
	TestFalse(TEXT("0.5 cm past the edge"), Circle.Contains(FVector2D(-300.0 + 300.3, 400.0 + 400.4)));
	TestTrue(TEXT("the centre"), Circle.Contains(FVector2D(-300.0, 400.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaDepthBands, "Project.Fishing.Water.QA.Area.DepthBands", Flags)
bool FQAWaterAreaDepthBands::RunTest(const FString& Parameters)
{
	// Spec 2/3: an area only counts where MinDepth <= depth < MaxDepth (max 0 = no limit); non-finite depth never matches. Depth
	// bands split the open sea (shallows vs deep water) and let a small band area sit on top of it.
	const FLureWaterAreaInfo Shallows = QAEverywhere(TEXT("qa_shallows"), TEXT("Habitat.Shore"), -100, 0.f, 300.f);
	const FLureWaterAreaInfo Deep = QAEverywhere(TEXT("qa_deep"), TEXT("Habitat.DeepDrop"), -100, 300.f, 0.f);
	const FLureWaterAreaInfo Shelf = QACircle(TEXT("qa_shelf"), TEXT("Habitat.Reef"), 0.0, 0.0, 1000.f, 10);
	FLureWaterAreaInfo Band = Shelf;
	Band.AreaId = TEXT("qa_band");
	Band.MinDepth = 100.f;
	Band.MaxDepth = 200.f;
	const TArray<FLureWaterAreaInfo> Sea = { Shallows, Deep };
	TestEqual(TEXT("depth 0: shallows (min inclusive)"), WinnerAt(Sea, 5000.0, 0.0, 0.f), FString(TEXT("qa_shallows")));
	TestEqual(TEXT("depth 299.9: shallows"), WinnerAt(Sea, 5000.0, 0.0, 299.9f), FString(TEXT("qa_shallows")));
	TestEqual(TEXT("depth 300: deep (max exclusive, min inclusive)"), WinnerAt(Sea, 5000.0, 0.0, 300.f), FString(TEXT("qa_deep")));
	TestEqual(TEXT("depth 5000 (the probe cap): deep (max 0 = no limit)"), WinnerAt(Sea, 5000.0, 0.0, 5000.f), FString(TEXT("qa_deep")));
	TestEqual(TEXT("NaN depth: no band matches -> default water"), WinnerAt(Sea, 5000.0, 0.0, NaN()), FString(TEXT("default")));
	TestFalse(TEXT("+inf depth never matches"), Deep.AcceptsDepth(Inf()));
	TestFalse(TEXT("-1 cm is below every band"), Shallows.AcceptsDepth(-1.f));
	const TArray<FLureWaterAreaInfo> WithBand = { Shallows, Deep, Band };
	TestEqual(TEXT("band 100-200, depth 99.9: under it the shallows"), WinnerAt(WithBand, 0.0, 0.0, 99.9f), FString(TEXT("qa_shallows")));
	TestEqual(TEXT("band 100-200, depth 100: the band"), WinnerAt(WithBand, 0.0, 0.0, 100.f), FString(TEXT("qa_band")));
	TestEqual(TEXT("band 100-200, depth 199.9: the band"), WinnerAt(WithBand, 0.0, 0.0, 199.9f), FString(TEXT("qa_band")));
	TestEqual(TEXT("band 100-200, depth 200: back to the shallows"), WinnerAt(WithBand, 0.0, 0.0, 200.f), FString(TEXT("qa_shallows")));
	TestEqual(TEXT("band area, outside its circle at depth 150: the shallows"), WinnerAt(WithBand, 2000.0, 0.0, 150.f), FString(TEXT("qa_shallows")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterAreaDefaultWaterContext, "Project.Fishing.Water.QA.Area.DefaultWaterContext", Flags)
bool FQAWaterAreaDefaultWaterContext::RunTest(const FString& Parameters)
{
	// Spec 2 "Default water": no area wins -> the default habitat, the default region, luck 0; an area gives its id, name, habitat,
	// region and luck. The HUD line names the water ("Water: <name>", "Water: open water").
	const FGameplayTag DefaultHabitat = Tag(TEXT("Habitat.Shore"));
	FLureWaterAreaInfo Cove = QACircle(TEXT("qa_cove"), TEXT("Habitat.Shore.Cove"), 0.0, 0.0, 500.f, 5);
	Cove.DisplayName = TEXT("QA Cove");
	Cove.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
	Cove.Luck = 0.5f;
	const TArray<FLureWaterAreaInfo> Areas = { Cove };
	const FLureWaterContext Open = FLureWaterRules::MakeWaterContext(Areas, FVector2D(4000.0, 0.0), 12.f, 640.f, DefaultHabitat);
	TestEqual(TEXT("open water: source Default"), static_cast<int32>(Open.Source), static_cast<int32>(ELureWaterSource::Default));
	TestTrue(TEXT("open water: no area id"), Open.AreaId.IsNone());
	TestTrue(TEXT("open water: the default habitat"), Open.HabitatTag == DefaultHabitat);
	TestFalse(TEXT("open water: no region (the settings' default region applies)"), Open.RegionTag.IsValid());
	TestEqual(TEXT("open water: luck 0"), Open.Luck, 0.f);
	TestTrue(TEXT("open water: on water, with depth and surface"), Open.bOnWater && Open.DepthCm == 640.f && Open.WaterZ == 12.f);
	const FLureWaterContext In = FLureWaterRules::MakeWaterContext(Areas, FVector2D(100.0, 0.0), 12.f, 640.f, DefaultHabitat);
	TestEqual(TEXT("in the cove: source Area"), static_cast<int32>(In.Source), static_cast<int32>(ELureWaterSource::Area));
	TestTrue(TEXT("in the cove: id, name, habitat, region, luck"), In.AreaId == TEXT("qa_cove") && In.AreaName == TEXT("QA Cove")
		&& In.HabitatTag == Cove.HabitatTag && In.RegionTag == Cove.RegionTag && In.Luck == 0.5f);
	TestEqual(TEXT("HUD line for an area"), FLureWaterRules::WaterLine(TEXT("QA Cove")), FString(TEXT("Water: QA Cove")));
	TestEqual(TEXT("HUD line for default water"), FLureWaterRules::WaterLine(FString()), FString(TEXT("Water: open water")));
	FLureWaterAreaInfo Unnamed = Cove;
	Unnamed.DisplayName.Reset();
	TestEqual(TEXT("an area without a name shows its id"), Unnamed.GetLabel(), FString(TEXT("qa_cove")));
	TestEqual(TEXT("the shipped default water habitat is Habitat.Shore"), GetDefault<ULureWaterSettings>()->DefaultWaterHabitat, FName(TEXT("Habitat.Shore")));
	TestTrue(TEXT("... and it is a registered tag"), GetDefault<ULureWaterSettings>()->GetDefaultWaterHabitatTag().IsValid());
	return true;
}

// =====================================================================================================================
// The bite decision (pure)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterBiteShallowBoundary, "Project.Fishing.Water.QA.Bite.ShallowBoundaryAndReasonOrder", Flags)
bool FQAWaterBiteShallowBoundary::RunTest(const FString& Parameters)
{
	// Spec 3/4: NotWater first, then TooShallow (< MinBiteDepth 15 cm) before any species check; 15 cm exactly bites. TooShallow
	// shows at once on the HUD, the other reasons after NoBiteHintDelay.
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureBiteRules Shipped = ShippedRules();
	TestEqual(TEXT("shipped MinBiteDepth is 15 cm"), Shipped.MinBiteDepth, 15.f);
	const FLureFishingEnvironment Noon = Environment(12.f);
	struct FCase { float Depth; ELureNoBiteReason Expect; };
	for (const FCase& Case : { FCase{ 0.f, ELureNoBiteReason::TooShallow }, FCase{ 5.f, ELureNoBiteReason::TooShallow }, FCase{ 14.99f, ELureNoBiteReason::TooShallow },
		FCase{ 15.f, ELureNoBiteReason::None }, FCase{ 15.01f, ELureNoBiteReason::None }, FCase{ 5000.f, ELureNoBiteReason::None } })
	{
		const FLureBiteDecision Decision = FLureWaterRules::DecideBite(Fish.Get(), DeepWater(TEXT("Habitat.Shore"), TEXT("Region.Tropical.PalmKey"), Case.Depth),
			FLureHotSpotBonus(), Noon, Shipped, 7);
		TestEqual(FString::Printf(TEXT("depth %.2f cm"), Case.Depth), FString(ReasonName(Decision.Reason)), FString(ReasonName(Case.Expect)));
	}
	// Order: land beats shallow; shallow beats "no species" (the reef at noon without a fallback) and wrong bait.
	FLureWaterContext Land = DeepWater(TEXT("Habitat.Shore"), nullptr, 0.f);
	Land.bOnWater = false;
	TestEqual(TEXT("on land (depth 0): NotWater, not TooShallow"), FString(ReasonName(FLureWaterRules::DecideBite(Fish.Get(), Land, FLureHotSpotBonus(), Noon, Shipped, 1).Reason)),
		FString(TEXT("NotWater")));
	TestEqual(TEXT("5 cm of reef at noon, no fallback: TooShallow first"), FString(ReasonName(FLureWaterRules::DecideBite(Fish.Get(),
		DeepWater(TEXT("Habitat.Reef"), nullptr, 5.f), FLureHotSpotBonus(), Noon, Rules(15.f, {}), 1).Reason)), FString(TEXT("TooShallow")));
	TestEqual(TEXT("5 cm with the wrong bait: TooShallow first"), FString(ReasonName(FLureWaterRules::DecideBite(Fish.Get(),
		DeepWater(TEXT("Habitat.Shore"), nullptr, 5.f), FLureHotSpotBonus(), Environment(12.f, TEXT("Bait.Squid")), Shipped, 1).Reason)), FString(TEXT("TooShallow")));
	TestEqual(TEXT("a hot spot does not make shallow water bite"), FString(ReasonName(FLureWaterRules::DecideBite(Fish.Get(),
		DeepWater(TEXT("Habitat.Shore"), nullptr, 10.f), FLureWaterRules::BonusFromRow(TEXT("Bubbles"), FLureHotSpotRow()), Noon, Shipped, 1).Reason)), FString(TEXT("TooShallow")));
	TestEqual(TEXT("MinBiteDepth 0 (data): 0 cm bites"), FString(ReasonName(FLureWaterRules::DecideBite(Fish.Get(),
		DeepWater(TEXT("Habitat.Shore"), nullptr, 0.f), FLureHotSpotBonus(), Noon, Rules(0.f, { TEXT("Habitat.Shore") }), 1).Reason)), FString(TEXT("None")));

	TestTrue(TEXT("TooShallow shows at once"), FLureWaterRules::ShowsAtOnce(ELureNoBiteReason::TooShallow));
	TestFalse(TEXT("NoSpecies waits for NoBiteHintDelay"), FLureWaterRules::ShowsAtOnce(ELureNoBiteReason::NoSpecies));
	TestFalse(TEXT("WrongBait waits for NoBiteHintDelay"), FLureWaterRules::ShowsAtOnce(ELureNoBiteReason::WrongBait));
	TestTrue(TEXT("text: too shallow"), FLureWaterRules::NoBiteText(ELureNoBiteReason::TooShallow, FGameplayTag()).Contains(TEXT("Too shallow")));
	TestTrue(TEXT("text: no fish live here"), FLureWaterRules::NoBiteText(ELureNoBiteReason::NoSpecies, FGameplayTag()).Contains(TEXT("No fish live in this water")));
	TestTrue(TEXT("text: wrong bait names the bait"), FLureWaterRules::NoBiteText(ELureNoBiteReason::WrongBait, Tag(TEXT("Bait.Squid"))).Contains(TEXT("Squid")));
	TestTrue(TEXT("text: None = empty"), FLureWaterRules::NoBiteText(ELureNoBiteReason::None, FGameplayTag()).IsEmpty());
	for (const ELureNoBiteReason Reason : { ELureNoBiteReason::NotWater, ELureNoBiteReason::TooShallow, ELureNoBiteReason::NoSpecies, ELureNoBiteReason::WrongBait })
	{
		TestFalse(FString(ReasonName(Reason)) + TEXT(": never the retired 'Nothing is biting here'"), FLureWaterRules::NoBiteText(Reason, Tag(TEXT("Bait.Shrimp"))).Contains(TEXT("Nothing is biting")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterGapFallbackOrder, "Project.Fishing.Water.QA.Gap.FallbackOrderAndBait", Flags)
bool FQAWaterGapFallbackOrder::RunTest(const FString& Parameters)
{
	// Spec 4 step 3/4: the water's own habitat if some species can bite now (bait ignored); else the first gap fallback (Shore,
	// then Reef) that has one; else NoSpecies. Bait never triggers the fallback: wrong bait on water that has fish = WrongBait.
	// Shipped fish: Bonefish (Shore, Lagoon; 05-19; worm/shrimp), Coral Snapper (Reef; 15-09; shrimp/squid).
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureBiteRules Shipped = ShippedRules();
	TestTrue(TEXT("shipped fallbacks: Shore, then Reef"), Shipped.GapFallbackHabitats.Num() == 2 && Shipped.GapFallbackHabitats[0] == Tag(TEXT("Habitat.Shore"))
		&& Shipped.GapFallbackHabitats[1] == Tag(TEXT("Habitat.Reef")));
	struct FCase
	{
		const TCHAR* Water;
		float Hours;
		const TCHAR* Bait;
		const TCHAR* ExpectHabitat;
		bool bFallback;
		ELureNoBiteReason Expect;
		const TCHAR* ExpectSpecies;
	};
	const FCase Cases[] = {
		{ TEXT("Habitat.Shore"), 12.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Shore"), false, ELureNoBiteReason::None, TEXT("Bonefish") },
		{ TEXT("Habitat.Reef"), 12.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Shore"), true, ELureNoBiteReason::None, TEXT("Bonefish") },         // reef gap at noon
		{ TEXT("Habitat.Reef.Edge"), 12.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Shore"), true, ELureNoBiteReason::None, TEXT("Bonefish") },    // child of Reef: same gap
		{ TEXT("Habitat.DeepDrop"), 12.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Shore"), true, ELureNoBiteReason::None, TEXT("Bonefish") },     // no deep fish yet
		{ TEXT("Habitat.DeepDrop"), 22.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Reef"), true, ELureNoBiteReason::None, TEXT("CoralSnapper") },  // shore gap at night -> second fallback
		{ TEXT("Habitat.Shore"), 22.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Reef"), true, ELureNoBiteReason::None, TEXT("CoralSnapper") },     // the shore itself at night
		{ TEXT("Habitat.Shore.Cove"), 3.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Reef"), true, ELureNoBiteReason::None, TEXT("CoralSnapper") },
		{ TEXT("Habitat.Reef"), 20.f, TEXT("Bait.Shrimp"), TEXT("Habitat.Reef"), false, ELureNoBiteReason::None, TEXT("CoralSnapper") },     // own habitat when it has fish
		{ TEXT("Habitat.Shore"), 16.f, TEXT("Bait.Squid"), TEXT("Habitat.Shore"), false, ELureNoBiteReason::WrongBait, nullptr },            // bonefish there, refuses squid; the reef's snapper would take it, but bait never falls back
		{ TEXT("Habitat.Reef"), 12.f, TEXT("Bait.Squid"), TEXT("Habitat.Shore"), true, ELureNoBiteReason::WrongBait, nullptr },              // gap -> shore, whose fish refuse squid
		{ TEXT("Habitat.Lagoon"), 12.f, TEXT("Bait.QA_None"), TEXT("Habitat.Lagoon"), false, ELureNoBiteReason::WrongBait, nullptr },
	};
	for (const FCase& Case : Cases)
	{
		const FString Label = FString::Printf(TEXT("%s at %.0f:00 with %s"), Case.Water, Case.Hours, Case.Bait);
		const FLureBiteDecision Decision = FLureWaterRules::DecideBite(Fish.Get(), DeepWater(Case.Water), FLureHotSpotBonus(), Environment(Case.Hours, Case.Bait), Shipped, 99);
		TestEqual(Label + TEXT(": reason"), FString(ReasonName(Decision.Reason)), FString(ReasonName(Case.Expect)));
		TestEqual(Label + TEXT(": bite habitat"), Decision.BiteHabitat.ToString(), FString(Case.ExpectHabitat));
		TestEqual(Label + TEXT(": fallback used"), Decision.bUsedFallback, Case.bFallback);
		if (Case.Expect == ELureNoBiteReason::None)
		{
			TestTrue(Label + TEXT(": the roll context uses the bite habitat"), Decision.Context.HabitatTag == Tag(Case.ExpectHabitat));
			FName Species;
			TestTrue(Label + TEXT(": PickSpecies finds a fish with that context"), FFishRoll::PickSpecies(Fish.Get(), Decision.Context, Species));
			TestEqual(Label + TEXT(": species"), Species, FName(Case.ExpectSpecies));
		}
	}
	// The fallback list is data: empty turns the safety net off; the order is honoured.
	TestEqual(TEXT("no fallbacks: the reef at noon has no fish -> NoSpecies"), FString(ReasonName(FLureWaterRules::DecideBite(Fish.Get(), DeepWater(TEXT("Habitat.Reef")),
		FLureHotSpotBonus(), Environment(12.f), Rules(15.f, {}), 1).Reason)), FString(TEXT("NoSpecies")));
	TestEqual(TEXT("fallbacks [Reef] only: deep water at noon still has none -> NoSpecies"), FString(ReasonName(FLureWaterRules::DecideBite(Fish.Get(),
		DeepWater(TEXT("Habitat.DeepDrop")), FLureHotSpotBonus(), Environment(12.f), Rules(15.f, { TEXT("Habitat.Reef") }), 1).Reason)), FString(TEXT("NoSpecies")));
	const FLureBiteDecision ReefFirst = FLureWaterRules::DecideBite(Fish.Get(), DeepWater(TEXT("Habitat.DeepDrop")), FLureHotSpotBonus(), Environment(16.f),
		Rules(15.f, { TEXT("Habitat.Reef"), TEXT("Habitat.Shore") }), 1);
	TestEqual(TEXT("fallbacks [Reef, Shore] at 16:00 (both have fish): the first in the list"), ReefFirst.BiteHabitat.ToString(), FString(TEXT("Habitat.Reef")));
	bool bFallback = false, bAny = false;
	const FGameplayTag Resolved = FLureWaterRules::ResolveBiteHabitat(Fish.Get(), Tag(TEXT("Habitat.DeepDrop")), Tag(TEXT("Region.Tropical")), 12.f, FGameplayTag(),
		{}, bFallback, bAny);
	TestTrue(TEXT("ResolveBiteHabitat without fallbacks: the water's own habitat, nothing found"), Resolved == Tag(TEXT("Habitat.DeepDrop")) && !bFallback && !bAny);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterGapLoggedOnce, "Project.Fishing.Water.QA.Gap.LoggedOncePerHabitatAndHour", Flags)
bool FQAWaterGapLoggedOnce::RunTest(const FString& Parameters)
{
	// Spec 4 step 3: a data gap logs one Warning per habitat and clock hour, never one per bite (so a long session doesn't spam
	// the log); a dead habitat (no fallback has fish either) is logged once too.
	// Exact Warning counts: DeepDrop at 12h and 13h, Reef.Edge at 12h, Lagoon.Mouth (dead) at 02h.
	AddExpectedMessagePlain(TEXT("Data gap: no species of Habitat.DeepDrop "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	AddExpectedMessagePlain(TEXT("Data gap: no species of Habitat.Reef.Edge "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Data gap: no species of Habitat.Lagoon.Mouth "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	FishQA::FTables Fish; // fresh tables: the "remembered" warnings are per table
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	FFishRoll::ResetDataWarnings(); // the "remembered" set is global and keyed by table address (a new table can reuse a freed one)
	const QAFishing::FLogCapture Log(TEXT("LogLureWater"));
	// Counted by text: once a line matches an expected message the automation framework hands it on at a lower verbosity, so the
	// Warning level and the exact per-habitat counts are checked by the expected messages above instead.
	auto GapCount = [&Log](ELogVerbosity::Type Verbosity, const TCHAR* Contains)
	{
		GLog->Flush(); // lines may still be queued for the output devices
		if (Verbosity == ELogVerbosity::Error)
		{
			return Log.Count(Verbosity, Contains);
		}
		int32 Found = 0;
		for (const TPair<ELogVerbosity::Type, FString>& Line : Log.Lines)
		{
			Found += (!Contains || Line.Value.Contains(Contains)) ? 1 : 0;
		}
		return Found;
	};
	const TArray<FGameplayTag> Fallbacks = { Tag(TEXT("Habitat.Shore")), Tag(TEXT("Habitat.Reef")) };
	const FGameplayTag Region = Tag(TEXT("Region.Tropical.PalmKey"));
	auto Resolve = [&](const TCHAR* Habitat, float Hours, TConstArrayView<FGameplayTag> List)
	{
		bool bFallback = false, bAny = false;
		FLureWaterRules::ResolveBiteHabitat(Fish.Get(), Tag(Habitat), Region, Hours, FGameplayTag(), List, bFallback, bAny);
		return GapCount(ELogVerbosity::Warning, TEXT("Data gap"));
	};
	TestEqual(TEXT("DeepDrop 12:00: one warning"), Resolve(TEXT("Habitat.DeepDrop"), 12.f, Fallbacks), 1);
	TestEqual(TEXT("DeepDrop 12:30 (same hour): still one"), Resolve(TEXT("Habitat.DeepDrop"), 12.5f, Fallbacks), 1);
	TestEqual(TEXT("DeepDrop 12:59: still one"), Resolve(TEXT("Habitat.DeepDrop"), 12.99f, Fallbacks), 1);
	for (int32 Bite = 0; Bite < 50; ++Bite)
	{
		Resolve(TEXT("Habitat.DeepDrop"), 12.25f, Fallbacks);
	}
	TestEqual(TEXT("50 more bites in that hour: still one"), GapCount(ELogVerbosity::Warning, TEXT("Data gap")), 1);
	TestEqual(TEXT("DeepDrop 13:00 (next hour): a second"), Resolve(TEXT("Habitat.DeepDrop"), 13.f, Fallbacks), 2);
	TestEqual(TEXT("Reef.Edge 12:00 (another habitat): a third"), Resolve(TEXT("Habitat.Reef.Edge"), 12.f, Fallbacks), 3);
	TestEqual(TEXT("the shore at noon has fish: no warning"), Resolve(TEXT("Habitat.Shore"), 12.f, Fallbacks), 3);
	TestTrue(TEXT("the warning names the habitat and the fallback"), GapCount(ELogVerbosity::Warning, TEXT("Habitat.DeepDrop")) >= 1
		&& GapCount(ELogVerbosity::Warning, TEXT("Habitat.Shore")) >= 1);
	// Dead water (no fallback list): logged once as well.
	TestEqual(TEXT("dead: Lagoon.Mouth 02:00 with no fallbacks: one more"), Resolve(TEXT("Habitat.Lagoon.Mouth"), 2.f, {}), 4);
	TestEqual(TEXT("dead again in that hour: no more"), Resolve(TEXT("Habitat.Lagoon.Mouth"), 2.7f, {}), 4);
	TestEqual(TEXT("no Error-level log for a data gap"), GapCount(ELogVerbosity::Error, nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterGapNoDeadWaterShipped, "Project.Fishing.Water.QA.Gap.NoDeadWaterAnyHourShipped", Flags)
bool FQAWaterGapNoDeadWaterShipped::RunTest(const FString& Parameters)
{
	// Acceptance: "every body of water can be fished; no 'Nothing is biting here' in valid water". With the shipped fish, bait
	// (the starter shrimp), settings (gap fallbacks) and layouts: every registered habitat, every region the layouts use (and the
	// default region), every weather and every quarter hour (plus the window edges) bites in water 15 cm deep or more.
	// The gaps without the fallback are listed for T-009 (info only).
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureBiteRules Shipped = ShippedRules();
	const FName DefaultBait = GetDefault<ULureFishingSettings>()->DefaultBait;
	TestFalse(TEXT("a default bait is set"), DefaultBait.IsNone());
	TArray<FGameplayTag> Habitats = TagsUnder(TEXT("Habitat"));
	Habitats.RemoveAll([](const FGameplayTag& T) { return T == Tag(TEXT("Habitat")); });
	TestTrue(TEXT("the habitats are registered (7 in the slice)"), Habitats.Num() >= 7);
	TSet<FString> Regions = { GetDefault<ULureFishingSettings>()->DefaultRegion.ToString() };
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Layout : LoadLayouts(*this))
	{
		for (const TCHAR* Type : { TEXT("water_area"), TEXT("fishing_spot") })
		{
			for (const TSharedPtr<FJsonObject>& Marker : MarkersOfType(Layout.Value, Type))
			{
				FString Region;
				if (Marker->TryGetStringField(TEXT("region"), Region) && !Region.IsEmpty())
				{
					Regions.Add(Region);
				}
			}
		}
	}
	TArray<FGameplayTag> Weathers = { FGameplayTag() };
	for (const FGameplayTag& Weather : TagsUnder(TEXT("Weather")))
	{
		if (Weather != Tag(TEXT("Weather")))
		{
			Weathers.Add(Weather);
		}
	}
	TArray<float> Hours;
	for (int32 Quarter = 0; Quarter < 96; ++Quarter)
	{
		Hours.Add(Quarter * 0.25f);
	}
	for (const float Edge : { 4.999f, 5.f, 8.999f, 9.f, 14.999f, 15.f, 18.999f, 19.f, 23.999f, 24.f })
	{
		Hours.Add(Edge);
	}
	int32 Checks = 0;
	TArray<FString> Dead;
	for (const FString& RegionName : Regions)
	{
		for (const FGameplayTag& Habitat : Habitats)
		{
			for (const FGameplayTag& Weather : Weathers)
			{
				for (const float Hour : Hours)
				{
					FLureWaterContext Water = DeepWater(*Habitat.ToString(), *RegionName, 15.f);
					FLureFishingEnvironment Env = Environment(Hour, *DefaultBait.ToString());
					Env.WeatherTag = Weather;
					const FLureBiteDecision Decision = FLureWaterRules::DecideBite(Fish.Get(), Water, FLureHotSpotBonus(), Env, Shipped, 5);
					++Checks;
					if (!Decision.CanBite() && Dead.Num() < 20)
					{
						Dead.Add(FString::Printf(TEXT("%s %s %s %.3f h: %s"), *RegionName, *Habitat.ToString(), *Weather.ToString(), Hour, ReasonName(Decision.Reason)));
					}
				}
			}
		}
	}
	TestEqual(FString::Printf(TEXT("dead water with the shipped data (%d checks): %s"), Checks, *FString::Join(Dead, TEXT("; "))), Dead.Num(), 0);
	// For T-009: the gaps the fallback covers today.
	for (const FGameplayTag& Habitat : Habitats)
	{
		const TArray<FString> Gaps = FLureWaterRules::FindGapHours(Fish.Get(), Habitat, Tag(TEXT("Region.Tropical.PalmKey")), {});
		if (Gaps.Num() > 0)
		{
			AddInfo(FString::Printf(TEXT("T-009 data gap (covered by the fallback): %s has no species at %s"), *Habitat.ToString(), *FString::Join(Gaps, TEXT(", "))));
		}
		TestEqual(Habitat.ToString() + TEXT(": no gap hours with the shipped fallbacks"),
			FLureWaterRules::FindGapHours(Fish.Get(), Habitat, Tag(TEXT("Region.Tropical.PalmKey")), Shipped.GapFallbackHabitats).Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterBiteContextSums, "Project.Fishing.Water.QA.Bite.ContextSumsLuckAndCarriesBonus", Flags)
bool FQAWaterBiteContextSums::RunTest(const FString& Parameters)
{
	// Spec 4 "The roll context": habitat = the one given (a fallback may differ from the water's), region = the area's or the
	// default, luck = area + gear + hot spot (non-finite terms count 0), SizeBonus/ValueMultiplier from the hot spot (neutral
	// without one), time, weather, bait and seed from the environment.
	FLureWaterContext Water = DeepWater(TEXT("Habitat.Reef"));
	Water.Luck = 0.5f;
	FLureFishingEnvironment Env = Environment(21.5f, TEXT("Bait.Squid"));
	Env.GearLuck = 0.25f;
	Env.WeatherTag = Tag(TEXT("Weather.Rain"));
	FLureHotSpotRow Row;
	Row.LuckBonus = 1.5f;
	Row.SizeBonus = 0.35f;
	Row.ValueMultiplier = 1.75f;
	const FLureHotSpotBonus Hot = FLureWaterRules::BonusFromRow(TEXT("Ripples"), Row);
	TestTrue(TEXT("BonusFromRow copies the row"), Hot.IsActive() && Hot.TypeId == TEXT("Ripples") && Hot.LuckBonus == 1.5f && Hot.SizeBonus == 0.35f
		&& Hot.ValueMultiplier == 1.75f && Hot.BiteWaitScale == Row.BiteWaitScale);
	const FFishRollContext Context = FLureWaterRules::MakeBiteContext(Water, Tag(TEXT("Habitat.Shore")), Hot, Env, -42);
	TestTrue(TEXT("habitat = the one given (a gap fallback)"), Context.HabitatTag == Tag(TEXT("Habitat.Shore")));
	TestTrue(TEXT("region = the area's"), Context.RegionTag == Tag(TEXT("Region.Tropical.PalmKey")));
	TestNearlyEqual(TEXT("luck = 0.5 + 0.25 + 1.5"), Context.Luck, 2.25f, 1.0e-5f);
	TestEqual(TEXT("size bonus"), Context.SizeBonus, 0.35f);
	TestEqual(TEXT("value multiplier"), Context.ValueMultiplier, 1.75f);
	TestTrue(TEXT("time, weather, bait, seed"), Context.TimeOfDayHours == 21.5f && Context.WeatherTag == Env.WeatherTag && Context.BaitTag == Env.BaitTag && Context.Seed == -42);
	TestTrue(TEXT("nothing forced (the roll picks species, rarity, modifiers, weight)"), Context.SpeciesId.IsNone() && Context.ForcedRarityId.IsNone()
		&& !Context.bForceModifiers && !Context.bForceWeightFraction);

	const FFishRollContext Plain = FLureWaterRules::MakeBiteContext(Water, Water.HabitatTag, FLureHotSpotBonus(), Env, 3);
	TestTrue(TEXT("no hot spot: SizeBonus exactly 0 and ValueMultiplier exactly 1"), Plain.SizeBonus == 0.f && Plain.ValueMultiplier == 1.f);
	TestNearlyEqual(TEXT("no hot spot: luck = area + gear"), Plain.Luck, 0.75f, 1.0e-5f);

	FLureHotSpotBonus NaNLuck = Hot;
	NaNLuck.LuckBonus = NaN();
	TestNearlyEqual(TEXT("NaN hot spot luck counts 0"), FLureWaterRules::MakeBiteContext(Water, Water.HabitatTag, NaNLuck, Env, 1).Luck, 0.75f, 1.0e-5f);
	FLureFishingEnvironment InfGear = Env;
	InfGear.GearLuck = Inf();
	TestNearlyEqual(TEXT("+inf gear luck counts 0"), FLureWaterRules::MakeBiteContext(Water, Water.HabitatTag, Hot, InfGear, 1).Luck, 2.f, 1.0e-5f);
	FLureWaterContext NaNArea = Water;
	NaNArea.Luck = NaN();
	TestNearlyEqual(TEXT("NaN area luck counts 0"), FLureWaterRules::MakeBiteContext(NaNArea, Water.HabitatTag, Hot, Env, 1).Luck, 1.75f, 1.0e-5f);
	FLureWaterContext NoRegion = Water;
	NoRegion.RegionTag = FGameplayTag();
	TestTrue(TEXT("no region: the environment's default region"), FLureWaterRules::MakeBiteContext(NoRegion, Water.HabitatTag, Hot, Env, 1).RegionTag == Env.DefaultRegionTag);
	return true;
}

// =====================================================================================================================
// The roll inputs (fish core)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterRollNoBonusBitIdentical, "Project.Fishing.Water.QA.Roll.NoBonusRollsUnchanged", Flags)
bool FQAWaterRollNoBonusBitIdentical::RunTest(const FString& Parameters)
{
	// Spec 6: SizeBonus 0 and ValueMultiplier 1 skip the new steps, so every roll without a hot spot is bit-identical to before
	// T-027. Two checks on a fixed seed set (both species, luck 0 and 3): a default context equals one with the neutral values
	// written explicitly, and the whole set hashes to the fingerprint pinned by QA (the roll code before T-027 only gained gated
	// steps; Project.Fish.QA.Roll.MatchesSpecFormula_Real checks the formula itself). Update the pin only with a deliberate
	// roll change.
	constexpr uint32 PinnedFingerprint = 2756817872u; // pinned by QA 2026-09-23 (lane eng1, T-027)
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	uint32 Fingerprint = 0;
	int32 Rolls = 0;
	for (const TCHAR* Species : { TEXT("Bonefish"), TEXT("CoralSnapper") })
	{
		for (const float Luck : { 0.f, 3.f })
		{
			for (int32 Seed = -100; Seed < 400; ++Seed)
			{
				const FFishRollContext Default = WaterLocal::RollContext(Species, Seed * 7919, Luck);
				FFishRollContext Neutral = Default;
				Neutral.SizeBonus = 0.f;
				Neutral.ValueMultiplier = 1.f;
				FFishInstance A, B;
				if (!FFishRoll::Roll(Fish.Get(), Default, A) || !FFishRoll::Roll(Fish.Get(), Neutral, B))
				{
					AddError(FString::Printf(TEXT("%s seed %d did not roll"), Species, Seed));
					return false;
				}
				const TArray<FString> Diff = QAFishing::DifferentFields(FFishInstance::StaticStruct(), &A, &B);
				if (Diff.Num() > 0)
				{
					AddError(FString::Printf(TEXT("%s seed %d: explicit neutral inputs change %s"), Species, Seed, *FString::Join(Diff, TEXT(", "))));
				}
				Fingerprint = HashCombine(Fingerprint, WaterLocal::FishHash(A));
				++Rolls;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("no-bonus roll fingerprint over %d rolls: %u"), Rolls, Fingerprint));
	if (PinnedFingerprint != 0u)
	{
		TestEqual(TEXT("the no-bonus rolls match the pinned fingerprint (rolls without a hot spot are unchanged)"), Fingerprint, PinnedFingerprint);
	}
	else
	{
		AddWarning(TEXT("QA: the roll fingerprint is not pinned yet"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterRollSizeBonusFormula, "Project.Fishing.Water.QA.Roll.SizeBonusFormula", Flags)
bool FQAWaterRollSizeBonusFormula::RunTest(const FString& Parameters)
{
	// Spec 6: stage 2, natural rolls only: Weight += (WeightMax - Weight) x clamp(SizeBonus, 0, 1). It changes nothing else the
	// roll decides (species, rarity, modifiers, seed); difficulty stats follow the bigger weight; forced weights ignore it;
	// NaN counts 0 (with a warning).
	AddExpectedMessagePlain(TEXT("SizeBonus is not finite"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FGameplayTag Strength = Tag(TEXT("Fish.Stat.Strength"));
	int32 Checked = 0;
	for (const TCHAR* Species : { TEXT("Bonefish"), TEXT("CoralSnapper") })
	{
		const FFishSpeciesRow* Row = Fish.Get().FindSpecies(Species);
		if (!TestNotNull(FString(Species) + TEXT(" row"), Row))
		{
			return false;
		}
		for (int32 Seed = 1; Seed <= 60; ++Seed)
		{
			const FFishRollContext Base = WaterLocal::PlainContext(Species, Seed * 104729);
			FFishInstance Zero;
			FFishRoll::Roll(Fish.Get(), Base, Zero);
			for (const float Bonus : { 0.15f, 0.35f, 0.5f, 1.f })
			{
				FFishRollContext Context = Base;
				Context.SizeBonus = Bonus;
				FFishInstance Big;
				FFishRoll::Roll(Fish.Get(), Context, Big);
				const float Expect = Zero.WeightKg + (Row->WeightMax - Zero.WeightKg) * Bonus;
				if (!FMath::IsNearlyEqual(Big.WeightKg, Expect, 1.0e-3f) || Big.SpeciesId != Zero.SpeciesId || Big.RarityId != Zero.RarityId
					|| Big.ModifierIds != Zero.ModifierIds || Big.Seed != Zero.Seed || Big.GetStat(Strength) + 1.0e-4f < Zero.GetStat(Strength))
				{
					AddError(FString::Printf(TEXT("%s seed %d bonus %.2f: weight %.4f (expected %.4f from %.4f), strength %.3f vs %.3f"), Species, Seed, Bonus,
						Big.WeightKg, Expect, Zero.WeightKg, Big.GetStat(Strength), Zero.GetStat(Strength)));
				}
				++Checked;
			}
			FFishRollContext Over = Base;
			Over.SizeBonus = 2.5f;
			FFishInstance Max;
			FFishRoll::Roll(Fish.Get(), Over, Max);
			TestNearlyEqual(FString::Printf(TEXT("%s seed %d: SizeBonus 2.5 clamps to 1 (WeightMax)"), Species, Seed), Max.WeightKg, Row->WeightMax, 1.0e-3f);
			for (const float Neutralish : { -0.5f, NaN() })
			{
				FFishRollContext Odd = Base;
				Odd.SizeBonus = Neutralish;
				FFishInstance Same;
				FFishRoll::Roll(Fish.Get(), Odd, Same);
				TestEqual(FString::Printf(TEXT("%s seed %d: SizeBonus %f counts 0"), Species, Seed, Neutralish),
					QAFishing::DifferentFields(FFishInstance::StaticStruct(), &Same, &Zero).Num(), 0);
			}
		}
		// Forced weight fraction ignores the bonus (tests, dev commands).
		FFishRollContext Forced = WaterLocal::PlainContext(Species, 5);
		Forced.bForceWeightFraction = true;
		Forced.ForcedWeightFraction = 0.25f;
		FFishInstance A, B;
		FFishRoll::Roll(Fish.Get(), Forced, A);
		Forced.SizeBonus = 0.9f;
		FFishRoll::Roll(Fish.Get(), Forced, B);
		TestEqual(FString(Species) + TEXT(": a forced weight ignores SizeBonus"), B.WeightKg, A.WeightKg);
		// Natural rarity/modifiers: the bonus never changes what else was rolled, and never makes a fish lighter.
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			FFishRollContext Natural = WaterLocal::RollContext(Species, Seed * 31337, 1.f);
			FFishInstance Plain, Bigger;
			FFishRoll::Roll(Fish.Get(), Natural, Plain);
			Natural.SizeBonus = 0.35f;
			FFishRoll::Roll(Fish.Get(), Natural, Bigger);
			if (Bigger.RarityId != Plain.RarityId || Bigger.ModifierIds != Plain.ModifierIds || Bigger.WeightKg + 1.0e-4f < Plain.WeightKg)
			{
				AddError(FString::Printf(TEXT("%s seed %d natural: rarity %s->%s, modifiers %d->%d, weight %.3f->%.3f"), Species, Seed, *Plain.RarityId.ToString(),
					*Bigger.RarityId.ToString(), Plain.ModifierIds.Num(), Bigger.ModifierIds.Num(), Plain.WeightKg, Bigger.WeightKg));
			}
		}
	}
	TestTrue(TEXT("checked"), Checked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterRollValueMultiplierFormula, "Project.Fishing.Water.QA.Roll.ValueMultiplierFormula", Flags)
bool FQAWaterRollValueMultiplierFormula::RunTest(const FString& Parameters)
{
	// Spec 6: stage 6: value = round(BaseValuePerKg x weight x rarity x modifiers x ValueMultiplier), >= 1; nothing else changes;
	// <= 0 or non-finite is ignored (with a warning).
	AddExpectedMessagePlain(TEXT("ValueMultiplier"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	for (const TCHAR* Species : { TEXT("Bonefish"), TEXT("CoralSnapper") })
	{
		const FFishSpeciesRow* Row = Fish.Get().FindSpecies(Species);
		if (!TestNotNull(FString(Species) + TEXT(" row"), Row))
		{
			return false;
		}
		for (int32 Seed = 1; Seed <= 60; ++Seed)
		{
			const FFishRollContext Base = WaterLocal::PlainContext(Species, Seed * 7727);
			FFishInstance One;
			FFishRoll::Roll(Fish.Get(), Base, One);
			for (const float Multiplier : { 1.25f, 2.f, 0.5f, 10.f })
			{
				FFishRollContext Context = Base;
				Context.ValueMultiplier = Multiplier;
				FFishInstance More;
				FFishRoll::Roll(Fish.Get(), Context, More);
				const double Raw = static_cast<double>(Row->BaseValuePerKg) * More.WeightKg * Multiplier; // Common (x1), no modifiers
				const int32 Expect = FMath::Max(1, static_cast<int32>(FMath::FloorToDouble(Raw + 0.5)));
				if (FMath::Abs(More.Value - Expect) > 1 || More.WeightKg != One.WeightKg || More.RarityId != One.RarityId || More.Xp != One.Xp)
				{
					AddError(FString::Printf(TEXT("%s seed %d x%.2f: value %d (expected %d), weight %.4f vs %.4f"), Species, Seed, Multiplier, More.Value, Expect,
						More.WeightKg, One.WeightKg));
				}
			}
			for (const float Ignored : { 0.f, -2.f, NaN(), Inf() })
			{
				FFishRollContext Context = Base;
				Context.ValueMultiplier = Ignored;
				FFishInstance Same;
				FFishRoll::Roll(Fish.Get(), Context, Same);
				TestEqual(FString::Printf(TEXT("%s seed %d: ValueMultiplier %f is ignored"), Species, Seed, Ignored), Same.Value, One.Value);
			}
		}
	}
	return true;
}

// =====================================================================================================================
// In a world
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterWorldShallowNeverBites, "Project.Fishing.Water.QA.World.ShallowWaterNeverBitesAndSaysSo", Flags)
bool FQAWaterWorldShallowNeverBites::RunTest(const FString& Parameters)
{
	// Spec 3: nothing bites where the water under the bobber is shallower than 15 cm, and the HUD says so at once (not after the
	// hint delay); 16 cm bites. The seabed is the only thing that changes between the cases.
	FLureFishingRow Shipped;
	if (!QAFishing::ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const float Depth : { 10.f, 14.f, 16.f })
	{
		const FString Label = FString::Printf(TEXT("%.0f cm deep"), Depth);
		QAFishing::FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		WaterLocal::AddSeabed(Scene, -Depth);
		FLureFishingRow Profile = QAFishing::FlowProfile(Shipped, 2.f);
		Profile.NoBiteHintDelay = 5.f;
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), Profile);
		if (!Fishing || !WaterLocal::CastAndWait(*this, Scene, Fishing))
		{
			return false;
		}
		TestNearlyEqual(Label + TEXT(": the server measured the depth"), Fishing->GetWaterContext().DepthCm, Depth, 1.f);
		const bool bShallow = Depth < 15.f;
		TestEqual(Label + TEXT(": nothing-here flag"), Fishing->GetNetState().bNoFishHere, bShallow);
		TestEqual(Label + TEXT(": replicated reason"), FString(ReasonName(Fishing->GetNetState().Water.NoBiteReason)),
			FString(bShallow ? TEXT("TooShallow") : TEXT("None")));
		Scene.Tick(2);
		TestEqual(Label + TEXT(": HUD says too shallow at once (before the 5 s hint delay)"), Fishing->GetStatusText().Contains(TEXT("Too shallow")), bShallow);
		const bool bBit = Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 60 * 20);
		TestEqual(Label + TEXT(": a bite within 20 s"), bBit, !bShallow);
		if (bShallow)
		{
			TestTrue(Label + TEXT(": no bite ever scheduled"), Fishing->GetScheduledBiteTime() < 0.0);
			TestEqual(Label + TEXT(": no nibbles"), static_cast<int32>(Fishing->GetNetState().NibbleId), 0);
			TestFalse(Label + TEXT(": no fish rolled"), Fishing->GetPendingFish().IsValid());
			TestTrue(Label + TEXT(": still says too shallow 20 s later"), Fishing->GetStatusText().Contains(TEXT("Too shallow")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterWorldOpenWaterEverywhere, "Project.Fishing.Water.QA.World.OpenWaterBitesEverywhere", Flags)
bool FQAWaterWorldOpenWaterEverywhere::RunTest(const FString& Parameters)
{
	// Acceptance: every body of water can be fished. A level with no areas and no spots: casts in 5 directions and 3 distances all
	// land in default water (the default habitat), are not flagged, say "Water: open water" and get a bite.
	FLureFishingRow Shipped;
	if (!QAFishing::ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	QAFishing::FScene Scene;
	if (!Scene.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Player = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Player, QAFishing::FlowProfile(Shipped, 0.5f), 12.f, 4321);
	if (!Fishing)
	{
		return false;
	}
	const FGameplayTag DefaultHabitat = GetDefault<ULureWaterSettings>()->GetDefaultWaterHabitatTag();
	int32 Casts = 0;
	for (const float Aim : { -30.f, -15.f, 0.f, 15.f, 30.f })
	{
		for (const float Charge : { 0.2f, 0.6f, 1.f })
		{
			const FString Label = FString::Printf(TEXT("aim %.0f, charge %.1f"), Aim, Charge);
			if (!TestTrue(Label + TEXT(": cast"), Fishing->AuthorityCast(Charge, Aim))
				|| !TestTrue(Label + TEXT(": lands"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 300)))
			{
				return false;
			}
			TestTrue(Label + TEXT(": on water"), Fishing->GetNetState().bOnWater);
			TestFalse(Label + TEXT(": not flagged"), Fishing->GetNetState().bNoFishHere);
			TestTrue(Label + TEXT(": default water"), Fishing->GetWaterContext().Source == ELureWaterSource::Default && Fishing->GetWaterContext().HabitatTag == DefaultHabitat);
			TestTrue(Label + TEXT(": HUD 'Water: open water'"), Fishing->GetStatusText().Contains(TEXT("Water: open water")));
			TestTrue(Label + TEXT(": a bite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120));
			Fishing->AuthorityReelIn();
			Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 300);
			++Casts;
		}
	}
	TestEqual(TEXT("15 casts"), Casts, 15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterWorldLegacySpots, "Project.Fishing.Water.QA.World.LegacySpotsOnlyWithoutAreas", Flags)
bool FQAWaterWorldLegacySpots::RunTest(const FString& Parameters)
{
	// Spec 2 "Legacy fishing spots": a level with no water area reads its fishing_spot markers as circle areas (habitat, region,
	// luck); as soon as one water area exists (anywhere) the markers are ignored for fishing; the switch turns it off.
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1); // default water at 20:00 (T-009)
	FLureFishingRow Shipped;
	if (!QAFishing::ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	QAFishing::FScene Scene;
	if (!Scene.Create(*this))
	{
		return false;
	}
	Scene.AddSpot(QAFishing::SpotCenter, { TEXT("Spot=qa_legacy"), TEXT("Habitat=Habitat.Reef"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200"), TEXT("Luck=0.5") });
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), QAFishing::FlowProfile(Shipped, 0.5f), 20.f);
	if (!Fishing)
	{
		return false;
	}
	bool bLegacy = false;
	TArray<FLureWaterAreaInfo> Areas = FLureWaterQuery::GatherAreas(Scene.World, &bLegacy);
	TestTrue(TEXT("no areas: the marker is a legacy area"), bLegacy && Areas.Num() == 1 && Areas[0].AreaId == TEXT("qa_legacy") && Areas[0].Source == ELureWaterSource::LegacySpot);
	if (!WaterLocal::CastAndWait(*this, Scene, Fishing))
	{
		return false;
	}
	TestEqual(TEXT("the bobber is in the legacy area"), Fishing->GetNetState().SpotId, FName(TEXT("qa_legacy")));
	TestTrue(TEXT("... with its habitat"), Fishing->GetWaterContext().HabitatTag == Tag(TEXT("Habitat.Reef")) && Fishing->GetWaterContext().Source == ELureWaterSource::LegacySpot);
	TestTrue(TEXT("a bite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120));
	TestTrue(TEXT("... rolled with the spot's habitat and luck"), Fishing->GetLastRollContext().HabitatTag == Tag(TEXT("Habitat.Reef"))
		&& FMath::IsNearlyEqual(Fishing->GetLastRollContext().Luck, 0.5f, 1.0e-4f));
	Fishing->AuthorityReelIn();
	Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 300);

	// One water area far away (100 m): the marker no longer counts.
	ALureWaterArea* Far = WaterLocal::SpawnArea(Scene.World, TEXT("qa_far_lagoon"), TEXT("Habitat.Lagoon"), FVector(10000.f, 10000.f, 0.f), 0.f, 0);
	if (!TestNotNull(TEXT("an area actor"), Far))
	{
		return false;
	}
	Far->SetShapeCircle(500.f);
	Areas = FLureWaterQuery::GatherAreas(Scene.World, &bLegacy);
	TestTrue(TEXT("with an area: only the area, no legacy"), !bLegacy && Areas.Num() == 1 && Areas[0].AreaId == TEXT("qa_far_lagoon"));
	if (!WaterLocal::CastAndWait(*this, Scene, Fishing))
	{
		return false;
	}
	TestTrue(TEXT("the same cast now lands in default water"), Fishing->GetNetState().SpotId.IsNone() && Fishing->GetWaterContext().Source == ELureWaterSource::Default);
	TestTrue(TEXT("... and still bites"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120));
	Fishing->AuthorityReelIn();
	Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 300);
	Far->Destroy();
	Scene.Tick(2);

	// The switch off: markers are never areas.
	{
		const WaterLocal::TScopedWater<bool> Off(&ULureWaterSettings::bLegacySpotsWhenNoAreas, false);
		Areas = FLureWaterQuery::GatherAreas(Scene.World, &bLegacy);
		TestTrue(TEXT("switch off: no areas at all"), !bLegacy && Areas.Num() == 0);
		if (!WaterLocal::CastAndWait(*this, Scene, Fishing))
		{
			return false;
		}
		TestTrue(TEXT("switch off: default water at the spot"), Fishing->GetNetState().SpotId.IsNone() && !Fishing->GetNetState().bNoFishHere);
		Fishing->AuthorityReelIn();
		Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 300);
	}
	// LegacySpotPriority is data: a legacy area carries it.
	{
		const WaterLocal::TScopedWater<int32> Priority(&ULureWaterSettings::LegacySpotPriority, 7);
		Areas = FLureWaterQuery::GatherAreas(Scene.World, &bLegacy);
		TestTrue(TEXT("LegacySpotPriority reaches the legacy area"), bLegacy && Areas.Num() == 1 && Areas[0].Priority == 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterWorldAreaActorTransform, "Project.Fishing.Water.QA.World.AreaActorYawAndScale", Flags)
bool FQAWaterWorldAreaActorTransform::RunTest(const FString& Parameters)
{
	// Spec 2: shapes are 2D columns; actor scale is ignored; boxes turn with the actor's yaw; polygons are given in world X/Y
	// (SetShapePolygon, after the actor is placed and turned) and come back unchanged in world space.
	QAFishing::FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	ALureWaterArea* Circle = WaterLocal::SpawnArea(Scene.World, TEXT("qa_circle"), TEXT("Habitat.Reef"), FVector(1000.f, 0.f, 0.f), 0.f, 0);
	ALureWaterArea* Box = WaterLocal::SpawnArea(Scene.World, TEXT("qa_box"), TEXT("Habitat.Lagoon"), FVector(-3000.f, 2000.f, 0.f), 90.f, 0);
	ALureWaterArea* Poly = WaterLocal::SpawnArea(Scene.World, TEXT("qa_poly"), TEXT("Habitat.Shore"), FVector(5000.f, 5000.f, 0.f), 30.f, 0);
	if (!Circle || !Box || !Poly)
	{
		AddError(TEXT("area actors did not spawn"));
		return false;
	}
	Circle->SetShapeCircle(500.f);
	Circle->SetActorScale3D(FVector(4.f, 4.f, 4.f));
	TestTrue(TEXT("circle: scale 4 is ignored (radius 500)"), Circle->GetWaterArea().Contains(FVector2D(1490.0, 0.0)) && !Circle->GetWaterArea().Contains(FVector2D(1600.0, 0.0)));
	Box->SetShapeBox(FVector2D(1000.0, 100.0));
	TestTrue(TEXT("box turned 90 by the actor: long along Y"), Box->GetWaterArea().Contains(FVector2D(-3000.0, 2900.0)) && !Box->GetWaterArea().Contains(FVector2D(-2100.0, 2000.0)));
	const TArray<FVector2D> World = { FVector2D(4000.0, 4000.0), FVector2D(6000.0, 4000.0), FVector2D(6000.0, 6000.0), FVector2D(5000.0, 6500.0), FVector2D(4000.0, 6000.0) };
	TestEqual(TEXT("SetShapePolygon keeps 5 points"), Poly->SetShapePolygon(World), 5);
	const FLureWaterAreaInfo Info = Poly->GetWaterArea();
	TestEqual(TEXT("polygon comes back with 5 points"), Info.Polygon.Num(), 5);
	for (int32 Index = 0; Index < FMath::Min(5, Info.Polygon.Num()); ++Index)
	{
		TestTrue(FString::Printf(TEXT("polygon point %d back in world space"), Index), Info.Polygon[Index].Equals(World[Index], 0.5));
	}
	TestTrue(TEXT("polygon: the roof point (5000, 6300) inside, (4100, 6400) outside"), Info.Contains(FVector2D(5000.0, 6300.0)) && !Info.Contains(FVector2D(4100.0, 6400.0)));
	Poly->SetActorScale3D(FVector(2.f, 2.f, 2.f));
	TestFalse(TEXT("polygon: scale is ignored"), Poly->GetWaterArea().Contains(FVector2D(7000.0, 5000.0)));
	TestTrue(TEXT("habitats set by name"), Circle->GetWaterArea().HabitatTag == Tag(TEXT("Habitat.Reef")) && Box->GetWaterArea().HabitatTag == Tag(TEXT("Habitat.Lagoon")));
	AddExpectedMessagePlain(TEXT("Habitat.QA_Nope"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	TestFalse(TEXT("SetAreaTags refuses an unregistered habitat"), Circle->SetAreaTags(TEXT("Habitat.QA_Nope"), NAME_None));
	return true;
}

// =====================================================================================================================
// Data validation
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterDataHotSpotRows, "Project.Fishing.Water.QA.Data.HotSpotRowsValid", Flags)
bool FQAWaterDataHotSpotRows::RunTest(const FString& Parameters)
{
	// Every DT_HotSpot row (data/tables/DT_HotSpot.json) is valid and does what a hot spot is for (GAME_DESIGN "Hot spots": better
	// odds - rarer, bigger or more valuable fish; appear in valid water, last a few minutes and move): unique names, no misspelled
	// columns (the importer silently drops unknown keys), sane numbers, registered habitats, a real boost and never a penalty, and
	// the struct defaults equal the shipped Bubbles row (the built-in fallback).
	FString Json;
	if (!LoadProjectText(*this, TEXT("data/tables/DT_HotSpot.json"), Json))
	{
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> Raw;
	if (!TestTrue(TEXT("DT_HotSpot.json is a JSON array"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Raw)))
	{
		return false;
	}
	TSet<FString> Names;
	for (const TSharedPtr<FJsonValue>& Value : Raw)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		const FString Name = Object.IsValid() ? Object->GetStringField(TEXT("Name")) : FString();
		bool bDuplicate = false;
		Names.Add(Name, &bDuplicate);
		TestFalse(TEXT("unique row name ") + Name, bDuplicate || Name.IsEmpty());
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			TestTrue(FString::Printf(TEXT("%s: column '%s' is a FLureHotSpotRow property"), *Name, *Field.Key),
				Field.Key == TEXT("Name") || FLureHotSpotRow::StaticStruct()->FindPropertyByName(FName(*Field.Key)) != nullptr);
		}
	}
	TStrongObjectPtr<UDataTable> Table;
	if (!LoadHotSpotTable(*this, Table))
	{
		return false;
	}
	TestTrue(TEXT("at least one hot spot type"), Table->GetRowMap().Num() > 0);
	TestEqual(TEXT("every JSON row imported"), Table->GetRowMap().Num(), Raw.Num());
	const float MinBiteDepth = GetDefault<ULureWaterSettings>()->MinBiteDepth;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FLureHotSpotRow& Row = *reinterpret_cast<const FLureHotSpotRow*>(Pair.Value);
		const FString Id = Pair.Key.ToString();
		const TArray<FString> Problems = Row.Validate(Pair.Key);
		TestEqual(FString::Printf(TEXT("%s: Validate finds nothing (%s)"), *Id, *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
		TestFalse(Id + TEXT(": DisplayName and HudText set"), Row.DisplayName.IsEmpty() || Row.HudText.IsEmpty());
		TestTrue(Id + TEXT(": SpawnInterval finite and > 0 (0 would never spawn)"), FMath::IsFinite(Row.SpawnInterval) && Row.SpawnInterval > 0.f);
		TestTrue(Id + TEXT(": MaxPerArea >= 1"), Row.MaxPerArea >= 1);
		TestTrue(Id + TEXT(": Radius > 0 and at most MinSpacing"), Row.Radius > 0.f && Row.Radius <= Row.MinSpacing);
		TestTrue(Id + TEXT(": lasts a few minutes: 1 <= LifetimeMin <= LifetimeMax <= 30 min"), Row.LifetimeMin >= 1.f && Row.LifetimeMin <= Row.LifetimeMax && Row.LifetimeMax <= 1800.f);
		TestTrue(Id + TEXT(": it moves: DriftSpeed > 0 and DriftRange > 0"), Row.DriftSpeed > 0.f && Row.DriftRange > 0.f);
		TestTrue(Id + TEXT(": MinDepth >= MinBiteDepth (a hot spot never sits where nothing bites)"), Row.MinDepth >= MinBiteDepth);
		TestTrue(Id + TEXT(": MaxDepth 0 or above MinDepth"), Row.MaxDepth == 0.f || Row.MaxDepth > Row.MinDepth);
		for (const FGameplayTag& Habitat : Row.AllowedHabitats)
		{
			TestTrue(FString::Printf(TEXT("%s: allowed habitat %s is a registered Habitat.* tag"), *Id, *Habitat.ToString()),
				Habitat.IsValid() && Habitat.MatchesTag(Tag(TEXT("Habitat"))));
		}
		TestTrue(Id + TEXT(": never a penalty (luck >= 0, size in [0,1], value >= 1, bite wait in (0,1])"), Row.LuckBonus >= 0.f && Row.SizeBonus >= 0.f && Row.SizeBonus <= 1.f
			&& Row.ValueMultiplier >= 1.f && Row.BiteWaitScale > 0.f && Row.BiteWaitScale <= 1.f);
		TestTrue(Id + TEXT(": a real boost (rarer, bigger or more valuable)"), Row.LuckBonus > 0.f || Row.SizeBonus > 0.f || Row.ValueMultiplier > 1.f);
		TestTrue(Id + TEXT(": VisualClass is None or a project class path"), Row.VisualClass.IsNull() || Row.VisualClass.ToString().StartsWith(TEXT("/Game/"))
			|| Row.VisualClass.ToString().StartsWith(TEXT("/Script/")));
	}
	// The struct defaults are the built-in fallback and must equal the shipped Bubbles row (spec 5).
	const FLureHotSpotRow* Bubbles = Table->FindRow<FLureHotSpotRow>(FLureWaterRules::FallbackHotSpotType(), TEXT("QA"));
	if (TestNotNull(TEXT("the fallback type is a shipped row"), Bubbles))
	{
		const FLureHotSpotRow Defaults;
		TArray<FString> Diff = QAFishing::DifferentFields(FLureHotSpotRow::StaticStruct(), &Defaults, Bubbles);
		Diff.Remove(TEXT("DevComment"));
		TestEqual(FString::Printf(TEXT("FLureHotSpotRow defaults == the Bubbles row (differs: %s)"), *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
	}
	TestTrue(TEXT("the settings point at /Game/Data/DT_HotSpot"), GetDefault<ULureWaterSettings>()->HotSpotTable.ToSoftObjectPath().GetAssetPathString().EndsWith(TEXT("/Game/Data/DT_HotSpot.DT_HotSpot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterDataHotSpotValidateCatchesBadRows, "Project.Fishing.Water.QA.Data.HotSpotValidateCatchesBadRows", Flags)
bool FQAWaterDataHotSpotValidateCatchesBadRows::RunTest(const FString& Parameters)
{
	// FLureHotSpotRow::Validate (the data rule behind the validation test) reports each kind of broken row; AllowsWater is
	// hierarchical and honours the depth band.
	const FLureHotSpotRow Good;
	TestEqual(TEXT("the default row is valid"), Good.Validate(TEXT("QA_Good")).Num(), 0);
	struct FBreak { const TCHAR* What; TFunction<void(FLureHotSpotRow&)> Apply; };
	const FBreak Breaks[] = {
		{ TEXT("Radius 0"), [](FLureHotSpotRow& R) { R.Radius = 0.f; } },
		{ TEXT("LifetimeMin > LifetimeMax"), [](FLureHotSpotRow& R) { R.LifetimeMin = 300.f; R.LifetimeMax = 100.f; } },
		{ TEXT("negative SpawnInterval"), [](FLureHotSpotRow& R) { R.SpawnInterval = -1.f; } },
		{ TEXT("NaN SpawnInterval"), [](FLureHotSpotRow& R) { R.SpawnInterval = NaN(); } },
		{ TEXT("SizeBonus 1.5"), [](FLureHotSpotRow& R) { R.SizeBonus = 1.5f; } },
		{ TEXT("negative LuckBonus"), [](FLureHotSpotRow& R) { R.LuckBonus = -1.f; } },
		{ TEXT("ValueMultiplier 0"), [](FLureHotSpotRow& R) { R.ValueMultiplier = 0.f; } },
		{ TEXT("BiteWaitScale 0"), [](FLureHotSpotRow& R) { R.BiteWaitScale = 0.f; } },
		{ TEXT("MaxDepth below MinDepth"), [](FLureHotSpotRow& R) { R.MinDepth = 200.f; R.MaxDepth = 100.f; } },
		{ TEXT("an invalid habitat"), [](FLureHotSpotRow& R) { R.AllowedHabitats = { FGameplayTag() }; } },
		{ TEXT("a non-habitat tag"), [](FLureHotSpotRow& R) { R.AllowedHabitats = { Tag(TEXT("Region.Tropical")) }; } },
		{ TEXT("negative MaxPerArea"), [](FLureHotSpotRow& R) { R.MaxPerArea = -1; } },
		{ TEXT("negative DriftRange"), [](FLureHotSpotRow& R) { R.DriftRange = -5.f; } },
	};
	for (const FBreak& Break : Breaks)
	{
		FLureHotSpotRow Row;
		Break.Apply(Row);
		TestTrue(FString::Printf(TEXT("Validate reports: %s"), Break.What), Row.Validate(TEXT("QA_Bad")).Num() > 0);
	}
	FLureHotSpotRow Reef;
	Reef.AllowedHabitats = { Tag(TEXT("Habitat.Reef")) };
	Reef.MinDepth = 90.f;
	Reef.MaxDepth = 400.f;
	TestTrue(TEXT("Reef allows Reef.Edge (hierarchical)"), Reef.AllowsWater(Tag(TEXT("Habitat.Reef.Edge")), 100.f));
	TestFalse(TEXT("Reef does not allow Shore"), Reef.AllowsWater(Tag(TEXT("Habitat.Shore")), 100.f));
	TestFalse(TEXT("89 cm is under MinDepth"), Reef.AllowsWater(Tag(TEXT("Habitat.Reef")), 89.f));
	TestTrue(TEXT("90 cm is in"), Reef.AllowsWater(Tag(TEXT("Habitat.Reef")), 90.f));
	TestFalse(TEXT("400 cm is at MaxDepth (exclusive)"), Reef.AllowsWater(Tag(TEXT("Habitat.Reef")), 400.f));
	FLureHotSpotRow Edge;
	Edge.AllowedHabitats = { Tag(TEXT("Habitat.Reef.Edge")) };
	TestFalse(TEXT("Reef.Edge does not allow its parent Reef"), Edge.AllowsWater(Tag(TEXT("Habitat.Reef")), 500.f));
	TestTrue(TEXT("an empty list allows any water"), Good.AllowsWater(Tag(TEXT("Habitat.DeepDrop")), 500.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAWaterDataLayoutSchema, "Project.Fishing.Water.QA.Data.LayoutWaterAreasFollowSchema", Flags)
bool FQAWaterDataLayoutSchema::RunTest(const FString& Parameters)
{
	// Every water_area and hot_spots marker in data/levels/*.json follows the schema of fishing-water-rules.md section 9 (checked
	// here independently of layout.py): known shape, registered Habitat.*/Region.* tags, radius/size > 0, polygons with >= 3
	// points, no zero-length edge and no self-crossing, depth [min >= 0, max 0 or > min], luck >= 0, unique ids; the game sees a
	// usable outline; hot_spots types are DT_HotSpot rows; one spawner per level; and no habitat a level uses is ever dead.
	// The checker itself is proven on the spec's own examples (all pass) and on broken markers (each fails).
	AddExpectedMessagePlain(TEXT("Data gap:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	TStrongObjectPtr<UDataTable> HotSpots;
	if (!LoadHotSpotTable(*this, HotSpots))
	{
		return false;
	}
	TSet<FName> Types;
	for (const TPair<FName, uint8*>& Pair : HotSpots->GetRowMap())
	{
		Types.Add(Pair.Key);
	}
	const FString SpecExamples = TEXT(R"({"markers": [
		{"type": "water_area", "id": "reef_flats", "name": "Reef Flats", "habitat": "Habitat.Reef", "region": "Region.Tropical.PalmKey", "priority": 10, "luck": 0.0, "depth": [0, 0], "shape": "circle", "at": [-1500, 5400, 0], "radius": 900},
		{"type": "water_area", "id": "lagoon", "name": "Lagoon", "habitat": "Habitat.Lagoon", "priority": 5, "shape": "polygon", "points": [[-2500, -3000], [1500, -3200], [1800, -6500], [-2800, -6200]]},
		{"type": "water_area", "id": "dock_shelf", "name": "Dock Shelf", "habitat": "Habitat.Shore", "priority": 5, "shape": "box", "at": [-6800, 600, 0], "size": [2400, 1600], "yaw": 0},
		{"type": "water_area", "id": "sea_shallows", "name": "Shallows", "habitat": "Habitat.Shore", "priority": -100, "shape": "everywhere", "depth": [0, 300]},
		{"type": "water_area", "id": "sea_deep", "name": "Deep water", "habitat": "Habitat.DeepDrop", "priority": -100, "shape": "everywhere", "depth": [300, 0]},
		{"type": "hot_spots", "id": "hot_spots", "at": [0, 0, 0], "types": ["Bubbles", "Ripples"], "max": 12, "seed": 0}]})");
	TestEqual(FString::Printf(TEXT("the spec's section 9 examples pass the checker (%s)"), *FString::Join(WaterLocal::CheckLayoutWater(WaterLocal::ParseObject(SpecExamples), Types), TEXT("; "))),
		WaterLocal::CheckLayoutWater(WaterLocal::ParseObject(SpecExamples), Types).Num(), 0);
	const TCHAR* BadMarkers[] = {
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "blob", "at": [0, 0, 0]})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.QA_Nope", "shape": "circle", "at": [0, 0, 0], "radius": 100})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Reef", "shape": "circle", "at": [0, 0, 0], "radius": 100})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "region": "Tropical", "shape": "circle", "at": [0, 0, 0], "radius": 100})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "circle", "at": [0, 0, 0], "radius": 0})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "box", "at": [0, 0, 0], "size": [100, 0]})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "polygon", "points": [[0, 0], [100, 0]]})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "polygon", "points": [[0, 0], [100, 0], [100, 0], [0, 100]]})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "polygon", "points": [[0, 0], [100, 100], [100, 0], [0, 100]]})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "everywhere", "depth": [300, 100]})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "everywhere", "depth": [-10, 0]})"),
		TEXT(R"({"type": "water_area", "id": "a", "habitat": "Habitat.Reef", "shape": "everywhere", "luck": -0.5})"),
		TEXT(R"({"type": "hot_spots", "id": "h", "types": ["Bubbles", "QA_NoSuchType"]})"),
	};
	for (const TCHAR* Bad : BadMarkers)
	{
		const TSharedPtr<FJsonObject> Root = WaterLocal::ParseObject(FString(TEXT("{\"markers\": [")) + Bad + TEXT("]}"));
		TestTrue(FString::Printf(TEXT("the checker rejects %s"), Bad), Root.IsValid() && WaterLocal::CheckLayoutWater(Root, Types).Num() > 0);
	}
	const TSharedPtr<FJsonObject> Twice = WaterLocal::ParseObject(TEXT(R"({"markers": [
		{"type": "water_area", "id": "same", "habitat": "Habitat.Reef", "shape": "everywhere"}, {"type": "water_area", "id": "same", "habitat": "Habitat.Shore", "shape": "everywhere"}]})"));
	TestTrue(TEXT("the checker rejects a duplicate id"), WaterLocal::CheckLayoutWater(Twice, Types).Num() > 0);

	// The real layouts.
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureBiteRules Shipped = ShippedRules();
	int32 AreaCount = 0;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Layout : LoadLayouts(*this))
	{
		TArray<FLureWaterAreaInfo> Areas;
		const TArray<FString> Problems = WaterLocal::CheckLayoutWater(Layout.Value, Types, &Areas);
		TestEqual(FString::Printf(TEXT("%s: water markers follow the schema (%s)"), *Layout.Key, *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
		AreaCount += Areas.Num();
		TSet<FGameplayTag> Used;
		for (const FLureWaterAreaInfo& Area : Areas)
		{
			Used.Add(Area.HabitatTag);
		}
		if (Areas.Num() > 0 || MarkersOfType(Layout.Value, TEXT("hot_spots")).Num() > 0)
		{
			Used.Add(GetDefault<ULureWaterSettings>()->GetDefaultWaterHabitatTag());
		}
		for (const FGameplayTag& Habitat : Used)
		{
			if (Habitat.IsValid())
			{
				const TArray<FString> Dead = FLureWaterRules::FindGapHours(Fish.Get(), Habitat, Tag(TEXT("Region.Tropical.PalmKey")), Shipped.GapFallbackHabitats);
				TestEqual(FString::Printf(TEXT("%s: %s is never dead (%s)"), *Layout.Key, *Habitat.ToString(), *FString::Join(Dead, TEXT(", "))), Dead.Num(), 0);
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d water areas, %d hot_spots markers"), *Layout.Key, Areas.Num(), MarkersOfType(Layout.Value, TEXT("hot_spots")).Num()));
	}
	AddInfo(FString::Printf(TEXT("%d water areas in all layouts"), AreaCount));
	return true;
}

} // namespace LureWaterQA

#endif // WITH_DEV_AUTOMATION_TESTS

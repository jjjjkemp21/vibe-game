// Lure T-006 QA (qa-engineer): fishing spots - marker tags, lookup, and what reaches the fish roll.
// Project.Fishing.QA.Spot.* - spec: docs/specs/fishing-rules.md "Spots" + lead decision 1; marker contract: docs/levels/L_PalmKey.md s11
// and Content/Python/levels/build_level.py marker_tags(); contract: Fishing/FishingSpots.h.
// T-027 (unreal-engineer, Jimmy's playtest redesign): every body of water can be fished; spot markers are legacy water areas
// (docs/specs/fishing-water-rules.md). The "no spot = no bite" tests became OpenWaterBitesWithoutSpots and
// DefaultWaterHabitatDecidesOpenWater; the "nothing fits" tests run without the gap fallback (a data gap).

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/FishingWater.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Fishing/LureWaterSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <limits>

// Everything lives in namespace QAFishing (unity builds merge test files; other files use global using-directives).
namespace QAFishing
{

namespace SpotLocal
{
	const FName SpotTag(TEXT("Lure.FishingSpot"));

	TArray<FName> SpotTagList(std::initializer_list<const TCHAR*> Texts)
	{
		TArray<FName> Out;
		for (const TCHAR* Text : Texts)
		{
			Out.Add(FName(Text));
		}
		return Out;
	}

	/** A reef spot (the snapper bites 15-09) for "nothing fits at noon" cases. */
	TArray<FString> ReefSpotTags()
	{
		return { TEXT("Spot=qa_reef"), TEXT("Habitat=Habitat.Reef"), TEXT("Region=Region.Tropical.PalmKey"), FString::Printf(TEXT("Radius=%.0f"), SpotRadius) };
	}

	/** Temporarily changes the water settings' gap fallback habitats (restored when the scope ends). T-027. */
	struct FScopedGapFallbacks
	{
		TArray<FName> Saved;
		explicit FScopedGapFallbacks(const TArray<FName>& Value)
		{
			ULureWaterSettings* Settings = GetMutableDefault<ULureWaterSettings>();
			Saved = Settings->GapFallbackHabitats;
			Settings->GapFallbackHabitats = Value;
		}
		~FScopedGapFallbacks()
		{
			GetMutableDefault<ULureWaterSettings>()->GapFallbackHabitats = Saved;
		}
	};

	/** Temporarily changes the default water habitat (restored when the scope ends). T-027. */
	struct FScopedDefaultWaterHabitat
	{
		FName Saved;
		explicit FScopedDefaultWaterHabitat(FName Value)
		{
			ULureWaterSettings* Settings = GetMutableDefault<ULureWaterSettings>();
			Saved = Settings->DefaultWaterHabitat;
			Settings->DefaultWaterHabitat = Value;
		}
		~FScopedDefaultWaterHabitat()
		{
			GetMutableDefault<ULureWaterSettings>()->DefaultWaterHabitat = Saved;
		}
	};

	/** The bite context of a spot read as a legacy water area, at XY (T-027: MakeWaterContext + MakeBiteContext). */
	FFishRollContext SpotContext(const FLureFishingSpot& Spot, const FLureFishingEnvironment& Environment, int32 Seed, const FVector2D& XY)
	{
		const TArray<FLureWaterAreaInfo> Areas = { FLureWaterRules::AreaFromLegacySpot(Spot, 0) };
		const FLureWaterContext Water = FLureWaterRules::MakeWaterContext(Areas, XY, 0.f, 800.f, Tag(TEXT("Habitat.Shore")));
		return FLureWaterRules::MakeBiteContext(Water, Water.HabitatTag, FLureHotSpotBonus(), Environment, Seed);
	}

	bool CastAndLand(FAutomationTestBase& Test, FScene& Scene, ULureFishingComponent* Fishing, float Charge = 0.5f)
	{
		if (!Test.TestTrue(TEXT("cast"), Fishing->AuthorityCast(Charge, 0.f)))
		{
			return false;
		}
		return Test.TestTrue(TEXT("lands"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 180));
	}
}

using namespace SpotLocal;

// =====================================================================================================================
// Marker tags
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotParseRadiusRules, "Project.Fishing.QA.Spot.ParseRadiusRules", QAFishing::Flags)
bool FQAFishSpotParseRadiusRules::RunTest(const FString& Parameters)
{
	// Contract: a marker needs the spot tag and Radius > 0 to be a spot; a missing or unusable Radius is reported.
	struct FCase
	{
		const TCHAR* RadiusTag;
		bool bSpot;
		float Radius;
	};
	const FCase Cases[] = {
		{ TEXT("Radius=600"), true, 600.f }, { TEXT("Radius=0.5"), true, 0.5f }, { TEXT("Radius= 450 "), true, 450.f }, { TEXT(" Radius =300"), true, 300.f },
		{ TEXT("radius=250"), true, 250.f }, { TEXT("RADIUS=125.25"), true, 125.25f }, { TEXT("Radius=+300"), true, 300.f },
		{ TEXT("Radius=0"), false, 0.f }, { TEXT("Radius=-300"), false, 0.f }, { TEXT("Radius="), false, 0.f }, { TEXT("Radius=abc"), false, 0.f },
		{ TEXT("Radius=300cm"), false, 0.f }, { TEXT("Radius=3,00"), false, 0.f }, { TEXT("Radius=nan"), false, 0.f }, { TEXT("Radius=inf"), false, 0.f },
		{ TEXT("Radius=1e3"), false, 0.f }, { TEXT("Radius=99999999999999999999999999999999999999999999"), false, 0.f }, { TEXT("Radius=-"), false, 0.f },
		{ TEXT("Radius=."), false, 0.f }, { TEXT("Radius"), false, 0.f }, { TEXT("=300"), false, 0.f }, { TEXT("Radiuss=300"), false, 0.f },
	};
	for (const FCase& Case : Cases)
	{
		FLureFishingSpot Spot;
		TArray<FString> Problems;
		const bool bSpot = FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("Lure.FishingSpot"), TEXT("Spot=qa"), Case.RadiusTag }), SpotTag, Spot, &Problems);
		const FString Label = FString::Printf(TEXT("'%s'"), Case.RadiusTag);
		TestEqual(Label + TEXT(": is a spot"), bSpot, Case.bSpot);
		if (Case.bSpot)
		{
			TestNearlyEqual(Label + TEXT(": radius"), Spot.Radius, Case.Radius, 1.0e-3f);
			TestTrue(Label + TEXT(": valid"), Spot.IsValid());
		}
		else
		{
			TestTrue(Label + TEXT(": the reason is reported"), Problems.Num() > 0);
			TestFalse(Label + TEXT(": not a valid spot"), Spot.IsValid() && bSpot);
		}
		TestTrue(Label + TEXT(": the radius is always a finite number"), FMath::IsFinite(Spot.Radius));
	}
	// Two Radius tags: a bad one never cancels a good one.
	FLureFishingSpot Spot;
	TestTrue(TEXT("a good Radius and a bad Radius: still a spot"), FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("Lure.FishingSpot"), TEXT("Radius=400"), TEXT("Radius=oops") }), SpotTag, Spot)
		&& FMath::IsNearlyEqual(Spot.Radius, 400.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotParseSoftProblemsKeepTheSpot, "Project.Fishing.QA.Spot.ParseSoftProblemsKeepTheSpot", QAFishing::Flags)
bool FQAFishSpotParseSoftProblemsKeepTheSpot::RunTest(const FString& Parameters)
{
	// Contract: soft problems (an unregistered Habitat/Region tag, a bad number) are listed, but the spot still parses.
	struct FCase
	{
		const TCHAR* Tag;
		const TCHAR* What;
	};
	const FCase Cases[] = {
		{ TEXT("Habitat=Habitat.QA_NotRegistered"), TEXT("unregistered habitat") }, { TEXT("Region=Region.QA_Nowhere"), TEXT("unregistered region") },
		{ TEXT("Luck=lots"), TEXT("text luck") }, { TEXT("Luck=-1"), TEXT("negative luck") }, { TEXT("Luck=nan"), TEXT("NaN luck") },
		{ TEXT("Hours=dawn"), TEXT("text hours") }, { TEXT("Hours=20-"), TEXT("half an hour range") }, { TEXT("Levels=high"), TEXT("text levels") },
		{ TEXT("CastFrom=1,2"), TEXT("two coordinates") }, { TEXT("CastFrom=1,2,3,4"), TEXT("four coordinates") }, { TEXT("CastFrom=a,b,c"), TEXT("text coordinates") },
	};
	for (const FCase& Case : Cases)
	{
		FLureFishingSpot Spot;
		TArray<FString> Problems;
		const bool bSpot = FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("Lure.FishingSpot"), TEXT("Spot=qa"), TEXT("Radius=500"), TEXT("Habitat=Habitat.Shore"), Case.Tag }),
			SpotTag, Spot, &Problems);
		TestTrue(FString::Printf(TEXT("%s ('%s'): still a spot"), Case.What, Case.Tag), bSpot);
		TestEqual(FString::Printf(TEXT("%s ('%s'): exactly one problem reported (%s)"), Case.What, Case.Tag, *FString::Join(Problems, TEXT("; "))), Problems.Num(), 1);
		TestTrue(FString::Printf(TEXT("%s: luck stays a usable number (>= 0, finite)"), Case.What), FMath::IsFinite(Spot.Luck) && Spot.Luck >= 0.f);
		TestTrue(FString::Printf(TEXT("%s: CastFrom stays finite"), Case.What), !Spot.CastFrom.ContainsNaN());
	}
	FLureFishingSpot Spot;
	TArray<FString> Problems;
	FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("Lure.FishingSpot"), TEXT("Radius=500"), TEXT("Habitat=Habitat.QA_NotRegistered"), TEXT("Region=Region.QA_Nowhere") }), SpotTag, Spot, &Problems);
	TestFalse(TEXT("an unregistered habitat gives no habitat (so nothing bites there)"), Spot.HabitatTag.IsValid());
	TestFalse(TEXT("an unregistered region gives no region (the default region is used)"), Spot.RegionTag.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotParseMalformedTagsNeverBreak, "Project.Fishing.QA.Spot.ParseMalformedTagsNeverBreak", QAFishing::Flags)
bool FQAFishSpotParseMalformedTagsNeverBreak::RunTest(const FString& Parameters)
{
	// Junk around a good marker: unknown keys, empty values, extra '=', whitespace, other markers' tags. The spot still parses with
	// the right values, and nothing crashes or turns into NaN.
	const TArray<FName> Junk = SpotTagList({ TEXT(""), TEXT("="), TEXT("=="), TEXT("Spot"), TEXT("Name=Big = Bay"), TEXT("Unknown=1"), TEXT("LureLayout=L_PalmKey"),
		TEXT("Lure.PatrolPoint"), TEXT("Owner=shark"), TEXT("Hours="), TEXT("Levels="), TEXT("Danger="), TEXT("CastFrom="), TEXT("Luck="), TEXT("Habitat="),
		TEXT("Region="), TEXT("Hours=;;"), TEXT("Levels=5-1"), TEXT("Hours=25-30"), TEXT("   "), TEXT("Lure.FishingSpot"),
		TEXT("Spot=qa_junk"), TEXT("Radius=640"), TEXT("Habitat=Habitat.Reef"), TEXT("Luck=1.25") });
	FLureFishingSpot Spot;
	TArray<FString> Problems;
	TestTrue(TEXT("a good marker among junk tags parses"), FLureFishingSpots::ParseSpotTags(Junk, SpotTag, Spot, &Problems));
	TestEqual(TEXT("Spot"), Spot.SpotId, FName(TEXT("qa_junk")));
	TestNearlyEqual(TEXT("Radius"), Spot.Radius, 640.f, 1.0e-3f);
	TestTrue(TEXT("Habitat (a later tag wins over the empty one)"), Spot.HabitatTag == Tag(TEXT("Habitat.Reef")));
	TestNearlyEqual(TEXT("Luck (a later tag wins over the empty one)"), Spot.Luck, 1.25f, 1.0e-4f);
	TestEqual(TEXT("Name keeps its '=' and spaces"), Spot.DisplayName, FString(TEXT("Big = Bay")));
	TestTrue(TEXT("no NaN anywhere"), FMath::IsFinite(Spot.Radius) && FMath::IsFinite(Spot.Luck) && !Spot.CastFrom.ContainsNaN());
	AddInfo(FString::Printf(TEXT("junk marker problems reported: %d (%s)"), Problems.Num(), *FString::Join(Problems, TEXT("; "))));

	FLureFishingSpot Empty;
	TestFalse(TEXT("no tags: not a spot"), FLureFishingSpots::ParseSpotTags({}, SpotTag, Empty));
	TestFalse(TEXT("only the spot tag: not a spot (no radius)"), FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("Lure.FishingSpot") }), SpotTag, Empty));
	TestFalse(TEXT("a full marker without the spot tag: not a spot"), FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("Spot=x"), TEXT("Radius=500"), TEXT("Habitat=Habitat.Shore") }), SpotTag, Empty));
	TestTrue(TEXT("the spot tag is the one asked for (settings), not a hard-coded name"),
		FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("QA.OtherSpotTag"), TEXT("Radius=500") }), FName(TEXT("QA.OtherSpotTag")), Empty));
	TestFalse(TEXT("... and Lure.FishingSpot is not a spot for another spot tag"),
		FLureFishingSpots::ParseSpotTags(SpotTagList({ TEXT("Lure.FishingSpot"), TEXT("Radius=500") }), FName(TEXT("QA.OtherSpotTag")), Empty));
	TestEqual(TEXT("the settings' spot tag is Lure.FishingSpot"), GetDefault<ULureFishingSettings>()->FishingSpotTag, SpotTag);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotLayoutMarkersParseCleanly, "Project.Fishing.QA.Spot.LayoutMarkersParseCleanly", QAFishing::Flags)
bool FQAFishSpotLayoutMarkersParseCleanly::RunTest(const FString& Parameters)
{
	// Every fishing_spot in data/levels/*.json, turned into tags exactly like build_level.py marker_tags(), parses with no problems:
	// radius > 0, registered habitat and region, the builder's luck. Also reports which spots can have bites (content check for the lead).
	FishQA::FTables Fish;
	if (!FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(FPaths::ProjectDir() / TEXT("data/levels/*.json")), true, false);
	TestTrue(TEXT("layouts exist in data/levels"), Files.Num() > 0);
	const float DefaultHours = GetDefault<ULureFishingSettings>()->DefaultTimeOfDayHours;
	int32 Markers = 0;
	TArray<FString> DeadNow;
	TArray<FString> DeadAlways;
	for (const FString& File : Files)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("data/levels") / File))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			AddError(TEXT("can't read ") + File);
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
			FString Type;
			if (!Marker.IsValid() || !Marker->TryGetStringField(TEXT("type"), Type) || Type != TEXT("fishing_spot"))
			{
				continue;
			}
			++Markers;
			const FString Id = File + TEXT(":") + Marker->GetStringField(TEXT("id"));
			FLureFishingSpot Spot;
			TArray<FString> Problems;
			const bool bSpot = FLureFishingSpots::ParseSpotTags(BuilderSpotTags(Marker), SpotTag, Spot, &Problems);
			TestTrue(Id + TEXT(": parses as a spot"), bSpot);
			TestEqual(FString::Printf(TEXT("%s: no problems (%s)"), *Id, *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
			TestNearlyEqual(Id + TEXT(": radius"), Spot.Radius, static_cast<float>(Marker->GetNumberField(TEXT("radius"))), 0.01f);
			TestTrue(Id + TEXT(": habitat registered"), Spot.HabitatTag.IsValid());
			TestTrue(Id + TEXT(": region registered"), Spot.RegionTag.IsValid());
			double Luck = 0.0;
			Marker->TryGetNumberField(TEXT("luck"), Luck);
			TestNearlyEqual(Id + TEXT(": luck"), Spot.Luck, static_cast<float>(Luck), 0.01f);

			// Content report: can anything of the spot's own habitat bite here (default bait, no gap fallback), at the fixed slice
			// time and at any hour?
			FLureFishingEnvironment Environment;
			Environment.BaitTag = Tag(TEXT("Bait.Shrimp"));
			Environment.DefaultRegionTag = Tag(TEXT("Region.Tropical"));
			const FVector2D SpotXY(Spot.Location.X, Spot.Location.Y);
			bool bAnyHour = false;
			bool bNow = false;
			for (int32 Hour = 0; Hour < 24; ++Hour)
			{
				Environment.TimeOfDayHours = Hour + 0.5f;
				FName Species;
				const bool bBites = FFishRoll::PickSpecies(Fish.Get(), SpotContext(Spot, Environment, 1, SpotXY), Species);
				bAnyHour |= bBites;
			}
			Environment.TimeOfDayHours = DefaultHours;
			FName Species;
			bNow = FFishRoll::PickSpecies(Fish.Get(), SpotContext(Spot, Environment, 1, SpotXY), Species);
			if (!bNow)
			{
				DeadNow.Add(Id);
			}
			if (!bAnyHour)
			{
				DeadAlways.Add(Id);
			}
		}
	}
	TestTrue(TEXT("the layouts have fishing spots"), Markers > 0);
	AddInfo(FString::Printf(TEXT("%d fishing spots. Nothing can bite at %.0f:00 (the fixed slice time): %s"), Markers, DefaultHours, DeadNow.Num() ? *FString::Join(DeadNow, TEXT(", ")) : TEXT("none")));
	AddInfo(FString::Printf(TEXT("Nothing can bite at any hour (no species for the habitat yet): %s"), DeadAlways.Num() ? *FString::Join(DeadAlways, TEXT(", ")) : TEXT("none")));
	return true;
}

// =====================================================================================================================
// Spot lookup
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotRadiusBoundaryIs2D, "Project.Fishing.QA.Spot.RadiusBoundaryIs2D", QAFishing::Flags)
bool FQAFishSpotRadiusBoundaryIs2D::RunTest(const FString& Parameters)
{
	// Spec: the bobber is in a spot if its 2D distance to the marker is within Radius (the edge counts; height does not).
	FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	const FVector Center(2000.f, -1000.f, 250.f); // marker 2.5 m above the sea
	Scene.AddSpot(Center, { TEXT("Spot=qa_edge"), TEXT("Radius=500"), TEXT("Habitat=Habitat.Shore") });
	const FVector2D Direction = FVector2D(3.f, 4.f).GetSafeNormal();
	FLureFishingSpot Found;
	for (const float Distance : { 0.f, 250.f, 499.5f, 500.f })
	{
		const FVector Point(Center.X + Direction.X * Distance, Center.Y + Direction.Y * Distance, 0.f);
		TestTrue(FString::Printf(TEXT("%.1f cm from the marker (radius 500): inside"), Distance), FLureFishingSpots::FindSpotAt(Scene.World, Point, SpotTag, Found) && Found.SpotId == TEXT("qa_edge"));
	}
	for (const float Distance : { 500.5f, 520.f, 5000.f })
	{
		const FVector Point(Center.X + Direction.X * Distance, Center.Y + Direction.Y * Distance, 0.f);
		TestFalse(FString::Printf(TEXT("%.1f cm from the marker: outside"), Distance), FLureFishingSpots::FindSpotAt(Scene.World, Point, SpotTag, Found));
	}
	TestTrue(TEXT("height is ignored (a point 50 m below the marker, same XY)"), FLureFishingSpots::FindSpotAt(Scene.World, Center - FVector(0.f, 0.f, 5000.f), SpotTag, Found));
	TestTrue(TEXT("the found spot carries the marker's location"), Found.Location.Equals(Center, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotDeepestSpotWins, "Project.Fishing.QA.Spot.DeepestSpotWins", QAFishing::Flags)
bool FQAFishSpotDeepestSpotWins::RunTest(const FString& Parameters)
{
	// Spec: overlapping spots, the one you are deepest in (smallest distance / radius) wins - not the smallest or the nearest marker.
	FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	Scene.AddSpot(FVector(0.f, 0.f, 0.f), { TEXT("Spot=big"), TEXT("Radius=1000") });
	Scene.AddSpot(FVector(60.f, 0.f, 0.f), { TEXT("Spot=small"), TEXT("Radius=100") });
	FLureFishingSpot Found;
	// At x = 40: big 40/1000 = 0.04, small 20/100 = 0.2 -> big (even though small's marker is nearer and small is smaller).
	TestTrue(TEXT("x = 40: deeper in the big spot"), FLureFishingSpots::FindSpotAt(Scene.World, FVector(40.f, 0.f, 0.f), SpotTag, Found) && Found.SpotId == TEXT("big"));
	// At x = 58: big 0.058, small 0.02 -> small.
	TestTrue(TEXT("x = 58: deeper in the small spot"), FLureFishingSpots::FindSpotAt(Scene.World, FVector(58.f, 0.f, 0.f), SpotTag, Found) && Found.SpotId == TEXT("small"));
	// Outside the small one, inside the big one.
	TestTrue(TEXT("x = 300: only the big spot"), FLureFishingSpots::FindSpotAt(Scene.World, FVector(300.f, 0.f, 0.f), SpotTag, Found) && Found.SpotId == TEXT("big"));
	// Markers that are not spots (no radius) never count.
	Scene.AddSpot(FVector(300.f, 0.f, 0.f), { TEXT("Spot=broken") });
	TestTrue(TEXT("a marker without a radius is ignored"), FLureFishingSpots::FindSpotAt(Scene.World, FVector(300.f, 0.f, 0.f), SpotTag, Found) && Found.SpotId == TEXT("big"));
	TestEqual(TEXT("two usable spots gathered"), FLureFishingSpots::GatherSpots(Scene.World, SpotTag).Num(), 2);
	return true;
}

// =====================================================================================================================
// What the spot gives the roll
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotContextFromSpotAndEnvironment, "Project.Fishing.QA.Spot.ContextFromSpotAndEnvironment", QAFishing::Flags)
bool FQAFishSpotContextFromSpotAndEnvironment::RunTest(const FString& Parameters)
{
	// Spec: habitat and region from the spot (no Region= -> DefaultRegion), Luck = spot Luck + gear luck, time and bait from the environment.
	// T-027: through the water model (the spot is a legacy water area; SpotContext).
	FLureFishingSpot Spot;
	Spot.Radius = 300.f;
	Spot.HabitatTag = Tag(TEXT("Habitat.Reef.Edge"));
	Spot.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
	Spot.Luck = 1.5f;
	FLureFishingEnvironment Environment;
	Environment.TimeOfDayHours = 21.25f;
	Environment.BaitTag = Tag(TEXT("Bait.Squid"));
	Environment.GearLuck = 0.75f;
	Environment.DefaultRegionTag = Tag(TEXT("Region.Tropical"));
	const FVector2D In(0.0, 0.0);
	const FFishRollContext Context = SpotContext(Spot, Environment, -123456, In);
	TestTrue(TEXT("habitat from the spot"), Context.HabitatTag == Spot.HabitatTag);
	TestTrue(TEXT("region from the spot"), Context.RegionTag == Spot.RegionTag);
	TestNearlyEqual(TEXT("luck = spot 1.5 + gear 0.75"), Context.Luck, 2.25f, 1.0e-5f);
	TestNearlyEqual(TEXT("time of day"), Context.TimeOfDayHours, 21.25f, 1.0e-5f);
	TestTrue(TEXT("bait"), Context.BaitTag == Environment.BaitTag);
	TestEqual(TEXT("seed (any int32, negative too)"), Context.Seed, -123456);
	TestTrue(TEXT("the roll picks the species (none forced)"), Context.SpeciesId.IsNone() && Context.ForcedRarityId.IsNone() && !Context.bForceModifiers && !Context.bForceWeightFraction);

	Spot.RegionTag = FGameplayTag();
	TestTrue(TEXT("a spot without Region= uses the default region"), SpotContext(Spot, Environment, 1, In).RegionTag == Environment.DefaultRegionTag);
	Environment.GearLuck = std::numeric_limits<float>::quiet_NaN();
	TestNearlyEqual(TEXT("NaN gear luck counts as 0"), SpotContext(Spot, Environment, 1, In).Luck, 1.5f, 1.0e-5f);
	Environment.GearLuck = 0.f;
	FLureFishingSpot Invalid = Spot;
	Invalid.Radius = 0.f;
	const FFishRollContext NoSpot = SpotContext(Invalid, Environment, 1, In);
	TestTrue(TEXT("an invalid spot (radius 0) is no area: the default water habitat (T-027: fish anywhere)"), NoSpot.HabitatTag == Tag(TEXT("Habitat.Shore")));
	TestNearlyEqual(TEXT("... and none of its luck"), NoSpot.Luck, 0.f, 1.0e-6f);
	TestTrue(TEXT("outside the spot: the default water habitat too"), SpotContext(Spot, Environment, 1, FVector2D(400.0, 0.0)).HabitatTag == Tag(TEXT("Habitat.Shore")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotMarkerDataReachesTheRoll, "Project.Fishing.QA.Spot.MarkerDataReachesTheRoll", QAFishing::Flags)
bool FQAFishSpotMarkerDataReachesTheRoll::RunTest(const FString& Parameters)
{
	// In a world: the server's bite roll uses the marker's habitat, region and luck (+ gear luck), the component's bait/time,
	// and the fish is exactly FFishRoll::PickSpecies + FFishRoll::Roll for that context (the one roll pipeline).
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	struct FCase
	{
		const TCHAR* Habitat;
		const TCHAR* Region; // nullptr = no Region= tag
		float Luck;
		float GearLuck;
		float Hours;
	};
	const FCase Cases[] = {
		{ TEXT("Habitat.Shore.Cove"), TEXT("Region.Tropical.PalmKey"), 0.5f, 0.f, 12.f },
		{ TEXT("Habitat.Lagoon"), nullptr, 1.25f, 0.5f, 8.f },
		{ TEXT("Habitat.Reef.Edge"), TEXT("Region.Tropical.PalmKey"), 0.f, 2.f, 22.f },
	};
	for (const FCase& Case : Cases)
	{
		const FString Label = FString::Printf(TEXT("%s, luck %.2f + gear %.2f, %.0f:00"), Case.Habitat, Case.Luck, Case.GearLuck, Case.Hours);
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		TArray<FString> SpotTags = { TEXT("Spot=qa_case"), FString(TEXT("Habitat=")) + Case.Habitat, FString::Printf(TEXT("Radius=%.0f"), SpotRadius),
			FString::Printf(TEXT("Luck=%.2f"), Case.Luck) };
		if (Case.Region)
		{
			SpotTags.Add(FString(TEXT("Region=")) + Case.Region);
		}
		Scene.AddSpot(SpotCenter, SpotTags);
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped), Case.Hours, 2024);
		if (!Fishing)
		{
			return false;
		}
		Fishing->GearLuck = Case.GearLuck;
		Scene.Tick(10);
		if (!CastAndLand(*this, Scene, Fishing))
		{
			continue;
		}
		TestTrue(Label + TEXT(": the bobber is in the marker's spot"), Fishing->HasCurrentSpot() && Fishing->GetNetState().SpotId == TEXT("qa_case"));
		if (!TestTrue(Label + TEXT(": a bite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120)))
		{
			continue;
		}
		const FFishRollContext& Context = Fishing->GetLastRollContext();
		TestTrue(Label + TEXT(": habitat from the marker"), Context.HabitatTag == Tag(Case.Habitat));
		TestTrue(Label + TEXT(": region from the marker, else the settings' default region"),
			Context.RegionTag == Tag(Case.Region ? Case.Region : *GetDefault<ULureFishingSettings>()->DefaultRegion.ToString()));
		TestNearlyEqual(Label + TEXT(": luck = marker + gear"), Context.Luck, Case.Luck + Case.GearLuck, 1.0e-4f);
		TestNearlyEqual(Label + TEXT(": time of day"), Context.TimeOfDayHours, Case.Hours, 1.0e-4f);
		// Independent oracle: the documented pipeline, step by step.
		FName Species;
		FFishInstance Expected;
		FFishRollContext RollContext = Context;
		const bool bPicked = FFishRoll::PickSpecies(Scene.Fish.Get(), Context, Species);
		RollContext.SpeciesId = Species;
		const bool bRolled = bPicked && FFishRoll::Roll(Scene.Fish.Get(), RollContext, Expected);
		TestTrue(Label + TEXT(": the oracle rolls"), bRolled);
		const TArray<FString> Diff = DifferentFields(FFishInstance::StaticStruct(), &Fishing->GetPendingFish(), &Expected);
		TestEqual(FString::Printf(TEXT("%s: the bite is PickSpecies + Roll of that context (differs in: %s)"), *Label, *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotOpenWaterBitesWithoutSpots, "Project.Fishing.QA.Spot.OpenWaterBitesWithoutSpots", QAFishing::Flags)
bool FQAFishSpotOpenWaterBitesWithoutSpots::RunTest(const FString& Parameters)
{
	// T-027 (replaces lead decision 1 "no spot = no bite", Jimmy's playtest): every body of water can be fished. Outside every
	// spot the bobber is in the default water: no nothing-here flag, the nibbles and the bite come, and the HUD never says
	// "Nothing is biting here".
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddSpot(FVector(1300.f, 3000.f, 0.f), { TEXT("Spot=qa_elsewhere"), TEXT("Habitat=Habitat.Reef"), TEXT("Radius=500") }); // 30 m to the side
	FLureFishingRow Profile = Shipped;
	Profile.BiteWaitMin = Profile.BiteWaitMax = 3.f;
	Profile.NibblesMin = Profile.NibblesMax = 2;
	Profile.NoBiteHintDelay = 1.f;
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), Profile);
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	if (!CastAndLand(*this, Scene, Fishing))
	{
		return false;
	}
	const double Landed = Fishing->GetNetState().StateStartTime;
	TestFalse(TEXT("no spot here"), Fishing->HasCurrentSpot());
	TestFalse(TEXT("no nothing-here flag"), Fishing->GetNetState().bNoFishHere);
	TestTrue(TEXT("a bite is scheduled"), Fishing->GetScheduledBiteTime() >= 0.0);
	Scene.AdvanceTo(Landed + 2.9);
	TestFalse(TEXT("after the hint delay: no 'Nothing is biting' hint"), Fishing->GetStatusText().Contains(TEXT("Nothing is biting")));
	TestTrue(TEXT("the nibbles came (tells of a coming bite)"), Fishing->GetNetState().NibbleId > 0);
	const bool bBite = Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 60);
	TestTrue(TEXT("the bite comes in open water"), bBite);
	TestTrue(TEXT("... from the default water habitat"), Fishing->GetLastRollContext().HabitatTag == Tag(*GetDefault<ULureWaterSettings>()->DefaultWaterHabitat.ToString()));
	TestTrue(TEXT("... and a fish was rolled"), Fishing->GetPendingFish().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotBobberOnLandInsideSpotNoBite, "Project.Fishing.QA.Spot.BobberOnLandInsideSpotNoBite", QAFishing::Flags)
bool FQAFishSpotBobberOnLandInsideSpotNoBite::RunTest(const FString& Parameters)
{
	// Spec: on land (a dock, a beach, a rock) the bobber lies there and nothing bites, even inside a spot's radius.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	Scene.AddBox(FVector(1100.f, 0.f, 15.f), FVector(400.f, 400.f, 15.f)); // a 30 cm rock 7-15 m out, inside the spot
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), FlowProfile(Shipped, 0.5f));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	if (!CastAndLand(*this, Scene, Fishing, (850.f - Shipped.MinCastDistance) / (Shipped.MaxCastDistance - Shipped.MinCastDistance)))
	{
		return false;
	}
	TestFalse(TEXT("on the rock: not on water"), Fishing->GetNetState().bOnWater);
	TestFalse(TEXT("on land there is no spot"), Fishing->HasCurrentSpot());
	TestFalse(TEXT("the nothing-here hint is for water; on land the HUD says it is on land"), Fishing->GetNetState().bNoFishHere);
	Scene.Tick(300);
	TestEqual(TEXT("5 s later: no bite"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
	TestEqual(TEXT("... no nibble"), static_cast<int32>(Fishing->GetNetState().NibbleId), 0);
	TestTrue(TEXT("HUD: on land"), Fishing->GetStatusText().Contains(TEXT("on land")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotNoFitSpotShowsNoNibbles, "Project.Fishing.QA.Spot.NoFitSpotShowsNoNibbles", QAFishing::Flags)
bool FQAFishSpotNoFitSpotShowsNoNibbles::RunTest(const FString& Parameters)
{
	// Spec: a spot where no species fits (the reef at noon) behaves like no spot: the bobber floats, no bite, the hint after
	// NoBiteHintDelay. Nibbles are the tells of a coming bite, so none may show when nothing can bite. Regression test for T006-B1.
	// T-027: this is a data gap; it only stays dead without the gap fallback habitats.
	const FScopedGapFallbacks NoFallback({});
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddSpot(SpotCenter, ReefSpotTags());
	FLureFishingRow Profile = Shipped; // shipped nibble rules
	Profile.NibblesMin = Profile.NibblesMax = 2;
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), Profile, 12.f);
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	if (!CastAndLand(*this, Scene, Fishing))
	{
		return false;
	}
	const double Landed = Fishing->GetNetState().StateStartTime;
	TestTrue(TEXT("QA precondition: the bobber is in the reef spot"), Fishing->HasCurrentSpot());
	Scene.AdvanceTo(Landed + 2.0 * Shipped.BiteWaitMax + 1.0);
	TestEqual(TEXT("no bite at the reef at noon"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
	TestEqual(FString::Printf(TEXT("no nibble (false tell) where nothing can bite: %d nibbles shown"), Fishing->GetNetState().NibbleId), static_cast<int32>(Fishing->GetNetState().NibbleId), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotNoFitHintAfterHintDelay, "Project.Fishing.QA.Spot.NoFitHintAfterHintDelay", QAFishing::Flags)
bool FQAFishSpotNoFitHintAfterHintDelay::RunTest(const FString& Parameters)
{
	// Spec: at a spot where no species fits, the no-bite hint shows after NoBiteHintDelay, like with no spot - even when the
	// bite wait is longer than the hint delay. Regression test for T006-B1 (part 2).
	// T-027: a data gap (no gap fallback here); the hint names the reason ("No fish live in this water right now.").
	const FScopedGapFallbacks NoFallback({});
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddSpot(SpotCenter, ReefSpotTags());
	FLureFishingRow Profile = Shipped;
	Profile.BiteWaitMin = Profile.BiteWaitMax = 10.f;
	Profile.NoBiteHintDelay = 8.f;
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), Profile, 12.f);
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	if (!CastAndLand(*this, Scene, Fishing))
	{
		return false;
	}
	const double Landed = Fishing->GetNetState().StateStartTime;
	Scene.AdvanceTo(Landed + Profile.NoBiteHintDelay - 0.1);
	TestFalse(TEXT("no hint before NoBiteHintDelay"), Fishing->GetStatusText().Contains(TEXT("No fish live in this water")));
	Scene.AdvanceTo(Landed + Profile.NoBiteHintDelay + 0.1);
	TestTrue(TEXT("the hint 0.1 s after NoBiteHintDelay (8 s), though the first bite check is due at 10 s"), Fishing->GetStatusText().Contains(TEXT("No fish live in this water")));
	TestFalse(TEXT("never the old 'Nothing is biting here'"), Fishing->GetStatusText().Contains(TEXT("Nothing is biting")));
	TestTrue(TEXT("... and the replicated nothing-here flag is set by then"), Fishing->GetNetState().bNoFishHere);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotNoFitSpotBitesWhenTimeFits, "Project.Fishing.QA.Spot.NoFitSpotBitesWhenTimeFits", QAFishing::Flags)
bool FQAFishSpotNoFitSpotBitesWhenTimeFits::RunTest(const FString& Parameters)
{
	// Spec: at a spot where nothing fits there is a new check every BiteWaitMax seconds (the time of day moves on).
	// T-027: a data gap (no gap fallback here).
	const FScopedGapFallbacks NoFallback({});
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddSpot(SpotCenter, ReefSpotTags());
	FLureFishingRow Profile = FlowProfile(Shipped, 1.f);
	Profile.BiteWaitMax = 2.f;
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), Profile, 12.f);
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	if (!CastAndLand(*this, Scene, Fishing))
	{
		return false;
	}
	Scene.Tick(200);
	TestTrue(TEXT("noon at the reef: nothing"), Fishing->GetFishingState() == ELureFishingState::Waiting && Fishing->GetNetState().bNoFishHere);
	const double Evening = Fishing->GetFishingTime();
	Fishing->TimeOfDayOverride = 20.f; // the snapper bites 15-09
	const bool bBit = Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 400);
	TestTrue(TEXT("20:00 at the reef: a bite comes"), bBit);
	if (bBit)
	{
		const double After = Fishing->GetNetState().StateStartTime - Evening;
		TestTrue(FString::Printf(TEXT("... within BiteWaitMax 2 s (+ a frame) of the time change (%.3f s)"), After), After <= Profile.BiteWaitMax + 2.0 * Dt);
		TestEqual(TEXT("... the reef fish"), Fishing->GetPendingFish().SpeciesId, FName(TEXT("CoralSnapper")));
		TestFalse(TEXT("... and the nothing-here flag is cleared"), Fishing->GetNetState().bNoFishHere);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotMarkersAreReadLive, "Project.Fishing.QA.Spot.MarkersAreReadLive", QAFishing::Flags)
bool FQAFishSpotMarkersAreReadLive::RunTest(const FString& Parameters)
{
	// Contract: markers placed, moved or removed at any time are seen by the next cast.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), FlowProfile(Shipped, 0.5f));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	auto CastHasSpot = [this, &Scene, Fishing]()
	{
		CastAndLand(*this, Scene, Fishing);
		const bool bSpot = Fishing->HasCurrentSpot();
		Fishing->AuthorityReelIn();
		Scene.Tick(2);
		return bSpot;
	};
	TestFalse(TEXT("no marker: no spot"), CastHasSpot());
	AActor* Marker = Scene.AddShoreSpot();
	TestTrue(TEXT("a marker placed after BeginPlay is found"), CastHasSpot());
	Marker->SetActorLocation(FVector(1300.f, 5000.f, 0.f));
	TestFalse(TEXT("moved away: no spot"), CastHasSpot());
	Marker->SetActorLocation(SpotCenter);
	TestTrue(TEXT("moved back: the spot again"), CastHasSpot());
	Marker->Destroy();
	TestFalse(TEXT("removed: no spot"), CastHasSpot());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotDefaultWaterHabitatDecidesOpenWater, "Project.Fishing.QA.Spot.DefaultWaterHabitatDecidesOpenWater", QAFishing::Flags)
bool FQAFishSpotDefaultWaterHabitatDecidesOpenWater::RunTest(const FString& Parameters)
{
	// T-027 (replaces the OffSpotHabitat setting): ULureWaterSettings::DefaultWaterHabitat is the habitat of water that no area or
	// spot covers; changing it is a data edit.
	TestEqual(TEXT("default: Habitat.Shore"), GetDefault<ULureWaterSettings>()->DefaultWaterHabitat, FName(TEXT("Habitat.Shore")));
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	const FScopedDefaultWaterHabitat Reef(TEXT("Habitat.Reef"));
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), FlowProfile(Shipped, 0.5f), 20.f); // the snapper bites 15-09
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	if (!CastAndLand(*this, Scene, Fishing))
	{
		return false;
	}
	TestFalse(TEXT("no spot here"), Fishing->HasCurrentSpot());
	TestTrue(TEXT("a fish bites in open water"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 120));
	TestTrue(TEXT("... from the default water habitat set in the settings"), Fishing->GetLastRollContext().HabitatTag == Tag(TEXT("Habitat.Reef")));
	TestEqual(TEXT("... so a reef fish"), Fishing->GetPendingFish().SpeciesId, FName(TEXT("CoralSnapper")));
	TestTrue(TEXT("... in the default region"), Fishing->GetLastRollContext().RegionTag == Tag(*GetDefault<ULureFishingSettings>()->DefaultRegion.ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishSpotTaggedWaterSurface, "Project.Fishing.QA.Spot.TaggedWaterSurface", QAFishing::Flags)
bool FQAFishSpotTaggedWaterSurface::RunTest(const FString& Parameters)
{
	// Spec: the water surface is a water volume (T-026), else the top of an actor tagged Lure.Water, else FallbackWaterZ.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	TestEqual(TEXT("settings: the water tag"), Settings->WaterTag, FName(TEXT("Lure.Water")));
	float Z = 1.f;
	TestTrue(TEXT("no tagged water: the fallback sea level"), FLureFishingSpots::FindWaterSurfaceZ(Scene.World, FVector2D(1000.f, 0.f), *Settings, Z) && FMath::IsNearlyEqual(Z, Settings->FallbackWaterZ));
	Scene.AddTaggedWater(-60.f, 3000.f); // like L_Dev_Movement's water_z
	TestTrue(TEXT("tagged water: its top"), FLureFishingSpots::FindWaterSurfaceZ(Scene.World, FVector2D(1000.f, 0.f), *Settings, Z) && FMath::IsNearlyEqual(Z, -60.f, 0.01f));
	TestTrue(TEXT("outside the tagged water: the fallback again"), FLureFishingSpots::FindWaterSurfaceZ(Scene.World, FVector2D(9000.f, 0.f), *Settings, Z) && FMath::IsNearlyEqual(Z, Settings->FallbackWaterZ));
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), FlowProfile(Shipped));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	TestTrue(TEXT("cast"), Fishing->AuthorityCast(0.5f, 0.f));
	TestTrue(TEXT("the bobber floats on the tagged water"), Fishing->GetNetState().bOnWater);
	TestNearlyEqual(TEXT("... at its surface"), static_cast<float>(Fishing->GetNetState().BobberRest.Z), -60.f, 0.5f);
	return true;
}

} // namespace QAFishing

#endif // WITH_DEV_AUTOMATION_TESTS

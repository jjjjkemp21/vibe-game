// QA-A3a (qa-engineer, 2026-09-24): independent A3 gate tests for T-071 "a cast bobber under a roof lands under it".
// Derived from docs/specs/fishing-rules.md "Cast" / "Landing height (T-071)": the downward ground search starts
// LandingSearchHeight (DT_Fishing, default 500 cm) above the cast origin, but never above the first solid surface straight
// above the origin. Covers what the implementer's Project.Fishing.Cast.UnderRoof.* does not: the default height's edges
// (a surface just under / just over origin + 500), the height coming from the table row, and a tall search still capped by a
// roof over the caster. FLureFishingSpots::ResolveLanding is called directly in a transient game world with NO water (a
// transient copy of the settings with the sea-level fallback off), so the flight is a level trace at eye height and the high
// slabs never block it.
// Project.Fishing.QA.S1.Cast.*   Everything lives in namespace LureQAS1CastLanding (unity builds: no file-scope using).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingSettings.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace LureQAS1CastLanding
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Eye = 160.f;
	constexpr float CastDistance = 600.f;
	constexpr float DefaultSearch = 500.f; // the spec's default LandingSearchHeight
	// Lane y = 0: a slab whose top is 5 cm UNDER eye + 500 (found by the default search).
	constexpr float LaneUnder = 0.f;
	constexpr float UnderTop = Eye + DefaultSearch - 5.f;
	// Lane y = 1000: a slab whose bottom is 5 cm OVER eye + 500 (missed by the default search).
	constexpr float LaneOver = 1000.f;
	constexpr float OverBottom = Eye + DefaultSearch + 5.f;
	constexpr float OverTop = OverBottom + 15.f;
	// Lane y = 2000: a high roof 2000..2020 cm over both the caster and the landing point.
	constexpr float LaneRoof = 2000.f;
	constexpr float HighRoofTop = 2020.f;

	struct FSlabWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		/** The project settings with the sea-level water fallback off: this world has no water at all. */
		TStrongObjectPtr<ULureFishingSettings> DrySettings;

		bool Create(FAutomationTestBase& Test)
		{
			DrySettings.Reset(DuplicateObject<ULureFishingSettings>(GetDefault<ULureFishingSettings>(), GetTransientPackage()));
			DrySettings->bUseFallbackWaterZ = false;
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			if (!World)
			{
				return false;
			}
			AddSlab(-3000.f, 3000.f, -3000.f, 3000.f, -200.f, 0.f);                       // ground, top z = 0
			AddSlab(400.f, 800.f, LaneUnder - 100.f, LaneUnder + 100.f, UnderTop - 25.f, UnderTop); // just under the default reach
			AddSlab(400.f, 800.f, LaneOver - 100.f, LaneOver + 100.f, OverBottom, OverTop);       // just over the default reach
			AddSlab(-500.f, 1500.f, LaneRoof - 200.f, LaneRoof + 200.f, HighRoofTop - 20.f, HighRoofTop); // high roof over the caster
			return true;
		}

		void AddSlab(float MinX, float MaxX, float MinY, float MaxY, float MinZ, float MaxZ)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
			Box->SetBoxExtent(FVector(MaxX - MinX, MaxY - MinY, MaxZ - MinZ) * 0.5f, false);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(FVector(MinX + MaxX, MinY + MaxY, MinZ + MaxZ) * 0.5f);
		}

		/** A cast from (0, LaneY, Eye) along +X; Profile null = the code default. */
		FLureCastLanding Cast(float LaneY, const FLureFishingRow* Profile = nullptr) const
		{
			const FVector Origin(0.f, LaneY, Eye);
			return FLureFishingSpots::ResolveLanding(World, nullptr, Origin, FVector2D(Origin.X, Origin.Y), FVector2D(1.f, 0.f), CastDistance,
				*DrySettings, Profile);
		}
	};

	inline FLureFishingRow ProfileWithSearch(float Height)
	{
		FLureFishingRow Row;
		Row.LandingSearchHeight = Height;
		return Row;
	}

	void ExpectRestZ(FAutomationTestBase& Test, const FString& Label, const FLureCastLanding& Landing, float ExpectedZ)
	{
		const FString Where = FString::Printf(TEXT("%s (rest %s, on water %d, blocked %d)"), *Label, *Landing.Rest.ToCompactString(), Landing.bOnWater ? 1 : 0,
			Landing.bBlocked ? 1 : 0);
		Test.TestFalse(Where + TEXT(": not blocked"), Landing.bBlocked);
		Test.TestFalse(Where + TEXT(": on land"), Landing.bOnWater);
		Test.TestTrue(Where + TEXT(": at the full distance"), FMath::IsNearlyEqual(Landing.Rest.X, static_cast<double>(CastDistance), 1.0));
		Test.TestTrue(FString::Printf(TEXT("%s: rests at z = %.0f"), *Where, ExpectedZ), FMath::IsNearlyEqual(Landing.Rest.Z, static_cast<double>(ExpectedZ), 1.0));
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** The default search reach (eye + 500 cm): a surface 5 cm under it is found, one 5 cm over it is not. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1CastDefaultEdges, "Project.Fishing.QA.S1.Cast.DefaultSearchHeightEdges", Flags)
	bool FQAS1CastDefaultEdges::RunTest(const FString& Parameters)
	{
		TestEqual(TEXT("the code default LandingSearchHeight is the spec's 500 cm"), FLureFishingRow().LandingSearchHeight, DefaultSearch);
		FSlabWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ExpectRestZ(*this, TEXT("a slab top 5 cm under eye + 500"), W.Cast(LaneUnder), UnderTop);
		ExpectRestZ(*this, TEXT("a slab 5 cm over eye + 500: out of reach, lands on the ground"), W.Cast(LaneOver), 0.f);
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** The reach is the table row's LandingSearchHeight: raising it finds the high slab, 0 searches from eye height only. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1CastFromTable, "Project.Fishing.QA.S1.Cast.SearchHeightComesFromTheRow", Flags)
	bool FQAS1CastFromTable::RunTest(const FString& Parameters)
	{
		FSlabWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FLureFishingRow Tall = ProfileWithSearch(DefaultSearch + 100.f);
		const FLureFishingRow Zero = ProfileWithSearch(0.f);
		ExpectRestZ(*this, TEXT("row reach 600 cm: the slab over the default reach is found"), W.Cast(LaneOver, &Tall), OverTop);
		ExpectRestZ(*this, TEXT("row reach 0 cm: the slab above eye height is not found"), W.Cast(LaneUnder, &Zero), 0.f);
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------
	/** A tall search (row reach 3000 cm) still starts under a roof over the caster: it lands on the ground, not the roof top. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1CastTallUnderRoof, "Project.Fishing.QA.S1.Cast.TallSearchStillStopsUnderARoof", Flags)
	bool FQAS1CastTallUnderRoof::RunTest(const FString& Parameters)
	{
		FSlabWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FLureFishingRow VeryTall = ProfileWithSearch(3000.f);
		// Control: with nothing overhead, the tall search reaches the slab in its lane (so the reach itself works).
		ExpectRestZ(*this, TEXT("reach 3000 cm, open sky: lands on the slab"), W.Cast(LaneUnder, &VeryTall), UnderTop);
		const FLureCastLanding Landing = W.Cast(LaneRoof, &VeryTall);
		ExpectRestZ(*this, TEXT("reach 3000 cm under a 20 m roof: lands on the ground under it"), Landing, 0.f);
		TestTrue(TEXT("never on the roof top"), !FMath::IsNearlyEqual(Landing.Rest.Z, static_cast<double>(HighRoofTop), 5.0));
		return true;
	}
}

#endif

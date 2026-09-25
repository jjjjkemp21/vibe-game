// T-071: a cast (or a fish drop) from under a roof lands under the roof, never on top of it.
// Project.Fishing.Cast.UnderRoof.* : a transient game world with ground (top z = 0) for x <= 600, Lure.Water (top z = -30)
// everywhere, and a box roof 300..320 cm up over x in [-500, 800]. FLureFishingSpots::ResolveLanding is called directly
// (what ULureFishingComponent's server cast uses).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/LureFishingSettings.h"
#include "Tests/AutomationCommon.h"

namespace LureCastUnderRoofTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float GroundEndX = 600.f;
	constexpr float WaterTop = -30.f;
	constexpr float RoofBottom = 300.f;
	constexpr float RoofTop = 320.f;
	constexpr float RoofMinX = -500.f;
	constexpr float RoofMaxX = 800.f;
	constexpr float EyeHeight = 160.f;
	/** A player under the roof at x = 0; casts go along +X. */
	const FVector UnderRoofOrigin(0.f, 0.f, EyeHeight);
	/** A player on open ground west of the roof. */
	const FVector OpenGroundOrigin(-900.f, 0.f, EyeHeight);
	const FVector2D AlongX(1.f, 0.f);

	struct FRoofWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			// Ground: x in [-2000, 600], top at z = 0.
			AddBox(FVector((-2000.f + GroundEndX) * 0.5f, 0.f, -100.f), FVector((GroundEndX + 2000.f) * 0.5f, 2000.f, 100.f), UCollisionProfile::BlockAll_ProfileName);
			// The roof: a solid slab 300..320 cm up.
			AddBox(FVector((RoofMinX + RoofMaxX) * 0.5f, 0.f, (RoofBottom + RoofTop) * 0.5f), FVector((RoofMaxX - RoofMinX) * 0.5f, 500.f, (RoofTop - RoofBottom) * 0.5f),
				UCollisionProfile::BlockAll_ProfileName);
			// The water surface: a non-colliding box tagged Lure.Water, top at z = WaterTop.
			AActor* Water = AddBox(FVector(0.f, 0.f, WaterTop - 500.f), FVector(20000.f, 20000.f, 500.f), UCollisionProfile::NoCollision_ProfileName);
			Water->Tags.Add(GetDefault<ULureFishingSettings>()->WaterTag);
			return World != nullptr;
		}

		AActor* AddBox(const FVector& Center, const FVector& Extent, FName Profile)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
			Box->SetBoxExtent(Extent, false);
			Box->SetCollisionProfileName(Profile);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(Center);
			return Actor;
		}

		FLureCastLanding Throw(const FVector& Origin, float Distance) const
		{
			return FLureFishingSpots::ResolveLanding(World, nullptr, Origin, FVector2D(Origin.X, Origin.Y), AlongX, Distance, *GetDefault<ULureFishingSettings>());
		}
	};

	void TestLanding(FAutomationTestBase& Test, const FString& Label, const FLureCastLanding& Landing, float ExpectedX, bool bExpectWater, float ExpectedZ,
		bool bCheckNotBlocked = true)
	{
		const FString Where = FString::Printf(TEXT("%s (rest %s, on water %d, blocked %d)"), *Label, *Landing.Rest.ToCompactString(), Landing.bOnWater ? 1 : 0,
			Landing.bBlocked ? 1 : 0);
		if (bCheckNotBlocked)
		{
			Test.TestFalse(FString::Printf(TEXT("%s: not blocked on the way"), *Where), Landing.bBlocked);
		}
		Test.TestEqual(FString::Printf(TEXT("%s: on water"), *Where), Landing.bOnWater, bExpectWater);
		Test.TestTrue(FString::Printf(TEXT("%s: at the full distance"), *Where), FMath::IsNearlyEqual(Landing.Rest.X, ExpectedX, 1.0));
		Test.TestTrue(FString::Printf(TEXT("%s: at z = %.0f"), *Where, ExpectedZ), FMath::IsNearlyEqual(Landing.Rest.Z, ExpectedZ, 1.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastUnderRoofGroundTest, "Project.Fishing.Cast.UnderRoof.OntoGround", LureCastUnderRoofTest::Flags)

bool FLureCastUnderRoofGroundTest::RunTest(const FString& Parameters)
{
	using namespace LureCastUnderRoofTest;
	FRoofWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	TestLanding(*this, TEXT("under the roof, onto the ground under it"), W.Throw(UnderRoofOrigin, 400.f), 400.f, false, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastUnderRoofWaterTest, "Project.Fishing.Cast.UnderRoof.OntoWater", LureCastUnderRoofTest::Flags)

bool FLureCastUnderRoofWaterTest::RunTest(const FString& Parameters)
{
	using namespace LureCastUnderRoofTest;
	FRoofWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	TestLanding(*this, TEXT("under the roof, onto the water under it"), W.Throw(UnderRoofOrigin, 700.f), 700.f, true, WaterTop);
	TestLanding(*this, TEXT("under the roof, onto the water past it"), W.Throw(UnderRoofOrigin, 1200.f), 1200.f, true, WaterTop);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastUnderRoofDropTest, "Project.Fishing.Cast.UnderRoof.DropInPlace", LureCastUnderRoofTest::Flags)

bool FLureCastUnderRoofDropTest::RunTest(const FString& Parameters)
{
	using namespace LureCastUnderRoofTest;
	FRoofWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	// A zero-distance landing from hand height, straight down (what the fish drop used before T-066). Its "flight" aims at the water under the ground, so
	// it reports blocked (by the ground, pulled back 0 cm): only where it rests matters here.
	TestLanding(*this, TEXT("a zero-distance drop under the roof"), W.Throw(FVector(300.f, 0.f, 120.f), 0.f), 300.f, false, 0.f,
		/*bCheckNotBlocked*/ false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastOpenGroundOntoRoofTest, "Project.Fishing.Cast.UnderRoof.FromOpenGroundOntoRoof", LureCastUnderRoofTest::Flags)

bool FLureCastOpenGroundOntoRoofTest::RunTest(const FString& Parameters)
{
	using namespace LureCastUnderRoofTest;
	FRoofWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	// No regression: from open ground (nothing overhead) a cast onto the roof still lands on top of it.
	TestLanding(*this, TEXT("open ground, onto the roof"), W.Throw(OpenGroundOrigin, 600.f), -300.f, false, RoofTop);
	return true;
}

#endif

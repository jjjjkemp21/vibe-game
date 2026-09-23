// Lure fishing-loop playtest bug 1: design zones (TriggerBoxes such as shark_zone / shadow_zone) must never block a cast.
// Project.Fishing.CastTrace.* : a transient game world with an 8 x 8 m dock (top z = 100) and a Lure.Water surface at z = 0;
// FLureFishingSpots::ResolveLanding / TraceCast are called directly (what ULureFishingComponent's server cast uses).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/TriggerBox.h"
#include "Engine/TriggerSphere.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/LureFishingSettings.h"
#include "Tests/AutomationCommon.h"

namespace LureCastTraceTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float DockTop = 100.f;
	constexpr float DockHalf = 400.f;
	/** The rod tip of a player standing near the dock's +X edge; casts go along +X over the water. */
	const FVector Origin(350.f, 0.f, DockTop + 150.f);
	const FVector2D StartXY(350.f, 0.f);
	const FVector2D AlongX(1.f, 0.f);
	constexpr float CastDistance = 1200.f; // lands at x = 1550, over the water

	struct FCastWorld
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
			AddBox(FVector(0.f, 0.f, DockTop * 0.5f), FVector(DockHalf, DockHalf, DockTop * 0.5f), UCollisionProfile::BlockAll_ProfileName);
			// The water surface: a non-colliding box tagged Lure.Water, top at z = 0.
			AActor* Water = AddBox(FVector(0.f, 0.f, -50.f), FVector(20000.f, 20000.f, 50.f), UCollisionProfile::NoCollision_ProfileName);
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

		/** A TriggerBox like the level's design zones (shark_zone: over the water, top at z = 100). */
		ATriggerBox* AddTriggerBox(const FVector& Center, const FVector& Extent)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATriggerBox* Trigger = World->SpawnActor<ATriggerBox>(ATriggerBox::StaticClass(), FTransform(Center), Params);
			if (UBoxComponent* Box = Trigger ? Cast<UBoxComponent>(Trigger->GetCollisionComponent()) : nullptr)
			{
				Box->SetBoxExtent(Extent, true);
			}
			return Trigger;
		}

		FLureCastLanding Throw(float Distance = CastDistance) const
		{
			return FLureFishingSpots::ResolveLanding(World, nullptr, Origin, StartXY, AlongX, Distance, *GetDefault<ULureFishingSettings>());
		}
	};

	void TestOnWater(FAutomationTestBase& Test, const FString& Label, const FLureCastLanding& Landing)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: the bobber lands on the water (rest %s, blocked %d)"), *Label, *Landing.Rest.ToCompactString(), Landing.bBlocked ? 1 : 0),
			Landing.bOnWater && !Landing.bBlocked);
		Test.TestTrue(FString::Printf(TEXT("%s: ... at the full distance, on the surface"), *Label),
			FMath::IsNearlyEqual(Landing.Rest.X, StartXY.X + CastDistance, 1.0) && FMath::IsNearlyEqual(Landing.Rest.Z, 0.0, 0.5));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastTraceChannelTest, "Project.Fishing.CastTrace.ChannelRegistered", LureCastTraceTest::Flags)

bool FLureCastTraceChannelTest::RunTest(const FString& Parameters)
{
	const UCollisionProfile* Profiles = UCollisionProfile::Get();
	TestEqual(TEXT("ECC_GameTraceChannel1 is \"LureCast\" (Config/DefaultEngine.ini)"),
		Profiles->ReturnChannelNameFromContainerIndex(static_cast<int32>(FLureFishingSpots::CastChannel)), FName(TEXT("LureCast")));
	FCollisionResponseTemplate Template;
	if (TestTrue(TEXT("the Trigger profile exists"), Profiles->GetProfileTemplate(TEXT("Trigger"), Template)))
	{
		TestEqual(TEXT("the Trigger profile ignores LureCast"), static_cast<int32>(Template.ResponseToChannels.GetResponse(FLureFishingSpots::CastChannel)),
			static_cast<int32>(ECR_Ignore));
	}
	if (TestTrue(TEXT("the BlockAll profile exists"), Profiles->GetProfileTemplate(UCollisionProfile::BlockAll_ProfileName, Template)))
	{
		TestEqual(TEXT("BlockAll blocks LureCast (level geometry stops casts)"), static_cast<int32>(Template.ResponseToChannels.GetResponse(FLureFishingSpots::CastChannel)),
			static_cast<int32>(ECR_Block));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastTraceTriggerOverWaterTest, "Project.Fishing.CastTrace.TriggerBoxOverWaterDoesNotBlock", LureCastTraceTest::Flags)

bool FLureCastTraceTriggerOverWaterTest::RunTest(const FString& Parameters)
{
	using LureCastTraceTest::TestOnWater;
	LureCastTraceTest::FCastWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	TestOnWater(*this, TEXT("open water (baseline)"), W.Throw());

	// shark_zone at jetty_end: a 10 m TriggerBox over the water around the landing point, its top at z = 100.
	ATriggerBox* Zone = W.AddTriggerBox(FVector(1500.f, 0.f, 0.f), FVector(500.f, 500.f, 100.f));
	if (!TestNotNull(TEXT("the TriggerBox spawns"), Zone))
	{
		return false;
	}
	TestOnWater(*this, TEXT("TriggerBox (Trigger profile) over the landing"), W.Throw());

	// "Whatever their collision profile": the same zone set to BlockAll still never stops a cast.
	Zone->GetCollisionComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	TestOnWater(*this, TEXT("TriggerBox set to BlockAll"), W.Throw());

	// A zone right in the flight path, taller than the rod tip (the flight trace, not only the landing trace).
	W.AddTriggerBox(FVector(900.f, 0.f, 0.f), FVector(200.f, 800.f, 600.f));
	TestOnWater(*this, TEXT("a tall TriggerBox across the flight path"), W.Throw());

	// A TriggerSphere (another ATriggerBase), also with a blocking profile.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ATriggerSphere* Sphere = W.World->SpawnActor<ATriggerSphere>(ATriggerSphere::StaticClass(), FTransform(FVector(1550.f, 0.f, 50.f)), Params))
	{
		Sphere->GetCollisionComponent()->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		TestOnWater(*this, TEXT("a blocking TriggerSphere at the landing"), W.Throw());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastTraceOverlapOnlyTest, "Project.Fishing.CastTrace.OverlapOnlyComponentsDoNotBlock", LureCastTraceTest::Flags)

bool FLureCastTraceOverlapOnlyTest::RunTest(const FString& Parameters)
{
	using LureCastTraceTest::TestOnWater;
	LureCastTraceTest::FCastWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	// A plain actor with an overlap-only box (any trigger shape a designer might place) over the landing.
	W.AddBox(FVector(1500.f, 0.f, 0.f), FVector(400.f, 400.f, 100.f), TEXT("OverlapAllDynamic"));
	TestOnWater(*this, TEXT("OverlapAllDynamic box"), W.Throw());

	// Overlap-only for everything physical but explicitly Block on LureCast: the filter still lets the cast through.
	AActor* Odd = W.AddBox(FVector(1550.f, 0.f, 50.f), FVector(300.f, 300.f, 60.f), TEXT("OverlapAll"));
	UPrimitiveComponent* OddBox = Cast<UPrimitiveComponent>(Odd->GetRootComponent());
	OddBox->SetCollisionResponseToChannel(FLureFishingSpots::CastChannel, ECR_Block);
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(LureCastTraceTest), false);
	TestFalse(TEXT("TraceCast passes an overlap-only box that blocks LureCast"),
		FLureFishingSpots::TraceCast(W.World, Hit, FVector(1550.f, 0.f, 400.f), FVector(1550.f, 0.f, -10.f), Query));
	TestOnWater(*this, TEXT("overlap-only box that blocks LureCast"), W.Throw());

	// The opt-out: a solid prop set to ignore LureCast doesn't stop casts either.
	AActor* Buoy = W.AddBox(FVector(1550.f, 0.f, 40.f), FVector(60.f, 60.f, 60.f), UCollisionProfile::BlockAll_ProfileName);
	Cast<UPrimitiveComponent>(Buoy->GetRootComponent())->SetCollisionResponseToChannel(FLureFishingSpots::CastChannel, ECR_Ignore);
	TestOnWater(*this, TEXT("solid prop that ignores LureCast"), W.Throw());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureCastTraceSolidStillBlocksTest, "Project.Fishing.CastTrace.SolidGeometryStillBlocks", LureCastTraceTest::Flags)

bool FLureCastTraceSolidStillBlocksTest::RunTest(const FString& Parameters)
{
	LureCastTraceTest::FCastWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	// A zone in front of a wall: the wall still stops the flight, the zone doesn't.
	W.AddTriggerBox(FVector(800.f, 0.f, 0.f), FVector(150.f, 600.f, 400.f));
	W.AddBox(FVector(1100.f, 0.f, 200.f), FVector(20.f, 600.f, 300.f), UCollisionProfile::BlockAll_ProfileName);
	const FLureCastLanding Walled = W.Throw();
	TestTrue(FString::Printf(TEXT("a wall on the way pulls the landing back in front of it (rest %s)"), *Walled.Rest.ToCompactString()),
		Walled.bBlocked && Walled.Rest.X < 1080.0 && Walled.Rest.X > 1000.0);
	TestTrue(TEXT("... onto the water in front of the wall"), Walled.bOnWater);

	// A rock above the water at the landing point is land.
	LureCastTraceTest::FCastWorld R;
	if (!R.Create(*this))
	{
		return false;
	}
	R.AddTriggerBox(FVector(1550.f, 0.f, 0.f), FVector(400.f, 400.f, 300.f));
	R.AddBox(FVector(1550.f, 0.f, 10.f), FVector(80.f, 80.f, 10.f), UCollisionProfile::BlockAll_ProfileName); // a low rock, top z = 20
	const FLureCastLanding Rock = R.Throw();
	TestTrue(FString::Printf(TEXT("a rock under a zone: the bobber lands on the rock, not the zone (rest %s)"), *Rock.Rest.ToCompactString()),
		!Rock.bOnWater && FMath::IsNearlyEqual(Rock.Rest.Z, 20.0, 1.0));
	return true;
}

#endif

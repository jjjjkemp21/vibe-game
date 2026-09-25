// Lure T-066 tests (unreal-engineer): a fish dropped under a roof falls from the hand's height, never onto the roof above
// (Jimmy's A2 playtest: "drops a fish under the shop roof, it teleports on top of the roof").
// Rules: docs/specs/catch-handling-rules.md "The hand", Drop. Project.Catch.Drop.UnderRoof.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureProgressionComponent.h"

namespace LureDropUnderRoofTest
{
	/** The player standing on the dock at Feet, facing +X, under a solid roof slab 200 cm above its eye (+-400 cm around it) */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;
		float RoofBottom = 0.0f;

		bool Create(FAutomationTestBase& Test, const FVector& Feet)
		{
			if (!W.Create(Test))
			{
				return false;
			}
			Player = W.SpawnPlayer(Test, Feet);
			Hands = LCT::HandsOf(Player);
			Use = LCT::InteractionOf(Player);
			if (!Test.TestNotNull(TEXT("player"), Player) || !Test.TestNotNull(TEXT("hands"), Hands) || !Test.TestNotNull(TEXT("use keys"), Use))
			{
				return false;
			}
			W.Tick(20); // standing on the dock
			const FVector Eye = Player->GetPawnViewLocation();
			RoofBottom = static_cast<float>(Eye.Z) + 200.0f;
			W.AddBox(FVector(Eye.X, Eye.Y, RoofBottom + 10.0f), FVector(400.0f, 400.0f, 10.0f));
			W.Tick(2);
			return true;
		}
	};

	bool IsOnDock(const FVector& Point)
	{
		return FMath::Abs(Point.Z - LCT::DockTop) < 2.0f && FMath::Abs(Point.X) <= LCT::DockHalf && FMath::Abs(Point.Y) <= LCT::DockHalf;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropUnderRoofOnDock, "Project.Catch.Drop.UnderRoof.LandsOnDock", LCT::Flags)
bool FDropUnderRoofOnDock::RunTest(const FString& Parameters)
{
	LureDropUnderRoofTest::FRig Rig;
	if (!Rig.Create(*this, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureFishItem* Item = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 661));
	if (!TestNotNull(TEXT("a fish in hand"), Item))
	{
		return false;
	}
	TestTrue(TEXT("F"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	const FVector Rest = Item->GetActorLocation();
	TestTrue(FString::Printf(TEXT("dropped under a roof: it lies on the dock, not on the roof (%s, roof bottom %.0f)"), *Rest.ToCompactString(), Rig.RoofBottom),
		Item->IsFree() && LureDropUnderRoofTest::IsOnDock(Rest));
	TestFalse(TEXT("... the hands are empty"), Rig.Hands->IsHoldingSomething());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropUnderRoofOverWater, "Project.Catch.Drop.UnderRoof.OverWaterReleases", LCT::Flags)
bool FDropUnderRoofOverWater::RunTest(const FString& Parameters)
{
	LureDropUnderRoofTest::FRig Rig;
	if (!Rig.Create(*this, FVector(LCT::DockHalf - 30.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureFishItem* Item = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 662));
	if (!TestNotNull(TEXT("a fish in hand"), Item))
	{
		return false;
	}
	if (ULureProgressionComponent* Progression = LCT::ProgressionOf(Rig.Player))
	{
		Progression->ClearNotices();
	}
	const TWeakObjectPtr<ALureFishItem> Weak = Item;
	TestTrue(TEXT("F at the dock's edge under a roof"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestFalse(FString::Printf(TEXT("the drop point is over the water: the fish is released (gone) (%s)"),
		Weak.IsValid() ? *Weak->GetActorLocation().ToCompactString() : TEXT("-")), Weak.IsValid());
	TestFalse(TEXT("... the hands are empty"), Rig.Hands->IsHoldingSomething());
	TestTrue(FString::Printf(TEXT("... \"Released the Bonefish\" (%s)"), *LCT::NoticesOf(Rig.Player)), LCT::NoticesOf(Rig.Player).Contains(TEXT("Released the Bonefish")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropUnderRoofLetGo, "Project.Catch.Drop.UnderRoof.LetGoLandsOnDock", LCT::Flags)
bool FDropUnderRoofLetGo::RunTest(const FString& Parameters)
{
	LureDropUnderRoofTest::FRig Rig;
	if (!Rig.Create(*this, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	LCT::UseEstimatedRodTip(Rig.Player);
	ALureFishItem* Item = Rig.W.Land(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 663));
	if (!TestNotNull(TEXT("landed"), Item))
	{
		return false;
	}
	Rig.W.Tick(10);
	const FVector Pivot = Rig.Hands->GetHangPivot();
	TestTrue(FString::Printf(TEXT("precondition: the rod tip is under the roof (%s)"), *Pivot.ToCompactString()), Pivot.Z < Rig.RoofBottom);
	TestTrue(TEXT("F lets the hanging fish go"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	const FVector Rest = Item->GetActorLocation();
	TestTrue(FString::Printf(TEXT("it drops under the rod tip onto the dock, not the roof (%s, tip %s)"), *Rest.ToCompactString(), *Pivot.ToCompactString()),
		Item->IsFree() && LureDropUnderRoofTest::IsOnDock(Rest) && FVector2D::Distance(FVector2D(Rest), FVector2D(Pivot)) < 2.0f);
	TestNull(TEXT("the hook is free"), Rig.Hands->GetHangingFish());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

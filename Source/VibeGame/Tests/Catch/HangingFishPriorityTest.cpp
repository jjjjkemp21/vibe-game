// Lure T-030j tests (unreal-engineer): your own hanging fish wins E. At the shop counter (fish on it) or at an open cooler,
// E takes the fish off your hook first; the counter's Sell shows again once the hook is empty.
// Rules: docs/specs/catch-handling-rules.md "Focus and input".
// Project.Catch.Focus.HangingFishWinsAtCounter, Project.Catch.Focus.HangingFishWinsAtCooler,
// Project.Catch.Focus.OthersHangingFishKeepsSell

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"

namespace LureHangingFishPriorityTest
{
	/** A player at Feet with hands, use keys and progression */
	struct FPlayer
	{
		ALurePlayerCharacter* Pawn = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;
		ULureProgressionComponent* Progression = nullptr;

		bool Spawn(FAutomationTestBase& Test, LCT::FWorld& W, const FVector& Feet)
		{
			Pawn = W.SpawnPlayer(Test, Feet);
			Hands = LCT::HandsOf(Pawn);
			Use = LCT::InteractionOf(Pawn);
			Progression = LCT::ProgressionOf(Pawn);
			return Test.TestNotNull(TEXT("player"), Pawn) && Test.TestNotNull(TEXT("hands"), Hands) && Test.TestNotNull(TEXT("use keys"), Use)
				&& Test.TestNotNull(TEXT("progression"), Progression);
		}

		FLureResolvedInteraction Key(ELureInteractKey InKey) const { return Use->ResolveInteraction(InKey); }
	};

	/** Lands Fish for P, takes it in hand and puts it on Counter (setup shortcut) */
	ALureFishItem* PutOnCounter(LCT::FWorld& W, FPlayer& P, ALureSellCounter* Counter, const FFishInstance& Fish)
	{
		ALureFishItem* Item = W.LandInHand(P.Pawn, Fish);
		return (Item && Counter->AuthorityPlaceFish(P.Pawn, Item)) ? Item : nullptr;
	}

	/** The world with a counter at (150, 0) facing the dock's center and a player at the center looking at it */
	struct FCounterRig
	{
		LCT::FWorld W;
		FPlayer P;
		ALureSellCounter* Counter = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!W.Create(Test) || !P.Spawn(Test, W, FVector(0.0f, 0.0f, LCT::DockTop)))
			{
				return false;
			}
			Counter = W.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
			if (!Test.TestNotNull(TEXT("counter"), Counter))
			{
				return false;
			}
			W.Tick(20);
			return true;
		}

		void LookAtCounter(ALurePlayerCharacter* Pawn)
		{
			LCT::LookAt(Pawn, Counter->GetActorLocation());
			W.Tick(1);
		}
	};
}

// ---- 1. At the counter: E grabs the hanging fish, then Sell comes back ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHangingFishWinsAtCounter, "Project.Catch.Focus.HangingFishWinsAtCounter", LCT::Flags)
bool FHangingFishWinsAtCounter::RunTest(const FString& Parameters)
{
	LureHangingFishPriorityTest::FCounterRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	LureHangingFishPriorityTest::FPlayer& P = Rig.P;
	ALureFishItem* First = LureHangingFishPriorityTest::PutOnCounter(Rig.W, P, Rig.Counter, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 3001));
	if (!TestNotNull(TEXT("a fish lies on the counter"), First))
	{
		return false;
	}
	Rig.LookAtCounter(P.Pawn);
	TestEqual(TEXT("precondition: empty hook, E sells"), LCT::VerbName(P.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::SellCounter));

	// A new catch hangs on the hook while the player looks at the counter.
	ALureFishItem* Hanging = Rig.W.Land(P.Pawn, LCT::MakeFish(TEXT("CoralSnapper"), 30, 1, 1.0f, 3002));
	Rig.W.Tick(5);
	Rig.LookAtCounter(P.Pawn);
	if (!TestTrue(TEXT("the catch hangs on the hook"), Hanging && P.Hands->GetHangingFish() == Hanging))
	{
		return false;
	}
	FLureResolvedInteraction E = P.Key(ELureInteractKey::Primary);
	TestEqual(TEXT("E grabs the hanging fish, not Sell"), LCT::VerbName(E.Verb), LCT::VerbName(ELureInteractVerb::GrabFish));
	TestTrue(TEXT("... its target is the hanging fish"), E.Target == Hanging);
	TestEqual(TEXT("... prompt"), E.Prompt.ToString(), FString(TEXT("Grab the Coral Snapper")));
	TestEqual(TEXT("F lets it go"), LCT::VerbName(P.Key(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::ReleaseFish));
	TestFalse(TEXT("the prompt line has no Sell"), P.Use->GetPromptText().Contains(TEXT("Sell")));

	const int32 Money0 = P.Progression->GetMoney();
	TestTrue(TEXT("E"), P.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("the fish is in the hand"), P.Hands->GetHeldFish() == Hanging && !P.Hands->GetHangingFish());
	TestEqual(TEXT("nothing was sold"), P.Progression->GetMoney(), Money0);
	TestEqual(TEXT("... the counter still has its fish"), Rig.Counter->GetFishOnCounter().Num(), 1);

	TestEqual(TEXT("hand full: E puts it on the counter"), LCT::VerbName(P.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::PlaceFishOnCounter));
	TestTrue(TEXT("E"), P.Use->PressKey(ELureInteractKey::Primary));
	E = P.Key(ELureInteractKey::Primary);
	TestEqual(TEXT("empty hook and hands: Sell shows again"), LCT::VerbName(E.Verb), LCT::VerbName(ELureInteractVerb::SellCounter));
	TestTrue(TEXT("... for both fish"), E.Prompt.ToString().StartsWith(TEXT("Sell 2 fish")));

	// Another catch, let go with F: Sell comes back too.
	ALureFishItem* Third = Rig.W.Land(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 20, 1, 1.5f, 3003));
	Rig.W.Tick(5);
	Rig.LookAtCounter(P.Pawn);
	TestTrue(TEXT("a third catch hangs"), Third && P.Hands->GetHangingFish() == Third);
	TestEqual(TEXT("E grabs it"), LCT::VerbName(P.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::GrabFish));
	TestTrue(TEXT("F"), P.Use->PressKey(ELureInteractKey::Secondary));
	Rig.W.Tick(5);
	TestTrue(TEXT("the hook is empty"), P.Hands->GetHangingFish() == nullptr && !P.Hands->IsHoldingSomething());
	Rig.LookAtCounter(P.Pawn);
	TestEqual(TEXT("Sell shows again"), LCT::VerbName(P.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::SellCounter));
	TestTrue(TEXT("E sells"), P.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("... paid"), P.Progression->GetMoney() > Money0);
	TestEqual(TEXT("... the counter is empty"), Rig.Counter->GetFishOnCounter().Num(), 0);
	return true;
}

// ---- 2. At an open cooler with fish in it: E grabs the hanging fish first ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHangingFishWinsAtCooler, "Project.Catch.Focus.HangingFishWinsAtCooler", LCT::Flags)
bool FHangingFishWinsAtCooler::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureHangingFishPriorityTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureCoolerActor* Cooler = W.SpawnCooler(FVector(90.0f, 0.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("cooler"), Cooler))
	{
		return false;
	}
	Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 3011), W.Now()));
	Cooler->AuthoritySetLidOpen(true);
	W.Tick(30);
	LCT::LookAt(P.Pawn, Cooler->GetInteractionLocation());
	W.Tick(1);
	TestEqual(TEXT("precondition: empty hook, E takes a fish out"), LCT::VerbName(P.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::TakeFishFromCooler));

	ALureFishItem* Hanging = W.Land(P.Pawn, LCT::MakeFish(TEXT("CoralSnapper"), 30, 1, 1.0f, 3012));
	W.Tick(5);
	LCT::LookAt(P.Pawn, Cooler->GetInteractionLocation());
	W.Tick(1);
	TestTrue(TEXT("the catch hangs"), Hanging && P.Hands->GetHangingFish() == Hanging);
	const FLureResolvedInteraction E = P.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("E grabs the hanging fish, not the cooler"), E.Verb == ELureInteractVerb::GrabFish && E.Target == Hanging);
	TestTrue(TEXT("E"), P.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("in hand; the cooler kept its fish"), P.Hands->GetHeldFish() == Hanging && Cooler->GetNumFish() == 1);
	TestEqual(TEXT("then E puts it in the cooler"), LCT::VerbName(P.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::PutFishInCooler));
	return true;
}

// ---- 3. Only YOUR hanging fish takes E: a friend's hanging fish doesn't hide your Sell ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOthersHangingFishKeepsSell, "Project.Catch.Focus.OthersHangingFishKeepsSell", LCT::Flags)
bool FOthersHangingFishKeepsSell::RunTest(const FString& Parameters)
{
	LureHangingFishPriorityTest::FCounterRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	LureHangingFishPriorityTest::FPlayer Friend;
	if (!Friend.Spawn(*this, Rig.W, FVector(0.0f, 60.0f, LCT::DockTop)))
	{
		return false;
	}
	Rig.W.Tick(20);
	TestNotNull(TEXT("a fish lies on the counter"),
		LureHangingFishPriorityTest::PutOnCounter(Rig.W, Rig.P, Rig.Counter, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 3021)));
	ALureFishItem* Theirs = Rig.W.Land(Friend.Pawn, LCT::MakeFish(TEXT("CoralSnapper"), 30, 1, 1.0f, 3022));
	Rig.W.Tick(5);
	TestTrue(TEXT("the friend's catch hangs on their hook"), Theirs && Friend.Hands->GetHangingFish() == Theirs);
	Rig.LookAtCounter(Rig.P.Pawn);
	Rig.LookAtCounter(Friend.Pawn);
	TestEqual(TEXT("my hook is empty: my E sells"), LCT::VerbName(Rig.P.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::SellCounter));
	const FLureResolvedInteraction FriendE = Friend.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("the friend's E grabs their own fish"), FriendE.Verb == ELureInteractVerb::GrabFish && FriendE.Target == Theirs);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

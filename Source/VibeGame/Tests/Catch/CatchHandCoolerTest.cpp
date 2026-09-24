// Lure T-030 tests (unreal-engineer): landing, the hand and the physical cooler in a game world (a dock box over the
// fallback sea; the shipped catch data injected). Rules: docs/specs/catch-handling-rules.md.
// Project.Catch.Landing.*, Project.Catch.Hand.*, Project.Catch.Cooler.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/FPArmsPose.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"

namespace LureCatchHandCoolerTest
{
	/** The player at the dock's center, looking along +X, with its hands, use keys and progression */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;
		ULureProgressionComponent* Progression = nullptr;
		ULureFishingComponent* Fishing = nullptr;

		bool Create(FAutomationTestBase& Test, bool bQuickFreshness = false)
		{
			if (!W.Create(Test, bQuickFreshness))
			{
				return false;
			}
			Player = W.SpawnPlayer(Test, FVector(0.0f, 0.0f, LCT::DockTop));
			Hands = LCT::HandsOf(Player);
			Use = LCT::InteractionOf(Player);
			Progression = LCT::ProgressionOf(Player);
			Fishing = Player ? Player->GetFishing() : nullptr;
			if (!Test.TestNotNull(TEXT("player"), Player) || !Test.TestNotNull(TEXT("hands"), Hands) || !Test.TestNotNull(TEXT("use keys"), Use)
				|| !Test.TestNotNull(TEXT("progression"), Progression) || !Test.TestNotNull(TEXT("fishing"), Fishing))
			{
				return false;
			}
			W.Tick(20); // standing on the dock
			return true;
		}

		FLureResolvedInteraction Key(ELureInteractKey InKey) const { return Use->ResolveInteraction(InKey); }
	};

	/** A cooler standing Ahead cm in front of the player's feet on the dock, its front toward the player */
	ALureCoolerActor* CoolerAhead(FRig& Rig, float Ahead = 130.0f, FName CoolerId = NAME_None)
	{
		return Rig.W.SpawnCooler(FVector(Ahead, 0.0f, LCT::DockTop), 180.0f, CoolerId);
	}

	bool IsOnDock(const FVector& Point)
	{
		return FMath::Abs(Point.Z - LCT::DockTop) < 1.0f && FMath::Abs(Point.X) <= LCT::DockHalf && FMath::Abs(Point.Y) <= LCT::DockHalf;
	}
}

// ---- Landing ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchLandingHangsOnHook, "Project.Catch.Landing.HangsOnHookWithXp", LCT::Flags)
bool FCatchLandingHangsOnHook::RunTest(const FString& Parameters)
{
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	const int32 Xp0 = Rig.Progression->GetTotalXp();
	const double LandedAt = Rig.W.Now();
	const FFishInstance Fish = LCT::MakeFish(TEXT("Bonefish"), 45, 30, 2.0f, 11);
	const FLureFishLandedResult Result = ULureCatchLibrary::HandleFishLanded(Rig.Player, Fish);
	ALureFishItem* Item = Cast<ALureFishItem>(Result.FishItem);
	TestTrue(TEXT("the landing is accepted, the fish on the hook"), Result.bAccepted && Result.bOnHook);
	if (!TestNotNull(TEXT("a fish item"), Item))
	{
		return false;
	}
	TestEqual(TEXT("XP on landing (never on selling)"), Rig.Progression->GetTotalXp(), Xp0 + 30);
	TestEqual(TEXT("... reported"), Result.XpGained, 30);
	TestTrue(TEXT("it hangs on this player's hook"), Item->IsHeldBy(Rig.Player, ELureHoldMode::Hook) && Rig.Hands->GetHangingFish() == Item);
	TestNull(TEXT("the hands stay empty"), Rig.Hands->GetHeldItem());
	TestTrue(TEXT("the item carries the rolled record unchanged"), Item->GetFish().Seed == 11 && Item->GetFish().ToString() == Fish.ToString());
	TestEqual(TEXT("freshness time zero is the landing"), Item->GetCatch().Freshness.AnchorTime, LandedAt, 1.0e-6);
	TestEqual(TEXT("... fresh"), Item->GetFreshness01(), 1.0f, 1.0e-6f);
	TestEqual(TEXT("... worth its rolled value"), Item->GetCurrentValue(), 45);

	Rig.W.Tick(30);
	const float Line = ULureCatchSubsystem::GetTuningFor(Rig.W.World).HangLineLength;
	const FVector Pivot = Rig.Hands->GetHangPivot();
	const FLureHangPendulum& Pendulum = Item->GetPendulum();
	TestTrue(TEXT("the swing runs on this machine"), Pendulum.bInitialized);
	TestTrue(FString::Printf(TEXT("the mouth hangs one line length below the rod tip (%.1f of %.1f cm)"), FVector::Dist(Pendulum.Bob, Pivot), Line),
		FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Pendulum.Bob, Pivot)), Line, 3.0f) && Pendulum.Bob.Z < Pivot.Z - 0.5f * Line);
	TestTrue(TEXT("the actor hangs from its mouth"), Item->GetActorTransform().TransformPosition(Item->GetMouthOffset()).Equals(Pendulum.Bob, 1.0));
	TestEqual(TEXT("casting is refused while a fish hangs (a line is out)"), LCT::BlockName(Rig.Fishing->GetCastBlock()), LCT::BlockName(ELureCastBlock::Busy));
	TestTrue(TEXT("... the rod stays in hand"), Rig.Fishing->IsRodInHand());
	TestEqual(TEXT("the HUD shows the hanging fish"), FString::Join(ULureCatchLibrary::GetPlaceholderLines(LCT::ControllerOf(Rig.Player)), TEXT(" | ")),
		FString(TEXT("On the hook: Bonefish (Common), 2.00 kg, 45 coins, fresh 100%")));

	AddExpectedMessagePlain(TEXT("got an invalid fish"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	const FLureFishLandedResult Bad = ULureCatchLibrary::HandleFishLanded(Rig.Player, FFishInstance());
	TestFalse(TEXT("an invalid fish is refused"), Bad.bAccepted || Bad.bOnHook || Bad.FishItem != nullptr);
	TestTrue(TEXT("... and changes nothing"), Rig.Hands->GetHangingFish() == Item && Rig.Progression->GetTotalXp() == Xp0 + 30);
	return true;
}

// ---- The hand ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchHandGrab, "Project.Catch.Hand.GrabStowsRodAndPosesArms", LCT::Flags)
bool FCatchHandGrab::RunTest(const FString& Parameters)
{
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	ALureFishItem* Item = Rig.W.Land(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 21));
	if (!TestNotNull(TEXT("landed"), Item))
	{
		return false;
	}
	// Looking at nothing: the keys fall back to the fish on the hook.
	FLureResolvedInteraction E = Rig.Key(ELureInteractKey::Primary);
	FLureResolvedInteraction F = Rig.Key(ELureInteractKey::Secondary);
	TestTrue(TEXT("E grabs the hanging fish"), E.Verb == ELureInteractVerb::GrabFish && E.Target == Item);
	TestEqual(TEXT("... prompt"), E.Prompt.ToString(), FString(TEXT("Grab the Bonefish")));
	TestTrue(TEXT("F lets it go"), F.Verb == ELureInteractVerb::ReleaseFish && F.Target == Item);
	TestEqual(TEXT("the prompt line names both keys"), Rig.Use->GetPromptText(), FString(TEXT("[E] Grab the Bonefish   [F] Let the Bonefish go")));

	TestTrue(TEXT("E"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("the fish is in the hand"), Rig.Hands->GetHeldFish() == Item && Item->IsHeldBy(Rig.Player, ELureHoldMode::Hand));
	TestNull(TEXT("... off the hook"), Rig.Hands->GetHangingFish());
	TestTrue(TEXT("the rod is stowed"), ULureHandsComponent::IsRodStowedFor(Rig.Player) && !Rig.Fishing->IsRodInHand());
	TestEqual(TEXT("no fishing with a fish in hand"), LCT::BlockName(Rig.Fishing->GetCastBlock()), LCT::BlockName(ELureCastBlock::NoRod));
	Rig.W.Tick(3);
	TestEqual(TEXT("the arms hold the fish"), static_cast<int32>(Rig.Player->GetArmsPose()), static_cast<int32>(EFPArmsPose::HoldFish));
	TestFalse(TEXT("the body shows no rod"), Rig.Player->IsHoldingRod());
	TestEqual(TEXT("walking speed is unchanged with a fish"), Rig.Hands->GetMoveSpeedMultiplier(), 1.0f);

	const FString Held = TEXT("Holding: Bonefish (Common), 1.50 kg, 45 coins, fresh 100%");
	TestEqual(TEXT("the HUD line: name, weight, value, freshness"), FString::Join(ULureCatchLibrary::GetPlaceholderLines(LCT::ControllerOf(Rig.Player)), TEXT(" | ")), Held);
	E = Rig.Key(ELureInteractKey::Primary);
	F = Rig.Key(ELureInteractKey::Secondary);
	TestFalse(TEXT("E does nothing with a fish in hand and nothing looked at"), E.HasVerb());
	TestTrue(TEXT("F drops it"), F.Verb == ELureInteractVerb::DropFish && F.Target == Item && F.Prompt.ToString() == TEXT("Drop the Bonefish"));
	const TArray<FString> Status = ULureProgressionLibrary::GetPlaceholderStatusLines(LCT::ControllerOf(Rig.Player));
	TestTrue(TEXT("the placeholder HUD shows the held fish and the prompt"), Status.Contains(Held) && Status.Contains(TEXT("[F] Drop the Bonefish")));

	// Debug tools can land a second fish while the hand is full: it waits on the hook; E does nothing, F drops the hand's fish.
	ALureFishItem* Second = Rig.W.Land(Rig.Player, LCT::MakeFish(TEXT("CoralSnapper"), 30, 5, 1.0f, 22));
	TestTrue(TEXT("a second fish hangs"), Second && Rig.Hands->GetHangingFish() == Second);
	TestFalse(TEXT("no grab with a full hand"), Rig.Key(ELureInteractKey::Primary).HasVerb());
	TestFalse(TEXT("... the server refuses it too"), Rig.Hands->AuthorityTakeInHand(Second));
	F = Rig.Key(ELureInteractKey::Secondary);
	TestTrue(TEXT("F drops the fish in hand first"), F.Verb == ELureInteractVerb::DropFish && F.Target == Item);
	TestTrue(TEXT("F"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestTrue(TEXT("... it lies on the dock"), Item->IsFree() && IsValid(Item));
	E = Rig.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("then E grabs the second fish"), E.Verb == ELureInteractVerb::GrabFish && E.Target == Second && E.Prompt.ToString() == TEXT("Grab the Coral Snapper"));
	TestTrue(TEXT("E"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("... in hand"), Rig.Hands->GetHeldFish() == Second && !Rig.Hands->GetHangingFish());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchHandDrop, "Project.Catch.Hand.DropOnLandReleaseInWater", LCT::Flags)
bool FCatchHandDrop::RunTest(const FString& Parameters)
{
	using LureCatchHandCoolerTest::IsOnDock;
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	ALureFishItem* Item = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 31));
	if (!TestNotNull(TEXT("a fish in hand"), Item))
	{
		return false;
	}
	const FVector Eye = Rig.Player->GetPawnViewLocation();
	const float Forward = ULureCatchSubsystem::GetTuningFor(Rig.W.World).DropForward;
	TestTrue(TEXT("F"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	const FVector Rest = Item->GetActorLocation();
	TestTrue(TEXT("dropped: it lies free, the hands are empty, the rod is back"), Item->IsFree() && !Rig.Hands->IsHoldingSomething() && Rig.Fishing->IsRodInHand());
	TestTrue(FString::Printf(TEXT("it lands DropForward in front, on the dock (%s)"), *Rest.ToCompactString()),
		IsOnDock(Rest) && FMath::IsNearlyEqual(static_cast<float>(Rest.X), static_cast<float>(Eye.X) + Forward, 2.0f) && FMath::IsNearlyEqual(static_cast<float>(Rest.Y), static_cast<float>(Eye.Y), 2.0f));
	TestTrue(TEXT("its mesh is tossed there (a short flight)"), Item->GetPlacement().bAnimate && Item->GetFlightTimeLeft() > 0.0f);
	Rig.W.Tick(40);
	TestEqual(TEXT("... and rests"), Item->GetFlightTimeLeft(), 0.0f);
	TestTrue(TEXT("... lying on its side on the planks"), FMath::IsNearlyEqual(static_cast<float>(Item->GetVisualTransform().GetLocation().Z), static_cast<float>(Rest.Z) + Item->GetLieHeight(), 0.5f));
	TestEqual(TEXT("a loose fish keeps spoiling (rate 1)"), Item->GetCatch().Freshness.Rate, 1.0f);

	// A loose fish is picked up again with E.
	LCT::LookAt(Rig.Player, Item->GetActorLocation());
	const FLureResolvedInteraction PickUp = Rig.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("E picks the loose fish up"), PickUp.Verb == ELureInteractVerb::GrabFish && PickUp.Target == Item && PickUp.Prompt.ToString() == TEXT("Pick up the Bonefish"));
	TestTrue(TEXT("E"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("... in hand"), Rig.Hands->GetHeldFish() == Item);

	// A drop that falls in the water releases the fish.
	LCT::PlaceAt(Rig.Player, FVector(LCT::DockHalf - 30.0f, 0.0f, LCT::DockTop), 0.0f);
	Rig.W.Tick(5);
	Rig.Progression->ClearNotices();
	const TWeakObjectPtr<ALureFishItem> Weak = Item;
	TestTrue(TEXT("F at the dock's edge"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestFalse(TEXT("into the water: the fish is released (gone)"), Weak.IsValid());
	TestTrue(TEXT("... the hands are empty"), !Rig.Hands->IsHoldingSomething());
	TestTrue(FString::Printf(TEXT("... \"Released the Bonefish\" (%s)"), *LCT::NoticesOf(Rig.Player)), LCT::NoticesOf(Rig.Player).Contains(TEXT("Released the Bonefish")));

	// A wall right in front stops the toss: the fish lands just before it, never inside it.
	LCT::PlaceAt(Rig.Player, FVector(0.0f, 0.0f, LCT::DockTop), 0.0f);
	Rig.W.AddBox(FVector(45.0f, 0.0f, LCT::DockTop + 100.0f), FVector(5.0f, 100.0f, 100.0f));
	Rig.W.Tick(5);
	ALureFishItem* Walled = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 32));
	TestTrue(TEXT("F facing a wall"), Walled && Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestTrue(FString::Printf(TEXT("... it lands in front of the wall (%s)"), Walled ? *Walled->GetActorLocation().ToCompactString() : TEXT("-")),
		Walled && Walled->IsFree() && IsOnDock(Walled->GetActorLocation()) && Walled->GetActorLocation().X < 40.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchHandLetGo, "Project.Catch.Hand.LetGoFromTheHook", LCT::Flags)
bool FCatchHandLetGo::RunTest(const FString& Parameters)
{
	using LureCatchHandCoolerTest::IsOnDock;
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	LCT::UseEstimatedRodTip(Rig.Player);
	ALureFishItem* Item = Rig.W.Land(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 41));
	if (!TestNotNull(TEXT("landed"), Item))
	{
		return false;
	}
	Rig.W.Tick(10);
	const FVector Pivot = Rig.Hands->GetHangPivot();
	const float TipAhead = static_cast<float>(Pivot.X - Rig.Player->GetActorLocation().X);
	TestTrue(FString::Printf(TEXT("QA precondition: the rod tip is ahead of the player (%.0f cm)"), TipAhead), TipAhead > 60.0f);
	TestTrue(TEXT("F over the dock"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	const FVector Rest = Item->GetActorLocation();
	TestTrue(FString::Printf(TEXT("it drops straight down under the rod tip onto the dock (%s, tip %s)"), *Rest.ToCompactString(), *Pivot.ToCompactString()),
		Item->IsFree() && IsOnDock(Rest) && FVector2D::Distance(FVector2D(Rest), FVector2D(Pivot)) < 2.0f);
	TestNull(TEXT("the hook is free"), Rig.Hands->GetHangingFish());
	TestEqual(TEXT("casting works again"), LCT::BlockName(Rig.Fishing->GetCastBlock()), LCT::BlockName(ELureCastBlock::None));
	TestTrue(TEXT("the loose fish can't be let go again from afar (it isn't on the hook)"), !Item->GetInteraction(Rig.Player, ELureInteractKey::Secondary).HasVerb());

	// At the edge, facing the water, the rod tip is over the water: letting go releases it.
	LCT::PlaceAt(Rig.Player, FVector(LCT::DockHalf - 40.0f, 0.0f, LCT::DockTop), 0.0f);
	Rig.W.Tick(5);
	ALureFishItem* Second = Rig.W.Land(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 42));
	Rig.W.Tick(5);
	Rig.Progression->ClearNotices();
	const TWeakObjectPtr<ALureFishItem> Weak = Second;
	TestTrue(FString::Printf(TEXT("QA precondition: the tip is over the water (x %.0f)"), Rig.Hands->GetHangPivot().X), Rig.Hands->GetHangPivot().X > LCT::DockHalf + 10.0f);
	TestTrue(TEXT("F at the edge"), Second && Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestFalse(TEXT("released into the water (gone)"), Weak.IsValid());
	TestTrue(TEXT("... notice"), LCT::NoticesOf(Rig.Player).Contains(TEXT("Released the Bonefish")));
	TestNull(TEXT("... the hook is free"), Rig.Hands->GetHangingFish());
	return true;
}

// ---- The cooler ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCoolerPutInTakeOut, "Project.Catch.Cooler.PutInTakeOutCapacityFour", LCT::Flags)
bool FCatchCoolerPutInTakeOut::RunTest(const FString& Parameters)
{
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	ALureCoolerActor* Cooler = LureCatchHandCoolerTest::CoolerAhead(Rig);
	if (!TestNotNull(TEXT("cooler"), Cooler))
	{
		return false;
	}
	TestEqual(TEXT("the starter cooler holds 4"), Cooler->GetCapacity(), 4);
	LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
	Rig.W.Tick(2);
	TestTrue(TEXT("looking at the cooler focuses it"), Rig.Use->FindFocusedInteractable() == Cooler);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		ALureFishItem* Fish = Rig.W.Land(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 10 + Index, 1, 1.5f, 100 + Index));
		Rig.W.Tick(2);
		const FLureResolvedInteraction Grab = Rig.Key(ELureInteractKey::Primary);
		TestTrue(FString::Printf(TEXT("fish %d: the cooler waits while a fish hangs; E grabs it"), Index), Fish && Grab.Verb == ELureInteractVerb::GrabFish && Grab.Target == Fish);
		Rig.Use->PressKey(ELureInteractKey::Primary);
		const FLureResolvedInteraction Put = Rig.Key(ELureInteractKey::Primary);
		TestTrue(FString::Printf(TEXT("fish %d: E puts it in the cooler"), Index), Put.Verb == ELureInteractVerb::PutFishInCooler && Put.Target == Cooler);
		TestEqual(FString::Printf(TEXT("fish %d: prompt"), Index), Put.Prompt.ToString(), FString::Printf(TEXT("Put the Bonefish in the cooler (%d/4)"), Index));
		TestTrue(FString::Printf(TEXT("fish %d: E"), Index), Rig.Use->PressKey(ELureInteractKey::Primary));
		TestEqual(FString::Printf(TEXT("fish %d: in the cooler"), Index), Cooler->GetNumFish(), Index + 1);
		TestFalse(FString::Printf(TEXT("fish %d: the item is gone (only the record goes in)"), Index), IsValid(Fish));
		TestFalse(FString::Printf(TEXT("fish %d: the hands are empty"), Index), Rig.Hands->IsHoldingSomething());
		TestFalse(FString::Printf(TEXT("fish %d: a closed lid stays closed"), Index), Cooler->IsLidOpen());
	}
	FLureCaughtFish Record;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestTrue(FString::Printf(TEXT("slot %d keeps the record (seed %d), in order"), Index, 100 + Index),
			Cooler->GetStorage()->GetFishAt(Index, Record) && Record.Fish.Seed == 100 + Index && Record.Fish.Value == 10 + Index);
		TestEqual(FString::Printf(TEXT("slot %d: closed = no spoiling"), Index), Record.Freshness.Rate, 0.0f);
	}

	// Full: E shows it, F still drops the fish in hand.
	ALureFishItem* Fifth = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 20, 1, 1.5f, 104));
	const FLureResolvedInteraction Full = Rig.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("a full cooler says so"), Fifth && !Full.HasVerb() && Full.Target == Cooler && Full.Prompt.ToString() == TEXT("Cooler full (4/4)"));
	TestFalse(TEXT("E does nothing"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestEqual(TEXT("prompt"), Rig.Use->GetPromptText(), FString(TEXT("Cooler full (4/4)   [F] Drop the Bonefish")));
	TestFalse(TEXT("the server refuses a fifth fish too"), Cooler->AuthorityPutFishIn(Rig.Player, Fifth));
	TestEqual(TEXT("... still 4"), Cooler->GetNumFish(), 4);
	Rig.Hands->AuthorityReleaseHeld();
	if (Fifth)
	{
		Fifth->Destroy();
	}

	// Empty hands: E opens, F picks up; open: E takes the top fish out, F closes.
	FLureResolvedInteraction E = Rig.Key(ELureInteractKey::Primary);
	FLureResolvedInteraction F = Rig.Key(ELureInteractKey::Secondary);
	TestTrue(TEXT("E opens"), E.Verb == ELureInteractVerb::OpenCooler && E.Prompt.ToString() == TEXT("Open the cooler (4/4)"));
	TestTrue(TEXT("F picks it up"), F.Verb == ELureInteractVerb::PickUpCooler && F.Prompt.ToString() == TEXT("Pick up the cooler"));
	TestTrue(TEXT("E"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("open: the fish inside spoil at the open rate"), Cooler->IsLidOpen() && FMath::IsNearlyEqual(Cooler->GetDecayRate(), Cooler->GetRow().OpenDecayRate));
	E = Rig.Key(ELureInteractKey::Primary);
	F = Rig.Key(ELureInteractKey::Secondary);
	TestTrue(TEXT("E takes the top fish out"), E.Verb == ELureInteractVerb::TakeFishFromCooler && E.Prompt.ToString() == TEXT("Take out the Bonefish (4/4)"));
	TestTrue(TEXT("F closes"), F.Verb == ELureInteractVerb::CloseCooler && F.Prompt.ToString() == TEXT("Close the cooler (4/4)"));
	TestTrue(TEXT("E"), Rig.Use->PressKey(ELureInteractKey::Primary));
	ALureFishItem* Out = Rig.Hands->GetHeldFish();
	TestTrue(TEXT("the last one put in comes out into the hand"), Out && Out->GetFish().Seed == 103 && Cooler->GetNumFish() == 3);
	TestEqual(TEXT("... spoiling again outside"), Out ? Out->GetCatch().Freshness.Rate : -1.0f, 1.0f);
	E = Rig.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("E puts it back (open lid)"), E.Verb == ELureInteractVerb::PutFishInCooler && E.Prompt.ToString() == TEXT("Put the Bonefish in the cooler (3/4)"));
	TestTrue(TEXT("E"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("back in, the lid still open"), Cooler->GetNumFish() == 4 && Cooler->IsLidOpen());
	TestTrue(TEXT("F closes it"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestTrue(TEXT("closed: no spoiling"), !Cooler->IsLidOpen() && Cooler->GetDecayRate() == Cooler->GetRow().ClosedDecayRate);
	TestNull(TEXT("nothing comes out of a closed cooler"), Cooler->AuthorityTakeFishOut(Rig.Player));
	TestEqual(TEXT("summary"), Cooler->GetSummary(), FString(TEXT("Starter cooler (4/4, closed)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCoolerLidHoldsFreshness, "Project.Catch.Cooler.LidHoldsFreshness", LCT::Flags)
bool FCatchCoolerLidHoldsFreshness::RunTest(const FString& Parameters)
{
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this, /*bQuickFreshness*/ true))
	{
		return false;
	}
	ALureCoolerActor* Cooler = LureCatchHandCoolerTest::CoolerAhead(Rig);
	ALureFishItem* Fish = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 40, 1, 1.5f, 51));
	if (!TestNotNull(TEXT("cooler"), Cooler) || !TestNotNull(TEXT("fish in hand"), Fish))
	{
		return false;
	}
	LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
	const FLureFreshnessRow Quick = ULureCatchSubsystem::Get(Rig.W.World)->GetFreshnessRow(TEXT("Bonefish"));
	TestEqual(TEXT("QA precondition: the quick profile (grace 1 s, spoil 2 s)"), Quick.SpoilSeconds, 2.0f);

	Rig.W.Advance(1.5f);
	const float InHand = Fish->GetExposureSeconds();
	TestTrue(FString::Printf(TEXT("in the hand it spoils in real time (%.2f s after 1.5 s)"), InHand), FMath::IsNearlyEqual(InHand, 1.5f, 0.15f));
	TestEqual(TEXT("freshness follows the curve"), Fish->GetFreshness01(), FLureFreshness::GetFreshness01(Quick, InHand), 1.0e-4f);
	TestTrue(TEXT("... it has lost some"), Fish->GetFreshness01() < 1.0f && Fish->GetCurrentValue() < 40);

	TestTrue(TEXT("E puts it in the closed cooler"), Rig.Use->PressKey(ELureInteractKey::Primary));
	FLureCaughtFish Stored;
	TestTrue(TEXT("stored"), Cooler->GetStorage()->GetFishAt(0, Stored));
	const float Inside = Stored.Freshness.GetExposure(Rig.W.Now());
	TestEqual(TEXT("the record keeps its exposure"), Inside, InHand, 1.0e-3f);
	Rig.W.Advance(10.0f);
	Cooler->GetStorage()->GetFishAt(0, Stored);
	TestEqual(TEXT("a closed cooler holds the freshness (10 s later)"), Stored.Freshness.GetExposure(Rig.W.Now()), Inside, 1.0e-4f);

	TestTrue(TEXT("open"), Cooler->AuthoritySetLidOpen(true));
	Rig.W.Advance(1.0f);
	Cooler->GetStorage()->GetFishAt(0, Stored);
	const float Open = Stored.Freshness.GetExposure(Rig.W.Now());
	TestTrue(FString::Printf(TEXT("an open cooler spoils like outside (+%.2f s in 1 s)"), Open - Inside), FMath::IsNearlyEqual(Open - Inside, 1.0f, 0.15f));
	TestTrue(TEXT("close"), Cooler->AuthoritySetLidOpen(false));
	const float Closed = [&]() { Cooler->GetStorage()->GetFishAt(0, Stored); return Stored.Freshness.GetExposure(Rig.W.Now()); }();
	Rig.W.Advance(5.0f);
	Cooler->GetStorage()->GetFishAt(0, Stored);
	TestEqual(TEXT("closed again: it holds"), Stored.Freshness.GetExposure(Rig.W.Now()), Closed, 1.0e-4f);

	TestTrue(TEXT("open to take it out"), Cooler->AuthoritySetLidOpen(true));
	ALureFishItem* Out = Cooler->AuthorityTakeFishOut(Rig.Player);
	if (!TestNotNull(TEXT("out"), Out))
	{
		return false;
	}
	const float OutNow = Out->GetExposureSeconds();
	TestEqual(TEXT("it comes out with the same exposure"), OutNow, Closed, 0.05f);
	Rig.W.Advance(1.0f);
	TestTrue(TEXT("... and spoils again in the hand"), FMath::IsNearlyEqual(Out->GetExposureSeconds() - OutNow, 1.0f, 0.15f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCoolerCarry, "Project.Catch.Cooler.CarryBlocksFishingAndSlows", LCT::Flags)
bool FCatchCoolerCarry::RunTest(const FString& Parameters)
{
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	ALureCoolerActor* Cooler = LureCatchHandCoolerTest::CoolerAhead(Rig);
	if (!TestNotNull(TEXT("cooler"), Cooler))
	{
		return false;
	}
	Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 61), Rig.W.Now()));
	Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 62), Rig.W.Now()));
	Cooler->AuthoritySetLidOpen(true);
	LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
	Rig.W.Tick(2);
	const float Walk = Rig.Player->GetLureMovement()->GetMaxSpeed();

	TestEqual(TEXT("an open cooler: F closes it first"), LCT::VerbName(Rig.Key(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::CloseCooler));
	TestTrue(TEXT("F"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestEqual(TEXT("closed: F picks it up"), LCT::VerbName(Rig.Key(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::PickUpCooler));
	TestTrue(TEXT("F"), Rig.Use->PressKey(ELureInteractKey::Secondary));
	TestTrue(TEXT("carried in both hands"), Rig.Hands->GetCarriedCooler() == Cooler && Cooler->IsHeldBy(Rig.Player, ELureHoldMode::Hand));
	TestEqual(TEXT("... its fish stay inside"), Cooler->GetNumFish(), 2);
	TestEqual(TEXT("no fishing while carrying"), LCT::BlockName(Rig.Fishing->GetCastBlock()), LCT::BlockName(ELureCastBlock::NoRod));
	TestFalse(TEXT("... the rod is stowed"), Rig.Fishing->IsRodInHand());
	Rig.W.Tick(3);
	TestEqual(TEXT("the arms carry the cooler"), static_cast<int32>(Rig.Player->GetArmsPose()), static_cast<int32>(EFPArmsPose::CarryCooler));
	const float Multiplier = Cooler->GetRow().CarrySpeedMultiplier;
	TestEqual(TEXT("QA precondition: DT_Cooler Starter carry multiplier"), Multiplier, 0.8f, 1.0e-4f);
	TestEqual(TEXT("carrying slows you by the row's multiplier"), Rig.Player->GetLureMovement()->GetMaxSpeed(), Walk * Multiplier, 0.5f);
	TestEqual(TEXT("... the hands report it"), Rig.Hands->GetMoveSpeedMultiplier(), Multiplier, 1.0e-4f);
	TestEqual(TEXT("a carried cooler doesn't collide"), static_cast<int32>(Cooler->GetCollisionBox()->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
	TestFalse(TEXT("its lid can't be opened while carried"), Cooler->AuthoritySetLidOpen(true));
	const FLureResolvedInteraction E = Rig.Key(ELureInteractKey::Primary);
	const FLureResolvedInteraction F = Rig.Key(ELureInteractKey::Secondary);
	TestTrue(TEXT("both keys put it down"), E.Verb == ELureInteractVerb::PutDownCooler && F.Verb == ELureInteractVerb::PutDownCooler && E.Target == Cooler);
	TestEqual(TEXT("HUD"), FString::Join(ULureCatchLibrary::GetPlaceholderLines(LCT::ControllerOf(Rig.Player)), TEXT(" | ")), FString(TEXT("Carrying: Starter cooler (2/4, closed)")));
	ALureFishItem* Loose = ALureFishItem::SpawnFish(Rig.W.World, FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 5, 1, 1.5f, 63), Rig.W.Now()),
		FTransform(FVector(60.0f, 40.0f, LCT::DockTop)));
	TestTrue(TEXT("full hands: a loose fish can't be picked up"), Loose && Loose->GetInteraction(Rig.Player, ELureInteractKey::Primary).IsEmpty());
	TestFalse(TEXT("... nor by the server"), Rig.Hands->AuthorityTakeInHand(Loose));

	// It moves with the player.
	LCT::PlaceAt(Rig.Player, FVector(0.0f, 200.0f, LCT::DockTop), 0.0f);
	Rig.W.Tick(2);
	TestTrue(TEXT("the carried cooler goes along"), FVector::Dist2D(Cooler->GetActorLocation(), Rig.Player->GetActorLocation()) < 150.0f);

	TestTrue(TEXT("E puts it down"), Rig.Use->PressKey(ELureInteractKey::Primary));
	const FVector Down = Cooler->GetActorLocation();
	const float Distance = ULureCatchSubsystem::GetTuningFor(Rig.W.World).PutDownDistance;
	TestTrue(TEXT("free"), Cooler->IsFree() && !Rig.Hands->IsHoldingSomething());
	TestTrue(FString::Printf(TEXT("it stands PutDownDistance in front, on the dock (%s)"), *Down.ToCompactString()),
		LureCatchHandCoolerTest::IsOnDock(Down) && FMath::IsNearlyEqual(static_cast<float>(FVector::Dist2D(Down, Rig.Player->GetActorLocation())), Distance, 2.0f)
		&& Down.X > Rig.Player->GetActorLocation().X);
	TestTrue(TEXT("... its front toward the player"), FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(Cooler->GetActorRotation().Yaw) - 180.0f)) < 1.0f);
	TestEqual(TEXT("... it collides again"), static_cast<int32>(Cooler->GetCollisionBox()->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::QueryAndPhysics));
	TestTrue(TEXT("the rod is back, full speed"), Rig.Fishing->IsRodInHand() && FMath::IsNearlyEqual(Rig.Player->GetLureMovement()->GetMaxSpeed(), Walk, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCoolerPutDownRoom, "Project.Catch.Cooler.PutDownNeedsRoom", LCT::Flags)
bool FCatchCoolerPutDownRoom::RunTest(const FString& Parameters)
{
	LureCatchHandCoolerTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	ALureCoolerActor* Cooler = LureCatchHandCoolerTest::CoolerAhead(Rig);
	if (!TestNotNull(TEXT("cooler"), Cooler) || !TestTrue(TEXT("picked up"), Cooler->AuthorityPickUp(Rig.Player)))
	{
		return false;
	}
	Rig.W.Tick(2);
	AActor* Wall = Rig.W.AddBox(FVector(100.0f, 0.0f, LCT::DockTop + 100.0f), FVector(30.0f, 200.0f, 100.0f));
	Rig.Progression->ClearNotices();
	FTransform Spot;
	TestFalse(TEXT("a wall in front: no room"), Cooler->FindPutDownSpot(Rig.Player, Spot));
	TestFalse(TEXT("E refuses"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("... it stays in the hands"), Rig.Hands->GetCarriedCooler() == Cooler);
	TestTrue(FString::Printf(TEXT("... and says why (%s)"), *LCT::NoticesOf(Rig.Player)), LCT::NoticesOf(Rig.Player).Contains(TEXT("No room to put the cooler down here")));
	Wall->Destroy();
	Rig.W.Tick(1);
	TestTrue(TEXT("room again without the wall"), Cooler->FindPutDownSpot(Rig.Player, Spot));

	LCT::PlaceAt(Rig.Player, FVector(LCT::DockHalf - 40.0f, 0.0f, LCT::DockTop), 0.0f);
	Rig.W.Tick(3);
	TestFalse(TEXT("facing the water at the edge: no floor to stand it on"), Cooler->FindPutDownSpot(Rig.Player, Spot));
	TestFalse(TEXT("... refused"), Cooler->AuthorityPutDown());
	TestTrue(TEXT("... still carried"), Rig.Hands->GetCarriedCooler() == Cooler);
	LCT::PlaceAt(Rig.Player, FVector(LCT::DockHalf - 40.0f, 0.0f, LCT::DockTop), 180.0f);
	Rig.W.Tick(3);
	TestTrue(TEXT("turned toward the dock it goes down"), Cooler->AuthorityPutDown());
	TestTrue(TEXT("... on the dock"), Cooler->IsFree() && LureCatchHandCoolerTest::IsOnDock(Cooler->GetActorLocation()));

	// Going prone puts a carried cooler down by itself (hide first, come back for it).
	TestTrue(TEXT("picked up again"), Cooler->AuthorityPickUp(Rig.Player));
	Rig.Player->RequestStance(ELureStance::Prone);
	const bool bDown = Rig.W.TickUntil([&]() { return Cooler->IsFree(); }, 180);
	TestTrue(TEXT("prone: the cooler is put down"), bDown && !Rig.Hands->IsHoldingSomething());
	TestTrue(FString::Printf(TEXT("... standing on the dock (%s)"), *Cooler->GetActorLocation().ToCompactString()), LureCatchHandCoolerTest::IsOnDock(Cooler->GetActorLocation()));
	TestTrue(TEXT("QA precondition: prone"), Rig.Player->IsProne());
	TestFalse(TEXT("... a cooler can't be picked up lying down"), Cooler->AuthorityPickUp(Rig.Player));
	const FLureInteraction Carry = Cooler->GetInteraction(Rig.Player, ELureInteractKey::Secondary);
	TestTrue(TEXT("... F says so instead"), !Carry.HasVerb() && Carry.Prompt.ToString() == TEXT("Stand up to carry the cooler"));
	TestEqual(TEXT("... E still opens it"), LCT::VerbName(Cooler->GetInteraction(Rig.Player, ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::OpenCooler));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCoolerFallingIn, "Project.Catch.Cooler.FallingInDropsAtLastDryGround", LCT::Flags)
bool FCatchCoolerFallingIn::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	if (!W.Create(*this) || !TestNotNull(TEXT("water"), W.AddWater(400.0f)))
	{
		return false;
	}
	ALurePlayerCharacter* Carrier = W.SpawnPlayer(*this, FVector(0.0f, -200.0f, LCT::DockTop));
	ALurePlayerCharacter* Holder = W.SpawnPlayer(*this, FVector(0.0f, 200.0f, LCT::DockTop));
	ALureCoolerActor* Cooler = W.SpawnCooler(FVector(130.0f, -200.0f, LCT::DockTop), 180.0f);
	if (!Carrier || !Holder || !TestNotNull(TEXT("cooler"), Cooler))
	{
		return false;
	}
	W.Tick(20);
	TestTrue(TEXT("one player carries the cooler"), Cooler->AuthorityPickUp(Carrier));
	ALureFishItem* Fish = W.LandInHand(Holder, LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 71));
	TestNotNull(TEXT("the other holds a fish"), Fish);
	W.Tick(30); // both stand on dry ground a while
	FVector DryCarrier;
	FVector DryHolder;
	TestTrue(TEXT("the last dry ground spots are known"), LCT::HandsOf(Carrier)->GetLastDryGround(DryCarrier) && LCT::HandsOf(Holder)->GetLastDryGround(DryHolder));
	TestTrue(TEXT("... at their feet"), DryCarrier.Equals(LCT::FeetOf(Carrier), 5.0) && DryHolder.Equals(LCT::FeetOf(Holder), 5.0));

	Carrier->SetActorLocation(FVector(1000.0f, -200.0f, -20.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Holder->SetActorLocation(FVector(1000.0f, 200.0f, -20.0f), false, nullptr, ETeleportType::TeleportPhysics);
	const bool bSwimming = W.TickUntil([&]() { return Carrier->IsSwimming() && Holder->IsSwimming(); }, 120);
	if (!TestTrue(TEXT("QA precondition: both fell in (swimming)"), bSwimming))
	{
		return false;
	}
	W.Tick(2);
	TestTrue(TEXT("the cooler went back to the carrier's last dry spot"), Cooler->IsFree() && FVector2D::Distance(FVector2D(Cooler->GetActorLocation()), FVector2D(DryCarrier)) < 1.0f
		&& LureCatchHandCoolerTest::IsOnDock(Cooler->GetActorLocation()));
	TestTrue(TEXT("the fish went back to the holder's last dry spot (not lost)"), IsValid(Fish) && Fish->IsFree() && Fish->GetActorLocation().Equals(DryHolder, 1.0));
	TestTrue(TEXT("both players' hands are empty"), !LCT::HandsOf(Carrier)->IsHoldingSomething() && !LCT::HandsOf(Holder)->IsHoldingSomething());
	TestFalse(TEXT("swimming: the hands can't take anything"), LCT::HandsOf(Carrier)->CanHoldItems() || LCT::HandsOf(Carrier)->AuthorityTakeInHand(Fish));
	TestTrue(TEXT("... nor offer it"), Fish->GetInteraction(Carrier, ELureInteractKey::Primary).IsEmpty() && Cooler->GetInteraction(Carrier, ELureInteractKey::Secondary).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCoolerRows, "Project.Catch.Cooler.RowsAndStorageRules", LCT::Flags)
bool FCatchCoolerRows::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	ALureCoolerActor* Large = W.SpawnCooler(FVector(0.0f, 300.0f, LCT::DockTop), 0.0f, TEXT("Large"));
	AddExpectedMessagePlain(TEXT("DT_Cooler has no row 'Nope'"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	ALureCoolerActor* Unknown = W.SpawnCooler(FVector(0.0f, -300.0f, LCT::DockTop), 0.0f, TEXT("Nope"));
	ALureCoolerActor* Default = W.SpawnCooler(FVector(300.0f, 0.0f, LCT::DockTop));
	if (!TestNotNull(TEXT("large"), Large) || !TestNotNull(TEXT("unknown"), Unknown) || !TestNotNull(TEXT("default"), Default))
	{
		return false;
	}
	TestTrue(TEXT("a Large cooler: its row"), Large->GetCoolerId() == TEXT("Large") && Large->GetCapacity() == 8);
	TestEqual(TEXT("... name and summary"), Large->GetSummary(), FString(TEXT("Large cooler (0/8, closed)")));
	TestEqual(TEXT("... carry multiplier"), Large->GetCarrySpeedMultiplier(), 0.7f, 1.0e-4f);
	TestTrue(TEXT("an unknown row becomes the default row"), Unknown->GetCoolerId() == TEXT("Starter") && Unknown->GetCapacity() == 4);
	TestTrue(TEXT("no row: the default row"), Default->GetCoolerId() == TEXT("Starter") && Default->GetCapacity() == 4);
	const FVector Half = Default->GetBoxHalfExtent();
	TestTrue(FString::Printf(TEXT("the box is the starter model's size (%s)"), *Half.ToCompactString()),
		FMath::IsNearlyEqual(Half.X, 22.0, 1.5) && FMath::IsNearlyEqual(Half.Y, 32.5, 1.5) && FMath::IsNearlyEqual(Half.Z, 19.6, 1.5));
	TestTrue(TEXT("coolers made for nobody still work (owner = none)"), Default->GetOwningPlayerState() == nullptr && Default->GetCoolerGuid().IsValid());
	TestNotEqual(TEXT("every cooler has its own save identity"), Default->GetCoolerGuid(), Large->GetCoolerGuid());

	ULureCoolerComponent* Storage = Default->GetStorage();
	AddExpectedMessagePlain(TEXT("AddFish got an invalid fish"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("an invalid record is refused"), Storage->AddFish(FLureCaughtFish()));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestTrue(FString::Printf(TEXT("fish %d fits"), Index), Storage->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 80 + Index), W.Now())));
	}
	TestFalse(TEXT("a fifth doesn't"), Storage->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 84), W.Now())));
	TestTrue(TEXT("full"), Default->IsFull());
	FLureCaughtFish Top;
	TestTrue(TEXT("the top of the pile is the last one in"), Storage->RemoveLastFish(Top) && Top.Fish.Seed == 83 && Default->GetNumFish() == 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

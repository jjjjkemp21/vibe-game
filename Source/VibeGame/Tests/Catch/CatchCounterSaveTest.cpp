// Lure T-030 tests (unreal-engineer): the sell counter, focus and verbs, getting caught, saves, the starter cooler, the
// cooler's contents display and the T-029 / T-032 seams, in a game world. Rules: docs/specs/catch-handling-rules.md.
// Project.Catch.Counter.*, Project.Catch.Interact.*, Project.Catch.Caught.*, Project.Catch.Save.*, Project.Catch.Starter.*,
// Project.Catch.Display.*, Project.Catch.Seams.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Misc/ScopeExit.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace LureCatchCounterSaveTest
{
	/** One player at Feet looking along +X, with the parts the tests use */
	struct FPlayer
	{
		ALurePlayerCharacter* Pawn = nullptr;
		ALurePlayerState* State = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;
		ULureProgressionComponent* Progression = nullptr;

		bool Spawn(FAutomationTestBase& Test, LCT::FWorld& W, const FVector& Feet)
		{
			Pawn = W.SpawnPlayer(Test, Feet);
			State = Pawn ? Pawn->GetPlayerState<ALurePlayerState>() : nullptr;
			Hands = LCT::HandsOf(Pawn);
			Use = LCT::InteractionOf(Pawn);
			Progression = LCT::ProgressionOf(Pawn);
			return Test.TestNotNull(TEXT("player"), Pawn) && Test.TestNotNull(TEXT("player state"), State) && Test.TestNotNull(TEXT("hands"), Hands)
				&& Test.TestNotNull(TEXT("use keys"), Use) && Test.TestNotNull(TEXT("progression"), Progression);
		}

		FLureResolvedInteraction Key(ELureInteractKey InKey) const { return Use->ResolveInteraction(InKey); }
	};

	/** A counter whose top is at Top, its customers' side (+X) toward the dock's center when Yaw = 180 and Top.X > 0 */
	ALureSellCounter* Counter(LCT::FWorld& W, const FVector& Top, float Yaw, FName MarketId = TEXT("Default"))
	{
		return W.SpawnCounter(Top, Yaw, MarketId);
	}

	const FLureCoolerSaveData* FindSaved(const TArray<FLureCoolerSaveData>& Coolers, const FGuid& Guid)
	{
		return Coolers.FindByPredicate([&Guid](const FLureCoolerSaveData& Data) { return Data.CoolerGuid == Guid; });
	}

	ALureCoolerActor* FindCooler(const TArray<ALureCoolerActor*>& Coolers, const FGuid& Guid)
	{
		ALureCoolerActor* const* Found = Coolers.FindByPredicate([&Guid](const ALureCoolerActor* Cooler) { return Cooler->GetCoolerGuid() == Guid; });
		return Found ? *Found : nullptr;
	}
}

// ---- The sell counter ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCounterSell, "Project.Catch.Counter.PlaceSellAndTakeBack", LCT::Flags)
bool FCatchCounterSell::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureSellCounter* Counter = LureCatchCounterSaveTest::Counter(W, FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("counter"), Counter))
	{
		return false;
	}
	W.Tick(20);
	const int32 Xp0 = P.Progression->GetTotalXp();
	ALureFishItem* A = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 7, 1.5f, 1));
	TestEqual(TEXT("QA precondition: the landing gave the XP"), P.Progression->GetTotalXp(), Xp0 + 7);
	LCT::LookAt(P.Pawn, Counter->GetActorLocation());
	W.Tick(1);
	FLureResolvedInteraction E = P.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("with a fish in hand, E puts it on the counter"), A && E.Verb == ELureInteractVerb::PlaceFishOnCounter && E.Target == Counter);
	TestEqual(TEXT("... prompt"), E.Prompt.ToString(), FString(TEXT("Put the Bonefish on the counter")));
	TestEqual(TEXT("... F still drops it"), LCT::VerbName(P.Key(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::DropFish));
	TestTrue(TEXT("E"), P.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("it lies on the counter"), A->IsFree() && A->GetCounter() == Counter && Counter->ContainsPoint(A->GetActorLocation()));
	TestFalse(TEXT("the hands are empty"), P.Hands->IsHoldingSomething());
	TestFalse(TEXT("a fish on the counter belongs to it (not picked up one by one)"), A->CanInteract(P.Pawn) || !A->GetInteraction(P.Pawn, ELureInteractKey::Primary).IsEmpty());

	ALureFishItem* B = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("CoralSnapper"), 30, 7, 1.0f, 2));
	TestTrue(TEXT("a second fish goes on the counter"), B && P.Use->PressKey(ELureInteractKey::Primary) && B->GetCounter() == Counter);
	TestTrue(TEXT("... next to the first, not on it"), B && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) >= 0.9f * Counter->FishSpacing);
	TestEqual(TEXT("the counter lists both"), Counter->GetFishOnCounter().Num(), 2);
	TestEqual(TEXT("quote: the fresh values"), Counter->QuoteAll(), 75);
	E = P.Key(ELureInteractKey::Primary);
	const FLureResolvedInteraction F = P.Key(ELureInteractKey::Secondary);
	TestTrue(TEXT("empty hands: E sells"), E.Verb == ELureInteractVerb::SellCounter && E.Prompt.ToString() == TEXT("Sell 2 fish (75 coins)"));
	TestTrue(TEXT("... F takes one back"), F.Verb == ELureInteractVerb::TakeFishFromCounter && F.Prompt.ToString() == TEXT("Take a fish back"));
	TestEqual(TEXT("prompt line"), P.Use->GetPromptText(), FString(TEXT("[E] Sell 2 fish (75 coins)   [F] Take a fish back")));

	TestTrue(TEXT("F"), P.Use->PressKey(ELureInteractKey::Secondary));
	TestTrue(TEXT("the last one put there comes back into the hand"), P.Hands->GetHeldFish() == B && B->GetCounter() == nullptr);
	TestEqual(TEXT("... one left"), Counter->GetFishOnCounter().Num(), 1);
	TestTrue(TEXT("E puts it back"), P.Use->PressKey(ELureInteractKey::Primary) && B->GetCounter() == Counter);

	const int32 Money0 = P.Progression->GetMoney();
	const int32 Xp1 = P.Progression->GetTotalXp();
	P.Progression->ClearNotices();
	const TWeakObjectPtr<ALureFishItem> WeakA = A;
	const TWeakObjectPtr<ALureFishItem> WeakB = B;
	TestTrue(TEXT("E sells everything on the counter"), P.Use->PressKey(ELureInteractKey::Primary));
	TestEqual(TEXT("paid"), P.Progression->GetMoney(), Money0 + 75);
	TestEqual(TEXT("no XP for selling"), P.Progression->GetTotalXp(), Xp1);
	TestFalse(TEXT("the sold fish are gone"), WeakA.IsValid() || WeakB.IsValid());
	TestEqual(TEXT("the counter is empty"), Counter->GetFishOnCounter().Num(), 0);
	TestTrue(FString::Printf(TEXT("\"Sold 2 fish for 75 coins\" (%s)"), *LCT::NoticesOf(P.Pawn)), LCT::NoticesOf(P.Pawn).Contains(TEXT("Sold 2 fish for 75 coins")));
	E = P.Key(ELureInteractKey::Primary);
	TestTrue(TEXT("an empty counter explains itself"), !E.HasVerb() && E.Target == Counter && E.Prompt.ToString() == TEXT("Put fish on the counter to sell them"));
	TestFalse(TEXT("... E does nothing"), P.Use->PressKey(ELureInteractKey::Primary));
	TestEqual(TEXT("... AuthoritySell sells nothing"), Counter->AuthoritySell(P.Pawn).FishSold, 0);

	// Another buyer pays its market's multiplier.
	Counter->Destroy();
	ALureSellCounter* Premium = LureCatchCounterSaveTest::Counter(W, FVector(150.0f, 0.0f, LCT::DockTop), 180.0f, TEXT("Premium"));
	W.Tick(1);
	ALureFishItem* C = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 3));
	TestTrue(TEXT("placed at the premium buyer"), C && Premium && Premium->AuthorityPlaceFish(P.Pawn, C));
	TestEqual(TEXT("x1.5: 67.5 rounds up to 68"), Premium ? Premium->QuoteAll() : -1, 68);
	const int32 Money1 = P.Progression->GetMoney();
	TestEqual(TEXT("sold"), Premium ? Premium->AuthoritySell(P.Pawn).MoneyEarned : -1, 68);
	TestEqual(TEXT("paid"), P.Progression->GetMoney(), Money1 + 68);

	// Out of reach the counter doesn't sell.
	ALureFishItem* D = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 4));
	Premium->AuthorityPlaceFish(P.Pawn, D);
	LCT::PlaceAt(P.Pawn, FVector(-LCT::DockHalf + 50.0f, 0.0f, LCT::DockTop), 0.0f);
	W.Tick(2);
	TestEqual(TEXT("too far away: nothing sold"), Premium->AuthoritySell(P.Pawn).FishSold, 0);
	TestEqual(TEXT("... the fish still lies there"), Premium->GetFishOnCounter().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCounterFreshness, "Project.Catch.Counter.FreshnessLowersPrice", LCT::Flags)
bool FCatchCounterFreshness::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this, /*bQuickFreshness*/ true) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureSellCounter* Counter = LureCatchCounterSaveTest::Counter(W, FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
	ALureFishItem* Fish = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 5));
	if (!TestNotNull(TEXT("counter"), Counter) || !TestNotNull(TEXT("fish"), Fish) || !TestTrue(TEXT("placed"), Counter->AuthorityPlaceFish(P.Pawn, Fish)))
	{
		return false;
	}
	TestEqual(TEXT("fresh: the full value"), Counter->QuoteAll(), 45);
	W.Advance(2.0f);
	const int32 Half = Counter->QuoteAll();
	TestEqual(TEXT("fish on the counter keep spoiling: the quote is the current value"), Half, FLureFreshness::GetSellPrice(Fish->GetFish(), Fish->GetValueShare(), 1.0f));
	TestTrue(FString::Printf(TEXT("... less than fresh, more than spoiled (%d)"), Half), Half < 45 && Half > 11);
	W.Advance(5.0f);
	TestEqual(TEXT("spoiled: MinValueShare 0.25 of 45 = 11.25 -> 11"), Counter->QuoteAll(), 11);
	const int32 Money0 = P.Progression->GetMoney();
	const FLureSaleResult Sale = Counter->AuthoritySell(P.Pawn);
	TestTrue(TEXT("the sale pays the current value"), Sale.FishSold == 1 && Sale.MoneyEarned == 11 && P.Progression->GetMoney() == Money0 + 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCounterDrop, "Project.Catch.Counter.DroppedFishCounts", LCT::Flags)
bool FCatchCounterDrop::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(40.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	// The counter's top area spans x 80..140 here; a drop lands DropForward (60 cm) in front of the eye, at x ~100.
	ALureSellCounter* Counter = LureCatchCounterSaveTest::Counter(W, FVector(110.0f, 0.0f, LCT::DockTop), 180.0f);
	ALureFishItem* Fish = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 6));
	if (!TestNotNull(TEXT("counter"), Counter) || !TestNotNull(TEXT("fish"), Fish))
	{
		return false;
	}
	LCT::PlaceAt(P.Pawn, FVector(40.0f, 0.0f, LCT::DockTop), 0.0f);
	W.Tick(2);
	TestEqual(TEXT("F drops the fish"), LCT::VerbName(P.Key(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::DropFish));
	TestTrue(TEXT("F"), P.Use->PressKey(ELureInteractKey::Secondary));
	TestTrue(FString::Printf(TEXT("a fish dropped onto the counter is on it (%s)"), *Fish->GetActorLocation().ToCompactString()), Fish->IsFree() && Fish->GetCounter() == Counter);
	TestEqual(TEXT("... and for sale"), Counter->QuoteAll(), 45);
	return true;
}

// ---- Focus and verbs ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchInteractFocus, "Project.Catch.Interact.FocusPromptsAndStaleVerbs", LCT::Flags)
bool FCatchInteractFocus::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureCoolerActor* Ahead = W.SpawnCooler(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
	ALureCoolerActor* Side = W.SpawnCooler(FVector(0.0f, 150.0f, LCT::DockTop), -90.0f);
	ALureCoolerActor* Far = W.SpawnCooler(FVector(0.0f, -500.0f, LCT::DockTop), 90.0f);
	if (!TestNotNull(TEXT("coolers"), Ahead) || !Side || !Far)
	{
		return false;
	}
	W.Tick(5);
	TestEqual(TEXT("key label E"), ILureInteractable::GetKeyLabel(ELureInteractKey::Primary), FString(TEXT("E")));
	TestEqual(TEXT("key label F"), ILureInteractable::GetKeyLabel(ELureInteractKey::Secondary), FString(TEXT("F")));

	LCT::LookAt(P.Pawn, Ahead->GetInteractionLocation());
	TestTrue(TEXT("looking at a cooler focuses it"), P.Use->FindFocusedInteractable() == Ahead);
	TestEqual(TEXT("... both keys in the prompt"), P.Use->GetPromptText(), FString(TEXT("[E] Open the cooler (0/4)   [F] Pick up the cooler")));
	LCT::LookAt(P.Pawn, Side->GetInteractionLocation());
	TestTrue(TEXT("turning to the other focuses that one"), P.Use->FindFocusedInteractable() == Side);
	LCT::LookAt(P.Pawn, Side->GetInteractionLocation() + FVector(0.0f, 0.0f, 200.0f));
	TestNull(TEXT("looking well above it: nothing"), P.Use->FindFocusedInteractable());
	TestTrue(TEXT("... no prompt"), P.Use->GetPromptText().IsEmpty());
	LCT::LookAt(P.Pawn, Far->GetInteractionLocation());
	TestNull(TEXT("out of reach: nothing"), P.Use->FindFocusedInteractable());

	// The server does only the verb it would offer itself now, in reach.
	TestFalse(TEXT("a stale verb is refused (closed lid: E opens, doesn't take out)"), P.Use->TryInteract(Ahead, ELureInteractKey::Primary, ELureInteractVerb::TakeFishFromCooler));
	TestFalse(TEXT("... a verb of the other key too"), P.Use->TryInteract(Ahead, ELureInteractKey::Primary, ELureInteractVerb::PickUpCooler));
	TestTrue(TEXT("the right verb works"), P.Use->TryInteract(Ahead, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler) && Ahead->IsLidOpen());
	TestFalse(TEXT("the same request again is stale now"), P.Use->TryInteract(Ahead, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler));
	TestFalse(TEXT("out of reach is refused"), P.Use->TryInteract(Far, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler) || Far->IsLidOpen());
	TestFalse(TEXT("something that isn't interactable"), P.Use->TryInteract(P.Pawn, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler));
	TestFalse(TEXT("nothing"), P.Use->TryInteract(nullptr, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler));
	TestFalse(TEXT("no verb"), P.Use->RequestInteract(Side, ELureInteractKey::Primary, ELureInteractVerb::None));

	// Someone else's hanging fish is theirs: not focusable, no verbs, refused by the server.
	LureCatchCounterSaveTest::FPlayer Q;
	if (!Q.Spawn(*this, W, FVector(-200.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureFishItem* Theirs = W.Land(Q.Pawn, LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 7));
	W.Tick(5);
	TestTrue(TEXT("their fish hangs"), Theirs && Q.Hands->GetHangingFish() == Theirs);
	TestFalse(TEXT("not mine to use"), Theirs->CanInteract(P.Pawn) || Theirs->GetInteraction(P.Pawn, ELureInteractKey::Primary).HasVerb());
	TestFalse(TEXT("the server refuses a grab"), P.Use->TryInteract(Theirs, ELureInteractKey::Primary, ELureInteractVerb::GrabFish) || P.Hands->AuthorityTakeInHand(Theirs));
	TestTrue(TEXT("... it still hangs on their hook"), Q.Hands->GetHangingFish() == Theirs);
	return true;
}

// ---- Getting caught and leaving ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchCaught, "Project.Catch.Caught.LosesHandFishKeepsCoolers", LCT::Flags)
bool FCatchCaught::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureCoolerActor* Kept = W.SpawnCooler(FVector(0.0f, 300.0f, LCT::DockTop), 0.0f, NAME_None, P.State);
	ALureCoolerActor* Carried = W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f, NAME_None, P.State);
	if (!TestNotNull(TEXT("coolers"), Kept) || !Carried)
	{
		return false;
	}
	Kept->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 8), W.Now()));
	Kept->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 9), W.Now()));
	const FVector KeptAt = Kept->GetActorLocation();
	ALureFishItem* InHand = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 10));
	ALureFishItem* OnHook = W.Land(P.Pawn, LCT::MakeFish(TEXT("CoralSnapper"), 30, 5, 1.0f, 11)); // debug tools: a second fish while the hand is full
	TestTrue(TEXT("QA precondition: a fish in hand and one on the hook"), InHand && OnHook && P.Hands->GetHeldFish() == InHand && P.Hands->GetHangingFish() == OnHook);
	P.Progression->AddMoney(50);
	const int32 Money = P.Progression->GetMoney();
	const int32 Xp = P.Progression->GetTotalXp();

	TestEqual(TEXT("caught: the fish in hand and on the hook are lost"), ULureCatchLibrary::HandlePlayerCaught(P.Pawn), 2);
	TestFalse(TEXT("... gone"), IsValid(InHand) || IsValid(OnHook));
	TestTrue(TEXT("... the hands are empty"), !P.Hands->IsHoldingSomething() && !P.Hands->GetHangingFish());
	TestTrue(TEXT("coolers elsewhere keep their fish and place"), Kept->GetNumFish() == 2 && Kept->GetActorLocation().Equals(KeptAt, 0.1));
	TestTrue(TEXT("money and XP are kept"), P.Progression->GetMoney() == Money && P.Progression->GetTotalXp() == Xp);

	// Caught while carrying a cooler: it is put down at the last dry ground spot, with its fish.
	Carried->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 12), W.Now()));
	TestTrue(TEXT("carrying"), Carried->AuthorityPickUp(P.Pawn));
	const FVector Dry(-200.0f, 100.0f, LCT::DockTop);
	P.Hands->SetLastDryGround(Dry);
	TestEqual(TEXT("caught while carrying: no fish lost"), ULureCatchLibrary::HandlePlayerCaught(P.Pawn), 0);
	TestTrue(FString::Printf(TEXT("... the cooler stands at the last dry spot (%s)"), *Carried->GetActorLocation().ToCompactString()),
		Carried->IsFree() && Carried->GetActorLocation().Equals(Dry, 1.0) && Carried->GetNumFish() == 1);

	// Leaving (the pawn goes away) loses nothing: what it held is put down.
	ALureFishItem* Held = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 13));
	const FVector LastDry(-300.0f, -100.0f, LCT::DockTop);
	P.Hands->SetLastDryGround(LastDry);
	P.Pawn->Destroy();
	W.Tick(1);
	TestTrue(TEXT("the pawn left: its fish lies at the last dry spot"), IsValid(Held) && Held->IsFree() && Held->GetActorLocation().Equals(LastDry, 1.0));
	return true;
}

// ---- Saves ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchSaveRoundTrip, "Project.Catch.Save.CoolersRoundTrip", LCT::Flags)
bool FCatchSaveRoundTrip::RunTest(const FString& Parameters)
{
	using LureCatchCounterSaveTest::FindCooler;
	using LureCatchCounterSaveTest::FindSaved;
	FLurePlayerSaveData Loaded;
	FGuid GuidA;
	FGuid GuidB;
	{
		LCT::FWorld W;
		LureCatchCounterSaveTest::FPlayer P;
		if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
		{
			return false;
		}
		ALureCoolerActor* A = ALureCoolerActor::SpawnCooler(W.World, TEXT("Starter"), FTransform(FRotator(0.0f, 30.0f, 0.0f), FVector(200.0f, 0.0f, LCT::DockTop)), P.State, FGuid(), /*bStarter*/ true);
		ALureCoolerActor* B = ALureCoolerActor::SpawnCooler(W.World, TEXT("Large"), FTransform(FVector(0.0f, -200.0f, LCT::DockTop)), P.State);
		ALureCoolerActor* Others = ALureCoolerActor::SpawnCooler(W.World, NAME_None, FTransform(FVector(-200.0f, 0.0f, LCT::DockTop)), nullptr);
		if (!TestNotNull(TEXT("cooler A"), A) || !TestNotNull(TEXT("cooler B"), B) || !TestNotNull(TEXT("someone else's"), Others))
		{
			return false;
		}
		GuidA = A->GetCoolerGuid();
		GuidB = B->GetCoolerGuid();
		A->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 45, 1, 2.0f, 1), W.Now()));
		A->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("CoralSnapper"), 30, 1, 1.0f, 2), W.Now()));
		A->AuthoritySetLidOpen(true);
		B->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 20, 1, 1.5f, 3), W.Now()));
		P.Progression->AddMoney(123);
		P.Progression->AddXp(40);
		W.Advance(2.0f);
		TestTrue(TEXT("B is carried at save time"), B->AuthorityPickUp(P.Pawn));
		P.Hands->SetLastDryGround(FVector(-100.0f, 50.0f, LCT::DockTop));

		const FLurePlayerSaveData Data = ULureCatchLibrary::GetPlayerSaveData(P.State);
		TestEqual(TEXT("progression is saved"), Data.Progress.Money, P.Progression->GetMoney());
		TestEqual(TEXT("... XP"), Data.Progress.TotalXp, P.Progression->GetTotalXp());
		TestEqual(TEXT("the player's two coolers are saved (not someone else's)"), Data.Coolers.Num(), 2);
		const FLureCoolerSaveData* SA = FindSaved(Data.Coolers, GuidA);
		const FLureCoolerSaveData* SB = FindSaved(Data.Coolers, GuidB);
		if (!TestNotNull(TEXT("A saved"), SA) || !TestNotNull(TEXT("B saved"), SB))
		{
			return false;
		}
		TestTrue(TEXT("A: row, place, facing, open lid, starter"), SA->CoolerId == TEXT("Starter") && SA->Location.Equals(FVector(200.0f, 0.0f, LCT::DockTop), 0.1)
			&& FMath::IsNearlyEqual(SA->Rotation.Yaw, 30.0, 0.01) && SA->bLidOpen && SA->bStarter);
		TestTrue(TEXT("A: both fish, in order"), SA->Fish.Num() == 2 && SA->Fish[0].Fish.Seed == 1 && SA->Fish[1].Fish.Seed == 2);
		TestTrue(FString::Printf(TEXT("A: their exposure now (open lid, ~2 s: %.2f)"), SA->Fish.Num() ? SA->Fish[0].Freshness.ExposedSeconds : -1.0f),
			SA->Fish.Num() == 2 && FMath::IsNearlyEqual(SA->Fish[0].Freshness.ExposedSeconds, 2.0f, 0.15f));
		// Its front (+X, the latch) toward the carrier, like any forced put-down (they stand away from that spot here).
		const FVector ToCarrier = P.Pawn->GetActorLocation() - FVector(-100.0f, 50.0f, LCT::DockTop);
		TestTrue(TEXT("B (carried): saved standing at its carrier's last dry spot, its front toward them"), SB->CoolerId == TEXT("Large") && SB->Location.Equals(FVector(-100.0f, 50.0f, LCT::DockTop), 0.1)
			&& !SB->bLidOpen && !SB->bStarter && ToCarrier.SizeSquared2D() > 100.0
			&& FMath::IsNearlyEqual(FRotator::NormalizeAxis(SB->Rotation.Yaw - FMath::RadiansToDegrees(FMath::Atan2(ToCarrier.Y, ToCarrier.X))), 0.0, 0.5));
		TestTrue(TEXT("B: its fish, still fresh (closed)"), SB->Fish.Num() == 1 && SB->Fish[0].Fish.Seed == 3 && SB->Fish[0].Freshness.ExposedSeconds == 0.0f);

		// The save format: only SaveGame fields travel (the freshness anchor and rate are rebuilt at load).
		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Archive(Writer, false);
			Archive.ArIsSaveGame = true;
			FLurePlayerSaveData::StaticStruct()->SerializeItem(Archive, const_cast<FLurePlayerSaveData*>(&Data), nullptr);
		}
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Archive(Reader, true);
			Archive.ArIsSaveGame = true;
			FLurePlayerSaveData::StaticStruct()->SerializeItem(Archive, &Loaded, nullptr);
		}
		TestTrue(TEXT("serialized"), Bytes.Num() > 0);
		TestEqual(TEXT("round trip: coolers"), Loaded.Coolers.Num(), 2);
		TestEqual(TEXT("round trip: money"), Loaded.Progress.Money, Data.Progress.Money);
		for (const FLureCoolerSaveData& Saved : Data.Coolers)
		{
			const FLureCoolerSaveData* Back = FindSaved(Loaded.Coolers, Saved.CoolerGuid);
			TestTrue(FString::Printf(TEXT("round trip: cooler %s"), *Saved.CoolerId.ToString()), Back && Back->CoolerId == Saved.CoolerId && Back->Location.Equals(Saved.Location, 0.01)
				&& Back->bLidOpen == Saved.bLidOpen && Back->bStarter == Saved.bStarter && Back->Fish.Num() == Saved.Fish.Num());
			for (int32 Index = 0; Back && Index < FMath::Min(Back->Fish.Num(), Saved.Fish.Num()); ++Index)
			{
				TestTrue(FString::Printf(TEXT("round trip: %s fish %d (record + exposure)"), *Saved.CoolerId.ToString(), Index),
					Back->Fish[Index].Fish.ToString() == Saved.Fish[Index].Fish.ToString() && Back->Fish[Index].Fish.Seed == Saved.Fish[Index].Fish.Seed
					&& Back->Fish[Index].Freshness.ExposedSeconds == Saved.Fish[Index].Freshness.ExposedSeconds);
			}
		}
	}

	// A new session: the player joins a fresh world, gets the automatic starter cooler, then their save arrives.
	LCT::FWorld W2;
	LureCatchCounterSaveTest::FPlayer P2;
	if (!W2.Create(*this) || !P2.Spawn(*this, W2, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureCoolerActor* Starter = ULureCatchLibrary::EnsureStarterCooler(P2.State, P2.Pawn);
	TestTrue(TEXT("the automatic starter cooler"), Starter && Starter->IsStarter() && Starter->GetNumFish() == 0);
	const TWeakObjectPtr<ALureCoolerActor> WeakStarter = Starter;
	TestTrue(TEXT("the save applies"), ULureCatchLibrary::ApplyPlayerSaveData(P2.State, Loaded));
	W2.Tick(1);
	TestFalse(TEXT("the empty automatic starter is replaced by the saved coolers"), WeakStarter.IsValid());
	const TArray<ALureCoolerActor*> Owned = ULureCatchLibrary::GetOwnedCoolers(P2.State);
	TestEqual(TEXT("the saved coolers are back"), Owned.Num(), 2);
	ALureCoolerActor* RA = FindCooler(Owned, GuidA);
	ALureCoolerActor* RB = FindCooler(Owned, GuidB);
	if (!TestNotNull(TEXT("A back (same identity)"), RA) || !TestNotNull(TEXT("B back"), RB))
	{
		return false;
	}
	TestTrue(TEXT("A: where it stood, open, 2 fish"), RA->GetActorLocation().Equals(FVector(200.0f, 0.0f, LCT::DockTop), 0.5) && RA->IsLidOpen() && RA->GetNumFish() == 2 && RA->IsStarter());
	TestTrue(TEXT("B: at the dry spot, closed, Large, 1 fish"), RB->GetActorLocation().Equals(FVector(-100.0f, 50.0f, LCT::DockTop), 0.5) && !RB->IsLidOpen()
		&& RB->GetCoolerId() == TEXT("Large") && RB->GetNumFish() == 1 && RB->IsFree());
	FLureCaughtFish Record;
	RA->GetStorage()->GetFishAt(0, Record);
	const float Saved = Record.Freshness.GetExposure(W2.Now());
	TestTrue(FString::Printf(TEXT("the exposure is kept (%.2f s)"), Saved), FMath::IsNearlyEqual(Saved, 2.0f, 0.15f));
	W2.Advance(1.0f);
	RA->GetStorage()->GetFishAt(0, Record);
	TestTrue(TEXT("... and goes on at the open lid's rate"), FMath::IsNearlyEqual(Record.Freshness.GetExposure(W2.Now()) - Saved, 1.0f, 0.15f));
	TestTrue(TEXT("progression is back"), P2.Progression->GetMoney() == Loaded.Progress.Money && P2.Progression->GetTotalXp() == Loaded.Progress.TotalXp);

	// A reconnect (the same save again in the same world) updates the coolers in place.
	TestEqual(TEXT("applied again"), ULureCatchLibrary::ApplyCoolerSaveData(P2.State, Loaded.Coolers), 2);
	const TArray<ALureCoolerActor*> Again = ULureCatchLibrary::GetOwnedCoolers(P2.State);
	TestTrue(TEXT("... no duplicates, the same actors"), Again.Num() == 2 && FindCooler(Again, GuidA) == RA && FindCooler(Again, GuidB) == RB);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchSaveUnknownRow, "Project.Catch.Save.UnknownRowKeepsFish", LCT::Flags)
bool FCatchSaveUnknownRow::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	FLureCoolerSaveData Data;
	Data.CoolerGuid = FGuid::NewGuid();
	Data.CoolerId = TEXT("Removed");
	Data.Location = FVector(0.0f, 250.0f, LCT::DockTop);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FLureCaughtFish Fish = FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 200 + Index), 0.0);
		Fish.Freshness.ExposedSeconds = 5.0f;
		Data.Fish.Add(Fish);
	}
	Data.Fish.Add(FLureCaughtFish()); // a broken record
	AddExpectedMessagePlain(TEXT("DT_Cooler has no row 'Removed'"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	TestEqual(TEXT("applied"), ULureCatchLibrary::ApplyCoolerSaveData(P.State, { Data }), 1);
	const TArray<ALureCoolerActor*> Owned = ULureCatchLibrary::GetOwnedCoolers(P.State);
	ALureCoolerActor* Cooler = Owned.Num() == 1 ? Owned[0] : nullptr;
	if (!TestNotNull(TEXT("the cooler"), Cooler))
	{
		return false;
	}
	TestEqual(TEXT("a removed row loads as the default row"), Cooler->GetCoolerId(), FName(TEXT("Starter")));
	TestEqual(TEXT("... keeping every fish (never trimmed; the broken record dropped)"), Cooler->GetNumFish(), 6);
	TestTrue(TEXT("... over capacity: full"), Cooler->GetCapacity() == 4 && Cooler->IsFull());
	W.Advance(3.0f);
	FLureCaughtFish Record;
	Cooler->GetStorage()->GetFishAt(5, Record);
	TestEqual(TEXT("the saved exposure, held by the closed lid"), Record.Freshness.GetExposure(W.Now()), 5.0f, 1.0e-4f);
	TestTrue(TEXT("open"), Cooler->AuthoritySetLidOpen(true));
	ALureFishItem* Out = Cooler->AuthorityTakeFishOut(P.Pawn);
	TestTrue(TEXT("taking out works"), Out && Out->GetFish().Seed == 205 && Cooler->GetNumFish() == 5);
	TestFalse(TEXT("putting in waits until there is room"), Cooler->AuthorityPutFishIn(P.Pawn, Out));
	return true;
}

// ---- The starter cooler ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchStarter, "Project.Catch.Starter.SpawnsOncePerPlayer", LCT::Flags)
bool FCatchStarter::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P1;
	LureCatchCounterSaveTest::FPlayer P2;
	LureCatchCounterSaveTest::FPlayer P3;
	LureCatchCounterSaveTest::FPlayer P4;
	if (!W.Create(*this) || !P1.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)) || !P2.Spawn(*this, W, FVector(0.0f, 150.0f, LCT::DockTop))
		|| !P3.Spawn(*this, W, FVector(0.0f, -150.0f, LCT::DockTop)) || !P4.Spawn(*this, W, FVector(-150.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	AActor* Start = W.SpawnMarker(FVector(-300.0f, 0.0f, LCT::DockTop + 92.0f), 0.0f); // a player start (capsule center)
	ALureCoolerActor* First = ULureCatchLibrary::EnsureStarterCooler(P1.State, Start);
	if (!TestNotNull(TEXT("a new player gets a starter cooler"), First))
	{
		return false;
	}
	const FVector Expected = Start->GetActorTransform().TransformPositionNoScale(Settings->StarterCoolerOffset);
	TestTrue(FString::Printf(TEXT("... next to the start, on the floor (%s)"), *First->GetActorLocation().ToCompactString()),
		FVector2D::Distance(FVector2D(First->GetActorLocation()), FVector2D(Expected)) < 0.5f && FMath::IsNearlyEqual(First->GetActorLocation().Z, LCT::DockTop, 0.5));
	// Its front (+X, the latch) toward the start, which is behind and to the side of it (not just the start's yaw + 180).
	const FVector ToStart = Start->GetActorLocation() - Expected;
	TestTrue(TEXT("... its front toward the start"), FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(First->GetActorRotation().Yaw)
		- static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToStart.Y, ToStart.X))))) < 0.5f);
	TestTrue(TEXT("... theirs, a starter, the default row, empty"), First->GetOwningPlayerState() == P1.State && First->IsStarter() && First->GetCoolerId() == TEXT("Starter") && First->GetNumFish() == 0);
	TestNull(TEXT("only once per player"), ULureCatchLibrary::EnsureStarterCooler(P1.State, Start));
	TestEqual(TEXT("... one cooler"), ULureCatchLibrary::GetOwnedCoolers(P1.State).Num(), 1);
	TestEqual(TEXT("the status line counts it"), ULureCatchLibrary::GetOwnCoolerStatus(P1.State), FString(TEXT("Cooler 0/4")));
	TestTrue(TEXT("... in the placeholder HUD"), ULureProgressionLibrary::GetPlaceholderStatusLines(LCT::ControllerOf(P1.Pawn)).Num() > 0
		&& ULureProgressionLibrary::GetPlaceholderStatusLines(LCT::ControllerOf(P1.Pawn))[0].Contains(TEXT("Cooler 0/4")));
	ALureCoolerActor* Second = ULureCatchLibrary::EnsureStarterCooler(P2.State, Start);
	TestTrue(TEXT("every player gets their own"), Second && Second != First && Second->GetOwningPlayerState() == P2.State);

	// A level marker (tag Lure.CoolerSpawn) wins: the players' coolers stand in a row along its +Y.
	const AActor* Marker = W.SpawnMarker(FVector(300.0f, -300.0f, LCT::DockTop), 90.0f, Settings->CoolerSpawnTag);
	ALureCoolerActor* Third = ULureCatchLibrary::EnsureStarterCooler(P3.State, Start);
	const FVector InRow = Marker->GetActorTransform().TransformPositionNoScale(FVector(0.0f, 2.0f * Settings->StarterCoolerSpacing, 0.0f));
	TestTrue(FString::Printf(TEXT("at the marker, third in the row (%s, expected %s)"), Third ? *Third->GetActorLocation().ToCompactString() : TEXT("-"), *InRow.ToCompactString()),
		Third && FVector2D::Distance(FVector2D(Third->GetActorLocation()), FVector2D(InRow)) < 0.5f);
	TestTrue(TEXT("... facing like the marker"), Third && FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(Third->GetActorRotation().Yaw) - 90.0f)) < 0.5f);

	// Switched off in the settings: nothing.
	ULureCatchSettings* Mutable = GetMutableDefault<ULureCatchSettings>();
	const bool bWas = Mutable->bSpawnStarterCooler;
	Mutable->bSpawnStarterCooler = false;
	ON_SCOPE_EXIT { Mutable->bSpawnStarterCooler = bWas; };
	TestNull(TEXT("switched off: no starter cooler"), ULureCatchLibrary::EnsureStarterCooler(P4.State, Start));
	return true;
}

// ---- What an open cooler shows ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchDisplay, "Project.Catch.Display.ShowsFishWhileOpen", LCT::Flags)
bool FCatchDisplay::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ALureCoolerActor* Large = W.SpawnCooler(FVector(150.0f, 150.0f, LCT::DockTop), 180.0f, TEXT("Large"));
	ALureCoolerActor* Starter = W.SpawnCooler(FVector(150.0f, -150.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("coolers"), Large) || !Starter)
	{
		return false;
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Large->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(Index % 2 ? TEXT("CoralSnapper") : TEXT("Bonefish"), 10, 1, 6.0f, 300 + Index), W.Now()));
	}
	Starter->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 310), W.Now()));
	Starter->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 311), W.Now()));
	W.Tick(10);
	TestEqual(TEXT("closed: nothing shows"), Large->GetNumDisplayedFish(), 0);

	const FLureCoolerDisplayRow Row = ULureCatchSubsystem::Get(W.World)->GetCoolerDisplayRow(TEXT("Large"));
	Large->AuthoritySetLidOpen(true);
	Starter->AuthoritySetLidOpen(true);
	W.Tick(30);
	TestEqual(TEXT("the lid is open"), Large->GetLidPitch(), ULureCatchSubsystem::GetTuningFor(W.World).LidOpenPitch, 0.5f);
	TestEqual(TEXT("open: as many fish as the display slots (6 inside)"), Large->GetNumDisplayedFish(), FMath::Min(6, Row.Slots.Num()));
	TestEqual(TEXT("... all of them when there are fewer"), Starter->GetNumDisplayedFish(), 2);
	int32 Checked = 0;
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Large);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		const USceneComponent* Parent = Primitive->GetAttachParent();
		if (Parent && Parent->GetFName() == TEXT("ContentsRoot") && Primitive->IsVisible())
		{
			++Checked;
			TestTrue(FString::Printf(TEXT("a shown fish is drawn at most MaxFishScale (%s)"), *Primitive->GetRelativeScale3D().ToCompactString()),
				Primitive->GetRelativeScale3D().GetMax() <= Row.MaxFishScale + 1.0e-3f);
			TestEqual(TEXT("... cosmetic: no collision"), static_cast<int32>(Primitive->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
		}
	}
	TestEqual(TEXT("the shown fish hang under the Contents point"), Checked, Large->GetNumDisplayedFish());
	// Each shown fish lies in its slot of the table, raised by the lie offset x its shown size (6 kg fish are over the
	// reference size, so they show at the cap).
	auto ShownAt = [](const ALureCoolerActor* Cooler)
	{
		TArray<FTransform> Out;
		TInlineComponentArray<UPrimitiveComponent*> Parts(Cooler);
		for (const UPrimitiveComponent* Part : Parts)
		{
			const USceneComponent* Parent = Part->GetAttachParent();
			if (Parent && Parent->GetFName() == TEXT("ContentsRoot") && Part->IsVisible())
			{
				Out.Add(Part->GetRelativeTransform());
			}
		}
		return Out;
	};
	const TArray<FTransform> LargeShown = ShownAt(Large);
	for (int32 Slot = 0; Slot < FMath::Min(Row.Slots.Num(), LargeShown.Num()); ++Slot)
	{
		const FVector Expected = Row.Slots[Slot].Location + FVector(0.0, 0.0, Row.LieOffsetCm * Row.MaxFishScale);
		const FTransform* Match = LargeShown.FindByPredicate([&Expected](const FTransform& Candidate) { return Candidate.GetLocation().Equals(Expected, 0.05); });
		TestTrue(FString::Printf(TEXT("slot %d: a fish at %s (its bed + the lie offset)"), Slot, *Expected.ToCompactString()), Match != nullptr);
		if (Match)
		{
			TestTrue(FString::Printf(TEXT("slot %d: turned like the slot"), Slot), Match->GetRotation().Equals(Row.Slots[Slot].Rotation.Quaternion(), 1.0e-3));
		}
	}
	FLureCaughtFish Record;
	Large->GetStorage()->GetFishAt(0, Record);
	TestEqual(TEXT("the records keep their real weight"), Record.Fish.WeightKg, 6.0f);

	Large->AuthoritySetLidOpen(false);
	W.Tick(30);
	TestEqual(TEXT("closed again: nothing shows"), Large->GetNumDisplayedFish(), 0);
	TestEqual(TEXT("... the lid is down"), Large->GetLidPitch(), 0.0f, 0.5f);

	TestTrue(TEXT("carrying closes it"), Starter->AuthorityPickUp(P.Pawn) && !Starter->IsLidOpen());
	W.Tick(30);
	TestEqual(TEXT("... nothing shows while carried"), Starter->GetNumDisplayedFish(), 0);

	// A fish put into a closed cooler: the lid opens and shuts around it.
	TestTrue(TEXT("put down"), Starter->AuthorityPutDown());
	ALureFishItem* Fish = W.LandInHand(P.Pawn, LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 312));
	TestTrue(TEXT("in it goes"), Fish && Starter->AuthorityPutFishIn(P.Pawn, Fish));
	W.Tick(4);
	TestTrue(FString::Printf(TEXT("... the lid lifts (%.1f deg)"), Starter->GetLidPitch()), Starter->GetLidPitch() > 5.0f && !Starter->IsLidOpen());
	W.Tick(60);
	TestEqual(TEXT("... and shuts"), Starter->GetLidPitch(), 0.0f, 0.5f);

	// The same species at another size: taking the top fish out re-seats the pile (slot 0 = the lowest shown fish) and each
	// shown fish is drawn at its own record's size.
	ALureCoolerActor* Pile = W.SpawnCooler(FVector(-150.0f, 150.0f, LCT::DockTop), 0.0f, TEXT("Large"));
	if (TestNotNull(TEXT("a second Large cooler"), Pile))
	{
		Pile->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 0.2f, 400), W.Now()));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Pile->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 6.0f, 401 + Index), W.Now()));
		}
		Pile->AuthoritySetLidOpen(true);
		W.Tick(30);
		auto BottomZ = [&ShownAt, &Row](const ALureCoolerActor* Cooler)
		{
			for (const FTransform& Part : ShownAt(Cooler))
			{
				if (FVector2D(Part.GetLocation()).Equals(FVector2D(Row.Slots[0].Location), 0.05))
				{
					return static_cast<float>(Part.GetLocation().Z);
				}
			}
			return -1.0f;
		};
		TestEqual(TEXT("the top four (6 kg) show: slot 0 holds a big one"), BottomZ(Pile), static_cast<float>(Row.Slots[0].Location.Z) + Row.LieOffsetCm * Row.MaxFishScale, 0.05f);
		FLureCaughtFish Top;
		TestTrue(TEXT("the top fish comes out"), Pile->GetStorage()->RemoveLastFish(Top));
		W.Tick(5);
		const float Reference = ULureCatchSubsystem::Get(W.World)->GetSpeciesReferenceWeight(TEXT("Bonefish"));
		const float Small = FMath::Min(FMath::Pow(0.2f / FMath::Max(Reference, 0.01f), 1.0f / 3.0f), Row.MaxFishScale);
		TestEqual(FString::Printf(TEXT("the small bonefish is now in slot 0, at its own size (%.2f)"), Small), BottomZ(Pile),
			static_cast<float>(Row.Slots[0].Location.Z) + Row.LieOffsetCm * Small, 0.05f);
	}
	return true;
}

// ---- Seams for T-029 (the landed fight fish) and T-032 (the physics line) ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchSeams, "Project.Catch.Seams.LandedVisualAndHangDriver", LCT::Flags)
bool FCatchSeams::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	LureCatchCounterSaveTest::FPlayer P;
	if (!W.Create(*this) || !P.Spawn(*this, W, FVector(0.0f, 0.0f, LCT::DockTop)))
	{
		return false;
	}
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(W.World);
	TArray<TPair<TWeakObjectPtr<ALureFishItem>, bool>> Events;
	const FDelegateHandle Handle = ALureFishItem::OnHookedChanged.AddLambda([&Events](ALureFishItem* Fish, bool bHooked) { Events.Emplace(Fish, bHooked); });
	ON_SCOPE_EXIT { ALureFishItem::OnHookedChanged.Remove(Handle); };

	// T-029: the landed fight fish, offered before the item exists, becomes its look.
	AActor* Visual = W.SpawnMarker(FVector(120.0f, 20.0f, LCT::DockTop + 120.0f));
	const FFishInstance Fish = LCT::MakeFish(TEXT("Bonefish"), 45, 1, 2.0f, 401);
	Subsystem->OfferLandedVisual(Visual, Fish);
	ALureFishItem* Item = W.Land(P.Pawn, Fish);
	if (!TestNotNull(TEXT("landed"), Item))
	{
		return false;
	}
	TestTrue(TEXT("the item adopted the landed fish"), Item->GetAdoptedVisual() == Visual && Visual->GetAttachParentActor() == Item);
	const FVector VisualStart(120.0f, 20.0f, LCT::DockTop + 120.0f);
	TestTrue(FString::Printf(TEXT("... the swing starts where the landed fish was (%s)"), *Item->GetPendulum().Bob.ToCompactString()),
		Item->GetPendulum().bInitialized && FVector::Dist(Item->GetPendulum().Bob, VisualStart) <= Item->GetMouthOffset().Size() + 1.0);
	TestFalse(TEXT("... its own mesh hides"), Item->GetFishMesh() && Item->GetFishMesh()->IsVisible());
	TestNull(TEXT("... the offer is used up"), Subsystem->ClaimLandedVisual(Fish));
	TestTrue(TEXT("T-032: one hooked event (on)"), Events.Num() == 1 && Events[0].Key.Get() == Item && Events[0].Value);

	// T-032: an external driver moves the hanging fish; the built-in swing leaves it alone.
	AActor* Line = W.SpawnMarker(FVector::ZeroVector);
	Item->SetExternalHangDriver(Line);
	TestTrue(TEXT("the driver is set"), Item->GetExternalHangDriver() == Line);
	TestEqual(TEXT("the line length it hangs on"), Item->GetHangLineLength(), ULureCatchSubsystem::GetTuningFor(W.World).HangLineLength);
	TestFalse(TEXT("the mouth point is known"), Item->GetMouthOffset().IsNearlyZero());
	const FVector Where(333.0f, 44.0f, LCT::DockTop + 150.0f);
	Item->SetActorLocation(Where);
	W.Tick(5);
	TestTrue(FString::Printf(TEXT("the driver's placement stands (%s)"), *Item->GetActorLocation().ToCompactString()), Item->GetActorLocation().Equals(Where, 0.5));

	TestTrue(TEXT("grabbed"), P.Hands->AuthorityTakeInHand(Item));
	TestTrue(TEXT("off the hook: an event (off)"), Events.Num() == 2 && Events[1].Key.Get() == Item && !Events[1].Value);
	TestNull(TEXT("... and the driver is let go"), Item->GetExternalHangDriver());
	TestTrue(TEXT("the adopted look follows into the hand"), Visual->GetAttachParentActor() == Item);
	const TWeakObjectPtr<AActor> WeakVisual = Visual;
	P.Hands->AuthorityReleaseHeld();
	Item->Destroy();
	W.Tick(1);
	TestFalse(TEXT("the adopted look goes with the item"), WeakVisual.IsValid());

	// An offer after the item exists (a client that got the item first) is adopted at once.
	const FFishInstance Fish2 = LCT::MakeFish(TEXT("CoralSnapper"), 30, 1, 1.0f, 402);
	ALureFishItem* Item2 = W.Land(P.Pawn, Fish2);
	AActor* Visual2 = W.SpawnMarker(FVector(100.0f, 0.0f, LCT::DockTop + 100.0f));
	Subsystem->OfferLandedVisual(Visual2, Fish2);
	TestTrue(TEXT("adopted at once"), Item2 && Item2->GetAdoptedVisual() == Visual2);
	TestTrue(TEXT("... the hanging swing starts from where the landed fish was"), Item2 && Item2->GetPendulum().bInitialized);

	// Nobody claims an offer: it is cleaned up after the timeout.
	AActor* Stray = W.SpawnMarker(FVector(0.0f, 300.0f, LCT::DockTop + 50.0f));
	const TWeakObjectPtr<AActor> WeakStray = Stray;
	Subsystem->OfferLandedVisual(Stray, LCT::MakeFish(TEXT("Bonefish"), 1, 1, 1.0f, 999));
	W.Advance(ULureCatchSubsystem::LandedVisualTimeout + 1.5f);
	TestFalse(TEXT("an unclaimed landed fish is removed after the timeout"), WeakStray.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

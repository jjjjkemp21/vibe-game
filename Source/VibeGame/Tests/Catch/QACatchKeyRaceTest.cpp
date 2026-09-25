// Lure T-037 independent QA (qa-engineer) for T-030n: the E / F keys act on the prompt shown at the end of the last frame
// (ULureInteractionComponent::PressKeyAsShown), and a player's Sell without the state token (0) is refused with a Warning.
// Black-box from docs/specs/catch-handling-rules.md ("The sell counter", "Focus and input": the key rule). One world = the
// listen host's view (every player local and on the server). "The same frame" = no world tick between the calls; key
// handlers that must run where real input runs (after the last frame's record, before this frame's) are pressed through
// LCT::FPressInTick. Complements SaleRaceKeyPressTest.cpp (the implementer's net repro, the stale record 3 frames old, a
// token-0 Sell from the host and a client) without repeating its cases.
// Project.Catch.QA.KeyRace.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureProgressionComponent.h"

namespace LureCatchQAKeyRace
{
	constexpr ELureInteractKey KeyE = ELureInteractKey::Primary;
	constexpr ELureInteractKey KeyF = ELureInteractKey::Secondary;
	const TCHAR* const NoTokenWarning = TEXT("without the prompt's state token");
	const TCHAR* const SellTwo = TEXT("Sell 2 fish (65 coins)"); // Bonefish 45 + Coral Snapper 20, fresh, market Default 1.0
	constexpr int32 WholeQuote = 65;
	constexpr int32 BonefishPrice = 45;

	/** The dock with the seller (host) and a second player, both local, and a counter in front of them */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Seller = nullptr;
		ALurePlayerCharacter* Taker = nullptr;
		ALureSellCounter* Counter = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!W.Create(Test))
			{
				return false;
			}
			Seller = W.SpawnPlayer(Test, FVector(0.0f, -80.0f, LCT::DockTop));
			Taker = W.SpawnPlayer(Test, FVector(0.0f, 80.0f, LCT::DockTop));
			Counter = W.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
			if (!Test.TestNotNull(TEXT("QA setup: seller"), Seller) || !Test.TestNotNull(TEXT("QA setup: taker"), Taker) || !Test.TestNotNull(TEXT("QA setup: counter"), Counter))
			{
				return false;
			}
			W.Tick(20);
			return Test.TestTrue(TEXT("QA setup: keys and progression"), Keys(Seller) && Keys(Taker) && LCT::ProgressionOf(Seller) && LCT::ProgressionOf(Taker));
		}

		static ULureInteractionComponent* Keys(const ALurePlayerCharacter* Player) { return LCT::InteractionOf(Player); }
		static int32 Money(const ALurePlayerCharacter* Player) { return LCT::ProgressionOf(Player)->GetMoney(); }
		static ALureFishItem* Held(const ALurePlayerCharacter* Player) { return LCT::HandsOf(Player)->GetHeldFish(); }

		/** Player puts Fish on the counter (server shortcut: landed, in hand, placed) */
		ALureFishItem* PutOn(ALurePlayerCharacter* Player, const FFishInstance& Fish)
		{
			ALureFishItem* Item = W.LandInHand(Player, Fish);
			return (Item && Counter->AuthorityPlaceFish(Player, Item)) ? Item : nullptr;
		}

		/** The seller's Bonefish (45) first, the taker's Coral Snapper (20) last (F takes back the last one put on) */
		bool PutTwo(FAutomationTestBase& Test, ALureFishItem*& OutBonefish, ALureFishItem*& OutSnapper, int32 Seed)
		{
			OutBonefish = PutOn(Seller, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, Seed));
			OutSnapper = PutOn(Taker, LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.0f, Seed + 1));
			return Test.TestTrue(TEXT("QA setup: 2 fish on the counter"), OutBonefish && OutSnapper && Counter->GetFishOnCounter().Num() == 2);
		}

		/** Both look at the counter; one frame so each key's prompt is recorded (the key then runs "next frame": age 1) */
		void LookAndShow()
		{
			LCT::LookAt(Seller, Counter->GetActorLocation());
			LCT::LookAt(Taker, Counter->GetActorLocation());
			W.Tick(1);
		}
	};

	/** The prompt Player's Key showed at the end of the last frame ("" if no fresh record) */
	FString Shown(const ALurePlayerCharacter* Player, ELureInteractKey Key)
	{
		FLureResolvedInteraction Record;
		int32 Token = 0;
		const ULureInteractionComponent* Keys = LCT::InteractionOf(Player);
		return (Keys && Keys->GetShownInteraction(Key, Record, Token)) ? Record.Prompt.ToString() : FString();
	}

	/**
	 *  One player, both keys in one frame: F (take back) then E (sell). The E prompt the host saw said "Sell 2 fish"; after its
	 *  own F only one is left, so E must not sell that one (the partial sale of the playtest bug): nothing is sold.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHostTakeBackThenSellSameFrame, "Project.Catch.QA.KeyRace.HostTakeBackThenSellSameFrame", LCT::Flags)
	bool FHostTakeBackThenSellSameFrame::RunTest(const FString& Parameters)
	{
		FRig Rig;
		ALureFishItem* Bonefish = nullptr;
		ALureFishItem* Snapper = nullptr;
		if (!Rig.Create(*this) || !Rig.PutTwo(*this, Bonefish, Snapper, 931))
		{
			return false;
		}
		Rig.LookAndShow();
		ALurePlayerCharacter* Host = Rig.Seller;
		TestEqual(TEXT("the host's shown E"), Shown(Host, KeyE), FString(SellTwo));
		TestEqual(TEXT("the host's shown F"), Shown(Host, KeyF), FString(TEXT("Take a fish back")));
		const int32 Money0 = FRig::Money(Host);
		ALureFishItem* HeldAfterF = nullptr;
		int32 MoneyAfterE = -1;
		LCT::FPressInTick Keys;
		Keys.Arm(Rig.W.World, []() { return true; }, [&]()
		{
			FRig::Keys(Host)->HandleAltInteractPressed(); // the F binding
			HeldAfterF = FRig::Held(Host);
			FRig::Keys(Host)->HandleInteractPressed(); // the E binding, same frame
			MoneyAfterE = FRig::Money(Host);
		});
		Rig.W.Tick(1);
		if (!TestTrue(TEXT("both keys ran inside the frame"), Keys.bDone))
		{
			return false;
		}
		TestTrue(TEXT("F took back the last fish put on (the snapper)"), HeldAfterF == Snapper);
		TestEqual(TEXT("E in the same frame sells nothing (never the 1 fish left of the 2 shown)"), MoneyAfterE, Money0);
		Rig.W.Tick(2);
		TestEqual(TEXT("... still nothing paid a frame later"), FRig::Money(Host), Money0);
		TestTrue(TEXT("... the Bonefish still lies on the counter"), IsValid(Bonefish) && Bonefish->GetCounter() == Rig.Counter && Rig.Counter->GetFishOnCounter().Num() == 1);
		TestTrue(TEXT("... the snapper is still in the host's hand"), FRig::Held(Host) == Snapper);
		return true;
	}

	/**
	 *  Two players, the other race order in one frame: the seller's E first, then the other player's F. The whole shown sale
	 *  (both fish, 65) is paid and the late take-back gets nothing. With HostTakeBackThenSellSameFrame and the implementer's
	 *  take-back-first case: both orders give the whole sale or nothing.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSellThenTakeBackSameFrame, "Project.Catch.QA.KeyRace.SellThenTakeBackSameFrameSellsWhole", LCT::Flags)
	bool FSellThenTakeBackSameFrame::RunTest(const FString& Parameters)
	{
		FRig Rig;
		ALureFishItem* Bonefish = nullptr;
		ALureFishItem* Snapper = nullptr;
		if (!Rig.Create(*this) || !Rig.PutTwo(*this, Bonefish, Snapper, 941))
		{
			return false;
		}
		Rig.LookAndShow();
		TestEqual(TEXT("the seller's shown E"), Shown(Rig.Seller, KeyE), FString(SellTwo));
		TestEqual(TEXT("the taker's shown F"), Shown(Rig.Taker, KeyF), FString(TEXT("Take a fish back")));
		TestEqual(TEXT("the counter quotes the shown total"), Rig.Counter->QuoteAll(), WholeQuote);
		const int32 SellerMoney0 = FRig::Money(Rig.Seller);
		const int32 TakerMoney0 = FRig::Money(Rig.Taker);
		LCT::FPressInTick Keys;
		Keys.Arm(Rig.W.World, []() { return true; }, [&]()
		{
			FRig::Keys(Rig.Seller)->HandleInteractPressed();
			FRig::Keys(Rig.Taker)->HandleAltInteractPressed();
		});
		Rig.W.Tick(1);
		if (!TestTrue(TEXT("both keys ran inside the frame"), Keys.bDone))
		{
			return false;
		}
		Rig.W.Tick(2);
		TestEqual(TEXT("the seller is paid the whole shown sale"), FRig::Money(Rig.Seller) - SellerMoney0, WholeQuote);
		TestEqual(TEXT("the counter is empty"), Rig.Counter->GetFishOnCounter().Num(), 0);
		TestNull(TEXT("the late take-back got no fish"), FRig::Held(Rig.Taker));
		TestEqual(TEXT("the taker is paid nothing"), FRig::Money(Rig.Taker), TakerMoney0);
		return true;
	}

	/**
	 *  The F key acts on its shown prompt too: F showed nothing (an empty counter, empty hands); a fish lands on the counter
	 *  in the same frame, before F runs. F does nothing (never takes back a fish the player didn't see); a frame later, with
	 *  "Take a fish back" shown, F takes it.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAltKeyShownNothingDoesNothing, "Project.Catch.QA.KeyRace.AltKeyShownNothingDoesNothing", LCT::Flags)
	bool FAltKeyShownNothingDoesNothing::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this))
		{
			return false;
		}
		Rig.LookAndShow();
		FLureResolvedInteraction Record;
		int32 Token = 0;
		TestTrue(TEXT("the seller's F prompt is recorded"), FRig::Keys(Rig.Seller)->GetShownInteraction(KeyF, Record, Token));
		TestFalse(FString::Printf(TEXT("... with no verb at an empty counter ('%s')"), *Record.Prompt.ToString()), Record.HasVerb());
		ALureFishItem* Fish = Rig.PutOn(Rig.Taker, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 951)); // this frame, before the key
		if (!TestNotNull(TEXT("a fish lands on the counter"), Fish))
		{
			return false;
		}
		TestTrue(TEXT("resolving now would take it back (the race is reproduced)"),
			FRig::Keys(Rig.Seller)->ResolveInteraction(KeyF).Verb == ELureInteractVerb::TakeFishFromCounter);
		FRig::Keys(Rig.Seller)->HandleAltInteractPressed();
		TestNull(TEXT("F on the shown nothing takes nothing"), FRig::Held(Rig.Seller));
		TestTrue(TEXT("... the fish still lies on the counter"), IsValid(Fish) && Fish->GetCounter() == Rig.Counter);
		Rig.W.Tick(1);
		TestEqual(TEXT("next frame F shows the take-back"), Shown(Rig.Seller, KeyF), FString(TEXT("Take a fish back")));
		FRig::Keys(Rig.Seller)->HandleAltInteractPressed();
		TestTrue(TEXT("... and F takes the fish"), FRig::Held(Rig.Seller) == Fish && Rig.Counter->GetFishOnCounter().Num() == 0);
		return true;
	}

	/**
	 *  Boundary of the record's age (MaxShownPromptAgeFrames = 1): 1 frame old it is still the prompt (the key runs early the
	 *  next frame); 2 frames old (paused, just possessed) it is not, and E acts on what it resolves to now.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordTwoFramesOldActsOnNow, "Project.Catch.QA.KeyRace.RecordTwoFramesOldActsOnNow", LCT::Flags)
	bool FRecordTwoFramesOldActsOnNow::RunTest(const FString& Parameters)
	{
		FRig Rig;
		ALureFishItem* Bonefish = nullptr;
		ALureFishItem* Snapper = nullptr;
		if (!Rig.Create(*this) || !Rig.PutTwo(*this, Bonefish, Snapper, 961))
		{
			return false;
		}
		Rig.LookAndShow();
		TestTrue(TEXT("the taker takes the snapper back (server, between frames)"), FRig::Keys(Rig.Taker)->PressKey(KeyF) && FRig::Held(Rig.Taker) == Snapper);
		TestEqual(TEXT("age 1: the record is still the shown 2-fish prompt"), Shown(Rig.Seller, KeyE), FString(SellTwo));
		GFrameCounter += 1; // age 2: no local tick for a frame
		FLureResolvedInteraction Record;
		int32 Token = 0;
		TestFalse(TEXT("age 2: the record is stale"), FRig::Keys(Rig.Seller)->GetShownInteraction(KeyE, Record, Token));
		const int32 Money0 = FRig::Money(Rig.Seller);
		FRig::Keys(Rig.Seller)->HandleInteractPressed();
		TestEqual(TEXT("... E acts on now: sells the 1 fish there (45)"), FRig::Money(Rig.Seller) - Money0, BonefishPrice);
		TestEqual(TEXT("... the counter is empty"), Rig.Counter->GetFishOnCounter().Num(), 0);
		return true;
	}

	/**
	 *  The token-0 rule refuses only a PLAYER's Sell: a player's take-back (no state token) with 0 still works, a player's
	 *  Sell with 0 is refused with exactly one Warning (the expected count 1 also proves the take-back logged none), and
	 *  server code (TryInteract) may still sell with 0.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTokenZeroRefusesOnlyPlayerSell, "Project.Catch.QA.KeyRace.TokenZeroRefusesOnlyPlayerSell", LCT::Flags)
	bool FTokenZeroRefusesOnlyPlayerSell::RunTest(const FString& Parameters)
	{
		FRig Rig;
		ALureFishItem* Bonefish = nullptr;
		ALureFishItem* Snapper = nullptr;
		if (!Rig.Create(*this) || !Rig.PutTwo(*this, Bonefish, Snapper, 971))
		{
			return false;
		}
		Rig.LookAndShow();
		AddExpectedMessagePlain(NoTokenWarning, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		ULureInteractionComponent* Keys = FRig::Keys(Rig.Seller);
		TestTrue(TEXT("a player's take-back with token 0 is done"), Keys->RequestInteract(Rig.Counter, KeyF, ELureInteractVerb::TakeFishFromCounter, 0) && FRig::Held(Rig.Seller) == Snapper);
		TestTrue(TEXT("QA setup: the snapper goes back on"), Rig.Counter->AuthorityPlaceFish(Rig.Seller, Snapper) && Rig.Counter->GetFishOnCounter().Num() == 2);
		const int32 Money0 = FRig::Money(Rig.Seller);
		TestFalse(TEXT("a player's Sell with token 0 is refused"), Keys->RequestInteract(Rig.Counter, KeyE, ELureInteractVerb::SellCounter, 0));
		TestTrue(TEXT("... nothing paid, both fish still there"), FRig::Money(Rig.Seller) == Money0 && Rig.Counter->GetFishOnCounter().Num() == 2);
		TestTrue(TEXT("server code may sell with 0 (TryInteract)"), Keys->TryInteract(Rig.Counter, KeyE, ELureInteractVerb::SellCounter, 0));
		TestEqual(TEXT("... the whole counter is sold"), FRig::Money(Rig.Seller) - Money0, WholeQuote);
		TestEqual(TEXT("... the counter is empty"), Rig.Counter->GetFishOnCounter().Num(), 0);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

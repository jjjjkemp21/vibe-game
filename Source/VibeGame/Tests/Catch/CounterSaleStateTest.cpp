// Lure T-030h tests (unreal-engineer): a sale sells exactly what the seller's prompt showed, or nothing. The Sell request
// carries the counter's contents token (ILureInteractable::GetInteractionStateToken) the seller's machine saw; the server
// refuses it when the fish on the counter changed in between (a take-back, a fish put on), tells the seller and their
// prompt refreshes. Rules: docs/specs/catch-handling-rules.md "The sell counter", "Focus and input".
// Project.Catch.Counter.ContentsToken, Project.Catch.Counter.SaleMatchesPrompt (the 2-client race: CatchNetTest.cpp)

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureProgressionComponent.h"

namespace LureCounterSaleStateTest
{
	/** Puts Fish on Counter the way a player does (landed, in the hand, placed); the item or null */
	ALureFishItem* PutOn(LCT::FWorld& W, ALureSellCounter* Counter, ALurePlayerCharacter* Player, const FFishInstance& Fish)
	{
		ALureFishItem* Item = W.LandInHand(Player, Fish);
		return (Item && Counter->AuthorityPlaceFish(Player, Item)) ? Item : nullptr;
	}

	int32 SellToken(const ALureSellCounter* Counter, const APawn* Pawn)
	{
		return static_cast<const ILureInteractable*>(Counter)->GetInteractionStateToken(Pawn, ELureInteractVerb::SellCounter);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCounterContentsToken, "Project.Catch.Counter.ContentsToken", LCT::Flags)
bool FCounterContentsToken::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(0.0f, 0.0f, LCT::DockTop));
	ALureSellCounter* One = W.SpawnCounter(FVector(150.0f, -250.0f, LCT::DockTop), 180.0f);
	ALureSellCounter* Two = W.SpawnCounter(FVector(150.0f, 250.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("counters"), One) || !Two)
	{
		return false;
	}
	W.Tick(20);
	const FFishInstance A = LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 11);
	const FFishInstance B = LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.0f, 12);
	const int32 Empty = One->GetContentsToken();
	TestNotEqual(TEXT("an empty counter's token is not 0 (0 = unchecked)"), Empty, 0);
	TestEqual(TEXT("... and the same on every empty counter"), Two->GetContentsToken(), Empty);

	TestNotNull(TEXT("A on counter one"), LureCounterSaleStateTest::PutOn(W, One, Player, A));
	const int32 OnlyA = One->GetContentsToken();
	TestTrue(TEXT("a fish put on changes it"), OnlyA != Empty && OnlyA != 0);
	TestNotNull(TEXT("B on counter one"), LureCounterSaleStateTest::PutOn(W, One, Player, B));
	// The same catches the other way round on counter two: the token depends on what lies there, not the order.
	TestNotNull(TEXT("B on counter two"), LureCounterSaleStateTest::PutOn(W, Two, Player, B));
	TestNotEqual(TEXT("B alone differs from A alone"), Two->GetContentsToken(), OnlyA);
	TestNotNull(TEXT("A on counter two"), LureCounterSaleStateTest::PutOn(W, Two, Player, A));
	const int32 Both = One->GetContentsToken();
	TestTrue(TEXT("A + B differs from A alone"), Both != OnlyA && Both != 0);
	TestEqual(TEXT("the same fish in another order: the same token"), Two->GetContentsToken(), Both);
	TestEqual(TEXT("... stable while nothing changes (spoiling doesn't change it)"), [&]() { W.Advance(2.0f); return One->GetContentsToken(); }(), Both);

	// Duplicate records count: a second identical A changes it.
	TestNotNull(TEXT("another identical A on counter two"), LureCounterSaleStateTest::PutOn(W, Two, Player, A));
	TestNotEqual(TEXT("A + A + B differs from A + B"), Two->GetContentsToken(), Both);

	// The interface: only Sell carries it.
	TestEqual(TEXT("the Sell verb's state token = the contents token"), LureCounterSaleStateTest::SellToken(One, Player), Both);
	const ILureInteractable* Counter = One;
	TestEqual(TEXT("Take back: no token"), Counter->GetInteractionStateToken(Player, ELureInteractVerb::TakeFishFromCounter), 0);
	TestEqual(TEXT("Put on: no token"), Counter->GetInteractionStateToken(Player, ELureInteractVerb::PlaceFishOnCounter), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCounterSaleMatchesPrompt, "Project.Catch.Counter.SaleMatchesPrompt", LCT::Flags)
bool FCounterSaleMatchesPrompt::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Seller = W.SpawnPlayer(*this, FVector(0.0f, -80.0f, LCT::DockTop));
	ALurePlayerCharacter* Taker = W.SpawnPlayer(*this, FVector(0.0f, 80.0f, LCT::DockTop));
	ALureSellCounter* Counter = W.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("players"), Seller) || !Taker || !TestNotNull(TEXT("counter"), Counter))
	{
		return false;
	}
	W.Tick(20);
	ULureInteractionComponent* SellerKeys = LCT::InteractionOf(Seller);
	ULureInteractionComponent* TakerKeys = LCT::InteractionOf(Taker);
	ULureProgressionComponent* SellerMoney = LCT::ProgressionOf(Seller);
	ULureProgressionComponent* TakerMoney = LCT::ProgressionOf(Taker);
	if (!TestNotNull(TEXT("keys"), SellerKeys) || !TakerKeys || !TestNotNull(TEXT("progression"), SellerMoney) || !TakerMoney)
	{
		return false;
	}
	// The seller's Bonefish first, the taker's snapper last (F takes the last one put on).
	ALureFishItem* A = LureCounterSaleStateTest::PutOn(W, Counter, Seller, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 21));
	ALureFishItem* B = LureCounterSaleStateTest::PutOn(W, Counter, Taker, LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.0f, 22));
	if (!TestNotNull(TEXT("both fish on the counter"), A) || !B)
	{
		return false;
	}
	LCT::LookAt(Seller, Counter->GetActorLocation());
	LCT::LookAt(Taker, Counter->GetActorLocation());
	W.Tick(1);

	// 1. A take-back between the seller's prompt and the server: refused, nothing sold, the seller is told.
	const FLureResolvedInteraction Seen = SellerKeys->ResolveInteraction(ELureInteractKey::Primary);
	TestTrue(FString::Printf(TEXT("the seller's prompt: Sell 2 fish (65 coins) (%s)"), *Seen.Prompt.ToString()),
		Seen.Verb == ELureInteractVerb::SellCounter && Seen.Target == Counter && Seen.Prompt.ToString() == TEXT("Sell 2 fish (65 coins)"));
	const int32 SeenToken = LureCounterSaleStateTest::SellToken(Counter, Seller);
	TestTrue(TEXT("the taker's F: take back"), TakerKeys->PressKey(ELureInteractKey::Secondary) && LCT::HandsOf(Taker)->GetHeldFish() == B);
	const int32 Money0 = SellerMoney->GetMoney();
	const int32 TakerMoney0 = TakerMoney->GetMoney();
	SellerMoney->ClearNotices();
	TestFalse(TEXT("the seller's stale Sell (2 fish seen, 1 there) is refused"), SellerKeys->TryInteract(Seen.Target, ELureInteractKey::Primary, Seen.Verb, SeenToken));
	TestEqual(TEXT("... nothing paid"), SellerMoney->GetMoney(), Money0);
	TestTrue(TEXT("... the Bonefish still lies on the counter"), IsValid(A) && A->GetCounter() == Counter && Counter->GetFishOnCounter().Num() == 1);
	TestTrue(TEXT("... the snapper stays in the taker's hand"), IsValid(B) && LCT::HandsOf(Taker)->GetHeldFish() == B);
	const FString Notices = LCT::NoticesOf(Seller);
	TestTrue(FString::Printf(TEXT("... the seller is told, with the new offer (%s)"), *Notices),
		Notices.Contains(TEXT("That just changed: nothing done")) && Notices.Contains(TEXT("Sell 1 fish (45 coins)")));
	TestFalse(TEXT("... no sale notice"), Notices.Contains(TEXT("Sold")));
	const FLureResolvedInteraction Fresh = SellerKeys->ResolveInteraction(ELureInteractKey::Primary);
	TestEqual(TEXT("the seller's prompt refreshes"), Fresh.Prompt.ToString(), FString(TEXT("Sell 1 fish (45 coins)")));

	// 2. A fish put on after the prompt: refused too (never sells more than it showed).
	const int32 OneSeen = LureCounterSaleStateTest::SellToken(Counter, Seller);
	LCT::LookAt(Taker, Counter->GetActorLocation());
	W.Tick(1);
	TestTrue(TEXT("the taker puts the snapper back (E)"), TakerKeys->PressKey(ELureInteractKey::Primary) && B->GetCounter() == Counter);
	TestFalse(TEXT("the seller's Sell for 1 fish, now 2 there: refused"), SellerKeys->TryInteract(Counter, ELureInteractKey::Primary, ELureInteractVerb::SellCounter, OneSeen));
	TestTrue(TEXT("... both still on the counter, nothing paid"), Counter->GetFishOnCounter().Num() == 2 && SellerMoney->GetMoney() == Money0);

	// 3. The key as pressed now: the prompt and the token come from the same view, so it sells exactly what it showed.
	TestEqual(TEXT("the prompt now"), SellerKeys->ResolveInteraction(ELureInteractKey::Primary).Prompt.ToString(), FString(TEXT("Sell 2 fish (65 coins)")));
	SellerMoney->ClearNotices();
	TestTrue(TEXT("E sells"), SellerKeys->PressKey(ELureInteractKey::Primary));
	TestEqual(TEXT("... paid exactly the prompt"), SellerMoney->GetMoney(), Money0 + 65);
	TestEqual(TEXT("... the taker isn't paid"), TakerMoney->GetMoney(), TakerMoney0);
	TestEqual(TEXT("... the counter is empty"), Counter->GetFishOnCounter().Num(), 0);
	TestTrue(TEXT("... \"Sold 2 fish for 65 coins\""), LCT::NoticesOf(Seller).Contains(TEXT("Sold 2 fish for 65 coins")));

	// 4. Callers that don't know a token (0: server code, tools) are not checked.
	ALureFishItem* C = LureCounterSaleStateTest::PutOn(W, Counter, Seller, LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 23));
	TestTrue(TEXT("token 0: not checked, sells"), C && SellerKeys->TryInteract(Counter, ELureInteractKey::Primary, ELureInteractVerb::SellCounter)
		&& SellerMoney->GetMoney() == Money0 + 95);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

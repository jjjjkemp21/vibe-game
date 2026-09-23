// Lure T-010 independent QA tests (qa-engineer): server authority, interaction range (client prompt vs server slack),
// and two players at one sell point. Project.Progression.QA.{Authority,Interact,Net}.*

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/Pawn.h"
#include "Interaction/LureInteractable.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureSellPoint.h"
#include "Tests/Progression/QAProgressionListener.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgAuthorityClientCannotSell, "Project.Progression.QA.Authority.ClientCannotSellAnyWay", QAP::Flags)
bool FQAProgAuthorityClientCannotSell::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	ALureSellPoint* Point = Env.SpawnSellPoint(*this, FVector::ZeroVector, TEXT("Triple"));
	if (!Player.IsValid() || !Player.Pawn || !Point)
	{
		return false;
	}
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 10, 1));
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_B"), 20, 1));
	Player.Progression->AddMoney(5);
	const QAP::FState Before = QAP::FState::Of(Player);

	QAP::ExpectWarnings(*this, TEXT("server-only"));
	for (const ENetRole Role : { ROLE_AutonomousProxy, ROLE_SimulatedProxy })
	{
		const FString Who = Role == ROLE_AutonomousProxy ? TEXT("owning client") : TEXT("other client");
		QAP::SetRoles(Role, { Player.State, Player.Pawn, Point });
		TestEqual(Who + TEXT(": SellAll sells nothing"), Point->SellAll(Player.Pawn).FishSold, 0);
		TestEqual(Who + TEXT(": SellOne sells nothing"), Point->SellOne(Player.Pawn, 0).FishSold, 0);
		TestFalse(Who + TEXT(": TryInteract refused"), Player.Interaction->TryInteract(Point, INDEX_NONE));
		Point->Interact(Player.Pawn, INDEX_NONE); // the interface call itself (server only)
		Player.Interaction->RequestInteract(Point, INDEX_NONE); // client -> server RPC; no server here, so it must not sell locally
		Player.Interaction->RequestInteract(Point, 1);
		Player.Interaction->HandleInteractPressed();
		TestEqual(Who + TEXT(": the component's SellAllFish sells nothing"), Player.Progression->SellAllFish(3.0f).FishSold, 0);
		TestFalse(Who + TEXT(": library HandleFishLanded refused"), ULureProgressionLibrary::HandleFishLanded(Player.Pawn, QAP::Fish(TEXT("QA_Cheat"), 9999, 9999)).bAccepted);
		TestTrue(FString::Printf(TEXT("%s: nothing changed (%s)"), *Who, *QAP::FState::Of(Player).Describe()), QAP::FState::Of(Player).Equals(Before));
	}
	QAP::SetRoles(ROLE_Authority, { Player.State, Player.Pawn, Point });
	const FLureSaleResult Sale = Point->SellAll(Player.Pawn);
	TestEqual(TEXT("back on the server the same call sells both fish (the refusal was the role)"), Sale.FishSold, 2);
	TestEqual(TEXT("... for (10 + 20) x 3"), Sale.MoneyEarned, 90);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgAuthorityClientEventsDontMutate, "Project.Progression.QA.Authority.ClientOnRepsOnlyNotify", QAP::Flags)
bool FQAProgAuthorityClientEventsDontMutate::RunTest(const FString& Parameters)
{
	// Client-side OnReps fire events for the HUD but never change the replicated values themselves.
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this);
	if (!Player.IsValid())
	{
		return false;
	}
	Player.Progression->AddXp(60);
	Player.Progression->AddMoney(7);
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 3, 1));
	const QAP::FState Before = QAP::FState::Of(Player);
	UQAProgressionListener* Listener = NewObject<UQAProgressionListener>();
	TStrongObjectPtr<UQAProgressionListener> Keep(Listener);
	Listener->Listen(Player.Progression, Player.Cooler);
	QAP::SetRoles(ROLE_SimulatedProxy, { Player.State });
	Player.Progression->OnRep_Money(0);
	Player.Progression->OnRep_TotalXp(0);
	Player.Progression->OnRep_Level(2);
	Player.Cooler->OnRep_StoredFish();
	Player.Cooler->OnRep_CoolerSize();
	TestTrue(TEXT("OnReps change no value"), QAP::FState::Of(Player).Equals(Before));
	TestEqual(TEXT("OnRep_Money -> one money event"), Listener->MoneyDeltas.Num(), 1);
	TestEqual(TEXT("OnRep_TotalXp -> one XP event"), Listener->XpDeltas.Num(), 1);
	TestEqual(TEXT("OnRep_Level with the same level (2 -> 2) is not a level-up"), Listener->LevelUps.Num(), 0);
	TestTrue(TEXT("the cooler OnReps tell the HUD"), Listener->CoolerChanges >= 1);
	QAP::SetRoles(ROLE_Authority, { Player.State });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgInteractClientPromptRange, "Project.Progression.QA.Interact.PromptRangeIsTheRadius", QAP::Flags)
bool FQAProgInteractClientPromptRange::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const float Radius = 300.0f;
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(Radius - 1.0f, 0.0f, 0.0f));
	ALureSellPoint* Point = Env.SpawnSellPoint(*this, FVector::ZeroVector, NAME_None, Radius);
	if (!Player.IsValid() || !Player.Pawn || !Point)
	{
		return false;
	}
	TestTrue(TEXT("1 cm inside the radius: the sell point is offered"), Player.Interaction->FindBestInteractable() == Point);
	TestFalse(TEXT("1 cm inside the radius: a prompt is shown"), Player.Interaction->GetPromptText().IsEmpty());
	Player.Pawn->SetActorLocation(FVector(Radius + 1.0f, 0.0f, 0.0f));
	TestNull(TEXT("1 cm outside the radius: nothing offered (the server slack is not shown to the player)"), Player.Interaction->FindBestInteractable());
	TestTrue(TEXT("1 cm outside the radius: no prompt"), Player.Interaction->GetPromptText().IsEmpty());
	Player.Pawn->SetActorLocation(FVector(0.0f, 0.0f, Radius - 1.0f));
	TestTrue(TEXT("range is 3D: straight above within the radius counts"), Player.Interaction->FindBestInteractable() == Point);
	Player.Pawn->SetActorLocation(FVector(250.0f, 0.0f, 250.0f));
	TestNull(TEXT("range is 3D: 250 cm out and 250 cm up (354 cm) is out of range"), Player.Interaction->FindBestInteractable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgInteractServerSlack, "Project.Progression.QA.Interact.ServerAcceptsWithinSlackRefusesBeyond", QAP::Flags)
bool FQAProgInteractServerSlack::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const float Radius = 300.0f;
	const float Slack = ILureInteractable::ServerRangeSlack;
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(Radius + Slack + 1.0f, 0.0f, 0.0f));
	ALureSellPoint* Point = Env.SpawnSellPoint(*this, FVector::ZeroVector, NAME_None, Radius);
	if (!Player.IsValid() || !Player.Pawn || !Point)
	{
		return false;
	}
	TestEqual(TEXT("slack from the spec: 150 cm"), Slack, 150.0f, 1e-3f);
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 10, 1));
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_B"), 20, 1));

	TestFalse(TEXT("radius + slack + 1 cm: TryInteract refused"), Player.Interaction->TryInteract(Point, INDEX_NONE));
	TestEqual(TEXT("radius + slack + 1 cm: SellAll sells nothing"), Point->SellAll(Player.Pawn).FishSold, 0);
	TestEqual(TEXT("radius + slack + 1 cm: SellOne sells nothing"), Point->SellOne(Player.Pawn, 0).FishSold, 0);
	TestEqual(TEXT("... fish kept"), Player.Cooler->GetNumFish(), 2);

	Player.Pawn->SetActorLocation(FVector(Radius + Slack - 1.0f, 0.0f, 0.0f));
	TestTrue(TEXT("radius + slack - 1 cm (lagging client): the server still sells one slot"), Player.Interaction->TryInteract(Point, 0));
	TestEqual(TEXT("... slot 0 sold for 10"), Player.Progression->GetMoney(), 10);
	TestEqual(TEXT("radius + slack - 1 cm: SellAll sells the rest"), Point->SellAll(Player.Pawn).MoneyEarned, 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgInteractBadTargets, "Project.Progression.QA.Interact.BadTargetsAndSlotsRefused", QAP::Flags)
bool FQAProgInteractBadTargets::RunTest(const FString& Parameters)
{
	// What a hacked or confused client could send in ServerInteract: null, a non-interactable actor, a bad slot.
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	ALureSellPoint* Point = Env.SpawnSellPoint(*this, FVector::ZeroVector);
	AActor* Plain = Env.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(50.0f, 0.0f, 0.0f)));
	if (!Player.IsValid() || !Player.Pawn || !Point || !TestNotNull(TEXT("plain actor"), Plain))
	{
		return false;
	}
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 10, 1));
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_B"), 20, 1));
	const QAP::FState Before = QAP::FState::Of(Player);
	TestFalse(TEXT("null target refused"), Player.Interaction->TryInteract(nullptr, INDEX_NONE));
	TestFalse(TEXT("a non-interactable actor refused"), Player.Interaction->TryInteract(Plain, INDEX_NONE));
	TestFalse(TEXT("RequestInteract with null sends nothing"), Player.Interaction->RequestInteract(nullptr, INDEX_NONE));
	Player.Interaction->TryInteract(Point, 2);
	Player.Interaction->TryInteract(Point, 99);
	Player.Interaction->TryInteract(Point, MAX_int32);
	TestEqual(TEXT("slots past the end (2, 99, MAX) sell nothing"), Player.Cooler->GetNumFish(), 2);
	TestTrue(FString::Printf(TEXT("nothing changed (%s)"), *QAP::FState::Of(Player).Describe()), QAP::FState::Of(Player).Equals(Before));
	TestTrue(TEXT("slot 1 sells exactly that fish"), Player.Interaction->TryInteract(Point, 1));
	FFishInstance Left;
	TestTrue(TEXT("the other fish stays in slot 0"), Player.Cooler->GetFishAt(0, Left) && Left.SpeciesId == FName(TEXT("QA_A")));
	TestEqual(TEXT("paid 20"), Player.Progression->GetMoney(), 20);

	// The player path after the dock is removed: the Interact key finds nothing and sells nothing.
	Point->Destroy();
	TestNull(TEXT("a destroyed sell point is no longer offered"), Player.Interaction->FindBestInteractable());
	Player.Interaction->HandleInteractPressed();
	TestEqual(TEXT("the Interact key sells nothing once the sell point is gone"), Player.Cooler->GetNumFish(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgInteractPawnWithoutProgression, "Project.Progression.QA.Interact.PawnWithoutPlayerStateSellsNothing", QAP::Flags)
bool FQAProgInteractPawnWithoutProgression::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	ULureInteractionComponent* Interaction = nullptr;
	APawn* Lone = Env.SpawnLonePawn(*this, FVector(100.0f, 0.0f, 0.0f), &Interaction);
	ALureSellPoint* Point = Env.SpawnSellPoint(*this, FVector::ZeroVector);
	if (!Lone || !Interaction || !Point)
	{
		return false;
	}
	TestEqual(TEXT("SellAll for a pawn without progression: nothing"), Point->SellAll(Lone).FishSold, 0);
	TestEqual(TEXT("QuoteAll: 0"), Point->QuoteAll(Lone), 0);
	TestFalse(TEXT("TryInteract: refused (nothing to sell for)"), Interaction->TryInteract(Point, INDEX_NONE));
	Interaction->HandleInteractPressed(); // must not crash
	TestEqual(TEXT("SellAll with a null pawn: nothing"), Point->SellAll(nullptr).FishSold, 0);
	TestEqual(TEXT("QuoteAll with a null pawn: 0"), Point->QuoteAll(nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgNetTwoPlayersOneDock, "Project.Progression.QA.Net.TwoPlayersSellOnlyTheirOwnCooler", QAP::Flags)
bool FQAProgNetTwoPlayersOneDock::RunTest(const FString& Parameters)
{
	// Co-op: each player's cooler, money and XP are separate; selling pays only the seller.
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer A = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	const QAP::FPlayer B = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(-100.0f, 0.0f, 0.0f));
	ALureSellPoint* Dock = Env.SpawnSellPoint(*this, FVector::ZeroVector, TEXT("Fancy"));
	if (!A.IsValid() || !B.IsValid() || !A.Pawn || !B.Pawn || !Dock)
	{
		return false;
	}
	TestTrue(TEXT("separate coolers"), A.Cooler != B.Cooler && A.Progression != B.Progression);
	ULureProgressionLibrary::HandleFishLanded(A.Pawn, QAP::Fish(TEXT("QA_A1"), 8, 30));
	ULureProgressionLibrary::HandleFishLanded(A.Pawn, QAP::Fish(TEXT("QA_A2"), 4, 30));
	ULureProgressionLibrary::HandleFishLanded(B.Pawn, QAP::Fish(TEXT("QA_B1"), 100, 5));
	TestEqual(TEXT("A's XP"), A.Progression->GetTotalXp(), 60);
	TestEqual(TEXT("A is level 2 (60 >= 50)"), A.Progression->GetLevel(), 2);
	TestEqual(TEXT("B's XP"), B.Progression->GetTotalXp(), 5);
	TestEqual(TEXT("B is level 1"), B.Progression->GetLevel(), 1);
	const QAP::FState BBefore = QAP::FState::Of(B);

	TestTrue(TEXT("A presses Interact at the dock"), A.Interaction->FindBestInteractable() == Dock);
	A.Interaction->HandleInteractPressed();
	TestEqual(TEXT("A sold 2 fish: round(10) + round(5) = 15"), A.Progression->GetMoney(), 15);
	TestEqual(TEXT("A's cooler is empty"), A.Cooler->GetNumFish(), 0);
	TestTrue(TEXT("B is untouched"), QAP::FState::Of(B).Equals(BBefore));

	B.Interaction->HandleInteractPressed();
	TestEqual(TEXT("B sold 1 fish: 100 x 1.25 = 125"), B.Progression->GetMoney(), 125);
	TestEqual(TEXT("A's money unchanged by B's sale"), A.Progression->GetMoney(), 15);

	TestEqual(TEXT("B caught: loses its own fish only"), ULureProgressionLibrary::HandlePlayerCaught(B.Pawn), 0);
	ULureProgressionLibrary::HandleFishLanded(A.Pawn, QAP::Fish(TEXT("QA_A3"), 1, 1));
	ULureProgressionLibrary::HandlePlayerCaught(B.Pawn);
	TestEqual(TEXT("B caught with A holding a fish: A keeps it"), A.Cooler->GetNumFish(), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

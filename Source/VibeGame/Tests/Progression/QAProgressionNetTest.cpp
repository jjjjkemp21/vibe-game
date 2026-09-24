// Lure T-010 independent QA tests (qa-engineer): client-side OnReps of the progression. Project.Progression.QA.Authority.*
//
// T-030 (unreal-engineer): retired here because they pinned ALureSellPoint and the abstract player-state cooler, replaced by
// ALureSellCounter and the physical cooler (docs/TEST_PLAN.md "T-030"; replacements in Project.Catch.Authority.*,
// Project.Catch.Interact.*, Project.Catch.Net.*):
//   Project.Progression.QA.Authority.ClientCannotSellAnyWay, .Interact.PromptRangeIsTheRadius,
//   .Interact.ServerAcceptsWithinSlackRefusesBeyond, .Interact.BadTargetsAndSlotsRefused,
//   .Interact.PawnWithoutPlayerStateSellsNothing, .Net.TwoPlayersSellOnlyTheirOwnCooler.
// ClientOnRepsOnlyNotify keeps its progression part (the cooler OnReps are tested with the cooler actor).

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Game/LurePlayerState.h"
#include "Progression/LureProgressionComponent.h"
#include "Tests/Progression/QAProgressionListener.h"

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
	const QAP::FState Before = QAP::FState::Of(Player);
	UQAProgressionListener* Listener = NewObject<UQAProgressionListener>();
	TStrongObjectPtr<UQAProgressionListener> Keep(Listener);
	Listener->Listen(Player.Progression);
	QAP::SetRoles(ROLE_SimulatedProxy, { Player.State });
	Player.Progression->OnRep_Money(0);
	Player.Progression->OnRep_TotalXp(0);
	Player.Progression->OnRep_Level(2);
	TestTrue(TEXT("OnReps change no value"), QAP::FState::Of(Player).Equals(Before));
	TestEqual(TEXT("OnRep_Money -> one money event"), Listener->MoneyDeltas.Num(), 1);
	TestEqual(TEXT("OnRep_TotalXp -> one XP event"), Listener->XpDeltas.Num(), 1);
	TestEqual(TEXT("OnRep_Level with the same level (2 -> 2) is not a level-up"), Listener->LevelUps.Num(), 0);
	QAP::SetRoles(ROLE_Authority, { Player.State });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

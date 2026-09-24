// Lure T-010 independent QA tests (qa-engineer): landing XP, money and levels on a player state in a test world.
// Project.Progression.QA.{Money,Level}.* (T-030 moved the cooler and selling to Project.Catch.*).

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/Pawn.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Catch/LureCatchLibrary.h"
#include "Tests/FishQATestHelpers.h"
#include "Tests/Progression/QAProgressionListener.h"

// T-030 (unreal-engineer): retired here because they pinned the abstract player-state cooler and ALureSellPoint, both
// replaced by the physical cooler and ALureSellCounter (docs/TEST_PLAN.md "T-030", replacements in Project.Catch.*):
//   Project.Progression.QA.Cooler.ShippedBasicFullReleasesNinthKeepsXp, .Cooler.KeepsRealCatchRecordsExactlyInOrder,
//   .Sell.EmptyCoolerPaysNothingAndFiresNoEvents, .Sell.RealCatchAtPalmKeyDockPaysRolledValue, .Sell.UnknownMarketIdPaysTimesOne,
//   .Sell.InvalidMarketRowFallsBackToTimesOne, .Sell.MoneySaturatesAtMaxOnSale.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgMoneyExtremeInputs, "Project.Progression.QA.Money.NegativeAndExtremeAmountsRefused", QAP::Flags)
bool FQAProgMoneyExtremeInputs::RunTest(const FString& Parameters)
{
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
	ULureProgressionComponent* P = Player.Progression;
	const int32 Start = P->GetMoney();
	TestTrue(TEXT("add 40"), P->AddMoney(40));
	TestFalse(TEXT("add MIN_int32 refused"), P->AddMoney(MIN_int32));
	TestFalse(TEXT("add -1 refused"), P->AddMoney(-1));
	TestFalse(TEXT("spend MIN_int32 refused (would add money)"), P->SpendMoney(MIN_int32));
	TestFalse(TEXT("spend -1 refused (would add money)"), P->SpendMoney(-1));
	TestFalse(TEXT("spend MAX_int32 with 40 refused"), P->SpendMoney(MAX_int32));
	TestEqual(TEXT("money unchanged by the refused calls"), P->GetMoney(), Start + 40);
	TestTrue(TEXT("spend 1"), P->SpendMoney(1));
	TestEqual(TEXT("39 left"), P->GetMoney(), Start + 39);
	TestFalse(TEXT("add 0 refused"), P->AddMoney(0));
	TestEqual(TEXT("XP: AddXp(MIN_int32) gives nothing"), P->AddXp(MIN_int32), 0);
	TestEqual(TEXT("XP stays 0"), P->GetTotalXp(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgLevelOneLandingManyLevels, "Project.Progression.QA.Level.OneLandingCrossesSeveralLevels", QAP::Flags)
bool FQAProgLevelOneLandingManyLevels::RunTest(const FString& Parameters)
{
	// Shipped curve: one big catch takes a new player from level 1 straight to level 5; one OnLevelUp(1, 5). Selling after gives no XP.
	QAP::FEnv Env;
	if (!Env.InitShipped(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	if (!Player.IsValid() || !Player.Pawn || !TestTrue(TEXT("the shipped curve has at least 5 levels"), Player.Progression->GetMaxLevel() >= 5))
	{
		return false;
	}
	UQAProgressionListener* Listener = NewObject<UQAProgressionListener>();
	TStrongObjectPtr<UQAProgressionListener> Keep(Listener);
	Listener->Listen(Player.Progression);
	const int32 Level5 = static_cast<int32>(Player.Progression->GetLevelCurve().GetXpAtLevelStart(5));

	const FLureFishLandedResult Result = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, QAP::Fish(TEXT("QA_Monster"), 500, Level5));
	TestEqual(TEXT("4 levels gained in one landing"), Result.LevelsGained, 4);
	TestEqual(TEXT("new level 5"), Result.NewLevel, 5);
	TestEqual(TEXT("player level 5"), Player.Progression->GetLevel(), 5);
	if (TestEqual(TEXT("ONE OnLevelUp for the jump"), Listener->LevelUps.Num(), 1))
	{
		TestTrue(TEXT("OnLevelUp (1 -> 5)"), Listener->LevelUps[0] == FIntPoint(1, 5));
	}
	TestEqual(TEXT("one OnLevelChanged"), Listener->LevelChanges.Num(), 1);
	TestEqual(TEXT("exactly at the level-5 start: 0 into the level"), Player.Progression->GetLevelProgress().XpIntoLevel, 0);

	const int32 Events = Listener->TotalEvents();
	TestTrue(TEXT("the fish sells (T-030: the sell counter pays through RecordSale)"), Player.Progression->RecordSale(1, 500));
	TestEqual(TEXT("selling gives no XP"), Player.Progression->GetTotalXp(), Level5);
	TestEqual(TEXT("selling gives no level"), Player.Progression->GetLevel(), 5);
	TestEqual(TEXT("the sale fires only a money event"), Listener->TotalEvents() - Events, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgLevelExactThresholds, "Project.Progression.QA.Level.ExactThresholdsJustUnderAndOver", QAP::Flags)
bool FQAProgLevelExactThresholds::RunTest(const FString& Parameters)
{
	// QA curve: level starts 0, 50, 130, 250, 450 (cap 5)
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	auto Land = [&](int32 Xp) { return ULureProgressionLibrary::HandleFishLanded(Player.Pawn, QAP::Fish(TEXT("QA_Fish"), 1, Xp)); };
	TestEqual(TEXT("49 XP: level 1"), Land(49).NewLevel, 1);
	TestEqual(TEXT("50 XP: level 2"), Land(1).NewLevel, 2);
	TestEqual(TEXT("129 XP: level 2"), Land(79).NewLevel, 2);
	TestEqual(TEXT("130 XP: level 3"), Land(1).NewLevel, 3);
	TestEqual(TEXT("a 0-XP fish changes nothing"), Land(0).LevelsGained, 0);
	TestEqual(TEXT("still 130 XP"), Player.Progression->GetTotalXp(), 130);
	TestEqual(TEXT("449 XP: level 4"), Land(319).NewLevel, 4);
	TestEqual(TEXT("450 XP: level 5 (the cap)"), Land(1).NewLevel, 5);
	TestTrue(TEXT("max level flag"), Player.Progression->GetLevelProgress().bIsMaxLevel);
	TestEqual(TEXT("past the cap: no level"), Land(10000).LevelsGained, 0);
	TestEqual(TEXT("XP past the cap keeps counting"), Player.Progression->GetTotalXp(), 10450);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgLevelXpSaturates, "Project.Progression.QA.Level.XpSaturatesAtMaxInt", QAP::Flags)
bool FQAProgLevelXpSaturates::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	TestEqual(TEXT("MAX_int32 XP at once: 1 -> cap"), Player.Progression->AddXp(MAX_int32), 4);
	Player.Progression->AddXp(MAX_int32);
	ULureProgressionLibrary::HandleFishLanded(Player.Pawn, QAP::Fish(TEXT("QA_Fish"), 1, MAX_int32));
	TestEqual(TEXT("XP saturates at MAX_int32 (never wraps)"), Player.Progression->GetTotalXp(), MAX_int32);
	TestEqual(TEXT("level stays at the cap"), Player.Progression->GetLevel(), 5);
	const FLureLevelProgress Progress = Player.Progression->GetLevelProgress();
	TestTrue(TEXT("progress is sane at MAX XP"), Progress.bIsMaxLevel && Progress.XpIntoLevel >= 0 && Progress.Fraction >= 0.0f && Progress.Fraction <= 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgLevelDifficultyWithoutProgression, "Project.Progression.QA.Level.DifficultyWithoutProgressionIsLevelOne", QAP::Flags)
bool FQAProgLevelDifficultyWithoutProgression::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	APawn* Lone = Env.SpawnLonePawn(*this, FVector::ZeroVector);
	if (!Lone)
	{
		return false;
	}
	for (const int32 FishLevel : { 1, 3, 10 })
	{
		TestEqual(FString::Printf(TEXT("a pawn without progression: fish level %d is judged against level 1"), FishLevel),
			ULureProgressionLibrary::GetFishDifficultyMultiplier(Lone, FishLevel), UFishLibrary::GetLevelDifficultyMultiplier(FishLevel, 1), 1e-6f);
		TestEqual(FString::Printf(TEXT("no context: fish level %d against level 1"), FishLevel),
			ULureProgressionLibrary::GetFishDifficultyMultiplier(nullptr, FishLevel), UFishLibrary::GetLevelDifficultyMultiplier(FishLevel, 1), 1e-6f);
	}
	TestTrue(TEXT("a fish 3 levels above a new player is clearly harder (shipped scaling)"), ULureProgressionLibrary::GetFishDifficultyMultiplier(Lone, 4) >= 1.5f);
	QAP::ExpectWarnings(*this, TEXT("has no player state"));
	TestFalse(TEXT("landing a fish on a pawn without progression is not accepted"),
		ULureProgressionLibrary::HandleFishLanded(Lone, QAP::Fish(TEXT("QA_Fish"), 5, 5)).bAccepted);
	TestFalse(TEXT("landing with no context is not accepted"), ULureProgressionLibrary::HandleFishLanded(nullptr, QAP::Fish(TEXT("QA_Fish"), 5, 5)).bAccepted);
	TestEqual(TEXT("caught with no progression (and no hands) loses nothing"), ULureCatchLibrary::HandlePlayerCaught(Lone), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

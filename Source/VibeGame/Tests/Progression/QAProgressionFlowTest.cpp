// Lure T-010 independent QA tests (qa-engineer): cooler, landing, selling, money and levels on a player state in a
// test world. Project.Progression.QA.{Cooler,Sell,Money,Level}.*

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/Pawn.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureSellPoint.h"
#include "Tests/FishQATestHelpers.h"
#include "Tests/Progression/QAProgressionListener.h"

namespace QAProgressionFlow
{
	/** Real catches from the one roll pipeline (the shipped fish JSON): up to Count fish over every species and seeds 1.. */
	TArray<FFishInstance> RollRealCatch(FAutomationTestBase& Test, int32 Count)
	{
		TArray<FFishInstance> Out;
		FishQA::FTables Tables;
		if (!FishQA::LoadReal(Test, Tables))
		{
			return Out;
		}
		const FFishTables View = Tables.Get();
		const TArray<FName> Species = FishQA::SortedRowNames<FFishSpeciesRow>(Tables.Species.Get());
		for (int32 Seed = 1; Seed < 200 && Out.Num() < Count; ++Seed)
		{
			for (const FName& Id : Species)
			{
				FFishInstance Fish;
				if (Out.Num() < Count && FFishRoll::Roll(View, FishQA::Ctx(Id, Seed * 7919), Fish))
				{
					Out.Add(Fish);
				}
			}
		}
		Test.TestEqual(TEXT("the roll pipeline produced the real catch"), Out.Num(), Count);
		return Out;
	}

	/** Sum of the spec's price for these fish at Multiplier */
	int64 ExpectedTotal(TConstArrayView<FFishInstance> Fish, double Multiplier)
	{
		int64 Total = 0;
		for (const FFishInstance& One : Fish)
		{
			Total += FMath::Max<int64>(1, static_cast<int64>(FMath::FloorToDouble(static_cast<double>(One.Value) * Multiplier + 0.5)));
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgCoolerShippedBasicFull, "Project.Progression.QA.Cooler.ShippedBasicFullReleasesNinthKeepsXp", QAP::Flags)
bool FQAProgCoolerShippedBasicFull::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitShipped(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	const int32 Slots = Env.Coolers->FindRow<FCoolerRow>(TEXT("Basic"), TEXT("QA"))->Slots;
	TestEqual(TEXT("a new player has the Basic cooler's slots from DT_Cooler"), Player.Cooler->GetCapacity(), Slots);
	int32 ExpectedXp = 0;
	for (int32 Index = 0; Index < Slots; ++Index)
	{
		const FLureFishLandedResult Result = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, QAP::Fish(TEXT("QA_Fish"), 10 + Index, 3, Index));
		ExpectedXp += 3;
		TestTrue(FString::Printf(TEXT("fish %d stored in slot %d"), Index + 1, Index), Result.bStoredInCooler && Result.CoolerSlot == Index);
	}
	TestTrue(TEXT("the cooler is full"), Player.Cooler->IsFull());
	const TArray<FFishInstance> Before = Player.Cooler->GetFish();

	const FLureFishLandedResult Extra = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, QAP::Fish(TEXT("QA_Big"), 999, 11, 99));
	ExpectedXp += 11;
	TestTrue(TEXT("one more fish: accepted"), Extra.bAccepted);
	TestFalse(TEXT("one more fish: released, not stored"), Extra.bStoredInCooler);
	TestEqual(TEXT("one more fish: no slot"), Extra.CoolerSlot, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("one more fish: its XP still counts"), Player.Progression->GetTotalXp(), ExpectedXp);
	TestEqual(TEXT("the full cooler still holds exactly its fish"), Player.Cooler->GetNumFish(), Slots);
	bool bSame = Before.Num() == Player.Cooler->GetFish().Num();
	for (int32 Index = 0; bSame && Index < Before.Num(); ++Index)
	{
		bSame = FishQA::Same(Before[Index], Player.Cooler->GetFish()[Index]);
	}
	TestTrue(TEXT("the released fish did not replace or change a stored fish"), bSame);

	FFishInstance Removed;
	Player.Cooler->RemoveFish(0, Removed);
	const FLureFishLandedResult After = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, QAP::Fish(TEXT("QA_Late"), 5, 1));
	TestTrue(TEXT("with one slot free the next fish goes in the last slot"), After.bStoredInCooler && After.CoolerSlot == Slots - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgCoolerKeepsRecords, "Project.Progression.QA.Cooler.KeepsRealCatchRecordsExactlyInOrder", QAP::Flags)
bool FQAProgCoolerKeepsRecords::RunTest(const FString& Parameters)
{
	const TArray<FFishInstance> Catch = QAProgressionFlow::RollRealCatch(*this, 4);
	QAP::FEnv Env;
	if (Catch.Num() != 4 || !Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	for (const FFishInstance& Fish : Catch)
	{
		ULureProgressionLibrary::HandleFishLanded(Player.Pawn, Fish);
	}
	if (!TestEqual(TEXT("4 fish in the QA Basic cooler (4 slots)"), Player.Cooler->GetNumFish(), 4))
	{
		return false;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FFishInstance Stored;
		TestTrue(FString::Printf(TEXT("slot %d holds catch %d unchanged (%s)"), Index, Index, *FishQA::Describe(Catch[Index])),
			Player.Cooler->GetFishAt(Index, Stored) && FishQA::Same(Stored, Catch[Index]));
	}
	int64 Xp = 0;
	for (const FFishInstance& Fish : Catch)
	{
		Xp += Fish.Xp;
	}
	TestEqual(TEXT("XP = the sum of the rolled fish's Xp"), static_cast<int64>(Player.Progression->GetTotalXp()), Xp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSellEmptyCooler, "Project.Progression.QA.Sell.EmptyCoolerPaysNothingAndFiresNoEvents", QAP::Flags)
bool FQAProgSellEmptyCooler::RunTest(const FString& Parameters)
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
	Player.Progression->AddMoney(12);
	UQAProgressionListener* Listener = NewObject<UQAProgressionListener>();
	TStrongObjectPtr<UQAProgressionListener> Keep(Listener);
	Listener->Listen(Player.Progression);
	const QAP::FState Before = QAP::FState::Of(Player);

	TestEqual(TEXT("quote for an empty cooler is 0"), Point->QuoteAll(Player.Pawn), 0);
	FLureSaleResult Sale = Point->SellAll(Player.Pawn);
	TestTrue(TEXT("SellAll on an empty cooler: 0 fish, 0 coins"), Sale.FishSold == 0 && Sale.MoneyEarned == 0);
	Sale = Point->SellOne(Player.Pawn, 0);
	TestTrue(TEXT("SellOne(0) on an empty cooler: 0 fish, 0 coins"), Sale.FishSold == 0 && Sale.MoneyEarned == 0);
	Sale = Player.Progression->SellAllFish(3.0f);
	TestTrue(TEXT("component SellAllFish on an empty cooler: nothing"), Sale.FishSold == 0 && Sale.MoneyEarned == 0);
	Player.Interaction->HandleInteractPressed();
	TestTrue(FString::Printf(TEXT("nothing changed (%s)"), *QAP::FState::Of(Player).Describe()), QAP::FState::Of(Player).Equals(Before));
	TestEqual(TEXT("no money, XP or level events"), Listener->TotalEvents(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSellRealCatchAtDock, "Project.Progression.QA.Sell.RealCatchAtPalmKeyDockPaysRolledValue", QAP::Flags)
bool FQAProgSellRealCatchAtDock::RunTest(const FString& Parameters)
{
	// End to end with shipped data: roll -> land -> sell at the PalmKeyDock market row.
	QAP::FEnv Env;
	if (!Env.InitShipped(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(0.0f, 150.0f, 0.0f));
	ALureSellPoint* Dock = Env.SpawnSellPoint(*this, FVector::ZeroVector, TEXT("PalmKeyDock"));
	const FFishMarketRow* Row = Env.Markets->FindRow<FFishMarketRow>(TEXT("PalmKeyDock"), TEXT("QA"), false);
	if (!Player.IsValid() || !Player.Pawn || !Dock || !TestNotNull(TEXT("PalmKeyDock row"), Row))
	{
		return false;
	}
	const TArray<FFishInstance> Catch = QAProgressionFlow::RollRealCatch(*this, Player.Cooler->GetCapacity());
	for (const FFishInstance& Fish : Catch)
	{
		ULureProgressionLibrary::HandleFishLanded(Player.Pawn, Fish);
	}
	TestEqual(TEXT("the cooler holds the catch"), Player.Cooler->GetNumFish(), Catch.Num());
	const int32 XpBefore = Player.Progression->GetTotalXp();
	const int32 LevelBefore = Player.Progression->GetLevel();
	const int64 Expected = QAProgressionFlow::ExpectedTotal(Catch, Row->SellMultiplier);
	TestEqual(TEXT("the prompt quote = what the spec's formula pays"), static_cast<int64>(Dock->QuoteAll(Player.Pawn)), Expected);

	const FLureSaleResult Sale = Dock->SellAll(Player.Pawn);
	TestEqual(TEXT("every fish sold"), Sale.FishSold, Catch.Num());
	TestEqual(TEXT("paid = sum of max(1, round-half-up(Value x dock multiplier))"), static_cast<int64>(Sale.MoneyEarned), Expected);
	TestEqual(TEXT("money = the sale"), static_cast<int64>(Player.Progression->GetMoney()), Expected);
	TestEqual(TEXT("selling gives no XP"), Player.Progression->GetTotalXp(), XpBefore);
	TestEqual(TEXT("selling changes no level"), Player.Progression->GetLevel(), LevelBefore);
	TestEqual(TEXT("the cooler is empty"), Player.Cooler->GetNumFish(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSellUnknownMarket, "Project.Progression.QA.Sell.UnknownMarketIdPaysTimesOne", QAP::Flags)
bool FQAProgSellUnknownMarket::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	ALureSellPoint* Point = Env.SpawnSellPoint(*this, FVector::ZeroVector, TEXT("QA_NoSuchBuyer"));
	if (!Player.IsValid() || !Player.Pawn || !Point)
	{
		return false;
	}
	QAP::ExpectWarnings(*this, TEXT("paying multiplier 1"));
	TestEqual(TEXT("an unknown MarketId keeps its id (no silent rename)"), Point->GetEffectiveMarketId(), FName(TEXT("QA_NoSuchBuyer")));
	TestEqual(TEXT("an unknown MarketId pays x1"), Point->GetSellMultiplier(), 1.0f, 1e-6f);
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 13, 1));
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_B"), 29, 1));
	const FLureSaleResult Sale = Point->SellAll(Player.Pawn);
	TestEqual(TEXT("both fish sold"), Sale.FishSold, 2);
	TestEqual(TEXT("paid the plain Values 13 + 29"), Sale.MoneyEarned, 42);
	TestEqual(TEXT("money 42"), Player.Progression->GetMoney(), 42);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSellInvalidMarketRow, "Project.Progression.QA.Sell.InvalidMarketRowFallsBackToTimesOne", QAP::Flags)
bool FQAProgSellInvalidMarketRow::RunTest(const FString& Parameters)
{
	// A bad row that slipped past validation (0 or negative multiplier) must not make fish worthless or cost money.
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Broken = QAP::MakeTable(FFishMarketRow::StaticStruct(),
		TEXT("Name,DisplayName,SellMultiplier,DevComment\nDefault,\"QA default\",1.0,\nQA_Zero,\"QA zero\",0,\nQA_Negative,\"QA negative\",-2,\n"));
	const QAP::FPlayer Player = Env.SpawnPlayer(*this, /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	ALureSellPoint* Zero = Env.SpawnSellPoint(*this, FVector::ZeroVector, TEXT("QA_Zero"), 300.0f, Broken.Get());
	ALureSellPoint* Negative = Env.SpawnSellPoint(*this, FVector(0.0f, 50.0f, 0.0f), TEXT("QA_Negative"), 300.0f, Broken.Get());
	if (!Player.IsValid() || !Player.Pawn || !Zero || !Negative)
	{
		return false;
	}
	QAP::ExpectWarnings(*this, TEXT("paying multiplier 1"));
	TestEqual(TEXT("a 0 multiplier row pays x1"), Zero->GetSellMultiplier(), 1.0f, 1e-6f);
	TestEqual(TEXT("a negative multiplier row pays x1"), Negative->GetSellMultiplier(), 1.0f, 1e-6f);
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 20, 1));
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_B"), 30, 1));
	TestEqual(TEXT("the 0 row pays the Value"), Zero->SellOne(Player.Pawn, 0).MoneyEarned, 20);
	TestEqual(TEXT("the negative row pays the Value"), Negative->SellAll(Player.Pawn).MoneyEarned, 30);
	TestEqual(TEXT("money never went down"), Player.Progression->GetMoney(), 50);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSellMoneySaturates, "Project.Progression.QA.Sell.MoneySaturatesAtMaxOnSale", QAP::Flags)
bool FQAProgSellMoneySaturates::RunTest(const FString& Parameters)
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
	TestTrue(TEXT("money to MAX_int32 - 3"), Player.Progression->AddMoney(MAX_int32 - 3));
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 100, 1));
	Player.Cooler->AddFish(QAP::Fish(TEXT("QA_B"), MAX_int32, 1));
	const FLureSaleResult Sale = Point->SellAll(Player.Pawn);
	TestEqual(TEXT("both fish sold"), Sale.FishSold, 2);
	TestEqual(TEXT("money saturates at MAX_int32 (never wraps negative)"), Player.Progression->GetMoney(), MAX_int32);
	TestTrue(TEXT("the reported earnings are not negative"), Sale.MoneyEarned >= 0);
	TestTrue(TEXT("spending still works at MAX"), Player.Progression->SpendMoney(MAX_int32));
	TestEqual(TEXT("0 after spending all"), Player.Progression->GetMoney(), 0);
	return true;
}

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
	ALureSellPoint* Dock = Env.SpawnSellPoint(*this, FVector::ZeroVector, TEXT("PalmKeyDock"));
	if (!Player.IsValid() || !Player.Pawn || !Dock || !TestTrue(TEXT("the shipped curve has at least 5 levels"), Player.Progression->GetMaxLevel() >= 5))
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
	const FLureSaleResult Sale = Dock->SellAll(Player.Pawn);
	TestEqual(TEXT("the fish sells"), Sale.FishSold, 1);
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
	TestEqual(TEXT("caught with no progression loses nothing"), ULureProgressionLibrary::HandlePlayerCaught(Lone), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

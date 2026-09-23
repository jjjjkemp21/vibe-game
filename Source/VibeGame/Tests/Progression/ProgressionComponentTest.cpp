// Lure T-010 tests (unreal-engineer): cooler, money, XP, levels, save data and server authority on ALurePlayerState
// (Project.Progression.Cooler.*, .Level.*, .Landed.*, .Money.*, .Authority.*, .Caught.*, .Save.*).

#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/Pawn.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionSettings.h"
#include "Progression/LureSellPoint.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Tests/Progression/ProgressionTestListener.h"

namespace ProgressionComponentTest
{
	/** Fixture tables for one test (kept alive for the test's duration) */
	struct FTables
	{
		TStrongObjectPtr<UDataTable> Levels;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Markets;

		explicit FTables(FAutomationTestBase& Test)
		{
			Levels = LPT::MakeTable(Test, FPlayerLevelRow::StaticStruct(), LPT::FixtureLevelCsv());
			Coolers = LPT::MakeTable(Test, FCoolerRow::StaticStruct(), LPT::FixtureCoolerCsv());
			Markets = LPT::MakeTable(Test, FFishMarketRow::StaticStruct(), LPT::FixtureMarketCsv());
		}
	};

	/** Money, XP, level and fish count, to prove a refused call changed nothing */
	struct FSnapshot
	{
		int32 Money = 0;
		int32 TotalXp = 0;
		int32 Level = 0;
		int32 Fish = 0;

		static FSnapshot Of(const LPT::FPlayer& Player)
		{
			FSnapshot Snapshot;
			Snapshot.Money = Player.Progression->GetMoney();
			Snapshot.TotalXp = Player.Progression->GetTotalXp();
			Snapshot.Level = Player.Progression->GetLevel();
			Snapshot.Fish = Player.Cooler->GetNumFish();
			return Snapshot;
		}

		bool operator==(const FSnapshot& Other) const
		{
			return Money == Other.Money && TotalXp == Other.TotalXp && Level == Other.Level && Fish == Other.Fish;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionCoolerLimits, "Project.Progression.Cooler.Limits", LPT::Flags)
bool FProgressionCoolerLimits::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Player.IsValid())
	{
		return false;
	}
	ULureCoolerComponent* Cooler = Player.Cooler;
	TestEqual(TEXT("starts with the default cooler row"), Cooler->GetCoolerId(), FName(TEXT("Basic")));
	TestEqual(TEXT("capacity from DT_Cooler (fixture Basic = 3)"), Cooler->GetCapacity(), 3);
	TestEqual(TEXT("starts empty"), Cooler->GetNumFish(), 0);

	int32 Slot = INDEX_NONE;
	TestTrue(TEXT("fish 1 fits"), Cooler->AddFishToSlot(LPT::MakeFish(TEXT("A"), 10, 1), Slot));
	TestEqual(TEXT("fish 1 in slot 0"), Slot, 0);
	TestTrue(TEXT("fish 2 fits"), Cooler->AddFish(LPT::MakeFish(TEXT("B"), 20, 1)));
	TestTrue(TEXT("fish 3 fits"), Cooler->AddFishToSlot(LPT::MakeFish(TEXT("C"), 30, 1), Slot));
	TestEqual(TEXT("fish 3 in slot 2"), Slot, 2);
	TestTrue(TEXT("now full"), Cooler->IsFull());
	TestEqual(TEXT("no free slots"), Cooler->GetFreeSlots(), 0);
	TestFalse(TEXT("fish 4 is refused (full)"), Cooler->AddFishToSlot(LPT::MakeFish(TEXT("D"), 40, 1), Slot));
	TestEqual(TEXT("a refused fish has no slot"), Slot, INDEX_NONE);
	TestEqual(TEXT("still 3 fish"), Cooler->GetNumFish(), 3);

	FFishInstance Removed;
	TestFalse(TEXT("removing slot 5 fails"), Cooler->RemoveFish(5, Removed));
	TestFalse(TEXT("removing slot -1 fails"), Cooler->RemoveFish(-1, Removed));
	TestTrue(TEXT("removing slot 1 works"), Cooler->RemoveFish(1, Removed));
	TestEqual(TEXT("the removed fish is B"), Removed.SpeciesId, FName(TEXT("B")));
	FFishInstance Moved;
	TestTrue(TEXT("slot 1 now holds C"), Cooler->GetFishAt(1, Moved) && Moved.SpeciesId == FName(TEXT("C")));
	TestFalse(TEXT("slot 2 is empty"), Cooler->GetFishAt(2, Moved));
	TestTrue(TEXT("room again after a removal"), Cooler->AddFish(LPT::MakeFish(TEXT("D"), 40, 1)));

	AddExpectedMessage(TEXT("got an invalid fish"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("an invalid fish (no species) is refused"), Cooler->AddFish(FFishInstance()));
	TestEqual(TEXT("Clear returns the count"), Cooler->Clear(), 3);
	TestEqual(TEXT("empty after Clear"), Cooler->GetNumFish(), 0);
	TestEqual(TEXT("Clear on an empty cooler removes nothing"), Cooler->Clear(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionCoolerUpgradeAndDowngrade, "Project.Progression.Cooler.UpgradeAndDowngrade", LPT::Flags)
bool FProgressionCoolerUpgradeAndDowngrade::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Player.IsValid())
	{
		return false;
	}
	ULureCoolerComponent* Cooler = Player.Cooler;
	TestEqual(TEXT("3 fish fill Basic"), LPT::FillCooler(Cooler, { 1, 2, 3, 4 }), 3);

	TestTrue(TEXT("upgrade to Big"), Cooler->SetCoolerId(TEXT("Big")));
	TestEqual(TEXT("Big has 5 slots"), Cooler->GetCapacity(), 5);
	TestEqual(TEXT("the fish stay"), Cooler->GetNumFish(), 3);
	TestEqual(TEXT("2 more fit"), LPT::FillCooler(Cooler, { 5, 6, 7 }), 2);

	AddExpectedMessage(TEXT("SetCoolerId 'Nope' is not a DT_Cooler row"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("an unknown row is refused"), Cooler->SetCoolerId(TEXT("Nope")));
	TestEqual(TEXT("still Big"), Cooler->GetCoolerId(), FName(TEXT("Big")));

	TestTrue(TEXT("downgrade to Tiny"), Cooler->SetCoolerId(TEXT("Tiny")));
	TestEqual(TEXT("Tiny has 1 slot"), Cooler->GetCapacity(), 1);
	TestEqual(TEXT("a smaller cooler deletes nothing"), Cooler->GetNumFish(), 5);
	TestTrue(TEXT("over capacity counts as full"), Cooler->IsFull());
	TestFalse(TEXT("no adds while over capacity"), Cooler->AddFish(LPT::MakeFish(TEXT("X"), 1, 1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelUpsAndEvents, "Project.Progression.Level.LevelUpsAndEvents", LPT::Flags)
bool FProgressionLevelUpsAndEvents::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Player.IsValid())
	{
		return false;
	}
	ULureProgressionComponent* Progression = Player.Progression;

	TestEqual(TEXT("starts at level 1"), Progression->GetLevel(), 1);
	TestEqual(TEXT("starts with 0 XP"), Progression->GetTotalXp(), 0);
	TestEqual(TEXT("max level from the table"), Progression->GetMaxLevel(), 4);

	TestEqual(TEXT("99 XP: no level"), Progression->AddXp(99), 0);
	TestEqual(TEXT("still level 1"), Progression->GetLevel(), 1);
	TestEqual(TEXT("+1 XP: exactly one level"), Progression->AddXp(1), 1);
	TestEqual(TEXT("level 2 at 100 XP"), Progression->GetLevel(), 2);
	TestEqual(TEXT("0 into level 2"), Progression->GetLevelProgress().XpIntoLevel, 0);
	TestEqual(TEXT("level 2 needs 150"), Progression->GetLevelProgress().XpForNextLevel, 150);

	TestEqual(TEXT("a multi-level jump (100 -> 460 XP) gains 2 levels at once"), Progression->AddXp(360), 2);
	TestEqual(TEXT("level 4 (the cap)"), Progression->GetLevel(), 4);
	TestTrue(TEXT("max level"), Progression->GetLevelProgress().bIsMaxLevel);
	TestEqual(TEXT("XP past the cap: no more levels"), Progression->AddXp(1000), 0);
	TestEqual(TEXT("still level 4"), Progression->GetLevel(), 4);
	TestEqual(TEXT("XP past the cap still counts"), Progression->GetTotalXp(), 1460);

	TestEqual(TEXT("0 XP does nothing"), Progression->AddXp(0), 0);
	TestEqual(TEXT("negative XP does nothing"), Progression->AddXp(-50), 0);
	TestEqual(TEXT("XP unchanged"), Progression->GetTotalXp(), 1460);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelEventsFireOncePerGain, "Project.Progression.Level.EventsFireOncePerGain", LPT::Flags)
bool FProgressionLevelEventsFireOncePerGain::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Player.IsValid())
	{
		return false;
	}
	ULureProgressionComponent* Progression = Player.Progression;
	ULureProgressionTestListener* Listener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> Keep(Listener);
	Listener->Listen(Progression);

	Progression->AddXp(50);
	TestEqual(TEXT("no level-up below 100 XP"), Listener->LevelUps.Num(), 0);
	TestEqual(TEXT("one XP event"), Listener->XpDeltas.Num(), 1);

	Progression->AddXp(400); // 450: level 1 -> 4
	if (TestEqual(TEXT("a multi-level jump fires ONE level-up"), Listener->LevelUps.Num(), 1))
	{
		TestEqual(TEXT("... from level 1"), Listener->LevelUps[0].Key, 1);
		TestEqual(TEXT("... to level 4"), Listener->LevelUps[0].Value, 4);
	}
	TestEqual(TEXT("one level-changed event too"), Listener->LevelChanges.Num(), 1);

	Progression->AddXp(10); // past the cap
	TestEqual(TEXT("no level-up past the cap"), Listener->LevelUps.Num(), 1);

	Progression->AddMoney(25);
	Progression->SpendMoney(5);
	TestTrue(TEXT("money events carry the deltas (+25, -5)"), Listener->MoneyDeltas == TArray<int32>({ 25, -5 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelClientLevelUpEvent, "Project.Progression.Level.ClientLevelUpEvent", LPT::Flags)
bool FProgressionLevelClientLevelUpEvent::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Player.IsValid())
	{
		return false;
	}
	ULureProgressionComponent* Progression = Player.Progression;
	ULureProgressionTestListener* Listener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> Keep(Listener);
	Listener->Listen(Progression);

	// The client path: a replicated Level arriving after BeginPlay fires OnLevelUp with the old and new level.
	Player.State->SetRole(ROLE_SimulatedProxy);
	AddExpectedMessage(TEXT("is server-only"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	TestEqual(TEXT("a client can't add XP"), Progression->AddXp(500), 0);
	TestEqual(TEXT("no event from the refused call"), Listener->LevelUps.Num(), 0);

	Progression->OnRep_Level(1); // as if Level had replicated as 1 -> 1: nothing rose
	TestEqual(TEXT("same level: no level-up"), Listener->LevelUps.Num(), 0);
	TestEqual(TEXT("... but a level-changed event"), Listener->LevelChanges.Num(), 1);
	Progression->OnRep_Level(0); // Level (1) is above the old value 0: counts as a rise after BeginPlay
	TestEqual(TEXT("a rise fires one level-up on the client"), Listener->LevelUps.Num(), 1);
	Progression->OnRep_Money(0);
	TestEqual(TEXT("OnRep_Money fires a money event"), Listener->MoneyDeltas.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLandedXpAndCooler, "Project.Progression.Landed.XpAndCooler", LPT::Flags)
bool FProgressionLandedXpAndCooler::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get(), /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	ULureProgressionTestListener* Listener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> Keep(Listener);
	Listener->Listen(Player.Progression);

	// Through the library from the pawn, as T-007's fishing component will call it.
	FLureFishLandedResult Result = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, LPT::MakeFish(TEXT("A"), 10, 60));
	TestTrue(TEXT("accepted"), Result.bAccepted);
	TestTrue(TEXT("stored"), Result.bStoredInCooler);
	TestEqual(TEXT("slot 0"), Result.CoolerSlot, 0);
	TestEqual(TEXT("XP = the fish's Xp"), Result.XpGained, 60);
	TestEqual(TEXT("no level yet"), Result.LevelsGained, 0);
	TestEqual(TEXT("total XP 60"), Player.Progression->GetTotalXp(), 60);

	Result = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, LPT::MakeFish(TEXT("B"), 10, 200));
	TestEqual(TEXT("260 XP: 1 -> 3"), Result.LevelsGained, 2);
	TestEqual(TEXT("new level 3"), Result.NewLevel, 3);

	ULureProgressionLibrary::HandleFishLanded(Player.Pawn, LPT::MakeFish(TEXT("C"), 10, 5));
	TestTrue(TEXT("the cooler is full (3)"), Player.Cooler->IsFull());
	Result = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, LPT::MakeFish(TEXT("D"), 10, 7));
	TestTrue(TEXT("full cooler: still accepted"), Result.bAccepted);
	TestFalse(TEXT("full cooler: released, not stored"), Result.bStoredInCooler);
	TestEqual(TEXT("full cooler: the XP still counts"), Result.XpGained, 7);
	TestEqual(TEXT("still 3 fish"), Player.Cooler->GetNumFish(), 3);

	AddExpectedMessage(TEXT("invalid fish"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	const int32 XpBefore = Player.Progression->GetTotalXp();
	Result = Player.Progression->HandleFishLanded(FFishInstance());
	TestFalse(TEXT("an invalid fish is not accepted"), Result.bAccepted);
	TestEqual(TEXT("an invalid fish gives no XP"), Player.Progression->GetTotalXp(), XpBefore);
	TestEqual(TEXT("OnFishLanded fired for the 4 accepted fish"), Listener->FishLanded, 4);

	TestTrue(TEXT("the library finds the progression from the pawn"), ULureProgressionLibrary::GetProgression(Player.Pawn) == Player.Progression);
	TestTrue(TEXT("the library finds the cooler from the pawn"), ULureProgressionLibrary::GetCooler(Player.Pawn) == Player.Cooler);
	TestTrue(TEXT("the library finds them from the player state"), ULureProgressionLibrary::GetProgression(Player.State) == Player.Progression);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelDifficultyForPlayer, "Project.Progression.Level.DifficultyForPlayer", LPT::Flags)
bool FProgressionLevelDifficultyForPlayer::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get(), /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	FFishLevelScaling Scaling;
	Scaling.OverLevelFactor = 0.5f;
	Scaling.UnderLevelFactor = 0.1f;
	Scaling.MinMultiplier = 0.5f;
	Scaling.MaxMultiplier = 4.0f;

	Player.Progression->AddXp(250); // level 3
	TestEqual(TEXT("player level 3"), Player.Progression->GetLevel(), 3);
	TestEqual(TEXT("a level-3 fish: 1"), Player.Progression->GetFishDifficultyMultiplierWith(3, Scaling), 1.0f, 1e-5f);
	TestEqual(TEXT("a level-5 fish (2 above): 2"), Player.Progression->GetFishDifficultyMultiplierWith(5, Scaling), 2.0f, 1e-5f);
	TestEqual(TEXT("a level-1 fish (2 below): 0.8"), Player.Progression->GetFishDifficultyMultiplierWith(1, Scaling), 0.8f, 1e-5f);
	for (int32 FishLevel = 1; FishLevel <= 10; ++FishLevel)
	{
		TestEqual(FString::Printf(TEXT("fish level %d = the fish system's hook at player level 3"), FishLevel),
			Player.Progression->GetFishDifficultyMultiplierWith(FishLevel, Scaling), FFishRoll::LevelDifficultyMultiplier(FishLevel, 3, Scaling), 1e-6f);
	}
	TestEqual(TEXT("the library uses the player's level and the settings' scaling"), ULureProgressionLibrary::GetFishDifficultyMultiplier(Player.Pawn, 6),
		UFishLibrary::GetLevelDifficultyMultiplier(6, 3), 1e-6f);

	Player.Progression->AddXp(200); // level 4
	TestTrue(TEXT("levelling up makes the same fish easier"), Player.Progression->GetFishDifficultyMultiplierWith(5, Scaling) < 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionMoneySpendAndSaturate, "Project.Progression.Money.SpendAndSaturate", LPT::Flags)
bool FProgressionMoneySpendAndSaturate::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Player.IsValid())
	{
		return false;
	}
	ULureProgressionComponent* Progression = Player.Progression;
	TestEqual(TEXT("starting money (settings StartingMoney)"), Progression->GetMoney(), GetDefault<ULureProgressionSettings>()->StartingMoney);
	const int32 Start = Progression->GetMoney();
	TestTrue(TEXT("add 100"), Progression->AddMoney(100));
	TestFalse(TEXT("add 0 is refused"), Progression->AddMoney(0));
	TestFalse(TEXT("add -5 is refused"), Progression->AddMoney(-5));
	TestFalse(TEXT("spending more than you have fails"), Progression->SpendMoney(Start + 101));
	TestEqual(TEXT("... and spends nothing"), Progression->GetMoney(), Start + 100);
	TestTrue(TEXT("spend exactly what you have"), Progression->SpendMoney(Start + 100));
	TestEqual(TEXT("0 left"), Progression->GetMoney(), 0);
	TestFalse(TEXT("spend 0 is refused"), Progression->SpendMoney(0));
	TestTrue(TEXT("add a lot"), Progression->AddMoney(MAX_int32 - 10));
	TestTrue(TEXT("add more"), Progression->AddMoney(100));
	TestEqual(TEXT("money saturates instead of wrapping"), Progression->GetMoney(), MAX_int32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionAuthorityClientCannotChangeState, "Project.Progression.Authority.ClientCannotChangeState", LPT::Flags)
bool FProgressionAuthorityClientCannotChangeState::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get(), /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	ALureSellPoint* Point = LPT::SpawnSellPoint(*this, World, FVector::ZeroVector, Tables.Markets.Get());
	if (!Player.IsValid() || !Player.Pawn || !Point)
	{
		return false;
	}
	// Server state first: 2 fish, 50 coins, level 2.
	LPT::FillCooler(Player.Cooler, { 10, 20 });
	Player.Progression->AddMoney(50);
	Player.Progression->AddXp(120);
	const FLureProgressSaveData Before = Player.Progression->GetSaveData();
	const FSnapshot Snapshot = FSnapshot::Of(Player);

	AddExpectedMessage(TEXT("is server-only"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	for (const ENetRole Role : { ROLE_SimulatedProxy, ROLE_AutonomousProxy })
	{
		const FString Label = Role == ROLE_SimulatedProxy ? TEXT("simulated client") : TEXT("owning client");
		Player.State->SetRole(Role);
		Player.Pawn->SetRole(Role);

		TestFalse(Label + TEXT(": AddFish refused"), Player.Cooler->AddFish(LPT::MakeFish(TEXT("Z"), 999, 999)));
		FFishInstance Out;
		TestFalse(Label + TEXT(": RemoveFish refused"), Player.Cooler->RemoveFish(0, Out));
		TestEqual(Label + TEXT(": Clear removes nothing"), Player.Cooler->Clear(), 0);
		TestEqual(Label + TEXT(": TakeAll takes nothing"), Player.Cooler->TakeAll().Num(), 0);
		TestFalse(Label + TEXT(": SetCoolerId refused"), Player.Cooler->SetCoolerId(TEXT("Big")));
		TestFalse(Label + TEXT(": AddMoney refused"), Player.Progression->AddMoney(1000));
		TestFalse(Label + TEXT(": SpendMoney refused"), Player.Progression->SpendMoney(10));
		TestEqual(Label + TEXT(": AddXp refused"), Player.Progression->AddXp(1000), 0);
		TestFalse(Label + TEXT(": HandleFishLanded refused"), Player.Progression->HandleFishLanded(LPT::MakeFish(TEXT("Z"), 999, 999)).bAccepted);
		TestEqual(Label + TEXT(": SellAllFish sells nothing"), Player.Progression->SellAllFish(1.0f).FishSold, 0);
		TestEqual(Label + TEXT(": SellOneFish sells nothing"), Player.Progression->SellOneFish(0, 1.0f).FishSold, 0);
		FLureProgressSaveData Cheat = Before;
		Cheat.Money = 1000000;
		TestFalse(Label + TEXT(": ApplySaveData refused"), Player.Progression->ApplySaveData(Cheat));
		TestFalse(Label + TEXT(": TryInteract refused"), Player.Interaction->TryInteract(Point));
		TestEqual(Label + TEXT(": the library's caught rule removes nothing"), ULureProgressionLibrary::HandlePlayerCaught(Player.Pawn), 0);
		TestTrue(Label + TEXT(": nothing changed"), FSnapshot::Of(Player) == Snapshot);
	}
	Player.State->SetRole(ROLE_Authority);
	Player.Pawn->SetRole(ROLE_Authority);
	TestTrue(TEXT("back on the server the state is intact"), FSnapshot::Of(Player) == Snapshot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionCaughtClearsCoolerKeepsProgress, "Project.Progression.Caught.ClearsCoolerKeepsProgress", LPT::Flags)
bool FProgressionCaughtClearsCoolerKeepsProgress::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get(), /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	LPT::FillCooler(Player.Cooler, { 10, 20, 30 });
	Player.Progression->AddMoney(75);
	Player.Progression->AddXp(300);
	const int32 Money = Player.Progression->GetMoney();
	const int32 Xp = Player.Progression->GetTotalXp();
	const int32 Level = Player.Progression->GetLevel();

	TestEqual(TEXT("caught: 3 unsold fish lost"), ULureProgressionLibrary::HandlePlayerCaught(Player.Pawn), 3);
	TestEqual(TEXT("the cooler is empty"), Player.Cooler->GetNumFish(), 0);
	TestEqual(TEXT("money kept"), Player.Progression->GetMoney(), Money);
	TestEqual(TEXT("XP kept"), Player.Progression->GetTotalXp(), Xp);
	TestEqual(TEXT("level kept"), Player.Progression->GetLevel(), Level);
	TestEqual(TEXT("the cooler row is kept (an upgrade is not lost)"), Player.Cooler->GetCoolerId(), FName(TEXT("Basic")));
	TestEqual(TEXT("caught with an empty cooler loses nothing"), ULureProgressionLibrary::HandlePlayerCaught(Player.Pawn), 0);

	// The progression lives on the player state, so a new pawn (respawn) sees the same values.
	APawn* NewPawn = World.World->SpawnActor<APawn>();
	if (NewPawn)
	{
		Player.Pawn->SetPlayerState(nullptr);
		NewPawn->SetPlayerState(Player.State);
		Player.Pawn->Destroy();
		TestTrue(TEXT("after respawn: same progression"), ULureProgressionLibrary::GetProgression(NewPawn) == Player.Progression);
		TestEqual(TEXT("after respawn: money kept"), Player.Progression->GetMoney(), Money);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionSaveRoundTrip, "Project.Progression.Save.RoundTrip", LPT::Flags)
bool FProgressionSaveRoundTrip::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Source = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	const LPT::FPlayer Target = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Source.IsValid() || !Target.IsValid())
	{
		return false;
	}
	Source.Cooler->SetCoolerId(TEXT("Big"));
	LPT::FillCooler(Source.Cooler, { 11, 22, 33 });
	Source.Progression->AddMoney(321);
	Source.Progression->AddXp(260);
	const FLureProgressSaveData Saved = Source.Progression->GetSaveData();
	TestEqual(TEXT("save: money"), Saved.Money, 321);
	TestEqual(TEXT("save: XP"), Saved.TotalXp, 260);
	TestEqual(TEXT("save: level"), Saved.Level, 3);
	TestEqual(TEXT("save: cooler row"), Saved.CoolerId, FName(TEXT("Big")));
	TestEqual(TEXT("save: 3 fish"), Saved.CoolerFish.Num(), 3);
	TestEqual(TEXT("save: version"), Saved.Version, FLureProgressSaveData::CurrentVersion);

	// Through the archive setup a USaveGame uses (SaveGame properties only), as T-019 will.
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails*/ false);
		Ar.ArIsSaveGame = true;
		FLureProgressSaveData::StaticStruct()->SerializeItem(Ar, const_cast<FLureProgressSaveData*>(&Saved), nullptr);
	}
	FLureProgressSaveData Loaded;
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Ar(Reader, /*bInLoadIfFindFails*/ true);
		Ar.ArIsSaveGame = true;
		FLureProgressSaveData::StaticStruct()->SerializeItem(Ar, &Loaded, nullptr);
	}
	TestTrue(TEXT("the save struct round-trips through a SaveGame archive"), FLureProgressSaveData::StaticStruct()->CompareScriptStruct(&Saved, &Loaded, PPF_None));

	ULureProgressionTestListener* Listener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> Keep(Listener);
	Listener->Listen(Target.Progression);
	TestTrue(TEXT("apply on another player"), Target.Progression->ApplySaveData(Loaded));
	const FLureProgressSaveData Resaved = Target.Progression->GetSaveData();
	TestTrue(TEXT("the loaded player saves the same data"), FLureProgressSaveData::StaticStruct()->CompareScriptStruct(&Saved, &Resaved, PPF_None));
	TestEqual(TEXT("loaded cooler capacity (Big)"), Target.Cooler->GetCapacity(), 5);
	TestEqual(TEXT("loading is not a level-up"), Listener->LevelUps.Num(), 0);
	TestEqual(TEXT("... but the level changed"), Listener->LevelChanges.Num(), 1);

	// Level rules on load: the saved level is a floor; extra XP (a rebalanced curve) levels up; the cap still applies.
	FLureProgressSaveData Floor = Saved;
	Floor.TotalXp = 10;
	Floor.Level = 3;
	Target.Progression->ApplySaveData(Floor);
	TestEqual(TEXT("a saved level above the XP is kept"), Target.Progression->GetLevel(), 3);
	FLureProgressSaveData Ahead = Saved;
	Ahead.TotalXp = 460;
	Ahead.Level = 1;
	Target.Progression->ApplySaveData(Ahead);
	TestEqual(TEXT("XP above the saved level levels up"), Target.Progression->GetLevel(), 4);
	FLureProgressSaveData TooHigh = Saved;
	TooHigh.Level = 99;
	Target.Progression->ApplySaveData(TooHigh);
	TestEqual(TEXT("a saved level past the curve is capped"), Target.Progression->GetLevel(), 4);

	// Cooler rules on load: invalid records are dropped, nothing is trimmed to capacity.
	FLureProgressSaveData Crowded = Saved;
	Crowded.CoolerId = TEXT("Tiny");
	Crowded.CoolerFish.Add(FFishInstance());
	Target.Progression->ApplySaveData(Crowded);
	TestEqual(TEXT("an invalid saved fish is dropped, the 3 valid ones kept in a 1-slot cooler"), Target.Cooler->GetNumFish(), 3);
	FLureProgressSaveData Negative = Saved;
	Negative.Money = -40;
	Negative.TotalXp = -1;
	Target.Progression->ApplySaveData(Negative);
	TestEqual(TEXT("negative saved money loads as 0"), Target.Progression->GetMoney(), 0);
	TestEqual(TEXT("negative saved XP loads as 0"), Target.Progression->GetTotalXp(), 0);
	FLureProgressSaveData NoCooler = Saved;
	NoCooler.CoolerId = NAME_None;
	Target.Progression->ApplySaveData(NoCooler);
	TestEqual(TEXT("no saved cooler row = the default cooler"), Target.Cooler->GetCoolerId(), FName(TEXT("Basic")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionSaveUnknownCoolerUsesDefaultRow, "Project.Progression.Save.UnknownCoolerUsesDefaultRow", LPT::Flags)
bool FProgressionSaveUnknownCoolerUsesDefaultRow::RunTest(const FString& Parameters)
{
	// QA T010-O2/O3: a saved cooler id that DT_Cooler no longer has switches to the DefaultCoolerId row (not FallbackCoolerSlots);
	// FallbackCoolerSlots is used only when the table itself is missing.
	using namespace ProgressionComponentTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	const LPT::FPlayer NoTable = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get());
	if (!Player.IsValid() || !NoTable.IsValid())
	{
		return false;
	}
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	TestEqual(TEXT("fixture: the default cooler row is Basic"), Settings->DefaultCoolerId, FName(TEXT("Basic")));

	FLureProgressSaveData Save;
	Save.CoolerId = TEXT("RemovedCooler");
	Save.CoolerFish = { LPT::MakeFish(TEXT("A"), 10, 1), LPT::MakeFish(TEXT("B"), 20, 1) };
	AddExpectedMessage(TEXT("has no row 'RemovedCooler'; using the default cooler row 'Basic'"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("has no row 'Nope'; using the default cooler row 'Basic'"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Player.Progression->ApplySaveData(Save);
	TestEqual(TEXT("an unknown saved cooler id becomes the default row"), Player.Cooler->GetCoolerId(), FName(TEXT("Basic")));
	TestEqual(TEXT("... with the default row's size (fixture Basic = 3), not FallbackCoolerSlots"), Player.Cooler->GetCapacity(), 3);
	TestEqual(TEXT("the fish are kept"), Player.Cooler->GetNumFish(), 2);
	TestEqual(TEXT("the next save stores the default row"), Player.Progression->GetSaveData().CoolerId, FName(TEXT("Basic")));
	TestEqual(TEXT("ResolveSlots of an unknown row = the default row's slots"), Player.Cooler->ResolveSlots(TEXT("Nope")), 3);

	// Only a missing table uses the C++ fallback size; the id is kept (nothing to check it against).
	AddExpectedMessage(TEXT("FallbackCoolerSlots"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	NoTable.Cooler->SetCoolerTable(nullptr);
	NoTable.Progression->ApplySaveData(Save);
	TestEqual(TEXT("no DT_Cooler: FallbackCoolerSlots"), NoTable.Cooler->GetCapacity(), FMath::Max(1, Settings->FallbackCoolerSlots));
	TestEqual(TEXT("no DT_Cooler: the saved id is kept"), NoTable.Cooler->GetCoolerId(), FName(TEXT("RemovedCooler")));
	TestEqual(TEXT("no DT_Cooler: the fish are kept"), NoTable.Cooler->GetNumFish(), 2);

	// The fallback size lives in one place (C++): DefaultGame.ini must not set a second copy.
	FString Ini;
	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultGame.ini");
	if (TestTrue(TEXT("DefaultGame.ini is readable"), FFileHelper::LoadFileToString(Ini, *IniPath)))
	{
		TArray<FString> Lines;
		Ini.ParseIntoArrayLines(Lines);
		bool bSetInIni = false;
		for (const FString& Line : Lines)
		{
			bSetInIni |= Line.TrimStart().StartsWith(TEXT("FallbackCoolerSlots="));
		}
		TestFalse(TEXT("FallbackCoolerSlots is not set in DefaultGame.ini (C++ default only)"), bSetInIni);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

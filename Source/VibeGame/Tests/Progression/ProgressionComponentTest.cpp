// Lure T-010 tests (unreal-engineer): money, XP, levels, the landing XP, save data and server authority on ALurePlayerState
// (Project.Progression.Level.*, .Landed.*, .Money.*, .Authority.*, .Save.*). T-030 moved the cooler to the physical
// ALureCoolerActor (Project.Catch.*); the old Cooler.*, Caught.* and Save.UnknownCoolerUsesDefaultRow tests moved there.

#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/Pawn.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionSettings.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Tests/Progression/ProgressionTestListener.h"

namespace ProgressionComponentTest
{
	/** Money, XP and level, to prove a refused call changed nothing */
	struct FSnapshot
	{
		int32 Money = 0;
		int32 TotalXp = 0;
		int32 Level = 0;

		static FSnapshot Of(const LPT::FPlayer& Player)
		{
			FSnapshot Snapshot;
			Snapshot.Money = Player.Progression->GetMoney();
			Snapshot.TotalXp = Player.Progression->GetTotalXp();
			Snapshot.Level = Player.Progression->GetLevel();
			return Snapshot;
		}

		bool operator==(const FSnapshot& Other) const
		{
			return Money == Other.Money && TotalXp == Other.TotalXp && Level == Other.Level;
		}
	};

	TStrongObjectPtr<UDataTable> Levels(FAutomationTestBase& Test)
	{
		return LPT::MakeTable(Test, FPlayerLevelRow::StaticStruct(), LPT::FixtureLevelCsv());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelUpsAndEvents, "Project.Progression.Level.LevelUpsAndEvents", LPT::Flags)
bool FProgressionLevelUpsAndEvents::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Levels.Get());
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
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Levels.Get());
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
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Levels.Get());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLandedXp, "Project.Progression.Landed.XpOnLanding", LPT::Flags)
bool FProgressionLandedXp::RunTest(const FString& Parameters)
{
	// T-030: the progression's part of landing is the XP (and OnFishLanded); the fish itself goes on the hook
	// (ULureCatchLibrary::HandleFishLanded, Project.Catch.Landing.*).
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Levels.Get(), /*bWithPawn*/ true);
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
	ULureProgressionTestListener* Listener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> Keep(Listener);
	Listener->Listen(Player.Progression);

	FLureFishLandedResult Result = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, LPT::MakeFish(TEXT("A"), 10, 60));
	TestTrue(TEXT("accepted"), Result.bAccepted);
	TestFalse(TEXT("the progression part never hangs a fish"), Result.bOnHook);
	TestEqual(TEXT("XP = the fish's Xp"), Result.XpGained, 60);
	TestEqual(TEXT("no level yet"), Result.LevelsGained, 0);
	TestEqual(TEXT("total XP 60"), Player.Progression->GetTotalXp(), 60);

	Result = ULureProgressionLibrary::HandleFishLanded(Player.Pawn, LPT::MakeFish(TEXT("B"), 10, 200));
	TestEqual(TEXT("260 XP: 1 -> 3"), Result.LevelsGained, 2);
	TestEqual(TEXT("new level 3"), Result.NewLevel, 3);

	AddExpectedMessage(TEXT("invalid fish"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	const int32 XpBefore = Player.Progression->GetTotalXp();
	Result = Player.Progression->HandleFishLanded(FFishInstance());
	TestFalse(TEXT("an invalid fish is not accepted"), Result.bAccepted);
	TestEqual(TEXT("an invalid fish gives no XP"), Player.Progression->GetTotalXp(), XpBefore);
	TestEqual(TEXT("OnFishLanded fired for the 2 accepted fish"), Listener->FishLanded, 2);

	TestTrue(TEXT("the library finds the progression from the pawn"), ULureProgressionLibrary::GetProgression(Player.Pawn) == Player.Progression);
	TestTrue(TEXT("the library finds it from the player state"), ULureProgressionLibrary::GetProgression(Player.State) == Player.Progression);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionLevelDifficultyForPlayer, "Project.Progression.Level.DifficultyForPlayer", LPT::Flags)
bool FProgressionLevelDifficultyForPlayer::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Levels.Get(), /*bWithPawn*/ true);
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
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Levels.Get());
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

	// T-030: the sell counter pays through RecordSale.
	TestFalse(TEXT("RecordSale of 0 fish pays nothing"), Progression->RecordSale(0, 50));
	TestEqual(TEXT("... money unchanged"), Progression->GetMoney(), MAX_int32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionAuthorityClientCannotChangeState, "Project.Progression.Authority.ClientCannotChangeState", LPT::Flags)
bool FProgressionAuthorityClientCannotChangeState::RunTest(const FString& Parameters)
{
	using namespace ProgressionComponentTest;
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Levels.Get(), /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	if (!Player.IsValid() || !Player.Pawn)
	{
		return false;
	}
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

		TestFalse(Label + TEXT(": AddMoney refused"), Player.Progression->AddMoney(1000));
		TestFalse(Label + TEXT(": SpendMoney refused"), Player.Progression->SpendMoney(10));
		TestEqual(Label + TEXT(": AddXp refused"), Player.Progression->AddXp(1000), 0);
		TestFalse(Label + TEXT(": HandleFishLanded refused"), Player.Progression->HandleFishLanded(LPT::MakeFish(TEXT("Z"), 999, 999)).bAccepted);
		TestFalse(Label + TEXT(": RecordSale refused"), Player.Progression->RecordSale(3, 999));
		FLureProgressSaveData Cheat = Before;
		Cheat.Money = 1000000;
		TestFalse(Label + TEXT(": ApplySaveData refused"), Player.Progression->ApplySaveData(Cheat));
		TestTrue(Label + TEXT(": nothing changed"), FSnapshot::Of(Player) == Snapshot);
	}
	Player.State->SetRole(ROLE_Authority);
	Player.Pawn->SetRole(ROLE_Authority);
	TestTrue(TEXT("back on the server the state is intact"), FSnapshot::Of(Player) == Snapshot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionSaveRoundTrip, "Project.Progression.Save.RoundTrip", LPT::Flags)
bool FProgressionSaveRoundTrip::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Levels = ProgressionComponentTest::Levels(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Source = LPT::SpawnPlayer(*this, World, Levels.Get());
	const LPT::FPlayer Target = LPT::SpawnPlayer(*this, World, Levels.Get());
	if (!Source.IsValid() || !Target.IsValid())
	{
		return false;
	}
	Source.Progression->AddMoney(321);
	Source.Progression->AddXp(260);
	const FLureProgressSaveData Saved = Source.Progression->GetSaveData();
	TestEqual(TEXT("save: money"), Saved.Money, 321);
	TestEqual(TEXT("save: XP"), Saved.TotalXp, 260);
	TestEqual(TEXT("save: level"), Saved.Level, 3);
	TestEqual(TEXT("save: version 2 (T-030: no abstract cooler)"), Saved.Version, 2);
	TestEqual(TEXT("save: the current version"), Saved.Version, FLureProgressSaveData::CurrentVersion);

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
	FLureProgressSaveData Negative = Saved;
	Negative.Money = -40;
	Negative.TotalXp = -1;
	Target.Progression->ApplySaveData(Negative);
	TestEqual(TEXT("negative saved money loads as 0"), Target.Progression->GetMoney(), 0);
	TestEqual(TEXT("negative saved XP loads as 0"), Target.Progression->GetTotalXp(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

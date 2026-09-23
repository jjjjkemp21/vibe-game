// Lure T-010 independent QA tests (qa-engineer): save data (FLureProgressSaveData), apply on load, and seamless travel.
// Project.Progression.QA.Save.*

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Game/LurePlayerState.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Tests/FishQATestHelpers.h"
#include "Tests/Progression/QAProgressionListener.h"

namespace QAProgressionSave
{
	/** Serializes Data the way a USaveGame does (SaveGame properties only) and reads it back */
	FLureProgressSaveData ThroughSaveArchive(const FLureProgressSaveData& Data)
	{
		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails*/ false);
			Ar.ArIsSaveGame = true;
			FLureProgressSaveData::StaticStruct()->SerializeItem(Ar, const_cast<FLureProgressSaveData*>(&Data), nullptr);
		}
		FLureProgressSaveData Loaded;
		Loaded.Money = -12345; // must be overwritten
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Ar(Reader, /*bInLoadIfFindFails*/ true);
			Ar.ArIsSaveGame = true;
			FLureProgressSaveData::StaticStruct()->SerializeItem(Ar, &Loaded, nullptr);
		}
		return Loaded;
	}

	TArray<FFishInstance> RealFish(FAutomationTestBase& Test, int32 Count)
	{
		TArray<FFishInstance> Out;
		FishQA::FTables Tables;
		if (!FishQA::LoadReal(Test, Tables))
		{
			return Out;
		}
		const FFishTables View = Tables.Get();
		for (int32 Seed = 11; Seed < 400 && Out.Num() < Count; ++Seed)
		{
			for (const FName& Id : FishQA::SortedRowNames<FFishSpeciesRow>(Tables.Species.Get()))
			{
				FFishInstance Fish;
				FFishRollContext Context = FishQA::Ctx(Id, Seed * 104729);
				Context.Luck = 1.0f; // more rarities and modifiers in the records
				if (Out.Num() < Count && FFishRoll::Roll(View, Context, Fish))
				{
					Out.Add(Fish);
				}
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSaveRealCatchRoundTrip, "Project.Progression.QA.Save.RealCatchRoundTripsExactly", QAP::Flags)
bool FQAProgSaveRealCatchRoundTrip::RunTest(const FString& Parameters)
{
	const TArray<FFishInstance> Catch = QAProgressionSave::RealFish(*this, 9);
	QAP::FEnv Env;
	if (!TestEqual(TEXT("9 real fish rolled"), Catch.Num(), 9) || !Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Source = Env.SpawnPlayer(*this);
	const QAP::FPlayer Target = Env.SpawnPlayer(*this);
	if (!Source.IsValid() || !Target.IsValid())
	{
		return false;
	}
	Source.Cooler->SetCoolerId(TEXT("Mega"));
	for (const FFishInstance& Fish : Catch)
	{
		Source.Cooler->AddFish(Fish);
	}
	Source.Progression->AddMoney(987654);
	Source.Progression->AddXp(301);

	const FLureProgressSaveData Saved = QAProgressionSave::ThroughSaveArchive(Source.Progression->GetSaveData());
	TestEqual(TEXT("saved money"), Saved.Money, 987654);
	TestEqual(TEXT("saved XP"), Saved.TotalXp, 301);
	TestEqual(TEXT("saved level (301 XP on the QA curve = 4)"), Saved.Level, 4);
	TestEqual(TEXT("saved cooler row"), Saved.CoolerId, FName(TEXT("Mega")));
	TestEqual(TEXT("saved version"), Saved.Version, FLureProgressSaveData::CurrentVersion);
	TestTrue(TEXT("apply on a fresh player"), Target.Progression->ApplySaveData(Saved));
	const QAP::FState Loaded = QAP::FState::Of(Target);
	TestTrue(FString::Printf(TEXT("the loaded player equals the saved one (%s vs %s)"), *Loaded.Describe(), *QAP::FState::Of(Source).Describe()),
		Loaded.Equals(QAP::FState::Of(Source)));
	for (int32 Index = 0; Index < FMath::Min(Catch.Num(), Target.Cooler->GetNumFish()); ++Index)
	{
		TestTrue(FString::Printf(TEXT("fish %d unchanged after save/load (never re-rolled or re-priced): %s"), Index, *FishQA::Describe(Catch[Index])),
			FishQA::Same(Target.Cooler->GetFish()[Index], Catch[Index]));
	}
	TestEqual(TEXT("loaded capacity from the saved row"), Target.Cooler->GetCapacity(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSaveApplyReplaces, "Project.Progression.QA.Save.ApplyReplacesAndIsIdempotent", QAP::Flags)
bool FQAProgSaveApplyReplaces::RunTest(const FString& Parameters)
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
	// Existing progress that the load must replace, not merge with
	Player.Cooler->SetCoolerId(TEXT("Mega"));
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Player.Cooler->AddFish(QAP::Fish(TEXT("QA_Old"), 5, 1, Index));
	}
	Player.Progression->AddMoney(500);
	Player.Progression->AddXp(460); // level 5

	FLureProgressSaveData Save;
	Save.Money = 42;
	Save.TotalXp = 60;
	Save.Level = 2;
	Save.CoolerId = TEXT("Basic");
	Save.CoolerFish = { QAP::Fish(TEXT("QA_New1"), 7, 1, 1), QAP::Fish(TEXT("QA_New2"), 9, 1, 2) };

	UQAProgressionListener* Listener = NewObject<UQAProgressionListener>();
	TStrongObjectPtr<UQAProgressionListener> Keep(Listener);
	Listener->Listen(Player.Progression);
	TestTrue(TEXT("apply"), Player.Progression->ApplySaveData(Save));
	TestEqual(TEXT("money replaced"), Player.Progression->GetMoney(), 42);
	TestEqual(TEXT("XP replaced"), Player.Progression->GetTotalXp(), 60);
	TestEqual(TEXT("level from the save (the old level 5 is not kept)"), Player.Progression->GetLevel(), 2);
	TestEqual(TEXT("cooler row replaced"), Player.Cooler->GetCoolerId(), FName(TEXT("Basic")));
	TestEqual(TEXT("cooler fish replaced (not merged)"), Player.Cooler->GetNumFish(), 2);
	TestEqual(TEXT("loading is never a level-up"), Listener->LevelUps.Num(), 0);

	const QAP::FState Once = QAP::FState::Of(Player);
	TestTrue(TEXT("apply again"), Player.Progression->ApplySaveData(Save));
	TestTrue(TEXT("applying the same save twice gives the same state"), QAP::FState::Of(Player).Equals(Once));
	const FLureProgressSaveData A = Player.Progression->GetSaveData();
	const FLureProgressSaveData B = Player.Progression->GetSaveData();
	TestTrue(TEXT("GetSaveData is stable (no side effects)"), FLureProgressSaveData::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None));
	TestTrue(TEXT("GetSaveData did not change the player"), QAP::FState::Of(Player).Equals(Once));
	TestEqual(TEXT("still no level-up"), Listener->LevelUps.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSaveDefaultStruct, "Project.Progression.QA.Save.DefaultSaveLoadsAsNewPlayer", QAP::Flags)
bool FQAProgSaveDefaultStruct::RunTest(const FString& Parameters)
{
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Fresh = Env.SpawnPlayer(*this);
	const QAP::FPlayer Loaded = Env.SpawnPlayer(*this);
	if (!Fresh.IsValid() || !Loaded.IsValid())
	{
		return false;
	}
	Loaded.Progression->AddXp(200);
	Loaded.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 1, 1));
	TestTrue(TEXT("apply an empty (default) save"), Loaded.Progression->ApplySaveData(FLureProgressSaveData()));
	TestEqual(TEXT("level 1"), Loaded.Progression->GetLevel(), 1);
	TestEqual(TEXT("0 XP"), Loaded.Progression->GetTotalXp(), 0);
	TestEqual(TEXT("0 fish"), Loaded.Cooler->GetNumFish(), 0);
	TestEqual(TEXT("the default cooler row (settings DefaultCoolerId)"), Loaded.Cooler->GetCoolerId(), GetDefault<ULureProgressionSettings>()->DefaultCoolerId);
	TestEqual(TEXT("the default cooler's capacity"), Loaded.Cooler->GetCapacity(), Fresh.Cooler->GetCapacity());
	TestTrue(TEXT("money is 0 or the starting money, never negative"), Loaded.Progression->GetMoney() >= 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSaveHostileValues, "Project.Progression.QA.Save.HostileValuesAreClamped", QAP::Flags)
bool FQAProgSaveHostileValues::RunTest(const FString& Parameters)
{
	// A corrupted or edited save file must not produce negative money, level 0, or a level past the curve.
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
	FLureProgressSaveData Save;
	Save.Money = MIN_int32;
	Save.TotalXp = MIN_int32;
	Save.Level = MIN_int32;
	Player.Progression->ApplySaveData(Save);
	TestEqual(TEXT("MIN money loads as 0"), Player.Progression->GetMoney(), 0);
	TestEqual(TEXT("MIN XP loads as 0"), Player.Progression->GetTotalXp(), 0);
	TestEqual(TEXT("MIN level loads as 1"), Player.Progression->GetLevel(), 1);

	Save.Level = 0;
	Save.TotalXp = 0;
	Player.Progression->ApplySaveData(Save);
	TestEqual(TEXT("level 0 loads as 1"), Player.Progression->GetLevel(), 1);

	Save.Money = MAX_int32;
	Save.TotalXp = MAX_int32;
	Save.Level = MAX_int32;
	Player.Progression->ApplySaveData(Save);
	TestEqual(TEXT("MAX money kept"), Player.Progression->GetMoney(), MAX_int32);
	TestEqual(TEXT("MAX XP kept"), Player.Progression->GetTotalXp(), MAX_int32);
	TestEqual(TEXT("MAX level capped at the curve (5)"), Player.Progression->GetLevel(), 5);
	TestTrue(TEXT("adding money at MAX stays MAX"), Player.Progression->AddMoney(1) && Player.Progression->GetMoney() == MAX_int32);
	Player.Progression->AddXp(1);
	TestEqual(TEXT("adding XP at MAX stays MAX"), Player.Progression->GetTotalXp(), MAX_int32);

	Save.Version = 999; // a newer save: load what is known
	Save.Money = 3;
	QAP::ExpectWarnings(*this, TEXT("is newer than"));
	Player.Progression->ApplySaveData(Save);
	TestEqual(TEXT("a newer save version still loads the known fields"), Player.Progression->GetMoney(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSaveUnknownCoolerRow, "Project.Progression.QA.Save.UnknownCoolerRowKeepsFish", QAP::Flags)
bool FQAProgSaveUnknownCoolerRow::RunTest(const FString& Parameters)
{
	// A save from a build with a cooler row that was later renamed or removed: the fish must survive.
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
	FLureProgressSaveData Save;
	Save.CoolerId = TEXT("QA_RemovedCooler");
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Save.CoolerFish.Add(QAP::Fish(TEXT("QA_Fish"), 10 + Index, 1, Index));
	}
	Save.CoolerFish.Insert(FFishInstance(), 2); // one broken record in the middle
	QAP::ExpectWarnings(*this, TEXT("QA_RemovedCooler"));
	Player.Progression->ApplySaveData(Save);
	TestEqual(TEXT("all 6 valid fish kept, the broken record dropped"), Player.Cooler->GetNumFish(), 6);
	FFishInstance Third;
	TestTrue(TEXT("order kept around the dropped record (slot 2 = the 3rd valid fish, Value 12)"), Player.Cooler->GetFishAt(2, Third) && Third.Value == 12);
	TestTrue(TEXT("the cooler still has a capacity >= 1"), Player.Cooler->GetCapacity() >= 1);
	AddInfo(FString::Printf(TEXT("unknown saved cooler row -> CoolerId '%s', capacity %d"), *Player.Cooler->GetCoolerId().ToString(), Player.Cooler->GetCapacity()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAProgSaveSeamlessTravel, "Project.Progression.QA.Save.SeamlessTravelCopiesProgress", QAP::Flags)
bool FQAProgSaveSeamlessTravel::RunTest(const FString& Parameters)
{
	// Seamless travel / reconnect: the engine copies the old player state into the new one (CopyProperties / OverrideWith).
	QAP::FEnv Env;
	if (!Env.InitQA(*this))
	{
		return false;
	}
	const QAP::FPlayer Old = Env.SpawnPlayer(*this);
	const QAP::FPlayer New = Env.SpawnPlayer(*this);
	const QAP::FPlayer Rejoin = Env.SpawnPlayer(*this);
	if (!Old.IsValid() || !New.IsValid() || !Rejoin.IsValid())
	{
		return false;
	}
	Old.Cooler->SetCoolerId(TEXT("Mega"));
	Old.Cooler->AddFish(QAP::Fish(TEXT("QA_A"), 11, 1, 1));
	Old.Cooler->AddFish(QAP::Fish(TEXT("QA_B"), 22, 1, 2));
	Old.Progression->AddMoney(333);
	Old.Progression->AddXp(140);
	const QAP::FState Expected = QAP::FState::Of(Old);

	Old.State->DispatchCopyProperties(New.State);
	TestTrue(FString::Printf(TEXT("CopyProperties carries everything (%s vs %s)"), *QAP::FState::Of(New).Describe(), *Expected.Describe()),
		QAP::FState::Of(New).Equals(Expected));
	TestTrue(TEXT("the source is unchanged"), QAP::FState::Of(Old).Equals(Expected));

	Rejoin.State->DispatchOverrideWith(Old.State);
	TestTrue(FString::Printf(TEXT("OverrideWith (reconnect) carries everything (%s)"), *QAP::FState::Of(Rejoin).Describe()), QAP::FState::Of(Rejoin).Equals(Expected));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

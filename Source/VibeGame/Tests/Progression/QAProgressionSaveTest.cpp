// Lure T-010 independent QA tests (qa-engineer): save data (FLureProgressSaveData), apply on load, and seamless travel.
// Project.Progression.QA.Save.*
//
// T-030 (unreal-engineer): FLureProgressSaveData v2 holds money, XP and level only (coolers are world objects saved by
// ULureCatchLibrary::GetCoolerSaveData, Project.Catch.Save.*). Retired here (docs/TEST_PLAN.md "T-030"):
//   Project.Progression.QA.Save.RealCatchRoundTripsExactly (the catch records now travel in FLureCoolerSaveData),
//   Project.Progression.QA.Save.UnknownCoolerRowKeepsFish (now Project.Catch.Save.UnknownCoolerRowKeepsFish).
// The other tests keep their progression part.

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Game/LurePlayerState.h"
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
	Player.Progression->AddMoney(500);
	Player.Progression->AddXp(460); // level 5

	FLureProgressSaveData Save;
	Save.Money = 42;
	Save.TotalXp = 60;
	Save.Level = 2;

	UQAProgressionListener* Listener = NewObject<UQAProgressionListener>();
	TStrongObjectPtr<UQAProgressionListener> Keep(Listener);
	Listener->Listen(Player.Progression);
	TestTrue(TEXT("apply"), Player.Progression->ApplySaveData(Save));
	TestEqual(TEXT("money replaced"), Player.Progression->GetMoney(), 42);
	TestEqual(TEXT("XP replaced"), Player.Progression->GetTotalXp(), 60);
	TestEqual(TEXT("level from the save (the old level 5 is not kept)"), Player.Progression->GetLevel(), 2);
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
	TestTrue(TEXT("apply an empty (default) save"), Loaded.Progression->ApplySaveData(FLureProgressSaveData()));
	TestEqual(TEXT("level 1"), Loaded.Progression->GetLevel(), 1);
	TestEqual(TEXT("0 XP"), Loaded.Progression->GetTotalXp(), 0);
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

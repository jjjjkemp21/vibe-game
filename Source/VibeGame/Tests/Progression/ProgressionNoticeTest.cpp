// Lure tests (unreal-engineer): the placeholder "Level up! Level N" and "Sold N fish for X coins" notices from the
// fishing-loop playtest (2026-09-23). They show on the owning player's machine only, once per event, for
// ULureProgressionSettings::NoticeSeconds. Project.Progression.Notice.* (T-030: sales come from the sell counter through
// ULureProgressionComponent::RecordSale; the counter itself is tested in Project.Catch.Counter.*).

#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/LureHUD.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Tests/Progression/ProgressionTestListener.h"

namespace ProgressionNoticeTest
{
	struct FFixture
	{
		TStrongObjectPtr<UDataTable> Levels;
		LPT::FWorld World;
		/** The local players given to the controllers (a controller counts as local when it has one) */
		TArray<TStrongObjectPtr<ULocalPlayer>> LocalPlayers;
		TArray<TWeakObjectPtr<APlayerController>> Controllers;

		~FFixture()
		{
			for (const TWeakObjectPtr<APlayerController>& Controller : Controllers)
			{
				if (Controller.IsValid())
				{
					Controller->Player = nullptr;
				}
			}
		}

		bool Init(FAutomationTestBase& Test)
		{
			Levels = LPT::MakeTable(Test, FPlayerLevelRow::StaticStruct(), LPT::FixtureLevelCsv());
			return Levels.IsValid() && World.Create(Test);
		}

		/** A player; with bLocalController its player state is owned by a (local, standalone) player controller */
		LPT::FPlayer Spawn(FAutomationTestBase& Test, bool bLocalController, APlayerController** OutController = nullptr)
		{
			LPT::FPlayer Player = LPT::SpawnPlayer(Test, World, Levels.Get());
			if (bLocalController && Player.IsValid())
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				APlayerController* Controller = World.World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity, Params);
				if (Test.TestNotNull(TEXT("player controller spawned"), Controller))
				{
					// Without a net driver a controller is local when it has a ULocalPlayer (APlayerController::IsLocalController).
					ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
					LocalPlayers.Emplace(LocalPlayer);
					Controllers.Add(Controller);
					Controller->Player = LocalPlayer;
					Controller->SetPlayerState(Player.State);
					Player.State->SetOwner(Controller);
					if (OutController)
					{
						*OutController = Controller;
					}
				}
			}
			return Player;
		}
	};

	void SetRoles(ENetRole Role, std::initializer_list<AActor*> Actors)
	{
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				Actor->SetRole(Role);
			}
		}
	}

	/** Advances game time by Seconds in 0.1 s ticks (one big tick is clamped by the world settings' max frame time) */
	void Advance(LPT::FWorld& World, float Seconds)
	{
		const double End = World.World->GetTimeSeconds() + Seconds;
		for (int32 Guard = 0; World.World->GetTimeSeconds() < End && Guard < 10000; ++Guard)
		{
			World.Wrapper.TickTestWorld(0.1f);
		}
	}

	FString Join(const TArray<FString>& Lines)
	{
		return FString::Join(Lines, TEXT(" | "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureNoticeLevelUpOwnerOnly, "Project.Progression.Notice.LevelUpOnceForTheOwner", LPT::Flags)
bool FLureNoticeLevelUpOwnerOnly::RunTest(const FString& Parameters)
{
	ProgressionNoticeTest::FFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	const LPT::FPlayer Mine = Fx.Spawn(*this, /*bLocalController*/ true);
	const LPT::FPlayer Other = Fx.Spawn(*this, /*bLocalController*/ false); // another player's state (no local controller)
	if (!Mine.IsValid() || !Other.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("my progression is the local player's"), Mine.Progression->IsLocalPlayerProgression());
	TestFalse(TEXT("the other player's is not"), Other.Progression->IsLocalPlayerProgression());

	Mine.Progression->AddXp(50);
	TestEqual(TEXT("no notice without a level-up"), Mine.Progression->GetNoticeLines().Num(), 0);
	Mine.Progression->AddXp(50); // 100 = level 2
	TestEqual(TEXT("level 2: one notice"), ProgressionNoticeTest::Join(Mine.Progression->GetNoticeLines()), FString(TEXT("Level up! Level 2")));
	Mine.Progression->AddXp(400); // 500 = level 4 (cap): a multi-level jump is ONE level-up
	TestEqual(TEXT("a jump to level 4 adds one notice"), ProgressionNoticeTest::Join(Mine.Progression->GetNoticeLines()),
		FString(TEXT("Level up! Level 2 | Level up! Level 4")));

	Other.Progression->AddXp(500);
	TestEqual(TEXT("the other player levelled up"), Other.Progression->GetLevel(), 4);
	TestEqual(TEXT("... but no notice on a machine where they are not the local player"), Other.Progression->GetNoticeLines().Num(), 0);
	TestEqual(TEXT("... and none added to mine"), Mine.Progression->GetNoticeLines().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureNoticeLevelUpClient, "Project.Progression.Notice.LevelUpOnTheOwningClient", LPT::Flags)
bool FLureNoticeLevelUpClient::RunTest(const FString& Parameters)
{
	ProgressionNoticeTest::FFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	APlayerController* Controller = nullptr;
	const LPT::FPlayer Mine = Fx.Spawn(*this, true, &Controller);
	const LPT::FPlayer Other = Fx.Spawn(*this, false);
	if (!Mine.IsValid() || !Other.IsValid() || !Controller)
	{
		return false;
	}
	// The server raised both levels; now look at them from a client: my state is owned by my autonomous controller,
	// the other player's state is a simulated proxy without an owner.
	Mine.Progression->AddXp(100);
	Other.Progression->AddXp(100);
	Mine.Progression->ClearNotices();
	ULureProgressionTestListener* Listener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> Keep(Listener);
	Listener->Listen(Mine.Progression);

	ProgressionNoticeTest::SetRoles(ROLE_SimulatedProxy, { Mine.State, Other.State });
	ProgressionNoticeTest::SetRoles(ROLE_AutonomousProxy, { Controller });
	Mine.Progression->OnRep_Level(1);
	Other.Progression->OnRep_Level(1);
	TestEqual(TEXT("client: one OnLevelUp for my rise"), Listener->LevelUps.Num(), 1);
	TestEqual(TEXT("client: one notice for my level-up"), ProgressionNoticeTest::Join(Mine.Progression->GetNoticeLines()), FString(TEXT("Level up! Level 2")));
	TestEqual(TEXT("client: no notice on the other player's copy"), Other.Progression->GetNoticeLines().Num(), 0);
	Mine.Progression->OnRep_Level(2); // a repeat of the same value is not a level-up
	TestEqual(TEXT("client: still one notice"), Mine.Progression->GetNoticeLines().Num(), 1);
	ProgressionNoticeTest::SetRoles(ROLE_Authority, { Mine.State, Other.State, Controller });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureNoticeSale, "Project.Progression.Notice.SaleOnceForTheSeller", LPT::Flags)
bool FLureNoticeSale::RunTest(const FString& Parameters)
{
	ProgressionNoticeTest::FFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	APlayerController* Controller = nullptr;
	const LPT::FPlayer Mine = Fx.Spawn(*this, true, &Controller);
	const LPT::FPlayer Other = Fx.Spawn(*this, false);
	if (!Mine.IsValid() || !Other.IsValid() || !Controller)
	{
		return false;
	}
	ULureProgressionTestListener* MyListener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> KeepMine(MyListener);
	MyListener->Listen(Mine.Progression);
	ULureProgressionTestListener* OtherListener = NewObject<ULureProgressionTestListener>();
	TStrongObjectPtr<ULureProgressionTestListener> KeepOther(OtherListener);
	OtherListener->Listen(Other.Progression);

	TestFalse(TEXT("nothing sold: no sale"), Mine.Progression->RecordSale(0, 0));
	TestEqual(TEXT("nothing sold: no sale event"), MyListener->Sales.Num(), 0);
	TestEqual(TEXT("nothing sold: no notice"), Mine.Progression->GetNoticeLines().Num(), 0);

	TestTrue(TEXT("a sale of 2 fish for 48 coins"), Mine.Progression->RecordSale(2, 48));
	TestEqual(TEXT("the seller is paid"), Mine.Progression->GetMoney(), 48);
	TestEqual(TEXT("one sale event for the seller"), MyListener->Sales.Num(), 1);
	if (MyListener->Sales.Num() == 1)
	{
		TestEqual(TEXT("... with the count"), MyListener->Sales[0].Key, 2);
		TestEqual(TEXT("... and the coins"), MyListener->Sales[0].Value, 48);
	}
	TestEqual(TEXT("one sale notice"), ProgressionNoticeTest::Join(Mine.Progression->GetNoticeLines()), FString(TEXT("Sold 2 fish for 48 coins")));
	TestEqual(TEXT("no sale event for the other player"), OtherListener->Sales.Num(), 0);
	TestEqual(TEXT("no notice for the other player"), Other.Progression->GetNoticeLines().Num(), 0);

	TestTrue(TEXT("a sale of one fish"), Mine.Progression->RecordSale(1, 9));
	const TArray<FString> AfterOne = Mine.Progression->GetNoticeLines();
	TestEqual(TEXT("one fish notifies too"), AfterOne.Num() > 0 ? AfterOne.Last() : FString(), FString(TEXT("Sold 1 fish for 9 coins")));

	// The owning client receiving the RPC (a client-owned player state and controller).
	Mine.Progression->ClearNotices();
	ProgressionNoticeTest::SetRoles(ROLE_SimulatedProxy, { Mine.State });
	ProgressionNoticeTest::SetRoles(ROLE_AutonomousProxy, { Controller });
	Mine.Progression->ClientFishSold(3, 75);
	TestEqual(TEXT("client: the notice shows on the seller's HUD"), ProgressionNoticeTest::Join(Mine.Progression->GetNoticeLines()), FString(TEXT("Sold 3 fish for 75 coins")));
	ProgressionNoticeTest::SetRoles(ROLE_Authority, { Mine.State, Controller });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureNoticeHud, "Project.Progression.Notice.HudShowsOwnNoticesThenExpires", LPT::Flags)
bool FLureNoticeHud::RunTest(const FString& Parameters)
{
	ProgressionNoticeTest::FFixture Fx;
	if (!Fx.Init(*this))
	{
		return false;
	}
	APlayerController* MyController = nullptr;
	APlayerController* OtherController = nullptr;
	const LPT::FPlayer Mine = Fx.Spawn(*this, true, &MyController);
	const LPT::FPlayer Other = Fx.Spawn(*this, true, &OtherController);
	if (!Mine.IsValid() || !Other.IsValid() || !MyController || !OtherController)
	{
		return false;
	}
	TestEqual(TEXT("no controller: no notices"), ALureHUD::GetNoticeLines(nullptr).Num(), 0);
	Mine.Progression->AddXp(100);
	Mine.Progression->RecordSale(1, 12);
	TestEqual(TEXT("my HUD: the level-up, then the sale"), ProgressionNoticeTest::Join(ALureHUD::GetNoticeLines(MyController)),
		FString(TEXT("Level up! Level 2 | Sold 1 fish for 12 coins")));
	TestEqual(TEXT("the other player's HUD shows none of mine"), ALureHUD::GetNoticeLines(OtherController).Num(), 0);

	for (int32 Index = 0; Index < ULureProgressionComponent::MaxNotices + 2; ++Index)
	{
		Mine.Progression->AddNotice(FString::Printf(TEXT("n%d"), Index));
	}
	TestEqual(TEXT("at most MaxNotices at once"), Mine.Progression->GetNoticeLines().Num(), ULureProgressionComponent::MaxNotices);
	const TArray<FString> Capped = Mine.Progression->GetNoticeLines();
	TestEqual(TEXT("... the newest kept"), Capped.Num() > 0 ? Capped.Last() : FString(), FString::Printf(TEXT("n%d"), ULureProgressionComponent::MaxNotices + 1));

	const float Seconds = GetDefault<ULureProgressionSettings>()->NoticeSeconds;
	TestTrue(TEXT("NoticeSeconds is a few seconds"), Seconds >= 0.5f && Seconds <= 30.0f);
	ProgressionNoticeTest::Advance(Fx.World, Seconds * 0.5f);
	TestEqual(TEXT("halfway: still showing"), Mine.Progression->GetNoticeLines().Num(), ULureProgressionComponent::MaxNotices);
	ProgressionNoticeTest::Advance(Fx.World, Seconds * 0.5f + 0.25f);
	TestEqual(TEXT("after NoticeSeconds: gone"), ALureHUD::GetNoticeLines(MyController).Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

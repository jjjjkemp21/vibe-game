// Lure T-030n tests (unreal-engineer): the E / F keys act on the prompt the player SAW, i.e. each key's prompt at the end of
// the last frame (ULureInteractionComponent::PressKeyAsShown), never on what this frame's network update changed it to
// before the key ran. Repro (playtest 2026-09-24 A2 item 8b): 2 fish on the counter; on the same frame the host takes one
// back (F) and the client sells (E). PIE ticks the listen server's world first, so the take-back reached the client in that
// frame's network receive, BEFORE its E handler ran; the old handler resolved the counter again and sent the new contents'
// token, and the server sold the remaining fish with no notice (prompt 81, paid 78). These tests press through the key
// handlers (HandleInteractPressed / HandleAltInteractPressed: what the Enhanced Input bindings call) and, for the race,
// from inside the seller's world tick right after its network receive (FWorldDelegates::OnWorldPreActorTick): where the
// player's input runs in a real frame. Also: a player's Sell without the state token is refused (0 only skips the check
// for server code calling TryInteract). Rules: docs/specs/catch-handling-rules.md "The sell counter", "Focus and input".
// Project.Catch.Net.SellKeyActsOnShownPrompt, Project.Catch.Interact.KeyActsOnShownPrompt

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionTypes.h"
#include "Tests/NetTestHelpers.h"

namespace LureSaleRaceKeyTest
{
	constexpr float NetDt = 1.0f / 60.0f;
	constexpr ELureInteractKey KeyE = ELureInteractKey::Primary;
	constexpr ELureInteractKey KeyF = ELureInteractKey::Secondary;
	const TCHAR* const NoTokenWarning = TEXT("without the prompt's state token");

	/** The shipped data every machine uses (text sources, never the binary assets); as in CatchNetTest.cpp */
	struct FData
	{
		TStrongObjectPtr<UDataTable> Movement;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Freshness;
		TStrongObjectPtr<UDataTable> Catch;
		TStrongObjectPtr<UDataTable> Display;
		TStrongObjectPtr<UDataTable> Markets;
		TStrongObjectPtr<UDataTable> Levels;
		FishQA::FTables Fish;
		FLureCatchRow Tuning;

		bool Load(FAutomationTestBase& Test)
		{
			Movement = LCT::Shipped(Test, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"));
			Coolers = LCT::Shipped(Test, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
			Freshness = LCT::Shipped(Test, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness.csv"));
			Catch = LCT::Shipped(Test, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
			Display = LCT::Shipped(Test, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
			Markets = LCT::MakeTableChecked(Test, FFishMarketRow::StaticStruct(), LCT::FixtureMarketCsv(), false, TEXT("markets"));
			Levels = LCT::MakeTableChecked(Test, FPlayerLevelRow::StaticStruct(), TEXT("Name,Level,XpToNext,DevComment\nL01,1,100,\nL02,2,150,\nL03,3,0,\n"), false, TEXT("levels"));
			if (!Movement.IsValid() || !Coolers.IsValid() || !Freshness.IsValid() || !Catch.IsValid() || !Display.IsValid() || !FishQA::LoadReal(Test, Fish))
			{
				return false;
			}
			const FLureCatchRow* Row = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("SaleRaceKeyPressTest"), false);
			if (!Test.TestNotNull(TEXT("DT_Catch Default"), Row))
			{
				return false;
			}
			Tuning = *Row;
			return true;
		}

		void Inject(UWorld* World) const
		{
			if (ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
			{
				Subsystem->SetTuning(Tuning);
				Subsystem->SetFreshnessTable(Freshness.Get());
				Subsystem->SetCoolerTable(Coolers.Get());
				Subsystem->SetCoolerDisplayTable(Display.Get());
				Subsystem->SetFishTables(Fish.Get());
			}
		}
	};

	/** The level geometry every machine has: the test dock (top z = LCT::DockTop) */
	void AddDock(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Dock"));
		Box->SetBoxExtent(FVector(LCT::DockHalf, LCT::DockHalf, LCT::DockTop * 0.5f), false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetRelativeLocation_Direct(FVector(0.0f, 0.0f, LCT::DockTop * 0.5f));
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
	}

	/**
	 *  A real in-process server with two connected players on the dock (UE::Net::FTestWorlds: dedicated server, both players
	 *  remote). One harness step = the server's tick, then each client's, then GFrameCounter++: the same order as a PIE frame
	 *  (the listen server's world ticks first), so a change the server makes reaches a client in that client's next tick.
	 */
	struct FNet
	{
		UE::Net::FTestWorlds& Worlds;
		FData Data;
		UWorld* Server = nullptr;
		ALurePlayerCharacter* Players[2] = { nullptr, nullptr };

		explicit FNet(UE::Net::FTestWorlds& InWorlds) : Worlds(InWorlds) {}

		bool Create(FAutomationTestBase& Test)
		{
			Test.AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			// Harness artifact: the docks are spawned per world (not replicated), so the players' movement base can't be sent.
			Test.AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			if (!Data.Load(Test))
			{
				return false;
			}
			Server = Worlds.Server.GetWorld();
			if (!Test.TestTrue(TEXT("harness: the server world is up"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()))
			{
				return false;
			}
			for (int32 Client = 0; Client < 2; ++Client)
			{
				if (!Test.TestTrue(FString::Printf(TEXT("harness: client %d connects"), Client), Worlds.CreateAndConnectClient()))
				{
					return false;
				}
			}
			AddDock(Server);
			Data.Inject(Server);
			for (UE::Net::FTestWorldInstance& Client : Worlds.Clients)
			{
				AddDock(Client.GetWorld());
				Data.Inject(Client.GetWorld());
			}
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Data.Movement.Get(), Rows, Problems);
			const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.0f;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				APlayerController* Controller = Worlds.GetServerPlayerControllerOfClient(Index);
				const FTransform Transform(FRotator::ZeroRotator, FVector(0.0f, Index == 0 ? -100.0f : 100.0f, LCT::DockTop + HalfHeight + 2.15f));
				ALurePlayerCharacter* Pawn = Server->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Test.TestNotNull(TEXT("harness: controller"), Controller) || !Test.TestNotNull(TEXT("harness: pawn"), Pawn))
				{
					return false;
				}
				Pawn->GetLureMovement()->ApplyMovementTable(Data.Movement.Get());
				Pawn->FinishSpawning(Transform);
				Controller->Possess(Pawn);
				if (ULureProgressionComponent* Progression = LCT::ProgressionOf(Pawn))
				{
					Progression->SetLevelTable(Data.Levels.Get());
				}
				Players[Index] = Pawn;
			}
			const bool bReady = Until([this]()
			{
				for (int32 Client = 0; Client < 2; ++Client)
				{
					const ALurePlayerCharacter* Own = On(Client, Players[Client]);
					const ALurePlayerCharacter* Other = On(Client, Players[1 - Client]);
					if (!Own || !Other || !Own->IsLocallyControlled() || !Own->GetHands() || !Other->GetHands() || !ClientPC(Client) || !ClientPC(Client)->PlayerState)
					{
						return false;
					}
				}
				return true;
			}, 600);
			if (!Test.TestTrue(TEXT("harness: each client has its own (controlled) player and the other one"), bReady))
			{
				return false;
			}
			Worlds.TickAll(30); // settle on the dock
			return true;
		}

		template <typename T>
		T* On(int32 Client, T* ServerObject) const
		{
			// The harness ensures when asked about an object the server hasn't replicated yet (no net id): ask that first.
			if (!ServerObject || !Worlds.IsServerObjectReplicated(ServerObject) || !Worlds.DoesReplicatedObjectExistOnClient(ServerObject, static_cast<uint32>(Client)))
			{
				return nullptr;
			}
			return Cast<T>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(ServerObject), static_cast<uint32>(Client)));
		}

		bool Until(TFunctionRef<bool()> Predicate, int32 MaxTicks = 180)
		{
			return Worlds.TickAllUntil([&Predicate]() { return Predicate(); }, NetDt, MaxTicks);
		}

		APlayerController* ClientPC(int32 Client) const
		{
			UWorld* World = Worlds.Clients.IsValidIndex(Client) ? Worlds.Clients[Client].GetWorld() : nullptr;
			return World ? World->GetFirstPlayerController() : nullptr;
		}

		/** Client's own player on its machine */
		ALurePlayerCharacter* Mine(int32 Client) const { return On(Client, Players[Client]); }

		ULureInteractionComponent* Keys(int32 Client) const { return LCT::InteractionOf(Mine(Client)); }

		/** The client's player looks at Target (its own view, which the server never checks) */
		void LookAt(int32 Client, const FVector& Target) const
		{
			ALurePlayerCharacter* Pawn = Mine(Client);
			if (APlayerController* Controller = ClientPC(Client); Controller && Pawn)
			{
				Controller->SetControlRotation((Target - Pawn->GetPawnViewLocation()).Rotation());
			}
		}

		ALureSellCounter* SpawnCounter(const FVector& Top, float Yaw)
		{
			const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Top);
			ALureSellCounter* Counter = Server->SpawnActorDeferred<ALureSellCounter>(ALureSellCounter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Counter)
			{
				Counter->MarketId = TEXT("Default");
				Counter->SetMarketTable(Data.Markets.Get());
				Counter->FinishSpawning(Transform);
			}
			return Counter;
		}

		/** Server shortcut: Player lands Fish, takes it in hand and puts it on the counter (the keys are what is tested) */
		ALureFishItem* PutOn(ALureSellCounter* Counter, int32 Player, const FFishInstance& Fish) const
		{
			ALureFishItem* Item = Cast<ALureFishItem>(ULureCatchLibrary::HandleFishLanded(Players[Player], Fish).FishItem);
			return (Item && LCT::HandsOf(Players[Player])->AuthorityTakeInHand(Item) && Counter->AuthorityPlaceFish(Players[Player], Item)) ? Item : nullptr;
		}

		bool BothSee(ALureSellCounter* Counter, int32 Num)
		{
			return Until([&]()
			{
				const ALureSellCounter* Zero = On(0, Counter);
				const ALureSellCounter* One = On(1, Counter);
				return Zero && One && Zero->GetFishOnCounter().Num() == Num && One->GetFishOnCounter().Num() == Num;
			});
		}
	};

	/** Presses a key inside a world tick after its network receive (shared: LCT::FPressInTick) */
	using FPressInTick = LCT::FPressInTick;

	/** The prompt Keys' key showed at the end of the last frame ("" if there is no fresh record) */
	FString ShownPrompt(const ULureInteractionComponent* Keys, ELureInteractKey Key)
	{
		FLureResolvedInteraction Shown;
		int32 Token = 0;
		return (Keys && Keys->GetShownInteraction(Key, Shown, Token)) ? Shown.Prompt.ToString() : FString();
	}

	/** Puts Fish on Counter the way a player does in a single world (landed, in the hand, placed); the item or null */
	ALureFishItem* PutOnLocal(LCT::FWorld& W, ALureSellCounter* Counter, ALurePlayerCharacter* Player, const FFishInstance& Fish)
	{
		ALureFishItem* Item = W.LandInHand(Player, Fish);
		return (Item && Counter->AuthorityPlaceFish(Player, Item)) ? Item : nullptr;
	}

	/**
	 *  The playtest race with a real server and two clients: the taker's F and the seller's E through the key handlers; the
	 *  take-back reaches the seller's machine in the same frame as its E, before the handler runs.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSellKeyActsOnShownPrompt, "Project.Catch.Net.SellKeyActsOnShownPrompt", LCT::Flags)
	bool FSellKeyActsOnShownPrompt::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		constexpr int32 Taker = 0;  // the playtest's host
		constexpr int32 Seller = 1; // the playtest's client
		ALureSellCounter* Counter = Net.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
		if (!TestNotNull(TEXT("counter"), Counter) || !TestTrue(TEXT("the counter reaches the clients"), Net.Until([&]() { return Net.On(0, Counter) && Net.On(1, Counter); })))
		{
			return false;
		}
		ULureProgressionComponent* SellerProgress = LCT::ProgressionOf(Net.Players[Seller]);
		ULureProgressionComponent* TakerProgress = LCT::ProgressionOf(Net.Players[Taker]);
		// The seller's Bonefish first, the taker's snapper last (F takes the last one put on).
		ALureFishItem* A = Net.PutOn(Counter, Seller, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 901));
		ALureFishItem* B = Net.PutOn(Counter, Taker, LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.0f, 902));
		if (!TestNotNull(TEXT("both fish on the counter"), A) || !TestNotNull(TEXT("... the snapper"), B) || !TestTrue(TEXT("both clients see 2 fish"), Net.BothSee(Counter, 2))
			|| !TestNotNull(TEXT("progression"), SellerProgress) || !TakerProgress)
		{
			return false;
		}
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Net.LookAt(Client, Net.On(Client, Counter)->GetActorLocation());
		}
		Worlds.TickAll(2);
		ULureInteractionComponent* SellerKeys = Net.Keys(Seller);
		ULureInteractionComponent* TakerKeys = Net.Keys(Taker);
		ALureSellCounter* SellerCounter = Net.On(Seller, Counter);
		if (!TestNotNull(TEXT("the seller's keys"), SellerKeys) || !TestNotNull(TEXT("the taker's keys"), TakerKeys) || !TestNotNull(TEXT("the seller's counter"), SellerCounter))
		{
			return false;
		}
		TestEqual(TEXT("the seller's shown E prompt (recorded at the end of the frame, what the HUD drew)"), ShownPrompt(SellerKeys, KeyE), FString(TEXT("Sell 2 fish (65 coins)")));
		TestTrue(FString::Printf(TEXT("... the same as its HUD prompt line (%s)"), *SellerKeys->GetPromptText()), SellerKeys->GetPromptText().Contains(TEXT("[E] Sell 2 fish (65 coins)")));
		TestEqual(TEXT("the taker's shown F prompt"), ShownPrompt(TakerKeys, KeyF), FString(TEXT("Take a fish back")));
		// Only the machine that controls a pawn records its prompts (the server's copy and the other client's copy don't).
		TestEqual(TEXT("no record on the server's copy of the seller"), ShownPrompt(LCT::InteractionOf(Net.Players[Seller]), KeyE), FString());
		TestEqual(TEXT("no record on the seller's copy of the taker"), ShownPrompt(LCT::InteractionOf(Net.On(Seller, Net.Players[Taker])), KeyF), FString());

		// 1. The repro frame: the taker's F runs on the server and replicates; on the seller's machine that news arrives in the
		//    same frame as its E, before the E handler runs. The E must act on the 2-fish prompt the seller saw: refused.
		const int32 Money0 = SellerProgress->GetMoney();
		const int32 TakerMoney0 = TakerProgress->GetMoney();
		FString ShownAtKey;
		FString NowAtKey;
		FPressInTick SellerE;
		SellerE.Arm(Worlds.Clients[Seller].GetWorld(),
			[SellerCounter]() { return SellerCounter->GetFishOnCounter().Num() == 1; },
			[&]()
			{
				ShownAtKey = ShownPrompt(SellerKeys, KeyE);
				NowAtKey = SellerKeys->ResolveInteraction(KeyE).Prompt.ToString(); // what the old handler acted on
				SellerKeys->HandleInteractPressed(); // what the E binding calls
			});
		TakerKeys->HandleAltInteractPressed(); // what the F binding calls
		const bool bPressed = Net.Until([&]() { return SellerE.bDone; });
		SellerE.Remove();
		if (!TestTrue(TEXT("the take-back reached the seller's machine and its E ran in that frame"), bPressed))
		{
			return false;
		}
		TestEqual(TEXT("at the key, the prompt the seller saw"), ShownAtKey, FString(TEXT("Sell 2 fish (65 coins)")));
		TestEqual(TEXT("at the key, the seller's machine already had the new contents (the race is reproduced)"), NowAtKey, FString(TEXT("Sell 1 fish (45 coins)")));
		TestTrue(TEXT("the taker holds the snapper (server)"), LCT::HandsOf(Net.Players[Taker])->GetHeldFish() == B);
		const bool bTold = Net.Until([&]() { return LCT::NoticesOf(Net.Mine(Seller)).Contains(TEXT("That just changed: nothing done. Now: Sell 1 fish (45 coins)")); });
		TestTrue(FString::Printf(TEXT("the seller is told, with the new offer (%s)"), *LCT::NoticesOf(Net.Mine(Seller))), bTold);
		Worlds.TickAll(10);
		TestEqual(TEXT("... nothing paid (the old handler paid 45 for 1 of the 2 fish shown)"), SellerProgress->GetMoney(), Money0);
		TestTrue(TEXT("... the Bonefish still lies on the counter (server)"), IsValid(A) && A->GetCounter() == Counter && Counter->GetFishOnCounter().Num() == 1);
		TestFalse(TEXT("... no sale notice"), LCT::NoticesOf(Net.Mine(Seller)).Contains(TEXT("Sold")));

		// 2. A player's Sell without the state token (an old or foreign caller): refused on the server, nothing sold.
		AddExpectedMessagePlain(NoTokenWarning, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestTrue(TEXT("a Sell request without a token is sent"), SellerKeys->RequestInteract(SellerCounter, KeyE, ELureInteractVerb::SellCounter));
		Worlds.TickAll(10);
		TestTrue(TEXT("... refused: nothing paid, the Bonefish still there"), SellerProgress->GetMoney() == Money0 && Counter->GetFishOnCounter().Num() == 1);

		// 3. E again once the prompt shows the new offer: sells exactly that.
		TestTrue(TEXT("the seller's shown prompt refreshes"), Net.Until([&]() { return ShownPrompt(SellerKeys, KeyE) == TEXT("Sell 1 fish (45 coins)"); }));
		SellerKeys->HandleInteractPressed();
		TestTrue(TEXT("... E sells what it shows"), Net.Until([&]() { return SellerProgress->GetMoney() == Money0 + 45; }));
		TestTrue(TEXT("... \"Sold 1 fish for 45 coins\""), Net.Until([&]() { return LCT::NoticesOf(Net.Mine(Seller)).Contains(TEXT("Sold 1 fish for 45 coins")); }));
		TestEqual(TEXT("... the counter is empty"), Counter->GetFishOnCounter().Num(), 0);

		// 4. Both keys on the same frame, both on a fresh 2-fish prompt: whichever the server runs first, all or nothing.
		ALureFishItem* C = Net.PutOn(Counter, Seller, LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 903));
		TestTrue(TEXT("server: the taker puts the snapper back (last)"), C && Counter->AuthorityPlaceFish(Net.Players[Taker], B));
		if (!TestTrue(TEXT("both clients see 2 fish again"), Net.BothSee(Counter, 2)))
		{
			return false;
		}
		Worlds.TickAll(2);
		TestEqual(TEXT("the seller's shown prompt"), ShownPrompt(SellerKeys, KeyE), FString(TEXT("Sell 2 fish (50 coins)")));
		const int32 Money1 = SellerProgress->GetMoney();
		TakerKeys->HandleAltInteractPressed();
		SellerKeys->HandleInteractPressed();
		Worlds.TickAll(60);
		const int32 Paid = SellerProgress->GetMoney() - Money1;
		const bool bTakenBack = LCT::HandsOf(Net.Players[Taker])->GetHeldFish() == B;
		AddInfo(FString::Printf(TEXT("same frame: %s ran first, paid %d"), bTakenBack ? TEXT("the take-back") : TEXT("the sale"), Paid));
		TestTrue(FString::Printf(TEXT("the seller is paid the whole prompt or nothing (%d)"), Paid), Paid == 50 || Paid == 0);
		if (Paid == 50)
		{
			TestTrue(TEXT("sold: the counter is empty and the take-back did nothing"), Counter->GetFishOnCounter().Num() == 0 && !bTakenBack);
		}
		else
		{
			TestTrue(TEXT("refused: the snapper in the taker's hand, the Bonefish on the counter"), bTakenBack && IsValid(C) && C->GetCounter() == Counter);
			TestTrue(TEXT("... and the seller is told"), Net.Until([&]() { return LCT::NoticesOf(Net.Mine(Seller)).Contains(TEXT("Now: Sell 1 fish (30 coins)")); }));
		}
		TestEqual(TEXT("the taker is never paid"), TakerProgress->GetMoney(), TakerMoney0);
		return true;
	}

	/**
	 *  One world (the listen host's view; both players local): what the record holds, the host's own E on a prompt that
	 *  changed this frame, a prompt that showed no verb, a stale record, and the host's request without a token.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeyActsOnShownPrompt, "Project.Catch.Interact.KeyActsOnShownPrompt", LCT::Flags)
	bool FKeyActsOnShownPrompt::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Seller = W.SpawnPlayer(*this, FVector(0.0f, -80.0f, LCT::DockTop));
		ALurePlayerCharacter* Taker = W.SpawnPlayer(*this, FVector(0.0f, 80.0f, LCT::DockTop));
		ALureSellCounter* Counter = W.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
		if (!TestNotNull(TEXT("players"), Seller) || !TestNotNull(TEXT("the taker"), Taker) || !TestNotNull(TEXT("counter"), Counter))
		{
			return false;
		}
		W.Tick(20);
		ULureInteractionComponent* SellerKeys = LCT::InteractionOf(Seller);
		ULureInteractionComponent* TakerKeys = LCT::InteractionOf(Taker);
		ULureProgressionComponent* SellerMoney = LCT::ProgressionOf(Seller);
		ULureProgressionComponent* TakerMoney = LCT::ProgressionOf(Taker);
		if (!TestNotNull(TEXT("keys"), SellerKeys) || !TestNotNull(TEXT("the taker's keys"), TakerKeys) || !TestNotNull(TEXT("progression"), SellerMoney) || !TakerMoney)
		{
			return false;
		}
		ALureFishItem* A = PutOnLocal(W, Counter, Seller, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 911));
		ALureFishItem* B = PutOnLocal(W, Counter, Taker, LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.0f, 912));
		if (!TestNotNull(TEXT("both fish on the counter"), A) || !TestNotNull(TEXT("... the snapper"), B))
		{
			return false;
		}
		LCT::LookAt(Seller, Counter->GetActorLocation());
		LCT::LookAt(Taker, Counter->GetActorLocation());
		W.Tick(1);

		// 1. The record = what the key resolves to at the end of the frame, with the state token of what it showed.
		FLureResolvedInteraction Shown;
		int32 ShownToken = 0;
		const FLureResolvedInteraction Now = SellerKeys->ResolveInteraction(KeyE);
		TestTrue(TEXT("the local player's E prompt is recorded"), SellerKeys->GetShownInteraction(KeyE, Shown, ShownToken));
		TestTrue(FString::Printf(TEXT("... the same target, verb and prompt as resolving now (%s)"), *Shown.Prompt.ToString()),
			Shown.Target == Counter && Shown.Verb == ELureInteractVerb::SellCounter && Shown.Verb == Now.Verb && Shown.Prompt.ToString() == TEXT("Sell 2 fish (65 coins)"));
		TestEqual(TEXT("... with the counter's contents token"), ShownToken, Counter->GetContentsToken());
		TestEqual(TEXT("F is recorded too: take a fish back"), ShownPrompt(SellerKeys, KeyF), FString(TEXT("Take a fish back")));

		// 2. The host's E in the frame another player's take-back already ran (a client's F reaches the listen server before the
		//    host's own input): E acts on the 2-fish prompt the host saw, so it is refused with the notice.
		TestTrue(TEXT("the taker's F: take back (server, this frame)"), TakerKeys->PressKey(KeyF) && LCT::HandsOf(Taker)->GetHeldFish() == B);
		TestEqual(TEXT("resolving now would show the new offer"), SellerKeys->ResolveInteraction(KeyE).Prompt.ToString(), FString(TEXT("Sell 1 fish (45 coins)")));
		const int32 Money0 = SellerMoney->GetMoney();
		const int32 TakerMoney0 = TakerMoney->GetMoney();
		SellerMoney->ClearNotices();
		SellerKeys->HandleInteractPressed();
		TestEqual(TEXT("... the host's E sells nothing"), SellerMoney->GetMoney(), Money0);
		TestTrue(TEXT("... the Bonefish still lies on the counter"), IsValid(A) && A->GetCounter() == Counter && Counter->GetFishOnCounter().Num() == 1);
		TestTrue(FString::Printf(TEXT("... the host is told (%s)"), *LCT::NoticesOf(Seller)), LCT::NoticesOf(Seller).Contains(TEXT("That just changed: nothing done. Now: Sell 1 fish (45 coins)")));

		// 3. A frame later the prompt shows the new offer; E sells exactly that.
		W.Tick(1);
		TestEqual(TEXT("the shown prompt refreshed"), ShownPrompt(SellerKeys, KeyE), FString(TEXT("Sell 1 fish (45 coins)")));
		SellerKeys->HandleInteractPressed();
		TestEqual(TEXT("... E sells it: paid 45"), SellerMoney->GetMoney(), Money0 + 45);
		TestEqual(TEXT("... the counter is empty"), Counter->GetFishOnCounter().Num(), 0);

		// 4. A key whose prompt showed no verb does nothing, even if one appeared this frame (never sells what wasn't shown).
		W.Tick(1);
		TestEqual(TEXT("the empty counter's shown E: info only"), ShownPrompt(SellerKeys, KeyE), FString(TEXT("Put fish on the counter to sell them")));
		ALureFishItem* C = PutOnLocal(W, Counter, Seller, LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 913)); // the taker still holds the snapper
		TestTrue(TEXT("a fish lands on the counter this frame"), C && Counter->GetFishOnCounter().Num() == 1);
		TestFalse(TEXT("... the key on the info prompt does nothing"), SellerKeys->PressKeyAsShown(KeyE));
		TestTrue(TEXT("... nothing sold"), SellerMoney->GetMoney() == Money0 + 45 && IsValid(C) && C->GetCounter() == Counter);

		// 5. A stale record (no local tick for two frames: paused, just possessed) is not what is on screen: the key acts on now.
		GFrameCounter += 2;
		FLureResolvedInteraction Stale;
		int32 StaleToken = 0;
		TestFalse(TEXT("the record is stale"), SellerKeys->GetShownInteraction(KeyE, Stale, StaleToken));
		SellerKeys->HandleInteractPressed();
		TestEqual(TEXT("... E sells what it shows now: paid 30"), SellerMoney->GetMoney(), Money0 + 75);

		// 6. The host's own request without the state token: refused (every key path sends it; only server code may skip it).
		ALureFishItem* D = PutOnLocal(W, Counter, Seller, LCT::MakeFish(TEXT("Bonefish"), 25, 1, 1.5f, 914));
		W.Tick(1);
		AddExpectedMessagePlain(NoTokenWarning, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestFalse(TEXT("RequestInteract(Sell) without a token is refused"), SellerKeys->RequestInteract(Counter, KeyE, ELureInteractVerb::SellCounter));
		TestTrue(TEXT("... nothing sold"), SellerMoney->GetMoney() == Money0 + 75 && IsValid(D) && D->GetCounter() == Counter);
		TestTrue(TEXT("... the E key sells it (it sends the token)"), SellerKeys->PressKeyAsShown(KeyE) && SellerMoney->GetMoney() == Money0 + 100);
		TestEqual(TEXT("the taker is never paid"), TakerMoney->GetMoney(), TakerMoney0);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

// Lure T-030 independent QA (qa-engineer): two players on a real in-process server (UE::Net::FTestWorlds: a dedicated server
// and 2 clients; every key press is a real ServerInteract RPC). Black-box from docs/specs/catch-handling-rules.md ("Focus and
// input" network rule, "Owner rules", "Leaving, falling in, getting caught") and the public headers. Project.Catch.QA.Net.*
//
// Patterns for the next QA agent:
// - Same-frame races: let both clients resolve their prompts, then PressKey on both with NO tick in between. Both RPCs go out
//   in the same TickAll and the server receives and judges them in one server frame, with no replication in between (the
//   second one runs against the state the first one left). Never assume who wins (packet order is the harness's business):
//   assert "exactly one", then name them Winner and Loser.
// - Census on EVERY machine (the server world and each client world), by a catch's identity (species, rarity, seed), after
//   every step: a catch exists exactly once, or 0 times once sold. Settles() waits for replication, then holds for 10 frames.
// - A player quitting = removing that client's FTestWorldInstance: its world ends and its net driver sends the close; the
//   server logs the player out (controller and pawn removed). Remove the LAST client so the other one keeps index 0, and
//   never look up the removed client or its server pawn again (the harness ensures on bad indexes and unreplicated objects).
// The whole file sits in namespace LureCatchQANet (unity builds: no file-scope using-directives).

#include "Tests/Catch/QACatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractable.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Tests/NetTestHelpers.h"

namespace LureCatchQANet
{
	constexpr float NetDt = 1.0f / 60.0f;
	constexpr ELureInteractKey KeyE = ELureInteractKey::Primary;
	constexpr ELureInteractKey KeyF = ELureInteractKey::Secondary;
	using EVerb = ELureInteractVerb;

	/** The shipped data every machine uses (text sources, never the binary assets); the markets are the fixture (Default x1.0) */
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
			const FLureCatchRow* Row = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("QACatchNetTest"), false);
			if (!Test.TestNotNull(TEXT("QA setup: DT_Catch Default"), Row))
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

		/** The spec's row choice restated: the row named like the species, else Default */
		FLureFreshnessRow FreshnessFor(FName Species) const
		{
			const FLureFreshnessRow* Row = Freshness->FindRow<FLureFreshnessRow>(Species, TEXT("QACatchNetTest"), false);
			Row = Row ? Row : Freshness->FindRow<FLureFreshnessRow>(TEXT("Default"), TEXT("QACatchNetTest"), false);
			return Row ? *Row : FLureFreshnessRow();
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

	/** A catch's identity on every machine: species, rarity and seed (the census key; a sold or stored catch has no item) */
	bool IsCatch(const FFishInstance& A, const FFishInstance& B)
	{
		return A.Seed == B.Seed && A.SpeciesId == B.SpeciesId && A.RarityId == B.RarityId;
	}

	/** Copies of Fish in World: live fish items (hook, hand, ground, counter) + records in every live cooler */
	int32 Copies(UWorld* World, const FFishInstance& Fish)
	{
		int32 Count = 0;
		if (!World)
		{
			return 0;
		}
		for (TActorIterator<ALureFishItem> It(World); It; ++It)
		{
			Count += (IsValid(*It) && !It->IsActorBeingDestroyed() && IsCatch(It->GetFish(), Fish)) ? 1 : 0;
		}
		for (TActorIterator<ALureCoolerActor> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetStorage())
			{
				for (const FLureCaughtFish& Record : It->GetStorage()->GetFish())
				{
					Count += IsCatch(Record.Fish, Fish) ? 1 : 0;
				}
			}
		}
		return Count;
	}

	/** The live fish item carrying Fish in World (null when it is in a cooler, sold or gone) */
	ALureFishItem* FindItem(UWorld* World, const FFishInstance& Fish)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ALureFishItem> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed() && IsCatch(It->GetFish(), Fish))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Nothing in the hands and nothing on the hook (any machine) */
	bool EmptyHanded(const ALurePlayerCharacter* Pawn)
	{
		const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
		return Hands && !Hands->IsHoldingSomething() && !Hands->GetHangingFish();
	}

	float Dist2D(const FVector& From, const FVector& To)
	{
		return static_cast<float>(FVector2D::Distance(FVector2D(From), FVector2D(To)));
	}

	int32 Money(const ALurePlayerCharacter* Pawn)
	{
		const ULureProgressionComponent* Progression = LCT::ProgressionOf(Pawn);
		return Progression ? Progression->GetMoney() : -1;
	}

	/** A server with two connected players standing on the dock (client 0 at y -100, client 1 at y +100, both facing +X) */
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

		/** Client's copy of a server object (null until it has arrived; never for a destroyed or unreplicated object) */
		template <typename T>
		T* On(int32 Client, T* ServerObject) const
		{
			if (!Worlds.Clients.IsValidIndex(Client) || !ServerObject || !Worlds.IsServerObjectReplicated(ServerObject)
				|| !Worlds.DoesReplicatedObjectExistOnClient(ServerObject, static_cast<uint32>(Client)))
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

		/** The client's player looks at Target (its own view; the server never checks it) */
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

		/** "server 1, client 0: 1, client 1: 1" */
		FString Census(const FFishInstance& Fish) const
		{
			FString Text = FString::Printf(TEXT("server %d"), Copies(Server, Fish));
			for (int32 Client = 0; Client < Worlds.Clients.Num(); ++Client)
			{
				Text += FString::Printf(TEXT(", client %d: %d"), Client, Copies(Worlds.Clients[Client].GetWorld(), Fish));
			}
			return Text;
		}

		bool CensusIs(const FFishInstance& Fish, int32 Expected) const
		{
			bool bAll = Copies(Server, Fish) == Expected;
			for (const UE::Net::FTestWorldInstance& Client : Worlds.Clients)
			{
				bAll &= Copies(Client.GetWorld(), Fish) == Expected;
			}
			return bAll;
		}

		/** The census reaches Expected on every machine (replication) and still holds 10 frames later */
		bool Settles(FAutomationTestBase& Test, const FFishInstance& Fish, int32 Expected, const FString& What)
		{
			const bool bReached = Until([&]() { return CensusIs(Fish, Expected); });
			Worlds.TickAll(10);
			return Test.TestTrue(FString::Printf(TEXT("%s: the catch exists %d time(s) on every machine (%s)"), *What, Expected, *Census(Fish)),
				bReached && CensusIs(Fish, Expected));
		}

		/** Client lands Fish (server), grabs it (E) and puts it on Counter (E): the real keys. The item (server), or null. */
		ALureFishItem* PutOnCounter(FAutomationTestBase& Test, ALureSellCounter* Counter, int32 Client, const FFishInstance& Fish)
		{
			ALureFishItem* Item = Cast<ALureFishItem>(ULureCatchLibrary::HandleFishLanded(Players[Client], Fish).FishItem);
			if (!Test.TestNotNull(TEXT("QA setup: the catch lands"), Item)
				|| !Test.TestTrue(TEXT("QA setup: it hangs on the angler's machine"), Until([&]() { return Mine(Client)->GetHands()->GetHangingFish() != nullptr; })))
			{
				return nullptr;
			}
			// Look away first: facing a counter with fish on it, E would be the counter's Sell (the focused target wins).
			LookAt(Client, Mine(Client)->GetPawnViewLocation() + FVector(-200.0f, 0.0f, -100.0f));
			Worlds.TickAll(2);
			Test.TestEqual(TEXT("QA setup: E grabs the hanging fish"), LCT::VerbName(Keys(Client)->ResolveInteraction(KeyE).Verb), LCT::VerbName(EVerb::GrabFish));
			Test.TestTrue(TEXT("QA setup: E"), Keys(Client)->PressKey(KeyE));
			if (!Test.TestTrue(TEXT("QA setup: in the angler's hand"), Until([&]() { return Mine(Client)->GetHands()->GetHeldFish() != nullptr; })))
			{
				return nullptr;
			}
			LookAt(Client, On(Client, Counter)->GetActorLocation());
			Worlds.TickAll(2);
			Test.TestEqual(TEXT("QA setup: E puts it on the counter"), LCT::VerbName(Keys(Client)->ResolveInteraction(KeyE).Verb), LCT::VerbName(EVerb::PlaceFishOnCounter));
			Test.TestTrue(TEXT("QA setup: E"), Keys(Client)->PressKey(KeyE));
			return Test.TestTrue(TEXT("QA setup: on the counter (server)"), Until([&]() { return Item->GetCounter() == Counter && Item->IsFree(); })) ? Item : nullptr;
		}
	};

	// =====================================================================================================================
	// Races: two clients ask for the same thing in the same frame
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQANetTakeOutRace, "Project.Catch.QA.Net.SimultaneousTakeOutOneWinner", LCT::Flags)
	bool FCatchQANetTakeOutRace::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		const FFishInstance Last = LureCatchQA::Roll(*this, Net.Data.Fish, TEXT("CoralSnapper"), 3101);
		ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(130.0f, 0.0f, LCT::DockTop)),
			Net.Players[0]->GetPlayerState()); // in both players' reach
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler) || !Last.IsValid())
		{
			return false;
		}
		TestTrue(TEXT("QA setup: the last fish in the cooler, lid open"), Cooler->GetStorage()->AddFish(LureCatchQA::Record(Last, FLureFreshness::GetServerTime(Net.Server)))
			&& Cooler->AuthoritySetLidOpen(true));
		if (!TestTrue(TEXT("both clients see the open cooler with its one fish"), Net.Until([&]()
			{
				const ALureCoolerActor* Copy0 = Net.On(0, Cooler);
				const ALureCoolerActor* Copy1 = Net.On(1, Cooler);
				return Copy0 && Copy1 && Copy0->GetNumFish() == 1 && Copy1->GetNumFish() == 1 && Copy0->IsLidOpen() && Copy1->IsLidOpen();
			})))
		{
			return false;
		}
		Net.Settles(*this, Last, 1, TEXT("before the race"));

		// Both clients look at the cooler; both prompts offer the same last fish.
		FLureResolvedInteraction Seen[2];
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Net.LookAt(Client, Net.On(Client, Cooler)->GetInteractionLocation());
		}
		Worlds.TickAll(2);
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Seen[Client] = Net.Keys(Client)->ResolveInteraction(KeyE);
			TestTrue(FString::Printf(TEXT("client %d's E: take the fish out of this cooler"), Client),
				Seen[Client].Verb == EVerb::TakeFishFromCooler && Seen[Client].Target.Get() == Net.On(Client, Cooler));
		}

		// Same frame: both E presses go out before any tick, so the server judges both requests in one frame.
		const bool bSent0 = Net.Keys(0)->PressKey(KeyE);
		const bool bSent1 = Net.Keys(1)->PressKey(KeyE);
		TestTrue(TEXT("both clients send their take-out"), bSent0 && bSent1);
		const bool bTaken = Net.Until([&]() { return Cooler->GetNumFish() == 0 && (LCT::HandsOf(Net.Players[0])->GetHeldFish() || LCT::HandsOf(Net.Players[1])->GetHeldFish()); });
		Worlds.TickAll(10); // the second request has arrived and been judged
		const ALureFishItem* Held0 = LCT::HandsOf(Net.Players[0])->GetHeldFish();
		const ALureFishItem* Held1 = LCT::HandsOf(Net.Players[1])->GetHeldFish();
		if (!TestTrue(TEXT("exactly one player holds the fish (server)"), bTaken && ((Held0 != nullptr) != (Held1 != nullptr))))
		{
			return false;
		}
		const int32 Winner = Held0 ? 0 : 1;
		const int32 Loser = 1 - Winner;
		AddInfo(FString::Printf(TEXT("client %d won the take-out race"), Winner));
		ALureFishItem* Item = LCT::HandsOf(Net.Players[Winner])->GetHeldFish();
		TestTrue(TEXT("... exactly the rolled catch, unchanged"), FishQA::Same(Item->GetFish(), Last));
		TestTrue(TEXT("the loser's hands stay empty (server)"), EmptyHanded(Net.Players[Loser]));
		TestTrue(TEXT("the loser's stale request did nothing else: the lid is still open, the cooler empty"), Cooler->IsLidOpen() && Cooler->GetNumFish() == 0);
		Net.Settles(*this, Last, 1, TEXT("after the take-out race"));
		TestTrue(TEXT("every client sees the fish in the winner's hand and the loser's hands empty"), Net.Until([&]()
		{
			for (int32 Client = 0; Client < 2; ++Client)
			{
				const ALureFishItem* Copy = Net.On(Client, Item);
				const ALureCoolerActor* CoolerCopy = Net.On(Client, Cooler);
				if (!Copy || !CoolerCopy || !Copy->IsHeldBy(Net.On(Client, Net.Players[Winner]), ELureHoldMode::Hand) || !EmptyHanded(Net.On(Client, Net.Players[Loser]))
					|| CoolerCopy->GetNumFish() != 0 || !CoolerCopy->IsLidOpen())
				{
					return false;
				}
			}
			return true;
		}));
		TestEqual(TEXT("the loser's E is now 'Close' (open and empty): a stale 'take out' could only have closed it"),
			LCT::VerbName(Net.Keys(Loser)->ResolveInteraction(KeyE).Verb), LCT::VerbName(EVerb::CloseCooler));

		// The same fish again, loose on the dock: the winner drops it (F) toward the other player (away from the cooler's
		// focus cone, so F is the held fish's own verb), then both grab it in the same frame.
		Net.LookAt(Winner, FVector(0.0f, 0.0f, LCT::DockTop));
		Worlds.TickAll(2);
		TestEqual(TEXT("the winner's F drops the fish"), LCT::VerbName(Net.Keys(Winner)->ResolveInteraction(KeyF).Verb), LCT::VerbName(EVerb::DropFish));
		TestTrue(TEXT("winner: F"), Net.Keys(Winner)->PressKey(KeyF));
		ALureFishItem* Loose = nullptr;
		const bool bLies = Net.Until([&]() { Loose = FindItem(Net.Server, Last); return Loose && Loose->IsFree(); });
		if (!TestTrue(TEXT("the fish lies loose on the dock (server)"), bLies && Loose->GetCounter() == nullptr && FMath::Abs(Loose->GetActorLocation().Z - LCT::DockTop) < 30.0f))
		{
			return false;
		}
		Net.Settles(*this, Last, 1, TEXT("dropped on the dock"));
		TestTrue(TEXT("both clients see it lying at rest"), Net.Until([&]()
		{
			const ALureFishItem* Copy0 = Net.On(0, Loose);
			const ALureFishItem* Copy1 = Net.On(1, Loose);
			return Copy0 && Copy1 && Copy0->IsFree() && Copy1->IsFree() && Copy0->GetFlightTimeLeft() <= 0.0f && Copy1->GetFlightTimeLeft() <= 0.0f;
		}));
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Net.LookAt(Client, Net.On(Client, Loose)->GetActorLocation());
		}
		Worlds.TickAll(2);
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Seen[Client] = Net.Keys(Client)->ResolveInteraction(KeyE);
			TestTrue(FString::Printf(TEXT("client %d's E: grab the loose fish"), Client), Seen[Client].Verb == EVerb::GrabFish && Seen[Client].Target.Get() == Net.On(Client, Loose));
		}
		const bool bGrab0 = Net.Keys(0)->PressKey(KeyE);
		const bool bGrab1 = Net.Keys(1)->PressKey(KeyE);
		TestTrue(TEXT("both clients send their grab"), bGrab0 && bGrab1);
		const bool bGrabbed = Net.Until([&]() { return Loose->GetHoldMode() == ELureHoldMode::Hand; });
		Worlds.TickAll(10);
		const bool bHas0 = Loose->IsHeldBy(Net.Players[0], ELureHoldMode::Hand);
		const bool bHas1 = Loose->IsHeldBy(Net.Players[1], ELureHoldMode::Hand);
		if (!TestTrue(TEXT("exactly one player holds the loose fish (server)"), bGrabbed && bHas0 != bHas1))
		{
			return false;
		}
		const int32 GrabWinner = bHas0 ? 0 : 1;
		AddInfo(FString::Printf(TEXT("client %d won the grab race"), GrabWinner));
		TestTrue(TEXT("the other player's hands stay empty (server)"), EmptyHanded(Net.Players[1 - GrabWinner]));
		TestTrue(TEXT("... and its hand-cache agrees: the winner's hands hold exactly this item"), LCT::HandsOf(Net.Players[GrabWinner])->GetHeldFish() == Loose);
		Net.Settles(*this, Last, 1, TEXT("after the grab race"));
		TestTrue(TEXT("every client sees it in the winner's hand"), Net.Until([&]()
		{
			for (int32 Client = 0; Client < 2; ++Client)
			{
				const ALureFishItem* Copy = Net.On(Client, Loose);
				if (!Copy || !Copy->IsHeldBy(Net.On(Client, Net.Players[GrabWinner]), ELureHoldMode::Hand) || !EmptyHanded(Net.On(Client, Net.Players[1 - GrabWinner])))
				{
					return false;
				}
			}
			return true;
		}));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQANetSellRace, "Project.Catch.QA.Net.SimultaneousSellPaysOnce", LCT::Flags)
	bool FCatchQANetSellRace::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		ALureSellCounter* Counter = Net.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
		if (!TestNotNull(TEXT("QA setup: counter"), Counter) || !TestTrue(TEXT("QA setup: the counter reaches both clients"), Net.Until([&]() { return Net.On(0, Counter) && Net.On(1, Counter); })))
		{
			return false;
		}
		const FFishInstance Catches[2] = { LureCatchQA::Roll(*this, Net.Data.Fish, TEXT("Bonefish"), 3201), LureCatchQA::Roll(*this, Net.Data.Fish, TEXT("CoralSnapper"), 3202) };
		ALureFishItem* Items[2] = { nullptr, nullptr };
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Items[Client] = Net.PutOnCounter(*this, Counter, Client, Catches[Client]);
			if (!Items[Client])
			{
				return false;
			}
		}
		if (!TestTrue(TEXT("both clients see 2 fish on the counter"), Net.Until([&]() { return Net.On(0, Counter)->GetFishOnCounter().Num() == 2 && Net.On(1, Counter)->GetFishOnCounter().Num() == 2; })))
		{
			return false;
		}

		// The oracle: inside the grace every value share is 1 (the price can't move during the race); the fixture market pays x1.
		TestEqual(TEXT("QA precondition: the fixture market Default pays x1"), Counter->GetSellMultiplier(), 1.0f);
		int32 Expected = 0;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FLureFreshnessRow Row = Net.Data.FreshnessFor(Catches[Index].SpeciesId);
			const double Exposure = Items[Index]->GetExposureSeconds();
			TestTrue(FString::Printf(TEXT("QA precondition: fish %d is inside its grace (%.1f s of %.0f)"), Index, Exposure, Row.GraceSeconds), Exposure + 5.0 < Row.GraceSeconds);
			Expected += LureCatchQA::OraclePrice(Catches[Index].Value, LureCatchQA::OracleShare(Row.GraceSeconds, Row.SpoilSeconds, Row.CurveExponent, Row.MinValueShare, Exposure + 1.0), 1.0);
		}
		TestEqual(TEXT("the server's quote is the oracle price of both fish"), Counter->QuoteAll(), Expected);
		const FString Offer = FString::Printf(TEXT("Sell 2 fish (%d coins)"), Expected);
		FLureResolvedInteraction Seen[2];
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Net.LookAt(Client, Net.On(Client, Counter)->GetActorLocation());
		}
		Worlds.TickAll(2);
		for (int32 Client = 0; Client < 2; ++Client)
		{
			Seen[Client] = Net.Keys(Client)->ResolveInteraction(KeyE);
			TestTrue(FString::Printf(TEXT("client %d's E: '%s' (it shows '%s')"), Client, *Offer, *Seen[Client].Prompt.ToString()),
				Seen[Client].Verb == EVerb::SellCounter && Seen[Client].Prompt.ToString() == Offer);
		}
		const int32 Money0[2] = { Money(Net.Players[0]), Money(Net.Players[1]) };
		const int32 Xp0 = LCT::ProgressionOf(Net.Players[0])->GetTotalXp() + LCT::ProgressionOf(Net.Players[1])->GetTotalXp();

		// Same frame: both sell presses reach the server together.
		const bool bSent0 = Net.Keys(0)->PressKey(KeyE);
		const bool bSent1 = Net.Keys(1)->PressKey(KeyE);
		TestTrue(TEXT("both clients send their sale"), bSent0 && bSent1);
		const bool bSold = Net.Until([&]() { return Counter->GetFishOnCounter().Num() == 0 && (Money(Net.Players[0]) != Money0[0] || Money(Net.Players[1]) != Money0[1]); });
		Worlds.TickAll(20); // the second request has arrived and been judged
		const int32 Paid[2] = { Money(Net.Players[0]) - Money0[0], Money(Net.Players[1]) - Money0[1] };
		TestTrue(TEXT("the sale happened"), bSold);
		TestEqual(FString::Printf(TEXT("paid exactly once in total (%d + %d)"), Paid[0], Paid[1]), Paid[0] + Paid[1], Expected);
		if (!TestTrue(TEXT("... all of it to one of the two sellers"), (Paid[0] == Expected && Paid[1] == 0) || (Paid[1] == Expected && Paid[0] == 0)))
		{
			return false;
		}
		const int32 Winner = Paid[0] == Expected ? 0 : 1;
		const int32 Loser = 1 - Winner;
		AddInfo(FString::Printf(TEXT("client %d won the sale race"), Winner));
		TestEqual(TEXT("XP is not given again by selling"), LCT::ProgressionOf(Net.Players[0])->GetTotalXp() + LCT::ProgressionOf(Net.Players[1])->GetTotalXp(), Xp0);
		TestTrue(TEXT("the loser's stale sale did nothing else (hands empty)"), EmptyHanded(Net.Players[Loser]));
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Net.Settles(*this, Catches[Index], 0, FString::Printf(TEXT("sold fish %d"), Index));
		}
		TestTrue(TEXT("the counter is empty on every machine"), Net.Until([&]() { return Net.On(0, Counter)->GetFishOnCounter().Num() == 0 && Net.On(1, Counter)->GetFishOnCounter().Num() == 0; }));
		const FString Notice = FString::Printf(TEXT("Sold 2 fish for %d coins"), Expected);
		TestTrue(FString::Printf(TEXT("the winner's machine shows '%s'"), *Notice), Net.Until([&]() { return LCT::NoticesOf(Net.Mine(Winner)).Contains(Notice); }));
		Worlds.TickAll(10);
		TestFalse(FString::Printf(TEXT("the loser's machine shows no sale (%s)"), *LCT::NoticesOf(Net.Mine(Loser))), LCT::NoticesOf(Net.Mine(Loser)).Contains(TEXT("Sold")));
		return true;
	}

	/** Sell vs take-back setup: client 0's catch on the counter first, client 1's last; both look at the counter. Each client's
	 *  request (target, verb, the state token its prompt was built from) is captured here, so the test decides which one the
	 *  server receives first: the second one is sent only after the first has been judged and replicated (no timing luck). */
	struct FSellTakeBack
	{
		static constexpr int32 Taker = 0;
		static constexpr int32 Seller = 1;
		ALureSellCounter* Counter = nullptr;
		FFishInstance Catches[2];
		int32 Price[2] = { 0, 0 };
		FLureResolvedInteraction TakeSeen;
		FLureResolvedInteraction SellSeen;
		int32 TakeToken = 0;
		int32 SellToken = 0;
		int32 Money0[2] = { 0, 0 };

		bool Setup(FAutomationTestBase& Test, FNet& Net, int32 SeedBase)
		{
			Counter = Net.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
			if (!Test.TestNotNull(TEXT("QA setup: counter"), Counter) || !Test.TestTrue(TEXT("QA setup: the counter reaches both clients"), Net.Until([&]() { return Net.On(0, Counter) && Net.On(1, Counter); })))
			{
				return false;
			}
			Catches[0] = LureCatchQA::Roll(Test, Net.Data.Fish, TEXT("Bonefish"), SeedBase + 1);
			Catches[1] = LureCatchQA::Roll(Test, Net.Data.Fish, TEXT("CoralSnapper"), SeedBase + 2);
			ALureFishItem* Items[2] = { nullptr, nullptr };
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Items[Index] = Net.PutOnCounter(Test, Counter, Index, Catches[Index]);
				if (!Items[Index])
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const FLureFreshnessRow Row = Net.Data.FreshnessFor(Catches[Index].SpeciesId);
				const double Exposure = Items[Index]->GetExposureSeconds();
				Test.TestTrue(FString::Printf(TEXT("QA precondition: fish %d is inside its grace (%.1f s)"), Index, Exposure), Exposure + 5.0 < Row.GraceSeconds);
				Price[Index] = LureCatchQA::OraclePrice(Catches[Index].Value, LureCatchQA::OracleShare(Row.GraceSeconds, Row.SpoilSeconds, Row.CurveExponent, Row.MinValueShare, Exposure + 1.0), 1.0);
			}
			if (!Test.TestTrue(TEXT("both clients see 2 fish on the counter"), Net.Until([&]() { return Net.On(0, Counter)->GetFishOnCounter().Num() == 2 && Net.On(1, Counter)->GetFishOnCounter().Num() == 2; })))
			{
				return false;
			}
			for (int32 Client = 0; Client < 2; ++Client)
			{
				Net.LookAt(Client, Net.On(Client, Counter)->GetActorLocation());
			}
			Net.Worlds.TickAll(2);
			TakeSeen = Net.Keys(Taker)->ResolveInteraction(KeyF);
			SellSeen = Net.Keys(Seller)->ResolveInteraction(KeyE);
			Test.TestTrue(TEXT("client 0's F: take the last fish back"), TakeSeen.Verb == EVerb::TakeFishFromCounter && TakeSeen.Target.Get() == Net.On(Taker, Counter));
			Test.TestTrue(FString::Printf(TEXT("client 1's E: sell both (%s)"), *SellSeen.Prompt.ToString()), SellSeen.Verb == EVerb::SellCounter && SellSeen.Target.Get() == Net.On(Seller, Counter)
				&& SellSeen.Prompt.ToString() == FString::Printf(TEXT("Sell 2 fish (%d coins)"), Price[0] + Price[1]));
			// The tokens exactly as PressKey would send them (read from each client's own copy of the counter).
			const ILureInteractable* TakerView = Cast<ILureInteractable>(Net.On(Taker, Counter));
			const ILureInteractable* SellerView = Cast<ILureInteractable>(Net.On(Seller, Counter));
			if (!Test.TestTrue(TEXT("QA setup: the counter is interactable on both clients"), TakerView && SellerView))
			{
				return false;
			}
			TakeToken = TakerView->GetInteractionStateToken(Net.Mine(Taker), EVerb::TakeFishFromCounter);
			SellToken = SellerView->GetInteractionStateToken(Net.Mine(Seller), EVerb::SellCounter);
			Money0[0] = Money(Net.Players[0]);
			Money0[1] = Money(Net.Players[1]);
			return TakeSeen.Verb == EVerb::TakeFishFromCounter && SellSeen.Verb == EVerb::SellCounter;
		}

		/** Client sends the request it captured in Setup (its now stale view), through the real ServerInteract RPC */
		bool SendStale(FNet& Net, int32 Client) const
		{
			const FLureResolvedInteraction& Seen = Client == Taker ? TakeSeen : SellSeen;
			return Net.Keys(Client)->RequestInteract(Seen.Target.Get(), Client == Taker ? KeyF : KeyE, Seen.Verb, Client == Taker ? TakeToken : SellToken);
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQANetTakeBackBeforeSell, "Project.Catch.QA.Net.SellRacesTakeBack.TakeBackFirst", LCT::Flags)
	bool FCatchQANetTakeBackBeforeSell::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		FSellTakeBack Race;
		if (!Net.Create(*this) || !Race.Setup(*this, Net, 3400))
		{
			return false;
		}
		constexpr int32 Taker = FSellTakeBack::Taker;
		constexpr int32 Seller = FSellTakeBack::Seller;
		TestNotEqual(TEXT("QA precondition: the sale carries a state token (else the server can't see the change)"), Race.SellToken, 0);

		// Forced order: the take-back is sent, judged and replicated before the seller's (stale) sale leaves its machine.
		TestTrue(TEXT("taker: F sent"), Race.SendStale(Net, Taker));
		if (!TestTrue(TEXT("the take-back ran on the server and both clients see 1 fish left"), Net.Until([&]()
			{
				return LCT::HandsOf(Net.Players[Taker])->GetHeldFish() != nullptr && Race.Counter->GetFishOnCounter().Num() == 1
					&& Net.On(0, Race.Counter)->GetFishOnCounter().Num() == 1 && Net.On(1, Race.Counter)->GetFishOnCounter().Num() == 1;
			})))
		{
			return false;
		}
		const ALureFishItem* Held = LCT::HandsOf(Net.Players[Taker])->GetHeldFish();
		TestTrue(TEXT("the take-back took the last fish put on the counter (client 1's)"), IsCatch(Held->GetFish(), Race.Catches[1]));
		TestTrue(TEXT("seller: the stale Sell 2 is sent"), Race.SendStale(Net, Seller));
		TestTrue(FString::Printf(TEXT("the seller sees 'That just changed: nothing done. Now: Sell 1 fish (...)' (%s)"), *LCT::NoticesOf(Net.Mine(Seller))),
			Net.Until([&]() { return LCT::NoticesOf(Net.Mine(Seller)).Contains(TEXT("That just changed: nothing done. Now: Sell 1 fish (")); }));
		Worlds.TickAll(10);
		TestEqual(TEXT("the taker is paid nothing"), Money(Net.Players[Taker]) - Race.Money0[Taker], 0);
		TestEqual(TEXT("the stale sale pays nothing"), Money(Net.Players[Seller]) - Race.Money0[Seller], 0);
		TestEqual(TEXT("the first catch is still on the counter (1 copy)"), Copies(Net.Server, Race.Catches[0]), 1);
		TestEqual(TEXT("the counter holds exactly 1 fish (server)"), Race.Counter->GetFishOnCounter().Num(), 1);
		TestFalse(FString::Printf(TEXT("no partial sale notice (%s)"), *LCT::NoticesOf(Net.Mine(Seller))), LCT::NoticesOf(Net.Mine(Seller)).Contains(TEXT("Sold")));
		TestTrue(TEXT("the taker's machine shows the fish in its own hand"), Net.Until([&]() { const ALureFishItem* Own = Net.Mine(Taker)->GetHands()->GetHeldFish(); return Own && IsCatch(Own->GetFish(), Race.Catches[1]); }));

		// Second E (a real key press): sells exactly what the refreshed prompt shows.
		const FLureResolvedInteraction Again = Net.Keys(Seller)->ResolveInteraction(KeyE);
		const FString AgainPrompt = Again.Prompt.ToString();
		int32 PromptCoins = -1;
		FString Left, Right;
		if (AgainPrompt.Split(TEXT("("), &Left, &Right))
		{
			PromptCoins = FCString::Atoi(*Right);
		}
		TestTrue(FString::Printf(TEXT("the refreshed prompt is 'Sell 1 fish (N coins)' (%s)"), *AgainPrompt), Again.Verb == EVerb::SellCounter && AgainPrompt.StartsWith(TEXT("Sell 1 fish (")) && PromptCoins > 0);
		TestTrue(FString::Printf(TEXT("the refreshed prompt's price is the remaining fish's price (%d vs %d)"), PromptCoins, Race.Price[0]), FMath::Abs(PromptCoins - Race.Price[0]) <= 1);
		const int32 MoneyBefore = Money(Net.Players[Seller]);
		TestTrue(TEXT("the second Sell is sent"), Net.Keys(Seller)->PressKey(KeyE));
		TestTrue(TEXT("the second Sell empties the counter (server)"), Net.Until([&]() { return Race.Counter->GetFishOnCounter().Num() == 0 && Money(Net.Players[Seller]) != MoneyBefore; }));
		Worlds.TickAll(10);
		TestEqual(TEXT("the second Sell pays exactly the prompt's total"), Money(Net.Players[Seller]) - MoneyBefore, PromptCoins);
		Net.Settles(*this, Race.Catches[0], 0, TEXT("catch 0 (sold on the second E)"));
		Net.Settles(*this, Race.Catches[1], 1, TEXT("catch 1 (taken back)"));
		const FString Notice = FString::Printf(TEXT("Sold 1 fish for %d coins"), PromptCoins);
		TestTrue(FString::Printf(TEXT("the seller's machine shows '%s'"), *Notice), Net.Until([&]() { return LCT::NoticesOf(Net.Mine(Seller)).Contains(Notice); }));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQANetSellBeforeTakeBack, "Project.Catch.QA.Net.SellRacesTakeBack.SellFirst", LCT::Flags)
	bool FCatchQANetSellBeforeTakeBack::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		FSellTakeBack Race;
		if (!Net.Create(*this) || !Race.Setup(*this, Net, 3410))
		{
			return false;
		}
		constexpr int32 Taker = FSellTakeBack::Taker;
		constexpr int32 Seller = FSellTakeBack::Seller;
		const int32 Full = Race.Price[0] + Race.Price[1];

		// Forced order: the sale is sent, judged and replicated before the taker's (stale) take-back leaves its machine.
		TestTrue(TEXT("seller: E sent"), Race.SendStale(Net, Seller));
		if (!TestTrue(TEXT("the sale ran on the server and both clients see an empty counter"), Net.Until([&]()
			{
				return Money(Net.Players[Seller]) != Race.Money0[Seller] && Race.Counter->GetFishOnCounter().Num() == 0
					&& Net.On(0, Race.Counter)->GetFishOnCounter().Num() == 0 && Net.On(1, Race.Counter)->GetFishOnCounter().Num() == 0;
			})))
		{
			return false;
		}
		TestEqual(TEXT("the seller is paid the full prompt (both fish)"), Money(Net.Players[Seller]) - Race.Money0[Seller], Full);
		TestTrue(TEXT("taker: the stale take-back is sent"), Race.SendStale(Net, Taker));
		Worlds.TickAll(20); // the stale take-back has arrived and been judged
		TestTrue(TEXT("the stale take-back takes nothing (taker's hands empty, server)"), EmptyHanded(Net.Players[Taker]));
		TestEqual(TEXT("the taker is paid nothing"), Money(Net.Players[Taker]) - Race.Money0[Taker], 0);
		TestEqual(TEXT("the seller's pay is unchanged by the stale take-back"), Money(Net.Players[Seller]) - Race.Money0[Seller], Full);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Net.Settles(*this, Race.Catches[Index], 0, FString::Printf(TEXT("catch %d (sold)"), Index));
		}
		const FString Notice = FString::Printf(TEXT("Sold 2 fish for %d coins"), Full);
		TestTrue(FString::Printf(TEXT("the seller's machine shows '%s'"), *Notice), Net.Until([&]() { return LCT::NoticesOf(Net.Mine(Seller)).Contains(Notice); }));
		TestTrue(TEXT("the taker's machine shows empty hands"), EmptyHanded(Net.Mine(Taker)));
		return true;
	}

	// =====================================================================================================================
	// Leaving: the cooler's carrier quits
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQANetCarrierLeaves, "Project.Catch.QA.Net.CarrierLeavesCoolerStaysUsable", LCT::Flags)
	bool FCatchQANetCarrierLeaves::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		// The carrier is client 1 (the last one: when it quits, the friend keeps client index 0). Its own starter-like cooler.
		ALurePlayerCharacter* Carrier = Net.Players[1];
		ALurePlayerCharacter* Friend = Net.Players[0];
		const TArray<FFishInstance> Inside = { LureCatchQA::Roll(*this, Net.Data.Fish, TEXT("Bonefish"), 3301), LureCatchQA::Roll(*this, Net.Data.Fish, TEXT("CoralSnapper"), 3302) };
		ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(110.0f, 100.0f, LCT::DockTop)), Carrier->GetPlayerState());
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		for (const FFishInstance& Fish : Inside)
		{
			TestTrue(TEXT("QA setup: a catch in the cooler"), Cooler->GetStorage()->AddFish(LureCatchQA::Record(Fish, FLureFreshness::GetServerTime(Net.Server))));
		}
		if (!TestTrue(TEXT("QA setup: both clients see the cooler with 2 fish"), Net.Until([&]() { return Net.On(0, Cooler) && Net.On(1, Cooler) && Net.On(0, Cooler)->GetNumFish() == 2 && Net.On(1, Cooler)->GetNumFish() == 2; })))
		{
			return false;
		}
		Net.LookAt(1, Net.On(1, Cooler)->GetInteractionLocation());
		Worlds.TickAll(2);
		TestEqual(TEXT("the carrier's F picks the cooler up"), LCT::VerbName(Net.Keys(1)->ResolveInteraction(KeyF).Verb), LCT::VerbName(EVerb::PickUpCooler));
		TestTrue(TEXT("carrier: F"), Net.Keys(1)->PressKey(KeyF));
		if (!TestTrue(TEXT("carried by the carrier on every machine"), Net.Until([&]()
			{
				return Cooler->IsHeldBy(Carrier, ELureHoldMode::Hand) && Net.On(0, Cooler)->IsHeldBy(Net.On(0, Carrier), ELureHoldMode::Hand)
					&& Net.On(1, Cooler)->IsHeldBy(Net.Mine(1), ELureHoldMode::Hand);
			})))
		{
			return false;
		}
		FVector Dry = FVector::ZeroVector;
		if (!TestTrue(TEXT("QA precondition: the server knows the carrier's last dry ground spot"), Net.Until([&]() { return LCT::HandsOf(Carrier)->GetLastDryGround(Dry); })))
		{
			return false;
		}
		for (const FFishInstance& Fish : Inside)
		{
			Net.Settles(*this, Fish, 1, TEXT("carried"));
		}

		// The carrier's game quits: its client world ends and its net driver tells the server, which logs the player out
		// (controller and pawn removed). Nothing of client 1 or its server pawn is looked up again after this.
		// Coolers are counted relative to this moment: the harness runs ALureGameMode, which gives each player a starter cooler at
		// login (the map has no PlayerStart, so the engine's start spot is the WorldSettings). The rule under test: leaving makes
		// no cooler and removes none.
		const int32 ServerCoolersBefore = LureCatchQA::CountCoolers(Net.Server);
		const int32 Client0CoolersBefore = LureCatchQA::CountCoolers(Worlds.Clients[0].GetWorld());
		AddInfo(FString::Printf(TEXT("coolers before the quit: server %d, client 0 %d (this test's cooler + the automatic starters)"), ServerCoolersBefore, Client0CoolersBefore));
		const TWeakObjectPtr<ALurePlayerCharacter> CarrierAlive = Carrier;
		const TWeakObjectPtr<APlayerController> CarrierController = Worlds.GetServerPlayerControllerOfClient(1);
		Net.Players[1] = nullptr;
		Carrier = nullptr;
		Worlds.Clients.RemoveAt(1);
		bool bLeft = Net.Until([&]() { return !CarrierAlive.IsValid() || CarrierAlive->IsActorBeingDestroyed(); }, 300);
		if (!bLeft && CarrierController.IsValid())
		{
			// Fallback (VibeGame doesn't link NetCore, so no UNetConnection::Close here): the engine's own removal of a player.
			AddInfo(TEXT("The server had not seen the quit after 5 s: kicking the player (AGameSession::KickPlayer removes pawn and controller, as a timeout would)"));
			const AGameModeBase* Mode = Net.Server->GetAuthGameMode();
			if (Mode && Mode->GameSession)
			{
				Mode->GameSession->KickPlayer(CarrierController.Get(), FText::FromString(TEXT("QA: the player left")));
			}
			bLeft = Net.Until([&]() { return !CarrierAlive.IsValid() || CarrierAlive->IsActorBeingDestroyed(); }, 300);
		}
		if (!TestTrue(TEXT("the server removed the carrier's pawn (the player left)"), bLeft))
		{
			return false;
		}
		Worlds.TickAll(3);

		// Nothing is lost: the cooler stands at the carrier's last dry ground spot, with its fish, lid closed.
		if (!TestTrue(TEXT("server: the cooler is still there and free (nobody holds it)"), IsValid(Cooler) && !Cooler->IsActorBeingDestroyed() && Cooler->IsFree() && Cooler->GetHolder() == nullptr))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("server: it stands on the dock at the last dry spot (%s vs %s)"), *Cooler->GetActorLocation().ToCompactString(), *Dry.ToCompactString()),
			Dist2D(Cooler->GetActorLocation(), Dry) < 5.0f && FMath::Abs(Cooler->GetActorLocation().Z - LCT::DockTop) < 1.5f);
		TestTrue(TEXT("server: its 2 fish, lid closed, spoiling at the closed rate"), Cooler->GetNumFish() == 2 && !Cooler->IsLidOpen() && Cooler->GetDecayRate() == Cooler->GetRow().ClosedDecayRate);
		TestEqual(TEXT("server: the leave made no cooler and removed none"), LureCatchQA::CountCoolers(Net.Server), ServerCoolersBefore);
		for (const FFishInstance& Fish : Inside)
		{
			TestEqual(TEXT("server: the catch exists once, exactly as rolled"), LureCatchQA::CountCopies(Net.Server, Fish), 1);
			Net.Settles(*this, Fish, 1, TEXT("after the carrier left"));
		}
		AddInfo(FString::Printf(TEXT("owner after the owner left: %s"), *GetNameSafe(Cooler->GetOwningPlayerState())));

		// The friend's machine agrees: free (no stale holder), where the server put it, standing (collision on), 2 fish.
		ALureCoolerActor* Cooler0 = Net.On(0, Cooler);
		if (!TestTrue(TEXT("client 0: the cooler is free, where the server put it, at rest"), Net.Until([&]()
			{
				return Cooler0 && Cooler0->IsFree() && Cooler0->GetHolder() == nullptr && Cooler0->GetFlightTimeLeft() <= 0.0f
					&& Cooler0->GetActorLocation().Equals(Cooler->GetActorLocation(), 0.2) && Cooler0->GetNumFish() == 2;
			})))
		{
			return false;
		}
		Worlds.TickAll(5);
		TestEqual(TEXT("client 0: it stands (collision on)"), static_cast<int32>(Cooler0->GetCollisionBox()->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::QueryAndPhysics));
		TestEqual(TEXT("server: it stands (collision on)"), static_cast<int32>(Cooler->GetCollisionBox()->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::QueryAndPhysics));
		TestEqual(TEXT("client 0: the leave made no cooler and removed none"), LureCatchQA::CountCoolers(Worlds.Clients[0].GetWorld()), Client0CoolersBefore);

		// The dedicated server's gameplay box is the clients' box, lid included (a mismatch = different collision and put-down
		// room on the server than the players see).
		const FBox ServerBox = Cooler->GetCollisionBox()->Bounds.GetBox();
		const FBox ClientBox = Cooler0->GetCollisionBox()->Bounds.GetBox();
		const FString Boxes = FString::Printf(TEXT("server %s..%s, client %s..%s"), *ServerBox.Min.ToCompactString(), *ServerBox.Max.ToCompactString(),
			*ClientBox.Min.ToCompactString(), *ClientBox.Max.ToCompactString());
		TestTrue(TEXT("the dedicated server's collision box is the client's (") + Boxes + TEXT(")"), ServerBox.Min.Equals(ClientBox.Min, 0.5) && ServerBox.Max.Equals(ClientBox.Max, 0.5));
		TestTrue(TEXT("... the same half size and center in the cooler's space"), Cooler->GetBoxHalfExtent().Equals(Cooler0->GetBoxHalfExtent(), 0.1) && Cooler->GetBoxCenter().Equals(Cooler0->GetBoxCenter(), 0.1));
		TArray<UStaticMeshComponent*> Meshes;
		Cooler0->GetComponents<UStaticMeshComponent>(Meshes);
		const UStaticMeshComponent* Body = nullptr;
		const UStaticMeshComponent* Lid = nullptr;
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			const FString Name = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetName() : FString();
			Body = Name == TEXT("SM_Cooler_Starter") ? Mesh : Body;
			Lid = Name == TEXT("SM_Cooler_Starter_Lid") ? Mesh : Lid;
		}
		if (TestTrue(TEXT("QA precondition: client 0 draws the Starter row's real meshes, lid closed"), Body && Lid && Cooler0->GetLidPitch() == 0.0f))
		{
			const FBox BodyBox = Body->Bounds.GetBox();
			const FBox Whole = BodyBox + Lid->Bounds.GetBox();
			TestTrue(TEXT("QA precondition: the closed lid adds height"), Whole.Max.Z > BodyBox.Max.Z + 2.0);
			TestTrue(FString::Printf(TEXT("the dedicated server's box reaches the top of the closed lid (server top %.1f, lid top %.1f)"), ServerBox.Max.Z, Whole.Max.Z),
				ServerBox.Max.Z >= Whole.Max.Z - 1.0);
		}

		// The friend can use it as before: pick it up (F) with both fish, and put it down again (E).
		Net.LookAt(0, Cooler0->GetInteractionLocation());
		Worlds.TickAll(2);
		TestEqual(TEXT("the friend's F picks it up"), LCT::VerbName(Net.Keys(0)->ResolveInteraction(KeyF).Verb), LCT::VerbName(EVerb::PickUpCooler));
		TestTrue(TEXT("friend: F"), Net.Keys(0)->PressKey(KeyF));
		TestTrue(TEXT("the friend carries it (server and its machine)"), Net.Until([&]() { return Cooler->IsHeldBy(Friend, ELureHoldMode::Hand) && Cooler0->IsHeldBy(Net.Mine(0), ELureHoldMode::Hand); }));
		TestEqual(TEXT("... with both fish"), Cooler->GetNumFish(), 2);
		TestTrue(TEXT("friend: E puts it down"), Net.Keys(0)->PressKey(KeyE));
		TestTrue(TEXT("free again on both machines"), Net.Until([&]() { return Cooler->IsFree() && Cooler0->IsFree(); }));
		for (const FFishInstance& Fish : Inside)
		{
			Net.Settles(*this, Fish, 1, TEXT("after the friend carried it"));
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

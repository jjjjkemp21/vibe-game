// Lure T-030 tests (unreal-engineer): replication and server authority of catch handling with a real in-process server and
// two connected clients (UE::Net::FTestWorlds; the harness's server is dedicated, so both players are remote). Every
// machine gets the same dock and the shipped catch data. Rules: docs/specs/catch-handling-rules.md "Owner rules", "Network".
// Project.Catch.Net.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/FPArmsPose.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Tests/NetTestHelpers.h"

namespace LureCatchNetTest
{
	constexpr float NetDt = 1.0f / 60.0f;

	/** The shipped data every machine uses (text sources, never the binary assets) */
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
			const FLureCatchRow* Row = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("CatchNetTest"), false);
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

	/** A server with two connected players standing on the dock, and the helpers to find their copies on the clients */
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
					const ALurePlayerCharacter* Mine = On(Client, Players[Client]);
					const ALurePlayerCharacter* Theirs = On(Client, Players[1 - Client]);
					if (!Mine || !Theirs || !Mine->IsLocallyControlled() || !Mine->GetHands() || !Theirs->GetHands() || !ClientPC(Client) || !ClientPC(Client)->PlayerState)
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
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchNetHandOff, "Project.Catch.Net.HandOffAndCoolerReplicate", LCT::Flags)
bool FCatchNetHandOff::RunTest(const FString& Parameters)
{
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	LureCatchNetTest::FNet Net(Worlds);
	if (!Net.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Angler = Net.Players[0];
	ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(130.0f, 0.0f, LCT::DockTop)), Angler->GetPlayerState()); // in both players' reach
	if (!TestNotNull(TEXT("server cooler"), Cooler) || !TestTrue(TEXT("the cooler reaches both clients"), Net.Until([&]() { return Net.On(0, Cooler) && Net.On(1, Cooler); })))
	{
		return false;
	}
	ALureCoolerActor* Cooler0 = Net.On(0, Cooler);
	ALureCoolerActor* Cooler1 = Net.On(1, Cooler);
	TestTrue(TEXT("clients see its row and size (replicated, their own DT_Cooler for the name)"), Cooler1->GetCapacity() == 4 && Cooler1->GetItemName().ToString() == TEXT("Starter cooler"));
	TestTrue(TEXT("... and whose it is"), Cooler1->GetOwningPlayerState() == Net.On(1, Angler->GetPlayerState()));

	// Landing on the server: the fish hangs on the angler's hook on every machine.
	const FFishInstance Fish = LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 501);
	ALureFishItem* Item = Cast<ALureFishItem>(ULureCatchLibrary::HandleFishLanded(Angler, Fish).FishItem);
	if (!TestNotNull(TEXT("server fish item"), Item))
	{
		return false;
	}
	const bool bHangs = Net.Until([&]()
	{
		const ALureFishItem* Own = Net.On(0, Item);
		const ALureFishItem* Other = Net.On(1, Item);
		return Own && Other && Net.Mine(0)->GetHands()->GetHangingFish() == Own && Other->IsHeldBy(Net.On(1, Angler), ELureHoldMode::Hook);
	});
	if (!TestTrue(TEXT("the fish hangs on the angler's hook on both clients"), bHangs))
	{
		return false;
	}
	ALureFishItem* Item0 = Net.On(0, Item);
	ALureFishItem* Item1 = Net.On(1, Item);
	TestTrue(TEXT("the record replicates unchanged"), Item1->GetFish().Seed == 501 && Item1->GetFish().ToString() == Fish.ToString());
	TestEqual(TEXT("the angler can't cast on its machine (a fish hangs)"), LCT::BlockName(Net.Mine(0)->GetFishing()->GetCastBlock()), LCT::BlockName(ELureCastBlock::Busy));
	TestTrue(TEXT("the other player's copy of the angler knows the hanging fish"), Net.On(1, Angler)->GetHands()->GetHangingFish() == Item1);

	// The angler grabs it with E on its machine; the server does it, everyone sees it in the hand.
	TestTrue(TEXT("client 0: E"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("the server puts it in the angler's hand"), Net.Until([&]() { return Item->IsHeldBy(Angler, ELureHoldMode::Hand); }));
	TestTrue(TEXT("client 0 holds it"), Net.Until([&]() { return Net.Mine(0)->GetHands()->GetHeldFish() == Item0; }));
	TestTrue(TEXT("client 1 sees it in the angler's hand"), Net.Until([&]() { return Item1->IsHeldBy(Net.On(1, Angler), ELureHoldMode::Hand); }));
	Worlds.TickAll(5);
	TestEqual(TEXT("client 0: the arms hold the fish"), static_cast<int32>(Net.Mine(0)->GetArmsPose()), static_cast<int32>(EFPArmsPose::HoldFish));
	TestEqual(TEXT("client 0: the rod is stowed"), LCT::BlockName(Net.Mine(0)->GetFishing()->GetCastBlock()), LCT::BlockName(ELureCastBlock::NoRod));
	const USceneComponent* Parent0 = Item0->GetRootComponent()->GetAttachParent();
	const USceneComponent* Parent1 = Item1->GetRootComponent()->GetAttachParent();
	TestTrue(TEXT("client 0 draws it in its first-person hand"), Parent0 && Parent0->GetOwner() == Net.Mine(0) && Parent0 != Net.Mine(0)->GetRootComponent());
	TestTrue(TEXT("client 1 draws it at the angler's body"), Parent1 && Parent1 == Net.On(1, Angler)->GetRootComponent());
	TestEqual(TEXT("client 0's HUD"), FString::Join(ULureCatchLibrary::GetPlaceholderLines(Net.ClientPC(0)), TEXT(" | ")),
		FString(TEXT("Holding: Bonefish (Common), 1.50 kg, 45 coins, fresh 100%")));

	// Into the shared cooler (closed): the record goes in, the item goes away everywhere.
	Net.LookAt(0, Cooler0->GetInteractionLocation());
	Worlds.TickAll(2);
	TestEqual(TEXT("client 0: E would put it in"), LCT::VerbName(Net.Keys(0)->ResolveInteraction(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::PutFishInCooler));
	const TWeakObjectPtr<ALureFishItem> Weak0 = Item0;
	const TWeakObjectPtr<ALureFishItem> Weak1 = Item1;
	TestTrue(TEXT("client 0: E"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("the server stores it"), Net.Until([&]() { return Cooler->GetNumFish() == 1; }));
	TestTrue(TEXT("both clients see 1 fish in it and the item gone"), Net.Until([&]() { return Cooler0->GetNumFish() == 1 && Cooler1->GetNumFish() == 1 && !Weak0.IsValid() && !Weak1.IsValid(); }));
	FLureCaughtFish Stored;
	TestTrue(TEXT("client 1 knows what is inside"), Cooler1->GetStorage()->GetFishAt(0, Stored) && Stored.Fish.Seed == 501);
	TestFalse(TEXT("... the lid stayed closed"), Cooler1->IsLidOpen());

	// The other player opens the shared cooler and takes the fish out.
	Net.LookAt(1, Cooler1->GetInteractionLocation());
	Worlds.TickAll(2);
	TestTrue(TEXT("client 1: E opens"), Net.Keys(1)->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("open on every machine"), Net.Until([&]() { return Cooler->IsLidOpen() && Cooler0->IsLidOpen() && Cooler1->IsLidOpen(); }));
	TestEqual(TEXT("client 1: E takes it out"), LCT::VerbName(Net.Keys(1)->ResolveInteraction(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::TakeFishFromCooler));
	TestTrue(TEXT("client 1: E"), Net.Keys(1)->PressKey(ELureInteractKey::Primary));
	ALurePlayerCharacter* Friend = Net.Players[1];
	TestTrue(TEXT("the friend holds the fish (server)"), Net.Until([&]() { return LCT::HandsOf(Friend)->GetHeldFish() && LCT::HandsOf(Friend)->GetHeldFish()->GetFish().Seed == 501; }));
	TestTrue(TEXT("... on the friend's machine"), Net.Until([&]() { const ALureFishItem* Held = Net.Mine(1)->GetHands()->GetHeldFish(); return Held && Held->GetFish().Seed == 501; }));
	TestTrue(TEXT("... and the angler sees it in the friend's hand"), Net.Until([&]() { const ALureFishItem* Held = Net.On(0, Friend)->GetHands()->GetHeldFish(); return Held && Held->GetFish().Seed == 501; }));
	TestTrue(TEXT("the cooler is empty everywhere"), Net.Until([&]() { return Cooler0->GetNumFish() == 0 && Cooler1->GetNumFish() == 0; }));
	const ALureFishItem* Taken = LCT::HandsOf(Friend)->GetHeldFish();
	const ALureFishItem* Taken1 = Net.Mine(1)->GetHands()->GetHeldFish();
	TestTrue(FString::Printf(TEXT("the friend's machine shows the server's freshness (%.2f vs %.2f s)"), Taken1 ? Taken1->GetExposureSeconds() : -1.0f, Taken ? Taken->GetExposureSeconds() : -1.0f),
		Taken && Taken1 && FMath::IsNearlyEqual(Taken1->GetExposureSeconds(), Taken->GetExposureSeconds(), 0.25f));

	// The angler closes and carries the cooler: one carrier at a time; the friend can't take it or open it.
	Net.LookAt(0, Cooler0->GetInteractionLocation());
	Worlds.TickAll(2);
	TestTrue(TEXT("client 0: F closes"), Net.Keys(0)->PressKey(ELureInteractKey::Secondary));
	TestTrue(TEXT("closed"), Net.Until([&]() { return !Cooler0->IsLidOpen(); }));
	TestTrue(TEXT("client 0: F picks it up"), Net.Keys(0)->PressKey(ELureInteractKey::Secondary));
	TestTrue(TEXT("carried by the angler on every machine"), Net.Until([&]() { return Cooler->IsHeldBy(Angler, ELureHoldMode::Hand) && Cooler0->IsHeldBy(Net.Mine(0), ELureHoldMode::Hand)
		&& Cooler1->IsHeldBy(Net.On(1, Angler), ELureHoldMode::Hand); }));
	Worlds.TickAll(5);
	TestEqual(TEXT("client 0: the arms carry it"), static_cast<int32>(Net.Mine(0)->GetArmsPose()), static_cast<int32>(EFPArmsPose::CarryCooler));
	TestEqual(TEXT("client 0 predicts the slower walk from its own DT_Cooler"), Net.Mine(0)->GetHands()->GetMoveSpeedMultiplier(), Cooler->GetRow().CarrySpeedMultiplier, 1.0e-4f);
	TestEqual(TEXT("... the same as the server"), LCT::HandsOf(Angler)->GetMoveSpeedMultiplier(), Cooler->GetRow().CarrySpeedMultiplier, 1.0e-4f);
	TestEqual(TEXT("no collision while carried (client 1's copy)"), static_cast<int32>(Cooler1->GetCollisionBox()->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
	TestTrue(TEXT("the friend's machine offers nothing on a carried cooler"), !Cooler1->CanInteract(Net.Mine(1)) && Cooler1->GetInteraction(Net.Mine(1), ELureInteractKey::Secondary).IsEmpty());
	Net.Keys(1)->RequestInteract(Cooler1, ELureInteractKey::Secondary, ELureInteractVerb::PickUpCooler);
	Worlds.TickAll(10);
	TestTrue(TEXT("... and the server refuses the friend"), Cooler->IsHeldBy(Angler, ELureHoldMode::Hand));

	TestTrue(TEXT("client 0: E puts it down"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("free on every machine"), Net.Until([&]() { return Cooler->IsFree() && Cooler0->IsFree() && Cooler1->IsFree(); }));
	Worlds.TickAll(30);
	TestTrue(FString::Printf(TEXT("the clients place it where the server did (%s / %s)"), *Cooler1->GetActorLocation().ToCompactString(), *Cooler->GetActorLocation().ToCompactString()),
		Cooler1->GetActorLocation().Equals(Cooler->GetActorLocation(), 0.2) && Cooler0->GetActorLocation().Equals(Cooler->GetActorLocation(), 0.2));
	TestEqual(TEXT("... standing (collision on)"), static_cast<int32>(Cooler1->GetCollisionBox()->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::QueryAndPhysics));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchNetSellShared, "Project.Catch.Net.CounterPaysTheSeller", LCT::Flags)
bool FCatchNetSellShared::RunTest(const FString& Parameters)
{
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	LureCatchNetTest::FNet Net(Worlds);
	if (!Net.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Angler = Net.Players[0];
	ALurePlayerCharacter* Friend = Net.Players[1];
	ALureSellCounter* Counter = Net.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("counter"), Counter) || !TestTrue(TEXT("the counter reaches the clients"), Net.Until([&]() { return Net.On(0, Counter) && Net.On(1, Counter); })))
	{
		return false;
	}
	ALureSellCounter* Counter1 = Net.On(1, Counter);
	TestTrue(TEXT("its settings replicate once"), Counter1->MarketId == TEXT("Default") && Counter1->CounterHalfSize.Equals(Counter->CounterHalfSize));
	ULureProgressionComponent* AnglerProgress = LCT::ProgressionOf(Angler);
	ULureProgressionComponent* FriendProgress = LCT::ProgressionOf(Friend);
	const int32 AnglerXp0 = AnglerProgress->GetTotalXp();
	const int32 FriendXp0 = FriendProgress->GetTotalXp();
	const int32 AnglerMoney0 = AnglerProgress->GetMoney();
	const int32 FriendMoney0 = FriendProgress->GetMoney();

	ALureFishItem* Item = Cast<ALureFishItem>(ULureCatchLibrary::HandleFishLanded(Angler, LCT::MakeFish(TEXT("Bonefish"), 45, 9, 1.5f, 601)).FishItem);
	TestEqual(TEXT("XP goes to the angler at the landing"), AnglerProgress->GetTotalXp(), AnglerXp0 + 9);
	if (!TestNotNull(TEXT("fish"), Item) || !TestTrue(TEXT("hangs on client 0"), Net.Until([&]() { return Net.Mine(0)->GetHands()->GetHangingFish() != nullptr; })))
	{
		return false;
	}
	TestTrue(TEXT("client 0: E grabs"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("in hand"), Net.Until([&]() { return Net.Mine(0)->GetHands()->GetHeldFish() != nullptr; }));
	Net.LookAt(0, Net.On(0, Counter)->GetActorLocation());
	Worlds.TickAll(2);
	TestTrue(TEXT("client 0: E puts it on the counter"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("on the counter (server)"), Net.Until([&]() { return Item->GetCounter() == Counter && Item->IsFree(); }));
	TestTrue(TEXT("... and on client 1's machine (the reference resolves)"), Net.Until([&]() { const ALureFishItem* Copy = Net.On(1, Item); return Copy && Copy->GetCounter() == Counter1; }));

	// The friend sells what the angler caught: the seller gets the money; XP stays where it was.
	Net.LookAt(1, Counter1->GetActorLocation());
	Worlds.TickAll(2);
	const FLureResolvedInteraction Sell = Net.Keys(1)->ResolveInteraction(ELureInteractKey::Primary);
	TestTrue(TEXT("client 1 sees the sale prompt"), Sell.Verb == ELureInteractVerb::SellCounter && Sell.Prompt.ToString() == TEXT("Sell 1 fish (45 coins)"));
	TestTrue(TEXT("client 1: E"), Net.Keys(1)->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("the friend is paid"), Net.Until([&]() { return FriendProgress->GetMoney() == FriendMoney0 + 45; }));
	TestEqual(TEXT("the angler isn't"), AnglerProgress->GetMoney(), AnglerMoney0);
	TestTrue(TEXT("XP unchanged by selling"), AnglerProgress->GetTotalXp() == AnglerXp0 + 9 && FriendProgress->GetTotalXp() == FriendXp0);
	TestTrue(TEXT("the counter is empty on every machine"), Net.Until([&]() { return Counter->GetFishOnCounter().Num() == 0 && Net.On(0, Counter)->GetFishOnCounter().Num() == 0 && Counter1->GetFishOnCounter().Num() == 0; }));
	TestTrue(TEXT("the friend's machine shows the sale notice"), Net.Until([&]() { return LCT::NoticesOf(Net.Mine(1)).Contains(TEXT("Sold 1 fish for 45 coins")); }));
	TestFalse(TEXT("... the angler's doesn't"), LCT::NoticesOf(Net.Mine(0)).Contains(TEXT("Sold")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchNetNoCheats, "Project.Catch.Net.ClientsCannotCheat", LCT::Flags)
bool FCatchNetNoCheats::RunTest(const FString& Parameters)
{
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	LureCatchNetTest::FNet Net(Worlds);
	if (!Net.Create(*this))
	{
		return false;
	}
	AddExpectedMessagePlain(TEXT("server-only"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	ALurePlayerCharacter* Angler = Net.Players[0];
	ALurePlayerCharacter* Cheater = Net.Players[1];
	ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(130.0f, 100.0f, LCT::DockTop)), Angler->GetPlayerState());
	ALureCoolerActor* FarCooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FVector(-550.0f, -550.0f, LCT::DockTop)), nullptr);
	if (!TestNotNull(TEXT("coolers"), Cooler) || !FarCooler)
	{
		return false;
	}
	Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 701), FLureFreshness::GetServerTime(Net.Server)));
	Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 702), FLureFreshness::GetServerTime(Net.Server)));
	ALureFishItem* Hanging = Cast<ALureFishItem>(ULureCatchLibrary::HandleFishLanded(Angler, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, 703)).FishItem);
	const bool bThere = Net.Until([&]()
	{
		return Net.On(1, Cooler) && Net.On(1, FarCooler) && Net.On(1, Hanging) && Net.On(1, Cooler)->GetNumFish() == 2 && Net.On(1, Hanging)->IsHeldBy(Net.On(1, Angler), ELureHoldMode::Hook);
	});
	if (!TestTrue(TEXT("everything reached the cheater's machine"), bThere))
	{
		return false;
	}
	ALureCoolerActor* Cooler1 = Net.On(1, Cooler);
	ALureFishItem* Hanging1 = Net.On(1, Hanging);
	ULureInteractionComponent* Keys = Net.Keys(1);

	// Asking the server for things it wouldn't offer.
	Keys->RequestInteract(Cooler1, ELureInteractKey::Primary, ELureInteractVerb::TakeFishFromCooler); // closed: E opens
	Keys->RequestInteract(Cooler1, ELureInteractKey::Secondary, ELureInteractVerb::OpenCooler);         // the wrong key's verb
	Keys->RequestInteract(Hanging1, ELureInteractKey::Primary, ELureInteractVerb::GrabFish);            // someone else's hanging fish
	Keys->RequestInteract(Hanging1, ELureInteractKey::Secondary, ELureInteractVerb::ReleaseFish);
	Keys->RequestInteract(Net.On(1, FarCooler), ELureInteractKey::Primary, ELureInteractVerb::OpenCooler); // 8 m away
	// Calling server functions on its own copies.
	TestFalse(TEXT("client: the lid"), Cooler1->AuthoritySetLidOpen(true));
	TestFalse(TEXT("client: add a fish"), Cooler1->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 999, 1, 1.5f, 704), 0.0)));
	TestFalse(TEXT("client: take the hanging fish"), Net.Mine(1)->GetHands()->AuthorityTakeInHand(Hanging1));
	TestFalse(TEXT("client: pick up the cooler"), Cooler1->AuthorityPickUp(Net.Mine(1)));
	TestFalse(TEXT("client: land a fish"), ULureCatchLibrary::HandleFishLanded(Net.Mine(1), LCT::MakeFish(TEXT("Bonefish"), 999, 999, 1.5f, 705)).bAccepted);
	TestNull(TEXT("client: spawn a fish"), ALureFishItem::SpawnFish(Worlds.Clients[1].GetWorld(), FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 999, 1, 1.5f, 706), 0.0), FTransform::Identity));
	TestNull(TEXT("client: spawn a cooler"), ALureCoolerActor::SpawnCooler(Worlds.Clients[1].GetWorld(), NAME_None, FTransform::Identity, nullptr));
	Worlds.TickAll(30);

	TestTrue(TEXT("the server changed nothing: 2 fish, closed, not carried"), Cooler->GetNumFish() == 2 && !Cooler->IsLidOpen() && Cooler->IsFree());
	TestTrue(TEXT("... the far cooler is still closed"), !FarCooler->IsLidOpen());
	TestTrue(TEXT("... the angler's fish still hangs on the angler's hook"), IsValid(Hanging) && Hanging->IsHeldBy(Angler, ELureHoldMode::Hook));
	TestFalse(TEXT("... the cheater holds nothing"), LCT::HandsOf(Cheater)->IsHoldingSomething() || LCT::HandsOf(Cheater)->GetHangingFish());
	TestEqual(TEXT("... no extra XP"), LCT::ProgressionOf(Cheater)->GetTotalXp(), 0);
	TestTrue(TEXT("the cheater's own machine shows the truth again"), Net.Until([&]() { return Cooler1->GetNumFish() == 2 && !Cooler1->IsLidOpen(); }));
	TestTrue(TEXT("... and the angler's"), Net.On(0, Cooler) && Net.On(0, Cooler)->GetNumFish() == 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

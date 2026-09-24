// Lure T-030 independent QA (qa-engineer): catch handling in a game world (one server, one or two players), black-box from
// docs/specs/catch-handling-rules.md and the public headers. Riskiest first: a fish never exists twice or vanishes
// (Conservation.*), selling pays once to the presser for exactly the counter's fish (Sell.*), freshness rates come from the
// data (Freshness.*), put-down rules on every path (PutDown.*), save v2 and the starter cooler (Save.*), falling in and
// getting caught (Water.*, Caught.*).
// Two players in one world: A is local (keys through PressKey); B is a remote-style player whose requests go through
// TryInteract, the server path of ServerInteract (the verb its prompt showed; the server recomputes it).
// The whole file sits in namespace LureCatchQAWorld (unity builds: no file-scope using-directives).

#include "Tests/Catch/QACatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Progression/LureProgressionTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace LureCatchQAWorld
{
	constexpr float Z = LCT::DockTop;
	constexpr ELureInteractKey KeyE = ELureInteractKey::Primary;
	constexpr ELureInteractKey KeyF = ELureInteractKey::Secondary;
	using EVerb = ELureInteractVerb;

	/** The test dock (LCT::FWorld) with player A (local, at the origin, facing +X) and optionally B (remote-style, 120 cm to A's right... -Y) */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* A = nullptr;
		ALurePlayerCharacter* B = nullptr;

		bool Create(FAutomationTestBase& Test, bool bSecond, bool bQuickFreshness = false, bool bWater = false)
		{
			if (!W.Create(Test, bQuickFreshness) || (bWater && !Test.TestNotNull(TEXT("QA setup: water"), W.AddWater(400.0f))))
			{
				return false;
			}
			A = W.SpawnPlayer(Test, FVector(0.0f, 0.0f, Z));
			B = bSecond ? W.SpawnPlayer(Test, FVector(0.0f, -120.0f, Z), false) : nullptr;
			W.Tick(10);
			return A && (!bSecond || B);
		}

		UWorld* World() const { return W.World; }
	};

	FLureResolvedInteraction Resolve(ALurePlayerCharacter* Player, ELureInteractKey Key)
	{
		return LCT::InteractionOf(Player)->ResolveInteraction(Key);
	}

	/** The server path of Player's request: the target and the verb its prompt showed (ServerInteract -> TryInteract) */
	bool Send(ALurePlayerCharacter* Player, const FLureResolvedInteraction& Seen, ELureInteractKey Key)
	{
		return LCT::InteractionOf(Player)->TryInteract(Seen.Target, Key, Seen.Verb);
	}

	/** Resolves Key, checks the prompt's verb, presses it (a local player) */
	bool PressExpect(FAutomationTestBase& Test, ALurePlayerCharacter* Player, ELureInteractKey Key, EVerb Verb, const FString& What)
	{
		const FLureResolvedInteraction Seen = Resolve(Player, Key);
		Test.TestEqual(What + TEXT(": the prompt's verb"), LCT::VerbName(Seen.Verb), LCT::VerbName(Verb));
		return Seen.Verb == Verb && Test.TestTrue(What + TEXT(": pressed"), LCT::InteractionOf(Player)->PressKey(Key));
	}

	int32 Money(const ALurePlayerCharacter* Player) { return LCT::ProgressionOf(Player)->GetMoney(); }
	int32 Xp(const ALurePlayerCharacter* Player) { return LCT::ProgressionOf(Player)->GetTotalXp(); }
	FVector Mid(const AActor* Actor) { return Actor->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f); }
	FVector Sky(const ALurePlayerCharacter* Player) { return Player->GetPawnViewLocation() + FVector(0.0f, 0.0f, 1000.0f); }
	float Dist2D(const FVector& From, const FVector& To) { return static_cast<float>(FVector2D::Distance(FVector2D(From), FVector2D(To))); }
	bool OnFloor(const AActor* Actor, float FloorZ = Z) { return FMath::Abs(Actor->GetActorLocation().Z - FloorZ) < 1.5f; }

	/** Server setup shortcut: records straight into Cooler, landed at Now */
	void Fill(ALureCoolerActor* Cooler, const TArray<FFishInstance>& Fish, double Now)
	{
		for (const FFishInstance& One : Fish)
		{
			Cooler->GetStorage()->AddFish(LureCatchQA::Record(One, Now));
		}
	}

	/** A free fish lying on the dock at XY */
	ALureFishItem* Loose(UWorld* World, const FFishInstance& Fish, const FVector2D& XY, double Now)
	{
		return ALureFishItem::SpawnFish(World, LureCatchQA::Record(Fish, Now), FTransform(FVector(XY.X, XY.Y, Z)));
	}

	/** A sell counter with its area and market set before it registers */
	ALureSellCounter* SpawnCounter(UWorld* World, const FVector& Top, float Yaw, FName MarketId, const UDataTable* Markets, const FVector& HalfSize = FVector(30.0f, 230.0f, 20.0f))
	{
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Top);
		ALureSellCounter* Counter = World->SpawnActorDeferred<ALureSellCounter>(ALureSellCounter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Counter)
		{
			Counter->MarketId = MarketId;
			Counter->CounterHalfSize = HalfSize;
			Counter->SetMarketTable(Markets);
			Counter->FinishSpawning(Transform);
		}
		return Counter;
	}

	/** A player's hands are empty and nothing hangs on the hook */
	bool EmptyHanded(const ALurePlayerCharacter* Player)
	{
		const ULureHandsComponent* Hands = LCT::HandsOf(Player);
		return Hands && !Hands->IsHoldingSomething() && !Hands->GetHangingFish();
	}

	// =====================================================================================================================
	// 1. Conservation: a catch exists exactly once, from the hook to the sale
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAJourney, "Project.Catch.QA.Conservation.FullJourneyOneRecord", LCT::Flags)
	bool FCatchQAJourney::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		ALureCoolerActor* First = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		ALureCoolerActor* Second = Rig.W.SpawnCooler(FVector(-110.0f, 0.0f, Z), 0.0f);
		ALureSellCounter* Counter = Rig.W.SpawnCounter(FVector(0.0f, 160.0f, Z), 90.0f);
		const FFishInstance Fish = LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("Bonefish"), 4242);
		if (!TestTrue(TEXT("QA setup"), First && Second && Counter && Fish.IsValid()))
		{
			return false;
		}
		Rig.W.Tick(5);
		const int32 Money0 = Money(Rig.A);
		int32 StepIndex = 0;
		auto Press = [&](const AActor* Look, ELureInteractKey Key, EVerb Verb)
		{
			const FString Step = FString::Printf(TEXT("step %d (%s)"), ++StepIndex, *LCT::VerbName(Verb));
			LCT::LookAt(Rig.A, !Look ? Sky(Rig.A) : (Look == Counter ? Look->GetActorLocation() : Mid(Look)));
			PressExpect(*this, Rig.A, Key, Verb, Step);
			Rig.W.Tick(2);
			TestEqual(Step + TEXT(": the catch exists exactly once, unchanged"), LureCatchQA::CountCopies(World, Fish), Verb == EVerb::SellCounter ? 0 : 1);
		};
		TestNotNull(TEXT("landed: it hangs on the hook"), Rig.W.Land(Rig.A, Fish));
		TestEqual(TEXT("landed: exactly one copy"), LureCatchQA::CountCopies(World, Fish), 1);
		Press(nullptr, KeyE, EVerb::GrabFish);
		Press(First, KeyE, EVerb::PutFishInCooler);
		TestTrue(TEXT("put into a closed cooler: it stays closed and holds the fish"), !First->IsLidOpen() && First->GetNumFish() == 1);
		Press(First, KeyE, EVerb::OpenCooler);
		Press(First, KeyE, EVerb::TakeFishFromCooler);
		Press(Counter, KeyE, EVerb::PlaceFishOnCounter);
		TestEqual(TEXT("on the counter"), Counter->GetFishOnCounter().Num(), 1);
		Press(Counter, KeyF, EVerb::TakeFishFromCounter);
		Press(Second, KeyE, EVerb::PutFishInCooler);
		Press(Second, KeyE, EVerb::OpenCooler);
		Press(Second, KeyE, EVerb::TakeFishFromCooler);
		Press(Counter, KeyE, EVerb::PlaceFishOnCounter);
		const int32 Expected = LureCatchQA::OraclePrice(Fish.Value, 1.0, 1.0); // fresh (inside the 120 s grace), Default market x1
		TestEqual(TEXT("the counter quotes the rolled value"), Counter->QuoteAll(), Expected);
		Press(Counter, KeyE, EVerb::SellCounter);
		TestEqual(TEXT("sold: paid once, the rolled value"), Money(Rig.A) - Money0, Expected);
		TestTrue(TEXT("... with the sale notice"), LCT::NoticesOf(Rig.A).Contains(ULureProgressionComponent::FormatSaleNotice(1, Expected)));
		TestTrue(TEXT("... both coolers and the counter are empty"), First->GetNumFish() == 0 && Second->GetNumFish() == 0 && Counter->GetFishOnCounter().Num() == 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAFullCooler, "Project.Catch.QA.Conservation.FullCoolerKeepsFishInHand", LCT::Flags)
	bool FCatchQAFullCooler::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, -60.0f, Z), 180.0f);
		TArray<FFishInstance> Inside;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Inside.Add(LCT::MakeFish(TEXT("Bonefish"), 10 + Index, 1, 1.0f, 500 + Index));
		}
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		Fill(Cooler, Inside, Rig.W.Now());
		const FFishInstance Mine = LCT::MakeFish(TEXT("Bonefish"), 31, 1, 1.5f, 601);
		const FFishInstance Theirs = LCT::MakeFish(TEXT("CoralSnapper"), 32, 1, 2.0f, 602);
		ALureFishItem* MineItem = Rig.W.LandInHand(Rig.A, Mine);
		ALureFishItem* TheirsItem = Rig.W.LandInHand(Rig.B, Theirs);
		if (!TestTrue(TEXT("QA setup: 3/4 and a fish in each hand"), Cooler->GetNumFish() == 3 && MineItem && TheirsItem))
		{
			return false;
		}
		LCT::LookAt(Rig.A, Mid(Cooler));
		LCT::LookAt(Rig.B, Mid(Cooler));
		const FLureResolvedInteraction SeenA = Resolve(Rig.A, KeyE);
		const FLureResolvedInteraction SeenB = Resolve(Rig.B, KeyE);
		TestTrue(TEXT("both prompts say: put it in the cooler"), SeenA.Verb == EVerb::PutFishInCooler && SeenB.Verb == EVerb::PutFishInCooler && SeenA.Target.Get() == Cooler && SeenB.Target.Get() == Cooler);
		TestTrue(TEXT("B's fish goes in first: 4/4"), Send(Rig.B, SeenB, KeyE) && Cooler->GetNumFish() == 4);
		TestFalse(TEXT("A's stale 'put in' is refused (the cooler is full now)"), Send(Rig.A, SeenA, KeyE));
		TestFalse(TEXT("the server call refuses a full cooler too"), Cooler->AuthorityPutFishIn(Rig.A, MineItem));
		Rig.W.Tick(2);
		TestTrue(TEXT("A still holds its fish"), IsValid(MineItem) && LCT::HandsOf(Rig.A)->GetHeldFish() == MineItem && MineItem->IsHeldBy(Rig.A, ELureHoldMode::Hand));
		TestEqual(TEXT("A's fish exists once"), LureCatchQA::CountCopies(World, Mine), 1);
		TestEqual(TEXT("B's fish exists once"), LureCatchQA::CountCopies(World, Theirs), 1);
		TestTrue(TEXT("the cooler holds 4 with B's on top"), Cooler->GetNumFish() == 4 && FishQA::Same(Cooler->GetStorage()->GetFish().Last().Fish, Theirs));
		for (const FFishInstance& Fish : Inside)
		{
			TestEqual(TEXT("the fish already inside are untouched"), LureCatchQA::CountCopies(World, Fish), 1);
		}
		const FLureResolvedInteraction Full = Resolve(Rig.A, KeyE);
		TestTrue(FString::Printf(TEXT("A's E is now the info 'Cooler full (4/4)': '%s'"), *Full.Prompt.ToString()), Full.Verb == EVerb::None && Full.Prompt.ToString().Contains(TEXT("full")));
		TestEqual(TEXT("A's F still drops the fish"), LCT::VerbName(Resolve(Rig.A, KeyF).Verb), LCT::VerbName(EVerb::DropFish));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQARaceLastFish, "Project.Catch.QA.Conservation.RaceForLastFishInCooler", LCT::Flags)
	bool FCatchQARaceLastFish::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, -60.0f, Z), 180.0f);
		const FFishInstance Last = LCT::MakeFish(TEXT("CoralSnapper"), 44, 2, 2.2f, 707);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		Fill(Cooler, { Last }, Rig.W.Now());
		TestTrue(TEXT("QA setup: open, one fish"), Cooler->AuthoritySetLidOpen(true) && Cooler->GetNumFish() == 1);
		LCT::LookAt(Rig.A, Mid(Cooler));
		LCT::LookAt(Rig.B, Mid(Cooler));
		const FLureResolvedInteraction SeenA = Resolve(Rig.A, KeyE);
		const FLureResolvedInteraction SeenB = Resolve(Rig.B, KeyE);
		TestTrue(TEXT("both prompts say: take out the fish"), SeenA.Verb == EVerb::TakeFishFromCooler && SeenB.Verb == EVerb::TakeFishFromCooler);
		TestTrue(TEXT("A takes it"), Send(Rig.A, SeenA, KeyE));
		TestFalse(TEXT("B's stale take-out is refused"), Send(Rig.B, SeenB, KeyE));
		Rig.W.Tick(2);
		const ALureFishItem* Held = LCT::HandsOf(Rig.A)->GetHeldFish();
		TestTrue(TEXT("A holds exactly that catch"), Held && FishQA::Same(Held->GetCatch().Fish, Last));
		TestTrue(TEXT("B's hands stay empty"), EmptyHanded(Rig.B));
		TestEqual(TEXT("the fish exists once"), LureCatchQA::CountCopies(Rig.World(), Last), 1);
		TestEqual(TEXT("the cooler is empty"), Cooler->GetNumFish(), 0);
		TestEqual(TEXT("B's E is now 'Close' (open, empty)"), LCT::VerbName(Resolve(Rig.B, KeyE).Verb), LCT::VerbName(EVerb::CloseCooler));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQARaceLooseAndCooler, "Project.Catch.QA.Conservation.RaceForLooseFishAndCooler", LCT::Flags)
	bool FCatchQARaceLooseAndCooler::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const FFishInstance Fish = LCT::MakeFish(TEXT("Bonefish"), 27, 1, 1.4f, 808);
		ALureFishItem* Item = Loose(World, Fish, FVector2D(100.0f, -60.0f), Rig.W.Now());
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(-100.0f, -60.0f, Z), 0.0f);
		if (!TestTrue(TEXT("QA setup: a loose fish and a cooler"), Item && Cooler))
		{
			return false;
		}
		Rig.W.Tick(5);
		LCT::LookAt(Rig.A, Item->GetActorLocation());
		LCT::LookAt(Rig.B, Item->GetActorLocation());
		FLureResolvedInteraction SeenA = Resolve(Rig.A, KeyE);
		FLureResolvedInteraction SeenB = Resolve(Rig.B, KeyE);
		TestTrue(TEXT("both prompts say: grab the loose fish"), SeenA.Verb == EVerb::GrabFish && SeenB.Verb == EVerb::GrabFish && SeenA.Target.Get() == Item && SeenB.Target.Get() == Item);
		TestTrue(TEXT("A grabs it"), Send(Rig.A, SeenA, KeyE));
		TestFalse(TEXT("B's stale grab is refused"), Send(Rig.B, SeenB, KeyE));
		Rig.W.Tick(2);
		TestTrue(TEXT("A holds it, B holds nothing"), Item->IsHeldBy(Rig.A, ELureHoldMode::Hand) && EmptyHanded(Rig.B));
		TestFalse(TEXT("a fish in someone's hand offers B no grab"), Item->GetInteraction(Rig.B, KeyE).Verb == EVerb::GrabFish);
		TestEqual(TEXT("the fish exists once"), LureCatchQA::CountCopies(World, Fish), 1);

		LCT::LookAt(Rig.A, Mid(Cooler));
		PressExpect(*this, Rig.A, KeyE, EVerb::PutFishInCooler, TEXT("A puts it in the cooler"));
		Rig.W.Tick(2);
		LCT::LookAt(Rig.B, Mid(Cooler));
		SeenA = Resolve(Rig.A, KeyF);
		SeenB = Resolve(Rig.B, KeyF);
		TestTrue(TEXT("both prompts say: pick up the cooler"), SeenA.Verb == EVerb::PickUpCooler && SeenB.Verb == EVerb::PickUpCooler);
		TestTrue(TEXT("B picks it up"), Send(Rig.B, SeenB, KeyF));
		TestFalse(TEXT("A's stale pick-up is refused"), Send(Rig.A, SeenA, KeyF));
		Rig.W.Tick(2);
		TestTrue(TEXT("B carries it, A's hands are empty"), Cooler->IsHeldBy(Rig.B, ELureHoldMode::Hand) && LCT::HandsOf(Rig.B)->GetCarriedCooler() == Cooler && EmptyHanded(Rig.A));
		TestTrue(TEXT("a carried cooler offers A nothing"), !Cooler->GetInteraction(Rig.A, KeyE).HasVerb() && !Cooler->GetInteraction(Rig.A, KeyF).HasVerb());
		TestFalse(TEXT("... the server refuses A's pick-up"), Cooler->AuthorityPickUp(Rig.A));
		TestNull(TEXT("... and A's take-out"), Cooler->AuthorityTakeFishOut(Rig.A));
		TestEqual(TEXT("the fish exists once (in the carried cooler)"), LureCatchQA::CountCopies(World, Fish), 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAServerCalls, "Project.Catch.QA.Conservation.ServerCallsRefuseItemsNotHeld", LCT::Flags)
	bool FCatchQAServerCalls::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const FFishInstance X = LCT::MakeFish(TEXT("Bonefish"), 21, 1, 1.2f, 901);
		const FFishInstance Y = LCT::MakeFish(TEXT("CoralSnapper"), 22, 1, 2.1f, 902);
		const FFishInstance L = LCT::MakeFish(TEXT("Bonefish"), 23, 1, 1.3f, 903);
		const FFishInstance Kept = LCT::MakeFish(TEXT("Bonefish"), 24, 1, 1.4f, 904);
		ALureFishItem* XItem = Rig.W.LandInHand(Rig.A, X);
		ALureFishItem* YItem = Rig.W.LandInHand(Rig.B, Y);
		ALureFishItem* LItem = Loose(World, L, FVector2D(-80.0f, -60.0f), Rig.W.Now());
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, -60.0f, Z), 180.0f);
		ALureSellCounter* Counter = Rig.W.SpawnCounter(FVector(0.0f, 160.0f, Z), 90.0f);
		if (!TestTrue(TEXT("QA setup"), XItem && YItem && LItem && Cooler && Counter))
		{
			return false;
		}
		Fill(Cooler, { Kept }, Rig.W.Now());
		Cooler->AuthoritySetLidOpen(true);
		TestFalse(TEXT("A can't put B's held fish in a cooler"), Cooler->AuthorityPutFishIn(Rig.A, YItem));
		TestFalse(TEXT("A can't put a loose fish it doesn't hold in a cooler"), Cooler->AuthorityPutFishIn(Rig.A, LItem));
		TestFalse(TEXT("B can't put A's held fish in a cooler"), Cooler->AuthorityPutFishIn(Rig.B, XItem));
		TestNull(TEXT("A can't take a fish out with its hand full"), Cooler->AuthorityTakeFishOut(Rig.A));
		TestFalse(TEXT("A can't place B's held fish on the counter"), Counter->AuthorityPlaceFish(Rig.A, YItem));
		TestFalse(TEXT("A can't place a loose fish it doesn't hold on the counter"), Counter->AuthorityPlaceFish(Rig.A, LItem));
		TestTrue(TEXT("QA setup: B places its own fish"), Counter->AuthorityPlaceFish(Rig.B, YItem));
		TestFalse(TEXT("B (empty-handed now) can't take A's held fish"), LCT::HandsOf(Rig.B)->AuthorityTakeInHand(XItem));
		TestFalse(TEXT("A can't take the counter's fish back with its hand full"), Counter->AuthorityTakeBack(Rig.A));
		Rig.W.Tick(2);
		TestTrue(TEXT("X is still in A's hand"), XItem->IsHeldBy(Rig.A, ELureHoldMode::Hand) && LCT::HandsOf(Rig.A)->GetHeldFish() == XItem);
		TestTrue(TEXT("Y is on the counter"), YItem->IsFree() && YItem->GetCounter() == Counter && Counter->GetFishOnCounter().Num() == 1);
		TestTrue(TEXT("the loose fish still lies free, on no counter"), LItem->IsFree() && LItem->GetCounter() == nullptr);
		TestEqual(TEXT("the cooler still holds only its fish"), Cooler->GetNumFish(), 1);
		for (const FFishInstance* Fish : { &X, &Y, &L, &Kept })
		{
			TestEqual(FString::Printf(TEXT("seed %d exists once"), Fish->Seed), LureCatchQA::CountCopies(World, *Fish), 1);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQALeaveCarrying, "Project.Catch.QA.Conservation.PawnLeavesMidCarry", LCT::Flags)
	bool FCatchQALeaveCarrying::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		TArray<FFishInstance> Inside = { LCT::MakeFish(TEXT("Bonefish"), 11, 1, 1.1f, 1001), LCT::MakeFish(TEXT("CoralSnapper"), 12, 1, 2.0f, 1002), LCT::MakeFish(TEXT("Bonefish"), 13, 1, 1.3f, 1003) };
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		Fill(Cooler, Inside, Rig.W.Now());
		LCT::LookAt(Rig.A, Mid(Cooler));
		PressExpect(*this, Rig.A, KeyF, EVerb::PickUpCooler, TEXT("A picks up the cooler"));
		Rig.W.Tick(40); // the hands sample the last dry ground spot
		FVector Dry = FVector::ZeroVector;
		TestTrue(TEXT("QA precondition: carrying, a dry spot known"), LCT::HandsOf(Rig.A)->GetCarriedCooler() == Cooler && LCT::HandsOf(Rig.A)->GetLastDryGround(Dry));

		Rig.A->Destroy(); // leaving the game: the server removes the pawn
		Rig.A = nullptr;
		Rig.W.Tick(3);
		TestTrue(TEXT("the cooler is free again"), IsValid(Cooler) && Cooler->IsFree());
		TestTrue(FString::Printf(TEXT("... standing on the floor at the last dry spot (%s vs %s)"), *Cooler->GetActorLocation().ToCompactString(), *Dry.ToCompactString()),
			OnFloor(Cooler) && Dist2D(Cooler->GetActorLocation(), Dry) < 5.0f);
		TestTrue(TEXT("... with its 3 fish, lid closed"), Cooler->GetNumFish() == 3 && !Cooler->IsLidOpen());
		for (const FFishInstance& Fish : Inside)
		{
			TestEqual(TEXT("each fish exists once"), LureCatchQA::CountCopies(World, Fish), 1);
		}
		LCT::LookAt(Rig.B, Mid(Cooler));
		const FLureResolvedInteraction Seen = Resolve(Rig.B, KeyF);
		TestTrue(TEXT("another player can pick it up"), Seen.Verb == EVerb::PickUpCooler && Send(Rig.B, Seen, KeyF) && LCT::HandsOf(Rig.B)->GetCarriedCooler() == Cooler);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQALeaveWithFish, "Project.Catch.QA.Conservation.PawnLeavesWithFishInHandAndOnHook", LCT::Flags)
	bool FCatchQALeaveWithFish::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const FFishInstance InHand = LCT::MakeFish(TEXT("Bonefish"), 15, 1, 1.5f, 1101);
		const FFishInstance OnHook = LCT::MakeFish(TEXT("CoralSnapper"), 16, 1, 2.5f, 1102);
		ALureFishItem* HandItem = Rig.W.LandInHand(Rig.A, InHand);
		ALureFishItem* HookItem = Rig.W.Land(Rig.A, OnHook);
		if (!TestTrue(TEXT("QA setup: a fish in the hand and one waiting on the hook"), HandItem && HookItem && LCT::HandsOf(Rig.A)->GetHangingFish() == HookItem))
		{
			return false;
		}
		Rig.W.Tick(40);
		FVector Dry = FVector::ZeroVector;
		TestTrue(TEXT("QA precondition: a dry spot known"), LCT::HandsOf(Rig.A)->GetLastDryGround(Dry));
		Rig.A->Destroy();
		Rig.A = nullptr;
		Rig.W.Tick(3);
		for (ALureFishItem* Item : { HandItem, HookItem })
		{
			const bool bThere = IsValid(Item) && Item->IsFree();
			TestTrue(TEXT("the fish is not lost: it lies free"), bThere);
			if (bThere)
			{
				const FVector At = Item->GetActorLocation();
				TestTrue(FString::Printf(TEXT("... at the last dry spot (%s vs %s)"), *At.ToCompactString(), *Dry.ToCompactString()), Dist2D(At, Dry) < 60.0f && At.Z > Z - 1.0f && At.Z < Z + 30.0f);
			}
		}
		TestEqual(TEXT("the hand fish exists once"), LureCatchQA::CountCopies(World, InHand), 1);
		TestEqual(TEXT("the hook fish exists once"), LureCatchQA::CountCopies(World, OnHook), 1);
		LCT::LookAt(Rig.B, Dry);
		const FLureResolvedInteraction Seen = Resolve(Rig.B, KeyE);
		TestTrue(TEXT("another player can grab one"), Seen.Verb == EVerb::GrabFish && Send(Rig.B, Seen, KeyE) && LCT::HandsOf(Rig.B)->GetHeldFish() != nullptr);
		return true;
	}

	// =====================================================================================================================
	// 2. Selling: once, to the presser, exactly the counter's fish, at the price now
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASellOnce, "Project.Catch.QA.Sell.PaysOnceToThePresserForRealCatches", LCT::Flags)
	bool FCatchQASellOnce::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const TStrongObjectPtr<UDataTable> Markets = LCT::Shipped(*this, FFishMarketRow::StaticStruct(), TEXT("DT_FishMarket.csv"));
		const FFishMarketRow* Palm = Markets.IsValid() ? Markets->FindRow<FFishMarketRow>(TEXT("PalmKeyDock"), TEXT("QA"), false) : nullptr;
		ALureSellCounter* Counter = SpawnCounter(World, FVector(0.0f, 160.0f, Z), 90.0f, TEXT("PalmKeyDock"), Markets.Get());
		if (!TestTrue(TEXT("QA setup: the Palm Key counter with the shipped market"), Palm && Counter))
		{
			return false;
		}
		LCT::PlaceAt(Rig.B, FVector(80.0f, 0.0f, Z), 90.0f);
		const TArray<FFishInstance> Catches = { LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("Bonefish"), 101), LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("CoralSnapper"), 202),
			LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("Bonefish"), 303) };
		int32 Oracle = 0;
		for (const FFishInstance& Fish : Catches)
		{
			Rig.W.LandInHand(Rig.A, Fish);
			LCT::LookAt(Rig.A, Counter->GetActorLocation());
			PressExpect(*this, Rig.A, KeyE, EVerb::PlaceFishOnCounter, TEXT("the angler puts a catch on the counter"));
			Rig.W.Tick(2);
			Oracle += LureCatchQA::OraclePrice(Fish.Value, 1.0, Palm->SellMultiplier); // fresh: well inside the 120 s grace
		}
		TestEqual(TEXT("the quote = the spec price of the 3 real catches"), Counter->QuoteAll(), Oracle);
		LCT::LookAt(Rig.A, Counter->GetActorLocation());
		LCT::LookAt(Rig.B, Counter->GetActorLocation());
		const FLureResolvedInteraction SeenA = Resolve(Rig.A, KeyE);
		const FLureResolvedInteraction SeenB = Resolve(Rig.B, KeyE);
		TestTrue(TEXT("both prompts say: sell"), SeenA.Verb == EVerb::SellCounter && SeenB.Verb == EVerb::SellCounter);
		const int32 MoneyA = Money(Rig.A);
		const int32 MoneyB = Money(Rig.B);
		const int32 XpA = Xp(Rig.A);
		const int32 XpB = Xp(Rig.B);
		TestTrue(TEXT("B (not the angler) sells"), Send(Rig.B, SeenB, KeyE));
		TestFalse(TEXT("A's stale sell is refused"), Send(Rig.A, SeenA, KeyE));
		Rig.W.Tick(2);
		TestEqual(TEXT("the presser is paid exactly once"), Money(Rig.B) - MoneyB, Oracle);
		TestEqual(TEXT("the angler is paid nothing"), Money(Rig.A) - MoneyA, 0);
		TestTrue(TEXT("no XP from selling"), Xp(Rig.A) == XpA && Xp(Rig.B) == XpB);
		for (const FFishInstance& Fish : Catches)
		{
			TestEqual(TEXT("each sold fish is gone"), LureCatchQA::CountCopies(World, Fish), 0);
		}
		TestTrue(TEXT("the counter is empty"), Counter->GetFishOnCounter().Num() == 0 && Counter->QuoteAll() == 0);
		const FLureResolvedInteraction After = Resolve(Rig.B, KeyE);
		TestTrue(FString::Printf(TEXT("an empty counter offers no sale: '%s'"), *After.Prompt.ToString()), After.Verb != EVerb::SellCounter);
		TestFalse(TEXT("a second sell sends nothing"), Send(Rig.B, SeenB, KeyE));
		TestEqual(TEXT("... and pays nothing"), Money(Rig.B) - MoneyB, Oracle);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASellOnlyThis, "Project.Catch.QA.Sell.OnlyThisCountersFish", LCT::Flags)
	bool FCatchQASellOnlyThis::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		ALureSellCounter* Here = Rig.W.SpawnCounter(FVector(0.0f, 160.0f, Z), 90.0f);
		ALureSellCounter* There = Rig.W.SpawnCounter(FVector(0.0f, -420.0f, Z), 90.0f);
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(150.0f, 60.0f, Z), 180.0f);
		const FFishInstance Sold = LCT::MakeFish(TEXT("Bonefish"), 40, 1, 1.5f, 1201);
		const FFishInstance OtherCounter = LCT::MakeFish(TEXT("Bonefish"), 41, 1, 1.5f, 1202);
		const FFishInstance Floor = LCT::MakeFish(TEXT("Bonefish"), 42, 1, 1.5f, 1203);
		const FFishInstance InCooler = LCT::MakeFish(TEXT("Bonefish"), 43, 1, 1.5f, 1204);
		const FFishInstance InHand = LCT::MakeFish(TEXT("Bonefish"), 44, 1, 1.5f, 1205);
		if (!TestTrue(TEXT("QA setup"), Here && There && Cooler))
		{
			return false;
		}
		Rig.W.LandInHand(Rig.A, Sold);
		LCT::LookAt(Rig.A, Here->GetActorLocation());
		PressExpect(*this, Rig.A, KeyE, EVerb::PlaceFishOnCounter, TEXT("A's fish on this counter"));
		LCT::PlaceAt(Rig.B, FVector(0.0f, -300.0f, Z), 0.0f);
		Rig.W.Tick(2);
		TestTrue(TEXT("QA setup: B's fish on the other counter"), There->AuthorityPlaceFish(Rig.B, Rig.W.LandInHand(Rig.B, OtherCounter)));
		Rig.W.LandInHand(Rig.B, InHand);
		Loose(World, Floor, FVector2D(0.0f, 235.0f), Rig.W.Now()); // 45 cm past this counter's area
		Fill(Cooler, { InCooler }, Rig.W.Now());
		Rig.W.Tick(2);
		TestTrue(TEXT("QA precondition: the loose fish is off the counter"), !Here->ContainsPoint(FVector(0.0f, 235.0f, Z)));
		const int32 Money0 = Money(Rig.A);
		PressExpect(*this, Rig.A, KeyE, EVerb::SellCounter, TEXT("A sells at this counter"));
		Rig.W.Tick(2);
		TestEqual(TEXT("paid for this counter's one fish only"), Money(Rig.A) - Money0, LureCatchQA::OraclePrice(Sold.Value, 1.0, 1.0));
		TestEqual(TEXT("this counter's fish is sold"), LureCatchQA::CountCopies(World, Sold), 0);
		TestTrue(TEXT("the other counter keeps its fish"), LureCatchQA::CountCopies(World, OtherCounter) == 1 && There->GetFishOnCounter().Num() == 1);
		TestEqual(TEXT("the fish on the floor next to it stays"), LureCatchQA::CountCopies(World, Floor), 1);
		TestEqual(TEXT("the fish in the cooler stays"), LureCatchQA::CountCopies(World, InCooler), 1);
		TestTrue(TEXT("the fish in B's hand stays there"), LureCatchQA::CountCopies(World, InHand) == 1 && LCT::HandsOf(Rig.B)->GetHeldFish() != nullptr);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASellSpoiling, "Project.Catch.QA.Sell.CounterFishKeepSpoiling", LCT::Flags)
	bool FCatchQASellSpoiling::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false, true)) // quick freshness: grace 1 s, spoil 2 s, exponent 1, min 0.25
		{
			return false;
		}
		ALureSellCounter* Counter = Rig.W.SpawnCounter(FVector(0.0f, 160.0f, Z), 90.0f);
		if (!TestNotNull(TEXT("QA setup: counter"), Counter))
		{
			return false;
		}
		const double Landed = Rig.W.Now();
		Rig.W.LandInHand(Rig.A, LCT::MakeFish(TEXT("Bonefish"), 100, 1, 1.5f, 1301));
		Rig.W.Advance(0.5f); // in the hand first
		LCT::LookAt(Rig.A, Counter->GetActorLocation());
		PressExpect(*this, Rig.A, KeyE, EVerb::PlaceFishOnCounter, TEXT("on the counter"));
		Rig.W.Advance(1.5f);
		const TArray<ALureFishItem*> OnIt = Counter->GetFishOnCounter();
		if (!TestEqual(TEXT("QA precondition: one fish on the counter"), OnIt.Num(), 1))
		{
			return false;
		}
		const float Exposure = OnIt[0]->GetExposureSeconds();
		TestEqual(TEXT("exposure runs on from the hand at rate 1 on the counter (no reset, no pause)"), Exposure, static_cast<float>(Rig.W.Now() - Landed), 0.05f);
		const double Share = LureCatchQA::OracleShare(1.0, 2.0, 1.0, 0.25, Exposure);
		TestEqual(TEXT("its value share follows the curve"), OnIt[0]->GetValueShare(), static_cast<float>(Share), 1.0e-3f);
		const int32 Quote1 = Counter->QuoteAll();
		TestTrue(FString::Printf(TEXT("the quote is the spec price now (%d vs %d)"), Quote1, LureCatchQA::OraclePrice(100, Share, 1.0)), FMath::Abs(Quote1 - LureCatchQA::OraclePrice(100, Share, 1.0)) <= 1);
		Rig.W.Advance(0.5f);
		const int32 Quote2 = Counter->QuoteAll();
		TestTrue(FString::Printf(TEXT("still spoiling on the counter: %d < %d"), Quote2, Quote1), Quote2 < Quote1);
		const int32 Money0 = Money(Rig.A);
		PressExpect(*this, Rig.A, KeyE, EVerb::SellCounter, TEXT("sell"));
		TestEqual(TEXT("paid the price of that moment"), Money(Rig.A) - Money0, Quote2);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASellMarkets, "Project.Catch.QA.Sell.MarketFallbacksAndMoneySaturates", LCT::Flags)
	bool FCatchQASellMarkets::RunTest(const FString& Parameters)
	{
		AddExpectedMessagePlain(TEXT("Nope"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		AddExpectedMessagePlain(TEXT("Broken"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		AddExpectedMessagePlain(TEXT("Zero"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const TStrongObjectPtr<UDataTable> Fixture = LCT::MakeTable(FFishMarketRow::StaticStruct(),
			TEXT("Name,DisplayName,SellMultiplier,DevComment\nDefault,Default,1.0,\nBroken,Broken,-2.0,\nZero,Zero,0,\n"), false);
		const TStrongObjectPtr<UDataTable> Shipped = LCT::Shipped(*this, FFishMarketRow::StaticStruct(), TEXT("DT_FishMarket.csv"));
		const FFishMarketRow* Palm = Shipped.IsValid() ? Shipped->FindRow<FFishMarketRow>(TEXT("PalmKeyDock"), TEXT("QA"), false) : nullptr;
		ALureSellCounter* Unknown = SpawnCounter(World, FVector(-400.0f, 400.0f, Z), 0.0f, TEXT("Nope"), Fixture.Get());
		ALureSellCounter* Broken = SpawnCounter(World, FVector(-400.0f, -400.0f, Z), 0.0f, TEXT("Broken"), Fixture.Get());
		ALureSellCounter* ZeroMarket = SpawnCounter(World, FVector(400.0f, -400.0f, Z), 0.0f, TEXT("Zero"), Fixture.Get());
		ALureSellCounter* PalmKey = SpawnCounter(World, FVector(400.0f, 400.0f, Z), 0.0f, TEXT("PalmKeyDock"), Shipped.Get());
		ALureSellCounter* Here = SpawnCounter(World, FVector(0.0f, 160.0f, Z), 90.0f, TEXT("Default"), Fixture.Get());
		if (!TestTrue(TEXT("QA setup"), Unknown && Broken && ZeroMarket && PalmKey && Here && Palm))
		{
			return false;
		}
		TestEqual(TEXT("an unknown market row pays x1"), Unknown->GetSellMultiplier(), 1.0f);
		TestEqual(TEXT("a negative multiplier pays x1"), Broken->GetSellMultiplier(), 1.0f);
		TestEqual(TEXT("a zero multiplier pays x1"), ZeroMarket->GetSellMultiplier(), 1.0f);
		TestEqual(TEXT("Palm Key pays its shipped row"), PalmKey->GetSellMultiplier(), Palm->SellMultiplier);

		ULureProgressionComponent* Progression = LCT::ProgressionOf(Rig.A);
		Progression->AddMoney(MAX_int32 - 5);
		const FFishInstance Fish = LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 1401);
		Rig.W.LandInHand(Rig.A, Fish);
		LCT::LookAt(Rig.A, Here->GetActorLocation());
		PressExpect(*this, Rig.A, KeyE, EVerb::PlaceFishOnCounter, TEXT("place"));
		PressExpect(*this, Rig.A, KeyE, EVerb::SellCounter, TEXT("sell with nearly max money"));
		TestEqual(TEXT("money saturates at the maximum"), Progression->GetMoney(), MAX_int32);
		TestEqual(TEXT("... and the fish is sold (not kept, not lost twice)"), LureCatchQA::CountCopies(World, Fish), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASellRange, "Project.Catch.QA.Sell.ServerRangeAndNoPlayerState", LCT::Flags)
	bool FCatchQASellRange::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		ALureSellCounter* Counter = Rig.W.SpawnCounter(FVector(0.0f, 200.0f, Z), 90.0f);
		const FFishInstance First = LCT::MakeFish(TEXT("Bonefish"), 50, 1, 1.5f, 1501);
		const FFishInstance Second = LCT::MakeFish(TEXT("Bonefish"), 60, 1, 1.5f, 1502);
		if (!TestNotNull(TEXT("QA setup: counter"), Counter))
		{
			return false;
		}
		auto PlaceOne = [&](const FFishInstance& Fish)
		{
			LCT::PlaceAt(Rig.A, FVector(0.0f, 0.0f, Z), 90.0f);
			Rig.W.Tick(2);
			Rig.W.LandInHand(Rig.A, Fish);
			LCT::LookAt(Rig.A, Counter->GetActorLocation());
			PressExpect(*this, Rig.A, KeyE, EVerb::PlaceFishOnCounter, TEXT("place"));
		};
		const float Dz = static_cast<float>(Rig.A->GetActorLocation().Z - Counter->GetActorLocation().Z);
		auto StandAt = [&](float Distance3D)
		{
			const float Horizontal = FMath::Sqrt(FMath::Max(0.0f, Distance3D * Distance3D - Dz * Dz));
			LCT::PlaceAt(Rig.A, FVector(0.0f, 200.0f - Horizontal, Z), 90.0f);
			Rig.W.Tick(2);
		};
		const float Limit = Counter->InteractionRadius + ILureInteractable::ServerRangeSlack;
		PlaceOne(First);
		const int32 Money0 = Money(Rig.A);
		StandAt(Limit + 10.0f);
		FLureSaleResult Sale = Counter->AuthoritySell(Rig.A);
		TestTrue(TEXT("10 cm beyond radius + slack: nothing sold"), Sale.FishSold == 0 && Money(Rig.A) == Money0 && LureCatchQA::CountCopies(World, First) == 1);
		StandAt(Limit - 10.0f);
		Sale = Counter->AuthoritySell(Rig.A);
		TestTrue(TEXT("10 cm inside radius + slack: sold"), Sale.FishSold == 1 && Money(Rig.A) - Money0 == 50 && LureCatchQA::CountCopies(World, First) == 0);

		PlaceOne(Second);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALurePlayerCharacter* Stranger = World->SpawnActor<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), FTransform(FVector(80.0f, 60.0f, Z + 95.0f)), Params);
		if (TestNotNull(TEXT("QA setup: a pawn without a player state"), Stranger))
		{
			Sale = Counter->AuthoritySell(Stranger);
			TestTrue(TEXT("a pawn without a player state sells nothing"), Sale.FishSold == 0 && Sale.MoneyEarned == 0 && LureCatchQA::CountCopies(World, Second) == 1);
		}
		return true;
	}

	// =====================================================================================================================
	// 3. Freshness in the world: rates from the data
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQACoolerRates, "Project.Catch.QA.Freshness.CoolerRatesComeFromTheRow", LCT::Flags)
	bool FCatchQACoolerRates::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		// Not the shipped 1 / 0: a hard-coded "closed = 0" or "open = 1" would pass with those.
		const TStrongObjectPtr<UDataTable> Rows = LCT::MakeTableChecked(*this, FCoolerRow::StaticStruct(),
			TEXT("Name,DisplayName,Slots,BodyMesh,LidMesh,OpenDecayRate,ClosedDecayRate,CarrySpeedMultiplier,DevComment\nStarter,QA cooler,4,,,0.5,0.25,0.8,\n"), false, TEXT("QA coolers"));
		ULureCatchSubsystem::Get(World)->SetCoolerTable(Rows.Get());
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		ALureFishItem* Item = Rig.W.LandInHand(Rig.A, LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 1601));
		if (!TestTrue(TEXT("QA setup"), Rows.IsValid() && Cooler && Item))
		{
			return false;
		}
		double Expected = 0.0;
		double Last = Rig.W.Now();
		float Rate = 1.0f;
		auto Mark = [&](float NewRate)
		{
			const double Now = Rig.W.Now();
			Expected += Rate * (Now - Last);
			Last = Now;
			Rate = NewRate;
		};
		auto ExpectedNow = [&]() { return static_cast<float>(Expected + Rate * (Rig.W.Now() - Last)); };
		auto InsideNow = [&]() { return Cooler->GetStorage()->GetFish()[0].Freshness.GetExposure(FLureFreshness::GetServerTime(World)); };
		Rig.W.Advance(1.0f);
		LCT::LookAt(Rig.A, Mid(Cooler));
		PressExpect(*this, Rig.A, KeyE, EVerb::PutFishInCooler, TEXT("into the closed cooler"));
		Mark(0.25f);
		TestEqual(TEXT("closed: the row's ClosedDecayRate 0.25"), Cooler->GetDecayRate(), 0.25f);
		Rig.W.Advance(2.0f);
		TestEqual(TEXT("closed for 2 s adds 0.5 s"), InsideNow(), ExpectedNow(), 0.02f);
		PressExpect(*this, Rig.A, KeyE, EVerb::OpenCooler, TEXT("open"));
		Mark(0.5f);
		TestEqual(TEXT("open: the row's OpenDecayRate 0.5"), Cooler->GetDecayRate(), 0.5f);
		Rig.W.Advance(2.0f);
		TestEqual(TEXT("open for 2 s adds 1 s"), InsideNow(), ExpectedNow(), 0.02f);
		PressExpect(*this, Rig.A, KeyF, EVerb::CloseCooler, TEXT("close"));
		Mark(0.25f);
		Rig.W.Advance(2.0f);
		TestEqual(TEXT("closed again for 2 s adds 0.5 s"), InsideNow(), ExpectedNow(), 0.02f);
		PressExpect(*this, Rig.A, KeyE, EVerb::OpenCooler, TEXT("open again"));
		Mark(0.5f);
		PressExpect(*this, Rig.A, KeyE, EVerb::TakeFishFromCooler, TEXT("take it out"));
		Mark(1.0f);
		Rig.W.Advance(1.0f);
		const ALureFishItem* Out = LCT::HandsOf(Rig.A)->GetHeldFish();
		TestTrue(FString::Printf(TEXT("out of the cooler it spoils at 1 again, total %.3f s (expected %.3f)"), Out ? Out->GetExposureSeconds() : -1.0f, ExpectedNow()),
			Out && FMath::IsNearlyEqual(Out->GetExposureSeconds(), ExpectedNow(), 0.02f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASpeciesRow, "Project.Catch.QA.Freshness.SpeciesRowInTheWorld", LCT::Flags)
	bool FCatchQASpeciesRow::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const TStrongObjectPtr<UDataTable> Rows = LCT::MakeTableChecked(*this, FLureFreshnessRow::StaticStruct(),
			TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\nDefault,1,2,1.0,0.25,\nCoralSnapper,0,4,2.0,0.5,\n"), false, TEXT("QA freshness"));
		if (!Rows.IsValid())
		{
			return false;
		}
		ULureCatchSubsystem::Get(World)->SetFreshnessTable(Rows.Get());
		const double Now = Rig.W.Now();
		ALureFishItem* Bone = Loose(World, LCT::MakeFish(TEXT("Bonefish"), 80, 1, 1.5f, 1701), FVector2D(100.0f, 100.0f), Now);
		ALureFishItem* Snapper = Loose(World, LCT::MakeFish(TEXT("CoralSnapper"), 80, 1, 2.5f, 1702), FVector2D(100.0f, -100.0f), Now);
		if (!TestTrue(TEXT("QA setup: two loose fish landed now"), Bone && Snapper))
		{
			return false;
		}
		Rig.W.Advance(2.0f);
		struct FCaseRow { ALureFishItem* Item; double Grace, Spoil, Exponent, Min; const TCHAR* What; };
		const FCaseRow Cases[] = { { Bone, 1.0, 2.0, 1.0, 0.25, TEXT("Bonefish uses Default") }, { Snapper, 0.0, 4.0, 2.0, 0.5, TEXT("CoralSnapper uses its own row") } };
		for (const FCaseRow& Case : Cases)
		{
			const float Exposure = Case.Item->GetExposureSeconds();
			TestEqual(FString::Printf(TEXT("%s: exposure = time since landing"), Case.What), Exposure, static_cast<float>(Rig.W.Now() - Now), 0.05f);
			const double Share = LureCatchQA::OracleShare(Case.Grace, Case.Spoil, Case.Exponent, Case.Min, Exposure);
			TestEqual(FString::Printf(TEXT("%s: freshness"), Case.What), Case.Item->GetFreshness01(), static_cast<float>(LureCatchQA::OracleFreshness(Case.Grace, Case.Spoil, Case.Exponent, Exposure)), 1.0e-3f);
			TestEqual(FString::Printf(TEXT("%s: value share"), Case.What), Case.Item->GetValueShare(), static_cast<float>(Share), 1.0e-3f);
			TestTrue(FString::Printf(TEXT("%s: current value %d = 80 x share (%d)"), Case.What, Case.Item->GetCurrentValue(), LureCatchQA::OraclePrice(80, Share, 1.0)),
				FMath::Abs(Case.Item->GetCurrentValue() - LureCatchQA::OraclePrice(80, Share, 1.0)) <= 1);
		}
		TestTrue(TEXT("same exposure, different rows: the snapper is fresher here"), Snapper->GetFreshness01() > Bone->GetFreshness01() + 0.1f);
		return true;
	}

	// =====================================================================================================================
	// 4. Put-down: the floor at your level, never a counter area, the latch toward you on every path, the lid in the box
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAPutDownYaws, "Project.Catch.QA.PutDown.FacesPlayerAtEveryYaw", LCT::Flags)
	bool FCatchQAPutDownYaws::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		const float Distance = ULureCatchSubsystem::GetTuningFor(Rig.World()).PutDownDistance;
		LCT::LookAt(Rig.A, Mid(Cooler));
		PressExpect(*this, Rig.A, KeyF, EVerb::PickUpCooler, TEXT("pick up"));
		for (int32 Yaw = 0; Yaw < 360; Yaw += 45)
		{
			const FString What = FString::Printf(TEXT("yaw %d"), Yaw);
			LCT::PlaceAt(Rig.A, FVector(0.0f, 0.0f, Z), static_cast<float>(Yaw));
			Rig.W.Tick(3);
			if (!PressExpect(*this, Rig.A, KeyF, EVerb::PutDownCooler, What + TEXT(": put down")))
			{
				return false;
			}
			Rig.W.Tick(2);
			TestTrue(What + TEXT(": free, on the floor"), Cooler->IsFree() && OnFloor(Cooler));
			TestTrue(FString::Printf(TEXT("%s: the latch faces the player (cos %.3f)"), *What, LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation())),
				LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation()) > 0.99f);
			TestEqual(What + TEXT(": PutDownDistance in front"), Dist2D(Cooler->GetActorLocation(), Rig.A->GetActorLocation()), Distance, 3.0f);
			LCT::LookAt(Rig.A, Mid(Cooler));
			PressExpect(*this, Rig.A, KeyF, EVerb::PickUpCooler, What + TEXT(": pick up again"));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAPutDownProne, "Project.Catch.QA.PutDown.ProneFacesPlayer", LCT::Flags)
	bool FCatchQAPutDownProne::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		auto CheckFacing = [&](const FString& What)
		{
			const FVector Player = Rig.A->GetActorLocation();
			if (Dist2D(Cooler->GetActorLocation(), Player) > 20.0f)
			{
				TestTrue(FString::Printf(TEXT("%s: the latch faces the player (cos %.3f)"), *What, LureCatchQA::FrontFacing(Cooler, Player)), LureCatchQA::FrontFacing(Cooler, Player) > 0.98f);
			}
			else
			{
				TestEqual(What + TEXT(": on the player's spot it faces their yaw + 180"), FRotator3f::NormalizeAxis(static_cast<float>(Cooler->GetActorRotation().Yaw - Rig.A->GetActorRotation().Yaw - 180.0)), 0.0f, 1.0f);
			}
		};
		// (a) Open floor: the normal put-down in front.
		LCT::PlaceAt(Rig.A, FVector(0.0f, 0.0f, Z), 30.0f);
		Rig.W.Tick(2);
		TestTrue(TEXT("QA setup: carrying"), Cooler->AuthorityPickUp(Rig.A));
		Rig.A->RequestStance(ELureStance::Prone);
		TestTrue(TEXT("going prone puts it down"), Rig.W.TickUntil([&]() { return Cooler->IsFree(); }, 180));
		TestTrue(TEXT("... on the floor"), OnFloor(Cooler));
		CheckFacing(TEXT("prone, open floor"));
		Rig.A->RequestStance(ELureStance::Stand);
		TestTrue(TEXT("QA setup: standing again"), Rig.W.TickUntil([&]() { return !Rig.A->IsProne(); }, 240));

		// (b) A wall right in front: no room, the forced put-down.
		LCT::PlaceAt(Rig.A, FVector(0.0f, 0.0f, Z), 0.0f);
		Rig.W.Tick(2);
		TestTrue(TEXT("QA setup: carrying again"), Cooler->AuthorityPickUp(Rig.A));
		Rig.W.AddBox(FVector(75.0f, 0.0f, Z + 100.0f), FVector(15.0f, 150.0f, 100.0f));
		Rig.W.Tick(2);
		Rig.A->RequestStance(ELureStance::Prone);
		TestTrue(TEXT("no room in front: prone still puts it down (never lost)"), Rig.W.TickUntil([&]() { return Cooler->IsFree(); }, 180));
		const FVector At = Cooler->GetActorLocation();
		TestTrue(FString::Printf(TEXT("... on the floor, not in the wall (%s)"), *At.ToCompactString()), OnFloor(Cooler) && !(At.X > 60.0 && At.X < 90.0 && FMath::Abs(At.Y) < 150.0));
		CheckFacing(TEXT("prone, forced"));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAPutDownWater, "Project.Catch.QA.PutDown.WaterFacesPlayer", LCT::Flags)
	bool FCatchQAPutDownWater::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false, false, true))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler) || !TestTrue(TEXT("QA setup: carrying"), Cooler->AuthorityPickUp(Rig.A)))
		{
			return false;
		}
		Rig.W.Tick(40);
		FVector Dry = FVector::ZeroVector;
		TestTrue(TEXT("QA precondition: a dry spot known"), LCT::HandsOf(Rig.A)->GetLastDryGround(Dry));
		// Falls in off the SIDE of the dock (still facing +X): "yaw + 180" would point the latch the wrong way.
		Rig.A->SetActorLocation(FVector(0.0f, -1000.0f, -20.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (!TestTrue(TEXT("QA precondition: swimming"), Rig.W.TickUntil([&]() { return Rig.A->IsSwimming(); }, 120)))
		{
			return false;
		}
		Rig.W.Tick(2);
		TestTrue(TEXT("the cooler is put down at the last dry spot"), Cooler->IsFree() && OnFloor(Cooler) && Dist2D(Cooler->GetActorLocation(), Dry) < 5.0f);
		TestTrue(FString::Printf(TEXT("... its latch toward the swimmer (cos %.3f)"), LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation())),
			LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation()) > 0.98f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAPutDownCaught, "Project.Catch.QA.PutDown.CaughtFacesPlayer", LCT::Flags)
	bool FCatchQAPutDownCaught::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		const TArray<FFishInstance> Inside = { LCT::MakeFish(TEXT("Bonefish"), 20, 1, 1.5f, 1801), LCT::MakeFish(TEXT("Bonefish"), 21, 1, 1.5f, 1802) };
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		Fill(Cooler, Inside, Rig.W.Now());
		TestTrue(TEXT("QA setup: carrying"), Cooler->AuthorityPickUp(Rig.A));
		LCT::ProgressionOf(Rig.A)->AddMoney(40);
		Rig.W.Tick(20);
		const int32 Money0 = Money(Rig.A);
		const int32 Xp0 = Xp(Rig.A);
		const FVector Dry(-200.0f, 150.0f, Z);
		LCT::HandsOf(Rig.A)->SetLastDryGround(Dry);
		TestEqual(TEXT("caught with a cooler: no fish lost"), ULureCatchLibrary::HandlePlayerCaught(Rig.A), 0);
		Rig.W.Tick(2);
		TestTrue(TEXT("the cooler is put down at the last dry spot"), Cooler->IsFree() && OnFloor(Cooler) && Dist2D(Cooler->GetActorLocation(), Dry) < 5.0f);
		TestTrue(FString::Printf(TEXT("... its latch toward the player (cos %.3f)"), LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation())),
			LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation()) > 0.98f);
		TestEqual(TEXT("... with its fish"), Cooler->GetNumFish(), 2);
		TestTrue(TEXT("money and XP kept"), Money(Rig.A) == Money0 && Xp(Rig.A) == Xp0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAPutDownStep, "Project.Catch.QA.PutDown.StepHeightBoundary", LCT::Flags)
	bool FCatchQAPutDownStep::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, Z), 180.0f);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler) || !TestTrue(TEXT("QA setup: carrying"), Cooler->AuthorityPickUp(Rig.A)))
		{
			return false;
		}
		const float Limit = Rig.A->GetCharacterMovement()->MaxStepHeight + 5.0f; // spec: MaxStepHeight + 5 cm above the feet
		const float Radius = Rig.A->GetCapsuleComponent()->GetScaledCapsuleRadius();
		// A platform under every spot tried (100 %, 85 % and just clear of the capsule), starting 2 cm off the capsule.
		auto Platform = [&](float Height)
		{
			return Rig.W.AddBox(FVector(Radius + 2.0f + 110.0f, 0.0f, Z + Height * 0.5f), FVector(110.0f, 120.0f, Height * 0.5f));
		};
		AActor* High = Platform(Limit + 3.0f);
		Rig.W.Tick(2);
		FText Problem;
		TestFalse(FString::Printf(TEXT("a top 3 cm over the limit (%.1f cm) is refused"), Limit + 3.0f), Cooler->AuthorityPutDown(&Problem));
		TestTrue(FString::Printf(TEXT("... 'No room to put the cooler down here' ('%s')"), *Problem.ToString()), Problem.ToString().Contains(TEXT("No room")));
		TestTrue(TEXT("... and it is still carried"), LCT::HandsOf(Rig.A)->GetCarriedCooler() == Cooler);
		High->Destroy();
		Platform(Limit - 3.0f);
		Rig.W.Tick(2);
		TestTrue(FString::Printf(TEXT("a top 3 cm under the limit (%.1f cm) is fine"), Limit - 3.0f), Cooler->AuthorityPutDown(&Problem));
		TestTrue(TEXT("... standing on it"), Cooler->IsFree() && OnFloor(Cooler, Z + Limit - 3.0f));
		TestTrue(TEXT("... facing the player"), LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation()) > 0.99f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAPutDownCounter, "Project.Catch.QA.PutDown.NeverInCounterArea", LCT::Flags)
	bool FCatchQAPutDownCounter::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(-110.0f, 0.0f, Z), 0.0f);
		// A counter area at floor level over the 100 % and 85 % spots (80 and 68 cm), not the one just clear of the capsule.
		ALureSellCounter* Counter = SpawnCounter(World, FVector(77.0f, 0.0f, Z), 0.0f, TEXT("Default"), Rig.W.Markets.Get(), FVector(15.0f, 100.0f, 20.0f));
		if (!TestTrue(TEXT("QA setup"), Cooler && Counter) || !TestTrue(TEXT("QA setup: carrying"), Cooler->AuthorityPickUp(Rig.A)))
		{
			return false;
		}
		LCT::PlaceAt(Rig.A, FVector(0.0f, 0.0f, Z), 0.0f);
		Rig.W.Tick(3);
		FText Problem;
		const bool bDown = Cooler->AuthorityPutDown(&Problem);
		const FVector At = Cooler->GetActorLocation();
		if (bDown)
		{
			TestTrue(FString::Printf(TEXT("put down outside every counter area (%s)"), *At.ToCompactString()), !Counter->ContainsPoint(At) && ALureSellCounter::FindCounterAt(World, At) == nullptr);
			TestTrue(TEXT("... facing the player"), LureCatchQA::FrontFacing(Cooler, Rig.A->GetActorLocation()) > 0.99f);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("refused with 'No room' ('%s'), still carried"), *Problem.ToString()), Problem.ToString().Contains(TEXT("No room")) && LCT::HandsOf(Rig.A)->GetCarriedCooler() == Cooler);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAPutDownLid, "Project.Catch.QA.PutDown.BoxIncludesClosedLid", LCT::Flags)
	bool FCatchQAPutDownLid::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(0.0f, 250.0f, Z), 90.0f);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		Rig.W.Tick(5);
		TArray<UStaticMeshComponent*> Meshes;
		Cooler->GetComponents<UStaticMeshComponent>(Meshes);
		const UStaticMeshComponent* Body = nullptr;
		const UStaticMeshComponent* Lid = nullptr;
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			const FString Name = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetName() : FString();
			Body = Name == TEXT("SM_Cooler_Starter") ? Mesh : Body;
			Lid = Name == TEXT("SM_Cooler_Starter_Lid") ? Mesh : Lid;
		}
		if (!TestTrue(TEXT("QA precondition: the Starter row's real meshes are in use (the lane has them)"), Body && Lid && Cooler->GetLidPitch() == 0.0f))
		{
			return false;
		}
		const FBox BodyBox = Body->Bounds.GetBox();
		const FBox Whole = BodyBox + Lid->Bounds.GetBox();
		const FBox Gameplay = Cooler->GetCollisionBox()->Bounds.GetBox();
		const FString Sizes = FString::Printf(TEXT("body %s, body+lid %s, gameplay box %s"), *BodyBox.GetSize().ToCompactString(), *Whole.GetSize().ToCompactString(), *Gameplay.GetSize().ToCompactString());
		TestTrue(TEXT("QA precondition: the closed lid adds height (") + Sizes + TEXT(")"), Whole.Max.Z > BodyBox.Max.Z + 2.0);
		TestTrue(TEXT("the gameplay box reaches the top of the closed lid (") + Sizes + TEXT(")"), Gameplay.Max.Z >= Whole.Max.Z - 1.0);
		TestTrue(TEXT("... and encloses body + lid sideways (") + Sizes + TEXT(")"), Gameplay.Min.X <= Whole.Min.X + 1.0 && Gameplay.Max.X >= Whole.Max.X - 1.0
			&& Gameplay.Min.Y <= Whole.Min.Y + 1.0 && Gameplay.Max.Y >= Whole.Max.Y - 1.0 && Gameplay.Min.Z <= Whole.Min.Z + 1.0);
		TestTrue(TEXT("... without growing much past it (") + Sizes + TEXT(")"), Gameplay.GetSize().Z <= Whole.GetSize().Z + 2.0);
		TestEqual(TEXT("GetBoxHalfExtent is that box"), static_cast<float>(Cooler->GetBoxHalfExtent().Z * 2.0), static_cast<float>(Gameplay.GetSize().Z), 1.0f);
		return true;
	}

	// =====================================================================================================================
	// 5. Save v2 and the starter cooler
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASaveRoundTrip, "Project.Catch.QA.Save.RealCatchesRoundTripNoDupes", LCT::Flags)
	bool FCatchQASaveRoundTrip::RunTest(const FString& Parameters)
	{
		TArray<FFishInstance> OpenFish;
		TArray<FFishInstance> ClosedFish;
		TArray<FLureCoolerSaveData> Saved;
		TArray<uint8> Bytes;
		{
			FRig Rig;
			if (!Rig.Create(*this, false))
			{
				return false;
			}
			APlayerState* Owner = Rig.A->GetPlayerState();
			OpenFish = { LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("Bonefish"), 2101), LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("CoralSnapper"), 2102) };
			ClosedFish = { LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("CoralSnapper"), 2103) };
			ALureCoolerActor* Open = Rig.W.SpawnCooler(FVector(150.0f, 0.0f, Z), 180.0f, TEXT("Starter"), Owner);
			ALureCoolerActor* Closed = Rig.W.SpawnCooler(FVector(-150.0f, 0.0f, Z), 0.0f, TEXT("Starter"), Owner);
			ALureCoolerActor* NotMine = Rig.W.SpawnCooler(FVector(0.0f, 200.0f, Z), 270.0f);
			if (!TestTrue(TEXT("QA setup"), Owner && Open && Closed && NotMine))
			{
				return false;
			}
			Fill(Open, OpenFish, Rig.W.Now());
			Fill(Closed, ClosedFish, Rig.W.Now());
			Fill(NotMine, { LCT::MakeFish(TEXT("Bonefish"), 5, 1, 1.0f, 2104) }, Rig.W.Now());
			Open->AuthoritySetLidOpen(true);
			Rig.W.Advance(3.0f);
			Saved = ULureCatchLibrary::GetCoolerSaveData(Owner);
			TestEqual(TEXT("the save has the 2 coolers this player owns"), Saved.Num(), 2);
			FLurePlayerSaveData Out;
			Out.Coolers = Saved;
			FMemoryWriter Writer(Bytes, true);
			FObjectAndNameAsStringProxyArchive Archive(Writer, false);
			Archive.ArIsSaveGame = true;
			FLurePlayerSaveData::StaticStruct()->SerializeItem(Archive, &Out, nullptr);
		}
		FLurePlayerSaveData Loaded;
		{
			FMemoryReader Reader(Bytes, true);
			FObjectAndNameAsStringProxyArchive Archive(Reader, true);
			Archive.ArIsSaveGame = true;
			FLurePlayerSaveData::StaticStruct()->SerializeItem(Archive, &Loaded, nullptr);
		}
		if (!TestEqual(TEXT("the SaveGame archive keeps both coolers"), Loaded.Coolers.Num(), 2))
		{
			return false;
		}

		FRig Fresh;
		if (!Fresh.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Fresh.World();
		APlayerState* Owner = Fresh.A->GetPlayerState();
		TestEqual(TEXT("a fresh world: both coolers applied"), ULureCatchLibrary::ApplyCoolerSaveData(Owner, Loaded.Coolers), 2);
		TestEqual(TEXT("... exactly two coolers exist"), LureCatchQA::CountCoolers(World), 2);
		auto FindByGuid = [World](const FGuid& Guid) -> ALureCoolerActor*
		{
			for (TActorIterator<ALureCoolerActor> It(World); It; ++It)
			{
				if (IsValid(*It) && It->GetCoolerGuid() == Guid)
				{
					return *It;
				}
			}
			return nullptr;
		};
		for (const FLureCoolerSaveData& Data : Saved)
		{
			ALureCoolerActor* Cooler = FindByGuid(Data.CoolerGuid);
			const FString What = Data.bLidOpen ? TEXT("open cooler") : TEXT("closed cooler");
			if (!TestNotNull(What + TEXT(": the cooler with the saved guid exists"), Cooler))
			{
				continue;
			}
			TestTrue(What + TEXT(": row, lid, place and owner restored"), Cooler->GetCoolerId() == Data.CoolerId && Cooler->IsLidOpen() == Data.bLidOpen
				&& Cooler->GetActorLocation().Equals(Data.Location, 0.5) && Cooler->GetOwningPlayerState() == Owner);
			const TArray<FLureCaughtFish>& Records = Cooler->GetStorage()->GetFish();
			const TArray<FFishInstance>& Expected = Data.bLidOpen ? OpenFish : ClosedFish;
			TestEqual(What + TEXT(": every fish back"), Records.Num(), Expected.Num());
			const double Now = FLureFreshness::GetServerTime(World);
			for (int32 Index = 0; Index < FMath::Min(Records.Num(), Expected.Num()); ++Index)
			{
				TestTrue(FString::Printf(TEXT("%s fish %d: the real catch, bit for bit, in order"), *What, Index), FishQA::Same(Records[Index].Fish, Expected[Index]));
				TestEqual(FString::Printf(TEXT("%s fish %d: exposure as saved"), *What, Index), Records[Index].Freshness.GetExposure(Now), Data.Fish[Index].Freshness.ExposedSeconds, 0.02f);
			}
		}
		TestTrue(TEXT("QA precondition: the open cooler had spoiled ~3 s"), Saved[0].bLidOpen ? Saved[0].Fish[0].Freshness.ExposedSeconds > 2.9f : Saved[1].Fish[0].Freshness.ExposedSeconds > 2.9f);
		const double T0 = FLureFreshness::GetServerTime(World);
		Fresh.W.Advance(2.0f);
		const double T1 = FLureFreshness::GetServerTime(World);
		for (const FLureCoolerSaveData& Data : Saved)
		{
			if (ALureCoolerActor* Cooler = FindByGuid(Data.CoolerGuid))
			{
				const float Gained = Cooler->GetStorage()->GetFish()[0].Freshness.GetExposure(T1) - Cooler->GetStorage()->GetFish()[0].Freshness.GetExposure(T0);
				TestEqual(FString(Data.bLidOpen ? TEXT("open") : TEXT("closed")) + TEXT(": after loading it spoils at its lid's rate (open 1, closed 0)"), Gained, Data.bLidOpen ? static_cast<float>(T1 - T0) : 0.0f, 0.02f);
			}
		}
		TestEqual(TEXT("applying the same save again (a reconnect) reuses the coolers"), ULureCatchLibrary::ApplyCoolerSaveData(Owner, Loaded.Coolers), 2);
		TestEqual(TEXT("... still exactly two coolers"), LureCatchQA::CountCoolers(World), 2);
		for (const FFishInstance& Fish : OpenFish)
		{
			TestEqual(TEXT("... each fish exists once"), LureCatchQA::CountCopies(World, Fish), 1);
		}
		TestEqual(TEXT("... each fish exists once (closed cooler)"), LureCatchQA::CountCopies(World, ClosedFish[0]), 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAStarter, "Project.Catch.QA.Save.FallbackStarterCooler", LCT::Flags)
	bool FCatchQAStarter::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		APlayerState* StateA = Rig.A->GetPlayerState();
		APlayerState* StateB = Rig.B->GetPlayerState();
		AActor* StartA = Rig.W.SpawnMarker(FVector(-300.0f, -250.0f, Z + 92.0f), 0.0f);
		AActor* StartB = Rig.W.SpawnMarker(FVector(-300.0f, 250.0f, Z + 92.0f), 90.0f);
		if (!TestTrue(TEXT("QA setup: no Lure.CoolerSpawn marker, starter coolers on"), Settings->bSpawnStarterCooler && StateA && StateB && StartA && StartB))
		{
			return false;
		}
		const int32 Before = LureCatchQA::CountCoolers(World);
		ALureCoolerActor* CoolerA = ULureCatchLibrary::EnsureStarterCooler(StateA, StartA);
		ALureCoolerActor* CoolerB = ULureCatchLibrary::EnsureStarterCooler(StateB, StartB);
		ULureCatchLibrary::EnsureStarterCooler(StateA, StartA); // a respawn: must not make another (counted below)
		TestEqual(TEXT("two starter coolers, no more"), LureCatchQA::CountCoolers(World) - Before, 2);
		const TPair<ALureCoolerActor*, const AActor*> Pairs[] = { { CoolerA, StartA }, { CoolerB, StartB } };
		for (const TPair<ALureCoolerActor*, const AActor*>& Pair : Pairs)
		{
			ALureCoolerActor* Cooler = Pair.Key;
			if (!TestNotNull(TEXT("the starter cooler spawned"), Cooler))
			{
				continue;
			}
			const FVector Wanted = Pair.Value->GetActorTransform().TransformPosition(Settings->StarterCoolerOffset);
			TestTrue(FString::Printf(TEXT("at StarterCoolerOffset in its start's frame (%s vs %s)"), *Cooler->GetActorLocation().ToCompactString(), *Wanted.ToCompactString()),
				Dist2D(Cooler->GetActorLocation(), Wanted) < 2.0f);
			TestTrue(TEXT("... dropped onto the floor"), OnFloor(Cooler));
			TestTrue(FString::Printf(TEXT("... its latch toward the start (cos %.3f)"), LureCatchQA::FrontFacing(Cooler, Pair.Value->GetActorLocation())),
				LureCatchQA::FrontFacing(Cooler, Pair.Value->GetActorLocation()) > 0.98f);
			TestTrue(TEXT("... a Starter row, flagged as the automatic starter"), Cooler->IsStarter() && Cooler->GetCoolerId() == GetDefault<ULureProgressionSettings>()->DefaultCoolerId);
		}
		TestTrue(TEXT("each player owns exactly their own"), ULureCatchLibrary::GetOwnedCoolers(StateA) == TArray<ALureCoolerActor*>({ CoolerA })
			&& ULureCatchLibrary::GetOwnedCoolers(StateB) == TArray<ALureCoolerActor*>({ CoolerB }));
		if (!CoolerA || !CoolerB)
		{
			return false;
		}

		// A save with its own cooler replaces the EMPTY starter; a starter holding fish is never deleted.
		auto SaveWith = [&](int32 Seed)
		{
			FLureCoolerSaveData Data;
			Data.CoolerGuid = FGuid::NewGuid();
			Data.CoolerId = TEXT("Starter");
			Data.Location = FVector(200.0f, Seed % 2 ? 200.0f : -200.0f, Z);
			Data.Fish.Add(LureCatchQA::Record(LCT::MakeFish(TEXT("Bonefish"), 9, 1, 1.0f, Seed), 0.0));
			return TArray<FLureCoolerSaveData>({ Data });
		};
		const FFishInstance Kept = LCT::MakeFish(TEXT("CoralSnapper"), 19, 1, 2.0f, 2201);
		Fill(CoolerB, { Kept }, Rig.W.Now());
		TestEqual(TEXT("A's save applies"), ULureCatchLibrary::ApplyCoolerSaveData(StateA, SaveWith(2203)), 1);
		TestEqual(TEXT("B's save applies"), ULureCatchLibrary::ApplyCoolerSaveData(StateB, SaveWith(2204)), 1);
		Rig.W.Tick(2);
		TestTrue(TEXT("A's empty starter is replaced by the saved cooler"), !IsValid(CoolerA) || CoolerA->IsActorBeingDestroyed());
		TestEqual(TEXT("... A owns one cooler"), ULureCatchLibrary::GetOwnedCoolers(StateA).Num(), 1);
		TestTrue(TEXT("B's starter holding a fish is kept"), IsValid(CoolerB) && !CoolerB->IsActorBeingDestroyed() && CoolerB->GetNumFish() == 1);
		TestEqual(TEXT("... that fish still exists once"), LureCatchQA::CountCopies(World, Kept), 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAStarterShared, "Project.Catch.QA.Save.FallbackStartersDoNotOverlap", LCT::Flags)
	bool FCatchQAStarterShared::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, true))
		{
			return false;
		}
		// Two players who spawn at the same player start (a map with one PlayerStart, e.g. PIE with 2 players).
		AActor* Start = Rig.W.SpawnMarker(FVector(-300.0f, 0.0f, Z + 92.0f), 0.0f);
		ALureCoolerActor* CoolerA = ULureCatchLibrary::EnsureStarterCooler(Rig.A->GetPlayerState(), Start);
		ALureCoolerActor* CoolerB = ULureCatchLibrary::EnsureStarterCooler(Rig.B->GetPlayerState(), Start);
		if (!TestTrue(TEXT("QA setup: both starter coolers spawned"), CoolerA && CoolerB))
		{
			return false;
		}
		Rig.W.Tick(2);
		const FBox BoxA = CoolerA->GetCollisionBox()->Bounds.GetBox().ExpandBy(-0.5);
		const FBox BoxB = CoolerB->GetCollisionBox()->Bounds.GetBox().ExpandBy(-0.5);
		TestFalse(FString::Printf(TEXT("the two coolers don't stand inside each other (%s and %s)"), *CoolerA->GetActorLocation().ToCompactString(), *CoolerB->GetActorLocation().ToCompactString()),
			BoxA.Intersect(BoxB));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQASaveReapplyAfterTakeOut, "Project.Catch.QA.Save.ReapplyAfterTakeOutNoDupes", LCT::Flags)
	bool FCatchQASaveReapplyAfterTakeOut::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		APlayerState* Owner = Rig.A->GetPlayerState();
		const TArray<FFishInstance> Fish = { LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("Bonefish"), 2401),
			LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("CoralSnapper"), 2402), LureCatchQA::Roll(*this, Rig.W.FishTables, TEXT("Bonefish"), 2403) };
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(150.0f, 0.0f, Z), 180.0f, TEXT("Starter"), Owner);
		if (!TestTrue(TEXT("QA setup"), Owner && Cooler))
		{
			return false;
		}
		Fill(Cooler, Fish, Rig.W.Now());
		const TArray<FLureCoolerSaveData> Saved = ULureCatchLibrary::GetCoolerSaveData(Owner);
		if (!TestTrue(TEXT("QA setup: the save holds the cooler with its 3 fish"), Saved.Num() == 1 && Saved[0].Fish.Num() == 3))
		{
			return false;
		}

		// After the save a fish is taken out (it is in A's hand now), then the same save is applied again (a reconnect).
		TestTrue(TEXT("QA setup: lid opens"), Cooler->AuthoritySetLidOpen(true));
		ALureFishItem* Taken = Cooler->AuthorityTakeFishOut(Rig.A);
		if (!TestTrue(TEXT("QA setup: one fish taken out"), Taken && Cooler->GetNumFish() == 2))
		{
			return false;
		}
		const FFishInstance TakenFish = Taken->GetFish();
		TestEqual(TEXT("re-applying the save while the cooler exists"), ULureCatchLibrary::ApplyCoolerSaveData(Owner, Saved), 1);
		Rig.W.Tick(2);
		TestEqual(TEXT("... still exactly one cooler"), LureCatchQA::CountCoolers(World), 1);
		for (int32 Index = 0; Index < Fish.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("... fish %d exists exactly once"), Index), LureCatchQA::CountCopies(World, Fish[Index]), 1);
		}
		TestTrue(TEXT("... the live cooler wins: same actor, its 2 fish, lid still open, still the owner's"), IsValid(Cooler) && !Cooler->IsActorBeingDestroyed()
			&& Cooler->GetNumFish() == 2 && Cooler->IsLidOpen() && Cooler->GetOwningPlayerState() == Owner);
		TestTrue(TEXT("... the taken fish is still the item out of the cooler"), IsValid(Taken) && !Taken->IsActorBeingDestroyed() && FishQA::Same(Taken->GetFish(), TakenFish));

		// Again (a second reconnect): still no copies.
		TestEqual(TEXT("re-applying once more"), ULureCatchLibrary::ApplyCoolerSaveData(Owner, Saved), 1);
		for (int32 Index = 0; Index < Fish.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("... fish %d still exists exactly once"), Index), LureCatchQA::CountCopies(World, Fish[Index]), 1);
		}
		return true;
	}

	// =====================================================================================================================
	// 6. Falling in and getting caught
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAWaterHanging, "Project.Catch.QA.Water.HangingFishGoesToLastDryGround", LCT::Flags)
	bool FCatchQAWaterHanging::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false, false, true))
		{
			return false;
		}
		const FFishInstance Fish = LCT::MakeFish(TEXT("Bonefish"), 33, 1, 1.5f, 2301);
		ALureFishItem* Item = Rig.W.Land(Rig.A, Fish);
		if (!TestNotNull(TEXT("QA setup: a fish hangs on the hook"), Item))
		{
			return false;
		}
		Rig.W.Tick(40);
		FVector Dry = FVector::ZeroVector;
		TestTrue(TEXT("QA precondition: a dry spot known"), LCT::HandsOf(Rig.A)->GetLastDryGround(Dry));
		Rig.A->SetActorLocation(FVector(0.0f, -1000.0f, -20.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (!TestTrue(TEXT("QA precondition: swimming"), Rig.W.TickUntil([&]() { return Rig.A->IsSwimming(); }, 120)))
		{
			return false;
		}
		Rig.W.Tick(3);
		TestTrue(TEXT("the hanging fish is not released or lost"), IsValid(Item) && LureCatchQA::CountCopies(Rig.World(), Fish) == 1 && !LCT::NoticesOf(Rig.A).Contains(TEXT("Released")));
		TestTrue(TEXT("... it's off the hook, lying free"), LCT::HandsOf(Rig.A)->GetHangingFish() == nullptr && IsValid(Item) && Item->IsFree());
		if (IsValid(Item))
		{
			const FVector At = Item->GetActorLocation();
			TestTrue(FString::Printf(TEXT("... at the last dry spot (%s vs %s)"), *At.ToCompactString(), *Dry.ToCompactString()), Dist2D(At, Dry) < 60.0f && At.Z > Z - 1.0f && At.Z < Z + 30.0f);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQACaught, "Project.Catch.QA.Caught.LosesHandAndHookFishOnly", LCT::Flags)
	bool FCatchQACaught::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, false))
		{
			return false;
		}
		UWorld* World = Rig.World();
		const FFishInstance InHand = LCT::MakeFish(TEXT("Bonefish"), 25, 3, 1.5f, 2401);
		const FFishInstance OnHook = LCT::MakeFish(TEXT("CoralSnapper"), 26, 3, 2.5f, 2402);
		const FFishInstance InOpen = LCT::MakeFish(TEXT("Bonefish"), 27, 3, 1.5f, 2403);
		const FFishInstance InClosed = LCT::MakeFish(TEXT("Bonefish"), 28, 3, 1.5f, 2404);
		ALureCoolerActor* Open = Rig.W.SpawnCooler(FVector(150.0f, 150.0f, Z), 180.0f);
		ALureCoolerActor* Closed = Rig.W.SpawnCooler(FVector(150.0f, -150.0f, Z), 180.0f);
		if (!TestTrue(TEXT("QA setup: coolers"), Open && Closed))
		{
			return false;
		}
		Fill(Open, { InOpen }, Rig.W.Now());
		Fill(Closed, { InClosed }, Rig.W.Now());
		Open->AuthoritySetLidOpen(true);
		Rig.W.LandInHand(Rig.A, InHand);
		Rig.W.Land(Rig.A, OnHook);
		LCT::ProgressionOf(Rig.A)->AddMoney(40);
		Rig.W.Tick(5);
		const int32 Money0 = Money(Rig.A);
		const int32 Xp0 = Xp(Rig.A);
		const int32 Level0 = LCT::ProgressionOf(Rig.A)->GetLevel();
		TestEqual(TEXT("caught: the hand fish and the hook fish are lost (2)"), ULureCatchLibrary::HandlePlayerCaught(Rig.A), 2);
		Rig.W.Tick(2);
		TestTrue(TEXT("both are gone"), LureCatchQA::CountCopies(World, InHand) == 0 && LureCatchQA::CountCopies(World, OnHook) == 0);
		TestTrue(TEXT("hands and hook are empty"), EmptyHanded(Rig.A));
		TestTrue(TEXT("the coolers keep their fish"), LureCatchQA::CountCopies(World, InOpen) == 1 && LureCatchQA::CountCopies(World, InClosed) == 1);
		TestTrue(TEXT("money, XP and level are kept"), Money(Rig.A) == Money0 && Xp(Rig.A) == Xp0 && LCT::ProgressionOf(Rig.A)->GetLevel() == Level0);
		const double T0 = FLureFreshness::GetServerTime(World);
		const float OpenBefore = Open->GetStorage()->GetFish()[0].Freshness.GetExposure(T0);
		const float ClosedBefore = Closed->GetStorage()->GetFish()[0].Freshness.GetExposure(T0);
		Rig.W.Advance(2.0f);
		const double T1 = FLureFreshness::GetServerTime(World);
		TestEqual(TEXT("the open cooler's fish keep spoiling"), Open->GetStorage()->GetFish()[0].Freshness.GetExposure(T1) - OpenBefore, static_cast<float>(T1 - T0), 0.02f);
		TestEqual(TEXT("the closed cooler's fish hold"), Closed->GetStorage()->GetFish()[0].Freshness.GetExposure(T1), ClosedBefore, 1.0e-3f);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure A3 gate independent QA (qa-engineer, QA-A3c): the held cooler (T-063/T-064/T-064c/T-065) and the drop under a roof
// (T-066). Black-box from docs/specs/catch-handling-rules.md ("The hand" Drop, "The cooler" Dump / The turn) and the
// task acceptance criteria; only the gaps the implementer tests leave: dump boundaries (a full cooler, DumpSpacing 0),
// the Drop rule's wall stop and roof rule applied to a dump, the "first surface below the hand", the carrier's lid while
// showing / closed in hand, and a stale Dump from the carrier's client after turning back.
// Project.Catch.QA.S1.*

#include "Tests/Catch/QACatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#if WITH_EDITOR
#include "Tests/Catch/CatchNetTestUtils.h"
#endif

namespace LureCatchQAS1
{
	/** A player on the dock at Feet looking level along +X, a cooler with NumFish in front; PickUp / Open / Show by keys */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;
		ALureCoolerActor* Cooler = nullptr;
		TArray<FFishInstance> Fish; // bottom first

		bool Create(FAutomationTestBase& Test, int32 NumFish, const FVector& Feet = FVector(0.0f, 0.0f, LCT::DockTop))
		{
			if (!W.Create(Test))
			{
				return false;
			}
			Player = W.SpawnPlayer(Test, Feet);
			Hands = LCT::HandsOf(Player);
			Use = LCT::InteractionOf(Player);
			Cooler = W.SpawnCooler(Feet + FVector(130.0f, 0.0f, 0.0f), 180.0f);
			if (!Test.TestNotNull(TEXT("player"), Player) || !Test.TestNotNull(TEXT("hands"), Hands) || !Test.TestNotNull(TEXT("use keys"), Use)
				|| !Test.TestNotNull(TEXT("cooler"), Cooler))
			{
				return false;
			}
			LCT::PlaceAt(Player, Feet, 0.0f);
			for (int32 Index = 0; Index < NumFish; ++Index)
			{
				const FFishInstance One = LCT::MakeFish(Index % 2 ? TEXT("CoralSnapper") : TEXT("Bonefish"), 10 + Index, 1, 1.2f + 0.3f * Index, 7100 + Index);
				Fish.Add(One);
				if (!Test.TestTrue(FString::Printf(TEXT("QA setup: fish %d goes in"), Index), Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(One, W.Now()))))
				{
					return false;
				}
			}
			W.Tick(20);
			return true;
		}

		FLureResolvedInteraction Key(ELureInteractKey InKey) const { return Use->ResolveInteraction(InKey); }

		bool Press(FAutomationTestBase& Test, ELureInteractKey InKey, ELureInteractVerb Verb, const FString& What)
		{
			const FLureResolvedInteraction Now = Key(InKey);
			if (!Test.TestEqual(What + TEXT(": the key's verb"), LCT::VerbName(Now.Verb), LCT::VerbName(Verb)) || !Test.TestTrue(What + TEXT(": the target is the cooler"), Now.Target == Cooler))
			{
				return false;
			}
			return Test.TestTrue(What, Use->PressKey(InKey));
		}

		void LookLevel()
		{
			if (APlayerController* Controller = LCT::ControllerOf(Player))
			{
				Controller->SetControlRotation(FRotator(0.0f, 0.0f, 0.0f));
			}
			if (UCameraComponent* Camera = Player->GetFirstPersonCamera())
			{
				FMinimalViewInfo View;
				Camera->GetCameraView(0.0f, View);
			}
			W.Tick(3);
		}

		/** F picks it up, E opens it, (E shows it) */
		bool CarryOpen(FAutomationTestBase& Test, bool bShow)
		{
			LCT::LookAt(Player, Cooler->GetInteractionLocation());
			W.Tick(2);
			if (!Press(Test, ELureInteractKey::Secondary, ELureInteractVerb::PickUpCooler, TEXT("F picks it up")))
			{
				return false;
			}
			LookLevel();
			if (!Press(Test, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler, TEXT("E opens it")))
			{
				return false;
			}
			if (bShow && !Press(Test, ELureInteractKey::Primary, ELureInteractVerb::ShowCooler, TEXT("E shows it")))
			{
				return false;
			}
			return Test.TestTrue(TEXT("QA precondition: carried and open"), Cooler->IsHeldBy(Player, ELureHoldMode::Hand) && Cooler->IsLidOpen() && Cooler->IsShowing() == bShow);
		}

		void Advance(float Seconds) { W.Tick(FMath::CeilToInt(Seconds / LCT::Dt)); }

		void SetTuning(TFunctionRef<void(FLureCatchRow&)> Edit)
		{
			FLureCatchRow Row = ULureCatchSubsystem::GetTuningFor(W.World);
			Edit(Row);
			if (ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(W.World))
			{
				Subsystem->SetTuning(Row);
			}
		}
	};

	TArray<ALureFishItem*> ItemsIn(UWorld* World)
	{
		TArray<ALureFishItem*> Out;
		for (TActorIterator<ALureFishItem> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	ALureFishItem* ItemWithSeed(const TArray<ALureFishItem*>& Items, int32 Seed)
	{
		for (ALureFishItem* Item : Items)
		{
			if (Item->GetFish().Seed == Seed)
			{
				return Item;
			}
		}
		return nullptr;
	}

	bool IsOnDock(const FVector& Point)
	{
		return FMath::Abs(Point.Z - LCT::DockTop) < 2.0 && FMath::Abs(Point.X) <= LCT::DockHalf && FMath::Abs(Point.Y) <= LCT::DockHalf;
	}

	/** Every dumped fish exists exactly once (an item), none left in the cooler */
	void CheckConserved(FAutomationTestBase& Test, const FRig& Rig, const FString& What)
	{
		Test.TestEqual(What + TEXT(": the cooler is empty"), Rig.Cooler->GetNumFish(), 0);
		for (const FFishInstance& One : Rig.Fish)
		{
			Test.TestEqual(What + FString::Printf(TEXT(": fish seed %d exists exactly once"), One.Seed), LureCatchQA::CountCopies(Rig.W.World, One), 1);
		}
	}

	// ---- T-065 boundary: a FULL cooler (Slots) dumps every fish; the 4th goes 2 x DumpSpacing right ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1DumpFullCooler, "Project.Catch.QA.S1.Dump.FullCoolerSpreadAndConserved", LCT::Flags)
	bool FQAS1DumpFullCooler::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 0))
		{
			return false;
		}
		const int32 Capacity = Rig.Cooler->GetCapacity();
		if (!TestTrue(FString::Printf(TEXT("QA precondition: the starter cooler holds at least 4 (spec: Slots 4; got %d)"), Capacity), Capacity >= 4))
		{
			return false;
		}
		for (int32 Index = 0; Index < Capacity; ++Index)
		{
			const FFishInstance One = LCT::MakeFish(Index % 2 ? TEXT("CoralSnapper") : TEXT("Bonefish"), 10 + Index, 1, 1.2f + 0.3f * Index, 7200 + Index);
			Rig.Fish.Add(One);
			Rig.Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(One, Rig.W.Now()));
		}
		if (!TestTrue(TEXT("QA precondition: full"), Rig.Cooler->IsFull()) || !Rig.CarryOpen(*this, /*bShow*/ true))
		{
			return false;
		}
		const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
		TestEqual(TEXT("F prompt counts every fish"), Rig.Key(ELureInteractKey::Secondary).Prompt.ToString(), FString::Printf(TEXT("Dump %d fish"), Capacity));
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		if (!Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler, TEXT("F dumps a full cooler")))
		{
			return false;
		}
		CheckConserved(*this, Rig, TEXT("full dump"));
		TestEqual(TEXT("one item per record"), ItemsIn(Rig.W.World).Num(), Capacity);
		// Spec: top first straight ahead, then right, left, 2 x right, 2 x left ... (Y = right when looking along +X).
		const TArray<ALureFishItem*> Items = ItemsIn(Rig.W.World);
		for (int32 Order = 0; Order < Capacity; ++Order)
		{
			const int32 Step = (Order + 1) / 2;
			const double Side = Order == 0 ? 0.0 : (Order % 2 == 1 ? 1.0 : -1.0) * Step * Tuning.DumpSpacing;
			const FFishInstance& One = Rig.Fish[Capacity - 1 - Order];
			const ALureFishItem* Item = ItemWithSeed(Items, One.Seed);
			if (!TestNotNull(FString::Printf(TEXT("fish out %d"), Order), Item))
			{
				continue;
			}
			const FVector Rest = Item->GetActorLocation();
			TestTrue(FString::Printf(TEXT("fish out %d rests DropForward ahead, %.0f cm to the side, on the dock (%s, eye %s)"), Order, Side, *Rest.ToCompactString(), *Eye.ToCompactString()),
				FMath::IsNearlyEqual(Rest.X, Eye.X + Tuning.DropForward, 2.0) && FMath::IsNearlyEqual(Rest.Y, Eye.Y + Side, 2.0) && IsOnDock(Rest));
		}
		TestTrue(TEXT("the cooler is still carried, open and shown"), Rig.Cooler->IsHeldBy(Rig.Player, ELureHoldMode::Hand) && Rig.Cooler->IsLidOpen() && Rig.Cooler->IsShowing());
		return true;
	}

	// ---- T-065 boundary: DumpSpacing 0 (valid, "one pile") puts every fish at the same spot ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1DumpOnePile, "Project.Catch.QA.S1.Dump.ZeroSpacingOnePile", LCT::Flags)
	bool FQAS1DumpOnePile::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 3))
		{
			return false;
		}
		Rig.SetTuning([](FLureCatchRow& Row) { Row.DumpSpacing = 0.0f; });
		if (!Rig.CarryOpen(*this, /*bShow*/ true))
		{
			return false;
		}
		const float Forward = ULureCatchSubsystem::GetTuningFor(Rig.W.World).DropForward;
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		if (!Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler, TEXT("F dumps")))
		{
			return false;
		}
		CheckConserved(*this, Rig, TEXT("one pile"));
		for (const ALureFishItem* Item : ItemsIn(Rig.W.World))
		{
			const FVector Rest = Item->GetActorLocation();
			TestTrue(FString::Printf(TEXT("%s rests straight ahead at DropForward, on the dock (%s)"), *Item->GetName(), *Rest.ToCompactString()),
				FMath::IsNearlyEqual(Rest.X, Eye.X + Forward, 2.0) && FMath::IsNearlyEqual(Rest.Y, Eye.Y, 2.0) && IsOnDock(Rest) && Item->IsFree());
		}
		return true;
	}

	// ---- T-065 x Drop rule: a wall in front stops every dumped fish just before it ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1DumpWall, "Project.Catch.QA.S1.Dump.WallStopsEveryFish", LCT::Flags)
	bool FQAS1DumpWall::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 3) || !Rig.CarryOpen(*this, /*bShow*/ true))
		{
			return false;
		}
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		const float Forward = ULureCatchSubsystem::GetTuningFor(Rig.W.World).DropForward;
		// A thin wall across the view, its near face half way to DropForward, wide enough for the whole spread.
		const double Face = Eye.X + Forward * 0.5;
		Rig.W.AddBox(FVector(Face + 5.0, Eye.Y, LCT::DockTop + 150.0f), FVector(5.0f, 200.0f, 150.0f));
		Rig.W.Tick(3);
		if (!Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler, TEXT("F dumps facing a wall")))
		{
			return false;
		}
		CheckConserved(*this, Rig, TEXT("wall"));
		for (const ALureFishItem* Item : ItemsIn(Rig.W.World))
		{
			const FVector Rest = Item->GetActorLocation();
			TestTrue(FString::Printf(TEXT("%s lands on the dock in front of the wall (%s, wall face x %.0f)"), *Item->GetName(), *Rest.ToCompactString(), Face),
				Item->IsFree() && IsOnDock(Rest) && Rest.X < Face);
		}
		return true;
	}

	// ---- T-065 x T-066: a dump under a roof lands on the dock, never on the roof ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1DumpRoof, "Project.Catch.QA.S1.Dump.UnderRoofLandsOnDock", LCT::Flags)
	bool FQAS1DumpRoof::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 3) || !Rig.CarryOpen(*this, /*bShow*/ true))
		{
			return false;
		}
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		const float RoofBottom = static_cast<float>(Eye.Z) + 150.0f; // the low end of the T-066 range (150-250 above the eye)
		Rig.W.AddBox(FVector(Eye.X, Eye.Y, RoofBottom + 10.0f), FVector(400.0f, 400.0f, 10.0f));
		Rig.W.Tick(3);
		if (!Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler, TEXT("F dumps under a roof")))
		{
			return false;
		}
		CheckConserved(*this, Rig, TEXT("roof"));
		for (const ALureFishItem* Item : ItemsIn(Rig.W.World))
		{
			const FVector Rest = Item->GetActorLocation();
			TestTrue(FString::Printf(TEXT("%s lies on the dock, not on the roof (%s, roof bottom %.0f)"), *Item->GetName(), *Rest.ToCompactString(), RoofBottom),
				Item->IsFree() && IsOnDock(Rest));
		}
		return true;
	}

	// ---- T-066 rule "the first surface below the hand": a low shelf under the drop point catches the fish ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1DropShelf, "Project.Catch.QA.S1.Drop.ShelfBelowHandCatchesFish", LCT::Flags)
	bool FQAS1DropShelf::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 0))
		{
			return false;
		}
		Rig.LookLevel();
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		const float Forward = ULureCatchSubsystem::GetTuningFor(Rig.W.World).DropForward;
		// A 40 cm high crate-like slab under the drop point (far below the hand, clear of the toss), and a roof above.
		constexpr float ShelfHeight = 40.0f;
		Rig.W.AddBox(FVector(Eye.X + Forward, Eye.Y, LCT::DockTop + ShelfHeight * 0.5f), FVector(25.0f, 40.0f, ShelfHeight * 0.5f));
		Rig.W.AddBox(FVector(Eye.X, Eye.Y, Eye.Z + 210.0f), FVector(400.0f, 400.0f, 10.0f));
		Rig.W.Tick(3);
		ALureFishItem* Item = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, 7301));
		if (!TestNotNull(TEXT("a fish in hand"), Item) || !TestTrue(TEXT("F drops it"), Rig.Use->PressKey(ELureInteractKey::Secondary)))
		{
			return false;
		}
		const FVector Rest = Item->GetActorLocation();
		TestTrue(FString::Printf(TEXT("it rests on the shelf top (%s, shelf top %.0f): the first surface below the hand"), *Rest.ToCompactString(), LCT::DockTop + ShelfHeight),
			Item->IsFree() && FMath::Abs(Rest.Z - (LCT::DockTop + ShelfHeight)) < 2.0);
		TestFalse(TEXT("... the hands are empty"), Rig.Hands->IsHoldingSomething());
		return true;
	}

	// ---- T-064c: the carrier's lid stays folded back while showing and turning back; closed in hand = 0; reopened = folded ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1LidShowing, "Project.Catch.QA.S1.Lid.CarrierPitchWhileShowingAndReclosed", LCT::Flags)
	bool FQAS1LidShowing::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 2) || !Rig.CarryOpen(*this, /*bShow*/ false))
		{
			return false;
		}
		const FLureCatchRow Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
		const float Speed = Tuning.LidOpenPitch / FMath::Max(0.01f, Tuning.LidOpenTime);
		const float Settle = Tuning.CarriedOpenLidPitch / FMath::Max(1.0f, Speed) + Tuning.ShowTurnTime + 0.3f;
		ALureCoolerActor* Cooler = Rig.Cooler;
		Rig.Advance(Settle);
		TestEqual(TEXT("carried open: CarriedOpenLidPitch"), Cooler->GetLidPitch(), Tuning.CarriedOpenLidPitch, 0.01f);
		if (!Rig.Press(*this, ELureInteractKey::Primary, ELureInteractVerb::ShowCooler, TEXT("E shows it")))
		{
			return false;
		}
		Rig.Advance(Settle);
		TestEqual(TEXT("showing: still CarriedOpenLidPitch on the carrier's machine"), Cooler->GetLidPitch(), Tuning.CarriedOpenLidPitch, 0.01f);
		if (!Rig.Press(*this, ELureInteractKey::Primary, ELureInteractVerb::TurnBackCooler, TEXT("E turns it back")))
		{
			return false;
		}
		Rig.Advance(Settle);
		TestEqual(TEXT("turned back: CarriedOpenLidPitch"), Cooler->GetLidPitch(), Tuning.CarriedOpenLidPitch, 0.01f);
		if (!Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::CloseCooler, TEXT("F closes it in hand")))
		{
			return false;
		}
		Rig.Advance(Settle);
		TestTrue(TEXT("closed, still carried"), !Cooler->IsLidOpen() && Cooler->IsHeldBy(Rig.Player, ELureHoldMode::Hand));
		TestEqual(TEXT("closed in hand: 0"), Cooler->GetLidPitch(), 0.0f, 0.01f);
		if (!Rig.Press(*this, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler, TEXT("E opens it again")))
		{
			return false;
		}
		Rig.Advance(Settle);
		TestEqual(TEXT("reopened in hand: CarriedOpenLidPitch again"), Cooler->GetLidPitch(), Tuning.CarriedOpenLidPitch, 0.01f);
		return true;
	}

#if WITH_EDITOR
	// ---- T-065 net failure case: the carrier's own stale Dump (sent after turning back) is refused by the server ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1NetStaleDump, "Project.Catch.QA.S1.Net.StaleDumpAfterTurnBackRefused", LCT::Flags)
	bool FQAS1NetStaleDump::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		LureCatchNetTest::FNet Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Carrier = Net.Players[0];
		ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(130.0f, -100.0f, LCT::DockTop)), Carrier->GetPlayerState());
		if (!TestNotNull(TEXT("server cooler"), Cooler))
		{
			return false;
		}
		const double Now = FLureFreshness::GetServerTime(Net.Server);
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 7401), Now));
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 2.0f, 7402), Now));
		if (!TestTrue(TEXT("the cooler with 2 fish reaches client 0"), Net.Until([&]() { return Net.On(0, Cooler) && Net.On(0, Cooler)->GetNumFish() == 2; })))
		{
			return false;
		}
		ALureCoolerActor* Cooler0 = Net.On(0, Cooler);
		Net.LookAt(0, Cooler0->GetInteractionLocation());
		Worlds.TickAll(2);
		TestTrue(TEXT("client 0: F picks it up"), Net.Keys(0)->PressKey(ELureInteractKey::Secondary));
		if (!TestTrue(TEXT("carried on the server and client 0"), Net.Until([&]() { return Cooler->IsHeldBy(Carrier, ELureHoldMode::Hand) && Cooler0->IsHeldBy(Net.Mine(0), ELureHoldMode::Hand); })))
		{
			return false;
		}
		Net.LookAt(0, Net.Mine(0)->GetPawnViewLocation() + Net.Mine(0)->GetActorForwardVector() * 100.0f);
		Worlds.TickAll(3);
		TestTrue(TEXT("client 0: E opens"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
		TestTrue(TEXT("open"), Net.Until([&]() { return Cooler->IsLidOpen() && Cooler0->IsLidOpen(); }));
		TestTrue(TEXT("client 0: E shows"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
		TestTrue(TEXT("showing"), Net.Until([&]() { return Cooler->IsShowing() && Cooler0->IsShowing(); }));
		TestEqual(TEXT("QA precondition: client 0 F = Dump"), LCT::VerbName(Net.Keys(0)->ResolveInteraction(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::DumpCooler));

		// The server turns it back (as the carrier's E would) before the carrier's Dump arrives: the Dump is stale.
		TestTrue(TEXT("the server turns it back"), Cooler->PerformInteraction(Carrier, ELureInteractVerb::TurnBackCooler) && !Cooler->IsShowing());
		Net.Keys(0)->RequestInteract(Cooler0, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler);
		Worlds.TickAll(20);
		TestEqual(TEXT("the server refuses a Dump on a cooler that is no longer shown: both fish stay"), Cooler->GetNumFish(), 2);
		TestEqual(TEXT("... no fish items appear"), ItemsIn(Net.Server).Num(), 0);
		TestTrue(TEXT("... client 0 still sees 2 fish, carried, open, not shown"),
			Net.Until([&]() { return Cooler0->GetNumFish() == 2 && Cooler0->IsLidOpen() && !Cooler0->IsShowing() && Cooler0->IsHeldBy(Net.Mine(0), ELureHoldMode::Hand); }));
		return true;
	}
#endif // WITH_EDITOR
}

#endif // WITH_DEV_AUTOMATION_TESTS

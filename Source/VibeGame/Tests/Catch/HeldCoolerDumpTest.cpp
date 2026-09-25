// Lure T-065 tests (unreal-engineer): dump the fish out of the cooler you carry and show. The key and prompt, the spread
// in front of you (the drop rule per fish, top first), the water releasing some with one summary notice, nothing copied or
// lost, a sell counter's area, a server-side add, DumpSpacing data, and the network.
// Rules: docs/specs/catch-handling-rules.md "The cooler" (verbs, "Dump while showing"), "The hand" (Drop), "Data".
// Project.Catch.HeldCooler.Dump.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include <limits>
#if WITH_EDITOR
#include "Tests/Catch/CatchNetTestUtils.h"
#endif

namespace LureHeldCoolerDumpTest
{
	/** A player on the dock looking along +X (or Yaw) carrying a cooler with NumFish, opened and shown (keys E, E) */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;
		ALureCoolerActor* Cooler = nullptr;

		bool Create(FAutomationTestBase& Test, int32 NumFish, const FVector& Feet = FVector(0.0f, 0.0f, LCT::DockTop), float Yaw = 0.0f, bool bWater = false)
		{
			if (!W.Create(Test) || (bWater && !Test.TestNotNull(TEXT("water"), W.AddWater(400.0f))))
			{
				return false;
			}
			Player = W.SpawnPlayer(Test, Feet);
			Hands = LCT::HandsOf(Player);
			Use = LCT::InteractionOf(Player);
			Cooler = W.SpawnCooler(Feet + FRotator(0.0f, Yaw, 0.0f).Vector() * 130.0f, Yaw + 180.0f);
			if (!Test.TestNotNull(TEXT("player"), Player) || !Test.TestNotNull(TEXT("hands"), Hands) || !Test.TestNotNull(TEXT("use keys"), Use)
				|| !Test.TestNotNull(TEXT("cooler"), Cooler))
			{
				return false;
			}
			LCT::PlaceAt(Player, Feet, Yaw);
			for (int32 Index = 0; Index < NumFish; ++Index)
			{
				Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(Index % 2 ? TEXT("CoralSnapper") : TEXT("Bonefish"), 10 + Index, 1,
					1.2f + 0.4f * Index, 6500 + Index), W.Now()));
			}
			W.Tick(20);
			// F picks it up, E opens it, E shows it.
			LCT::LookAt(Player, Cooler->GetInteractionLocation());
			W.Tick(2);
			if (!Press(Test, ELureInteractKey::Secondary, ELureInteractVerb::PickUpCooler, TEXT("F picks it up")))
			{
				return false;
			}
			Look(Yaw);
			return Press(Test, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler, TEXT("E opens it"))
				&& Press(Test, ELureInteractKey::Primary, ELureInteractVerb::ShowCooler, TEXT("E shows it"))
				&& Test.TestTrue(TEXT("carried, open, showing"), Cooler->IsHeldBy(Player, ELureHoldMode::Hand) && Cooler->IsLidOpen() && Cooler->IsShowing());
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

		/** Looks level along Yaw; the camera follows at once */
		void Look(float Yaw)
		{
			if (APlayerController* Controller = LCT::ControllerOf(Player))
			{
				Controller->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
			}
			if (UCameraComponent* Camera = Player->GetFirstPersonCamera())
			{
				FMinimalViewInfo View;
				Camera->GetCameraView(0.0f, View);
			}
			W.Tick(3);
		}

		/** The seeds in the cooler, bottom first */
		TArray<int32> Seeds() const
		{
			TArray<int32> Out;
			for (const FLureCaughtFish& Fish : Cooler->GetStorage()->GetFish())
			{
				Out.Add(Fish.Fish.Seed);
			}
			return Out;
		}
	};

	/** Every fish item in World */
	TArray<ALureFishItem*> ItemsIn(UWorld* World)
	{
		TArray<ALureFishItem*> Out;
		for (TActorIterator<ALureFishItem> It(World); It; ++It)
		{
			if (IsValid(*It))
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

	// ---- 1 + 2. F dumps every fish, top first, spread in front; nothing copied or lost; the cooler stays shown, empty ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerDumpSpread, "Project.Catch.HeldCooler.Dump.KeysSpreadAndNothingLost", LCT::Flags)
	bool FHeldCoolerDumpSpread::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 3))
		{
			return false;
		}
		ALureCoolerActor* Cooler = Rig.Cooler;
		const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
		TestEqual(TEXT("DT_Catch DumpSpacing (shipped)"), Tuning.DumpSpacing, 25.0f, 1.0e-4f);
		const FLureResolvedInteraction F = Rig.Key(ELureInteractKey::Secondary);
		TestEqual(TEXT("showing with 3 fish: F dumps"), LCT::VerbName(F.Verb), LCT::VerbName(ELureInteractVerb::DumpCooler));
		TestEqual(TEXT("... prompt"), F.Prompt.ToString(), FString(TEXT("Dump 3 fish")));
		TestEqual(TEXT("... E still turns it back"), LCT::VerbName(Rig.Key(ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::TurnBackCooler));

		ULureProgressionComponent* Progression = LCT::ProgressionOf(Rig.Player);
		if (!TestNotNull(TEXT("progression"), Progression) || !TestEqual(TEXT("QA precondition: no fish items yet"), ItemsIn(Rig.W.World).Num(), 0))
		{
			return false;
		}
		const int32 Xp0 = Progression->GetTotalXp();
		Progression->ClearNotices();
		const TArray<int32> Seeds = Rig.Seeds(); // bottom first: the top is the last
		const double Now = Rig.W.Now();
		TMap<int32, float> Exposure;
		for (const FLureCaughtFish& Fish : Cooler->GetStorage()->GetFish())
		{
			Exposure.Add(Fish.Fish.Seed, Fish.Freshness.GetExposure(Now));
		}
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		const FVector Mouth = Cooler->GetMouthLocation();
		TestTrue(FString::Printf(TEXT("QA precondition: the mouth is at the cooler (%.0f cm from its contents)"), FVector::Dist(Mouth, Cooler->GetContentsRoot()->GetComponentLocation())),
			FVector::Dist(Mouth, Cooler->GetContentsRoot()->GetComponentLocation()) < 60.0);

		if (!Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler, TEXT("F dumps")))
		{
			return false;
		}
		const TArray<ALureFishItem*> Items = ItemsIn(Rig.W.World);
		TestEqual(TEXT("records out = items on the ground (3)"), Items.Num(), 3);
		TestEqual(TEXT("the cooler is empty"), Cooler->GetNumFish(), 0);
		TestTrue(TEXT("... still carried, open and shown"), Cooler->IsHeldBy(Rig.Player, ELureHoldMode::Hand) && Cooler->IsLidOpen() && Cooler->IsShowing());
		TestEqual(TEXT("... F closes it now"), LCT::VerbName(Rig.Key(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::CloseCooler));
		TestEqual(TEXT("no XP change"), Progression->GetTotalXp(), Xp0);
		const FString Notices = LCT::NoticesOf(Rig.Player);
		TestTrue(FString::Printf(TEXT("one summary notice \"Dumped 3 fish\" (%s)"), *Notices), Notices == TEXT("Dumped 3 fish"));

		// Top first: the top fish straight ahead at DropForward, then right, then left (DumpSpacing).
		const float Sides[] = { 0.0f, Tuning.DumpSpacing, -Tuning.DumpSpacing };
		for (int32 Order = 0; Order < Seeds.Num(); ++Order)
		{
			const int32 Seed = Seeds[Seeds.Num() - 1 - Order];
			ALureFishItem* Item = ItemWithSeed(Items, Seed);
			const FString What = FString::Printf(TEXT("fish %d out (seed %d)"), Order, Seed);
			if (!TestNotNull(What, Item))
			{
				continue;
			}
			const FVector Rest = Item->GetActorLocation();
			TestTrue(FString::Printf(TEXT("%s: lands DropForward ahead, %.0f cm to the side, on the dock (%s, eye %s)"), *What, Sides[Order], *Rest.ToCompactString(), *Eye.ToCompactString()),
				FMath::IsNearlyEqual(Rest.X, Eye.X + Tuning.DropForward, 2.0) && FMath::IsNearlyEqual(Rest.Y, Eye.Y + Sides[Order], 2.0)
				&& FMath::IsNearlyEqual(Rest.Z, static_cast<double>(LCT::DockTop), 2.0));
			TestTrue(What + TEXT(": lies loose (free, not on a counter)"), Item->IsFree() && Item->GetCounter() == nullptr);
			TestTrue(FString::Printf(TEXT("%s: its flight starts at the cooler's mouth (%s vs %s)"), *What, *Item->GetPlacement().From.ToCompactString(), *Mouth.ToCompactString()),
				Item->GetPlacement().bAnimate && FVector2D::Distance(FVector2D(Item->GetPlacement().From), FVector2D(Mouth)) < 1.0
				&& Item->GetPlacement().From.Z >= Mouth.Z - 1.0 && Item->GetPlacement().From.Z <= Mouth.Z + 10.0); // + the fish's own visual offset
			TestEqual(What + TEXT(": spoils at the out-of-cooler rate"), Item->GetCatch().Freshness.Rate, 1.0f);
			TestEqual(What + TEXT(": its exposure is kept"), Item->GetExposureSeconds(), Exposure.FindRef(Seed), 0.05f);
		}

		// Anyone can pick a dumped fish up.
		ALurePlayerCharacter* Friend = Rig.W.SpawnPlayer(*this, FVector(0.0f, 200.0f, LCT::DockTop), /*bLocal*/ false);
		Rig.W.Tick(40); // the flights land
		if (TestNotNull(TEXT("friend"), Friend) && Items.Num() > 0)
		{
			TestEqual(TEXT("a friend can pick a dumped fish up"), LCT::VerbName(Items[0]->GetInteraction(Friend, ELureInteractKey::Primary).Verb), LCT::VerbName(ELureInteractVerb::GrabFish));
		}
		for (const ALureFishItem* Item : Items)
		{
			TestEqual(TEXT("at rest"), Item->GetFlightTimeLeft(), 0.0f);
		}

		// The server's rules: a closed, not-shown or empty cooler is not dumped; nor by someone who doesn't carry it.
		TestEqual(TEXT("an empty cooler: nothing"), Cooler->AuthorityDumpFish(Rig.Player), 0);
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 6590), Rig.W.Now()));
		TestFalse(TEXT("not the carrier: refused"), Cooler->PerformInteraction(Friend, ELureInteractVerb::DumpCooler));
		TestTrue(TEXT("turned back"), Cooler->AuthoritySetShowing(false));
		TestEqual(TEXT("not shown: nothing"), Cooler->AuthorityDumpFish(Rig.Player), 0);
		TestEqual(TEXT("... F closes"), LCT::VerbName(Rig.Key(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::CloseCooler));
		TestEqual(TEXT("... the fish stays"), Cooler->GetNumFish(), 1);
		return true;
	}

	// ---- 1. The water releases what falls into it: one notice with the count ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerDumpWater, "Project.Catch.HeldCooler.Dump.WaterReleasesOneNotice", LCT::Flags)
	bool FHeldCoolerDumpWater::RunTest(const FString& Parameters)
	{
		// At the dock's +X edge looking along +Y: the carrier's right is -X, left is +X. DumpSpacing 40 (data): the top fish
		// lands 25 cm in from the edge, the next 65 cm in (right), the last 15 cm past the edge (left): in the water.
		FRig Rig;
		if (!Rig.Create(*this, 3, FVector(LCT::DockHalf - 25.0f, 0.0f, LCT::DockTop), 90.0f, /*bWater*/ true))
		{
			return false;
		}
		FLureCatchRow Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
		Tuning.DumpSpacing = 40.0f;
		ULureCatchSubsystem::Get(Rig.W.World)->SetTuning(Tuning);
		ULureProgressionComponent* Progression = LCT::ProgressionOf(Rig.Player);
		if (!TestNotNull(TEXT("progression"), Progression))
		{
			return false;
		}
		Progression->ClearNotices();
		const TArray<int32> Seeds = Rig.Seeds();
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		TestTrue(FString::Printf(TEXT("QA precondition: the eye is 25 cm in from the edge (x %.1f)"), Eye.X), FMath::IsNearlyEqual(Eye.X, LCT::DockHalf - 25.0, 3.0));
		if (!Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler, TEXT("F dumps at the edge")))
		{
			return false;
		}
		const TArray<ALureFishItem*> Items = ItemsIn(Rig.W.World);
		TestEqual(TEXT("two lie on the dock"), Items.Num(), 2);
		TestEqual(TEXT("records out = items + released (the cooler is empty)"), Rig.Cooler->GetNumFish(), 0);
		TestNotNull(TEXT("the top fish is on the dock"), ItemWithSeed(Items, Seeds[2]));
		TestNotNull(TEXT("the next (right, inland) is on the dock"), ItemWithSeed(Items, Seeds[1]));
		TestNull(TEXT("the last (left, past the edge) is released"), ItemWithSeed(Items, Seeds[0]));
		const FString Notices = LCT::NoticesOf(Rig.Player);
		TestTrue(FString::Printf(TEXT("one notice \"Dumped 3 fish, 1 released\", no per-fish release notice (%s)"), *Notices), Notices == TEXT("Dumped 3 fish, 1 released"));

		// A single drop still says "Released the <fish>" (unchanged): a fish from the dock dropped into the water.
		Rig.W.Tick(40);
		TestTrue(TEXT("closed (F on the empty shown cooler)"), Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::CloseCooler, TEXT("F closes")));
		ALureFishItem* Single = Items.Num() > 0 ? Items[0] : nullptr;
		if (TestNotNull(TEXT("a dumped fish"), Single))
		{
			Progression->ClearNotices();
			Single->AuthorityRelease(Rig.Player);
			TestTrue(FString::Printf(TEXT("a single release still notifies (%s)"), *LCT::NoticesOf(Rig.Player)), LCT::NoticesOf(Rig.Player).StartsWith(TEXT("Released the ")));
		}
		return true;
	}

	// ---- 3. A sell counter's area: the fish join the counter, as a single drop does ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerDumpCounter, "Project.Catch.HeldCooler.Dump.OnSellCounter", LCT::Flags)
	bool FHeldCoolerDumpCounter::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 3))
		{
			return false;
		}
		const float Forward = ULureCatchSubsystem::GetTuningFor(Rig.W.World).DropForward;
		const FVector Eye = Rig.Player->GetPawnViewLocation();
		ALureSellCounter* Counter = Rig.W.SpawnCounter(FVector(Eye.X + Forward, Eye.Y, LCT::DockTop), 180.0f);
		Rig.W.Tick(2);
		if (!TestNotNull(TEXT("counter"), Counter) || !Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler, TEXT("F dumps onto the counter's area")))
		{
			return false;
		}
		const TArray<ALureFishItem*> Items = ItemsIn(Rig.W.World);
		TestEqual(TEXT("3 items"), Items.Num(), 3);
		for (const ALureFishItem* Item : Items)
		{
			TestTrue(FString::Printf(TEXT("%s joined the counter"), *Item->GetName()), Item->GetCounter() == Counter);
		}
		TestEqual(TEXT("the counter has the 3 fish"), Counter->GetFishOnCounter().Num(), 3);
		return true;
	}

	// ---- Data: DT_Catch DumpSpacing and the spread ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerDumpData, "Project.Catch.HeldCooler.Dump.DataDumpSpacing", LCT::Flags)
	bool FHeldCoolerDumpData::RunTest(const FString& Parameters)
	{
		const TStrongObjectPtr<UDataTable> Catch = LCT::Shipped(*this, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
		const FLureCatchRow* Row = Catch.IsValid() ? Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("HeldCoolerDump"), false) : nullptr;
		if (!TestNotNull(TEXT("DT_Catch Default"), Row))
		{
			return false;
		}
		TestEqual(TEXT("DumpSpacing shipped (PLACEHOLDER 25 cm)"), Row->DumpSpacing, 25.0f, 1.0e-5f);
		TestEqual(TEXT("... = the built-in row"), FLureCatchRow::GetFallbackRow().DumpSpacing, Row->DumpSpacing, 1.0e-5f);
		FString Problem;
		TestTrue(TEXT("the shipped row is valid"), Row->Validate(Problem));
		for (const float Bad : { -1.0f, 201.0f, std::numeric_limits<float>::quiet_NaN() })
		{
			FLureCatchRow Broken = *Row;
			Broken.DumpSpacing = Bad;
			TestFalse(FString::Printf(TEXT("DumpSpacing %g is refused"), Bad), Broken.Validate(Problem));
			TestTrue(TEXT("... naming the column"), Problem.Contains(TEXT("DumpSpacing")));
		}
		FLureCatchRow Pile = *Row;
		Pile.DumpSpacing = 0.0f;
		TestTrue(TEXT("0 (one pile) is valid"), Pile.Validate(Problem));
		// An older DT_Catch without the column still imports, with the default.
		TArray<FString> Problems;
		const TStrongObjectPtr<UDataTable> Old = LCT::MakeTable(FLureCatchRow::StaticStruct(),
			TEXT("Name,HangLineLength,HangDamping,ReachDistance,FocusAngleDeg,DropForward,DropArcTime,PutDownDistance,PutDownMaxFall,LidOpenPitch,LidOpenTime,ShowTurnTime,DevComment\n")
			TEXT("Default,40,1.2,250,20,60,0.35,80,300,100,0.25,0.4,old\n"), false, &Problems);
		const FLureCatchRow* OldRow = Old.IsValid() ? Old->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("HeldCoolerDump"), false) : nullptr;
		TestTrue(FString::Printf(TEXT("an older DT_Catch imports (%s)"), *FString::Join(Problems, TEXT(" | "))), OldRow && Problems.Num() == 0);
		TestTrue(TEXT("... with DumpSpacing at its default"), OldRow && FMath::IsNearlyEqual(OldRow->DumpSpacing, 25.0f));
		// The spread: straight ahead, then right, left, 2 x right, 2 x left.
		const float Expected[] = { 0.0f, 25.0f, -25.0f, 50.0f, -50.0f };
		for (int32 Index = 0; Index < 5; ++Index)
		{
			TestEqual(FString::Printf(TEXT("side offset of fish %d"), Index), ALureCoolerActor::GetDumpSideOffset(Index, 25.0f), Expected[Index], 1.0e-4f);
		}
		return true;
	}

#if WITH_EDITOR
	// ---- 3 + 4. Network: a client dumps (right after a server-side add); every machine sees the same rest points, an empty cooler ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerDumpNet, "Project.Catch.HeldCooler.Dump.NetClientDumps", LCT::Flags)
	bool FHeldCoolerDumpNet::RunTest(const FString& Parameters)
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
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 6701), Now));
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 2.0f, 6702), Now));
		if (!TestTrue(TEXT("the cooler with 2 fish reaches both clients"), Net.Until([&]() { return Net.On(0, Cooler) && Net.On(1, Cooler) && Net.On(0, Cooler)->GetNumFish() == 2 && Net.On(1, Cooler)->GetNumFish() == 2; })))
		{
			return false;
		}
		ALureCoolerActor* Cooler0 = Net.On(0, Cooler);
		ALureCoolerActor* Cooler1 = Net.On(1, Cooler);
		ULureProgressionComponent* Progress = LCT::ProgressionOf(Carrier);
		const int32 Xp0 = Progress ? Progress->GetTotalXp() : 0;

		// Client 0: F picks it up, E opens, E shows.
		Net.LookAt(0, Cooler0->GetInteractionLocation());
		Worlds.TickAll(2);
		TestTrue(TEXT("client 0: F picks it up"), Net.Keys(0)->PressKey(ELureInteractKey::Secondary));
		if (!TestTrue(TEXT("carried everywhere"), Net.Until([&]() { return Cooler->IsHeldBy(Carrier, ELureHoldMode::Hand) && Cooler0->IsHeldBy(Net.Mine(0), ELureHoldMode::Hand)
			&& Cooler1->IsHeldBy(Net.On(1, Carrier), ELureHoldMode::Hand); })))
		{
			return false;
		}
		Net.LookAt(0, Net.Mine(0)->GetPawnViewLocation() + Net.Mine(0)->GetActorForwardVector() * 100.0f);
		Worlds.TickAll(3);
		TestTrue(TEXT("client 0: E opens"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
		TestTrue(TEXT("open"), Net.Until([&]() { return Cooler->IsLidOpen() && Cooler0->IsLidOpen(); }));
		TestTrue(TEXT("client 0: E shows"), Net.Keys(0)->PressKey(ELureInteractKey::Primary));
		if (!TestTrue(TEXT("showing on every machine"), Net.Until([&]() { return Cooler->IsShowing() && Cooler0->IsShowing() && Cooler1->IsShowing(); })))
		{
			return false;
		}
		Worlds.TickAll(10);

		// A stale Dump from the other player: refused by the server's verb check.
		Net.Keys(1)->RequestInteract(Cooler1, ELureInteractKey::Secondary, ELureInteractVerb::DumpCooler);
		Worlds.TickAll(10);
		TestEqual(TEXT("the server refuses a Dump from a non-carrier"), Cooler->GetNumFish(), 2);

		// Client 0 sees "Dump 2 fish"; the server adds one right before the request arrives: the dump takes what the server has.
		const FLureResolvedInteraction F = Net.Keys(0)->ResolveInteraction(ELureInteractKey::Secondary);
		TestTrue(FString::Printf(TEXT("client 0: F = \"Dump 2 fish\" (%s)"), *F.Prompt.ToString()), F.Verb == ELureInteractVerb::DumpCooler && F.Prompt.ToString() == TEXT("Dump 2 fish"));
		if (ULureProgressionComponent* Mine = LCT::ProgressionOf(Net.Mine(0)))
		{
			Mine->ClearNotices();
		}
		TestTrue(TEXT("client 0: F"), Net.Keys(0)->PressKey(ELureInteractKey::Secondary));
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 25, 1, 1.8f, 6703), FLureFreshness::GetServerTime(Net.Server)));
		if (!TestTrue(TEXT("the server dumps all 3"), Net.Until([&]() { return Cooler->GetNumFish() == 0; })))
		{
			return false;
		}
		const TArray<ALureFishItem*> Items = ItemsIn(Net.Server);
		TestEqual(TEXT("3 items on the server"), Items.Num(), 3);
		TestTrue(TEXT("the cooler count is 0 on every machine"), Net.Until([&]() { return Cooler0->GetNumFish() == 0 && Cooler1->GetNumFish() == 0; }));
		TestTrue(TEXT("still carried, open and shown everywhere"), Cooler->IsHeldBy(Carrier, ELureHoldMode::Hand) && Cooler->IsShowing() && Cooler0->IsShowing() && Cooler1->IsShowing()
			&& Cooler0->IsLidOpen() && Cooler1->IsLidOpen());
		const bool bCopies = Net.Until([&]()
		{
			for (ALureFishItem* Item : Items)
			{
				const ALureFishItem* Copy0 = Net.On(0, Item);
				const ALureFishItem* Copy1 = Net.On(1, Item);
				if (!Copy0 || !Copy1 || !Copy0->IsFree() || !Copy1->IsFree())
				{
					return false;
				}
			}
			return true;
		});
		if (TestTrue(TEXT("every item reaches both clients, free"), bCopies))
		{
			for (ALureFishItem* Item : Items)
			{
				const FVector Rest = Item->GetActorLocation();
				const FVector Rest0 = Net.On(0, Item)->GetActorLocation();
				const FVector Rest1 = Net.On(1, Item)->GetActorLocation();
				TestTrue(FString::Printf(TEXT("%s: the same rest point everywhere (%s / %s / %s), on the dock"), *Item->GetName(), *Rest.ToCompactString(), *Rest0.ToCompactString(), *Rest1.ToCompactString()),
					FVector::Dist(Rest, Rest0) < 1.0 && FVector::Dist(Rest, Rest1) < 1.0 && FMath::IsNearlyEqual(Rest.Z, static_cast<double>(LCT::DockTop), 2.0));
			}
		}
		TestTrue(TEXT("client 0 sees \"Dumped 3 fish\""), Net.Until([&]() { return LCT::NoticesOf(Net.Mine(0)).Contains(TEXT("Dumped 3 fish")); }));
		TestFalse(TEXT("... and no per-fish release notice"), LCT::NoticesOf(Net.Mine(0)).Contains(TEXT("Released")));
		TestFalse(TEXT("... client 1 doesn't"), LCT::NoticesOf(Net.Mine(1)).Contains(TEXT("Dumped")));
		TestEqual(TEXT("no XP change"), Progress ? Progress->GetTotalXp() : 0, Xp0);
		TestEqual(TEXT("client 0: F closes the empty cooler"), LCT::VerbName(Net.Keys(0)->ResolveInteraction(ELureInteractKey::Secondary).Verb), LCT::VerbName(ELureInteractVerb::CloseCooler));
		return true;
	}
#endif // WITH_EDITOR
}

#endif // WITH_DEV_AUTOMATION_TESTS

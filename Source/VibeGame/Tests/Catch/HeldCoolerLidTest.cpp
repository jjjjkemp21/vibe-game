// Lure T-064c tests (unreal-engineer): a cooler you carry open folds its lid back (CarriedOpenLidPitch, DT_Catch) in your
// own first-person view only; other machines and a standing open cooler keep LidOpenPitch. Cosmetic, local, no new replication.
// Rules: docs/specs/catch-handling-rules.md "Open and show while carrying" (the turn), "Data".
// Project.Catch.HeldCooler.Lid.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include <limits>
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#if WITH_EDITOR
#include "Tests/Catch/CatchNetTestUtils.h"
#endif

namespace LureHeldCoolerLidTest
{
	/** Seconds the lid needs to swing from 0 to Pitch at the open speed (LidOpenPitch / LidOpenTime) */
	float SwingTime(const FLureCatchRow& Tuning, float Pitch)
	{
		return Tuning.LidOpenPitch > 0.0f ? Tuning.LidOpenTime * Pitch / Tuning.LidOpenPitch : 0.0f;
	}

	/** A player on the dock looking along +X, a cooler with 2 fish standing in front of him */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;
		ALureCoolerActor* Cooler = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!W.Create(Test))
			{
				return false;
			}
			Player = W.SpawnPlayer(Test, FVector(0.0f, 0.0f, LCT::DockTop));
			Hands = LCT::HandsOf(Player);
			Use = LCT::InteractionOf(Player);
			Cooler = W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f);
			if (!Test.TestNotNull(TEXT("player"), Player) || !Test.TestNotNull(TEXT("hands"), Hands) || !Test.TestNotNull(TEXT("use keys"), Use)
				|| !Test.TestNotNull(TEXT("cooler"), Cooler))
			{
				return false;
			}
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10 + Index, 1, 1.5f, 6700 + Index), W.Now()));
			}
			W.Tick(20);
			return true;
		}

		bool PickUp(FAutomationTestBase& Test)
		{
			LCT::LookAt(Player, Cooler->GetInteractionLocation());
			W.Tick(2);
			if (!Test.TestTrue(TEXT("F picks it up"), Use->PressKey(ELureInteractKey::Secondary)))
			{
				return false;
			}
			if (APlayerController* Controller = LCT::ControllerOf(Player))
			{
				Controller->SetControlRotation(FRotator(0.0f, Player->GetActorRotation().Yaw, 0.0f));
			}
			W.Tick(3);
			return Test.TestTrue(TEXT("carried in both hands"), Hands->GetCarriedCooler() == Cooler && Cooler->IsHeldBy(Player, ELureHoldMode::Hand));
		}

		bool Open(FAutomationTestBase& Test)
		{
			const FLureResolvedInteraction Now = Use->ResolveInteraction(ELureInteractKey::Primary);
			return Test.TestEqual(TEXT("E opens"), LCT::VerbName(Now.Verb), LCT::VerbName(ELureInteractVerb::OpenCooler))
				&& Test.TestTrue(TEXT("E"), Use->PressKey(ELureInteractKey::Primary));
		}

		void Advance(float Seconds) { W.Tick(FMath::CeilToInt(Seconds / LCT::Dt)); }
	};

	// ---- 1 + 3. The carrier's own view: open folds back; the swing speed; put down; close ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerLidCarrier, "Project.Catch.HeldCooler.Lid.CarrierFoldsBack", LCT::Flags)
	bool FHeldCoolerLidCarrier::RunTest(const FString& Parameters)
	{
		// Shipped tuning, then a data-edited fold (proves the column drives it).
		for (const float Custom : { -1.0f, 200.0f })
		{
			const FString Case = Custom < 0.0f ? TEXT("shipped") : TEXT("data 200");
			FRig Rig;
			if (!Rig.Create(*this))
			{
				return false;
			}
			if (Custom >= 0.0f)
			{
				FLureCatchRow Row = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
				Row.CarriedOpenLidPitch = Custom;
				ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(Rig.W.World);
				if (!TestNotNull(TEXT("catch subsystem"), Subsystem))
				{
					return false;
				}
				Subsystem->SetTuning(Row);
			}
			const FLureCatchRow Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
			ALureCoolerActor* Cooler = Rig.Cooler;
			if (!Rig.PickUp(*this))
			{
				return false;
			}
			TestEqual(Case + TEXT(": carried closed: 0"), Cooler->GetLidPitch(), 0.0f, 0.01f);
			if (!Rig.Open(*this))
			{
				return false;
			}
			const float Speed = Tuning.LidOpenPitch / FMath::Max(0.01f, Tuning.LidOpenTime);
			const float Slack = Speed * LCT::Dt * 3.0f;
			// The same angular speed as a standing lid: after LidOpenTime it is near LidOpenPitch, still swinging on.
			Rig.Advance(Tuning.LidOpenTime);
			TestTrue(FString::Printf(TEXT("%s: after LidOpenTime the lid is near LidOpenPitch (%.1f, %.1f +- %.1f)"), *Case, Cooler->GetLidPitch(), Tuning.LidOpenPitch, Slack),
				FMath::Abs(Cooler->GetLidPitch() - Tuning.LidOpenPitch) <= Slack);
			Rig.Advance(SwingTime(Tuning, Tuning.CarriedOpenLidPitch) - Tuning.LidOpenTime + 0.1f);
			TestEqual(Case + TEXT(": the carrier's open lid settles at CarriedOpenLidPitch"), Cooler->GetLidPitch(), Tuning.CarriedOpenLidPitch, 0.01f);
			TestTrue(Case + TEXT(": the fish still show"), Cooler->GetNumDisplayedFish() > 0);

			// Put it down open: back to LidOpenPitch on the former carrier's machine.
			TestTrue(Case + TEXT(": the server puts it down"), Cooler->AuthorityPutDown());
			Rig.Advance(SwingTime(Tuning, FMath::Abs(Tuning.CarriedOpenLidPitch - Tuning.LidOpenPitch)) + 0.7f);
			TestTrue(Case + TEXT(": free and open"), Cooler->IsFree() && Cooler->IsLidOpen());
			TestEqual(Case + TEXT(": standing open: LidOpenPitch"), Cooler->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);

			// Close: 0.
			TestTrue(Case + TEXT(": the server closes it"), Cooler->AuthoritySetLidOpen(false));
			Rig.Advance(Tuning.LidOpenTime + 0.1f);
			TestEqual(Case + TEXT(": closed: 0"), Cooler->GetLidPitch(), 0.0f, 0.01f);
		}
		return true;
	}

	// ---- 2. A standing open cooler never folds back ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerLidStanding, "Project.Catch.HeldCooler.Lid.StandingKeepsOpenPitch", LCT::Flags)
	bool FHeldCoolerLidStanding::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this))
		{
			return false;
		}
		const FLureCatchRow Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
		TestTrue(TEXT("the server opens the standing cooler"), Rig.Cooler->AuthoritySetLidOpen(true));
		Rig.Advance(SwingTime(Tuning, Tuning.CarriedOpenLidPitch) + 0.2f);
		TestEqual(TEXT("standing open: LidOpenPitch"), Rig.Cooler->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);
		TestEqual(TEXT("the target here is LidOpenPitch"), Rig.Cooler->GetLidTargetPitch(Tuning), Tuning.LidOpenPitch, 0.01f);
		return true;
	}

	// ---- 4. Data ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerLidData, "Project.Catch.HeldCooler.Lid.Data", LCT::Flags)
	bool FHeldCoolerLidData::RunTest(const FString& Parameters)
	{
		const TStrongObjectPtr<UDataTable> Catch = LCT::Shipped(*this, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
		const FLureCatchRow* Row = Catch.IsValid() ? Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("HeldCoolerLid"), false) : nullptr;
		if (!TestNotNull(TEXT("DT_Catch Default"), Row))
		{
			return false;
		}
		TestEqual(TEXT("CarriedOpenLidPitch shipped (235 deg, art gate A)"), Row->CarriedOpenLidPitch, 235.0f, 1.0e-5f);
		TestEqual(TEXT("... = the built-in row"), FLureCatchRow::GetFallbackRow().CarriedOpenLidPitch, Row->CarriedOpenLidPitch, 1.0e-5f);
		FString Problem;
		TestTrue(TEXT("the shipped row is valid"), Row->Validate(Problem));
		for (const float Good : { 0.0f, 270.0f })
		{
			FLureCatchRow Edge = *Row;
			Edge.CarriedOpenLidPitch = Good;
			TestTrue(FString::Printf(TEXT("CarriedOpenLidPitch %.0f is allowed"), Good), Edge.Validate(Problem));
		}
		for (const float Bad : { -1.0f, 271.0f, std::numeric_limits<float>::quiet_NaN() })
		{
			FLureCatchRow Broken = *Row;
			Broken.CarriedOpenLidPitch = Bad;
			TestFalse(FString::Printf(TEXT("CarriedOpenLidPitch %g is refused"), Bad), Broken.Validate(Problem));
			TestTrue(TEXT("... naming the column"), Problem.Contains(TEXT("CarriedOpenLidPitch")));
		}
		// A DT_Catch without the column still imports, with the default.
		TArray<FString> Problems;
		const TStrongObjectPtr<UDataTable> Old = LCT::MakeTable(FLureCatchRow::StaticStruct(),
			TEXT("Name,HangLineLength,HangDamping,ReachDistance,FocusAngleDeg,DropForward,DropArcTime,PutDownDistance,PutDownMaxFall,LidOpenPitch,LidOpenTime,DevComment\n")
			TEXT("Default,40,1.2,250,20,60,0.35,80,300,100,0.25,old\n"), false, &Problems);
		const FLureCatchRow* OldRow = Old.IsValid() ? Old->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("HeldCoolerLid"), false) : nullptr;
		TestTrue(FString::Printf(TEXT("an older DT_Catch imports (%s)"), *FString::Join(Problems, TEXT(" | "))), OldRow && Problems.Num() == 0);
		TestTrue(TEXT("... with CarriedOpenLidPitch at its default"), OldRow && FMath::IsNearlyEqual(OldRow->CarriedOpenLidPitch, 235.0f));
		return true;
	}

#if WITH_EDITOR
	// ---- 2. Network: a client carries it open; only that client folds the lid back ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerLidNet, "Project.Catch.HeldCooler.Lid.NetOnlyCarrierFolds", LCT::Flags)
	bool FHeldCoolerLidNet::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		LureCatchNetTest::FNet Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Carrier = Net.Players[0];
		ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(130.0f, 0.0f, LCT::DockTop)), Carrier->GetPlayerState());
		if (!TestNotNull(TEXT("server cooler"), Cooler))
		{
			return false;
		}
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 6801), FLureFreshness::GetServerTime(Net.Server)));
		if (!TestTrue(TEXT("the cooler reaches both clients"), Net.Until([&]() { return Net.On(0, Cooler) && Net.On(1, Cooler) && Net.On(0, Cooler)->GetNumFish() == 1 && Net.On(1, Cooler)->GetNumFish() == 1; })))
		{
			return false;
		}
		ALureCoolerActor* Cooler0 = Net.On(0, Cooler);
		ALureCoolerActor* Cooler1 = Net.On(1, Cooler);
		const FLureCatchRow& Tuning = Net.Data.Tuning;
		const int32 SettleTicks = FMath::CeilToInt((SwingTime(Tuning, Tuning.CarriedOpenLidPitch) + 0.2f) / LureCatchNetTest::NetDt);

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
		TestTrue(TEXT("open on every machine"), Net.Until([&]() { return Cooler->IsLidOpen() && Cooler0->IsLidOpen() && Cooler1->IsLidOpen(); }));
		Worlds.TickAll(SettleTicks);
		TestEqual(TEXT("the carrier (client 0): CarriedOpenLidPitch"), Cooler0->GetLidPitch(), Tuning.CarriedOpenLidPitch, 0.01f);
		TestEqual(TEXT("the other client: LidOpenPitch"), Cooler1->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);
		// The test server draws nothing (no lid animation there); its target is still the shared one.
		TestEqual(TEXT("the server's lid target: LidOpenPitch"), Cooler->GetLidTargetPitch(Tuning), Tuning.LidOpenPitch, 0.01f);

		// Put down open: the carrier's lid comes back to LidOpenPitch; the others never moved.
		TestTrue(TEXT("the server puts it down"), Cooler->AuthorityPutDown());
		TestTrue(TEXT("free everywhere"), Net.Until([&]() { return Cooler->IsFree() && Cooler0->IsFree() && Cooler1->IsFree(); }));
		Worlds.TickAll(SettleTicks);
		TestEqual(TEXT("the former carrier: LidOpenPitch"), Cooler0->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);
		TestEqual(TEXT("the other client: LidOpenPitch"), Cooler1->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);
		return true;
	}
#endif // WITH_EDITOR
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure T-006 QA (qa-engineer): bite, hook window, early presses, misses, landing and determinism.
// Project.Fishing.QA.Hook.* - spec: docs/specs/fishing-rules.md "Bite, hook, miss" and "Flow and authority".

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/PlayerController.h"

// Everything lives in namespace QAFishing (unity builds merge test files; other files use global using-directives).
namespace QAFishing
{

namespace HookLocal
{
	bool IsState(const ULureFishingComponent* Fishing, ELureFishingState State)
	{
		return Fishing->GetFishingState() == State;
	}

	/** Casts from where the character stands and waits for the bite. Returns the bite's server time (< 0 = no bite). */
	double CastUntilBite(FAutomationTestBase& Test, FScene& Scene, ALurePlayerCharacter* Character, ULureFishingComponent* Fishing, int32 MaxFrames = 240)
	{
		if (!Fishing->AuthorityCast(0.5f, static_cast<float>(Character->GetActorRotation().Yaw)))
		{
			Test.AddError(TEXT("QA: cast refused: ") + BlockName(Fishing->GetNetState().ResultReason));
			return -1.0;
		}
		if (!Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Biting); }, MaxFrames))
		{
			Test.AddError(TEXT("QA: no bite came (state ") + StateName(Fishing->GetFishingState()) + TEXT(")"));
			return -1.0;
		}
		return Fishing->GetNetState().StateStartTime;
	}

	/** Casts, waits for the bite, lets Offset seconds of the bite pass, presses if still biting. Returns the result; reels in after. */
	ELureFishingResult HookAfter(FAutomationTestBase& Test, FScene& Scene, ALurePlayerCharacter* Character, ULureFishingComponent* Fishing, double Offset)
	{
		const double BiteStart = CastUntilBite(Test, Scene, Character, Fishing);
		if (BiteStart < 0.0)
		{
			return ELureFishingResult::None;
		}
		Scene.AdvanceTo(BiteStart + Offset);
		if (IsState(Fishing, ELureFishingState::Biting))
		{
			Fishing->AuthorityHook();
		}
		const ELureFishingResult Result = Fishing->GetNetState().LastResult;
		Fishing->AuthorityReelIn();
		Scene.Tick(1);
		return Result;
	}

	FLureFishingRow SpookProfile(const FLureFishingRow& Shipped)
	{
		FLureFishingRow Row = FlowProfile(Shipped, 5.f, 1.f);
		Row.NibblesMin = Row.NibblesMax = 2;
		Row.NibbleInterval = 1.1f;
		Row.NibbleDuration = 0.45f;
		Row.SpookDelay = 3.f;
		Row.EarlyHook = ELureEarlyHookRule::Spook;
		return Row;
	}

	struct FBiteRecord
	{
		double WaitAfterLanding = 0.0;
		int32 Nibbles = 0;
		FFishInstance Fish;
	};

	/** Three casts in a fresh scene with Seed; what happened each time. */
	TArray<FBiteRecord> RecordBites(FAutomationTestBase& Test, const FLureFishingRow& Profile, int32 Seed)
	{
		TArray<FBiteRecord> Records;
		FScene Scene;
		if (!Scene.Create(Test))
		{
			return Records;
		}
		Scene.AddShoreSpot();
		ALurePlayerCharacter* Character = Scene.Spawn(Test);
		ULureFishingComponent* Fishing = Scene.SetUpFishing(Test, Character, Profile, 12.f, Seed);
		if (!Fishing)
		{
			return Records;
		}
		Scene.Tick(10);
		for (int32 Cast = 0; Cast < 3; ++Cast)
		{
			FBiteRecord Record;
			Fishing->AuthorityCast(0.5f, 0.f);
			Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Waiting); }, 180);
			const double Landed = Fishing->GetNetState().StateStartTime;
			const uint8 NibblesBefore = Fishing->GetNetState().NibbleId;
			Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Biting); }, 1200);
			Record.WaitAfterLanding = Fishing->GetNetState().StateStartTime - Landed;
			Record.Nibbles = static_cast<uint8>(Fishing->GetNetState().NibbleId - NibblesBefore);
			Record.Fish = Fishing->GetPendingFish();
			Records.Add(Record);
			Fishing->AuthorityReelIn();
			Scene.Tick(2);
		}
		return Records;
	}
}

using namespace HookLocal;

// =====================================================================================================================
// The hook window
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookWindowEdgesPure, "Project.Fishing.QA.Hook.WindowEdgesPure", QAFishing::Flags)
bool FQAFishHookWindowEdgesPure::RunTest(const FString& Parameters)
{
	// Spec: a press inside [bite, bite + HookWindow (+ grace)] hooks; the edges are part of the window.
	FLureFishingRow Row;
	if (!ShippedFishingRow(*this, Row))
	{
		return false;
	}
	for (const float Window : { 0.3f, Row.HookWindow, 1.7f })
	{
		Row.HookWindow = Window;
		for (const double Start : { 0.0, 1234.5, 1.0e6 })
		{
			for (const float Grace : { 0.f, 0.15f, 0.4f })
			{
				const FString Label = FString::Printf(TEXT("window %.2f, bite at %.1f, grace %.2f"), Window, Start, Grace);
				const double Deadline = Start + static_cast<double>(Window) + static_cast<double>(Grace);
				TestTrue(Label + TEXT(": deadline = bite + window + grace"), FMath::IsNearlyEqual(FLureFishingRules::HookDeadline(Row, Start, Grace), Deadline, 1.0e-9));
				TestTrue(Label + TEXT(": at the bite"), FLureFishingRules::IsInHookWindow(Row, Start, Start, Grace));
				TestFalse(Label + TEXT(": 0.1 ms before the bite"), FLureFishingRules::IsInHookWindow(Row, Start, Start - 1.0e-4, Grace));
				TestTrue(Label + TEXT(": 0.1 ms before the deadline"), FLureFishingRules::IsInHookWindow(Row, Start, Deadline - 1.0e-4, Grace));
				TestTrue(Label + TEXT(": at the deadline"), FLureFishingRules::IsInHookWindow(Row, Start, Deadline, Grace));
				TestFalse(Label + TEXT(": 0.1 ms after the deadline"), FLureFishingRules::IsInHookWindow(Row, Start, Deadline + 1.0e-4, Grace));
			}
		}
		TestTrue(TEXT("a negative grace counts as none"), FMath::IsNearlyEqual(FLureFishingRules::HookDeadline(Row, 10.0, -1.f), 10.0 + Window, 1.0e-9));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookJustInsideTheWindowHooks, "Project.Fishing.QA.Hook.JustInsideTheWindowHooks", QAFishing::Flags)
bool FQAFishHookJustInsideTheWindowHooks::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const float Window : { 0.4f, Shipped.HookWindow, 1.5f })
	{
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		Scene.AddShoreSpot();
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped, 0.5f, Window));
		if (!Fishing)
		{
			return false;
		}
		Scene.Tick(10);
		const double BiteStart = CastUntilBite(*this, Scene, Character, Fishing);
		if (BiteStart < 0.0)
		{
			continue;
		}
		Scene.AdvanceTo(BiteStart + Window - 0.005);
		const FString Label = FString::Printf(TEXT("window %.2f s, press 5 ms before it closes"), Window);
		if (!TestEqual(Label + TEXT(": still biting"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Biting)))
		{
			continue;
		}
		const FFishInstance Pending = Fishing->GetPendingFish();
		Fishing->AuthorityHook();
		TestEqual(Label + TEXT(": hooked"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
		TestEqual(Label + TEXT(": result Hooked"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Hooked));
		TestTrue(Label + TEXT(": the fish that bit is on the line"), Fishing->GetHookedFish().IsValid() && Fishing->GetHookedFish().Seed == Pending.Seed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookJustAfterTheWindowMisses, "Project.Fishing.QA.Hook.JustAfterTheWindowMisses", QAFishing::Flags)
bool FQAFishHookJustAfterTheWindowMisses::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const float Window : { 0.4f, Shipped.HookWindow, 1.5f })
	{
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		Scene.AddShoreSpot();
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped, 0.5f, Window));
		if (!Fishing)
		{
			return false;
		}
		Scene.Tick(10);
		const double BiteStart = CastUntilBite(*this, Scene, Character, Fishing);
		if (BiteStart < 0.0)
		{
			continue;
		}
		const FString Label = FString::Printf(TEXT("window %.2f s"), Window);
		Scene.AdvanceTo(BiteStart + Window - 0.005);
		TestEqual(Label + TEXT(": 5 ms before the end the bite is still on"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Biting));
		Scene.AdvanceTo(BiteStart + Window + 0.005);
		TestNotEqual(Label + TEXT(": 5 ms after the end the bite is over"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Biting));
		TestEqual(Label + TEXT(": result Missed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Missed));
		const double MissedAt = Fishing->GetNetState().ResultTime;
		TestTrue(FString::Printf(TEXT("%s: missed at +%.4f s, not before the window closed"), *Label, MissedAt - BiteStart), MissedAt >= BiteStart + Window - 1.0e-6);
		TestFalse(Label + TEXT(": the server dropped the fish"), Fishing->GetPendingFish().IsValid());
		TestFalse(Label + TEXT(": nothing is hooked"), Fishing->GetHookedFish().IsValid());
		Fishing->AuthorityHook();
		TestNotEqual(Label + TEXT(": a press after the miss does not hook"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
		TestFalse(Label + TEXT(": ... and brings no fish back"), Fishing->GetHookedFish().IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookLatePressIsAMiss, "Project.Fishing.QA.Hook.LatePressIsAMiss", QAFishing::Flags)
bool FQAFishHookLatePressIsAMiss::RunTest(const FString& Parameters)
{
	// The press reaches the server after the window closed but before the server's own tick noticed: still a miss.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	const float Window = 0.6f;
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped, 0.5f, Window));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	for (const double Offset : { Window + 0.005, Window - 0.005 })
	{
		const double BiteStart = CastUntilBite(*this, Scene, Character, Fishing);
		if (BiteStart < 0.0)
		{
			return false;
		}
		Fishing->SetComponentTickEnabled(false);
		Scene.AdvanceTo(BiteStart + Offset);
		Fishing->AuthorityHook();
		Fishing->SetComponentTickEnabled(true);
		const bool bLate = Offset > Window;
		const FString Label = FString::Printf(TEXT("press at +%.3f s (window %.2f)"), Offset, Window);
		TestEqual(Label, ResultName(Fishing->GetNetState().LastResult), ResultName(bLate ? ELureFishingResult::Missed : ELureFishingResult::Hooked));
		TestEqual(Label + TEXT(": fish on the line"), Fishing->GetHookedFish().IsValid(), !bLate);
		Fishing->AuthorityReelIn();
		Scene.Tick(2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookWindowComesFromTheTable, "Project.Fishing.QA.Hook.WindowComesFromTheTable", QAFishing::Flags)
bool FQAFishHookWindowComesFromTheTable::RunTest(const FString& Parameters)
{
	// Through DT_Fishing (an edited copy of the CSV), not SetFishingProfile: the window lasts what the data says.
	FString Csv;
	if (!LoadFishingCsv(*this, Csv))
	{
		return false;
	}
	for (const TCHAR* Window : { TEXT("0.35"), TEXT("1.6") })
	{
		FString Edited = WithCell(*this, Csv, TEXT("Default"), TEXT("HookWindow"), Window);
		Edited = WithCell(*this, Edited, TEXT("Default"), TEXT("BiteWaitMin"), TEXT("0.5"));
		Edited = WithCell(*this, Edited, TEXT("Default"), TEXT("BiteWaitMax"), TEXT("0.5"));
		Edited = WithCell(*this, Edited, TEXT("Default"), TEXT("NibblesMax"), TEXT("0"));
		const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(*this, FLureFishingRow::StaticStruct(), Edited, TEXT("DT_Fishing fixture")));
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		Scene.AddShoreSpot();
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
		if (!Fishing)
		{
			return false;
		}
		Fishing->ApplyFishingTable(Table.Get());
		Fishing->SetFishTables(Scene.Fish.Get());
		Fishing->SetRandomSeed(99);
		Fishing->TimeOfDayOverride = 12.f;
		Scene.Tick(10);
		const double BiteStart = CastUntilBite(*this, Scene, Character, Fishing);
		if (BiteStart < 0.0)
		{
			continue;
		}
		Scene.TickUntil([Fishing]() { return !IsState(Fishing, ELureFishingState::Biting); }, 240);
		const double Lasted = Fishing->GetNetState().ResultTime - BiteStart;
		const double Expected = FCString::Atod(Window);
		TestTrue(FString::Printf(TEXT("HookWindow %s in the table: the bite lasted %.3f s"), Window, Lasted), Lasted >= Expected - 1.0e-6 && Lasted <= Expected + Dt + 1.0e-3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookRemoteGraceOnlyForRemotePlayers, "Project.Fishing.QA.Hook.RemoteGraceOnlyForRemotePlayers", QAFishing::Flags)
bool FQAFishHookRemoteGraceOnlyForRemotePlayers::RunTest(const FString& Parameters)
{
	// Spec: remote players get HookLatencyGrace extra hook time on the server; the host and standalone get none.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	const float Window = 0.6f;
	const float Grace = 0.25f; // not the shipped 0.15: the grace comes from data
	FLureFishingRow Profile = FlowProfile(Shipped, 0.5f, Window);
	Profile.HookLatencyGrace = Grace;

	ALurePlayerCharacter* Remote = Scene.Spawn(*this, StandFeet);
	ALurePlayerCharacter* RemoteWithController = Scene.Spawn(*this, StandFeet + FVector(0.f, 120.f, 0.f));
	ALurePlayerCharacter* Host = Scene.Spawn(*this, StandFeet + FVector(0.f, -120.f, 0.f));
	ULureFishingComponent* RemoteFishing = Scene.SetUpFishing(*this, Remote, Profile);
	ULureFishingComponent* RemoteControllerFishing = Scene.SetUpFishing(*this, RemoteWithController, Profile);
	ULureFishingComponent* HostFishing = Scene.SetUpFishing(*this, Host, Profile);
	if (!RemoteFishing || !RemoteControllerFishing || !HostFishing)
	{
		return false;
	}
	Scene.PossessLocally(Host);
	APlayerController* RemoteController = Scene.World->SpawnActor<APlayerController>();
	Scene.Tick(10);

	TestEqual(TEXT("standalone: no grace for anyone (remote-looking pawn)"), RemoteFishing->GetHookGrace(), 0.f);
	TestEqual(TEXT("standalone: no grace for the local player"), HostFishing->GetHookGrace(), 0.f);

	Scene.SetListenServer(true);
	if (!TestEqual(TEXT("QA harness: the world is a listen server"), static_cast<int32>(Scene.World->GetNetMode()), static_cast<int32>(NM_ListenServer)))
	{
		return false;
	}
	if (RemoteController)
	{
		RemoteController->Possess(RemoteWithController);
	}
	TestFalse(TEXT("QA harness: the remote controller is not local"), RemoteController && RemoteController->IsLocalController());
	TestTrue(TEXT("QA harness: the host is locally controlled"), Host->IsLocallyControlled());
	TestNearlyEqual(TEXT("listen server: a remote player's pawn gets HookLatencyGrace"), RemoteControllerFishing->GetHookGrace(), Grace, 1.0e-6f);
	TestNearlyEqual(TEXT("listen server: a pawn nobody here controls counts as remote"), RemoteFishing->GetHookGrace(), Grace, 1.0e-6f);
	TestEqual(TEXT("listen server: the host gets no grace"), HostFishing->GetHookGrace(), 0.f);

	// The grace moves the edge: remote hooks inside window + grace, misses just after it; the host misses just after the window.
	TestEqual(TEXT("remote: 10 ms before window + grace = hooked"), ResultName(HookAfter(*this, Scene, Remote, RemoteFishing, Window + Grace - 0.01)), ResultName(ELureFishingResult::Hooked));
	TestEqual(TEXT("remote: 10 ms after window + grace = missed"), ResultName(HookAfter(*this, Scene, Remote, RemoteFishing, Window + Grace + 0.01)), ResultName(ELureFishingResult::Missed));
	TestEqual(TEXT("host: 10 ms before the window closes = hooked"), ResultName(HookAfter(*this, Scene, Host, HostFishing, Window - 0.01)), ResultName(ELureFishingResult::Hooked));
	TestEqual(TEXT("host: 10 ms after the window = missed (no grace)"), ResultName(HookAfter(*this, Scene, Host, HostFishing, Window + 0.01)), ResultName(ELureFishingResult::Missed));
	if (RemoteController)
	{
		RemoteController->UnPossess();
	}
	Scene.SetListenServer(false);
	return true;
}

// =====================================================================================================================
// Early presses, spook, presses with nothing to hook
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookEarlyPressDuringNibbleFollowsRule, "Project.Fishing.QA.Hook.EarlyPressDuringNibbleFollowsRule", QAFishing::Flags)
bool FQAFishHookEarlyPressDuringNibbleFollowsRule::RunTest(const FString& Parameters)
{
	// Spec: an early press (before a bite, nibbles included) follows EarlyHook: ReelIn, Ignore or Spook.
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const ELureEarlyHookRule Rule : { ELureEarlyHookRule::ReelIn, ELureEarlyHookRule::Ignore, ELureEarlyHookRule::Spook })
	{
		const FString Label = StaticEnum<ELureEarlyHookRule>()->GetNameStringByValue(static_cast<int64>(Rule));
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		Scene.AddShoreSpot();
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		FLureFishingRow Profile = StageProfile(Shipped);
		Profile.EarlyHook = Rule;
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile);
		Scene.Tick(10);
		if (!Fishing || !DriveToStage(*this, Scene, Character, Fishing, EStage::Nibble))
		{
			continue;
		}
		const FLureFishingNetState Before = Fishing->GetNetState();
		const double BiteBefore = Fishing->GetScheduledBiteTime();
		Fishing->AuthorityHook();
		const FLureFishingNetState& After = Fishing->GetNetState();
		switch (Rule)
		{
		case ELureEarlyHookRule::ReelIn:
			TestEqual(Label + TEXT(": the press during a nibble reels in"), StateName(After.State), StateName(ELureFishingState::Idle));
			TestEqual(Label + TEXT(": result ReeledIn"), ResultName(After.LastResult), ResultName(ELureFishingResult::ReeledIn));
			TestEqual(Label + TEXT(": the player's choice (no reason)"), BlockName(After.ResultReason), BlockName(ELureCastBlock::None));
			TestTrue(Label + TEXT(": no bite scheduled any more"), Fishing->GetScheduledBiteTime() < 0.0);
			break;
		case ELureEarlyHookRule::Ignore:
			TestEqual(Label + TEXT(": nothing happens"), StateName(After.State), StateName(ELureFishingState::Waiting));
			TestEqual(Label + TEXT(": no new result"), After.ResultId, Before.ResultId);
			TestEqual(Label + TEXT(": the bite time is unchanged"), Fishing->GetScheduledBiteTime(), BiteBefore);
			TestTrue(Label + TEXT(": the bite still comes"), Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Biting); }, 400));
			break;
		case ELureEarlyHookRule::Spook:
			TestEqual(Label + TEXT(": still waiting"), StateName(After.State), StateName(ELureFishingState::Waiting));
			TestEqual(Label + TEXT(": result Spooked"), ResultName(After.LastResult), ResultName(ELureFishingResult::Spooked));
			TestTrue(Label + TEXT(": the bite comes later"), Fishing->GetScheduledBiteTime() > BiteBefore);
			TestTrue(Label + TEXT(": HUD: too early"), Fishing->GetStatusText().Contains(TEXT("Too early")));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookSpookDelaysBiteAndItsNibbles, "Project.Fishing.QA.Hook.SpookDelaysBiteAndItsNibbles", QAFishing::Flags)
bool FQAFishHookSpookDelaysBiteAndItsNibbles::RunTest(const FString& Parameters)
{
	// Spec: EarlyHook = Spook pushes the bite back by SpookDelay; nibbles are the tells that lead up to the bite.
	// Regression test for T006-B2.
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	const FLureFishingRow Profile = SpookProfile(Shipped);
	for (const double SpookAfter : { 0.5, 4.5 })
	{
		const FString Label = FString::Printf(TEXT("bite due %.1f s after landing, spooked at +%.1f s"), Profile.BiteWaitMin, SpookAfter);
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		Scene.AddShoreSpot();
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile);
		if (!Fishing)
		{
			return false;
		}
		Scene.Tick(10);
		Fishing->AuthorityCast(0.5f, 0.f);
		Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Waiting); }, 180);
		const double Landed = Fishing->GetNetState().StateStartTime;
		const double BiteDue = Fishing->GetScheduledBiteTime();
		TestTrue(Label + TEXT(": QA precondition, the bite is due BiteWait after landing"), FMath::IsNearlyEqual(BiteDue - Landed, static_cast<double>(Profile.BiteWaitMin), 1.0e-3));
		Scene.AdvanceTo(Landed + SpookAfter);
		const uint8 NibblesBeforeSpook = Fishing->GetNetState().NibbleId;
		Fishing->AuthorityHook();
		TestEqual(Label + TEXT(": result Spooked"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Spooked));
		const double NewBite = Fishing->GetScheduledBiteTime();
		TestTrue(FString::Printf(TEXT("%s: the bite moves back by SpookDelay %.1f s (was +%.2f s, now +%.2f s after landing; expected +%.2f s)"), *Label, Profile.SpookDelay,
			BiteDue - Landed, NewBite - Landed, BiteDue - Landed + Profile.SpookDelay), FMath::IsNearlyEqual(NewBite, BiteDue + Profile.SpookDelay, 1.0e-3));
		if (SpookAfter < 1.0)
		{
			// No nibble had come yet: both still come, and before the (later) bite.
			TestEqual(Label + TEXT(": QA precondition, no nibble before the spook"), static_cast<int32>(NibblesBeforeSpook), 0);
			Scene.TickUntil([Fishing]() { return !IsState(Fishing, ELureFishingState::Waiting); }, 1200);
			TestEqual(Label + TEXT(": the bite came"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Biting));
			TestEqual(FString::Printf(TEXT("%s: both nibbles still lead up to the bite (%d came)"), *Label, Fishing->GetNetState().NibbleId - NibblesBeforeSpook),
				static_cast<int32>(Fishing->GetNetState().NibbleId - NibblesBeforeSpook), Profile.NibblesMin);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookPressWithNothingToHookReelsIn, "Project.Fishing.QA.Hook.PressWithNothingToHookReelsIn", QAFishing::Flags)
bool FQAFishHookPressWithNothingToHookReelsIn::RunTest(const FString& Parameters)
{
	// Spec: on land or with nothing biting, a press always reels in (whatever EarlyHook says).
	// T-027: open water without a spot now has fish (fish anywhere); "nothing biting" is now water too shallow for fish.
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const ELureEarlyHookRule Rule : { ELureEarlyHookRule::Ignore, ELureEarlyHookRule::Spook, ELureEarlyHookRule::ReelIn })
	{
		const FString Rule_ = StaticEnum<ELureEarlyHookRule>()->GetNameStringByValue(static_cast<int64>(Rule));
		for (const bool bOnLand : { true, false })
		{
			const FString Label = Rule_ + (bOnLand ? TEXT(", bobber on a rock") : TEXT(", water too shallow for fish"));
			FScene Scene;
			if (!Scene.Create(*this))
			{
				return false;
			}
			if (bOnLand)
			{
				Scene.AddBox(FVector(900.f, 0.f, 20.f), FVector(500.f, 500.f, 20.f)); // a rock 40 cm above the sea, 4-14 m out
				Scene.AddShoreSpot(); // the rock is inside the spot: land still wins
			}
			else
			{
				Scene.AddBox(FVector(900.f, 0.f, -55.f), FVector(500.f, 500.f, 50.f)); // a sandbar 5 cm under the sea, 4-14 m out
				Scene.AddShoreSpot(); // a spot there does not make it deeper
			}
			ALurePlayerCharacter* Character = Scene.Spawn(*this);
			FLureFishingRow Profile = FlowProfile(Shipped, 2.f);
			Profile.EarlyHook = Rule;
			ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile);
			if (!Fishing)
			{
				return false;
			}
			Scene.Tick(10);
			Fishing->AuthorityCast(0.2f, 0.f);
			Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Waiting); }, 180);
			TestEqual(Label + TEXT(": QA precondition, on land"), Fishing->GetNetState().bOnWater, !bOnLand);
			Scene.Tick(30);
			Fishing->AuthorityHook();
			TestEqual(Label + TEXT(": a press reels in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
			TestEqual(Label + TEXT(": result ReeledIn"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::ReeledIn));
		}
	}
	return true;
}

// =====================================================================================================================
// Miss, hook, land
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookMissLosesTheFishForGood, "Project.Fishing.QA.Hook.MissLosesTheFishForGood", QAFishing::Flags)
bool FQAFishHookMissLosesTheFishForGood::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const bool bMissEndsCast : { false, true })
	{
		const FString Label = bMissEndsCast ? TEXT("MissEndsCast True") : TEXT("MissEndsCast False");
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		Scene.AddShoreSpot();
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		FLureFishingRow Profile = FlowProfile(Shipped, 0.5f, 0.4f);
		Profile.MissEndsCast = bMissEndsCast;
		Profile.RebiteWaitMin = Profile.RebiteWaitMax = 1.7f;
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile);
		if (!Fishing)
		{
			return false;
		}
		Scene.Tick(10);
		if (CastUntilBite(*this, Scene, Character, Fishing) < 0.0)
		{
			continue;
		}
		const FFishInstance Missed = Fishing->GetPendingFish();
		Scene.TickUntil([Fishing]() { return !IsState(Fishing, ELureFishingState::Biting); }, 120);
		TestEqual(Label + TEXT(": missed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Missed));
		TestFalse(Label + TEXT(": the rolled fish is gone on the server"), Fishing->GetPendingFish().IsValid());
		TestFalse(Label + TEXT(": nothing is hooked"), Fishing->GetHookedFish().IsValid());
		TestFalse(Label + TEXT(": nothing is landed"), Fishing->GetLastLandedFish().IsValid());
		if (bMissEndsCast)
		{
			TestEqual(Label + TEXT(": the line comes in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
			TestTrue(Label + TEXT(": no bite scheduled"), Fishing->GetScheduledBiteTime() < 0.0);
			Scene.Tick(300);
			TestEqual(Label + TEXT(": still idle 5 s later"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
			continue;
		}
		TestEqual(Label + TEXT(": the bobber stays"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
		const double MissedAt = Fishing->GetNetState().ResultTime;
		TestTrue(FString::Printf(TEXT("%s: a new bite after RebiteWait 1.7 s (in %.3f s)"), *Label, Fishing->GetScheduledBiteTime() - MissedAt),
			FMath::IsNearlyEqual(Fishing->GetScheduledBiteTime() - MissedAt, 1.7, 1.0e-3));
		if (!TestTrue(Label + TEXT(": the rebite comes"), Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Biting); }, 240)))
		{
			continue;
		}
		TestNotEqual(Label + TEXT(": the rebite is a new roll (new seed)"), Fishing->GetPendingFish().Seed, Missed.Seed);
		Fishing->AuthorityHook();
		TestTrue(Label + TEXT(": hooking the rebite hooks the new fish, not the missed one"), Fishing->GetHookedFish().IsValid() && Fishing->GetHookedFish().Seed != Missed.Seed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookHookedAndLandedFishIsTheBite, "Project.Fishing.QA.Hook.HookedAndLandedFishIsTheBite", QAFishing::Flags)
bool FQAFishHookHookedAndLandedFishIsTheBite::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	FLureFishingRow Profile = FlowProfile(Shipped);
	Profile.AutoLandDelay = 0.7f;
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile, 12.f, 4242);
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	for (int32 Catch = 0; Catch < 3; ++Catch)
	{
		const FString Label = FString::Printf(TEXT("catch %d"), Catch + 1);
		if (CastUntilBite(*this, Scene, Character, Fishing) < 0.0)
		{
			return false;
		}
		const FFishInstance Bite = Fishing->GetPendingFish();
		TestTrue(Label + TEXT(": the bite is a real rolled fish"), Bite.IsValid() && Bite.WeightKg > 0.f && Bite.Value >= 1);
		Fishing->AuthorityHook();
		const FFishInstance HookedFish = Fishing->GetHookedFish();
		const TArray<FString> HookDiff = DifferentFields(FFishInstance::StaticStruct(), &HookedFish, &Bite);
		TestEqual(FString::Printf(TEXT("%s: the hooked fish is exactly the bite (differs in: %s)"), *Label, *FString::Join(HookDiff, TEXT(", "))), HookDiff.Num(), 0);
		const double HookedAt = Fishing->GetNetState().StateStartTime;
		TestTrue(Label + TEXT(": lands"), Scene.TickUntil([Fishing]() { return IsState(Fishing, ELureFishingState::Idle); }, 240));
		const double LandedAt = Fishing->GetNetState().ResultTime;
		TestTrue(FString::Printf(TEXT("%s: landed %.3f s after the hook (AutoLandDelay 0.7)"), *Label, LandedAt - HookedAt), LandedAt - HookedAt >= 0.7 - 1.0e-6 && LandedAt - HookedAt <= 0.7 + Dt + 1.0e-3);
		TestEqual(Label + TEXT(": result Landed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
		const FFishInstance LandedFish = Fishing->GetLastLandedFish();
		const TArray<FString> LandDiff = DifferentFields(FFishInstance::StaticStruct(), &LandedFish, &Bite);
		TestEqual(FString::Printf(TEXT("%s: the landed fish is exactly the bite (differs in: %s)"), *Label, *FString::Join(LandDiff, TEXT(", "))), LandDiff.Num(), 0);
		TestFalse(Label + TEXT(": nothing left on the line"), Fishing->GetHookedFish().IsValid());
		TestTrue(Label + TEXT(": HUD: caught"), Fishing->GetStatusText().Contains(TEXT("Caught:")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookPressWhileHookedChangesNothing, "Project.Fishing.QA.Hook.PressWhileHookedChangesNothing", QAFishing::Flags)
bool FQAFishHookPressWhileHookedChangesNothing::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped)); // AutoLandDelay 0: stays hooked (T-007 mode)
	if (!Fishing || CastUntilBite(*this, Scene, Character, Fishing) < 0.0)
	{
		return false;
	}
	Fishing->AuthorityHook();
	const FLureFishingNetState Hooked = Fishing->GetNetState();
	const FFishInstance Fish = Fishing->GetHookedFish();
	for (int32 Press = 0; Press < 5; ++Press)
	{
		Fishing->AuthorityHook();
		Fishing->PressHook();
		Fishing->PressCast();
		Scene.Tick(6);
	}
	TestEqual(TEXT("still hooked"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
	TestEqual(TEXT("no new result"), Fishing->GetNetState().ResultId, Hooked.ResultId);
	TestEqual(TEXT("the same fish"), Fishing->GetHookedFish().Seed, Fish.Seed);
	Scene.Tick(600);
	TestEqual(TEXT("AutoLandDelay 0: still hooked 10 s later (the reel fight, T-007, takes over)"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
	return true;
}

// =====================================================================================================================
// Determinism and the one roll pipeline
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookBiteSequenceDeterministicWithSeed, "Project.Fishing.QA.Hook.BiteSequenceDeterministicWithSeed", QAFishing::Flags)
bool FQAFishHookBiteSequenceDeterministicWithSeed::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	FLureFishingRow Profile = Shipped; // shipped random ranges: waits 4-10 s, 0-2 nibbles
	Profile.AutoLandDelay = 0.f;
	const TArray<FBiteRecord> First = RecordBites(*this, Profile, 777);
	const TArray<FBiteRecord> Second = RecordBites(*this, Profile, 777);
	const TArray<FBiteRecord> Other = RecordBites(*this, Profile, 778);
	if (!TestTrue(TEXT("three bites each run"), First.Num() == 3 && Second.Num() == 3 && Other.Num() == 3))
	{
		return false;
	}
	bool bAnyDifferent = false;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FString Label = FString::Printf(TEXT("cast %d"), Index + 1);
		TestTrue(FString::Printf(TEXT("%s: same seed, same wait (%.4f vs %.4f s)"), *Label, First[Index].WaitAfterLanding, Second[Index].WaitAfterLanding),
			FMath::IsNearlyEqual(First[Index].WaitAfterLanding, Second[Index].WaitAfterLanding, 1.0e-6));
		TestEqual(Label + TEXT(": same seed, same nibbles"), First[Index].Nibbles, Second[Index].Nibbles);
		const TArray<FString> Diff = DifferentFields(FFishInstance::StaticStruct(), &First[Index].Fish, &Second[Index].Fish);
		TestEqual(FString::Printf(TEXT("%s: same seed, same fish (differs in: %s)"), *Label, *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
		TestTrue(FString::Printf(TEXT("%s: the wait is within [BiteWaitMin, BiteWaitMax] (%.3f s)"), *Label, First[Index].WaitAfterLanding),
			First[Index].WaitAfterLanding >= Profile.BiteWaitMin - Dt && First[Index].WaitAfterLanding <= Profile.BiteWaitMax + Dt);
		bAnyDifferent |= !FMath::IsNearlyEqual(First[Index].WaitAfterLanding, Other[Index].WaitAfterLanding, 1.0e-6) || First[Index].Fish.Seed != Other[Index].Fish.Seed;
	}
	TestTrue(TEXT("another seed gives another sequence"), bAnyDifferent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookServerNeverForcesTheRoll, "Project.Fishing.QA.Hook.ServerNeverForcesTheRoll", QAFishing::Flags)
bool FQAFishHookServerNeverForcesTheRoll::RunTest(const FString& Parameters)
{
	// The bite goes through the one roll pipeline with a real roll: no forced rarity, modifiers or weight (dev/test-only knobs).
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	TSet<int32> Seeds;
	for (int32 Cast = 0; Cast < 8; ++Cast)
	{
		if (CastUntilBite(*this, Scene, Character, Fishing) < 0.0)
		{
			return false;
		}
		const FFishRollContext& Context = Fishing->GetLastRollContext();
		TestTrue(TEXT("no forced rarity"), Context.ForcedRarityId.IsNone());
		TestFalse(TEXT("no forced modifiers"), Context.bForceModifiers || Context.ForcedModifierIds.Num() > 0);
		TestFalse(TEXT("no forced weight"), Context.bForceWeightFraction);
		TestTrue(TEXT("the default bait (Bait.Shrimp) is on the hook"), Context.BaitTag == Tag(TEXT("Bait.Shrimp")) && Context.BaitTag.IsValid());
		TestEqual(TEXT("the roll's seed is the fish's seed"), Fishing->GetPendingFish().Seed, Context.Seed);
		Seeds.Add(Context.Seed);
		Fishing->AuthorityReelIn();
		Scene.Tick(2);
	}
	TestTrue(FString::Printf(TEXT("every bite rolls with a fresh seed (%d distinct of 8)"), Seeds.Num()), Seeds.Num() == 8);
	return true;
}

// =====================================================================================================================
// Bite waits and nibbles (pure)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookBiteWaitWithinDataRange, "Project.Fishing.QA.Hook.BiteWaitWithinDataRange", QAFishing::Flags)
bool FQAFishHookBiteWaitWithinDataRange::RunTest(const FString& Parameters)
{
	FLureFishingRow Row;
	if (!ShippedFishingRow(*this, Row))
	{
		return false;
	}
	for (const bool bAfterMiss : { false, true })
	{
		const float Min = bAfterMiss ? Row.RebiteWaitMin : Row.BiteWaitMin;
		const float Max = bAfterMiss ? Row.RebiteWaitMax : Row.BiteWaitMax;
		const FString Label = FString::Printf(TEXT("%s [%.1f, %.1f]"), bAfterMiss ? TEXT("rebite") : TEXT("first bite"), Min, Max);
		FRandomStream Rng(31337);
		float Lowest = TNumericLimits<float>::Max();
		float Highest = -TNumericLimits<float>::Max();
		bool bInside = true;
		for (int32 Sample = 0; Sample < 10000; ++Sample)
		{
			const float Wait = FLureFishingRules::RandomBiteWait(Row, Rng, bAfterMiss);
			bInside &= Wait >= Min && Wait <= Max;
			Lowest = FMath::Min(Lowest, Wait);
			Highest = FMath::Max(Highest, Wait);
		}
		TestTrue(Label + TEXT(": every wait inside the range"), bInside);
		TestTrue(FString::Printf(TEXT("%s: the whole range is used (%.3f..%.3f)"), *Label, Lowest, Highest), Lowest < Min + 0.01f * (Max - Min) && Highest > Max - 0.01f * (Max - Min));
	}
	FLureFishingRow Fixed = Row;
	Fixed.BiteWaitMin = Fixed.BiteWaitMax = 6.5f;
	FRandomStream Rng(5);
	TestNearlyEqual(TEXT("Min == Max: exactly that wait"), FLureFishingRules::RandomBiteWait(Fixed, Rng, false), 6.5f, 1.0e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishHookNibblesWithinDataRange, "Project.Fishing.QA.Hook.NibblesWithinDataRange", QAFishing::Flags)
bool FQAFishHookNibblesWithinDataRange::RunTest(const FString& Parameters)
{
	// Spec: before the bite, [NibblesMin, NibblesMax] nibbles, about NibbleInterval apart; nibbles are tells only (they end before the bite).
	FLureFishingRow Row;
	if (!ShippedFishingRow(*this, Row))
	{
		return false;
	}
	TArray<int32> CountSeen;
	CountSeen.SetNumZeroed(Row.NibblesMax + 1);
	int32 OutOfRange = 0;
	int32 Unordered = 0;
	int32 AfterBite = 0;
	int32 NotPositive = 0;
	for (int32 Seed = 0; Seed < 5000; ++Seed)
	{
		for (const bool bAfterMiss : { false, true })
		{
			FRandomStream Rng(Seed);
			const float Wait = FLureFishingRules::RandomBiteWait(Row, Rng, bAfterMiss);
			const TArray<float> Times = FLureFishingRules::NibbleTimes(Row, Rng, Wait);
			if (Times.Num() < Row.NibblesMin || Times.Num() > Row.NibblesMax)
			{
				++OutOfRange;
			}
			else
			{
				++CountSeen[Times.Num()];
			}
			for (int32 Index = 0; Index < Times.Num(); ++Index)
			{
				NotPositive += Times[Index] > 0.f ? 0 : 1;
				AfterBite += Times[Index] + Row.NibbleDuration < Wait ? 0 : 1;
				Unordered += (Index > 0 && Times[Index] < Times[Index - 1]) ? 1 : 0;
			}
		}
	}
	TestEqual(FString::Printf(TEXT("the nibble count is always within [%d, %d]"), Row.NibblesMin, Row.NibblesMax), OutOfRange, 0);
	for (int32 Count = Row.NibblesMin; Count <= Row.NibblesMax; ++Count)
	{
		TestTrue(FString::Printf(TEXT("%d nibbles happen (%d of 10000)"), Count, CountSeen[Count]), CountSeen[Count] > 0);
	}
	TestEqual(TEXT("nibble times ascend"), Unordered, 0);
	TestEqual(TEXT("nibbles come after landing"), NotPositive, 0);
	TestEqual(TEXT("every nibble ends before the bite"), AfterBite, 0);

	FLureFishingRow None = Row;
	None.NibblesMin = None.NibblesMax = 0;
	FRandomStream Rng(1);
	TestEqual(TEXT("NibblesMax 0: no nibbles"), FLureFishingRules::NibbleTimes(None, Rng, 8.f).Num(), 0);
	return true;
}

} // namespace QAFishing

#endif // WITH_DEV_AUTOMATION_TESTS

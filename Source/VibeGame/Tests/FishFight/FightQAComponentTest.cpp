// QA-owned integration tests for the reel fight in ULureFishingComponent (T-007):
// Project.Fishing.Fight.QA.{Authority,Replication,Landing,Pipeline,Component,Data}.*
// Written by the qa-engineer from docs/specs/reel-fight-rules.md and the component's header contract (black-box).
// Worlds are transient FTestWorldWrapper game worlds (an 8 x 8 m dock over a Lure.Water surface). "Clients" are copies of
// the component whose owner has a non-authority role; replication is simulated through the engine's replication layout
// (FRepLayout bits written from the server object and read into the copy, RepNotifies called like the net driver does).

#include "FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace LureFightQA
{
	namespace ComponentQA
	{
		/** The reference Coral Snapper (Common, 2.5 kg = its ReferenceWeight; level 3 vs a level 1 player). */
		inline bool ReferenceSnapper(FAutomationTestBase& Test, const FishQA::FTables& Fish, FFishInstance& Out)
		{
			return RollFish(Test, Fish, TEXT("CoralSnapper"), TEXT("Common"), (2.5f - 0.8f) / (7.f - 0.8f), 21, Out);
		}

		inline FLureGearLoadout Loadout(FName Rod, FName Line, FName Hook)
		{
			FLureGearLoadout Out;
			Out.Rod = Rod;
			Out.Line = Line;
			Out.Hook = Hook;
			return Out;
		}

		/** T-030 (unreal-engineer): a landed fish hangs on the angler's hook until it is grabbed or let go, and a cast is Busy
		 *  until then. Takes it off the hook (and away) so the next cast starts. */
		inline void TakeFishOffTheHook(ULureFishingComponent* Fishing)
		{
			ULureHandsComponent* Hands = Fishing && Fishing->GetOwner() ? Fishing->GetOwner()->FindComponentByClass<ULureHandsComponent>() : nullptr;
			if (ALureFishItem* Fish = Hands ? Hands->AuthorityReleaseHanging() : nullptr)
			{
				Fish->Destroy();
			}
		}

		/** A pattern table with one row (the species' FightPatternId) holding Pattern. */
		inline TStrongObjectPtr<UDataTable> OnePatternTable(FName RowName, const FLureFightPatternRow& Pattern)
		{
			TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
			Table->RowStruct = FLureFightPatternRow::StaticStruct();
			Table->AddRow(RowName, Pattern);
			return Table;
		}

		/** The shipped DT_Gear rows plus extra CSV lines. */
		inline bool GearWithExtraRows(FAutomationTestBase& Test, const FFightTables& Data, const TCHAR* ExtraLines, TStrongObjectPtr<UDataTable>& Out)
		{
			FString Csv = Data.GearCsv;
			if (!Csv.EndsWith(TEXT("\n")))
			{
				Csv += TEXT("\n");
			}
			Csv += ExtraLines;
			return MakeTableChecked(Test, Out, FLureGearRow::StaticStruct(), Csv, false, TEXT("fixture DT_Gear (shipped + extra rows)"));
		}

		/** A stand-in for another machine's copy: it never moves on its own (its movement is replicated in the real game). */
		inline void MakeCopy(ALurePlayerCharacter* Character, ENetRole Role)
		{
			Character->GetCharacterMovement()->SetComponentTickEnabled(false);
			Character->SetRole(Role);
		}
	}

	// =================================================================================================================
	// Server authority
	// =================================================================================================================

	/** A client copy can't land a fish, change the tension, hook or re-gear: it only asks, and forging its copy changes nothing. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAAuthorityClient, "Project.Fishing.Fight.QA.Authority.ClientCannotLandOrSetTension", Flags)
	bool FLureFightQAAuthorityClient::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Bonefish;
		if (!RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 51, Bonefish))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* ServerCharacter = World.Spawn(StandAt());
		ALurePlayerCharacter* ClientCharacter = World.Spawn(StandAt() + FVector(0.f, 250.f, 0.f));
		ULureFishingComponent* Server = SetUpFishing(ServerCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		ULureFishingComponent* Client = SetUpFishing(ClientCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!TestNotNull(TEXT("server"), Server) || !TestNotNull(TEXT("client"), Client) || !CastAndWait(*this, World, Server) || !TestTrue(TEXT("the server hooks"), Server->AuthorityHookFish(Bonefish)))
		{
			return false;
		}
		ComponentQA::MakeCopy(ClientCharacter, ROLE_AutonomousProxy);
		ReplicateFishing(*this, Server, Client);
		TestEqual(TEXT("the client sees the fish on"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Hooked));
		TestTrue(TEXT("the client sees the fight"), Client->GetFightNet().bActive);

		int32 ClientLanded = 0;
		Client->OnFishLandedNative.AddLambda([&ClientLanded](ULureFishingComponent*, const FFishInstance&) { ++ClientLanded; });
		const FLureGearLoadout Before = Client->GetLoadout();
		TestFalse(TEXT("client: can't hook a fish of its choice"), Client->AuthorityHookFish(Bonefish));
		TestFalse(TEXT("client: can't change its gear"), Client->AuthoritySetLoadout(ComponentQA::Loadout(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Squid"))));
		TestTrue(TEXT("client: ... its loadout is unchanged"), Client->GetLoadout() == Before);
		TestFalse(TEXT("client: can't cast"), Client->AuthorityCast(0.5f, 0.f));
		Client->AuthoritySetReeling(true);
		TestFalse(TEXT("client: its reel input is only a request (no server reel state)"), Client->IsServerReeling());
		if (UFunction* Rpc = Client->FindFunction(TEXT("ServerSetReeling")))
		{
			struct { bool bReeling; } Params{ true };
			Client->ProcessEvent(Rpc, &Params);
			TestFalse(TEXT("client: the ServerSetReeling RPC does not reel on the client's own copy"), Client->IsServerReeling());
		}
		Client->AuthorityReelIn();
		Client->AuthorityHook();
		TestEqual(TEXT("client: reel-in and hook requests don't change its copy"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Hooked));

		// A hacked client rewrites its replicated copies: the fish "at its feet", no tension, outcome Landed, fight over.
		FLureFightNetState* Forged = ReplicatedField<FLureFightNetState>(Client, TEXT("FightNet"));
		if (TestNotNull(TEXT("FightNet is reachable by reflection"), Forged))
		{
			Forged->LineOut = 0.f;
			Forged->Tension = 0.f;
			Forged->Outcome = ELureFightOutcome::Landed;
		}
		const int32 ServerStepsBefore = Server->GetFightState().Steps;
		World.Tick(60);
		TestEqual(TEXT("client: OnFishLanded never fires on a client copy"), ClientLanded, 0);
		TestFalse(TEXT("client: no landed fish"), Client->GetLastLandedFish().IsValid());
		TestEqual(TEXT("client: its copy never simulates the fight"), Client->GetFightState().Steps, 0);
		TestEqual(TEXT("server: still fighting"), StateName(Server->GetFishingState()), StateName(ELureFishingState::Hooked));
		TestTrue(TEXT("server: its own simulation went on"), Server->GetFightState().Steps > ServerStepsBefore);
		TestTrue(FString::Printf(TEXT("server: the fish is still out (%.0f cm)"), Server->GetFightNet().LineOut), Server->GetFightNet().LineOut > Server->GetFightTuning().LandDistance);
		TestFalse(TEXT("server: the forged request to reel did not reach it"), Server->IsServerReeling());
		ReplicateFishing(*this, Server, Client);
		TestEqual(TEXT("the next update replaces the forged copy with the server's truth"), Client->GetFightNet().LineOut, Server->GetFightNet().LineOut);
		TestEqual(TEXT("... outcome back to None"), OutcomeName(Client->GetFightNet().Outcome), OutcomeName(ELureFightOutcome::None));
		ClientCharacter->SetRole(ROLE_Authority);
		return true;
	}

	/** While a fish is on, the server refuses a new cast and a new hook, and the fight goes on untouched. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAAuthorityBusy, "Project.Fishing.Fight.QA.Authority.RequestsDuringFightDontReset", Flags)
	bool FLureFightQAAuthorityBusy::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Bonefish;
		FFishInstance Other;
		if (!RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 52, Bonefish) || !RollFish(*this, Fish, TEXT("CoralSnapper"), TEXT("Rare"), 1.f, 53, Other))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt()), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
		{
			return false;
		}
		World.Tick(20);
		const int32 Steps = Fishing->GetFightState().Steps;
		const float LineOut = Fishing->GetFightState().LineOut;
		TestFalse(TEXT("a cast while the fish is on is refused"), Fishing->AuthorityCast(1.f, 90.f));
		TestFalse(TEXT("hooking another fish while one is on is refused"), Fishing->AuthorityHookFish(Other));
		Fishing->AuthorityHook();
		TestEqual(TEXT("still hooked"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
		TestTrue(TEXT("the same fish"), SameFish(Fishing->GetHookedFish(), Bonefish));
		TestTrue(TEXT("the fight still runs"), Fishing->GetFightNet().bActive);
		TestEqual(TEXT("the fight was not restarted"), Fishing->GetFightState().Steps, Steps);
		TestEqual(TEXT("... nor its line"), Fishing->GetFightState().LineOut, LineOut);
		World.Tick(2);
		TestTrue(TEXT("... and it goes on"), Fishing->GetFightState().Steps > Steps);
		return true;
	}

	// =================================================================================================================
	// Replication
	// =================================================================================================================

	/** The loadout and the fight state go to every machine (COND_None): the owner's HUD and every other player's view need them. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAReplicationConditions, "Project.Fishing.Fight.QA.Replication.PropsReachOwnerAndProxies", Flags)
	bool FLureFightQAReplicationConditions::RunTest(const FString& Parameters)
	{
		UClass* Class = ULureFishingComponent::StaticClass();
		// Like FRepLayout does before it reads RepIndex (without it RepIndex is unassigned and the engine's duplicate check asserts).
		Class->SetUpRuntimeReplicationData();
		TArray<FLifetimeProperty> Lifetime;
		GetDefault<ULureFishingComponent>()->GetLifetimeReplicatedProps(Lifetime);
		for (const TCHAR* Name : { TEXT("Loadout"), TEXT("FightNet"), TEXT("HookedFish"), TEXT("LastLandedFish"), TEXT("NetState") })
		{
			const FProperty* Property = FindFProperty<FProperty>(Class, Name);
			if (!TestNotNull(FString::Printf(TEXT("%s exists"), Name), Property))
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("%s is a replicated property"), Name), Property->HasAnyPropertyFlags(CPF_Net));
			const FLifetimeProperty* Entry = Lifetime.FindByPredicate([Property](const FLifetimeProperty& P) { return P.RepIndex == Property->RepIndex; });
			if (TestNotNull(FString::Printf(TEXT("%s is registered in GetLifetimeReplicatedProps"), Name), Entry))
			{
				TestEqual(FString::Printf(TEXT("%s goes to the owner AND to other players (COND_None)"), Name), static_cast<int32>(Entry->Condition), static_cast<int32>(COND_None));
			}
		}
		const FProperty* Loadout = FindFProperty<FProperty>(Class, TEXT("Loadout"));
		TestTrue(TEXT("Loadout has a RepNotify (clients re-resolve the gear numbers)"), Loadout && Loadout->HasAnyPropertyFlags(CPF_RepNotify) && Class->FindFunctionByName(Loadout->RepNotifyFunc) != nullptr);

		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = World.Spawn(StandAt());
		if (TestNotNull(TEXT("character"), Character))
		{
			TestTrue(TEXT("the player character replicates"), Character->GetIsReplicated());
			TestTrue(TEXT("its fishing component replicates"), Character->GetFishing() && Character->GetFishing()->GetIsReplicated());
		}
		return true;
	}

	/** FLureFightNetState and FLureGearLoadout survive the engine's replication path exactly, within a small size. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAReplicationRoundTrip, "Project.Fishing.Fight.QA.Replication.FightStateRoundTrips", Flags)
	bool FLureFightQAReplicationRoundTrip::RunTest(const FString& Parameters)
	{
		static_assert(!TStructOpsTypeTraits<FLureFightNetState>::WithNetSerializer, "FLureFightNetState now has a custom NetSerialize: test it here");
		static_assert(!TStructOpsTypeTraits<FLureGearLoadout>::WithNetSerializer, "FLureGearLoadout now has a custom NetSerialize: test it here");
		FLureFightNetState Sample;
		Sample.bActive = true;
		Sample.FightId = 200;
		Sample.bReeling = true;
		Sample.bExhausted = true;
		Sample.Outcome = ELureFightOutcome::Spooled;
		Sample.PatternId = TEXT("Dive");
		Sample.MoveId = TEXT("Sulk");
		Sample.Tension = 12.5f;
		Sample.LineStrength = 22.f;
		Sample.SlackTension = 1.75f;
		Sample.Stamina = 0.375f;
		Sample.LineOut = 1234.5f;
		Sample.SpoolLength = 6000.f;
		Sample.Depth = 88.25f;
		Sample.SideDeg = -33.5f;
		Sample.SnapProgress = 0.5f;
		Sample.SlackProgress = 0.25f;
		FLureFightNetState Defaults;
		for (FLureFightNetState* In : { &Sample, &Defaults })
		{
			FLureFightNetState Out;
			Out.Tension = -1.f; // make sure the read writes every field
			int64 Bits = 0;
			if (NetRoundTrip(*this, FLureFightNetState::StaticStruct(), In, &Out, Bits))
			{
				TestTrue(FString::Printf(TEXT("FLureFightNetState round trip (%s, %lld bits)"), In == &Sample ? TEXT("every field set") : TEXT("defaults"), Bits),
					FLureFightNetState::StaticStruct()->CompareScriptStruct(In, &Out, PPF_None));
				if (In == &Sample)
				{
					AddInfo(FString::Printf(TEXT("a full fight update is %lld bytes"), (Bits + 7) / 8));
					TestTrue(FString::Printf(TEXT("a full fight update stays small (%lld bytes <= 128)"), (Bits + 7) / 8), (Bits + 7) / 8 <= 128);
				}
			}
		}
		FLureGearLoadout Loadout = ComponentQA::Loadout(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Squid"));
		FLureGearLoadout LoadoutOut;
		int64 Bits = 0;
		if (NetRoundTrip(*this, FLureGearLoadout::StaticStruct(), &Loadout, &LoadoutOut, Bits))
		{
			TestTrue(FString::Printf(TEXT("FLureGearLoadout round trip (%lld bits)"), Bits), LoadoutOut == Loadout);
		}
		return true;
	}

	/** An owner copy and a simulated proxy follow the server's loadout and fight through replication, and never simulate or land. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAReplicationProxies, "Project.Fishing.Fight.QA.Replication.ProxyFollowsServerFight", Flags)
	bool FLureFightQAReplicationProxies::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Bonefish;
		if (!RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Uncommon"), 0.2f, 81, Bonefish))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* ServerCharacter = World.Spawn(StandAt());
		ALurePlayerCharacter* OwnerCharacter = World.Spawn(StandAt() + FVector(0.f, 250.f, 0.f));
		ALurePlayerCharacter* ProxyCharacter = World.Spawn(StandAt() + FVector(0.f, -250.f, 0.f));
		ULureFishingComponent* Server = SetUpFishing(ServerCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		ULureFishingComponent* Owner = SetUpFishing(OwnerCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		ULureFishingComponent* Proxy = SetUpFishing(ProxyCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!Server || !Owner || !Proxy)
		{
			AddError(TEXT("characters without fishing components"));
			return false;
		}
		ComponentQA::MakeCopy(OwnerCharacter, ROLE_AutonomousProxy);
		ComponentQA::MakeCopy(ProxyCharacter, ROLE_SimulatedProxy);
		const TPair<const TCHAR*, ULureFishingComponent*> Copies[] = { { TEXT("owner"), Owner }, { TEXT("proxy"), Proxy } };
		int32 CopiesLanded = 0;
		for (const TPair<const TCHAR*, ULureFishingComponent*>& Copy : Copies)
		{
			Copy.Value->OnFishLandedNative.AddLambda([&CopiesLanded](ULureFishingComponent*, const FFishInstance&) { ++CopiesLanded; });
		}

		// Gear: each change on the server reaches both copies, which resolve the same numbers (their cache follows OnRep_Loadout).
		const FLureGearLoadout Kits[] = { ComponentQA::Loadout(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Shrimp")), GetDefault<ULureFishingSettings>()->DefaultLoadout,
			ComponentQA::Loadout(TEXT("Rod_Starter"), TEXT("Line_Braid"), TEXT("Hook_Squid")) };
		for (const TPair<const TCHAR*, ULureFishingComponent*>& Copy : Copies)
		{
			Copy.Value->GetGearStats(); // the copy has already resolved (and cached) the default gear before the first update
		}
		for (const FLureGearLoadout& Kit : Kits)
		{
			if (!TestTrue(TEXT("the server equips ") + Kit.Rod.ToString() + TEXT("/") + Kit.Line.ToString() + TEXT("/") + Kit.Hook.ToString(), Server->AuthoritySetLoadout(Kit)))
			{
				continue;
			}
			const FLureGearStats ServerGear = Server->GetGearStats();
			for (const TPair<const TCHAR*, ULureFishingComponent*>& Copy : Copies)
			{
				ReplicateFishing(*this, Server, Copy.Value);
				const FLureGearStats CopyGear = Copy.Value->GetGearStats();
				TestTrue(FString::Printf(TEXT("%s: the replicated loadout"), Copy.Key), Copy.Value->GetLoadout() == Kit);
				TestTrue(FString::Printf(TEXT("%s: the same gear numbers as the server (%s line %.0f)"), Copy.Key, *CopyGear.LineId.ToString(), CopyGear.LineStrength),
					FLureGearStats::StaticStruct()->CompareScriptStruct(&CopyGear, &ServerGear, PPF_None));
			}
		}
		Server->AuthoritySetLoadout(ComponentQA::Loadout(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Shrimp")));

		// The fight: every update reaches both copies; they show it and never simulate it.
		if (!CastAndWait(*this, World, Server, 0.25f) || !TestTrue(TEXT("the server hooks"), Server->AuthorityHookFish(Bonefish)))
		{
			return false;
		}
		Server->AuthoritySetReeling(true);
		int32 Updates = 0;
		bool bAllMatch = true;
		bool bBobberRides = true;
		bool bNeverSimulated = true;
		bool bHud = true;
		for (int32 Frame = 0; Frame < 60 * 60 && Server->GetFishingState() == ELureFishingState::Hooked; Frame += 6)
		{
			World.Tick(6);
			for (const TPair<const TCHAR*, ULureFishingComponent*>& Copy : Copies)
			{
				ReplicateFishing(*this, Server, Copy.Value);
				const FLureFightNetState ServerFight = Server->GetFightNet();
				const FLureFightNetState CopyFight = Copy.Value->GetFightNet();
				bAllMatch &= FLureFightNetState::StaticStruct()->CompareScriptStruct(&CopyFight, &ServerFight, PPF_None);
				bAllMatch &= SameFish(Copy.Value->GetHookedFish(), Server->GetHookedFish()) && Copy.Value->GetFishingState() == Server->GetFishingState();
				bNeverSimulated &= Copy.Value->GetFightState().Steps == 0;
				if (CopyFight.bActive && Copy.Value->GetFishingState() == ELureFishingState::Hooked)
				{
					const float Ride = static_cast<float>(FVector::Dist2D(Copy.Value->GetBobberLocation(), Copy.Value->GetOwner()->GetActorLocation()));
					bBobberRides &= FMath::IsNearlyEqual(Ride, CopyFight.LineOut, 2.f);
					bHud &= Copy.Value->GetStatusText().Contains(TEXT("Tension ["));
				}
			}
			++Updates;
		}
		TestTrue(FString::Printf(TEXT("the server landed the fish (%s)"), *ResultName(Server->GetNetState().LastResult)), Server->GetNetState().LastResult == ELureFishingResult::Landed);
		TestTrue(FString::Printf(TEXT("both copies matched the server's fight state and fish after every update (%d updates)"), Updates), bAllMatch && Updates > 5);
		TestTrue(TEXT("both copies draw the bobber on the fish (LineOut from their player)"), bBobberRides);
		TestTrue(TEXT("both copies show the tension readout while it is on"), bHud);
		TestTrue(TEXT("the copies never simulated the fight"), bNeverSimulated);
		for (const TPair<const TCHAR*, ULureFishingComponent*>& Copy : Copies)
		{
			TestEqual(FString::Printf(TEXT("%s: learns the landing"), Copy.Key), ResultName(Copy.Value->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
			TestTrue(FString::Printf(TEXT("%s: the replicated catch is the hooked instance"), Copy.Key), SameFish(Copy.Value->GetLastLandedFish(), Bonefish));
			TestFalse(FString::Printf(TEXT("%s: the fight is over"), Copy.Key), Copy.Value->GetFightNet().bActive);
		}
		TestEqual(TEXT("OnFishLanded never fires on a copy"), CopiesLanded, 0);
		OwnerCharacter->SetRole(ROLE_Authority);
		ProxyCharacter->SetRole(ROLE_Authority);
		return true;
	}

	// =================================================================================================================
	// Landing
	// =================================================================================================================

	/** OnFishLanded fires exactly once per landed fish: with a re-entrant listener, with two players, after the fight, and with the debug land switch. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQALandingOnce, "Project.Fishing.Fight.QA.Landing.OnFishLandedFiresOnce", Flags)
	bool FLureFightQALandingOnce::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance FishA;
		FFishInstance FishB;
		FFishInstance FishB2;
		if (!RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.2f, 61, FishA) || !RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Uncommon"), 0.25f, 62, FishB)
			|| !RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.1f, 63, FishB2))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingComponent* A = SetUpFishing(World.Spawn(StandAt()), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get(), 12.f, 1);
		ULureFishingComponent* B = SetUpFishing(World.Spawn(StandAt() + FVector(0.f, 300.f, 0.f)), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get(), 12.f, 2);
		if (!A || !B)
		{
			AddError(TEXT("no fishing components"));
			return false;
		}
		TArray<FFishInstance> LandedA;
		TArray<FFishInstance> LandedB;
		// A's listener re-enters the component from inside the hand-off (a cooler that also recasts, say).
		A->OnFishLandedNative.AddLambda([&LandedA](ULureFishingComponent* Fishing, const FFishInstance& Landed)
		{
			LandedA.Add(Landed);
			Fishing->AuthorityReelIn();
			Fishing->AuthorityHook();
			Fishing->AuthoritySetReeling(true);
			Fishing->AuthorityCast(0.5f, 0.f);
		});
		B->OnFishLandedNative.AddLambda([&LandedB](ULureFishingComponent*, const FFishInstance& Landed) { LandedB.Add(Landed); });
		FLogCapture Log(TEXT("LogLureFish"));
		if (!CastAndWait(*this, World, A, 0.f) || !CastAndWait(*this, World, B, 0.f) || !TestTrue(TEXT("A hooks"), A->AuthorityHookFish(FishA)) || !TestTrue(TEXT("B hooks"), B->AuthorityHookFish(FishB)))
		{
			return false;
		}
		A->AuthoritySetReeling(true);
		B->AuthoritySetReeling(true);
		TestTrue(TEXT("both small bonefish are landed by reeling"), World.TickUntil([&]() { return LandedA.Num() > 0 && LandedB.Num() > 0; }, 60 * 60));
		World.Tick(60);
		if (TestEqual(TEXT("A: OnFishLanded fired once"), LandedA.Num(), 1))
		{
			TestTrue(TEXT("A: with A's fish"), SameFish(LandedA[0], FishA));
		}
		if (TestEqual(TEXT("B: OnFishLanded fired once"), LandedB.Num(), 1))
		{
			TestTrue(TEXT("B: with B's fish"), SameFish(LandedB[0], FishB));
		}
		TestTrue(TEXT("A: LastLandedFish is A's fish"), SameFish(A->GetLastLandedFish(), FishA));
		TestTrue(TEXT("B: LastLandedFish is B's fish"), SameFish(B->GetLastLandedFish(), FishB));
		TestEqual(TEXT("one Catch: log line per landed fish"), Log.Count(TEXT("Catch:")), 2);

		ComponentQA::TakeFishOffTheHook(B); // T-030: B's landed fish hangs on B's hook until it is taken off
		// The debug switch (AutoLandDelay > 0) turned on in the middle of a fight lands the fish once, not once per path.
		if (!TestEqual(TEXT("B is idle again"), StateName(B->GetFishingState()), StateName(ELureFishingState::Idle)) || !CastAndWait(*this, World, B, 0.f)
			|| !TestTrue(TEXT("B hooks another"), B->AuthorityHookFish(FishB2)))
		{
			return false;
		}
		World.Tick(10);
		TestTrue(TEXT("B: the fight is on"), B->GetFightNet().bActive);
		FLureFishingRow Debug = QuickProfile();
		Debug.AutoLandDelay = 0.3f;
		B->SetFishingProfile(Debug);
		TestTrue(TEXT("B: the debug switch lands it"), World.TickUntil([B]() { return B->GetFishingState() == ELureFishingState::Idle; }, 120));
		World.Tick(60);
		TestEqual(TEXT("B: exactly one more OnFishLanded"), LandedB.Num(), 2);
		TestFalse(TEXT("B: no fight left running"), B->GetFightNet().bActive);
		TestEqual(TEXT("three Catch: lines in all"), Log.Count(TEXT("Catch:")), 3);
		return true;
	}

	/** Snapped, spooled, thrown, reeled in and line-cancelled fish are lost: never handed to OnFishLanded, nothing left in hand. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQALandingLost, "Project.Fishing.Fight.QA.Landing.LostFishNeverLands", Flags)
	bool FLureFightQALandingLost::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Bonefish;
		if (!RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 71, Bonefish))
		{
			return false;
		}
		const FFishSpeciesRow* Species = Fish.Species->FindRow<FFishSpeciesRow>(Bonefish.SpeciesId, TEXT("FightQA"), false);
		if (!TestNotNull(TEXT("Bonefish species row"), Species))
		{
			return false;
		}
		const FName PatternRow = Species->FightPatternId;
		TStrongObjectPtr<UDataTable> Gear;
		if (!ComponentQA::GearWithExtraRows(*this, Data, TEXT("Line_QAShort,Line,QA Short Line,1,0,0,0,0,50,1500,0,None,0,qa: a strong line on a short spool\n"), Gear))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt()), Fish, Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!Fishing)
		{
			return false;
		}
		int32 Landed = 0;
		Fishing->OnFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, const FFishInstance&) { ++Landed; });
		FLogCapture Log(TEXT("LogLureFish"));

		struct FCase
		{
			const TCHAR* Name;
			FLureFightPatternRow Pattern;
			FLureGearLoadout Kit;
			bool bReel;
			int32 ReelInAfterFrames; // > 0: the line comes in by request after that many frames
			ELureCastBlock ReelInReason;
			ELureFishingResult Result;
			ELureFightOutcome Outcome;
			const TCHAR* Hud;
		};
		const FLureGearLoadout Starter = GetDefault<ULureFishingSettings>()->DefaultLoadout;
		const FCase Cases[] = {
			{ TEXT("snapped"), MakePattern({ MakeMove(TEXT("Heave"), 3.5f, 0.f, 0.f) }, TEXT("Heave")), Starter, true, 0, ELureCastBlock::None,
				ELureFishingResult::Snapped, ELureFightOutcome::Snapped, TEXT("SNAP") },
			{ TEXT("spooled"), MakePattern({ MakeMove(TEXT("Bolt"), 2.5f, 2.f, 1.f) }, TEXT("Bolt")), ComponentQA::Loadout(Starter.Rod, TEXT("Line_QAShort"), Starter.Hook), false, 0,
				ELureCastBlock::None, ELureFishingResult::Snapped, ELureFightOutcome::Spooled, TEXT("took all your line") },
			{ TEXT("threw the hook"), MakePattern({ MakeMove(TEXT("Sit"), 0.f, 0.f, 0.f) }, TEXT("Sit")), Starter, false, 0, ELureCastBlock::None,
				ELureFishingResult::ThrewHook, ELureFightOutcome::ThrewHook, TEXT("threw the hook") },
			{ TEXT("reeled in"), *Data.Pattern(PatternRow), Starter, false, 30, ELureCastBlock::None, ELureFishingResult::Lost, ELureFightOutcome::None, TEXT("") },
			{ TEXT("line cancelled (sprinting)"), *Data.Pattern(PatternRow), Starter, true, 30, ELureCastBlock::Sprinting, ELureFishingResult::Lost, ELureFightOutcome::None, TEXT("") },
		};
		for (const FCase& Case : Cases)
		{
			const TStrongObjectPtr<UDataTable> Patterns = ComponentQA::OnePatternTable(PatternRow, Case.Pattern);
			Fishing->SetFightTables(Gear.Get(), Patterns.Get(), Data.Fight.Get());
			if (!TestTrue(FString::Printf(TEXT("%s: gear equipped"), Case.Name), Fishing->AuthoritySetLoadout(Case.Kit)) || !CastAndWait(*this, World, Fishing)
				|| !TestTrue(FString::Printf(TEXT("%s: hooked"), Case.Name), Fishing->AuthorityHookFish(Bonefish)))
			{
				return false;
			}
			Fishing->AuthoritySetReeling(Case.bReel);
			if (Case.ReelInAfterFrames > 0)
			{
				World.Tick(Case.ReelInAfterFrames);
				TestTrue(FString::Printf(TEXT("%s: still fighting before the reel-in"), Case.Name), Fishing->GetFightNet().bActive);
				Fishing->AuthorityReelIn(Case.ReelInReason);
			}
			TestTrue(FString::Printf(TEXT("%s: the fight ends"), Case.Name), World.TickUntil([Fishing]() { return Fishing->GetFishingState() != ELureFishingState::Hooked; }, 30 * 60));
			const FLureFishingNetState& Net = Fishing->GetNetState();
			TestEqual(FString::Printf(TEXT("%s: result"), Case.Name), ResultName(Net.LastResult), ResultName(Case.Result));
			if (Case.ReelInAfterFrames > 0)
			{
				TestEqual(FString::Printf(TEXT("%s: reason"), Case.Name), static_cast<int32>(Net.ResultReason), static_cast<int32>(Case.ReelInReason));
			}
			else
			{
				TestEqual(FString::Printf(TEXT("%s: fight outcome"), Case.Name), OutcomeName(Fishing->GetFightNet().Outcome), OutcomeName(Case.Outcome));
			}
			if (FCString::Strlen(Case.Hud) > 0)
			{
				TestTrue(FString::Printf(TEXT("%s: the HUD says so (%s)"), Case.Name, *Fishing->GetStatusText().Replace(TEXT("\n"), TEXT(" | "))), Fishing->GetStatusText().Contains(Case.Hud));
			}
			TestEqual(FString::Printf(TEXT("%s: state Idle"), Case.Name), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
			TestFalse(FString::Printf(TEXT("%s: no fish on the line"), Case.Name), Fishing->GetHookedFish().IsValid());
			TestFalse(FString::Printf(TEXT("%s: nothing landed"), Case.Name), Fishing->GetLastLandedFish().IsValid());
			TestFalse(FString::Printf(TEXT("%s: no fight left running"), Case.Name), Fishing->GetFightNet().bActive);
			TestFalse(FString::Printf(TEXT("%s: the reel input is reset"), Case.Name), Fishing->IsServerReeling());
			TestEqual(FString::Printf(TEXT("%s: OnFishLanded never fired"), Case.Name), Landed, 0);
			World.Tick(2);
		}
		TestEqual(TEXT("no Catch: line for a lost fish"), Log.Count(TEXT("Catch:")), 0);
		return true;
	}

	// =================================================================================================================
	// The fish on the line comes from the one roll pipeline
	// =================================================================================================================

	/** A natural bite: the hooked fish IS the roll pipeline's record, the fight reads it, the landed fish is that same record. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAPipelineBite, "Project.Fishing.Fight.QA.Pipeline.HookedFishFromTheRollPipeline", Flags)
	bool FLureFightQAPipelineBite::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		World.AddShoreSpot();
		ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt()), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get(), 12.f, 4321);
		if (!Fishing)
		{
			return false;
		}
		// Strong gear so any bonefish the roll gives is landed while holding reel (the bait stays shrimp: the bonefish's bait).
		TestTrue(TEXT("strong gear, shrimp bait"), Fishing->AuthoritySetLoadout(ComponentQA::Loadout(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Shrimp"))));
		TArray<FFishInstance> Landed;
		Fishing->OnFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, const FFishInstance& Catch) { Landed.Add(Catch); });
		if (!CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240)))
		{
			return false;
		}
		const FFishInstance Pending = Fishing->GetPendingFish();
		const FFishRollContext Context = Fishing->GetLastRollContext();
		TestTrue(TEXT("the bite rolled a fish"), Pending.IsValid());
		FFishInstance Decided;
		TestTrue(TEXT("the bite decision replays from the stored context"), FLureFishingRules::DecideBite(Fish.Get(), Context, Decided));
		TestTrue(TEXT("... to the same record (PickSpecies + Roll)"), SameFish(Decided, Pending));
		FFishRollContext WithSpecies = Context;
		WithSpecies.SpeciesId = Pending.SpeciesId;
		FFishInstance Rolled;
		TestTrue(TEXT("FFishRoll::Roll of the picked species"), FFishRoll::Roll(Fish.Get(), WithSpecies, Rolled));
		TestTrue(TEXT("... gives exactly the bite's record"), SameFish(Rolled, Pending));

		Fishing->AuthoritySetReeling(true);
		Fishing->AuthorityHook();
		if (!TestEqual(TEXT("hooked in the window"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked)))
		{
			return false;
		}
		const FFishInstance Hooked = Fishing->GetHookedFish();
		TestTrue(TEXT("the hooked fish is the rolled record"), SameFish(Hooked, Pending));
		const FLureFightFish Expected = FLureFight::MakeFish(Hooked, Fishing->GetFightTuning(), Fishing->PlayerLevel, GetDefault<UFishSettings>()->LevelScaling);
		TestTrue(TEXT("the fight's fish is made from the record's final stats"), SameFightFish(Fishing->GetFightState().Fish, Expected));
		TestEqual(TEXT("the fight is seeded from the record's seed"), Fishing->GetFightState().Rng.GetInitialSeed(), FLureFight::FightSeed(Hooked.Seed));
		const FFishSpeciesRow* Species = Fish.Species->FindRow<FFishSpeciesRow>(Hooked.SpeciesId, TEXT("FightQA"), false);
		TestTrue(TEXT("the fight uses the species' FightPatternId"), Species && Fishing->GetFightNet().PatternId == Species->FightPatternId);

		TestTrue(TEXT("landed by reeling"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 60 * 60));
		TestEqual(TEXT("result"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
		if (TestEqual(TEXT("one hand-off"), Landed.Num(), 1))
		{
			TestTrue(TEXT("OnFishLanded hands off the unchanged record"), SameFish(Landed[0], Pending));
		}
		TestTrue(TEXT("LastLandedFish is the unchanged record"), SameFish(Fishing->GetLastLandedFish(), Pending));

		// The fight reads the record, not the species row: a later data change to the species' base stats doesn't change this fish.
		if (FFishSpeciesRow* MutableSpecies = Fish.Species->FindRow<FFishSpeciesRow>(Hooked.SpeciesId, TEXT("FightQA"), false))
		{
			for (FFishStatValue& Stat : MutableSpecies->BaseStats)
			{
				Stat.Value *= 10.f;
			}
		}
		ComponentQA::TakeFishOffTheHook(Fishing); // T-030: the landed fish hangs on the hook until it is taken off
		if (CastAndWait(*this, World, Fishing) && TestTrue(TEXT("the same record hooked again"), Fishing->AuthorityHookFish(Hooked)))
		{
			TestTrue(TEXT("species base stats x10 in the table: the fight's fish is unchanged (it reads the record)"), SameFightFish(Fishing->GetFightState().Fish, Expected));
		}
		return true;
	}

	/** Over every species x rarity x modifier x size of the shipped data: the fight fish follows the record's final stats (spec formulas). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAPipelineStats, "Project.Fishing.Fight.QA.Pipeline.FightReadsFinalStats", Flags)
	bool FLureFightQAPipelineStats::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FFishLevelScaling& Scaling = GetDefault<UFishSettings>()->LevelScaling;
		TArray<FName> Modifiers;
		Modifiers.Add(NAME_None);
		for (const TPair<FName, uint8*>& Pair : Fish.Modifiers->GetRowMap())
		{
			Modifiers.Add(Pair.Key);
		}
		int32 Checked = 0;
		int32 Failed = 0;
		for (const TPair<FName, uint8*>& SpeciesPair : Fish.Species->GetRowMap())
		{
			for (const TPair<FName, uint8*>& RarityPair : Fish.Rarities->GetRowMap())
			{
				for (const FName Modifier : Modifiers)
				{
					for (const float Size : { 0.f, 0.5f, 1.f })
					{
						FFishInstance Record;
						TArray<FName> Forced;
						if (!Modifier.IsNone())
						{
							Forced.Add(Modifier);
						}
						FFishRollContext Context;
						Context.SpeciesId = SpeciesPair.Key;
						Context.Seed = 900 + Checked;
						Context.ForcedRarityId = RarityPair.Key;
						Context.bForceModifiers = true;
						Context.ForcedModifierIds = Forced;
						Context.bForceWeightFraction = true;
						Context.ForcedWeightFraction = Size;
						Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
						if (!FFishRoll::Roll(Fish.Get(), Context, Record))
						{
							continue; // a modifier the species can't have: not a fish
						}
						for (const int32 PlayerLevel : { 1, 4, 12 })
						{
							++Checked;
							const FLureFightFish F = FLureFight::MakeFish(Record, T, PlayerLevel, Scaling);
							const double Level = T.ApplyLevelScaling ? Scaling.GetMultiplier(Record.Level, PlayerLevel) : 1.0;
							const double Pull = FMath::Max(0.05, Record.GetStat(T.StrengthStat) * static_cast<double>(T.PullPerStrength) * Level);
							const double Speed = Record.GetStat(T.SpeedStat) * static_cast<double>(T.SpeedPerStat);
							const double Pool = FMath::Max(0.5, Record.GetStat(T.StaminaStat) * static_cast<double>(T.StaminaPerStat));
							const double Aggression = Record.GetStat(T.AggressionStat);
							const double Rest = FMath::Pow(static_cast<double>(Record.DifficultyRating), -static_cast<double>(T.RestDifficultyExponent));
							auto Close = [](double A, double B) { return FMath::Abs(A - B) <= 1.0e-4 * FMath::Max(1.0, FMath::Abs(B)); };
							const bool bOk = Close(F.BasePull, Pull) && Close(F.BaseSpeed, Speed) && Close(F.StaminaPool, Pool) && Close(F.Aggression, Aggression)
								&& Close(F.LevelMultiplier, Level) && (Rest < 0.25 || Rest > 4.0 || Close(F.RestScale, Rest));
							if (!bOk && ++Failed <= 5)
							{
								AddError(FString::Printf(TEXT("%s %s %s size %.1f player %d: pull %.4f/%.4f speed %.3f/%.3f pool %.3f/%.3f aggression %.3f/%.3f level %.4f/%.4f rest %.4f/%.4f"),
									*SpeciesPair.Key.ToString(), *RarityPair.Key.ToString(), *Modifier.ToString(), Size, PlayerLevel, F.BasePull, Pull, F.BaseSpeed, Speed, F.StaminaPool, Pool,
									F.Aggression, Aggression, F.LevelMultiplier, Level, F.RestScale, Rest));
							}
						}
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%d rolled records x player levels checked"), Checked));
		TestTrue(TEXT("many records checked"), Checked > 100);
		TestEqual(TEXT("records whose fight fish doesn't follow their final stats"), Failed, 0);
		return true;
	}

	// =================================================================================================================
	// Component behaviour
	// =================================================================================================================

	/** The server's fight gives the same result, on the same fixed step, whatever the frame rate (30, 60 or 120 fps). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAComponentFrameRate, "Project.Fishing.Fight.QA.Component.FrameRateIndependentOutcome", Flags)
	bool FLureFightQAComponentFrameRate::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Snapper;
		if (!ComponentQA::ReferenceSnapper(*this, Fish, Snapper))
		{
			return false;
		}
		struct FResult { ELureFightOutcome Outcome; int32 Steps; float LineOutAtStart; };
		TArray<FResult> Results;
		for (const float FrameDt : { 1.f / 30.f, 1.f / 60.f, 1.f / 120.f })
		{
			FWorld World;
			if (!World.Create(*this))
			{
				return false;
			}
			ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt()), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
			// The shortest cast keeps the fight short (a few seconds of dives and runs) so three frame rates stay cheap.
			if (!Fishing || !TestTrue(TEXT("gear"), Fishing->AuthoritySetLoadout(ComponentQA::Loadout(TEXT("Rod_Starter"), TEXT("Line_Braid"), TEXT("Hook_Shrimp"))))
				|| !CastAndWait(*this, World, Fishing, 0.f))
			{
				return false;
			}
			Fishing->AuthoritySetReeling(true);
			if (!TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Snapper)))
			{
				return false;
			}
			const float Start = Fishing->GetFightState().LineOut;
			World.TickUntil([Fishing]() { return Fishing->GetFishingState() != ELureFishingState::Hooked; }, FMath::CeilToInt(90.f / FrameDt), FrameDt);
			Results.Add({ Fishing->GetFightState().Outcome, Fishing->GetFightState().Steps, Start });
			AddInfo(FString::Printf(TEXT("%.0f fps: %s after %d steps (%.2f s), %s"), 1.f / FrameDt, *OutcomeName(Fishing->GetFightState().Outcome), Fishing->GetFightState().Steps,
				Fishing->GetFightState().Elapsed, *ResultName(Fishing->GetNetState().LastResult)));
		}
		if (!TestEqual(TEXT("three runs"), Results.Num(), 3))
		{
			return false;
		}
		TestTrue(TEXT("the fixture fight ends (the braid lands the reference snapper)"), Results[0].Outcome == ELureFightOutcome::Landed);
		for (int32 Index = 1; Index < Results.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("run %d: the same start"), Index), Results[Index].LineOutAtStart, Results[0].LineOutAtStart);
			TestEqual(FString::Printf(TEXT("run %d: the same outcome"), Index), OutcomeName(Results[Index].Outcome), OutcomeName(Results[0].Outcome));
			TestEqual(FString::Printf(TEXT("run %d: on the same fixed step"), Index), Results[Index].Steps, Results[0].Steps);
		}
		return true;
	}

	/** "While a fish is on, the bobber distance rule does not apply" (the fight has its spool), and it is back once the fight is over. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQAComponentDistanceRule, "Project.Fishing.Fight.QA.Component.FightSuspendsBobberDistanceRule", Flags)
	bool FLureFightQAComponentDistanceRule::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Bonefish;
		if (!RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 91, Bonefish))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		// A second platform far behind the dock (more than MaxLineLength from any bobber in front of the dock).
		const FVector FarFeet(-4000.f, 0.f, DockTop);
		World.AddBox(FVector(FarFeet.X, 0.f, DockTop * 0.5f), FVector(400.f, 400.f, DockTop * 0.5f));
		ALurePlayerCharacter* Character = World.Spawn(StandAt());
		ULureFishingComponent* Fishing = SetUpFishing(Character, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!Fishing || !CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
		{
			return false;
		}
		const FVector Offset = Character->GetActorLocation() - StandAt();
		const float MaxLine = Fishing->GetProfile().MaxLineLength;
		Character->SetActorLocation(FarFeet + Offset, false, nullptr, ETeleportType::TeleportPhysics);
		const float Away = static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), Fishing->GetNetState().BobberRest));
		if (!TestTrue(FString::Printf(TEXT("fixture: %.0f cm from the bobber, beyond MaxLineLength %.0f"), Away, MaxLine), Away > MaxLine))
		{
			return false;
		}
		World.Tick(30);
		TestEqual(TEXT("fighting: walking beyond MaxLineLength does not bring the line in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
		TestTrue(TEXT("... the fight goes on"), Fishing->GetFightNet().bActive);
		Fishing->AuthorityReelIn();
		Character->SetActorLocation(StandAt() + Offset, false, nullptr, ETeleportType::TeleportPhysics);
		World.Tick(10);
		if (!CastAndWait(*this, World, Fishing))
		{
			return false;
		}
		Character->SetActorLocation(FarFeet + Offset, false, nullptr, ETeleportType::TeleportPhysics);
		World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 30);
		TestEqual(TEXT("not fighting: beyond MaxLineLength the line comes in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
		TestEqual(TEXT("... because it was too far"), static_cast<int32>(Fishing->GetNetState().ResultReason), static_cast<int32>(ELureCastBlock::TooFar));
		return true;
	}

	/** A new rod and line are DT_Gear rows only: equipped by name, their numbers drive the server's fight and reach the clients' HUD state. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataNewGear, "Project.Fishing.Fight.QA.Data.NewGearRowsNeedNoCode", Flags)
	bool FLureFightQADataNewGear::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Snapper;
		if (!ComponentQA::ReferenceSnapper(*this, Fish, Snapper))
		{
			return false;
		}
		TStrongObjectPtr<UDataTable> Gear;
		if (!ComponentQA::GearWithExtraRows(*this, Data,
			TEXT("Rod_QA,Rod,QA Rod,300,20,200,9,1.1,0,0,0,None,0,qa\n")
			TEXT("Line_QA40,Line,QA Line 40,200,0,0,0,0,40,9000,0,None,0,qa\n"), Gear))
		{
			return false;
		}
		FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingComponent* Fishing = SetUpFishing(World.Spawn(StandAt()), Fish, Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!Fishing || !TestTrue(TEXT("the new rows can be equipped by name"), Fishing->AuthoritySetLoadout(ComponentQA::Loadout(TEXT("Rod_QA"), TEXT("Line_QA40"), TEXT("Hook_Shrimp")))))
		{
			return false;
		}
		const FLureGearStats Stats = Fishing->GetGearStats();
		TestFalse(TEXT("resolved from the table (no built-in items)"), Stats.bUsedFallback);
		TestTrue(TEXT("the new rod's numbers"), Stats.RodPower == 20.f && Stats.ReelSpeed == 200.f && Stats.Drag == 9.f && Stats.CastDistanceMultiplier == 1.1f);
		TestTrue(TEXT("the new line's numbers"), Stats.LineStrength == 40.f && Stats.SpoolLength == 9000.f);
		if (!CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hook the reference snapper"), Fishing->AuthorityHookFish(Snapper)))
		{
			return false;
		}
		TestEqual(TEXT("the fight runs on the new rod"), Fishing->GetFightState().Gear.RodPower, 20.f);
		TestEqual(TEXT("clients see the new line's strength"), Fishing->GetFightNet().LineStrength, 40.f);
		TestEqual(TEXT("... and its spool"), Fishing->GetFightNet().SpoolLength, 9000.f);
		Fishing->AuthoritySetReeling(true);
		TestTrue(TEXT("the new gear lands the snapper while holding reel"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 60 * 60));
		TestEqual(TEXT("landed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

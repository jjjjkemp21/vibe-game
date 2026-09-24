// Lure T-006 QA (qa-engineer): server authority and replication of the fishing state.
// Project.Fishing.QA.Net.* (single world: reflection, the FRepLayout wire format, client-role copies) and
// Project.Fishing.QA.Net2P.* (a real in-process listen server with two connected clients, UE::Net::FTestWorlds).
// Spec: docs/specs/fishing-rules.md "Flow and authority" (the client only asks: ServerCast(charge, aim yaw); the server decides
// everything else; clients draw from the replicated NetState + HookedFish + LastLandedFish; remote players get HookLatencyGrace).

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/PlayerController.h"
#include "Net/RepLayout.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_EDITOR
#include "Tests/NetTestHelpers.h"
#endif

// Everything lives in namespace QAFishing (unity builds merge test files; other files use global using-directives).
namespace QAFishing
{

namespace NetLocal
{

	/** Server RPC parameters as the generated thunk lays them out (two floats). */
	struct FServerCastParams
	{
		float Charge01;
		float AimYawDegrees;
	};

	/** Sends Server RPC Name with Params through ProcessEvent, exactly like the generated ServerXxx() wrapper does. */
	bool CallServerRpc(ULureFishingComponent* Component, const TCHAR* Name, void* Params)
	{
		UFunction* Function = Component ? Component->FindFunction(FName(Name)) : nullptr;
		if (!Function)
		{
			return false;
		}
		Component->ProcessEvent(Function, Params);
		return true;
	}

	/** Copies Property from Source to Target through the engine's replication layout (the property replication wire format). */
	bool NetCopy(FAutomationTestBase& Test, const FStructProperty* Property, UObject* Source, UObject* Target, int64* OutBits = nullptr)
	{
		const TSharedPtr<FRepLayout> Layout = FRepLayout::CreateFromStruct(Property->Struct, nullptr, ECreateRepLayoutFlags::None);
		if (!Layout.IsValid())
		{
			Test.AddError(TEXT("QA: no FRepLayout for ") + Property->Struct->GetName());
			return false;
		}
		FNetBitWriter Writer(nullptr, 64 * 1024 * 8);
		bool bUnmapped = false;
		Layout->SerializePropertiesForStruct(Property->Struct, Writer, nullptr, Property->ContainerPtrToValuePtr<void>(Source), bUnmapped);
		if (OutBits)
		{
			*OutBits = Writer.GetNumBits();
		}
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		Layout->SerializePropertiesForStruct(Property->Struct, Reader, nullptr, Property->ContainerPtrToValuePtr<void>(Target), bUnmapped);
		return !Writer.IsError() && !Reader.IsError();
	}

	/** "Replicates" the server component's replicated properties to a client-role copy, then calls the RepNotify like the net driver. */
	void Replicate(FAutomationTestBase& Test, ULureFishingComponent* Server, ULureFishingComponent* Client)
	{
		const FLureFishingNetState Previous = Client->GetNetState();
		for (TFieldIterator<FProperty> It(ULureFishingComponent::StaticClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Net) && It->GetOwnerClass() == ULureFishingComponent::StaticClass())
			{
				if (const FStructProperty* Struct = CastField<FStructProperty>(*It))
				{
					NetCopy(Test, Struct, Server, Client);
				}
				else
				{
					Test.AddError(TEXT("QA: unexpected non-struct replicated property ") + It->GetName());
				}
			}
		}
		if (UFunction* OnRep = Client->FindFunction(TEXT("OnRep_NetState")))
		{
			struct
			{
				FLureFishingNetState PreviousState;
			} Params{ Previous };
			Client->ProcessEvent(OnRep, &Params);
		}
	}

	void TestSameNetState(FAutomationTestBase& Test, const FString& Label, const FLureFishingNetState& Client, const FLureFishingNetState& Server)
	{
		Test.TestEqual(Label + TEXT(": State"), StateName(Client.State), StateName(Server.State));
		Test.TestEqual(Label + TEXT(": CastId"), Client.CastId, Server.CastId);
		Test.TestTrue(Label + TEXT(": CastOrigin (0.1 cm quantized)"), FVector(Client.CastOrigin).Equals(FVector(Server.CastOrigin), 0.051));
		Test.TestTrue(Label + TEXT(": BobberRest (0.1 cm quantized)"), FVector(Client.BobberRest).Equals(FVector(Server.BobberRest), 0.051));
		Test.TestEqual(Label + TEXT(": FlightTime"), Client.FlightTime, Server.FlightTime);
		Test.TestEqual(Label + TEXT(": StateStartTime"), Client.StateStartTime, Server.StateStartTime);
		Test.TestEqual(Label + TEXT(": bOnWater"), Client.bOnWater, Server.bOnWater);
		Test.TestEqual(Label + TEXT(": bNoFishHere"), Client.bNoFishHere, Server.bNoFishHere);
		Test.TestEqual(Label + TEXT(": SpotId"), Client.SpotId, Server.SpotId);
		Test.TestEqual(Label + TEXT(": NibbleId"), Client.NibbleId, Server.NibbleId);
		Test.TestEqual(Label + TEXT(": LastNibbleTime"), Client.LastNibbleTime, Server.LastNibbleTime);
		Test.TestEqual(Label + TEXT(": LastResult"), ResultName(Client.LastResult), ResultName(Server.LastResult));
		Test.TestEqual(Label + TEXT(": ResultId"), Client.ResultId, Server.ResultId);
		Test.TestEqual(Label + TEXT(": ResultTime"), Client.ResultTime, Server.ResultTime);
		Test.TestEqual(Label + TEXT(": ResultReason"), BlockName(Client.ResultReason), BlockName(Server.ResultReason));
	}

	/** Server player + a copy that plays another machine's role (no authority), in one standalone world. */
	struct FNetRig
	{
		FScene Scene;
		ALurePlayerCharacter* ServerCharacter = nullptr;
		ALurePlayerCharacter* ClientCharacter = nullptr;
		ULureFishingComponent* Server = nullptr;
		ULureFishingComponent* Client = nullptr;
		FLureFishingRow Profile;

		bool Make(FAutomationTestBase& Test, ENetRole ClientRole)
		{
			FLureFishingRow Shipped;
			if (!ShippedFishingRow(Test, Shipped) || !Scene.Create(Test))
			{
				return false;
			}
			Scene.AddShoreSpot();
			Profile = StageProfile(Shipped);
			ServerCharacter = Scene.Spawn(Test);
			ClientCharacter = Scene.Spawn(Test, StandFeet + FVector(0.f, 150.f, 0.f));
			Server = Scene.SetUpFishing(Test, ServerCharacter, Profile);
			Client = Scene.SetUpFishing(Test, ClientCharacter, Profile);
			if (!Server || !Client)
			{
				return false;
			}
			Scene.Tick(10);
			ClientCharacter->SetRole(ClientRole);
			return true;
		}

		~FNetRig()
		{
			if (IsValid(ClientCharacter))
			{
				ClientCharacter->SetRole(ROLE_Authority);
			}
		}
	};
}

namespace NetLocal
{

// =====================================================================================================================
// What crosses the wire
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNetOnlyChargeAndYawCrossTheWire, "Project.Fishing.QA.Net.OnlyChargeAndYawCrossTheWire", QAFishing::Flags)
bool FQAFishNetOnlyChargeAndYawCrossTheWire::RunTest(const FString& Parameters)
{
	// Spec: the client only asks - ServerCast(charge, aim yaw), a hook press, a reel-in. No client RPC can carry a fish, a species,
	// a spot, a landing point or a time.
	TArray<FString> Rpcs;
	for (UClass* Class : { ULureFishingComponent::StaticClass(), ALurePlayerCharacter::StaticClass() })
	{
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const UFunction* Function = *It;
			if (!Function->HasAnyFunctionFlags(FUNC_NetServer))
			{
				continue;
			}
			TArray<FString> Params;
			for (TFieldIterator<FProperty> Param(Function); Param && Param->HasAnyPropertyFlags(CPF_Parm); ++Param)
			{
				Params.Add(Param->GetCPPType() + TEXT(" ") + Param->GetName());
				// T-028: ServerSetFightInput sends its rod aim and reel step as bytes (plain numbers too; an enum byte would not be).
				const FByteProperty* Byte = CastField<FByteProperty>(*Param);
				TestTrue(FString::Printf(TEXT("%s::%s(%s): parameters are plain numbers (no fish, name, vector, struct or object)"), *Class->GetName(), *Function->GetName(), *Param->GetName()),
					Param->IsA<FFloatProperty>() || Param->IsA<FBoolProperty>() || (Byte && !Byte->Enum));
			}
			Rpcs.Add(FString::Printf(TEXT("%s::%s(%s)"), *Class->GetName(), *Function->GetName(), *FString::Join(Params, TEXT(", "))));
		}
	}
	AddInfo(TEXT("Server RPCs: ") + FString::Join(Rpcs, TEXT("; ")));
	const UFunction* CastRpc = ULureFishingComponent::StaticClass()->FindFunctionByName(TEXT("ServerCast"));
	const UFunction* HookRpc = ULureFishingComponent::StaticClass()->FindFunctionByName(TEXT("ServerHook"));
	const UFunction* ReelInRpc = ULureFishingComponent::StaticClass()->FindFunctionByName(TEXT("ServerReelIn"));
	if (TestNotNull(TEXT("ServerCast"), CastRpc))
	{
		TestEqual(TEXT("ServerCast takes exactly two floats (charge, aim yaw)"), static_cast<int32>(CastRpc->NumParms), 2);
		TestEqual(TEXT("... and their size is two floats (the QA call layout)"), static_cast<int32>(CastRpc->ParmsSize), static_cast<int32>(sizeof(FServerCastParams)));
	}
	TestTrue(TEXT("ServerHook takes nothing"), HookRpc && HookRpc->NumParms == 0);
	TestTrue(TEXT("ServerReelIn takes nothing"), ReelInRpc && ReelInRpc->NumParms == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNetReplicatedToEveryone, "Project.Fishing.QA.Net.ReplicatedToEveryone", QAFishing::Flags)
bool FQAFishNetReplicatedToEveryone::RunTest(const FString& Parameters)
{
	// Everyone sees every bobber and line (NetState must reach other players); the owner gets its fish. The server's pending fish is
	// never replicated (not even a property), so a client can't learn the species before it hooks. T-007 adds FightNet (the fight
	// for the HUD and visuals) and Loadout (the equipped gear): both are gameplay-visible, so every player gets them.
	UClass* Class = ULureFishingComponent::StaticClass();
	Class->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Lifetime;
	GetDefault<ULureFishingComponent>()->GetLifetimeReplicatedProps(Lifetime);
	TArray<FString> Own;
	for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_Net))
		{
			Own.Add(It->GetName());
		}
	}
	Own.Sort();
	TestEqual(TEXT("the replicated fishing properties"), FString::Join(Own, TEXT(", ")), FString(TEXT("FightNet, HookedFish, LastLandedFish, Loadout, NetState")));
	for (const TCHAR* Name : { TEXT("NetState"), TEXT("HookedFish"), TEXT("LastLandedFish"), TEXT("FightNet"), TEXT("Loadout") })
	{
		const FProperty* Property = Class->FindPropertyByName(Name);
		const FLifetimeProperty* Rep = Property ? Lifetime.FindByPredicate([Property](const FLifetimeProperty& P) { return P.RepIndex == Property->RepIndex; }) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s is registered for replication"), Name), Rep))
		{
			continue;
		}
		const ELifetimeCondition Condition = Rep->Condition;
		TestTrue(FString::Printf(TEXT("%s reaches the owning player (condition %d)"), Name, static_cast<int32>(Condition)),
			Condition != COND_SkipOwner && Condition != COND_SimulatedOnly && Condition != COND_Never && Condition != COND_SimulatedOnlyNoReplay);
		if (FCString::Strcmp(Name, TEXT("NetState")) == 0)
		{
			TestEqual(TEXT("NetState reaches every player (COND_None: others see the bobber)"), static_cast<int32>(Condition), static_cast<int32>(COND_None));
		}
	}
	for (TFieldIterator<FProperty> It(FLureFishingNetState::StaticStruct()); It; ++It)
	{
		const FStructProperty* Struct = CastField<FStructProperty>(*It);
		TestFalse(FString::Printf(TEXT("NetState.%s is no fish or roll data"), *It->GetName()),
			Struct && (Struct->Struct == FFishInstance::StaticStruct() || Struct->Struct == FFishRollContext::StaticStruct()));
	}
	TestNull(TEXT("no PendingFish property exists to replicate"), Class->FindPropertyByName(TEXT("PendingFish")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNetNetStateWireRoundTrip, "Project.Fishing.QA.Net.NetStateWireRoundTrip", QAFishing::Flags)
bool FQAFishNetNetStateWireRoundTrip::RunTest(const FString& Parameters)
{
	static_assert(!TStructOpsTypeTraits<FLureFishingNetState>::WithNetSerializer, "FLureFishingNetState got a custom NetSerialize: add a round trip for it");
	const FStructProperty* Property = CastField<FStructProperty>(ULureFishingComponent::StaticClass()->FindPropertyByName(TEXT("NetState")));
	if (!TestNotNull(TEXT("NetState property"), Property))
	{
		return false;
	}
	ULureFishingComponent* Source = NewObject<ULureFishingComponent>();
	ULureFishingComponent* Target = NewObject<ULureFishingComponent>();
	FLureFishingNetState* SourceState = Property->ContainerPtrToValuePtr<FLureFishingNetState>(Source);
	struct FCase
	{
		ELureFishingState State;
		ELureFishingResult Result;
		ELureCastBlock Reason;
		double Time;
	};
	const FCase Cases[] = { { ELureFishingState::Idle, ELureFishingResult::None, ELureCastBlock::None, 0.0 },
		{ ELureFishingState::Casting, ELureFishingResult::Refused, ELureCastBlock::Sprinting, 12.5 },
		{ ELureFishingState::Waiting, ELureFishingResult::Spooked, ELureCastBlock::None, 3600.0 + 1.0 / 3.0 },
		{ ELureFishingState::Biting, ELureFishingResult::Missed, ELureCastBlock::TooFar, 86400.0 * 3.0 + 0.123456789 },
		{ ELureFishingState::Hooked, ELureFishingResult::Lost, ELureCastBlock::RodTucked, 1.0e7 + 0.5 } };
	int64 MaxBits = 0;
	for (const FCase& Case : Cases)
	{
		FLureFishingNetState& State = *SourceState;
		State.State = Case.State;
		State.CastId = 255;
		State.CastOrigin = FVector(-7012.34, 600.05, 239.99);
		State.BobberRest = FVector(15000.44, -16800.06, -60.0);
		State.FlightTime = 1.2345f;
		State.StateStartTime = Case.Time;
		State.bOnWater = Case.State != ELureFishingState::Idle;
		State.bNoFishHere = Case.State == ELureFishingState::Waiting;
		State.SpotId = TEXT("hidden_cove");
		State.NibbleId = 254;
		State.LastNibbleTime = Case.Time - 0.75;
		State.LastResult = Case.Result;
		State.ResultId = 128;
		State.ResultTime = Case.Time + 0.25;
		State.ResultReason = Case.Reason;
		int64 Bits = 0;
		TestTrue(TEXT("wire round trip without errors"), NetCopy(*this, Property, Source, Target, &Bits));
		MaxBits = FMath::Max(MaxBits, Bits);
		TestSameNetState(*this, TEXT("round trip in ") + StateName(Case.State), Target->GetNetState(), State);
	}
	AddInfo(FString::Printf(TEXT("NetState is at most %lld bits (%lld bytes) on the wire"), MaxBits, (MaxBits + 7) / 8));
	TestTrue(FString::Printf(TEXT("NetState stays small (%lld bytes <= 128)"), (MaxBits + 7) / 8), (MaxBits + 7) / 8 <= 128);
	return true;
}

// =====================================================================================================================
// Server authority in one world (client-role copies)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNetClientCopyCannotDecide, "Project.Fishing.QA.Net.ClientCopyCannotDecide", QAFishing::Flags)
bool FQAFishNetClientCopyCannotDecide::RunTest(const FString& Parameters)
{
	// A copy without authority (the owner's or another player's machine) never casts, bites, hooks, misses, nibbles or lands on its own.
	for (const ENetRole Role : { ROLE_AutonomousProxy, ROLE_SimulatedProxy })
	{
		const FString Label = Role == ROLE_AutonomousProxy ? TEXT("owner's copy") : TEXT("other player's copy");
		FNetRig Rig;
		if (!Rig.Make(*this, Role))
		{
			return false;
		}
		TestFalse(Label + TEXT(": AuthorityCast refuses"), Rig.Client->AuthorityCast(1.f, 0.f));
		Rig.Client->AuthorityHook();
		Rig.Client->AuthorityReelIn();
		TestEqual(Label + TEXT(": still idle, no result"), StateName(Rig.Client->GetFishingState()), StateName(ELureFishingState::Idle));
		TestEqual(Label + TEXT(": no cast id"), static_cast<int32>(Rig.Client->GetNetState().CastId), 0);

		// The server casts; the copy gets Waiting and then time passes without updates: it never bites or nibbles by itself.
		if (!DriveToStage(*this, Rig.Scene, Rig.ServerCharacter, Rig.Server, EStage::Waiting))
		{
			continue;
		}
		Replicate(*this, Rig.Server, Rig.Client);
		const FLureFishingNetState Copied = Rig.Client->GetNetState();
		Rig.Client->AuthorityHook();
		Rig.Scene.Tick(360); // 6 s: the server bites (3 s) and misses (3 s window) meanwhile
		TestTrue(TEXT("QA precondition: the server moved on (nibble, bite or miss)"),
			Rig.Server->GetNetState().NibbleId != Copied.NibbleId || Rig.Server->GetFishingState() != Copied.State);
		TestEqual(Label + TEXT(": without updates the copy stays Waiting"), StateName(Rig.Client->GetFishingState()), StateName(ELureFishingState::Waiting));
		TestEqual(Label + TEXT(": ... with no nibble of its own"), Rig.Client->GetNetState().NibbleId, Copied.NibbleId);
		TestEqual(Label + TEXT(": ... and no result of its own"), Rig.Client->GetNetState().ResultId, Copied.ResultId);
		TestEqual(Label + TEXT(": ... and no state of its own"), StateName(Rig.Client->GetNetState().State), StateName(Copied.State));
		TestTrue(Label + TEXT(": the copy never rolls a fish"), !Rig.Client->GetPendingFish().IsValid() && Rig.Client->GetScheduledBiteTime() < 0.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNetClientFollowsServerAtEveryStage, "Project.Fishing.QA.Net.ClientFollowsServerAtEveryStage", QAFishing::Flags)
bool FQAFishNetClientFollowsServerAtEveryStage::RunTest(const FString& Parameters)
{
	// After each update through the wire format, the copy's state equals the server's and it draws the bobber where the server does.
	for (const EStage Stage : { EStage::Casting, EStage::Waiting, EStage::Nibble, EStage::Biting, EStage::Hooked })
	{
		FNetRig Rig;
		if (!Rig.Make(*this, ROLE_SimulatedProxy) || !DriveToStage(*this, Rig.Scene, Rig.ServerCharacter, Rig.Server, Stage))
		{
			continue;
		}
		Replicate(*this, Rig.Server, Rig.Client);
		const FString Label = TEXT("other player's copy, ") + StageName(Stage);
		TestSameNetState(*this, Label, Rig.Client->GetNetState(), Rig.Server->GetNetState());
		if (Stage == EStage::Hooked)
		{
			// T-007: at Hooked the bobber rides on the fish, placed from the player's position along FightNet (LineOut, SideDeg,
			// tension, depth). The rig's copy is a second pawn 150 cm to the side; put it where the server's pawn is, as the
			// replicated pawn would be on another machine.
			TestTrue(Label + TEXT(": the fight reached the copy (FightNet.bActive)"), Rig.Client->GetFightNet().bActive && Rig.Server->GetFightNet().bActive);
			TestNearlyEqual(Label + TEXT(": FightNet.LineOut"), Rig.Client->GetFightNet().LineOut, Rig.Server->GetFightNet().LineOut, 0.5f);
			TestNearlyEqual(Label + TEXT(": FightNet.SideDeg"), Rig.Client->GetFightNet().SideDeg, Rig.Server->GetFightNet().SideDeg, 0.1f);
			Rig.ClientCharacter->SetActorLocationAndRotation(Rig.ServerCharacter->GetActorLocation(), Rig.ServerCharacter->GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		}
		TestTrue(Label + TEXT(": the same bobber position"), Rig.Client->GetBobberLocation().Equals(Rig.Server->GetBobberLocation(), 0.2));
		TestEqual(Label + TEXT(": the same hooked fish"), Rig.Client->GetHookedFish().Seed, Rig.Server->GetHookedFish().Seed);
		if (Stage == EStage::Hooked)
		{
			Rig.Server->AuthorityReelIn();
			Replicate(*this, Rig.Server, Rig.Client);
			TestEqual(Label + TEXT(" -> reeled in: the copy follows (Lost)"), ResultName(Rig.Client->GetNetState().LastResult), ResultName(ELureFishingResult::Lost));
			TestFalse(Label + TEXT(" -> reeled in: no fish on the copy"), Rig.Client->GetHookedFish().IsValid());
		}
		if (Stage == EStage::Nibble)
		{
			TestEqual(Label + TEXT(": the nibble reached the copy"), Rig.Client->GetNetState().NibbleId, Rig.Server->GetNetState().NibbleId);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNetBiteStaysSecretUntilHooked, "Project.Fishing.QA.Net.BiteStaysSecretUntilHooked", QAFishing::Flags)
bool FQAFishNetBiteStaysSecretUntilHooked::RunTest(const FString& Parameters)
{
	// Spec: the fish that bites stays on the server until hooked; then it replicates as HookedFish (and LastLandedFish once landed).
	FNetRig Rig;
	if (!Rig.Make(*this, ROLE_AutonomousProxy))
	{
		return false;
	}
	FLureFishingRow Profile = Rig.Profile;
	Profile.AutoLandDelay = 0.5f;
	Rig.Server->SetFishingProfile(Profile);
	if (!DriveToStage(*this, Rig.Scene, Rig.ServerCharacter, Rig.Server, EStage::Biting))
	{
		return false;
	}
	Replicate(*this, Rig.Server, Rig.Client);
	TestTrue(TEXT("the server holds the rolled fish"), Rig.Server->GetPendingFish().IsValid());
	TestEqual(TEXT("the owner sees the bite"), StateName(Rig.Client->GetFishingState()), StateName(ELureFishingState::Biting));
	TestFalse(TEXT("... but no fish (nothing replicated says which)"), Rig.Client->GetHookedFish().IsValid() || Rig.Client->GetLastLandedFish().IsValid());
	const FFishInstance Pending = Rig.Server->GetPendingFish();
	Rig.Server->AuthorityHook();
	Replicate(*this, Rig.Server, Rig.Client);
	TestEqual(TEXT("hooked: the owner gets the fish"), Rig.Client->GetHookedFish().Seed, Pending.Seed);
	TestEqual(TEXT("hooked: the species"), Rig.Client->GetHookedFish().SpeciesId, Pending.SpeciesId);
	TestTrue(TEXT("hooked: the owner's HUD names it"), Rig.Client->GetStatusText().Contains(TEXT("Hooked:")));
	Rig.Scene.TickUntil([&Rig]() { return Rig.Server->GetFishingState() == ELureFishingState::Idle; }, 120);
	Replicate(*this, Rig.Server, Rig.Client);
	TestEqual(TEXT("landed: the owner gets LastLandedFish"), Rig.Client->GetLastLandedFish().Seed, Pending.Seed);
	TestFalse(TEXT("landed: nothing on the line"), Rig.Client->GetHookedFish().IsValid());
	TestTrue(TEXT("landed: the owner's HUD says caught"), Rig.Client->GetStatusText().Contains(TEXT("Caught:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNetServerRpcsClampHostileRequests, "Project.Fishing.QA.Net.ServerRpcsClampHostileRequests", QAFishing::Flags)
bool FQAFishNetServerRpcsClampHostileRequests::RunTest(const FString& Parameters)
{
	// The RPC entry points (called through ProcessEvent like the network does) clamp what a modified client could send.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	FLureFishingRow Profile = FlowProfile(Shipped, 30.f);
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile);
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	struct FCase
	{
		float Charge;
		float Yaw;
		float Expected;
	};
	const FCase Cases[] = { { 1.0e6f, 0.f, Profile.MaxCastDistance }, { -1.0e6f, 0.f, Profile.MinCastDistance }, { NaN, 0.f, Profile.MinCastDistance },
		{ 1.f, NaN, Profile.MaxCastDistance }, { 1.f, 1.0e30f, -1.f } };
	for (const FCase& Case : Cases)
	{
		const FVector Eye = Character->GetPawnViewLocation();
		FServerCastParams Params{ Case.Charge, Case.Yaw };
		TestTrue(TEXT("ServerCast exists"), CallServerRpc(Fishing, TEXT("ServerCast"), &Params));
		const FString Label = FString::Printf(TEXT("ServerCast(%g, %g)"), Case.Charge, Case.Yaw);
		if (!TestEqual(Label + TEXT(": a cast"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Casting)))
		{
			continue;
		}
		const FVector Rest = Fishing->GetNetState().BobberRest;
		const float Distance = static_cast<float>(FVector::Dist2D(Eye, Rest));
		TestFalse(Label + TEXT(": a finite landing point"), Rest.ContainsNaN());
		if (Case.Expected > 0.f)
		{
			TestNearlyEqual(FString::Printf(TEXT("%s: lands %.0f cm out (got %.1f)"), *Label, Case.Expected, Distance), Distance, Case.Expected, 1.f);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s: within the cast range (%.1f)"), *Label, Distance), Distance <= Profile.MaxCastDistance + 1.f);
		}
		CallServerRpc(Fishing, TEXT("ServerReelIn"), nullptr);
		TestEqual(Label + TEXT(": ServerReelIn brings it in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
	}
	// A hook request can't create a bite: before the bite it is an early press (EarlyHook ReelIn).
	FServerCastParams Params{ 0.5f, 0.f };
	CallServerRpc(Fishing, TEXT("ServerCast"), &Params);
	Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 180);
	CallServerRpc(Fishing, TEXT("ServerHook"), nullptr);
	TestEqual(TEXT("ServerHook before a bite: no bite, the line comes in"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
	TestFalse(TEXT("... and no fish"), Fishing->GetHookedFish().IsValid());
	return true;
}

// =====================================================================================================================
// A real listen server with two clients (in-process net drivers)
// =====================================================================================================================

#if WITH_EDITOR

namespace QAFishingNet2P
{
	AActor* AddDock(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(FVector(DockHalf, DockHalf, DockTop * 0.5f), false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetRelativeLocation_Direct(FVector(0.f, 0.f, DockTop * 0.5f));
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		return Actor;
	}

	void AddSpot(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None);
		Root->SetRelativeLocation_Direct(SpotCenter);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->Tags = { GetDefault<ULureFishingSettings>()->FishingSpotTag, TEXT("Spot=qa_net"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"),
			FName(*FString::Printf(TEXT("Radius=%.0f"), SpotRadius)) };
	}

	ALurePlayerCharacter* SpawnFor(UWorld* World, APlayerController* Controller, const FVector& Feet, const UDataTable* Movement)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Movement, Rows, Problems);
		const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, RowOf(Rows, ELureMovementState::Stand).CapsuleHalfHeight + 2.15f));
		ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Character)
		{
			Character->GetLureMovement()->ApplyMovementTable(Movement);
			Character->FinishSpawning(Transform);
			if (Controller)
			{
				Controller->Possess(Character);
			}
		}
		return Character;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishNet2PServerDecidesClientsFollow, "Project.Fishing.QA.Net2P.ServerDecidesClientsFollow", QAFishing::Flags)
bool FQAFishNet2PServerDecidesClientsFollow::RunTest(const FString& Parameters)
{
	// Two players on a server (real net drivers, in-process; the harness's server is dedicated, so both players are remote). Player 0 casts with the button on its own machine: the server
	// decides, the owner and the other player see the same line; the bite, the hook press and the landing go the same way. Player 1
	// (a "modified" client) can't cast out of range, force a bite or decide anything on its machine.
	using namespace QAFishingNet2P;
	FLureFishingRow Shipped;
	FString MovementCsv;
	FishQA::FTables Fish;
	if (!ShippedFishingRow(*this, Shipped) || !LoadMovementCsv(*this, MovementCsv) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Movement(MakeTableChecked(*this, FLureMovementRow::StaticStruct(), MovementCsv, TEXT("DT_Movement")));
	AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	// Harness artifact: the test's docks are spawned per world (not replicated), so the players' movement base can't be sent.
	AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

	// The engine's harness: an in-process server (it runs as a dedicated server, net mode 1) and two connected clients.
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* ServerWorld = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("QA harness: the server world is up with a net driver"), Worlds.Server.IsLoaded() && ServerWorld && ServerWorld->GetNetDriver()))
	{
		return false;
	}
	for (int32 Client = 0; Client < 2; ++Client)
	{
		if (!TestTrue(FString::Printf(TEXT("QA harness: client %d connects"), Client), Worlds.CreateAndConnectClient()))
		{
			return false;
		}
	}
	AddInfo(FString::Printf(TEXT("server net mode %d"), static_cast<int32>(ServerWorld->GetNetMode())));
	AddDock(ServerWorld);
	AddSpot(ServerWorld);
	for (UE::Net::FTestWorldInstance& Client : Worlds.Clients)
	{
		AddDock(Client.GetWorld()); // each machine has the level geometry
	}

	ALurePlayerCharacter* Players[2] = { nullptr, nullptr };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Players[Index] = SpawnFor(ServerWorld, Worlds.GetServerPlayerControllerOfClient(Index), StandFeet + FVector(0.f, Index == 0 ? -100.f : 100.f, 0.f), Movement.Get());
		if (!TestNotNull(FString::Printf(TEXT("QA harness: player %d spawned"), Index), Players[Index]))
		{
			return false;
		}
	}
	auto CopyOn = [&Worlds](int32 Client, ALurePlayerCharacter* ServerCharacter) -> ALurePlayerCharacter*
	{
		// FindReplicatedObjectOnClient ensures when the object isn't there yet, so ask first.
		if (!Worlds.DoesReplicatedObjectExistOnClient(ServerCharacter, static_cast<uint32>(Client)))
		{
			return nullptr;
		}
		return Cast<ALurePlayerCharacter>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(ServerCharacter), static_cast<uint32>(Client)));
	};
	const bool bReplicated = Worlds.TickAllUntil([&]()
	{
		for (int32 Client = 0; Client < 2; ++Client)
		{
			const ALurePlayerCharacter* Own = CopyOn(Client, Players[Client]);
			const ALurePlayerCharacter* Other = CopyOn(Client, Players[1 - Client]);
			if (!Own || !Other || !Own->IsLocallyControlled() || !Own->GetFishing() || !Other->GetFishing())
			{
				return false;
			}
		}
		return true;
	}, Dt, 600);
	if (!TestTrue(TEXT("QA harness: each client has its own (controlled) player and the other player"), bReplicated))
	{
		return false;
	}

	FLureFishingRow Profile = FlowProfile(Shipped, 1.5f, 1.f);
	Profile.AutoLandDelay = 1.f;
	FLureFishingRow Quiet = FlowProfile(Shipped, 60.f); // player 1: no bite during the test
	ULureFishingComponent* Server0 = Players[0]->GetFishing();
	ULureFishingComponent* Server1 = Players[1]->GetFishing();
	Server0->SetFishingProfile(Profile);
	Server0->SetFishTables(Fish.Get());
	Server0->SetRandomSeed(606);
	Server0->TimeOfDayOverride = 12.f;
	Server1->SetFishingProfile(Quiet);
	Server1->SetFishTables(Fish.Get());
	Server1->SetRandomSeed(707);
	ULureFishingComponent* Own0 = CopyOn(0, Players[0])->GetFishing();   // player 0 on its own machine
	ULureFishingComponent* Other0 = CopyOn(1, Players[0])->GetFishing(); // player 0 as player 1 sees it
	ULureFishingComponent* Own1 = CopyOn(1, Players[1])->GetFishing();   // player 1 on its own machine
	Own0->SetFishingProfile(Profile);
	Other0->SetFishingProfile(Profile);
	Own1->SetFishingProfile(Quiet);
	Worlds.TickAll(60); // settle on the ground, moves flowing

	TestFalse(TEXT("client machines have no authority over the players"), Own0->GetOwner()->HasAuthority() || Other0->GetOwner()->HasAuthority());
	TestNearlyEqual(TEXT("the server gives the remote players HookLatencyGrace"), Server0->GetHookGrace(), Profile.HookLatencyGrace, 1.0e-6f);

	// 1) Player 0 holds and releases the fishing button on its machine.
	const FVector ServerEye = Players[0]->GetPawnViewLocation();
	Own0->PressCast();
	Worlds.TickAll(36);
	TestTrue(TEXT("player 0: charging on its machine"), Own0->IsCharging());
	TestEqual(TEXT("player 0: the server has no line yet (charging is local)"), StateName(Server0->GetFishingState()), StateName(ELureFishingState::Idle));
	const float Charge = Own0->GetCharge();
	Own0->ReleaseCast();
	if (!TestTrue(TEXT("the server casts for player 0"), Worlds.TickAllUntil([Server0]() { return Server0->GetFishingState() != ELureFishingState::Idle; }, Dt, 60)))
	{
		return false;
	}
	const float Distance = static_cast<float>(FVector::Dist2D(ServerEye, Server0->GetNetState().BobberRest));
	TestNearlyEqual(FString::Printf(TEXT("the server used player 0's charge %.2f (lands %.0f cm out, got %.0f)"), Charge, FLureFishingRules::CastDistance(Profile, Charge), Distance),
		Distance, FLureFishingRules::CastDistance(Profile, Charge), 30.f);
	const bool bBothSee = Worlds.TickAllUntil([&]()
	{
		return Server0->GetFishingState() == ELureFishingState::Waiting && Own0->GetFishingState() == ELureFishingState::Waiting
			&& Other0->GetFishingState() == ELureFishingState::Waiting;
	}, Dt, 180);
	TestTrue(TEXT("the bobber lands: the server, the owner and the other player all show Waiting"), bBothSee);
	TestTrue(TEXT("the owner's bobber rests where the server's does"), FVector(Own0->GetNetState().BobberRest).Equals(FVector(Server0->GetNetState().BobberRest), 0.1));
	TestTrue(TEXT("the other player's too"), FVector(Other0->GetNetState().BobberRest).Equals(FVector(Server0->GetNetState().BobberRest), 0.1));
	TestTrue(TEXT("the other player draws player 0's line"), Other0->GetLine() && Other0->GetLine()->IsLineVisible());

	// 2) The bite: the server decides; clients see the dip but not the fish.
	if (!TestTrue(TEXT("a bite (server)"), Worlds.TickAllUntil([Server0]() { return Server0->GetFishingState() == ELureFishingState::Biting; }, Dt, 240)))
	{
		return false;
	}
	const FFishInstance Pending = Server0->GetPendingFish();
	TestTrue(TEXT("the owner sees the bite"), Worlds.TickAllUntil([Own0]() { return Own0->GetFishingState() == ELureFishingState::Biting; }, Dt, 30));
	TestFalse(TEXT("... without knowing the fish"), Own0->GetHookedFish().IsValid());

	// 3) Player 0 presses the button in time on its machine: the server hooks the fish that bit.
	Own0->PressCast();
	TestTrue(TEXT("the server hooks"), Worlds.TickAllUntil([Server0]() { return Server0->GetFishingState() == ELureFishingState::Hooked; }, Dt, 30));
	TestEqual(TEXT("the hooked fish is the one that bit"), Server0->GetHookedFish().Seed, Pending.Seed);
	TestTrue(TEXT("the owner gets the hooked fish"), Worlds.TickAllUntil([Own0, &Pending]() { return Own0->GetHookedFish().Seed == Pending.Seed && Own0->GetHookedFish().IsValid(); }, Dt, 30));
	TestTrue(TEXT("the other player sees player 0 hooked"), Worlds.TickAllUntil([Other0]() { return Other0->GetFishingState() == ELureFishingState::Hooked; }, Dt, 30));

	// 4) Landed after AutoLandDelay: everyone agrees.
	TestTrue(TEXT("landed (server)"), Worlds.TickAllUntil([Server0]() { return Server0->GetFishingState() == ELureFishingState::Idle; }, Dt, 120));
	TestEqual(TEXT("result Landed"), ResultName(Server0->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
	TestTrue(TEXT("the owner gets LastLandedFish"), Worlds.TickAllUntil([Own0, &Pending]() { return Own0->GetLastLandedFish().Seed == Pending.Seed && Own0->GetLastLandedFish().IsValid(); }, Dt, 30));
	TestTrue(TEXT("the owner's HUD says caught"), Own0->GetStatusText().Contains(TEXT("Caught:")));

	// 5) Player 1 is a modified client: it can't decide anything locally, and the server clamps what it asks.
	TestFalse(TEXT("player 1's machine can't cast by itself"), Own1->AuthorityCast(1.f, 0.f));
	Own1->AuthorityHook();
	Worlds.TickAll(5);
	TestEqual(TEXT("... the server's player 1 is untouched"), StateName(Server1->GetFishingState()), StateName(ELureFishingState::Idle));
	const FVector Eye1 = Players[1]->GetPawnViewLocation();
	FServerCastParams Hostile{ 1.0e6f, 0.f };
	CallServerRpc(Own1, TEXT("ServerCast"), &Hostile);
	if (TestTrue(TEXT("player 1's hostile ServerCast reaches the server"), Worlds.TickAllUntil([Server1]() { return Server1->GetFishingState() != ELureFishingState::Idle; }, Dt, 60)))
	{
		TestNearlyEqual(TEXT("... and is clamped to MaxCastDistance"), static_cast<float>(FVector::Dist2D(Eye1, Server1->GetNetState().BobberRest)), Quiet.MaxCastDistance, 2.f);
	}
	Worlds.TickAllUntil([Server1]() { return Server1->GetFishingState() == ELureFishingState::Waiting; }, Dt, 120);
	CallServerRpc(Own1, TEXT("ServerHook"), nullptr);
	Worlds.TickAll(10);
	TestNotEqual(TEXT("player 1's ServerHook before any bite does not hook"), StateName(Server1->GetFishingState()), StateName(ELureFishingState::Hooked));
	TestFalse(TEXT("... and gives no fish"), Server1->GetHookedFish().IsValid() || Own1->GetHookedFish().IsValid());
	return true;
}

#endif // WITH_EDITOR

} // namespace NetLocal

} // namespace QAFishing

#endif // WITH_DEV_AUTOMATION_TESTS

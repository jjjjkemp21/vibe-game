// Lure T-004 QA (qa-engineer): client prediction / replication checks that run headless, and the runtime input actions.
// Project.Movement.QA.Net.*, .Input.*  (QA design groups K, L). Real latency, corrections and proxies seen by another
// player need the playtester's 2-player PIE (see the QA playtester checklist).

#include "Tests/Movement/QAMovementTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectGlobals.h"

// =====================================================================================================================
// K: prediction and replication
// =====================================================================================================================

namespace QAMovementNet
{
	/** Compressed flags of a fresh client saved move for these wishes. */
	uint8 FlagsFor(ALurePlayerCharacter* Character, bool bSprint, bool bProne, bool bCrouch)
	{
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		FNetworkPredictionData_Client_Character* ClientData = Movement->GetPredictionData_Client_Character();
		Movement->SetSprintRequested(bSprint);
		Movement->SetProneRequested(bProne);
		Movement->bWantsToCrouch = bCrouch;
		FSavedMovePtr Move = ClientData->AllocateNewMove();
		Move->SetMoveFor(Character, QAM::Dt, FVector::ZeroVector, *ClientData);
		return Move->GetCompressedFlags();
	}

	FSavedMovePtr MoveFor(ALurePlayerCharacter* Character, bool bSprint, bool bProne)
	{
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		FNetworkPredictionData_Client_Character* ClientData = Movement->GetPredictionData_Client_Character();
		Movement->SetSprintRequested(bSprint);
		Movement->SetProneRequested(bProne);
		Movement->bWantsToCrouch = false;
		FSavedMovePtr Move = ClientData->AllocateNewMove();
		Move->SetMoveFor(Character, QAM::Dt, FVector::ZeroVector, *ClientData);
		Move->PostUpdate(Character, FSavedMove_Character::PostUpdate_Record);
		return Move;
	}

	int32 CountBits(uint8 Value)
	{
		int32 Count = 0;
		for (; Value; Value &= Value - 1)
		{
			++Count;
		}
		return Count;
	}

	constexpr uint8 EngineOwnedBits = 0x0F; // JumpPressed 0x01, WantsToCrouch 0x02, Reserved 0x04 / 0x08
	constexpr uint8 CustomBits = 0xF0;		// FLAG_Custom_0..3

	ALurePlayerCharacter* Spawn(FAutomationTestBase& Test, QAM::FWorld& World, const UDataTable* Table, const FVector& Feet = FVector::ZeroVector)
	{
		ALurePlayerCharacter* Character = World.Spawn(Test, Feet, Table);
		if (Character)
		{
			World.Tick(QAM::SettleFrames);
		}
		return Character;
	}

	/** Bit that turning one wish on adds to the compressed flags (must be exactly one custom bit). */
	uint8 WishBit(FAutomationTestBase& Test, const TCHAR* Wish, bool bSprint, bool bProne)
	{
		QAM::FWorld World;
		if (!World.Create(Test))
		{
			return 0;
		}
		ALurePlayerCharacter* Character = Spawn(Test, World, QAM::FixtureA(Test));
		if (!Character)
		{
			return 0;
		}
		const uint8 Off = FlagsFor(Character, false, false, false);
		const uint8 On = FlagsFor(Character, bSprint, bProne, false);
		const uint8 Bit = Off ^ On;
		Test.TestEqual(FString::Printf(TEXT("%s changes exactly one flag bit (0x%02x)"), Wish, Bit), CountBits(Bit), 1);
		Test.TestTrue(FString::Printf(TEXT("%s uses a custom bit (FLAG_Custom_0..3), not an engine bit (0x%02x)"), Wish, Bit), (Bit & CustomBits) == Bit && (Bit & EngineOwnedBits) == 0);
		return Bit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetSprintUsesOneCustomFlagBit, "Project.Movement.QA.Net.SprintUsesOneCustomFlagBit", QAMovement::Flags)
bool FQAMoveNetSprintUsesOneCustomFlagBit::RunTest(const FString& Parameters)
{
	return QAMovementNet::WishBit(*this, TEXT("sprint"), true, false) != 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetProneUsesOneCustomFlagBit, "Project.Movement.QA.Net.ProneUsesOneCustomFlagBit", QAMovement::Flags)
bool FQAMoveNetProneUsesOneCustomFlagBit::RunTest(const FString& Parameters)
{
	const uint8 Prone = QAMovementNet::WishBit(*this, TEXT("prone"), false, true);
	const uint8 Sprint = QAMovementNet::WishBit(*this, TEXT("sprint"), true, false);
	TestNotEqual(TEXT("prone and sprint use different bits"), static_cast<int32>(Prone), static_cast<int32>(Sprint));
	return Prone != 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetCompressedFlagsRoundTrip, "Project.Movement.QA.Net.CompressedFlagsRoundTrip", QAMovement::Flags)
bool FQAMoveNetCompressedFlagsRoundTrip::RunTest(const FString& Parameters)
{
	// Client move -> flags -> a separate "server" character: the wishes arrive intact (and the engine's crouch bit still works).
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	UDataTable* Table = QAM::FixtureA(*this);
	ALurePlayerCharacter* Client = QAMovementNet::Spawn(*this, World, Table);
	ALurePlayerCharacter* Server = QAMovementNet::Spawn(*this, World, Table, FVector(0.f, 600.f, 0.f));
	if (!Client || !Server)
	{
		return false;
	}
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();
	for (int32 Bits = 0; Bits < 8; ++Bits)
	{
		const bool bSprint = (Bits & 1) != 0;
		const bool bProne = (Bits & 2) != 0;
		const bool bCrouch = (Bits & 4) != 0;
		const uint8 Flags = QAMovementNet::FlagsFor(Client, bSprint, bProne, bCrouch);
		ServerMovement->SetSprintRequested(!bSprint);
		ServerMovement->SetProneRequested(!bProne);
		ServerMovement->bWantsToCrouch = !bCrouch;
		ServerMovement->UpdateFromCompressedFlags(Flags);
		const FString Label = FString::Printf(TEXT("sprint %d prone %d crouch %d (flags 0x%02x)"), bSprint, bProne, bCrouch, Flags);
		TestEqual(Label + TEXT(": server sprint wish"), ServerMovement->IsSprintRequested(), bSprint);
		TestEqual(Label + TEXT(": server prone wish"), ServerMovement->IsProneRequested(), bProne);
		TestEqual(Label + TEXT(": server crouch wish"), ServerMovement->IsCrouchRequested(), bCrouch);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetMovesWithDifferentFlagsDoNotCombine, "Project.Movement.QA.Net.MovesWithDifferentFlagsDoNotCombine", QAMovement::Flags)
bool FQAMoveNetMovesWithDifferentFlagsDoNotCombine::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementNet::Spawn(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	const FSavedMovePtr Walk = QAMovementNet::MoveFor(Character, false, false);
	const FSavedMovePtr Walk2 = QAMovementNet::MoveFor(Character, false, false);
	const FSavedMovePtr Sprint = QAMovementNet::MoveFor(Character, true, false);
	const FSavedMovePtr Prone = QAMovementNet::MoveFor(Character, false, true);
	const FSavedMovePtr Sprint2 = QAMovementNet::MoveFor(Character, true, false);
	TestFalse(TEXT("walk + sprint moves never combine (the sprint change would be lost)"), Walk->CanCombineWith(Sprint, Character, 1.f));
	TestFalse(TEXT("walk + prone moves never combine"), Walk->CanCombineWith(Prone, Character, 1.f));
	TestFalse(TEXT("sprint + prone moves never combine"), Sprint->CanCombineWith(Prone, Character, 1.f));
	// Whether two IDENTICAL hand-built moves combine is not deterministic headless (moves made with AllocateNewMove outside the
	// client's real move pipeline; observed both 0/0 and 1/0 across runs), so it's reported, not asserted (TEST_PLAN T004-Q3).
	AddInfo(FString::Printf(TEXT("identical walk moves combine: %d; identical sprint moves combine: %d (informational)"),
		Walk->CanCombineWith(Walk2, Character, 1.f), Sprint->CanCombineWith(Sprint2, Character, 1.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetClearResetsCustomFlags, "Project.Movement.QA.Net.ClearResetsCustomFlags", QAMovement::Flags)
bool FQAMoveNetClearResetsCustomFlags::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementNet::Spawn(*this, World, QAM::FixtureA(*this));
	if (!Character)
	{
		return false;
	}
	const FSavedMovePtr Move = QAMovementNet::MoveFor(Character, true, true);
	TestTrue(TEXT("the move carries custom bits"), (Move->GetCompressedFlags() & QAMovementNet::CustomBits) != 0);
	Move->Clear();
	TestEqual(TEXT("a recycled move carries no stale sprint/prone bits"), static_cast<int32>(Move->GetCompressedFlags() & QAMovementNet::CustomBits), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetServerIgnoresSprintWhileProne, "Project.Movement.QA.Net.ServerIgnoresSprintWhileProne", QAMovement::Flags)
bool FQAMoveNetServerIgnoresSprintWhileProne::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Server = QAMovementNet::Spawn(*this, World, QAM::FixtureA(*this));
	ALurePlayerCharacter* Client = QAMovementNet::Spawn(*this, World, QAM::FixtureA(*this), FVector(0.f, 600.f, 0.f));
	if (!Server || !Client)
	{
		return false;
	}
	const uint8 SprintAndProne = QAMovementNet::FlagsFor(Client, true, true, false);
	Server->GetLureMovement()->UpdateFromCompressedFlags(SprintAndProne);
	float Fastest = 0.f;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		World.TickMoving(Server, 1);
		Server->GetLureMovement()->UpdateFromCompressedFlags(SprintAndProne);
		Fastest = FMath::Max(Fastest, QAM::HorizontalSpeed(Server));
	}
	TestEqual(TEXT("server puts the player prone"), QAM::StanceName(Server->GetStance()), FString(TEXT("Prone")));
	TestFalse(TEXT("server does not count it as sprinting"), Server->IsSprinting());
	TestNearlyEqual(TEXT("server speed cap is Prone's"), QAM::MaxSpeedNow(Server), 89.f, 0.01f);
	TestTrue(FString::Printf(TEXT("never faster than prone speed (%.1f)"), Fastest), Fastest <= 89.f * 1.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetServerEnforcesHeadroom, "Project.Movement.QA.Net.ServerEnforcesHeadroom", QAMovement::Flags)
bool FQAMoveNetServerEnforcesHeadroom::RunTest(const FString& Parameters)
{
	// A client asking (through its move flags) to stand under the 60 cm slab stays prone on the server.
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Server = QAMovementNet::Spawn(*this, World, QAM::ShippedTable(*this));
	if (!Server || !TestTrue(TEXT("prone"), QAM::EnterStance(World, Server, ELureStance::Prone)))
	{
		return false;
	}
	World.AddSlab(QAM::CrawlGap, -300.f, 300.f);
	World.Tick(2);
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		Server->GetLureMovement()->UpdateFromCompressedFlags(0); // no prone, no crouch: "stand up"
		World.Tick(1);
	}
	TestEqual(TEXT("server keeps the player prone under the slab"), QAM::StanceName(Server->GetStance()), FString(TEXT("Prone")));
	TestFalse(TEXT("no penetration"), QAM::IsPenetrating(Server));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetStanceStateIsReplicated, "Project.Movement.QA.Net.StanceStateIsReplicated", QAMovement::Flags)
bool FQAMoveNetStanceStateIsReplicated::RunTest(const FString& Parameters)
{
	const FProperty* Prone = FindFProperty<FProperty>(ALurePlayerCharacter::StaticClass(), TEXT("bIsProne"));
	const FProperty* Crouched = FindFProperty<FProperty>(ACharacter::StaticClass(), TEXT("bIsCrouched"));
	if (!TestNotNull(TEXT("bIsProne property"), Prone) || !TestNotNull(TEXT("ACharacter::bIsCrouched property"), Crouched))
	{
		return false;
	}
	TestTrue(TEXT("bIsProne is replicated"), Prone->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("bIsProne has a RepNotify"), Prone->HasAnyPropertyFlags(CPF_RepNotify));
	TestEqual(TEXT("RepNotify function"), Prone->RepNotifyFunc, FName(TEXT("OnRep_IsProne")));
	TestNotNull(TEXT("OnRep_IsProne is a UFUNCTION"), ALurePlayerCharacter::StaticClass()->FindFunctionByName(Prone->RepNotifyFunc));

	// Like FRepLayout does before it reads RepIndex (without it, RepIndex is unassigned and the engine's duplicate check asserts).
	ALurePlayerCharacter::StaticClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Lifetime;
	GetDefault<ALurePlayerCharacter>()->GetLifetimeReplicatedProps(Lifetime);
	const FLifetimeProperty* ProneRep = Lifetime.FindByPredicate([Prone](const FLifetimeProperty& P) { return P.RepIndex == Prone->RepIndex; });
	const FLifetimeProperty* CrouchRep = Lifetime.FindByPredicate([Crouched](const FLifetimeProperty& P) { return P.RepIndex == Crouched->RepIndex; });
	if (TestNotNull(TEXT("bIsProne is registered for replication"), ProneRep))
	{
		TestEqual(TEXT("bIsProne goes to other players only (COND_SimulatedOnly, like bIsCrouched)"), static_cast<int32>(ProneRep->Condition), static_cast<int32>(COND_SimulatedOnly));
	}
	TestNotNull(TEXT("the engine's bIsCrouched is still replicated (Super called)"), CrouchRep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveNetSimulatedProxyAppliesStanceCapsule, "Project.Movement.QA.Net.SimulatedProxyAppliesStanceCapsule", QAMovement::Flags)
bool FQAMoveNetSimulatedProxyAppliesStanceCapsule::RunTest(const FString& Parameters)
{
	// What another player's machine does with the replicated posture: the proxy's capsule (and placeholder body) follow it,
	// also when bIsCrouched and bIsProne arrive in the same update, in either order.
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	UDataTable* Table = QAM::FixtureA(*this);
	ALurePlayerCharacter* Proxy = QAMovementNet::Spawn(*this, World, Table);
	if (!Proxy)
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = QAM::Resolve(Table);
	auto BodyHeight = [Proxy]() { return static_cast<float>(Proxy->GetPlaceholderBody()->Bounds.GetBox().GetSize().Z); };
	Proxy->SetRole(ROLE_SimulatedProxy);

	Proxy->SetIsProne(true);
	Proxy->OnRep_IsProne();
	QAM::TestCapsule(*this, Proxy, QAM::RowOf(Rows, ELureMovementState::Prone), TEXT("proxy prone"));
	TestNearlyEqual(TEXT("proxy prone: body height follows"), BodyHeight(), QAM::Clear(QAM::RowOf(Rows, ELureMovementState::Prone)), 1.f);

	Proxy->SetIsProne(false);
	Proxy->OnRep_IsProne();
	QAM::TestCapsule(*this, Proxy, QAM::RowOf(Rows, ELureMovementState::Stand), TEXT("proxy stand"));

	Proxy->SetIsCrouched(true);
	Proxy->OnRep_IsCrouched();
	QAM::TestCapsule(*this, Proxy, QAM::RowOf(Rows, ELureMovementState::Crouch), TEXT("proxy crouch"));

	// Crouch -> Prone in one update, crouched first.
	Proxy->SetIsProne(true);
	Proxy->OnRep_IsCrouched();
	Proxy->OnRep_IsProne();
	QAM::TestCapsule(*this, Proxy, QAM::RowOf(Rows, ELureMovementState::Prone), TEXT("proxy crouched+prone (crouch notify first)"));

	// Prone -> Crouch: prone clears while crouched stays set.
	Proxy->SetIsProne(false);
	Proxy->OnRep_IsProne();
	QAM::TestCapsule(*this, Proxy, QAM::RowOf(Rows, ELureMovementState::Crouch), TEXT("proxy prone -> crouch"));

	// Stand -> Prone+Crouched in one update, prone notify first.
	Proxy->SetIsCrouched(false);
	Proxy->OnRep_IsCrouched();
	QAM::TestCapsule(*this, Proxy, QAM::RowOf(Rows, ELureMovementState::Stand), TEXT("proxy back to stand"));
	Proxy->SetIsProne(true);
	Proxy->SetIsCrouched(true);
	Proxy->OnRep_IsProne();
	Proxy->OnRep_IsCrouched();
	QAM::TestCapsule(*this, Proxy, QAM::RowOf(Rows, ELureMovementState::Prone), TEXT("proxy prone+crouched (prone notify first)"));

	Proxy->SetRole(ROLE_Authority);
	return true;
}

// =====================================================================================================================
// L: input actions (runtime-created, looked up by name; A21)
// =====================================================================================================================

namespace QAMovementInput
{
	const TCHAR* const Names[] = { TEXT("Move"), TEXT("Look"), TEXT("Jump"), TEXT("Sprint"), TEXT("Crouch"), TEXT("Prone") };

	TArray<const FEnhancedActionKeyMapping*> MappingsFor(const UInputMappingContext* Context, const UInputAction* Action)
	{
		TArray<const FEnhancedActionKeyMapping*> Result;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == Action)
			{
				Result.Add(&Mapping);
			}
		}
		return Result;
	}

	/** An action instance with a chosen trigger event and value (the fields are protected in FInputActionInstance). */
	struct FTestActionInstance : public FInputActionInstance
	{
		FTestActionInstance(const UInputAction* InAction, ETriggerEvent InEvent, const FInputActionValue& InValue)
			: FInputActionInstance(InAction)
		{
			TriggerEvent = InEvent;
			Value = InValue;
		}
	};

	/** Runs every binding of Action for Event (what Enhanced Input does when the key fires). Returns how many ran. */
	int32 Fire(const UEnhancedInputComponent* Input, FName ActionName, ETriggerEvent Event, const FInputActionValue& Value)
	{
		const UInputAction* Action = ULureInputSubsystem::GetInputActionByName(ActionName);
		const FTestActionInstance Instance(Action, Event, Value);
		int32 Ran = 0;
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				Binding->Execute(Instance);
				++Ran;
			}
		}
		return Ran;
	}

	/** A character possessed by a local player controller (input component created by PawnClientRestart). */
	ALurePlayerCharacter* SpawnPossessed(FAutomationTestBase& Test, QAM::FWorld& World)
	{
		ALurePlayerCharacter* Character = World.Spawn(Test, FVector::ZeroVector, QAM::FixtureA(Test));
		APlayerController* Controller = World.World->SpawnActor<APlayerController>();
		if (!Character || !Test.TestNotNull(TEXT("player controller"), Controller))
		{
			return nullptr;
		}
		Controller->SetAsLocalPlayerController();
		Controller->Possess(Character);
		World.Tick(QAM::SettleFrames);
		return Character;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputAllSixActionsResolveByName, "Project.Movement.QA.Input.AllSixActionsResolveByName", QAMovement::Flags)
bool FQAMoveInputAllSixActionsResolveByName::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : QAMovementInput::Names)
	{
		TestNotNull(FString::Printf(TEXT("%s resolves"), Name), ULureInputSubsystem::GetInputActionByName(Name));
	}
	const TArray<FName> Listed = ULureInputSubsystem::GetInputActionNames();
	// QA (T-006 review): the list is EXACTLY the known actions - the 6 movement actions, T-010's Interact, T-030's AltInteract, and
	// T-006's Cast and Hook (tested in Project.Fishing.QA.Input.*). A new action must be added here on purpose; an unexpected,
	// duplicate or renamed action fails.
	const TArray<FName> Known = { TEXT("Move"), TEXT("Look"), TEXT("Jump"), TEXT("Sprint"), TEXT("Crouch"), TEXT("Prone"), TEXT("Interact"), TEXT("AltInteract"), TEXT("Cast"), TEXT("Hook") };
	TestEqual(TEXT("GetInputActionNames lists exactly the known actions (6 movement + Interact + AltInteract + Cast + Hook)"), Listed.Num(), Known.Num());
	TSet<FName> Unique;
	for (const FName& Name : Listed)
	{
		bool bDuplicate = false;
		Unique.Add(Name, &bDuplicate);
		TestFalse(FString::Printf(TEXT("%s is listed once"), *Name.ToString()), bDuplicate);
		TestTrue(FString::Printf(TEXT("%s is a known action"), *Name.ToString()), Known.Contains(Name));
		TestNotNull(FString::Printf(TEXT("%s resolves by name"), *Name.ToString()), ULureInputSubsystem::GetInputActionByName(Name));
	}
	for (const TCHAR* Name : QAMovementInput::Names)
	{
		TestTrue(FString::Printf(TEXT("GetInputActionNames lists %s"), Name), Listed.Contains(FName(Name)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputValueTypes, "Project.Movement.QA.Input.ValueTypes", QAMovement::Flags)
bool FQAMoveInputValueTypes::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : QAMovementInput::Names)
	{
		const UInputAction* Action = ULureInputSubsystem::GetInputActionByName(Name);
		if (!TestNotNull(FString::Printf(TEXT("%s"), Name), Action))
		{
			continue;
		}
		const bool bAxis = FCString::Strcmp(Name, TEXT("Move")) == 0 || FCString::Strcmp(Name, TEXT("Look")) == 0;
		TestEqual(FString::Printf(TEXT("%s value type"), Name), static_cast<int32>(Action->ValueType),
			static_cast<int32>(bAxis ? EInputActionValueType::Axis2D : EInputActionValueType::Boolean));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputUnknownNameReturnsNull, "Project.Movement.QA.Input.UnknownNameReturnsNull", QAMovement::Flags)
bool FQAMoveInputUnknownNameReturnsNull::RunTest(const FString& Parameters)
{
	TestNull(TEXT("'Fly'"), ULureInputSubsystem::GetInputActionByName(TEXT("Fly")));
	TestNull(TEXT("NAME_None"), ULureInputSubsystem::GetInputActionByName(NAME_None));
	TestNull(TEXT("'IA_Lure_Sprint' (the object name, not the action name)"), ULureInputSubsystem::GetInputActionByName(TEXT("IA_Lure_Sprint")));
	TestNotNull(TEXT("names are case-insensitive: 'sprint'"), ULureInputSubsystem::GetInputActionByName(TEXT("sprint")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputLookupIsStableAndMapped, "Project.Movement.QA.Input.LookupIsStableAndMapped", QAMovement::Flags)
bool FQAMoveInputLookupIsStableAndMapped::RunTest(const FString& Parameters)
{
	// The playtester injects the object it gets by name: it must be the one the mapping context maps and the character binds.
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	if (!TestNotNull(TEXT("default mapping context"), Context))
	{
		return false;
	}
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementInput::SpawnPossessed(*this, World);
	ALurePlayerCharacter* Second = World.Spawn(*this, FVector(0.f, 600.f, 0.f), QAM::FixtureA(*this));
	const UEnhancedInputComponent* Input = Character ? Cast<UEnhancedInputComponent>(Character->InputComponent) : nullptr;
	if (!Second || !TestNotNull(TEXT("enhanced input component after possession"), Input))
	{
		return false;
	}
	for (const TCHAR* Name : QAMovementInput::Names)
	{
		const UInputAction* Action = ULureInputSubsystem::GetInputActionByName(Name);
		TestTrue(FString::Printf(TEXT("%s: same object on every lookup"), Name), Action && ULureInputSubsystem::GetInputActionByName(Name) == Action);
		TestTrue(FString::Printf(TEXT("%s: the mapping context maps this object"), Name), QAMovementInput::MappingsFor(Context, Action).Num() > 0);
		TestTrue(FString::Printf(TEXT("%s: the character binds this object"), Name), Input->GetActionEventBindings().ContainsByPredicate(
			[Action](const TUniquePtr<FEnhancedInputActionEventBinding>& Binding) { return Binding && Binding->GetAction() == Action; }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputActionsSurviveGarbageCollection, "Project.Movement.QA.Input.ActionsSurviveGarbageCollection", QAMovement::Flags)
bool FQAMoveInputActionsSurviveGarbageCollection::RunTest(const FString& Parameters)
{
	TArray<TWeakObjectPtr<UInputAction>> Before;
	for (const TCHAR* Name : QAMovementInput::Names)
	{
		Before.Add(ULureInputSubsystem::GetInputActionByName(Name));
	}
	TWeakObjectPtr<UInputMappingContext> ContextBefore = ULureInputSubsystem::GetDefaultMappingContext();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(QAMovementInput::Names)); ++Index)
	{
		TestTrue(FString::Printf(TEXT("%s survives GC and is still the looked-up object"), QAMovementInput::Names[Index]),
			Before[Index].IsValid() && ULureInputSubsystem::GetInputActionByName(QAMovementInput::Names[Index]) == Before[Index].Get());
	}
	TestTrue(TEXT("mapping context survives GC"), ContextBefore.IsValid() && ULureInputSubsystem::GetDefaultMappingContext() == ContextBefore.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputKeyboardAndGamepadBindings, "Project.Movement.QA.Input.KeyboardAndGamepadBindings", QAMovement::Flags)
bool FQAMoveInputKeyboardAndGamepadBindings::RunTest(const FString& Parameters)
{
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	if (!TestNotNull(TEXT("default mapping context"), Context))
	{
		return false;
	}
	for (const TCHAR* Name : QAMovementInput::Names)
	{
		bool bKeyboardMouse = false;
		bool bGamepad = false;
		for (const FEnhancedActionKeyMapping* Mapping : QAMovementInput::MappingsFor(Context, ULureInputSubsystem::GetInputActionByName(Name)))
		{
			bGamepad |= Mapping->Key.IsGamepadKey();
			bKeyboardMouse |= !Mapping->Key.IsGamepadKey() && Mapping->Key.IsValid();
		}
		TestTrue(FString::Printf(TEXT("%s has a keyboard/mouse key"), Name), bKeyboardMouse);
		TestTrue(FString::Printf(TEXT("%s has a gamepad key"), Name), bGamepad);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputNoKeyBoundToTwoActions, "Project.Movement.QA.Input.NoKeyBoundToTwoActions", QAMovement::Flags)
bool FQAMoveInputNoKeyBoundToTwoActions::RunTest(const FString& Parameters)
{
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	if (!TestNotNull(TEXT("default mapping context"), Context))
	{
		return false;
	}
	TMap<FKey, const UInputAction*> Owner;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (const UInputAction* const* Existing = Owner.Find(Mapping.Key))
		{
			TestTrue(FString::Printf(TEXT("key %s drives one action only (%s vs %s)"), *Mapping.Key.ToString(), *GetNameSafe(*Existing), *GetNameSafe(Mapping.Action)), *Existing == Mapping.Action);
		}
		Owner.Add(Mapping.Key, Mapping.Action);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputPlaytestKeyF8IsFree, "Project.Movement.QA.Input.PlaytestKeyF8IsFree", QAMovement::Flags)
bool FQAMoveInputPlaytestKeyF8IsFree::RunTest(const FString& Parameters)
{
	// Regression guard: F8 is the playtest feedback key (T-003); movement must never take it.
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	if (!TestNotNull(TEXT("default mapping context"), Context))
	{
		return false;
	}
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		TestFalse(FString::Printf(TEXT("%s is not on F8"), *GetNameSafe(Mapping.Action)), Mapping.Key == EKeys::F8);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputByNameLookupCallableFromPython, "Project.Movement.QA.Input.ByNameLookupCallableFromPython", QAMovement::Flags)
bool FQAMoveInputByNameLookupCallableFromPython::RunTest(const FString& Parameters)
{
	for (const TCHAR* Function : { TEXT("GetInputActionByName"), TEXT("GetDefaultMappingContext"), TEXT("GetInputActionNames") })
	{
		const UFunction* Found = ULureInputSubsystem::StaticClass()->FindFunctionByName(Function);
		if (TestNotNull(FString::Printf(TEXT("%s is a UFUNCTION"), Function), Found))
		{
			TestTrue(FString::Printf(TEXT("%s is static and BlueprintCallable (usable from Python without an instance)"), Function), Found->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintCallable));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputPossessionBindsAllActions, "Project.Movement.QA.Input.PossessionBindsAllActions", QAMovement::Flags)
bool FQAMoveInputPossessionBindsAllActions::RunTest(const FString& Parameters)
{
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementInput::SpawnPossessed(*this, World);
	const UEnhancedInputComponent* Input = Character ? Cast<UEnhancedInputComponent>(Character->InputComponent) : nullptr;
	if (!TestNotNull(TEXT("an EnhancedInputComponent after local possession"), Input))
	{
		return false;
	}
	for (const TCHAR* Name : QAMovementInput::Names)
	{
		const UInputAction* Action = ULureInputSubsystem::GetInputActionByName(Name);
		int32 Count = 0;
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			Count += (Binding && Binding->GetAction() == Action) ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("%s is bound (%d bindings)"), Name, Count), Count > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveInputExecutingBindingsDrivesStances, "Project.Movement.QA.Input.ExecutingBindingsDrivesStances", QAMovement::Flags)
bool FQAMoveInputExecutingBindingsDrivesStances::RunTest(const FString& Parameters)
{
	// Action -> handler wiring with the default control modes (sprint hold, crouch toggle, prone toggle).
	const ULureCharacterSettings* Settings = GetDefault<ULureCharacterSettings>();
	TestFalse(TEXT("default: sprint is hold"), Settings->bSprintIsToggle);
	TestTrue(TEXT("default: crouch is toggle"), Settings->bCrouchIsToggle);
	TestTrue(TEXT("default: prone is toggle"), Settings->bProneIsToggle);

	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementInput::SpawnPossessed(*this, World);
	const UEnhancedInputComponent* Input = Character ? Cast<UEnhancedInputComponent>(Character->InputComponent) : nullptr;
	if (!TestNotNull(TEXT("enhanced input component"), Input))
	{
		return false;
	}
	const FInputActionValue Pressed(true);
	const FInputActionValue Released(false);
	auto Stance = [Character]() { return QAM::StanceName(Character->GetStance()); };

	TestTrue(TEXT("Crouch press runs a handler"), QAMovementInput::Fire(Input, TEXT("Crouch"), ETriggerEvent::Started, Pressed) > 0);
	World.Tick(QAM::SettleFrames);
	TestEqual(TEXT("Crouch press -> Crouch"), Stance(), FString(TEXT("Crouch")));
	QAMovementInput::Fire(Input, TEXT("Crouch"), ETriggerEvent::Completed, Released);
	World.Tick(QAM::SettleFrames);
	TestEqual(TEXT("Crouch release keeps the toggle"), Stance(), FString(TEXT("Crouch")));

	QAMovementInput::Fire(Input, TEXT("Prone"), ETriggerEvent::Started, Pressed);
	QAMovementInput::Fire(Input, TEXT("Prone"), ETriggerEvent::Completed, Released);
	World.Tick(QAM::SettleFrames);
	TestEqual(TEXT("Prone press -> Prone"), Stance(), FString(TEXT("Prone")));

	QAMovementInput::Fire(Input, TEXT("Prone"), ETriggerEvent::Started, Pressed);
	QAMovementInput::Fire(Input, TEXT("Prone"), ETriggerEvent::Completed, Released);
	World.Tick(QAM::SettleFrames);
	TestEqual(TEXT("Prone press again -> Stand"), Stance(), FString(TEXT("Stand")));

	QAMovementInput::Fire(Input, TEXT("Sprint"), ETriggerEvent::Started, Pressed);
	TestTrue(TEXT("Sprint press -> sprint wish"), Character->GetLureMovement()->IsSprintRequested());
	const FVector Start = Character->GetActorLocation();
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		QAMovementInput::Fire(Input, TEXT("Move"), ETriggerEvent::Triggered, FInputActionValue(FInputActionValue::Axis2D(0.f, 1.f)));
		World.Tick(1);
	}
	TestTrue(FString::Printf(TEXT("Move forward moves the character forward (%.1f cm)"), Character->GetActorLocation().X - Start.X), Character->GetActorLocation().X - Start.X > 50.f);
	TestTrue(TEXT("sprinting while the key is held and moving"), Character->IsSprinting());
	QAMovementInput::Fire(Input, TEXT("Sprint"), ETriggerEvent::Completed, Released);
	TestFalse(TEXT("Sprint release (hold mode) ends the wish"), Character->GetLureMovement()->IsSprintRequested());

	const float GroundZ = Character->GetActorLocation().Z;
	QAMovementInput::Fire(Input, TEXT("Jump"), ETriggerEvent::Started, Pressed);
	World.Tick(5);
	QAMovementInput::Fire(Input, TEXT("Jump"), ETriggerEvent::Completed, Released);
	TestTrue(TEXT("Jump press jumps"), Character->GetActorLocation().Z > GroundZ + 5.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

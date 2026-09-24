// Lure T-026 netfix: networking of the climbs (Project.Movement.Swim.Net.*). Review findings N0-N3 and test gap T1
// (Saved/AgentLogs/review/20260923-T026-ultracode-review-partial.md); contract: docs/specs/swimming.md "Networking".
//
// A standalone test world has no net driver, so these tests call the engine functions the network path calls, in the
// same order, on separate copies of the scene (one test world per machine, same geometry at the same place):
//  - owning client, per move (ControlledCharacterMove + ReplicateMoveToServer): CheckJumpInput, then SetMoveFor (the
//    saved move and its compressed flags), PerformMovement, PostUpdate; the move stays in SavedMoves until acknowledged;
//  - server, per client move (ServerMove_PerformMovement): MoveAutonomous(time stamp, flags, acceleration);
//  - the server's reply (ServerSendMoveResponse -> MoveResponsePacked_ClientReceive): ServerFillResponseData, Serialize
//    through FNetBitWriter / FNetBitReader, ClientHandleMoveResponse; then the replay (ClientUpdatePositionAfterServerUpdate);
//  - other players' copies (SimulatedTick -> SimulateMovement): ROLE_SimulatedProxy, ApplyNetworkMovementMode, MoveSmooth.
// Latency, packet loss and the RPCs themselves still need the playtester's 2-player PIE.
// Names are qualified (LureSwimNet::) on purpose: unity builds can merge this file with others that use using-directives.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Character/LureLadder.h"
#include "Character/LureWaterVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/CoreNet.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Tests/Movement/LureMovementTestAccess.h"
#include "Tests/Movement/LureSwimTestListener.h"

namespace LureSwimNet
{
	using FAccess = FLureMovementTestAccess;

	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Seabed of the test sea, cm (the water surface is at z = 0). */
	constexpr float SeabedZ = -600.f;

	/** The dock's water-side face (the dock extends toward +X, 1000 cm wide in Y). */
	constexpr float FaceX = 100.f;

	/** A swimmer's capsule center 20 cm in front of the face (radius 34; ClimbOutReach is 45). */
	constexpr float SwimX = FaceX - 34.f - 20.f;

	/** Client moves that cover any climb here (a 60 cm edge takes ~51 moves at 60 fps); stays under the engine's 96 saved moves. */
	constexpr int32 ClimbMoves = 70;

	/** Same place, cm: client and server simulate the same moves from the same state, so they must agree to the float. */
	constexpr float SamePlace = 0.01f;

	UDataTable* ShippedTable(FAutomationTestBase& Test)
	{
		FString Csv;
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv"));
		if (!Test.TestTrue(TEXT("data/tables/DT_Movement.csv loads"), FFileHelper::LoadFileToString(Csv, *Path)))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureMovementRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		Test.TestEqual(FString::Printf(TEXT("CSV import problems (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		return Table;
	}

	FLureMovementRow RowOf(const UDataTable* Table, ELureMovementState State)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Table, Rows, Problems);
		return Rows[static_cast<int32>(State)];
	}

	/** One machine's copy of the scene: seabed, water (surface z = 0), optionally a dock of DockHeight at FaceX. */
	struct FMachine
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test, float DockHeight)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			AddBox(FVector(0.f, 0.f, SeabedZ - 50.f), FVector(4000.f, 4000.f, 50.f));
			if (DockHeight > 0.f)
			{
				AddBox(FVector(FaceX + 250.f, 0.f, 0.5f * (DockHeight + SeabedZ)), FVector(250.f, 500.f, 0.5f * (DockHeight - SeabedZ)));
			}
			const FTransform WaterTransform(FRotator::ZeroRotator, FVector::ZeroVector);
			ALureWaterVolume* Water = World->SpawnActorDeferred<ALureWaterVolume>(ALureWaterVolume::StaticClass(), WaterTransform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Test.TestNotNull(TEXT("water volume spawns"), Water))
			{
				return false;
			}
			Water->SurfaceHalfSize = FVector2D(3500.0, 3500.0);
			Water->WaterDepth = 800.f;
			Water->FinishSpawning(WaterTransform);
			Tick(1);
			return true;
		}

		AActor* AddBox(const FVector& Center, const FVector& Extent)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
			Box->SetMobility(EComponentMobility::Static);
			Box->SetBoxExtent(Extent, false);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->SetRelativeLocation_Direct(Center);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			return Actor;
		}

		/** Spawns a character with its capsule center at Center (table applied before BeginPlay). Call Tick to let it settle. */
		ALurePlayerCharacter* Spawn(FAutomationTestBase& Test, const FVector& Center, const UDataTable* Table)
		{
			const FTransform Transform(FRotator::ZeroRotator, Center);
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Test.TestNotNull(TEXT("character spawns"), Character))
			{
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Table);
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			Character->FinishSpawning(Transform);
			return Character;
		}

		/** 1 m above the water at (X, Y): falls in and floats. */
		ALurePlayerCharacter* SpawnSwimmer(FAutomationTestBase& Test, const UDataTable* Table, float X = SwimX, float Y = 0.f)
		{
			return Spawn(Test, FVector(X, Y, 190.f), Table);
		}

		void Tick(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(Dt);
			}
		}
	};

	/** Frames for spawned characters to fall in (or land) and settle. */
	constexpr int32 SettleFrames = 180;

	FNetworkPredictionData_Client_Character& ClientDataOf(ALurePlayerCharacter* Character)
	{
		return *Character->GetLureMovement()->GetPredictionData_Client_Character();
	}

	/** The owning client's move, first half (ControlledCharacterMove): the Jump press is handled before the move is saved. */
	void BeginClientMove(ALurePlayerCharacter* Character, bool bPressJump)
	{
		if (bPressJump)
		{
			Character->Jump();
		}
		Character->CheckJumpInput(Dt);
	}

	/** Second half (ReplicateMoveToServer): stamp and save the move, run it, record its result. It stays unacknowledged in SavedMoves. */
	FSavedMovePtr FinishClientMove(ALurePlayerCharacter* Character)
	{
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		FNetworkPredictionData_Client_Character& ClientData = ClientDataOf(Character);
		ClientData.CurrentTimeStamp += Dt;
		FSavedMovePtr Move = ClientData.CreateSavedMove();
		Move->SetMoveFor(Character, Dt, FVector::ZeroVector, ClientData);
		FAccess::SetAcceleration(*Movement, Move->Acceleration.GetClampedToMaxSize(Movement->GetMaxAcceleration()));
		FAccess::PerformMovement(*Movement, Move->DeltaTime);
		Move->PostUpdate(Character, FSavedMove_Character::PostUpdate_Record);
		ClientData.SavedMoves.Push(Move);
		return Move;
	}

	FSavedMovePtr ClientMove(ALurePlayerCharacter* Character, bool bPressJump = false)
	{
		BeginClientMove(Character, bPressJump);
		return FinishClientMove(Character);
	}

	/** The server runs a client move from what the RPC carries: time stamp, delta time, compressed flags, acceleration. */
	void ServerMove(ALurePlayerCharacter* Server, const FSavedMove_Character& Move)
	{
		FAccess::MoveAutonomous(*Server->GetLureMovement(), Move.TimeStamp, Move.DeltaTime, Move.GetCompressedFlags(), Move.Acceleration);
	}

	/** What ServerMoveHandleClientError records about the server's copy after the move at ClientTimeStamp. */
	FClientAdjustment AdjustmentFrom(ALurePlayerCharacter* Server, float ClientTimeStamp, bool bCorrection)
	{
		const ULureCharacterMovementComponent* Movement = Server->GetLureMovement();
		FClientAdjustment Adjustment;
		Adjustment.TimeStamp = ClientTimeStamp;
		Adjustment.DeltaTime = Dt;
		Adjustment.bAckGoodMove = !bCorrection;
		Adjustment.NewLoc = Server->GetActorLocation();
		Adjustment.NewVel = Movement->Velocity;
		Adjustment.NewRot = Server->GetActorRotation();
		Adjustment.GravityDirection = Movement->GetGravityDirection();
		Adjustment.MovementMode = Movement->PackNetworkMovementMode();
		return Adjustment;
	}

	/**
	 *  The server's reply about the client's move at ClientTimeStamp, over the wire: filled from the server's copy
	 *  (ServerFillResponseData), written to bits, read into the client's own container and handled there.
	 */
	bool SendReply(FAutomationTestBase& Test, ALurePlayerCharacter* Server, ALurePlayerCharacter* Client, float ClientTimeStamp, bool bCorrection = true)
	{
		ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();
		FLureMoveResponseDataContainer& Sent = FAccess::ResponseData(*ServerMovement);
		Sent.ServerFillResponseData(*ServerMovement, AdjustmentFrom(Server, ClientTimeStamp, bCorrection));
		FNetBitWriter Writer(nullptr, 0);
		const bool bWritten = Sent.Serialize(*ServerMovement, Writer, nullptr) && !Writer.IsError();

		ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
		FLureMoveResponseDataContainer& Received = FAccess::ResponseData(*ClientMovement);
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		const bool bRead = bWritten && Received.Serialize(*ClientMovement, Reader, nullptr) && !Reader.IsError();
		if (!Test.TestTrue(TEXT("the server's reply goes through serialization"), bRead))
		{
			return false;
		}
		ClientMovement->ClientHandleMoveResponse(Received);
		return true;
	}

	float FeetZ(const ALurePlayerCharacter* Character)
	{
		return static_cast<float>(Character->GetActorLocation().Z) - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	float Distance(const ALurePlayerCharacter* A, const ALurePlayerCharacter* B)
	{
		return static_cast<float>((A->GetActorLocation() - B->GetActorLocation()).Size());
	}

	FString EventsText(const ULureSwimTestListener* Listener)
	{
		TArray<FString> Parts;
		for (const bool bIn : Listener->Events)
		{
			Parts.Add(bIn ? TEXT("in") : TEXT("out"));
		}
		return FString::Join(Parts, TEXT(","));
	}

	ULureSwimTestListener* Listen(ALurePlayerCharacter* Character)
	{
		ULureSwimTestListener* Listener = NewObject<ULureSwimTestListener>();
		Character->OnSwimStateChanged.AddDynamic(Listener, &ULureSwimTestListener::OnSwimStateChanged);
		return Listener;
	}

	/** Standing on the dock after a climb out of the water (the climb ended where it should). */
	void TestStandingOnDock(FAutomationTestBase& Test, const ALurePlayerCharacter* Character, float DockHeight, const FString& Label)
	{
		const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		Test.TestTrue(Label + TEXT(": walking"), Movement->IsMovingOnGround());
		Test.TestFalse(Label + TEXT(": out of the water"), Character->IsSwimming());
		Test.TestNearlyEqual(Label + TEXT(": feet on the dock top"), FeetZ(Character), DockHeight, 3.f);
		Test.TestTrue(Label + TEXT(": past the dock's edge"), Character->GetActorLocation().X > FaceX);
	}

	uint8 PackedMode(ULureCharacterMovementComponent* Movement, EMovementMode Mode, ELureCustomMovementMode Custom = ELureCustomMovementMode::None)
	{
		TGuardValue<TEnumAsByte<EMovementMode>> ModeGuard(Movement->MovementMode, TEnumAsByte<EMovementMode>(Mode));
		TGuardValue<uint8> CustomGuard(Movement->CustomMovementMode, static_cast<uint8>(Custom));
		return Movement->PackNetworkMovementMode();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// N0: the Jump press reaches the server
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetJumpFlagTest, "Project.Movement.Swim.Net.JumpFlagReachesTheServer", LureSwimNet::Flags)

bool FLureSwimNetJumpFlagTest::RunTest(const FString& Parameters)
{
	// A client presses Jump at the 60 cm jetty. Before the fix the climb started inside CheckJumpInput, which cleared
	// bPressedJump before the move was saved: no Jump flag, the server kept swimming, and the client was corrected back.
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	LureSwimNet::FMachine RefusingMachine; // a 61 cm edge (too high), and open water further out
	if (!Table || !ClientMachine.Create(*this, 60.f) || !ServerMachine.Create(*this, 60.f) || !RefusingMachine.Create(*this, 61.f))
	{
		return false;
	}
	ALurePlayerCharacter* Client = ClientMachine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* HighEdge = RefusingMachine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* OpenWater = RefusingMachine.SpawnSwimmer(*this, Table, -1500.f);
	if (!Client || !Server || !HighEdge || !OpenWater)
	{
		return false;
	}
	ClientMachine.Tick(LureSwimNet::SettleFrames);
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	RefusingMachine.Tick(LureSwimNet::SettleFrames);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();
	TestTrue(TEXT("both copies swim at the jetty"), ClientMovement->IsSwimming() && ServerMovement->IsSwimming());
	TestTrue(TEXT("both copies start at the same place"), LureSwimNet::Distance(Client, Server) < LureSwimNet::SamePlace);

	// 1. The owning client: Jump queues the climb; the saved move carries the Jump flag.
	LureSwimNet::BeginClientMove(Client, /*bPressJump*/ true);
	TestEqual(TEXT("client: still swimming after CheckJumpInput (the climb starts inside the move)"), static_cast<int32>(ClientMovement->MovementMode), static_cast<int32>(MOVE_Swimming));
	TestTrue(TEXT("client: Jump is still pressed when the move is saved"), static_cast<bool>(Client->bPressedJump));
	TestTrue(TEXT("client: the climb is queued"), LureSwimNet::FAccess::IsClimbOutRequested(*ClientMovement));
	const FSavedMovePtr JumpMove = LureSwimNet::FinishClientMove(Client);
	TestTrue(TEXT("client: the saved move carries FLAG_JumpPressed"), (JumpMove->GetCompressedFlags() & FSavedMove_Character::FLAG_JumpPressed) != 0);
	TestTrue(TEXT("client: the climb started inside the move"), ClientMovement->IsClimbingOut());
	TestTrue(TEXT("client: still in the water while climbing"), Client->IsSwimming());
	TestFalse(TEXT("client: the request is used up"), LureSwimNet::FAccess::IsClimbOutRequested(*ClientMovement));

	// 2. The server gets only the flags and plans its own climb: the same result, so no correction, move after move.
	LureSwimNet::ServerMove(Server, *JumpMove);
	TestTrue(TEXT("server: climbs out from the Jump flag"), ServerMovement->IsClimbingOut());
	float WorstError = LureSwimNet::Distance(Client, Server);
	bool bSameModes = ServerMovement->PackNetworkMovementMode() == JumpMove->EndPackedMovementMode;
	for (int32 Index = 0; Index < LureSwimNet::ClimbMoves; ++Index)
	{
		const FSavedMovePtr Move = LureSwimNet::ClientMove(Client);
		LureSwimNet::ServerMove(Server, *Move);
		WorstError = FMath::Max(WorstError, LureSwimNet::Distance(Client, Server));
		bSameModes &= ServerMovement->PackNetworkMovementMode() == Move->EndPackedMovementMode;
	}
	TestTrue(FString::Printf(TEXT("client and server agree on every move (worst %.4f cm)"), WorstError), WorstError < LureSwimNet::SamePlace);
	TestTrue(TEXT("client and server agree on the mode of every move"), bSameModes);
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("client"));
	LureSwimNet::TestStandingOnDock(*this, Server, 60.f, TEXT("server"));

	// 3. Only the Jump bit travels, never a plan: a client can't force a climb the server wouldn't allow.
	LureSwimNet::ServerMove(HighEdge, *JumpMove);
	LureSwimNet::ServerMove(OpenWater, *JumpMove);
	TestTrue(TEXT("server at a 61 cm edge: the same flags don't climb"), HighEdge->GetLureMovement()->IsSwimming());
	TestTrue(TEXT("server in open water: the same flags don't climb"), OpenWater->GetLureMovement()->IsSwimming());

	// 4. A queued climb lives for one move only: a move that ends before its physics (zero time) drops it.
	ALurePlayerCharacter* Second = ServerMachine.SpawnSwimmer(*this, Table, LureSwimNet::SwimX, 300.f);
	if (!Second)
	{
		return false;
	}
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	ULureCharacterMovementComponent* SecondMovement = Second->GetLureMovement();
	LureSwimNet::BeginClientMove(Second, true);
	TestTrue(TEXT("queued"), LureSwimNet::FAccess::IsClimbOutRequested(*SecondMovement));
	LureSwimNet::FAccess::PerformMovement(*SecondMovement, 0.f);
	TestFalse(TEXT("a zero-time move drops the request"), LureSwimNet::FAccess::IsClimbOutRequested(*SecondMovement));
	LureSwimNet::ClientMove(Second);
	TestTrue(TEXT("the next move (no Jump) doesn't climb"), SecondMovement->IsSwimming());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetReplayTest, "Project.Movement.Swim.Net.ReplayReplansTheClimbFromTheJumpFlag", LureSwimNet::Flags)

bool FLureSwimNetReplayTest::RunTest(const FString& Parameters)
{
	// A correction for a move before the Jump makes the client replay the jump move: the replay must plan the climb again
	// from the saved Jump flag and end where the prediction did, without extra in/out events.
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ClientMachine.Create(*this, 60.f) || !ServerMachine.Create(*this, 60.f))
	{
		return false;
	}
	ALurePlayerCharacter* Client = ClientMachine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table);
	if (!Client || !Server)
	{
		return false;
	}
	ClientMachine.Tick(LureSwimNet::SettleFrames);
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();

	const FSavedMovePtr BeforeJump = LureSwimNet::ClientMove(Client);
	LureSwimNet::ClientMove(Client, /*bPressJump*/ true);
	for (int32 Index = 0; Index < LureSwimNet::ClimbMoves; ++Index)
	{
		LureSwimNet::ClientMove(Client);
	}
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("predicted"));
	const FVector Predicted = Client->GetActorLocation();
	ULureSwimTestListener* Listener = LureSwimNet::Listen(Client);
	FGCObjectScopeGuard KeepListener(Listener);

	// The server's copy after the first move (still swimming), sent as a correction.
	LureSwimNet::ServerMove(Server, *BeforeJump);
	if (!LureSwimNet::SendReply(*this, Server, Client, BeforeJump->TimeStamp))
	{
		return false;
	}
	TestTrue(TEXT("the correction puts the client back in the water"), ClientMovement->IsSwimming());
	TestEqual(TEXT("the moves after it wait for the replay"), LureSwimNet::ClientDataOf(Client).SavedMoves.Num(), LureSwimNet::ClimbMoves + 1);

	LureSwimNet::FAccess::ReplayUnacknowledgedMoves(*ClientMovement);
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("replayed"));
	const float Off = static_cast<float>((Client->GetActorLocation() - Predicted).Size());
	TestTrue(FString::Printf(TEXT("the replay ends where the prediction did (%.4f cm)"), Off), Off < LureSwimNet::SamePlace);
	TestEqual(TEXT("no swim events from the correction and replay (the settled state didn't change)"), LureSwimNet::EventsText(Listener), FString());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// N2 / N3 / T1: corrections carry the climb plan and the takeoff height
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetResponseDataTest, "Project.Movement.Swim.Net.ReplyCarriesClimbStateOnlyInCorrections", LureSwimNet::Flags)

bool FLureSwimNetResponseDataTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ServerMachine.Create(*this, 60.f))
	{
		return false;
	}
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table);
	if (!Server)
	{
		return false;
	}
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	ULureCharacterMovementComponent* Movement = Server->GetLureMovement();
	TestTrue(TEXT("the component uses the Lure reply container"), LureSwimNet::FAccess::UsesLureResponseData(*Movement));

	// The server climbs (from a Jump flag) and is mid-climb.
	LureSwimNet::FAccess::MoveAutonomous(*Movement, 1.f, LureSwimNet::Dt, FSavedMove_Character::FLAG_JumpPressed, FVector::ZeroVector);
	for (int32 Index = 0; Index < 10; ++Index)
	{
		LureSwimNet::FAccess::MoveAutonomous(*Movement, 1.f + (Index + 1) * LureSwimNet::Dt, LureSwimNet::Dt, 0, FVector::ZeroVector);
	}
	if (!TestTrue(TEXT("server: mid-climb"), Movement->IsClimbingOut() && LureSwimNet::FAccess::HasClimbPlan(*Movement)))
	{
		return false;
	}
	const FLureClimbPlan& Plan = LureSwimNet::FAccess::ClimbPlan(*Movement);
	LureSwimNet::FAccess::SetTakeoffFeetHeight(*Movement, 37.25f);

	auto RoundTrip = [&](FCharacterMoveResponseDataContainer& Sent, FCharacterMoveResponseDataContainer& Received, bool bCorrection, int64& OutBits)
	{
		Sent.ServerFillResponseData(*Movement, LureSwimNet::AdjustmentFrom(Server, 2.f, bCorrection));
		FNetBitWriter Writer(nullptr, 0);
		const bool bWritten = Sent.Serialize(*Movement, Writer, nullptr) && !Writer.IsError();
		OutBits = Writer.GetNumBits();
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		return bWritten && Received.Serialize(*Movement, Reader, nullptr) && !Reader.IsError() && Reader.AtEnd();
	};

	// A correction mid-climb: the plan and the takeoff arrive exactly.
	FLureMoveResponseDataContainer Sent;
	FLureMoveResponseDataContainer Received;
	int64 CorrectionBits = 0;
	TestTrue(TEXT("correction: written and read back, nothing left over"), RoundTrip(Sent, Received, true, CorrectionBits));
	TestTrue(TEXT("correction: it is a correction"), Received.IsCorrection());
	TestTrue(TEXT("correction: the server's plan is in it"), Received.bHasClimbPlan);
	TestTrue(TEXT("correction: plan start"), Received.ClimbPlan.Start == Plan.Start);
	TestTrue(TEXT("correction: plan rise"), Received.ClimbPlan.RiseTo == Plan.RiseTo);
	TestTrue(TEXT("correction: plan target"), Received.ClimbPlan.Target == Plan.Target);
	TestEqual(TEXT("correction: plan speed"), Received.ClimbPlan.Speed, Plan.Speed);
	TestEqual(TEXT("correction: plan edge height"), Received.ClimbPlan.LedgeHeight, Plan.LedgeHeight);
	TestEqual(TEXT("correction: plan ladder flag"), Received.ClimbPlan.bUsesLadder, Plan.bUsesLadder);
	TestEqual(TEXT("correction: takeoff height"), Received.TakeoffFeetHeight, 37.25f);
	TestTrue(TEXT("correction: the engine's location"), Received.ClientAdjustment.NewLoc == Server->GetActorLocation());
	TestEqual(TEXT("correction: the engine's mode (climbing out)"), static_cast<int32>(Received.ClientAdjustment.MovementMode), static_cast<int32>(Movement->PackNetworkMovementMode()));
	AddInfo(FString::Printf(TEXT("A correction during a climb is %lld bits (the engine allows %d)."), CorrectionBits, 4096));

	// Acks (frequent) keep the engine's size and carry nothing extra; reading one clears what the container held.
	int64 AckBits = 0;
	int64 EngineAckBits = 0;
	TestTrue(TEXT("ack: written and read back"), RoundTrip(Sent, Received, false, AckBits));
	TestFalse(TEXT("ack: no plan"), Received.bHasClimbPlan);
	TestEqual(TEXT("ack: no takeoff height"), Received.TakeoffFeetHeight, 0.f);
	FCharacterMoveResponseDataContainer EngineSent;
	FCharacterMoveResponseDataContainer EngineReceived;
	TestTrue(TEXT("engine ack: written and read back"), RoundTrip(EngineSent, EngineReceived, false, EngineAckBits));
	TestEqual(TEXT("an ack is exactly the engine's size"), AckBits, EngineAckBits);

	// A correction while not climbing: no plan, the takeoff still travels.
	ALurePlayerCharacter* Swimmer = ServerMachine.SpawnSwimmer(*this, Table, -1500.f);
	if (!Swimmer)
	{
		return false;
	}
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	ULureCharacterMovementComponent* SwimmerMovement = Swimmer->GetLureMovement();
	LureSwimNet::FAccess::SetTakeoffFeetHeight(*SwimmerMovement, -12.5f);
	FLureMoveResponseDataContainer SwimSent;
	FLureMoveResponseDataContainer SwimReceived;
	SwimSent.ServerFillResponseData(*SwimmerMovement, LureSwimNet::AdjustmentFrom(Swimmer, 3.f, true));
	FNetBitWriter Writer(nullptr, 0);
	TestTrue(TEXT("swimming correction: written"), SwimSent.Serialize(*SwimmerMovement, Writer, nullptr));
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	TestTrue(TEXT("swimming correction: read back"), SwimReceived.Serialize(*SwimmerMovement, Reader, nullptr) && Reader.AtEnd());
	TestFalse(TEXT("swimming correction: no plan"), SwimReceived.bHasClimbPlan);
	TestEqual(TEXT("swimming correction: takeoff height"), SwimReceived.TakeoffFeetHeight, -12.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetUnpredictedClimbTest, "Project.Movement.Swim.Net.CorrectionIntoAnUnpredictedClimbFinishesIt", LureSwimNet::Flags)

bool FLureSwimNetUnpredictedClimbTest::RunTest(const FString& Parameters)
{
	// Review N2 case A / T1. On the client another player's delayed copy still stands where the climb would land, so the
	// client's Jump finds no room and it keeps swimming; on the server that player has moved on, so the server climbs.
	// The correction puts the client into the climb: with the server's plan its replay finishes the climb (before the
	// fix it held still in the water, then was pulled up the edge once per correction).
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ClientMachine.Create(*this, 60.f) || !ServerMachine.Create(*this, 60.f))
	{
		return false;
	}
	ALurePlayerCharacter* Client = ClientMachine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table);
	if (!Client || !Server)
	{
		return false;
	}
	ClientMachine.Tick(LureSwimNet::SettleFrames);
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();

	// The other player as the client sees it: standing on the dock right where the climb would end.
	AActor* OtherPlayer = ClientMachine.AddBox(FVector(LureSwimNet::FaceX + 50.f, 0.f, 60.f + 100.f), FVector(30.f, 30.f, 100.f));
	ULureSwimTestListener* Listener = LureSwimNet::Listen(Client);
	FGCObjectScopeGuard KeepListener(Listener);

	const FSavedMovePtr JumpMove = LureSwimNet::ClientMove(Client, /*bPressJump*/ true);
	TestTrue(TEXT("client: no room on the dock, still swimming"), ClientMovement->IsSwimming());
	TestTrue(TEXT("client: the Jump flag still goes to the server"), (JumpMove->GetCompressedFlags() & FSavedMove_Character::FLAG_JumpPressed) != 0);
	TArray<FSavedMovePtr> Later;
	for (int32 Index = 0; Index < LureSwimNet::ClimbMoves; ++Index)
	{
		Later.Add(LureSwimNet::ClientMove(Client));
	}
	TestTrue(TEXT("client: still swimming at the dock"), ClientMovement->IsSwimming());
	OtherPlayer->Destroy(); // by the time the correction arrives, the client sees the other player elsewhere too

	// The server climbs from the Jump flag, and corrects the client.
	LureSwimNet::ServerMove(Server, *JumpMove);
	if (!TestTrue(TEXT("server: climbs out"), ServerMovement->IsClimbingOut()) || !LureSwimNet::SendReply(*this, Server, Client, JumpMove->TimeStamp))
	{
		return false;
	}
	TestTrue(TEXT("corrected: climbing out"), ClientMovement->IsClimbingOut());
	TestTrue(TEXT("corrected: with the server's plan"), LureSwimNet::FAccess::HasClimbPlan(*ClientMovement)
		&& LureSwimNet::FAccess::ClimbPlan(*ClientMovement).Target == LureSwimNet::FAccess::ClimbPlan(*ServerMovement).Target);
	TestTrue(TEXT("corrected: at the server's place"), LureSwimNet::Distance(Client, Server) < LureSwimNet::SamePlace);

	// The replay continues the server's climb; the server runs the same moves.
	LureSwimNet::FAccess::ReplayUnacknowledgedMoves(*ClientMovement);
	for (const FSavedMovePtr& Move : Later)
	{
		LureSwimNet::ServerMove(Server, *Move);
	}
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("client after the replay"));
	LureSwimNet::TestStandingOnDock(*this, Server, 60.f, TEXT("server"));
	TestTrue(FString::Printf(TEXT("the replay ends where the server is (%.4f cm)"), LureSwimNet::Distance(Client, Server)), LureSwimNet::Distance(Client, Server) < LureSwimNet::SamePlace);
	TestEqual(TEXT("one swim event: out of the water once the climb settled"), LureSwimNet::EventsText(Listener), FString(TEXT("out")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetFinishedClimbTest, "Project.Movement.Swim.Net.CorrectionIntoAFinishedClimbKeepsGoing", LureSwimNet::Flags)

bool FLureSwimNetFinishedClimbTest::RunTest(const FString& Parameters)
{
	// Review N2 case B. The client already stands on the dock when a correction for a move in the middle of the climb
	// arrives (the server's copy is still climbing). The replay must continue from the server's state with its plan
	// (before the fix it froze there for about one round trip) and fire no swim events: the client never really went
	// back into the water (before the fix it fired in, then out, which would cancel a cast made right after climbing out).
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ClientMachine.Create(*this, 60.f) || !ServerMachine.Create(*this, 60.f))
	{
		return false;
	}
	ALurePlayerCharacter* Client = ClientMachine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table);
	if (!Client || !Server)
	{
		return false;
	}
	ClientMachine.Tick(LureSwimNet::SettleFrames);
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();

	TArray<FSavedMovePtr> Moves;
	Moves.Add(LureSwimNet::ClientMove(Client, /*bPressJump*/ true));
	for (int32 Index = 0; Index < LureSwimNet::ClimbMoves; ++Index)
	{
		Moves.Add(LureSwimNet::ClientMove(Client));
	}
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("predicted"));
	ULureSwimTestListener* Listener = LureSwimNet::Listen(Client);
	FGCObjectScopeGuard KeepListener(Listener);

	// The server is 15 moves in: still climbing.
	constexpr int32 CorrectedMoves = 15;
	for (int32 Index = 0; Index < CorrectedMoves; ++Index)
	{
		LureSwimNet::ServerMove(Server, *Moves[Index]);
	}
	if (!TestTrue(TEXT("server: mid-climb"), ServerMovement->IsClimbingOut())
		|| !LureSwimNet::SendReply(*this, Server, Client, Moves[CorrectedMoves - 1]->TimeStamp))
	{
		return false;
	}
	TestTrue(TEXT("corrected: back in the server's climb"), ClientMovement->IsClimbingOut());
	TestTrue(TEXT("corrected: with the server's plan"), LureSwimNet::FAccess::HasClimbPlan(*ClientMovement));
	TestEqual(TEXT("corrected: no swim event while the correction is being applied"), LureSwimNet::EventsText(Listener), FString());

	LureSwimNet::FAccess::ReplayUnacknowledgedMoves(*ClientMovement);
	for (int32 Index = CorrectedMoves; Index < Moves.Num(); ++Index)
	{
		LureSwimNet::ServerMove(Server, *Moves[Index]);
	}
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("client after the replay"));
	TestTrue(FString::Printf(TEXT("the replay ends where the server is (%.4f cm)"), LureSwimNet::Distance(Client, Server)), LureSwimNet::Distance(Client, Server) < LureSwimNet::SamePlace);
	TestEqual(TEXT("no swim events: it stayed out of the water"), LureSwimNet::EventsText(Listener), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetTakeoffTest, "Project.Movement.Swim.Net.CorrectionRestoresTheTakeoffHeight", LureSwimNet::Flags)

bool FLureSwimNetTakeoffTest::RunTest(const FString& Parameters)
{
	// Review N3. The takeoff feet height is the climb rule's zero (landings and the jump climb are measured from it).
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ClientMachine.Create(*this, 60.f) || !ServerMachine.Create(*this, 60.f))
	{
		return false;
	}
	// Both copies stand on the dock top.
	const float HalfHeight = LureSwimNet::RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight;
	const FVector OnDock(LureSwimNet::FaceX + 250.f, 0.f, 60.f + HalfHeight + 2.f);
	ALurePlayerCharacter* Client = ClientMachine.Spawn(*this, OnDock, Table);
	ALurePlayerCharacter* Server = ServerMachine.Spawn(*this, OnDock, Table);
	if (!Client || !Server)
	{
		return false;
	}
	ClientMachine.Tick(30);
	ServerMachine.Tick(30);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();
	TestTrue(TEXT("both copies stand on the dock"), ClientMovement->IsMovingOnGround() && ServerMovement->IsMovingOnGround());

	// 1. A walking client corrected into the server's jump, mid-air: the mode change must not make the corrected
	//    mid-air feet height the takeoff.
	const FSavedMovePtr Standing = LureSwimNet::ClientMove(Client);
	LureSwimNet::FAccess::MoveAutonomous(*ServerMovement, Standing->TimeStamp, LureSwimNet::Dt, FSavedMove_Character::FLAG_JumpPressed, FVector::ZeroVector);
	for (int32 Index = 1; Index <= 8; ++Index)
	{
		LureSwimNet::FAccess::MoveAutonomous(*ServerMovement, Standing->TimeStamp + Index * LureSwimNet::Dt, LureSwimNet::Dt, 0, FVector::ZeroVector);
	}
	const float ServerTakeoff = ServerMovement->GetTakeoffFeetHeight();
	TestTrue(TEXT("server: in the air after its jump"), ServerMovement->IsFalling() && LureSwimNet::FeetZ(Server) > ServerTakeoff + 10.f);
	if (!LureSwimNet::SendReply(*this, Server, Client, Standing->TimeStamp))
	{
		return false;
	}
	TestTrue(TEXT("walking -> falling correction: falling"), ClientMovement->IsFalling());
	TestTrue(TEXT("walking -> falling correction: at the server's place"), LureSwimNet::Distance(Client, Server) < LureSwimNet::SamePlace);
	TestEqual(TEXT("walking -> falling correction: the server's takeoff, not the corrected mid-air feet"), ClientMovement->GetTakeoffFeetHeight(), ServerTakeoff);

	// 2. Falling -> falling: the mode doesn't change, so only the correction can bring the server's value (here the
	//    client had taken off again from higher up, the server hadn't).
	LureSwimNet::FAccess::SetTakeoffFeetHeight(*ClientMovement, ServerTakeoff + 50.f);
	const FSavedMovePtr Falling = LureSwimNet::ClientMove(Client);
	LureSwimNet::FAccess::MoveAutonomous(*ServerMovement, Falling->TimeStamp, LureSwimNet::Dt, 0, FVector::ZeroVector);
	if (!LureSwimNet::SendReply(*this, Server, Client, Falling->TimeStamp))
	{
		return false;
	}
	TestTrue(TEXT("falling -> falling correction: still falling"), ClientMovement->IsFalling());
	TestEqual(TEXT("falling -> falling correction: the server's takeoff"), ClientMovement->GetTakeoffFeetHeight(), ServerTakeoff);

	// 3. A correction the engine ignores (no saved move with that time stamp) changes nothing.
	LureSwimNet::FAccess::SetTakeoffFeetHeight(*ClientMovement, 123.f);
	const FVector Before = Client->GetActorLocation();
	if (!LureSwimNet::SendReply(*this, Server, Client, 99999.f))
	{
		return false;
	}
	TestEqual(TEXT("ignored correction: takeoff unchanged"), ClientMovement->GetTakeoffFeetHeight(), 123.f);
	TestTrue(TEXT("ignored correction: not moved"), Client->GetActorLocation() == Before);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// N1: other players' copies during a climb
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetProxyClimbTest, "Project.Movement.Swim.Net.OtherPlayersCopyFollowsTheClimb", LureSwimNet::Flags)

bool FLureSwimNetProxyClimbTest::RunTest(const FString& Parameters)
{
	// Player B climbs; on player A's machine B is a simulated proxy with no plan. SimulateMovement runs PhysCustom for it
	// through MoveSmooth. Before the fix that dropped the copy to Falling every tick and ACharacter::PostNetReceive put the
	// climb back on every net update: an in/out OnSwimStateChanged pair per update and a sagging, snapping capsule.
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine Machine;
	if (!Table || !Machine.Create(*this, 60.f))
	{
		return false;
	}
	ALurePlayerCharacter* Climber = Machine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* Copy = Machine.SpawnSwimmer(*this, Table, -1500.f);
	const float HalfHeight = LureSwimNet::RowOf(Table, ELureMovementState::Stand).CapsuleHalfHeight;
	ALurePlayerCharacter* LandCopy = Machine.Spawn(*this, FVector(LureSwimNet::FaceX + 250.f, 200.f, 60.f + HalfHeight + 2.f), Table);
	if (!Climber || !Copy || !LandCopy)
	{
		return false;
	}
	Machine.Tick(LureSwimNet::SettleFrames);
	ULureCharacterMovementComponent* ClimberMovement = Climber->GetLureMovement();
	ULureCharacterMovementComponent* CopyMovement = Copy->GetLureMovement();

	// What the server replicates for a climber: its packed mode and velocity.
	LureSwimNet::FAccess::MoveAutonomous(*ClimberMovement, 1.f, LureSwimNet::Dt, FSavedMove_Character::FLAG_JumpPressed, FVector::ZeroVector);
	LureSwimNet::FAccess::MoveAutonomous(*ClimberMovement, 1.f + LureSwimNet::Dt, LureSwimNet::Dt, 0, FVector::ZeroVector);
	if (!TestTrue(TEXT("the climber is climbing out"), ClimberMovement->IsClimbingOut()))
	{
		return false;
	}
	const uint8 ClimbPacked = ClimberMovement->PackNetworkMovementMode();
	const FVector ClimbVelocity = ClimberMovement->Velocity;
	TestTrue(FString::Printf(TEXT("the climb rises (%.0f cm/s)"), ClimbVelocity.Z), ClimbVelocity.Z > 100.f);

	// The copy on another machine: swimming, then the replicated climb.
	TestTrue(TEXT("copy: swimming"), Copy->IsSwimming());
	ULureSwimTestListener* Listener = LureSwimNet::Listen(Copy);
	FGCObjectScopeGuard KeepListener(Listener);
	Copy->SetRole(ROLE_SimulatedProxy);
	CopyMovement->ApplyNetworkMovementMode(ClimbPacked);
	CopyMovement->Velocity = ClimbVelocity;
	const float StartZ = static_cast<float>(Copy->GetActorLocation().Z);
	constexpr int32 Frames = 30;
	int32 ModeReapplied = 0;
	bool bAlwaysClimbing = true;
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		// A net update every third frame: ACharacter::PostNetReceive re-applies the mode only if the copy's differs.
		if (Frame % 3 == 0 && CopyMovement->PackNetworkMovementMode() != ClimbPacked)
		{
			++ModeReapplied;
			CopyMovement->ApplyNetworkMovementMode(ClimbPacked);
		}
		CopyMovement->MoveSmooth(CopyMovement->Velocity, LureSwimNet::Dt); // what SimulateMovement runs for MOVE_Custom
		bAlwaysClimbing &= CopyMovement->IsClimbingOut() && Copy->IsSwimming();
	}
	TestTrue(TEXT("copy: climbing out (and in the water) the whole time"), bAlwaysClimbing);
	TestEqual(TEXT("copy: its mode never left the replicated one (nothing to re-apply)"), ModeReapplied, 0);
	TestFalse(TEXT("copy: never plans a climb of its own"), LureSwimNet::FAccess::HasClimbPlan(*CopyMovement));
	const float Rise = static_cast<float>(Copy->GetActorLocation().Z) - StartZ;
	TestNearlyEqual(TEXT("copy: follows the replicated velocity (no gravity sag)"), Rise, static_cast<float>(ClimbVelocity.Z) * Frames * LureSwimNet::Dt, 0.5f);
	TestEqual(TEXT("copy: no swim events during the climb"), LureSwimNet::EventsText(Listener), FString());

	// The server's replicated Walking ends the climb.
	CopyMovement->ApplyNetworkMovementMode(LureSwimNet::PackedMode(CopyMovement, MOVE_Walking));
	TestFalse(TEXT("copy: out of the water when the server says walking"), Copy->IsSwimming());
	TestEqual(TEXT("copy: one event, out"), LureSwimNet::EventsText(Listener), FString(TEXT("out")));
	Copy->SetRole(ROLE_Authority);

	// A land ledge climb behaves the same (no flips to falling or walking, no landing).
	ULureCharacterMovementComponent* LandMovement = LandCopy->GetLureMovement();
	TestTrue(TEXT("land copy: standing on the dock"), LandMovement->IsMovingOnGround());
	LandCopy->SetRole(ROLE_SimulatedProxy);
	const uint8 LedgePacked = LureSwimNet::PackedMode(LandMovement, MOVE_Custom, ELureCustomMovementMode::LedgeClimb);
	LandMovement->ApplyNetworkMovementMode(LedgePacked);
	LandMovement->Velocity = FVector(0.f, 0.f, 400.f);
	const float LandStartZ = static_cast<float>(LandCopy->GetActorLocation().Z);
	bool bAlwaysLedgeClimbing = true;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		LandMovement->MoveSmooth(LandMovement->Velocity, LureSwimNet::Dt);
		bAlwaysLedgeClimbing &= LandMovement->IsLedgeClimbing() && LandMovement->PackNetworkMovementMode() == LedgePacked;
	}
	TestTrue(TEXT("land copy: ledge-climbing the whole time"), bAlwaysLedgeClimbing);
	TestNearlyEqual(TEXT("land copy: follows the replicated velocity"), static_cast<float>(LandCopy->GetActorLocation().Z) - LandStartZ, 400.f * 10.f * LureSwimNet::Dt, 0.5f);
	TestFalse(TEXT("land copy: never in the water"), LandCopy->IsSwimming());
	LandCopy->SetRole(ROLE_Authority);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// T-026 QA B1: a teleport during a climb ends it on the server, and the owning client follows without a ping-pong
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetTeleportMidClimbTest, "Project.Movement.Swim.Net.TeleportMidClimbEndsItOnServerAndOwner", LureSwimNet::Flags)

bool FLureSwimNetTeleportMidClimbTest::RunTest(const FString& Parameters)
{
	// The server teleports a player who is climbing out (a respawn, Lure.Teleport). The server's copy must drop the climb
	// at once; the owning client, still climbing in its prediction, is corrected to the server's mode with no plan, stays
	// where the server put it, and needs no further corrections (client and server agree move after move). One "out".
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ClientMachine.Create(*this, 60.f) || !ServerMachine.Create(*this, 60.f))
	{
		return false;
	}
	// Dry land far from the dock (top 30 cm above the water), on both machines.
	constexpr float LandTop = 30.f;
	const FVector LandCenter(2000.f, 1500.f, 0.f);
	for (LureSwimNet::FMachine* Machine : { &ClientMachine, &ServerMachine })
	{
		Machine->AddBox(FVector(LandCenter.X, LandCenter.Y, 0.5f * (LandTop + LureSwimNet::SeabedZ)), FVector(400.f, 400.f, 0.5f * (LandTop - LureSwimNet::SeabedZ)));
	}
	ALurePlayerCharacter* Client = ClientMachine.SpawnSwimmer(*this, Table);
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table);
	if (!Client || !Server)
	{
		return false;
	}
	ClientMachine.Tick(LureSwimNet::SettleFrames);
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();
	ULureSwimTestListener* Listener = LureSwimNet::Listen(Client);
	FGCObjectScopeGuard KeepListener(Listener);

	// Both climb out from the same Jump move and are 8 moves into the climb.
	TArray<FSavedMovePtr> Moves;
	Moves.Add(LureSwimNet::ClientMove(Client, /*bPressJump*/ true));
	LureSwimNet::ServerMove(Server, *Moves.Last());
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Moves.Add(LureSwimNet::ClientMove(Client));
		LureSwimNet::ServerMove(Server, *Moves.Last());
	}
	if (!TestTrue(TEXT("setup: both copies mid-climb"), ClientMovement->IsClimbingOut() && ServerMovement->IsClimbingOut()))
	{
		return false;
	}

	// The server teleports its copy onto the land.
	const float HalfHeight = Server->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Destination(LandCenter.X, LandCenter.Y, LandTop + HalfHeight + 5.f);
	TestTrue(TEXT("server: teleport accepted"), Server->TeleportTo(Destination, FRotator::ZeroRotator));
	TestFalse(TEXT("server: the climb ended at once"), ServerMovement->IsClimbing());
	TestFalse(TEXT("server: the plan is gone"), LureSwimNet::FAccess::HasClimbPlan(*ServerMovement));
	TestFalse(TEXT("server: out of the water"), Server->IsSwimming());

	// The client didn't know yet: 10 more predicted climb moves. The server runs them where it is now (it drops 5 cm onto
	// the land), then corrects.
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Moves.Add(LureSwimNet::ClientMove(Client));
		LureSwimNet::ServerMove(Server, *Moves.Last());
	}
	TestTrue(TEXT("client: still climbing in its prediction"), ClientMovement->IsClimbingOut());
	TestTrue(TEXT("server: on the land"), ServerMovement->IsMovingOnGround());
	if (!LureSwimNet::SendReply(*this, Server, Client, Moves.Last()->TimeStamp))
	{
		return false;
	}
	LureSwimNet::FAccess::ReplayUnacknowledgedMoves(*ClientMovement);
	TestFalse(TEXT("client corrected: not climbing"), ClientMovement->IsClimbing());
	TestFalse(TEXT("client corrected: no plan"), LureSwimNet::FAccess::HasClimbPlan(*ClientMovement));
	TestEqual(TEXT("client corrected: the server's mode"), static_cast<int32>(ClientMovement->PackNetworkMovementMode()), static_cast<int32>(ServerMovement->PackNetworkMovementMode()));
	TestTrue(FString::Printf(TEXT("client corrected: where the server is (%.4f cm)"), LureSwimNet::Distance(Client, Server)), LureSwimNet::Distance(Client, Server) < LureSwimNet::SamePlace);

	// No ping-pong: from here client and server agree on every move (no correction needed), and nobody drifts back.
	float WorstError = 0.f;
	for (int32 Index = 0; Index < 60; ++Index)
	{
		const FSavedMovePtr Move = LureSwimNet::ClientMove(Client);
		LureSwimNet::ServerMove(Server, *Move);
		WorstError = FMath::Max(WorstError, LureSwimNet::Distance(Client, Server));
	}
	TestTrue(FString::Printf(TEXT("client and server agree after the correction (worst %.4f cm)"), WorstError), WorstError < LureSwimNet::SamePlace);
	const float Drift = static_cast<float>(FVector::Dist2D(Client->GetActorLocation(), Destination));
	TestTrue(FString::Printf(TEXT("client stays where it was sent (moved %.2f cm)"), Drift), Drift < 1.f);
	TestTrue(TEXT("client: walking on the land"), ClientMovement->IsMovingOnGround());
	TestTrue(TEXT("server: walking on the land"), ServerMovement->IsMovingOnGround());
	TestEqual(TEXT("client: one swim event, out"), LureSwimNet::EventsText(Listener), FString(TEXT("out")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Playtest 2026-09-23 bug 2: Jump held before reaching a ladder (or a climbable edge) climbs on arrival
// ---------------------------------------------------------------------------------------------------------------------

namespace LureSwimNet
{
	/** Start of the approach: 160 cm of water between the capsule's front and the face, outside the ladder's 80 cm grab zone. */
	constexpr float ApproachX = FaceX - 34.f - 160.f;

	/** Enough moves to swim the approach at 170 cm/s (plus the acceleration) and climb a 150 cm dock. */
	constexpr int32 ApproachAndClimbMoves = 200;

	/** An owning-client move with input: the player swims forward (+X, toward the dock) at full acceleration. */
	FSavedMovePtr ClientMoveForward(ALurePlayerCharacter* Character)
	{
		ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
		FNetworkPredictionData_Client_Character& ClientData = ClientDataOf(Character);
		Character->CheckJumpInput(Dt);
		ClientData.CurrentTimeStamp += Dt;
		FSavedMovePtr Move = ClientData.CreateSavedMove();
		// Forward while in the water; the climb and the dock need no input (walking on would carry it off the far side).
		const FVector Input = Movement->IsSwimming() ? FVector(Movement->GetMaxAcceleration(), 0.f, 0.f) : FVector::ZeroVector;
		Move->SetMoveFor(Character, Dt, Input, ClientData);
		FAccess::SetInputAcceleration(*Movement, Move->Acceleration.GetClampedToMaxSize(Movement->GetMaxAcceleration()));
		FAccess::PerformMovement(*Movement, Move->DeltaTime);
		Move->PostUpdate(Character, FSavedMove_Character::PostUpdate_Record);
		ClientData.SavedMoves.Push(Move);
		return Move;
	}

	bool HasJumpFlag(const FSavedMove_Character& Move)
	{
		return (Move.GetCompressedFlags() & FSavedMove_Character::FLAG_JumpPressed) != 0;
	}

	ALureLadder* AddLadder(FMachine& Machine)
	{
		// On the dock face at the water line, +X pointing out over the water (yaw 180), default 300 cm climb height.
		const FTransform Transform(FRotator(0.f, 180.f, 0.f), FVector(FaceX, 0.f, 0.f));
		ALureLadder* Ladder = Machine.World->SpawnActorDeferred<ALureLadder>(ALureLadder::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Ladder)
		{
			Ladder->FinishSpawning(Transform);
		}
		return Ladder;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetHeldJumpLadderTest, "Project.Movement.Swim.Net.HeldJumpClimbsTheLadderOnArrival", LureSwimNet::Flags)

bool FLureSwimNetHeldJumpLadderTest::RunTest(const FString& Parameters)
{
	// The player presses Jump in open water and keeps holding it while swimming into a ladder's grab zone (150 cm dock).
	// Before the fix only a fresh press climbed. Now every move while swimming with Jump held carries the Jump flag, and
	// the climb is queued in the move that reaches the ladder: predicted on the client, planned again by the server
	// from the saved moves' flags, the same result on both.
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ClientMachine.Create(*this, 150.f) || !ServerMachine.Create(*this, 150.f))
	{
		return false;
	}
	ALureLadder* ClientLadder = LureSwimNet::AddLadder(ClientMachine);
	ALureLadder* ServerLadder = LureSwimNet::AddLadder(ServerMachine);
	if (!TestNotNull(TEXT("client ladder spawns"), ClientLadder) || !TestNotNull(TEXT("server ladder spawns"), ServerLadder))
	{
		return false;
	}
	ALurePlayerCharacter* Client = ClientMachine.SpawnSwimmer(*this, Table, LureSwimNet::ApproachX);
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table, LureSwimNet::ApproachX);
	if (!Client || !Server)
	{
		return false;
	}
	ClientMachine.Tick(LureSwimNet::SettleFrames);
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
	ULureCharacterMovementComponent* ServerMovement = Server->GetLureMovement();
	TestTrue(TEXT("both copies swim"), ClientMovement->IsSwimming() && ServerMovement->IsSwimming());
	TestFalse(TEXT("the approach starts outside the ladder's grab zone"), ClientLadder->IsInGrabZone(Client->GetActorLocation()));
	TestTrue(TEXT("both copies start at the same place"), LureSwimNet::Distance(Client, Server) < LureSwimNet::SamePlace);

	// Jump goes down in open water and stays held (the player's input: DoJumpStart, no DoJumpEnd).
	Client->DoJumpStart();
	TestTrue(TEXT("Jump is held"), Client->IsJumpHeld());
	int32 ArrivalMove = INDEX_NONE;
	bool bFlagOnEverySwimMove = true;
	bool bNoClimbBeforeTheZone = true;
	float WorstError = 0.f;
	bool bSameModes = true;
	for (int32 Index = 0; Index < LureSwimNet::ApproachAndClimbMoves; ++Index)
	{
		const bool bWasSwimming = ClientMovement->IsSwimming();
		const bool bWasInZone = ClientLadder->IsInGrabZone(Client->GetActorLocation());
		const FSavedMovePtr Move = LureSwimNet::ClientMoveForward(Client);
		if (bWasSwimming)
		{
			bFlagOnEverySwimMove &= LureSwimNet::HasJumpFlag(*Move);
		}
		if (ArrivalMove == INDEX_NONE && ClientMovement->IsClimbingOut())
		{
			ArrivalMove = Index;
			bNoClimbBeforeTheZone &= bWasInZone;
		}
		LureSwimNet::ServerMove(Server, *Move);
		WorstError = FMath::Max(WorstError, LureSwimNet::Distance(Client, Server));
		bSameModes &= ServerMovement->PackNetworkMovementMode() == Move->EndPackedMovementMode;
	}
	TestTrue(TEXT("the held Jump climbs the ladder on arrival (no fresh press)"), ArrivalMove != INDEX_NONE);
	TestTrue(FString::Printf(TEXT("after swimming there first (climb started on move %d)"), ArrivalMove), ArrivalMove > 10);
	TestTrue(TEXT("the climb starts only once the swimmer is in the grab zone"), bNoClimbBeforeTheZone);
	TestTrue(TEXT("every move in the water with Jump held carries FLAG_JumpPressed"), bFlagOnEverySwimMove);
	TestTrue(FString::Printf(TEXT("client and server agree on every move (worst %.4f cm)"), WorstError), WorstError < LureSwimNet::SamePlace);
	TestTrue(TEXT("client and server agree on the mode of every move"), bSameModes);
	LureSwimNet::TestStandingOnDock(*this, Client, 150.f, TEXT("client"));
	LureSwimNet::TestStandingOnDock(*this, Server, 150.f, TEXT("server"));

	// Still holding Jump on the dock: out of the water a held Jump does nothing new (no jump, no ledge climb).
	TestTrue(TEXT("Jump is still held on the dock"), Client->IsJumpHeld());
	bool bNoJumpFlagOnLand = true;
	for (int32 Index = 0; Index < 30; ++Index)
	{
		bNoJumpFlagOnLand &= !LureSwimNet::HasJumpFlag(*LureSwimNet::ClientMove(Client));
	}
	TestTrue(TEXT("on land a held Jump sends no new Jump flags"), bNoJumpFlagOnLand);
	TestTrue(TEXT("on land a held Jump doesn't jump again"), ClientMovement->IsMovingOnGround());
	Client->DoJumpEnd();
	TestFalse(TEXT("released"), Client->IsJumpHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimNetHeldJumpEdgeReplayTest, "Project.Movement.Swim.Net.HeldJumpClimbsTheEdgeOnArrivalAndReplays", LureSwimNet::Flags)

bool FLureSwimNetHeldJumpEdgeReplayTest::RunTest(const FString& Parameters)
{
	// The same for a 60 cm edge (no ladder; the same queue in DoJump): Jump held while swimming there climbs on arrival.
	// A correction for a move before the arrival makes the client replay the approach from its saved moves: the replay
	// re-plans the climb from the saved Jump flags and ends where the prediction did. Held Jump in open water, or a Jump
	// released before arrival, never climbs.
	UDataTable* Table = LureSwimNet::ShippedTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	LureSwimNet::FMachine ClientMachine;
	LureSwimNet::FMachine ServerMachine;
	if (!Table || !ClientMachine.Create(*this, 60.f) || !ServerMachine.Create(*this, 60.f))
	{
		return false;
	}
	ALurePlayerCharacter* Client = ClientMachine.SpawnSwimmer(*this, Table, LureSwimNet::ApproachX);
	ALurePlayerCharacter* Server = ServerMachine.SpawnSwimmer(*this, Table, LureSwimNet::ApproachX);
	ALurePlayerCharacter* Released = ServerMachine.SpawnSwimmer(*this, Table, LureSwimNet::ApproachX, 300.f);
	ALurePlayerCharacter* OpenWater = ServerMachine.SpawnSwimmer(*this, Table, -1500.f, -300.f);
	if (!Client || !Server || !Released || !OpenWater)
	{
		return false;
	}
	ClientMachine.Tick(LureSwimNet::SettleFrames);
	ServerMachine.Tick(LureSwimNet::SettleFrames);
	Client->SetRole(ROLE_AutonomousProxy);
	ULureCharacterMovementComponent* ClientMovement = Client->GetLureMovement();
	TestTrue(TEXT("the client swims"), ClientMovement->IsSwimming());

	// 1. Predicted: hold Jump from open water, swim to the edge, climb on arrival.
	Client->DoJumpStart();
	// What each move's RPC carries, copied: acknowledged saved moves go back to the engine's pool and get reused.
	struct FSentMove
	{
		float TimeStamp = 0.f;
		float DeltaTime = 0.f;
		uint8 Flags = 0;
		FVector Acceleration = FVector::ZeroVector;
	};
	TArray<FSentMove> Moves;
	int32 ArrivalMove = INDEX_NONE;
	for (int32 Index = 0; Index < LureSwimNet::ApproachAndClimbMoves && !ClientMovement->IsMovingOnGround(); ++Index)
	{
		const FSavedMovePtr Move = LureSwimNet::ClientMoveForward(Client);
		Moves.Add({ Move->TimeStamp, Move->DeltaTime, Move->GetCompressedFlags(), Move->Acceleration });
		// The server acks the oldest moves as good, as it would in play: the engine flushes the saved moves at 96.
		FNetworkPredictionData_Client_Character& ClientData = LureSwimNet::ClientDataOf(Client);
		if (ClientData.SavedMoves.Num() > 80)
		{
			ClientData.AckMove(0, *ClientMovement);
		}
		if (ArrivalMove == INDEX_NONE && ClientMovement->IsClimbingOut())
		{
			ArrivalMove = Index;
		}
	}
	TestTrue(FString::Printf(TEXT("the held Jump climbs the 60 cm edge on arrival (move %d)"), ArrivalMove), ArrivalMove != INDEX_NONE && ArrivalMove > 10);
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("predicted"));
	if (ArrivalMove == INDEX_NONE || ArrivalMove < 5)
	{
		return false;
	}
	const FVector Predicted = Client->GetActorLocation();

	// 2. The server runs the moves up to a few before the arrival and corrects the client there: the replay of the rest
	//    (saved moves, bClientUpdating) must climb from the saved Jump flags alone.
	const int32 CorrectedMove = ArrivalMove - 5;
	for (int32 Index = 0; Index <= CorrectedMove; ++Index)
	{
		const FSentMove& Sent = Moves[Index];
		LureSwimNet::FAccess::MoveAutonomous(*Server->GetLureMovement(), Sent.TimeStamp, Sent.DeltaTime, Sent.Flags, Sent.Acceleration);
	}
	TestTrue(TEXT("server: still swimming before the arrival"), Server->GetLureMovement()->IsSwimming());
	Client->DoJumpEnd(); // the player has let go by the time the correction arrives: the replay must not depend on it
	if (!LureSwimNet::SendReply(*this, Server, Client, Moves[CorrectedMove].TimeStamp))
	{
		return false;
	}
	TestTrue(TEXT("the correction puts the client back in the water"), ClientMovement->IsSwimming());
	LureSwimNet::FAccess::ReplayUnacknowledgedMoves(*ClientMovement);
	LureSwimNet::TestStandingOnDock(*this, Client, 60.f, TEXT("replayed"));
	const float Off = static_cast<float>((Client->GetActorLocation() - Predicted).Size());
	TestTrue(FString::Printf(TEXT("the replay ends where the prediction did (%.4f cm)"), Off), Off < LureSwimNet::SamePlace);

	// 3. Released before arrival: swims into the edge and stays in the water (a climb needs Jump held or a fresh press).
	Released->DoJumpStart();
	LureSwimNet::ClientMoveForward(Released);
	Released->DoJumpEnd();
	for (int32 Index = 0; Index < 120; ++Index)
	{
		LureSwimNet::ClientMoveForward(Released);
	}
	TestTrue(TEXT("Jump released before the edge: no climb"), Released->GetLureMovement()->IsSwimming());
	TestTrue(TEXT("and it did reach the edge"), Released->GetActorLocation().X > LureSwimNet::FaceX - 34.f - 10.f);

	// 4. Held in open water: no edge, nothing happens (no jump out of the water).
	OpenWater->DoJumpStart();
	for (int32 Index = 0; Index < 60; ++Index)
	{
		LureSwimNet::ClientMove(OpenWater);
	}
	TestTrue(TEXT("Jump held in open water: still swimming"), OpenWater->GetLureMovement()->IsSwimming());
	OpenWater->DoJumpEnd();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

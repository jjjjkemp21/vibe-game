// Lure T-025 / fishing-loop playtest B4: Lure.Teleport Player=<id> must turn a remote client's own view (its control rotation
// on its machine), not only the server's copy. Real in-process server + client (UE::Net::FTestWorlds); movement from
// data/tables/DT_Movement.csv. Project.Dev.Teleport.Net.*

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING && WITH_EDITOR

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Dev/LureDevCommands.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "Tests/NetTestHelpers.h"
#include "UObject/Script.h"
#include "UObject/StrongObjectPtr.h"

namespace LureDevTeleportNet
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float NetDt = 1.f / 60.f;

	AActor* AddFloor(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(FVector(3000.f, 3000.f, 50.f), false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetRelativeLocation_Direct(FVector(0.f, 0.f, -50.f));
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		return Actor;
	}

	UDataTable* LoadMovement(FAutomationTestBase& Test)
	{
		FString Csv;
		if (!Test.TestTrue(TEXT("data/tables/DT_Movement.csv loads"),
			FFileHelper::LoadFileToString(Csv, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv")))))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureMovementRow::StaticStruct();
		for (const FString& Problem : Table->CreateTableFromCSVString(Csv))
		{
			Test.AddError(TEXT("DT_Movement.csv import problem: ") + Problem);
		}
		return Table;
	}

	float YawOf(const AController* Controller)
	{
		return Controller ? FRotator::NormalizeAxis(static_cast<float>(Controller->GetControlRotation().Yaw)) : 9999.f;
	}

	bool YawNear(float A, float B, float Tolerance = 0.5f)
	{
		return FMath::Abs(FRotator::NormalizeAxis(A - B)) <= Tolerance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevTeleportNetTurnsClientViewTest, "Project.Dev.Teleport.Net.TurnsRemoteClientView", LureDevTeleportNet::Flags)

bool FLureDevTeleportNetTurnsClientViewTest::RunTest(const FString& Parameters)
{
	using LureDevTeleportNet::NetDt;
	using LureDevTeleportNet::YawNear;
	using LureDevTeleportNet::YawOf;

	const TStrongObjectPtr<UDataTable> Movement(LureDevTeleportNet::LoadMovement(*this));
	if (!Movement.IsValid())
	{
		return false;
	}
	AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	// Harness artifact: the floor is spawned per world (not replicated), so the movement base can't be sent.
	AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* ServerWorld = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("the server world is up with a net driver"), Worlds.Server.IsLoaded() && ServerWorld && ServerWorld->GetNetDriver())
		|| !TestTrue(TEXT("a client connects"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	LureDevTeleportNet::AddFloor(ServerWorld);
	LureDevTeleportNet::AddFloor(Worlds.Clients[0].GetWorld());

	APlayerController* ServerPC = Worlds.GetServerPlayerControllerOfClient(0);
	if (!TestNotNull(TEXT("the server has the client's PlayerController"), ServerPC))
	{
		return false;
	}
	TArray<FLureMovementRow> Rows;
	TArray<FString> Problems;
	FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
	const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.f;
	const FTransform Transform(FRotator(0.f, 23.f, 0.f), FVector(0.f, 0.f, HalfHeight + 2.15f));
	ALurePlayerCharacter* ServerPawn = ServerWorld->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("the player spawns"), ServerPawn))
	{
		return false;
	}
	ServerPawn->GetLureMovement()->ApplyMovementTable(Movement.Get());
	ServerPawn->FinishSpawning(Transform);
	ServerPC->Possess(ServerPawn);

	APlayerController* ClientPC = nullptr;
	const bool bReady = Worlds.TickAllUntil([&]()
	{
		ClientPC = Worlds.Clients[0].GetWorld() ? Worlds.Clients[0].GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Own = ClientPC ? ClientPC->GetPawn() : nullptr;
		return Own && Own->IsLocallyControlled() && ClientPC->PlayerState != nullptr;
	}, NetDt, 600);
	if (!TestTrue(TEXT("the client controls its own replicated player"), bReady))
	{
		return false;
	}
	ClientPC->SetControlRotation(FRotator(0.f, 23.f, 0.f)); // the playtest's starting view
	Worlds.TickAll(30);                                     // moves (with that view) flow to the server
	TestTrue(FString::Printf(TEXT("the client looks along yaw 23 before the teleport (%.1f)"), YawOf(ClientPC)), YawNear(YawOf(ClientPC), 23.f));

	// The console path exactly as the playtest driver runs it: editor Python calls unreal.SystemLibrary.execute_console_command,
	// which runs inside an FEditorScriptExecutionGuard (GAllowActorScriptExecutionInEditor = true). That guard made every RPC run
	// locally, so ClientSetRotation never left the server (the B4 cause). Server world, Player=<the client's PlayerId>.
	const int32 PlayerId = ServerPC->PlayerState ? ServerPC->PlayerState->GetPlayerId() : INDEX_NONE;
	FOutputDeviceNull Null;
	{
		FEditorScriptExecutionGuard LikeEditorPython;
		TestTrue(TEXT("Lure.Teleport 500 200 0 180 Player=<client> runs on the server (inside the editor-Python guard)"),
			IConsoleManager::Get().ProcessUserConsoleInput(*FString::Printf(TEXT("Lure.Teleport 500 200 0 180 Player=%d"), PlayerId), Null, ServerWorld));
		TestTrue(TEXT("... and leaves the editor-Python guard as it was"), GAllowActorScriptExecutionInEditor);
	}
	TestTrue(TEXT("the server's copy of the controller turns at once"), YawNear(YawOf(ServerPC), 180.f));

	const bool bTurned = Worlds.TickAllUntil([&]() { return YawNear(YawOf(ClientPC), 180.f); }, NetDt, 120);
	TestTrue(FString::Printf(TEXT("B4: the client's own control rotation turns to 180 (got %.1f)"), YawOf(ClientPC)), bTurned);
	Worlds.TickAll(120); // two seconds of normal play: the client keeps sending moves with its view
	TestTrue(FString::Printf(TEXT("... and stays there (client %.1f)"), YawOf(ClientPC)), YawNear(YawOf(ClientPC), 180.f));
	TestTrue(FString::Printf(TEXT("... and the server agrees (server %.1f)"), YawOf(ServerPC)), YawNear(YawOf(ServerPC), 180.f));
	if (const APawn* ClientPawn = ClientPC->GetPawn())
	{
		TestTrue(FString::Printf(TEXT("... and the client's body faces it (%.1f)"), ClientPawn->GetActorRotation().Yaw), YawNear(static_cast<float>(ClientPawn->GetActorRotation().Yaw), 180.f, 1.f));
		TestTrue(FString::Printf(TEXT("the client's player arrived (%s)"), *ClientPawn->GetActorLocation().ToCompactString()),
			FVector2D(ClientPawn->GetActorLocation()).Equals(FVector2D(500.f, 200.f), 5.f));
	}

	// TeleportPawn itself (any other caller, e.g. a Python-called helper) also sends the RPC under the guard.
	{
		FEditorScriptExecutionGuard LikeEditorPython;
		FString Error;
		TestTrue(TEXT("TeleportPawn to yaw -90 (inside the editor-Python guard)"), FLureDevCommands::TeleportPawn(ServerPawn, FVector(-400.f, -300.f, 0.f), FRotator(0.f, -90.f, 0.f), Error));
	}
	TestTrue(FString::Printf(TEXT("the client's control rotation turns to -90 (got %.1f)"), YawOf(ClientPC)),
		Worlds.TickAllUntil([&]() { return YawNear(YawOf(ClientPC), -90.f); }, NetDt, 120));
	return true;
}

#endif

// Lure T-025: tests for the dev console commands (Project.Dev.*) and the Python playtest driver.
// Worlds are transient test worlds; fish tables come from data/tables/*.json, movement from data/tables/DT_Movement.csv.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dev/LureDevCommands.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Fish/FishInstance.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationCommon.h"
#include "Tests/Progression/ProgressionTestUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace LureDevTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float DevTickDt = 1.f / 60.f;
	/** Feet may hover up to the floor clearance plus the movement component's floor distance above the floor. */
	constexpr float FeetTolerance = 5.f;

	FString TablePath(const TCHAR* FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
	}

	/** A transient game world with a 60 x 60 m floor (top at z = 0), markers and possessed Lure characters. */
	struct FDevWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		TStrongObjectPtr<UDataTable> MovementTable;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			FString Csv;
			if (!World || !FFileHelper::LoadFileToString(Csv, *TablePath(TEXT("DT_Movement.csv"))))
			{
				Test.AddError(TEXT("no test world, or data/tables/DT_Movement.csv is missing"));
				return false;
			}
			MovementTable.Reset(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
			MovementTable->RowStruct = FLureMovementRow::StaticStruct();
			for (const FString& Problem : MovementTable->CreateTableFromCSVString(Csv))
			{
				Test.AddError(TEXT("DT_Movement.csv import problem: ") + Problem);
			}
			AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f));
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

		/** A marker like build_level.py makes (TargetPoint-style: an actor at the point with tags). */
		AActor* AddMarker(const FVector& Location, float Yaw, const TArray<FString>& Tags)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), Location), Params);
			USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));
			for (const FString& Tag : Tags)
			{
				Actor->Tags.Add(FName(*Tag));
			}
			return Actor;
		}

		/** A Lure character standing at Feet, possessed by a local PlayerController with a PlayerState of this PlayerId. */
		ALurePlayerCharacter* SpawnPlayer(FAutomationTestBase& Test, const FVector& Feet, int32 PlayerId, APlayerController** OutController = nullptr)
		{
			const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, 100.f));
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character || !Character->GetLureMovement())
			{
				Test.AddError(TEXT("ALurePlayerCharacter did not spawn"));
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(MovementTable.Get());
			Character->FinishSpawning(Transform);
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			if (!Controller)
			{
				Test.AddError(TEXT("the PlayerController did not spawn"));
				return nullptr;
			}
			if (!Controller->PlayerState)
			{
				FActorSpawnParameters Params;
				Params.Owner = Controller;
				Controller->SetPlayerState(World->SpawnActor<APlayerState>(APlayerState::StaticClass(), Params));
			}
			Controller->PlayerState->SetPlayerId(PlayerId);
			// UE 5.8: without a net driver, a PlayerController counts as local only with a LocalPlayer or this flag.
			Controller->SetAsLocalPlayerController();
			Controller->Possess(Character);
			Tick(10);
			if (OutController)
			{
				*OutController = Controller;
			}
			return Character;
		}

		void Tick(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(DevTickDt);
			}
		}
	};

	float FeetZ(const ALurePlayerCharacter* Character)
	{
		return Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** Owns the four fish tables built from data/tables/*.json, plus the project's roll tuning. */
	struct FFishFixture
	{
		TStrongObjectPtr<UDataTable> Species, Rarities, Modifiers, Stats;

		bool Load(FAutomationTestBase& Test)
		{
			return Import(Test, Species, FFishSpeciesRow::StaticStruct(), TEXT("DT_FishSpecies.json"))
				&& Import(Test, Rarities, FFishRarityRow::StaticStruct(), TEXT("DT_FishRarity.json"))
				&& Import(Test, Modifiers, FFishModifierRow::StaticStruct(), TEXT("DT_FishModifier.json"))
				&& Import(Test, Stats, FFishStatRow::StaticStruct(), TEXT("DT_FishStat.json"));
		}

		FFishTables Get() const
		{
			FFishTables Tables;
			Tables.Species = Species.Get();
			Tables.Rarities = Rarities.Get();
			Tables.Modifiers = Modifiers.Get();
			Tables.Stats = Stats.Get();
			Tables.Tuning = GetDefault<UFishSettings>()->RollTuning;
			return Tables;
		}

	private:
		static bool Import(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const TCHAR* FileName)
		{
			FString Json;
			if (!FFileHelper::LoadFileToString(Json, *TablePath(FileName)))
			{
				Test.AddError(FString::Printf(TEXT("can't read data/tables/%s"), FileName));
				return false;
			}
			Out.Reset(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
			Out->RowStruct = RowStruct;
			const TArray<FString> Problems = Out->CreateTableFromJSONString(Json);
			for (const FString& Problem : Problems)
			{
				Test.AddError(FString::Printf(TEXT("%s import problem: %s"), FileName, *Problem));
			}
			return Problems.IsEmpty() && Out->GetRowMap().Num() > 0;
		}
	};

	TArray<FString> Args(const TCHAR* Line)
	{
		TArray<FString> Out;
		FString(Line).ParseIntoArrayWS(Out);
		return Out;
	}
}

// The tests stay inside the namespace (no file-scope using-directive: unity builds merge .cpp files).
namespace LureDevTest
{

// ---------------------------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevCommandsRegisteredTest, "Project.Dev.Commands.Registered", LureDevTest::Flags)

bool FLureDevCommandsRegisteredTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { FLureDevCommands::TeleportCommand, FLureDevCommands::SetStanceCommand, FLureDevCommands::GiveFishCommand,
		FLureDevCommands::ScreenshotCommand })
	{
		IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(Name, false);
		TestTrue(FString::Printf(TEXT("%s is a registered console command"), Name), Object && Object->AsCommand() != nullptr);
	}
	const FString Stances = FLureDevCommands::StanceNames();
	TestTrue(FString::Printf(TEXT("stance names come from ELureStance (%s)"), *Stances), Stances.StartsWith(TEXT("Stand, Crouch, Prone")) && !Stances.Contains(TEXT("MAX")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Teleport
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevTeleportFindsTaggedActorsTest, "Project.Dev.Teleport.FindsTaggedActors", LureDevTest::Flags)

bool FLureDevTeleportFindsTaggedActorsTest::RunTest(const FString& Parameters)
{
	FDevWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	// Tags exactly as Content/Python/levels/build_level.py writes them.
	AActor* T3 = W.AddMarker(FVector(800.f, 0.f, 0.f), 90.f, { TEXT("LureLayout"), TEXT("LureId=tp_T3"), TEXT("Lure.Teleport"), TEXT("Teleport=tp_T3"), TEXT("Name=T3 Crouch ceilings") });
	AActor* T4 = W.AddMarker(FVector(0.f, 900.f, 0.f), -45.f, { TEXT("LureId=tp_T4"), TEXT("Lure.Teleport"), TEXT("Teleport=tp_T4") });
	AActor* Spot = W.AddMarker(FVector(-1000.f, 600.f, 0.f), 0.f, { TEXT("LureId=dock_end"), TEXT("Lure.FishingSpot"), TEXT("Spot=dock_end"), TEXT("Habitat=Habitat.Shore"),
		TEXT("CastFrom=-400,600,0") });
	AActor* SpotB = W.AddMarker(FVector(1500.f, -1500.f, 0.f), 0.f, { TEXT("Lure.FishingSpot"), TEXT("Spot=zz_reef") });
	AActor* Start = W.AddMarker(FVector(-200.f, -200.f, 100.f), 20.f, { TEXT("Lure.PlayerStart"), TEXT("Lure.Respawn"), TEXT("Respawn=Dock") });
	// A decoy: an exact tag "T3" loses to the Teleport=tp_T3 marker (tier 1 < tier 0).
	W.AddMarker(FVector(1200.f, 1200.f, 0.f), 0.f, { TEXT("T3") });

	FLureTeleportTarget Target;
	FString Error;
	auto Find = [&](const TCHAR* Id, int32 Index = 0) { Target = FLureTeleportTarget(); Error.Reset(); return FLureDevCommands::FindTeleportTarget(W.World, Id, Index, Target, Error); };

	TestTrue(TEXT("tp_T3 finds the teleport marker"), Find(TEXT("tp_T3")) && Target.Marker.Get() == T3);
	TestTrue(TEXT("the view turns to the marker's yaw"), Target.ViewRotation.IsSet() && FMath::IsNearlyEqual(Target.ViewRotation->Yaw, 90.f, 0.01f));
	TestTrue(TEXT("the feet go to the marker"), Target.FeetLocation.Equals(FVector(800.f, 0.f, 0.f), 0.01));
	TestTrue(TEXT("short id T3 finds Teleport=tp_T3 (before the exact-tag decoy)"), Find(TEXT("T3")) && Target.Marker.Get() == T3);
	TestTrue(TEXT("ids are case-insensitive"), Find(TEXT("TP_t4")) && Target.Marker.Get() == T4);
	TestTrue(TEXT("a full tag works"), Find(TEXT("Teleport=tp_T4")) && Target.Marker.Get() == T4);

	TestTrue(TEXT("Lure.Teleport (exact tag) matches the teleport markers"), Find(TEXT("Lure.Teleport")) && Target.NumMatches == 2);
	const AActor* First = Target.Marker.Get();
	TestTrue(TEXT("index 1 picks the other one"), Find(TEXT("Lure.Teleport"), 1) && Target.Marker.Get() != First && (Target.Marker.Get() == T3 || Target.Marker.Get() == T4));
	TestFalse(TEXT("index out of range fails"), Find(TEXT("Lure.Teleport"), 2));
	TestTrue(TEXT("... with a message naming the count"), Error.Contains(TEXT("matched 2")));

	TestTrue(TEXT("a spot id (Spot=dock_end) finds the fishing spot"), Find(TEXT("dock_end")) && Target.Marker.Get() == Spot);
	TestTrue(TEXT("fishing spot: the feet go to CastFrom"), Target.FeetLocation.Equals(FVector(-400.f, 600.f, 0.f), 0.01));
	TestTrue(TEXT("fishing spot: facing the spot (-X from CastFrom = yaw 180)"), Target.ViewRotation.IsSet() && FMath::IsNearlyEqual(FMath::Abs(Target.ViewRotation->Yaw), 180.f, 0.01f));
	TestTrue(TEXT("Lure.FishingSpot matches both spots"), Find(TEXT("Lure.FishingSpot")) && Target.NumMatches == 2);
	TestTrue(TEXT("Lure.FishingSpot 1 is the other spot"), Find(TEXT("Lure.FishingSpot"), 1) && (Target.Marker.Get() == Spot || Target.Marker.Get() == SpotB));

	TestTrue(TEXT("Respawn=Dock (exact tag)"), Find(TEXT("Respawn=Dock")) && Target.Marker.Get() == Start);
	TestTrue(TEXT("Dock (a Key=Dock tag value)"), Find(TEXT("Dock")) && Target.Marker.Get() == Start);
	TestTrue(TEXT("an actor name works"), Find(*T4->GetName()) && Target.Marker.Get() == T4);

	TestFalse(TEXT("an unknown id fails"), Find(TEXT("no_such_marker")));
	TestTrue(TEXT("... and says what it tried"), Error.Contains(TEXT("no marker")) && Error.Contains(TEXT("no_such_marker")));
	TestFalse(TEXT("an empty id fails"), Find(TEXT("  ")));

	// Coordinates and argument checks.
	TestTrue(TEXT("X Y Z Yaw parse as a point"), FLureDevCommands::ResolveTeleportArgs(W.World, Args(TEXT("100 -250.5 30 45")), Target, Error)
		&& Target.FeetLocation.Equals(FVector(100.f, -250.5f, 30.f), 0.01) && Target.ViewRotation.IsSet() && FMath::IsNearlyEqual(Target.ViewRotation->Yaw, 45.f));
	TestTrue(TEXT("X Y Z keeps the view"), FLureDevCommands::ResolveTeleportArgs(W.World, Args(TEXT("1 2 3")), Target, Error) && !Target.ViewRotation.IsSet());
	TestTrue(TEXT("<marker> <Index>"), FLureDevCommands::ResolveTeleportArgs(W.World, Args(TEXT("Lure.Teleport 1")), Target, Error) && Target.NumMatches == 2);
	TestFalse(TEXT("a non-integer index is refused"), FLureDevCommands::ResolveTeleportArgs(W.World, Args(TEXT("tp_T3 first")), Target, Error));
	TestFalse(TEXT("no arguments is refused"), FLureDevCommands::ResolveTeleportArgs(W.World, TArray<FString>(), Target, Error));
	TestTrue(TEXT("... with the usage"), Error.Contains(TEXT("usage")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevTeleportMovesPlayerTest, "Project.Dev.Teleport.MovesPlayer", LureDevTest::Flags)

bool FLureDevTeleportMovesPlayerTest::RunTest(const FString& Parameters)
{
	FDevWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	W.AddMarker(FVector(800.f, 200.f, 0.f), 90.f, { TEXT("Lure.Teleport"), TEXT("Teleport=tp_T3") });
	W.AddMarker(FVector(-600.f, -300.f, 100.f), 20.f, { TEXT("Lure.Respawn"), TEXT("Respawn=Dock") });   // PlayerStart height
	W.AddBox(FVector(0.f, 1500.f, 60.f), FVector(200.f, 200.f, 60.f));                                   // a 120 cm block
	W.AddMarker(FVector(0.f, 1500.f, 120.f), 0.f, { TEXT("Teleport=tp_Top") });
	APlayerController* PC = nullptr;
	ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector::ZeroVector, 5, &PC);
	if (!Player || !PC)
	{
		return false;
	}

	// The core function: feet on the floor at the marker, view turned, movement stopped.
	Player->GetLureMovement()->Velocity = FVector(300.f, 0.f, 0.f);
	FString Error;
	FVector Feet;
	TestTrue(TEXT("TeleportPawn succeeds"), FLureDevCommands::TeleportPawn(Player, FVector(800.f, 200.f, 0.f), FRotator(0.f, 90.f, 0.f), Error, &Feet));
	TestTrue(FString::Printf(TEXT("XY at the marker (%s)"), *Player->GetActorLocation().ToCompactString()), FVector2D(Player->GetActorLocation()).Equals(FVector2D(800.f, 200.f), 0.5f));
	TestTrue(TEXT("movement stopped"), Player->GetVelocity().IsNearlyZero(1.f));
	TestTrue(TEXT("control yaw 90"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw), 90.f, 0.1f));
	W.Tick(20);
	TestTrue(FString::Printf(TEXT("feet on the floor after settling (%.2f)"), FeetZ(Player)), FMath::Abs(FeetZ(Player)) < FeetTolerance);
	TestTrue(TEXT("walking after the teleport"), Player->GetLureMovement()->IsMovingOnGround());

	// The console command, as Python runs it (execute_console_command -> ProcessUserConsoleInput with the world).
	FOutputDeviceNull Null;
	TestTrue(TEXT("Lure.Teleport Respawn=Dock runs"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Lure.Teleport Respawn=Dock"), Null, W.World));
	W.Tick(20);
	TestTrue(FString::Printf(TEXT("at the PlayerStart-height marker, feet on the floor (%s, feet %.2f)"), *Player->GetActorLocation().ToCompactString(), FeetZ(Player)),
		FVector2D(Player->GetActorLocation()).Equals(FVector2D(-600.f, -300.f), 0.5f) && FMath::Abs(FeetZ(Player)) < FeetTolerance);
	TestTrue(TEXT("control yaw 20"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw), 20.f, 0.1f));

	TestTrue(TEXT("Lure.Teleport tp_Top (on a 120 cm block)"), FLureDevCommands::RunTeleport(Args(TEXT("tp_Top")), W.World, Null));
	W.Tick(20);
	TestTrue(FString::Printf(TEXT("standing on the block (feet %.2f)"), FeetZ(Player)), FMath::Abs(FeetZ(Player) - 120.f) < FeetTolerance);

	TestTrue(TEXT("Lure.Teleport X Y Z Yaw"), FLureDevCommands::RunTeleport(Args(TEXT("-1500 900 0 -90")), W.World, Null));
	W.Tick(20);
	TestTrue(TEXT("at the point"), FVector2D(Player->GetActorLocation()).Equals(FVector2D(-1500.f, 900.f), 0.5f) && FMath::Abs(FeetZ(Player)) < FeetTolerance);
	TestTrue(TEXT("control yaw -90"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw), -90.f, 0.1f));

	AddExpectedMessage(TEXT("no marker \"nowhere\""), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	TestFalse(TEXT("an unknown marker is refused"), FLureDevCommands::RunTeleport(Args(TEXT("nowhere")), W.World, Null));
	TestTrue(TEXT("the player did not move on a refused teleport"), FVector2D(Player->GetActorLocation()).Equals(FVector2D(-1500.f, 900.f), 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevTeleportPicksPlayerTest, "Project.Dev.Teleport.PlayerIdPicksThePlayer", LureDevTest::Flags)

bool FLureDevTeleportPicksPlayerTest::RunTest(const FString& Parameters)
{
	FDevWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	W.AddMarker(FVector(800.f, 0.f, 0.f), 0.f, { TEXT("Teleport=tp_T1") });
	ALurePlayerCharacter* Host = W.SpawnPlayer(*this, FVector(0.f, 0.f, 0.f), 3);
	ALurePlayerCharacter* Guest = W.SpawnPlayer(*this, FVector(0.f, 500.f, 0.f), 8);
	if (!Host || !Guest)
	{
		return false;
	}
	FOutputDeviceNull Null;
	TestTrue(TEXT("Lure.Teleport tp_T1 Player=8 runs"), FLureDevCommands::RunTeleport(Args(TEXT("tp_T1 Player=8")), W.World, Null));
	W.Tick(10);
	TestTrue(TEXT("PlayerId 8 moved"), FVector2D(Guest->GetActorLocation()).Equals(FVector2D(800.f, 0.f), 0.5f));
	TestTrue(TEXT("PlayerId 3 stayed"), FVector2D(Host->GetActorLocation()).Equals(FVector2D(0.f, 0.f), 0.5f));

	// B4 (T-006/T-010 playtest): the yaw reaches that player's controller, not the host's.
	const float HostYaw = Host->GetController() ? static_cast<float>(Host->GetController()->GetControlRotation().Yaw) : 0.f;
	TestTrue(TEXT("Lure.Teleport 800 300 0 180 Player=8 runs"), FLureDevCommands::RunTeleport(Args(TEXT("800 300 0 180 Player=8")), W.World, Null));
	W.Tick(2);
	if (TestNotNull(TEXT("PlayerId 8 has a controller"), Guest->GetController()))
	{
		TestTrue(TEXT("PlayerId 8 control yaw 180"), FMath::IsNearlyEqual(FMath::Abs(FRotator::NormalizeAxis(Guest->GetController()->GetControlRotation().Yaw)), 180.f, 0.1f));
	}
	if (Host->GetController())
	{
		TestTrue(TEXT("PlayerId 3 control yaw unchanged"), FMath::IsNearlyEqual(static_cast<float>(Host->GetController()->GetControlRotation().Yaw), HostYaw, 0.1f));
	}

	FString Error;
	TestNull(TEXT("an unknown PlayerId finds nobody"), FLureDevCommands::FindPlayer(W.World, TOptional<int32>(42), Error));
	TestTrue(TEXT("... and lists the PlayerIds"), Error.Contains(TEXT("42")) && Error.Contains(TEXT("3")) && Error.Contains(TEXT("8")));
	TArray<FString> Bad = Args(TEXT("tp_T1 Player=abc"));
	TOptional<int32> Id;
	TestFalse(TEXT("Player=abc is refused"), FLureDevCommands::TakePlayerId(Bad, Id, Error));
	TestTrue(TEXT("a world is required"), FLureDevCommands::FindPlayer(nullptr, TOptional<int32>(), Error) == nullptr && Error.Contains(TEXT("no game world")));
	TestTrue(TEXT("a test world has authority"), FLureDevCommands::HasAuthority(W.World));
	TestTrue(TEXT("ResolveWorld keeps a game world"), FLureDevCommands::ResolveWorld(W.World) == W.World);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stance
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevSetStanceTest, "Project.Dev.SetStance.ChangesStance", LureDevTest::Flags)

bool FLureDevSetStanceTest::RunTest(const FString& Parameters)
{
	ELureStance Stance = ELureStance::Stand;
	TestTrue(TEXT("parses Crouch"), FLureDevCommands::ParseStance(TEXT("Crouch"), Stance) && Stance == ELureStance::Crouch);
	TestTrue(TEXT("parses prone (any case)"), FLureDevCommands::ParseStance(TEXT("pRoNe"), Stance) && Stance == ELureStance::Prone);
	TestTrue(TEXT("parses Stand"), FLureDevCommands::ParseStance(TEXT(" stand "), Stance) && Stance == ELureStance::Stand);
	TestFalse(TEXT("refuses Sit"), FLureDevCommands::ParseStance(TEXT("Sit"), Stance));
	TestFalse(TEXT("refuses the _MAX entry"), FLureDevCommands::ParseStance(TEXT("ELureStance_MAX"), Stance));
	TestFalse(TEXT("refuses empty"), FLureDevCommands::ParseStance(TEXT(""), Stance));

	FDevWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector::ZeroVector, 1);
	if (!Player)
	{
		return false;
	}
	FOutputDeviceNull Null;
	TestTrue(TEXT("Lure.SetStance Crouch runs"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Lure.SetStance Crouch"), Null, W.World));
	W.Tick(30);
	TestEqual(TEXT("crouched"), static_cast<int32>(Player->GetStance()), static_cast<int32>(ELureStance::Crouch));

	TestTrue(TEXT("Lure.SetStance prone"), FLureDevCommands::RunSetStance(Args(TEXT("prone")), W.World, Null));
	W.Tick(30);
	TestEqual(TEXT("prone"), static_cast<int32>(Player->GetStance()), static_cast<int32>(ELureStance::Prone));
	TestTrue(TEXT("IsProne"), Player->IsProne());

	TestTrue(TEXT("Lure.SetStance Stand Player=1"), FLureDevCommands::RunSetStance(Args(TEXT("Stand Player=1")), W.World, Null));
	W.Tick(30);
	TestEqual(TEXT("standing"), static_cast<int32>(Player->GetStance()), static_cast<int32>(ELureStance::Stand));

	AddExpectedMessage(TEXT("Lure.SetStance: usage"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2, false);
	TestFalse(TEXT("an unknown stance is refused"), FLureDevCommands::RunSetStance(Args(TEXT("Sit")), W.World, Null));
	TestFalse(TEXT("no stance is refused"), FLureDevCommands::RunSetStance(TArray<FString>(), W.World, Null));

	FString Error;
	TestFalse(TEXT("SetStance needs a character"), FLureDevCommands::SetStance(nullptr, ELureStance::Crouch, Error));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fish
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevGiveFishArgsTest, "Project.Dev.GiveFish.ParsesArguments", LureDevTest::Flags)

bool FLureDevGiveFishArgsTest::RunTest(const FString& Parameters)
{
	FLureGiveFishArgs A;
	FString Error;
	TestTrue(TEXT("species only"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish")), A, Error)
		&& A.SpeciesId == FName(TEXT("Bonefish")) && A.RarityId.IsNone() && !A.Seed.IsSet() && !A.PlayerId.IsSet() && !A.bForceModifiers && !A.WeightFraction.IsSet());
	TestTrue(TEXT("species rarity seed"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Rare 42")), A, Error)
		&& A.RarityId == FName(TEXT("Rare")) && A.Seed.Get(0) == 42);
	TestTrue(TEXT("seed before rarity"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish -7 Epic")), A, Error)
		&& A.RarityId == FName(TEXT("Epic")) && A.Seed.Get(0) == -7);
	TestTrue(TEXT("- rolls the rarity"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish - 5")), A, Error) && A.RarityId.IsNone() && A.Seed.Get(0) == 5);
	TestTrue(TEXT("any rolls the rarity"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish any")), A, Error) && A.RarityId.IsNone());
	TestTrue(TEXT("options"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Mods=Albino,Giant Weight=0.5 Player=2")), A, Error)
		&& A.bForceModifiers && A.ModifierIds.Num() == 2 && FMath::IsNearlyEqual(A.WeightFraction.Get(-1.f), 0.5f) && A.PlayerId.Get(-1) == 2);
	TestTrue(TEXT("Mods=- forces no modifiers"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Mods=-")), A, Error) && A.bForceModifiers && A.ModifierIds.IsEmpty());
	TestFalse(TEXT("no species"), FLureDevCommands::ParseGiveFishArgs(TArray<FString>(), A, Error));
	TestTrue(TEXT("... usage"), Error.Contains(TEXT("usage")));
	TestFalse(TEXT("options only"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Player=1")), A, Error));
	TestFalse(TEXT("two seeds"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish 1 2")), A, Error));
	TestFalse(TEXT("two rarities"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Rare Epic")), A, Error));
	TestFalse(TEXT("weight out of range"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Weight=2")), A, Error));
	TestFalse(TEXT("weight not a number"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Weight=heavy")), A, Error));
	TestFalse(TEXT("unknown option"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Color=red")), A, Error));
	TestFalse(TEXT("bad player id"), FLureDevCommands::ParseGiveFishArgs(Args(TEXT("Bonefish Player=x")), A, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevGiveFishRollsTest, "Project.Dev.GiveFish.RollsValidInstance", LureDevTest::Flags)

bool FLureDevGiveFishRollsTest::RunTest(const FString& Parameters)
{
	FFishFixture Fixture;
	if (!Fixture.Load(*this))
	{
		return false;
	}
	const FFishTables Tables = Fixture.Get();
	// Data-driven: every species and every rarity in the JSON sources can be given.
	for (const FName& SpeciesId : Fixture.Species->GetRowNames())
	{
		FLureGiveFishArgs A;
		A.SpeciesId = SpeciesId;
		A.Seed = 1234;
		FFishInstance Fish;
		FString Error;
		const bool bOk = FLureDevCommands::GiveFish(Tables, A, Fish, Error);
		TestTrue(FString::Printf(TEXT("%s rolls (%s)"), *SpeciesId.ToString(), *Error), bOk);
		if (!bOk)
		{
			continue;
		}
		const FFishSpeciesRow* Species = Tables.FindSpecies(SpeciesId);
		TestTrue(TEXT("a valid instance of that species"), Fish.IsValid() && Fish.SpeciesId == SpeciesId);
		TestTrue(TEXT("seed kept"), Fish.Seed == 1234);
		TestTrue(TEXT("a known rarity"), Tables.FindRarity(Fish.RarityId) != nullptr);
		TestTrue(FString::Printf(TEXT("weight %.3f kg is positive and finite"), Fish.WeightKg), FMath::IsFinite(Fish.WeightKg) && Fish.WeightKg > 0.f);
		TestTrue(TEXT("value >= 1, level >= 1"), Fish.Value >= 1 && Fish.Level >= 1);
		TestTrue(TEXT("species row exists"), Species != nullptr);

		FFishInstance Again;
		TestTrue(TEXT("same seed, same fish"), FLureDevCommands::GiveFish(Tables, A, Again, Error) && Again.ToString() == Fish.ToString());

		for (const FName& RarityId : Fixture.Rarities->GetRowNames())
		{
			FLureGiveFishArgs Forced = A;
			Forced.RarityId = RarityId;
			FFishInstance Rare;
			TestTrue(FString::Printf(TEXT("%s %s: forced rarity"), *SpeciesId.ToString(), *RarityId.ToString()),
				FLureDevCommands::GiveFish(Tables, Forced, Rare, Error) && Rare.RarityId == RarityId);
		}
		// Weight=0 vs Weight=1 with the same rarity and no modifiers: the light one is lighter (relative, so rarity data may change).
		FLureGiveFishArgs Light = A;
		Light.RarityId = Fixture.Rarities->GetRowNames()[0];
		Light.WeightFraction = 0.f;
		Light.bForceModifiers = true;
		FLureGiveFishArgs Heavy = Light;
		Heavy.WeightFraction = 1.f;
		FFishInstance Min, Max;
		TestTrue(TEXT("Weight=0 and Weight=1 roll"), FLureDevCommands::GiveFish(Tables, Light, Min, Error) && FLureDevCommands::GiveFish(Tables, Heavy, Max, Error));
		TestTrue(TEXT("Mods= forces no modifiers"), Min.ModifierIds.IsEmpty() && Max.ModifierIds.IsEmpty());
		TestTrue(FString::Printf(TEXT("Weight=0 (%.3f kg) is lighter than Weight=1 (%.3f kg)"), Min.WeightKg, Max.WeightKg),
			Species && (Species->WeightMax <= Species->WeightMin || Min.WeightKg < Max.WeightKg));
	}

	// Refusals name the valid ids.
	FLureGiveFishArgs Bad;
	Bad.SpeciesId = TEXT("NotAFish");
	FFishInstance Fish;
	FString Error;
	TestFalse(TEXT("unknown species"), FLureDevCommands::GiveFish(Tables, Bad, Fish, Error));
	TestTrue(TEXT("... lists the species"), Error.Contains(Fixture.Species->GetRowNames()[0].ToString()));
	TestFalse(TEXT("... and the out fish is empty"), Fish.IsValid());
	Bad.SpeciesId = Fixture.Species->GetRowNames()[0];
	Bad.RarityId = TEXT("Mythic_NotReal");
	TestFalse(TEXT("unknown rarity"), FLureDevCommands::GiveFish(Tables, Bad, Fish, Error));
	TestTrue(TEXT("... lists the rarities"), Error.Contains(Fixture.Rarities->GetRowNames()[0].ToString()));
	Bad.RarityId = NAME_None;
	Bad.bForceModifiers = true;
	Bad.ModifierIds = { TEXT("Glowing_NotReal") };
	TestFalse(TEXT("unknown modifier"), FLureDevCommands::GiveFish(Tables, Bad, Fish, Error));
	TestFalse(TEXT("missing tables"), FLureDevCommands::GiveFish(FFishTables(), Bad, Fish, Error));

	// The console path with the same tables (no world: logs only, like the editor console without PIE).
	FOutputDeviceNull Null;
	FFishInstance Logged;
	const FString Line = Fixture.Species->GetRowNames()[0].ToString() + TEXT(" 99");
	TestTrue(TEXT("RunGiveFish rolls and reports"), FLureDevCommands::RunGiveFish(Args(*Line), nullptr, Null, &Tables, &Logged) && Logged.IsValid() && Logged.Seed == 99);
	return true;
}

/** T-010 follow-up: on the server, Lure.GiveFish lands the fish like a real catch (cooler + XP) for the target player. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevGiveFishLandsTest, "Project.Dev.GiveFish.LandsInCoolerWithXp", LureDevTest::Flags)

bool FLureDevGiveFishLandsTest::RunTest(const FString& Parameters)
{
	FFishFixture Fixture;
	LPT::FWorld W;
	if (!Fixture.Load(*this) || !W.Create(*this))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Levels = LPT::MakeTable(*this, FPlayerLevelRow::StaticStruct(), LPT::FixtureLevelCsv());
	const TStrongObjectPtr<UDataTable> Coolers = LPT::MakeTable(*this, FCoolerRow::StaticStruct(), LPT::FixtureCoolerCsv());
	LPT::FPlayer Player = LPT::SpawnPlayer(*this, W, Levels.Get(), Coolers.Get());
	APlayerController* Controller = W.World->SpawnActor<APlayerController>();
	if (!Player.IsValid() || !TestNotNull(TEXT("controller spawned"), Controller))
	{
		return false;
	}
	Controller->SetPlayerState(Player.State);
	Player.State->SetPlayerId(7);
	Controller->SetAsLocalPlayerController();
	const int32 XpBefore = Player.Progression->GetTotalXp();

	FOutputDeviceNull Null;
	const FFishTables Tables = Fixture.Get();
	FFishInstance Fish;
	const FString Line = Fixture.Species->GetRowNames()[0].ToString() + TEXT(" - 99 Player=7");
	if (!TestTrue(TEXT("RunGiveFish rolls on the server"), FLureDevCommands::RunGiveFish(Args(*Line), W.World, Null, &Tables, &Fish) && Fish.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("the fish is in the player's cooler"), Player.Cooler->GetNumFish(), 1);
	FFishInstance Stored;
	TestTrue(TEXT("... in slot 0, the same roll"), Player.Cooler->GetFishAt(0, Stored) && Stored.Seed == 99 && Stored.ToString() == Fish.ToString());
	TestEqual(TEXT("the fish's XP was added"), Player.Progression->GetTotalXp(), XpBefore + FMath::Max(0, Fish.Xp));

	// Without Player= it is the world's first local player: the same player again.
	const FString Second = Fixture.Species->GetRowNames()[0].ToString() + TEXT(" 5");
	TestTrue(TEXT("a second GiveFish"), FLureDevCommands::RunGiveFish(Args(*Second), W.World, Null, &Tables, &Fish));
	TestEqual(TEXT("... lands in the same cooler"), Player.Cooler->GetNumFish(), 2);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevScreenshotRefusesTest, "Project.Dev.Screenshot.RefusesWithoutViewport", LureDevTest::Flags)

bool FLureDevScreenshotRefusesTest::RunTest(const FString& Parameters)
{
	FDevWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AgentLogs/tests/lure_screenshot_refused.png"));
	IFileManager::Get().Delete(*File, false, true, true);
	FString Error;
	TestFalse(TEXT("no file name is refused"), FLureDevCommands::CaptureViewportWithUI(W.World, TEXT("  "), Error));
	TestFalse(TEXT("no world is refused"), FLureDevCommands::CaptureViewportWithUI(nullptr, File, Error));
	TestFalse(TEXT("a world without a game viewport is refused"), FLureDevCommands::CaptureViewportWithUI(W.World, File, Error));
	TestTrue(TEXT("... with a hint"), Error.Contains(TEXT("game viewport")));
	FOutputDeviceNull Null;
	TestFalse(TEXT("Lure.Screenshot without a file prints the usage"), FLureDevCommands::RunScreenshot(TArray<FString>(), W.World, Null));
	TestFalse(TEXT("Lure.Screenshot in a world without a viewport fails"), FLureDevCommands::RunScreenshot(Args(*File), W.World, Null));
	TestFalse(TEXT("nothing was written"), IFileManager::Get().FileExists(*File));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Python playtest driver
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevPlaytestDriverImportsTest, "Project.Dev.PlaytestDriver.ImportsAndApisExist", LureDevTest::Flags)

bool FLureDevPlaytestDriverImportsTest::RunTest(const FString& Parameters)
{
	const FString Script = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Python/playtest_driver.py"));
	if (!TestTrue(TEXT("Content/Python/playtest_driver.py exists"), IFileManager::Get().FileExists(*Script)))
	{
		return false;
	}
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
	{
		AddError(TEXT("PythonScriptPlugin is not loaded: can't import the driver"));
		return false;
	}
	const FString Report = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AgentLogs/tests/playtest_driver_self_check.json"));
	IFileManager::Get().Delete(*Report, false, true, true);

	// Imports the driver in the editor's Python (no PIE needed), checks every unreal API it uses exists, and sets the PIE
	// play settings to a 2-player listen server and back (what start_pie(players=2) / stop_pie() do).
	const FString Command = FString::Printf(TEXT("py import importlib, playtest_driver as pd; importlib.reload(pd); pd.self_check(r'%s', True)"), *Report);
	FOutputDeviceNull Null;
	GEngine->Exec(nullptr, *Command, Null);

	FString Json;
	if (!TestTrue(TEXT("the self check wrote its report (the import worked)"), FFileHelper::LoadFileToString(Json, *Report)))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("the report is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Missing = nullptr;
	if (Root->TryGetArrayField(TEXT("missing"), Missing) && Missing)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Missing)
		{
			AddError(TEXT("the driver uses an API that does not exist: ") + Value->AsString());
		}
	}
	TestTrue(TEXT("self_check ok"), Root->GetBoolField(TEXT("ok")));
	const TArray<TSharedPtr<FJsonValue>>* Helpers = nullptr;
	TestTrue(TEXT("the docstring lists the helpers"), Root->TryGetArrayField(TEXT("helpers"), Helpers) && Helpers && Helpers->Num() >= 20);
	const TSharedPtr<FJsonObject>* Roundtrip = nullptr;
	TestTrue(TEXT("the play settings round trip ran"), Root->TryGetObjectField(TEXT("roundtrip"), Roundtrip) && Roundtrip);
	return true;
}

/** QA (T-025): a client's own console refuses Teleport and GiveFish (server-only) and nothing moves. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDevClientRefusesTest, "Project.Dev.QA.ClientConsoleRefuses", LureDevTest::Flags)

bool FLureDevClientRefusesTest::RunTest(const FString& Parameters)
{
	FDevWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	W.AddMarker(FVector(800.f, 0.f, 0.f), 0.f, { TEXT("Teleport=tp_T1") });
	ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(0.f, 0.f, 0.f), 1);
	FFishFixture Fixture;
	if (!Player || !Fixture.Load(*this))
	{
		return false;
	}
	// Make the test world report NM_Client without a net driver: UWorld::AttemptDeriveFromURL treats a pending
	// NextURL with a host as a client. No tick runs while it is set, so no travel happens; it is cleared below.
	W.World->NextURL = TEXT("127.0.0.1:7777/Game/Maps/Dev/L_Nowhere");
	const bool bClient = W.World->GetNetMode() == NM_Client;
	TestTrue(TEXT("the test world now reports NM_Client"), bClient);
	TestFalse(TEXT("HasAuthority is false on a client"), FLureDevCommands::HasAuthority(W.World));

	FOutputDeviceNull Null;
	const FVector Before = Player->GetActorLocation();
	const FFishTables Tables = Fixture.Get();
	FFishInstance Fish;
	const FString FishLine = Fixture.Species->GetRowNames()[0].ToString() + TEXT(" 5");
	const bool bTeleported = FLureDevCommands::RunTeleport(Args(TEXT("tp_T1")), W.World, Null);
	const bool bTeleportedXYZ = FLureDevCommands::RunTeleport(Args(TEXT("500 500 0")), W.World, Null);
	const bool bGaveFish = FLureDevCommands::RunGiveFish(Args(*FishLine), W.World, Null, &Tables, &Fish);
	W.World->NextURL.Reset();

	TestFalse(TEXT("Teleport <marker> is refused on a client"), bTeleported);
	TestFalse(TEXT("Teleport X Y Z is refused on a client"), bTeleportedXYZ);
	TestTrue(TEXT("the player did not move"), Player->GetActorLocation().Equals(Before, 0.5));
	TestFalse(TEXT("GiveFish is refused on a client"), bGaveFish);
	TestFalse(TEXT("... and no fish was rolled"), Fish.IsValid());
	TestTrue(TEXT("back to authority after clearing the URL"), FLureDevCommands::HasAuthority(W.World));
	return true;
}

} // namespace LureDevTest

#endif // WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

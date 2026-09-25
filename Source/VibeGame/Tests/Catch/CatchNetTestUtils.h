// Lure T-030 tests (unreal-engineer): the shared network harness of the Project.Catch net tests (moved out of
// CatchNetTest.cpp for T-064 so other files reuse it): a real in-process server (dedicated) with two connected clients
// (UE::Net::FTestWorlds), the test dock and the shipped catch data on every machine. Names are qualified (no using).

#pragma once

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureSellCounter.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionTypes.h"
#include "Tests/NetTestHelpers.h"

namespace LureCatchNetTest
{
	constexpr float NetDt = 1.0f / 60.0f;

	/** The shipped data every machine uses (text sources, never the binary assets) */
	struct FData
	{
		TStrongObjectPtr<UDataTable> Movement;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Freshness;
		TStrongObjectPtr<UDataTable> Catch;
		TStrongObjectPtr<UDataTable> Display;
		TStrongObjectPtr<UDataTable> Markets;
		TStrongObjectPtr<UDataTable> Levels;
		FishQA::FTables Fish;
		FLureCatchRow Tuning;

		bool Load(FAutomationTestBase& Test)
		{
			Movement = LCT::Shipped(Test, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"));
			Coolers = LCT::Shipped(Test, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
			Freshness = LCT::Shipped(Test, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness.csv"));
			Catch = LCT::Shipped(Test, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
			Display = LCT::Shipped(Test, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
			Markets = LCT::MakeTableChecked(Test, FFishMarketRow::StaticStruct(), LCT::FixtureMarketCsv(), false, TEXT("markets"));
			Levels = LCT::MakeTableChecked(Test, FPlayerLevelRow::StaticStruct(), TEXT("Name,Level,XpToNext,DevComment\nL01,1,100,\nL02,2,150,\nL03,3,0,\n"), false, TEXT("levels"));
			if (!Movement.IsValid() || !Coolers.IsValid() || !Freshness.IsValid() || !Catch.IsValid() || !Display.IsValid() || !FishQA::LoadReal(Test, Fish))
			{
				return false;
			}
			const FLureCatchRow* Row = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("CatchNetTest"), false);
			if (!Test.TestNotNull(TEXT("DT_Catch Default"), Row))
			{
				return false;
			}
			Tuning = *Row;
			return true;
		}

		void Inject(UWorld* World) const
		{
			if (ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
			{
				Subsystem->SetTuning(Tuning);
				Subsystem->SetFreshnessTable(Freshness.Get());
				Subsystem->SetCoolerTable(Coolers.Get());
				Subsystem->SetCoolerDisplayTable(Display.Get());
				Subsystem->SetFishTables(Fish.Get());
			}
		}
	};

	/** The level geometry every machine has: the test dock (top z = LCT::DockTop) */
	inline void AddDock(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Dock"));
		Box->SetBoxExtent(FVector(LCT::DockHalf, LCT::DockHalf, LCT::DockTop * 0.5f), false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetRelativeLocation_Direct(FVector(0.0f, 0.0f, LCT::DockTop * 0.5f));
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
	}

	/** A server with two connected players standing on the dock, and the helpers to find their copies on the clients */
	struct FNet
	{
		UE::Net::FTestWorlds& Worlds;
		FData Data;
		UWorld* Server = nullptr;
		ALurePlayerCharacter* Players[2] = { nullptr, nullptr };

		explicit FNet(UE::Net::FTestWorlds& InWorlds) : Worlds(InWorlds) {}

		bool Create(FAutomationTestBase& Test)
		{
			Test.AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			// Harness artifact: the docks are spawned per world (not replicated), so the players' movement base can't be sent.
			Test.AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			if (!Data.Load(Test))
			{
				return false;
			}
			Server = Worlds.Server.GetWorld();
			if (!Test.TestTrue(TEXT("harness: the server world is up"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()))
			{
				return false;
			}
			for (int32 Client = 0; Client < 2; ++Client)
			{
				if (!Test.TestTrue(FString::Printf(TEXT("harness: client %d connects"), Client), Worlds.CreateAndConnectClient()))
				{
					return false;
				}
			}
			AddDock(Server);
			Data.Inject(Server);
			for (UE::Net::FTestWorldInstance& Client : Worlds.Clients)
			{
				AddDock(Client.GetWorld());
				Data.Inject(Client.GetWorld());
			}
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Data.Movement.Get(), Rows, Problems);
			const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.0f;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				APlayerController* Controller = Worlds.GetServerPlayerControllerOfClient(Index);
				const FTransform Transform(FRotator::ZeroRotator, FVector(0.0f, Index == 0 ? -100.0f : 100.0f, LCT::DockTop + HalfHeight + 2.15f));
				ALurePlayerCharacter* Pawn = Server->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Test.TestNotNull(TEXT("harness: controller"), Controller) || !Test.TestNotNull(TEXT("harness: pawn"), Pawn))
				{
					return false;
				}
				Pawn->GetLureMovement()->ApplyMovementTable(Data.Movement.Get());
				Pawn->FinishSpawning(Transform);
				Controller->Possess(Pawn);
				if (ULureProgressionComponent* Progression = LCT::ProgressionOf(Pawn))
				{
					Progression->SetLevelTable(Data.Levels.Get());
				}
				Players[Index] = Pawn;
			}
			const bool bReady = Until([this]()
			{
				for (int32 Client = 0; Client < 2; ++Client)
				{
					const ALurePlayerCharacter* Mine = On(Client, Players[Client]);
					const ALurePlayerCharacter* Theirs = On(Client, Players[1 - Client]);
					if (!Mine || !Theirs || !Mine->IsLocallyControlled() || !Mine->GetHands() || !Theirs->GetHands() || !ClientPC(Client) || !ClientPC(Client)->PlayerState)
					{
						return false;
					}
				}
				return true;
			}, 600);
			if (!Test.TestTrue(TEXT("harness: each client has its own (controlled) player and the other one"), bReady))
			{
				return false;
			}
			Worlds.TickAll(30); // settle on the dock
			return true;
		}

		template <typename T>
		T* On(int32 Client, T* ServerObject) const
		{
			// The harness ensures when asked about an object the server hasn't replicated yet (no net id): ask that first.
			if (!ServerObject || !Worlds.IsServerObjectReplicated(ServerObject) || !Worlds.DoesReplicatedObjectExistOnClient(ServerObject, static_cast<uint32>(Client)))
			{
				return nullptr;
			}
			return Cast<T>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(ServerObject), static_cast<uint32>(Client)));
		}

		bool Until(TFunctionRef<bool()> Predicate, int32 MaxTicks = 180)
		{
			return Worlds.TickAllUntil([&Predicate]() { return Predicate(); }, NetDt, MaxTicks);
		}

		APlayerController* ClientPC(int32 Client) const
		{
			UWorld* World = Worlds.Clients.IsValidIndex(Client) ? Worlds.Clients[Client].GetWorld() : nullptr;
			return World ? World->GetFirstPlayerController() : nullptr;
		}

		/** Client's own player on its machine */
		ALurePlayerCharacter* Mine(int32 Client) const { return On(Client, Players[Client]); }

		ULureInteractionComponent* Keys(int32 Client) const { return LCT::InteractionOf(Mine(Client)); }

		/** The client's player looks at Target (its own view, which the server never checks) */
		void LookAt(int32 Client, const FVector& Target) const
		{
			ALurePlayerCharacter* Pawn = Mine(Client);
			if (APlayerController* Controller = ClientPC(Client); Controller && Pawn)
			{
				Controller->SetControlRotation((Target - Pawn->GetPawnViewLocation()).Rotation());
			}
		}

		ALureSellCounter* SpawnCounter(const FVector& Top, float Yaw)
		{
			const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Top);
			ALureSellCounter* Counter = Server->SpawnActorDeferred<ALureSellCounter>(ALureSellCounter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Counter)
			{
				Counter->MarketId = TEXT("Default");
				Counter->SetMarketTable(Data.Markets.Get());
				Counter->FinishSpawning(Transform);
			}
			return Counter;
		}
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

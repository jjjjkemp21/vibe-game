// Lure T-028 QA (qa-engineer, 2026-09-23): rod steering over a real network, black-box.
// Project.Fishing.Fight.Rod.QA.Net2P.*
// The engine's in-process harness (UE::Net::FTestWorlds): a dedicated server and two connected clients with real net drivers, so the
// owner's ServerSetFightInput really is an unreliable RPC on the wire and FightNet really replicates to the other player. The harness
// can drop or delay every packet a client sends in one frame (TickClientsAndDrop / TickClientsAndDelay).
// Spec: reel-fight-rules.md "Networking": 4 bytes, unreliable, at most every FightInputSendSeconds on change, at once for a step change,
// again every FightInputResendSeconds; the server drops packets with no fight or another FightId and clamps the rest; tension, line and
// outcome are only its own simulation; FightNet carries the rod angle back for the other players.

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Tests/NetTestHelpers.h"

namespace LureRodQA
{
	namespace RodNet
	{
		constexpr float Dt = 1.f / 60.f;
		const FVector StandFeet(250.f, 0.f, LureFightQA::DockTop);

		/** Log lines the harness itself causes (not the game's business). */
		inline void ExpectHarnessMessages(FAutomationTestBase& Test)
		{
			Test.AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			Test.AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			Test.AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			Test.AddExpectedMessagePlain(TEXT("using the built-in"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			Test.AddExpectedMessagePlain(TEXT("No owning connection"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		}

		/** The dock and the water surface (every machine has the level geometry). */
		inline void AddLevel(UWorld* World)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Dock = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Box = NewObject<UBoxComponent>(Dock, NAME_None);
			Box->SetMobility(EComponentMobility::Static);
			Box->SetBoxExtent(FVector(LureFightQA::DockEdgeX, LureFightQA::DockEdgeX, LureFightQA::DockTop * 0.5f), false);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->SetRelativeLocation_Direct(FVector(0.f, 0.f, LureFightQA::DockTop * 0.5f));
			Dock->SetRootComponent(Box);
			Box->RegisterComponent();

			AActor* Water = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Surface = NewObject<UBoxComponent>(Water, NAME_None);
			Surface->SetBoxExtent(FVector(20000.f, 20000.f, 50.f), false);
			Surface->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Surface->SetRelativeLocation_Direct(FVector(0.f, 0.f, -50.f));
			Water->SetRootComponent(Surface);
			Surface->RegisterComponent();
			Water->Tags.Add(GetDefault<ULureFishingSettings>()->WaterTag);
		}

		inline ALurePlayerCharacter* SpawnFor(UWorld* World, APlayerController* Controller, const FVector& Feet, const UDataTable* Movement)
		{
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Movement, Rows, Problems);
			const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.f;
			const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, HalfHeight + 2.15f));
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

		/** Server RPC parameters as the generated thunk lays them out (four bytes). */
		struct FFightInputParams
		{
			uint8 FightId;
			uint8 RodPitch;
			uint8 RodYaw;
			uint8 ReelStep;
		};

		/** A dedicated server with players 0 and 1, each on their own client; player 0 has a fish on. */
		struct FRig
		{
			UE::Net::FTestWorlds Worlds{ FString(TEXT("/Engine/Maps/Entry")), FString(TEXT("/Script/VibeGame.LureGameMode")) };
			LureFightQA::FFightTables Data;
			FishQA::FTables Fish;
			FFishInstance Bonefish;
			TStrongObjectPtr<UDataTable> Movement;
			TStrongObjectPtr<UDataTable> Patterns;
			ALurePlayerCharacter* Players[2] = { nullptr, nullptr }; // on the server
			ALurePlayerCharacter* OwnChar0 = nullptr;   // player 0 on its own machine (client 0)
			ALurePlayerCharacter* OtherChar0 = nullptr; // player 0 as player 1 sees it (client 1)
			ALurePlayerCharacter* OwnChar1 = nullptr;   // player 1 on its own machine
			ULureFishingComponent* Server0 = nullptr;
			ULureFishingComponent* Own0 = nullptr;
			ULureFishingComponent* Other0 = nullptr;
			ULureFishingComponent* Own1 = nullptr;
			APlayerController* PC0 = nullptr;
			APlayerController* PC1 = nullptr;

			void Tick(int32 Frames) { Worlds.TickAll(Frames); }

			template <typename PredicateT>
			bool TickUntil(const PredicateT& Predicate, int32 MaxFrames) { return Worlds.TickAllUntil(Predicate, Dt, MaxFrames); }

			ALurePlayerCharacter* CopyOn(int32 Client, ALurePlayerCharacter* ServerCharacter)
			{
				if (!Worlds.DoesReplicatedObjectExistOnClient(ServerCharacter, static_cast<uint32>(Client)))
				{
					return nullptr;
				}
				return Cast<ALurePlayerCharacter>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(ServerCharacter), static_cast<uint32>(Client)));
			}

			bool Create(FAutomationTestBase& Test)
			{
				FString MovementCsv;
				if (!Data.Load(Test) || !FishQA::LoadReal(Test, Fish) || !LureFightQA::RollFish(Test, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish)
					|| !LureFightQA::ReadSource(Test, TEXT("DT_Movement.csv"), MovementCsv)
					|| !LureFightQA::MakeTableChecked(Test, Movement, FLureMovementRow::StaticStruct(), MovementCsv, false, TEXT("DT_Movement.csv")))
				{
					return false;
				}
				// The fish holds on a steady leftward swim (the run hint is on) and never ends the fight by itself.
				Patterns = PatternTable(OneMove(SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, -0.6f)));
				UWorld* ServerWorld = Worlds.Server.GetWorld();
				if (!Test.TestTrue(TEXT("QA harness: the server world is up with a net driver"), Worlds.Server.IsLoaded() && ServerWorld && ServerWorld->GetNetDriver()))
				{
					return false;
				}
				for (int32 Client = 0; Client < 2; ++Client)
				{
					if (!Test.TestTrue(FString::Printf(TEXT("QA harness: client %d connects"), Client), Worlds.CreateAndConnectClient()))
					{
						return false;
					}
				}
				AddLevel(ServerWorld);
				for (UE::Net::FTestWorldInstance& Client : Worlds.Clients)
				{
					AddLevel(Client.GetWorld());
				}
				for (int32 Index = 0; Index < 2; ++Index)
				{
					Players[Index] = SpawnFor(ServerWorld, Worlds.GetServerPlayerControllerOfClient(Index), StandFeet + FVector(0.f, Index == 0 ? -150.f : 150.f, 0.f), Movement.Get());
					if (!Test.TestNotNull(FString::Printf(TEXT("QA harness: player %d spawned"), Index), Players[Index]))
					{
						return false;
					}
				}
				const bool bReplicated = Worlds.TickAllUntil([this]()
				{
					for (int32 Client = 0; Client < 2; ++Client)
					{
						const ALurePlayerCharacter* Own = CopyOn(Client, Players[Client]);
						const ALurePlayerCharacter* Other = CopyOn(Client, Players[1 - Client]);
						if (!Own || !Other || !Own->IsLocallyControlled() || !Own->GetFishing() || !Other->GetFishing() || !Own->GetController())
						{
							return false;
						}
					}
					return true;
				}, Dt, 600);
				if (!Test.TestTrue(TEXT("QA harness: each client has its own (controlled) player and the other player"), bReplicated))
				{
					return false;
				}
				OwnChar0 = CopyOn(0, Players[0]);
				OtherChar0 = CopyOn(1, Players[0]);
				OwnChar1 = CopyOn(1, Players[1]);
				Server0 = Players[0]->GetFishing();
				Own0 = OwnChar0->GetFishing();
				Other0 = OtherChar0->GetFishing();
				Own1 = OwnChar1->GetFishing();
				PC0 = Cast<APlayerController>(OwnChar0->GetController());
				PC1 = Cast<APlayerController>(OwnChar1->GetController());
				// Every machine uses the same profile and fight tables (the owner reads its aim with the same tuning as the server).
				for (ULureFishingComponent* Component : { Server0, Players[1]->GetFishing(), Own0, Other0, Own1 })
				{
					Component->SetFishingProfile(LureFightQA::QuickProfile());
					Component->SetFightTables(Data.Gear.Get(), Patterns.Get(), Data.Fight.Get());
				}
				Server0->SetFishTables(Fish.Get());
				Server0->SetRandomSeed(606);
				Server0->TimeOfDayOverride = 12.f;
				Worlds.TickAll(60);
				return Test.TestNotNull(TEXT("client 0's own controller"), PC0) && Test.TestNotNull(TEXT("client 1's own controller"), PC1)
					&& Test.TestFalse(TEXT("client machines have no authority over the players"), OwnChar0->HasAuthority() || OtherChar0->HasAuthority());
			}

			/** The server casts for player 0 and hooks the fish; both clients see the fight, and player 0's machine steers. */
			bool StartFight(FAutomationTestBase& Test)
			{
				if (!Test.TestTrue(TEXT("the server casts for player 0"), Server0->AuthorityCast(0.5f, 0.f))
					|| !Test.TestTrue(TEXT("the bobber lands"), TickUntil([this]() { return Server0->GetFishingState() == ELureFishingState::Waiting; }, 240))
					|| !Test.TestTrue(TEXT("the server hooks the fish"), Server0->AuthorityHookFish(Bonefish)))
				{
					return false;
				}
				return Test.TestTrue(TEXT("player 0's machine steers, player 1's machine sees the fight"), TickUntil([this]()
				{
					return Own0->IsSteeringRod() && Other0->GetFightNet().bActive && Other0->GetFishingState() == ELureFishingState::Hooked;
				}, 120));
			}
		};

		inline bool Near(float A, float B) { return FMath::IsNearlyEqual(A, B, 1.f / 127.f + 1.0e-5f); }
	}

	/**
	 *  Player 0 steers on its own machine: its view takes none of it, the aim reaches the server over the wire and the fight runs with it;
	 *  player 1 sees player 0's rod (angle, eased, run side, reel step) and never steers it, and its own look is its view. When the fight
	 *  ends on the server, player 0's look is the view again and player 1 sees the rod go level.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQANet2PSteer, "Project.Fishing.Fight.Rod.QA.Net2P.OwnerSteersOverTheWireOthersSeeIt", Flags)
	bool FRodQANet2PSteer::RunTest(const FString& Parameters)
	{
		RodNet::ExpectHarnessMessages(*this);
		RodNet::FRig Rig;
		if (!Rig.Create(*this) || !Rig.StartFight(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = Rig.Own0->GetFightTuning();
		Rig.PC0->RotationInput = FRotator::ZeroRotator;
		Rig.OwnChar0->DoLook(0.5f * T.RodAimSideDeg, 0.5f * T.RodAimUpDeg);
		TestTrue(TEXT("player 0's machine: the look input does not turn its view"), Rig.PC0->RotationInput.IsNearlyZero());
		TestTrue(TEXT("player 0's machine: the rod is half right, half back"), Rig.Own0->GetRodAim().Equals(FVector2D(0.5, 0.5), 1.0e-4));

		TestTrue(TEXT("the server gets the aim over the wire"), Rig.TickUntil([&Rig]()
		{
			const FLureFightInput Now = Rig.Server0->GetServerFightInput();
			return RodNet::Near(Now.RodPitch, 0.5f) && RodNet::Near(Now.RodYaw, 0.5f);
		}, 30));
		Rig.Tick(2);
		TestTrue(TEXT("the server's fight runs with it"), RodNet::Near(Rig.Server0->GetFightState().RodPitch, 0.5f) && RodNet::Near(Rig.Server0->GetFightState().RodYaw, 0.5f));
		TestFalse(TEXT("on the server player 0 is remote: never steering there"), Rig.Server0->IsSteeringRod());

		TestTrue(TEXT("player 1's machine: player 0's rod angle arrives"), Rig.TickUntil([&Rig]() { return Rig.Other0->GetRodAim().Equals(FVector2D(0.5, 0.5), 1.0 / 127.0 + 1.0e-5); }, 30));
		TestFalse(TEXT("player 1's machine never steers player 0's rod"), Rig.Other0->IsSteeringRod());
		TestFalse(TEXT("... nor takes look input for it"), Rig.Other0->ConsumeLookInput(10.f, 10.f));
		Rig.Tick(30);
		const FVector2D Eased = Rig.Other0->GetRodAimForAnimation();
		TestTrue(FString::Printf(TEXT("player 1's machine: player 0's rod eases to the angle (%.3f, %.3f)"), Eased.X, Eased.Y), Eased.Equals(FVector2D(0.5, 0.5), 0.02));
		TestTrue(TEXT("player 1's machine: the fish's run side"), Rig.Other0->GetFightNet().RunSide == ELureFightRunSide::Left);
		TestTrue(TEXT("player 0's HUD shows the server's run hint"), HasLine(Rig.Own0->GetStatusText(), TEXT("Fish runs LEFT: pull right")));

		Rig.PC1->RotationInput = FRotator::ZeroRotator;
		Rig.OwnChar1->DoLook(3.f, -2.f);
		TestTrue(TEXT("player 1's own look input still turns its view"), FMath::IsNearlyEqual(static_cast<float>(Rig.PC1->RotationInput.Yaw), 3.f)
			&& FMath::IsNearlyEqual(static_cast<float>(Rig.PC1->RotationInput.Pitch), -2.f));
		Rig.PC1->RotationInput = FRotator::ZeroRotator;

		Rig.Own0->StepReelSpeed(1);
		TestTrue(TEXT("a reel step reaches the server at once"), Rig.TickUntil([&Rig]() { return Rig.Server0->GetServerFightInput().ReelStep == 2; }, 10));
		TestTrue(TEXT("... and player 1's machine shows it"), Rig.TickUntil([&Rig]() { return Rig.Other0->GetReelStep() == 2; }, 20));

		Rig.Server0->AuthorityReelIn();
		TestTrue(TEXT("the fight ends: player 0's machine stops steering"), Rig.TickUntil([&Rig]() { return !Rig.Own0->IsSteeringRod(); }, 30));
		Rig.PC0->RotationInput = FRotator::ZeroRotator;
		Rig.OwnChar0->DoLook(4.f, 1.f);
		TestTrue(TEXT("player 0's look input turns its view again"), FMath::IsNearlyEqual(static_cast<float>(Rig.PC0->RotationInput.Yaw), 4.f)
			&& FMath::IsNearlyEqual(static_cast<float>(Rig.PC0->RotationInput.Pitch), 1.f));
		Rig.PC0->RotationInput = FRotator::ZeroRotator;
		TestTrue(TEXT("player 1's machine: player 0's rod goes back to level"), Rig.TickUntil([&Rig]()
		{
			return Rig.Other0->GetRodAim().IsZero() && Rig.Other0->GetRodAimForAnimation().Size() < 0.01;
		}, 90));
		return true;
	}

	/**
	 *  The aim is unreliable, so the wire may lose or reorder it: every packet player 0's machine sends for 8 frames is dropped (the new aim
	 *  with them); with no further input the resend repairs the server within FightInputResendSeconds. Then one aim is held back 20 frames
	 *  while a newer one goes out: whatever the late packet does, the server ends on the newest aim.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQANet2PLoss, "Project.Fishing.Fight.Rod.QA.Net2P.DroppedAndDelayedAimIsRepaired", Flags)
	bool FRodQANet2PLoss::RunTest(const FString& Parameters)
	{
		RodNet::ExpectHarnessMessages(*this);
		RodNet::FRig Rig;
		if (!Rig.Create(*this) || !Rig.StartFight(*this))
		{
			return false;
		}
		const float Side = Rig.Own0->GetFightTuning().RodAimSideDeg;
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		auto ServerYaw = [&Rig]() { return Rig.Server0->GetServerFightInput().RodYaw; };

		Rig.OwnChar0->DoLook(-2.f * Side, 0.f); // fully left
		TestTrue(TEXT("settled: the server has the rod fully left"), Rig.TickUntil([&]() { return RodNet::Near(ServerYaw(), -1.f); }, 30));
		Rig.Tick(20);

		Rig.OwnChar0->DoLook(4.f * Side, 0.f); // fully right, sent while everything is lost
		for (int32 Frame = 0; Frame < 8; ++Frame)
		{
			Rig.Worlds.TickServer();
			Rig.Worlds.TickClientsAndDrop();
			++GFrameCounter;
		}
		TestTrue(FString::Printf(TEXT("the packets with the new aim were lost: the server still has the old one (%.3f)"), ServerYaw()), RodNet::Near(ServerYaw(), -1.f));
		int32 Frames = 0;
		while (Frames < 90 && !RodNet::Near(ServerYaw(), 1.f))
		{
			Rig.Tick(1);
			++Frames;
		}
		const int32 Allowed = FMath::CeilToInt(Settings->FightInputResendSeconds / RodNet::Dt) + 6;
		TestTrue(FString::Printf(TEXT("no further input: the resend repairs the server in %d frames (<= %d)"), Frames, Allowed), RodNet::Near(ServerYaw(), 1.f) && Frames <= Allowed);

		// C (centered) goes out in a frame whose packets are held back 20 frames; D (half left) goes out normally right after.
		Rig.Tick(10);
		Rig.OwnChar0->DoLook(-Side, 0.f); // C: centered
		Rig.Worlds.TickServer();
		Rig.Worlds.TickClientsAndDelay(20);
		++GFrameCounter;
		Rig.OwnChar0->DoLook(-0.5f * Side, 0.f); // D: half left
		bool bSawD = false;
		bool bStaleAfterD = false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Rig.Tick(1);
			const float Now = ServerYaw();
			bStaleAfterD |= bSawD && RodNet::Near(Now, 0.f);
			bSawD |= RodNet::Near(Now, -0.5f);
		}
		AddInfo(FString::Printf(TEXT("a late aim packet after a newer one: %s"), bStaleAfterD ? TEXT("applied for a moment, then repaired by the resend") : TEXT("never applied")));
		TestTrue(TEXT("the newer aim reached the server"), bSawD);
		TestTrue(FString::Printf(TEXT("the server ends on the newest aim (%.3f)"), ServerYaw()), RodNet::Near(ServerYaw(), -0.5f));
		return true;
	}

	/**
	 *  What a client can't do: packets naming another fight are never used; out-of-range bytes (a flood of them) are clamped; player 1's
	 *  machine can't send input for player 0's rod at all; player 0's own machine can't set the reel, the rod, the tension or the outcome
	 *  through its copy (Authority calls, a tampered FightNet): the server's fight goes on and the copy is the server's again.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQANet2PHostile, "Project.Fishing.Fight.Rod.QA.Net2P.ForeignStaleAndHostileInputIgnored", Flags)
	bool FRodQANet2PHostile::RunTest(const FString& Parameters)
	{
		RodNet::ExpectHarnessMessages(*this);
		RodNet::FRig Rig;
		if (!Rig.Create(*this) || !Rig.StartFight(*this))
		{
			return false;
		}
		Rig.Tick(20);
		const uint8 FightId = Rig.Server0->GetFightNet().FightId;
		const int32 Top = FLureFight::NumReelSteps(Rig.Server0->GetFightTuning()) - 1;
		const int32 Default = FLureFight::DefaultReelStep(Rig.Server0->GetFightTuning());
		auto Input = [&Rig]() { return Rig.Server0->GetServerFightInput(); };
		UFunction* Rpc = Rig.Own0->FindFunction(TEXT("ServerSetFightInput"));
		if (!TestNotNull(TEXT("ServerSetFightInput"), Rpc) || !TestTrue(TEXT("its parameters are 4 bytes"), Rpc->ParmsSize == sizeof(RodNet::FFightInputParams)))
		{
			return false;
		}
		TestTrue(TEXT("settled: the server has the owner's neutral rod"), Input().RodPitch == 0.f && Input().RodYaw == 0.f && Input().ReelStep == Default);

		int32 Applied = 0;
		for (const uint8 Id : { static_cast<uint8>(FightId - 1), static_cast<uint8>(FightId + 1), static_cast<uint8>(FightId + 77) })
		{
			for (int32 Frame = 0; Frame < 10; ++Frame)
			{
				RodNet::FFightInputParams Params{ Id, 254, 254, 0 };
				Rig.Own0->ProcessEvent(Rpc, &Params);
				Rig.Tick(1);
				Applied += (Input().RodPitch == 1.f && Input().RodYaw == 1.f) ? 1 : 0;
			}
		}
		TestEqual(TEXT("packets naming another fight: never used"), Applied, 0);

		int32 Clamped = 0;
		int32 Unexpected = 0;
		for (int32 Frame = 0; Frame < 20; ++Frame)
		{
			RodNet::FFightInputParams Params{ FightId, 255, 0, 200 };
			Rig.Own0->ProcessEvent(Rpc, &Params);
			Rig.Tick(1);
			const FLureFightInput Now = Input();
			if (Now.RodPitch == 1.f && Now.RodYaw == -1.f && Now.ReelStep == Top)
			{
				++Clamped;
			}
			else if (!(Now.RodPitch == 0.f && Now.RodYaw == 0.f && Now.ReelStep == Default))
			{
				++Unexpected; // neither the clamped hostile input nor the owner's own resend
			}
		}
		TestTrue(FString::Printf(TEXT("a flood of out-of-range bytes is clamped (pitch 255 -> +1, yaw 0 -> -1, step 200 -> %d): %d of 20 frames"), Top, Clamped), Clamped >= 10);
		TestEqual(TEXT("... and the server never holds anything else"), Unexpected, 0);
		Rig.Tick(20);
		TestTrue(TEXT("after the flood the owner's resend restores its own rod"), Input().RodPitch == 0.f && Input().RodYaw == 0.f && Input().ReelStep == Default);

		UFunction* ForeignRpc = Rig.Other0->FindFunction(TEXT("ServerSetFightInput"));
		int32 Foreign = 0;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			RodNet::FFightInputParams Params{ FightId, 0, 254, 2 };
			if (ForeignRpc)
			{
				Rig.Other0->ProcessEvent(ForeignRpc, &Params);
			}
			Rig.Other0->AuthoritySetFightInput(FightId, -1.f, 1.f, 2);
			Rig.Tick(1);
			Foreign += (Input().RodPitch == -1.f && Input().RodYaw == 1.f) ? 1 : 0;
		}
		TestEqual(TEXT("player 1's machine can't steer player 0's rod (RPC on its copy, Authority call)"), Foreign, 0);

		Rig.Server0->AuthoritySetReeling(true); // the server reels: the line and tension change every frame
		Rig.Tick(5);
		FLureFightNetState* Copy = LureFightQA::ReplicatedField<FLureFightNetState>(Rig.Own0, TEXT("FightNet"));
		if (TestNotNull(TEXT("player 0's copy of FightNet"), Copy))
		{
			Copy->Tension = 0.f;
			Copy->LineOut = 1.f;
			Copy->Outcome = ELureFightOutcome::Landed;
		}
		Rig.Own0->AuthoritySetReeling(false);
		Rig.Own0->AuthoritySetFightInput(FightId, -1.f, -1.f, 0);
		Rig.Own0->AuthorityReelIn();
		Rig.Own0->AuthorityHookFish(Rig.Bonefish);
		Rig.Tick(3);
		const FLureFightNetState& Server = Rig.Server0->GetFightNet();
		TestTrue(TEXT("the server's fight goes on: hooked, no outcome, still reeling"), Rig.Server0->GetFishingState() == ELureFishingState::Hooked && Server.bActive
			&& Server.Outcome == ELureFightOutcome::None && Rig.Server0->IsServerReeling());
		TestFalse(TEXT("... with its own rod input, not the client's"), Input().RodPitch == -1.f && Input().RodYaw == -1.f);
		TestTrue(FString::Printf(TEXT("player 0's copy is the server's again (line out %.0f vs %.0f cm)"), Rig.Own0->GetFightNet().LineOut, Server.LineOut),
			Rig.Own0->GetFightNet().LineOut > 100.f && FMath::IsNearlyEqual(Rig.Own0->GetFightNet().LineOut, Server.LineOut, 50.f));
		Rig.Server0->AuthorityReelIn();
		Rig.Tick(5);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

// Independent QA tests for the fighting fish visual (T-029): spawn and despawn, one fish per fight per machine,
// dedicated servers, leaks, and the landed hand-off (OnFightFishLanded / KeepLandedFish).
// Project.FishVisual.QA.{Lifecycle,Multiplayer,Net,Landed}.*  Written by the qa-engineer from docs/specs/fight-fish-visual.md.

#include "Tests/FishVisual/FightFishVisualQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureWaterVolume.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/ScopeExit.h"

// Tests live inside the helper namespace (no file-scope using-directive: unity builds merge test files).
namespace LureFightFishQA
{
	// =================================================================================================================
	// Lifecycle: every way a fight ends
	// =================================================================================================================

	/** Landed, snapped, spooled, thrown hook, reeled in and cancelled: one fish during the fight, the right ending, nothing left. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAEveryOutcome, "Project.FishVisual.QA.Lifecycle.EveryFightOutcome", Flags)
	bool FFishVisualQAEveryOutcome::RunTest(const FString& Parameters)
	{
		FRealScene Scene;
		if (!Scene.Init(*this))
		{
			return false;
		}
		for (const EOutcomeCase Case : { EOutcomeCase::Landed, EOutcomeCase::Snapped, EOutcomeCase::Spooled, EOutcomeCase::ThrewHook, EOutcomeCase::ReeledIn,
			EOutcomeCase::Cancelled })
		{
			if (!Scene.RunOutcome(*this, Case))
			{
				return false;
			}
		}
		TestEqual(TEXT("exactly one landed event over the six fights"), Scene.Landed, 1);
		return true;
	}

	/** Engine water with its surface at z = 0 around the dock (as Project.Fishing.Fight.SwimClimb). */
	static ALureWaterVolume* AddWaterVolume(UWorld* World, float Depth)
	{
		const FTransform Transform(FRotator::ZeroRotator, FVector::ZeroVector);
		ALureWaterVolume* Volume = World->SpawnActorDeferred<ALureWaterVolume>(ALureWaterVolume::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Volume)
		{
			return nullptr;
		}
		Volume->SurfaceHalfSize = FVector2D(3500.0, 3500.0);
		Volume->WaterDepth = Depth;
		Volume->FinishSpawning(Transform);
		return Volume;
	}

	/** After a fight cut short by swimming or climbing: no landed event, the fish swims away, then nothing is left. */
	static void ExpectEscapedAndGone(FAutomationTestBase& Test, FRealScene& Scene, TWeakObjectPtr<ALureFightFish> Weak, const TCHAR* What)
	{
		Scene.World.Tick(1); // the end is seen within a frame
		Test.TestEqual(*FString::Printf(TEXT("%s: result Lost"), What), LureFightQA::ResultName(Scene.Fishing->GetNetState().LastResult),
			LureFightQA::ResultName(ELureFishingResult::Lost));
		Test.TestEqual(*FString::Printf(TEXT("%s: no landed event"), What), Scene.Landed, 0);
		Test.TestNull(*FString::Printf(TEXT("%s: no fight fish for the player"), What), Scene.Visuals->FindFish(Scene.Fishing));
		const float EscapeTime = Scene.Visuals->GetVisualRow().EscapeTime;
		Test.TestTrue(*FString::Printf(TEXT("%s: the fish is removed within EscapeTime"), What),
			Scene.World.TickUntil([&Weak]() { return !Weak.IsValid(); }, FMath::CeilToInt((EscapeTime + 0.25f) / Dt)));
		Scene.World.Tick(2);
		Test.TestEqual(*FString::Printf(TEXT("%s: no fish left (subsystem)"), What), Scene.Visuals->GetNumFish(), 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: no fish actor left"), What), CountFishActors(Scene.World.World), 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: still no landed event"), What), Scene.Landed, 0);
	}

	/** Falling in mid-fight (T-026 swimming) loses the fish: the visual lets it swim away and removes it. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQASwimCut, "Project.FishVisual.QA.Lifecycle.SwimmingCutsTheFight", Flags)
	bool FFishVisualQASwimCut::RunTest(const FString& Parameters)
	{
		FRealScene Scene;
		if (!Scene.Init(*this) || !TestNotNull(TEXT("water volume"), AddWaterVolume(Scene.World.World, 800.f)))
		{
			return false;
		}
		Scene.Fishing->bRodEquipped = true;
		Scene.World.Tick(10);
		if (!LureFightQA::CastAndWait(*this, Scene.World, Scene.Fishing) || !TestTrue(TEXT("hooked"), Scene.Fishing->AuthorityHookFish(Scene.Bonefish)))
		{
			return false;
		}
		Scene.World.Tick(20);
		TWeakObjectPtr<ALureFightFish> Weak = Scene.Visuals->FindFish(Scene.Fishing);
		if (!TestTrue(TEXT("a fish fights on the line"), Weak.IsValid()))
		{
			return false;
		}
		Scene.Character->SetActorLocation(FVector(1000.f, 0.f, -20.f), false, nullptr, ETeleportType::TeleportPhysics);
		bool bSwam = false;
		for (int32 Frame = 0; Frame < 120 && Scene.Fishing->GetFishingState() != ELureFishingState::Idle; ++Frame)
		{
			Scene.World.Tick(1);
			bSwam |= Scene.Character->IsSwimming();
		}
		if (!TestTrue(TEXT("the character swims and the line is in"), bSwam && Scene.Fishing->GetFishingState() == ELureFishingState::Idle))
		{
			return false;
		}
		ExpectEscapedAndGone(*this, Scene, Weak, TEXT("swimming"));
		return true;
	}

	/** A ledge pull-up mid-fight (T-026 climbing) loses the fish: the visual lets it swim away and removes it. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAClimbCut, "Project.FishVisual.QA.Lifecycle.ClimbingCutsTheFight", Flags)
	bool FFishVisualQAClimbCut::RunTest(const FString& Parameters)
	{
		FRealScene Scene;
		if (!Scene.Init(*this))
		{
			return false;
		}
		Scene.Fishing->bRodEquipped = true;
		Scene.World.Tick(10);
		if (!LureFightQA::CastAndWait(*this, Scene.World, Scene.Fishing) || !TestTrue(TEXT("hooked"), Scene.Fishing->AuthorityHookFish(Scene.Bonefish)))
		{
			return false;
		}
		Scene.World.Tick(20);
		TWeakObjectPtr<ALureFightFish> Weak = Scene.Visuals->FindFish(Scene.Fishing);
		if (!TestTrue(TEXT("a fish fights on the line"), Weak.IsValid()))
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Scene.Character->GetLureMovement();
		const float Radius = Scene.Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = Scene.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Scene.Character->SetActorLocation(FVector(-LureFightQA::DockEdgeX - Radius - 1.f, 0.f, LureFightQA::DockTop - 20.f + HalfHeight), false, nullptr,
			ETeleportType::TeleportPhysics);
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = FVector(0.f, 0.f, -20.f);
		Scene.Character->JumpCurrentCount = 1;
		bool bClimbSeen = false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Scene.Character->AddMovementInput(FVector::ForwardVector, 1.f, /*bForce*/ true);
			Scene.World.Tick(1);
			bClimbSeen |= Movement->IsLedgeClimbing();
			if (bClimbSeen && Movement->IsMovingOnGround())
			{
				break;
			}
		}
		if (!TestTrue(TEXT("the pull-up ran and the line is in"), bClimbSeen && Scene.Fishing->GetFishingState() == ELureFishingState::Idle))
		{
			return false;
		}
		ExpectEscapedAndGone(*this, Scene, Weak, TEXT("climbing"));
		return true;
	}

	/** 18 real fights (every ending three times): no fish actor, subsystem entry or other actor is left behind. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQANoLeaks, "Project.FishVisual.QA.Lifecycle.NoLeaksAfterManyFights", Flags)
	bool FFishVisualQANoLeaks::RunTest(const FString& Parameters)
	{
		FRealScene Scene;
		if (!Scene.Init(*this))
		{
			return false;
		}
		int32 ActorsAfterFirstRound = INDEX_NONE;
		for (int32 Round = 0; Round < 3; ++Round)
		{
			for (const EOutcomeCase Case : { EOutcomeCase::Landed, EOutcomeCase::Snapped, EOutcomeCase::Spooled, EOutcomeCase::ThrewHook, EOutcomeCase::ReeledIn,
				EOutcomeCase::Cancelled })
			{
				if (!Scene.RunOutcome(*this, Case, Round))
				{
					return false;
				}
			}
			Scene.World.Tick(30);
			const int32 Actors = CountAllActors(Scene.World.World);
			if (ActorsAfterFirstRound == INDEX_NONE)
			{
				ActorsAfterFirstRound = Actors;
			}
			else
			{
				TestEqual(FString::Printf(TEXT("round %d: the world's actor count does not grow"), Round), Actors, ActorsAfterFirstRound);
			}
		}
		TestEqual(TEXT("one landed event per landed fight (3)"), Scene.Landed, 3);
		TestEqual(TEXT("no fish left (subsystem)"), Scene.Visuals->GetNumFish(), 0);
		TestEqual(TEXT("no fish actor left"), CountFishActors(Scene.World.World), 0);
		return true;
	}

	/**
	 *  300 replicated fights on a client copy, with the fight id wrapping past 255: each new id is one new fish; a new id
	 *  while the old fight still shows (its end never seen) lets the old fish swim away (no landed event); the same id again
	 *  after an end is a new fish. Nothing is left at the end.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAFightIds, "Project.FishVisual.QA.Lifecycle.FightIdsWrapAndRestart", Flags)
	bool FFishVisualQAFightIds::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		FPuppet Puppet;
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !Puppet.Create(*this, World, LureFightQA::StandAt()))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());
		const float EscapeTime = Visuals->GetVisualRow().EscapeTime;
		int32 Landed = 0;
		Visuals->OnFightFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, ALureFightFish*, const FFishInstance&) { ++Landed; });

		int32 ExpectedLanded = 0;
		bool bSpawnOk = true;
		bool bIdOk = true;
		bool bEndOk = true;
		bool bRestartOk = true;
		FString FirstProblem;
		for (int32 Fight = 0; Fight < 300; ++Fight)
		{
			const uint8 Id = static_cast<uint8>(250 + Fight); // 250..255, 0, 1, ... (wraps)
			Puppet.Start(Id, MakeRecord(TEXT("Bonefish"), 1.f + 0.01f * (Fight % 100)));
			World.Tick(2);
			ALureFightFish* Actor = Visuals->FindFish(Puppet.Fishing);
			if (!Actor || CountFishActors(World.World) != 1 || Visuals->GetNumFish() != 1)
			{
				bSpawnOk = false;
				FirstProblem = FirstProblem.IsEmpty() ? FString::Printf(TEXT("fight %d (id %d): %d fish actors, NumFish %d"), Fight, Id, CountFishActors(World.World), Visuals->GetNumFish()) : FirstProblem;
				continue;
			}
			bIdOk &= Actor->GetFightId() == Id;
			TWeakObjectPtr<ALureFightFish> Weak = Actor;
			switch (Fight % 4)
			{
			case 0: // landed
				Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
				++ExpectedLanded;
				World.Tick(1);
				bEndOk &= !Weak.IsValid();
				break;
			case 1: // snapped
				Puppet.End(ELureFightOutcome::Snapped, ELureFishingResult::Snapped);
				World.TickUntil([&Weak]() { return !Weak.IsValid(); }, FMath::CeilToInt((EscapeTime + 0.25f) / Dt));
				bEndOk &= !Weak.IsValid();
				break;
			case 2: // the same id again after the end (a new fish)
			{
				Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
				++ExpectedLanded;
				World.Tick(1);
				Puppet.Start(Id, MakeRecord(TEXT("CoralSnapper"), 2.5f));
				World.Tick(2);
				ALureFightFish* Again = Visuals->FindFish(Puppet.Fishing);
				bRestartOk &= Again != nullptr && Again != Weak.Get() && Again->GetFish().SpeciesId == FName(TEXT("CoralSnapper")) && CountFishActors(World.World) == 1;
				Weak = Again;
				Puppet.End(ELureFightOutcome::ThrewHook, ELureFishingResult::ThrewHook);
				World.TickUntil([&Weak]() { return !Weak.IsValid(); }, FMath::CeilToInt((EscapeTime + 0.25f) / Dt));
				bEndOk &= !Weak.IsValid();
				break;
			}
			default: // a new fight id while this one still shows: the old fish swims away, the new one appears
			{
				Puppet.Start(static_cast<uint8>(Id + 100), MakeRecord(TEXT("CoralSnapper"), 3.f));
				World.Tick(1);
				ALureFightFish* Next = Visuals->FindFish(Puppet.Fishing);
				bRestartOk &= Next != nullptr && Next != Weak.Get() && Next->GetFightId() == static_cast<uint8>(Id + 100);
				bRestartOk &= Weak.IsValid() && Weak->GetPhase() == EFightFishPhase::Escaping && Visuals->GetNumFish() == 2;
				TWeakObjectPtr<ALureFightFish> WeakNext = Next;
				Puppet.End(ELureFightOutcome::Spooled, ELureFishingResult::Snapped);
				World.TickUntil([&Weak, &WeakNext]() { return !Weak.IsValid() && !WeakNext.IsValid(); }, FMath::CeilToInt((EscapeTime + 0.25f) / Dt));
				bEndOk &= !Weak.IsValid() && !WeakNext.IsValid();
				break;
			}
			}
			if (CountFishActors(World.World) != 0 || Visuals->GetNumFish() != 0)
			{
				bEndOk = false;
				FirstProblem = FirstProblem.IsEmpty() ? FString::Printf(TEXT("after fight %d (id %d): %d fish actors, NumFish %d"), Fight, Id, CountFishActors(World.World), Visuals->GetNumFish()) : FirstProblem;
				World.Tick(FMath::CeilToInt((EscapeTime + 0.25f) / Dt));
			}
		}
		TestTrue(FString::Printf(TEXT("every new fight id spawns exactly one fish (%s)"), *FirstProblem), bSpawnOk);
		TestTrue(TEXT("each fish carries its fight id (also across the wrap)"), bIdOk);
		TestTrue(FString::Printf(TEXT("every ending removes the fish (%s)"), *FirstProblem), bEndOk);
		TestTrue(TEXT("a new id mid-fight swaps the fish; the same id after an end is a new fish"), bRestartOk);
		TestEqual(TEXT("one landed event per landed fight, none for the rest"), Landed, ExpectedLanded);
		TestEqual(TEXT("no fish actor left"), CountFishActors(World.World), 0);
		return true;
	}

	/** A player who leaves (pawn destroyed) mid-fight takes the fish with it: nothing is left, no landed event. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAOwnerLeaves, "Project.FishVisual.QA.Lifecycle.PlayerLeavesMidFight", Flags)
	bool FFishVisualQAOwnerLeaves::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		FPuppet Puppet;
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !Puppet.Create(*this, World, LureFightQA::StandAt()))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());
		int32 Landed = 0;
		Visuals->OnFightFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, ALureFightFish*, const FFishInstance&) { ++Landed; });
		Puppet.Start(9, MakeRecord(TEXT("Bonefish"), 2.f));
		World.Tick(10);
		TWeakObjectPtr<ALureFightFish> Weak = Visuals->FindFish(Puppet.Fishing);
		if (!TestTrue(TEXT("a fish fights"), Weak.IsValid()))
		{
			return false;
		}
		Puppet.Character->SetRole(ROLE_Authority);
		Puppet.Character->Destroy();
		Puppet.Character = nullptr;
		World.Tick(2);
		TestFalse(TEXT("the fish is gone with its player"), Weak.IsValid());
		TestEqual(TEXT("no fish left (subsystem)"), Visuals->GetNumFish(), 0);
		TestEqual(TEXT("no fish actor left"), CountFishActors(World.World), 0);
		TestEqual(TEXT("no landed event"), Landed, 0);
		return true;
	}

	// =================================================================================================================
	// One visual per fight per machine
	// =================================================================================================================

	/** Two players fight at once on one machine: two fish, each on its own line, landing one leaves the other alone. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQATwoPlayers, "Project.FishVisual.QA.Multiplayer.TwoPlayersFightAtOnce", Flags)
	bool FFishVisualQATwoPlayers::RunTest(const FString& Parameters)
	{
		FRealScene Scene;
		if (!Scene.Init(*this))
		{
			return false;
		}
		FFishInstance Snapper;
		if (!LureFightQA::RollFish(*this, Scene.Fish, TEXT("CoralSnapper"), TEXT("Common"), 0.5f, 72, Snapper))
		{
			return false;
		}
		ALurePlayerCharacter* Second = Scene.World.Spawn(LureFightQA::StandAt() + FVector(0.f, -250.f, 0.f));
		ULureFishingComponent* B = LureFightQA::SetUpFishing(Second, Scene.Fish, Scene.Gear.Get(), Scene.Data.Patterns.Get(), Scene.Data.Fight.Get(), 12.f, 777);
		ULureFishingComponent* A = Scene.Fishing;
		if (!TestNotNull(TEXT("second player"), B))
		{
			return false;
		}
		if (!LureFightQA::CastAndWait(*this, Scene.World, A) || !TestTrue(TEXT("A hooks a Bonefish"), A->AuthorityHookFish(Scene.Bonefish))
			|| !LureFightQA::CastAndWait(*this, Scene.World, B) || !TestTrue(TEXT("B hooks a Coral Snapper"), B->AuthorityHookFish(Snapper)))
		{
			return false;
		}
		bool bTwo = true;
		bool bOwnLines = true;
		bool bStable = true;
		ALureFightFish* FishA = nullptr;
		ALureFightFish* FishB = nullptr;
		for (int32 Frame = 0; Frame < 120 && A->GetFishingState() == ELureFishingState::Hooked && B->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			Scene.World.Tick(1);
			ALureFightFish* NowA = Scene.Visuals->FindFish(A);
			ALureFightFish* NowB = Scene.Visuals->FindFish(B);
			bTwo &= CountFishActors(Scene.World.World) == 2 && Scene.Visuals->GetNumFish() == 2 && NowA && NowB && NowA != NowB;
			if (Frame == 0)
			{
				FishA = NowA;
				FishB = NowB;
			}
			bStable &= NowA == FishA && NowB == FishB;
			if (NowA && NowB)
			{
				const FFightFishView ViewA = FFightFishViewAdapter::FromComponent(*A);
				const FFightFishView ViewB = FFightFishViewAdapter::FromComponent(*B);
				bOwnLines &= FVector::Dist2D(NowA->GetLastTarget(), ViewA.LineEnd) <= NowA->GetMouthOffsetCm() * NowA->GetFishScale() + 10.f;
				bOwnLines &= FVector::Dist2D(NowB->GetLastTarget(), ViewB.LineEnd) <= NowB->GetMouthOffsetCm() * NowB->GetFishScale() + 10.f;
			}
		}
		TestTrue(TEXT("two fights: exactly two fish, one per player"), bTwo);
		TestTrue(TEXT("each fish stays the same actor"), bStable);
		TestTrue(TEXT("each fish follows its own line end"), bOwnLines);
		if (!TestTrue(TEXT("both fish found"), FishA && FishB))
		{
			return false;
		}
		TestTrue(TEXT("A's fish is A's record"), LureFightQA::SameFish(FishA->GetFish(), Scene.Bonefish));
		TestTrue(TEXT("B's fish is B's record"), LureFightQA::SameFish(FishB->GetFish(), Snapper));

		// A lands; B keeps fighting with the same fish.
		TWeakObjectPtr<ALureFightFish> WeakB = FishB;
		A->AuthoritySetReeling(true);
		B->AuthoritySetReeling(false);
		bool bBUntouched = true;
		for (int32 Frame = 0; Frame < 60 * 60 && A->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			Scene.World.Tick(1);
			if (B->GetFishingState() == ELureFishingState::Hooked)
			{
				bBUntouched &= Scene.Visuals->FindFish(B) == WeakB.Get() && WeakB.IsValid();
			}
		}
		Scene.World.Tick(1);
		TestEqual(TEXT("A landed"), LureFightQA::ResultName(A->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed));
		TestEqual(TEXT("one landed event"), Scene.Landed, 1);
		TestTrue(TEXT("... from A"), Scene.LandedBy.Num() == 1 && Scene.LandedBy[0] == A);
		TestTrue(TEXT("B's fish was not touched by A's landing"), bBUntouched);
		ClearHangingCatch(A->GetOwner()); // T-030: A's landed fish (with its fight fish as the look) comes off the hook
		if (B->GetFishingState() == ELureFishingState::Hooked)
		{
			TestEqual(TEXT("only B's fish is left"), Scene.Visuals->GetNumFish(), 1);
			B->AuthorityReelIn();
			Scene.World.TickUntil([&WeakB]() { return !WeakB.IsValid(); }, FMath::CeilToInt((Scene.Visuals->GetVisualRow().EscapeTime + 0.25f) / Dt));
		}
		else
		{
			AddWarning(TEXT("B's fight ended on its own before A landed (the two-at-once part still ran)"));
			Scene.World.Tick(FMath::CeilToInt((Scene.Visuals->GetVisualRow().EscapeTime + 0.25f) / Dt));
		}
		Scene.World.Tick(2);
		TestEqual(TEXT("nothing left"), CountFishActors(Scene.World.World), 0);
		TestEqual(TEXT("still one landed event"), Scene.Landed, 1);
		return true;
	}

	/**
	 *  Two machines (a server world and a client world with a copy of the player fed by replication): each machine has
	 *  exactly one fish for the fight, the same record and size, and each fires its own landed event once.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAPerMachine, "Project.FishVisual.QA.Multiplayer.OneFishPerMachine", Flags)
	bool FFishVisualQAPerMachine::RunTest(const FString& Parameters)
	{
		FRealScene Server;
		if (!Server.Init(*this))
		{
			return false;
		}
		LureFightQA::FWorld ClientWorld;
		if (!ClientWorld.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* ClientVisuals = ClientWorld.World->GetSubsystem<ULureFightFishSubsystem>();
		ALurePlayerCharacter* Copy = ClientWorld.Spawn(LureFightQA::StandAt());
		ULureFishingComponent* Client = LureFightQA::SetUpFishing(Copy, Server.Fish, Server.Gear.Get(), Server.Data.Patterns.Get(), Server.Data.Fight.Get());
		if (!TestNotNull(TEXT("client subsystem"), ClientVisuals) || !TestNotNull(TEXT("client copy"), Client))
		{
			return false;
		}
		Copy->GetCharacterMovement()->SetComponentTickEnabled(false);
		Copy->SetRole(ROLE_SimulatedProxy);
		ClientVisuals->SetTables(Server.Fish.Species.Get(), Server.Visual.Get());
		int32 ClientLanded = 0;
		FFishInstance ClientRecord;
		ClientVisuals->OnFightFishLandedNative.AddLambda([&ClientLanded, &ClientRecord, Client](ULureFishingComponent* By, ALureFightFish*, const FFishInstance& Record)
		{
			ClientLanded += By == Client ? 1 : 0;
			ClientRecord = Record;
		});

		if (!LureFightQA::CastAndWait(*this, Server.World, Server.Fishing) || !TestTrue(TEXT("server hooks"), Server.Fishing->AuthorityHookFish(Server.Bonefish)))
		{
			Copy->SetRole(ROLE_Authority);
			return false;
		}
		Server.Fishing->AuthoritySetReeling(true);
		bool bServerOne = true;
		bool bClientOne = true;
		bool bSame = true;
		int32 FightFrames = 0;
		for (int32 Frame = 0; Frame < 60 * 60 && Server.Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			Server.World.Tick(1);
			LureFightQA::ReplicateFishing(*this, Server.Fishing, Client);
			ClientWorld.Tick(1);
			if (Server.Fishing->GetFishingState() != ELureFishingState::Hooked)
			{
				break;
			}
			++FightFrames;
			bServerOne &= CountFishActors(Server.World.World) == 1 && Server.Visuals->GetNumFish() == 1;
			bClientOne &= CountFishActors(ClientWorld.World) == 1 && ClientVisuals->GetNumFish() == 1;
			const ALureFightFish* S = Server.Visuals->FindFish(Server.Fishing);
			const ALureFightFish* C = ClientVisuals->FindFish(Client);
			bSame &= S && C && LureFightQA::SameFish(S->GetFish(), C->GetFish()) && S->GetFishScale() == C->GetFishScale() && S->GetFightId() == C->GetFightId();
		}
		TestTrue(TEXT("the fight ran"), FightFrames > 30);
		TestTrue(TEXT("server machine: exactly one fish during the fight"), bServerOne);
		TestTrue(TEXT("client machine: exactly one fish during the fight"), bClientOne);
		TestTrue(TEXT("both machines show the same fish record, size and fight id"), bSame);
		LureFightQA::ReplicateFishing(*this, Server.Fishing, Client);
		Server.World.Tick(2);
		ClientWorld.Tick(2);
		TestEqual(TEXT("server: landed event once"), Server.Landed, 1);
		TestEqual(TEXT("client: landed event once"), ClientLanded, 1);
		TestTrue(TEXT("client: the landed record is the hooked fish"), LureFightQA::SameFish(ClientRecord, Server.Bonefish));
		ClearHangingCatch(Server.Character); // T-030: the landed fish (with its fight fish as the look) comes off the hook
		TestEqual(TEXT("server: no fish left"), CountFishActors(Server.World.World), 0);
		TestEqual(TEXT("client: no fish left"), CountFishActors(ClientWorld.World), 0);
		Copy->SetRole(ROLE_Authority);
		return true;
	}

	// =================================================================================================================
	// Dedicated server: no fish
	// =================================================================================================================

	/** The subsystem exists only in game worlds (not editor worlds). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAWorldTypes, "Project.FishVisual.QA.Net.OnlyInGameWorlds", Flags)
	bool FFishVisualQAWorldTypes::RunTest(const FString& Parameters)
	{
		{
			FTestWorldWrapper Game;
			if (!Game.CreateTestWorld(EWorldType::Game))
			{
				Game.ForwardErrorMessages(this);
				return false;
			}
			TestNotNull(TEXT("a game world has the fight fish subsystem"), Game.GetTestWorld()->GetSubsystem<ULureFightFishSubsystem>());
			TestTrue(TEXT("this process is not a dedicated server: ShouldCreateSubsystem is true for a game world"),
				IsRunningDedicatedServer() || GetDefault<ULureFightFishSubsystem>()->ShouldCreateSubsystem(Game.GetTestWorld()));
		}
		{
			FTestWorldWrapper Editor;
			if (!Editor.CreateTestWorld(EWorldType::Editor))
			{
				Editor.ForwardErrorMessages(this);
				return false;
			}
			TestNull(TEXT("an editor world has none"), Editor.GetTestWorld()->GetSubsystem<ULureFightFishSubsystem>());
		}
		{
			FTestWorldWrapper Preview;
			if (!Preview.CreateTestWorld(EWorldType::EditorPreview))
			{
				Preview.ForwardErrorMessages(this);
				return false;
			}
			TestNull(TEXT("an editor preview world has none"), Preview.GetTestWorld()->GetSubsystem<ULureFightFishSubsystem>());
		}
		return true;
	}

	/**
	 *  A world running as a dedicated server (the PIE "play as dedicated server" net mode, the only one an editor test can
	 *  make): the fight runs, but no fish is spawned and no landed event fires.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQADedicated, "Project.FishVisual.QA.Net.NoFishOnDedicatedServer", Flags)
	bool FFishVisualQADedicated::RunTest(const FString& Parameters)
	{
#if WITH_EDITOR
		FRealScene Scene;
		if (!Scene.Init(*this))
		{
			return false;
		}
		UWorld* World = Scene.World.World;
		const EWorldType::Type OldType = World->WorldType;
		World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
		World->WorldType = EWorldType::PIE;
		ON_SCOPE_EXIT
		{
			World->SetPlayInEditorInitialNetMode(NM_Standalone);
			World->WorldType = OldType;
		};
		if (!TestTrue(TEXT("the world runs as a dedicated server"), World->GetNetMode() == NM_DedicatedServer))
		{
			return false;
		}
		if (!LureFightQA::CastAndWait(*this, Scene.World, Scene.Fishing) || !TestTrue(TEXT("the server hooks"), Scene.Fishing->AuthorityHookFish(Scene.Bonefish)))
		{
			return false;
		}
		Scene.Fishing->AuthoritySetReeling(true);
		bool bNoFish = true;
		int32 FightFrames = 0;
		for (int32 Frame = 0; Frame < 60 * 60 && Scene.Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			Scene.World.Tick(1);
			FightFrames += Scene.Fishing->GetFightNet().bActive ? 1 : 0;
			bNoFish &= CountFishActors(World) == 0 && Scene.Visuals->GetNumFish() == 0 && Scene.Visuals->FindFish(Scene.Fishing) == nullptr;
		}
		Scene.World.Tick(2);
		TestTrue(TEXT("the server still ran the fight"), FightFrames > 10);
		TestEqual(TEXT("the fight ended landed"), LureFightQA::ResultName(Scene.Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed));
		TestTrue(TEXT("no fish on a dedicated server, ever"), bNoFish);
		TestEqual(TEXT("no landed event on a dedicated server"), Scene.Landed, 0);
#else
		AddInfo(TEXT("needs an editor build"));
#endif
		return true;
	}

	// =================================================================================================================
	// The landed hand-off (T-030 seam)
	// =================================================================================================================

	/**
	 *  OnFightFishLanded contract on a client copy: fires once per landed fight with (the player's fishing, a fish at the
	 *  line end playing Flop at alpha 1, the hooked record); KeepLandedFish only works for that fish during the event;
	 *  not kept = destroyed right after; kept = the listener's (not counted, never moved again); a listener may destroy it.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQALanded, "Project.FishVisual.QA.Landed.HandOffContract", Flags)
	bool FFishVisualQALanded::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		FPuppet Puppet;
		FPuppet Other;
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !Puppet.Create(*this, World, LureFightQA::StandAt())
			|| !Other.Create(*this, World, LureFightQA::StandAt() + FVector(0.f, -250.f, 0.f)))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());

		enum class EListener : uint8 { Nothing, Keep, KeepTwice, DestroyIt };
		EListener Mode = EListener::Nothing;
		int32 Events = 0;
		bool bArgsOk = true;
		bool bKeepOthersRefused = true;
		bool bKeepResult = false;
		FFishInstance Record;
		FVector LineEndAtEvent = FVector::ZeroVector;
		float DistanceToLineEnd = -1.f;
		TWeakObjectPtr<ALureFightFish> EventFish;
		ALureFightFish* OtherFish = nullptr;
		Visuals->OnFightFishLandedNative.AddLambda([&](ULureFishingComponent* By, ALureFightFish* Actor, const FFishInstance& Landed)
		{
			++Events;
			EventFish = Actor;
			Record = Landed;
			bArgsOk &= By == Puppet.Fishing && Actor != nullptr;
			if (!Actor)
			{
				return;
			}
			bArgsOk &= Actor->GetPhase() == EFightFishPhase::Landed && Actor->GetAnimState().Role == EFishAnimRole::Flop && Actor->GetAnimState().Amplitude == 1.f;
			DistanceToLineEnd = static_cast<float>(FVector::Dist2D(Actor->GetMouthLocation(), LineEndAtEvent));
			bKeepOthersRefused &= !Visuals->KeepLandedFish(nullptr);
			bKeepOthersRefused &= OtherFish == nullptr || !Visuals->KeepLandedFish(OtherFish);
			switch (Mode)
			{
			case EListener::Keep:
				bKeepResult = Visuals->KeepLandedFish(Actor);
				break;
			case EListener::KeepTwice:
				bKeepResult = Visuals->KeepLandedFish(Actor) && Visuals->KeepLandedFish(Actor);
				break;
			case EListener::DestroyIt:
				Actor->Destroy();
				break;
			default:
				break;
			}
		});
		// A second listener that only watches (two listeners must both see the event).
		int32 SecondListener = 0;
		Visuals->OnFightFishLandedNative.AddLambda([&SecondListener](ULureFishingComponent*, ALureFightFish*, const FFishInstance&) { ++SecondListener; });

		// The other player fights all along (its fish must never be keepable by this event).
		Other.Start(200, MakeRecord(TEXT("CoralSnapper"), 3.f), TEXT("Rest"));
		uint8 Id = 1;
		auto LandOnce = [&](EListener InMode, const FFishInstance& Hooked) -> TWeakObjectPtr<ALureFightFish>
		{
			Mode = InMode;
			Puppet.Start(Id++, Hooked, TEXT("Rest"));
			World.Tick(120); // settled at the line end
			OtherFish = Visuals->FindFish(Other.Fishing);
			LineEndAtEvent = Puppet.LineEnd();
			Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
			World.Tick(1);
			return EventFish;
		};

		// Not kept: destroyed right after the event.
		const FFishInstance First = MakeRecord(TEXT("Bonefish"), 2.25f);
		TWeakObjectPtr<ALureFightFish> Landed1 = LandOnce(EListener::Nothing, First);
		TestEqual(TEXT("not kept: one event"), Events, 1);
		TestEqual(TEXT("not kept: every listener got it"), SecondListener, 1);
		TestTrue(TEXT("not kept: the arguments (this player's fishing; the fish in its Landed phase playing Flop at alpha 1)"), bArgsOk);
		TestTrue(TEXT("not kept: the record is the hooked fish"), LureFightQA::SameFish(Record, First));
		TestTrue(FString::Printf(TEXT("not kept: GetMouthLocation is at the line end (%.1f cm off)"), DistanceToLineEnd), DistanceToLineEnd >= 0.f && DistanceToLineEnd < 5.f);
		TestFalse(TEXT("not kept: destroyed right after"), Landed1.IsValid());
		TestTrue(TEXT("KeepLandedFish refuses null and another player's fish during the event"), bKeepOthersRefused);
		World.Tick(60);
		TestEqual(TEXT("the landed state stays, the event does not repeat"), Events, 1);
		TestEqual(TEXT("only the other player's fish remains"), Visuals->GetNumFish(), 1);

		// Kept: the listener's fish (not destroyed, not counted, not moved, still flopping).
		TWeakObjectPtr<ALureFightFish> Kept = LandOnce(EListener::Keep, MakeRecord(TEXT("Bonefish"), 1.f));
		TestEqual(TEXT("kept: second event"), Events, 2);
		TestTrue(TEXT("kept: KeepLandedFish(the fish) during the event"), bKeepResult);
		if (TestTrue(TEXT("kept: the fish lives on"), Kept.IsValid()))
		{
			const FVector At = Kept->GetActorLocation();
			World.Tick(60);
			TestTrue(TEXT("kept: still alive later"), Kept.IsValid());
			if (Kept.IsValid())
			{
				TestTrue(TEXT("kept: the subsystem never moves it again"), Kept->GetActorLocation().Equals(At, 0.01));
				TestTrue(TEXT("kept: still Landed, playing Flop"), Kept->GetPhase() == EFightFishPhase::Landed && Kept->GetAnimState().Role == EFishAnimRole::Flop);
				TestFalse(TEXT("kept: KeepLandedFish after the event does nothing"), Visuals->KeepLandedFish(Kept.Get()));
				TestEqual(TEXT("kept: not counted by the subsystem (only the other player's fish)"), Visuals->GetNumFish(), 1);
				Kept->Destroy();
			}
		}

		// Kept twice: harmless.
		TWeakObjectPtr<ALureFightFish> Twice = LandOnce(EListener::KeepTwice, MakeRecord(TEXT("Bonefish"), 1.f));
		TestTrue(TEXT("keep twice: both calls accepted"), bKeepResult);
		TestTrue(TEXT("keep twice: the fish lives on"), Twice.IsValid());
		if (Twice.IsValid())
		{
			Twice->Destroy();
		}

		// A listener destroys the fish itself: no crash, nothing left behind.
		TWeakObjectPtr<ALureFightFish> Destroyed = LandOnce(EListener::DestroyIt, MakeRecord(TEXT("Bonefish"), 1.f));
		World.Tick(2);
		TestFalse(TEXT("destroyed by the listener: gone"), Destroyed.IsValid());
		TestEqual(TEXT("four landed events"), Events, 4);
		TestEqual(TEXT("the second listener saw all four"), SecondListener, 4);

		// Escaped endings never fire it.
		Puppet.Start(Id++, MakeRecord(TEXT("Bonefish"), 1.f));
		World.Tick(5);
		Puppet.End(ELureFightOutcome::ThrewHook, ELureFishingResult::ThrewHook);
		World.Tick(FMath::CeilToInt((Visuals->GetVisualRow().EscapeTime + 0.25f) / Dt));
		TestEqual(TEXT("an escaped fish: no landed event"), Events, 4);

		// Landed by the line's result alone (Outcome not replicated yet) is still a landing.
		Puppet.Start(Id++, MakeRecord(TEXT("Bonefish"), 1.f));
		World.Tick(5);
		Puppet.End(ELureFightOutcome::None, ELureFishingResult::Landed);
		World.Tick(1);
		TestEqual(TEXT("LastResult Landed with no Outcome yet: landed event"), Events, 5);

		Other.End(ELureFightOutcome::Snapped, ELureFishingResult::Snapped);
		World.Tick(FMath::CeilToInt((Visuals->GetVisualRow().EscapeTime + 0.25f) / Dt));
		TestEqual(TEXT("nothing left"), CountFishActors(World.World), 0);
		return true;
	}

	/** The Blueprint side of the seam: OnFightFishLanded is assignable with (Fishing, Fish, Landed); KeepLandedFish is callable. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQALandedReflection, "Project.FishVisual.QA.Landed.BlueprintSeam", Flags)
	bool FFishVisualQALandedReflection::RunTest(const FString& Parameters)
	{
		const FMulticastDelegateProperty* Event = FindFProperty<FMulticastDelegateProperty>(ULureFightFishSubsystem::StaticClass(), TEXT("OnFightFishLanded"));
		if (!TestNotNull(TEXT("OnFightFishLanded is a reflected multicast delegate"), Event))
		{
			return false;
		}
		TestTrue(TEXT("BlueprintAssignable"), Event->HasAnyPropertyFlags(CPF_BlueprintAssignable));
		const UFunction* Signature = Event->SignatureFunction;
		if (TestNotNull(TEXT("signature"), Signature))
		{
			TArray<const FProperty*> Params;
			for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				Params.Add(*It);
			}
			if (TestEqual(TEXT("three parameters"), Params.Num(), 3))
			{
				const FObjectPropertyBase* P0 = CastField<FObjectPropertyBase>(Params[0]);
				const FObjectPropertyBase* P1 = CastField<FObjectPropertyBase>(Params[1]);
				const FStructProperty* P2 = CastField<FStructProperty>(Params[2]);
				TestTrue(TEXT("1: ULureFishingComponent* Fishing"), P0 && P0->PropertyClass == ULureFishingComponent::StaticClass() && P0->GetFName() == TEXT("Fishing"));
				TestTrue(TEXT("2: ALureFightFish* Fish"), P1 && P1->PropertyClass == ALureFightFish::StaticClass() && P1->GetFName() == TEXT("Fish"));
				TestTrue(TEXT("3: const FFishInstance& Landed"), P2 && P2->Struct == FFishInstance::StaticStruct() && P2->GetFName() == TEXT("Landed"));
			}
		}
		const UFunction* Keep = ULureFightFishSubsystem::StaticClass()->FindFunctionByName(TEXT("KeepLandedFish"));
		TestTrue(TEXT("KeepLandedFish is BlueprintCallable"), Keep && Keep->HasAnyFunctionFlags(FUNC_BlueprintCallable));
		const UFunction* Mouth = ALureFightFish::StaticClass()->FindFunctionByName(TEXT("GetMouthLocation"));
		TestTrue(TEXT("ALureFightFish::GetMouthLocation is BlueprintPure (for T-030 / T-032)"), Mouth && Mouth->HasAnyFunctionFlags(FUNC_BlueprintPure));
		TestFalse(TEXT("ALureFightFish is not replicated (each machine has its own)"), GetDefault<ALureFightFish>()->GetIsReplicated());
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

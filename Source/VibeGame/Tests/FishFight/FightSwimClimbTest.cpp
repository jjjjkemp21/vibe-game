// Lure T-007 x T-026 integration (unreal-engineer, 2026-09-23): a reel fight in progress when the player starts swimming
// or climbing ends like any line out under the fishing rules: the line comes in, the fish is lost (result Lost, reason
// Swimming / Climbing), nothing lands (no OnFishLanded, no cooler). docs/specs/reel-fight-rules.md, "Swimming and climbing".
// Project.Fishing.Fight.SwimClimb.*

#include "FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureWaterVolume.h"
#include "Components/CapsuleComponent.h"

// Helpers and tests share this namespace (no file-scope using-directive: unity builds merge test files).
namespace LureFightSwimClimb
{
	FString BlockName(ELureCastBlock Block)
	{
		return StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Block));
	}

	/** Engine water (a T-026 ALureWaterVolume) with its surface at z = 0 over +-3500 cm around the dock, Depth deep. */
	ALureWaterVolume* AddWaterVolume(UWorld* World, float Depth)
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

	/** The fixture tables, a rolled Bonefish, and a world where the character stands on the dock with a fish fighting on the line. */
	struct FFightScene
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		FFishInstance Bonefish;
		LureFightQA::FWorld World;
		ALurePlayerCharacter* Character = nullptr;
		ULureFishingComponent* Fishing = nullptr;
		int32 Landed = 0;

		bool StartFight(FAutomationTestBase& Test, bool bWithWaterVolume)
		{
			if (!Data.Load(Test) || !FishQA::LoadReal(Test, Fish)
				|| !LureFightQA::RollFish(Test, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish) || !World.Create(Test))
			{
				return false;
			}
			if (bWithWaterVolume && !Test.TestNotNull(TEXT("water volume"), AddWaterVolume(World.World, 800.f)))
			{
				return false;
			}
			Character = World.Spawn(LureFightQA::StandAt());
			Fishing = LureFightQA::SetUpFishing(Character, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
			if (!Test.TestNotNull(TEXT("character with fishing"), Fishing))
			{
				return false;
			}
			Fishing->bRodEquipped = true;
			Fishing->OnFishLandedNative.AddLambda([this](ULureFishingComponent*, const FFishInstance&) { ++Landed; });
			World.Tick(10);
			if (!LureFightQA::CastAndWait(Test, World, Fishing) || !Test.TestTrue(TEXT("hooked"), Fishing->AuthorityHookFish(Bonefish)))
			{
				return false;
			}
			Fishing->AuthoritySetReeling(false);
			World.Tick(20);
			return Test.TestTrue(TEXT("the fight is on"), Fishing->GetFishingState() == ELureFishingState::Hooked && Fishing->GetFightNet().bActive);
		}

		/** The fight ended as a lost fish for Reason: line in, nothing landed, nothing left running. */
		void ExpectLost(FAutomationTestBase& Test, ELureCastBlock Reason) const
		{
			const FLureFishingNetState& Net = Fishing->GetNetState();
			Test.TestEqual(TEXT("state Idle (the line is in)"), LureFightQA::StateName(Fishing->GetFishingState()), LureFightQA::StateName(ELureFishingState::Idle));
			Test.TestEqual(TEXT("result Lost"), LureFightQA::ResultName(Net.LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
			Test.TestEqual(TEXT("reason"), BlockName(Net.ResultReason), BlockName(Reason));
			Test.TestFalse(TEXT("no fight left running"), Fishing->GetFightNet().bActive);
			Test.TestFalse(TEXT("no fish on the line"), Fishing->GetHookedFish().IsValid());
			Test.TestFalse(TEXT("nothing landed"), Fishing->GetLastLandedFish().IsValid());
			Test.TestFalse(TEXT("the reel input is reset"), Fishing->IsServerReeling());
			Test.TestEqual(TEXT("OnFishLanded never fired"), Landed, 0);
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightSwimLosesFish, "Project.Fishing.Fight.SwimClimb.FallingInLosesTheFish", LureFightQA::Flags)
	bool FLureFightSwimLosesFish::RunTest(const FString& Parameters)
	{
		FFightScene Scene;
		if (!Scene.StartFight(*this, /*bWithWaterVolume*/ true))
		{
			return false;
		}
		// Off the dock into the water mid-fight (the capsule center under the surface).
		Scene.Character->SetActorLocation(FVector(1000.f, 0.f, -20.f), false, nullptr, ETeleportType::TeleportPhysics);
		bool bSwam = false;
		for (int32 Frame = 0; Frame < 120 && Scene.Fishing->GetFishingState() != ELureFishingState::Idle; ++Frame)
		{
			Scene.World.Tick(1);
			bSwam |= Scene.Character->IsSwimming();
		}
		if (!TestTrue(TEXT("the character swims"), bSwam || Scene.Character->IsSwimming()))
		{
			return false;
		}
		Scene.ExpectLost(*this, ELureCastBlock::Swimming);
		Scene.World.Tick(30);
		TestFalse(TEXT("the line stays in while swimming"), Scene.Fishing->IsLineOut());
		TestEqual(TEXT("still nothing landed"), Scene.Landed, 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightClimbLosesFish, "Project.Fishing.Fight.SwimClimb.LedgePullUpLosesTheFish", LureFightQA::Flags)
	bool FLureFightClimbLosesFish::RunTest(const FString& Parameters)
	{
		FFightScene Scene;
		if (!Scene.StartFight(*this, /*bWithWaterVolume*/ false))
		{
			return false;
		}
		ULureCharacterMovementComponent* Movement = Scene.Character->GetLureMovement();
		const float Radius = Scene.Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = Scene.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		// In the air against the dock's back face (x = -DockEdgeX) after a jump, on the way down, facing it
		// (as Project.Fishing.Climb.LedgePullUpReelsTheLineIn). The fight's line rules ignore the bobber distance.
		Scene.Character->SetActorLocation(FVector(-LureFightQA::DockEdgeX - Radius - 1.f, 0.f, LureFightQA::DockTop - 20.f + HalfHeight), false, nullptr,
			ETeleportType::TeleportPhysics);
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = FVector(0.f, 0.f, -20.f);
		Scene.Character->JumpCurrentCount = 1;
		TestTrue(TEXT("in the air after the jump: still fighting"), Scene.Fishing->GetFightNet().bActive);

		bool bClimbSeen = false;
		bool bLostDuringClimb = false;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Scene.Character->AddMovementInput(FVector::ForwardVector, 1.f, /*bForce*/ true);
			Scene.World.Tick(1);
			if (Movement->IsLedgeClimbing())
			{
				bClimbSeen = true;
				bLostDuringClimb |= Scene.Fishing->GetFishingState() == ELureFishingState::Idle;
			}
			if (Movement->IsMovingOnGround())
			{
				break;
			}
		}
		if (!TestTrue(TEXT("the pull-up ran"), bClimbSeen))
		{
			return false;
		}
		TestTrue(TEXT("the fish was lost while climbing"), bLostDuringClimb);
		Scene.ExpectLost(*this, ELureCastBlock::Climbing);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

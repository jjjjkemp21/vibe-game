// Lure T-026 x T-006 merge: fishing while swimming and climbing (review D1), the swimming arms pose, and the stance hint
// on the placeholder HUD. Specs: docs/specs/fishing-rules.md ("No casting while", "A line out comes in"),
// docs/specs/swimming.md ("Fishing", "Networking contract"). Tables come from data/tables/DT_Movement.csv.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/FPArmsPose.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Character/LureWaterVolume.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Game/LureHUD.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GCObjectScopeGuard.h"

// Helpers and tests share this namespace (no file-scope using-directive: unity builds merge test files).
namespace LureSwimFishingTest
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Seabed of the deep pool, cm (the water surface is z = 0). */
	constexpr float SeabedZ = -600.f;

	FString BlockText(ELureCastBlock Block)
	{
		return StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Block));
	}

	FString ResultText(ELureFishingResult Result)
	{
		return StaticEnum<ELureFishingResult>()->GetNameStringByValue(static_cast<int64>(Result));
	}

	FString PoseText(EFPArmsPose Pose)
	{
		return StaticEnum<EFPArmsPose>()->GetNameStringByValue(static_cast<int64>(Pose));
	}

	/** data/tables/DT_Movement.csv as a transient table (guard it: test worlds collect garbage while it is in use). */
	UDataTable* ShippedMovementTable(FAutomationTestBase& Test)
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

	FLureMovementRow ShippedRow(const UDataTable* Table, ELureMovementState State)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Table, Rows, Problems);
		return Rows[static_cast<int32>(State)];
	}

	/** Transient game world with boxes, water volumes and the player character. */
	struct FScene
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			return World != nullptr;
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

		/** Water whose surface is at z = 0 over +-3500 cm, Depth deep. */
		ALureWaterVolume* AddWater(float Depth)
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

		/** The character with its capsule center at Center, facing +X. With bPossess a local player controller owns it (so its arms update). */
		ALurePlayerCharacter* Spawn(const FVector& Center, const UDataTable* Table, bool bPossess)
		{
			const FTransform Transform(FRotator::ZeroRotator, Center);
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character)
			{
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Table);
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			Character->FinishSpawning(Transform);
			if (bPossess)
			{
				APlayerController* Controller = World->SpawnActor<APlayerController>();
				if (!Controller)
				{
					return nullptr;
				}
				Controller->SetAsLocalPlayerController();
				Controller->Possess(Character);
			}
			return Character;
		}

		void Tick(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(Dt);
			}
		}
	};

	/** The rod in hand (whatever the project setting says) and the built-in fishing profile. */
	ULureFishingComponent* ReadyFishing(ALurePlayerCharacter* Character)
	{
		ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
		if (Fishing)
		{
			Fishing->bRodEquipped = true;
			Fishing->SetFishingProfile(FLureFishingRules::GetFallbackRow());
		}
		return Fishing;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingClimbingIsBusyRules, "Project.Fishing.Rules.ClimbingIsBusy", LureSwimFishingTest::Flags)

bool FLureFishingClimbingIsBusyRules::RunTest(const FString& Parameters)
{
	// Review D1: the T-026 climbs are MOVE_Custom, where the engine's IsSwimming/IsFalling are both false.
	const FLureMovementRow Stand = FLureMovementData::GetFallbackRow(ELureMovementState::Stand);
	const FLureFishingRow Profile = FLureFishingRules::GetFallbackRow();

	FLureCastConditions PullUp;
	PullUp.bClimbing = true;
	TestEqual(TEXT("a pull-up onto a ledge: no cast"), BlockText(FLureFishingRules::GetCastBlock(PullUp, Stand)), BlockText(ELureCastBlock::Climbing));
	TestEqual(TEXT("a pull-up brings a line out in"), BlockText(FLureFishingRules::GetLineCancel(PullUp, Stand, Profile, 100.f)), BlockText(ELureCastBlock::Climbing));

	FLureCastConditions ClimbOut = PullUp;
	ClimbOut.bSwimming = true;
	TestEqual(TEXT("climbing out of the water counts as swimming (cast)"), BlockText(FLureFishingRules::GetCastBlock(ClimbOut, Stand)), BlockText(ELureCastBlock::Swimming));
	TestEqual(TEXT("climbing out of the water counts as swimming (line)"), BlockText(FLureFishingRules::GetLineCancel(ClimbOut, Stand, Profile, 100.f)), BlockText(ELureCastBlock::Swimming));

	FLureCastConditions LineOut = PullUp;
	LineOut.bLineOut = true;
	TestEqual(TEXT("a line already out is reported first"), BlockText(FLureFishingRules::GetCastBlock(LineOut, Stand)), BlockText(ELureCastBlock::Busy));

	FLureCastConditions Still;
	TestEqual(TEXT("standing still still casts"), BlockText(FLureFishingRules::GetCastBlock(Still, Stand)), BlockText(ELureCastBlock::None));
	FLureCastConditions Jump;
	Jump.bFalling = true;
	TestEqual(TEXT("a plain jump still keeps the line"), BlockText(FLureFishingRules::GetLineCancel(Jump, Stand, Profile, 100.f)), BlockText(ELureCastBlock::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingClimbOutTest, "Project.Fishing.Swim.ClimbOutBlocksFishingAndACastRightAfterStays", LureSwimFishingTest::Flags)

bool FLureFishingClimbOutTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedMovementTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FScene Scene;
	if (!Table || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddBox(FVector(0.f, 0.f, SeabedZ - 50.f), FVector(4000.f, 4000.f, 50.f));
	Scene.AddWater(800.f);
	// A dock 50 cm above the water (within the Swim row's 60 cm climb rule), its water-side face at x = 100.
	const float FaceX = 100.f;
	const float EdgeHeight = 50.f;
	Scene.AddBox(FVector(FaceX + 250.f, 0.f, 0.5f * (EdgeHeight + SeabedZ)), FVector(250.f, 500.f, 0.5f * (EdgeHeight - SeabedZ)));

	ALurePlayerCharacter* Character = Scene.Spawn(FVector(FaceX - 54.f, 0.f, 190.f), Table, /*bPossess*/ true);
	ULureFishingComponent* Fishing = ReadyFishing(Character);
	if (!TestNotNull(TEXT("character with fishing"), Fishing))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	Scene.Tick(180);

	// Swimming: no rod, Idle arms, no cast.
	if (!TestTrue(TEXT("fell in: swimming"), Character->IsSwimming()))
	{
		return false;
	}
	TestEqual(TEXT("swimming: cast block"), BlockText(Fishing->GetCastBlock()), BlockText(ELureCastBlock::Swimming));
	TestFalse(TEXT("swimming: the rod is not in hand"), Fishing->IsRodInHand());
	TestFalse(TEXT("swimming: the character doesn't hold the rod"), Character->IsHoldingRod());
	TestEqual(TEXT("swimming: the arms show Idle (no rod out)"), PoseText(Character->GetArmsPose()), PoseText(EFPArmsPose::Idle));
	TestFalse(TEXT("swimming: the server refuses a cast"), Fishing->AuthorityCast(0.5f, 180.f));

	// Jump climbs out; the whole climb is busy.
	Character->Jump();
	bool bClimbSeen = false;
	bool bCastAllowedDuringClimb = false;
	bool bRodDuringClimb = false;
	bool bCastAcceptedDuringClimb = false;
	bool bOnLand = false;
	for (int32 Frame = 0; Frame < 150; ++Frame)
	{
		Scene.Tick(1);
		if (Movement->IsClimbingOut())
		{
			if (!bClimbSeen)
			{
				bCastAcceptedDuringClimb = Fishing->AuthorityCast(0.5f, 180.f);
			}
			bClimbSeen = true;
			bCastAllowedDuringClimb |= Fishing->GetCastBlock() == ELureCastBlock::None;
			bRodDuringClimb |= Fishing->IsRodInHand();
		}
		if (Movement->IsMovingOnGround() && !Character->IsSwimming())
		{
			bOnLand = true;
			break;
		}
	}
	if (!TestTrue(TEXT("the climb out ran"), bClimbSeen) || !TestTrue(TEXT("standing on the dock"), bOnLand))
	{
		return false;
	}
	TestFalse(TEXT("climbing out: never allowed to cast"), bCastAllowedDuringClimb);
	TestFalse(TEXT("climbing out: the server refused a cast"), bCastAcceptedDuringClimb);
	TestFalse(TEXT("climbing out: the rod stays away"), bRodDuringClimb);

	// On the dock at once: the cast goes out, and a late swim event (the owner's settled OnSwimStateChanged after a
	// correction replays the climb, swimming.md) must not cancel it: fishing reads the state, it doesn't listen to events.
	TestEqual(TEXT("on land: cast allowed"), BlockText(Fishing->GetCastBlock()), BlockText(ELureCastBlock::None));
	TestTrue(TEXT("cast right after climbing out"), Fishing->AuthorityCast(0.f, 180.f));
	Character->OnSwimStateChanged.Broadcast(true);
	Character->OnSwimStateChanged.Broadcast(false);
	Scene.Tick(30);
	TestTrue(TEXT("the line is still out after a late swim event"), Fishing->IsLineOut());
	TestNotEqual(TEXT("not reeled in"), ResultText(Fishing->GetNetState().LastResult), ResultText(ELureFishingResult::ReeledIn));
	TestTrue(TEXT("on land: the rod is back in hand"), Fishing->IsRodInHand());
	TestEqual(TEXT("on land: the arms hold the rod"), PoseText(Character->GetArmsPose()), PoseText(EFPArmsPose::HoldRod));
	if (APlayerController* Controller = Cast<APlayerController>(Character->GetController()))
	{
		Controller->UnPossess();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFishingLedgeClimbTest, "Project.Fishing.Climb.LedgePullUpReelsTheLineIn", LureSwimFishingTest::Flags)

bool FLureFishingLedgeClimbTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedMovementTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FScene Scene;
	if (!Table || !Scene.Create(*this))
	{
		return false;
	}
	const FLureMovementRow Stand = ShippedRow(Table, ELureMovementState::Stand);
	Scene.AddBox(FVector(0.f, 0.f, -50.f), FVector(3000.f, 3000.f, 50.f)); // dry floor at z = 0
	const float FaceX = 100.f;
	const float LedgeTop = 100.f;
	Scene.AddBox(FVector(FaceX + 300.f, 0.f, 0.5f * LedgeTop), FVector(300.f, 300.f, 0.5f * LedgeTop));

	ALurePlayerCharacter* Character = Scene.Spawn(FVector(-200.f, 0.f, Stand.CapsuleHalfHeight + 2.f), Table, /*bPossess*/ false);
	ULureFishingComponent* Fishing = ReadyFishing(Character);
	if (!TestNotNull(TEXT("character with fishing"), Fishing))
	{
		return false;
	}
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	Scene.Tick(20);
	if (!TestTrue(TEXT("cast (onto the floor behind)"), Fishing->AuthorityCast(0.f, 180.f)))
	{
		return false;
	}
	Scene.Tick(5);
	TestTrue(TEXT("the line is out"), Fishing->IsLineOut());

	// In the air against the ledge's face after a jump, on the way down (as Project.Movement.Climb.JumpClimbPullsUpOntoAReachedLedge).
	Character->SetActorLocation(FVector(FaceX - Stand.CapsuleRadius - 1.f, 0.f, LedgeTop - 20.f + Stand.CapsuleHalfHeight));
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.f, 0.f, -20.f);
	Character->JumpCurrentCount = 1;

	TestTrue(TEXT("in the air after the jump, the line is still out"), Fishing->IsLineOut());
	bool bClimbSeen = false;
	bool bChecked = false;
	ELureFishingResult ReelResult = ELureFishingResult::None;
	ELureCastBlock ReelReason = ELureCastBlock::None;
	ELureCastBlock BlockDuringClimb = ELureCastBlock::None;
	bool bCastAccepted = false;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Character->AddMovementInput(FVector::ForwardVector, 1.f, /*bForce*/ true);
		Scene.Tick(1);
		if (Movement->IsLedgeClimbing())
		{
			bClimbSeen = true;
			if (!bChecked && !Fishing->IsLineOut())
			{
				bChecked = true;
				ReelResult = Fishing->GetNetState().LastResult;
				ReelReason = Fishing->GetNetState().ResultReason;
				BlockDuringClimb = Fishing->GetCastBlock();
				bCastAccepted = Fishing->AuthorityCast(0.f, 180.f);
			}
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
	TestTrue(TEXT("the pull-up brought the line in while climbing"), bChecked);
	TestEqual(TEXT("result ReeledIn"), ResultText(ReelResult), ResultText(ELureFishingResult::ReeledIn));
	TestEqual(TEXT("reason Climbing"), BlockText(ReelReason), BlockText(ELureCastBlock::Climbing));
	TestEqual(TEXT("climbing: cast block"), BlockText(BlockDuringClimb), BlockText(ELureCastBlock::Climbing));
	TestFalse(TEXT("climbing: the server refuses a cast"), bCastAccepted);
	TestFalse(TEXT("the line stays in"), Fishing->IsLineOut());
	TestTrue(TEXT("the rod stays in hand on land (a pull-up is not swimming)"), Fishing->IsRodInHand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSwimStanceHintOnHudTest, "Project.Movement.Swim.Depth.StanceHintOnHud", LureSwimFishingTest::Flags)

bool FLureSwimStanceHintOnHudTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = ShippedMovementTable(*this);
	FGCObjectScopeGuard KeepTable(Table);
	FScene Scene;
	if (!Table || !Scene.Create(*this))
	{
		return false;
	}
	// Wading in 60 cm of water: crouching would put the capsule center under the surface, so it is refused (T-026 B2).
	const float Depth = 60.f;
	Scene.AddBox(FVector(0.f, 0.f, -Depth - 50.f), FVector(1500.f, 1500.f, 50.f));
	Scene.AddWater(400.f);
	const FLureMovementRow Stand = ShippedRow(Table, ELureMovementState::Stand);
	ALurePlayerCharacter* Character = Scene.Spawn(FVector(0.f, 0.f, -Depth + Stand.CapsuleHalfHeight + 2.15f), Table, /*bPossess*/ false);
	if (!TestNotNull(TEXT("character"), Character))
	{
		return false;
	}
	Scene.Tick(30);
	TestFalse(TEXT("wading, not swimming"), Character->IsSwimming());
	TestFalse(TEXT("no hint on the HUD before"), ALureHUD::GetStatusText(Character).Contains(TEXT("Too deep")));

	Character->RequestStance(ELureStance::Crouch);
	Scene.Tick(1);
	const FString Hint = Character->GetStanceHintText();
	TestFalse(TEXT("the crouch is refused with a hint"), Hint.IsEmpty());
	const FString Status = ALureHUD::GetStatusText(Character);
	TestTrue(FString::Printf(TEXT("the HUD shows the hint (\"%s\")"), *Status), !Hint.IsEmpty() && Status.Contains(Hint));
	TestTrue(TEXT("the hint reads \"Too deep to crouch here.\""), Status.Contains(TEXT("Too deep to crouch here.")));

	// The hint goes away after StanceHintDuration.
	Scene.Tick(FMath::CeilToInt((Character->StanceHintDuration + 0.2f) / Dt));
	TestFalse(TEXT("the HUD hint goes away"), ALureHUD::GetStatusText(Character).Contains(TEXT("Too deep")));
	return true;
}

} // namespace LureSwimFishingTest

#endif // WITH_DEV_AUTOMATION_TESTS

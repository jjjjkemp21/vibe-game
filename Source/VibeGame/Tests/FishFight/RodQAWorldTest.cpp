// Lure T-028 QA (qa-engineer, 2026-09-23): rod steering in a world, black-box.
// Project.Fishing.Fight.Rod.QA.{Input, Reel, Net, Camera, Hud, Signs}.*
// A standalone owner: a local player controller possessing a character on the T-006/T-007 test dock. In a standalone world the owning
// client is also the server, so the owner's RPCs run at once: what the server sees is what the owner sent. The fish is a rolled Common
// Bonefish whose fight pattern (row "Run") is replaced by one fixture move per test, so each ending is driven on purpose.
// Spec: docs/specs/reel-fight-rules.md "Rod steering and reel speed (T-028)"; GAME_DESIGN.md "Fight controls": "once a fish is hooked,
// the mouse steers the rod instead of the view, and the camera gently follows the rod and fish until the fish is landed or lost".

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "InputMappingContext.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"

namespace LureRodQA
{
	namespace RodWorld
	{
		/** The shipped tables, the real fish tables, a rolled Common Bonefish, a world with the dock, and the owner. */
		struct FScene
		{
			LureFightQA::FFightTables Data;
			FishQA::FTables Fish;
			FFishInstance Bonefish;
			LureFightQA::FWorld World;
			TStrongObjectPtr<UDataTable> Gear;
			TStrongObjectPtr<UDataTable> Patterns;
			FOwner Owner;

			~FScene()
			{
				Owner.Release();
			}

			/** Pattern = the Bonefish's fight; LineStrength / SpoolLength > 0 change the starter line; a water volume for swimming. */
			bool Create(FAutomationTestBase& Test, const FLureFightPatternRow& Pattern, float LineStrength = 0.f, float SpoolLength = 0.f, bool bWaterVolume = false)
			{
				if (!Data.Load(Test) || !FishQA::LoadReal(Test, Fish) || !LureFightQA::RollFish(Test, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish)
					|| !World.Create(Test))
				{
					return false;
				}
				if (bWaterVolume && !Test.TestNotNull(TEXT("QA rig: water volume"), AddWaterVolume(World.World, 800.f)))
				{
					return false;
				}
				Gear = GearTable(Data.Gear.Get(), LineStrength, SpoolLength);
				Patterns = PatternTable(Pattern);
				return Owner.Create(Test, World, Fish, Gear.Get(), Patterns.Get(), Data.Fight.Get());
			}

			/** Another character on the dock with the same fishing setup (no controller). */
			ALurePlayerCharacter* SpawnOther(const FVector& Feet)
			{
				ALurePlayerCharacter* Other = World.Spawn(Feet);
				LureFightQA::SetUpFishing(Other, Fish, Gear.Get(), Patterns.Get(), Data.Fight.Get());
				return Other;
			}
		};

		/** A fight that goes on by itself: the fish holds with its base pull, doesn't swim, has no side (unless Side != 0). */
		inline FLureFightPatternRow SteadyPattern(float Side = 0.f)
		{
			return OneMove(SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, Side));
		}

		/** Everything "the look is the view again" means once a fight is over. */
		inline void ExpectLookIsTheViewAgain(FAutomationTestBase& Test, FScene& Scene, const FString& Why)
		{
			FOwner& Owner = Scene.Owner;
			ULureFishingComponent* Fishing = Owner.Fishing;
			Test.TestFalse(Why + TEXT(": not steering the rod"), Fishing->IsSteeringRod());
			Test.TestFalse(Why + TEXT(": no fight on"), Fishing->GetFightNet().bActive);
			Test.TestTrue(Why + TEXT(": the look input turns the view again, all of it"), Owner.LookTurnsTheView(3.f, -2.f));
			Test.TestFalse(Why + TEXT(": ConsumeLookInput refuses"), Fishing->ConsumeLookInput(5.f, 5.f));
			Test.TestTrue(Why + TEXT(": the rod aim is level and centered"), Fishing->GetRodAim().IsZero());
			Test.TestFalse(Why + TEXT(": look input is not ignored"), Owner.Controller->IsLookInputIgnored());
			Test.TestTrue(Why + TEXT(": nothing holds the camera (the control rotation stays where it is put)"), Owner.ViewIsFree(Scene.World));
			Scene.World.Tick(60);
			const FVector2D Eased = Fishing->GetRodAimForAnimation();
			Test.TestTrue(FString::Printf(TEXT("%s: the arms' eased rod aim is back to level (%.3f, %.3f)"), *Why, Eased.X, Eased.Y), Eased.Size() < 0.01f);
			const FString Text = Fishing->GetStatusText();
			Test.TestFalse(Why + TEXT(": HUD: no rod line"), HasLine(Text, TEXT("Rod: ")));
			Test.TestFalse(Why + TEXT(": HUD: no reel line"), HasLine(Text, TEXT("Reel ")));
			Test.TestFalse(Why + TEXT(": HUD: no run hint"), HasLine(Text, TEXT("Fish runs")));
		}

		enum class EEnding : uint8 { Landed, Snapped, Spooled, ThrewHook, ReeledIn, Swimming, Climbing };

		inline FString EndingName(EEnding Ending)
		{
			switch (Ending)
			{
			case EEnding::Landed: return TEXT("landed");
			case EEnding::Snapped: return TEXT("snapped");
			case EEnding::Spooled: return TEXT("spooled");
			case EEnding::ThrewHook: return TEXT("threw the hook");
			case EEnding::ReeledIn: return TEXT("reeled in");
			case EEnding::Swimming: return TEXT("fell in (swimming)");
			case EEnding::Climbing: return TEXT("pulled up a ledge (climbing)");
			default: return TEXT("?");
			}
		}

		/**
		 *  Hooks a fish, steers the rod (the view takes nothing, the camera is held on the fish), drives the fight to Ending, checks the result
		 *  is that ending, then checks the look is the view again with no stuck state.
		 */
		inline bool RunEnding(FAutomationTestBase& Test, EEnding Ending)
		{
			const FString Name = EndingName(Ending);
			FLureFightMove Move;
			float Charge = 0.5f;
			float Line = 0.f;
			float Spool = 0.f;
			switch (Ending)
			{
			case EEnding::Landed: Move = SideMove(TEXT("Gentle"), 0.2f, 0.f, 0.f, 0.f); Charge = 0.f; break;       // reeled in from the shortest cast
			case EEnding::Snapped: Move = SideMove(TEXT("Hard"), 2.f, 0.f, 0.f, 0.f); Line = 1.f; break;            // reeling a hard fish on a 1-unit line
			case EEnding::Spooled: Move = SideMove(TEXT("Away"), 3.f, 1.f, 1.f, 0.f); Charge = 0.f; Spool = 600.f; break; // runs off a 6 m spool
			case EEnding::ThrewHook: Move = SideMove(TEXT("Soft"), 0.5f, 0.f, 0.f, 0.f); break;                     // the rod dipped: slack line
			default: Move = SideMove(TEXT("Swim"), 1.f, 0.f, 0.f, -0.6f); break;
			}
			FScene Scene;
			if (!Scene.Create(Test, OneMove(Move), Line, Spool, Ending == EEnding::Swimming))
			{
				return false;
			}
			FOwner& Owner = Scene.Owner;
			ULureFishingComponent* Fishing = Owner.Fishing;
			Test.TestTrue(Name + TEXT(", before the hook: the look input turns the view"), Owner.LookTurnsTheView(2.f, 1.f));
			if (!HookAndFight(Test, Scene.World, Fishing, Scene.Bonefish, Charge))
			{
				return false;
			}
			const FLureFishFightRow& T = Fishing->GetFightTuning();
			bool bPartial = false;
			Test.TestTrue(Name + TEXT(", fight: steering the rod"), Fishing->IsSteeringRod());
			const bool bViewTook = Owner.LookTurnsTheView(30.f, 20.f, &bPartial);
			Test.TestFalse(Name + TEXT(", fight: the look input does not turn the view (none of it)"), bViewTook || bPartial);
			const FVector2D Aim = Fishing->GetRodAim();
			Test.TestTrue(FString::Printf(TEXT("%s, fight: the rod took it: yaw 30/%.0f, pitch 20/%.0f (%.3f, %.3f)"), *Name, T.RodAimSideDeg, T.RodAimUpDeg, Aim.X, Aim.Y),
				FMath::IsNearlyEqual(static_cast<float>(Aim.X), 30.f / T.RodAimSideDeg, 1.0e-4f) && FMath::IsNearlyEqual(static_cast<float>(Aim.Y), 20.f / T.RodAimUpDeg, 1.0e-4f));
			Test.TestFalse(Name + TEXT(", fight: the camera is held on the fish (the control rotation is moved)"), Owner.ViewIsFree(Scene.World, 10));

			ULureCharacterMovementComponent* Movement = Owner.Character->GetLureMovement();
			switch (Ending)
			{
			case EEnding::Landed:
			case EEnding::Snapped:
				Fishing->SetReelHeld(true);
				break;
			case EEnding::ThrewHook:
				Owner.Character->DoLook(0.f, -200.f); // fully dipped, not reeling
				break;
			case EEnding::ReeledIn:
				Fishing->RequestReelIn();
				break;
			case EEnding::Swimming:
				Owner.Character->SetActorLocation(FVector(1000.f, 0.f, -20.f), false, nullptr, ETeleportType::TeleportPhysics);
				break;
			case EEnding::Climbing:
			{
				// In the air against the dock's back face after a jump, on the way down: moving into it pulls the player up the ledge.
				const float Radius = Owner.Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
				const float HalfHeight = Owner.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Owner.Character->SetActorLocation(FVector(-LureFightQA::DockEdgeX - Radius - 1.f, 0.f, LureFightQA::DockTop - 20.f + HalfHeight), false, nullptr,
					ETeleportType::TeleportPhysics);
				Movement->SetMovementMode(MOVE_Falling);
				Movement->Velocity = FVector(0.f, 0.f, -20.f);
				Owner.Character->JumpCurrentCount = 1;
				break;
			}
			default:
				break; // Spooled: let it run
			}
			bool bClimbed = false;
			for (int32 Frame = 0; Frame < 60 * 12 && Fishing->GetFishingState() != ELureFishingState::Idle; ++Frame)
			{
				if (Ending == EEnding::Climbing)
				{
					Owner.Character->AddMovementInput(FVector::ForwardVector, 1.f, /*bForce*/ true);
				}
				Scene.World.Tick(1);
				bClimbed |= Movement->IsLedgeClimbing();
			}
			if (!Test.TestEqual(Name + TEXT(": the fight ended (state Idle)"), LureFightQA::StateName(Fishing->GetFishingState()), LureFightQA::StateName(ELureFishingState::Idle)))
			{
				return false;
			}
			const FLureFishingNetState& Net = Fishing->GetNetState();
			const FString Result = LureFightQA::ResultName(Net.LastResult);
			const FString Reason = StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Net.ResultReason));
			const FString Outcome = LureFightQA::OutcomeName(Fishing->GetFightNet().Outcome);
			switch (Ending)
			{
			case EEnding::Landed: Test.TestEqual(Name + TEXT(": result"), Result, LureFightQA::ResultName(ELureFishingResult::Landed)); break;
			case EEnding::Snapped: Test.TestEqual(Name + TEXT(": fight outcome"), Outcome, LureFightQA::OutcomeName(ELureFightOutcome::Snapped)); break;
			case EEnding::Spooled: Test.TestEqual(Name + TEXT(": fight outcome"), Outcome, LureFightQA::OutcomeName(ELureFightOutcome::Spooled)); break;
			case EEnding::ThrewHook: Test.TestEqual(Name + TEXT(": result"), Result, LureFightQA::ResultName(ELureFishingResult::ThrewHook)); break;
			case EEnding::ReeledIn:
				Test.TestEqual(Name + TEXT(": result"), Result, LureFightQA::ResultName(ELureFishingResult::Lost));
				Test.TestEqual(Name + TEXT(": reason (the player's choice)"), Reason, StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(ELureCastBlock::None)));
				break;
			case EEnding::Swimming:
				Test.TestTrue(Name + TEXT(": the character swims"), Owner.Character->IsSwimming());
				Test.TestEqual(Name + TEXT(": result"), Result, LureFightQA::ResultName(ELureFishingResult::Lost));
				Test.TestEqual(Name + TEXT(": reason"), Reason, StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(ELureCastBlock::Swimming)));
				break;
			case EEnding::Climbing:
				Test.TestTrue(Name + TEXT(": the pull-up ran"), bClimbed);
				Test.TestEqual(Name + TEXT(": result"), Result, LureFightQA::ResultName(ELureFishingResult::Lost));
				Test.TestEqual(Name + TEXT(": reason"), Reason, StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(ELureCastBlock::Climbing)));
				break;
			default:
				break;
			}
			Fishing->SetReelHeld(false);
			ExpectLookIsTheViewAgain(Test, Scene, Name);

			if (Ending == EEnding::Landed)
			{
				// T-030: the landed fish hangs on the hook (a cast is Busy) until it is taken off.
				if (ULureHandsComponent* Hands = ULureHandsComponent::Get(Owner.Character))
				{
					if (ALureFishItem* Hanging = Hands->AuthorityReleaseHanging())
					{
						Hanging->Destroy();
					}
				}
				// The next fish: the rod starts level and centered and the look steers it again (no state left over from the last fight).
				if (HookAndFight(Test, Scene.World, Fishing, Scene.Bonefish, 0.5f))
				{
					Test.TestTrue(Name + TEXT(", next fight: steering again"), Fishing->IsSteeringRod());
					Test.TestTrue(Name + TEXT(", next fight: the rod starts level and centered"), Fishing->GetRodAim().IsZero());
					Test.TestTrue(Name + TEXT(", next fight: the server's rod starts level too"), Fishing->GetServerFightInput().RodPitch == 0.f && Fishing->GetServerFightInput().RodYaw == 0.f);
					Test.TestFalse(Name + TEXT(", next fight: the look input steers the rod, not the view"), Owner.LookTurnsTheView(5.f, 5.f));
					Fishing->RequestReelIn();
					Scene.World.Tick(2);
				}
			}
			return true;
		}
	}

	// =================================================================================================================
	// Input routing: look -> rod on the hook, rod -> look on every ending; no stuck input mode
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQALookLanded, "Project.Fishing.Fight.Rod.QA.Input.LookReturns.Landed", Flags)
	bool FRodQALookLanded::RunTest(const FString& Parameters) { return RodWorld::RunEnding(*this, RodWorld::EEnding::Landed); }

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQALookSnapped, "Project.Fishing.Fight.Rod.QA.Input.LookReturns.Snapped", Flags)
	bool FRodQALookSnapped::RunTest(const FString& Parameters) { return RodWorld::RunEnding(*this, RodWorld::EEnding::Snapped); }

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQALookSpooled, "Project.Fishing.Fight.Rod.QA.Input.LookReturns.Spooled", Flags)
	bool FRodQALookSpooled::RunTest(const FString& Parameters) { return RodWorld::RunEnding(*this, RodWorld::EEnding::Spooled); }

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQALookThrewHook, "Project.Fishing.Fight.Rod.QA.Input.LookReturns.ThrewHook", Flags)
	bool FRodQALookThrewHook::RunTest(const FString& Parameters) { return RodWorld::RunEnding(*this, RodWorld::EEnding::ThrewHook); }

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQALookReeledIn, "Project.Fishing.Fight.Rod.QA.Input.LookReturns.ReeledIn", Flags)
	bool FRodQALookReeledIn::RunTest(const FString& Parameters) { return RodWorld::RunEnding(*this, RodWorld::EEnding::ReeledIn); }

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQALookSwimming, "Project.Fishing.Fight.Rod.QA.Input.LookReturns.FallingInCutsTheLine", Flags)
	bool FRodQALookSwimming::RunTest(const FString& Parameters) { return RodWorld::RunEnding(*this, RodWorld::EEnding::Swimming); }

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQALookClimbing, "Project.Fishing.Fight.Rod.QA.Input.LookReturns.LedgePullUpCutsTheLine", Flags)
	bool FRodQALookClimbing::RunTest(const FString& Parameters) { return RodWorld::RunEnding(*this, RodWorld::EEnding::Climbing); }

	/** A respawn mid-fight (the pawn destroyed, a new one possessed): the new pawn's look is the view; nothing holds the camera. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQARespawnDestroyed, "Project.Fishing.Fight.Rod.QA.Input.Respawn.PawnDestroyedMidFight", Flags)
	bool FRodQARespawnDestroyed::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern(-0.6f)) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
		{
			return false;
		}
		FOwner& Owner = Scene.Owner;
		Owner.Character->DoLook(40.f, 30.f); // the rod steered hard right and back
		Scene.World.Tick(5);
		ALurePlayerCharacter* Old = Owner.Character;
		Owner.Controller->UnPossess();
		Old->Destroy();
		Scene.World.Tick(2);
		ALurePlayerCharacter* Fresh = Scene.SpawnOther(LureFightQA::StandAt() + FVector(0.f, 200.f, 0.f));
		if (!TestNotNull(TEXT("the new pawn"), Fresh))
		{
			return false;
		}
		Owner.Repossess(Fresh);
		Scene.World.Tick(5);
		TestTrue(TEXT("the new pawn is the local player's"), Fresh->IsLocallyControlled());
		TestFalse(TEXT("the new pawn: not steering"), Owner.Fishing->IsSteeringRod());
		TestTrue(TEXT("the new pawn: the look input turns the view"), Owner.LookTurnsTheView(3.f, -2.f));
		TestTrue(TEXT("the new pawn: the rod aim is level"), Owner.Fishing->GetRodAim().IsZero());
		TestTrue(TEXT("nothing holds the camera"), Owner.ViewIsFree(Scene.World));
		AddInfo(FString::Printf(TEXT("after a respawn the new pawn's reel step is %d (the default is %d)"), Owner.Fishing->GetReelStep(), FLureFight::DefaultReelStep(Owner.Fishing->GetFightTuning())));
		return true;
	}

	/** The controller takes another pawn while the first one still has a fish on: the new pawn's look is the view; the old fight holds nothing. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQARespawnSwapped, "Project.Fishing.Fight.Rod.QA.Input.Respawn.NewPawnWhileTheOldOneFights", Flags)
	bool FRodQARespawnSwapped::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern(-0.6f)) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
		{
			return false;
		}
		FOwner& Owner = Scene.Owner;
		Owner.Character->DoLook(40.f, 30.f);
		Scene.World.Tick(5);
		ULureFishingComponent* OldFishing = Owner.Fishing;
		ALurePlayerCharacter* Fresh = Scene.SpawnOther(LureFightQA::StandAt() + FVector(0.f, 200.f, 0.f));
		if (!TestNotNull(TEXT("the new pawn"), Fresh))
		{
			return false;
		}
		Owner.Repossess(Fresh);
		Scene.World.Tick(5);
		TestFalse(TEXT("the old pawn no longer steers"), OldFishing->IsSteeringRod());
		TestFalse(TEXT("the old pawn refuses look input"), OldFishing->ConsumeLookInput(10.f, 10.f));
		TestFalse(TEXT("the new pawn does not steer"), Owner.Fishing->IsSteeringRod());
		TestTrue(TEXT("the new pawn's look turns the view"), Owner.LookTurnsTheView(3.f, -2.f));
		TestTrue(TEXT("the old fight does not hold the camera"), Owner.ViewIsFree(Scene.World));
		AddInfo(FString::Printf(TEXT("the unpossessed pawn still has its fish on: %s (state %s, reeling %d)"), OldFishing->GetFightNet().bActive ? TEXT("yes") : TEXT("no"),
			*LureFightQA::StateName(OldFishing->GetFishingState()), OldFishing->IsServerReeling()));
		OldFishing->AuthorityReelIn();
		return true;
	}

	/**
	 *  A teleport on land mid-fight: the input mode stays consistent with the fight (still the rod while the fish is on: the fight has no
	 *  distance rule, the fish follows the player at the fight's line out), and the look is the view once it ends.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQATeleport, "Project.Fishing.Fight.Rod.QA.Input.TeleportOnLandKeepsItConsistent", Flags)
	bool FRodQATeleport::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern(-0.6f)) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
		{
			return false;
		}
		FOwner& Owner = Scene.Owner;
		ULureFishingComponent* Fishing = Owner.Fishing;
		Owner.Character->DoLook(45.f, 0.f);
		Scene.World.Tick(10);
		const FVector To = Owner.Character->GetActorLocation() + FVector(-200.f, 250.f, 0.f); // still on the dock
		TestTrue(TEXT("the teleport works"), Owner.Character->TeleportTo(To, Owner.Character->GetActorRotation()));
		Scene.World.Tick(3);
		const bool bStillOn = Fishing->GetFishingState() == ELureFishingState::Hooked && Fishing->GetFightNet().bActive;
		AddInfo(FString::Printf(TEXT("a teleport on land mid-fight %s the fish"), bStillOn ? TEXT("keeps") : TEXT("loses")));
		if (bStillOn)
		{
			TestTrue(TEXT("fish still on: the look input still steers the rod"), Fishing->IsSteeringRod() && !Owner.LookTurnsTheView(-9.f, 0.f));
			TestNearlyEqual(TEXT("fish still on: the rod keeps its aim"), static_cast<float>(Fishing->GetRodAim().X), 36.f / Fishing->GetFightTuning().RodAimSideDeg, 1.0e-4f);
			TestNearlyEqual(TEXT("the fish follows the player at the fight's line out"), static_cast<float>(FVector::Dist2D(Owner.Character->GetActorLocation(), Fishing->GetBobberLocation())),
				Fishing->GetFightNet().LineOut, 5.f);
			Fishing->RequestReelIn();
			Scene.World.Tick(2);
		}
		RodWorld::ExpectLookIsTheViewAgain(*this, Scene, TEXT("after the teleport"));
		return true;
	}

	/**
	 *  Only the owning player steers: a pawn nobody controls (the server's copy of a remote player) never consumes look input or wheel
	 *  presses and shows the server's rod; a hooked fish with no fight (the AutoLandDelay debug switch) leaves the look on the view.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAOnlyOwner, "Project.Fishing.Fight.Rod.QA.Input.OnlyTheOwnerSteers", Flags)
	bool FRodQAOnlyOwner::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern(-0.6f)))
		{
			return false;
		}
		ALurePlayerCharacter* Nobody = Scene.SpawnOther(LureFightQA::StandAt() + FVector(0.f, -250.f, 0.f));
		ULureFishingComponent* Remote = Nobody ? Nobody->GetFishing() : nullptr;
		if (!TestNotNull(TEXT("the uncontrolled player"), Remote))
		{
			return false;
		}
		Scene.World.Tick(5);
		if (!HookAndFight(*this, Scene.World, Remote, Scene.Bonefish))
		{
			return false;
		}
		TestFalse(TEXT("no controller: never steering"), Remote->IsSteeringRod());
		TestFalse(TEXT("no controller: look input is not consumed"), Remote->ConsumeLookInput(10.f, 10.f));
		const int32 Before = Remote->GetReelStep();
		Remote->StepReelSpeed(1);
		Remote->StepReelSpeed(1);
		TestEqual(TEXT("no controller: the wheel does nothing"), Remote->GetReelStep(), Before);
		TestTrue(TEXT("no controller: the rod shown is the server's (level)"), Remote->GetRodAim().IsZero());
		Remote->AuthoritySetFightInput(Remote->GetFightNet().FightId, 0.5f, -0.5f, 2);
		Scene.World.Tick(2);
		TestTrue(FString::Printf(TEXT("the server's view of that player's rod follows its input (%.2f, %.2f)"), Remote->GetRodAim().X, Remote->GetRodAim().Y),
			Remote->GetRodAim().Equals(FVector2D(-0.5, 0.5), 1.0e-5));
		TestEqual(TEXT("... and its reel step"), Remote->GetReelStep(), 2);
		Remote->AuthorityReelIn();

		// The owner with the debug switch: a hooked fish lands after a delay, no fight runs, so the mouse stays on the view.
		FLureFishingRow Debug = LureFightQA::QuickProfile();
		Debug.AutoLandDelay = 5.f;
		Scene.Owner.Fishing->SetFishingProfile(Debug);
		if (LureFightQA::CastAndWait(*this, Scene.World, Scene.Owner.Fishing) && TestTrue(TEXT("hooked (debug)"), Scene.Owner.Fishing->AuthorityHookFish(Scene.Bonefish)))
		{
			Scene.World.Tick(2);
			TestEqual(TEXT("debug: Hooked"), LureFightQA::StateName(Scene.Owner.Fishing->GetFishingState()), LureFightQA::StateName(ELureFishingState::Hooked));
			TestFalse(TEXT("debug: no fight, not steering"), Scene.Owner.Fishing->IsSteeringRod());
			TestTrue(TEXT("debug: the look input turns the view"), Scene.Owner.LookTurnsTheView(4.f, 1.f));
			Scene.Owner.Fishing->RequestReelIn();
			Scene.World.Tick(2);
		}
		return true;
	}

	/** Spec: "the mouse wheel (or bumpers) sets the reel speed": the keys come from the settings, map only to the reel actions, and the character binds them. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAReelKeys, "Project.Fishing.Fight.Rod.QA.Input.ReelKeysMappedAndBound", Flags)
	bool FRodQAReelKeys::RunTest(const FString& Parameters)
	{
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
		const UInputAction* Faster = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelFaster);
		const UInputAction* Slower = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelSlower);
		if (!TestNotNull(TEXT("mapping context"), Context) || !TestNotNull(TEXT("ReelFaster"), Faster) || !TestNotNull(TEXT("ReelSlower"), Slower))
		{
			return false;
		}
		TestTrue(TEXT("ReelFaster and ReelSlower are buttons"), Faster->ValueType == EInputActionValueType::Boolean && Slower->ValueType == EInputActionValueType::Boolean);
		TestTrue(TEXT("settings: the wheel up and the right bumper are faster"), Settings->ReelFasterKeys.Contains(EKeys::MouseScrollUp) && Settings->ReelFasterKeys.Contains(EKeys::Gamepad_RightShoulder));
		TestTrue(TEXT("settings: the wheel down and the left bumper are slower"), Settings->ReelSlowerKeys.Contains(EKeys::MouseScrollDown) && Settings->ReelSlowerKeys.Contains(EKeys::Gamepad_LeftShoulder));
		TSet<FKey> FasterMapped, SlowerMapped;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == Faster)
			{
				FasterMapped.Add(Mapping.Key);
			}
			else if (Mapping.Action == Slower)
			{
				SlowerMapped.Add(Mapping.Key);
			}
			else
			{
				TestFalse(FString::Printf(TEXT("%s's key %s is not a reel-speed key"), *GetNameSafe(Mapping.Action), *Mapping.Key.ToString()),
					Settings->ReelFasterKeys.Contains(Mapping.Key) || Settings->ReelSlowerKeys.Contains(Mapping.Key));
			}
		}
		TSet<FKey> FasterKeys, SlowerKeys;
		for (const FKey& Key : Settings->ReelFasterKeys)
		{
			FasterKeys.Add(Key);
		}
		for (const FKey& Key : Settings->ReelSlowerKeys)
		{
			SlowerKeys.Add(Key);
		}
		TestTrue(TEXT("ReelFaster is mapped to exactly the settings' keys"), FasterMapped.Num() == FasterKeys.Num() && FasterMapped.Includes(FasterKeys));
		TestTrue(TEXT("ReelSlower is mapped to exactly the settings' keys"), SlowerMapped.Num() == SlowerKeys.Num() && SlowerMapped.Includes(SlowerKeys));
		TestTrue(TEXT("no key is both faster and slower"), FasterMapped.Intersect(SlowerMapped).Num() == 0);

		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern()))
		{
			return false;
		}
		const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Scene.Owner.Character->InputComponent);
		if (!TestNotNull(TEXT("the possessed character has an enhanced input component"), Input))
		{
			return false;
		}
		int32 FasterBound = 0, SlowerBound = 0;
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetTriggerEvent() == ETriggerEvent::Started)
			{
				FasterBound += Binding->GetAction() == Faster ? 1 : 0;
				SlowerBound += Binding->GetAction() == Slower ? 1 : 0;
			}
		}
		TestEqual(TEXT("ReelFaster is bound once, on press (Started)"), FasterBound, 1);
		TestEqual(TEXT("ReelSlower is bound once, on press (Started)"), SlowerBound, 1);
		return true;
	}

	// =================================================================================================================
	// Reel speed: wheel spam, clamping, sent at once, kept between fights, nothing without a fish
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAWheelSpam, "Project.Fishing.Fight.Rod.QA.Reel.WheelSpamClampsAndSendsTheLast", Flags)
	bool FRodQAWheelSpam::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern()))
		{
			return false;
		}
		FOwner& Owner = Scene.Owner;
		ULureFishingComponent* Fishing = Owner.Fishing;
		const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Owner.Character->InputComponent);
		const UInputAction* Faster = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelFaster);
		const UInputAction* Slower = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelSlower);
		const FLureFishFightRow& T = Fishing->GetFightTuning();
		const int32 Default = FLureFight::DefaultReelStep(T);
		const int32 Top = FLureFight::NumReelSteps(T) - 1;
		if (!TestNotNull(TEXT("input component"), Input) || !TestTrue(TEXT("the wheel is bound"), Fire(Input, Faster, ETriggerEvent::Started) == 1))
		{
			return false;
		}
		TestEqual(TEXT("no fish on: the wheel does nothing"), Fishing->GetReelStep(), Default);
		if (!HookAndFight(*this, Scene.World, Fishing, Scene.Bonefish))
		{
			return false;
		}
		TestEqual(TEXT("fight: starts on the default step"), Fishing->GetReelStep(), Default);

		for (int32 Press = 0; Press < 50; ++Press)
		{
			Fire(Input, Faster, ETriggerEvent::Started);
		}
		TestEqual(TEXT("50 wheel-ups in one frame: the fastest step, no further"), Fishing->GetReelStep(), Top);
		Scene.World.Tick(1);
		TestEqual(TEXT("... sent at once: the server has it after one frame"), Fishing->GetServerFightInput().ReelStep, Top);
		Scene.World.Tick(1);
		TestEqual(TEXT("... and the fight reels at it"), Fishing->GetFightState().ReelStep, Top);

		for (int32 Press = 0; Press < 50; ++Press)
		{
			Fire(Input, Slower, ETriggerEvent::Started);
		}
		TestEqual(TEXT("50 wheel-downs: the slowest step, no further"), Fishing->GetReelStep(), 0);
		// T-028b (O6): the server rate-limits reel-step changes (a burst, then FightReelStepsPerSecond); one over the limit waits, the last one wins.
		const ULureFishingSettings* Limits = GetDefault<ULureFishingSettings>();
		const int32 LimitFrames = FMath::CeilToInt(1.f / FMath::Max(1.0e-3f, Limits->FightReelStepsPerSecond) / LureFightQA::WorldDt) + 1;
		Scene.World.Tick(LimitFrames);
		TestEqual(TEXT("... the server follows within its reel-step rate limit"), Fishing->GetServerFightInput().ReelStep, 0);

		for (int32 Press = 0; Press < 51; ++Press)
		{
			Fire(Input, Press % 2 == 0 ? Faster : Slower, ETriggerEvent::Started);
		}
		TestEqual(TEXT("51 alternating presses from the slowest (up first): one step up"), Fishing->GetReelStep(), 1);
		Scene.World.Tick(LimitFrames);
		TestEqual(TEXT("... the server has the last one"), Fishing->GetServerFightInput().ReelStep, 1);

		// A twitching wheel: one press a frame, alternating, for a second.
		int32 Changes = 0;
		int32 Last = Fishing->GetServerFightInput().ReelStep;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Fire(Input, Frame % 2 == 0 ? Faster : Slower, ETriggerEvent::Started);
			Scene.World.Tick(1);
			const int32 Now = Fishing->GetServerFightInput().ReelStep;
			Changes += Now != Last ? 1 : 0;
			Last = Now;
		}
		Scene.World.Tick(LimitFrames);
		TestEqual(TEXT("a twitching wheel: the server always ends on the owner's step"), Fishing->GetServerFightInput().ReelStep, Fishing->GetReelStep());
		const int32 MaxChanges = FMath::Max(1, Limits->FightReelStepBurst) + FMath::CeilToInt(Limits->FightReelStepsPerSecond) + 1;
		TestTrue(FString::Printf(TEXT("a wheel twitching every frame for 1 s: %d reel-step changes on the server (rate limit: at most %d)"), Changes, MaxChanges), Changes <= MaxChanges);

		const int32 Kept = Fishing->GetReelStep();
		Fishing->RequestReelIn();
		Scene.World.Tick(2);
		for (int32 Press = 0; Press < 3; ++Press)
		{
			Fire(Input, Faster, ETriggerEvent::Started);
		}
		TestEqual(TEXT("after the fight: the wheel does nothing and the step is kept"), Fishing->GetReelStep(), Kept);
		return true;
	}

	/** "The step is kept from fight to fight (a reel keeps its setting)": on the owner, on the server from the first step, and in what others see. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAStepKept, "Project.Fishing.Fight.Rod.QA.Reel.StepKeptForTheNextFight", Flags)
	bool FRodQAStepKept::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern()) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
		{
			return false;
		}
		ULureFishingComponent* Fishing = Scene.Owner.Fishing;
		Fishing->StepReelSpeed(-1);
		Fishing->StepReelSpeed(-1);
		Scene.World.Tick(2);
		TestEqual(TEXT("fight 1: the slowest step"), Fishing->GetFightState().ReelStep, 0);
		Fishing->RequestReelIn();
		Scene.World.Tick(2);
		if (!LureFightQA::CastAndWait(*this, Scene.World, Fishing) || !TestTrue(TEXT("fight 2: hooked"), Fishing->AuthorityHookFish(Scene.Bonefish)))
		{
			return false;
		}
		TestEqual(TEXT("fight 2, the hook frame: the server already uses the kept step"), Fishing->GetFightState().ReelStep, 0);
		TestEqual(TEXT("fight 2, the hook frame: FightNet (what others see) has it"), static_cast<int32>(Fishing->GetFightNet().ReelStep), 0);
		Scene.World.Tick(1);
		TestEqual(TEXT("fight 2: the owner's step is kept"), Fishing->GetReelStep(), 0);
		TestEqual(TEXT("fight 2: the server input too"), Fishing->GetServerFightInput().ReelStep, 0);
		TestEqual(TEXT("fight 2: HUD"), FindLine(Fishing->GetStatusText(), TEXT("Reel ")), FString(TEXT("Reel 1/3 (wheel or LB/RB)")));
		TestTrue(TEXT("fight 2: the rod starts level (unlike the step, the aim is per fight)"), Fishing->GetRodAim().IsZero());
		return true;
	}

	// =================================================================================================================
	// The input stream to the server: rate limited, repaired by the resend
	// =================================================================================================================

	/** The mouse moving every frame for 2 s at 30, 60 and 144 fps: the aim reaches the server at most every FightInputSendSeconds, and not much less often. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASendRate, "Project.Fishing.Fight.Rod.QA.Net.AimSendIsRateLimited", Flags)
	bool FRodQASendRate::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern()) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
		{
			return false;
		}
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		ULureFishingComponent* Fishing = Scene.Owner.Fishing;
		float Degrees = 0.f;
		float Direction = 1.f;
		for (const float Rate : { 30.f, 60.f, 144.f })
		{
			const float Dt = 1.f / Rate;
			const int32 Frames = FMath::RoundToInt(2.f * Rate);
			int32 Sends = 0;
			float Last = Fishing->GetServerFightInput().RodYaw;
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				if (FMath::Abs(Degrees + Direction) > 40.f)
				{
					Direction = -Direction;
				}
				Degrees += Direction;
				Scene.Owner.Character->DoLook(Direction, 0.f); // one degree a frame, zig-zag: the aim changes every frame
				Scene.World.Tick(1, Dt);
				const float Now = Fishing->GetServerFightInput().RodYaw;
				Sends += Now != Last ? 1 : 0;
				Last = Now;
			}
			const int32 Most = FMath::FloorToInt(2.f / Settings->FightInputSendSeconds) + 1;
			const int32 Least = FMath::FloorToInt(2.f / (Settings->FightInputSendSeconds + 2.f * Dt)) - 1;
			TestTrue(FString::Printf(TEXT("%.0f fps, the aim changing every frame for 2 s: %d updates reach the server (between %d and %d: every %.2f s)"), Rate, Sends, Least, Most,
				Settings->FightInputSendSeconds), Sends <= Most && Sends >= Least);
		}
		return true;
	}

	/** A lost update (the server's copy of the aim goes stale while the owner holds still) is repaired by the resend every FightInputResendSeconds. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAResend, "Project.Fishing.Fight.Rod.QA.Net.LostAimRepairedByTheResend", Flags)
	bool FRodQAResend::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern()) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
		{
			return false;
		}
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		ULureFishingComponent* Fishing = Scene.Owner.Fishing;
		const float Half = 0.5f * Fishing->GetFightTuning().RodAimSideDeg;
		Scene.Owner.Character->DoLook(Half, 0.f);
		Scene.World.Tick(30);
		TestNearlyEqual(TEXT("settled: the server has the aim"), Fishing->GetServerFightInput().RodYaw, 0.5f, 1.f / 127.f);
		const uint8 FightId = Fishing->GetFightNet().FightId;
		int32 Repairs = 0, Stale = 0, LongestStale = 0;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Fishing->AuthoritySetFightInput(FightId, 0.f, 0.f, INDEX_NONE); // as if the owner's last packet had been lost
			Scene.World.Tick(1);
			if (FMath::IsNearlyEqual(Fishing->GetServerFightInput().RodYaw, 0.5f, 1.f / 127.f))
			{
				++Repairs;
				Stale = 0;
			}
			else
			{
				LongestStale = FMath::Max(LongestStale, ++Stale);
			}
		}
		const float Dt = LureFightQA::WorldDt;
		const int32 Most = FMath::CeilToInt(2.f / Settings->FightInputResendSeconds) + 1;
		const int32 Least = FMath::FloorToInt(2.f / (Settings->FightInputResendSeconds + 2.f * Dt)) - 1;
		TestTrue(FString::Printf(TEXT("2 s of a stale server copy, the owner still: %d repairs (between %d and %d: every %.2f s)"), Repairs, Least, Most, Settings->FightInputResendSeconds),
			Repairs >= Least && Repairs <= Most);
		TestTrue(FString::Printf(TEXT("the longest the server fights with the stale aim: %d frames (<= %d)"), LongestStale, FMath::CeilToInt(Settings->FightInputResendSeconds / Dt) + 1),
			LongestStale <= FMath::CeilToInt(Settings->FightInputResendSeconds / Dt) + 1);
		return true;
	}

	// =================================================================================================================
	// The camera follows the fish and the rod, then lets go without a snap
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQACamera, "Project.Fishing.Fight.Rod.QA.Camera.FollowsTheFishAndTheRodThenLetsGo", Flags)
	bool FRodQACamera::RunTest(const FString& Parameters)
	{
		RodWorld::FScene Scene;
		if (!Scene.Create(*this, RodWorld::SteadyPattern()) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
		{
			return false;
		}
		FOwner& Owner = Scene.Owner;
		ULureFishingComponent* Fishing = Owner.Fishing;
		const FLureFishFightRow& T = Fishing->GetFightTuning();
		const UCameraComponent* Camera = Owner.Character->GetFirstPersonCamera();
		if (!TestNotNull(TEXT("first-person camera"), Camera))
		{
			return false;
		}
		auto AtFish = [&]() { return (Fishing->GetBobberLocation() - Camera->GetComponentLocation()).Rotation(); };
		auto View = [&]() { return Owner.Controller->GetControlRotation().GetNormalized(); };

		// Looking away when the fish is hooked: the view eases back onto the fish, never swinging away from it.
		Owner.Controller->SetControlRotation(FRotator(20.f, 90.f, 0.f));
		float Start = -1.f, Previous = 1000.f;
		bool bMonotone = true;
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			Scene.World.Tick(1);
			const float Error = FMath::Abs(static_cast<float>(FRotator::NormalizeAxis(View().Yaw - AtFish().Yaw)));
			Start = Start < 0.f ? Error : Start;
			bMonotone &= Error <= Previous + 1.0e-3f;
			Previous = Error;
		}
		TestTrue(TEXT("the view eases toward the fish without overshooting"), bMonotone);
		TestTrue(FString::Printf(TEXT("after 1.5 s the view is on the fish (%.2f deg off, from %.1f)"), Previous, Start), Previous < 0.05f * Start);

		Owner.Character->DoLook(T.RodAimSideDeg, 0.f); // the rod fully right
		Scene.World.Tick(120);
		TestNearlyEqual(TEXT("rod fully right: the view turns RodAimSideDeg x CameraRodYawShare right of the fish"), static_cast<float>(FRotator::NormalizeAxis(View().Yaw - AtFish().Yaw)),
			T.RodAimSideDeg * T.CameraRodYawShare, 1.5f);
		Owner.Character->DoLook(0.f, T.RodAimUpDeg); // ... and fully back
		Scene.World.Tick(120);
		TestNearlyEqual(TEXT("rod fully back: the view lifts RodAimUpDeg x CameraRodPitchShare above the fish"), static_cast<float>(FRotator::NormalizeAxis(View().Pitch - AtFish().Pitch)),
			T.RodAimUpDeg * T.CameraRodPitchShare, 1.5f);

		const FRotator Before = View();
		Fishing->RequestReelIn();
		Scene.World.Tick(1);
		TestTrue(FString::Printf(TEXT("the fight's end does not snap the view (%s -> %s)"), *Before.ToString(), *View().ToString()), View().Equals(Before, 0.01f));
		TestTrue(TEXT("after the fight nothing holds the camera"), Owner.ViewIsFree(Scene.World));
		return true;
	}

	// =================================================================================================================
	// HUD lines
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAHudLines, "Project.Fishing.Fight.Rod.QA.Hud.LinesFollowTheFight", Flags)
	bool FRodQAHudLines::RunTest(const FString& Parameters)
	{
		{
			RodWorld::FScene Scene;
			if (!Scene.Create(*this, RodWorld::SteadyPattern(-1.f)) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
			{
				return false;
			}
			ULureFishingComponent* Fishing = Scene.Owner.Fishing;
			ALurePlayerCharacter* Character = Scene.Owner.Character;
			const FLureFishFightRow& T = Fishing->GetFightTuning();
			Scene.World.Tick(2);
			TestEqual(TEXT("level, centered"), FindLine(Fishing->GetStatusText(), TEXT("Rod: ")), FString(TEXT("Rod: level")));
			TestEqual(TEXT("the default step"), FindLine(Fishing->GetStatusText(), TEXT("Reel ")), FString(TEXT("Reel 2/3 (wheel or LB/RB)")));
			TestTrue(TEXT("a left run: the hint"), HasLine(Fishing->GetStatusText(), TEXT("Fish runs LEFT: pull right")));
			Character->DoLook(T.RodAimSideDeg, 0.f);
			TestEqual(TEXT("rod right against the left run"), FindLine(Fishing->GetStatusText(), TEXT("Rod: ")), FString(TEXT("Rod: level-right  (turning it)")));
			Character->DoLook(-2.f * T.RodAimSideDeg, 0.f);
			TestEqual(TEXT("rod left, with the run"), FindLine(Fishing->GetStatusText(), TEXT("Rod: ")), FString(TEXT("Rod: level-left  (same way: losing line)")));
			Character->DoLook(T.RodAimSideDeg + T.RodAimSideDeg / 3.f, 0.f);
			TestEqual(TEXT("a third of the way right is still level, and not yet turning"), FindLine(Fishing->GetStatusText(), TEXT("Rod: ")), FString(TEXT("Rod: level")));
			Character->DoLook(-T.RodAimSideDeg / 3.f, T.RodAimUpDeg);
			TestEqual(TEXT("fully back, centered"), FindLine(Fishing->GetStatusText(), TEXT("Rod: ")), FString(TEXT("Rod: back")));
			Character->DoLook(0.f, -T.RodAimUpDeg - T.RodAimDownDeg);
			TestEqual(TEXT("fully dipped"), FindLine(Fishing->GetStatusText(), TEXT("Rod: ")), FString(TEXT("Rod: dipped")));
			Fishing->StepReelSpeed(1);
			TestEqual(TEXT("one step faster"), FindLine(Fishing->GetStatusText(), TEXT("Reel ")), FString(TEXT("Reel 3/3 (wheel or LB/RB)")));
			Fishing->RequestReelIn();
			Scene.World.Tick(2);
			const FString After = Fishing->GetStatusText();
			TestFalse(TEXT("after the fight: no rod line"), HasLine(After, TEXT("Rod: ")));
			TestFalse(TEXT("after the fight: no reel line"), HasLine(After, TEXT("Reel ")));
			TestFalse(TEXT("after the fight: no run hint"), HasLine(After, TEXT("Fish runs")));
		}
		{
			RodWorld::FScene Scene;
			if (!Scene.Create(*this, RodWorld::SteadyPattern(0.f)) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
			{
				return false;
			}
			Scene.World.Tick(2);
			Scene.Owner.Character->DoLook(Scene.Owner.Fishing->GetFightTuning().RodAimSideDeg, 0.f);
			const FString Text = Scene.Owner.Fishing->GetStatusText();
			TestFalse(TEXT("a straight fish: no run hint"), HasLine(Text, TEXT("Fish runs")));
			TestEqual(TEXT("a straight fish: the rod line has no turning note"), FindLine(Text, TEXT("Rod: ")), FString(TEXT("Rod: level-right")));
		}
		return true;
	}

	// =================================================================================================================
	// Signs: left is left for the sim, the HUD, the fish on screen, the mouse and the camera
	// =================================================================================================================

	/**
	 *  A fish swimming to one side (a Side -1 move = LEFT, +1 = RIGHT): the bobber moves to the player's left (right), the HUD says so, the
	 *  camera follows it that way; doing what the hint says (the mouse the other way) is "turning it" for the HUD and the fight, and the fish
	 *  swings back toward the middle.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASigns, "Project.Fishing.Fight.Rod.QA.Signs.RunSideMatchesWhatThePlayerSees", Flags)
	bool FRodQASigns::RunTest(const FString& Parameters)
	{
		for (const float Side : { -1.f, 1.f })
		{
			const bool bLeft = Side < 0.f;
			const FString Run = bLeft ? TEXT("left run") : TEXT("right run");
			RodWorld::FScene Scene;
			if (!Scene.Create(*this, OneMove(SideMove(TEXT("Run"), 1.f, 1.f, 0.f, Side))) || !HookAndFight(*this, Scene.World, Scene.Owner.Fishing, Scene.Bonefish))
			{
				return false;
			}
			FOwner& Owner = Scene.Owner;
			ULureFishingComponent* Fishing = Owner.Fishing;
			const FVector Player = Owner.Character->GetActorLocation();
			const FVector Facing = (FVector(Fishing->GetNetState().BobberRest) - Player).GetSafeNormal2D();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
			Scene.World.Tick(90);
			const float Offset = static_cast<float>(FVector::DotProduct(Fishing->GetBobberLocation() - Owner.Character->GetActorLocation(), Right));
			TestTrue(FString::Printf(TEXT("%s: the fish is on the player's %s (%.0f cm)"), *Run, bLeft ? TEXT("left") : TEXT("right"), Offset), bLeft ? Offset < -10.f : Offset > 10.f);
			TestTrue(FString::Printf(TEXT("%s: the HUD says so"), *Run), HasLine(Fishing->GetStatusText(), bLeft ? TEXT("Fish runs LEFT: pull right") : TEXT("Fish runs RIGHT: pull left")));
			const float ViewYaw = static_cast<float>(FRotator::NormalizeAxis(Owner.Controller->GetControlRotation().Yaw - Facing.Rotation().Yaw));
			TestTrue(FString::Printf(TEXT("%s: the camera follows the fish that way (%.1f deg)"), *Run, ViewYaw), bLeft ? ViewYaw < -1.f : ViewYaw > 1.f);

			const float SwingBefore = Fishing->GetFightState().SideDeg;
			Owner.Character->DoLook(bLeft ? 45.f : -45.f, 0.f); // what the hint says
			Scene.World.Tick(6); // the aim goes out within FightInputSendSeconds; the server steps with it the frame after
			TestTrue(FString::Printf(TEXT("%s: the mouse the hint's way = 'turning it'"), *Run), FindLine(Fishing->GetStatusText(), TEXT("Rod: ")).Contains(TEXT("(turning it)")));
			TestTrue(FString::Printf(TEXT("%s: ... and the fight scores it against the run (+1)"), *Run), Fishing->GetFightState().Side == 1.f);
			Scene.World.Tick(30);
			const float SwingTurned = Fishing->GetFightState().SideDeg;
			TestTrue(FString::Printf(TEXT("%s: turned, the fish swings back toward the middle (%.2f -> %.2f deg)"), *Run, SwingBefore, SwingTurned),
				bLeft ? SwingTurned > SwingBefore : SwingTurned < SwingBefore);
			Scene.World.Tick(240);
			AddInfo(FString::Printf(TEXT("%s held turned 4 s more: the swing went %.1f -> %.1f deg (%s the middle); the HUD still says '%s'"), *Run, SwingBefore,
				Fishing->GetFightState().SideDeg, (Fishing->GetFightState().SideDeg * SwingBefore < 0.f) ? TEXT("past") : TEXT("not past"),
				*FindLine(Fishing->GetStatusText(), TEXT("Fish runs"))));
			Fishing->RequestReelIn();
			Scene.World.Tick(2);
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

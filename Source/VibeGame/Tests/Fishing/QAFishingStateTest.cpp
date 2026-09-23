// Lure T-006 QA (qa-engineer): the fishing state machine and every interrupt at every stage.
// Project.Fishing.QA.State.* and Project.Fishing.QA.Interrupt.* - spec: docs/specs/fishing-rules.md "Flow and authority", "Cast"
// (no casting while ...; a line out comes in on its own when ...; jumping keeps the line) and lead decision 3.
// Stages: Charging (owning client), Casting, Waiting, Nibble (a nibble in progress), Biting, Hooked.

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

// Everything lives in namespace QAFishing (unity builds merge test files; other files use global using-directives).
namespace QAFishing
{

namespace StateLocal
{
	/** One player, possessed by a local controller (standalone = the owning client and the server), at the shore spot. */
	struct FRig
	{
		FScene Scene;
		ALurePlayerCharacter* Character = nullptr;
		ULureFishingComponent* Fishing = nullptr;
		APlayerController* Controller = nullptr;
		FLureFishingRow Profile;
		AActor* Spot = nullptr;

		bool Make(FAutomationTestBase& Test, bool bProne = false)
		{
			FLureFishingRow Shipped;
			if (!ShippedFishingRow(Test, Shipped) || !Scene.Create(Test))
			{
				return false;
			}
			Spot = Scene.AddShoreSpot();
			Character = Scene.Spawn(Test);
			Profile = StageProfile(Shipped);
			Fishing = Scene.SetUpFishing(Test, Character, Profile);
			if (!Fishing)
			{
				return false;
			}
			Controller = Scene.PossessLocally(Character);
			Scene.Tick(10);
			if (bProne)
			{
				Character->RequestStance(ELureStance::Prone);
				Scene.Tick(45);
				if (!Test.TestEqual(TEXT("QA: prone"), static_cast<int32>(Character->GetStance()), static_cast<int32>(ELureStance::Prone)))
				{
					return false;
				}
			}
			return true;
		}
	};

	struct FSnapshot
	{
		FLureFishingNetState Net;
		int32 HookedSeed = 0;
		bool bCharging = false;
	};

	FSnapshot SnapLine(const ULureFishingComponent* Fishing)
	{
		FSnapshot Snapshot;
		Snapshot.Net = Fishing->GetNetState();
		Snapshot.HookedSeed = Fishing->GetHookedFish().Seed;
		Snapshot.bCharging = Fishing->IsCharging();
		return Snapshot;
	}

	/** The interrupt ended the stage: the charge is dropped (no cast), or the line came in with Reason (a hooked fish is lost). */
	void ExpectEnded(FAutomationTestBase& Test, const FString& Label, FRig& Rig, EStage Stage, ELureCastBlock Reason, const FSnapshot& Before)
	{
		ULureFishingComponent* Fishing = Rig.Fishing;
		if (Stage == EStage::Charging)
		{
			Test.TestFalse(Label + TEXT(": the charge is dropped"), Fishing->IsCharging());
			Fishing->ReleaseCast();
			Test.TestEqual(Label + TEXT(": releasing afterwards casts nothing"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
			Test.TestEqual(Label + TEXT(": no cast id used"), Fishing->GetNetState().CastId, Before.Net.CastId);
			return;
		}
		const FLureFishingNetState& Net = Fishing->GetNetState();
		Test.TestEqual(Label + TEXT(": the line comes in"), StateName(Net.State), StateName(ELureFishingState::Idle));
		Test.TestEqual(Label + TEXT(": result"), ResultName(Net.LastResult), ResultName(Stage == EStage::Hooked ? ELureFishingResult::Lost : ELureFishingResult::ReeledIn));
		Test.TestEqual(Label + TEXT(": reason"), BlockName(Net.ResultReason), BlockName(Reason));
		Test.TestFalse(Label + TEXT(": no fish on the line"), Fishing->GetHookedFish().IsValid());
		Test.TestFalse(Label + TEXT(": no fish pending on the server"), Fishing->GetPendingFish().IsValid());
		Test.TestFalse(Label + TEXT(": nothing landed"), Fishing->GetLastLandedFish().IsValid());
		Test.TestTrue(Label + TEXT(": no bite scheduled"), Fishing->GetScheduledBiteTime() < 0.0);
	}

	/** The interrupt did not end the stage: the charge goes on, or the same line is still out (a hooked fish is kept). */
	void ExpectContinues(FAutomationTestBase& Test, const FString& Label, FRig& Rig, EStage Stage, const FSnapshot& Before)
	{
		ULureFishingComponent* Fishing = Rig.Fishing;
		if (Stage == EStage::Charging)
		{
			if (Test.TestTrue(Label + TEXT(": still charging"), Fishing->IsCharging()))
			{
				Fishing->ReleaseCast();
				Test.TestEqual(Label + TEXT(": the release casts"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Casting));
			}
			return;
		}
		const FLureFishingNetState& Net = Fishing->GetNetState();
		Test.TestTrue(Label + TEXT(": the line is still out"), Net.State != ELureFishingState::Idle);
		Test.TestEqual(Label + TEXT(": the same cast"), Net.CastId, Before.Net.CastId);
		Test.TestTrue(Label + TEXT(": no reel-in or loss"), Net.LastResult != ELureFishingResult::ReeledIn && Net.LastResult != ELureFishingResult::Lost);
		if (Stage == EStage::Biting && Net.State == ELureFishingState::Biting)
		{
			Fishing->AuthorityHook();
			Test.TestEqual(Label + TEXT(": the bite can still be hooked"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
		}
		if (Stage == EStage::Hooked)
		{
			Test.TestEqual(Label + TEXT(": still hooked"), StateName(Net.State), StateName(ELureFishingState::Hooked));
			Test.TestEqual(Label + TEXT(": the same fish"), Fishing->GetHookedFish().Seed, Before.HookedSeed);
		}
	}

	/** Runs Interrupt at every stage in a fresh rig; Check decides what must hold after it. */
	void ForEachStage(FAutomationTestBase& Test, bool bProne, TFunctionRef<void(FRig&, EStage)> Interrupt, TFunctionRef<void(FRig&, EStage, const FSnapshot&)> Check,
		const TArray<EStage>& Stages = AllStages())
	{
		for (const EStage Stage : Stages)
		{
			FRig Rig;
			if (!Rig.Make(Test, bProne) || !DriveToStage(Test, Rig.Scene, Rig.Character, Rig.Fishing, Stage))
			{
				continue;
			}
			const FSnapshot Before = SnapLine(Rig.Fishing);
			Interrupt(Rig, Stage);
			Check(Rig, Stage, Before);
			if (IsValid(Rig.Character))
			{
				Rig.Character->SetSprintRequested(false);
			}
		}
	}

	/** Moves with sprint held until the line (or charge) is gone, at most Frames. */
	void SprintAway(FRig& Rig, int32 Frames = 40)
	{
		Rig.Character->SetSprintRequested(true);
		for (int32 Frame = 0; Frame < Frames && (Rig.Fishing->IsLineOut() || Rig.Fishing->IsCharging()); ++Frame)
		{
			Rig.Scene.TickMoving(Rig.Character, -FVector::ForwardVector, 1);
		}
	}
}

namespace StateLocal
{

// =====================================================================================================================
// The happy path, in order
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishStateFullSequenceInOrder, "Project.Fishing.QA.State.FullSequenceInOrder", QAFishing::Flags)
bool FQAFishStateFullSequenceInOrder::RunTest(const FString& Parameters)
{
	// idle -> charging (client only) -> cast -> waiting -> nibble -> bite -> hooked -> landed (idle), nothing skipped, nothing extra.
	FRig Rig;
	if (!Rig.Make(*this))
	{
		return false;
	}
	FLureFishingRow Profile = Rig.Profile;
	Profile.CastFlightTimeMin = Profile.CastFlightTimeMax = 0.5f;
	Profile.BiteWaitMin = Profile.BiteWaitMax = 2.5f;
	Profile.NibblesMin = Profile.NibblesMax = 1;
	Profile.HookWindow = 1.f;
	Profile.AutoLandDelay = 0.5f;
	Rig.Fishing->SetFishingProfile(Profile);
	ULureFishingComponent* Fishing = Rig.Fishing;

	TArray<ELureFishingState> Sequence = { Fishing->GetFishingState() };
	auto Record = [&Sequence, Fishing]()
	{
		if (Sequence.Last() != Fishing->GetFishingState())
		{
			Sequence.Add(Fishing->GetFishingState());
		}
	};
	const FLureFishingNetState Start = Fishing->GetNetState();

	Fishing->PressCast();
	bool bIdleWhileCharging = true;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Rig.Scene.Tick(1);
		bIdleWhileCharging &= Fishing->GetFishingState() == ELureFishingState::Idle && Fishing->IsCharging();
		Record();
	}
	TestTrue(TEXT("charging is the owning client's: the server state stays Idle while the button is held"), bIdleWhileCharging);
	TestTrue(TEXT("HUD: cast power while charging"), Fishing->GetStatusText().Contains(TEXT("Cast power")));
	Fishing->ReleaseCast();
	Record();
	TestEqual(TEXT("one cast id per cast"), Fishing->GetNetState().CastId, static_cast<uint8>(Start.CastId + 1));
	uint8 NibblesWhileWaiting = 0;
	uint8 NibblesOtherwise = 0;
	uint8 LastNibble = Fishing->GetNetState().NibbleId;
	for (int32 Frame = 0; Frame < 600 && Fishing->GetFishingState() != ELureFishingState::Biting; ++Frame)
	{
		Rig.Scene.Tick(1);
		const uint8 Nibble = Fishing->GetNetState().NibbleId;
		(Fishing->GetFishingState() == ELureFishingState::Waiting ? NibblesWhileWaiting : NibblesOtherwise) += static_cast<uint8>(Nibble - LastNibble);
		LastNibble = Nibble;
		Record();
	}
	TestEqual(TEXT("one nibble, while waiting"), static_cast<int32>(NibblesWhileWaiting), 1);
	TestEqual(TEXT("no nibble outside waiting"), static_cast<int32>(NibblesOtherwise), 0);
	TestTrue(TEXT("HUD: bite prompt"), Fishing->GetStatusText().Contains(TEXT("BITE")));
	Fishing->PressCast(); // the fishing button hooks while the line is out
	Record();
	const uint8 HookResultId = Fishing->GetNetState().ResultId;
	TestEqual(TEXT("result Hooked"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Hooked));
	Rig.Scene.TickUntil([Fishing, &Record]() { Record(); return Fishing->GetFishingState() == ELureFishingState::Idle; }, 120);
	Record();
	TestEqual(TEXT("result Landed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
	TestEqual(TEXT("one result per event"), Fishing->GetNetState().ResultId, static_cast<uint8>(HookResultId + 1));

	const TArray<ELureFishingState> Expected = { ELureFishingState::Idle, ELureFishingState::Casting, ELureFishingState::Waiting, ELureFishingState::Biting,
		ELureFishingState::Hooked, ELureFishingState::Idle };
	TArray<FString> Got;
	for (const ELureFishingState State : Sequence)
	{
		Got.Add(StateName(State));
	}
	TArray<FString> Want;
	for (const ELureFishingState State : Expected)
	{
		Want.Add(StateName(State));
	}
	TestEqual(TEXT("the states, in order"), FString::Join(Got, TEXT(" > ")), FString::Join(Want, TEXT(" > ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishStateCastingLastsTheFlightTime, "Project.Fishing.QA.State.CastingLastsTheFlightTime", QAFishing::Flags)
bool FQAFishStateCastingLastsTheFlightTime::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!Rig.Make(*this))
	{
		return false;
	}
	FLureFishingRow Profile = Rig.Profile;
	Profile.CastSpeed = 500.f; // 300..800 cm at 500 cm/s: 0.6-1.6 s, clamped to [0.7, 1.2]
	Profile.CastFlightTimeMin = 0.7f;
	Profile.CastFlightTimeMax = 1.2f;
	Rig.Fishing->SetFishingProfile(Profile);
	for (const float Charge : { 0.f, 1.f })
	{
		ULureFishingComponent* Fishing = Rig.Fishing;
		TestTrue(TEXT("cast"), Fishing->AuthorityCast(Charge, 0.f));
		const FLureFishingNetState& Net = Fishing->GetNetState();
		const float Expected = FLureFishingRules::CastFlightTime(Profile, static_cast<float>(FVector::Dist(Net.CastOrigin, Net.BobberRest)));
		const FString Label = FString::Printf(TEXT("charge %.0f"), Charge);
		TestNearlyEqual(Label + TEXT(": the flight time is the data's (distance / speed, clamped)"), Net.FlightTime, Expected, 1.0e-4f);
		TestTrue(Label + TEXT(": ... inside [0.7, 1.2]"), Net.FlightTime >= 0.7f - 1.0e-4f && Net.FlightTime <= 1.2f + 1.0e-4f);
		const double Start = Net.StateStartTime;
		Rig.Scene.AdvanceTo(Start + Net.FlightTime - 2.0 * Dt);
		TestEqual(Label + TEXT(": still flying just before the flight time"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Casting));
		TestTrue(Label + TEXT(": the bobber is above its rest point in flight"), Fishing->GetBobberLocation().Z > Fishing->GetNetState().BobberRest.Z);
		Rig.Scene.AdvanceTo(Start + Net.FlightTime + Dt);
		TestEqual(Label + TEXT(": landed right after the flight time"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
		Fishing->AuthorityReelIn();
		Rig.Scene.Tick(2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishStateReleasedChargeIsTheCastCharge, "Project.Fishing.QA.State.ReleasedChargeIsTheCastCharge", QAFishing::Flags)
bool FQAFishStateReleasedChargeIsTheCastCharge::RunTest(const FString& Parameters)
{
	// Hold to charge (ChargeTime from data), release to cast: the distance follows the charge held.
	FRig Rig;
	if (!Rig.Make(*this))
	{
		return false;
	}
	FLureFishingRow Profile = Rig.Profile;
	Profile.ChargeTime = 1.5f;
	Rig.Fishing->SetFishingProfile(Profile);
	ULureFishingComponent* Fishing = Rig.Fishing;
	for (const int32 Frames : { 0, 30, 60, 90, 180 })
	{
		const FVector Eye = Rig.Character->GetPawnViewLocation();
		Fishing->PressCast();
		Rig.Scene.Tick(Frames);
		const float Held = Frames * Dt;
		const float ExpectedCharge = FMath::Clamp(Held / Profile.ChargeTime, 0.f, 1.f);
		const FString Label = FString::Printf(TEXT("held %.2f s"), Held);
		TestNearlyEqual(Label + TEXT(": charge = held / ChargeTime"), Fishing->GetCharge(), ExpectedCharge, 0.02f);
		Fishing->ReleaseCast();
		TestFalse(Label + TEXT(": released: not charging"), Fishing->IsCharging());
		if (!TestEqual(Label + TEXT(": released: casting"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Casting)))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Eye, Fishing->GetNetState().BobberRest));
		const float Expected = FLureFishingRules::CastDistance(Profile, ExpectedCharge);
		TestNearlyEqual(FString::Printf(TEXT("%s: lands %.0f cm out (got %.1f)"), *Label, Expected, Distance), Distance, Expected, 25.f);
		Fishing->AuthorityReelIn();
		Rig.Scene.Tick(2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishStateCastButtonPerState, "Project.Fishing.QA.State.CastButtonPerState", QAFishing::Flags)
bool FQAFishStateCastButtonPerState::RunTest(const FString& Parameters)
{
	// Spec (Controls): hold to charge, release to cast; while the line is out a press hooks (or reels in early).
	for (const EStage Stage : { EStage::Casting, EStage::Waiting, EStage::Biting, EStage::Hooked })
	{
		FRig Rig;
		if (!Rig.Make(*this) || !DriveToStage(*this, Rig.Scene, Rig.Character, Rig.Fishing, Stage))
		{
			continue;
		}
		ULureFishingComponent* Fishing = Rig.Fishing;
		const FLureFishingNetState Before = Fishing->GetNetState();
		Fishing->PressCast();
		const FString Label = TEXT("press during ") + StageName(Stage);
		TestFalse(Label + TEXT(": never starts a new charge while the line is out"), Fishing->IsCharging());
		switch (Stage)
		{
		case EStage::Waiting:
			TestEqual(Label + TEXT(": early press = reel in (EarlyHook ReelIn)"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Idle));
			break;
		case EStage::Biting:
			TestEqual(Label + TEXT(": hooks"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
			break;
		default:
			TestEqual(Label + TEXT(": no change"), StateName(Fishing->GetFishingState()), StateName(Before.State));
			TestEqual(Label + TEXT(": no result"), Fishing->GetNetState().ResultId, Before.ResultId);
			break;
		}
		Fishing->ReleaseCast();
		TestTrue(Label + TEXT(": the release casts nothing new"), Fishing->GetNetState().CastId == Before.CastId);
	}
	return true;
}

// =====================================================================================================================
// Interrupts at every stage
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptStanceChangesKeepTheLine, "Project.Fishing.QA.Interrupt.StanceChangesKeepTheLine", QAFishing::Flags)
bool FQAFishInterruptStanceChangesKeepTheLine::RunTest(const FString& Parameters)
{
	// Crouching, lying down and standing up again (without moving) never end a cast: prone fishing is allowed (Jimmy 2026-09-22).
	ForEachStage(*this, false,
		[](FRig& Rig, EStage)
		{
			for (const ELureStance Stance : { ELureStance::Crouch, ELureStance::Prone, ELureStance::Crouch, ELureStance::Stand })
			{
				Rig.Character->RequestStance(Stance);
				Rig.Scene.Tick(12);
			}
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before)
		{
			TestEqual(StageName(Stage) + TEXT(": QA precondition, standing again"), static_cast<int32>(Rig.Character->GetStance()), static_cast<int32>(ELureStance::Stand));
			ExpectContinues(*this, StageName(Stage) + TEXT(" + crouch/prone/stand"), Rig, Stage, Before);
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptSprintEndsEveryStage, "Project.Fishing.QA.Interrupt.SprintEndsEveryStage", QAFishing::Flags)
bool FQAFishInterruptSprintEndsEveryStage::RunTest(const FString& Parameters)
{
	// Spec: no casting while sprinting (DT_Movement Sprint CanFish = False); a line out comes in when you start sprinting.
	ForEachStage(*this, false,
		[](FRig& Rig, EStage) { SprintAway(Rig); },
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before)
		{
			TestTrue(StageName(Stage) + TEXT(": QA precondition, sprinting"), Rig.Character->IsSprinting());
			ExpectEnded(*this, StageName(Stage) + TEXT(" + sprint"), Rig, Stage, ELureCastBlock::Sprinting, Before);
			if (Stage != EStage::Charging)
			{
				TestTrue(StageName(Stage) + TEXT(" + sprint: HUD says why"), Rig.Fishing->GetStatusText().Contains(TEXT("sprinting")));
			}
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptSprintHeldStandingStillKeepsTheLine, "Project.Fishing.QA.Interrupt.SprintHeldStandingStillKeepsTheLine", QAFishing::Flags)
bool FQAFishInterruptSprintHeldStandingStillKeepsTheLine::RunTest(const FString& Parameters)
{
	// Holding sprint without moving is not sprinting (movement A2), so the line stays.
	ForEachStage(*this, false,
		[](FRig& Rig, EStage)
		{
			Rig.Character->SetSprintRequested(true);
			Rig.Scene.Tick(20);
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before)
		{
			TestFalse(StageName(Stage) + TEXT(": QA precondition, not sprinting"), Rig.Character->IsSprinting());
			ExpectContinues(*this, StageName(Stage) + TEXT(" + sprint held, standing still"), Rig, Stage, Before);
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptProneCrawlEndsEveryStage, "Project.Fishing.QA.Interrupt.ProneCrawlEndsEveryStage", QAFishing::Flags)
bool FQAFishInterruptProneCrawlEndsEveryStage::RunTest(const FString& Parameters)
{
	// Spec: prone and still casts; crawling tucks the rod (DT_Movement Prone RodPoseMoving = ProneTuck), so a line out comes in.
	ForEachStage(*this, /*bProne*/ true,
		[](FRig& Rig, EStage)
		{
			for (int32 Frame = 0; Frame < 30 && (Rig.Fishing->IsLineOut() || Rig.Fishing->IsCharging()); ++Frame)
			{
				Rig.Scene.TickMoving(Rig.Character, -FVector::ForwardVector, 1);
			}
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before)
		{
			TestEqual(StageName(Stage) + TEXT(": QA precondition, still prone"), static_cast<int32>(Rig.Character->GetStance()), static_cast<int32>(ELureStance::Prone));
			ExpectEnded(*this, StageName(Stage) + TEXT(" + crawl"), Rig, Stage, ELureCastBlock::RodTucked, Before);
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptProneStillKeepsEveryStage, "Project.Fishing.QA.Interrupt.ProneStillKeepsEveryStage", QAFishing::Flags)
bool FQAFishInterruptProneStillKeepsEveryStage::RunTest(const FString& Parameters)
{
	// Lying still (the whole cast made while prone) keeps every stage going.
	ForEachStage(*this, /*bProne*/ true,
		[](FRig& Rig, EStage) { Rig.Scene.Tick(20); },
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before) { ExpectContinues(*this, StageName(Stage) + TEXT(" while lying still"), Rig, Stage, Before); });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptSwimEndsEveryStage, "Project.Fishing.QA.Interrupt.SwimEndsEveryStage", QAFishing::Flags)
bool FQAFishInterruptSwimEndsEveryStage::RunTest(const FString& Parameters)
{
	// Swim stub: the engine's swimming mode (T-026 adds water volumes). The fishing component is ticked right after the switch,
	// before the movement component could leave swimming again (there is no water volume in the test world).
	ForEachStage(*this, false,
		[](FRig& Rig, EStage)
		{
			Rig.Character->GetCharacterMovement()->SetMovementMode(MOVE_Swimming);
			Rig.Fishing->TickComponent(Dt, LEVELTICK_All, nullptr);
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before)
		{
			TestFalse(StageName(Stage) + TEXT(" + swim: the rod is not in hand"), Rig.Fishing->IsRodInHand());
			ExpectEnded(*this, StageName(Stage) + TEXT(" + swim"), Rig, Stage, ELureCastBlock::Swimming, Before);
			Rig.Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptJumpKeepsTheLine, "Project.Fishing.QA.Interrupt.JumpKeepsTheLine", QAFishing::Flags)
bool FQAFishInterruptJumpKeepsTheLine::RunTest(const FString& Parameters)
{
	// Spec: jumping keeps the line; no casting in the air.
	ForEachStage(*this, false,
		[this](FRig& Rig, EStage Stage)
		{
			Rig.Character->Jump();
			const bool bAirborne = Rig.Scene.TickUntil([&Rig]() { return Rig.Character->GetCharacterMovement()->IsFalling(); }, 10);
			Rig.Character->StopJumping();
			TestTrue(StageName(Stage) + TEXT(": QA precondition, in the air"), bAirborne);
			if (Stage == EStage::Charging)
			{
				Rig.Fishing->ReleaseCast(); // released in the air
			}
			Rig.Scene.TickUntil([&Rig]() { return !Rig.Character->GetCharacterMovement()->IsFalling(); }, 120);
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before)
		{
			if (Stage == EStage::Charging)
			{
				TestEqual(TEXT("Charging + jump: nothing was cast from the air"), Rig.Fishing->GetNetState().CastId, Before.Net.CastId);
				TestFalse(TEXT("Charging + jump: not charging any more"), Rig.Fishing->IsCharging());
				return;
			}
			ExpectContinues(*this, StageName(Stage) + TEXT(" + jump"), Rig, Stage, Before);
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptRodPutAwayEndsEveryStage, "Project.Fishing.QA.Interrupt.RodPutAwayEndsEveryStage", QAFishing::Flags)
bool FQAFishInterruptRodPutAwayEndsEveryStage::RunTest(const FString& Parameters)
{
	ForEachStage(*this, false,
		[](FRig& Rig, EStage)
		{
			Rig.Fishing->bRodEquipped = false;
			Rig.Scene.Tick(2);
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before)
		{
			ExpectEnded(*this, StageName(Stage) + TEXT(" + rod put away"), Rig, Stage, ELureCastBlock::NoRod, Before);
			Rig.Fishing->bRodEquipped = true;
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptTooFarFromTheBobber, "Project.Fishing.QA.Interrupt.TooFarFromTheBobber", QAFishing::Flags)
bool FQAFishInterruptTooFarFromTheBobber::RunTest(const FString& Parameters)
{
	// Spec: before a fish is hooked, the line comes in when you get farther than MaxLineLength (900 here) from the bobber;
	// just inside it stays. T-007: during the fight (Hooked) the fight's own line rules (SpoolLength) apply instead, so
	// walking past MaxLineLength does not end it (checked below).
	const TArray<EStage> LineStages = { EStage::Casting, EStage::Waiting, EStage::Nibble, EStage::Biting };
	ForEachStage(*this, false,
		[this](FRig& Rig, EStage Stage)
		{
			const FVector Rest = Rig.Fishing->GetNetState().BobberRest;
			const float MaxLine = Rig.Fishing->GetProfile().MaxLineLength;
			const FVector Location = Rig.Character->GetActorLocation();
			Rig.Character->SetActorLocation(FVector(Rest.X - (MaxLine - 10.f), Location.Y, Location.Z), false, nullptr, ETeleportType::TeleportPhysics);
			Rig.Scene.Tick(2);
			TestTrue(StageName(Stage) + TEXT(": 10 cm inside MaxLineLength the line stays"), Rig.Fishing->IsLineOut());
			Rig.Character->SetActorLocation(FVector(Rest.X - (MaxLine + 10.f), Location.Y, Location.Z), false, nullptr, ETeleportType::TeleportPhysics);
			Rig.Scene.Tick(2);
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot& Before) { ExpectEnded(*this, StageName(Stage) + TEXT(" + 10 cm beyond MaxLineLength"), Rig, Stage, ELureCastBlock::TooFar, Before); },
		LineStages);
	ForEachStage(*this, false,
		[](FRig& Rig, EStage)
		{
			const FVector Rest = Rig.Fishing->GetNetState().BobberRest;
			const float MaxLine = Rig.Fishing->GetProfile().MaxLineLength;
			const FVector Location = Rig.Character->GetActorLocation();
			Rig.Character->SetActorLocation(FVector(Rest.X - (MaxLine + 10.f), Location.Y, Location.Z), false, nullptr, ETeleportType::TeleportPhysics);
			Rig.Scene.Tick(2);
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot&)
		{
			TestEqual(StageName(Stage) + TEXT(" + 10 cm beyond MaxLineLength: the fight goes on (no bobber distance rule during a fight)"),
				static_cast<int32>(Rig.Fishing->GetFishingState()), static_cast<int32>(ELureFishingState::Hooked));
			TestTrue(StageName(Stage) + TEXT(": the line is still out"), Rig.Fishing->IsLineOut());
		},
		TArray<EStage>{ EStage::Hooked });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptPawnDestroyedAtEveryStage, "Project.Fishing.QA.Interrupt.PawnDestroyedAtEveryStage", QAFishing::Flags)
bool FQAFishInterruptPawnDestroyedAtEveryStage::RunTest(const FString& Parameters)
{
	// A player leaving (pawn destroyed) at any moment is safe: no errors, and the next player can fish in the same world.
	// The destroyed pawn may be garbage-collected while the world ticks: only a weak pointer may look at it afterwards.
	TWeakObjectPtr<ALurePlayerCharacter> Destroyed;
	ForEachStage(*this, false,
		[&Destroyed](FRig& Rig, EStage)
		{
			if (Rig.Controller)
			{
				Rig.Controller->UnPossess();
			}
			Destroyed = Rig.Character;
			Rig.Character->Destroy();
			Rig.Character = nullptr;
			Rig.Fishing = nullptr;
			Rig.Scene.Tick(90);
		},
		[this, &Destroyed](FRig& Rig, EStage Stage, const FSnapshot&)
		{
			const FString Label = StageName(Stage) + TEXT(" + pawn destroyed");
			TestFalse(Label + TEXT(": the pawn is gone"), Destroyed.IsValid());
			ALurePlayerCharacter* Next = Rig.Scene.Spawn(*this);
			ULureFishingComponent* Fishing = Rig.Scene.SetUpFishing(*this, Next, FlowProfile(Rig.Profile, 0.3f));
			Rig.Scene.Tick(10);
			if (Fishing)
			{
				TestTrue(Label + TEXT(": a new player can cast"), Fishing->AuthorityCast(0.5f, 0.f));
				TestTrue(Label + TEXT(": ... and gets a bite"), Rig.Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240));
			}
			Rig.Character = nullptr;
		});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInterruptSpotLostWhileFishing, "Project.Fishing.QA.Interrupt.SpotLostWhileFishing", QAFishing::Flags)
bool FQAFishInterruptSpotLostWhileFishing::RunTest(const FString& Parameters)
{
	// The spot marker disappears mid-cast (streamed out, deleted): safe at every stage; the next cast sees no spot (markers are
	// read live), so nothing bites there.
	ForEachStage(*this, false,
		[](FRig& Rig, EStage)
		{
			Rig.Spot->Destroy();
			Rig.Spot = nullptr;
			Rig.Scene.Tick(240);
		},
		[this](FRig& Rig, EStage Stage, const FSnapshot&)
		{
			const FString Label = StageName(Stage) + TEXT(" + spot marker removed");
			AddInfo(FString::Printf(TEXT("%s: 4 s later the line is %s (last result %s)"), *Label, *StateName(Rig.Fishing->GetFishingState()),
				*ResultName(Rig.Fishing->GetNetState().LastResult)));
			Rig.Fishing->ReleaseCast();
			Rig.Fishing->AuthorityReelIn();
			Rig.Scene.Tick(2);
			TestTrue(Label + TEXT(": the next cast works"), Rig.Fishing->AuthorityCast(0.f, 0.f));
			Rig.Scene.TickUntil([&Rig]() { return Rig.Fishing->GetFishingState() == ELureFishingState::Waiting; }, 180);
			TestFalse(Label + TEXT(": the next cast finds no spot"), Rig.Fishing->HasCurrentSpot());
			TestTrue(Label + TEXT(": ... so nothing can bite"), Rig.Fishing->GetNetState().bNoFishHere && Rig.Fishing->GetScheduledBiteTime() < 0.0);
		},
		{ EStage::Casting, EStage::Waiting, EStage::Nibble, EStage::Biting, EStage::Hooked });
	return true;
}

} // namespace StateLocal

} // namespace QAFishing

#endif // WITH_DEV_AUTOMATION_TESTS

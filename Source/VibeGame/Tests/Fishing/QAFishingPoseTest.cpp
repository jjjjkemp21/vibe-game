// Lure T-006 QA (qa-engineer): rod pose per stance and motion (DT_Movement columns), and how it gates fishing.
// Project.Fishing.QA.Pose.* - spec: art/export/Characters/SK_FPArms.anim.md "Switch rule", docs/specs/fishing-rules.md "Rod, bobber, line",
// docs/specs/movement-rules.md "Prone fishing"; contract: Character/FPArmsPose.h.

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/PlayerController.h"
#include <limits>

// Everything lives in namespace QAFishing (unity builds merge test files; other files use global using-directives).
namespace QAFishing
{

namespace PoseLocal
{
	EFPArmsPose PoseStep(FLureRodPoseState& State, const FLureMovementRow& Row, float Speed, float Input = 0.f, bool bHolding = true, bool bBlocked = false,
		bool bLineOut = false, float DeltaTime = 1.f / 64.f)
	{
		FLureRodPoseInput In;
		In.bHoldingRod = bHolding;
		In.Speed2D = Speed;
		In.MoveInput = Input;
		In.bHoldBlocked = bBlocked;
		In.bLineOut = bLineOut;
		In.DeltaTime = DeltaTime;
		return FLureRodPose::Step(State, Row, In);
	}

	/** A possessed player on the dock with the shipped (or given) movement table. */
	struct FPoseRig
	{
		FScene Scene;
		ALurePlayerCharacter* Character = nullptr;
		ULureFishingComponent* Fishing = nullptr;
		FLureFishingRow Profile;

		bool Make(FAutomationTestBase& Test, const FString* MovementCsv = nullptr, const FVector& Feet = StandFeet)
		{
			FLureFishingRow Shipped;
			if (!ShippedFishingRow(Test, Shipped) || !Scene.Create(Test, true, MovementCsv))
			{
				return false;
			}
			Scene.AddShoreSpot();
			Character = Scene.Spawn(Test, Feet);
			Profile = FlowProfile(Shipped, 30.f);
			Fishing = Scene.SetUpFishing(Test, Character, Profile);
			if (!Fishing)
			{
				return false;
			}
			Scene.PossessLocally(Character);
			Scene.Tick(10);
			return true;
		}

		EFPArmsPose Pose() const { return Character->GetArmsPose(); }
	};
}

using namespace PoseLocal;

// =====================================================================================================================
// Pure switch rule
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseShippedRowsPerStanceAndMotion, "Project.Fishing.QA.Pose.ShippedRowsPerStanceAndMotion", QAFishing::Flags)
bool FQAFishPoseShippedRowsPerStanceAndMotion::RunTest(const FString& Parameters)
{
	// Spec: Stand/Sprint/Crouch = HoldRod; Prone still = ProneHold, Prone moving = ProneTuck; no rod = Idle.
	TArray<FLureMovementRow> Rows;
	if (!ShippedMovementRows(*this, Rows))
	{
		return false;
	}
	struct FExpect
	{
		ELureMovementState State;
		EFPArmsPose Still;
		EFPArmsPose Moving;
	};
	const FExpect Expectations[] = { { ELureMovementState::Stand, EFPArmsPose::HoldRod, EFPArmsPose::HoldRod },
		{ ELureMovementState::Sprint, EFPArmsPose::HoldRod, EFPArmsPose::HoldRod }, { ELureMovementState::Crouch, EFPArmsPose::HoldRod, EFPArmsPose::HoldRod },
		{ ELureMovementState::Prone, EFPArmsPose::ProneHold, EFPArmsPose::ProneTuck } };
	for (const FExpect& Expect : Expectations)
	{
		const FLureMovementRow& Row = RowOf(Rows, Expect.State);
		const FString Name = FLureMovementData::GetRowName(Expect.State).ToString();
		FLureRodPoseState NoRod;
		TestEqual(Name + TEXT(": no rod = Idle"), PoseName(PoseStep(NoRod, Row, 0.f, 0.f, false)), PoseName(EFPArmsPose::Idle));
		TestEqual(Name + TEXT(": no rod, moving = Idle"), PoseName(PoseStep(NoRod, Row, 300.f, 1.f, false)), PoseName(EFPArmsPose::Idle));
		FLureRodPoseState Still;
		TestEqual(Name + TEXT(": still"), PoseName(PoseStep(Still, Row, 0.f)), PoseName(Expect.Still));
		FLureRodPoseState Moving;
		TestEqual(Name + TEXT(": moving"), PoseName(PoseStep(Moving, Row, Row.RodMoveSpeedIn + 20.f, 1.f)), PoseName(Expect.Moving));
		TestEqual(Name + TEXT(": the still pose is the RodPoseStill column"), PoseName(Row.RodPoseStill), PoseName(Expect.Still));
		TestEqual(Name + TEXT(": the moving pose is the RodPoseMoving column"), PoseName(Row.RodPoseMoving), PoseName(Expect.Moving));
		TestEqual(Name + TEXT(": a tucked pose is only ever the moving prone pose"), FLureRodPose::IsTucked(Expect.Moving), Expect.State == ELureMovementState::Prone);
	}
	TestTrue(TEXT("IsTucked: only ProneTuck"), FLureRodPose::IsTucked(EFPArmsPose::ProneTuck) && !FLureRodPose::IsTucked(EFPArmsPose::ProneHold)
		&& !FLureRodPose::IsTucked(EFPArmsPose::HoldRod) && !FLureRodPose::IsTucked(EFPArmsPose::Idle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseSwitchHysteresisEdges, "Project.Fishing.QA.Pose.SwitchHysteresisEdges", QAFishing::Flags)
bool FQAFishPoseSwitchHysteresisEdges::RunTest(const FString& Parameters)
{
	// Switch rule: still -> moving at once when Speed2D > RodMoveSpeedIn or move input > 0.2; moving -> still after RodStillDelay
	// below RodMoveSpeedOut with no input. Times use 1/16 s steps (exact in binary).
	TArray<FLureMovementRow> Rows;
	if (!ShippedMovementRows(*this, Rows))
	{
		return false;
	}
	FLureMovementRow Row = RowOf(Rows, ELureMovementState::Prone);
	Row.RodMoveSpeedIn = 20.f;
	Row.RodMoveSpeedOut = 6.f;
	Row.RodStillDelay = 0.25f;
	const float Step16 = 1.f / 16.f;
	{
		FLureRodPoseState State;
		TestEqual(TEXT("speed exactly RodMoveSpeedIn: still"), PoseName(PoseStep(State, Row, 20.f)), PoseName(EFPArmsPose::ProneHold));
		TestEqual(TEXT("just above RodMoveSpeedIn: tucked at once"), PoseName(PoseStep(State, Row, 20.01f)), PoseName(EFPArmsPose::ProneTuck));
	}
	{
		FLureRodPoseState State;
		TestEqual(TEXT("move input exactly 0.2: still"), PoseName(PoseStep(State, Row, 0.f, 0.2f)), PoseName(EFPArmsPose::ProneHold));
		TestEqual(TEXT("move input 0.21 (pushing a wall, no speed): tucked"), PoseName(PoseStep(State, Row, 0.f, 0.21f)), PoseName(EFPArmsPose::ProneTuck));
	}
	{
		FLureRodPoseState State;
		PoseStep(State, Row, 100.f, 1.f);
		bool bStayed = true;
		for (int32 Frame = 0; Frame < 64; ++Frame)
		{
			bStayed &= PoseStep(State, Row, 6.f, 0.f, true, false, false, Step16) == EFPArmsPose::ProneTuck; // at RodMoveSpeedOut: not slow enough
		}
		TestTrue(TEXT("between Out and In for 4 s: stays tucked"), bStayed);
	}
	{
		FLureRodPoseState State;
		PoseStep(State, Row, 100.f, 1.f);
		EFPArmsPose Pose = EFPArmsPose::ProneTuck;
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Pose = PoseStep(State, Row, 5.9f, 0.f, true, false, false, Step16);
		}
		TestEqual(TEXT("slow for 3/16 s (< RodStillDelay 0.25): still tucked"), PoseName(Pose), PoseName(EFPArmsPose::ProneTuck));
		Pose = PoseStep(State, Row, 5.9f, 0.f, true, false, false, Step16);
		TestEqual(TEXT("slow for 4/16 s (= RodStillDelay): the hold comes back"), PoseName(Pose), PoseName(EFPArmsPose::ProneHold));
	}
	{
		FLureRodPoseState State;
		PoseStep(State, Row, 100.f, 1.f);
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			PoseStep(State, Row, 0.f, 0.f, true, false, false, Step16);
		}
		PoseStep(State, Row, 0.f, 1.f, true, false, false, Step16); // a short push resets the pause
		EFPArmsPose Pose = EFPArmsPose::ProneTuck;
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Pose = PoseStep(State, Row, 0.f, 0.f, true, false, false, Step16);
		}
		TestEqual(TEXT("input during the pause restarts RodStillDelay"), PoseName(Pose), PoseName(EFPArmsPose::ProneTuck));
	}
	{
		FLureRodPoseState State;
			const EFPArmsPose Pose = PoseStep(State, Row, NaN, NaN, true, false, false, NaN);
		TestTrue(TEXT("NaN inputs give a valid pose"), Pose == EFPArmsPose::ProneHold || Pose == EFPArmsPose::ProneTuck);
		TestTrue(TEXT("... and finite state"), FMath::IsFinite(State.StillTimer) && FMath::IsFinite(State.PitchFollowUp));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseWallTuckRules, "Project.Fishing.QA.Pose.WallTuckRules", QAFishing::Flags)
bool FQAFishPoseWallTuckRules::RunTest(const FString& Parameters)
{
	// Spec: a still prone player facing a wall (RodHoldClearance) keeps the tuck unless a line is out.
	TArray<FLureMovementRow> Rows;
	if (!ShippedMovementRows(*this, Rows))
	{
		return false;
	}
	const FLureMovementRow& Prone = RowOf(Rows, ELureMovementState::Prone);
	const FLureMovementRow& Stand = RowOf(Rows, ELureMovementState::Stand);
	FLureRodPoseState State;
	TestEqual(TEXT("prone, still, wall ahead, no line: tucked"), PoseName(PoseStep(State, Prone, 0.f, 0.f, true, true, false)), PoseName(EFPArmsPose::ProneTuck));
	TestEqual(TEXT("prone, still, wall ahead, line out: the hold (you are fishing)"), PoseName(PoseStep(State, Prone, 0.f, 0.f, true, true, true)), PoseName(EFPArmsPose::ProneHold));
	TestEqual(TEXT("prone, still, no wall: the hold"), PoseName(PoseStep(State, Prone, 0.f, 0.f, true, false, false)), PoseName(EFPArmsPose::ProneHold));
	FLureMovementRow NoCheck = Prone;
	NoCheck.RodHoldClearance = 0.f;
	FLureRodPoseState Off;
	TestEqual(TEXT("RodHoldClearance 0 (check off): the hold even at a wall"), PoseName(PoseStep(Off, NoCheck, 0.f, 0.f, true, true, false)), PoseName(EFPArmsPose::ProneHold));
	FLureRodPoseState Standing;
	TestEqual(TEXT("standing at a wall: HoldRod (only the prone hold tucks)"), PoseName(PoseStep(Standing, Stand, 0.f, 0.f, true, true, false)), PoseName(EFPArmsPose::HoldRod));
	FLureRodPoseState Crawl;
	TestEqual(TEXT("crawling with a line out: still tucked (the line comes in; the rod never clips)"), PoseName(PoseStep(Crawl, Prone, 50.f, 1.f, true, false, true)), PoseName(EFPArmsPose::ProneTuck));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPosePitchFollowUpEases, "Project.Fishing.QA.Pose.PitchFollowUpEases", QAFishing::Flags)
bool FQAFishPosePitchFollowUpEases::RunTest(const FString& Parameters)
{
	// Contract: PitchFollowUp eases to the row's ArmsPitchFollowUp over its RodPoseBlendTime; the first step takes the row's value.
	TArray<FLureMovementRow> Rows;
	if (!ShippedMovementRows(*this, Rows))
	{
		return false;
	}
	FLureMovementRow Stand = RowOf(Rows, ELureMovementState::Stand);
	FLureMovementRow Prone = RowOf(Rows, ELureMovementState::Prone);
	Prone.RodPoseBlendTime = 0.5f;
	FLureRodPoseState State;
	PoseStep(State, Stand, 0.f, 0.f, true, false, false, 0.f);
	TestNearlyEqual(TEXT("first step: the row's follow-up at once"), State.PitchFollowUp, Stand.ArmsPitchFollowUp, 1.0e-6f);
	PoseStep(State, Prone, 0.f, 0.f, true, false, false, 0.125f);
	TestNearlyEqual(TEXT("a quarter of the blend time: a quarter of the way"), State.PitchFollowUp, 0.75f, 1.0e-4f);
	PoseStep(State, Prone, 0.f, 0.f, true, false, false, 0.125f);
	TestNearlyEqual(TEXT("half the blend time: half way"), State.PitchFollowUp, 0.5f, 1.0e-4f);
	PoseStep(State, Prone, 0.f, 0.f, true, false, false, 1.f);
	TestNearlyEqual(TEXT("after the blend time: the prone value"), State.PitchFollowUp, Prone.ArmsPitchFollowUp, 1.0e-4f);
	FLureMovementRow Snap = Stand;
	Snap.RodPoseBlendTime = 0.f;
	PoseStep(State, Snap, 0.f, 0.f, true, false, false, 1.f / 60.f);
	TestNearlyEqual(TEXT("blend time 0: snaps"), State.PitchFollowUp, Stand.ArmsPitchFollowUp, 1.0e-4f);
	FLureMovementRow Wild = Stand;
	Wild.ArmsPitchFollowUp = 3.f;
	FLureRodPoseState Fresh;
	PoseStep(Fresh, Wild, 0.f);
	TestTrue(TEXT("an out-of-range follow-up is kept within [0, 1]"), Fresh.PitchFollowUp >= 0.f && Fresh.PitchFollowUp <= 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseArmsCounterPitchFormula, "Project.Fishing.QA.Pose.ArmsCounterPitchFormula", QAFishing::Flags)
bool FQAFishPoseArmsCounterPitchFormula::RunTest(const FString& Parameters)
{
	// Contract: -(1 - FollowUp) * max(0, CameraPitch), pitch normalized to [-180, 180].
	for (const float FollowUp : { 0.f, 0.25f, 1.f })
	{
		for (const float Pitch : { -60.f, 0.f, 30.f, 89.f, 330.f, 390.f, -330.f })
		{
			const float Normalized = FRotator::NormalizeAxis(Pitch);
			const float Expected = -(1.f - FollowUp) * FMath::Max(0.f, Normalized);
			TestNearlyEqual(FString::Printf(TEXT("follow-up %.2f, pitch %.0f"), FollowUp, Pitch), FLureRodPose::ArmsCounterPitch(FollowUp, Pitch), Expected, 1.0e-3f);
		}
	}
	TestEqual(TEXT("NaN pitch: no counter-pitch"), FLureRodPose::ArmsCounterPitch(0.f, NaN), 0.f);
	TestEqual(TEXT("NaN follow-up: no counter-pitch"), FLureRodPose::ArmsCounterPitch(NaN, 30.f), 0.f);
	TestNearlyEqual(TEXT("follow-up above 1 counts as 1"), FLureRodPose::ArmsCounterPitch(2.f, 30.f), 0.f, 1.0e-4f);
	TestNearlyEqual(TEXT("follow-up below 0 counts as 0"), FLureRodPose::ArmsCounterPitch(-1.f, 30.f), -30.f, 1.0e-4f);
	return true;
}

// =====================================================================================================================
// In a world
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseClearanceTraceBoundaries, "Project.Fishing.QA.Pose.ClearanceTraceBoundaries", QAFishing::Flags)
bool FQAFishPoseClearanceTraceBoundaries::RunTest(const FString& Parameters)
{
	// Contract: a 5 cm sphere from the eye along the view's HORIZONTAL forward hits something within Distance cm.
	FScene Scene;
	if (!Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	const FVector Eye(0.f, 0.f, 40.f);
	AActor* Near = Scene.AddBox(FVector(120.f + 50.f, 0.f, 40.f), FVector(50.f, 200.f, 100.f)); // face at x = 120
	TestTrue(TEXT("a wall face 120 cm ahead, clearance 130: blocked"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator::ZeroRotator, 130.f, nullptr));
	TestTrue(TEXT("looking 60 deg down, same wall: still blocked (the probe stays horizontal)"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator(-60.f, 0.f, 0.f), 130.f, nullptr));
	TestFalse(TEXT("the same wall ignored (the player's own actor): clear"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator::ZeroRotator, 130.f, Near));
	TestFalse(TEXT("clearance 100 (< 120 - probe radius): clear"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator::ZeroRotator, 100.f, nullptr));
	TestFalse(TEXT("turned away: clear"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator(0.f, 180.f, 0.f), 130.f, nullptr));
	TestFalse(TEXT("clearance 0 (check off): never blocked"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator::ZeroRotator, 0.f, nullptr));
	TestFalse(TEXT("no world: never blocked"), FLureRodPose::TraceHoldClearance(nullptr, Eye, FRotator::ZeroRotator, 130.f, nullptr));
	Near->Destroy();
	Scene.AddBox(FVector(145.f + 50.f, 0.f, 40.f), FVector(50.f, 200.f, 100.f)); // face at x = 145
	TestFalse(TEXT("a wall face 145 cm ahead, clearance 130: clear"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator::ZeroRotator, 130.f, nullptr));
	Scene.AddBox(FVector(60.f, 0.f, -20.f), FVector(60.f, 60.f, 10.f)); // a floor under the probe, 50 cm below the eye
	TestFalse(TEXT("the ground below does not block"), FLureRodPose::TraceHoldClearance(Scene.World, Eye, FRotator::ZeroRotator, 130.f, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseCharacterPoseFollowsStanceAndMotion, "Project.Fishing.QA.Pose.CharacterPoseFollowsStanceAndMotion", QAFishing::Flags)
bool FQAFishPoseCharacterPoseFollowsStanceAndMotion::RunTest(const FString& Parameters)
{
	FPoseRig Rig;
	TArray<FLureMovementRow> Rows;
	if (!ShippedMovementRows(*this, Rows) || !Rig.Make(*this, nullptr, FVector(-300.f, 0.f, DockTop)))
	{
		return false;
	}
	const FLureMovementRow& Prone = RowOf(Rows, ELureMovementState::Prone);
	TestTrue(TEXT("the rod is in hand"), Rig.Character->IsHoldingRod());
	TestEqual(TEXT("standing still: HoldRod"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::HoldRod));
	Rig.Scene.TickMoving(Rig.Character, FVector::ForwardVector, 20);
	TestEqual(TEXT("walking: HoldRod"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::HoldRod));
	Rig.Character->SetSprintRequested(true);
	Rig.Scene.TickMoving(Rig.Character, -FVector::ForwardVector, 20);
	TestTrue(TEXT("QA precondition: sprinting"), Rig.Character->IsSprinting());
	TestEqual(TEXT("sprinting: HoldRod"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::HoldRod));
	Rig.Character->SetSprintRequested(false);
	Rig.Scene.Tick(40);
	Rig.Character->RequestStance(ELureStance::Crouch);
	Rig.Scene.Tick(20);
	TestEqual(TEXT("crouched: HoldRod"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::HoldRod));
	Rig.Character->RequestStance(ELureStance::Prone);
	Rig.Scene.Tick(40);
	TestEqual(TEXT("prone, still: ProneHold"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::ProneHold));
	TestNearlyEqual(TEXT("prone: the blend time is the Prone row's"), Rig.Character->GetArmsPoseBlendTime(), Prone.RodPoseBlendTime, 1.0e-4f);
	Rig.Scene.TickMoving(Rig.Character, FVector::ForwardVector, 15);
	TestEqual(TEXT("crawling: ProneTuck"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::ProneTuck));
	// Stop: the tuck stays while the body slows and for RodStillDelay, then the hold comes back.
	const bool bBack = Rig.Scene.TickUntil([&Rig]() { return Rig.Character->GetArmsPose() == EFPArmsPose::ProneHold; }, 120);
	TestTrue(TEXT("stopped: the hold comes back"), bBack);
	Rig.Scene.Tick(5);
	Rig.Fishing->bRodEquipped = false;
	Rig.Scene.Tick(2);
	TestEqual(TEXT("rod put away: Idle"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::Idle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseWallAheadTucksUnlessFishing, "Project.Fishing.QA.Pose.WallAheadTucksUnlessFishing", QAFishing::Flags)
bool FQAFishPoseWallAheadTucksUnlessFishing::RunTest(const FString& Parameters)
{
	// Prone and still facing a wall: the arms keep the tuck, so the owner can't start a cast; with a line already out the hold stays.
	FPoseRig Rig;
	if (!Rig.Make(*this))
	{
		return false;
	}
	Rig.Character->RequestStance(ELureStance::Prone);
	Rig.Scene.Tick(40);
	TestEqual(TEXT("prone facing the water: ProneHold"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::ProneHold));
	TestTrue(TEXT("cast from prone"), Rig.Fishing->AuthorityCast(0.5f, 0.f));
	Rig.Scene.Tick(10);
	const FVector Eye = Rig.Character->GetPawnViewLocation();
	Rig.Scene.AddBox(FVector(Eye.X + 90.f + 20.f, 0.f, Eye.Z), FVector(20.f, 300.f, 30.f)); // a low wall 90 cm ahead of the eye
	Rig.Scene.Tick(10);
	TestTrue(TEXT("QA precondition: the wall is within RodHoldClearance"), FLureRodPose::TraceHoldClearance(Rig.Scene.World, Eye, Rig.Character->GetControlRotation(), 130.f, Rig.Character));
	TestEqual(TEXT("wall ahead, line out: the hold stays"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::ProneHold));
	TestTrue(TEXT("... and the line stays out"), Rig.Fishing->IsLineOut());
	Rig.Fishing->AuthorityReelIn();
	Rig.Scene.Tick(5);
	TestEqual(TEXT("wall ahead, no line: tucked"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::ProneTuck));
	TestEqual(TEXT("the owner can't cast with a tucked rod"), BlockName(Rig.Fishing->GetCastBlock()), BlockName(ELureCastBlock::RodTucked));
	Rig.Fishing->PressCast();
	TestFalse(TEXT("... pressing the button does not start a charge"), Rig.Fishing->IsCharging());
	TestTrue(TEXT("... and the HUD says why"), Rig.Fishing->GetStatusText().Contains(TEXT("Can't cast")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishPoseColumnsDriveThePoseAndTheLineRules, "Project.Fishing.QA.Pose.ColumnsDriveThePoseAndTheLineRules", QAFishing::Flags)
bool FQAFishPoseColumnsDriveThePoseAndTheLineRules::RunTest(const FString& Parameters)
{
	// Data-driven: an edited DT_Movement where crouch-walking tucks the rod and standing cannot fish changes the pose AND the rules,
	// without code (new stances/gear later reuse the columns).
	FString Csv;
	if (!LoadMovementCsv(*this, Csv))
	{
		return false;
	}
	Csv = WithCell(*this, Csv, TEXT("Crouch"), TEXT("RodPoseMoving"), TEXT("ProneTuck"));
	Csv = WithCell(*this, Csv, TEXT("Crouch"), TEXT("RodMoveSpeedIn"), TEXT("40"));
	Csv = WithCell(*this, Csv, TEXT("Crouch"), TEXT("RodMoveSpeedOut"), TEXT("10"));
	Csv = WithCell(*this, Csv, TEXT("Stand"), TEXT("CanFish"), TEXT("False"));
	const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(*this, FLureMovementRow::StaticStruct(), Csv, TEXT("DT_Movement fixture")));
	TArray<FLureMovementRow> Rows;
	TArray<FString> Problems;
	TestEqual(TEXT("the fixture rows are valid (no fallback)"), static_cast<int32>(FLureMovementData::ResolveRows(Table.Get(), Rows, Problems)), 0);
	FLureRodPoseState Pure;
	TestEqual(TEXT("pure: crouch-walking tucks per the fixture"), PoseName(PoseStep(Pure, RowOf(Rows, ELureMovementState::Crouch), 60.f, 1.f)), PoseName(EFPArmsPose::ProneTuck));
	FLureCastConditions Walking;
	Walking.Speed2D = 60.f;
	TestEqual(TEXT("pure: crouch-walking at 60 cm/s can't cast (tucked)"), BlockName(FLureFishingRules::GetCastBlock(Walking, RowOf(Rows, ELureMovementState::Crouch))), BlockName(ELureCastBlock::RodTucked));
	Walking.Speed2D = 30.f;
	TestEqual(TEXT("pure: below the fixture's RodMoveSpeedIn 40 it can"), BlockName(FLureFishingRules::GetCastBlock(Walking, RowOf(Rows, ELureMovementState::Crouch))), BlockName(ELureCastBlock::None));
	TestEqual(TEXT("pure: standing with CanFish False can't cast (the CanFish reason)"), BlockName(FLureFishingRules::GetCastBlock(FLureCastConditions(), RowOf(Rows, ELureMovementState::Stand))), BlockName(ELureCastBlock::Sprinting));

	FPoseRig Rig;
	if (!Rig.Make(*this, &Csv))
	{
		return false;
	}
	TestFalse(TEXT("world: standing (CanFish False) the server refuses"), Rig.Fishing->AuthorityCast(0.5f, 0.f));
	Rig.Character->RequestStance(ELureStance::Crouch);
	Rig.Scene.Tick(20);
	TestTrue(TEXT("world: crouched (CanFish True) the cast works"), Rig.Fishing->AuthorityCast(0.5f, 0.f));
	Rig.Scene.Tick(5);
	for (int32 Frame = 0; Frame < 40 && Rig.Fishing->IsLineOut(); ++Frame)
	{
		Rig.Scene.TickMoving(Rig.Character, -FVector::ForwardVector, 1);
	}
	TestFalse(TEXT("world: crouch-walking brings the line in (the fixture tucks the rod)"), Rig.Fishing->IsLineOut());
	TestEqual(TEXT("world: ... reason RodTucked"), BlockName(Rig.Fishing->GetNetState().ResultReason), BlockName(ELureCastBlock::RodTucked));
	TestEqual(TEXT("world: the arms show the tuck"), PoseName(Rig.Pose()), PoseName(EFPArmsPose::ProneTuck));
	return true;
}

} // namespace QAFishing

#endif // WITH_DEV_AUTOMATION_TESTS

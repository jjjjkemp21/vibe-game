// QA-A3b (A3 gate, Full tier): independent QA tests for the fighting / dangling fish visual, derived from the acceptance
// criteria and docs/specs/fight-fish-visual.md (not from the implementation). They cover the boundaries the implementer
// tests leave open:
//  - T-048  the mouth on the line: swing speed edges (MinFacingSpeed, RunSwingFullSpeed), RunSwingDeg 0 / over 80, lag cap 0.
//  - T-048b the swing from the move's own side: side share clamped, unknown PatternId -> built-in pattern, the actor path.
//  - T-059a clip rate from stamina: stamina out of 0..1, a role not listed = 1, data sanity of RoleStaminaRates.
//  - T-061  (fish side) the wiggle is read from the adopted landed fish (a skinned mesh on an attached actor) and never
//           from a hidden mesh.
// Project.FishVisual.QA.S1.*  Data from data/tables/*.json (never the binary assets).

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Tests/AutomationCommon.h"

namespace LureFishVisualQAS1
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The fish at +X of the player: mouth -> player is yaw 180; +Y is the player's right. */
	const FVector Player(0.f, 0.f, 100.f);
	const FVector Mouth(1500.f, 0.f, -25.f);
	constexpr float BaseYaw = 180.f;

	bool LoadVisualRow(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Table, FFishVisualRow& OutRow)
	{
		FString Text;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_FishVisual.json"), Text)
			|| !LureFightQA::MakeTableChecked(Test, Table, FFishVisualRow::StaticStruct(), Text, true, TEXT("DT_FishVisual.json")))
		{
			return false;
		}
		const FFishVisualRow* Row = Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("QAS1"), false);
		if (!Row)
		{
			Test.AddError(TEXT("DT_FishVisual.json has no row named by ULureFishVisualSettings::VisualRow (Default)"));
			return false;
		}
		OutRow = *Row;
		return true;
	}

	bool LoadPatterns(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Table)
	{
		FString Text;
		return LureFightQA::ReadSource(Test, TEXT("DT_FightPattern.json"), Text)
			&& LureFightQA::MakeTableChecked(Test, Table, FLureFightPatternRow::StaticStruct(), Text, true, TEXT("DT_FightPattern.json"));
	}

	/** Signed swing of Rotation away from mouth -> player, degrees. */
	float Swing(const FRotator& Rotation, const FVector& From, const FVector& To)
	{
		const FVector ToPlayer = (To - From).GetSafeNormal2D();
		const float Base = FMath::RadiansToDegrees(FMath::Atan2(ToPlayer.Y, ToPlayer.X));
		return FMath::FindDeltaAngleDegrees(Base, static_cast<float>(Rotation.Yaw));
	}

	/** Where the head points across the line seen from the player: + = the player's right. */
	float HeadToPlayersRight(const FVector& Forward, const FVector& From, const FVector& To)
	{
		const FVector ToFish = (From - To).GetSafeNormal2D();
		const FVector Right(-ToFish.Y, ToFish.X, 0.f);
		return static_cast<float>(FVector::DotProduct(Forward.GetSafeNormal2D(), Right));
	}

	FFightFishAnimInput Fighting(FName Move, float Stamina)
	{
		FFightFishAnimInput In;
		In.Phase = EFightFishPhase::Fighting;
		In.MoveId = Move;
		In.SecondsSinceHook = 10.f;
		In.SpeedCmS = 120.f;
		In.BodyLengthCm = 55.f;
		In.WeightKg = 1.5f;
		In.ReferenceWeightKg = 1.5f;
		In.AnimRate = 1.2f;
		In.AnimAmplitude = 0.8f;
		In.Stamina01 = Stamina;
		return In;
	}

	FString RoleName(EFishAnimRole Role)
	{
		return StaticEnum<EFishAnimRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	// =================================================================================================================
	// T-048: the swing's speed edges
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1SwingSpeedEdges, "Project.FishVisual.QA.S1.Swing.SpeedEdges", Flags)
	bool FQAS1SwingSpeedEdges::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		if (!LoadVisualRow(*this, Table, Row))
		{
			return false;
		}
		const FRotator Current(0.f, BaseYaw, 0.f);
		auto SwingAt = [&Row, &Current](float Speed)
		{
			return Swing(FFightFishVisual::FightFacing(Row, FVector(0.f, Speed, 0.f), Mouth, Player, Current, false), Mouth, Player);
		};
		// Spec: "While the mouth swims faster than MinFacingSpeed the body swings sideways by RunSwingDeg x min(1, speed / RunSwingFullSpeed)".
		const float Under = Row.MinFacingSpeed - 0.5f;
		TestTrue(FString::Printf(TEXT("just under MinFacingSpeed (%.1f cm/s): no swing (%.3f deg)"), Under, SwingAt(Under)), FMath::Abs(SwingAt(Under)) <= 0.01f);
		const float Over = Row.MinFacingSpeed + 0.5f;
		const float Expected = Row.RunSwingDeg * FMath::Min(1.f, Over / Row.RunSwingFullSpeed);
		TestTrue(FString::Printf(TEXT("just over MinFacingSpeed (%.1f cm/s): swing %.3f = RunSwingDeg x speed / RunSwingFullSpeed = %.3f"), Over, FMath::Abs(SwingAt(Over)), Expected),
			FMath::IsNearlyEqual(FMath::Abs(SwingAt(Over)), Expected, 0.05f));
		TestTrue(TEXT("just over: the head goes to the swim's side (+Y)"),
			FFightFishVisual::FightFacing(Row, FVector(0.f, Over, 0.f), Mouth, Player, Current, false).Vector().Y > 0.0);
		TestTrue(FString::Printf(TEXT("at RunSwingFullSpeed: the full RunSwingDeg %.1f (%.3f)"), Row.RunSwingDeg, FMath::Abs(SwingAt(Row.RunSwingFullSpeed))),
			FMath::IsNearlyEqual(FMath::Abs(SwingAt(Row.RunSwingFullSpeed)), Row.RunSwingDeg, 0.05f));
		TestTrue(FString::Printf(TEXT("just under RunSwingFullSpeed: a little less than full (%.3f)"), FMath::Abs(SwingAt(Row.RunSwingFullSpeed - 10.f))),
			FMath::Abs(SwingAt(Row.RunSwingFullSpeed - 10.f)) < Row.RunSwingDeg - 0.5f);
		TestTrue(FString::Printf(TEXT("10x RunSwingFullSpeed: capped at RunSwingDeg (%.3f)"), FMath::Abs(SwingAt(10.f * Row.RunSwingFullSpeed))),
			FMath::IsNearlyEqual(FMath::Abs(SwingAt(10.f * Row.RunSwingFullSpeed)), Row.RunSwingDeg, 0.05f));
		return true;
	}

	// =================================================================================================================
	// T-048: RunSwingDeg 0 and over the 80 deg max; ClampToLine keeps pitch and roll
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1SwingLimitEdges, "Project.FishVisual.QA.S1.Swing.LimitEdges", Flags)
	bool FQAS1SwingLimitEdges::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		if (!LoadVisualRow(*this, Table, Row))
		{
			return false;
		}
		FString Problem;
		FFishVisualRow Edge = Row;
		Edge.RunSwingDeg = 80.f;
		TestTrue(TEXT("RunSwingDeg 80 (the max) is valid"), Edge.Validate(Problem));
		Edge.RunSwingDeg = 0.f;
		TestTrue(TEXT("RunSwingDeg 0 (no swing) is valid"), Edge.Validate(Problem));
		Edge.RunSwingDeg = 80.5f;
		TestFalse(TEXT("RunSwingDeg 80.5 is rejected (the body would cross the line)"), Edge.Validate(Problem));
		Edge.RunSwingDeg = -1.f;
		TestFalse(TEXT("RunSwingDeg -1 is rejected"), Edge.Validate(Problem));

		// 0: never swings, and the clamp pins the yaw on mouth -> player.
		FFishVisualRow None = Row;
		None.RunSwingDeg = 0.f;
		const FRotator Fast = FFightFishVisual::FightFacing(None, FVector(0.f, 500.f, 0.f), Mouth, Player, FRotator(0.f, BaseYaw, 0.f), false);
		TestTrue(FString::Printf(TEXT("RunSwingDeg 0, fast sideways swim: no swing (%.3f)"), Swing(Fast, Mouth, Player)), FMath::Abs(Swing(Fast, Mouth, Player)) <= 0.01f);
		const FRotator Pinned = FFightFishVisual::ClampToLine(None, FRotator(0.f, BaseYaw + 50.f, 0.f), Mouth, Player);
		TestTrue(FString::Printf(TEXT("RunSwingDeg 0: ClampToLine pins the yaw on mouth -> player (%.3f)"), Swing(Pinned, Mouth, Player)), FMath::Abs(Swing(Pinned, Mouth, Player)) <= 0.01f);

		// Over the max (a bad value that skipped Validate): the clamp still holds 80, pitch and roll untouched.
		FFishVisualRow Over = Row;
		Over.RunSwingDeg = 120.f;
		const FRotator Wide = FFightFishVisual::ClampToLine(Over, FRotator(10.f, BaseYaw + 150.f, 5.f), Mouth, Player);
		TestTrue(FString::Printf(TEXT("RunSwingDeg 120: ClampToLine keeps the yaw within 80 (%.3f)"), Swing(Wide, Mouth, Player)), FMath::Abs(Swing(Wide, Mouth, Player)) <= 80.01f);
		TestTrue(FString::Printf(TEXT("ClampToLine keeps pitch 10 (%.3f) and roll 5 (%.3f)"), Wide.Pitch, Wide.Roll),
			FMath::IsNearlyEqual(static_cast<float>(Wide.Pitch), 10.f, 0.01f) && FMath::IsNearlyEqual(static_cast<float>(Wide.Roll), 5.f, 0.01f));

		// Shipped: a fish facing straight away from the player is brought back to exactly the cone's edge.
		const FRotator Away = FFightFishVisual::ClampToLine(Row, FRotator(0.f, 0.f, 0.f), Mouth, Player);
		TestTrue(FString::Printf(TEXT("facing straight away -> at the RunSwingDeg %.1f edge (%.3f)"), Row.RunSwingDeg, FMath::Abs(Swing(Away, Mouth, Player))),
			FMath::IsNearlyEqual(FMath::Abs(Swing(Away, Mouth, Player)), Row.RunSwingDeg, 0.01f));
		const FRotator Inside = FFightFishVisual::ClampToLine(Row, FRotator(0.f, BaseYaw + 0.5f * Row.RunSwingDeg, 0.f), Mouth, Player);
		TestTrue(TEXT("inside the cone: unchanged"), FMath::IsNearlyEqual(Swing(Inside, Mouth, Player), 0.5f * Row.RunSwingDeg, 0.01f));
		return true;
	}

	// =================================================================================================================
	// T-048: the mouth lag cap at 0 and at its edge
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1MouthLagEdges, "Project.FishVisual.QA.S1.Mouth.LagCapEdges", Flags)
	bool FQAS1MouthLagEdges::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		if (!LoadVisualRow(*this, Table, Row))
		{
			return false;
		}
		FString Problem;
		FFishVisualRow Zero = Row;
		Zero.MouthMaxLagCm = 0.f;
		TestTrue(TEXT("MouthMaxLagCm 0 is valid"), Zero.Validate(Problem));
		const FVector Target(300.f, 40.f, -80.f);
		const FVector Pinned = FFightFishVisual::StepMouth(Zero, FVector(0.f, 0.f, -25.f), Target, 1.f / 60.f, 0.2f);
		TestTrue(FString::Printf(TEXT("MouthMaxLagCm 0: the mouth is on the line end horizontally (%.4f cm)"), FVector::Dist2D(Pinned, Target)), FVector::Dist2D(Pinned, Target) <= 0.001);

		const FVector From(0.f, 0.f, -25.f);
		const FVector Inside(Row.MouthMaxLagCm - 0.1f, 0.f, -25.f);
		const FVector InStep = FFightFishVisual::StepMouth(Row, From, Inside, 1.f / 60.f, 5.f);
		TestTrue(FString::Printf(TEXT("just inside the cap (%.1f cm), slow smoothing: only smoothed, not pulled in (%.3f cm left)"), Inside.X, FVector::Dist2D(InStep, Inside)),
			FVector::Dist2D(InStep, Inside) > 0.5 * Inside.X && FVector::Dist2D(InStep, Inside) <= Inside.X);
		const FVector Outside(Row.MouthMaxLagCm + 0.1f, 0.f, -25.f);
		const FVector OutStep = FFightFishVisual::StepMouth(Row, From, Outside, 1.f / 60.f, 5.f);
		TestTrue(FString::Printf(TEXT("just outside the cap: at most MouthMaxLagCm %.1f behind (%.4f)"), Row.MouthMaxLagCm, FVector::Dist2D(OutStep, Outside)),
			FVector::Dist2D(OutStep, Outside) <= Row.MouthMaxLagCm + 0.001);
		const FVector NoTime = FFightFishVisual::StepMouth(Row, From, FVector(900.f, -300.f, -25.f), 0.f, 0.2f);
		TestTrue(FString::Printf(TEXT("DeltaTime 0, far target: still at most MouthMaxLagCm behind (%.4f)"), FVector::Dist2D(NoTime, FVector(900.f, -300.f, 0.f))),
			FVector::Dist2D(NoTime, FVector(900.f, -300.f, 0.f)) <= Row.MouthMaxLagCm + 0.001);
		return true;
	}

	// =================================================================================================================
	// T-059a: stamina outside 0..1
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1StaminaOutOfRange, "Project.FishVisual.QA.S1.PlayRate.StaminaOutOfRange", Flags)
	bool FQAS1StaminaOutOfRange::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		if (!LoadVisualRow(*this, Table, Row))
		{
			return false;
		}
		for (const FFishMoveAnimRole& Move : Row.MoveRoles)
		{
			const FFishAnimState Empty = FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 0.f));
			const FFishAnimState Below = FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, -1.f));
			const FFishAnimState Full = FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 1.f));
			const FFishAnimState Above = FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 2.5f));
			const FString Name = Move.MoveId.ToString();
			TestTrue(FString::Printf(TEXT("%s: stamina -1 plays as 0 (rate %.4f vs %.4f, alpha %.4f vs %.4f)"), *Name, Below.PlayRate, Empty.PlayRate, Below.Amplitude, Empty.Amplitude),
				FMath::IsNearlyEqual(Below.PlayRate, Empty.PlayRate, 1.e-5f) && FMath::IsNearlyEqual(Below.Amplitude, Empty.Amplitude, 1.e-5f));
			TestTrue(FString::Printf(TEXT("%s: stamina 2.5 plays as 1 (rate %.4f vs %.4f, alpha %.4f vs %.4f)"), *Name, Above.PlayRate, Full.PlayRate, Above.Amplitude, Full.Amplitude),
				FMath::IsNearlyEqual(Above.PlayRate, Full.PlayRate, 1.e-5f) && FMath::IsNearlyEqual(Above.Amplitude, Full.Amplitude, 1.e-5f));
			TestTrue(FString::Printf(TEXT("%s: alpha never over AnimAmplitude x FreshAmplitudeScale (%.4f)"), *Name, Above.Amplitude), Above.Amplitude <= 0.8f * Row.FreshAmplitudeScale + 1.e-5f);
		}
		FLureFightNetState Fight;
		Fight.bActive = true;
		Fight.Stamina = -3.f;
		Fight.LineOut = 1000.f;
		FLureFishingNetState Line;
		Line.State = ELureFishingState::Hooked;
		Line.BobberRest = FVector(1000.f, 0.f, 0.f);
		const FFightFishView View = FFightFishViewAdapter::Make(Fight, Line, FFishInstance(), Player, FVector::ForwardVector, false);
		TestEqual(TEXT("adapter: a negative fight stamina reaches the view as 0"), View.Stamina01, 0.f, 1.e-6f);
		return true;
	}

	// =================================================================================================================
	// T-059a: a role not in RoleStaminaRates plays at factor 1; the alpha rule does not depend on that list
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1UnlistedRole, "Project.FishVisual.QA.S1.PlayRate.UnlistedRoleIsOne", Flags)
	bool FQAS1UnlistedRole::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		if (!LoadVisualRow(*this, Table, Row))
		{
			return false;
		}
		TestEqual(TEXT("Flop is not in the shipped RoleStaminaRates: factor 1 when tired"), FFightFishVisual::StaminaRate(Row, EFishAnimRole::Flop, 0.f), 1.f, 1.e-6f);

		FFishVisualRow Empty = Row;
		Empty.RoleStaminaRates.Reset();
		FString Problem;
		TestTrue(FString::Printf(TEXT("an empty RoleStaminaRates (optional column) is valid (%s)"), *Problem), Empty.Validate(Problem));
		const UEnum* Roles = StaticEnum<EFishAnimRole>();
		for (int32 Index = 0; Index < Roles->NumEnums() - 1; ++Index)
		{
			const EFishAnimRole Role = static_cast<EFishAnimRole>(Roles->GetValueByIndex(Index));
			for (const float Stamina : { 0.f, 0.5f, 1.f })
			{
				TestEqual(FString::Printf(TEXT("empty list: %s at stamina %.1f -> 1"), *RoleName(Role), Stamina), FFightFishVisual::StaminaRate(Empty, Role, Stamina), 1.f, 1.e-6f);
			}
		}
		const FFishAnimState Tired = FFightFishVisual::ComputeAnimState(Empty, Fighting(TEXT("Run"), 0.f));
		const FFishAnimState Fresh = FFightFishVisual::ComputeAnimState(Empty, Fighting(TEXT("Run"), 1.f));
		TestEqual(TEXT("empty list: Run at stamina 0 = AnimRate x weight factor (1.2)"), Tired.PlayRate, 1.2f, 1.e-5f);
		TestEqual(TEXT("empty list: same rate at stamina 1"), Fresh.PlayRate, Tired.PlayRate, 1.e-5f);
		TestEqual(TEXT("empty list: alpha still AnimAmplitude x TiredAmplitudeScale at stamina 0"), Tired.Amplitude, 0.8f * Empty.TiredAmplitudeScale, 1.e-5f);
		return true;
	}

	// =================================================================================================================
	// T-059a data: RoleStaminaRates in every DT_FishVisual row make sense (adding content never silently breaks it)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1StaminaData, "Project.FishVisual.QA.S1.Data.RoleStaminaRatesSane", Flags)
	bool FQAS1StaminaData::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Default;
		if (!LoadVisualRow(*this, Table, Default))
		{
			return false;
		}
		int32 Rows = 0;
		Table->ForeachRow<FFishVisualRow>(TEXT("QAS1"), [this, &Rows](const FName& Name, const FFishVisualRow& Row)
		{
			++Rows;
			const FString R = Name.ToString();
			TSet<EFishAnimRole> Listed;
			for (const FFishRoleStaminaRate& Rate : Row.RoleStaminaRates)
			{
				TestFalse(FString::Printf(TEXT("%s: %s listed once"), *R, *RoleName(Rate.Role)), Listed.Contains(Rate.Role));
				Listed.Add(Rate.Role);
				TestTrue(FString::Printf(TEXT("%s: %s FreshRate %.3f > 0"), *R, *RoleName(Rate.Role), Rate.FreshRate), Rate.FreshRate > 0.f);
				TestTrue(FString::Printf(TEXT("%s: %s 0 < TiredRate %.3f <= FreshRate %.3f (a spent fish never beats faster)"), *R, *RoleName(Rate.Role), Rate.TiredRate, Rate.FreshRate),
					Rate.TiredRate > 0.f && Rate.TiredRate <= Rate.FreshRate);
			}
			TestTrue(FString::Printf(TEXT("%s: 0 <= TiredAmplitudeScale %.3f <= FreshAmplitudeScale %.3f <= 1"), *R, Row.TiredAmplitudeScale, Row.FreshAmplitudeScale),
				Row.TiredAmplitudeScale >= 0.f && Row.TiredAmplitudeScale <= Row.FreshAmplitudeScale && Row.FreshAmplitudeScale <= 1.f);
			if (Row.RoleStaminaRates.Num() > 0)
			{
				// Every role a fight move can play (and the hook-set Thrash) has a stamina rate: a missing one would beat at full speed when spent.
				TSet<EFishAnimRole> Played = { Row.UnknownMoveRole, EFishAnimRole::Thrash };
				for (const FFishMoveAnimRole& Move : Row.MoveRoles)
				{
					Played.Add(Move.Role);
				}
				for (const EFishAnimRole Role : Played)
				{
					TestTrue(FString::Printf(TEXT("%s: fight role %s has a RoleStaminaRates entry"), *R, *RoleName(Role)), Listed.Contains(Role));
				}
			}
		});
		TestTrue(FString::Printf(TEXT("rows checked (%d)"), Rows), Rows > 0);
		return true;
	}

	// =================================================================================================================
	// T-048b: the move side's size is capped; an unknown PatternId falls back to the built-in pattern
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1MoveSideEdges, "Project.FishVisual.QA.S1.SwimFacing.MoveSideEdges", Flags)
	bool FQAS1MoveSideEdges::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Visual;
		TStrongObjectPtr<UDataTable> Patterns;
		FFishVisualRow Row;
		if (!LoadVisualRow(*this, Visual, Row) || !LoadPatterns(*this, Patterns))
		{
			return false;
		}
		const FRotator Current(0.f, BaseYaw, 0.f);
		const FVector DriftLeftFast(-60.f, -500.f, 0.f); // reeled in and dragged fast to the player's left
		for (const float Side : { 3.f, -3.f })
		{
			const FRotator Facing = FFightFishVisual::FightFacing(Row, DriftLeftFast, Mouth, Player, Current, false, true, Side);
			const float Size = FMath::Abs(Swing(Facing, Mouth, Player));
			TestTrue(FString::Printf(TEXT("MoveSwimSide %.0f: capped at RunSwingDeg %.1f (%.3f)"), Side, Row.RunSwingDeg, Size), Size <= Row.RunSwingDeg + 0.01f && Size >= Row.RunSwingDeg - 0.05f);
			TestTrue(FString::Printf(TEXT("MoveSwimSide %.0f: the head to the move's side"), Side), HeadToPlayersRight(Facing.Vector(), Mouth, Player) * Side > 0.f);
		}
		const FRotator Half = FFightFishVisual::FightFacing(Row, DriftLeftFast, Mouth, Player, Current, false, true, 0.5f);
		TestTrue(FString::Printf(TEXT("MoveSwimSide 0.5 against a fast drift left: right, half the swing (%.3f)"), Swing(Half, Mouth, Player)),
			HeadToPlayersRight(Half.Vector(), Mouth, Player) > 0.f && FMath::IsNearlyEqual(FMath::Abs(Swing(Half, Mouth, Player)), 0.5f * Row.RunSwingDeg, 0.05f));

		// Unknown PatternId (a renamed/removed row): the built-in pattern, like PatternId None, on every machine.
		const FLureFightPatternRow Unknown = FFightFishViewAdapter::FindPattern(Patterns.Get(), TEXT("QA_NoSuchPattern"));
		const FLureFightPatternRow& Fallback = FFightFishViewAdapter::FallbackPattern();
		TestEqual(TEXT("unknown PatternId -> the built-in pattern (move count)"), Unknown.Moves.Num(), Fallback.Moves.Num());
		bool bSame = Unknown.Moves.Num() == Fallback.Moves.Num();
		for (int32 Index = 0; bSame && Index < Unknown.Moves.Num(); ++Index)
		{
			bSame = Unknown.Moves[Index].Id == Fallback.Moves[Index].Id;
		}
		TestTrue(TEXT("unknown PatternId -> the built-in pattern (move ids)"), bSame);
		return true;
	}

	// =================================================================================================================
	// T-048b: the actor swings to the move's side while reeled in and dragged the other way; flips with the side
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1ActorMoveSide, "Project.FishVisual.QA.S1.SwimFacing.ActorSwingsToMoveSide", Flags)
	bool FQAS1ActorMoveSide::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		LureFightQA::FWorld World;
		if (!LoadVisualRow(*this, Table, Row) || !World.Create(*this))
		{
			return false;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureFightFish* Fish = World.World->SpawnActor<ALureFightFish>(ALureFightFish::StaticClass(), FTransform::Identity, Params);
		if (!TestNotNull(TEXT("fish actor"), Fish))
		{
			return false;
		}
		FLureFightFishSetup Setup;
		Setup.Row = Row;
		Setup.Fish.SpeciesId = TEXT("Bonefish");
		Setup.Fish.WeightKg = 1.5f;
		Setup.ReferenceWeightKg = 1.5f;
		Fish->Setup(Setup);

		FFightFishView View;
		View.bFighting = true;
		View.FightId = 1;
		View.MoveId = TEXT("Swim");
		View.bHasAuthority = false; // a client: the side comes from replicated fields only
		View.PlayerLocation = Player;
		View.WaterZ = 0.f;
		View.LineEnd = FVector(1500.f, 0.f, 0.f);
		View.bMoveSwims = true;
		constexpr float Share = 0.6f;
		const float Expected = Row.RunSwingDeg * Share;
		float WorstLag = 0.f;
		for (const float Side : { 1.f, -1.f })
		{
			View.MoveSwimSide = Side * Share;
			for (int32 Frame = 0; Frame < 150; ++Frame)
			{
				// Reeled in (30 cm/s) and dragged to the side opposite the move's (60 cm/s): the old ground-velocity rule would swing the other way.
				View.LineEnd += FVector(-0.5f, -Side * 1.f, 0.f);
				Fish->ApplyView(View, LureFightQA::WorldDt);
				WorstLag = FMath::Max(WorstLag, static_cast<float>(FVector::Dist2D(Fish->GetMouthLocation(), View.LineEnd)));
			}
			const FVector MouthNow = Fish->GetMouthLocation();
			const FRotator Rotation = Fish->GetActorRotation();
			TestTrue(FString::Printf(TEXT("side %+.0f: the head to the move's side, not the drag's (%.3f)"), Side, HeadToPlayersRight(Rotation.Vector(), MouthNow, Player)),
				HeadToPlayersRight(Rotation.Vector(), MouthNow, Player) * Side > 0.f);
			TestTrue(FString::Printf(TEXT("side %+.0f: swing %.2f = RunSwingDeg x share %.2f (+-1.5)"), Side, FMath::Abs(Swing(Rotation, MouthNow, Player)), Expected),
				FMath::IsNearlyEqual(FMath::Abs(Swing(Rotation, MouthNow, Player)), Expected, 1.5f));
		}
		TestTrue(FString::Printf(TEXT("the mouth stays on the line end (worst %.2f cm, max MouthMaxLagCm %.1f)"), WorstLag, Row.MouthMaxLagCm), WorstLag <= Row.MouthMaxLagCm + 0.01f);
		Fish->Destroy();
		return true;
	}

	// =================================================================================================================
	// T-061 (fish side): the wiggle is read from the adopted landed fish and never from a hidden mesh
	// =================================================================================================================

	constexpr float FrameDt = 1.f / 60.f;
	const FVector Tip(0.f, 0.f, 300.f);
	constexpr float HangLength = 100.f;
	const FVector HookOffset(20.f, 0.f, 0.f);
	const FVector BodyRest(-25.f, 0.f, 0.f);

	/** A rod line with a carrier (the fish item) hanging from it; the landed fish visual is a separate actor attached to it. */
	struct FHangRig
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ULureFishingLineComponent* Line = nullptr;
		AActor* Carrier = nullptr;
		UStaticMeshComponent* ItemLook = nullptr;
		AActor* Visual = nullptr;
		UPoseableMeshComponent* Skeleton = nullptr;
		FName Bone;

		AActor* Spawn(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Root->SetMobility(EComponentMobility::Movable);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocation(Location);
			return Actor;
		}

		bool Create(FAutomationTestBase& Test, bool bItemLook)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			AActor* Owner = Spawn(FVector::ZeroVector);
			Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("QAS1Line"), RF_Transient);
			Line->SetupAttachment(Owner->GetRootComponent());
			Line->RegisterComponent();
			Line->Setup(nullptr, nullptr, FLinearColor::White, 12);
			FLureFishingLineRow Tuning = FLureFishingLineRules::GetFallbackRow();
			Tuning.HangWiggleCoupling = 1.f;
			if (!Test.TestTrue(TEXT("line tuning valid"), Line->SetTuning(Tuning)))
			{
				return false;
			}
			Carrier = Spawn(Tip - FVector(0.f, 0.f, HangLength) - HookOffset);
			if (bItemLook)
			{
				ItemLook = NewObject<UStaticMeshComponent>(Carrier, TEXT("ItemLook"));
				ItemLook->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				ItemLook->SetMobility(EComponentMobility::Movable);
				ItemLook->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				ItemLook->SetupAttachment(Carrier->GetRootComponent());
				ItemLook->SetRelativeScale3D(FVector(0.3f, 0.05f, 0.1f));
				ItemLook->RegisterComponent();
				ItemLook->SetRelativeLocation(BodyRest);
			}
			Line->SetEndpoints(Tip, Tip - FVector(0.f, 0.f, HangLength));
			Line->AttachEndActor(Carrier, HangLength, HookOffset, /*bOrientAlongLine*/ true);
			return Test.TestTrue(TEXT("the carrier hangs"), Line->GetEndActor() == Carrier);
		}

		/** The landed fish visual: a skinned mesh on its own actor, attached to the carrier at identity (as AdoptVisual does). */
		bool AddVisual(FAutomationTestBase& Test, bool bVisible)
		{
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
			if (!Test.TestNotNull(TEXT("the engine's SkeletalCube loads"), Mesh))
			{
				return false;
			}
			Visual = Spawn(Carrier->GetActorLocation());
			Skeleton = NewObject<UPoseableMeshComponent>(Visual, TEXT("VisualSkeleton"));
			Skeleton->SetSkinnedAssetAndUpdate(Mesh);
			Skeleton->SetupAttachment(Visual->GetRootComponent());
			Skeleton->RegisterComponent();
			Skeleton->SetVisibility(bVisible);
			Bone = Skeleton->GetBoneName(0);
			Visual->AttachToActor(Carrier, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			Visual->SetActorRelativeLocation(FVector::ZeroVector);
			Visual->SetActorRelativeRotation(FRotator::ZeroRotator);
			Pose(BodyRest);
			return Test.TestTrue(TEXT("the visual's skeleton has a posed bone"), !Bone.IsNone() && Skeleton->GetNumComponentSpaceTransforms() > 0);
		}

		void Pose(const FVector& Offset)
		{
			if (Skeleton)
			{
				Skeleton->SetBoneLocationByName(Bone, Offset, EBoneSpaces::ComponentSpace);
				Skeleton->RefreshBoneTransforms();
			}
		}

		FVector MidPoint() const
		{
			const TArray<FVector>& Points = Line->GetPoints();
			return Points[Points.Num() / 2];
		}

		void Rest(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Pose(BodyRest);
				Line->UpdateLine(FrameDt);
			}
		}

		/** Wiggles the visual's body sideways (Amplitude cm at Hz) for Lead + Measured frames; returns the middle's sideways samples. */
		TArray<double> Wiggle(double Amplitude, double Hz, int32 Lead, int32 Measured, double& OutMaxFromRest)
		{
			TArray<double> Samples;
			const FVector Side = Carrier->GetActorQuat().GetAxisY();
			const FVector MidRest = MidPoint();
			OutMaxFromRest = 0.0;
			for (int32 Frame = 1; Frame <= Lead + Measured; ++Frame)
			{
				Pose(BodyRest + FVector(0.0, Amplitude * FMath::Sin(2.0 * UE_DOUBLE_PI * Hz * Frame * FrameDt), 0.0));
				Line->UpdateLine(FrameDt);
				OutMaxFromRest = FMath::Max(OutMaxFromRest, FVector::Dist(MidPoint(), MidRest));
				if (Frame > Lead)
				{
					Samples.Add(FVector::DotProduct(MidPoint() - MidRest, Side));
				}
			}
			return Samples;
		}
	};

	/** Amplitude of the Hz component of Samples taken every FrameDt (one DFT bin). */
	double AmplitudeAt(const TArray<double>& Samples, double Hz)
	{
		double Re = 0.0;
		double Im = 0.0;
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			const double Phase = 2.0 * UE_DOUBLE_PI * Hz * Index * FrameDt;
			Re += Samples[Index] * FMath::Cos(Phase);
			Im -= Samples[Index] * FMath::Sin(Phase);
		}
		return 2.0 * FMath::Sqrt(Re * Re + Im * Im) / FMath::Max(1, Samples.Num());
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1HangAdopted, "Project.FishVisual.QA.S1.Hang.AdoptedFishWiggleMovesTheLine", Flags)
	bool FQAS1HangAdopted::RunTest(const FString& Parameters)
	{
		// The item hangs showing its own (still) mesh; then it adopts the landed fish visual: its own mesh is hidden and the
		// visual (a skinned mesh on an attached actor) wiggles. The line must follow the visual's wiggle.
		FHangRig Rig;
		if (!Rig.Create(*this, /*bItemLook*/ true))
		{
			return false;
		}
		Rig.Rest(60);
		double Before = 0.0;
		Rig.Wiggle(0.0, 3.0, 0, 60, Before);
		TestTrue(FString::Printf(TEXT("before adoption, a still item: the line rests (%.4f cm)"), Before), Before <= 0.01);
		if (!Rig.AddVisual(*this, /*bVisible*/ true))
		{
			return false;
		}
		Rig.ItemLook->SetVisibility(false); // AdoptVisual hides the item's own meshes
		Rig.Rest(60);
		double MaxFromRest = 0.0;
		const TArray<double> Mid = Rig.Wiggle(3.0, 3.0, 60, 240, MaxFromRest);
		const double AtHz = AmplitudeAt(Mid, 3.0);
		const double Off = FMath::Max(AmplitudeAt(Mid, 1.0), AmplitudeAt(Mid, 6.0));
		TestTrue(FString::Printf(TEXT("the adopted fish's 3 cm, 3 Hz wiggle moves the line's middle >= 0.5 cm at 3 Hz (%.3f cm)"), AtHz), AtHz >= 0.5);
		TestTrue(FString::Printf(TEXT("... at the wiggle's frequency (3 Hz %.3f vs 1/6 Hz %.3f)"), AtHz, Off), AtHz >= 3.0 * Off);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAS1HangHidden, "Project.FishVisual.QA.S1.Hang.HiddenFishIsNotAWiggle", Flags)
	bool FQAS1HangHidden::RunTest(const FString& Parameters)
	{
		// A hidden fish mesh (not drawn: e.g. a hidden look) wiggling must not move the line; once it is shown, it does.
		FHangRig Rig;
		if (!Rig.Create(*this, /*bItemLook*/ false) || !Rig.AddVisual(*this, /*bVisible*/ false))
		{
			return false;
		}
		Rig.Rest(60);
		double Hidden = 0.0;
		Rig.Wiggle(3.0, 3.0, 0, 180, Hidden);
		TestTrue(FString::Printf(TEXT("hidden mesh wiggling: the line's middle stays at rest (<= 0.01 cm; %.4f)"), Hidden), Hidden <= 0.01);
		Rig.Skeleton->SetVisibility(true);
		Rig.Rest(60); // the search runs again within WiggleSearchInterval
		double Shown = 0.0;
		const TArray<double> Mid = Rig.Wiggle(3.0, 3.0, 60, 240, Shown);
		TestTrue(FString::Printf(TEXT("shown: the wiggle moves the line's middle >= 0.5 cm at 3 Hz (%.3f cm)"), AmplitudeAt(Mid, 3.0)), AmplitudeAt(Mid, 3.0) >= 0.5);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

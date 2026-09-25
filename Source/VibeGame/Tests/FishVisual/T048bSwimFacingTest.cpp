// T-048b: a fighting fish swings toward its fight move's own swim direction, not toward its ground velocity
// (art, SK_Fish.anim.md "Eng follow-ups (S3)" item 5: a Run while reeling read as "swimming at you").
// Project.FishVisual.SwimFacing.*: the swing's side and size come from the DT_FightPattern move (Away/Side, the replicated
// RunSide); Rest, Sulk, a tired fish and an unknown move keep the T-048 ground-velocity rule; every case keeps the
// MouthOnLine rule (yaw within RunSwingDeg of mouth -> player, no body point nearer the player than the mouth); host and
// client get the same swing from the same replicated state. The side comes only from replicated fields (FLureFightNetState
// PatternId, MoveId, RunSide, bExhausted) and the shipped table, so the pure tests on the view inputs are the network test.
// Data from data/tables/DT_FishVisual.json and DT_FightPattern.json (never the binary assets).

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fish/FightFishVisual.h"
#include "Fish/FishVisualSettings.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"

namespace LureT048bSwimFacingTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FData
	{
		TStrongObjectPtr<UDataTable> VisualTable;
		TStrongObjectPtr<UDataTable> PatternTable;
		FFishVisualRow Row;
	};

	bool Load(FAutomationTestBase& Test, FData& Out)
	{
		FString Visual;
		FString Pattern;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_FishVisual.json"), Visual)
			|| !LureFightQA::MakeTableChecked(Test, Out.VisualTable, FFishVisualRow::StaticStruct(), Visual, true, TEXT("DT_FishVisual.json"))
			|| !LureFightQA::ReadSource(Test, TEXT("DT_FightPattern.json"), Pattern)
			|| !LureFightQA::MakeTableChecked(Test, Out.PatternTable, FLureFightPatternRow::StaticStruct(), Pattern, true, TEXT("DT_FightPattern.json")))
		{
			return false;
		}
		const FFishVisualRow* Row = Out.VisualTable->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("SwimFacing"), false);
		if (!Row)
		{
			Test.AddError(TEXT("DT_FishVisual.json has no row named by ULureFishVisualSettings::VisualRow (Default)"));
			return false;
		}
		Out.Row = *Row;
		return true;
	}

	/** The replicated fight state of a fight in PatternId doing MoveId, running to Side. */
	FLureFightNetState MakeFight(FName PatternId, FName MoveId, ELureFightRunSide Side, bool bExhausted)
	{
		FLureFightNetState Fight;
		Fight.bActive = true;
		Fight.FightId = 7;
		Fight.PatternId = PatternId;
		Fight.MoveId = bExhausted ? NAME_None : MoveId;
		Fight.bExhausted = bExhausted;
		Fight.RunSide = bExhausted ? ELureFightRunSide::None : Side;
		Fight.Stamina = bExhausted ? 0.f : 0.6f;
		Fight.FishLocation = FVector(1200.f, 0.f, 0.f);
		return Fight;
	}

	/** The view a machine builds from that state (the adapter, as the fight-fish subsystem does). */
	FFightFishView MakeView(const FData& Data, const FLureFightNetState& Fight, bool bHasAuthority)
	{
		FLureFishingNetState Line;
		Line.State = ELureFishingState::Hooked;
		FFightFishView View = FFightFishViewAdapter::Make(Fight, Line, FFishInstance(), FVector::ZeroVector, FVector::ForwardVector, bHasAuthority);
		FFightFishViewAdapter::ApplyMove(View, Fight, FFightFishViewAdapter::FindPattern(Data.PatternTable.Get(), Fight.PatternId));
		return View;
	}

	/** Signed swing of Rotation from mouth -> player, degrees (+ = larger yaw). */
	float Swing(const FRotator& Rotation, const FVector& Mouth, const FVector& Player)
	{
		const FVector ToPlayer = (Player - Mouth).GetSafeNormal2D();
		const float BaseYaw = FMath::RadiansToDegrees(FMath::Atan2(ToPlayer.Y, ToPlayer.X));
		return FMath::FindDeltaAngleDegrees(BaseYaw, static_cast<float>(Rotation.Yaw));
	}

	/** Where the head points across the line, seen from the player: + = the player's right, - = the left. */
	float HeadToPlayersRight(const FRotator& Rotation, const FVector& Mouth, const FVector& Player)
	{
		const FVector ToFish = (Mouth - Player).GetSafeNormal2D();
		const FVector Right(-ToFish.Y, ToFish.X, 0.f);
		return static_cast<float>(FVector::DotProduct(Rotation.Vector().GetSafeNormal2D(), Right));
	}

	float AngleToPlayer(const FVector& Forward, const FVector& Mouth, const FVector& Player)
	{
		const FVector A = Forward.GetSafeNormal2D();
		const FVector B = (Player - Mouth).GetSafeNormal2D();
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(A, B)), -1.f, 1.f)));
	}

	/** Seen from above, how much closer to the player than the mouth the nearest body point is (<= 0 = the mouth is nearest), cm. */
	float BodyNearerThanMouth(const FVector& Forward, const FVector& Mouth, const FVector& Player, float BodyLengthCm)
	{
		const double MouthDistance = FVector::Dist2D(Mouth, Player);
		double Worst = -UE_BIG_NUMBER;
		constexpr int32 Samples = 20;
		for (int32 Index = 1; Index <= Samples; ++Index)
		{
			const FVector Point = Mouth - Forward.GetSafeNormal() * (BodyLengthCm * Index / Samples);
			Worst = FMath::Max(Worst, MouthDistance - FVector::Dist2D(Point, Player));
		}
		return static_cast<float>(Worst);
	}

	/** The move's sideways share |Side| / length(Away, Side) from the pattern source. */
	float SideShare(const FData& Data, FName PatternId, FName MoveId)
	{
		const FLureFightMove* Move = FFightFishViewAdapter::FindMove(FFightFishViewAdapter::FindPattern(Data.PatternTable.Get(), PatternId), MoveId);
		if (!Move)
		{
			return -1.f;
		}
		const float Length = FMath::Sqrt(Move->Away * Move->Away + Move->Side * Move->Side);
		return Length > 0.f ? FMath::Abs(Move->Side) / Length : 0.f;
	}

	// =================================================================================================================
	// A swimming move reeled straight in: the head goes to the move's side, not to the side the ground velocity drifts
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT048bMoveSideWhileReeled, "Project.FishVisual.SwimFacing.MoveSideWhileReeled", Flags)
	bool FT048bMoveSideWhileReeled::RunTest(const FString& Parameters)
	{
		FData Data;
		if (!Load(*this, Data))
		{
			return false;
		}
		const float MaxSwing = FMath::Clamp(Data.Row.RunSwingDeg, 0.f, 80.f);
		TestTrue(TEXT("RunSwingDeg > 0 (there is a swing to check)"), MaxSwing > 0.f);

		struct FCase { const TCHAR* Pattern; const TCHAR* Move; };
		const FCase Cases[] = { { TEXT("Run"), TEXT("Run") }, { TEXT("Run"), TEXT("Swim") }, { TEXT("Dart"), TEXT("Dart") }, { TEXT("Dive"), TEXT("Swim") } };
		const ELureFightRunSide Sides[] = { ELureFightRunSide::Left, ELureFightRunSide::Right };
		const float Bearings[] = { 0.f, 40.f, -75.f, 180.f };
		const FVector Player(0.f, 0.f, 100.f);
		int32 Checked = 0;
		for (const FCase& Case : Cases)
		{
			const float Share = SideShare(Data, Case.Pattern, Case.Move);
			if (!TestTrue(FString::Printf(TEXT("%s.%s is in DT_FightPattern.json with a sideways share"), Case.Pattern, Case.Move), Share > 0.f))
			{
				continue;
			}
			for (const ELureFightRunSide Side : Sides)
			{
				const float SideSign = Side == ELureFightRunSide::Right ? 1.f : -1.f;
				const FFightFishView View = MakeView(Data, MakeFight(Case.Pattern, Case.Move, Side, false), true);
				const FString What = FString::Printf(TEXT("%s.%s running %s"), Case.Pattern, Case.Move, SideSign > 0.f ? TEXT("right") : TEXT("left"));
				TestTrue(What + TEXT(": the move swims"), View.bMoveSwims);
				TestTrue(FString::Printf(TEXT("%s: MoveSwimSide %.3f = side x share %.3f"), *What, View.MoveSwimSide, SideSign * Share),
					FMath::IsNearlyEqual(View.MoveSwimSide, SideSign * Share, 1e-4f));

				for (const float Bearing : Bearings)
				{
					const FVector ToFish = FRotator(0.f, Bearing, 0.f).Vector();
					const FVector Mouth = Player + ToFish * 1500.0 + FVector(0.f, 0.f, -125.f);
					const FVector Right(-ToFish.Y, ToFish.X, 0.f);
					// Reeled straight in (the Bonefish case, -30 cm/s toward the player) while the line drifts 29 cm/s to the OTHER
					// side, and a still mouth (the rod holds it): the head still goes to the move's side.
					const FVector Velocities[] = { -ToFish * 30.0 - Right * (29.0 * SideSign), FVector::ZeroVector, -ToFish * 120.0 };
					for (const FVector& Velocity : Velocities)
					{
						const FRotator Current(0.f, Bearing + 180.f - 50.f * SideSign, 0.f); // leaning to the other side before
						const FRotator Facing = FFightFishVisual::FightFacing(Data.Row, Velocity, Mouth, Player, Current, View.bExhausted, View.bMoveSwims, View.MoveSwimSide);
						const FString Where = FString::Printf(TEXT("%s, bearing %.0f, velocity (%.0f, %.0f)"), *What, Bearing, Velocity.X, Velocity.Y);
						TestTrue(FString::Printf(TEXT("%s: the head points to the move's side (%.3f)"), *Where, HeadToPlayersRight(Facing, Mouth, Player)),
							HeadToPlayersRight(Facing, Mouth, Player) * SideSign > 0.f);
						TestTrue(FString::Printf(TEXT("%s: swing %.2f = RunSwingDeg x share %.2f"), *Where, FMath::Abs(Swing(Facing, Mouth, Player)), MaxSwing * Share),
							FMath::IsNearlyEqual(FMath::Abs(Swing(Facing, Mouth, Player)), MaxSwing * Share, 0.01f));
						TestTrue(FString::Printf(TEXT("%s: level roll"), *Where), FMath::IsNearlyZero(Facing.Roll));
						++Checked;
					}
					// Before T-048b (the ground-velocity rule): the Bonefish case turned the head the other way.
					const FVector Bonefish = Velocities[0];
					const FRotator Old = FFightFishVisual::FightFacing(Data.Row, Bonefish, Mouth, Player, FRotator(0.f, Bearing + 180.f, 0.f), false);
					TestTrue(FString::Printf(TEXT("%s, bearing %.0f: the old rule turned the head to the drift's side (why this task exists)"), *What, Bearing),
						HeadToPlayersRight(Old, Mouth, Player) * SideSign < 0.f);
				}
			}
		}
		TestTrue(FString::Printf(TEXT("cases checked: %d"), Checked), Checked == 4 * 2 * 4 * 3);
		return true;
	}

	// =================================================================================================================
	// Rest, Sulk, tired and unknown moves keep the ground-velocity rule; a move with no side (Charge, Dive) swims straight
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT048bOldRuleKept, "Project.FishVisual.SwimFacing.RestSulkTiredKeepOldRule", Flags)
	bool FT048bOldRuleKept::RunTest(const FString& Parameters)
	{
		FData Data;
		if (!Load(*this, Data))
		{
			return false;
		}
		const FFishVisualRow& Row = Data.Row;
		const float MaxSwing = FMath::Clamp(Row.RunSwingDeg, 0.f, 80.f);
		const FVector Player(0.f, 0.f, 100.f);
		const FVector Mouth(1500.f, 0.f, -25.f); // fish at +X: faces yaw 180; +Y is the player's right
		// Drifting to the player's right at 150 cm/s (and 50 in): the old rule swings the head right, by speed / RunSwingFullSpeed.
		const FVector Velocity(-50.f, 150.f, 0.f);
		const float Speed = static_cast<float>(Velocity.Size2D());
		const float OldSwing = MaxSwing * FMath::Clamp(Speed / FMath::Max(1.f, Row.RunSwingFullSpeed), 0.f, 1.f);

		struct FCase { const TCHAR* Pattern; const TCHAR* Move; bool bExhausted; };
		const FCase Cases[] = {
			{ TEXT("Run"), TEXT("Rest"), false }, { TEXT("Dart"), TEXT("Rest"), false }, { TEXT("Dive"), TEXT("Sulk"), false },
			{ TEXT("Run"), TEXT("Run"), true }, { TEXT("Run"), TEXT("NoSuchMove"), false } };
		for (const FCase& Case : Cases)
		{
			const FFightFishView View = MakeView(Data, MakeFight(Case.Pattern, Case.Move, ELureFightRunSide::Left, Case.bExhausted), true);
			const FString What = FString::Printf(TEXT("%s.%s%s"), Case.Pattern, Case.Move, Case.bExhausted ? TEXT(" (tired)") : TEXT(""));
			TestFalse(What + TEXT(": not a swimming move (the ground-velocity rule)"), View.bMoveSwims);
			TestTrue(What + TEXT(": no move side"), View.MoveSwimSide == 0.f);
			const FRotator Facing = FFightFishVisual::FightFacing(Row, Velocity, Mouth, Player, FRotator(0.f, 180.f, 0.f), View.bExhausted, View.bMoveSwims, View.MoveSwimSide);
			const FRotator Unchanged = FFightFishVisual::FightFacing(Row, Velocity, Mouth, Player, FRotator(0.f, 180.f, 0.f), View.bExhausted);
			TestTrue(What + TEXT(": the same rotation as the T-048 call"), Facing.Equals(Unchanged, 1e-3f));
			if (Case.bExhausted)
			{
				TestTrue(FString::Printf(TEXT("%s: no swing (%.2f)"), *What, Swing(Facing, Mouth, Player)), FMath::IsNearlyZero(Swing(Facing, Mouth, Player), 0.01f));
				TestTrue(What + TEXT(": roll ExhaustedRollDeg"), FMath::IsNearlyEqual(static_cast<float>(Facing.Roll), Row.ExhaustedRollDeg, 0.01f));
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: the head swings to the drift's side (right) by %.2f (got %.2f)"), *What, OldSwing, -Swing(Facing, Mouth, Player)),
					HeadToPlayersRight(Facing, Mouth, Player) > 0.f && FMath::IsNearlyEqual(-Swing(Facing, Mouth, Player), OldSwing, 0.01f));
			}
		}

		// A tired fish never swings, even if a caller says the move swims.
		const FRotator Tired = FFightFishVisual::FightFacing(Row, Velocity, Mouth, Player, FRotator(0.f, 180.f, 0.f), true, true, 1.f);
		TestTrue(TEXT("tired + bMoveSwims: no swing"), FMath::IsNearlyZero(Swing(Tired, Mouth, Player), 0.01f));

		// Moves with no side: straight at the player (Charge) or straight away and down (Dive): no swing, whatever the drift.
		const TCHAR* Straight[][2] = { { TEXT("Dart"), TEXT("Charge") }, { TEXT("Dive"), TEXT("Dive") } };
		for (const auto& Pair : Straight)
		{
			const FFightFishView View = MakeView(Data, MakeFight(Pair[0], Pair[1], ELureFightRunSide::None, false), true);
			TestTrue(FString::Printf(TEXT("%s.%s swims"), Pair[0], Pair[1]), View.bMoveSwims);
			const FRotator Facing = FFightFishVisual::FightFacing(Row, Velocity, Mouth, Player, FRotator(0.f, 130.f, 0.f), false, View.bMoveSwims, View.MoveSwimSide);
			TestTrue(FString::Printf(TEXT("%s.%s: no swing (%.2f)"), Pair[0], Pair[1], Swing(Facing, Mouth, Player)), FMath::IsNearlyZero(Swing(Facing, Mouth, Player), 0.01f));
		}

		// Not fighting: nothing from the move.
		FLureFightNetState Over = MakeFight(TEXT("Run"), TEXT("Run"), ELureFightRunSide::Right, false);
		Over.bActive = false;
		TestFalse(TEXT("fight over: not a swimming move"), MakeView(Data, Over, true).bMoveSwims);
		return true;
	}

	// =================================================================================================================
	// MouthOnLine (a)-(c) in every case: yaw within RunSwingDeg of mouth -> player, the mouth is the body point nearest the player
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT048bMouthOnLine, "Project.FishVisual.SwimFacing.KeepsMouthOnLine", Flags)
	bool FT048bMouthOnLine::RunTest(const FString& Parameters)
	{
		FData Data;
		if (!Load(*this, Data))
		{
			return false;
		}
		const FVector Player(0.f, 0.f, 100.f);
		const float SwingLimits[] = { Data.Row.RunSwingDeg, 80.f }; // shipped, and the most the row allows
		const ELureFightRunSide Sides[] = { ELureFightRunSide::Left, ELureFightRunSide::None, ELureFightRunSide::Right };
		const FVector Velocities[] = { FVector::ZeroVector, FVector(-30.f, 29.f, 0.f), FVector(200.f, -300.f, -80.f), FVector(-400.f, 0.f, 150.f) };
		const float Bearings[] = { 0.f, 70.f, -120.f };
		const float CurrentYaws[] = { 0.f, 90.f, -90.f, 180.f };
		int32 Cases = 0;
		float WorstAngle = 0.f;
		float WorstBody = -UE_BIG_NUMBER;
		for (const float Limit : SwingLimits)
		{
			FFishVisualRow Row = Data.Row;
			Row.RunSwingDeg = Limit;
			for (const FName PatternId : Data.PatternTable->GetRowNames())
			{
				const FLureFightPatternRow Pattern = FFightFishViewAdapter::FindPattern(Data.PatternTable.Get(), PatternId);
				for (const FLureFightMove& Move : Pattern.Moves)
				{
					for (const ELureFightRunSide Side : Sides)
					{
						for (const bool bExhausted : { false, true })
						{
							const FFightFishView View = MakeView(Data, MakeFight(PatternId, Move.Id, Side, bExhausted), false);
							for (const float Bearing : Bearings)
							{
								const FVector Mouth = Player + FRotator(0.f, Bearing, 0.f).Vector() * 900.0 + FVector(0.f, 0.f, -140.f);
								for (const FVector& Velocity : Velocities)
								{
									for (const float CurrentYaw : CurrentYaws)
									{
										const FRotator Facing = FFightFishVisual::FightFacing(Row, Velocity, Mouth, Player, FRotator(0.f, Bearing + 180.f + CurrentYaw, 0.f),
											View.bExhausted, View.bMoveSwims, View.MoveSwimSide);
										const FRotator Final = FFightFishVisual::ClampToLine(Row, Facing, Mouth, Player);
										const FVector Forward = Final.Vector();
										WorstAngle = FMath::Max(WorstAngle, AngleToPlayer(Facing.Vector(), Mouth, Player) - Limit);
										WorstAngle = FMath::Max(WorstAngle, AngleToPlayer(Forward, Mouth, Player) - Limit);
										WorstBody = FMath::Max(WorstBody, BodyNearerThanMouth(Forward, Mouth, Player, 110.f));
										++Cases;
									}
								}
							}
						}
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("cases: %d (every pattern move x side x tired x bearing x velocity x facing before)"), Cases), Cases > 500);
		TestTrue(FString::Printf(TEXT("(b) yaw never more than RunSwingDeg from mouth -> player (worst excess %.3f deg)"), WorstAngle), WorstAngle <= 0.01f);
		TestTrue(FString::Printf(TEXT("(c) no body point nearer the player than the mouth (worst %.3f cm)"), WorstBody), WorstBody <= 0.01f);
		return true;
	}

	// =================================================================================================================
	// Host and remote client: the same replicated state gives the same side and swing (RandomSide: the server's pick)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT048bHostClient, "Project.FishVisual.SwimFacing.SameOnHostAndClient", Flags)
	bool FT048bHostClient::RunTest(const FString& Parameters)
	{
		FData Data;
		if (!Load(*this, Data))
		{
			return false;
		}
		const FVector Player(0.f, 0.f, 100.f);
		const FVector Mouth(-300.f, 1100.f, -40.f);
		const FVector Velocity(20.f, -35.f, 0.f);
		for (const ELureFightRunSide Side : { ELureFightRunSide::Left, ELureFightRunSide::Right })
		{
			const FLureFightNetState Server = MakeFight(TEXT("Run"), TEXT("Swim"), Side, false);
			const FLureFightNetState Replicated = Server; // what the client receives: the same struct, no extra field
			const FFightFishView Host = MakeView(Data, Server, true);
			const FFightFishView Client = MakeView(Data, Replicated, false);
			TestTrue(TEXT("same bMoveSwims"), Host.bMoveSwims == Client.bMoveSwims);
			TestTrue(TEXT("same MoveSwimSide"), Host.MoveSwimSide == Client.MoveSwimSide);
			const FRotator HostFacing = FFightFishVisual::FightFacing(Data.Row, Velocity, Mouth, Player, FRotator::ZeroRotator, Host.bExhausted, Host.bMoveSwims, Host.MoveSwimSide);
			const FRotator ClientFacing = FFightFishVisual::FightFacing(Data.Row, Velocity, Mouth, Player, FRotator::ZeroRotator, Client.bExhausted, Client.bMoveSwims, Client.MoveSwimSide);
			TestTrue(TEXT("same facing on host and client"), HostFacing.Equals(ClientFacing, 1e-4f));
		}
		// The RandomSide pick is the replicated RunSide: left and right mirror each other.
		const FFightFishView Left = MakeView(Data, MakeFight(TEXT("Run"), TEXT("Swim"), ELureFightRunSide::Left, false), false);
		const FFightFishView Right = MakeView(Data, MakeFight(TEXT("Run"), TEXT("Swim"), ELureFightRunSide::Right, false), false);
		TestTrue(FString::Printf(TEXT("left %.3f mirrors right %.3f"), Left.MoveSwimSide, Right.MoveSwimSide),
			Right.MoveSwimSide > 0.f && FMath::IsNearlyEqual(Left.MoveSwimSide, -Right.MoveSwimSide));
		// A PatternId the client does not know (the server fell back: it replicates None) gives the built-in pattern everywhere.
		const FLureFightPatternRow Fallback = FLureFightPatternRow::GetFallbackPattern();
		TestTrue(TEXT("PatternId None -> the built-in pattern"),
			FFightFishViewAdapter::FindPattern(Data.PatternTable.Get(), NAME_None).Moves.Num() == Fallback.Moves.Num());
		TestTrue(TEXT("no table -> the built-in pattern"), FFightFishViewAdapter::FindPattern(nullptr, TEXT("Run")).Moves.Num() == Fallback.Moves.Num());
		return true;
	}
}

#endif

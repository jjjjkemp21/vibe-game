// T-048: the line meets the fish's mouth (Jimmy, 2026-09-24: "the bobber looks as if the fish is hooked by the tail").
// Project.FishVisual.MouthOnLine.*: the mouth is at the line end, the fish faces the rod with its body away from the player
// (swung sideways at most RunSwingDeg while it swims), no body point is nearer the player than the mouth (seen from above),
// and the bobber stays over the mouth. Data from data/tables/DT_FishVisual.json (never the binary asset).

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fish/FightFishVisual.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"
#include "Fish/LureFightFishSubsystem.h"
#include "Fishing/FightFishViewAdapter.h"

namespace LureMouthOnLineTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	bool LoadRow(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Table, FFishVisualRow& OutRow)
	{
		FString Text;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_FishVisual.json"), Text)
			|| !LureFightQA::MakeTableChecked(Test, Table, FFishVisualRow::StaticStruct(), Text, true, TEXT("DT_FishVisual.json")))
		{
			return false;
		}
		const FFishVisualRow* Row = Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("MouthOnLine"), false);
		if (!Row)
		{
			Test.AddError(TEXT("DT_FishVisual.json has no row named by ULureFishVisualSettings::VisualRow (Default)"));
			return false;
		}
		OutRow = *Row;
		return true;
	}

	/** Angle seen from above between the fish's forward and the direction mouth -> player, degrees. */
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

	// =================================================================================================================
	// The pure rules: fights at several SideDeg, line lengths, swims, sizes and facings before
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualMouthOnLinePure, "Project.FishVisual.MouthOnLine.PureRules", Flags)
	bool FFishVisualMouthOnLinePure::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		if (!LoadRow(*this, Table, Row))
		{
			return false;
		}
		FString Problem;
		TestTrue(FString::Printf(TEXT("the row is valid (%s)"), *Problem), Row.Validate(Problem));
		TestTrue(FString::Printf(TEXT("RunSwingDeg %.1f is in (0, 80]: the body swings but never crosses the line"), Row.RunSwingDeg), Row.RunSwingDeg > 0.f && Row.RunSwingDeg <= 80.f);

		const FVector Player(0.f, 0.f, 100.f);
		const float SideDegs[] = { -70.f, -35.f, 0.f, 35.f, 70.f };
		const float LineOuts[] = { 600.f, 2000.f };
		const float Offsets[] = { 16.f, 27.5f, 70.f }; // mouth offset x scale (small, reference, big fish)
		const float CurrentYaws[] = { 0.f, 60.f, -60.f, 180.f }; // facing before, relative to the direction to the player
		int32 Cases = 0;
		float WorstMouth = 0.f;
		float WorstAngle = 0.f;
		float WorstBody = -UE_BIG_NUMBER;
		float WorstRoll = 0.f;
		for (const float Side : SideDegs)
		{
			for (const float LineOut : LineOuts)
			{
				const FVector Away = FVector::ForwardVector.RotateAngleAxis(Side, FVector::UpVector);
				const FVector Perp(-Away.Y, Away.X, 0.f);
				FFightFishView View;
				View.bFighting = true;
				View.PlayerLocation = Player;
				View.WaterZ = 0.f;
				View.LineEnd = FVector(Player.X, Player.Y, 0.f) + Away * LineOut;
				const FVector Velocities[] = {
					FVector::ZeroVector, Away * 300.f, -Away * 150.f, Perp * 250.f, -Perp * 250.f, (Away + Perp) * 200.f,
					Away * 300.f + FVector(0.f, 0.f, -200.f), -Away * 150.f + FVector(0.f, 0.f, 100.f), Perp * 60.f };
				for (const FVector& Velocity : Velocities)
				{
					for (const bool bTired : { false, true })
					{
						for (const float CurrentYaw : CurrentYaws)
						{
							const float BaseYaw = static_cast<float>((-Away).Rotation().Yaw);
							const FRotator Current(0.f, BaseYaw + CurrentYaw, 0.f);
							const FVector MouthPoint = FFightFishVisual::MouthTarget(Row, View, -UE_BIG_NUMBER);
							const FRotator Desired = FFightFishVisual::FightFacing(Row, Velocity, MouthPoint, Player, Current, bTired);
							// The rule itself stays in the cone; the actor also clamps its smoothed rotation.
							WorstAngle = FMath::Max(WorstAngle, AngleToPlayer(Desired.Vector(), MouthPoint, Player));
							const FRotator Facing = FFightFishVisual::ClampToLine(Row, Desired, MouthPoint, Player);
							WorstRoll = FMath::Max(WorstRoll, bTired ? FMath::Abs(static_cast<float>(Facing.Roll)) : 0.f);
							for (const float Offset : Offsets)
							{
								const FVector Center = FFightFishVisual::TargetLocation(Row, View, Facing.Vector(), Offset, -UE_BIG_NUMBER);
								const FVector Mouth = Center + Facing.Vector() * Offset; // ALureFightFish::GetMouthLocation without a mesh
								WorstMouth = FMath::Max(WorstMouth, static_cast<float>(FVector::Dist2D(Mouth, View.LineEnd)));
								WorstAngle = FMath::Max(WorstAngle, AngleToPlayer(Facing.Vector(), Mouth, Player));
								WorstBody = FMath::Max(WorstBody, BodyNearerThanMouth(Facing.Vector(), Mouth, Player, 2.f * Offset));
								++Cases;
							}
						}
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("cases (%d)"), Cases), Cases > 1000);
		TestTrue(FString::Printf(TEXT("(a) the mouth is at the line end (worst %.3f cm, max 1)"), WorstMouth), WorstMouth <= 1.f);
		TestTrue(FString::Printf(TEXT("(b) forward vs mouth -> player within RunSwingDeg %.0f (worst %.2f deg)"), Row.RunSwingDeg, WorstAngle), WorstAngle <= Row.RunSwingDeg + 0.01f);
		TestTrue(FString::Printf(TEXT("(c) no body point nearer the player than the mouth (worst %.3f cm nearer)"), WorstBody), WorstBody <= 0.01f);
		TestTrue(FString::Printf(TEXT("tired: upright (worst roll %.2f)"), WorstRoll), WorstRoll <= 0.01f);

		// The swing: a fast sideways swim swings the body the full angle, the head toward the swim; still or tired = straight.
		const FVector Mouth(1500.f, 0.f, -25.f);
		const float Base = 180.f; // mouth -> player
		const FRotator Left = FFightFishVisual::FightFacing(Row, FVector(0.f, 400.f, 0.f), Mouth, Player, FRotator(0.f, Base, 0.f), false);
		TestEqual(TEXT("swimming +Y fast: swung the full RunSwingDeg"), FMath::Abs(FMath::FindDeltaAngleDegrees(Base, static_cast<float>(Left.Yaw))), Row.RunSwingDeg, 0.01f);
		TestTrue(FString::Printf(TEXT("... head toward +Y (forward %s)"), *Left.Vector().ToCompactString()), Left.Vector().Y > 0.0);
		const FRotator Right = FFightFishVisual::FightFacing(Row, FVector(0.f, -400.f, 0.f), Mouth, Player, FRotator(0.f, Base, 0.f), false);
		TestTrue(TEXT("swimming -Y: head toward -Y"), Right.Vector().Y < 0.0);
		const FRotator Half = FFightFishVisual::FightFacing(Row, FVector(0.f, 0.5f * Row.RunSwingFullSpeed, 0.f), Mouth, Player, FRotator(0.f, Base, 0.f), false);
		TestEqual(TEXT("half the full-swing speed: half the swing"), FMath::Abs(FMath::FindDeltaAngleDegrees(Base, static_cast<float>(Half.Yaw))), 0.5f * Row.RunSwingDeg, 0.05f);
		const FRotator Out = FFightFishVisual::FightFacing(Row, FVector(400.f, 0.f, 0.f), Mouth, Player, FRotator(0.f, Base - 10.f, 0.f), false);
		TestTrue(TEXT("running straight out: swung to the side it already leaned to"), FMath::FindDeltaAngleDegrees(Base, static_cast<float>(Out.Yaw)) < -Row.RunSwingDeg + 0.5f);
		const FRotator Still = FFightFishVisual::FightFacing(Row, FVector::ZeroVector, Mouth, Player, FRotator(0.f, 20.f, 0.f), false);
		TestEqual(TEXT("still: faces the rod, level"), static_cast<float>(Still.Yaw), Base, 0.01f);
		TestEqual(TEXT("still: level"), static_cast<float>(Still.Pitch), 0.f, 0.01f);
		const FRotator Tired = FFightFishVisual::FightFacing(Row, FVector(0.f, 400.f, 0.f), Mouth, Player, FRotator(0.f, Base, 0.f), true);
		TestEqual(TEXT("tired and dragged sideways: no swing"), FMath::Abs(FMath::FindDeltaAngleDegrees(Base, static_cast<float>(Tired.Yaw))), 0.f, 0.01f);
		const FRotator Dive = FFightFishVisual::FightFacing(Row, FVector(300.f, 0.f, -800.f), Mouth, Player, FRotator(0.f, Base, 0.f), false);
		TestTrue(FString::Printf(TEXT("diving while running out (tail first): the tail leads down, nose up, clamped (pitch %.1f)"), Dive.Pitch),
			Dive.Pitch > 0.f && Dive.Pitch <= Row.MaxPitchDeg + 0.01f);

		// The lag cap: the smoothed line point never trails the target by more than MouthMaxLagCm horizontally.
		const FVector Far = FFightFishVisual::StepMouth(Row, FVector(0.f, 0.f, -25.f), FVector(300.f, 0.f, -80.f), 1.f / 60.f, 0.2f);
		TestEqual(TEXT("StepMouth: 300 cm behind -> MouthMaxLagCm behind"), static_cast<float>(FVector::Dist2D(Far, FVector(300.f, 0.f, 0.f))), Row.MouthMaxLagCm, 0.01f);
		TestTrue(TEXT("StepMouth: Z only smoothed"), Far.Z < -25.f && Far.Z > -80.f);
		const FVector Near = FFightFishVisual::StepMouth(Row, FVector(0.f, 0.f, 0.f), FVector(2.f, 0.f, 0.f), 1.f / 60.f, 0.08f);
		TestEqual(TEXT("StepMouth: a small lag is just smoothed"), static_cast<float>(Near.X), 2.f * FFightFishVisual::SmoothAlpha(1.f / 60.f, 0.08f), 1.e-4f);
		return true;
	}

	// =================================================================================================================
	// The actor through a scripted fight: runs out, swings sideways, reeled in, dives, tired
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualMouthOnLineActor, "Project.FishVisual.MouthOnLine.ActorFollowsRuns", Flags)
	bool FFishVisualMouthOnLineActor::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		LureFightQA::FWorld World;
		if (!LoadRow(*this, Table, Row) || !World.Create(*this))
		{
			return false;
		}
		struct FCase { const TCHAR* Name; float WeightKg; bool bAuthority; };
		const FCase Cases[] = { { TEXT("small, server"), 0.5f, true }, { TEXT("reference, proxy"), 1.5f, false }, { TEXT("big, server"), 12.f, true } };
		for (const FCase& Case : Cases)
		{
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
			Setup.Fish.WeightKg = Case.WeightKg;
			Setup.ReferenceWeightKg = 1.5f;
			Fish->Setup(Setup);

			FFightFishView View;
			View.bFighting = true;
			View.FightId = 1;
			View.MoveId = TEXT("Run");
			View.bHasAuthority = Case.bAuthority;
			View.WaterZ = 0.f;
			View.PlayerLocation = FVector(0.f, 0.f, 100.f);
			float LineOut = 1200.f;
			float SideDeg = 0.f;
			float WorstLag = 0.f;
			float WorstAngle = 0.f;
			float WorstBody = -UE_BIG_NUMBER;
			float MostSwing = 0.f;
			float SettledOff = -1.f;
			int32 Frames = 0;
			constexpr float Dt = LureFightQA::WorldDt;
			for (int32 Frame = 0; Frame < 780; ++Frame)
			{
				const float T = Frame * Dt;
				if (T < 2.f)       { LineOut += 250.f * Dt; }                          // runs straight out
				else if (T < 4.f)  { LineOut += 60.f * Dt; SideDeg += 30.f * Dt; }     // swings across to the left
				else if (T < 6.f)  { LineOut -= 120.f * Dt; SideDeg -= 45.f * Dt; }    // reeled in while it crosses back
				else if (T < 9.f)  { View.bExhausted = true; View.MoveId = NAME_None; LineOut -= 60.f * Dt; } // tired, reeled in
				// else: still for 3 s (the mouth settles on the line end)
				View.DepthCm = T < 9.f ? 75.f + 75.f * FMath::Sin(1.3f * T) : 0.f;
				View.LineEnd = FVector(View.PlayerLocation.X, View.PlayerLocation.Y, 0.f) + FVector::ForwardVector.RotateAngleAxis(SideDeg, FVector::UpVector) * LineOut;
				Fish->ApplyView(View, Dt);
				++Frames;
				const FVector Mouth = Fish->GetMouthLocation();
				const FVector Forward = Fish->GetActorForwardVector();
				WorstLag = FMath::Max(WorstLag, static_cast<float>(FVector::Dist2D(Mouth, View.LineEnd)));
				const float Angle = AngleToPlayer(Forward, Mouth, View.PlayerLocation);
				WorstAngle = FMath::Max(WorstAngle, Angle);
				if (!View.bExhausted)
				{
					MostSwing = FMath::Max(MostSwing, Angle);
				}
				WorstBody = FMath::Max(WorstBody, BodyNearerThanMouth(Forward, Mouth, View.PlayerLocation, Fish->GetBodyLengthCm()));
				SettledOff = static_cast<float>(FVector::Dist2D(Mouth, View.LineEnd));
			}
			TestTrue(FString::Printf(TEXT("%s: ran the fight (%d frames)"), Case.Name, Frames), Frames > 700);
			TestTrue(FString::Printf(TEXT("%s: (a) the mouth never trails the line end by more than MouthMaxLagCm %.0f (worst %.2f cm)"), Case.Name, Row.MouthMaxLagCm, WorstLag),
				WorstLag <= Row.MouthMaxLagCm + 0.01f);
			TestTrue(FString::Printf(TEXT("%s: (a) once the line end rests, the mouth is on it (%.3f cm, max 1)"), Case.Name, SettledOff), SettledOff >= 0.f && SettledOff <= 1.f);
			TestTrue(FString::Printf(TEXT("%s: (b) forward vs mouth -> player within RunSwingDeg %.0f (worst %.2f deg)"), Case.Name, Row.RunSwingDeg, WorstAngle),
				WorstAngle <= Row.RunSwingDeg + 0.05f);
			TestTrue(FString::Printf(TEXT("%s: (c) no body point nearer the player than the mouth (worst %.3f cm nearer)"), Case.Name, WorstBody), WorstBody <= 0.01f);
			TestTrue(FString::Printf(TEXT("%s: while swimming the body swings sideways (most %.1f deg)"), Case.Name, MostSwing), MostSwing >= 0.5f * Row.RunSwingDeg);
			TestEqual(FString::Printf(TEXT("%s: tired at the end: upright"), Case.Name), static_cast<float>(Fish->GetActorRotation().Roll), 0.f, 0.5f);
			Fish->Destroy();
		}
		return true;
	}

	// =================================================================================================================
	// The real fight: the bobber stays over the mouth
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualMouthOnLineBobber, "Project.FishVisual.MouthOnLine.BobberOverMouth", Flags)
	bool FFishVisualMouthOnLineBobber::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		FFishVisualRow Row;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LoadRow(*this, Visual, Row))
		{
			return false;
		}
		FFishInstance Bonefish;
		if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.5f, 481, Bonefish))
		{
			return false;
		}
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		ULureFishingComponent* Fishing = LureFightQA::SetUpFishing(World.Spawn(LureFightQA::StandAt()), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!TestNotNull(TEXT("fight fish subsystem"), Visuals) || !TestNotNull(TEXT("fishing"), Fishing))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());
		if (!LureFightQA::CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hooked"), Fishing->AuthorityHookFish(Bonefish)))
		{
			return false;
		}
		int32 Frames = 0;
		float Worst = 0.f;
		float WorstAngle = 0.f;
		float MaxLineMove = 0.f;
		FVector LastLineEnd = FVector::ZeroVector;
		for (int32 Frame = 0; Frame < 12 * 60 && Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			Fishing->AuthoritySetReeling((Frame / 90) % 2 == 1); // 1.5 s runs, 1.5 s reeling
			World.Tick(1);
			const ALureFightFish* Actor = Visuals->FindFish(Fishing);
			const FFightFishView View = FFightFishViewAdapter::FromComponent(*Fishing);
			if (!Actor || !View.bFighting)
			{
				continue;
			}
			if (Frames > 0)
			{
				MaxLineMove = FMath::Max(MaxLineMove, static_cast<float>(FVector::Dist2D(LastLineEnd, View.LineEnd)));
			}
			LastLineEnd = View.LineEnd;
			++Frames;
			const FVector Mouth = Actor->GetMouthLocation();
			Worst = FMath::Max(Worst, static_cast<float>(FVector::Dist2D(Fishing->GetBobberLocation(), Mouth)));
			WorstAngle = FMath::Max(WorstAngle, AngleToPlayer(Actor->GetActorForwardVector(), Mouth, View.PlayerLocation));
		}
		TestTrue(FString::Printf(TEXT("fought for a while (%d frames)"), Frames), Frames > 120);
		TestTrue(FString::Printf(TEXT("the line end moved (fastest %.1f cm per frame)"), MaxLineMove), MaxLineMove > 0.5f);
		// Tolerance: MouthMaxLagCm (the smoothing) + 2 cm (the Mouth bone of an imported mesh sits a little off the body axis).
		TestTrue(FString::Printf(TEXT("the bobber stays over the mouth (worst %.2f cm, max %.0f)"), Worst, Row.MouthMaxLagCm + 2.f), Worst <= Row.MouthMaxLagCm + 2.f);
		TestTrue(FString::Printf(TEXT("the fish faces the rod (worst %.1f deg from mouth -> player)"), WorstAngle), WorstAngle <= Row.RunSwingDeg + 0.5f);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

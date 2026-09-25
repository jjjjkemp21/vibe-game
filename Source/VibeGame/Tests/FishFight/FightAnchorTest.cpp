// Lure T-045 (unreal-engineer, 2026-09-24): the hooked fish stays put in the world while its angler walks.
// Jimmy (Playtest/2026-09-24 A2): "WHEN a player walks around while reeling in a fish, THEN the bobber + fish follow the players
// movements ... If the player moves away, the bobber and fish should remain in the same place and only be reeled towards the
// player's direction". The fight now keeps the fish's world XY (FLureFightState Anchor/BaseDir, FLureFight::PlaceFish /
// MovePlayer / FishLocation) and replicates it (FLureFightNetState::FishLocation). Spec: docs/specs/reel-fight-rules.md
// "The fish stays put (T-045)". Project.Fishing.Fight.Anchor.*
// Everything lives in namespace LureFightAnchorTest (unity builds: no file-scope using-directives).

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fishing/FightFishViewAdapter.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace LureFightAnchorTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The starter kit with the shipped drag cap (what a new player fights with). */
	inline FLureGearStats StarterKit(const LureFightQA::FFightTables& Data)
	{
		FLureGearStats Kit = Data.Starter();
		FLureGear::ApplyDragLineCap(Kit, Data.Tuning()->DragLineCap);
		return Kit;
	}

	/** A fight with a Common-Bonefish-like fish (pull 2.5, speed 60 cm/s) on the shipped tuning, the player at Player and the fish at Fish. */
	inline FLureFightState MakeFight(const LureFightQA::FFightTables& Data, const FLureFightPatternRow& Pattern, int32 Seed, const FVector2D& Player, const FVector2D& Fish,
		float StaminaPool = 60.f)
	{
		FLureFightState State;
		FLureFight::Begin(State, LureFightQA::MakeFightFish(2.5f, 60.f, StaminaPool), Pattern, TEXT("AnchorTest"), StarterKit(Data), *Data.Tuning(), Seed,
			static_cast<float>(FVector2D::Distance(Player, Fish)));
		FLureFight::PlaceFish(State, Player, Fish);
		return State;
	}

	/** A fish that holds still on the line (pulls, never swims): only the player and the reel can change where it is. */
	inline FLureFightPatternRow StillFish()
	{
		return LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f));
	}

	/** Straight runs, rests and dashes toward you, no sideways swim: the fish's own motion is only along the line. */
	inline FLureFightPatternRow StraightFish()
	{
		FLureFightMove Run = LureFightQA::MakeMove(TEXT("Run"), 1.6f, 1.f, 1.f);
		FLureFightMove Rest = LureFightQA::MakeMove(TEXT("Rest"), 0.6f, 0.f, 0.f, 1000.f, true);
		FLureFightMove Toward = LureFightQA::MakeMove(TEXT("Toward"), 0.8f, 1.2f, -0.6f);
		for (FLureFightMove* Move : { &Run, &Rest, &Toward })
		{
			Move->DurationMin = 0.4f;
			Move->DurationMax = 1.2f;
		}
		return LureFightQA::MakePattern({ Run, Rest, Toward }, TEXT("Run"));
	}

	/** The fight's physics (everything but where the fish is), bit for bit. */
	inline bool SamePhysics(const FLureFightState& A, const FLureFightState& B)
	{
		return A.Tension == B.Tension && A.Stamina == B.Stamina && A.Pull == B.Pull && A.Speed == B.Speed && A.MoveIndex == B.MoveIndex && A.MoveTimeLeft == B.MoveTimeLeft
			&& A.bExhausted == B.bExhausted && A.OverTime == B.OverTime && A.SlackTime == B.SlackTime && A.Depth == B.Depth && A.Outcome == B.Outcome && A.Steps == B.Steps;
	}

	/** The player's path for a walk of Steps steps: 5 m to the side (-Y), then 5 m back (-X), at an even pace. */
	inline FVector2D WalkPath(const FVector2D& Start, int32 Step, int32 Steps)
	{
		const double T = FMath::Clamp(static_cast<double>(Step) / static_cast<double>(FMath::Max(1, Steps)), 0.0, 1.0);
		return T <= 0.5 ? Start + FVector2D(0.0, -500.0 * T * 2.0) : Start + FVector2D(-500.0 * (T - 0.5) * 2.0, -500.0);
	}

	// =================================================================================================================
	// The pure fight
	// =================================================================================================================

	/** A still player: where the fight happens in the world changes nothing, and MovePlayer to the same spot is a no-op (bit for bit). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightAnchorStill, "Project.Fishing.Fight.Anchor.StillPlayerIsThePreAnchorFight", Flags)
	bool FFightAnchorStill::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFightPatternRow* Run = Data.Pattern(TEXT("Run"));
		if (!TestNotNull(TEXT("the shipped Run pattern"), Run))
		{
			return false;
		}
		// A: the pre-T-045 fight (Begin only: the player at the origin, the fish along +X). B: the same fight somewhere else in the world.
		FLureFightState A;
		FLureFight::Begin(A, LureFightQA::MakeFightFish(2.5f, 60.f, 60.f), *Run, TEXT("Run"), StarterKit(Data), *Data.Tuning(), 4242, 1000.f);
		const FVector2D Player(12345.5, -6789.25);
		const FVector2D Direction(0.6, 0.8);
		FLureFightState B = MakeFight(Data, *Run, 4242, Player, Player + Direction * 1000.0);
		TestEqual(TEXT("PlaceFish: LineOut = the distance"), B.LineOut, 1000.f, 1.0e-3f);
		TestTrue(TEXT("PlaceFish: the fish is where it was put"), FLureFight::FishLocation(B).Equals(Player + Direction * 1000.0, 1.0e-3));
		TestTrue(TEXT("Begin alone: the fish along +X from the origin"), FLureFight::FishLocation(A).Equals(FVector2D(1000.0, 0.0), 1.0e-6));

		int32 Mismatches = 0;
		int32 OffFormula = 0;
		double MaxSide = 0.0;
		for (int32 Step = 0; Step < 60 * 60 && !A.IsOver(); ++Step)
		{
			const FLureFightInput Input = LureRodQA::In((Step / 90) % 3 != 0, 0.f, 0.f); // reel two seconds of three
			FLureFight::MovePlayer(B, Player); // the player did not move: nothing may change
			FLureFight::Step(A, Input);
			FLureFight::Step(B, Input);
			Mismatches += (SamePhysics(A, B) && A.LineOut == B.LineOut && A.SideDeg == B.SideDeg) ? 0 : 1;
			// The fish is where the pre-T-045 visual put it: the player + (the start direction turned by SideDeg) x LineOut.
			const FVector Old = FVector(Player, 0.0) + FVector(Direction, 0.0).RotateAngleAxis(B.SideDeg, FVector::UpVector) * B.LineOut;
			OffFormula += FLureFight::FishLocation(B).Equals(FVector2D(Old), 0.01) ? 0 : 1;
			MaxSide = FMath::Max(MaxSide, static_cast<double>(FMath::Abs(B.SideDeg)));
		}
		AddInfo(FString::Printf(TEXT("%d steps, outcome %s, the fish swung up to %.1f deg"), A.Steps, *LureFightQA::OutcomeName(A.Outcome), MaxSide));
		TestTrue(TEXT("the fight ran and swung sideways"), A.Steps > 600 && MaxSide > 5.0);
		TestEqual(TEXT("steps where the fight elsewhere in the world differs (LineOut, SideDeg, tension, stamina, moves, outcome)"), Mismatches, 0);
		TestEqual(TEXT("steps where the fish is not where the old placement rule put it"), OffFormula, 0);
		return true;
	}

	/**
	 *  The acceptance: with no reel input, the player walks 5 m to the side and 5 m back. A fish that holds still stays exactly where it
	 *  was; a swimming fish moves only by its own swimming (compared with the same seed and a still player): the same physics (no extra
	 *  tension from walking), the same run along its line and the same swim across it, and LineOut is the real distance.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightAnchorWalk, "Project.Fishing.Fight.Anchor.WalkingLeavesTheFishPut", Flags)
	bool FFightAnchorWalk::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FVector2D Player(350.0, 0.0);
		const int32 WalkSteps = 600; // 10 m in 10 s
		const FLureFightInput NoReel = LureRodQA::In(false);

		// 1. A fish that holds still: not a millimetre of drift, whatever the player does.
		{
			const FVector2D Fish(1350.0, 250.0);
			FLureFightState Still = MakeFight(Data, StillFish(), 7, Player, Fish);
			FLureFightState Walked = MakeFight(Data, StillFish(), 7, Player, Fish);
			double MaxDrift = 0.0;
			double MaxLineOutError = 0.0;
			int32 PhysicsMismatches = 0;
			for (int32 Step = 1; Step <= WalkSteps + 60; ++Step)
			{
				const FVector2D Now = WalkPath(Player, Step, WalkSteps);
				FLureFight::MovePlayer(Walked, Now);
				FLureFight::Step(Walked, NoReel);
				FLureFight::Step(Still, NoReel);
				MaxDrift = FMath::Max(MaxDrift, FVector2D::Distance(FLureFight::FishLocation(Walked), Fish));
				MaxLineOutError = FMath::Max(MaxLineOutError, FMath::Abs(static_cast<double>(Walked.LineOut) - FVector2D::Distance(Now, Fish)));
				PhysicsMismatches += SamePhysics(Still, Walked) ? 0 : 1;
			}
			AddInfo(FString::Printf(TEXT("still fish: drift %.4f cm while the player walked 10 m; LineOut %.0f cm at the end"), MaxDrift, Walked.LineOut));
			TestTrue(FString::Printf(TEXT("a still fish stays put (drift %.4f cm)"), MaxDrift), MaxDrift < 0.01);
			TestTrue(FString::Printf(TEXT("LineOut is the real distance to the player (error %.4f cm)"), MaxLineOutError), MaxLineOutError < 0.01);
			TestEqual(TEXT("steps where walking changed the fight's physics (tension, stamina, moves...)"), PhysicsMismatches, 0);
			TestFalse(TEXT("the fight is still on"), Walked.IsOver());
		}

		// 2. A fish that runs, rests and dashes toward you (no sideways swim): each step it moves along its line by exactly what the
		//    still-player fight moved it, and the fight is the same fight.
		{
			const FVector2D Fish(1350.0, 0.0);
			FLureFightState Still = MakeFight(Data, StraightFish(), 11, Player, Fish);
			FLureFightState Walked = MakeFight(Data, StraightFish(), 11, Player, Fish);
			double MaxError = 0.0;
			double Travelled = 0.0;
			int32 PhysicsMismatches = 0;
			for (int32 Step = 1; Step <= WalkSteps + 60 && !Still.IsOver(); ++Step)
			{
				const FVector2D Now = WalkPath(Player, Step, WalkSteps);
				FLureFight::MovePlayer(Walked, Now);
				const FVector2D Before = FLureFight::FishLocation(Walked);
				const float LineBefore = Still.LineOut;
				FLureFight::Step(Walked, NoReel);
				FLureFight::Step(Still, NoReel);
				const FVector2D Expected = Before + (Before - Now).GetSafeNormal() * static_cast<double>(Still.LineOut - LineBefore);
				MaxError = FMath::Max(MaxError, FVector2D::Distance(FLureFight::FishLocation(Walked), Expected));
				Travelled += FMath::Abs(static_cast<double>(Still.LineOut - LineBefore));
				PhysicsMismatches += SamePhysics(Still, Walked) ? 0 : 1;
			}
			AddInfo(FString::Printf(TEXT("straight fish: swam %.0f cm along its line; largest step error %.5f cm"), Travelled, MaxError));
			TestTrue(TEXT("the fish swam (runs and dashes)"), Travelled > 100.0);
			TestTrue(FString::Printf(TEXT("the fish moved only by its own swim, along its line (error %.5f cm)"), MaxError), MaxError < 0.01);
			TestEqual(TEXT("steps where walking changed the fight's physics"), PhysicsMismatches, 0);
		}

		// 3. A fish swimming across its line (far out, so neither fight reaches the swing limit): the same swim each step.
		{
			const FVector2D Fish(2350.0, 0.0);
			const FLureFightPatternRow Across = LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Across"), 1.f, 1.f, 0.f, 1.f));
			FLureFightState Still = MakeFight(Data, Across, 13, Player, Fish);
			FLureFightState Walked = MakeFight(Data, Across, 13, Player, Fish);
			double MaxError = 0.0;
			for (int32 Step = 1; Step <= WalkSteps; ++Step)
			{
				FLureFight::MovePlayer(Walked, WalkPath(Player, Step, WalkSteps));
				const FVector2D StillBefore = FLureFight::FishLocation(Still);
				const FVector2D WalkedBefore = FLureFight::FishLocation(Walked);
				FLureFight::Step(Walked, NoReel);
				FLureFight::Step(Still, NoReel);
				const double StillSwim = FVector2D::Distance(FLureFight::FishLocation(Still), StillBefore);
				const double WalkedSwim = FVector2D::Distance(FLureFight::FishLocation(Walked), WalkedBefore);
				MaxError = FMath::Max(MaxError, FMath::Abs(StillSwim - WalkedSwim));
			}
			AddInfo(FString::Printf(TEXT("sideways swim: the fish swung to %.1f deg (still player) / %.1f deg (walked); largest per-step swim difference %.5f cm"),
				Still.SideDeg, Walked.SideDeg, MaxError));
			TestTrue(TEXT("the fish swam across its line"), FMath::Abs(Still.SideDeg) > 5.f && FMath::Abs(Still.SideDeg) < Data.Tuning()->MaxSideDeg);
			TestTrue(FString::Printf(TEXT("the same swim across the line each step (difference %.5f cm)"), MaxError), MaxError < 0.01);
		}
		return true;
	}

	/** Reeling pulls the fish toward where the player is NOW: after a 5 m side step, and while the player keeps walking. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightAnchorReel, "Project.Fishing.Fight.Anchor.ReelPullsTowardTheMovedPlayer", Flags)
	bool FFightAnchorReel::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FVector2D Start(350.0, 0.0);
		const FVector2D Fish(1350.0, 0.0);
		const FLureFightInput Reel = LureRodQA::In(true);

		// The player steps 5 m to the side, then reels.
		{
			FLureFightState State = MakeFight(Data, StillFish(), 21, Start, Fish);
			const FVector2D Moved = Start + FVector2D(0.0, -500.0);
			FLureFight::MovePlayer(State, Moved);
			double WorstCos = 1.0;
			int32 Steps = 0;
			while (!State.IsOver() && Steps < 60 * 60)
			{
				FLureFight::MovePlayer(State, Moved);
				const FVector2D Before = FLureFight::FishLocation(State);
				FLureFight::Step(State, Reel);
				++Steps;
				const FVector2D Moved2 = FLureFight::FishLocation(State) - Before;
				if (Moved2.Size() > 1.0e-3)
				{
					WorstCos = FMath::Min(WorstCos, FVector2D::DotProduct(Moved2.GetSafeNormal(), (Moved - Before).GetSafeNormal()));
				}
			}
			const FVector2D End = FLureFight::FishLocation(State);
			AddInfo(FString::Printf(TEXT("side step then reel: %s after %.1f s; the fish ended %.0f cm from the moved player, %.0f cm from where they stood"),
				*LureFightQA::OutcomeName(State.Outcome), State.Elapsed, FVector2D::Distance(End, Moved), FVector2D::Distance(End, Start)));
			TestEqual(TEXT("landed"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Landed));
			TestTrue(FString::Printf(TEXT("every step pulled the fish straight at the moved player (worst cos %.6f)"), WorstCos), WorstCos > 0.99999);
			TestTrue(TEXT("it came in to the moved player"), FVector2D::Distance(End, Moved) <= Data.Tuning()->LandDistance + 0.5);
			TestTrue(TEXT("... not to where the player stood before"), FVector2D::Distance(End, Start) > 300.0);
		}

		// The player keeps walking sideways while reeling: each step pulls toward the player's position of that step.
		{
			FLureFightState State = MakeFight(Data, StillFish(), 22, Start, Fish);
			double WorstCos = 1.0;
			for (int32 Step = 1; Step <= 240 && !State.IsOver(); ++Step)
			{
				const FVector2D Now = Start + FVector2D(0.0, 2.0 * Step); // 120 cm/s to the right
				FLureFight::MovePlayer(State, Now);
				const FVector2D Before = FLureFight::FishLocation(State);
				FLureFight::Step(State, Reel);
				const FVector2D Moved2 = FLureFight::FishLocation(State) - Before;
				if (Moved2.Size() > 1.0e-3)
				{
					WorstCos = FMath::Min(WorstCos, FVector2D::DotProduct(Moved2.GetSafeNormal(), (Now - Before).GetSafeNormal()));
				}
			}
			TestTrue(FString::Printf(TEXT("walking while reeling: every step pulled toward the player's position then (worst cos %.6f)"), WorstCos), WorstCos > 0.99999);
		}
		return true;
	}

	// =================================================================================================================
	// The world: the real repro and replication
	// =================================================================================================================

	/** The T-006 test dock, widened: a 20 x 20 m platform behind the dock's sea face (x = 400) so the player can walk 5 m each way. */
	inline void AddPlatform(LureFightQA::FWorld& World)
	{
		World.AddBox(FVector(-600.f, 0.f, LureFightQA::DockTop * 0.5f), FVector(1000.f, 1000.f, LureFightQA::DockTop * 0.5f));
	}

	/** Walks Character along Direction (world, flat) until it has gone Distance cm (or MaxFrames); calls Each after every frame. */
	inline float Walk(LureFightQA::FWorld& World, ALurePlayerCharacter* Character, const FVector& Direction, float Distance, TFunctionRef<void()> Each, int32 MaxFrames = 600)
	{
		const FVector From = Character->GetActorLocation();
		for (int32 Frame = 0; Frame < MaxFrames && FVector::Dist2D(Character->GetActorLocation(), From) < Distance; ++Frame)
		{
			Character->AddMovementInput(Direction, 1.f, true);
			World.Tick(1);
			Each();
		}
		return static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), From));
	}

	/**
	 *  The playtest repro in a world: a fish on (holding still, not reeled), the angler walks 5 m to the side and 5 m back along the
	 *  dock. The bobber, the fish's replicated location and the fish visual's line end stay where the fish was hooked (the old code
	 *  moved them with the player); the line out is the real distance and the tension does not change. Then reeling brings the fish
	 *  toward where the angler stands now, and it lands.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightAnchorWorld, "Project.Fishing.Fight.Anchor.BobberAndFishStayPutWhileTheAnglerWalks", Flags)
	bool FFightAnchorWorld::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		FFishInstance Bonefish;
		LureFightQA::FWorld World;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish)
			|| !World.Create(*this))
		{
			return false;
		}
		AddPlatform(World);
		const TStrongObjectPtr<UDataTable> Patterns = LureRodQA::PatternTable(StillFish());
		ALurePlayerCharacter* Character = World.Spawn(LureFightQA::StandAt());
		ULureFishingComponent* Fishing = LureFightQA::SetUpFishing(Character, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get());
		if (!TestNotNull(TEXT("character with fishing"), Fishing))
		{
			return false;
		}
		World.Tick(10);
		if (!LureRodQA::HookAndFight(*this, World, Fishing, Bonefish))
		{
			return false;
		}
		Fishing->AuthoritySetReeling(false);
		World.Tick(120); // the tension has reached its target; from here it only eases off as the fish tires
		const FVector2D Hooked(Fishing->GetNetState().BobberRest.X, Fishing->GetNetState().BobberRest.Y);
		const float SettledTension = Fishing->GetFightNet().Tension;

		double MaxBobber = 0.0;
		double MaxFishLocation = 0.0;
		double MaxLineEnd = 0.0;
		double MaxLineOutError = 0.0;
		double MaxTensionRise = 0.0; // a fish that holds still only eases off as it tires: any rise would be the walking's
		int32 Frames = 0;
		auto Check = [&]()
		{
			if (Fishing->GetFishingState() != ELureFishingState::Hooked || !Fishing->GetFightNet().bActive)
			{
				return;
			}
			++Frames;
			const FLureFightNetState& Net = Fishing->GetFightNet();
			MaxBobber = FMath::Max(MaxBobber, FVector2D::Distance(FVector2D(Fishing->GetBobberLocation()), Hooked));
			MaxFishLocation = FMath::Max(MaxFishLocation, FVector2D::Distance(FVector2D(Net.FishLocation), Hooked));
			MaxLineEnd = FMath::Max(MaxLineEnd, FVector2D::Distance(FVector2D(FFightFishViewAdapter::FromComponent(*Fishing).LineEnd), Hooked));
			MaxLineOutError = FMath::Max(MaxLineOutError, FMath::Abs(static_cast<double>(Net.LineOut) - FVector::Dist2D(Character->GetActorLocation(), FVector(Hooked, 0.0))));
			MaxTensionRise = FMath::Max(MaxTensionRise, static_cast<double>(Net.Tension - SettledTension));
		};
		const float Side = Walk(World, Character, FVector(0.f, -1.f, 0.f), 500.f, Check);
		const float Back = Walk(World, Character, FVector(-1.f, 0.f, 0.f), 500.f, Check);
		AddInfo(FString::Printf(TEXT("walked %.0f cm to the side and %.0f cm back over %d frames; LineOut %.0f cm now"), Side, Back, Frames, Fishing->GetFightNet().LineOut));
		TestTrue(TEXT("the angler walked 5 m to the side and 5 m back"), Side >= 499.f && Back >= 499.f);
		TestTrue(TEXT("the fish stayed on through the walk"), Fishing->GetFishingState() == ELureFishingState::Hooked && Fishing->GetFightNet().bActive);
		TestTrue(FString::Printf(TEXT("the bobber stayed where the fish was hooked (off by up to %.3f cm)"), MaxBobber), MaxBobber < 0.5);
		TestTrue(FString::Printf(TEXT("the replicated fish location stayed put (%.3f cm)"), MaxFishLocation), MaxFishLocation < 0.5);
		TestTrue(FString::Printf(TEXT("the fish visual's line end stayed put (%.3f cm)"), MaxLineEnd), MaxLineEnd < 0.5);
		TestTrue(FString::Printf(TEXT("LineOut followed the real distance (error %.3f cm)"), MaxLineOutError), MaxLineOutError < 1.0);
		TestTrue(FString::Printf(TEXT("walking added no tension (largest rise %.4f)"), MaxTensionRise), MaxTensionRise < 1.0e-3);

		// Reel from where the angler stands now: the fish comes straight toward the angler's position now (on the line from where
		// it was to the angler, within the published location's quantization of 0.05 cm per axis).
		Fishing->AuthoritySetReeling(true);
		const FVector2D Angler(Character->GetActorLocation());
		const FVector2D ReelStart(Fishing->GetFightNet().FishLocation);
		const FVector2D Toward = (Angler - ReelStart).GetSafeNormal();
		double WorstOff = 0.0;
		double Came = 0.0;
		for (int32 Frame = 0; Frame < 30 && Fishing->GetFightNet().bActive; ++Frame)
		{
			World.Tick(1);
			const FVector2D Moved = FVector2D(Fishing->GetFightNet().FishLocation) - ReelStart;
			WorstOff = FMath::Max(WorstOff, FMath::Abs(FVector2D::CrossProduct(Toward, Moved)));
			Came = FVector2D::DotProduct(Toward, Moved);
		}
		TestTrue(FString::Printf(TEXT("reeling pulls the fish toward where the angler stands now (%.1f cm closer, at most %.3f cm off that line)"), Came, WorstOff),
			Came > 10.0 && WorstOff < 0.15);
		const bool bLanded = World.TickUntil([Fishing]() { return Fishing->GetFishingState() != ELureFishingState::Hooked; }, 60 * 60);
		TestTrue(TEXT("the fight ended"), bLanded);
		TestEqual(TEXT("... with the fish landed"), LureFightQA::ResultName(Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed));
		return true;
	}

	/** A copy of a character on another machine: no authority, and it never moves by itself. */
	inline void MakeCopy(ALurePlayerCharacter* Character, ENetRole Role)
	{
		Character->GetCharacterMovement()->SetComponentTickEnabled(false);
		Character->SetRole(Role);
	}

	/**
	 *  Host and clients: the owner's copy and another player's copy (standing elsewhere: where a copy's pawn is must not matter) draw
	 *  the fish where the server's fight has it (within the net quantization), also after the angler walks; a player who joins mid-fight
	 *  gets it with its first update; the owner leaving mid-fight ends the fight as before (Lost, reason Unpossessed) on every machine.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFightAnchorNet, "Project.Fishing.Fight.Anchor.ClientsSeeTheReplicatedFishLocation", Flags)
	bool FFightAnchorNet::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		FFishInstance Bonefish;
		LureFightQA::FWorld World;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish)
			|| !World.Create(*this))
		{
			return false;
		}
		AddPlatform(World);
		const TStrongObjectPtr<UDataTable> Patterns = LureRodQA::PatternTable(StillFish());
		LureRodQA::FOwner Owner;
		if (!Owner.Create(*this, World, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get()))
		{
			return false;
		}
		ULureFishingComponent* Server = Owner.Fishing;
		ALurePlayerCharacter* OwnerCopyPawn = World.Spawn(LureFightQA::StandAt() + FVector(-300.f, 600.f, 0.f));
		ALurePlayerCharacter* OtherCopyPawn = World.Spawn(LureFightQA::StandAt() + FVector(-900.f, -700.f, 0.f));
		ULureFishingComponent* OwnerCopy = LureFightQA::SetUpFishing(OwnerCopyPawn, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get());
		ULureFishingComponent* OtherCopy = LureFightQA::SetUpFishing(OtherCopyPawn, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get());
		if (!TestNotNull(TEXT("owner copy"), OwnerCopy) || !TestNotNull(TEXT("other player's copy"), OtherCopy))
		{
			Owner.Release();
			return false;
		}
		MakeCopy(OwnerCopyPawn, ROLE_AutonomousProxy);
		MakeCopy(OtherCopyPawn, ROLE_SimulatedProxy);
		ALurePlayerCharacter* LatePawn = nullptr;
		auto Restore = [&]()
		{
			for (ALurePlayerCharacter* Pawn : { OwnerCopyPawn, OtherCopyPawn, LatePawn })
			{
				if (Pawn)
				{
					Pawn->SetRole(ROLE_Authority);
				}
			}
			Owner.Release();
		};
		if (!LureRodQA::HookAndFight(*this, World, Server, Bonefish))
		{
			Restore();
			return false;
		}
		Server->AuthoritySetReeling(false);

		// Every machine shows the fish where the server has it. The server publishes the location already quantized as an
		// FVector_NetQuantize10 (0.05 cm per axis), so a copy holds exactly the server's value.
		auto ExpectSeen = [&](ULureFishingComponent* Copy, const TCHAR* Who, const TCHAR* When)
		{
			const FVector2D Truth(Server->GetFightNet().FishLocation);
			const FLureFightNetState& Net = Copy->GetFightNet();
			TestTrue(FString::Printf(TEXT("%s, %s: the fight is on"), Who, When), Net.bActive && Copy->GetFishingState() == ELureFishingState::Hooked);
			TestTrue(FString::Printf(TEXT("%s, %s: the replicated fish location is the server's (%.3f cm off)"), Who, When, FVector2D::Distance(FVector2D(Net.FishLocation), Truth)),
				Net.FishLocation.Equals(Server->GetFightNet().FishLocation, 0.0));
			TestTrue(FString::Printf(TEXT("%s, %s: the bobber is on it"), Who, When), FVector2D::Distance(FVector2D(Copy->GetBobberLocation()), Truth) <= 0.1);
			TestTrue(FString::Printf(TEXT("%s, %s: the fish visual's line end is on it"), Who, When),
				FVector2D::Distance(FVector2D(FFightFishViewAdapter::FromComponent(*Copy).LineEnd), Truth) <= 0.1);
		};
		World.Tick(10);
		LureFightQA::ReplicateFishing(*this, Server, OwnerCopy);
		LureFightQA::ReplicateFishing(*this, Server, OtherCopy);
		const FVector2D Before(Server->GetFightNet().FishLocation);
		ExpectSeen(OwnerCopy, TEXT("owner's copy"), TEXT("after the hook"));
		ExpectSeen(OtherCopy, TEXT("other player's copy"), TEXT("after the hook"));

		// The angler walks 3 m: on every machine the fish stays where it was.
		const float Walked = Walk(World, Owner.Character, FVector(0.f, -1.f, 0.f), 300.f, []() {});
		LureFightQA::ReplicateFishing(*this, Server, OwnerCopy);
		LureFightQA::ReplicateFishing(*this, Server, OtherCopy);
		TestTrue(TEXT("the angler walked 3 m"), Walked >= 299.f);
		TestTrue(TEXT("server: the fish did not move with the angler"), FVector2D::Distance(FVector2D(Server->GetFightNet().FishLocation), Before) < 0.05);
		ExpectSeen(OwnerCopy, TEXT("owner's copy"), TEXT("after the walk"));
		ExpectSeen(OtherCopy, TEXT("other player's copy"), TEXT("after the walk"));

		// A player joining mid-fight: its first update carries the fish's place.
		LatePawn = World.Spawn(LureFightQA::StandAt() + FVector(-1200.f, 300.f, 0.f));
		ULureFishingComponent* Late = LureFightQA::SetUpFishing(LatePawn, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get());
		if (TestNotNull(TEXT("late joiner's copy"), Late))
		{
			MakeCopy(LatePawn, ROLE_SimulatedProxy);
			LureFightQA::ReplicateFishing(*this, Server, Late);
			World.Tick(1);
			ExpectSeen(Late, TEXT("late joiner"), TEXT("first update"));
			TestEqual(TEXT("late joiner: its copy never simulated"), Late->GetFightState().Steps, 0);
		}

		// The owner leaves mid-fight: the fight ends as before, and every machine learns it.
		Owner.Controller->UnPossess();
		TestEqual(TEXT("server: the fight ends (Idle)"), LureFightQA::StateName(Server->GetFishingState()), LureFightQA::StateName(ELureFishingState::Idle));
		TestEqual(TEXT("server: the fish is lost"), LureFightQA::ResultName(Server->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
		TestEqual(TEXT("server: reason Unpossessed"), StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Server->GetNetState().ResultReason)),
			StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(ELureCastBlock::Unpossessed)));
		for (ULureFishingComponent* Copy : { OwnerCopy, OtherCopy, Late })
		{
			if (Copy)
			{
				LureFightQA::ReplicateFishing(*this, Server, Copy);
				TestFalse(TEXT("a copy learns the fight is over"), Copy->GetFightNet().bActive);
				TestEqual(TEXT("... the fish is lost there too"), LureFightQA::ResultName(Copy->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
			}
		}
		World.Tick(2);
		Restore();
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

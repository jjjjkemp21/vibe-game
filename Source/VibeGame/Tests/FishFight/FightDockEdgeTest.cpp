// Lure T-047 (unreal-engineer, 2026-09-24): a hooked fish stops at a dock edge, is lifted up it and in over the top, then lands.
// Jimmy (Playtest/2026-09-24 A2): "WHEN player reels in a fish while standing on a dock, AND the player moves backwards, THEN
// the fish will magically phase thru the dock along with the fishing line. Instead, have proper physics where the fish instead
// is lifted out of the water at the EDGE of the dock, where the fishing line can not phase thru".
// The server looks for an edge between the fish and the angler (FLureFightEdgeQuery, Fishing/FightEdge.h); the pure fight keeps
// the fish in front of it, lifts it up the edge and carries it in over the top (FLureFight::SetEdge / EdgeLineOut /
// MoveOnTheLine); FLureFightNetState::Lift replicates the lift. Spec: docs/specs/reel-fight-rules.md "Dock edges (T-047)".
// Project.Fishing.Fight.DockEdge.*. Everything lives in namespace LureFightDockEdgeTest (unity builds: no file-scope using).

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Components/BoxComponent.h"
#include "Fishing/FightEdge.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Fishing/FishingLineSim.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/FishVisual/FightFishVisualQATestUtils.h"
#include <limits>

namespace LureFightDockEdgeTest
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
	inline FLureFightState MakeFight(const LureFightQA::FFightTables& Data, const FLureFightPatternRow& Pattern, int32 Seed, const FVector2D& Player, const FVector2D& Fish)
	{
		FLureFightState State;
		FLureFight::Begin(State, LureFightQA::MakeFightFish(2.5f, 60.f, 60.f), Pattern, TEXT("DockEdgeTest"), StarterKit(Data), *Data.Tuning(), Seed,
			static_cast<float>(FVector2D::Distance(Player, Fish)));
		FLureFight::PlaceFish(State, Player, Fish);
		return State;
	}

	/** A fish that holds still on the line (pulls, never swims): only the reel moves it. */
	inline FLureFightPatternRow StillFish()
	{
		return LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f));
	}

	/** A fish that only swims sideways (never along the line): only its swing moves it. */
	inline FLureFightPatternRow CirclingFish()
	{
		return LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Circle"), 1.f, 1.f, 0.f, 1.f));
	}

	inline FLureFightEdge MakeEdge(const FVector2D& Point, const FVector2D& Normal, float LandLift)
	{
		FLureFightEdge Edge;
		Edge.bValid = true;
		Edge.Point = Point;
		Edge.Normal = Normal.GetSafeNormal();
		Edge.LandLift = LandLift;
		return Edge;
	}

	/** How far the fish is in front of the edge, cm (negative = behind it, i.e. into the dock). */
	inline double InFront(const FLureFightState& State, const FLureFightEdge& Edge)
	{
		return FVector2D::DotProduct(FLureFight::FishLocation(State) - Edge.Point, Edge.Normal);
	}

	// ---- The world: the QA test dock (an 8 x 8 m box from the water surface z = 0 up to its deck at z = 100) ----

	constexpr double DockHalf = 400.0;

	/** How far XY is outside the dock's footprint, cm (0 = over the dock). */
	inline double OutsideDock(const FVector2D& XY)
	{
		const double Dx = FMath::Max(FMath::Abs(XY.X) - DockHalf, 0.0);
		const double Dy = FMath::Max(FMath::Abs(XY.Y) - DockHalf, 0.0);
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}

	/** The dock as the line's solids (the same box as LureFightQA::FWorld::Create). */
	inline FLureLineColliders DockSolids()
	{
		FLureLineColliders Solids;
		Solids.AddBox(FVector(DockHalf, DockHalf, 0.5 * LureFightQA::DockTop), FTransform::Identity, FTransform(FVector(0.0, 0.0, 0.5 * LureFightQA::DockTop)));
		return Solids;
	}

	/** How deep the deepest line point is inside Solids, cm (0 = all outside; same as LureLineCollisionTests::DeepestPoint). */
	inline double DeepestPoint(const FLureLineColliders& Solids, const TArray<FVector>& Points)
	{
		double Deepest = 0.0;
		for (const FVector& Point : Points)
		{
			Deepest = FMath::Max(Deepest, Solids.Penetration(Point, 0.0));
		}
		return Deepest;
	}

	/** A box of any collision profile, turned (the QA world's AddBox is axis-aligned and BlockAll). */
	inline void AddBox(UWorld* World, const FVector& Center, const FVector& Extent, const FRotator& Rotation = FRotator::ZeroRotator,
		FName Profile = UCollisionProfile::BlockAll_ProfileName)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
		Box->SetBoxExtent(Extent, false);
		Box->SetCollisionProfileName(Profile);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocationAndRotation(Center, Rotation);
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

	/** The T-029 real-fight scene plus the shipped DT_Catch row in the catch subsystem, so a landed fish hangs on the line (T-030). */
	struct FDockScene : LureFightFishQA::FRealScene
	{
		TStrongObjectPtr<UDataTable> CatchTable;
		ULureHandsComponent* Hands = nullptr;

		bool Setup(FAutomationTestBase& Test)
		{
			FString Text;
			if (!Init(Test) || !LureFightQA::ReadSource(Test, TEXT("DT_Catch.csv"), Text)
				|| !LureFightQA::MakeTableChecked(Test, CatchTable, FLureCatchRow::StaticStruct(), Text, false, TEXT("DT_Catch.csv")))
			{
				return false;
			}
			const FLureCatchRow* Row = CatchTable->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("FightDockEdgeTest"), false);
			ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(World.World);
			Hands = ULureHandsComponent::Get(Character);
			if (!Test.TestNotNull(TEXT("DT_Catch.csv Default row"), Row) || !Test.TestNotNull(TEXT("the catch subsystem"), Catch)
				|| !Test.TestNotNull(TEXT("the player's hands"), Hands))
			{
				return false;
			}
			Catch->SetTuning(*Row);
			Catch->SetFreshnessTable(nullptr);
			Catch->SetFishTables(Fish.Get());
			return true;
		}
	};

	/** What a world fight did at the dock, frame by frame (Watch() after every frame of the fight). */
	struct FDockWatch
	{
		float Clearance = 25.f;
		float WaterZ = 0.f;
		int32 Frames = 0;
		/** Deepest the fish got into the dock's footprint + EdgeClearance while on the water or on the edge (not over the top), cm. */
		double WorstInside = 0.0;
		/** Over the footprint: the lowest it was above the deck, cm, and whether it was ever there without being over the top. */
		double LowestOverDeck = std::numeric_limits<double>::max();
		bool bOverDeckBelowTop = false;
		float MaxLift = 0.f;
		float Top = 0.f;
		/** Frames lifted part way up the edge, and how far from the edge's face the fish was then (the most off 25 cm). */
		int32 LiftingFrames = 0;
		double WorstLiftOffEdge = 0.0;
		double WorstLinePoint = 0.0;

		void Watch(const ULureFishingComponent& Fishing, const FLureLineColliders& Dock)
		{
			const FLureFightState& Fight = Fishing.GetFightState();
			if (Fishing.GetFishingState() != ELureFishingState::Hooked || !Fishing.GetFightNet().bActive)
			{
				return;
			}
			++Frames;
			const FVector2D Fish = FLureFight::FishLocation(Fight);
			const double Outside = OutsideDock(Fish);
			const float Lift = Fishing.GetFightNet().Lift;
			const bool bOverTheTop = Fight.Edge.bValid && Lift >= Fight.Edge.LandLift - 0.01f;
			if (!bOverTheTop)
			{
				WorstInside = FMath::Max(WorstInside, static_cast<double>(Clearance) - Outside);
			}
			if (Outside <= 0.0)
			{
				LowestOverDeck = FMath::Min(LowestOverDeck, static_cast<double>(WaterZ + Lift) - LureFightQA::DockTop);
				bOverDeckBelowTop |= !bOverTheTop;
			}
			MaxLift = FMath::Max(MaxLift, Lift);
			if (Fight.Edge.bValid)
			{
				Top = Fight.Edge.LandLift;
			}
			if (Lift > 0.f && !bOverTheTop)
			{
				++LiftingFrames;
				WorstLiftOffEdge = FMath::Max(WorstLiftOffEdge, FMath::Abs(Outside - static_cast<double>(Clearance)));
			}
			if (const ULureFishingLineComponent* Line = Fishing.GetLine())
			{
				WorstLinePoint = FMath::Max(WorstLinePoint, DeepestPoint(Dock, Line->GetPoints()));
			}
		}
	};

	// =================================================================================================================
	// The pure fight
	// =================================================================================================================

	/**
	 *  Reeling a still fish toward an angler behind a dock edge: it stops at the edge (never behind it), goes straight up it to the
	 *  edge's LandLift, comes in over the top, and lands within LandDistance after hanging EdgeLandHold s. Two anglers: 3 m back
	 *  from the edge (the old fight reels the fish through the dock) and at the dock's end (the old fight lands it in the water).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeSimLift, "Project.Fishing.Fight.DockEdge.Sim.StopsLiftsAndComesInOverTheTop", Flags)
	bool FDockEdgeSimLift::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& Tuning = *Data.Tuning();
		const FLureFightPatternRow Still = StillFish();
		struct FCase
		{
			const TCHAR* Name;
			double EdgeAt;
		};
		for (const FCase& Case : { FCase{ TEXT("angler 3 m back from the edge"), 300.0 }, FCase{ TEXT("angler at the dock's end"), 75.0 } })
		{
			const float Top = 140.f;
			const FLureFightEdge Edge = MakeEdge(FVector2D(Case.EdgeAt, 0.0), FVector2D(1.0, 0.0), Top);
			FLureFightState Old = MakeFight(Data, Still, 11, FVector2D::ZeroVector, FVector2D(1000.0, 0.0));
			FLureFightState New = Old;
			FLureFight::SetEdge(New, Edge);

			// Control: without the edge (the pre-T-047 fight) the fish is reeled in on the water to LandDistance and lands there.
			double OldDeepest = 0.0;
			while (!Old.IsOver() && Old.Steps < 60 * 180)
			{
				FLureFight::Step(Old, LureFightQA::Input(true));
				OldDeepest = FMath::Max(OldDeepest, -InFront(Old, Edge));
			}
			TestTrue(FString::Printf(TEXT("%s: control: without the edge it lands on the water, never lifted (%.0f cm into the dock)"), Case.Name, OldDeepest),
				Old.Outcome == ELureFightOutcome::Landed && Old.Lift == 0.f);

			double WorstBehind = 0.0;
			double WorstClimbDrift = 0.0;
			float WorstBelowTopOverDock = 0.f;
			float MaxLift = 0.f;
			float LiftDrop = 0.f;
			float LineOutAtFirstLift = -1.f;
			FVector2D ClimbXY = FVector2D::ZeroVector;
			bool bCameInOverTheTop = false;
			while (!New.IsOver() && New.Steps < 60 * 180)
			{
				const float LiftBefore = New.Lift;
				FLureFight::Step(New, LureFightQA::Input(true));
				const float Wall = FLureFight::EdgeLineOut(New);
				if (New.Lift < Top - 0.01f)
				{
					WorstBehind = FMath::Max(WorstBehind, -InFront(New, Edge)); // on the water or on the edge: never behind it
				}
				if (New.Lift > 0.f && LineOutAtFirstLift < 0.f)
				{
					LineOutAtFirstLift = New.LineOut;
					ClimbXY = FLureFight::FishLocation(New);
				}
				if (New.Lift > 0.f && New.Lift < Top - 0.01f)
				{
					WorstClimbDrift = FMath::Max(WorstClimbDrift, FVector2D::Distance(FLureFight::FishLocation(New), ClimbXY)); // straight up
				}
				if (New.LineOut < Wall - 0.01f)
				{
					bCameInOverTheTop = true;
					WorstBelowTopOverDock = FMath::Max(WorstBelowTopOverDock, Top - New.Lift);
				}
				MaxLift = FMath::Max(MaxLift, New.Lift);
				LiftDrop = FMath::Max(LiftDrop, LiftBefore - New.Lift); // a still fish reeled in never sinks back
			}
			TestTrue(FString::Printf(TEXT("%s: the fish never gets behind the edge on the water (%.4f cm)"), Case.Name, WorstBehind), WorstBehind <= 0.01);
			TestTrue(FString::Printf(TEXT("%s: it stops at the edge (LineOut %.3f when it starts to rise)"), Case.Name, LineOutAtFirstLift),
				FMath::IsNearlyEqual(LineOutAtFirstLift, static_cast<float>(Case.EdgeAt), 0.01f));
			TestTrue(FString::Printf(TEXT("%s: and goes straight up there (drifted %.4f cm)"), Case.Name, WorstClimbDrift), WorstClimbDrift <= 0.01);
			TestTrue(FString::Printf(TEXT("%s: lifted to the edge's LandLift (%.2f)"), Case.Name, MaxLift), MaxLift >= Top - 0.01f);
			TestTrue(FString::Printf(TEXT("%s: over the dock only at the top (at most %.4f below it)"), Case.Name, WorstBelowTopOverDock), WorstBelowTopOverDock <= 0.01f);
			TestTrue(FString::Printf(TEXT("%s: a still fish reeled in never sinks back (%.4f)"), Case.Name, LiftDrop), LiftDrop <= 1.0e-4f);
			TestEqual(FString::Printf(TEXT("%s: it lands"), Case.Name), LureFightQA::OutcomeName(New.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Landed));
			TestTrue(FString::Printf(TEXT("%s: over the top, within LandDistance, after hanging EdgeLandHold (LineOut %.1f, lift %.1f, held %.3f s)"), Case.Name,
				New.LineOut, New.Lift, New.LiftHeld), New.Lift >= Top - 0.01f && New.LineOut <= Tuning.LandDistance && New.LiftHeld > Tuning.EdgeLandHold - 1.0e-4f);
			if (Case.EdgeAt > Tuning.LandDistance)
			{
				TestTrue(FString::Printf(TEXT("%s: it came in over the top to land"), Case.Name), bCameInOverTheTop);
				TestTrue(FString::Printf(TEXT("%s: control: the old fight reeled it through the dock's face (%.0f cm in)"), Case.Name, OldDeepest), OldDeepest > 100.0);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: control: the old fight landed it on the water %.0f cm out, short of the edge"), Case.Name, Old.LineOut),
					Old.LineOut > static_cast<float>(Case.EdgeAt));
			}
		}
		return true;
	}

	/** The line's path past an edge, step by step (FLureFight::MoveOnTheLine / EdgeLineOut / EdgeBlocks / SetEdge). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeSimPath, "Project.Fishing.Fight.DockEdge.Sim.PathPastTheEdge", Flags)
	bool FDockEdgeSimPath::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFightPatternRow Still = StillFish();
		FLureFightState S = MakeFight(Data, Still, 3, FVector2D::ZeroVector, FVector2D(500.0, 0.0));
		TestEqual(TEXT("no edge: no limit"), FLureFight::EdgeLineOut(S), 0.f);
		// An oblique edge: through (300, 50), facing (2, 1). Along +X the fish meets it 325 cm out: ((300, 50) . n) / ((1, 0) . n).
		const FLureFightEdge Edge = MakeEdge(FVector2D(300.0, 50.0), FVector2D(2.0, 1.0), 100.f);
		FLureFight::SetEdge(S, Edge);
		TestTrue(TEXT("the edge stands between the fish and the player"), FLureFight::EdgeBlocks(S));
		TestTrue(FString::Printf(TEXT("EdgeLineOut along +X is 325 (%.4f)"), FLureFight::EdgeLineOut(S)), FMath::IsNearlyEqual(FLureFight::EdgeLineOut(S), 325.f, 1.0e-3f));
		auto Expect = [this, &S](const TCHAR* What, float LineOut, float Lift)
		{
			TestTrue(FString::Printf(TEXT("%s: LineOut %.3f (want %.3f), Lift %.3f (want %.3f)"), What, S.LineOut, LineOut, S.Lift, Lift),
				FMath::IsNearlyEqual(S.LineOut, LineOut, 1.0e-3f) && FMath::IsNearlyEqual(S.Lift, Lift, 1.0e-3f));
		};
		FLureFight::MoveOnTheLine(S, -100.f);
		Expect(TEXT("reeled 100 on the water"), 400.f, 0.f);
		FLureFight::MoveOnTheLine(S, -100.f);
		Expect(TEXT("reeled 100 more: 75 in to the edge, 25 up it"), 325.f, 25.f);
		FLureFight::MoveOnTheLine(S, 10.f);
		Expect(TEXT("the fish takes 10: back down the edge first"), 325.f, 15.f);
		FLureFight::MoveOnTheLine(S, 20.f);
		Expect(TEXT("takes 20: down into the water, then 5 out"), 330.f, 0.f);
		FLureFight::MoveOnTheLine(S, -150.f);
		Expect(TEXT("reeled 150: 5 in, 100 up to the top, 45 in over the dock"), 280.f, 100.f);
		FLureFight::MoveOnTheLine(S, -1000.f);
		Expect(TEXT("reeled all the way: at the player, still at the top"), 0.f, 100.f);
		FLureFight::MoveOnTheLine(S, 400.f);
		Expect(TEXT("a run of 400: out over the dock (325), then 75 down the edge"), 325.f, 25.f);
		S.LineOut = 200.f;
		S.Lift = 0.f;
		FLureFight::MoveOnTheLine(S, 0.f);
		Expect(TEXT("on the water behind the edge (the player walked, a new edge): put back out at it"), 325.f, 0.f);
		S.LineOut = 400.f;
		S.Lift = 50.f;
		FLureFight::MoveOnTheLine(S, 0.f);
		Expect(TEXT("lifted but out on the water, away from the edge: in the water"), 400.f, 0.f);

		// Where the edge does not limit the fish.
		FLureFight::PlaceFish(S, FVector2D::ZeroVector, FVector2D(0.0, -500.0));
		TestEqual(TEXT("a bearing that never meets the edge: no limit"), FLureFight::EdgeLineOut(S), 0.f);
		FLureFight::PlaceFish(S, FVector2D(400.0, 0.0), FVector2D(900.0, 0.0));
		TestFalse(TEXT("the player in front of the edge: it does not stand between them"), FLureFight::EdgeBlocks(S));
		TestEqual(TEXT("... no limit"), FLureFight::EdgeLineOut(S), 0.f);
		// Over the dock with the edge walked round (the player went out in front of it): the fish stays up.
		FLureFight::PlaceFish(S, FVector2D::ZeroVector, FVector2D(200.0, 0.0));
		S.Lift = 100.f;
		FLureFight::MovePlayer(S, FVector2D(400.0, 0.0));
		FLureFight::MoveOnTheLine(S, 0.f);
		TestEqual(TEXT("over the dock, no limit on this bearing: it stays up (it is not over water)"), S.Lift, 100.f);
		// No edge at all: the line only; a lifted fish drops back into the water.
		FLureFight::SetEdge(S, FLureFightEdge());
		FLureFight::PlaceFish(S, FVector2D::ZeroVector, FVector2D(500.0, 0.0));
		S.Lift = 30.f;
		FLureFight::MoveOnTheLine(S, -10.f);
		Expect(TEXT("no edge: reeled 10, a lifted fish drops"), 490.f, 0.f);

		// SetEdge cleans what it is given.
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		FLureFightEdge Bad = Edge;
		Bad.Point.X = NaN;
		FLureFight::SetEdge(S, Bad);
		TestFalse(TEXT("SetEdge: a NaN point is no edge"), S.Edge.bValid);
		Bad = Edge;
		Bad.Normal = FVector2D::ZeroVector;
		FLureFight::SetEdge(S, Bad);
		TestFalse(TEXT("SetEdge: no normal is no edge"), S.Edge.bValid);
		Bad = Edge;
		Bad.Normal = FVector2D(3.0, 4.0);
		Bad.LandLift = -5.f;
		FLureFight::SetEdge(S, Bad);
		TestTrue(TEXT("SetEdge: the normal is made unit length, a negative LandLift is 0"), S.Edge.bValid && S.Edge.Normal.Equals(FVector2D(0.6, 0.8), 1.0e-9) && S.Edge.LandLift == 0.f);
		return true;
	}

	/**
	 *  A fish swimming sideways 4 m out, the edge 3 m out square to the line: it swings until it is against the edge
	 *  (400 cos a = 300: 41.41 deg) and no further; without the edge it swings to MaxSideDeg (50 deg, into the dock).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeSimSwing, "Project.Fishing.Fight.DockEdge.Sim.SwingStopsAtTheEdge", Flags)
	bool FDockEdgeSimSwing::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& Tuning = *Data.Tuning();
		const FLureFightEdge Edge = MakeEdge(FVector2D(300.0, 0.0), FVector2D(1.0, 0.0), 100.f);
		FLureFightState Free = MakeFight(Data, CirclingFish(), 5, FVector2D::ZeroVector, FVector2D(400.0, 0.0));
		FLureFightState Held = Free;
		FLureFight::SetEdge(Held, Edge);
		const double Limit = FMath::RadiansToDegrees(FMath::Acos(300.0 / 400.0));
		double WorstBehind = 0.0;
		double HeldMax = 0.0;
		double FreeMax = 0.0;
		for (int32 Step = 0; Step < 60 * 12 && !Held.IsOver() && !Free.IsOver(); ++Step)
		{
			FLureFight::Step(Held, LureFightQA::Input(false));
			FLureFight::Step(Free, LureFightQA::Input(false));
			WorstBehind = FMath::Max(WorstBehind, -InFront(Held, Edge));
			HeldMax = FMath::Max(HeldMax, static_cast<double>(FMath::Abs(Held.SideDeg)));
			FreeMax = FMath::Max(FreeMax, static_cast<double>(FMath::Abs(Free.SideDeg)));
		}
		TestTrue(TEXT("both fights went on for 12 s"), !Held.IsOver() && !Free.IsOver());
		TestTrue(FString::Printf(TEXT("control: without the edge it swings to MaxSideDeg (%.2f deg)"), FreeMax), FreeMax >= Tuning.MaxSideDeg - 0.01);
		TestTrue(FString::Printf(TEXT("with the edge it swings up to it (%.3f deg of %.3f)"), HeldMax, Limit), HeldMax >= Limit - 0.5 && HeldMax <= Limit + 0.01);
		TestTrue(FString::Printf(TEXT("and never behind it (%.4f cm)"), WorstBehind), WorstBehind <= 0.01);
		TestEqual(TEXT("it is never lifted (not reeled)"), Held.Lift, 0.f);
		return true;
	}

	// =================================================================================================================
	// The data
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeData, "Project.Fishing.Fight.DockEdge.Data.Columns", Flags)
	bool FDockEdgeData::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& Shipped = *Data.Tuning();
		const FLureFishFightRow Defaults;
		TestTrue(TEXT("the shipped Default row has the dock-edge columns at the struct defaults"),
			Shipped.EdgeClearance == Defaults.EdgeClearance && Shipped.EdgeProbeDepth == Defaults.EdgeProbeDepth && Shipped.EdgeProbeHeight == Defaults.EdgeProbeHeight
			&& Shipped.EdgeQueryInterval == Defaults.EdgeQueryInterval && Shipped.EdgeLiftClearance == Defaults.EdgeLiftClearance
			&& Shipped.EdgeTopInset == Defaults.EdgeTopInset && Shipped.EdgeMaxLift == Defaults.EdgeMaxLift && Shipped.EdgeLandHold == Defaults.EdgeLandHold);
		FString Problem;
		TestTrue(TEXT("the shipped dock-edge columns validate: ") + Problem, Shipped.ValidateEdge(Problem));
		TestTrue(TEXT("... and the whole row: ") + Problem, Shipped.Validate(Problem));

		struct FBad
		{
			const TCHAR* What;
			TFunction<void(FLureFishFightRow&)> Break;
		};
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();
		const FBad Bad[] = {
			{ TEXT("EdgeClearance -1"), [](FLureFishFightRow& R) { R.EdgeClearance = -1.f; } },
			{ TEXT("EdgeClearance NaN"), [NaN](FLureFishFightRow& R) { R.EdgeClearance = NaN; } },
			{ TEXT("EdgeClearance 250"), [](FLureFishFightRow& R) { R.EdgeClearance = 250.f; } },
			{ TEXT("EdgeProbeDepth -1"), [](FLureFishFightRow& R) { R.EdgeProbeDepth = -1.f; } },
			{ TEXT("EdgeProbeHeight +Inf"), [Inf](FLureFishFightRow& R) { R.EdgeProbeHeight = Inf; } },
			{ TEXT("no probe height while on"), [](FLureFishFightRow& R) { R.EdgeProbeDepth = 0.f; R.EdgeProbeHeight = 0.f; } },
			{ TEXT("EdgeQueryInterval 2"), [](FLureFishFightRow& R) { R.EdgeQueryInterval = 2.f; } },
			{ TEXT("EdgeLiftClearance -1"), [](FLureFishFightRow& R) { R.EdgeLiftClearance = -1.f; } },
			{ TEXT("EdgeTopInset NaN"), [NaN](FLureFishFightRow& R) { R.EdgeTopInset = NaN; } },
			{ TEXT("EdgeMaxLift below EdgeLiftClearance"), [](FLureFishFightRow& R) { R.EdgeMaxLift = 0.5f * R.EdgeLiftClearance; } },
			{ TEXT("EdgeLandHold -0.5"), [](FLureFishFightRow& R) { R.EdgeLandHold = -0.5f; } },
		};
		for (const FBad& Case : Bad)
		{
			FLureFishFightRow Row = Shipped;
			Case.Break(Row);
			FString Why;
			TestFalse(FString::Printf(TEXT("%s is invalid"), Case.What), Row.Validate(Why));
		}
		FLureFishFightRow Off = Shipped;
		Off.EdgeClearance = 0.f;
		Off.EdgeProbeDepth = 0.f;
		Off.EdgeProbeHeight = 0.f;
		TestTrue(TEXT("EdgeClearance 0 (no edge checks) with no probe is valid: ") + Problem, Off.Validate(Problem));

		// Every column is in DT_FishFight.csv, and they are optional: a CSV without them imports with the defaults.
		TArray<FString> Lines;
		Data.FightCsv.ParseIntoArrayLines(Lines, true);
		TArray<FString> Header;
		TArray<FString> Values;
		if (!TestTrue(TEXT("DT_FishFight.csv has a header and a row"), Lines.Num() >= 2))
		{
			return false;
		}
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		Lines[1].ParseIntoArray(Values, TEXT(","), false);
		for (const TCHAR* Column : { TEXT("EdgeClearance"), TEXT("EdgeProbeDepth"), TEXT("EdgeProbeHeight"), TEXT("EdgeQueryInterval"), TEXT("EdgeLiftClearance"),
			TEXT("EdgeTopInset"), TEXT("EdgeMaxLift"), TEXT("EdgeLandHold") })
		{
			TestTrue(FString::Printf(TEXT("DT_FishFight.csv has the %s column"), Column), Header.ContainsByPredicate([Column](const FString& H) { return H.TrimStartAndEnd() == Column; }));
		}
		TArray<FString> KeptHeader;
		TArray<FString> KeptValues;
		for (int32 Index = 0; Index < Header.Num() && Index < Values.Num(); ++Index)
		{
			if (!Header[Index].TrimStartAndEnd().StartsWith(TEXT("Edge")))
			{
				KeptHeader.Add(Header[Index]);
				KeptValues.Add(Values[Index]);
			}
		}
		TStrongObjectPtr<UDataTable> Table;
		const TArray<FString> Problems = LureFightQA::MakeTable(Table, FLureFishFightRow::StaticStruct(),
			FString::Join(KeptHeader, TEXT(",")) + TEXT("\n") + FString::Join(KeptValues, TEXT(",")) + TEXT("\n"), false);
		TestEqual(TEXT("a CSV without the dock-edge columns imports: ") + FString::Join(Problems, TEXT(" | ")), Problems.Num(), 0);
		const FLureFishFightRow* Without = Table.IsValid() ? Table->FindRow<FLureFishFightRow>(TEXT("Default"), TEXT("FightDockEdgeTest"), false) : nullptr;
		TestTrue(TEXT("... and gets the defaults"), Without && Without->EdgeClearance == Defaults.EdgeClearance && Without->EdgeLiftClearance == Defaults.EdgeLiftClearance
			&& Without->EdgeLandHold == Defaults.EdgeLandHold && Without->EdgeMaxLift == Defaults.EdgeMaxLift);
		return true;
	}

	// =================================================================================================================
	// The server's look for an edge (a world query)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeQuery, "Project.Fishing.Fight.DockEdge.Query.FindsWhatStopsTheFish", Flags)
	bool FDockEdgeQuery::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		LureFightQA::FWorld World;
		if (!Data.Load(*this) || !World.Create(*this))
		{
			return false;
		}
		const FLureFishFightRow& Tuning = *Data.Tuning();
		const float Clearance = Tuning.EdgeClearance;
		// A trigger zone off the dock's side, a piling in open water, a deck on posts (its underside 50 cm over the water) and a
		// 10 degree beach going into the water at x = 2000.
		AddBox(World.World, FVector(800.0, 700.0, 0.0), FVector(50.0, 50.0, 100.0), FRotator::ZeroRotator, UCollisionProfile::OverlapAll_ProfileName);
		AddBox(World.World, FVector(700.0, 1500.0, 25.0), FVector(15.0, 15.0, 125.0));
		AddBox(World.World, FVector(0.0, 3000.0, 60.0), FVector(400.0, 400.0, 10.0));
		AddBox(World.World, FVector(2000.0, -3000.0, -50.0 * FMath::Cos(FMath::DegreesToRadians(10.0))), FVector(1000.0, 300.0, 50.0), FRotator(-10.0, 0.0, 0.0));
		World.Tick(1);

		auto Near = [](double A, double B, double Tolerance) { return FMath::Abs(A - B) <= Tolerance; };
		// Straight at the dock's sea face (x = 400): the edge is EdgeClearance out from it, facing the sea; its top is the deck (z 100).
		FLureFightEdge Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(1000.0, 0.0), FVector2D(150.0, 0.0), 0.f, Tuning);
		TestTrue(FString::Printf(TEXT("the dock's face: an edge at (%.2f, %.2f) facing (%.3f, %.3f), LandLift %.2f"), Edge.Point.X, Edge.Point.Y, Edge.Normal.X, Edge.Normal.Y, Edge.LandLift),
			Edge.bValid && Near(Edge.Point.X, DockHalf + Clearance, 0.5) && Near(Edge.Point.Y, 0.0, 0.5) && Edge.Normal.X > 0.999
			&& Near(Edge.LandLift, LureFightQA::DockTop + Tuning.EdgeLiftClearance, 0.5));
		// Coming in at an angle: the same face.
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(1000.0, 300.0), FVector2D(150.0, 0.0), 0.f, Tuning);
		TestTrue(FString::Printf(TEXT("at an angle: the same face (%.2f, %.2f)"), Edge.Point.X, Edge.Point.Y), Edge.bValid && Near(Edge.Point.X, DockHalf + Clearance, 0.5)
			&& Edge.Normal.X > 0.999);
		// A fish already against the face (held at the edge): the same edge.
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(DockHalf + Clearance, 0.0), FVector2D(150.0, 0.0), 0.f, Tuning);
		TestTrue(FString::Printf(TEXT("a fish touching the face: the edge where it is (%.2f, %.2f)"), Edge.Point.X, Edge.Point.Y), Edge.bValid
			&& Near(Edge.Point.X, DockHalf + Clearance, 0.5) && Edge.Normal.X > 0.99);
		// The way open: off the dock's side, the angler further along the same side.
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(1400.0, 700.0), FVector2D(1000.0, 700.0), 0.f, Tuning);
		TestFalse(TEXT("open water: no edge"), Edge.bValid);
		// Only what stops a cast stops the fish: the trigger zone does not.
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(1000.0, 700.0), FVector2D(600.0, 700.0), 0.f, Tuning);
		TestFalse(TEXT("a trigger zone in the way: no edge"), Edge.bValid);
		// A piling: the fish stops at it; the top is the piling's (z 150).
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(1000.0, 1500.0), FVector2D(300.0, 1500.0), 0.f, Tuning);
		TestTrue(FString::Printf(TEXT("a piling: an edge at x %.2f, LandLift %.2f"), Edge.Point.X, Edge.LandLift), Edge.bValid && Near(Edge.Point.X, 715.0 + Clearance, 0.5)
			&& Near(Edge.LandLift, 150.0 + Tuning.EdgeLiftClearance, 0.5));
		// A deck on posts, its underside 50 cm over the water: the fish can't swim in under it (the probe reaches EdgeProbeHeight up)...
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(1000.0, 3000.0), FVector2D(0.0, 3000.0), 0.f, Tuning);
		TestTrue(FString::Printf(TEXT("a deck 50-70 cm over the water: an edge at x %.2f, LandLift %.2f"), Edge.Point.X, Edge.LandLift), Edge.bValid
			&& Near(Edge.Point.X, DockHalf + Clearance, 0.5) && Near(Edge.LandLift, 70.0 + Tuning.EdgeLiftClearance, 0.5));
		// ... but under a deck higher than EdgeProbeHeight it swims.
		FLureFishFightRow Low = Tuning;
		Low.EdgeProbeHeight = 40.f;
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(1000.0, 3000.0), FVector2D(0.0, 3000.0), 0.f, Low);
		TestFalse(TEXT("EdgeProbeHeight 40: it swims in under the deck"), Edge.bValid);
		// The beach: the fish stops where the ground comes up to the probe (about 10 cm deep), the edge across its path.
		Edge = FLureFightEdgeQuery::Find(World.World, FVector2D(3500.0, -3000.0), FVector2D(500.0, -3000.0), 0.f, Tuning);
		TestTrue(FString::Printf(TEXT("the beach: an edge at x %.1f facing (%.3f, %.3f), LandLift %.1f"), Edge.Point.X, Edge.Normal.X, Edge.Normal.Y, Edge.LandLift),
			Edge.bValid && Edge.Point.X > 2000.0 && Edge.Point.X < 2150.0 && Edge.Normal.X > 0.999 && Edge.LandLift > 0.f && Edge.LandLift < Tuning.EdgeLiftClearance + 20.f);
		// Off: no look at all.
		FLureFishFightRow Off = Tuning;
		Off.EdgeClearance = 0.f;
		TestFalse(TEXT("EdgeClearance 0: no edge"), FLureFightEdgeQuery::Find(World.World, FVector2D(1000.0, 0.0), FVector2D(150.0, 0.0), 0.f, Off).bValid);
		return true;
	}

	// =================================================================================================================
	// The world: Jimmy's repro and the real bonefish, host and client
	// =================================================================================================================

	/**
	 *  Jimmy's repro: a fish on, the angler on the dock reels and walks 2.5 m back. The fish comes to the dock's face and stops
	 *  EdgeClearance out (never in under the dock), goes straight up the face to the deck + EdgeLiftClearance, comes in over the
	 *  deck and lands; then it hangs on the line (T-030). No line point is ever inside the dock, during the fight, the lift or the
	 *  hang. The old code reeled it in on the water to LandDistance: 1 m inside the dock's footprint.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeWalkBack, "Project.Fishing.Fight.DockEdge.World.WalkBackWhileReeling", Flags)
	bool FDockEdgeWalkBack::RunTest(const FString& Parameters)
	{
		FDockScene S;
		if (!S.Setup(*this))
		{
			return false;
		}
		const TStrongObjectPtr<UDataTable> Patterns = S.OnePattern(StillFish());
		S.Fishing->SetFightTables(S.Gear.Get(), Patterns.Get(), S.Data.Fight.Get());
		S.World.Tick(10);
		if (!LureRodQA::HookAndFight(*this, S.World, S.Fishing, S.Bonefish))
		{
			return false;
		}
		const FLureFishFightRow& Tuning = *S.Data.Tuning();
		const FLureLineColliders Dock = DockSolids();
		FDockWatch Watch;
		Watch.Clearance = Tuning.EdgeClearance;
		Watch.WaterZ = static_cast<float>(S.Fishing->GetNetState().BobberRest.Z);
		auto Each = [&]() { Watch.Watch(*S.Fishing, Dock); };

		S.Fishing->AuthoritySetReeling(true);
		const float Back = Walk(S.World, S.Character, FVector(-1.f, 0.f, 0.f), 250.f, Each);
		S.World.TickUntil([&]() { Each(); return S.Fishing->GetFishingState() != ELureFishingState::Hooked; }, 60 * 60);
		S.Fishing->AuthoritySetReeling(false);
		AddInfo(FString::Printf(TEXT("walked %.0f cm back; %d fight frames; top %.1f cm over the water; lifted %d frames; lowest over the deck %.1f cm"),
			Back, Watch.Frames, Watch.Top, Watch.LiftingFrames, Watch.LowestOverDeck));
		TestTrue(TEXT("the angler walked 2.5 m back while reeling"), Back >= 249.f);
		TestEqual(TEXT("the fish is landed"), LureFightQA::ResultName(S.Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed));
		TestTrue(FString::Printf(TEXT("on the water it never came within EdgeClearance of the dock (at most %.3f cm too close)"), Watch.WorstInside), Watch.WorstInside <= 0.05);
		TestTrue(FString::Printf(TEXT("it went up the dock's face (%d frames, at most %.3f cm off the edge)"), Watch.LiftingFrames, Watch.WorstLiftOffEdge),
			Watch.LiftingFrames > 0 && Watch.WorstLiftOffEdge <= 0.05);
		TestTrue(FString::Printf(TEXT("to the deck + EdgeLiftClearance (lift %.1f, top %.1f)"), Watch.MaxLift, Watch.Top),
			Watch.MaxLift >= Watch.Top - 0.01f && Watch.Top > 0.f && FMath::IsNearlyEqual(Watch.WaterZ + Watch.Top, LureFightQA::DockTop + Tuning.EdgeLiftClearance, 1.f));
		TestTrue(FString::Printf(TEXT("over the deck only above it (lowest %.1f cm over the deck)"), Watch.LowestOverDeck),
			!Watch.bOverDeckBelowTop && Watch.LowestOverDeck >= Tuning.EdgeLiftClearance - 1.0);
		TestTrue(FString::Printf(TEXT("no line point inside the dock during the fight and the lift (deepest %.3f cm)"), Watch.WorstLinePoint), Watch.WorstLinePoint <= 0.05);

		// The hang (T-030): the fish on the line under the rod tip, swinging in over the deck.
		const ULureFishingLineComponent* Line = S.Fishing->GetLine();
		ALureFishItem* Hanging = S.Hands ? S.Hands->GetHangingFish() : nullptr;
		TestNotNull(TEXT("the landed fish hangs on the line"), Hanging);
		double WorstHang = Line ? DeepestPoint(Dock, Line->GetPoints()) : 0.0; // the landing frame itself
		double LowestFish = std::numeric_limits<double>::max();
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			S.World.Tick(1);
			if (Line)
			{
				WorstHang = FMath::Max(WorstHang, DeepestPoint(Dock, Line->GetPoints()));
			}
			if (Hanging)
			{
				const FBox Bounds = Hanging->GetComponentsBoundingBox(/*bNonColliding*/ true);
				if (Bounds.IsValid && OutsideDock(FVector2D(Bounds.GetCenter())) <= 0.0)
				{
					LowestFish = FMath::Min(LowestFish, Bounds.Min.Z - LureFightQA::DockTop);
				}
			}
		}
		AddInfo(FString::Printf(TEXT("hang: the fish's lowest point over the deck %.1f cm above it (mesh bounds)"), LowestFish));
		TestTrue(FString::Printf(TEXT("no line point inside the dock while the fish hangs (deepest %.3f cm)"), WorstHang), WorstHang <= 0.05);
		return true;
	}

	/**
	 *  The real bonefish (its shipped pattern: runs, side swims) fought from the dock's end (the angler 50 cm from the face): on the
	 *  water it keeps EdgeClearance from the dock, it is lifted up the face and lands. A client's copy draws it lifted: the
	 *  replicated Lift, the bobber and the fish visual's line end raised by it.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDockEdgeRealFish, "Project.Fishing.Fight.DockEdge.World.RealBonefishFromTheDockEnd", Flags)
	bool FDockEdgeRealFish::RunTest(const FString& Parameters)
	{
		LureFightFishQA::FRealScene S;
		if (!S.Init(*this))
		{
			return false;
		}
		ALurePlayerCharacter* CopyPawn = S.World.Spawn(LureFightQA::StandAt() + FVector(-500.f, 300.f, 0.f));
		ULureFishingComponent* Copy = LureFightQA::SetUpFishing(CopyPawn, S.Fish, S.Gear.Get(), S.Data.Patterns.Get(), S.Data.Fight.Get());
		if (!TestNotNull(TEXT("a client's copy"), Copy))
		{
			return false;
		}
		CopyPawn->GetCharacterMovement()->SetComponentTickEnabled(false);
		CopyPawn->SetRole(ROLE_SimulatedProxy);
		S.World.Tick(10);
		if (!LureRodQA::HookAndFight(*this, S.World, S.Fishing, S.Bonefish))
		{
			CopyPawn->SetRole(ROLE_Authority);
			return false;
		}
		const FLureFishFightRow& Tuning = *S.Data.Tuning();
		const FLureLineColliders Dock = DockSolids();
		FDockWatch Watch;
		Watch.Clearance = Tuning.EdgeClearance;
		Watch.WaterZ = static_cast<float>(S.Fishing->GetNetState().BobberRest.Z);
		bool bCopyChecked = false;
		S.Fishing->AuthoritySetReeling(true);
		S.World.TickUntil([&]()
		{
			Watch.Watch(*S.Fishing, Dock);
			if (!bCopyChecked && S.Fishing->GetFightNet().bActive && S.Fishing->GetFightNet().Lift > 60.f)
			{
				bCopyChecked = true;
				LureFightQA::ReplicateFishing(*this, S.Fishing, Copy);
				const FLureFightNetState& Net = Copy->GetFightNet();
				const float Lift = S.Fishing->GetFightNet().Lift;
				const FFightFishView View = FFightFishViewAdapter::FromComponent(*Copy);
				const double Surface = Copy->GetNetState().BobberRest.Z;
				TestEqual(TEXT("client: the replicated lift is the server's"), Net.Lift, Lift);
				TestTrue(FString::Printf(TEXT("client: the fish visual's line end is lifted (z %.2f, surface %.2f, lift %.2f)"), View.LineEnd.Z, Surface, Lift),
					FMath::IsNearlyEqual(View.LineEnd.Z, Surface + Lift, 0.01) && FMath::IsNearlyEqual(View.WaterZ, static_cast<float>(Surface) + Lift, 0.01f));
				// T-049f: the bobber rides the lift by the hooked-bobber rule (FishingComponent ComputeBobberPose): Surface + Lift, pulled
				// under by BiteDipDepth x (0.5 + 0.5 x tension) and never by a dive while lifted. The old "> Surface + Lift - 20" held only
				// while the line was slack-ish (tension < 1/3 of the line); a fish that still pulls at the lift (the T-049 tune) dips it up
				// to BiteDipDepth (30), the same as on the water.
				const double Dip = Copy->GetProfile().BiteDipDepth * (0.5 + 0.5 * FMath::Clamp(Net.GetTension01(), 0.f, 1.f));
				const double Expected = Surface + Lift - Dip;
				const double ClientZ = Copy->GetBobberLocation().Z;
				TestEqual(TEXT("client: no dive while lifted"), Net.Depth, 0.f);
				TestTrue(FString::Printf(TEXT("client: the bobber is lifted with it (z %.2f = surface %.2f + lift %.2f - dip %.2f at tension %.2f; the server draws %.2f)"),
					ClientZ, Surface, Lift, Dip, Net.GetTension01(), S.Fishing->GetBobberLocation().Z),
					FMath::IsNearlyEqual(ClientZ, Expected, 0.01) && ClientZ >= Surface + Lift - Copy->GetProfile().BiteDipDepth - 0.01
					&& FMath::IsNearlyEqual(ClientZ, S.Fishing->GetBobberLocation().Z, 0.01));
			}
			return S.Fishing->GetFishingState() != ELureFishingState::Hooked;
		}, 60 * 60);
		S.Fishing->AuthoritySetReeling(false);
		CopyPawn->SetRole(ROLE_Authority);
		AddInfo(FString::Printf(TEXT("%d fight frames; top %.1f cm over the water; lifted %d frames"), Watch.Frames, Watch.Top, Watch.LiftingFrames));
		TestEqual(TEXT("the fish is landed"), LureFightQA::ResultName(S.Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed));
		TestTrue(FString::Printf(TEXT("on the water it never came within EdgeClearance of the dock (at most %.3f cm too close)"), Watch.WorstInside), Watch.WorstInside <= 0.05);
		TestTrue(FString::Printf(TEXT("it went up the dock's face to the top (lift %.1f of %.1f, %d frames)"), Watch.MaxLift, Watch.Top, Watch.LiftingFrames),
			Watch.LiftingFrames > 0 && Watch.MaxLift >= Watch.Top - 0.01f && Watch.Top > 0.f);
		TestTrue(TEXT("a client's copy saw it lifted"), bCopyChecked);
		TestTrue(FString::Printf(TEXT("no line point inside the dock (deepest %.3f cm)"), Watch.WorstLinePoint), Watch.WorstLinePoint <= 0.05);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

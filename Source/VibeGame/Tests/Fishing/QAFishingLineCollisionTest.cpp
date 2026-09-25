// Lure T-032e QA (qa-engineer): independent tests for T-032b part A, the fishing line collides with what blocks a cast.
// Project.Fishing.QA.LineCollision.* (see docs/TEST_PLAN.md, T-032).
// Black-box: expectations come from docs/specs/fishing-line.md ("Collision", "Hanging actor", "Known limits") and the contract
// comments in Fishing/FishingLineSim.h, never from the .cpp. Where possible the line is judged against the shapes by independent
// geometry (local-space box clamp, point-to-segment distance), not by FLureLineColliders::Penetration.
// Not duplicated here (the implementer's Project.Fishing.Line.Collision.*): axis-aligned drape, 1 frame = 1 sub-step tunnelling,
// deep bounds-only skip, hanging line through the component, allocations. The new DT_FishingLine columns are covered by the
// reflective QA data tests (Line.QA.Data.EveryRowValid / ValidateMatchesFieldRanges walk every field).
// Everything lives in namespace LureQALineCollisionTests (unity builds: no file-scope using-directives).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"
#include "Templates/Function.h"

namespace LureQALineCollisionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameDt = 1.f / 60.f;

	FLureFishingLineRow Row()
	{
		return FLureFishingLineRules::GetFallbackRow();
	}

	FLureLineSimInput Pinned(const FVector& Start, const FVector& End, float RestLength, const FLureLineColliders* Solids)
	{
		FLureLineSimInput In;
		In.Start = Start;
		In.End = End;
		In.RestLength = RestLength;
		In.Colliders = Solids;
		return In;
	}

	FLureLineSimInput Free(const FVector& Start, const FVector& End, float RestLength, const FLureLineColliders* Solids)
	{
		FLureLineSimInput In = Pinned(Start, End, RestLength, Solids);
		In.bFreeEnd = true;
		In.EndMass = 1.f;
		return In;
	}

	/** Runs Frames frames of In on a fresh Segments-segment line; the result in OutSim. */
	void Run(FLureLineSim& OutSim, const FLureLineSimInput& In, int32 Frames, int32 Segments = 12)
	{
		const FLureFishingLineRow Tuning = Row();
		OutSim.Init(Segments);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			OutSim.Step(FrameDt, In, Tuning);
		}
	}

	double MaxPointGap(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		double Max = A.Num() == B.Num() ? 0.0 : TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < FMath::Min(A.Num(), B.Num()); ++Index)
		{
			Max = FMath::Max(Max, FVector::Dist(A[Index], B[Index]));
		}
		return Max;
	}

	/** The line's height where it passes x = X (the first segment spanning X), cm; the lowest point's height if none does. */
	double HeightAtX(const TArray<FVector>& Points, double X)
	{
		double Lowest = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector& A = Points[Index - 1];
			const FVector& B = Points[Index];
			Lowest = FMath::Min(Lowest, B.Z);
			if ((A.X - X) * (B.X - X) <= 0.0 && A.X != B.X)
			{
				return FMath::Lerp(A.Z, B.Z, (X - A.X) / (B.X - A.X));
			}
		}
		return Lowest;
	}

	/** Calls Visit on every point and on Samples points spread inside each segment (the line as drawn). */
	void ForEachSample(const TArray<FVector>& Points, int32 Samples, TFunctionRef<void(const FVector&, bool)> Visit)
	{
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Visit(Points[Index], true);
			if (Index > 0)
			{
				for (int32 Sample = 1; Sample <= Samples; ++Sample)
				{
					Visit(FMath::Lerp(Points[Index - 1], Points[Index], static_cast<double>(Sample) / (Samples + 1)), false);
				}
			}
		}
	}

	/**
	 *  Independent geometry: a box of HalfExtent placed by Local, then World. A turned element in a non-uniformly scaled owner is
	 *  a parallelepiped (sheared), so its 6 faces are built here from its placed corners (cross products of the placed edges),
	 *  not by composing FTransforms (which can't hold a shear). SignedDistance is the largest face distance: < 0 inside, and
	 *  the line's contract "keep CollisionRadius from the surface" is SignedDistance >= CollisionRadius.
	 */
	struct FQABox
	{
		FVector Normals[6];
		FVector Points[6];
		FVector Centre;

		FQABox(const FVector& HalfExtent, const FTransform& Local, const FTransform& World)
		{
			auto Place = [&Local, &World](const FVector& P) { return World.TransformPosition(Local.TransformPosition(P)); };
			Centre = Place(FVector::ZeroVector);
			int32 Face = 0;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				for (const double Sign : { -1.0, 1.0 })
				{
					FVector C = FVector::ZeroVector;
					C[Axis] = Sign * HalfExtent[Axis];
					FVector U = C;
					U[(Axis + 1) % 3] += HalfExtent[(Axis + 1) % 3];
					FVector V = C;
					V[(Axis + 2) % 3] += HalfExtent[(Axis + 2) % 3];
					const FVector At = Place(C);
					FVector N = FVector::CrossProduct(Place(U) - At, Place(V) - At).GetSafeNormal();
					if (FVector::DotProduct(N, At - Centre) < 0.0)
					{
						N = -N;
					}
					Normals[Face] = N;
					Points[Face] = At;
					++Face;
				}
			}
		}

		double SignedDistance(const FVector& P) const
		{
			double Max = -TNumericLimits<double>::Max();
			for (int32 Face = 0; Face < 6; ++Face)
			{
				Max = FMath::Max(Max, FVector::DotProduct(P - Points[Face], Normals[Face]));
			}
			return Max;
		}

		bool Inside(const FVector& P) const { return SignedDistance(P) < 0.0; }
	};

	/** Result of judging a line against one shape by an independent distance function. */
	struct FClearance
	{
		/** Smallest distance of a point to the shape's surface, cm. */
		double Point = TNumericLimits<double>::Max();
		/** Smallest distance of a point or a segment sample. */
		double Any = TNumericLimits<double>::Max();
	};

	FClearance Clearance(const TArray<FVector>& Points, TFunctionRef<double(const FVector&)> Distance)
	{
		FClearance Out;
		ForEachSample(Points, 32, [&Out, &Distance](const FVector& P, bool bPoint)
		{
			const double D = Distance(P);
			Out.Any = FMath::Min(Out.Any, D);
			if (bPoint)
			{
				Out.Point = FMath::Min(Out.Point, D);
			}
		});
		return Out;
	}

	// =================================================================================================================
	// 1. Tunnelling when a long frame is split into MaxSubsteps sub-steps (the implementer checks 1 sub-step per Step)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineCollisionTunnelSubsteps, "Project.Fishing.QA.LineCollision.NoTunnelAtMaxSubsteps", Flags)
	bool FQALineCollisionTunnelSubsteps::RunTest(const FString& Parameters)
	{
		// A 1 cm plank (top z = 101) 6 m wide under a free end thrown down at 60 m/s. Frames of 1/15 s (exactly MaxSubsteps
		// sub-steps at SubstepRate) and 0.25 s (slow motion, capped at MaxSubsteps): the end moves ~50 cm per sub-step, 50 x the
		// plank. Everything starts above the plank and the plank is wider than the line can reach sideways, so any point below its
		// top has gone through it; the control (no colliders) shows the throw does reach below it.
		const FLureFishingLineRow Tuning = Row();
		const double Radius = Tuning.CollisionRadius;
		const double Top = 101.0;
		FLureLineColliders Plank;
		Plank.AddBox(FVector(300.0, 300.0, 0.5), FTransform::Identity, FTransform(FVector(0.0, 0.0, 100.5)));

		struct FCase { float Dt; FVector EndFrom; FVector Velocity; };
		const FCase Cases[] = {
			{ 1.f / 15.f, FVector(0.0, 0.0, 350.0), FVector(0.0, 0.0, -6000.0) },
			{ 1.f / 15.f, FVector(-100.0, 0.0, 350.0), FVector(1500.0, 500.0, -6000.0) },
			{ 0.25f, FVector(0.0, 0.0, 350.0), FVector(0.0, 0.0, -6000.0) },
			{ 0.25f, FVector(80.0, -60.0, 350.0), FVector(-1200.0, 900.0, -6000.0) },
		};
		for (const FCase& Case : Cases)
		{
			const FString Name = FString::Printf(TEXT("dt %.3f s, thrown %s"), Case.Dt, *Case.Velocity.ToCompactString());
			for (const bool bCollide : { false, true })
			{
				const FLureLineSimInput In = Free(FVector(0.0, 0.0, 400.0), Case.EndFrom, 500.f, bCollide ? &Plank : nullptr);
				FLureLineSim Sim;
				Sim.Init(12);
				Sim.Reset(In);
				Sim.AddEndVelocity(Case.Velocity);
				double Lowest = TNumericLimits<double>::Max();
				int32 Substeps = 0;
				for (int32 Frame = 0; Frame < 30; ++Frame)
				{
					Sim.Step(Case.Dt, In, Tuning);
					Substeps = FMath::Max(Substeps, Sim.GetLastSubsteps());
					for (const FVector& Point : Sim.GetPoints())
					{
						Lowest = FMath::Min(Lowest, Point.Z);
					}
				}
				if (!bCollide)
				{
					TestTrue(FString::Printf(TEXT("%s: control: without the plank the end goes below it (lowest %.1f)"), *Name, Lowest), Lowest < 100.0 - 50.0);
					continue;
				}
				TestEqual(FString::Printf(TEXT("%s: each frame runs MaxSubsteps sub-steps"), *Name), Substeps, Tuning.MaxSubsteps);
				TestTrue(FString::Printf(TEXT("%s: no point ever gets below the plank's top + CollisionRadius (lowest %.4f, top %.1f)"), *Name, Lowest, Top),
					Lowest >= Top + Radius - 0.01);
				const FVector End = Sim.GetEnd();
				TestTrue(FString::Printf(TEXT("%s: the end comes to rest on the plank (end %s)"), *Name, *End.ToCompactString()),
					End.Z <= Top + Radius + 1.0 && FMath::Abs(End.X) < 300.0 && FMath::Abs(End.Y) < 300.0);
				TestTrue(FString::Printf(TEXT("%s: finite"), *Name), Sim.IsFinite());
			}
		}
		return true;
	}

	// =================================================================================================================
	// 2. A rotated, non-uniformly scaled box (judged by independent box geometry)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineCollisionRotatedBox, "Project.Fishing.QA.LineCollision.RotatedScaledBox", Flags)
	bool FQALineCollisionRotatedBox::RunTest(const FString& Parameters)
	{
		// A crate tipped over (pitch 20, yaw 35, roll 10) and scaled (1.5, 1, 0.8), its centre at (0, 0, 60), under the middle of
		// a slack line between two raised ends. Once as a world-placed box, once as a box element offset inside a rotated,
		// scaled owner (Local then World). Every point keeps CollisionRadius from the real box and no drawn sample enters it.
		const FLureFishingLineRow Tuning = Row();
		const double Radius = Tuning.CollisionRadius;
		const FVector Half(80.0, 40.0, 20.0);
		const FTransform Placed(FRotator(20.0, 35.0, 10.0), FVector(0.0, 0.0, 60.0), FVector(1.5, 1.0, 0.8));
		// The same box as an element: 30 cm along its owner's X, the owner turned 90 deg yaw and scaled 2 x on its X.
		const FTransform ElementLocal(FRotator(0.0, -30.0, 0.0), FVector(30.0, 0.0, 0.0));
		const FTransform Owner(FRotator(15.0, 90.0, 0.0), FVector(0.0, -40.0, 70.0), FVector(2.0, 1.0, 1.0));
		struct FCase { const TCHAR* Name; FTransform Local; FTransform World; };
		const FCase Cases[] = { { TEXT("tipped crate"), FTransform::Identity, Placed }, { TEXT("box element in a turned, scaled owner"), ElementLocal, Owner } };
		for (const FCase& Case : Cases)
		{
			const FQABox Box(Half, Case.Local, Case.World);
			TestTrue(FString::Printf(TEXT("%s: the test's own geometry has its centre inside"), Case.Name), Box.Inside(Box.Centre));
			const FVector Centre = Box.Centre;
			FLureLineColliders Solids;
			if (!TestTrue(FString::Printf(TEXT("%s: added"), Case.Name), Solids.AddBox(Half, Case.Local, Case.World)))
			{
				continue;
			}
			const FVector Tip(-300.0, Centre.Y, 170.0);
			const FVector End(300.0, Centre.Y, 170.0);
			FLureLineSim Without;
			FLureLineSim FreeStart;
			Run(FreeStart, Pinned(Tip, End, 720.f, nullptr), 1, 24);
			Run(Without, Pinned(Tip, End, 720.f, nullptr), 240, 24);
			const double Before = HeightAtX(FreeStart.GetPoints(), Centre.X);
			const double After = HeightAtX(Without.GetPoints(), Centre.X);
			TestTrue(FString::Printf(TEXT("%s: control: without collision the line sags through the box's centre (z %.1f -> %.1f past %.1f)"), Case.Name, Before, After, Centre.Z),
				Before > Centre.Z && After < Centre.Z);

			FLureLineSim With;
			With.Init(24);
			double WorstPoint = TNumericLimits<double>::Max();
			double WorstAny = TNumericLimits<double>::Max();
			for (int32 Frame = 0; Frame < 240; ++Frame)
			{
				With.Step(FrameDt, Pinned(Tip, End, 720.f, &Solids), Tuning);
				const FClearance C = Clearance(With.GetPoints(), [&Box](const FVector& P) { return Box.SignedDistance(P); });
				WorstPoint = FMath::Min(WorstPoint, C.Point);
				WorstAny = FMath::Min(WorstAny, C.Any);
			}
			const FClearance Last = Clearance(With.GetPoints(), [&Box](const FVector& P) { return Box.SignedDistance(P); });
			TestTrue(FString::Printf(TEXT("%s: every point keeps CollisionRadius %.1f from the box every frame (closest %.4f cm)"), Case.Name, Radius, WorstPoint),
				WorstPoint >= Radius - 0.01);
			TestTrue(FString::Printf(TEXT("%s: no drawn sample ever enters the real box (closest %.4f cm)"), Case.Name, WorstAny), WorstAny > 0.0);
			TestTrue(FString::Printf(TEXT("%s: the settled line rests on it (closest %.3f cm, within CollisionRadius + 1)"), Case.Name, Last.Any), Last.Any <= Radius + 1.0);
			TestTrue(FString::Printf(TEXT("%s: it is held up (z %.1f at the centre, free line %.1f)"), Case.Name, HeightAtX(With.GetPoints(), Centre.X), After),
				HeightAtX(With.GetPoints(), Centre.X) > After + 20.0);
			TestTrue(FString::Printf(TEXT("%s: finite"), Case.Name), With.IsFinite());
		}
		return true;
	}

	// =================================================================================================================
	// 3. Spheres and tilted capsules (judged by independent point-to-segment distances)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineCollisionRounded, "Project.Fishing.QA.LineCollision.SphereAndTiltedCapsule", Flags)
	bool FQALineCollisionRounded::RunTest(const FString& Parameters)
	{
		// A slack line (720 cm over a 600 cm chord, 24 segments) between two ends at z = 150 sags onto: a buoy (sphere r 20 off
		// the middle, where segment ends don't sit on its top), a tilted rail (capsule r 6 across the line, rising 50 cm over its
		// length), and a thin rope rail (capsule r 1.5, thinner than a segment's spacing). Distances are measured to the true
		// shape: points >= r + CollisionRadius, drawn samples >= r + CollisionRadius - 0.1 once settled and never inside.
		const FLureFishingLineRow Tuning = Row();
		const double Radius = Tuning.CollisionRadius;
		struct FCase { const TCHAR* Name; FVector A; FVector B; double R; };
		const FCase Cases[] = {
			{ TEXT("buoy"), FVector(140.0, 0.0, 70.0), FVector(140.0, 0.0, 70.0), 20.0 },
			{ TEXT("tilted rail"), FVector(-100.0, -150.0, 40.0), FVector(100.0, 150.0, 90.0), 6.0 },
			{ TEXT("rope rail"), FVector(-12.0, -200.0, 60.0), FVector(12.0, 200.0, 60.0), 1.5 },
		};
		const FVector Tip(-300.0, 0.0, 150.0);
		const FVector End(300.0, 0.0, 150.0);
		for (const FCase& Case : Cases)
		{
			const bool bSphere = Case.A == Case.B;
			const FVector Mid = 0.5 * (Case.A + Case.B);
			auto Distance = [&Case](const FVector& P) { return FMath::Max(0.0, FMath::PointDistToSegment(P, Case.A, Case.B) - Case.R); };
			FLureLineColliders Solids;
			const bool bAdded = bSphere ? Solids.AddSphere(Case.A, Case.R) : Solids.AddCapsule(Case.A, Case.B, Case.R);
			if (!TestTrue(FString::Printf(TEXT("%s: added"), Case.Name), bAdded))
			{
				continue;
			}
			FLureLineSim Without;
			Run(Without, Pinned(Tip, End, 720.f, nullptr), 240, 24);
			TestTrue(FString::Printf(TEXT("%s: control: the free line ends up below it (z %.1f at x %.0f)"), Case.Name, HeightAtX(Without.GetPoints(), Mid.X), Mid.X),
				HeightAtX(Without.GetPoints(), Mid.X) < Mid.Z - Case.R);

			FLureLineSim With;
			With.Init(24);
			double WorstPoint = TNumericLimits<double>::Max();
			double WorstAny = TNumericLimits<double>::Max();
			for (int32 Frame = 0; Frame < 240; ++Frame)
			{
				With.Step(FrameDt, Pinned(Tip, End, 720.f, &Solids), Tuning);
				const FClearance C = Clearance(With.GetPoints(), Distance);
				WorstPoint = FMath::Min(WorstPoint, C.Point);
				WorstAny = FMath::Min(WorstAny, C.Any);
			}
			const FClearance Last = Clearance(With.GetPoints(), Distance);
			TestTrue(FString::Printf(TEXT("%s: every point keeps CollisionRadius from it every frame (closest %.4f cm)"), Case.Name, WorstPoint), WorstPoint >= Radius - 0.01);
			TestTrue(FString::Printf(TEXT("%s: no drawn sample ever enters it (closest %.4f cm)"), Case.Name, WorstAny), WorstAny > 0.0);
			TestTrue(FString::Printf(TEXT("%s: once settled the drawn line keeps ~CollisionRadius from it (closest %.4f cm)"), Case.Name, Last.Any), Last.Any >= Radius - 0.1);
			TestTrue(FString::Printf(TEXT("%s: the settled line rests on it (closest %.3f cm)"), Case.Name, Last.Any), Last.Any <= Radius + 1.0);
			TestTrue(FString::Printf(TEXT("%s: it lies over it, not under (z %.1f at x %.0f)"), Case.Name, HeightAtX(With.GetPoints(), Mid.X), Mid.X),
				HeightAtX(With.GetPoints(), Mid.X) > Mid.Z);
		}
		return true;
	}

	// =================================================================================================================
	// 4. Bounds-only boxes: skipped exactly while the tip or a PINNED end is inside the box grown by CollisionRadius
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineCollisionBoundsOnlyGrown, "Project.Fishing.QA.LineCollision.BoundsOnlyGrownBoundary", Flags)
	bool FQALineCollisionBoundsOnlyGrown::RunTest(const FString& Parameters)
	{
		// A boat's stand-in box (x -200..200, y -150..150, z 0..300). "Ignored while the rod tip or a pinned end is inside it
		// (grown by CollisionRadius)": half a radius outside the real box counts as inside (the line equals the free line
		// exactly); a radius and a half outside does not (the line is kept out of the box). A free end inside does not count.
		const FLureFishingLineRow Tuning = Row();
		const double Radius = Tuning.CollisionRadius;
		const FBox HullBox(FVector(-200.0, -150.0, 0.0), FVector(200.0, 150.0, 300.0));
		FLureLineColliders Hull;
		Hull.AddBoundsBox(HullBox);
		const FQABox Box(HullBox.GetExtent(), FTransform::Identity, FTransform(HullBox.GetCenter()));
		const double Inner = 0.5 * Radius;
		const double Outer = 1.5 * Radius;

		auto Judge = [this, &Box, Radius](const TCHAR* Name, const FLureLineSimInput& In, bool bExpectSkipped)
		{
			FLureLineSimInput FreeIn = In;
			FreeIn.Colliders = nullptr;
			FLureLineSim With;
			FLureLineSim Without;
			Run(With, In, 180);
			Run(Without, FreeIn, 180);
			const FClearance Control = Clearance(Without.GetPoints(), [&Box](const FVector& P) { return Box.Inside(P) ? 0.0 : 1.0; });
			TestTrue(FString::Printf(TEXT("%s: control: the free line passes through the box"), Name), Control.Any == 0.0);
			const double Gap = MaxPointGap(With.GetPoints(), Without.GetPoints());
			if (bExpectSkipped)
			{
				TestEqual(FString::Printf(TEXT("%s: the box is ignored (the line is exactly the free line)"), Name), Gap, 0.0);
			}
			else
			{
				int32 PointsInside = 0;
				for (int32 Index = 1; Index < With.GetPoints().Num() - (In.bFreeEnd ? 0 : 1); ++Index)
				{
					PointsInside += Box.SignedDistance(With.GetPoints()[Index]) < Radius - 0.01 ? 1 : 0;
				}
				TestTrue(FString::Printf(TEXT("%s: the box is collided (the line differs from the free line by %.1f cm)"), Name, Gap), Gap > 1.0);
				TestEqual(FString::Printf(TEXT("%s: no free point is inside the box grown by CollisionRadius"), Name), PointsInside, 0);
			}
		};

		// The tip just outside the -X face (the angler leans over the gunwale), the end pinned on the water past the far side.
		for (const bool bInGrown : { true, false })
		{
			const double X = -200.0 - (bInGrown ? Inner : Outer);
			Judge(bInGrown ? TEXT("tip 0.5 x CollisionRadius outside the real box") : TEXT("tip 1.5 x CollisionRadius outside the real box"),
				Pinned(FVector(X, 0.0, 250.0), FVector(450.0, 0.0, 0.0), 1000.f, &Hull), bInGrown);
		}
		// A pinned end just above the top (a bobber resting on the deck's stand-in box), the tip outside and higher.
		for (const bool bInGrown : { true, false })
		{
			const double Z = 300.0 + (bInGrown ? Inner : Outer);
			Judge(bInGrown ? TEXT("pinned end 0.5 x CollisionRadius over the top") : TEXT("pinned end 1.5 x CollisionRadius over the top"),
				Pinned(FVector(-450.0, 0.0, 450.0), FVector(0.0, 0.0, Z), 900.f, &Hull), bInGrown);
		}
		// A FREE end laid inside the box (a snapped end falling onto the boat): not a reason to skip it; the tip is outside.
		{
			FLureLineSimInput In = Free(FVector(-450.0, 0.0, 450.0), FVector(0.0, 0.0, 200.0), 900.f, &Hull);
			FLureLineSimInput FreeIn = In;
			FreeIn.Colliders = nullptr;
			FLureLineSim With;
			FLureLineSim Without;
			Run(With, In, 180);
			Run(Without, FreeIn, 180);
			TestTrue(TEXT("free end inside: control: the free line's end falls through the box"), Without.GetEnd().Z < 0.0);
			TestTrue(FString::Printf(TEXT("free end inside: the box still collides (the line differs from the free line by %.1f cm; end %s)"),
				MaxPointGap(With.GetPoints(), Without.GetPoints()), *With.GetEnd().ToCompactString()), MaxPointGap(With.GetPoints(), Without.GetPoints()) > 1.0);
		}
		return true;
	}

	// =================================================================================================================
	// 5. MaxNearSolids (64) applies per kind, to the first NEAR solids in the colliders' order
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineCollisionNearCap, "Project.Fishing.QA.LineCollision.NearSolidsCapPerKind", Flags)
	bool FQALineCollisionNearCap::RunTest(const FString& Parameters)
	{
		// A slack line sags onto a blocker (a box or a sphere, top z = 70) under its middle. Fillers are small solids near the line
		// (40 cm beside it, inside the reach) that it never touches, or far away (100 m, outside the reach). Spec: "At most 64
		// hulls/boxes and 64 spheres/capsules per Step (the first ones in the colliders' order); the rest are ignored that Step",
		// counting only solids near the line.
		const FLureFishingLineRow Tuning = Row();
		const double Radius = Tuning.CollisionRadius;
		constexpr int32 Cap = 64;
		enum class EKind : uint8 { Box, Sphere };
		auto AddBlocker = [](FLureLineColliders& Solids, EKind Kind)
		{
			return Kind == EKind::Box ? Solids.AddBox(FVector(60.0, 60.0, 10.0), FTransform::Identity, FTransform(FVector(0.0, 0.0, 60.0)))
				: Solids.AddSphere(FVector(0.0, 0.0, 30.0), 40.0);
		};
		auto AddFillers = [](FLureLineColliders& Solids, EKind Kind, int32 Count, bool bFar)
		{
			bool bOk = true;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector At = bFar ? FVector(10000.0 + 20.0 * Index, 10000.0, 0.0) : FVector(-250.0 + 500.0 * Index / FMath::Max(1, Count - 1), 40.0, 170.0);
				bOk &= Kind == EKind::Box ? Solids.AddBox(FVector(2.0), FTransform::Identity, FTransform(At)) : Solids.AddSphere(At, 2.0);
			}
			return bOk;
		};

		struct FCase
		{
			const TCHAR* Name;
			EKind FillerKind;
			int32 Fillers;
			bool bFar;
			bool bBlockerFirst;
			EKind BlockerKind;
			bool bCollides;
		};
		const FCase Cases[] = {
			{ TEXT("63 near boxes, then the box blocker (64th)"), EKind::Box, Cap - 1, false, false, EKind::Box, true },
			{ TEXT("64 near boxes, then the box blocker (65th)"), EKind::Box, Cap, false, false, EKind::Box, false },
			{ TEXT("the box blocker first, then 64 near boxes"), EKind::Box, Cap, false, true, EKind::Box, true },
			{ TEXT("64 near boxes, then a sphere blocker (other kind)"), EKind::Box, Cap, false, false, EKind::Sphere, true },
			{ TEXT("64 near spheres, then a box blocker (other kind)"), EKind::Sphere, Cap, false, false, EKind::Box, true },
			{ TEXT("63 near spheres, then the sphere blocker (64th)"), EKind::Sphere, Cap - 1, false, false, EKind::Sphere, true },
			{ TEXT("64 near spheres, then the sphere blocker (65th)"), EKind::Sphere, Cap, false, false, EKind::Sphere, false },
			{ TEXT("64 far boxes, then the box blocker (far solids don't count)"), EKind::Box, Cap, true, false, EKind::Box, true },
			{ TEXT("64 far spheres, then the sphere blocker (far solids don't count)"), EKind::Sphere, Cap, true, false, EKind::Sphere, true },
		};
		const FVector Tip(-300.0, 0.0, 150.0);
		const FVector End(300.0, 0.0, 150.0);
		for (const FCase& Case : Cases)
		{
			FLureLineColliders Solids;
			Solids.Reserve(8 * 200, 200, 200);
			bool bOk = true;
			if (Case.bBlockerFirst)
			{
				bOk &= AddBlocker(Solids, Case.BlockerKind);
			}
			bOk &= AddFillers(Solids, Case.FillerKind, Case.Fillers, Case.bFar);
			if (!Case.bBlockerFirst)
			{
				bOk &= AddBlocker(Solids, Case.BlockerKind);
			}
			if (!TestTrue(FString::Printf(TEXT("%s: every solid added"), Case.Name), bOk && Solids.NumSolids() == Case.Fillers + 1))
			{
				continue;
			}
			FLureLineColliders BlockerOnly;
			AddBlocker(BlockerOnly, Case.BlockerKind);
			FLureLineSim Sim;
			Run(Sim, Pinned(Tip, End, 700.f, &Solids), 180);
			const double Deepest = [&BlockerOnly, &Sim, Radius]
			{
				double Max = 0.0;
				for (const FVector& Point : Sim.GetPoints())
				{
					Max = FMath::Max(Max, BlockerOnly.Penetration(Point, Radius));
				}
				return Max;
			}();
			const double MidZ = HeightAtX(Sim.GetPoints(), 0.0);
			if (Case.bCollides)
			{
				TestTrue(FString::Printf(TEXT("%s: the blocker is collided (deepest point %.4f cm, z %.1f over it)"), Case.Name, Deepest, MidZ),
					Deepest <= 0.01 && MidZ >= 70.0);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: the blocker is ignored (the line sags through it to z %.1f, under its z 50..70)"), Case.Name, MidZ),
					MidZ < 50.0);
			}
			TestTrue(FString::Printf(TEXT("%s: finite"), Case.Name), Sim.IsFinite());
		}
		return true;
	}

	// =================================================================================================================
	// 6. Without colliders (null, empty, far away, near but never touched) the line is bit-identical to the free sim
	// =================================================================================================================

	/** A busy deterministic script: moving tip, pinned end on the water, length changes, a snap with recoil, a free end in flight. */
	void RunScript(const FLureLineColliders* Solids, TArray<TArray<FVector>>& OutFrames)
	{
		const FLureFishingLineRow Tuning = Row();
		FLureLineSim Sim;
		Sim.Init(16);
		OutFrames.Reset();
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			const double T = Frame * FrameDt;
			FLureLineSimInput In = Pinned(FVector(-100.0 + 60.0 * FMath::Sin(2.0 * T), 30.0 * FMath::Cos(3.0 * T), 250.0 + 20.0 * FMath::Sin(5.0 * T)),
				FVector(600.0 + 100.0 * FMath::Sin(0.7 * T), 80.0 * FMath::Sin(1.3 * T), 0.0), static_cast<float>(900.0 - 150.0 * FMath::Min(1.0, T / 2.0)), Solids);
			In.bHasWater = true;
			In.WaterZ = 0.f;
			In.Float = Tuning.FloatStrength;
			if (Frame == 200)
			{
				Sim.ReleaseEnd(1.f);
				Sim.AddRecoil(Tuning.RecoilSpeed);
			}
			if (Frame >= 200)
			{
				In.bFreeEnd = true;
				In.EndMass = 1.f;
				In.End = Sim.GetEnd();
				In.RestLength = 600.f;
			}
			Sim.Step(Frame == 120 ? 0.3f : FrameDt, In, Tuning);
			OutFrames.Add(Sim.GetPoints());
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineCollisionNoColliders, "Project.Fishing.QA.LineCollision.NoCollidersSameAsFreeSim", Flags)
	bool FQALineCollisionNoColliders::RunTest(const FString& Parameters)
	{
		// Collision must cost the free line nothing: with no solid to touch, every point of every frame equals the old free sim
		// exactly (moving ends, water, a length change, a hitch past MaxSubsteps, a snap and its recoil).
		TArray<TArray<FVector>> Reference;
		RunScript(nullptr, Reference);
		double Lowest = TNumericLimits<double>::Max();
		for (const TArray<FVector>& Points : Reference)
		{
			for (const FVector& Point : Points)
			{
				Lowest = FMath::Min(Lowest, Point.Z);
			}
		}

		FLureLineColliders Empty;
		Empty.Reserve(64, 8, 8);
		FLureLineColliders Far;
		Far.AddBox(FVector(100.0), FTransform::Identity, FTransform(FVector(0.0, 10000.0, 0.0)));
		Far.AddSphere(FVector(-10000.0, 0.0, 100.0), 50.0);
		Far.AddCapsule(FVector(0.0, 0.0, 20000.0), FVector(500.0, 0.0, 20000.0), 20.0);
		// A seabed 30 cm under the lowest point the free line ever reaches: near (within the reach), never touched.
		FLureLineColliders Near;
		Near.AddBox(FVector(3000.0, 3000.0, 50.0), FTransform::Identity, FTransform(FVector(0.0, 0.0, Lowest - 30.0 - 50.0)));
		Near.AddSphere(FVector(250.0, 0.0, Lowest - 30.0 - 400.0), 400.0);

		struct FCase { const TCHAR* Name; const FLureLineColliders* Solids; };
		for (const FCase& Case : { FCase{ TEXT("empty colliders"), &Empty }, FCase{ TEXT("solids 100 m away"), &Far }, FCase{ TEXT("a seabed 30 cm under the line, never touched"), &Near } })
		{
			TArray<TArray<FVector>> Frames;
			RunScript(Case.Solids, Frames);
			double Worst = 0.0;
			int32 FirstDiff = INDEX_NONE;
			for (int32 Frame = 0; Frame < FMath::Min(Frames.Num(), Reference.Num()); ++Frame)
			{
				const double Gap = MaxPointGap(Frames[Frame], Reference[Frame]);
				if (Gap > 0.0 && FirstDiff == INDEX_NONE)
				{
					FirstDiff = Frame;
				}
				Worst = FMath::Max(Worst, Gap);
			}
			TestEqual(FString::Printf(TEXT("%s: same number of frames"), Case.Name), Frames.Num(), Reference.Num());
			TestTrue(FString::Printf(TEXT("%s: every point of every frame equals the free sim exactly (worst %.6f cm, first difference at frame %d; lowest free point z %.1f)"),
				Case.Name, Worst, FirstDiff, Lowest), Worst == 0.0);
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

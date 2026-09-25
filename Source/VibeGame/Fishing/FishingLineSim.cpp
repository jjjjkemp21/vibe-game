// Lure: the fishing line's rope simulation (T-032).

#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"

namespace LureLineSimPrivate
{
	/** Gravity, cm/s2 (x GravityScale). */
	constexpr double Gravity = 980.0;

	/** Points this close above the float height still count as on the water (they get the water drag). */
	constexpr double WetBand = 0.5;

	/** Sub-step used for velocity impulses before the first sub-step ran. */
	constexpr double DefaultSubstep = 1.0 / 120.0;

	/**
	 *  A pinned line at most this share longer than the straight distance is exactly as long (fully taut). A rest length a
	 *  float step above the chord would otherwise show as ~0.3 cm of sag on a 15 m line (T032-B1).
	 */
	constexpr double TautShare = 1.0e-6;

	/**
	 *  Line under the water rises to the surface at most this fast, cm/s. A stronger lift would fight the no-stretch
	 *  constraints each sub-step: a slack line to a deep end (a diving fish) would stretch its last segments (T032-O2).
	 */
	constexpr double MaxFloatRiseSpeed = 240.0;

	/** A pinned line's length: at least Chord, and exactly Chord when within TautShare of it. */
	double PinnedLength(double RestLength, double Chord)
	{
		return RestLength <= Chord * (1.0 + TautShare) ? Chord : RestLength;
	}

	FVector ClampCoordinates(const FVector& Point)
	{
		const double Max = FLureLineSim::MaxCoordinate;
		return FVector(FMath::Clamp(Point.X, -Max, Max), FMath::Clamp(Point.Y, -Max, Max), FMath::Clamp(Point.Z, -Max, Max));
	}

	double Finite(double Value, double Fallback)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	/** Point moved into the ball (Center, Radius): unchanged if inside, else onto its surface. */
	FVector IntoBall(const FVector& Point, const FVector& Center, double Radius)
	{
		const FVector Delta = Point - Center;
		const double DistanceSquared = Delta.SizeSquared();
		if (DistanceSquared <= Radius * Radius)
		{
			return Point;
		}
		return Center + Delta * (Radius / FMath::Sqrt(DistanceSquared));
	}

	/**
	 *  The closest point to Point inside both balls (A, RadiusA) and (B, RadiusB) (their lens), exactly, in one step.
	 *  Balls that only touch (a line exactly as long as the straight distance) or miss give the point on the axis in proportion,
	 *  so a taut line lies exactly straight. (Projecting onto one ball, then the other, converges far too slowly there and
	 *  leaves a gravity sag of many cm on a taut line.)
	 */
	FVector IntoLens(const FVector& Point, const FVector& A, double RadiusA, const FVector& B, double RadiusB, const FVector& Axis, double Distance)
	{
		const bool bInA = FVector::DistSquared(Point, A) <= RadiusA * RadiusA;
		const bool bInB = FVector::DistSquared(Point, B) <= RadiusB * RadiusB;
		if (bInA && bInB)
		{
			return Point;
		}
		if (Distance <= UE_SMALL_NUMBER)
		{
			return IntoBall(Point, A, FMath::Min(RadiusA, RadiusB)); // both ends in one place
		}
		if (RadiusA + RadiusB <= Distance)
		{
			const double Sum = RadiusA + RadiusB;
			return Sum > 0.0 ? A + Axis * (RadiusA / Sum) : A;
		}
		if (RadiusA >= Distance + RadiusB)
		{
			return IntoBall(Point, B, RadiusB); // ball B lies inside ball A
		}
		if (RadiusB >= Distance + RadiusA)
		{
			return IntoBall(Point, A, RadiusA);
		}
		// Onto one ball's surface if that lands inside the other ball...
		if (!bInA)
		{
			const FVector OnA = IntoBall(Point, A, RadiusA);
			if (FVector::DistSquared(OnA, B) <= RadiusB * RadiusB)
			{
				return OnA;
			}
		}
		if (!bInB)
		{
			const FVector OnB = IntoBall(Point, B, RadiusB);
			if (FVector::DistSquared(OnB, A) <= RadiusA * RadiusA)
			{
				return OnB;
			}
		}
		// ... else onto the rim circle where the two spheres meet.
		const FVector Unit = Axis / Distance;
		const double Along = (Distance * Distance + RadiusA * RadiusA - RadiusB * RadiusB) / (2.0 * Distance);
		const double RimRadius = FMath::Sqrt(FMath::Max(0.0, RadiusA * RadiusA - Along * Along));
		FVector Out = (Point - A) - Unit * FVector::DotProduct(Point - A, Unit);
		double OutLength = Out.Size();
		if (OutLength <= UE_SMALL_NUMBER)
		{
			// On the axis: any side will do; prefer below (where gravity would take it).
			Out = -FVector::UpVector - Unit * FVector::DotProduct(-FVector::UpVector, Unit);
			OutLength = Out.Size();
			if (OutLength <= UE_SMALL_NUMBER)
			{
				Out = FVector::ForwardVector - Unit * FVector::DotProduct(FVector::ForwardVector, Unit);
				OutLength = FMath::Max(Out.Size(), UE_SMALL_NUMBER);
			}
		}
		return A + Unit * Along + Out * (RimRadius / OutLength);
	}

	// ---- Collision (T-032b; docs/specs/fishing-line.md "Collision") ----
	// Every solid is grown by the line's radius; "inside" below means inside that grown solid.

	/** A segment at most this deep in a solid rests on it rather than cutting it, cm (and a lift clears it by this much). */
	constexpr double CutTolerance = 0.05;

	/** A point this close to a solid's surface touches it: it slides on its face and gets the contact friction, cm. */
	constexpr double TouchBand = 0.5;

	/**
	 *  A path that starts this little inside a face starts on it, cm. Points are pushed out exactly onto a face, so rounding
	 *  can leave one a hair inside; taken as inside, a point resting on a thin plank and dragged through it would not tunnel-check.
	 */
	constexpr double OnSurface = 1.0e-3;

	/** A solid placed with a scale below this on any axis is degenerate and skipped. */
	constexpr double MinSolidScale = 1.0e-4;

	/** Faces this close to opposite (a thin slab's top and bottom) have no edge between them to lift a segment over. */
	constexpr double OppositeFacesCos = -0.999;

	/** The near-solid search reaches this much farther than the line can move in a frame, cm (gravity, rounding). */
	constexpr double NearSlack = 50.0;

	/** A segment this much of its length from parallel to a face runs parallel to it. */
	constexpr double FlatShare = 1.0e-6;

	FVector NormalOf(const FPlane& Plane)
	{
		return FVector(Plane.X, Plane.Y, Plane.Z);
	}

	bool UsableTransform(const FTransform& Transform)
	{
		if (Transform.ContainsNaN())
		{
			return false;
		}
		const FVector Scale = Transform.GetScale3D().GetAbs();
		return Scale.X >= MinSolidScale && Scale.Y >= MinSolidScale && Scale.Z >= MinSolidScale;
	}

	/** Plane moved by Transform (x' = R (S x) + T): the normal by the inverse transpose, R (n / S) renormalized, through the moved point. */
	bool TransformPlane(const FPlane& Plane, const FTransform& Transform, FPlane& Out)
	{
		const FVector Normal = NormalOf(Plane);
		const double SizeSquared = Normal.SizeSquared();
		if (Normal.ContainsNaN() || !FMath::IsFinite(Plane.W) || SizeSquared <= UE_SMALL_NUMBER)
		{
			return false;
		}
		const FVector OnPlane = Normal * (Plane.W / SizeSquared);
		FVector Moved = Transform.GetRotation().RotateVector(Normal / Transform.GetScale3D());
		if (Moved.ContainsNaN() || !Moved.Normalize(UE_SMALL_NUMBER))
		{
			return false;
		}
		Out = FPlane(Moved, FVector::DotProduct(Moved, Transform.TransformPosition(OnPlane)));
		return FMath::IsFinite(Out.W);
	}

	/** A unit vector at right angles to Along, as close to up as possible (+X for a vertical Along). */
	FVector PerpendicularTo(const FVector& Along)
	{
		const FVector Unit = Along.GetSafeNormal();
		FVector Out = FVector::UpVector - Unit * Unit.Z;
		if (!Out.Normalize(1.0e-6))
		{
			Out = FVector::ForwardVector - Unit * Unit.X;
			if (!Out.Normalize(1.0e-6))
			{
				Out = FVector::UpVector;
			}
		}
		return Out;
	}

	/**
	 *  Keeps Point out of a convex solid, judged along its path this sub-step (From -> Point). True if it moved.
	 *  - Inside: out through the face it came in by. Through the nearest face instead when the path started inside, or when the
	 *    point was within TouchBand of that face both before and after (it only slides along it: resting on a face, or crossing
	 *    the seam onto a solid side by side, even one a few mm higher). A thin solid's far face is never nearest in that sense,
	 *    so a fast point that lands near it goes back out the way it came (no tunnelling).
	 *  - Outside, but the path went right through (a fast point, a thin plank): back onto the face it went in by, so a point
	 *    never tunnels. A path that only grazes an edge (less than CutTolerance deep) is left alone.
	 */
	bool PushPointOutOfConvex(FVector& Point, const FVector& From, const FPlane* Planes, int32 Num, double Radius)
	{
		double Enter = 0.0;
		double Leave = 1.0;
		int32 EntryFace = INDEX_NONE;
		int32 NearestFace = INDEX_NONE;
		double NearestDot = -TNumericLimits<double>::Max();
		double NearestFromDot = 0.0;
		for (int32 Face = 0; Face < Num; ++Face)
		{
			double FromDot = Planes[Face].PlaneDot(From) - Radius;
			if (FromDot < 0.0 && FromDot > -OnSurface)
			{
				FromDot = 0.0; // it started on this face (see OnSurface)
			}
			const double PointDot = Planes[Face].PlaneDot(Point) - Radius;
			if (FromDot >= 0.0 && PointDot >= 0.0)
			{
				return false; // the whole path is beyond this face
			}
			if (PointDot > NearestDot)
			{
				NearestDot = PointDot;
				NearestFromDot = FromDot;
				NearestFace = Face;
			}
			if (FromDot >= 0.0)
			{
				const double Crossing = FromDot / (FromDot - PointDot); // in through this face here
				if (EntryFace == INDEX_NONE || Crossing > Enter)
				{
					Enter = Crossing;
					EntryFace = Face;
				}
			}
			else if (PointDot >= 0.0)
			{
				Leave = FMath::Min(Leave, FromDot / (FromDot - PointDot)); // out through this one
			}
		}
		if (NearestFace == INDEX_NONE || Enter >= Leave)
		{
			return false; // the path passes by
		}
		int32 Face = NearestFace;
		if (NearestDot >= 0.0)
		{
			// Outside now: did the path pass through the solid (deeper than a graze)?
			if (EntryFace == INDEX_NONE)
			{
				return false; // it came from inside: leaving is fine
			}
			const FVector Middle = FMath::Lerp(From, Point, 0.5 * (Enter + Leave));
			double MiddleDot = -TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < Num; ++Index)
			{
				MiddleDot = FMath::Max(MiddleDot, Planes[Index].PlaneDot(Middle) - Radius);
			}
			if (MiddleDot > -CutTolerance)
			{
				return false;
			}
			Face = EntryFace;
		}
		else if (EntryFace != INDEX_NONE && (NearestDot < -TouchBand || NearestFromDot < -TouchBand))
		{
			Face = EntryFace; // it came in from outside this sub-step, not just sliding along its nearest face
		}
		const FPlane& Plane = Planes[Face];
		Point += NormalOf(Plane) * (Radius - Plane.PlaneDot(Point));
		return true;
	}

	/**
	 *  Keeps Point out of a rounded solid, judged along its path this sub-step (From -> Point), like PushPointOutOfConvex. True if
	 *  it moved.
	 *  - Inside: radially out (straight up from its core line if exactly on it) when the path started inside, or when the point
	 *    was within TouchBand of the surface before and after (it slides along it); else back to where its path came in.
	 *  - Outside, but the path went through it deeper than CutTolerance (a fast point past a thin rail): back to where it came in.
	 */
	bool PushPointOutOfRounded(FVector& Point, const FVector& From, const FLureLineColliders::FRounded& Solid, double Radius)
	{
		const double Reach = Solid.Radius + Radius;
		const FVector Core = FMath::ClosestPointOnSegment(Point, Solid.A, Solid.B);
		const double Distance = FVector::Dist(Point, Core);
		const double FromDistance = FVector::Dist(From, FMath::ClosestPointOnSegment(From, Solid.A, Solid.B));
		const bool bCameFromOutside = FromDistance > Reach - OnSurface;
		FVector Deep = Point; // a point of the path inside the solid
		if (Distance >= Reach)
		{
			if (!bCameFromOutside)
			{
				return false; // it came from inside: leaving is fine
			}
			FVector OnCore;
			FMath::SegmentDistToSegmentSafe(From, Point, Solid.A, Solid.B, Deep, OnCore);
			const double Graze = FMath::Max(0.0, Reach - CutTolerance);
			if (FVector::DistSquared(Deep, OnCore) >= Graze * Graze)
			{
				return false; // it passed by, or only grazed it
			}
		}
		else if (!bCameFromOutside || (Distance >= Reach - TouchBand && FromDistance <= Reach + TouchBand))
		{
			Point = Core + (Distance > UE_KINDA_SMALL_NUMBER ? (Point - Core) / Distance : FVector::UpVector) * Reach;
			return true;
		}
		// The distance to the core line along the path is convex: outside at From, inside at Deep, so one crossing between.
		double Outside = 0.0;
		double Inside = 1.0;
		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			const double Middle = 0.5 * (Outside + Inside);
			const FVector Probe = FMath::Lerp(From, Deep, Middle);
			(FVector::DistSquared(Probe, FMath::ClosestPointOnSegment(Probe, Solid.A, Solid.B)) < Reach * Reach ? Inside : Outside) = Middle;
		}
		Point = FMath::Lerp(From, Deep, Outside);
		return true;
	}

	/** An end lying on exactly one face of a convex solid keeps only the part of Move along that face (it slides, no hop off it). */
	void SlideOnFace(const FVector& Point, FVector& Move, const FPlane* Planes, int32 Num, double Radius)
	{
		int32 OnFace = INDEX_NONE;
		for (int32 Face = 0; Face < Num; ++Face)
		{
			const double Dot = Planes[Face].PlaneDot(Point) - Radius;
			if (Dot > TouchBand)
			{
				return; // not on this solid
			}
			if (Dot >= -TouchBand)
			{
				if (OnFace != INDEX_NONE)
				{
					return; // on an edge: free to go over it
				}
				OnFace = Face;
			}
		}
		if (OnFace != INDEX_NONE)
		{
			const FVector Normal = NormalOf(Planes[OnFace]);
			const double Off = FVector::DotProduct(Move, Normal);
			if (Off > 0.0)
			{
				Move -= Normal * Off;
			}
		}
	}

	/** Moves the ends of segment A-B so its point at T moves by Distance along Direction (PBD by the weights), each end at most MaxMove. */
	bool MoveSegmentAt(FVector& A, FVector& B, double WeightA, double WeightB, double T, const FVector& Direction, double Distance,
		double MaxMove, const FPlane* SlidePlanes, int32 NumSlidePlanes, double Radius)
	{
		const double Weights = FMath::Square(1.0 - T) * WeightA + FMath::Square(T) * WeightB;
		if (Weights <= UE_SMALL_NUMBER || !FMath::IsFinite(Distance) || Distance <= 0.0)
		{
			return false;
		}
		const double Lambda = Distance / Weights;
		FVector MoveA = Direction * ((1.0 - T) * WeightA * Lambda);
		FVector MoveB = Direction * (T * WeightB * Lambda);
		if (SlidePlanes)
		{
			SlideOnFace(A, MoveA, SlidePlanes, NumSlidePlanes, Radius);
			SlideOnFace(B, MoveB, SlidePlanes, NumSlidePlanes, Radius);
		}
		A += MoveA.GetClampedToMaxSize(MaxMove);
		B += MoveB.GetClampedToMaxSize(MaxMove);
		return true;
	}

	/**
	 *  Segment A-B that dips into a convex solid between two ends outside it is lifted out, exactly. Along the segment
	 *  (t in [0, 1]) face k's distance is f_k(t) = a_k + s_k t; the segment is inside where all f_k < 0, and its deepest point
	 *  is at the bottom of F(t) = max_k f_k(t), a convex broken line. It is found by walking F from where the segment goes in:
	 *  the leading face falls until a steeper face takes over. The bottom is then
	 *   - where a falling face i meets a rising face j: an edge the segment cuts. A rigid move d raises the bottom at the rate
	 *     W.d / (s_j - s_i), W = s_j n_i - s_i n_j, which is at right angles to both the segment and the edge: the least move
	 *     that clears the cut is along W;
	 *   - a face the segment runs parallel to (it lies under that face): straight out of that face, at the middle of that stretch.
	 *  The move is shared by the ends by where the bottom is and the weights (PBD); an end resting on a face slides along it
	 *  instead of hopping off it (MoveSegmentAt). True if it moved.
	 *  A segment with an end inside is left alone: the point pass pushes that end out first, and the next pass lifts what is
	 *  left. (Lifting it here would move the other end by 1 / t of the depth, a whole segment when the inside end is a pinned
	 *  rod tip a few mm inside a solid.)
	 *  Known limit: a segment straight through a thin slab (both ends outside, beyond opposite faces) has no edge to lift over.
	 *  Points never tunnel (PushPointOutOfConvex), so only a reset or a teleport leaves a segment like that.
	 */
	bool LiftSegmentOutOfConvex(FVector& A, FVector& B, double WeightA, double WeightB, const FPlane* Planes, int32 Num, double Radius, double MaxMove)
	{
		const FVector Along = B - A;
		const double Length = Along.Size();
		if (Num <= 0 || Length <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const auto Offset = [&](int32 Face) { return Planes[Face].PlaneDot(A) - Radius; };
		const auto Slope = [&](int32 Face) { return FVector::DotProduct(NormalOf(Planes[Face]), Along); };
		const auto OnOrOut = [](double Dot) { return Dot > -OnSurface; }; // an end a hair inside a face lies on it (OnSurface)

		// Clip by the faces (most segments near a solid miss it): inside for t in (Enter, Leave), going in through Leading.
		double Enter = 0.0;
		double Leave = 1.0;
		int32 Leading = INDEX_NONE;
		bool bLeaves = false;
		for (int32 Face = 0; Face < Num; ++Face)
		{
			const double At = Offset(Face);
			const double Rise = Slope(Face);
			const bool bAOut = OnOrOut(At);
			const bool bBOut = OnOrOut(At + Rise);
			if (bAOut && bBOut)
			{
				return false; // both ends beyond this face
			}
			if (bAOut)
			{
				const double Crossing = FMath::Max(0.0, At) / -Rise;
				if (Leading == INDEX_NONE || Crossing > Enter)
				{
					Enter = Crossing;
					Leading = Face;
				}
			}
			else if (bBOut)
			{
				Leave = FMath::Min(Leave, At / FMath::Min(-Rise, -UE_SMALL_NUMBER));
				bLeaves = true;
			}
		}
		if (Leading == INDEX_NONE || !bLeaves || Enter >= Leave)
		{
			return false; // an end inside (the point pass first), or it passes by
		}

		// Walk down F: from T, the next face to overtake the leading one is the one that catches up first (steepest on a tie).
		const double FlatSlope = FlatShare * Length;
		const auto NextFace = [&](int32 Current, double From, double& OutAt) -> int32
		{
			const double CurrentRise = Slope(Current);
			const double CurrentAt = Offset(Current);
			int32 Next = INDEX_NONE;
			double NextRise = CurrentRise;
			OutAt = 1.0;
			for (int32 Face = 0; Face < Num; ++Face)
			{
				const double Rise = Slope(Face);
				if (Face == Current || Rise <= CurrentRise)
				{
					continue;
				}
				const double Catch = FMath::Max(From, (CurrentAt - Offset(Face)) / (Rise - CurrentRise));
				if (Catch < OutAt || (Next != INDEX_NONE && Catch == OutAt && Rise > NextRise))
				{
					Next = Face;
					OutAt = Catch;
					NextRise = Rise;
				}
			}
			return Next;
		};
		double T = Enter;
		int32 Falling = INDEX_NONE;
		for (int32 Guard = 0; Guard < Num && Slope(Leading) < -FlatSlope; ++Guard)
		{
			double Catch = 1.0;
			const int32 Next = NextFace(Leading, T, Catch);
			if (Next == INDEX_NONE)
			{
				return false; // (only by rounding: B is outside, so F rises again before t = 1)
			}
			Falling = Leading;
			Leading = Next;
			T = Catch;
		}
		if (Slope(Leading) < -FlatSlope)
		{
			return false; // (never: every step takes a steeper face)
		}
		const double Bottom = Offset(Leading) + Slope(Leading) * T;
		if (Bottom >= -CutTolerance)
		{
			return false; // resting on the solid, not cutting it
		}
		const FVector LeadingNormal = NormalOf(Planes[Leading]);
		const double LeadingRise = Slope(Leading);
		if (LeadingRise > FlatSlope)
		{
			if (Falling == INDEX_NONE)
			{
				return false; // (only by rounding: A is outside, so F falls first)
			}
			const FVector FallingNormal = NormalOf(Planes[Falling]);
			if (FVector::DotProduct(FallingNormal, LeadingNormal) < OppositeFacesCos)
			{
				return false; // straight through a slab: no edge (the known limit)
			}
			const double FallingRise = Slope(Falling);
			const FVector Across = FallingNormal * LeadingRise - LeadingNormal * FallingRise;
			const double AcrossSize = Across.Size();
			if (AcrossSize <= UE_SMALL_NUMBER)
			{
				return false;
			}
			const double Distance = (CutTolerance - Bottom) * (LeadingRise - FallingRise) / AcrossSize;
			return MoveSegmentAt(A, B, WeightA, WeightB, FMath::Clamp(T, 0.0, 1.0), Across / AcrossSize, Distance, MaxMove, Planes, Num, Radius);
		}
		// Parallel to the leading face: straight out of it, at the middle of the stretch it lies under.
		double FlatEnd = 1.0;
		NextFace(Leading, T, FlatEnd);
		const double Middle = FMath::Clamp(0.5 * (T + FlatEnd), 0.0, 1.0);
		return MoveSegmentAt(A, B, WeightA, WeightB, Middle, LeadingNormal, CutTolerance - Bottom, MaxMove, Planes, Num, Radius);
	}

	/**
	 *  Segment A-B that dips into a rounded solid between two ends outside it is pushed out where it comes closest to the core
	 *  line (PBD). An end inside, or an end coming closest: the point pass (like LiftSegmentOutOfConvex).
	 */
	bool LiftSegmentOutOfRounded(FVector& A, FVector& B, double WeightA, double WeightB, const FLureLineColliders::FRounded& Solid, double Radius, double MaxMove)
	{
		FVector OnSegment;
		FVector OnCore;
		FMath::SegmentDistToSegmentSafe(A, B, Solid.A, Solid.B, OnSegment, OnCore);
		const double Reach = Solid.Radius + Radius;
		const FVector Out = OnSegment - OnCore;
		const double DistanceSquared = Out.SizeSquared();
		const double Cut = FMath::Max(0.0, Reach - CutTolerance);
		if (DistanceSquared >= Cut * Cut)
		{
			return false;
		}
		const double Inside = FMath::Square(FMath::Max(0.0, Reach - OnSurface));
		if (FVector::DistSquared(A, FMath::ClosestPointOnSegment(A, Solid.A, Solid.B)) < Inside
			|| FVector::DistSquared(B, FMath::ClosestPointOnSegment(B, Solid.A, Solid.B)) < Inside)
		{
			return false; // an end inside: the point pass first
		}
		const FVector Along = B - A;
		const double LengthSquared = Along.SizeSquared();
		if (LengthSquared <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const double T = FVector::DotProduct(OnSegment - A, Along) / LengthSquared;
		if (T <= 1.0e-6 || T >= 1.0 - 1.0e-6)
		{
			return false; // an end comes closest: the point pass
		}
		const double Distance = FMath::Sqrt(DistanceSquared);
		FVector Normal = Distance > UE_KINDA_SMALL_NUMBER ? Out / Distance : FVector::CrossProduct(Along, Solid.B - Solid.A);
		if (Distance <= UE_KINDA_SMALL_NUMBER && !Normal.Normalize(1.0e-6))
		{
			Normal = PerpendicularTo(Along);
		}
		return MoveSegmentAt(A, B, WeightA, WeightB, T, Normal, Reach + CutTolerance - Distance, MaxMove, nullptr, 0, Radius);
	}

	/** The normal of the face of a convex solid Point touches (within TouchBand), else false. */
	bool TouchNormal(const FVector& Point, const FPlane* Planes, int32 Num, double Radius, FVector& OutNormal)
	{
		int32 Nearest = INDEX_NONE;
		double NearestDot = -TNumericLimits<double>::Max();
		for (int32 Face = 0; Face < Num; ++Face)
		{
			const double Dot = Planes[Face].PlaneDot(Point) - Radius;
			if (Dot > TouchBand)
			{
				return false;
			}
			if (Dot > NearestDot)
			{
				NearestDot = Dot;
				Nearest = Face;
			}
		}
		if (Nearest == INDEX_NONE)
		{
			return false;
		}
		OutNormal = NormalOf(Planes[Nearest]);
		return true;
	}

	/** The outward normal where Point touches a rounded solid (within TouchBand), else false. */
	bool TouchNormal(const FVector& Point, const FLureLineColliders::FRounded& Solid, double Radius, FVector& OutNormal)
	{
		const FVector Out = Point - FMath::ClosestPointOnSegment(Point, Solid.A, Solid.B);
		const double Reach = Solid.Radius + Radius + TouchBand;
		const double DistanceSquared = Out.SizeSquared();
		if (DistanceSquared > Reach * Reach)
		{
			return false;
		}
		const double Distance = FMath::Sqrt(DistanceSquared);
		OutNormal = Distance > UE_KINDA_SMALL_NUMBER ? Out / Distance : FVector::UpVector;
		return true;
	}

	/**
	 *  A collision just moved a free point by Move (Prev: its Verlet previous position). It keeps its speed along the move only
	 *  if it was already going that way, and loses any speed against it: a push out of a solid never adds speed (no pop out of
	 *  a solid a line was laid into, no hop off an edge it was lifted over), and a point going in stops (no bounce).
	 */
	void AbsorbMove(const FVector& Point, FVector& Prev, const FVector& Move)
	{
		const double Size = Move.Size();
		if (Size <= UE_SMALL_NUMBER)
		{
			return;
		}
		const FVector Unit = Move / Size;
		const double Along = FVector::DotProduct(Point - Move - Prev, Unit); // its speed along the move before it, cm per sub-step
		Prev += Unit * (Size + FMath::Min(Along, 0.0));
	}

	/** Solid can be used: at least 4 planes, all within Solids.Planes, and valid bounds. */
	bool IsUsable(const FLureLineColliders& Solids, const FLureLineColliders::FConvex& Solid)
	{
		return Solid.Num >= 4 && Solid.First >= 0 && Solid.First + Solid.Num <= Solids.Planes.Num() && Solid.Bounds.IsValid;
	}
}

// ---- FLureLineColliders ----

void FLureLineColliders::Reserve(int32 NumPlanes, int32 NumConvexes, int32 NumRounded)
{
	Planes.Reserve(NumPlanes);
	Convexes.Reserve(NumConvexes);
	Rounded.Reserve(NumRounded);
}

void FLureLineColliders::Reset()
{
	Planes.Reset();
	Convexes.Reset();
	Rounded.Reset();
}

bool FLureLineColliders::AddBox(const FVector& HalfExtent, const FTransform& Local, const FTransform& World)
{
	if (HalfExtent.ContainsNaN() || HalfExtent.X <= 0.0 || HalfExtent.Y <= 0.0 || HalfExtent.Z <= 0.0)
	{
		return false;
	}
	const FPlane Faces[6] = {
		FPlane(FVector::ForwardVector, HalfExtent.X), FPlane(-FVector::ForwardVector, HalfExtent.X),
		FPlane(FVector::RightVector, HalfExtent.Y), FPlane(-FVector::RightVector, HalfExtent.Y),
		FPlane(FVector::UpVector, HalfExtent.Z), FPlane(-FVector::UpVector, HalfExtent.Z) };
	return AddConvex(MakeArrayView(Faces, 6), FBox(-HalfExtent, HalfExtent), Local, World);
}

bool FLureLineColliders::AddConvex(TArrayView<const FPlane> LocalPlanes, const FBox& LocalBounds, const FTransform& Local, const FTransform& World)
{
	using namespace LureLineSimPrivate; // function scope only (unity builds, rule 7)
	if (LocalPlanes.Num() < 4 || !LocalBounds.IsValid || LocalBounds.Min.ContainsNaN() || LocalBounds.Max.ContainsNaN()
		|| !UsableTransform(Local) || !UsableTransform(World))
	{
		return false;
	}
	const int32 First = Planes.Num();
	for (const FPlane& Plane : LocalPlanes)
	{
		FPlane InOwner;
		FPlane InWorld;
		if (!TransformPlane(Plane, Local, InOwner) || !TransformPlane(InOwner, World, InWorld))
		{
			Planes.SetNum(First, EAllowShrinking::No); // all or nothing
			return false;
		}
		Planes.Add(InWorld);
	}
	FConvex& Solid = Convexes.AddDefaulted_GetRef();
	Solid.First = First;
	Solid.Num = LocalPlanes.Num();
	Solid.Bounds = LocalBounds.TransformBy(Local).TransformBy(World);
	return true;
}

bool FLureLineColliders::AddBoundsBox(const FBox& WorldBox)
{
	if (!WorldBox.IsValid || WorldBox.Min.ContainsNaN() || WorldBox.Max.ContainsNaN())
	{
		return false;
	}
	// A flat mesh (a floor quad) still gets some thickness.
	const FVector HalfExtent = WorldBox.GetExtent().ComponentMax(FVector(0.5));
	if (!AddBox(HalfExtent, FTransform(WorldBox.GetCenter()), FTransform::Identity))
	{
		return false;
	}
	Convexes.Last().bBoundsOnly = true;
	return true;
}

bool FLureLineColliders::AddSphere(const FVector& Center, double Radius)
{
	return AddCapsule(Center, Center, Radius);
}

bool FLureLineColliders::AddCapsule(const FVector& A, const FVector& B, double Radius)
{
	if (A.ContainsNaN() || B.ContainsNaN() || !FMath::IsFinite(Radius) || Radius <= 0.0)
	{
		return false;
	}
	FRounded& Solid = Rounded.AddDefaulted_GetRef();
	Solid.A = A;
	Solid.B = B;
	Solid.Radius = Radius;
	Solid.Bounds = FBox(A.ComponentMin(B), A.ComponentMax(B)).ExpandBy(Radius);
	return true;
}

double FLureLineColliders::Penetration(const FVector& Point, double Grow) const
{
	double Deepest = 0.0;
	for (const FConvex& Solid : Convexes)
	{
		if (!LureLineSimPrivate::IsUsable(*this, Solid))
		{
			continue;
		}
		double Nearest = -TNumericLimits<double>::Max();
		for (int32 Index = Solid.First; Index < Solid.First + Solid.Num; ++Index)
		{
			Nearest = FMath::Max(Nearest, Planes[Index].PlaneDot(Point) - Grow);
		}
		Deepest = FMath::Max(Deepest, -Nearest);
	}
	for (const FRounded& Solid : Rounded)
	{
		Deepest = FMath::Max(Deepest, Solid.Radius + Grow - FVector::Dist(Point, FMath::ClosestPointOnSegment(Point, Solid.A, Solid.B)));
	}
	return Deepest;
}

// ---- FLureLineSim ----

void FLureLineSim::Init(int32 Segments)
{
	const int32 Count = FMath::Clamp(Segments, 1, MaxSegments) + 1;
	Positions.SetNumZeroed(Count, EAllowShrinking::Yes);
	Previous.SetNumZeroed(Count, EAllowShrinking::Yes);
	SubstepStart.SetNumZeroed(Count, EAllowShrinking::Yes);
	LastStart = LastEnd = FVector::ZeroVector;
	SegmentLength = 0.0;
	LastSubstep = 0.0;
	LastSubsteps = 0;
	bHasState = false;
	bFreeEnd = false;
	EndInvMass = 0.0;
}

double FLureLineSim::InvMassOf(int32 Index) const
{
	if (Index <= 0)
	{
		return 0.0;
	}
	if (Index >= GetNumSegments())
	{
		return bFreeEnd ? EndInvMass : 0.0;
	}
	return 1.0;
}

FLureLineSimInput FLureLineSim::Sanitize(const FLureLineSimInput& In) const
{
	using namespace LureLineSimPrivate;
	FLureLineSimInput Out = In;
	const FVector StartFallback = bHasState ? LastStart : FVector::ZeroVector;
	Out.Start = In.Start.ContainsNaN() ? StartFallback : ClampCoordinates(In.Start);
	Out.End = In.End.ContainsNaN() ? (bHasState ? LastEnd : Out.Start) : ClampCoordinates(In.End);
	Out.RestLength = static_cast<float>(FMath::Clamp(Finite(In.RestLength, GetRestLength()), 0.0, MaxLength));
	Out.EndMass = (FMath::IsFinite(In.EndMass) && In.EndMass > 0.f) ? FMath::Clamp(In.EndMass, 0.001f, 1.0e6f) : 1.f;
	Out.EndDrag = FMath::IsFinite(In.EndDrag) ? FMath::Min(In.EndDrag, 1000.f) : -1.f;
	Out.MaxSwingDeg = FMath::IsFinite(In.MaxSwingDeg) ? FMath::Clamp(In.MaxSwingDeg, 0.f, 90.f) : 0.f;
	Out.bHasWater = In.bHasWater && FMath::IsFinite(In.WaterZ) && FMath::Abs(In.WaterZ) < MaxCoordinate;
	Out.WaterZ = Out.bHasWater ? In.WaterZ : 0.f;
	Out.Float = FMath::IsFinite(In.Float) ? FMath::Clamp(In.Float, 0.f, 1.f) : 0.f;
	return Out;
}

void FLureLineSim::Reset(const FLureLineSimInput& InRaw)
{
	if (!IsInitialized())
	{
		return;
	}
	const FLureLineSimInput In = Sanitize(InRaw);
	const int32 N = GetNumSegments();
	bHasState = true;
	bFreeEnd = In.bFreeEnd;
	EndInvMass = bFreeEnd ? 1.0 / In.EndMass : 0.0;
	LastSubstep = 0.0;
	LastSubsteps = 0;

	FVector Direction = In.End - In.Start;
	const double Distance = Direction.Size();
	if (bFreeEnd)
	{
		// Laid straight from the tip toward the end (down if there is no direction), no longer than the line.
		Direction = Distance > UE_KINDA_SMALL_NUMBER ? Direction / Distance : -FVector::UpVector;
		const double Length = Distance > UE_KINDA_SMALL_NUMBER ? FMath::Min(Distance, static_cast<double>(In.RestLength)) : In.RestLength;
		SegmentLength = In.RestLength / N;
		for (int32 Index = 0; Index <= N; ++Index)
		{
			Positions[Index] = In.Start + Direction * (Length * Index / N);
		}
	}
	else
	{
		SegmentLength = LureLineSimPrivate::PinnedLength(In.RestLength, Distance) / N;
		for (int32 Index = 0; Index <= N; ++Index)
		{
			Positions[Index] = FMath::Lerp(In.Start, In.End, static_cast<double>(Index) / N);
		}
	}
	for (int32 Index = 0; Index <= N; ++Index)
	{
		Previous[Index] = Positions[Index];
	}
	LastStart = Positions[0];
	LastEnd = Positions[N];
}

void FLureLineSim::Step(float DeltaTime, const FLureLineSimInput& InRaw, const FLureFishingLineRow& Tuning)
{
	using namespace LureLineSimPrivate;
	if (!IsInitialized())
	{
		return;
	}
	const FLureLineSimInput In = Sanitize(InRaw);
	if (!bHasState)
	{
		Reset(In);
	}
	const int32 N = GetNumSegments();

	// A teleport (or a long hitch) resets the line instead of whipping it across the world.
	const double Teleport = FMath::Max(1.0, Finite(Tuning.TeleportDistance, 1500.0));
	const bool bTipJumped = FVector::DistSquared(In.Start, LastStart) > Teleport * Teleport;
	const bool bEndJumped = !In.bFreeEnd && FVector::DistSquared(In.End, LastEnd) > Teleport * Teleport;
	if (bTipJumped || bEndJumped)
	{
		Reset(In);
		return;
	}

	bFreeEnd = In.bFreeEnd;
	EndInvMass = bFreeEnd ? 1.0 / In.EndMass : 0.0;
	// The line's length goes from the last Step's to this one's linearly over the sub-steps; a pinned line is clamped per
	// sub-step to the straight distance between its ends there (T-032b). One whole-frame clamp to the longer of the two
	// chords left a frame's worth of extra line (sag) every frame the ends closed in, so a reeled line never pulled straight.
	const double FromRest = GetRestLength();
	const double ToRest = FMath::Min(static_cast<double>(In.RestLength), MaxLength);

	const double Dt = (FMath::IsFinite(DeltaTime) && DeltaTime > 0.f) ? static_cast<double>(DeltaTime) : 0.0;
	if (Dt <= 0.0)
	{
		// No time passes: only the pinned ends move (a paused or first frame).
		Previous[0] = Positions[0] = In.Start;
		if (!bFreeEnd)
		{
			Previous[N] = Positions[N] = In.End;
		}
		SegmentLength = (bFreeEnd ? ToRest : PinnedLength(ToRest, FVector::Dist(In.Start, In.End))) / N;
		LastStart = Positions[0];
		LastEnd = Positions[N];
		LastSubsteps = 0;
		return;
	}

	const double Rate = FMath::Clamp(Finite(Tuning.SubstepRate, 120.0), 30.0, 1000.0);
	const int32 MaxSubsteps = FMath::Clamp(Tuning.MaxSubsteps, 1, 32);
	const int32 Iterations = FMath::Clamp(Tuning.Iterations, 1, 32);
	const double FrameTime = FMath::Min(Dt, MaxSubsteps / Rate); // a longer frame runs in slow motion
	const int32 Substeps = FMath::Clamp(FMath::CeilToInt32(FrameTime * Rate - 1.0e-6), 1, MaxSubsteps);
	const double H = FrameTime / Substeps;

	const double GravityAccel = Gravity * FMath::Clamp(Finite(Tuning.GravityScale, 1.0), 0.0, 10.0);
	const FVector GravityStep(0.0, 0.0, -GravityAccel * H * H);
	const double AirKeep = FMath::Exp(-FMath::Max(0.0, Finite(Tuning.AirDrag, 0.0)) * H);
	const double WaterKeep = FMath::Exp(-FMath::Max(0.0, Finite(Tuning.WaterDrag, 0.0)) * H);
	const double EndKeep = In.EndDrag >= 0.f ? FMath::Exp(-static_cast<double>(In.EndDrag) * H) : AirKeep;
	const double SurfaceZ = In.WaterZ + FMath::Max(0.0, Finite(Tuning.FloatHeight, 0.0));
	const double WetZ = SurfaceZ + WetBand;
	const double Float = In.bHasWater ? static_cast<double>(In.Float) : 0.0;
	const bool bFloatFreeEnd = EndInvMass >= 1.0; // a snapped end floats; a hanging fish (heavier) does not

	// Collision (T-032b): the solids within reach of the line this frame, found once (fixed size: no allocation).
	FNearSolids Near;
	const FNearSolids* NearSolids = nullptr;
	double ContactKeep = 1.0;
	if (In.Colliders && !In.Colliders->IsEmpty())
	{
		// How far the line can get this frame: its ends' moves, its change in length and its points' speed (+ NearSlack).
		double Motion = FMath::Max(FVector::Dist(In.Start, LastStart), bFreeEnd ? 0.0 : FVector::Dist(In.End, LastEnd));
		Motion = FMath::Max(Motion, FMath::Abs(ToRest - FromRest));
		// Speeds are stored per last sub-step (per DefaultSubstep for impulses added before the first one ran).
		const double SubstepsPerFrame = FrameTime / (LastSubstep > 0.0 ? LastSubstep : DefaultSubstep);
		for (int32 Index = 1; Index <= N; ++Index)
		{
			Motion = FMath::Max(Motion, FVector::Dist(Positions[Index], Previous[Index]) * SubstepsPerFrame);
		}
		const double Radius = FMath::Clamp(Finite(Tuning.CollisionRadius, 1.0), 0.0, 50.0);
		FindNearSolids(In, Radius + TouchBand + NearSlack + Motion, Radius, Near);
		NearSolids = Near.IsEmpty() ? nullptr : &Near;
		ContactKeep = FMath::Exp(-FMath::Max(0.0, Finite(Tuning.GroundFriction, 0.0)) * H);
	}

	const FVector FromStart = LastStart;
	const FVector FromEnd = LastEnd;

	// Swing guard (T-032b): a free end rises per sub-step at most as fast as gravity lets it coast up to SwingCos x the line
	// under the tip, plus what the tip rises. Upward only: a cap on the whole speed would also kill the sideways lag of a
	// hanging fish behind a moving rod.
	const bool bSwingGuard = bFreeEnd && In.MaxSwingDeg > 0.f && GravityAccel > 0.0;
	const double SwingCos = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(In.MaxSwingDeg)));
	const double TipRise = FMath::Max(0.0, (In.Start.Z - FromStart.Z) / Substeps);

	for (int32 Sub = 1; Sub <= Substeps; ++Sub)
	{
		const double Alpha = static_cast<double>(Sub) / Substeps;
		const double Rest = FMath::Lerp(FromRest, ToRest, Alpha);
		const double VelocityScale = LastSubstep > 0.0 ? H / LastSubstep : 1.0; // time-corrected Verlet
		const int32 LastFree = bFreeEnd ? N : N - 1;
		for (int32 Index = 1; Index <= LastFree; ++Index)
		{
			const bool bWet = In.bHasWater && Positions[Index].Z < WetZ;
			const double Keep = bWet ? WaterKeep : (Index == N ? EndKeep : AirKeep);
			FVector Velocity = (Positions[Index] - Previous[Index]) * (VelocityScale * Keep);
			if (bSwingGuard && Index == N)
			{
				const double TopZ = FMath::Lerp(FromStart.Z, In.Start.Z, Alpha) - Rest * SwingCos;
				const double MaxRise = FMath::Sqrt(2.0 * GravityAccel * FMath::Max(0.0, TopZ - Positions[N].Z)) * H + TipRise;
				Velocity.Z = FMath::Min(Velocity.Z, MaxRise);
			}
			Previous[Index] = Positions[Index];
			Positions[Index] += Velocity + GravityStep;
		}
		Previous[0] = Positions[0];
		Positions[0] = FMath::Lerp(FromStart, In.Start, Alpha);
		if (!bFreeEnd)
		{
			Previous[N] = Positions[N];
			Positions[N] = FMath::Lerp(FromEnd, In.End, Alpha);
		}
		if (NearSolids)
		{
			// Where every point began this sub-step: collision judges each point's path from here (no tunnelling).
			for (int32 Index = 0; Index <= N; ++Index)
			{
				SubstepStart[Index] = Previous[Index];
			}
		}
		// The lerped rest is >= the lerped chord >= the chord of the lerped ends, so this clamp is only a safety net.
		SegmentLength = (bFreeEnd ? Rest : PinnedLength(Rest, FVector::Dist(Positions[0], Positions[N]))) / N;
		// Float before the constraints: the line never stretches to stay on the water. A slack line to a deep end (a diving
		// fish) is pulled under near the end instead of stretching its last segments (T032-O2).
		if (Float > 0.0)
		{
			ApplyFloat(SurfaceZ, Float, bFloatFreeEnd, MaxFloatRiseSpeed * H);
		}
		SolveConstraints(Iterations, NearSolids);
		if (NearSolids)
		{
			ApplyContactVelocity(*NearSolids, ContactKeep);
		}
		LastSubstep = H;
	}
	LastSubsteps = Substeps;
	LastStart = Positions[0];
	LastEnd = Positions[N];

	if (!IsFinite())
	{
		Reset(In); // never keep a broken state
	}
}

void FLureLineSim::SolveConstraints(int32 Iterations, const FNearSolids* Near)
{
	const int32 N = GetNumSegments();
	const double Segment = SegmentLength;
	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		// (a) One-sided distance constraints, sweeping alternately from each end (Gauss-Seidel).
		const bool bForward = (Iteration % 2) == 0;
		for (int32 Step = 0; Step < N; ++Step)
		{
			const int32 A = bForward ? Step : N - 1 - Step;
			const int32 B = A + 1;
			const double WeightA = InvMassOf(A);
			const double WeightB = InvMassOf(B);
			const double Weight = WeightA + WeightB;
			if (Weight <= 0.0)
			{
				continue;
			}
			const FVector Delta = Positions[B] - Positions[A];
			const double Length = Delta.Size();
			if (Length <= Segment || Length <= UE_SMALL_NUMBER)
			{
				continue; // slack: a line can't push
			}
			const FVector Correction = Delta * ((Length - Segment) / (Length * Weight));
			Positions[A] += Correction * WeightA;
			Positions[B] -= Correction * WeightB;
		}

		// (b) Tethers: no point farther from a pinned end than the line between them. A free end: the ball around the tip.
		//     Both ends pinned: exactly into the lens where both balls overlap (a taut line lies exactly straight).
		const FVector Tip = Positions[0];
		if (bFreeEnd)
		{
			for (int32 Index = 1; Index <= N; ++Index)
			{
				Positions[Index] = LureLineSimPrivate::IntoBall(Positions[Index], Tip, Index * Segment);
			}
		}
		else
		{
			const FVector End = Positions[N];
			const FVector Axis = End - Tip;
			const double Distance = Axis.Size();
			for (int32 Index = 1; Index < N; ++Index)
			{
				Positions[Index] = LureLineSimPrivate::IntoLens(Positions[Index], Tip, Index * Segment, End, (N - Index) * Segment, Axis, Distance);
			}
		}

		// (c) Collision last in every pass, so the length solve can't pull the line back into a solid (T-032b): points out
		//     first, so the segment pass only sees segments with both ends outside.
		if (Near)
		{
			CollidePoints(*Near);
			CollideSegments(*Near);
		}
	}
	if (Near)
	{
		CollidePoints(*Near); // the last word: a lift over one solid's edge may have pushed a point into another
	}
}

// ---- Collision (T-032b) ----

void FLureLineSim::FindNearSolids(const FLureLineSimInput& In, double Reach, double Radius, FNearSolids& Near) const
{
	using namespace LureLineSimPrivate;
	Near.Colliders = In.Colliders;
	Near.NumConvex = 0;
	Near.NumRounded = 0;
	Near.Radius = Radius;
	if (!In.Colliders)
	{
		return;
	}
	FBox LineBox(ForceInit);
	for (const FVector& Point : Positions)
	{
		LineBox += Point;
	}
	LineBox += In.Start;
	if (!In.bFreeEnd)
	{
		LineBox += In.End;
	}
	const FBox Search = LineBox.ExpandBy(Reach);
	const FLureLineColliders& Solids = *In.Colliders;
	for (int32 Index = 0; Index < Solids.Convexes.Num() && Near.NumConvex < MaxNearSolids; ++Index)
	{
		const FLureLineColliders::FConvex& Solid = Solids.Convexes[Index];
		if (!IsUsable(Solids, Solid) || !Solid.Bounds.Intersect(Search))
		{
			continue;
		}
		if (Solid.bBoundsOnly)
		{
			// Only a stand-in box: with the rod tip or a pinned end in it, it surely is not the real shape there (a boat, an arch).
			const FBox Held = Solid.Bounds.ExpandBy(Radius);
			if (Held.IsInsideOrOn(In.Start) || (!In.bFreeEnd && Held.IsInsideOrOn(In.End)))
			{
				continue;
			}
		}
		Near.Convex[Near.NumConvex++] = Index;
	}
	for (int32 Index = 0; Index < Solids.Rounded.Num() && Near.NumRounded < MaxNearSolids; ++Index)
	{
		const FLureLineColliders::FRounded& Solid = Solids.Rounded[Index];
		if (Solid.Radius > 0.0 && Solid.Bounds.IsValid && Solid.Bounds.Intersect(Search))
		{
			Near.Rounded[Near.NumRounded++] = Index;
		}
	}
}

void FLureLineSim::CollidePoints(const FNearSolids& Near)
{
	using namespace LureLineSimPrivate;
	const FLureLineColliders& Solids = *Near.Colliders;
	const FPlane* Planes = Solids.Planes.GetData();
	const double Radius = Near.Radius;
	const int32 N = GetNumSegments();
	const int32 LastFree = bFreeEnd ? N : N - 1;
	for (int32 Index = 1; Index <= LastFree; ++Index)
	{
		FVector& Point = Positions[Index];
		const FVector& From = SubstepStart[Index];
		const FVector Before = Point;
		for (int32 Which = 0; Which < Near.NumConvex; ++Which)
		{
			const FLureLineColliders::FConvex& Solid = Solids.Convexes[Near.Convex[Which]];
			if (Solid.Bounds.Intersect(FBox(Point.ComponentMin(From), Point.ComponentMax(From)).ExpandBy(Radius)))
			{
				PushPointOutOfConvex(Point, From, Planes + Solid.First, Solid.Num, Radius);
			}
		}
		for (int32 Which = 0; Which < Near.NumRounded; ++Which)
		{
			const FLureLineColliders::FRounded& Solid = Solids.Rounded[Near.Rounded[Which]];
			if (Solid.Bounds.Intersect(FBox(Point.ComponentMin(From), Point.ComponentMax(From)).ExpandBy(Radius)))
			{
				PushPointOutOfRounded(Point, From, Solid, Radius);
			}
		}
		AbsorbMove(Point, Previous[Index], Point - Before);
	}
}

void FLureLineSim::CollideSegments(const FNearSolids& Near)
{
	using namespace LureLineSimPrivate;
	const FLureLineColliders& Solids = *Near.Colliders;
	const FPlane* Planes = Solids.Planes.GetData();
	const double Radius = Near.Radius;
	const int32 N = GetNumSegments();
	const double MaxMove = FMath::Max(SegmentLength, 1.0);
	for (int32 Index = 0; Index < N; ++Index)
	{
		const double WeightA = InvMassOf(Index);
		const double WeightB = InvMassOf(Index + 1);
		if (WeightA + WeightB <= 0.0)
		{
			continue; // both ends pinned
		}
		FVector& A = Positions[Index];
		FVector& B = Positions[Index + 1];
		const FVector BeforeA = A;
		const FVector BeforeB = B;
		for (int32 Which = 0; Which < Near.NumConvex; ++Which)
		{
			const FLureLineColliders::FConvex& Solid = Solids.Convexes[Near.Convex[Which]];
			if (Solid.Bounds.Intersect(FBox(A.ComponentMin(B), A.ComponentMax(B)).ExpandBy(Radius)))
			{
				LiftSegmentOutOfConvex(A, B, WeightA, WeightB, Planes + Solid.First, Solid.Num, Radius, MaxMove);
			}
		}
		for (int32 Which = 0; Which < Near.NumRounded; ++Which)
		{
			const FLureLineColliders::FRounded& Solid = Solids.Rounded[Near.Rounded[Which]];
			if (Solid.Bounds.Intersect(FBox(A.ComponentMin(B), A.ComponentMax(B)).ExpandBy(Radius)))
			{
				LiftSegmentOutOfRounded(A, B, WeightA, WeightB, Solid, Radius, MaxMove);
			}
		}
		if (WeightA > 0.0)
		{
			AbsorbMove(A, Previous[Index], A - BeforeA);
		}
		if (WeightB > 0.0)
		{
			AbsorbMove(B, Previous[Index + 1], B - BeforeB);
		}
	}
}

void FLureLineSim::ApplyContactVelocity(const FNearSolids& Near, double Keep)
{
	using namespace LureLineSimPrivate;
	const FLureLineColliders& Solids = *Near.Colliders;
	const FPlane* Planes = Solids.Planes.GetData();
	const double Radius = Near.Radius;
	const int32 N = GetNumSegments();
	const int32 LastFree = bFreeEnd ? N : N - 1;
	for (int32 Index = 1; Index <= LastFree; ++Index)
	{
		const FVector& Point = Positions[Index];
		const FBox Box = FBox(Point, Point).ExpandBy(Radius + TouchBand);
		FVector Normal = FVector::ZeroVector;
		bool bTouching = false;
		for (int32 Which = 0; Which < Near.NumConvex && !bTouching; ++Which)
		{
			const FLureLineColliders::FConvex& Solid = Solids.Convexes[Near.Convex[Which]];
			bTouching = Solid.Bounds.Intersect(Box) && TouchNormal(Point, Planes + Solid.First, Solid.Num, Radius, Normal);
		}
		for (int32 Which = 0; Which < Near.NumRounded && !bTouching; ++Which)
		{
			const FLureLineColliders::FRounded& Solid = Solids.Rounded[Near.Rounded[Which]];
			bTouching = Solid.Bounds.Intersect(Box) && TouchNormal(Point, Solid, Radius, Normal);
		}
		if (!bTouching)
		{
			continue;
		}
		// No speed into the surface (no bounce), and sliding along it slows down (friction).
		FVector Velocity = Point - Previous[Index];
		const double Into = FVector::DotProduct(Velocity, Normal);
		Velocity = (Velocity - Normal * Into) * Keep + Normal * FMath::Max(Into, 0.0);
		Previous[Index] = Point - Velocity;
	}
}

void FLureLineSim::ApplyFloat(double SurfaceZ, double Float, bool bFloatFreeEnd, double MaxLift)
{
	const int32 N = GetNumSegments();
	const int32 Last = (bFreeEnd && bFloatFreeEnd) ? N : N - 1;
	for (int32 Index = 1; Index <= Last; ++Index)
	{
		FVector& Point = Positions[Index];
		if (Point.Z < SurfaceZ)
		{
			const double Lift = FMath::Min((SurfaceZ - Point.Z) * Float, MaxLift);
			Point.Z += Lift;
			// It rises without gaining speed from the lift, and loses its vertical speed in proportion (no bounce off the surface).
			Previous[Index].Z = FMath::Lerp(Previous[Index].Z + Lift, Point.Z, Float);
		}
	}
}

void FLureLineSim::ReleaseEnd(float EndMass)
{
	if (!IsInitialized() || !bHasState)
	{
		return;
	}
	bFreeEnd = true;
	EndInvMass = 1.0 / ((FMath::IsFinite(EndMass) && EndMass > 0.f) ? FMath::Clamp(static_cast<double>(EndMass), 0.001, 1.0e6) : 1.0);
}

void FLureLineSim::AddEndVelocity(const FVector& Velocity)
{
	if (!IsInitialized() || !bHasState || !bFreeEnd || Velocity.ContainsNaN())
	{
		return;
	}
	const double H = LastSubstep > 0.0 ? LastSubstep : LureLineSimPrivate::DefaultSubstep;
	Previous.Last() -= Velocity * H;
}

void FLureLineSim::AddRecoil(float Speed)
{
	if (!IsInitialized() || !bHasState || !FMath::IsFinite(Speed) || Speed == 0.f)
	{
		return;
	}
	const int32 N = GetNumSegments();
	const double Total = GetPolylineLength();
	if (Total <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	const double H = LastSubstep > 0.0 ? LastSubstep : LureLineSimPrivate::DefaultSubstep;
	double Along = 0.0;
	for (int32 Index = 1; Index <= N; ++Index)
	{
		const FVector Back = Positions[Index - 1] - Positions[Index];
		const double Length = Back.Size();
		Along += Length;
		if (InvMassOf(Index) <= 0.0 || Length <= UE_SMALL_NUMBER)
		{
			continue;
		}
		Previous[Index] -= Back * (static_cast<double>(Speed) * (Along / Total) / Length) * H;
	}
}

FVector FLureLineSim::GetStart() const
{
	return IsInitialized() ? Positions[0] : FVector::ZeroVector;
}

FVector FLureLineSim::GetEnd() const
{
	return IsInitialized() ? Positions.Last() : FVector::ZeroVector;
}

FVector FLureLineSim::GetEndDirection() const
{
	if (!IsInitialized())
	{
		return FVector::UpVector;
	}
	const int32 N = GetNumSegments();
	// The last segment that has a length (a slack end can bunch up).
	for (int32 Index = N; Index >= 1; --Index)
	{
		const FVector Up = Positions[Index - 1] - Positions[N];
		if (Up.SizeSquared() > UE_KINDA_SMALL_NUMBER)
		{
			return Up.GetUnsafeNormal();
		}
	}
	return FVector::UpVector;
}

FVector FLureLineSim::VelocityOf(int32 Index) const
{
	return (IsInitialized() && LastSubstep > 0.0) ? (Positions[Index] - Previous[Index]) / LastSubstep : FVector::ZeroVector;
}

FVector FLureLineSim::GetEndVelocity() const
{
	return IsInitialized() ? VelocityOf(GetNumSegments()) : FVector::ZeroVector;
}

double FLureLineSim::GetPolylineLength() const
{
	double Length = 0.0;
	for (int32 Index = 1; Index < Positions.Num(); ++Index)
	{
		Length += FVector::Dist(Positions[Index - 1], Positions[Index]);
	}
	return Length;
}

bool FLureLineSim::IsFinite() const
{
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		if (Positions[Index].ContainsNaN() || Previous[Index].ContainsNaN())
		{
			return false;
		}
	}
	return true;
}

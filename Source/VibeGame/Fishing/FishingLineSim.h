// Lure: the fishing line's rope simulation (T-032). Plain C++, no UObjects: tests drive it directly, ULureFishingLineComponent
// draws it. Model and tuning: docs/specs/fishing-line.md ("Simulation").

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

struct FLureFishingLineRow;

/**
 *  Solid shapes a line can't pass through (world space, cm), gathered by the line's owner (ULureFishingLineComponent: what
 *  blocks a cast around the line). Plain data, no UObjects: Reserve once, then Reset + Add whenever the line moves somewhere new;
 *  nothing allocates while the reserve lasts. Spec: docs/specs/fishing-line.md ("Collision").
 *  - Convex solids (boxes, convex hulls) as planes with unit outward normals: a point is inside when every PlaneDot < 0.
 *  - Rounded solids: every point within Radius of the segment A-B (a sphere when A = B, else a capsule).
 */
struct FLureLineColliders
{
	struct FConvex
	{
		/** Its planes are Planes[First, First + Num). */
		int32 First = 0;
		int32 Num = 0;
		FBox Bounds = FBox(ForceInit);
		/**
		 *  Only the bounding box of the real shape (a mesh without simple collision). It can be far bigger than the shape (a
		 *  boat, an arch), so the line ignores it while the rod tip or a pinned end is inside it.
		 */
		bool bBoundsOnly = false;
	};

	struct FRounded
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		double Radius = 0.0;
		FBox Bounds = FBox(ForceInit);
	};

	TArray<FPlane> Planes;
	TArray<FConvex> Convexes;
	TArray<FRounded> Rounded;

	void Reserve(int32 NumPlanes, int32 NumConvexes, int32 NumRounded);

	/** Forgets every solid and keeps the memory. */
	void Reset();

	bool IsEmpty() const { return Convexes.Num() == 0 && Rounded.Num() == 0; }
	int32 NumSolids() const { return Convexes.Num() + Rounded.Num(); }

	/** Box of half-size HalfExtent around its local origin, placed by Local (e.g. a body's box element) then World (any scale). */
	bool AddBox(const FVector& HalfExtent, const FTransform& Local, const FTransform& World);

	/**
	 *  Convex hull: LocalPlanes (unit outward normals, at least 4; FKConvexElem::GetPlanes) and LocalBounds (FKConvexElem::ElemBox),
	 *  placed like AddBox. Non-uniform and mirroring scales are exact. False (nothing added) for bad input or a scale below 1e-4.
	 */
	bool AddConvex(TArrayView<const FPlane> LocalPlanes, const FBox& LocalBounds, const FTransform& Local, const FTransform& World);

	/** World axis-aligned box that stands in for a shape without simple collision (see FConvex::bBoundsOnly). */
	bool AddBoundsBox(const FBox& WorldBox);

	bool AddSphere(const FVector& Center, double Radius);
	bool AddCapsule(const FVector& A, const FVector& B, double Radius);

	/** How deep Point is inside the deepest solid grown by Grow, cm (0 = outside all of them). For tests and debugging. */
	double Penetration(const FVector& Point, double Grow = 0.0) const;
};

/** What the line hangs from this frame (world space, cm). */
struct FLureLineSimInput
{
	/** The rod tip. The first point is pinned here: it moves there linearly over the frame's sub-steps, exactly there at the end. */
	FVector Start = FVector::ZeroVector;

	/** The line's end (bobber, fish): pinned like Start, unless bFreeEnd (then only Reset uses it, as the direction to lay the line). */
	FVector End = FVector::ZeroVector;

	/** The end is free: it hangs and swings (a landed fish) or flies (a snapped line). */
	bool bFreeEnd = false;

	/** Weight of a free end, in line points (a hanging fish: HangEndMass; a snapped end: 1). */
	float EndMass = 1.f;

	/** Drag of a free end, per second (a hanging fish: HangDrag). < 0 = the line's own AirDrag. */
	float EndDrag = -1.f;

	/**
	 *  Swing guard for a free end, degrees (0 = off; clamped to 0..90; a hanging fish: HangMaxSwingDeg). The end can't rise faster
	 *  than gravity would let it coast up to RestLength x cos(MaxSwingDeg) under the tip (+ what the tip itself rises), so a
	 *  jerked or reeled line never throws it over the tip. Upward only: sideways and downward motion are untouched.
	 */
	float MaxSwingDeg = 0.f;

	/**
	 *  Total line length, cm, reached over the frame's sub-steps (linearly from the last Step's length). A pinned line is never
	 *  shorter than the straight distance between its ends in any sub-step (it would have to stretch).
	 */
	float RestLength = 0.f;

	/** The water surface under the line (when bHasWater), cm, and how strongly line under it rises to it (0..1 per sub-step). */
	bool bHasWater = false;
	float WaterZ = 0.f;
	float Float = 0.f;

	/**
	 *  Solids the line lies on and bends around (null or empty = none), kept Tuning.CollisionRadius away. Read during Step only;
	 *  the owner keeps them alive and unchanged while Step runs.
	 */
	const FLureLineColliders* Colliders = nullptr;
};

/**
 *  A fishing line as Segments + 1 points (point 0 = the rod tip, the last = the end).
 *
 *  Each sub-step: Verlet integration (gravity; air drag, or water drag on and under the water), the pinned ends move along
 *  their frame path, the float rule (points under the water rise toward the surface (+ FloatHeight) without bouncing, at
 *  most 240 cm/s), then
 *  Iterations passes of
 *   (a) one-sided distance constraints: a segment may go slack but never stretch (a line is not a stick), and
 *   (b) tethers: no point farther from a pinned end than the line between them. With both ends pinned each point is moved
 *       exactly into the lens where the two balls overlap (one step, not ball after ball), so the line stays inextensible
 *       with few passes and a line exactly as long as the straight distance lies exactly straight (fully taut; a pinned
 *       line within 1e-6 of the straight distance counts as exactly that long).
 *  The constraints run last, so floating never stretches the line: a slack line to a deep end is pulled under near the end.
 *  With In.Colliders, every pass ends with (c) collision: points are pushed out through the face they came in by (also when
 *  they passed right through a thin solid within the sub-step), then segments that dip into a solid between two outside ends
 *  are lifted over its edge; a last point pass follows the last pass. A collision move never adds speed along itself (no pop,
 *  no bounce), and line resting on a surface slides with GroundFriction (once per sub-step). Collision has the last word, so
 *  a taut line wrapped over an edge stretches a little instead of cutting through it.
 *
 *  Init allocates; nothing else does: Reset, Step, the impulses and the queries never allocate.
 *  Robustness: non-finite inputs are replaced by the last good ones, coordinates and lengths are clamped, a frame longer
 *  than MaxSubsteps sub-steps runs in slow motion, and a tip or end that jumps farther than TeleportDistance resets the line.
 */
class FLureLineSim
{
public:

	/** Most segments (Init clamps to [1, MaxSegments]). */
	static constexpr int32 MaxSegments = 64;

	/** Longest line, cm (1 km); longer requests are clamped. */
	static constexpr double MaxLength = 100000.0;

	/** Coordinates are clamped to +-MaxCoordinate, cm (1000 km). */
	static constexpr double MaxCoordinate = 1.0e8;

	/** Allocates Segments + 1 points (Segments clamped to [1, MaxSegments]) and forgets any state. */
	void Init(int32 Segments);

	bool IsInitialized() const { return Positions.Num() >= 2; }
	int32 GetNumSegments() const { return FMath::Max(0, Positions.Num() - 1); }

	/** True once Reset or Step ran after Init. */
	bool HasState() const { return bHasState; }

	/**
	 *  Lays the line at rest. Pinned end: straight from Start to End. Free end: straight from Start toward End (straight down if
	 *  End = Start), min(RestLength, the distance) long.
	 */
	void Reset(const FLureLineSimInput& In);

	/** Advances DeltaTime seconds (<= 0: only the pinned ends move). The first Step after Init resets to the input first. */
	void Step(float DeltaTime, const FLureLineSimInput& In, const FLureFishingLineRow& Tuning);

	/** Frees the end where it is, keeping its current velocity (a snap). EndMass as in FLureLineSimInput. */
	void ReleaseEnd(float EndMass);

	/** Adds a velocity (cm/s) to a free end (a flop, a kick). A pinned end ignores it. */
	void AddEndVelocity(const FVector& Velocity);

	/** Snap recoil: every free point gets a velocity back along the line toward the tip: Speed at the end, 0 at the tip. */
	void AddRecoil(float Speed);

	const TArray<FVector>& GetPoints() const { return Positions; }
	FVector GetStart() const;
	FVector GetEnd() const;

	/** Unit vector from the end up the line (toward the point before it); +Z if the last segment has no length. */
	FVector GetEndDirection() const;

	/** The end's velocity, cm/s (0 before the first sub-step). */
	FVector GetEndVelocity() const;

	/** Length of the polyline through the points, cm. */
	double GetPolylineLength() const;

	/** Rest length of one segment now, cm (the line's length / segments). */
	double GetSegmentLength() const { return SegmentLength; }

	/** Rest length of the whole line now, cm. */
	double GetRestLength() const { return SegmentLength * GetNumSegments(); }

	bool IsEndFree() const { return bFreeEnd; }

	/** Sub-steps the last Step ran. */
	int32 GetLastSubsteps() const { return LastSubsteps; }

	/** Every point is finite. */
	bool IsFinite() const;

private:

	TArray<FVector> Positions;
	TArray<FVector> Previous;
	FVector LastStart = FVector::ZeroVector;
	FVector LastEnd = FVector::ZeroVector;
	double SegmentLength = 0.0;
	double LastSubstep = 0.0;
	int32 LastSubsteps = 0;
	bool bHasState = false;
	bool bFreeEnd = false;
	/** Inverse mass of a free end (1 / EndMass); 0 while pinned. */
	double EndInvMass = 0.0;

	/** 0 = pinned (the tip; the end unless free), 1 / EndMass for a free end, 1 for the points in between. */
	double InvMassOf(int32 Index) const;
	/** Input with every value finite and in range (the last good values for bad ones). */
	FLureLineSimInput Sanitize(const FLureLineSimInput& In) const;

	// ---- Collision (T-032b) ----

	/** Most solids one Step collides with (the first ones, in the colliders' order, near the line's box); the rest are ignored that Step. */
	static constexpr int32 MaxNearSolids = 64;

	/** The solids near the line this Step, as indices into the colliders (fixed size: no allocation). */
	struct FNearSolids
	{
		const FLureLineColliders* Colliders = nullptr;
		int32 Convex[MaxNearSolids];
		int32 Rounded[MaxNearSolids];
		int32 NumConvex = 0;
		int32 NumRounded = 0;
		/** Tuning.CollisionRadius. */
		double Radius = 0.0;

		bool IsEmpty() const { return NumConvex == 0 && NumRounded == 0; }
	};

	/** Where each point was when the sub-step began: the path collision judges (Previous also carries speed edits). */
	TArray<FVector> SubstepStart;

	/** Fills Near with the solids within Reach of the line's box (points, In.Start, a pinned In.End); none without colliders. */
	void FindNearSolids(const FLureLineSimInput& In, double Reach, double Radius, FNearSolids& Near) const;
	/** Free points out of every near solid, judged along their path this sub-step (SubstepStart -> now). */
	void CollidePoints(const FNearSolids& Near);
	/** Segments whose ends are outside a solid but that dip into it, lifted over the edge they cut. */
	void CollideSegments(const FNearSolids& Near);
	/** Once per sub-step: free points touching a solid lose their speed into it and keep Keep of their speed along it. */
	void ApplyContactVelocity(const FNearSolids& Near, double Keep);

	/**
	 *  Iterations solver passes. With Near, each pass ends with CollidePoints then CollideSegments, and one more
	 *  CollidePoints follows the last pass (points have the last word).
	 */
	void SolveConstraints(int32 Iterations, const FNearSolids* Near = nullptr);
	/** Float rule for one sub-step; no point rises more than MaxLift, cm. */
	void ApplyFloat(double SurfaceZ, double Float, bool bFloatFreeEnd, double MaxLift);
	/** Velocity of point Index after the last sub-step (cm/s). */
	FVector VelocityOf(int32 Index) const;
};

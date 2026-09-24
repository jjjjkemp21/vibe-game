// Lure: the fishing line's rope simulation (T-032). Plain C++, no UObjects: tests drive it directly, ULureFishingLineComponent
// draws it. Model and tuning: docs/specs/fishing-line.md ("Simulation").

#pragma once

#include "CoreMinimal.h"

struct FLureFishingLineRow;

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
	void SolveConstraints(int32 Iterations);
	/** Float rule for one sub-step; no point rises more than MaxLift, cm. */
	void ApplyFloat(double SurfaceZ, double Float, bool bFloatFreeEnd, double MaxLift);
	/** Velocity of point Index after the last sub-step (cm/s). */
	FVector VelocityOf(int32 Index) const;
};

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
}

void FLureLineSim::Init(int32 Segments)
{
	const int32 Count = FMath::Clamp(Segments, 1, MaxSegments) + 1;
	Positions.SetNumZeroed(Count, EAllowShrinking::Yes);
	Previous.SetNumZeroed(Count, EAllowShrinking::Yes);
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
		SegmentLength = FMath::Max(static_cast<double>(In.RestLength), Distance) / N;
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
	double RestLength = In.RestLength;
	if (!bFreeEnd)
	{
		// A pinned line can't be shorter than the straight distance at either end of this frame (it would have to stretch).
		RestLength = FMath::Max(RestLength, FMath::Max(FVector::Dist(In.Start, In.End), FVector::Dist(LastStart, LastEnd)));
	}
	SegmentLength = FMath::Min(RestLength, MaxLength) / N;

	const double Dt = (FMath::IsFinite(DeltaTime) && DeltaTime > 0.f) ? static_cast<double>(DeltaTime) : 0.0;
	if (Dt <= 0.0)
	{
		// No time passes: only the pinned ends move (a paused or first frame).
		Previous[0] = Positions[0] = In.Start;
		if (!bFreeEnd)
		{
			Previous[N] = Positions[N] = In.End;
		}
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

	const FVector GravityStep(0.0, 0.0, -Gravity * FMath::Clamp(Finite(Tuning.GravityScale, 1.0), 0.0, 10.0) * H * H);
	const double AirKeep = FMath::Exp(-FMath::Max(0.0, Finite(Tuning.AirDrag, 0.0)) * H);
	const double WaterKeep = FMath::Exp(-FMath::Max(0.0, Finite(Tuning.WaterDrag, 0.0)) * H);
	const double EndKeep = In.EndDrag >= 0.f ? FMath::Exp(-static_cast<double>(In.EndDrag) * H) : AirKeep;
	const double SurfaceZ = In.WaterZ + FMath::Max(0.0, Finite(Tuning.FloatHeight, 0.0));
	const double WetZ = SurfaceZ + WetBand;
	const double Float = In.bHasWater ? static_cast<double>(In.Float) : 0.0;
	const bool bFloatFreeEnd = EndInvMass >= 1.0; // a snapped end floats; a hanging fish (heavier) does not

	const FVector FromStart = LastStart;
	const FVector FromEnd = LastEnd;
	for (int32 Sub = 1; Sub <= Substeps; ++Sub)
	{
		const double Alpha = static_cast<double>(Sub) / Substeps;
		const double VelocityScale = LastSubstep > 0.0 ? H / LastSubstep : 1.0; // time-corrected Verlet
		const int32 LastFree = bFreeEnd ? N : N - 1;
		for (int32 Index = 1; Index <= LastFree; ++Index)
		{
			const bool bWet = In.bHasWater && Positions[Index].Z < WetZ;
			const double Keep = bWet ? WaterKeep : (Index == N ? EndKeep : AirKeep);
			const FVector Velocity = (Positions[Index] - Previous[Index]) * (VelocityScale * Keep);
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
		SolveConstraints(Iterations);
		if (Float > 0.0)
		{
			ApplyFloat(SurfaceZ, Float, bFloatFreeEnd);
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

void FLureLineSim::SolveConstraints(int32 Iterations)
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
	}
}

void FLureLineSim::ApplyFloat(double SurfaceZ, double Float, bool bFloatFreeEnd)
{
	const int32 N = GetNumSegments();
	const int32 Last = (bFreeEnd && bFloatFreeEnd) ? N : N - 1;
	for (int32 Index = 1; Index <= Last; ++Index)
	{
		FVector& Point = Positions[Index];
		if (Point.Z < SurfaceZ)
		{
			const double Lift = (SurfaceZ - Point.Z) * Float;
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

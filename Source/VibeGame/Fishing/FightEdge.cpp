// Lure T-047: the server's look for a dock edge between a hooked fish and its angler. See FightEdge.h.

#include "Fishing/FightEdge.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "GameFramework/Actor.h"

namespace LureFightEdgePrivate
{
	bool Finite2(const FVector2D& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y);
	}

	float NonNegative(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f;
	}

	/** Skips what a hit was on, for the next pass of a filtered query (false: nothing to skip, stop). */
	bool IgnoreHit(FCollisionQueryParams& Params, const FHitResult& Hit)
	{
		if (UPrimitiveComponent* Component = Hit.GetComponent())
		{
			Params.AddIgnoredComponent(Component);
			return true;
		}
		if (const AActor* Actor = Hit.GetActor())
		{
			Params.AddIgnoredActor(Actor);
			return true;
		}
		return false;
	}
}

FLureFightEdge FLureFightEdgeQuery::Find(const UWorld* World, const FVector2D& FishXY, const FVector2D& PlayerXY, float WaterZ, const FLureFishFightRow& Tuning,
	const AActor* Ignore)
{
	FLureFightEdge Edge;
	const float Radius = LureFightEdgePrivate::NonNegative(Tuning.EdgeClearance);
	if (!World || Radius <= 0.f || !FMath::IsFinite(WaterZ) || !LureFightEdgePrivate::Finite2(FishXY) || !LureFightEdgePrivate::Finite2(PlayerXY))
	{
		return Edge;
	}
	const FVector2D Path = PlayerXY - FishXY;
	const double Length = Path.Size();
	if (Length < 1.0)
	{
		return Edge; // the fish is at the player: nothing between them
	}
	const FVector2D Along = Path / Length;
	const float Below = LureFightEdgePrivate::NonNegative(Tuning.EdgeProbeDepth);
	const float Above = LureFightEdgePrivate::NonNegative(Tuning.EdgeProbeHeight);
	const float MaxLift = LureFightEdgePrivate::NonNegative(Tuning.EdgeMaxLift);

	// The probe: a capsule over the water line (its half height includes the round ends).
	const double CenterZ = static_cast<double>(WaterZ) + 0.5 * static_cast<double>(Above - Below);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, FMath::Max(Radius, 0.5f * (Above + Below)));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureFightEdge), /*bTraceComplex*/ false, Ignore);
	const FVector Start(FishXY.X, FishXY.Y, CenterZ);
	const FVector End(PlayerXY.X, PlayerXY.Y, CenterZ);
	constexpr int32 MaxPasses = 16; // each pass skips one more thing that isn't solid (zones, pawns, overlap-only shapes)
	for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
	{
		FHitResult Hit;
		if (!World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, FLureFishingSpots::CastChannel, Capsule, Params))
		{
			return Edge; // the way is open
		}
		const FVector2D Push(Hit.Normal.X, Hit.Normal.Y); // for a sweep: from what was hit toward the capsule's centre
		const bool bShallowGround = Hit.bStartPenetrating && Push.Size() < 0.5;
		if (!FLureFishingSpots::BlocksCast(Hit) || bShallowGround)
		{
			if (!LureFightEdgePrivate::IgnoreHit(Params, Hit))
			{
				return Edge;
			}
			continue;
		}
		FVector2D Point;
		FVector2D Normal;
		if (Hit.bStartPenetrating)
		{
			// Already touching (a fish held at the edge, or put against it): the edge is where it would be pushed out to.
			Normal = Push.GetSafeNormal();
			Point = FishXY + FVector2D(Hit.Normal.X, Hit.Normal.Y) * static_cast<double>(FMath::Max(0.f, Hit.PenetrationDepth));
		}
		else
		{
			Point = FVector2D(Hit.Location.X, Hit.Location.Y);
			Normal = Push.Size() >= 0.3 ? Push.GetSafeNormal() : -Along; // a nearly flat hit (the shore's slope): across the path
		}
		if (FVector2D::DotProduct(PlayerXY - Point, Normal) >= 0.0)
		{
			Normal = -Along; // an odd normal that would not separate them: the edge across the path
		}

		// Its top, measured EdgeTopInset in past the face.
		const FVector2D Inside = Point - Normal * static_cast<double>(Radius + LureFightEdgePrivate::NonNegative(Tuning.EdgeTopInset));
		FCollisionQueryParams TopParams(SCENE_QUERY_STAT(LureFightEdgeTop), /*bTraceComplex*/ false, Ignore);
		FHitResult Top;
		float Lift = MaxLift; // no top found (it starts above the highest lift: a cliff): as high as allowed
		if (FLureFishingSpots::TraceCast(World, Top, FVector(Inside.X, Inside.Y, static_cast<double>(WaterZ) + MaxLift + 1.0),
			FVector(Inside.X, Inside.Y, static_cast<double>(WaterZ) - Below - 1.0), TopParams))
		{
			Lift = static_cast<float>(Top.ImpactPoint.Z - static_cast<double>(WaterZ)) + LureFightEdgePrivate::NonNegative(Tuning.EdgeLiftClearance);
		}
		Edge.bValid = true;
		Edge.Point = Point;
		Edge.Normal = Normal;
		Edge.LandLift = FMath::Clamp(Lift, 0.f, MaxLift);
		return Edge;
	}
	return Edge;
}

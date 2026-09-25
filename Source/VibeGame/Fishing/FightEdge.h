// Lure T-047: the server's look for a dock edge between a hooked fish and its angler (docs/specs/reel-fight-rules.md
// "Dock edges"). The fight itself stays pure (FLureFight::SetEdge takes the result); this is the only world query of the fight.

#pragma once

#include "CoreMinimal.h"
#include "Fishing/FishFight.h"

class AActor;
class UWorld;

struct FLureFightEdgeQuery
{
	/**
	 *  Looks along the water line from the fish toward the player for anything solid, the way a cast is stopped
	 *  (FLureFishingSpots::CastChannel and BlocksCast: volumes, triggers, pawns and overlap-only shapes never count):
	 *   - a capsule of radius EdgeClearance spanning WaterZ - EdgeProbeDepth .. WaterZ + EdgeProbeHeight is swept from FishXY to
	 *     PlayerXY. The first solid hit is the edge: Point = the capsule's centre there (EdgeClearance out from the face),
	 *     Normal = the hit's horizontal normal (a nearly flat hit, the shore's slope or a deck's underside: back along the sweep).
	 *     A fish already touching something starts the sweep inside it: the edge is where it would be pushed out to (the hit's
	 *     depenetration), unless that push is mostly vertical (the ground under very shallow water: ignored, the sweep goes on).
	 *   - the edge's top: a trace straight down EdgeTopInset past the face (from WaterZ + EdgeMaxLift). LandLift = top - WaterZ +
	 *     EdgeLiftClearance, at most EdgeMaxLift; no top found (it is taller than that: a cliff) = EdgeMaxLift.
	 *  bValid false: the way is open, EdgeClearance is 0, or no world. Ignore (the angler) is skipped.
	 */
	static FLureFightEdge Find(const UWorld* World, const FVector2D& FishXY, const FVector2D& PlayerXY, float WaterZ, const FLureFishFightRow& Tuning,
		const AActor* Ignore = nullptr);
};

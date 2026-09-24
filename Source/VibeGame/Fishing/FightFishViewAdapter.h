// Lure: the fish visual's adapter to the fight (T-029). The ONLY place the fish visual reads the fight's replicated
// structs (FLureFightNetState, FLureFishingNetState) and ULureFishingComponent. When the fight changes (T-028: rod aim,
// reel speed, new states), update this file; ALureFightFish, ULureFightFishSubsystem and FFightFishVisual stay as they are.

#pragma once

#include "CoreMinimal.h"
#include "Fish/FightFishVisual.h"

struct FFishInstance;
struct FLureFightNetState;
struct FLureFishingNetState;
class ULureFishingComponent;

struct FFightFishViewAdapter
{
	/**
	 *  The view from replicated state (every machine has these):
	 *    bFighting = Fight.bActive and the line is Hooked. End (when not fighting) = Landed if Fight.Outcome or the line's
	 *    LastResult is Landed, else Escaped.
	 *    LineEnd = Player + (direction to the bobber's rest point, turned by Fight.SideDeg) x Fight.LineOut, at the water
	 *    surface (Line.BobberRest.Z) - the same point the bobber rides on during the fight.
	 */
	static FFightFishView Make(const FLureFightNetState& Fight, const FLureFishingNetState& Line, const FFishInstance& HookedFish,
		const FVector& PlayerLocation, const FVector& PlayerForward, bool bHasAuthority);

	/** Make() from a fishing component and its owner (location, forward, authority). */
	static FFightFishView FromComponent(const ULureFishingComponent& Fishing);
};

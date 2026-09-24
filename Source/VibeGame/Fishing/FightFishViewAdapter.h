// Lure: the fish visual's adapter to the fight (T-029). The ONLY place the fish visual reads the fight's replicated
// structs (FLureFightNetState, FLureFishingNetState) and ULureFishingComponent. When the fight changes, update this file;
// ALureFightFish, ULureFightFishSubsystem and FFightFishVisual stay as they are.
// T-028 (rod-steered fight): the rod turns the fish through SideDeg (and its pull/tension through Tension), which this
// adapter already reads, so the fish follows the rod with no new field. RodPitch/RodYaw/ReelStep/RunSide are the rod's and
// the HUD's state, not the fish's: not read here. Test: Project.FishVisual.Adapter.FollowsRodSteeredFight.

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

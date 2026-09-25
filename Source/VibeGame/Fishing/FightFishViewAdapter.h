// Lure: the fish visual's adapter to the fight (T-029). The ONLY place the fish visual reads the fight's replicated
// structs (FLureFightNetState, FLureFishingNetState) and ULureFishingComponent. When the fight changes, update this file;
// ALureFightFish, ULureFightFishSubsystem and FFightFishVisual stay as they are.
// T-045: the line end is the fish's replicated world location (FLureFightNetState::FishLocation), so the fish stays put
// while its player walks, and every machine shows it at the same place. Test: Project.Fishing.Fight.Anchor.*.
// T-047: a fish lifted at a dock edge (FLureFightNetState::Lift) is drawn that much higher: WaterZ and LineEnd rise with it.
// Test: Project.Fishing.Fight.DockEdge.*.
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
	 *    LineEnd = Fight.FishLocation (T-045: the fish's world XY from the server's fight; the player's walking never moves
	 *    it), at the water surface (Line.BobberRest.Z) - the same point the bobber rides on during the fight. WaterZ and LineEnd
	 *    are raised by Fight.Lift (T-047: lifted at a dock edge; kept after the fight ends, for the landed hand-off).
	 *    PlayerLocation is passed through (the fish faces away from it); PlayerForward is unused since T-045.
	 */
	static FFightFishView Make(const FLureFightNetState& Fight, const FLureFishingNetState& Line, const FFishInstance& HookedFish,
		const FVector& PlayerLocation, const FVector& PlayerForward, bool bHasAuthority);

	/** Make() from a fishing component and its owner (location, forward, authority). */
	static FFightFishView FromComponent(const ULureFishingComponent& Fishing);
};

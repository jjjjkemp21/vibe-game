// Lure: the fish visual's adapter to the fight (T-029). The ONLY place the fish visual reads the fight's replicated
// structs (FLureFightNetState, FLureFishingNetState) and ULureFishingComponent. When the fight changes, update this file;
// ALureFightFish, ULureFightFishSubsystem and FFightFishVisual stay as they are.
// T-045: the line end is the fish's replicated world location (FLureFightNetState::FishLocation), so the fish stays put
// while its player walks, and every machine shows it at the same place. Test: Project.Fishing.Fight.Anchor.*.
// T-047: a fish lifted at a dock edge (FLureFightNetState::Lift) is drawn that much higher: WaterZ and LineEnd rise with it.
// Test: Project.Fishing.Fight.DockEdge.*.
// T-028 (rod-steered fight): the rod turns the fish through SideDeg (and its pull/tension through Tension), which this
// adapter already reads, so the fish follows the rod with no new field. RodPitch/RodYaw/ReelStep/RunSide are the rod's and
// the HUD's state, not the fish's: not read here (RunSide only for T-048b below). Test: Project.FishVisual.Adapter.FollowsRodSteeredFight.
// T-048b: the body swing follows the current move's own swim (DT_FightPattern row PatternId, move MoveId: Away/Side/Rest)
// with the side from the replicated RunSide (the side the server picked for RandomSide moves), so every machine gets the
// same swing from replicated fields and the shipped table, no new replicated field. Test: Project.FishVisual.SwimFacing.*.

#pragma once

#include "CoreMinimal.h"
#include "Fish/FightFishVisual.h"
#include "Fishing/FishFightTypes.h"

struct FFishInstance;
struct FLureFightNetState;
class UDataTable;

/**
 *  T-048b: the fight patterns the fish visuals use, resolved once per PatternId (no DataTable lookup, validation or copy
 *  per frame). The owner (ULureFightFishSubsystem) keeps the table alive; SetTable with another table clears the cache.
 */
struct FFightPatternCache
{
	/** Use this table from now on (null = the built-in pattern for every id). A different table clears the cache. */
	void SetTable(const UDataTable* InTable);

	/** FFightFishViewAdapter::FindPattern(Table, PatternId), looked up the first time an id is asked for. Valid until the next Get or SetTable. */
	const FLureFightPatternRow& Get(FName PatternId);

	/** How many times a pattern was looked up in the table (tests: once per id). */
	int32 GetResolveCount() const { return ResolveCount; }

private:
	TWeakObjectPtr<const UDataTable> Table;
	TMap<FName, FLureFightPatternRow> Rows;
	int32 ResolveCount = 0;
};
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

	/**
	 *  T-048b: View.bMoveSwims / View.MoveSwimSide from the fight's current move in Pattern (the DT_FightPattern row the
	 *  fight uses; FindPattern). bMoveSwims = View.bFighting, not exhausted, the move MoveId is in the pattern, is not a Rest
	 *  and has Speed > 0. MoveSwimSide = RunSide (Right +1, Left -1, None 0) x |Side| / length(Away, Side) of that move.
	 */
	static void ApplyMove(FFightFishView& View, const FLureFightNetState& Fight, const FLureFightPatternRow& Pattern);

	/** The move MoveId in Pattern (nullptr: None or not in it). */
	static const FLureFightMove* FindMove(const FLureFightPatternRow& Pattern, FName MoveId);

	/**
	 *  The pattern the server fights with: row PatternId of PatternTable when it is a valid FLureFightPatternRow; else the
	 *  built-in pattern (the server replicates PatternId None when it fell back to it, so every machine agrees).
	 */
	static FLureFightPatternRow FindPattern(const UDataTable* PatternTable, FName PatternId);

	/** The built-in pattern (FLureFightPatternRow::GetFallbackPattern), built once. */
	static const FLureFightPatternRow& FallbackPattern();

	/**
	 *  Make() from a fishing component and its owner (location, forward, authority), then ApplyMove with the fight's pattern
	 *  from Patterns (resolved once per PatternId); no cache = the built-in pattern.
	 */
	static FFightFishView FromComponent(const ULureFishingComponent& Fishing, FFightPatternCache* Patterns = nullptr);
};

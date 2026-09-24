// Lure: physics fishing line, tuning row and pure rules (T-032). Spec: docs/specs/fishing-line.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Fishing/FishingTypes.h"
#include "FishingLineTypes.generated.h"

/**
 *  One DT_FishingLine row (source data/tables/DT_FishingLine.csv): how the cosmetic fishing line moves.
 *  The line's look stays where it was: segment count, pixel width and minimum width in DT_Fishing (LineSegments,
 *  LinePixelWidth, LineMinWidth); mesh, material and colour in the Lure Fishing settings.
 *  One row per kind of line ("Default" today). A line gear item can name its own row later (braid: stiffer, floats more)
 *  without code. The struct defaults ARE the shipped "Default" row (Project.Fishing.Line.Data.* checks they match).
 */
USTRUCT(BlueprintType)
struct FLureFishingLineRow : public FTableRowBase
{
	GENERATED_BODY()

	// ---- Simulation ----

	/** Sub-steps per second. A frame runs ceil(DeltaTime x SubstepRate) sub-steps (at least 1, at most MaxSubsteps). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Simulation", meta=(ClampMin="30", ClampMax="1000"))
	float SubstepRate = 120.f;

	/** Most sub-steps in one frame. A longer frame (a hitch) moves the line in slow motion instead of exploding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Simulation", meta=(ClampMin="1", ClampMax="32"))
	int32 MaxSubsteps = 8;

	/** Constraint passes per sub-step: more = a stiffer, less stretchy line (costs more). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Simulation", meta=(ClampMin="1", ClampMax="32"))
	int32 Iterations = 6;

	/** Gravity on the line, x 980 cm/s2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Simulation", meta=(ClampMin="0"))
	float GravityScale = 1.f;

	/** Share of its speed the line loses per second in the air (a thin line settles fast). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Simulation", meta=(ClampMin="0"))
	float AirDrag = 2.f;

	/** Share of its speed the line loses per second on or under the water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Simulation", meta=(ClampMin="0"))
	float WaterDrag = 10.f;

	// ---- Slack and tension ----

	/** Extra line, as a share of the straight distance, when there is no tension: the line droops from the rod tip and lies on the water. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0", ClampMax="1"))
	float SlackShare = 0.05f;

	/**
	 *  How fast the slack goes as tension rises: tautness = 1 - (1 - Tension01)^TautExponent. Tension01 1 = the line's strength
	 *  (the snap threshold): fully straight there. Higher = straight sooner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0.1", ClampMax="10"))
	float TautExponent = 3.f;

	/**
	 *  Per second, how fast the line's length shrinks toward a tighter target (63 % of the way each 1 / LengthResponse s): a
	 *  line tightening to a still-slack shape, a hanging line reeling up. Slack appears at once.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0.1"))
	float LengthResponse = 6.f;

	/**
	 *  Seconds a line with the full SlackShare takes to go straight when pulled to the snap threshold (tension 1). Its sag
	 *  falls at a steady rate and stops at the straight line, so it doesn't whip past it. Smaller = snappier.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0.05", ClampMax="10"))
	float StraightenTime = 0.4f;

	/** Tension the line shows per fishing state (0..1). During a reel fight the fight's own tension is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0", ClampMax="1"))
	float CastTension = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0", ClampMax="1"))
	float WaitTension = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0", ClampMax="1"))
	float BiteTension = 0.6f;

	/** A fish on the line without a reel fight (the AutoLandDelay debug placeholder). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0", ClampMax="1"))
	float HookedTension = 0.8f;

	// ---- Water ----

	/** 0..1 per sub-step: how fast line under the water rises to the surface. Scaled by (1 - tautness): a taut line cuts straight in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0", ClampMax="1"))
	float FloatStrength = 0.5f;

	/** The floating line rests this far above the water surface, cm (so it shows over the water plane). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="0"))
	float FloatHeight = 0.4f;

	/** When the owner gives no water height, the line looks it up again once its end moved this far, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Water", meta=(ClampMin="1"))
	float WaterRefreshDistance = 500.f;

	// ---- Snap ----

	/** A snapped line's free end flies back toward the rod at this speed, cm/s (points in between: in proportion). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Snap", meta=(ClampMin="0"))
	float RecoilSpeed = 2500.f;

	/** Seconds the snap recoil plays before the line is gone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Snap", meta=(ClampMin="0.05"))
	float RecoilTime = 0.8f;

	/** The snapped line shrinks to this share of its length during the recoil (it curls back toward the rod). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Snap", meta=(ClampMin="0", ClampMax="1"))
	float RecoilLengthShare = 0.3f;

	// ---- Hanging actor (the landed fish) ----

	/** A hanging actor weighs this many line points: it pulls the line straight and swings like a pendulum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hanging", meta=(ClampMin="1"))
	float HangEndMass = 25.f;

	/** Share of its speed a hanging actor loses per second (lower = it swings longer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hanging", meta=(ClampMin="0"))
	float HangDrag = 0.4f;

	/**
	 *  A longer line reels a hanging actor up at most this fast, cm/s, and slower near the end (it slows at half of gravity), so
	 *  the actor arrives under the rod tip without being flung past it (T-032b). A shorter line drops to its length at once.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hanging", meta=(ClampMin="1"))
	float HangReelSpeed = 500.f;

	/**
	 *  A hanging actor swings at most this far from straight under the rod tip, degrees: faster than that it can't rise, so a
	 *  rod that jumps or whips never throws it over the tip (T-032b). 90 = level with the tip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hanging", meta=(ClampMin="10", ClampMax="90"))
	float HangMaxSwingDeg = 70.f;

	// ---- Safety ----

	/** A rod tip or end that jumps farther than this in one frame (a teleport) resets the line instead of whipping it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Safety", meta=(ClampMin="1"))
	float TeleportDistance = 1500.f;

	// ---- Collision (T-032b): the line lies on docks, rocks and the ground and bends over their edges ----

	/**
	 *  The line keeps this far from solid surfaces, cm (what blocks a cast: docks, posts, rocks, the ground; see
	 *  docs/specs/fishing-line.md "Collision"). 0 = on the surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0", ClampMax="50"))
	float CollisionRadius = 1.f;

	/** Share of its sliding speed line touching a surface loses per second (it comes to rest on a dock instead of skating). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0"))
	float GroundFriction = 8.f;

	/** Solids are looked up in the line's bounds grown by this much, cm, and looked up again only once the line leaves that box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="10"))
	float CollisionQueryMargin = 300.f;

	/** While a movable solid (a boat) is near the line, the solids are looked up again this often, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0.02"))
	float CollisionRefreshTime = 0.2f;

	/** Runtime sanity check (finite, in range). Returns false and a reason if the row is unusable. */
	bool Validate(FString& OutProblem) const;
};

/** Pure rules of the fishing line (every machine computes them from replicated state; tests call them directly). */
struct FLureFishingLineRules
{
	/** 0..1, how straight tension pulls the line: 1 - (1 - Tension01)^TautExponent (Tension01 clamped to 0..1; NaN = 0). */
	static float Tautness(float Tension01, const FLureFishingLineRow& Row);

	/**
	 *  The length the line moves toward, cm: Chord x (1 + Slack x (1 - Tautness)), never below Chord.
	 *  Slack < 0 (or NaN) = the row's SlackShare.
	 */
	static float TargetRestLength(float Chord, float Tension01, float Slack, const FLureFishingLineRow& Row);

	/**
	 *  The line length after DeltaTime: grows to a longer Target at once (slack appears as fast as the fish swims at you);
	 *  shrinks toward a shorter one smoothly (LengthResponse per second), and is exactly the target once within 1e-5 of it
	 *  (so a line pulled to the snap threshold ends exactly straight); never below MinLength.
	 */
	static float FollowRestLength(float Current, float Target, float MinLength, float DeltaTime, const FLureFishingLineRow& Row);

	/**
	 *  A pinned line's length after DeltaTime (Chord = the straight distance between its ends): grows to a longer Target at
	 *  once; shrinks like FollowRestLength, and toward a (nearly) straight target at least so fast that its sag falls at a
	 *  steady rate: a line with the full SlackShare goes straight in StraightenTime s and stops there without whipping past.
	 *  Exactly the target once within 1e-5 of it; never below Chord.
	 */
	static float TightenRestLength(float Current, float Target, float Chord, float DeltaTime, const FLureFishingLineRow& Row);

	/**
	 *  A pinned line's length carried to ends that moved (T-032b): the same share longer than the straight distance as before
	 *  (Current / LastChord, clamped to 1..2) at the new distance Chord, so ends that close in (reeling, a fish swimming at you)
	 *  leave no extra line behind and ends that part don't pull the line straight. Current unchanged when LastChord < 1 cm or
	 *  any input is not finite (a new line).
	 */
	static float CarryRestLength(float Current, float LastChord, float Chord);

	/**
	 *  A hanging line's length after DeltaTime, reeled toward Target (the hang length): a shorter line drops to Target at once;
	 *  a longer one shrinks at min(HangReelSpeed, sqrt(2 x a x (Current - Target))) cm/s with a = half of gravity
	 *  (980 x GravityScale / 2; no easing without gravity), so it arrives exactly at Target, slowing down on the way, and the
	 *  hanging actor, which gravity slows faster than that, never overtakes the reel and flies past the rod tip (T-032b).
	 */
	static float ReelInRestLength(float Current, float Target, float DeltaTime, const FLureFishingLineRow& Row);

	/**
	 *  The tension the line shows in a fishing state: CastTension while the bobber flies, WaitTension while it floats,
	 *  BiteTension during a bite, the fight's Tension01 (clamped 0..1) in a reel fight, HookedTension on a fish without one.
	 *  In a reel fight the fishing component passes FLureFight::LineTension (1 from DT_FishFight TautTension of the line's
	 *  strength: a fish pulling on the line pulls it straight, T-032b), not the raw share of the strength.
	 */
	static float StateTension(ELureFishingState State, bool bFightActive, float FightTension01, const FLureFishingLineRow& Row);

	/** 0..1 float strength per sub-step: FloatStrength x (1 - Tautness). */
	static float FloatAmount(float Tension01, const FLureFishingLineRow& Row);

	/** The built-in row used when DT_FishingLine is missing or its row is invalid (the struct defaults = the shipped CSV). */
	static FLureFishingLineRow GetFallbackRow();

	/** Substring of the one message logged when the line uses the built-in row. */
	static const TCHAR* FallbackMarker;
};

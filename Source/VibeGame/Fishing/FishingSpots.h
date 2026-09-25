// Lure: fishing spot markers and water lookups (T-006). Marker contract: docs/levels/L_PalmKey.md section 11.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Fishing/FishingTypes.h"

class AActor;
struct FCollisionQueryParams;
struct FHitResult;
class ULureFishingSettings;
class UWorld;

/** Where a cast lands (server). */
struct FLureCastLanding
{
	/** Bobber rest point: on the water surface (the bobber's pivot is its waterline) or the ground hit. */
	FVector Rest = FVector::ZeroVector;
	bool bOnWater = false;
	/** The water surface height under the rest point (valid when bFoundWater). */
	float WaterZ = 0.f;
	bool bFoundWater = false;
	/** The flight was blocked (wall, rock) and the landing pulled back. */
	bool bBlocked = false;
};

/**
 *  Reads fishing spots from marker actors and finds water. Markers are any actors (TargetPoints today, a gameplay class later)
 *  tagged with the settings' FishingSpotTag ("Lure.FishingSpot") plus "Key=Value" tags:
 *    Spot=<id>  Name=<text>  Habitat=<tag>  Region=<tag>  Radius=<cm>  Luck=<float>  Hours=<s-e;s-e>  Levels=<min-max>
 *    Danger=<name>  CastFrom=<x,y,z>
 *  Keys are case-insensitive; unknown tags are ignored. A marker needs Radius > 0 to count. Lookups walk the level's actors
 *  each call (a few per cast, about 10 markers), so markers placed, moved or removed at any time are always seen.
 */
struct FLureFishingSpots
{
	/**
	 *  Parses one marker's tags. Returns false (and why, in OutProblems) if it isn't a usable spot (not tagged, or Radius missing).
	 *  Soft problems (an unregistered Habitat/Region tag, a bad number) are listed in OutProblems but the spot still parses.
	 */
	static bool ParseSpotTags(const TArray<FName>& Tags, FName SpotTag, FLureFishingSpot& OutSpot, TArray<FString>* OutProblems = nullptr);

	/** Every usable spot marker in World (the location is the actor's). */
	static TArray<FLureFishingSpot> GatherSpots(const UWorld* World, FName SpotTag);

	/**
	 *  The spot whose radius (2D, on the water) contains Location. Overlapping spots: the one you are deepest in wins
	 *  (smallest distance / radius). False if none.
	 */
	static bool FindSpotAt(const UWorld* World, const FVector& Location, FName SpotTag, FLureFishingSpot& OutSpot);

	/** Water surface height at XY: a water physics volume, then an actor tagged WaterTag (top of its bounds), then the fallback Z. */
	static bool FindWaterSurfaceZ(const UWorld* World, const FVector2D& XY, const ULureFishingSettings& Settings, float& OutZ);

	/**
	 *  Where a cast from Origin toward the horizontal Direction lands after Distance cm (measured from StartXY):
	 *  pulled back in front of anything solid on the way, then on the water surface if the ground there is not above it,
	 *  else on the ground (land). Ignores IgnoreActor (the caster). Traces use TraceCast (zones and triggers never block).
	 *  T-071: the downward ground search starts at most Profile.LandingSearchHeight above the origin (or the water), and
	 *  below the first solid surface straight above Origin (a roof over the caster): under a roof the bobber lands on the
	 *  ground/water under it, never on the roof. Profile null = the built-in FLureFishingRow defaults.
	 */
	/**
	 *  The trace channel of cast flights and landings: "LureCast" in Config/DefaultEngine.ini ([/Script/Engine.CollisionProfile],
	 *  default Block; the Trigger/OverlapAll profiles ignore it). Set an asset's LureCast response to Ignore to let casts pass it.
	 */
	static constexpr ECollisionChannel CastChannel = ECC_GameTraceChannel1;

	/**
	 *  Does this hit stop a cast (flight or landing)? Only solid level geometry does. Never: design zones and other volumes
	 *  (AVolume, ATriggerBase), pawns, components that don't block CastChannel, and overlap-only components (they block none of
	 *  WorldStatic, WorldDynamic, Pawn, PhysicsBody, Vehicle), whatever collision profile they were given.
	 */
	static bool BlocksCast(const FHitResult& Hit);

	/**
	 *  Line trace on CastChannel that passes through everything BlocksCast rejects (they are ignored and the trace goes on).
	 *  True with OutHit = the first solid hit. Params' ignore lists are kept (a copy is extended).
	 */
	static bool TraceCast(const UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionQueryParams& Params);

	static FLureCastLanding ResolveLanding(const UWorld* World, const AActor* IgnoreActor, const FVector& Origin, const FVector2D& StartXY,
		const FVector2D& Direction, float Distance, const ULureFishingSettings& Settings, const FLureFishingRow* Profile = nullptr);
};

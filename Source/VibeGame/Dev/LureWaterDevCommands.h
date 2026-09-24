// Lure: dev-only console commands for the water model and hot spots (T-027). Compiled out of Shipping builds.
//
//   Lure.Water.Show [Seconds]                 draws every water area (outline, colored by habitat) and hot spot for Seconds
//                                             (default 20) and lists them in the log
//   Lure.Water.Probe [Distance]               the water Distance cm (default 1000) in front of the local player: area,
//                                             habitat, depth, hot spot
//   Lure.HotSpot.Spawn [Type] [Distance] [Lifetime]   server/standalone: a hot spot of DT_HotSpot row Type (default: the
//                                             first row) on the water Distance cm (default 1000) in front of the local
//                                             player, for Lifetime seconds (default: from the row)
//   Lure.HotSpot.Clear                        server/standalone: removes every hot spot

#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class ALureHotSpot;
class UWorld;

struct FLureWaterDevCommands
{
	static const TCHAR* const ShowCommand;
	static const TCHAR* const ProbeCommand;
	static const TCHAR* const SpawnHotSpotCommand;
	static const TCHAR* const ClearHotSpotsCommand;

	/** The point on the water Distance cm in front of the local player (view yaw, horizontal). False if there is no player. */
	static bool GetPointInFront(UWorld* World, float Distance, FVector2D& OutXY);

	/** Spawns a hot spot of TypeId (None = the first row) at the water in front of the local player. Null + OutMessage on failure. */
	static ALureHotSpot* SpawnHotSpotInFront(UWorld* World, FName TypeId, float Distance, float Lifetime, FString& OutMessage);

	/** Removes every hot spot (server/standalone); returns how many. */
	static int32 ClearHotSpots(UWorld* World);

	/** One line per water area and hot spot; draws them for DrawSeconds when > 0. */
	static FString DescribeWater(UWorld* World, float DrawSeconds);

	/** What the water Distance cm in front of the local player is. */
	static FString ProbeInFront(UWorld* World, float Distance);
};

#endif // !UE_BUILD_SHIPPING

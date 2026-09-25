// Lure: dev-only console commands for the day/night clock (T-068a). Compiled out of Shipping builds.
//
//   Lure.Time.Set <hour>                      server/standalone: jumps to the hour ("21", "21.5" or "21:30")
//   Lure.Time.Scale <x>                       server/standalone: real-time multiplier from now on (1 = the data's pace, 0 = frozen)
//   Lure.Time.Phase <Dawn|Day|Dusk|Night>     server/standalone: jumps to the start of the phase
// Every client follows through the replicated clock state. On a client world they refuse with a message (never an RPC).
// Without an argument each prints the clock ("06:40 Dawn, scale 1").

#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class FOutputDevice;
class UWorld;

struct FLureTimeDevCommands
{
	static const TCHAR* const SetCommand;
	static const TCHAR* const ScaleCommand;
	static const TCHAR* const PhaseCommand;

	/** "21", "21.5" or "21:30" as hours; false if it is not a time. */
	static bool ParseHour(const FString& Text, float& OutHour);

	/** The console entry points (public for tests). True = done; false = refused (the reason is logged and written to Ar). */
	static bool RunSet(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar);
	static bool RunScale(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar);
	static bool RunPhase(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar);
};

#endif // !UE_BUILD_SHIPPING

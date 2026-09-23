// Lure: dev-only console commands for scripted playtests (T-025). Everything below the log category is compiled out
// of Shipping builds.

#pragma once

#include "CoreMinimal.h"
#include "Character/LureMovementTypes.h"

/** Dev command feedback (Display = done, Warning = refused or failed). Python reads these lines from the editor log. */
DECLARE_LOG_CATEGORY_EXTERN(LogLureDev, Log, All);

#if !UE_BUILD_SHIPPING

class AActor;
class ALurePlayerCharacter;
class APawn;
class APlayerController;
class FOutputDevice;
class UWorld;
struct FFishInstance;
struct FFishTables;

/** Where a teleport goes (FLureDevCommands::FindTeleportTarget). */
struct FLureTeleportTarget
{
	/** The marker actor (null for a coordinate teleport) */
	TWeakObjectPtr<AActor> Marker;

	/** Where the feet go, before the floor probe */
	FVector FeetLocation = FVector::ZeroVector;

	/** The view after the teleport (pitch 0); unset = keep the current view */
	TOptional<FRotator> ViewRotation;

	/** How the id matched, for the log (e.g. tag "Teleport=tp_T3") */
	FString MatchedBy;

	/** How many actors matched in the winning tier (the index picks one; they are sorted by name) */
	int32 NumMatches = 0;
};

/** Parsed Lure.GiveFish arguments. */
struct FLureGiveFishArgs
{
	FName SpeciesId;

	/** None = roll the rarity */
	FName RarityId;

	/** Unset = a fresh random seed */
	TOptional<int32> Seed;

	/** Unset = the local player */
	TOptional<int32> PlayerId;

	/** Mods=A,B (Mods= or Mods=- forces "no modifiers") */
	bool bForceModifiers = false;
	TArray<FName> ModifierIds;

	/** Weight=0..1: position in the species weight range */
	TOptional<float> WeightFraction;
};

/**
 *  Dev console commands for scripted playtests (non-Shipping builds only). Content/Python/playtest_driver.py runs them
 *  through unreal.SystemLibrary.execute_console_command(world, ...); a person can type them in the PIE console (~).
 *
 *    Lure.Teleport <marker> [Index] [Player=<PlayerId>]        server / standalone only
 *    Lure.Teleport <X> <Y> <Z> [Yaw] [Player=<PlayerId>]       feet location in cm, yaw in degrees
 *    Lure.SetStance <Stand|Crouch|Prone> [Player=<PlayerId>]   the local player, exactly like pressing the stance key
 *    Lure.GiveFish <SpeciesId> [Rarity|-] [Seed] [Mods=A,B] [Weight=0..1] [Player=<PlayerId>]   server / standalone only
 *    (TODO T-006: Lure.Fishing.ForceBite once the fishing component is in main.)
 *
 *  Markers (Lure.Teleport), matched case-insensitively in tiers; the first tier with a match wins, its actors are sorted
 *  by name and Index (default 0) picks one:
 *    1. tag "Teleport=<id>" or "Teleport=tp_<id>"          (teleport markers: "tp_T3" or just "T3")
 *    2. a tag equal to <id>                                 ("Lure.FishingSpot", "Respawn=Dock", "Lure.Shop")
 *    3. a "Key=<id>" tag                                    ("dock_end" -> Spot=dock_end, "npc_dock" -> LureId=npc_dock)
 *    4. the actor's name or label
 *  The feet go to the marker; a "CastFrom=x,y,z" tag (fishing spots) is used instead, facing the spot. The view turns to
 *  the marker's yaw. A floor probe (FloorProbeUp above to FloorProbeDown below) puts the feet on the ground under the
 *  point, so PlayerStarts (100 cm up) and floor markers both work.
 *
 *  Players: the command acts on the world it runs in. Without Player= it is the first local player of that world; with
 *  Player=<id> it is the player whose PlayerState has that PlayerId (lets the host, or Python in the server world, move
 *  a client). Teleport and GiveFish change server state, so on a client world they refuse with a hint (server-
 *  authoritative). SetStance goes through the stance request like the key (it reaches the server through the movement
 *  component), so it only acts on a locally controlled pawn.
 */
struct FLureDevCommands
{
	static const TCHAR* const TeleportCommand;
	static const TCHAR* const SetStanceCommand;
	static const TCHAR* const GiveFishCommand;

	/** The floor probe starts this far above the target point, cm. */
	static constexpr float FloorProbeUp = 50.f;
	/** ... and ends this far below it, cm. No floor in that range = keep the point's height (the pawn falls or swims). */
	static constexpr float FloorProbeDown = 500.f;
	/** Gap between the feet and the floor after a teleport (UCharacterMovementComponent floor distance range), cm. */
	static constexpr float FloorClearance = 2.4f;

	// ---- Worlds and players ----

	/** InWorld if it is a game world, otherwise the first PIE/game world (standalone or listen server first); may be null. */
	static UWorld* ResolveWorld(UWorld* InWorld);

	/** True unless the world is a network client. */
	static bool HasAuthority(const UWorld* World);

	/** The target player controller (see the class comment); null with OutError. */
	static APlayerController* FindPlayer(UWorld* World, const TOptional<int32>& PlayerId, FString& OutError);

	/** Removes a "Key=Value" token (case-insensitive key) from Args; true if found. */
	static bool TakeKeyValue(TArray<FString>& Args, const TCHAR* Key, FString& OutValue);

	/** Removes a Player=<int> token; false with OutError if the value is not an integer. */
	static bool TakePlayerId(TArray<FString>& Args, TOptional<int32>& OutPlayerId, FString& OutError);

	// ---- Teleport ----

	/** Resolves a marker id (see the class comment). Index picks among several matches. */
	static bool FindTeleportTarget(UWorld* World, const FString& Id, int32 Index, FLureTeleportTarget& OutTarget, FString& OutError);

	/**
	 *  Resolves Lure.Teleport's arguments (without Player=): "<X> <Y> <Z> [Yaw]" (3+ numbers) or "<marker> [Index]".
	 */
	static bool ResolveTeleportArgs(UWorld* World, const TArray<FString>& Args, FLureTeleportTarget& OutTarget, FString& OutError);

	/** The floor point under Feet (ECC_Pawn line trace, see FloorProbeUp/Down), or Feet if there is none. */
	static FVector ProbeFloor(const UWorld* World, const FVector& Feet, const AActor* Ignore);

	/**
	 *  Moves Pawn so its feet stand on the floor at FeetLocation (authority only), stops its movement and, if
	 *  ViewRotation is set, turns its controller (ClientSetRotation, so a remote client's view follows).
	 *  OutFeet = where the feet ended up.
	 */
	static bool TeleportPawn(APawn* Pawn, const FVector& FeetLocation, const TOptional<FRotator>& ViewRotation, FString& OutError, FVector* OutFeet = nullptr);

	// ---- Stance ----

	/** "Stand", "Crouch" or "Prone" (any ELureStance name, case-insensitive). */
	static bool ParseStance(const FString& Text, ELureStance& OutStance);

	/** The ELureStance names, comma separated (help text). */
	static FString StanceNames();

	/** RequestStance on a locally controlled character (what the stance keys do). */
	static bool SetStance(ALurePlayerCharacter* Character, ELureStance Stance, FString& OutError);

	// ---- Fish ----

	/** <SpeciesId> then, in any order: a rarity id ("-", "any" or "random" = roll), an integer seed, Mods=A,B, Weight=0..1, Player=<id>. */
	static bool ParseGiveFishArgs(const TArray<FString>& Args, FLureGiveFishArgs& OutArgs, FString& OutError);

	/** Checks the ids against the tables (errors list the valid ids) and rolls with FFishRoll::Roll. */
	static bool GiveFish(const FFishTables& Tables, const FLureGiveFishArgs& Args, FFishInstance& OutFish, FString& OutError);

	// ---- Console entry points (what the registered commands run; public for tests) ----

	static bool RunTeleport(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar);
	static bool RunSetStance(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar);

	/** TablesOverride: tests pass tables built from data/tables/*.json; null = UFishSettings::LoadTables. */
	static bool RunGiveFish(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar, const FFishTables* TablesOverride = nullptr, FFishInstance* OutFish = nullptr);
};

#endif // !UE_BUILD_SHIPPING

// Lure: fishing data types and pure rules (T-006). Tuning lives in data/tables/DT_Fishing.csv -> /Game/Data/DT_Fishing.
// Decisions: docs/specs/fishing-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/NetSerialization.h"
#include "GameplayTagContainer.h"
#include "Math/RandomStream.h"
#include "Character/LureMovementTypes.h"
#include "Fish/FishTypes.h"
#include "Fish/FishInstance.h"
#include "FishingTypes.generated.h"

class AActor;
struct FFishTables;

/** Log category for casting, bites, hooks, spots and the fishing tables. */
DECLARE_LOG_CATEGORY_EXTERN(LogLureFishing, Log, All);

/** Where the line is (server-decided, replicated). Charging is client-local and not a state here. */
UENUM(BlueprintType)
enum class ELureFishingState : uint8
{
	/** No line out. */
	Idle,
	/** The bobber is flying to its landing point. */
	Casting,
	/** The bobber floats (or lies on land); nibbles may tip it; a bite comes when the server decides. */
	Waiting,
	/** A fish bites: the hook window is open (the bobber is pulled under). */
	Biting,
	/** A fish is on the line: the server runs the reel fight (T-007) until it is landed, snaps the line or throws the hook. */
	Hooked
};

/** The last thing that happened (for the placeholder HUD text, sounds and later systems). */
UENUM(BlueprintType)
enum class ELureFishingResult : uint8
{
	None,
	/** The server refused a cast (ResultReason says why). */
	Refused,
	/** The hook window closed: the bite (and its rolled fish) is lost. */
	Missed,
	/** Hooked in time. */
	Hooked,
	/** The fish was landed (placeholder until the reel fight). */
	Landed,
	/** The line came in with nothing on it (player reeled in, or a rule cancelled it: ResultReason). */
	ReeledIn,
	/** A hooked fish was lost because the line had to come in (ResultReason). */
	Lost,
	/** An early hook scared the fish (EarlyHook = Spook): the next bite comes later. */
	Spooked,
	/** Reel fight (T-007): the line broke (tension over its strength too long, or the fish took the whole spool). */
	Snapped,
	/** Reel fight (T-007): the line stayed slack too long and the fish threw the hook. */
	ThrewHook
};

/** Why a cast is refused or a line comes in on its own. */
UENUM(BlueprintType)
enum class ELureCastBlock : uint8
{
	None,
	/** The rod is not in hand. */
	NoRod,
	/** In the water (swimming). */
	Swimming,
	/** The movement row's CanFish is false (sprinting). */
	Sprinting,
	/** In the air. */
	InAir,
	/** Moving in a pose where the rod is tucked (prone crawl). */
	RodTucked,
	/** A line is already out. */
	Busy,
	/** The bobber is farther than MaxLineLength. */
	TooFar
};

/** What an early hook (a press while waiting, before a bite) does. */
UENUM(BlueprintType)
enum class ELureEarlyHookRule : uint8
{
	/** Reel the line in (the default: press to reel in and recast). */
	ReelIn,
	/** Nothing happens. */
	Ignore,
	/** The fish is scared: the bite is pushed back by SpookDelay. */
	Spook
};

/**
 *  One fishing profile of DT_Fishing (source: data/tables/DT_Fishing.csv; row "Default"; gear can pick other rows later).
 *  Units: cm, cm/s, seconds, degrees. The column names in the CSV are the property names below.
 */
USTRUCT(BlueprintType)
struct FLureFishingRow : public FTableRowBase
{
	GENERATED_BODY()

	// ---- Cast ----

	/** Seconds of holding for a full charge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0.05"))
	float ChargeTime = 1.2f;

	/** Distance curve: Min + (Max - Min) * Charge ^ ChargeExponent (1 = linear). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0.1"))
	float ChargeExponent = 1.f;

	/** Cast distance at no charge, cm (horizontal, from the eye). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0"))
	float MinCastDistance = 300.f;

	/** Cast distance at full charge, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0"))
	float MaxCastDistance = 1800.f;

	/** Bobber flight speed, cm/s (flight time = distance / speed, clamped). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="1"))
	float CastSpeed = 1600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0.05"))
	float CastFlightTimeMin = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0.05"))
	float CastFlightTimeMax = 1.3f;

	/** Arc apex above the straight flight line, as a share of the cast distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0"))
	float CastArcHeightRatio = 0.2f;

	/** The line comes in if the player gets farther than this from the bobber, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast", meta=(ClampMin="0"))
	float MaxLineLength = 2600.f;

	// ---- Bite ----

	/** Seconds from landing to the bite: random in [BiteWaitMin, BiteWaitMax]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float BiteWaitMin = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float BiteWaitMax = 10.f;

	/** Nibbles before a bite (the bobber tips so its white half shows): random count in [NibblesMin, NibblesMax]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	int32 NibblesMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	int32 NibblesMax = 2;

	/** Seconds between nibbles (they come just before the bite). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0.05"))
	float NibbleInterval = 1.1f;

	/** Seconds one nibble tip lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0.05"))
	float NibbleDuration = 0.45f;

	/** Seconds the hook window stays open after the bite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0.05"))
	float HookWindow = 0.8f;

	/** Extra seconds the server allows a REMOTE player's hook (network delay). The host and standalone get none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float HookLatencyGrace = 0.15f;

	/** A miss ends the cast (true) or the bobber stays and a new bite may come after RebiteWaitMin..Max (false). The missed fish is always lost. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite")
	bool MissEndsCast = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float RebiteWaitMin = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float RebiteWaitMax = 12.f;

	/** What a hook press does before a bite (including during nibbles). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite")
	ELureEarlyHookRule EarlyHook = ELureEarlyHookRule::ReelIn;

	/** Seconds an early hook pushes the bite back (EarlyHook = Spook). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float SpookDelay = 3.f;

	/** Debug/tests only: > 0 skips the reel fight and lands the fish this many seconds after the hook (the T-006 placeholder). 0 = the reel fight (T-007). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float AutoLandDelay = 0.f;

	/** Seconds of waiting before the HUD says nothing bites here (no spot in range, or no species fits). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bite", meta=(ClampMin="0"))
	float NoBiteHintDelay = 8.f;

	// ---- Bobber (cosmetic, every machine) ----

	/** Readability scale of SM_Bobber (ART_STYLE: 4.5x; the mesh is real size). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bobber", meta=(ClampMin="0.1"))
	float BobberScale = 4.5f;

	/** Idle bob on the water, cm (up and down). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bobber", meta=(ClampMin="0"))
	float BobberBobAmplitude = 1.2f;

	/** Idle bobs per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bobber", meta=(ClampMin="0"))
	float BobberBobFrequency = 0.5f;

	/** Idle wobble, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bobber", meta=(ClampMin="0"))
	float BobberTiltDeg = 4.f;

	/** How far a nibble tips the bobber, degrees (the white half shows; B-S4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bobber", meta=(ClampMin="0"))
	float NibbleTiltDeg = 65.f;

	/** How far the bite pulls the bobber under, cm (>= its scaled height, so the red top disappears; B-S4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bobber", meta=(ClampMin="0"))
	float BiteDipDepth = 30.f;

	/** Tugs per second while the fish bites. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bobber", meta=(ClampMin="0"))
	float BiteDipRate = 3.f;

	// ---- Line (cosmetic) ----

	/** Line width in pixels on a 1920x1080 screen at the current FOV (designer B-S3: >= 2), at every distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line", meta=(ClampMin="0.1"))
	float LinePixelWidth = 2.5f;

	/** Thinnest line in the world, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line", meta=(ClampMin="0"))
	float LineMinWidth = 0.15f;

	/** Sag of a slack line, as a share of its length. The line goes taut when a fish bites or is on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line", meta=(ClampMin="0"))
	float LineSag = 0.06f;

	/** Line segments (spline meshes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line", meta=(ClampMin="1", ClampMax="64"))
	int32 LineSegments = 12;

	// ---- Feel ----

	/** Controller rumble on a bite (0..1) and its length, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feel", meta=(ClampMin="0", ClampMax="1"))
	float BiteRumbleIntensity = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feel", meta=(ClampMin="0"))
	float BiteRumbleDuration = 0.3f;

	/** Placeholder cast motion (until a cast montage exists): the rod tips back with the charge, whips forward on release. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feel", meta=(ClampMin="0"))
	float CastSwingBackDeg = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feel", meta=(ClampMin="0"))
	float CastSwingForwardDeg = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feel", meta=(ClampMin="0.01"))
	float CastSwingTime = 0.35f;

	/** Runtime sanity check (finite, ranges ordered, positive times). Returns false and a reason if the row is unusable. */
	bool Validate(FString& OutProblem) const;
};

/** The replicated state of one player's line. Written only by the server; clients draw the bobber and line from it. */
USTRUCT(BlueprintType)
struct FLureFishingNetState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	ELureFishingState State = ELureFishingState::Idle;

	/** +1 per cast (wraps). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	uint8 CastId = 0;

	/** Where the bobber leaves (about the rod tip), world. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	FVector_NetQuantize10 CastOrigin;

	/** Where the bobber rests: on the water surface (its pivot is the waterline) or on the ground. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	FVector_NetQuantize10 BobberRest;

	/** Seconds of flight. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	float FlightTime = 0.f;

	/** Server time (GetFishingTime) when the current state began. For Biting: the bite. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	double StateStartTime = 0.0;

	/** The bobber landed on water (false = on land or a dock: nothing bites). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	bool bOnWater = false;

	/** Nothing can bite here now (no fishing spot in range, or no species fits the spot, time or bait). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	bool bNoFishHere = false;

	/** The fishing spot the bobber is in (marker tag Spot=...); None = none. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	FName SpotId;

	/** +1 per nibble; clients tip the bobber when it changes. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	uint8 NibbleId = 0;

	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	double LastNibbleTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	ELureFishingResult LastResult = ELureFishingResult::None;

	/** +1 per result (so the same result twice still shows). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	uint8 ResultId = 0;

	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	double ResultTime = 0.0;

	/** Why a cast was refused or the line came in on its own (None = the player's choice). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing")
	ELureCastBlock ResultReason = ELureCastBlock::None;
};

/** A fishing spot read from a marker actor (L_PalmKey.md section 11: tags Lure.FishingSpot + Key=Value). */
USTRUCT(BlueprintType)
struct FLureFishingSpot
{
	GENERATED_BODY()

	/** Spot= (e.g. dock_end). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	FName SpotId;

	/** Name= (e.g. Dock End). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	FString DisplayName;

	/** Habitat= (e.g. Habitat.Shore.Cove). Feeds the bite picker. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	FGameplayTag HabitatTag;

	/** Region= (e.g. Region.Tropical.PalmKey). Feeds the bite picker and modifier conditions. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	FGameplayTag RegionTag;

	/** Radius= on the water around the marker, cm. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	float Radius = 0.f;

	/** Luck= added to the rarity roll's Luck (fish-system-rules stage 3). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	float Luck = 0.f;

	/** Hours= (info only: bites are decided by each species' time windows). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	TArray<FFishTimeWindow> Hours;

	/** Levels= (info only). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	int32 LevelMin = 0;

	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	int32 LevelMax = 0;

	/** Danger= (info only; threats read it later). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	FName Danger;

	/** CastFrom= where players stand, world. */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	FVector CastFrom = FVector::ZeroVector;

	/** The marker's location (center of the spot on the water). */
	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Fishing Spot")
	TWeakObjectPtr<AActor> Marker;

	bool IsValid() const { return Radius > 0.f; }
};

/** What besides the spot feeds a bite: time, weather, bait and gear luck (T-011/T-013 fill these later). */
struct FLureFishingEnvironment
{
	float TimeOfDayHours = 12.f;
	FGameplayTag WeatherTag;
	FGameplayTag BaitTag;
	/** Added to the spot's luck. */
	float GearLuck = 0.f;
	/** Used when there is no spot, or the spot has no Region=. */
	FGameplayTag DefaultRegionTag;
	/** Habitat used when no spot is in range (None = nothing bites off-spot; see docs/specs/fishing-rules.md). */
	FGameplayTag OffSpotHabitatTag;
};

/** What the cast rules look at. */
struct FLureCastConditions
{
	bool bHasRod = true;
	bool bSwimming = false;
	bool bFalling = false;
	/** Horizontal speed, cm/s. */
	float Speed2D = 0.f;
	/** A line is out (for casting only). */
	bool bLineOut = false;
};

/** Pure fishing rules (no world, no RNG of their own), so they are unit-testable. */
struct FLureFishingRules
{
	/** Charge 0..1 after holding HeldSeconds: HeldSeconds / ChargeTime, clamped (NaN = 0). */
	static float ChargeFromHoldTime(const FLureFishingRow& Row, float HeldSeconds);

	/** Horizontal cast distance, cm: Min + (Max - Min) * clamp(Charge) ^ ChargeExponent (NaN charge = 0). */
	static float CastDistance(const FLureFishingRow& Row, float Charge01);

	/** Flight seconds: clamp(Distance / CastSpeed, CastFlightTimeMin, CastFlightTimeMax). */
	static float CastFlightTime(const FLureFishingRow& Row, float Distance);

	/** Point on the flight arc at Alpha 0..1: a straight line From -> To plus a parabola of ArcHeight at the middle. */
	static FVector CastArcPoint(const FVector& From, const FVector& To, float ArcHeight, float Alpha);

	/** Last server time a hook counts for a bite that began at BiteStart: BiteStart + HookWindow + Grace. */
	static double HookDeadline(const FLureFishingRow& Row, double BiteStart, float Grace);

	/** True if Now is inside [BiteStart, HookDeadline]. */
	static bool IsInHookWindow(const FLureFishingRow& Row, double BiteStart, double Now, float Grace);

	/** Why a cast can't start now (None = it can). Order: NoRod, Busy, Swimming, InAir, Sprinting (row CanFish), RodTucked. */
	static ELureCastBlock GetCastBlock(const FLureCastConditions& Conditions, const FLureMovementRow& Row);

	/** Why a line that is out must come in now (None = it stays): NoRod, Swimming, Sprinting, RodTucked, TooFar. Jumping is fine. */
	static ELureCastBlock GetLineCancel(const FLureCastConditions& Conditions, const FLureMovementRow& Row, const FLureFishingRow& Fishing, float BobberDistance2D);

	/** Moving (Speed2D > RodMoveSpeedIn) in a row whose moving pose tucks the rod. */
	static bool IsRodTuckedByMotion(const FLureMovementRow& Row, float Speed2D);

	/** Seconds until the bite: random in [BiteWaitMin, BiteWaitMax], or [RebiteWaitMin, RebiteWaitMax] after a miss. */
	static float RandomBiteWait(const FLureFishingRow& Row, FRandomStream& Rng, bool bAfterMiss);

	/** Nibble times in seconds after the wait began, ascending, all before Wait (count random in [NibblesMin, NibblesMax]). */
	static TArray<float> NibbleTimes(const FLureFishingRow& Row, FRandomStream& Rng, float Wait);

	/** The fish-roll context of a bite: habitat and region from the spot (or the off-spot habitat), Luck = spot luck + gear luck. */
	static FFishRollContext MakeRollContext(const FLureFishingSpot* Spot, const FLureFishingEnvironment& Environment, int32 Seed);

	/** Can anything bite with this context (a spot, or an off-spot habitat)? */
	static bool CanHaveBites(const FLureFishingSpot* Spot, const FLureFishingEnvironment& Environment);

	/** The bite: FFishRoll::PickSpecies then FFishRoll::Roll (the one roll pipeline). False = nothing bites. */
	static bool DecideBite(const FFishTables& Tables, const FFishRollContext& Context, FFishInstance& OutFish);

	/** World width (cm) that shows as PixelWidth pixels at DistanceCm on a ReferenceScreenWidth-wide view with HorizontalFovDeg; >= MinWidth. */
	static float LineWidthAtDistance(float PixelWidth, float DistanceCm, float HorizontalFovDeg, float ReferenceScreenWidth, float MinWidth);

	/** Pixels a WidthCm line covers at DistanceCm (inverse of LineWidthAtDistance without the minimum). */
	static float LinePixelsAtDistance(float WidthCm, float DistanceCm, float HorizontalFovDeg, float ReferenceScreenWidth);

	/** Points of a sagging line Start -> End (Segments + 1 points): straight plus a parabolic sag of Sag * length at the middle. */
	static void ComputeLinePoints(const FVector& Start, const FVector& End, float Sag, int32 Segments, TArray<FVector>& OutPoints);

	/** The built-in profile used when DT_Fishing is missing or its row is invalid (same values as the shipped CSV). */
	static FLureFishingRow GetFallbackRow();

	/** Substring of the one warning logged when the fishing table falls back. */
	static const TCHAR* FallbackWarningMarker;
};

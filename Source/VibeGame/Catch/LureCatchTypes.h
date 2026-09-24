// Lure: catch handling types (T-030): the caught-fish record with its freshness, the catch data rows, the pure freshness
// rules, the hanging-fish pendulum and the save structs. Rules: docs/specs/catch-handling-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/NetSerialization.h"
#include "Fish/FishInstance.h"
#include "Progression/LureProgressionTypes.h"
#include "LureCatchTypes.generated.h"

class APawn;
class UAnimSequenceBase;
class UObject;

/** Catch handling log: data problems are Warnings (never check/ensure); a client calling a server-only function is a Warning. */
DECLARE_LOG_CATEGORY_EXTERN(LogLureCatch, Log, All);

/** How a carryable item is held right now */
UENUM(BlueprintType)
enum class ELureHoldMode : uint8
{
	/** Free: standing or lying in the world */
	None,
	/** In a player's hand(s) */
	Hand,
	/** Hanging on a player's hook (fish only) */
	Hook
};

/** How many hands an item takes */
UENUM(BlueprintType)
enum class ELureHoldKind : uint8
{
	OneHand,
	TwoHands
};

/**
 *  The freshness of one catch, as an anchor: Exposure(Now) = ExposedSeconds + Rate x (Now - AnchorTime), in server world
 *  time. The rate changes only on events (into or out of a cooler, lid open/close, load), so nothing ticks and every machine
 *  can show the live value. Time zero is the landing.
 */
USTRUCT(BlueprintType)
struct FLureFreshnessState
{
	GENERATED_BODY()

	/** Seconds of spoiling up to AnchorTime (rate-weighted). The only saved field. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Freshness")
	float ExposedSeconds = 0.0f;

	/** Server world time of the anchor. Not saved: a load re-anchors at the load time. */
	UPROPERTY(BlueprintReadOnly, Category="Freshness")
	double AnchorTime = 0.0;

	/** Spoiling seconds per second from AnchorTime on: 1 outside a cooler, the cooler row's rate inside */
	UPROPERTY(BlueprintReadOnly, Category="Freshness")
	float Rate = 1.0f;

	/** Exposure at Now: never below ExposedSeconds (a Now before the anchor counts as the anchor); NaN-safe */
	float GetExposure(double Now) const;

	/** Samples the exposure at Now and continues from there at NewRate (negative or NaN = 0) */
	void SetRate(float NewRate, double Now);

	/** Fresh from Now (the landing) at InRate */
	static FLureFreshnessState StartAt(double Now, float InRate = 1.0f);
};

/** A catch with its freshness: the record that moves between the hook, a hand, the ground, coolers and the sell counter. */
USTRUCT(BlueprintType)
struct FLureCaughtFish
{
	GENERATED_BODY()

	/** The roll pipeline's record (never re-rolled or re-priced) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Catch")
	FFishInstance Fish;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Catch")
	FLureFreshnessState Freshness;

	bool IsValid() const { return Fish.IsValid(); }

	/** A fish landed at Now: fresh, spoiling at rate 1 */
	static FLureCaughtFish Landed(const FFishInstance& InFish, double Now);
};

/**
 *  DT_Freshness row (source data/tables/DT_Freshness.csv, row struct LureFreshnessRow): how fast a fish spoils outside a
 *  closed cooler. The row named like the fish's species wins, else the settings' default row ("Default").
 */
USTRUCT(BlueprintType)
struct FLureFreshnessRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Seconds of exposure with no loss at all, >= 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Freshness", meta=(ClampMin="0", Units="s"))
	float GraceSeconds = 120.0f;

	/** Seconds from the end of the grace to the lowest value, > 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Freshness", meta=(ClampMin="0.1", Units="s"))
	float SpoilSeconds = 600.0f;

	/** Shape of the loss: freshness = 1 - x ^ CurveExponent (1 = linear; > 1 slow then fast; < 1 fast then slow), > 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Freshness", meta=(ClampMin="0.05"))
	float CurveExponent = 1.0f;

	/** Share of the value a fully spoiled fish still sells for, in [0, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Freshness", meta=(ClampMin="0", ClampMax="1"))
	float MinValueShare = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Freshness", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;

	/** False (and why) for a row the rules can't use */
	bool Validate(FString& OutProblem) const;

	/** The row used when DT_Freshness or its default row is missing: the same numbers as the shipped Default row */
	static FLureFreshnessRow GetFallbackRow();
};

/**
 *  DT_Catch row (source data/tables/DT_Catch.csv, row struct LureCatchRow, row "Default"): the feel of catch handling.
 *  Units: cm, seconds, degrees.
 */
USTRUCT(BlueprintType)
struct FLureCatchRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Line from the rod tip to a landed fish's mouth while it hangs, cm (> 0) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hanging", meta=(ClampMin="1", Units="cm"))
	float HangLineLength = 40.0f;

	/** How fast the hanging fish's swing dies down, 1/s (>= 0; 0 = swings on) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hanging", meta=(ClampMin="0"))
	float HangDamping = 1.2f;

	/** How far a fish or a cooler can be used from, cm (pawn location to the item) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reach", meta=(ClampMin="10", Units="cm"))
	float ReachDistance = 250.0f;

	/** How far off the view a target may be and still be the one you use, degrees */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reach", meta=(ClampMin="0.5", ClampMax="90"))
	float FocusAngleDeg = 20.0f;

	/** A dropped fish lands this far in front of the eye, horizontally, cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Drop", meta=(ClampMin="0", Units="cm"))
	float DropForward = 60.0f;

	/** Seconds a dropped or placed item takes to fly to its rest point (cosmetic) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Drop", meta=(ClampMin="0", Units="s"))
	float DropArcTime = 0.35f;

	/** A cooler is put down this far in front of the player (capsule center, horizontally), cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cooler", meta=(ClampMin="10", Units="cm"))
	float PutDownDistance = 80.0f;

	/** How far below the put-down point the floor is searched, cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cooler", meta=(ClampMin="10", Units="cm"))
	float PutDownMaxFall = 300.0f;

	/** Open lid angle (relative pitch at the hinge), degrees */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cooler", meta=(ClampMin="0", ClampMax="180"))
	float LidOpenPitch = 100.0f;

	/** Seconds the lid takes to open or close (cosmetic) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cooler", meta=(ClampMin="0", Units="s"))
	float LidOpenTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catch", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;

	bool Validate(FString& OutProblem) const;

	/** The row used when DT_Catch or its row is missing: the same numbers as the shipped Default row */
	static FLureCatchRow GetFallbackRow();
};

/** Where one fish lies inside an open cooler (cosmetic), relative to the body mesh's "Contents" socket */
USTRUCT(BlueprintType)
struct FLureCoolerDisplaySlot
{
	GENERATED_BODY()

	/** cm, relative to the Contents socket (cooler axes: +X front, +Y along the long side, +Z up) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Display")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Display")
	FRotator Rotation = FRotator::ZeroRotator;
};

/**
 *  DT_CoolerDisplay row (source data/tables/DT_CoolerDisplay.json, row struct LureCoolerDisplayRow; row name = the DT_Cooler
 *  row it dresses): how the fish inside an OPEN cooler are shown. Cosmetic only (no physics); the contents stay records.
 *  A cooler type without a row uses the built-in layout (GetFallbackRow).
 */
USTRUCT(BlueprintType)
struct FLureCoolerDisplayRow : public FTableRowBase
{
	GENERATED_BODY()

	/** One per shown fish, bottom of the pile first; with more fish than slots the top ones (the last put in) are shown */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Display")
	TArray<FLureCoolerDisplaySlot> Slots;

	/** The pose the shown fish hold (an A_Fish_* clip on SKEL_Fish, e.g. A_Fish_Curled; additive clips go on the fish's own ref pose). None = straight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Display", meta=(DataTableImportOptional))
	TSoftObjectPtr<UAnimSequenceBase> FishPose;

	/** Seconds into FishPose the fish hold, >= 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Display", meta=(ClampMin="0", DataTableImportOptional))
	float PoseTime = 0.0f;

	/** Shown fish are drawn at their weight's size but at most this scale (1 = reference size), so a big catch fits, > 0.
	 *  The record keeps its real weight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Display", meta=(ClampMin="0.05"))
	float MaxFishScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Display", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;

	bool Validate(FString& OutProblem) const;

	/** Built-in layout (no table or no row for the cooler type): 4 straight fish, two layers of two, drawn at 0.6 scale */
	static FLureCoolerDisplayRow GetFallbackRow();
};

/** Pure freshness rules (no world). Formulas: docs/specs/catch-handling-rules.md "Freshness". */
struct FLureFreshness
{
	/** x = clamp((Exposure - Grace) / Spoil, 0, 1); freshness = 1 - x ^ CurveExponent. 1 during the grace; NaN-safe (NaN = fresh). */
	static float GetFreshness01(const FLureFreshnessRow& Row, float ExposureSeconds);

	/** MinValueShare + (1 - MinValueShare) x freshness, in [MinValueShare, 1] */
	static float GetValueShare(const FLureFreshnessRow& Row, float ExposureSeconds);

	/** What the fish is worth now at a x1 buyer: max(1, round-half-up(Fish.Value x ValueShare)); 0 for an invalid fish or Value <= 0 */
	static int32 GetCurrentValue(const FFishInstance& Fish, float ValueShare);

	/** The sale price: max(1, round-half-up(Fish.Value x ValueShare x SellMultiplier)) (FLureProgressionRules::GetSellPrice with the share) */
	static int32 GetSellPrice(const FFishInstance& Fish, float ValueShare, float SellMultiplier);

	/**
	 *  The row for SpeciesId: the row named like the species, else DefaultRowName, else the built-in row (bOutFallback true).
	 *  A row that fails Validate counts as missing.
	 */
	static FLureFreshnessRow FindRow(const UDataTable* Table, FName SpeciesId, FName DefaultRowName, bool* bOutFallback = nullptr);

	/** Server world time (what every anchor uses): the game state's synchronized clock, else the world's time; 0 without a world */
	static double GetServerTime(const UObject* WorldContext);
};

/**
 *  The hanging fish's swing: a damped pendulum of fixed length, position-based (the point is moved, then pulled back onto the
 *  sphere around the pivot), so a moving pivot (the player walks, turns, the rod bobs) swings the fish naturally. Pure.
 *  Each machine simulates its own swing (cosmetic). T-032 replaces this with the physics line's end.
 */
struct FLureHangPendulum
{
	/** The hook end (the fish's mouth), world */
	FVector Bob = FVector::ZeroVector;

	/** cm/s */
	FVector Velocity = FVector::ZeroVector;

	bool bInitialized = false;

	/** Hangs still, straight below Pivot */
	void Reset(const FVector& Pivot, float Length);

	/**
	 *  Advances DeltaSeconds (split in steps of at most 1/60 s, at most 8): damping exp(-Damping x dt), gravity GravityZ
	 *  (cm/s2, negative = down), then the length constraint. A pivot more than 4 lengths away (a teleport) resets it; a
	 *  point more than half a length off the line's reach (a set start point, a hitch) is moved onto it first without a kick.
	 */
	void Step(const FVector& Pivot, float Length, float GravityZ, float Damping, float DeltaSeconds);
};

/** Where a free item rests, replicated (T-030 items): clients fly the mesh from From to Location when PlaceId changes. */
USTRUCT(BlueprintType)
struct FLureItemPlacement
{
	GENERATED_BODY()

	/** Rest point = the actor location (the gameplay truth at once) */
	UPROPERTY(BlueprintReadOnly, Category="Item")
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Item")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Where the flight starts (the hand, the hook) */
	UPROPERTY(BlueprintReadOnly, Category="Item")
	FVector_NetQuantize10 From = FVector::ZeroVector;

	/** Changes on every placement */
	UPROPERTY(BlueprintReadOnly, Category="Item")
	uint8 PlaceId = 0;

	/** Fly from From (a drop), or appear at Location (a spawn, a load) */
	UPROPERTY(BlueprintReadOnly, Category="Item")
	bool bAnimate = false;
};

/** Who holds an item and how, replicated as one value so both change together. */
USTRUCT(BlueprintType)
struct FLureItemHold
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Item")
	TObjectPtr<APawn> Holder = nullptr;

	UPROPERTY(BlueprintReadOnly, Category="Item")
	ELureHoldMode Mode = ELureHoldMode::None;

	bool IsHeld() const { return Holder != nullptr && Mode != ELureHoldMode::None; }
};

/** One cooler in a save (T-019): identity, type, where it stands, the lid and every fish with its exposure at save time. */
USTRUCT(BlueprintType)
struct FLureCoolerSaveData
{
	GENERATED_BODY()

	/** Identity across saves and reconnects (a load updates the cooler with this guid if it is in the world) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Cooler")
	FGuid CoolerGuid;

	/** DT_Cooler row (None or an unknown row = the default row, fish kept) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Cooler")
	FName CoolerId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Cooler")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Cooler")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Cooler")
	bool bLidOpen = false;

	/** The fish inside, in order; Freshness.ExposedSeconds = the exposure at save time */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Cooler")
	TArray<FLureCaughtFish> Fish;

	/** This cooler was the automatic starter cooler (a save that has its own coolers replaces it) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Cooler")
	bool bStarter = false;
};

/** Everything of one player T-019 saves: progression (player state) plus the coolers they own (world). */
USTRUCT(BlueprintType)
struct FLurePlayerSaveData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Save")
	FLureProgressSaveData Progress;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Save")
	TArray<FLureCoolerSaveData> Coolers;
};

/** Data validation of the catch tables (reusable on editor reimport). Each returns the problems (empty = OK). */
struct FLureCatchData
{
	/** Row struct, at least one row, every row valid, DefaultRow (if not None) exists */
	static TArray<FString> ValidateFreshnessTable(const UDataTable* Table, FName DefaultRow = NAME_None);

	/** Row struct, at least one row, every row valid, Row (if not None) exists */
	static TArray<FString> ValidateCatchTable(const UDataTable* Table, FName Row = NAME_None);

	/** Row struct, every row valid; with CoolerTable: every display row names a DT_Cooler row (a typo would never be used) */
	static TArray<FString> ValidateCoolerDisplayTable(const UDataTable* Table, const UDataTable* CoolerTable = nullptr);
};

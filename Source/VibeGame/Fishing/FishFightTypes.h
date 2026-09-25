// Lure: reel fight, line tension and gear data (T-007). Rules: docs/specs/reel-fight-rules.md.
// Sources (the text is the truth; the editor-operator imports the assets to /Game/Data/):
//   data/tables/DT_Gear.csv          -> /Game/Data/DT_Gear          (FLureGearRow)          rods, lines, hooks/bait
//   data/tables/DT_FightPattern.json -> /Game/Data/DT_FightPattern  (FLureFightPatternRow)  fish fight patterns (species FightPatternId)
//   data/tables/DT_FishFight.csv     -> /Game/Data/DT_FishFight     (FLureFishFightRow)     fight tuning (row Default)
// A new rod, line, hook/bait or fight pattern is a new row. Missing tables or rows use built-in data with one warning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "FishFightTypes.generated.h"

class UDataTable;

/** Which gear slot a DT_Gear row fills. */
UENUM(BlueprintType)
enum class ELureGearSlot : uint8
{
	Rod,
	Line,
	/** Hook and bait together (which species bite, how securely they stay hooked). */
	Hook
};

/**
 *  Which way the hooked fish swims across the line right now (T-028): its move's Side share x its random side, when that
 *  share is at least DT_FishFight SideMinShare. The player steers the rod the other way to turn it.
 */
UENUM(BlueprintType)
enum class ELureFightRunSide : uint8
{
	/** Straight away, toward you, down, resting or tired: no side to steer against. */
	None,
	Left,
	Right
};

/** How a reel fight ended (None = still on). */
UENUM(BlueprintType)
enum class ELureFightOutcome : uint8
{
	None,
	/** Reeled in to LandDistance: the fish is caught. */
	Landed,
	/** Tension stayed above the line's strength for longer than SnapGraceTime. */
	Snapped,
	/** The fish took more line than the spool holds (the line breaks at the spool). */
	Spooled,
	/** The line stayed slack for longer than SlackGraceTime x HookSecurity: the fish shook the hook. */
	ThrewHook
};

/**
 *  DT_Gear row: one rod, line or hook/bait. Row name = item id (the loadout stores it). Only the columns of the row's Slot
 *  count; the others are 0 / None in the CSV. Tension units are abstract "kg of pull" shared with the fish (DT_FishFight).
 */
USTRUCT(BlueprintType)
struct FLureGearRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gear")
	ELureGearSlot Slot = ELureGearSlot::Rod;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gear")
	FText DisplayName;

	/** Shop price in coins (the shop is T-012). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gear", meta=(ClampMin="0"))
	int32 Price = 0;

	// ---- Rod ----

	/** How hard you can pull: reeling gains line only while the fish pulls less than this, and cranking loads the line with ReelLoad x RodPower. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod", meta=(ClampMin="0"))
	float RodPower = 8.f;

	/** Line gained per second while reeling a fish that does not pull back, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod", meta=(ClampMin="0"))
	float ReelSpeed = 180.f;

	/** While you are NOT reeling, the drag lets line out at this tension (letting the fish run keeps the tension at or below it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod", meta=(ClampMin="0"))
	float Drag = 5.f;

	/** Multiplies the cast distance of DT_Fishing (Min/MaxCastDistance). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod", meta=(ClampMin="0"))
	float CastDistanceMultiplier = 1.f;

	// ---- Line ----

	/** Tension the line holds. Above it for longer than DT_FishFight SnapGraceTime, the line snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line", meta=(ClampMin="0"))
	float LineStrength = 10.f;

	/** Line on the spool, cm. A fish that runs farther than this takes it all (the line breaks). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line", meta=(ClampMin="0"))
	float SpoolLength = 4000.f;

	// ---- Hook and bait ----

	/** Multiplies DT_FishFight SlackGraceTime: how long the line may stay slack before the fish throws the hook. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hook", meta=(ClampMin="0"))
	float HookSecurity = 1.f;

	/** Bait on the hook (decides which species bite). None = the settings' DefaultBait. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hook", meta=(Categories="Bait,Hook"))
	FGameplayTag BaitTag;

	/** Rarity luck added to the fishing spot's Luck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hook", meta=(ClampMin="0"))
	float Luck = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gear", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;

	/** The columns of its Slot are finite and in range (rod: power, reel speed > 0; line: strength, spool > 0; hook: security > 0). */
	bool Validate(FString& OutProblem) const;
};

/** The equipped gear: DT_Gear row names per slot. Replicated by the fishing component; the shop (T-012) changes it on the server. */
USTRUCT(BlueprintType)
struct FLureGearLoadout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Gear")
	FName Rod;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Gear")
	FName Line;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Gear")
	FName Hook;

	FName Get(ELureGearSlot Slot) const;
	void Set(ELureGearSlot Slot, FName Id);
	bool operator==(const FLureGearLoadout& Other) const { return Rod == Other.Rod && Line == Other.Line && Hook == Other.Hook; }
	bool operator!=(const FLureGearLoadout& Other) const { return !(*this == Other); }
};

/** The numbers of an equipped loadout (every machine resolves it from DT_Gear). */
USTRUCT(BlueprintType)
struct FLureGearStats
{
	GENERATED_BODY()

	/** The items used (a slot whose row is missing uses the built-in starter item and its id is None). */
	UPROPERTY(BlueprintReadOnly, Category="Gear")
	FName RodId;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	FName LineId;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	FName HookId;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float RodPower = 8.f;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float ReelSpeed = 180.f;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float Drag = 5.f;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float CastDistanceMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float LineStrength = 10.f;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float SpoolLength = 4000.f;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float HookSecurity = 1.f;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	FGameplayTag BaitTag;

	UPROPERTY(BlueprintReadOnly, Category="Gear")
	float Luck = 0.f;

	/** At least one slot used a built-in item (missing table or row). */
	UPROPERTY(BlueprintReadOnly, Category="Gear")
	bool bUsedFallback = false;
};

/** Gear helpers (pure). */
struct FLureGear
{
	/** The DT_Gear row Id (null if the table, its row struct or the row is missing). */
	static const FLureGearRow* FindItem(const UDataTable* Table, FName Id);

	/** The built-in starter item of a slot (the shipped default loadout's rows; a test checks they match DT_Gear.csv). */
	static FLureGearRow GetFallbackItem(ELureGearSlot Slot);

	/** Copies a row's slot columns into Stats (and its id). */
	static void ApplyItem(FLureGearStats& Stats, const FLureGearRow& Row, FName Id);

	/**
	 *  The loadout's numbers. Per slot: the row Loadout names, if it exists, is valid and has that slot; otherwise the
	 *  built-in starter item (bUsedFallback, and a problem line). Table null = every slot built-in.
	 */
	static FLureGearStats Resolve(const UDataTable* Table, const FLureGearLoadout& Loadout, TArray<FString>* OutProblems = nullptr);

	/**
	 *  Caps the reel's drag at what the line holds: Stats.Drag = min(rod drag, LineStrength x DragLineCap) (DT_FishFight
	 *  DragLineCap; a non-finite or non-positive cap is ignored). The fishing component applies it to the resolved loadout.
	 */
	static void ApplyDragLineCap(FLureGearStats& Stats, float DragLineCap);

	/** Can Id be equipped in Slot (exists, valid, same slot)? OutProblem says why not. */
	static bool CanEquip(const UDataTable* Table, ELureGearSlot Slot, FName Id, FString* OutProblem = nullptr);
};

/**
 *  One move of a fight pattern: what the fish does for a few seconds. Every number is a multiplier of the fish's own
 *  pull and speed (from its FFishInstance stats), so a pattern works for any species and size.
 */
USTRUCT(BlueprintType)
struct FLureFightMove
{
	GENERATED_BODY()

	/** Unique in the pattern (e.g. Run). Replicated to clients for the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move")
	FName Id;

	/** Fish state text for the HUD (e.g. "running!"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move")
	FText Label;

	/** Pick weight when the next move is chosen (>= 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="0"))
	float Weight = 1.f;

	/** Extra pick weight per point of the fish's aggression stat (aggressive fish make these moves more often). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="0"))
	float AggressionWeight = 0.f;

	/** Seconds the move lasts: random in [DurationMin, DurationMax]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="0.01"))
	float DurationMin = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="0.01"))
	float DurationMax = 2.f;

	/** x the fish's base pull (0 = no pull: the line goes slack unless you reel). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="0"))
	float Pull = 1.f;

	/** x the fish's base swim speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="0"))
	float Speed = 1.f;

	/** Share of the swim that takes line: 1 = straight away, 0 = across, -1 = toward you (the line shortens). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="-1", ClampMax="1"))
	float Away = 1.f;

	/** Share of the swim that goes sideways (the fish swings around you; cosmetic). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="-1", ClampMax="1"))
	float Side = 0.f;

	/** Pick left or right at random each time the move starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move")
	bool RandomSide = false;

	/** Share of the swim that goes down (dives; cosmetic depth, -1 = rises). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move", meta=(ClampMin="-1", ClampMax="1"))
	float Down = 0.f;

	/** A pause: its duration shrinks for fish with a high DifficultyRating (DT_FishFight RestDifficultyExponent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Move")
	bool Rest = false;
};

/** DT_FightPattern row: how a kind of fish fights (dart, dive, run...). Row name = the species' FightPatternId. */
USTRUCT(BlueprintType)
struct FLureFightPatternRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern")
	FText DisplayName;

	/** First move right after the hook (None = a random pick). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern")
	FName OpeningMove;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern")
	TArray<FLureFightMove> Moves;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern", meta=(MultiLine=true, DataTableImportOptional))
	FString DevComment;

	/** At least one move with a positive weight, unique ids, ranges ordered, finite numbers, OpeningMove known. */
	bool Validate(FString& OutProblem) const;

	/** Index of the move Id, or INDEX_NONE. */
	int32 FindMove(FName MoveId) const;

	/** A generic pattern (swim, run, rest) for a missing table or an unknown/invalid FightPatternId. */
	static FLureFightPatternRow GetFallbackPattern();
};

/**
 *  DT_FishFight row (row Default): the fight's tuning. Which fish stats drive the fight are tags here, so stats stay data.
 *  Formulas: docs/specs/reel-fight-rules.md and FishFight.h.
 */
USTRUCT(BlueprintType)
struct FLureFishFightRow : public FTableRowBase
{
	GENERATED_BODY()

	// ---- Fish ----

	/** Stat for the fish's pull (Fish.Stat.Strength). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(Categories="Fish.Stat"))
	FGameplayTag StrengthStat;

	/** Stat for how long it fights (Fish.Stat.Stamina). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(Categories="Fish.Stat"))
	FGameplayTag StaminaStat;

	/** Stat for how fast it swims and runs (Fish.Stat.Speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(Categories="Fish.Stat"))
	FGameplayTag SpeedStat;

	/** Stat that makes the pattern's aggressive moves more likely (Fish.Stat.Aggression). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(Categories="Fish.Stat"))
	FGameplayTag AggressionStat;

	/** Base pull = strength stat x this (x the level multiplier), tension units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0"))
	float PullPerStrength = 0.25f;

	/** Base swim speed = speed stat x this, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0"))
	float SpeedPerStat = 5.f;

	/** Stamina pool = stamina stat x this, in tension-seconds (the fish tires by working against the line's tension). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0"))
	float StaminaPerStat = 18.f;

	/** Multiply the pull by UFishSettings::LevelScaling (fish level vs player level; fish above your level pull harder). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish")
	bool ApplyLevelScaling = true;

	/** Rest moves last DifficultyRating ^ -this as long (harder fish rest less). 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0"))
	float RestDifficultyExponent = 1.f;

	/** Share of its pull and speed a fish with no stamina left still has; an exhausted fish pulls this share and stops moving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0", ClampMax="1"))
	float TiredPull = 0.3f;

	/** Stamina share at which the fish is exhausted (no more moves). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0", ClampMax="1"))
	float ExhaustedStamina = 0.02f;

	/** Stamina share regained per second while the line is slack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish", meta=(ClampMin="0"))
	float StaminaRecovery = 0.04f;

	// ---- Line and tension ----

	/** Reeling: tension target = pull x ReelStrain + RodPower x ReelLoad (cranking against a pulling fish loads the line). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0"))
	float ReelStrain = 1.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0"))
	float ReelLoad = 0.15f;

	/** Not reeling: the drag starts giving line when the pull passes DragHold x Drag, and gives it freely at Drag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0", ClampMax="0.99"))
	float DragHold = 0.5f;

	/**
	 *  The reel's effective drag never exceeds what the line holds: Drag = min(rod Drag, LineStrength x DragLineCap), so a
	 *  stronger rod on a weak line never makes a fish harder. Optional column (default 0.9), in (0, 1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0.01", ClampMax="1", DataTableImportOptional))
	float DragLineCap = 0.9f;

	/** Seconds (time constant) the tension takes to rise / fall toward its target. 0 = instant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0"))
	float TensionRiseTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0"))
	float TensionFallTime = 0.25f;

	/** The line snaps when the tension stays above its strength for longer than this, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0"))
	float SnapGraceTime = 0.6f;

	/** The line is slack below SlackShare x the fish's base pull. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0"))
	float SlackShare = 0.35f;

	/** Seconds of slack line before the fish throws the hook (x the hook's HookSecurity). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="0.01"))
	float SlackGraceTime = 2.5f;

	/** Landed when the fish is this close to the player, cm (horizontal). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="1"))
	float LandDistance = 150.f;

	/** Fixed simulation steps per second (the server's fight is frame-rate independent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tension", meta=(ClampMin="10", ClampMax="240"))
	int32 SimRate = 60;

	// ---- Look (cosmetic, every machine) ----

	/** Deepest dive, cm, and how fast the fish comes back up when not diving, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0"))
	float MaxDepth = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0"))
	float DepthRecovery = 80.f;

	/** How far the fish swings around you, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0", ClampMax="170"))
	float MaxSideDeg = 50.f;

	/** The bobber sinks this share of the fish's depth (it stays readable). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0", ClampMax="1"))
	float DiveBobberShare = 0.1f;

	/** Placeholder rod bend: the rod tips toward the fish by this at full line strength, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0"))
	float RodTensionPitchDeg = 25.f;

	/** Rod shake while the tension is over the line's strength, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0"))
	float RodShakeDeg = 2.5f;

	/** The line is drawn fully taut from this share of its strength; below it, it sags (FLureFight::LineTension feeds the physics line). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0.01", ClampMax="1"))
	float TautTension = 0.3f;

	// ---- Rod steering (T-028; docs/specs/reel-fight-rules.md "Rod steering"). Optional columns: a missing one uses the default. ----
	// The rod input is RodPitch (-1 dipped toward the fish .. +1 pulled back/up) and RodYaw (-1 left .. +1 right of the line).
	// Level and centered at the default reel step is exactly the T-007 fight.

	/** Mouse/stick look degrees that take the rod from level to fully pulled back (RodPitch +1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod aim", meta=(ClampMin="1", DataTableImportOptional))
	float RodAimUpDeg = 35.f;

	/** Look degrees from level to fully dipped toward the fish (RodPitch -1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod aim", meta=(ClampMin="1", DataTableImportOptional))
	float RodAimDownDeg = 35.f;

	/** Look degrees from centered to fully left or right (RodYaw -1 / +1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod aim", meta=(ClampMin="1", DataTableImportOptional))
	float RodAimSideDeg = 45.f;

	/** Rod pulled fully back: the tension you apply and the rod's power x (1 + this). In between: linear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod aim", meta=(ClampMin="0", DataTableImportOptional))
	float PitchBackPressure = 0.3f;

	/** Rod fully dipped: the tension you apply and the rod's power x (1 - this). Below 1 (the rod keeps some power). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod aim", meta=(ClampMin="0", ClampMax="0.95", DataTableImportOptional))
	float PitchDipPressure = 0.5f;

	/**
	 *  Rod fully dipped: the rod's power (line gained, line held while you reel) x (1 - this), T-028b. More than PitchDipPressure: a
	 *  rod pointed at the fish relieves the line but barely works the fish, so dipping is relief, not a way to reel in (a dipped rod
	 *  at the fastest reel no longer matches skilled play). Below 1 (the rod keeps some power).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rod aim", meta=(ClampMin="0", ClampMax="0.95", DataTableImportOptional))
	float PitchDipPower = 0.8f;

	/** A move's |Side| share at or above this is a sideways run (left or right by its random side); below it, no side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Side pressure", meta=(ClampMin="0", ClampMax="1", DataTableImportOptional))
	float SideMinShare = 0.15f;

	/** Side score S (+1 = rod fully against the run, -1 = fully with it): the rod's power x (1 + S x this). Below 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Side pressure", meta=(ClampMin="0", ClampMax="0.95", DataTableImportOptional))
	float SideLeverage = 0.5f;

	/** Against the run (S > 0) the fish is turned: its move's clock runs (1 + S x this) times as fast (the run ends sooner). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Side pressure", meta=(ClampMin="0", DataTableImportOptional))
	float SideTurnRate = 1.f;

	/** Against the run: the fish's pull x (1 - S x this) while it is being turned. Below 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Side pressure", meta=(ClampMin="0", ClampMax="0.95", DataTableImportOptional))
	float SideTurnPull = 0.2f;

	/** Against the run: the fish spends stamina (1 + S x this) times as fast. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Side pressure", meta=(ClampMin="0", DataTableImportOptional))
	float SideDrain = 1.5f;

	/** Reel speed steps (mouse wheel / bumpers), evenly spaced from ReelSpeedMin to ReelSpeedMax times the rod's ReelSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reel speed", meta=(ClampMin="1", ClampMax="9", DataTableImportOptional))
	int32 ReelSteps = 3;

	/** The step a new player starts on, 1-based as the HUD shows it ("Reel 2/3"). Its speed must be 1 (= the T-007 reel; Validate checks). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reel speed", meta=(ClampMin="1", ClampMax="9", DataTableImportOptional))
	int32 ReelDefaultStep = 2;

	/** Speed of the slowest and the fastest step, x the rod's ReelSpeed (one step = speed 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reel speed", meta=(ClampMin="0.05", DataTableImportOptional))
	float ReelSpeedMin = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reel speed", meta=(ClampMin="0.05", DataTableImportOptional))
	float ReelSpeedMax = 1.5f;

	/** Cranking load: the rod's reeling load (RodPower x ReelLoad) x max(0, 1 + (step speed - 1) x this). Fast reeling adds tension. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reel speed", meta=(ClampMin="0", DataTableImportOptional))
	float ReelLoadPerSpeed = 1.5f;

	/** Owner's camera while a fish is on: eases toward the fish (plus a share of the rod's aim) with this time constant, s. 0 = snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0", DataTableImportOptional))
	float CameraFollowTime = 0.35f;

	/** Share of the rod's yaw and pitch (in look degrees) the camera turns with. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0", ClampMax="1", DataTableImportOptional))
	float CameraRodYawShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0", ClampMax="1", DataTableImportOptional))
	float CameraRodPitchShare = 0.35f;

	/** Placeholder rod turn at full aim, degrees (until ABP_FPArms plays the rod-aim aim offset; then the arms turn it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0", DataTableImportOptional))
	float RodAimLookPitchDeg = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0", DataTableImportOptional))
	float RodAimLookYawDeg = 25.f;

	/** The arms' rod aim (and other players' view of this rod) eases toward the aim with this time constant, s; back to level after the fight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Look", meta=(ClampMin="0", DataTableImportOptional))
	float RodAimBlendTime = 0.1f;

	/** Finite, in range, SimRate 10-240, the four stat tags set, the rod-steering columns in range. */
	bool Validate(FString& OutProblem) const;

	/** The T-028 rod-steering columns only (part of Validate). */
	bool ValidateRodSteering(FString& OutProblem) const;

	/** The built-in tuning (= the shipped DT_FishFight Default row; a test checks). */
	static FLureFishFightRow GetFallbackRow();
};

/** What clients get of the server's fight (replicated): the placeholder HUD and the visuals read it. */
USTRUCT(BlueprintType)
struct FLureFightNetState
{
	GENERATED_BODY()

	/** A fight is on (the fish is hooked and the server simulates it). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	bool bActive = false;

	/** +1 per fight (wraps). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	uint8 FightId = 0;

	/** The server's reel input (what it simulates with). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	bool bReeling = false;

	/** The fish has no stamina left (tired: easy to reel in). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	bool bExhausted = false;

	/** How the last fight ended (None while it is on). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	ELureFightOutcome Outcome = ELureFightOutcome::None;

	/** DT_FightPattern row and the current move id (None = tired). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	FName PatternId;

	UPROPERTY(BlueprintReadOnly, Category="Fight")
	FName MoveId;

	/** Line tension now, and the line's strength (tension units). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float Tension = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float LineStrength = 10.f;

	/** Below this the line is slack (SlackShare x the fish's base pull). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float SlackTension = 0.f;

	/** Fish stamina 0..1. */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float Stamina = 1.f;

	/** Horizontal distance of the fish from the player, cm, and the spool's length. */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float LineOut = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float SpoolLength = 0.f;

	/** Cosmetic: how deep the fish is, cm, and how far it swung around the player, degrees. */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float Depth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float SideDeg = 0.f;

	/** 0..1 toward a snap (time over the line's strength / SnapGraceTime) and toward a thrown hook (slack time / grace). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float SnapProgress = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float SlackProgress = 0.f;

	/**
	 *  T-028: the rod angle the server fights with (the owner's last input, -1..1; pitch + = pulled back/up, yaw + = right of
	 *  the line). Cosmetic for other players (their view of this player's rod and line); the owner uses its own live aim.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float RodPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Fight")
	float RodYaw = 0.f;

	/** The reel speed step the server fights with, 0-based (the HUD shows ReelStep + 1). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	uint8 ReelStep = 0;

	/** Which way the fish swims across the line right now (the HUD's "Fish runs LEFT: pull right"). */
	UPROPERTY(BlueprintReadOnly, Category="Fight")
	ELureFightRunSide RunSide = ELureFightRunSide::None;

	/** Tension / line strength (0 if no line). */
	float GetTension01() const { return LineStrength > 0.f ? Tension / LineStrength : 0.f; }
};

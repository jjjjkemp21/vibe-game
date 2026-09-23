// Lure: casting, bobber, bite and hook (T-006), reel fight, line tension and gear (T-007). Server-authoritative and replicated.
// Decisions: docs/specs/fishing-rules.md, docs/specs/reel-fight-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishFight.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"
#include "LureFishingComponent.generated.h"

class ACharacter;
class APawn;
class UCameraComponent;
class UDataTable;
class UEnhancedInputComponent;
class ULureFishingLineComponent;
class ULureFishingSettings;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class UAnimMontage;
class ULureFishingComponent;

/** Fired on the server (and on clients as their state replicates) when something happens to the line. Fish is empty unless hooked/landed/lost. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLureFishingEventSignature, ELureFishingResult, Result, const FFishInstance&, Fish);

/** Server only: a fish was landed (the cooler, T-010, stores the instance). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLureFishLandedSignature, ULureFishingComponent*, Fishing, const FFishInstance&, Fish);
DECLARE_MULTICAST_DELEGATE_TwoParams(FLureFishLandedNative, ULureFishingComponent* /*Fishing*/, const FFishInstance& /*Fish*/);

/**
 *  One player's fishing: rod, cast, bobber, bite and hook.
 *
 *  Authority: the owning client only asks (ServerCast / ServerHook / ServerReelIn). The server checks the rules (no fishing while
 *  sprinting, swimming, in the air or crawling prone), picks the landing point, finds the fishing spot, schedules nibbles and the
 *  bite, rolls the fish with the one roll pipeline (FFishRoll::PickSpecies + Roll, seeded from a server RNG), times the hook
 *  window and decides hit or miss. Everything clients need is in the replicated NetState (+ HookedFish, LastLandedFish); every
 *  machine draws the bobber and line from it. Charging is local to the owning client (the server clamps the charge it gets).
 *
 *  Tuning: a DT_Fishing row (ULureFishingSettings::FishingTable, row ProfileRow or the settings' DefaultProfileRow); a missing
 *  table or row uses the built-in profile with one warning. Rod pose rules per stance: DT_Movement (RodPose*, CanFish).
 *
 *  Visuals (never on a dedicated server): SM_Rod_Basic on the arms' hand_r_rod bone (owner only, world scale kept, so it
 *  survives a scaled root bone), SM_Bobber at BobberScale, and the line from the rod's LineTip (first-person corrected) to the
 *  bobber's LineAttach, drawn >= LinePixelWidth px wide. Placeholder cast motion: the rod swings (CastMontage replaces it).
 *
 *  Reel fight (T-007): once hooked, the server simulates the fight (FLureFight, fixed steps) from the fish's final stats, its
 *  species' DT_FightPattern row, DT_FishFight tuning and the equipped gear (DT_Gear, replicated Loadout). The owning client only
 *  sends its reel input (ServerSetReeling: the Cast button held while a fish is on). Clients get FightNet (tension, fish
 *  state, line out) for the placeholder HUD, the rod bend and the line. Landing hands the instance to OnFishLanded (server).
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureFishingComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureFishingComponent();

	// ---- Tuning ----

	/** DT_Fishing row this player uses (gear profiles later). None = ULureFishingSettings::DefaultProfileRow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Fishing")
	FName ProfileRow;

	/** Uses Table's ProfileRow (null = the built-in profile). Tests and gear call this; normally the settings table is used. */
	UFUNCTION(BlueprintCallable, Category="Lure|Fishing")
	void ApplyFishingTable(const UDataTable* Table);

	/** Uses Row directly (validated; an invalid row falls back to the built-in profile). */
	void SetFishingProfile(const FLureFishingRow& Row);

	/** The profile in use (resolved from the settings table on first use). */
	const FLureFishingRow& GetProfile() const;

	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	FLureFishingRow GetFishingProfile() const { return GetProfile(); }

	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	bool IsUsingFallbackProfile() const;

	/** The rod is equipped (no inventory yet: always, until something takes it away). In the water the rod is never in hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|Fishing")
	bool bRodEquipped = true;

	/** Time of day for bites, hours (< 0 = ULureFishingSettings::DefaultTimeOfDayHours until the day/night cycle, T-013). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|Fishing|Bite")
	float TimeOfDayOverride = -1.f;

	/** Rarity luck from gear (T-011), added to the spot's Luck. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|Fishing|Bite")
	float GearLuck = 0.f;

	/** Bait override (debug); none = the equipped hook's bait (DT_Gear), then ULureFishingSettings::DefaultBait. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|Fishing|Bite", meta=(Categories="Bait,Hook"))
	FGameplayTag BaitTag;

	/** Player level the fight compares the fish's level with (UFishSettings::LevelScaling). T-010 sets it; 1 until then. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|Fishing|Fight", meta=(ClampMin="1"))
	int32 PlayerLevel = 1;

	// ---- Gear (T-007) ----

	/** The equipped gear (DT_Gear row names; a None slot = ULureFishingSettings::DefaultLoadout). Replicated. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing|Gear")
	FLureGearLoadout GetLoadout() const;

	/**
	 *  Server: equips gear (the shop, T-012, saves). Per slot: a known row of that slot is equipped; None keeps the current
	 *  item; an unknown id or a row of another slot is refused (warning) and keeps the current item. True if nothing was refused.
	 *  Refused entirely while a fish is on (false).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Fishing|Gear")
	bool AuthoritySetLoadout(const FLureGearLoadout& NewLoadout);

	/** The equipped gear's numbers (resolved from DT_Gear on every machine). */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing|Gear")
	FLureGearStats GetGearStats() const;

	// ---- Input (owning client; the character binds the Cast and Hook actions here) ----

	/** Cast button down: starts charging when no line is out; with a line out it hooks (PressHook). */
	UFUNCTION(BlueprintCallable, Category="Lure|Fishing")
	void PressCast();

	/** Cast button up: casts with the charge so far (asks the server). */
	UFUNCTION(BlueprintCallable, Category="Lure|Fishing")
	void ReleaseCast();

	/** Hook: during a bite it hooks; before a bite it follows the profile's EarlyHook rule (default: reel in). */
	UFUNCTION(BlueprintCallable, Category="Lure|Fishing")
	void PressHook();

	/** Brings the line in (any state). */
	UFUNCTION(BlueprintCallable, Category="Lure|Fishing")
	void RequestReelIn();

	/** Owning client: an extra reel button (debug tools, a future separate key). The Cast button held while a fish is on reels too. */
	UFUNCTION(BlueprintCallable, Category="Lure|Fishing")
	void SetReelHeld(bool bHeld) { bReelButtonHeld = bHeld; }

	/** Owning client: the player holds reel (Cast button or SetReelHeld) while a fish is on. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	bool WantsToReel() const;

	/** Binds the Cast and Hook actions of ULureInputSubsystem. */
	void BindInput(UEnhancedInputComponent& Input);

	// ---- State (every machine) ----

	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	ELureFishingState GetFishingState() const { return NetState.State; }

	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	FLureFishingNetState GetFishingNetState() const { return NetState; }

	const FLureFishingNetState& GetNetState() const { return NetState; }

	/** A line is out (casting, waiting, biting or hooked). */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	bool IsLineOut() const { return NetState.State != ELureFishingState::Idle; }

	/** Owning client: the cast button is held. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	bool IsCharging() const { return bCharging; }

	/** Owning client: charge 0..1 while charging. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	float GetCharge() const { return Charge; }

	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	bool IsRodInHand() const;

	/** Why a cast can't start right now (None = it can). The owning client also refuses while its arms show a tucked rod. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	ELureCastBlock GetCastBlock() const;

	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	FFishInstance GetHookedFish() const { return HookedFish; }

	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	FFishInstance GetLastLandedFish() const { return LastLandedFish; }

	/** The reel fight as clients see it (tension, fish state, line out). */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing|Fight")
	FLureFightNetState GetFightNetState() const { return FightNet; }

	const FLureFightNetState& GetFightNet() const { return FightNet; }

	/** The fight tuning in use (DT_FishFight row, or the built-in one). */
	const FLureFishFightRow& GetFightTuning() const;

	/** Placeholder HUD text (plain lines): cast power, bite/hook prompt, results. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	FString GetStatusText() const;

	/** Server-synchronized time the fishing state uses, seconds. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	double GetFishingTime() const;

	/** Where the bobber is drawn now (computed from the replicated state; also without a mesh). */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	FVector GetBobberLocation() const;

	/** Where the line starts: the rod's LineTip (first-person corrected) for the owner, an estimate from the eye for others. */
	UFUNCTION(BlueprintPure, Category="Lure|Fishing")
	FVector GetLineStart() const;

	UStaticMeshComponent* GetRodMesh() const { return RodMesh; }
	UStaticMeshComponent* GetBobberMesh() const { return BobberMesh; }
	ULureFishingLineComponent* GetLine() const { return Line; }

	// ---- Server (authority). Public for tests and debug tools; the RPCs call these. ----

	/** Starts a cast if the rules allow (else a Refused result). Charge 0..1, AimYawDegrees = horizontal direction. */
	bool AuthorityCast(float Charge01, float AimYawDegrees);

	/** Hook press: hooks inside the window, a late press is a miss, an early press follows EarlyHook. */
	void AuthorityHook();

	/** Brings the line in; Reason None = the player's choice. A hooked fish is lost. */
	void AuthorityReelIn(ELureCastBlock Reason = ELureCastBlock::None);

	/** Fish tables for bites (tests). Default: UFishSettings::LoadTables on the first bite. */
	void SetFishTables(const FFishTables& InTables);

	/** Seeds the server RNG (bite waits, nibbles, fish seeds). Default: a random seed at BeginPlay. */
	void SetRandomSeed(int32 Seed);

	/** Server: the fish that bites right now (lost on a miss, becomes HookedFish on a hook). */
	const FFishInstance& GetPendingFish() const { return PendingFish; }

	/** Server: the context of the last bite roll (spot habitat, region, luck, time, seed). */
	const FFishRollContext& GetLastRollContext() const { return LastRollContext; }

	/** Server: the fishing spot the bobber is in (valid only if HasCurrentSpot). */
	const FLureFishingSpot& GetCurrentSpot() const { return CurrentSpot; }
	bool HasCurrentSpot() const { return bHasSpot; }

	/** Server: when the next bite (or bite attempt) is due; < 0 = none scheduled. */
	double GetScheduledBiteTime() const { return NextBiteTime; }

	/** Server: extra hook time this player gets (HookLatencyGrace for remote players, 0 for the host and standalone). */
	float GetHookGrace() const;

	/** Server: hooks Fish right away (the line must be in the water: Waiting or Biting). Tests and debug tools (T-025 Lure.GiveFish). */
	bool AuthorityHookFish(const FFishInstance& Fish);

	/** Server: the reel input the fight runs with (the ServerSetReeling RPC calls this; tests and debug tools too). */
	void AuthoritySetReeling(bool bReeling);

	/** Server: the reel input now. */
	bool IsServerReeling() const { return bServerReeling; }

	/** Server: the fight simulation (valid while FightNet.bActive). */
	const FLureFightState& GetFightState() const { return Fight; }

	/** Tables for the fight (tests). Null = the built-in data for that table. Default: the settings' tables. */
	void SetFightTables(const UDataTable* InGearTable, const UDataTable* InPatternTable, const UDataTable* InFightTable);

	/** Server only: a fish was landed (after the reel fight, or the AutoLandDelay debug placeholder). Hand-off to the cooler (T-010). */
	UPROPERTY(BlueprintAssignable, Category="Lure|Fishing")
	FLureFishLandedSignature OnFishLanded;

	/** Native OnFishLanded (C++ listeners and tests). */
	FLureFishLandedNative OnFishLandedNative;

	UPROPERTY(BlueprintAssignable, Category="Lure|Fishing")
	FLureFishingEventSignature OnFishingEvent;

	// ---- UActorComponent ----

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	/** The line's state; written only by the server. */
	UPROPERTY(ReplicatedUsing=OnRep_NetState, BlueprintReadOnly, Category="Lure|Fishing")
	FLureFishingNetState NetState;

	/** The fish on the line (replicated when hooked). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Lure|Fishing")
	FFishInstance HookedFish;

	/** The last fish landed. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Lure|Fishing")
	FFishInstance LastLandedFish;

	/** The equipped gear (server-written). */
	UPROPERTY(ReplicatedUsing=OnRep_Loadout, BlueprintReadOnly, Category="Lure|Fishing|Gear")
	FLureGearLoadout Loadout;

	/** The reel fight for clients (server-written every tick while a fish is on). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Lure|Fishing|Fight")
	FLureFightNetState FightNet;

	UFUNCTION()
	void OnRep_NetState(const FLureFishingNetState& PreviousState);

	UFUNCTION()
	void OnRep_Loadout();

	/** The owning client's reel input (hold = true). The only fight input a client sends: the server decides the outcome. */
	UFUNCTION(Server, Reliable)
	void ServerSetReeling(bool bReeling);

	UFUNCTION(Server, Reliable)
	void ServerCast(float Charge01, float AimYawDegrees);

	UFUNCTION(Server, Reliable)
	void ServerHook();

	UFUNCTION(Server, Reliable)
	void ServerReelIn();

private:

	// Profile.
	FLureFishingRow Profile;
	bool bProfileResolved = false;
	bool bFallbackProfile = true;
	void ResolveProfile();
	void ApplyProfileFrom(const UDataTable* Table, const FString& MissingReason);

	// Server.
	FRandomStream Rng;
	bool bSeedSet = false;
	FFishTables Tables;
	bool bTablesSet = false;
	bool bTablesTried = false;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> TableRefs;
	FFishInstance PendingFish;
	FFishRollContext LastRollContext;
	FLureFishingSpot CurrentSpot;
	bool bHasSpot = false;
	double NextBiteTime = -1.0;
	TArray<double> NibbleSchedule;
	int32 NextNibbleIndex = 0;

	void ServerTick(double Now);
	void SetNetState(const FLureFishingNetState& NewState);
	void BeginResult(FLureFishingNetState& State, ELureFishingResult Result, ELureCastBlock Reason, double Now) const;
	void LandBobber(double Now);
	void ScheduleBite(double Now, bool bAfterMiss);
	void UpdateNibbles(double Now);
	void TryBite(double Now);
	void Hook(double Now);
	void Miss(double Now);
	void LandFish(double Now);
	void Refuse(ELureCastBlock Reason, double Now);

	// Reel fight and gear (T-007).
	FLureFightState Fight;
	bool bServerReeling = false;
	double FightLastTime = 0.0;
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> GearTableRef;
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> PatternTableRef;
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> FightTableRef;
	bool bFightTablesResolved = false;
	FLureFishFightRow FightTuning;
	mutable FLureGearStats GearStats;
	mutable bool bGearResolved = false;
	void ResolveFightTables();
	FLureGearLoadout GetEffectiveLoadout() const;
	FLureFightPatternRow FindPattern(FName PatternId, FName& OutUsedId);
	void BeginFight(double Now);
	void UpdateFight(double Now);
	void EndFight(ELureFightOutcome Outcome, double Now);
	void PublishFight();
	bool EnsureFishTables();
	FLureFishingEnvironment MakeEnvironment() const;

	// Rules input.
	ACharacter* GetCharacter() const;
	APawn* GetPawn() const;
	bool IsOwnerLocallyControlled() const;
	FLureCastConditions GetConditions() const;
	const FLureMovementRow& GetMovementRow() const;
	FVector GetEyeLocation() const;
	UCameraComponent* GetOwnerCamera() const;

	// Owning client.
	bool bCharging = false;
	double ChargeStartTime = 0.0;
	float Charge = 0.f;
	bool bAwaitingCast = false;
	double AwaitingSince = 0.0;
	bool bCastButtonHeld = false;
	bool bReelButtonHeld = false;
	bool bReelSent = false;
	void UpdateReelInput();
	uint8 AwaitedCastId = 0;
	ELureCastBlock LocalRefusal = ELureCastBlock::None;
	double LocalRefusalTime = -1000.0;
	double SwingStartTime = -1.0;
	float SwingFromPitch = 0.f;
	void UpdateCharge();
	double GetLocalTime() const;

	// Visuals.
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RodMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BobberMesh;

	UPROPERTY(Transient)
	TObjectPtr<ULureFishingLineComponent> Line;

	bool bRodTried = false;
	bool bBobberTried = false;
	bool bLineTried = false;
	/** Pivot to the bobber's lowest point at BobberScale, cm (a bobber on land rests on it). */
	float BobberBottom = 0.f;
	/** Pivot to LineAttach at BobberScale, cm (used when the mesh has no socket). */
	float BobberTop = 14.7f;
	void UpdateVisuals(float DeltaTime);
	void EnsureRod();
	void EnsureBobberAndLine();
	void UpdateRod();
	void ComputeBobberPose(double Now, FVector& OutLocation, FRotator& OutRotation) const;
	void GetViewer(FVector& OutLocation, float& OutFovDeg) const;
	void HandleStateChanged(const FLureFishingNetState& Previous);
	void PlayFeedbackSound(const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location) const;
	void PlayArmsMontage(const TSoftObjectPtr<UAnimMontage>& Montage, bool bStop = false) const;
	void UpdateFightMontages();
	bool bReelMontagePlaying = false;
};

// Lure: first-person movement with sprint, crouch and prone (T-004). Server-authoritative and client-predicted.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LureSwimTypes.h"
#include "LureCharacterMovementComponent.generated.h"

class ALureLadder;
class ALurePlayerCharacter;
class UDataTable;

/**
 *  The server's reply to a client move (FCharacterMoveResponseDataContainer), plus the climb state the next predicted
 *  moves depend on. Sent only with corrections (acks stay the engine's size): the climb plan while the server climbs,
 *  and the takeoff feet height of the climb rule. The owning client restores both after the engine applies the
 *  correction (ULureCharacterMovementComponent::ClientHandleMoveResponse), so its replay continues the server's climb.
 *  Pattern for new predicted state: rebuild it from the saved move, or add it here.
 */
struct FLureMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	using Super = FCharacterMoveResponseDataContainer;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;

	/** The server was climbing (MOVE_Custom ClimbOut or LedgeClimb) on this plan at the corrected move. */
	bool bHasClimbPlan = false;
	FLureClimbPlan ClimbPlan;

	/** The server's climb-rule reference at the corrected move (ULureCharacterMovementComponent::GetTakeoffFeetHeight), cm. */
	float TakeoffFeetHeight = 0.f;
};

/**
 *  Character movement for Lure.
 *
 *  Stances: Stand, Crouch, Prone (ELureStance), plus Sprint as a Stand modifier. Every speed, capsule, eye height,
 *  camera blend time, noise multiplier and jump rule comes from DT_Movement (ApplyMovementTable / the settings table,
 *  built-in fallback rows otherwise).
 *
 *  Networking (same model as the engine's crouch):
 *  - The owning client sets wishes (bWantsToCrouch, sprint, prone). They travel to the server in every saved move's
 *    compressed flags: crouch = the engine's FLAG_WantsToCrouch, sprint = FLAG_Custom_0, prone = FLAG_Custom_1.
 *  - Client and server run the same UpdateCharacterStateBeforeMovement from those flags, so stance changes are
 *    predicted locally and re-simulated on corrections; the server has the final say (headroom checks, no sprint
 *    while crouched or prone, no jump while prone).
 *  - The resulting posture is replicated to other players through ACharacter::bIsCrouched and
 *    ALurePlayerCharacter::bIsProne (COND_SimulatedOnly, OnRep resizes the proxy capsule).
 *  - Climbs (T-026 netfix, docs/specs/swimming.md "Networking"): only the saved move's Jump flag travels; the climb
 *    starts inside the move (never in DoJump) and the server plans its own. Corrections carry the plan and the takeoff
 *    height (FLureMoveResponseDataContainer). Other players' copies follow the replicated movement during a climb.
 *
 *  Crouch keeps the engine's replicated crouch (bWantsToCrouch, bIsCrouched, OnRep_IsCrouched, IsCrouching()), but
 *  Crouch()/UnCrouch() are overridden so every capsule change goes through ResizeCapsuleForStance: the heights come
 *  from DT_Movement (the engine's UnCrouch would restore the class-default capsule) and the stand-up check uses the
 *  target stance's radius as well as its height.
 */
UCLASS()
class ULureCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_Lure;
	friend struct FLureMoveResponseDataContainer;

	/** Automation tests (Source/VibeGame/Tests) reach protected movement internals through this; gameplay code never does. */
	friend struct FLureMovementTestAccess;

public:

	ULureCharacterMovementComponent();

	// ---- Data ----

	/**
	 *  Uses Table for all tuning (null = built-in fallback rows). Missing or invalid rows fall back per state with ONE
	 *  warning (LogLureMovement, contains "using built-in fallback rows"). Call before BeginPlay (e.g. on a deferred
	 *  spawn) to skip the settings table; after BeginPlay it re-applies the capsule and eye height at once.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void ApplyMovementTable(const UDataTable* Table);

	/** The resolved row in use for a state. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	FLureMovementRow GetMovementRow(ELureMovementState State) const;

	/** The built-in row used when DT_Movement is missing or a row is invalid. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	static FLureMovementRow GetFallbackRow(ELureMovementState State);

	/** True if the given state currently uses the built-in fallback row. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsUsingFallbackRow(ELureMovementState State) const;

	/** C++ access to a resolved row without a copy. */
	const FLureMovementRow& GetRow(ELureMovementState State) const;
	const FLureMovementRow& GetStanceRow(ELureStance Stance) const;

	// ---- State ----

	/** Current posture (from the replicated crouch/prone state). */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	ELureStance GetStance() const;

	/** Sprinting = sprint requested, standing, on the ground or in the air, and with movement input. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsSprinting() const;

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsProne() const;

	/** The DT_Movement row in use right now (Sprint when sprinting, otherwise the stance). */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	ELureMovementState GetMovementState() const;

	/** The posture the wishes ask for (Prone > Crouch > Stand). Differs from GetStance while a stand-up is queued under a ceiling. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	ELureStance GetRequestedStance() const;

	/** Noise multiplier of the row in use (for the noise system, T-014). */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	float GetStanceNoiseMultiplier() const;

	/** Stance rules for jumping: the row's CanJump (current and requested posture); never while prone. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool CanJumpInCurrentStance() const;

	/** Pure headroom query: would the capsule of Stance fit here now (ceiling and walls)? Changes nothing. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool CanEnterStance(ELureStance Stance) const;

	/** Height of the capsule bottom along the up axis (world Z with normal gravity), cm. EyeHeight is measured from here. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	float GetFeetHeight() const;

	// ---- Wishes (sent to the server in the saved moves). ALurePlayerCharacter's request functions keep them consistent. ----

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsSprintRequested() const { return bWantsToSprint; }

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsProneRequested() const { return bWantsToProne; }

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsCrouchRequested() const { return bWantsToCrouch; }

	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void SetSprintRequested(bool bRequested) { bWantsToSprint = bRequested; }

	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void SetProneRequested(bool bRequested) { bWantsToProne = bRequested; }

	/** Applies a move's compressed flags (what the server does with each client move). Public for tests; the engine declares it protected. */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	// ---- Prone (mirrors Crouch/UnCrouch; bClientSimulation = applying replicated state on a simulated proxy) ----

	virtual void Prone(bool bClientSimulation = false);
	virtual void UnProne(bool bClientSimulation = false);

	/** Prone is possible only on the ground (it is refused in the air, and ends if you walk off a ledge), and not where the water is too deep for it. */
	virtual bool CanProneInCurrentState() const;

	/**
	 *  Wading (T-026 B2): would going into Stance here put the capsule center in water? Then the stance is refused before
	 *  anything changes (the engine would switch to swimming, stand you up and drop you out again: an in/out blip that
	 *  cancels fishing). Only on the ground, where a stance change keeps the feet in place (e.g. crouch in 60 cm of water, prone in 30 cm).
	 */
	UFUNCTION(BlueprintPure, Category="Lure|Swim")
	bool IsStanceTooDeepForWater(ELureStance Stance) const;

	/** True if Point is inside the water volume the engine would pick there (the highest-priority physics volume holding it). */
	bool IsPointInWater(const FVector& Point) const;

	/** Master switch for prone (like the engine's CanEverCrouch). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|Movement")
	bool bCanEverProne = true;

	/**
	 *  Largest sideways push (cm) used to make room for a wider capsule when getting up next to walls. Never less than
	 *  GetStanceNudgeLimit's automatic minimum (enough for walls on two sides, so a data change can't strand you prone).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|Movement", meta=(ClampMin="0"))
	float MaxStanceNudge = 14.f;

	/** The push allowed when a capsule of OldRadius grows to NewRadius: max(MaxStanceNudge, sqrt(2) x (NewRadius - OldRadius) + 1 cm). */
	float GetStanceNudgeLimit(float NewRadius, float OldRadius) const;

	// ---- Swimming (T-026; defined in LureSwimMovement.cpp, spec docs/specs/swimming.md) ----
	//  Water = a physics volume with bWaterVolume (ALureWaterVolume); the engine switches to MOVE_Swimming when the capsule
	//  center enters it. While swimming: the Swim / SwimSprint rows, the Stand capsule, no crouch or prone (wishes are
	//  cleared), Jump climbs out (MOVE_Custom ClimbOut). All of it runs in the predicted move, like the stances.

	/** Climbing out of the water right now (MOVE_Custom, ELureCustomMovementMode::ClimbOut). */
	UFUNCTION(BlueprintPure, Category="Lure|Swim")
	bool IsClimbingOut() const;

	/** Pulling up onto a ledge a jump reached (MOVE_Custom, ELureCustomMovementMode::LedgeClimb). */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsLedgeClimbing() const;

	/** Either climb (ClimbOut or LedgeClimb). */
	bool IsClimbing() const { return IsClimbingOut() || IsLedgeClimbing(); }

	/**
	 *  Owning client only: true while it applies a server correction and replays its unacknowledged moves. The movement
	 *  mode can pass through states the server never had then, so OnSwimStateChanged waits until it is over.
	 */
	bool IsReconcilingWithServer() const { return bReconcilingWithServer; }

	/** In the water for gameplay: swimming or climbing out. No stances, no fishing. */
	UFUNCTION(BlueprintPure, Category="Lure|Swim")
	bool IsSwimmingOrClimbingOut() const { return IsSwimming() || IsClimbingOut(); }

	/** The one surface rule: the row in use has SurfaceFloatDepth > 0 (the Swim rows). Otherwise the engine's free swimming runs (diving, later). */
	UFUNCTION(BlueprintPure, Category="Lure|Swim")
	bool ShouldFloatAtSurface() const;

	/** Height of the water surface where the character is (its water volume's top), cm. False when not in water. */
	UFUNCTION(BlueprintPure, Category="Lure|Swim")
	bool GetWaterSurfaceHeight(float& OutSurfaceZ) const;

	/** Highest edge (cm above the water) Jump would climb onto from here: the swim row's ClimbMaxHeight, or a ladder's. */
	UFUNCTION(BlueprintPure, Category="Lure|Swim")
	float GetClimbOutMaxHeight() const;

	/** Pure query: is there an edge in front (or a ladder here) to climb out of the water onto right now, and how? Changes nothing. */
	UFUNCTION(BlueprintPure, Category="Lure|Swim")
	bool FindClimbOutPlan(FLureClimbPlan& OutPlan) const;

	/**
	 *  Pure query for the jump climb: airborne from a jump, on the way down, moving toward a ledge whose top is above the
	 *  feet (within the capsule radius) and at most ClimbMaxHeight above the takeoff, with room to stand. Changes nothing.
	 */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool FindJumpClimbPlan(FLureClimbPlan& OutPlan) const;

	/** Feet height where the current fall or jump started (the climb rule's reference), cm. */
	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	float GetTakeoffFeetHeight() const { return TakeoffFeetHeight; }

	/** The ladder whose grab zone holds Location (null if none). */
	const ALureLadder* FindLadderAt(const FVector& Location) const;

	/**
	 *  Starts a climb out if FindClimbOutPlan finds one. Call it only inside a move (PerformMovement): Jump in the water
	 *  queues it (DoJump) and PhysSwimming calls this, so the Jump flag still reaches the server in the saved move.
	 */
	bool TryStartClimbOut();

	/** Starts a jump climb if FindJumpClimbPlan finds one (checked every falling update). */
	bool TryStartJumpClimb();

	/**
	 *  Pure query for stepping out of the water (T-026 QA B3): surface swimming, pushing toward a submerged edge whose top
	 *  is above the feet and at most MaxStepHeight up, where you stand with the capsule center out of the water (wading).
	 *  The step out is a ClimbOut climb, so it is predicted and corrected like one. Changes nothing.
	 */
	bool FindStepOutPlan(float SurfaceZ, FLureClimbPlan& OutPlan) const;

	/** Starts a step out if FindStepOutPlan finds one (PhysSurfaceSwimming, when the swimmer runs into an edge). */
	bool TryStartStepOut(float SurfaceZ);

	/**
	 *  Vertical speed of the surface float after DeltaTime: a critically damped spring pulling the capsule center to
	 *  TargetZ (implicit, so any frame time is stable). SettleTime = seconds to settle within about 2%. Pure, for tests.
	 */
	static float ComputeSurfaceFloatVelocity(float CenterZ, float VerticalSpeed, float TargetZ, float SettleTime, float DeltaTime);

	// The swim feel (settle time, braking, climb-out reach and lowest edge) is data: DT_Movement Swim rows (T-026 D3/D4).

	/** How close (cm) the body must be to a ledge's face for a jump to pull up onto it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|Movement", meta=(ClampMin="0"))
	float JumpClimbReach = 10.f;

	/** CharacterMovement: refuses a landing that would lift the feet onto a ledge higher than the climb rule allows (T-004 B1). */
	virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;

	// ---- UCharacterMovementComponent ----

	virtual void BeginPlay() override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation = false) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/**
	 *  A teleport (respawn, Lure.Teleport) ends a climb: the plan is dropped and the engine picks the mode at the new place
	 *  (swimming in water, else falling/landing). T-026 B1. Runs where TeleportTo runs (the server; the owning client too
	 *  when it teleports locally); the owning client of a server teleport gets the server's mode and no plan in the correction.
	 */
	virtual void OnTeleported() override;

	/** Owning client: applies the server's reply, then restores the climb state a correction carries (FLureMoveResponseDataContainer). */
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;

protected:

	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	virtual void PerformMovement(float DeltaTime) override;

	/** The engine calls this only when it really applies a correction (not for a stale or ignored one). */
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity,
		FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode,
		FVector ServerGravityDirection) override;

	// ---- Swimming (LureSwimMovement.cpp) ----
	virtual void PhysSwimming(float DeltaTime, int32 Iterations) override;
	virtual void PhysFalling(float DeltaTime, int32 Iterations) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	/** What FindLedgePlan looks for. Heights are world Z, cm. */
	struct FLedgeQuery
	{
		FVector Forward = FVector::ForwardVector;	// horizontal, normalized: where to look
		float ReferenceZ = 0.f;						// the rule's zero (water surface or takeoff feet)
		float MaxHeight = 0.f;						// highest allowed top above ReferenceZ
		float LowestTopZ = 0.f;						// tops below this don't count
		float HighestTopZ = TNumericLimits<float>::Max(); // extra cap on the top (a jump reaches only so far above the feet)
		float FaceReach = 0.f;						// how far ahead the edge's face may be
		const ALureLadder* Ladder = nullptr;		// at a ladder: the face is the ladder itself
		float TargetRadius = 0.f;					// scaled capsule after the climb
		float TargetHalfHeight = 0.f;
		float Speed = 0.f;
	};

	/** The shared edge finder: face, top, room to stand, clear path. */
	bool FindLedgePlan(const FLedgeQuery& Query, FLureClimbPlan& OutPlan) const;

	/** Enters the climb mode on a plan. */
	void StartClimb(const FLureClimbPlan& Plan, ELureCustomMovementMode Mode);

	/** Surface swimming: horizontal swim input, vertical float spring to the row's SurfaceFloatDepth. */
	void PhysSurfaceSwimming(float DeltaTime, int32 Iterations, float SurfaceZ);

	/** Follows ClimbPlan: straight up along the edge, then onto it; walking at the end. Other players' copies follow the replicated movement. */
	void PhysClimb(float DeltaTime, int32 Iterations);

	/**
	 *  The plan of the climb in progress. Client and server each plan the climb from the same move; when they disagree,
	 *  the server's correction carries its plan (FLureMoveResponseDataContainer). Simulated proxies never have one.
	 */
	FLureClimbPlan ClimbPlan;
	bool bHasClimbPlan = false;

	/**
	 *  Feet height when the character last left the ground or the water (set on entering MOVE_Falling). Carried in
	 *  corrections. Deliberately NOT restored from saved moves: every replay starts from a correction, and the replay
	 *  must recompute it wherever the corrected path enters Falling (a saved value would be the old, wrong path's).
	 */
	float TakeoffFeetHeight = 0.f;

	/**
	 *  Jump was pressed in the water with an edge to climb (set in DoJump, which runs before the owning client saves
	 *  the move). PhysSwimming starts the climb inside the same move; PerformMovement drops a leftover request.
	 */
	bool bClimbOutRequested = false;

	/** Server reply storage (registered with SetMoveResponseDataContainer in the constructor). */
	FLureMoveResponseDataContainer LureMoveResponseData;

	/** Set by OnClientCorrectionReceived while ClientHandleMoveResponse runs: the correction was really applied. */
	bool bClientCorrectionApplied = false;

	/** See IsReconcilingWithServer. */
	bool bReconcilingWithServer = false;

	/** After an applied correction: the server's takeoff height, and its plan if the corrected mode is a climb. */
	void ApplyCorrectionClimbState(const FLureMoveResponseDataContainer& Response);

	/** Resizes the capsule to Stance's row, keeping the feet in place on the ground. Checks for room when growing (not for client simulation or bForce). Returns false if blocked. */
	bool ResizeCapsuleForStance(ELureStance Stance, bool bClientSimulation, bool bForce = false);

	/** Where a capsule of this unscaled size fits now (feet kept on the ground; center, feet or head kept in the air; small sideways nudge). */
	bool FindCapsuleLocation(float Radius, float HalfHeight, FVector& OutLocation) const;

	bool IsCapsuleEncroachedAt(const FVector& Location, float ScaledRadius, float ScaledHalfHeight) const;
	bool FindNudgedCapsuleLocation(const FVector& Location, float ScaledRadius, float ScaledHalfHeight, FVector& OutLocation) const;

	/** Engine fields other code reads (MaxWalkSpeed, MaxWalkSpeedCrouched, CrouchedHalfHeight, JumpZVelocity, MaxAcceleration). */
	void SyncEngineFieldsFromRows();

	/** After the rows change: capsule of the current stance (forced), then the character snaps its eye height. */
	void RefreshShapeFromRows();

	void ResolveTableFromSettings();
	void ApplyResolvedTable(const UDataTable* Table, const FString& MissingReason);

	void NotifyCapsuleResized(float OldFeetHeight);
	void CallOnStartCrouch();
	void CallOnEndCrouch();

	ALurePlayerCharacter* GetLureCharacter() const;

	/** Crouch allowed if we ignore prone (for the Prone -> Crouch transition). */
	bool CanCrouchIgnoringProne() const;

	/** The resolved DT_Movement rows, indexed by ELureMovementState. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category="Lure|Movement")
	TArray<FLureMovementRow> ResolvedRows;

	/** Bit (1 << ELureMovementState) set = that state uses the built-in fallback row. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category="Lure|Movement")
	uint8 FallbackRowMask = FLureMovementData::AllStatesMask;

	bool bMovementTableApplied = false;

	uint8 bWantsToSprint : 1;
	uint8 bWantsToProne : 1;
};

/** Saved move carrying the sprint and prone wishes (compressed flag bits FLAG_Custom_0 and FLAG_Custom_1). */
class FSavedMove_Lure : public FSavedMove_Character
{
public:

	using Super = FSavedMove_Character;

	static constexpr uint8 FLAG_Sprint = FSavedMove_Character::FLAG_Custom_0;
	static constexpr uint8 FLAG_Prone = FSavedMove_Character::FLAG_Custom_1;

	uint8 bSavedWantsToSprint : 1;
	uint8 bSavedWantsToProne : 1;

	FSavedMove_Lure();

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
};

/** Client prediction data that allocates FSavedMove_Lure. */
class FNetworkPredictionData_Client_Lure : public FNetworkPredictionData_Client_Character
{
public:

	using Super = FNetworkPredictionData_Client_Character;

	explicit FNetworkPredictionData_Client_Lure(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};

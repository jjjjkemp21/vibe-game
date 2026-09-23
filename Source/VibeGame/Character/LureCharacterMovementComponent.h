// Lure: first-person movement with sprint, crouch and prone (T-004). Server-authoritative and client-predicted.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "LureCharacterMovementComponent.generated.h"

class ALurePlayerCharacter;
class UDataTable;

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

	/** Prone is possible only on the ground (it is refused in the air, and ends if you walk off a ledge). */
	virtual bool CanProneInCurrentState() const;

	/** Master switch for prone (like the engine's CanEverCrouch). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|Movement")
	bool bCanEverProne = true;

	/** Largest sideways push (cm) used to make room for a wider capsule when getting up next to a wall. 0 = never push. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|Movement", meta=(ClampMin="0"))
	float MaxStanceNudge = 8.f;

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

protected:

	virtual bool ClientUpdatePositionAfterServerUpdate() override;

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

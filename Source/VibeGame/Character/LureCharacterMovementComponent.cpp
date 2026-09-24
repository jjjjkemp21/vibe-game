// Lure: first-person movement with sprint, crouch and prone (T-004). Swimming (T-026) is in LureSwimMovement.cpp.

#include "Character/LureCharacterMovementComponent.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/PackageName.h"

namespace LureMovementPrivate
{
	/** Grows the stand-up test shape a hair, like the engine's UnCrouch, so we never end up touching geometry. */
	constexpr float SweepInflation = UE_KINDA_SMALL_NUMBER * 10.f;

	/** Size tolerance (cm) for "the capsule already has this size" (simulated proxies are shrunk by 0.01 cm). */
	constexpr float SizeTolerance = 0.05f;
}

ULureCharacterMovementComponent::ULureCharacterMovementComponent()
{
	bWantsToSprint = false;
	bWantsToProne = false;

	TArray<FString> Unused;
	FallbackRowMask = FLureMovementData::ResolveRows(nullptr, ResolvedRows, Unused);

	// First person: the controller's yaw turns the body; the camera never orbits.
	bOrientRotationToMovement = false;

	// Crouch is on; walking off a ledge (a dock edge) while crouched is allowed (lead decision, docs/specs/movement-rules.md).
	GetNavAgentPropertiesRef().bCanCrouch = true;
	bCanWalkOffLedgesWhenCrouching = true;

	AirControl = 0.35f;
	BrakingDecelerationFalling = 1500.f;

	// Server replies carry the climb state (see FLureMoveResponseDataContainer).
	SetMoveResponseDataContainer(LureMoveResponseData);

	SyncEngineFieldsFromRows();
}

// ---- Data ----

void ULureCharacterMovementComponent::ApplyMovementTable(const UDataTable* Table)
{
	ApplyResolvedTable(Table, FString());
	if (HasBegunPlay())
	{
		RefreshShapeFromRows();
	}
}

void ULureCharacterMovementComponent::ApplyResolvedTable(const UDataTable* Table, const FString& MissingReason)
{
	TArray<FString> Problems;
	FallbackRowMask = FLureMovementData::ResolveRows(Table, ResolvedRows, Problems);
	bMovementTableApplied = true;

	if (Problems.Num() > 0)
	{
		if (!Table && !MissingReason.IsEmpty())
		{
			Problems.Reset();
			Problems.Add(MissingReason);
		}
		const FString TableName = Table ? Table->GetPathName() : FString(TEXT("(none)"));
		UE_LOG(LogLureMovement, Warning, TEXT("%s"), *FLureMovementData::FormatResolveWarning(TableName, Problems, FallbackRowMask));
	}

	SyncEngineFieldsFromRows();
}

void ULureCharacterMovementComponent::ResolveTableFromSettings()
{
	const TSoftObjectPtr<UDataTable>& SoftTable = GetDefault<ULureCharacterSettings>()->MovementTable;
	if (SoftTable.IsNull())
	{
		ApplyResolvedTable(nullptr, TEXT("no table is set in Project Settings > Game > Lure Character"));
		return;
	}

	const UDataTable* Table = SoftTable.Get();
	if (!Table)
	{
		// Check first so a table that was never imported (every lane, and main until the editor-operator imports it) doesn't log a load error.
		const FString PackageName = SoftTable.ToSoftObjectPath().GetLongPackageName();
		if (FPackageName::DoesPackageExist(PackageName))
		{
			Table = SoftTable.LoadSynchronous();
		}
	}

	ApplyResolvedTable(Table, Table ? FString() : FString::Printf(TEXT("asset '%s' not found (import data/tables/DT_Movement.csv)"), *SoftTable.ToString()));
}

void ULureCharacterMovementComponent::SyncEngineFieldsFromRows()
{
	const FLureMovementRow& Stand = GetRow(ELureMovementState::Stand);
	const FLureMovementRow& Crouched = GetRow(ELureMovementState::Crouch);
	MaxWalkSpeed = Stand.MaxSpeed;
	MaxWalkSpeedCrouched = Crouched.MaxSpeed;
	MaxSwimSpeed = GetRow(ELureMovementState::Swim).MaxSpeed;
	// Swimming (T-026): coast to a stop in the water instead of gliding on (the engine default is 0).
	BrakingDecelerationSwimming = GetRow(ELureMovementState::Swim).SwimBrakingDeceleration;
	MaxAcceleration = Stand.MaxAcceleration;
	JumpZVelocity = Stand.JumpZVelocity;
	SetCrouchedHalfHeight(FMath::Max(Crouched.CapsuleHalfHeight, Crouched.CapsuleRadius));
}

void ULureCharacterMovementComponent::RefreshShapeFromRows()
{
	if (!HasValidData())
	{
		return;
	}
	const bool bSimulatedProxy = CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy;
	ResizeCapsuleForStance(GetStance(), bSimulatedProxy, /*bForce*/ true);

	if (ALurePlayerCharacter* LureCharacter = GetLureCharacter())
	{
		LureCharacter->SnapEyeHeightToStance();
	}
}

FLureMovementRow ULureCharacterMovementComponent::GetMovementRow(ELureMovementState State) const
{
	return GetRow(State);
}

FLureMovementRow ULureCharacterMovementComponent::GetFallbackRow(ELureMovementState State)
{
	return FLureMovementData::GetFallbackRow(State);
}

bool ULureCharacterMovementComponent::IsUsingFallbackRow(ELureMovementState State) const
{
	return (FallbackRowMask & (1u << static_cast<int32>(State))) != 0;
}

const FLureMovementRow& ULureCharacterMovementComponent::GetRow(ELureMovementState State) const
{
	const int32 Index = static_cast<int32>(State);
	if (ResolvedRows.IsValidIndex(Index))
	{
		return ResolvedRows[Index];
	}
	static const FLureMovementRow StandFallback = FLureMovementData::GetFallbackRow(ELureMovementState::Stand);
	return StandFallback;
}

const FLureMovementRow& ULureCharacterMovementComponent::GetStanceRow(ELureStance Stance) const
{
	return GetRow(FLureMovementData::ToMovementState(Stance));
}

// ---- State ----

ALurePlayerCharacter* ULureCharacterMovementComponent::GetLureCharacter() const
{
	return Cast<ALurePlayerCharacter>(CharacterOwner);
}

bool ULureCharacterMovementComponent::IsProne() const
{
	const ALurePlayerCharacter* LureCharacter = GetLureCharacter();
	return LureCharacter && LureCharacter->IsProne();
}

ELureStance ULureCharacterMovementComponent::GetStance() const
{
	if (IsProne())
	{
		return ELureStance::Prone;
	}
	return IsCrouching() ? ELureStance::Crouch : ELureStance::Stand;
}

bool ULureCharacterMovementComponent::IsSprinting() const
{
	return bWantsToSprint
		&& GetStance() == ELureStance::Stand
		&& (IsMovingOnGround() || IsFalling() || IsSwimming())
		&& !Acceleration.IsNearlyZero();
}

ELureMovementState ULureCharacterMovementComponent::GetMovementState() const
{
	if (IsSwimming())
	{
		return IsSprinting() ? ELureMovementState::SwimSprint : ELureMovementState::Swim;
	}
	switch (GetStance())
	{
	case ELureStance::Prone:
		return ELureMovementState::Prone;
	case ELureStance::Crouch:
		return ELureMovementState::Crouch;
	case ELureStance::Stand:
	default:
		return IsSprinting() ? ELureMovementState::Sprint : ELureMovementState::Stand;
	}
}

ELureStance ULureCharacterMovementComponent::GetRequestedStance() const
{
	if (bWantsToProne)
	{
		return ELureStance::Prone;
	}
	return bWantsToCrouch ? ELureStance::Crouch : ELureStance::Stand;
}

float ULureCharacterMovementComponent::GetStanceNoiseMultiplier() const
{
	return GetRow(GetMovementState()).NoiseMultiplier;
}

bool ULureCharacterMovementComponent::CanJumpInCurrentStance() const
{
	// In the water Jump means "climb out" (T-026): allowed where the row (or a ladder) allows a climb.
	if (IsSwimming())
	{
		return GetClimbOutMaxHeight() > 0.f;
	}
	// Hard rule (GAME_DESIGN): never jump while prone or about to go prone, whatever DT_Movement says.
	if (IsProne() || bWantsToProne)
	{
		return false;
	}
	if (!GetRow(GetMovementState()).CanJump)
	{
		return false;
	}
	const ELureStance Requested = GetRequestedStance();
	return Requested == GetStance() || GetStanceRow(Requested).CanJump;
}

bool ULureCharacterMovementComponent::CanEnterStance(ELureStance Stance) const
{
	if (!HasValidData())
	{
		return false;
	}
	if (Stance == ELureStance::Prone && !CanProneInCurrentState())
	{
		return false;
	}
	if (Stance == ELureStance::Crouch && !CanCrouchIgnoringProne())
	{
		return false;
	}
	if (Stance == GetStance())
	{
		return true;
	}

	const FLureMovementRow& Row = GetStanceRow(Stance);
	const float Radius = FMath::Max(Row.CapsuleRadius, 1.f);
	FVector Unused;
	return FindCapsuleLocation(Radius, FMath::Max(Row.CapsuleHalfHeight, Radius), Unused);
}

void ULureCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsToSprint = (Flags & FSavedMove_Lure::FLAG_Sprint) != 0;
	bWantsToProne = (Flags & FSavedMove_Lure::FLAG_Prone) != 0;
}

bool ULureCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	// Replaying saved moves rewrites the wishes from each move's flags; afterwards restore what the player holds now
	// (the engine does the same for jump and crouch).
	const bool bRealWantsToSprint = bWantsToSprint;
	const bool bRealWantsToProne = bWantsToProne;
	bool bResult = false;
	{
		TGuardValue<bool> Reconciling(bReconcilingWithServer, true);
		bResult = Super::ClientUpdatePositionAfterServerUpdate();
	}
	bWantsToSprint = bRealWantsToSprint;
	bWantsToProne = bRealWantsToProne;

	// The correction and the replay are done: report the settled in-water state once, not every mode in between (a
	// correction into a climb the client had already finished would otherwise fire "in" then "out" in one frame).
	if (ALurePlayerCharacter* LureCharacter = GetLureCharacter())
	{
		LureCharacter->UpdateSwimState();
	}
	return bResult;
}

void ULureCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	Super::PerformMovement(DeltaSeconds);

	// A queued climb (DoJump) belongs to this move only: drop it even if the move ended before PhysSwimming ran.
	bClimbOutRequested = false;
}

// ---- Engine overrides ----

void ULureCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bMovementTableApplied)
	{
		ResolveTableFromSettings();
	}
	RefreshShapeFromRows();
}

float ULureCharacterMovementComponent::GetMaxSpeed() const
{
	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
	case MOVE_Falling:
	{
		// T-030: carrying the cooler (both hands) slows you by its DT_Cooler CarrySpeedMultiplier (1 with empty hands).
		const ALurePlayerCharacter* Lure = GetLureCharacter();
		const ULureHandsComponent* Hands = Lure ? Lure->GetHands() : nullptr;
		return GetRow(GetMovementState()).MaxSpeed * (Hands ? Hands->GetMoveSpeedMultiplier() : 1.f);
	}
	case MOVE_Swimming:
		return GetRow(GetMovementState()).MaxSpeed;
	default:
		return Super::GetMaxSpeed();
	}
}

float ULureCharacterMovementComponent::GetMaxAcceleration() const
{
	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
	case MOVE_Falling:
	case MOVE_Swimming:
		return GetRow(GetMovementState()).MaxAcceleration;
	default:
		return Super::GetMaxAcceleration();
	}
}

bool ULureCharacterMovementComponent::CanAttemptJump() const
{
	// The engine version also refuses while bWantsToCrouch; here DT_Movement's CanJump decides (crouch jumps are allowed by default).
	// Swimming: Jump climbs out (see DoJump).
	return IsJumpAllowed() && CanJumpInCurrentStance() && (IsMovingOnGround() || IsFalling() || IsSwimming());
}

bool ULureCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (IsSwimming())
	{
		// No jumping from the water: Jump pulls you out onto a low edge or up a ladder. DoJump runs in CheckJumpInput,
		// BEFORE the owning client saves the move, so it only queues the climb. Changing the mode here would clear
		// bPressedJump (ACharacter::OnMovementModeChanged -> ResetJumpState), the move would carry no Jump flag, and the
		// server would never climb (review N0). PhysSwimming starts the climb inside the move. The server receives only
		// the Jump flag and plans its own climb, so a client can't force one the server wouldn't allow; replays re-plan
		// from the same flag. The engine calls DoJump even when CanJump() is false (p.UseLegacyDoJump), so check it here.
		FLureClimbPlan Unused;
		bClimbOutRequested = CharacterOwner && CharacterOwner->CanJump() && FindClimbOutPlan(Unused);
		return bClimbOutRequested;
	}
	JumpZVelocity = GetRow(GetMovementState()).JumpZVelocity;
	return Super::DoJump(bReplayingMoves, DeltaTime);
}

bool ULureCharacterMovementComponent::CanCrouchInCurrentState() const
{
	// While prone, prone owns the capsule; Prone -> Crouch goes through UnProne.
	return !IsProne() && CanCrouchIgnoringProne();
}

bool ULureCharacterMovementComponent::CanCrouchIgnoringProne() const
{
	// Wading too deep for it (T-026 B2): refused here, on the owning client and the server alike, before anything changes.
	return Super::CanCrouchInCurrentState() && !IsStanceTooDeepForWater(ELureStance::Crouch);
}

bool ULureCharacterMovementComponent::CanProneInCurrentState() const
{
	return bCanEverProne && IsMovingOnGround() && UpdatedComponent && !UpdatedComponent->IsSimulatingPhysics() && !IsStanceTooDeepForWater(ELureStance::Prone);
}

void ULureCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}
	if (!GetLureCharacter())
	{
		Super::Crouch(bClientSimulation);
		return;
	}
	if (!bClientSimulation && !CanCrouchInCurrentState())
	{
		return;
	}
	if (bClientSimulation && IsProne())
	{
		return; // simulated proxy: the prone state owns the capsule
	}

	if (!ResizeCapsuleForStance(ELureStance::Crouch, bClientSimulation))
	{
		return;
	}
	if (!bClientSimulation)
	{
		CharacterOwner->SetIsCrouched(true);
	}
	CallOnStartCrouch();
}

void ULureCharacterMovementComponent::UnCrouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}
	if (!GetLureCharacter())
	{
		Super::UnCrouch(bClientSimulation);
		return;
	}
	if (IsProne())
	{
		// Prone owns the capsule; only the crouch state is cleared.
		if (!bClientSimulation)
		{
			CharacterOwner->SetIsCrouched(false);
		}
		return;
	}

	// Back to DT_Movement's Stand capsule (not the class default). Blocked = stay crouched; the engine retries every update while bWantsToCrouch is false.
	if (!ResizeCapsuleForStance(ELureStance::Stand, bClientSimulation))
	{
		return;
	}
	if (!bClientSimulation)
	{
		CharacterOwner->SetIsCrouched(false);
	}
	CallOnEndCrouch();
}

void ULureCharacterMovementComponent::Prone(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}
	ALurePlayerCharacter* LureCharacter = GetLureCharacter();
	if (!LureCharacter)
	{
		return;
	}
	if (!bClientSimulation && (IsProne() || !CanProneInCurrentState()))
	{
		return;
	}

	if (!ResizeCapsuleForStance(ELureStance::Prone, bClientSimulation))
	{
		return;
	}
	if (!bClientSimulation)
	{
		if (CharacterOwner->IsCrouched())
		{
			CharacterOwner->SetIsCrouched(false);
		}
		LureCharacter->SetIsProne(true);
	}
	LureCharacter->OnStartProne();
}

void ULureCharacterMovementComponent::UnProne(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}
	ALurePlayerCharacter* LureCharacter = GetLureCharacter();
	if (!LureCharacter)
	{
		return;
	}
	if (!bClientSimulation && !IsProne())
	{
		return;
	}

	// Get up to the requested posture only (no partial rise): blocked = stay prone, retried every update while the wish differs.
	ELureStance Target = ELureStance::Stand;
	if (bClientSimulation)
	{
		Target = CharacterOwner->IsCrouched() ? ELureStance::Crouch : ELureStance::Stand;
	}
	else if (bWantsToCrouch && CanCrouchIgnoringProne())
	{
		Target = ELureStance::Crouch;
	}

	if (!ResizeCapsuleForStance(Target, bClientSimulation))
	{
		return;
	}
	if (!bClientSimulation)
	{
		LureCharacter->SetIsProne(false);
		CharacterOwner->SetIsCrouched(Target == ELureStance::Crouch);
	}
	LureCharacter->OnEndProne();
	if (Target == ELureStance::Crouch)
	{
		CallOnStartCrouch();
	}
}

void ULureCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// Proxies get the replicated prone state (like crouch). Everyone else acts on the wishes (the server on the client's flags).
	if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy && GetLureCharacter())
	{
		// No crouch or prone in the water (T-026): the wishes are dropped, so you stand when you get out. The owning client
		// and the server both do this in the same move, so the next moves carry the cleared flags.
		if (IsSwimmingOrClimbingOut())
		{
			bWantsToCrouch = false;
			bWantsToProne = false;
		}

		const bool bIsProneNow = IsProne();
		if (bIsProneNow && (!bWantsToProne || !CanProneInCurrentState()))
		{
			UnProne(false);
		}
		else if (!bIsProneNow && bWantsToProne && CanProneInCurrentState())
		{
			Prone(false);
		}
	}

	// Engine crouch handling (calls our Crouch/UnCrouch; crouch is refused while prone).
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void ULureCharacterMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);

	// Walked off a ledge while prone: get up in the air (the wish stays, so you go prone again on landing).
	if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy && IsProne() && !CanProneInCurrentState())
	{
		UnProne(false);
	}
}

void ULureCharacterMovementComponent::CallOnStartCrouch()
{
	// Engine convention: the adjust is the class-default half height minus the crouched one.
	const float DefaultHalfHeight = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float Adjust = DefaultHalfHeight - Capsule->GetUnscaledCapsuleHalfHeight();
	CharacterOwner->OnStartCrouch(Adjust, Adjust * Capsule->GetShapeScale());
}

void ULureCharacterMovementComponent::CallOnEndCrouch()
{
	const float DefaultHalfHeight = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const FLureMovementRow& Crouched = GetStanceRow(ELureStance::Crouch);
	const float Adjust = DefaultHalfHeight - FMath::Max(Crouched.CapsuleHalfHeight, Crouched.CapsuleRadius);
	CharacterOwner->OnEndCrouch(Adjust, Adjust * CharacterOwner->GetCapsuleComponent()->GetShapeScale());
}

// ---- Capsule ----

float ULureCharacterMovementComponent::GetFeetHeight() const
{
	if (!UpdatedComponent || !CharacterOwner)
	{
		return 0.f;
	}
	const FVector Up = -GetGravityDirection();
	return (UpdatedComponent->GetComponentLocation() | Up) - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

bool ULureCharacterMovementComponent::ResizeCapsuleForStance(ELureStance Stance, bool bClientSimulation, bool bForce)
{
	if (!HasValidData())
	{
		return false;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const FLureMovementRow& Row = GetStanceRow(Stance);
	const float NewRadius = FMath::Max(Row.CapsuleRadius, 1.f);
	const float NewHalfHeight = FMath::Max(Row.CapsuleHalfHeight, NewRadius); // the engine would raise it to the radius anyway

	float OldRadius = 0.f;
	float OldHalfHeight = 0.f;
	Capsule->GetUnscaledCapsuleSize(OldRadius, OldHalfHeight);
	if (FMath::IsNearlyEqual(OldRadius, NewRadius, LureMovementPrivate::SizeTolerance) && FMath::IsNearlyEqual(OldHalfHeight, NewHalfHeight, LureMovementPrivate::SizeTolerance))
	{
		return true;
	}

	const float Scale = Capsule->GetShapeScale();
	const FVector Up = -GetGravityDirection();
	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const float OldFeetHeight = GetFeetHeight();
	const FVector MeshOffsetChange = (NewHalfHeight - OldHalfHeight) * Scale * Up;

	if (bClientSimulation)
	{
		// Simulated proxy: the server's location follows in the next update; just resize (like the engine's Crouch(true)).
		Capsule->SetCapsuleSize(NewRadius, NewHalfHeight, true);
		if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
		{
			bShrinkProxyCapsule = true;
			AdjustProxyCapsuleSize();
			if (FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
			{
				ClientData->MeshTranslationOffset += MeshOffsetChange;
				ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
			}
		}
		NotifyCapsuleResized(OldFeetHeight);
		return true;
	}

	FVector NewLocation = OldLocation;
	if (bForce)
	{
		NewLocation = OldLocation + (NewHalfHeight - OldHalfHeight) * Scale * Up; // keep the feet
	}
	else if (!FindCapsuleLocation(NewRadius, NewHalfHeight, NewLocation))
	{
		return false;
	}

	const bool bShrinksOnly = NewHalfHeight <= OldHalfHeight + UE_KINDA_SMALL_NUMBER && NewRadius <= OldRadius + UE_KINDA_SMALL_NUMBER;
	Capsule->SetCapsuleSize(NewRadius, NewHalfHeight, true);

	const FVector Delta = NewLocation - OldLocation;
	if (!Delta.IsNearlyZero())
	{
		// Not MoveUpdatedComponent: a plane constraint would stop the base from staying in place (same as the engine's crouch).
		// Shrinking stays inside the old capsule (sweep like the engine's Crouch); growing was checked for room above.
		UpdatedComponent->MoveComponent(Delta, UpdatedComponent->GetComponentQuat(), bShrinksOnly, nullptr, MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}
	bForceNextFloorCheck = true;

	// Listen server: don't smooth the remote client's mesh through the pop (engine does the same for crouch).
	if (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
	{
		if (FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
		{
			ClientData->MeshTranslationOffset += MeshOffsetChange;
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}

	NotifyCapsuleResized(OldFeetHeight);
	return true;
}

void ULureCharacterMovementComponent::NotifyCapsuleResized(float OldFeetHeight)
{
	if (ALurePlayerCharacter* LureCharacter = GetLureCharacter())
	{
		LureCharacter->HandleCapsuleResized(OldFeetHeight);
	}
}

bool ULureCharacterMovementComponent::FindCapsuleLocation(float Radius, float HalfHeight, FVector& OutLocation) const
{
	if (!HasValidData())
	{
		return false;
	}

	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float Scale = Capsule->GetShapeScale();
	const float OldScaledRadius = Capsule->GetScaledCapsuleRadius();
	const float OldScaledHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float NewScaledRadius = Radius * Scale;
	const float NewScaledHalfHeight = HalfHeight * Scale;
	const FVector Up = -GetGravityDirection();
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	const float HeightDelta = NewScaledHalfHeight - OldScaledHalfHeight;
	const FVector FeetKept = PawnLocation + HeightDelta * Up;
	const bool bGrows = NewScaledHalfHeight > OldScaledHalfHeight + UE_KINDA_SMALL_NUMBER || NewScaledRadius > OldScaledRadius + UE_KINDA_SMALL_NUMBER;

	if (bCrouchMaintainsBaseLocation)
	{
		// On the ground: the feet stay where they are.
		if (!bGrows || !IsCapsuleEncroachedAt(FeetKept, NewScaledRadius, NewScaledHalfHeight))
		{
			OutLocation = FeetKept;
			return true;
		}

		// Something barely overhead: sit right on the floor instead of hovering (engine UnCrouch trick).
		const float MinFloorDist = UE_KINDA_SMALL_NUMBER * 10.f;
		if (IsMovingOnGround() && CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
		{
			const FVector Lower = FeetKept - (CurrentFloor.FloorDist - MinFloorDist) * Up;
			if (!IsCapsuleEncroachedAt(Lower, NewScaledRadius, NewScaledHalfHeight))
			{
				OutLocation = Lower;
				return true;
			}
		}

		// Getting up next to a wall with a wider capsule: a small sideways push.
		if (NewScaledRadius > OldScaledRadius + UE_KINDA_SMALL_NUMBER)
		{
			return FindNudgedCapsuleLocation(FeetKept, NewScaledRadius, NewScaledHalfHeight, OutLocation);
		}
		return false;
	}

	// In the air: keep the center; when growing and blocked, try keeping the feet, then the head.
	if (!bGrows)
	{
		OutLocation = PawnLocation;
		return true;
	}
	// Crawled off a ledge (prone again on landing): grow upward from the feet first, so the camera, held at the prone
	// eye height during the fall, doesn't move (T-004 playtest: it rose 90 cm, then dropped 130 cm).
	const FVector CenterFirst[] = { PawnLocation, FeetKept, PawnLocation - HeightDelta * Up };
	const FVector FeetFirst[] = { FeetKept, PawnLocation, PawnLocation - HeightDelta * Up };
	const FVector (&Candidates)[3] = bWantsToProne ? FeetFirst : CenterFirst;
	for (const FVector& Candidate : Candidates)
	{
		if (!IsCapsuleEncroachedAt(Candidate, NewScaledRadius, NewScaledHalfHeight))
		{
			OutLocation = Candidate;
			return true;
		}
	}
	return false;
}

bool ULureCharacterMovementComponent::IsCapsuleEncroachedAt(const FVector& Location, float ScaledRadius, float ScaledHalfHeight) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureStanceTrace), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(Params, ResponseParam);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(ScaledRadius, ScaledHalfHeight + LureMovementPrivate::SweepInflation);
	return GetWorld()->OverlapBlockingTestByChannel(Location, GetWorldToGravityTransform(), UpdatedComponent->GetCollisionObjectType(), Shape, Params, ResponseParam);
}

float ULureCharacterMovementComponent::GetStanceNudgeLimit(float NewRadius, float OldRadius) const
{
	// Flush against walls on two sides (a corner), each wall needs the radius difference: sqrt(2) times it, plus 1 cm.
	const float CornerNeed = UE_SQRT_2 * FMath::Max(NewRadius - OldRadius, 0.f) + 1.f;
	return FMath::Max(MaxStanceNudge, CornerNeed);
}

bool ULureCharacterMovementComponent::FindNudgedCapsuleLocation(const FVector& Location, float ScaledRadius, float ScaledHalfHeight, FVector& OutLocation) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureStanceNudge), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(Params, ResponseParam);
	const FQuat Rotation = GetWorldToGravityTransform();
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(ScaledRadius, ScaledHalfHeight + LureMovementPrivate::SweepInflation);
	const FVector Up = -GetGravityDirection();

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(Overlaps, Location, Rotation, Channel, Shape, Params, ResponseParam);

	FVector Push = FVector::ZeroVector;
	bool bAnyBlocking = false;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Overlap.bBlockingHit || !Component)
		{
			continue;
		}
		FMTDResult MTD;
		if (!Component->ComputePenetration(MTD, Shape, Location, Rotation))
		{
			return false;
		}
		const FVector Step = MTD.Direction * MTD.Distance;
		if (FMath::Abs(Step | Up) > 0.5f)
		{
			return false; // a ceiling or the floor, not a wall: no sideways fix
		}
		Push += Step;
		bAnyBlocking = true;
	}
	if (!bAnyBlocking)
	{
		return false;
	}

	Push = FVector::VectorPlaneProject(Push, Up);
	const float Distance = Push.Size();
	if (Distance <= UE_KINDA_SMALL_NUMBER || Distance > GetStanceNudgeLimit(ScaledRadius, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius()))
	{
		return false;
	}

	const FVector Candidate = Location + Push + (Push / Distance) * 0.1f;
	if (IsCapsuleEncroachedAt(Candidate, ScaledRadius, ScaledHalfHeight))
	{
		return false;
	}

	// The current (smaller) capsule must be able to slide there, so the push never goes through a thin wall.
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const FCollisionShape CurrentShape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	const FVector From = UpdatedComponent->GetComponentLocation();
	if (GetWorld()->SweepTestByChannel(From, From + (Candidate - Location), Rotation, Channel, CurrentShape, Params, ResponseParam))
	{
		return false;
	}

	OutLocation = Candidate;
	return true;
}

// ---- Prediction ----

FNetworkPredictionData_Client* ULureCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		ULureCharacterMovementComponent* MutableThis = const_cast<ULureCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Lure(*this);
	}
	return ClientPredictionData;
}

// ---- Server corrections (T-026 netfix, review N2/N3) ----

void ULureCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	TGuardValue<bool> Reconciling(bReconcilingWithServer, true);
	bClientCorrectionApplied = false;

	// The engine: ack, or correction (location, velocity, mode; the replay follows in the next TickComponent).
	Super::ClientHandleMoveResponse(MoveResponse);

	// Only for a correction the engine really applied (a stale timestamp or an unresolved base is ignored), and only for
	// our own container (the one MoveResponsePacked_ClientReceive deserializes into).
	if (bClientCorrectionApplied && &MoveResponse == &LureMoveResponseData)
	{
		ApplyCorrectionClimbState(LureMoveResponseData);
	}
	bClientCorrectionApplied = false;
}

void ULureCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation,
	FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition,
	uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewMovementBaseInterfaceData, NewBaseBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
	bClientCorrectionApplied = true;
}

void ULureCharacterMovementComponent::ApplyCorrectionClimbState(const FLureMoveResponseDataContainer& Response)
{
	// Runs after the engine applied the corrected mode. Entering Falling reset the takeoff to the corrected mid-air feet
	// height, and Falling -> Falling kept the client's own; the server's value is the right one either way (N3).
	TakeoffFeetHeight = Response.TakeoffFeetHeight;

	// Corrected into a climb: continue the server's plan, so the replay climbs instead of holding still (N2).
	bHasClimbPlan = Response.bHasClimbPlan && IsClimbing();
	ClimbPlan = bHasClimbPlan ? Response.ClimbPlan : FLureClimbPlan();
}

void FLureMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	Super::ServerFillResponseData(CharacterMovement, PendingAdjustment);

	bHasClimbPlan = false;
	ClimbPlan = FLureClimbPlan();
	TakeoffFeetHeight = 0.f;

	const ULureCharacterMovementComponent* Movement = Cast<const ULureCharacterMovementComponent>(&CharacterMovement);
	if (IsCorrection() && Movement)
	{
		// The server's state now is its state after the corrected move: client moves arrive before actors tick, and the
		// reply is sent when the frame ends (the engine reads its root motion state for corrections the same way).
		bHasClimbPlan = Movement->bHasClimbPlan && Movement->IsClimbing();
		ClimbPlan = bHasClimbPlan ? Movement->ClimbPlan : FLureClimbPlan();
		TakeoffFeetHeight = Movement->TakeoffFeetHeight;
	}
}

bool FLureMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	const bool bEngineDataOk = Super::Serialize(CharacterMovement, Ar, PackageMap);

	if (!IsCorrection())
	{
		// Acks carry nothing extra (they are frequent; keep them the engine's size).
		if (Ar.IsLoading())
		{
			bHasClimbPlan = false;
			ClimbPlan = FLureClimbPlan();
			TakeoffFeetHeight = 0.f;
		}
		return bEngineDataOk;
	}

	// Full precision, like the engine's corrected location: the replay must follow the server's path exactly.
	bool bLocalSuccess = true;
	Ar.SerializeBits(&bHasClimbPlan, 1);
	if (bHasClimbPlan)
	{
		ClimbPlan.Start.NetSerialize(Ar, PackageMap, bLocalSuccess);
		ClimbPlan.RiseTo.NetSerialize(Ar, PackageMap, bLocalSuccess);
		ClimbPlan.Target.NetSerialize(Ar, PackageMap, bLocalSuccess);
		Ar << ClimbPlan.LedgeHeight;
		Ar << ClimbPlan.Speed;
		Ar.SerializeBits(&ClimbPlan.bUsesLadder, 1);
	}
	else if (Ar.IsLoading())
	{
		ClimbPlan = FLureClimbPlan();
	}
	Ar << TakeoffFeetHeight;

	return bEngineDataOk && bLocalSuccess && !Ar.IsError();
}

FSavedMove_Lure::FSavedMove_Lure()
	: bSavedWantsToSprint(false)
	, bSavedWantsToProne(false)
{
}

void FSavedMove_Lure::Clear()
{
	Super::Clear();
	bSavedWantsToSprint = false;
	bSavedWantsToProne = false;
}

uint8 FSavedMove_Lure::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToSprint)
	{
		Result |= FLAG_Sprint;
	}
	if (bSavedWantsToProne)
	{
		Result |= FLAG_Prone;
	}
	return Result;
}

bool FSavedMove_Lure::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Lure* Other = static_cast<const FSavedMove_Lure*>(NewMove.Get());
	if (!Other || bSavedWantsToSprint != Other->bSavedWantsToSprint || bSavedWantsToProne != Other->bSavedWantsToProne)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Lure::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const ULureCharacterMovementComponent* Movement = C ? Cast<ULureCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		bSavedWantsToSprint = Movement->bWantsToSprint;
		bSavedWantsToProne = Movement->bWantsToProne;
	}
}

FNetworkPredictionData_Client_Lure::FNetworkPredictionData_Client_Lure(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_Lure::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Lure());
}

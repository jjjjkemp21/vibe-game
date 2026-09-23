// Lure: surface swimming (T-026) and climbing onto edges: out of the water, or up a ledge a jump reached (T-004 B1).
// Members of ULureCharacterMovementComponent. Spec: docs/specs/swimming.md.
// Tuning: DT_Movement rows Swim / SwimSprint (SurfaceFloatDepth) and the climb rule ClimbMaxHeight / ClimbSpeed on every row.

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureLadder.h"
#include "Character/LureWaterVolume.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"

namespace LureSwimPrivate
{
	/** Radius of the probe that finds the top of an edge, cm. */
	constexpr float LedgeProbeRadius = 5.f;

	/** How far past the edge's face the probe looks for its top, cm (more than the probe radius). */
	constexpr float LedgeProbeInset = 12.f;

	/** An edge exactly at the allowed height counts; this much higher does not, cm. */
	constexpr float LedgeHeightTolerance = 0.5f;

	/** How far past the face (beyond the capsule radius) the climb puts the body, cm. */
	constexpr float TargetInset = 5.f;

	/** Gap between the feet and the edge top when the climb ends (the walking floor distance), cm. */
	constexpr float StandGap = 2.f;

	/** The path checks use a capsule this much thinner, so touching the edge's face doesn't count as blocked, cm. */
	constexpr float PathShrink = 1.f;

	/** Distance that counts as "arrived", cm. */
	constexpr float ArriveTolerance = 0.25f;

	/** A climb step that makes less than this share of its planned progress is blocked (the climb ends, you drop back). */
	constexpr float MinStepProgress = 0.1f;

	/** Critically damped spring: after SettleTime about 2% of the offset is left ((1 + x) e^-x = 0.02 at x = 5.8). */
	constexpr float SettleOmegaTimesTime = 5.8f;

	/** A landing may lift the feet this much without counting as climbing a ledge (the walking floor distance), cm. */
	constexpr float LandingLiftTolerance = 2.5f;

	/** Contact normal vs surface normal: below this dot product the capsule is touching an edge, not a surface. */
	constexpr float EdgeContactDot = 0.995f;

	/**
	 *  Climbing out also takes edge tops this much below the highest top a swimmer steps onto (feet + MaxStepHeight), so
	 *  the step range and the climb range overlap: every submerged top is either stepped onto or climbed (T-026 B3/D3).
	 */
	constexpr float StepClimbOverlap = 10.f;

	/** How close (cm) a submerged edge's face must be for swimming into it to step out onto it (touching it, in practice). */
	constexpr float StepOutReach = 10.f;

	bool IsClimbMode(uint8 CustomMode)
	{
		return CustomMode == static_cast<uint8>(ELureCustomMovementMode::ClimbOut) || CustomMode == static_cast<uint8>(ELureCustomMovementMode::LedgeClimb);
	}
}

// ---- Queries ----

bool ULureCharacterMovementComponent::IsClimbingOut() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(ELureCustomMovementMode::ClimbOut) && UpdatedComponent;
}

bool ULureCharacterMovementComponent::IsLedgeClimbing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(ELureCustomMovementMode::LedgeClimb) && UpdatedComponent;
}

bool ULureCharacterMovementComponent::ShouldFloatAtSurface() const
{
	return GetRow(GetMovementState()).SurfaceFloatDepth > 0.f;
}

bool ULureCharacterMovementComponent::IsPointInWater(const FVector& Point) const
{
	// The engine's choice of physics volume (USceneComponent::UpdatePhysicsVolume): the highest priority one holding the
	// point, else the world's default one.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const APhysicsVolume* Chosen = World->GetDefaultPhysicsVolume();
	for (auto It = World->GetNonDefaultPhysicsVolumeIterator(); It; ++It)
	{
		const APhysicsVolume* Volume = It->Get();
		if (Volume && (!Chosen || Volume->Priority > Chosen->Priority) && Volume->EncompassesPoint(Point))
		{
			Chosen = Volume;
		}
	}
	return Chosen && Chosen->bWaterVolume;
}

bool ULureCharacterMovementComponent::IsStanceTooDeepForWater(ELureStance Stance) const
{
	// On the ground a stance change keeps the feet where they are (FindCapsuleLocation), so the new capsule center is
	// feet + the stance's half height. In the air the center stays put: nothing to check there.
	if (!HasValidData() || !IsMovingOnGround())
	{
		return false;
	}
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const FLureMovementRow& Row = GetStanceRow(Stance);
	const float Radius = FMath::Max(Row.CapsuleRadius, 1.f);
	const float HalfHeight = FMath::Max(Row.CapsuleHalfHeight, Radius) * Capsule->GetShapeScale();
	const FVector Center = UpdatedComponent->GetComponentLocation() + (HalfHeight - Capsule->GetScaledCapsuleHalfHeight()) * -GetGravityDirection();
	return IsPointInWater(Center);
}

bool ULureCharacterMovementComponent::GetWaterSurfaceHeight(float& OutSurfaceZ) const
{
	const APhysicsVolume* Volume = GetPhysicsVolume();
	if (!Volume || !Volume->bWaterVolume)
	{
		return false;
	}
	if (const ALureWaterVolume* Water = Cast<ALureWaterVolume>(Volume))
	{
		OutSurfaceZ = Water->GetSurfaceHeight();
		return true;
	}
	// Any other water volume (e.g. a hand-made BSP one): the top of its bounds.
	const USceneComponent* Root = Volume->GetRootComponent();
	if (!Root)
	{
		return false;
	}
	OutSurfaceZ = static_cast<float>(Root->Bounds.Origin.Z + Root->Bounds.BoxExtent.Z);
	return true;
}

const ALureLadder* ULureCharacterMovementComponent::FindLadderAt(const FVector& Location) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ALureLadder> It(World); It; ++It)
	{
		if (IsValid(*It) && It->IsInGrabZone(Location))
		{
			return *It;
		}
	}
	return nullptr;
}

float ULureCharacterMovementComponent::GetClimbOutMaxHeight() const
{
	float MaxHeight = GetRow(IsSwimming() ? GetMovementState() : ELureMovementState::Swim).ClimbMaxHeight;
	if (UpdatedComponent)
	{
		if (const ALureLadder* Ladder = FindLadderAt(UpdatedComponent->GetComponentLocation()))
		{
			MaxHeight = FMath::Max(MaxHeight, Ladder->MaxClimbHeight);
		}
	}
	return MaxHeight;
}

float ULureCharacterMovementComponent::ComputeSurfaceFloatVelocity(float CenterZ, float VerticalSpeed, float TargetZ, float SettleTime, float DeltaTime)
{
	if (!(DeltaTime > 0.f) || !FMath::IsFinite(CenterZ) || !FMath::IsFinite(VerticalSpeed) || !FMath::IsFinite(TargetZ))
	{
		return FMath::IsFinite(VerticalSpeed) ? VerticalSpeed : 0.f;
	}
	// a = w^2 (Target - z) - 2 w v, integrated implicitly: stable for any frame time, never overshoots from rest.
	const float Omega = LureSwimPrivate::SettleOmegaTimesTime / FMath::Max(SettleTime, 0.05f);
	const float Denominator = FMath::Square(1.f + Omega * DeltaTime);
	return (VerticalSpeed + DeltaTime * Omega * Omega * (TargetZ - CenterZ)) / Denominator;
}

// ---- Finding an edge to climb ----

bool ULureCharacterMovementComponent::FindLedgePlan(const FLedgeQuery& Query, FLureClimbPlan& OutPlan) const
{
	using namespace LureSwimPrivate;

	if (!HasValidData() || !(Query.MaxHeight > 0.f) || !(Query.Speed > 0.f) || Query.Forward.IsNearlyZero())
	{
		return false;
	}

	const FVector Location = UpdatedComponent->GetComponentLocation();
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = Query.Forward;

	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureLedgeClimb), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
	const FCollisionShape PathShape = FCollisionShape::MakeCapsule(FMath::Max(Radius - PathShrink, 1.f), FMath::Max(HalfHeight - PathShrink, 1.f));

	// 1. The edge's face: within reach in front of the body (at a ladder: the ladder itself).
	FVector FacePoint = FVector::ZeroVector;
	if (Query.Ladder)
	{
		FacePoint = Query.Ladder->GetActorLocation();
	}
	else
	{
		FHitResult FaceHit;
		if (!World->SweepSingleByChannel(FaceHit, Location, Location + Forward * Query.FaceReach, FQuat::Identity, Channel, PathShape, Params, Response)
			|| FaceHit.bStartPenetrating)
		{
			return false;
		}
		FacePoint = FaceHit.ImpactPoint;
	}

	// 2. The edge's top: a small probe comes down just past the face from the highest allowed height. If it starts inside
	//    something, the edge is too high; if it finds nothing above the lowest allowed top, there is no edge.
	const float TopLimitZ = FMath::Min(Query.ReferenceZ + Query.MaxHeight, Query.HighestTopZ) + LedgeHeightTolerance;
	if (TopLimitZ <= Query.LowestTopZ)
	{
		return false;
	}
	const FVector ProbeXY = FacePoint + Forward * LedgeProbeInset;
	const FVector ProbeStart(ProbeXY.X, ProbeXY.Y, TopLimitZ + LedgeProbeRadius);
	const FVector ProbeEnd(ProbeXY.X, ProbeXY.Y, Query.LowestTopZ);
	FHitResult TopHit;
	if (!World->SweepSingleByChannel(TopHit, ProbeStart, ProbeEnd, FQuat::Identity, Channel, FCollisionShape::MakeSphere(LedgeProbeRadius), Params, Response)
		|| TopHit.bStartPenetrating || !IsWalkable(TopHit))
	{
		return false;
	}
	const float LedgeZ = static_cast<float>(TopHit.ImpactPoint.Z);
	if (LedgeZ > TopLimitZ || LedgeZ < Query.LowestTopZ)
	{
		return false;
	}

	// 3. Room to stand on it, with a floor under the feet.
	FVector Target = FacePoint + Forward * (Query.TargetRadius + TargetInset);
	Target.Z = LedgeZ + Query.TargetHalfHeight + StandGap;
	FHitResult FloorHit;
	const FVector FloorEnd = Target - FVector(0.f, 0.f, Query.TargetHalfHeight + StandGap + 20.f);
	if (!World->LineTraceSingleByChannel(FloorHit, Target, FloorEnd, Channel, Params, Response) || FloorHit.bStartPenetrating || !IsWalkable(FloorHit)
		|| FloorHit.ImpactPoint.Z > LedgeZ + LedgeHeightTolerance + 1.f)
	{
		return false;
	}
	Target.Z = FloorHit.ImpactPoint.Z + Query.TargetHalfHeight + StandGap;
	if (IsCapsuleEncroachedAt(Target, Query.TargetRadius, Query.TargetHalfHeight))
	{
		return false;
	}

	// 4. A clear path: straight up along the face, then across onto the edge.
	const FVector RiseTo(Location.X, Location.Y, FMath::Max(Target.Z, Location.Z));
	if (World->SweepTestByChannel(Location, RiseTo, FQuat::Identity, Channel, PathShape, Params, Response)
		|| World->SweepTestByChannel(RiseTo, Target, FQuat::Identity, Channel, PathShape, Params, Response))
	{
		return false;
	}

	OutPlan.Start = Location;
	OutPlan.RiseTo = RiseTo;
	OutPlan.Target = Target;
	OutPlan.LedgeHeight = LedgeZ - Query.ReferenceZ;
	OutPlan.Speed = Query.Speed;
	OutPlan.bUsesLadder = false;
	return true;
}

bool ULureCharacterMovementComponent::FindClimbOutPlan(FLureClimbPlan& OutPlan) const
{
	if (!HasValidData() || !IsSwimming())
	{
		return false;
	}
	float SurfaceZ = 0.f;
	if (!GetWaterSurfaceHeight(SurfaceZ))
	{
		return false;
	}

	const FLureMovementRow& Row = GetRow(GetMovementState());
	const float Scale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const FLureMovementRow& Stand = GetStanceRow(ELureStance::Stand);

	// Only from the surface (T-026 D2): the head at most ClimbOutSurfaceTolerance below the water. A floating swimmer's
	// head is always above it; this matters for rows without the surface float (diving, later).
	const float HeadZ = static_cast<float>(UpdatedComponent->GetComponentLocation().Z) + CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (HeadZ < SurfaceZ - Row.ClimbOutSurfaceTolerance)
	{
		return false;
	}

	FLedgeQuery Query;
	Query.Forward = FVector(CharacterOwner->GetActorForwardVector().X, CharacterOwner->GetActorForwardVector().Y, 0.0).GetSafeNormal();
	Query.ReferenceZ = SurfaceZ;
	Query.MaxHeight = Row.ClimbMaxHeight;
	// Lower tops are the seabed (walk out there): below ClimbOutLowestTop, and within a step of the feet. Tops too high to
	// step onto are always climbable, whatever the column says, so no shelf is a wall from the water (T-026 B3/D3).
	const float StepReachZ = GetFeetHeight() + MaxStepHeight - LureSwimPrivate::StepClimbOverlap;
	Query.LowestTopZ = FMath::Min(SurfaceZ + FMath::Min(Row.ClimbOutLowestTop, 0.f), StepReachZ);
	Query.FaceReach = Row.ClimbOutReach;
	Query.TargetRadius = FMath::Max(Stand.CapsuleRadius, 1.f) * Scale; // you get out standing
	Query.TargetHalfHeight = FMath::Max(Stand.CapsuleHalfHeight, Stand.CapsuleRadius) * Scale;
	Query.Speed = Row.ClimbSpeed;

	// At a ladder: climb into the dock whichever way you face, as high as the ladder goes.
	bool bUsesLadder = false;
	if (const ALureLadder* Ladder = FindLadderAt(UpdatedComponent->GetComponentLocation()))
	{
		Query.Ladder = Ladder;
		Query.Forward = Ladder->GetClimbDirection();
		if (Ladder->ClimbSpeed > 0.f)
		{
			Query.Speed = Ladder->ClimbSpeed;
		}
		if (Ladder->MaxClimbHeight > Query.MaxHeight)
		{
			Query.MaxHeight = Ladder->MaxClimbHeight;
			bUsesLadder = true;
		}
	}

	if (!FindLedgePlan(Query, OutPlan))
	{
		return false;
	}
	OutPlan.bUsesLadder = bUsesLadder;
	return true;
}

bool ULureCharacterMovementComponent::FindJumpClimbPlan(FLureClimbPlan& OutPlan) const
{
	// Only after a jump, on the way down, while moving: a rising jump clears what it can by itself.
	if (!HasValidData() || !IsFalling() || CharacterOwner->JumpCurrentCount <= 0 || Velocity.Z > 0.f)
	{
		return false;
	}
	const FVector InputDirection = Acceleration.GetSafeNormal2D();
	if (InputDirection.IsNearlyZero())
	{
		return false;
	}

	const FLureMovementRow& Row = GetRow(GetMovementState());
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float Feet = GetFeetHeight();

	FLedgeQuery Query;
	Query.Forward = InputDirection;
	Query.ReferenceZ = TakeoffFeetHeight;
	Query.MaxHeight = Row.ClimbMaxHeight;
	Query.LowestTopZ = Feet + 1.f;											// a ledge the feet are below (else you just land on it)
	Query.HighestTopZ = Feet + Capsule->GetScaledCapsuleRadius();			// that the jump actually reached
	Query.FaceReach = JumpClimbReach;
	Query.TargetRadius = Capsule->GetScaledCapsuleRadius();					// same posture on top
	Query.TargetHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	Query.Speed = Row.ClimbSpeed;
	return FindLedgePlan(Query, OutPlan);
}

// ---- Starting a climb ----

void ULureCharacterMovementComponent::StartClimb(const FLureClimbPlan& Plan, ELureCustomMovementMode Mode)
{
	ClimbPlan = Plan;
	bHasClimbPlan = true;
	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Custom, static_cast<uint8>(Mode));
}

bool ULureCharacterMovementComponent::TryStartClimbOut()
{
	FLureClimbPlan Plan;
	if (!FindClimbOutPlan(Plan))
	{
		return false;
	}
	StartClimb(Plan, ELureCustomMovementMode::ClimbOut);
	return true;
}

bool ULureCharacterMovementComponent::FindStepOutPlan(float SurfaceZ, FLureClimbPlan& OutPlan) const
{
	// Only while pushing toward it, like the jump climb.
	const FVector InputDirection = Acceleration.GetSafeNormal2D();
	if (!HasValidData() || !IsSwimming() || InputDirection.IsNearlyZero() || !(MaxStepHeight > 0.f))
	{
		return false;
	}
	const FLureMovementRow& Row = GetRow(GetMovementState());
	const FLureMovementRow& Stand = GetStanceRow(ELureStance::Stand);
	const float Scale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float Feet = GetFeetHeight();

	FLedgeQuery Query;
	Query.Forward = InputDirection;
	Query.ReferenceZ = Feet;
	Query.MaxHeight = MaxStepHeight;
	Query.FaceReach = LureSwimPrivate::StepOutReach;
	Query.TargetRadius = FMath::Max(Stand.CapsuleRadius, 1.f) * Scale; // you get out standing
	Query.TargetHalfHeight = FMath::Max(Stand.CapsuleHalfHeight, Stand.CapsuleRadius) * Scale;
	// Tops above the feet where you stand with the capsule center out of the water (wading). Lower ones are swum over.
	Query.LowestTopZ = FMath::Max(Feet + 1.f, SurfaceZ - Query.TargetHalfHeight - LureSwimPrivate::StandGap + 1.f);
	Query.Speed = Row.ClimbSpeed;
	return FindLedgePlan(Query, OutPlan);
}

bool ULureCharacterMovementComponent::TryStartStepOut(float SurfaceZ)
{
	FLureClimbPlan Plan;
	if (!FindStepOutPlan(SurfaceZ, Plan))
	{
		return false;
	}
	StartClimb(Plan, ELureCustomMovementMode::ClimbOut);
	return true;
}

bool ULureCharacterMovementComponent::TryStartJumpClimb()
{
	FLureClimbPlan Plan;
	if (!FindJumpClimbPlan(Plan))
	{
		return false;
	}
	StartClimb(Plan, ELureCustomMovementMode::LedgeClimb);
	return true;
}

bool ULureCharacterMovementComponent::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	if (!Super::IsValidLandingSpot(CapsuleLocation, Hit))
	{
		return false;
	}
	if (!IsFalling() || !CharacterOwner)
	{
		return true;
	}
	// The climb rule (T-004 B1). A row without one (ClimbMaxHeight 0 or missing) lands like the engine: nothing refused.
	const float ClimbMaxHeight = GetRow(GetMovementState()).ClimbMaxHeight;
	if (ClimbMaxHeight <= 0.f)
	{
		return true;
	}
	// Landings from above are never refused. A landing that LIFTS the feet (the rounded
	// capsule bottom catching something above them) is refused when it is higher than ClimbMaxHeight above the takeoff,
	// and whenever it is a ledge's edge rather than a slope: the jump climb then pulls you up onto ledges within the
	// rule (PhysFalling), so you never hang perched below a ledge top and every edge shape behaves the same.
	const float Feet = static_cast<float>(CapsuleLocation.Z) - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float FloorZ = static_cast<float>(Hit.ImpactPoint.Z);
	if (FloorZ <= Feet + LureSwimPrivate::LandingLiftTolerance)
	{
		return true;
	}
	if (FloorZ > TakeoffFeetHeight + ClimbMaxHeight + LureSwimPrivate::LandingLiftTolerance)
	{
		return false;
	}
	const bool bEdgeContact = FVector::DotProduct(Hit.Normal, Hit.ImpactNormal) < LureSwimPrivate::EdgeContactDot;
	return !bEdgeContact;
}

// ---- Physics ----

void ULureCharacterMovementComponent::OnTeleported()
{
	// A teleport during a climb ends it (T-026 B1): without this the climb kept its plan and dragged the character back
	// toward the old edge, because the engine leaves a custom mode alone. Pick the mode for the new place here: in water
	// straight to swimming (the climb counts as in the water, so no in/out blip), else falling, which the engine turns
	// into walking when there is ground right below. Other players' copies never climb on a plan: their mode is replicated.
	if (HasValidData() && IsClimbing() && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		bHasClimbPlan = false;
		ClimbPlan = FLureClimbPlan();
		Velocity = FVector::ZeroVector;
		SetMovementMode((CanEverSwim() && IsInWater()) ? DefaultWaterMovementMode.GetValue() : MOVE_Falling);
	}
	Super::OnTeleported();
}

void ULureCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (MovementMode != MOVE_Custom || !LureSwimPrivate::IsClimbMode(CustomMovementMode))
	{
		bHasClimbPlan = false;
	}
	if (MovementMode == MOVE_Falling && PreviousMovementMode != MOVE_Falling)
	{
		TakeoffFeetHeight = GetFeetHeight(); // where the jump or fall started: the climb rule's zero
	}
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void ULureCharacterMovementComponent::PhysSwimming(float DeltaTime, int32 Iterations)
{
	// Jump was pressed this move with an edge to climb (DoJump queued it): the climb starts here, inside the move, the
	// same way on the owning client, on the server (from the move's Jump flag) and in replays.
	if (bClimbOutRequested)
	{
		bClimbOutRequested = false;
		if (TryStartClimbOut())
		{
			StartNewPhysics(DeltaTime, Iterations);
			return;
		}
	}

	float SurfaceZ = 0.f;
	if (ShouldFloatAtSurface() && GetWaterSurfaceHeight(SurfaceZ))
	{
		PhysSurfaceSwimming(DeltaTime, Iterations, SurfaceZ);
		return;
	}
	// No surface rule for this row (a future diving row): the engine's free 3D swimming with buoyancy.
	Super::PhysSwimming(DeltaTime, Iterations);
}

void ULureCharacterMovementComponent::PhysSurfaceSwimming(float DeltaTime, int32 Iterations, float SurfaceZ)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	RestorePreAdditiveRootMotionVelocity();
	Iterations++;
	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	bJustTeleported = false;
	bool bSteppedUp = false;

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		// Horizontal: the swim input (first person moves along the view's yaw), fluid friction, the row's speeds.
		const float VerticalSpeed = static_cast<float>(Velocity.Z);
		Acceleration.Z = 0.f;
		Velocity.Z = 0.f;
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction;
		CalcVelocity(DeltaTime, Friction, true, GetMaxBrakingDeceleration());

		// Vertical: the surface float, the one "stay at the surface" rule (row SurfaceFloatDepth).
		const FLureMovementRow& Row = GetRow(GetMovementState());
		const float TargetZ = SurfaceZ - Row.SurfaceFloatDepth;
		Velocity.Z = ComputeSurfaceFloatVelocity(static_cast<float>(OldLocation.Z), VerticalSpeed, TargetZ, Row.SurfaceFloatSettleTime, DeltaTime);
	}
	ApplyRootMotionToVelocity(DeltaTime);
	const float PlannedVerticalSpeed = static_cast<float>(Velocity.Z);

	const FVector Delta = Velocity * DeltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

	// Pushed out of the water (up a beach or onto a shelf): the engine switched to falling; land from there.
	if (!IsSwimming())
	{
		StartNewPhysics(DeltaTime * (1.f - Hit.Time), Iterations);
		return;
	}

	if (Hit.Time < 1.f)
	{
		const FVector GravityDir(0.f, 0.f, -1.f);
		const float UpDown = static_cast<float>(GravityDir | Velocity.GetSafeNormal());
		const bool bLevelMove = UpDown < 0.5f && UpDown > -0.2f;

		// Swimming into a submerged edge within a step of the feet, with room to stand on it head out of the water: step
		// out onto it (T-026 QA B3). The contact is a wall, or the edge caught by the capsule's rounded bottom (the engine
		// reports a floor-like normal there). Slopes (beaches) are not edges: you walk out there as before.
		const bool bWallOrEdge = FMath::Abs(Hit.ImpactNormal.Z) < 0.2f || FVector::DotProduct(Hit.Normal, Hit.ImpactNormal) < LureSwimPrivate::EdgeContactDot;
		if (bWallOrEdge && bLevelMove && TryStartStepOut(SurfaceZ))
		{
			StartNewPhysics(DeltaTime * (1.f - Hit.Time), Iterations);
			return;
		}

		// Other low steps (rocks deeper under the surface) are stepped over like the engine's swimming; the rest is slid along.
		if (FMath::Abs(Hit.ImpactNormal.Z) < 0.2f && bLevelMove && CanStepUp(Hit))
		{
			const FVector RealVelocity = Velocity;
			Velocity.Z = 1.f; // the engine's trick: moving up, in case the step takes us out of the water
			bSteppedUp = StepUp(GravityDir, Delta * (1.f - Hit.Time), Hit);
			Velocity = RealVelocity;
		}
		if (!bSteppedUp && IsSwimming())
		{
			HandleImpact(Hit, DeltaTime, Delta);
			SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
		}
		if (!IsSwimming())
		{
			StartNewPhysics(0.f, Iterations);
			return;
		}
	}

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bJustTeleported)
	{
		// What really happened (walls, the seabed and steps).
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
		if (bSteppedUp)
		{
			// A step moves the capsule; it never becomes upward speed. A 17 cm step in one frame was 1000 cm/s, and the
			// float spring then threw the swimmer metres out of the water (T-026 B3).
			Velocity.Z = FMath::Min(static_cast<float>(Velocity.Z), PlannedVerticalSpeed);
		}
	}
}

void ULureCharacterMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
	// A jump that reached a ledge within the climb rule pulls you up onto it (consistent for any edge shape).
	if (DeltaTime >= MIN_TICK_TIME && TryStartJumpClimb())
	{
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}
	Super::PhysFalling(DeltaTime, Iterations);
}

void ULureCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (LureSwimPrivate::IsClimbMode(CustomMovementMode))
	{
		PhysClimb(DeltaTime, Iterations);
		return;
	}
	Super::PhysCustom(DeltaTime, Iterations);
}

void ULureCharacterMovementComponent::PhysClimb(float DeltaTime, int32 Iterations)
{
	using namespace LureSwimPrivate;

	if (DeltaTime < MIN_TICK_TIME || !CharacterOwner)
	{
		return;
	}

	// Other players' copies (simulated proxies): the engine runs PhysCustom for them too (SimulateMovement -> MoveSmooth),
	// and they never have a plan. Follow the replicated velocity and never change the mode here: the server's next
	// replicated mode (Walking, or Falling if the climb was blocked) ends the climb. Changing it here would flip the copy
	// between Falling and the replicated climb on every net update, and fire OnSwimStateChanged each time (review N1).
	// Pattern: every PhysCustom sub-mode needs a simulated-proxy branch like this one.
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		const FVector ProxyDelta = Velocity * DeltaTime;
		if (!ProxyDelta.IsNearlyZero())
		{
			FHitResult Hit(1.f);
			SafeMoveUpdatedComponent(ProxyDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
			if (Hit.IsValidBlockingHit() && Hit.Time < 1.f)
			{
				SlideAlongSurface(ProxyDelta, 1.f - Hit.Time, Hit.Normal, Hit, false);
			}
		}
		return;
	}

	if (!bHasClimbPlan)
	{
		// Last resort: corrections carry the server's plan (FLureMoveResponseDataContainer), so an owning client should
		// never be here. If it is, hold still: the server finishes the climb and its next correction carries the plan.
		if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy)
		{
			Velocity = FVector::ZeroVector;
			return;
		}
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	Iterations++;
	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FQuat Rotation = UpdatedComponent->GetComponentQuat();
	float Budget = ClimbPlan.Speed * DeltaTime;
	bool bBlocked = false;

	// Moves Delta with a slide along whatever it touches; false if it made (almost) no progress along Delta.
	auto MoveAlong = [this, &Rotation](const FVector& Delta)
	{
		const FVector Start = UpdatedComponent->GetComponentLocation();
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent(Delta, Rotation, true, Hit);
		if (Hit.IsValidBlockingHit() && Hit.Time < 1.f)
		{
			SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, false);
		}
		const float Wanted = static_cast<float>(Delta.Size());
		const float Made = static_cast<float>((UpdatedComponent->GetComponentLocation() - Start) | Delta.GetSafeNormal());
		return Made >= MinStepProgress * Wanted;
	};

	// 1. Straight up along the edge.
	const float Rise = static_cast<float>(ClimbPlan.RiseTo.Z - OldLocation.Z);
	if (Rise > ArriveTolerance)
	{
		const float Step = FMath::Min(Budget, Rise);
		bBlocked |= !MoveAlong(FVector(0.f, 0.f, Step));
		Budget -= Step;
	}

	// 2. Across onto the edge.
	FVector Location = UpdatedComponent->GetComponentLocation();
	if (!bBlocked && Budget > 0.f && ClimbPlan.RiseTo.Z - Location.Z <= ArriveTolerance)
	{
		const FVector Across = ClimbPlan.Target - Location;
		const float Distance = static_cast<float>(Across.Size());
		if (Distance > ArriveTolerance)
		{
			bBlocked |= !MoveAlong(Across / Distance * FMath::Min(Budget, Distance));
		}
		Location = UpdatedComponent->GetComponentLocation();
	}

	// Never faster than the climb (a slide along the edge can add a hair; a step out of the water must not read as a launch).
	Velocity = ((Location - OldLocation) / DeltaTime).GetClampedToMaxSize(ClimbPlan.Speed);

	if (bBlocked)
	{
		// Something got in the way: let go (you drop back and can try again).
		bHasClimbPlan = false;
		SetMovementMode(MOVE_Falling);
		return;
	}
	if ((ClimbPlan.Target - Location).Size() <= ArriveTolerance)
	{
		bHasClimbPlan = false;
		Velocity = FVector::ZeroVector;
		SetMovementMode(MOVE_Walking);
	}
}

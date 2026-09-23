// Lure: surface swimming and climbing out of the water (T-026). Members of ULureCharacterMovementComponent.
// Spec: docs/specs/swimming.md. Tuning: DT_Movement rows Swim / SwimSprint (SurfaceFloatDepth, ClimbOutMaxHeight, ClimbOutSpeed).

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

	/** Edge tops lower than this (cm relative to the water surface) are the seabed: walk out there instead. */
	constexpr float LowestLedgeHeight = -20.f;

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
}

// ---- Queries ----

bool ULureCharacterMovementComponent::IsClimbingOut() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(ELureCustomMovementMode::ClimbOut) && UpdatedComponent;
}

bool ULureCharacterMovementComponent::ShouldFloatAtSurface() const
{
	return GetRow(GetMovementState()).SurfaceFloatDepth > 0.f;
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
	float MaxHeight = GetRow(IsSwimming() ? GetMovementState() : ELureMovementState::Swim).ClimbOutMaxHeight;
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

// ---- Climbing out ----

bool ULureCharacterMovementComponent::FindClimbOutPlan(FLureClimbOutPlan& OutPlan) const
{
	using namespace LureSwimPrivate;

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
	const FVector Location = UpdatedComponent->GetComponentLocation();
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Scale = Capsule->GetShapeScale();
	const FLureMovementRow& Stand = GetStanceRow(ELureStance::Stand);
	const float StandRadius = FMath::Max(Stand.CapsuleRadius, 1.f) * Scale;
	const float StandHalfHeight = FMath::Max(Stand.CapsuleHalfHeight, Stand.CapsuleRadius) * Scale;

	// Where to look: in front of the body, or into the dock at a ladder.
	float MaxHeight = Row.ClimbOutMaxHeight;
	float Speed = Row.ClimbOutSpeed;
	FVector Forward = FVector(CharacterOwner->GetActorForwardVector().X, CharacterOwner->GetActorForwardVector().Y, 0.0).GetSafeNormal();
	const ALureLadder* Ladder = FindLadderAt(Location);
	bool bUsesLadder = false;
	if (Ladder)
	{
		Forward = Ladder->GetClimbDirection();
		if (Ladder->ClimbSpeed > 0.f)
		{
			Speed = Ladder->ClimbSpeed;
		}
		if (Ladder->MaxClimbHeight > MaxHeight)
		{
			MaxHeight = Ladder->MaxClimbHeight;
			bUsesLadder = true;
		}
	}
	if (!(MaxHeight > 0.f) || !(Speed > 0.f) || Forward.IsNearlyZero())
	{
		return false;
	}

	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureClimbOut), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
	const FCollisionShape PathShape = FCollisionShape::MakeCapsule(FMath::Max(Radius - PathShrink, 1.f), FMath::Max(HalfHeight - PathShrink, 1.f));

	// 1. The edge's face: within reach in front of the body (at a ladder: the ladder itself).
	FVector FacePoint = FVector::ZeroVector;
	if (Ladder)
	{
		FacePoint = Ladder->GetActorLocation();
	}
	else
	{
		FHitResult FaceHit;
		if (!World->SweepSingleByChannel(FaceHit, Location, Location + Forward * ClimbOutReach, FQuat::Identity, Channel, PathShape, Params, Response)
			|| FaceHit.bStartPenetrating)
		{
			return false;
		}
		FacePoint = FaceHit.ImpactPoint;
	}

	// 2. The edge's top: a small probe comes down just past the face from the highest allowed edge height. If it starts
	//    inside something, the edge is too high; if it finds nothing above the seabed line, there is no edge.
	const FVector ProbeXY = FacePoint + Forward * LedgeProbeInset;
	const float TopLimitZ = SurfaceZ + MaxHeight + LedgeHeightTolerance;
	const FVector ProbeStart(ProbeXY.X, ProbeXY.Y, TopLimitZ + LedgeProbeRadius);
	const FVector ProbeEnd(ProbeXY.X, ProbeXY.Y, SurfaceZ + LowestLedgeHeight);
	FHitResult TopHit;
	if (!World->SweepSingleByChannel(TopHit, ProbeStart, ProbeEnd, FQuat::Identity, Channel, FCollisionShape::MakeSphere(LedgeProbeRadius), Params, Response)
		|| TopHit.bStartPenetrating || !IsWalkable(TopHit))
	{
		return false;
	}
	const float LedgeZ = static_cast<float>(TopHit.ImpactPoint.Z);
	if (LedgeZ - SurfaceZ > MaxHeight + LedgeHeightTolerance)
	{
		return false;
	}

	// 3. Room to stand on it, with a floor under the feet.
	FVector Target = FacePoint + Forward * (StandRadius + TargetInset);
	Target.Z = LedgeZ + StandHalfHeight + StandGap;
	FHitResult FloorHit;
	const FVector FloorEnd = Target - FVector(0.f, 0.f, StandHalfHeight + StandGap + 20.f);
	if (!World->LineTraceSingleByChannel(FloorHit, Target, FloorEnd, Channel, Params, Response) || FloorHit.bStartPenetrating || !IsWalkable(FloorHit)
		|| FloorHit.ImpactPoint.Z > LedgeZ + LedgeHeightTolerance + 1.f)
	{
		return false;
	}
	Target.Z = FloorHit.ImpactPoint.Z + StandHalfHeight + StandGap;
	if (IsCapsuleEncroachedAt(Target, StandRadius, StandHalfHeight))
	{
		return false;
	}

	// 4. A clear path: straight up along the face, then across onto the edge.
	const FVector RiseTo(Location.X, Location.Y, Target.Z);
	if (World->SweepTestByChannel(Location, RiseTo, FQuat::Identity, Channel, PathShape, Params, Response)
		|| World->SweepTestByChannel(RiseTo, Target, FQuat::Identity, Channel, PathShape, Params, Response))
	{
		return false;
	}

	OutPlan.Start = Location;
	OutPlan.RiseTo = RiseTo;
	OutPlan.Target = Target;
	OutPlan.LedgeHeight = LedgeZ - SurfaceZ;
	OutPlan.Speed = Speed;
	OutPlan.bUsesLadder = bUsesLadder;
	return true;
}

bool ULureCharacterMovementComponent::TryStartClimbOut()
{
	FLureClimbOutPlan Plan;
	if (!FindClimbOutPlan(Plan))
	{
		return false;
	}
	ClimbOutPlan = Plan;
	bHasClimbOutPlan = true;
	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Custom, static_cast<uint8>(ELureCustomMovementMode::ClimbOut));
	return true;
}

// ---- Physics ----

void ULureCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (!IsClimbingOut())
	{
		bHasClimbOutPlan = false;
	}
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void ULureCharacterMovementComponent::PhysSwimming(float DeltaTime, int32 Iterations)
{
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

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		// Horizontal: the swim input (first person moves along the view's yaw), fluid friction, the row's speeds.
		const float VerticalSpeed = static_cast<float>(Velocity.Z);
		Acceleration.Z = 0.f;
		Velocity.Z = 0.f;
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction;
		CalcVelocity(DeltaTime, Friction, true, GetMaxBrakingDeceleration());

		// Vertical: the surface float, the one "stay at the surface" rule (row SurfaceFloatDepth).
		const float TargetZ = SurfaceZ - GetRow(GetMovementState()).SurfaceFloatDepth;
		Velocity.Z = ComputeSurfaceFloatVelocity(static_cast<float>(OldLocation.Z), VerticalSpeed, TargetZ, SurfaceFloatSettleTime, DeltaTime);
	}
	ApplyRootMotionToVelocity(DeltaTime);

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
		// Low steps (rocks under the surface) are stepped over, like the engine's swimming; everything else is slid along.
		const FVector GravityDir(0.f, 0.f, -1.f);
		const float UpDown = static_cast<float>(GravityDir | Velocity.GetSafeNormal());
		bool bSteppedUp = false;
		if (FMath::Abs(Hit.ImpactNormal.Z) < 0.2f && UpDown < 0.5f && UpDown > -0.2f && CanStepUp(Hit))
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
	}
}

void ULureCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (CustomMovementMode == static_cast<uint8>(ELureCustomMovementMode::ClimbOut))
	{
		PhysClimbOut(DeltaTime, Iterations);
		return;
	}
	Super::PhysCustom(DeltaTime, Iterations);
}

void ULureCharacterMovementComponent::PhysClimbOut(float DeltaTime, int32 Iterations)
{
	using namespace LureSwimPrivate;

	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}
	if (!bHasClimbOutPlan)
	{
		// Only a server correction can put an owning client here without a plan: hold still, the server finishes the climb.
		if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy)
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
	float Budget = ClimbOutPlan.Speed * DeltaTime;
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
	const float Rise = static_cast<float>(ClimbOutPlan.RiseTo.Z - OldLocation.Z);
	if (Rise > ArriveTolerance)
	{
		const float Step = FMath::Min(Budget, Rise);
		bBlocked |= !MoveAlong(FVector(0.f, 0.f, Step));
		Budget -= Step;
	}

	// 2. Across onto the edge.
	FVector Location = UpdatedComponent->GetComponentLocation();
	if (!bBlocked && Budget > 0.f && ClimbOutPlan.RiseTo.Z - Location.Z <= ArriveTolerance)
	{
		const FVector Across = ClimbOutPlan.Target - Location;
		const float Distance = static_cast<float>(Across.Size());
		if (Distance > ArriveTolerance)
		{
			bBlocked |= !MoveAlong(Across / Distance * FMath::Min(Budget, Distance));
		}
		Location = UpdatedComponent->GetComponentLocation();
	}

	Velocity = (Location - OldLocation) / DeltaTime;

	if (bBlocked)
	{
		// Something got in the way: let go (you drop back into the water and can try again).
		bHasClimbOutPlan = false;
		SetMovementMode(MOVE_Falling);
		return;
	}
	if ((ClimbOutPlan.Target - Location).Size() <= ArriveTolerance)
	{
		bHasClimbOutPlan = false;
		Velocity = FVector::ZeroVector;
		SetMovementMode(MOVE_Walking);
	}
}

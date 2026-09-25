// Lure: the fish you see fighting on the line (T-029).

#include "Fish/LureFightFish.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "ReferenceSkeleton.h"

ALureFightFish::ALureFightFish()
{
	PrimaryActorTick.bCanEverTick = false; // ULureFightFishSubsystem moves it after the fight and the characters have ticked
	bReplicates = false;                   // every machine spawns its own from the replicated fight
	SetCanBeDamaged(false);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->bReceivesDecals = false;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

void ALureFightFish::Setup(const FLureFightFishSetup& InSetup)
{
	Setup_ = InSetup;
	const FFishVisualRow& Row = Setup_.Row;
	FishScale = FFightFishVisual::WeightScale(Setup_.Fish.WeightKg, Setup_.ReferenceWeightKg, Row);
	SetActorScale3D(FVector(FishScale));

	BodyLengthCm = Row.DefaultBodyLengthCm;
	MouthOffsetCm = 0.5f * BodyLengthCm;
	if (Setup_.Mesh)
	{
		Mesh->SetSkeletalMesh(Setup_.Mesh);
		const FBoxSphereBounds Bounds = Setup_.Mesh->GetImportedBounds();
		if (Bounds.BoxExtent.X > 0.5)
		{
			BodyLengthCm = static_cast<float>(2.0 * Bounds.BoxExtent.X);
			MouthOffsetCm = 0.5f * BodyLengthCm;
		}
		const FReferenceSkeleton& Skeleton = Setup_.Mesh->GetRefSkeleton();
		const int32 MouthIndex = Row.MouthBone.IsNone() ? INDEX_NONE : Skeleton.FindBoneIndex(Row.MouthBone);
		if (MouthIndex != INDEX_NONE)
		{
			// Component space of the bone: its local transform times every parent's (the fishkit bones are unrotated).
			FTransform Pose = FTransform::Identity;
			for (int32 Index = MouthIndex; Index != INDEX_NONE; Index = Skeleton.GetParentIndex(Index))
			{
				Pose = Pose * Skeleton.GetRefBonePose()[Index];
			}
			MouthOffsetCm = static_cast<float>(Pose.GetLocation().X);
		}
		else if (Row.MouthBone.IsNone())
		{
			MouthOffsetCm = 0.f;
		}
		if (Setup_.AnimClass)
		{
			Mesh->SetAnimInstanceClass(Setup_.AnimClass);
		}
		Mesh->SetVisibility(true);
	}
	else
	{
		Mesh->SetVisibility(false);
	}

	Phase = EFightFishPhase::Fighting;
	Seconds = 0.f;
	Stamina01 = 1.f;
	EndSeconds = 0.f;
	bPlaced = false;
	Velocity = FVector::ZeroVector;
	UpdateAnim(EFightFishPhase::Fighting, NAME_None, FVector::ZeroVector);
}

float ALureFightFish::TraceFloorZ(const FVector& At, float WaterZ, float BelowZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return -UE_BIG_NUMBER;
	}
	const FFishVisualRow& Row = Setup_.Row;
	// From just under the surface (dock planks above the water don't count) down past the deepest the fish is shown.
	const FVector Start(At.X, At.Y, WaterZ - 1.f);
	const float EndZ = FMath::Min(WaterZ - (Row.SurfaceDepth + Row.MaxShownDepth + Row.FloorClearance + 100.f), BelowZ - Row.FloorClearance - 100.f);
	const FVector End(At.X, At.Y, EndZ);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureFightFishFloor), false, this);
	return FLureFishingSpots::TraceCast(World, Hit, Start, End, Params) ? static_cast<float>(Hit.ImpactPoint.Z) : -UE_BIG_NUMBER;
}

void ALureFightFish::MoveTo(const FVector& Target, const FVector& PlayerLocation, float DeltaTime, float SmoothTime, bool bSnap)
{
	const FFishVisualRow& Row = Setup_.Row;
	const FVector Before = GetActorLocation();
	const FVector New = bSnap ? Target : FMath::Lerp(Before, Target, FFightFishVisual::SmoothAlpha(DeltaTime, SmoothTime));
	Velocity = (bSnap || DeltaTime <= 0.f) ? FVector::ZeroVector : (New - Before) / DeltaTime;
	const FRotator Desired = FFightFishVisual::FacingRotation(Row, Velocity, New, PlayerLocation, GetActorRotation(), bExhausted);
	const FQuat Rotation = bSnap ? Desired.Quaternion()
		: FQuat::Slerp(GetActorQuat(), Desired.Quaternion(), FFightFishVisual::SmoothAlpha(DeltaTime, Row.RotationSmoothTime));
	SetActorLocationAndRotation(New, Rotation);
}

void ALureFightFish::ApplyView(const FFightFishView& View, float DeltaTime)
{
	if (Phase != EFightFishPhase::Fighting)
	{
		return;
	}
	const FFishVisualRow& Row = Setup_.Row;
	Seconds += FMath::Max(0.f, DeltaTime);
	bExhausted = View.bExhausted;
	Stamina01 = View.Stamina01;
	LastWaterZ = View.WaterZ;
	bHasWaterZ = FMath::IsFinite(View.WaterZ);

	// T-048: the mouth is the point that follows the line end (the bobber is over it); the body hangs back from it, away
	// from the rod, swung sideways while the fish swims. Only the line point is smoothed, so the mouth never leaves the line.
	const float FloorZ = Row.FloorClearance >= 0.f ? TraceFloorZ(View.LineEnd, View.WaterZ) : -UE_BIG_NUMBER;
	const FVector Target = FFightFishVisual::MouthTarget(Row, View, FloorZ);
	const FVector Before = MouthPoint;
	const bool bSnap = !bPlaced || FVector::Dist(Target, Before) > Row.SnapDistance;
	const float SmoothTime = View.bHasAuthority ? Row.AuthoritySmoothTime : Row.ProxySmoothTime;
	MouthPoint = bSnap ? Target : FFightFishVisual::StepMouth(Row, Before, Target, DeltaTime, SmoothTime);
	Velocity = (bSnap || DeltaTime <= 0.f) ? FVector::ZeroVector : (MouthPoint - Before) / DeltaTime;

	const FRotator Desired = FFightFishVisual::FightFacing(Row, Velocity, MouthPoint, View.PlayerLocation, GetActorRotation(), bExhausted);
	const FQuat Smoothed = bSnap ? Desired.Quaternion()
		: FQuat::Slerp(GetActorQuat(), Desired.Quaternion(), FFightFishVisual::SmoothAlpha(DeltaTime, Row.RotationSmoothTime));
	const FRotator Rotation = FFightFishVisual::ClampToLine(Row, Smoothed.Rotator(), MouthPoint, View.PlayerLocation);
	const FVector Forward = Rotation.Vector();
	const float Offset = MouthOffsetCm * FishScale;
	LastTarget = FFightFishVisual::CenterForMouth(Target, Forward, Offset);
	SetActorLocationAndRotation(FFightFishVisual::CenterForMouth(MouthPoint, Forward, Offset), Rotation);
	bPlaced = true;
	UpdateAnim(EFightFishPhase::Fighting, View.MoveId, MouthPoint - Before);
}

void ALureFightFish::UpdateAnim(EFightFishPhase InPhase, FName MoveId, const FVector& TurnToward)
{
	FFightFishAnimInput In;
	In.Phase = InPhase;
	In.MoveId = MoveId;
	In.bExhausted = bExhausted;
	In.SecondsSinceHook = Seconds;
	In.SpeedCmS = static_cast<float>(Velocity.Size());
	In.BodyLengthCm = BodyLengthCm * FishScale;
	In.WeightKg = Setup_.Fish.WeightKg;
	In.ReferenceWeightKg = Setup_.ReferenceWeightKg;
	In.AnimRate = Setup_.AnimRate;
	In.AnimAmplitude = Setup_.AnimAmplitude;
	In.bDartRight = bDartRight;
	In.Stamina01 = Stamina01;
	FFishAnimState State = FFightFishVisual::ComputeAnimState(Setup_.Row, In);
	if (State.Role == EFishAnimRole::Dart && LastRole != EFishAnimRole::Dart)
	{
		// A new dart: its C-start turns to the side the fish is heading (+Y = the fish's right).
		const FVector Local = GetActorQuat().UnrotateVector(TurnToward);
		bDartRight = Local.Y > 0.0;
		In.bDartRight = bDartRight;
		State = FFightFishVisual::ComputeAnimState(Setup_.Row, In);
	}
	LastRole = State.Role;
	AnimState = State;
}

void ALureFightFish::BeginEnd(EFightFishEnd End, const FVector& AwayFrom)
{
	if (Phase != EFightFishPhase::Fighting || End == EFightFishEnd::None)
	{
		return;
	}
	EndSeconds = 0.f;
	bExhausted = false;
	if (End == EFightFishEnd::Landed)
	{
		Phase = EFightFishPhase::Landed;
		Velocity = FVector::ZeroVector;
	}
	else
	{
		Phase = EFightFishPhase::Escaping;
		EscapeDirection = (GetActorLocation() - AwayFrom).GetSafeNormal2D();
		if (EscapeDirection.IsNearlyZero())
		{
			EscapeDirection = GetActorForwardVector().GetSafeNormal2D();
		}
	}
	UpdateAnim(Phase, NAME_None, FVector::ZeroVector);
}

void ALureFightFish::TickAfterFight(float DeltaTime)
{
	if (Phase == EFightFishPhase::Fighting)
	{
		return;
	}
	const float Dt = FMath::Max(0.f, DeltaTime);
	Seconds += Dt;
	EndSeconds += Dt;
	if (Phase == EFightFishPhase::Escaping)
	{
		const FFishVisualRow& Row = Setup_.Row;
		const FVector Here = GetActorLocation();
		FVector Next = FFightFishVisual::EscapeStep(Row, Here, EscapeDirection, Dt, bHasWaterZ ? LastWaterZ : UE_BIG_NUMBER, -UE_BIG_NUMBER);
		if (bHasWaterZ && Row.FloorClearance >= 0.f)
		{
			// Stay above the seabed while sinking away (shallow sand), traced where the fish is going.
			const float FloorZ = TraceFloorZ(Next, LastWaterZ, static_cast<float>(Next.Z));
			Next = FFightFishVisual::EscapeStep(Row, Here, EscapeDirection, Dt, LastWaterZ, FloorZ);
		}
		const FVector Player = Here - EscapeDirection * 1000.f; // "away from" point for the facing rule
		MoveTo(Next, Player, Dt, 0.f, false);
	}
	UpdateAnim(Phase, NAME_None, FVector::ZeroVector);
}

bool ALureFightFish::IsFinished() const
{
	return Phase == EFightFishPhase::Escaping && EndSeconds >= Setup_.Row.EscapeTime;
}

FVector ALureFightFish::GetMouthLocation() const
{
	const FName Bone = Setup_.Row.MouthBone;
	if (Mesh && Mesh->GetSkeletalMeshAsset() && !Bone.IsNone() && Mesh->DoesSocketExist(Bone))
	{
		return Mesh->GetSocketLocation(Bone);
	}
	return GetActorLocation() + GetActorForwardVector() * MouthOffsetCm * FishScale;
}

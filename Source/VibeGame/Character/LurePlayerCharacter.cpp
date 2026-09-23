// Lure: first-person player character (T-004).

#include "Character/LurePlayerCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ALurePlayerCharacter::ALurePlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<ULureCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	bIsProne = false;
	PrimaryActorTick.bCanEverTick = true;

	// Class-default capsule = the built-in Stand row; BeginPlay applies DT_Movement's Stand row.
	const FLureMovementRow Stand = FLureMovementData::GetFallbackRow(ELureMovementState::Stand);
	GetCapsuleComponent()->InitCapsuleSize(Stand.CapsuleRadius, Stand.CapsuleHalfHeight);

	// First person: the body turns with the view (yaw only).
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, Stand.EyeHeight - Stand.CapsuleHalfHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(90.f);
	// First-person primitives (the arms, later the rod) render with their own FOV and scaled toward the eye (SK_FPArms.anim.md):
	// the arms were composed for 90 degrees, and scaling toward the eye keeps the image while stopping clipping into walls.
	FirstPersonCamera->SetEnableFirstPersonFieldOfView(true);
	FirstPersonCamera->SetFirstPersonFieldOfView(90.f);
	FirstPersonCamera->SetEnableFirstPersonScale(true);
	FirstPersonCamera->SetFirstPersonScale(0.6f);

	FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
	FirstPersonArms->SetupAttachment(FirstPersonCamera);
	FirstPersonArms->SetOnlyOwnerSee(true);
	FirstPersonArms->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonArms->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	FirstPersonArms->SetCanEverAffectNavigation(false);
	FirstPersonArms->SetCastShadow(false);
	FirstPersonArms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FirstPersonArms->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

	// Asset paths from art/export/Characters/SK_FPArms.anim.md (imported by the editor-operator; all optional at runtime).
	FirstPersonArmsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Art/Characters/FPArms/SK_FPArms.SK_FPArms")));
	FirstPersonArmsAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Art/Characters/FPArms/ABP_FPArms.ABP_FPArms_C")));
	StanceDipAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/Art/Characters/FPArms/A_FPArms_StanceDip.A_FPArms_StanceDip")));
	StanceAdditiveSlot = TEXT("StanceAdditive");
	SwimStrokeAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/Art/Characters/FPArms/A_FPArms_SwimStroke.A_FPArms_SwimStroke"))); // not made yet (T-026)

	// Third-person body slot (empty for now): never visible to its owner, origin at the feet.
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -Stand.CapsuleHalfHeight));

	// Placeholder body for other players (lead decision A18): an engine cylinder the size of the capsule.
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetOwnerNoSee(true);
	PlaceholderBody->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	PlaceholderBody->SetCanEverAffectNavigation(false);
	PlaceholderBody->SetRelativeScale3D(FVector(2.f * Stand.CapsuleRadius / 100.f, 2.f * Stand.CapsuleRadius / 100.f, 2.f * Stand.CapsuleHalfHeight / 100.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterial.Succeeded())
	{
		PlaceholderBodyMaterial = BasicShapeMaterial.Object;
	}
	PlaceholderBodyColor = FLinearColor(FColor(0x7C, 0x8A, 0x63));

	CurrentEyeHeight = Stand.EyeHeight;
	EyeBlendFrom = Stand.EyeHeight;
	EyeBlendTo = Stand.EyeHeight;
	BaseEyeHeight = Stand.EyeHeight - Stand.CapsuleHalfHeight;
}

ULureCharacterMovementComponent* ALurePlayerCharacter::GetLureMovement() const
{
	return Cast<ULureCharacterMovementComponent>(GetCharacterMovement());
}

void ALurePlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ALurePlayerCharacter, bIsProne, COND_SimulatedOnly);
}

void ALurePlayerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Tick after movement so the camera follows this frame's stance and capsule.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		AddTickPrerequisiteComponent(Movement);
	}
}

void ALurePlayerCharacter::BeginPlay()
{
	// The movement component's BeginPlay (inside Super) resolves DT_Movement and applies the stance capsule.
	Super::BeginPlay();

	LoadFirstPersonArms();
	LoadArmsAnimation();
	SetupPlaceholderBodyMaterial();
	SnapEyeHeightToStance();
	LastDipStance = GetStance();
}

void ALurePlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveMappingContextFrom(MappedController.Get());
	Super::EndPlay(EndPlayReason);
}

void ALurePlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateEyeHeight(DeltaSeconds);
	if (IsLocallyControlled())
	{
		UpdateSprintToggle();
		UpdateArmsMotion(DeltaSeconds);
	}
}

// ---- Arms and body ----

void ALurePlayerCharacter::LoadFirstPersonArms()
{
	if (!FirstPersonArms || FirstPersonArms->GetSkeletalMeshAsset() || FirstPersonArmsMesh.IsNull() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	USkeletalMesh* ArmsAsset = FirstPersonArmsMesh.Get();
	if (!ArmsAsset)
	{
		const FSoftObjectPath Path = FirstPersonArmsMesh.ToSoftObjectPath();
		if (!FPackageName::DoesPackageExist(Path.GetLongPackageName()))
		{
			UE_LOG(LogLureMovement, Log, TEXT("First-person arms '%s' are not imported yet; no arms shown."), *Path.ToString());
			return;
		}
		UObject* Loaded = Path.TryLoad();
		ArmsAsset = Cast<USkeletalMesh>(Loaded);
		if (!ArmsAsset)
		{
			UE_LOG(LogLureMovement, Warning, TEXT("First-person arms '%s' is a %s, not a skeletal mesh: import SK_FPArms as a Skeletal Mesh."),
				*Path.ToString(), Loaded ? *Loaded->GetClass()->GetName() : TEXT("missing object"));
			return;
		}
	}
	FirstPersonArms->SetSkeletalMeshAsset(ArmsAsset);
}

void ALurePlayerCharacter::SetupPlaceholderBodyMaterial()
{
	if (!PlaceholderBody || !PlaceholderBodyMaterial || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PlaceholderBodyMaterial, this))
	{
		Material->SetVectorParameterValue(TEXT("Color"), PlaceholderBodyColor);
		PlaceholderBody->SetMaterial(0, Material);
	}
}

void ALurePlayerCharacter::LoadArmsAnimation()
{
	// Only worth it once the arms mesh is there (the ABP targets SKEL_FPArms).
	if (!FirstPersonArms || !FirstPersonArms->GetSkeletalMeshAsset() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (!FirstPersonArms->GetAnimInstance() && !FirstPersonArmsAnimClass.IsNull())
	{
		UClass* AnimClass = FirstPersonArmsAnimClass.Get();
		if (!AnimClass)
		{
			const FSoftObjectPath Path = FirstPersonArmsAnimClass.ToSoftObjectPath();
			if (FPackageName::DoesPackageExist(Path.GetLongPackageName()))
			{
				AnimClass = FirstPersonArmsAnimClass.LoadSynchronous();
			}
			else
			{
				UE_LOG(LogLureMovement, Log, TEXT("Arms animation Blueprint '%s' is not imported yet; the arms keep their bind pose."), *Path.ToString());
			}
		}
		if (AnimClass)
		{
			FirstPersonArms->SetAnimInstanceClass(AnimClass);
		}
	}

	if (!StanceDipAnimation.IsNull())
	{
		LoadedStanceDip = StanceDipAnimation.Get();
		if (!LoadedStanceDip && FPackageName::DoesPackageExist(StanceDipAnimation.ToSoftObjectPath().GetLongPackageName()))
		{
			LoadedStanceDip = StanceDipAnimation.LoadSynchronous();
		}
	}
	LoadSwimStroke();
}

void ALurePlayerCharacter::UpdateArmsMotion(float DeltaSeconds)
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	if (!FirstPersonArms || !Movement)
	{
		return;
	}

	// How fast the view turns (the arms lag it a little).
	FVector2D LookRate = FVector2D::ZeroVector;
	const FRotator ControlRotation = GetControlRotation();
	if (bHasLastControlRotation && DeltaSeconds > 0.f)
	{
		const FRotator Delta = (ControlRotation - LastControlRotation).GetNormalized();
		LookRate = FVector2D(Delta.Yaw, Delta.Pitch) / DeltaSeconds;
	}
	LastControlRotation = ControlRotation;
	bHasLastControlRotation = true;

	// Walk bob + look sway as an offset on the arms (the camera stays steady).
	const FLureMovementRow& Row = Movement->GetRow(Movement->GetMovementState());
	const FLureArmsMotionSettings& ArmsSettings = GetDefault<ULureCharacterSettings>()->ArmsMotion;
	FTransform Offset = FLureArmsBob::Step(ArmsBobState, Row, ArmsSettings,
		static_cast<float>(Movement->Velocity.Size2D()), Movement->IsMovingOnGround(), bHoldingRod, LookRate, DeltaSeconds);

	// Per-state pull-back toward the eye (DT_Movement ArmsPullBack): prone, the hands stay out of a wall the capsule touches.
	ArmsPullBackNow = (DeltaSeconds > 0.f && ArmsSettings.StanceOffsetBlendSpeed > 0.f)
		? FMath::FInterpTo(ArmsPullBackNow, Row.ArmsPullBack, DeltaSeconds, ArmsSettings.StanceOffsetBlendSpeed)
		: Row.ArmsPullBack;
	Offset.AddToTranslation(FVector(-ArmsPullBackNow, 0.f, 0.f));
	const FTransform ArmsOffset = ApplySwimArms(Offset, DeltaSeconds);
	FirstPersonArms->SetRelativeLocationAndRotation(ArmsOffset.GetLocation(), ArmsOffset.Rotator());

	// Additive dip whenever the posture changes.
	const ELureStance Stance = Movement->GetStance();
	if (Stance != LastDipStance)
	{
		LastDipStance = Stance;
		PlayStanceDip(Movement->GetStanceRow(Stance).StanceDipPlayRate);
	}
}

bool ALurePlayerCharacter::PlayStanceDip(float PlayRate)
{
	if (!(PlayRate > 0.f) || !FirstPersonArms || !LoadedStanceDip)
	{
		return false;
	}
	UAnimInstance* AnimInstance = FirstPersonArms->GetAnimInstance();
	if (!AnimInstance)
	{
		return false;
	}
	return AnimInstance->PlaySlotAnimationAsDynamicMontage(LoadedStanceDip, StanceAdditiveSlot, 0.f, 0.05f, PlayRate) != nullptr;
}

FTransform ALurePlayerCharacter::GetArmsBobOffset() const
{
	return FirstPersonArms ? FirstPersonArms->GetRelativeTransform() : FTransform::Identity;
}

void ALurePlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (IsLocallyControlled())
	{
		PlayStanceDip(GetDefault<ULureCharacterSettings>()->ArmsMotion.LandingDipPlayRate);
	}
}

void ALurePlayerCharacter::UpdatePlaceholderBody()
{
	if (!PlaceholderBody || !GetCapsuleComponent())
	{
		return;
	}
	PlaceholderBody->SetVisibility(bShowPlaceholderBody);

	float Radius = 0.f;
	float HalfHeight = 0.f;
	GetCapsuleComponent()->GetUnscaledCapsuleSize(Radius, HalfHeight);
	// The engine cylinder is 100 cm wide and tall, centered on its origin.
	PlaceholderBody->SetRelativeScale3D(FVector(2.f * Radius / 100.f, 2.f * Radius / 100.f, 2.f * HalfHeight / 100.f));
}

void ALurePlayerCharacter::ApplyBodyMeshOffset()
{
	// Same as ACharacter::OnStartCrouch, for any capsule height: keep the body's feet on the floor.
	const ACharacter* DefaultCharacter = GetClass()->GetDefaultObject<ACharacter>();
	if (!GetMesh() || !GetCapsuleComponent() || !DefaultCharacter || !DefaultCharacter->GetMesh() || !DefaultCharacter->GetCapsuleComponent())
	{
		return;
	}
	const float Adjust = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	FVector& MeshRelativeLocation = GetMesh()->GetRelativeLocation_DirectMutable();
	MeshRelativeLocation.Z = DefaultCharacter->GetMesh()->GetRelativeLocation().Z + Adjust;
	BaseTranslationOffset.Z = MeshRelativeLocation.Z;
}

void ALurePlayerCharacter::RefreshStanceVisuals()
{
	ApplyEyeHeight();
	UpdatePlaceholderBody();
	ApplyBodyMeshOffset();
}

// ---- Eye height ----

float ALurePlayerCharacter::EvaluateEyeBlend(float From, float To, float Elapsed, float Duration)
{
	if (!(Duration > 0.f) || !(Elapsed < Duration) || !FMath::IsFinite(From))
	{
		return To;
	}
	const float Alpha = FMath::Clamp(Elapsed / Duration, 0.f, 1.f);
	const float Smooth = Alpha * Alpha * (3.f - 2.f * Alpha);
	return From + (To - From) * Smooth;
}

ELureMovementState ALurePlayerCharacter::GetEyeState() const
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	if (!Movement)
	{
		return ELureMovementState::Stand;
	}
	// Crawled off a ledge: you go prone again on landing, so the camera stays at the prone height through the fall.
	if (Movement->IsFalling() && Movement->IsProneRequested())
	{
		return ELureMovementState::Prone;
	}
	return Movement->GetMovementState();
}

float ALurePlayerCharacter::GetTargetEyeHeight() const
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	return Movement ? Movement->GetRow(GetEyeState()).EyeHeight : CurrentEyeHeight;
}

void ALurePlayerCharacter::BeginEyeBlend(float TargetEyeHeight, float Duration)
{
	EyeBlendFrom = FMath::IsFinite(CurrentEyeHeight) ? CurrentEyeHeight : TargetEyeHeight;
	EyeBlendTo = TargetEyeHeight;
	EyeBlendDuration = (FMath::IsFinite(Duration) && Duration > 0.f) ? Duration : 0.f;
	EyeBlendElapsed = 0.f;
	if (EyeBlendDuration <= 0.f)
	{
		CurrentEyeHeight = TargetEyeHeight;
	}
}

void ALurePlayerCharacter::UpdateEyeHeight(float DeltaSeconds)
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	if (!Movement)
	{
		return;
	}

	// The target row's TransitionTime is the time to reach its eye height (lead decision A8), unless the row we leave
	// sets an ExitTransitionTime (getting up from prone takes longer than going down to crouch; T-004 playtest).
	const ELureMovementState State = GetEyeState();
	const FLureMovementRow& Row = Movement->GetRow(State);
	if (!FMath::IsNearlyEqual(Row.EyeHeight, EyeBlendTo, 0.01f))
	{
		const float ExitTime = Movement->GetRow(EyeBlendState).ExitTransitionTime;
		BeginEyeBlend(Row.EyeHeight, ExitTime > 0.f ? ExitTime : Row.TransitionTime);
	}
	EyeBlendState = State;

	if (EyeBlendElapsed < EyeBlendDuration && FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.f)
	{
		EyeBlendElapsed = FMath::Min(EyeBlendElapsed + DeltaSeconds, EyeBlendDuration);
	}
	CurrentEyeHeight = EvaluateEyeBlend(EyeBlendFrom, EyeBlendTo, EyeBlendElapsed, EyeBlendDuration);

	ClampEyeToHeadroom();
	ApplyEyeHeight();
}

void ALurePlayerCharacter::ClampEyeToHeadroom()
{
	// While the camera eases down after the capsule already shrank (e.g. going prone at a gap's mouth), it can sit above
	// the capsule top. Keep it under whatever is overhead so it never looks through a ceiling.
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	UWorld* World = GetWorld();
	if (!Capsule || !Movement || !World || CameraHeadroomRadius <= 0.f)
	{
		return;
	}

	const float ScaledHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (CurrentEyeHeight + CameraHeadroomRadius <= 2.f * ScaledHalfHeight)
	{
		return; // the camera probe is inside the capsule, which already has room
	}

	const FVector Up = -Movement->GetGravityDirection();
	const FVector Center = Capsule->GetComponentLocation();
	const FVector Feet = Center - Up * ScaledHalfHeight;
	const FVector Camera = Feet + Up * CurrentEyeHeight;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureEyeHeadroom), false, this);
	const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, Center, Camera, FQuat::Identity, Capsule->GetCollisionObjectType(), FCollisionShape::MakeSphere(CameraHeadroomRadius), Params, Response)
		&& !Hit.bStartPenetrating)
	{
		const float MaxEyeHeight = ((Hit.Location - Feet) | Up) - 0.5f;
		if (MaxEyeHeight < CurrentEyeHeight)
		{
			// Only ever lowers the camera toward its target, never below it.
			CurrentEyeHeight = FMath::Max(MaxEyeHeight, FMath::Min(EyeBlendTo, CurrentEyeHeight));
		}
	}
}

void ALurePlayerCharacter::ApplyEyeHeight()
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}
	BaseEyeHeight = CurrentEyeHeight - Capsule->GetScaledCapsuleHalfHeight();
	if (FirstPersonCamera)
	{
		const float Scale = FMath::Max(Capsule->GetShapeScale(), UE_KINDA_SMALL_NUMBER);
		FVector Relative = FirstPersonCamera->GetRelativeLocation();
		Relative.Z = BaseEyeHeight / Scale;
		FirstPersonCamera->SetRelativeLocation(Relative);
	}
}

void ALurePlayerCharacter::SnapEyeHeightToStance()
{
	const float Target = GetTargetEyeHeight();
	EyeBlendState = GetEyeState();
	CurrentEyeHeight = Target;
	EyeBlendFrom = Target;
	EyeBlendTo = Target;
	EyeBlendElapsed = 0.f;
	EyeBlendDuration = 0.f;
	RefreshStanceVisuals();
}

void ALurePlayerCharacter::HandleCapsuleResized(float OldFeetHeight)
{
	if (const ULureCharacterMovementComponent* Movement = GetLureMovement())
	{
		const float Shift = OldFeetHeight - Movement->GetFeetHeight();
		if (!FMath::IsNearlyZero(Shift, 0.01f))
		{
			// The feet moved (resize in the air): keep the camera where it is in the world and ease on from there.
			CurrentEyeHeight += Shift;
			EyeBlendFrom = CurrentEyeHeight;
			EyeBlendElapsed = 0.f;
		}
	}
	// Same eye height above the feet on the new capsule: the camera doesn't pop when the capsule moves.
	RefreshStanceVisuals();
}

void ALurePlayerCharacter::RecalculateBaseEyeHeight()
{
	if (!GetCapsuleComponent())
	{
		Super::RecalculateBaseEyeHeight();
		return;
	}
	BaseEyeHeight = CurrentEyeHeight - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

FVector ALurePlayerCharacter::GetPawnViewLocation() const
{
	// What AI sight and noise checks will use: exactly where the player's eye is (low when prone).
	return FirstPersonCamera ? FirstPersonCamera->GetComponentLocation() : Super::GetPawnViewLocation();
}

// ---- Stance ----

ELureStance ALurePlayerCharacter::GetStance() const
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	return Movement ? Movement->GetStance() : ELureStance::Stand;
}

ELureStance ALurePlayerCharacter::GetRequestedStance() const
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	return Movement ? Movement->GetRequestedStance() : ELureStance::Stand;
}

bool ALurePlayerCharacter::IsSprinting() const
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	return Movement && Movement->IsSprinting();
}

void ALurePlayerCharacter::RequestStance(ELureStance Stance)
{
	ULureCharacterMovementComponent* Movement = GetLureMovement();
	if (!Movement)
	{
		return;
	}
	// No crouch or prone in the water (T-026), nor where the water is too deep for that stance (B2: the capsule center
	// would go under). Refused before anything changes, not queued; the movement component refuses the same on the server.
	if (Stance != ELureStance::Stand && (IsSwimming() || Movement->IsStanceTooDeepForWater(Stance)))
	{
		ShowStanceHint(Stance == ELureStance::Crouch ? TEXT("Too deep to crouch here.") : TEXT("Too deep to go prone here."));
		return;
	}

	switch (Stance)
	{
	case ELureStance::Stand:
		Movement->bWantsToCrouch = false;
		Movement->SetProneRequested(false);
		break;

	case ELureStance::Crouch:
		if (Movement->CanEverCrouch())
		{
			Movement->bWantsToCrouch = true;
			Movement->SetProneRequested(false);
		}
		break;

	case ELureStance::Prone:
		// Refused in the air and not queued (QA A6); the server applies the same rule through CanProneInCurrentState.
		if (Movement->bCanEverProne && !Movement->IsFalling())
		{
			Movement->SetProneRequested(true);
			Movement->bWantsToCrouch = false;
		}
		break;
	}
}

void ALurePlayerCharacter::ShowStanceHint(const FString& Hint)
{
	StanceHint = Hint;
	StanceHintTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

FString ALurePlayerCharacter::GetStanceHintText() const
{
	const UWorld* World = GetWorld();
	if (StanceHint.IsEmpty() || !World || World->GetTimeSeconds() - StanceHintTime > StanceHintDuration)
	{
		return FString();
	}
	return StanceHint;
}

void ALurePlayerCharacter::ToggleCrouch()
{
	RequestStance(GetRequestedStance() == ELureStance::Crouch ? ELureStance::Stand : ELureStance::Crouch);
}

void ALurePlayerCharacter::ToggleProne()
{
	RequestStance(GetRequestedStance() == ELureStance::Prone ? ELureStance::Stand : ELureStance::Prone);
}

void ALurePlayerCharacter::SetSprintRequested(bool bRequested)
{
	ULureCharacterMovementComponent* Movement = GetLureMovement();
	if (!Movement)
	{
		return;
	}
	Movement->SetSprintRequested(bRequested);
	if (!bRequested)
	{
		bSprintToggledOn = false;
	}

	// Sprint from crouch stands you up first (queued under a ceiling); from prone it waits until you stand (lead decision A1).
	if (bRequested && Movement->GetRequestedStance() == ELureStance::Crouch)
	{
		RequestStance(ELureStance::Stand);
	}
}

void ALurePlayerCharacter::ToggleSprint()
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	const bool bNewValue = !(Movement && Movement->IsSprintRequested());
	SetSprintRequested(bNewValue);
	bSprintToggledOn = bNewValue;
	LastMoveInputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void ALurePlayerCharacter::UpdateSprintToggle()
{
	// Toggled sprint ends once you stop moving for a moment.
	if (!bSprintToggledOn || !GetWorld())
	{
		return;
	}
	const float StopDelay = GetDefault<ULureCharacterSettings>()->SprintToggleStopDelay;
	if (GetWorld()->GetTimeSeconds() - LastMoveInputTime > StopDelay)
	{
		SetSprintRequested(false);
	}
}

void ALurePlayerCharacter::SetIsProne(bool bNewIsProne)
{
	bIsProne = bNewIsProne;
}

void ALurePlayerCharacter::OnStartProne()
{
	RefreshStanceVisuals();
}

void ALurePlayerCharacter::OnEndProne()
{
	RefreshStanceVisuals();
}

void ALurePlayerCharacter::OnRep_IsProne()
{
	if (ULureCharacterMovementComponent* Movement = GetLureMovement())
	{
		Movement->SetProneRequested(bIsProne);
		if (bIsProne)
		{
			Movement->Prone(/*bClientSimulation*/ true);
		}
		else
		{
			Movement->UnProne(/*bClientSimulation*/ true);
		}
		Movement->bNetworkUpdateReceived = true;
	}
}

void ALurePlayerCharacter::OnRep_IsCrouched()
{
	// While prone, the prone state owns the capsule; the crouch flag alone changes nothing (both may arrive in one update).
	if (bIsProne)
	{
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->bWantsToCrouch = IsCrouched();
			Movement->bNetworkUpdateReceived = true;
		}
		return;
	}
	Super::OnRep_IsCrouched();
}

void ALurePlayerCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	RefreshStanceVisuals();
}

void ALurePlayerCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	RefreshStanceVisuals(); // the engine resets the body offset to the class default; the Stand row may differ
}

bool ALurePlayerCharacter::CanJumpInternal_Implementation() const
{
	// The engine refuses every jump while crouched; here DT_Movement's CanJump decides (and prone never jumps).
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	return Movement && Movement->CanJumpInCurrentStance() && JumpIsAllowedInternal();
}

void ALurePlayerCharacter::Restart()
{
	Super::Restart(); // clears the crouch wish (engine)

	if (ULureCharacterMovementComponent* Movement = GetLureMovement())
	{
		Movement->SetProneRequested(false);
		Movement->SetSprintRequested(false);
	}
	bSprintToggledOn = false;
}

// ---- Input ----

void ALurePlayerCharacter::NotifyControllerChanged()
{
	APlayerController* OldController = Cast<APlayerController>(PreviousController);
	APlayerController* NewController = Cast<APlayerController>(Controller);

	Super::NotifyControllerChanged();

	if (OldController && OldController != NewController)
	{
		RemoveMappingContextFrom(OldController);
	}
	AddMappingContextTo(NewController);
}

void ALurePlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart(); // creates the input component and calls SetupPlayerInputComponent for the local player
	AddMappingContextTo(Cast<APlayerController>(Controller));
}

void ALurePlayerCharacter::AddMappingContextTo(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
	UInputMappingContext* MappingContext = ULureInputSubsystem::GetDefaultMappingContext();
	if (!InputSubsystem || !MappingContext)
	{
		return; // e.g. a remote player's pawn on the server, or an automation world without local players
	}
	if (!InputSubsystem->HasMappingContext(MappingContext))
	{
		InputSubsystem->AddMappingContext(MappingContext, ULureInputSubsystem::MappingPriority);
	}
	MappedController = PlayerController;
}

void ALurePlayerCharacter::RemoveMappingContextFrom(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}
	// The mapping context is shared: if another Lure character already took over this player, keep it.
	const APawn* CurrentPawn = PlayerController->GetPawn();
	if (CurrentPawn && CurrentPawn != this && CurrentPawn->IsA<ALurePlayerCharacter>())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
	UInputMappingContext* MappingContext = ULureInputSubsystem::GetDefaultMappingContext();
	if (InputSubsystem && MappingContext)
	{
		InputSubsystem->RemoveMappingContext(MappingContext);
	}
	if (MappedController.Get() == PlayerController)
	{
		MappedController.Reset();
	}
}

void ALurePlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogLureMovement, Warning, TEXT("%s: the input component is not an EnhancedInputComponent; controls are not bound."), *GetName());
		return;
	}

	UInputAction* MoveAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Move);
	UInputAction* LookAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Look);
	UInputAction* JumpAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Jump);
	UInputAction* SprintAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Sprint);
	UInputAction* CrouchAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Crouch);
	UInputAction* ProneAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Prone);
	if (!MoveAction || !LookAction || !JumpAction || !SprintAction || !CrouchAction || !ProneAction)
	{
		UE_LOG(LogLureMovement, Warning, TEXT("%s: Lure input actions are missing (ULureInputSubsystem not running); controls are not bound."), *GetName());
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ALurePlayerCharacter::HandleMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ALurePlayerCharacter::HandleLook);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ALurePlayerCharacter::HandleJumpPressed);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ALurePlayerCharacter::HandleJumpReleased);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ALurePlayerCharacter::HandleSprintPressed);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ALurePlayerCharacter::HandleSprintReleased);
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ALurePlayerCharacter::HandleCrouchPressed);
	Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ALurePlayerCharacter::HandleCrouchReleased);
	Input->BindAction(ProneAction, ETriggerEvent::Started, this, &ALurePlayerCharacter::HandlePronePressed);
	Input->BindAction(ProneAction, ETriggerEvent::Completed, this, &ALurePlayerCharacter::HandleProneReleased);
}

void ALurePlayerCharacter::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	DoMove(Axis.X, Axis.Y);
}

void ALurePlayerCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	DoLook(Axis.X, Axis.Y);
}

void ALurePlayerCharacter::DoMove(float Right, float Forward)
{
	if (!Controller)
	{
		return;
	}
	if ((!FMath::IsNearlyZero(Right) || !FMath::IsNearlyZero(Forward)) && GetWorld())
	{
		LastMoveInputTime = GetWorld()->GetTimeSeconds();
	}
	AddMovementInput(GetActorForwardVector(), Forward);
	AddMovementInput(GetActorRightVector(), Right);
}

void ALurePlayerCharacter::DoLook(float YawDegrees, float PitchDegrees)
{
	// Degrees straight into the controller's rotation input, independent of the legacy input scales in DefaultInput.ini.
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController || PlayerController->IsLookInputIgnored())
	{
		return;
	}
	PlayerController->RotationInput.Yaw += YawDegrees;
	PlayerController->RotationInput.Pitch += PitchDegrees;
}

void ALurePlayerCharacter::DoJumpStart()
{
	Jump(); // CanJump (stance rules) is checked when the jump is processed; a press while prone does nothing
}

void ALurePlayerCharacter::DoJumpEnd()
{
	StopJumping();
}

void ALurePlayerCharacter::HandleJumpPressed()
{
	DoJumpStart();
}

void ALurePlayerCharacter::HandleJumpReleased()
{
	DoJumpEnd();
}

void ALurePlayerCharacter::HandleSprintPressed()
{
	if (GetDefault<ULureCharacterSettings>()->bSprintIsToggle)
	{
		ToggleSprint();
	}
	else
	{
		SetSprintRequested(true);
	}
}

void ALurePlayerCharacter::HandleSprintReleased()
{
	if (!GetDefault<ULureCharacterSettings>()->bSprintIsToggle)
	{
		SetSprintRequested(false);
	}
}

void ALurePlayerCharacter::HandleCrouchPressed()
{
	if (GetDefault<ULureCharacterSettings>()->bCrouchIsToggle)
	{
		ToggleCrouch();
	}
	else
	{
		RequestStance(ELureStance::Crouch);
	}
}

void ALurePlayerCharacter::HandleCrouchReleased()
{
	if (!GetDefault<ULureCharacterSettings>()->bCrouchIsToggle && GetRequestedStance() == ELureStance::Crouch)
	{
		RequestStance(ELureStance::Stand);
	}
}

void ALurePlayerCharacter::HandlePronePressed()
{
	if (GetDefault<ULureCharacterSettings>()->bProneIsToggle)
	{
		ToggleProne();
	}
	else
	{
		RequestStance(ELureStance::Prone);
	}
}

void ALurePlayerCharacter::HandleProneReleased()
{
	if (!GetDefault<ULureCharacterSettings>()->bProneIsToggle && GetRequestedStance() == ELureStance::Prone)
	{
		RequestStance(ELureStance::Stand);
	}
}

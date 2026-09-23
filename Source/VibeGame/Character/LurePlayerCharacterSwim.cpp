// Lure: the player character's swimming parts (T-026): the in-water state and event, and the placeholder arms.

#include "Character/LurePlayerCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/PackageName.h"

namespace LureSwimArmsPrivate
{
	/** The swim stroke replaces the arms' pose, so it plays in the ABP's full-body slot (SK_FPArms.anim.md graph). */
	const FName StrokeSlot(TEXT("DefaultSlot"));
	constexpr float StrokeBlendTime = 0.25f;
	constexpr int32 StrokeLoopCount = 1000000;
}

bool ALurePlayerCharacter::IsSwimming() const
{
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	return Movement && Movement->IsSwimmingOrClimbingOut();
}

void ALurePlayerCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	// Runs wherever the mode changes: server, owning client and other players (replicated mode).
	UpdateSwimState();
}

void ALurePlayerCharacter::UpdateSwimState()
{
	// While the owning client applies a server correction and replays its moves, the mode can pass through states the
	// server never had (e.g. back into a climb it had already finished): report only the settled state, which the
	// movement component asks for once the replay is done (ULureCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate).
	const ULureCharacterMovementComponent* Movement = GetLureMovement();
	if (Movement && Movement->IsReconcilingWithServer())
	{
		return;
	}
	const bool bSwimmingNow = IsSwimming();
	if (bSwimmingNow != bWasSwimming)
	{
		bWasSwimming = bSwimmingNow;
		OnSwimStateChanged.Broadcast(bSwimmingNow);
	}
}

void ALurePlayerCharacter::LoadSwimStroke()
{
	if (LoadedSwimStroke || SwimStrokeAnimation.IsNull() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	LoadedSwimStroke = SwimStrokeAnimation.Get();
	if (!LoadedSwimStroke && FPackageName::DoesPackageExist(SwimStrokeAnimation.ToSoftObjectPath().GetLongPackageName()))
	{
		LoadedSwimStroke = SwimStrokeAnimation.LoadSynchronous();
	}
}

FTransform ALurePlayerCharacter::ApplySwimArms(const FTransform& Offset, float DeltaSeconds)
{
	using namespace LureSwimArmsPrivate;

	const bool bInWater = IsSwimming();
	UAnimInstance* AnimInstance = FirstPersonArms ? FirstPersonArms->GetAnimInstance() : nullptr;

	// Once the animation-artist's swim stroke exists (and the arms are animated): loop it in the water.
	if (LoadedSwimStroke && AnimInstance)
	{
		if (bInWater && !bSwimStrokePlaying)
		{
			bSwimStrokePlaying = AnimInstance->PlaySlotAnimationAsDynamicMontage(LoadedSwimStroke, StrokeSlot, StrokeBlendTime, StrokeBlendTime, 1.f, StrokeLoopCount) != nullptr;
		}
		else if (!bInWater && bSwimStrokePlaying)
		{
			AnimInstance->StopSlotAnimation(StrokeBlendTime, StrokeSlot);
			bSwimStrokePlaying = false;
		}
		SwimArmsAlpha = 0.f;
		return Offset;
	}

	// Placeholder: the arms sink out of view while swimming and come back on land.
	const float Target = bInWater ? 1.f : 0.f;
	SwimArmsAlpha = (DeltaSeconds > 0.f) ? FMath::FInterpTo(SwimArmsAlpha, Target, DeltaSeconds, SwimArmsBlendSpeed) : Target;
	if (SwimArmsAlpha <= UE_KINDA_SMALL_NUMBER)
	{
		SwimArmsAlpha = 0.f;
		return Offset;
	}
	FTransform Lowered = Offset;
	Lowered.AddToTranslation(FVector(0.f, 0.f, -SwimArmsDrop * SwimArmsAlpha));
	Lowered.ConcatenateRotation(FRotator(SwimArmsPitch * SwimArmsAlpha, 0.f, 0.f).Quaternion());
	return Lowered;
}

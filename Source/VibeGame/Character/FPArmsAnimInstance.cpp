// Lure: first-person arms anim instance (T-004).

#include "Character/FPArmsAnimInstance.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Fishing/LureFishingComponent.h"

void UFPArmsAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (const ALurePlayerCharacter* Character = Cast<ALurePlayerCharacter>(TryGetPawnOwner()))
	{
		bHoldingRod = Character->IsHoldingRod();
		ArmsPose = Character->GetArmsPose();
		ArmsPoseBlendTime = Character->GetArmsPoseBlendTime();
		Stance = Character->GetStance();
		bSwimming = Character->IsSwimming();
		const FVector2D RodAim = Character->GetFishing() ? Character->GetFishing()->GetRodAimForAnimation() : FVector2D::ZeroVector;
		RodAimPitch = static_cast<float>(RodAim.Y);
		RodAimYaw = static_cast<float>(RodAim.X);
		// T-030c: the held fish's visual scale (the one its mesh is drawn at) picks the small or trophy hold.
		const ULureHandsComponent* Hands = Character->GetHands();
		const ALureFishItem* Fish = Hands ? Hands->GetHeldFish() : nullptr;
		if (Fish)
		{
			const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(Character);
			HoldFishSizeAlpha = ComputeHoldFishSizeAlpha(Fish->GetWeightScale(), Tuning.HoldFishScaleSmall, Tuning.HoldFishScaleLarge);
		}
		else
		{
			HoldFishSizeAlpha = 0.f;
		}
	}
	else
	{
		HoldFishSizeAlpha = 0.f;
	}
}

float UFPArmsAnimInstance::ComputeHoldFishSizeAlpha(float FishScale, float ScaleSmall, float ScaleLarge)
{
	if (!FMath::IsFinite(FishScale) || !(FishScale > 0.f) || !FMath::IsFinite(ScaleSmall) || !FMath::IsFinite(ScaleLarge)
		|| !(ScaleLarge > ScaleSmall))
	{
		return 0.f;
	}
	return FMath::Clamp((FishScale - ScaleSmall) / (ScaleLarge - ScaleSmall), 0.f, 1.f);
}

// Lure: first-person arms anim instance (T-004).

#include "Character/FPArmsAnimInstance.h"
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
	}
}

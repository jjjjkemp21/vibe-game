// Lure: first-person arms anim instance (T-004).

#include "Character/FPArmsAnimInstance.h"
#include "Character/LurePlayerCharacter.h"

void UFPArmsAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (const ALurePlayerCharacter* Character = Cast<ALurePlayerCharacter>(TryGetPawnOwner()))
	{
		bHoldingRod = Character->IsHoldingRod();
		ArmsPose = Character->GetArmsPose();
		ArmsPoseBlendTime = Character->GetArmsPoseBlendTime();
		Stance = Character->GetStance();
	}
}

// Lure: fish anim instance (T-029).

#include "Fish/FishAnimInstance.h"
#include "Fish/LureFightFish.h"

void UFishAnimInstance::SetAnimState(const FFishAnimState& State)
{
	Role = State.Role;
	PlayRate = FMath::IsFinite(State.PlayRate) ? FMath::Max(0.f, State.PlayRate) : 1.f;
	Amplitude = FMath::IsFinite(State.Amplitude) ? FMath::Clamp(State.Amplitude, 0.f, 1.f) : 1.f;
	RoleBlendTime = FMath::IsFinite(State.RoleBlendTime) ? FMath::Max(0.f, State.RoleBlendTime) : 0.2f;
	DartStartTime = FMath::IsFinite(State.DartStartTime) ? FMath::Max(0.f, State.DartStartTime) : 0.f;
}

FFishAnimState UFishAnimInstance::GetAnimState() const
{
	FFishAnimState State;
	State.Role = Role;
	State.PlayRate = PlayRate;
	State.Amplitude = Amplitude;
	State.RoleBlendTime = RoleBlendTime;
	State.DartStartTime = DartStartTime;
	return State;
}

bool UFishAnimInstance::UpdateFromOwner()
{
	if (const ALureFightFish* Fish = Cast<ALureFightFish>(GetOwningActor()))
	{
		SetAnimState(Fish->GetAnimState());
		return true;
	}
	return false;
}

void UFishAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	UpdateFromOwner();
}

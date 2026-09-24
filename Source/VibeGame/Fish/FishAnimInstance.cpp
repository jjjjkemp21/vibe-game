// Lure: fish anim instance (T-029).

#include "Fish/FishAnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimNodes/AnimNode_BlendListByEnum.h"
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

bool UFishAnimInstance::SetHeldPose(UAnimSequenceBase* Pose, float Time)
{
	if (!Pose)
	{
		return false;
	}
	FFishAnimState State;
	State.Role = EFishAnimRole::Curled;
	State.PlayRate = 0.f;
	State.Amplitude = 1.f;
	State.RoleBlendTime = 0.f;
	State.DartStartTime = 0.f;
	SetAnimState(State);
	DisplayPose = Pose;
	DisplayPoseTime = FMath::IsFinite(Time) ? FMath::Max(0.f, Time) : 0.f;
	bHeldPose = true;
	return true;
}

bool UFishAnimInstance::CanPlayRole(EFishAnimRole InRole) const
{
	return ClassHasRolePin(GetClass(), InRole);
}

bool UFishAnimInstance::ClassHasRolePin(const UClass* AnimClass, EFishAnimRole InRole)
{
	const IAnimClassInterface* AnimInterface = AnimClass ? IAnimClassInterface::GetFromClass(AnimClass) : nullptr;
	const UObject* Defaults = AnimInterface ? AnimClass->GetDefaultObject(false) : nullptr;
	if (!Defaults)
	{
		return false;
	}
	const int32 Value = static_cast<int32>(InRole);
	// The class default object's nodes carry the compiled (folded) data: the blend's enum value -> pose pin table, where
	// pose 0 is the Default pin (a role without its own pin).
	for (const FStructProperty* Property : AnimInterface->GetAnimNodeProperties())
	{
		if (Property && Property->Struct && Property->Struct->IsChildOf(FAnimNode_BlendListByEnum::StaticStruct()))
		{
			const FAnimNode_BlendListByEnum* Node = Property->ContainerPtrToValuePtr<FAnimNode_BlendListByEnum>(Defaults);
			const TArray<int32>& EnumToPose = Node->GetEnumToPoseIndex();
			if (Value == 0 || (EnumToPose.IsValidIndex(Value) && EnumToPose[Value] > 0))
			{
				return true;
			}
		}
	}
	return false;
}

void UFishAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (!bHeldPose)
	{
		UpdateFromOwner();
	}
}

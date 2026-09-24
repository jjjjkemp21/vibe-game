// Lure: fish anim instance (T-029).

#include "Fish/FishAnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimNodes/AnimNode_BlendListByEnum.h"
#include "Animation/AnimNode_SequencePlayer.h"
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
	// The Curled player reads Start Position only when its pin (re)activates: a fish already Curled would keep the old
	// time. Seek it on the next update (game thread, before the graph updates; T-030l).
	bHeldPoseSeekPending = true;
	return true;
}

bool UFishAnimInstance::SeekHeldPosePlayer()
{
	const UClass* AnimClass = GetClass();
	const IAnimClassInterface* AnimInterface = AnimClass ? IAnimClassInterface::GetFromClass(AnimClass) : nullptr;
	if (!AnimInterface)
	{
		return false;
	}
	const TArray<FStructProperty*>& Nodes = AnimInterface->GetAnimNodeProperties();
	const FArrayProperty* BlendPoseProperty = FindFProperty<FArrayProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("BlendPose"));
	const int32 Value = static_cast<int32>(EFishAnimRole::Curled);
	bool bSeeked = false;
	for (const FStructProperty* Property : Nodes)
	{
		if (!BlendPoseProperty || !Property || !Property->Struct || !Property->Struct->IsChildOf(FAnimNode_BlendListByEnum::StaticStruct()))
		{
			continue;
		}
		const FAnimNode_BlendListByEnum* Blend = Property->ContainerPtrToValuePtr<FAnimNode_BlendListByEnum>(this);
		const TArray<int32>& EnumToPose = Blend->GetEnumToPoseIndex();
		const int32 PoseIndex = EnumToPose.IsValidIndex(Value) ? EnumToPose[Value] : 0;
		// BlendPose is protected: read it through reflection. A pose link's LinkID indexes the node list directly (as
		// FPoseLinkBase::AttemptRelink does; only node ids are reversed).
		FScriptArrayHelper Links(BlendPoseProperty, BlendPoseProperty->ContainerPtrToValuePtr<void>(Blend));
		if (PoseIndex <= 0 || !Links.IsValidIndex(PoseIndex))
		{
			continue;
		}
		const FPoseLink* Link = reinterpret_cast<const FPoseLink*>(Links.GetRawPtr(PoseIndex));
		const FStructProperty* Linked = Nodes.IsValidIndex(Link->LinkID) ? Nodes[Link->LinkID] : nullptr;
		if (Linked && Linked->Struct && Linked->Struct->IsChildOf(FAnimNode_SequencePlayerBase::StaticStruct()))
		{
			Linked->ContainerPtrToValuePtr<FAnimNode_SequencePlayerBase>(this)->SetAccumulatedTime(DisplayPoseTime);
			bSeeked = true;
		}
	}
	return bSeeked;
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
	else if (bHeldPoseSeekPending)
	{
		bHeldPoseSeekPending = false;
		SeekHeldPosePlayer();
	}
}

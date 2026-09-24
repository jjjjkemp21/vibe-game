// Lure QA: ABP_Fish's Blend Poses by EFishAnimRole pins play the right clip for every role (a reordered or miswired pin
// would silently show the wrong motion). Curled's Sequence is bound to DisplayPose (not a fixed clip): checked by holding
// a clip other than A_Fish_Curled. Spec: art/export/Fish/SK_Fish.anim.md "ABP_Fish"; Source/VibeGame/Fish/FishAnimInstance.h.

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Fish/FishAnimInstance.h"

namespace LureFishAnimGraphPinOrderTest
{
	const TCHAR* const Folder = TEXT("/Game/Art/Fish/");

	/** Mean over bones of cm + 0.25 x degrees (component space) */
	float PoseError(TArrayView<const FTransform> A, TArrayView<const FTransform> B)
	{
		const int32 Num = FMath::Min(A.Num(), B.Num());
		double Sum = 0.0;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Sum += FVector::Dist(A[Index].GetLocation(), B[Index].GetLocation())
				+ 0.25 * FMath::RadiansToDegrees(A[Index].GetRotation().AngularDistance(B[Index].GetRotation()));
		}
		return Num > 0 ? static_cast<float>(Sum / Num) : TNumericLimits<float>::Max();
	}

	UAnimSequenceBase* LoadClip(const TCHAR* Name)
	{
		return LoadObject<UAnimSequenceBase>(nullptr, *FString::Printf(TEXT("%s%s.%s"), Folder, Name, Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	TArray<FTransform> Evaluate(USkeletalMeshComponent* Mesh, float DeltaSeconds)
	{
		Mesh->TickAnimation(DeltaSeconds, false);
		Mesh->RefreshBoneTransforms();
		return TArray<FTransform>(Mesh->GetComponentSpaceTransforms());
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishAnimGraphPinOrder, "Project.FishVisual.AnimGraph.PinOrderMatchesEnum", LureFightQA::Flags)
	bool FFishAnimGraphPinOrder::RunTest(const FString& Parameters)
	{
		// Role order = EFishAnimRole order (SK_Fish.anim.md "ABP_Fish").
		const TCHAR* ClipNames[] = { TEXT("A_Fish_Swim_Idle"), TEXT("A_Fish_Swim_Fast"), TEXT("A_Fish_Hooked_Thrash"), TEXT("A_Fish_Fight_Run"),
			TEXT("A_Fish_Fight_Dive"), TEXT("A_Fish_Fight_Dart"), TEXT("A_Fish_Landed_Flop"), TEXT("A_Fish_Curled") };
		constexpr int32 NumRoles = UE_ARRAY_COUNT(ClipNames);
		TestEqual(TEXT("EFishAnimRole has the 8 roles this test checks"), static_cast<int32>(StaticEnum<EFishAnimRole>()->GetMaxEnumValue()), NumRoles);

		UClass* AbpClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Art/Fish/ABP_Fish.ABP_Fish_C"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Fish/SK_Bonefish.SK_Bonefish"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		TArray<UAnimSequenceBase*> Clips;
		for (const TCHAR* Name : ClipNames)
		{
			Clips.Add(LoadClip(Name));
		}
		if (!AbpClass || !MeshAsset || Clips.Contains(nullptr))
		{
			AddInfo(TEXT("ABP_Fish, SK_Bonefish or an A_Fish clip is not loadable here: pin-order check skipped."));
			return true;
		}
		for (int32 Role = 0; Role < NumRoles; ++Role)
		{
			TestTrue(FString::Printf(TEXT("ABP_Fish has a pin for role %d"), Role), UFishAnimInstance::ClassHasRolePin(AbpClass, static_cast<EFishAnimRole>(Role)));
		}

		LureFightQA::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		// A plain actor owns both meshes, so the anim instance keeps the state set here (UpdateFromOwner needs a fight fish).
		AActor* Owner = W.World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("owner"), Owner))
		{
			return false;
		}
		auto MakeMesh = [Owner](USkeletalMesh* Asset)
		{
			USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Owner);
			Mesh->SetSkeletalMeshAsset(Asset);
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->RegisterComponent();
			return Mesh;
		};

		// References: each clip on a single-node player (additive onto the ref pose, like the graph's Local Space Ref Pose
		// base). Editor-only reference (UE warns that single-node additives don't cook; this never runs cooked).
		AddExpectedMessagePlain(TEXT("on an AnimSingleNodeInstance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		USkeletalMeshComponent* Ref = MakeMesh(MeshAsset);
		Ref->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		auto Sample = [Ref](UAnimSequenceBase* Clip, float Time)
		{
			Ref->SetAnimation(Clip);
			Ref->SetPosition(Time, false);
			return Evaluate(Ref, 0.f);
		};
		// The graph runs at play rate 0 below, so every role holds its start frame: time 0 (Dart: DartStartTime, 0 here).
		TArray<TArray<FTransform>> Refs;
		for (UAnimSequenceBase* Clip : Clips)
		{
			Refs.Add(Sample(Clip, 0.f));
		}
		for (int32 First = 0; First < NumRoles; ++First)
		{
			for (int32 Second = First + 1; Second < NumRoles; ++Second)
			{
				TestTrue(FString::Printf(TEXT("%s and %s start on distinct poses"), ClipNames[First], ClipNames[Second]), PoseError(Refs[First], Refs[Second]) > 0.1f);
			}
		}

		USkeletalMeshComponent* Fish = MakeMesh(MeshAsset);
		Fish->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Fish->SetAnimInstanceClass(AbpClass);
		UFishAnimInstance* Anim = Cast<UFishAnimInstance>(Fish->GetAnimInstance());
		if (!TestNotNull(TEXT("ABP_Fish is a UFishAnimInstance"), Anim))
		{
			return false;
		}
		auto Nearest = [&](const TArray<FTransform>& Pose, FString& All)
		{
			int32 Best = INDEX_NONE;
			float BestError = TNumericLimits<float>::Max();
			for (int32 Clip = 0; Clip < NumRoles; ++Clip)
			{
				const float ClipError = PoseError(Pose, Refs[Clip]);
				All += FString::Printf(TEXT(" %s %.3f"), ClipNames[Clip], ClipError);
				if (ClipError < BestError)
				{
					BestError = ClipError;
					Best = Clip;
				}
			}
			return Best;
		};
		auto SetRole = [Anim](EFishAnimRole Role)
		{
			FFishAnimState State;
			State.Role = Role;
			State.PlayRate = 0.f;
			State.Amplitude = 1.f;
			State.RoleBlendTime = 0.f;
			State.DartStartTime = 0.f;
			Anim->SetAnimState(State);
		};
		auto Settle = [Fish]()
		{
			TArray<FTransform> Pose;
			for (int32 Frame = 0; Frame < 10; ++Frame)
			{
				Pose = Evaluate(Fish, 1.f / 60.f);
			}
			return Pose;
		};

		for (int32 Role = 0; Role < NumRoles; ++Role)
		{
			SetRole(static_cast<EFishAnimRole>(Role));
			if (Role == static_cast<int32>(EFishAnimRole::Curled))
			{
				Anim->DisplayPose = Clips[Role]; // the cooler's own pose through the Sequence <- DisplayPose binding
				Anim->DisplayPoseTime = 0.f;
			}
			const TArray<FTransform> Pose = Settle();
			FString All;
			const int32 Best = Nearest(Pose, All);
			AddInfo(FString::Printf(TEXT("role %d: pose errors%s"), Role, *All));
			const FString RoleName = StaticEnum<EFishAnimRole>()->GetNameStringByValue(Role);
			TestEqual(FString::Printf(TEXT("role %s plays its own clip"), *RoleName), Best != INDEX_NONE ? FString(ClipNames[Best]) : FString(), FString(ClipNames[Role]));
			TestTrue(FString::Printf(TEXT("role %s: matches its clip's start frame (error %.3f)"), *RoleName, PoseError(Pose, Refs[Role])), PoseError(Pose, Refs[Role]) < 0.05f);
		}

		// Curled's Sequence pin is bound to DisplayPose (and Start Position to DisplayPoseTime): hold other clips there and
		// the graph must show them at that time, not A_Fish_Curled. (Leave Curled first so the pin re-activates.)
		for (const int32 Held : { 2, 6 }) // Thrash, Flop
		{
			SetRole(EFishAnimRole::SwimIdle);
			Settle();
			const float Time = Clips[Held]->GetPlayLength() * 0.4f;
			TestTrue(TEXT("SetHeldPose"), Anim->SetHeldPose(Clips[Held], Time));
			TestTrue(TEXT("held: role Curled"), Anim->Role == EFishAnimRole::Curled);
			const TArray<FTransform> Pose = Settle();
			const float AtTime = PoseError(Pose, Sample(Clips[Held], Time));
			const float CurledError = PoseError(Pose, Refs[static_cast<int32>(EFishAnimRole::Curled)]);
			TestTrue(FString::Printf(TEXT("Curled pin plays DisplayPose %s at DisplayPoseTime (error %.3f)"), ClipNames[Held], AtTime), AtTime < 0.05f);
			TestTrue(FString::Printf(TEXT("... not the node's own A_Fish_Curled (error %.3f)"), CurledError), CurledError > 1.f);
		}
		return true;
	}
}

#endif

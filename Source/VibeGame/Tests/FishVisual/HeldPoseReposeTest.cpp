// Lure: re-calling UFishAnimInstance::SetHeldPose on a fish that is already Curled applies the new clip and time
// (T-030l). The Curled Sequence Player reads Start Position only when its pin activates, so before the fix a re-pose
// kept the old time. Spec: Source/VibeGame/Fish/FishAnimInstance.h (SetHeldPose).

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Fish/FishAnimInstance.h"

namespace LureHeldPoseReposeTest
{
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
		return LoadObject<UAnimSequenceBase>(nullptr, *FString::Printf(TEXT("/Game/Art/Fish/%s.%s"), Name, Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	TArray<FTransform> Evaluate(USkeletalMeshComponent* Mesh, float DeltaSeconds)
	{
		Mesh->TickAnimation(DeltaSeconds, false);
		Mesh->RefreshBoneTransforms();
		return TArray<FTransform>(Mesh->GetComponentSpaceTransforms());
	}

	TArray<FTransform> Settle(USkeletalMeshComponent* Mesh)
	{
		TArray<FTransform> Pose;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			Pose = Evaluate(Mesh, 1.f / 60.f);
		}
		return Pose;
	}

	USkeletalMeshComponent* MakeMesh(AActor* Owner, USkeletalMesh* Asset)
	{
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Owner);
		Mesh->SetSkeletalMeshAsset(Asset);
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->RegisterComponent();
		return Mesh;
	}

	// ---- ABP_Fish: a fish already Curled re-poses to a new time, then to a new clip, without leaving Curled ----
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldPoseRepose, "Project.FishVisual.HeldPose.ReposeWhileCurled", LureFightQA::Flags)
	bool FHeldPoseRepose::RunTest(const FString& Parameters)
	{
		UClass* AbpClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Art/Fish/ABP_Fish.ABP_Fish_C"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Fish/SK_Bonefish.SK_Bonefish"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		UAnimSequenceBase* Thrash = LoadClip(TEXT("A_Fish_Hooked_Thrash"));
		UAnimSequenceBase* Flop = LoadClip(TEXT("A_Fish_Landed_Flop"));
		if (!AbpClass || !MeshAsset || !Thrash || !Flop || !UFishAnimInstance::ClassHasRolePin(AbpClass, EFishAnimRole::Curled))
		{
			AddInfo(TEXT("ABP_Fish (with its Curled pin), SK_Bonefish or an A_Fish clip is not loadable here: re-pose check skipped."));
			return true;
		}

		LureFightQA::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		// A plain actor owner, so the anim instance keeps the state set here (UpdateFromOwner needs a fight fish).
		AActor* Owner = W.World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("owner"), Owner))
		{
			return false;
		}

		// References on a single-node player (editor-only; UE warns that single-node additives don't cook).
		AddExpectedMessagePlain(TEXT("on an AnimSingleNodeInstance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		USkeletalMeshComponent* Ref = MakeMesh(Owner, MeshAsset);
		Ref->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		auto Sample = [Ref](UAnimSequenceBase* Clip, float Time)
		{
			Ref->SetAnimation(Clip);
			Ref->SetPosition(Time, false);
			return Evaluate(Ref, 0.f);
		};

		USkeletalMeshComponent* Fish = MakeMesh(Owner, MeshAsset);
		Fish->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Fish->SetAnimInstanceClass(AbpClass);
		UFishAnimInstance* Anim = Cast<UFishAnimInstance>(Fish->GetAnimInstance());
		if (!TestNotNull(TEXT("ABP_Fish is a UFishAnimInstance"), Anim))
		{
			return false;
		}
		Settle(Fish); // start in SwimIdle so the first hold activates the Curled pin

		const float Early = Thrash->GetPlayLength() * 0.15f;
		const float Late = Thrash->GetPlayLength() * 0.6f;
		const float FlopTime = Flop->GetPlayLength() * 0.4f;
		const TArray<FTransform> ThrashEarly = Sample(Thrash, Early);
		const TArray<FTransform> ThrashLate = Sample(Thrash, Late);
		const TArray<FTransform> FlopAt = Sample(Flop, FlopTime);
		TestTrue(TEXT("the two Thrash times are distinct poses (else the time check proves nothing)"), PoseError(ThrashEarly, ThrashLate) > 0.1f);

		TestTrue(TEXT("first hold"), Anim->SetHeldPose(Thrash, Early));
		const TArray<FTransform> First = Settle(Fish);
		TestTrue(FString::Printf(TEXT("first hold shows Thrash at its time (error %.3f)"), PoseError(First, ThrashEarly)), PoseError(First, ThrashEarly) < 0.05f);

		// Same clip, new time, still Curled: the new time must show.
		TestTrue(TEXT("re-hold, new time"), Anim->SetHeldPose(Thrash, Late));
		TestTrue(TEXT("role stays Curled"), Anim->Role == EFishAnimRole::Curled);
		const TArray<FTransform> Second = Settle(Fish);
		TestTrue(FString::Printf(TEXT("re-hold shows the new time (error %.3f)"), PoseError(Second, ThrashLate)), PoseError(Second, ThrashLate) < 0.05f);
		TestTrue(FString::Printf(TEXT("... not the old time (error %.3f)"), PoseError(Second, ThrashEarly)), PoseError(Second, ThrashEarly) > 0.1f);

		// New clip and time, still Curled.
		TestTrue(TEXT("re-hold, new clip"), Anim->SetHeldPose(Flop, FlopTime));
		const TArray<FTransform> Third = Settle(Fish);
		TestTrue(FString::Printf(TEXT("re-hold shows the new clip at its time (error %.3f)"), PoseError(Third, FlopAt)), PoseError(Third, FlopAt) < 0.05f);

		// Back to time 0 of the same clip (the cooler's usual pose time).
		TestTrue(TEXT("re-hold, time 0"), Anim->SetHeldPose(Flop, 0.f));
		const TArray<FTransform> Fourth = Settle(Fish);
		TestTrue(FString::Printf(TEXT("re-hold at time 0 (error %.3f)"), PoseError(Fourth, Sample(Flop, 0.f))), PoseError(Fourth, Sample(Flop, 0.f)) < 0.05f);
		return true;
	}

	// ---- A native UFishAnimInstance (no graph, no Curled pin): re-posing stores the values and never breaks ----
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldPoseReposeNative, "Project.FishVisual.HeldPose.ReposeNativeClassSafe", LureFightQA::Flags)
	bool FHeldPoseReposeNative::RunTest(const FString& Parameters)
	{
		USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Fish/SK_Bonefish.SK_Bonefish"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		UAnimSequenceBase* Thrash = LoadClip(TEXT("A_Fish_Hooked_Thrash"));
		UAnimSequenceBase* Flop = LoadClip(TEXT("A_Fish_Landed_Flop"));
		if (!MeshAsset || !Thrash || !Flop)
		{
			AddInfo(TEXT("SK_Bonefish or an A_Fish clip is not loadable here: native re-pose check skipped."));
			return true;
		}
		LureFightQA::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		AActor* Owner = W.World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("owner"), Owner))
		{
			return false;
		}
		USkeletalMeshComponent* Fish = MakeMesh(Owner, MeshAsset);
		Fish->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Fish->SetAnimInstanceClass(UFishAnimInstance::StaticClass());
		UFishAnimInstance* Anim = Cast<UFishAnimInstance>(Fish->GetAnimInstance());
		if (!TestNotNull(TEXT("native UFishAnimInstance"), Anim))
		{
			return false;
		}
		TestFalse(TEXT("a native class has no Curled pin (callers fall back to single-node)"), Anim->CanPlayRole(EFishAnimRole::Curled));
		TestTrue(TEXT("first hold"), Anim->SetHeldPose(Thrash, 0.1f));
		Settle(Fish);
		TestTrue(TEXT("re-hold"), Anim->SetHeldPose(Flop, 0.3f));
		const TArray<FTransform> Pose = Settle(Fish);
		TestTrue(TEXT("re-hold stores the new clip"), Anim->DisplayPose == Flop);
		TestEqual(TEXT("re-hold stores the new time"), Anim->DisplayPoseTime, 0.3f);
		TestTrue(TEXT("still held and Curled"), Anim->HasHeldPose() && Anim->Role == EFishAnimRole::Curled);
		TestTrue(TEXT("the mesh still evaluates"), Pose.Num() > 0);
		TestFalse(TEXT("a null re-hold is ignored"), Anim->SetHeldPose(nullptr, 1.f));
		TestTrue(TEXT("... and keeps the last pose"), Anim->DisplayPose == Flop && Anim->DisplayPoseTime == 0.3f);
		return true;
	}
}

#endif

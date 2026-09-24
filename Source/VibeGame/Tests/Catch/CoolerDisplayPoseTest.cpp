// Lure T-030f tests (unreal-engineer): the fish shown in an open cooler hold their DT_CoolerDisplay pose through the fish anim
// class (ABP_Fish's Curled pin, cook-safe), and fall back to the single-node player without that class or pin.
// Rules: docs/specs/catch-handling-rules.md "The cooler". Project.Catch.Display.Pose*, Project.FishVisual.HeldPose.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/FishVisualSettings.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Progression/LureCoolerComponent.h"
#include "Tests/Catch/CoolerDisplayTestFishAnim.h"

namespace LureCoolerDisplayPoseTest
{
	/** The skinned fish shown under the cooler's Contents point */
	TArray<USkeletalMeshComponent*> ShownSkinned(const ALureCoolerActor* Cooler)
	{
		TArray<USkeletalMeshComponent*> Out;
		TInlineComponentArray<USkeletalMeshComponent*> Parts(Cooler);
		for (USkeletalMeshComponent* Part : Parts)
		{
			const USceneComponent* Parent = Part->GetAttachParent();
			if (Parent && Parent->GetFName() == TEXT("ContentsRoot") && Part->IsVisible())
			{
				Out.Add(Part);
			}
		}
		return Out;
	}

	/** A starter cooler with two bonefish, lid open and ticked; null on failure (test errors added) */
	ALureCoolerActor* OpenCoolerWithFish(FAutomationTestBase& Test, LureCatchTest::FWorld& W, float Y)
	{
		ALureCoolerActor* Cooler = W.SpawnCooler(FVector(150.0f, Y, LureCatchTest::DockTop), 180.0f);
		if (!Test.TestNotNull(TEXT("cooler"), Cooler))
		{
			return nullptr;
		}
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LureCatchTest::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 410), W.Now()));
		Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LureCatchTest::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 411), W.Now()));
		Cooler->AuthoritySetLidOpen(true);
		W.Tick(30);
		Test.TestEqual(TEXT("open: both fish show"), Cooler->GetNumDisplayedFish(), 2);
		return Cooler;
	}

	/** Sets the fish visual settings' AnimClass for the scope of a test */
	struct FAnimClassScope
	{
		TSoftClassPtr<UAnimInstance> Was;
		explicit FAnimClassScope(const TSoftClassPtr<UAnimInstance>& Class)
		{
			ULureFishVisualSettings* Settings = GetMutableDefault<ULureFishVisualSettings>();
			Was = Settings->AnimClass;
			Settings->AnimClass = Class;
		}
		~FAnimClassScope() { GetMutableDefault<ULureFishVisualSettings>()->AnimClass = Was; }
	};

	/** Checks every shown skinned fish plays Pose on the single-node player (the fallback) */
	void ExpectSingleNode(FAutomationTestBase& Test, const TArray<USkeletalMeshComponent*>& Shown, const UAnimSequenceBase* Pose, const TCHAR* Why)
	{
		for (USkeletalMeshComponent* Fish : Shown)
		{
			Test.TestEqual(FString::Printf(TEXT("%s: single-node mode"), Why), static_cast<int32>(Fish->GetAnimationMode()), static_cast<int32>(EAnimationMode::AnimationSingleNode));
			UAnimSingleNodeInstance* Single = Fish->GetSingleNodeInstance();
			Test.TestTrue(FString::Printf(TEXT("%s: ... playing the table's pose"), Why), Single && Single->GetCurrentAsset() == Pose);
			Test.TestTrue(FString::Printf(TEXT("%s: ... and the fish is visible"), Why), Fish->IsVisible() && Fish->GetSkeletalMeshAsset() != nullptr);
		}
	}
}

// ---- The display fish get the fish anim class and hold the Curled role ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerDisplayPoseThroughFishAnim, "Project.Catch.Display.PoseThroughFishAnim", LCT::Flags)
bool FCoolerDisplayPoseThroughFishAnim::RunTest(const FString& Parameters)
{
	const LureCoolerDisplayPoseTest::FAnimClassScope Scope{TSoftClassPtr<UAnimInstance>(UCoolerDisplayTestFishAnim::StaticClass())};
	LCT::FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	ALureCoolerActor* Cooler = LureCoolerDisplayPoseTest::OpenCoolerWithFish(*this, W, 150.0f);
	if (!Cooler)
	{
		return false;
	}
	const FLureCoolerDisplayRow Row = ULureCatchSubsystem::Get(W.World)->GetCoolerDisplayRow(Cooler->GetCoolerId());
	const UAnimSequenceBase* Pose = Row.FishPose.LoadSynchronous();
	if (!TestNotNull(TEXT("the table's pose clip loads (A_Fish_Curled)"), Pose))
	{
		return false;
	}
	const TArray<USkeletalMeshComponent*> Shown = LureCoolerDisplayPoseTest::ShownSkinned(Cooler);
	TestEqual(TEXT("both shown fish are skinned (SK_Bonefish)"), Shown.Num(), 2);
	for (USkeletalMeshComponent* Fish : Shown)
	{
		TestEqual(TEXT("Anim Blueprint mode, not single node"), static_cast<int32>(Fish->GetAnimationMode()), static_cast<int32>(EAnimationMode::AnimationBlueprint));
		TestTrue(TEXT("... with the settings' fish anim class"), Fish->GetAnimClass() == UCoolerDisplayTestFishAnim::StaticClass());
		const UFishAnimInstance* Anim = Cast<UFishAnimInstance>(Fish->GetAnimInstance());
		if (!TestNotNull(TEXT("... a fish anim instance"), Anim))
		{
			continue;
		}
		TestTrue(TEXT("role Curled"), Anim->Role == EFishAnimRole::Curled);
		TestTrue(TEXT("held pose set"), Anim->HasHeldPose());
		TestTrue(TEXT("DisplayPose = the table's clip"), Anim->DisplayPose == Pose);
		TestEqual(TEXT("DisplayPoseTime = the table's PoseTime"), Anim->DisplayPoseTime, Row.PoseTime);
		TestEqual(TEXT("alpha 1"), Anim->Amplitude, 1.0f);
		TestEqual(TEXT("play rate 0 (held)"), Anim->PlayRate, 0.0f);
		TestEqual(TEXT("no blend in"), Anim->RoleBlendTime, 0.0f);
		TestTrue(TEXT("visible"), Fish->IsVisible());
	}
	// The anim updates keep the held role (the cooler is no fight fish, and a held pose skips the owner anyway).
	W.Tick(20);
	for (USkeletalMeshComponent* Fish : LureCoolerDisplayPoseTest::ShownSkinned(Cooler))
	{
		const UFishAnimInstance* Anim = Cast<UFishAnimInstance>(Fish->GetAnimInstance());
		TestTrue(TEXT("after updates: still Curled"), Anim && Anim->Role == EFishAnimRole::Curled && Anim->DisplayPose == Pose);
	}
	return true;
}

// ---- Without the class or the pin: today's single-node player, never invisible ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerDisplayPoseFallback, "Project.Catch.Display.PoseFallbackSingleNode", LCT::Flags)
bool FCoolerDisplayPoseFallback::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	{
		// No AnimClass set: the fish anim class resolves to native UFishAnimInstance, which has no graph and so no Curled pin.
		const LureCoolerDisplayPoseTest::FAnimClassScope Scope{TSoftClassPtr<UAnimInstance>()};
		ALureCoolerActor* Cooler = LureCoolerDisplayPoseTest::OpenCoolerWithFish(*this, W, 150.0f);
		if (!Cooler)
		{
			return false;
		}
		const FLureCoolerDisplayRow Row = ULureCatchSubsystem::Get(W.World)->GetCoolerDisplayRow(Cooler->GetCoolerId());
		const UAnimSequenceBase* Pose = Row.FishPose.LoadSynchronous();
		const TArray<USkeletalMeshComponent*> Shown = LureCoolerDisplayPoseTest::ShownSkinned(Cooler);
		TestEqual(TEXT("no anim class: both fish still show"), Shown.Num(), 2);
		LureCoolerDisplayPoseTest::ExpectSingleNode(*this, Shown, Pose, TEXT("no anim class"));
	}
	{
		// The shipped setting (ABP_Fish): the graph path exactly when its Blend Poses has a Curled pin, else the fallback.
		const LureCoolerDisplayPoseTest::FAnimClassScope Scope{GetDefault<ULureFishVisualSettings>()->AnimClass};
		const FSoftObjectPath AbpPath = GetDefault<ULureFishVisualSettings>()->AnimClass.ToSoftObjectPath();
		UClass* Abp = FPackageName::DoesPackageExist(AbpPath.GetLongPackageName()) ? Cast<UClass>(AbpPath.TryLoad()) : nullptr;
		if (!Abp)
		{
			AddInfo(FString::Printf(TEXT("%s is not in this checkout: ABP_Fish part skipped"), *AbpPath.ToString()));
			return true;
		}
		TestTrue(TEXT("ABP_Fish: the pin reader finds its SwimIdle (Default) pin"), UFishAnimInstance::ClassHasRolePin(Abp, EFishAnimRole::SwimIdle));
		TestTrue(TEXT("ABP_Fish: ... and its Flop pin"), UFishAnimInstance::ClassHasRolePin(Abp, EFishAnimRole::Flop));
		const bool bCurledPin = UFishAnimInstance::ClassHasRolePin(Abp, EFishAnimRole::Curled);
		AddInfo(FString::Printf(TEXT("ABP_Fish has the Curled pin: %s"), bCurledPin ? TEXT("yes (graph path)") : TEXT("not yet (single-node fallback)")));
		ALureCoolerActor* Cooler = LureCoolerDisplayPoseTest::OpenCoolerWithFish(*this, W, -150.0f);
		if (!Cooler)
		{
			return false;
		}
		const FLureCoolerDisplayRow Row = ULureCatchSubsystem::Get(W.World)->GetCoolerDisplayRow(Cooler->GetCoolerId());
		const UAnimSequenceBase* Pose = Row.FishPose.LoadSynchronous();
		const TArray<USkeletalMeshComponent*> Shown = LureCoolerDisplayPoseTest::ShownSkinned(Cooler);
		TestEqual(TEXT("ABP_Fish: both fish show"), Shown.Num(), 2);
		if (bCurledPin)
		{
			for (USkeletalMeshComponent* Fish : Shown)
			{
				const UFishAnimInstance* Anim = Cast<UFishAnimInstance>(Fish->GetAnimInstance());
				TestTrue(TEXT("ABP_Fish with the pin: an ABP_Fish instance holding Curled"), Fish->GetAnimClass() == Abp && Anim && Anim->Role == EFishAnimRole::Curled && Anim->DisplayPose == Pose);
			}
		}
		else
		{
			LureCoolerDisplayPoseTest::ExpectSingleNode(*this, Shown, Pose, TEXT("ABP_Fish without the Curled pin"));
		}
	}
	return true;
}

// ---- The anim instance's held-pose API and the role guard ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishHeldPoseApi, "Project.FishVisual.HeldPose.Api", LCT::Flags)
bool FFishHeldPoseApi::RunTest(const FString& Parameters)
{
	UFishAnimInstance* Anim = NewObject<UFishAnimInstance>(GetTransientPackage());
	TestFalse(TEXT("a fresh instance holds nothing"), Anim->HasHeldPose());
	TestFalse(TEXT("a null pose is ignored"), Anim->SetHeldPose(nullptr, 0.0f));
	TestTrue(TEXT("... the role is unchanged"), Anim->Role == EFishAnimRole::SwimIdle && !Anim->HasHeldPose());

	// Any anim asset object works for the API (it only stores the reference).
	UAnimSequenceBase* Pose = NewObject<UAnimSequence>(GetTransientPackage());
	TestTrue(TEXT("a pose is held"), Anim->SetHeldPose(Pose, 0.25f));
	TestTrue(TEXT("... Curled, the pose, its time"), Anim->Role == EFishAnimRole::Curled && Anim->DisplayPose == Pose && Anim->DisplayPoseTime == 0.25f);
	TestTrue(TEXT("... alpha 1, rate 0, no blend"), Anim->Amplitude == 1.0f && Anim->PlayRate == 0.0f && Anim->RoleBlendTime == 0.0f);
	TestTrue(TEXT("a negative time clamps to 0"), Anim->SetHeldPose(Pose, -3.0f) && Anim->DisplayPoseTime == 0.0f);
	TestTrue(TEXT("a NaN time is 0"), Anim->SetHeldPose(Pose, NAN) && Anim->DisplayPoseTime == 0.0f);

	TestFalse(TEXT("no class: no pin"), UFishAnimInstance::ClassHasRolePin(nullptr, EFishAnimRole::Curled));
	TestFalse(TEXT("native UFishAnimInstance (no graph): no Curled pin"), UFishAnimInstance::ClassHasRolePin(UFishAnimInstance::StaticClass(), EFishAnimRole::Curled));
	TestFalse(TEXT("... nor a SwimIdle one"), UFishAnimInstance::ClassHasRolePin(UFishAnimInstance::StaticClass(), EFishAnimRole::SwimIdle));
	TestFalse(TEXT("an instance of it can't play Curled"), Anim->CanPlayRole(EFishAnimRole::Curled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishHeldPoseNotAFightRole, "Project.FishVisual.HeldPose.NotAFightRole", LCT::Flags)
bool FFishHeldPoseNotAFightRole::RunTest(const FString& Parameters)
{
	FString Problem;
	TestTrue(TEXT("the built-in row is valid"), FFishVisualRow::GetFallbackRow().Validate(Problem));

	FFishVisualRow Row = FFishVisualRow::GetFallbackRow();
	FFishMoveAnimRole Entry;
	Entry.MoveId = TEXT("Curl");
	Entry.Role = EFishAnimRole::Curled;
	Row.MoveRoles.Add(Entry);
	Problem.Reset();
	TestFalse(TEXT("a fight move can't play Curled"), Row.Validate(Problem));
	TestTrue(FString::Printf(TEXT("... and the problem says so (%s)"), *Problem), Problem.Contains(TEXT("Curled")));

	Row = FFishVisualRow::GetFallbackRow();
	Row.UnknownMoveRole = EFishAnimRole::Curled;
	Problem.Reset();
	TestFalse(TEXT("UnknownMoveRole can't be Curled"), Row.Validate(Problem));

	TestEqual(TEXT("Curled comes after Flop (appended: the ABP_Fish pins keep their values)"), static_cast<int32>(EFishAnimRole::Curled), static_cast<int32>(EFishAnimRole::Flop) + 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

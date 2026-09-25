// Lure T-063 tests (unreal-engineer): on the carrier's own machine the carried cooler sits on the first-person arms' `cooler`
// bone (ULureCatchSettings::CarriedCoolerSocket) with a zero relative transform, so it rides the CarryCooler clip's idle breath
// with the fists. Arms without that bone keep the fixed CarriedCoolerOffset; no arms mesh keeps the camera fallback.
// Spec: art/export/Characters/SK_FPArms.anim.md "CarryCooler", docs/specs/catch-handling-rules.md. Project.Catch.HeldCooler.Bone.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCoolerActor.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"

namespace LureHeldCoolerBoneTest
{
	/** SK_FPArms.anim.md: the cooler bone's reference position (arms component space, cm) */
	const FVector SpecCoolerBone(36.2f, 0.0f, -51.6f);

	/** Restores CarriedCoolerSocket when a test changes it */
	struct FSocketGuard
	{
		FName Saved;
		FSocketGuard() : Saved(GetDefault<ULureCatchSettings>()->CarriedCoolerSocket) {}
		~FSocketGuard() { GetMutableDefault<ULureCatchSettings>()->CarriedCoolerSocket = Saved; }
		void Set(FName Socket) { GetMutableDefault<ULureCatchSettings>()->CarriedCoolerSocket = Socket; }
	};

	/** A player on the dock and a cooler in front of him; bRemoveArmsMesh empties the arms before the pick-up */
	bool PickUp(FAutomationTestBase& Test, LCT::FWorld& W, ALurePlayerCharacter*& OutPlayer, ALureCoolerActor*& OutCooler, bool bRemoveArmsMesh = false)
	{
		if (!W.Create(Test))
		{
			return false;
		}
		OutPlayer = W.SpawnPlayer(Test, FVector(0.0f, 0.0f, LCT::DockTop));
		if (!Test.TestNotNull(TEXT("player"), OutPlayer) || !Test.TestNotNull(TEXT("arms"), OutPlayer->GetFirstPersonArms()))
		{
			return false;
		}
		W.Tick(5);
		if (bRemoveArmsMesh)
		{
			OutPlayer->GetFirstPersonArms()->SetSkeletalMeshAsset(nullptr);
		}
		OutCooler = W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f);
		if (!Test.TestNotNull(TEXT("cooler"), OutCooler) || !Test.TestTrue(TEXT("picked up"), OutCooler->AuthorityPickUp(OutPlayer)))
		{
			return false;
		}
		W.Tick(3);
		return Test.TestNotNull(TEXT("cooler root"), OutCooler->GetRootComponent());
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerBoneDefault, "Project.Catch.HeldCooler.Bone.SettingDefault", LCT::Flags)
	bool FHeldCoolerBoneDefault::RunTest(const FString& Parameters)
	{
		TestEqual(TEXT("the carried cooler goes on the CarryCooler attach bone"), GetDefault<ULureCatchSettings>()->CarriedCoolerSocket, FName(TEXT("cooler")));
		// Acceptance 6: where the clip puts the cooler vs the old fixed offset (the lead's PIE playtest judges the look).
		const FVector OldOffset = GetDefault<ULureCatchSettings>()->CarriedCoolerOffset;
		AddInfo(FString::Printf(TEXT("cooler bone reference %s vs old offset %s: %.1f cm apart (delta %s)"), *SpecCoolerBone.ToString(),
			*OldOffset.ToString(), FVector::Dist(SpecCoolerBone, OldOffset), *(SpecCoolerBone - OldOffset).ToString()));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerBoneFollows, "Project.Catch.HeldCooler.Bone.FollowsCoolerBone", LCT::Flags)
	bool FHeldCoolerBoneFollows::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ALureCoolerActor* Cooler = nullptr;
		if (!PickUp(*this, W, Player, Cooler))
		{
			return false;
		}
		const FName Socket = GetDefault<ULureCatchSettings>()->CarriedCoolerSocket;
		USkeletalMeshComponent* Arms = Player->GetFirstPersonArms();
		USceneComponent* Root = Cooler->GetRootComponent();
		if (!Arms->GetSkeletalMeshAsset() || !Arms->DoesSocketExist(Socket))
		{
			AddWarning(FString::Printf(TEXT("SK_FPArms (or its bone %s) is not loaded here: only the fallback is checked"), *Socket.ToString()));
			TestTrue(TEXT("no bone: the old fallback parent"), Root->GetAttachParent() == (Arms->GetSkeletalMeshAsset() ? static_cast<USceneComponent*>(Arms) : Player->GetFirstPersonCamera()));
			return true;
		}

		// Acceptance 1: on the bone, zero relative transform (fails on the old code: socket None, relative = the fixed offset).
		TestTrue(TEXT("attached to the first-person arms"), Root->GetAttachParent() == Arms);
		TestEqual(TEXT("on bone cooler"), Root->GetAttachSocketName(), Socket);
		TestTrue(FString::Printf(TEXT("zero relative location (%s)"), *Root->GetRelativeLocation().ToString()), Root->GetRelativeLocation().IsNearlyZero(1.0e-3f));
		TestTrue(FString::Printf(TEXT("zero relative rotation (%s)"), *Root->GetRelativeRotation().ToString()), Root->GetRelativeRotation().IsNearlyZero(1.0e-3f));

		// Acceptance 2: the pivot equals the bone every frame. Force the pose to tick without a renderer so the clip can move it.
		Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		const FVector FirstBoneLocal = Arms->GetSocketTransform(Socket, RTS_Component).GetLocation();
		AddInfo(FString::Printf(TEXT("cooler bone in arms space at pick-up: %s (spec reference %s)"), *FirstBoneLocal.ToString(), *SpecCoolerBone.ToString()));
		float MaxBoneMove = 0.0f;
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			W.Tick(1, 0.1f);
			const FTransform Bone = Arms->GetSocketTransform(Socket);
			const FTransform Pivot = Root->GetComponentTransform();
			const float Dist = FVector::Dist(Bone.GetLocation(), Pivot.GetLocation());
			const float AngleDeg = FMath::RadiansToDegrees(Bone.GetRotation().AngularDistance(Pivot.GetRotation()));
			if (!TestTrue(FString::Printf(TEXT("frame %d: the pivot is on the bone (%.4f cm, %.4f deg)"), Frame, Dist, AngleDeg), Dist < 0.1f && AngleDeg < 0.1f))
			{
				break;
			}
			MaxBoneMove = FMath::Max(MaxBoneMove, FVector::Dist(Arms->GetSocketTransform(Socket, RTS_Component).GetLocation(), FirstBoneLocal));
		}
		AddInfo(FString::Printf(TEXT("the cooler bone moved up to %.3f cm in arms space over 3 s (0 = the idle clip does not tick headless)"), MaxBoneMove));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerBoneMissing, "Project.Catch.HeldCooler.Bone.MissingBoneKeepsOffset", LCT::Flags)
	bool FHeldCoolerBoneMissing::RunTest(const FString& Parameters)
	{
		FSocketGuard Guard;
		Guard.Set(TEXT("no_such_bone"));
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ALureCoolerActor* Cooler = nullptr;
		if (!PickUp(*this, W, Player, Cooler))
		{
			return false;
		}
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		USkeletalMeshComponent* Arms = Player->GetFirstPersonArms();
		USceneComponent* Root = Cooler->GetRootComponent();
		if (!Arms->GetSkeletalMeshAsset())
		{
			AddWarning(TEXT("SK_FPArms is not loaded here: the camera fallback is checked instead"));
			TestTrue(TEXT("no arms mesh: on the camera"), Root->GetAttachParent() == Player->GetFirstPersonCamera());
			return true;
		}
		TestTrue(TEXT("a missing bone: on the arms component"), Root->GetAttachParent() == Arms);
		TestTrue(TEXT("... no socket"), Root->GetAttachSocketName().IsNone());
		TestTrue(FString::Printf(TEXT("... at the old fixed offset (%s)"), *Root->GetRelativeLocation().ToString()), Root->GetRelativeLocation().Equals(Settings->CarriedCoolerOffset, 1.0e-3f));
		TestTrue(TEXT("... with the old rotation"), Root->GetRelativeRotation().Equals(Settings->CarriedCoolerRotation, 1.0e-3f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerBoneNone, "Project.Catch.HeldCooler.Bone.SettingNoneKeepsOffset", LCT::Flags)
	bool FHeldCoolerBoneNone::RunTest(const FString& Parameters)
	{
		FSocketGuard Guard;
		Guard.Set(NAME_None);
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ALureCoolerActor* Cooler = nullptr;
		if (!PickUp(*this, W, Player, Cooler))
		{
			return false;
		}
		USceneComponent* Root = Cooler->GetRootComponent();
		USceneComponent* Expected = Player->GetFirstPersonArms()->GetSkeletalMeshAsset() ? static_cast<USceneComponent*>(Player->GetFirstPersonArms()) : Player->GetFirstPersonCamera();
		TestTrue(TEXT("setting None: on the arms component (camera without arms), no socket"), Root->GetAttachParent() == Expected && Root->GetAttachSocketName().IsNone());
		TestTrue(TEXT("setting None: at the old fixed offset"), Root->GetRelativeLocation().Equals(GetDefault<ULureCatchSettings>()->CarriedCoolerOffset, 1.0e-3f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldCoolerBoneNoArms, "Project.Catch.HeldCooler.Bone.NoArmsMeshUsesCamera", LCT::Flags)
	bool FHeldCoolerBoneNoArms::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ALureCoolerActor* Cooler = nullptr;
		if (!PickUp(*this, W, Player, Cooler, true))
		{
			return false;
		}
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		USceneComponent* Root = Cooler->GetRootComponent();
		if (Player->GetFirstPersonArms()->GetSkeletalMeshAsset())
		{
			AddWarning(TEXT("the player re-assigned the arms mesh after it was cleared: the camera fallback was not reachable"));
			return true;
		}
		TestTrue(TEXT("no arms mesh: on the camera"), Root->GetAttachParent() == Player->GetFirstPersonCamera());
		TestTrue(TEXT("... no socket"), Root->GetAttachSocketName().IsNone());
		TestTrue(FString::Printf(TEXT("... at the fixed offset (%s)"), *Root->GetRelativeLocation().ToString()), Root->GetRelativeLocation().Equals(Settings->CarriedCoolerOffset, 1.0e-3f));
		return true;
	}
}

#endif

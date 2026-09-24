// Lure T-030e tests (unreal-engineer): the fish in the owner's first-person hand sits on the HoldFish clip's hand_r_fish bone
// with rotation 0, at -S x Grip + (1 - S) x Contact, so the right palm's contact (under the gills) stays on the palm at any
// size, and it is drawn as a first-person primitive (no collision, no shadow). Spec: art/export/Characters/SK_FPArms.anim.md
// "HoldFish". Project.Catch.HoldFishAttach.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureFishItem.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace LureCatchHoldFishAttachTest
{
	/** The spec's numbers (SK_FPArms.anim.md "HoldFish") */
	const FVector SpecContact(7.6f, 2.9f, -4.2f);
	const FVector BonefishGrip(9.43f, 0.0f, 0.0f);
	const FVector CoralSnapperGrip(9.66f, 0.0f, 0.0f);
	/** hand_r_fish at HoldFish frame 0 in arms component space */
	const FTransform SpecHandFishFrame(FRotator(5.1f, 79.7f, -15.0f), FVector(46.9f, 8.0f, -16.8f));

	/** Where a point of the fish (its space at scale 1, cm) lands in the hand bone's frame: the fish scaled by S, placed at Location */
	FVector FishPointInBone(const FVector& PointAtScale1, float Scale, const FVector& Location)
	{
		return Location + Scale * PointAtScale1;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchHoldFishAttachFormula, "Project.Catch.HoldFishAttach.GripFormula", LCT::Flags)
	bool FCatchHoldFishAttachFormula::RunTest(const FString& Parameters)
	{
		for (const FVector& Grip : { BonefishGrip, CoralSnapperGrip })
		{
			const FString Fish = Grip.Equals(BonefishGrip) ? TEXT("Bonefish") : TEXT("CoralSnapper");

			// S = 1: just -Grip, so the Grip bone sits on the hand bone.
			const FVector AtOne = ALureFishItem::ComputeHeldFishLocation(Grip, 1.0f, SpecContact);
			TestTrue(FString::Printf(TEXT("%s S = 1: relative location = -Grip (%s)"), *Fish, *AtOne.ToString()), AtOne.Equals(-Grip, 1.0e-4f));
			TestTrue(FString::Printf(TEXT("%s S = 1: the Grip bone is on the hand bone"), *Fish), FishPointInBone(Grip, 1.0f, AtOne).IsNearlyZero(1.0e-4f));

			// Any S: the right palm's contact (Grip + Contact in fish space) lands on Contact in the bone, so on the same arms point.
			const FVector ContactAtOne = SpecHandFishFrame.TransformPosition(FishPointInBone(Grip + SpecContact, 1.0f, AtOne));
			for (const float Scale : { 0.8f, 1.3f, 1.6f })
			{
				const FVector Location = ALureFishItem::ComputeHeldFishLocation(Grip, Scale, SpecContact);
				const FVector InBone = FishPointInBone(Grip + SpecContact, Scale, Location);
				TestTrue(FString::Printf(TEXT("%s S = %.1f: the contact is on the palm contact in the bone (%s)"), *Fish, Scale, *InBone.ToString()),
					InBone.Equals(SpecContact, 1.0e-3f));
				const FVector InArms = SpecHandFishFrame.TransformPosition(InBone);
				TestTrue(FString::Printf(TEXT("%s S = %.1f: the contact is at the same arms-space point as at S = 1 (%s vs %s)"), *Fish, Scale,
					*InArms.ToString(), *ContactAtOne.ToString()), InArms.Equals(ContactAtOne, 1.0e-3f));
			}

			// Spelled out for S = 1.6: -1.6 x Grip - 0.6 x Contact.
			const FVector Trophy = ALureFishItem::ComputeHeldFishLocation(Grip, 1.6f, SpecContact);
			TestTrue(FString::Printf(TEXT("%s S = 1.6: -1.6 Grip - 0.6 Contact (%s)"), *Fish, *Trophy.ToString()),
				Trophy.Equals(-1.6f * Grip - 0.6f * SpecContact, 1.0e-3f));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchHoldFishAttachSettings, "Project.Catch.HoldFishAttach.SettingsDefaults", LCT::Flags)
	bool FCatchHoldFishAttachSettings::RunTest(const FString& Parameters)
	{
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		TestEqual(TEXT("the held fish goes on the HoldFish attach bone"), Settings->HeldFishSocket, FName(TEXT("hand_r_fish")));
		TestTrue(FString::Printf(TEXT("the palm contact is the spec's (7.6, 2.9, -4.2) (%s)"), *Settings->HeldFishContactPoint.ToString()),
			Settings->HeldFishContactPoint.Equals(SpecContact, 1.0e-4f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchHoldFishAttachInHand, "Project.Catch.HoldFishAttach.InHandFirstPerson", LCT::Flags)
	bool FCatchHoldFishAttachInHand::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(0.0f, 0.0f, LCT::DockTop));
		if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("arms"), Player->GetFirstPersonArms()))
		{
			return false;
		}
		W.Tick(5);
		// A heavy Bonefish: drawn above scale 1, so the scale is in play.
		ALureFishItem* Fish = W.LandInHand(Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 12.0f, 7));
		if (!TestNotNull(TEXT("fish in hand"), Fish))
		{
			return false;
		}
		W.Tick(3);
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		const float Scale = Fish->GetWeightScale();
		USceneComponent* Root = Fish->GetRootComponent();
		UPrimitiveComponent* Mesh = Fish->GetFishMesh();
		if (!TestNotNull(TEXT("item root"), Root) || !TestNotNull(TEXT("fish mesh"), Mesh) || !TestTrue(TEXT("a positive scale"), Scale > 0.0f))
		{
			return false;
		}
		const FVector GripAtOne = Fish->GetGripOffset() / Scale;

		// First-person primitive like the rod: FirstPerson type, no collision, no shadow.
		TestTrue(TEXT("the fish mesh is a first-person primitive"), Mesh->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson);
		TestFalse(TEXT("the fish mesh has no collision"), Mesh->IsCollisionEnabled());
		TestFalse(TEXT("the fish mesh casts no shadow in first person"), Mesh->CastShadow);

		USkeletalMeshComponent* Arms = Player->GetFirstPersonArms();
		const bool bArmsReady = Arms->GetSkeletalMeshAsset() && Arms->DoesSocketExist(Settings->HeldFishSocket);
		if (!bArmsReady)
		{
			// Lane content without the re-imported SK_FPArms: the camera fallback (unchanged) holds it.
			AddWarning(FString::Printf(TEXT("SK_FPArms (or its bone %s) is not loaded here: only the camera fallback is checked"), *Settings->HeldFishSocket.ToString()));
			TestTrue(TEXT("no arms bone: on the camera"), Root->GetAttachParent() == Player->GetFirstPersonCamera());
			return true;
		}

		TestTrue(TEXT("attached to the first-person arms"), Root->GetAttachParent() == Arms);
		TestEqual(TEXT("on bone hand_r_fish"), Root->GetAttachSocketName(), FName(TEXT("hand_r_fish")));
		TestTrue(FString::Printf(TEXT("relative rotation 0 (%s)"), *Root->GetRelativeRotation().ToString()),
			Root->GetRelativeRotation().IsNearlyZero(1.0e-3f));
		const FVector Expected = ALureFishItem::ComputeHeldFishLocation(GripAtOne, Scale, Settings->HeldFishContactPoint);
		TestTrue(FString::Printf(TEXT("relative location = -S Grip + (1 - S) Contact at S = %.3f (%s vs %s)"), Scale,
			*Root->GetRelativeLocation().ToString(), *Expected.ToString()), Root->GetRelativeLocation().Equals(Expected, 1.0e-2f));

		// In the world: the fish's own palm-contact point is on the bone's contact point (the mesh scale is applied once).
		const FTransform Bone = Arms->GetSocketTransform(Settings->HeldFishSocket);
		const FVector ContactOnHand = Bone.TransformPosition(Settings->HeldFishContactPoint);
		const bool bSkeletal = Mesh->IsA<USkeletalMeshComponent>();
		if (!bSkeletal)
		{
			AddWarning(TEXT("SK_Bonefish is not loaded here (placeholder shape): the world check uses the item root at scale S"));
		}
		// The skeletal fish mesh carries the scale S itself (EnsureLook); the placeholder's is squashed, so use the root there.
		const FVector ContactOnFish = bSkeletal
			? Mesh->GetComponentTransform().TransformPosition(GripAtOne + Settings->HeldFishContactPoint)
			: Root->GetComponentTransform().TransformPosition(Scale * (GripAtOne + Settings->HeldFishContactPoint));
		TestTrue(FString::Printf(TEXT("the throat is on the right palm in the world (%.3f cm apart)"), FVector::Dist(ContactOnFish, ContactOnHand)),
			FVector::Dist(ContactOnFish, ContactOnHand) < 0.05f);
		TestTrue(TEXT("the fish is not rotated against the bone"), Root->GetComponentQuat().Equals(Bone.GetRotation(), 1.0e-3f));
		if (bSkeletal)
		{
			TestTrue(FString::Printf(TEXT("the fish mesh is drawn at scale S = %.3f"), Scale), Mesh->GetComponentScale().Equals(FVector(Scale), 1.0e-3f));
		}
		return true;
	}
}

#endif

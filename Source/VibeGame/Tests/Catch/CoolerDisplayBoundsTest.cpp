// Lure T-030m tests (unreal-engineer): every fish shown in an open starter cooler lies inside the liner and under the
// closed lid, whatever the DT_CoolerDisplay slot layout is (measured per vertex in the held pose, not hard-coded slots).
// Rules: docs/specs/catch-handling-rules.md "Contents display". Project.Catch.Display.FishInsideLiner

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureCoolerActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishTypes.h"
#include "Fish/LureFightFishSubsystem.h"
#include "Progression/LureCoolerComponent.h"

namespace LureCoolerDisplayBoundsTest
{
	/** The starter cooler's inner space in Contents-socket space (cm): the liner footprint at the floor
	 *  (sm_cooler_starter.py HX0/HY0 0.190/0.270 + LINER_FLOOR -0.016; the walls flare outward above), from the floor
	 *  (the Contents socket) up to the closed lid's underside (33.0 - 5.0, SK_Fish.anim.md). An AABB check: the
	 *  superellipse corners are not checked. */
	const FBox InnerBox(FVector(-17.5, -25.5, 0.0), FVector(17.5, 25.5, 28.0));
	constexpr double ToleranceCm = 1.0;

	struct FPileFish
	{
		FName Species;
		float WeightKg = 1.0f;
	};

	/** Poses every shown skinned fish now: headless has no rendering, so the bones are never refreshed on their own */
	void RefreshShownPoses(ALureCoolerActor* Cooler)
	{
		TInlineComponentArray<USkeletalMeshComponent*> Parts(Cooler);
		for (USkeletalMeshComponent* Part : Parts)
		{
			if (Part->GetAttachParent() != Cooler->GetContentsRoot() || !Part->IsVisible())
			{
				continue;
			}
			Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			for (int32 Step = 0; Step < 10; ++Step)
			{
				Part->TickAnimation(1.0f / 60.0f, false);
				Part->RefreshBoneTransforms();
			}
		}
	}

	FString DescribeBox(const FBox& Box)
	{
		return FString::Printf(TEXT("X %.2f..%.2f, Y %.2f..%.2f, Z %.2f..%.2f"), Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Box.Min.Z, Box.Max.Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerDisplayFishInsideLiner, "Project.Catch.Display.FishInsideLiner", LureCatchTest::Flags)
bool FCoolerDisplayFishInsideLiner::RunTest(const FString& Parameters)
{
	namespace LB = LureCoolerDisplayBoundsTest;
	LureCatchTest::FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	const FFishSpeciesRow* Bonefish = W.FishTables.Species ? W.FishTables.Species->FindRow<FFishSpeciesRow>(TEXT("Bonefish"), TEXT("bounds")) : nullptr;
	const FFishSpeciesRow* Snapper = W.FishTables.Species ? W.FishTables.Species->FindRow<FFishSpeciesRow>(TEXT("CoralSnapper"), TEXT("bounds")) : nullptr;
	if (!TestNotNull(TEXT("Bonefish row"), Bonefish) || !TestNotNull(TEXT("CoralSnapper row"), Snapper))
	{
		return false;
	}
	auto Weights = [](const FFishSpeciesRow& Row)
	{
		// Tiny, reference, heaviest roll, and past it (the shown scale caps at MaxFishScale)
		return TArray<float>{ Row.WeightMin * 0.25f, Row.ReferenceWeight, Row.WeightMax, Row.WeightMax * 4.0f };
	};
	const TArray<float> BoneW = Weights(*Bonefish);
	const TArray<float> SnapW = Weights(*Snapper);

	// Piles of 1..4 of mixed species and sizes, the max fish in a different slot each time, plus an all-max pile of 4
	TArray<TArray<LB::FPileFish>> Piles;
	for (int32 Count = 1; Count <= 4; ++Count)
	{
		for (int32 MaxAt = 0; MaxAt < Count; ++MaxAt)
		{
			TArray<LB::FPileFish> Pile;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const bool bBone = (Index + MaxAt) % 2 == 0;
				const TArray<float>& W4 = bBone ? BoneW : SnapW;
				Pile.Add({ bBone ? FName(TEXT("Bonefish")) : FName(TEXT("CoralSnapper")), Index == MaxAt ? W4[3] : W4[(Index + Count) % 3] });
			}
			Piles.Add(Pile);
		}
	}
	Piles.Add({ { TEXT("Bonefish"), BoneW[3] }, { TEXT("CoralSnapper"), SnapW[3] }, { TEXT("Bonefish"), BoneW[3] }, { TEXT("CoralSnapper"), SnapW[3] } });

	TArray<ALureCoolerActor*> Coolers;
	for (int32 Case = 0; Case < Piles.Num(); ++Case)
	{
		const float Y = -LureCatchTest::DockHalf + 60.0f + Case * 100.0f;
		ALureCoolerActor* Cooler = W.SpawnCooler(FVector(0.0f, Y, LureCatchTest::DockTop), 0.0f);
		if (!TestNotNull(FString::Printf(TEXT("case %d: cooler"), Case), Cooler))
		{
			return false;
		}
		int32 Seed = 7300 + Case * 10;
		for (const LB::FPileFish& Fish : Piles[Case])
		{
			Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LureCatchTest::MakeFish(Fish.Species, 10, 1, Fish.WeightKg, Seed++), W.Now()));
		}
		Cooler->AuthoritySetLidOpen(true);
		Coolers.Add(Cooler);
	}
	W.Tick(30);

	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(W.World);
	ULureFightFishSubsystem* Visuals = ULureFightFishSubsystem::Get(W.World);
	const FFishVisualRow VisualRow = Visuals ? Visuals->GetVisualRow() : FFishVisualRow::GetFallbackRow();
	for (int32 Case = 0; Case < Piles.Num(); ++Case)
	{
		ALureCoolerActor* Cooler = Coolers[Case];
		const TArray<LB::FPileFish>& Pile = Piles[Case];
		const FLureCoolerDisplayRow Row = Subsystem ? Subsystem->GetCoolerDisplayRow(Cooler->GetCoolerId()) : FLureCoolerDisplayRow::GetFallbackRow();
		const int32 Shown = FMath::Min(Pile.Num(), Row.Slots.Num());
		if (!TestEqual(FString::Printf(TEXT("case %d: %d of %d fish show"), Case, Shown, Pile.Num()), Cooler->GetNumDisplayedFish(), Shown))
		{
			continue;
		}
		LB::RefreshShownPoses(Cooler);
		const TArray<FBox> Boxes = Cooler->GetDisplayedFishBounds();
		if (!TestEqual(FString::Printf(TEXT("case %d: one box per shown fish"), Case), Boxes.Num(), Shown))
		{
			continue;
		}
		const FTransform Contents = Cooler->GetContentsRoot()->GetComponentTransform();
		const FBox Allowed = LB::InnerBox.ExpandBy(LB::ToleranceCm);
		for (int32 Slot = 0; Slot < Shown; ++Slot)
		{
			const LB::FPileFish& Fish = Pile[Pile.Num() - Shown + Slot];
			const FBox Local = Boxes[Slot].InverseTransformBy(Contents);
			const float Reference = Subsystem ? Subsystem->GetSpeciesReferenceWeight(Fish.Species) : 0.0f;
			const float Scale = ALureCoolerActor::GetDisplayFishScale(Fish.WeightKg, Reference, VisualRow, Row);
			TestTrue(FString::Printf(TEXT("case %d slot %d (%s %.2f kg, scale ~%.3f): inside the liner +-%.1f cm (fish %s; allowed %s)"),
				Case, Slot, *Fish.Species.ToString(), Fish.WeightKg, Scale, LB::ToleranceCm, *LB::DescribeBox(Local), *LB::DescribeBox(Allowed)),
				Allowed.IsInsideOrOn(Local.Min) && Allowed.IsInsideOrOn(Local.Max));
			// Not vacuous: a real fish-sized box (a curled fish still spans well over 5 cm)
			TestTrue(FString::Printf(TEXT("case %d slot %d: the box is a real fish (%s)"), Case, Slot, *LB::DescribeBox(Local)),
				Local.IsValid && Local.GetSize().GetMax() > 5.0);
			AddInfo(FString::Printf(TEXT("case %d slot %d %s %.2f kg: %s"), Case, Slot, *Fish.Species.ToString(), Fish.WeightKg, *LB::DescribeBox(Local)));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

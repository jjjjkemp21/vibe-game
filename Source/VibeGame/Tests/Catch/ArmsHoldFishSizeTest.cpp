// Lure T-030c tests (unreal-engineer): UFPArmsAnimInstance::HoldFishSizeAlpha, the blend of the two HoldFish clips by the held
// fish's visual scale (SK_FPArms.anim.md "HoldFish"; DT_Catch HoldFishScaleSmall / HoldFishScaleLarge).
// Project.Arms.HoldFishSize.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/FPArmsAnimInstance.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"

namespace LureArmsHoldFishSizeTest
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmsHoldFishSizeAlphaMath, "Project.Arms.HoldFishSize.AlphaMath", LCT::Flags)
	bool FArmsHoldFishSizeAlphaMath::RunTest(const FString& Parameters)
	{
		auto Alpha = [](float S) { return UFPArmsAnimInstance::ComputeHoldFishSizeAlpha(S, 1.0f, 1.6f); };
		TestEqual(TEXT("S = 1.0 (the small clip's fish) gives 0"), Alpha(1.0f), 0.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("S = 1.6 (the trophy clip's fish) gives 1"), Alpha(1.6f), 1.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("S = 1.3 gives 0.5"), Alpha(1.3f), 0.5f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("a smaller fish clamps to 0"), Alpha(0.6f), 0.0f);
		TestEqual(TEXT("a bigger fish clamps to 1"), Alpha(3.0f), 1.0f);
		TestEqual(TEXT("an unknown scale (0) gives 0"), Alpha(0.0f), 0.0f);
		TestEqual(TEXT("a negative scale gives 0"), Alpha(-1.0f), 0.0f);
		TestEqual(TEXT("a NaN scale gives 0"), Alpha(NAN), 0.0f);
		TestEqual(TEXT("a bad range (large <= small) gives 0"), UFPArmsAnimInstance::ComputeHoldFishSizeAlpha(1.3f, 1.6f, 1.0f), 0.0f);
		TestEqual(TEXT("other reference scales: S = 1.5 in 1.0 .. 2.0 gives 0.5"), UFPArmsAnimInstance::ComputeHoldFishSizeAlpha(1.5f, 1.0f, 2.0f), 0.5f, KINDA_SMALL_NUMBER);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmsHoldFishSizeData, "Project.Arms.HoldFishSize.ShippedDataAndValidation", LCT::Flags)
	bool FArmsHoldFishSizeData::RunTest(const FString& Parameters)
	{
		const TStrongObjectPtr<UDataTable> Table = LCT::Shipped(*this, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
		const FLureCatchRow* Row = Table.IsValid() ? Table->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("test")) : nullptr;
		if (!TestNotNull(TEXT("DT_Catch Default row"), Row))
		{
			return false;
		}
		TestEqual(TEXT("HoldFishScaleSmall = the small clip's scale"), Row->HoldFishScaleSmall, 1.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("HoldFishScaleLarge = the trophy clip's scale"), Row->HoldFishScaleLarge, 1.6f, KINDA_SMALL_NUMBER);
		const FLureCatchRow Fallback = FLureCatchRow::GetFallbackRow();
		TestEqual(TEXT("the fallback matches (small)"), Fallback.HoldFishScaleSmall, Row->HoldFishScaleSmall);
		TestEqual(TEXT("the fallback matches (large)"), Fallback.HoldFishScaleLarge, Row->HoldFishScaleLarge);

		FLureCatchRow Bad;
		Bad.HoldFishScaleLarge = Bad.HoldFishScaleSmall;
		FString Problem;
		TestFalse(TEXT("large = small is rejected"), Bad.Validate(Problem));
		TestTrue(TEXT("... naming the column"), Problem.Contains(TEXT("HoldFishScaleLarge")));
		Bad = FLureCatchRow();
		Bad.HoldFishScaleSmall = 0.0f;
		TestFalse(TEXT("a zero small scale is rejected"), Bad.Validate(Problem));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmsHoldFishSizeInHand, "Project.Arms.HoldFishSize.FollowsTheHeldFish", LCT::Flags)
	bool FArmsHoldFishSizeInHand::RunTest(const FString& Parameters)
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
		W.Tick(10);
		UFPArmsAnimInstance* Anim = NewObject<UFPArmsAnimInstance>(Player->GetFirstPersonArms());

		// No owner pawn: 0.
		UFPArmsAnimInstance* Orphan = NewObject<UFPArmsAnimInstance>(NewObject<USkeletalMeshComponent>());
		Orphan->HoldFishSizeAlpha = 0.7f;
		Orphan->NativeUpdateAnimation(0.016f);
		TestEqual(TEXT("no owner: 0"), Orphan->HoldFishSizeAlpha, 0.0f);

		// Empty hands: 0.
		Anim->HoldFishSizeAlpha = 0.7f;
		Anim->NativeUpdateAnimation(0.016f);
		TestEqual(TEXT("no fish in hand: 0"), Anim->HoldFishSizeAlpha, 0.0f);

		// A heavy fish in hand: its visual scale drives the alpha (the same numbers as the math with DT_Catch).
		ALureFishItem* Fish = W.LandInHand(Player, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 40.0f, 7));
		if (!TestNotNull(TEXT("fish in hand"), Fish))
		{
			return false;
		}
		W.Tick(2);
		const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(Player);
		const float Scale = Fish->GetWeightScale();
		Anim->NativeUpdateAnimation(0.016f);
		TestEqual(TEXT("in hand: alpha from the fish's visual scale"), Anim->HoldFishSizeAlpha,
			UFPArmsAnimInstance::ComputeHoldFishSizeAlpha(Scale, Tuning.HoldFishScaleSmall, Tuning.HoldFishScaleLarge), KINDA_SMALL_NUMBER);
		if (Scale >= Tuning.HoldFishScaleLarge)
		{
			TestEqual(FString::Printf(TEXT("a trophy (scale %.2f) gives 1"), Scale), Anim->HoldFishSizeAlpha, 1.0f);
		}
		else
		{
			AddWarning(FString::Printf(TEXT("the 40 kg Bonefish drew at scale %.2f only (reference weight changed?)"), Scale));
		}
		return true;
	}
}

#endif

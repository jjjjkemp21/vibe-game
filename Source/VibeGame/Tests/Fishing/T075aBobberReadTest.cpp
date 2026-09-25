// Lure T-075a: the bobber and the bite read at 18 m (designer Sprint 1 must-fix 1).
// Cause: the bite held the whole bobber ~30 cm under clear (Thin Translucent, OpacityMin 0.12) water, so on screen it only
// slid a few px lower and stayed visible, then held still: a bite looked like idle. Now the bite ducks the bobber under and
// bobs it back up (the red dome shows again) BiteDipRate times a second (FLureFishingRules::BiteDipShare, DT_Fishing).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/Fishing/QAFishingTestUtils.h"

#include "Character/LurePlayerCharacter.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"

namespace LureT075aTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** SM_Bobber (art/recipes/sm_bobber.py), real cm above its waterline pivot: the red dome's top and the LineAttach top. */
	constexpr float DomeTopCm = 3.0f;
	constexpr float TopCm = 4.9f;

	/** Screen px per cm at Distance on a 1920 px wide view with a 90 deg horizontal FOV (the ART_STYLE readability reference). */
	float PxPerCm(float DistanceCm)
	{
		return 1920.f / (2.f * DistanceCm * FMath::Tan(FMath::DegreesToRadians(45.f)));
	}

	/** The designer's "visible range" of a bite: the dip (cm below rest) at which the red dome is fully under / still half shows. */
	float DomeUnderDip(const FLureFishingRow& Row) { return DomeTopCm * Row.BobberScale; }
	float DomeShowsDip(const FLureFishingRow& Row) { return 0.5f * DomeTopCm * Row.BobberScale; }

	/** The bite pose of the shipped data, from the CSV source: big enough, deep enough, and it comes back up every tug. */
	bool CheckShippedBiteData(FAutomationTestBase& Test, const FLureFishingRow& Row)
	{
		const float Px = PxPerCm(1800.f);
		const float DomePx = DomeTopCm * Row.BobberScale * Px;
		Test.TestTrue(FString::Printf(TEXT("the red dome at rest is >= 12 px tall at 18 m (1080p, 90 deg): %.1f px (BobberScale %.2f)"), DomePx, Row.BobberScale),
			DomePx >= 12.f);
		const float Deep = Row.BiteDipDepth;
		const float Shallow = Row.BiteDipMinShare * Row.BiteDipDepth;
		Test.TestTrue(FString::Printf(TEXT("a bite pulls >= 15 cm (%.1f)"), Deep), Deep >= 15.f);
		Test.TestTrue(FString::Printf(TEXT("each tug takes the whole bobber under, red included (B-S4: %.1f >= %.1f)"), Deep, TopCm * Row.BobberScale),
			Deep >= TopCm * Row.BobberScale);
		Test.TestTrue(FString::Printf(TEXT("between tugs the red dome shows again (dip %.1f <= %.1f)"), Shallow, DomeShowsDip(Row)), Shallow <= DomeShowsDip(Row));
		Test.TestTrue(FString::Printf(TEXT("the duck is >= 15 cm = %.1f px at 18 m (%.1f cm)"), 15.f * Px, Deep - Shallow), Deep - Shallow >= 15.f);
		Test.TestTrue(FString::Printf(TEXT(">= 2 ducks inside the hook window (%.2f/s x %.2f s)"), Row.BiteDipRate, Row.HookWindow), Row.BiteDipRate * Row.HookWindow >= 2.f);
		Test.TestTrue(FString::Printf(TEXT("the first pull is at once (BiteDipAttack %.2f s <= 0.1)"), Row.BiteDipAttack), Row.BiteDipAttack <= 0.1f);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureT075aCsvBiteReads, "Project.Fishing.T075a.CsvBiteReadsAt18m", LureT075aTests::Flags)
bool FLureT075aCsvBiteReads::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	if (!QAFishing::ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	LureT075aTests::CheckShippedBiteData(*this, Shipped);
	FString Problem;
	TestTrue(TEXT("the shipped row validates ") + Problem, Shipped.Validate(Problem));
	FLureFishingRow Bad = Shipped;
	Bad.BiteDipMinShare = 1.5f;
	TestFalse(TEXT("BiteDipMinShare > 1 is rejected"), Bad.Validate(Problem));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureT075aDipShareRule, "Project.Fishing.T075a.DipShareRule", LureT075aTests::Flags)
bool FLureT075aDipShareRule::RunTest(const FString& Parameters)
{
	FLureFishingRow Row;
	Row.BiteDipRate = 2.f;
	Row.BiteDipMinShare = 0.2f;
	TestNearlyEqual(TEXT("fully under at the bite"), FLureFishingRules::BiteDipShare(Row, 0.f), 1.f, 1.e-4f);
	TestNearlyEqual(TEXT("back up to MinShare half a tug later"), FLureFishingRules::BiteDipShare(Row, 0.25f), 0.2f, 1.e-4f);
	TestNearlyEqual(TEXT("under again a full tug later"), FLureFishingRules::BiteDipShare(Row, 0.5f), 1.f, 1.e-4f);
	TestNearlyEqual(TEXT("before the bite = at the bite"), FLureFishingRules::BiteDipShare(Row, -1.f), 1.f, 1.e-4f);
	Row.BiteDipRate = 0.f;
	TestNearlyEqual(TEXT("rate 0: held under"), FLureFishingRules::BiteDipShare(Row, 0.3f), 1.f, 1.e-4f);
	Row.BiteDipRate = 2.f;
	Row.BiteDipMinShare = 1.f;
	TestNearlyEqual(TEXT("MinShare 1: held under"), FLureFishingRules::BiteDipShare(Row, 0.25f), 1.f, 1.e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureT075aBitePoseDucksAndBobs, "Project.Fishing.T075a.BitePoseDucksAndBobs", LureT075aTests::Flags)
bool FLureT075aBitePoseDucksAndBobs::RunTest(const FString& Parameters)
{
	// The real component, the shipped bobber data: from the bite frame on, the bobber is >= 15 cm under at once, never deeper
	// than BiteDipDepth or above its rest, and the red dome goes under and comes back at least twice in the shipped hook window.
	FLureFishingRow Shipped;
	QAFishing::FScene Scene;
	if (!QAFishing::ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	FLureFishingRow Profile = QAFishing::StageProfile(Shipped); // a long hook window so the whole shipped window is sampled
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile);
	if (!Character || !Fishing)
	{
		return false;
	}
	Scene.PossessLocally(Character);
	Scene.Tick(10);
	if (!TestTrue(TEXT("cast"), Fishing->AuthorityCast(0.f, static_cast<float>(Character->GetActorRotation().Yaw)))
		|| !TestTrue(TEXT("landed"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 180))
		|| !TestTrue(TEXT("a bite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 600)))
	{
		return false;
	}

	const float RestZ = static_cast<float>(Fishing->GetNetState().BobberRest.Z);
	const double BiteTime = Fishing->GetNetState().StateStartTime;
	const float Under = LureT075aTests::DomeUnderDip(Profile);
	const float Shows = LureT075aTests::DomeShowsDip(Profile);
	float FirstDeepTime = -1.f;
	float MaxDip = -TNumericLimits<float>::Max();
	float MinDip = TNumericLimits<float>::Max();
	int32 Resurfaced = 0;
	bool bWasUnder = false;
	const int32 Frames = FMath::CeilToInt(Shipped.HookWindow / QAFishing::Dt);
	for (int32 Frame = 0; Frame <= Frames && Fishing->GetFishingState() == ELureFishingState::Biting; ++Frame)
	{
		const float Since = static_cast<float>(Fishing->GetFishingTime() - BiteTime);
		const float Dip = RestZ - static_cast<float>(Fishing->GetBobberLocation().Z);
		MaxDip = FMath::Max(MaxDip, Dip);
		MinDip = FMath::Min(MinDip, Dip);
		if (FirstDeepTime < 0.f && Dip >= 15.f)
		{
			FirstDeepTime = Since;
		}
		if (Dip >= Under)
		{
			bWasUnder = true;
		}
		else if (bWasUnder && Dip <= Shows)
		{
			bWasUnder = false;
			++Resurfaced;
		}
		Scene.Tick(1);
	}
	TestTrue(FString::Printf(TEXT(">= 15 cm under within 0.1 s of the bite (first at %.3f s)"), FirstDeepTime), FirstDeepTime >= 0.f && FirstDeepTime <= 0.1f);
	TestTrue(FString::Printf(TEXT("never deeper than BiteDipDepth %.1f (max %.1f)"), Profile.BiteDipDepth, MaxDip), MaxDip <= Profile.BiteDipDepth + 0.01f);
	TestTrue(FString::Printf(TEXT("never above the rest (min dip %.1f)"), MinDip), MinDip >= -0.01f);
	TestTrue(FString::Printf(TEXT("the red dome (under at %.1f cm, shows at <= %.1f cm) comes back up >= 2 times in the %.2f s hook window (%d)"),
		Under, Shows, Shipped.HookWindow, Resurfaced), Resurfaced >= 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure T-075b (designer must-fix 3, Sprint 1 build): during a fight the owner's rod stays on screen at hard steering.
// Project.Fishing.T075b.*. The first-person arms (and the rod in their hand) are children of the camera, so the fight
// camera's turn (CameraRodYawShare) never moves them on screen; what pushed the rod out of a 90 deg view is the arms' own
// side swing (the aim offset's Right pose holds the rod ~47 deg right). DT_FishFight CameraMaxRodYawDeg (35) caps the yaw
// the owner's arms get (FLureRodControl::CapViewAim); the fight, the HUD and the network keep the full aim.
// Tables come from the text sources in data/tables/. No file-scope using-directives (unity builds).

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/FPArmsAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Fishing/LureRodControl.h"
#include "GameFramework/PlayerController.h"

namespace LureT075bTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The rod's yaw off the view's center for the owner's arms at an aim (their side pose turns it RodAimSideDeg at yaw 1). */
	float ArmsRodYawDeg(const FVector2D& ArmsAim, const FLureFishFightRow& T)
	{
		return FLureRodControl::ViewRodYawDeg(static_cast<float>(ArmsAim.X), T.RodAimSideDeg);
	}

// =====================================================================================================================
// The pure cap
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT075bCap, "Project.Fishing.T075b.CapKeepsTheRodInView", Flags)
bool FT075bCap::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	TestTrue(FString::Printf(TEXT("the shipped rod swings further than the cap (RodAimSideDeg %.1f > CameraMaxRodYawDeg %.1f), so the cap matters"),
		T.RodAimSideDeg, T.CameraMaxRodYawDeg), T.RodAimSideDeg > T.CameraMaxRodYawDeg && T.CameraMaxRodYawDeg > 0.f);

	const float Below = T.CameraMaxRodYawDeg / T.RodAimSideDeg;
	for (int32 Step = -20; Step <= 20; ++Step)
	{
		const float Yaw = Step / 20.f;
		for (const float Pitch : { -1.f, 0.f, 0.6f })
		{
			const FVector2D Capped = FLureRodControl::CapViewAim(FVector2D(Yaw, Pitch), T.RodAimSideDeg, T);
			const float ViewDeg = FMath::Abs(ArmsRodYawDeg(Capped, T));
			TestTrue(FString::Printf(TEXT("yaw %.2f pitch %.1f: the rod is %.2f deg off center, within %.1f"), Yaw, Pitch, ViewDeg, T.CameraMaxRodYawDeg),
				ViewDeg <= T.CameraMaxRodYawDeg + 1.0e-3f);
			TestEqual(FString::Printf(TEXT("yaw %.2f: the pitch is never touched"), Yaw), static_cast<float>(Capped.Y), Pitch);
			if (FMath::Abs(Yaw) <= Below)
			{
				TestEqual(FString::Printf(TEXT("yaw %.2f (%.1f deg, under the cap): unchanged"), Yaw, Yaw * T.RodAimSideDeg), static_cast<float>(Capped.X), Yaw);
			}
			else
			{
				TestNearlyEqual(FString::Printf(TEXT("yaw %.2f: held at the cap on its own side"), Yaw), ArmsRodYawDeg(Capped, T),
					FMath::Sign(Yaw) * T.CameraMaxRodYawDeg, 1.0e-3f);
			}
		}
	}

	FLureFishFightRow Off = T;
	Off.CameraMaxRodYawDeg = 0.f;
	TestTrue(TEXT("CameraMaxRodYawDeg 0: no cap"), FLureRodControl::CapViewAim(FVector2D(1.f, 0.5f), Off.RodAimSideDeg, Off).Equals(FVector2D(1.f, 0.5f)));
	TestTrue(TEXT("a rod that turns less than the cap (the 25 deg placeholder turn) is never capped"),
		FLureRodControl::CapViewAim(FVector2D(-1.f, 0.f), 25.f, T).Equals(FVector2D(-1.f, 0.f)));
	FLureFishFightRow Bad = T;
	Bad.CameraMaxRodYawDeg = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("a NaN cap is no cap (never a NaN aim)"), FLureRodControl::CapViewAim(FVector2D(1.f, 0.f), T.RodAimSideDeg, Bad).Equals(FVector2D(1.f, 0.f)));
	return true;
}

// =====================================================================================================================
// In a fight: full left / right steering, the owner's arms stay within the cap; the fight and the camera are unchanged
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT075bFight, "Project.Fishing.T075b.HardSteeringKeepsTheRodOnScreen", Flags)
bool FT075bFight::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	FFishInstance Bonefish;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 131, Bonefish))
	{
		return false;
	}
	LureFightQA::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(LureFightQA::StandAt());
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("character"), Character) || !TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.Tick(10);
	ULureFishingComponent* Fishing = LureFightQA::SetUpFishing(Character, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
	if (!TestNotNull(TEXT("fishing"), Fishing) || !LureFightQA::CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
	{
		Controller->UnPossess();
		return false;
	}
	World.Tick(1);
	const int32 SendTicks = FMath::CeilToInt(GetDefault<ULureFishingSettings>()->FightInputSendSeconds / LureFightQA::WorldDt) + 2;
	const UFPArmsAnimInstance* Anim = Character->GetFirstPersonArms() ? Cast<UFPArmsAnimInstance>(Character->GetFirstPersonArms()->GetAnimInstance()) : nullptr;

	for (const float Side : { 1.f, -1.f })
	{
		const TCHAR* Name = Side > 0.f ? TEXT("full right") : TEXT("full left");
		Character->DoLook(Side * 4.f * T.RodAimSideDeg, 0.f);
		World.Tick(FMath::Max(60, SendTicks));
		TestNearlyEqual(FString::Printf(TEXT("%s: the rod's aim is the full aim (no gameplay change)"), Name), static_cast<float>(Fishing->GetRodAim().X), Side, 1.0e-4f);
		TestNearlyEqual(FString::Printf(TEXT("%s: the server fights with the full aim"), Name), Fishing->GetFightState().RodYaw, Side, 1.0e-4f);
		TestTrue(FString::Printf(TEXT("%s: the HUD names the full side"), Name), Fishing->GetStatusText().Contains(Side > 0.f ? TEXT("-right") : TEXT("-left")));

		const FVector2D Arms = Fishing->GetRodAimForAnimation();
		const float CameraYaw = static_cast<float>(Controller->GetControlRotation().Yaw);
		const float RodYaw = CameraYaw + ArmsRodYawDeg(Arms, T); // the arms ride the camera: the rod points this way in the world
		const float Offset = FMath::Abs(FRotator::NormalizeAxis(RodYaw - CameraYaw));
		TestTrue(FString::Printf(TEXT("%s: the camera's yaw offset from the rod/arms direction is %.2f deg, within CameraMaxRodYawDeg %.1f (was %.1f before T-075b)"),
			Name, Offset, T.CameraMaxRodYawDeg, T.RodAimSideDeg), Offset <= T.CameraMaxRodYawDeg + 0.05f);
		TestTrue(FString::Printf(TEXT("%s: ... and the arms still swing to that side, to the cap (%.2f deg)"), Name, Offset),
			Offset >= T.CameraMaxRodYawDeg - 0.5f && FMath::Sign(Arms.X) == Side);
		if (Anim)
		{
			TestNearlyEqual(FString::Printf(TEXT("%s: the arms' anim instance plays the capped aim"), Name), Anim->RodAimYaw, static_cast<float>(Arms.X), 0.02f);
		}

		// The camera is not changed by T-075b: it still heads at the fish turned by CameraRodYawShare of the FULL aim.
		const UCameraComponent* Camera = Character->GetFirstPersonCamera();
		const FVector Eye = Camera ? Camera->GetComponentLocation() : Character->GetPawnViewLocation();
		const FRotator Target = FLureRodControl::CameraTarget(Eye, Fishing->GetBobberLocation(), 0.f, Side, T);
		TestTrue(FString::Printf(TEXT("%s: the camera still follows the fish and the full rod aim (yaw %.1f vs %.1f)"), Name, CameraYaw, Target.Yaw),
			FMath::Abs(FRotator::NormalizeAxis(CameraYaw - Target.Yaw)) < 6.f);

		Character->DoLook(-Side * T.RodAimSideDeg, 0.f); // back to center (the rod stays where the mouse leaves it)
	}

	// Below the cap nothing changes: half right (22.5 deg on the shipped row) reaches the arms as it is.
	Character->DoLook(0.5f * T.RodAimSideDeg, 0.f);
	World.Tick(45);
	TestNearlyEqual(TEXT("half right: the rod aim"), static_cast<float>(Fishing->GetRodAim().X), 0.5f, 1.0e-4f);
	TestNearlyEqual(TEXT("half right (under the cap): the arms get it unchanged"), static_cast<float>(Fishing->GetRodAimForAnimation().X), 0.5f, 0.01f);

	Controller->UnPossess();
	return true;
}

// =====================================================================================================================
// The column
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT075bData, "Project.Fishing.T075b.Data.CameraMaxRodYawDeg", Flags)
bool FT075bData::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& Shipped = *Data.Tuning();
	const FLureFishFightRow Defaults;
	TestEqual(TEXT("the struct default is 35 deg (lead + designer)"), Defaults.CameraMaxRodYawDeg, 35.f);
	TestEqual(TEXT("the shipped Default row has the struct default"), Shipped.CameraMaxRodYawDeg, Defaults.CameraMaxRodYawDeg);
	FString Problem;
	TestTrue(TEXT("the shipped row validates: ") + Problem, Shipped.Validate(Problem));

	struct FCase
	{
		const TCHAR* What;
		float Value;
		bool bValid;
	};
	const FCase Cases[] = {
		{ TEXT("0 (no cap)"), 0.f, true },
		{ TEXT("90"), 90.f, true },
		{ TEXT("-1"), -1.f, false },
		{ TEXT("91"), 91.f, false },
		{ TEXT("NaN"), std::numeric_limits<float>::quiet_NaN(), false },
		{ TEXT("+Inf"), std::numeric_limits<float>::infinity(), false },
	};
	for (const FCase& Case : Cases)
	{
		FLureFishFightRow Row = Shipped;
		Row.CameraMaxRodYawDeg = Case.Value;
		FString Why;
		TestEqual(FString::Printf(TEXT("CameraMaxRodYawDeg %s valid: %d (%s)"), Case.What, Case.bValid ? 1 : 0, *Why), Row.Validate(Why), Case.bValid);
	}

	// The column is in DT_FishFight.csv, and optional: a CSV without it imports with the default.
	TArray<FString> Lines;
	Data.FightCsv.ParseIntoArrayLines(Lines, true);
	if (!TestTrue(TEXT("DT_FishFight.csv has a header and a row"), Lines.Num() >= 2))
	{
		return false;
	}
	TArray<FString> Header;
	TArray<FString> Values;
	Lines[0].ParseIntoArray(Header, TEXT(","), false);
	Lines[1].ParseIntoArray(Values, TEXT(","), false);
	TestTrue(TEXT("DT_FishFight.csv has the CameraMaxRodYawDeg column"), Header.ContainsByPredicate([](const FString& H) { return H.TrimStartAndEnd() == TEXT("CameraMaxRodYawDeg"); }));
	TArray<FString> KeptHeader;
	TArray<FString> KeptValues;
	for (int32 Index = 0; Index < Header.Num() && Index < Values.Num(); ++Index)
	{
		if (Header[Index].TrimStartAndEnd() != TEXT("CameraMaxRodYawDeg"))
		{
			KeptHeader.Add(Header[Index]);
			KeptValues.Add(Values[Index]);
		}
	}
	TStrongObjectPtr<UDataTable> Table;
	const TArray<FString> Problems = LureFightQA::MakeTable(Table, FLureFishFightRow::StaticStruct(),
		FString::Join(KeptHeader, TEXT(",")) + TEXT("\n") + FString::Join(KeptValues, TEXT(",")) + TEXT("\n"), false);
	TestEqual(TEXT("a CSV without the column imports: ") + FString::Join(Problems, TEXT(" | ")), Problems.Num(), 0);
	const FLureFishFightRow* Without = Table.IsValid() ? Table->FindRow<FLureFishFightRow>(TEXT("Default"), TEXT("T075bFightCameraTest"), false) : nullptr;
	TestTrue(TEXT("... and gets the default"), Without && Without->CameraMaxRodYawDeg == Defaults.CameraMaxRodYawDeg);
	return true;
}

} // namespace LureT075bTest

#endif // WITH_DEV_AUTOMATION_TESTS

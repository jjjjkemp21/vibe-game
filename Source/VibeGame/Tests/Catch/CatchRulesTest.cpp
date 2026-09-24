// Lure T-030 tests (unreal-engineer): pure catch rules (freshness curve and anchor, sale price with freshness, the hanging
// pendulum), the catch data (DT_Catch, DT_Freshness, DT_CoolerDisplay, the new DT_Cooler columns, settings, keys) and the
// replication setup (reflection). Project.Catch.Rules.*, Project.Catch.Data.*, Project.Catch.Net.ReplicationSetup

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCarryableItem.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "Game/LureGameMode.h"
#include "Game/LurePlayerState.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Interaction/LureInteractionComponent.h"
#include "Net/UnrealNetwork.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Progression/LureProgressionTypes.h"

namespace LureCatchRulesTest
{
	FLureFreshnessRow Row(float Grace, float Spoil, float Exponent, float Min)
	{
		FLureFreshnessRow Out;
		Out.GraceSeconds = Grace;
		Out.SpoilSeconds = Spoil;
		Out.CurveExponent = Exponent;
		Out.MinValueShare = Min;
		return Out;
	}

	bool AnyContains(const TArray<FString>& Problems, const TCHAR* Needle)
	{
		return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchRulesFreshnessCurve, "Project.Catch.Rules.FreshnessCurve", LCT::Flags)
	bool FCatchRulesFreshnessCurve::RunTest(const FString& Parameters)
	{
		const FLureFreshnessRow Linear = Row(100.0f, 200.0f, 1.0f, 0.3f);
		TestEqual(TEXT("fresh at 0 s"), FLureFreshness::GetFreshness01(Linear, 0.0f), 1.0f, 1e-6f);
		TestEqual(TEXT("still fresh at the end of the grace"), FLureFreshness::GetFreshness01(Linear, 100.0f), 1.0f, 1e-6f);
		TestEqual(TEXT("half way through the spoil time: 0.5"), FLureFreshness::GetFreshness01(Linear, 200.0f), 0.5f, 1e-6f);
		TestEqual(TEXT("spoiled at grace + spoil"), FLureFreshness::GetFreshness01(Linear, 300.0f), 0.0f, 1e-6f);
		TestEqual(TEXT("never below 0 later"), FLureFreshness::GetFreshness01(Linear, 1.0e6f), 0.0f, 1e-6f);
		TestEqual(TEXT("value share = min + (1 - min) x freshness (half way: 0.65)"), FLureFreshness::GetValueShare(Linear, 200.0f), 0.65f, 1e-6f);
		TestEqual(TEXT("value share floor = MinValueShare"), FLureFreshness::GetValueShare(Linear, 1.0e6f), 0.3f, 1e-6f);
		TestEqual(TEXT("value share 1 when fresh"), FLureFreshness::GetValueShare(Linear, 0.0f), 1.0f, 1e-6f);

		const FLureFreshnessRow Slow = Row(0.0f, 100.0f, 2.0f, 0.0f);
		TestEqual(TEXT("exponent 2: x = 0.5 -> 1 - 0.25"), FLureFreshness::GetFreshness01(Slow, 50.0f), 0.75f, 1e-6f);
		const FLureFreshnessRow Fast = Row(0.0f, 100.0f, 0.5f, 0.0f);
		TestEqual(TEXT("exponent 0.5: x = 0.25 -> 1 - 0.5"), FLureFreshness::GetFreshness01(Fast, 25.0f), 0.5f, 1e-6f);
		for (float Seconds = 0.0f; Seconds < 400.0f; Seconds += 7.0f)
		{
			if (FLureFreshness::GetFreshness01(Linear, Seconds + 7.0f) > FLureFreshness::GetFreshness01(Linear, Seconds) + 1e-6f)
			{
				AddError(FString::Printf(TEXT("freshness rose between %g and %g s"), Seconds, Seconds + 7.0f));
			}
		}

		TestEqual(TEXT("NaN exposure counts as fresh"), FLureFreshness::GetFreshness01(Linear, NAN), 1.0f, 1e-6f);
		TestEqual(TEXT("+inf exposure is spoiled"), FLureFreshness::GetFreshness01(Linear, INFINITY), 0.0f, 1e-6f);
		TestEqual(TEXT("negative exposure is fresh"), FLureFreshness::GetFreshness01(Linear, -50.0f), 1.0f, 1e-6f);
		const FLureFreshnessRow Broken = Row(NAN, 0.0f, -1.0f, 5.0f);
		const float Share = FLureFreshness::GetValueShare(Broken, 10.0f);
		TestTrue(FString::Printf(TEXT("a broken row still gives a share in [0, 1] (%g)"), Share), FMath::IsFinite(Share) && Share >= 0.0f && Share <= 1.0f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchRulesFreshnessAnchor, "Project.Catch.Rules.FreshnessAnchor", LCT::Flags)
	bool FCatchRulesFreshnessAnchor::RunTest(const FString& Parameters)
	{
		FLureFreshnessState State = FLureFreshnessState::StartAt(1000.0, 1.0f);
		TestEqual(TEXT("time zero at the landing"), State.GetExposure(1000.0), 0.0f, 1e-6f);
		TestEqual(TEXT("rate 1: 30 s later, 30 s of exposure"), State.GetExposure(1030.0), 30.0f, 1e-4f);
		TestEqual(TEXT("a time before the anchor counts as the anchor"), State.GetExposure(900.0), 0.0f, 1e-6f);

		State.SetRate(0.0f, 1030.0); // into a closed cooler
		TestEqual(TEXT("closed cooler: the exposure holds"), State.GetExposure(5000.0), 30.0f, 1e-4f);
		State.SetRate(1.0f, 5000.0); // out again
		TestEqual(TEXT("out again: it continues from 30 s"), State.GetExposure(5010.0), 40.0f, 1e-4f);
		State.SetRate(0.5f, 5010.0); // a cooler that slows it down
		TestEqual(TEXT("rate 0.5: 20 s -> 10 s more"), State.GetExposure(5030.0), 50.0f, 1e-4f);

		FLureFreshnessState Hostile = FLureFreshnessState::StartAt(0.0, -3.0f);
		TestEqual(TEXT("a negative rate counts as 0"), Hostile.GetExposure(100.0), 0.0f, 1e-6f);
		Hostile.SetRate(NAN, 10.0);
		TestEqual(TEXT("a NaN rate counts as 0"), Hostile.Rate, 0.0f, 1e-6f);
		Hostile.ExposedSeconds = NAN;
		TestEqual(TEXT("NaN exposure reads as 0"), Hostile.GetExposure(20.0), 0.0f, 1e-6f);

		const FLureCaughtFish Landed = FLureCaughtFish::Landed(LCT::MakeFish(TEXT("A"), 10, 1), 77.0);
		TestTrue(TEXT("a landed catch is valid"), Landed.IsValid());
		TestEqual(TEXT("... anchored at the landing"), Landed.Freshness.AnchorTime, 77.0, 1e-9);
		TestEqual(TEXT("... spoiling at rate 1"), Landed.Freshness.Rate, 1.0f, 1e-6f);
		TestEqual(TEXT("... fresh"), Landed.Freshness.ExposedSeconds, 0.0f, 1e-6f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchRulesSellPrice, "Project.Catch.Rules.SellPriceWithFreshness", LCT::Flags)
	bool FCatchRulesSellPrice::RunTest(const FString& Parameters)
	{
		const FFishInstance Fish = LCT::MakeFish(TEXT("A"), 45, 1);
		TestEqual(TEXT("share 1 = the T-010 price"), FLureFreshness::GetSellPrice(Fish, 1.0f, 1.5f), FLureProgressionRules::GetSellPrice(Fish, 1.5f));
		TestEqual(TEXT("45 x 0.5 = 22.5 -> 23 (round half up)"), FLureFreshness::GetSellPrice(Fish, 0.5f, 1.0f), 23);
		TestEqual(TEXT("45 x 0.3 x 1.5 = 20.25 -> 20"), FLureFreshness::GetSellPrice(Fish, 0.3f, 1.5f), 20);
		TestEqual(TEXT("current value = the price at a x1 buyer"), FLureFreshness::GetCurrentValue(Fish, 0.5f), 23);
		TestEqual(TEXT("a real fish always pays at least 1"), FLureFreshness::GetSellPrice(LCT::MakeFish(TEXT("B"), 1, 1), 0.01f, 1.0f), 1);
		TestEqual(TEXT("... even at share 0"), FLureFreshness::GetSellPrice(Fish, 0.0f, 1.0f), 1);
		TestEqual(TEXT("an invalid fish pays nothing"), FLureFreshness::GetSellPrice(FFishInstance(), 1.0f, 1.0f), 0);
		TestEqual(TEXT("a broken multiplier pays nothing"), FLureFreshness::GetSellPrice(Fish, 1.0f, 0.0f), 0);
		TestEqual(TEXT("a share above 1 is clamped"), FLureFreshness::GetSellPrice(Fish, 3.0f, 1.0f), 45);
		TestEqual(TEXT("a NaN share counts as fresh"), FLureFreshness::GetSellPrice(Fish, NAN, 1.0f), 45);
		int32 Previous = 0;
		for (int32 Step = 0; Step <= 20; ++Step)
		{
			const int32 Price = FLureFreshness::GetSellPrice(Fish, Step / 20.0f, 1.0f);
			TestTrue(FString::Printf(TEXT("price never drops as the share rises (%d at %d/20)"), Price, Step), Price >= Previous);
			Previous = Price;
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchRulesFindFreshnessRow, "Project.Catch.Rules.FreshnessRowPerSpecies", LCT::Flags)
	bool FCatchRulesFindFreshnessRow::RunTest(const FString& Parameters)
	{
		const TStrongObjectPtr<UDataTable> Table = LCT::MakeTableChecked(*this, FLureFreshnessRow::StaticStruct(),
			TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\n")
			TEXT("Default,120,600,1.0,0.3,\n")
			TEXT("Bonefish,30,60,1.0,0.5,\"spoils fast\"\n"), false, TEXT("freshness fixture"));
		bool bFallback = true;
		FLureFreshnessRow Found = FLureFreshness::FindRow(Table.Get(), TEXT("Bonefish"), TEXT("Default"), &bFallback);
		TestTrue(TEXT("the species row wins"), !bFallback && FMath::IsNearlyEqual(Found.GraceSeconds, 30.0f));
		Found = FLureFreshness::FindRow(Table.Get(), TEXT("CoralSnapper"), TEXT("Default"), &bFallback);
		TestTrue(TEXT("other species use Default"), !bFallback && FMath::IsNearlyEqual(Found.GraceSeconds, 120.0f));
		Found = FLureFreshness::FindRow(nullptr, TEXT("Bonefish"), TEXT("Default"), &bFallback);
		TestTrue(TEXT("no table: the built-in row"), bFallback && FMath::IsNearlyEqual(Found.SpoilSeconds, FLureFreshnessRow::GetFallbackRow().SpoilSeconds));
		const TStrongObjectPtr<UDataTable> BadDefault = LCT::MakeTable(FLureFreshnessRow::StaticStruct(),
			TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\nDefault,120,0,1.0,0.3,\n"), false);
		FLureFreshness::FindRow(BadDefault.Get(), TEXT("X"), TEXT("Default"), &bFallback);
		TestTrue(TEXT("an invalid default row: the built-in row"), bFallback);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchRulesPendulum, "Project.Catch.Rules.PendulumHangsAndSwings", LCT::Flags)
	bool FCatchRulesPendulum::RunTest(const FString& Parameters)
	{
		const float Length = 40.0f;
		const float Gravity = -980.0f;
		FLureHangPendulum Pendulum;
		const FVector Pivot(100.0f, 200.0f, 300.0f);
		Pendulum.Step(Pivot, Length, Gravity, 1.2f, 1.0f / 60.0f);
		TestTrue(TEXT("the first step hangs it straight down"), Pendulum.bInitialized && Pendulum.Bob.Equals(Pivot - FVector(0.0f, 0.0f, Length), 0.01));
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Pendulum.Step(Pivot, Length, Gravity, 1.2f, 1.0f / 60.0f);
		}
		TestTrue(FString::Printf(TEXT("at rest it stays straight below (%s)"), *Pendulum.Bob.ToCompactString()), Pendulum.Bob.Equals(Pivot - FVector(0.0f, 0.0f, Length), 0.05));

		// The player walks 30 cm to the side: the fish lags, swings, and settles again below the new pivot.
		const FVector Moved = Pivot + FVector(30.0f, 0.0f, 0.0f);
		Pendulum.Step(Moved, Length, Gravity, 1.2f, 1.0f / 60.0f);
		float MaxAngle = 0.0f;
		float MaxLengthError = 0.0f;
		for (int32 Step = 0; Step < 60 * 8; ++Step)
		{
			Pendulum.Step(Moved, Length, Gravity, 1.2f, 1.0f / 60.0f);
			const FVector Line = Pendulum.Bob - Moved;
			MaxLengthError = FMath::Max(MaxLengthError, static_cast<float>(FMath::Abs(Line.Size() - Length)));
			MaxAngle = FMath::Max(MaxAngle, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(-Line.GetSafeNormal().Z, -1.0, 1.0)))));
		}
		TestTrue(FString::Printf(TEXT("the move made it swing (%.1f deg)"), MaxAngle), MaxAngle > 10.0f);
		TestTrue(FString::Printf(TEXT("the line keeps its length (worst %.3f cm)"), MaxLengthError), MaxLengthError < 0.01f);
		const FVector Settled = Pendulum.Bob - Moved;
		const float Rest = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(-Settled.GetSafeNormal().Z, -1.0, 1.0))));
		TestTrue(FString::Printf(TEXT("the swing dies down (%.2f deg after 8 s)"), Rest), Rest < 2.0f);

		// A start point off the line's reach (an adopted landed fish, T-029 seam) is taken without a kick.
		FLureHangPendulum Adopted;
		Adopted.Bob = Pivot + FVector(100.0f, 0.0f, -20.0f);
		Adopted.bInitialized = true;
		Adopted.Step(Pivot, Length, Gravity, 1.2f, 1.0f / 60.0f);
		TestTrue(FString::Printf(TEXT("an off-reach start moves onto the line, toward where it was (%s)"), *Adopted.Bob.ToCompactString()),
			FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Adopted.Bob, Pivot)), Length, 0.01f) && Adopted.Bob.X > Pivot.X + 0.8f * Length);
		TestTrue(FString::Printf(TEXT("... without flying off (%.0f cm/s)"), Adopted.Velocity.Size()), Adopted.Velocity.Size() < 150.0f);

		// Damping 0 keeps swinging; a big jump (teleport) resets it.
		FLureHangPendulum Free;
		Free.Reset(Pivot, Length);
		Free.Velocity = FVector(100.0f, 0.0f, 0.0f);
		float LateMaxSpeed = 0.0f;
		for (int32 Step = 0; Step < 600; ++Step)
		{
			Free.Step(Pivot, Length, Gravity, 0.0f, 1.0f / 60.0f);
			LateMaxSpeed = Step >= 480 ? FMath::Max(LateMaxSpeed, static_cast<float>(Free.Velocity.Size())) : LateMaxSpeed;
		}
		// The position-based step itself loses a little (about 0.2/s on the speed at 60 Hz); DT_Catch HangDamping (1.2/s) dominates.
		TestTrue(FString::Printf(TEXT("no damping: still swinging after 10 s (top speed %.0f of 100 cm/s over the last 2 s)"), LateMaxSpeed), LateMaxSpeed > 10.0f);
		Free.Step(Pivot + FVector(5000.0f, 0.0f, 0.0f), Length, Gravity, 0.0f, 1.0f / 60.0f);
		TestTrue(TEXT("a teleport resets it below the new pivot"), Free.Bob.Equals(Pivot + FVector(5000.0f, 0.0f, -Length), 0.01) && Free.Velocity.IsNearlyZero());
		Free.Step(FVector(NAN, 0.0f, 0.0f), Length, Gravity, 0.0f, 1.0f / 60.0f);
		TestFalse(TEXT("a NaN pivot never spreads NaN"), Free.Bob.ContainsNaN() || Free.Velocity.ContainsNaN());
		Free.Step(Pivot, Length, Gravity, 0.0f, 5.0f); // a huge hitch
		TestFalse(TEXT("a long frame stays finite"), Free.Bob.ContainsNaN());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchDataShippedTablesValid, "Project.Catch.Data.ShippedTablesValid", LCT::Flags)
	bool FCatchDataShippedTablesValid::RunTest(const FString& Parameters)
	{
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		const TStrongObjectPtr<UDataTable> Catch = LCT::Shipped(*this, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
		const TStrongObjectPtr<UDataTable> Freshness = LCT::Shipped(*this, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness.csv"));
		const TStrongObjectPtr<UDataTable> Display = LCT::Shipped(*this, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
		const TStrongObjectPtr<UDataTable> Coolers = LCT::Shipped(*this, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
		if (!Catch.IsValid() || !Freshness.IsValid() || !Display.IsValid() || !Coolers.IsValid())
		{
			return false;
		}
		for (const TCHAR* File : { TEXT("DT_Catch.csv"), TEXT("DT_Freshness.csv"), TEXT("DT_Cooler.csv") })
		{
			FString Csv;
			LCT::ReadSource(*this, File, Csv);
			UScriptStruct* Struct = FString(File) == TEXT("DT_Catch.csv") ? FLureCatchRow::StaticStruct()
				: (FString(File) == TEXT("DT_Freshness.csv") ? FLureFreshnessRow::StaticStruct() : FCoolerRow::StaticStruct());
			const TArray<FString> Problems = FLureProgressionData::ValidateCsvSource(Csv, Struct, File);
			TestEqual(FString::Printf(TEXT("%s source problems: %s"), File, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		}
		TArray<FString> Problems = FLureCatchData::ValidateCatchTable(Catch.Get(), Settings->CatchRow);
		TestEqual(FString::Printf(TEXT("DT_Catch: %s"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		Problems = FLureCatchData::ValidateFreshnessTable(Freshness.Get(), Settings->DefaultFreshnessRow);
		TestEqual(FString::Printf(TEXT("DT_Freshness: %s"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		Problems = FLureCatchData::ValidateCoolerDisplayTable(Display.Get(), Coolers.Get());
		TestEqual(FString::Printf(TEXT("DT_CoolerDisplay: %s"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		Problems = FLureProgressionData::ValidateCoolerTable(Coolers.Get(), GetDefault<ULureProgressionSettings>()->DefaultCoolerId);
		TestEqual(FString::Printf(TEXT("DT_Cooler: %s"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);

		// Jimmy: a starter cooler holds 4 fish; closed = fresh, open = spoils like outside; carrying slows you a little.
		const FCoolerRow* Starter = Coolers->FindRow<FCoolerRow>(GetDefault<ULureProgressionSettings>()->DefaultCoolerId, TEXT("CatchTest"), false);
		if (TestNotNull(TEXT("the default cooler row exists"), Starter))
		{
			TestEqual(TEXT("the starter cooler holds 4 fish"), Starter->Slots, 4);
			TestEqual(TEXT("closed: the fish stay fresh"), Starter->ClosedDecayRate, 0.0f, 1e-6f);
			TestEqual(TEXT("open: they spoil like outside"), Starter->OpenDecayRate, 1.0f, 1e-6f);
			TestTrue(TEXT("carrying it slows you (0 < multiplier < 1)"), Starter->CarrySpeedMultiplier > 0.0f && Starter->CarrySpeedMultiplier < 1.0f);
			TestFalse(TEXT("it names a body mesh"), Starter->BodyMesh.IsNull());
			TestFalse(TEXT("it names a lid mesh"), Starter->LidMesh.IsNull());
		}
		TestEqual(TEXT("the default cooler row is Starter"), GetDefault<ULureProgressionSettings>()->DefaultCoolerId, FName(TEXT("Starter")));

		// The built-in rows (missing tables) equal the shipped Default rows.
		const FLureCatchRow* CatchRow = Catch->FindRow<FLureCatchRow>(Settings->CatchRow, TEXT("CatchTest"), false);
		const FLureCatchRow Builtin = FLureCatchRow::GetFallbackRow();
		if (TestNotNull(TEXT("DT_Catch Default row"), CatchRow))
		{
			for (TFieldIterator<FFloatProperty> It(FLureCatchRow::StaticStruct()); It; ++It)
			{
				TestEqual(FString::Printf(TEXT("built-in %s = DT_Catch.csv"), *It->GetName()), It->GetPropertyValue_InContainer(&Builtin), It->GetPropertyValue_InContainer(CatchRow), 1e-5f);
			}
		}
		const FLureFreshnessRow* FreshRow = Freshness->FindRow<FLureFreshnessRow>(Settings->DefaultFreshnessRow, TEXT("CatchTest"), false);
		const FLureFreshnessRow BuiltinFresh = FLureFreshnessRow::GetFallbackRow();
		if (TestNotNull(TEXT("DT_Freshness Default row"), FreshRow))
		{
			for (TFieldIterator<FFloatProperty> It(FLureFreshnessRow::StaticStruct()); It; ++It)
			{
				TestEqual(FString::Printf(TEXT("built-in %s = DT_Freshness.csv"), *It->GetName()), It->GetPropertyValue_InContainer(&BuiltinFresh), It->GetPropertyValue_InContainer(FreshRow), 1e-5f);
			}
		}
		const FLureCoolerDisplayRow* StarterDisplay = Display->FindRow<FLureCoolerDisplayRow>(TEXT("Starter"), TEXT("CatchTest"), false);
		if (TestNotNull(TEXT("DT_CoolerDisplay dresses the Starter cooler"), StarterDisplay) && Starter)
		{
			TestEqual(TEXT("one display slot per starter fish"), StarterDisplay->Slots.Num(), Starter->Slots);
			TestEqual(TEXT("fish show at most at reference size (SK_Fish.anim.md: 4 fish at 1.0 fit under the lid)"), StarterDisplay->MaxFishScale, 1.0f);
			TestEqual(TEXT("the lie offset of the reference fish (4.24 / 4.26 cm)"), StarterDisplay->LieOffsetCm, 4.25f, 0.02f);
			if (StarterDisplay->Slots.Num() == 4)
			{
				TestTrue(TEXT("slot 2 is the table's (6.5, 1, 12.35), yaw 140, roll 90"), StarterDisplay->Slots[2].Location.Equals(FVector(6.5, 1.0, 12.35), 1.0e-3)
					&& StarterDisplay->Slots[2].Rotation.Equals(FRotator(0.0f, 140.0f, 90.0f), 1.0e-3f));
			}
			// A cooler type without a row gets the built-in layout: the shipped Starter row.
			const FLureCoolerDisplayRow BuiltinDisplay = FLureCoolerDisplayRow::GetFallbackRow();
			bool bSameSlots = BuiltinDisplay.Slots.Num() == StarterDisplay->Slots.Num();
			for (int32 Index = 0; bSameSlots && Index < BuiltinDisplay.Slots.Num(); ++Index)
			{
				bSameSlots = BuiltinDisplay.Slots[Index].Location.Equals(StarterDisplay->Slots[Index].Location, 1.0e-3)
					&& BuiltinDisplay.Slots[Index].Rotation.Equals(StarterDisplay->Slots[Index].Rotation, 1.0e-3f);
			}
			TestTrue(TEXT("the built-in display layout = the shipped Starter slots"), bSameSlots);
			TestTrue(TEXT("... the same pose, size cap and lie offset"), BuiltinDisplay.FishPose.ToSoftObjectPath() == StarterDisplay->FishPose.ToSoftObjectPath()
				&& BuiltinDisplay.MaxFishScale == StarterDisplay->MaxFishScale && FMath::IsNearlyEqual(BuiltinDisplay.LieOffsetCm, StarterDisplay->LieOffsetCm));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchDataValidatorsCatchBadRows, "Project.Catch.Data.ValidatorsCatchBadRows", LCT::Flags)
	bool FCatchDataValidatorsCatchBadRows::RunTest(const FString& Parameters)
	{
		auto Freshness = [](const FString& Line)
		{
			const TStrongObjectPtr<UDataTable> Table = LCT::MakeTable(FLureFreshnessRow::StaticStruct(),
				TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\n") + Line + TEXT("\n"), false);
			return FLureCatchData::ValidateFreshnessTable(Table.Get(), TEXT("Default"));
		};
		TestEqual(TEXT("a sane freshness row"), Freshness(TEXT("Default,10,20,1,0.5,")).Num(), 0);
		TestTrue(TEXT("negative grace"), AnyContains(Freshness(TEXT("Default,-1,20,1,0.5,")), TEXT("GraceSeconds")));
		TestTrue(TEXT("zero spoil time"), AnyContains(Freshness(TEXT("Default,10,0,1,0.5,")), TEXT("SpoilSeconds")));
		TestTrue(TEXT("zero exponent"), AnyContains(Freshness(TEXT("Default,10,20,0,0.5,")), TEXT("CurveExponent")));
		TestTrue(TEXT("min share above 1"), AnyContains(Freshness(TEXT("Default,10,20,1,1.5,")), TEXT("MinValueShare")));
		TestTrue(TEXT("no Default row"), AnyContains(Freshness(TEXT("Other,10,20,1,0.5,")), TEXT("Default")));

		auto Catch = [](const FString& Line)
		{
			const TStrongObjectPtr<UDataTable> Table = LCT::MakeTable(FLureCatchRow::StaticStruct(),
				TEXT("Name,HangLineLength,HangDamping,ReachDistance,FocusAngleDeg,DropForward,DropArcTime,PutDownDistance,PutDownMaxFall,LidOpenPitch,LidOpenTime,DevComment\n") + Line + TEXT("\n"), false);
			return FLureCatchData::ValidateCatchTable(Table.Get(), TEXT("Default"));
		};
		TestEqual(TEXT("a sane catch row"), Catch(TEXT("Default,40,1.2,250,20,60,0.35,80,300,100,0.25,")).Num(), 0);
		TestTrue(TEXT("a zero hang line"), AnyContains(Catch(TEXT("Default,0,1.2,250,20,60,0.35,80,300,100,0.25,")), TEXT("HangLineLength")));
		TestTrue(TEXT("a zero reach"), AnyContains(Catch(TEXT("Default,40,1.2,0,20,60,0.35,80,300,100,0.25,")), TEXT("ReachDistance")));
		TestTrue(TEXT("a focus angle past 90"), AnyContains(Catch(TEXT("Default,40,1.2,250,120,60,0.35,80,300,100,0.25,")), TEXT("FocusAngleDeg")));
		TestTrue(TEXT("a lid past 180"), AnyContains(Catch(TEXT("Default,40,1.2,250,20,60,0.35,80,300,200,0.25,")), TEXT("LidOpenPitch")));

		auto Cooler = [](const FString& Line)
		{
			const TStrongObjectPtr<UDataTable> Table = LCT::MakeTable(FCoolerRow::StaticStruct(),
				TEXT("Name,DisplayName,Slots,BodyMesh,LidMesh,OpenDecayRate,ClosedDecayRate,CarrySpeedMultiplier,DevComment\n") + Line + TEXT("\n"), false);
			return FLureProgressionData::ValidateCoolerTable(Table.Get());
		};
		TestEqual(TEXT("a sane cooler row"), Cooler(TEXT("A,\"A\",4,,,1.0,0.0,0.8,")).Num(), 0);
		TestTrue(TEXT("a negative closed rate"), AnyContains(Cooler(TEXT("A,\"A\",4,,,1.0,-0.1,0.8,")), TEXT("ClosedDecayRate")));
		TestTrue(TEXT("an absurd open rate"), AnyContains(Cooler(TEXT("A,\"A\",4,,,11,0.0,0.8,")), TEXT("OpenDecayRate")));
		TestTrue(TEXT("a zero carry multiplier"), AnyContains(Cooler(TEXT("A,\"A\",4,,,1.0,0.0,0,")), TEXT("CarrySpeedMultiplier")));
		TestTrue(TEXT("a carry multiplier past 2"), AnyContains(Cooler(TEXT("A,\"A\",4,,,1.0,0.0,2.5,")), TEXT("CarrySpeedMultiplier")));

		auto Display = [](const FString& Json, const UDataTable* Coolers)
		{
			const TStrongObjectPtr<UDataTable> Table = LCT::MakeTable(FLureCoolerDisplayRow::StaticStruct(), Json, true);
			return FLureCatchData::ValidateCoolerDisplayTable(Table.Get(), Coolers);
		};
		const TStrongObjectPtr<UDataTable> Coolers = LCT::MakeTableChecked(*this, FCoolerRow::StaticStruct(),
			TEXT("Name,DisplayName,Slots,DevComment\nStarter,\"S\",4,\n"), false, TEXT("coolers"));
		TestEqual(TEXT("a sane display row"), Display(TEXT("[{\"Name\":\"Starter\",\"Slots\":[],\"MaxFishScale\":0.7}]"), Coolers.Get()).Num(), 0);
		TestTrue(TEXT("a zero fish scale"), AnyContains(Display(TEXT("[{\"Name\":\"Starter\",\"Slots\":[],\"MaxFishScale\":0}]"), Coolers.Get()), TEXT("MaxFishScale")));
		TestTrue(TEXT("a display row for a cooler type that doesn't exist"), AnyContains(Display(TEXT("[{\"Name\":\"Startr\",\"Slots\":[],\"MaxFishScale\":1}]"), Coolers.Get()), TEXT("Startr")));
		TestTrue(TEXT("a slot 10 m away"), AnyContains(Display(TEXT("[{\"Name\":\"Starter\",\"Slots\":[{\"Location\":{\"X\":1000,\"Y\":0,\"Z\":0},\"Rotation\":{\"Pitch\":0,\"Yaw\":0,\"Roll\":0}}],\"MaxFishScale\":1}]"), Coolers.Get()), TEXT("slot 0")));
		TestTrue(TEXT("a negative lie offset"), AnyContains(Display(TEXT("[{\"Name\":\"Starter\",\"Slots\":[],\"MaxFishScale\":1,\"LieOffsetCm\":-1}]"), Coolers.Get()), TEXT("LieOffsetCm")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchDataSettingsAndKeys, "Project.Catch.Data.SettingsAndKeys", LCT::Flags)
	bool FCatchDataSettingsAndKeys::RunTest(const FString& Parameters)
	{
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		TestEqual(TEXT("DT_Catch path"), Settings->CatchTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_Catch.DT_Catch")));
		TestEqual(TEXT("DT_Freshness path"), Settings->FreshnessTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_Freshness.DT_Freshness")));
		TestEqual(TEXT("DT_CoolerDisplay path"), Settings->CoolerDisplayTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_CoolerDisplay.DT_CoolerDisplay")));
		TestTrue(TEXT("the starter cooler is on"), Settings->bSpawnStarterCooler);
		TestFalse(TEXT("a cooler spawn tag is set"), Settings->CoolerSpawnTag.IsNone());
		TestTrue(TEXT("the fish mesh search has the SK_ and SM_ conventions"), Settings->FishMeshPaths.Num() >= 2 && Settings->FishMeshPaths[0].Contains(TEXT("{Species}")));
		TestFalse(TEXT("a held-fish socket is set"), Settings->HeldFishSocket.IsNone());

		// The two use keys: Interact (E, gamepad X) and AltInteract (F, gamepad Y), both buttons.
		const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
		const UInputAction* Interact = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Interact);
		const UInputAction* Alt = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::AltInteract);
		if (!TestNotNull(TEXT("mapping context"), Context) || !TestNotNull(TEXT("Interact"), Interact) || !TestNotNull(TEXT("AltInteract"), Alt))
		{
			return false;
		}
		TestEqual(TEXT("AltInteract is a button"), static_cast<int32>(Alt->ValueType), static_cast<int32>(EInputActionValueType::Boolean));
		auto IsMapped = [Context](const UInputAction* Action, const FKey& Key)
		{
			return Context->GetMappings().ContainsByPredicate([&](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action && Mapping.Key == Key; });
		};
		TestTrue(TEXT("E interacts"), IsMapped(Interact, EKeys::E));
		TestTrue(TEXT("gamepad X (face left) interacts"), IsMapped(Interact, EKeys::Gamepad_FaceButton_Left));
		TestTrue(TEXT("F is Alt Interact"), IsMapped(Alt, EKeys::F));
		TestTrue(TEXT("gamepad Y (face top) is Alt Interact"), IsMapped(Alt, EKeys::Gamepad_FaceButton_Top));
		TestFalse(TEXT("the fishing button never drops a fish (LMB is not a use key)"), IsMapped(Alt, EKeys::LeftMouseButton) || IsMapped(Interact, EKeys::LeftMouseButton));
		TestTrue(TEXT("GetInputActionNames lists both"), ULureInputSubsystem::GetInputActionNames().Contains(FLureInputActionNames::Interact)
			&& ULureInputSubsystem::GetInputActionNames().Contains(FLureInputActionNames::AltInteract));

		// The character binds both keys (Started) when a local player possesses it.
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(0.0f, 0.0f, LCT::DockTop));
		W.Tick(3);
		const UEnhancedInputComponent* Input = Player ? Cast<UEnhancedInputComponent>(Player->InputComponent) : nullptr;
		if (!TestNotNull(TEXT("enhanced input after possession"), Input))
		{
			return false;
		}
		auto IsBound = [Input](const UInputAction* Action)
		{
			return Input->GetActionEventBindings().ContainsByPredicate([Action](const TUniquePtr<FEnhancedInputActionEventBinding>& Binding)
			{
				return Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == ETriggerEvent::Started;
			});
		};
		TestTrue(TEXT("Interact is bound"), IsBound(Interact));
		TestTrue(TEXT("AltInteract is bound"), IsBound(Alt));
		TestNotNull(TEXT("the character has hands"), Player->GetHands());
		return true;
	}

	/** Checks Property of Class replicates with Condition (RepIndex needs the class's replication data, as the net driver builds it) */
	void ExpectReplicated(FAutomationTestBase& Test, UClass* Class, FName Property, ELifetimeCondition Condition)
	{
		const FProperty* Found = Class->FindPropertyByName(Property);
		if (!Test.TestNotNull(FString::Printf(TEXT("%s.%s exists"), *Class->GetName(), *Property.ToString()), Found))
		{
			return;
		}
		Test.TestTrue(FString::Printf(TEXT("%s.%s is replicated"), *Class->GetName(), *Property.ToString()), Found->HasAnyPropertyFlags(CPF_Net));
		Class->SetUpRuntimeReplicationData();
		TArray<FLifetimeProperty> Lifetime;
		Class->GetDefaultObject()->GetLifetimeReplicatedProps(Lifetime);
		const FLifetimeProperty* Entry = Lifetime.FindByPredicate([Found](const FLifetimeProperty& Candidate) { return Candidate.RepIndex == Found->RepIndex; });
		if (Test.TestNotNull(FString::Printf(TEXT("%s.%s is registered"), *Class->GetName(), *Property.ToString()), Entry))
		{
			Test.TestEqual(FString::Printf(TEXT("%s.%s condition"), *Class->GetName(), *Property.ToString()), static_cast<int32>(Entry->Condition), static_cast<int32>(Condition));
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchNetReplicationSetup, "Project.Catch.Net.ReplicationSetup", LCT::Flags)
	bool FCatchNetReplicationSetup::RunTest(const FString& Parameters)
	{
		// Items: the hold and the placement are the truth; the engine's movement replication is off (every machine places them).
		ExpectReplicated(*this, ALureCarryableItem::StaticClass(), TEXT("Hold"), COND_None);
		ExpectReplicated(*this, ALureCarryableItem::StaticClass(), TEXT("Placement"), COND_None);
		ExpectReplicated(*this, ALureFishItem::StaticClass(), TEXT("Catch"), COND_None);
		ExpectReplicated(*this, ALureFishItem::StaticClass(), TEXT("Counter"), COND_None);
		ExpectReplicated(*this, ALureCoolerActor::StaticClass(), TEXT("bLidOpen"), COND_None);
		ExpectReplicated(*this, ALureCoolerActor::StaticClass(), TEXT("LidPulseId"), COND_None);
		ExpectReplicated(*this, ALureCoolerActor::StaticClass(), TEXT("OwningPlayerState"), COND_None);
		ExpectReplicated(*this, ALureCoolerActor::StaticClass(), TEXT("CoolerGuid"), COND_None);
		ExpectReplicated(*this, ALureCoolerActor::StaticClass(), TEXT("bStarter"), COND_None);
		// The storage is a shared world object now: its records go to everyone (T-010 sent them to the owner only).
		ExpectReplicated(*this, ULureCoolerComponent::StaticClass(), TEXT("StoredFish"), COND_None);
		ExpectReplicated(*this, ULureCoolerComponent::StaticClass(), TEXT("CoolerId"), COND_None);
		ExpectReplicated(*this, ULureCoolerComponent::StaticClass(), TEXT("Capacity"), COND_None);
		ExpectReplicated(*this, ULureCoolerComponent::StaticClass(), TEXT("DecayRate"), COND_None);
		for (const TCHAR* Setting : { TEXT("MarketId"), TEXT("CounterHalfSize"), TEXT("InteractionRadius"), TEXT("FishSpacing") })
		{
			ExpectReplicated(*this, ALureSellCounter::StaticClass(), Setting, COND_InitialOnly);
		}
		ExpectReplicated(*this, ULureProgressionComponent::StaticClass(), TEXT("Money"), COND_None);
		ExpectReplicated(*this, ULureProgressionComponent::StaticClass(), TEXT("TotalXp"), COND_None);
		ExpectReplicated(*this, ULureProgressionComponent::StaticClass(), TEXT("Level"), COND_None);

		const ALureFishItem* Fish = GetDefault<ALureFishItem>();
		const ALureCoolerActor* Cooler = GetDefault<ALureCoolerActor>();
		TestTrue(TEXT("items replicate, without movement replication"), Fish->GetIsReplicated() && !Fish->IsReplicatingMovement() && Cooler->GetIsReplicated() && !Cooler->IsReplicatingMovement());
		TestTrue(TEXT("coolers are always relevant (a player's catch waits in them)"), Cooler->bAlwaysRelevant);
		TestTrue(TEXT("the counter replicates"), GetDefault<ALureSellCounter>()->GetIsReplicated());
		TestTrue(TEXT("the storage, the hands (notice RPC) and the use keys (server RPC) replicate"), GetDefault<ULureCoolerComponent>()->GetIsReplicated()
			&& GetDefault<ULureHandsComponent>()->GetIsReplicated() && GetDefault<ULureInteractionComponent>()->GetIsReplicated());
		TestTrue(TEXT("ALureGameMode uses ALurePlayerState (progression only)"), GetDefault<ALureGameMode>()->PlayerStateClass == ALurePlayerState::StaticClass()
			&& GetDefault<ALurePlayerState>()->GetProgression() != nullptr);

		const UFunction* ServerInteract = ULureInteractionComponent::StaticClass()->FindFunctionByName(TEXT("ServerInteract"));
		TestTrue(TEXT("ServerInteract is a reliable server RPC"), ServerInteract && ServerInteract->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
		const UFunction* Notice = ULureHandsComponent::StaticClass()->FindFunctionByName(TEXT("ClientNotice"));
		TestTrue(TEXT("ClientNotice is a reliable client RPC"), Notice && Notice->HasAllFunctionFlags(FUNC_Net | FUNC_NetClient | FUNC_NetReliable));
		for (const TCHAR* Name : { TEXT("AddFish"), TEXT("RemoveFish"), TEXT("Clear"), TEXT("SetCoolerId") })
		{
			const UFunction* Function = ULureCoolerComponent::StaticClass()->FindFunctionByName(Name);
			TestTrue(FString::Printf(TEXT("cooler storage %s is BlueprintAuthorityOnly"), Name), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

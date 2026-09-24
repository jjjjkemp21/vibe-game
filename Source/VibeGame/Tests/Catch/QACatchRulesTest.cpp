// Lure T-030 independent QA (qa-engineer): the freshness rules and the catch data, black-box from
// docs/specs/catch-handling-rules.md ("Freshness", "Data") and the public headers. Pure: no world.
// Tests: Project.Catch.QA.Freshness.{CurveMatchesOracle, PriceRoundsHalfUpWithFloor, AnchorLongSessionAndBadRates,
// RowLookupFallbacks}; Project.Catch.QA.Data.{ShippedTablesRestated, FreshnessValidatorBoundaries,
// CatchRowValidatorBoundaries, DisplayValidatorBoundaries}. Oracles: Tests/Catch/QACatchTestUtils.h.

#include "Tests/Catch/QACatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchTypes.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"
#include "Progression/LureProgressionSettings.h"
#include "Progression/LureProgressionTypes.h"

namespace LureCatchQARules
{
	FLureFreshnessRow Fresh(float Grace, float Spoil, float Exponent, float MinShare)
	{
		FLureFreshnessRow Row;
		Row.GraceSeconds = Grace;
		Row.SpoilSeconds = Spoil;
		Row.CurveExponent = Exponent;
		Row.MinValueShare = MinShare;
		return Row;
	}

	template <typename RowType>
	bool Valid(const RowType& Row)
	{
		FString Problem;
		return Row.Validate(Problem);
	}

	FString Where(const FLureFreshnessRow& Row, double Exposure)
	{
		return FString::Printf(TEXT("row (grace %g, spoil %g, exp %g, min %g) at %g s"), Row.GraceSeconds, Row.SpoilSeconds, Row.CurveExponent, Row.MinValueShare, Exposure);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAFreshnessCurve, "Project.Catch.QA.Freshness.CurveMatchesOracle", LCT::Flags)
bool FCatchQAFreshnessCurve::RunTest(const FString& Parameters)
{
	using FRow = FLureFreshnessRow;
	const FRow Rows[] = {
		LureCatchQARules::Fresh(120.0f, 600.0f, 1.0f, 0.3f), // shipped Default
		LureCatchQARules::Fresh(0.0f, 10.0f, 2.0f, 0.0f),
		LureCatchQARules::Fresh(5.0f, 10.0f, 0.5f, 1.0f),
		LureCatchQARules::Fresh(1.0f, 0.1f, 3.0f, 0.5f),
		LureCatchQARules::Fresh(30.0f, 90.0f, 1.7f, 0.15f) };
	for (const FRow& Row : Rows)
	{
		const double G = Row.GraceSeconds;
		const double S = Row.SpoilSeconds;
		const double Samples[] = { -5.0, 0.0, G - 0.01, G, G + 0.01, G + S * 0.25, G + S * 0.5, G + S * 0.999, G + S, G + S + 1.0, 1.0e6 };
		float Previous = 1.0f;
		for (const double X : Samples)
		{
			const float Freshness = FLureFreshness::GetFreshness01(Row, static_cast<float>(X));
			const float Share = FLureFreshness::GetValueShare(Row, static_cast<float>(X));
			TestEqual(FString::Printf(TEXT("freshness, %s"), *LureCatchQARules::Where(Row, X)), Freshness,
				static_cast<float>(LureCatchQA::OracleFreshness(G, S, Row.CurveExponent, static_cast<float>(X))), 1.0e-4f);
			TestEqual(FString::Printf(TEXT("value share, %s"), *LureCatchQARules::Where(Row, X)), Share,
				static_cast<float>(LureCatchQA::OracleShare(G, S, Row.CurveExponent, Row.MinValueShare, static_cast<float>(X))), 1.0e-4f);
			TestTrue(FString::Printf(TEXT("never gets fresher with more exposure, %s"), *LureCatchQARules::Where(Row, X)), X < 0.0 || Freshness <= Previous + 1.0e-6f);
			Previous = X < 0.0 ? Previous : Freshness;
		}
		TestEqual(FString::Printf(TEXT("the end of the grace is still 100%%, %s"), *LureCatchQARules::Where(Row, G)), FLureFreshness::GetFreshness01(Row, static_cast<float>(G)), 1.0f);
		TestEqual(FString::Printf(TEXT("fully spoiled sells for MinValueShare, %s"), *LureCatchQARules::Where(Row, G + S)), FLureFreshness::GetValueShare(Row, static_cast<float>(G + S + 1.0)), Row.MinValueShare, 1.0e-6f);
	}
	TestEqual(TEXT("NaN exposure counts as fresh (header: NaN-safe, NaN = fresh)"), FLureFreshness::GetFreshness01(Rows[0], NAN), 1.0f);
	TestEqual(TEXT("... and keeps the full value"), FLureFreshness::GetValueShare(Rows[0], NAN), 1.0f);
	TestEqual(TEXT("infinite exposure is fully spoiled"), FLureFreshness::GetFreshness01(Rows[0], INFINITY), 0.0f);
	TestEqual(TEXT("... and sells for the floor share"), FLureFreshness::GetValueShare(Rows[0], INFINITY), 0.3f, 1.0e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAFreshnessPrice, "Project.Catch.QA.Freshness.PriceRoundsHalfUpWithFloor", LCT::Flags)
bool FCatchQAFreshnessPrice::RunTest(const FString& Parameters)
{
	auto Fish = [](int32 Value) { return LCT::MakeFish(TEXT("Bonefish"), Value, 1); };
	// Exact .5 products (representable in float): round half UP.
	TestEqual(TEXT("5 x 0.5 = 2.5 -> 3"), FLureFreshness::GetCurrentValue(Fish(5), 0.5f), 3);
	TestEqual(TEXT("3 x 0.5 = 1.5 -> 2"), FLureFreshness::GetCurrentValue(Fish(3), 0.5f), 2);
	TestEqual(TEXT("4 x 0.375 = 1.5 -> 2"), FLureFreshness::GetCurrentValue(Fish(4), 0.375f), 2);
	TestEqual(TEXT("1 x 0.3 -> 0 -> at least 1 coin"), FLureFreshness::GetCurrentValue(Fish(1), 0.3f), 1);
	TestEqual(TEXT("a Value 0 fish is worth 0 (header)"), FLureFreshness::GetCurrentValue(Fish(0), 1.0f), 0);
	TestEqual(TEXT("a negative Value is worth 0 (header)"), FLureFreshness::GetCurrentValue(Fish(-5), 1.0f), 0);
	TestEqual(TEXT("an invalid fish is worth 0 (header)"), FLureFreshness::GetCurrentValue(FFishInstance(), 1.0f), 0);
	TestEqual(TEXT("sale: 7 x 0.5 x 1.5 = 5.25 -> 5"), FLureFreshness::GetSellPrice(Fish(7), 0.5f, 1.5f), 5);
	TestEqual(TEXT("sale: 10 x 0.25 x 1 = 2.5 -> 3"), FLureFreshness::GetSellPrice(Fish(10), 0.25f, 1.0f), 3);
	TestEqual(TEXT("sale: 3 x 0.5 x 1.5 = 2.25 -> 2"), FLureFreshness::GetSellPrice(Fish(3), 0.5f, 1.5f), 2);
	TestEqual(TEXT("sale: 1 x 0.3 x 1 -> at least 1 coin"), FLureFreshness::GetSellPrice(Fish(1), 0.3f, 1.0f), 1);
	TestEqual(TEXT("sale: a huge catch saturates instead of overflowing"), FLureFreshness::GetSellPrice(Fish(2000000000), 1.0f, 1.5f), MAX_int32);

	// Sweep (products away from a .5 boundary, so float and double agree), and the T-010 formula at full freshness.
	const int32 Values[] = { 1, 2, 7, 30, 45, 99, 250, 1234 };
	const float Shares[] = { 0.3f, 0.3125f, 0.5f, 0.6f, 0.875f, 1.0f };
	const float Multipliers[] = { 1.0f, 1.5f, 0.8f };
	int32 Checked = 0;
	for (const int32 Value : Values)
	{
		TestEqual(FString::Printf(TEXT("fresh (share 1) = the T-010 price, Value %d"), Value),
			FLureFreshness::GetSellPrice(Fish(Value), 1.0f, 1.5f), FLureProgressionRules::GetSellPrice(Fish(Value), 1.5f));
		for (const float Share : Shares)
		{
			for (const float Multiplier : Multipliers)
			{
				const double Product = static_cast<double>(Value) * Share * Multiplier;
				if (FMath::Abs(FMath::Frac(Product) - 0.5) < 1.0e-3)
				{
					continue;
				}
				++Checked;
				TestEqual(FString::Printf(TEXT("sale price %d x %g x %g"), Value, Share, Multiplier), FLureFreshness::GetSellPrice(Fish(Value), Share, Multiplier),
					LureCatchQA::OraclePrice(Value, Share, Multiplier));
				if (Multiplier == 1.0f)
				{
					TestEqual(FString::Printf(TEXT("current value %d x %g"), Value, Share), FLureFreshness::GetCurrentValue(Fish(Value), Share), LureCatchQA::OraclePrice(Value, Share, 1.0));
				}
			}
		}
	}
	TestTrue(TEXT("QA precondition: the sweep checked prices"), Checked > 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAFreshnessAnchor, "Project.Catch.QA.Freshness.AnchorLongSessionAndBadRates", LCT::Flags)
bool FCatchQAFreshnessAnchor::RunTest(const FString& Parameters)
{
	const double T0 = 3.0 * 86400.0; // a server that has run for three days
	const FLureFreshnessState Start = FLureFreshnessState::StartAt(T0);
	TestEqual(TEXT("landed: no exposure yet"), Start.GetExposure(T0), 0.0f);
	TestEqual(TEXT("a time before the anchor counts as the anchor"), Start.GetExposure(T0 - 100.0), 0.0f);
	TestEqual(TEXT("50,000 s later on a 3-day clock: exact to 0.01 s"), Start.GetExposure(T0 + 50000.25), 50000.25f, 0.01f);
	TestEqual(TEXT("a slower rate from the landing"), FLureFreshnessState::StartAt(T0, 0.25f).GetExposure(T0 + 8.0), 2.0f, 1.0e-4f);

	// 1000 lid cycles of 0.1 s open (rate 1) + 0.1 s closed (rate 0): 100 s of exposure, no drift.
	FLureFreshnessState Cycled = FLureFreshnessState::StartAt(T0, 1.0f);
	double Now = T0;
	for (int32 Cycle = 0; Cycle < 1000; ++Cycle)
	{
		Now += 0.1;
		Cycled.SetRate(0.0f, Now);
		Now += 0.1;
		Cycled.SetRate(1.0f, Now);
	}
	TestEqual(TEXT("1000 open/close cycles add up (0.05 s)"), Cycled.GetExposure(Now), 100.0f, 0.05f);

	// A fish already exposed for 40,000 s keeps a 0.1 s step resolution.
	FLureFreshnessState Old;
	Old.ExposedSeconds = 40000.0f;
	Old.AnchorTime = T0;
	Old.Rate = 1.0f;
	double Later = T0;
	for (int32 Step = 0; Step < 100; ++Step)
	{
		Later += 0.1;
		Old.SetRate(1.0f, Later);
	}
	TestEqual(TEXT("tens of thousands of seconds: 100 re-anchors of 0.1 s add 10 s (0.25 s)"), Old.GetExposure(Later), 40010.0f, 0.25f);

	FLureFreshnessState Bad = FLureFreshnessState::StartAt(T0);
	Bad.SetRate(-3.0f, T0 + 10.0);
	TestEqual(TEXT("a negative rate is 0 (header)"), Bad.Rate, 0.0f);
	TestEqual(TEXT("... the exposure holds at the sample"), Bad.GetExposure(T0 + 100.0), 10.0f, 1.0e-3f);
	Bad.SetRate(1.0f, T0 + 100.0);
	Bad.SetRate(NAN, T0 + 110.0);
	TestEqual(TEXT("a NaN rate is 0 (header)"), Bad.Rate, 0.0f);
	TestEqual(TEXT("... after 10 more exposed seconds"), Bad.GetExposure(T0 + 500.0), 20.0f, 1.0e-3f);
	const float NanNow = Bad.GetExposure(NAN);
	TestTrue(FString::Printf(TEXT("a NaN time is safe (finite, not below the anchor): %f"), NanNow), FMath::IsFinite(NanNow) && NanNow >= 20.0f - 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQAFreshnessRows, "Project.Catch.QA.Freshness.RowLookupFallbacks", LCT::Flags)
bool FCatchQAFreshnessRows::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("DT_Freshness"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	const TStrongObjectPtr<UDataTable> Table = LCT::MakeTable(FLureFreshnessRow::StaticStruct(),
		TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\nDefault,10,20,1.0,0.5,\nBonefish,3,6,2.0,0.2,\nCoralSnapper,5,0,1.0,0.3,bad: spoil 0\n"), false);
	const TStrongObjectPtr<UDataTable> BadDefault = LCT::MakeTable(FLureFreshnessRow::StaticStruct(),
		TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\nDefault,-1,20,1.0,0.5,bad: negative grace\n"), false);
	if (!TestTrue(TEXT("QA fixtures import"), Table.IsValid() && BadDefault.IsValid() && Table->GetRowMap().Num() == 3))
	{
		return false;
	}
	bool bFallback = true;
	FLureFreshnessRow Row = FLureFreshness::FindRow(Table.Get(), TEXT("Bonefish"), TEXT("Default"), &bFallback);
	TestTrue(TEXT("the species row wins over Default"), Row.GraceSeconds == 3.0f && Row.CurveExponent == 2.0f && !bFallback);
	Row = FLureFreshness::FindRow(Table.Get(), TEXT("CoralSnapper"), TEXT("Default"), &bFallback);
	TestTrue(TEXT("an invalid species row counts as missing: Default"), Row.GraceSeconds == 10.0f && Row.SpoilSeconds == 20.0f && !bFallback);
	Row = FLureFreshness::FindRow(Table.Get(), TEXT("Nobody"), TEXT("Default"), &bFallback);
	TestTrue(TEXT("a species without a row: Default"), Row.GraceSeconds == 10.0f && !bFallback);

	const FLureFreshnessRow BuiltIn = FLureFreshnessRow::GetFallbackRow();
	TestTrue(TEXT("the built-in row = the spec's Default (120 s, 600 s, 1.0, 0.3)"), BuiltIn.GraceSeconds == 120.0f && BuiltIn.SpoilSeconds == 600.0f
		&& BuiltIn.CurveExponent == 1.0f && FMath::IsNearlyEqual(BuiltIn.MinValueShare, 0.3f) && LureCatchQARules::Valid(BuiltIn));
	auto IsBuiltIn = [&BuiltIn](const FLureFreshnessRow& R)
	{
		return R.GraceSeconds == BuiltIn.GraceSeconds && R.SpoilSeconds == BuiltIn.SpoilSeconds && R.CurveExponent == BuiltIn.CurveExponent && R.MinValueShare == BuiltIn.MinValueShare;
	};
	Row = FLureFreshness::FindRow(Table.Get(), TEXT("Nobody"), TEXT("Missing"), &bFallback);
	TestTrue(TEXT("no species row and no default row: built-in (flagged)"), IsBuiltIn(Row) && bFallback);
	Row = FLureFreshness::FindRow(BadDefault.Get(), TEXT("Bonefish"), TEXT("Default"), &bFallback);
	TestTrue(TEXT("an invalid Default row counts as missing: built-in (flagged)"), IsBuiltIn(Row) && bFallback);
	Row = FLureFreshness::FindRow(nullptr, TEXT("Bonefish"), TEXT("Default"), &bFallback);
	TestTrue(TEXT("no table: built-in (flagged)"), IsBuiltIn(Row) && bFallback);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQADataShipped, "Project.Catch.QA.Data.ShippedTablesRestated", LCT::Flags)
bool FCatchQADataShipped::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Coolers = LCT::Shipped(*this, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
	const TStrongObjectPtr<UDataTable> Freshness = LCT::Shipped(*this, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness.csv"));
	const TStrongObjectPtr<UDataTable> Catch = LCT::Shipped(*this, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
	const TStrongObjectPtr<UDataTable> Display = LCT::Shipped(*this, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
	const TStrongObjectPtr<UDataTable> Species = LCT::Shipped(*this, FFishSpeciesRow::StaticStruct(), TEXT("DT_FishSpecies.json"));
	if (!Coolers.IsValid() || !Freshness.IsValid() || !Catch.IsValid() || !Display.IsValid() || !Species.IsValid())
	{
		return false;
	}

	// DT_Cooler: every row usable, closed never spoils faster than open, carrying slows, the meshes exist.
	const FName DefaultCooler = GetDefault<ULureProgressionSettings>()->DefaultCoolerId;
	TestNotNull(TEXT("DT_Cooler has the settings' DefaultCoolerId row"), Coolers->FindRow<FCoolerRow>(DefaultCooler, TEXT("QA"), false));
	for (const TPair<FName, uint8*>& Pair : Coolers->GetRowMap())
	{
		const FCoolerRow& Row = *reinterpret_cast<const FCoolerRow*>(Pair.Value);
		const FString Name = Pair.Key.ToString();
		TestTrue(Name + TEXT(": Slots in [1, MaxCoolerSlots]"), Row.Slots >= 1 && Row.Slots <= FLureProgressionData::MaxCoolerSlots);
		TestTrue(Name + TEXT(": decay rates finite, >= 0"), FMath::IsFinite(Row.OpenDecayRate) && FMath::IsFinite(Row.ClosedDecayRate) && Row.OpenDecayRate >= 0.0f && Row.ClosedDecayRate >= 0.0f);
		TestTrue(Name + TEXT(": a closed lid never spoils faster than an open one"), Row.ClosedDecayRate <= Row.OpenDecayRate);
		TestTrue(Name + TEXT(": carrying slows you (0 < multiplier <= 1)"), Row.CarrySpeedMultiplier > 0.0f && Row.CarrySpeedMultiplier <= 1.0f);
		TestFalse(Name + TEXT(": has a display name"), Row.DisplayName.IsEmpty());
		for (const TSoftObjectPtr<UStaticMesh>* Mesh : { &Row.BodyMesh, &Row.LidMesh })
		{
			const FString Package = Mesh->ToSoftObjectPath().GetLongPackageName();
			TestTrue(FString::Printf(TEXT("%s: mesh %s exists"), *Name, *Package), !Package.IsEmpty() && FPackageName::DoesPackageExist(Package));
		}
	}
	const FCoolerRow* Starter = Coolers->FindRow<FCoolerRow>(TEXT("Starter"), TEXT("QA"), false);
	TestTrue(TEXT("Starter = 4 slots, closed 0 (holds), open 1"), Starter && Starter->Slots == 4 && Starter->ClosedDecayRate == 0.0f && Starter->OpenDecayRate == 1.0f);
	TestTrue(TEXT("the validator agrees"), FLureProgressionData::ValidateCoolerTable(Coolers.Get(), DefaultCooler).IsEmpty());

	// DT_Freshness: Default exists; every other row is named like a real species (a typo would never be used).
	TestNotNull(TEXT("DT_Freshness has Default"), Freshness->FindRow<FLureFreshnessRow>(TEXT("Default"), TEXT("QA"), false));
	for (const TPair<FName, uint8*>& Pair : Freshness->GetRowMap())
	{
		const FLureFreshnessRow& Row = *reinterpret_cast<const FLureFreshnessRow*>(Pair.Value);
		TestTrue(Pair.Key.ToString() + TEXT(": Default or a DT_FishSpecies row"), Pair.Key == TEXT("Default") || Species->GetRowMap().Contains(Pair.Key));
		TestTrue(Pair.Key.ToString() + TEXT(": grace >= 0, spoil > 0, exponent > 0, min share in [0, 1], all finite"), Row.GraceSeconds >= 0.0f && Row.SpoilSeconds > 0.0f
			&& Row.CurveExponent > 0.0f && Row.MinValueShare >= 0.0f && Row.MinValueShare <= 1.0f && FMath::IsFinite(Row.GraceSeconds + Row.SpoilSeconds + Row.CurveExponent + Row.MinValueShare));
	}
	TestTrue(TEXT("the freshness validator agrees"), FLureCatchData::ValidateFreshnessTable(Freshness.Get(), TEXT("Default")).IsEmpty());

	// DT_Catch: the Default row, and the numbers work together.
	const FLureCatchRow* Tuning = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("QA"), false);
	if (TestNotNull(TEXT("DT_Catch has Default"), Tuning))
	{
		TestTrue(TEXT("a cooler put down is in reach (PutDownDistance + half the box < ReachDistance)"), Tuning->PutDownDistance + 22.0f < Tuning->ReachDistance);
		TestTrue(TEXT("put-down clears the capsule and the box (>= 60 cm)"), Tuning->PutDownDistance >= 60.0f);
		TestTrue(TEXT("a dropped fish is in reach"), Tuning->DropForward < Tuning->ReachDistance);
		TestTrue(TEXT("a hanging fish is in reach"), Tuning->HangLineLength > 0.0f && Tuning->HangLineLength < Tuning->ReachDistance);
		TestTrue(TEXT("focus angle in [0.5, 90]"), Tuning->FocusAngleDeg >= 0.5f && Tuning->FocusAngleDeg <= 90.0f);
		TestTrue(TEXT("the lid opens (0, 180]"), Tuning->LidOpenPitch > 0.0f && Tuning->LidOpenPitch <= 180.0f);
		TestTrue(TEXT("the put-down floor search reaches past a step"), Tuning->PutDownMaxFall >= 50.0f);
	}
	TestTrue(TEXT("the catch validator agrees"), FLureCatchData::ValidateCatchTable(Catch.Get(), TEXT("Default")).IsEmpty());

	// DT_CoolerDisplay: dresses real coolers, no more slots than fish, slots inside the box, pitch 0, side down.
	for (const TPair<FName, uint8*>& Pair : Display->GetRowMap())
	{
		const FLureCoolerDisplayRow& Row = *reinterpret_cast<const FLureCoolerDisplayRow*>(Pair.Value);
		const FString Name = Pair.Key.ToString();
		const FCoolerRow* Cooler = Coolers->FindRow<FCoolerRow>(Pair.Key, TEXT("QA"), false);
		TestNotNull(Name + TEXT(": names a DT_Cooler row"), Cooler);
		TestTrue(Name + TEXT(": 1..Slots display slots"), Row.Slots.Num() >= 1 && (!Cooler || Row.Slots.Num() <= Cooler->Slots));
		TestTrue(Name + TEXT(": 0 < MaxFishScale <= 1, LieOffsetCm in [0, 50], PoseTime >= 0"), Row.MaxFishScale > 0.0f && Row.MaxFishScale <= 1.0f
			&& Row.LieOffsetCm >= 0.0f && Row.LieOffsetCm <= 50.0f && Row.PoseTime >= 0.0f);
		const FString Pose = Row.FishPose.ToSoftObjectPath().GetLongPackageName();
		TestTrue(Name + TEXT(": the pose is a fish clip (") + Pose + TEXT(")"), Pose.IsEmpty() || Pose.StartsWith(TEXT("/Game/Art/Fish/")));
		for (int32 Index = 0; Index < Row.Slots.Num(); ++Index)
		{
			const FLureCoolerDisplaySlot& Slot = Row.Slots[Index];
			TestTrue(FString::Printf(TEXT("%s slot %d: inside the 44 x 64 cm box, bed 0..30 cm"), *Name, Index), FMath::Abs(Slot.Location.X) < 22.0 && FMath::Abs(Slot.Location.Y) < 32.0
				&& Slot.Location.Z >= 0.0 && Slot.Location.Z + Row.LieOffsetCm * Row.MaxFishScale <= 30.0);
			TestTrue(FString::Printf(TEXT("%s slot %d: pitch 0, roll +-90 (a side down)"), *Name, Index), FMath::IsNearlyZero(Slot.Rotation.Pitch, 0.01)
				&& FMath::IsNearlyEqual(FMath::Abs(Slot.Rotation.Roll), 90.0, 0.01));
			TestTrue(FString::Printf(TEXT("%s slot %d: stacked bottom first"), *Name, Index), Index == 0 || Slot.Location.Z >= Row.Slots[Index - 1].Location.Z);
		}
	}
	TestTrue(TEXT("the display validator agrees"), FLureCatchData::ValidateCoolerDisplayTable(Display.Get(), Coolers.Get()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQADataFreshnessValidator, "Project.Catch.QA.Data.FreshnessValidatorBoundaries", LCT::Flags)
bool FCatchQADataFreshnessValidator::RunTest(const FString& Parameters)
{
	auto Check = [this](const TCHAR* What, const FLureFreshnessRow& Row, bool bExpected)
	{
		FString Problem;
		const bool bValid = Row.Validate(Problem);
		TestTrue(FString::Printf(TEXT("%s: %s"), What, bExpected ? TEXT("valid") : TEXT("refused")), bValid == bExpected);
		TestTrue(FString::Printf(TEXT("%s: a refusal says why"), What), bValid || !Problem.IsEmpty());
	};
	using LureCatchQARules::Fresh;
	Check(TEXT("shipped Default"), Fresh(120.0f, 600.0f, 1.0f, 0.3f), true);
	Check(TEXT("grace 0"), Fresh(0.0f, 600.0f, 1.0f, 0.3f), true);
	Check(TEXT("grace -0.01"), Fresh(-0.01f, 600.0f, 1.0f, 0.3f), false);
	Check(TEXT("spoil 0"), Fresh(120.0f, 0.0f, 1.0f, 0.3f), false);
	Check(TEXT("spoil -1"), Fresh(120.0f, -1.0f, 1.0f, 0.3f), false);
	Check(TEXT("spoil 0.1"), Fresh(120.0f, 0.1f, 1.0f, 0.3f), true);
	Check(TEXT("exponent 0"), Fresh(120.0f, 600.0f, 0.0f, 0.3f), false);
	Check(TEXT("exponent -1"), Fresh(120.0f, 600.0f, -1.0f, 0.3f), false);
	Check(TEXT("exponent 0.05"), Fresh(120.0f, 600.0f, 0.05f, 0.3f), true);
	Check(TEXT("min share 0"), Fresh(120.0f, 600.0f, 1.0f, 0.0f), true);
	Check(TEXT("min share 1"), Fresh(120.0f, 600.0f, 1.0f, 1.0f), true);
	Check(TEXT("min share -0.01"), Fresh(120.0f, 600.0f, 1.0f, -0.01f), false);
	Check(TEXT("min share 1.01"), Fresh(120.0f, 600.0f, 1.0f, 1.01f), false);
	Check(TEXT("NaN grace"), Fresh(NAN, 600.0f, 1.0f, 0.3f), false);
	Check(TEXT("NaN spoil"), Fresh(120.0f, NAN, 1.0f, 0.3f), false);
	Check(TEXT("NaN exponent"), Fresh(120.0f, 600.0f, NAN, 0.3f), false);
	Check(TEXT("NaN min share"), Fresh(120.0f, 600.0f, 1.0f, NAN), false);

	AddExpectedMessagePlain(TEXT("DT_Freshness"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	const TStrongObjectPtr<UDataTable> NoDefault = LCT::MakeTable(FLureFreshnessRow::StaticStruct(),
		TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\nBonefish,10,20,1.0,0.5,\n"), false);
	TestFalse(TEXT("table validator: the default row must exist"), FLureCatchData::ValidateFreshnessTable(NoDefault.Get(), TEXT("Default")).IsEmpty());
	TestFalse(TEXT("table validator: no table is a problem"), FLureCatchData::ValidateFreshnessTable(nullptr, TEXT("Default")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQADataCatchValidator, "Project.Catch.QA.Data.CatchRowValidatorBoundaries", LCT::Flags)
bool FCatchQADataCatchValidator::RunTest(const FString& Parameters)
{
	const FLureCatchRow Good = FLureCatchRow::GetFallbackRow();
	TestTrue(TEXT("the built-in row is valid"), LureCatchQARules::Valid(Good));
	struct FCase { const TCHAR* What; float FLureCatchRow::* Field; float Value; bool bValid; };
	const FCase Cases[] = {
		{ TEXT("HangLineLength 0"), &FLureCatchRow::HangLineLength, 0.0f, false },
		{ TEXT("HangLineLength NaN"), &FLureCatchRow::HangLineLength, NAN, false },
		{ TEXT("HangDamping 0 (swings on)"), &FLureCatchRow::HangDamping, 0.0f, true },
		{ TEXT("HangDamping -0.1"), &FLureCatchRow::HangDamping, -0.1f, false },
		{ TEXT("ReachDistance 0"), &FLureCatchRow::ReachDistance, 0.0f, false },
		{ TEXT("ReachDistance NaN"), &FLureCatchRow::ReachDistance, NAN, false },
		{ TEXT("FocusAngleDeg 0"), &FLureCatchRow::FocusAngleDeg, 0.0f, false },
		{ TEXT("FocusAngleDeg 90"), &FLureCatchRow::FocusAngleDeg, 90.0f, true },
		{ TEXT("FocusAngleDeg 91"), &FLureCatchRow::FocusAngleDeg, 91.0f, false },
		{ TEXT("DropForward 0"), &FLureCatchRow::DropForward, 0.0f, true },
		{ TEXT("DropForward -1"), &FLureCatchRow::DropForward, -1.0f, false },
		{ TEXT("DropArcTime -0.1"), &FLureCatchRow::DropArcTime, -0.1f, false },
		{ TEXT("PutDownDistance 0"), &FLureCatchRow::PutDownDistance, 0.0f, false },
		{ TEXT("PutDownDistance NaN"), &FLureCatchRow::PutDownDistance, NAN, false },
		{ TEXT("PutDownMaxFall 0"), &FLureCatchRow::PutDownMaxFall, 0.0f, false },
		{ TEXT("LidOpenPitch 180"), &FLureCatchRow::LidOpenPitch, 180.0f, true },
		{ TEXT("LidOpenPitch 181"), &FLureCatchRow::LidOpenPitch, 181.0f, false },
		{ TEXT("LidOpenPitch -1"), &FLureCatchRow::LidOpenPitch, -1.0f, false },
		{ TEXT("LidOpenTime -0.1"), &FLureCatchRow::LidOpenTime, -0.1f, false } };
	for (const FCase& Case : Cases)
	{
		FLureCatchRow Row = Good;
		Row.*Case.Field = Case.Value;
		FString Problem;
		const bool bValid = Row.Validate(Problem);
		TestTrue(FString::Printf(TEXT("%s: %s"), Case.What, Case.bValid ? TEXT("valid") : TEXT("refused")), bValid == Case.bValid);
		TestTrue(FString::Printf(TEXT("%s: a refusal says why"), Case.What), bValid || !Problem.IsEmpty());
	}
	AddExpectedMessagePlain(TEXT("DT_Catch"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	const TStrongObjectPtr<UDataTable> Other = LCT::MakeTable(FLureCatchRow::StaticStruct(),
		TEXT("Name,HangLineLength,HangDamping,ReachDistance,FocusAngleDeg,DropForward,DropArcTime,PutDownDistance,PutDownMaxFall,LidOpenPitch,LidOpenTime,DevComment\nFast,40,1.2,250,20,60,0.35,80,300,100,0.25,\n"), false);
	TestFalse(TEXT("table validator: the named row must exist"), FLureCatchData::ValidateCatchTable(Other.Get(), TEXT("Default")).IsEmpty());
	TestTrue(TEXT("table validator: a valid table with that row passes"), FLureCatchData::ValidateCatchTable(Other.Get(), TEXT("Fast")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchQADataDisplayValidator, "Project.Catch.QA.Data.DisplayValidatorBoundaries", LCT::Flags)
bool FCatchQADataDisplayValidator::RunTest(const FString& Parameters)
{
	FLureCoolerDisplayRow Good = FLureCoolerDisplayRow::GetFallbackRow();
	TestTrue(TEXT("the built-in layout is valid"), LureCatchQARules::Valid(Good));
	auto Check = [this, &Good](const TCHAR* What, TFunctionRef<void(FLureCoolerDisplayRow&)> Change, bool bExpected)
	{
		FLureCoolerDisplayRow Row = Good;
		Change(Row);
		FString Problem;
		const bool bValid = Row.Validate(Problem);
		TestTrue(FString::Printf(TEXT("%s: %s"), What, bExpected ? TEXT("valid") : TEXT("refused")), bValid == bExpected);
		TestTrue(FString::Printf(TEXT("%s: a refusal says why"), What), bValid || !Problem.IsEmpty());
	};
	Check(TEXT("LieOffsetCm 0"), [](FLureCoolerDisplayRow& R) { R.LieOffsetCm = 0.0f; }, true);
	Check(TEXT("LieOffsetCm 50"), [](FLureCoolerDisplayRow& R) { R.LieOffsetCm = 50.0f; }, true);
	Check(TEXT("LieOffsetCm -0.01"), [](FLureCoolerDisplayRow& R) { R.LieOffsetCm = -0.01f; }, false);
	Check(TEXT("LieOffsetCm 50.01"), [](FLureCoolerDisplayRow& R) { R.LieOffsetCm = 50.01f; }, false);
	Check(TEXT("LieOffsetCm NaN"), [](FLureCoolerDisplayRow& R) { R.LieOffsetCm = NAN; }, false);
	Check(TEXT("MaxFishScale 0"), [](FLureCoolerDisplayRow& R) { R.MaxFishScale = 0.0f; }, false);
	Check(TEXT("MaxFishScale NaN"), [](FLureCoolerDisplayRow& R) { R.MaxFishScale = NAN; }, false);
	Check(TEXT("PoseTime -1"), [](FLureCoolerDisplayRow& R) { R.PoseTime = -1.0f; }, false);

	const FString Slot = TEXT("{ \"Location\": { \"X\": 1.0, \"Y\": 0.0, \"Z\": 0.0 }, \"Rotation\": { \"Pitch\": 0.0, \"Yaw\": 90.0, \"Roll\": 90.0 } }");
	const FString Json = FString::Printf(TEXT("[ { \"Name\": \"Starter\", \"Slots\": [ %s ], \"MaxFishScale\": 1.0, \"LieOffsetCm\": 4.25 },")
		TEXT(" { \"Name\": \"Huge\", \"Slots\": [ %s ], \"MaxFishScale\": 1.0, \"LieOffsetCm\": 4.25 } ]"), *Slot, *Slot);
	const TStrongObjectPtr<UDataTable> Display = LCT::MakeTableChecked(*this, FLureCoolerDisplayRow::StaticStruct(), Json, true, TEXT("QA display fixture"));
	const TStrongObjectPtr<UDataTable> Coolers = LCT::Shipped(*this, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
	if (!Display.IsValid() || !Coolers.IsValid())
	{
		return false;
	}
	const TArray<FString> Problems = FLureCatchData::ValidateCoolerDisplayTable(Display.Get(), Coolers.Get());
	TestTrue(FString::Printf(TEXT("a display row for a cooler type that doesn't exist is named (%s)"), *FString::Join(Problems, TEXT("; "))),
		Problems.Num() == 1 && Problems[0].Contains(TEXT("Huge")));
	TestTrue(TEXT("without the cooler table only the rows are checked"), FLureCatchData::ValidateCoolerDisplayTable(Display.Get(), nullptr).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

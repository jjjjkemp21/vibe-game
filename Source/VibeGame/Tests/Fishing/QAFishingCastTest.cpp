// Lure T-006 QA (qa-engineer): cast charge, distance, flight and the fishing profile (DT_Fishing + fallback).
// Project.Fishing.QA.Cast.* - spec: docs/specs/fishing-rules.md "Cast"; contracts: Fishing/FishingTypes.h, LureFishingComponent.h.

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Misc/PackageName.h"
#include <limits>

// Everything lives in namespace QAFishing (unity builds merge test files; other files use global using-directives).
namespace QAFishing
{

namespace CastLocal
{

	/** The spec formula, restated: Min + (Max - Min) * clamp(charge)^Exponent. */
	double SpecDistance(const FLureFishingRow& Row, double Charge)
	{
		const double C = FMath::Clamp(Charge, 0.0, 1.0);
		return Row.MinCastDistance + (Row.MaxCastDistance - Row.MinCastDistance) * FMath::Pow(C, static_cast<double>(Row.ChargeExponent));
	}

	/** A fixture unlike the shipped row (catches hard-coded numbers). */
	FLureFishingRow FixtureRow(const FLureFishingRow& Shipped)
	{
		FLureFishingRow Row = Shipped;
		Row.ChargeTime = 2.5f;
		Row.ChargeExponent = 1.7f;
		Row.MinCastDistance = 450.f;
		Row.MaxCastDistance = 2200.f;
		Row.MaxLineLength = 3000.f;
		Row.CastSpeed = 900.f;
		Row.CastFlightTimeMin = 0.6f;
		Row.CastFlightTimeMax = 2.1f;
		return Row;
	}

	/** 2D distance from the eye to where the bobber rests, after AuthorityCast (standalone). */
	bool CastAndMeasure(FAutomationTestBase& Test, ALurePlayerCharacter* Character, ULureFishingComponent* Fishing, float Charge, float Yaw, float& OutDistance, FVector& OutRest)
	{
		const FVector Eye = Character->GetPawnViewLocation();
		if (!Fishing->AuthorityCast(Charge, Yaw))
		{
			Test.AddError(FString::Printf(TEXT("cast (charge %f) refused: %s"), Charge, *BlockName(Fishing->GetNetState().ResultReason)));
			return false;
		}
		OutRest = Fishing->GetNetState().BobberRest;
		OutDistance = static_cast<float>(FVector::Dist2D(Eye, OutRest));
		Fishing->AuthorityReelIn();
		return true;
	}
}

using namespace CastLocal;

// =====================================================================================================================
// Pure rules
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastChargeFromHoldTime, "Project.Fishing.QA.Cast.ChargeFromHoldTime", QAFishing::Flags)
bool FQAFishCastChargeFromHoldTime::RunTest(const FString& Parameters)
{
	// Spec: Charge = hold time / ChargeTime (clamped). ChargeTime comes from data.
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const FLureFishingRow& Row : { Shipped, FixtureRow(Shipped) })
	{
		const FString Label = FString::Printf(TEXT("ChargeTime %.2f"), Row.ChargeTime);
		TestEqual(Label + TEXT(": no hold = 0"), FLureFishingRules::ChargeFromHoldTime(Row, 0.f), 0.f);
		TestEqual(Label + TEXT(": negative hold = 0"), FLureFishingRules::ChargeFromHoldTime(Row, -3.f), 0.f);
		TestEqual(Label + TEXT(": NaN hold = 0"), FLureFishingRules::ChargeFromHoldTime(Row, NaN), 0.f);
		TestNearlyEqual(Label + TEXT(": a quarter of ChargeTime = 0.25"), FLureFishingRules::ChargeFromHoldTime(Row, 0.25f * Row.ChargeTime), 0.25f, 1.0e-5f);
		TestNearlyEqual(Label + TEXT(": exactly ChargeTime = full"), FLureFishingRules::ChargeFromHoldTime(Row, Row.ChargeTime), 1.f, 1.0e-6f);
		TestTrue(Label + TEXT(": 1 ms short of ChargeTime is not full"), FLureFishingRules::ChargeFromHoldTime(Row, Row.ChargeTime - 0.001f) < 1.f);
		TestEqual(Label + TEXT(": held 10x longer stays full"), FLureFishingRules::ChargeFromHoldTime(Row, 10.f * Row.ChargeTime), 1.f);
		const float Forever = FLureFishingRules::ChargeFromHoldTime(Row, Inf);
		TestTrue(FString::Printf(TEXT("%s: an infinite hold stays in [0, 1] (%f)"), *Label, Forever), FMath::IsFinite(Forever) && Forever >= 0.f && Forever <= 1.f);
		float Previous = -1.f;
		bool bMonotone = true;
		for (int32 Step = 0; Step <= 400; ++Step)
		{
			const float Charge = FLureFishingRules::ChargeFromHoldTime(Row, Step * 0.01f);
			bMonotone &= Charge >= Previous && Charge >= 0.f && Charge <= 1.f;
			Previous = Charge;
		}
		TestTrue(Label + TEXT(": holding longer never lowers the charge and it stays in [0, 1]"), bMonotone);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastDistanceEndpointsFromData, "Project.Fishing.QA.Cast.DistanceEndpointsFromData", QAFishing::Flags)
bool FQAFishCastDistanceEndpointsFromData::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const FLureFishingRow& Row : { Shipped, FixtureRow(Shipped) })
	{
		const FString Label = FString::Printf(TEXT("Min %.0f / Max %.0f"), Row.MinCastDistance, Row.MaxCastDistance);
		TestNearlyEqual(Label + TEXT(": no charge = MinCastDistance"), FLureFishingRules::CastDistance(Row, 0.f), Row.MinCastDistance, 0.01f);
		TestNearlyEqual(Label + TEXT(": full charge = MaxCastDistance"), FLureFishingRules::CastDistance(Row, 1.f), Row.MaxCastDistance, 0.01f);
		for (int32 Step = 0; Step <= 20; ++Step)
		{
			const float Charge = Step / 20.f;
			const double Expected = SpecDistance(Row, Charge);
			TestNearlyEqual(FString::Printf(TEXT("%s: charge %.2f follows Min + (Max - Min) * c^%.2f"), *Label, Charge, Row.ChargeExponent),
				static_cast<double>(FLureFishingRules::CastDistance(Row, Charge)), Expected, FMath::Max(0.05, Expected * 1.0e-5));
		}
	}
	// Min == Max: every charge casts that far.
	FLureFishingRow Flat = Shipped;
	Flat.MinCastDistance = Flat.MaxCastDistance = 777.f;
	for (const float Charge : { 0.f, 0.3f, 1.f })
	{
		TestNearlyEqual(FString::Printf(TEXT("Min == Max: charge %.1f = 777"), Charge), FLureFishingRules::CastDistance(Flat, Charge), 777.f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastDistanceClampedForAnyCharge, "Project.Fishing.QA.Cast.DistanceClampedForAnyCharge", QAFishing::Flags)
bool FQAFishCastDistanceClampedForAnyCharge::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const FLureFishingRow& Row : { Shipped, FixtureRow(Shipped) })
	{
		for (const float Charge : { -1.0e9f, -1.f, -0.0001f, 1.0001f, 2.f, 1.0e9f, NaN, Inf, -Inf })
		{
			const float Distance = FLureFishingRules::CastDistance(Row, Charge);
			TestTrue(FString::Printf(TEXT("charge %f: %f is within [%.0f, %.0f]"), Charge, Distance, Row.MinCastDistance, Row.MaxCastDistance),
				FMath::IsFinite(Distance) && Distance >= Row.MinCastDistance - 0.01f && Distance <= Row.MaxCastDistance + 0.01f);
		}
		TestNearlyEqual(TEXT("above 1 = Max"), FLureFishingRules::CastDistance(Row, 1.0001f), Row.MaxCastDistance, 0.01f);
		TestNearlyEqual(TEXT("far above 1 = Max"), FLureFishingRules::CastDistance(Row, 1.0e9f), Row.MaxCastDistance, 0.01f);
		TestNearlyEqual(TEXT("below 0 = Min"), FLureFishingRules::CastDistance(Row, -0.0001f), Row.MinCastDistance, 0.01f);
		TestNearlyEqual(TEXT("NaN = no charge = Min"), FLureFishingRules::CastDistance(Row, NaN), Row.MinCastDistance, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastDistanceMonotoneForAnyExponent, "Project.Fishing.QA.Cast.DistanceMonotoneForAnyExponent", QAFishing::Flags)
bool FQAFishCastDistanceMonotoneForAnyExponent::RunTest(const FString& Parameters)
{
	FLureFishingRow Row;
	if (!ShippedFishingRow(*this, Row))
	{
		return false;
	}
	for (const float Exponent : { 0.1f, 0.5f, 1.f, 2.f, 3.5f })
	{
		Row.ChargeExponent = Exponent;
		float Previous = -1.f;
		bool bNonDecreasing = true;
		int32 StrictSteps = 0;
		for (int32 Step = 0; Step <= 1000; ++Step)
		{
			const float Distance = FLureFishingRules::CastDistance(Row, Step / 1000.f);
			bNonDecreasing &= Distance >= Previous;
			StrictSteps += Distance > Previous ? 1 : 0;
			Previous = Distance;
		}
		TestTrue(FString::Printf(TEXT("exponent %.1f: more charge never casts shorter"), Exponent), bNonDecreasing);
		TestTrue(FString::Printf(TEXT("exponent %.1f: more charge casts farther (%d of 1001 steps grow)"), Exponent, StrictSteps), StrictSteps > 900);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastFlightTimeClamped, "Project.Fishing.QA.Cast.FlightTimeClamped", QAFishing::Flags)
bool FQAFishCastFlightTimeClamped::RunTest(const FString& Parameters)
{
	// Spec: flight time = distance / CastSpeed, clamped to [CastFlightTimeMin, CastFlightTimeMax].
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	for (const FLureFishingRow& Row : { Shipped, FixtureRow(Shipped) })
	{
		const FString Label = FString::Printf(TEXT("speed %.0f, [%.2f, %.2f] s"), Row.CastSpeed, Row.CastFlightTimeMin, Row.CastFlightTimeMax);
		const float Middle = 0.5f * (Row.CastFlightTimeMin + Row.CastFlightTimeMax);
		TestNearlyEqual(Label + TEXT(": inside the clamp = distance / speed"), FLureFishingRules::CastFlightTime(Row, Middle * Row.CastSpeed), Middle, 1.0e-4f);
		TestNearlyEqual(Label + TEXT(": at the lower edge"), FLureFishingRules::CastFlightTime(Row, Row.CastFlightTimeMin * Row.CastSpeed), Row.CastFlightTimeMin, 1.0e-4f);
		TestNearlyEqual(Label + TEXT(": at the upper edge"), FLureFishingRules::CastFlightTime(Row, Row.CastFlightTimeMax * Row.CastSpeed), Row.CastFlightTimeMax, 1.0e-4f);
		TestNearlyEqual(Label + TEXT(": zero distance = minimum"), FLureFishingRules::CastFlightTime(Row, 0.f), Row.CastFlightTimeMin, 1.0e-4f);
		TestNearlyEqual(Label + TEXT(": negative distance = minimum"), FLureFishingRules::CastFlightTime(Row, -500.f), Row.CastFlightTimeMin, 1.0e-4f);
		TestNearlyEqual(Label + TEXT(": huge distance = maximum"), FLureFishingRules::CastFlightTime(Row, 1.0e9f), Row.CastFlightTimeMax, 1.0e-4f);
		const float NaNFlight = FLureFishingRules::CastFlightTime(Row, NaN);
		TestTrue(Label + TEXT(": NaN distance stays in the clamp"), NaNFlight >= Row.CastFlightTimeMin && NaNFlight <= Row.CastFlightTimeMax);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastArcShape, "Project.Fishing.QA.Cast.ArcShape", QAFishing::Flags)
bool FQAFishCastArcShape::RunTest(const FString& Parameters)
{
	// Contract: a straight line From -> To plus a parabola of ArcHeight at the middle.
	const FVector From(100.f, -50.f, 300.f);
	const FVector To(1500.f, 400.f, 0.f);
	const float Arc = 260.f;
	TestTrue(TEXT("starts at From"), FLureFishingRules::CastArcPoint(From, To, Arc, 0.f).Equals(From, 0.01));
	TestTrue(TEXT("ends at To"), FLureFishingRules::CastArcPoint(From, To, Arc, 1.f).Equals(To, 0.01));
	const FVector Mid = FLureFishingRules::CastArcPoint(From, To, Arc, 0.5f);
	TestTrue(TEXT("the apex is ArcHeight above the middle of the line"), Mid.Equals(FMath::Lerp(From, To, 0.5f) + FVector(0.f, 0.f, Arc), 0.01));
	for (const float T : { 0.1f, 0.25f, 0.4f })
	{
		const float Above = static_cast<float>((FLureFishingRules::CastArcPoint(From, To, Arc, T) - FMath::Lerp(From, To, T)).Z);
		const float AboveMirror = static_cast<float>((FLureFishingRules::CastArcPoint(From, To, Arc, 1.f - T) - FMath::Lerp(From, To, 1.f - T)).Z);
		TestNearlyEqual(FString::Printf(TEXT("symmetric at %.2f / %.2f"), T, 1.f - T), Above, AboveMirror, 0.01f);
		TestTrue(FString::Printf(TEXT("above the line at %.2f, below the apex"), T), Above > 0.f && Above < Arc);
	}
	TestTrue(TEXT("alpha below 0 stays at From"), FLureFishingRules::CastArcPoint(From, To, Arc, -0.5f).Equals(From, 0.01));
	TestTrue(TEXT("alpha above 1 stays at To (the bobber never overshoots)"), FLureFishingRules::CastArcPoint(From, To, Arc, 1.5f).Equals(To, 0.01));
	const FVector NaNPoint = FLureFishingRules::CastArcPoint(From, To, Arc, NaN);
	TestTrue(TEXT("a NaN alpha gives a finite point on the path's ends"), !NaNPoint.ContainsNaN() && (NaNPoint.Equals(From, 0.01) || NaNPoint.Equals(To, 0.01)));
	return true;
}

// =====================================================================================================================
// In a world: the server casts
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastServerClampsRequestedCharge, "Project.Fishing.QA.Cast.ServerClampsRequestedCharge", QAFishing::Flags)
bool FQAFishCastServerClampsRequestedCharge::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	struct FCase
	{
		float Charge;
		float Expected;
	};
	const FCase Cases[] = { { 7.5f, Shipped.MaxCastDistance }, { 1.0e9f, Shipped.MaxCastDistance }, { -2.f, Shipped.MinCastDistance }, { NaN, Shipped.MinCastDistance },
		{ 0.5f, static_cast<float>(SpecDistance(Shipped, 0.5)) } };
	for (const FCase& Case : Cases)
	{
		float Distance = 0.f;
		FVector Rest;
		if (CastAndMeasure(*this, Character, Fishing, Case.Charge, 0.f, Distance, Rest))
		{
			TestNearlyEqual(FString::Printf(TEXT("requested charge %f: the server casts %.0f cm (got %.1f)"), Case.Charge, Case.Expected, Distance), Distance, Case.Expected, 1.f);
		}
	}
	float Distance = 0.f;
	FVector Rest;
	if (CastAndMeasure(*this, Character, Fishing, Inf, 0.f, Distance, Rest))
	{
		TestTrue(FString::Printf(TEXT("an infinite charge still lands within [Min, Max] (%.1f)"), Distance), Distance >= Shipped.MinCastDistance - 1.f && Distance <= Shipped.MaxCastDistance + 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastLandingFollowsTheTable, "Project.Fishing.QA.Cast.LandingFollowsTheTable", QAFishing::Flags)
bool FQAFishCastLandingFollowsTheTable::RunTest(const FString& Parameters)
{
	// The table path (ApplyFishingTable with an edited copy of DT_Fishing.csv), not SetFishingProfile: other numbers, other casts.
	FString Csv;
	FScene Scene;
	if (!LoadFishingCsv(*this, Csv) || !Scene.Create(*this))
	{
		return false;
	}
	Csv = WithCell(*this, Csv, TEXT("Default"), TEXT("MinCastDistance"), TEXT("500"));
	Csv = WithCell(*this, Csv, TEXT("Default"), TEXT("MaxCastDistance"), TEXT("1500"));
	Csv = WithCell(*this, Csv, TEXT("Default"), TEXT("ChargeExponent"), TEXT("2"));
	const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(*this, FLureFishingRow::StaticStruct(), Csv, TEXT("DT_Fishing fixture")));
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!Fishing)
	{
		return false;
	}
	Fishing->ApplyFishingTable(Table.Get());
	TestFalse(TEXT("the fixture table is used (no fallback)"), Fishing->IsUsingFallbackProfile());
	Scene.Tick(10);
	for (const float Charge : { 0.f, 0.5f, 1.f })
	{
		const float Expected = 500.f + 1000.f * Charge * Charge;
		float Distance = 0.f;
		FVector Rest;
		if (CastAndMeasure(*this, Character, Fishing, Charge, 0.f, Distance, Rest))
		{
			TestNearlyEqual(FString::Printf(TEXT("fixture: charge %.1f lands %.0f cm out (got %.1f)"), Charge, Expected, Distance), Distance, Expected, 1.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastAimYawSetsDirection, "Project.Fishing.QA.Cast.AimYawSetsDirection", QAFishing::Flags)
bool FQAFishCastAimYawSetsDirection::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this, /*bDock*/ false))
	{
		return false;
	}
	// A small raft far out at sea, so every direction is open water.
	Scene.AddBox(FVector(0.f, 0.f, 25.f), FVector(150.f, 150.f, 25.f));
	ALurePlayerCharacter* Character = Scene.Spawn(*this, FVector(0.f, 0.f, 50.f));
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	for (const float Yaw : { -90.f, 0.f, 45.f, 135.f, 179.f, 540.f })
	{
		float Distance = 0.f;
		FVector Rest;
		if (!CastAndMeasure(*this, Character, Fishing, 1.f, Yaw, Distance, Rest))
		{
			continue;
		}
		const FVector Direction = (Rest - Character->GetPawnViewLocation()).GetSafeNormal2D();
		const float Got = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
		TestTrue(FString::Printf(TEXT("aim yaw %.0f: lands along %.1f deg"), Yaw, Got), FMath::Abs(FRotator::NormalizeAxis(Got - Yaw)) < 0.5f);
		TestNearlyEqual(FString::Printf(TEXT("aim yaw %.0f: full distance"), Yaw), Distance, Shipped.MaxCastDistance, 1.f);
	}
	float Distance = 0.f;
	FVector Rest;
	if (CastAndMeasure(*this, Character, Fishing, 1.f, NaN, Distance, Rest))
	{
		TestTrue(TEXT("a NaN aim still lands at a finite point within the cast range"), !Rest.ContainsNaN() && Distance <= Shipped.MaxCastDistance + 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastSecondCastWhileLineOutIsRefused, "Project.Fishing.QA.Cast.SecondCastWhileLineOutIsRefused", QAFishing::Flags)
bool FQAFishCastSecondCastWhileLineOutIsRefused::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Scene.Spawn(*this), FlowProfile(Shipped, 30.f));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	TestTrue(TEXT("first cast"), Fishing->AuthorityCast(0.5f, 0.f));
	Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 180);
	const FLureFishingNetState Before = Fishing->GetNetState();
	TestFalse(TEXT("a second cast while the line is out is refused"), Fishing->AuthorityCast(1.f, 90.f));
	const FLureFishingNetState& After = Fishing->GetNetState();
	TestEqual(TEXT("... result Refused"), ResultName(After.LastResult), ResultName(ELureFishingResult::Refused));
	TestEqual(TEXT("... reason Busy"), BlockName(After.ResultReason), BlockName(ELureCastBlock::Busy));
	TestEqual(TEXT("the line out is untouched: state"), StateName(After.State), StateName(Before.State));
	TestEqual(TEXT("... same cast id"), After.CastId, Before.CastId);
	TestTrue(TEXT("... same bobber rest"), FVector(After.BobberRest).Equals(FVector(Before.BobberRest), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastRefusedCastKeepsTheLineIdle, "Project.Fishing.QA.Cast.RefusedCastKeepsTheLineIdle", QAFishing::Flags)
bool FQAFishCastRefusedCastKeepsTheLineIdle::RunTest(const FString& Parameters)
{
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped));
	if (!Fishing)
	{
		return false;
	}
	Scene.Tick(10);
	Fishing->bRodEquipped = false;
	const FLureFishingNetState Before = Fishing->GetNetState();
	TestFalse(TEXT("no rod: the server refuses"), Fishing->AuthorityCast(1.f, 0.f));
	const FLureFishingNetState& After = Fishing->GetNetState();
	TestEqual(TEXT("still Idle"), StateName(After.State), StateName(ELureFishingState::Idle));
	TestEqual(TEXT("result Refused"), ResultName(After.LastResult), ResultName(ELureFishingResult::Refused));
	TestEqual(TEXT("reason NoRod"), BlockName(After.ResultReason), BlockName(ELureCastBlock::NoRod));
	TestEqual(TEXT("a new result id (so the same refusal twice still shows)"), After.ResultId, static_cast<uint8>(Before.ResultId + 1));
	TestEqual(TEXT("no new cast id"), After.CastId, Before.CastId);
	TestTrue(TEXT("HUD: can't cast"), Fishing->GetStatusText().Contains(TEXT("Can't cast")));
	Fishing->bRodEquipped = true;
	TestTrue(TEXT("rod back: the cast works"), Fishing->AuthorityCast(1.f, 0.f));
	return true;
}

// =====================================================================================================================
// The profile: DT_Fishing rows, ProfileRow, fallback
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastProfileRowSelection, "Project.Fishing.QA.Cast.ProfileRowSelection", QAFishing::Flags)
bool FQAFishCastProfileRowSelection::RunTest(const FString& Parameters)
{
	FString Csv;
	FScene Scene;
	if (!LoadFishingCsv(*this, Csv) || !Scene.Create(*this))
	{
		return false;
	}
	// A gear profile row next to Default (gear picks rows later without code).
	Csv = WithRowCopy(*this, Csv, TEXT("Default"), TEXT("QA_Pro"));
	Csv = WithCell(*this, Csv, TEXT("QA_Pro"), TEXT("MaxCastDistance"), TEXT("2400"));
	Csv = WithCell(*this, Csv, TEXT("QA_Pro"), TEXT("MaxLineLength"), TEXT("3200"));
	Csv = WithCell(*this, Csv, TEXT("QA_Pro"), TEXT("HookWindow"), TEXT("1.25"));
	const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(*this, FLureFishingRow::StaticStruct(), Csv, TEXT("DT_Fishing fixture")));
	const FLureFishingRow* Default = Table->FindRow<FLureFishingRow>(TEXT("Default"), TEXT("QA"), false);
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!Fishing || !TestNotNull(TEXT("Default row"), Default))
	{
		return false;
	}
	TestEqual(TEXT("the settings' default profile row is Default"), GetDefault<ULureFishingSettings>()->DefaultProfileRow, FName(TEXT("Default")));

	Fishing->ProfileRow = NAME_None;
	Fishing->ApplyFishingTable(Table.Get());
	TestFalse(TEXT("ProfileRow None: a table row is used"), Fishing->IsUsingFallbackProfile());
	TestNearlyEqual(TEXT("ProfileRow None: the Default row"), Fishing->GetProfile().MaxCastDistance, Default->MaxCastDistance, 0.001f);

	Fishing->ProfileRow = TEXT("QA_Pro");
	Fishing->ApplyFishingTable(Table.Get());
	TestFalse(TEXT("ProfileRow QA_Pro: a table row is used"), Fishing->IsUsingFallbackProfile());
	TestNearlyEqual(TEXT("ProfileRow QA_Pro: its MaxCastDistance"), Fishing->GetProfile().MaxCastDistance, 2400.f, 0.001f);
	TestNearlyEqual(TEXT("ProfileRow QA_Pro: its HookWindow"), Fishing->GetProfile().HookWindow, 1.25f, 0.001f);

	AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	Fishing->ProfileRow = TEXT("QA_NoSuchRow");
	Fishing->ApplyFishingTable(Table.Get());
	TestTrue(TEXT("an unknown ProfileRow: the built-in profile"), Fishing->IsUsingFallbackProfile());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastMissingOrBadTableUsesBuiltInProfile, "Project.Fishing.QA.Cast.MissingOrBadTableUsesBuiltInProfile", QAFishing::Flags)
bool FQAFishCastMissingOrBadTableUsesBuiltInProfile::RunTest(const FString& Parameters)
{
	// Spec: a missing table or row uses the built-in profile with one warning (never an error). The built-in profile
	// is the shipped Default row.
	FString FishingCsv;
	FString MovementCsv;
	FLureFishingRow Shipped;
	FScene Scene;
	if (!LoadFishingCsv(*this, FishingCsv) || !LoadMovementCsv(*this, MovementCsv) || !ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!Fishing)
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> WrongStruct(MakeTableChecked(*this, FLureMovementRow::StaticStruct(), MovementCsv, TEXT("DT_Movement")));
	const TStrongObjectPtr<UDataTable> InvalidRow(MakeTableChecked(*this, FLureFishingRow::StaticStruct(),
		WithCell(*this, FishingCsv, TEXT("Default"), TEXT("HookWindow"), TEXT("0")), TEXT("DT_Fishing HookWindow 0")));
	const TStrongObjectPtr<UDataTable> Reversed(MakeTableChecked(*this, FLureFishingRow::StaticStruct(),
		WithCell(*this, FishingCsv, TEXT("Default"), TEXT("MinCastDistance"), TEXT("5000")), TEXT("DT_Fishing Min > Max")));
	const TStrongObjectPtr<UDataTable> NoDefault(MakeTableChecked(*this, FLureFishingRow::StaticStruct(),
		WithRowCopy(*this, FishingCsv, TEXT("Default"), TEXT("Other")).Replace(TEXT("\nDefault,"), TEXT("\nRenamed,")), TEXT("DT_Fishing without Default")));

	AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FLogCapture Capture(TEXT("LogLureFishing"));
	struct FCase
	{
		const TCHAR* Name;
		const UDataTable* Table;
	};
	const FCase Cases[] = { { TEXT("no table"), nullptr }, { TEXT("wrong row struct"), WrongStruct.Get() }, { TEXT("HookWindow 0"), InvalidRow.Get() },
		{ TEXT("Min > Max"), Reversed.Get() }, { TEXT("no Default row"), NoDefault.Get() } };
	for (const FCase& Case : Cases)
	{
		Fishing->ApplyFishingTable(Case.Table);
		TestTrue(FString::Printf(TEXT("%s: the built-in profile"), Case.Name), Fishing->IsUsingFallbackProfile());
		const TArray<FString> Differences = DifferentFields(FLureFishingRow::StaticStruct(), &Fishing->GetProfile(), &Shipped);
		TestEqual(FString::Printf(TEXT("%s: the built-in profile equals the shipped Default row (differs in: %s)"), Case.Name, *FString::Join(Differences, TEXT(", "))),
			Differences.Num(), 0);
	}
	TestTrue(FString::Printf(TEXT("at most one fallback warning for %d fallbacks (got %d)"), UE_ARRAY_COUNT(Cases), Capture.Count(ELogVerbosity::Warning)),
		Capture.Count(ELogVerbosity::Warning) <= 1);
	TestEqual(TEXT("never an error"), Capture.Count(ELogVerbosity::Error), 0);

	// A good table afterwards is used again.
	const TStrongObjectPtr<UDataTable> Good(MakeTableChecked(*this, FLureFishingRow::StaticStruct(), FishingCsv, TEXT("DT_Fishing")));
	Fishing->ApplyFishingTable(Good.Get());
	TestFalse(TEXT("the shipped table afterwards: no fallback"), Fishing->IsUsingFallbackProfile());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastInvalidProfileRowFallsBack, "Project.Fishing.QA.Cast.InvalidProfileRowFallsBack", QAFishing::Flags)
bool FQAFishCastInvalidProfileRowFallsBack::RunTest(const FString& Parameters)
{
	// SetFishingProfile validates too: a broken row (e.g. from gear data) never reaches the rules.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!Fishing)
	{
		return false;
	}
	AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	FLureFishingRow Bad = Shipped;
	Bad.ChargeTime = 0.f;
	Bad.MaxCastDistance = 99999.f;
	Fishing->SetFishingProfile(Bad);
	TestTrue(TEXT("ChargeTime 0: falls back"), Fishing->IsUsingFallbackProfile());
	TestNearlyEqual(TEXT("... to the shipped MaxCastDistance"), Fishing->GetProfile().MaxCastDistance, Shipped.MaxCastDistance, 0.001f);
	FLureFishingRow Good = Shipped;
	Good.MaxCastDistance = 2000.f;
	Good.MaxLineLength = 2600.f;
	Fishing->SetFishingProfile(Good);
	TestFalse(TEXT("a valid row is used"), Fishing->IsUsingFallbackProfile());
	TestNearlyEqual(TEXT("... with its values"), Fishing->GetProfile().MaxCastDistance, 2000.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishCastSettingsPathMatchesEnvironment, "Project.Fishing.QA.Cast.SettingsPathMatchesEnvironment", QAFishing::Flags)
bool FQAFishCastSettingsPathMatchesEnvironment::RunTest(const FString& Parameters)
{
	// The game's own path: the settings point at /Game/Data/DT_Fishing. In a lane the asset is not imported (built-in profile);
	// in main after the editor-operator's import, a fresh component uses the asset, whose Default row equals the CSV.
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	TestEqual(TEXT("settings: FishingTable"), Settings->FishingTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_Fishing.DT_Fishing")));
	TestEqual(TEXT("settings: DefaultProfileRow"), Settings->DefaultProfileRow, FName(TEXT("Default")));
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
	if (!Fishing)
	{
		return false;
	}
	const bool bImported = FPackageName::DoesPackageExist(Settings->FishingTable.ToSoftObjectPath().GetLongPackageName());
	AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	const TArray<FString> Differences = DifferentFields(FLureFishingRow::StaticStruct(), &Fishing->GetProfile(), &Shipped);
	if (bImported)
	{
		TestFalse(TEXT("DT_Fishing is imported: the component uses it"), Fishing->IsUsingFallbackProfile());
	}
	else
	{
		AddInfo(TEXT("/Game/Data/DT_Fishing is not imported here (lane): the built-in profile is expected."));
		TestTrue(TEXT("not imported: the built-in profile"), Fishing->IsUsingFallbackProfile());
	}
	TestEqual(FString::Printf(TEXT("either way the profile equals the CSV's Default row (differs in: %s)"), *FString::Join(Differences, TEXT(", "))), Differences.Num(), 0);
	return true;
}

} // namespace QAFishing

#endif // WITH_DEV_AUTOMATION_TESTS

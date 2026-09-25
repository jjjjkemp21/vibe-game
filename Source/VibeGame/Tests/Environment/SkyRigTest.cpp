// Lure T-068b (unreal-engineer): the sky rig and the time-of-day blend, docs/specs/day-night-water.md §3.2 and §5 AC7-AC8.
// Tables come from data/tables/DT_TimeOfDay.json and DT_DayCycle.json (never the binary assets). The clock is driven with
// SetHour at scale 0 in transient game worlds. Project.Environment.Sky.*

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING && WITH_EDITOR

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DataTable.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Environment/LureDayClock.h"
#include "Environment/LureDayClockComponent.h"
#include "Environment/LureSkyRig.h"
#include "Environment/LureTimeOfDay.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace LureSkyRigTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	UDataTable* LoadJsonTable(FAutomationTestBase& Test, const TCHAR* File, UScriptStruct* RowStruct)
	{
		FString Json;
		if (!Test.TestTrue(FString::Printf(TEXT("data/tables/%s loads"), File),
			FFileHelper::LoadFileToString(Json, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / File))))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = RowStruct;
		for (const FString& Problem : Table->CreateTableFromJSONString(Json))
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), File, *Problem));
		}
		return Table;
	}

	UDataTable* LoadTimeOfDay(FAutomationTestBase& Test)
	{
		return LoadJsonTable(Test, TEXT("DT_TimeOfDay.json"), FLureTimeOfDayRow::StaticStruct());
	}

	/** The clock over the shipped DT_DayCycle Default row (phase midpoints Dawn 6, Day 12, Dusk 18, Night 0). */
	FLureDayClock DefaultClock(FAutomationTestBase& Test)
	{
		const TStrongObjectPtr<UDataTable> Table(LoadJsonTable(Test, TEXT("DT_DayCycle.json"), FLureDayCycleRow::StaticStruct()));
		const FLureDayCycleRow* Row = Table.IsValid() ? Table->FindRow<FLureDayCycleRow>(TEXT("Default"), TEXT("SkyRigTest"), false) : nullptr;
		Test.TestNotNull(TEXT("DT_DayCycle has a Default row"), Row);
		return Row ? FLureDayClock(*Row) : FLureDayClock();
	}

	FLinearColor Hex(const TCHAR* Text)
	{
		return FLinearColor(FColor::FromHex(Text));
	}

	FLinearColor Mean(const FLinearColor& A, const FLinearColor& B)
	{
		return FLinearColor((A.R + B.R) * 0.5f, (A.G + B.G) * 0.5f, (A.B + B.B) * 0.5f, 1.f);
	}

	void TestColor(FAutomationTestBase& Test, const FString& What, const FLinearColor& Actual, const FLinearColor& Expected, float Tolerance = 1e-4f)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: (%.5f, %.5f, %.5f) ~ (%.5f, %.5f, %.5f)"), *What, Actual.R, Actual.G, Actual.B, Expected.R, Expected.G, Expected.B),
			FMath::IsNearlyEqual(Actual.R, Expected.R, Tolerance) && FMath::IsNearlyEqual(Actual.G, Expected.G, Tolerance) && FMath::IsNearlyEqual(Actual.B, Expected.B, Tolerance));
	}

	/** Every value of Actual is the linear mean of A and B (colours per linear channel). */
	void TestIsMean(FAutomationTestBase& Test, const FString& Label, const FLureTimeOfDayLook& Actual, const FLureTimeOfDayLook& A, const FLureTimeOfDayLook& B)
	{
		auto F = [&](const TCHAR* Name, float V, float VA, float VB) { Test.TestNearlyEqual(Label + TEXT(" ") + Name, V, (VA + VB) * 0.5f, 1e-4f); };
		auto C = [&](const TCHAR* Name, const FLinearColor& V, const FLinearColor& VA, const FLinearColor& VB) { TestColor(Test, Label + TEXT(" ") + Name, V, Mean(VA, VB)); };
		F(TEXT("SunElevation"), Actual.SunElevation, A.SunElevation, B.SunElevation);
		C(TEXT("SunColor"), Actual.SunColor, A.SunColor, B.SunColor);
		F(TEXT("SunIntensity"), Actual.SunIntensity, A.SunIntensity, B.SunIntensity);
		C(TEXT("MoonColor"), Actual.MoonColor, A.MoonColor, B.MoonColor);
		F(TEXT("MoonIntensity"), Actual.MoonIntensity, A.MoonIntensity, B.MoonIntensity);
		C(TEXT("SkyLuminanceFactor"), Actual.SkyLuminanceFactor, A.SkyLuminanceFactor, B.SkyLuminanceFactor);
		F(TEXT("SkyLightIntensity"), Actual.SkyLightIntensity, A.SkyLightIntensity, B.SkyLightIntensity);
		C(TEXT("FogColor"), Actual.FogColor, A.FogColor, B.FogColor);
		F(TEXT("FogDensity"), Actual.FogDensity, A.FogDensity, B.FogDensity);
		F(TEXT("FogHeightFalloff"), Actual.FogHeightFalloff, A.FogHeightFalloff, B.FogHeightFalloff);
		F(TEXT("FogStartDistance"), Actual.FogStartDistance, A.FogStartDistance, B.FogStartDistance);
		F(TEXT("FogMaxOpacity"), Actual.FogMaxOpacity, A.FogMaxOpacity, B.FogMaxOpacity);
		F(TEXT("FogSkyAmbient"), Actual.FogSkyAmbient, A.FogSkyAmbient, B.FogSkyAmbient);
		F(TEXT("ExposureEV100"), Actual.ExposureEV100, A.ExposureEV100, B.ExposureEV100);
		C(TEXT("WaterShallowColor"), Actual.WaterShallowColor, A.WaterShallowColor, B.WaterShallowColor);
		C(TEXT("WaterDeepColor"), Actual.WaterDeepColor, A.WaterDeepColor, B.WaterDeepColor);
		C(TEXT("WaterFoamColor"), Actual.WaterFoamColor, A.WaterFoamColor, B.WaterFoamColor);
		F(TEXT("WaveScale"), Actual.WaveScale, A.WaveScale, B.WaveScale);
		F(TEXT("NightLightIntensity"), Actual.NightLightIntensity, A.NightLightIntensity, B.NightLightIntensity);
	}

	/** Two rows that differ in every value (every colour channel too). */
	void MakeDistinctRows(FLureTimeOfDayRow& A, FLureTimeOfDayRow& B)
	{
		A.SunPitch = 40.f; B.SunPitch = -20.f;
		A.SunColor = TEXT("#FFF4E0"); B.SunColor = TEXT("#204080");
		A.SunIntensity = 10.f; B.SunIntensity = 2.f;
		A.MoonColor = TEXT("#102030"); B.MoonColor = TEXT("#A0B0C0");
		A.MoonIntensity = 0.f; B.MoonIntensity = 0.4f;
		A.SkyLuminanceR = 2.f; B.SkyLuminanceR = 1.f; A.SkyLuminanceG = 3.f; B.SkyLuminanceG = 0.5f; A.SkyLuminanceB = 4.f; B.SkyLuminanceB = 1.5f;
		A.SkyLightIntensity = 0.35f; B.SkyLightIntensity = 0.1f;
		A.FogColor = TEXT("#8FD3F0"); B.FogColor = TEXT("#1B2440");
		A.FogDensity = 0.05f; B.FogDensity = 0.02f;
		A.FogHeightFalloff = 0.2f; B.FogHeightFalloff = 0.1f;
		A.FogStartDistance = 3000.f; B.FogStartDistance = 1000.f;
		A.FogMaxOpacity = 1.f; B.FogMaxOpacity = 0.6f;
		A.FogSkyAmbient = 0.f; B.FogSkyAmbient = 0.5f;
		A.ExposureEV100 = 1.f; B.ExposureEV100 = -3.f;
		A.WaterShallowColor = TEXT("#3ED1C4"); B.WaterShallowColor = TEXT("#103040");
		A.WaterDeepColor = TEXT("#0A5560"); B.WaterDeepColor = TEXT("#020810");
		A.WaterFoamColor = TEXT("#F2FBF8"); B.WaterFoamColor = TEXT("#8090A0");
		A.WaveScale = 0.7f; B.WaveScale = 1.2f;
		A.NightLightIntensity = 0.f; B.NightLightIntensity = 1.f;
	}

	/** The horizontal direction (x, y) a light travels. */
	FVector2D Heading(const FRotator& Rotation)
	{
		const FVector Forward = Rotation.Vector();
		return FVector2D(Forward.X, Forward.Y).GetSafeNormal();
	}

	/** A game world with a clock frozen at scale 0, plus spawned sky targets. */
	struct FSkyWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ULureDayClockComponent* Clock = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			Clock = ULureDayClockComponent::Get(World);
			if (!Test.TestNotNull(TEXT("the world has the day clock"), Clock))
			{
				return false;
			}
			Clock->SetTimeScale(0.f);
			return true;
		}

		template <typename T>
		T* Spawn(FName Tag = NAME_None)
		{
			T* Actor = World->SpawnActor<T>();
			if (Actor && !Tag.IsNone())
			{
				Actor->Tags.Add(Tag);
			}
			return Actor;
		}

		APointLight* SpawnPointLight(float Intensity, FName Tag)
		{
			APointLight* Light = Spawn<APointLight>(Tag);
			Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
			Light->GetLightComponent()->SetIntensity(Intensity);
			return Light;
		}

		/** The rig, spawned deferred so it uses Table's rows from its BeginPlay (null Table = no rows). */
		ALureSkyRig* SpawnRig(const UDataTable* Table)
		{
			ALureSkyRig* Rig = World->SpawnActorDeferred<ALureSkyRig>(ALureSkyRig::StaticClass(), FTransform::Identity);
			Rig->SetTimeOfDayTable(Table);
			Rig->FinishSpawning(FTransform::Identity);
			return Rig;
		}
	};
}

// ---- DT_TimeOfDay: every row validates, every region has all four phases, the shipped Tropical values ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSkyDataTest, "Project.Environment.Sky.Data.TimeOfDayRows", LureSkyRigTest::Flags)

bool FLureSkyDataTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Table(LureSkyRigTest::LoadTimeOfDay(*this));
	if (!Table.IsValid())
	{
		return false;
	}
	TArray<FString> Problems;
	TestTrue(TEXT("DT_TimeOfDay.json validates (names, values, 4 phases per region)"), FLureTimeOfDayBlend::ValidateTable(Table.Get(), &Problems));
	for (const FString& Problem : Problems)
	{
		AddError(TEXT("DT_TimeOfDay: ") + Problem);
	}
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		TArray<FString> RowProblems;
		TestTrue(FString::Printf(TEXT("row %s validates"), *Pair.Key.ToString()),
			FLureTimeOfDayBlend::ValidateRow(*reinterpret_cast<const FLureTimeOfDayRow*>(Pair.Value), &RowProblems));
	}

	// The four Tropical rows with the spec §4 values (WaveScale, NightLightIntensity) and the migrated layout presets.
	const TMap<ELureDayPhase, FLureTimeOfDayRow> Tropical = FLureTimeOfDayBlend::GatherRegion(Table.Get(), TEXT("Tropical"));
	TestEqual(TEXT("Tropical has 4 rows"), Tropical.Num(), 4);
	struct FExpect { ELureDayPhase Phase; float Wave; float Night; float EV; const TCHAR* Fog; };
	const FExpect Expected[] = {
		{ ELureDayPhase::Dawn, 0.9f, 0.3f, 0.5f, TEXT("#F6C7A0") },
		{ ELureDayPhase::Day, 0.7f, 0.f, 1.f, TEXT("#8FD3F0") },
		{ ELureDayPhase::Dusk, 1.f, 1.f, 0.f, TEXT("#FF9A5A") },
		{ ELureDayPhase::Night, 1.2f, 1.f, -3.f, TEXT("#1B2440") } };
	for (const FExpect& E : Expected)
	{
		const FString Name = FLureTimeOfDayBlend::MakeRowName(TEXT("Tropical"), E.Phase).ToString();
		const FLureTimeOfDayRow* Row = Tropical.Find(E.Phase);
		if (!TestNotNull(Name + TEXT(" exists"), Row))
		{
			continue;
		}
		TestNearlyEqual(Name + TEXT(" WaveScale (spec §4)"), Row->WaveScale, E.Wave);
		TestNearlyEqual(Name + TEXT(" NightLightIntensity (spec §4)"), Row->NightLightIntensity, E.Night);
		TestNearlyEqual(Name + TEXT(" ExposureEV100"), Row->ExposureEV100, E.EV);
		TestEqual(Name + TEXT(" FogColor"), Row->FogColor, FString(E.Fog));
		TestTrue(Name + TEXT(" anchors at its phase midpoint (AnchorHour < 0)"), Row->AnchorHour < 0.f);
	}
	if (const FLureTimeOfDayRow* Day = Tropical.Find(ELureDayPhase::Day))
	{
		TestNearlyEqual(TEXT("Tropical_Day = the approved layout day: fog density 0.05"), Day->FogDensity, 0.05f);
		TestNearlyEqual(TEXT("... sun pitch 50"), Day->SunPitch, 50.f);
		TestNearlyEqual(TEXT("... sun 10 lux"), Day->SunIntensity, 10.f);
		TestNearlyEqual(TEXT("... sky light 0.35"), Day->SkyLightIntensity, 0.35f);
		TestNearlyEqual(TEXT("... sky luminance B 3.1"), Day->SkyLuminanceB, 3.1f);
	}
	if (const FLureTimeOfDayRow* Dawn = Tropical.Find(ELureDayPhase::Dawn))
	{
		TestNearlyEqual(TEXT("Tropical_Dawn: sun 8 deg (spec §4)"), Dawn->SunPitch, 8.f);
	}

	// Validation catches bad data.
	{
		FLureTimeOfDayRow Bad;
		Bad.FogColor = TEXT("8FD3F");
		Bad.NightLightIntensity = 2.f;
		Bad.FogDensity = -1.f;
		Bad.AnchorHour = 24.f;
		TArray<FString> BadProblems;
		TestFalse(TEXT("a bad row fails validation"), FLureTimeOfDayBlend::ValidateRow(Bad, &BadProblems));
		TestEqual(TEXT("... with one problem per bad value"), BadProblems.Num(), 4);
		TestTrue(TEXT("a default row validates"), FLureTimeOfDayBlend::ValidateRow(FLureTimeOfDayRow()));
	}
	{
		UDataTable* Missing = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Missing->RowStruct = FLureTimeOfDayRow::StaticStruct();
		Missing->AddRow(TEXT("Foggy_Day"), FLureTimeOfDayRow());
		Missing->AddRow(TEXT("Foggy_Night"), FLureTimeOfDayRow());
		Missing->AddRow(TEXT("Foggy"), FLureTimeOfDayRow());
		TArray<FString> MissingProblems;
		TestFalse(TEXT("a region without all four phases (and a bad row name) fails"), FLureTimeOfDayBlend::ValidateTable(Missing, &MissingProblems));
		TestEqual(TEXT("... Foggy_Dawn, Foggy_Dusk missing + 'Foggy' not <Region>_<Phase>"), MissingProblems.Num(), 3);
	}
	FName Region;
	ELureDayPhase Phase;
	TestTrue(TEXT("'Murky_Deep_Night' parses (region may hold '_')"), FLureTimeOfDayBlend::ParseRowName(TEXT("Murky_Deep_Night"), Region, Phase)
		&& Region == FName(TEXT("Murky_Deep")) && Phase == ELureDayPhase::Night);
	return true;
}

// ---- AC7: the midpoint between two rows is the linear mean of every value (colours in linear space) ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSkyBlendTest, "Project.Environment.Sky.Pure.BlendMidpointIsLinearMean", LureSkyRigTest::Flags)

bool FLureSkyBlendTest::RunTest(const FString& Parameters)
{
	using namespace LureSkyRigTest;
	const FLureDayClock Clock = DefaultClock(*this);
	TestNearlyEqual(TEXT("Dawn midpoint 06:00"), FLureTimeOfDayBlend::PhaseMidpoint(Clock, ELureDayPhase::Dawn), 6.0, 1e-9);
	TestNearlyEqual(TEXT("Day midpoint 12:00"), FLureTimeOfDayBlend::PhaseMidpoint(Clock, ELureDayPhase::Day), 12.0, 1e-9);
	TestNearlyEqual(TEXT("Dusk midpoint 18:00"), FLureTimeOfDayBlend::PhaseMidpoint(Clock, ELureDayPhase::Dusk), 18.0, 1e-9);
	TestNearlyEqual(TEXT("Night midpoint 00:00 (19 -> 5 wraps)"), FLureTimeOfDayBlend::PhaseMidpoint(Clock, ELureDayPhase::Night), 0.0, 1e-9);

	// Two rows that differ in every value: at the hour halfway between their anchors, each value is the mean.
	FLureTimeOfDayRow A, B;
	MakeDistinctRows(A, B);
	TMap<ELureDayPhase, FLureTimeOfDayRow> Rows;
	Rows.Add(ELureDayPhase::Dusk, A);
	Rows.Add(ELureDayPhase::Night, B);
	const TArray<FLureTimeOfDayAnchor> Anchors = FLureTimeOfDayBlend::BuildAnchors(Rows, Clock);
	const FLureTimeOfDayLook LookA = FLureTimeOfDayBlend::Resolve(A);
	const FLureTimeOfDayLook LookB = FLureTimeOfDayBlend::Resolve(B);
	TestColor(*this, TEXT("colours resolve sRGB hex -> linear (#8FD3F0)"), LookA.FogColor, FLinearColor(0.274677f, 0.651406f, 0.871367f));
	TestIsMean(*this, TEXT("21:00 (Dusk 18 .. Night 24)"), FLureTimeOfDayBlend::Evaluate(Anchors, 21.0), LookA, LookB);
	TestIsMean(*this, TEXT("09:00 (Night 0 .. Dusk 18, across the other way)"), FLureTimeOfDayBlend::Evaluate(Anchors, 9.0), LookB, LookA);
	TestIsMean(*this, TEXT("Lerp 0.5"), FLureTimeOfDayBlend::Lerp(LookA, LookB, 0.5f), LookA, LookB);
	TestNearlyEqual(TEXT("at the Dusk anchor = the Dusk row"), FLureTimeOfDayBlend::Evaluate(Anchors, 18.0).SunIntensity, A.SunIntensity);
	TestNearlyEqual(TEXT("at the Night anchor (24 = 0) = the Night row"), FLureTimeOfDayBlend::Evaluate(Anchors, 24.0).SunIntensity, B.SunIntensity);
	TestNearlyEqual(TEXT("22:00 is 2/3 of the way Dusk -> Night"), FLureTimeOfDayBlend::Evaluate(Anchors, 22.0).ExposureEV100, 1.f + (-3.f - 1.f) * (2.f / 3.f), 1e-4f);

	// The shipped rows: 12:00 = Tropical_Day exactly; 03:00 blends Night -> Dawn across midnight.
	const TStrongObjectPtr<UDataTable> Table(LoadTimeOfDay(*this));
	const TArray<FLureTimeOfDayAnchor> Shipped = FLureTimeOfDayBlend::BuildAnchors(FLureTimeOfDayBlend::GatherRegion(Table.Get(), TEXT("Tropical")), Clock);
	if (TestEqual(TEXT("four Tropical anchors"), Shipped.Num(), 4))
	{
		TestTrue(TEXT("anchors ascend: Night 0, Dawn 6, Day 12, Dusk 18"), Shipped[0].Phase == ELureDayPhase::Night && Shipped[1].Phase == ELureDayPhase::Dawn
			&& Shipped[2].Phase == ELureDayPhase::Day && Shipped[3].Phase == ELureDayPhase::Dusk);
		const FLureTimeOfDayLook Noon = FLureTimeOfDayBlend::Evaluate(Shipped, 12.0);
		TestNearlyEqual(TEXT("12:00: EV 1 (Tropical_Day)"), Noon.ExposureEV100, 1.f);
		TestNearlyEqual(TEXT("12:00: night lights 0"), Noon.NightLightIntensity, 0.f);
		TestIsMean(*this, TEXT("03:00 (Night 0 .. Dawn 6)"), FLureTimeOfDayBlend::Evaluate(Shipped, 3.0), Shipped[0].Look, Shipped[1].Look);
	}

	// AnchorHour moves a row's anchor (design can shift it without code).
	FLureTimeOfDayRow Moved = A;
	Moved.AnchorHour = 16.f;
	TestNearlyEqual(TEXT("AnchorHour 16 overrides the Dusk midpoint"), FLureTimeOfDayBlend::AnchorHour(Moved, ELureDayPhase::Dusk, Clock), 16.0, 1e-9);
	TestNearlyEqual(TEXT("AnchorHour -1 = the midpoint"), FLureTimeOfDayBlend::AnchorHour(A, ELureDayPhase::Dusk, Clock), 18.0, 1e-9);

	// Exposure is manual, bias = -EV100, never auto.
	FPostProcessSettings Settings;
	FLureTimeOfDayBlend::ApplyFixedExposure(Settings, -1.5f);
	TestTrue(TEXT("exposure method overridden to Manual"), Settings.bOverride_AutoExposureMethod && Settings.AutoExposureMethod == EAutoExposureMethod::AEM_Manual);
	TestTrue(TEXT("no physical camera exposure"), Settings.bOverride_AutoExposureApplyPhysicalCameraExposure && !Settings.AutoExposureApplyPhysicalCameraExposure);
	TestTrue(TEXT("bias = -EV100"), Settings.bOverride_AutoExposureBias && FMath::IsNearlyEqual(Settings.AutoExposureBias, 1.5f));
	return true;
}

// ---- The fog's on-screen target: the ported exposure + filmic inverse matches levels/layout.py ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSkyFogTargetTest, "Project.Environment.Sky.Pure.FogOnScreenTarget", LureSkyRigTest::Flags)

bool FLureSkyFogTargetTest::RunTest(const FString& Parameters)
{
	using namespace LureSkyRigTest;
	// Reference values printed by levels/layout.py (ue_filmic, on_screen_to_scene) on 2026-09-24.
	TestNearlyEqual(TEXT("filmic(0.18) = 0.18 (mid grey kept)"), FLureTimeOfDayBlend::FilmicToneMap(0.18), 0.18, 1e-5);
	TestNearlyEqual(TEXT("filmic(0.01) = 0.001657"), FLureTimeOfDayBlend::FilmicToneMap(0.01), 0.001657, 1e-5);
	TestNearlyEqual(TEXT("filmic(1.0) = 0.723359"), FLureTimeOfDayBlend::FilmicToneMap(1.0), 0.723359, 1e-5);
	TestNearlyEqual(TEXT("filmic(4.0) = 0.944155"), FLureTimeOfDayBlend::FilmicToneMap(4.0), 0.944155, 1e-5);
	TestNearlyEqual(TEXT("inverse(filmic(0.7)) = 0.7"), FLureTimeOfDayBlend::FilmicInverse(FLureTimeOfDayBlend::FilmicToneMap(0.7)), 0.7, 1e-4);
	TestNearlyEqual(TEXT("exposure scale at EV 1 = 0.5"), FLureTimeOfDayBlend::ExposureScale(1.f), 0.5, 1e-9);

	auto Relative = [this](const FString& What, const FLinearColor& Actual, const FLinearColor& Expected)
	{
		TestTrue(FString::Printf(TEXT("%s: (%.6f, %.6f, %.6f) within 0.1%% of (%.6f, %.6f, %.6f)"), *What, Actual.R, Actual.G, Actual.B, Expected.R, Expected.G, Expected.B),
			FMath::IsNearlyEqual(Actual.R, Expected.R, Expected.R * 1e-3f) && FMath::IsNearlyEqual(Actual.G, Expected.G, Expected.G * 1e-3f)
			&& FMath::IsNearlyEqual(Actual.B, Expected.B, Expected.B * 1e-3f));
	};
	Relative(TEXT("day fog #8FD3F0 at EV 1"), FLureTimeOfDayBlend::OnScreenToScene(Hex(TEXT("#8FD3F0")), 1.f), FLinearColor(0.501691f, 1.527077f, 4.266760f));
	Relative(TEXT("night fog #1B2440 at EV -3"), FLureTimeOfDayBlend::OnScreenToScene(Hex(TEXT("#1B2440")), -3.f), FLinearColor(0.003825f, 0.005085f, 0.009750f));
	Relative(TEXT("dusk fog #FF9A5A at EV 0"), FLureTimeOfDayBlend::OnScreenToScene(Hex(TEXT("#FF9A5A")), 0.f), FLinearColor(8.959137f, 0.290145f, 0.121460f));

	FLureTimeOfDayLook Look;
	Look.FogColor = Hex(TEXT("#8FD3F0"));
	Look.ExposureEV100 = 1.f;
	Relative(TEXT("the look's fog scene colour uses its own EV"), Look.GetFogSceneColor(), FLinearColor(0.501691f, 1.527077f, 4.266760f));
	return true;
}

// ---- The sun's yaw: east at sunrise, west at sunset, steady with the hour; the moon opposite ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSkySunYawTest, "Project.Environment.Sky.Pure.SunYaw", LureSkyRigTest::Flags)

bool FLureSkySunYawTest::RunTest(const FString& Parameters)
{
	using namespace LureSkyRigTest;
	const FLureDayClock Clock = DefaultClock(*this);
	TestNearlyEqual(TEXT("06:00 (sunrise = Dawn midpoint): azimuth 0 (east)"), FLureTimeOfDayBlend::SunAzimuth(6.0, Clock), 0.0, 1e-6);
	TestNearlyEqual(TEXT("12:00: azimuth 90"), FLureTimeOfDayBlend::SunAzimuth(12.0, Clock), 90.0, 1e-6);
	TestNearlyEqual(TEXT("18:00 (sunset = Dusk midpoint): azimuth 180 (west)"), FLureTimeOfDayBlend::SunAzimuth(18.0, Clock), 180.0, 1e-6);
	TestNearlyEqual(TEXT("00:00: azimuth 270"), FLureTimeOfDayBlend::SunAzimuth(0.0, Clock), 270.0, 1e-6);
	for (double Hour = 6.0; Hour < 29.5; Hour += 1.0)
	{
		const double Step = FMath::Fmod(FLureTimeOfDayBlend::SunAzimuth(Hour + 1.0, Clock) - FLureTimeOfDayBlend::SunAzimuth(Hour, Clock) + 360.0, 360.0);
		TestNearlyEqual(FString::Printf(TEXT("steady: %02d:00 -> +1 h turns 15 deg"), static_cast<int32>(Hour) % 24), Step, 15.0, 1e-6);
	}

	// The light travels away from the sun: at sunrise (sun in the east, level +X) it heads west (-X).
	auto TestHeading = [this](const FString& What, const FRotator& Rotation, const FVector2D& Expected)
	{
		const FVector2D H = Heading(Rotation);
		TestTrue(FString::Printf(TEXT("%s: heading (%.3f, %.3f) ~ (%.3f, %.3f)"), *What, H.X, H.Y, Expected.X, Expected.Y), H.Equals(Expected, 1e-3));
	};
	TestHeading(TEXT("06:00 sun light heads west (-X)"), FLureTimeOfDayBlend::SunRotation(6.0, 8.f, Clock, 0.f), FVector2D(-1.0, 0.0));
	TestHeading(TEXT("12:00 sun light heads -Y (sun at +Y)"), FLureTimeOfDayBlend::SunRotation(12.0, 50.f, Clock, 0.f), FVector2D(0.0, -1.0));
	TestHeading(TEXT("18:00 sun light heads east (+X; sun sets in the west)"), FLureTimeOfDayBlend::SunRotation(18.0, 4.f, Clock, 0.f), FVector2D(1.0, 0.0));
	TestHeading(TEXT("EastYaw 90: 06:00 heads -Y"), FLureTimeOfDayBlend::SunRotation(6.0, 8.f, Clock, 90.f), FVector2D(0.0, -1.0));
	TestTrue(TEXT("elevation 50: the sun light points down"), FLureTimeOfDayBlend::SunRotation(12.0, 50.f, Clock, 0.f).Vector().Z < -0.7);
	TestNearlyEqual(TEXT("... at 50 deg"), FLureTimeOfDayBlend::SunRotation(12.0, 50.f, Clock, 0.f).Pitch, -50.0, 1e-4);

	const FVector Sun = FLureTimeOfDayBlend::SunRotation(21.0, -30.f, Clock, 0.f).Vector();
	const FVector Moon = FLureTimeOfDayBlend::MoonRotation(21.0, -30.f, Clock, 0.f).Vector();
	TestTrue(TEXT("the moon light is exactly opposite the sun light"), Moon.Equals(-Sun, 1e-4));
	TestTrue(TEXT("at night (sun below) the moon shines down"), Moon.Z < 0.0);
	return true;
}

// ---- AC8 + the rig in a world: night lights 0 at 12:00 and full at 22:00, every target follows the clock ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSkyWorldTest, "Project.Environment.Sky.World.RigAppliesLookAndNightLights", LureSkyRigTest::Flags)

bool FLureSkyWorldTest::RunTest(const FString& Parameters)
{
	using namespace LureSkyRigTest;
	const TStrongObjectPtr<UDataTable> Table(LoadTimeOfDay(*this));
	FSkyWorld W;
	if (!Table.IsValid() || !W.Create(*this))
	{
		return false;
	}
	ADirectionalLight* Sun = W.Spawn<ADirectionalLight>();
	ADirectionalLight* Moon = W.Spawn<ADirectionalLight>(LureSkyTags::Moon);
	ASkyAtmosphere* Atmosphere = W.Spawn<ASkyAtmosphere>();
	ASkyLight* Sky = W.Spawn<ASkyLight>();
	AExponentialHeightFog* Fog = W.Spawn<AExponentialHeightFog>();
	APostProcessVolume* Post = W.Spawn<APostProcessVolume>();
	Post->bUnbound = true;
	APointLight* Lantern = W.SpawnPointLight(8.f, LureSkyTags::NightLight);
	APointLight* Beacon = W.SpawnPointLight(5.f, LureSkyTags::NightLight);
	APointLight* CaveFill = W.SpawnPointLight(3.f, NAME_None);
	W.Clock->SetHour(12.f);

	ALureSkyRig* Rig = W.SpawnRig(Table.Get());
	if (!TestNotNull(TEXT("the rig spawns"), Rig))
	{
		return false;
	}
	TestTrue(TEXT("fallback lookup: the sun is the untagged directional light"), Rig->SunLight == Sun);
	TestTrue(TEXT("... the moon is the Lure.Moon one"), Rig->MoonLight == Moon);
	TestTrue(TEXT("... sky atmosphere, sky light, fog and the unbound volume found"), Rig->SkyAtmosphere == Atmosphere && Rig->SkyLight == Sky
		&& Rig->HeightFog == Fog && Rig->PostProcessVolume == Post);
	TestEqual(TEXT("two Lure.NightLight lights gathered"), Rig->GetNightLightCount(), 2);
	TestTrue(TEXT("the sky light uses real-time capture"), Sky->GetLightComponent()->bRealTimeCapture);
	TestTrue(TEXT("BeginPlay applied the look at once"), Rig->GetApplyCount() >= 1 && FMath::IsNearlyEqual(Rig->GetAppliedHour(), 12.0, 1e-3));

	// 12:00 = Tropical_Day: night lights off, the untagged light untouched, the day look everywhere.
	const FLureDayClock& Clock = W.Clock->GetClock();
	TestNearlyEqual(TEXT("AC8 12:00: the lantern is at 0"), Lantern->GetLightComponent()->Intensity, 0.f);
	TestNearlyEqual(TEXT("AC8 12:00: the Beacon is at 0"), Beacon->GetLightComponent()->Intensity, 0.f);
	TestFalse(TEXT("... and hidden"), Lantern->GetLightComponent()->IsVisible());
	TestNearlyEqual(TEXT("an untagged light keeps its intensity"), CaveFill->GetLightComponent()->Intensity, 3.f);
	TestNearlyEqual(TEXT("12:00 sun 10 lux"), Sun->GetLightComponent()->Intensity, 10.f);
	TestTrue(TEXT("12:00 sun rotation follows the yaw rule"), Sun->GetActorRotation().Equals(FLureTimeOfDayBlend::SunRotation(12.0, 50.f, Clock, 0.f), 0.01f));
	TestNearlyEqual(TEXT("12:00 moon off"), Moon->GetLightComponent()->Intensity, 0.f);
	TestTrue(TEXT("the moon is opposite the sun"), Moon->GetActorRotation().Vector().Equals(-Sun->GetActorRotation().Vector(), 1e-3));
	TestNearlyEqual(TEXT("12:00 sky light 0.35"), Sky->GetLightComponent()->Intensity, 0.35f);
	TestColor(*this, TEXT("12:00 sky luminance factor"), Atmosphere->GetComponent()->SkyLuminanceFactor, FLinearColor(2.35f, 2.75f, 3.1f));
	TestNearlyEqual(TEXT("12:00 fog density 0.05"), Fog->GetComponent()->FogDensity, 0.05f);
	TestColor(*this, TEXT("12:00 fog = the #8FD3F0 on-screen target at EV 1"), Fog->GetComponent()->FogInscatteringLuminance,
		FLureTimeOfDayBlend::OnScreenToScene(Hex(TEXT("#8FD3F0")), 1.f), 2e-3f);
	TestTrue(TEXT("exposure manual, bias -1 (EV 1)"), Post->Settings.AutoExposureMethod == EAutoExposureMethod::AEM_Manual && Post->Settings.bOverride_AutoExposureMethod
		&& FMath::IsNearlyEqual(Post->Settings.AutoExposureBias, -1.f, 1e-4f));

	// Lure.Time.Set 22 (SetHour on the server): the rig applies at once, without waiting for its tick.
	const int32 AppliesBefore = Rig->GetApplyCount();
	W.Clock->SetHour(22.f);
	TestTrue(TEXT("a clock jump applies at once"), Rig->GetApplyCount() > AppliesBefore && FMath::IsNearlyEqual(Rig->GetAppliedHour(), 22.0, 1e-3));
	TestNearlyEqual(TEXT("AC8 22:00: the lantern at full (8)"), Lantern->GetLightComponent()->Intensity, 8.f);
	TestNearlyEqual(TEXT("AC8 22:00: the Beacon at full (5)"), Beacon->GetLightComponent()->Intensity, 5.f);
	TestTrue(TEXT("... and visible"), Lantern->GetLightComponent()->IsVisible());
	TestNearlyEqual(TEXT("22:00 exposure bias = -(0 + (-3 - 0) x 2/3) = 2"), Post->Settings.AutoExposureBias, 2.f, 1e-3f);
	TestNearlyEqual(TEXT("22:00 sun 1 lux (3 -> 0, 2/3 of the way)"), Sun->GetLightComponent()->Intensity, 1.f, 1e-3f);
	TestNearlyEqual(TEXT("22:00 moon 0.11 lux (0.03 -> 0.15)"), Moon->GetLightComponent()->Intensity, 0.03f + (0.15f - 0.03f) * (2.f / 3.f), 1e-3f);

	// A running clock: the rig re-applies on its own interval.
	W.Clock->SetTimeScale(1.f);
	const int32 AppliesRunning = Rig->GetApplyCount();
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		W.Wrapper.TickTestWorld(0.1f);
	}
	TestTrue(FString::Printf(TEXT("a running clock re-applies (%d applies in 1 s)"), Rig->GetApplyCount() - AppliesRunning), Rig->GetApplyCount() - AppliesRunning >= 5);
	TestTrue(TEXT("... at the clock's hour"), FMath::IsNearlyEqual(Rig->GetAppliedHour(), static_cast<double>(W.Clock->GetHour()), 0.01));

	// A frozen clock costs nothing.
	W.Clock->SetTimeScale(0.f);
	const int32 AppliesFrozen = Rig->GetApplyCount();
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		W.Wrapper.TickTestWorld(0.1f);
	}
	TestEqual(TEXT("a frozen clock does not re-apply"), Rig->GetApplyCount(), AppliesFrozen);

	// Budget (spec §3.5: game thread <= 0.1 ms): one apply, averaged over 200.
	const double Start = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < 200; ++Index)
	{
		Rig->ApplyNow();
	}
	const double AverageMs = (FPlatformTime::Seconds() - Start) * 1000.0 / 200.0;
	AddInfo(FString::Printf(TEXT("sky rig apply: %.4f ms (budget 0.1 ms)"), AverageMs));
	TestTrue(FString::Printf(TEXT("one apply costs <= 0.1 ms (%.4f ms)"), AverageMs), AverageMs <= 0.1);
	return true;
}

// ---- Fallbacks: missing targets are skipped with one warning each; no rows = the level keeps its look ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureSkyMissingTest, "Project.Environment.Sky.World.MissingTargetsAndRows", LureSkyRigTest::Flags)

bool FLureSkyMissingTest::RunTest(const FString& Parameters)
{
	using namespace LureSkyRigTest;
	const TStrongObjectPtr<UDataTable> Table(LoadTimeOfDay(*this));
	FSkyWorld W;
	if (!Table.IsValid() || !W.Create(*this))
	{
		return false;
	}
	// Only a sun (tagged) and a night light: no moon, sky, sky light, fog or post-process volume.
	ADirectionalLight* Decoy = W.Spawn<ADirectionalLight>();
	ADirectionalLight* Sun = W.Spawn<ADirectionalLight>(LureSkyTags::Sun);
	APointLight* Lantern = W.SpawnPointLight(8.f, LureSkyTags::NightLight);
	W.Clock->SetHour(12.f);

	AddExpectedMessagePlain(TEXT("no SkyAtmosphere"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("no SkyLight"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("no ExponentialHeightFog"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("no unbound PostProcessVolume"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	ALureSkyRig* Rig = W.SpawnRig(Table.Get());
	if (!TestNotNull(TEXT("the rig spawns"), Rig))
	{
		return false;
	}
	TestTrue(TEXT("the Lure.Sun tag wins over an untagged directional light"), Rig->SunLight == Sun && Rig->SunLight != Decoy);
	TestTrue(TEXT("no moon without the Lure.Moon tag"), Rig->MoonLight == nullptr);
	W.Clock->SetHour(22.f);
	TestNearlyEqual(TEXT("the sun still follows (1 lux at 22:00)"), Sun->GetLightComponent()->Intensity, 1.f, 1e-3f);
	TestNearlyEqual(TEXT("the night light still follows (full at 22:00)"), Lantern->GetLightComponent()->Intensity, 8.f);
	TestNearlyEqual(TEXT("the untagged decoy keeps its intensity"), Decoy->GetLightComponent()->Intensity, W.Spawn<ADirectionalLight>()->GetLightComponent()->Intensity);
	Rig->Destroy();

	// No rows for the region (unknown region, or no table): nothing is applied and nothing breaks.
	ALureSkyRig* Empty = W.World->SpawnActorDeferred<ALureSkyRig>(ALureSkyRig::StaticClass(), FTransform::Identity);
	Empty->Region = TEXT("Nowhere");
	Empty->SetTimeOfDayTable(Table.Get());
	Empty->FinishSpawning(FTransform::Identity);
	Lantern->GetLightComponent()->SetIntensity(6.f);
	W.Clock->SetHour(12.f);
	W.Wrapper.TickTestWorld(0.2f);
	TestEqual(TEXT("no rows: no apply"), Empty->GetApplyCount(), 0);
	TestNearlyEqual(TEXT("no rows: the lights keep their look"), Lantern->GetLightComponent()->Intensity, 6.f);
	TestTrue(TEXT("no anchors"), Empty->GetAnchors().IsEmpty());
	return true;
}

#endif

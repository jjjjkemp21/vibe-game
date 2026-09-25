// Lure T-068a (unreal-engineer): the day/night clock, docs/specs/day-night-water.md §3.1 and §5 AC1-AC6.
// Tables come from data/tables/DT_DayCycle.json (never the binary asset). No wall clock: time is driven through the pure
// clock (FLureDayClock with given times) or by ticking transient worlds with fixed steps. Project.Environment.Clock.*

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING && WITH_EDITOR

#include "Dev/LureTimeDevCommands.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Environment/LureDayClock.h"
#include "Environment/LureDayClockComponent.h"
#include "Environment/LureDayNightSettings.h"
#include "Game/LureGameState.h"
#include "Game/LureHUD.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/Environment/DayClockTestListener.h"
#include "Tests/Fishing/FishingWaterTestUtils.h"
#include "Tests/NetTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

namespace LureDayClockTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr double GameMinute = 1.0 / 60.0;
	constexpr float NetDt = 1.f / 60.f;

	UDataTable* LoadDayCycle(FAutomationTestBase& Test)
	{
		FString Json;
		if (!Test.TestTrue(TEXT("data/tables/DT_DayCycle.json loads"),
			FFileHelper::LoadFileToString(Json, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_DayCycle.json")))))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureDayCycleRow::StaticStruct();
		for (const FString& Problem : Table->CreateTableFromJSONString(Json))
		{
			Test.AddError(TEXT("DT_DayCycle.json import problem: ") + Problem);
		}
		return Table;
	}

	/** The shipped Default row (from the JSON source). */
	bool LoadDefaultRow(FAutomationTestBase& Test, FLureDayCycleRow& OutRow)
	{
		const TStrongObjectPtr<UDataTable> Table(LoadDayCycle(Test));
		const FLureDayCycleRow* Row = Table.IsValid() ? Table->FindRow<FLureDayCycleRow>(TEXT("Default"), TEXT("DayClockTest"), false) : nullptr;
		if (!Test.TestNotNull(TEXT("DT_DayCycle has a Default row"), Row))
		{
			return false;
		}
		OutRow = *Row;
		return true;
	}

	/** |A - B| in hours across midnight. */
	double HourDiff(double A, double B)
	{
		return FMath::Abs(FLureDayClock::NormalizeHour(A - B + 12.0) - 12.0);
	}

	/** |A - B| in real seconds of the day, across the day's wrap. */
	double RealDiff(const FLureDayClock& Clock, double HourA, double HourB)
	{
		const double Day = Clock.GetDaySeconds();
		const double D = FMath::Abs(Clock.RealSecondsFromDawn(HourA) - Clock.RealSecondsFromDawn(HourB));
		return FMath::Min(D, Day - D);
	}

	const TCHAR* Name(ELureDayPhase Phase)
	{
		switch (Phase)
		{
		case ELureDayPhase::Dawn: return TEXT("Dawn");
		case ELureDayPhase::Day: return TEXT("Day");
		case ELureDayPhase::Dusk: return TEXT("Dusk");
		case ELureDayPhase::Night: return TEXT("Night");
		}
		return TEXT("?");
	}

	bool MatchesHudFormat(const FString& Text)
	{
		const FRegexPattern Pattern(TEXT("^([01][0-9]|2[0-3]):[0-5][0-9] (Dawn|Day|Dusk|Night)$"));
		FRegexMatcher Matcher(Pattern, Text);
		return Matcher.FindNext();
	}

	/** Every property but DevComment equal. */
	TArray<FString> DiffRows(const FLureDayCycleRow& A, const FLureDayCycleRow& B)
	{
		TArray<FString> Fields;
		for (TFieldIterator<FProperty> It(FLureDayCycleRow::StaticStruct()); It; ++It)
		{
			if (It->GetFName() == GET_MEMBER_NAME_CHECKED(FLureDayCycleRow, DevComment))
			{
				continue;
			}
			if (!It->Identical(It->ContainerPtrToValuePtr<void>(&A), It->ContainerPtrToValuePtr<void>(&B), PPF_None))
			{
				Fields.Add(It->GetName());
			}
		}
		return Fields;
	}

	/** Seconds of real time between phase changes found by querying the clock every Step seconds from Start for Duration. */
	struct FPhaseChange
	{
		double Time = 0.0;
		ELureDayPhase Phase = ELureDayPhase::Day;
	};

	TArray<FPhaseChange> SamplePhaseChanges(const FLureDayClock& Clock, const FLureDayClockState& State, double Start, double Duration, double Step)
	{
		TArray<FPhaseChange> Changes;
		ELureDayPhase Last = Clock.GetPhase(Clock.HourAt(State, Start));
		for (double Time = Start + Step; Time <= Start + Duration + 1.0e-9; Time += Step)
		{
			const ELureDayPhase Phase = Clock.GetPhase(Clock.HourAt(State, Time));
			if (Phase != Last)
			{
				Changes.Add({ Time, Phase });
				Last = Phase;
			}
		}
		return Changes;
	}
}

// ---- Data ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDayClockDataTest, "Project.Environment.Clock.Data.DayCycleRows", LureDayClockTest::Flags)

bool FLureDayClockDataTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDataTable> Table(LureDayClockTest::LoadDayCycle(*this));
	if (!Table.IsValid() || !TestTrue(TEXT("DT_DayCycle has rows"), Table->GetRowMap().Num() > 0))
	{
		return false;
	}
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FLureDayCycleRow& Row = *reinterpret_cast<const FLureDayCycleRow*>(Pair.Value);
		TArray<FString> Problems;
		TestTrue(FString::Printf(TEXT("row %s is valid (%s)"), *Pair.Key.ToString(), *FString::Join(Problems, TEXT("; "))), FLureDayClock::Validate(Row, &Problems));
		for (const FString& Problem : Problems)
		{
			AddError(FString::Printf(TEXT("row %s: %s"), *Pair.Key.ToString(), *Problem));
		}
	}

	FLureDayCycleRow Default;
	if (!LureDayClockTest::LoadDefaultRow(*this, Default))
	{
		return false;
	}
	const TArray<FString> Diff = LureDayClockTest::DiffRows(Default, FLureDayClock::GetFallbackRow());
	TestTrue(FString::Printf(TEXT("the built-in fallback row equals the Default row (differs in: %s)"), *FString::Join(Diff, TEXT(", "))), Diff.IsEmpty());
	TestNearlyEqual(TEXT("Default: the phase minutes sum to DayLengthMinutes (20)"),
		Default.DawnMinutes + Default.DayMinutes + Default.DuskMinutes + Default.NightMinutes, Default.DayLengthMinutes, 1.0e-4f);
	TestNearlyEqual(TEXT("Default: spec §4 values (20 min, start 8, 5/7/17/19, 2/9/2/7)"),
		Default.DayLengthMinutes + Default.StartHour + Default.DawnStart + Default.DayStart + Default.DuskStart + Default.NightStart
		+ Default.DawnMinutes + Default.DayMinutes + Default.DuskMinutes + Default.NightMinutes, 20.f + 8.f + 5.f + 7.f + 17.f + 19.f + 2.f + 9.f + 2.f + 7.f, 1.0e-4f);

	// Validation rejects broken rows, and a clock over one runs the fallback row.
	struct FBad { const TCHAR* What; TFunction<void(FLureDayCycleRow&)> Break; };
	const FBad Bad[] = {
		{ TEXT("DayStart before DawnStart"), [](FLureDayCycleRow& R) { R.DayStart = 4.f; } },
		{ TEXT("DuskStart equal to DayStart"), [](FLureDayCycleRow& R) { R.DuskStart = R.DayStart; } },
		{ TEXT("NightStart 24"), [](FLureDayCycleRow& R) { R.NightStart = 24.f; } },
		{ TEXT("DawnStart negative"), [](FLureDayCycleRow& R) { R.DawnStart = -1.f; } },
		{ TEXT("NightMinutes 0"), [](FLureDayCycleRow& R) { R.NightMinutes = 0.f; } },
		{ TEXT("DawnMinutes negative"), [](FLureDayCycleRow& R) { R.DawnMinutes = -2.f; } },
		{ TEXT("DayLengthMinutes 0"), [](FLureDayCycleRow& R) { R.DayLengthMinutes = 0.f; } },
		{ TEXT("StartHour 25"), [](FLureDayCycleRow& R) { R.StartHour = 25.f; } },
		{ TEXT("StartTimeScale negative"), [](FLureDayCycleRow& R) { R.StartTimeScale = -1.f; } },
		{ TEXT("StartTimeScale NaN"), [](FLureDayCycleRow& R) { R.StartTimeScale = std::numeric_limits<float>::quiet_NaN(); } },
	};
	for (const FBad& Case : Bad)
	{
		FLureDayCycleRow Row = Default;
		Row.DayLengthMinutes = 13.f; // tells a fallback clock (20) apart
		Case.Break(Row);
		TArray<FString> Problems;
		TestFalse(FString::Printf(TEXT("invalid: %s"), Case.What), FLureDayClock::Validate(Row, &Problems));
		TestTrue(FString::Printf(TEXT("... with a reason (%s)"), Case.What), Problems.Num() > 0);
		const FLureDayClock Clock(Row);
		TestTrue(FString::Printf(TEXT("... and the clock runs the fallback row (%s)"), Case.What), Clock.IsFallback() && FMath::IsNearlyEqual(Clock.GetDaySeconds(), 1200.0));
	}

	// The settings resolver: a missing table or row gives the fallback row and a reason.
	FLureDayCycleRow Resolved;
	TArray<FString> Problems;
	TestTrue(TEXT("ResolveRow finds Default"), ULureDayNightSettings::ResolveRow(Table.Get(), TEXT("Default"), Resolved, Problems) && Problems.IsEmpty());
	TestTrue(TEXT("... the same row"), LureDayClockTest::DiffRows(Resolved, Default).IsEmpty());
	Problems.Reset();
	TestFalse(TEXT("ResolveRow: no table"), ULureDayNightSettings::ResolveRow(nullptr, TEXT("Default"), Resolved, Problems));
	TestTrue(TEXT("... reason given, fallback row"), Problems.Num() == 1 && LureDayClockTest::DiffRows(Resolved, FLureDayClock::GetFallbackRow()).IsEmpty());
	Problems.Reset();
	TestFalse(TEXT("ResolveRow: missing row"), ULureDayNightSettings::ResolveRow(Table.Get(), TEXT("NoSuchRow"), Resolved, Problems));
	TestTrue(TEXT("... reason names the row"), Problems.Num() == 1 && Problems[0].Contains(TEXT("NoSuchRow")));
	TestEqual(TEXT("settings default table path"), GetDefault<ULureDayNightSettings>()->DayCycleTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_DayCycle.DT_DayCycle")));
	TestEqual(TEXT("settings default row"), GetDefault<ULureDayNightSettings>()->DayCycleRow, FName(TEXT("Default")));
	return true;
}

// ---- AC1: the pure clock ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDayClockPureTimingTest, "Project.Environment.Clock.Pure.DayAndPhaseLengths", LureDayClockTest::Flags)

bool FLureDayClockPureTimingTest::RunTest(const FString& Parameters)
{
	using LureDayClockTest::HourDiff;
	FLureDayCycleRow Row;
	if (!LureDayClockTest::LoadDefaultRow(*this, Row))
	{
		return false;
	}
	const FLureDayClock Clock(Row);
	TestFalse(TEXT("the Default row drives the clock"), Clock.IsFallback());
	TestNearlyEqual(TEXT("a day is 1200 real seconds"), Clock.GetDaySeconds(), 1200.0, 1.0e-6);

	// Each phase lasts its RealMinutes (closed form).
	const ELureDayPhase Phases[] = { ELureDayPhase::Dawn, ELureDayPhase::Day, ELureDayPhase::Dusk, ELureDayPhase::Night };
	const float Minutes[] = { Row.DawnMinutes, Row.DayMinutes, Row.DuskMinutes, Row.NightMinutes };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const ELureDayPhase Phase = Phases[Index];
		const double Start = Clock.GetPhaseStartHour(Phase);
		const double End = Clock.GetPhaseStartHour(Phases[(Index + 1) % 4]);
		double Seconds = Clock.RealSecondsFromDawn(End) - Clock.RealSecondsFromDawn(Start);
		Seconds = Seconds <= 0.0 ? Seconds + Clock.GetDaySeconds() : Seconds;
		TestNearlyEqual(FString::Printf(TEXT("%s lasts %g real minutes"), LureDayClockTest::Name(Phase), Minutes[Index]), Seconds, Minutes[Index] * 60.0, 1.0);
		TestNearlyEqual(FString::Printf(TEXT("%s: GetPhaseSeconds"), LureDayClockTest::Name(Phase)), Clock.GetPhaseSeconds(Phase), Minutes[Index] * 60.0, 1.0e-6);
	}
	TestNearlyEqual(TEXT("Dawn rate: 2 game hours in 120 s"), Clock.GetRate(ELureDayPhase::Dawn), 2.0 / 120.0, 1.0e-9);
	TestNearlyEqual(TEXT("Night rate: 10 game hours in 420 s"), Clock.GetRate(ELureDayPhase::Night), 10.0 / 420.0, 1.0e-9);

	// The same, sampled like a running game: the state starts at 08:00 at server time 100 s, queried every 0.25 s for a day.
	FLureDayClockState State;
	State.ReferenceHour = Row.StartHour;
	State.ReferenceServerTime = 100.0;
	State.TimeScale = 1.f;
	TestNearlyEqual(TEXT("08:00 at the reference time"), Clock.HourAt(State, 100.0), 8.0, 1.0e-9);
	TestNearlyEqual(TEXT("an earlier server time counts as the reference time"), Clock.HourAt(State, 50.0), 8.0, 1.0e-9);
	const TArray<LureDayClockTest::FPhaseChange> Changes = LureDayClockTest::SamplePhaseChanges(Clock, State, 100.0, 1200.0, 0.25);
	if (TestEqual(TEXT("a day of sampling crosses 4 phase boundaries"), Changes.Num(), 4))
	{
		TestTrue(TEXT("... Dusk, Night, Dawn, Day in order"), Changes[0].Phase == ELureDayPhase::Dusk && Changes[1].Phase == ELureDayPhase::Night
			&& Changes[2].Phase == ELureDayPhase::Dawn && Changes[3].Phase == ELureDayPhase::Day);
		TestNearlyEqual(TEXT("Dusk at 17:00 = 486 s after 08:00 (9 of 10 day hours of 540 s)"), Changes[0].Time - 100.0, 486.0, 1.0);
		TestNearlyEqual(TEXT("Dusk lasts 120 s"), Changes[1].Time - Changes[0].Time, 120.0, 1.0);
		TestNearlyEqual(TEXT("Night lasts 420 s"), Changes[2].Time - Changes[1].Time, 420.0, 1.0);
		TestNearlyEqual(TEXT("Dawn lasts 120 s"), Changes[3].Time - Changes[2].Time, 120.0, 1.0);
	}
	TestTrue(TEXT("24 game hours in 1200 s (+-1 s)"), LureDayClockTest::RealDiff(Clock, Clock.HourAt(State, 1300.0), 8.0) <= 1.0);
	TestTrue(TEXT("... not yet after 1190 s"), LureDayClockTest::RealDiff(Clock, Clock.HourAt(State, 1290.0), 8.0) >= 9.0);
	TestTrue(TEXT("2 days in 2400 s"), HourDiff(Clock.HourAt(State, 2500.0), 8.0) < 1.0e-6);

	// Monotonic and continuous: small steps never move backwards or jump.
	double Previous = Clock.RealSecondsFromDawn(Clock.HourAt(State, 100.0));
	bool bSmooth = true;
	for (double Time = 100.1; Time < 1300.0; Time += 0.1)
	{
		const double Now = Clock.RealSecondsFromDawn(Clock.HourAt(State, Time));
		double Step = Now - Previous;
		Step = Step < -600.0 ? Step + Clock.GetDaySeconds() : Step;
		bSmooth &= FMath::IsNearlyEqual(Step, 0.1, 1.0e-6);
		Previous = Now;
	}
	TestTrue(TEXT("0.1 s of real time is always 0.1 s of the day (continuous across phases and midnight)"), bSmooth);

	// Round trip and wrap.
	bool bRoundTrip = true;
	for (double Hour = 0.0; Hour < 24.0; Hour += 0.07)
	{
		bRoundTrip &= HourDiff(Clock.HourAtRealSecondsFromDawn(Clock.RealSecondsFromDawn(Hour)), Hour) < 1.0e-9;
	}
	TestTrue(TEXT("hour -> real seconds -> hour round-trips"), bRoundTrip);
	TestNearlyEqual(TEXT("negative real seconds wrap"), Clock.HourAtRealSecondsFromDawn(-60.0), Clock.HourAtRealSecondsFromDawn(1140.0), 1.0e-9);

	// Phases, alpha.
	struct FCase { double Hour; ELureDayPhase Phase; };
	const FCase Cases[] = { { 4.99, ELureDayPhase::Night }, { 5.0, ELureDayPhase::Dawn }, { 6.99, ELureDayPhase::Dawn }, { 7.0, ELureDayPhase::Day },
		{ 16.99, ELureDayPhase::Day }, { 17.0, ELureDayPhase::Dusk }, { 18.99, ELureDayPhase::Dusk }, { 19.0, ELureDayPhase::Night }, { 0.0, ELureDayPhase::Night },
		{ 23.99, ELureDayPhase::Night }, { 24.0, ELureDayPhase::Night }, { -1.0, ELureDayPhase::Night } };
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%.2f h is %s"), Case.Hour, LureDayClockTest::Name(Case.Phase)), LureDayClockTest::Name(Clock.GetPhase(Case.Hour)), LureDayClockTest::Name(Case.Phase));
	}
	TestNearlyEqual(TEXT("alpha 06:00 = 0.5 of Dawn"), Clock.GetPhaseAlpha(6.0), 0.5, 1.0e-9);
	TestNearlyEqual(TEXT("alpha 00:00 = 0.5 of Night (19 -> 5)"), Clock.GetPhaseAlpha(0.0), 0.5, 1.0e-9);
	TestNearlyEqual(TEXT("alpha 17:00 = 0"), Clock.GetPhaseAlpha(17.0), 0.0, 1.0e-9);

	// Time scale.
	FLureDayClockState Fast = State;
	Fast.TimeScale = 2.f;
	TestTrue(TEXT("scale 2: 60 s of server time = 120 s of the day"), LureDayClockTest::RealDiff(Clock, Clock.HourAt(Fast, 160.0), Clock.HourAt(State, 220.0)) < 1.0e-6);
	FLureDayClockState Frozen = State;
	Frozen.TimeScale = 0.f;
	TestNearlyEqual(TEXT("scale 0: frozen"), Clock.HourAt(Frozen, 5000.0), 8.0, 1.0e-9);
	TestNearlyEqual(TEXT("next phase from 08:00 in 486 s"), Clock.SecondsToNextPhase(State, 100.0), 486.0, 1.0e-6);
	TestNearlyEqual(TEXT("... 243 s at scale 2"), Clock.SecondsToNextPhase(Fast, 100.0), 243.0, 1.0e-6);
	TestTrue(TEXT("... never while frozen"), Clock.SecondsToNextPhase(Frozen, 100.0) < 0.0);

	// The HUD text format (AC6, pure part).
	TestEqual(TEXT("06:40 Dawn"), Clock.FormatClock(6.0 + 40.0 / 60.0), FString(TEXT("06:40 Dawn")));
	TestEqual(TEXT("float 06:40 still 06:40"), Clock.FormatClock(static_cast<float>(6.0 + 40.0 / 60.0)), FString(TEXT("06:40 Dawn")));
	TestEqual(TEXT("21:00 Night"), Clock.FormatClock(21.0), FString(TEXT("21:00 Night")));
	TestEqual(TEXT("00:00"), FLureDayClock::FormatHour(0.0), FString(TEXT("00:00")));
	TestEqual(TEXT("23:59"), FLureDayClock::FormatHour(23.99), FString(TEXT("23:59")));
	TestEqual(TEXT("24 wraps to 00:00"), FLureDayClock::FormatHour(24.0), FString(TEXT("00:00")));
	TestEqual(TEXT("-1 wraps to 23:00"), FLureDayClock::FormatHour(-1.0), FString(TEXT("23:00")));
	ELureDayPhase Parsed = ELureDayPhase::Day;
	TestTrue(TEXT("ParsePhase 'dusk'"), FLureDayClock::ParsePhase(TEXT("dusk"), Parsed) && Parsed == ELureDayPhase::Dusk);
	TestFalse(TEXT("ParsePhase 'noon' fails"), FLureDayClock::ParsePhase(TEXT("noon"), Parsed));
	return true;
}

// ---- AC2: a new DT_DayCycle value changes the day with no code change ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDayClockDataDrivenTest, "Project.Environment.Clock.Pure.DayLengthFromData", LureDayClockTest::Flags)

bool FLureDayClockDataDrivenTest::RunTest(const FString& Parameters)
{
	FLureDayCycleRow Row;
	if (!LureDayClockTest::LoadDefaultRow(*this, Row))
	{
		return false;
	}
	const FLureDayClock Normal(Row);
	FLureDayCycleRow HalfRow = Row;
	HalfRow.DayLengthMinutes = 10.f;
	const FLureDayClock Half(HalfRow);
	TestFalse(TEXT("DayLengthMinutes 10 is a valid row"), Half.IsFallback());
	TestNearlyEqual(TEXT("the day is 600 s"), Half.GetDaySeconds(), 600.0, 1.0e-6);
	for (const ELureDayPhase Phase : { ELureDayPhase::Dawn, ELureDayPhase::Day, ELureDayPhase::Dusk, ELureDayPhase::Night })
	{
		TestNearlyEqual(FString::Printf(TEXT("%s is halved"), LureDayClockTest::Name(Phase)), Half.GetPhaseSeconds(Phase), Normal.GetPhaseSeconds(Phase) * 0.5, 1.0e-6);
		TestNearlyEqual(FString::Printf(TEXT("%s keeps its hours"), LureDayClockTest::Name(Phase)), Half.GetPhaseHours(Phase), Normal.GetPhaseHours(Phase), 1.0e-9);
	}
	FLureDayClockState State;
	State.ReferenceHour = HalfRow.StartHour;
	bool bSame = true;
	for (double Seconds = 0.0; Seconds <= 1200.0; Seconds += 7.3)
	{
		bSame &= LureDayClockTest::HourDiff(Half.HourAt(State, Seconds), Normal.HourAt(State, Seconds * 2.0)) < 1.0e-6;
	}
	TestTrue(TEXT("the half day shows at t what the normal day shows at 2t"), bSame);
	TestTrue(TEXT("24 h in 600 s"), LureDayClockTest::RealDiff(Half, Half.HourAt(State, 600.0), HalfRow.StartHour) <= 0.5);

	// The phase minutes are weights: equal minutes split any day length evenly.
	FLureDayCycleRow Even = Row;
	Even.DawnMinutes = Even.DayMinutes = Even.DuskMinutes = Even.NightMinutes = 1.f;
	const FLureDayClock EvenClock(Even);
	TestNearlyEqual(TEXT("equal weights: Dawn is a quarter of 20 min"), EvenClock.GetPhaseSeconds(ELureDayPhase::Dawn), 300.0, 1.0e-6);
	TestNearlyEqual(TEXT("equal weights: Night is a quarter of 20 min"), EvenClock.GetPhaseSeconds(ELureDayPhase::Night), 300.0, 1.0e-6);

	// Other phase starts move the phases (a new region's row).
	FLureDayCycleRow Polar = Row;
	Polar.DawnStart = 3.f;
	Polar.DayStart = 4.f;
	Polar.DuskStart = 22.f;
	Polar.NightStart = 23.f;
	const FLureDayClock PolarClock(Polar);
	TestFalse(TEXT("another row is valid"), PolarClock.IsFallback());
	TestEqual(TEXT("... and 22:30 is Dusk there"), LureDayClockTest::Name(PolarClock.GetPhase(22.5)), TEXT("Dusk"));
	return true;
}

// ---- AC1 + AC6 through the component: the game state's clock in a game world ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDayClockWorldTest, "Project.Environment.Clock.World.PhaseEventsAndHud", LureDayClockTest::Flags)

bool FLureDayClockWorldTest::RunTest(const FString& Parameters)
{
	FLureDayCycleRow Row;
	if (!LureDayClockTest::LoadDefaultRow(*this, Row))
	{
		return false;
	}
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
	{
		Wrapper.ForwardErrorMessages(this);
		AddError(TEXT("the test world could not be created"));
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	const ALureGameState* GameState = World ? Cast<ALureGameState>(World->GetGameState()) : nullptr;
	if (!TestNotNull(TEXT("the game mode spawns ALureGameState"), GameState))
	{
		return false;
	}
	ULureDayClockComponent* Clock = ULureDayClockComponent::Get(World);
	if (!TestTrue(TEXT("ULureDayClockComponent::Get finds the game state's clock"), Clock && Clock == GameState->GetDayClock()))
	{
		return false;
	}
	TestTrue(TEXT("the clock component replicates"), Clock->GetIsReplicated());
	TestFalse(TEXT("the clock never ticks"), Clock->PrimaryComponentTick.bCanEverTick);
	Clock->SetDayCycleRow(Row);
	TestTrue(FString::Printf(TEXT("the session starts at StartHour 08:00 (%s)"), *Clock->GetClockText()), LureDayClockTest::HourDiff(Clock->GetHour(), 8.0) < LureDayClockTest::GameMinute);
	TestEqual(TEXT("... in the Day phase"), LureDayClockTest::Name(Clock->GetPhase()), TEXT("Day"));
	// Until T-068b's sky rig the shipped row starts frozen (StartTimeScale 0): a fresh world stays at 08:00.
	TestNearlyEqual(TEXT("... frozen (shipped StartTimeScale 0)"), Clock->GetTimeScale(), 0.f);
	for (int32 Tick = 0; Tick < 20; ++Tick)
	{
		Wrapper.TickTestWorld(0.5f);
	}
	TestEqual(TEXT("a fresh world is still at 08:00 Day 10 s later"), Clock->GetClockText(), FString(TEXT("08:00 Day")));
	TestTrue(TEXT("SetTimeScale 1 (run the day)"), Clock->SetTimeScale(1.f));

	const TStrongObjectPtr<ULureDayClockTestListener> Listener(NewObject<ULureDayClockTestListener>());
	Clock->OnPhaseChanged.AddDynamic(Listener.Get(), &ULureDayClockTestListener::OnPhaseChanged);

	// A whole day in 0.5 s steps: OnPhaseChanged fires at each boundary (+-1 s), and 24 h pass in 1200 s.
	const double Start = Clock->GetServerTime();
	TArray<double> EventTimes;
	while (Clock->GetServerTime() < Start + 1200.0 - 1.0e-6)
	{
		Wrapper.TickTestWorld(0.5f);
		while (EventTimes.Num() < Listener->Events.Num())
		{
			EventTimes.Add(Clock->GetServerTime() - Start);
		}
	}
	TestTrue(FString::Printf(TEXT("after 1200 s it is 08:00 again (%s)"), *Clock->GetClockText()),
		LureDayClockTest::RealDiff(Clock->GetClock(), Clock->GetHour(), 8.0) <= 1.0);
	if (TestEqual(TEXT("OnPhaseChanged fired 4 times in a day"), Listener->Events.Num(), 4))
	{
		const ELureDayPhase Expected[4][2] = { { ELureDayPhase::Dusk, ELureDayPhase::Day }, { ELureDayPhase::Night, ELureDayPhase::Dusk },
			{ ELureDayPhase::Dawn, ELureDayPhase::Night }, { ELureDayPhase::Day, ELureDayPhase::Dawn } };
		const double ExpectedTimes[4] = { 486.0, 606.0, 1026.0, 1146.0 };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			TestTrue(FString::Printf(TEXT("event %d: %s -> %s"), Index, LureDayClockTest::Name(Expected[Index][1]), LureDayClockTest::Name(Expected[Index][0])),
				Listener->Events[Index].Key == Expected[Index][0] && Listener->Events[Index].Value == Expected[Index][1]);
			TestNearlyEqual(FString::Printf(TEXT("event %d at %.0f s (+-1 s)"), Index, ExpectedTimes[Index]), EventTimes[Index], ExpectedTimes[Index], 1.0);
		}
	}

	// AC6: the HUD line is the clock's, HH:MM <Phase>.
	TestEqual(TEXT("the HUD text is the clock's text"), ALureHUD::GetClockText(World), Clock->GetClockText());
	TestTrue(FString::Printf(TEXT("the HUD text is 'HH:MM <Phase>' (%s)"), *ALureHUD::GetClockText(World)), LureDayClockTest::MatchesHudFormat(ALureHUD::GetClockText(World)));
	TestTrue(TEXT("SetTimeScale 0 (frozen)"), Clock->SetTimeScale(0.f));
	TestTrue(TEXT("SetHour 06:40"), Clock->SetHour(6.f + 40.f / 60.f));
	TestEqual(TEXT("HUD: 06:40 Dawn"), ALureHUD::GetClockText(World), FString(TEXT("06:40 Dawn")));
	TestTrue(TEXT("the jump fired Day -> Dawn"), Listener->Events.Num() == 5 && Listener->Events.Last().Key == ELureDayPhase::Dawn && Listener->Events.Last().Value == ELureDayPhase::Day);
	for (int32 Tick = 0; Tick < 20; ++Tick)
	{
		Wrapper.TickTestWorld(0.5f);
	}
	TestEqual(TEXT("frozen: still 06:40 Dawn 10 s later"), ALureHUD::GetClockText(World), FString(TEXT("06:40 Dawn")));
	TestTrue(TEXT("SetPhase Night"), Clock->SetPhase(ELureDayPhase::Night));
	TestEqual(TEXT("HUD: 19:00 Night"), ALureHUD::GetClockText(World), FString(TEXT("19:00 Night")));
	TestTrue(TEXT("SetHour 25 wraps to 01:00"), Clock->SetHour(25.f) && ALureHUD::GetClockText(World) == TEXT("01:00 Night"));
	TestFalse(TEXT("a negative scale is refused"), Clock->SetTimeScale(-1.f));
	TestTrue(TEXT("SetTimeScale 60"), Clock->SetTimeScale(60.f));
	const double Before = Clock->GetServerTime();
	Wrapper.TickTestWorld(0.25f);
	const double Elapsed = Clock->GetServerTime() - Before;
	TestTrue(FString::Printf(TEXT("scale 60: %.2f s of play is %.0f s of the day (%s)"), Elapsed, Elapsed * 60.0, *Clock->GetClockText()),
		Elapsed > 0.0 && LureDayClockTest::RealDiff(Clock->GetClock(), Clock->GetHour(), Clock->GetClock().Advance(1.0, Elapsed * 60.0)) <= 0.01);
	return true;
}

// ---- AC3: the bite context uses the clock's hour ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDayClockBiteHourTest, "Project.Environment.Clock.World.BiteUsesClockHour", LureDayClockTest::Flags)

bool FLureDayClockBiteHourTest::RunTest(const FString& Parameters)
{
	FLureDayCycleRow Row;
	if (!LureDayClockTest::LoadDefaultRow(*this, Row))
	{
		return false;
	}
	AddExpectedMessagePlain(TEXT("Data gap"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	LureWaterTest::FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureDayClockComponent* Clock = ULureDayClockComponent::Get(World.World);
	if (!TestNotNull(TEXT("the world has the game state's clock"), Clock))
	{
		return false;
	}
	Clock->SetDayCycleRow(Row);

	// A night-only test species (a Bonefish copy biting from NightStart to DawnStart) is the only species.
	const FFishSpeciesRow* Bonefish = FishQA::Row<FFishSpeciesRow>(World.Fish.Species, TEXT("Bonefish"));
	if (!TestNotNull(TEXT("the real species table has Bonefish"), Bonefish))
	{
		return false;
	}
	FFishSpeciesRow Night = *Bonefish;
	Night.TimeWindows.Reset();
	FFishTimeWindow Window;
	Window.StartHour = Row.NightStart;
	Window.EndHour = Row.DawnStart;
	Night.TimeWindows.Add(Window);
	UDataTable* Species = FishQA::NewTable(FFishSpeciesRow::StaticStruct());
	Species->AddRow(TEXT("QA_NightOnly"), Night);
	World.Fish.Species.Reset(Species);

	ALurePlayerCharacter* Character = World.Spawn(*this);
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, Character, LureWaterTest::QuickProfile());
	if (!Fishing)
	{
		return false;
	}
	Fishing->TimeOfDayOverride = -1.f; // the clock decides
	World.Tick(10);
	TestTrue(TEXT("frozen for the test"), Clock->SetTimeScale(0.f));

	struct FCase { float Hour; bool bBites; };
	const FCase Cases[] = { { 12.f, false }, { 18.9f, false }, { 19.1f, true }, { 23.5f, true }, { 2.f, true }, { 4.9f, true }, { 5.1f, false }, { 8.f, false } };
	for (const FCase& Case : Cases)
	{
		Clock->SetHour(Case.Hour);
		const FString Label = FString::Printf(TEXT("%s (%s)"), *Clock->GetClockText(), Case.bBites ? TEXT("night: bites") : TEXT("not night: no bite"));
		if (!LureWaterTest::CastAndLand(*this, World, Fishing))
		{
			return false;
		}
		if (Case.bBites)
		{
			TestFalse(Label + TEXT(": fish here"), Fishing->GetNetState().bNoFishHere);
			if (TestTrue(Label + TEXT(": a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240)))
			{
				TestNearlyEqual(Label + TEXT(": the roll's hour is the clock's"), Fishing->GetLastRollContext().TimeOfDayHours, Clock->GetHour(), 1.0e-4f);
				TestEqual(Label + TEXT(": the night fish"), Fishing->GetPendingFish().SpeciesId, FName(TEXT("QA_NightOnly")));
			}
		}
		else
		{
			TestTrue(Label + TEXT(": no fish here"), Fishing->GetNetState().bNoFishHere);
			TestNearlyEqual(Label + TEXT(": the checked hour is the clock's"), Fishing->GetLastRollContext().TimeOfDayHours, Clock->GetHour(), 1.0e-4f);
			World.Tick(30);
			TestTrue(Label + TEXT(": and nothing bites"), Fishing->GetFishingState() == ELureFishingState::Waiting);
		}
		Fishing->AuthorityReelIn();
		World.Tick(5);
	}

	// TimeOfDayOverride still wins over the clock.
	Clock->SetHour(12.f);
	Fishing->TimeOfDayOverride = 21.f;
	if (LureWaterTest::CastAndLand(*this, World, Fishing))
	{
		TestFalse(TEXT("override 21:00 at clock 12:00: fish here"), Fishing->GetNetState().bNoFishHere);
		TestTrue(TEXT("... a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240));
		TestNearlyEqual(TEXT("... rolled at the override hour"), Fishing->GetLastRollContext().TimeOfDayHours, 21.f, 1.0e-4f);
		Fishing->AuthorityReelIn();
	}
	TestNearlyEqual(TEXT("GetHourOr without a clock = the fallback"), ULureDayClockComponent::GetHourOr(nullptr, 16.f), 16.f);
	return true;
}

// ---- AC4: Lure.Time.* on the server reaches every client; a client can't set the time ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDayClockNetSetTest, "Project.Environment.Clock.Net.ServerSetReachesClients", LureDayClockTest::Flags)

bool FLureDayClockNetSetTest::RunTest(const FString& Parameters)
{
	using LureDayClockTest::HourDiff;
	using LureDayClockTest::GameMinute;
	using LureDayClockTest::NetDt;
	AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("refused"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* ServerWorld = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("the server world is up"), Worlds.Server.IsLoaded() && ServerWorld && ServerWorld->GetNetDriver())
		|| !TestTrue(TEXT("client 1 connects"), Worlds.CreateAndConnectClient())
		|| !TestTrue(TEXT("client 2 connects"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	ULureDayClockComponent* ServerClock = ULureDayClockComponent::Get(ServerWorld);
	if (!TestNotNull(TEXT("the server has the clock"), ServerClock))
	{
		return false;
	}
	TestTrue(TEXT("the server runs the clock (scale 1)"), ServerClock->SetTimeScale(1.f));
	auto ClientClock = [&Worlds](int32 Index) { return ULureDayClockComponent::Get(Worlds.Clients[Index].GetWorld()); };
	auto AllMatch = [&]()
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const ULureDayClockComponent* Clock = ClientClock(Index);
			if (!Clock || HourDiff(Clock->GetHour(), ServerClock->GetHour()) > GameMinute)
			{
				return false;
			}
		}
		return true;
	};
	TestTrue(TEXT("both clients get the clock and show the server's hour"), Worlds.TickAllUntil(AllMatch, NetDt, 600));
	Worlds.TickAll(60);

	// The server sets 21:00 through the console, as a playtest does.
	FOutputDeviceNull Null;
	TestTrue(TEXT("Lure.Time.Set 21 runs on the server"), FLureTimeDevCommands::RunSet({ TEXT("21") }, ServerWorld, Null));
	TestTrue(TEXT("... the server is at 21:00"), HourDiff(ServerClock->GetHour(), 21.0) < GameMinute);
	int32 Ticks = 0;
	const bool bFollowed = Worlds.TickAllUntil([&]()
	{
		++Ticks;
		return AllMatch() && HourDiff(ClientClock(0)->GetHour(), 21.0) < 2.0 * GameMinute;
	}, NetDt, 60);
	TestTrue(FString::Printf(TEXT("AC4: every client shows 21:00 +-1 game-minute within one update (%d ticks; %s / %s)"), Ticks,
		ClientClock(0) ? *ClientClock(0)->GetClockText() : TEXT("-"), ClientClock(1) ? *ClientClock(1)->GetClockText() : TEXT("-")), bFollowed);
	TestEqual(TEXT("... the client HUD line"), ALureHUD::GetClockText(Worlds.Clients[0].GetWorld()), FString(TEXT("21:00 Night")));

	// A client can't set the time: the command refuses and the component call does nothing.
	UWorld* ClientWorld = Worlds.Clients[0].GetWorld();
	TestFalse(TEXT("Lure.Time.Set 3 on a client refuses"), FLureTimeDevCommands::RunSet({ TEXT("3") }, ClientWorld, Null));
	TestFalse(TEXT("Lure.Time.Scale 0 on a client refuses"), FLureTimeDevCommands::RunScale({ TEXT("0") }, ClientWorld, Null));
	TestFalse(TEXT("Lure.Time.Phase Day on a client refuses"), FLureTimeDevCommands::RunPhase({ TEXT("Day") }, ClientWorld, Null));
	TestTrue(TEXT("the registered console command exists on a client"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Lure.Time.Set 3"), Null, ClientWorld));
	TestFalse(TEXT("the client's component refuses SetHour"), ClientClock(0)->SetHour(3.f));
	Worlds.TickAll(30);
	TestTrue(FString::Printf(TEXT("... the server stays at night (%s)"), *ServerClock->GetClockText()), HourDiff(ServerClock->GetHour(), 21.0) < 5.0 * GameMinute);
	TestTrue(TEXT("... and the clients still match it"), AllMatch());

	// Scale and Phase through the console on the server world.
	TestTrue(TEXT("console Lure.Time.Scale 0 on the server"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Lure.Time.Scale 0"), Null, ServerWorld));
	TestTrue(TEXT("console Lure.Time.Phase Dusk on the server"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Lure.Time.Phase Dusk"), Null, ServerWorld));
	TestTrue(TEXT("the server is frozen at 17:00"), ServerClock->GetTimeScale() == 0.f && HourDiff(ServerClock->GetHour(), 17.0) < 1.0e-4);
	TestTrue(TEXT("both clients follow to 17:00 Dusk"), Worlds.TickAllUntil([&]()
	{
		return ClientClock(0)->GetClockText() == TEXT("17:00 Dusk") && ClientClock(1)->GetClockText() == TEXT("17:00 Dusk");
	}, NetDt, 60));
	Worlds.TickAll(120);
	TestTrue(TEXT("... and stay frozen"), ClientClock(0)->GetClockText() == TEXT("17:00 Dusk") && ClientClock(1)->GetClockText() == TEXT("17:00 Dusk"));
	TestFalse(TEXT("Lure.Time.Set rejects a non-hour"), FLureTimeDevCommands::RunSet({ TEXT("noon") }, ServerWorld, Null));
	TestFalse(TEXT("Lure.Time.Scale rejects a negative"), FLureTimeDevCommands::RunScale({ TEXT("-2") }, ServerWorld, Null));
	TestFalse(TEXT("Lure.Time.Phase rejects an unknown phase"), FLureTimeDevCommands::RunPhase({ TEXT("Noon") }, ServerWorld, Null));
	float Parsed = 0.f;
	TestTrue(TEXT("ParseHour 21:30"), FLureTimeDevCommands::ParseHour(TEXT("21:30"), Parsed) && FMath::IsNearlyEqual(Parsed, 21.5f));
	return true;
}

// ---- AC5: a late joiner shows the server's hour; clients never differ by more than a game-minute ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureDayClockNetLateJoinTest, "Project.Environment.Clock.Net.LateJoinerMatchesServer", LureDayClockTest::Flags)

bool FLureDayClockNetLateJoinTest::RunTest(const FString& Parameters)
{
	using LureDayClockTest::HourDiff;
	using LureDayClockTest::GameMinute;
	using LureDayClockTest::NetDt;
	AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* ServerWorld = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("the server world is up"), Worlds.Server.IsLoaded() && ServerWorld && ServerWorld->GetNetDriver())
		|| !TestTrue(TEXT("client 1 connects"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	ULureDayClockComponent* ServerClock = ULureDayClockComponent::Get(ServerWorld);
	if (!TestNotNull(TEXT("the server has the clock"), ServerClock))
	{
		return false;
	}
	TestTrue(TEXT("the server runs the clock (scale 1)"), ServerClock->SetTimeScale(1.f));
	// Mid-day on the server: 5 s of play, then 14:30 and 10 more seconds (the late client's world starts at time 0).
	Worlds.TickAll(300);
	TestTrue(TEXT("Set 14:30"), ServerClock->SetHour(14.5f));
	Worlds.TickAll(600);
	if (!TestTrue(TEXT("client 2 joins late"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	auto ClientClock = [&Worlds](int32 Index) { return ULureDayClockComponent::Get(Worlds.Clients[Index].GetWorld()); };
	const bool bJoined = Worlds.TickAllUntil([&]() { return ClientClock(1) != nullptr && ClientClock(1)->HasBegunPlay(); }, NetDt, 600);
	if (!TestTrue(TEXT("the late client gets the game state and its clock"), bJoined))
	{
		return false;
	}
	Worlds.TickAll(30); // the server time sync settles (ReplicatedWorldTimeSeconds every 0.1 s)
	TestTrue(FString::Printf(TEXT("AC5: the late joiner shows the server's hour +-1 game-minute (%s vs %s)"), *ClientClock(1)->GetClockText(), *ServerClock->GetClockText()),
		HourDiff(ClientClock(1)->GetHour(), ServerClock->GetHour()) <= GameMinute);
	TestTrue(TEXT("... about 14:40 (10 s of Day after 14:30)"), HourDiff(ServerClock->GetHour(), 14.5 + 10.5 * ServerClock->GetClock().GetRate(ELureDayPhase::Day)) < 2.0 * GameMinute);

	// Ten seconds of play: the two clients and the server never differ by more than one game-minute.
	double Worst = 0.0;
	for (int32 Tick = 0; Tick < 600; ++Tick)
	{
		Worlds.TickAll(1);
		const double A = ClientClock(0)->GetHour();
		const double B = ClientClock(1)->GetHour();
		Worst = FMath::Max(Worst, FMath::Max(HourDiff(A, B), FMath::Max(HourDiff(A, ServerClock->GetHour()), HourDiff(B, ServerClock->GetHour()))));
	}
	TestTrue(FString::Printf(TEXT("AC5: the clients never differ by more than 1 game-minute (worst %.3f game-minutes)"), Worst * 60.0), Worst <= GameMinute);
	return true;
}

#endif

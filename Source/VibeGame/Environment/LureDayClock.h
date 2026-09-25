// Lure: the day/night clock math (T-068a). Pure and world-free: no UWorld, no wall clock; callers pass the times.
// Rules: docs/specs/day-night-water.md §3.1 (rules 1-6) and §4 (DT_DayCycle).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "LureDayClock.generated.h"

/** Day/night clock log (table fallback, dev commands, state changes). */
DECLARE_LOG_CATEGORY_EXTERN(LogLureDayNight, Log, All);

/** The four phases of a day, in day order (the phase that follows Night is Dawn). */
UENUM(BlueprintType)
enum class ELureDayPhase : uint8
{
	Dawn,
	Day,
	Dusk,
	Night,
};

/**
 *  DT_DayCycle row (source data/tables/DT_DayCycle.json; a region with another day is a new row). Row "Default".
 *  The *Start columns are the hours (game time) at which each phase begins; the *Minutes columns are the real minutes each
 *  phase lasts (rule 4, the source of truth for pacing). DayLengthMinutes is the length of a whole day: the phase minutes
 *  are weights, scaled so they sum to DayLengthMinutes (so the Default row, whose minutes sum to 20, runs them as written,
 *  and DayLengthMinutes 10 halves every phase). The struct defaults are the built-in fallback row and equal the Default row.
 */
USTRUCT(BlueprintType)
struct FLureDayCycleRow : public FTableRowBase
{
	GENERATED_BODY()

	/** A whole day, real minutes (the phase minutes are scaled to sum to this). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0.01"))
	float DayLengthMinutes = 20.f;

	/** The hour a session starts at, [0, 24). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0", ClampMax="24"))
	float StartHour = 8.f;

	/** Real-time multiplier at session start (1 = the data's pace; 0 = the clock stays at StartHour until Lure.Time.Scale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0"))
	float StartTimeScale = 1.f;

	/** Phase start hours, [0, 24) and strictly ascending (Dawn < Day < Dusk < Night); Night runs on to the next DawnStart. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0", ClampMax="24"))
	float DawnStart = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0", ClampMax="24"))
	float DayStart = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0", ClampMax="24"))
	float DuskStart = 17.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0", ClampMax="24"))
	float NightStart = 19.f;

	/** Real minutes of each phase (weights, see the struct comment), > 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0.01"))
	float DawnMinutes = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0.01"))
	float DayMinutes = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0.01"))
	float DuskMinutes = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(ClampMin="0.01"))
	float NightMinutes = 7.f;

	/** Notes for designers (not used by the game). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight")
	FString DevComment;
};

/**
 *  What the server replicates (rule 2), and only when it changes (session start, Set, Scale, Phase): the hour at a
 *  reference server time and the time scale. Every machine computes the hour from its synced server world time
 *  (AGameStateBase::GetServerWorldTimeSeconds), so there is no per-tick replication.
 */
USTRUCT(BlueprintType)
struct FLureDayClockState
{
	GENERATED_BODY()

	/** The hour at ReferenceServerTime, [0, 24). */
	UPROPERTY(BlueprintReadOnly, Category="Lure|DayNight")
	float ReferenceHour = 8.f;

	/** Server world time (s) at which the hour was ReferenceHour. */
	UPROPERTY(BlueprintReadOnly, Category="Lure|DayNight")
	double ReferenceServerTime = 0.0;

	/** Real-time multiplier (1 = the data's pace, 0 = frozen). */
	UPROPERTY(BlueprintReadOnly, Category="Lure|DayNight")
	float TimeScale = 1.f;
};

/**
 *  The clock math over one DT_DayCycle row. Each phase runs at its own constant rate (its game hours over its real
 *  seconds), so the hour is a piecewise-linear function of real time. Everything is closed-form (no stepping), in double.
 *  "Real seconds from dawn" = real seconds since the last DawnStart at scale 1; a day is GetDaySeconds() of them.
 */
struct FLureDayClock
{
	static constexpr int32 NumPhases = 4;

	/** A clock over the built-in fallback row. */
	FLureDayClock();

	/** A clock over Row; an invalid row (see Validate) gives the fallback row's clock (IsFallback() = true). */
	explicit FLureDayClock(const FLureDayCycleRow& Row);

	/** The built-in row (= the struct defaults = the shipped Default row). */
	static FLureDayCycleRow GetFallbackRow();

	/** True if Row can drive a clock: starts in [0, 24) and strictly ascending, minutes > 0, DayLengthMinutes > 0, StartHour in [0, 24]. */
	static bool Validate(const FLureDayCycleRow& Row, TArray<FString>* OutProblems = nullptr);

	/** Hours into [0, 24). */
	static double NormalizeHour(double Hour);

	/** "Dawn", "Day", "Dusk", "Night". */
	static FString PhaseName(ELureDayPhase Phase);

	/** A phase from its name (case-insensitive). */
	static bool ParsePhase(const FString& Text, ELureDayPhase& OutPhase);

	/** "HH:MM" of a game hour (whole game minutes, rounded down). */
	static FString FormatHour(double Hour);

	const FLureDayCycleRow& GetRow() const { return Row; }
	bool IsFallback() const { return bFallback; }

	/** Real seconds of a whole day (DayLengthMinutes x 60). */
	double GetDaySeconds() const { return DaySeconds; }

	/** Real seconds a phase lasts at scale 1. */
	double GetPhaseSeconds(ELureDayPhase Phase) const { return PhaseSeconds[Index(Phase)]; }

	/** Game hours a phase covers. */
	double GetPhaseHours(ELureDayPhase Phase) const { return PhaseHours[Index(Phase)]; }

	/** The hour a phase starts at. */
	double GetPhaseStartHour(ELureDayPhase Phase) const { return PhaseStart[Index(Phase)]; }

	/** Game hours per real second during a phase, at scale 1. */
	double GetRate(ELureDayPhase Phase) const { return PhaseHours[Index(Phase)] / PhaseSeconds[Index(Phase)]; }

	/** The phase at Hour. */
	ELureDayPhase GetPhase(double Hour) const;

	/** How far through its phase Hour is, [0, 1) (game hours; real time is linear within a phase too). */
	double GetPhaseAlpha(double Hour) const;

	/** Real seconds from the last DawnStart to Hour, [0, GetDaySeconds()). */
	double RealSecondsFromDawn(double Hour) const;

	/** The hour Seconds of real time after DawnStart (any value: whole days wrap). */
	double HourAtRealSecondsFromDawn(double Seconds) const;

	/** The hour RealSeconds (scale applied already; >= 0) after Hour. */
	double Advance(double Hour, double RealSeconds) const;

	/** The hour at ServerTime for a replicated state (a negative elapsed time or scale counts as 0). */
	double HourAt(const FLureDayClockState& State, double ServerTime) const;

	/** Server seconds from ServerTime until the phase changes; < 0 when the clock is frozen (TimeScale <= 0). */
	double SecondsToNextPhase(const FLureDayClockState& State, double ServerTime) const;

	/** "06:40 Dawn" (the HUD line, AC6). */
	FString FormatClock(double Hour) const;

private:

	static int32 Index(ELureDayPhase Phase) { return FMath::Clamp(static_cast<int32>(Phase), 0, NumPhases - 1); }
	void Build();
	/** Hours since DawnStart, [0, 24). */
	double HoursFromDawn(double Hour) const;
	int32 PhaseIndexFromDawnHours(double HoursFromDawnValue) const;

	FLureDayCycleRow Row;
	bool bFallback = false;
	double DaySeconds = 1200.0;
	double PhaseStart[NumPhases] = {};
	double PhaseHours[NumPhases] = {};
	double PhaseSeconds[NumPhases] = {};
	/** Game hours from DawnStart to each phase's start (0, ...) */
	double CumHours[NumPhases] = {};
	/** Real seconds from DawnStart to each phase's start (0, ...) */
	double CumSeconds[NumPhases] = {};
};

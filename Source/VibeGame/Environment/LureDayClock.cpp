// Lure: the day/night clock math (T-068a).

#include "Environment/LureDayClock.h"

DEFINE_LOG_CATEGORY(LogLureDayNight);

FLureDayClock::FLureDayClock()
	: Row(GetFallbackRow())
{
	Build();
}

FLureDayClock::FLureDayClock(const FLureDayCycleRow& InRow)
	: Row(InRow)
{
	if (!Validate(Row))
	{
		Row = GetFallbackRow();
		bFallback = true;
	}
	Build();
}

FLureDayCycleRow FLureDayClock::GetFallbackRow()
{
	return FLureDayCycleRow();
}

bool FLureDayClock::Validate(const FLureDayCycleRow& InRow, TArray<FString>* OutProblems)
{
	bool bOk = true;
	auto Problem = [&bOk, OutProblems](const FString& Text)
	{
		bOk = false;
		if (OutProblems)
		{
			OutProblems->Add(Text);
		}
	};
	const float Starts[NumPhases] = { InRow.DawnStart, InRow.DayStart, InRow.DuskStart, InRow.NightStart };
	const float Minutes[NumPhases] = { InRow.DawnMinutes, InRow.DayMinutes, InRow.DuskMinutes, InRow.NightMinutes };
	for (int32 Phase = 0; Phase < NumPhases; ++Phase)
	{
		const FString Name = PhaseName(static_cast<ELureDayPhase>(Phase));
		if (!FMath::IsFinite(Starts[Phase]) || Starts[Phase] < 0.f || Starts[Phase] >= 24.f)
		{
			Problem(FString::Printf(TEXT("%sStart %g is not in [0, 24)"), *Name, Starts[Phase]));
		}
		if (Phase > 0 && !(Starts[Phase] > Starts[Phase - 1]))
		{
			Problem(FString::Printf(TEXT("%sStart %g must be after %sStart %g"), *Name, Starts[Phase],
				*PhaseName(static_cast<ELureDayPhase>(Phase - 1)), Starts[Phase - 1]));
		}
		if (!FMath::IsFinite(Minutes[Phase]) || !(Minutes[Phase] > 0.f))
		{
			Problem(FString::Printf(TEXT("%sMinutes %g must be > 0"), *Name, Minutes[Phase]));
		}
	}
	if (!FMath::IsFinite(InRow.DayLengthMinutes) || !(InRow.DayLengthMinutes > 0.f))
	{
		Problem(FString::Printf(TEXT("DayLengthMinutes %g must be > 0"), InRow.DayLengthMinutes));
	}
	if (!FMath::IsFinite(InRow.StartHour) || InRow.StartHour < 0.f || InRow.StartHour > 24.f)
	{
		Problem(FString::Printf(TEXT("StartHour %g is not in [0, 24]"), InRow.StartHour));
	}
	if (!FMath::IsFinite(InRow.StartTimeScale) || InRow.StartTimeScale < 0.f)
	{
		Problem(FString::Printf(TEXT("StartTimeScale %g must be >= 0"), InRow.StartTimeScale));
	}
	return bOk;
}

double FLureDayClock::NormalizeHour(double Hour)
{
	if (!FMath::IsFinite(Hour))
	{
		return 0.0;
	}
	double Result = FMath::Fmod(Hour, 24.0);
	if (Result < 0.0)
	{
		Result += 24.0;
	}
	return Result >= 24.0 ? 0.0 : Result;
}

FString FLureDayClock::PhaseName(ELureDayPhase Phase)
{
	switch (Phase)
	{
	case ELureDayPhase::Dawn: return TEXT("Dawn");
	case ELureDayPhase::Day: return TEXT("Day");
	case ELureDayPhase::Dusk: return TEXT("Dusk");
	case ELureDayPhase::Night: return TEXT("Night");
	}
	return TEXT("Day");
}

bool FLureDayClock::ParsePhase(const FString& Text, ELureDayPhase& OutPhase)
{
	for (int32 Phase = 0; Phase < NumPhases; ++Phase)
	{
		if (Text.Equals(PhaseName(static_cast<ELureDayPhase>(Phase)), ESearchCase::IgnoreCase))
		{
			OutPhase = static_cast<ELureDayPhase>(Phase);
			return true;
		}
	}
	return false;
}

FString FLureDayClock::FormatHour(double Hour)
{
	// Whole game minutes, rounded down; the tiny bias keeps 06:40 from showing as 06:39 through float rounding.
	const int64 Minutes = static_cast<int64>(FMath::FloorToDouble(NormalizeHour(Hour) * 60.0 + 1.0e-3)) % (24 * 60);
	return FString::Printf(TEXT("%02d:%02d"), static_cast<int32>(Minutes / 60), static_cast<int32>(Minutes % 60));
}

void FLureDayClock::Build()
{
	const float Starts[NumPhases] = { Row.DawnStart, Row.DayStart, Row.DuskStart, Row.NightStart };
	const float Minutes[NumPhases] = { Row.DawnMinutes, Row.DayMinutes, Row.DuskMinutes, Row.NightMinutes };
	double SumMinutes = 0.0;
	for (int32 Phase = 0; Phase < NumPhases; ++Phase)
	{
		SumMinutes += Minutes[Phase];
	}
	DaySeconds = static_cast<double>(Row.DayLengthMinutes) * 60.0;
	// Rule 4 + the DT_DayCycle note: the phase minutes are weights scaled to sum to DayLengthMinutes.
	const double Scale = DaySeconds / (SumMinutes * 60.0);
	double Hours = 0.0;
	double Seconds = 0.0;
	for (int32 Phase = 0; Phase < NumPhases; ++Phase)
	{
		PhaseStart[Phase] = Starts[Phase];
		PhaseHours[Phase] = Phase + 1 < NumPhases ? static_cast<double>(Starts[Phase + 1]) - Starts[Phase]
			: static_cast<double>(Starts[0]) + 24.0 - Starts[Phase];
		PhaseSeconds[Phase] = static_cast<double>(Minutes[Phase]) * 60.0 * Scale;
		CumHours[Phase] = Hours;
		CumSeconds[Phase] = Seconds;
		Hours += PhaseHours[Phase];
		Seconds += PhaseSeconds[Phase];
	}
}

double FLureDayClock::HoursFromDawn(double Hour) const
{
	return NormalizeHour(NormalizeHour(Hour) - PhaseStart[0]);
}

int32 FLureDayClock::PhaseIndexFromDawnHours(double HoursFromDawnValue) const
{
	for (int32 Phase = NumPhases - 1; Phase > 0; --Phase)
	{
		if (HoursFromDawnValue >= CumHours[Phase])
		{
			return Phase;
		}
	}
	return 0;
}

ELureDayPhase FLureDayClock::GetPhase(double Hour) const
{
	return static_cast<ELureDayPhase>(PhaseIndexFromDawnHours(HoursFromDawn(Hour)));
}

double FLureDayClock::GetPhaseAlpha(double Hour) const
{
	const double FromDawn = HoursFromDawn(Hour);
	const int32 Phase = PhaseIndexFromDawnHours(FromDawn);
	return FMath::Clamp((FromDawn - CumHours[Phase]) / PhaseHours[Phase], 0.0, 1.0);
}

double FLureDayClock::RealSecondsFromDawn(double Hour) const
{
	const double FromDawn = HoursFromDawn(Hour);
	const int32 Phase = PhaseIndexFromDawnHours(FromDawn);
	return CumSeconds[Phase] + (FromDawn - CumHours[Phase]) / PhaseHours[Phase] * PhaseSeconds[Phase];
}

double FLureDayClock::HourAtRealSecondsFromDawn(double Seconds) const
{
	if (!FMath::IsFinite(Seconds))
	{
		return PhaseStart[0];
	}
	double InDay = FMath::Fmod(Seconds, DaySeconds);
	if (InDay < 0.0)
	{
		InDay += DaySeconds;
	}
	int32 Phase = 0;
	for (int32 Candidate = NumPhases - 1; Candidate > 0; --Candidate)
	{
		if (InDay >= CumSeconds[Candidate])
		{
			Phase = Candidate;
			break;
		}
	}
	const double FromDawn = CumHours[Phase] + (InDay - CumSeconds[Phase]) / PhaseSeconds[Phase] * PhaseHours[Phase];
	return NormalizeHour(PhaseStart[0] + FromDawn);
}

double FLureDayClock::Advance(double Hour, double RealSeconds) const
{
	return HourAtRealSecondsFromDawn(RealSecondsFromDawn(Hour) + FMath::Max(0.0, RealSeconds));
}

double FLureDayClock::HourAt(const FLureDayClockState& State, double ServerTime) const
{
	const double Scale = FMath::IsFinite(State.TimeScale) ? FMath::Max(0.0, static_cast<double>(State.TimeScale)) : 0.0;
	const double Elapsed = FMath::Max(0.0, ServerTime - State.ReferenceServerTime) * Scale;
	return Advance(State.ReferenceHour, Elapsed);
}

double FLureDayClock::SecondsToNextPhase(const FLureDayClockState& State, double ServerTime) const
{
	if (!FMath::IsFinite(State.TimeScale) || State.TimeScale <= 0.f)
	{
		return -1.0;
	}
	const double Hour = HourAt(State, ServerTime);
	const int32 Phase = PhaseIndexFromDawnHours(HoursFromDawn(Hour));
	const double PhaseEnd = Phase + 1 < NumPhases ? CumSeconds[Phase + 1] : DaySeconds;
	const double Remaining = FMath::Max(0.0, PhaseEnd - RealSecondsFromDawn(Hour));
	return Remaining / State.TimeScale;
}

FString FLureDayClock::FormatClock(double Hour) const
{
	return FormatHour(Hour) + TEXT(" ") + PhaseName(GetPhase(Hour));
}

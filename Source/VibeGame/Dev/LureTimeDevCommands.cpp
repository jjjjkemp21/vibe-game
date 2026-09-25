// Lure: dev-only console commands for the day/night clock (T-068a).

#include "Dev/LureTimeDevCommands.h"

#if !UE_BUILD_SHIPPING

#include "Dev/LureDevCommands.h"
#include "Engine/World.h"
#include "Environment/LureDayClockComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

const TCHAR* const FLureTimeDevCommands::SetCommand = TEXT("Lure.Time.Set");
const TCHAR* const FLureTimeDevCommands::ScaleCommand = TEXT("Lure.Time.Scale");
const TCHAR* const FLureTimeDevCommands::PhaseCommand = TEXT("Lure.Time.Phase");

namespace LureTimeDevPrivate
{
	bool Refuse(const TCHAR* Command, const FString& Reason, FOutputDevice& Ar)
	{
		UE_LOG(LogLureDev, Warning, TEXT("%s refused: %s"), Command, *Reason);
		Ar.Logf(TEXT("%s refused: %s"), Command, *Reason);
		return false;
	}

	FString Describe(const ULureDayClockComponent& Clock)
	{
		return FString::Printf(TEXT("%s, scale %g"), *Clock.GetClockText(), Clock.GetTimeScale());
	}

	void Report(const TCHAR* Command, const ULureDayClockComponent& Clock, FOutputDevice& Ar)
	{
		const FString Text = Describe(Clock);
		UE_LOG(LogLureDev, Display, TEXT("%s: %s"), Command, *Text);
		Ar.Log(Text);
	}

	/** The server's clock, or null after refusing (no world, a client world, no clock). */
	ULureDayClockComponent* FindServerClock(UWorld* InWorld, const TCHAR* Command, FOutputDevice& Ar)
	{
		UWorld* World = FLureDevCommands::ResolveWorld(InWorld);
		if (!World)
		{
			Refuse(Command, TEXT("no game world (start PIE first)"), Ar);
			return nullptr;
		}
		if (!FLureDevCommands::HasAuthority(World))
		{
			Refuse(Command, TEXT("the time of day is server-owned: run it on the host (server) world, every client follows"), Ar);
			return nullptr;
		}
		ULureDayClockComponent* Clock = ULureDayClockComponent::Get(World);
		if (!Clock)
		{
			Refuse(Command, TEXT("this world has no day clock (the game state is not ALureGameState: check the map's game mode override)"), Ar);
			return nullptr;
		}
		return Clock;
	}
}

bool FLureTimeDevCommands::ParseHour(const FString& Text, float& OutHour)
{
	FString HoursText;
	FString MinutesText;
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.Split(TEXT(":"), &HoursText, &MinutesText))
	{
		if (!HoursText.IsNumeric() || !MinutesText.IsNumeric())
		{
			return false;
		}
		OutHour = FCString::Atof(*HoursText) + FCString::Atof(*MinutesText) / 60.f;
	}
	else
	{
		if (!Trimmed.IsNumeric())
		{
			return false;
		}
		OutHour = FCString::Atof(*Trimmed);
	}
	return FMath::IsFinite(OutHour);
}

bool FLureTimeDevCommands::RunSet(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	using namespace LureTimeDevPrivate;
	ULureDayClockComponent* Clock = FindServerClock(World, SetCommand, Ar);
	if (!Clock)
	{
		return false;
	}
	if (Args.Num() == 0)
	{
		Report(SetCommand, *Clock, Ar);
		return true;
	}
	float Hour = 0.f;
	if (!ParseHour(Args[0], Hour))
	{
		return Refuse(SetCommand, FString::Printf(TEXT("'%s' is not an hour (use 21, 21.5 or 21:30)"), *Args[0]), Ar);
	}
	if (!Clock->SetHour(Hour))
	{
		return Refuse(SetCommand, TEXT("the clock did not accept the hour"), Ar);
	}
	Report(SetCommand, *Clock, Ar);
	return true;
}

bool FLureTimeDevCommands::RunScale(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	using namespace LureTimeDevPrivate;
	ULureDayClockComponent* Clock = FindServerClock(World, ScaleCommand, Ar);
	if (!Clock)
	{
		return false;
	}
	if (Args.Num() == 0)
	{
		Report(ScaleCommand, *Clock, Ar);
		return true;
	}
	const float Scale = Args[0].IsNumeric() ? FCString::Atof(*Args[0]) : -1.f;
	if (!FMath::IsFinite(Scale) || Scale < 0.f)
	{
		return Refuse(ScaleCommand, FString::Printf(TEXT("'%s' is not a scale >= 0 (1 = normal, 0 = frozen)"), *Args[0]), Ar);
	}
	if (!Clock->SetTimeScale(Scale))
	{
		return Refuse(ScaleCommand, TEXT("the clock did not accept the scale"), Ar);
	}
	Report(ScaleCommand, *Clock, Ar);
	return true;
}

bool FLureTimeDevCommands::RunPhase(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	using namespace LureTimeDevPrivate;
	ULureDayClockComponent* Clock = FindServerClock(World, PhaseCommand, Ar);
	if (!Clock)
	{
		return false;
	}
	if (Args.Num() == 0)
	{
		Report(PhaseCommand, *Clock, Ar);
		return true;
	}
	ELureDayPhase Phase = ELureDayPhase::Day;
	if (!FLureDayClock::ParsePhase(Args[0], Phase))
	{
		return Refuse(PhaseCommand, FString::Printf(TEXT("'%s' is not a phase (Dawn, Day, Dusk or Night)"), *Args[0]), Ar);
	}
	if (!Clock->SetPhase(Phase))
	{
		return Refuse(PhaseCommand, TEXT("the clock did not accept the phase"), Ar);
	}
	Report(PhaseCommand, *Clock, Ar);
	return true;
}

namespace LureTimeDevPrivate
{
	FAutoConsoleCommandWithWorldArgsAndOutputDevice SetConsoleCommand(
		FLureTimeDevCommands::SetCommand,
		TEXT("Lure.Time.Set <hour>: jumps the time of day to the hour (21, 21.5 or 21:30). Server/standalone only; every client follows (T-068a)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			FLureTimeDevCommands::RunSet(Args, World, Ar);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScaleConsoleCommand(
		FLureTimeDevCommands::ScaleCommand,
		TEXT("Lure.Time.Scale <x>: the day runs x times the data's pace (0 = frozen). Server/standalone only (T-068a)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			FLureTimeDevCommands::RunScale(Args, World, Ar);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice PhaseConsoleCommand(
		FLureTimeDevCommands::PhaseCommand,
		TEXT("Lure.Time.Phase <Dawn|Day|Dusk|Night>: jumps to the start of the phase. Server/standalone only (T-068a)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			FLureTimeDevCommands::RunPhase(Args, World, Ar);
		}));
}

#endif // !UE_BUILD_SHIPPING

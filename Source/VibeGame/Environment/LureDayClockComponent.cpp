// Lure: the server-owned day/night clock (T-068a).

#include "Environment/LureDayClockComponent.h"
#include "Engine/World.h"
#include "Environment/LureDayNightSettings.h"
#include "Game/LureGameState.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace LureDayClockPrivate
{
	/** The phase timer fires this long after the computed boundary, so the phase has certainly changed (s). */
	constexpr double TimerMargin = 0.02;
}

ULureDayClockComponent::ULureDayClockComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	const FLureDayCycleRow Fallback = FLureDayClock::GetFallbackRow();
	State.ReferenceHour = static_cast<float>(FLureDayClock::NormalizeHour(Fallback.StartHour));
}

ULureDayClockComponent* ULureDayClockComponent::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const ALureGameState* GameState = World ? Cast<ALureGameState>(World->GetGameState()) : nullptr;
	return GameState ? GameState->GetDayClock() : nullptr;
}

float ULureDayClockComponent::GetHourOr(const UObject* WorldContext, float Fallback)
{
	const ULureDayClockComponent* Clock = Get(WorldContext);
	return Clock ? Clock->GetHour() : Fallback;
}

double ULureDayClockComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = Cast<AGameStateBase>(GetOwner());
	if (!GameState)
	{
		GameState = World->GetGameState();
	}
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

double ULureDayClockComponent::GetHourAtServerTime(double ServerTime) const
{
	return Clock.HourAt(State, ServerTime);
}

float ULureDayClockComponent::GetHour() const
{
	return static_cast<float>(GetHourAtServerTime(GetServerTime()));
}

ELureDayPhase ULureDayClockComponent::GetPhase() const
{
	return Clock.GetPhase(GetHourAtServerTime(GetServerTime()));
}

float ULureDayClockComponent::GetPhaseAlpha() const
{
	return static_cast<float>(Clock.GetPhaseAlpha(GetHourAtServerTime(GetServerTime())));
}

FString ULureDayClockComponent::GetClockText() const
{
	return Clock.FormatClock(GetHourAtServerTime(GetServerTime()));
}

bool ULureDayClockComponent::CanChange() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void ULureDayClockComponent::Rebase(double NewHour, float NewScale)
{
	State.ReferenceHour = static_cast<float>(FLureDayClock::NormalizeHour(NewHour));
	State.ReferenceServerTime = GetServerTime();
	State.TimeScale = FMath::IsFinite(NewScale) ? FMath::Max(0.f, NewScale) : 0.f;
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	UE_LOG(LogLureDayNight, Log, TEXT("Clock: %s, scale %g (server time %.2f)"), *Clock.FormatClock(State.ReferenceHour), State.TimeScale, State.ReferenceServerTime);
	RefreshPhase();
}

bool ULureDayClockComponent::SetHour(float Hour)
{
	if (!CanChange() || !FMath::IsFinite(Hour))
	{
		return false;
	}
	Rebase(Hour, State.TimeScale);
	return true;
}

bool ULureDayClockComponent::SetTimeScale(float TimeScale)
{
	if (!CanChange() || !FMath::IsFinite(TimeScale) || TimeScale < 0.f)
	{
		return false;
	}
	Rebase(GetHourAtServerTime(GetServerTime()), TimeScale);
	return true;
}

bool ULureDayClockComponent::SetPhase(ELureDayPhase Phase)
{
	if (!CanChange())
	{
		return false;
	}
	Rebase(Clock.GetPhaseStartHour(Phase), State.TimeScale);
	return true;
}

void ULureDayClockComponent::SetDayCycleRow(const FLureDayCycleRow& Row)
{
	const double HourNow = GetHourAtServerTime(GetServerTime());
	Clock = FLureDayClock(Row);
	bRowSet = true;
	if (HasBegunPlay() && CanChange())
	{
		Rebase(HourNow, State.TimeScale);
	}
	else if (HasBegunPlay())
	{
		RefreshPhase();
	}
}

void ULureDayClockComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bRowSet)
	{
		Clock = FLureDayClock(ULureDayNightSettings::LoadDayCycleRow());
	}
	if (CanChange())
	{
		// Rule 1: a session starts at StartHour, at the data's pace.
		Rebase(Clock.GetRow().StartHour, 1.f);
	}
	else
	{
		RefreshPhase();
	}
}

void ULureDayClockComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhaseTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void ULureDayClockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ULureDayClockComponent, State);
}

void ULureDayClockComponent::OnRep_State()
{
	if (HasBegunPlay())
	{
		RefreshPhase();
	}
}

void ULureDayClockComponent::RefreshPhase()
{
	const ELureDayPhase Phase = GetPhase();
	if (!bPhaseKnown)
	{
		bPhaseKnown = true;
		LastPhase = Phase;
	}
	else if (Phase != LastPhase)
	{
		const ELureDayPhase Old = LastPhase;
		LastPhase = Phase;
		UE_LOG(LogLureDayNight, Verbose, TEXT("Phase %s -> %s at %s"), *FLureDayClock::PhaseName(Old), *FLureDayClock::PhaseName(Phase), *GetClockText());
		OnPhaseChanged.Broadcast(Phase, Old);
	}
	ArmPhaseTimer();
}

void ULureDayClockComponent::ArmPhaseTimer()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	Timers.ClearTimer(PhaseTimer);
	const double Remaining = Clock.SecondsToNextPhase(State, GetServerTime());
	if (Remaining < 0.0)
	{
		return; // frozen
	}
	const float Delay = static_cast<float>(FMath::Max(Remaining + LureDayClockPrivate::TimerMargin, LureDayClockPrivate::TimerMargin));
	Timers.SetTimer(PhaseTimer, FTimerDelegate::CreateUObject(this, &ULureDayClockComponent::RefreshPhase), Delay, /*bLoop*/ false);
}

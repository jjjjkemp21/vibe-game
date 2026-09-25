// Lure: the server-owned day/night clock (T-068a), a component on ALureGameState.
// Rules: docs/specs/day-night-water.md §3.1 (rules 1-6); contract in §7.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Environment/LureDayClock.h"
#include "LureDayClockComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLureDayPhaseChangedSignature, ELureDayPhase, NewPhase, ELureDayPhase, OldPhase);

/**
 *  The time of day. The server owns it: it starts the session at the row's StartHour and replicates one small state
 *  (FLureDayClockState) only when it changes (session start, SetHour, SetTimeScale, SetPhase). Every machine computes the
 *  hour from that state and its synced server world time (AGameStateBase::GetServerWorldTimeSeconds), so all clients show
 *  the same hour with no per-tick replication. The component never ticks; queries compute the hour, and a timer armed for
 *  the next phase boundary fires OnPhaseChanged on every machine.
 *
 *  Reading it: ULureDayClockComponent::Get(WorldContext) (the GameState's clock), then GetHour / GetPhase / GetPhaseAlpha.
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureDayClockComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureDayClockComponent();

	// ---- Finding the clock ----

	/** The clock on WorldContext's GameState (null without one, e.g. before the GameState replicates). */
	static ULureDayClockComponent* Get(const UObject* WorldContext);

	/** The clock's hour, or Fallback without a clock (the bite context: fishing's DefaultTimeOfDayHours). */
	static float GetHourOr(const UObject* WorldContext, float Fallback);

	// ---- Queries (every machine) ----

	/** The hour now, [0, 24). */
	UFUNCTION(BlueprintPure, Category="Lure|DayNight")
	float GetHour() const;

	/** The phase now. */
	UFUNCTION(BlueprintPure, Category="Lure|DayNight")
	ELureDayPhase GetPhase() const;

	/** How far through the current phase, [0, 1). */
	UFUNCTION(BlueprintPure, Category="Lure|DayNight")
	float GetPhaseAlpha() const;

	/** The real-time multiplier (1 = the data's pace, 0 = frozen). */
	UFUNCTION(BlueprintPure, Category="Lure|DayNight")
	float GetTimeScale() const { return State.TimeScale; }

	/** "06:40 Dawn" (the HUD line). */
	UFUNCTION(BlueprintPure, Category="Lure|DayNight")
	FString GetClockText() const;

	/** The hour at a given server world time (s): what GetHour returns at that time if nothing changes. */
	double GetHourAtServerTime(double ServerTime) const;

	/** The synced server world time this machine computes the hour from, s. */
	double GetServerTime() const;

	const FLureDayClock& GetClock() const { return Clock; }
	const FLureDayClockState& GetState() const { return State; }

	/** Fired on every machine when the computed phase changes (a jump over several phases fires once, old -> new). */
	UPROPERTY(BlueprintAssignable, Category="Lure|DayNight")
	FLureDayPhaseChangedSignature OnPhaseChanged;

	/**
	 *  Fired on every machine when the clock jumps or changes pace (SetHour / SetTimeScale / SetPhase, a new row, a
	 *  replicated state arriving), not as time passes. The sky rig re-applies at once on it (T-068b).
	 */
	FSimpleMulticastDelegate OnClockChanged;

	// ---- Changing it (server only; a client call does nothing and returns false) ----

	/** Jumps to Hour (any value; wrapped into [0, 24)), keeping the time scale. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|DayNight")
	bool SetHour(float Hour);

	/** Sets the real-time multiplier (>= 0; 0 = frozen) from now on. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|DayNight")
	bool SetTimeScale(float TimeScale);

	/** Jumps to the start of Phase. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|DayNight")
	bool SetPhase(ELureDayPhase Phase);

	/**
	 *  Runs the clock over Row instead of the configured DT_DayCycle row (tests, a region later); the hour carries on from
	 *  where it is. Call it on every machine (the row is data every machine has; only the state replicates).
	 */
	void SetDayCycleRow(const FLureDayCycleRow& Row);

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	/** The replicated clock state (changes only on start / Set / Scale / Phase). */
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FLureDayClockState State;

	UFUNCTION()
	void OnRep_State();

private:

	bool CanChange() const;
	/** Server: the state becomes (Hour now = NewHour, scale NewScale) at the current server time. */
	void Rebase(double NewHour, float NewScale);
	/** Broadcasts OnPhaseChanged if the computed phase differs from the last one seen, then re-arms the timer. */
	void RefreshPhase();
	void ArmPhaseTimer();

	FLureDayClock Clock;
	/** Set by SetDayCycleRow: BeginPlay keeps it instead of loading the configured row. */
	bool bRowSet = false;
	ELureDayPhase LastPhase = ELureDayPhase::Day;
	bool bPhaseKnown = false;
	FTimerHandle PhaseTimer;
};

// Lure T-068a test helper: records ULureDayClockComponent::OnPhaseChanged (a dynamic delegate needs a UFUNCTION).

#pragma once

#include "CoreMinimal.h"
#include "Environment/LureDayClock.h"
#include "UObject/Object.h"
#include "DayClockTestListener.generated.h"

UCLASS(Transient, NotBlueprintable, HideDropdown)
class ULureDayClockTestListener : public UObject
{
	GENERATED_BODY()

public:

	/** Every (new, old) pair the delegate fired with, in order. */
	TArray<TPair<ELureDayPhase, ELureDayPhase>> Events;

	UFUNCTION()
	void OnPhaseChanged(ELureDayPhase NewPhase, ELureDayPhase OldPhase) { Events.Emplace(NewPhase, OldPhase); }
};

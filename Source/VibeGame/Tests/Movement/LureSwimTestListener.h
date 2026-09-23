// Lure T-026 test helper: records ALurePlayerCharacter::OnSwimStateChanged (a dynamic delegate needs a UFUNCTION).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LureSwimTestListener.generated.h"

UCLASS(Transient, NotBlueprintable, HideDropdown)
class ULureSwimTestListener : public UObject
{
	GENERATED_BODY()

public:

	/** Every value the delegate fired with, in order. */
	TArray<bool> Events;

	UFUNCTION()
	void OnSwimStateChanged(bool bSwimming) { Events.Add(bSwimming); }
};

// Lure: per-world list of interactable actors (T-010).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LureInteractionSubsystem.generated.h"

/**
 *  The interactables of one world (actors implementing ILureInteractable register at BeginPlay, unregister at EndPlay).
 *  A short list, so finding the nearest one is a plain loop: no collision channels or overlap events needed.
 */
UCLASS()
class ULureInteractionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/** The subsystem of WorldContext's world (null without a world) */
	static ULureInteractionSubsystem* Get(const UObject* WorldContext);

	/** Adds Actor if it implements ILureInteractable (no duplicates) */
	void Register(AActor* Actor);

	void Unregister(AActor* Actor);

	/** Registered actors that are still valid */
	TArray<AActor*> GetInteractables() const;

private:

	TArray<TWeakObjectPtr<AActor>> Interactables;
};

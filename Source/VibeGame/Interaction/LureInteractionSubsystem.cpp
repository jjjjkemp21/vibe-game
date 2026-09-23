// Lure: per-world list of interactable actors (T-010).

#include "Interaction/LureInteractionSubsystem.h"
#include "Interaction/LureInteractable.h"
#include "Engine/World.h"

ULureInteractionSubsystem* ULureInteractionSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<ULureInteractionSubsystem>() : nullptr;
}

void ULureInteractionSubsystem::Register(AActor* Actor)
{
	if (!Actor || !Actor->Implements<ULureInteractable>())
	{
		return;
	}
	Interactables.RemoveAll([](const TWeakObjectPtr<AActor>& Entry) { return !Entry.IsValid(); });
	Interactables.AddUnique(Actor);
}

void ULureInteractionSubsystem::Unregister(AActor* Actor)
{
	Interactables.RemoveAll([Actor](const TWeakObjectPtr<AActor>& Entry) { return !Entry.IsValid() || Entry.Get() == Actor; });
}

TArray<AActor*> ULureInteractionSubsystem::GetInteractables() const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<AActor>& Entry : Interactables)
	{
		if (AActor* Actor = Entry.Get())
		{
			Result.Add(Actor);
		}
	}
	return Result;
}

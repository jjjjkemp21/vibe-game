// Lure: the player's Interact key (T-010).

#include "Interaction/LureInteractionComponent.h"
#include "Interaction/LureInteractable.h"
#include "Interaction/LureInteractionSubsystem.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "Progression/LureProgressionTypes.h"

ULureInteractionComponent::ULureInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // server RPCs
}

APawn* ULureInteractionComponent::GetPawn() const
{
	return Cast<APawn>(GetOwner());
}

void ULureInteractionComponent::BindInput(UEnhancedInputComponent& Input)
{
	UInputAction* Action = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Interact);
	if (!Action)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: the Interact input action is missing (ULureInputSubsystem not running); Interact is not bound."), *GetPathNameSafe(this));
		return;
	}
	Input.BindAction(Action, ETriggerEvent::Started, this, &ULureInteractionComponent::HandleInteractPressed);
}

AActor* ULureInteractionComponent::FindBestInteractable() const
{
	const APawn* Pawn = GetPawn();
	const ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(this);
	if (!Pawn || !Subsystem)
	{
		return nullptr;
	}
	AActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (AActor* Actor : Subsystem->GetInteractables())
	{
		const ILureInteractable* Interactable = Cast<ILureInteractable>(Actor);
		if (!Interactable || !Interactable->IsInInteractionRange(Pawn) || !Interactable->CanInteract(Pawn))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Interactable->GetInteractionLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Actor;
		}
	}
	return Best;
}

void ULureInteractionComponent::HandleInteractPressed()
{
	if (AActor* Target = FindBestInteractable())
	{
		RequestInteract(Target, INDEX_NONE);
	}
}

bool ULureInteractionComponent::RequestInteract(AActor* Target, int32 Option)
{
	const AActor* Owner = GetOwner();
	if (!Target || !Owner)
	{
		return false;
	}
	if (Owner->HasAuthority())
	{
		return TryInteract(Target, Option);
	}
	ServerInteract(Target, Option);
	return true;
}

void ULureInteractionComponent::ServerInteract_Implementation(AActor* Target, int32 Option)
{
	TryInteract(Target, Option);
}

bool ULureInteractionComponent::TryInteract(AActor* Target, int32 Option)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: TryInteract is server-only; ignored on this client."), *GetPathNameSafe(this));
		return false;
	}
	if (!IsValid(Target))
	{
		return false; // null or being destroyed
	}
	APawn* Pawn = GetPawn();
	ILureInteractable* Interactable = Cast<ILureInteractable>(Target);
	if (!Pawn || !Interactable)
	{
		return false;
	}
	if (!Interactable->IsInInteractionRange(Pawn, ILureInteractable::ServerRangeSlack))
	{
		UE_LOG(LogLureProgression, Log, TEXT("%s: %s is out of range; interaction refused."), *GetNameSafe(Pawn), *GetNameSafe(Target));
		return false;
	}
	if (!Interactable->CanInteract(Pawn))
	{
		return false;
	}
	return Interactable->Interact(Pawn, Option);
}

FString ULureInteractionComponent::GetPromptText() const
{
	const AActor* Target = FindBestInteractable();
	const ILureInteractable* Interactable = Cast<ILureInteractable>(Target);
	if (!Interactable)
	{
		return FString();
	}
	const TArray<FKey>& Keys = GetDefault<ULureCharacterSettings>()->InteractKeys;
	const FString KeyLabel = Keys.Num() > 0 ? Keys[0].GetDisplayName(/*bLongDisplayName*/ false).ToString() : FString(TEXT("Interact"));
	return FString::Printf(TEXT("[%s] %s"), *KeyLabel, *Interactable->GetInteractionPrompt(GetPawn()).ToString());
}

// Lure: the player's use keys (T-010; T-030 verbs, focus and Alt Interact).

#include "Interaction/LureInteractionComponent.h"
#include "Interaction/LureInteractable.h"
#include "Interaction/LureInteractionSubsystem.h"
#include "Catch/LureCarryableItem.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
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
	UInputAction* Interact = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Interact);
	UInputAction* AltInteract = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::AltInteract);
	if (!Interact || !AltInteract)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: the Interact/AltInteract input actions are missing (ULureInputSubsystem not running); the use keys are not bound."), *GetPathNameSafe(this));
		return;
	}
	Input.BindAction(Interact, ETriggerEvent::Started, this, &ULureInteractionComponent::HandleInteractPressed);
	Input.BindAction(AltInteract, ETriggerEvent::Started, this, &ULureInteractionComponent::HandleAltInteractPressed);
}

AActor* ULureInteractionComponent::FindFocusedInteractable() const
{
	const APawn* Pawn = GetPawn();
	const ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(this);
	if (!Pawn || !Subsystem)
	{
		return nullptr;
	}
	const FVector Eye = Pawn->GetPawnViewLocation();
	const FVector Direction = Pawn->GetViewRotation().Vector();
	const float MaxAngle = ULureCatchSubsystem::GetTuningFor(this).FocusAngleDeg;
	const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	const AActor* Held = Hands ? Hands->GetHeldItem() : nullptr;

	AActor* Best = nullptr;
	float BestAngle = TNumericLimits<float>::Max();
	double BestDistance = TNumericLimits<double>::Max();
	for (AActor* Actor : Subsystem->GetInteractables())
	{
		if (Actor == Held || !IsValid(Actor))
		{
			continue; // the held item is a fallback, never the thing you look at
		}
		const ILureInteractable* Interactable = Cast<ILureInteractable>(Actor);
		if (!Interactable || !Interactable->IsInInteractionRange(Pawn) || !Interactable->CanInteract(Pawn))
		{
			continue;
		}
		// Only targets that do (or say) something for this player can take the focus.
		if (Interactable->GetInteraction(Pawn, ELureInteractKey::Primary).IsEmpty() && Interactable->GetInteraction(Pawn, ELureInteractKey::Secondary).IsEmpty())
		{
			continue;
		}
		const float Angle = Interactable->GetFocusAngle(Eye, Direction);
		if (!(Angle <= MaxAngle))
		{
			continue;
		}
		const double Distance = FVector::Dist(Eye, Interactable->GetInteractionLocation());
		if (Angle < BestAngle - 0.01f || (FMath::Abs(Angle - BestAngle) <= 0.01f && Distance < BestDistance))
		{
			Best = Actor;
			BestAngle = Angle;
			BestDistance = Distance;
		}
	}
	return Best;
}

FLureResolvedInteraction ULureInteractionComponent::ResolveFallback(ELureInteractKey Key) const
{
	FLureResolvedInteraction Result;
	const APawn* Pawn = GetPawn();
	const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!Hands)
	{
		return Result;
	}
	for (ALureCarryableItem* Own : { static_cast<ALureCarryableItem*>(Hands->GetHeldItem()), static_cast<ALureCarryableItem*>(Hands->GetHangingFish()) })
	{
		if (!Own)
		{
			continue;
		}
		const FLureInteraction Interaction = Own->GetInteraction(Pawn, Key);
		if (Interaction.HasVerb())
		{
			Result.Target = Own;
			Result.Verb = Interaction.Verb;
			Result.Prompt = Interaction.Prompt;
			return Result;
		}
	}
	return Result;
}

FLureResolvedInteraction ULureInteractionComponent::ResolveInteraction(ELureInteractKey Key) const
{
	FLureResolvedInteraction Result;
	const APawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return Result;
	}
	AActor* Focus = FindFocusedInteractable();
	const ILureInteractable* Interactable = Cast<ILureInteractable>(Focus);
	if (!Interactable)
	{
		return ResolveFallback(Key);
	}
	const FLureInteraction Interaction = Interactable->GetInteraction(Pawn, Key);
	if (Interaction.HasVerb())
	{
		Result.Target = Focus;
		Result.Verb = Interaction.Verb;
		Result.Prompt = Interaction.Prompt;
		return Result;
	}
	const FLureResolvedInteraction Fallback = ResolveFallback(Key);
	if (Fallback.HasVerb())
	{
		return Fallback;
	}
	if (!Interaction.Prompt.IsEmpty())
	{
		Result.Target = Focus; // an info line ("Cooler full (4/4)")
		Result.Prompt = Interaction.Prompt;
	}
	return Result;
}

bool ULureInteractionComponent::PressKey(ELureInteractKey Key)
{
	const FLureResolvedInteraction Resolved = ResolveInteraction(Key);
	return Resolved.HasVerb() && RequestInteract(Resolved.Target, Key, Resolved.Verb);
}

void ULureInteractionComponent::HandleInteractPressed()
{
	PressKey(ELureInteractKey::Primary);
}

void ULureInteractionComponent::HandleAltInteractPressed()
{
	PressKey(ELureInteractKey::Secondary);
}

bool ULureInteractionComponent::RequestInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb)
{
	const AActor* Owner = GetOwner();
	if (!Target || !Owner || Verb == ELureInteractVerb::None)
	{
		return false;
	}
	if (Owner->HasAuthority())
	{
		return TryInteract(Target, Key, Verb);
	}
	ServerInteract(Target, Key, Verb);
	return true;
}

void ULureInteractionComponent::ServerInteract_Implementation(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb)
{
	TryInteract(Target, Key, Verb);
}

bool ULureInteractionComponent::TryInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: TryInteract is server-only; ignored on this client."), *GetPathNameSafe(this));
		return false;
	}
	if (!IsValid(Target) || Verb == ELureInteractVerb::None)
	{
		return false; // null, being destroyed, or nothing asked
	}
	APawn* Pawn = GetPawn();
	ILureInteractable* Interactable = Cast<ILureInteractable>(Target);
	if (!Pawn || !Interactable)
	{
		return false;
	}
	if (!Interactable->IsInInteractionRange(Pawn, ILureInteractable::ServerRangeSlack))
	{
		UE_LOG(LogLureProgression, Log, TEXT("%s: %s is out of reach; interaction refused."), *GetNameSafe(Pawn), *GetNameSafe(Target));
		return false;
	}
	if (!Interactable->CanInteract(Pawn))
	{
		return false;
	}
	// The server decides what the key does now; a stale client prompt (another player took the last fish) does nothing.
	const FLureInteraction Now = Interactable->GetInteraction(Pawn, Key);
	if (Now.Verb != Verb)
	{
		UE_LOG(LogLureProgression, Verbose, TEXT("%s: %s no longer offers that (asked %d, now %d); refused."), *GetNameSafe(Pawn), *GetNameSafe(Target),
			static_cast<int32>(Verb), static_cast<int32>(Now.Verb));
		return false;
	}
	return Interactable->PerformInteraction(Pawn, Verb);
}

FString ULureInteractionComponent::GetPromptText() const
{
	TArray<FString> Parts;
	FString LastInfo;
	for (const ELureInteractKey Key : { ELureInteractKey::Primary, ELureInteractKey::Secondary })
	{
		const FLureResolvedInteraction Resolved = ResolveInteraction(Key);
		if (Resolved.Prompt.IsEmpty())
		{
			continue;
		}
		const FString Text = Resolved.Prompt.ToString();
		if (Resolved.HasVerb())
		{
			Parts.Add(FString::Printf(TEXT("[%s] %s"), *ILureInteractable::GetKeyLabel(Key), *Text));
		}
		else if (Text != LastInfo)
		{
			Parts.Add(Text);
			LastInfo = Text;
		}
	}
	return FString::Join(Parts, TEXT("   "));
}

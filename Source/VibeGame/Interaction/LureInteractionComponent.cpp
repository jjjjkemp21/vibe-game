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
	// T-030n: the tick records what the prompts show at the end of the frame, in the last tick group: after the network
	// receive, the input, movement, the camera update and the line (TG_PostUpdateWork). The HUD draws this same state after
	// the world tick. Only the locally controlled pawn records (see TickComponent).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_LastDemotable;
	SetIsReplicatedByDefault(true); // server RPCs
}

APawn* ULureInteractionComponent::GetPawn() const
{
	return Cast<APawn>(GetOwner());
}

void ULureInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APawn* Pawn = GetPawn();
	if (Pawn && Pawn->IsLocallyControlled())
	{
		RecordShownPrompts();
	}
}

void ULureInteractionComponent::RecordShownPrompts()
{
	for (const ELureInteractKey Key : { ELureInteractKey::Primary, ELureInteractKey::Secondary })
	{
		const FLureResolvedInteraction Resolved = ResolveInteraction(Key);
		FShownKey& Record = Shown[static_cast<int32>(Key)];
		Record.Target = Resolved.Target.Get();
		Record.Verb = Resolved.HasVerb() ? Resolved.Verb : ELureInteractVerb::None;
		Record.Prompt = Resolved.Prompt;
		Record.StateToken = StateTokenOf(Resolved);
	}
	ShownFrame = GFrameCounter;
	bHasShown = true;
}

int32 ULureInteractionComponent::StateTokenOf(const FLureResolvedInteraction& Resolved) const
{
	const ILureInteractable* Interactable = Resolved.HasVerb() ? Cast<ILureInteractable>(Resolved.Target) : nullptr;
	return Interactable ? Interactable->GetInteractionStateToken(GetPawn(), Resolved.Verb) : 0;
}

bool ULureInteractionComponent::GetShownInteraction(ELureInteractKey Key, FLureResolvedInteraction& OutShown, int32& OutStateToken) const
{
	OutShown = FLureResolvedInteraction();
	OutStateToken = 0;
	// GFrameCounter only grows; a record older than the last frame is not what is on screen now (no local tick: paused, unpossessed).
	if (!bHasShown || GFrameCounter < ShownFrame || GFrameCounter - ShownFrame > MaxShownPromptAgeFrames)
	{
		return false;
	}
	const FShownKey& Record = Shown[static_cast<int32>(Key)];
	OutShown.Target = Record.Target.Get();
	OutShown.Verb = OutShown.Target ? Record.Verb : ELureInteractVerb::None; // its target is gone: nothing to act on
	OutShown.Prompt = Record.Prompt;
	OutStateToken = Record.StateToken;
	return true;
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

	// T-030g: what the view ray actually hits wins first (the nearest hit), then the smallest angle within FocusAngleDeg
	// (ties to the nearer). A big cooler next to a small fish no longer takes the E key from the fish you look straight at.
	AActor* BestHit = nullptr;
	double BestHitDistance = TNumericLimits<double>::Max();
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
		const double Hit = Interactable->GetFocusHitDistance(Eye, Direction);
		if (Hit >= 0.0 && Hit < BestHitDistance)
		{
			BestHit = Actor;
			BestHitDistance = Hit;
		}
		if (BestHit)
		{
			continue; // the angle rule only matters while nothing is hit
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
	return BestHit ? BestHit : Best;
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
	// T-030j: while your own landed fish hangs on your line, E takes it off the hook, whatever you look at (a counter's Sell,
	// a cooler). The targets hide their E verbs while a fish hangs too; this rule makes the priority explicit here.
	if (Key == ELureInteractKey::Primary)
	{
		const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
		if (ALureFishItem* Hanging = Hands ? Hands->GetHangingFish() : nullptr)
		{
			const FLureInteraction Interaction = Hanging->GetInteraction(Pawn, Key);
			if (Interaction.HasVerb())
			{
				Result.Target = Hanging;
				Result.Verb = Interaction.Verb;
				Result.Prompt = Interaction.Prompt;
				return Result;
			}
		}
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
	if (!Resolved.HasVerb())
	{
		return false;
	}
	// T-030h: what the prompt acts on, as this machine sees it now, e.g. the fish on the counter.
	return RequestInteract(Resolved.Target, Key, Resolved.Verb, StateTokenOf(Resolved));
}

bool ULureInteractionComponent::PressKeyAsShown(ELureInteractKey Key)
{
	FLureResolvedInteraction Seen;
	int32 SeenState = 0;
	if (!GetShownInteraction(Key, Seen, SeenState))
	{
		return PressKey(Key); // no fresh record (no local tick yet): what it shows now
	}
	// T-030n: the frame's network update ran before this key; if it changed what the key does, say so in the log (the server
	// then refuses the shown request with a notice, or does exactly what was shown).
	const FLureResolvedInteraction Now = ResolveInteraction(Key);
	if (Now.Target != Seen.Target || Now.Verb != Seen.Verb || StateTokenOf(Now) != SeenState)
	{
		UE_LOG(LogLureProgression, Log, TEXT("%s: %s acts on the prompt shown last frame ('%s' on %s), not on this frame's ('%s' on %s)."),
			*GetNameSafe(GetOwner()), *ILureInteractable::GetKeyLabel(Key), *Seen.Prompt.ToString(), *GetNameSafe(Seen.Target),
			*Now.Prompt.ToString(), *GetNameSafe(Now.Target));
	}
	if (!Seen.HasVerb())
	{
		return false; // the prompt showed nothing for this key (or its target is gone)
	}
	return RequestInteract(Seen.Target, Key, Seen.Verb, SeenState);
}

void ULureInteractionComponent::HandleInteractPressed()
{
	PressKeyAsShown(ELureInteractKey::Primary);
}

void ULureInteractionComponent::HandleAltInteractPressed()
{
	PressKeyAsShown(ELureInteractKey::Secondary);
}

bool ULureInteractionComponent::RequestInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState)
{
	const AActor* Owner = GetOwner();
	if (!Target || !Owner || Verb == ELureInteractVerb::None)
	{
		return false;
	}
	if (Owner->HasAuthority())
	{
		return AuthorityInteract(Target, Key, Verb, ExpectedState, /*bPlayerRequest*/ true);
	}
	ServerInteract(Target, Key, Verb, ExpectedState);
	return true;
}

void ULureInteractionComponent::ServerInteract_Implementation(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState)
{
	AuthorityInteract(Target, Key, Verb, ExpectedState, /*bPlayerRequest*/ true);
}

bool ULureInteractionComponent::TryInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState)
{
	return AuthorityInteract(Target, Key, Verb, ExpectedState, /*bPlayerRequest*/ false);
}

bool ULureInteractionComponent::AuthorityInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState, bool bPlayerRequest)
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
	// T-030h: the same verb on changed contents (a fish taken back from the counter between the prompt and the key) would do
	// more or less than the prompt showed: refused. The player's prompt refreshes from replication; the notice says why.
	// T-030n: a player's request for a verb with a state token must carry it (every key path sends it), so 0 is refused
	// there; only server code calling TryInteract directly may pass 0 to skip the check.
	const int32 NowState = Interactable->GetInteractionStateToken(Pawn, Verb);
	if (NowState != ExpectedState && (ExpectedState != 0 || bPlayerRequest))
	{
		if (ExpectedState == 0)
		{
			UE_LOG(LogLureProgression, Warning, TEXT("%s: asked %s (verb %d) without the prompt's state token; refused (the keys send it: PressKeyAsShown, PressKey)."),
				*GetNameSafe(Pawn), *GetNameSafe(Target), static_cast<int32>(Verb));
			return false;
		}
		UE_LOG(LogLureProgression, Log, TEXT("%s: %s changed since the prompt (verb %d, state %d, seen %d); refused."), *GetNameSafe(Pawn), *GetNameSafe(Target),
			static_cast<int32>(Verb), NowState, ExpectedState);
		if (ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn))
		{
			Hands->ClientNotice(Now.Prompt.IsEmpty() ? FString(TEXT("That just changed: nothing done"))
				: FString::Printf(TEXT("That just changed: nothing done. Now: %s"), *Now.Prompt.ToString()));
		}
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

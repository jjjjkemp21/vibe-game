// Lure: the player's use keys (T-010 Interact; T-030 verbs, focus and the Alt Interact key).
// Rules: docs/specs/catch-handling-rules.md "Focus and input".

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/LureInteractable.h"
#include "LureInteractionComponent.generated.h"

class APawn;
class UEnhancedInputComponent;

/** What a key would do right now: the target, the verb and its prompt (Verb None with a prompt = an info line). */
USTRUCT(BlueprintType)
struct FLureResolvedInteraction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Interaction")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(BlueprintReadOnly, Category="Interaction")
	ELureInteractVerb Verb = ELureInteractVerb::None;

	UPROPERTY(BlueprintReadOnly, Category="Interaction")
	FText Prompt;

	bool HasVerb() const { return Target != nullptr && Verb != ELureInteractVerb::None; }
};

/**
 *  On the player's pawn. Picks what the player looks at (registered interactables in reach with a verb or info for this
 *  player: the nearest one whose shape the view ray hits, T-030g; if the ray hits none, the smallest focus angle up to
 *  DT_Catch FocusAngleDeg, ties to the nearer), and resolves each key:
 *  the focused target's verb for that key, else the held item's, else the hanging fish's. Gives the HUD its prompt and, on a
 *  key (Interact E / X, AltInteract F / Y), asks the server to do the verb.
 *  Server-authoritative: the server checks the target (interactable, reach + ILureInteractable::ServerRangeSlack,
 *  CanInteract), that ITS verb for the key is the one the client expected and, T-030h, that the verb's state token
 *  (ILureInteractable::GetInteractionStateToken: e.g. the fish on the counter) is the one the client saw, then performs it.
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureInteractionComponent();

	/** Binds Interact and AltInteract (called from the pawn's SetupPlayerInputComponent) */
	void BindInput(UEnhancedInputComponent& Input);

	/** The interactable the player looks at (see the class comment); null if none */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	AActor* FindFocusedInteractable() const;

	/** What Key does now: the focused target's verb, else the held item's, else the hanging fish's; an info prompt if none */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	FLureResolvedInteraction ResolveInteraction(ELureInteractKey Key) const;

	/**
	 *  Local player: asks for Verb on Target with Key. On a client this sends ServerInteract; on the server (standalone,
	 *  listen host) it runs TryInteract. False if there is nothing to send. ExpectedState: the verb's state token the
	 *  player saw (ILureInteractable::GetInteractionStateToken; PressKey fills it), 0 = not checked.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	bool RequestInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState = 0);

	/**
	 *  Server: validates Target (interactable, in reach + slack, CanInteract, its verb for Key == Verb and, when
	 *  ExpectedState is not 0, its state token for Verb == ExpectedState) and performs it. A token mismatch (the contents
	 *  changed since the player's prompt, e.g. a fish taken back from the counter) does nothing and tells the player
	 *  (notice with the new prompt; their prompt refreshes from replication).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Interaction")
	bool TryInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState = 0);

	/** "[E] Put the Bonefish in the cooler (1/4)   [F] Drop the Bonefish" (info prompts without a key), or empty */
	UFUNCTION(BlueprintPure, Category="Lure|Interaction")
	FString GetPromptText() const;

	/** Presses Key as the input does: resolves, takes the verb's state token from this machine's view and requests. False if the key does nothing now. Public for tests and the playtest driver. */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	bool PressKey(ELureInteractKey Key);

	/** The Interact key (Primary) */
	void HandleInteractPressed();

	/** The Alt Interact key (Secondary) */
	void HandleAltInteractPressed();

protected:

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState);

private:

	APawn* GetPawn() const;

	/** The pawn's own fallbacks for a key: the held item, then the hanging fish */
	FLureResolvedInteraction ResolveFallback(ELureInteractKey Key) const;
};

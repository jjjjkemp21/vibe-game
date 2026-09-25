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
 *
 *  T-030n, what a key press acts on: the prompt the player SAW, i.e. each key's resolution at the end of the last frame
 *  (recorded by this component's tick in TG_LastDemotable for the local player; the HUD draws that same state after the
 *  world tick). A frame runs: network receive -> the player's input (the key handlers) -> gameplay -> this record -> HUD.
 *  So another player's change (a take-back) can arrive in the same frame as the key, BEFORE the key handler runs; in PIE
 *  the listen host's world ticks first, so a host action always arrives that way. Resolving at the key would then send the
 *  new contents' token and the server would sell what the player never saw. The key handlers therefore press the record
 *  (PressKeyAsShown); PressKey resolves now (tests and tools that set things up directly).
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureInteractionComponent();

	/** A shown prompt this many frames old is still what the player saw (1 = the last frame; the key runs early next frame) */
	static constexpr uint64 MaxShownPromptAgeFrames = 1;

	/** Binds Interact and AltInteract (called from the pawn's SetupPlayerInputComponent) */
	void BindInput(UEnhancedInputComponent& Input);

	/** T-030n: records what each key's prompt shows at the end of this frame (the locally controlled pawn only) */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The interactable the player looks at (see the class comment); null if none */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	AActor* FindFocusedInteractable() const;

	/** What Key does now: the focused target's verb, else the held item's, else the hanging fish's; an info prompt if none */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	FLureResolvedInteraction ResolveInteraction(ELureInteractKey Key) const;

	/**
	 *  A player's request: asks for Verb on Target with Key. On a client this sends ServerInteract; on the server (standalone,
	 *  listen host) it runs the same checks right away. False if there is nothing to send (or, on the server, if refused).
	 *  ExpectedState: the verb's state token the player saw (ILureInteractable::GetInteractionStateToken; PressKey and
	 *  PressKeyAsShown fill it). T-030n: a verb that has a state token (the counter's Sell) is refused without it (0).
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	bool RequestInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState = 0);

	/**
	 *  Server code: validates Target (interactable, in reach + slack, CanInteract, its verb for Key == Verb and its state
	 *  token for Verb == ExpectedState) and performs it. A token mismatch (the contents changed since the player's prompt,
	 *  e.g. a fish taken back from the counter) does nothing and tells the player (notice with the new prompt; their prompt
	 *  refreshes from replication). ExpectedState 0 skips the token check HERE only (server code and tools that know no
	 *  token); players' requests (ServerInteract, RequestInteract) must carry it (T-030n).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Interaction")
	bool TryInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState = 0);

	/** "[E] Put the Bonefish in the cooler (1/4)   [F] Drop the Bonefish" (info prompts without a key), or empty. Resolved now. */
	UFUNCTION(BlueprintPure, Category="Lure|Interaction")
	FString GetPromptText() const;

	/**
	 *  Presses Key on what this machine sees NOW: resolves, takes the verb's state token now and requests. False if the key
	 *  does nothing now. For tests and tools that change things directly between frames; the real keys use PressKeyAsShown.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	bool PressKey(ELureInteractKey Key);

	/**
	 *  T-030n: presses Key the way the E / F keys do: on what Key's prompt showed at the end of the last frame (target, verb,
	 *  state token; see the class comment), so the server does exactly what the player saw or refuses it with a notice.
	 *  False if the shown prompt had no verb for Key or its target is gone. With no fresh record (no local tick yet) it
	 *  presses as PressKey.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	bool PressKeyAsShown(ELureInteractKey Key);

	/**
	 *  T-030n: what PressKeyAsShown would send for Key (the recorded prompt and its state token), if the record is fresh
	 *  (made at most MaxShownPromptAgeFrames frames ago). False if there is none (not the local player, no tick yet).
	 */
	bool GetShownInteraction(ELureInteractKey Key, FLureResolvedInteraction& OutShown, int32& OutStateToken) const;

	/** The Interact key (Primary): PressKeyAsShown */
	void HandleInteractPressed();

	/** The Alt Interact key (Secondary): PressKeyAsShown */
	void HandleAltInteractPressed();

protected:

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState);

private:

	/** One key's prompt as shown at the end of a frame (T-030n). Weak: the target may be destroyed before the next key. */
	struct FShownKey
	{
		TWeakObjectPtr<AActor> Target;
		ELureInteractVerb Verb = ELureInteractVerb::None;
		FText Prompt;
		int32 StateToken = 0;
	};

	/** Primary, Secondary */
	FShownKey Shown[2];

	/** GFrameCounter when Shown was recorded; bHasShown false until the first record */
	uint64 ShownFrame = 0;
	bool bHasShown = false;

	APawn* GetPawn() const;

	/** The pawn's own fallbacks for a key: the held item, then the hanging fish */
	FLureResolvedInteraction ResolveFallback(ELureInteractKey Key) const;

	/** Records both keys' prompts (TickComponent) */
	void RecordShownPrompts();

	/** Resolved's state token from this machine's view (0 without a verb) */
	int32 StateTokenOf(const FLureResolvedInteraction& Resolved) const;

	/**
	 *  The server checks and the verb (TryInteract, ServerInteract, the host's RequestInteract). bPlayerRequest: a player's
	 *  key; then a verb with a state token needs ExpectedState (0 refused, T-030n).
	 */
	bool AuthorityInteract(AActor* Target, ELureInteractKey Key, ELureInteractVerb Verb, int32 ExpectedState, bool bPlayerRequest);
};

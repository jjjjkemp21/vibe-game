// Lure: the player's Interact key (T-010).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LureInteractionComponent.generated.h"

class APawn;
class UEnhancedInputComponent;

/**
 *  On the player's pawn. Finds the nearest ILureInteractable in range (ULureInteractionSubsystem), gives the HUD its
 *  prompt, and on the Interact action (E / gamepad face-left, ULureInputSubsystem) asks the server to use it.
 *  Server-authoritative: the server checks range (radius + ILureInteractable::ServerRangeSlack) and CanInteract again before Interact runs.
 */
UCLASS(ClassGroup=(Lure), meta=(BlueprintSpawnableComponent))
class ULureInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULureInteractionComponent();

	/** Binds the Interact action (called from the pawn's SetupPlayerInputComponent) */
	void BindInput(UEnhancedInputComponent& Input);

	/** Nearest registered interactable whose range contains the pawn and whose CanInteract passes; null if none */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	AActor* FindBestInteractable() const;

	/**
	 *  Local player: uses Target (Option INDEX_NONE = its default action). On a client this sends a server RPC; on the
	 *  server (standalone, listen host) it runs TryInteract. Returns false if there is nothing to send.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Interaction")
	bool RequestInteract(AActor* Target, int32 Option = -1);

	/** Server: validates Target (interactable, in range + slack, CanInteract) and runs Interact. False if refused. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Lure|Interaction")
	bool TryInteract(AActor* Target, int32 Option = -1);

	/** "[E] Sell 3 fish (45 coins)" for the best interactable, or empty */
	UFUNCTION(BlueprintPure, Category="Lure|Interaction")
	FString GetPromptText() const;

	/** The Interact press handler (what the key does); public so tests and the playtest driver can run it */
	void HandleInteractPressed();

protected:

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target, int32 Option);

private:

	APawn* GetPawn() const;
};

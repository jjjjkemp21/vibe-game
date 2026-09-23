// Lure: things the player can use with the Interact key (T-010: sell points; later NPCs, boats, ladders...).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LureInteractable.generated.h"

class APawn;

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class ULureInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 *  C++ interface for an interactable actor. The actor registers itself with ULureInteractionSubsystem (BeginPlay) and
 *  unregisters (EndPlay); ULureInteractionComponent on the player's pawn picks the nearest one in range, shows its
 *  prompt, and on the Interact key asks the server to run Interact (range and CanInteract are checked again there).
 */
class ILureInteractable
{
	GENERATED_BODY()

public:

	/** World point the interaction radius is measured from */
	virtual FVector GetInteractionLocation() const = 0;

	/** Range in cm (3D distance from the pawn's location) */
	virtual float GetInteractionRadius() const = 0;

	/** Checks besides range (default: any pawn) */
	virtual bool CanInteract(const APawn* Pawn) const { return Pawn != nullptr; }

	/** Plain prompt text without the key, e.g. "Sell 3 fish (45 coins)" */
	virtual FText GetInteractionPrompt(const APawn* Pawn) const = 0;

	/**
	 *  Server only. Option: INDEX_NONE = the default action (a sell point sells everything); >= 0 = a specific choice
	 *  (a sell point sells that cooler slot). Returns true if something happened.
	 */
	virtual bool Interact(APawn* Pawn, int32 Option) = 0;

	/** Extra range (cm) the server accepts on top of the radius, for network lag between the client's check and the server's */
	static constexpr float ServerRangeSlack = 150.0f;

	/** Distance from Pawn to GetInteractionLocation() <= GetInteractionRadius() + Slack */
	bool IsInInteractionRange(const APawn* Pawn, float Slack = 0.0f) const;
};

// Lure: things the player can use with the Interact key.

#include "Interaction/LureInteractable.h"
#include "GameFramework/Pawn.h"

bool ILureInteractable::IsInInteractionRange(const APawn* Pawn, float Slack) const
{
	if (!Pawn)
	{
		return false;
	}
	const float Radius = FMath::Max(0.0f, GetInteractionRadius()) + FMath::Max(0.0f, Slack);
	return FVector::DistSquared(Pawn->GetActorLocation(), GetInteractionLocation()) <= FMath::Square(static_cast<double>(Radius));
}

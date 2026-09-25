// Lure: the fish visual's adapter to the fight (T-029).

#include "Fishing/FightFishViewAdapter.h"
#include "Fish/FishInstance.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/Actor.h"

FFightFishView FFightFishViewAdapter::Make(const FLureFightNetState& Fight, const FLureFishingNetState& Line, const FFishInstance& HookedFish,
	const FVector& PlayerLocation, const FVector& /*PlayerForward: unused since T-045*/, bool bHasAuthority)
{
	FFightFishView View;
	View.bFighting = Fight.bActive && Line.State == ELureFishingState::Hooked;
	View.FightId = Fight.FightId;
	if (!View.bFighting)
	{
		View.End = (Fight.Outcome == ELureFightOutcome::Landed || Line.LastResult == ELureFishingResult::Landed) ? EFightFishEnd::Landed : EFightFishEnd::Escaped;
	}
	View.MoveId = Fight.MoveId;
	View.bExhausted = Fight.bExhausted;
	View.DepthCm = Fight.Depth;
	View.Tension01 = Fight.GetTension01();
	View.Stamina01 = FMath::Clamp(Fight.Stamina, 0.f, 1.f); // T-059a: the clip rate and alpha follow the fish's effort
	View.PlayerLocation = PlayerLocation;
	// T-047: a fish lifted out of the water at a dock edge rides that much higher (the surface it is drawn from rises with it;
	// the lift stays in the view after the fight, so a landed fish is handed on where it was lifted to).
	View.WaterZ = static_cast<float>(Line.BobberRest.Z) + (FMath::IsFinite(Fight.Lift) ? FMath::Max(0.f, Fight.Lift) : 0.f);
	View.bHasAuthority = bHasAuthority;
	View.Fish = HookedFish;

	// T-045: the fish is where the server's fight has it in the world (FishLocation), not placed from this machine's player.
	View.LineEnd = FVector(Fight.FishLocation.X, Fight.FishLocation.Y, View.WaterZ);
	return View;
}

FFightFishView FFightFishViewAdapter::FromComponent(const ULureFishingComponent& Fishing)
{
	const AActor* Owner = Fishing.GetOwner();
	return Make(Fishing.GetFightNet(), Fishing.GetNetState(), Fishing.GetHookedFish(),
		Owner ? Owner->GetActorLocation() : FVector::ZeroVector, Owner ? Owner->GetActorForwardVector() : FVector::ForwardVector,
		Owner && Owner->HasAuthority());
}

// Lure: the fish visual's adapter to the fight (T-029).

#include "Fishing/FightFishViewAdapter.h"
#include "Fish/FishInstance.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/Actor.h"

FFightFishView FFightFishViewAdapter::Make(const FLureFightNetState& Fight, const FLureFishingNetState& Line, const FFishInstance& HookedFish,
	const FVector& PlayerLocation, const FVector& PlayerForward, bool bHasAuthority)
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
	View.PlayerLocation = PlayerLocation;
	View.WaterZ = static_cast<float>(Line.BobberRest.Z);
	View.bHasAuthority = bHasAuthority;
	View.Fish = HookedFish;

	FVector Direction = (FVector(Line.BobberRest) - PlayerLocation).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = PlayerForward.GetSafeNormal2D();
	}
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::ForwardVector;
	}
	Direction = Direction.RotateAngleAxis(Fight.SideDeg, FVector::UpVector);
	View.LineEnd = PlayerLocation + Direction * FMath::Max(0.f, Fight.LineOut);
	View.LineEnd.Z = View.WaterZ;
	return View;
}

FFightFishView FFightFishViewAdapter::FromComponent(const ULureFishingComponent& Fishing)
{
	const AActor* Owner = Fishing.GetOwner();
	return Make(Fishing.GetFightNet(), Fishing.GetNetState(), Fishing.GetHookedFish(),
		Owner ? Owner->GetActorLocation() : FVector::ZeroVector, Owner ? Owner->GetActorForwardVector() : FVector::ForwardVector,
		Owner && Owner->HasAuthority());
}

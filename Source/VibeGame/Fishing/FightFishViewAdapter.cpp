// Lure: the fish visual's adapter to the fight (T-029).

#include "Fishing/FightFishViewAdapter.h"
#include "Fish/FishInstance.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureRodControl.h"
#include "Engine/DataTable.h"
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

const FLureFightMove* FFightFishViewAdapter::FindMove(const FLureFightPatternRow& Pattern, FName MoveId)
{
	return MoveId.IsNone() ? nullptr : Pattern.Moves.FindByPredicate([MoveId](const FLureFightMove& Move) { return Move.Id == MoveId; });
}

FLureFightPatternRow FFightFishViewAdapter::FindPattern(const UDataTable* PatternTable, FName PatternId)
{
	const FLureFightPatternRow* Row = nullptr;
	if (PatternTable && !PatternId.IsNone() && PatternTable->GetRowStruct() && PatternTable->GetRowStruct()->IsChildOf(FLureFightPatternRow::StaticStruct()))
	{
		Row = reinterpret_cast<const FLureFightPatternRow*>(PatternTable->FindRowUnchecked(PatternId));
	}
	FString Problem;
	return (Row && Row->Validate(Problem)) ? *Row : FLureFightPatternRow::GetFallbackPattern();
}

void FFightFishViewAdapter::ApplyMove(FFightFishView& View, const FLureFightNetState& Fight, const FLureFightPatternRow& Pattern)
{
	View.bMoveSwims = false;
	View.MoveSwimSide = 0.f;
	const FLureFightMove* Move = FindMove(Pattern, Fight.MoveId);
	if (!View.bFighting || Fight.bExhausted || !Move || Move->Rest || !(FMath::IsFinite(Move->Speed) && Move->Speed > 0.f))
	{
		return; // Rest, Sulk, tired, unknown: the ground-velocity rule
	}
	View.bMoveSwims = true;
	const float Away = FMath::IsFinite(Move->Away) ? Move->Away : 0.f;
	const float Side = FMath::IsFinite(Move->Side) ? FMath::Abs(Move->Side) : 0.f;
	const float Length = FMath::Sqrt(Away * Away + Side * Side);
	const float Share = Length > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(Side / Length, 0.f, 1.f) : 0.f;
	// The side from the replicated RunSide: the server's pick for RandomSide moves (0 under DT_FishFight SideMinShare).
	View.MoveSwimSide = Share * static_cast<float>(FLureRodControl::DirectionFromRunSide(Fight.RunSide));
}

void FFightPatternCache::SetTable(const UDataTable* InTable)
{
	if (Table.Get() != InTable || (InTable == nullptr && Table.IsStale()))
	{
		Rows.Reset();
	}
	Table = InTable;
}

const FLureFightPatternRow& FFightPatternCache::Get(FName PatternId)
{
	if (const FLureFightPatternRow* Found = Rows.Find(PatternId))
	{
		return *Found;
	}
	++ResolveCount;
	return Rows.Add(PatternId, FFightFishViewAdapter::FindPattern(Table.Get(), PatternId));
}

const FLureFightPatternRow& FFightFishViewAdapter::FallbackPattern()
{
	static const FLureFightPatternRow Fallback = FLureFightPatternRow::GetFallbackPattern();
	return Fallback;
}

FFightFishView FFightFishViewAdapter::FromComponent(const ULureFishingComponent& Fishing, FFightPatternCache* Patterns)
{
	const AActor* Owner = Fishing.GetOwner();
	const FLureFightNetState& Fight = Fishing.GetFightNet();
	FFightFishView View = Make(Fight, Fishing.GetNetState(), Fishing.GetHookedFish(),
		Owner ? Owner->GetActorLocation() : FVector::ZeroVector, Owner ? Owner->GetActorForwardVector() : FVector::ForwardVector,
		Owner && Owner->HasAuthority());
	if (View.bFighting)
	{
		ApplyMove(View, Fight, Patterns ? Patterns->Get(Fight.PatternId) : FallbackPattern());
	}
	return View;
}

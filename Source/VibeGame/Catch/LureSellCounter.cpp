// Lure: the shop's sell counter (T-030).

#include "Catch/LureSellCounter.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "GameFramework/Pawn.h"
#include "Interaction/LureInteractionSubsystem.h"
#include "Misc/Crc.h"
#include "Net/UnrealNetwork.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionSettings.h"

#define LOCTEXT_NAMESPACE "LureSellCounter"

ALureSellCounter::ALureSellCounter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // referable (RPCs, the fish's Counter) even when spawned at runtime; its settings replicate once
	SetNetDormancy(DORM_Initial);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	AreaPreview = CreateDefaultSubobject<UBoxComponent>(TEXT("AreaPreview"));
	AreaPreview->SetupAttachment(Root);
	AreaPreview->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	AreaPreview->SetGenerateOverlapEvents(false);
	AreaPreview->SetCanEverAffectNavigation(false);
	AreaPreview->SetHiddenInGame(true);
	AreaPreview->ShapeColor = FColor(80, 220, 120);
	AreaPreview->InitBoxExtent(CounterHalfSize);
	AreaPreview->SetRelativeLocation(FVector(0.0f, 0.0f, CounterHalfSize.Z));
}

void ALureSellCounter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ALureSellCounter, MarketId, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ALureSellCounter, CounterHalfSize, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ALureSellCounter, InteractionRadius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ALureSellCounter, FishSpacing, COND_InitialOnly);
}

void ALureSellCounter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (AreaPreview)
	{
		AreaPreview->SetBoxExtent(CounterHalfSize.ComponentMax(FVector(1.0f)));
		AreaPreview->SetRelativeLocation(FVector(0.0f, 0.0f, CounterHalfSize.Z));
	}
}

void ALureSellCounter::BeginPlay()
{
	Super::BeginPlay();
	if (ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(this))
	{
		Subsystem->Register(this);
	}
}

void ALureSellCounter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(this))
	{
		Subsystem->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

// ---- Market ----

void ALureSellCounter::SetMarketTable(const UDataTable* Table)
{
	MarketTable = const_cast<UDataTable*>(Table);
	bMarketTableInjected = true;
}

FName ALureSellCounter::GetEffectiveMarketId() const
{
	return MarketId.IsNone() ? GetDefault<ULureProgressionSettings>()->DefaultMarketId : MarketId;
}

float ALureSellCounter::GetSellMultiplier() const
{
	const FName Id = GetEffectiveMarketId();
	const UDataTable* Table = MarketTable;
	if (!Table && !bMarketTableInjected)
	{
		FString Error;
		Table = ULureProgressionSettings::LoadTable(GetDefault<ULureProgressionSettings>()->MarketTable, FFishMarketRow::StaticStruct(), TEXT("DT_FishMarket"), Error);
		if (!Table)
		{
			WarnMarketOnce(FString::Printf(TEXT("%s; paying multiplier 1."), *Error));
			return 1.0f;
		}
	}
	const FFishMarketRow* Row = Table ? Table->FindRow<FFishMarketRow>(Id, TEXT("SellCounter"), false) : nullptr;
	if (!Row)
	{
		WarnMarketOnce(FString::Printf(TEXT("DT_FishMarket has no row '%s'; paying multiplier 1."), *Id.ToString()));
		return 1.0f;
	}
	if (!FMath::IsFinite(Row->SellMultiplier) || !(Row->SellMultiplier > 0.0f))
	{
		WarnMarketOnce(FString::Printf(TEXT("DT_FishMarket row '%s' has SellMultiplier %g; paying multiplier 1."), *Id.ToString(), Row->SellMultiplier));
		return 1.0f;
	}
	return FMath::Min(Row->SellMultiplier, FLureProgressionData::MaxSellMultiplier);
}

void ALureSellCounter::WarnMarketOnce(const FString& Message) const
{
	if (!bWarnedMarket)
	{
		bWarnedMarket = true;
		UE_LOG(LogLureProgression, Warning, TEXT("%s: %s"), *GetName(), *Message);
	}
}

// ---- The counter area ----

bool ALureSellCounter::ContainsPoint(const FVector& Point) const
{
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Point);
	return FMath::Abs(Local.X) <= CounterHalfSize.X && FMath::Abs(Local.Y) <= CounterHalfSize.Y
		&& Local.Z >= -5.0 && Local.Z <= 2.0 * CounterHalfSize.Z;
}

ALureSellCounter* ALureSellCounter::FindCounterAt(const UWorld* World, const FVector& Location)
{
	const ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(World);
	if (!Subsystem)
	{
		return nullptr;
	}
	for (AActor* Actor : Subsystem->GetInteractables())
	{
		ALureSellCounter* Counter = Cast<ALureSellCounter>(Actor);
		if (Counter && Counter->ContainsPoint(Location))
		{
			return Counter;
		}
	}
	return nullptr;
}

TArray<ALureFishItem*> ALureSellCounter::GetFishOnCounter() const
{
	TArray<ALureFishItem*> Result;
	if (const ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(this))
	{
		for (ALureCarryableItem* Item : Catch->GetItems())
		{
			ALureFishItem* Fish = Cast<ALureFishItem>(Item);
			if (Fish && Fish->IsFree() && Fish->GetCounter() == this)
			{
				Result.Add(Fish);
			}
		}
	}
	return Result;
}

int32 ALureSellCounter::QuoteAll() const
{
	const float Multiplier = GetSellMultiplier();
	int32 Total = 0;
	for (const ALureFishItem* Fish : GetFishOnCounter())
	{
		Total = FLureProgressionRules::SaturatingAdd(Total, FLureFreshness::GetSellPrice(Fish->GetFish(), Fish->GetValueShare(), Multiplier));
	}
	return Total;
}

TArray<float> ALureSellCounter::GetSpotOffsets(float HalfLengthY, float Spacing)
{
	const float Step = FMath::Max(10.0f, Spacing);
	const int32 Spots = FMath::Max(1, FMath::FloorToInt(2.0f * FMath::Max(0.0f, HalfLengthY) / Step));
	TArray<float> Offsets;
	Offsets.Reserve(Spots);
	for (int32 Index = 0; Index < Spots; ++Index)
	{
		// Centred along the counter: symmetric around 0, the outermost half a step inside the ends.
		Offsets.Add(Step * (Index - 0.5f * (Spots - 1)));
	}
	// Nearest the centre first; of two at the same distance, + before -.
	Offsets.Sort([](float A, float B)
	{
		const float DistA = FMath::Abs(A);
		const float DistB = FMath::Abs(B);
		return !FMath::IsNearlyEqual(DistA, DistB, 0.01f) ? DistA < DistB : A > B;
	});
	return Offsets;
}

FTransform ALureSellCounter::GetPlacementSpot() const
{
	const TArray<ALureFishItem*> OnCounter = GetFishOnCounter();
	const float Spacing = FMath::Max(10.0f, FishSpacing);
	const TArray<float> Offsets = GetSpotOffsets(CounterHalfSize.Y, Spacing);
	const FTransform Frame = GetActorTransform();
	// A full counter falls back to the centre spot (the first in the order).
	FVector Chosen = Frame.TransformPositionNoScale(FVector(0.0, Offsets[0], 0.0));
	for (const float Offset : Offsets)
	{
		const FVector World = Frame.TransformPositionNoScale(FVector(0.0, Offset, 0.0));
		const bool bTaken = OnCounter.ContainsByPredicate([&World, Spacing](const ALureFishItem* Fish)
		{
			return FVector::Dist2D(Fish->GetActorLocation(), World) < 0.5f * Spacing;
		});
		if (!bTaken)
		{
			Chosen = World;
			break;
		}
	}
	// On the real top surface under the spot (the origin should be on it; a small trace keeps a misplaced marker honest).
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureCounterSpot), false, this);
		if (FLureFishingSpots::TraceCast(World, Hit, Chosen + FVector::UpVector * (2.0 * CounterHalfSize.Z), Chosen - FVector::UpVector * 30.0, Params))
		{
			Chosen = Hit.ImpactPoint;
		}
	}
	// Lying along the counter.
	return FTransform(FRotator(0.0f, GetActorRotation().Yaw + 90.0f, 0.0f), Chosen);
}

int32 ALureSellCounter::GetContentsToken() const
{
	// Per fish: the CRC of its record summary (species, rarity, modifiers, weight, level, value, seed...), lower case (an FName
	// may arrive on a client in another casing). Sorted, so the order the items registered in doesn't matter; duplicates count.
	TArray<uint32> Fish;
	for (const ALureFishItem* Item : GetFishOnCounter())
	{
		Fish.Add(FCrc::StrCrc32(*Item->GetFish().ToString().ToLower()));
	}
	Fish.Sort();
	const uint32 Token = FCrc::MemCrc32(Fish.GetData(), Fish.Num() * sizeof(uint32), static_cast<uint32>(Fish.Num()));
	return Token == 0u ? 1 : static_cast<int32>(Token);
}

// ---- Server ----

FLureSaleResult ALureSellCounter::AuthoritySell(APawn* Seller)
{
	FLureSaleResult Result;
	ULureProgressionComponent* Progression = ULureProgressionLibrary::GetProgression(Seller);
	if (!IsValid(this) || !HasAuthority() || !Progression || !IsInInteractionRange(Seller, ServerRangeSlack))
	{
		return Result;
	}
	const TArray<ALureFishItem*> Sold = GetFishOnCounter();
	if (Sold.Num() == 0)
	{
		return Result;
	}
	const float Multiplier = GetSellMultiplier();
	for (ALureFishItem* Fish : Sold)
	{
		Result.MoneyEarned = FLureProgressionRules::SaturatingAdd(Result.MoneyEarned, FLureFreshness::GetSellPrice(Fish->GetFish(), Fish->GetValueShare(), Multiplier));
		++Result.FishSold;
		Fish->Destroy();
	}
	PlacedOrder.RemoveAll([](const TWeakObjectPtr<ALureFishItem>& Entry) { return !Entry.IsValid(); });
	Progression->RecordSale(Result.FishSold, Result.MoneyEarned);
	UE_LOG(LogLureProgression, Log, TEXT("%s sold %d fish at %s for %d coins"), *GetNameSafe(Seller), Result.FishSold, *GetName(), Result.MoneyEarned);
	return Result;
}

bool ALureSellCounter::AuthorityPlaceFish(APawn* Seller, ALureFishItem* Fish)
{
	if (!HasAuthority() || !IsValid(Fish) || !Fish->IsHeldBy(Seller, ELureHoldMode::Hand))
	{
		return false;
	}
	const FTransform Spot = GetPlacementSpot();
	Fish->AuthorityPlace(Spot.GetLocation(), Spot.Rotator(), Fish->GetVisualTransform().GetLocation(), /*bAnimate*/ true);
	Fish->AuthoritySetCounter(this);
	PlacedOrder.RemoveAll([Fish](const TWeakObjectPtr<ALureFishItem>& Entry) { return !Entry.IsValid() || Entry.Get() == Fish; });
	PlacedOrder.Add(Fish);
	UE_LOG(LogLureCatch, Log, TEXT("%s put %s on %s."), *GetNameSafe(Seller), *Fish->GetFish().SpeciesId.ToString(), *GetName());
	return true;
}

bool ALureSellCounter::AuthorityTakeBack(APawn* Pawn)
{
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!HasAuthority() || !Hands || Hands->IsHoldingSomething() || Hands->GetHangingFish())
	{
		return false;
	}
	ALureFishItem* Last = nullptr;
	for (int32 Index = PlacedOrder.Num() - 1; Index >= 0 && !Last; --Index)
	{
		ALureFishItem* Fish = PlacedOrder[Index].Get();
		if (IsValid(Fish) && Fish->IsFree() && Fish->GetCounter() == this)
		{
			Last = Fish;
		}
	}
	if (!Last)
	{
		const TArray<ALureFishItem*> OnCounter = GetFishOnCounter(); // dropped ones too
		Last = OnCounter.Num() > 0 ? OnCounter.Last() : nullptr;
	}
	if (!Last || !Hands->AuthorityTakeInHand(Last)) // clears its counter
	{
		return false;
	}
	PlacedOrder.RemoveAll([Last](const TWeakObjectPtr<ALureFishItem>& Entry) { return !Entry.IsValid() || Entry.Get() == Last; });
	return true;
}

// ---- Interaction ----

namespace LureSellCounterPrivate
{
	/** The focus shape: the top area plus the counter's front below it (looking at the counter anywhere counts) */
	FTransform FocusBox(const ALureSellCounter& Counter, const FVector& CounterHalfSize, FVector& OutHalf)
	{
		OutHalf = FVector(CounterHalfSize.X, CounterHalfSize.Y, CounterHalfSize.Z + 50.0f);
		return FTransform(Counter.GetActorRotation(), Counter.GetActorTransform().TransformPositionNoScale(FVector(0.0, 0.0, CounterHalfSize.Z - 50.0)));
	}
}

float ALureSellCounter::GetFocusAngle(const FVector& ViewLocation, const FVector& ViewDirection) const
{
	FVector Half;
	const FTransform Box = LureSellCounterPrivate::FocusBox(*this, CounterHalfSize, Half);
	return AngleToBox(ViewLocation, ViewDirection, Box, Half);
}

double ALureSellCounter::GetFocusHitDistance(const FVector& ViewLocation, const FVector& ViewDirection) const
{
	FVector Half;
	const FTransform Box = LureSellCounterPrivate::FocusBox(*this, CounterHalfSize, Half);
	return RayToBox(ViewLocation, ViewDirection, Box, Half);
}

bool ALureSellCounter::CanInteract(const APawn* Pawn) const
{
	return Pawn && IsValid(this);
}

FLureInteraction ALureSellCounter::GetInteraction(const APawn* Pawn, ELureInteractKey Key) const
{
	const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!Hands || !CanInteract(Pawn) || Hands->GetCarriedCooler() || Hands->GetHangingFish())
	{
		return FLureInteraction();
	}
	if (const ALureFishItem* Fish = Hands->GetHeldFish())
	{
		return Key == ELureInteractKey::Primary
			? FLureInteraction::Make(ELureInteractVerb::PlaceFishOnCounter, FText::Format(LOCTEXT("Place", "Put the {0} on the counter"), Fish->GetItemName()))
			: FLureInteraction(); // F falls through to the fish in your hand (drop it: it may land on the counter too)
	}
	const TArray<ALureFishItem*> OnCounter = GetFishOnCounter();
	if (OnCounter.Num() == 0)
	{
		return Key == ELureInteractKey::Primary ? FLureInteraction::Info(LOCTEXT("Empty", "Put fish on the counter to sell them")) : FLureInteraction();
	}
	if (Key == ELureInteractKey::Primary)
	{
		return FLureInteraction::Make(ELureInteractVerb::SellCounter,
			FText::Format(LOCTEXT("Sell", "Sell {0} fish ({1} coins)"), FText::AsNumber(OnCounter.Num()), FText::AsNumber(QuoteAll())));
	}
	return FLureInteraction::Make(ELureInteractVerb::TakeFishFromCounter, LOCTEXT("TakeBack", "Take a fish back")); // the last one put there (server order)
}

int32 ALureSellCounter::GetInteractionStateToken(const APawn* Pawn, ELureInteractVerb Verb) const
{
	return Verb == ELureInteractVerb::SellCounter ? GetContentsToken() : 0;
}

bool ALureSellCounter::PerformInteraction(APawn* Pawn, ELureInteractVerb Verb)
{
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!HasAuthority() || !Hands)
	{
		return false;
	}
	switch (Verb)
	{
	case ELureInteractVerb::PlaceFishOnCounter:
		return AuthorityPlaceFish(Pawn, Hands->GetHeldFish());
	case ELureInteractVerb::SellCounter:
		return AuthoritySell(Pawn).FishSold > 0;
	case ELureInteractVerb::TakeFishFromCounter:
		return AuthorityTakeBack(Pawn);
	default:
		return false;
	}
}

#undef LOCTEXT_NAMESPACE

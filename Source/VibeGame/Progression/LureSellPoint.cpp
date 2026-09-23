// Lure: a place to sell the cooler (T-010).

#include "Progression/LureSellPoint.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionSettings.h"
#include "Interaction/LureInteractionSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "GameFramework/Pawn.h"

#define LOCTEXT_NAMESPACE "LureSellPoint"

ALureSellPoint::ALureSellPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // referable in RPCs even when spawned at runtime; no replicated state
	SetNetDormancy(DORM_Initial);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	RadiusPreview = CreateDefaultSubobject<USphereComponent>(TEXT("RadiusPreview"));
	RadiusPreview->SetupAttachment(Root);
	RadiusPreview->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RadiusPreview->SetGenerateOverlapEvents(false);
	RadiusPreview->SetCanEverAffectNavigation(false);
	RadiusPreview->SetHiddenInGame(true);
	RadiusPreview->ShapeColor = FColor(80, 220, 120);
	RadiusPreview->InitSphereRadius(InteractionRadius);
}

void ALureSellPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (RadiusPreview)
	{
		RadiusPreview->SetSphereRadius(InteractionRadius);
	}
}

void ALureSellPoint::BeginPlay()
{
	Super::BeginPlay();
	if (ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(this))
	{
		Subsystem->Register(this);
	}
}

void ALureSellPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(this))
	{
		Subsystem->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ALureSellPoint::SetMarketTable(const UDataTable* Table)
{
	MarketTable = const_cast<UDataTable*>(Table);
	bMarketTableInjected = true;
}

FName ALureSellPoint::GetEffectiveMarketId() const
{
	return MarketId.IsNone() ? GetDefault<ULureProgressionSettings>()->DefaultMarketId : MarketId;
}

float ALureSellPoint::GetSellMultiplier() const
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
	const FFishMarketRow* Row = Table ? Table->FindRow<FFishMarketRow>(Id, TEXT("SellPoint"), false) : nullptr;
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

void ALureSellPoint::WarnMarketOnce(const FString& Message) const
{
	if (!bWarnedMarket)
	{
		bWarnedMarket = true;
		UE_LOG(LogLureProgression, Warning, TEXT("%s: %s"), *GetName(), *Message);
	}
}

FLureSaleResult ALureSellPoint::SellAll(APawn* Seller)
{
	ULureProgressionComponent* Progression = ULureProgressionLibrary::GetProgression(Seller);
	if (!IsValid(this) || !HasAuthority() || !Progression || !IsInInteractionRange(Seller, ServerRangeSlack))
	{
		return FLureSaleResult();
	}
	const FLureSaleResult Result = Progression->SellAllFish(GetSellMultiplier());
	if (Result.FishSold > 0)
	{
		UE_LOG(LogLureProgression, Log, TEXT("%s sold %d fish at %s for %d coins"), *GetNameSafe(Seller), Result.FishSold, *GetName(), Result.MoneyEarned);
	}
	else
	{
		UE_LOG(LogLureProgression, Verbose, TEXT("%s: cooler is empty, nothing to sell at %s"), *GetNameSafe(Seller), *GetName());
	}
	return Result;
}

FLureSaleResult ALureSellPoint::SellOne(APawn* Seller, int32 SlotIndex)
{
	ULureProgressionComponent* Progression = ULureProgressionLibrary::GetProgression(Seller);
	if (!IsValid(this) || !HasAuthority() || !Progression || !IsInInteractionRange(Seller, ServerRangeSlack))
	{
		return FLureSaleResult();
	}
	return Progression->SellOneFish(SlotIndex, GetSellMultiplier());
}

int32 ALureSellPoint::QuoteAll(const APawn* Seller) const
{
	const ULureCoolerComponent* Cooler = ULureProgressionLibrary::GetCooler(Seller);
	return Cooler ? FLureProgressionRules::GetSellTotal(Cooler->GetFish(), GetSellMultiplier()) : 0;
}

bool ALureSellPoint::CanInteract(const APawn* Pawn) const
{
	return Pawn && ULureProgressionLibrary::GetProgression(Pawn) != nullptr;
}

FText ALureSellPoint::GetInteractionPrompt(const APawn* Pawn) const
{
	const ULureCoolerComponent* Cooler = ULureProgressionLibrary::GetCooler(Pawn);
	const int32 Count = Cooler ? Cooler->GetNumFish() : 0;
	if (Count == 0)
	{
		return LOCTEXT("Empty", "Cooler is empty: nothing to sell");
	}
	return FText::Format(LOCTEXT("SellAll", "Sell {0} fish ({1} coins)"), FText::AsNumber(Count), FText::AsNumber(QuoteAll(Pawn)));
}

bool ALureSellPoint::Interact(APawn* Pawn, int32 Option)
{
	const FLureSaleResult Result = Option == INDEX_NONE ? SellAll(Pawn) : SellOne(Pawn, Option);
	return Result.FishSold > 0;
}

#undef LOCTEXT_NAMESPACE

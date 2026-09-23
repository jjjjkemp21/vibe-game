// Lure: money, XP and levels (T-010).

#include "Progression/LureProgressionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionSettings.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

ULureProgressionComponent::ULureProgressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void ULureProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ULureProgressionComponent, Money);
	DOREPLIFETIME(ULureProgressionComponent, TotalXp);
	DOREPLIFETIME(ULureProgressionComponent, Level);
}

void ULureProgressionComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && !bSaveApplied)
	{
		const int32 Starting = FMath::Max(0, GetDefault<ULureProgressionSettings>()->StartingMoney);
		if (Starting != Money)
		{
			SetMoneyInternal(Starting);
		}
	}
	ULureProgressionLibrary::RegisterPlaceholderHud();
}

bool ULureProgressionComponent::CheckServer(const TCHAR* What) const
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		return true;
	}
	UE_LOG(LogLureProgression, Warning, TEXT("%s: %s is server-only; ignored on this client."), *GetPathNameSafe(this), What);
	return false;
}

void ULureProgressionComponent::ForceNetUpdate()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate(); // PlayerStates update rarely by default
	}
}

// ---- Data ----

void ULureProgressionComponent::SetLevelTable(const UDataTable* Table)
{
	LevelTable = const_cast<UDataTable*>(Table);
	bTableInjected = true;
	bCurveBuilt = false;
}

const FLureLevelCurve& ULureProgressionComponent::GetLevelCurve() const
{
	if (!bCurveBuilt)
	{
		bCurveBuilt = true;
		const UDataTable* Table = LevelTable;
		if (!Table && !bTableInjected)
		{
			FString Error;
			Table = ULureProgressionSettings::LoadTable(GetDefault<ULureProgressionSettings>()->PlayerLevelTable, FPlayerLevelRow::StaticStruct(), TEXT("DT_PlayerLevel"), Error);
			if (!Table)
			{
				UE_LOG(LogLureProgression, Warning, TEXT("Progression: %s; XP still counts but the level stays until the table exists."), *Error);
			}
		}
		TArray<FString> Problems;
		Curve = Table ? FLureLevelCurve::FromTable(Table, &Problems) : FLureLevelCurve();
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogLureProgression, Warning, TEXT("Progression: %s"), *Problem);
		}
	}
	return Curve;
}

// ---- Money ----

void ULureProgressionComponent::SetMoneyInternal(int32 NewMoney)
{
	const int32 Old = Money;
	Money = FMath::Max(0, NewMoney);
	ForceNetUpdate();
	OnMoneyChanged.Broadcast(this, Money, Money - Old);
}

bool ULureProgressionComponent::AddMoney(int32 Amount)
{
	if (!CheckServer(TEXT("AddMoney")) || Amount <= 0)
	{
		return false;
	}
	SetMoneyInternal(FLureProgressionRules::SaturatingAdd(Money, Amount));
	return true;
}

bool ULureProgressionComponent::SpendMoney(int32 Amount)
{
	if (!CheckServer(TEXT("SpendMoney")) || Amount <= 0 || Amount > Money)
	{
		return false;
	}
	SetMoneyInternal(Money - Amount);
	return true;
}

// ---- XP and levels ----

int32 ULureProgressionComponent::AddXp(int32 Amount)
{
	if (!CheckServer(TEXT("AddXp")) || Amount <= 0)
	{
		return 0;
	}
	const int32 OldXp = TotalXp;
	TotalXp = FLureProgressionRules::SaturatingAdd(TotalXp, Amount);
	const int32 OldLevel = Level;
	Level = FMath::Max(Level, GetLevelCurve().GetLevelForXp(TotalXp));
	ForceNetUpdate();

	OnXpChanged.Broadcast(this, TotalXp, TotalXp - OldXp);
	if (Level > OldLevel)
	{
		UE_LOG(LogLureProgression, Log, TEXT("%s: level up %d -> %d (XP %d)"), *GetNameSafe(GetOwner()), OldLevel, Level, TotalXp);
		OnLevelChanged.Broadcast(this, OldLevel, Level);
		OnLevelUp.Broadcast(this, OldLevel, Level);
	}
	return Level - OldLevel;
}

FLureLevelProgress ULureProgressionComponent::GetLevelProgress() const
{
	const FLureLevelCurve& LevelCurve = GetLevelCurve();
	if (LevelCurve.IsEmpty())
	{
		FLureLevelProgress Progress;
		Progress.Level = Level;
		Progress.XpIntoLevel = TotalXp;
		Progress.bIsMaxLevel = true;
		Progress.Fraction = 1.0f;
		return Progress;
	}
	return LevelCurve.GetProgress(TotalXp, Level);
}

float ULureProgressionComponent::GetFishDifficultyMultiplier(int32 FishLevel) const
{
	return GetFishDifficultyMultiplierWith(FishLevel, GetDefault<UFishSettings>()->LevelScaling);
}

float ULureProgressionComponent::GetFishDifficultyMultiplierWith(int32 FishLevel, const FFishLevelScaling& Scaling) const
{
	return FFishRoll::LevelDifficultyMultiplier(FishLevel, Level, Scaling);
}

// ---- Fish flows ----

ULureCoolerComponent* ULureProgressionComponent::GetCooler() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<ULureCoolerComponent>() : nullptr;
}

FLureFishLandedResult ULureProgressionComponent::HandleFishLanded(const FFishInstance& Fish)
{
	FLureFishLandedResult Result;
	Result.NewLevel = Level;
	if (!CheckServer(TEXT("HandleFishLanded")))
	{
		return Result;
	}
	if (!Fish.IsValid())
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: HandleFishLanded got an invalid fish (no species); ignored."), *GetPathNameSafe(this));
		return Result;
	}

	Result.bAccepted = true;
	if (ULureCoolerComponent* Cooler = GetCooler())
	{
		Result.bStoredInCooler = Cooler->AddFishToSlot(Fish, Result.CoolerSlot);
	}
	else
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: no cooler next to the progression component; the fish is not stored."), *GetPathNameSafe(this));
	}
	Result.XpGained = FMath::Max(0, Fish.Xp);
	Result.LevelsGained = Result.XpGained > 0 ? AddXp(Result.XpGained) : 0;
	Result.NewLevel = Level;

	UE_LOG(LogLureProgression, Log, TEXT("%s landed %s: +%d XP, %s"), *GetNameSafe(GetOwner()), *Fish.SpeciesId.ToString(), Result.XpGained,
		Result.bStoredInCooler ? TEXT("in the cooler") : TEXT("cooler full, released"));
	OnFishLanded.Broadcast(this, Fish, Result);
	return Result;
}

FLureSaleResult ULureProgressionComponent::SellAllFish(float SellMultiplier)
{
	FLureSaleResult Result;
	ULureCoolerComponent* Cooler = GetCooler();
	if (!CheckServer(TEXT("SellAllFish")) || !Cooler || Cooler->GetNumFish() == 0)
	{
		return Result;
	}
	const TArray<FFishInstance> Sold = Cooler->TakeAll();
	Result.FishSold = Sold.Num();
	Result.MoneyEarned = FLureProgressionRules::GetSellTotal(Sold, SellMultiplier);
	AddMoney(Result.MoneyEarned);
	return Result;
}

FLureSaleResult ULureProgressionComponent::SellOneFish(int32 SlotIndex, float SellMultiplier)
{
	FLureSaleResult Result;
	ULureCoolerComponent* Cooler = GetCooler();
	if (!CheckServer(TEXT("SellOneFish")) || !Cooler)
	{
		return Result;
	}
	FFishInstance Fish;
	if (!Cooler->RemoveFish(SlotIndex, Fish))
	{
		return Result;
	}
	Result.FishSold = 1;
	Result.MoneyEarned = FLureProgressionRules::GetSellPrice(Fish, SellMultiplier);
	AddMoney(Result.MoneyEarned);
	return Result;
}

// ---- Save ----

FLureProgressSaveData ULureProgressionComponent::GetSaveData() const
{
	FLureProgressSaveData Data;
	Data.Money = Money;
	Data.TotalXp = TotalXp;
	Data.Level = Level;
	if (const ULureCoolerComponent* Cooler = GetCooler())
	{
		Data.CoolerId = Cooler->GetCoolerId();
		Data.CoolerFish = Cooler->GetFish();
	}
	return Data;
}

bool ULureProgressionComponent::ApplySaveData(const FLureProgressSaveData& Data)
{
	if (!CheckServer(TEXT("ApplySaveData")))
	{
		return false;
	}
	if (Data.Version > FLureProgressSaveData::CurrentVersion)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: save data version %d is newer than %d; loading what is known."),
			*GetPathNameSafe(this), Data.Version, FLureProgressSaveData::CurrentVersion);
	}
	bSaveApplied = true;

	const int32 OldMoney = Money;
	const int32 OldXp = TotalXp;
	const int32 OldLevel = Level;

	Money = FMath::Max(0, Data.Money);
	TotalXp = FMath::Max(0, Data.TotalXp);
	const FLureLevelCurve& LevelCurve = GetLevelCurve();
	const int32 Saved = FMath::Max(1, Data.Level);
	Level = LevelCurve.IsEmpty() ? Saved : FMath::Clamp(FMath::Max(Saved, LevelCurve.GetLevelForXp(TotalXp)), 1, LevelCurve.GetMaxLevel());

	if (ULureCoolerComponent* Cooler = GetCooler())
	{
		Cooler->RestoreState(Data.CoolerId, Data.CoolerFish);
	}
	ForceNetUpdate();

	OnMoneyChanged.Broadcast(this, Money, Money - OldMoney);
	OnXpChanged.Broadcast(this, TotalXp, TotalXp - OldXp);
	if (Level != OldLevel)
	{
		OnLevelChanged.Broadcast(this, OldLevel, Level);
	}
	return true;
}

// ---- Client ----

void ULureProgressionComponent::OnRep_Money(int32 OldMoney)
{
	OnMoneyChanged.Broadcast(this, Money, Money - OldMoney);
}

void ULureProgressionComponent::OnRep_TotalXp(int32 OldTotalXp)
{
	OnXpChanged.Broadcast(this, TotalXp, TotalXp - OldTotalXp);
}

void ULureProgressionComponent::OnRep_Level(int32 OldLevel)
{
	OnLevelChanged.Broadcast(this, OldLevel, Level);
	// The first values a client receives (joining) are not level-ups: only a rise after BeginPlay is.
	if (Level > OldLevel && HasBegunPlay())
	{
		OnLevelUp.Broadcast(this, OldLevel, Level);
	}
}

// ---- Placeholder UI ----

FString ULureProgressionComponent::GetStatusText() const
{
	const FLureLevelProgress Progress = GetLevelProgress();
	const FString XpText = Progress.bIsMaxLevel
		? FString::Printf(TEXT("XP %d, max level"), TotalXp)
		: FString::Printf(TEXT("XP %d/%d"), Progress.XpIntoLevel, Progress.XpForNextLevel);
	const ULureCoolerComponent* Cooler = GetCooler();
	const FString CoolerText = Cooler ? FString::Printf(TEXT("Cooler %d/%d"), Cooler->GetNumFish(), Cooler->GetCapacity()) : FString(TEXT("No cooler"));
	return FString::Printf(TEXT("Money %d   Level %d (%s)   %s"), Money, Level, *XpText, *CoolerText);
}

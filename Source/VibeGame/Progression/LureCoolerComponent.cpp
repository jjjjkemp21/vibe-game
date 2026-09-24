// Lure: a cooler's storage (T-010; T-030 records with freshness on the physical cooler).

#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionSettings.h"
#include "Progression/LureProgressionTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

ULureCoolerComponent::ULureCoolerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void ULureCoolerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// A cooler is a shared world object (T-030): everyone may look inside, so the contents go to everyone.
	DOREPLIFETIME(ULureCoolerComponent, StoredFish);
	DOREPLIFETIME(ULureCoolerComponent, CoolerId);
	DOREPLIFETIME(ULureCoolerComponent, Capacity);
	DOREPLIFETIME(ULureCoolerComponent, DecayRate);
}

void ULureCoolerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EnsureCapacity();
	}
}

bool ULureCoolerComponent::CheckServer(const TCHAR* What) const
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		return true;
	}
	UE_LOG(LogLureProgression, Warning, TEXT("%s: %s is server-only; ignored on this client."), *GetPathNameSafe(this), What);
	return false;
}

double ULureCoolerComponent::GetNow() const
{
	return FLureFreshness::GetServerTime(this);
}

const UDataTable* ULureCoolerComponent::GetCoolerTable() const
{
	if (CoolerTable || bTableInjected)
	{
		return CoolerTable;
	}
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	FString Error;
	const UDataTable* Table = ULureProgressionSettings::LoadTable(Settings->CoolerTable, FCoolerRow::StaticStruct(), TEXT("DT_Cooler"), Error);
	if (!Table)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("Cooler: %s; using FallbackCoolerSlots %d."), *Error, Settings->FallbackCoolerSlots);
	}
	return Table;
}

FName ULureCoolerComponent::ResolveCoolerId(FName InCoolerId) const
{
	const FName DefaultId = GetDefault<ULureProgressionSettings>()->DefaultCoolerId;
	if (InCoolerId.IsNone())
	{
		return DefaultId;
	}
	const UDataTable* Table = GetCoolerTable();
	if (!Table || Table->FindRow<FCoolerRow>(InCoolerId, TEXT("Cooler"), false))
	{
		return InCoolerId; // no table: nothing to check against (FallbackCoolerSlots applies)
	}
	if (DefaultId.IsNone() || !Table->FindRow<FCoolerRow>(DefaultId, TEXT("Cooler"), false))
	{
		return InCoolerId; // no default row either: ResolveSlots uses FallbackCoolerSlots
	}
	UE_LOG(LogLureProgression, Warning, TEXT("Cooler: DT_Cooler has no row '%s'; using the default cooler row '%s'."), *InCoolerId.ToString(), *DefaultId.ToString());
	return DefaultId;
}

int32 ULureCoolerComponent::ResolveSlots(FName InCoolerId) const
{
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	const int32 Fallback = FMath::Max(1, Settings->FallbackCoolerSlots);
	const UDataTable* Table = GetCoolerTable();
	if (!Table)
	{
		return Fallback;
	}
	FName RowId = InCoolerId;
	const FCoolerRow* Row = Table->FindRow<FCoolerRow>(RowId, TEXT("Cooler"), /*bWarnIfRowMissing*/ false);
	if (!Row && !Settings->DefaultCoolerId.IsNone())
	{
		RowId = Settings->DefaultCoolerId;
		Row = Table->FindRow<FCoolerRow>(RowId, TEXT("Cooler"), false);
		if (Row)
		{
			UE_LOG(LogLureProgression, Warning, TEXT("Cooler: DT_Cooler has no row '%s'; using the default cooler row '%s'."), *InCoolerId.ToString(), *RowId.ToString());
		}
	}
	if (!Row)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("Cooler: DT_Cooler has no row '%s' and no default row '%s'; using FallbackCoolerSlots %d."),
			*InCoolerId.ToString(), *Settings->DefaultCoolerId.ToString(), Fallback);
		return Fallback;
	}
	if (Row->Slots < 1)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("Cooler: DT_Cooler row '%s' has Slots %d; using 1."), *RowId.ToString(), Row->Slots);
	}
	return FMath::Clamp(Row->Slots, 1, FLureProgressionData::MaxCoolerSlots);
}

const FCoolerRow* ULureCoolerComponent::FindRow() const
{
	const UDataTable* Table = GetCoolerTable();
	return Table && !CoolerId.IsNone() ? Table->FindRow<FCoolerRow>(CoolerId, TEXT("Cooler"), false) : nullptr;
}

FCoolerRow ULureCoolerComponent::FindRowOrDefault() const
{
	if (const FCoolerRow* Row = FindRow())
	{
		return *Row;
	}
	FCoolerRow Default;
	Default.DisplayName = NSLOCTEXT("LureCooler", "Cooler", "Cooler");
	Default.Slots = GetCapacity();
	Default.OpenDecayRate = 1.0f;
	Default.ClosedDecayRate = 0.0f;
	Default.CarrySpeedMultiplier = 1.0f;
	return Default;
}

void ULureCoolerComponent::EnsureCapacity()
{
	if (Capacity <= 0)
	{
		CoolerId = ResolveCoolerId(CoolerId);
		Capacity = ResolveSlots(CoolerId);
	}
	else if (CoolerId.IsNone())
	{
		CoolerId = GetDefault<ULureProgressionSettings>()->DefaultCoolerId;
	}
}

void ULureCoolerComponent::SetCoolerTable(const UDataTable* Table)
{
	CoolerTable = const_cast<UDataTable*>(Table);
	bTableInjected = true;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		CoolerId = ResolveCoolerId(CoolerId);
		Capacity = ResolveSlots(CoolerId);
		NotifyChanged();
	}
}

int32 ULureCoolerComponent::GetCapacity() const
{
	return Capacity > 0 ? Capacity : FMath::Max(1, GetDefault<ULureProgressionSettings>()->FallbackCoolerSlots);
}

bool ULureCoolerComponent::GetFishAt(int32 SlotIndex, FLureCaughtFish& OutFish) const
{
	if (!StoredFish.IsValidIndex(SlotIndex))
	{
		OutFish = FLureCaughtFish();
		return false;
	}
	OutFish = StoredFish[SlotIndex];
	return true;
}

bool ULureCoolerComponent::AddFish(const FLureCaughtFish& Fish)
{
	int32 Slot = INDEX_NONE;
	return AddFishToSlot(Fish, Slot);
}

bool ULureCoolerComponent::AddFishToSlot(const FLureCaughtFish& Fish, int32& OutSlot)
{
	OutSlot = INDEX_NONE;
	if (!CheckServer(TEXT("AddFish")))
	{
		return false;
	}
	if (!Fish.IsValid())
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: AddFish got an invalid fish (no species); refused."), *GetPathNameSafe(this));
		return false;
	}
	EnsureCapacity();
	if (IsFull())
	{
		return false;
	}
	FLureCaughtFish Stored = Fish;
	Stored.Freshness.SetRate(DecayRate, GetNow()); // spoils at the cooler's speed from now on
	OutSlot = StoredFish.Add(Stored);
	NotifyChanged();
	return true;
}

bool ULureCoolerComponent::RemoveFish(int32 SlotIndex, FLureCaughtFish& OutFish)
{
	OutFish = FLureCaughtFish();
	if (!CheckServer(TEXT("RemoveFish")) || !StoredFish.IsValidIndex(SlotIndex))
	{
		return false;
	}
	OutFish = StoredFish[SlotIndex];
	OutFish.Freshness.SetRate(OutFish.Freshness.Rate, GetNow()); // the exposure so far, anchored now
	StoredFish.RemoveAt(SlotIndex);
	NotifyChanged();
	return true;
}

bool ULureCoolerComponent::RemoveLastFish(FLureCaughtFish& OutFish)
{
	return RemoveFish(StoredFish.Num() - 1, OutFish);
}

int32 ULureCoolerComponent::Clear()
{
	if (!CheckServer(TEXT("Clear")))
	{
		return 0;
	}
	const int32 Removed = StoredFish.Num();
	StoredFish.Reset();
	if (Removed > 0)
	{
		NotifyChanged();
	}
	return Removed;
}

TArray<FLureCaughtFish> ULureCoolerComponent::TakeAll()
{
	if (!CheckServer(TEXT("TakeAll")))
	{
		return {};
	}
	TArray<FLureCaughtFish> Taken = MoveTemp(StoredFish);
	StoredFish.Reset();
	const double Now = GetNow();
	for (FLureCaughtFish& Fish : Taken)
	{
		Fish.Freshness.SetRate(Fish.Freshness.Rate, Now);
	}
	if (Taken.Num() > 0)
	{
		NotifyChanged();
	}
	return Taken;
}

bool ULureCoolerComponent::SetCoolerId(FName NewCoolerId)
{
	if (!CheckServer(TEXT("SetCoolerId")))
	{
		return false;
	}
	const UDataTable* Table = GetCoolerTable();
	if (!Table || !Table->FindRow<FCoolerRow>(NewCoolerId, TEXT("Cooler"), false))
	{
		UE_LOG(LogLureProgression, Warning, TEXT("%s: SetCoolerId '%s' is not a DT_Cooler row; unchanged."), *GetPathNameSafe(this), *NewCoolerId.ToString());
		return false;
	}
	CoolerId = NewCoolerId;
	Capacity = ResolveSlots(CoolerId);
	NotifyChanged();
	return true;
}

void ULureCoolerComponent::SetDecayRate(float NewRate)
{
	if (!CheckServer(TEXT("SetDecayRate")))
	{
		return;
	}
	const float Rate = FMath::IsFinite(NewRate) ? FMath::Max(0.0f, NewRate) : 0.0f;
	const double Now = GetNow();
	DecayRate = Rate;
	for (FLureCaughtFish& Fish : StoredFish)
	{
		Fish.Freshness.SetRate(Rate, Now);
	}
	NotifyChanged();
}

void ULureCoolerComponent::RestoreState(FName InCoolerId, const TArray<FLureCaughtFish>& InFish, float InDecayRate)
{
	if (!CheckServer(TEXT("RestoreState")))
	{
		return;
	}
	CoolerId = ResolveCoolerId(InCoolerId); // None or an unknown row (renamed/removed) -> the default row
	Capacity = ResolveSlots(CoolerId);
	DecayRate = FMath::IsFinite(InDecayRate) ? FMath::Max(0.0f, InDecayRate) : 0.0f;
	const double Now = GetNow();
	StoredFish.Reset();
	for (const FLureCaughtFish& Fish : InFish)
	{
		if (Fish.IsValid())
		{
			FLureCaughtFish Loaded = Fish;
			// The saved exposure is the whole history; the anchor starts now at the cooler's speed (server time restarts per session).
			Loaded.Freshness = FLureFreshnessState::StartAt(Now, DecayRate);
			Loaded.Freshness.ExposedSeconds = FMath::IsFinite(Fish.Freshness.ExposedSeconds) ? FMath::Max(0.0f, Fish.Freshness.ExposedSeconds) : 0.0f;
			StoredFish.Add(Loaded);
		}
	}
	if (StoredFish.Num() > Capacity)
	{
		UE_LOG(LogLureProgression, Log, TEXT("%s: the loaded cooler holds %d fish in %d slots; kept them all (no adds until there is room)."),
			*GetPathNameSafe(this), StoredFish.Num(), Capacity);
	}
	NotifyChanged();
}

void ULureCoolerComponent::NotifyChanged()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	OnCoolerChanged.Broadcast(this);
}

void ULureCoolerComponent::OnRep_StoredFish()
{
	OnCoolerChanged.Broadcast(this);
}

void ULureCoolerComponent::OnRep_CoolerSize()
{
	OnCoolerChanged.Broadcast(this);
}

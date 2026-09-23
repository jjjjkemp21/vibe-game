// Lure: the player's cooler (T-010).

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
	DOREPLIFETIME_CONDITION(ULureCoolerComponent, StoredFish, COND_OwnerOnly);
	DOREPLIFETIME(ULureCoolerComponent, CoolerId);
	DOREPLIFETIME(ULureCoolerComponent, Capacity);
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

int32 ULureCoolerComponent::ResolveSlots(FName InCoolerId) const
{
	const ULureProgressionSettings* Settings = GetDefault<ULureProgressionSettings>();
	const int32 Fallback = FMath::Max(1, Settings->FallbackCoolerSlots);
	const UDataTable* Table = GetCoolerTable();
	if (!Table)
	{
		return Fallback;
	}
	const FCoolerRow* Row = Table->FindRow<FCoolerRow>(InCoolerId, TEXT("Cooler"), /*bWarnIfRowMissing*/ false);
	if (!Row)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("Cooler: DT_Cooler has no row '%s'; using FallbackCoolerSlots %d."), *InCoolerId.ToString(), Fallback);
		return Fallback;
	}
	if (Row->Slots < 1)
	{
		UE_LOG(LogLureProgression, Warning, TEXT("Cooler: DT_Cooler row '%s' has Slots %d; using 1."), *InCoolerId.ToString(), Row->Slots);
	}
	return FMath::Clamp(Row->Slots, 1, FLureProgressionData::MaxCoolerSlots);
}

void ULureCoolerComponent::EnsureCapacity()
{
	if (CoolerId.IsNone())
	{
		CoolerId = GetDefault<ULureProgressionSettings>()->DefaultCoolerId;
	}
	if (Capacity <= 0)
	{
		Capacity = ResolveSlots(CoolerId);
	}
}

void ULureCoolerComponent::SetCoolerTable(const UDataTable* Table)
{
	CoolerTable = const_cast<UDataTable*>(Table);
	bTableInjected = true;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (CoolerId.IsNone())
		{
			CoolerId = GetDefault<ULureProgressionSettings>()->DefaultCoolerId;
		}
		Capacity = ResolveSlots(CoolerId);
		NotifyChanged();
	}
}

int32 ULureCoolerComponent::GetCapacity() const
{
	return Capacity > 0 ? Capacity : FMath::Max(1, GetDefault<ULureProgressionSettings>()->FallbackCoolerSlots);
}

bool ULureCoolerComponent::GetFishAt(int32 SlotIndex, FFishInstance& OutFish) const
{
	if (!StoredFish.IsValidIndex(SlotIndex))
	{
		OutFish = FFishInstance();
		return false;
	}
	OutFish = StoredFish[SlotIndex];
	return true;
}

bool ULureCoolerComponent::AddFish(const FFishInstance& Fish)
{
	int32 Slot = INDEX_NONE;
	return AddFishToSlot(Fish, Slot);
}

bool ULureCoolerComponent::AddFishToSlot(const FFishInstance& Fish, int32& OutSlot)
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
	OutSlot = StoredFish.Add(Fish);
	NotifyChanged();
	return true;
}

bool ULureCoolerComponent::RemoveFish(int32 SlotIndex, FFishInstance& OutFish)
{
	OutFish = FFishInstance();
	if (!CheckServer(TEXT("RemoveFish")) || !StoredFish.IsValidIndex(SlotIndex))
	{
		return false;
	}
	OutFish = StoredFish[SlotIndex];
	StoredFish.RemoveAt(SlotIndex);
	NotifyChanged();
	return true;
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

TArray<FFishInstance> ULureCoolerComponent::TakeAll()
{
	if (!CheckServer(TEXT("TakeAll")))
	{
		return {};
	}
	TArray<FFishInstance> Taken = MoveTemp(StoredFish);
	StoredFish.Reset();
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

void ULureCoolerComponent::RestoreState(FName InCoolerId, const TArray<FFishInstance>& InFish)
{
	if (!CheckServer(TEXT("RestoreState")))
	{
		return;
	}
	CoolerId = InCoolerId.IsNone() ? GetDefault<ULureProgressionSettings>()->DefaultCoolerId : InCoolerId;
	Capacity = ResolveSlots(CoolerId);
	StoredFish.Reset();
	for (const FFishInstance& Fish : InFish)
	{
		if (Fish.IsValid())
		{
			StoredFish.Add(Fish);
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
		Owner->ForceNetUpdate(); // PlayerStates update rarely by default
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

// Lure: per-world catch handling data and item registry (T-030).

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCarryableItem.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableRenderAsset.h"
#include "Engine/World.h"
#include "Fish/FishSettings.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

namespace LureCatchSubsystemPrivate
{
	/** One warning per session per missing table (every world resolves it). */
	bool bWarnedCatchTable = false;

	/** Loads a soft reference if its package exists (no load errors for assets not imported yet, e.g. in lanes). */
	UObject* LoadIfExists(const FSoftObjectPath& Path)
	{
		if (Path.IsNull())
		{
			return nullptr;
		}
		if (UObject* Loaded = Path.ResolveObject())
		{
			return Loaded;
		}
		const FString Package = Path.GetLongPackageName();
		if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
		{
			return nullptr;
		}
		return Path.TryLoad();
	}
}

ULureCatchSubsystem* ULureCatchSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<ULureCatchSubsystem>() : nullptr;
}

const FLureCatchRow& ULureCatchSubsystem::GetTuningFor(const UObject* WorldContext)
{
	if (ULureCatchSubsystem* Subsystem = Get(WorldContext))
	{
		return Subsystem->GetTuning();
	}
	static const FLureCatchRow Fallback = FLureCatchRow::GetFallbackRow();
	return Fallback;
}

const FLureCatchRow& ULureCatchSubsystem::GetTuning()
{
	if (bTuningResolved)
	{
		return Tuning;
	}
	bTuningResolved = true;
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	FString Error;
	const UDataTable* Table = ULureCatchSettings::LoadTable(Settings->CatchTable, FLureCatchRow::StaticStruct(), TEXT("DT_Catch"), Error);
	const FLureCatchRow* Row = Table ? reinterpret_cast<const FLureCatchRow*>(Table->FindRowUnchecked(Settings->CatchRow)) : nullptr;
	FString Problem;
	if (Row && Row->Validate(Problem))
	{
		Tuning = *Row;
		return Tuning;
	}
	Tuning = FLureCatchRow::GetFallbackRow();
	if (!LureCatchSubsystemPrivate::bWarnedCatchTable)
	{
		LureCatchSubsystemPrivate::bWarnedCatchTable = true;
		const FString Why = !Table ? Error : (!Row ? FString::Printf(TEXT("DT_Catch has no row '%s'"), *Settings->CatchRow.ToString())
			: FString::Printf(TEXT("DT_Catch row '%s' is invalid (%s)"), *Settings->CatchRow.ToString(), *Problem));
		UE_LOG(LogLureCatch, Warning, TEXT("Catch: %s; using the built-in catch tuning."), *Why);
	}
	return Tuning;
}

FLureFreshnessRow ULureCatchSubsystem::GetFreshnessRow(FName SpeciesId)
{
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	if (!bFreshnessResolved)
	{
		bFreshnessResolved = true;
		if (!bFreshnessInjected)
		{
			FString Error;
			FreshnessTable = const_cast<UDataTable*>(ULureCatchSettings::LoadTable(Settings->FreshnessTable, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness"), Error));
			if (!FreshnessTable)
			{
				UE_LOG(LogLureCatch, Warning, TEXT("Freshness: %s; using the built-in freshness row."), *Error);
				WarnedFreshness.Add(NAME_None);
			}
		}
	}
	bool bFallback = false;
	const FLureFreshnessRow Row = FLureFreshness::FindRow(FreshnessTable, SpeciesId, Settings->DefaultFreshnessRow, &bFallback);
	if (bFallback && FreshnessTable && !WarnedFreshness.Contains(Settings->DefaultFreshnessRow))
	{
		WarnedFreshness.Add(Settings->DefaultFreshnessRow);
		UE_LOG(LogLureCatch, Warning, TEXT("Freshness: %s has no valid row '%s' (or '%s'); using the built-in freshness row."),
			*FreshnessTable->GetName(), *SpeciesId.ToString(), *Settings->DefaultFreshnessRow.ToString());
	}
	return Row;
}

FLureCoolerDisplayRow ULureCatchSubsystem::GetCoolerDisplayRow(FName CoolerId)
{
	if (!bDisplayResolved)
	{
		bDisplayResolved = true;
		FString Error;
		DisplayTable = const_cast<UDataTable*>(ULureCatchSettings::LoadTable(GetDefault<ULureCatchSettings>()->CoolerDisplayTable, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay"), Error));
		if (!DisplayTable)
		{
			UE_LOG(LogLureCatch, Log, TEXT("Cooler display: %s; the built-in layout shows the fish."), *Error);
		}
	}
	if (DisplayTable && !CoolerId.IsNone())
	{
		if (const FLureCoolerDisplayRow* Row = reinterpret_cast<const FLureCoolerDisplayRow*>(DisplayTable->FindRowUnchecked(CoolerId)))
		{
			FString Problem;
			if (Row->Validate(Problem))
			{
				return *Row;
			}
		}
	}
	return FLureCoolerDisplayRow::GetFallbackRow();
}

void ULureCatchSubsystem::SetCoolerDisplayTable(const UDataTable* Table)
{
	DisplayTable = const_cast<UDataTable*>(Table);
	bDisplayResolved = true;
}

double ULureCatchSubsystem::GetServerTime() const
{
	return FLureFreshness::GetServerTime(this);
}

void ULureCatchSubsystem::SetTuning(const FLureCatchRow& Row)
{
	FString Problem;
	Tuning = Row.Validate(Problem) ? Row : FLureCatchRow::GetFallbackRow();
	bTuningResolved = true;
}

void ULureCatchSubsystem::SetFreshnessTable(const UDataTable* Table)
{
	FreshnessTable = const_cast<UDataTable*>(Table);
	bFreshnessInjected = true;
	bFreshnessResolved = true;
	WarnedFreshness.Reset();
}

void ULureCatchSubsystem::SetCoolerTable(const UDataTable* Table)
{
	CoolerTable = const_cast<UDataTable*>(Table);
	bCoolerTableInjected = true;
}

const UDataTable* ULureCatchSubsystem::GetCoolerTableOverride(bool& bOutInjected) const
{
	bOutInjected = bCoolerTableInjected;
	return CoolerTable;
}

void ULureCatchSubsystem::SetFishTables(const FFishTables& InTables)
{
	FishTables = InTables;
	bFishTablesResolved = true;
	FishTableRefs.Reset();
	for (const UDataTable* Table : { FishTables.Species, FishTables.Rarities, FishTables.Modifiers, FishTables.Stats })
	{
		if (Table)
		{
			FishTableRefs.Add(const_cast<UDataTable*>(Table));
		}
	}
}

const FFishTables& ULureCatchSubsystem::GetFishTables()
{
	if (!bFishTablesResolved)
	{
		bFishTablesResolved = true;
		FFishTables Loaded;
		FString Error;
		GetDefault<UFishSettings>()->LoadTables(Loaded, Error); // tables that loaded are set even when another is missing
		SetFishTables(Loaded);
	}
	return FishTables;
}

FText ULureCatchSubsystem::GetSpeciesDisplayName(FName SpeciesId)
{
	if (const FFishSpeciesRow* Species = GetFishTables().FindSpecies(SpeciesId))
	{
		if (!Species->DisplayName.IsEmpty())
		{
			return Species->DisplayName;
		}
	}
	return FText::FromName(SpeciesId);
}

FText ULureCatchSubsystem::GetRarityDisplayName(FName RarityId)
{
	if (const FFishRarityRow* Rarity = GetFishTables().FindRarity(RarityId))
	{
		if (!Rarity->DisplayName.IsEmpty())
		{
			return Rarity->DisplayName;
		}
	}
	return FText::FromName(RarityId);
}

float ULureCatchSubsystem::GetSpeciesReferenceWeight(FName SpeciesId)
{
	const FFishSpeciesRow* Species = GetFishTables().FindSpecies(SpeciesId);
	return (Species && FMath::IsFinite(Species->ReferenceWeight) && Species->ReferenceWeight > 0.0f) ? Species->ReferenceWeight : 0.0f;
}

UStreamableRenderAsset* ULureCatchSubsystem::LoadSpeciesMesh(FName SpeciesId)
{
	if (const FFishSpeciesRow* Species = GetFishTables().FindSpecies(SpeciesId))
	{
		if (UStreamableRenderAsset* Mesh = Cast<UStreamableRenderAsset>(LureCatchSubsystemPrivate::LoadIfExists(Species->Mesh.ToSoftObjectPath())))
		{
			return Mesh;
		}
	}
	if (SpeciesId.IsNone())
	{
		return nullptr;
	}
	for (const FString& Pattern : GetDefault<ULureCatchSettings>()->FishMeshPaths)
	{
		const FString Path = Pattern.Replace(TEXT("{Species}"), *SpeciesId.ToString());
		if (UStreamableRenderAsset* Mesh = Cast<UStreamableRenderAsset>(LureCatchSubsystemPrivate::LoadIfExists(FSoftObjectPath(Path))))
		{
			if (Mesh->IsA<UStaticMesh>() || Mesh->IsA<USkeletalMesh>())
			{
				return Mesh;
			}
		}
	}
	return nullptr;
}

void ULureCatchSubsystem::OfferLandedVisual(AActor* Visual, const FFishInstance& Fish)
{
	UWorld* World = GetWorld();
	if (!IsValid(Visual) || !World || !Fish.IsValid())
	{
		return;
	}
	// Already here (the server, or a client that got the item first): hand it over now.
	for (ALureCarryableItem* Item : GetItems())
	{
		ALureFishItem* FishItem = Cast<ALureFishItem>(Item);
		if (FishItem && !FishItem->GetAdoptedVisual() && FishItem->GetFish().Seed == Fish.Seed && FishItem->GetFish().SpeciesId == Fish.SpeciesId
			&& FishItem->GetFish().RarityId == Fish.RarityId && FishItem->AdoptVisual(Visual))
		{
			return;
		}
	}
	FLandedVisualOffer Offer;
	Offer.Visual = Visual;
	Offer.Seed = Fish.Seed;
	Offer.SpeciesId = Fish.SpeciesId;
	Offer.RarityId = Fish.RarityId;
	Offer.Time = World->GetTimeSeconds();
	LandedOffers.Add(Offer);
	World->GetTimerManager().SetTimer(LandedOfferTimer, FTimerDelegate::CreateUObject(this, &ULureCatchSubsystem::PurgeLandedOffers), LandedVisualTimeout, false);
}

AActor* ULureCatchSubsystem::ClaimLandedVisual(const FFishInstance& Fish)
{
	for (int32 Index = 0; Index < LandedOffers.Num(); ++Index)
	{
		const FLandedVisualOffer& Offer = LandedOffers[Index];
		if (Offer.Seed == Fish.Seed && Offer.SpeciesId == Fish.SpeciesId && Offer.RarityId == Fish.RarityId)
		{
			AActor* Visual = Offer.Visual.Get();
			LandedOffers.RemoveAt(Index);
			return IsValid(Visual) ? Visual : nullptr;
		}
	}
	return nullptr;
}

void ULureCatchSubsystem::PurgeLandedOffers()
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	for (int32 Index = LandedOffers.Num() - 1; Index >= 0; --Index)
	{
		if (Now - LandedOffers[Index].Time >= LandedVisualTimeout - 0.01 || !LandedOffers[Index].Visual.IsValid())
		{
			if (AActor* Visual = LandedOffers[Index].Visual.Get())
			{
				Visual->Destroy();
			}
			LandedOffers.RemoveAt(Index);
		}
	}
	if (LandedOffers.Num() > 0 && World)
	{
		GetWorld()->GetTimerManager().SetTimer(LandedOfferTimer, FTimerDelegate::CreateUObject(this, &ULureCatchSubsystem::PurgeLandedOffers), 1.0f, false);
	}
}

void ULureCatchSubsystem::RegisterItem(ALureCarryableItem* Item)
{
	if (!Item)
	{
		return;
	}
	Items.RemoveAll([](const TWeakObjectPtr<ALureCarryableItem>& Entry) { return !Entry.IsValid(); });
	Items.AddUnique(Item);
}

void ULureCatchSubsystem::UnregisterItem(ALureCarryableItem* Item)
{
	Items.RemoveAll([Item](const TWeakObjectPtr<ALureCarryableItem>& Entry) { return !Entry.IsValid() || Entry.Get() == Item; });
}

TArray<ALureCarryableItem*> ULureCatchSubsystem::GetItems() const
{
	TArray<ALureCarryableItem*> Result;
	for (const TWeakObjectPtr<ALureCarryableItem>& Entry : Items)
	{
		if (ALureCarryableItem* Item = Entry.Get(); IsValid(Item))
		{
			Result.Add(Item);
		}
	}
	return Result;
}

TArray<ALureCoolerActor*> ULureCatchSubsystem::GetCoolersOwnedBy(const APlayerState* PlayerState) const
{
	TArray<ALureCoolerActor*> Result;
	if (!PlayerState)
	{
		return Result;
	}
	for (ALureCarryableItem* Item : GetItems())
	{
		ALureCoolerActor* Cooler = Cast<ALureCoolerActor>(Item);
		if (Cooler && Cooler->GetOwningPlayerState() == PlayerState)
		{
			Result.Add(Cooler);
		}
	}
	return Result;
}

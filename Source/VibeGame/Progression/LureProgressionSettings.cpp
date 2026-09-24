// Lure: progression settings (T-010).

#include "Progression/LureProgressionSettings.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"

ULureProgressionSettings::ULureProgressionSettings()
{
	// Defaults mirror Config/DefaultGame.ini, which is the place to change them.
	PlayerLevelTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_PlayerLevel.DT_PlayerLevel")));
	CoolerTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Cooler.DT_Cooler")));
	MarketTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FishMarket.DT_FishMarket")));
	DefaultCoolerId = TEXT("Starter");
	DefaultMarketId = TEXT("Default");
}

const UDataTable* ULureProgressionSettings::LoadTable(const TSoftObjectPtr<UDataTable>& Ref, const UScriptStruct* ExpectedRow, const TCHAR* Name, FString& OutError)
{
	OutError.Reset();
	if (Ref.IsNull())
	{
		OutError = FString::Printf(TEXT("%s is not set in Project Settings > Game > Progression"), Name);
		return nullptr;
	}
	UDataTable* Table = Ref.Get();
	if (!Table)
	{
		const FString PackageName = Ref.ToSoftObjectPath().GetLongPackageName();
		if (PackageName.IsEmpty() || !FPackageName::DoesPackageExist(PackageName))
		{
			OutError = FString::Printf(TEXT("%s asset '%s' does not exist (import data/tables/%s.csv as a DataTable with row struct %s)"),
				Name, *Ref.ToString(), Name, ExpectedRow ? *ExpectedRow->GetName() : TEXT("?"));
			return nullptr;
		}
		Table = Ref.LoadSynchronous();
	}
	if (!Table)
	{
		OutError = FString::Printf(TEXT("%s asset '%s' failed to load or is not a DataTable"), Name, *Ref.ToString());
		return nullptr;
	}
	if (Table->GetRowStruct() != ExpectedRow)
	{
		OutError = FString::Printf(TEXT("%s asset '%s' uses row struct '%s', expected '%s'"), Name, *Ref.ToString(),
			*GetNameSafe(Table->GetRowStruct()), ExpectedRow ? *ExpectedRow->GetName() : TEXT("?"));
		return nullptr;
	}
	return Table;
}

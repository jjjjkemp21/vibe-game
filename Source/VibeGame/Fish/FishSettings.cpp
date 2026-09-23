// Copyright Epic Games, Inc. All Rights Reserved.

#include "Fish/FishSettings.h"
#include "Fish/FishRoll.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"

UFishSettings::UFishSettings()
{
	SpeciesTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FishSpecies.DT_FishSpecies")));
	RarityTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FishRarity.DT_FishRarity")));
	ModifierTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FishModifier.DT_FishModifier")));
	StatTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FishStat.DT_FishStat")));
}

namespace FishSettingsPrivate
{
	/** Loads one table; never triggers engine load errors for a missing asset (checks the package exists first) */
	static const UDataTable* LoadTable(const TSoftObjectPtr<UDataTable>& Ref, const UScriptStruct* ExpectedRow, const TCHAR* Name, TArray<FString>& Errors)
	{
		if (Ref.IsNull())
		{
			Errors.Add(FString::Printf(TEXT("%s is not set in Project Settings > Game > Fish System"), Name));
			return nullptr;
		}

		UDataTable* Table = Ref.Get();
		if (!Table)
		{
			const FString PackageName = Ref.ToSoftObjectPath().GetLongPackageName();
			if (PackageName.IsEmpty() || !FPackageName::DoesPackageExist(PackageName))
			{
				Errors.Add(FString::Printf(TEXT("%s asset '%s' does not exist (import data/tables/%s.json as a DataTable with row struct %s)"),
					Name, *Ref.ToString(), Name, *ExpectedRow->GetName()));
				return nullptr;
			}
			Table = Ref.LoadSynchronous();
		}
		if (!Table)
		{
			Errors.Add(FString::Printf(TEXT("%s asset '%s' failed to load or is not a DataTable"), Name, *Ref.ToString()));
			return nullptr;
		}
		if (Table->GetRowStruct() != ExpectedRow)
		{
			Errors.Add(FString::Printf(TEXT("%s asset '%s' uses row struct '%s', expected '%s'"), Name, *Ref.ToString(),
				Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("None"), *ExpectedRow->GetName()));
			return nullptr;
		}
		return Table;
	}
}

bool UFishSettings::LoadTables(FFishTables& OutTables, FString& OutError) const
{
	using namespace FishSettingsPrivate;
	TArray<FString> Errors;
	OutTables = FFishTables();
	OutTables.Species = LoadTable(SpeciesTable, FFishSpeciesRow::StaticStruct(), TEXT("DT_FishSpecies"), Errors);
	OutTables.Rarities = LoadTable(RarityTable, FFishRarityRow::StaticStruct(), TEXT("DT_FishRarity"), Errors);
	OutTables.Modifiers = LoadTable(ModifierTable, FFishModifierRow::StaticStruct(), TEXT("DT_FishModifier"), Errors);
	OutTables.Stats = LoadTable(StatTable, FFishStatRow::StaticStruct(), TEXT("DT_FishStat"), Errors);
	OutTables.Tuning = RollTuning;
	OutError = FString::Join(Errors, TEXT("; "));
	return Errors.Num() == 0;
}

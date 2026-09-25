// Lure: day/night settings (T-068a).

#include "Environment/LureDayNightSettings.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"

ULureDayNightSettings::ULureDayNightSettings()
{
	DayCycleTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_DayCycle.DT_DayCycle")));
	DayCycleRow = TEXT("Default");
}

bool ULureDayNightSettings::ResolveRow(const UDataTable* Table, FName RowName, FLureDayCycleRow& OutRow, TArray<FString>& OutProblems)
{
	OutRow = FLureDayClock::GetFallbackRow();
	if (!Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FLureDayCycleRow::StaticStruct()))
	{
		OutProblems.Add(TEXT("no DT_DayCycle table (row struct LureDayCycleRow)"));
		return false;
	}
	const FLureDayCycleRow* Found = Table->FindRow<FLureDayCycleRow>(RowName, TEXT("LureDayNight"), /*bWarnIfRowMissing*/ false);
	if (!Found)
	{
		OutProblems.Add(FString::Printf(TEXT("DT_DayCycle has no row '%s'"), *RowName.ToString()));
		return false;
	}
	if (!FLureDayClock::Validate(*Found, &OutProblems))
	{
		return false;
	}
	OutRow = *Found;
	return true;
}

FLureDayCycleRow ULureDayNightSettings::LoadDayCycleRow()
{
	const ULureDayNightSettings* Settings = GetDefault<ULureDayNightSettings>();
	const UDataTable* Table = nullptr;
	if (!Settings->DayCycleTable.IsNull())
	{
		Table = Settings->DayCycleTable.Get();
		if (!Table)
		{
			// Only load a package that exists (an unimported table must not log a load error in every world).
			const FString Package = Settings->DayCycleTable.ToSoftObjectPath().GetLongPackageName();
			if (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package))
			{
				Table = Settings->DayCycleTable.LoadSynchronous();
			}
		}
	}
	FLureDayCycleRow Row;
	TArray<FString> Problems;
	if (!ResolveRow(Table, Settings->DayCycleRow, Row, Problems))
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogLureDayNight, Warning, TEXT("Day/night: '%s' row '%s' is not usable (%s; source data/tables/DT_DayCycle.json); using the built-in row."),
				*Settings->DayCycleTable.ToString(), *Settings->DayCycleRow.ToString(), *FString::Join(Problems, TEXT("; ")));
		}
	}
	return Row;
}

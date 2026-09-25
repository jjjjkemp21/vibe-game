// Lure: day/night settings (T-068a). Project Settings > Game > Lure Day/Night; stored in Config/DefaultGame.ini.
// Rules: docs/specs/day-night-water.md §3.1 and §4. T-068b adds DT_TimeOfDay here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Environment/LureDayClock.h"
#include "LureDayNightSettings.generated.h"

class UDataTable;

/**
 *  Where the day/night data lives, in [/Script/VibeGame.LureDayNightSettings] (DefaultGame.ini). The table path defaults in
 *  the constructor; no Config/ entry is needed. A missing table or row, or an invalid row, gives the built-in row
 *  (FLureDayClock::GetFallbackRow, equal to the shipped Default row) and one warning per session.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Lure Day/Night"))
class ULureDayNightSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULureDayNightSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** The day cycles (row struct LureDayCycleRow; source data/tables/DT_DayCycle.json). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureDayCycleRow"))
	TSoftObjectPtr<UDataTable> DayCycleTable;

	/** The DT_DayCycle row the clock runs (regions with another day are other rows). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Lure|DayNight")
	FName DayCycleRow;

	/**
	 *  The row RowName of Table (pure; tests pass tables built from the JSON source). False with OutProblems when the table
	 *  or row is missing or the row is invalid; OutRow is then the fallback row.
	 */
	static bool ResolveRow(const UDataTable* Table, FName RowName, FLureDayCycleRow& OutRow, TArray<FString>& OutProblems);

	/** The configured row (loads the table), or the fallback row with one warning per session. */
	static FLureDayCycleRow LoadDayCycleRow();
};

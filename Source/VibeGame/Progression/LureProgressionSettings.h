// Lure: progression settings (T-010).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LureProgressionSettings.generated.h"

class UDataTable;

/**
 *  Project Settings > Game > Progression, saved in [/Script/VibeGame.LureProgressionSettings] in DefaultGame.ini.
 *  Points at the progression DataTables (imported from data/tables/*.csv) and holds the few values that are not rows.
 *  Components take tables injected by tests first and only fall back to these soft pointers.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Progression"))
class ULureProgressionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULureProgressionSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** /Game/Data/DT_PlayerLevel (row struct PlayerLevelRow), source data/tables/DT_PlayerLevel.csv */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Tables", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.PlayerLevelRow"))
	TSoftObjectPtr<UDataTable> PlayerLevelTable;

	/** /Game/Data/DT_Cooler (row struct CoolerRow), source data/tables/DT_Cooler.csv */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Tables", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.CoolerRow"))
	TSoftObjectPtr<UDataTable> CoolerTable;

	/** /Game/Data/DT_FishMarket (row struct FishMarketRow), source data/tables/DT_FishMarket.csv */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Tables", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.FishMarketRow"))
	TSoftObjectPtr<UDataTable> MarketTable;

	/** DT_Cooler row every new player starts with */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Cooler")
	FName DefaultCoolerId;

	/** Emergency cooler size, used only when DT_Cooler itself (or its DefaultCoolerId row) is missing, so the game still works (Warning).
	 *  An unknown row (e.g. an old save) uses the DefaultCoolerId row instead. The one place for this value: not set in DefaultGame.ini,
	 *  and not a copy of the Basic row (its size lives only in data/tables/DT_Cooler.csv). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Cooler", meta=(ClampMin="1"))
	int32 FallbackCoolerSlots = 8;

	/** DT_FishMarket row used by sell points whose MarketId is None. A missing table or row pays multiplier 1 (Warning). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Selling")
	FName DefaultMarketId;

	/** Money a new player starts with */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Money", meta=(ClampMin="0"))
	int32 StartingMoney = 0;

	/** Placeholder text lines (money, level, XP, cooler, the interact prompt) drawn top-left until T-011's HUD exists */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Placeholder UI")
	bool bShowPlaceholderText = true;

	/** Seconds a placeholder notice ("Level up! Level 2", "Sold 2 fish for 48 coins") stays on the owner's HUD */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Placeholder UI", meta=(ClampMin="0.5", Units="s"))
	float NoticeSeconds = 4.0f;

	/**
	 *  Loads a table synchronously (game thread). Null with a readable OutError if it is not set, the asset does not exist
	 *  (checked first, so no engine load errors are logged) or it has another row struct.
	 */
	static const UDataTable* LoadTable(const TSoftObjectPtr<UDataTable>& Ref, const UScriptStruct* ExpectedRow, const TCHAR* Name, FString& OutError);
};

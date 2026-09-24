// Lure: water and hot spot settings (T-027).

#include "Fishing/LureWaterSettings.h"
#include "Engine/DataTable.h"

ULureWaterSettings::ULureWaterSettings()
{
	// Defaults mirror Config/DefaultGame.ini.
	DefaultWaterHabitat = TEXT("Habitat.Shore");
	GapFallbackHabitats = { TEXT("Habitat.Shore"), TEXT("Habitat.Reef") };
	HotSpotTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_HotSpot.DT_HotSpot")));
}

FGameplayTag ULureWaterSettings::GetDefaultWaterHabitatTag() const
{
	return FGameplayTag::RequestGameplayTag(DefaultWaterHabitat, /*ErrorIfNotFound*/ false);
}

TArray<FGameplayTag> ULureWaterSettings::GetGapFallbackTags() const
{
	TArray<FGameplayTag> Tags;
	for (const FName& Name : GapFallbackHabitats)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Name, /*ErrorIfNotFound*/ false);
		if (Tag.IsValid())
		{
			Tags.Add(Tag);
		}
	}
	return Tags;
}

// Lure T-010 independent QA tests (qa-engineer): records progression and cooler events (dynamic delegates need UFUNCTIONs).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "QAProgressionListener.generated.h"

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UQAProgressionListener : public UObject
{
	GENERATED_BODY()

public:

	/** (Old, New) per event, in order */
	TArray<FIntPoint> LevelUps;
	TArray<FIntPoint> LevelChanges;
	TArray<int32> MoneyDeltas;
	TArray<int32> XpDeltas;
	int32 FishLanded = 0;
	int32 CoolerChanges = 0;

	void Listen(ULureProgressionComponent* Progression, ULureCoolerComponent* Cooler = nullptr)
	{
		if (Progression)
		{
			Progression->OnLevelUp.AddDynamic(this, &UQAProgressionListener::HandleLevelUp);
			Progression->OnLevelChanged.AddDynamic(this, &UQAProgressionListener::HandleLevelChanged);
			Progression->OnMoneyChanged.AddDynamic(this, &UQAProgressionListener::HandleMoneyChanged);
			Progression->OnXpChanged.AddDynamic(this, &UQAProgressionListener::HandleXpChanged);
			Progression->OnFishLanded.AddDynamic(this, &UQAProgressionListener::HandleFishLanded);
		}
		if (Cooler)
		{
			Cooler->OnCoolerChanged.AddDynamic(this, &UQAProgressionListener::HandleCoolerChanged);
		}
	}

	int32 TotalEvents() const { return LevelUps.Num() + LevelChanges.Num() + MoneyDeltas.Num() + XpDeltas.Num() + FishLanded; }

	UFUNCTION()
	void HandleLevelUp(ULureProgressionComponent* Progression, int32 OldLevel, int32 NewLevel) { LevelUps.Emplace(OldLevel, NewLevel); }

	UFUNCTION()
	void HandleLevelChanged(ULureProgressionComponent* Progression, int32 OldLevel, int32 NewLevel) { LevelChanges.Emplace(OldLevel, NewLevel); }

	UFUNCTION()
	void HandleMoneyChanged(ULureProgressionComponent* Progression, int32 NewMoney, int32 Delta) { MoneyDeltas.Add(Delta); }

	UFUNCTION()
	void HandleXpChanged(ULureProgressionComponent* Progression, int32 NewTotalXp, int32 Delta) { XpDeltas.Add(Delta); }

	UFUNCTION()
	void HandleFishLanded(ULureProgressionComponent* Progression, const FFishInstance& Fish, const FLureFishLandedResult& Result) { ++FishLanded; }

	UFUNCTION()
	void HandleCoolerChanged(ULureCoolerComponent* Cooler) { ++CoolerChanges; }
};

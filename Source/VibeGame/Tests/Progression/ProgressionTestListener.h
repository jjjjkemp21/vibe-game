// Lure T-010 test helper: records ULureProgressionComponent's events (dynamic delegates need UFUNCTIONs).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Progression/LureProgressionComponent.h"
#include "ProgressionTestListener.generated.h"

UCLASS(Transient, NotBlueprintable, HideDropdown)
class ULureProgressionTestListener : public UObject
{
	GENERATED_BODY()

public:

	/** (OldLevel, NewLevel) of every OnLevelUp / OnLevelChanged, in order */
	TArray<TPair<int32, int32>> LevelUps;
	TArray<TPair<int32, int32>> LevelChanges;
	/** Delta of every OnMoneyChanged / OnXpChanged */
	TArray<int32> MoneyDeltas;
	TArray<int32> XpDeltas;
	int32 FishLanded = 0;
	/** (FishSold, MoneyEarned) of every OnFishSold */
	TArray<TPair<int32, int32>> Sales;

	void Listen(ULureProgressionComponent* Progression)
	{
		Progression->OnLevelUp.AddDynamic(this, &ULureProgressionTestListener::HandleLevelUp);
		Progression->OnLevelChanged.AddDynamic(this, &ULureProgressionTestListener::HandleLevelChanged);
		Progression->OnMoneyChanged.AddDynamic(this, &ULureProgressionTestListener::HandleMoneyChanged);
		Progression->OnXpChanged.AddDynamic(this, &ULureProgressionTestListener::HandleXpChanged);
		Progression->OnFishLanded.AddDynamic(this, &ULureProgressionTestListener::HandleFishLanded);
		Progression->OnFishSold.AddDynamic(this, &ULureProgressionTestListener::HandleFishSold);
	}

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
	void HandleFishSold(ULureProgressionComponent* Progression, int32 FishSold, int32 MoneyEarned) { Sales.Emplace(FishSold, MoneyEarned); }
};

// Lure: fish visual settings (T-029). Project Settings > Game > Lure Fish Visuals ([/Script/VibeGame.LureFishVisualSettings]).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FishVisualSettings.generated.h"

class ALureFightFish;
class UAnimInstance;
class UDataTable;
class USkeletalMesh;

/**
 *  Asset references of the fighting fish. The tuning is the DT_FishVisual row this points at (source
 *  data/tables/DT_FishVisual.json). Every reference is optional at runtime: a missing table uses the built-in row (one
 *  warning), a missing ABP_Fish plays the reference pose, a species without a skinned mesh uses FallbackMesh, and with no
 *  mesh at all nothing is drawn.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Lure Fish Visuals"))
class ULureFishVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULureFishVisualSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** /Game/Data/DT_FishVisual (row struct FishVisualRow), source data/tables/DT_FishVisual.json. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.FishVisualRow"))
	TSoftObjectPtr<UDataTable> VisualTable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data")
	FName VisualRow;

	/** ABP_Fish (child of UFishAnimInstance). Missing = UFishAnimInstance itself (reference pose, no clips). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Skinned fish for a species with no SkeletalMesh (and no skeletal Mesh) in DT_FishSpecies. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<USkeletalMesh> FallbackMesh;

	/** The fighting fish actor (a thin Blueprint child is optional). None = ALureFightFish. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftClassPtr<ALureFightFish> FishActorClass;
};

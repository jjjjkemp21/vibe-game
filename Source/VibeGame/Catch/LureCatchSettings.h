// Lure: catch handling settings (T-030). Project Settings > Game > Catch Handling; stored in Config/DefaultGame.ini.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LureCatchSettings.generated.h"

class ALureCoolerActor;
class ALureFishItem;
class UDataTable;

/**
 *  [/Script/VibeGame.LureCatchSettings] in DefaultGame.ini. The feel numbers are the DT_Catch row and DT_Freshness rows these
 *  point at; here are the tables, the item classes, where held items sit (sockets and offsets the animation-artist's clips
 *  are matched with) and the starter cooler spawn. Rules: docs/specs/catch-handling-rules.md.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catch Handling"))
class ULureCatchSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULureCatchSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ---- Data ----

	/** Catch feel (row struct LureCatchRow; source data/tables/DT_Catch.csv). Missing = built-in row + one warning. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureCatchRow"))
	TSoftObjectPtr<UDataTable> CatchTable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data")
	FName CatchRow;

	/** Freshness (row struct LureFreshnessRow; source data/tables/DT_Freshness.csv). A row named like a species overrides the default row. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureFreshnessRow"))
	TSoftObjectPtr<UDataTable> FreshnessTable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data")
	FName DefaultFreshnessRow;

	/** How the fish inside an open cooler are shown (row struct LureCoolerDisplayRow; source data/tables/DT_CoolerDisplay.json; row = the DT_Cooler row). Missing = the built-in layout. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureCoolerDisplayRow"))
	TSoftObjectPtr<UDataTable> CoolerDisplayTable;

	// ---- Classes (thin Blueprint children may add sounds or effects; no logic) ----

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Classes")
	TSoftClassPtr<ALureFishItem> FishItemClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Classes")
	TSoftClassPtr<ALureCoolerActor> CoolerClass;

	// ---- Fish look ----

	/**
	 *  Where a species' mesh is looked for when its DT_FishSpecies row has no Mesh: {Species} = the species id. The first
	 *  existing asset wins (skeletal or static); none = a placeholder shape. The line hangs from the "Mouth" bone or socket,
	 *  the hand holds the "Grip" bone or socket (fish anim spec art/export/Fish/SK_Fish.anim.md).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fish")
	TArray<FString> FishMeshPaths;

	// ---- Held items: owner's first-person view (the item is drawn as a first-person primitive there) ----

	/** Bone or socket of the first-person arms the held fish's Grip goes to (the HoldFish clip's hand). None or missing = the camera. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Held Fish")
	FName HeldFishSocket;

	/** The fish's Grip relative to that socket (or to the camera without arms), cm and degrees */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Held Fish")
	FVector HeldFishOffset;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Held Fish")
	FRotator HeldFishRotation;

	/** Where the held fish is without arms (tests, a missing mesh): relative to the camera */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Held Fish")
	FVector HeldFishCameraOffset;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Held Fish")
	FRotator HeldFishCameraRotation;

	/** The carried cooler's pivot (bottom center) relative to the first-person arms component (the CarryCooler clip's hands at the handles) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Carried Cooler")
	FVector CarriedCoolerOffset;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Carried Cooler")
	FRotator CarriedCoolerRotation;

	// ---- Held items: what other players see (relative to the holder's capsule center) ----

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Third Person")
	FVector ThirdPersonFishOffset;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Third Person")
	FRotator ThirdPersonFishRotation;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Third Person")
	FVector ThirdPersonCoolerOffset;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Third Person")
	FRotator ThirdPersonCoolerRotation;

	// ---- Starter cooler ----

	/** Give every player a starter cooler (ULureProgressionSettings DefaultCoolerId) when they first spawn at a player start */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Starter Cooler")
	bool bSpawnStarterCooler = true;

	/** Actor tag of a starter cooler spot in the level (optional). Several players' coolers line up along its +Y. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Starter Cooler")
	FName CoolerSpawnTag;

	/** Without a tagged spot: where the cooler goes relative to the player start (its frame, cm) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Starter Cooler")
	FVector StarterCoolerOffset;

	/** Distance between players' starter coolers at one spot, cm */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Starter Cooler", meta=(ClampMin="0"))
	float StarterCoolerSpacing = 90.0f;

	/** Loads a table if its asset exists (game thread); null with a readable OutError otherwise (no engine load errors). */
	static const UDataTable* LoadTable(const TSoftObjectPtr<UDataTable>& Ref, const UScriptStruct* ExpectedRow, const TCHAR* Name, FString& OutError);
};

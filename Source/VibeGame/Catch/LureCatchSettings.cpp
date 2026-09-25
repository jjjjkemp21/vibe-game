// Lure: catch handling settings (T-030).

#include "Catch/LureCatchSettings.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"

ULureCatchSettings::ULureCatchSettings()
{
	// Defaults mirror Config/DefaultGame.ini, which is the place to change them.
	CatchTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Catch.DT_Catch")));
	CatchRow = TEXT("Default");
	FreshnessTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Freshness.DT_Freshness")));
	DefaultFreshnessRow = TEXT("Default");
	CoolerDisplayTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_CoolerDisplay.DT_CoolerDisplay")));

	FishItemClass = ALureFishItem::StaticClass();
	CoolerClass = ALureCoolerActor::StaticClass();

	FishMeshPaths = { TEXT("/Game/Art/Fish/SK_{Species}.SK_{Species}"), TEXT("/Game/Art/Fish/SM_{Species}.SM_{Species}") };

	// First person: the HoldFish attach bone and the right palm's contact under the gills (SK_FPArms.anim.md "HoldFish").
	HeldFishSocket = TEXT("hand_r_fish");
	HeldFishContactPoint = FVector(7.6f, 2.9f, -4.2f);
	HeldFishCameraOffset = FVector(45.0f, 16.0f, -24.0f);
	HeldFishCameraRotation = FRotator(0.0f, -70.0f, 0.0f);
	// First person: the CarryCooler clip's cooler pivot bone (SK_FPArms.anim.md "CarryCooler"); zero relative transform.
	CarriedCoolerSocket = TEXT("cooler");
	// Fallback without that bone: the cooler's top shows at the bottom of a 90 degree view (pivot = bottom center, long side left-right).
	CarriedCoolerOffset = FVector(60.0f, 0.0f, -59.0f);
	CarriedCoolerRotation = FRotator::ZeroRotator;

	// Others see the owner's HoldFish pose (hand_r_fish at frame 0, from the eye): side-on, head right, at hand height.
	ThirdPersonFishOffset = FVector(46.9f, 8.0f, -16.8f);
	ThirdPersonFishRotation = FRotator(5.1f, 79.7f, -15.0f);
	ThirdPersonCoolerOffset = FVector(45.0f, 0.0f, -45.0f);
	ThirdPersonCoolerRotation = FRotator::ZeroRotator;

	CoolerSpawnTag = TEXT("Lure.CoolerSpawn");
	StarterCoolerOffset = FVector(150.0f, 100.0f, 0.0f);
}

const UDataTable* ULureCatchSettings::LoadTable(const TSoftObjectPtr<UDataTable>& Ref, const UScriptStruct* ExpectedRow, const TCHAR* Name, FString& OutError)
{
	OutError.Reset();
	if (Ref.IsNull())
	{
		OutError = FString::Printf(TEXT("%s is not set in Project Settings > Game > Catch Handling"), Name);
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

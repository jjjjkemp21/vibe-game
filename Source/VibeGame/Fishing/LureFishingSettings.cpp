// Lure: fishing settings (T-006).

#include "Fishing/LureFishingSettings.h"
#include "Animation/AnimMontage.h"
#include "Character/LureInputSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"

ULureFishingSettings::ULureFishingSettings()
{
	// Defaults mirror Config/DefaultGame.ini.
	FishingTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Fishing.DT_Fishing")));
	DefaultProfileRow = TEXT("Default");

	RodMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/Props/SM_Rod_Basic.SM_Rod_Basic")));
	BobberMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/Props/SM_Bobber.SM_Bobber")));
	LineMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	LineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
	LineColor = FLinearColor(0.93f, 0.92f, 0.86f); // off-white: reads over dark water and bright sky

	RodAttachBone = TEXT("hand_r_rod");
	RodLineSocket = TEXT("LineTip");
	BobberLineSocket = TEXT("LineAttach");
	RodTipOffsetFromEye = FVector(120.f, 30.f, 40.f);

	FishingSpotTag = TEXT("Lure.FishingSpot");
	WaterTag = TEXT("Lure.Water");
	DefaultBait = TEXT("Bait.Shrimp");
	DefaultRegion = TEXT("Region.Tropical");

	CastKeys = { EKeys::LeftMouseButton, EKeys::Gamepad_RightTrigger };

	// Reel fight and gear (T-007).
	GearTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Gear.DT_Gear")));
	FightPatternTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FightPattern.DT_FightPattern")));
	FishFightTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FishFight.DT_FishFight")));
	FishFightRow = TEXT("Default");
	DefaultLoadout.Rod = TEXT("Rod_Starter");
	DefaultLoadout.Line = TEXT("Line_Mono");
	DefaultLoadout.Hook = TEXT("Hook_Shrimp");
}

#if WITH_EDITOR
void ULureFishingSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Key changes apply to the shared mapping context right away.
	if (ULureInputSubsystem* Input = ULureInputSubsystem::Get())
	{
		Input->RebuildMappings();
	}
}
#endif

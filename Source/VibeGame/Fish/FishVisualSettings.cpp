// Lure: fish visual settings (T-029).

#include "Fish/FishVisualSettings.h"
#include "Animation/AnimInstance.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Fish/LureFightFish.h"

ULureFishVisualSettings::ULureFishVisualSettings()
{
	VisualTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_FishVisual.DT_FishVisual")));
	VisualRow = TEXT("Default");
	AnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Art/Fish/ABP_Fish.ABP_Fish_C")));
	FallbackMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Art/Fish/SK_Bonefish.SK_Bonefish")));
}

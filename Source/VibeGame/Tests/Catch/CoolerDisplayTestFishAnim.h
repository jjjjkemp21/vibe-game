// Lure T-030f test helper: a fish anim instance whose "graph" has every role pin, so the cooler display's ABP path can be
// tested before ABP_Fish has its Curled pin (Project.Catch.Display.PoseThroughFishAnim).

#pragma once

#include "CoreMinimal.h"
#include "Fish/FishAnimInstance.h"
#include "CoolerDisplayTestFishAnim.generated.h"

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UCoolerDisplayTestFishAnim : public UFishAnimInstance
{
	GENERATED_BODY()

public:

	virtual bool CanPlayRole(EFishAnimRole InRole) const override { return true; }
};

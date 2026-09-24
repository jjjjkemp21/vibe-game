// Lure T-030d test (unreal-engineer): the fish in hand is drawn at the fight fish's scale (FFightFishVisual::WeightScale, clamped
// by DT_FishVisual MinScale..MaxScale), so it does not jump in size when it lands.
// Project.Catch.FishScale.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureFishItem.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFishSubsystem.h"

namespace LureCatchFishScaleTest
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchFishScaleHeldMatchesFight, "Project.Catch.FishScale.HeldMatchesFightFish", LCT::Flags)
	bool FCatchFishScaleHeldMatchesFight::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(0.0f, 0.0f, LCT::DockTop));
		ULureFightFishSubsystem* Visuals = ULureFightFishSubsystem::Get(Player);
		if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("fight fish subsystem"), Visuals))
		{
			return false;
		}

		// A 40 kg Bonefish: its raw cube-root scale is well above 1.2, the species' visual MaxScale here.
		const FFishInstance Fish = LCT::MakeFish(TEXT("Bonefish"), 45, 5, 40.0f, 7);
		const float Reference = ULureCatchSubsystem::Get(Player) ? ULureCatchSubsystem::Get(Player)->GetSpeciesReferenceWeight(Fish.SpeciesId) : 0.0f;
		if (!TestTrue(TEXT("Bonefish has a reference weight"), Reference > 0.0f))
		{
			return false;
		}
		const float Raw = FMath::Pow(Fish.WeightKg / Reference, 1.0f / 3.0f);

		// The shipped DT_FishVisual source, its row tightened.
		const TStrongObjectPtr<UDataTable> Table = LCT::Shipped(*this, FFishVisualRow::StaticStruct(), TEXT("DT_FishVisual.json"));
		FFishVisualRow* Shipped = Table.IsValid() ? Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("test")) : nullptr;
		if (!TestNotNull(TEXT("DT_FishVisual row"), Shipped))
		{
			return false;
		}
		Shipped->MinScale = 0.5f;
		Shipped->MaxScale = FMath::Max(0.6f, Raw * 0.5f);
		const FFishVisualRow Row = *Shipped;
		FString Problem;
		const bool bValid = Row.Validate(Problem);
		TestTrue(FString::Printf(TEXT("fixture row is valid (%s)"), *Problem), bValid);
		TestTrue(TEXT("MaxScale is below the raw cube-root scale"), Row.MaxScale < Raw - 0.05f);
		Visuals->SetTables(nullptr, Table.Get());
		TestEqual(TEXT("the subsystem uses the fixture row"), Visuals->GetVisualRow().MaxScale, Row.MaxScale);

		ALureFishItem* Item = W.LandInHand(Player, Fish);
		if (!TestNotNull(TEXT("fish in hand"), Item))
		{
			return false;
		}
		const float Expected = FFightFishVisual::WeightScale(Fish.WeightKg, Reference, Row);
		TestEqual(TEXT("held fish scale = the fight fish's WeightScale"), Item->GetWeightScale(), Expected, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("... which is the species MaxScale (clamped)"), Item->GetWeightScale(), Row.MaxScale, KINDA_SMALL_NUMBER);
		return true;
	}
}

#endif

// T-058a: a tired fish stays upright (Jimmy, 2026-09-24: "do not have the fish lay sideways"). Project.FishVisual.ExhaustedUpright
// Data from the text source data/tables/DT_FishVisual.json (never the binary asset).

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fish/FightFishVisual.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"

namespace LureExhaustedUprightTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualExhaustedUpright, "Project.FishVisual.ExhaustedUpright", Flags)
	bool FFishVisualExhaustedUpright::RunTest(const FString& Parameters)
	{
		FString Text;
		TStrongObjectPtr<UDataTable> Table;
		if (!LureFightQA::ReadSource(*this, TEXT("DT_FishVisual.json"), Text)
			|| !LureFightQA::MakeTableChecked(*this, Table, FFishVisualRow::StaticStruct(), Text, true, TEXT("DT_FishVisual.json")))
		{
			return false;
		}
		const FFishVisualRow* Row = Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("ExhaustedUpright"), false);
		if (!TestNotNull(TEXT("DT_FishVisual.json has the settings' row (Default)"), Row))
		{
			return false;
		}
		TestEqual(TEXT("DT_FishVisual.json ExhaustedRollDeg is 0 (upright)"), Row->ExhaustedRollDeg, 0.f);
		TestEqual(TEXT("the built-in row agrees"), FFishVisualRow::GetFallbackRow().ExhaustedRollDeg, 0.f);

		// The pure facing rule, still and swimming, tired: no roll.
		const FVector Player(0.f, 0.f, 100.f);
		const FVector Fish(1500.f, 300.f, -30.f);
		const FVector Velocities[] = { FVector::ZeroVector, FVector(0.f, 250.f, 0.f), FVector(-120.f, 0.f, -40.f) };
		for (const FVector& Velocity : Velocities)
		{
			const FRotator Tired = FFightFishVisual::FacingRotation(*Row, Velocity, Fish, Player, FRotator::ZeroRotator, true);
			TestEqual(FString::Printf(TEXT("tired, velocity %s: roll 0"), *Velocity.ToCompactString()), static_cast<float>(Tired.Roll), 0.f, 1.e-4f);
		}

		// The actor: a tired fish over many frames stays upright.
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureFightFish* Actor = World.World->SpawnActor<ALureFightFish>(ALureFightFish::StaticClass(), FTransform::Identity, Params);
		if (!TestNotNull(TEXT("fish actor"), Actor))
		{
			return false;
		}
		FLureFightFishSetup Setup;
		Setup.Row = *Row;
		Setup.Fish.SpeciesId = TEXT("Bonefish");
		Setup.Fish.WeightKg = 1.5f;
		Setup.ReferenceWeightKg = 1.5f;
		Actor->Setup(Setup);
		FFightFishView View;
		View.bFighting = true;
		View.FightId = 1;
		View.bExhausted = true;
		View.bHasAuthority = true;
		View.PlayerLocation = Player;
		View.LineEnd = FVector(1500.f, 0.f, 0.f);
		View.WaterZ = 0.f;
		float WorstRoll = 0.f;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			View.LineEnd.X -= 1.5f; // reeled in slowly
			Actor->ApplyView(View, LureFightQA::WorldDt);
			WorstRoll = FMath::Max(WorstRoll, FMath::Abs(static_cast<float>(Actor->GetActorRotation().Roll)));
		}
		TestTrue(FString::Printf(TEXT("tired fish actor never rolls (worst %.2f deg)"), WorstRoll), WorstRoll < 0.5f);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

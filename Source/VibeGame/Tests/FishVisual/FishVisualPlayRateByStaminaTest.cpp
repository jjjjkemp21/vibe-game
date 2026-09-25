// T-059a: while it fights, a fish's clip rate and alpha follow its stamina (effort), not its speed (art S3 gate A:
// reeling a spent fish in made it beat faster than a fresh one). Project.FishVisual.PlayRateByStamina.*
// Data from data/tables/DT_FishVisual.json (never the binary asset).

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fish/FightFishVisual.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"

namespace LurePlayRateByStaminaTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	bool LoadRow(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Table, FFishVisualRow& OutRow)
	{
		FString Text;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_FishVisual.json"), Text)
			|| !LureFightQA::MakeTableChecked(Test, Table, FFishVisualRow::StaticStruct(), Text, true, TEXT("DT_FishVisual.json")))
		{
			return false;
		}
		const FFishVisualRow* Row = Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("PlayRateByStamina"), false);
		if (!Row)
		{
			Test.AddError(TEXT("DT_FishVisual.json has no row named by ULureFishVisualSettings::VisualRow (Default)"));
			return false;
		}
		OutRow = *Row;
		return true;
	}

	FString RoleName(EFishAnimRole Role)
	{
		return StaticEnum<EFishAnimRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	FFightFishAnimInput Fighting(FName Move, float Stamina, float SpeedCmS, float WeightKg)
	{
		FFightFishAnimInput In;
		In.Phase = EFightFishPhase::Fighting;
		In.MoveId = Move;
		In.SecondsSinceHook = 10.f;
		In.SpeedCmS = SpeedCmS;
		In.BodyLengthCm = 55.f;
		In.WeightKg = WeightKg;
		In.ReferenceWeightKg = 1.5f;
		In.Stamina01 = Stamina;
		return In;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualPlayRateByStaminaRules, "Project.FishVisual.PlayRateByStamina.Rules", Flags)
	bool FFishVisualPlayRateByStaminaRules::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		if (!LoadRow(*this, Table, Row))
		{
			return false;
		}
		FString Problem;
		TestTrue(FString::Printf(TEXT("the row is valid (%s)"), *Problem), Row.Validate(Problem));
		TestEqual(TEXT("Sulk holds: SwimIdle (JSON)"), RoleName(FFightFishVisual::RoleForMove(Row, TEXT("Sulk"))), RoleName(EFishAnimRole::SwimIdle));
		TestEqual(TEXT("the built-in row agrees"), RoleName(FFightFishVisual::RoleForMove(FFishVisualRow::GetFallbackRow(), TEXT("Sulk"))), RoleName(EFishAnimRole::SwimIdle));
		TestTrue(TEXT("RoleStaminaRates lists the fight roles"), Row.RoleStaminaRates.Num() >= 6);

		// Every move, several weights: tired < fresh, and the speed never changes the fight rate.
		const float Weights[] = { 0.5f, 1.5f, 6.f };
		const float Speeds[] = { 0.f, 50.f, 300.f, 5000.f };
		for (const FFishMoveAnimRole& Move : Row.MoveRoles)
		{
			for (const float Weight : Weights)
			{
				const float Tired = FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 0.f, 100.f, Weight)).PlayRate;
				const float Fresh = FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 1.f, 100.f, Weight)).PlayRate;
				TestTrue(FString::Printf(TEXT("%s, %.1f kg: rate at stamina 0 (%.3f) < at stamina 1 (%.3f)"), *Move.MoveId.ToString(), Weight, Tired, Fresh), Tired < Fresh);
				const float WeightFactor = FMath::Pow(1.5f / Weight, Row.OtherRateWeightExponent);
				const float Half = FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 0.5f, 100.f, Weight)).PlayRate;
				TestEqual(FString::Printf(TEXT("%s, %.1f kg: rate = weight factor x lerp(TiredRate, FreshRate, 0.5)"), *Move.MoveId.ToString(), Weight), Half,
					WeightFactor * FFightFishVisual::StaminaRate(Row, Move.Role, 0.5f), 1.e-5f);
				for (const float Speed : Speeds)
				{
					TestEqual(FString::Printf(TEXT("%s, %.1f kg, %.0f cm/s: the speed does not change the rate"), *Move.MoveId.ToString(), Weight, Speed),
						FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 0.3f, Speed, Weight)).PlayRate,
						FFightFishVisual::ComputeAnimState(Row, Fighting(Move.MoveId, 0.3f, 100.f, Weight)).PlayRate, 1.e-6f);
				}
			}
		}

		// Art's gate A table: a Bonefish (1.15 at full stamina) running: 0.92 at half, 0.74 at 0.1; resting: 1.03 / 0.94.
		FFightFishAnimInput Bone = Fighting(TEXT("Run"), 0.5f, 60.f, 1.5f);
		Bone.AnimRate = 1.15f;
		TestEqual(TEXT("Bonefish Run at 0.5: 0.92"), FFightFishVisual::ComputeAnimState(Row, Bone).PlayRate, 0.92f, 0.005f);
		Bone.Stamina01 = 0.1f;
		TestEqual(TEXT("Bonefish Run at 0.1: 0.74"), FFightFishVisual::ComputeAnimState(Row, Bone).PlayRate, 0.74f, 0.005f);
		Bone.MoveId = TEXT("Rest");
		Bone.Stamina01 = 0.5f;
		TestEqual(TEXT("Bonefish Rest at 0.5: 1.03"), FFightFishVisual::ComputeAnimState(Row, Bone).PlayRate, 1.035f, 0.006f);

		// Alpha: AnimAmplitude x lerp(0.75, 1, stamina).
		FFightFishAnimInput Amp = Fighting(TEXT("Swim"), 0.f, 100.f, 1.5f);
		Amp.AnimAmplitude = 0.8f;
		TestEqual(TEXT("alpha at stamina 0: 0.75 x AnimAmplitude"), FFightFishVisual::ComputeAnimState(Row, Amp).Amplitude, 0.8f * Row.TiredAmplitudeScale, 1.e-5f);
		TestEqual(TEXT("TiredAmplitudeScale is 0.75 (JSON)"), Row.TiredAmplitudeScale, 0.75f, 1.e-6f);
		Amp.Stamina01 = 1.f;
		TestEqual(TEXT("alpha at stamina 1: AnimAmplitude"), FFightFishVisual::ComputeAnimState(Row, Amp).Amplitude, 0.8f * Row.FreshAmplitudeScale, 1.e-5f);
		Amp.Stamina01 = 0.5f;
		TestEqual(TEXT("alpha at stamina 0.5: 0.875 x AnimAmplitude"), FFightFishVisual::ComputeAnimState(Row, Amp).Amplitude, 0.8f * 0.875f, 1.e-5f);

		// Unchanged: the escape swim's speed rule, the tired (exhausted) calm swim, the flop, the hook-set thrash at full stamina.
		FFightFishAnimInput Escape = Fighting(NAME_None, 0.1f, 94.f, 1.5f);
		Escape.Phase = EFightFishPhase::Escaping;
		Escape.BodyLengthCm = 53.5f;
		const float Hz = FFightFishVisual::TailBeatHz(Row, EFishAnimRole::SwimFast);
		TestEqual(TEXT("escape: speed-matched rate, stamina ignored"), FFightFishVisual::ComputeAnimState(Row, Escape).PlayRate,
			FMath::Clamp(94.f / (Row.StrideBodyLengths * 53.5f * Hz), Row.MinPlayRate, Row.MaxPlayRate), 1.e-4f);
		const float EscapeSlow = FFightFishVisual::ComputeAnimState(Row, Escape).PlayRate;
		Escape.SpeedCmS = 200.f;
		TestTrue(TEXT("escape: faster = faster clip"), FFightFishVisual::ComputeAnimState(Row, Escape).PlayRate > EscapeSlow);
		FFightFishAnimInput Spent = Fighting(TEXT("Run"), 0.01f, 300.f, 1.5f);
		Spent.bExhausted = true;
		const FFishAnimState SpentState = FFightFishVisual::ComputeAnimState(Row, Spent);
		TestEqual(TEXT("exhausted: ExhaustedPlayRate"), SpentState.PlayRate, Row.ExhaustedPlayRate, 1.e-6f);
		TestEqual(TEXT("exhausted: alpha x ExhaustedAmplitudeScale"), SpentState.Amplitude, Row.ExhaustedAmplitudeScale, 1.e-6f);
		FFightFishAnimInput Landed = Fighting(NAME_None, 0.2f, 0.f, 3.f);
		Landed.Phase = EFightFishPhase::Landed;
		TestEqual(TEXT("landed flop: weight rate, stamina ignored"), FFightFishVisual::ComputeAnimState(Row, Landed).PlayRate, FMath::Pow(0.5f, Row.OtherRateWeightExponent), 1.e-5f);
		TestEqual(TEXT("landed flop: alpha 1"), FFightFishVisual::ComputeAnimState(Row, Landed).Amplitude, 1.f, 1.e-6f);
		FFightFishAnimInput Hook = Fighting(TEXT("Run"), 1.f, 400.f, 3.f);
		Hook.SecondsSinceHook = 0.f;
		TestEqual(TEXT("hook-set thrash at full stamina: the weight rate as before"), FFightFishVisual::ComputeAnimState(Row, Hook).PlayRate, FMath::Pow(0.5f, Row.OtherRateWeightExponent), 1.e-5f);
		FFightFishAnimInput Default;
		TestEqual(TEXT("callers that never set it (the held fish) get a fresh fish"), Default.Stamina01, 1.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualPlayRateByStaminaActor, "Project.FishVisual.PlayRateByStamina.AdapterAndActor", Flags)
	bool FFishVisualPlayRateByStaminaActor::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		FFishVisualRow Row;
		LureFightQA::FWorld World;
		if (!LoadRow(*this, Table, Row) || !World.Create(*this))
		{
			return false;
		}

		// The adapter copies the fight's stamina.
		FLureFightNetState Fight;
		Fight.bActive = true;
		Fight.Stamina = 0.3f;
		Fight.LineOut = 1000.f;
		FLureFishingNetState Line;
		Line.State = ELureFishingState::Hooked;
		Line.BobberRest = FVector(1000.f, 0.f, 0.f);
		const FFightFishView FromFight = FFightFishViewAdapter::Make(Fight, Line, FFishInstance(), FVector(0.f, 0.f, 100.f), FVector::ForwardVector, true);
		TestEqual(TEXT("adapter: View.Stamina01 = the fight's stamina"), FromFight.Stamina01, 0.3f, 1.e-6f);
		Fight.Stamina = 7.f;
		TestEqual(TEXT("adapter: clamped to 1"), FFightFishViewAdapter::Make(Fight, Line, FFishInstance(), FVector(0.f, 0.f, 100.f), FVector::ForwardVector, true).Stamina01, 1.f, 1.e-6f);

		// The actor: a spent fish reeled in fast beats slower than a fresh one holding still.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureFightFish* Fish = World.World->SpawnActor<ALureFightFish>(ALureFightFish::StaticClass(), FTransform::Identity, Params);
		if (!TestNotNull(TEXT("fish actor"), Fish))
		{
			return false;
		}
		FLureFightFishSetup Setup;
		Setup.Row = Row;
		Setup.Fish.SpeciesId = TEXT("Bonefish");
		Setup.Fish.WeightKg = 1.5f;
		Setup.ReferenceWeightKg = 1.5f;
		Fish->Setup(Setup);
		FFightFishView View;
		View.bFighting = true;
		View.FightId = 1;
		View.MoveId = TEXT("Swim");
		View.bHasAuthority = true;
		View.PlayerLocation = FVector(0.f, 0.f, 100.f);
		View.LineEnd = FVector(1500.f, 0.f, 0.f);
		View.Stamina01 = 1.f;
		for (int32 Frame = 0; Frame < 120; ++Frame) // past the hook-set thrash, holding still
		{
			Fish->ApplyView(View, LureFightQA::WorldDt);
		}
		const float FreshStill = Fish->GetAnimState().PlayRate;
		View.Stamina01 = 0.1f;
		float SpentFast = 0.f;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			View.LineEnd.X -= 5.f; // reeled in at 300 cm/s
			Fish->ApplyView(View, LureFightQA::WorldDt);
			SpentFast = Fish->GetAnimState().PlayRate;
		}
		TestTrue(FString::Printf(TEXT("fish speed while reeled in (%.0f cm/s)"), Fish->GetFishVelocity().Size()), Fish->GetFishVelocity().Size() > 200.f);
		TestTrue(FString::Printf(TEXT("a spent fish reeled in fast (%.3f) beats slower than a fresh one holding still (%.3f)"), SpentFast, FreshStill), SpentFast < FreshStill);
		TestEqual(TEXT("... at the stamina rule"), SpentFast, FFightFishVisual::StaminaRate(Row, EFishAnimRole::SwimFast, 0.1f), 1.e-4f);
		TestEqual(TEXT("... with alpha lerp(0.75, 1, 0.1)"), Fish->GetAnimState().Amplitude, FMath::Lerp(Row.TiredAmplitudeScale, Row.FreshAmplitudeScale, 0.1f), 1.e-4f);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

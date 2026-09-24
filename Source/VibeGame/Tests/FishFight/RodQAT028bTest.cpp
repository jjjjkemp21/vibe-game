// Lure T-028b independent QA (qa-engineer, 2026-09-23). Black-box checks of the T-028b fight fixes, written apart from the implementer's
// RodFollowUpTest.cpp. Spec: docs/specs/reel-fight-rules.md ("T-028b").
// Project.Fishing.Fight.Rod.QA.T028b.*
// Everything lives in namespace LureQAT028b (unity builds: no file-scope using).

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fish/FishDataValidator.h"

namespace LureQAT028b
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Plays one fight with a fixed input until it ends (or 180 s). Returns the elapsed time, -1 when the fish was not landed. */
	inline float PlayFixed(const FLureFishFightRow& T, const FLureGearStats& Kit, const FLureFightMove& Move, const FLureFightInput& Input, ELureFightOutcome& OutOutcome)
	{
		FLureFightState State;
		FLureFight::Begin(State, LureFightQA::MakeFightFish(4.f, 0.f, 200.f), LureRodQA::OneMove(Move), TEXT("QA_T028b"), Kit, T, 7, 1000.f);
		while (!State.IsOver() && State.Elapsed < 180.f)
		{
			FLureFight::Step(State, Input);
		}
		OutOutcome = State.Outcome;
		return State.Outcome == ELureFightOutcome::Landed ? State.Elapsed : -1.f;
	}

	/**
	 *  O1: with the shipped numbers (PitchDipPower 0.8 > PitchDipPressure 0.5) the rod dipped at the fastest reel gains line slower than the
	 *  spec's skilled pump (rod 60 % back, fastest reel) at every pull; more PitchDipPower never gains more;
	 *  and on a resting fish the dipped-fast fight takes at least 30 % longer than pumping (or never lands).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAT028bDippedFastSlower, "Project.Fishing.Fight.Rod.QA.T028b.DippedFastestIsSlowerThanSkilled", Flags)
	bool FQAT028bDippedFastSlower::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		FLureGearStats Kit = Data.Starter();
		FLureGear::ApplyDragLineCap(Kit, T.DragLineCap);
		const int32 Fastest = FLureFight::NumReelSteps(T) - 1;
		TestNearlyEqual(TEXT("spec: shipped PitchDipPower 0.8"), T.PitchDipPower, 0.8f, 1.0e-4f);
		TestNearlyEqual(TEXT("spec: shipped PitchDipPressure 0.5"), T.PitchDipPressure, 0.5f, 1.0e-4f);

		const float Pulls[] = { 0.5f, 1.f, 2.f, 3.f };
		for (const float Pull : Pulls)
		{
			const float Dipped = FLureFight::LineGainSpeed(Pull, true, Kit, FLureFight::RodFactors(LureRodQA::In(true, -1.f, 0.f, Fastest), nullptr, 1.f, T));
			const float Pump = FLureFight::LineGainSpeed(Pull, true, Kit, FLureFight::RodFactors(LureRodQA::In(true, 0.6f, 0.f, Fastest), nullptr, 1.f, T));
			const float Plain = FLureFight::LineGainSpeed(Pull, true, Kit, FLureFight::RodFactors(LureRodQA::In(true), nullptr, 1.f, T));
			TestTrue(FString::Printf(TEXT("pull %.1f: dipped+fastest %.1f < pump %.1f cm/s"), Pull, Dipped, Pump), Dipped < Pump);
			// Not a spec rule: on a very light pull the fastest step's speed can outgain the plain reel even dipped (pull 0.5: 123.8 vs 112.5).
			AddInfo(FString::Printf(TEXT("pull %.1f: dipped+fastest %.1f vs plain reel %.1f cm/s"), Pull, Dipped, Plain));
			float Previous = TNumericLimits<float>::Max();
			for (const float Power : { 0.5f, 0.65f, 0.8f, 0.95f })
			{
				FLureFishFightRow Row = T;
				Row.PitchDipPower = Power;
				const float Gain = FLureFight::LineGainSpeed(Pull, true, Kit, FLureFight::RodFactors(LureRodQA::In(true, -1.f, 0.f, Fastest), nullptr, 1.f, Row));
				TestTrue(FString::Printf(TEXT("pull %.1f: PitchDipPower %.2f gains %.2f, no more than a smaller power (%.2f)"), Pull, Power, Gain, Previous), Gain <= Previous + 1.0e-4f);
				Previous = Gain;
			}
		}

		const FLureFightMove Resting = LureRodQA::SideMove(TEXT("Resting"), 0.3f, 0.f, 0.f, 0.f);
		ELureFightOutcome PumpOutcome = ELureFightOutcome::None;
		ELureFightOutcome DipOutcome = ELureFightOutcome::None;
		const float PumpTime = PlayFixed(T, Kit, Resting, LureRodQA::In(true, 0.6f, 0.f, Fastest), PumpOutcome);
		const float DipTime = PlayFixed(T, Kit, Resting, LureRodQA::In(true, -1.f, 0.f, Fastest), DipOutcome);
		AddInfo(FString::Printf(TEXT("resting fish, 10 m: pump %s %.1f s; dipped+fastest %s %.1f s"), *LureFightQA::OutcomeName(PumpOutcome), PumpTime, *LureFightQA::OutcomeName(DipOutcome), DipTime));
		if (TestTrue(TEXT("fixture: pumping lands the resting fish"), PumpTime > 0.f))
		{
			TestTrue(FString::Printf(TEXT("dipped+fastest takes >= 30 %% longer (%.1f s vs %.1f s) or never lands"), DipTime, PumpTime), DipTime < 0.f || DipTime >= 1.3f * PumpTime);
		}
		return true;
	}

	/** O2: holding reel at the slowest step with the rod fully dipped never throws the hook, over several fish pulls, 30 s each; the slack timer never runs. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAT028bSlowestDippedKeepsHook, "Project.Fishing.Fight.Rod.QA.T028b.SlowestDippedReelNeverThrowsHook", Flags)
	bool FQAT028bSlowestDippedKeepsHook::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureGearStats Kit = Data.Starter();
		const float MovePulls[] = { 0.f, 0.05f, 0.1f, 0.3f, 0.6f };
		const float BasePulls[] = { 1.f, 4.f, 10.f };
		for (const float BasePull : BasePulls)
		{
			for (const float MovePull : MovePulls)
			{
				FLureFightState State;
				FLureFight::Begin(State, LureFightQA::MakeFightFish(BasePull, 0.f, 1.0e9f), LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Soft"), MovePull, 0.f, 0.f, 0.f)),
					TEXT("QA_T028b"), Kit, T, 11, 3000.f);
				bool bSlackTime = false;
				while (!State.IsOver() && State.Elapsed < 30.f)
				{
					FLureFight::Step(State, LureRodQA::In(true, -1.f, 0.f, 0));
					bSlackTime |= State.SlackTime > 0.f;
				}
				const FString Label = FString::Printf(TEXT("base pull %.0f, move pull %.2f"), BasePull, MovePull);
				TestNotEqual(Label + TEXT(": never throws the hook"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::ThrewHook));
				TestFalse(Label + TEXT(": the slack timer never runs"), bSlackTime);
			}
		}
		return true;
	}

	/** O4: a teleport mid-fight ends it (Lost, Teleported, nothing landed), and the same pawn can hook and fight the next fish. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAT028bTeleport, "Project.Fishing.Fight.Rod.QA.T028b.TeleportEndsFightAndNextFightWorks", Flags)
	bool FQAT028bTeleport::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		FFishInstance Bonefish;
		LureFightQA::FWorld World;
		LureRodQA::FOwner Owner;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 131, Bonefish) || !World.Create(*this))
		{
			return false;
		}
		TStrongObjectPtr<UDataTable> Patterns = LureRodQA::PatternTable(LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f)));
		if (!Owner.Create(*this, World, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get()) || !LureRodQA::HookAndFight(*this, World, Owner.Fishing, Bonefish))
		{
			Owner.Release();
			return false;
		}
		ULureFishingComponent* Fishing = Owner.Fishing;
		int32 Landed = 0;
		Fishing->OnFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, const FFishInstance&) { ++Landed; });
		Fishing->SetReelHeld(true);
		World.Tick(30);
		TestTrue(TEXT("fixture: fighting"), Fishing->GetFightNet().bActive);
		ALurePlayerCharacter* Character = Owner.Character;
		TestTrue(TEXT("a far teleport works"), Character->TeleportTo(Character->GetActorLocation() + FVector(-300.f, 0.f, 0.f), Character->GetActorRotation()));
		World.Tick(30);
		TestFalse(TEXT("the fight is over"), Fishing->GetFightNet().bActive);
		TestEqual(TEXT("result Lost"), LureFightQA::ResultName(Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
		TestTrue(TEXT("reason Teleported"), Fishing->GetNetState().ResultReason == ELureCastBlock::Teleported);
		TestEqual(TEXT("nothing landed, even with reel held for 0.5 s after"), Landed, 0);
		Fishing->SetReelHeld(false);
		World.Tick(5);
		TestTrue(TEXT("the same pawn hooks the next fish"), LureRodQA::HookAndFight(*this, World, Fishing, Bonefish));
		TestTrue(TEXT("... and that fight runs"), Fishing->GetFightNet().bActive);
		Fishing->AuthorityReelIn();
		Owner.Release();
		return true;
	}

	/** O6: exactly FightReelStepBurst (4) step changes apply in one instant, then about FightReelStepsPerSecond (10/s); the last held-back step wins. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAT028bStepRate, "Project.Fishing.Fight.Rod.QA.T028b.ReelStepSpamIsRateLimited", Flags)
	bool FQAT028bStepRate::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		FFishInstance Bonefish;
		LureFightQA::FWorld World;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish) || !World.Create(*this))
		{
			return false;
		}
		TStrongObjectPtr<UDataTable> Patterns = LureRodQA::PatternTable(LureRodQA::OneMove(LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f)));
		ALurePlayerCharacter* Pawn = World.Spawn(LureFightQA::StandAt());
		ULureFishingComponent* Remote = Pawn ? LureFightQA::SetUpFishing(Pawn, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get()) : nullptr;
		if (!TestNotNull(TEXT("a remote (uncontrolled) fisher"), Remote))
		{
			return false;
		}
		World.Tick(5);
		if (!LureRodQA::HookAndFight(*this, World, Remote, Bonefish))
		{
			return false;
		}
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		TestEqual(TEXT("spec: burst 4"), Settings->FightReelStepBurst, 4);
		TestEqual(TEXT("spec: 10 per second"), Settings->FightReelStepsPerSecond, 10.f);
		const uint8 FightId = Remote->GetFightNet().FightId;
		auto Step = [Remote]() { return Remote->GetServerFightInput().ReelStep; };
		auto Send = [Remote, FightId](int32 S) { Remote->AuthoritySetFightInput(FightId, 0.f, 0.f, S); };

		// Ten changes in one instant, each different from the step in use: exactly 4 apply.
		int32 Applied = 0;
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const int32 Before = Step();
			Send(Before == 0 ? 2 : 0);
			Applied += Step() != Before ? 1 : 0;
		}
		TestEqual(TEXT("10 changes at once: exactly the burst (4) applies"), Applied, 4);

		// Held back: several asks, the last one wins after one token (0.1 s).
		const int32 InUse = Step();
		const int32 Want = InUse == 2 ? 1 : 2;
		Send(InUse == 0 ? 1 : 0);
		Send(Want);
		TestEqual(TEXT("over the limit: nothing applies yet"), Step(), InUse);
		World.Tick(FMath::CeilToInt(0.1f / LureFightQA::WorldDt) + 1);
		TestEqual(TEXT("after 0.1 s: the last step asked for"), Step(), Want);

		// Spam for 1 s (a new step every frame, cycling 0-1-2): about 10 changes (between 8 and 11 with the frame rounding).
		World.Tick(60); // refill the bucket fully
		int32 Changes = 0;
		int32 Last = Step();
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Send((Last + 1) % 3);
			World.Tick(1);
			Changes += Step() != Last ? 1 : 0;
			Last = Step();
		}
		TestTrue(FString::Printf(TEXT("a new step every frame for 1 s from a full bucket: %d changes (4 + 10/s: 8..15)"), Changes), Changes >= 8 && Changes <= 15);
		Remote->AuthorityReelIn();
		return true;
	}

	/** O7: text in ANY numeric cell of the shipped DT_FishFight row fails the CSV source check, with one problem naming that column; a bool cell with text fails too. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAT028bCsvEveryCell, "Project.Fishing.Fight.Rod.QA.T028b.TextInEveryFightNumberCellFails", Flags)
	bool FQAT028bCsvEveryCell::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		TArray<FString> Lines;
		Data.FightCsv.ParseIntoArrayLines(Lines, true);
		if (!TestTrue(TEXT("fixture: a header and a row"), Lines.Num() >= 2))
		{
			return false;
		}
		TArray<FString> Header;
		TArray<FString> Row;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		Lines[1].ParseIntoArray(Row, TEXT(","), false);
		int32 Checked = 0;
		for (int32 Index = 1; Index < Header.Num() && Index < Row.Num(); ++Index)
		{
			const FProperty* Prop = FLureFishFightRow::StaticStruct()->FindPropertyByName(FName(*Header[Index].TrimStartAndEnd()));
			const bool bNumeric = Prop && Prop->IsA<FNumericProperty>() && !CastField<FNumericProperty>(Prop)->IsEnum();
			const bool bBool = Prop && Prop->IsA<FBoolProperty>();
			if (!bNumeric && !bBool)
			{
				continue;
			}
			TArray<FString> Bad = Row;
			Bad[Index] = bBool ? TEXT("maybe") : TEXT("abc");
			const FString Csv = FString::Join(Header, TEXT(",")) + TEXT("\n") + FString::Join(Bad, TEXT(",")) + TEXT("\n");
			const TArray<FString> Problems = FFishDataValidator::ValidateCsvSource(Csv, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight"));
			TestTrue(FString::Printf(TEXT("%s = '%s': one problem naming it (%s)"), *Header[Index], *Bad[Index], *FString::Join(Problems, TEXT(" | "))),
				Problems.Num() == 1 && Problems[0].Contains(Header[Index].TrimStartAndEnd()));
			++Checked;
		}
		TestTrue(FString::Printf(TEXT("fixture: many cells checked (%d)"), Checked), Checked >= 20);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

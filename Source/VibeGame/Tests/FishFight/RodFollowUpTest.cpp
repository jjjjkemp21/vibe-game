// Lure T-028b (unreal-engineer, 2026-09-23): the follow-ups of the T-028 senior QA (Saved/AgentLogs/qa/20260923-185837-T028.md, O1-O8).
// Project.Fishing.Fight.Rod.T028b.*
//   O1 a dipped rod is relief, not a way to reel in (DT_FishFight PitchDipPower): the "dipped + fastest reel + hold + steer" posture no
//      longer matches skilled play.   O2 one slack rule (FLureFight::IsSlack): reeling is never slack; the HUD never shows both lines.
//   O4 a teleport ends the fight.   O5 switching pawns ends the old pawn's fight.   O6 the server rate-limits reel-step changes.
//   O7 text in a number cell of a CSV table source fails validation (FFishDataValidator::ValidateCsvSource).
//   O8 Validate requires the default reel step to be speed 1.
// Spec: docs/specs/reel-fight-rules.md ("Rod steering", "T-028b"). Everything lives in namespace LureRodT028b (unity builds: no file-scope using).

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Fish/FishDataValidator.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"

namespace LureRodT028b
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	// =================================================================================================================
	// O1: the balance players (QA's, from RodQABalanceTest.cpp: they read the HUD with a 0.3 s reaction)
	// =================================================================================================================

	enum class EPlayer : uint8 { Advice, DippedFast };

	struct FResult
	{
		ELureFightOutcome Outcome = ELureFightOutcome::None;
		float Elapsed = 0.f;
	};

	/** Advice = GAME_DESIGN's skilled play (rod up and against a run, ease off at 90 % of the bar, reel again under 60 %); DippedFast = the O1 posture. */
	inline FResult Play(FLureFightState State, EPlayer Player)
	{
		const FLureFishFightRow& T = State.Tuning;
		const float Dt = FLureFight::StepSeconds(T);
		constexpr float React = 0.3f;
		const int32 Fastest = FLureFight::NumReelSteps(T) - 1;
		int32 SeenKey = TNumericLimits<int32>::Min();
		float SinceChange = 0.f;
		int32 SeenRun = 0;
		float SinceBar = 1000.f;
		float Bar = 0.f;
		bool bEasing = false;
		while (!State.IsOver() && State.Elapsed < 180.f)
		{
			const int32 Key = (State.bExhausted ? -1 : State.MoveIndex) * 3 + State.RunDir + 1;
			if (Key != SeenKey)
			{
				SeenKey = Key;
				SinceChange = 0.f;
			}
			SinceChange += Dt;
			if (SinceChange >= React - 1.0e-4f)
			{
				SeenRun = State.RunDir;
			}
			SinceBar += Dt;
			if (SinceBar >= React - 1.0e-4f)
			{
				SinceBar = 0.f;
				Bar = State.Tension / FMath::Max(1.0e-3f, State.Gear.LineStrength);
			}
			FLureFightInput Input;
			Input.bReeling = true;
			Input.RodYaw = -static_cast<float>(SeenRun);
			if (Player == EPlayer::Advice)
			{
				bEasing = bEasing ? Bar >= 0.6f : Bar >= 0.9f;
				Input.bReeling = !bEasing;
				Input.RodPitch = SeenRun != 0 ? 0.5f : 0.f;
			}
			else
			{
				Input.RodPitch = -1.f;
				Input.ReelStep = Fastest;
			}
			FLureFight::Step(State, Input);
		}
		return { State.Outcome, State.Elapsed };
	}

	struct FGroup
	{
		int32 Fish = 0;
		int32 Lost[2] = { 0, 0 };
		TArray<float> Times[2];
	};

	inline FGroup RunGroup(const LureFightQA::FFightTables& Data, const FishQA::FTables& Fish, FName Species, FName PatternId, int32 Count, int32 FirstSeed, float Hours)
	{
		FGroup Group;
		const FLureFishFightRow& T = *Data.Tuning();
		FLureGearStats Kit = Data.Starter();
		FLureGear::ApplyDragLineCap(Kit, T.DragLineCap);
		const FLureFightPatternRow* Pattern = Data.Pattern(PatternId);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FFishRollContext Context;
			Context.SpeciesId = Species;
			Context.Seed = FirstSeed + Index;
			Context.RegionTag = LureFightQA::Tag(TEXT("Region.Tropical.PalmKey"));
			Context.TimeOfDayHours = Hours;
			FFishInstance Instance;
			if (!FFishRoll::Roll(Fish.Get(), Context, Instance))
			{
				continue;
			}
			++Group.Fish;
			FLureFightState State;
			FLureFight::Begin(State, FLureFight::MakeFish(Instance, T, 1, GetDefault<UFishSettings>()->LevelScaling), Pattern ? *Pattern : FLureFightPatternRow::GetFallbackPattern(),
				PatternId, Kit, T, FLureFight::FightSeed(Instance.Seed), 1000.f);
			for (int32 P = 0; P < 2; ++P)
			{
				const FResult Result = Play(State, static_cast<EPlayer>(P));
				if (Result.Outcome == ELureFightOutcome::Landed)
				{
					Group.Times[P].Add(Result.Elapsed);
				}
				else
				{
					++Group.Lost[P];
				}
			}
		}
		return Group;
	}

	/** O1 acceptance: the posture loses at least 3x as many fish as skilled play (and at least one), or its median fight is >= 30 % longer. */
	inline void ExpectPostureLoses(FAutomationTestBase& Test, const TCHAR* Species, const FGroup& G)
	{
		const float Skilled = LureRodQA::Median(G.Times[0]);
		const float Posture = LureRodQA::Median(G.Times[1]);
		const bool bLosesMore = G.Lost[1] >= 3 * FMath::Max(1, G.Lost[0]);
		const bool bSlower = G.Times[1].Num() == 0 || Posture >= 1.3f * Skilled;
		Test.AddInfo(FString::Printf(TEXT("%s, %d fish: skilled (advice) lost %d, median %.1f s; posture (dipped + fastest + hold + steer) lost %d, median %.1f s (x%.2f)"),
			Species, G.Fish, G.Lost[0], Skilled, G.Lost[1], Posture, Skilled > 0.f ? Posture / Skilled : 0.f));
		Test.TestTrue(FString::Printf(TEXT("%s: the posture loses >= 3x as many (%d vs %d) or takes >= 30 %% longer (%.1f s vs %.1f s)"), Species, G.Lost[1], G.Lost[0], Posture, Skilled),
			bLosesMore || bSlower);
	}

	/**
	 *  O1: the posture "rod fully dipped, fastest reel, reel held, rod against the runs" (no watching) matched skilled play on the bonefish
	 *  (0 of 200 lost, 7.8 s vs 8.5 s). With PitchDipPower it clearly loses to skilled play on both species, and skilled play stays safe.
	 *  Same fish as QA's balance tests (the roll pipeline, same seeds).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bDippedPosture, "Project.Fishing.Fight.Rod.T028b.DippedFastPostureLosesToSkilledPlay", Flags)
	bool FRodT028bDippedPosture::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const FGroup Bone = RunGroup(Data, Fish, TEXT("Bonefish"), TEXT("Run"), 200, 31000, 12.f);
		const FGroup Snap = RunGroup(Data, Fish, TEXT("CoralSnapper"), TEXT("Dive"), 100, 34000, 20.f);
		TestTrue(TEXT("fixture: both species rolled"), Bone.Fish >= 190 && Snap.Fish >= 95);
		ExpectPostureLoses(*this, TEXT("bonefish"), Bone);
		ExpectPostureLoses(*this, TEXT("snapper"), Snap);
		TestTrue(FString::Printf(TEXT("skilled play stays safe on the bonefish (lost %d, at most 2 %%)"), Bone.Lost[0]), Bone.Lost[0] * 50 <= Bone.Fish);
		TestTrue(FString::Printf(TEXT("skilled play stays safe on the snapper (lost %d, at most 10 %%)"), Snap.Lost[0]), Snap.Lost[0] * 10 <= Snap.Fish);
		return true;
	}

	/**
	 *  O1, the rule: a dipped rod relieves the tension by PitchDipPressure but loses PitchDipPower of its power (more), so it barely works the
	 *  fish; pulled back the power follows the pressure; level is exactly 1 (the neutral rod is still the T-007 fight). Data, not code:
	 *  PitchDipPower = PitchDipPressure gives the T-028 rod back.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bDipIsRelief, "Project.Fishing.Fight.Rod.T028b.DipIsReliefNotReeling", Flags)
	bool FRodT028bDipIsRelief::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureGearStats Starter = Data.Starter();
		TestTrue(FString::Printf(TEXT("shipped: dipping costs more power (%.2f) than it relieves tension (%.2f)"), T.PitchDipPower, T.PitchDipPressure), T.PitchDipPower > T.PitchDipPressure);
		TestTrue(TEXT("level: power share exactly 1"), FLureFight::PitchPower(0.f, T) == 1.f && FLureFight::PitchPower(-0.f, T) == 1.f);
		TestNearlyEqual(TEXT("fully dipped: 1 - PitchDipPower"), FLureFight::PitchPower(-1.f, T), 1.f - T.PitchDipPower, 1.0e-6f);
		TestNearlyEqual(TEXT("half dipped: linear"), FLureFight::PitchPower(-0.5f, T), 1.f - 0.5f * T.PitchDipPower, 1.0e-6f);
		TestNearlyEqual(TEXT("pulled back: the power follows the pressure"), FLureFight::PitchPower(1.f, T), FLureFight::PitchPressure(1.f, T), 1.0e-6f);
		TestEqual(TEXT("NaN pitch: level"), FLureFight::PitchPower(LureRodQA::QNaN(), T), 1.f);
		const FLureRodFactors Neutral = FLureFight::RodFactors(LureRodQA::In(true), nullptr, 1.f, T);
		TestTrue(TEXT("the neutral rod: every factor exactly 1 (the T-007 fight)"), Neutral.Pressure == 1.f && Neutral.Power == 1.f && Neutral.ReelSpeed == 1.f && Neutral.ReelLoad == 1.f);
		const FLureRodFactors Dipped = FLureFight::RodFactors(LureRodQA::In(true, -1.f), nullptr, 1.f, T);
		TestNearlyEqual(TEXT("dipped: the tension is relieved by PitchDipPressure"), Dipped.Pressure, 1.f - T.PitchDipPressure, 1.0e-6f);
		TestNearlyEqual(TEXT("dipped: the rod's power drops by PitchDipPower"), Dipped.Power, 1.f - T.PitchDipPower, 1.0e-6f);

		// The O1 cancellation, on a fish pulling 2 (a Common bonefish swimming): the fastest reel with the rod dipped used to gain as much as the
		// plain reel (power 8 x 0.5 = 4, 180 cm/s x (1 - 2/4) = 90 = 120 x (1 - 2/8)). Now it gains far less.
		const int32 Fastest = FLureFight::NumReelSteps(T) - 1;
		const float Plain = FLureFight::LineGainSpeed(2.f, true, Starter);
		const float DippedFast = FLureFight::LineGainSpeed(2.f, true, Starter, FLureFight::RodFactors(LureRodQA::In(true, -1.f, 0.f, Fastest), nullptr, 1.f, T));
		TestTrue(FString::Printf(TEXT("dipped + fastest gains less than half the plain reel (%.1f vs %.1f cm/s)"), DippedFast, Plain), DippedFast < 0.5f * Plain);
		FLureFishFightRow Old = T;
		Old.PitchDipPower = Old.PitchDipPressure;
		const float OldDippedFast = FLureFight::LineGainSpeed(2.f, true, Starter, FLureFight::RodFactors(LureRodQA::In(true, -1.f, 0.f, Fastest), nullptr, 1.f, Old));
		TestNearlyEqual(TEXT("data: PitchDipPower = PitchDipPressure is the T-028 rod (the cancellation)"), OldDippedFast, Plain, 0.5f);
		FString Problem;
		TestTrue(TEXT("... and that row is valid data: ") + Problem, Old.Validate(Problem));
		FLureFishFightRow TooMuch = T;
		TooMuch.PitchDipPower = 0.96f;
		TestFalse(TEXT("PitchDipPower above 0.95 is invalid (the rod keeps some power)"), TooMuch.Validate(Problem));
		TooMuch.PitchDipPower = -0.1f;
		TestFalse(TEXT("PitchDipPower below 0 is invalid"), TooMuch.Validate(Problem));
		return true;
	}

	// =================================================================================================================
	// O2: one slack rule
	// =================================================================================================================

	/**
	 *  Reeling is never slack, whatever the rod and the reel step do; letting it run under the slack line is. The same rule runs the hook timer
	 *  and the stamina recovery (FLureFight::IsSlack).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bOneSlackRule, "Project.Fishing.Fight.Rod.T028b.OneSlackRule", Flags)
	bool FRodT028bOneSlackRule::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		FLureFishFightRow T = LureFightQA::InstantTuning(*Data.Tuning(), 60);
		T.StaminaRecovery = FMath::Max(0.1f, Data.Tuning()->StaminaRecovery); // the recovery is on, so "no recovery while reeling" means something
		const FLureGearStats Starter = Data.Starter();
		const FLureFightFish Fish = LureFightQA::MakeFightFish(4.f, 0.f, 200.f);
		const float Slack = FLureFight::SlackTension(Fish, T);
		TestFalse(TEXT("IsSlack: reeling at zero tension is not slack"), FLureFight::IsSlack(0.f, true, Fish, T));
		TestTrue(TEXT("IsSlack: letting it run under the slack line is"), FLureFight::IsSlack(Slack * 0.5f, false, Fish, T));
		TestFalse(TEXT("IsSlack: letting it run at the slack line is not"), FLureFight::IsSlack(Slack, false, Fish, T));

		// A soft fish (pull 0.3 x 4): reeling with the rod fully dipped at the slowest step puts the tension under the slack line.
		const FLureFightMove Resting = LureRodQA::SideMove(TEXT("Resting"), 0.3f, 0.f, 0.f, 0.f);
		FLureFightState Reeled;
		FLureFight::Begin(Reeled, Fish, LureRodQA::OneMove(Resting), TEXT("T028b_Slack"), Starter, T, 2, 3000.f);
		bool bEverSlackTime = false;
		bool bRecovered = false;
		float Previous = Reeled.Stamina;
		for (int32 Step = 0; Step < 60 * 20 && !Reeled.IsOver(); ++Step)
		{
			FLureFight::Step(Reeled, LureRodQA::In(true, -1.f, 0.f, 0));
			bEverSlackTime |= Reeled.SlackTime > 0.f;
			bRecovered |= Reeled.Stamina > Previous;
			Previous = Reeled.Stamina;
		}
		TestTrue(FString::Printf(TEXT("fixture: the reeled tension %.3f is under the slack line %.3f"), Reeled.Tension, Slack), Reeled.Tension < Slack);
		TestNotEqual(TEXT("reeling dipped at the slowest step for 20 s: the fish never throws the hook"), LureFightQA::OutcomeName(Reeled.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::ThrewHook));
		TestFalse(TEXT("... the slack timer never ran"), bEverSlackTime);
		TestFalse(TEXT("... and the fish never recovered stamina (not slack for the recovery either)"), bRecovered);

		// Letting the same fish run with the rod dipped: slack; the hook goes after the grace in whole steps, plus one.
		FLureFightState Loose;
		FLureFight::Begin(Loose, Fish, LureRodQA::OneMove(Resting), TEXT("T028b_Slack"), Starter, T, 2, 3000.f);
		int32 Steps = 0;
		while (!Loose.IsOver() && Steps < 60 * 30)
		{
			FLureFight::Step(Loose, LureRodQA::In(false, -1.f));
			++Steps;
		}
		TestEqual(TEXT("letting it run under the slack line: the fish throws the hook"), LureFightQA::OutcomeName(Loose.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::ThrewHook));
		TestEqual(TEXT("... after the grace in whole steps, plus one"), Steps, FMath::RoundToInt(FLureFight::SlackGrace(Starter, T) * 60.f) + 1);
		return true;
	}

	/** A world with the owner (a local player controller possessing a character on the test dock) and a rolled Common Bonefish on one move. */
	struct FScene
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		FFishInstance Bonefish;
		LureFightQA::FWorld World;
		TStrongObjectPtr<UDataTable> Patterns;
		LureRodQA::FOwner Owner;

		~FScene()
		{
			Owner.Release();
		}

		bool Create(FAutomationTestBase& Test, const FLureFightMove& Move)
		{
			if (!Data.Load(Test) || !FishQA::LoadReal(Test, Fish) || !LureFightQA::RollFish(Test, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish) || !World.Create(Test))
			{
				return false;
			}
			Patterns = LureRodQA::PatternTable(LureRodQA::OneMove(Move));
			return Owner.Create(Test, World, Fish, Data.Gear.Get(), Patterns.Get(), Data.Fight.Get());
		}

		bool Hook(FAutomationTestBase& Test)
		{
			return LureRodQA::HookAndFight(Test, World, Owner.Fishing, Bonefish);
		}
	};

	/** The HUD shows one slack story: while reeling (the rod dipped, the slowest step, under the slack line) no slack warning; let go and it shows. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bHudSlack, "Project.Fishing.Fight.Rod.T028b.HudNeverContradictsItself", Flags)
	bool FRodT028bHudSlack::RunTest(const FString& Parameters)
	{
		FScene Scene;
		if (!Scene.Create(*this, LureRodQA::SideMove(TEXT("Soft"), 0.3f, 0.f, 0.f, 0.f)) || !Scene.Hook(*this))
		{
			return false;
		}
		ULureFishingComponent* Fishing = Scene.Owner.Fishing;
		Fishing->StepReelSpeed(-9);
		Scene.Owner.Character->DoLook(0.f, -200.f); // the rod fully dipped
		Fishing->SetReelHeld(true);
		Scene.World.Tick(240);
		const FString Reeling = Fishing->GetStatusText();
		TestTrue(FString::Printf(TEXT("fixture: the rod is dipped (%.2f) at the slowest step (%d)"), Fishing->GetRodAim().Y, Fishing->GetReelStep()), Fishing->GetRodAim().Y <= -0.99f && Fishing->GetReelStep() == 0);
		TestTrue(FString::Printf(TEXT("fixture: the tension %.3f is under the slack line %.3f"), Fishing->GetFightNet().Tension, Fishing->GetFightNet().SlackTension),
			Fishing->GetFightNet().Tension < Fishing->GetFightNet().SlackTension);
		TestTrue(TEXT("reeling for 4 s: the fish is still on"), Fishing->GetFishingState() == ELureFishingState::Hooked && Fishing->GetFightNet().bActive);
		TestTrue(TEXT("HUD: 'Reeling.'"), Reeling.Contains(TEXT("Reeling. Release to let it run.")));
		TestFalse(TEXT("HUD: no slack warning while reeling"), Reeling.Contains(TEXT("Slack line")));
		TestEqual(TEXT("the server's slack progress stays 0 while reeling"), Fishing->GetFightNet().SlackProgress, 0.f);

		Fishing->SetReelHeld(false);
		Scene.World.Tick(90);
		const FString Loose = Fishing->GetStatusText();
		TestTrue(TEXT("let go for 1.5 s: the fish is still on"), Fishing->GetFishingState() == ELureFishingState::Hooked);
		TestTrue(TEXT("HUD: the slack warning"), Loose.Contains(TEXT("!! Slack line: reel or it throws the hook !!")));
		TestTrue(TEXT("HUD: 'Hold Click/RT to reel.'"), Loose.Contains(TEXT("Hold Click/RT to reel.")));
		TestFalse(TEXT("HUD: no 'Reeling.' next to it"), Loose.Contains(TEXT("Reeling.")));
		Fishing->AuthorityReelIn();
		return true;
	}

	// =================================================================================================================
	// O4, O5: a teleport or a pawn switch ends the fight
	// =================================================================================================================

	/** A teleport (TeleportTo) mid-fight: the fish is lost (reason Teleported), nothing lands, the HUD says why. A line with no fish stays out. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bTeleport, "Project.Fishing.Fight.Rod.T028b.TeleportEndsTheFight", Flags)
	bool FRodT028bTeleport::RunTest(const FString& Parameters)
	{
		FScene Scene;
		if (!Scene.Create(*this, LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f)))
		{
			return false;
		}
		ULureFishingComponent* Fishing = Scene.Owner.Fishing;
		ALurePlayerCharacter* Character = Scene.Owner.Character;
		int32 Landed = 0;
		Fishing->OnFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, const FFishInstance&) { ++Landed; });

		// A line out, no fish: a short hop keeps it (the normal line rules decide).
		if (!LureFightQA::CastAndWait(*this, Scene.World, Fishing))
		{
			return false;
		}
		TestTrue(TEXT("a teleport with no fish on"), Character->TeleportTo(Character->GetActorLocation() + FVector(-100.f, 100.f, 0.f), Character->GetActorRotation()));
		Scene.World.Tick(2);
		TestEqual(TEXT("... the line stays out"), LureFightQA::StateName(Fishing->GetFishingState()), LureFightQA::StateName(ELureFishingState::Waiting));

		// A fish on: an editor/test probe (bIsATest) is not a teleport; a real one ends the fight.
		if (!TestTrue(TEXT("hooked"), Fishing->AuthorityHookFish(Scene.Bonefish)))
		{
			return false;
		}
		Scene.World.Tick(2);
		TestTrue(TEXT("the fight runs"), Fishing->GetFightNet().bActive);
		Character->TeleportTo(Character->GetActorLocation() + FVector(0.f, 200.f, 0.f), Character->GetActorRotation(), /*bIsATest*/ true);
		TestTrue(TEXT("a TeleportTo test probe does not end the fight"), Fishing->GetFishingState() == ELureFishingState::Hooked && Fishing->GetFightNet().bActive);
		TestTrue(TEXT("the teleport works"), Character->TeleportTo(Character->GetActorLocation() + FVector(-200.f, 250.f, 0.f), Character->GetActorRotation()));
		const FLureFishingNetState& Net = Fishing->GetNetState();
		TestEqual(TEXT("the teleport ends the fight at once: Idle"), LureFightQA::StateName(Fishing->GetFishingState()), LureFightQA::StateName(ELureFishingState::Idle));
		TestFalse(TEXT("... no fight on"), Fishing->GetFightNet().bActive);
		TestEqual(TEXT("... the fish is lost"), LureFightQA::ResultName(Net.LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
		TestEqual(TEXT("... reason Teleported"), StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Net.ResultReason)),
			StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(ELureCastBlock::Teleported)));
		Scene.World.Tick(2);
		TestTrue(TEXT("HUD: 'The fish got away (teleported).'"), Fishing->GetStatusText().Contains(TEXT("The fish got away (teleported).")));
		TestFalse(TEXT("the look is the view again"), Fishing->IsSteeringRod());
		TestEqual(TEXT("nothing landed"), Landed, 0);
		TestFalse(TEXT("no fish in hand"), Fishing->GetHookedFish().IsValid() || Fishing->GetLastLandedFish().IsValid());
		return true;
	}

	/** The controller takes another pawn while the first one fights: the old pawn's fight ends (reason Unpossessed), nothing lands. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bPawnSwitch, "Project.Fishing.Fight.Rod.T028b.PawnSwitchEndsTheOldFight", Flags)
	bool FRodT028bPawnSwitch::RunTest(const FString& Parameters)
	{
		FScene Scene;
		if (!Scene.Create(*this, LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f)) || !Scene.Hook(*this))
		{
			return false;
		}
		ULureFishingComponent* Old = Scene.Owner.Fishing;
		int32 Landed = 0;
		Old->OnFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, const FFishInstance&) { ++Landed; });
		Scene.Owner.Character->DoLook(40.f, 30.f);
		Scene.World.Tick(5);
		ALurePlayerCharacter* Fresh = Scene.World.Spawn(LureFightQA::StandAt() + FVector(0.f, 200.f, 0.f));
		if (!TestNotNull(TEXT("the new pawn"), Fresh))
		{
			return false;
		}
		LureFightQA::SetUpFishing(Fresh, Scene.Fish, Scene.Data.Gear.Get(), Scene.Patterns.Get(), Scene.Data.Fight.Get());
		Scene.Owner.Repossess(Fresh);
		TestEqual(TEXT("the old pawn's fight ends at once: Idle"), LureFightQA::StateName(Old->GetFishingState()), LureFightQA::StateName(ELureFishingState::Idle));
		TestFalse(TEXT("... no fight on"), Old->GetFightNet().bActive);
		TestEqual(TEXT("... the fish is lost"), LureFightQA::ResultName(Old->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
		TestEqual(TEXT("... reason Unpossessed"), StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Old->GetNetState().ResultReason)),
			StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(ELureCastBlock::Unpossessed)));
		Scene.World.Tick(60);
		TestEqual(TEXT("... and stays Idle (nobody fights it on)"), LureFightQA::StateName(Old->GetFishingState()), LureFightQA::StateName(ELureFishingState::Idle));
		TestEqual(TEXT("nothing landed"), Landed, 0);
		TestFalse(TEXT("the new pawn does not steer"), Scene.Owner.Fishing->IsSteeringRod());
		return true;
	}

	// =================================================================================================================
	// O6: the server rate-limits reel-step changes
	// =================================================================================================================

	/** Up to FightReelStepBurst changes go through at once, then FightReelStepsPerSecond; a change over the limit waits and the last one wins. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bStepRate, "Project.Fishing.Fight.Rod.T028b.ServerRateLimitsReelSteps", Flags)
	bool FRodT028bStepRate::RunTest(const FString& Parameters)
	{
		FScene Scene;
		if (!Scene.Create(*this, LureRodQA::SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f)))
		{
			return false;
		}
		// The server's copy of a remote player (no controller): only the calls below reach its fight input (no owner resends in between).
		ALurePlayerCharacter* Other = Scene.World.Spawn(LureFightQA::StandAt() + FVector(0.f, -250.f, 0.f));
		ULureFishingComponent* Remote = Other ? LureFightQA::SetUpFishing(Other, Scene.Fish, Scene.Data.Gear.Get(), Scene.Patterns.Get(), Scene.Data.Fight.Get()) : nullptr;
		if (!TestNotNull(TEXT("the remote player"), Remote))
		{
			return false;
		}
		Scene.World.Tick(5);
		if (!LureRodQA::HookAndFight(*this, Scene.World, Remote, Scene.Bonefish))
		{
			return false;
		}
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		const int32 Burst = FMath::Max(1, Settings->FightReelStepBurst);
		const float Rate = Settings->FightReelStepsPerSecond;
		TestTrue(FString::Printf(TEXT("shipped: a burst (%d) and a rate (%.1f / s)"), Burst, Rate), Burst >= 2 && Rate > 0.f);
		TestEqual(TEXT("fixture: 3 reel steps"), FLureFight::NumReelSteps(Remote->GetFightTuning()), 3);
		const uint8 FightId = Remote->GetFightNet().FightId;
		auto ServerStep = [Remote]() { return Remote->GetServerFightInput().ReelStep; };
		auto Send = [Remote, FightId](int32 Step, float Pitch = 0.f, float Yaw = 0.f) { Remote->AuthoritySetFightInput(FightId, Pitch, Yaw, Step); };
		const int32 Frames = FMath::CeilToInt(1.f / Rate / LureFightQA::WorldDt) + 1; // one token back

		// A flood of changes in one instant: at most the burst applies.
		int32 Applied = 0;
		int32 Last = ServerStep();
		for (int32 Index = 0; Index < Burst + 6; ++Index)
		{
			Send(Index % 2 == 0 ? 0 : 2);
			Applied += ServerStep() != Last ? 1 : 0;
			Last = ServerStep();
		}
		TestTrue(FString::Printf(TEXT("%d changes in one instant: %d applied (1 to the burst %d)"), Burst + 6, Applied, Burst), Applied >= 1 && Applied <= Burst);
		const int32 InUse = ServerStep();
		const int32 Target = InUse == 0 ? 2 : 0;

		// Over the limit the change waits; the aim never does; the latest held-back step is the one that applies.
		Send(1, 0.5f, -0.5f);
		TestEqual(TEXT("over the limit: the step change waits"), ServerStep(), InUse);
		TestTrue(TEXT("... the aim applies at once (only steps are limited)"), Remote->GetServerFightInput().RodPitch == 0.5f && Remote->GetServerFightInput().RodYaw == -0.5f);
		Send(Target, 0.5f, -0.5f);
		TestEqual(TEXT("... still waiting"), ServerStep(), InUse);
		Scene.World.Tick(Frames);
		TestEqual(TEXT("once the limit allows: the last step asked for wins"), ServerStep(), Target);
		TestEqual(TEXT("... in the fight too"), Remote->GetFightState().ReelStep, Target);

		// Asking for the step in use again drops the waiting change.
		Send(InUse);
		TestEqual(TEXT("over the limit again: waits"), ServerStep(), Target);
		Send(Target);
		Scene.World.Tick(Frames * 3);
		TestEqual(TEXT("asking for the step in use cancels the waiting change"), ServerStep(), Target);

		// Sustained: a change every frame for 2 s reaches the fight at most Burst + rate x 2 s times (60 asked for).
		int32 Changes = 0;
		Last = Remote->GetFightState().ReelStep;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Send(Frame % 2 == 0 ? 0 : 2);
			Scene.World.Tick(1);
			Changes += Remote->GetFightState().ReelStep != Last ? 1 : 0;
			Last = Remote->GetFightState().ReelStep;
		}
		const int32 Max = Burst + FMath::CeilToInt(2.f * Rate) + 1;
		TestTrue(FString::Printf(TEXT("a change every frame for 2 s: %d reel-step changes in the fight (2 to %d)"), Changes, Max), Changes >= 2 && Changes <= Max);
		TestTrue(TEXT("the fight is still on"), Remote->GetFightNet().bActive);
		Remote->AuthorityReelIn();
		return true;
	}

	// =================================================================================================================
	// O7, O8: stricter data validation
	// =================================================================================================================

	/** Text in a number cell (the QA's four cases and more) fails the CSV source check; the shipped DT_FishFight and DT_Gear pass it. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bCsvCells, "Project.Fishing.Fight.Rod.T028b.TextInANumberCellFailsValidation", Flags)
	bool FRodT028bCsvCells::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const TArray<FString> Shipped = FFishDataValidator::ValidateCsvSource(Data.FightCsv, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight"));
		TestEqual(TEXT("the shipped DT_FishFight.csv passes: ") + FString::Join(Shipped, TEXT(" | ")), Shipped.Num(), 0);
		const TArray<FString> Gear = FFishDataValidator::ValidateCsvSource(Data.GearCsv, FLureGearRow::StaticStruct(), TEXT("DT_Gear"));
		TestEqual(TEXT("the shipped DT_Gear.csv passes: ") + FString::Join(Gear, TEXT(" | ")), Gear.Num(), 0);

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
		struct FCase { const TCHAR* Column; const TCHAR* Text; };
		const FCase Cases[] = {
			{ TEXT("SideDrain"), TEXT("fast") }, { TEXT("PitchBackPressure"), TEXT("0.3x") }, { TEXT("RodAimSideDeg"), TEXT("45deg") }, { TEXT("ReelSteps"), TEXT("three") },
			{ TEXT("ReelSpeedMax"), TEXT("") }, { TEXT("SimRate"), TEXT("60.5") }, { TEXT("PullPerStrength"), TEXT("0.2.5") }, { TEXT("ApplyLevelScaling"), TEXT("yes") },
			{ TEXT("PitchDipPower"), TEXT("most") },
		};
		for (const FCase& Case : Cases)
		{
			const int32 Index = Header.IndexOfByKey(FString(Case.Column));
			if (!TestTrue(FString::Printf(TEXT("column %s"), Case.Column), Index != INDEX_NONE && Row.IsValidIndex(Index)))
			{
				continue;
			}
			TArray<FString> Bad = Row;
			Bad[Index] = Case.Text;
			const FString Csv = FString::Join(Header, TEXT(",")) + TEXT("\n") + FString::Join(Bad, TEXT(",")) + TEXT("\n");
			const TArray<FString> Problems = FFishDataValidator::ValidateCsvSource(Csv, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight"));
			TestTrue(FString::Printf(TEXT("%s = '%s' fails validation, naming the column (%s)"), Case.Column, Case.Text, *FString::Join(Problems, TEXT(" | "))),
				Problems.Num() == 1 && Problems[0].Contains(Case.Column));
		}
		// Plain numbers of every shape pass: a sign, no fraction, a fraction.
		{
			TArray<FString> Good = Row;
			Good[Header.IndexOfByKey(FString(TEXT("SideDrain")))] = TEXT("+1");
			Good[Header.IndexOfByKey(FString(TEXT("PitchBackPressure")))] = TEXT(".3");
			const FString Csv = FString::Join(Header, TEXT(",")) + TEXT("\n") + FString::Join(Good, TEXT(",")) + TEXT("\n");
			const TArray<FString> Problems = FFishDataValidator::ValidateCsvSource(Csv, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight"));
			TestEqual(TEXT("'+1' and '.3' are plain numbers: ") + FString::Join(Problems, TEXT(" | ")), Problems.Num(), 0);
		}
		// An unknown column and a short row are reported too.
		{
			const FString Csv = FString::Join(Header, TEXT(",")) + TEXT(",Nope\n") + FString::Join(Row, TEXT(",")) + TEXT(",1\n");
			TestTrue(TEXT("an unknown column is reported"), FFishDataValidator::ValidateCsvSource(Csv, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight")).Num() > 0);
			TArray<FString> Short = Row;
			Short.Pop();
			const FString CsvShort = FString::Join(Header, TEXT(",")) + TEXT("\n") + FString::Join(Short, TEXT(",")) + TEXT("\n");
			TestTrue(TEXT("a row with a missing cell is reported"), FFishDataValidator::ValidateCsvSource(CsvShort, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight")).Num() > 0);
		}
		TestTrue(TEXT("no row struct is reported"), FFishDataValidator::ValidateCsvSource(Data.FightCsv, nullptr, TEXT("DT_FishFight")).Num() > 0);
		return true;
	}

	/** O8: the default reel step must be speed 1 (the T-007 reel): Validate refuses a row where it isn't. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodT028bDefaultStep, "Project.Fishing.Fight.Rod.T028b.DefaultReelStepMustBeSpeedOne", Flags)
	bool FRodT028bDefaultStep::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		FString Problem;
		TestTrue(TEXT("shipped: valid: ") + Problem, T.Validate(Problem));
		struct FCase { const TCHAR* What; int32 Steps; int32 Default; float Min; float Max; bool bValid; };
		const FCase Cases[] = {
			{ TEXT("3 steps 0.5-1.5, default 2 (speed 1)"), 3, 2, 0.5f, 1.5f, true },
			{ TEXT("3 steps 0.6-1.5, default 2 (speed 1.05)"), 3, 2, 0.6f, 1.5f, false },
			{ TEXT("3 steps 0.5-1.5, default 1 (speed 0.5)"), 3, 1, 0.5f, 1.5f, false },
			{ TEXT("3 steps 0.5-1.5, default 3 (speed 1.5)"), 3, 3, 0.5f, 1.5f, false },
			{ TEXT("5 steps 0.6-1.4, default 3 (speed 1)"), 5, 3, 0.6f, 1.4f, true },
			{ TEXT("3 steps 1-2, default 1 (speed 1)"), 3, 1, 1.f, 2.f, true },
			{ TEXT("1 step (always speed 1)"), 1, 1, 0.5f, 1.5f, true },
			{ TEXT("2 steps 0.8-0.9, default 2 (speed 0.9)"), 2, 2, 0.8f, 0.9f, false },
		};
		for (const FCase& Case : Cases)
		{
			FLureFishFightRow Row = T;
			Row.ReelSteps = Case.Steps;
			Row.ReelDefaultStep = Case.Default;
			Row.ReelSpeedMin = Case.Min;
			Row.ReelSpeedMax = Case.Max;
			Problem.Reset();
			const bool bValid = Row.Validate(Problem);
			TestEqual(FString::Printf(TEXT("%s: %s (%s)"), Case.What, Case.bValid ? TEXT("valid") : TEXT("invalid"), *Problem), bValid, Case.bValid);
			if (!Case.bValid)
			{
				TestTrue(TEXT("... the problem names the default reel step"), Problem.Contains(TEXT("default reel step")));
			}
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

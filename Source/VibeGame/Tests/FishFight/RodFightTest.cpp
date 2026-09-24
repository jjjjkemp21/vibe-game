// Lure T-028: mouse-steered rod fight + reel speed (implementer's tests). Project.Fishing.Fight.Rod.*
// Rules: docs/specs/reel-fight-rules.md "Rod steering"; formulas in Fishing/FishFight.h; owner-side helpers in Fishing/LureRodControl.h.
// Tables come from the text sources in data/tables/ (never the binary assets). Worlds, tables and replication through
// FRepLayout reuse the fight QA fixtures (FightQATestUtils.h, read-only here). No file-scope using-directives (unity builds).

#include "FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Character/FPArmsAnimInstance.h"
#include "Character/LureInputSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Fishing/LureRodControl.h"
#include "Game/LureHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"

namespace LureRodFightTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FLureFightInput Rod(bool bReeling, float Pitch = 0.f, float Yaw = 0.f, int32 Step = INDEX_NONE)
	{
		FLureFightInput Input;
		Input.bReeling = bReeling;
		Input.RodPitch = Pitch;
		Input.RodYaw = Yaw;
		Input.ReelStep = Step;
		return Input;
	}

	/** A move that swims sideways: Side -1 = to the left (RandomSide off, so its side sign is +1). */
	FLureFightMove SideMove(const TCHAR* Id, float Pull, float Speed, float Away, float Side, float Duration, float AggressionWeight = 0.f)
	{
		FLureFightMove Move = LureFightQA::MakeMove(Id, Pull, Speed, Away, Duration, false, 1.f, AggressionWeight);
		Move.Side = Side;
		return Move;
	}

	/** A fight with a real (finite) stamina pool on the given moves. */
	FLureFightState FishFight(const FLureFishFightRow& Tuning, const TArray<FLureFightMove>& Moves, const FLureGearStats& Gear, float BasePull, float BaseSpeed,
		float StaminaPool, float StartLineOut = 2000.f, int32 Seed = 77)
	{
		FLureFightState State;
		FLureFight::Begin(State, LureFightQA::MakeFightFish(BasePull, BaseSpeed, StaminaPool), LureFightQA::MakePattern(Moves, Moves[0].Id), TEXT("Rod_Test"),
			Gear, Tuning, Seed, StartLineOut);
		return State;
	}

	void Run(FLureFightState& State, const FLureFightInput& Input, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / FLureFight::StepSeconds(State.Tuning));
		for (int32 Step = 0; Step < Steps && !State.IsOver(); ++Step)
		{
			FLureFight::Step(State, Input);
		}
	}

	/** A real fish's fight: its species pattern, the shipped tuning, a level 1 player. */
	FLureFightState RealFight(const LureFightQA::FFightTables& Data, const FFishInstance& Fish, FName PatternId, const FLureGearStats& Gear, int32 Seed, float StartLineOut = 1000.f)
	{
		FLureFightState State;
		const FLureFishFightRow& Tuning = *Data.Tuning();
		const FLureFightPatternRow* Pattern = Data.Pattern(PatternId);
		FLureFight::Begin(State, FLureFight::MakeFish(Fish, Tuning, 1, GetDefault<UFishSettings>()->LevelScaling), Pattern ? *Pattern : FLureFightPatternRow::GetFallbackPattern(),
			PatternId, Gear, Tuning, Seed, StartLineOut);
		return State;
	}

	/**
	 *  Scripted players (the C++ twin of tools/balance/reel_fight_model.py; 0.3 s reactions):
	 *  Hold reels the whole fight with the neutral rod; Careful is the T-007 tension watcher (reels below 70 %, eases off above 90 %);
	 *  Side holds reel and steers against every sideways run; WrongSide steers WITH the run; Back holds reel with the rod fully back;
	 *  Fast holds reel at the fastest step; Skilled steers against runs, eases on hard moves (rod dipped, slow reel), dips hard when
	 *  the bar passes 85 %, pumps (rod back, fast reel) while the fish rests (until it has once overpowered the line) or is tired,
	 *  reels fast with the rod level through a gentle swim (bar under half, same condition), with the Careful watcher on the reel button.
	 */
	enum class EPlayer : uint8 { Hold, Careful, Side, WrongSide, Back, Fast, Skilled };

	struct FPlay
	{
		ELureFightOutcome Outcome = ELureFightOutcome::None;
		float Elapsed = 0.f;
		float Peak01 = 0.f;
		bool Landed() const { return Outcome == ELureFightOutcome::Landed; }
	};

	FPlay Play(FLureFightState State, EPlayer Player)
	{
		const FLureFishFightRow& T = State.Tuning;
		const float Dt = FLureFight::StepSeconds(T);
		constexpr float React = 0.3f;
		const int32 Fastest = FLureFight::NumReelSteps(T) - 1;
		bool bReel = Player != EPlayer::Careful;
		bool bWatchReel = true;
		float Pitch = 0.f;
		float Yaw = 0.f;
		int32 Step = INDEX_NONE;
		float SinceTension = 1000.f;
		float SinceMove = 0.f;
		int32 SeenKey = TNumericLimits<int32>::Min();
		const FLureFightMove* SeenMove = nullptr;
		int32 SeenDir = 0;
		FPlay Out;
		while (!State.IsOver() && State.Elapsed < 180.f)
		{
			const FLureFightMove* Move = State.bExhausted ? nullptr : State.GetMove();
			const int32 Key = (Move ? State.MoveIndex : -1) * 3 + State.RunDir + 1;
			if (Key != SeenKey)
			{
				SeenKey = Key;
				SinceMove = 0.f;
			}
			SinceMove += Dt;
			if (SinceMove >= React - 1.0e-4f)
			{
				SeenMove = Move;
				SeenDir = State.RunDir;
			}
			SinceTension += Dt;
			const float T01 = State.Tension / FMath::Max(1.0e-3f, State.Gear.LineStrength);
			const bool bDecide = SinceTension >= React - 1.0e-4f;
			if (bDecide)
			{
				SinceTension = 0.f;
				bWatchReel = bWatchReel ? T01 < 0.9f : T01 < 0.7f;
			}
			switch (Player)
			{
			case EPlayer::Careful: bReel = bWatchReel; break;
			case EPlayer::Side: Yaw = -static_cast<float>(SeenDir); break;
			case EPlayer::WrongSide: Yaw = static_cast<float>(SeenDir); break;
			case EPlayer::Back: Pitch = 1.f; break;
			case EPlayer::Fast: Step = Fastest; break;
			case EPlayer::Skilled:
			{
				Yaw = -static_cast<float>(SeenDir);
				const bool bHard = SeenMove && SeenMove->AggressionWeight > 0.f;
				const bool bPump = State.bExhausted || (SeenMove && SeenMove->Rest && Out.Peak01 <= 1.f);
				if (bDecide)
				{
					if (T01 > 0.85f)
					{
						Pitch = -1.f;
						Step = 0;
					}
					else if (T01 < 0.6f)
					{
						// A gentle swim (bar under half, the fish never overpowered the line): reel fast with the rod level.
						const bool bGentle = T01 < 0.5f && Out.Peak01 <= 1.f;
						Pitch = bHard ? -0.5f : (bPump ? 0.6f : 0.f);
						Step = bHard ? 0 : ((bPump || bGentle) ? Fastest : INDEX_NONE);
					}
				}
				else if (bHard && Pitch > -0.5f)
				{
					Pitch = -0.5f;
					Step = 0;
				}
				bReel = bWatchReel;
				break;
			}
			case EPlayer::Hold:
			default:
				break;
			}
			FLureFight::Step(State, Rod(bReel, Pitch, Yaw, Step));
			Out.Peak01 = FMath::Max(Out.Peak01, State.Tension / FMath::Max(1.0e-3f, State.Gear.LineStrength));
		}
		Out.Outcome = State.Outcome;
		Out.Elapsed = State.Elapsed;
		return Out;
	}

	float Median(TArray<float> Values)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	/** Runs the enhanced-input bindings of Action (like the player pressing it). */
	void Fire(const UEnhancedInputComponent* Input, const UInputAction* Action, ETriggerEvent Event)
	{
		struct FInstance : public FInputActionInstance
		{
			FInstance(const UInputAction* InAction, ETriggerEvent InEvent) : FInputActionInstance(InAction)
			{
				TriggerEvent = InEvent;
				Value = FInputActionValue(InEvent != ETriggerEvent::Completed);
			}
		};
		const FInstance Instance(Action, Event);
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				Binding->Execute(Instance);
			}
		}
	}

	/** A standing player with a local controller (the owning client in a standalone world: it is also the server). */
	struct FOwnerRig
	{
		ALurePlayerCharacter* Character = nullptr;
		APlayerController* Controller = nullptr;
		ULureFishingComponent* Fishing = nullptr;

		bool Create(FAutomationTestBase& Test, LureFightQA::FWorld& World, const LureFightQA::FFightTables& Data, const FishQA::FTables& Fish)
		{
			Character = World.Spawn(LureFightQA::StandAt());
			Controller = World.World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("character"), Character) || !Test.TestNotNull(TEXT("controller"), Controller))
			{
				return false;
			}
			Controller->SetAsLocalPlayerController();
			Controller->Possess(Character);
			World.Tick(10);
			Fishing = LureFightQA::SetUpFishing(Character, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
			return Test.TestNotNull(TEXT("fishing"), Fishing);
		}

		void Release()
		{
			if (Controller)
			{
				Controller->UnPossess();
			}
		}
	};

	/** A copy of a character that stands for another machine's view (never moves on its own). */
	void MakeCopy(ALurePlayerCharacter* Character, ENetRole Role)
	{
		Character->GetCharacterMovement()->SetComponentTickEnabled(false);
		Character->SetRole(Role);
	}

// =====================================================================================================================
// The neutral rod is the T-007 fight
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodNeutralIsT007, "Project.Fishing.Fight.Rod.NeutralRodIsTheT007Fight", Flags)
bool FLureRodNeutralIsT007::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	const FLureGearStats Starter = Data.Starter();

	// The shipped default reel step is the T-007 reel (speed 1, cranking load 1), so a player who never touches the wheel fights T-007.
	TestEqual(TEXT("default step (0-based) = ReelDefaultStep - 1"), FLureFight::DefaultReelStep(T), T.ReelDefaultStep - 1);
	TestEqual(TEXT("the shipped default step has speed exactly 1"), FLureFight::ReelStepSpeed(FLureFight::DefaultReelStep(T), T), 1.f);
	TestEqual(TEXT("... and cranking load exactly 1"), FLureFight::ReelStepLoad(FLureFight::DefaultReelStep(T), T), 1.f);

	// Every factor of the neutral rod is exactly neutral, whatever the fish does.
	const FLureFightMove Sideways = SideMove(TEXT("Run"), 2.f, 1.f, 1.f, -0.8f, 1.f, 0.1f);
	for (const FLureFightMove* Move : { &Sideways, static_cast<const FLureFightMove*>(nullptr) })
	{
		for (const float SideSign : { -1.f, 1.f })
		{
			const FLureRodFactors F = FLureFight::RodFactors(FLureFightInput(), Move, SideSign, T);
			TestTrue(FString::Printf(TEXT("neutral rod (move %s, side %+.0f): every factor is 1 and the side score 0"), Move ? TEXT("sideways") : TEXT("none"), SideSign),
				F.Pressure == 1.f && F.Side == 0.f && F.Power == 1.f && F.ReelSpeed == 1.f && F.ReelLoad == 1.f && F.Pull == 1.f && F.MoveClock == 1.f && F.Drain == 1.f);
		}
	}

	// The step functions with the neutral factors = the T-007 functions (no factors), on a grid of pulls.
	const FLureRodFactors Neutral = FLureFight::RodFactors(FLureFightInput(), &Sideways, 1.f, T);
	bool bSame = true;
	for (const float Pull : { 0.f, 0.7f, 2.5f, 6.f, 8.f, 13.f, 40.f })
	{
		for (const bool bReel : { false, true })
		{
			bSame &= FLureFight::TargetTension(Pull, bReel, Starter, T) == FLureFight::TargetTension(Pull, bReel, Starter, T, Neutral);
			bSame &= FLureFight::LineGainSpeed(Pull, bReel, Starter) == FLureFight::LineGainSpeed(Pull, bReel, Starter, Neutral);
			bSame &= FLureFight::LineTakenSpeed(Pull, 90.f, bReel, Starter, T) == FLureFight::LineTakenSpeed(Pull, 90.f, bReel, Starter, T, Neutral);
		}
	}
	TestTrue(TEXT("tension target, line gain and line taken: the neutral rod changes nothing (bit for bit)"), bSame);

	// A whole real fight: the default input and the explicit neutral input (level, centered, the default step) are the same fight.
	FFishInstance Bonefish;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.4f, 91, Bonefish))
	{
		return false;
	}
	for (const bool bReel : { true, false })
	{
		FLureFightState A = RealFight(Data, Bonefish, TEXT("Run"), Starter, 5);
		FLureFightState B = RealFight(Data, Bonefish, TEXT("Run"), Starter, 5);
		bool bEqual = true;
		for (int32 Step = 0; Step < 60 * 40 && !A.IsOver(); ++Step)
		{
			FLureFight::Step(A, Rod(bReel));
			FLureFight::Step(B, Rod(bReel, 0.f, 0.f, FLureFight::DefaultReelStep(T)));
			bEqual &= A.Tension == B.Tension && A.LineOut == B.LineOut && A.Stamina == B.Stamina && A.MoveIndex == B.MoveIndex && A.SideDeg == B.SideDeg;
		}
		TestTrue(FString::Printf(TEXT("%s: INDEX_NONE step = the default step, same fight to the end (%s)"), bReel ? TEXT("reeling") : TEXT("letting it run"),
			*LureFightQA::OutcomeName(A.Outcome)), bEqual && A.Outcome == B.Outcome);
		TestEqual(TEXT("the state records the default step"), A.ReelStep, FLureFight::DefaultReelStep(T));
	}
	return true;
}

// =====================================================================================================================
// Rod pitch scales the tension (pull back: more, dip: less), the drag still caps a running fish
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodPitchScalesTension, "Project.Fishing.Fight.Rod.PitchScalesTension", Flags)
bool FLureRodPitchScalesTension::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	const FLureGearStats Starter = Data.Starter();
	TestTrue(TEXT("shipped: pulling back adds pressure, dipping removes some"), T.PitchBackPressure > 0.f && T.PitchDipPressure > 0.f && T.PitchDipPressure < 1.f);

	// P(pitch).
	TestEqual(TEXT("level: P = 1"), FLureFight::PitchPressure(0.f, T), 1.f);
	TestNearlyEqual(TEXT("fully back: P = 1 + PitchBackPressure"), FLureFight::PitchPressure(1.f, T), 1.f + T.PitchBackPressure, 1.0e-6f);
	TestNearlyEqual(TEXT("fully dipped: P = 1 - PitchDipPressure"), FLureFight::PitchPressure(-1.f, T), 1.f - T.PitchDipPressure, 1.0e-6f);
	TestNearlyEqual(TEXT("half back: linear"), FLureFight::PitchPressure(0.5f, T), 1.f + 0.5f * T.PitchBackPressure, 1.0e-6f);
	TestNearlyEqual(TEXT("half dipped: linear"), FLureFight::PitchPressure(-0.5f, T), 1.f - 0.5f * T.PitchDipPressure, 1.0e-6f);
	TestNearlyEqual(TEXT("beyond the range clamps (a client sends 5)"), FLureFight::PitchPressure(5.f, T), 1.f + T.PitchBackPressure, 1.0e-6f);
	TestEqual(TEXT("NaN = level"), FLureFight::PitchPressure(std::numeric_limits<float>::quiet_NaN(), T), 1.f);

	// Step 4: the tension target, reeling = the T-007 target x P; letting it run = min(pull x P, drag).
	const float Pull = 4.f;
	for (const float Pitch : { -1.f, -0.5f, 0.f, 0.5f, 1.f })
	{
		const FLureRodFactors F = FLureFight::RodFactors(Rod(true, Pitch), nullptr, 1.f, T);
		TestNearlyEqual(FString::Printf(TEXT("reeling, pitch %+.1f: target = (Pull x ReelStrain + RodPower x ReelLoad) x P"), Pitch),
			FLureFight::TargetTension(Pull, true, Starter, T, F), FLureFight::TargetTension(Pull, true, Starter, T) * FLureFight::PitchPressure(Pitch, T), 1.0e-4f);
		TestNearlyEqual(FString::Printf(TEXT("letting it run, pitch %+.1f: target = min(Pull x P, Drag)"), Pitch),
			FLureFight::TargetTension(Pull, false, Starter, T, F), FMath::Min(Pull * FLureFight::PitchPressure(Pitch, T), Starter.Drag), 1.0e-4f);
		TestNearlyEqual(FString::Printf(TEXT("pitch %+.1f: the rod's power x P"), Pitch), F.Power, FLureFight::PitchPressure(Pitch, T), 1.0e-6f);
	}
	const FLureRodFactors Back = FLureFight::RodFactors(Rod(true, 1.f), nullptr, 1.f, T);
	const FLureRodFactors Dipped = FLureFight::RodFactors(Rod(true, -1.f), nullptr, 1.f, T);
	TestEqual(TEXT("letting a strong fish run with the rod fully back: never above the drag"), FLureFight::TargetTension(30.f, false, Starter, T, Back), Starter.Drag);
	TestTrue(TEXT("pulling back gains more line on the same fish"), FLureFight::LineGainSpeed(Pull, true, Starter, Back) > FLureFight::LineGainSpeed(Pull, true, Starter));
	TestTrue(TEXT("dipping gains less"), FLureFight::LineGainSpeed(Pull, true, Starter, Dipped) < FLureFight::LineGainSpeed(Pull, true, Starter));
	TestTrue(TEXT("reeling dipped: a fish below the rod's power but above its dipped power takes line (you give it line)"),
		FLureFight::LineTakenSpeed(6.f, 100.f, true, Starter, T) == 0.f && FLureFight::LineTakenSpeed(6.f, 100.f, true, Starter, T, Dipped) > 0.f);

	// In the fight: same fish, same seed, reeling 2 s: back > level > dipped in tension, and the fish tires fastest under the most tension.
	const FLureFightMove Swim = LureFightQA::MakeMove(TEXT("Swim"), 1.f, 0.f, 0.f);
	float Tension[3];
	float Stamina[3];
	const float Pitches[3] = { 1.f, 0.f, -1.f };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FLureFightState Steady = FishFight(T, { Swim }, Starter, 4.f, 0.f, 1.0e9f); // never tires: the tension alone
		Run(Steady, Rod(true, Pitches[Index]), 2.f);
		Tension[Index] = Steady.Tension;
		FLureFightState Tiring = FishFight(T, { Swim }, Starter, 4.f, 0.f, 200.f);
		Run(Tiring, Rod(true, Pitches[Index]), 2.f);
		Stamina[Index] = Tiring.Stamina;
	}
	TestTrue(FString::Printf(TEXT("tension: back %.2f > level %.2f > dipped %.2f"), Tension[0], Tension[1], Tension[2]), Tension[0] > Tension[1] && Tension[1] > Tension[2]);
	TestNearlyEqual(TEXT("... back settles at level x (1 + PitchBackPressure)"), Tension[0], Tension[1] * (1.f + T.PitchBackPressure), 1.0e-3f * Tension[1]);
	TestNearlyEqual(TEXT("... dipped at level x (1 - PitchDipPressure)"), Tension[2], Tension[1] * (1.f - T.PitchDipPressure), 1.0e-3f * Tension[1]);
	TestTrue(FString::Printf(TEXT("stamina after 2 s: back %.3f < level %.3f < dipped %.3f (the fish works against the tension)"), Stamina[0], Stamina[1], Stamina[2]),
		Stamina[0] < Stamina[1] && Stamina[1] < Stamina[2]);

	// Letting a fish run that pulls twice the line, with the rod fully back for 10 s: the drag holds the line under its strength.
	// (Drag at the DragLineCap, above this fish's slack line, so the only question is the snap.)
	{
		FLureGearStats Capped = Starter;
		Capped.Drag = Starter.LineStrength * T.DragLineCap;
		FLureFightState State = LureFightQA::SteadyFight(LureFightQA::InstantTuning(T, T.SimRate), 2.f * Starter.LineStrength, Swim, Capped);
		Run(State, Rod(false, 1.f), 10.f);
		TestEqual(TEXT("rod back, not reeling: no snap in 10 s"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::None));
		TestTrue(FString::Printf(TEXT("... the tension stays at the drag (%.2f)"), State.Tension), State.Tension <= Capped.Drag + 1.0e-3f);
	}
	return true;
}

// =====================================================================================================================
// Side pressure against the run turns the fish; the same side loses line
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodSidePressure, "Project.Fishing.Fight.Rod.SidePressureAgainstTheRun", Flags)
bool FLureRodSidePressure::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	const FLureGearStats Starter = Data.Starter();

	// Which way the fish runs: its move's Side x its random side, from SideMinShare up.
	const FLureFightMove Left = SideMove(TEXT("Left"), 1.f, 1.f, 1.f, -0.5f, 10.f);
	const FLureFightMove Right = SideMove(TEXT("Right"), 1.f, 1.f, 1.f, 0.3f, 10.f);
	const FLureFightMove Slight = SideMove(TEXT("Slight"), 1.f, 1.f, 1.f, 0.5f * T.SideMinShare, 10.f);
	const FLureFightMove Straight = SideMove(TEXT("Straight"), 1.f, 1.f, 1.f, 0.f, 10.f);
	TestEqual(TEXT("Side -0.5: runs left"), FLureFight::RunDirection(&Left, 1.f, T), -1);
	TestEqual(TEXT("Side +0.3: runs right"), FLureFight::RunDirection(&Right, 1.f, T), 1);
	TestEqual(TEXT("Side +0.3 on its random left side: runs left"), FLureFight::RunDirection(&Right, -1.f, T), -1);
	TestEqual(TEXT("Side below SideMinShare: no side"), FLureFight::RunDirection(&Slight, 1.f, T), 0);
	TestEqual(TEXT("Side 0 (straight away, dives, rests): no side"), FLureFight::RunDirection(&Straight, 1.f, T), 0);
	TestEqual(TEXT("an exhausted fish (no move): no side"), FLureFight::RunDirection(nullptr, 1.f, T), 0);

	// The side score: +1 = rod fully against the run, -1 = fully with it.
	TestEqual(TEXT("fish runs left, rod right: +1 (against)"), FLureFight::SideScore(1.f, -1), 1.f);
	TestEqual(TEXT("fish runs left, rod left: -1 (with it)"), FLureFight::SideScore(-1.f, -1), -1.f);
	TestEqual(TEXT("fish runs right, rod left: +1"), FLureFight::SideScore(-1.f, 1), 1.f);
	TestEqual(TEXT("half right against a left run: +0.5"), FLureFight::SideScore(0.5f, -1), 0.5f);
	TestEqual(TEXT("no run: 0 whatever the rod"), FLureFight::SideScore(1.f, 0), 0.f);

	// The factors.
	const FLureRodFactors Against = FLureFight::RodFactors(Rod(true, 0.f, 1.f), &Left, 1.f, T);
	const FLureRodFactors With = FLureFight::RodFactors(Rod(true, 0.f, -1.f), &Left, 1.f, T);
	TestNearlyEqual(TEXT("against: rod power x (1 + SideLeverage)"), Against.Power, 1.f + T.SideLeverage, 1.0e-6f);
	TestNearlyEqual(TEXT("against: fish pull x (1 - SideTurnPull)"), Against.Pull, 1.f - T.SideTurnPull, 1.0e-6f);
	TestNearlyEqual(TEXT("against: its move clock x (1 + SideTurnRate)"), Against.MoveClock, 1.f + T.SideTurnRate, 1.0e-6f);
	TestNearlyEqual(TEXT("against: its stamina drain x (1 + SideDrain)"), Against.Drain, 1.f + T.SideDrain, 1.0e-6f);
	TestNearlyEqual(TEXT("with it: rod power x (1 - SideLeverage)"), With.Power, 1.f - T.SideLeverage, 1.0e-6f);
	TestTrue(TEXT("with it: no turning (pull, clock, drain unchanged)"), With.Pull == 1.f && With.MoveClock == 1.f && With.Drain == 1.f);

	// In the fight: a fish running left for a long time, reeling 3 s with the rod against it, centered, and with it.
	const FLureFightMove LeftRun = SideMove(TEXT("LeftRun"), 1.6f, 1.f, 1.f, -1.f, 100.f);
	struct FResult { float Stamina; float LineOut; float Tension; float SideDeg; };
	auto Fight3s = [&](float Yaw)
	{
		FLureFightState State = FishFight(T, { LeftRun }, Starter, 4.f, 60.f, 200.f);
		Run(State, Rod(true, 0.f, Yaw), 3.f);
		TestEqual(FString::Printf(TEXT("yaw %+.0f: the fish runs left (RunDir -1)"), Yaw), State.RunDir, -1);
		TestNearlyEqual(FString::Printf(TEXT("yaw %+.0f: the state's side score"), Yaw), State.Side, FLureFight::SideScore(Yaw, -1), 1.0e-6f);
		return FResult{ State.Stamina, State.LineOut, State.Tension, State.SideDeg };
	};
	const FResult Opp = Fight3s(1.f);
	const FResult Mid = Fight3s(0.f);
	const FResult Same = Fight3s(-1.f);
	TestTrue(FString::Printf(TEXT("against the run the fish tires faster (stamina %.3f < %.3f)"), Opp.Stamina, Mid.Stamina), Opp.Stamina < Mid.Stamina);
	TestTrue(FString::Printf(TEXT("against the run you gain line (%.0f < %.0f cm out)"), Opp.LineOut, Mid.LineOut), Opp.LineOut < Mid.LineOut);
	TestTrue(FString::Printf(TEXT("with the run you lose ground (%.0f > %.0f cm out)"), Same.LineOut, Mid.LineOut), Same.LineOut > Mid.LineOut);
	TestTrue(FString::Printf(TEXT("the turned fish pulls less: lower tension (%.2f < %.2f)"), Opp.Tension, Mid.Tension), Opp.Tension < Mid.Tension);
	TestTrue(FString::Printf(TEXT("cosmetic: centered, the fish swings left (%.2f deg)"), Mid.SideDeg), Mid.SideDeg < 0.f);
	TestTrue(FString::Printf(TEXT("cosmetic: turned, it swings back right (%.2f deg)"), Opp.SideDeg), Opp.SideDeg > 0.f);

	// Turning ends the run sooner: a 2 s left run then a rest; fully against it the run lasts 2 / (1 + SideTurnRate) s.
	{
		const FLureFightMove Burst = SideMove(TEXT("Burst"), 1.f, 1.f, 1.f, -1.f, 2.f);
		FLureFightMove Rest = LureFightQA::MakeMove(TEXT("Rest"), 0.3f, 0.f, 0.f, 50.f, true, 0.f);
		Rest.Weight = 1.f;
		for (const float Yaw : { 0.f, 1.f })
		{
			FLureFightPatternRow Pattern = LureFightQA::MakePattern({ Burst, Rest }, TEXT("Burst"));
			Pattern.Moves[0].Weight = 0.f; // after the opening burst, only the rest can be picked
			FLureFightState State;
			FLureFight::Begin(State, LureFightQA::MakeFightFish(3.f, 60.f, 1.0e6f), Pattern, TEXT("Rod_Turn"), Starter, T, 3, 2000.f);
			float Ended = -1.f;
			for (int32 Step = 0; Step < 60 * 4 && Ended < 0.f; ++Step)
			{
				FLureFight::Step(State, Rod(true, 0.f, Yaw));
				if (State.GetMoveId() != FName(TEXT("Burst")))
				{
					Ended = State.Elapsed;
				}
			}
			const float Expected = 2.f / (Yaw > 0.f ? 1.f + T.SideTurnRate : 1.f);
			TestNearlyEqual(FString::Printf(TEXT("yaw %+.0f: the 2 s run ends after %.2f s"), Yaw, Expected), Ended, Expected, 2.5f * FLureFight::StepSeconds(T));
		}
	}

	// A move with no side (a dive): the yaw changes nothing at all.
	{
		const FLureFightMove Dive = SideMove(TEXT("Dive"), 1.4f, 1.f, 0.6f, 0.f, 100.f);
		FLureFightState Base = FishFight(T, { Dive }, Starter, 4.f, 60.f, 60.f);
		FLureFightState Steered = FishFight(T, { Dive }, Starter, 4.f, 60.f, 60.f);
		bool bSame = true;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			FLureFight::Step(Base, Rod(true));
			FLureFight::Step(Steered, Rod(true, 0.f, 1.f));
			bSame &= Base.Tension == Steered.Tension && Base.LineOut == Steered.LineOut && Base.Stamina == Steered.Stamina;
		}
		TestTrue(TEXT("a straight dive: steering left or right does nothing"), bSame && Steered.RunDir == 0 && Steered.Side == 0.f);
	}
	return true;
}

// =====================================================================================================================
// Reel speed steps: faster gains line and adds tension
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodReelSteps, "Project.Fishing.Fight.Rod.ReelSpeedSteps", Flags)
bool FLureRodReelSteps::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	const FLureGearStats Starter = Data.Starter();
	const int32 Num = FLureFight::NumReelSteps(T);
	TestEqual(TEXT("shipped: 3 steps (the HUD's Reel 1/3 .. 3/3)"), Num, 3);
	for (int32 Step = 0; Step < Num; ++Step)
	{
		const float Expected = T.ReelSpeedMin + (T.ReelSpeedMax - T.ReelSpeedMin) * static_cast<float>(Step) / static_cast<float>(Num - 1);
		TestNearlyEqual(FString::Printf(TEXT("step %d speed: evenly from ReelSpeedMin to ReelSpeedMax"), Step + 1), FLureFight::ReelStepSpeed(Step, T), Expected, 1.0e-6f);
		TestNearlyEqual(FString::Printf(TEXT("step %d cranking load: max(0, 1 + (speed - 1) x ReelLoadPerSpeed)"), Step + 1), FLureFight::ReelStepLoad(Step, T),
			FMath::Max(0.f, 1.f + (Expected - 1.f) * T.ReelLoadPerSpeed), 1.0e-6f);
	}
	TestTrue(TEXT("each step is faster and loads the line more than the one below"), FLureFight::ReelStepSpeed(2, T) > FLureFight::ReelStepSpeed(1, T)
		&& FLureFight::ReelStepSpeed(1, T) > FLureFight::ReelStepSpeed(0, T) && FLureFight::ReelStepLoad(2, T) > FLureFight::ReelStepLoad(1, T));
	TestEqual(TEXT("INDEX_NONE = the default step"), FLureFight::ClampReelStep(INDEX_NONE, T), FLureFight::DefaultReelStep(T));
	TestEqual(TEXT("a step above the range clamps to the fastest"), FLureFight::ClampReelStep(200, T), Num - 1);
	TestEqual(TEXT("a real step stays"), FLureFight::ClampReelStep(0, T), 0);

	// Other data, other steps (a data edit, no code): 5 steps from 0.6 to 1.4; a single step is the plain reel.
	FLureFishFightRow Five = T;
	Five.ReelSteps = 5;
	Five.ReelDefaultStep = 3;
	Five.ReelSpeedMin = 0.6f;
	Five.ReelSpeedMax = 1.4f;
	FString Problem;
	TestTrue(TEXT("5 steps validate: ") + Problem, Five.Validate(Problem));
	TestNearlyEqual(TEXT("5 steps: the 2nd is 0.8"), FLureFight::ReelStepSpeed(1, Five), 0.8f, 1.0e-6f);
	TestNearlyEqual(TEXT("5 steps: the default (3rd) is 1"), FLureFight::ReelStepSpeed(FLureFight::DefaultReelStep(Five), Five), 1.f, 1.0e-6f);
	FLureFishFightRow One = T;
	One.ReelSteps = 1;
	One.ReelDefaultStep = 1;
	TestTrue(TEXT("1 step validates: ") + Problem, One.Validate(Problem));
	TestTrue(TEXT("a single step is speed 1, load 1"), FLureFight::ReelStepSpeed(0, One) == 1.f && FLureFight::ReelStepLoad(0, One) == 1.f);

	// In the fight: a light fish, reeling 2 s at each step: faster steps bring in more line and load the line more.
	const FLureFightMove Swim = LureFightQA::MakeMove(TEXT("Swim"), 1.f, 0.f, 0.f);
	float Gained[3];
	float Tension[3];
	for (int32 Step = 0; Step < 3; ++Step)
	{
		FLureFightState State = FishFight(T, { Swim }, Starter, 2.f, 0.f, 1.0e6f, 3000.f);
		Run(State, Rod(true, 0.f, 0.f, Step), 2.f);
		Gained[Step] = 3000.f - State.LineOut;
		Tension[Step] = State.Tension;
	}
	TestTrue(FString::Printf(TEXT("line gained in 2 s: slow %.0f < normal %.0f < fast %.0f cm"), Gained[0], Gained[1], Gained[2]), Gained[0] < Gained[1] && Gained[1] < Gained[2]);
	TestNearlyEqual(TEXT("... the fast step gains ReelSpeedMax / 1 as much as the normal one"), Gained[2] / Gained[1], FLureFight::ReelStepSpeed(2, T), 0.02f);
	TestTrue(FString::Printf(TEXT("tension: slow %.2f < normal %.2f < fast %.2f"), Tension[0], Tension[1], Tension[2]), Tension[0] < Tension[1] && Tension[1] < Tension[2]);
	TestNearlyEqual(TEXT("... fast adds RodPower x ReelLoad x (load - 1)"), Tension[2] - Tension[1], Starter.RodPower * T.ReelLoad * (FLureFight::ReelStepLoad(2, T) - 1.f), 0.02f);
	return true;
}

// =====================================================================================================================
// The outcome rules keep their whole-step timers when the rod is what crosses a threshold
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodOutcomes, "Project.Fishing.Fight.Rod.OutcomesKeepTheirRules", Flags)
bool FLureRodOutcomes::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	// Instant tension (rise/fall 0), a fish that never tires: every threshold is crossed on a known step.
	const FLureFishFightRow Instant = LureFightQA::InstantTuning(*Data.Tuning(), 60);
	const FLureGearStats Starter = Data.Starter();
	const float StepSeconds = FLureFight::StepSeconds(Instant);
	auto StepsUntilOver = [](FLureFightState& State, const FLureFightInput& Input, int32 MaxSteps)
	{
		int32 Steps = 0;
		while (!State.IsOver() && Steps < MaxSteps)
		{
			FLureFight::Step(State, Input);
			++Steps;
		}
		return Steps;
	};

	// Snap: pulling 6 while reeling is 9.0 on the 10 line when level, 11.7 with the rod fully back.
	{
		const FLureFightMove Hold = LureFightQA::MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f);
		FLureFightState Level = LureFightQA::SteadyFight(Instant, 6.f, Hold, Starter);
		StepsUntilOver(Level, Rod(true), 300);
		TestEqual(TEXT("level: the line holds"), LureFightQA::OutcomeName(Level.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::None));
		FLureFightState Back = LureFightQA::SteadyFight(Instant, 6.f, Hold, Starter);
		const int32 Steps = StepsUntilOver(Back, Rod(true, 1.f), 300);
		TestEqual(TEXT("rod fully back: the line snaps"), LureFightQA::OutcomeName(Back.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Snapped));
		TestEqual(TEXT("... after exactly SnapGraceTime of whole steps over the strength, plus one (T007-B1)"), Steps, FMath::RoundToInt(Instant.SnapGraceTime / StepSeconds) + 1);
	}

	// Slack: letting a soft-pulling fish run keeps the line taut when level; fully dipped it goes slack and the fish throws the hook
	// after SlackGraceTime x HookSecurity of whole steps, plus one.
	{
		const FLureFightMove Soft = LureFightQA::MakeMove(TEXT("Soft"), 0.5f, 0.f, 0.f);
		FLureFightState Level = LureFightQA::SteadyFight(Instant, 4.f, Soft, Starter);
		StepsUntilOver(Level, Rod(false), 600);
		TestEqual(TEXT("level, letting it run: the line stays taut"), LureFightQA::OutcomeName(Level.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::None));
		FLureFightState Dipped = LureFightQA::SteadyFight(Instant, 4.f, Soft, Starter);
		const int32 Steps = StepsUntilOver(Dipped, Rod(false, -1.f), 600);
		TestEqual(TEXT("fully dipped: the fish throws the hook"), LureFightQA::OutcomeName(Dipped.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::ThrewHook));
		TestEqual(TEXT("... after exactly the slack grace in whole steps, plus one"), Steps, FMath::RoundToInt(FLureFight::SlackGrace(Starter, Instant) / StepSeconds) + 1);
	}

	// Landing: at the fastest step the fish lands on the first step within LandDistance, and the fight is over (frozen).
	{
		const FLureFightMove Hold = LureFightQA::MakeMove(TEXT("Hold"), 1.f, 0.f, 0.f);
		FLureFightState Fast = LureFightQA::SteadyFight(Instant, 1.f, Hold, Starter, 400.f);
		const int32 Fastest = FLureFight::NumReelSteps(Instant) - 1;
		float Before = Fast.LineOut;
		for (int32 Step = 0; Step < 600 && !Fast.IsOver(); ++Step)
		{
			Before = Fast.LineOut;
			FLureFight::Step(Fast, Rod(true, 0.f, 0.f, Fastest));
		}
		TestEqual(TEXT("fast reel: landed"), LureFightQA::OutcomeName(Fast.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Landed));
		TestTrue(FString::Printf(TEXT("... on the first step within LandDistance (%.1f -> %.1f cm)"), Before, Fast.LineOut), Fast.LineOut <= Instant.LandDistance && Before > Instant.LandDistance);
		const float Gain = Starter.ReelSpeed * FLureFight::ReelStepSpeed(Fastest, Instant) * (1.f - 1.f / Starter.RodPower);
		TestNearlyEqual(TEXT("... after (400 - LandDistance) / the fast reel's gain"), Fast.Elapsed, (400.f - Instant.LandDistance) / Gain, 2.f * StepSeconds);
		const float Frozen = Fast.LineOut;
		FLureFight::Step(Fast, Rod(true, 0.f, 0.f, Fastest));
		TestTrue(TEXT("after the landing nothing moves"), Fast.LineOut == Frozen && Fast.Outcome == ELureFightOutcome::Landed);
	}
	return true;
}

// =====================================================================================================================
// The balance: skilled play (against the run, easing on runs) lands faster and safer than holding reel
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodSkilledBeatsHolding, "Project.Fishing.Fight.Rod.SkilledPlayBeatsHolding", Flags)
bool FLureRodSkilledBeatsHolding::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureGearStats Starter = Data.Starter();
	const FName Run(TEXT("Run"));
	const FName Dive(TEXT("Dive"));

	// 1. Bonefish as the roll pipeline gives them, starter kit, the fight 10 m out (the tools/balance model's targets).
	int32 Rolled = 0;
	TMap<EPlayer, int32> Lost;
	TArray<float> SkilledOverHold, WrongOverHold, SideOverHold, SkilledTimes;
	int32 FastMoreSnaps = 0, BackMoreSnaps = 0;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
	{
		FFishRollContext Context;
		Context.SpeciesId = TEXT("Bonefish");
		Context.Seed = 7000 + Seed;
		Context.RegionTag = LureFightQA::Tag(TEXT("Region.Tropical.PalmKey"));
		Context.TimeOfDayHours = 12.f;
		FFishInstance Bonefish;
		if (!FFishRoll::Roll(Fish.Get(), Context, Bonefish))
		{
			continue;
		}
		++Rolled;
		TMap<EPlayer, FPlay> Plays;
		for (const EPlayer Player : { EPlayer::Hold, EPlayer::Side, EPlayer::WrongSide, EPlayer::Back, EPlayer::Fast, EPlayer::Skilled })
		{
			const FPlay Result = Play(RealFight(Data, Bonefish, Run, Starter, Seed), Player);
			Plays.Add(Player, Result);
			Lost.FindOrAdd(Player) += Result.Landed() ? 0 : 1;
		}
		const FPlay& Hold = Plays[EPlayer::Hold];
		if (Plays[EPlayer::Skilled].Landed())
		{
			SkilledTimes.Add(Plays[EPlayer::Skilled].Elapsed);
		}
		if (Hold.Landed())
		{
			if (Plays[EPlayer::Skilled].Landed()) { SkilledOverHold.Add(Plays[EPlayer::Skilled].Elapsed / Hold.Elapsed); }
			if (Plays[EPlayer::WrongSide].Landed()) { WrongOverHold.Add(Plays[EPlayer::WrongSide].Elapsed / Hold.Elapsed); }
			if (Plays[EPlayer::Side].Landed()) { SideOverHold.Add(Plays[EPlayer::Side].Elapsed / Hold.Elapsed); }
		}
	}
	FastMoreSnaps = Lost.FindRef(EPlayer::Fast) - Lost.FindRef(EPlayer::Hold);
	BackMoreSnaps = Lost.FindRef(EPlayer::Back) - Lost.FindRef(EPlayer::Hold);
	AddInfo(FString::Printf(TEXT("%d rolled bonefish, starter kit, lost: hold %d, steer only %d, wrong side %d, rod back %d, fast reel %d, skilled %d; skilled median %.1f s; ")
		TEXT("on the fish holding lands: skilled/hold time median %.2f, steer-only/hold %.2f, wrong-side/hold %.2f"), Rolled, Lost.FindRef(EPlayer::Hold), Lost.FindRef(EPlayer::Side),
		Lost.FindRef(EPlayer::WrongSide), Lost.FindRef(EPlayer::Back), Lost.FindRef(EPlayer::Fast), Lost.FindRef(EPlayer::Skilled), Median(SkilledTimes), Median(SkilledOverHold),
		Median(SideOverHold), Median(WrongOverHold)));
	TestTrue(TEXT("fixture: the roll pipeline gave bonefish"), Rolled >= 190);
	TestTrue(FString::Printf(TEXT("holding reel still loses a real share (%d of %d, 25-60 %%, the T-007 tune)"), Lost.FindRef(EPlayer::Hold), Rolled),
		Lost.FindRef(EPlayer::Hold) >= Rolled / 4 && Lost.FindRef(EPlayer::Hold) <= Rolled * 3 / 5);
	TestTrue(FString::Printf(TEXT("SAFER: skilled play loses at most 2 %% (%d of %d)"), Lost.FindRef(EPlayer::Skilled), Rolled), Lost.FindRef(EPlayer::Skilled) <= Rolled / 50);
	TestTrue(FString::Printf(TEXT("FASTER: on the fish holding lands, skilled takes <= 0.85 of the time (median %.2f, %d fish)"), Median(SkilledOverHold), SkilledOverHold.Num()),
		SkilledOverHold.Num() >= Rolled / 3 && Median(SkilledOverHold) <= 0.85f);
	TestTrue(FString::Printf(TEXT("steering against the runs alone snaps far fewer than holding (%d vs %d)"), Lost.FindRef(EPlayer::Side), Lost.FindRef(EPlayer::Hold)),
		Lost.FindRef(EPlayer::Side) * 5 <= Lost.FindRef(EPlayer::Hold) * 3);
	TestTrue(FString::Printf(TEXT("... and is faster on the same fish (median %.2f)"), Median(SideOverHold)), Median(SideOverHold) < 1.f);
	TestTrue(FString::Printf(TEXT("steering WITH the runs loses ground: slower on the same fish (median %.2f >= 1.05)"), Median(WrongOverHold)), Median(WrongOverHold) >= 1.05f);
	TestTrue(FString::Printf(TEXT("holding reel with the rod fully back snaps more (+%d)"), BackMoreSnaps), BackMoreSnaps > 0);
	TestTrue(FString::Printf(TEXT("holding reel at the fastest step snaps more (+%d)"), FastMoreSnaps), FastMoreSnaps > 0);

	// 2. The reference fish: a typical Common (1.5 kg) lands both ways, skilled faster; the playtest's Rare 2.04 kg snaps a held line,
	//    skilled play lands it faster than the T-007 careful player.
	auto Fraction = [](float Kg) { return (Kg - 0.5f) / 4.f; };
	FFishInstance Typical;
	FFishInstance Rare;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), Fraction(1.5f), 33, Typical)
		|| !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Rare"), Fraction(2.04f), 31, Rare))
	{
		return false;
	}
	for (int32 Seed = 1; Seed <= 6; ++Seed)
	{
		const FPlay Hold = Play(RealFight(Data, Typical, Run, Starter, Seed), EPlayer::Hold);
		const FPlay Skilled = Play(RealFight(Data, Typical, Run, Starter, Seed), EPlayer::Skilled);
		TestTrue(FString::Printf(TEXT("1.5 kg Common, seed %d: skilled lands it faster than holding (%.1f s vs %.1f s)"), Seed, Skilled.Elapsed, Hold.Elapsed),
			Skilled.Landed() && Hold.Landed() && Skilled.Elapsed < Hold.Elapsed);
		const FPlay RareHold = Play(RealFight(Data, Rare, Run, Starter, Seed), EPlayer::Hold);
		const FPlay RareCareful = Play(RealFight(Data, Rare, Run, Starter, Seed), EPlayer::Careful);
		const FPlay RareSkilled = Play(RealFight(Data, Rare, Run, Starter, Seed), EPlayer::Skilled);
		TestEqual(FString::Printf(TEXT("Rare 2.04 kg, seed %d: holding reel snaps"), Seed), LureFightQA::OutcomeName(RareHold.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Snapped));
		TestTrue(FString::Printf(TEXT("Rare 2.04 kg, seed %d: skilled lands it faster than careful (%.1f s vs %.1f s)"), Seed, RareSkilled.Elapsed, RareCareful.Elapsed),
			RareSkilled.Landed() && RareCareful.Landed() && RareSkilled.Elapsed < RareCareful.Elapsed);
	}

	// 3. The Coral Snapper stays the harder fish: skilled play is no less safe than careful play, and a skilled snapper fight is longer
	//    than a skilled bonefish fight.
	int32 SnapperSkilledLost = 0, SnapperCarefulLost = 0;
	TArray<float> SnapperSkilled;
	for (int32 Seed = 1; Seed <= 100; ++Seed)
	{
		FFishRollContext Context;
		Context.SpeciesId = TEXT("CoralSnapper");
		Context.Seed = 9000 + Seed;
		Context.RegionTag = LureFightQA::Tag(TEXT("Region.Tropical.PalmKey"));
		Context.TimeOfDayHours = 20.f;
		FFishInstance Snapper;
		if (!FFishRoll::Roll(Fish.Get(), Context, Snapper))
		{
			continue;
		}
		const FPlay Skilled = Play(RealFight(Data, Snapper, Dive, Starter, Seed), EPlayer::Skilled);
		const FPlay Careful = Play(RealFight(Data, Snapper, Dive, Starter, Seed), EPlayer::Careful);
		SnapperSkilledLost += Skilled.Landed() ? 0 : 1;
		SnapperCarefulLost += Careful.Landed() ? 0 : 1;
		if (Skilled.Landed())
		{
			SnapperSkilled.Add(Skilled.Elapsed);
		}
	}
	AddInfo(FString::Printf(TEXT("100 rolled snappers, starter kit: skilled loses %d (median %.1f s), careful loses %d"), SnapperSkilledLost, Median(SnapperSkilled), SnapperCarefulLost));
	TestTrue(FString::Printf(TEXT("snapper: skilled play loses no more than careful play (+2) (%d vs %d)"), SnapperSkilledLost, SnapperCarefulLost), SnapperSkilledLost <= SnapperCarefulLost + 2);
	TestTrue(FString::Printf(TEXT("snapper fights stay longer than bonefish fights for a skilled player (%.1f s vs %.1f s)"), Median(SnapperSkilled), Median(SkilledTimes)),
		Median(SnapperSkilled) > Median(SkilledTimes));
	return true;
}

// =====================================================================================================================
// Input: look steers the rod during a fight (the view otherwise); the wheel steps the reel; look returns after the fight
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodInputRouting, "Project.Fishing.Fight.Rod.InputRoutesLookToTheRod", Flags)
bool FLureRodInputRouting::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	FFishInstance Bonefish;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 101, Bonefish))
	{
		return false;
	}
	LureFightQA::FWorld World;
	FOwnerRig Owner;
	if (!World.Create(*this) || !Owner.Create(*this, World, Data, Fish))
	{
		return false;
	}
	ALurePlayerCharacter* Character = Owner.Character;
	APlayerController* Controller = Owner.Controller;
	ULureFishingComponent* Fishing = Owner.Fishing;

	// No fish on: look input turns the view, the rod stays level.
	Controller->RotationInput = FRotator::ZeroRotator;
	Character->DoLook(5.f, 3.f);
	TestFalse(TEXT("no fish on: not steering the rod"), Fishing->IsSteeringRod());
	TestTrue(TEXT("no fish on: the look input turns the view"), FMath::IsNearlyEqual(static_cast<float>(Controller->RotationInput.Yaw), 5.f) && FMath::IsNearlyEqual(static_cast<float>(Controller->RotationInput.Pitch), 3.f));
	TestTrue(TEXT("no fish on: the rod aim is level and centered"), Fishing->GetRodAim().IsNearlyZero());
	Controller->RotationInput = FRotator::ZeroRotator;

	// A fish on: the look input steers the rod, not the view.
	if (!LureFightQA::CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
	{
		Owner.Release();
		return false;
	}
	World.Tick(1);
	TestTrue(TEXT("fish on: steering the rod"), Fishing->IsSteeringRod());
	Character->DoLook(10.f, -6.f);
	TestTrue(TEXT("fish on: the look input does not turn the view"), Controller->RotationInput.IsNearlyZero());
	TestNearlyEqual(TEXT("... it swings the rod right: yaw = degrees / RodAimSideDeg"), static_cast<float>(Fishing->GetRodAim().X), 10.f / T.RodAimSideDeg, 1.0e-4f);
	TestNearlyEqual(TEXT("... and dips it: pitch = -degrees / RodAimDownDeg"), static_cast<float>(Fishing->GetRodAim().Y), -6.f / T.RodAimDownDeg, 1.0e-4f);
	Character->DoLook(500.f, 500.f);
	TestTrue(TEXT("the rod stops at its range (fully right and back)"), Fishing->GetRodAim().Equals(FVector2D(1.f, 1.f), 1.0e-5f));
	TestTrue(TEXT("HUD: the rod angle"), Fishing->GetStatusText().Contains(TEXT("Rod: back-right")));

	// The owner sends it (standalone: the RPC runs here; aim changes go out at most every FightInputSendSeconds) and the server fights with it.
	World.Tick(FMath::CeilToInt(GetDefault<ULureFishingSettings>()->FightInputSendSeconds / LureFightQA::WorldDt) + 2);
	TestNearlyEqual(TEXT("the server fights with the rod yaw"), Fishing->GetFightState().RodYaw, 1.f, 1.0e-4f);
	TestNearlyEqual(TEXT("... and pitch"), Fishing->GetFightState().RodPitch, 1.f, 1.0e-4f);
	TestNearlyEqual(TEXT("FightNet carries it for the other players"), Fishing->GetFightNet().RodYaw, 1.f, 1.0e-4f);

	// The camera eases toward the fish, turned by the rod's share.
	World.Tick(60);
	{
		const UCameraComponent* Camera = Character->GetFirstPersonCamera();
		const FVector Eye = Camera ? Camera->GetComponentLocation() : Character->GetPawnViewLocation();
		const FRotator Target = FLureRodControl::CameraTarget(Eye, Fishing->GetBobberLocation(), 1.f, 1.f, T);
		const FRotator Now = Controller->GetControlRotation().GetNormalized();
		TestTrue(FString::Printf(TEXT("the camera follows the fish and the rod (yaw %.1f vs %.1f, pitch %.1f vs %.1f)"), Now.Yaw, Target.Yaw, Now.Pitch, Target.Pitch),
			FMath::Abs(FRotator::NormalizeAxis(Now.Yaw - Target.Yaw)) < 6.f && FMath::Abs(Now.Pitch - Target.Pitch) < 6.f);
	}

	// The wheel / bumpers: one step per press, within the range, shown in the HUD, sent to the server.
	const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Character->InputComponent);
	UInputAction* Faster = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelFaster);
	UInputAction* Slower = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelSlower);
	if (TestNotNull(TEXT("input component"), Input) && TestNotNull(TEXT("ReelFaster action"), Faster) && TestNotNull(TEXT("ReelSlower action"), Slower))
	{
		TestEqual(TEXT("starts on the default step"), Fishing->GetReelStep(), FLureFight::DefaultReelStep(T));
		TestTrue(TEXT("HUD: Reel 2/3"), Fishing->GetStatusText().Contains(TEXT("Reel 2/3")));
		Fire(Input, Faster, ETriggerEvent::Started);
		TestEqual(TEXT("wheel up: one step faster"), Fishing->GetReelStep(), 2);
		Fire(Input, Faster, ETriggerEvent::Started);
		TestEqual(TEXT("wheel up at the top: stays on the fastest"), Fishing->GetReelStep(), 2);
		TestTrue(TEXT("HUD: Reel 3/3"), Fishing->GetStatusText().Contains(TEXT("Reel 3/3")));
		World.Tick(2);
		TestEqual(TEXT("the server reels at the chosen step"), Fishing->GetFightState().ReelStep, 2);
		for (int32 Press = 0; Press < 3; ++Press)
		{
			Fire(Input, Slower, ETriggerEvent::Started);
		}
		TestEqual(TEXT("wheel down x3: the slowest step"), Fishing->GetReelStep(), 0);
		TestTrue(TEXT("HUD: Reel 1/3"), Fishing->GetStatusText().Contains(TEXT("Reel 1/3")));
		World.Tick(2);
		TestEqual(TEXT("the server follows"), Fishing->GetFightState().ReelStep, 0);
	}

	// The fight ends: look turns the view again, the rod eases back to level; the reel step stays for the next fish.
	Fishing->AuthorityReelIn();
	World.Tick(1);
	TestFalse(TEXT("after the fight: not steering"), Fishing->IsSteeringRod());
	Controller->RotationInput = FRotator::ZeroRotator;
	Character->DoLook(4.f, -2.f);
	TestTrue(TEXT("after the fight: the look input turns the view again"), FMath::IsNearlyEqual(static_cast<float>(Controller->RotationInput.Yaw), 4.f) && FMath::IsNearlyEqual(static_cast<float>(Controller->RotationInput.Pitch), -2.f));
	Controller->RotationInput = FRotator::ZeroRotator;
	TestTrue(TEXT("after the fight: the rod aim is level"), Fishing->GetRodAim().IsNearlyZero());
	World.Tick(60);
	TestTrue(FString::Printf(TEXT("... and the arms' eased aim is back to level (%.3f, %.3f)"), Fishing->GetRodAimForAnimation().X, Fishing->GetRodAimForAnimation().Y),
		Fishing->GetRodAimForAnimation().Size() < 0.01f);
	TestEqual(TEXT("the reel step is kept for the next fight"), Fishing->GetReelStep(), 0);

	// A new fight starts with the rod level again.
	if (LureFightQA::CastAndWait(*this, World, Fishing) && TestTrue(TEXT("hook again"), Fishing->AuthorityHookFish(Bonefish)))
	{
		World.Tick(2);
		TestTrue(TEXT("new fight: the rod starts level and centered"), Fishing->GetRodAim().IsNearlyZero() && Fishing->GetFightState().RodYaw == 0.f);
		TestEqual(TEXT("new fight: the server uses the kept reel step"), Fishing->GetFightState().ReelStep, 0);
	}
	Owner.Release();
	return true;
}

// =====================================================================================================================
// Server authority: the rod is only input; the server clamps it and decides everything
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodServerAuthority, "Project.Fishing.Fight.Rod.ServerAuthorityOverTheRod", Flags)
bool FLureRodServerAuthority::RunTest(const FString& Parameters)
{
	// The wire: one unreliable RPC of 4 bytes (fight id, pitch, yaw, step). No tension, fish, line or outcome.
	const UFunction* Rpc = ULureFishingComponent::StaticClass()->FindFunctionByName(TEXT("ServerSetFightInput"));
	if (TestNotNull(TEXT("ServerSetFightInput"), Rpc))
	{
		TestTrue(TEXT("a server RPC"), Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer));
		TestFalse(TEXT("unreliable (sent at a rate; a lost packet is repaired by the next)"), Rpc->HasAnyFunctionFlags(FUNC_NetReliable));
		int32 Bytes = 0;
		bool bOnlyBytes = true;
		for (TFieldIterator<FProperty> It(Rpc); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			++Bytes;
			bOnlyBytes &= CastField<FByteProperty>(*It) != nullptr;
		}
		TestTrue(FString::Printf(TEXT("exactly 4 byte parameters (%d)"), Bytes), Bytes == 4 && bOnlyBytes);
	}

	// Packing: 0 is exact, the ends are exact, a round trip is within half a step.
	TestEqual(TEXT("pack 0 = 127"), static_cast<int32>(FLureRodControl::PackAxis(0.f)), 127);
	TestEqual(TEXT("unpack 127 = 0 exactly (the neutral rod stays the T-007 fight on the network)"), FLureRodControl::UnpackAxis(127), 0.f);
	TestEqual(TEXT("pack -1 = 0"), static_cast<int32>(FLureRodControl::PackAxis(-1.f)), 0);
	TestEqual(TEXT("pack +1 = 254"), static_cast<int32>(FLureRodControl::PackAxis(1.f)), 254);
	TestEqual(TEXT("unpack 255 reads +1"), FLureRodControl::UnpackAxis(255), 1.f);
	TestEqual(TEXT("pack NaN = 127 (neutral)"), static_cast<int32>(FLureRodControl::PackAxis(std::numeric_limits<float>::quiet_NaN())), 127);
	bool bRoundTrip = true;
	for (float Value = -1.f; Value <= 1.f; Value += 0.037f)
	{
		bRoundTrip &= FMath::Abs(FLureRodControl::UnpackAxis(FLureRodControl::PackAxis(Value)) - Value) <= 0.5f / 127.f + 1.0e-6f;
	}
	TestTrue(TEXT("round trip within half a step"), bRoundTrip);

	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	FFishInstance Bonefish;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 111, Bonefish))
	{
		return false;
	}
	LureFightQA::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* ServerCharacter = World.Spawn(LureFightQA::StandAt());
	ALurePlayerCharacter* ClientCharacter = World.Spawn(LureFightQA::StandAt() + FVector(0.f, 250.f, 0.f));
	ULureFishingComponent* Server = LureFightQA::SetUpFishing(ServerCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
	ULureFishingComponent* Client = LureFightQA::SetUpFishing(ClientCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
	if (!TestNotNull(TEXT("server"), Server) || !TestNotNull(TEXT("client"), Client))
	{
		return false;
	}

	// No fight: rod input is ignored.
	Server->AuthoritySetFightInput(0, 1.f, 1.f, 2);
	TestTrue(TEXT("no fight: the input is ignored"), Server->GetServerFightInput().RodPitch == 0.f && Server->GetServerFightInput().RodYaw == 0.f);

	if (!LureFightQA::CastAndWait(*this, World, Server) || !TestTrue(TEXT("the server hooks"), Server->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	const uint8 FightId = Server->GetFightNet().FightId;

	// The server clamps whatever arrives.
	Server->AuthoritySetFightInput(FightId, 5.f, -9.f, 200);
	FLureFightInput In = Server->GetServerFightInput();
	TestTrue(FString::Printf(TEXT("out of range: clamped (%.2f, %.2f, step %d)"), In.RodPitch, In.RodYaw, In.ReelStep), In.RodPitch == 1.f && In.RodYaw == -1.f && In.ReelStep == 2);
	Server->AuthoritySetFightInput(FightId, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1);
	In = Server->GetServerFightInput();
	TestTrue(TEXT("NaN / infinity: the neutral rod"), In.RodPitch == 0.f && In.RodYaw == 0.f && In.ReelStep == 1);

	// A late packet from an earlier fight is ignored.
	Server->AuthoritySetFightInput(static_cast<uint8>(FightId - 1), 1.f, 1.f, 0);
	In = Server->GetServerFightInput();
	TestTrue(TEXT("a packet from the previous fight: ignored"), In.RodPitch == 0.f && In.RodYaw == 0.f && In.ReelStep == 1);

	// The RPC path: bytes in, the same clamped input.
	if (UFunction* Function = Server->FindFunction(TEXT("ServerSetFightInput")))
	{
		struct { uint8 FightId; uint8 RodPitch; uint8 RodYaw; uint8 ReelStep; } Params{ FightId, FLureRodControl::PackAxis(0.5f), FLureRodControl::PackAxis(-0.25f), 0 };
		Server->ProcessEvent(Function, &Params);
		In = Server->GetServerFightInput();
		TestTrue(FString::Printf(TEXT("the RPC on the server sets the input (%.3f, %.3f, step %d)"), In.RodPitch, In.RodYaw, In.ReelStep),
			FMath::IsNearlyEqual(In.RodPitch, 0.5f, 0.005f) && FMath::IsNearlyEqual(In.RodYaw, -0.25f, 0.005f) && In.ReelStep == 0);
	}

	// A client copy asks, it never sets: neither its own copy nor (through its absorbed RPC) anything else changes.
	MakeCopy(ClientCharacter, ROLE_AutonomousProxy);
	LureFightQA::ReplicateFishing(*this, Server, Client);
	TestTrue(TEXT("the client sees the fight"), Client->GetFightNet().bActive);
	Client->AuthoritySetFightInput(Client->GetFightNet().FightId, 1.f, 1.f, 2);
	In = Client->GetServerFightInput();
	TestTrue(TEXT("client: AuthoritySetFightInput does nothing on a client copy"), In.RodPitch == 0.f && In.RodYaw == 0.f && In.ReelStep == INDEX_NONE);
	if (UFunction* Function = Client->FindFunction(TEXT("ServerSetFightInput")))
	{
		struct { uint8 FightId; uint8 RodPitch; uint8 RodYaw; uint8 ReelStep; } Params{ Client->GetFightNet().FightId, 254, 254, 2 };
		Client->ProcessEvent(Function, &Params);
		TestTrue(TEXT("client: its RPC does not run on its own copy"), Client->GetServerFightInput().RodPitch == 0.f);
	}

	// The tension is the server's own simulation of that clamped input: replaying the same frame on a copy gives the same numbers.
	Server->AuthoritySetFightInput(FightId, 1.f, 0.f, 2);
	Server->AuthoritySetReeling(true);
	World.Tick(5);
	FLureFightState Replay = Server->GetFightState();
	const FLureFightInput Applied = Server->GetServerFightInput();
	World.Tick(1);
	FLureFight::Advance(Replay, Applied, LureFightQA::WorldDt);
	const FLureFightState& Fight = Server->GetFightState();
	TestEqual(TEXT("replay: the same number of fixed steps"), Replay.Steps, Fight.Steps);
	TestTrue(FString::Printf(TEXT("the server's tension (%.4f) is its own step of the clamped input (%.4f)"), Fight.Tension, Replay.Tension),
		Replay.Steps == Fight.Steps && FMath::IsNearlyEqual(Replay.Tension, Fight.Tension, 1.0e-4f) && FMath::IsNearlyEqual(Replay.LineOut, Fight.LineOut, 1.0e-3f));
	TestEqual(TEXT("the server still fights"), LureFightQA::StateName(Server->GetFishingState()), LureFightQA::StateName(ELureFishingState::Hooked));
	ClientCharacter->SetRole(ROLE_Authority);
	return true;
}

// =====================================================================================================================
// Replication: other players see the rod angle (cosmetic) and draw the line from it
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodReplication, "Project.Fishing.Fight.Rod.AimReplicatesToProxies", Flags)
bool FLureRodReplication::RunTest(const FString& Parameters)
{
	// The new FightNet fields survive the engine's replication path exactly.
	{
		FLureFightNetState Sample;
		Sample.bActive = true;
		Sample.RodPitch = 0.5f;
		Sample.RodYaw = -0.75f;
		Sample.ReelStep = 2;
		Sample.RunSide = ELureFightRunSide::Left;
		FLureFightNetState Out;
		int64 Bits = 0;
		if (LureFightQA::NetRoundTrip(*this, FLureFightNetState::StaticStruct(), &Sample, &Out, Bits))
		{
			TestTrue(FString::Printf(TEXT("rod pitch, yaw, reel step and run side round-trip (%lld bits)"), Bits),
				Out.RodPitch == 0.5f && Out.RodYaw == -0.75f && Out.ReelStep == 2 && Out.RunSide == ELureFightRunSide::Left);
		}
	}

	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	FFishInstance Bonefish;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 121, Bonefish))
	{
		return false;
	}
	LureFightQA::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* ServerCharacter = World.Spawn(LureFightQA::StandAt());
	ALurePlayerCharacter* ProxyCharacter = World.Spawn(LureFightQA::StandAt() + FVector(0.f, -250.f, 0.f));
	ULureFishingComponent* Server = LureFightQA::SetUpFishing(ServerCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
	ULureFishingComponent* Proxy = LureFightQA::SetUpFishing(ProxyCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
	if (!TestNotNull(TEXT("server"), Server) || !TestNotNull(TEXT("proxy"), Proxy) || !LureFightQA::CastAndWait(*this, World, Server)
		|| !TestTrue(TEXT("the server hooks"), Server->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	MakeCopy(ProxyCharacter, ROLE_SimulatedProxy);
	LureFightQA::ReplicateFishing(*this, Server, Proxy);
	World.Tick(2);
	const FVector NeutralStart = Proxy->GetLineStart() - ProxyCharacter->GetPawnViewLocation();

	// The owner steers: left and pulled back half way, fast reel.
	Server->AuthoritySetFightInput(Server->GetFightNet().FightId, 0.5f, -1.f, 2);
	World.Tick(2);
	TestNearlyEqual(TEXT("server: FightNet.RodPitch"), Server->GetFightNet().RodPitch, 0.5f, 1.0e-5f);
	TestNearlyEqual(TEXT("server: FightNet.RodYaw"), Server->GetFightNet().RodYaw, -1.f, 1.0e-5f);
	TestEqual(TEXT("server: FightNet.ReelStep"), static_cast<int32>(Server->GetFightNet().ReelStep), 2);
	LureFightQA::ReplicateFishing(*this, Server, Proxy);
	TestTrue(TEXT("proxy: sees the server's rod aim"), Proxy->GetRodAim().Equals(FVector2D(-1.f, 0.5f), 1.0e-5f));
	TestEqual(TEXT("proxy: sees the reel step"), Proxy->GetReelStep(), 2);
	TestEqual(TEXT("proxy: never simulates the fight"), Proxy->GetFightState().Steps, 0);
	World.Tick(40);
	LureFightQA::ReplicateFishing(*this, Server, Proxy);
	World.Tick(1);
	TestTrue(FString::Printf(TEXT("proxy: its rod eases to the aim (%.3f, %.3f)"), Proxy->GetRodAimForAnimation().X, Proxy->GetRodAimForAnimation().Y),
		Proxy->GetRodAimForAnimation().Equals(FVector2D(-1.f, 0.5f), 0.02f));
	// The line starts at this player's rod tip as the others see it: turned left and raised by the aim.
	const FVector Steered = Proxy->GetLineStart() - ProxyCharacter->GetPawnViewLocation();
	const FRotator Facing(0.f, ProxyCharacter->GetBaseAimRotation().Yaw, 0.f);
	const FVector LocalNeutral = Facing.UnrotateVector(NeutralStart);
	const FVector LocalSteered = Facing.UnrotateVector(Steered);
	TestTrue(FString::Printf(TEXT("proxy: the rod tip swings left (%.1f -> %.1f cm right of the eye)"), LocalNeutral.Y, LocalSteered.Y), LocalSteered.Y < LocalNeutral.Y - 20.f);
	TestTrue(FString::Printf(TEXT("proxy: ... and up (%.1f -> %.1f cm above the eye)"), LocalNeutral.Z, LocalSteered.Z), LocalSteered.Z > LocalNeutral.Z + 5.f);
	TestTrue(TEXT("proxy: the swing is the rod's look turn"), FMath::IsNearlyEqual(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(LocalSteered.Y, LocalSteered.X)) - FMath::RadiansToDegrees(FMath::Atan2(LocalNeutral.Y, LocalNeutral.X))),
		-T.RodAimLookYawDeg, 3.f));

	// The fight ends: the proxy's rod returns to level.
	Server->AuthorityReelIn();
	LureFightQA::ReplicateFishing(*this, Server, Proxy);
	World.Tick(60);
	TestTrue(TEXT("proxy: after the fight the rod aim is 0"), Proxy->GetRodAim().IsNearlyZero() && Proxy->GetRodAimForAnimation().Size() < 0.01f);
	ProxyCharacter->SetRole(ROLE_Authority);
	return true;
}

// =====================================================================================================================
// Pure helpers: the aim, the camera, the HUD words, the data columns
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodHelpers, "Project.Fishing.Fight.Rod.AimCameraHudAndData", Flags)
bool FLureRodHelpers::RunTest(const FString& Parameters)
{
	LureFightQA::FFightTables Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();

	// The aim: look degrees accumulate and clamp to the rod's range; up and down have their own range.
	FLureRodAim Aim;
	Aim.AddLookInput(0.5f * T.RodAimSideDeg, 0.5f * T.RodAimUpDeg, T);
	TestTrue(TEXT("half the range right and back = (0.5, 0.5)"), FMath::IsNearlyEqual(Aim.GetYaw01(T), 0.5f) && FMath::IsNearlyEqual(Aim.GetPitch01(T), 0.5f));
	Aim.AddLookInput(-4.f * T.RodAimSideDeg, -4.f * T.RodAimDownDeg, T);
	TestTrue(TEXT("far past the range: fully left and dipped"), Aim.GetYaw01(T) == -1.f && Aim.GetPitch01(T) == -1.f);
	TestNearlyEqual(TEXT("... the stored degrees stop at the range (so coming back is immediate)"), Aim.PitchDeg, -T.RodAimDownDeg, 1.0e-4f);
	Aim.AddLookInput(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), T);
	TestTrue(TEXT("non-finite look input is ignored"), Aim.GetYaw01(T) == -1.f && Aim.GetPitch01(T) == -1.f);
	Aim.Reset();
	TestTrue(TEXT("reset: level and centered"), Aim.GetYaw01(T) == 0.f && Aim.GetPitch01(T) == 0.f);
	TestNearlyEqual(TEXT("degrees of an aim: the inverse"), FLureRodControl::PitchDegrees(-0.5f, T), -0.5f * T.RodAimDownDeg, 1.0e-5f);

	// The camera: at the fish, turned by the rod's share; eased; the short way round.
	const FVector Eye(0.f, 0.f, 200.f);
	const FVector FishAhead(1000.f, 0.f, 0.f);
	const FRotator Straight = FLureRodControl::CameraTarget(Eye, FishAhead, 0.f, 0.f, T);
	TestNearlyEqual(TEXT("camera target: yaw at the fish"), static_cast<float>(Straight.Yaw), 0.f, 1.0e-3f);
	TestNearlyEqual(TEXT("camera target: looking down at the fish"), static_cast<float>(Straight.Pitch), FMath::RadiansToDegrees(FMath::Atan2(-200.f, 1000.f)), 1.0e-2f);
	const FRotator Turned = FLureRodControl::CameraTarget(Eye, FishAhead, 1.f, 1.f, T);
	TestNearlyEqual(TEXT("rod fully right: the camera turns CameraRodYawShare of RodAimSideDeg"), static_cast<float>(Turned.Yaw), T.RodAimSideDeg * T.CameraRodYawShare, 1.0e-3f);
	TestNearlyEqual(TEXT("rod fully back: the camera lifts CameraRodPitchShare of RodAimUpDeg"), static_cast<float>(Turned.Pitch), static_cast<float>(Straight.Pitch) + T.RodAimUpDeg * T.CameraRodPitchShare, 1.0e-2f);
	TestTrue(TEXT("a fish straight below: pitch clamped at -80"), FLureRodControl::CameraTarget(Eye, FVector(0.f, 0.f, -500.f), -1.f, 0.f, T).Pitch >= -80.f);
	const FRotator Half = FLureRodControl::CameraStep(FRotator(0.f, 0.f, 0.f), FRotator(-20.f, 40.f, 0.f), T.CameraFollowTime, T);
	TestNearlyEqual(TEXT("camera: 63 % of the way after CameraFollowTime (yaw)"), static_cast<float>(Half.Yaw), 40.f * (1.f - FMath::Exp(-1.f)), 0.01f);
	TestNearlyEqual(TEXT("camera: ... (pitch)"), static_cast<float>(Half.Pitch), -20.f * (1.f - FMath::Exp(-1.f)), 0.01f);
	const FRotator Wrap = FLureRodControl::CameraStep(FRotator(0.f, 170.f, 0.f), FRotator(0.f, -170.f, 0.f), 10.f, T);
	TestTrue(FString::Printf(TEXT("camera: the short way round across 180 (%.1f)"), Wrap.Yaw), FMath::Abs(FRotator::NormalizeAxis(Wrap.Yaw + 170.f)) < 0.1f);
	const FRotator Small = FLureRodControl::CameraStep(FRotator(0.f, 170.f, 0.f), FRotator(0.f, -170.f, 0.f), 0.05f, T);
	TestTrue(FString::Printf(TEXT("camera: a small step goes past 180, not back through 0 (%.1f)"), Small.Yaw), Small.Yaw > 170.f || Small.Yaw < -170.f);
	FLureFishFightRow Snap = T;
	Snap.CameraFollowTime = 0.f;
	TestTrue(TEXT("CameraFollowTime 0: straight there"), FLureRodControl::CameraStep(FRotator::ZeroRotator, FRotator(10.f, 30.f, 0.f), 0.016f, Snap).Equals(FRotator(10.f, 30.f, 0.f), 1.0e-3f));
	TestTrue(TEXT("no time, no move"), FLureRodControl::CameraStep(FRotator(5.f, 5.f, 0.f), FRotator(10.f, 30.f, 0.f), 0.f, T).Equals(FRotator(5.f, 5.f, 0.f), 1.0e-3f));

	// HUD words.
	TestEqual(TEXT("rod level and centered"), FLureRodControl::DescribeRod(0.f, 0.f), FString(TEXT("level")));
	TestEqual(TEXT("rod back and right"), FLureRodControl::DescribeRod(0.6f, 0.5f), FString(TEXT("back-right")));
	TestEqual(TEXT("rod dipped and left"), FLureRodControl::DescribeRod(-0.6f, -0.9f), FString(TEXT("dipped-left")));
	TestEqual(TEXT("small moves are still level"), FLureRodControl::DescribeRod(0.2f, -0.2f), FString(TEXT("level")));
	TestTrue(TEXT("reel text"), FLureRodControl::ReelText(1, 3).StartsWith(TEXT("Reel 2/3")));
	TestEqual(TEXT("fish runs left: pull right"), FLureRodControl::RunHint(ELureFightRunSide::Left), FString(TEXT("Fish runs LEFT: pull right")));
	TestEqual(TEXT("fish runs right: pull left"), FLureRodControl::RunHint(ELureFightRunSide::Right), FString(TEXT("Fish runs RIGHT: pull left")));
	TestTrue(TEXT("no side: no hint"), FLureRodControl::RunHint(ELureFightRunSide::None).IsEmpty());
	TestTrue(TEXT("run side <-> direction"), FLureRodControl::RunSideFromDirection(-1) == ELureFightRunSide::Left && FLureRodControl::DirectionFromRunSide(ELureFightRunSide::Right) == 1
		&& FLureRodControl::RunSideFromDirection(0) == ELureFightRunSide::None);

	// The placeholder rod turn and the eased aim.
	const FRotator Look = FLureRodControl::RodLook(1.f, -1.f, T);
	TestTrue(TEXT("rod look: tip up and left by RodAimLook*Deg"), FMath::IsNearlyEqual(static_cast<float>(Look.Pitch), T.RodAimLookPitchDeg) && FMath::IsNearlyEqual(static_cast<float>(Look.Yaw), -T.RodAimLookYawDeg));
	TestTrue(TEXT("eased aim: 63 % after the time constant"), FLureRodControl::EaseAim(FVector2D::ZeroVector, FVector2D(1.f, 0.f), 0.1f, 0.1f).Equals(FVector2D(1.f - FMath::Exp(-1.f), 0.f), 1.0e-4f));

	// The data: the shipped columns validate; bad values are refused; the columns are optional (a CSV without them = these defaults).
	FString Problem;
	TestTrue(TEXT("shipped rod-steering columns validate: ") + Problem, T.ValidateRodSteering(Problem));
	struct FBad { const TCHAR* What; TFunction<void(FLureFishFightRow&)> Break; };
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const FBad Bad[] = {
		{ TEXT("RodAimUpDeg 0.5"), [](FLureFishFightRow& R) { R.RodAimUpDeg = 0.5f; } },
		{ TEXT("RodAimSideDeg NaN"), [NaN](FLureFishFightRow& R) { R.RodAimSideDeg = NaN; } },
		{ TEXT("PitchBackPressure -0.1"), [](FLureFishFightRow& R) { R.PitchBackPressure = -0.1f; } },
		{ TEXT("PitchDipPressure 1"), [](FLureFishFightRow& R) { R.PitchDipPressure = 1.f; } },
		{ TEXT("SideLeverage 1"), [](FLureFishFightRow& R) { R.SideLeverage = 1.f; } },
		{ TEXT("SideTurnPull 1"), [](FLureFishFightRow& R) { R.SideTurnPull = 1.f; } },
		{ TEXT("SideMinShare 1.5"), [](FLureFishFightRow& R) { R.SideMinShare = 1.5f; } },
		{ TEXT("SideDrain -1"), [](FLureFishFightRow& R) { R.SideDrain = -1.f; } },
		{ TEXT("ReelSteps 0"), [](FLureFishFightRow& R) { R.ReelSteps = 0; } },
		{ TEXT("ReelSteps 10"), [](FLureFishFightRow& R) { R.ReelSteps = 10; } },
		{ TEXT("ReelDefaultStep past ReelSteps"), [](FLureFishFightRow& R) { R.ReelDefaultStep = R.ReelSteps + 1; } },
		{ TEXT("ReelDefaultStep 0"), [](FLureFishFightRow& R) { R.ReelDefaultStep = 0; } },
		{ TEXT("ReelSpeedMax below ReelSpeedMin"), [](FLureFishFightRow& R) { R.ReelSpeedMax = 0.5f * R.ReelSpeedMin; } },
		{ TEXT("ReelSpeedMin 0"), [](FLureFishFightRow& R) { R.ReelSpeedMin = 0.f; } },
		{ TEXT("CameraRodYawShare 1.5"), [](FLureFishFightRow& R) { R.CameraRodYawShare = 1.5f; } },
		{ TEXT("CameraFollowTime -1"), [](FLureFishFightRow& R) { R.CameraFollowTime = -1.f; } },
		{ TEXT("RodAimBlendTime NaN"), [NaN](FLureFishFightRow& R) { R.RodAimBlendTime = NaN; } },
	};
	for (const FBad& Case : Bad)
	{
		FLureFishFightRow Row = T;
		Case.Break(Row);
		FString Why;
		TestFalse(FString::Printf(TEXT("%s is invalid"), Case.What), Row.Validate(Why));
	}
	{
		TArray<FString> Lines;
		Data.FightCsv.ParseIntoArrayLines(Lines, true);
		TArray<FString> Header, Values;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		Lines[1].ParseIntoArray(Values, TEXT(","), false);
		const int32 First = Header.IndexOfByPredicate([](const FString& H) { return H.TrimStartAndEnd() == TEXT("RodAimUpDeg"); });
		if (TestTrue(TEXT("DT_FishFight.csv has the rod-steering columns (from RodAimUpDeg on)"), First != INDEX_NONE))
		{
			Header.SetNum(First);
			Values.SetNum(First);
			TStrongObjectPtr<UDataTable> Table;
			const TArray<FString> Problems = LureFightQA::MakeTable(Table, FLureFishFightRow::StaticStruct(), FString::Join(Header, TEXT(",")) + TEXT("\n") + FString::Join(Values, TEXT(",")) + TEXT("\n"), false);
			TestEqual(TEXT("a CSV without the T-028 columns imports: ") + FString::Join(Problems, TEXT(" | ")), Problems.Num(), 0);
			const FLureFishFightRow* Old = Table->FindRow<FLureFishFightRow>(TEXT("Default"), TEXT("test"), false);
			const FLureFishFightRow Defaults;
			TestTrue(TEXT("... and gets the struct defaults = the shipped values"), Old && Old->RodAimSideDeg == Defaults.RodAimSideDeg && Old->SideDrain == Defaults.SideDrain
				&& Old->ReelSteps == Defaults.ReelSteps && Old->ReelLoadPerSpeed == Defaults.ReelLoadPerSpeed && Old->SideDrain == T.SideDrain && Old->ReelSpeedMax == T.ReelSpeedMax);
		}
	}
	return true;
}

// =====================================================================================================================
// The arms read the rod aim (the aim offset); the placeholder rod turn follows it until the aim offset is wired
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureRodArms, "Project.Fishing.Fight.Rod.ArmsFollowTheRodAim", Flags)
bool FLureRodArms::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("UFPArmsAnimInstance: the aim offset flag defaults off (the fishing code turns the rod until ABP_FPArms sets it)"),
		GetDefault<UFPArmsAnimInstance>()->bRodAimOffsetInGraph);
	for (const TCHAR* Name : { TEXT("RodAimPitch"), TEXT("RodAimYaw") })
	{
		const FProperty* Property = UFPArmsAnimInstance::StaticClass()->FindPropertyByName(Name);
		TestTrue(FString::Printf(TEXT("UFPArmsAnimInstance.%s is a Blueprint-readable float (the aim offset's axis)"), Name),
			Property && CastField<FFloatProperty>(Property) && Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}

	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	FFishInstance Bonefish;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 131, Bonefish))
	{
		return false;
	}
	LureFightQA::FWorld World;
	FOwnerRig Owner;
	if (!World.Create(*this) || !Owner.Create(*this, World, Data, Fish))
	{
		return false;
	}
	ULureFishingComponent* Fishing = Owner.Fishing;
	if (!LureFightQA::CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
	{
		Owner.Release();
		return false;
	}
	World.Tick(1);
	Owner.Character->DoLook(0.5f * T.RodAimSideDeg, -0.5f * T.RodAimDownDeg); // half right, half dipped
	World.Tick(45);
	const FVector2D Eased = Fishing->GetRodAimForAnimation();
	TestTrue(FString::Printf(TEXT("the eased aim reaches the rod aim (%.3f, %.3f)"), Eased.X, Eased.Y), Eased.Equals(FVector2D(0.5f, -0.5f), 0.01f));
	if (const UFPArmsAnimInstance* Anim = Cast<UFPArmsAnimInstance>(Owner.Character->GetFirstPersonArms()->GetAnimInstance()))
	{
		World.Tick(1);
		TestTrue(FString::Printf(TEXT("the arms' anim instance reads it (yaw %.3f, pitch %.3f)"), Anim->RodAimYaw, Anim->RodAimPitch),
			FMath::IsNearlyEqual(Anim->RodAimYaw, 0.5f, 0.02f) && FMath::IsNearlyEqual(Anim->RodAimPitch, -0.5f, 0.02f));
	}
	else
	{
		AddInfo(TEXT("ABP_FPArms is not a UFPArmsAnimInstance here (not imported): the anim-instance check is skipped."));
	}
	if (const UStaticMeshComponent* RodMesh = Fishing->GetRodMesh())
	{
		const FRotator Rotation = RodMesh->GetRelativeRotation();
		TestNearlyEqual(TEXT("placeholder rod turn: yaw = aim x RodAimLookYawDeg"), static_cast<float>(Rotation.Yaw), 0.5f * T.RodAimLookYawDeg, 0.5f);
	}
	else
	{
		AddInfo(TEXT("SK_FPArms or SM_Rod_Basic is not imported here: the rod turn check is skipped."));
	}
	Owner.Release();
	return true;
}

} // namespace LureRodFightTest

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure T-028 QA (qa-engineer, 2026-09-23): the rod-steering maths of the reel fight, black-box.
// Project.Fishing.Fight.Rod.QA.{Sim, Math, Side, Reel, Timers}.*
// Oracle: an independent transcription of the FishFight.h formula comment (steps 1-6 with the T-028 rod factors) checks every step
// of ~160 fights with random rod input, including hostile values. The edge tests restate the spec's rules at their boundaries.
// Tables come from the text sources in data/tables/ (FightQATestUtils.h), never from the binary assets.

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <cmath>

namespace LureRodQA
{
	// =================================================================================================================
	// The oracle: FishFight.h steps 1-6 with the rod input, restated from the header comment and the spec
	// =================================================================================================================

	namespace Oracle
	{
		struct FPre
		{
			float LineOut, Tension, Stamina, OverTime, SlackTime, MoveTimeLeft, SideSign, SideDeg;
			int32 MoveIndex, Steps;
			bool bExhausted;
		};

		inline FPre Snapshot(const FLureFightState& S)
		{
			return { S.LineOut, S.Tension, S.Stamina, S.OverTime, S.SlackTime, S.MoveTimeLeft, S.SideSign, S.SideDeg, S.MoveIndex, S.Steps, S.bExhausted };
		}

		inline double Clamp01(double X) { return FMath::Clamp(X, 0.0, 1.0); }
		inline bool Near(double A, double B, double Rel = 1.0e-4) { return FMath::Abs(A - B) <= Rel * FMath::Max(1.0, FMath::Abs(B)); }

		/** The spec's reel steps: ReelSteps (1-9) evenly from ReelSpeedMin to ReelSpeedMax; INDEX_NONE / negative = ReelDefaultStep (1-based). */
		inline int32 NumSteps(const FLureFishFightRow& T) { return FMath::Clamp(T.ReelSteps, 1, 9); }
		inline int32 DefaultStep(const FLureFishFightRow& T) { return FMath::Clamp(T.ReelDefaultStep - 1, 0, NumSteps(T) - 1); }
		inline int32 CleanStep(int32 Step, const FLureFishFightRow& T) { return Step < 0 ? DefaultStep(T) : FMath::Min(Step, NumSteps(T) - 1); }
		inline double StepSpeed(int32 Step, const FLureFishFightRow& T)
		{
			const int32 N = NumSteps(T);
			return N == 1 ? 1.0 : T.ReelSpeedMin + (static_cast<double>(T.ReelSpeedMax) - T.ReelSpeedMin) * CleanStep(Step, T) / static_cast<double>(N - 1);
		}
		inline double StepLoad(int32 Step, const FLureFishFightRow& T) { return FMath::Max(0.0, 1.0 + (StepSpeed(Step, T) - 1.0) * T.ReelLoadPerSpeed); }

		enum class EResult : uint8 { Match, Skipped, Mismatch };

		struct FSeen
		{
			int32 RunLeft = 0, RunRight = 0, RunNone = 0, Against = 0, With = 0, Hostile = 0;
			TSet<int32> Steps;
		};

		/** Checks one step (Pre + the raw input -> State) against the spec. A newly picked move and its side are read from State (the RNG's business). */
		inline EResult Check(const FPre& Pre, const FLureFightInput& Raw, const FLureFightState& State, FSeen& Seen, FString& Why)
		{
			const FLureFishFightRow& T = State.Tuning;
			const FLureGearStats& G = State.Gear;
			const FLureFightFish& F = State.Fish;
			const float Dt = 1.f / static_cast<float>(FMath::Clamp(T.SimRate, 10, 240));

			// The server clamps whatever arrives: pitch and yaw into [-1, 1] (non-finite = 0), the step into a real one.
			const double P01 = Unit(Raw.RodPitch);
			const double Y01 = Unit(Raw.RodYaw);
			const int32 Step = CleanStep(Raw.ReelStep, T);

			// 1. The move used this step. Its clock runs (1 + S+ x SideTurnRate) x dt, S from the move it was making.
			const FLureFightMove* Move = nullptr;
			float SideSign = Pre.SideSign;
			if (!Pre.bExhausted)
			{
				const FLureFightMove* PreMove = State.Pattern.Moves.IsValidIndex(Pre.MoveIndex) ? &State.Pattern.Moves[Pre.MoveIndex] : nullptr;
				if (PreMove && FMath::Abs(FMath::Abs(PreMove->Side) - T.SideMinShare) < 1.0e-6f)
				{
					return EResult::Skipped; // |Side| right at SideMinShare: a knife edge for float comparisons
				}
				const double SPre = FMath::Clamp(-Y01 * ExpectedRunDir(PreMove, Pre.SideSign, T.SideMinShare), -1.0, 1.0);
				const double Clock = 1.0 + FMath::Max(0.0, SPre) * T.SideTurnRate;
				const double Left = Pre.MoveTimeLeft - Dt * Clock;
				if (FMath::Abs(Left) < 1.0e-5)
				{
					return EResult::Skipped;
				}
				if (Left <= 0.0)
				{
					if (State.bExhausted)
					{
						return EResult::Skipped; // the new pick was cleared by the exhaustion in this very step: not observable
					}
					Move = State.GetMove();
					SideSign = State.SideSign;
				}
				else
				{
					Move = PreMove;
					if (!State.bExhausted)
					{
						if (FMath::Abs(Left - State.MoveTimeLeft) > 1.0e-4)
						{
							Why += FString::Printf(TEXT(" move clock: time left expected %.6f got %.6f (S %.2f);"), Left, State.MoveTimeLeft, SPre);
						}
						if (State.MoveIndex != Pre.MoveIndex || State.SideSign != Pre.SideSign)
						{
							Why += TEXT(" the move or its side changed before its time was up;");
						}
					}
				}
			}
			if (Move && FMath::Abs(FMath::Abs(Move->Side) - T.SideMinShare) < 1.0e-6f)
			{
				return EResult::Skipped;
			}

			// 2. The rod factors.
			const int32 RunDir = ExpectedRunDir(Move, SideSign, T.SideMinShare);
			const double S = FMath::Clamp(-Y01 * RunDir, -1.0, 1.0);
			const double SPlus = FMath::Max(0.0, S);
			const double P = P01 >= 0.0 ? 1.0 + P01 * T.PitchBackPressure : 1.0 + P01 * T.PitchDipPressure;
			const double PowerF = P * (1.0 + S * T.SideLeverage);
			const double PullF = 1.0 - SPlus * T.SideTurnPull;
			const double DrainF = 1.0 + SPlus * T.SideDrain;
			const double SpeedF = StepSpeed(Step, T);
			const double LoadF = StepLoad(Step, T);

			// 3. The fish (turned: pulls less; its swim speed is its own).
			const double SF = T.TiredPull + (1.0 - T.TiredPull) * Clamp01(Pre.Stamina);
			const double Pull = (Move ? F.BasePull * Move->Pull * SF : F.BasePull * T.TiredPull) * PullF;
			const double Speed = Move ? F.BaseSpeed * Move->Speed * SF : 0.0;
			const double Va = Move ? Speed * FMath::Clamp(static_cast<double>(Move->Away), -1.0, 1.0) : 0.0;

			// 4. The line: the rod's power and the reel speed are scaled; the drag is not.
			const double RodPower = G.RodPower * PowerF;
			const double ReelSpeed = G.ReelSpeed * SpeedF;
			double Gain = 0.0;
			double Taken = Va;
			double Target = 0.0;
			if (Raw.bReeling)
			{
				Gain = (RodPower > 0.0 && ReelSpeed > 0.0) ? ReelSpeed * Clamp01(1.0 - Pull / RodPower) : 0.0;
				if (Va > 0.0)
				{
					Taken = RodPower > 0.0 ? Va * Clamp01(Pull / RodPower - 1.0) : Va;
				}
				Target = (Pull * T.ReelStrain + G.RodPower * T.ReelLoad * LoadF) * P;
			}
			else
			{
				if (Va > 0.0)
				{
					Taken = G.Drag > 0.f ? Va * Clamp01((Pull / G.Drag - T.DragHold) / (1.0 - T.DragHold)) : Va;
				}
				Target = FMath::Min(Pull * P, static_cast<double>(G.Drag)); // letting it run never goes over the drag
			}
			const double LineOut = FMath::Max(0.0, Pre.LineOut + (Taken - Gain) * Dt);

			// 5. Tension eases toward the target.
			const double Tau = Target > Pre.Tension ? T.TensionRiseTime : T.TensionFallTime;
			const double Tension = FMath::Max(0.0, Tau > 0.0 ? Pre.Tension + (Target - Pre.Tension) * (1.0 - FMath::Exp(-Dt / Tau)) : Target);

			// 6. Stamina: a turned fish tires (1 + S+ x SideDrain) times as fast.
			const double Slack = FMath::Max(0.01, static_cast<double>(T.SlackShare) * F.BasePull);
			const double Pool = FMath::Max(0.01, static_cast<double>(F.StaminaPool));
			if (Near(Tension, Slack))
			{
				return EResult::Skipped;
			}
			const bool bSlack = Tension < Slack;
			const double Energy = Pre.Stamina * Pool - Tension * Dt * DrainF + (bSlack ? Pool * T.StaminaRecovery * Dt : 0.0);
			const double Stamina = Clamp01(Energy / Pool);
			if (!Pre.bExhausted && Near(Stamina, T.ExhaustedStamina))
			{
				return EResult::Skipped;
			}
			const bool bExhausted = Pre.bExhausted || Stamina <= T.ExhaustedStamina;

			// Cosmetic swing: Speed x Side x SideSign x dt / radius, x (1 - 2 S+) (a turned fish swings back), within MaxSideDeg.
			double SideDeg = Pre.SideDeg;
			if (Move)
			{
				SideDeg += FMath::RadiansToDegrees(Speed * FMath::Clamp(static_cast<double>(Move->Side), -1.0, 1.0) * SideSign * Dt / FMath::Max(100.0, LineOut)) * (1.0 - 2.0 * SPlus);
			}
			SideDeg = FMath::Clamp(SideDeg, -static_cast<double>(T.MaxSideDeg), static_cast<double>(T.MaxSideDeg));

			// Outcome (first that applies): Landed, Spooled, Snapped, ThrewHook; "longer than" the grace in whole steps.
			if (Near(LineOut, T.LandDistance) || Near(LineOut, G.SpoolLength) || Near(Tension, G.LineStrength))
			{
				return EResult::Skipped;
			}
			auto LongerThan = [Dt](double Time, double Grace)
			{
				return FMath::RoundToDouble(Time / Dt) > FMath::Max(0.0, Grace) / Dt + 1.0e-4;
			};
			ELureFightOutcome Outcome = ELureFightOutcome::None;
			double Over = Pre.OverTime;
			double SlackTime = Pre.SlackTime;
			bool bTimers = true;
			if (LineOut <= T.LandDistance)
			{
				Outcome = ELureFightOutcome::Landed;
				bTimers = false;
			}
			else if (G.SpoolLength > 0.f && LineOut > G.SpoolLength)
			{
				Outcome = ELureFightOutcome::Spooled;
				bTimers = false;
			}
			else
			{
				Over = Tension > G.LineStrength ? Pre.OverTime + Dt : 0.0;
				if (LongerThan(Over, T.SnapGraceTime))
				{
					Outcome = ELureFightOutcome::Snapped;
				}
				else
				{
					SlackTime = bSlack ? Pre.SlackTime + Dt : 0.0;
					if (LongerThan(SlackTime, static_cast<double>(T.SlackGraceTime) * G.HookSecurity))
					{
						Outcome = ELureFightOutcome::ThrewHook;
					}
				}
			}

			auto Off = [&Why](const TCHAR* Field, double Expected, double Actual, double Rel = 1.0e-4)
			{
				if (FMath::Abs(Expected - Actual) > Rel * FMath::Max(1.0, FMath::Abs(Expected)))
				{
					Why += FString::Printf(TEXT(" %s expected %.6f got %.6f;"), Field, Expected, Actual);
				}
			};
			Off(TEXT("Pull"), Pull, State.Pull);
			Off(TEXT("Speed"), Speed, State.Speed);
			Off(TEXT("LineOut"), LineOut, State.LineOut);
			Off(TEXT("Tension"), Tension, State.Tension);
			Off(TEXT("Stamina"), Stamina, State.Stamina);
			Off(TEXT("SideDeg"), SideDeg, State.SideDeg, 1.0e-3);
			Off(TEXT("recorded RodPitch"), P01, State.RodPitch, 0.0);
			Off(TEXT("recorded RodYaw"), Y01, State.RodYaw, 0.0);
			Off(TEXT("recorded Side score"), S, State.Side, 1.0e-6);
			if (State.ReelStep != Step)
			{
				Why += FString::Printf(TEXT(" recorded ReelStep expected %d got %d;"), Step, State.ReelStep);
			}
			if (State.RunDir != RunDir)
			{
				Why += FString::Printf(TEXT(" RunDir expected %d got %d;"), RunDir, State.RunDir);
			}
			if (State.bExhausted != bExhausted)
			{
				Why += FString::Printf(TEXT(" exhausted expected %d got %d;"), bExhausted, State.bExhausted);
			}
			if (State.Outcome != Outcome)
			{
				Why += FString::Printf(TEXT(" outcome expected %s got %s;"), *LureFightQA::OutcomeName(Outcome), *LureFightQA::OutcomeName(State.Outcome));
			}
			if (bTimers && Outcome == ELureFightOutcome::None)
			{
				Off(TEXT("OverTime"), Over, State.OverTime);
				Off(TEXT("SlackTime"), SlackTime, State.SlackTime);
			}
			if (State.Steps != Pre.Steps + 1)
			{
				Why += TEXT(" Steps did not advance by one;");
			}
			Seen.RunLeft += RunDir < 0 ? 1 : 0;
			Seen.RunRight += RunDir > 0 ? 1 : 0;
			Seen.RunNone += RunDir == 0 ? 1 : 0;
			Seen.Against += S > 0.0 ? 1 : 0;
			Seen.With += S < 0.0 ? 1 : 0;
			Seen.Hostile += (!FMath::IsFinite(Raw.RodPitch) || FMath::Abs(Raw.RodPitch) > 1.f || !FMath::IsFinite(Raw.RodYaw) || FMath::Abs(Raw.RodYaw) > 1.f
				|| Raw.ReelStep >= NumSteps(T) || Raw.ReelStep < INDEX_NONE) ? 1 : 0;
			Seen.Steps.Add(Step);
			return Why.IsEmpty() ? EResult::Match : EResult::Mismatch;
		}

		/** Rod axis values a client might send: the range, its ends, in between, out of range and non-finite. */
		inline float RandomAxis(FRandomStream& R)
		{
			static const float Menu[] = { -1.f, -0.75f, -0.5f, -0.25f, -0.f, 0.f, 0.2f, 0.5f, 0.8f, 1.f, 1.0000001f, -1.0000001f, 3.5f, -7.f, 1.0e30f };
			const int32 Pick = R.RandHelper(20);
			if (Pick < static_cast<int32>(UE_ARRAY_COUNT(Menu)))
			{
				return Menu[Pick];
			}
			if (Pick == 15)
			{
				return QNaN();
			}
			if (Pick == 16)
			{
				return R.FRand() < 0.5f ? QInf() : -QInf();
			}
			return R.FRandRange(-1.f, 1.f);
		}

		inline int32 RandomStep(FRandomStream& R, const FLureFishFightRow& T)
		{
			const int32 Pick = R.RandHelper(10);
			if (Pick == 0) { return INDEX_NONE; }
			if (Pick == 1) { return NumSteps(T) + R.RandHelper(300); }
			if (Pick == 2) { return -2 - R.RandHelper(1000); }
			return R.RandHelper(NumSteps(T));
		}

		/** A random pattern with sideways moves (random sides included). */
		inline FLureFightPatternRow RandomPattern(FRandomStream& R)
		{
			FLureFightPatternRow Pattern;
			const int32 Count = 1 + R.RandHelper(5);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FLureFightMove Move;
				Move.Id = FName(*FString::Printf(TEXT("M%d"), Index));
				Move.Label = FText::FromName(Move.Id);
				Move.Weight = Index == 0 ? R.FRandRange(0.5f, 4.f) : R.FRandRange(0.f, 4.f);
				Move.AggressionWeight = R.FRandRange(0.f, 0.2f);
				Move.DurationMin = R.FRandRange(0.05f, 2.f);
				Move.DurationMax = Move.DurationMin + R.FRandRange(0.f, 2.f);
				Move.Pull = R.FRandRange(0.f, 2.5f);
				Move.Speed = R.FRandRange(0.f, 2.5f);
				Move.Away = R.FRandRange(-1.f, 1.f);
				Move.Side = R.FRand() < 0.2f ? 0.f : R.FRandRange(-1.f, 1.f);
				Move.RandomSide = R.FRand() < 0.5f;
				Move.Down = R.FRandRange(-1.f, 1.f);
				Move.Rest = R.FRand() < 0.3f;
				Pattern.Moves.Add(Move);
			}
			Pattern.OpeningMove = R.FRand() < 0.5f ? Pattern.Moves[R.RandHelper(Count)].Id : NAME_None;
			return Pattern;
		}

		/** Rod-steering tuning anywhere inside the ranges the spec documents (Validate accepts them). */
		inline void RandomRodTuning(FRandomStream& R, FLureFishFightRow& T)
		{
			T.PitchBackPressure = R.FRandRange(0.f, 1.f);
			T.PitchDipPressure = R.FRandRange(0.f, 0.95f);
			T.SideMinShare = R.FRand() < 0.1f ? 0.f : R.FRandRange(0.f, 1.f);
			T.SideLeverage = R.FRandRange(0.f, 0.95f);
			T.SideTurnRate = R.FRandRange(0.f, 3.f);
			T.SideTurnPull = R.FRandRange(0.f, 0.95f);
			T.SideDrain = R.FRandRange(0.f, 4.f);
			T.ReelSteps = 1 + R.RandHelper(9);
			T.ReelDefaultStep = 1 + R.RandHelper(T.ReelSteps);
			T.ReelSpeedMin = R.FRandRange(0.05f, 1.f);
			T.ReelSpeedMax = T.ReelSpeedMin + (R.FRand() < 0.1f ? 0.f : R.FRandRange(0.f, 2.f));
			T.ReelLoadPerSpeed = R.FRandRange(0.f, 3.f);
		}
	}

	/**
	 *  Every step of ~160 fights (real rolled fish on the shipped data, random fish/gear/tuning inside the validated ranges, shipped,
	 *  fallback and random patterns with sideways and random-side moves) under random rod input - pitch, yaw and step, including
	 *  out-of-range, non-finite and INDEX_NONE values - matches an independent transcription of FishFight.h steps 1-6 with the rod.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASimOracle, "Project.Fishing.Fight.Rod.QA.Sim.StepMatchesSpecWithRodInput", Flags)
	bool FRodQASimOracle::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const FLureFishFightRow& Shipped = *Data.Tuning();
		const FFishLevelScaling& Scaling = GetDefault<UFishSettings>()->LevelScaling;

		struct FFixture
		{
			FString Name;
			FLureFightFish Fish;
			FLureFightPatternRow Pattern;
			FLureGearStats Gear;
			FLureFishFightRow Tuning;
			int32 Seed = 0;
			float Start = 1000.f;
		};
		TArray<FFixture> Fixtures;
		{
			FFishInstance Bonefish, Snapper;
			if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Rare"), 0.5f, 31, Bonefish)
				|| !LureFightQA::RollFish(*this, Fish, TEXT("CoralSnapper"), TEXT("Common"), 0.3f, 32, Snapper))
			{
				return false;
			}
			const FLureGearStats Kits[] = { Data.Starter(), Data.Resolve(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Squid")) };
			int32 Seed = 500;
			for (const FFishInstance* Real : { &Bonefish, &Snapper })
			{
				for (const FName PatternId : { FName(TEXT("Run")), FName(TEXT("Dive")), FName(TEXT("Dart")) })
				{
					for (const FLureGearStats& Kit : Kits)
					{
						Fixtures.Add({ FString::Printf(TEXT("%s/%s/%s"), *Real->SpeciesId.ToString(), *PatternId.ToString(), *Kit.RodId.ToString()),
							FLureFight::MakeFish(*Real, Shipped, 1, Scaling), *Data.Pattern(PatternId), Kit, Shipped, ++Seed, 1000.f });
					}
				}
			}
		}
		FRandomStream R(20260928);
		const FName ShippedPatterns[] = { TEXT("Run"), TEXT("Dive"), TEXT("Dart") };
		const int32 Rates[] = { 10, 30, 60, 120, 240 };
		for (int32 Index = 0; Index < 150; ++Index)
		{
			FFixture X;
			X.Name = FString::Printf(TEXT("random %d"), Index);
			X.Tuning = Shipped;
			Oracle::RandomRodTuning(R, X.Tuning);
			if (Index % 3 != 0)
			{
				X.Tuning.TiredPull = R.FRandRange(0.f, 1.f);
				X.Tuning.ExhaustedStamina = R.FRandRange(0.f, 0.3f);
				X.Tuning.StaminaRecovery = R.FRandRange(0.f, 0.2f);
				X.Tuning.ReelStrain = R.FRandRange(0.5f, 2.5f);
				X.Tuning.ReelLoad = R.FRandRange(0.f, 0.5f);
				X.Tuning.DragHold = R.FRandRange(0.f, 0.95f);
				X.Tuning.TensionRiseTime = R.FRand() < 0.15f ? 0.f : R.FRandRange(0.02f, 0.6f);
				X.Tuning.TensionFallTime = R.FRand() < 0.15f ? 0.f : R.FRandRange(0.02f, 0.6f);
				X.Tuning.SnapGraceTime = R.FRandRange(0.f, 1.5f);
				X.Tuning.SlackShare = R.FRandRange(0.f, 0.8f);
				X.Tuning.SlackGraceTime = R.FRandRange(0.2f, 4.f);
				X.Tuning.LandDistance = R.FRandRange(50.f, 300.f);
				X.Tuning.MaxSideDeg = R.FRandRange(5.f, 170.f);
				X.Tuning.SimRate = Rates[R.RandHelper(static_cast<int32>(UE_ARRAY_COUNT(Rates)))];
			}
			FString Problem;
			const bool bValid = X.Tuning.Validate(Problem);
			if (!TestTrue(FString::Printf(TEXT("QA fixture %d is valid data: %s"), Index, *Problem), bValid))
			{
				return false;
			}
			X.Gear = LureFightQA::MakeGear(R.FRandRange(1.f, 30.f), R.FRandRange(0.f, 300.f), R.FRand() < 0.1f ? 0.f : R.FRandRange(0.5f, 30.f), R.FRandRange(2.f, 40.f),
				R.FRandRange(800.f, 8000.f), R.FRandRange(0.2f, 3.f));
			X.Fish = LureFightQA::MakeFightFish(R.FRandRange(0.05f, 20.f), R.FRandRange(0.f, 300.f), R.FRandRange(0.5f, 200.f), R.FRandRange(0.f, 60.f), R.FRandRange(0.25f, 4.f));
			const int32 PatternPick = R.RandHelper(6);
			X.Pattern = PatternPick < 3 ? *Data.Pattern(ShippedPatterns[PatternPick]) : (PatternPick == 3 ? FLureFightPatternRow::GetFallbackPattern() : Oracle::RandomPattern(R));
			X.Seed = static_cast<int32>(R.GetUnsignedInt());
			X.Start = R.FRandRange(X.Tuning.LandDistance + 10.f, 3000.f);
			Fixtures.Add(MoveTemp(X));
		}

		int32 Checked = 0, Skipped = 0, Failed = 0;
		TMap<ELureFightOutcome, int32> Outcomes;
		Oracle::FSeen Seen;
		FRandomStream Inputs(8128);
		for (const FFixture& X : Fixtures)
		{
			FLureFightState S;
			FLureFight::Begin(S, X.Fish, X.Pattern, TEXT("QA_Rod"), X.Gear, X.Tuning, X.Seed, X.Start);
			FLureFightInput Input = In(Inputs.FRand() < 0.5f, Oracle::RandomAxis(Inputs), Oracle::RandomAxis(Inputs), Oracle::RandomStep(Inputs, X.Tuning));
			while (!S.IsOver() && S.Steps < 2400)
			{
				if (Inputs.FRand() < 1.f / 25.f) { Input.bReeling = !Input.bReeling; }
				if (Inputs.FRand() < 1.f / 20.f) { Input.RodPitch = Oracle::RandomAxis(Inputs); }
				if (Inputs.FRand() < 1.f / 20.f) { Input.RodYaw = Oracle::RandomAxis(Inputs); }
				if (Inputs.FRand() < 1.f / 40.f) { Input.ReelStep = Oracle::RandomStep(Inputs, X.Tuning); }
				const Oracle::FPre Pre = Oracle::Snapshot(S);
				FLureFight::Step(S, Input);
				FString Why;
				const Oracle::EResult Result = Oracle::Check(Pre, Input, S, Seen, Why);
				if (Result == Oracle::EResult::Skipped)
				{
					++Skipped;
					continue;
				}
				++Checked;
				if (Result == Oracle::EResult::Mismatch)
				{
					++Failed;
					if (Failed <= 10)
					{
						AddError(FString::Printf(TEXT("%s, step %d (reel %d, pitch %g, yaw %g, step %d, move %s):%s"), *X.Name, S.Steps, Input.bReeling, Input.RodPitch, Input.RodYaw,
							Input.ReelStep, *S.GetMoveId().ToString(), *Why));
					}
					break;
				}
			}
			Outcomes.FindOrAdd(S.Outcome)++;
		}
		AddInfo(FString::Printf(TEXT("%d fixtures, %d steps checked, %d skipped at a threshold; runs left %d / right %d / none %d; rod against %d / with %d; hostile inputs %d; reel steps used %d; ")
			TEXT("outcomes: landed %d, snapped %d, spooled %d, threw hook %d, still on %d"), Fixtures.Num(), Checked, Skipped, Seen.RunLeft, Seen.RunRight, Seen.RunNone, Seen.Against,
			Seen.With, Seen.Hostile, Seen.Steps.Num(), Outcomes.FindRef(ELureFightOutcome::Landed), Outcomes.FindRef(ELureFightOutcome::Snapped),
			Outcomes.FindRef(ELureFightOutcome::Spooled), Outcomes.FindRef(ELureFightOutcome::ThrewHook), Outcomes.FindRef(ELureFightOutcome::None)));
		TestEqual(TEXT("fixtures whose steps don't match the documented formulas"), Failed, 0);
		TestTrue(FString::Printf(TEXT("the check covers many steps (%d)"), Checked), Checked > 50000);
		TestTrue(FString::Printf(TEXT("threshold ties are rare (%d of %d)"), Skipped, Checked + Skipped), Skipped * 20 < Checked + Skipped);
		TestTrue(TEXT("both run sides and straight moves were checked"), Seen.RunLeft > 1000 && Seen.RunRight > 1000 && Seen.RunNone > 1000);
		TestTrue(TEXT("the rod was against and with the run"), Seen.Against > 1000 && Seen.With > 1000);
		TestTrue(TEXT("hostile inputs were checked"), Seen.Hostile > 1000);
		TestTrue(TEXT("every reel step 0..8 was used somewhere"), Seen.Steps.Num() >= 9);
		for (const ELureFightOutcome Outcome : { ELureFightOutcome::Landed, ELureFightOutcome::Snapped, ELureFightOutcome::Spooled, ELureFightOutcome::ThrewHook })
		{
			TestTrue(TEXT("the fixtures reach every outcome: ") + LureFightQA::OutcomeName(Outcome), Outcomes.FindRef(Outcome) > 0);
		}
		return true;
	}

	// =================================================================================================================
	// Pitch at the edges
	// =================================================================================================================

	/** P = 1 + p x PitchBackPressure (p >= 0) or 1 + p x PitchDipPressure (p < 0), p clamped to [-1, 1], non-finite = level. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAPitchEdges, "Project.Fishing.Fight.Rod.QA.Math.PitchEdges", Flags)
	bool FRodQAPitchEdges::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureGearStats Starter = Data.Starter();
		const float Pitches[] = { -1.f, -0.5f, -1.0e-7f, -0.f, 0.f, 1.0e-7f, 1.f / 3.f, 0.5f, 1.f, 1.0000001f, -1.0000001f, 2.f, -2.f, 1.0e30f, -1.0e30f,
			TNumericLimits<float>::Max(), -TNumericLimits<float>::Max(), QInf(), -QInf(), QNaN() };
		for (const float Pitch : Pitches)
		{
			const float Clean = Unit(Pitch);
			const float Expected = Clean >= 0.f ? 1.f + Clean * T.PitchBackPressure : 1.f + Clean * T.PitchDipPressure;
			TestNearlyEqual(FString::Printf(TEXT("pitch %g: P = %g"), Pitch, Expected), FLureFight::PitchPressure(Pitch, T), Expected, 1.0e-6f);
			const FLureRodFactors Factors = FLureFight::RodFactors(In(true, Pitch), nullptr, 1.f, T);
			TestNearlyEqual(FString::Printf(TEXT("pitch %g: RodFactors.Pressure"), Pitch), Factors.Pressure, Expected, 1.0e-6f);
			TestNearlyEqual(FString::Printf(TEXT("pitch %g: RodFactors.Power = P (no run)"), Pitch), Factors.Power, Expected, 1.0e-6f);
			TestTrue(FString::Printf(TEXT("pitch %g: the server keeps exactly %g"), Pitch, Clean), FLureFight::SanitizeInput(In(true, Pitch), T).RodPitch == Clean);
		}
		TestTrue(TEXT("level is exactly 1 (and -0 too)"), FLureFight::PitchPressure(0.f, T) == 1.f && FLureFight::PitchPressure(-0.f, T) == 1.f);

		// "Pulling the rod back or up raises line tension; dipping it lowers tension": reeling, for every pull; letting it run, never above the drag.
		for (const float Pull : { 0.f, 0.5f, 2.f, 6.f, 20.f, 200.f })
		{
			const float Back = FLureFight::TargetTension(Pull, true, Starter, T, FLureFight::RodFactors(In(true, 1.f), nullptr, 1.f, T));
			const float Level = FLureFight::TargetTension(Pull, true, Starter, T, FLureFight::RodFactors(In(true, 0.f), nullptr, 1.f, T));
			const float Dipped = FLureFight::TargetTension(Pull, true, Starter, T, FLureFight::RodFactors(In(true, -1.f), nullptr, 1.f, T));
			TestTrue(FString::Printf(TEXT("reeling, pull %g: back %.3f > level %.3f > dipped %.3f"), Pull, Back, Level, Dipped), Back > Level && Level > Dipped);
			for (const float Pitch : { -1.f, 0.f, 1.f })
			{
				const float Run = FLureFight::TargetTension(Pull, false, Starter, T, FLureFight::RodFactors(In(false, Pitch), nullptr, 1.f, T));
				TestTrue(FString::Printf(TEXT("letting it run, pull %g, pitch %+.0f: %.3f <= drag %.3f"), Pull, Pitch, Run, Starter.Drag), Run <= Starter.Drag);
			}
		}

		// The data's edges. PitchBackPressure 0: pulling back does nothing. PitchDipPressure at its 0.95 limit: the rod keeps 5 % of its power,
		// the line still comes in on a fish that doesn't pull, and nothing divides by zero.
		FLureFishFightRow NoBack = T;
		NoBack.PitchBackPressure = 0.f;
		TestEqual(TEXT("PitchBackPressure 0: fully back is level"), FLureFight::PitchPressure(1.f, NoBack), 1.f);
		FLureFishFightRow MaxDip = T;
		MaxDip.PitchDipPressure = 0.95f;
		FString Problem;
		const bool bMaxDipValid = MaxDip.ValidateRodSteering(Problem);
		TestTrue(TEXT("PitchDipPressure 0.95 is valid data: ") + Problem, bMaxDipValid);
		const FLureRodFactors Dipped = FLureFight::RodFactors(In(true, -1.f), nullptr, 1.f, MaxDip);
		TestNearlyEqual(TEXT("PitchDipPressure 0.95, fully dipped: P = 0.05"), Dipped.Pressure, 0.05f, 1.0e-6f);
		TestTrue(TEXT("... the line still comes in on a fish that doesn't pull"), FLureFight::LineGainSpeed(0.f, true, Starter, Dipped) > 0.f);
		TestTrue(TEXT("... and a strong fish is finite"), FMath::IsFinite(FLureFight::LineGainSpeed(1000.f, true, Starter, Dipped))
			&& FMath::IsFinite(FLureFight::LineTakenSpeed(1000.f, 300.f, true, Starter, MaxDip, Dipped)) && FMath::IsFinite(FLureFight::TargetTension(1000.f, true, Starter, MaxDip, Dipped)));

		// Bad data that slipped past Validate (a hand-made row): never a negative or non-finite pressure.
		for (const float Bad : { 5.f, 1.f, -3.f, QNaN(), QInf() })
		{
			FLureFishFightRow Row = T;
			Row.PitchDipPressure = Bad;
			Row.PitchBackPressure = Bad;
			for (const float Pitch : { -1.f, 1.f })
			{
				const float Pressure = FLureFight::PitchPressure(Pitch, Row);
				TestTrue(FString::Printf(TEXT("bad pressure data %g, pitch %+.0f: P = %g is finite and >= 0"), Bad, Pitch, Pressure), FMath::IsFinite(Pressure) && Pressure >= 0.f);
			}
		}
		return true;
	}

	// =================================================================================================================
	// Yaw, run direction and side score at the edges
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAYawEdges, "Project.Fishing.Fight.Rod.QA.Math.YawAndRunDirectionEdges", Flags)
	bool FRodQAYawEdges::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const float Share = T.SideMinShare;
		TestTrue(TEXT("shipped SideMinShare is a real share"), Share > 0.f && Share < 1.f);
		const float Sides[] = { -1.f, -0.5f, -Share, std::nextafter(-Share, 0.f), -1.0e-7f, -0.f, 0.f, 1.0e-7f, std::nextafter(Share, 0.f), Share, 0.5f, 1.f, 1.5f, -3.f, QNaN() };
		for (const float Side : Sides)
		{
			for (const float SideSign : { -1.f, 1.f })
			{
				FLureFightMove Move = SideMove(TEXT("M"), 1.f, 1.f, 1.f, 0.f);
				Move.Side = Side;
				const int32 Expected = ExpectedRunDir(&Move, SideSign, Share);
				TestEqual(FString::Printf(TEXT("Side %g x side sign %+.0f: run direction %d"), Side, SideSign, Expected), FLureFight::RunDirection(&Move, SideSign, T), Expected);
			}
		}
		const FLureFightMove AtShare = SideMove(TEXT("M"), 1.f, 1.f, 1.f, -Share);
		TestEqual(TEXT("|Side| exactly SideMinShare counts as a run ('at or above')"), FLureFight::RunDirection(&AtShare, 1.f, T), -1);
		TestEqual(TEXT("an exhausted fish (no move): no run"), FLureFight::RunDirection(nullptr, 1.f, T), 0);

		FLureFishFightRow Zero = T;
		Zero.SideMinShare = 0.f;
		FLureFightMove Tiny = SideMove(TEXT("Tiny"), 1.f, 1.f, 1.f, 1.0e-6f);
		FLureFightMove Straight = SideMove(TEXT("Straight"), 1.f, 1.f, 1.f, 0.f);
		TestEqual(TEXT("SideMinShare 0: any sideways share is a run"), FLureFight::RunDirection(&Tiny, -1.f, Zero), -1);
		TestEqual(TEXT("SideMinShare 0: a straight move is still no run"), FLureFight::RunDirection(&Straight, 1.f, Zero), 0);
		FLureFishFightRow One = T;
		One.SideMinShare = 1.f;
		FLureFightMove Most = SideMove(TEXT("Most"), 1.f, 1.f, 1.f, 0.99f);
		FLureFightMove Full = SideMove(TEXT("Full"), 1.f, 1.f, 1.f, 1.f);
		TestEqual(TEXT("SideMinShare 1: 0.99 is no run"), FLureFight::RunDirection(&Most, 1.f, One), 0);
		TestEqual(TEXT("SideMinShare 1: 1.0 is a run"), FLureFight::RunDirection(&Full, 1.f, One), 1);

		// The side score: clamp(-RodYaw x RunDir, -1, 1), the yaw clamped to [-1, 1] and non-finite = centered.
		const float Yaws[] = { -1.f, -0.5f, -0.f, 0.f, 0.25f, 1.f, 1.0000001f, 2.f, -2.f, 1.0e30f, QInf(), -QInf(), QNaN() };
		for (const float Yaw : Yaws)
		{
			for (const int32 RunDir : { -1, 0, 1, 5, -7 })
			{
				const float Expected = FMath::Clamp(-Unit(Yaw) * static_cast<float>(FMath::Clamp(RunDir, -1, 1)), -1.f, 1.f);
				TestEqual(FString::Printf(TEXT("yaw %g vs run %d: side score %g"), Yaw, RunDir, Expected), FLureFight::SideScore(Yaw, RunDir), Expected);
			}
		}
		return true;
	}

	// =================================================================================================================
	// Side pressure vs run direction: every combination
	// =================================================================================================================

	/** Every move side x random side x rod yaw x pitch (and an exhausted fish): the side score and the factors follow the spec. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASideFactors, "Project.Fishing.Fight.Rod.QA.Side.EveryCombinationFactors", Flags)
	bool FRodQASideFactors::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		TestTrue(TEXT("shipped: turning does something (SideTurnRate, SideTurnPull, SideDrain > 0) and leverage both ways (SideLeverage > 0)"),
			T.SideTurnRate > 0.f && T.SideTurnPull > 0.f && T.SideDrain > 0.f && T.SideLeverage > 0.f);
		int32 Cases = 0;
		for (const float Side : { -1.f, -0.3f, -0.1f, 0.f, 0.1f, 0.3f, 1.f })
		{
			for (const float SideSign : { -1.f, 1.f })
			{
				for (const bool bExhausted : { false, true })
				{
					const FLureFightMove Move = SideMove(TEXT("M"), 1.f, 1.f, 1.f, Side);
					const FLureFightMove* Used = bExhausted ? nullptr : &Move;
					const int32 RunDir = ExpectedRunDir(Used, SideSign, T.SideMinShare);
					for (const float Yaw : { -1.f, -0.5f, 0.f, 0.5f, 1.f })
					{
						for (const float Pitch : { -1.f, 0.f, 1.f })
						{
							++Cases;
							const FLureRodFactors F = FLureFight::RodFactors(In(true, Pitch, Yaw), Used, SideSign, T);
							const float S = FMath::Clamp(-Yaw * static_cast<float>(RunDir), -1.f, 1.f);
							const float SPlus = FMath::Max(0.f, S);
							const float P = FLureFight::PitchPressure(Pitch, T);
							const FString Label = FString::Printf(TEXT("side %+.1f x sign %+.0f%s, yaw %+.1f, pitch %+.0f"), Side, SideSign, bExhausted ? TEXT(" (exhausted)") : TEXT(""), Yaw, Pitch);
							TestNearlyEqual(Label + TEXT(": side score"), F.Side, S, 1.0e-6f);
							TestNearlyEqual(Label + TEXT(": power = P x (1 + S x SideLeverage)"), F.Power, P * (1.f + S * T.SideLeverage), 1.0e-5f);
							TestNearlyEqual(Label + TEXT(": pull x (1 - S+ x SideTurnPull)"), F.Pull, 1.f - SPlus * T.SideTurnPull, 1.0e-6f);
							TestNearlyEqual(Label + TEXT(": move clock x (1 + S+ x SideTurnRate)"), F.MoveClock, 1.f + SPlus * T.SideTurnRate, 1.0e-6f);
							TestNearlyEqual(Label + TEXT(": drain x (1 + S+ x SideDrain)"), F.Drain, 1.f + SPlus * T.SideDrain, 1.0e-6f);
							if (S > 0.f)
							{
								TestTrue(Label + TEXT(": against the run the fish is turned (pulls less, its run ends sooner, it tires faster) and the rod gains leverage"),
									F.Pull < 1.f && F.MoveClock > 1.f && F.Drain > 1.f && F.Power > P);
							}
							else if (S < 0.f)
							{
								TestTrue(Label + TEXT(": with the run you lose ground (less power) and nothing turns the fish"), F.Power < P && F.Pull == 1.f && F.MoveClock == 1.f && F.Drain == 1.f);
							}
							else
							{
								TestTrue(Label + TEXT(": no side pressure: only the pitch acts"), F.Power == P && F.Pull == 1.f && F.MoveClock == 1.f && F.Drain == 1.f);
							}
						}
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%d combinations"), Cases));
		return true;
	}

	/**
	 *  The same fish running left, straight or right, reeled 2 s with the rod against, centered or with the run: against gains the most line,
	 *  pulls the least tension and tires the fish fastest; with the run loses ground but changes neither the tension nor the tiring; a left run
	 *  with the rod right is exactly the mirror of a right run with the rod left; a straight move ignores the yaw.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASideInFight, "Project.Fishing.Fight.Rod.QA.Side.EveryCombinationInTheFight", Flags)
	bool FRodQASideInFight::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureGearStats Starter = Data.Starter();
		struct FEnd { float LineOut, Tension, Stamina, SideDeg; int32 RunDir; };
		auto Fight = [&](float Side, float Yaw)
		{
			FLureFightState State;
			const FLureFightMove Move = SideMove(TEXT("Swim"), 1.2f, 1.f, 0.6f, Side);
			FLureFight::Begin(State, LureFightQA::MakeFightFish(3.f, 90.f, 150.f), OneMove(Move), TEXT("QA_Side"), Starter, T, 11, 2500.f);
			for (int32 Step = 0; Step < 120 && !State.IsOver(); ++Step)
			{
				FLureFight::Step(State, In(true, 0.f, Yaw));
			}
			TestEqual(FString::Printf(TEXT("side %+.0f, yaw %+.0f: still on after 2 s"), Side, Yaw), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::None));
			return FEnd{ State.LineOut, State.Tension, State.Stamina, State.SideDeg, State.RunDir };
		};
		for (const float Side : { -1.f, 1.f })
		{
			const int32 RunDir = Side < 0.f ? -1 : 1;
			const FEnd Against = Fight(Side, -static_cast<float>(RunDir));
			const FEnd Centered = Fight(Side, 0.f);
			const FEnd With = Fight(Side, static_cast<float>(RunDir));
			const FString Run = RunDir < 0 ? TEXT("left run") : TEXT("right run");
			TestTrue(FString::Printf(TEXT("%s: RunDir %d in the state"), *Run, RunDir), Against.RunDir == RunDir && Centered.RunDir == RunDir && With.RunDir == RunDir);
			TestTrue(FString::Printf(TEXT("%s: line out against %.1f < centered %.1f < with %.1f"), *Run, Against.LineOut, Centered.LineOut, With.LineOut),
				Against.LineOut < Centered.LineOut && Centered.LineOut < With.LineOut);
			TestTrue(FString::Printf(TEXT("%s: tension against %.3f < centered %.3f"), *Run, Against.Tension, Centered.Tension), Against.Tension < Centered.Tension);
			TestTrue(FString::Printf(TEXT("%s: stamina against %.4f < centered %.4f (a turned fish tires faster)"), *Run, Against.Stamina, Centered.Stamina), Against.Stamina < Centered.Stamina);
			TestTrue(FString::Printf(TEXT("%s: with the run the tension and the tiring are the centered ones (%.4f / %.4f)"), *Run, With.Tension, With.Stamina),
				With.Tension == Centered.Tension && With.Stamina == Centered.Stamina);
			TestTrue(FString::Printf(TEXT("%s: centered, the fish swings to its side (%.2f deg)"), *Run, Centered.SideDeg), Centered.SideDeg * static_cast<float>(RunDir) > 0.f);
			TestTrue(FString::Printf(TEXT("%s: turned, it swings back the other way (%.2f deg)"), *Run, Against.SideDeg), Against.SideDeg * static_cast<float>(RunDir) < 0.f);
		}
		const FEnd LeftRodRight = Fight(-1.f, 1.f);
		const FEnd RightRodLeft = Fight(1.f, -1.f);
		TestTrue(TEXT("mirror: a left run with the rod right = a right run with the rod left (line, tension, stamina bit for bit, swing mirrored)"),
			LeftRodRight.LineOut == RightRodLeft.LineOut && LeftRodRight.Tension == RightRodLeft.Tension && LeftRodRight.Stamina == RightRodLeft.Stamina
			&& LeftRodRight.SideDeg == -RightRodLeft.SideDeg);
		const FEnd StraightLeft = Fight(0.f, -1.f);
		const FEnd StraightCenter = Fight(0.f, 0.f);
		const FEnd StraightRight = Fight(0.f, 1.f);
		TestTrue(TEXT("a straight move: the yaw changes nothing (bit for bit)"), StraightLeft.LineOut == StraightCenter.LineOut && StraightRight.LineOut == StraightCenter.LineOut
			&& StraightLeft.Tension == StraightCenter.Tension && StraightRight.Stamina == StraightCenter.Stamina && StraightCenter.RunDir == 0);

		// The move clock: a 2 s run held against at S = 1 and S = 0.5 ends after 2 / (1 + S x SideTurnRate) s; with it (S < 0), after 2 s.
		for (const float Yaw : { 1.f, 0.5f, 0.f, -1.f })
		{
			FLureFightMove Burst = SideMove(TEXT("Burst"), 1.f, 1.f, 1.f, -1.f, 2.f);
			FLureFightMove Rest = LureFightQA::MakeMove(TEXT("Rest"), 0.3f, 0.f, 0.f, 50.f, true, 1.f);
			FLureFightPatternRow Pattern = LureFightQA::MakePattern({ Burst, Rest }, Burst.Id);
			Pattern.Moves[0].Weight = 0.f; // after the opening burst only the rest can be picked
			FLureFightState State;
			FLureFight::Begin(State, LureFightQA::MakeFightFish(2.f, 60.f, 1.0e6f), Pattern, TEXT("QA_Clock"), Starter, T, 5, 2500.f);
			float Ended = -1.f;
			for (int32 Step = 0; Step < 300 && Ended < 0.f; ++Step)
			{
				FLureFight::Step(State, In(true, 0.f, Yaw));
				if (State.GetMoveId() != Burst.Id)
				{
					Ended = State.Elapsed;
				}
			}
			const float S = FMath::Max(0.f, Yaw); // the burst runs left: RunDir -1, S = yaw
			const float Expected = 2.f / (1.f + S * T.SideTurnRate);
			TestNearlyEqual(FString::Printf(TEXT("yaw %+.1f against a left run: the 2 s run ends after %.3f s"), Yaw, Expected), Ended, Expected, 1.01f * FLureFight::StepSeconds(T));
		}
		return true;
	}

	/** A RandomSide move draws its side every time it starts: both sides come up, about evenly, and the side score always follows the drawn side. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQARandomSide, "Project.Fishing.Fight.Rod.QA.Side.RandomSideFlipsTheRun", Flags)
	bool FRodQARandomSide::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const FLureFightMove Zig = SideMove(TEXT("Zig"), 0.5f, 0.f, 0.f, 0.6f, 0.1f, /*bRandomSide*/ true);
		auto Run = [&](int32 Seed, TArray<int32>& OutSides)
		{
			FLureFightState State;
			FLureFight::Begin(State, LureFightQA::MakeFightFish(2.f, 0.f, 1.0e9f), OneMove(Zig), TEXT("QA_Zig"), Data.Starter(), T, Seed, 1000.f);
			int32 LastDir = 2;
			int32 Mismatch = 0;
			for (int32 Step = 0; Step < 60 * 60 && !State.IsOver(); ++Step)
			{
				const int32 Before = State.MoveIndex;
				const float BeforeLeft = State.MoveTimeLeft;
				FLureFight::Step(State, In(false, 0.f, 1.f)); // let it run: reeling lands the fish before the run flips often enough
				Mismatch += State.Side == FLureFight::SideScore(1.f, State.RunDir) && State.Side == -static_cast<float>(State.RunDir) ? 0 : 1;
				if (State.MoveTimeLeft > BeforeLeft || Before != State.MoveIndex || LastDir == 2)
				{
					OutSides.Add(State.RunDir); // a new start of the move
				}
				LastDir = State.RunDir;
			}
			return Mismatch;
		};
		TArray<int32> Sides;
		TestEqual(TEXT("every step: the side score is -yaw x the drawn side"), Run(77, Sides), 0);
		int32 Left = 0, Right = 0, None = 0;
		for (const int32 Dir : Sides)
		{
			Left += Dir < 0 ? 1 : 0;
			Right += Dir > 0 ? 1 : 0;
			None += Dir == 0 ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("%d move starts: left %d, right %d"), Sides.Num(), Left, Right));
		TestTrue(TEXT("many move starts"), Sides.Num() > 300);
		TestEqual(TEXT("a sideways RandomSide move always has a side"), None, 0);
		TestTrue(FString::Printf(TEXT("both sides about evenly (left %d of %d, 40-60 %%)"), Left, Sides.Num()), Left * 10 >= Sides.Num() * 4 && Left * 10 <= Sides.Num() * 6);
		TArray<int32> Again;
		Run(77, Again);
		TestTrue(TEXT("the same fight seed draws the same sides"), Again == Sides);
		return true;
	}

	// =================================================================================================================
	// Reel speed steps and clamping
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAReelSteps, "Project.Fishing.Fight.Rod.QA.Reel.StepsClampAndScale", Flags)
	bool FRodQAReelSteps::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& Shipped = *Data.Tuning();
		const FLureGearStats Starter = Data.Starter();

		// The number of steps is 1-9, the default is ReelDefaultStep (1-based) clamped into them.
		for (const int32 Steps : { -5, 0, 1, 2, 3, 9, 10, 100 })
		{
			FLureFishFightRow T = Shipped;
			T.ReelSteps = Steps;
			TestEqual(FString::Printf(TEXT("ReelSteps %d -> %d steps"), Steps, FMath::Clamp(Steps, 1, 9)), FLureFight::NumReelSteps(T), FMath::Clamp(Steps, 1, 9));
		}
		for (const int32 Default : { -3, 0, 1, 2, 3, 5, 100 })
		{
			FLureFishFightRow T = Shipped;
			T.ReelSteps = 3;
			T.ReelDefaultStep = Default;
			TestEqual(FString::Printf(TEXT("3 steps, ReelDefaultStep %d -> 0-based %d"), Default, FMath::Clamp(Default - 1, 0, 2)), FLureFight::DefaultReelStep(T), FMath::Clamp(Default - 1, 0, 2));
		}
		// Any step a client sends becomes a real one: negative = the default step, too high = the fastest.
		const int32 N = FLureFight::NumReelSteps(Shipped);
		const int32 ClientSteps[] = { TNumericLimits<int32>::Min(), -100, -2, static_cast<int32>(INDEX_NONE), 0, 1, N - 1, N, N + 1, 255, TNumericLimits<int32>::Max() };
		for (const int32 Step : ClientSteps)
		{
			const int32 Expected = Step < 0 ? FLureFight::DefaultReelStep(Shipped) : FMath::Min(Step, N - 1);
			TestEqual(FString::Printf(TEXT("step %d -> %d"), Step, Expected), FLureFight::ClampReelStep(Step, Shipped), Expected);
			TestEqual(FString::Printf(TEXT("step %d: the server keeps %d"), Step, Expected), FLureFight::SanitizeInput(In(true, 0.f, 0.f, Step), Shipped).ReelStep, Expected);
		}

		// Speeds: evenly from ReelSpeedMin to ReelSpeedMax for every count of steps; one step = speed 1 and load 1; loads never negative.
		for (int32 Steps = 1; Steps <= 9; ++Steps)
		{
			for (const TPair<float, float>& Range : { TPair<float, float>(0.5f, 1.5f), TPair<float, float>(0.05f, 3.f), TPair<float, float>(1.f, 1.f), TPair<float, float>(0.8f, 0.8f) })
			{
				FLureFishFightRow T = Shipped;
				T.ReelSteps = Steps;
				T.ReelDefaultStep = 1;
				T.ReelSpeedMin = Range.Key;
				T.ReelSpeedMax = Range.Value;
				float Previous = -1.f;
				bool bOk = true;
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					const float Speed = FLureFight::ReelStepSpeed(Step, T);
					const float Expected = Steps == 1 ? 1.f : Range.Key + (Range.Value - Range.Key) * static_cast<float>(Step) / static_cast<float>(Steps - 1);
					bOk &= FMath::IsNearlyEqual(Speed, Expected, 1.0e-5f) && Speed >= Previous;
					bOk &= FLureFight::ReelStepLoad(Step, T) >= 0.f;
					Previous = Speed;
				}
				bOk &= FLureFight::ReelStepSpeed(Steps + 5, T) == FLureFight::ReelStepSpeed(Steps - 1, T);
				TestTrue(FString::Printf(TEXT("%d steps from %.2f to %.2f: evenly spaced, never slower upward, loads >= 0, a step past the top is the top"), Steps, Range.Key, Range.Value), bOk);
			}
		}
		// Shipped: 0.5 / 1 / 1.5 with cranking loads x0.25 / x1 / x1.75 (spec); the default step is the T-007 reel exactly.
		TestNearlyEqual(TEXT("shipped slow step: speed 0.5"), FLureFight::ReelStepSpeed(0, Shipped), 0.5f, 1.0e-6f);
		TestNearlyEqual(TEXT("shipped fast step: speed 1.5"), FLureFight::ReelStepSpeed(2, Shipped), 1.5f, 1.0e-6f);
		TestNearlyEqual(TEXT("shipped slow step: load x0.25"), FLureFight::ReelStepLoad(0, Shipped), 0.25f, 1.0e-6f);
		TestNearlyEqual(TEXT("shipped fast step: load x1.75"), FLureFight::ReelStepLoad(2, Shipped), 1.75f, 1.0e-6f);
		TestTrue(TEXT("shipped default step: speed exactly 1"), FLureFight::ReelStepSpeed(FLureFight::DefaultReelStep(Shipped), Shipped) == 1.f);
		TestTrue(TEXT("shipped default step: load exactly 1"), FLureFight::ReelStepLoad(FLureFight::DefaultReelStep(Shipped), Shipped) == 1.f);
		// ReelLoadPerSpeed 0: every step loads the line like the T-007 reel.
		FLureFishFightRow Flat = Shipped;
		Flat.ReelLoadPerSpeed = 0.f;
		TestTrue(TEXT("ReelLoadPerSpeed 0: every step's load is 1"), FLureFight::ReelStepLoad(0, Flat) == 1.f && FLureFight::ReelStepLoad(2, Flat) == 1.f);

		// "Fast gains line but builds tension": the step scales the gain exactly and only the cranking part of the tension.
		for (int32 Step = 0; Step < N; ++Step)
		{
			const FLureRodFactors F = FLureFight::RodFactors(In(true, 0.f, 0.f, Step), nullptr, 1.f, Shipped);
			const float Speed = FLureFight::ReelStepSpeed(Step, Shipped);
			for (const float Pull : { 0.f, 2.f, 6.f })
			{
				TestNearlyEqual(FString::Printf(TEXT("step %d, pull %g: gain = ReelSpeed x step speed x (1 - pull / power)"), Step, Pull), FLureFight::LineGainSpeed(Pull, true, Starter, F),
					Starter.ReelSpeed * Speed * FMath::Clamp(1.f - Pull / Starter.RodPower, 0.f, 1.f), 1.0e-3f);
				TestNearlyEqual(FString::Printf(TEXT("step %d, pull %g: tension = pull x ReelStrain + RodPower x ReelLoad x step load"), Step, Pull), FLureFight::TargetTension(Pull, true, Starter, Shipped, F),
					Pull * Shipped.ReelStrain + Starter.RodPower * Shipped.ReelLoad * FLureFight::ReelStepLoad(Step, Shipped), 1.0e-4f);
				TestTrue(FString::Printf(TEXT("step %d, pull %g: letting it run ignores the reel step"), Step, Pull),
					FLureFight::TargetTension(Pull, false, Starter, Shipped, F) == FLureFight::TargetTension(Pull, false, Starter, Shipped) && FLureFight::LineGainSpeed(Pull, false, Starter, F) == 0.f);
			}
		}
		return true;
	}

	// =================================================================================================================
	// The drag cap holds whatever the rod does
	// =================================================================================================================

	/**
	 *  For every shipped rod x line x hook (with the DragLineCap rule applied), every rod input at its extremes and a fish pulling five
	 *  times the line on a sideways run: letting it run never snaps the line and the tension never passes the drag (60 s each).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQADragCap, "Project.Fishing.Fight.Rod.QA.Math.DragCapHoldsForEveryRodInput", Flags)
	bool FRodQADragCap::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		int32 Fights = 0;
		int32 Snaps = 0;
		for (const FName Rod : { FName(TEXT("Rod_Starter")), FName(TEXT("Rod_Reef")) })
		{
			for (const FName Line : { FName(TEXT("Line_Mono")), FName(TEXT("Line_Braid")) })
			{
				for (const FName Hook : { FName(TEXT("Hook_Shrimp")), FName(TEXT("Hook_Squid")) })
				{
					FLureGearStats Kit = Data.Resolve(Rod, Line, Hook);
					const float RodDrag = Kit.Drag;
					// A fish this strong counts as slack at the drag (SlackShare x its pull > the drag) and would throw the hook in seconds:
					// a hook that never lets go keeps each fight on for the whole 60 s (only the snap rule is under test here).
					Kit.HookSecurity = 1.0e4f;
					FLureGear::ApplyDragLineCap(Kit, T.DragLineCap);
					const FString KitName = FString::Printf(TEXT("%s/%s/%s"), *Rod.ToString(), *Line.ToString(), *Hook.ToString());
					TestTrue(FString::Printf(TEXT("%s: drag %.2f = min(rod drag %.2f, line %.1f x DragLineCap %.2f)"), *KitName, Kit.Drag, RodDrag, Kit.LineStrength, T.DragLineCap),
						FMath::IsNearlyEqual(Kit.Drag, FMath::Min(RodDrag, Kit.LineStrength * T.DragLineCap), 1.0e-5f) && Kit.Drag < Kit.LineStrength);
					for (const float Side : { -1.f, 1.f })
					{
						for (const float Pitch : { -1.f, 0.f, 1.f })
						{
							for (const float Yaw : { -1.f, 0.f, 1.f })
							{
								for (const int32 Step : { 0, static_cast<int32>(INDEX_NONE), 8 })
								{
									FLureFightState State;
									const FLureFightMove Hard = SideMove(TEXT("Hard"), 1.f, 0.f, 0.f, Side);
									FLureFight::Begin(State, LureFightQA::MakeFightFish(5.f * Kit.LineStrength, 0.f, 1.0e9f), OneMove(Hard), TEXT("QA_Drag"), Kit, T, 3, 2000.f);
									float Peak = 0.f;
									for (int32 Frame = 0; Frame < 60 * 60 && !State.IsOver(); ++Frame)
									{
										FLureFight::Step(State, In(false, Pitch, Yaw, Step));
										Peak = FMath::Max(Peak, State.Tension);
									}
									++Fights;
									const bool bSnapped = State.Outcome == ELureFightOutcome::Snapped;
									Snaps += bSnapped ? 1 : 0;
									if (bSnapped || Peak > Kit.Drag + 1.0e-4f)
									{
										AddError(FString::Printf(TEXT("%s, run %+.0f, pitch %+.0f, yaw %+.0f, step %d, not reeling: %s, peak tension %.3f over the drag %.3f"), *KitName, Side, Pitch, Yaw, Step,
											*LureFightQA::OutcomeName(State.Outcome), Peak, Kit.Drag));
									}
								}
							}
						}
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%d fights of 60 s letting a 5x fish run: %d snapped"), Fights, Snaps));

		// The hand-over: reeling over the line with the rod back, then letting it run before the grace is up: the tension drops to the drag at
		// once (instant tension here), the snap timer resets, and nothing snaps for the next 60 s.
		{
			const FLureFishFightRow Instant = LureFightQA::InstantTuning(T, 60);
			FLureGearStats Kit = Data.Starter();
			FLureGear::ApplyDragLineCap(Kit, T.DragLineCap);
			FLureFightState State;
			FLureFight::Begin(State, LureFightQA::MakeFightFish(Kit.LineStrength, 0.f, 1.0e9f), OneMove(SideMove(TEXT("Pull"), 1.f, 0.f, 0.f, 0.f)), TEXT("QA_Hand"), Kit, Instant, 3, 2000.f);
			const int32 Grace = FMath::RoundToInt(Instant.SnapGraceTime * 60.f);
			for (int32 Step = 0; Step < Grace; ++Step)
			{
				FLureFight::Step(State, In(true, 1.f));
			}
			TestTrue(FString::Printf(TEXT("reeling with the rod back: over the line for the whole grace (%.2f > %.1f) without snapping"), State.Tension, Kit.LineStrength),
				State.Tension > Kit.LineStrength && !State.IsOver());
			FLureFight::Step(State, In(false, 1.f));
			TestTrue(FString::Printf(TEXT("let it run on the next step: at the drag (%.2f) and the snap timer reset (%.3f)"), State.Tension, State.OverTime),
				FMath::IsNearlyEqual(State.Tension, Kit.Drag, 1.0e-4f) && State.OverTime == 0.f);
			for (int32 Step = 0; Step < 3600 && !State.IsOver(); ++Step)
			{
				FLureFight::Step(State, In(false, 1.f));
			}
			TestEqual(TEXT("... and nothing snaps in 60 s"), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::None));
		}
		return true;
	}

	// =================================================================================================================
	// Snap and slack timers when the rod is what crosses the threshold
	// =================================================================================================================

	/**
	 *  Instant tension at 30/60/120 Hz: level the line holds; pulled back it is over. Exactly SnapGraceTime of whole steps over holds, one more
	 *  snaps; one level step in between resets the timer; a pitch just under the crossing never snaps, just over it snaps on time.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASnapTimer, "Project.Fishing.Fight.Rod.QA.Timers.SnapWholeStepsWhenTheRodCrosses", Flags)
	bool FRodQASnapTimer::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureGearStats Starter = Data.Starter();
		const FLureFightMove Hold = SideMove(TEXT("Hold"), 1.f, 0.f, 0.f, 0.f);
		for (const int32 Rate : { 30, 60, 120 })
		{
			const FLureFishFightRow T = LureFightQA::InstantTuning(*Data.Tuning(), Rate);
			const int32 Grace = FMath::RoundToInt(T.SnapGraceTime * static_cast<float>(Rate));
			// Pull 6 while reeling at the level rod = 6 x 1.3 + 8 x 0.15 = 9.0 on the 10 line; fully back = 9.0 x 1.3 = 11.7.
			const float Pull = (0.9f * Starter.LineStrength - Starter.RodPower * T.ReelLoad) / T.ReelStrain;
			auto Fresh = [&]()
			{
				FLureFightState State;
				FLureFight::Begin(State, LureFightQA::MakeFightFish(Pull, 0.f, 1.0e9f), OneMove(Hold), TEXT("QA_Snap"), Starter, T, 1, 4000.f);
				return State;
			};
			{
				FLureFightState State = Fresh();
				for (int32 Step = 0; Step < 10 * Rate && !State.IsOver(); ++Step)
				{
					FLureFight::Step(State, In(true));
				}
				TestTrue(FString::Printf(TEXT("%d Hz, level: the line holds for 10 s (%.2f of %.1f)"), Rate, State.Tension, Starter.LineStrength), !State.IsOver() && State.Tension < Starter.LineStrength);
			}
			{
				FLureFightState State = Fresh();
				for (int32 Step = 0; Step < Grace; ++Step)
				{
					FLureFight::Step(State, In(true, 1.f));
				}
				TestTrue(FString::Printf(TEXT("%d Hz, rod back: over the line (%.2f) for exactly the grace (%d steps): still on"), Rate, State.Tension, Grace), State.Tension > Starter.LineStrength && !State.IsOver());
				FLureFight::Step(State, In(true, 1.f));
				TestEqual(FString::Printf(TEXT("%d Hz, rod back: one step more snaps"), Rate), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Snapped));
			}
			{
				FLureFightState State = Fresh();
				for (int32 Step = 0; Step < Grace; ++Step)
				{
					FLureFight::Step(State, In(true, 1.f));
				}
				FLureFight::Step(State, In(true, 0.f)); // one level step: under the line, the timer resets
				TestEqual(FString::Printf(TEXT("%d Hz: one level step resets the snap timer"), Rate), State.OverTime, 0.f);
				for (int32 Step = 0; Step < Grace; ++Step)
				{
					FLureFight::Step(State, In(true, 1.f));
				}
				TestFalse(FString::Printf(TEXT("%d Hz: back again for the whole grace: still on"), Rate), State.IsOver());
				FLureFight::Step(State, In(true, 1.f));
				TestEqual(FString::Printf(TEXT("%d Hz: ... and one more snaps"), Rate), LureFightQA::OutcomeName(State.Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Snapped));
			}
			// The crossing pitch: level tension x (1 + p x PitchBackPressure) = the line.
			const float Crossing = (Starter.LineStrength / (0.9f * Starter.LineStrength) - 1.f) / T.PitchBackPressure;
			{
				FLureFightState State = Fresh();
				for (int32 Step = 0; Step < 10 * Rate && !State.IsOver(); ++Step)
				{
					FLureFight::Step(State, In(true, Crossing - 1.0e-3f));
				}
				TestFalse(FString::Printf(TEXT("%d Hz: pitch just under the crossing (%.4f): never snaps (%.4f of %.1f)"), Rate, Crossing, State.Tension, Starter.LineStrength), State.IsOver());
			}
			{
				FLureFightState State = Fresh();
				int32 Steps = 0;
				while (!State.IsOver() && Steps < 10 * Rate)
				{
					FLureFight::Step(State, In(true, Crossing + 1.0e-3f));
					++Steps;
				}
				TestEqual(FString::Printf(TEXT("%d Hz: pitch just over the crossing snaps after grace + 1 steps"), Rate), Steps, Grace + 1);
			}
			// Steering against a sideways run lowers the same fish's tension under the line even with the rod back (it pulls 20 % less).
			{
				FLureFightState State;
				FLureFight::Begin(State, LureFightQA::MakeFightFish(Pull, 0.f, 1.0e9f), OneMove(SideMove(TEXT("Run"), 1.f, 0.f, 0.f, -1.f)), TEXT("QA_Turn"), Starter, T, 1, 4000.f);
				for (int32 Step = 0; Step < 10 * Rate && !State.IsOver(); ++Step)
				{
					FLureFight::Step(State, In(true, Crossing + 1.0e-3f, 1.f));
				}
				TestFalse(FString::Printf(TEXT("%d Hz: the same pitch against a left run holds (%.2f)"), Rate, State.Tension), State.IsOver());
			}
		}
		return true;
	}

	/**
	 *  Instant tension: a soft fish let run keeps the line taut when level and slack when the rod is fully dipped (the hook is thrown after
	 *  SlackGraceTime x HookSecurity of whole steps, plus one). The slack rule doesn't care about the reel button: reeling at the slowest step with
	 *  the rod dipped can be slack too (the fish throws the hook while the player reels). The slack line itself: at it is not slack.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASlackTimer, "Project.Fishing.Fight.Rod.QA.Timers.SlackWholeStepsWhenTheRodDips", Flags)
	bool FRodQASlackTimer::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow T = LureFightQA::InstantTuning(*Data.Tuning(), 60);
		const float BasePull = 4.f;
		const float Slack = T.SlackShare * BasePull;
		for (const FName Hook : { FName(TEXT("Hook_Shrimp")), FName(TEXT("Hook_Squid")) })
		{
			const FLureGearStats Kit = Data.Resolve(TEXT("Rod_Starter"), TEXT("Line_Mono"), Hook);
			const int32 Grace = FMath::RoundToInt(T.SlackGraceTime * Kit.HookSecurity * 60.f);
			auto StepsToThrow = [&](const FLureFightMove& Move, const FLureFightInput& Input)
			{
				FLureFightState State;
				FLureFight::Begin(State, LureFightQA::MakeFightFish(BasePull, 0.f, 1.0e9f), OneMove(Move), TEXT("QA_Slack"), Kit, T, 2, 3000.f);
				int32 Steps = 0;
				while (!State.IsOver() && Steps < 60 * 30)
				{
					FLureFight::Step(State, Input);
					++Steps;
				}
				return State.Outcome == ELureFightOutcome::ThrewHook ? Steps : -1;
			};
			// Letting a Pull 0.5 fish run: level = 2.0 (taut, slack below 1.4); fully dipped = 1.0 (slack).
			const FLureFightMove Soft = SideMove(TEXT("Soft"), 0.5f, 0.f, 0.f, 0.f);
			TestEqual(FString::Printf(TEXT("%s, letting it run level: never throws in 30 s"), *Hook.ToString()), StepsToThrow(Soft, In(false)), -1);
			TestEqual(FString::Printf(TEXT("%s, letting it run fully dipped: throws after exactly %d + 1 steps"), *Hook.ToString(), Grace), StepsToThrow(Soft, In(false, -1.f)), Grace + 1);
			// Reeling a Pull 0.3 fish: level = 1.2 x 1.3 + 1.2 = 2.76 (taut); fully dipped at the slowest step = (1.56 + 0.3) x 0.5 = 0.93 (slack).
			const FLureFightMove Resting = SideMove(TEXT("Resting"), 0.3f, 0.f, 0.f, 0.f);
			TestEqual(FString::Printf(TEXT("%s, reeling level: never throws"), *Hook.ToString()), StepsToThrow(Resting, In(true)), -1);
			TestEqual(FString::Printf(TEXT("%s, REELING with the rod fully dipped at the slowest step: slack, throws after %d + 1 steps"), *Hook.ToString(), Grace),
				StepsToThrow(Resting, In(true, -1.f, 0.f, 0)), Grace + 1);
		}
		// The slack line itself is not slack ("below"): a dip that puts the tension exactly at SlackShare x BasePull never throws.
		{
			const FLureGearStats Kit = Data.Starter();
			const float Pull = 0.5f; // x BasePull = 2.0 letting it run; dipped by d: 2.0 x (1 - d x PitchDipPressure) = Slack
			const float Dip = (1.f - Slack / (Pull * BasePull)) / T.PitchDipPressure;
			FLureFightState State;
			FLureFight::Begin(State, LureFightQA::MakeFightFish(BasePull, 0.f, 1.0e9f), OneMove(SideMove(TEXT("Soft"), Pull, 0.f, 0.f, 0.f)), TEXT("QA_Edge"), Kit, T, 2, 3000.f);
			for (int32 Step = 0; Step < 60 * 10 && !State.IsOver(); ++Step)
			{
				FLureFight::Step(State, In(false, -(Dip - 1.0e-3f)));
			}
			TestFalse(FString::Printf(TEXT("a dip of %.4f keeps the tension %.4f just above the slack line %.4f: never throws"), Dip - 1.0e-3f, State.Tension, Slack), State.IsOver());
		}
		return true;
	}

	// =================================================================================================================
	// The wire: one byte per axis, the server clamps everything
	// =================================================================================================================

	/** Every byte unpacks into [-1, 1], never decreasing; 127 is exactly 0, 0 is -1, 254 and 255 are +1; packing a byte's value gives the byte back. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAPackEveryByte, "Project.Fishing.Fight.Rod.QA.Net.PackEveryByte", Flags)
	bool FRodQAPackEveryByte::RunTest(const FString& Parameters)
	{
		float Previous = -2.f;
		int32 BadRange = 0, BadOrder = 0, BadRoundTrip = 0;
		for (int32 Byte = 0; Byte <= 255; ++Byte)
		{
			const float Value = FLureRodControl::UnpackAxis(static_cast<uint8>(Byte));
			BadRange += (FMath::IsFinite(Value) && Value >= -1.f && Value <= 1.f) ? 0 : 1;
			BadOrder += Value >= Previous ? 0 : 1;
			Previous = Value;
			if (Byte <= 254)
			{
				BadRoundTrip += FLureRodControl::PackAxis(Value) == static_cast<uint8>(Byte) ? 0 : 1;
			}
		}
		TestEqual(TEXT("bytes outside [-1, 1] or non-finite"), BadRange, 0);
		TestEqual(TEXT("bytes whose value is below the previous byte's"), BadOrder, 0);
		TestEqual(TEXT("bytes 0-254 that don't pack back to themselves"), BadRoundTrip, 0);
		TestTrue(TEXT("127 = exactly 0 (the neutral rod stays the T-007 fight on the wire)"), FLureRodControl::UnpackAxis(127) == 0.f);
		TestTrue(TEXT("0 = exactly -1"), FLureRodControl::UnpackAxis(0) == -1.f);
		TestTrue(TEXT("254 = exactly +1"), FLureRodControl::UnpackAxis(254) == 1.f);
		TestTrue(TEXT("255 = exactly +1"), FLureRodControl::UnpackAxis(255) == 1.f);
		TestEqual(TEXT("pack 0 = 127"), static_cast<int32>(FLureRodControl::PackAxis(0.f)), 127);
		for (const float Value : { 0.f, -0.f, 1.f, -1.f, 1.5f, -1.5f, 1.0e30f, -1.0e30f, QInf(), -QInf(), QNaN(), 1.0e-9f })
		{
			const uint8 Packed = FLureRodControl::PackAxis(Value);
			TestNearlyEqual(FString::Printf(TEXT("pack %g -> %d -> %g (the clamped value)"), Value, Packed, FLureRodControl::UnpackAxis(Packed)), FLureRodControl::UnpackAxis(Packed), Unit(Value), 0.5f / 127.f + 1.0e-6f);
		}
		float Worst = 0.f;
		for (int32 Index = 0; Index <= 20000; ++Index)
		{
			const float Value = -1.f + 2.f * static_cast<float>(Index) / 20000.f;
			Worst = FMath::Max(Worst, FMath::Abs(FLureRodControl::UnpackAxis(FLureRodControl::PackAxis(Value)) - Value));
		}
		TestTrue(FString::Printf(TEXT("round trip error at most half a byte step (%.6f <= %.6f)"), Worst, 0.5f / 127.f), Worst <= 0.5f / 127.f + 1.0e-6f);
		// The neutral rod through the wire is bit for bit the neutral rod.
		LureFightQA::FFightTables Data;
		if (Data.Load(*this))
		{
			const FLureFishFightRow& T = *Data.Tuning();
			const FLureFightMove Run = SideMove(TEXT("Run"), 1.f, 1.f, 1.f, -1.f);
			const FLureRodFactors Wire = FLureFight::RodFactors(In(true, FLureRodControl::UnpackAxis(FLureRodControl::PackAxis(0.f)), FLureRodControl::UnpackAxis(FLureRodControl::PackAxis(0.f))),
				&Run, 1.f, T);
			TestTrue(TEXT("the neutral rod after the wire: every factor exactly 1"), Wire.Pressure == 1.f && Wire.Side == 0.f && Wire.Power == 1.f && Wire.Pull == 1.f && Wire.MoveClock == 1.f && Wire.Drain == 1.f);
		}
		return true;
	}

	/** Hostile inputs through Step: the state records the clamped rod, every number stays finite, the fight goes on. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQASanitize, "Project.Fishing.Fight.Rod.QA.Net.HostileInputIsClampedInTheStep", Flags)
	bool FRodQASanitize::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		const FLureFishFightRow& T = *Data.Tuning();
		const float Axes[] = { QNaN(), QInf(), -QInf(), 1.0e30f, -1.0e30f, TNumericLimits<float>::Max(), -TNumericLimits<float>::Max(), 1.0000001f, -1.0000001f,
			TNumericLimits<float>::Min() * 0.5f, -0.f, 0.37f };
		const int32 Steps[] = { TNumericLimits<int32>::Min(), -1, 0, 2, 3, 255, TNumericLimits<int32>::Max() };
		int32 Checked = 0;
		for (const float Pitch : Axes)
		{
			for (const float Yaw : Axes)
			{
				for (const int32 Step : Steps)
				{
					FLureFightState State;
					FLureFight::Begin(State, LureFightQA::MakeFightFish(3.f, 80.f, 60.f), OneMove(SideMove(TEXT("Run"), 1.5f, 1.f, 0.8f, -0.6f)), TEXT("QA_Hostile"), Data.Starter(), T, 9, 1500.f);
					for (int32 Frame = 0; Frame < 5; ++Frame)
					{
						FLureFight::Step(State, In(true, Pitch, Yaw, Step));
					}
					++Checked;
					const FLureFightInput Clean = FLureFight::SanitizeInput(In(true, Pitch, Yaw, Step), T);
					const bool bOk = State.RodPitch == Unit(Pitch) && State.RodYaw == Unit(Yaw) && State.ReelStep == FLureFight::ClampReelStep(Step, T) && Clean.RodPitch == State.RodPitch
						&& Clean.RodYaw == State.RodYaw && Clean.ReelStep == State.ReelStep && Clean.bReeling && LureFightQA::AllFinite(State) && FMath::IsFinite(State.Side);
					if (!bOk)
					{
						AddError(FString::Printf(TEXT("pitch %g, yaw %g, step %d: recorded (%g, %g, %d), sanitized (%g, %g, %d), finite %d"), Pitch, Yaw, Step, State.RodPitch, State.RodYaw,
							State.ReelStep, Clean.RodPitch, Clean.RodYaw, Clean.ReelStep, LureFightQA::AllFinite(State)));
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%d hostile input combinations"), Checked));
		return true;
	}

	// =================================================================================================================
	// HUD words at the zone edges
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQAHudWords, "Project.Fishing.Fight.Rod.QA.Hud.WordsAtTheZoneEdges", Flags)
	bool FRodQAHudWords::RunTest(const FString& Parameters)
	{
		// Spec: "Rod: back-right" etc., zones at one third of the range ("level" inside, the edge itself included).
		const float Zone = 1.f / 3.f;
		const float Above = std::nextafter(Zone, 1.f);
		const float Below = std::nextafter(-Zone, -1.f);
		struct FCase { float Pitch; float Yaw; const TCHAR* Words; };
		const FCase Cases[] = {
			{ 0.f, 0.f, TEXT("level") }, { Zone, 0.f, TEXT("level") }, { Above, 0.f, TEXT("back") }, { -Zone, 0.f, TEXT("level") }, { Below, 0.f, TEXT("dipped") },
			{ 0.f, Zone, TEXT("level") }, { 0.f, Above, TEXT("level-right") }, { 0.f, -Zone, TEXT("level") }, { 0.f, Below, TEXT("level-left") },
			{ 1.f, 1.f, TEXT("back-right") }, { 1.f, -1.f, TEXT("back-left") }, { -1.f, 1.f, TEXT("dipped-right") }, { -1.f, -1.f, TEXT("dipped-left") },
			{ 5.f, -5.f, TEXT("back-left") }, { QNaN(), QNaN(), TEXT("level") },
		};
		for (const FCase& Case : Cases)
		{
			TestEqual(FString::Printf(TEXT("pitch %g, yaw %g"), Case.Pitch, Case.Yaw), FLureRodControl::DescribeRod(Case.Pitch, Case.Yaw), FString(Case.Words));
		}
		for (int32 Num = 1; Num <= 9; ++Num)
		{
			for (int32 Step = -2; Step <= Num + 1; ++Step)
			{
				const FString Expected = FString::Printf(TEXT("Reel %d/%d (wheel or LB/RB)"), FMath::Clamp(Step, 0, Num - 1) + 1, Num);
				TestEqual(FString::Printf(TEXT("reel step %d of %d"), Step, Num), FLureRodControl::ReelText(Step, Num), Expected);
			}
		}
		TestEqual(TEXT("no steps in the data: Reel 1/1"), FLureRodControl::ReelText(0, 0), FString(TEXT("Reel 1/1 (wheel or LB/RB)")));
		TestEqual(TEXT("run left"), FLureRodControl::RunHint(ELureFightRunSide::Left), FString(TEXT("Fish runs LEFT: pull right")));
		TestEqual(TEXT("run right"), FLureRodControl::RunHint(ELureFightRunSide::Right), FString(TEXT("Fish runs RIGHT: pull left")));
		TestTrue(TEXT("no run: no hint"), FLureRodControl::RunHint(ELureFightRunSide::None).IsEmpty());
		TestTrue(TEXT("a byte that is no side (a corrupt value): no hint"), FLureRodControl::RunHint(static_cast<ELureFightRunSide>(7)).IsEmpty());
		for (const int32 Dir : { -9, -1, 0, 1, 9 })
		{
			const int32 Expected = FMath::Clamp(Dir, -1, 1);
			TestEqual(FString::Printf(TEXT("direction %d -> side -> direction %d"), Dir, Expected), FLureRodControl::DirectionFromRunSide(FLureRodControl::RunSideFromDirection(Dir)), Expected);
		}
		// The hint is the side score's sign convention: steering the way the hint says is "against" (+1).
		TestEqual(TEXT("'runs LEFT: pull right' = yaw +1 against run -1 = side score +1"), FLureFight::SideScore(1.f, FLureRodControl::DirectionFromRunSide(ELureFightRunSide::Left)), 1.f);
		TestEqual(TEXT("'runs RIGHT: pull left' = yaw -1 against run +1 = side score +1"), FLureFight::SideScore(-1.f, FLureRodControl::DirectionFromRunSide(ELureFightRunSide::Right)), 1.f);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

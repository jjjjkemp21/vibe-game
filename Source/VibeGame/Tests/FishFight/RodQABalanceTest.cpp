// Lure T-028 QA (qa-engineer, 2026-09-23): the balance claims, re-derived with QA's own scripted players.
// Project.Fishing.Fight.Rod.QA.Balance.*
// The players are written from GAME_DESIGN.md "Fighting" and the HUD, not from the implementer's scripts (RodFightTest.cpp /
// tools/balance/reel_fight_model.py): they see the HUD (the tension bar and "Fish runs LEFT: pull right") with a 0.3 s reaction.
//   Hold         reels the whole fight, rod level and centered, default reel (the T-007 player)
//   Steer        Hold + the rod against every run the HUD shows (nothing else)
//   Advice       Jimmy's words: on a run, "angle the rod up and to the other side" (pitch +0.5, yaw against); "ease off when tension
//                spikes, and let the fish run" (release the reel at 90 % of the bar, reel again under 60 %); default reel speed
//   AdviceWheel  Advice + the wheel: slowest step at 75 % of the bar, fastest while the bar is under half and nothing runs
//   AdviceWrong  Advice, but the rod WITH the run (the same side)
//   DippedFast   a posture, no watching: rod fully dipped, fastest reel, reel held, rod against the runs
//   DippedSlow   a posture: rod fully dipped, slowest reel, reel held, nothing else
// Fish come from the one roll pipeline (FFishRoll::Roll) on the shipped data; the starter kit; the fight starts 10 m out.

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace LureRodQA
{
	namespace RodBalance
	{
		enum class EPlayer : uint8 { Hold, Steer, Advice, AdviceWheel, AdviceWrong, DippedFast, DippedSlow };

		inline const TCHAR* PlayerName(EPlayer Player)
		{
			switch (Player)
			{
			case EPlayer::Hold: return TEXT("hold");
			case EPlayer::Steer: return TEXT("steer");
			case EPlayer::Advice: return TEXT("advice");
			case EPlayer::AdviceWheel: return TEXT("advice+wheel");
			case EPlayer::AdviceWrong: return TEXT("advice, rod with the run");
			case EPlayer::DippedFast: return TEXT("posture: dipped+fastest+steer");
			case EPlayer::DippedSlow: return TEXT("posture: dipped+slowest");
			default: return TEXT("?");
			}
		}

		struct FResult
		{
			ELureFightOutcome Outcome = ELureFightOutcome::None;
			float Elapsed = 0.f;
			bool Landed() const { return Outcome == ELureFightOutcome::Landed; }
		};

		inline FResult Play(FLureFightState State, EPlayer Player)
		{
			const FLureFishFightRow& T = State.Tuning;
			const float Dt = 1.f / static_cast<float>(FMath::Clamp(T.SimRate, 10, 240));
			constexpr float React = 0.3f;
			const int32 Fastest = FLureFight::NumReelSteps(T) - 1;
			bool bReel = true;
			float Pitch = 0.f;
			float Yaw = 0.f;
			int32 Step = INDEX_NONE;
			int32 SeenKey = TNumericLimits<int32>::Min();
			float SinceChange = 0.f;
			int32 SeenRun = 0;       // the HUD's run hint, as the player has taken it in
			float SinceBar = 1000.f;
			float Bar = 0.f;         // the tension bar, as the player has taken it in
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
				switch (Player)
				{
				case EPlayer::Steer:
					Yaw = -static_cast<float>(SeenRun);
					break;
				case EPlayer::Advice:
				case EPlayer::AdviceWheel:
				case EPlayer::AdviceWrong:
					bEasing = bEasing ? Bar >= 0.6f : Bar >= 0.9f;
					bReel = !bEasing;
					Yaw = (Player == EPlayer::AdviceWrong ? 1.f : -1.f) * static_cast<float>(SeenRun);
					Pitch = SeenRun != 0 ? 0.5f : 0.f;
					if (Player == EPlayer::AdviceWheel)
					{
						Step = Bar >= 0.75f ? 0 : ((Bar < 0.5f && SeenRun == 0) ? Fastest : INDEX_NONE);
					}
					break;
				case EPlayer::DippedFast:
					Pitch = -1.f;
					Step = Fastest;
					Yaw = -static_cast<float>(SeenRun);
					break;
				case EPlayer::DippedSlow:
					Pitch = -1.f;
					Step = 0;
					break;
				case EPlayer::Hold:
				default:
					break;
				}
				FLureFight::Step(State, In(bReel, Pitch, Yaw, Step));
			}
			return { State.Outcome, State.Elapsed };
		}

		inline FLureFightState Fight(const LureFightQA::FFightTables& Data, const FFishInstance& Fish, FName PatternId)
		{
			const FLureFishFightRow& T = *Data.Tuning();
			FLureGearStats Kit = Data.Starter();
			FLureGear::ApplyDragLineCap(Kit, T.DragLineCap);
			FLureFightState State;
			const FLureFightPatternRow* Pattern = Data.Pattern(PatternId);
			FLureFight::Begin(State, FLureFight::MakeFish(Fish, T, 1, GetDefault<UFishSettings>()->LevelScaling), Pattern ? *Pattern : FLureFightPatternRow::GetFallbackPattern(),
				PatternId, Kit, T, FLureFight::FightSeed(Fish.Seed), 1000.f);
			return State;
		}

		/** Rolled fish of Species through the one roll pipeline (natural rarity, weight and modifiers). */
		inline TArray<FFishInstance> Roll(const FishQA::FTables& Fish, FName Species, int32 Count, int32 FirstSeed, float Hours)
		{
			TArray<FFishInstance> Out;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FFishRollContext Context;
				Context.SpeciesId = Species;
				Context.Seed = FirstSeed + Index;
				Context.RegionTag = LureFightQA::Tag(TEXT("Region.Tropical.PalmKey"));
				Context.TimeOfDayHours = Hours;
				FFishInstance Instance;
				if (FFishRoll::Roll(Fish.Get(), Context, Instance))
				{
					Out.Add(Instance);
				}
			}
			return Out;
		}

		struct FGroup
		{
			int32 Fish = 0;
			TMap<EPlayer, int32> Lost;
			TMap<EPlayer, int32> ThrewHook;
			TMap<EPlayer, TArray<float>> Times;         // landed fights
			TMap<EPlayer, TArray<float>> RatioToHold;   // on the fish both this player and Hold land
			TMap<EPlayer, TArray<float>> RatioToAdvice; // on the fish both this player and Advice land
		};

		inline FGroup Run(const LureFightQA::FFightTables& Data, const TArray<FFishInstance>& Fishes, FName PatternId, const TArray<EPlayer>& Players)
		{
			FGroup Group;
			Group.Fish = Fishes.Num();
			for (const FFishInstance& Fish : Fishes)
			{
				TMap<EPlayer, FResult> Results;
				for (const EPlayer Player : Players)
				{
					const FResult Result = Play(Fight(Data, Fish, PatternId), Player);
					Results.Add(Player, Result);
					Group.Lost.FindOrAdd(Player) += Result.Landed() ? 0 : 1;
					Group.ThrewHook.FindOrAdd(Player) += Result.Outcome == ELureFightOutcome::ThrewHook ? 1 : 0;
					if (Result.Landed())
					{
						Group.Times.FindOrAdd(Player).Add(Result.Elapsed);
					}
				}
				for (const EPlayer Player : Players)
				{
					const FResult& Mine = Results[Player];
					if (const FResult* Hold = Results.Find(EPlayer::Hold); Hold && Hold->Landed() && Mine.Landed())
					{
						Group.RatioToHold.FindOrAdd(Player).Add(Mine.Elapsed / Hold->Elapsed);
					}
					if (const FResult* Advice = Results.Find(EPlayer::Advice); Advice && Advice->Landed() && Mine.Landed())
					{
						Group.RatioToAdvice.FindOrAdd(Player).Add(Mine.Elapsed / Advice->Elapsed);
					}
				}
			}
			return Group;
		}

		inline FString Describe(const FGroup& Group, const TArray<EPlayer>& Players)
		{
			TArray<FString> Parts;
			for (const EPlayer Player : Players)
			{
				Parts.Add(FString::Printf(TEXT("%s: lost %d (thrown hook %d), median %.1f s, vs hold x%.2f"), PlayerName(Player), Group.Lost.FindRef(Player), Group.ThrewHook.FindRef(Player),
					Median(Group.Times.FindRef(Player)), Median(Group.RatioToHold.FindRef(Player))));
			}
			return FString::Printf(TEXT("%d fish; "), Group.Fish) + FString::Join(Parts, TEXT("; "));
		}
	}

	/**
	 *  GAME_DESIGN "Fighting", played as written, on 200 rolled bonefish: holding reel still loses a real share (the T-007 tune); the design's
	 *  advice (rod up and against the run, ease off when the bar spikes) is SAFER (at most 2 % lost) and no slower; with the wheel it is also
	 *  clearly FASTER than holding on the fish holding lands; steering alone already loses fewer fish than holding.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQABalanceAdvice, "Project.Fishing.Fight.Rod.QA.Balance.DesignAdviceIsSaferAndFaster", Flags)
	bool FRodQABalanceAdvice::RunTest(const FString& Parameters)
	{
		using RodBalance::EPlayer;
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const TArray<FFishInstance> Bonefish = RodBalance::Roll(Fish, TEXT("Bonefish"), 200, 31000, 12.f);
		const TArray<EPlayer> Players = { EPlayer::Hold, EPlayer::Steer, EPlayer::Advice, EPlayer::AdviceWheel, EPlayer::DippedFast, EPlayer::DippedSlow };
		const RodBalance::FGroup G = RodBalance::Run(Data, Bonefish, TEXT("Run"), Players);
		AddInfo(TEXT("bonefish, starter kit: ") + RodBalance::Describe(G, Players));
		const int32 N = G.Fish;
		TestTrue(TEXT("fixture: the roll pipeline gave the bonefish"), N >= 190);
		TestTrue(FString::Printf(TEXT("holding reel still loses a real share (%d of %d; 25-60 %%)"), G.Lost.FindRef(EPlayer::Hold), N),
			G.Lost.FindRef(EPlayer::Hold) * 4 >= N && G.Lost.FindRef(EPlayer::Hold) * 5 <= N * 3);
		TestTrue(FString::Printf(TEXT("SAFER: the design's advice loses at most 2 %% (%d)"), G.Lost.FindRef(EPlayer::Advice)), G.Lost.FindRef(EPlayer::Advice) * 50 <= N);
		TestTrue(FString::Printf(TEXT("SAFER: advice + wheel loses at most 2 %% (%d)"), G.Lost.FindRef(EPlayer::AdviceWheel)), G.Lost.FindRef(EPlayer::AdviceWheel) * 50 <= N);
		TestTrue(FString::Printf(TEXT("steering alone loses fewer than holding (%d vs %d)"), G.Lost.FindRef(EPlayer::Steer), G.Lost.FindRef(EPlayer::Hold)),
			G.Lost.FindRef(EPlayer::Steer) < G.Lost.FindRef(EPlayer::Hold));
		const float AdviceRatio = Median(G.RatioToHold.FindRef(EPlayer::Advice));
		const float WheelRatio = Median(G.RatioToHold.FindRef(EPlayer::AdviceWheel));
		TestTrue(FString::Printf(TEXT("the advice is no slower than holding on the fish holding lands (median x%.2f <= 1.0, %d fish)"), AdviceRatio, G.RatioToHold.FindRef(EPlayer::Advice).Num()),
			AdviceRatio <= 1.f && G.RatioToHold.FindRef(EPlayer::Advice).Num() >= N / 3);
		TestTrue(FString::Printf(TEXT("FASTER: advice + wheel beats holding on the fish holding lands (median x%.2f <= 0.85)"), WheelRatio), WheelRatio <= 0.85f);
		return true;
	}

	/** "Matching the fish's direction loses ground": the same player with the rod on the run's side takes clearly longer on the same fish. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQABalanceWrongSide, "Project.Fishing.Fight.Rod.QA.Balance.RodWithTheRunLosesGround", Flags)
	bool FRodQABalanceWrongSide::RunTest(const FString& Parameters)
	{
		using RodBalance::EPlayer;
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const TArray<FFishInstance> Bonefish = RodBalance::Roll(Fish, TEXT("Bonefish"), 150, 32000, 12.f);
		const TArray<EPlayer> Players = { EPlayer::Advice, EPlayer::AdviceWrong };
		const RodBalance::FGroup G = RodBalance::Run(Data, Bonefish, TEXT("Run"), Players);
		AddInfo(TEXT("bonefish: ") + RodBalance::Describe(G, Players));
		const float Ratio = Median(G.RatioToAdvice.FindRef(EPlayer::AdviceWrong));
		TestTrue(FString::Printf(TEXT("the rod with the run is slower on the same fish (median x%.2f >= 1.05, %d fish)"), Ratio, G.RatioToAdvice.FindRef(EPlayer::AdviceWrong).Num()),
			Ratio >= 1.05f && G.RatioToAdvice.FindRef(EPlayer::AdviceWrong).Num() >= Bonefish.Num() / 2);
		return true;
	}

	/** "The snapper is harder than the bonefish": for every player it loses at least as many and takes longer to land. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQABalanceSnapper, "Project.Fishing.Fight.Rod.QA.Balance.SnapperHarderThanBonefishForEveryPlayer", Flags)
	bool FRodQABalanceSnapper::RunTest(const FString& Parameters)
	{
		using RodBalance::EPlayer;
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const TArray<FFishInstance> Bonefish = RodBalance::Roll(Fish, TEXT("Bonefish"), 100, 33000, 12.f);
		const TArray<FFishInstance> Snapper = RodBalance::Roll(Fish, TEXT("CoralSnapper"), 100, 34000, 20.f);
		TestTrue(TEXT("fixture: both species rolled"), Bonefish.Num() >= 95 && Snapper.Num() >= 95);
		const TArray<EPlayer> Players = { EPlayer::Hold, EPlayer::Advice, EPlayer::AdviceWheel };
		const RodBalance::FGroup Bone = RodBalance::Run(Data, Bonefish, TEXT("Run"), Players);
		const RodBalance::FGroup Snap = RodBalance::Run(Data, Snapper, TEXT("Dive"), Players);
		AddInfo(TEXT("bonefish: ") + RodBalance::Describe(Bone, Players));
		AddInfo(TEXT("snapper: ") + RodBalance::Describe(Snap, Players));
		for (const EPlayer Player : Players)
		{
			const float BoneLoss = static_cast<float>(Bone.Lost.FindRef(Player)) / FMath::Max(1, Bone.Fish);
			const float SnapLoss = static_cast<float>(Snap.Lost.FindRef(Player)) / FMath::Max(1, Snap.Fish);
			const float BoneTime = Median(Bone.Times.FindRef(Player));
			const float SnapTime = Median(Snap.Times.FindRef(Player));
			TestTrue(FString::Printf(TEXT("%s: the snapper loses at least as many (%.0f %% vs %.0f %%)"), RodBalance::PlayerName(Player), 100.f * SnapLoss, 100.f * BoneLoss), SnapLoss >= BoneLoss);
			TestTrue(FString::Printf(TEXT("%s: the snapper takes longer to land (median %.1f s vs %.1f s)"), RodBalance::PlayerName(Player), SnapTime, BoneTime),
				Snap.Times.FindRef(Player).Num() > 0 && SnapTime > BoneTime);
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

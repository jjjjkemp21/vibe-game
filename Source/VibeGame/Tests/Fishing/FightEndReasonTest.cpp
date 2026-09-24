// Lure T-032c (unreal-engineer, 2026-09-24): every way a reel fight ends says why, on screen and in LogLureFishing.
// Playtest A2 "New 3": sprinting away mid-fight cut the line (result Lost, reason Sprinting) with no log line, and the
// 2.5 s HUD message was gone before the player stopped running. Now the line rules' path (AuthorityReelIn) logs
// "<owner>: the line came in (<reason>) after X s (N cm out) - <species> lost." and a lost fish's HUD message stays
// FightEndMessageSeconds (ULureFishingSettings, default 6 s). docs/specs/reel-fight-rules.md.
// Project.Fishing.Fight.EndReason.*

#include "../FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers and tests share this namespace (no file-scope using-directive: unity builds merge test files).
namespace LureFightEndReason
{
	/** The fixture tables, a rolled Bonefish, and a character on the dock with a fish fighting on the line. */
	struct FScene
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		FFishInstance Bonefish;
		LureFightQA::FWorld World;
		ALurePlayerCharacter* Character = nullptr;
		ULureFishingComponent* Fishing = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Data.Load(Test) || !FishQA::LoadReal(Test, Fish)
				|| !LureFightQA::RollFish(Test, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 97, Bonefish) || !World.Create(Test))
			{
				return false;
			}
			Character = World.Spawn(LureFightQA::StandAt());
			Fishing = LureFightQA::SetUpFishing(Character, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
			if (!Test.TestNotNull(TEXT("character with fishing"), Fishing))
			{
				return false;
			}
			Fishing->bRodEquipped = true;
			World.Tick(10);
			return true;
		}

		bool StartFight(FAutomationTestBase& Test)
		{
			if (!LureFightQA::CastAndWait(Test, World, Fishing) || !Test.TestTrue(TEXT("hooked"), Fishing->AuthorityHookFish(Bonefish)))
			{
				return false;
			}
			Fishing->AuthoritySetReeling(false);
			World.Tick(20);
			return Test.TestTrue(TEXT("the fight is on"), Fishing->GetFishingState() == ELureFishingState::Hooked && Fishing->GetFightNet().bActive);
		}

		FString Hud() const { return Fishing->GetStatusText().Replace(TEXT("\n"), TEXT(" | ")); }
	};

	FString BlockName(ELureCastBlock Block)
	{
		return StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Block));
	}

	/** The playtest repro: sprinting away from the water mid-fight. The fish is lost for Sprinting, the log says so, and the
	 *  HUD message is still there after the normal message time (the player is still running). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightEndSprintAway, "Project.Fishing.Fight.EndReason.SprintingAwayLogsAndShowsWhy", LureFightQA::Flags)
	bool FLureFightEndSprintAway::RunTest(const FString& Parameters)
	{
		FScene Scene;
		if (!Scene.Create(*this) || !Scene.StartFight(*this))
		{
			return false;
		}
		LureFightQA::FLogCapture Log(TEXT("LogLureFishing"));
		ULureFishingComponent* Fishing = Scene.Fishing;
		Scene.Character->SetSprintRequested(true);
		bool bSprinted = false;
		for (int32 Frame = 0; Frame < 120 && Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			Scene.Character->AddMovementInput(-FVector::ForwardVector, 1.f, true); // toward the island (away from the water)
			Scene.World.Tick(1);
			bSprinted |= Scene.Character->IsSprinting();
		}
		Scene.Character->SetSprintRequested(false);
		TestTrue(TEXT("the character sprinted"), bSprinted);
		const FLureFishingNetState& Net = Fishing->GetNetState();
		TestEqual(TEXT("state Idle"), LureFightQA::StateName(Fishing->GetFishingState()), LureFightQA::StateName(ELureFishingState::Idle));
		TestEqual(TEXT("result Lost"), LureFightQA::ResultName(Net.LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
		TestEqual(TEXT("reason Sprinting"), BlockName(Net.ResultReason), BlockName(ELureCastBlock::Sprinting));
		TestEqual(TEXT("LogLureFishing gives the end reason"), Log.Count(TEXT("the line came in (sprinting)")), 1);
		TestEqual(TEXT("LogLureFishing names the lost fish"), Log.Count(TEXT("Bonefish lost.")), 1);
		TestTrue(FString::Printf(TEXT("HUD right away: 'The fish got away (sprinting).' (%s)"), *Scene.Hud()), Scene.Hud().Contains(TEXT("The fish got away (sprinting).")));

		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		TestTrue(TEXT("a lost fish's message outlasts the normal message"), Settings->FightEndMessageSeconds > Settings->HudMessageSeconds + 1.f);
		const int32 PastNormal = FMath::CeilToInt((Settings->HudMessageSeconds + 1.f) / LureFightQA::WorldDt);
		Scene.World.Tick(PastNormal);
		TestTrue(FString::Printf(TEXT("HUD after %.1f s: still says why (%s)"), Settings->HudMessageSeconds + 1.f, *Scene.Hud()), Scene.Hud().Contains(TEXT("The fish got away (sprinting).")));
		const int32 PastFightEnd = FMath::CeilToInt(Settings->FightEndMessageSeconds / LureFightQA::WorldDt);
		Scene.World.Tick(PastFightEnd);
		TestFalse(FString::Printf(TEXT("HUD after %.1f s: the message is gone (%s)"), Settings->FightEndMessageSeconds + Settings->HudMessageSeconds + 1.f, *Scene.Hud()),
			Scene.Hud().Contains(TEXT("got away")));
		return true;
	}

	/** Every line-rule end of a fight (AuthorityReelIn with each reason) logs one reason line and shows a HUD message. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightEndEveryLineRule, "Project.Fishing.Fight.EndReason.EveryLineRuleLogsItsReason", LureFightQA::Flags)
	bool FLureFightEndEveryLineRule::RunTest(const FString& Parameters)
	{
		FScene Scene;
		if (!Scene.Create(*this))
		{
			return false;
		}
		struct FCase
		{
			ELureCastBlock Reason;
			const TCHAR* LogText;
			const TCHAR* HudText;
		};
		const FCase Cases[] = {
			{ ELureCastBlock::None, TEXT("the line came in (reeled in)"), TEXT("The fish got away (line reeled in).") },
			{ ELureCastBlock::Sprinting, TEXT("the line came in (sprinting)"), TEXT("The fish got away (sprinting).") },
			{ ELureCastBlock::Swimming, TEXT("the line came in (swimming)"), TEXT("The fish got away (swimming).") },
			{ ELureCastBlock::Climbing, TEXT("the line came in (climbing)"), TEXT("The fish got away (climbing).") },
			{ ELureCastBlock::RodTucked, TEXT("the line came in (crawling)"), TEXT("The fish got away (crawling).") },
			{ ELureCastBlock::Teleported, TEXT("the line came in (teleported)"), TEXT("The fish got away (teleported).") },
			{ ELureCastBlock::Unpossessed, TEXT("the line came in (the player left)"), TEXT("The fish got away (the player left).") },
		};
		for (const FCase& Case : Cases)
		{
			const FString Name = BlockName(Case.Reason);
			if (!Scene.StartFight(*this))
			{
				return false;
			}
			LureFightQA::FLogCapture Log(TEXT("LogLureFishing"));
			Scene.Fishing->AuthorityReelIn(Case.Reason);
			TestEqual(FString::Printf(TEXT("%s: result Lost"), *Name), LureFightQA::ResultName(Scene.Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
			TestEqual(FString::Printf(TEXT("%s: one reason line in LogLureFishing"), *Name), Log.Count(Case.LogText), 1);
			TestEqual(FString::Printf(TEXT("%s: the log names the lost fish"), *Name), Log.Count(TEXT("Bonefish lost.")), 1);
			TestTrue(FString::Printf(TEXT("%s: HUD says why (%s)"), *Name, *Scene.Hud()), Scene.Hud().Contains(Case.HudText));
			Scene.World.Tick(2);
		}

		// A line with no fish on it that comes in is not a lost fish: no "lost" line.
		if (!LureFightQA::CastAndWait(*this, Scene.World, Scene.Fishing))
		{
			return false;
		}
		LureFightQA::FLogCapture Log(TEXT("LogLureFishing"));
		Scene.Fishing->AuthorityReelIn(ELureCastBlock::Sprinting);
		TestEqual(TEXT("no fish on: result ReeledIn"), LureFightQA::ResultName(Scene.Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::ReeledIn));
		TestEqual(TEXT("no fish on: no 'lost' log line"), Log.Count(TEXT(" lost.")), 0);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

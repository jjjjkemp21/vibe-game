// Lure T-007 x T-010 integration (unreal-engineer, playtest B1 of 2026-09-23): a real catch (hook, reel fight, landing on the
// server) reaches the player's cooler and XP exactly once, for the host's pawn and for a pawn owned by a remote client.
// Project.Fishing.Fight.Landing.GoesToCoolerAndXp

#include "FightQATestUtils.h"
#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Game/LurePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionTypes.h"

namespace LureFightLandingProgression
{
	/** A server-side player: an ALurePlayerState (fixture tables) and a controller possessing Character. bLocal false = a remote client's controller. */
	struct FServerPlayer
	{
		ALurePlayerState* State = nullptr;
		ULureCoolerComponent* Cooler = nullptr;
		ULureProgressionComponent* Progression = nullptr;
	};

	static bool MakePlayer(FAutomationTestBase& Test, UWorld* World, ALurePlayerCharacter* Character, bool bLocal, const UDataTable* Levels,
		const UDataTable* Coolers, FServerPlayer& Out)
	{
		ALurePlayerState* State = World->SpawnActorDeferred<ALurePlayerState>(ALurePlayerState::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test.TestNotNull(TEXT("ALurePlayerState spawned"), State) || !State->GetCooler() || !State->GetProgression())
		{
			return false;
		}
		State->GetCooler()->SetCoolerTable(Coolers);
		State->GetProgression()->SetLevelTable(Levels);
		State->FinishSpawning(FTransform::Identity);

		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("PlayerController spawned"), Controller))
		{
			return false;
		}
		if (bLocal)
		{
			Controller->SetAsLocalPlayerController();
		}
		State->SetOwner(Controller);
		Controller->SetPlayerState(State);
		Controller->Possess(Character);
		Character->SetPlayerState(State);
		Test.TestEqual(TEXT("the controller is local only for the host"), Controller->IsLocalController(), bLocal);

		Out.State = State;
		Out.Cooler = State->GetCooler();
		Out.Progression = State->GetProgression();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightLandingGoesToCooler, "Project.Fishing.Fight.Landing.GoesToCoolerAndXp", LureFightQA::Flags)
	bool FLureFightLandingGoesToCooler::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance HostFish;
		FFishInstance ClientFish;
		FFishInstance HostFish2;
		if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.2f, 71, HostFish)
			|| !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Uncommon"), 0.25f, 72, ClientFish)
			|| !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.1f, 73, HostFish2))
		{
			return false;
		}
		TestTrue(TEXT("the rolled fish carry XP"), HostFish.Xp > 0 && ClientFish.Xp > 0 && HostFish2.Xp > 0);

		const TStrongObjectPtr<UDataTable> Levels = LPT::MakeTable(*this, FPlayerLevelRow::StaticStruct(), LPT::FixtureLevelCsv());
		const TStrongObjectPtr<UDataTable> Coolers = LPT::MakeTable(*this, FCoolerRow::StaticStruct(), LPT::FixtureCoolerCsv());
		LureFightQA::FWorld World;
		if (!Levels.IsValid() || !Coolers.IsValid() || !World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* HostCharacter = World.Spawn(LureFightQA::StandAt());
		ALurePlayerCharacter* ClientCharacter = World.Spawn(LureFightQA::StandAt() + FVector(0.f, 300.f, 0.f));
		FServerPlayer Host;
		FServerPlayer Client;
		if (!HostCharacter || !ClientCharacter || !MakePlayer(*this, World.World, HostCharacter, true, Levels.Get(), Coolers.Get(), Host)
			|| !MakePlayer(*this, World.World, ClientCharacter, false, Levels.Get(), Coolers.Get(), Client))
		{
			return false;
		}
		TestTrue(TEXT("the server's copy of the client's pawn has authority"), ClientCharacter->HasAuthority());

		ULureFishingComponent* HostFishing = LureFightQA::SetUpFishing(HostCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get(), 12.f, 1);
		ULureFishingComponent* ClientFishing = LureFightQA::SetUpFishing(ClientCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get(), 12.f, 2);
		if (!HostFishing || !ClientFishing)
		{
			AddError(TEXT("no fishing components"));
			return false;
		}
		int32 HostLanded = 0;
		int32 ClientLanded = 0;
		HostFishing->OnFishLandedNative.AddLambda([&HostLanded](ULureFishingComponent*, const FFishInstance&) { ++HostLanded; });
		ClientFishing->OnFishLandedNative.AddLambda([&ClientLanded](ULureFishingComponent*, const FFishInstance&) { ++ClientLanded; });

		TestEqual(TEXT("host cooler starts empty"), Host.Cooler->GetNumFish(), 0);
		TestEqual(TEXT("client cooler starts empty"), Client.Cooler->GetNumFish(), 0);
		TestEqual(TEXT("host starts at 0 XP"), Host.Progression->GetTotalXp(), 0);

		// Hook, fight (reeling), land.
		if (!LureFightQA::CastAndWait(*this, World, HostFishing, 0.f) || !LureFightQA::CastAndWait(*this, World, ClientFishing, 0.f)
			|| !TestTrue(TEXT("host hooks"), HostFishing->AuthorityHookFish(HostFish)) || !TestTrue(TEXT("client hooks"), ClientFishing->AuthorityHookFish(ClientFish)))
		{
			return false;
		}
		World.Tick(5);
		TestTrue(TEXT("the host's fight is on"), HostFishing->GetFightNet().bActive);
		TestTrue(TEXT("the client's fight is on"), ClientFishing->GetFightNet().bActive);
		HostFishing->AuthoritySetReeling(true);
		ClientFishing->AuthoritySetReeling(true);
		TestTrue(TEXT("both fish are landed by reeling"), World.TickUntil([&]() { return HostLanded > 0 && ClientLanded > 0; }, 60 * 60));
		World.Tick(60);

		TestEqual(TEXT("host: one landing"), HostLanded, 1);
		TestEqual(TEXT("client: one landing"), ClientLanded, 1);
		if (TestEqual(TEXT("host: the landed fish is in the host's cooler, once"), Host.Cooler->GetNumFish(), 1))
		{
			TestTrue(TEXT("host: it is the host's fish"), LureFightQA::SameFish(Host.Cooler->GetFish()[0], HostFish));
		}
		if (TestEqual(TEXT("client: the landed fish is in the client's cooler, once"), Client.Cooler->GetNumFish(), 1))
		{
			TestTrue(TEXT("client: it is the client's fish"), LureFightQA::SameFish(Client.Cooler->GetFish()[0], ClientFish));
		}
		TestEqual(TEXT("host: XP = the fish's XP"), Host.Progression->GetTotalXp(), HostFish.Xp);
		TestEqual(TEXT("client: XP = the fish's XP"), Client.Progression->GetTotalXp(), ClientFish.Xp);

		// A second catch adds exactly one more fish and its XP.
		if (!TestEqual(TEXT("host is idle again"), static_cast<int32>(HostFishing->GetFishingState()), static_cast<int32>(ELureFishingState::Idle))
			|| !LureFightQA::CastAndWait(*this, World, HostFishing, 0.f) || !TestTrue(TEXT("host hooks a second fish"), HostFishing->AuthorityHookFish(HostFish2)))
		{
			return false;
		}
		HostFishing->AuthoritySetReeling(true);
		TestTrue(TEXT("the second fish is landed"), World.TickUntil([&]() { return HostLanded > 1; }, 60 * 60));
		World.Tick(60);
		TestEqual(TEXT("host: two fish in the cooler"), Host.Cooler->GetNumFish(), 2);
		TestEqual(TEXT("host: XP of both fish"), Host.Progression->GetTotalXp(), HostFish.Xp + HostFish2.Xp);
		TestEqual(TEXT("client: unchanged by the host's catch"), Client.Cooler->GetNumFish(), 1);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

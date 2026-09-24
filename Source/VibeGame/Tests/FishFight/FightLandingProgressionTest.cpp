// Lure T-007 x T-010 x T-030 integration (unreal-engineer, playtest B1 of 2026-09-23; T-030 catch handling): a real catch (hook,
// reel fight, landing on the server) gives its XP exactly once and hangs on the angler's hook, for the host's pawn and for a pawn
// owned by a remote client; a new cast waits until the fish is off the hook (here: put into a physical cooler).
// Project.Fishing.Fight.Landing.HangsOnHookWithXp

#include "FightQATestUtils.h"
#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
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
		ULureProgressionComponent* Progression = nullptr;
	};

	static bool MakePlayer(FAutomationTestBase& Test, UWorld* World, ALurePlayerCharacter* Character, bool bLocal, const UDataTable* Levels, FServerPlayer& Out)
	{
		ALurePlayerState* State = World->SpawnActorDeferred<ALurePlayerState>(ALurePlayerState::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test.TestNotNull(TEXT("ALurePlayerState spawned"), State) || !State->GetProgression())
		{
			return false;
		}
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
		Out.Progression = State->GetProgression();
		return true;
	}

	/** The record on Character's hook (invalid if nothing hangs there) */
	static FFishInstance Hanging(const ALurePlayerCharacter* Character)
	{
		const ULureHandsComponent* Hands = Character ? Character->GetHands() : nullptr;
		const ALureFishItem* Fish = Hands ? Hands->GetHangingFish() : nullptr;
		return Fish ? Fish->GetFish() : FFishInstance();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightLandingHangsOnHook, "Project.Fishing.Fight.Landing.HangsOnHookWithXp", LureFightQA::Flags)
	bool FLureFightLandingHangsOnHook::RunTest(const FString& Parameters)
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
		const TStrongObjectPtr<UDataTable> Coolers = LPT::ShippedTable(*this, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
		LureFightQA::FWorld World;
		if (!Levels.IsValid() || !Coolers.IsValid() || !World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* HostCharacter = World.Spawn(LureFightQA::StandAt());
		ALurePlayerCharacter* ClientCharacter = World.Spawn(LureFightQA::StandAt() + FVector(0.f, 300.f, 0.f));
		FServerPlayer Host;
		FServerPlayer Client;
		if (!HostCharacter || !ClientCharacter || !MakePlayer(*this, World.World, HostCharacter, true, Levels.Get(), Host)
			|| !MakePlayer(*this, World.World, ClientCharacter, false, Levels.Get(), Client))
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

		TestFalse(TEXT("nothing hangs on the host's hook yet"), Hanging(HostCharacter).IsValid());
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
		TestTrue(TEXT("host: the landed fish hangs on the host's hook"), LureFightQA::SameFish(Hanging(HostCharacter), HostFish));
		TestTrue(TEXT("client: the landed fish hangs on the client's hook"), LureFightQA::SameFish(Hanging(ClientCharacter), ClientFish));
		TestEqual(TEXT("host: XP = the fish's XP, once"), Host.Progression->GetTotalXp(), HostFish.Xp);
		TestEqual(TEXT("client: XP = the fish's XP, once"), Client.Progression->GetTotalXp(), ClientFish.Xp);

		// While a fish hangs, a new cast is refused (the line is still out, with the fish on it).
		TestEqual(TEXT("host is idle again"), static_cast<int32>(HostFishing->GetFishingState()), static_cast<int32>(ELureFishingState::Idle));
		TestEqual(TEXT("host: a cast is refused while the fish hangs"), static_cast<int32>(HostFishing->GetCastBlock()), static_cast<int32>(ELureCastBlock::Busy));
		TestFalse(TEXT("host: AuthorityCast is refused"), HostFishing->AuthorityCast(0.f, 0.f));

		// Grab it and put it in a cooler: the hands are free, the rod comes back and fishing goes on.
		ULureHandsComponent* Hands = HostCharacter->GetHands();
		ALureFishItem* First = Hands ? Hands->GetHangingFish() : nullptr;
		if (ULureCatchSubsystem* Catch = ULureCatchSubsystem::Get(World.World))
		{
			Catch->SetCoolerTable(Coolers.Get()); // the shipped DT_Cooler.csv, not the binary asset
		}
		ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(World.World, NAME_None, FTransform(LureFightQA::StandAt() + FVector(-60.f, 0.f, 0.f)), Host.State);
		if (!TestNotNull(TEXT("the host's fish item"), First) || !TestNotNull(TEXT("a cooler"), Cooler) || !TestTrue(TEXT("grab it"), Hands->AuthorityTakeInHand(First)))
		{
			return false;
		}
		TestFalse(TEXT("the rod is stowed while the fish is in hand"), HostFishing->IsRodInHand());
		TestTrue(TEXT("into the cooler"), Cooler->AuthorityPutFishIn(HostCharacter, First));
		World.Tick(2);
		TestTrue(TEXT("the rod is back"), HostFishing->IsRodInHand());
		FLureCaughtFish Stored;
		TestTrue(TEXT("the cooler holds the host's fish"), Cooler->GetStorage()->GetFishAt(0, Stored) && LureFightQA::SameFish(Stored.Fish, HostFish));

		// A second catch: one more landing, its XP, and it hangs.
		if (!LureFightQA::CastAndWait(*this, World, HostFishing, 0.f) || !TestTrue(TEXT("host hooks a second fish"), HostFishing->AuthorityHookFish(HostFish2)))
		{
			return false;
		}
		HostFishing->AuthoritySetReeling(true);
		TestTrue(TEXT("the second fish is landed"), World.TickUntil([&]() { return HostLanded > 1; }, 60 * 60));
		World.Tick(60);
		TestTrue(TEXT("host: the second fish hangs"), LureFightQA::SameFish(Hanging(HostCharacter), HostFish2));
		TestEqual(TEXT("host: XP of both fish"), Host.Progression->GetTotalXp(), HostFish.Xp + HostFish2.Xp);
		TestTrue(TEXT("client: unchanged by the host's catch"), LureFightQA::SameFish(Hanging(ClientCharacter), ClientFish));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

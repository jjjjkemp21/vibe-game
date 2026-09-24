// Lure T-030g tests (unreal-engineer): regressions from the A2 playtest (Saved/AgentLogs/playtest/20260923-231437-A2).
//  - the cooler lid's look follows its state (the put-in clack is short; a late-joining client doesn't clack);
//  - every fish, from the smallest to the largest weight, is visible inside the open cooler (the pile at the back wall);
//  - E acts on what the view ray hits first (a fish looked at beside a cooler), then the angle rule;
//  - the status line's cooler count follows the cooler you carry.
// Rules: docs/specs/catch-handling-rules.md "The cooler", "Focus and input", "HUD".
// Project.Catch.Cooler.LidLookMatchesState, Project.Catch.Net.LidLookOnClient, Project.Catch.Display.EveryFishVisible,
// Project.Catch.Focus.ViewRayHitWins, Project.Catch.Hud.CarriedCoolerCount

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishTypes.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionLibrary.h"
#if WITH_EDITOR
#include "Tests/NetTestHelpers.h"
#endif

namespace LureCoolerLidFocusTest
{
	/** The player at the dock's center looking along +X, with a cooler Ahead cm in front (its front toward the player) */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ULureHandsComponent* Hands = nullptr;
		ULureInteractionComponent* Use = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!W.Create(Test))
			{
				return false;
			}
			Player = W.SpawnPlayer(Test, FVector(0.0f, 0.0f, LCT::DockTop));
			Hands = LCT::HandsOf(Player);
			Use = LCT::InteractionOf(Player);
			if (!Test.TestNotNull(TEXT("player"), Player) || !Test.TestNotNull(TEXT("hands"), Hands) || !Test.TestNotNull(TEXT("use keys"), Use))
			{
				return false;
			}
			W.Tick(20); // standing on the dock
			return true;
		}
	};

	/** The shown fish of Cooler (visible components under its ContentsRoot) */
	TArray<const UPrimitiveComponent*> ShownFish(const ALureCoolerActor* Cooler)
	{
		TArray<const UPrimitiveComponent*> Out;
		TInlineComponentArray<UPrimitiveComponent*> Parts(Cooler);
		for (const UPrimitiveComponent* Part : Parts)
		{
			const USceneComponent* Parent = Part->GetAttachParent();
			if (Parent && Parent->GetFName() == TEXT("ContentsRoot") && Part->IsVisible())
			{
				Out.Add(Part);
			}
		}
		return Out;
	}

	/**
	 *  The cooler geometry the visibility check uses, in the Contents socket's space (cm; +X = the front, toward the player).
	 *  Conservative: the placeholder box's front face and body top (the real SM_Cooler_Starter liner lip is ~1 cm further in
	 *  and ~1.4 cm lower); the Contents socket is 5 cm above the cooler's floor (the liner floor center).
	 */
	constexpr double ContentsZ = 5.0;
	constexpr double RimX = 22.0;
	constexpr double RimZ = 35.0 - ContentsZ;
	constexpr double LinerHalfX = 17.5;
	constexpr double LinerHalfY = 25.5;
	/** The closed lid's underside (SK_Fish.anim.md: 33.0 cm above the pivot) */
	constexpr double LidUndersideZ = 33.0 - ContentsZ;
	/** The display must show every fish out to this far (eye to the cooler's center, horizontally) */
	constexpr double ViewDistance = 150.0;

	/** Height of the sight line from an eye (EyeZ, D in front of the center) over the front rim, at X */
	double SightLineZ(double EyeZ, double D, double X)
	{
		return RimZ - (RimX - X) * (EyeZ - RimZ) / (D - RimX);
	}
}

// ---- 1. The lid's look follows its state ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerLidLookMatchesState, "Project.Catch.Cooler.LidLookMatchesState", LCT::Flags)
bool FCoolerLidLookMatchesState::RunTest(const FString& Parameters)
{
	LureCoolerLidFocusTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
	TestEqual(TEXT("DT_Catch: the clack is 30 deg"), Tuning.LidPulsePitch, 30.0f);
	ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("cooler"), Cooler))
	{
		return false;
	}
	TestTrue(TEXT("a fresh cooler is closed and looks closed before its first tick"), !Cooler->IsLidOpen() && FMath::IsNearlyZero(Cooler->GetLidPitch(), 0.01f));
	Rig.W.Tick(10);
	TestEqual(TEXT("... and after"), Cooler->GetLidPitch(), 0.0f, 0.01f);

	// A fish into the closed cooler: a short clack, never an open-looking lid. The A2 screenshots were taken 0.27-0.38 s after
	// the put-in, with the old pulse still holding the lid 60 deg up while the prompt said "Open the cooler".
	ALureFishItem* Fish = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 20, 1, 1.5f, 901));
	TestTrue(TEXT("in it goes"), Fish && Cooler->AuthorityPutFishIn(Rig.Player, Fish));
	float MaxPitch = 0.0f;
	float PitchAfterLidTime = -1.0f;
	bool bEverOpen = false;
	const int32 LidTimeTicks = FMath::CeilToInt(Tuning.LidOpenTime / LCT::Dt);
	for (int32 Tick = 1; Tick <= 40; ++Tick)
	{
		Rig.W.Tick(1);
		MaxPitch = FMath::Max(MaxPitch, Cooler->GetLidPitch());
		bEverOpen |= Cooler->IsLidOpen();
		if (Tick >= LidTimeTicks)
		{
			PitchAfterLidTime = FMath::Max(PitchAfterLidTime, Cooler->GetLidPitch());
		}
	}
	TestFalse(TEXT("the lid state stays closed"), bEverOpen);
	TestTrue(FString::Printf(TEXT("the lid clacks: up to %.1f deg, at most LidPulsePitch"), MaxPitch), MaxPitch > 5.0f && MaxPitch <= Tuning.LidPulsePitch + 0.5f);
	TestEqual(TEXT("... and is shut again within LidOpenTime (and stays shut)"), PitchAfterLidTime, 0.0f, 0.01f);
	LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
	Rig.W.Tick(1);
	const FLureResolvedInteraction E = Rig.Use->ResolveInteraction(ELureInteractKey::Primary);
	TestTrue(TEXT("the prompt says Open, the lid looks shut"), E.Verb == ELureInteractVerb::OpenCooler && E.Target == Cooler && Cooler->GetLidPitch() < 0.01f);

	// Open and close.
	TestTrue(TEXT("open"), Cooler->AuthoritySetLidOpen(true));
	Rig.W.Tick(30);
	TestEqual(TEXT("open: the lid at LidOpenPitch"), Cooler->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);
	// A fish into the open cooler: no clack, the lid stays open.
	Fish = Rig.W.LandInHand(Rig.Player, LCT::MakeFish(TEXT("Bonefish"), 20, 1, 1.5f, 902));
	TestTrue(TEXT("in it goes (open)"), Fish && Cooler->AuthorityPutFishIn(Rig.Player, Fish));
	float MinOpen = TNumericLimits<float>::Max();
	for (int32 Tick = 0; Tick < 30; ++Tick)
	{
		Rig.W.Tick(1);
		MinOpen = FMath::Min(MinOpen, Cooler->GetLidPitch());
	}
	TestEqual(TEXT("... the open lid doesn't move"), MinOpen, Tuning.LidOpenPitch, 0.01f);
	TestTrue(TEXT("close"), Cooler->AuthoritySetLidOpen(false));
	Rig.W.Tick(30);
	TestEqual(TEXT("closed: the lid down"), Cooler->GetLidPitch(), 0.0f, 0.01f);

	// Carry an open cooler and put it down: closed, and it looks closed.
	TestTrue(TEXT("open again"), Cooler->AuthoritySetLidOpen(true));
	Rig.W.Tick(30);
	TestTrue(TEXT("pick up (closes it)"), Cooler->AuthorityPickUp(Rig.Player) && !Cooler->IsLidOpen());
	Rig.W.Tick(30);
	TestEqual(TEXT("carried: the lid down"), Cooler->GetLidPitch(), 0.0f, 0.01f);
	TestTrue(TEXT("put down"), Cooler->AuthorityPutDown());
	Rig.W.Tick(60);
	TestTrue(TEXT("put down: closed and looks closed"), !Cooler->IsLidOpen() && FMath::IsNearlyZero(Cooler->GetLidPitch(), 0.01f));
	LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
	Rig.W.Tick(2);
	TestTrue(TEXT("... the prompt agrees"), Rig.Use->ResolveInteraction(ELureInteractKey::Primary).Verb == ELureInteractVerb::OpenCooler);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerLidLookOnClient, "Project.Catch.Net.LidLookOnClient", LCT::Flags)
bool FCoolerLidLookOnClient::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	Worlds.SetTickInSeconds(LCT::Dt);
	UWorld* Server = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("harness: the server world is up"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Movement = LCT::Shipped(*this, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"));
	const TStrongObjectPtr<UDataTable> Coolers = LCT::Shipped(*this, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
	const TStrongObjectPtr<UDataTable> Freshness = LCT::Shipped(*this, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness.csv"));
	const TStrongObjectPtr<UDataTable> Catch = LCT::Shipped(*this, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
	const TStrongObjectPtr<UDataTable> Display = LCT::Shipped(*this, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
	FishQA::FTables Fish;
	if (!Movement.IsValid() || !Coolers.IsValid() || !Freshness.IsValid() || !Catch.IsValid() || !Display.IsValid() || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FLureCatchRow* Tuning = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("LidOnClient"), false);
	if (!TestNotNull(TEXT("DT_Catch Default"), Tuning))
	{
		return false;
	}
	auto Prepare = [&](UWorld* World)
	{
		if (ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
		{
			Subsystem->SetTuning(*Tuning);
			Subsystem->SetFreshnessTable(Freshness.Get());
			Subsystem->SetCoolerTable(Coolers.Get());
			Subsystem->SetCoolerDisplayTable(Display.Get());
			Subsystem->SetFishTables(Fish.Get());
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Dock = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Dock, TEXT("Dock"));
		Box->SetBoxExtent(FVector(LCT::DockHalf, LCT::DockHalf, LCT::DockTop * 0.5f), false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetRelativeLocation_Direct(FVector(0.0f, 0.0f, LCT::DockTop * 0.5f));
		Dock->SetRootComponent(Box);
		Box->RegisterComponent();
	};
	Prepare(Server);

	// A server-side player (nobody controls it) who puts fish in and carries.
	TArray<FLureMovementRow> Rows;
	TArray<FString> Problems;
	FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
	const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.0f;
	const FTransform PawnAt(FRotator::ZeroRotator, FVector(0.0f, 0.0f, LCT::DockTop + HalfHeight + 2.15f));
	ALurePlayerCharacter* Pawn = Server->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), PawnAt, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("server pawn"), Pawn))
	{
		return false;
	}
	Pawn->GetLureMovement()->ApplyMovementTable(Movement.Get());
	Pawn->FinishSpawning(PawnAt);
	ULureHandsComponent* Hands = LCT::HandsOf(Pawn);
	ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(130.0f, 0.0f, LCT::DockTop)), nullptr);
	if (!TestNotNull(TEXT("hands"), Hands) || !TestNotNull(TEXT("server cooler"), Cooler))
	{
		return false;
	}
	Worlds.TickServer();
	auto PutFishIn = [&](int32 Seed)
	{
		ALureFishItem* Item = ALureFishItem::SpawnFish(Server, FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, Seed), 0.0), FTransform(FVector(40.0f, 0.0f, LCT::DockTop)));
		return Item && Hands->AuthorityTakeInHand(Item) && Cooler->AuthorityPutFishIn(Pawn, Item);
	};
	// Before the client joins: a fish goes into the closed cooler (the pulse id is bumped).
	TestTrue(TEXT("server: a fish in before the client joins"), PutFishIn(911));
	for (int32 Tick = 0; Tick < 30; ++Tick)
	{
		Worlds.TickServer();
	}

	if (!TestTrue(TEXT("harness: the client connects"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	UWorld* ClientWorld = Worlds.Clients[0].GetWorld();
	Prepare(ClientWorld);
	auto OnClient = [&Worlds, Cooler]() -> ALureCoolerActor*
	{
		if (!Worlds.IsServerObjectReplicated(Cooler) || !Worlds.DoesReplicatedObjectExistOnClient(Cooler, 0u))
		{
			return nullptr;
		}
		return Cast<ALureCoolerActor>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(Cooler), 0u));
	};
	if (!TestTrue(TEXT("the cooler reaches the client"), Worlds.TickAllUntil([&]() { return OnClient() != nullptr && OnClient()->HasActorBegunPlay(); }, LCT::Dt, 600)))
	{
		return false;
	}
	ALureCoolerActor* Remote = OnClient();
	float MaxJoin = 0.0f;
	for (int32 Tick = 0; Tick < 40; ++Tick)
	{
		Worlds.TickAll(1);
		MaxJoin = FMath::Max(MaxJoin, Remote->GetLidPitch());
	}
	TestTrue(FString::Printf(TEXT("client: a closed cooler with an old pulse joins shut, no clack (max %.1f deg)"), MaxJoin), !Remote->IsLidOpen() && MaxJoin < 0.01f);

	// Open and close on the server: the client's lid follows.
	TestTrue(TEXT("server: open"), Cooler->AuthoritySetLidOpen(true));
	TestTrue(TEXT("client: the lid opens"), Worlds.TickAllUntil([&]() { return Remote->IsLidOpen() && FMath::IsNearlyEqual(Remote->GetLidPitch(), Tuning->LidOpenPitch, 0.01f); }, LCT::Dt, 120));
	TestTrue(TEXT("server: close"), Cooler->AuthoritySetLidOpen(false));
	TestTrue(TEXT("client: the lid shuts"), Worlds.TickAllUntil([&]() { return !Remote->IsLidOpen() && Remote->GetLidPitch() < 0.01f; }, LCT::Dt, 120));

	// A fish into the closed cooler with the client watching: a short clack there too.
	TestTrue(TEXT("server: another fish in"), PutFishIn(912));
	float MaxLive = 0.0f;
	int32 FirstUp = -1;
	int32 LastUp = -1;
	bool bEverOpen = false;
	for (int32 Tick = 0; Tick < 90; ++Tick)
	{
		Worlds.TickAll(1);
		const float Pitch = Remote->GetLidPitch();
		MaxLive = FMath::Max(MaxLive, Pitch);
		bEverOpen |= Remote->IsLidOpen();
		if (Pitch > 0.01f)
		{
			FirstUp = FirstUp < 0 ? Tick : FirstUp;
			LastUp = Tick;
		}
	}
	TestFalse(TEXT("client: the lid state stays closed"), bEverOpen);
	TestTrue(FString::Printf(TEXT("client: the lid clacks (max %.1f deg, at most LidPulsePitch)"), MaxLive), MaxLive > 5.0f && MaxLive <= Tuning->LidPulsePitch + 0.5f);
	TestTrue(FString::Printf(TEXT("client: ... for less than LidOpenTime (%d ticks)"), LastUp - FirstUp + 1),
		FirstUp >= 0 && (LastUp - FirstUp + 1) * LCT::Dt <= Tuning->LidOpenTime + 0.001f);

	// Carried and put down on the server: shut on the client.
	TestTrue(TEXT("server: open, then pick up (closes)"), Cooler->AuthoritySetLidOpen(true) && Cooler->AuthorityPickUp(Pawn));
	TestTrue(TEXT("client: carried, the lid shut"), Worlds.TickAllUntil([&]() { return !Remote->IsFree() && !Remote->IsLidOpen() && Remote->GetLidPitch() < 0.01f; }, LCT::Dt, 120));
	TestTrue(TEXT("server: put down"), Cooler->AuthorityPutDown());
	TestTrue(TEXT("client: put down, the lid shut"), Worlds.TickAllUntil([&]() { return Remote->IsFree() && !Remote->IsLidOpen() && Remote->GetLidPitch() < 0.01f; }, LCT::Dt, 120));
	Worlds.TickAll(30);
	TestTrue(TEXT("client: ... and stays shut"), !Remote->IsLidOpen() && Remote->GetLidPitch() < 0.01f);
	return true;
}
#endif // WITH_EDITOR

// ---- 2. Every fish is visible in the open cooler ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerDisplayEveryFishVisible, "Project.Catch.Display.EveryFishVisible", LCT::Flags)
bool FCoolerDisplayEveryFishVisible::RunTest(const FString& Parameters)
{
	namespace T = LureCoolerLidFocusTest;
	const TStrongObjectPtr<UDataTable> Display = LCT::Shipped(*this, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
	const TStrongObjectPtr<UDataTable> Visual = LCT::Shipped(*this, FFishVisualRow::StaticStruct(), TEXT("DT_FishVisual.json"));
	const TStrongObjectPtr<UDataTable> Movement = LCT::Shipped(*this, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"));
	const TStrongObjectPtr<UDataTable> Catch = LCT::Shipped(*this, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
	FishQA::FTables Fish;
	if (!Display.IsValid() || !Visual.IsValid() || !Movement.IsValid() || !Catch.IsValid() || !FishQA::LoadReal(*this, Fish))
	{
		return false;
	}
	const FFishVisualRow* VisualRow = Visual->FindRow<FFishVisualRow>(TEXT("Default"), TEXT("EveryFishVisible"), false);
	const FLureMovementRow* Stand = Movement->FindRow<FLureMovementRow>(TEXT("Stand"), TEXT("EveryFishVisible"), false);
	const FLureCatchRow* Tuning = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("EveryFishVisible"), false);
	if (!TestNotNull(TEXT("DT_FishVisual Default"), VisualRow) || !TestNotNull(TEXT("DT_Movement Stand"), Stand) || !TestNotNull(TEXT("DT_Catch Default"), Tuning))
	{
		return false;
	}
	// The standing eye, cooler on the player's floor, in the Contents space.
	const double EyeZ = Stand->EyeHeight - T::ContentsZ;
	// Every species from a quarter of its lightest to four times its heaviest roll (any size modifier, T-008/T-027).
	TArray<TPair<FName, float>> Cases;
	TArray<float> References;
	Fish.Species->ForeachRow<FFishSpeciesRow>(TEXT("EveryFishVisible"), [&](const FName& Name, const FFishSpeciesRow& Row)
	{
		for (const float Weight : { Row.WeightMin * 0.25f, Row.WeightMin, Row.ReferenceWeight, Row.WeightMax, Row.WeightMax * 4.0f })
		{
			Cases.Add(TPair<FName, float>(Name, Weight));
			References.Add(Row.ReferenceWeight);
		}
	});
	TestTrue(TEXT("species to check"), Cases.Num() >= 10);

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Pair : Display->GetRowMap())
	{
		const FLureCoolerDisplayRow& Row = *reinterpret_cast<const FLureCoolerDisplayRow*>(Pair.Value);
		const FString Name = Pair.Key.ToString();
		for (int32 Slot = 0; Slot < Row.Slots.Num(); ++Slot)
		{
			for (int32 Case = 0; Case < Cases.Num(); ++Case)
			{
				const float Scale = ALureCoolerActor::GetDisplayFishScale(Cases[Case].Value, References[Case], *VisualRow, Row);
				const FTransform Place = ALureCoolerActor::GetDisplayFishTransform(Row, Slot, Scale);
				const FVector At = Place.GetLocation();
				const double Bottom = At.Z - Row.LieOffsetCm * Scale;
				const double Top = At.Z + Row.LieOffsetCm * Scale;
				const FString What = FString::Printf(TEXT("%s slot %d, %s %.2f kg (scale %.2f)"), *Name, Slot, *Cases[Case].Key.ToString(), Cases[Case].Value, Scale);
				++Checked;
				if (!(Scale > 0.2f && Scale <= Row.MaxFishScale + 1.0e-4f) || !FMath::IsNearlyEqual(static_cast<float>(Place.GetScale3D().X), Scale, 1.0e-4f))
				{
					AddError(What + TEXT(": shown between 0.2 and MaxFishScale"));
				}
				if (Bottom < -0.01 || Top > T::LidUndersideZ || FMath::Abs(At.X) > T::LinerHalfX || FMath::Abs(At.Y) > T::LinerHalfY)
				{
					AddError(FString::Printf(TEXT("%s: inside the liner, on the floor or a fish, under the closed lid (at %s, bottom %.2f, top %.2f)"), *What, *At.ToCompactString(), Bottom, Top));
				}
				// The top fish of a pile of Slot + 1 (a lone fish in slot 0) must clear the sight line over the front rim
				// from the put-down distance out to ViewDistance.
				for (double D = Tuning->PutDownDistance; D <= T::ViewDistance + 0.01; D += 5.0)
				{
					const double Line = T::SightLineZ(EyeZ, D, At.X);
					if (Top < Line + 1.0)
					{
						AddError(FString::Printf(TEXT("%s: hidden by the front wall from %.0f cm (its top %.2f cm, the sight line %.2f cm)"), *What, D, Top, Line));
						break;
					}
				}
			}
		}
	}
	TestTrue(FString::Printf(TEXT("checked %d shown fish"), Checked), Checked >= 40);

	// The playtest case in a world: a lone 5.5 kg Bonefish in the open starter cooler is shown, at the cap, at the back.
	LCT::FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	ALureCoolerActor* Cooler = W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f);
	if (!TestNotNull(TEXT("cooler"), Cooler))
	{
		return false;
	}
	Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 36, 1, 5.5f, 921), W.Now()));
	TestTrue(TEXT("open"), Cooler->AuthoritySetLidOpen(true));
	W.Tick(30);
	const TArray<const UPrimitiveComponent*> Shown = LureCoolerLidFocusTest::ShownFish(Cooler);
	TestEqual(TEXT("the lone Bonefish shows"), Cooler->GetNumDisplayedFish(), 1);
	const FLureCoolerDisplayRow Row = ULureCatchSubsystem::Get(W.World)->GetCoolerDisplayRow(Cooler->GetCoolerId());
	if (TestEqual(TEXT("one fish drawn"), Shown.Num(), 1))
	{
		const FTransform Expected = ALureCoolerActor::GetDisplayFishTransform(Row, 0, Row.MaxFishScale);
		const FTransform Actual = Shown[0]->GetRelativeTransform();
		TestTrue(FString::Printf(TEXT("... in slot 0 at the cap (%s)"), *Actual.GetLocation().ToCompactString()), Actual.GetLocation().Equals(Expected.GetLocation(), 0.05));
		TestTrue(TEXT("... at the back of the cooler (away from the front wall)"), Actual.GetLocation().X < 0.0);
		TestFalse(TEXT("... never hidden in game"), Shown[0]->bHiddenInGame);
	}
	return true;
}

// ---- 3. What the view ray hits wins the focus ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFocusViewRayHitWins, "Project.Catch.Focus.ViewRayHitWins", LCT::Flags)
bool FFocusViewRayHitWins::RunTest(const FString& Parameters)
{
	LureCoolerLidFocusTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	// A loose fish on the dock just in front of a cooler; the player looks steeply down at the fish (A2 shots 30/31).
	ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(66.0f, 0.0f, LCT::DockTop), 180.0f);
	ALureFishItem* Loose = ALureFishItem::SpawnFish(Rig.W.World, FLureCaughtFish::Landed(LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.5f, 932), Rig.W.Now()),
		FTransform(FVector(40.0f, 0.0f, LCT::DockTop)));
	if (!TestNotNull(TEXT("cooler"), Cooler) || !TestNotNull(TEXT("loose fish"), Loose))
	{
		return false;
	}
	Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 30, 1, 5.5f, 931), Rig.W.Now()));
	Cooler->AuthoritySetLidOpen(true); // E on the cooler would take the Bonefish out, as in the playtest
	Rig.W.Tick(30);
	LCT::LookAt(Rig.Player, Loose->GetInteractionLocation());
	Rig.W.Tick(2);
	const FVector Eye = Rig.Player->GetPawnViewLocation();
	const FVector View = Rig.Player->GetViewRotation().Vector();
	const ILureInteractable* CoolerUse = Cooler;
	const ILureInteractable* FishUse = Loose;
	// The old rule's trap: both at 0 deg (the ray passes through the cooler's 35 cm focus sphere too), and the cooler's
	// center is nearer to the eye, so the tie went to the cooler.
	TestTrue(FString::Printf(TEXT("setup: both at 0 deg (cooler %.2f, fish %.2f)"), CoolerUse->GetFocusAngle(Eye, View), FishUse->GetFocusAngle(Eye, View)),
		CoolerUse->GetFocusAngle(Eye, View) <= 0.01f && FishUse->GetFocusAngle(Eye, View) <= 0.01f);
	TestTrue(TEXT("setup: the cooler's center is the nearer"), FVector::Dist(Eye, Cooler->GetInteractionLocation()) < FVector::Dist(Eye, Loose->GetInteractionLocation()));
	TestTrue(TEXT("the ray misses the cooler's box"), CoolerUse->GetFocusHitDistance(Eye, View) < 0.0);
	TestTrue(TEXT("... and hits the fish"), FishUse->GetFocusHitDistance(Eye, View) >= 0.0);
	TestTrue(TEXT("the fish you look at has the focus"), Rig.Use->FindFocusedInteractable() == Loose);
	const FLureResolvedInteraction E = Rig.Use->ResolveInteraction(ELureInteractKey::Primary);
	TestTrue(TEXT("E picks the fish up (not the Bonefish out of the cooler)"), E.Target == Loose && E.Verb == ELureInteractVerb::GrabFish);
	TestTrue(TEXT("E"), Rig.Use->PressKey(ELureInteractKey::Primary));
	TestTrue(TEXT("the snapper is in hand, the cooler keeps its fish"), Rig.Hands->GetHeldFish() == Loose && Cooler->GetNumFish() == 1);
	TestTrue(TEXT("drop it again"), Rig.Hands->AuthorityReleaseHeld() == Loose);
	Loose->AuthorityPlace(FVector(40.0f, 0.0f, LCT::DockTop), FRotator::ZeroRotator, FVector(40.0f, 0.0f, LCT::DockTop), /*bAnimate*/ false);
	Rig.W.Tick(5);

	// Looking at the cooler's top, the ray hits its box first: the cooler.
	LCT::LookAt(Rig.Player, Cooler->GetActorLocation() + FVector(0.0f, 0.0f, Cooler->GetBoxHalfExtent().Z * 2.0f - 2.0f));
	Rig.W.Tick(2);
	TestTrue(TEXT("looking at the cooler's top: the cooler"), Rig.Use->FindFocusedInteractable() == Cooler);

	// Nothing hit: the angle rule (the smaller angle within FocusAngleDeg).
	LCT::LookAt(Rig.Player, Loose->GetInteractionLocation() + FVector(0.0f, -45.0f, 0.0f));
	Rig.W.Tick(2);
	const FVector Eye2 = Rig.Player->GetPawnViewLocation();
	const FVector View2 = Rig.Player->GetViewRotation().Vector();
	const bool bNoHit = CoolerUse->GetFocusHitDistance(Eye2, View2) < 0.0 && FishUse->GetFocusHitDistance(Eye2, View2) < 0.0;
	TestTrue(TEXT("setup: looking beside both hits neither"), bNoHit);
	if (bNoHit)
	{
		const float FishAngle = FishUse->GetFocusAngle(Eye2, View2);
		const float CoolerAngle = CoolerUse->GetFocusAngle(Eye2, View2);
		AActor* Expected = FishAngle < CoolerAngle ? static_cast<AActor*>(Loose) : static_cast<AActor*>(Cooler);
		TestTrue(FString::Printf(TEXT("... the smaller angle wins (fish %.1f, cooler %.1f deg)"), FishAngle, CoolerAngle),
			FMath::Min(FishAngle, CoolerAngle) > ULureCatchSubsystem::GetTuningFor(Rig.W.World).FocusAngleDeg ? Rig.Use->FindFocusedInteractable() == nullptr
			: Rig.Use->FindFocusedInteractable() == Expected);
	}
	return true;
}

// ---- 4. The status line counts the cooler you carry ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudCarriedCoolerCount, "Project.Catch.Hud.CarriedCoolerCount", LCT::Flags)
bool FHudCarriedCoolerCount::RunTest(const FString& Parameters)
{
	LureCoolerLidFocusTest::FRig Rig;
	if (!Rig.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Friend = Rig.W.SpawnPlayer(*this, FVector(-200.0f, 200.0f, LCT::DockTop), /*bLocal*/ false);
	APlayerController* Controller = LCT::ControllerOf(Rig.Player);
	if (!TestNotNull(TEXT("friend"), Friend) || !TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}
	ALureCoolerActor* Mine = Rig.W.SpawnCooler(FVector(-150.0f, -150.0f, LCT::DockTop), 0.0f, NAME_None, Rig.Player->GetPlayerState());
	ALureCoolerActor* Theirs = Rig.W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f, NAME_None, Friend->GetPlayerState());
	if (!TestNotNull(TEXT("my cooler"), Mine) || !TestNotNull(TEXT("their cooler"), Theirs))
	{
		return false;
	}
	Theirs->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 30, 1, 1.5f, 941), Rig.W.Now()));
	Rig.W.Tick(5);
	auto StatusLine = [Controller]()
	{
		const TArray<FString> Lines = ULureProgressionLibrary::GetPlaceholderStatusLines(Controller);
		return Lines.Num() > 0 ? Lines[0] : FString();
	};
	TestTrue(FString::Printf(TEXT("hands empty: my own cooler (%s)"), *StatusLine()), StatusLine().EndsWith(TEXT("Cooler 0/4")));
	TestEqual(TEXT("... GetCoolerStatus"), ULureCatchLibrary::GetCoolerStatus(Controller), FString(TEXT("Cooler 0/4")));

	TestTrue(TEXT("I pick up my friend's cooler"), Theirs->AuthorityPickUp(Rig.Player));
	Rig.W.Tick(2);
	const FString Joined = FString::Join(ULureProgressionLibrary::GetPlaceholderStatusLines(Controller), TEXT(" | "));
	TestTrue(FString::Printf(TEXT("carrying theirs: the count is the carried cooler's (%s)"), *StatusLine()), StatusLine().EndsWith(TEXT("Cooler 1/4")));
	TestTrue(FString::Printf(TEXT("... one count on screen: it matches the Carrying line (%s)"), *Joined), Joined.Contains(TEXT("Carrying: Starter cooler (1/4, closed)"))
		&& !Joined.Contains(TEXT("0/4")));
	TestEqual(TEXT("my own cooler still counts itself"), ULureCatchLibrary::GetOwnCoolerStatus(Rig.Player->GetPlayerState()), FString(TEXT("Cooler 0/4")));

	TestTrue(TEXT("put it down"), Theirs->AuthorityPutDown());
	Rig.W.Tick(2);
	TestTrue(FString::Printf(TEXT("put down: my own again (%s)"), *StatusLine()), StatusLine().EndsWith(TEXT("Cooler 0/4")));
	TestEqual(TEXT("no controller: empty"), ULureCatchLibrary::GetCoolerStatus(nullptr), FString());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

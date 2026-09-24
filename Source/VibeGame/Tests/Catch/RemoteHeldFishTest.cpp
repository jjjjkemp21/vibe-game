// Lure T-030k tests (unreal-engineer): a fish held by ANOTHER player (a simulated proxy on this machine) lies side-on in
// front of their capsule at hand height, the owner's first-person HoldFish pose seen from outside, and plays the same clip
// role as the holder's own copy. A real in-process server with two connected clients (UE::Net::FTestWorlds; the server is
// dedicated, so client 1 sees client 0 as a simulated proxy). Rules: docs/specs/catch-handling-rules.md "Items and the
// carry model"; the pose: art/export/Characters/SK_FPArms.anim.md "HoldFish". Project.Catch.RemoteHeldFish.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fish/FishAnimInstance.h"
#include "GameFramework/PlayerController.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionTypes.h"
#include "Tests/NetTestHelpers.h"

namespace LureRemoteHeldFishTest
{
	constexpr float NetDt = 1.0f / 60.0f;

	/** hand_r_fish at HoldFish frame 0, relative to the eye (SK_FPArms.anim.md "HoldFish"): what the holder sees */
	const FRotator SpecHandFishRotation(5.1f, 79.7f, -15.0f);
	const FVector SpecHandFishLocation(46.9f, 8.0f, -16.8f);

	/** The shipped data every machine uses (text sources, never the binary assets) */
	struct FData
	{
		TStrongObjectPtr<UDataTable> Movement;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Freshness;
		TStrongObjectPtr<UDataTable> Catch;
		TStrongObjectPtr<UDataTable> Display;
		TStrongObjectPtr<UDataTable> Levels;
		FishQA::FTables Fish;
		FLureCatchRow Tuning;

		bool Load(FAutomationTestBase& Test)
		{
			Movement = LCT::Shipped(Test, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"));
			Coolers = LCT::Shipped(Test, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
			Freshness = LCT::Shipped(Test, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness.csv"));
			Catch = LCT::Shipped(Test, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
			Display = LCT::Shipped(Test, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
			Levels = LCT::MakeTableChecked(Test, FPlayerLevelRow::StaticStruct(), TEXT("Name,Level,XpToNext,DevComment\nL01,1,100,\nL02,2,150,\nL03,3,0,\n"), false, TEXT("levels"));
			if (!Movement.IsValid() || !Coolers.IsValid() || !Freshness.IsValid() || !Catch.IsValid() || !Display.IsValid() || !Levels.IsValid() || !FishQA::LoadReal(Test, Fish))
			{
				return false;
			}
			const FLureCatchRow* Row = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("RemoteHeldFishTest"), false);
			if (!Test.TestNotNull(TEXT("DT_Catch Default"), Row))
			{
				return false;
			}
			Tuning = *Row;
			return true;
		}

		void Inject(UWorld* World) const
		{
			if (ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
			{
				Subsystem->SetTuning(Tuning);
				Subsystem->SetFreshnessTable(Freshness.Get());
				Subsystem->SetCoolerTable(Coolers.Get());
				Subsystem->SetCoolerDisplayTable(Display.Get());
				Subsystem->SetFishTables(Fish.Get());
			}
		}
	};

	/** The level geometry every machine has: the test dock (top z = LCT::DockTop) */
	void AddDock(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Dock"));
		Box->SetBoxExtent(FVector(LCT::DockHalf, LCT::DockHalf, LCT::DockTop * 0.5f), false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetRelativeLocation_Direct(FVector(0.0f, 0.0f, LCT::DockTop * 0.5f));
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
	}

	/** A dedicated server with two connected players on the dock (player 0 faces Yaw0), and their copies on the clients */
	struct FNet
	{
		UE::Net::FTestWorlds& Worlds;
		FData Data;
		UWorld* Server = nullptr;
		ALurePlayerCharacter* Players[2] = { nullptr, nullptr };

		explicit FNet(UE::Net::FTestWorlds& InWorlds) : Worlds(InWorlds) {}

		bool Create(FAutomationTestBase& Test, float Yaw0)
		{
			Test.AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			// Harness artifact: the docks are spawned per world (not replicated), so the players' movement base can't be sent.
			Test.AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			if (!Data.Load(Test))
			{
				return false;
			}
			Server = Worlds.Server.GetWorld();
			if (!Test.TestTrue(TEXT("harness: the server world is up"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()))
			{
				return false;
			}
			for (int32 Client = 0; Client < 2; ++Client)
			{
				if (!Test.TestTrue(FString::Printf(TEXT("harness: client %d connects"), Client), Worlds.CreateAndConnectClient()))
				{
					return false;
				}
			}
			AddDock(Server);
			Data.Inject(Server);
			for (UE::Net::FTestWorldInstance& Client : Worlds.Clients)
			{
				AddDock(Client.GetWorld());
				Data.Inject(Client.GetWorld());
			}
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Data.Movement.Get(), Rows, Problems);
			const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.0f;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				APlayerController* Controller = Worlds.GetServerPlayerControllerOfClient(Index);
				const FTransform Transform(FRotator(0.0f, Index == 0 ? Yaw0 : 0.0f, 0.0f), FVector(Index == 0 ? 0.0f : 250.0f, Index == 0 ? -100.0f : 100.0f, LCT::DockTop + HalfHeight + 2.15f));
				ALurePlayerCharacter* Pawn = Server->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Test.TestNotNull(TEXT("harness: controller"), Controller) || !Test.TestNotNull(TEXT("harness: pawn"), Pawn))
				{
					return false;
				}
				Pawn->GetLureMovement()->ApplyMovementTable(Data.Movement.Get());
				Pawn->FinishSpawning(Transform);
				Controller->Possess(Pawn);
				Controller->SetControlRotation(Transform.Rotator());
				if (ULureProgressionComponent* Progression = LCT::ProgressionOf(Pawn))
				{
					Progression->SetLevelTable(Data.Levels.Get());
				}
				Players[Index] = Pawn;
			}
			const bool bReady = Until([this]()
			{
				for (int32 Client = 0; Client < 2; ++Client)
				{
					const ALurePlayerCharacter* Mine = On(Client, Players[Client]);
					const ALurePlayerCharacter* Theirs = On(Client, Players[1 - Client]);
					if (!Mine || !Theirs || !Mine->IsLocallyControlled() || !Mine->GetHands() || !Theirs->GetHands() || !ClientPC(Client))
					{
						return false;
					}
				}
				return true;
			}, 600);
			if (!Test.TestTrue(TEXT("harness: each client has its own (controlled) player and the other one"), bReady))
			{
				return false;
			}
			if (APlayerController* Own = ClientPC(0))
			{
				Own->SetControlRotation(FRotator(0.0f, Yaw0, 0.0f)); // a level view: the first-person hold at its authored frame
			}
			Worlds.TickAll(30); // settle on the dock
			return true;
		}

		template <typename T>
		T* On(int32 Client, T* ServerObject) const
		{
			// The harness ensures when asked about an object the server hasn't replicated yet (no net id): ask that first.
			if (!ServerObject || !Worlds.IsServerObjectReplicated(ServerObject) || !Worlds.DoesReplicatedObjectExistOnClient(ServerObject, static_cast<uint32>(Client)))
			{
				return nullptr;
			}
			return Cast<T>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(ServerObject), static_cast<uint32>(Client)));
		}

		bool Until(TFunctionRef<bool()> Predicate, int32 MaxTicks = 180)
		{
			return Worlds.TickAllUntil([&Predicate]() { return Predicate(); }, NetDt, MaxTicks);
		}

		APlayerController* ClientPC(int32 Client) const
		{
			UWorld* World = Worlds.Clients.IsValidIndex(Client) ? Worlds.Clients[Client].GetWorld() : nullptr;
			return World ? World->GetFirstPlayerController() : nullptr;
		}

		/** Player 0 lands a Bonefish and the server puts it in its hand; true once both clients see it there */
		ALureFishItem* HoldFishOnPlayer0(FAutomationTestBase& Test, int32 Seed)
		{
			ALurePlayerCharacter* Holder = Players[0];
			ALureFishItem* Item = Cast<ALureFishItem>(ULureCatchLibrary::HandleFishLanded(Holder, LCT::MakeFish(TEXT("Bonefish"), 45, 5, 1.5f, Seed)).FishItem);
			if (!Test.TestNotNull(TEXT("server fish item"), Item) || !Test.TestTrue(TEXT("the server puts it in player 0's hand"), LCT::HandsOf(Holder)->AuthorityTakeInHand(Item)))
			{
				return nullptr;
			}
			const bool bSeen = Until([&]()
			{
				const ALureFishItem* Own = On(0, Item);
				const ALureFishItem* Other = On(1, Item);
				return Own && Other && Own->IsHeldBy(On(0, Holder), ELureHoldMode::Hand) && Other->IsHeldBy(On(1, Holder), ELureHoldMode::Hand)
					&& Own->HasLook() && Other->HasLook();
			});
			if (!Test.TestTrue(TEXT("both clients see the fish in player 0's hand"), bSeen))
			{
				return nullptr;
			}
			Worlds.TickAll(5);
			return Item;
		}
	};

	FString RoleName(EFishAnimRole Role)
	{
		return StaticEnum<EFishAnimRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	/** Where the settings put the grip frame (the first-person hand_r_fish) on Holder's body, in the world */
	FTransform ExpectedGripFrame(const APawn* Holder)
	{
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		const FTransform Local(Settings->ThirdPersonFishRotation, Settings->ThirdPersonFishOffset + FVector(0.0f, 0.0f, Holder->BaseEyeHeight));
		return Local * Holder->GetActorTransform();
	}

	/** The fish's right-palm contact (under the gills) in the world: what the holder's hand grips */
	FVector ContactInWorld(const ALureFishItem* Fish)
	{
		const FVector Contact = GetDefault<ULureCatchSettings>()->HeldFishContactPoint;
		return Fish->GetActorTransform().TransformPosition(Fish->GetGripOffset() + Fish->GetWeightScale() * Contact);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteHeldFishDefaults, "Project.Catch.RemoteHeldFish.SettingsMatchFirstPersonHold", LCT::Flags)
	bool FRemoteHeldFishDefaults::RunTest(const FString& Parameters)
	{
		// Other players see the owner's pose: the third-person grip frame is the HoldFish clip's hand_r_fish frame from the eye.
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		TestTrue(FString::Printf(TEXT("rotation = HoldFish frame 0 (%s)"), *Settings->ThirdPersonFishRotation.ToString()),
			Settings->ThirdPersonFishRotation.Equals(SpecHandFishRotation, 0.01f));
		TestTrue(FString::Printf(TEXT("offset from the eye = HoldFish frame 0 (%s)"), *Settings->ThirdPersonFishOffset.ToString()),
			Settings->ThirdPersonFishOffset.Equals(SpecHandFishLocation, 0.01f));
		// Side-on and level: the fish's head to the holder's right, its back up (not the old nose-down 60 degrees).
		const FQuat Rotation = Settings->ThirdPersonFishRotation.Quaternion();
		TestTrue(TEXT("the head points to the holder's right"), Rotation.GetForwardVector().Y > 0.95);
		TestTrue(TEXT("the fish is level (not head-down)"), FMath::Abs(Rotation.GetForwardVector().Z) < 0.2);
		TestTrue(TEXT("its back is up"), Rotation.GetUpVector().Z > 0.9);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteHeldFishOnProxy, "Project.Catch.RemoteHeldFish.SideOnAtHandHeightOnProxy", LCT::Flags)
	bool FRemoteHeldFishOnProxy::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		constexpr float Yaw0 = 35.0f; // not axis-aligned: the fish must turn with the holder's body
		if (!Net.Create(*this, Yaw0))
		{
			return false;
		}
		ALureFishItem* Item = Net.HoldFishOnPlayer0(*this, 701);
		if (!Item)
		{
			return false;
		}
		ALurePlayerCharacter* Holder1 = Net.On(1, Net.Players[0]); // the holder as client 1 sees it
		ALureFishItem* Item1 = Net.On(1, Item);
		ALureFishItem* Item0 = Net.On(0, Item);
		if (!TestNotNull(TEXT("client 1's copy of the holder"), Holder1) || !TestNotNull(TEXT("client 1's fish"), Item1) || !TestNotNull(TEXT("client 0's fish"), Item0))
		{
			return false;
		}
		TestTrue(TEXT("on client 1 the holder is a simulated proxy"), !Holder1->IsLocallyControlled() && Holder1->GetLocalRole() == ROLE_SimulatedProxy);
		TestTrue(FString::Printf(TEXT("the proxy faces the holder's yaw (%.1f)"), Holder1->GetActorRotation().Yaw), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Holder1->GetActorRotation().Yaw), Yaw0, 1.0));

		// Attached to the proxy's capsule (not a first-person primitive), at the grip frame the settings give.
		const USceneComponent* Root1 = Item1->GetRootComponent();
		TestTrue(TEXT("attached to the proxy's capsule"), Root1->GetAttachParent() == Holder1->GetRootComponent() && Root1->GetAttachSocketName().IsNone());
		const UPrimitiveComponent* Mesh1 = Item1->GetFishMesh();
		TestTrue(TEXT("drawn as a world primitive for the other player"), Mesh1 && Mesh1->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::None);
		const FTransform Grip = ExpectedGripFrame(Holder1);
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		const FVector Contact = Grip.TransformPosition(Settings->HeldFishContactPoint);
		TestTrue(FString::Printf(TEXT("the throat is in the proxy's right hand (%.3f cm off)"), FVector::Dist(ContactInWorld(Item1), Contact)),
			FVector::Dist(ContactInWorld(Item1), Contact) < 0.1);
		TestTrue(TEXT("the fish is not rotated against the grip frame"), Item1->GetActorQuat().Equals(Grip.GetRotation(), 1.0e-3f));

		// Side-on: head to the holder's right, level, back up; seen from in front the other player sees its flank.
		const FVector Forward = Item1->GetActorForwardVector();
		const FVector Up = Item1->GetActorUpVector();
		TestTrue(FString::Printf(TEXT("head to the holder's right (%.3f)"), FVector::DotProduct(Forward, Holder1->GetActorRightVector())),
			FVector::DotProduct(Forward, Holder1->GetActorRightVector()) > 0.95);
		TestTrue(FString::Printf(TEXT("level, not head-down (forward z %.3f)"), Forward.Z), FMath::Abs(Forward.Z) < 0.2);
		TestTrue(FString::Printf(TEXT("back up (up z %.3f)"), Up.Z), Up.Z > 0.9);
		TestTrue(TEXT("its flank faces a viewer in front of the holder"), FMath::Abs(FVector::DotProduct(Item1->GetActorRightVector(), Holder1->GetActorForwardVector())) > 0.9);

		// Hand height, in front of the body: between the capsule center and the eye, clear of the capsule.
		const FVector Center = Holder1->GetActorLocation();
		const float Radius = Holder1->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const FVector Hand = Grip.GetLocation();
		TestTrue(FString::Printf(TEXT("hand height: above the capsule center, below the eye (%.1f < %.1f < %.1f)"), Center.Z, Hand.Z, Holder1->GetPawnViewLocation().Z),
			Hand.Z > Center.Z + 20.0 && Hand.Z < Holder1->GetPawnViewLocation().Z);
		TestTrue(FString::Printf(TEXT("in front of the body, clear of the capsule (%.1f > %.1f)"), FVector::DotProduct(Hand - Center, Holder1->GetActorForwardVector()), Radius),
			FVector::DotProduct(Hand - Center, Holder1->GetActorForwardVector()) > Radius);

		// The same pose as the holder's own view: client 0's fish relative to its camera = client 1's relative to the body's eye.
		const FQuat Seen = Holder1->GetActorQuat().Inverse() * Item1->GetActorQuat();
		TestTrue(FString::Printf(TEXT("the pose relative to the body is HoldFish frame 0 (%s)"), *Seen.Rotator().ToString()),
			Seen.Equals(SpecHandFishRotation.Quaternion(), 1.0e-3f));

		// Crouching: the fish follows the proxy's eye down (no new hold on the way).
		ALurePlayerCharacter* Own0 = Net.On(0, Net.Players[0]);
		const float EyeBefore = static_cast<float>(Holder1->GetPawnViewLocation().Z);
		const float HandBefore = static_cast<float>(ContactInWorld(Item1).Z);
		Own0->RequestStance(ELureStance::Crouch);
		Net.Until([&]() { return Net.Players[0]->GetStance() == ELureStance::Crouch; }, 120);
		Worlds.TickAll(60);
		const float EyeDrop = EyeBefore - static_cast<float>(Holder1->GetPawnViewLocation().Z);
		const float HandDrop = HandBefore - static_cast<float>(ContactInWorld(Item1).Z);
		if (EyeDrop > 10.0f)
		{
			TestTrue(FString::Printf(TEXT("crouched: the fish drops with the proxy's eye (%.1f vs %.1f cm)"), HandDrop, EyeDrop), FMath::IsNearlyEqual(HandDrop, EyeDrop, 1.0f));
		}
		else
		{
			AddWarning(FString::Printf(TEXT("the proxy's eye did not drop on crouch here (%.1f cm): only the eye-relative formula is checked"), EyeDrop));
		}
		const FVector ContactNow = ExpectedGripFrame(Holder1).TransformPosition(Settings->HeldFishContactPoint);
		TestTrue(TEXT("still in the proxy's right hand"), FVector::Dist(ContactInWorld(Item1), ContactNow) < 0.1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteHeldFishAnimRole, "Project.Catch.RemoteHeldFish.SameAnimRoleAsHolder", LCT::Flags)
	bool FRemoteHeldFishAnimRole::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNet Net(Worlds);
		if (!Net.Create(*this, 0.0f))
		{
			return false;
		}
		ALureFishItem* Item = Net.HoldFishOnPlayer0(*this, 702);
		if (!Item)
		{
			return false;
		}
		ALureFishItem* Item0 = Net.On(0, Item);
		ALureFishItem* Item1 = Net.On(1, Item);
		if (!TestNotNull(TEXT("client 0's fish"), Item0) || !TestNotNull(TEXT("client 1's fish"), Item1))
		{
			return false;
		}
		const EFishAnimRole InHand = Item1->GetInHandAnimState().Role;
		TestEqual(TEXT("in a hand a fish plays the landed fish's role (Flop)"), RoleName(InHand), RoleName(EFishAnimRole::Flop));
		TestEqual(TEXT("the same rule on both machines"), RoleName(Item0->GetInHandAnimState().Role), RoleName(InHand));

		FFishAnimState Own;
		FFishAnimState Other;
		const bool bOwn = Item0->GetShownAnimState(Own);
		const bool bOther = Item1->GetShownAnimState(Other);
		if (!bOwn && !bOther && !(Item1->GetFishMesh() && Item1->GetFishMesh()->IsA<USkeletalMeshComponent>()))
		{
			AddWarning(TEXT("SK_Bonefish is not loaded here (placeholder shape, no anim instance): only the rule is checked"));
			return true;
		}
		TestTrue(TEXT("the holder's copy plays a fish anim"), bOwn);
		TestTrue(TEXT("the other player's copy plays a fish anim"), bOther);
		TestEqual(TEXT("holder: the in-hand role"), RoleName(Own.Role), RoleName(InHand));
		TestEqual(TEXT("other player: the same role as the holder"), RoleName(Other.Role), RoleName(Own.Role));
		TestEqual(TEXT("... at the same play rate"), Other.PlayRate, Own.PlayRate, 1.0e-4f);
		TestEqual(TEXT("... and amplitude"), Other.Amplitude, Own.Amplitude, 1.0e-4f);

		// Put down: the own mesh stops playing it on every machine (a fish on the ground lies still).
		const TWeakObjectPtr<ALureFishItem> Weak1 = Item1;
		ALurePlayerCharacter* Holder = Net.Players[0];
		const FVector Spot = Holder->GetActorLocation() + Holder->GetActorForwardVector() * 80.0f - FVector(0.0f, 0.0f, 90.0f);
		LCT::HandsOf(Holder)->AuthorityReleaseHeld();
		Item->AuthorityPlace(Spot, FRotator::ZeroRotator, Item->GetActorLocation(), /*bAnimate*/ false);
		TestTrue(TEXT("free on client 1"), Net.Until([&]() { return Weak1.IsValid() && Weak1->IsFree(); }));
		Worlds.TickAll(3);
		FFishAnimState Down;
		TestFalse(TEXT("on the ground: no in-hand anim on client 1's copy"), Weak1.IsValid() && Weak1->GetShownAnimState(Down));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

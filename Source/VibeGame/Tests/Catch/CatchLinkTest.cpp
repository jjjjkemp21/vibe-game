// Integration of T-030 (the hanging fish item) with T-029 (the landed fight fish) and T-032 (the physics line), wired by
// ULureCatchLinkSubsystem. Project.Catch.Link.*  Rules: docs/specs/catch-handling-rules.md ("Landing", "The glue").

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/FishVisual/FightFishVisualQATestUtils.h"
#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchLinkSubsystem.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCatchTypes.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Components/SceneComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Misc/ScopeExit.h"

namespace LureCatchLinkTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Where the item's mouth (the line's hook point) is now */
	inline FVector MouthOf(const ALureFishItem* Item)
	{
		return Item->GetActorLocation() + Item->GetActorQuat().RotateVector(Item->GetMouthOffset());
	}

	/** A plain actor with a scene root at Transform (a stand-in for a landed fight fish) */
	inline AActor* SpawnMarker(UWorld* World, const FTransform& Transform)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorTransform(Transform);
		return Actor;
	}

	/** The T-029 real-fight scene plus the shipped DT_Catch row and the fish tables in the catch subsystem */
	struct FScene : LureFightFishQA::FRealScene
	{
		TStrongObjectPtr<UDataTable> CatchTable;
		FLureCatchRow Tuning;
		ULureCatchSubsystem* Catch = nullptr;
		ULureCatchLinkSubsystem* Link = nullptr;
		ULureHandsComponent* Hands = nullptr;

		bool Setup(FAutomationTestBase& Test)
		{
			FString Text;
			if (!Init(Test) || !LureFightQA::ReadSource(Test, TEXT("DT_Catch.csv"), Text)
				|| !LureFightQA::MakeTableChecked(Test, CatchTable, FLureCatchRow::StaticStruct(), Text, false, TEXT("DT_Catch.csv")))
			{
				return false;
			}
			const FLureCatchRow* Row = CatchTable->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("CatchLinkTest"), false);
			Catch = ULureCatchSubsystem::Get(World.World);
			Link = ULureCatchLinkSubsystem::Get(World.World);
			Hands = ULureHandsComponent::Get(Character);
			if (!Test.TestNotNull(TEXT("DT_Catch.csv Default row"), Row) || !Test.TestNotNull(TEXT("the catch subsystem"), Catch)
				|| !Test.TestNotNull(TEXT("the catch link subsystem"), Link) || !Test.TestNotNull(TEXT("the player's hands"), Hands))
			{
				return false;
			}
			Tuning = *Row;
			Catch->SetTuning(Tuning);
			Catch->SetFreshnessTable(nullptr);
			Catch->SetFishTables(Fish.Get());
			return true;
		}
	};

	// ---- A real fight: the landed fish hangs on the line's end, swings, and goes into the hand ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchLinkLandedFish, "Project.Catch.Link.LandedFishHangsSwingsAndGoesIntoTheHand", Flags)
	bool FCatchLinkLandedFish::RunTest(const FString& Parameters)
	{
		FScene S;
		if (!S.Setup(*this) || !LureFightQA::CastAndWait(*this, S.World, S.Fishing))
		{
			return false;
		}
		if (!TestTrue(TEXT("hooked"), S.Fishing->AuthorityHookFish(S.Bonefish)))
		{
			return false;
		}
		S.World.Tick(1);
		const TWeakObjectPtr<ALureFightFish> FightFish = S.Visuals->FindFish(S.Fishing);
		ULureFishingLineComponent* Line = S.Fishing->GetLine();
		if (!TestTrue(TEXT("T-029 shows the fighting fish"), FightFish.IsValid()) || !TestNotNull(TEXT("T-032 draws the line"), Line))
		{
			return false;
		}

		bool bAdopted = false;
		FTransform LandedAt;
		TWeakObjectPtr<ALureFishItem> Adopter;
		const FDelegateHandle Handle = ALureFishItem::OnLandedVisualAdopted.AddLambda([&](ALureFishItem* Item, const FTransform& VisualWorld)
		{
			bAdopted = true;
			LandedAt = VisualWorld;
			Adopter = Item;
		});
		ON_SCOPE_EXIT { ALureFishItem::OnLandedVisualAdopted.Remove(Handle); };

		S.Fishing->AuthoritySetReeling(true);
		for (int32 Frame = 0; Frame < 40 * 60 && S.Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			S.World.Tick(1);
		}
		S.Fishing->AuthoritySetReeling(false);
		if (!TestEqual(TEXT("the fight is landed"), LureFightQA::ResultName(S.Fishing->GetNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed)))
		{
			return false;
		}

		// The landing frame (the fishing step lands and hangs the item in TG_PostUpdateWork; T-029 hands its fish over later
		// in TG_LastDemotable): the landed fish is the hanging fish's look, and the line already ends at its mouth.
		ALureFishItem* Item = S.Hands->GetHangingFish();
		if (!TestNotNull(TEXT("a fish item hangs on the hook"), Item))
		{
			return false;
		}
		TestTrue(TEXT("the landed fight fish became its look (kept, not destroyed)"), FightFish.IsValid() && Item->GetAdoptedVisual() == FightFish.Get()
			&& FightFish->GetAttachParentActor() == Item);
		TestTrue(TEXT("... in the landing frame"), bAdopted && Adopter.Get() == Item);
		TestTrue(TEXT("the physics line holds it"), Line->GetEndActor() == Item && Item->GetExternalHangDriver() == Line && S.Link->GetLineFor(Item) == Line);
		const FVector LandedMouth = LandedAt.GetLocation() + LandedAt.GetRotation().RotateVector(Item->GetMouthOffset());
		TestTrue(FString::Printf(TEXT("same frame: the line ends at the landed fish's mouth as drawn this frame (end %s, mouth %s)"),
			*Line->GetEndPoint().ToCompactString(), *LandedMouth.ToCompactString()), Line->GetEndPoint().Equals(LandedMouth, 0.5));
		TestTrue(TEXT("... and the fish's mouth is on it"), MouthOf(Item).Equals(Line->GetEndPoint(), 0.1));
		TestTrue(TEXT("the line is drawn"), Line->IsLineVisible());

		// Every frame after: the line moves the fish in its own update, so the mouth is this frame's line end, and the line
		// starts at the rod tip. It swings in under the rod tip and settles on the hang line.
		float MaxGap = 0.f;
		double Travel = 0.0;
		FVector Previous = Line->GetEndPoint();
		auto Step = [&](int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				S.World.Tick(1);
				MaxGap = FMath::Max(MaxGap, static_cast<float>(FVector::Dist(MouthOf(Item), Line->GetEndPoint())));
				Travel += FVector::Dist(Previous, Line->GetEndPoint());
				Previous = Line->GetEndPoint();
			}
		};
		auto Settled = [&]()
		{
			const FVector Tip = Line->GetStartPoint();
			const FVector End = Line->GetEndPoint();
			return FVector::Dist2D(Tip, End) < 0.3 * S.Tuning.HangLineLength && End.Z < Tip.Z - 0.8 * S.Tuning.HangLineLength
				&& FMath::IsNearlyEqual(Line->GetRestLength(), S.Tuning.HangLineLength, 1.f);
		};
		Step(10 * 60);
		TestTrue(FString::Printf(TEXT("it swung in (the end travelled %.0f cm)"), Travel), Travel > 5.0);
		TestTrue(FString::Printf(TEXT("it settled under the rod tip on %.0f cm of line (tip %s, end %s, line %.1f)"), S.Tuning.HangLineLength,
			*Line->GetStartPoint().ToCompactString(), *Line->GetEndPoint().ToCompactString(), Line->GetRestLength()), Settled());
		TestTrue(FString::Printf(TEXT("the fish's mouth stayed on the line's end every frame (worst gap %.3f cm)"), MaxGap), MaxGap <= 0.1f);

		// The player steps aside: the fish lags behind on its line, then swings back under the tip.
		const FVector Side = S.Character->GetActorRightVector();
		S.Character->SetActorLocation(S.Character->GetActorLocation() + Side * 60.f, false, nullptr, ETeleportType::TeleportPhysics);
		Step(1);
		const double Lag = FVector::DotProduct(Line->GetStartPoint() - Line->GetEndPoint(), Side);
		TestTrue(FString::Printf(TEXT("after a step aside the fish hangs behind the rod tip (%.1f cm)"), Lag), Lag > 10.0);
		Step(10 * 60);
		TestTrue(TEXT("... then swings back under it"), Settled());
		TestTrue(FString::Printf(TEXT("... the mouth on the line's end all along (worst gap %.3f cm)"), MaxGap), MaxGap <= 0.1f);

		// Into the hand: the line lets go, the landed fish look goes along.
		if (!TestTrue(TEXT("grabbed"), S.Hands->AuthorityTakeInHand(Item)))
		{
			return false;
		}
		TestTrue(TEXT("in the hand"), Item->GetHold().Mode == ELureHoldMode::Hand && S.Hands->GetHeldFish() == Item);
		TestNull(TEXT("the line let go of it"), Line->GetEndActor());
		TestNull(TEXT("... and no longer drives it"), Item->GetExternalHangDriver());
		TestNull(TEXT("... nor is it linked"), S.Link->GetLineFor(Item));
		S.World.Tick(2);
		TestFalse(TEXT("no line is drawn with the fish in the hand"), Line->IsLineVisible());
		TestTrue(TEXT("the landed fish look is in the hand with the item"), FightFish.IsValid() && FightFish->GetAttachParentActor() == Item);
		TestTrue(TEXT("the item rides on the player"), Item->GetAttachParentActor() == S.Character);

		if (ALureCarryableItem* Held = S.Hands->AuthorityReleaseHeld())
		{
			Held->Destroy();
		}
		S.World.Tick(1);
		TestFalse(TEXT("the look goes with the item"), FightFish.IsValid());
		return true;
	}

	// ---- The two replication orders: the line always ends at the landed fish's mouth, the same frame ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatchLinkSameFrame, "Project.Catch.Link.LineEndIsThisFramesFishMouth", Flags)
	bool FCatchLinkSameFrame::RunTest(const FString& Parameters)
	{
		FScene S;
		if (!S.Setup(*this) || !LureFightQA::CastAndWait(*this, S.World, S.Fishing))
		{
			return false;
		}
		ULureFishingLineComponent* Line = S.Fishing->GetLine();
		if (!TestNotNull(TEXT("the line exists after a cast"), Line))
		{
			return false;
		}
		S.Fishing->AuthorityReelIn();
		S.World.TickUntil([&]() { return S.Fishing->GetFishingState() == ELureFishingState::Idle && !Line->IsLineVisible(); }, 5 * 60);
		TestFalse(TEXT("the line came in"), Line->IsLineVisible());

		// A: the item hangs first, the landed fish arrives after (a client that got the item before it saw the fight end).
		FFishInstance FishA = S.Bonefish;
		FishA.Seed = 9101;
		ULureCatchLibrary::HandleFishLanded(S.Character, FishA);
		ALureFishItem* ItemA = S.Hands->GetHangingFish();
		if (!TestNotNull(TEXT("A hangs"), ItemA))
		{
			return false;
		}
		TestTrue(TEXT("A: on the line at once"), Line->GetEndActor() == ItemA && ItemA->GetExternalHangDriver() == Line && Line->IsLineVisible());
		S.World.Tick(3);
		const FVector Tip = Line->GetStartPoint();
		const FTransform VisualA(FRotator(0.f, 35.f, 0.f), Tip + FVector(180.f, -40.f, -120.f));
		AActor* MarkerA = SpawnMarker(S.World.World, VisualA);
		S.Catch->OfferLandedVisual(MarkerA, FishA);
		TestTrue(TEXT("A: adopted at once"), ItemA->GetAdoptedVisual() == MarkerA);
		const FVector MouthA = VisualA.GetLocation() + VisualA.GetRotation().RotateVector(ItemA->GetMouthOffset());
		TestTrue(FString::Printf(TEXT("A: before any tick, the line ends at the landed fish's mouth (end %s, mouth %s)"),
			*Line->GetEndPoint().ToCompactString(), *MouthA.ToCompactString()), Line->GetEndPoint().Equals(MouthA, 0.5));
		TestTrue(TEXT("A: the item's mouth is there too"), MouthOf(ItemA).Equals(Line->GetEndPoint(), 0.1));
		TestTrue(TEXT("A: the line is laid from the rod tip"), Line->GetStartPoint().Equals(Tip, 1.0));
		TestTrue(FString::Printf(TEXT("A: it is reeled up from there (line %.0f cm, going to %.0f)"), Line->GetRestLength(), S.Tuning.HangLineLength),
			Line->GetRestLength() > S.Tuning.HangLineLength + 50.f && FMath::IsNearlyEqual(Line->GetTargetRestLength(), S.Tuning.HangLineLength, 0.5f));
		S.World.Tick(1);
		TestTrue(TEXT("A: next frame the mouth is still the line's end"), MouthOf(ItemA).Equals(Line->GetEndPoint(), 0.1));

		// B: the landed fish is there first, the item arrives and hangs after (T-030's swing start; the older fish drops).
		FFishInstance FishB = S.Bonefish;
		FishB.Seed = 9102;
		const FVector TipB = Line->GetStartPoint();
		const FTransform VisualB(FRotator(0.f, -70.f, 0.f), TipB + FVector(-90.f, 150.f, -140.f));
		AActor* MarkerB = SpawnMarker(S.World.World, VisualB);
		S.Catch->OfferLandedVisual(MarkerB, FishB);
		ULureCatchLibrary::HandleFishLanded(S.Character, FishB);
		ALureFishItem* ItemB = S.Hands->GetHangingFish();
		if (!TestTrue(TEXT("B hangs now"), ItemB && ItemB != ItemA))
		{
			return false;
		}
		TestTrue(TEXT("B: adopted its landed fish"), ItemB->GetAdoptedVisual() == MarkerB);
		TestTrue(TEXT("B: on the line"), Line->GetEndActor() == ItemB && ItemB->GetExternalHangDriver() == Line && S.Link->GetLineFor(ItemB) == Line);
		const FVector MouthB = VisualB.GetLocation() + VisualB.GetRotation().RotateVector(ItemB->GetMouthOffset());
		TestTrue(FString::Printf(TEXT("B: before any tick, the line ends at the landed fish's mouth (end %s, mouth %s)"),
			*Line->GetEndPoint().ToCompactString(), *MouthB.ToCompactString()), Line->GetEndPoint().Equals(MouthB, 0.5));
		TestTrue(TEXT("B: the item's mouth is there too"), MouthOf(ItemB).Equals(Line->GetEndPoint(), 0.1));
		TestTrue(TEXT("A dropped off the hook and the line let go of it"), ItemA->GetHold().Mode != ELureHoldMode::Hook && !ItemA->GetExternalHangDriver()
			&& !S.Link->GetLineFor(ItemA));
		S.World.Tick(1);
		TestTrue(TEXT("B: next frame the mouth is still the line's end"), MouthOf(ItemB).Equals(Line->GetEndPoint(), 0.1));

		if (ALureFishItem* Hanging = S.Hands->AuthorityReleaseHanging())
		{
			Hanging->Destroy();
		}
		ItemA->Destroy();
		S.World.Tick(2);
		TestFalse(TEXT("with no fish on it the line is gone"), Line->IsLineVisible());
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

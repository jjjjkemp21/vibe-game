// Lure T-030g independent QA (qa-engineer): the A2 playtest regressions, black-box from docs/specs/catch-handling-rules.md
// ("The cooler": lid look + contents display, "Focus and input", "HUD") and the public headers (never the .cpp).
// The engineer's own tests are Tests/Catch/CoolerLidFocusTest.cpp; these are different cases, not copies:
//  - Lid: the clack follows the DT_Catch value (20 and 0 injected, not just the shipped 30), toggles mid-swing settle,
//    a put-in during a closing swing, the Large row; a client that joins while the lid is OPEN shows it open at once
//    (no swing from 0); a clack from a real client key press is seen on BOTH clients.
//  - Display: contracts only (DT_CoolerDisplay slot values are being re-solved): one slot per capacity at most, pile
//    order bottom-up, beds on or above the liner floor, the scale cap (monotonic, custom caps, bad weights); in a world,
//    every shown fish (lone big, lone tiny, full cooler; turned and raised coolers) inside the cooler's box, above its floor,
//    at distinct slots.
//  - Focus: the ray geometry helpers (RayToBox/RayToSphere/AngleToBox/AngleToSphere) against hand-computed values; side by
//    side and fish-behind-cooler scenes in both spawn orders; a cooler someone else carries never takes the focus.
//  - HUD: carrying your own / a friend's Large cooler / live count changes / a friend carrying yours / no own cooler; and
//    over the network, the carrier's and the owner's machines.
// Project.Catch.QA.Lid.*, Project.Catch.QA.Display.*, Project.Catch.QA.Focus.*, Project.Catch.QA.Hud.*, Project.Catch.QA.Net.*
// The whole file sits in namespace LureCoolerLidFocusQA (unity builds: no file-scope using-directives).

#include "Tests/Catch/QACatchTestUtils.h"

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
#include "Interaction/LureInteractable.h"
#include "Interaction/LureInteractionComponent.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureProgressionTypes.h"
#if WITH_EDITOR
#include "Tests/NetTestHelpers.h"
#endif
#include <limits>

namespace LureCoolerLidFocusQA
{
	constexpr ELureInteractKey KeyE = ELureInteractKey::Primary;
	constexpr ELureInteractKey KeyF = ELureInteractKey::Secondary;

	/** One lid trace: the pitch and state every tick */
	struct FLidTrace
	{
		float MaxPitch = 0.0f;
		float MinPitch = TNumericLimits<float>::Max();
		int32 TicksUp = 0;
		bool bEverOpen = false;

		void Sample(const ALureCoolerActor* Cooler)
		{
			const float Pitch = Cooler->GetLidPitch();
			MaxPitch = FMath::Max(MaxPitch, Pitch);
			MinPitch = FMath::Min(MinPitch, Pitch);
			TicksUp += Pitch > 0.01f ? 1 : 0;
			bEverOpen |= Cooler->IsLidOpen();
		}
	};

	/** The visible shown fish of Cooler (primitives attached under its ContentsRoot), and that root (null if none) */
	TArray<const UPrimitiveComponent*> ShownFish(const ALureCoolerActor* Cooler, const USceneComponent** OutRoot = nullptr)
	{
		TArray<const UPrimitiveComponent*> Out;
		TInlineComponentArray<UPrimitiveComponent*> Parts(Cooler);
		for (const UPrimitiveComponent* Part : Parts)
		{
			const USceneComponent* Parent = Part->GetAttachParent();
			if (Parent && Parent->GetFName() == TEXT("ContentsRoot") && Part->IsVisible())
			{
				Out.Add(Part);
				if (OutRoot)
				{
					*OutRoot = Parent;
				}
			}
		}
		return Out;
	}

	/** "Cooler 3/4" -> "3/4"; the "(3/4, closed)" of a "Carrying: ..." line -> "3/4"; else empty */
	FString CountOf(const FString& Text)
	{
		int32 Open = INDEX_NONE;
		if (Text.StartsWith(TEXT("Carrying:")))
		{
			Open = Text.Find(TEXT("("));
			const int32 Comma = Open == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			const int32 Close = Open == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			const int32 End = Comma != INDEX_NONE && (Close == INDEX_NONE || Comma < Close) ? Comma : Close;
			return Open != INDEX_NONE && End != INDEX_NONE ? Text.Mid(Open + 1, End - Open - 1).TrimStartAndEnd() : FString();
		}
		const int32 At = Text.Find(TEXT("Cooler "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return At == INDEX_NONE ? FString() : Text.Mid(At + 7).TrimStartAndEnd();
	}

	/** The HUD of Controller: the status line's cooler count and the Carrying line's count (empty when missing) */
	struct FHudCounts
	{
		FString Status;
		FString Carrying;
		FString All;
	};

	FHudCounts HudOf(const APlayerController* Controller)
	{
		FHudCounts Out;
		const TArray<FString> Status = ULureProgressionLibrary::GetPlaceholderStatusLines(Controller);
		const TArray<FString> Lines = ULureCatchLibrary::GetPlaceholderLines(Controller);
		Out.All = FString::Join(Status, TEXT(" | ")) + TEXT(" || ") + FString::Join(Lines, TEXT(" | "));
		if (Status.Num() > 0 && Status[0].Contains(TEXT("Cooler ")))
		{
			Out.Status = CountOf(Status[0]);
		}
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("Carrying:")))
			{
				Out.Carrying = CountOf(Line);
			}
		}
		return Out;
	}

	/** A player standing at the dock's center looking along +X */
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
			if (!Test.TestNotNull(TEXT("QA setup: player"), Player) || !Test.TestNotNull(TEXT("QA setup: hands"), Hands) || !Test.TestNotNull(TEXT("QA setup: use keys"), Use))
			{
				return false;
			}
			W.Tick(20);
			return true;
		}

		/** Server: a Bonefish of WeightKg from the hand into Cooler */
		bool PutIn(ALureCoolerActor* Cooler, int32 Seed, float WeightKg = 1.5f)
		{
			ALureFishItem* Fish = W.LandInHand(Player, LCT::MakeFish(TEXT("Bonefish"), 20, 1, WeightKg, Seed));
			return Fish && Cooler->AuthorityPutFishIn(Player, Fish);
		}
	};

	// =====================================================================================================================
	// 1. The lid's look follows its state (server)
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQALidPulseFollowsTuning, "Project.Catch.QA.Lid.PulseFollowsTuning", LCT::Flags)
	bool FCoolerQALidPulseFollowsTuning::RunTest(const FString& Parameters)
	{
		// Shipped 30, then 20 and 0 injected: the clack is data (DT_Catch LidPulsePitch), never the old 60 deg / 0.5 s hold.
		for (const float Pulse : { -1.0f, 20.0f, 0.0f })
		{
			FRig Rig;
			if (!Rig.Create(*this))
			{
				return false;
			}
			ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(Rig.W.World);
			FLureCatchRow Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
			if (Pulse >= 0.0f && Subsystem)
			{
				Tuning.LidPulsePitch = Pulse;
				Subsystem->SetTuning(Tuning);
			}
			const FLureCatchRow& Used = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
			const float Expected = Used.LidPulsePitch;
			TestEqual(FString::Printf(TEXT("QA setup: pulse %.0f in use"), Expected), Expected, Pulse >= 0.0f ? Pulse : 30.0f);
			for (const FName Row : { FName(TEXT("Starter")), FName(TEXT("Large")) })
			{
				ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(130.0f, Row == TEXT("Large") ? 120.0f : -120.0f, LCT::DockTop), 180.0f, Row);
				if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
				{
					return false;
				}
				Rig.W.Tick(5);
				const FString What = FString::Printf(TEXT("%s, pulse %.0f"), *Row.ToString(), Expected);
				TestTrue(What + TEXT(": in it goes (closed)"), Rig.PutIn(Cooler, 1000 + static_cast<int32>(Expected) + (Row == TEXT("Large") ? 1 : 0)));
				FLidTrace Trace;
				for (int32 Tick = 0; Tick < 60; ++Tick)
				{
					Rig.W.Tick(1);
					Trace.Sample(Cooler);
				}
				TestFalse(What + TEXT(": the lid state stays closed"), Trace.bEverOpen);
				TestTrue(FString::Printf(TEXT("%s: never below closed (min %.2f)"), *What, Trace.MinPitch), Trace.MinPitch >= -0.01f);
				TestTrue(FString::Printf(TEXT("%s: never above LidPulsePitch (max %.2f)"), *What, Trace.MaxPitch), Trace.MaxPitch <= Expected + 0.5f);
				if (Expected > 0.0f)
				{
					TestTrue(FString::Printf(TEXT("%s: the lid actually reaches the clack (max %.2f)"), *What, Trace.MaxPitch), Trace.MaxPitch >= 0.8f * Expected);
				}
				else
				{
					TestTrue(FString::Printf(TEXT("%s: pulse 0 = the lid never moves (max %.2f)"), *What, Trace.MaxPitch), Trace.MaxPitch <= 0.01f);
				}
				// Short: up for at most LidOpenTime (the spec says about 0.15 s), then shut for good.
				TestTrue(FString::Printf(TEXT("%s: up for %d ticks, at most LidOpenTime"), *What, Trace.TicksUp), Trace.TicksUp * LCT::Dt <= Used.LidOpenTime + 0.001f);
				TestEqual(What + TEXT(": shut at the end"), Cooler->GetLidPitch(), 0.0f, 0.01f);
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQALidToggleSettles, "Project.Catch.QA.Lid.ToggleMidSwingSettles", LCT::Flags)
	bool FCoolerQALidToggleSettles::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this))
		{
			return false;
		}
		const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(Rig.W.World);
		const int32 SettleTicks = FMath::CeilToInt(Tuning.LidOpenTime / LCT::Dt) + 2;
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		Rig.W.Tick(5);

		// Open, and close again 3 ticks into the swing: the lid turns back, never overshoots either end, and ends shut.
		TestTrue(TEXT("open"), Cooler->AuthoritySetLidOpen(true));
		Rig.W.Tick(3);
		const float Mid = Cooler->GetLidPitch();
		TestTrue(FString::Printf(TEXT("mid-swing (%.1f deg)"), Mid), Mid > 0.01f && Mid < Tuning.LidOpenPitch - 0.01f);
		TestTrue(TEXT("close mid-swing"), Cooler->AuthoritySetLidOpen(false));
		FLidTrace Back;
		float Previous = Mid;
		bool bMonotonic = true;
		for (int32 Tick = 0; Tick < SettleTicks; ++Tick)
		{
			Rig.W.Tick(1);
			Back.Sample(Cooler);
			bMonotonic &= Cooler->GetLidPitch() <= Previous + 0.01f;
			Previous = Cooler->GetLidPitch();
		}
		TestTrue(FString::Printf(TEXT("closing: only goes down from %.1f (max %.1f)"), Mid, Back.MaxPitch), bMonotonic && Back.MaxPitch <= Mid + 0.01f);
		TestTrue(FString::Printf(TEXT("closing: never below 0 (min %.2f)"), Back.MinPitch), Back.MinPitch >= -0.01f);
		TestEqual(TEXT("closed within LidOpenTime"), Cooler->GetLidPitch(), 0.0f, 0.01f);

		// Open, close, open within a few ticks: ends fully open, between the ends throughout.
		TestTrue(TEXT("open"), Cooler->AuthoritySetLidOpen(true));
		Rig.W.Tick(2);
		TestTrue(TEXT("close"), Cooler->AuthoritySetLidOpen(false));
		Rig.W.Tick(1);
		TestTrue(TEXT("open again"), Cooler->AuthoritySetLidOpen(true));
		FLidTrace Again;
		for (int32 Tick = 0; Tick < SettleTicks; ++Tick)
		{
			Rig.W.Tick(1);
			Again.Sample(Cooler);
		}
		TestTrue(FString::Printf(TEXT("between the ends (%.1f..%.1f)"), Again.MinPitch, Again.MaxPitch), Again.MinPitch >= -0.01f && Again.MaxPitch <= Tuning.LidOpenPitch + 0.01f);
		TestEqual(TEXT("open: LidOpenPitch"), Cooler->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);
		Rig.W.Tick(30);
		TestEqual(TEXT("... and stays there"), Cooler->GetLidPitch(), Tuning.LidOpenPitch, 0.01f);

		// Close, and put a fish in while the lid is still swinging down (the lid state is closed: a clack): still ends shut,
		// never reads open, never above where it was.
		TestTrue(TEXT("close"), Cooler->AuthoritySetLidOpen(false));
		Rig.W.Tick(2);
		const float Closing = Cooler->GetLidPitch();
		TestTrue(TEXT("put a fish in while it closes"), Rig.PutIn(Cooler, 1101));
		FLidTrace Clack;
		for (int32 Tick = 0; Tick < SettleTicks + 20; ++Tick)
		{
			Rig.W.Tick(1);
			Clack.Sample(Cooler);
		}
		TestFalse(TEXT("... the lid state stays closed"), Clack.bEverOpen);
		TestTrue(FString::Printf(TEXT("... never above the closing lid or open (max %.1f, was %.1f)"), Clack.MaxPitch, Closing),
			Clack.MaxPitch <= FMath::Max(Closing, Tuning.LidPulsePitch) + 0.5f);
		TestEqual(TEXT("... and ends shut"), Cooler->GetLidPitch(), 0.0f, 0.01f);
		LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
		Rig.W.Tick(2);
		TestEqual(TEXT("... the prompt says Open"), LCT::VerbName(Rig.Use->ResolveInteraction(KeyE).Verb), LCT::VerbName(ELureInteractVerb::OpenCooler));

		// Two clacks in consecutive ticks: still at most one pulse high, shut at the end.
		TestTrue(TEXT("first fish"), Rig.PutIn(Cooler, 1102));
		Rig.W.Tick(1);
		TestTrue(TEXT("second fish"), Rig.PutIn(Cooler, 1103));
		FLidTrace Double;
		for (int32 Tick = 0; Tick < SettleTicks + 20; ++Tick)
		{
			Rig.W.Tick(1);
			Double.Sample(Cooler);
		}
		TestTrue(FString::Printf(TEXT("double clack: at most LidPulsePitch (max %.1f)"), Double.MaxPitch), !Double.bEverOpen && Double.MaxPitch <= Tuning.LidPulsePitch + 0.5f);
		TestEqual(TEXT("double clack: shut at the end"), Cooler->GetLidPitch(), 0.0f, 0.01f);
		return true;
	}

	// =====================================================================================================================
	// 2. The contents display: contracts (slot values are being re-solved; never hard-coded here)
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQADisplaySlotContracts, "Project.Catch.QA.Display.SlotContracts", LCT::Flags)
	bool FCoolerQADisplaySlotContracts::RunTest(const FString& Parameters)
	{
		const TStrongObjectPtr<UDataTable> Display = LCT::Shipped(*this, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
		const TStrongObjectPtr<UDataTable> Coolers = LCT::Shipped(*this, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
		if (!Display.IsValid() || !Coolers.IsValid())
		{
			return false;
		}
		TestTrue(TEXT("a Starter display row exists"), Display->FindRow<FLureCoolerDisplayRow>(TEXT("Starter"), TEXT("QA"), false) != nullptr);
		int32 Rows = 0;
		for (const TPair<FName, uint8*>& Pair : Display->GetRowMap())
		{
			++Rows;
			const FLureCoolerDisplayRow& Row = *reinterpret_cast<const FLureCoolerDisplayRow*>(Pair.Value);
			const FString Name = Pair.Key.ToString();
			const FCoolerRow* Cooler = Coolers->FindRow<FCoolerRow>(Pair.Key, TEXT("QA"), false);
			if (!TestNotNull(Name + TEXT(": names a DT_Cooler row"), Cooler))
			{
				continue;
			}
			// One slot per fish the cooler holds at most (Large reuses the starter's 4 until it has its own model); the model's
			// own row shows every fish it holds.
			TestTrue(FString::Printf(TEXT("%s: 1..capacity slots (%d, capacity %d)"), *Name, Row.Slots.Num(), Cooler->Slots), Row.Slots.Num() >= 1 && Row.Slots.Num() <= Cooler->Slots);
			if (Pair.Key == TEXT("Starter"))
			{
				TestEqual(TEXT("Starter: one slot per capacity"), Row.Slots.Num(), Cooler->Slots);
			}
			TestTrue(FString::Printf(TEXT("%s: 0 < MaxFishScale <= 1 (%.3f; 1.0 = four fish fit under the lid)"), *Name, Row.MaxFishScale), Row.MaxFishScale > 0.0f && Row.MaxFishScale <= 1.0f + 1.0e-4f);
			FString Problem;
			const bool bValid = Row.Validate(Problem);
			TestTrue(Name + TEXT(": its own validator passes (") + Problem + TEXT(")"), bValid);
			for (int32 Slot = 0; Slot < Row.Slots.Num(); ++Slot)
			{
				const FLureCoolerDisplaySlot& S = Row.Slots[Slot];
				const FString At = FString::Printf(TEXT("%s slot %d (%s, %s)"), *Name, Slot, *S.Location.ToCompactString(), *S.Rotation.ToCompactString());
				TestTrue(At + TEXT(": the bed is on or above the liner floor (Contents point)"), S.Location.Z >= -0.01);
				TestTrue(At + TEXT(": slot 0 is the bottom of the pile (no bed below it)"), S.Location.Z >= Row.Slots[0].Location.Z - 0.01);
				TestTrue(At + TEXT(": lies on its side (pitch 0, roll +-90)"), FMath::IsNearlyZero(S.Rotation.Pitch, 0.5) && FMath::IsNearlyEqual(FMath::Abs(S.Rotation.Roll), 90.0, 0.5));
				for (int32 Other = 0; Other < Slot; ++Other)
				{
					TestTrue(FString::Printf(TEXT("%s: not the same spot as slot %d"), *At, Other), !S.Location.Equals(Row.Slots[Other].Location, 0.5));
				}
				// A lone fish at the cap: the transform's scale is the cap, it sits LieOffsetCm x scale above its bed.
				const FTransform Place = ALureCoolerActor::GetDisplayFishTransform(Row, Slot, Row.MaxFishScale);
				TestTrue(At + TEXT(": scale = the shown scale"), FMath::IsNearlyEqual(static_cast<float>(Place.GetScale3D().X), Row.MaxFishScale, 1.0e-4f));
				TestEqual(At + TEXT(": origin = bed + LieOffsetCm x scale"), Place.GetLocation().Z, S.Location.Z + Row.LieOffsetCm * Row.MaxFishScale, 0.01);
			}
			// A bad slot index is identity (documented), not a crash or a garbage place.
			for (const int32 Bad : { -1, Row.Slots.Num(), Row.Slots.Num() + 5 })
			{
				TestTrue(FString::Printf(TEXT("%s: slot %d = identity"), *Name, Bad), ALureCoolerActor::GetDisplayFishTransform(Row, Bad, 0.7f).Equals(FTransform::Identity, 1.0e-4));
			}
		}
		TestTrue(TEXT("rows checked"), Rows >= 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQADisplayScaleCap, "Project.Catch.QA.Display.ScaleCapMonotonic", LCT::Flags)
	bool FCoolerQADisplayScaleCap::RunTest(const FString& Parameters)
	{
		const TStrongObjectPtr<UDataTable> Visual = LCT::Shipped(*this, FFishVisualRow::StaticStruct(), TEXT("DT_FishVisual.json"));
		const TStrongObjectPtr<UDataTable> Display = LCT::Shipped(*this, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
		const FFishVisualRow* VisualRow = Visual.IsValid() ? Visual->FindRow<FFishVisualRow>(TEXT("Default"), TEXT("QA"), false) : nullptr;
		const FLureCoolerDisplayRow* Shipped = Display.IsValid() ? Display->FindRow<FLureCoolerDisplayRow>(TEXT("Starter"), TEXT("QA"), false) : nullptr;
		if (!TestNotNull(TEXT("QA setup: DT_FishVisual Default"), VisualRow) || !TestNotNull(TEXT("QA setup: DT_CoolerDisplay Starter"), Shipped))
		{
			return false;
		}
		for (const float Cap : { Shipped->MaxFishScale, 0.6f, 0.3f })
		{
			FLureCoolerDisplayRow Row = *Shipped;
			Row.MaxFishScale = Cap;
			const float Reference = 1.5f;
			float Previous = 0.0f;
			bool bMonotonic = true;
			bool bOracle = true;
			FString FirstBad;
			for (float Weight = 0.01f; Weight <= 60.0f; Weight *= 1.25f)
			{
				const float Scale = ALureCoolerActor::GetDisplayFishScale(Weight, Reference, *VisualRow, Row);
				// Spec: (Weight / ReferenceWeight)^(1/3) (the fight fish's weight scale, with its clamps), at most MaxFishScale.
				const float Oracle = FMath::Min(Cap, FFightFishVisual::WeightScale(Weight, Reference, *VisualRow));
				const float Raw = FMath::Min(Cap, FMath::Clamp(FMath::Pow(Weight / Reference, 1.0f / 3.0f), VisualRow->MinScale, VisualRow->MaxScale));
				if (!FMath::IsNearlyEqual(Scale, Oracle, 1.0e-4f) || !FMath::IsNearlyEqual(Scale, Raw, 1.0e-3f) || Scale > Cap + 1.0e-5f)
				{
					bOracle = false;
					FirstBad = FirstBad.IsEmpty() ? FString::Printf(TEXT("%.3f kg -> %.4f (oracle %.4f)"), Weight, Scale, Raw) : FirstBad;
				}
				bMonotonic &= Scale >= Previous - 1.0e-5f;
				Previous = Scale;
			}
			TestTrue(FString::Printf(TEXT("cap %.2f: min(cap, (W/Ref)^(1/3)) %s"), Cap, *FirstBad), bOracle);
			TestTrue(FString::Printf(TEXT("cap %.2f: never smaller for a heavier fish"), Cap), bMonotonic);
			TestEqual(FString::Printf(TEXT("cap %.2f: a huge fish shows at the cap"), Cap), ALureCoolerActor::GetDisplayFishScale(1000.0f, Reference, *VisualRow, Row), Cap, 1.0e-4f);
			// Bad weights: finite, positive, never above the cap.
			for (const float Bad : { 0.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
			{
				const float Scale = ALureCoolerActor::GetDisplayFishScale(Bad, Reference, *VisualRow, Row);
				TestTrue(FString::Printf(TEXT("cap %.2f, weight %f: finite, > 0, <= cap (%f)"), Cap, Bad, Scale), FMath::IsFinite(Scale) && Scale > 0.0f && Scale <= Cap + 1.0e-5f);
			}
			const float BadReference = ALureCoolerActor::GetDisplayFishScale(2.0f, 0.0f, *VisualRow, Row);
			TestTrue(FString::Printf(TEXT("cap %.2f, reference 0: finite, > 0, <= cap (%f)"), Cap, BadReference), FMath::IsFinite(BadReference) && BadReference > 0.0f && BadReference <= Cap + 1.0e-5f);
		}
		return true;
	}

	/** Every shown fish of an open Cooler lies inside its box, on or above its floor and the liner floor, at distinct spots */
	void CheckShownInsideBody(FAutomationTestBase& Test, const ALureCoolerActor* Cooler, const FLureCoolerDisplayRow& Row, int32 ExpectedShown, const FString& What)
	{
		const USceneComponent* Root = nullptr;
		const TArray<const UPrimitiveComponent*> Shown = ShownFish(Cooler, &Root);
		Test.TestEqual(What + TEXT(": fish drawn"), Shown.Num(), ExpectedShown);
		Test.TestEqual(What + TEXT(": GetNumDisplayedFish"), Cooler->GetNumDisplayedFish(), ExpectedShown);
		if (!Root)
		{
			return;
		}
		const FTransform Actor = Cooler->GetActorTransform();
		const FVector Center = Cooler->GetBoxCenter();
		const FVector Half = Cooler->GetBoxHalfExtent();
		const double LinerFloor = Actor.InverseTransformPosition(Root->GetComponentLocation()).Z;
		Test.TestTrue(FString::Printf(TEXT("%s: the Contents point is inside the box, above the floor (z %.2f)"), *What, LinerFloor), LinerFloor >= -0.01 && LinerFloor <= Center.Z + Half.Z);
		TArray<FVector> Spots;
		for (const UPrimitiveComponent* Fish : Shown)
		{
			const FVector Local = Actor.InverseTransformPosition(Fish->GetComponentLocation());
			const float Scale = static_cast<float>(Fish->GetRelativeScale3D().X);
			const double Bottom = Local.Z - Row.LieOffsetCm * Scale;
			const double Top = Local.Z + Row.LieOffsetCm * Scale;
			const FString Where = FString::Printf(TEXT("%s: fish at %s (scale %.3f, bottom %.2f, top %.2f; box %s +- %s, liner floor %.2f)"), *What, *Local.ToCompactString(), Scale,
				Bottom, Top, *Center.ToCompactString(), *Half.ToCompactString(), LinerFloor);
			Test.TestTrue(Where + TEXT(": at most MaxFishScale"), Scale > 0.0f && Scale <= Row.MaxFishScale + 1.0e-4f);
			Test.TestTrue(Where + TEXT(": inside the walls"), FMath::Abs(Local.X - Center.X) <= Half.X && FMath::Abs(Local.Y - Center.Y) <= Half.Y);
			Test.TestTrue(Where + TEXT(": not under the liner floor"), Bottom >= LinerFloor - 0.05);
			Test.TestTrue(Where + TEXT(": not under the cooler's floor"), Bottom >= -0.01);
			Test.TestTrue(Where + TEXT(": under the top of the cooler"), Top <= Center.Z + Half.Z + 0.01);
			Test.TestFalse(Where + TEXT(": never hidden in game"), Fish->bHiddenInGame);
			for (const FVector& Other : Spots)
			{
				Test.TestTrue(Where + TEXT(": its own slot"), !Local.Equals(Other, 0.5));
			}
			Spots.Add(Local);
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQADisplayInsideBody, "Project.Catch.QA.Display.ShownFishInsideBody", LCT::Flags)
	bool FCoolerQADisplayInsideBody::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FFishSpeciesRow* Bonefish = W.FishTables.Species.IsValid() ? W.FishTables.Species->FindRow<FFishSpeciesRow>(TEXT("Bonefish"), TEXT("QA"), false) : nullptr;
		if (!TestNotNull(TEXT("QA setup: Bonefish species row"), Bonefish))
		{
			return false;
		}
		struct FCase
		{
			FName Row;
			FVector Floor;
			float Yaw;
			TArray<float> Weights;
			FString What;
		};
		TArray<FCase> Cases;
		// The playtest case: a lone 5.5 kg Bonefish (heavier than its reference: shown at the cap).
		Cases.Add({ TEXT("Starter"), FVector(130.0f, -200.0f, LCT::DockTop), 180.0f, { 5.5f }, TEXT("lone 5.5 kg Bonefish") });
		// A lone tiny one (a quarter of the lightest roll) and a lone monster (4x the heaviest roll, any size modifier).
		Cases.Add({ TEXT("Starter"), FVector(130.0f, 0.0f, LCT::DockTop), 180.0f, { Bonefish->WeightMin * 0.25f }, TEXT("lone tiny Bonefish") });
		Cases.Add({ TEXT("Starter"), FVector(130.0f, 200.0f, LCT::DockTop), 180.0f, { Bonefish->WeightMax * 4.0f }, TEXT("lone monster Bonefish") });
		// Full coolers of monsters: turned (the display follows the cooler) and standing on a raised box.
		Cases.Add({ TEXT("Starter"), FVector(-150.0f, -200.0f, LCT::DockTop), 37.0f, { 18.0f, 18.0f, 18.0f, 18.0f }, TEXT("full Starter, turned 37 deg") });
		Cases.Add({ TEXT("Large"), FVector(-150.0f, 200.0f, LCT::DockTop), -120.0f, { 18.0f, 0.2f, 18.0f, 0.2f, 18.0f, 18.0f, 0.2f, 18.0f }, TEXT("full Large, turned -120 deg") });
		W.AddBox(FVector(-350.0f, 0.0f, LCT::DockTop + 30.0f), FVector(60.0f, 60.0f, 30.0f));
		Cases.Add({ TEXT("Starter"), FVector(-350.0f, 0.0f, LCT::DockTop + 60.0f), 90.0f, { 5.5f, 5.5f }, TEXT("two on a raised box") });

		int32 Seed = 1200;
		for (const FCase& Case : Cases)
		{
			ALureCoolerActor* Cooler = W.SpawnCooler(Case.Floor, Case.Yaw, Case.Row);
			if (!TestNotNull(Case.What + TEXT(": QA setup: cooler"), Cooler))
			{
				continue;
			}
			for (const float Weight : Case.Weights)
			{
				TestTrue(Case.What + TEXT(": QA setup: a fish in"), Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 20, 1, Weight, ++Seed), W.Now())));
			}
			TestTrue(Case.What + TEXT(": open"), Cooler->AuthoritySetLidOpen(true));
			W.Tick(30);
			const FLureCoolerDisplayRow Row = ULureCatchSubsystem::Get(W.World)->GetCoolerDisplayRow(Cooler->GetCoolerId());
			TestTrue(FString::Printf(TEXT("%s: the cooler stands on its floor (z %.1f)"), *Case.What, Cooler->GetActorLocation().Z), FMath::IsNearlyEqual(Cooler->GetActorLocation().Z, Case.Floor.Z, 1.0));
			CheckShownInsideBody(*this, Cooler, Row, FMath::Min(Case.Weights.Num(), Row.Slots.Num()), Case.What);
			if (Case.Weights.Num() == 1)
			{
				const TArray<const UPrimitiveComponent*> Shown = ShownFish(Cooler);
				if (Shown.Num() == 1)
				{
					const float Expected = FMath::Min(Row.MaxFishScale, FMath::Pow(Case.Weights[0] / Bonefish->ReferenceWeight, 1.0f / 3.0f));
					TestEqual(Case.What + TEXT(": shown at min(cap, (W/Ref)^(1/3))"), static_cast<float>(Shown[0]->GetRelativeScale3D().X), Expected, 2.0e-3f);
				}
			}
			// Taking the top fish out re-seats the rest; the pile stays inside.
			if (Case.Weights.Num() > 1)
			{
				FLureCaughtFish Out;
				TestTrue(Case.What + TEXT(": take the top one out"), Cooler->GetStorage()->RemoveLastFish(Out));
				W.Tick(5);
				CheckShownInsideBody(*this, Cooler, Row, FMath::Min(Case.Weights.Num() - 1, Row.Slots.Num()), Case.What + TEXT(", one out"));
			}
			// Closed: nothing shows.
			TestTrue(Case.What + TEXT(": close"), Cooler->AuthoritySetLidOpen(false));
			W.Tick(30);
			TestEqual(Case.What + TEXT(": closed, nothing drawn"), ShownFish(Cooler).Num(), 0);
		}
		return true;
	}

	// =====================================================================================================================
	// 3. Focus: what the view ray hits wins
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQAFocusRayGeometry, "Project.Catch.QA.Focus.RayGeometry", LCT::Flags)
	bool FCoolerQAFocusRayGeometry::RunTest(const FString& Parameters)
	{
		const FVector Eye = FVector::ZeroVector;
		const FVector Ahead = FVector::ForwardVector;
		const FVector Cube(10.0);
		// Boxes (hand-computed entry distances)
		TestEqual(TEXT("box ahead: enters at 90"), ILureInteractable::RayToBox(Eye, Ahead, FTransform(FVector(100.0, 0.0, 0.0)), Cube), 90.0, 1.0e-3);
		TestEqual(TEXT("box ahead, off-center hit: enters at 90"), ILureInteractable::RayToBox(Eye, Ahead, FTransform(FVector(100.0, 9.0, -9.0)), Cube), 90.0, 1.0e-3);
		TestEqual(TEXT("from inside: 0"), ILureInteractable::RayToBox(Eye, Ahead, FTransform(FVector(5.0, 0.0, 0.0)), Cube), 0.0, 1.0e-3);
		TestTrue(TEXT("beside: miss"), ILureInteractable::RayToBox(Eye, Ahead, FTransform(FVector(100.0, 10.5, 0.0)), Cube) < 0.0);
		TestTrue(TEXT("behind the eye: miss"), ILureInteractable::RayToBox(Eye, Ahead, FTransform(FVector(-100.0, 0.0, 0.0)), Cube) < 0.0);
		TestEqual(TEXT("turned 45 deg: enters at its corner (100 - 10 sqrt 2)"),
			ILureInteractable::RayToBox(Eye, Ahead, FTransform(FRotator(0.0, 45.0, 0.0), FVector(100.0, 0.0, 0.0)), Cube), 100.0 - 10.0 * UE_DOUBLE_SQRT_2, 1.0e-3);
		TestEqual(TEXT("scale is ignored"), ILureInteractable::RayToBox(Eye, Ahead, FTransform(FRotator::ZeroRotator, FVector(100.0, 0.0, 0.0), FVector(3.0)), Cube), 90.0, 1.0e-3);
		const FVector Down = FVector(1.0, 0.0, -1.0).GetSafeNormal();
		TestEqual(TEXT("looking 45 deg down onto a box top"), ILureInteractable::RayToBox(FVector(0.0, 0.0, 110.0), Down, FTransform(FVector(100.0, 0.0, 0.0)), Cube),
			100.0 * UE_DOUBLE_SQRT_2, 1.0e-3);
		// Spheres
		TestEqual(TEXT("sphere ahead: enters at 90"), ILureInteractable::RayToSphere(Eye, Ahead, FVector(100.0, 0.0, 0.0), 10.0f), 90.0, 1.0e-3);
		TestEqual(TEXT("sphere from inside: 0"), ILureInteractable::RayToSphere(Eye, Ahead, FVector(3.0, 0.0, 0.0), 10.0f), 0.0, 1.0e-3);
		TestTrue(TEXT("sphere beside: miss"), ILureInteractable::RayToSphere(Eye, Ahead, FVector(100.0, 10.5, 0.0), 10.0f) < 0.0);
		TestTrue(TEXT("sphere behind: miss"), ILureInteractable::RayToSphere(Eye, Ahead, FVector(-100.0, 0.0, 0.0), 10.0f) < 0.0);
		TestTrue(TEXT("radius 0: never hit"), ILureInteractable::RayToSphere(Eye, Ahead, FVector(100.0, 0.0, 0.0), 0.0f) < 0.0);
		TestEqual(TEXT("off-center chord: 100 - sqrt(100 - 36)"), ILureInteractable::RayToSphere(Eye, Ahead, FVector(100.0, 6.0, 0.0), 10.0f), 92.0, 1.0e-3);
		// Angles
		TestEqual(TEXT("angle through a box: 0"), ILureInteractable::AngleToBox(Eye, Ahead, FTransform(FVector(100.0, 5.0, 0.0)), Cube), 0.0f, 1.0e-3f);
		TestEqual(TEXT("angle to a point box at 45 deg"), ILureInteractable::AngleToBox(Eye, Ahead, FTransform(FVector(100.0, 100.0, 0.0)), FVector(0.001)), 45.0f, 0.05f);
		TestEqual(TEXT("angle through a sphere: 0"), ILureInteractable::AngleToSphere(Eye, Ahead, FVector(100.0, 5.0, 0.0), 10.0f), 0.0f, 1.0e-3f);
		TestEqual(TEXT("angle to a point at 45 deg"), ILureInteractable::AngleToSphere(Eye, Ahead, FVector(100.0, 100.0, 0.0), 0.0f), 45.0f, 0.05f);
		const float Near = ILureInteractable::AngleToSphere(Eye, Ahead, FVector(100.0, 100.0, 0.0), 10.0f);
		TestTrue(FString::Printf(TEXT("a sphere's size makes its angle smaller than its center's (%.2f)"), Near), Near < 45.0f && Near > 40.0f);
		TestTrue(TEXT("behind the eye: a large angle"), ILureInteractable::AngleToSphere(Eye, Ahead, FVector(-100.0, 0.0, 0.0), 10.0f) > 90.0f);
		return true;
	}

	/** Loose fish (lying on the dock) and a closed cooler in the world of Rig; FishFirst = the spawn (registration) order */
	struct FScene
	{
		ALureCoolerActor* Cooler = nullptr;
		ALureFishItem* Fish = nullptr;

		bool Create(FAutomationTestBase& Test, FRig& Rig, const FVector& CoolerAt, const FVector& FishAt, bool bFishFirst, int32 Seed)
		{
			auto MakeFish = [&]()
			{
				Fish = ALureFishItem::SpawnFish(Rig.W.World, FLureCaughtFish::Landed(LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.5f, Seed), Rig.W.Now()), FTransform(FishAt));
			};
			if (bFishFirst)
			{
				MakeFish();
			}
			Cooler = Rig.W.SpawnCooler(CoolerAt, 180.0f);
			if (!bFishFirst)
			{
				MakeFish();
			}
			Rig.W.Tick(10);
			return Test.TestNotNull(TEXT("QA setup: cooler"), Cooler) && Test.TestNotNull(TEXT("QA setup: loose fish"), Fish);
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQAFocusLookedAtWins, "Project.Catch.QA.Focus.LookedAtTargetWins", LCT::Flags)
	bool FCoolerQAFocusLookedAtWins::RunTest(const FString& Parameters)
	{
		for (const bool bFishFirst : { false, true })
		{
			const FString Order = bFishFirst ? TEXT("fish spawned first") : TEXT("cooler spawned first");
			// Side by side, both in reach: whichever you look at.
			{
				FRig Rig;
				FScene Scene;
				if (!Rig.Create(*this) || !Scene.Create(*this, Rig, FVector(110.0f, 45.0f, LCT::DockTop), FVector(110.0f, -45.0f, LCT::DockTop), bFishFirst, 1300))
				{
					return false;
				}
				const ILureInteractable* CoolerUse = Scene.Cooler;
				const ILureInteractable* FishUse = Scene.Fish;
				TestTrue(Order + TEXT(": QA setup: both in reach"), CoolerUse->IsInInteractionRange(Rig.Player) && FishUse->IsInInteractionRange(Rig.Player));
				for (int32 Round = 0; Round < 2; ++Round) // back and forth twice: no stickiness
				{
					LCT::LookAt(Rig.Player, Scene.Fish->GetInteractionLocation());
					Rig.W.Tick(2);
					TestTrue(Order + TEXT(": looking at the fish: the fish"), Rig.Use->FindFocusedInteractable() == Scene.Fish);
					TestEqual(Order + TEXT(": ... E grabs it"), LCT::VerbName(Rig.Use->ResolveInteraction(KeyE).Verb), LCT::VerbName(ELureInteractVerb::GrabFish));
					LCT::LookAt(Rig.Player, Scene.Cooler->GetInteractionLocation());
					Rig.W.Tick(2);
					TestTrue(Order + TEXT(": looking at the cooler: the cooler"), Rig.Use->FindFocusedInteractable() == Scene.Cooler);
					TestEqual(Order + TEXT(": ... E opens it"), LCT::VerbName(Rig.Use->ResolveInteraction(KeyE).Verb), LCT::VerbName(ELureInteractVerb::OpenCooler));
				}
				// E on the fish really grabs it and leaves the cooler alone.
				LCT::LookAt(Rig.Player, Scene.Fish->GetInteractionLocation());
				Rig.W.Tick(2);
				TestTrue(Order + TEXT(": E"), Rig.Use->PressKey(KeyE));
				Rig.W.Tick(2);
				TestTrue(Order + TEXT(": the fish is in hand, the cooler still closed and empty"), Rig.Hands->GetHeldFish() == Scene.Fish && !Scene.Cooler->IsLidOpen() && Scene.Cooler->GetNumFish() == 0);
			}
			// The fish lies beyond the cooler: looking down past the cooler's top at the fish, the ray misses the (nearer) cooler
			// box and hits the fish; looking at the cooler's front, the cooler's box is the first hit.
			{
				FRig Rig;
				FScene Scene;
				if (!Rig.Create(*this) || !Scene.Create(*this, Rig, FVector(80.0f, 0.0f, LCT::DockTop), FVector(160.0f, 0.0f, LCT::DockTop), bFishFirst, 1310))
				{
					return false;
				}
				const ILureInteractable* CoolerUse = Scene.Cooler;
				const ILureInteractable* FishUse = Scene.Fish;
				TestTrue(Order + TEXT(": QA setup (beyond): both in reach"), CoolerUse->IsInInteractionRange(Rig.Player) && FishUse->IsInInteractionRange(Rig.Player));
				LCT::LookAt(Rig.Player, Scene.Fish->GetInteractionLocation());
				Rig.W.Tick(2);
				const FVector Eye = Rig.Player->GetPawnViewLocation();
				const FVector View = Rig.Player->GetViewRotation().Vector();
				const double CoolerHit = CoolerUse->GetFocusHitDistance(Eye, View);
				const double FishHit = FishUse->GetFocusHitDistance(Eye, View);
				TestTrue(FString::Printf(TEXT("%s: QA setup (beyond): the ray clears the cooler (%.1f) and hits the fish (%.1f)"), *Order, CoolerHit, FishHit), CoolerHit < 0.0 && FishHit >= 0.0);
				TestTrue(Order + TEXT(": looking at the fish beyond the cooler: the fish"), Rig.Use->FindFocusedInteractable() == Scene.Fish);
				const FVector Front = Scene.Cooler->GetActorLocation() + Scene.Cooler->GetActorForwardVector() * (Scene.Cooler->GetBoxHalfExtent().X - 2.0f) + FVector(0.0f, 0.0f, Scene.Cooler->GetBoxHalfExtent().Z);
				LCT::LookAt(Rig.Player, Front);
				Rig.W.Tick(2);
				const FVector Eye2 = Rig.Player->GetPawnViewLocation();
				const FVector View2 = Rig.Player->GetViewRotation().Vector();
				TestTrue(Order + TEXT(": QA setup (front): the ray hits the cooler's box"), CoolerUse->GetFocusHitDistance(Eye2, View2) >= 0.0);
				TestTrue(Order + TEXT(": looking at the cooler's front: the cooler"), Rig.Use->FindFocusedInteractable() == Scene.Cooler);
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQAFocusCarriedNeverFocused, "Project.Catch.QA.Focus.CarriedCoolerNeverFocused", LCT::Flags)
	bool FCoolerQAFocusCarriedNeverFocused::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Friend = Rig.W.SpawnPlayer(*this, FVector(90.0f, 60.0f, LCT::DockTop), /*bLocal*/ false);
		ALureCoolerActor* Cooler = Rig.W.SpawnCooler(FVector(110.0f, 0.0f, LCT::DockTop), 180.0f);
		ALureFishItem* Loose = ALureFishItem::SpawnFish(Rig.W.World, FLureCaughtFish::Landed(LCT::MakeFish(TEXT("CoralSnapper"), 20, 1, 1.5f, 1320), Rig.W.Now()),
			FTransform(FVector(100.0f, -60.0f, LCT::DockTop)));
		if (!TestNotNull(TEXT("QA setup: friend"), Friend) || !TestNotNull(TEXT("QA setup: cooler"), Cooler) || !TestNotNull(TEXT("QA setup: loose fish"), Loose))
		{
			return false;
		}
		Rig.W.Tick(10);
		LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
		Rig.W.Tick(2);
		TestTrue(TEXT("QA setup: standing, the cooler takes the focus"), Rig.Use->FindFocusedInteractable() == Cooler);
		TestTrue(TEXT("the friend picks it up"), Cooler->AuthorityPickUp(Friend));
		Rig.W.Tick(5);
		const ILureInteractable* CoolerUse = Cooler;
		TestTrue(TEXT("QA setup: the carried cooler is still in my reach"), CoolerUse->IsInInteractionRange(Rig.Player));
		LCT::LookAt(Rig.Player, Cooler->GetInteractionLocation());
		Rig.W.Tick(2);
		TestTrue(TEXT("carried by the friend: no verb for me"), !CoolerUse->GetInteraction(Rig.Player, KeyE).HasVerb() && !CoolerUse->GetInteraction(Rig.Player, KeyF).HasVerb());
		TestTrue(TEXT("carried by the friend: never my focus"), Rig.Use->FindFocusedInteractable() != Cooler);
		const FLureResolvedInteraction E = Rig.Use->ResolveInteraction(KeyE);
		TestTrue(TEXT("... E does nothing to it"), E.Target != Cooler);
		// The server refuses a stale verb on it too.
		TestFalse(TEXT("... a stale Open is refused"), Rig.Use->TryInteract(Cooler, KeyE, ELureInteractVerb::OpenCooler));
		TestTrue(TEXT("... it stays in the friend's hands, closed"), !Cooler->IsFree() && !Cooler->IsLidOpen());
		return true;
	}

	// =====================================================================================================================
	// 4. The HUD's cooler count follows the cooler you carry
	// =====================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQAHudFollowsCarried, "Project.Catch.QA.Hud.CoolerCountFollowsCarried", LCT::Flags)
	bool FCoolerQAHudFollowsCarried::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Friend = Rig.W.SpawnPlayer(*this, FVector(30.0f, -230.0f, LCT::DockTop), /*bLocal*/ false);
		ALurePlayerCharacter* Newcomer = Rig.W.SpawnPlayer(*this, FVector(30.0f, 230.0f, LCT::DockTop), /*bLocal*/ false);
		APlayerController* MyPC = LCT::ControllerOf(Rig.Player);
		APlayerController* FriendPC = LCT::ControllerOf(Friend);
		APlayerController* NewPC = LCT::ControllerOf(Newcomer);
		if (!TestNotNull(TEXT("QA setup: friend"), Friend) || !TestNotNull(TEXT("QA setup: newcomer"), Newcomer) || !TestNotNull(TEXT("QA setup: my controller"), MyPC)
			|| !TestNotNull(TEXT("QA setup: friend's controller"), FriendPC) || !TestNotNull(TEXT("QA setup: newcomer's controller"), NewPC))
		{
			return false;
		}
		// Every put-down goes back to these spots (AuthorityPutDownAt), so each pick-up starts in everyone's reach.
		const FVector MineSpot(110.0f, -120.0f, LCT::DockTop);
		const FVector TheirSpot(110.0f, 120.0f, LCT::DockTop);
		ALureCoolerActor* Mine = Rig.W.SpawnCooler(MineSpot, 180.0f, TEXT("Starter"), Rig.Player->GetPlayerState());
		ALureCoolerActor* Theirs = Rig.W.SpawnCooler(TheirSpot, 180.0f, TEXT("Large"), Friend->GetPlayerState());
		if (!TestNotNull(TEXT("QA setup: my cooler"), Mine) || !TestNotNull(TEXT("QA setup: their cooler"), Theirs))
		{
			return false;
		}
		int32 Seed = 1400;
		auto Add = [&](ALureCoolerActor* Cooler, int32 Count)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 20, 1, 1.5f, ++Seed), Rig.W.Now()));
			}
		};
		Add(Mine, 2);
		Add(Theirs, 3);
		Rig.W.Tick(5);
		auto Expect = [&](APlayerController* PC, const TCHAR* Status, const TCHAR* Carrying, const FString& What)
		{
			const FHudCounts Hud = HudOf(PC);
			TestEqual(FString::Printf(TEXT("%s: the status count (%s)"), *What, *Hud.All), Hud.Status, FString(Status));
			TestEqual(FString::Printf(TEXT("%s: the Carrying count (%s)"), *What, *Hud.All), Hud.Carrying, FString(Carrying));
			TestEqual(What + TEXT(": GetCoolerStatus"), ULureCatchLibrary::GetCoolerStatus(PC), FString(Status).IsEmpty() ? FString() : FString(TEXT("Cooler ")) + Status);
		};

		Expect(MyPC, TEXT("2/4"), TEXT(""), TEXT("hands empty: my own"));
		Expect(FriendPC, TEXT("3/8"), TEXT(""), TEXT("friend, hands empty: their own"));
		Expect(NewPC, TEXT(""), TEXT(""), TEXT("no cooler of my own, hands empty: no count"));

		// I carry my own.
		TestTrue(TEXT("I pick up mine"), Mine->AuthorityPickUp(Rig.Player));
		Rig.W.Tick(2);
		Expect(MyPC, TEXT("2/4"), TEXT("2/4"), TEXT("carrying mine"));
		Mine->AuthorityPutDownAt(MineSpot, 180.0f);
		Rig.W.Tick(2);

		// I carry the friend's Large cooler: its count everywhere on my screen, and it follows the storage live.
		TestTrue(TEXT("I pick up theirs"), Theirs->AuthorityPickUp(Rig.Player));
		Rig.W.Tick(2);
		Expect(MyPC, TEXT("3/8"), TEXT("3/8"), TEXT("carrying the friend's Large"));
		TestFalse(TEXT("... my own 2/4 is nowhere on my screen"), HudOf(MyPC).All.Contains(TEXT("Cooler 2/4")) || HudOf(MyPC).All.Contains(TEXT("(2/4")));
		Expect(FriendPC, TEXT("3/8"), TEXT(""), TEXT("friend while I carry theirs: their own"));
		Add(Theirs, 1);
		Rig.W.Tick(2);
		Expect(MyPC, TEXT("4/8"), TEXT("4/8"), TEXT("a fish added while I carry it: live"));
		FLureCaughtFish Out;
		TestTrue(TEXT("QA setup: one out"), Theirs->GetStorage()->RemoveLastFish(Out) && Theirs->GetStorage()->RemoveLastFish(Out));
		Rig.W.Tick(2);
		Expect(MyPC, TEXT("2/8"), TEXT("2/8"), TEXT("two out while I carry it: live"));
		Theirs->AuthorityPutDownAt(TheirSpot, 180.0f);
		Rig.W.Tick(2);
		Expect(MyPC, TEXT("2/4"), TEXT(""), TEXT("put down: my own again"));

		// The friend carries MY cooler: their screen shows mine, mine shows my own (it is still mine).
		TestTrue(TEXT("the friend picks up mine"), Mine->AuthorityPickUp(Friend));
		Rig.W.Tick(2);
		Expect(FriendPC, TEXT("2/4"), TEXT("2/4"), TEXT("friend carrying mine"));
		Expect(MyPC, TEXT("2/4"), TEXT(""), TEXT("me while the friend carries mine"));
		Mine->AuthorityPutDownAt(MineSpot, 180.0f);
		Rig.W.Tick(2);

		// A player with no cooler of their own carrying someone's.
		TestTrue(TEXT("the newcomer picks up the friend's"), Theirs->AuthorityPickUp(Newcomer));
		Rig.W.Tick(2);
		Expect(NewPC, TEXT("2/8"), TEXT("2/8"), TEXT("newcomer carrying the friend's"));
		Theirs->AuthorityPutDownAt(TheirSpot, 180.0f);
		Rig.W.Tick(2);
		Expect(NewPC, TEXT(""), TEXT(""), TEXT("newcomer put it down: no count again"));
		return true;
	}

	// =====================================================================================================================
	// 5. Network: the lid and the HUD on clients (UE::Net::FTestWorlds: a server and real clients)
	// =====================================================================================================================

#if WITH_EDITOR
	/** Shipped text tables for every machine */
	struct FNetData
	{
		TStrongObjectPtr<UDataTable> Movement;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Freshness;
		TStrongObjectPtr<UDataTable> Catch;
		TStrongObjectPtr<UDataTable> Display;
		TStrongObjectPtr<UDataTable> Levels;
		FishQA::FTables Fish;
		FLureCatchRow Tuning;
		float HalfHeight = 90.0f;

		bool Load(FAutomationTestBase& Test)
		{
			Test.AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			Test.AddExpectedMessagePlain(TEXT("NOT Supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
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
			const FLureCatchRow* Row = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("QA"), false);
			if (!Test.TestNotNull(TEXT("QA setup: DT_Catch Default"), Row))
			{
				return false;
			}
			Tuning = *Row;
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
			HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.0f;
			return true;
		}

		/** The data and the dock (level geometry, not replicated) in World */
		void Prepare(UWorld* World) const
		{
			if (ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
			{
				Subsystem->SetTuning(Tuning);
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
		}

		/** A player pawn on the server standing at Feet (possessed by Controller if given) */
		ALurePlayerCharacter* SpawnPawn(UWorld* Server, const FVector& Feet, APlayerController* Controller) const
		{
			const FTransform At(FRotator::ZeroRotator, Feet + FVector(0.0f, 0.0f, HalfHeight + 2.15f));
			ALurePlayerCharacter* Pawn = Server->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Pawn)
			{
				return nullptr;
			}
			Pawn->GetLureMovement()->ApplyMovementTable(Movement.Get());
			Pawn->FinishSpawning(At);
			if (Controller)
			{
				Controller->Possess(Pawn);
				if (ULureProgressionComponent* Progression = LCT::ProgressionOf(Pawn))
				{
					Progression->SetLevelTable(Levels.Get());
				}
			}
			return Pawn;
		}
	};

	/** Client's copy of a server object (null until replicated) */
	template <typename T>
	T* OnClient(UE::Net::FTestWorlds& Worlds, int32 Client, T* ServerObject)
	{
		if (!Worlds.Clients.IsValidIndex(Client) || !ServerObject || !Worlds.IsServerObjectReplicated(ServerObject)
			|| !Worlds.DoesReplicatedObjectExistOnClient(ServerObject, static_cast<uint32>(Client)))
		{
			return nullptr;
		}
		return Cast<T>(Worlds.FindReplicatedObjectOnClient(static_cast<UObject*>(ServerObject), static_cast<uint32>(Client)));
	}

	APlayerController* ClientPC(UE::Net::FTestWorlds& Worlds, int32 Client)
	{
		UWorld* World = Worlds.Clients.IsValidIndex(Client) ? Worlds.Clients[Client].GetWorld() : nullptr;
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQANetLidJoinsOpen, "Project.Catch.QA.Net.LidJoinsOpenWithoutSwing", LCT::Flags)
	bool FCoolerQANetLidJoinsOpen::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FNetData Data;
		UWorld* Server = Worlds.Server.GetWorld();
		if (!Data.Load(*this) || !TestTrue(TEXT("harness: the server world is up"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()))
		{
			return false;
		}
		Data.Prepare(Server);
		ALurePlayerCharacter* Pawn = Data.SpawnPawn(Server, FVector(0.0f, 0.0f, LCT::DockTop), nullptr);
		ULureHandsComponent* Hands = LCT::HandsOf(Pawn);
		ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(130.0f, 0.0f, LCT::DockTop)), nullptr);
		if (!TestNotNull(TEXT("QA setup: hands"), Hands) || !TestNotNull(TEXT("QA setup: cooler"), Cooler))
		{
			return false;
		}
		int32 Seed = 1500;
		auto PutIn = [&]()
		{
			ALureFishItem* Item = ALureFishItem::SpawnFish(Server, FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 5.5f, ++Seed), 0.0), FTransform(FVector(40.0f, 0.0f, LCT::DockTop)));
			return Item && Hands->AuthorityTakeInHand(Item) && Cooler->AuthorityPutFishIn(Pawn, Item);
		};
		// Before anyone joins: a clack (pulse id bumped), then the lid opened and left open.
		TestTrue(TEXT("server: a fish in (clack)"), PutIn());
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Worlds.TickServer();
		}
		TestTrue(TEXT("server: open"), Cooler->AuthoritySetLidOpen(true));
		for (int32 Tick = 0; Tick < 30; ++Tick)
		{
			Worlds.TickServer();
		}
		if (!TestTrue(TEXT("harness: the client connects"), Worlds.CreateAndConnectClient()))
		{
			return false;
		}
		Data.Prepare(Worlds.Clients[0].GetWorld());
		// From the first frame the client's cooler has begun play, its lid is fully open (no swing up from 0).
		float FirstSeen = -1.0f;
		const bool bArrived = Worlds.TickAllUntil([&]()
		{
			const ALureCoolerActor* Remote = OnClient(Worlds, 0, Cooler);
			if (Remote && Remote->HasActorBegunPlay())
			{
				FirstSeen = Remote->GetLidPitch();
				return true;
			}
			return false;
		}, LCT::Dt, 600);
		if (!TestTrue(TEXT("the cooler reaches the client"), bArrived))
		{
			return false;
		}
		ALureCoolerActor* Remote = OnClient(Worlds, 0, Cooler);
		FLidTrace Join;
		for (int32 Tick = 0; Tick < 40; ++Tick)
		{
			Join.Sample(Remote);
			Worlds.TickAll(1);
		}
		TestTrue(FString::Printf(TEXT("client: joins open (first seen %.2f deg, LidOpenPitch %.0f)"), FirstSeen, Data.Tuning.LidOpenPitch),
			Remote->IsLidOpen() && FMath::IsNearlyEqual(FirstSeen, Data.Tuning.LidOpenPitch, 0.5f));
		TestTrue(FString::Printf(TEXT("client: ... and never swings (%.2f..%.2f)"), Join.MinPitch, Join.MaxPitch), Join.MinPitch >= Data.Tuning.LidOpenPitch - 0.5f && Join.MaxPitch <= Data.Tuning.LidOpenPitch + 0.5f);
		TestEqual(TEXT("client: the open cooler shows its fish"), Remote->GetNumDisplayedFish(), 1);

		// A fish into the open cooler: no clack on the client, the lid stays open.
		TestTrue(TEXT("server: a fish into the open cooler"), PutIn());
		FLidTrace Open;
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Worlds.TickAll(1);
			Open.Sample(Remote);
		}
		TestTrue(FString::Printf(TEXT("client: the open lid doesn't move (%.2f..%.2f)"), Open.MinPitch, Open.MaxPitch), Open.MinPitch >= Data.Tuning.LidOpenPitch - 0.5f);
		TestEqual(TEXT("client: two fish show"), Remote->GetNumDisplayedFish(), 2);

		// Close: shut on the client within LidOpenTime of the change arriving, nothing shows.
		TestTrue(TEXT("server: close"), Cooler->AuthoritySetLidOpen(false));
		TestTrue(TEXT("client: the state arrives"), Worlds.TickAllUntil([&]() { return !Remote->IsLidOpen(); }, LCT::Dt, 120));
		Worlds.TickAll(FMath::CeilToInt(Data.Tuning.LidOpenTime / LCT::Dt) + 2);
		TestEqual(TEXT("client: shut within LidOpenTime"), Remote->GetLidPitch(), 0.0f, 0.01f);
		TestEqual(TEXT("client: closed, nothing shows"), Remote->GetNumDisplayedFish(), 0);

		// A second client joining now (closed, an old pulse): shut from its first frame.
		if (TestTrue(TEXT("harness: a second client connects"), Worlds.CreateAndConnectClient()))
		{
			Data.Prepare(Worlds.Clients[1].GetWorld());
			TestTrue(TEXT("the cooler reaches the second client"), Worlds.TickAllUntil([&]() { const ALureCoolerActor* Late = OnClient(Worlds, 1, Cooler); return Late && Late->HasActorBegunPlay(); }, LCT::Dt, 600));
			if (ALureCoolerActor* Late = OnClient(Worlds, 1, Cooler))
			{
				FLidTrace Shut;
				for (int32 Tick = 0; Tick < 40; ++Tick)
				{
					Shut.Sample(Late);
					Worlds.TickAll(1);
				}
				TestTrue(FString::Printf(TEXT("second client: closed and shut from the start (max %.2f)"), Shut.MaxPitch), !Shut.bEverOpen && Shut.MaxPitch < 0.01f);
			}
		}
		return true;
	}

	/** Two possessed players on the dock: client 0 at y -100, client 1 at y +100, facing +X */
	struct FTwoPlayers
	{
		UE::Net::FTestWorlds& Worlds;
		FNetData Data;
		UWorld* Server = nullptr;
		ALurePlayerCharacter* Players[2] = { nullptr, nullptr };

		explicit FTwoPlayers(UE::Net::FTestWorlds& InWorlds) : Worlds(InWorlds) {}

		bool Create(FAutomationTestBase& Test)
		{
			Server = Worlds.Server.GetWorld();
			if (!Data.Load(Test) || !Test.TestTrue(TEXT("harness: the server world is up"), Worlds.Server.IsLoaded() && Server && Server->GetNetDriver()))
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
			Data.Prepare(Server);
			for (UE::Net::FTestWorldInstance& Client : Worlds.Clients)
			{
				Data.Prepare(Client.GetWorld());
			}
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Players[Index] = Data.SpawnPawn(Server, FVector(0.0f, Index == 0 ? -100.0f : 100.0f, LCT::DockTop), Worlds.GetServerPlayerControllerOfClient(Index));
				if (!Test.TestNotNull(TEXT("harness: pawn"), Players[Index]))
				{
					return false;
				}
			}
			const bool bReady = Worlds.TickAllUntil([this]()
			{
				for (int32 Client = 0; Client < 2; ++Client)
				{
					const ALurePlayerCharacter* Own = OnClient(Worlds, Client, Players[Client]);
					const ALurePlayerCharacter* Other = OnClient(Worlds, Client, Players[1 - Client]);
					if (!Own || !Other || !Own->IsLocallyControlled() || !Own->GetHands() || !Other->GetHands() || !ClientPC(Worlds, Client) || !ClientPC(Worlds, Client)->PlayerState)
					{
						return false;
					}
				}
				return true;
			}, LCT::Dt, 600);
			if (!Test.TestTrue(TEXT("harness: each client has its own player and the other one"), bReady))
			{
				return false;
			}
			Worlds.TickAll(30);
			return true;
		}

		ALurePlayerCharacter* Mine(int32 Client) const { return OnClient(Worlds, Client, Players[Client]); }
		ULureInteractionComponent* Keys(int32 Client) const { return LCT::InteractionOf(Mine(Client)); }

		void LookAt(int32 Client, const FVector& Target) const
		{
			ALurePlayerCharacter* Pawn = Mine(Client);
			if (APlayerController* Controller = ClientPC(Worlds, Client); Controller && Pawn)
			{
				Controller->SetControlRotation((Target - Pawn->GetPawnViewLocation()).Rotation());
			}
		}

		bool Until(TFunctionRef<bool()> Predicate, int32 MaxTicks = 180)
		{
			return Worlds.TickAllUntil([&Predicate]() { return Predicate(); }, LCT::Dt, MaxTicks);
		}

		/** Client looks at Target until its E / F resolves to Verb on Target, then presses the key (a real ServerInteract) */
		bool Press(FAutomationTestBase& Test, int32 Client, AActor* ServerTarget, const FVector& LookTarget, ELureInteractKey Key, ELureInteractVerb Verb, const FString& What)
		{
			LookAt(Client, LookTarget);
			const bool bReady = Until([&]()
			{
				LookAt(Client, LookTarget);
				const FLureResolvedInteraction Resolved = Keys(Client)->ResolveInteraction(Key);
				return Resolved.Verb == Verb && (!ServerTarget || Resolved.Target == OnClient(Worlds, Client, ServerTarget));
			}, 120);
			const FLureResolvedInteraction Now = Keys(Client)->ResolveInteraction(Key);
			return Test.TestTrue(FString::Printf(TEXT("%s: the prompt is %s (%s)"), *What, *LCT::VerbName(Verb), *LCT::VerbName(Now.Verb)), bReady)
				&& Test.TestTrue(What + TEXT(": key sent"), Keys(Client)->PressKey(Key));
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQANetClackBothClients, "Project.Catch.QA.Net.ClackSeenByBothClients", LCT::Flags)
	bool FCoolerQANetClackBothClients::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FTwoPlayers Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		// Client 1's closed cooler in front of client 0.
		ALureCoolerActor* Cooler = ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(120.0f, -100.0f, LCT::DockTop)),
			Net.Players[1]->GetPlayerState());
		ALureFishItem* Item = ALureFishItem::SpawnFish(Net.Server, FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 1601), 0.0), FTransform(FVector(40.0f, -100.0f, LCT::DockTop)));
		ULureHandsComponent* Hands = LCT::HandsOf(Net.Players[0]);
		if (!TestNotNull(TEXT("QA setup: cooler"), Cooler) || !TestNotNull(TEXT("QA setup: fish"), Item) || !TestNotNull(TEXT("QA setup: hands"), Hands))
		{
			return false;
		}
		TestTrue(TEXT("QA setup: the fish in client 0's hand"), Hands->AuthorityTakeInHand(Item));
		if (!TestTrue(TEXT("both clients see the cooler, client 0 holds the fish"), Net.Until([&]()
			{
				return OnClient(Worlds, 0, Cooler) && OnClient(Worlds, 1, Cooler) && LCT::HandsOf(Net.Mine(0))->GetHeldFish() != nullptr;
			}, 600)))
		{
			return false;
		}
		Worlds.TickAll(20);
		ALureCoolerActor* Views[2] = { OnClient(Worlds, 0, Cooler), OnClient(Worlds, 1, Cooler) };
		if (!Net.Press(*this, 0, Cooler, Views[0]->GetInteractionLocation(), KeyE, ELureInteractVerb::PutFishInCooler, TEXT("client 0 puts the fish in")))
		{
			return false;
		}
		// The clients are the rendering machines (the harness server is dedicated): their lids must clack.
		FLidTrace Traces[2];
		for (int32 Tick = 0; Tick < 120; ++Tick)
		{
			Worlds.TickAll(1);
			Traces[0].Sample(Views[0]);
			Traces[1].Sample(Views[1]);
		}
		TestEqual(TEXT("server: the fish is in"), Cooler->GetNumFish(), 1);
		const TCHAR* Names[2] = { TEXT("client 0 (put it in)"), TEXT("client 1 (the owner, watching)") };
		const float Pulse = Net.Data.Tuning.LidPulsePitch;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FLidTrace& T = Traces[Index];
			TestFalse(FString::Printf(TEXT("%s: the lid state stays closed"), Names[Index]), T.bEverOpen);
			TestTrue(FString::Printf(TEXT("%s: a clack (max %.1f deg, pulse %.0f)"), Names[Index], T.MaxPitch, Pulse), T.MaxPitch >= 0.8f * Pulse && T.MaxPitch <= Pulse + 0.5f);
			TestTrue(FString::Printf(TEXT("%s: short (%d ticks up, LidOpenTime %.2f s)"), Names[Index], T.TicksUp, Net.Data.Tuning.LidOpenTime), T.TicksUp * LCT::Dt <= Net.Data.Tuning.LidOpenTime + 0.001f);
			TestTrue(FString::Printf(TEXT("%s: never below closed (%.2f)"), Names[Index], T.MinPitch), T.MinPitch >= -0.01f);
		}
		TestTrue(TEXT("both clients: shut at the end, the server's state closed"), Views[0]->GetLidPitch() < 0.01f && Views[1]->GetLidPitch() < 0.01f && !Cooler->IsLidOpen());
		// The prompt agrees with the look on client 0: Open.
		Net.LookAt(0, Views[0]->GetInteractionLocation());
		Worlds.TickAll(2);
		TestEqual(TEXT("client 0: the prompt says Open"), LCT::VerbName(Net.Keys(0)->ResolveInteraction(KeyE).Verb), LCT::VerbName(ELureInteractVerb::OpenCooler));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoolerQANetHudCarriedCount, "Project.Catch.QA.Net.HudCountsCarriedCoolerOnClients", LCT::Flags)
	bool FCoolerQANetHudCarriedCount::RunTest(const FString& Parameters)
	{
		UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
		FTwoPlayers Net(Worlds);
		if (!Net.Create(*this))
		{
			return false;
		}
		// Client 0 owns an empty cooler behind them; client 1 owns a cooler with 1 fish in front of client 0. The game mode may
		// already have made each player a starter cooler: use it (one own cooler each), else make one.
		auto OwnCooler = [&](int32 Index, const FVector& Spot, float Yaw)
		{
			ALureCoolerActor* Found = nullptr;
			int32 Count = 0;
			for (TActorIterator<ALureCoolerActor> It(Net.Server); It; ++It)
			{
				if (IsValid(*It) && It->GetOwningPlayerState() == Net.Players[Index]->GetPlayerState())
				{
					Found = Found ? Found : *It;
					++Count;
				}
			}
			TestTrue(FString::Printf(TEXT("QA setup: player %d owns at most one cooler (%d)"), Index, Count), Count <= 1);
			if (Found)
			{
				Found->AuthorityPutDownAt(Spot, Yaw);
				return Found;
			}
			return ALureCoolerActor::SpawnCooler(Net.Server, NAME_None, FTransform(FRotator(0.0f, Yaw, 0.0f), Spot), Net.Players[Index]->GetPlayerState());
		};
		ALureCoolerActor* Mine = OwnCooler(0, FVector(-250.0f, -100.0f, LCT::DockTop), 0.0f);
		ALureCoolerActor* Theirs = OwnCooler(1, FVector(120.0f, -100.0f, LCT::DockTop), 180.0f);
		if (!TestNotNull(TEXT("QA setup: client 0's cooler"), Mine) || !TestNotNull(TEXT("QA setup: client 1's cooler"), Theirs))
		{
			return false;
		}
		TestEqual(TEXT("QA setup: client 1's cooler starts empty"), Theirs->GetNumFish(), 0);
		Theirs->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 1701), FLureFreshness::GetServerTime(Net.Server)));
		auto Counts = [&](int32 Client) { return HudOf(ClientPC(Worlds, Client)); };
		const bool bArrived = Net.Until([&]()
		{
			return OnClient(Worlds, 0, Mine) && OnClient(Worlds, 0, Theirs) && OnClient(Worlds, 1, Theirs) && Counts(0).Status == TEXT("0/4") && Counts(1).Status == TEXT("1/4");
		}, 600);
		TestTrue(FString::Printf(TEXT("hands empty, each client counts its own (client 0: %s, client 1: %s)"), *Counts(0).All, *Counts(1).All), bArrived);

		// Client 0 picks up client 1's cooler with F (a real key press).
		ALureCoolerActor* TheirsOn0 = OnClient(Worlds, 0, Theirs);
		if (!TheirsOn0 || !Net.Press(*this, 0, Theirs, TheirsOn0->GetInteractionLocation(), KeyF, ELureInteractVerb::PickUpCooler, TEXT("client 0 picks up client 1's cooler")))
		{
			return false;
		}
		TestTrue(TEXT("server: client 0 carries it"), Net.Until([&]() { return LCT::HandsOf(Net.Players[0])->GetCarriedCooler() == Theirs; }));
		const bool bCarried = Net.Until([&]() { return Counts(0).Carrying == TEXT("1/4"); }, 180);
		Worlds.TickAll(5);
		const FHudCounts Carrier = Counts(0);
		TestTrue(FString::Printf(TEXT("client 0 (carrier): status and Carrying agree on 1/4 (%s)"), *Carrier.All), bCarried && Carrier.Status == TEXT("1/4") && Carrier.Carrying == TEXT("1/4"));
		TestFalse(FString::Printf(TEXT("client 0: no 0/4 cooler count on screen (%s)"), *Carrier.All), Carrier.All.Contains(TEXT("Cooler 0/4")) || Carrier.All.Contains(TEXT("(0/4")));
		const FHudCounts Owner = Counts(1);
		TestTrue(FString::Printf(TEXT("client 1 (owner, hands empty): its own 1/4 (%s)"), *Owner.All), Owner.Status == TEXT("1/4") && Owner.Carrying.IsEmpty());
		TestEqual(TEXT("server's view of client 0 agrees"), ULureCatchLibrary::GetCoolerStatus(Worlds.GetServerPlayerControllerOfClient(0)), FString(TEXT("Cooler 1/4")));

		// A fish added on the server while carried: client 0's count follows.
		Theirs->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("Bonefish"), 10, 1, 1.5f, 1702), FLureFreshness::GetServerTime(Net.Server)));
		TestTrue(FString::Printf(TEXT("client 0: 2/4 after a fish is added (%s)"), *Counts(0).All), Net.Until([&]() { return Counts(0).Status == TEXT("2/4") && Counts(0).Carrying == TEXT("2/4"); }));

		// Put it down (F, T-064): client 0 counts its own again.
		if (!Net.Press(*this, 0, nullptr, Net.Mine(0)->GetPawnViewLocation() + Net.Mine(0)->GetActorForwardVector() * 100.0f + FVector(0.0f, 0.0f, -100.0f), KeyF,
			ELureInteractVerb::PutDownCooler, TEXT("client 0 puts it down")))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("client 0: its own 0/4 again, no Carrying line (%s)"), *Counts(0).All), Net.Until([&]() { return Counts(0).Status == TEXT("0/4") && Counts(0).Carrying.IsEmpty(); }));
		return true;
	}
#endif // WITH_EDITOR
}

#endif // WITH_DEV_AUTOMATION_TESTS

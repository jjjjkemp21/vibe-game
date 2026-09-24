// QA-owned helpers for the independent fighting-fish visual tests (Project.FishVisual.QA.*, T-029), written by the qa-engineer.
// Black-box: expectations come from docs/specs/fight-fish-visual.md and the contract comments in Fish/FightFishVisual.h,
// Fish/LureFightFish.h, Fish/LureFightFishSubsystem.h and Fishing/FightFishViewAdapter.h, never from the implementation.
// Tables come from the text sources in data/tables/ (never the binary /Game/Data assets) or from fixtures built here.
//
// Two ways to drive the visual:
//  - FRealScene: a real server-side fight (ULureFishingComponent on an authority character, the T-007 fight rules).
//  - FPuppet: a character copy with ROLE_SimulatedProxy whose replicated fishing structs the test writes directly (what a
//    client receives). It never simulates, so a test can play any sequence of fight ids, moves and endings cheaply.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/FishFight/FightQATestUtils.h"
#include "EngineUtils.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"
#include "Fish/LureFightFishSubsystem.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace LureFightFishQA
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = LureFightQA::WorldDt;

	inline FString RoleName(EFishAnimRole Role)
	{
		return StaticEnum<EFishAnimRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	/** data/tables/DT_FishVisual.json as a transient table (import problems are test errors). */
	inline bool LoadVisual(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, FString* OutText = nullptr)
	{
		FString Text;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_FishVisual.json"), Text))
		{
			return false;
		}
		if (OutText)
		{
			*OutText = Text;
		}
		return LureFightQA::MakeTableChecked(Test, Out, FFishVisualRow::StaticStruct(), Text, true, TEXT("DT_FishVisual.json"));
	}

	/** The row the settings name (Default). */
	inline const FFishVisualRow* SettingsRow(const UDataTable* Table)
	{
		return Table ? Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("FishVisualQA"), false) : nullptr;
	}

	/** A one-row visual table (row named by the settings) holding Row. */
	inline TStrongObjectPtr<UDataTable> OneVisualTable(const FFishVisualRow& Row, FName RowName = NAME_None)
	{
		TStrongObjectPtr<UDataTable> Table(FishQA::NewTable(FFishVisualRow::StaticStruct()));
		Table->AddRow(RowName.IsNone() ? GetDefault<ULureFishVisualSettings>()->VisualRow : RowName, Row);
		return Table;
	}

	/** Two rows compare equal, DevComment ignored. */
	inline bool SameVisualRow(const FFishVisualRow& A, const FFishVisualRow& B)
	{
		FFishVisualRow Copy = A;
		Copy.DevComment = B.DevComment;
		return FFishVisualRow::StaticStruct()->CompareScriptStruct(&Copy, &B, PPF_None);
	}

	/** Live fight fish actors in World (pending-kill ones are skipped by the iterator). */
	inline int32 CountFishActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ALureFightFish> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	inline int32 CountAllActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	/** A species table: the shipped rows plus Extra (name -> row). */
	inline TStrongObjectPtr<UDataTable> SpeciesWith(const FishQA::FTables& Fish, const TArray<TPair<FName, FFishSpeciesRow>>& Extra)
	{
		TStrongObjectPtr<UDataTable> Table(FishQA::NewTable(FFishSpeciesRow::StaticStruct()));
		Fish.Species->ForeachRow<FFishSpeciesRow>(TEXT("FishVisualQA"), [&Table](const FName& Name, const FFishSpeciesRow& Row) { Table->AddRow(Name, Row); });
		for (const TPair<FName, FFishSpeciesRow>& Row : Extra)
		{
			Table->AddRow(Row.Key, Row.Value);
		}
		return Table;
	}

	/**
	 *  T-030 x T-029 (ULureCatchLinkSubsystem, docs/specs/catch-handling-rules.md "The glue"): after a real landing the fight
	 *  fish is either gone or kept as the look of the fish item now hanging on Owner's hook (it goes when that item goes).
	 */
	inline bool LandedFishHandedOff(const TWeakObjectPtr<ALureFightFish>& Fish, const AActor* Owner)
	{
		if (!Fish.IsValid())
		{
			return true;
		}
		const ULureHandsComponent* Hands = ULureHandsComponent::Get(Owner);
		const ALureFishItem* Hanging = Hands ? Hands->GetHangingFish() : nullptr;
		return Hanging && Hanging->GetAdoptedVisual() == Fish.Get();
	}

	/** T-030: a landed fish hangs on the hook (a cast is Busy) until it is taken off: removes it and the look it adopted. True if one hung. */
	inline bool ClearHangingCatch(AActor* Owner)
	{
		ULureHandsComponent* Hands = ULureHandsComponent::Get(Owner);
		ALureFishItem* Hanging = Hands ? Hands->AuthorityReleaseHanging() : nullptr;
		if (Hanging)
		{
			Hanging->Destroy();
		}
		return Hanging != nullptr;
	}

	/** A plain fish record (not rolled; the visual only reads species and weight). */
	inline FFishInstance MakeRecord(FName Species, float WeightKg)
	{
		FFishInstance Fish;
		Fish.SpeciesId = Species;
		Fish.WeightKg = WeightKg;
		return Fish;
	}

	// =================================================================================================================
	// A client's copy of a player whose replicated fishing state the test writes (no simulation)
	// =================================================================================================================

	struct FPuppet
	{
		ALurePlayerCharacter* Character = nullptr;
		ULureFishingComponent* Fishing = nullptr;
		FLureFightNetState* Fight = nullptr;
		FLureFishingNetState* Line = nullptr;
		FFishInstance* Hooked = nullptr;

		~FPuppet() { Restore(); }

		bool Create(FAutomationTestBase& Test, LureFightQA::FWorld& World, const FVector& Feet)
		{
			Character = World.Spawn(Feet);
			Fishing = Character ? Character->GetFishing() : nullptr;
			if (!Test.TestNotNull(TEXT("puppet: character with fishing"), Fishing))
			{
				return false;
			}
			Character->GetCharacterMovement()->SetComponentTickEnabled(false);
			Character->SetRole(ROLE_SimulatedProxy);
			Fight = LureFightQA::ReplicatedField<FLureFightNetState>(Fishing, TEXT("FightNet"));
			Line = LureFightQA::ReplicatedField<FLureFishingNetState>(Fishing, TEXT("NetState"));
			Hooked = LureFightQA::ReplicatedField<FFishInstance>(Fishing, TEXT("HookedFish"));
			return Test.TestTrue(TEXT("puppet: FightNet, NetState and HookedFish are writable"), Fight && Line && Hooked);
		}

		/** Back to authority before the world goes (the character may already be destroyed). */
		void Restore()
		{
			if (IsValid(Character))
			{
				Character->SetRole(ROLE_Authority);
			}
			Character = nullptr;
		}

		FVector Player() const { return Character->GetActorLocation(); }

		/** The bobber rest point: 1600 cm out along +X from the dock, on the water (z = 0). */
		FVector BobberRest() const { return FVector(1600.f, Player().Y, 0.f); }

		/** A fight is on (as the server replicates it): hooked line, active fight with this id and move. */
		void Start(uint8 FightId, const FFishInstance& Fish, FName MoveId = TEXT("Swim"), float Depth = 0.f, float LineOut = 1200.f)
		{
			Line->State = ELureFishingState::Hooked;
			Line->LastResult = ELureFishingResult::Hooked;
			Line->BobberRest = BobberRest();
			*Hooked = Fish;
			Fight->bActive = true;
			Fight->FightId = FightId;
			Fight->Outcome = ELureFightOutcome::None;
			Fight->MoveId = MoveId;
			Fight->bExhausted = false;
			Fight->Depth = Depth;
			Fight->LineOut = LineOut;
			Fight->SideDeg = 0.f;
			Fight->LineStrength = 10.f;
			Fight->Tension = 3.f;
		}

		/** The fight ended (as the server replicates it): line in, fight inactive, the outcome and the line's result. */
		void End(ELureFightOutcome Outcome, ELureFishingResult Result)
		{
			Fight->bActive = false;
			Fight->Outcome = Outcome;
			Fight->MoveId = NAME_None;
			Line->State = ELureFishingState::Idle;
			Line->LastResult = Result;
			*Hooked = FFishInstance();
		}

		/** Where the line ends in the water now (the adapter's rule; asserted separately by the Adapter tests). */
		FVector LineEnd() const { return FFightFishViewAdapter::FromComponent(*Fishing).LineEnd; }
	};

	// =================================================================================================================
	// A real fight on the server (standalone) with the fish visual watching
	// =================================================================================================================

	/** How a fight ends in RunOutcome. */
	enum class EOutcomeCase : uint8
	{
		Landed,
		Snapped,
		Spooled,
		ThrewHook,
		ReeledIn,
		Cancelled
	};

	inline const TCHAR* CaseName(EOutcomeCase Case)
	{
		switch (Case)
		{
		case EOutcomeCase::Landed: return TEXT("landed");
		case EOutcomeCase::Snapped: return TEXT("snapped");
		case EOutcomeCase::Spooled: return TEXT("spooled");
		case EOutcomeCase::ThrewHook: return TEXT("threw the hook");
		case EOutcomeCase::ReeledIn: return TEXT("reeled in");
		default: return TEXT("cancelled (sprinting)");
		}
	}

	struct FRealScene
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		TStrongObjectPtr<UDataTable> Gear;
		LureFightQA::FWorld World;
		ULureFightFishSubsystem* Visuals = nullptr;
		ALurePlayerCharacter* Character = nullptr;
		ULureFishingComponent* Fishing = nullptr;
		FFishInstance Bonefish;
		int32 Landed = 0;
		TArray<FFishInstance> LandedRecords;
		TArray<ULureFishingComponent*> LandedBy;

		/** Tables, world, one authority character with fishing, the visual on the text-source tables, a landed counter. */
		bool Init(FAutomationTestBase& Test)
		{
			if (!Data.Load(Test) || !FishQA::LoadReal(Test, Fish) || !LoadVisual(Test, Visual) || !World.Create(Test))
			{
				return false;
			}
			FString Csv = Data.GearCsv;
			if (!Csv.EndsWith(TEXT("\n")))
			{
				Csv += TEXT("\n");
			}
			Csv += TEXT("Line_QAShort,Line,QA Short Line,1,0,0,0,0,50,1500,0,None,0,qa: a strong line on a short spool\n");
			if (!LureFightQA::MakeTableChecked(Test, Gear, FLureGearRow::StaticStruct(), Csv, false, TEXT("fixture DT_Gear (shipped + short spool)"))
				|| !LureFightQA::RollFish(Test, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 71, Bonefish))
			{
				return false;
			}
			Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
			Character = World.Spawn(LureFightQA::StandAt());
			Fishing = LureFightQA::SetUpFishing(Character, Fish, Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
			if (!Test.TestNotNull(TEXT("fight fish subsystem"), Visuals) || !Test.TestNotNull(TEXT("fishing"), Fishing))
			{
				return false;
			}
			Visuals->SetTables(Fish.Species.Get(), Visual.Get());
			Visuals->OnFightFishLandedNative.AddLambda([this](ULureFishingComponent* By, ALureFightFish*, const FFishInstance& Record)
			{
				++Landed;
				LandedRecords.Add(Record);
				LandedBy.Add(By);
			});
			return true;
		}

		/** A pattern table where the species' pattern row holds only Pattern. */
		TStrongObjectPtr<UDataTable> OnePattern(const FLureFightPatternRow& Pattern) const
		{
			const FFishSpeciesRow* Species = FishQA::Row<FFishSpeciesRow>(Fish.Species, Bonefish.SpeciesId);
			TStrongObjectPtr<UDataTable> Table(FishQA::NewTable(FLureFightPatternRow::StaticStruct()));
			Table->AddRow(Species ? Species->FightPatternId : FName(TEXT("Run")), Pattern);
			return Table;
		}

		/**
		 *  One whole fight that ends as Case, checked all the way: no fish before the hook, exactly one fish (the hooked one)
		 *  during the fight, then Landed = one landed event and the fish gone; anything else = no event, the fish swims away
		 *  for EscapeTime and is removed. Leaves the world with no fish.
		 */
		bool RunOutcome(FAutomationTestBase& Test, EOutcomeCase Case, int32 Round = 0)
		{
			const FString Name = FString::Printf(TEXT("round %d, %s"), Round, CaseName(Case));
			const FLureGearLoadout Starter = GetDefault<ULureFishingSettings>()->DefaultLoadout;
			FLureGearLoadout Kit = Starter;
			TStrongObjectPtr<UDataTable> Patterns;
			bool bReel = false;
			int32 ReelInAfter = 0;
			ELureCastBlock Reason = ELureCastBlock::None;
			const FLureFightPatternRow* Shipped = nullptr;
			if (const FFishSpeciesRow* Species = FishQA::Row<FFishSpeciesRow>(Fish.Species, Bonefish.SpeciesId))
			{
				Shipped = Data.Pattern(Species->FightPatternId);
			}
			if (!Test.TestNotNull(*FString::Printf(TEXT("%s: the Bonefish pattern"), *Name), Shipped))
			{
				return false;
			}
			switch (Case)
			{
			case EOutcomeCase::Landed:
				Patterns = OnePattern(*Shipped);
				bReel = true;
				break;
			case EOutcomeCase::Snapped:
				Patterns = OnePattern(LureFightQA::MakePattern({ LureFightQA::MakeMove(TEXT("Heave"), 3.5f, 0.f, 0.f) }, TEXT("Heave")));
				bReel = true;
				break;
			case EOutcomeCase::Spooled:
				Patterns = OnePattern(LureFightQA::MakePattern({ LureFightQA::MakeMove(TEXT("Bolt"), 2.5f, 2.f, 1.f) }, TEXT("Bolt")));
				Kit.Line = TEXT("Line_QAShort");
				break;
			case EOutcomeCase::ThrewHook:
				Patterns = OnePattern(LureFightQA::MakePattern({ LureFightQA::MakeMove(TEXT("Sit"), 0.f, 0.f, 0.f) }, TEXT("Sit")));
				break;
			case EOutcomeCase::ReeledIn:
				Patterns = OnePattern(*Shipped);
				ReelInAfter = 30;
				break;
			case EOutcomeCase::Cancelled:
				Patterns = OnePattern(*Shipped);
				bReel = true;
				ReelInAfter = 30;
				Reason = ELureCastBlock::Sprinting;
				break;
			}
			Fishing->SetFightTables(Gear.Get(), Patterns.Get(), Data.Fight.Get());
			if (!Test.TestTrue(*FString::Printf(TEXT("%s: gear equipped"), *Name), Fishing->AuthoritySetLoadout(Kit))
				|| !LureFightQA::CastAndWait(Test, World, Fishing))
			{
				return false;
			}
			Test.TestEqual(*FString::Printf(TEXT("%s: no fish before the hook"), *Name), Visuals->GetNumFish(), 0);
			Test.TestEqual(*FString::Printf(TEXT("%s: no fish actor before the hook"), *Name), CountFishActors(World.World), 0);
			if (!Test.TestTrue(*FString::Printf(TEXT("%s: hooked"), *Name), Fishing->AuthorityHookFish(Bonefish)))
			{
				return false;
			}
			World.Tick(1);
			ALureFightFish* Actor = Visuals->FindFish(Fishing);
			if (!Test.TestNotNull(*FString::Printf(TEXT("%s: a fish appears with the fight"), *Name), Actor))
			{
				return false;
			}
			Test.TestEqual(*FString::Printf(TEXT("%s: one fish"), *Name), Visuals->GetNumFish(), 1);
			Test.TestEqual(*FString::Printf(TEXT("%s: one fish actor"), *Name), CountFishActors(World.World), 1);
			Test.TestTrue(*FString::Printf(TEXT("%s: it is the hooked fish"), *Name), LureFightQA::SameFish(Actor->GetFish(), Bonefish));
			Test.TestEqual(*FString::Printf(TEXT("%s: its fight id"), *Name), static_cast<int32>(Actor->GetFightId()), static_cast<int32>(Fishing->GetFightNet().FightId));

			TWeakObjectPtr<ALureFightFish> Weak = Actor;
			const int32 LandedBefore = Landed;
			Fishing->AuthoritySetReeling(bReel);
			if (ReelInAfter > 0)
			{
				World.Tick(ReelInAfter);
				Test.TestTrue(*FString::Printf(TEXT("%s: still fighting before the reel-in"), *Name), Fishing->GetFightNet().bActive);
				Fishing->AuthorityReelIn(Reason);
			}
			bool bOneFish = true;
			bool bSameFish = true;
			int32 Frames = 0;
			for (; Frames < 40 * 60 && Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frames)
			{
				World.Tick(1);
				bOneFish &= CountFishActors(World.World) <= 1;
				if (Fishing->GetFishingState() == ELureFishingState::Hooked)
				{
					bSameFish &= Visuals->FindFish(Fishing) == Weak.Get() && Weak.IsValid();
				}
			}
			if (!Test.TestTrue(*FString::Printf(TEXT("%s: the fight ends"), *Name), Fishing->GetFishingState() != ELureFishingState::Hooked))
			{
				return false;
			}
			Test.TestTrue(*FString::Printf(TEXT("%s: never more than one fish actor during the fight"), *Name), bOneFish);
			Test.TestTrue(*FString::Printf(TEXT("%s: the same fish actor all fight long"), *Name), bSameFish);

			// The fight really ended the way this case is about (else the case proves nothing).
			const FLureFishingNetState& Net = Fishing->GetNetState();
			const ELureFightOutcome Outcome = Fishing->GetFightNet().Outcome;
			switch (Case)
			{
			case EOutcomeCase::Landed:
				Test.TestEqual(*FString::Printf(TEXT("%s: result"), *Name), LureFightQA::ResultName(Net.LastResult), LureFightQA::ResultName(ELureFishingResult::Landed));
				break;
			case EOutcomeCase::Snapped:
				Test.TestEqual(*FString::Printf(TEXT("%s: outcome"), *Name), LureFightQA::OutcomeName(Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Snapped));
				break;
			case EOutcomeCase::Spooled:
				Test.TestEqual(*FString::Printf(TEXT("%s: outcome"), *Name), LureFightQA::OutcomeName(Outcome), LureFightQA::OutcomeName(ELureFightOutcome::Spooled));
				break;
			case EOutcomeCase::ThrewHook:
				Test.TestEqual(*FString::Printf(TEXT("%s: outcome"), *Name), LureFightQA::OutcomeName(Outcome), LureFightQA::OutcomeName(ELureFightOutcome::ThrewHook));
				break;
			default:
				Test.TestEqual(*FString::Printf(TEXT("%s: result"), *Name), LureFightQA::ResultName(Net.LastResult), LureFightQA::ResultName(ELureFishingResult::Lost));
				break;
			}

			Fishing->AuthoritySetReeling(false);
			if (Case == EOutcomeCase::Landed)
			{
				World.Tick(1);
				Test.TestEqual(*FString::Printf(TEXT("%s: OnFightFishLanded once"), *Name), Landed - LandedBefore, 1);
				if (LandedRecords.Num() > 0)
				{
					Test.TestTrue(*FString::Printf(TEXT("%s: the landed record is the hooked fish"), *Name), LureFightQA::SameFish(LandedRecords.Last(), Bonefish));
					Test.TestTrue(*FString::Printf(TEXT("%s: landed by this player"), *Name), LandedBy.Last() == Fishing);
				}
				Test.TestTrue(*FString::Printf(TEXT("%s: the fight fish is destroyed after the hand-off, or is the hanging fish's look (T-030)"), *Name),
					LandedFishHandedOff(Weak, Character));
				ClearHangingCatch(Character); // T-030: off the hook before the next cast; its look goes with it
				Test.TestFalse(*FString::Printf(TEXT("%s: the fight fish is gone once the hanging fish is"), *Name), Weak.IsValid());
			}
			else
			{
				World.Tick(1); // the end is seen within a frame (the one-frame order is Placement.UsesThisFramesFightState)
				Test.TestEqual(*FString::Printf(TEXT("%s: no landed event"), *Name), Landed - LandedBefore, 0);
				Test.TestNull(*FString::Printf(TEXT("%s: no longer this player's fight fish"), *Name), Visuals->FindFish(Fishing));
				if (Test.TestTrue(*FString::Printf(TEXT("%s: the lost fish is still there, swimming away"), *Name), Weak.IsValid()))
				{
					Test.TestTrue(*FString::Printf(TEXT("%s: escaping"), *Name), Weak->GetPhase() == EFightFishPhase::Escaping);
					Test.TestEqual(*FString::Printf(TEXT("%s: escaping plays SwimFast"), *Name), RoleName(Weak->GetAnimState().Role), RoleName(EFishAnimRole::SwimFast));
					Test.TestEqual(*FString::Printf(TEXT("%s: counted while it swims away"), *Name), Visuals->GetNumFish(), 1);
				}
				const float EscapeTime = Visuals->GetVisualRow().EscapeTime;
				Test.TestTrue(*FString::Printf(TEXT("%s: removed after EscapeTime"), *Name),
					World.TickUntil([&Weak]() { return !Weak.IsValid(); }, FMath::CeilToInt((EscapeTime + 0.25f) / Dt)));
				Test.TestEqual(*FString::Printf(TEXT("%s: still no landed event"), *Name), Landed - LandedBefore, 0);
			}
			World.Tick(2);
			Test.TestEqual(*FString::Printf(TEXT("%s: no fish left (subsystem)"), *Name), Visuals->GetNumFish(), 0);
			Test.TestEqual(*FString::Printf(TEXT("%s: no fish actor left (world)"), *Name), CountFishActors(World.World), 0);
			return true;
		}
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS

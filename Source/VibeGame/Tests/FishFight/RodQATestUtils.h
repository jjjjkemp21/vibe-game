// QA-owned helpers for the independent T-028 tests (Project.Fishing.Fight.Rod.QA.*), written by the qa-engineer (2026-09-23).
// Black-box: expectations come from docs/specs/reel-fight-rules.md "Rod steering and reel speed (T-028)", GAME_DESIGN.md
// "Fight controls" / "Fighting", the formula comment in Fishing/FishFight.h and the contract comments in Fishing/LureRodControl.h,
// Fishing/LureFishingComponent.h and Fishing/FishFightTypes.h, never from the implementation. Builds on the T-007 fight QA
// fixtures (FightQATestUtils.h: worlds, tables from data/tables/, the roll pipeline, FRepLayout replication).
// Everything is in namespace LureRodQA (unity builds merge .cpp files: no file-scope using-directives anywhere).
// Every fixture UObject is held by TStrongObjectPtr (UWorld::Tick can collect garbage in the middle of a long run).

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FightQATestUtils.h"
#include "Camera/CameraComponent.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LureWaterVolume.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "Fishing/LureRodControl.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include <limits>

namespace LureRodQA
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	inline float QNaN() { return std::numeric_limits<float>::quiet_NaN(); }
	inline float QInf() { return std::numeric_limits<float>::infinity(); }

	/** The fight input of one step: the reel button, the rod (pitch + = pulled back, yaw + = right of the line) and the reel step. */
	inline FLureFightInput In(bool bReel, float Pitch = 0.f, float Yaw = 0.f, int32 Step = INDEX_NONE)
	{
		FLureFightInput Input;
		Input.bReeling = bReel;
		Input.RodPitch = Pitch;
		Input.RodYaw = Yaw;
		Input.ReelStep = Step;
		return Input;
	}

	/** A move with a sideways share. RandomSide off: its side sign is +1, so Side < 0 runs LEFT and Side > 0 runs RIGHT. */
	inline FLureFightMove SideMove(const TCHAR* Id, float Pull, float Speed, float Away, float Side, float Duration = 1000.f, bool bRandomSide = false)
	{
		FLureFightMove Move = LureFightQA::MakeMove(Id, Pull, Speed, Away, Duration);
		Move.Side = Side;
		Move.RandomSide = bRandomSide;
		return Move;
	}

	/** The spec's run direction, restated: sign(Side x SideSign) when |Side| >= SideMinShare (and Side != 0), else 0; no move = 0. */
	inline int32 ExpectedRunDir(const FLureFightMove* Move, float SideSign, float SideMinShare)
	{
		if (!Move || !FMath::IsFinite(Move->Side))
		{
			return 0;
		}
		const float Side = FMath::Clamp(Move->Side, -1.f, 1.f);
		const float Lateral = Side * (SideSign < 0.f ? -1.f : 1.f);
		if (Lateral == 0.f || FMath::Abs(Side) < FMath::Max(0.f, SideMinShare))
		{
			return 0;
		}
		return Lateral > 0.f ? 1 : -1;
	}

	/** A rod axis as the fight reads it: clamped to [-1, 1], anything non-finite is the neutral 0. */
	inline float Unit(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.f, 1.f) : 0.f;
	}

	// ------------------------------------------------------------------------------------------------------------
	// HUD text
	// ------------------------------------------------------------------------------------------------------------

	inline TArray<FString> Lines(const FString& Text)
	{
		TArray<FString> Out;
		Text.ParseIntoArrayLines(Out, /*bCullEmpty*/ true);
		return Out;
	}

	/** The first line that starts with Prefix (empty if none). */
	inline FString FindLine(const FString& Text, const TCHAR* Prefix)
	{
		for (const FString& Line : Lines(Text))
		{
			if (Line.StartsWith(Prefix))
			{
				return Line;
			}
		}
		return FString();
	}

	inline bool HasLine(const FString& Text, const TCHAR* Prefix)
	{
		return !FindLine(Text, Prefix).IsEmpty();
	}

	// ------------------------------------------------------------------------------------------------------------
	// Input
	// ------------------------------------------------------------------------------------------------------------

	/** Runs Input's bindings of Action for Event, like the player pressing (Started) the key. Returns how many bindings ran. */
	inline int32 Fire(const UEnhancedInputComponent* Input, const UInputAction* Action, ETriggerEvent Event)
	{
		if (!Input || !Action)
		{
			return 0;
		}
		struct FInstance : public FInputActionInstance
		{
			FInstance(const UInputAction* InAction, ETriggerEvent InEvent) : FInputActionInstance(InAction)
			{
				TriggerEvent = InEvent;
				Value = FInputActionValue(InEvent != ETriggerEvent::Completed);
			}
		};
		const FInstance Instance(Action, Event);
		int32 Ran = 0;
		// Copy the binding pointers first: a handler must not invalidate the array we walk.
		TArray<FEnhancedInputActionEventBinding*> Bindings;
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				Bindings.Add(Binding.Get());
			}
		}
		for (FEnhancedInputActionEventBinding* Binding : Bindings)
		{
			Binding->Execute(Instance);
			++Ran;
		}
		return Ran;
	}

	// ------------------------------------------------------------------------------------------------------------
	// Fixture tables built in code (rows copied from the shipped text sources, then changed)
	// ------------------------------------------------------------------------------------------------------------

	/** A DT_FightPattern table with one row (the Bonefish's FightPatternId is "Run", so a Bonefish fights with this row). */
	inline TStrongObjectPtr<UDataTable> PatternTable(const FLureFightPatternRow& Row, FName RowName = TEXT("Run"))
	{
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = FLureFightPatternRow::StaticStruct();
		Table->AddRow(RowName, Row);
		return Table;
	}

	/** A DT_Gear table: every shipped row, with the starter line's (Line_Mono) strength and spool changed when > 0. */
	inline TStrongObjectPtr<UDataTable> GearTable(const UDataTable* Shipped, float LineStrength = 0.f, float SpoolLength = 0.f)
	{
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = FLureGearRow::StaticStruct();
		if (Shipped)
		{
			for (const TPair<FName, uint8*>& Pair : Shipped->GetRowMap())
			{
				FLureGearRow Row = *reinterpret_cast<const FLureGearRow*>(Pair.Value);
				if (Pair.Key == FName(TEXT("Line_Mono")))
				{
					Row.LineStrength = LineStrength > 0.f ? LineStrength : Row.LineStrength;
					Row.SpoolLength = SpoolLength > 0.f ? SpoolLength : Row.SpoolLength;
				}
				Table->AddRow(Pair.Key, Row);
			}
		}
		return Table;
	}

	/** A DT_FishFight table with one row under the settings' row name. */
	inline TStrongObjectPtr<UDataTable> FightTable(const FLureFishFightRow& Row)
	{
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = FLureFishFightRow::StaticStruct();
		Table->AddRow(GetDefault<ULureFishingSettings>()->FishFightRow, Row);
		return Table;
	}

	/** A one-move pattern (the fish keeps doing Move; its opening move). */
	inline FLureFightPatternRow OneMove(const FLureFightMove& Move)
	{
		return LureFightQA::MakePattern({ Move }, Move.Id);
	}

	// ------------------------------------------------------------------------------------------------------------
	// A standalone owner: a local player controller possessing a character on the T-006/T-007 test dock
	// (in a standalone world the owning client is also the server, so the owner's RPCs run right away)
	// ------------------------------------------------------------------------------------------------------------

	struct FOwner
	{
		ALurePlayerCharacter* Character = nullptr;
		APlayerController* Controller = nullptr;
		ULureFishingComponent* Fishing = nullptr;

		bool Create(FAutomationTestBase& Test, LureFightQA::FWorld& World, const FishQA::FTables& Fish, const UDataTable* Gear, const UDataTable* Patterns,
			const UDataTable* Fight, const FVector& Feet = LureFightQA::StandAt())
		{
			Character = World.Spawn(Feet);
			Controller = World.World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("QA rig: character"), Character) || !Test.TestNotNull(TEXT("QA rig: player controller"), Controller))
			{
				return false;
			}
			Controller->SetAsLocalPlayerController();
			Controller->Possess(Character);
			World.Tick(10);
			Fishing = LureFightQA::SetUpFishing(Character, Fish, Gear, Patterns, Fight);
			return Test.TestNotNull(TEXT("QA rig: fishing component"), Fishing) && Test.TestTrue(TEXT("QA rig: the character is locally controlled"), Character->IsLocallyControlled());
		}

		/** Possesses another character (a respawn): the fishing pointer follows. */
		void Repossess(ALurePlayerCharacter* NewCharacter)
		{
			if (Controller)
			{
				Controller->UnPossess();
				Controller->Possess(NewCharacter);
			}
			Character = NewCharacter;
			Fishing = NewCharacter ? NewCharacter->GetFishing() : nullptr;
		}

		void Release()
		{
			if (IsValid(Controller))
			{
				Controller->UnPossess();
			}
		}

		/** DoLook(Yaw, Pitch) on the character. True if the view took exactly that input (the controller's rotation input), false if it took none. */
		bool LookTurnsTheView(float YawDegrees, float PitchDegrees, bool* bOutPartial = nullptr) const
		{
			Controller->RotationInput = FRotator::ZeroRotator;
			Character->DoLook(YawDegrees, PitchDegrees);
			const FRotator Took = Controller->RotationInput;
			Controller->RotationInput = FRotator::ZeroRotator;
			const bool bAll = FMath::IsNearlyEqual(static_cast<float>(Took.Yaw), YawDegrees, 1.0e-4f) && FMath::IsNearlyEqual(static_cast<float>(Took.Pitch), PitchDegrees, 1.0e-4f);
			if (bOutPartial)
			{
				*bOutPartial = !bAll && !Took.IsNearlyZero(1.0e-6);
			}
			return bAll;
		}

		/** Nothing moves the control rotation over Frames (the fight's camera does not hold the view). */
		bool ViewIsFree(LureFightQA::FWorld& World, int32 Frames = 30) const
		{
			const FRotator Set(-7.f, 77.f, 0.f);
			Controller->SetControlRotation(Set);
			World.Tick(Frames);
			return Controller->GetControlRotation().Equals(Set, 0.01f);
		}
	};

	/** Casts along +X with Charge, hooks Fish on the server and ticks once: the fight runs (the owner steers). */
	inline bool HookAndFight(FAutomationTestBase& Test, LureFightQA::FWorld& World, ULureFishingComponent* Fishing, const FFishInstance& Fish, float Charge = 0.5f)
	{
		if (!LureFightQA::CastAndWait(Test, World, Fishing, Charge) || !Test.TestTrue(TEXT("the server hooks the fish"), Fishing->AuthorityHookFish(Fish)))
		{
			return false;
		}
		World.Tick(1);
		return Test.TestTrue(TEXT("the fight runs (Hooked, FightNet.bActive)"), Fishing->GetFishingState() == ELureFishingState::Hooked && Fishing->GetFightNet().bActive);
	}

	/** Engine water (a T-026 ALureWaterVolume) with its surface at z = 0 over +-3500 cm around the dock, Depth deep. */
	inline ALureWaterVolume* AddWaterVolume(UWorld* World, float Depth)
	{
		const FTransform Transform(FRotator::ZeroRotator, FVector::ZeroVector);
		ALureWaterVolume* Volume = World->SpawnActorDeferred<ALureWaterVolume>(ALureWaterVolume::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Volume)
		{
			return nullptr;
		}
		Volume->SurfaceHalfSize = FVector2D(3500.0, 3500.0);
		Volume->WaterDepth = Depth;
		Volume->FinishSpawning(Transform);
		return Volume;
	}

	inline float Median(TArray<float> Values)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

// QA-owned helpers for the independent reel fight tests (Project.Fishing.Fight.QA.*, T-007), written by the qa-engineer.
// Black-box: expectations come from docs/specs/reel-fight-rules.md, the formula comment in Fishing/FishFight.h and the
// contract comments in Fishing/FishFightTypes.h, never from the implementation. Tables come from the text sources in
// data/tables/ (never the binary /Game/Data assets) or from fixture text built in the tests.
// Every fixture UObject is held by TStrongObjectPtr: UWorld::Tick runs ConditionalCollectGarbage, so a raw pointer can be
// collected in the middle of a long full-suite run.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/FishQATestHelpers.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Fishing/FishFight.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Net/RepLayout.h"
#include "Tests/AutomationCommon.h"
#include "UObject/CoreNet.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace LureFightQA
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float WorldDt = 1.f / 60.f;
	constexpr float DockTop = 100.f;
	constexpr float DockEdgeX = 400.f;

	inline FVector StandAt() { return FVector(350.f, 0.f, DockTop); }

	inline FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
	}

	inline FString OutcomeName(ELureFightOutcome Outcome)
	{
		return StaticEnum<ELureFightOutcome>()->GetNameStringByValue(static_cast<int64>(Outcome));
	}

	inline FString ResultName(ELureFishingResult Result)
	{
		return StaticEnum<ELureFishingResult>()->GetNameStringByValue(static_cast<int64>(Result));
	}

	inline FString StateName(ELureFishingState State)
	{
		return StaticEnum<ELureFishingState>()->GetNameStringByValue(static_cast<int64>(State));
	}

	// ------------------------------------------------------------------------------------------------------------
	// Tables from text
	// ------------------------------------------------------------------------------------------------------------

	inline FString SourcePath(const TCHAR* FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
	}

	inline bool ReadSource(FAutomationTestBase& Test, const TCHAR* FileName, FString& OutText)
	{
		const bool bOk = FFileHelper::LoadFileToString(OutText, *SourcePath(FileName));
		if (!bOk)
		{
			Test.AddError(FString::Printf(TEXT("can't read data/tables/%s"), FileName));
		}
		return bOk;
	}

	/** A transient table from CSV or JSON text; returns the engine's import problems (the table is always created). */
	inline TArray<FString> MakeTable(TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const FString& Text, bool bJson)
	{
		Out.Reset(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Out->RowStruct = RowStruct;
		return bJson ? Out->CreateTableFromJSONString(Text) : Out->CreateTableFromCSVString(Text);
	}

	/** Same, and every import problem is a test error. */
	inline bool MakeTableChecked(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const FString& Text, bool bJson, const TCHAR* What)
	{
		const TArray<FString> Problems = MakeTable(Out, RowStruct, Text, bJson);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), What, *Problem));
		}
		return Problems.Num() == 0;
	}

	/** The three T-007 tables from data/tables/ (the shipped data). */
	struct FFightTables
	{
		TStrongObjectPtr<UDataTable> Gear;
		TStrongObjectPtr<UDataTable> Patterns;
		TStrongObjectPtr<UDataTable> Fight;
		FString GearCsv;
		FString PatternJson;
		FString FightCsv;

		bool Load(FAutomationTestBase& Test)
		{
			bool bOk = ReadSource(Test, TEXT("DT_Gear.csv"), GearCsv);
			bOk &= ReadSource(Test, TEXT("DT_FightPattern.json"), PatternJson);
			bOk &= ReadSource(Test, TEXT("DT_FishFight.csv"), FightCsv);
			if (!bOk)
			{
				return false;
			}
			bOk &= MakeTableChecked(Test, Gear, FLureGearRow::StaticStruct(), GearCsv, false, TEXT("DT_Gear.csv"));
			bOk &= MakeTableChecked(Test, Patterns, FLureFightPatternRow::StaticStruct(), PatternJson, true, TEXT("DT_FightPattern.json"));
			bOk &= MakeTableChecked(Test, Fight, FLureFishFightRow::StaticStruct(), FightCsv, false, TEXT("DT_FishFight.csv"));
			if (bOk && !Tuning())
			{
				Test.AddError(TEXT("DT_FishFight.csv has no row named by ULureFishingSettings::FishFightRow"));
				bOk = false;
			}
			return bOk;
		}

		const FLureFishFightRow* Tuning() const
		{
			return Fight.IsValid() ? Fight->FindRow<FLureFishFightRow>(GetDefault<ULureFishingSettings>()->FishFightRow, TEXT("FightQA"), false) : nullptr;
		}

		const FLureFightPatternRow* Pattern(FName Id) const
		{
			return Patterns.IsValid() ? Patterns->FindRow<FLureFightPatternRow>(Id, TEXT("FightQA"), false) : nullptr;
		}

		const FLureGearRow* Item(FName Id) const
		{
			return Gear.IsValid() ? Gear->FindRow<FLureGearRow>(Id, TEXT("FightQA"), false) : nullptr;
		}

		FLureGearStats Resolve(FName Rod, FName Line, FName Hook) const
		{
			FLureGearLoadout Loadout;
			Loadout.Rod = Rod;
			Loadout.Line = Line;
			Loadout.Hook = Hook;
			return FLureGear::Resolve(Gear.Get(), Loadout);
		}

		FLureGearStats Starter() const
		{
			const FLureGearLoadout& Defaults = GetDefault<ULureFishingSettings>()->DefaultLoadout;
			return Resolve(Defaults.Rod, Defaults.Line, Defaults.Hook);
		}
	};

	// ------------------------------------------------------------------------------------------------------------
	// Pure-simulation fixtures (every number exactly representable where a test depends on it)
	// ------------------------------------------------------------------------------------------------------------

	inline FLureGearStats MakeGear(float RodPower, float ReelSpeed, float Drag, float LineStrength, float SpoolLength, float HookSecurity)
	{
		FLureGearStats Gear;
		Gear.RodPower = RodPower;
		Gear.ReelSpeed = ReelSpeed;
		Gear.Drag = Drag;
		Gear.LineStrength = LineStrength;
		Gear.SpoolLength = SpoolLength;
		Gear.HookSecurity = HookSecurity;
		return Gear;
	}

	inline FLureFightMove MakeMove(const TCHAR* Id, float Pull, float Speed, float Away, float Duration = 1000.f, bool bRest = false, float Weight = 1.f, float AggressionWeight = 0.f)
	{
		FLureFightMove Move;
		Move.Id = FName(Id);
		Move.Label = FText::FromString(Id);
		Move.Weight = Weight;
		Move.AggressionWeight = AggressionWeight;
		Move.DurationMin = Duration;
		Move.DurationMax = Duration;
		Move.Pull = Pull;
		Move.Speed = Speed;
		Move.Away = Away;
		Move.Rest = bRest;
		return Move;
	}

	inline FLureFightPatternRow MakePattern(const TArray<FLureFightMove>& Moves, FName Opening)
	{
		FLureFightPatternRow Pattern;
		Pattern.DisplayName = FText::FromString(TEXT("QA pattern"));
		Pattern.Moves = Moves;
		Pattern.OpeningMove = Opening;
		return Pattern;
	}

	inline FLureFightFish MakeFightFish(float BasePull, float BaseSpeed, float StaminaPool, float Aggression = 0.f, float RestScale = 1.f)
	{
		FLureFightFish Fish;
		Fish.BasePull = BasePull;
		Fish.BaseSpeed = BaseSpeed;
		Fish.StaminaPool = StaminaPool;
		Fish.Aggression = Aggression;
		Fish.RestScale = RestScale;
		return Fish;
	}

	/**
	 *  Timing fixture: the shipped tuning, but the tension follows its target within the step (rise/fall time 0), the pull
	 *  never tires (TiredPull 1) and nothing recovers, so the time over/under a threshold is exactly a whole number of steps.
	 */
	inline FLureFishFightRow InstantTuning(const FLureFishFightRow& Shipped, int32 SimRate)
	{
		FLureFishFightRow Tuning = Shipped;
		Tuning.TensionRiseTime = 0.f;
		Tuning.TensionFallTime = 0.f;
		Tuning.TiredPull = 1.f;
		Tuning.StaminaRecovery = 0.f;
		Tuning.ExhaustedStamina = 0.f;
		Tuning.SimRate = SimRate;
		return Tuning;
	}

	/** A fight on one never-ending move, with a fish that never tires (for timing and boundary tests). */
	inline FLureFightState SteadyFight(const FLureFishFightRow& Tuning, float BasePull, const FLureFightMove& Move, const FLureGearStats& Gear, float StartLineOut = 2000.f, float BaseSpeed = 0.f)
	{
		FLureFightState State;
		FLureFight::Begin(State, MakeFightFish(BasePull, BaseSpeed, 1.0e9f), MakePattern({ Move }, Move.Id), TEXT("QA_Steady"), Gear, Tuning, 1234, StartLineOut);
		return State;
	}

	inline FLureFightInput Input(bool bReeling)
	{
		FLureFightInput In;
		In.bReeling = bReeling;
		return In;
	}

	/** Steps until the fight ends (or MaxSteps); returns the number of steps taken by this call. */
	inline int32 StepUntilOver(FLureFightState& State, bool bReeling, int32 MaxSteps)
	{
		int32 Taken = 0;
		while (!State.IsOver() && Taken < MaxSteps)
		{
			FLureFight::Step(State, Input(bReeling));
			++Taken;
		}
		return Taken;
	}

	inline bool AllFinite(const FLureFightState& S)
	{
		for (const float Value : { S.LineOut, S.Tension, S.Stamina, S.OverTime, S.SlackTime, S.Depth, S.SideDeg, S.Pull, S.Speed, S.Elapsed, S.MoveTimeLeft })
		{
			if (!FMath::IsFinite(Value))
			{
				return false;
			}
		}
		return true;
	}

	/** Scripted players: Hold reels all the time; Careful reels below 70 % of the line, eases off above 90 %, decides every 0.3 s. */
	enum class EPlayer : uint8 { Hold, Careful, Never };

	inline ELureFightOutcome RunPlayer(FLureFightState& State, EPlayer Player, float MaxSeconds = 180.f)
	{
		bool bReel = Player == EPlayer::Hold;
		const float Step = FLureFight::StepSeconds(State.Tuning);
		float SinceDecision = 1000.f;
		while (!State.IsOver() && State.Elapsed < MaxSeconds)
		{
			if (Player == EPlayer::Careful)
			{
				SinceDecision += Step;
				if (SinceDecision >= 0.3f - 1.0e-4f)
				{
					SinceDecision = 0.f;
					const float Tension01 = State.Tension / FMath::Max(1.0e-3f, State.Gear.LineStrength);
					bReel = bReel ? Tension01 < 0.9f : Tension01 < 0.7f;
				}
			}
			FLureFight::Step(State, Input(bReel));
		}
		return State.Outcome;
	}

	// ------------------------------------------------------------------------------------------------------------
	// Fish from the one roll pipeline
	// ------------------------------------------------------------------------------------------------------------

	/** A fish of Species from FFishRoll::Roll with forced rarity, no modifiers (unless given) and a forced weight fraction. */
	inline bool RollFish(FAutomationTestBase& Test, const FishQA::FTables& Tables, FName Species, FName Rarity, float WeightFraction, int32 Seed, FFishInstance& Out,
		const TArray<FName>& Modifiers = TArray<FName>())
	{
		FFishRollContext Context;
		Context.SpeciesId = Species;
		Context.Seed = Seed;
		Context.ForcedRarityId = Rarity;
		Context.bForceModifiers = true;
		Context.ForcedModifierIds = Modifiers;
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = WeightFraction;
		Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
		Context.TimeOfDayHours = 12.f;
		const bool bOk = FFishRoll::Roll(Tables.Get(), Context, Out);
		if (!bOk)
		{
			Test.AddError(FString::Printf(TEXT("FFishRoll::Roll failed for %s %s"), *Species.ToString(), *Rarity.ToString()));
		}
		return bOk;
	}

	inline bool SameFish(const FFishInstance& A, const FFishInstance& B)
	{
		return FFishInstance::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
	}

	inline bool SameFightFish(const FLureFightFish& A, const FLureFightFish& B)
	{
		return A.BasePull == B.BasePull && A.BaseSpeed == B.BaseSpeed && A.StaminaPool == B.StaminaPool && A.Aggression == B.Aggression
			&& A.RestScale == B.RestScale && A.LevelMultiplier == B.LevelMultiplier && A.DifficultyRating == B.DifficultyRating;
	}

	// ------------------------------------------------------------------------------------------------------------
	// Log capture
	// ------------------------------------------------------------------------------------------------------------

	class FLogCapture : public FOutputDevice
	{
	public:
		explicit FLogCapture(FName InCategory) : Category(InCategory) { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& InCategory) override
		{
			if (InCategory == Category)
			{
				FScopeLock Lock(&Mutex);
				Lines.Add(V);
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		int32 Count(const TCHAR* Needle)
		{
			GLog->Flush();
			FScopeLock Lock(&Mutex);
			int32 N = 0;
			for (const FString& Line : Lines)
			{
				N += Line.Contains(Needle) ? 1 : 0;
			}
			return N;
		}
	private:
		FName Category;
		FCriticalSection Mutex;
		TArray<FString> Lines;
	};

	// ------------------------------------------------------------------------------------------------------------
	// Test world: an 8 x 8 m dock 1 m above a Lure.Water surface at z = 0 (the T-006 test layout)
	// ------------------------------------------------------------------------------------------------------------

	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		TStrongObjectPtr<UDataTable> Movement;

		bool Create(FAutomationTestBase& Test)
		{
			FString Csv;
			if (!ReadSource(Test, TEXT("DT_Movement.csv"), Csv) || !MakeTableChecked(Test, Movement, FLureMovementRow::StaticStruct(), Csv, false, TEXT("DT_Movement.csv")))
			{
				return false;
			}
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			AddBox(FVector(0.f, 0.f, DockTop * 0.5f), FVector(DockEdgeX, DockEdgeX, DockTop * 0.5f));
			AActor* Water = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Water, TEXT("Water"));
			Box->SetBoxExtent(FVector(20000.f, 20000.f, 50.f), false);
			Box->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Water->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(FVector(0.f, 0.f, -50.f));
			Water->Tags.Add(GetDefault<ULureFishingSettings>()->WaterTag);
			return World != nullptr;
		}

		void AddBox(const FVector& Center, const FVector& Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
			Box->SetBoxExtent(Extent, false);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(Center);
		}

		void AddSpot(const FVector& Location, const TArray<FString>& KeyValues)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocation(Location);
			Actor->Tags.Add(GetDefault<ULureFishingSettings>()->FishingSpotTag);
			for (const FString& KeyValue : KeyValues)
			{
				Actor->Tags.Add(FName(*KeyValue));
			}
		}

		/** The shore spot the starter Bonefish bites at (noon, shrimp). */
		void AddShoreSpot()
		{
			AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=qa_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200") });
		}

		ALurePlayerCharacter* Spawn(const FVector& Feet)
		{
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
			const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.f;
			const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, HalfHeight + 2.15f));
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character)
			{
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Movement.Get());
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			Character->FinishSpawning(Transform);
			return Character;
		}

		void Tick(int32 Frames, float DeltaTime = WorldDt)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(DeltaTime);
			}
		}

		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, float DeltaTime = WorldDt)
		{
			for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
			{
				if (Predicate())
				{
					return true;
				}
				Wrapper.TickTestWorld(DeltaTime);
			}
			return Predicate();
		}
	};

	/** Bite 0.2 s after the bobber lands, no nibbles, a 0.8 s hook window, the reel fight on (AutoLandDelay 0). */
	inline FLureFishingRow QuickProfile()
	{
		FLureFishingRow Row = FLureFishingRules::GetFallbackRow();
		Row.BiteWaitMin = 0.2f;
		Row.BiteWaitMax = 0.2f;
		Row.NibblesMin = 0;
		Row.NibblesMax = 0;
		Row.HookWindow = 0.8f;
		Row.AutoLandDelay = 0.f;
		return Row;
	}

	/** Server-side setup of a character's fishing: quick profile, the given fish and fight tables, fixed seed and time. */
	inline ULureFishingComponent* SetUpFishing(ALurePlayerCharacter* Character, const FishQA::FTables& Fish, const UDataTable* Gear, const UDataTable* Patterns,
		const UDataTable* Fight, float Hours = 12.f, int32 Seed = 1234)
	{
		ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
		if (Fishing)
		{
			Fishing->SetFishingProfile(QuickProfile());
			Fishing->SetFishTables(Fish.Get());
			Fishing->SetRandomSeed(Seed);
			Fishing->TimeOfDayOverride = Hours;
			Fishing->SetFightTables(Gear, Patterns, Fight);
		}
		return Fishing;
	}

	/** Casts along +X (half charge by default; 0 = the shortest cast, a short fight) and ticks until the bobber is in the water (Waiting). */
	inline bool CastAndWait(FAutomationTestBase& Test, FWorld& World, ULureFishingComponent* Fishing, float Charge = 0.5f)
	{
		if (!Test.TestTrue(TEXT("the cast starts"), Fishing && Fishing->AuthorityCast(Charge, 0.f)))
		{
			return false;
		}
		return Test.TestTrue(TEXT("the bobber lands in the water (Waiting)"),
			World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	}

	// ------------------------------------------------------------------------------------------------------------
	// Replication through the engine's replication layout (FRepLayout: the path struct properties take on the wire)
	// ------------------------------------------------------------------------------------------------------------

	/** Serializes Struct at In through FRepLayout and reads it into Out. OutBits = bits on the wire. */
	inline bool NetRoundTrip(FAutomationTestBase& Test, UScriptStruct* Struct, void* In, void* Out, int64& OutBits)
	{
		const TSharedPtr<FRepLayout> Layout = FRepLayout::CreateFromStruct(Struct, nullptr, ECreateRepLayoutFlags::None);
		if (!Test.TestTrue(FString::Printf(TEXT("FRepLayout for %s"), *Struct->GetName()), Layout.IsValid()))
		{
			return false;
		}
		FNetBitWriter Writer(nullptr, 64 * 1024 * 8);
		bool bHasUnmapped = false;
		Layout->SerializePropertiesForStruct(Struct, Writer, nullptr, In, bHasUnmapped);
		OutBits = Writer.GetNumBits();
		if (!Test.TestFalse(FString::Printf(TEXT("%s: the writer did not overflow"), *Struct->GetName()), Writer.IsError()))
		{
			return false;
		}
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		bool bReadUnmapped = false;
		Layout->SerializePropertiesForStruct(Struct, Reader, nullptr, Out, bReadUnmapped);
		return Test.TestFalse(FString::Printf(TEXT("%s: the reader did not overflow"), *Struct->GetName()), Reader.IsError());
	}

	/** Calls Object's RepNotify for Property with the previous value (what the net driver does after a change). */
	inline void CallRepNotify(UObject* Object, const FProperty* Property, const void* PreviousValue)
	{
		UFunction* Function = Object->FindFunction(Property->RepNotifyFunc);
		if (!Function)
		{
			return;
		}
		TArray<uint8> Parms;
		Parms.AddZeroed(FMath::Max<int32>(16, Function->ParmsSize));
		FProperty* First = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Parms.GetData());
			if (!First)
			{
				First = *It;
			}
		}
		if (First)
		{
			First->CopyCompleteValue(First->ContainerPtrToValuePtr<void>(Parms.GetData()), PreviousValue);
		}
		Object->ProcessEvent(Function, Parms.GetData());
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms.GetData());
		}
	}

	/**
	 *  "Replicates" Server's fishing component to Client: every replicated struct property of ULureFishingComponent goes
	 *  through FRepLayout (bits written by the server object, read into the client object); RepNotifies fire for the
	 *  properties that changed, with their previous value, after all values are in (like one net update).
	 */
	inline bool ReplicateFishing(FAutomationTestBase& Test, ULureFishingComponent* Server, ULureFishingComponent* Client)
	{
		struct FChanged { const FProperty* Property; TArray<uint8> Previous; };
		TArray<FChanged> Changed;
		bool bOk = true;
		for (TFieldIterator<FProperty> It(ULureFishingComponent::StaticClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Net) || Property->GetOwnerClass() != ULureFishingComponent::StaticClass())
			{
				continue;
			}
			void* ClientValue = Property->ContainerPtrToValuePtr<void>(Client);
			void* ServerValue = Property->ContainerPtrToValuePtr<void>(Server);
			TArray<uint8> Previous;
			Previous.AddZeroed(Property->GetSize());
			Property->InitializeValue(Previous.GetData());
			Property->CopyCompleteValue(Previous.GetData(), ClientValue);
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				int64 Bits = 0;
				bOk &= NetRoundTrip(Test, StructProperty->Struct, ServerValue, ClientValue, Bits);
			}
			else
			{
				Property->CopyCompleteValue(ClientValue, ServerValue);
			}
			if (Property->HasAnyPropertyFlags(CPF_RepNotify) && !Property->Identical(Previous.GetData(), ClientValue))
			{
				Changed.Add({ Property, MoveTemp(Previous) });
			}
			else
			{
				Property->DestroyValue(Previous.GetData());
			}
		}
		for (FChanged& Change : Changed)
		{
			CallRepNotify(Client, Change.Property, Change.Previous.GetData());
			Change.Property->DestroyValue(Change.Previous.GetData());
		}
		return bOk;
	}

	/** Writable access to a protected replicated struct of the component (a hacked client, or a test fixture). */
	template <typename T>
	T* ReplicatedField(ULureFishingComponent* Component, const TCHAR* Name)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(ULureFishingComponent::StaticClass(), FName(Name));
		return (Property && Property->Struct == T::StaticStruct()) ? Property->ContainerPtrToValuePtr<T>(Component) : nullptr;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

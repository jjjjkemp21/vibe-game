// Lure T-007: reel fight, line tension and gear (implementer's tests; QA adds its own). Project.Fishing.Fight.*
// Data comes from the text sources in data/tables/ (DT_Gear.csv, DT_FightPattern.json, DT_FishFight.csv and the fish JSONs),
// never the binary assets. Worlds are transient FTestWorldWrapper game worlds: a 1 m high dock and a Lure.Water surface at z = 0.
// Rules: docs/specs/reel-fight-rules.md.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fish/FishDataValidator.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Fishing/FishFight.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Game/LureHUD.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Tests/AutomationCommon.h"
#include "Tests/FishQATestHelpers.h"
#include "UObject/EnumProperty.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// Everything stays inside this namespace (no file-scope using-directive: unity builds share translation units).
namespace LureFishFightTest
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = 1.f / 60.f;
	constexpr float DockTop = 100.f;
	constexpr float DockEdgeX = 400.f;
	const FVector StandAt(350.f, 0.f, DockTop);

	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), false);
	}

	FString SourcePath(const TCHAR* FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
	}

	FString OutcomeName(ELureFightOutcome Outcome)
	{
		return StaticEnum<ELureFightOutcome>()->GetNameStringByValue(static_cast<int64>(Outcome));
	}

	FString ResultName(ELureFishingResult Result)
	{
		return StaticEnum<ELureFishingResult>()->GetNameStringByValue(static_cast<int64>(Result));
	}

	FString StateName(ELureFishingState State)
	{
		return StaticEnum<ELureFishingState>()->GetNameStringByValue(static_cast<int64>(State));
	}

	/** Imports a data/tables source into a new transient table; import problems are test errors. */
	bool ImportSource(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const TCHAR* FileName, FString* OutText = nullptr)
	{
		FString Text;
		if (!Test.TestTrue(FString::Printf(TEXT("data/tables/%s loads"), FileName), FFileHelper::LoadFileToString(Text, *SourcePath(FileName))))
		{
			return false;
		}
		Out.Reset(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Out->RowStruct = RowStruct;
		const TArray<FString> Problems = FString(FileName).EndsWith(TEXT(".json")) ? Out->CreateTableFromJSONString(Text) : Out->CreateTableFromCSVString(Text);
		Test.TestEqual(FString::Printf(TEXT("%s import problems (%s)"), FileName, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		if (OutText)
		{
			*OutText = Text;
		}
		return Problems.Num() == 0;
	}

	/** The three T-007 tables from their sources. */
	struct FFightData
	{
		TStrongObjectPtr<UDataTable> Gear;
		TStrongObjectPtr<UDataTable> Patterns;
		TStrongObjectPtr<UDataTable> Fight;
		FString GearCsv;
		FString PatternJson;
		FString FightCsv;

		bool Load(FAutomationTestBase& Test)
		{
			bool bOk = ImportSource(Test, Gear, FLureGearRow::StaticStruct(), TEXT("DT_Gear.csv"), &GearCsv);
			bOk &= ImportSource(Test, Patterns, FLureFightPatternRow::StaticStruct(), TEXT("DT_FightPattern.json"), &PatternJson);
			bOk &= ImportSource(Test, Fight, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight.csv"), &FightCsv);
			return bOk && Tuning() != nullptr;
		}

		const FLureFishFightRow* Tuning() const
		{
			return Fight.IsValid() ? Fight->FindRow<FLureFishFightRow>(TEXT("Default"), TEXT("test"), false) : nullptr;
		}

		const FLureFightPatternRow* Pattern(FName Id) const
		{
			return Patterns.IsValid() ? Patterns->FindRow<FLureFightPatternRow>(Id, TEXT("test"), false) : nullptr;
		}

		FLureGearStats Gear3(FName Rod, FName Line, FName Hook) const
		{
			FLureGearLoadout Loadout;
			Loadout.Rod = Rod;
			Loadout.Line = Line;
			Loadout.Hook = Hook;
			return FLureGear::Resolve(Gear.Get(), Loadout);
		}

		/** The default loadout (Rod_Starter, Line_Mono, Hook_Shrimp). */
		FLureGearStats Starter() const { return Gear3(TEXT("Rod_Starter"), TEXT("Line_Mono"), TEXT("Hook_Shrimp")); }

		/** The shop upgrades (Rod_Reef, Line_Braid, Hook_Squid). */
		FLureGearStats Reef() const { return Gear3(TEXT("Rod_Reef"), TEXT("Line_Braid"), TEXT("Hook_Squid")); }
	};

	/** A fish from the real pipeline with forced rarity, no modifiers and a forced weight (0 = WeightMin, 1 = WeightMax). */
	bool RollFish(FAutomationTestBase& Test, const FishQA::FTables& Tables, FName Species, FName Rarity, float WeightFraction, int32 Seed, FFishInstance& Out)
	{
		FFishRollContext Context;
		Context.SpeciesId = Species;
		Context.Seed = Seed;
		Context.ForcedRarityId = Rarity;
		Context.bForceModifiers = true;
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = WeightFraction;
		Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
		Context.TimeOfDayHours = 12.f;
		return Test.TestTrue(FString::Printf(TEXT("roll %s %s at %.2f of its weight range"), *Species.ToString(), *Rarity.ToString(), WeightFraction), FFishRoll::Roll(Tables.Get(), Context, Out));
	}

	/** Coral Snapper weight fraction at its ReferenceWeight 2.5 kg (range 0.8..7 kg): stats as written in DT_FishSpecies. */
	constexpr float SnapperReferenceFraction = (2.5f - 0.8f) / (7.f - 0.8f);

	/** Starts a pure fight with the shipped tuning. */
	FLureFightState BeginFight(const FFightData& Data, const FFishInstance& Fish, FName PatternId, const FLureGearStats& Gear, int32 Seed, float StartLineOut = 1000.f, int32 PlayerLevel = 1)
	{
		FLureFightState State;
		const FLureFishFightRow& Tuning = *Data.Tuning();
		const FLureFightPatternRow* Pattern = Data.Pattern(PatternId);
		FLureFight::Begin(State, FLureFight::MakeFish(Fish, Tuning, PlayerLevel, GetDefault<UFishSettings>()->LevelScaling),
			Pattern ? *Pattern : FLureFightPatternRow::GetFallbackPattern(), PatternId, Gear, Tuning, Seed, StartLineOut);
		return State;
	}

	/** Scripted players. Hold: reels the whole time. Careful: watches the tension bar (reels below 70 %, eases off above 90 %) and reacts every 0.3 s. */
	enum class EPlayer : uint8 { Hold, Careful, Never };

	bool CarefulDecision(float Tension01, bool bWasReeling)
	{
		return bWasReeling ? Tension01 < 0.9f : Tension01 < 0.7f;
	}

	ELureFightOutcome RunPlayer(FLureFightState& State, EPlayer Player, float MaxSeconds = 180.f)
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
					bReel = CarefulDecision(State.Tension / State.Gear.LineStrength, bReel);
				}
			}
			FLureFightInput Input;
			Input.bReeling = bReel;
			FLureFight::Step(State, Input);
		}
		return State.Outcome;
	}

	/** Captures LogLureFish lines of every verbosity. */
	class FFishLogCapture : public FOutputDevice
	{
	public:
		FFishLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FFishLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName(TEXT("LogLureFish")))
			{
				FScopeLock Lock(&Mutex);
				Lines.Add(V);
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		TArray<FString> Get() { GLog->Flush(); FScopeLock Lock(&Mutex); return Lines; }
	private:
		FCriticalSection Mutex;
		TArray<FString> Lines;
	};

	/** A game world with a dock and water (like the T-006 tests). */
	struct FFightWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		TStrongObjectPtr<UDataTable> Movement;

		bool Create(FAutomationTestBase& Test)
		{
			if (!ImportSource(Test, Movement, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv")))
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

		ALurePlayerCharacter* Spawn(const FVector& Feet)
		{
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
			const float HalfHeight = Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight;
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

		void Tick(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(Dt);
			}
		}

		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames)
		{
			for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
			{
				if (Predicate())
				{
					return true;
				}
				Wrapper.TickTestWorld(Dt);
			}
			return Predicate();
		}
	};

	/** A quick fishing profile (bite 0.2 s after landing, no nibbles, the reel fight on). */
	FLureFishingRow QuickProfile()
	{
		FLureFishingRow Row = FLureFishingRules::GetFallbackRow();
		Row.BiteWaitMin = Row.BiteWaitMax = 0.2f;
		Row.NibblesMin = Row.NibblesMax = 0;
		Row.HookWindow = 0.8f;
		Row.AutoLandDelay = 0.f;
		return Row;
	}

	ULureFishingComponent* SetUp(ALurePlayerCharacter* Character, const FishQA::FTables& Fish, const FFightData& Data, float Hours = 12.f)
	{
		ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
		if (Fishing)
		{
			Fishing->SetFishingProfile(QuickProfile());
			Fishing->SetFishTables(Fish.Get());
			Fishing->SetRandomSeed(1234);
			Fishing->TimeOfDayOverride = Hours;
			Fishing->SetFightTables(Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		}
		return Fishing;
	}

	bool CastAndLand(FAutomationTestBase& Test, FFightWorld& World, ULureFishingComponent* Fishing)
	{
		if (!Test.TestTrue(TEXT("cast starts"), Fishing && Fishing->AuthorityCast(0.5f, 0.f)))
		{
			return false;
		}
		return Test.TestTrue(TEXT("bobber lands (Waiting)"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	}

	/** Copies the component's replicated properties to a client copy and calls its RepNotifies (what the net driver does). */
	void Replicate(ULureFishingComponent* Server, ULureFishingComponent* Client)
	{
		const FLureFishingNetState Previous = Client->GetNetState();
		for (TFieldIterator<FProperty> It(ULureFishingComponent::StaticClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Net) && It->GetOwnerClass() == ULureFishingComponent::StaticClass())
			{
				It->CopyCompleteValue_InContainer(Client, Server);
			}
		}
		struct { FLureFishingNetState PreviousState; } Params{ Previous };
		Client->ProcessEvent(Client->FindFunction(TEXT("OnRep_NetState")), &Params);
		Client->ProcessEvent(Client->FindFunction(TEXT("OnRep_Loadout")), nullptr);
	}

	/** A pattern table from JSON text (for data-only additions in tests). Strong pointer: world ticks can run GC mid-test. */
	TStrongObjectPtr<UDataTable> PatternTableFromJson(FAutomationTestBase& Test, const FString& Json)
	{
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = FLureFightPatternRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromJSONString(Json);
		Test.TestEqual(FString::Printf(TEXT("fixture pattern JSON imports (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		return Table;
	}

	FString MoveJson(const TCHAR* Id, float Pull, float Speed, float Away, float Duration = 100.f, bool bRest = false)
	{
		return FString::Printf(TEXT("{ \"Id\": \"%s\", \"Label\": \"%s\", \"Weight\": 1, \"AggressionWeight\": 0, \"DurationMin\": %g, \"DurationMax\": %g, \"Pull\": %g, \"Speed\": %g, \"Away\": %g, \"Side\": 0, \"RandomSide\": false, \"Down\": 0, \"Rest\": %s }"),
			Id, Id, Duration, Duration, Pull, Speed, Away, bRest ? TEXT("true") : TEXT("false"));
	}

	FString PatternJson(const TCHAR* Name, const TArray<FString>& Moves, const TCHAR* Opening)
	{
		return FString::Printf(TEXT("{ \"Name\": \"%s\", \"DisplayName\": \"%s\", \"OpeningMove\": \"%s\", \"Moves\": [ %s ] }"), Name, Name, Opening, *FString::Join(Moves, TEXT(", ")));
	}

	/** A fight on a single move that never ends by itself (huge stamina), for timing tests. */
	FLureFightState SteadyFight(const FLureFishFightRow& Tuning, float BasePull, float BaseSpeed, const FLureFightMove& Move, const FLureGearStats& Gear, float StartLineOut = 3000.f)
	{
		FLureFightPatternRow Pattern;
		Pattern.Moves.Add(Move);
		Pattern.OpeningMove = Move.Id;
		FLureFightFish Fish;
		Fish.BasePull = BasePull;
		Fish.BaseSpeed = BaseSpeed;
		Fish.StaminaPool = 1.0e9f;
		FLureFightState State;
		FLureFight::Begin(State, Fish, Pattern, TEXT("Test"), Gear, Tuning, 99, StartLineOut);
		return State;
	}

	FLureFightMove Move(const TCHAR* Id, float Pull, float Speed, float Away, float Duration = 1000.f)
	{
		FLureFightMove Out;
		Out.Id = FName(Id);
		Out.Label = FText::FromString(Id);
		Out.Pull = Pull;
		Out.Speed = Speed;
		Out.Away = Away;
		Out.DurationMin = Out.DurationMax = Duration;
		return Out;
	}

// =====================================================================================================================
// Tension math from data
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightTensionMathFromData, "Project.Fishing.Fight.TensionMathFromData", LureFishFightTest::TestFlags)
bool FLureFightTensionMathFromData::RunTest(const FString& Parameters)
{
	FFightData Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	const FLureGearStats Starter = Data.Starter();
	const FLureGearStats Reef = Data.Reef();
	TestFalse(TEXT("the default loadout resolves from DT_Gear (no built-in items)"), Starter.bUsedFallback);
	TestEqual(TEXT("starter rod"), Starter.RodId, FName(TEXT("Rod_Starter")));

	// Step 4: the tension target.
	const float Pull = 4.f;
	TestNearlyEqual(TEXT("reeling: Pull x ReelStrain + RodPower x ReelLoad"), FLureFight::TargetTension(Pull, true, Starter, T), Pull * T.ReelStrain + Starter.RodPower * T.ReelLoad, 1.0e-4f);
	TestNearlyEqual(TEXT("not reeling, pull below the drag: the pull"), FLureFight::TargetTension(Pull, false, Starter, T), Pull, 1.0e-4f);
	TestNearlyEqual(TEXT("not reeling, pull above the drag: the drag"), FLureFight::TargetTension(3.f * Starter.Drag, false, Starter, T), Starter.Drag, 1.0e-4f);
	TestTrue(TEXT("reeling loads the line more than letting it run"), FLureFight::TargetTension(Pull, true, Starter, T) > FLureFight::TargetTension(Pull, false, Starter, T));

	// Step 3: the line.
	TestNearlyEqual(TEXT("gain = ReelSpeed x (1 - Pull / RodPower)"), FLureFight::LineGainSpeed(Pull, true, Starter), Starter.ReelSpeed * (1.f - Pull / Starter.RodPower), 1.0e-3f);
	TestNearlyEqual(TEXT("no gain on a fish that out-pulls the rod"), FLureFight::LineGainSpeed(Starter.RodPower * 1.5f, true, Starter), 0.f, 1.0e-4f);
	TestNearlyEqual(TEXT("no gain without reeling"), FLureFight::LineGainSpeed(Pull, false, Starter), 0.f, 1.0e-4f);
	TestTrue(TEXT("a stronger rod gains faster on the same fish"), FLureFight::LineGainSpeed(Pull, true, Reef) > FLureFight::LineGainSpeed(Pull, true, Starter));
	TestNearlyEqual(TEXT("reeling: a fish below the rod's power takes no line"), FLureFight::LineTakenSpeed(Pull, 100.f, true, Starter, T), 0.f, 1.0e-4f);
	TestNearlyEqual(TEXT("reeling: at 1.5x the rod's power it takes half its speed"), FLureFight::LineTakenSpeed(1.5f * Starter.RodPower, 100.f, true, Starter, T), 50.f, 1.0e-3f);
	TestNearlyEqual(TEXT("not reeling: below DragHold x Drag the drag holds"), FLureFight::LineTakenSpeed(T.DragHold * Starter.Drag, 100.f, false, Starter, T), 0.f, 1.0e-3f);
	TestNearlyEqual(TEXT("not reeling: at the drag it runs freely"), FLureFight::LineTakenSpeed(Starter.Drag, 100.f, false, Starter, T), 100.f, 1.0e-3f);
	TestNearlyEqual(TEXT("not reeling: half way between"), FLureFight::LineTakenSpeed(0.5f * (1.f + T.DragHold) * Starter.Drag, 100.f, false, Starter, T), 50.f, 1.0e-2f);
	TestNearlyEqual(TEXT("swimming toward you shortens the line"), FLureFight::LineTakenSpeed(Pull, -80.f, false, Starter, T), -80.f, 1.0e-4f);

	// Easing (time constants from data).
	TestNearlyEqual(TEXT("rising: 63 % of the way after TensionRiseTime"), FLureFight::EaseTension(0.f, 10.f, T.TensionRiseTime, T), 10.f * (1.f - FMath::Exp(-1.f)), 1.0e-3f);
	TestNearlyEqual(TEXT("falling: 63 % of the way after TensionFallTime"), FLureFight::EaseTension(10.f, 0.f, T.TensionFallTime, T), 10.f * FMath::Exp(-1.f), 1.0e-3f);
	FLureFishFightRow Instant = T;
	Instant.TensionRiseTime = 0.f;
	TestNearlyEqual(TEXT("rise time 0 = instant"), FLureFight::EaseTension(0.f, 10.f, Dt, Instant), 10.f, 1.0e-4f);

	// Slack and hook security.
	FLureFightFish Fish;
	Fish.BasePull = 4.f;
	TestNearlyEqual(TEXT("slack below SlackShare x BasePull"), FLureFight::SlackTension(Fish, T), T.SlackShare * 4.f, 1.0e-4f);
	TestNearlyEqual(TEXT("slack grace = SlackGraceTime x HookSecurity (starter hook)"), FLureFight::SlackGrace(Starter, T), T.SlackGraceTime * Starter.HookSecurity, 1.0e-4f);
	TestTrue(TEXT("a more secure hook holds a slack line longer"), FLureFight::SlackGrace(Reef, T) > FLureFight::SlackGrace(Starter, T));

	// The fish from its FFishInstance final stats (tags from DT_FishFight).
	FishQA::FTables Tables;
	if (!FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FFishInstance Bonefish;
	FFishInstance Snapper;
	if (!RollFish(*this, Tables, TEXT("Bonefish"), TEXT("Common"), 0.5f, 11, Bonefish) || !RollFish(*this, Tables, TEXT("CoralSnapper"), TEXT("Rare"), 1.f, 12, Snapper))
	{
		return false;
	}
	const FFishLevelScaling& Scaling = GetDefault<UFishSettings>()->LevelScaling;
	const FLureFightFish Bone = FLureFight::MakeFish(Bonefish, T, 1, Scaling);
	TestNearlyEqual(TEXT("bonefish (level 1 vs player 1): pull = strength x PullPerStrength"), Bone.BasePull, Bonefish.GetStat(T.StrengthStat) * T.PullPerStrength, 1.0e-3f);
	TestNearlyEqual(TEXT("bonefish: speed = speed stat x SpeedPerStat"), Bone.BaseSpeed, Bonefish.GetStat(T.SpeedStat) * T.SpeedPerStat, 1.0e-3f);
	TestNearlyEqual(TEXT("bonefish: stamina pool = stamina stat x StaminaPerStat"), Bone.StaminaPool, Bonefish.GetStat(T.StaminaStat) * T.StaminaPerStat, 1.0e-3f);
	TestNearlyEqual(TEXT("bonefish: aggression from its stat"), Bone.Aggression, Bonefish.GetStat(T.AggressionStat), 1.0e-3f);
	const FLureFightFish Snap = FLureFight::MakeFish(Snapper, T, 1, Scaling);
	TestNearlyEqual(TEXT("a fish above your level pulls harder (UFishSettings level hook)"), Snap.LevelMultiplier, Scaling.GetMultiplier(Snapper.Level, 1), 1.0e-4f);
	TestTrue(TEXT("... the Rare snapper (level 4) is above a level 1 player"), Snap.LevelMultiplier > 1.f);
	TestNearlyEqual(TEXT("... its pull includes it"), Snap.BasePull, Snapper.GetStat(T.StrengthStat) * T.PullPerStrength * Snap.LevelMultiplier, 1.0e-3f);
	TestNearlyEqual(TEXT("... at the fish's level it is 1"), FLureFight::MakeFish(Snapper, T, Snapper.Level, Scaling).LevelMultiplier, 1.f, 1.0e-4f);
	FLureFishFightRow NoLevels = T;
	NoLevels.ApplyLevelScaling = false;
	TestNearlyEqual(TEXT("ApplyLevelScaling off: no level multiplier"), FLureFight::MakeFish(Snapper, NoLevels, 1, Scaling).LevelMultiplier, 1.f, 1.0e-4f);
	FFishInstance Hard = Bonefish;
	Hard.DifficultyRating = 2.f;
	TestNearlyEqual(TEXT("DifficultyRating 2: rests last half as long (exponent 1)"), FLureFight::MakeFish(Hard, T, 1, Scaling).RestScale, FMath::Pow(2.f, -T.RestDifficultyExponent), 1.0e-4f);

	// Pull and speed from the move and the stamina.
	FLureFightMove Run = Move(TEXT("Run"), 1.6f, 1.8f, 1.f);
	TestNearlyEqual(TEXT("fresh fish: pull = BasePull x Move.Pull"), FLureFight::FishPull(Bone, &Run, 1.f, T), Bone.BasePull * 1.6f, 1.0e-3f);
	TestNearlyEqual(TEXT("half stamina: x (TiredPull + (1 - TiredPull) / 2)"), FLureFight::FishPull(Bone, &Run, 0.5f, T), Bone.BasePull * 1.6f * (T.TiredPull + 0.5f * (1.f - T.TiredPull)), 1.0e-3f);
	TestNearlyEqual(TEXT("exhausted: BasePull x TiredPull"), FLureFight::FishPull(Bone, nullptr, 0.f, T), Bone.BasePull * T.TiredPull, 1.0e-3f);
	TestNearlyEqual(TEXT("speed = BaseSpeed x Move.Speed"), FLureFight::FishSpeed(Bone, &Run, 1.f, T), Bone.BaseSpeed * 1.8f, 1.0e-2f);

	// Other data, other numbers.
	FLureFishFightRow Strained = T;
	Strained.ReelStrain = 2.f;
	Strained.ReelLoad = 0.f;
	TestNearlyEqual(TEXT("fixture: ReelStrain 2, ReelLoad 0 -> reeling tension = 2 x pull"), FLureFight::TargetTension(Pull, true, Starter, Strained), 8.f, 1.0e-4f);
	FLureGearStats Loose = Starter;
	Loose.Drag = 2.f;
	TestNearlyEqual(TEXT("fixture: drag 2 caps the tension at 2 when letting it run"), FLureFight::TargetTension(Pull, false, Loose, T), 2.f, 1.0e-4f);
	return true;
}

// =====================================================================================================================
// Snap after the grace time, not before
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightSnapAfterGrace, "Project.Fishing.Fight.SnapAfterGraceNotBefore", LureFishFightTest::TestFlags)
bool FLureFightSnapAfterGrace::RunTest(const FString& Parameters)
{
	FFightData Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureGearStats Starter = Data.Starter();
	for (const float Grace : { Data.Tuning()->SnapGraceTime, 1.5f })
	{
		FLureFishFightRow Tuning = *Data.Tuning();
		Tuning.SnapGraceTime = Grace;
		// A steady fish that pulls 10 (reeling target 10 x 1.3 + 8 x 0.15 = 14.2 > the 10 line) and never tires or moves.
		FLureFightState State = SteadyFight(Tuning, 10.f, 0.f, Move(TEXT("Hold"), 1.f, 0.f, 0.f), Starter);
		FLureFightInput Reel;
		Reel.bReeling = true;
		float OverSince = -1.f;
		float OverBeforeSnap = 0.f;
		while (!State.IsOver() && State.Elapsed < 10.f)
		{
			OverBeforeSnap = State.OverTime;
			FLureFight::Step(State, Reel);
			if (OverSince < 0.f && State.Tension > Starter.LineStrength)
			{
				OverSince = State.Elapsed - FLureFight::StepSeconds(Tuning); // the step that crossed began then
			}
			if (OverSince >= 0.f && !State.IsOver() && State.Elapsed - OverSince > Grace + 2.f * Dt)
			{
				break; // should have snapped by now
			}
		}
		const FString Label = FString::Printf(TEXT("grace %.2f s"), Grace);
		TestEqual(Label + TEXT(": snapped"), OutcomeName(State.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		const float Above = State.Elapsed - OverSince;
		TestTrue(FString::Printf(TEXT("%s: snapped after the tension stayed above the line's strength for the grace time (%.3f s)"), *Label, Above),
			Above >= Grace - 1.0e-3f && Above <= Grace + 2.f * Dt);
		TestTrue(FString::Printf(TEXT("%s: at the snap the time over strength (%.4f s) is past the grace"), *Label, State.OverTime), State.OverTime > Grace);
		TestTrue(FString::Printf(TEXT("%s: one step earlier (%.4f s) it was not (no snap before the grace)"), *Label, OverBeforeSnap), OverBeforeSnap <= Grace + 0.5f * FLureFight::StepSeconds(Tuning)); // half a step: float step sums drift (T007-B1)
		TestTrue(Label + TEXT(": the tension crossed the line's strength first"), OverSince >= 0.f);
	}

	// Dipping below the strength resets the timer: reel 0.4 s, ease off 0.4 s (grace 0.6 s) never snaps.
	{
		const FLureFishFightRow& Tuning = *Data.Tuning();
		FLureFightState State = SteadyFight(Tuning, 10.f, 0.f, Move(TEXT("Hold"), 1.f, 0.f, 0.f), Starter);
		float LongestOver = 0.f;
		bool bWentOver = false;
		while (!State.IsOver() && State.Elapsed < 10.f)
		{
			FLureFightInput Input;
			Input.bReeling = FMath::Fmod(State.Elapsed, 0.8f) < 0.4f;
			FLureFight::Step(State, Input);
			LongestOver = FMath::Max(LongestOver, State.OverTime);
			bWentOver |= State.Tension > Starter.LineStrength;
		}
		TestTrue(TEXT("pulsing: the tension went over the line's strength"), bWentOver);
		TestTrue(FString::Printf(TEXT("pulsing: never over for the grace time (longest %.2f s)"), LongestOver), LongestOver < Tuning.SnapGraceTime);
		TestEqual(TEXT("pulsing: the line held for 10 s"), OutcomeName(State.Outcome), OutcomeName(ELureFightOutcome::None));
	}

	// Letting it run: the drag keeps the tension at 5 on the 10 line: never snaps.
	{
		FLureFightState State = SteadyFight(*Data.Tuning(), 10.f, 0.f, Move(TEXT("Hold"), 1.f, 0.f, 0.f), Starter);
		RunPlayer(State, EPlayer::Never, 10.f);
		TestEqual(TEXT("not reeling: the drag protects the line"), OutcomeName(State.Outcome), OutcomeName(ELureFightOutcome::None));
		TestNearlyEqual(TEXT("... tension at the drag"), State.Tension, Starter.Drag, 0.01f);
	}
	return true;
}

// =====================================================================================================================
// Gear decides the outcome (the T-007 acceptance test)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightGearDecidesOutcome, "Project.Fishing.Fight.GearDecidesOutcome", LureFishFightTest::TestFlags)
bool FLureFightGearDecidesOutcome::RunTest(const FString& Parameters)
{
	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	const FLureGearStats Starter = Data.Starter();
	const FLureGearStats Reef = Data.Reef();
	const FName Dive(TEXT("Dive"));

	// A: the same "hold reel" input on a reference Coral Snapper (level 3, above a level 1 player): the starter line snaps, the reef kit lands it.
	FFishInstance Snapper;
	if (!RollFish(*this, Tables, TEXT("CoralSnapper"), TEXT("Common"), SnapperReferenceFraction, 21, Snapper))
	{
		return false;
	}
	for (int32 Seed = 1; Seed <= 8; ++Seed)
	{
		FLureFightState Weak = BeginFight(Data, Snapper, Dive, Starter, Seed);
		FLureFightState Strong = BeginFight(Data, Snapper, Dive, Reef, Seed);
		RunPlayer(Weak, EPlayer::Hold);
		RunPlayer(Strong, EPlayer::Hold);
		TestEqual(FString::Printf(TEXT("hold reel, seed %d: the starter line snaps (%.1f s)"), Seed, Weak.Elapsed), OutcomeName(Weak.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		TestEqual(FString::Printf(TEXT("hold reel, seed %d: the reef rod and braid land it (%.1f s)"), Seed, Strong.Elapsed), OutcomeName(Strong.Outcome), OutcomeName(ELureFightOutcome::Landed));
	}

	// B (lead balance decision, 2026-09-23): full reel vs gear is part A (same seeds: starter snaps, reef lands); on the 7 kg snapper full reel
	// snaps the starter line (the 7 kg fish also snaps the reef braid under full reel: logged, reported to the lead);
	// a careful player (reels below 70 %, eases off above 90 %, reacts every 0.3 s) lands it with both kits, and the reef kit
	// does better: a higher land rate or a shorter mean land time over the same seeds.
	FFishInstance BigSnapper;
	if (!RollFish(*this, Tables, TEXT("CoralSnapper"), TEXT("Common"), 1.f, 22, BigSnapper))
	{
		return false;
	}
	TestNearlyEqual(TEXT("fixture: 7 kg"), BigSnapper.WeightKg, 7.f, 0.01f);
	int32 WeakLanded = 0, StrongLanded = 0;
	double WeakTime = 0.0, StrongTime = 0.0;
	for (int32 Seed = 1; Seed <= 8; ++Seed)
	{
		FLureFightState WeakHold = BeginFight(Data, BigSnapper, Dive, Starter, Seed);
		FLureFightState StrongHold = BeginFight(Data, BigSnapper, Dive, Reef, Seed);
		RunPlayer(WeakHold, EPlayer::Hold);
		RunPlayer(StrongHold, EPlayer::Hold);
		TestEqual(FString::Printf(TEXT("7 kg, hold reel, seed %d: the starter line snaps (%.1f s)"), Seed, WeakHold.Elapsed), OutcomeName(WeakHold.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		AddInfo(FString::Printf(TEXT("7 kg, hold reel, seed %d: reef kit %s after %.1f s (full reel on the reef kit is covered by part A)"), Seed, *OutcomeName(StrongHold.Outcome), StrongHold.Elapsed));

		FLureFightState Weak = BeginFight(Data, BigSnapper, Dive, Starter, Seed);
		FLureFightState Strong = BeginFight(Data, BigSnapper, Dive, Reef, Seed);
		RunPlayer(Weak, EPlayer::Careful);
		RunPlayer(Strong, EPlayer::Careful);
		AddInfo(FString::Printf(TEXT("careful, seed %d: starter %s after %.1f s, reef %s after %.1f s"), Seed, *OutcomeName(Weak.Outcome), Weak.Elapsed, *OutcomeName(Strong.Outcome), Strong.Elapsed));
		if (Weak.Outcome == ELureFightOutcome::Landed) { ++WeakLanded; WeakTime += Weak.Elapsed; }
		if (Strong.Outcome == ELureFightOutcome::Landed) { ++StrongLanded; StrongTime += Strong.Elapsed; }
	}
	TestTrue(FString::Printf(TEXT("careful: the starter kit can land it (%d/8)"), WeakLanded), WeakLanded > 0);
	TestTrue(FString::Printf(TEXT("careful: the reef kit lands it (%d/8)"), StrongLanded), StrongLanded > 0);
	const double WeakMean = WeakLanded > 0 ? WeakTime / WeakLanded : 0.0;
	const double StrongMean = StrongLanded > 0 ? StrongTime / StrongLanded : 0.0;
	TestTrue(FString::Printf(TEXT("careful: better gear lands more often or faster (reef %d/8, mean %.1f s; starter %d/8, mean %.1f s)"), StrongLanded, StrongMean, WeakLanded, WeakMean),
		StrongLanded > WeakLanded || (StrongLanded == WeakLanded && StrongLanded > 0 && StrongMean < WeakMean));

	// C: the whole component, server-side, with the loadout from DT_Gear: same fish, same careful player.
	for (const bool bRightGear : { false, true })
	{
		FFightWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = World.Spawn(StandAt);
		ULureFishingComponent* Fishing = SetUp(Character, Tables, Data);
		if (!TestNotNull(TEXT("fishing"), Fishing))
		{
			return false;
		}
		FLureGearLoadout Loadout;
		Loadout.Rod = bRightGear ? TEXT("Rod_Reef") : TEXT("Rod_Starter");
		Loadout.Line = bRightGear ? TEXT("Line_Braid") : TEXT("Line_Mono");
		Loadout.Hook = bRightGear ? TEXT("Hook_Squid") : TEXT("Hook_Shrimp");
		TestTrue(TEXT("loadout equipped"), Fishing->AuthoritySetLoadout(Loadout));
		if (!CastAndLand(*this, World, Fishing) || !TestTrue(TEXT("hook the 7 kg snapper"), Fishing->AuthorityHookFish(BigSnapper)))
		{
			return false;
		}
		const FString Label = bRightGear ? TEXT("world, reef kit") : TEXT("world, starter kit");
		TestTrue(Label + TEXT(": the fight is on"), Fishing->GetFightNet().bActive);
		TestEqual(Label + TEXT(": the species' pattern (Dive)"), Fishing->GetFightNet().PatternId, Dive);
		TArray<FFishInstance> Landed;
		Fishing->OnFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, const FFishInstance& Fish) { Landed.Add(Fish); });
		int32 Frame = 0;
		bool bReel = false;
		World.TickUntil([&]()
		{
			if (Frame++ % 18 == 0)
			{
				bReel = CarefulDecision(Fishing->GetFightNet().GetTension01(), bReel);
				Fishing->AuthoritySetReeling(bReel);
			}
			return Fishing->GetFishingState() != ELureFishingState::Hooked;
		}, 60 * 180);
		const ELureFishingResult Result = Fishing->GetNetState().LastResult;
		// Careful play lands the 7 kg snapper with either kit (lead balance decision); gear decides via part B's rates.
		TestEqual(FString::Printf(TEXT("%s: landed (%s)"), *Label, *OutcomeName(Fishing->GetFightNet().Outcome)), ResultName(Result), ResultName(ELureFishingResult::Landed));
		TestEqual(Label + TEXT(": the landed fish is the hooked one"), Fishing->GetLastLandedFish().Seed, BigSnapper.Seed);
		TestEqual(Label + TEXT(": OnFishLanded once"), Landed.Num(), 1);
		TestFalse(Label + TEXT(": the fight is over"), Fishing->GetFightNet().bActive);
		TestFalse(Label + TEXT(": the reel input is reset"), Fishing->IsServerReeling());
	}
	return true;
}

// =====================================================================================================================
// Fight patterns drive the pull, deterministically
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightPatternsDrivePull, "Project.Fishing.Fight.PatternsDrivePullDeterministically", LureFishFightTest::TestFlags)
bool FLureFightPatternsDrivePull::RunTest(const FString& Parameters)
{
	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FFishInstance Bonefish;
	if (!RollFish(*this, Tables, TEXT("Bonefish"), TEXT("Common"), 0.3f, 31, Bonefish))
	{
		return false;
	}
	// Gear that never ends the fight (so only the pattern shapes the trace).
	FLureGearStats Endless = Data.Starter();
	Endless.LineStrength = 1.0e6f;
	Endless.SpoolLength = 1.0e9f;
	Endless.HookSecurity = 1.0e6f;

	struct FTrace
	{
		TArray<float> Pulls;
		TArray<FName> Moves;
	};
	auto Record = [&](FName PatternId, int32 Seed, float Aggression = -1.f, const FLureFightPatternRow* Override = nullptr)
	{
		FLureFightState State = BeginFight(Data, Bonefish, PatternId, Endless, Seed, 100000.f);
		const FLureFightPatternRow Pattern = Override ? *Override : State.Pattern;
		FLureFightFish Fish = State.Fish;
		if (Aggression >= 0.f)
		{
			Fish.Aggression = Aggression;
			Fish.StaminaPool = 1.0e9f; // never tires: only the move mix differs
		}
		const FLureFishFightRow Tuning = State.Tuning;
		FLureFight::Begin(State, Fish, Pattern, PatternId, Endless, Tuning, Seed, 100000.f);
		FTrace Trace;
		Trace.Moves.Add(State.GetMoveId());
		bool bPullMatchesMove = true;
		for (int32 Step = 0; Step < 60 * 60 && !State.IsOver(); ++Step)
		{
			const float StaminaBefore = State.Stamina;
			FLureFight::Step(State, FLureFightInput());
			if (const FLureFightMove* Current = State.GetMove())
			{
				bPullMatchesMove &= FMath::IsNearlyEqual(State.Pull, State.Fish.BasePull * Current->Pull * FLureFight::StaminaFactor(State.Tuning, StaminaBefore), 1.0e-4f);
			}
			Trace.Pulls.Add(State.Pull);
			Trace.Moves.Add(State.GetMoveId());
		}
		TestTrue(FString::Printf(TEXT("%s: every step's pull = BasePull x the move's Pull x stamina factor"), *PatternId.ToString()), bPullMatchesMove);
		return Trace;
	};

	// Same seed, same fight; another seed, another fight.
	const FTrace A = Record(TEXT("Run"), 42);
	const FTrace B = Record(TEXT("Run"), 42);
	const FTrace C = Record(TEXT("Run"), 43);
	TestTrue(TEXT("seed 42 twice: identical pull traces"), A.Pulls == B.Pulls);
	TestTrue(TEXT("seed 42 twice: identical move sequences"), A.Moves == B.Moves);
	TestFalse(TEXT("seed 43: a different fight"), A.Pulls == C.Pulls);
	TestEqual(TEXT("FightSeed is stable for a fish record"), FLureFight::FightSeed(Bonefish.Seed), FLureFight::FightSeed(Bonefish.Seed));

	// Patterns change the pull (same fish, same seed).
	TMap<FName, FTrace> ByPattern;
	for (const TCHAR* PatternName : { TEXT("Run"), TEXT("Dive"), TEXT("Dart") })
	{
		const FName PatternId(PatternName);
		const FLureFightPatternRow* Pattern = Data.Pattern(PatternId);
		if (!TestNotNull(FString::Printf(TEXT("DT_FightPattern row %s"), PatternName), Pattern))
		{
			return false;
		}
		const FTrace Trace = Record(PatternId, 42);
		TestEqual(FString::Printf(TEXT("%s: opens with its OpeningMove"), PatternName), Trace.Moves[0], Pattern->OpeningMove);
		bool bKnownMoves = true;
		for (const FName& MoveId : Trace.Moves)
		{
			bKnownMoves &= MoveId.IsNone() || Pattern->FindMove(MoveId) != INDEX_NONE;
		}
		TestTrue(FString::Printf(TEXT("%s: only its own moves"), PatternName), bKnownMoves);
		TSet<FName> Distinct(Trace.Moves);
		TestTrue(FString::Printf(TEXT("%s: switches between several moves in a minute (%d)"), PatternName, Distinct.Num()), Distinct.Num() >= 2);
		ByPattern.Add(PatternId, Trace);
	}
	TestFalse(TEXT("Run and Dive pull differently"), ByPattern[TEXT("Run")].Pulls == ByPattern[TEXT("Dive")].Pulls);
	TestFalse(TEXT("Run and Dart pull differently"), ByPattern[TEXT("Run")].Pulls == ByPattern[TEXT("Dart")].Pulls);

	// A move's Pull in the data is the pull: double the opening Run's Pull, double the first step's pull.
	{
		FLureFightPatternRow Doubled = *Data.Pattern(TEXT("Run"));
		const int32 RunIndex = Doubled.FindMove(TEXT("Run"));
		Doubled.Moves[RunIndex].Pull *= 2.f;
		const FTrace Double = Record(TEXT("Run"), 42, -1.f, &Doubled);
		TestNearlyEqual(TEXT("data: Run.Pull x2 -> the opening pull x2"), Double.Pulls[0], 2.f * A.Pulls[0], 1.0e-3f);
	}

	// Aggression makes the aggressive moves more frequent (Run has AggressionWeight > 0).
	{
		auto RunShare = [](const FTrace& Trace)
		{
			int32 Runs = 0;
			for (const FName& MoveId : Trace.Moves)
			{
				Runs += MoveId == FName(TEXT("Run")) ? 1 : 0;
			}
			return static_cast<float>(Runs) / FMath::Max(1, Trace.Moves.Num());
		};
		const float Calm = RunShare(Record(TEXT("Run"), 7, 0.f));
		const float Wild = RunShare(Record(TEXT("Run"), 7, 300.f));
		TestTrue(FString::Printf(TEXT("aggression 300 runs more than aggression 0 (%.0f %% vs %.0f %% of the time)"), Wild * 100.f, Calm * 100.f), Wild > Calm);
	}

	// A new pattern is a data row: a species pointing at it fights with it, no code.
	{
		const FString Json = FString::Printf(TEXT("[ %s ]"), *PatternJson(TEXT("Test_Thrash"), { MoveJson(TEXT("Thrash"), 2.2f, 0.5f, 0.3f, 1.f), MoveJson(TEXT("Rest"), 0.3f, 0.f, 0.f, 1.f, true) }, TEXT("Thrash")));
		const TStrongObjectPtr<UDataTable> NewPatterns = PatternTableFromJson(*this, Json);
		const FLureFightPatternRow* Thrash = NewPatterns->FindRow<FLureFightPatternRow>(TEXT("Test_Thrash"), TEXT("test"), false);
		FString Problem;
		if (!TestNotNull(TEXT("new pattern row"), Thrash) || !TestTrue(TEXT("new pattern validates: ") + Problem, Thrash->Validate(Problem)))
		{
			return false;
		}
		FFishSpeciesRow* Species = Tables.Species->FindRow<FFishSpeciesRow>(TEXT("Bonefish"), TEXT("test"), false);
		if (!TestNotNull(TEXT("Bonefish row"), Species))
		{
			return false;
		}
		Species->FightPatternId = TEXT("Test_Thrash"); // the species row points at the new pattern (a data edit)

		FFightWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingComponent* Fishing = SetUp(World.Spawn(StandAt), Tables, Data);
		Fishing->SetFightTables(Data.Gear.Get(), NewPatterns.Get(), Data.Fight.Get());
		if (!CastAndLand(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
		{
			return false;
		}
		World.Tick(2);
		TestEqual(TEXT("the fight uses the new pattern"), Fishing->GetFightNet().PatternId, FName(TEXT("Test_Thrash")));
		TestEqual(TEXT("... opening with its move"), Fishing->GetFightState().GetMoveId(), FName(TEXT("Thrash")));
		TestNearlyEqual(TEXT("... pulling 2.2 x the fish"), Fishing->GetFightState().Pull,
			Fishing->GetFightState().Fish.BasePull * 2.2f * FLureFight::StaminaFactor(Fishing->GetFightTuning(), Fishing->GetFightState().Stamina), 0.05f * Fishing->GetFightState().Fish.BasePull);
		TestTrue(TEXT("HUD shows the move's label"), Fishing->GetStatusText().Contains(TEXT("Fish: Thrash")));

		// An unknown pattern id falls back to the built-in pattern (with a warning), so a typo never breaks fishing.
		Species->FightPatternId = TEXT("NoSuchPattern");
		Fishing->AuthorityReelIn();
		AddExpectedMessage(TEXT("NoSuchPattern"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		if (!CastAndLand(*this, World, Fishing) || !TestTrue(TEXT("hook again"), Fishing->AuthorityHookFish(Bonefish)))
		{
			return false;
		}
		TestTrue(TEXT("unknown pattern: the fight still runs"), Fishing->GetFightNet().bActive);
		TestTrue(TEXT("... on the built-in pattern"), Fishing->GetFightNet().PatternId.IsNone());
	}
	return true;
}

// =====================================================================================================================
// A slack line throws the hook (hook security from DT_Gear)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightSlackThrowsHook, "Project.Fishing.Fight.SlackThrowsHook", LureFishFightTest::TestFlags)
bool FLureFightSlackThrowsHook::RunTest(const FString& Parameters)
{
	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	const FLureFishFightRow& Tuning = *Data.Tuning();
	// The fish swims slowly toward you with no pull: a slack line unless you reel.
	const FLureFightMove Charge = Move(TEXT("Charge"), 0.f, 1.f, -0.1f);
	for (const FName Hook : { FName(TEXT("Hook_Shrimp")), FName(TEXT("Hook_Squid")) })
	{
		const FLureGearStats Gear = Data.Gear3(TEXT("Rod_Starter"), TEXT("Line_Mono"), Hook);
		const float Grace = FLureFight::SlackGrace(Gear, Tuning);
		TestNearlyEqual(Hook.ToString() + TEXT(": grace = SlackGraceTime x HookSecurity"), Grace, Tuning.SlackGraceTime * Gear.HookSecurity, 1.0e-4f);
		FLureFightState Loose = SteadyFight(Tuning, 2.5f, 100.f, Charge, Gear);
		RunPlayer(Loose, EPlayer::Never, 30.f);
		TestEqual(Hook.ToString() + TEXT(": a slack line throws the hook"), OutcomeName(Loose.Outcome), OutcomeName(ELureFightOutcome::ThrewHook));
		TestTrue(FString::Printf(TEXT("%s: ... after the grace %.2f s (%.3f s), not before"), *Hook.ToString(), Grace, Loose.Elapsed), Loose.Elapsed >= Grace - 1.0e-3f && Loose.Elapsed <= Grace + 2.f * Dt);
		TestTrue(Hook.ToString() + TEXT(": the slack time is past the grace at the throw"), Loose.SlackTime > Grace);

		FLureFightState Tight = SteadyFight(Tuning, 2.5f, 100.f, Charge, Gear);
		RunPlayer(Tight, EPlayer::Hold, 10.f);
		TestEqual(Hook.ToString() + TEXT(": reeling keeps the line tight (still on after 10 s)"), OutcomeName(Tight.Outcome), OutcomeName(ELureFightOutcome::None));
	}
	TestTrue(TEXT("Hook_Squid is the more secure hook"), Data.Gear3(NAME_None, NAME_None, TEXT("Hook_Squid")).HookSecurity > Data.Starter().HookSecurity);

	// A real fish left alone (never reeled) tires, the line goes slack and it throws the hook.
	FFishInstance Bonefish;
	if (!RollFish(*this, Tables, TEXT("Bonefish"), TEXT("Common"), 0.3f, 41, Bonefish))
	{
		return false;
	}
	for (int32 Seed = 1; Seed <= 4; ++Seed)
	{
		FLureFightState State = BeginFight(Data, Bonefish, TEXT("Run"), Data.Starter(), Seed);
		RunPlayer(State, EPlayer::Never, 180.f);
		TestEqual(FString::Printf(TEXT("never reeling, seed %d: the fish throws the hook (%.1f s)"), Seed, State.Elapsed), OutcomeName(State.Outcome), OutcomeName(ELureFightOutcome::ThrewHook));
	}

	// The component: the result reaches the replicated state and the HUD.
	FFightWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = SetUp(World.Spawn(StandAt), Tables, Data);
	const FString Json = FString::Printf(TEXT("[ %s ]"), *PatternJson(TEXT("Run"), { MoveJson(TEXT("Charge"), 0.f, 1.f, -0.1f) }, TEXT("Charge")));
	const TStrongObjectPtr<UDataTable> Patterns = PatternTableFromJson(*this, Json);
	Fishing->SetFightTables(Data.Gear.Get(), Patterns.Get(), Data.Fight.Get());
	if (!CastAndLand(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	World.Tick(90);
	TestTrue(TEXT("HUD warns about the slack line"), Fishing->GetStatusText().Contains(TEXT("Slack")));
	TestTrue(TEXT("the fish throws the hook"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() != ELureFishingState::Hooked; }, 600));
	TestEqual(TEXT("result ThrewHook"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::ThrewHook));
	TestEqual(TEXT("fight outcome ThrewHook"), OutcomeName(Fishing->GetFightNet().Outcome), OutcomeName(ELureFightOutcome::ThrewHook));
	TestTrue(TEXT("HUD: the fish threw the hook"), Fishing->GetStatusText().Contains(TEXT("threw the hook")));
	TestFalse(TEXT("no fish in hand"), Fishing->GetHookedFish().IsValid() || Fishing->GetLastLandedFish().IsValid());
	return true;
}

// =====================================================================================================================
// Server authority: a client can't force a landing
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightServerAuthority, "Project.Fishing.Fight.ServerAuthority", LureFishFightTest::TestFlags)
bool FLureFightServerAuthority::RunTest(const FString& Parameters)
{
	// What a client can send: the three T-006 requests plus its reel button, nothing about the fish or the outcome.
	UClass* Class = ULureFishingComponent::StaticClass();
	TSet<FString> ServerRpcs;
	for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		if (It->HasAnyFunctionFlags(FUNC_NetServer))
		{
			ServerRpcs.Add(It->GetName());
		}
	}
	TestEqual(TEXT("server RPCs = ServerCast, ServerHook, ServerReelIn, ServerSetReeling"), ServerRpcs.Num(), 4);
	for (const TCHAR* Name : { TEXT("ServerCast"), TEXT("ServerHook"), TEXT("ServerReelIn"), TEXT("ServerSetReeling") })
	{
		TestTrue(FString::Printf(TEXT("%s is a server RPC"), Name), ServerRpcs.Contains(Name));
	}
	const UFunction* SetReeling = Class->FindFunctionByName(TEXT("ServerSetReeling"));
	if (TestNotNull(TEXT("ServerSetReeling"), SetReeling))
	{
		TestTrue(TEXT("ServerSetReeling is reliable"), SetReeling->HasAnyFunctionFlags(FUNC_NetReliable));
		int32 Params = 0;
		bool bOnlyBool = true;
		for (TFieldIterator<FProperty> It(SetReeling); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			++Params;
			bOnlyBool &= CastField<FBoolProperty>(*It) != nullptr;
		}
		TestTrue(TEXT("its only parameter is the button (a bool)"), Params == 1 && bOnlyBool);
	}
	for (const TCHAR* Name : { TEXT("FightNet"), TEXT("Loadout") })
	{
		const FProperty* Property = Class->FindPropertyByName(Name);
		TestTrue(FString::Printf(TEXT("%s replicates (server-written)"), Name), Property && Property->HasAnyPropertyFlags(CPF_Net));
	}

	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FFishInstance Bonefish;
	if (!RollFish(*this, Tables, TEXT("Bonefish"), TEXT("Common"), 0.f, 51, Bonefish))
	{
		return false;
	}
	FFightWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* ServerCharacter = World.Spawn(StandAt);
	ALurePlayerCharacter* ClientCharacter = World.Spawn(StandAt + FVector(0.f, 200.f, 0.f));
	ULureFishingComponent* Server = SetUp(ServerCharacter, Tables, Data);
	ULureFishingComponent* Client = SetUp(ClientCharacter, Tables, Data);
	if (!TestNotNull(TEXT("server"), Server) || !TestNotNull(TEXT("client"), Client) || !CastAndLand(*this, World, Server) || !TestTrue(TEXT("server hooks"), Server->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	ClientCharacter->SetRole(ROLE_AutonomousProxy); // the client's copy: no authority
	Replicate(Server, Client);
	TestEqual(TEXT("client: sees the fish on"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Hooked));
	TestTrue(TEXT("client: sees the fight"), Client->GetFightNet().bActive);

	TArray<FFishInstance> ClientLanded;
	Client->OnFishLandedNative.AddLambda([&ClientLanded](ULureFishingComponent*, const FFishInstance& Fish) { ClientLanded.Add(Fish); });
	TestFalse(TEXT("client: can't hook a fish of its choice"), Client->AuthorityHookFish(Bonefish));
	TestFalse(TEXT("client: can't change its gear"), Client->AuthoritySetLoadout(FLureGearLoadout()));
	Client->AuthoritySetReeling(true);
	TestFalse(TEXT("client: its reel button is only a request"), Client->IsServerReeling());

	// A cheating client rewrites its own copy: the fish is "at its feet". The client still simulates nothing and lands nothing.
	const FLureFightNetState Before = Client->GetFightNet();
	FLureFightNetState* Forged = Class->FindPropertyByName(TEXT("FightNet"))->ContainerPtrToValuePtr<FLureFightNetState>(Client);
	Forged->LineOut = 0.f;
	Server->AuthoritySetReeling(false);
	World.Tick(30);
	TestEqual(TEXT("client: still hooked on its side (it doesn't decide)"), StateName(Client->GetFishingState()), StateName(ELureFishingState::Hooked));
	TestFalse(TEXT("client: no fish landed"), Client->GetLastLandedFish().IsValid());
	TestEqual(TEXT("client: OnFishLanded never fires on a client"), ClientLanded.Num(), 0);
	TestNearlyEqual(TEXT("client: its copy is not simulated"), Client->GetFightNet().Stamina, Before.Stamina, 1.0e-6f);
	TestTrue(TEXT("server: the fish is still far out"), Server->GetFightNet().LineOut > 500.f);
	TestEqual(TEXT("server: still fighting"), StateName(Server->GetFishingState()), StateName(ELureFishingState::Hooked));

	// The reel request is honoured by the server's own simulation: one frame of reeling can't land a fish 10 m out.
	Server->AuthoritySetReeling(true);
	const float LineBefore = Server->GetFightNet().LineOut;
	World.Tick(1);
	TestTrue(TEXT("server: one frame of reeling brings in at most ReelSpeed x dt"), LineBefore - Server->GetFightNet().LineOut <= Server->GetGearStats().ReelSpeed * Dt * 2.f + 0.01f);
	TestEqual(TEXT("server: still hooked after one frame"), StateName(Server->GetFishingState()), StateName(ELureFishingState::Hooked));

	// Reeling on, the server's simulation lands it; the client only learns it from replication.
	TArray<FFishInstance> ServerLanded;
	Server->OnFishLandedNative.AddLambda([&ServerLanded](ULureFishingComponent*, const FFishInstance& Fish) { ServerLanded.Add(Fish); });
	TestTrue(TEXT("server: lands the fish by reeling"), World.TickUntil([Server]() { return Server->GetFishingState() == ELureFishingState::Idle; }, 60 * 60));
	TestEqual(TEXT("server: landed"), ResultName(Server->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
	TestEqual(TEXT("server: OnFishLanded once"), ServerLanded.Num(), 1);
	Replicate(Server, Client);
	TestEqual(TEXT("client: learns the landing from the server"), ResultName(Client->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
	TestEqual(TEXT("client: the replicated catch"), Client->GetLastLandedFish().Seed, Bonefish.Seed);
	TestEqual(TEXT("client: still no OnFishLanded on the client (the cooler is server-side)"), ClientLanded.Num(), 0);
	ClientCharacter->SetRole(ROLE_Authority);
	return true;
}

// =====================================================================================================================
// DT_Gear, DT_FightPattern and DT_FishFight validation
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightDataValid, "Project.Fishing.Fight.GearTableValid", LureFishFightTest::TestFlags)
bool FLureFightDataValid::RunTest(const FString& Parameters)
{
	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();

	// ---- DT_Gear ----
	TMap<ELureGearSlot, int32> PerSlot;
	for (const TPair<FName, uint8*>& Pair : Data.Gear->GetRowMap())
	{
		const FLureGearRow& Row = *reinterpret_cast<const FLureGearRow*>(Pair.Value);
		const FString Name = Pair.Key.ToString();
		FString Problem;
		TestTrue(Name + TEXT(" validates: ") + Problem, Row.Validate(Problem));
		TestFalse(Name + TEXT(": has a display name"), Row.DisplayName.IsEmpty());
		PerSlot.FindOrAdd(Row.Slot)++;
		// Only the slot's columns count; the others are 0 / None so the sheet stays honest.
		const bool bRod = Row.Slot == ELureGearSlot::Rod;
		const bool bLine = Row.Slot == ELureGearSlot::Line;
		const bool bHook = Row.Slot == ELureGearSlot::Hook;
		TestTrue(Name + TEXT(": rod columns only on rods"), bRod || (Row.RodPower == 0.f && Row.ReelSpeed == 0.f && Row.Drag == 0.f && Row.CastDistanceMultiplier == 0.f));
		TestTrue(Name + TEXT(": line columns only on lines"), bLine || (Row.LineStrength == 0.f && Row.SpoolLength == 0.f));
		TestTrue(Name + TEXT(": hook columns only on hooks"), bHook || (Row.HookSecurity == 0.f && Row.Luck == 0.f && !Row.BaitTag.IsValid()));
		if (bHook)
		{
			TestTrue(Name + TEXT(": bait is a registered Bait tag"), Row.BaitTag.IsValid() && Row.BaitTag.MatchesTag(Tag(TEXT("Bait"))));
			bool bSomeoneBites = false;
			for (const TPair<FName, uint8*>& SpeciesPair : Tables.Species->GetRowMap())
			{
				const FFishSpeciesRow& Species = *reinterpret_cast<const FFishSpeciesRow*>(SpeciesPair.Value);
				bSomeoneBites |= Species.AcceptedBait.Num() == 0 || Species.AcceptedBait.ContainsByPredicate([&Row](const FGameplayTag& Bait) { return Row.BaitTag.MatchesTag(Bait); });
			}
			TestTrue(Name + TEXT(": some species takes its bait"), bSomeoneBites);
		}
		if (bRod)
		{
			TestTrue(Name + TEXT(": drag below the rod's power (letting it run eases the line)"), Row.Drag < Row.RodPower);
		}
	}
	TestTrue(TEXT("at least 2 rods, 2 lines and 2 hooks (the T-012 shop)"), PerSlot.FindRef(ELureGearSlot::Rod) >= 2 && PerSlot.FindRef(ELureGearSlot::Line) >= 2 && PerSlot.FindRef(ELureGearSlot::Hook) >= 2);

	// The default loadout exists, has the right slots, and the built-in items equal it.
	for (const ELureGearSlot Slot : { ELureGearSlot::Rod, ELureGearSlot::Line, ELureGearSlot::Hook })
	{
		const FName Id = Settings->DefaultLoadout.Get(Slot);
		const FString SlotName = StaticEnum<ELureGearSlot>()->GetNameStringByValue(static_cast<int64>(Slot));
		FString Problem;
		TestTrue(FString::Printf(TEXT("default %s '%s' can be equipped: %s"), *SlotName, *Id.ToString(), *Problem), FLureGear::CanEquip(Data.Gear.Get(), Slot, Id, &Problem));
		const FLureGearRow* Row = FLureGear::FindItem(Data.Gear.Get(), Id);
		if (!Row)
		{
			continue;
		}
		FLureGearStats FromTable;
		FLureGearStats BuiltIn;
		FLureGear::ApplyItem(FromTable, *Row, NAME_None);
		FLureGear::ApplyItem(BuiltIn, FLureGear::GetFallbackItem(Slot), NAME_None);
		TestTrue(FString::Printf(TEXT("built-in %s = the default row in DT_Gear.csv (update FLureGearRow defaults with the CSV)"), *SlotName),
			FLureGearStats::StaticStruct()->CompareScriptStruct(&FromTable, &BuiltIn, PPF_None));
	}
	TestTrue(TEXT("no table: every slot built-in"), FLureGear::Resolve(nullptr, Settings->DefaultLoadout).bUsedFallback);
	TestTrue(TEXT("an upgrade rod is stronger than the starter"), Data.Reef().RodPower > Data.Starter().RodPower);
	TestTrue(TEXT("an upgrade line is stronger than the starter"), Data.Reef().LineStrength > Data.Starter().LineStrength);

	// Raw CSV cells: numbers are numbers, the slot a known name, tags None or registered (the importer turns bad text into 0 silently).
	auto CheckCsvCells = [this](const FString& Csv, const UScriptStruct* RowStruct, const TCHAR* File)
	{
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines, true);
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		for (int32 Line = 1; Line < Lines.Num(); ++Line)
		{
			TArray<FString> Cells;
			Lines[Line].ParseIntoArray(Cells, TEXT(","), false);
			TestEqual(FString::Printf(TEXT("%s line %d has one cell per column (no commas in text)"), File, Line + 1), Cells.Num(), Header.Num());
			for (int32 Column = 1; Column < FMath::Min(Cells.Num(), Header.Num()); ++Column)
			{
				const FProperty* Property = RowStruct->FindPropertyByName(FName(*Header[Column].TrimStartAndEnd()));
				const FString Cell = Cells[Column].TrimStartAndEnd();
				const FString Where = FString::Printf(TEXT("%s %s.%s = '%s'"), File, *Cells[0], *Header[Column], *Cell);
				if (!TestNotNull(Where + TEXT(": known column"), Property))
				{
					continue;
				}
				if (CastField<FBoolProperty>(Property))
				{
					TestTrue(Where + TEXT(" is True/False"), Cell.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Cell.Equals(TEXT("False"), ESearchCase::IgnoreCase));
				}
				else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
				{
					TestTrue(Where + TEXT(" is a known name"), Enum->GetEnum()->GetIndexByNameString(Cell) != INDEX_NONE);
				}
				else if (const FStructProperty* Struct = CastField<FStructProperty>(Property); Struct && Struct->Struct == FGameplayTag::StaticStruct())
				{
					TestTrue(Where + TEXT(" is None or a registered tag"), Cell == TEXT("None") || Tag(*Cell).IsValid());
				}
				else if (CastField<FNumericProperty>(Property))
				{
					TestTrue(Where + TEXT(" is a number"), !Cell.IsEmpty() && FCString::IsNumeric(*Cell));
				}
			}
		}
		TArray<FString> Missing;
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			if (!Header.Contains(It->GetName()))
			{
				Missing.Add(It->GetName());
			}
		}
		TestEqual(FString::Printf(TEXT("%s: every field is a column (missing: %s)"), File, *FString::Join(Missing, TEXT(", "))), Missing.Num(), 0);
	};
	CheckCsvCells(Data.GearCsv, FLureGearRow::StaticStruct(), TEXT("DT_Gear.csv"));
	CheckCsvCells(Data.FightCsv, FLureFishFightRow::StaticStruct(), TEXT("DT_FishFight.csv"));

	// Broken rows are caught.
	{
		FLureGearRow Rod = *FLureGear::FindItem(Data.Gear.Get(), TEXT("Rod_Starter"));
		FString Problem;
		Rod.RodPower = 0.f;
		TestFalse(TEXT("a rod with no power is invalid"), Rod.Validate(Problem));
		FLureGearRow Line = *FLureGear::FindItem(Data.Gear.Get(), TEXT("Line_Mono"));
		Line.LineStrength = -1.f;
		TestFalse(TEXT("a line with negative strength is invalid"), Line.Validate(Problem));
		FLureGearRow Hook = *FLureGear::FindItem(Data.Gear.Get(), TEXT("Hook_Shrimp"));
		Hook.HookSecurity = 0.f;
		TestFalse(TEXT("a hook with no security is invalid"), Hook.Validate(Problem));
		TestFalse(TEXT("a line can't go in the rod slot"), FLureGear::CanEquip(Data.Gear.Get(), ELureGearSlot::Rod, TEXT("Line_Mono")));
		TestFalse(TEXT("an unknown id can't be equipped"), FLureGear::CanEquip(Data.Gear.Get(), ELureGearSlot::Rod, TEXT("Rod_Nope")));
	}

	// ---- DT_FightPattern ----
	const TArray<FString> JsonProblems = FFishDataValidator::ValidateJsonSource(Data.PatternJson, FLureFightPatternRow::StaticStruct(), TEXT("DT_FightPattern"));
	TestEqual(FString::Printf(TEXT("DT_FightPattern.json values are typed (%s)"), *FString::Join(JsonProblems, TEXT(" | "))), JsonProblems.Num(), 0);
	for (const TPair<FName, uint8*>& Pair : Data.Patterns->GetRowMap())
	{
		const FLureFightPatternRow& Row = *reinterpret_cast<const FLureFightPatternRow*>(Pair.Value);
		FString Problem;
		TestTrue(Pair.Key.ToString() + TEXT(" validates: ") + Problem, Row.Validate(Problem));
		for (const FLureFightMove& FightMove : Row.Moves)
		{
			TestFalse(FString::Printf(TEXT("%s.%s has a HUD label"), *Pair.Key.ToString(), *FightMove.Id.ToString()), FightMove.Label.IsEmpty());
		}
	}
	for (const TCHAR* Required : { TEXT("Run"), TEXT("Dive"), TEXT("Dart") })
	{
		TestNotNull(FString::Printf(TEXT("pattern %s exists"), Required), Data.Pattern(Required));
	}
	for (const TPair<FName, uint8*>& Pair : Tables.Species->GetRowMap())
	{
		const FFishSpeciesRow& Species = *reinterpret_cast<const FFishSpeciesRow*>(Pair.Value);
		TestNotNull(FString::Printf(TEXT("species %s: FightPatternId '%s' is a DT_FightPattern row"), *Pair.Key.ToString(), *Species.FightPatternId.ToString()), Data.Pattern(Species.FightPatternId));
	}
	{
		FString Problem;
		FLureFightPatternRow Empty;
		TestFalse(TEXT("a pattern with no moves is invalid"), Empty.Validate(Problem));
		FLureFightPatternRow BadOpening = *Data.Pattern(TEXT("Run"));
		BadOpening.OpeningMove = TEXT("Nope");
		TestFalse(TEXT("an unknown OpeningMove is invalid"), BadOpening.Validate(Problem));
		FLureFightPatternRow Twice = *Data.Pattern(TEXT("Run"));
		const FLureFightMove First = Twice.Moves[0]; // a copy: adding an element of the same array asserts
		Twice.Moves.Add(First);
		TestFalse(TEXT("a move id used twice is invalid"), Twice.Validate(Problem));
		FLureFightPatternRow Backwards = *Data.Pattern(TEXT("Run"));
		Backwards.Moves[0].DurationMax = Backwards.Moves[0].DurationMin * 0.5f;
		TestFalse(TEXT("DurationMax < DurationMin is invalid"), Backwards.Validate(Problem));
		TestTrue(TEXT("the built-in pattern validates"), FLureFightPatternRow::GetFallbackPattern().Validate(Problem));
	}

	// ---- DT_FishFight ----
	const FLureFishFightRow& Tuning = *Data.Tuning();
	FString Problem;
	TestTrue(TEXT("DT_FishFight Default validates: ") + Problem, Tuning.Validate(Problem));
	const FLureFishFightRow BuiltIn = FLureFishFightRow::GetFallbackRow();
	TestTrue(TEXT("the built-in fight tuning equals the shipped Default row (update FLureFishFightRow defaults with the CSV)"),
		FLureFishFightRow::StaticStruct()->CompareScriptStruct(&Tuning, &BuiltIn, PPF_None));
	TestEqual(TEXT("settings use row Default"), Settings->FishFightRow, FName(TEXT("Default")));
	for (const FGameplayTag& StatTag : { Tuning.StrengthStat, Tuning.StaminaStat, Tuning.SpeedStat, Tuning.AggressionStat })
	{
		TestNotNull(FString::Printf(TEXT("fight stat %s has a DT_FishStat row"), *StatTag.ToString()), Tables.Get().FindStat(StatTag));
	}
	TestTrue(TEXT("the line can snap: SnapGraceTime is short (< 2 s)"), Tuning.SnapGraceTime < 2.f);
	TestTrue(TEXT("a tired fish on a loose line counts as slack (TiredPull < SlackShare)"), Tuning.TiredPull < Tuning.SlackShare);
	FLureFishFightRow NoTag = Tuning;
	NoTag.StrengthStat = FGameplayTag();
	TestFalse(TEXT("a tuning row without a strength stat is invalid"), NoTag.Validate(Problem));
	return true;
}

// =====================================================================================================================
// Loadout: replicated, defaults, gear changes the cast, bait and luck
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightLoadout, "Project.Fishing.Fight.LoadoutReplicatedWithDefaults", LureFishFightTest::TestFlags)
bool FLureFightLoadout::RunTest(const FString& Parameters)
{
	const FProperty* LoadoutProperty = ULureFishingComponent::StaticClass()->FindPropertyByName(TEXT("Loadout"));
	TestTrue(TEXT("Loadout replicates with a RepNotify"), LoadoutProperty && LoadoutProperty->HasAnyPropertyFlags(CPF_Net | CPF_RepNotify) && LoadoutProperty->RepNotifyFunc == TEXT("OnRep_Loadout"));

	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FFightWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_reef"), TEXT("Habitat=Habitat.Reef"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200"), TEXT("Luck=1") });
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	ULureFishingComponent* Fishing = SetUp(Character, Tables, Data, 20.f); // 20:00: the snapper bites
	if (!TestNotNull(TEXT("fishing"), Fishing))
	{
		return false;
	}
	const FLureGearLoadout& Defaults = GetDefault<ULureFishingSettings>()->DefaultLoadout;
	TestTrue(TEXT("starts with the settings' default loadout"), Fishing->GetLoadout() == Defaults);
	const FLureGearStats Start = Fishing->GetGearStats();
	TestFalse(TEXT("... resolved from DT_Gear"), Start.bUsedFallback);
	TestNearlyEqual(TEXT("... starter line strength"), Start.LineStrength, Data.Starter().LineStrength, 1.0e-4f);

	// Casting further with the reef rod (CastDistanceMultiplier).
	const float Eye = static_cast<float>(Character->GetPawnViewLocation().X);
	TestTrue(TEXT("cast with the starter rod"), Fishing->AuthorityCast(0.5f, 0.f));
	const float StarterDistance = static_cast<float>(Fishing->GetNetState().BobberRest.X) - Eye;
	Fishing->AuthorityReelIn();

	// The reef rod on the starter line: the component caps the drag at the line (DragLineCap, T-007 balance rule).
	FLureGearLoadout ReefRodOnly = Defaults;
	ReefRodOnly.Rod = TEXT("Rod_Reef");
	TestTrue(TEXT("equip the reef rod on the starter line"), Fishing->AuthoritySetLoadout(ReefRodOnly));
	const float CappedDrag = Fishing->GetGearStats().Drag;
	TestNearlyEqual(FString::Printf(TEXT("drag = min(rod drag %.1f, line %.1f x DragLineCap %.2f)"), Data.Gear3(TEXT("Rod_Reef"), Defaults.Line, Defaults.Hook).Drag,
		Start.LineStrength, Data.Tuning()->DragLineCap), CappedDrag, FMath::Min(Data.Gear3(TEXT("Rod_Reef"), Defaults.Line, Defaults.Hook).Drag, Start.LineStrength * Data.Tuning()->DragLineCap), 1.0e-4f);
	TestTrue(TEXT("... below the line's strength"), CappedDrag < Start.LineStrength);

	FLureGearLoadout Reef;
	Reef.Rod = TEXT("Rod_Reef");
	Reef.Line = TEXT("Line_Braid");
	Reef.Hook = TEXT("Hook_Squid");
	TestTrue(TEXT("equip the reef kit"), Fishing->AuthoritySetLoadout(Reef));
	TestTrue(TEXT("the loadout changed"), Fishing->GetLoadout() == Reef);
	const FLureGearStats Stats = Fishing->GetGearStats();
	TestNearlyEqual(TEXT("reef rod power"), Stats.RodPower, Data.Reef().RodPower, 1.0e-4f);
	TestNearlyEqual(TEXT("braid strength"), Stats.LineStrength, Data.Reef().LineStrength, 1.0e-4f);
	TestTrue(TEXT("squid on the hook"), Stats.BaitTag == Tag(TEXT("Bait.Squid")));
	TestTrue(TEXT("cast with the reef rod"), Fishing->AuthorityCast(0.5f, 0.f));
	const float ReefDistance = static_cast<float>(Fishing->GetNetState().BobberRest.X) - Eye;
	TestNearlyEqual(FString::Printf(TEXT("the reef rod casts x%.2f as far (%.0f vs %.0f cm)"), Stats.CastDistanceMultiplier, ReefDistance, StarterDistance), ReefDistance, StarterDistance * Stats.CastDistanceMultiplier, 2.f);

	// The hook's bait and luck reach the bite: squid at the reef at 20:00 brings the snapper.
	TestTrue(TEXT("lands"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	TestTrue(TEXT("a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240));
	TestTrue(TEXT("the roll used the hook's bait"), Fishing->GetLastRollContext().BaitTag == Tag(TEXT("Bait.Squid")));
	TestNearlyEqual(TEXT("luck = spot 1 + hook 0.5"), Fishing->GetLastRollContext().Luck, 1.f + Stats.Luck, 1.0e-4f);
	TestEqual(TEXT("squid brings the snapper"), Fishing->GetPendingFish().SpeciesId, FName(TEXT("CoralSnapper")));

	// Refusals keep the current item per slot; nothing changes while a fish is on.
	FLureGearLoadout Wrong;
	Wrong.Rod = TEXT("Line_Mono");
	Wrong.Hook = TEXT("Hook_Shrimp");
	AddExpectedMessage(TEXT("can't equip Line_Mono"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("a line in the rod slot is refused"), Fishing->AuthoritySetLoadout(Wrong));
	TestEqual(TEXT("... the rod stays"), Fishing->GetLoadout().Rod, FName(TEXT("Rod_Reef")));
	TestEqual(TEXT("... the valid hook in the same request is equipped"), Fishing->GetLoadout().Hook, FName(TEXT("Hook_Shrimp")));
	TestEqual(TEXT("... None keeps the line"), Fishing->GetLoadout().Line, FName(TEXT("Line_Braid")));
	Fishing->AuthorityHook();
	TestEqual(TEXT("hooked"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
	TestFalse(TEXT("no gear change while a fish is on"), Fishing->AuthoritySetLoadout(Reef));
	TestNearlyEqual(TEXT("the fight uses the equipped line"), Fishing->GetFightNet().LineStrength, Data.Reef().LineStrength, 1.0e-4f);

	// A client copy resolves the same numbers from the replicated names.
	ALurePlayerCharacter* ClientCharacter = World.Spawn(StandAt + FVector(0.f, 200.f, 0.f));
	ULureFishingComponent* Client = SetUp(ClientCharacter, Tables, Data);
	ClientCharacter->SetRole(ROLE_AutonomousProxy);
	Replicate(Fishing, Client);
	TestTrue(TEXT("client: the replicated loadout"), Client->GetLoadout() == Fishing->GetLoadout());
	const FLureGearStats ServerGear = Fishing->GetGearStats();
	const FLureGearStats ClientGear = Client->GetGearStats();
	TestTrue(TEXT("client: the same gear numbers"), FLureGearStats::StaticStruct()->CompareScriptStruct(&ClientGear, &ServerGear, PPF_None));
	ClientCharacter->SetRole(ROLE_Authority);

	// The bonefish ignores squid: at a shore spot at noon with squid, nothing bites.
	{
		FFightWorld Shore;
		if (!Shore.Create(*this))
		{
			return false;
		}
		Shore.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Radius=1200") });
		ULureFishingComponent* ShoreFishing = SetUp(Shore.Spawn(StandAt), Tables, Data, 12.f);
		FLureGearLoadout Squid;
		Squid.Hook = TEXT("Hook_Squid");
		ShoreFishing->AuthoritySetLoadout(Squid);
		if (!CastAndLand(*this, Shore, ShoreFishing))
		{
			return false;
		}
		Shore.Tick(40);
		TestEqual(TEXT("shore + squid: no bite"), StateName(ShoreFishing->GetFishingState()), StateName(ELureFishingState::Waiting));
		TestTrue(TEXT("shore + squid: nothing takes it"), ShoreFishing->GetNetState().bNoFishHere);
	}
	return true;
}

// =====================================================================================================================
// Landing: OnFishLanded hand-off, the catch log, the HUD
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightLandedHandsOff, "Project.Fishing.Fight.LandedHandsOffAndLogs", LureFishFightTest::TestFlags)
bool FLureFightLandedHandsOff::RunTest(const FString& Parameters)
{
	const FProperty* Delegate = ULureFishingComponent::StaticClass()->FindPropertyByName(TEXT("OnFishLanded"));
	TestTrue(TEXT("OnFishLanded is a Blueprint-assignable delegate (the cooler, T-010)"), Delegate && CastField<FMulticastDelegateProperty>(Delegate) && Delegate->HasAnyPropertyFlags(CPF_BlueprintAssignable));

	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FFishInstance Bonefish;
	if (!RollFish(*this, Tables, TEXT("Bonefish"), TEXT("Uncommon"), 0.2f, 61, Bonefish))
	{
		return false;
	}
	FFightWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	ULureFishingComponent* Fishing = SetUp(Character, Tables, Data);
	if (!CastAndLand(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	TArray<FFishInstance> Landed;
	Fishing->OnFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, const FFishInstance& Fish) { Landed.Add(Fish); });

	// The placeholder HUD while fighting: plain text, tension bar, fish state, line out.
	Fishing->AuthoritySetReeling(true);
	World.Tick(20);
	const FString Hud = ALureHUD::GetStatusText(Character);
	TestTrue(TEXT("HUD: hooked fish"), Hud.Contains(TEXT("Hooked: Bonefish")));
	TestTrue(TEXT("HUD: tension bar and percent"), Hud.Contains(TEXT("Tension [")) && Hud.Contains(TEXT("%")) && Hud.Contains(TEXT("|")));
	TestTrue(TEXT("HUD: fish state"), Hud.Contains(TEXT("Fish: ")) && Hud.Contains(TEXT("stamina")));
	TestTrue(TEXT("HUD: line out"), Hud.Contains(TEXT("Line out")));
	TestTrue(TEXT("HUD: reeling hint"), Hud.Contains(TEXT("Reeling")));

	FFishLogCapture Log;
	TestTrue(TEXT("reeling lands the small bonefish"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 60 * 60));
	TestEqual(TEXT("result Landed"), ResultName(Fishing->GetNetState().LastResult), ResultName(ELureFishingResult::Landed));
	TestEqual(TEXT("fight outcome Landed"), OutcomeName(Fishing->GetFightNet().Outcome), OutcomeName(ELureFightOutcome::Landed));
	if (TestEqual(TEXT("OnFishLanded fired once"), Landed.Num(), 1))
	{
		TestEqual(TEXT("... with the hooked instance"), Landed[0].Seed, Bonefish.Seed);
		TestEqual(TEXT("... species"), Landed[0].SpeciesId, Bonefish.SpeciesId);
		TestEqual(TEXT("... value"), Landed[0].Value, Bonefish.Value);
	}
	TestEqual(TEXT("LastLandedFish replicates the catch"), Fishing->GetLastLandedFish().Seed, Bonefish.Seed);
	const TArray<FString> Lines = Log.Get();
	const FString* Catch = Lines.FindByPredicate([](const FString& Line) { return Line.Contains(TEXT("Catch:")); });
	if (TestNotNull(TEXT("LogLureFish has a Catch line"), Catch))
	{
		TestTrue(TEXT("... species"), Catch->Contains(TEXT("Bonefish")));
		TestTrue(TEXT("... rarity"), Catch->Contains(TEXT("Uncommon")));
		TestTrue(TEXT("... weight"), Catch->Contains(FString::Printf(TEXT("%.2f kg"), Bonefish.WeightKg)));
		TestTrue(TEXT("... value"), Catch->Contains(FString::Printf(TEXT("%d coins"), Bonefish.Value)));
	}
	TestTrue(TEXT("HUD: caught"), Fishing->GetStatusText().Contains(TEXT("Caught: Bonefish")));
	TestFalse(TEXT("HUD: no fight readout after the landing"), Fishing->GetStatusText().Contains(TEXT("Tension [")));

	// The T-006 placeholder still works as a debug option (AutoLandDelay > 0 skips the fight).
	FLureFishingRow Placeholder = QuickProfile();
	Placeholder.AutoLandDelay = 0.3f;
	Fishing->SetFishingProfile(Placeholder);
	if (!CastAndLand(*this, World, Fishing) || !TestTrue(TEXT("hook (placeholder)"), Fishing->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	TestFalse(TEXT("AutoLandDelay > 0: no fight"), Fishing->GetFightNet().bActive);
	TestTrue(TEXT("... lands after the delay"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 120));
	TestEqual(TEXT("... OnFishLanded fired again"), Landed.Num(), 2);
	TestNearlyEqual(TEXT("the shipped DT_Fishing profile runs the fight (AutoLandDelay 0)"), FLureFishingRules::GetFallbackRow().AutoLandDelay, 0.f, 0.f);
	return true;
}

// =====================================================================================================================
// Visuals: rod bend and line sag follow the tension; the bobber rides on the fish
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightVisuals, "Project.Fishing.Fight.RodAndLineFollowTension", LureFishFightTest::TestFlags)
bool FLureFightVisuals::RunTest(const FString& Parameters)
{
	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	const FLureFishFightRow& T = *Data.Tuning();
	TestNearlyEqual(TEXT("rod: no tension, no bend"), FLureFight::RodPitch(0.f, 0.f, T), 0.f, 1.0e-4f);
	TestNearlyEqual(TEXT("rod: half tension, half the bend (tip toward the fish)"), FLureFight::RodPitch(0.5f, 0.f, T), -0.5f * T.RodTensionPitchDeg, 1.0e-3f);
	TestNearlyEqual(TEXT("rod: full strength, full bend"), FLureFight::RodPitch(1.f, 0.f, T), -T.RodTensionPitchDeg, 1.0e-3f);
	TestTrue(TEXT("rod: over strength it shakes around the full bend"), FMath::Abs(FLureFight::RodPitch(1.2f, 0.37f, T) + T.RodTensionPitchDeg) <= T.RodShakeDeg + 1.0e-3f);
	const float BaseSag = FLureFishingRules::GetFallbackRow().LineSag;
	TestNearlyEqual(TEXT("line: slack line sags fully"), FLureFight::LineSag(BaseSag, 0.f, T), BaseSag, 1.0e-5f);
	TestNearlyEqual(TEXT("line: half way to taut"), FLureFight::LineSag(BaseSag, 0.5f * T.TautTension, T), 0.5f * BaseSag, 1.0e-5f);
	TestNearlyEqual(TEXT("line: taut from TautTension"), FLureFight::LineSag(BaseSag, T.TautTension, T), 0.f, 1.0e-5f);

	FFishInstance Bonefish;
	if (!RollFish(*this, Tables, TEXT("Bonefish"), TEXT("Common"), 0.5f, 71, Bonefish))
	{
		return false;
	}
	FFightWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!Character || !Controller)
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.Tick(10);
	ULureFishingComponent* Fishing = SetUp(Character, Tables, Data);
	if (!CastAndLand(*this, World, Fishing) || !TestTrue(TEXT("hook"), Fishing->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	Fishing->AuthoritySetReeling(true);
	World.Tick(30);
	const FLureFightNetState& Net = Fishing->GetFightNet();
	const FVector Bobber = Fishing->GetBobberLocation();
	TestNearlyEqual(TEXT("the bobber rides on the fish (LineOut from the player)"), static_cast<float>(FVector::Dist2D(Bobber, Character->GetActorLocation())), Net.LineOut, 2.f);
	TestTrue(TEXT("... pulled under the surface"), Bobber.Z < Fishing->GetNetState().BobberRest.Z - 1.f);
	if (const UStaticMeshComponent* Rod = Fishing->GetRodMesh(); Rod && Net.GetTension01() <= 1.f)
	{
		TestNearlyEqual(FString::Printf(TEXT("the rod bends with the tension (%.0f %%)"), Net.GetTension01() * 100.f), static_cast<float>(Rod->GetRelativeRotation().Pitch),
			FLureFight::RodPitch(Net.GetTension01(), 0.f, T), 1.f);
	}
	else
	{
		AddInfo(TEXT("SK_FPArms or SM_Rod_Basic is not imported here: the rod bend check is skipped."));
	}
	if (const ULureFishingLineComponent* Line = Fishing->GetLine(); Line && Line->IsLineVisible() && Line->GetPoints().Num() >= 3)
	{
		// T-032: the physics line's length follows the fight's tension (docs/specs/fishing-line.md): fully straight at the line's strength.
		const TArray<FVector>& Points = Line->GetPoints();
		const float Chord = static_cast<float>(FVector::Dist(Points[0], Points.Last()));
		TestNearlyEqual(TEXT("the line shows the fight's tension"), Line->GetTension(), FMath::Clamp(Net.GetTension01(), 0.f, 1.f), 1.0e-4f);
		TestNearlyEqual(TEXT("the line's length target follows the tension rule"), Line->GetTargetRestLength(),
			FLureFishingLineRules::TargetRestLength(Chord, Net.GetTension01(), -1.f, Line->GetTuning()), 0.5f);
		TestTrue(TEXT("the line is never shorter than the straight distance"), Line->GetRestLength() >= Chord - 0.5f);
	}
	else
	{
		AddInfo(TEXT("No line component here: the line sag check is skipped."));
	}
	Controller->UnPossess();
	return true;
}

// =====================================================================================================================
// Input: hold the Cast button to reel
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightHoldToReel, "Project.Fishing.Fight.HoldToReelInput", LureFishFightTest::TestFlags)
bool FLureFightHoldToReel::RunTest(const FString& Parameters)
{
	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	FFightWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	World.AddSpot(FVector(1400.f, 0.f, 0.f), { TEXT("Spot=test_shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"), TEXT("Radius=1200") });
	ALurePlayerCharacter* Character = World.Spawn(StandAt);
	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!Character || !Controller)
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.Tick(10);
	ULureFishingComponent* Fishing = SetUp(Character, Tables, Data);
	const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Character->InputComponent);
	UInputAction* CastAction = ULureInputSubsystem::GetInputActionByName(TEXT("Cast"));
	if (!TestNotNull(TEXT("input"), Input) || !TestNotNull(TEXT("Cast action"), CastAction) || !CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	struct FInstance : public FInputActionInstance
	{
		FInstance(const UInputAction* Action, ETriggerEvent Event) : FInputActionInstance(Action) { TriggerEvent = Event; Value = FInputActionValue(Event != ETriggerEvent::Completed); }
	};
	auto Fire = [Input](const UInputAction* Action, ETriggerEvent Event)
	{
		const FInstance Instance(Action, Event);
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				Binding->Execute(Instance);
			}
		}
	};

	// Press to hook during the bite and keep holding: the reel starts right away.
	TestTrue(TEXT("a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240));
	Fire(CastAction, ETriggerEvent::Started);
	TestEqual(TEXT("the press hooks"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
	World.Tick(2);
	TestTrue(TEXT("holding: the player wants to reel"), Fishing->WantsToReel());
	TestTrue(TEXT("holding: the server reels"), Fishing->IsServerReeling());
	TestTrue(TEXT("holding: the fight shows it"), Fishing->GetFightNet().bReeling);
	const float LineWhileReeling = Fishing->GetFightNet().LineOut;

	// Release: let it run.
	Fire(CastAction, ETriggerEvent::Completed);
	World.Tick(2);
	TestFalse(TEXT("released: no reeling"), Fishing->IsServerReeling());
	TestTrue(TEXT("released: HUD says hold to reel"), Fishing->GetStatusText().Contains(TEXT("Hold Click/RT to reel")));

	// Hold again until it is landed.
	Fire(CastAction, ETriggerEvent::Started);
	TestTrue(TEXT("holding lands the fish"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 60 * 90));
	TestTrue(TEXT("the line came in while reeling"), LineWhileReeling > 0.f);
	TestTrue(TEXT("result: landed, snapped or thrown (the fight decided)"), Fishing->GetNetState().LastResult == ELureFishingResult::Landed
		|| Fishing->GetNetState().LastResult == ELureFishingResult::Snapped || Fishing->GetNetState().LastResult == ELureFishingResult::ThrewHook);
	World.Tick(2);
	TestFalse(TEXT("after the fight: no reel request"), Fishing->WantsToReel());
	Fire(CastAction, ETriggerEvent::Completed);
	Controller->UnPossess();
	return true;
}

// =====================================================================================================================
// Balance rule: the reel's drag never exceeds what the line holds
// =====================================================================================================================

/**
 *  Effective drag = min(rod Drag, LineStrength x DragLineCap) (DT_FishFight, optional column, default 0.9): buying the rod
 *  before the line never makes a fish harder. Letting a strong fish run on the reef rod + starter line never snaps it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightDragNeverExceedsLine, "Project.Fishing.Fight.DragNeverExceedsTheLine", LureFishFightTest::TestFlags)
bool FLureFightDragNeverExceedsLine::RunTest(const FString& Parameters)
{
	FFightData Data;
	if (!Data.Load(*this))
	{
		return false;
	}
	const FLureFishFightRow& Tuning = *Data.Tuning();
	TestNearlyEqual(TEXT("shipped DragLineCap"), Tuning.DragLineCap, 0.9f, 1.0e-6f);
	TestNearlyEqual(TEXT("the struct default (optional column) is 0.9"), FLureFishFightRow().DragLineCap, 0.9f, 1.0e-6f);

	// The rule, on every rod x line pair in DT_Gear.
	TArray<FName> Rods, Lines;
	Data.Gear->ForeachRow<FLureGearRow>(TEXT("test"), [&Rods, &Lines](const FName& Id, const FLureGearRow& Row)
	{
		if (Row.Slot == ELureGearSlot::Rod)
		{
			Rods.Add(Id);
		}
		else if (Row.Slot == ELureGearSlot::Line)
		{
			Lines.Add(Id);
		}
	});
	TestTrue(TEXT("DT_Gear has rods and lines"), Rods.Num() > 0 && Lines.Num() > 0);
	for (const FName Rod : Rods)
	{
		for (const FName Line : Lines)
		{
			FLureGearStats Stats = Data.Gear3(Rod, Line, TEXT("Hook_Shrimp"));
			const float RodDrag = Stats.Drag;
			FLureGear::ApplyDragLineCap(Stats, Tuning.DragLineCap);
			const FString Label = FString::Printf(TEXT("%s + %s"), *Rod.ToString(), *Line.ToString());
			TestNearlyEqual(Label + TEXT(": drag = min(rod drag, line x cap)"), Stats.Drag, FMath::Min(RodDrag, Stats.LineStrength * Tuning.DragLineCap), 1.0e-5f);
			TestTrue(Label + TEXT(": drag below the line's strength"), Stats.Drag < Stats.LineStrength);
		}
	}

	// Edge values: a bad cap is ignored, a cap of 1 allows drag up to the strength, never raises a weak rod's drag.
	FLureGearStats Edge;
	Edge.Drag = 12.f;
	Edge.LineStrength = 10.f;
	for (const float Bad : { 0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
	{
		FLureGearStats Copy = Edge;
		FLureGear::ApplyDragLineCap(Copy, Bad);
		TestEqual(FString::Printf(TEXT("cap %f is ignored"), Bad), Copy.Drag, 12.f);
	}
	{
		FLureGearStats Copy = Edge;
		FLureGear::ApplyDragLineCap(Copy, 1.f);
		TestEqual(TEXT("cap 1: drag = the line's strength"), Copy.Drag, 10.f);
		Copy.Drag = 3.f;
		FLureGear::ApplyDragLineCap(Copy, 0.9f);
		TestEqual(TEXT("a weak rod's drag is unchanged"), Copy.Drag, 3.f);
	}

	// Validation: DragLineCap must be in (0, 1].
	for (const float Bad : { 0.f, -0.5f, 1.01f, std::numeric_limits<float>::quiet_NaN() })
	{
		FLureFishFightRow Row = Tuning;
		Row.DragLineCap = Bad;
		FString Problem;
		TestFalse(FString::Printf(TEXT("DragLineCap %f is invalid"), Bad), Row.Validate(Problem));
	}
	{
		FLureFishFightRow Row = Tuning;
		Row.DragLineCap = 1.f;
		FString Problem;
		TestTrue(TEXT("DragLineCap 1 is valid: ") + Problem, Row.Validate(Problem));
	}
	// The optional column may be left out of the CSV (default 0.9).
	{
		TArray<FString> Lines2;
		Data.FightCsv.ParseIntoArrayLines(Lines2);
		TArray<FString> Header, Values;
		Lines2[0].ParseIntoArray(Header, TEXT(","), false);
		Lines2[1].ParseIntoArray(Values, TEXT(","), false);
		const int32 Column = Header.IndexOfByPredicate([](const FString& H) { return H.TrimStartAndEnd() == TEXT("DragLineCap"); });
		if (TestTrue(TEXT("DT_FishFight.csv has a DragLineCap column"), Column != INDEX_NONE))
		{
			Header.RemoveAt(Column);
			Values.RemoveAt(Column);
			TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
			Table->RowStruct = FLureFishFightRow::StaticStruct();
			const TArray<FString> Problems = Table->CreateTableFromCSVString(FString::Join(Header, TEXT(",")) + TEXT("\n") + FString::Join(Values, TEXT(",")) + TEXT("\n"));
			TestEqual(TEXT("imports without the column: ") + FString::Join(Problems, TEXT(" | ")), Problems.Num(), 0);
			const FLureFishFightRow* Row = Table->FindRow<FLureFishFightRow>(TEXT("Default"), TEXT("test"), false);
			TestTrue(TEXT("... and uses the default 0.9"), Row && FMath::IsNearlyEqual(Row->DragLineCap, 0.9f));
		}
	}

	// The balance: a never-tiring fish pulling twice the starter line, left to run on the reef rod + starter line.
	// Capped (9 < 10) the line holds; the raw rod drag (12) would snap it.
	FLureFishFightRow Instant = Tuning;
	Instant.TiredPull = 1.f;
	FLureGearStats Capped = Data.Gear3(TEXT("Rod_Reef"), TEXT("Line_Mono"), TEXT("Hook_Shrimp"));
	FLureGearStats Raw = Capped;
	FLureGear::ApplyDragLineCap(Capped, Tuning.DragLineCap);
	TestTrue(TEXT("fixture: the reef rod's own drag is above the starter line"), Raw.Drag > Raw.LineStrength);
	FLureFightState Held = SteadyFight(Instant, 2.f * Raw.LineStrength, 0.f, Move(TEXT("Hold"), 1.f, 0.f, 0.f), Capped);
	FLureFightState Snapped = SteadyFight(Instant, 2.f * Raw.LineStrength, 0.f, Move(TEXT("Hold"), 1.f, 0.f, 0.f), Raw);
	const FLureFightInput Let;
	for (int32 i = 0; i < 600; ++i)
	{
		FLureFight::Step(Held, Let);
		FLureFight::Step(Snapped, Let);
	}
	TestEqual(TEXT("capped drag: letting it run never snaps the line (10 s)"), OutcomeName(Held.Outcome), OutcomeName(ELureFightOutcome::None));
	TestEqual(TEXT("uncapped rod drag would snap it"), OutcomeName(Snapped.Outcome), OutcomeName(ELureFightOutcome::Snapped));
	return true;
}

// =====================================================================================================================
// Fishing-loop playtest tune (2026-09-23): the bonefish's Run punishes holding reel; careful play is clearly safer
// =====================================================================================================================

/** A starter-kit fight played to the end; PeakTension01 = the highest tension / line strength seen. */
struct FBalanceRun
{
	ELureFightOutcome Outcome = ELureFightOutcome::None;
	float Elapsed = 0.f;
	float PeakTension01 = 0.f;
};

/**
 *  Hold, Careful (the tension-bar watcher of RunPlayer), or with bRunAware: lets every aggressive move (the HUD's "running!")
 *  run and reels the rest, reacting 0.3 s after each move change.
 */
FBalanceRun PlayBalance(FLureFightState State, EPlayer Player, bool bRunAware = false)
{
	FBalanceRun Out;
	bool bReel = Player == EPlayer::Hold;
	const float Step = FLureFight::StepSeconds(State.Tuning);
	float SinceDecision = 1000.f;
	int32 SeenMove = -2;
	while (!State.IsOver() && State.Elapsed < 180.f)
	{
		if (bRunAware)
		{
			const int32 MoveIndex = State.bExhausted ? INDEX_NONE : State.MoveIndex;
			const FLureFightMove* Move = State.bExhausted ? nullptr : State.GetMove();
			SinceDecision = MoveIndex != SeenMove ? 0.f : SinceDecision + Step;
			SeenMove = MoveIndex;
			if (SinceDecision >= 0.3f - 1.0e-4f)
			{
				bReel = !(Move && Move->AggressionWeight > 0.f);
			}
		}
		else if (Player == EPlayer::Careful)
		{
			SinceDecision += Step;
			if (SinceDecision >= 0.3f - 1.0e-4f)
			{
				SinceDecision = 0.f;
				bReel = CarefulDecision(State.Tension / State.Gear.LineStrength, bReel);
			}
		}
		FLureFightInput Input;
		Input.bReeling = bReel;
		FLureFight::Step(State, Input);
		Out.PeakTension01 = FMath::Max(Out.PeakTension01, State.Tension / State.Gear.LineStrength);
	}
	Out.Outcome = State.Outcome;
	Out.Elapsed = State.Elapsed;
	return Out;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightBonefishRunBalance, "Project.Fishing.Fight.BonefishRunPunishesHoldReel", LureFishFightTest::TestFlags)
bool FLureFightBonefishRunBalance::RunTest(const FString& Parameters)
{
	FFightData Data;
	FishQA::FTables Tables;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Tables))
	{
		return false;
	}
	const FLureGearStats Starter = Data.Starter();
	const FName Run(TEXT("Run"));
	const FName Dive(TEXT("Dive"));
	// Weight fractions are positions in the species range (0 = WeightMin, 1 = WeightMax): Bonefish 0.5..4.5 kg.
	auto Fraction = [](float Kg) { return (Kg - 0.5f) / 4.f; };

	// 1. The playtest's fish (Rare, 2.04 kg) and a 3 kg Common: holding reel through the opening Run snaps the starter line;
	//    the careful player (eases off above 90 % of the line) lands them within the 8-16 s fight length.
	struct FCase { const TCHAR* Rarity; float Kg; int32 Seed; };
	for (const FCase& Case : { FCase{ TEXT("Rare"), 2.04f, 31 }, FCase{ TEXT("Common"), 3.f, 32 } })
	{
		FFishInstance Fish;
		if (!RollFish(*this, Tables, TEXT("Bonefish"), Case.Rarity, Fraction(Case.Kg), Case.Seed, Fish))
		{
			return false;
		}
		for (int32 Seed = 1; Seed <= 6; ++Seed)
		{
			const FBalanceRun Hold = PlayBalance(BeginFight(Data, Fish, Run, Starter, Seed), EPlayer::Hold);
			const FBalanceRun Careful = PlayBalance(BeginFight(Data, Fish, Run, Starter, Seed), EPlayer::Careful);
			const FString Label = FString::Printf(TEXT("%s %.2f kg bonefish, seed %d"), Case.Rarity, Fish.WeightKg, Seed);
			TestEqual(FString::Printf(TEXT("%s: holding reel through the Run snaps the starter line (peak %.0f %%, %.1f s)"), *Label, 100.f * Hold.PeakTension01, Hold.Elapsed),
				OutcomeName(Hold.Outcome), OutcomeName(ELureFightOutcome::Snapped));
			TestEqual(FString::Printf(TEXT("%s: careful play lands it (%.1f s)"), *Label, Careful.Elapsed), OutcomeName(Careful.Outcome), OutcomeName(ELureFightOutcome::Landed));
			TestTrue(FString::Printf(TEXT("%s: ... in 8-16 s (%.1f s)"), *Label, Careful.Elapsed), Careful.Elapsed >= 8.f && Careful.Elapsed <= 16.f);
		}
	}

	// 2. A typical Common (1.5 kg): holding reel takes the tension near the snap mark (>= 80 %) without snapping it.
	FFishInstance TypicalBonefish;
	if (!RollFish(*this, Tables, TEXT("Bonefish"), TEXT("Common"), Fraction(1.5f), 33, TypicalBonefish))
	{
		return false;
	}
	{
		const FBalanceRun Hold = PlayBalance(BeginFight(Data, TypicalBonefish, Run, Starter, 1), EPlayer::Hold);
		TestTrue(FString::Printf(TEXT("typical 1.5 kg bonefish: holding reel peaks near the snap mark (%.0f %% of the line)"), 100.f * Hold.PeakTension01),
			Hold.PeakTension01 >= 0.8f && Hold.PeakTension01 <= 1.f);
		TestEqual(TEXT("... and lands it"), OutcomeName(Hold.Outcome), OutcomeName(ELureFightOutcome::Landed));
	}

	// 3. Bonefish as the roll pipeline gives them (random rarity, weight and modifiers; noon at Palm Key), starter kit:
	//    holding reel loses a real share to snaps, careful play loses (almost) none, careful fights take 8-16 s.
	int32 Rolled = 0, HoldSnaps = 0, CarefulLosses = 0, AwareLosses = 0;
	TArray<float> CarefulTimes, AwareTimes, HoldTimes;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
	{
		FFishRollContext Context;
		Context.SpeciesId = TEXT("Bonefish");
		Context.Seed = 5000 + Seed;
		Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
		Context.TimeOfDayHours = 12.f;
		FFishInstance Fish;
		if (!FFishRoll::Roll(Tables.Get(), Context, Fish))
		{
			continue;
		}
		++Rolled;
		const FBalanceRun Hold = PlayBalance(BeginFight(Data, Fish, Run, Starter, Seed), EPlayer::Hold);
		const FBalanceRun Careful = PlayBalance(BeginFight(Data, Fish, Run, Starter, Seed), EPlayer::Careful);
		const FBalanceRun Aware = PlayBalance(BeginFight(Data, Fish, Run, Starter, Seed), EPlayer::Careful, /*bRunAware*/ true);
		HoldSnaps += Hold.Outcome == ELureFightOutcome::Snapped ? 1 : 0;
		if (Hold.Outcome == ELureFightOutcome::Landed) { HoldTimes.Add(Hold.Elapsed); }
		if (Careful.Outcome == ELureFightOutcome::Landed) { CarefulTimes.Add(Careful.Elapsed); } else { ++CarefulLosses; }
		if (Aware.Outcome == ELureFightOutcome::Landed) { AwareTimes.Add(Aware.Elapsed); } else { ++AwareLosses; }
	}
	auto Percentile = [](TArray<float> Times, float P) { Times.Sort(); return Times.Num() ? Times[FMath::Clamp(FMath::FloorToInt(P * Times.Num()), 0, Times.Num() - 1)] : 0.f; };
	AddInfo(FString::Printf(TEXT("%d rolled bonefish, starter kit: hold snaps %d (landed median %.1f s); careful loses %d (median %.1f s, p10 %.1f s, p90 %.1f s); run-aware loses %d (median %.1f s, p90 %.1f s)"),
		Rolled, HoldSnaps, Percentile(HoldTimes, 0.5f), CarefulLosses, Percentile(CarefulTimes, 0.5f), Percentile(CarefulTimes, 0.1f), Percentile(CarefulTimes, 0.9f),
		AwareLosses, Percentile(AwareTimes, 0.5f), Percentile(AwareTimes, 0.9f)));
	TestTrue(TEXT("fixture: the roll pipeline gave bonefish"), Rolled >= 190);
	TestTrue(FString::Printf(TEXT("holding reel snaps a real share of bonefish (%d of %d, want 25-60 %%)"), HoldSnaps, Rolled), HoldSnaps >= Rolled / 4 && HoldSnaps <= Rolled * 3 / 5);
	TestTrue(FString::Printf(TEXT("careful play is clearly safer (loses %d of %d)"), CarefulLosses, Rolled), CarefulLosses <= Rolled / 50);
	TestTrue(FString::Printf(TEXT("easing off during every run is safe too (loses %d of %d)"), AwareLosses, Rolled), AwareLosses <= Rolled / 50);
	TestTrue(FString::Printf(TEXT("careful fights take about 8-16 s (p10 %.1f s >= 7.5, p90 %.1f s <= 16)"), Percentile(CarefulTimes, 0.1f), Percentile(CarefulTimes, 0.9f)),
		Percentile(CarefulTimes, 0.1f) >= 7.5f && Percentile(CarefulTimes, 0.9f) <= 16.f);
	TestTrue(FString::Printf(TEXT("easing off during every run: median within 16 s (%.1f s)"), Percentile(AwareTimes, 0.5f)), Percentile(AwareTimes, 0.5f) <= 16.f);

	// 4. The Coral Snapper stays the harder fish: the reference snapper snaps a held line every time and a careful fight is longer.
	FFishInstance Snapper;
	if (!RollFish(*this, Tables, TEXT("CoralSnapper"), TEXT("Common"), SnapperReferenceFraction, 21, Snapper))
	{
		return false;
	}
	double SnapperCareful = 0.0, BonefishCareful = 0.0;
	for (int32 Seed = 1; Seed <= 6; ++Seed)
	{
		const FBalanceRun SnapHold = PlayBalance(BeginFight(Data, Snapper, Dive, Starter, Seed), EPlayer::Hold);
		TestEqual(FString::Printf(TEXT("reference snapper, seed %d: holding reel snaps the starter line"), Seed), OutcomeName(SnapHold.Outcome), OutcomeName(ELureFightOutcome::Snapped));
		SnapperCareful += PlayBalance(BeginFight(Data, Snapper, Dive, Starter, Seed), EPlayer::Careful).Elapsed;
		BonefishCareful += PlayBalance(BeginFight(Data, TypicalBonefish, Run, Starter, Seed), EPlayer::Careful).Elapsed;
	}
	TestTrue(FString::Printf(TEXT("a careful snapper fight takes longer than a careful bonefish fight (%.1f s vs %.1f s)"), SnapperCareful / 6.0, BonefishCareful / 6.0),
		SnapperCareful > BonefishCareful);
	return true;
}

} // namespace LureFishFightTest

#endif // WITH_DEV_AUTOMATION_TESTS

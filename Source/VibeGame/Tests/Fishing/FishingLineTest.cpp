// Lure T-032: the physics fishing line (implementer's tests; QA adds its own). Project.Fishing.Line.*
// Spec: docs/specs/fishing-line.md. The solver (FLureLineSim) is tested without a world; the component in transient test
// worlds; the integration test casts, waits and fights in a QA scene (dock + tagged water at z = 0).
// Tuning comes from the built-in row (= data/tables/DT_FishingLine.csv, checked in Data.SourceRowsValid).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishFight.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Templates/Function.h"
#include "Tests/AutomationCommon.h"
#include "Tests/Fishing/QAFishingTestUtils.h"
#include "Tests/FishQATestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include <cmath>
#include <limits>

#if MALLOC_GT_HOOKS
// Core's game-thread allocation hook (UnrealMemory.cpp; STATS builds): called with 0 = malloc, 1 = realloc, 2 = free.
extern CORE_API TFunction<void(int32)>* GGameThreadMallocHook;
#endif

namespace LureFishingLineTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = 1.f / 60.f;
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();

	FLureFishingLineRow Tuning()
	{
		return FLureFishingLineRules::GetFallbackRow();
	}

	FLureLineSimInput Pinned(const FVector& Start, const FVector& End, float RestLength)
	{
		FLureLineSimInput In;
		In.Start = Start;
		In.End = End;
		In.RestLength = RestLength;
		return In;
	}

	FLureLineSimInput Hanging(const FVector& Tip, float Length, float EndMass, float EndDrag)
	{
		FLureLineSimInput In;
		In.Start = Tip;
		In.End = Tip - FVector(0.f, 0.f, Length);
		In.bFreeEnd = true;
		In.EndMass = EndMass;
		In.EndDrag = EndDrag;
		In.RestLength = Length;
		return In;
	}

	void Run(FLureLineSim& Sim, const FLureLineSimInput& In, const FLureFishingLineRow& Row, float Seconds, float StepDt = Dt)
	{
		const int32 Frames = FMath::CeilToInt32(Seconds / StepDt);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Sim.Step(StepDt, In, Row);
		}
	}

	/** Largest distance of any point from the straight segment between the first and the last point, cm. */
	double MaxDeviation(const TArray<FVector>& Points)
	{
		double Max = 0.0;
		for (const FVector& Point : Points)
		{
			Max = FMath::Max(Max, static_cast<double>(FMath::PointDistToSegment(Point, Points[0], Points.Last())));
		}
		return Max;
	}

	/** Sag at the middle of a uniform line Length long hung between two points at the same height Span apart (the catenary), cm. */
	double CatenarySag(double Span, double Length)
	{
		// sinh(u) / u = Length / Span with u = Span / (2a); sag = a (cosh u - 1).
		double Low = 1.0e-6;
		double High = 20.0;
		for (int32 Iteration = 0; Iteration < 200; ++Iteration)
		{
			const double Mid = 0.5 * (Low + High);
			(std::sinh(Mid) / Mid < Length / Span ? Low : High) = Mid;
		}
		const double U = 0.5 * (Low + High);
		return Span / (2.0 * U) * (std::cosh(U) - 1.0);
	}

	/** Counts game-thread allocations (malloc, realloc, free) while Body runs. False if the hook is not available in this build. */
	bool CountAllocations(TFunctionRef<void()> Body, int32& OutCount)
	{
		OutCount = 0;
#if MALLOC_GT_HOOKS
		int32 Count = 0;
		TFunction<void(int32)> Hook([&Count](int32) { ++Count; });
		{
			// Positive control: the hook must see a known allocation, else it is not active here.
			TGuardValue<TFunction<void(int32)>*> Guard(GGameThreadMallocHook, &Hook);
			void* Probe = FMemory::Malloc(64);
			FMemory::Free(Probe);
		}
		if (Count == 0)
		{
			return false;
		}
		Count = 0;
		{
			TGuardValue<TFunction<void(int32)>*> Guard(GGameThreadMallocHook, &Hook);
			Body();
		}
		OutCount = Count;
		return true;
#else
		Body();
		return false;
#endif
	}

	/** A transient game world with one plain actor that owns a line component. */
	struct FLineWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		ULureFishingLineComponent* Line = nullptr;

		bool Create(FAutomationTestBase& Test, bool bMesh = true, int32 Segments = 12)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			Owner = SpawnMovable(FVector::ZeroVector);
			if (!Owner)
			{
				return false;
			}
			Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("TestLine"), RF_Transient);
			Line->SetupAttachment(Owner->GetRootComponent());
			Line->RegisterComponent();
			UStaticMesh* Cylinder = bMesh ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")) : nullptr;
			if (bMesh && !Test.TestNotNull(TEXT("the engine cylinder mesh loads"), Cylinder))
			{
				return false;
			}
			Line->Setup(Cylinder, nullptr, FLinearColor::White, Segments);
			Line->SetTuning(Tuning());
			return true;
		}

		/** A plain actor with a scene root (so it can be moved). */
		AActor* SpawnMovable(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			if (Actor)
			{
				USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
				Actor->SetRootComponent(Root);
				Root->RegisterComponent();
				Root->SetWorldLocation(Location);
			}
			return Actor;
		}

		void Tick(int32 Frames, float DeltaTime = Dt)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(DeltaTime);
			}
		}
	};

	/** The shipped fight tables with a weak starter line (LineStrength), so a fight snaps it within a second or two. */
	struct FWeakLineTables
	{
		TStrongObjectPtr<UDataTable> Gear;
		TStrongObjectPtr<UDataTable> Patterns;
		TStrongObjectPtr<UDataTable> Fight;

		bool Load(FAutomationTestBase& Test, float LineStrength)
		{
			FString GearCsv;
			FString PatternJson;
			FString FightCsv;
			if (!QAFishing::LoadSource(Test, TEXT("DT_Gear.csv"), GearCsv) || !QAFishing::LoadSource(Test, TEXT("DT_FightPattern.json"), PatternJson)
				|| !QAFishing::LoadSource(Test, TEXT("DT_FishFight.csv"), FightCsv))
			{
				return false;
			}
			const FString WeakGear = QAFishing::WithCell(Test, GearCsv, TEXT("Line_Mono"), TEXT("LineStrength"), FString::SanitizeFloat(LineStrength));
			Gear.Reset(QAFishing::MakeTableChecked(Test, FLureGearRow::StaticStruct(), WeakGear, TEXT("DT_Gear (weak line)")));
			Fight.Reset(QAFishing::MakeTableChecked(Test, FLureFishFightRow::StaticStruct(), FightCsv, TEXT("DT_FishFight")));
			Patterns.Reset(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
			Patterns->RowStruct = FLureFightPatternRow::StaticStruct();
			const TArray<FString> Problems = Patterns->CreateTableFromJSONString(PatternJson);
			Test.TestEqual(FString::Printf(TEXT("DT_FightPattern imports (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
			return Gear.IsValid() && Fight.IsValid() && Problems.Num() == 0;
		}
	};

	bool RollBonefish(FAutomationTestBase& Test, const FishQA::FTables& Tables, FFishInstance& Out)
	{
		FFishRollContext Context;
		Context.SpeciesId = TEXT("Bonefish");
		Context.Seed = 71;
		Context.ForcedRarityId = TEXT("Common");
		Context.bForceModifiers = true;
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = 0.5f;
		Context.RegionTag = FGameplayTag::RequestGameplayTag(TEXT("Region.Tropical.PalmKey"), /*ErrorIfNotFound*/ false);
		Context.TimeOfDayHours = 12.f;
		return Test.TestTrue(TEXT("roll a Common bonefish"), FFishRoll::Roll(Tables.Get(), Context, Out));
	}

	// =================================================================================================================
	// Rules and data
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineRulesTest, "Project.Fishing.Line.Rules.TensionStraightensTheLine", Flags)
	bool FLureLineRulesTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		TestEqual(TEXT("no tension: not taut"), FLureFishingLineRules::Tautness(0.f, Row), 0.f);
		TestEqual(TEXT("the snap threshold (1): fully taut"), FLureFishingLineRules::Tautness(1.f, Row), 1.f);
		TestEqual(TEXT("NaN tension = slack"), FLureFishingLineRules::Tautness(NaN, Row), 0.f);
		TestEqual(TEXT("negative tension = slack"), FLureFishingLineRules::Tautness(-3.f, Row), 0.f);
		TestEqual(TEXT("over the line's strength = fully taut"), FLureFishingLineRules::Tautness(7.f, Row), 1.f);
		TestTrue(TEXT("still a hair of slack just under the snap threshold"), FLureFishingLineRules::TargetRestLength(1000.f, 0.95f, -1.f, Row) > 1000.f);

		bool bTautRises = true;
		bool bLengthFalls = true;
		float PreviousTaut = -1.f;
		float PreviousLength = TNumericLimits<float>::Max();
		for (float Tension = 0.f; Tension <= 1.0001f; Tension += 0.05f)
		{
			const float Taut = FLureFishingLineRules::Tautness(Tension, Row);
			const float Length = FLureFishingLineRules::TargetRestLength(1000.f, Tension, -1.f, Row);
			bTautRises &= Taut >= PreviousTaut;
			bLengthFalls &= Length <= PreviousLength;
			PreviousTaut = Taut;
			PreviousLength = Length;
		}
		TestTrue(TEXT("tautness rises with tension"), bTautRises);
		TestTrue(TEXT("the line's length falls as tension rises (it straightens)"), bLengthFalls);
		TestNearlyEqual(TEXT("slack: SlackShare extra line"), FLureFishingLineRules::TargetRestLength(1000.f, 0.f, -1.f, Row), 1000.f * (1.f + Row.SlackShare), 1.0e-3f);
		TestNearlyEqual(TEXT("at the snap threshold: exactly the straight distance"), FLureFishingLineRules::TargetRestLength(1000.f, 1.f, -1.f, Row), 1000.f, 1.0e-3f);
		TestNearlyEqual(TEXT("SetSlack overrides the share"), FLureFishingLineRules::TargetRestLength(1000.f, 0.f, 0.2f, Row), 1200.f, 1.0e-3f);
		TestNearlyEqual(TEXT("NaN slack = the row's share"), FLureFishingLineRules::TargetRestLength(1000.f, 0.f, NaN, Row), 1000.f * (1.f + Row.SlackShare), 1.0e-3f);
		TestEqual(TEXT("NaN distance = no line"), FLureFishingLineRules::TargetRestLength(NaN, 0.f, -1.f, Row), 0.f);

		TestEqual(TEXT("slack appears at once"), FLureFishingLineRules::FollowRestLength(1000.f, 1100.f, 1000.f, Dt, Row), 1100.f);
		TestNearlyEqual(TEXT("the line tightens smoothly (1 / LengthResponse s: 63 %)"),
			FLureFishingLineRules::FollowRestLength(1100.f, 1000.f, 1000.f, 1.f / Row.LengthResponse, Row), 1000.f + 100.f * FMath::Exp(-1.f), 0.05f);
		TestEqual(TEXT("never shorter than the straight distance"), FLureFishingLineRules::FollowRestLength(1100.f, 1000.f, 1050.f, 10.f, Row), 1050.f);
		TestEqual(TEXT("NaN length recovers to the target"), FLureFishingLineRules::FollowRestLength(NaN, 1000.f, 900.f, Dt, Row), 1000.f);

		TestEqual(TEXT("casting shows CastTension"), FLureFishingLineRules::StateTension(ELureFishingState::Casting, false, 0.f, Row), Row.CastTension);
		TestEqual(TEXT("waiting shows WaitTension"), FLureFishingLineRules::StateTension(ELureFishingState::Waiting, false, 0.f, Row), Row.WaitTension);
		TestEqual(TEXT("a bite shows BiteTension"), FLureFishingLineRules::StateTension(ELureFishingState::Biting, false, 0.f, Row), Row.BiteTension);
		TestEqual(TEXT("hooked without a fight: HookedTension"), FLureFishingLineRules::StateTension(ELureFishingState::Hooked, false, 0.3f, Row), Row.HookedTension);
		TestEqual(TEXT("a reel fight shows the fight's tension"), FLureFishingLineRules::StateTension(ELureFishingState::Hooked, true, 0.42f, Row), 0.42f);
		TestEqual(TEXT("... clamped at the snap threshold"), FLureFishingLineRules::StateTension(ELureFishingState::Hooked, true, 1.7f, Row), 1.f);
		TestEqual(TEXT("... NaN = 0"), FLureFishingLineRules::StateTension(ELureFishingState::Hooked, true, NaN, Row), 0.f);
		TestEqual(TEXT("idle: 0"), FLureFishingLineRules::StateTension(ELureFishingState::Idle, false, 0.9f, Row), 0.f);
		TestEqual(TEXT("slack line floats at FloatStrength"), FLureFishingLineRules::FloatAmount(0.f, Row), Row.FloatStrength);
		TestEqual(TEXT("a taut line does not float"), FLureFishingLineRules::FloatAmount(1.f, Row), 0.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineDataTest, "Project.Fishing.Line.Data.SourceRowsValid", Flags)
	bool FLureLineDataTest::RunTest(const FString& Parameters)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_FishingLine.csv"));
		FString Csv;
		if (!TestTrue(TEXT("data/tables/DT_FishingLine.csv loads"), FFileHelper::LoadFileToString(Csv, *Path)))
		{
			return false;
		}
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = FLureFishingLineRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		TestEqual(FString::Printf(TEXT("imports cleanly (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		TestTrue(TEXT("has rows"), Table->GetRowMap().Num() > 0);
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FLureFishingLineRow* Row = reinterpret_cast<const FLureFishingLineRow*>(Pair.Value);
			FString Problem;
			TestTrue(FString::Printf(TEXT("row %s is valid (%s)"), *Pair.Key.ToString(), *Problem), Row && Row->Validate(Problem));
		}

		const FLureFishingLineRow* Default = Table->FindRow<FLureFishingLineRow>(TEXT("Default"), TEXT("test"), false);
		if (TestNotNull(TEXT("a Default row"), Default))
		{
			const FLureFishingLineRow BuiltIn = FLureFishingLineRules::GetFallbackRow();
			TArray<FString> Different;
			for (TFieldIterator<FProperty> It(FLureFishingLineRow::StaticStruct()); It; ++It)
			{
				if (!It->Identical_InContainer(Default, &BuiltIn))
				{
					Different.Add(It->GetName());
				}
			}
			TestEqual(FString::Printf(TEXT("the built-in row equals the shipped Default row (differs: %s)"), *FString::Join(Different, TEXT(", "))), Different.Num(), 0);
		}

		// Data-driven: a new kind of line is a new row, no code (e.g. a floating braid).
		const FString WithBraid = Csv.TrimEnd() + TEXT("\nBraid,120,8,6,1.0,1.5,8.0,0.08,2.0,5.0,0.3,0.35,0.0,0.6,0.8,0.8,0.6,500,3000,0.6,0.25,25,0.4,1500,1.0,8.0,300,0.2,500,70\n");
		TStrongObjectPtr<UDataTable> Extended(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Extended->RowStruct = FLureFishingLineRow::StaticStruct();
		TestEqual(TEXT("a new row imports"), Extended->CreateTableFromCSVString(WithBraid).Num(), 0);
		const FLureFishingLineRow* Braid = Extended->FindRow<FLureFishingLineRow>(TEXT("Braid"), TEXT("test"), false);
		FString BraidProblem;
		TestTrue(TEXT("the new row is valid"), Braid && Braid->Validate(BraidProblem));

		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		TestEqual(TEXT("settings: FishingLineTable"), Settings->FishingLineTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_FishingLine.DT_FishingLine")));
		TestEqual(TEXT("settings: FishingLineRow"), Settings->FishingLineRow, FName(TEXT("Default")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineValidateTest, "Project.Fishing.Line.Data.ValidateRejectsBadRows", Flags)
	bool FLureLineValidateTest::RunTest(const FString& Parameters)
	{
		struct FCase
		{
			const TCHAR* Name;
			TFunction<void(FLureFishingLineRow&)> Break;
		};
		const FCase Cases[] = {
			{ TEXT("NaN gravity"), [](FLureFishingLineRow& R) { R.GravityScale = std::numeric_limits<float>::quiet_NaN(); } },
			{ TEXT("negative drag"), [](FLureFishingLineRow& R) { R.AirDrag = -1.f; } },
			{ TEXT("SubstepRate 10"), [](FLureFishingLineRow& R) { R.SubstepRate = 10.f; } },
			{ TEXT("MaxSubsteps 0"), [](FLureFishingLineRow& R) { R.MaxSubsteps = 0; } },
			{ TEXT("Iterations 100"), [](FLureFishingLineRow& R) { R.Iterations = 100; } },
			{ TEXT("SlackShare 2"), [](FLureFishingLineRow& R) { R.SlackShare = 2.f; } },
			{ TEXT("TautExponent 0"), [](FLureFishingLineRow& R) { R.TautExponent = 0.f; } },
			{ TEXT("BiteTension 1.5"), [](FLureFishingLineRow& R) { R.BiteTension = 1.5f; } },
			{ TEXT("FloatStrength 3"), [](FLureFishingLineRow& R) { R.FloatStrength = 3.f; } },
			{ TEXT("RecoilTime 0"), [](FLureFishingLineRow& R) { R.RecoilTime = 0.f; } },
			{ TEXT("HangEndMass 0.5"), [](FLureFishingLineRow& R) { R.HangEndMass = 0.5f; } },
			{ TEXT("Inf teleport distance"), [](FLureFishingLineRow& R) { R.TeleportDistance = std::numeric_limits<float>::infinity(); } },
		};
		FString Problem;
		TestTrue(TEXT("the built-in row is valid"), Tuning().Validate(Problem));
		for (const FCase& Case : Cases)
		{
			FLureFishingLineRow Row = Tuning();
			Case.Break(Row);
			FString Why;
			TestFalse(FString::Printf(TEXT("%s is rejected"), Case.Name), Row.Validate(Why));
			TestFalse(FString::Printf(TEXT("%s: a reason is given"), Case.Name), Why.IsEmpty());
		}

		// The component refuses an invalid row and keeps the built-in one.
		ULureFishingLineComponent* Line = NewObject<ULureFishingLineComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		FLureFishingLineRow Bad = Tuning();
		Bad.Iterations = 0;
		TestFalse(TEXT("SetTuning refuses an invalid row"), Line->SetTuning(Bad));
		TestTrue(TEXT("... and uses the built-in row"), Line->IsUsingFallbackTuning() && Line->GetTuning().Iterations == Tuning().Iterations);
		FLureFishingLineRow Good = Tuning();
		Good.SlackShare = 0.1f;
		TestTrue(TEXT("SetTuning takes a valid row"), Line->SetTuning(Good) && !Line->IsUsingFallbackTuning() && Line->GetTuning().SlackShare == 0.1f);
		return true;
	}

	// =================================================================================================================
	// The rope (FLureLineSim, no world)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineSagTest, "Project.Fishing.Line.Sim.SagsWhenSlack", Flags)
	bool FLureLineSagTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		FLureLineSim Sim;
		Sim.Init(12);
		const FVector A(0.f, 0.f, 1000.f);
		const FVector B(1500.f, 0.f, 1000.f);
		const float Length = 1500.f * (1.f + Row.SlackShare); // a slack line (no tension)
		Run(Sim, Pinned(A, B, Length), Row, 5.f);
		const TArray<FVector>& P = Sim.GetPoints();
		const double Sag = 1000.0 - P[6].Z;
		const double Expected = CatenarySag(1500.0, Length);
		TestTrue(FString::Printf(TEXT("the middle sags like a hanging line: %.1f cm vs catenary %.1f cm (15 %%)"), Sag, Expected), FMath::Abs(Sag - Expected) <= 0.15 * Expected);
		bool bBelow = true;
		bool bSymmetric = true;
		for (int32 Index = 1; Index < 12; ++Index)
		{
			bBelow &= P[Index].Z < 1000.0 - 1.0;
			bSymmetric &= FMath::Abs(P[Index].Z - P[12 - Index].Z) < 2.0;
		}
		TestTrue(TEXT("every point in between hangs below the ends"), bBelow);
		TestTrue(TEXT("the sag is symmetric"), bSymmetric);
		TestTrue(TEXT("pinned at both ends"), P[0].Equals(A, 0.01) && P.Last().Equals(B, 0.01));
		const FVector Middle = P[6];
		Run(Sim, Pinned(A, B, Length), Row, 0.5f);
		TestTrue(FString::Printf(TEXT("at rest after 5 s (the middle moves %.3f cm in 0.5 s)"), FVector::Dist(Middle, Sim.GetPoints()[6])), FVector::Dist(Middle, Sim.GetPoints()[6]) < 0.5);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineTautTest, "Project.Fishing.Line.Sim.StraightWhenTaut", Flags)
	bool FLureLineTautTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Fish(1500.f, 0.f, 0.f);
		const float Chord = static_cast<float>(FVector::Dist(Tip, Fish));

		FLureLineSim Sim;
		Sim.Init(12);
		Run(Sim, Pinned(Tip, Fish, FLureFishingLineRules::TargetRestLength(Chord, 1.f, -1.f, Row)), Row, 2.f);
		TestTrue(FString::Printf(TEXT("at the snap threshold the line is straight (max %.3f cm off)"), MaxDeviation(Sim.GetPoints())), MaxDeviation(Sim.GetPoints()) < 1.0);
		bool bEven = true;
		for (int32 Index = 1; Index < Sim.GetPoints().Num(); ++Index)
		{
			bEven &= FMath::IsNearlyEqual(FVector::Dist(Sim.GetPoints()[Index - 1], Sim.GetPoints()[Index]), Chord / 12.0, Chord / 12.0 * 0.01);
		}
		TestTrue(TEXT("... with evenly spaced points"), bEven);

		// Straightens smoothly as tension rises.
		double Previous = TNumericLimits<double>::Max();
		bool bFalls = true;
		FString Trace;
		for (const float Tension : { 0.f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f })
		{
			FLureLineSim Swept;
			Swept.Init(12);
			Run(Swept, Pinned(Tip, Fish, FLureFishingLineRules::TargetRestLength(Chord, Tension, -1.f, Row)), Row, 5.f);
			const double Deviation = MaxDeviation(Swept.GetPoints());
			bFalls &= Deviation <= Previous + 0.5;
			Previous = Deviation;
			Trace += FString::Printf(TEXT(" %.2f:%.1f"), Tension, Deviation);
		}
		AddInfo(TEXT("max distance from straight by tension (cm):") + Trace);
		TestTrue(TEXT("the more tension, the straighter"), bFalls);
		FLureLineSim Slack;
		Slack.Init(12);
		Run(Slack, Pinned(Tip, Fish, FLureFishingLineRules::TargetRestLength(Chord, 0.f, -1.f, Row)), Row, 5.f);
		TestTrue(TEXT("a slack line clearly sags (> 50 cm off straight)"), MaxDeviation(Slack.GetPoints()) > 50.0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFloatTest, "Project.Fishing.Line.Sim.FloatsOnTheWater", Flags)
	bool FLureLineFloatTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		const float WaterZ = 0.f;
		const FVector Tip(0.f, 0.f, 270.f); // rod tip over a dock
		const FVector Bobber(1500.f, 0.f, WaterZ);
		const float Chord = static_cast<float>(FVector::Dist(Tip, Bobber));

		FLureLineSimInput In = Pinned(Tip, Bobber, FLureFishingLineRules::TargetRestLength(Chord, 0.f, -1.f, Row));
		FLureLineSim Dry;
		Dry.Init(12);
		Run(Dry, In, Row, 5.f);
		double Lowest = TNumericLimits<double>::Max();
		for (const FVector& Point : Dry.GetPoints())
		{
			Lowest = FMath::Min(Lowest, Point.Z);
		}
		TestTrue(FString::Printf(TEXT("without water the slack line hangs below the water level (lowest %.1f)"), Lowest), Lowest < WaterZ - 5.0);

		In.bHasWater = true;
		In.WaterZ = WaterZ;
		In.Float = FLureFishingLineRules::FloatAmount(0.f, Row);
		FLureLineSim Wet;
		Wet.Init(12);
		Run(Wet, In, Row, 5.f);
		const double Surface = WaterZ + Row.FloatHeight;
		int32 OnWater = 0;
		bool bAtSurface = true;
		bool bNoneUnder = true;
		FString Heights;
		for (int32 Index = 1; Index < Wet.GetPoints().Num() - 1; ++Index)
		{
			const double Z = Wet.GetPoints()[Index].Z;
			Heights += FString::Printf(TEXT(" %.2f"), Z);
			bNoneUnder &= Z > WaterZ - 1.0;
			if (Z < Surface + 5.0)
			{
				++OnWater;
				bAtSurface &= FMath::Abs(Z - Surface) <= 1.0;
			}
		}
		AddInfo(TEXT("point heights on the water (cm):") + Heights);
		TestTrue(FString::Printf(TEXT("part of the slack line lies on the water (%d points)"), OnWater), OnWater >= 3);
		TestTrue(TEXT("floating points sit at the water surface (+ FloatHeight, 1 cm)"), bAtSurface);
		TestTrue(TEXT("no point sinks under the surface"), bNoneUnder);

		// A taut line cuts straight into the water to a fish below the surface (floating fades with tension).
		const FVector Fish(1500.f, 0.f, -150.f);
		FLureLineSimInput Taut = Pinned(Tip, Fish, static_cast<float>(FVector::Dist(Tip, Fish)));
		Taut.bHasWater = true;
		Taut.WaterZ = WaterZ;
		Taut.Float = FLureFishingLineRules::FloatAmount(1.f, Row);
		FLureLineSim Line;
		Line.Init(12);
		Run(Line, Taut, Row, 3.f);
		TestTrue(FString::Printf(TEXT("a taut line goes straight under the water (max %.3f cm off)"), MaxDeviation(Line.GetPoints())), MaxDeviation(Line.GetPoints()) < 1.0);
		TestTrue(TEXT("... its last points are under the surface"), Line.GetPoints()[11].Z < WaterZ - 50.0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineLengthTest, "Project.Fishing.Line.Sim.LengthConserved", Flags)
	bool FLureLineLengthTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();

		FLureLineSim Slack;
		Slack.Init(12);
		Run(Slack, Pinned(FVector(0.f, 0.f, 1000.f), FVector(1500.f, 0.f, 1000.f), 1575.f), Row, 5.f);
		TestNearlyEqual(TEXT("a hanging slack line keeps its length (2 %)"), Slack.GetPolylineLength(), 1575.0, 0.02 * 1575.0);

		FLureLineSim Taut;
		Taut.Init(12);
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Fish(1500.f, 0.f, 0.f);
		Run(Taut, Pinned(Tip, Fish, static_cast<float>(FVector::Dist(Tip, Fish))), Row, 2.f);
		TestNearlyEqual(TEXT("a taut line is exactly the straight distance (0.5 %)"), Taut.GetPolylineLength(), FVector::Dist(Tip, Fish), 0.005 * FVector::Dist(Tip, Fish));

		// A fish running around at up to ~19 m/s: never stretched more than 3 %.
		FLureLineSim Moving;
		Moving.Init(12);
		double WorstStretch = 0.0;
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			const double T = Frame * Dt;
			const FVector End(1500.0 + 300.0 * FMath::Sin(2.0 * UE_PI * T), 200.0 * FMath::Cos(2.0 * UE_PI * T), -50.0);
			const float Rest = static_cast<float>(FVector::Dist(Tip, End)) * (1.f + Row.SlackShare);
			Moving.Step(Dt, Pinned(Tip, End, Rest), Row);
			WorstStretch = FMath::Max(WorstStretch, Moving.GetPolylineLength() / Moving.GetRestLength() - 1.0);
		}
		TestTrue(FString::Printf(TEXT("a moving end never stretches the line more than 3 %% (worst %.2f %%)"), WorstStretch * 100.0), WorstStretch <= 0.03);

		// A heavy fish hanging on 100 cm while the rod circles: it stays on 100 cm of line.
		FLureLineSim Hang;
		Hang.Init(12);
		double Shortest = TNumericLimits<double>::Max();
		double Longest = 0.0;
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			const double T = Frame * Dt;
			const FVector Rod(30.0 * FMath::Cos(UE_PI * T), 30.0 * FMath::Sin(UE_PI * T), 500.0);
			Hang.Step(Dt, Hanging(Rod, 100.f, Row.HangEndMass, Row.HangDrag), Row);
			if (Frame >= 60)
			{
				const double Reach = FVector::Dist(Hang.GetStart(), Hang.GetEnd());
				Shortest = FMath::Min(Shortest, Reach);
				Longest = FMath::Max(Longest, FMath::Max(Reach, Hang.GetPolylineLength()));
			}
		}
		TestTrue(FString::Printf(TEXT("a hanging fish stays on its 100 cm of line (%.1f .. %.1f cm)"), Shortest, Longest), Shortest >= 95.0 && Longest <= 102.0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineExtremesTest, "Project.Fishing.Line.Sim.NoNaNAtExtremeInputs", Flags)
	bool FLureLineExtremesTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		const FVector Good(0.f, 0.f, 200.f);
		const TArray<FVector> Points = { Good, FVector(NaN, 0.f, 0.f), FVector(Inf, -Inf, 0.f), FVector(1.0e12, -1.0e12, 1.0e12), FVector(1500.f, 0.f, 0.f), Good };
		const TArray<float> Lengths = { 0.f, -5.f, NaN, Inf, 1.0e12f, 1.0e-9f, 1500.f };
		const TArray<float> Times = { 0.f, -1.f, NaN, Inf, 1.0e-9f, Dt, 1.f, 100.f };
		const TArray<float> Masses = { 0.f, NaN, 1.0e12f, 1.f, 25.f };
		const TArray<float> Floats = { NaN, -1.f, 2.f, 0.5f };
		const TArray<float> Waters = { NaN, Inf, 1.0e12f, 0.f };
		int32 Steps = 0;
		int32 Broken = 0;
		for (const int32 Segments : { 0, 1, 12, 64, 1000 })
		{
			FLureLineSim Sim;
			Sim.Init(Segments);
			for (int32 Case = 0; Case < 240; ++Case)
			{
				FLureLineSimInput In;
				In.Start = Points[Case % Points.Num()];
				In.End = Points[(Case / 2 + 3) % Points.Num()];
				In.RestLength = Lengths[Case % Lengths.Num()];
				In.bFreeEnd = (Case % 5) == 0;
				In.EndMass = Masses[Case % Masses.Num()];
				In.EndDrag = (Case % 3) == 0 ? NaN : ((Case % 3) == 1 ? -5.f : 1.0e9f);
				In.bHasWater = (Case % 2) == 0;
				In.WaterZ = Waters[Case % Waters.Num()];
				In.Float = Floats[Case % Floats.Num()];
				Sim.Step(Times[Case % Times.Num()], In, Row);
				if ((Case % 7) == 0)
				{
					Sim.AddRecoil((Case % 2) == 0 ? NaN : 1.0e9f);
					Sim.AddEndVelocity((Case % 2) == 0 ? FVector(NaN) : FVector(1.0e9));
					Sim.ReleaseEnd((Case % 2) == 0 ? NaN : 0.f);
				}
				++Steps;
				Broken += Sim.IsFinite() && !Sim.GetEndDirection().ContainsNaN() && FMath::IsFinite(Sim.GetPolylineLength()) ? 0 : 1;
			}
			// It recovers: good inputs put the line back on the rod tip.
			Sim.Step(Dt, Pinned(Good, FVector(1500.f, 0.f, 0.f), 1600.f), Row);
			Sim.Step(Dt, Pinned(Good, FVector(1500.f, 0.f, 0.f), 1600.f), Row);
			TestTrue(FString::Printf(TEXT("%d segments: back on the rod tip after bad inputs"), Segments), Sim.GetStart().Equals(Good, 0.01));
		}
		TestEqual(FString::Printf(TEXT("every point stays finite over %d steps of extreme inputs"), Steps), Broken, 0);

		// The component's setters ignore bad values.
		ULureFishingLineComponent* Line = NewObject<ULureFishingLineComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Line->SetTension(NaN);
		TestEqual(TEXT("NaN tension = 0"), Line->GetTension(), 0.f);
		Line->SetTension(9.f);
		TestEqual(TEXT("tension clamps to 1"), Line->GetTension(), 1.f);
		Line->AttachEndActor(nullptr, 100.f);
		TestTrue(TEXT("attaching nothing does nothing"), Line->GetMode() == ELureLineMode::None && Line->GetEndActor() == nullptr);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineSubstepTest, "Project.Fishing.Line.Sim.SubstepsAndTeleports", Flags)
	bool FLureLineSubstepTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		FLureLineSim Sim;
		Sim.Init(12);
		const FLureLineSimInput In = Pinned(FVector(0.f, 0.f, 270.f), FVector(1500.f, 0.f, 0.f), 1600.f);
		Sim.Step(Dt, In, Row);
		TestEqual(TEXT("60 fps at 120 Hz: 2 sub-steps"), Sim.GetLastSubsteps(), 2);
		Sim.Step(1.f / 144.f, In, Row);
		TestEqual(TEXT("144 fps: 1 sub-step"), Sim.GetLastSubsteps(), 1);
		Sim.Step(1.f, In, Row);
		TestEqual(TEXT("a 1 s hitch runs MaxSubsteps (slow motion, no explosion)"), Sim.GetLastSubsteps(), Row.MaxSubsteps);
		TestTrue(TEXT("... and stays finite"), Sim.IsFinite());

		Run(Sim, In, Row, 2.f);
		const FVector NewTip(20000.f, 5000.f, 270.f);
		const FVector NewEnd(21500.f, 5000.f, 0.f);
		Sim.Step(Dt, Pinned(NewTip, NewEnd, 1600.f), Row);
		TestTrue(TEXT("a teleport moves the whole line (no whip across the map)"), Sim.GetStart().Equals(NewTip, 0.01) && Sim.GetEnd().Equals(NewEnd, 0.01));
		bool bNear = true;
		for (const FVector& Point : Sim.GetPoints())
		{
			bNear &= FMath::PointDistToSegment(Point, NewTip, NewEnd) < 1.0;
		}
		TestTrue(TEXT("... laid out between the new ends"), bNear);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineRecoilSimTest, "Project.Fishing.Line.Sim.RecoilAndSwing", Flags)
	bool FLureLineRecoilSimTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Fish(1500.f, 0.f, 0.f);

		// Snap: the free end flies back toward the rod.
		FLureLineSim Snap;
		Snap.Init(12);
		Run(Snap, Pinned(Tip, Fish, static_cast<float>(FVector::Dist(Tip, Fish))), Row, 1.f);
		const double Before = FVector::Dist(Snap.GetEnd(), Tip);
		Snap.ReleaseEnd(1.f);
		Snap.AddRecoil(Row.RecoilSpeed);
		FLureLineSimInput Free = Pinned(Tip, Fish, static_cast<float>(FVector::Dist(Tip, Fish)));
		Free.bFreeEnd = true;
		Run(Snap, Free, Row, 0.1f);
		const double After = FVector::Dist(Snap.GetEnd(), Tip);
		TestTrue(FString::Printf(TEXT("the snapped end whips back toward the rod (%.0f -> %.0f cm)"), Before, After), After < Before - 100.0);

		// Swing: a heavy end on 100 cm settles under the tip, lags when the rod moves, swings past and settles again.
		const FVector Rod(0.f, 0.f, 500.f);
		FLureLineSim Swing;
		Swing.Init(12);
		Run(Swing, Hanging(Rod, 100.f, Row.HangEndMass, Row.HangDrag), Row, 6.f);
		TestTrue(FString::Printf(TEXT("the hanging end settles 100 cm under the tip (%s)"), *Swing.GetEnd().ToString()), Swing.GetEnd().Equals(Rod - FVector(0.f, 0.f, 100.f), 3.0));
		TestTrue(TEXT("its direction points up the line"), Swing.GetEndDirection().Equals(FVector::UpVector, 0.05));
		double MaxX = -1.0;
		bool bLagged = false;
		for (int32 Frame = 0; Frame < 900; ++Frame)
		{
			const double Moved = FMath::Min(1.0, Frame / 12.0) * 50.0; // the rod moves 50 cm sideways in 0.2 s
			Swing.Step(Dt, Hanging(Rod + FVector(Moved, 0.0, 0.0), 100.f, Row.HangEndMass, Row.HangDrag), Row);
			if (Frame == 12)
			{
				bLagged = Swing.GetEnd().X < 40.0;
			}
			MaxX = FMath::Max(MaxX, Swing.GetEnd().X);
		}
		TestTrue(TEXT("it lags behind when the rod moves"), bLagged);
		TestTrue(FString::Printf(TEXT("it swings past (max x %.1f)"), MaxX), MaxX > 55.0);
		TestTrue(FString::Printf(TEXT("and settles under the tip again (x %.1f)"), Swing.GetEnd().X), FMath::Abs(Swing.GetEnd().X - 50.0) < 5.0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineAllocTest, "Project.Fishing.Line.Sim.AllocationFreeSteadyState", Flags)
	bool FLureLineAllocTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		FLureLineSim Sim;
		Sim.Init(12);
		const FVector Tip(0.f, 0.f, 270.f);
		Sim.Step(Dt, Pinned(Tip, FVector(1500.f, 0.f, 0.f), 1600.f), Row);
		const FVector* Data = Sim.GetPoints().GetData();
		int32 Count = 0;
		const bool bCounted = CountAllocations([&Sim, &Row, &Tip]()
		{
			for (int32 Frame = 0; Frame < 600; ++Frame)
			{
				const double T = Frame * Dt;
				FLureLineSimInput In = Pinned(Tip, FVector(1500.0 + 200.0 * FMath::Sin(T), 100.0 * FMath::Cos(T), 0.0), 1500.f + static_cast<float>(Frame % 200));
				In.bHasWater = (Frame % 2) == 0;
				In.Float = 0.5f;
				Sim.Step((Frame % 3) == 0 ? 1.f / 30.f : 1.f / 144.f, In, Row);
			}
			Sim.ReleaseEnd(1.f);
			Sim.AddRecoil(Row.RecoilSpeed);
			for (int32 Frame = 0; Frame < 60; ++Frame)
			{
				FLureLineSimInput In = Pinned(Tip, Tip, 800.f);
				In.bFreeEnd = true;
				Sim.Step(Dt, In, Row);
				Sim.AddEndVelocity(FVector(10.f, 0.f, 0.f));
			}
			volatile double Sink = Sim.GetPolylineLength() + Sim.GetEndDirection().X + Sim.GetEndVelocity().X;
			(void)Sink;
		}, Count);
		if (bCounted)
		{
			TestEqual(TEXT("660 simulated frames allocate nothing"), Count, 0);
		}
		else
		{
			AddInfo(TEXT("The game-thread allocation hook is not active in this build: only the buffer check runs."));
		}
		TestTrue(TEXT("the point buffer never moved (no reallocation)"), Sim.GetPoints().GetData() == Data);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineCostTest, "Project.Fishing.Line.Sim.Cost", Flags)
	bool FLureLineCostTest::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = Tuning();
		const FVector Tip(0.f, 0.f, 270.f);
		double TwelveMicros = 0.0;
		for (const int32 Segments : { 12, 24, 64 })
		{
			FLureLineSim Sim;
			Sim.Init(Segments);
			Run(Sim, Pinned(Tip, FVector(1500.f, 0.f, 0.f), 1600.f), Row, 1.f);
			constexpr int32 Frames = 3000;
			const double Start = FPlatformTime::Seconds();
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				const double T = Frame * Dt;
				FLureLineSimInput In = Pinned(Tip, FVector(1500.0 + 200.0 * FMath::Sin(T), 0.0, 0.0), 1575.f);
				In.bHasWater = true;
				In.Float = 0.5f;
				Sim.Step(Dt, In, Row);
			}
			const double Micros = (FPlatformTime::Seconds() - Start) * 1.0e6 / Frames;
			AddInfo(FString::Printf(TEXT("line simulation, %d segments: %.2f us per 60 fps frame (%d sub-steps x %d passes)"), Segments, Micros, Sim.GetLastSubsteps(), Row.Iterations));
			if (Segments == 12)
			{
				TwelveMicros = Micros;
			}
		}
		TestTrue(FString::Printf(TEXT("the shipped line (12 segments) simulates in < 200 us per frame (%.2f us)"), TwelveMicros), TwelveMicros < 200.0);
		return true;
	}

	// =================================================================================================================
	// The component (test worlds)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineComponentApiTest, "Project.Fishing.Line.Component.EndpointApi", Flags)
	bool FLureLineComponentApiTest::RunTest(const FString& Parameters)
	{
		FLineWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingLineComponent* Line = World.Line;
		TestFalse(TEXT("no line before it is set"), Line->IsLineVisible());
		TestFalse(TEXT("no tick before a line is out"), Line->IsComponentTickEnabled());
		TestEqual(TEXT("fixed segment count from Setup"), Line->GetNumSegments(), 12);

		const FVector Tip(0.f, 0.f, 270.f);
		const FVector Bobber(1200.f, 300.f, 0.f);
		const FVector Eye(-50.f, 0.f, 300.f);
		Line->SetViewer(Eye, 90.f);
		Line->SetWidthRule(2.5f, 1920.f, 0.15f);
		Line->SetWaterSurfaceZ(0.f);
		Line->SetTension(0.f);
		Line->SetEndpoints(Tip, Bobber);
		TestTrue(TEXT("SetEndpoints starts the line (points right away)"), Line->IsLineVisible() && Line->GetPoints().Num() == 13);
		TestTrue(TEXT("... and its tick"), Line->IsComponentTickEnabled());
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Line->SetEndpoints(Tip, Bobber);
			World.Tick(1);
		}
		TestTrue(TEXT("the line starts at the rod tip"), Line->GetStartPoint().Equals(Tip, 0.01) && Line->GetPoints()[0].Equals(Tip, 0.01));
		TestTrue(TEXT("GetEndPoint is the pinned end"), Line->GetEndPoint().Equals(Bobber, 0.01) && Line->GetPoints().Last().Equals(Bobber, 0.01));
		TestEqual(TEXT("mode Pinned"), static_cast<int32>(Line->GetMode()), static_cast<int32>(ELureLineMode::Pinned));
		bool bWide = true;
		for (int32 Index = 0; Index < Line->GetPoints().Num(); ++Index)
		{
			bWide &= FLureFishingRules::LinePixelsAtDistance(Line->GetWidths()[Index], static_cast<float>(FVector::Dist(Eye, Line->GetPoints()[Index])), 90.f, 1920.f) >= 2.f - 0.01f;
		}
		TestTrue(TEXT("every point is >= 2 px wide from the viewer (B-S3)"), bWide);

		// A start provider (the drawn rod tip) wins over SetEndpoints' tip.
		const FVector DrawnTip(10.f, -20.f, 280.f);
		Line->SetStartProvider(FLureLinePointProvider::CreateLambda([DrawnTip]() { return DrawnTip; }));
		Line->SetEndpoints(Tip, Bobber);
		World.Tick(1);
		TestTrue(TEXT("the line starts at the provider's tip"), Line->GetPoints()[0].Equals(DrawnTip, 0.01));
		Line->SetStartProvider(FLureLinePointProvider());

		// Hide: gone, and no simulation while no line is out.
		Line->Hide();
		TestFalse(TEXT("Hide hides the line"), Line->IsLineVisible());
		TestFalse(TEXT("... and stops its tick (no simulation when the line isn't out)"), Line->IsComponentTickEnabled());
		const FVector EndBefore = Line->GetPoints().Last();
		World.Tick(10);
		TestTrue(TEXT("... nothing moves while hidden"), Line->GetPoints().Last().Equals(EndBefore, 0.0));
		Line->SetEndpoints(Tip + FVector(0.f, 0.f, 10.f), Bobber);
		TestTrue(TEXT("a new line starts from scratch at the new tip"), Line->IsLineVisible() && Line->GetPoints()[0].Equals(Tip + FVector(0.f, 0.f, 10.f), 0.01));
		Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineComponentHangTest, "Project.Fishing.Line.Component.HangingActorSwings", Flags)
	bool FLureLineComponentHangTest::RunTest(const FString& Parameters)
	{
		FLineWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingLineComponent* Line = World.Line;
		const FVector Tip(0.f, 0.f, 400.f);
		AActor* FishActor = World.SpawnMovable(FVector(150.f, 0.f, 350.f));
		const FVector Hook(30.f, 0.f, 0.f); // the mouth, 30 cm in front of the pivot
		Line->SetEndpoints(Tip, FVector(1500.f, 0.f, 0.f));
		World.Tick(2);
		Line->AttachEndActor(FishActor, 100.f, Hook);
		TestEqual(TEXT("mode Hanging"), static_cast<int32>(Line->GetMode()), static_cast<int32>(ELureLineMode::Hanging));
		TestTrue(TEXT("GetEndActor"), Line->GetEndActor() == FishActor);
		Line->Hide(); // the fishing state went idle: the hanging line stays
		for (int32 Frame = 0; Frame < 360; ++Frame)
		{
			World.Tick(1);
		}
		TestTrue(TEXT("the hanging line stays after Hide"), Line->IsLineVisible() && Line->GetMode() == ELureLineMode::Hanging);
		const FVector End = Line->GetEndPoint();
		TestTrue(FString::Printf(TEXT("the actor hangs 100 cm under the rod tip (end %s)"), *End.ToString()), End.Equals(Tip - FVector(0.f, 0.f, 100.f), 4.0));
		const FVector Mouth = FishActor->GetActorLocation() + FishActor->GetActorQuat().RotateVector(Hook);
		TestTrue(TEXT("its hook point sits on the line's end"), Mouth.Equals(End, 0.1));
		TestTrue(TEXT("its +X (head) points up the line"), FishActor->GetActorForwardVector().Equals(FVector::UpVector, 0.1));

		// The rod moves: the fish lags, swings past, and GetEndPoint follows it.
		const FVector Moved = Tip + FVector(0.f, 60.f, 0.f);
		double MaxY = -1.0;
		bool bLagged = false;
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			Line->SetEndpoints(Frame < 6 ? FMath::Lerp(Tip, Moved, Frame / 6.f) : Moved, FVector(1500.f, 0.f, 0.f));
			World.Tick(1);
			if (Frame == 6)
			{
				bLagged = Line->GetEndPoint().Y < 45.0;
			}
			MaxY = FMath::Max(MaxY, Line->GetEndPoint().Y);
		}
		TestTrue(TEXT("the fish lags when the rod moves"), bLagged);
		TestTrue(FString::Printf(TEXT("and swings past (max y %.1f)"), MaxY), MaxY > 65.0);
		Line->AddEndVelocity(FVector(300.f, 0.f, 0.f));
		World.Tick(3);
		TestTrue(TEXT("AddEndVelocity kicks it"), Line->GetEndPoint().X > 5.0);

		// Detach while the line is still out (SetEndpoints was called above): back to the pinned end.
		Line->DetachEndActor();
		TestEqual(TEXT("detached with the line out: Pinned again"), static_cast<int32>(Line->GetMode()), static_cast<int32>(ELureLineMode::Pinned));
		TestTrue(TEXT("the actor is let go"), Line->GetEndActor() == nullptr);
		Line->Hide();
		TestFalse(TEXT("then Hide: gone"), Line->IsLineVisible());

		// Attach with no line out: the line starts, reeling the actor up to HangLength; destroying the actor ends it.
		AActor* Second = World.SpawnMovable(FVector(0.f, 0.f, 100.f));
		Line->AttachEndActor(Second, 120.f);
		TestTrue(TEXT("attach starts a line"), Line->IsLineVisible() && Line->GetMode() == ELureLineMode::Hanging);
		World.Tick(240);
		TestNearlyEqual(TEXT("reeled up to its hang length"), Line->GetRestLength(), 120.f, 1.f);
		Second->Destroy();
		World.Tick(2);
		TestFalse(TEXT("its actor destroyed and no line out: the line is gone"), Line->IsLineVisible());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineComponentSnapTest, "Project.Fishing.Line.Component.SnapRecoil", Flags)
	bool FLureLineComponentSnapTest::RunTest(const FString& Parameters)
	{
		FLineWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFishingLineComponent* Line = World.Line;
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Fish(1500.f, 0.f, -100.f);
		Line->Snap();
		TestFalse(TEXT("no line: Snap does nothing"), Line->IsRecoiling());
		Line->SetWaterSurfaceZ(0.f);
		Line->SetTension(1.f);
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Line->SetEndpoints(Tip, Fish);
			World.Tick(1);
		}
		const double Before = FVector::Dist(Line->GetEndPoint(), Tip);
		Line->Snap();
		Line->Hide(); // the fishing state goes idle the same frame
		TestTrue(TEXT("Snap starts the recoil"), Line->IsRecoiling());
		TestTrue(TEXT("Hide lets the recoil play"), Line->IsLineVisible());
		World.Tick(6);
		const double After = FVector::Dist(Line->GetEndPoint(), Tip);
		TestTrue(FString::Printf(TEXT("the loose end whips back toward the rod (%.0f -> %.0f cm in 0.1 s)"), Before, After), After < Before - 100.0);
		TestTrue(TEXT("the line shortens as it recoils"), Line->GetRestLength() < Line->GetTargetRestLength() / Line->GetTuning().RecoilLengthShare);
		World.Tick(FMath::CeilToInt32(Line->GetTuning().RecoilTime / Dt) + 2);
		TestFalse(TEXT("after RecoilTime the line is gone"), Line->IsLineVisible() || Line->IsRecoiling());
		TestFalse(TEXT("... and stops ticking"), Line->IsComponentTickEnabled());

		// A new cast during a recoil starts a fresh line.
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Line->SetEndpoints(Tip, Fish);
			World.Tick(1);
		}
		Line->Snap();
		World.Tick(3);
		Line->SetEndpoints(Tip, Fish);
		TestTrue(TEXT("SetEndpoints during a recoil starts a new pinned line"), !Line->IsRecoiling() && Line->GetMode() == ELureLineMode::Pinned
			&& Line->GetPoints().Last().Equals(Fish, 0.01));
		Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineComponentBudgetTest, "Project.Fishing.Line.Component.AllocationsAndCost", Flags)
	bool FLureLineComponentBudgetTest::RunTest(const FString& Parameters)
	{
		const FVector Tip(0.f, 0.f, 270.f);
		auto Feed = [&Tip](ULureFishingLineComponent* Line, int32 Frame)
		{
			const double T = Frame * Dt;
			Line->SetTension(0.5f + 0.5f * static_cast<float>(FMath::Sin(T)));
			Line->SetEndpoints(Tip, FVector(1500.0 + 200.0 * FMath::Sin(T), 50.0 * FMath::Cos(T), 0.0));
			Line->UpdateLine(Dt);
		};

		// Undrawn line (no mesh): simulation + widths must not allocate.
		{
			FLineWorld World;
			if (!World.Create(*this, /*bMesh*/ false))
			{
				return false;
			}
			ULureFishingLineComponent* Line = World.Line;
			Line->SetViewer(FVector(-50.f, 0.f, 300.f), 90.f);
			Line->SetWaterSurfaceZ(0.f);
			Feed(Line, 0);
			int32 Count = 0;
			const bool bCounted = CountAllocations([&Feed, Line]()
			{
				for (int32 Frame = 1; Frame <= 300; ++Frame)
				{
					Feed(Line, Frame);
				}
			}, Count);
			if (bCounted)
			{
				TestEqual(TEXT("the line's own per-frame work (inputs, simulation, widths) allocates nothing"), Count, 0);
			}
			else
			{
				AddInfo(TEXT("The game-thread allocation hook is not active in this build."));
			}
			constexpr int32 Frames = 1000;
			const double Start = FPlatformTime::Seconds();
			for (int32 Frame = 1; Frame <= Frames; ++Frame)
			{
				Feed(Line, 300 + Frame);
			}
			const double Micros = (FPlatformTime::Seconds() - Start) * 1.0e6 / Frames;
			AddInfo(FString::Printf(TEXT("undrawn line, 12 segments: %.2f us per frame (inputs + simulation + widths)"), Micros));
			TestTrue(FString::Printf(TEXT("the line's own work costs < 200 us per frame (%.2f us)"), Micros), Micros < 200.0);
			Line->Hide();
		}

		// Drawn line (12 spline-mesh segments, as shipped): cost per frame, and what the engine's spline updates allocate.
		{
			FLineWorld World;
			if (!World.Create(*this, /*bMesh*/ true))
			{
				return false;
			}
			ULureFishingLineComponent* Line = World.Line;
			Line->SetViewer(FVector(-50.f, 0.f, 300.f), 90.f);
			Line->SetWaterSurfaceZ(0.f);
			Feed(Line, 0);
			constexpr int32 Frames = 600;
			const double Start = FPlatformTime::Seconds();
			for (int32 Frame = 1; Frame <= Frames; ++Frame)
			{
				Feed(Line, Frame);
			}
			const double Micros = (FPlatformTime::Seconds() - Start) * 1.0e6 / Frames;
			int32 EngineCount = 0;
			const bool bCounted = CountAllocations([&Feed, Line]() { Feed(Line, Frames + 1); }, EngineCount);
			AddInfo(FString::Printf(TEXT("drawn line, 12 segments: %.1f us per frame on the game thread (simulation + 12 spline-mesh updates)%s"),
				Micros, bCounted ? *FString::Printf(TEXT("; the engine's spline updates allocate %d times per frame (render commands)"), EngineCount) : TEXT("")));
			TestTrue(FString::Printf(TEXT("a drawn line costs < 2 ms per frame (%.1f us)"), Micros), Micros < 2000.0);
			Line->Hide();
		}
		return true;
	}

	// =================================================================================================================
	// In the game: cast, wait, fight, snap
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureLineFishingTest, "Project.Fishing.Line.Fishing.CastWaitFightSnap", Flags)
	bool FLureLineFishingTest::RunTest(const FString& Parameters)
	{
		QAFishing::FScene Scene;
		FWeakLineTables Fight;
		if (!Scene.Create(*this) || !Fight.Load(*this, 0.5f))
		{
			return false;
		}
		Scene.AddTaggedWater(0.f);
		ALurePlayerCharacter* Character = Scene.Spawn(*this);
		APlayerController* Controller = Scene.PossessLocally(Character);
		FLureFishingRow Shipped;
		if (!Character || !Controller || !QAFishing::ShippedFishingRow(*this, Shipped))
		{
			return false;
		}
		FLureFishingRow Profile = QAFishing::FlowProfile(Shipped, /*BiteWait*/ 60.f);
		Profile.MinCastDistance = Profile.MaxCastDistance = 1200.f;
		ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, Profile);
		if (!Fishing)
		{
			return false;
		}
		Fishing->SetFightTables(Fight.Gear.Get(), Fight.Patterns.Get(), Fight.Fight.Get());
		Scene.Tick(5);
		if (!TestTrue(TEXT("cast"), Fishing->AuthorityCast(1.f, 0.f))
			|| !TestTrue(TEXT("the bobber lands on the water"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 300)))
		{
			return false;
		}
		Scene.Tick(240);
		ULureFishingLineComponent* Line = Fishing->GetLine();
		if (!TestNotNull(TEXT("the fishing component has a line"), Line))
		{
			return false;
		}
		const FLureFishingLineRow& Row = Line->GetTuning();
		TestTrue(TEXT("the line is out while waiting"), Line->IsLineVisible() && Line->GetMode() == ELureLineMode::Pinned);
		TestEqual(TEXT("waiting shows WaitTension"), Line->GetTension(), Row.WaitTension);
		TestTrue(TEXT("it starts at the rod tip as drawn this frame"), Line->GetPoints()[0].Equals(Fishing->GetLineStart(), 0.5));
		const FName Socket = GetDefault<ULureFishingSettings>()->BobberLineSocket;
		const UStaticMeshComponent* Bobber = Fishing->GetBobberMesh();
		const FVector BobberEnd = (Bobber && Bobber->DoesSocketExist(Socket)) ? Bobber->GetSocketLocation(Socket) : Fishing->GetBobberLocation();
		TestTrue(TEXT("it ends at the bobber"), Line->GetPoints().Last().Equals(BobberEnd, Bobber && Bobber->DoesSocketExist(Socket) ? 0.5 : 60.0));
		int32 Floating = 0;
		for (int32 Index = 1; Index < Line->GetPoints().Num() - 1; ++Index)
		{
			Floating += FMath::Abs(Line->GetPoints()[Index].Z - Row.FloatHeight) <= 1.5 ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("the slack line lies on the water (%d points at the surface)"), Floating), Floating >= 2);
		const double SlackDeviation = MaxDeviation(Line->GetPoints());

		// A fish on a weak line: the line shows the fight's tension, then snaps and whips back.
		FFishInstance Fish;
		if (!RollBonefish(*this, Scene.Fish, Fish) || !TestTrue(TEXT("hook the fish"), Fishing->AuthorityHookFish(Fish)))
		{
			return false;
		}
		Fishing->AuthoritySetReeling(true);
		bool bTensionShown = true;
		bool bSnapped = false;
		double TautDeviation = SlackDeviation;
		for (int32 Frame = 0; Frame < 600 && !bSnapped; ++Frame)
		{
			Scene.Tick(1);
			const FLureFightNetState& Net = Fishing->GetFightNet();
			if (Net.bActive)
			{
				bTensionShown &= FMath::IsNearlyEqual(Line->GetTension(), FLureFight::LineTension(Net.GetTension01(), Fishing->GetFightTuning()), 1.0e-4f); // T-032b
				if (Net.GetTension01() >= 1.f)
				{
					TautDeviation = FMath::Min(TautDeviation, MaxDeviation(Line->GetPoints()));
				}
			}
			bSnapped = Fishing->GetNetState().LastResult == ELureFishingResult::Snapped;
		}
		TestTrue(TEXT("during the fight the line shows the fight's tension"), bTensionShown);
		if (!TestTrue(TEXT("a 0.5-strength line snaps (if the fight rules changed, update this fixture)"), bSnapped))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("over the line's strength it pulls straighter than slack (%.1f -> %.1f cm off straight)"), SlackDeviation, TautDeviation), TautDeviation < SlackDeviation);
		TestTrue(TEXT("the snapped line recoils (it does not just vanish)"), Line->IsRecoiling() && Line->IsLineVisible());
		Scene.Tick(FMath::CeilToInt32(Row.RecoilTime / QAFishing::Dt) + 2);
		TestFalse(TEXT("then it is gone"), Line->IsLineVisible());
		TestFalse(TEXT("... and no longer simulated"), Line->IsComponentTickEnabled());
		Controller->UnPossess();
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

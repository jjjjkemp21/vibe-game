// Lure T-032 QA (qa-engineer): independent tests of the physics fishing line. Project.Fishing.Line.QA.*
// Black-box: expectations come from docs/specs/fishing-line.md, the T-032 acceptance line in docs/TASKS.md and the contract
// comments in Fishing/FishingLineTypes.h, FishingLineSim.h and LureFishingLineComponent.h, never from the .cpp files.
// Tuning is the built-in row (= data/tables/DT_FishingLine.csv, checked in Data.*). Tables come from the text sources.
// Everything lives in namespace LureQALineTest (unity builds: no file-scope using-directives).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Fishing/FishFightTypes.h"
#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/RepLayout.h"
#include "Templates/Function.h"
#include "Tests/AutomationCommon.h"
#include "Tests/Fishing/QAFishingTestUtils.h"
#include "UObject/CoreNet.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include <cmath>
#include <limits>

#if MALLOC_GT_HOOKS
// Core's game-thread allocation hook (UnrealMemory.cpp; STATS builds): called with 0 = malloc, 1 = realloc, 2 = free.
extern CORE_API TFunction<void(int32)>* GGameThreadMallocHook;
#endif

namespace LureQALineTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = 1.f / 60.f;
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();

	FLureFishingLineRow Row()
	{
		return FLureFishingLineRules::GetFallbackRow();
	}

	int32 Frames(float Seconds)
	{
		return FMath::CeilToInt32(Seconds / Dt);
	}

	/** Largest distance of any point from the straight segment between the first and the last point, cm. */
	double Deviation(const TArray<FVector>& Points)
	{
		double Max = 0.0;
		for (const FVector& Point : Points)
		{
			Max = FMath::Max(Max, static_cast<double>(FMath::PointDistToSegment(Point, Points[0], Points.Last())));
		}
		return Max;
	}

	double PolylineLength(const TArray<FVector>& Points)
	{
		double Length = 0.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += FVector::Dist(Points[Index - 1], Points[Index]);
		}
		return Length;
	}

	bool AllFinite(const TArray<FVector>& Points)
	{
		for (const FVector& Point : Points)
		{
			if (Point.ContainsNaN())
			{
				return false;
			}
		}
		return true;
	}

	FLureLineSimInput PinnedInput(const FVector& Start, const FVector& End, float RestLength)
	{
		FLureLineSimInput In;
		In.Start = Start;
		In.End = End;
		In.RestLength = RestLength;
		return In;
	}

	/** Counts game-thread allocations (malloc, realloc, free) while Body runs. False if the hook is not active in this build. */
	bool CountGameThreadAllocs(TFunctionRef<void()> Body, int32& OutCount)
	{
		OutCount = 0;
#if MALLOC_GT_HOOKS
		int32 Count = 0;
		TFunction<void(int32)> Hook([&Count](int32) { ++Count; });
		{
			TGuardValue<TFunction<void(int32)>*> Guard(GGameThreadMallocHook, &Hook);
			void* Probe = FMemory::Malloc(64);
			FMemory::Free(Probe);
		}
		if (Count == 0)
		{
			Body();
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

	/** A transient game world with line components on plain actors. */
	struct FQALineWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		ULureFishingLineComponent* Line = nullptr;

		bool Create(FAutomationTestBase& Test, bool bMesh = false, int32 Segments = 12)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			Line = AddLine(Test, bMesh, Segments, &Owner);
			return Line != nullptr;
		}

		ULureFishingLineComponent* AddLine(FAutomationTestBase& Test, bool bMesh, int32 Segments, AActor** OutOwner = nullptr)
		{
			AActor* LineOwner = SpawnMovable(FVector::ZeroVector);
			if (!LineOwner)
			{
				Test.AddError(TEXT("QA: could not spawn a line owner"));
				return nullptr;
			}
			ULureFishingLineComponent* NewLine = NewObject<ULureFishingLineComponent>(LineOwner, NAME_None, RF_Transient);
			NewLine->SetupAttachment(LineOwner->GetRootComponent());
			NewLine->RegisterComponent();
			UStaticMesh* Cylinder = bMesh ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")) : nullptr;
			if (bMesh && !Cylinder)
			{
				Test.AddError(TEXT("QA: the engine cylinder mesh does not load"));
				return nullptr;
			}
			NewLine->Setup(Cylinder, nullptr, FLinearColor::White, Segments);
			NewLine->SetTuning(Row());
			if (OutOwner)
			{
				*OutOwner = LineOwner;
			}
			return NewLine;
		}

		AActor* SpawnMovable(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation, Location), Params);
			if (Actor)
			{
				USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None);
				Actor->SetRootComponent(Root);
				Root->RegisterComponent();
				Root->SetWorldLocationAndRotation(Location, Rotation);
			}
			return Actor;
		}

		/** A non-colliding box tagged as water (settings WaterTag), its top at SurfaceZ, centred on CenterXY. */
		AActor* AddWater(const FVector2D& CenterXY, float SurfaceZ, float HalfSize)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
			Box->SetBoxExtent(FVector(HalfSize, HalfSize, 25.f), false);
			Box->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Box->SetRelativeLocation_Direct(FVector(CenterXY.X, CenterXY.Y, SurfaceZ - 25.f));
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Actor->Tags.Add(GetDefault<ULureFishingSettings>()->WaterTag);
			return Actor;
		}

		void Tick(int32 Count)
		{
			for (int32 Frame = 0; Frame < Count; ++Frame)
			{
				Wrapper.TickTestWorld(Dt);
			}
		}
	};

	/** Feeds the same ends every frame and simulates (UpdateLine, no world tick) for Seconds. */
	void Hold(ULureFishingLineComponent* Line, const FVector& Tip, const FVector& End, float Seconds)
	{
		for (int32 Frame = 0; Frame < Frames(Seconds); ++Frame)
		{
			Line->SetEndpoints(Tip, End);
			Line->UpdateLine(Dt);
		}
	}

	/** Points strictly between the ends that lie within Band cm of Z, and whether all of them are within Tol of Z. */
	int32 PointsNear(const TArray<FVector>& Points, double Z, double Band)
	{
		int32 Count = 0;
		for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
		{
			Count += FMath::Abs(Points[Index].Z - Z) <= Band ? 1 : 0;
		}
		return Count;
	}

	double LowestInner(const TArray<FVector>& Points)
	{
		double Lowest = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
		{
			Lowest = FMath::Min(Lowest, Points[Index].Z);
		}
		return Lowest;
	}

	int32 LiveSegmentComponents(const AActor* Owner)
	{
		int32 Count = 0;
		for (TObjectIterator<ULureLineSegmentComponent> It; It; ++It)
		{
			if (IsValid(*It) && !It->HasAnyFlags(RF_ClassDefaultObject) && It->GetTypedOuter<AActor>() == Owner)
			{
				++Count;
			}
		}
		return Count;
	}

	bool LoadLineCsv(FAutomationTestBase& Test, FString& OutCsv)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables/DT_FishingLine.csv"));
		return Test.TestTrue(TEXT("data/tables/DT_FishingLine.csv loads"), FFileHelper::LoadFileToString(OutCsv, *Path));
	}

	// ---- Replication (the engine's property wire format, like the net driver) ----

	bool WireCopy(FAutomationTestBase& Test, const FStructProperty* Property, UObject* Source, UObject* Target)
	{
		const TSharedPtr<FRepLayout> Layout = FRepLayout::CreateFromStruct(Property->Struct, nullptr, ECreateRepLayoutFlags::None);
		if (!Layout.IsValid())
		{
			Test.AddError(TEXT("QA: no FRepLayout for ") + Property->Struct->GetName());
			return false;
		}
		FNetBitWriter Writer(nullptr, 64 * 1024 * 8);
		bool bUnmapped = false;
		Layout->SerializePropertiesForStruct(Property->Struct, Writer, nullptr, Property->ContainerPtrToValuePtr<void>(Source), bUnmapped);
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		Layout->SerializePropertiesForStruct(Property->Struct, Reader, nullptr, Property->ContainerPtrToValuePtr<void>(Target), bUnmapped);
		return !Writer.IsError() && !Reader.IsError();
	}

	/** Copies every replicated property of the server's fishing component to the proxy's, then calls OnRep_NetState like the net driver. */
	void ReplicateFishing(FAutomationTestBase& Test, ULureFishingComponent* Server, ULureFishingComponent* Proxy)
	{
		const FLureFishingNetState Previous = Proxy->GetNetState();
		for (TFieldIterator<FProperty> It(ULureFishingComponent::StaticClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Net) && It->GetOwnerClass() == ULureFishingComponent::StaticClass())
			{
				if (const FStructProperty* Struct = CastField<FStructProperty>(*It))
				{
					WireCopy(Test, Struct, Server, Proxy);
				}
				else
				{
					Test.AddError(TEXT("QA: unexpected non-struct replicated property ") + It->GetName());
				}
			}
		}
		if (UFunction* OnRep = Proxy->FindFunction(TEXT("OnRep_NetState")))
		{
			struct
			{
				FLureFishingNetState PreviousState;
			} Params{ Previous };
			Proxy->ProcessEvent(OnRep, &Params);
		}
	}

	/** A server player and another player's copy of it (ROLE_SimulatedProxy) standing where the server's pawn stands. */
	struct FProxyRig
	{
		QAFishing::FScene Scene;
		ALurePlayerCharacter* ServerCharacter = nullptr;
		ALurePlayerCharacter* ProxyCharacter = nullptr;
		ULureFishingComponent* Server = nullptr;
		ULureFishingComponent* Proxy = nullptr;
		TStrongObjectPtr<UDataTable> Gear;
		TStrongObjectPtr<UDataTable> Patterns;
		TStrongObjectPtr<UDataTable> Fight;

		/** BiteWait > 0: a flow profile whose bite comes BiteWait s after landing (cast with CastStraightOut), else the stage profile. */
		bool Make(FAutomationTestBase& Test, float WeakLineStrength = -1.f, float BiteWait = -1.f)
		{
			FLureFishingRow Shipped;
			if (!QAFishing::ShippedFishingRow(Test, Shipped) || !Scene.Create(Test))
			{
				return false;
			}
			Scene.AddShoreSpot();
			FLureFishingRow Profile = QAFishing::StageProfile(Shipped);
			if (BiteWait > 0.f)
			{
				Profile = QAFishing::FlowProfile(Shipped, BiteWait);
				Profile.MinCastDistance = Profile.MaxCastDistance = 1200.f;
			}
			ServerCharacter = Scene.Spawn(Test);
			ProxyCharacter = Scene.Spawn(Test, QAFishing::StandFeet + FVector(0.f, 150.f, 0.f));
			Server = Scene.SetUpFishing(Test, ServerCharacter, Profile);
			Proxy = Scene.SetUpFishing(Test, ProxyCharacter, Profile);
			if (!Server || !Proxy)
			{
				return false;
			}
			if (WeakLineStrength > 0.f)
			{
				FString GearCsv;
				FString PatternJson;
				FString FightCsv;
				if (!QAFishing::LoadSource(Test, TEXT("DT_Gear.csv"), GearCsv) || !QAFishing::LoadSource(Test, TEXT("DT_FightPattern.json"), PatternJson)
					|| !QAFishing::LoadSource(Test, TEXT("DT_FishFight.csv"), FightCsv))
				{
					return false;
				}
				const FString Weak = QAFishing::WithCell(Test, GearCsv, TEXT("Line_Mono"), TEXT("LineStrength"), FString::SanitizeFloat(WeakLineStrength));
				Gear.Reset(QAFishing::MakeTableChecked(Test, FLureGearRow::StaticStruct(), Weak, TEXT("DT_Gear (weak line)")));
				Fight.Reset(QAFishing::MakeTableChecked(Test, FLureFishFightRow::StaticStruct(), FightCsv, TEXT("DT_FishFight")));
				Patterns.Reset(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
				Patterns->RowStruct = FLureFightPatternRow::StaticStruct();
				const TArray<FString> Problems = Patterns->CreateTableFromJSONString(PatternJson);
				if (!Test.TestEqual(TEXT("QA: DT_FightPattern imports"), Problems.Num(), 0) || !Gear.IsValid() || !Fight.IsValid())
				{
					return false;
				}
				Server->SetFightTables(Gear.Get(), Patterns.Get(), Fight.Get());
				Proxy->SetFightTables(Gear.Get(), Patterns.Get(), Fight.Get());
			}
			Scene.Tick(10);
			ProxyCharacter->SetRole(ROLE_SimulatedProxy);
			return true;
		}

		/** The replicated pawn stands where the server's pawn stands (as it would on another machine). */
		void AlignPawns()
		{
			ProxyCharacter->SetActorLocationAndRotation(ServerCharacter->GetActorLocation(), ServerCharacter->GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		}

		~FProxyRig()
		{
			if (Scene.World)
			{
				Scene.Tick(1);
			}
		}
	};

	/** Largest distance between matching points of two lines, cm (Inf if the counts differ). */
	double MaxPointGap(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		if (A.Num() != B.Num() || A.Num() == 0)
		{
			return std::numeric_limits<double>::infinity();
		}
		double Max = 0.0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			Max = FMath::Max(Max, FVector::Dist(A[Index], B[Index]));
		}
		return Max;
	}

	// =================================================================================================================
	// Sag vs tension
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineSagMonotone, "Project.Fishing.Line.QA.Sag.MonotoneAcrossTensionRange", Flags)
	bool FQALineSagMonotone::RunTest(const FString& Parameters)
	{
		// Through SetTension on the component, 21 levels up (the line shortens) and back down (slack appears): the more tension,
		// the straighter; the same tension gives the same shape whichever way you got there.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FVector Tip(0.f, 0.f, 1300.f);
		const FVector End(1500.f, 0.f, 1000.f); // 15 m out, 3 m below the tip; far above the fallback sea
		TArray<double> Up;
		TArray<double> Down;
		for (int32 Step = 0; Step <= 20; ++Step)
		{
			W.Line->SetTension(Step / 20.f);
			Hold(W.Line, Tip, End, 3.f);
			Up.Add(Deviation(W.Line->GetPoints()));
		}
		for (int32 Step = 20; Step >= 0; --Step)
		{
			W.Line->SetTension(Step / 20.f);
			Hold(W.Line, Tip, End, 3.f);
			Down.Insert(Deviation(W.Line->GetPoints()), 0);
		}
		FString Trace;
		bool bUpFalls = true;
		bool bDownRises = true;
		double WorstPath = 0.0;
		for (int32 Step = 0; Step <= 20; ++Step)
		{
			Trace += FString::Printf(TEXT(" %.2f:%.1f/%.1f"), Step / 20.f, Up[Step], Down[Step]);
			if (Step > 0)
			{
				bUpFalls &= Up[Step] <= Up[Step - 1] + 0.5;
				bDownRises &= Down[Step] <= Down[Step - 1] + 0.5;
			}
			WorstPath = FMath::Max(WorstPath, FMath::Abs(Up[Step] - Down[Step]) - 0.02 * FMath::Max(Up[Step], Down[Step]));
		}
		AddInfo(TEXT("tension: max cm off straight, raising / lowering:") + Trace);
		TestTrue(TEXT("raising tension never makes the line sag more"), bUpFalls);
		TestTrue(TEXT("lowering tension never makes the line sag less"), bDownRises);
		TestTrue(FString::Printf(TEXT("same tension, same shape up or down (2 cm + 2 %%; worst excess %.2f cm)"), WorstPath), WorstPath <= 2.0);
		TestTrue(FString::Printf(TEXT("no tension: a 15 m line clearly sags (%.1f cm)"), Up[0]), Up[0] > 100.0);
		TestTrue(FString::Printf(TEXT("half tension: still visibly slack (%.1f cm)"), Up[10]), Up[10] > 20.0);
		TestTrue(FString::Printf(TEXT("strictly straighter from 0 to 0.5 to 0.9 (%.1f > %.1f > %.1f)"), Up[0], Up[10], Up[18]), Up[0] > Up[10] + 10.0 && Up[10] > Up[18] + 10.0);
		TestTrue(FString::Printf(TEXT("the snap threshold: straight (%.3f cm)"), Up[20]), Up[20] < 0.1);
		W.Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineStraightensSmoothly, "Project.Fishing.Line.QA.Sag.StraightensOverTimeWithoutPop", Flags)
	bool FQALineStraightensSmoothly::RunTest(const FString& Parameters)
	{
		// Spec: longer at once, shorter smoothly (LengthResponse). A slack line pulled to the snap threshold straightens over a
		// few tenths of a second, frame by frame, never jumping straight in one frame and never sagging back.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FVector Tip(0.f, 0.f, 1300.f);
		const FVector End(1500.f, 0.f, 1000.f);
		W.Line->SetTension(0.f);
		Hold(W.Line, Tip, End, 4.f);
		const double Slack = Deviation(W.Line->GetPoints());
		const float SlackLength = W.Line->GetRestLength();
		W.Line->SetTension(1.f);
		Hold(W.Line, Tip, End, Dt);
		const double FirstFrame = Deviation(W.Line->GetPoints());
		TestTrue(FString::Printf(TEXT("no pop: one frame after full tension the line still sags (%.1f of %.1f cm)"), FirstFrame, Slack), FirstFrame > 0.5 * Slack);
		TestTrue(TEXT("... and its length shrinks, not jumps to the chord"), W.Line->GetRestLength() < SlackLength && W.Line->GetRestLength() > FVector::Dist(Tip, End) + 1.0);
		double Previous = FirstFrame;
		float PreviousLength = W.Line->GetRestLength();
		double WorstRise = 0.0;
		bool bLengthFalls = true;
		int32 StraightAt = -1;
		double AtHalfSecond = 0.0;
		FString Timeline;
		for (int32 Frame = 1; Frame < Frames(3.f); ++Frame)
		{
			Hold(W.Line, Tip, End, Dt);
			const double Now = Deviation(W.Line->GetPoints());
			if (Frame == 30)
			{
				AtHalfSecond = Now;
			}
			if (Frame % 15 == 0 && Frame <= 120)
			{
				Timeline += FString::Printf(TEXT(" %.2fs:%.1f"), Frame * Dt, Now);
			}
			WorstRise = FMath::Max(WorstRise, Now - Previous);
			bLengthFalls &= W.Line->GetRestLength() <= PreviousLength + 1.0e-3f;
			if (StraightAt < 0 && Now < 1.0)
			{
				StraightAt = Frame;
			}
			Previous = Now;
			PreviousLength = W.Line->GetRestLength();
		}
		AddInfo(FString::Printf(TEXT("slack %.1f cm; after full tension, cm off straight:%s; straight (< 1 cm) after %.2f s"), Slack, *Timeline, StraightAt * Dt));
		TestTrue(FString::Printf(TEXT("spec: it straightens over ~0.3-0.5 s: after 0.5 s at most a quarter of the slack sag is left (%.1f of %.1f cm)"), AtHalfSecond, Slack), AtHalfSecond <= 0.25 * Slack);
		TestTrue(FString::Printf(TEXT("the sag only goes down while straightening (worst rise %.2f cm per frame, limit 2)"), WorstRise), WorstRise <= 2.0);
		TestTrue(TEXT("the length only goes down while straightening"), bLengthFalls);
		TestTrue(TEXT("straight within 3 s"), StraightAt > 0);
		TestTrue(FString::Printf(TEXT("the target is the chord at the snap threshold (%.2f vs %.2f)"), W.Line->GetTargetRestLength(), FVector::Dist(Tip, End)),
			FMath::IsNearlyEqual(W.Line->GetTargetRestLength(), static_cast<float>(FVector::Dist(Tip, End)), 0.5f));

		// Slack appears at once: back to no tension, the target and the length are long again the next frame.
		W.Line->SetTension(0.f);
		Hold(W.Line, Tip, End, Dt);
		TestTrue(FString::Printf(TEXT("slack comes back at once (%.1f vs %.1f cm)"), W.Line->GetRestLength(), SlackLength), FMath::IsNearlyEqual(W.Line->GetRestLength(), SlackLength, 1.f));
		W.Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineExactlyStraight, "Project.Fishing.Line.QA.Taut.ExactlyStraightAtSnapThreshold", Flags)
	bool FQALineExactlyStraight::RunTest(const FString& Parameters)
	{
		// Acceptance: exactly straight at the snap threshold (tension 1), for any geometry, segment count and over-threshold input.
		struct FGeo
		{
			const TCHAR* Name;
			FVector Tip;
			FVector End;
			bool bWater;
		};
		const FGeo Geos[] = {
			{ TEXT("level 15 m"), FVector(0.f, 0.f, 1000.f), FVector(1500.f, 0.f, 1000.f), false },
			{ TEXT("dock to fish under water"), FVector(0.f, 0.f, 300.f), FVector(1500.f, 200.f, -150.f), true },
			{ TEXT("straight down 5 m"), FVector(0.f, 0.f, 800.f), FVector(0.f, 0.f, 300.f), false },
			{ TEXT("long 30 m cast"), FVector(0.f, 0.f, 500.f), FVector(-2000.f, 2200.f, 0.f), true },
			{ TEXT("short 50 cm"), FVector(0.f, 0.f, 1000.f), FVector(40.f, 0.f, 970.f), false },
		};
		{
			// Probe (info): how sensitive the solver is to a rest length a few float steps above the chord.
			FString Probe;
			for (const int32 Ulps : { 0, 1, 5 })
			{
				float Rest = 1500.f;
				for (int32 Ulp = 0; Ulp < Ulps; ++Ulp)
				{
					Rest = std::nextafter(Rest, 2000.f);
				}
				FLureLineSim Sim;
				Sim.Init(12);
				for (int32 Frame = 0; Frame < Frames(3.f); ++Frame)
				{
					Sim.Step(Dt, PinnedInput(FVector(0.f, 0.f, 1000.f), FVector(1500.f, 0.f, 1000.f), Rest), Row());
				}
				Probe += FString::Printf(TEXT(" chord+%d ulp (%.6f cm): %.3f cm off;"), Ulps, Rest - 1500.f, Deviation(Sim.GetPoints()));
			}
			AddInfo(TEXT("solver, 15 m level line:") + Probe);
		}
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		for (const int32 Segments : { 2, 12, 64 })
		{
			ULureFishingLineComponent* Line = W.AddLine(*this, false, Segments);
			if (!Line)
			{
				return false;
			}
			for (const FGeo& Geo : Geos)
			{
				for (const float Tension : { 1.f, 1.5f, 1.0e6f })
				{
					const FString Label = FString::Printf(TEXT("%s, %d segments, tension %.1f"), Geo.Name, Segments, Tension);
					if (Geo.bWater)
					{
						Line->SetWaterSurfaceZ(0.f);
					}
					else
					{
						Line->SetWaterSurfaceZ(-100000.f);
					}
					Line->SetTension(0.f);
					Hold(Line, Geo.Tip, Geo.End, 1.f);
					Line->SetTension(Tension);
					Hold(Line, Geo.Tip, Geo.End, 4.f);
					const TArray<FVector>& P = Line->GetPoints();
					const double Chord = FVector::Dist(Geo.Tip, Geo.End);
					const double Off = Deviation(P);
					TestTrue(FString::Printf(TEXT("%s: straight (%.4f cm off; rest length - chord = %.6f cm)"), *Label, Off, Line->GetRestLength() - Chord), Off < 0.1);
					TestTrue(FString::Printf(TEXT("%s: as long as the straight distance (%.2f vs %.2f)"), *Label, PolylineLength(P), Chord),
						FMath::IsNearlyEqual(PolylineLength(P), Chord, FMath::Max(0.001 * Chord, 0.05)));
					TestTrue(FString::Printf(TEXT("%s: rest length = chord (%.2f)"), *Label, Line->GetRestLength()), FMath::IsNearlyEqual(static_cast<double>(Line->GetRestLength()), Chord, FMath::Max(0.001 * Chord, 0.05)));
					bool bEven = true;
					for (int32 Index = 1; Index < P.Num(); ++Index)
					{
						bEven &= FMath::IsNearlyEqual(FVector::Dist(P[Index - 1], P[Index]), Chord / Segments, FMath::Max(0.01 * Chord / Segments, 0.05));
					}
					TestTrue(Label + TEXT(": evenly spaced points"), bEven);
					TestTrue(Label + TEXT(": pinned at both ends"), P[0].Equals(Geo.Tip, 0.01) && P.Last().Equals(Geo.End, 0.01));
				}
			}
			Line->Hide();
		}
		return true;
	}

	// =================================================================================================================
	// Water
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineFloatsAtWaterZ, "Project.Fishing.Line.QA.Water.FloatsAtTheGivenWaterZ", Flags)
	bool FQALineFloatsAtWaterZ::RunTest(const FString& Parameters)
	{
		// A slack line from a rod tip 2.7 m above the water to a bobber on it lies on the water at WaterZ + FloatHeight, at any
		// water height (sea level, a mountain lake, a sunken cave pool). No point sinks under.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const float FloatHeight = Row().FloatHeight;
		for (const float WaterZ : { 0.f, 2500.f, -1200.f })
		{
			const FVector Tip(0.f, 0.f, WaterZ + 270.f);
			const FVector Bobber(1500.f, 100.f, WaterZ);
			W.Line->SetTension(0.f);
			W.Line->SetWaterSurfaceZ(WaterZ);
			Hold(W.Line, Tip, Bobber, 5.f);
			const TArray<FVector>& P = W.Line->GetPoints();
			const int32 Floating = PointsNear(P, WaterZ + FloatHeight, 1.0);
			const double Lowest = LowestInner(P);
			TestTrue(FString::Printf(TEXT("water %.0f: part of the slack line lies on the water (%d points)"), WaterZ, Floating), Floating >= 3);
			TestTrue(FString::Printf(TEXT("water %.0f: nothing under the surface (lowest %.2f)"), WaterZ, Lowest), Lowest >= WaterZ - 0.5);
			TestTrue(FString::Printf(TEXT("water %.0f: the floating part sits at WaterZ + FloatHeight (lowest %.2f)"), WaterZ, Lowest), FMath::Abs(Lowest - (WaterZ + FloatHeight)) <= 1.0);
		}
		W.Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineFollowsWaterLevel, "Project.Fishing.Line.QA.Water.FollowsWaterLevelChanges", Flags)
	bool FQALineFollowsWaterLevel::RunTest(const FString& Parameters)
	{
		// The water height changes while the line is out (tide, the owner moved to another pond): the floating part follows up and down.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const float FloatHeight = Row().FloatHeight;
		const FVector Tip(0.f, 0.f, 400.f);
		W.Line->SetTension(0.f);
		for (const float WaterZ : { 0.f, 80.f, -60.f, 0.f })
		{
			const FVector Bobber(1500.f, 0.f, WaterZ);
			W.Line->SetWaterSurfaceZ(WaterZ);
			Hold(W.Line, Tip, Bobber, 3.f);
			const TArray<FVector>& P = W.Line->GetPoints();
			TestTrue(FString::Printf(TEXT("water -> %.0f: the line floats at the new level (%d points)"), WaterZ, PointsNear(P, WaterZ + FloatHeight, 1.0)), PointsNear(P, WaterZ + FloatHeight, 1.0) >= 3);
			TestTrue(FString::Printf(TEXT("water -> %.0f: nothing under it (lowest %.2f)"), WaterZ, LowestInner(P)), LowestInner(P) >= WaterZ - 0.5);
		}
		W.Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineFallbackSea, "Project.Fishing.Line.QA.Water.FallbackSeaLevel", Flags)
	bool FQALineFallbackSea::RunTest(const FString& Parameters)
	{
		// No water height from the owner and no water in the world: the line floats on the settings' fallback sea level; with the
		// fallback switched off, there is no water and the slack line hangs freely.
		ULureFishingSettings* Settings = GetMutableDefault<ULureFishingSettings>();
		TGuardValue<float> GuardZ(Settings->FallbackWaterZ, 37.f);
		TGuardValue<bool> GuardUse(Settings->bUseFallbackWaterZ, true);
		const float FloatHeight = Row().FloatHeight;
		const FVector Tip(0.f, 0.f, 37.f + 270.f);
		const FVector Bobber(1500.f, 0.f, 37.f);
		{
			FQALineWorld W;
			if (!W.Create(*this))
			{
				return false;
			}
			W.Line->SetTension(0.f);
			W.Line->ClearWaterSurfaceZ();
			Hold(W.Line, Tip, Bobber, 5.f);
			const TArray<FVector>& P = W.Line->GetPoints();
			TestTrue(FString::Printf(TEXT("fallback sea 37: the line floats on it (%d points, lowest %.2f)"), PointsNear(P, 37.0 + FloatHeight, 1.0), LowestInner(P)),
				PointsNear(P, 37.0 + FloatHeight, 1.0) >= 3 && LowestInner(P) >= 36.5);
			W.Line->Hide();
		}
		{
			TGuardValue<bool> GuardOff(Settings->bUseFallbackWaterZ, false);
			FQALineWorld W;
			if (!W.Create(*this))
			{
				return false;
			}
			W.Line->SetTension(0.f);
			W.Line->ClearWaterSurfaceZ();
			Hold(W.Line, Tip, Bobber, 5.f);
			TestTrue(FString::Printf(TEXT("no water at all: the slack line hangs below where the sea was (lowest %.2f)"), LowestInner(W.Line->GetPoints())), LowestInner(W.Line->GetPoints()) < 37.0 - 5.0);
			W.Line->Hide();
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineTaggedWater, "Project.Fishing.Line.QA.Water.LooksUpWaterAtItsEnd", Flags)
	bool FQALineTaggedWater::RunTest(const FString& Parameters)
	{
		// Without SetWaterSurfaceZ the line finds the water under its end (tagged water first, then the fallback), an owner's water
		// height wins, and once the end moves far (beyond WaterRefreshDistance) into another pond it floats on that pond.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FLureFishingLineRow Tuning = Row();
		const float FloatHeight = Tuning.FloatHeight;
		W.AddWater(FVector2D(1500.f, 0.f), 50.f, 400.f);   // pond A: x 1100..1900, surface 50
		W.AddWater(FVector2D(1500.f, 3000.f), 150.f, 800.f); // pond B: y 2200..3800, surface 150
		const FVector Tip(0.f, 0.f, 400.f);
		W.Line->SetTension(0.f);
		W.Line->ClearWaterSurfaceZ();
		const FVector InA(1500.f, 0.f, 50.f);
		Hold(W.Line, Tip, InA, 5.f);
		TestTrue(FString::Printf(TEXT("pond A: floats at its surface 50 (%d points, lowest %.2f)"), PointsNear(W.Line->GetPoints(), 50.0 + FloatHeight, 1.0), LowestInner(W.Line->GetPoints())),
			PointsNear(W.Line->GetPoints(), 50.0 + FloatHeight, 1.0) >= 2 && LowestInner(W.Line->GetPoints()) >= 49.5);

		W.Line->SetWaterSurfaceZ(120.f);
		Hold(W.Line, Tip, FVector(1500.f, 0.f, 120.f), 4.f);
		TestTrue(FString::Printf(TEXT("the owner's water height wins over the lookup (lowest %.2f)"), LowestInner(W.Line->GetPoints())), FMath::Abs(LowestInner(W.Line->GetPoints()) - (120.0 + FloatHeight)) <= 1.0);
		W.Line->ClearWaterSurfaceZ();
		Hold(W.Line, Tip, InA, 4.f);
		TestTrue(FString::Printf(TEXT("cleared: back on pond A (lowest %.2f)"), LowestInner(W.Line->GetPoints())), FMath::Abs(LowestInner(W.Line->GetPoints()) - (50.0 + FloatHeight)) <= 1.0);

		// The end moves 30 m (well beyond WaterRefreshDistance) into pond B, in steps below TeleportDistance.
		const FVector InB(1500.f, 3000.f, 150.f);
		TestTrue(TEXT("QA precondition: the move is beyond WaterRefreshDistance"), FVector::Dist2D(InA, InB) > Tuning.WaterRefreshDistance);
		for (int32 Step = 1; Step <= 30; ++Step)
		{
			Hold(W.Line, Tip, FMath::Lerp(InA, InB, Step / 30.f), Dt);
		}
		Hold(W.Line, Tip, InB, 5.f);
		const TArray<FVector>& P = W.Line->GetPoints();
		TestTrue(FString::Printf(TEXT("pond B: floats at its surface 150 (%d points, lowest %.2f)"), PointsNear(P, 150.0 + FloatHeight, 1.0), LowestInner(P)),
			PointsNear(P, 150.0 + FloatHeight, 1.0) >= 2 && LowestInner(P) >= 149.5);
		W.Line->Hide();
		return true;
	}

	// =================================================================================================================
	// Length
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineLengthTolerance, "Project.Fishing.Line.QA.Length.ConservedWithinTolerance", Flags)
	bool FQALineLengthTolerance::RunTest(const FString& Parameters)
	{
		// The line can go slack but never stretch: at rest no segment is more than 1 % longer than its share of the line, the
		// polyline is between the chord and the rest length (+1 %), and the rest length follows the spec's target formula.
		const FLureFishingLineRow Tuning = Row();
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FVector Tip(0.f, 0.f, 1300.f);
		const FVector End(1500.f, 300.f, 900.f);
		const double Chord = FVector::Dist(Tip, End);
		for (const int32 Segments : { 4, 12, 32 })
		{
			ULureFishingLineComponent* Line = W.AddLine(*this, false, Segments);
			if (!Line)
			{
				return false;
			}
			Line->SetWaterSurfaceZ(-100000.f);
			for (const float Tension : { 0.f, 0.3f, 0.6f, 0.9f, 1.f })
			{
				const FString Label = FString::Printf(TEXT("%d segments, tension %.1f"), Segments, Tension);
				Line->SetTension(Tension);
				Hold(Line, Tip, End, 4.f);
				const double Taut = 1.0 - FMath::Pow(1.0 - Tension, static_cast<double>(Tuning.TautExponent));
				const double Expected = Chord * (1.0 + Tuning.SlackShare * (1.0 - Taut));
				TestTrue(FString::Printf(TEXT("%s: rest length %.2f = chord x (1 + SlackShare x (1 - tautness)) = %.2f"), *Label, Line->GetRestLength(), Expected),
					FMath::IsNearlyEqual(static_cast<double>(Line->GetRestLength()), Expected, 0.002 * Chord));
				const TArray<FVector>& P = Line->GetPoints();
				const double Segment = Line->GetRestLength() / Segments;
				double Worst = 0.0;
				for (int32 Index = 1; Index < P.Num(); ++Index)
				{
					Worst = FMath::Max(Worst, FVector::Dist(P[Index - 1], P[Index]) / Segment - 1.0);
				}
				const double Poly = PolylineLength(P);
				TestTrue(FString::Printf(TEXT("%s: no segment stretched over 1 %% (worst %.3f %%)"), *Label, Worst * 100.0), Worst <= 0.01);
				TestTrue(FString::Printf(TEXT("%s: chord <= polyline %.2f <= rest length + 1 %%"), *Label, Poly), Poly >= Chord - 0.01 && Poly <= Line->GetRestLength() * 1.01);
				TestTrue(FString::Printf(TEXT("%s: a hanging line uses its whole length (polyline within 1 %%)"), *Label), Poly >= Line->GetRestLength() * 0.99);
			}
			Line->Hide();
		}

		// A slack line on the water to a sunken end (the bobber rides a diving fish): float runs after the constraints in each
		// sub-step, so the last segments down to a deep end stretch. Asserted at the deepest the shipped data can put the line's end
		// (DT_FishFight MaxDepth x DiveBobberShare); deeper ends are measured for the record (risk if the end ever goes deeper).
		float ShippedDepth = 0.f;
		{
			FString FightCsv;
			if (QAFishing::LoadSource(*this, TEXT("DT_FishFight.csv"), FightCsv))
			{
				TStrongObjectPtr<UDataTable> FightTable(QAFishing::MakeTableChecked(*this, FLureFishFightRow::StaticStruct(), FightCsv, TEXT("DT_FishFight")));
				if (FightTable.IsValid())
				{
					for (const TPair<FName, uint8*>& Pair : FightTable->GetRowMap())
					{
						const FLureFishFightRow& Fight = *reinterpret_cast<const FLureFishFightRow*>(Pair.Value);
						ShippedDepth = FMath::Max(ShippedDepth, Fight.MaxDepth * Fight.DiveBobberShare);
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("QA: the shipped deepest line end is known (%.1f cm)"), ShippedDepth), ShippedDepth > 0.f);
		FString DeepRecord;
		for (const float Depth : { ShippedDepth, 100.f, 250.f, 400.f })
		{
			FLureLineSim Deep;
			Deep.Init(12);
			const FVector Rod(0.f, 0.f, 270.f);
			const FVector DeepFish(1500.f, 0.f, -Depth);
			FLureLineSimInput In = PinnedInput(Rod, DeepFish, FLureFishingLineRules::TargetRestLength(static_cast<float>(FVector::Dist(Rod, DeepFish)), 0.f, -1.f, Tuning));
			In.bHasWater = true;
			In.WaterZ = 0.f;
			In.Float = FLureFishingLineRules::FloatAmount(0.f, Tuning);
			double Worst = 0.0;
			for (int32 Frame = 0; Frame < Frames(4.f); ++Frame)
			{
				Deep.Step(Dt, In, Tuning);
				const TArray<FVector>& P = Deep.GetPoints();
				for (int32 Index = 1; Index < P.Num(); ++Index)
				{
					Worst = FMath::Max(Worst, FVector::Dist(P[Index - 1], P[Index]) / Deep.GetSegmentLength() - 1.0);
				}
			}
			if (Depth == ShippedDepth)
			{
				TestTrue(FString::Printf(TEXT("slack line to an end %.0f cm deep (the shipped maximum): no segment stretched over 3 %% (worst %.2f %%)"), Depth, Worst * 100.0), Worst <= 0.03);
			}
			else
			{
				DeepRecord += FString::Printf(TEXT(" %.0f cm: %.1f %%;"), Depth, Worst * 100.0);
			}
		}
		AddInfo(TEXT("slack line to a deeper end (not reachable with shipped data), worst segment stretch:") + DeepRecord);

		// A fish zig-zagging at up to 10 m/s with sudden turns: the line stretches at most 3 % at any frame.
		FLureLineSim Sim;
		Sim.Init(12);
		FRandomStream Random(3203);
		FVector Fish(1500.f, 0.f, -50.f);
		FVector Velocity(0.f, 800.f, 0.f);
		double WorstStretch = 0.0;
		float Tension = 0.5f;
		for (int32 Frame = 0; Frame < 1200; ++Frame)
		{
			if (Frame % 20 == 0)
			{
				// A new run: any direction, up to 10 m/s.
				const double Angle = Random.FRandRange(0.f, 2.f * UE_PI);
				Velocity = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Random.FRandRange(200.f, 1000.f);
			}
			Tension = FMath::Clamp(Tension + Random.FRandRange(-0.05f, 0.05f), 0.f, 1.f); // a fight's tension moves smoothly
			Fish += Velocity * Dt;
			Fish.X = FMath::Clamp(Fish.X, 500.0, 2500.0);
			Fish.Y = FMath::Clamp(Fish.Y, -1000.0, 1000.0);
			const FVector Rod(0.f, 0.f, 300.f);
			const float Rest = FLureFishingLineRules::TargetRestLength(static_cast<float>(FVector::Dist(Rod, Fish)), Tension, -1.f, Tuning);
			Sim.Step(Dt, PinnedInput(Rod, Fish, Rest), Tuning);
			WorstStretch = FMath::Max(WorstStretch, Sim.GetPolylineLength() / Sim.GetRestLength() - 1.0);
		}
		TestTrue(FString::Printf(TEXT("a fish zig-zagging at up to 10 m/s never stretches the line over 3 %% (worst %.2f %%)"), WorstStretch * 100.0), WorstStretch <= 0.03);
		return true;
	}

	// =================================================================================================================
	// Extreme inputs
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineZeroLength, "Project.Fishing.Line.QA.Extreme.ZeroLengthLine", Flags)
	bool FQALineZeroLength::RunTest(const FString& Parameters)
	{
		// Tip and end in the same place (the bobber reeled to the rod tip), at every tension: finite, every point on the tip,
		// a finite unit end direction. The same for a hanging actor on 0 cm of line.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FVector Tip(100.f, -50.f, 300.f);
		for (const float Tension : { 0.f, 0.5f, 1.f })
		{
			W.Line->SetTension(Tension);
			Hold(W.Line, Tip, Tip, 1.f);
			const TArray<FVector>& P = W.Line->GetPoints();
			bool bOnTip = true;
			for (const FVector& Point : P)
			{
				bOnTip &= Point.Equals(Tip, 0.5);
			}
			TestTrue(FString::Printf(TEXT("tension %.1f: all points finite"), Tension), AllFinite(P));
			TestTrue(FString::Printf(TEXT("tension %.1f: a zero-length line sits on the tip"), Tension), bOnTip);
			TestTrue(FString::Printf(TEXT("tension %.1f: rest length 0 (%.3f)"), Tension, W.Line->GetRestLength()), FMath::IsNearlyZero(W.Line->GetRestLength(), 0.5f));
			const FVector Up = W.Line->GetEndDirection();
			TestTrue(TEXT("the end direction is a finite unit vector"), !Up.ContainsNaN() && FMath::IsNearlyEqual(Up.Size(), 1.0, 1.0e-3));
		}
		// Then it opens up normally.
		Hold(W.Line, Tip, Tip + FVector(1200.f, 0.f, -300.f), 2.f);
		TestTrue(TEXT("from zero length the line opens up to the bobber"), W.Line->GetPoints().Last().Equals(Tip + FVector(1200.f, 0.f, -300.f), 0.01) && AllFinite(W.Line->GetPoints()));
		W.Line->Hide();

		AActor* Fish = W.SpawnMovable(Tip);
		W.Line->AttachEndActor(Fish, 0.f);
		W.Tick(60);
		TestTrue(TEXT("a hanging actor on 0 cm of line: finite, at the tip"), !W.Line->GetEndPoint().ContainsNaN() && W.Line->GetEndPoint().Equals(Tip, 2.0) && !Fish->GetActorLocation().ContainsNaN());
		W.Line->DetachEndActor();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineHugeDistances, "Project.Fishing.Line.QA.Extreme.HugeDistances", Flags)
	bool FQALineHugeDistances::RunTest(const FString& Parameters)
	{
		// A 200 m line is still a line; a 500 km one and coordinates far outside the world are clamped: finite, never beyond the
		// documented coordinate clamp, and back to normal once the inputs are sane again.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FVector Tip(0.f, 0.f, 300.f);
		W.Line->SetWaterSurfaceZ(0.f);
		const FVector Far(20000.f, 0.f, 0.f);
		Hold(W.Line, Tip, Far, 3.f);
		TestTrue(TEXT("a 200 m line: pinned at both ends and finite"), W.Line->GetPoints()[0].Equals(Tip, 0.01) && W.Line->GetPoints().Last().Equals(Far, 0.01) && AllFinite(W.Line->GetPoints()));

		const TArray<TPair<FVector, FVector>> Cases = {
			{ Tip, FVector(5.0e7, 0.0, 0.0) },
			{ FVector(-5.0e9, 3.0e9, 1.0e10), FVector(5.0e9, -3.0e9, -1.0e10) },
			{ FVector(1.0e30, 0.0, 0.0), Tip },
			{ FVector(TNumericLimits<float>::Max()), FVector(-TNumericLimits<float>::Max()) },
		};
		for (const TPair<FVector, FVector>& Case : Cases)
		{
			W.Line->SetTension(0.f);
			for (int32 Frame = 0; Frame < 30; ++Frame)
			{
				W.Line->SetEndpoints(Case.Key, Case.Value);
				W.Line->SetTension(Frame % 2 == 0 ? 0.f : 1.f);
				W.Line->UpdateLine(Dt);
			}
			const TArray<FVector>& P = W.Line->GetPoints();
			bool bBounded = true;
			for (const FVector& Point : P)
			{
				bBounded &= Point.GetAbsMax() <= FLureLineSim::MaxCoordinate * 1.001;
			}
			TestTrue(FString::Printf(TEXT("%s -> %s: finite"), *Case.Key.ToString(), *Case.Value.ToString()), AllFinite(P) && FMath::IsFinite(W.Line->GetRestLength()));
			TestTrue(FString::Printf(TEXT("%s -> %s: within the coordinate clamp"), *Case.Key.ToString(), *Case.Value.ToString()), bBounded);
			TestTrue(TEXT("... the end direction is finite"), !W.Line->GetEndDirection().ContainsNaN());
		}
		Hold(W.Line, Tip, FVector(1500.f, 0.f, 0.f), 1.f);
		TestTrue(TEXT("back to sane inputs: the line is back between the rod and the bobber"),
			W.Line->GetPoints()[0].Equals(Tip, 0.01) && W.Line->GetPoints().Last().Equals(FVector(1500.f, 0.f, 0.f), 0.01) && Deviation(W.Line->GetPoints()) < 500.0);
		W.Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineNonFiniteInputs, "Project.Fishing.Line.QA.Extreme.NonFiniteInputsKeepLastGood", Flags)
	bool FQALineNonFiniteInputs::RunTest(const FString& Parameters)
	{
		// NaN/Inf endpoints, tension, slack, water and viewer through the component's API: the line keeps its last good ends,
		// stays finite with positive finite widths, and follows again when good values come back.
		FQALineWorld W;
		if (!W.Create(*this, /*bMesh*/ true))
		{
			return false;
		}
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Bobber(1400.f, 0.f, 0.f);
		W.Line->SetViewer(FVector(-50.f, 0.f, 320.f), 90.f);
		W.Line->SetWaterSurfaceZ(0.f);
		Hold(W.Line, Tip, Bobber, 1.f);
		const FVector BadVectors[] = { FVector(NaN), FVector(Inf, 0.f, 0.f), FVector(0.f, -Inf, NaN) };
		int32 Case = 0;
		for (const FVector& Bad : BadVectors)
		{
			for (int32 Which = 0; Which < 3; ++Which)
			{
				const FString Label = FString::Printf(TEXT("case %d (%s on %s)"), Case++, *Bad.ToString(), Which == 0 ? TEXT("the tip") : (Which == 1 ? TEXT("the end") : TEXT("both")));
				for (int32 Frame = 0; Frame < 20; ++Frame)
				{
					W.Line->SetEndpoints(Which == 1 ? Tip : Bad, Which == 0 ? Bobber : Bad);
					W.Line->SetTension(Frame % 2 ? NaN : Inf);
					W.Line->SetSlack(Frame % 2 ? NaN : Inf);
					W.Line->SetWaterSurfaceZ(Frame % 2 ? NaN : -Inf);
					W.Line->SetViewer(Frame % 2 ? FVector(NaN) : FVector(Inf), Frame % 2 ? NaN : 0.f);
					W.Line->SetWidthRule(NaN, 0.f, -Inf);
					W.Line->UpdateLine(Frame % 3 == 0 ? NaN : Dt);
				}
				const TArray<FVector>& P = W.Line->GetPoints();
				TestTrue(Label + TEXT(": finite points"), AllFinite(P));
				TestTrue(Label + TEXT(": finite rest length, tension and end direction"), FMath::IsFinite(W.Line->GetRestLength()) && FMath::IsFinite(W.Line->GetTension()) && !W.Line->GetEndDirection().ContainsNaN());
				TestTrue(Label + TEXT(": the good end stays where it was"), (Which == 1 ? P[0].Equals(Tip, 0.01) : true) && (Which == 0 ? P.Last().Equals(Bobber, 0.01) : true));
				TestTrue(Label + TEXT(": a bad end keeps its last good value"), (Which != 1 ? P[0].Equals(Tip, 0.01) : true) && (Which != 0 ? P.Last().Equals(Bobber, 0.01) : true));
				bool bWidths = W.Line->GetWidths().Num() == P.Num();
				for (const float Width : W.Line->GetWidths())
				{
					bWidths &= FMath::IsFinite(Width) && Width > 0.f;
				}
				TestTrue(Label + TEXT(": one positive finite width per point"), bWidths);
				// Good values again.
				W.Line->SetViewer(FVector(-50.f, 0.f, 320.f), 90.f);
				W.Line->SetWidthRule(2.5f, 1920.f, 0.15f);
				W.Line->SetWaterSurfaceZ(0.f);
				W.Line->SetSlack(-1.f);
				W.Line->SetTension(0.f);
				Hold(W.Line, Tip + FVector(0.f, 10.f, 0.f), Bobber + FVector(0.f, 10.f, 0.f), 0.5f);
				TestTrue(Label + TEXT(": follows good ends again"), W.Line->GetPoints()[0].Equals(Tip + FVector(0.f, 10.f, 0.f), 0.01) && W.Line->GetPoints().Last().Equals(Bobber + FVector(0.f, 10.f, 0.f), 0.01));
				Hold(W.Line, Tip, Bobber, 0.5f);
			}
		}
		W.Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineRapidTeleports, "Project.Fishing.Line.QA.Extreme.RapidTeleportsNoExplosion", Flags)
	bool FQALineRapidTeleports::RunTest(const FString& Parameters)
	{
		// Fixed-seed fuzz of the solver: ends that jump small, medium and huge amounts every frame, hitches, zero and tiny frames,
		// random rest lengths (also shorter than the chord), water on/off, free-end phases. Every frame: finite, pinned ends exactly on
		// their inputs, and no explosion: no point farther from either pinned end than the line between them (+5 % + 5 cm).
		const FLureFishingLineRow Tuning = Row();
		FRandomStream Random(32032);
		FLureLineSim Sim;
		Sim.Init(12);
		FVector Tip(0.f, 0.f, 300.f);
		FVector End(1500.f, 0.f, 0.f);
		int32 NotFinite = 0;
		int32 OffPin = 0;
		int32 Exploded = 0;
		double WorstReach = 0.0;
		FString Examples;
		const int32 N = Sim.GetNumSegments();
		for (int32 Frame = 0; Frame < 3000; ++Frame)
		{
			const int32 Kind = Random.RandRange(0, 9);
			const float Jump = Kind < 5 ? 20.f : (Kind < 8 ? 800.f : 20000.f); // small moves, fast moves, teleports
			Tip += Random.GetUnitVector() * Random.FRandRange(0.f, Jump);
			End += Random.GetUnitVector() * Random.FRandRange(0.f, Jump);
			// Realistic heights: the rod tip above the water, the end (bobber, fish) near or under it; anywhere in a 1 km square.
			Tip = Tip.BoundToCube(50000.0);
			End = End.BoundToCube(50000.0);
			Tip.Z = FMath::Clamp(Tip.Z, 100.0, 2000.0);
			End.Z = FMath::Clamp(End.Z, -600.0, 300.0);
			const bool bFree = (Frame / 200) % 5 == 4;
			// Inputs as the component forms them: rest length and float from one tension; sometimes a rest length shorter than the chord.
			const float Tension = Random.FRand();
			const bool bTooShort = Random.FRand() < 0.1f;
			const float Chord = static_cast<float>(FVector::Dist(Tip, End));
			FLureLineSimInput In = PinnedInput(Tip, End, bTooShort ? 0.5f * Chord : FLureFishingLineRules::TargetRestLength(Chord, Tension, Random.FRand() < 0.2f ? Random.FRand() : -1.f, Tuning));
			In.bFreeEnd = bFree;
			In.EndMass = bFree ? Tuning.HangEndMass : 1.f;
			In.bHasWater = Random.FRand() < 0.5f;
			In.WaterZ = static_cast<float>(Random.FRandRange(-50.f, 50.f));
			In.Float = bTooShort ? 0.f : FLureFishingLineRules::FloatAmount(Tension, Tuning);
			const float StepDt = static_cast<float>(Kind == 9 ? Random.FRandRange(0.1f, 2.f) : (Kind == 0 ? 0.0 : Random.FRandRange(1.0e-5f, 0.05f)));
			if (bFree && !Sim.IsEndFree())
			{
				Sim.ReleaseEnd(Tuning.HangEndMass);
			}
			Sim.Step(StepDt, In, Tuning);
			const TArray<FVector>& P = Sim.GetPoints();
			if (!Sim.IsFinite() || !AllFinite(P))
			{
				++NotFinite;
				continue;
			}
			const double Seg = Sim.GetSegmentLength();
			OffPin += P[0].Equals(Tip, 0.05) ? 0 : 1;
			if (StepDt <= 0.f)
			{
				continue; // documented: a frame of 0 s only moves the pinned ends (no solve)
			}
			if (!Sim.IsEndFree())
			{
				OffPin += P.Last().Equals(End, 0.05) ? 0 : 1;
			}
			for (int32 Index = 0; Index <= N; ++Index)
			{
				const double FromTip = FVector::Dist(P[Index], P[0]) - Index * Seg;
				const double FromEnd = Sim.IsEndFree() ? 0.0 : FVector::Dist(P[Index], P.Last()) - (N - Index) * Seg;
				const double Over = FMath::Max(FromTip, FromEnd);
				WorstReach = FMath::Max(WorstReach, Over / FMath::Max(Sim.GetRestLength(), 1.0));
				if (Over > 0.05 * Sim.GetRestLength() + 5.0)
				{
					if (Exploded < 6)
					{
						Examples += FString::Printf(TEXT(" [frame %d kind %d dt %.4f free %d tooShort %d point %d over %.1f rest %.1f chord %.1f]"),
							Frame, Kind, StepDt, Sim.IsEndFree() ? 1 : 0, bTooShort ? 1 : 0, Index, Over, Sim.GetRestLength(), FVector::Dist(Tip, End));
					}
					++Exploded;
				}
			}
		}
		if (!Examples.IsEmpty())
		{
			AddInfo(TEXT("first violations:") + Examples);
		}
		TestEqual(TEXT("every frame finite"), NotFinite, 0);
		TestEqual(TEXT("pinned ends always exactly on their inputs"), OffPin, 0);
		TestEqual(FString::Printf(TEXT("no point ever farther from a pinned end than the line between them (+5 %% + 5 cm; worst %.3f %% of the line)"), WorstReach * 100.0), Exploded, 0);
		return true;
	}

	// =================================================================================================================
	// Snap
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineSnapRecoil, "Project.Fishing.Line.QA.Snap.RecoilThenGone", Flags)
	bool FQALineSnapRecoil::RunTest(const FString& Parameters)
	{
		// Snap: the end lets go, whips back toward the rod, the line shrinks toward RecoilLengthShare of its length, it plays for
		// RecoilTime (still there at 90 %), then it is gone and stops ticking. A second Snap during the recoil changes nothing.
		FQALineWorld W;
		if (!W.Create(*this, /*bMesh*/ true))
		{
			return false;
		}
		const FLureFishingLineRow Tuning = Row();
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Fish(1500.f, 300.f, -80.f);
		W.Line->SetWaterSurfaceZ(0.f);
		W.Line->SetTension(1.f);
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			W.Line->SetEndpoints(Tip, Fish);
			W.Tick(1);
		}
		const float StartLength = W.Line->GetRestLength();
		const double StartReach = FVector::Dist(W.Line->GetEndPoint(), Tip);
		W.Line->Snap();
		W.Line->Hide();
		TestEqual(TEXT("mode Recoil"), static_cast<int32>(W.Line->GetMode()), static_cast<int32>(ELureLineMode::Recoil));
		double MinReach = StartReach;
		bool bFinite = true;
		const int32 Visible = FMath::FloorToInt32(0.9f * Tuning.RecoilTime / Dt);
		float LastLength = StartLength;
		for (int32 Frame = 0; Frame < Visible; ++Frame)
		{
			if (Frame == 5)
			{
				W.Line->Snap(); // a second snap signal (e.g. a late RepNotify)
			}
			W.Tick(1);
			MinReach = FMath::Min(MinReach, FVector::Dist(W.Line->GetEndPoint(), Tip));
			bFinite &= AllFinite(W.Line->GetPoints()) && !W.Line->GetEndPoint().ContainsNaN();
			LastLength = W.Line->GetRestLength();
		}
		TestTrue(TEXT("finite through the recoil"), bFinite);
		TestTrue(TEXT("still recoiling at 90 % of RecoilTime"), W.Line->IsRecoiling() && W.Line->IsLineVisible());
		TestTrue(FString::Printf(TEXT("the loose end came back toward the rod (%.0f -> closest %.0f cm)"), StartReach, MinReach), MinReach < 0.5 * StartReach);
		TestTrue(FString::Printf(TEXT("the line shrank toward RecoilLengthShare (%.0f -> %.0f cm, share %.2f)"), StartLength, LastLength, Tuning.RecoilLengthShare),
			LastLength <= StartLength * (Tuning.RecoilLengthShare + 0.15f) && LastLength >= StartLength * Tuning.RecoilLengthShare * 0.9f);
		W.Tick(Frames(0.1f * Tuning.RecoilTime) + 3);
		TestFalse(TEXT("after RecoilTime the line is gone"), W.Line->IsLineVisible() || W.Line->IsRecoiling());
		TestEqual(TEXT("mode None"), static_cast<int32>(W.Line->GetMode()), static_cast<int32>(ELureLineMode::None));
		TestFalse(TEXT("... and does not tick"), W.Line->IsComponentTickEnabled());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineNoLeak, "Project.Fishing.Line.QA.Snap.NoLeakAfterManyCasts", Flags)
	bool FQALineNoLeak::RunTest(const FString& Parameters)
	{
		// 120 casts ending every way (snap + recoil, reel in (Hide), a new cast during a recoil, a landed fish hung and destroyed,
		// the old SetLine): the same drawn segment components throughout, no new components on the owner, same point count, tick off.
		FQALineWorld W;
		if (!W.Create(*this, /*bMesh*/ true))
		{
			return false;
		}
		const FLureFishingLineRow Tuning = Row();
		const int32 BaseSegments = LiveSegmentComponents(W.Owner);
		const int32 BaseComponents = W.Owner->GetComponents().Num();
		const int32 BasePoints = W.Line->GetPoints().Num();
		TestTrue(FString::Printf(TEXT("QA precondition: the drawn line has its segment components (%d)"), BaseSegments), BaseSegments >= 12);
		const FVector Tip(0.f, 0.f, 300.f);
		int32 Hung = 0;
		for (int32 Cast = 0; Cast < 120; ++Cast)
		{
			const FVector Bobber(800.f + 10.f * Cast, (Cast % 7) * 50.f, 0.f);
			W.Line->SetTension((Cast % 5) / 4.f);
			for (int32 Frame = 0; Frame < 8; ++Frame)
			{
				W.Line->SetEndpoints(Tip, Bobber);
				W.Tick(1);
			}
			switch (Cast % 5)
			{
			case 0:
				W.Line->Snap();
				W.Line->Hide();
				W.Tick(Frames(Tuning.RecoilTime) + 3);
				break;
			case 1:
				W.Line->Hide();
				W.Tick(2);
				break;
			case 2:
				W.Line->Snap();
				W.Tick(3);
				W.Line->SetEndpoints(Tip, Bobber); // a new cast during the recoil
				W.Tick(3);
				W.Line->Hide();
				W.Tick(2);
				break;
			case 3:
			{
				AActor* Fish = W.SpawnMovable(Bobber);
				W.Line->Hide();
				W.Line->AttachEndActor(Fish, 100.f);
				W.Tick(10);
				Fish->Destroy();
				W.Tick(3);
				++Hung;
				break;
			}
			default:
				W.Line->SetLine(Tip, Bobber, 0.05f, FVector(-50.f, 0.f, 320.f), 90.f, 2.5f, 1920.f, 0.15f);
				W.Tick(3);
				W.Line->Hide();
				W.Tick(1);
				break;
			}
			if (W.Line->IsLineVisible() || W.Line->IsComponentTickEnabled())
			{
				AddError(FString::Printf(TEXT("cast %d (ending %d): the line is still out or ticking"), Cast, Cast % 5));
				break;
			}
		}
		TestEqual(TEXT("the same number of live segment components"), LiveSegmentComponents(W.Owner), BaseSegments);
		TestEqual(TEXT("no components added to the owner"), W.Owner->GetComponents().Num(), BaseComponents);
		TestEqual(TEXT("the same number of points"), W.Line->GetPoints().Num(), BasePoints);
		TestTrue(TEXT("QA: fish were hung and destroyed"), Hung > 0);
		return true;
	}

	// =================================================================================================================
	// Cost while the line is in
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineIdleCost, "Project.Fishing.Line.QA.Cost.NothingRunsWhileTheLineIsIn", Flags)
	bool FQALineIdleCost::RunTest(const FString& Parameters)
	{
		// In every "no line" situation the component does not tick and nothing moves; setting tension, water, viewer or slack
		// alone does not start a line (only SetEndpoints, SetLine or AttachEndActor do).
		FQALineWorld W;
		if (!W.Create(*this, /*bMesh*/ true))
		{
			return false;
		}
		const FLureFishingLineRow Tuning = Row();
		const FVector Tip(0.f, 0.f, 300.f);
		const FVector Bobber(1200.f, 0.f, 0.f);
		auto ExpectIdle = [this, &W](const TCHAR* When)
		{
			TestFalse(FString::Printf(TEXT("%s: not visible"), When), W.Line->IsLineVisible());
			TestFalse(FString::Printf(TEXT("%s: no tick"), When), W.Line->IsComponentTickEnabled());
			const TArray<FVector> Before = W.Line->GetPoints();
			W.Tick(120);
			TestFalse(FString::Printf(TEXT("%s: still no tick after 2 s"), When), W.Line->IsComponentTickEnabled());
			TestTrue(FString::Printf(TEXT("%s: nothing moved"), When), Before == W.Line->GetPoints());
		};
		ExpectIdle(TEXT("never used"));
		W.Line->SetTension(0.7f);
		W.Line->SetSlack(0.2f);
		W.Line->SetWaterSurfaceZ(0.f);
		W.Line->SetViewer(FVector(-50.f, 0.f, 320.f), 90.f);
		W.Line->SetWidthRule(2.5f, 1920.f, 0.15f);
		W.Line->DetachEndActor();
		W.Line->Snap();
		ExpectIdle(TEXT("setters, detach and snap with no line"));

		W.Line->SetEndpoints(Tip, Bobber);
		W.Tick(30);
		W.Line->Hide();
		ExpectIdle(TEXT("after Hide"));

		W.Line->SetEndpoints(Tip, Bobber);
		W.Tick(30);
		W.Line->Snap();
		W.Line->Hide();
		W.Tick(Frames(Tuning.RecoilTime) + 3);
		ExpectIdle(TEXT("after a snap recoil"));

		AActor* Fish = W.SpawnMovable(Tip - FVector(0.f, 0.f, 100.f));
		W.Line->AttachEndActor(Fish, 100.f);
		W.Tick(30);
		W.Line->DetachEndActor();
		ExpectIdle(TEXT("after a hanging actor was detached (no line out)"));

		AActor* Other = W.SpawnMovable(Tip - FVector(0.f, 0.f, 100.f));
		W.Line->AttachEndActor(Other, 100.f);
		W.Tick(30);
		Other->Destroy();
		W.Tick(2);
		ExpectIdle(TEXT("after the hanging actor was destroyed"));
		return true;
	}

	// =================================================================================================================
	// End point and hanging actor contract
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineEndPointContract, "Project.Fishing.Line.QA.Api.PinnedEndPointContract", Flags)
	bool FQALineEndPointContract::RunTest(const FString& Parameters)
	{
		// Pinned: every frame GetStartPoint/points[0] are the tip and GetEndPoint/points.Last() are exactly this frame's end, even
		// while the fish swims fast or the end jumps beyond TeleportDistance; GetEndDirection is a unit vector up the last segment.
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		W.Line->SetWaterSurfaceZ(0.f);
		const FVector Tip(0.f, 0.f, 300.f);
		int32 Wrong = 0;
		int32 BadDirection = 0;
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			const double T = Frame * Dt;
			FVector End(1500.0 + 600.0 * FMath::Sin(3.0 * T), 900.0 * FMath::Cos(2.0 * T), -100.0);
			if (Frame == 300)
			{
				End += FVector(4000.f, 0.f, 0.f); // beyond TeleportDistance
			}
			W.Line->SetTension(static_cast<float>(0.5 + 0.5 * FMath::Sin(T)));
			W.Line->SetEndpoints(Tip, End);
			W.Line->UpdateLine(Dt);
			const TArray<FVector>& P = W.Line->GetPoints();
			Wrong += (W.Line->GetEndPoint().Equals(End, 0.01) && P.Last().Equals(End, 0.01) && W.Line->GetStartPoint().Equals(Tip, 0.01) && P[0].Equals(Tip, 0.01)) ? 0 : 1;
			const FVector Up = W.Line->GetEndDirection();
			const FVector Expected = (P[P.Num() - 2] - P.Last()).GetSafeNormal();
			BadDirection += (FMath::IsNearlyEqual(Up.Size(), 1.0, 1.0e-3) && (Expected.IsZero() || Up.Equals(Expected, 1.0e-3))) ? 0 : 1;
		}
		TestEqual(TEXT("frames where an end was not exactly on its input"), Wrong, 0);
		TestEqual(TEXT("frames where GetEndDirection was not the unit vector up the last segment"), BadDirection, 0);
		TestEqual(TEXT("mode Pinned"), static_cast<int32>(W.Line->GetMode()), static_cast<int32>(ELureLineMode::Pinned));
		TestTrue(TEXT("no end actor while pinned"), W.Line->GetEndActor() == nullptr);
		W.Line->DetachEndActor();
		TestEqual(TEXT("DetachEndActor with nothing attached keeps the pinned line"), static_cast<int32>(W.Line->GetMode()), static_cast<int32>(ELureLineMode::Pinned));
		W.Line->Hide();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineAttachContract, "Project.Fishing.Line.QA.Api.AttachEndActorContract", Flags)
	bool FQALineAttachContract::RunTest(const FString& Parameters)
	{
		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		const FLureFishingLineRow Tuning = Row();
		const FVector Tip(0.f, 0.f, 500.f);
		W.Line->SetWaterSurfaceZ(-100000.f);

		// (1) A 15 m line out, a fish attached on 100 cm without orienting: the line is reeled up smoothly (not in one frame) to
		//     HangLength; every frame the actor's hook point is the line's end and its rotation is left alone.
		Hold(W.Line, Tip, FVector(1500.f, 0.f, 300.f), 1.f);
		const FRotator Facing(0.f, 90.f, 0.f);
		AActor* Fish = W.SpawnMovable(FVector(1500.f, 0.f, 300.f), Facing);
		const FVector Hook(0.f, 25.f, 10.f);
		W.Line->AttachEndActor(Fish, 100.f, Hook, /*bOrientAlongLine*/ false);
		TestTrue(TEXT("GetEndActor is the fish"), W.Line->GetEndActor() == Fish);
		TestEqual(TEXT("mode Hanging"), static_cast<int32>(W.Line->GetMode()), static_cast<int32>(ELureLineMode::Hanging));
		int32 OffHook = 0;
		int32 Turned = 0;
		bool bShrinks = true;
		float Previous = TNumericLimits<float>::Max();
		float AfterOneFrame = 0.f;
		for (int32 Frame = 0; Frame < Frames(4.f); ++Frame)
		{
			W.Tick(1);
			const FVector HookWorld = Fish->GetActorLocation() + Fish->GetActorQuat().RotateVector(Hook);
			OffHook += HookWorld.Equals(W.Line->GetEndPoint(), 0.1) ? 0 : 1;
			Turned += Fish->GetActorRotation().Equals(Facing, 0.01) ? 0 : 1;
			bShrinks &= W.Line->GetRestLength() <= Previous + 0.01f;
			Previous = W.Line->GetRestLength();
			if (Frame == 0)
			{
				AfterOneFrame = W.Line->GetRestLength();
			}
		}
		TestEqual(TEXT("frames where the hook point was not on the line's end"), OffHook, 0);
		TestEqual(TEXT("bOrientAlongLine false: frames where the actor was turned"), Turned, 0);
		TestTrue(FString::Printf(TEXT("a longer line is reeled up smoothly: not at once (%.0f cm after one frame)"), AfterOneFrame), AfterOneFrame > 400.f);
		TestTrue(TEXT("... and only shortens"), bShrinks);
		TestTrue(FString::Printf(TEXT("... to HangLength (%.2f)"), W.Line->GetRestLength()), FMath::IsNearlyEqual(W.Line->GetRestLength(), 100.f, 1.f));
		TestTrue(FString::Printf(TEXT("the end is never farther from the tip than the line (%.1f)"), FVector::Dist(Tip, W.Line->GetEndPoint())), FVector::Dist(Tip, W.Line->GetEndPoint()) <= W.Line->GetRestLength() * 1.02 + 1.0);
		W.Line->DetachEndActor();
		TestTrue(TEXT("detached: the actor is let go, the line (still out) is pinned again"), W.Line->GetEndActor() == nullptr && W.Line->GetMode() == ELureLineMode::Pinned);
		const FVector LetGo = Fish->GetActorLocation();
		W.Tick(10);
		TestTrue(TEXT("a let-go actor stays where it is"), Fish->GetActorLocation().Equals(LetGo, 0.01));
		W.Line->Hide();

		// (2) No line out, the fish 20 cm under the tip, HangLength 150: a shorter line drops: the fish falls to 150 cm under the tip.
		AActor* Low = W.SpawnMovable(Tip - FVector(0.f, 0.f, 20.f));
		W.Line->AttachEndActor(Low, 150.f);
		W.Tick(2);
		TestTrue(FString::Printf(TEXT("a shorter line drops to HangLength at once (%.1f)"), W.Line->GetRestLength()), FMath::IsNearlyEqual(W.Line->GetRestLength(), 150.f, 1.f));
		W.Tick(Frames(4.f));
		TestTrue(FString::Printf(TEXT("the fish hangs 150 cm under the tip (end %s)"), *W.Line->GetEndPoint().ToString()), W.Line->GetEndPoint().Equals(Tip - FVector(0.f, 0.f, 150.f), 5.0));
		TestTrue(FString::Printf(TEXT("bOrientAlongLine: +X points up the line (%s vs %s)"), *Low->GetActorForwardVector().ToString(), *W.Line->GetEndDirection().ToString()),
			Low->GetActorForwardVector().Equals(W.Line->GetEndDirection(), 0.05));
		W.Line->DetachEndActor();
		TestFalse(TEXT("detached with no line out: gone"), W.Line->IsLineVisible());

		// (3) Hostile hang lengths: finite, no crash; the actor stays finite.
		for (const float Hang : { -10.f, NaN, Inf, 1.0e9f })
		{
			AActor* Odd = W.SpawnMovable(Tip - FVector(0.f, 0.f, 50.f));
			W.Line->AttachEndActor(Odd, Hang);
			W.Tick(30);
			TestTrue(FString::Printf(TEXT("hang length %f: finite end, actor and line"), Hang),
				!W.Line->GetEndPoint().ContainsNaN() && !Odd->GetActorLocation().ContainsNaN() && AllFinite(W.Line->GetPoints()) && FMath::IsFinite(W.Line->GetRestLength()));
			TestTrue(FString::Printf(TEXT("hang length %f: the end is within 1 km of the tip"), Hang), FVector::Dist(W.Line->GetEndPoint(), Tip) <= FLureLineSim::MaxLength * 1.01);
			W.Line->DetachEndActor();
			Odd->Destroy();
		}
		W.Tick(2);
		TestFalse(TEXT("all detached: no line"), W.Line->IsLineVisible());
		(void)Tuning;
		return true;
	}

	// =================================================================================================================
	// Proxies (cosmetic on every machine from replicated inputs)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineDeterministic, "Project.Fishing.Line.QA.Net.SameInputsSameLine", Flags)
	bool FQALineDeterministic::RunTest(const FString& Parameters)
	{
		// Every machine simulates its own line: the same inputs must give the same line, bit for bit, whatever the owner's net
		// role (server, owner's client, another player's copy).
		const FLureFishingLineRow Tuning = Row();
		{
			FLureLineSim A;
			FLureLineSim B;
			A.Init(12);
			B.Init(12);
			FRandomStream Random(77);
			bool bSame = true;
			for (int32 Frame = 0; Frame < 900 && bSame; ++Frame)
			{
				FLureLineSimInput In = PinnedInput(FVector(0.f, 0.f, 300.f), FVector(1500.f + Random.FRandRange(-300.f, 300.f), Random.FRandRange(-300.f, 300.f), -30.f), 0.f);
				In.RestLength = static_cast<float>(FVector::Dist(In.Start, In.End)) * (1.f + 0.05f * Random.FRand());
				In.bHasWater = true;
				In.Float = Random.FRand();
				const float StepDt = static_cast<float>(Random.FRandRange(1.f / 144.f, 1.f / 20.f));
				A.Step(StepDt, In, Tuning);
				B.Step(StepDt, In, Tuning);
				if (Frame == 600)
				{
					A.ReleaseEnd(1.f);
					B.ReleaseEnd(1.f);
					A.AddRecoil(Tuning.RecoilSpeed);
					B.AddRecoil(Tuning.RecoilSpeed);
				}
				bSame = FMemory::Memcmp(A.GetPoints().GetData(), B.GetPoints().GetData(), sizeof(FVector) * A.GetPoints().Num()) == 0;
			}
			TestTrue(TEXT("two solvers fed the same inputs stay bit-identical (900 frames incl. a snap)"), bSame);
		}

		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		TArray<AActor*> Owners;
		TArray<ULureFishingLineComponent*> Lines;
		for (const ENetRole Role : { ROLE_Authority, ROLE_AutonomousProxy, ROLE_SimulatedProxy })
		{
			AActor* LineOwner = nullptr;
			ULureFishingLineComponent* Line = W.AddLine(*this, false, 12, &LineOwner);
			if (!Line)
			{
				return false;
			}
			LineOwner->SetRole(Role);
			Owners.Add(LineOwner);
			Lines.Add(Line);
		}
		FRandomStream Random(99);
		bool bSame = true;
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			const FVector Tip(0.f, 10.f * FMath::Sin(Frame * 0.1f), 300.f);
			const FVector End(1500.f + Random.FRandRange(-50.f, 50.f), 0.f, 0.f);
			const float Tension = Random.FRand();
			for (ULureFishingLineComponent* Line : Lines)
			{
				Line->SetWaterSurfaceZ(0.f);
				Line->SetTension(Tension);
				Line->SetEndpoints(Tip, End);
				Line->UpdateLine(Dt);
			}
			for (int32 Index = 1; Index < Lines.Num(); ++Index)
			{
				bSame &= Lines[Index]->GetPoints() == Lines[0]->GetPoints();
			}
		}
		TestTrue(TEXT("authority, autonomous and simulated-proxy owners draw the identical line"), bSame);
		for (ULureFishingLineComponent* Line : Lines)
		{
			Line->Hide();
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineProxyMatchesServer, "Project.Fishing.Line.QA.Net.ProxyLineMatchesServer", Flags)
	bool FQALineProxyMatchesServer::RunTest(const FString& Parameters)
	{
		// The server casts and fights; another player's copy only gets the replicated state (wire format). Its line shows the same
		// tension, starts and ends where the server's does and has the same shape.
		for (const bool bHooked : { false, true })
		{
			const FString Label = bHooked ? TEXT("Hooked (reel fight)") : TEXT("Waiting");
			FProxyRig Rig;
			if (bHooked)
			{
				if (!Rig.Make(*this) || !QAFishing::DriveToStage(*this, Rig.Scene, Rig.ServerCharacter, Rig.Server, QAFishing::EStage::Hooked))
				{
					continue;
				}
			}
			else
			{
				// A bite 60 s after landing: the bobber just floats for the whole comparison.
				if (!Rig.Make(*this, -1.f, 60.f) || !TestTrue(TEXT("QA: cast"), Rig.Server->AuthorityCast(1.f, 0.f))
					|| !TestTrue(TEXT("QA: the bobber lands"), Rig.Scene.TickUntil([&Rig]() { return Rig.Server->GetFishingState() == ELureFishingState::Waiting; }, 300)))
				{
					continue;
				}
			}
			Rig.AlignPawns();
			ReplicateFishing(*this, Rig.Server, Rig.Proxy);
			double WorstGap = 0.0;
			double WorstTension = 0.0;
			bool bEnds = true;
			float ServerPreviousTension = Rig.Server->GetLine() ? Rig.Server->GetLine()->GetTension() : 0.f;
			for (int32 Frame = 0; Frame < 180; ++Frame)
			{
				Rig.Scene.Tick(1);
				ULureFishingLineComponent* S = Rig.Server->GetLine();
				ULureFishingLineComponent* P = Rig.Proxy->GetLine();
				if (!S || !P)
				{
					AddError(Label + TEXT(": a fishing component has no line"));
					break;
				}
				if (Frame >= 120)
				{
					// The proxy simulates this frame from what the server replicated after its previous frame: it may be one frame behind.
					WorstTension = FMath::Max(WorstTension, static_cast<double>(FMath::Min(FMath::Abs(S->GetTension() - P->GetTension()), FMath::Abs(ServerPreviousTension - P->GetTension()))));
					bEnds &= S->GetStartPoint().Equals(P->GetStartPoint(), 0.5) && S->GetEndPoint().Equals(P->GetEndPoint(), bHooked ? 30.0 : 0.5);
					WorstGap = FMath::Max(WorstGap, MaxPointGap(S->GetPoints(), P->GetPoints()));
				}
				ServerPreviousTension = S->GetTension();
				ReplicateFishing(*this, Rig.Server, Rig.Proxy);
			}
			ULureFishingLineComponent* S = Rig.Server->GetLine();
			ULureFishingLineComponent* P = Rig.Proxy->GetLine();
			if (!S || !P)
			{
				continue;
			}
			AddInfo(FString::Printf(TEXT("%s: server vs proxy over the last 60 frames: worst point gap %.2f cm, worst tension gap %.4f"), *Label, WorstGap, WorstTension));
			TestTrue(Label + TEXT(": both lines are out, pinned"), S->IsLineVisible() && P->IsLineVisible() && S->GetMode() == ELureLineMode::Pinned && P->GetMode() == ELureLineMode::Pinned);
			TestTrue(FString::Printf(TEXT("%s: the proxy shows the server's tension (worst %.4f)"), *Label, WorstTension), WorstTension <= (bHooked ? 0.02 : 1.0e-4));
			TestTrue(Label + TEXT(": the proxy's line starts and ends where the server's does"), bEnds);
			// Waiting: the proxy's line started later; slack line resting on the water is history-dependent (one-sided constraints),
			// so a late start converges to nearly, not exactly, the same shape (SameInputsSameLine proves identical inputs give identical lines).
			TestTrue(FString::Printf(TEXT("%s: the same shape (worst gap %.2f cm)"), *Label, WorstGap), WorstGap <= (bHooked ? 30.0 : 10.0));
			if (!bHooked)
			{
				TestEqual(Label + TEXT(": the proxy shows WaitTension"), P->GetTension(), P->GetTuning().WaitTension);
				// T-046: the waiting line holds some tension (lifted, not lying on the water). The proxy must match the server's
				// share of inner points at the float height and never sink below it.
				const double FloatZ = P->GetTuning().FloatHeight;
				const int32 Inner = FMath::Max(1, P->GetPoints().Num() - 2);
				const int32 ServerNear = PointsNear(S->GetPoints(), FloatZ, 1.5);
				const int32 ProxyNear = PointsNear(P->GetPoints(), FloatZ, 1.5);
				TestTrue(FString::Printf(TEXT("%s: waiting, the proxy has the server's share of points at the float height (server %d, proxy %d of %d)"), *Label, ServerNear, ProxyNear, Inner),
					FMath::Abs(ServerNear - ProxyNear) <= FMath::Max(1, Inner / 10));
				TestTrue(FString::Printf(TEXT("%s: waiting, the proxy's line never sinks below the float height (lowest %.2f)"), *Label, LowestInner(P->GetPoints())),
					LowestInner(P->GetPoints()) >= FloatZ - 1.5);
			}
			else
			{
				TestTrue(Label + TEXT(": the fight is on for both"), Rig.Server->GetFightNet().bActive && Rig.Proxy->GetFightNet().bActive);
			}
			Rig.Server->AuthorityReelIn();
			ReplicateFishing(*this, Rig.Server, Rig.Proxy);
			Rig.Scene.Tick(3);
			TestFalse(Label + TEXT(": reeled in on the server: the proxy's line comes in"), P->IsLineVisible());
			TestFalse(Label + TEXT(": ... and stops ticking"), P->IsComponentTickEnabled());
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineProxySnap, "Project.Fishing.Line.QA.Net.ProxyShowsTheSnap", Flags)
	bool FQALineProxySnap::RunTest(const FString& Parameters)
	{
		// A weak line snaps on the server; the replicated Snapped result makes the other player's copy play the recoil too, then its
		// line is gone and stops ticking.
		FProxyRig Rig;
		if (!Rig.Make(*this, /*WeakLineStrength*/ 0.5f) || !QAFishing::DriveToStage(*this, Rig.Scene, Rig.ServerCharacter, Rig.Server, QAFishing::EStage::Hooked))
		{
			return false;
		}
		Rig.AlignPawns();
		ReplicateFishing(*this, Rig.Server, Rig.Proxy);
		Rig.Server->AuthoritySetReeling(true);
		bool bSnapped = false;
		for (int32 Frame = 0; Frame < 900 && !bSnapped; ++Frame)
		{
			Rig.Scene.Tick(1);
			ReplicateFishing(*this, Rig.Server, Rig.Proxy);
			bSnapped = Rig.Proxy->GetNetState().LastResult == ELureFishingResult::Snapped;
		}
		if (!TestTrue(TEXT("QA precondition: the 0.5-strength line snaps and the proxy gets the result"), bSnapped))
		{
			return false;
		}
		ULureFishingLineComponent* P = Rig.Proxy->GetLine();
		if (!TestNotNull(TEXT("the proxy has a line"), P))
		{
			return false;
		}
		TestTrue(TEXT("the proxy's line recoils"), P->IsRecoiling() && P->IsLineVisible());
		const float RecoilTime = P->GetTuning().RecoilTime;
		Rig.Scene.Tick(Frames(RecoilTime) + 3);
		TestFalse(TEXT("then it is gone on the proxy"), P->IsLineVisible() || P->IsRecoiling());
		TestFalse(TEXT("... and stops ticking"), P->IsComponentTickEnabled());
		return true;
	}

	// =================================================================================================================
	// Data validation (DT_FishingLine)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineDataRows, "Project.Fishing.Line.QA.Data.EveryRowValid", Flags)
	bool FQALineDataRows::RunTest(const FString& Parameters)
	{
		// Raw text first (a typo must not become a silent 0, T004-Q2): the header is exactly the row struct's fields, every cell is a
		// number (integers for integer fields), row names are unique; then each row is inside its fields' ClampMin/ClampMax,
		// Validate agrees, and the design relations hold.
		FString Csv;
		if (!LoadLineCsv(*this, Csv))
		{
			return false;
		}
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		QAFishing::SplitCsv(Csv, Header, Rows);
		TSet<FString> Fields;
		for (TFieldIterator<FProperty> It(FLureFishingLineRow::StaticStruct(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			Fields.Add(It->GetName());
		}
		TestTrue(TEXT("the first column is Name"), Header.Num() > 0 && Header[0] == TEXT("Name"));
		TSet<FString> Columns;
		for (int32 Index = 1; Index < Header.Num(); ++Index)
		{
			TestFalse(TEXT("no duplicate column ") + Header[Index], Columns.Contains(Header[Index]));
			Columns.Add(Header[Index]);
			TestTrue(TEXT("the column is a field of FLureFishingLineRow: ") + Header[Index], Fields.Contains(Header[Index]));
		}
		for (const FString& Field : Fields)
		{
			TestTrue(TEXT("every field has a column: ") + Field, Columns.Contains(Field));
		}
		TestTrue(TEXT("at least one row"), Rows.Num() > 0);
		TSet<FName> Names;
		for (const TArray<FString>& Cells : Rows)
		{
			if (!TestEqual(TEXT("cells per row = columns"), Cells.Num(), Header.Num()))
			{
				continue;
			}
			const FName Name(*Cells[0]);
			TestFalse(TEXT("unique row name (as FName compares) ") + Cells[0], Names.Contains(Name));
			Names.Add(Name);
			for (int32 Index = 1; Index < Header.Num(); ++Index)
			{
				const FProperty* Property = FLureFishingLineRow::StaticStruct()->FindPropertyByName(FName(*Header[Index]));
				const FString Cell = Cells[Index].TrimStartAndEnd();
				if (Property && Property->IsA<FIntProperty>())
				{
					int32 Value = 0;
					TestTrue(FString::Printf(TEXT("%s.%s is an integer ('%s')"), *Cells[0], *Header[Index], *Cell), FDefaultValueHelper::ParseInt(Cell, Value));
				}
				else
				{
					double Value = 0.0;
					TestTrue(FString::Printf(TEXT("%s.%s is a number ('%s')"), *Cells[0], *Header[Index], *Cell), FDefaultValueHelper::ParseDouble(Cell, Value) && FMath::IsFinite(Value));
				}
			}
		}
		TestTrue(TEXT("a Default row (the settings' FishingLineRow)"), Names.Contains(GetDefault<ULureFishingSettings>()->FishingLineRow));

		TStrongObjectPtr<UDataTable> Table(QAFishing::MakeTableChecked(*this, FLureFishingLineRow::StaticStruct(), Csv, TEXT("DT_FishingLine")));
		if (!Table.IsValid())
		{
			return false;
		}
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FLureFishingLineRow& R = *reinterpret_cast<const FLureFishingLineRow*>(Pair.Value);
			const FString Id = Pair.Key.ToString();
#if WITH_METADATA
			for (TFieldIterator<FNumericProperty> It(FLureFishingLineRow::StaticStruct(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const double Value = It->IsFloatingPoint() ? It->GetFloatingPointPropertyValue(It->ContainerPtrToValuePtr<void>(&R)) : static_cast<double>(It->GetSignedIntPropertyValue(It->ContainerPtrToValuePtr<void>(&R)));
				if (It->HasMetaData(TEXT("ClampMin")))
				{
					TestTrue(FString::Printf(TEXT("%s.%s = %g >= ClampMin %s"), *Id, *It->GetName(), Value, *It->GetMetaData(TEXT("ClampMin"))), Value >= FCString::Atod(*It->GetMetaData(TEXT("ClampMin"))));
				}
				if (It->HasMetaData(TEXT("ClampMax")))
				{
					TestTrue(FString::Printf(TEXT("%s.%s = %g <= ClampMax %s"), *Id, *It->GetName(), Value, *It->GetMetaData(TEXT("ClampMax"))), Value <= FCString::Atod(*It->GetMetaData(TEXT("ClampMax"))));
				}
			}
#endif
			FString Problem;
			TestTrue(FString::Printf(TEXT("%s: Validate accepts it (%s)"), *Id, *Problem), R.Validate(Problem));
			// Design relations (docs/specs/fishing-line.md "What you see").
			TestTrue(Id + TEXT(": waiting is the slackest state (WaitTension <= CastTension, < BiteTension <= HookedTension)"),
				R.WaitTension <= R.CastTension && R.WaitTension < R.BiteTension && R.BiteTension <= R.HookedTension);
			TestTrue(Id + TEXT(": HookedTension < 1 (only the snap threshold is fully straight)"), R.HookedTension < 1.f);
			TestTrue(Id + TEXT(": MaxSubsteps / SubstepRate covers a 30 fps frame in real time"), R.MaxSubsteps / R.SubstepRate >= 1.f / 30.f - 1.0e-6f);
			TestTrue(Id + TEXT(": a snapped line shrinks (RecoilLengthShare < 1)"), R.RecoilLengthShare < 1.f);
			TestTrue(Id + TEXT(": slack exists at no tension (SlackShare > 0)"), R.SlackShare > 0.f);
			TestTrue(Id + TEXT(": a line straightens within a second (LengthResponse >= 2 /s)"), R.LengthResponse >= 2.f);
			TestTrue(Id + TEXT(": the recoil is visible (RecoilTime <= 3 s, RecoilSpeed > 0)"), R.RecoilTime <= 3.f && R.RecoilSpeed > 0.f);
			TestTrue(Id + TEXT(": a hanging fish outweighs a line point (HangEndMass > 1)"), R.HangEndMass > 1.f);
			TestTrue(Id + TEXT(": TeleportDistance beyond a normal frame's move (> 500 cm)"), R.TeleportDistance > 500.f);
		}

		// The binary asset, when it exists (main after the editor-operator's import), equals the source.
		const UDataTable* Asset = GetDefault<ULureFishingSettings>()->FishingLineTable.LoadSynchronous();
		if (Asset)
		{
			TestTrue(TEXT("the imported asset uses FLureFishingLineRow"), Asset->GetRowStruct() == FLureFishingLineRow::StaticStruct());
			TestEqual(TEXT("the imported asset has the source's rows"), Asset->GetRowMap().Num(), Table->GetRowMap().Num());
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				const FLureFishingLineRow* Imported = Asset->FindRow<FLureFishingLineRow>(Pair.Key, TEXT("QA"), false);
				if (TestNotNull(TEXT("imported row ") + Pair.Key.ToString(), Imported))
				{
					const TArray<FString> Diff = QAFishing::DifferentFields(FLureFishingLineRow::StaticStruct(), Imported, Pair.Value);
					TestEqual(FString::Printf(TEXT("imported row %s equals the source (differs: %s)"), *Pair.Key.ToString(), *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
				}
			}
		}
		else
		{
			AddInfo(TEXT("/Game/Data/DT_FishingLine is not imported here (lane): the asset comparison is skipped; the lead confirms it in main."));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineValidateBoundaries, "Project.Fishing.Line.QA.Data.ValidateMatchesFieldRanges", Flags)
	bool FQALineValidateBoundaries::RunTest(const FString& Parameters)
	{
		// The header says "Units and ranges: the header comments; Validate enforces them". For every numeric field: its ClampMin and
		// ClampMax are accepted, just outside them is refused (with a reason), and NaN/Inf are refused.
#if WITH_METADATA
		const FLureFishingLineRow Base = Row();
		FString Problem;
		if (!TestTrue(TEXT("the built-in row is valid"), Base.Validate(Problem)))
		{
			return false;
		}
		int32 Checked = 0;
		for (TFieldIterator<FNumericProperty> It(FLureFishingLineRow::StaticStruct(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const FNumericProperty* Property = *It;
			const FString Name = Property->GetName();
			auto With = [Property](const FLureFishingLineRow& From, double Value)
			{
				FLureFishingLineRow Copy = From;
				void* Ptr = Property->ContainerPtrToValuePtr<void>(&Copy);
				if (Property->IsFloatingPoint())
				{
					Property->SetFloatingPointPropertyValue(Ptr, Value);
				}
				else
				{
					Property->SetIntPropertyValue(Ptr, static_cast<int64>(Value));
				}
				return Copy;
			};
			auto Check = [this, &Name, &With, &Base](double Value, bool bExpected, const TCHAR* What)
			{
				const FLureFishingLineRow Copy = With(Base, Value);
				FString Why;
				const bool bValid = Copy.Validate(Why);
				TestEqual(FString::Printf(TEXT("%s = %g (%s): %s"), *Name, Value, What, bExpected ? TEXT("accepted") : TEXT("refused")), bValid, bExpected);
				if (!bExpected && !bValid)
				{
					TestFalse(FString::Printf(TEXT("%s = %g: a reason is given"), *Name, Value), Why.IsEmpty());
				}
			};
			const bool bInt = !Property->IsFloatingPoint();
			if (Property->HasMetaData(TEXT("ClampMin")))
			{
				const double Min = FCString::Atod(*Property->GetMetaData(TEXT("ClampMin")));
				Check(Min, true, TEXT("ClampMin"));
				Check(bInt ? Min - 1.0 : Min - FMath::Max(1.0e-3, FMath::Abs(Min) * 1.0e-3), false, TEXT("just under ClampMin"));
				++Checked;
			}
			if (Property->HasMetaData(TEXT("ClampMax")))
			{
				const double Max = FCString::Atod(*Property->GetMetaData(TEXT("ClampMax")));
				Check(Max, true, TEXT("ClampMax"));
				Check(bInt ? Max + 1.0 : Max + FMath::Max(1.0e-3, FMath::Abs(Max) * 1.0e-3), false, TEXT("just over ClampMax"));
			}
			if (!bInt)
			{
				Check(std::numeric_limits<double>::quiet_NaN(), false, TEXT("NaN"));
				Check(std::numeric_limits<double>::infinity(), false, TEXT("+Inf"));
			}
		}
		TestTrue(FString::Printf(TEXT("QA: every field has a documented minimum (%d checked)"), Checked), Checked >= 20);
#else
		AddInfo(TEXT("No metadata in this build: skipped."));
#endif
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineTuningSource, "Project.Fishing.Line.QA.Data.TuningComesFromTheTable", Flags)
	bool FQALineTuningSource::RunTest(const FString& Parameters)
	{
		// A fresh component resolves its tuning from the settings: the imported DT_FishingLine row when it exists, else the built-in
		// row (which equals the source's Default row), without warnings (the automation run fails on warnings).
		FString Csv;
		if (!LoadLineCsv(*this, Csv))
		{
			return false;
		}
		TStrongObjectPtr<UDataTable> Table(QAFishing::MakeTableChecked(*this, FLureFishingLineRow::StaticStruct(), Csv, TEXT("DT_FishingLine")));
		const FLureFishingLineRow* Source = Table.IsValid() ? Table->FindRow<FLureFishingLineRow>(GetDefault<ULureFishingSettings>()->FishingLineRow, TEXT("QA"), false) : nullptr;
		if (!TestNotNull(TEXT("the source has the settings' row"), Source))
		{
			return false;
		}
		ULureFishingLineComponent* Line = NewObject<ULureFishingLineComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		const FLureFishingLineRow Resolved = Line->GetTuning();
		const bool bAsset = GetDefault<ULureFishingSettings>()->FishingLineTable.LoadSynchronous() != nullptr;
		TestEqual(TEXT("fallback tuning exactly when the table asset is missing"), Line->IsUsingFallbackTuning(), !bAsset);
		const TArray<FString> Diff = QAFishing::DifferentFields(FLureFishingLineRow::StaticStruct(), &Resolved, Source);
		TestEqual(FString::Printf(TEXT("the resolved tuning equals the source row (differs: %s)"), *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
		const FLureFishingLineRow Fallback = FLureFishingLineRules::GetFallbackRow();
		const TArray<FString> FallbackDiff = QAFishing::DifferentFields(FLureFishingLineRow::StaticStruct(), &Fallback, Source);
		TestEqual(FString::Printf(TEXT("the built-in row equals the source row (differs: %s)"), *FString::Join(FallbackDiff, TEXT(", "))), FallbackDiff.Num(), 0);
		return true;
	}

	// =================================================================================================================
	// Allocations
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineAllocFree, "Project.Fishing.Line.QA.Alloc.SteadyStateEveryMode", Flags)
	bool FQALineAllocFree::RunTest(const FString& Parameters)
	{
		// After Init: pinned with water and changing tension, teleport resets, NaN inputs, a hanging end with kicks and a snap recoil
		// allocate nothing in the solver; the component's per-frame API (incl. the legacy SetLine, the water lookup cache and
		// ClearWaterSurfaceZ as a line off the water uses it) allocates nothing for an undrawn line.
		const FLureFishingLineRow Tuning = Row();
		FLureLineSim Sim;
		Sim.Init(24);
		const FVector Tip(0.f, 0.f, 300.f);
		Sim.Step(Dt, PinnedInput(Tip, FVector(1500.f, 0.f, 0.f), 1600.f), Tuning);
		const FVector* Data = Sim.GetPoints().GetData();
		int32 Count = 0;
		const bool bCounted = CountGameThreadAllocs([&]()
		{
			for (int32 Frame = 0; Frame < 900; ++Frame)
			{
				FVector End(1500.f + 5.f * (Frame % 40), 0.f, 0.f);
				FVector Start = Tip;
				if (Frame % 150 == 75)
				{
					Start += FVector(30000.f, 0.f, 0.f); // teleport: reset
				}
				if (Frame % 97 == 0)
				{
					End = FVector(NaN);
				}
				FLureLineSimInput In = PinnedInput(Start, End, 1500.f + (Frame % 100));
				In.bHasWater = Frame % 2 == 0;
				In.Float = FLureFishingLineRules::FloatAmount((Frame % 10) / 10.f, Tuning);
				In.bFreeEnd = Frame >= 700;
				In.EndMass = Frame >= 800 ? Tuning.HangEndMass : 1.f;
				if (Frame == 700)
				{
					Sim.ReleaseEnd(1.f);
					Sim.AddRecoil(Tuning.RecoilSpeed);
				}
				Sim.Step((Frame % 3) ? Dt : 1.f / 25.f, In, Tuning);
				if (Frame >= 800)
				{
					Sim.AddEndVelocity(FVector(20.f, 0.f, 0.f));
				}
			}
			volatile double Sink = Sim.GetPolylineLength() + Sim.GetEndDirection().X + Sim.GetEndVelocity().Y;
			(void)Sink;
		}, Count);
		if (bCounted)
		{
			TestEqual(TEXT("the solver allocates nothing in 900 frames of every mode"), Count, 0);
		}
		else
		{
			AddInfo(TEXT("The game-thread allocation hook is not active in this build: only the buffer check runs."));
		}
		TestTrue(TEXT("the solver's point buffer never moved"), Sim.GetPoints().GetData() == Data);

		FQALineWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		W.AddWater(FVector2D(1500.f, 0.f), 0.f, 2000.f);
		ULureFishingLineComponent* Line = W.Line;
		Line->ClearWaterSurfaceZ();
		Hold(Line, Tip, FVector(1500.f, 0.f, 0.f), 0.5f); // first water lookup happens outside the counted window
		int32 LineCount = 0;
		const bool bLineCounted = CountGameThreadAllocs([&]()
		{
			for (int32 Frame = 0; Frame < 600; ++Frame)
			{
				const FVector End(1500.f + 100.f * FMath::Sin(Frame * 0.05f), 50.f, 0.f); // stays within WaterRefreshDistance
				switch (Frame % 4)
				{
				case 0:
					Line->SetLine(Tip, End, 0.08f, FVector(-50.f, 0.f, 320.f), 90.f, 2.5f, 1920.f, 0.15f);
					break;
				case 1:
					Line->SetViewer(FVector(-50.f, 0.f, 320.f), 90.f);
					Line->SetWidthRule(2.5f, 1920.f, 0.15f);
					Line->SetTension((Frame % 20) / 19.f);
					Line->SetSlack(-1.f);
					Line->SetEndpoints(Tip, End);
					break;
				case 2:
					Line->SetSlack(0.1f);
					Line->SetEndpoints(Tip, End);
					break;
				default:
					Line->ClearWaterSurfaceZ();
					Line->SetEndpoints(Tip, End);
					break;
				}
				Line->UpdateLine(Dt);
			}
			volatile double Sink = Line->GetEndPoint().X + Line->GetEndDirection().Z + Line->GetStartPoint().Y;
			(void)Sink;
		}, LineCount);
		if (bLineCounted)
		{
			TestEqual(TEXT("the component's per-frame API allocates nothing (undrawn line, 600 frames)"), LineCount, 0);
		}
		Line->Hide();
		return true;
	}

	// =================================================================================================================
	// Back-compat (T-006 API)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQALineLegacyApi, "Project.Fishing.Line.QA.Compat.SetLineAndHideStillWork", Flags)
	bool FQALineLegacyApi::RunTest(const FString& Parameters)
	{
		// Older callers call SetLine(Start, End, Sag, View, Fov, PixelWidth, ReferenceWidth, MinWidth) every frame and Hide() when done.
		// Spec: Sag becomes slack (a line sagging Sag x length is about 8/3 x Sag^2 longer); the width rule and viewer apply.
		FQALineWorld W;
		if (!W.Create(*this, /*bMesh*/ true))
		{
			return false;
		}
		const FVector Start(0.f, 0.f, 1000.f);
		const FVector End(1500.f, 0.f, 1000.f);
		const FVector Eye(-50.f, 0.f, 1020.f);
		auto Drive = [&W, &Start, &End, &Eye](float Sag, float Seconds)
		{
			for (int32 Frame = 0; Frame < Frames(Seconds); ++Frame)
			{
				W.Line->SetLine(Start, End, Sag, Eye, 90.f, 2.5f, 1920.f, 0.15f);
				W.Tick(1);
			}
		};
		TArray<double> Sags;
		for (const float Sag : { 0.f, 0.05f, 0.1f, 0.2f })
		{
			Drive(Sag, 4.f);
			const TArray<FVector>& P = W.Line->GetPoints();
			const FString Label = FString::Printf(TEXT("SetLine sag %.2f"), Sag);
			TestTrue(Label + TEXT(": the line is out, pinned"), W.Line->IsLineVisible() && W.Line->GetMode() == ELureLineMode::Pinned);
			TestTrue(Label + TEXT(": 13 points, 13 widths, 12 segments"), P.Num() == 13 && W.Line->GetWidths().Num() == 13 && W.Line->GetNumSegments() == 12);
			TestTrue(Label + TEXT(": starts at Start, ends at End"), P[0].Equals(Start, 0.01) && P.Last().Equals(End, 0.01));
			const double Expected = 1500.0 * (1.0 + 8.0 / 3.0 * Sag * Sag);
			TestTrue(FString::Printf(TEXT("%s: length %.1f = chord x (1 + 8/3 sag^2) = %.1f"), *Label, W.Line->GetRestLength(), Expected), FMath::IsNearlyEqual(static_cast<double>(W.Line->GetRestLength()), Expected, 0.005 * 1500.0));
			const double Mid = Deviation(P);
			Sags.Add(Mid);
			if (Sag > 0.f)
			{
				TestTrue(FString::Printf(TEXT("%s: sags about sag x length (%.1f vs %.1f cm, 25 %%)"), *Label, Mid, Sag * 1500.0), FMath::Abs(Mid - Sag * 1500.0) <= 0.25 * Sag * 1500.0);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: straight (%.2f cm)"), *Label, Mid), Mid < 1.0);
			}
			bool bWide = true;
			for (int32 Index = 0; Index < P.Num(); ++Index)
			{
				bWide &= FLureFishingRules::LinePixelsAtDistance(W.Line->GetWidths()[Index], static_cast<float>(FVector::Dist(Eye, P[Index])), 90.f, 1920.f) >= 2.5f - 0.01f
					&& W.Line->GetWidths()[Index] >= 0.15f - 1.0e-4f;
			}
			TestTrue(Label + TEXT(": every point at least the pixel width and the minimum width"), bWide);
		}
		TestTrue(TEXT("more sag, more droop"), Sags[0] < Sags[1] && Sags[1] < Sags[2] && Sags[2] < Sags[3]);

		W.Line->Hide();
		TestFalse(TEXT("Hide: gone"), W.Line->IsLineVisible());
		TestFalse(TEXT("Hide: no tick"), W.Line->IsComponentTickEnabled());
		W.Tick(5);
		TestFalse(TEXT("Hide: stays gone"), W.Line->IsLineVisible());
		Drive(0.1f, 0.1f);
		TestTrue(TEXT("SetLine after Hide starts a new line at Start"), W.Line->IsLineVisible() && W.Line->GetPoints()[0].Equals(Start, 0.01));
		for (const float Bad : { NaN, -0.5f, Inf })
		{
			Drive(Bad, 0.5f);
			TestTrue(FString::Printf(TEXT("SetLine sag %f: finite and pinned"), Bad), AllFinite(W.Line->GetPoints()) && W.Line->GetPoints().Last().Equals(End, 0.01) && FMath::IsFinite(W.Line->GetRestLength()));
		}
		W.Line->Hide();
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

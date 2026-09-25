// T-061 (unreal-engineer): a hanging fish's own wiggle moves the line. The line reads the body centre from the pose the fish
// is drawn with (a skinned mesh's bones, else a primitive's bounds), relative to the mouth, and pushes its free end against
// the change in that sideways speed x DT_FishingLine HangWiggleCoupling. Test poses only (no art): a moving primitive and an
// engine skeletal cube posed bone by bone. Project.Fishing.Line.Hang.Wiggle.*  Spec: docs/specs/fishing-line.md.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Fishing/FishingLineTypes.h"
#include "Fishing/LureFishingLineComponent.h"
#include "GameFramework/Actor.h"
#include "Templates/Function.h"
#include "Tests/AutomationCommon.h"
#include <limits>

#if MALLOC_GT_HOOKS
// Core's game-thread allocation hook (UnrealMemory.cpp; STATS builds): called with 0 = malloc, 1 = realloc, 2 = free.
extern CORE_API TFunction<void(int32)>* GGameThreadMallocHook;
#endif

namespace LureLineHangWiggleTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float FrameDt = 1.0f / 60.0f;
	/** The rod tip, and the hang: 100 cm of line, the mouth 20 cm ahead of the fish's pivot (+X, up the line). */
	const FVector Tip(0.0, 0.0, 300.0);
	constexpr float HangLength = 100.0f;
	const FVector Mouth(20.0, 0.0, 0.0);
	/** Where the test body centre rests, actor space (toward the tail, below the mouth). */
	const FVector BodyRest(-25.0, 0.0, 0.0);

	enum class EPose : uint8 { Primitive, Skinned };

	/** A line (no mesh: simulated, not drawn) with a test fish hanging from it; the fish's body is moved by hand each frame. */
	struct FRig
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ULureFishingLineComponent* Line = nullptr;
		AActor* Fish = nullptr;
		UStaticMeshComponent* Body = nullptr;
		UPoseableMeshComponent* Skeleton = nullptr;
		FName Bone;

		bool Create(FAutomationTestBase& Test, EPose Pose, float Coupling, const FVector& BodyOffset = BodyRest)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			AActor* Owner = Spawn(FVector::ZeroVector);
			Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("WiggleLine"), RF_Transient);
			Line->SetupAttachment(Owner->GetRootComponent());
			Line->RegisterComponent();
			Line->Setup(nullptr, nullptr, FLinearColor::White, 12);
			FLureFishingLineRow Row = FLureFishingLineRules::GetFallbackRow();
			Row.HangWiggleCoupling = Coupling;
			if (!Test.TestTrue(TEXT("the tuning is valid"), Line->SetTuning(Row)))
			{
				return false;
			}

			Fish = Spawn(Tip - FVector(0.0, 0.0, HangLength) - Mouth);
			if (Pose == EPose::Primitive)
			{
				Body = NewObject<UStaticMeshComponent>(Fish, TEXT("Body"));
				Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Body->SetMobility(EComponentMobility::Movable);
				Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Body->SetupAttachment(Fish->GetRootComponent());
				Body->SetRelativeScale3D(FVector(0.3, 0.05, 0.1));
				Body->RegisterComponent();
				Body->SetRelativeLocation(BodyOffset);
			}
			else
			{
				USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
				if (!Test.TestNotNull(TEXT("the engine's SkeletalCube loads"), Mesh))
				{
					return false;
				}
				Skeleton = NewObject<UPoseableMeshComponent>(Fish, TEXT("Skeleton"));
				Skeleton->SetSkinnedAssetAndUpdate(Mesh);
				Skeleton->SetupAttachment(Fish->GetRootComponent());
				Skeleton->RegisterComponent();
				Bone = Skeleton->GetBoneName(0);
				if (!Test.TestTrue(TEXT("the skeletal cube has a posed bone"), !Bone.IsNone() && Skeleton->GetNumComponentSpaceTransforms() > 0))
				{
					return false;
				}
				SetBody(BodyOffset);
			}
			Line->SetEndpoints(Tip, Tip - FVector(0.0, 0.0, HangLength)); // a straight line out, then the fish is hung on it
			Line->AttachEndActor(Fish, HangLength, Mouth, /*bOrientAlongLine*/ true);
			return Test.TestTrue(TEXT("the fish hangs"), Line->GetEndActor() == Fish);
		}

		AActor* Spawn(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Root->SetMobility(EComponentMobility::Movable);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocation(Location);
			return Actor;
		}

		/** Poses the body: its centre at Offset (actor space, relative to the fish's root). */
		void SetBody(const FVector& Offset)
		{
			if (Body)
			{
				Body->SetRelativeLocation(Offset);
			}
			if (Skeleton)
			{
				// Component space = actor space here (the skeleton sits on the root): the single-bone cube's bone is its centre.
				Skeleton->SetBoneLocationByName(Bone, Offset, EBoneSpaces::ComponentSpace);
				Skeleton->RefreshBoneTransforms();
			}
		}

		FVector MidPoint() const
		{
			const TArray<FVector>& Points = Line->GetPoints();
			return Points[Points.Num() / 2];
		}
	};

	/** Amplitude (cm) of the Hz component of Samples taken every FrameDt (a single DFT bin; exact over whole periods). */
	double AmplitudeAt(const TArray<double>& Samples, double Hz)
	{
		double Re = 0.0;
		double Im = 0.0;
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			const double Phase = 2.0 * UE_DOUBLE_PI * Hz * Index * FrameDt;
			Re += Samples[Index] * FMath::Cos(Phase);
			Im -= Samples[Index] * FMath::Sin(Phase);
		}
		return 2.0 * FMath::Sqrt(Re * Re + Im * Im) / FMath::Max(1, Samples.Num());
	}

	struct FWiggleRun
	{
		/** Sideways samples (cm, along the fish's side) of the line's middle point and its end over the measured whole periods. */
		TArray<double> Mid;
		TArray<double> End;
		double MidPeakToPeak = 0.0;
		double MidMaxFromRest = 0.0;
	};

	/**
	 *  1 s at rest, then the body wiggles sideways (along the fish's +Y) by Amplitude cm at Hz for 1 s of lead-in and
	 *  MeasureSeconds measured (a whole number of periods). Everything at a fixed FrameDt; the line is stepped directly.
	 */
	FWiggleRun Run(FRig& Rig, double Amplitude, double Hz, double MeasureSeconds)
	{
		FWiggleRun Out;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Rig.SetBody(BodyRest);
			Rig.Line->UpdateLine(FrameDt);
		}
		const FVector Side = Rig.Fish->GetActorQuat().GetAxisY();
		const FVector MidRest = Rig.MidPoint();
		const FVector EndRest = Rig.Line->GetEndPoint();
		const int32 Lead = 60;
		const int32 Measured = FMath::RoundToInt(MeasureSeconds / FrameDt);
		double Low = TNumericLimits<double>::Max();
		double High = -TNumericLimits<double>::Max();
		for (int32 Frame = 1; Frame <= Lead + Measured; ++Frame)
		{
			Rig.SetBody(BodyRest + FVector(0.0, Amplitude * FMath::Sin(2.0 * UE_DOUBLE_PI * Hz * Frame * FrameDt), 0.0));
			Rig.Line->UpdateLine(FrameDt);
			const double Mid = FVector::DotProduct(Rig.MidPoint() - MidRest, Side);
			Out.MidMaxFromRest = FMath::Max(Out.MidMaxFromRest, FVector::Dist(Rig.MidPoint(), MidRest));
			if (Frame > Lead)
			{
				Out.Mid.Add(Mid);
				Out.End.Add(FVector::DotProduct(Rig.Line->GetEndPoint() - EndRest, Side));
				Low = FMath::Min(Low, Mid);
				High = FMath::Max(High, Mid);
			}
		}
		Out.MidPeakToPeak = High - Low;
		return Out;
	}

	/** The wiggle's frequency dominates the middle point's motion: its amplitude beats every probe frequency by Ratio. */
	bool FollowsFrequency(FAutomationTestBase& Test, const TCHAR* What, const FWiggleRun& Result, double Hz, double Ratio)
	{
		const double AtWiggle = AmplitudeAt(Result.Mid, Hz);
		const double Probes[] = { 0.25, 0.5, 0.75, Hz * 0.5, Hz * 1.5, Hz * 2.0, Hz * 3.0 };
		double Strongest = 0.0;
		double StrongestHz = 0.0;
		for (const double Probe : Probes)
		{
			const double Amplitude = AmplitudeAt(Result.Mid, Probe);
			if (Amplitude > Strongest)
			{
				Strongest = Amplitude;
				StrongestHz = Probe;
			}
		}
		return Test.TestTrue(FString::Printf(TEXT("%s: the middle moves at the wiggle's %.2f Hz (%.2f cm) %.0fx more than at any other probed frequency (%.3f cm at %.2f Hz)"),
			What, Hz, AtWiggle, Ratio, Strongest, StrongestHz), AtWiggle >= Ratio * Strongest);
	}

	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiggleData, "Project.Fishing.Line.Hang.Wiggle.Data", Flags)
	bool FWiggleData::RunTest(const FString& Parameters)
	{
		const FLureFishingLineRow Row = FLureFishingLineRules::GetFallbackRow();
		TestEqual(TEXT("shipped HangWiggleCoupling is 1 (physical: the body centre stays put)"), Row.HangWiggleCoupling, 1.0f);
		auto Valid = [](float Coupling)
		{
			FLureFishingLineRow Copy = FLureFishingLineRules::GetFallbackRow();
			Copy.HangWiggleCoupling = Coupling;
			FString Problem;
			return Copy.Validate(Problem);
		};
		TestTrue(TEXT("0 (off) is valid"), Valid(0.0f));
		TestTrue(TEXT("10 is valid"), Valid(10.0f));
		TestFalse(TEXT("negative is refused"), Valid(-0.1f));
		TestFalse(TEXT("over 10 is refused"), Valid(10.1f));
		TestFalse(TEXT("NaN is refused"), Valid(std::numeric_limits<float>::quiet_NaN()));
		TestFalse(TEXT("+Inf is refused"), Valid(std::numeric_limits<float>::infinity()));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiggleMovesTheLine, "Project.Fishing.Line.Hang.Wiggle.MovesTheLine", Flags)
	bool FWiggleMovesTheLine::RunTest(const FString& Parameters)
	{
		// A 4 cm sideways wiggle at 2 Hz (a primitive body), 4 s measured = 8 whole periods.
		constexpr double Amplitude = 4.0;
		constexpr double Hz = 2.0;
		{
			FRig Rig;
			if (!Rig.Create(*this, EPose::Primitive, 1.0f))
			{
				return false;
			}
			const FWiggleRun Result = Run(Rig, Amplitude, Hz, 4.0);
			const double Mid = AmplitudeAt(Result.Mid, Hz);
			const double End = AmplitudeAt(Result.End, Hz);
			TestTrue(FString::Printf(TEXT("coupling 1: the line's middle moves sideways >= 1 cm at the wiggle's frequency (%.2f cm; peak to peak %.2f cm)"),
				Mid, Result.MidPeakToPeak), Mid >= 1.0);
			TestTrue(FString::Printf(TEXT("coupling 1: the end moves about as far as the body, the other way (%.2f cm for a %.1f cm wiggle)"), End, Amplitude),
				End >= 0.5 * Amplitude && End <= 1.5 * Amplitude);
			FollowsFrequency(*this, TEXT("coupling 1"), Result, Hz, 3.0);
			AddInfo(FString::Printf(TEXT("Wiggle.MovesTheLine (4 cm at 2 Hz, coupling 1): end %.2f cm, middle %.2f cm (peak to peak %.2f)"), End, Mid, Result.MidPeakToPeak));
		}
		{
			FRig Rig;
			if (!Rig.Create(*this, EPose::Primitive, 0.0f))
			{
				return false;
			}
			const FWiggleRun Result = Run(Rig, Amplitude, Hz, 4.0);
			TestTrue(FString::Printf(TEXT("coupling 0: the middle doesn't move (<= 0.01 cm from rest; %.5f cm)"), Result.MidMaxFromRest), Result.MidMaxFromRest <= 0.01);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiggleSkinnedPose, "Project.Fishing.Line.Hang.Wiggle.SkinnedPose", Flags)
	bool FWiggleSkinnedPose::RunTest(const FString& Parameters)
	{
		// The pose read from a skinned mesh's bones (what the real fish is drawn with), another frequency: 3 Hz, 4 s = 12 periods.
		constexpr double Amplitude = 3.0;
		constexpr double Hz = 3.0;
		FRig Rig;
		if (!Rig.Create(*this, EPose::Skinned, 1.0f))
		{
			return false;
		}
		const FWiggleRun Result = Run(Rig, Amplitude, Hz, 4.0);
		const double Mid = AmplitudeAt(Result.Mid, Hz);
		TestTrue(FString::Printf(TEXT("the line's middle moves sideways >= 0.5 cm at the wiggle's frequency (%.2f cm)"), Mid), Mid >= 0.5);
		FollowsFrequency(*this, TEXT("skinned"), Result, Hz, 3.0);
		AddInfo(FString::Printf(TEXT("Wiggle.SkinnedPose (3 cm at 3 Hz): end %.2f cm, middle %.2f cm"), AmplitudeAt(Result.End, Hz), Mid));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiggleTurningIsNotAWiggle, "Project.Fishing.Line.Hang.Wiggle.TurningIsNotAWiggle", Flags)
	bool FWiggleTurningIsNotAWiggle::RunTest(const FString& Parameters)
	{
		// The fish turns its side to a viewer walking around it (T-043) with its body off the line's axis: its body moves in the
		// world, but not relative to the mouth, so the line must not move.
		FRig Rig;
		if (!Rig.Create(*this, EPose::Primitive, 1.0f, BodyRest + FVector(0.0, 6.0, 0.0)))
		{
			return false;
		}
		Rig.Line->DetachEndActor();
		Rig.Line->AttachEndActor(Rig.Fish, HangLength, Mouth, /*bOrientAlongLine*/ true, /*bFaceViewer*/ true);
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Rig.Line->SetViewer(Tip + FVector(400.0, 0.0, -100.0), 90.0f);
			Rig.Line->UpdateLine(FrameDt);
		}
		const FVector MidRest = Rig.MidPoint();
		double Worst = 0.0;
		double Turned = 0.0;
		const FVector StartSide = Rig.Fish->GetActorQuat().GetAxisY();
		for (int32 Frame = 1; Frame <= 240; ++Frame)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Frame / 240.0; // once around in 4 s
			Rig.Line->SetViewer(Tip + FVector(400.0 * FMath::Cos(Angle), 400.0 * FMath::Sin(Angle), -100.0), 90.0f);
			Rig.Line->UpdateLine(FrameDt);
			Worst = FMath::Max(Worst, FVector::Dist(Rig.MidPoint(), MidRest));
			Turned = FMath::Max(Turned, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Rig.Fish->GetActorQuat().GetAxisY(), StartSide), -1.0, 1.0))));
		}
		TestTrue(FString::Printf(TEXT("the fish turned (%.0f deg)"), Turned), Turned > 90.0);
		TestTrue(FString::Printf(TEXT("its turning doesn't move the line (middle <= 0.05 cm from rest; %.4f cm)"), Worst), Worst <= 0.05);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiggleAllocations, "Project.Fishing.Line.Hang.Wiggle.Allocations", Flags)
	bool FWiggleAllocations::RunTest(const FString& Parameters)
	{
#if MALLOC_GT_HOOKS
		FRig Rig;
		if (!Rig.Create(*this, EPose::Skinned, 1.0f))
		{
			return false;
		}
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Rig.SetBody(BodyRest + FVector(0.0, 2.0 * FMath::Sin(Frame * 0.4), 0.0));
			Rig.Line->UpdateLine(FrameDt);
		}
		int32 Count = 0;
		TFunction<void(int32)> Hook([&Count](int32) { ++Count; });
		{
			TGuardValue<TFunction<void(int32)>*> Guard(GGameThreadMallocHook, &Hook);
			void* Probe = FMemory::Malloc(64);
			FMemory::Free(Probe);
		}
		if (Count == 0)
		{
			AddInfo(TEXT("The game-thread allocation hook is not active in this build: skipped."));
			return true;
		}
		int32 Total = 0;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Rig.SetBody(BodyRest + FVector(0.0, 2.0 * FMath::Sin((30 + Frame) * 0.4), 0.0)); // the test's posing is not counted
			Count = 0;
			{
				TGuardValue<TFunction<void(int32)>*> Guard(GGameThreadMallocHook, &Hook);
				Rig.Line->UpdateLine(FrameDt);
			}
			Total += Count;
		}
		TestEqual(TEXT("a hanging, wiggling fish's line allocates nothing per frame (120 frames)"), Total, 0);
#else
		AddInfo(TEXT("No game-thread allocation hook in this build: skipped."));
#endif
		return true;
	}
}

#endif

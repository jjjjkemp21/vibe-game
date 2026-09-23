// Lure T-026 crash fix: ALureWaterVolume in EDITOR worlds, placed the way the level builder places it
// (Content/Python/levels/build_level.py: EditorActorSubsystem.spawn_actor_from_class -> the editor's volume actor factory
// for our class -> UActorFactory::CreateBrushForVolumeActor -> FBSPOps::csgPrepMovingBrush, which asserted
// `Actor->Brush` because the volume used to drop its brush while the factory was building it).
// Spec: docs/specs/swimming.md, "Water volume and the editor". Tests: Project.Movement.Swim.WaterVolumeEditor.*
// A regression here crashes the test run (an assert, not a failed check): tools/run-tests.ps1 reports that as a failure.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "ActorFactories/ActorFactory.h"
#include "ActorFactories/ActorFactoryVolume.h"
#include "AssetRegistry/AssetData.h"
#include "Character/LureWaterVolume.h"
#include "Components/BrushComponent.h"
#include "Editor.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Engine/BrushBuilder.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Model.h"
#include "PhysicsEngine/BodySetup.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/PlacementSubsystem.h"
#include "Tests/AutomationCommon.h"
#include <limits>

namespace LureWaterVolumeEditorTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The water box a volume should be: surface center (world), yaw, half size and depth. */
	struct FWaterBox
	{
		FVector SurfaceCenter = FVector::ZeroVector;
		double Yaw = 0.0;
		FVector2D HalfSize = FVector2D(5000.0, 5000.0);
		double Depth = 1000.0;
	};

	/** What Python's set_editor_property does (build_level._size_water): PreEditChange, the value, PostEditChangeProperty. */
	template <typename TValue>
	void SetEditorProperty(ALureWaterVolume* Volume, FName PropertyName, TValue& Member, const TValue& Value)
	{
		FProperty* Property = ALureWaterVolume::StaticClass()->FindPropertyByName(PropertyName);
		Volume->PreEditChange(Property);
		Member = Value;
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		Volume->PostEditChangeProperty(Event);
	}

	/** build_level._size_water on a spawned volume. */
	void SizeLikeTheBuilder(ALureWaterVolume* Volume, const FVector2D& HalfSize, float Depth)
	{
		SetEditorProperty(Volume, GET_MEMBER_NAME_CHECKED(ALureWaterVolume, SurfaceHalfSize), Volume->SurfaceHalfSize, HalfSize);
		SetEditorProperty(Volume, GET_MEMBER_NAME_CHECKED(ALureWaterVolume, WaterDepth), Volume->WaterDepth, Depth);
		Volume->SetWaterSize(HalfSize, Depth);
	}

	/**
	 *  One consistent water box: the collision body is our box (not the editor's uncooked convex hulls of the brush), the
	 *  engine's volume test answers from it, the bounds (the engine's quick physics-volume test) match it, and in editor
	 *  worlds the brush exists and its 6 faces are the same box, facing out.
	 */
	void CheckWaterBox(FAutomationTestBase& Test, const FString& Step, ALureWaterVolume* Volume, const FWaterBox& Want, bool bExpectBrush)
	{
		const FString P = Step + TEXT(": ");
		if (!Test.TestNotNull(*(P + TEXT("the volume exists")), Volume))
		{
			return;
		}
		const double HX = Want.HalfSize.X;
		const double HY = Want.HalfSize.Y;
		const double Depth = Want.Depth;
		UBrushComponent* Comp = Volume->GetBrushComponent();
		if (!Test.TestNotNull(*(P + TEXT("brush component")), Comp))
		{
			return;
		}

		Test.TestTrue(*(P + TEXT("actor at the surface center")), Volume->GetActorLocation().Equals(Want.SurfaceCenter, 0.5));
		Test.TestNearlyEqual(*(P + TEXT("actor yaw")), Volume->GetActorRotation().Yaw, Want.Yaw, 0.01);
		Test.TestNearlyEqual(*(P + TEXT("surface height")), static_cast<double>(Volume->GetSurfaceHeight()), Want.SurfaceCenter.Z, 0.5);

		// Collision body.
		const UBodySetup* Body = Comp->BrushBodySetup;
		if (Test.TestNotNull(*(P + TEXT("collision body")), Body))
		{
			Test.TestEqual(*(P + TEXT("the body is one box")), Body->AggGeom.BoxElems.Num(), 1);
			Test.TestEqual(*(P + TEXT("no convex hulls in the body (the editor's, never cooked here)")), Body->AggGeom.ConvexElems.Num(), 0);
			if (Body->AggGeom.BoxElems.Num() == 1)
			{
				const FKBoxElem& Box = Body->AggGeom.BoxElems[0];
				Test.TestNearlyEqual(*(P + TEXT("box size X")), static_cast<double>(Box.X), 2.0 * HX, 0.1);
				Test.TestNearlyEqual(*(P + TEXT("box size Y")), static_cast<double>(Box.Y), 2.0 * HY, 0.1);
				Test.TestNearlyEqual(*(P + TEXT("box size Z = depth")), static_cast<double>(Box.Z), Depth, 0.1);
				Test.TestTrue(*(P + TEXT("box top at the actor's origin")), Box.Center.Equals(FVector(0.0, 0.0, -0.5 * Depth), 0.1));
			}
		}

		// The engine's volume test (the character's physics-volume check), from the body's geometry.
		const FTransform ToWorld = Volume->GetActorTransform();
		auto At = [&ToWorld](double X, double Y, double Z) { return ToWorld.TransformPosition(FVector(X, Y, Z)); };
		Test.TestTrue(*(P + TEXT("water just under the surface")), Volume->EncompassesPoint(At(0.0, 0.0, -10.0)));
		Test.TestTrue(*(P + TEXT("water near a bottom corner")), Volume->EncompassesPoint(At(0.9 * HX, -0.9 * HY, -0.95 * Depth)));
		Test.TestFalse(*(P + TEXT("dry just above the surface")), Volume->EncompassesPoint(At(0.0, 0.0, 10.0)));
		Test.TestFalse(*(P + TEXT("dry just below the bottom")), Volume->EncompassesPoint(At(0.0, 0.0, -Depth - 10.0)));
		Test.TestFalse(*(P + TEXT("dry just outside in X")), Volume->EncompassesPoint(At(HX + 10.0, 0.0, -10.0)));
		Test.TestFalse(*(P + TEXT("dry just outside in Y")), Volume->EncompassesPoint(At(0.0, HY + 10.0, -10.0)));
		Test.TestTrue(*(P + TEXT("IsPointInWater agrees")), Volume->IsPointInWater(At(0.5 * HX, 0.5 * HY, -0.5 * Depth)) && !Volume->IsPointInWater(At(0.0, 0.0, 10.0)));

		// Bounds: a world box around the rotated water box, top = surface, bottom = surface - depth.
		const FBoxSphereBounds& B = Comp->Bounds;
		const double C = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Want.Yaw)));
		const double S = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Want.Yaw)));
		Test.TestNearlyEqual(*(P + TEXT("bounds top = the surface")), B.Origin.Z + B.BoxExtent.Z, Want.SurfaceCenter.Z, 0.5);
		Test.TestNearlyEqual(*(P + TEXT("bounds bottom = surface - depth")), B.Origin.Z - B.BoxExtent.Z, Want.SurfaceCenter.Z - Depth, 0.5);
		Test.TestNearlyEqual(*(P + TEXT("bounds X")), B.BoxExtent.X, C * HX + S * HY, 1.0);
		Test.TestNearlyEqual(*(P + TEXT("bounds Y")), B.BoxExtent.Y, S * HX + C * HY, 1.0);
		Test.TestTrue(*(P + TEXT("bounds centered on the surface center")), FVector2D(B.Origin).Equals(FVector2D(Want.SurfaceCenter), 1.0));

		// Editor brush.
		UModel* Brush = Volume->Brush;
		if (!bExpectBrush)
		{
			Test.TestNull(*(P + TEXT("no brush in game worlds (the body gives the bounds)")), Brush);
			return;
		}
		if (!Test.TestNotNull(*(P + TEXT("the editor brush exists (the editor's brush code asserts without one)")), Brush))
		{
			return;
		}
		Test.TestTrue(*(P + TEXT("the component uses the actor's brush")), Comp->Brush == Brush);
		Test.TestNull(*(P + TEXT("no brush builder settings")), Volume->BrushBuilder.Get());
		if (!Test.TestNotNull(*(P + TEXT("brush polygons")), Brush->Polys.Get()))
		{
			return;
		}
		const TArray<FPoly>& Polys = Brush->Polys->Element;
		Test.TestEqual(*(P + TEXT("the brush has 6 faces")), Polys.Num(), 6);
		const FVector3f BoxCenter(0.f, 0.f, static_cast<float>(-0.5 * Depth));
		FBox Corners(ForceInit);
		bool bAllOut = true;
		bool bAllQuads = true;
		for (const FPoly& Poly : Polys)
		{
			bAllQuads &= Poly.Vertices.Num() == 4;
			FVector3f Mid = FVector3f::ZeroVector;
			for (const FVector3f& Vertex : Poly.Vertices)
			{
				Mid += Vertex;
				Corners += FVector(Vertex);
			}
			Mid /= static_cast<float>(FMath::Max(Poly.Vertices.Num(), 1));
			bAllOut &= (Poly.Normal | (Mid - BoxCenter)) > 0.f;
		}
		Test.TestTrue(*(P + TEXT("every face is a quad")), bAllQuads);
		Test.TestTrue(*(P + TEXT("every face points out (not an inverted brush)")), bAllOut);
		Test.TestTrue(*(P + TEXT("brush corners = the water box (local)")),
			Corners.Min.Equals(FVector(-HX, -HY, -Depth), 0.1) && Corners.Max.Equals(FVector(HX, HY, 0.0), 0.1));
	}

	int32 CountWater(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ALureWaterVolume> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	ALureWaterVolume* FirstWater(UWorld* World)
	{
		for (TActorIterator<ALureWaterVolume> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				return *It;
			}
		}
		return nullptr;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// The builder's editor placement path, in an editor world of our own (the open level is never touched)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterVolumeEditorPlacedTest, "Project.Movement.Swim.WaterVolumeEditor.PlacedLikeTheLevelBuilder", LureWaterVolumeEditorTest::Flags)

bool FLureWaterVolumeEditorPlacedTest::RunTest(const FString& Parameters)
{
	using LureWaterVolumeEditorTest::FWaterBox;

	if (!TestNotNull(TEXT("GEditor"), GEditor))
	{
		return false;
	}
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Editor))
	{
		Wrapper.ForwardErrorMessages(this);
		AddError(TEXT("the editor test world could not be created"));
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();

	// The factory spawn_actor_from_class gets for our class (TryPlacingAssetObject: FindActorFactoryForActorClass).
	UActorFactory* Factory = GEditor->FindActorFactoryForActorClass(ALureWaterVolume::StaticClass());
	if (!TestNotNull(TEXT("the editor has an actor factory for ALureWaterVolume"), Factory))
	{
		return false;
	}
	TestTrue(TEXT("it is a volume factory (builds a brush, then FBSPOps::csgPrepMovingBrush: the path that asserted)"), Factory->IsA<UActorFactoryVolume>());
	UPlacementSubsystem* Placement = GEditor->GetEditorSubsystem<UPlacementSubsystem>();
	if (!TestNotNull(TEXT("placement subsystem"), Placement))
	{
		return false;
	}

	// 1. Place: the same PlaceAsset call as UE::AssetPlacementUtil::PlaceAssetUsingFactory, into our level
	//    (PrePlaceAsset, PlaceAsset = CreateActor with PostSpawnActor, PostPlaceAsset = PostSpawnActor again).
	FWaterBox Want;
	Want.SurfaceCenter = FVector(1000.0, 3300.0, -60.0); // L_Dev_Movement's pool
	{
		FAssetPlacementInfo Info;
		Info.AssetToPlace = FAssetData(ALureWaterVolume::StaticClass());
		Info.PreferredLevel = World->PersistentLevel;
		Info.FinalizedTransform = FTransform(Want.SurfaceCenter);
		Info.FactoryOverride = Factory;
		const TArray<FTypedElementHandle> Placed = Placement->PlaceAsset(Info, FPlacementOptions());
		TestEqual(TEXT("one element placed"), Placed.Num(), 1);
	}
	TestEqual(TEXT("one water volume in the level"), LureWaterVolumeEditorTest::CountWater(World), 1);
	ALureWaterVolume* Volume = LureWaterVolumeEditorTest::FirstWater(World);
	if (!TestNotNull(TEXT("the factory placed a water volume"), Volume))
	{
		return false;
	}
	// What UEditorActorSubsystem's spawn does right after placing.
	Volume->SetActorLocationAndRotation(Want.SurfaceCenter, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("placed (default size)"), Volume, Want, true);

	// 2. Size and label it like build_level (_size_water, then _finish_actor).
	Want.HalfSize = FVector2D(600.0, 400.0);
	Want.Depth = 250.0;
	LureWaterVolumeEditorTest::SizeLikeTheBuilder(Volume, Want.HalfSize, static_cast<float>(Want.Depth));
	Volume->SetActorLabel(TEXT("pool.water_volume"));
	{
		FProperty* TagsProperty = AActor::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(AActor, Tags));
		Volume->PreEditChange(TagsProperty);
		Volume->Tags = { FName(TEXT("LureLayout")), FName(TEXT("LureLayout=L_Dev_Movement")), FName(TEXT("LureId=pool/water_volume")) };
		FPropertyChangedEvent TagsEvent(TagsProperty, EPropertyChangeType::ValueSet);
		Volume->PostEditChangeProperty(TagsEvent);
	}
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("sized like the builder"), Volume, Want, true);

	// 3. Move and turn it like a viewport drag (PostEditMove while dragging, then when done).
	Want.SurfaceCenter = FVector(1400.0, 2900.0, -40.0);
	Want.Yaw = 30.0;
	Volume->Modify();
	Volume->SetActorLocation(FVector(1200.0, 3100.0, -50.0));
	Volume->PostEditMove(false);
	Volume->SetActorLocationAndRotation(Want.SurfaceCenter, FRotator(0.0, Want.Yaw, 0.0));
	Volume->PostEditMove(true);
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("moved and turned"), Volume, Want, true);

	// 4. The editor's brush rebuilds (csgPrepMovingBrush replaces the collision with the brush's hulls; the hook restores it).
	Volume->PostEditImport(); // paste / duplicate
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("after a paste's brush rebuild"), Volume, Want, true);
	{
		TGuardValue<bool> Undoing(GIsTransacting, true); // undo/redo notify with no property: AVolume rebuilds the brush
		FPropertyChangedEvent UndoEvent(nullptr);
		Volume->PostEditChangeProperty(UndoEvent);
	}
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("after an undo's brush rebuild"), Volume, Want, true);

	// 5. Resize from a script after all that.
	Want.HalfSize = FVector2D(800.0, 500.0);
	Want.Depth = 300.0;
	Volume->SetWaterSize(Want.HalfSize, static_cast<float>(Want.Depth));
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("resized"), Volume, Want, true);

	// 6. Delete it like build_level.destroy_actors_with_tag (EditorActorSubsystem.destroy_actors -> EditorDestroyActor).
	TestTrue(TEXT("EditorDestroyActor removes it"), World->EditorDestroyActor(Volume, true));
	TestEqual(TEXT("no water volume left"), LureWaterVolumeEditorTest::CountWater(World), 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The builder's own call, EditorActorSubsystem.spawn_actor_from_class, in the editor's world (headless runs only)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterVolumeEditorSpawnFromClassTest, "Project.Movement.Swim.WaterVolumeEditor.SpawnActorFromClass", LureWaterVolumeEditorTest::Flags)

bool FLureWaterVolumeEditorSpawnFromClassTest::RunTest(const FString& Parameters)
{
	using LureWaterVolumeEditorTest::FWaterBox;

	if (!FApp::IsUnattended())
	{
		AddInfo(TEXT("Skipped: it places an actor in the editor's open level, so it only runs headless (tools/run-tests.ps1)."));
		return true;
	}
	UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("EditorActorSubsystem"), ActorSubsystem))
	{
		return false;
	}

	FWaterBox Want;
	Want.SurfaceCenter = FVector(60000.0, 60000.0, -3000.0); // far from anything in the startup level
	Want.Yaw = 15.0;
	AActor* Spawned = ActorSubsystem->SpawnActorFromClass(ALureWaterVolume::StaticClass(), Want.SurfaceCenter, FRotator(0.0, Want.Yaw, 0.0));
	ALureWaterVolume* Volume = Cast<ALureWaterVolume>(Spawned);
	if (!TestNotNull(TEXT("spawn_actor_from_class gives a water volume"), Volume))
	{
		return false;
	}
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("spawned"), Volume, Want, true);

	Want.HalfSize = FVector2D(40000.0, 40000.0); // L_PalmKey's sea
	Want.Depth = 1000.0;
	LureWaterVolumeEditorTest::SizeLikeTheBuilder(Volume, Want.HalfSize, static_cast<float>(Want.Depth));
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("sized like the builder"), Volume, Want, true);

	Want.SurfaceCenter += FVector(500.0, -300.0, 0.0);
	Volume->SetActorLocation(Want.SurfaceCenter);
	Volume->PostEditMove(true);
	LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("moved"), Volume, Want, true);

	TestTrue(TEXT("destroy_actor removes it"), ActorSubsystem->DestroyActor(Volume));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Brushes only where the editor needs them; SetWaterSize safe at runtime
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterVolumeEditorWorldKindsTest, "Project.Movement.Swim.WaterVolumeEditor.BrushOnlyInEditorWorlds", LureWaterVolumeEditorTest::Flags)

bool FLureWaterVolumeEditorWorldKindsTest::RunTest(const FString& Parameters)
{
	using LureWaterVolumeEditorTest::FWaterBox;

	// Spawned from code in an editor world (no actor factory): the volume makes its own brush.
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Editor))
		{
			Wrapper.ForwardErrorMessages(this);
			AddError(TEXT("the editor test world could not be created"));
			return false;
		}
		FWaterBox Want;
		Want.SurfaceCenter = FVector(-500.0, 250.0, 20.0);
		Want.HalfSize = FVector2D(300.0, 200.0);
		Want.Depth = 150.0;
		const FTransform Transform(Want.SurfaceCenter);
		ALureWaterVolume* Volume = Wrapper.GetTestWorld()->SpawnActorDeferred<ALureWaterVolume>(ALureWaterVolume::StaticClass(), Transform);
		if (!TestNotNull(TEXT("editor world: spawns"), Volume))
		{
			return false;
		}
		Volume->SurfaceHalfSize = Want.HalfSize;
		Volume->WaterDepth = static_cast<float>(Want.Depth);
		Volume->FinishSpawning(Transform);
		LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("editor world, spawned from code"), Volume, Want, true);
		Want.HalfSize = FVector2D(50.0, 80.0);
		Want.Depth = 40.0;
		Volume->SetWaterSize(Want.HalfSize, static_cast<float>(Want.Depth));
		LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("editor world, resized"), Volume, Want, true);
		TestTrue(TEXT("editor world: EditorDestroyActor removes it"), Wrapper.GetTestWorld()->EditorDestroyActor(Volume, true));
	}

	// A game world (PIE, a server spawning water at runtime): no brush; SetWaterSize never reaches editor code.
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(this);
			AddError(TEXT("the game test world could not be created"));
			return false;
		}
		FWaterBox Want;
		Want.SurfaceCenter = FVector(0.0, 0.0, 0.0);
		Want.HalfSize = FVector2D(1000.0, 700.0);
		Want.Depth = 400.0;
		const FTransform Transform(Want.SurfaceCenter);
		ALureWaterVolume* Volume = Wrapper.GetTestWorld()->SpawnActorDeferred<ALureWaterVolume>(ALureWaterVolume::StaticClass(), Transform);
		if (!TestNotNull(TEXT("game world: spawns"), Volume))
		{
			return false;
		}
		Volume->SurfaceHalfSize = Want.HalfSize;
		Volume->WaterDepth = static_cast<float>(Want.Depth);
		Volume->FinishSpawning(Transform);
		LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("game world"), Volume, Want, false);
		Want.HalfSize = FVector2D(200.0, 100.0);
		Want.Depth = 90.0;
		Volume->SetWaterSize(Want.HalfSize, static_cast<float>(Want.Depth));
		LureWaterVolumeEditorTest::CheckWaterBox(*this, TEXT("game world, resized at runtime"), Volume, Want, false);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Bad sizes (a layout typo, NaN from a script) are clamped before they reach physics or the brush
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureWaterVolumeEditorBadSizesTest, "Project.Movement.Swim.WaterVolumeEditor.BadSizesAreClamped", LureWaterVolumeEditorTest::Flags)

bool FLureWaterVolumeEditorBadSizesTest::RunTest(const FString& Parameters)
{
	for (const EWorldType::Type Kind : { EWorldType::Editor, EWorldType::Game })
	{
		const FString P = Kind == EWorldType::Editor ? TEXT("editor world: ") : TEXT("game world: ");
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(Kind) || (Kind == EWorldType::Game && !Wrapper.BeginPlayInTestWorld()))
		{
			Wrapper.ForwardErrorMessages(this);
			AddError(P + TEXT("the test world could not be created"));
			return false;
		}
		ALureWaterVolume* Volume = Wrapper.GetTestWorld()->SpawnActor<ALureWaterVolume>(ALureWaterVolume::StaticClass(), FTransform::Identity);
		if (!TestNotNull(*(P + TEXT("spawns")), Volume))
		{
			return false;
		}
		Volume->SetWaterSize(FVector2D(std::numeric_limits<double>::quiet_NaN(), 1.0e30), -5.f);
		const FVector Half = Volume->GetWaterBoxHalfExtent();
		TestTrue(*(P + TEXT("NaN -> 1 cm, 1e30 -> 100 km, negative depth -> 1 cm")), Half.Equals(FVector(1.0, 1.0e7, 0.5), 0.001));
		const UBodySetup* Body = Volume->GetBrushComponent()->BrushBodySetup;
		if (TestNotNull(*(P + TEXT("collision body")), Body) && TestEqual(*(P + TEXT("one box")), Body->AggGeom.BoxElems.Num(), 1))
		{
			const FKBoxElem& Box = Body->AggGeom.BoxElems[0];
			TestTrue(*(P + TEXT("box size is finite and clamped")), FMath::IsNearlyEqual(static_cast<double>(Box.X), 2.0, 0.001) && FMath::IsNearlyEqual(static_cast<double>(Box.Y), 2.0e7, 1.0) && FMath::IsNearlyEqual(static_cast<double>(Box.Z), 1.0, 0.001));
		}
		const FBoxSphereBounds& Bounds = Volume->GetBrushComponent()->Bounds;
		TestFalse(*(P + TEXT("bounds have no NaN")), Bounds.Origin.ContainsNaN() || Bounds.BoxExtent.ContainsNaN() || !FMath::IsFinite(Bounds.SphereRadius));
		TestTrue(*(P + TEXT("the thin box still holds water")), Volume->EncompassesPoint(FVector(0.0, 0.0, -0.5)));
		if (Kind == EWorldType::Editor && TestNotNull(*(P + TEXT("brush")), Volume->Brush.Get()) && Volume->Brush->Polys)
		{
			bool bFinite = Volume->Brush->Polys->Element.Num() == 6;
			for (const FPoly& Poly : Volume->Brush->Polys->Element)
			{
				for (const FVector3f& Vertex : Poly.Vertices)
				{
					bFinite &= !Vertex.ContainsNaN();
				}
				bFinite &= !Poly.Normal.ContainsNaN() && Poly.Normal.IsNormalized();
			}
			TestTrue(*(P + TEXT("brush faces finite, with unit normals")), bFinite);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

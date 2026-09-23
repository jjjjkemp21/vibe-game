// Lure: water volume (T-026). Design notes (why the brush stays, why the hook): docs/specs/swimming.md,
// "Water volume and the editor".

#include "Character/LureWaterVolume.h"
#include "Components/BrushComponent.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_EDITOR
#include "Algo/Reverse.h"
#include "Engine/Polys.h"
#include "Model.h"
#endif

#if WITH_EDITOR
namespace LureWaterVolumeBrush
{
	/** One face of the box. Wound so FPoly::CalcNormal points along Outward: brush polygons face out (UBrushComponent::HasInvertedPolys). */
	FPoly MakeFace(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FVector3f& Outward, int32 Index, ABrush* Owner)
	{
		FPoly Poly;
		Poly.Init();
		Poly.Vertices.Add(A);
		Poly.Vertices.Add(B);
		Poly.Vertices.Add(C);
		Poly.Vertices.Add(D);
		if ((((B - A) ^ (C - A)) | Outward) < 0.f)
		{
			Algo::Reverse(Poly.Vertices);
		}
		Poly.Base = Poly.Vertices[0];
		Poly.PolyFlags = 0;      // what the volume factories' brush builders give
		Poly.Material = nullptr; // volumes carry no material (UActorFactory::CreateBrushForVolumeActor clears them too)
		Poly.Finalize(Owner, /*NoError*/ 1); // normal and texture vectors (4 distinct corners: never fails)
		Poly.iLink = Index;      // what FBSPOps::bspValidateBrush sets for a box: no two faces share a plane
		return Poly;
	}

	/** The water box's 6 faces in actor space: the top (the water surface) at z = 0, the bottom at z = -2 * HalfExtent.Z. */
	TArray<FPoly> MakeBoxPolys(const FVector& HalfExtent, ABrush* Owner)
	{
		const float X = static_cast<float>(HalfExtent.X);
		const float Y = static_cast<float>(HalfExtent.Y);
		const float Bottom = static_cast<float>(-2.0 * HalfExtent.Z);
		const FVector3f P[8] = {
			FVector3f(-X, -Y, Bottom), FVector3f(X, -Y, Bottom), FVector3f(X, Y, Bottom), FVector3f(-X, Y, Bottom),
			FVector3f(-X, -Y, 0.f),    FVector3f(X, -Y, 0.f),    FVector3f(X, Y, 0.f),    FVector3f(-X, Y, 0.f)};
		TArray<FPoly> Polys;
		Polys.Reserve(6);
		Polys.Add(MakeFace(P[4], P[5], P[6], P[7], FVector3f(0.f, 0.f, 1.f), 0, Owner));  // top: the water surface
		Polys.Add(MakeFace(P[0], P[3], P[2], P[1], FVector3f(0.f, 0.f, -1.f), 1, Owner)); // bottom
		Polys.Add(MakeFace(P[1], P[2], P[6], P[5], FVector3f(1.f, 0.f, 0.f), 2, Owner));  // +X
		Polys.Add(MakeFace(P[0], P[4], P[7], P[3], FVector3f(-1.f, 0.f, 0.f), 3, Owner)); // -X
		Polys.Add(MakeFace(P[2], P[3], P[7], P[6], FVector3f(0.f, 1.f, 0.f), 4, Owner));  // +Y
		Polys.Add(MakeFace(P[0], P[1], P[5], P[4], FVector3f(0.f, -1.f, 0.f), 5, Owner)); // -Y
		return Polys;
	}

	/** Same corners in the same order (geometry only). */
	bool SameShape(const TArray<FPoly>& A, const TArray<FPoly>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 PolyIndex = 0; PolyIndex < A.Num(); ++PolyIndex)
		{
			const FPoly& PA = A[PolyIndex];
			const FPoly& PB = B[PolyIndex];
			if (PA.Vertices.Num() != PB.Vertices.Num())
			{
				return false;
			}
			for (int32 Vertex = 0; Vertex < PA.Vertices.Num(); ++Vertex)
			{
				if (!PA.Vertices[Vertex].Equals(PB.Vertices[Vertex], 0.01f))
				{
					return false;
				}
			}
		}
		return true;
	}
}
#endif // WITH_EDITOR

namespace LureWaterVolumeSize
{
	/** Largest half size on any axis, cm (100 km): a typo like 4e40 must not reach the physics engine as an infinite box. */
	constexpr double MaxHalfSize = 1.0e7;

	/** Finite and within [Min, MaxHalfSize]; NaN and infinities become Min. */
	double Sane(double Value, double Min)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, MaxHalfSize) : Min;
	}
}

ALureWaterVolume::ALureWaterVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWaterVolume = true;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// APhysicsVolume sets the overlap-only profile. The character's physics-volume check needs overlap events on both sides.
	UBrushComponent* BrushComp = GetBrushComponent();
	BrushComp->SetGenerateOverlapEvents(true);
	BrushComp->SetCanEverAffectNavigation(false);
}

void ALureWaterVolume::SetWaterSize(FVector2D NewSurfaceHalfSize, float NewWaterDepth)
{
	SurfaceHalfSize = NewSurfaceHalfSize;
	WaterDepth = NewWaterDepth;
	RebuildWaterBody();
}

float ALureWaterVolume::GetSurfaceHeight() const
{
	return static_cast<float>(GetActorLocation().Z);
}

FVector ALureWaterVolume::GetWaterBoxHalfExtent() const
{
	using LureWaterVolumeSize::Sane;
	return FVector(Sane(SurfaceHalfSize.X, 1.0), Sane(SurfaceHalfSize.Y, 1.0), 0.5 * Sane(WaterDepth, 1.0));
}

bool ALureWaterVolume::IsPointInWater(const FVector& Point) const
{
	const FVector Half = GetWaterBoxHalfExtent();
	const FVector Local = GetActorTransform().InverseTransformPosition(Point);
	return FMath::Abs(Local.X) <= Half.X
		&& FMath::Abs(Local.Y) <= Half.Y
		&& Local.Z <= 0.0
		&& Local.Z >= -2.0 * Half.Z;
}

void ALureWaterVolume::RebuildWaterBody()
{
	UBrushComponent* BrushComp = GetBrushComponent();
	if (!BrushComp)
	{
		return;
	}
	const FVector Half = GetWaterBoxHalfExtent();

#if WITH_EDITOR
	// Before the bounds update below: in editor builds UBrushComponent::CalcBounds prefers the brush's polygons.
	SyncEditorBrush(Half);
#endif

	// Same recipe as UShapeComponent: a transient simple box that also answers complex traces (FindWaterLine uses one).
	// A fresh body every time, because the editor's BuildSimpleBrushCollision edits the current one in place.
	UBodySetup* Body = NewObject<UBodySetup>(BrushComp, NAME_None, RF_Transient);
	Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	Body->bNeverNeedsCookedCollisionData = true;
	FKBoxElem Box(static_cast<float>(2.0 * Half.X), static_cast<float>(2.0 * Half.Y), static_cast<float>(2.0 * Half.Z)); // full sizes
	Box.Center = FVector(0.0, 0.0, -Half.Z); // top face at the actor's origin (the water surface)
	Body->AggGeom.BoxElems.Add(Box);
	BrushComp->BrushBodySetup = Body;

	if (BrushComp->IsRegistered())
	{
		BrushComp->RecreatePhysicsState();
		BrushComp->UpdateBounds();
		BrushComp->MarkRenderStateDirty();
	}
}

#if WITH_EDITOR
void ALureWaterVolume::SyncEditorBrush(const FVector& HalfExtent)
{
	UBrushComponent* BrushComp = GetBrushComponent();
	if (!BrushComp || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}

	// The same flags as the volume factories give it (UActorFactory::CreateBrushForVolumeActor).
	const EObjectFlags BrushFlags = GetFlags() & (RF_Transient | RF_Transactional);
	if (!Brush)
	{
		const UWorld* World = GetWorld();
		if (!GIsEditor || !World || World->IsGameWorld())
		{
			return; // runtime: the collision body alone gives the bounds
		}
		Brush = NewObject<UModel>(this, NAME_None, BrushFlags);
		Brush->Initialize(nullptr, /*InRootOutside*/ true);
	}
	if (!Brush->Polys)
	{
		Brush->Polys = NewObject<UPolys>(Brush, NAME_None, BrushFlags);
	}
	BrushComp->Brush = Brush; // the editor reads both pointers
	BrushBuilder = nullptr;   // the brush is derived from our two properties; a builder's shape settings would not match it

	TArray<FPoly> Wanted = LureWaterVolumeBrush::MakeBoxPolys(HalfExtent, this);
	if (!LureWaterVolumeBrush::SameShape(Brush->Polys->Element, Wanted))
	{
		// Only the polygons: the brush's BSP nodes are rebuilt from them by the editor's next FBSPOps::RebuildBrush, and
		// nothing else reads a volume's nodes (its collision is our body).
		Brush->Polys->Element = MoveTemp(Wanted);
		Brush->Linked = 1; // iLinks are set (MakeFace)
	}
	Brush->BuildBound();
}
#endif // WITH_EDITOR

void ALureWaterVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildWaterBody();
}

void ALureWaterVolume::PostRegisterAllComponents()
{
	// The body is transient: build it whenever the components come up (spawn, level load, PIE copy, editor re-register).
	RebuildWaterBody();
	Super::PostRegisterAllComponents();
}

void ALureWaterVolume::RebuildNavigationData()
{
	RebuildWaterBody();
	Super::RebuildNavigationData();
}

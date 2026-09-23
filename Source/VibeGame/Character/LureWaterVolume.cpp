// Lure: water volume (T-026).

#include "Character/LureWaterVolume.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"
#include "PhysicsEngine/BodySetup.h"

ALureWaterVolume::ALureWaterVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWaterVolume = true;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// APhysicsVolume sets the overlap-only profile. The character's physics-volume check needs overlap events on both sides.
	UBrushComponent* BrushComp = GetBrushComponent();
	BrushComp->SetGenerateOverlapEvents(true);
	BrushComp->SetCanEverAffectNavigation(false);

#if WITH_EDITORONLY_DATA
	EditorPreview = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("EditorPreview"));
	if (EditorPreview)
	{
		EditorPreview->SetupAttachment(BrushComp);
		EditorPreview->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		EditorPreview->SetGenerateOverlapEvents(false);
		EditorPreview->SetCanEverAffectNavigation(false);
		EditorPreview->SetHiddenInGame(true);
		EditorPreview->ShapeColor = FColor(64, 160, 255);
		EditorPreview->bIsEditorOnly = true;
	}
#endif
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

bool ALureWaterVolume::IsPointInWater(const FVector& Point) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Point);
	return FMath::Abs(Local.X) <= FMath::Max(SurfaceHalfSize.X, 1.0)
		&& FMath::Abs(Local.Y) <= FMath::Max(SurfaceHalfSize.Y, 1.0)
		&& Local.Z <= 0.0
		&& Local.Z >= -FMath::Max(WaterDepth, 1.f);
}

void ALureWaterVolume::RebuildWaterBody()
{
	UBrushComponent* BrushComp = GetBrushComponent();
	if (!BrushComp)
	{
		return;
	}

	// The shape is ours. Drop any BSP brush an editor factory created (in the editor its bounds would win over the box).
	if (Brush || BrushComp->Brush)
	{
		Brush = nullptr;
		BrushComp->Brush = nullptr;
	}

	const float HalfX = static_cast<float>(FMath::Max(SurfaceHalfSize.X, 1.0));
	const float HalfY = static_cast<float>(FMath::Max(SurfaceHalfSize.Y, 1.0));
	const float HalfZ = 0.5f * FMath::Max(WaterDepth, 1.f);

	// Same recipe as UShapeComponent: a transient simple box that also answers complex traces (FindWaterLine uses one).
	UBodySetup* Body = NewObject<UBodySetup>(BrushComp, NAME_None, RF_Transient);
	Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	Body->bNeverNeedsCookedCollisionData = true;
	FKBoxElem Box(2.f * HalfX, 2.f * HalfY, 2.f * HalfZ); // full sizes
	Box.Center = FVector(0.f, 0.f, -HalfZ);                // top face at the actor's origin (the water surface)
	Body->AggGeom.BoxElems.Add(Box);
	BrushComp->BrushBodySetup = Body;

	if (BrushComp->IsRegistered())
	{
		BrushComp->RecreatePhysicsState();
		BrushComp->UpdateBounds();
		BrushComp->MarkRenderStateDirty();
	}

#if WITH_EDITORONLY_DATA
	if (EditorPreview)
	{
		EditorPreview->SetBoxExtent(FVector(HalfX, HalfY, HalfZ));
		EditorPreview->SetRelativeLocation(FVector(0.f, 0.f, -HalfZ));
	}
#endif
}

void ALureWaterVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildWaterBody();
}

void ALureWaterVolume::PostRegisterAllComponents()
{
	// The body is transient: build it whenever the components come up (spawn, level load, PIE copy).
	RebuildWaterBody();
	Super::PostRegisterAllComponents();
}

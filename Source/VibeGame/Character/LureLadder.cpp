// Lure: ladder out of the water (T-026).

#include "Character/LureLadder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ALureLadder::ALureLadder()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

	LadderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LadderMesh"));
	LadderMesh->SetupAttachment(Root);
	LadderMesh->SetMobility(EComponentMobility::Movable); // resized by OnConstruction, also in game worlds
	LadderMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	LadderMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		LadderMesh->SetStaticMesh(CubeMesh.Object);
	}
	UpdateVisual();
}

bool ALureLadder::IsInGrabZone(const FVector& Point) const
{
	// Zone box in the ladder's space: from the dock face (X = 0) out to 2 * X half size, centered on the water line.
	const FVector Local = GetActorTransform().InverseTransformPosition(Point);
	return Local.X >= -5.0 && Local.X <= 2.0 * GrabZoneHalfSize.X
		&& FMath::Abs(Local.Y) <= GrabZoneHalfSize.Y
		&& FMath::Abs(Local.Z) <= GrabZoneHalfSize.Z;
}

FVector ALureLadder::GetClimbDirection() const
{
	const FVector Forward = FVector(GetActorForwardVector().X, GetActorForwardVector().Y, 0.0).GetSafeNormal();
	return Forward.IsNearlyZero() ? FVector::BackwardVector : -Forward;
}

void ALureLadder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisual();
}

void ALureLadder::UpdateVisual()
{
	if (!LadderMesh)
	{
		return;
	}
	// The engine cube is 100 cm and centered: a 50 cm wide, 6 cm thick board against the dock face.
	const float Height = FMath::Max(VisualHeightAboveWater + VisualDepthBelowWater, 1.f);
	LadderMesh->SetRelativeLocation(FVector(3.f, 0.f, 0.5f * (VisualHeightAboveWater - VisualDepthBelowWater)));
	LadderMesh->SetRelativeScale3D(FVector(0.06f, 0.5f, Height / 100.f));
}

// Lure: the fishing line (T-006).

#include "Fishing/LureFishingLineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Fishing/FishingTypes.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

ULureFishingLineComponent::ULureFishingLineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
}

void ULureFishingLineComponent::Setup(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FLinearColor& Color, int32 NumSegments)
{
	DestroySegments();
	Mesh = InMesh;
	Material = InMaterial;
	if (!Mesh || !GetOwner())
	{
		return;
	}
	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	MeshDiameter = FMath::Max(0.01f, 2.f * static_cast<float>(FMath::Max(MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y)));

	UMaterialInterface* SegmentMaterial = Material;
	if (Material)
	{
		if (UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, this))
		{
			Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
			SegmentMaterial = Dynamic;
		}
	}

	const int32 Count = FMath::Clamp(NumSegments, 1, 64);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCanEverAffectNavigation(false);
		Segment->SetCastShadow(false);
		Segment->SetUsingAbsoluteLocation(true);
		Segment->SetUsingAbsoluteRotation(true);
		Segment->SetUsingAbsoluteScale(true);
		Segment->SetStaticMesh(Mesh);
		if (SegmentMaterial)
		{
			Segment->SetMaterial(0, SegmentMaterial);
		}
		Segment->SetForwardAxis(ESplineMeshAxis::Z, false);
		Segment->SetupAttachment(this);
		Segment->SetWorldTransform(FTransform::Identity);
		Segment->SetVisibility(false);
		Segment->RegisterComponent();
		Segments.Add(Segment);
	}
	bLineVisible = false;
}

void ULureFishingLineComponent::SetLine(const FVector& Start, const FVector& End, float Sag, const FVector& ViewLocation, float HorizontalFovDeg,
	float PixelWidth, float ReferenceScreenWidth, float MinWidth)
{
	if (Segments.Num() == 0)
	{
		return;
	}
	FLureFishingRules::ComputeLinePoints(Start, End, Sag, Segments.Num(), Points);
	Widths.SetNum(Points.Num());
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const float Distance = static_cast<float>(FVector::Dist(ViewLocation, Points[Index]));
		Widths[Index] = FLureFishingRules::LineWidthAtDistance(PixelWidth, Distance, HorizontalFovDeg, ReferenceScreenWidth, MinWidth);
	}

	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		USplineMeshComponent* Segment = Segments[Index];
		if (!Segment)
		{
			continue;
		}
		const FVector& A = Points[Index];
		const FVector& B = Points[Index + 1];
		// Tangents from the neighbors (Catmull-Rom style), so the sag bends smoothly across segments.
		const FVector& Prev = Points[FMath::Max(0, Index - 1)];
		const FVector& Next = Points[FMath::Min(Points.Num() - 1, Index + 2)];
		const float Span = FMath::Max(1.f, static_cast<float>(FVector::Dist(A, B)));
		const FVector TangentA = (B - Prev).GetSafeNormal() * Span;
		const FVector TangentB = (Next - A).GetSafeNormal() * Span;
		const FVector Up = FMath::Abs((B - A).GetSafeNormal().Z) > 0.95f ? FVector::ForwardVector : FVector::UpVector;
		Segment->SetSplineUpDir(Up, false);
		Segment->SetStartAndEnd(A, TangentA, B, TangentB, false);
		Segment->SetStartScale(FVector2D(Widths[Index] / MeshDiameter), false);
		Segment->SetEndScale(FVector2D(Widths[Index + 1] / MeshDiameter), false);
		Segment->UpdateMesh();
		if (!Segment->IsVisible())
		{
			Segment->SetVisibility(true);
		}
	}
	bLineVisible = true;
}

void ULureFishingLineComponent::Hide()
{
	if (!bLineVisible)
	{
		return;
	}
	for (USplineMeshComponent* Segment : Segments)
	{
		if (Segment)
		{
			Segment->SetVisibility(false);
		}
	}
	bLineVisible = false;
}

void ULureFishingLineComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	DestroySegments();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void ULureFishingLineComponent::DestroySegments()
{
	for (USplineMeshComponent* Segment : Segments)
	{
		if (IsValid(Segment))
		{
			Segment->DestroyComponent();
		}
	}
	Segments.Reset();
	Points.Reset();
	Widths.Reset();
	bLineVisible = false;
}

// Lure: the look of a hot spot (T-027). Placeholder only (engine basic shapes); real VFX come from a Blueprint child.

#include "Fishing/LureHotSpotVisualComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Fishing/LureHotSpot.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"

ULureHotSpotVisualComponent::ULureHotSpotVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PlaceholderMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	PlaceholderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
}

void ULureHotSpotVisualComponent::InitFromHotSpot(const FLureHotSpotState& InState)
{
	HotSpot = InState;

	// Where the bubbles come up, and when: from the hot spot's seed (the same on every machine).
	FRandomStream Stream(HotSpot.Seed ^ 0x5bd1e995);
	const int32 Count = FMath::Clamp(BubbleCount, 0, 32);
	BubbleSpots.Reset(Count);
	BubblePhases.Reset(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = Stream.FRand() * UE_TWO_PI;
		const float Distance = FMath::Sqrt(Stream.FRand()) * FMath::Clamp(BubbleSpread, 0.f, 1.f);
		BubbleSpots.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance);
		BubblePhases.Add(Stream.FRand());
	}

	if (bDrawPlaceholder && !bBuilt)
	{
		BuildPlaceholder();
	}
	if (Material)
	{
		Material->SetVectorParameterValue(ColorParameter, FLinearColor(HotSpot.Color));
	}
}

UInstancedStaticMeshComponent* ULureHotSpotVisualComponent::MakePart(UStaticMesh* Mesh)
{
	UInstancedStaticMeshComponent* Part = NewObject<UInstancedStaticMeshComponent>(GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this), NAME_None, RF_Transient);
	Part->SetStaticMesh(Mesh);
	Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(false);
	Part->bReceivesDecals = false;
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	Part->SetupAttachment(this);
	Part->RegisterComponent();
	return Part;
}

void ULureHotSpotVisualComponent::BuildPlaceholder()
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !IsRegistered())
	{
		return;
	}
	UStaticMesh* Mesh = PlaceholderMesh.LoadSynchronous();
	if (!Mesh)
	{
		return;
	}
	if (UMaterialInterface* Base = PlaceholderMaterial.LoadSynchronous())
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
	}

	// Rings: dots on a 100 cm circle; the ring component's scale spreads them (TickComponent). Dot size is set so a dot is
	// DotSize wide when the ring reaches the full radius.
	const float Radius = FMath::Max(1.f, HotSpot.Radius);
	const int32 Dots = FMath::Clamp(DotsPerRing, 3, 64);
	for (int32 RingIndex = 0; RingIndex < FMath::Clamp(RingCount, 0, 4); ++RingIndex)
	{
		UInstancedStaticMeshComponent* Ring = MakePart(Mesh);
		TArray<FTransform> Instances;
		for (int32 Dot = 0; Dot < Dots; ++Dot)
		{
			const float Angle = UE_TWO_PI * (static_cast<float>(Dot) + 0.5f * RingIndex) / static_cast<float>(Dots);
			const FVector Scale(DotSize / Radius, DotSize / Radius, DotHeight / 100.f);
			Instances.Add(FTransform(FQuat::Identity, FVector(100.f * FMath::Cos(Angle), 100.f * FMath::Sin(Angle), 0.f), Scale));
		}
		Ring->AddInstances(Instances, /*bShouldReturnIndices*/ false);
		Rings.Add(Ring);
	}
	if (BubbleSpots.Num() > 0)
	{
		Bubbles = MakePart(Mesh);
		TArray<FTransform> Instances;
		Instances.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.01f)), BubbleSpots.Num());
		Bubbles->AddInstances(Instances, /*bShouldReturnIndices*/ false);
	}
	bBuilt = true;
}

void ULureHotSpotVisualComponent::DestroyPlaceholder()
{
	for (UInstancedStaticMeshComponent* Ring : Rings)
	{
		if (IsValid(Ring))
		{
			Ring->DestroyComponent();
		}
	}
	Rings.Reset();
	if (IsValid(Bubbles))
	{
		Bubbles->DestroyComponent();
	}
	Bubbles = nullptr;
	bBuilt = false;
}

TArray<UInstancedStaticMeshComponent*> ULureHotSpotVisualComponent::GetPlaceholderParts() const
{
	TArray<UInstancedStaticMeshComponent*> Parts;
	for (UInstancedStaticMeshComponent* Ring : Rings)
	{
		Parts.Add(Ring);
	}
	if (Bubbles)
	{
		Parts.Add(Bubbles);
	}
	return Parts;
}

void ULureHotSpotVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ALureHotSpot* Owner = Cast<ALureHotSpot>(GetOwner());
	const double Time = Owner ? Owner->GetTime() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	UpdatePlaceholder(Time, Owner ? Owner->GetFadeAt(Time) : 1.f);
}

void ULureHotSpotVisualComponent::UpdatePlaceholder(double Time, float Fade)
{
	if (!bBuilt)
	{
		return;
	}
	const bool bShow = Fade > 0.01f;
	if (IsVisible() != bShow)
	{
		SetVisibility(bShow, /*bPropagateToChildren*/ true);
	}
	if (!bShow)
	{
		return;
	}
	const double Radius = FMath::Max(1.0, static_cast<double>(HotSpot.Radius));
	const double RingSeconds = FMath::Max(0.1, static_cast<double>(RingPeriod));
	for (int32 Index = 0; Index < Rings.Num(); ++Index)
	{
		UInstancedStaticMeshComponent* Ring = Rings[Index];
		if (!Ring)
		{
			continue;
		}
		const double Progress = FMath::Frac(Time / RingSeconds + static_cast<double>(Index) / FMath::Max(1, Rings.Num()));
		const double RingRadius = FMath::Lerp(0.25, 1.0, Progress) * Radius;
		const double Height = FMath::Max(0.001, (1.0 - Progress * Progress) * Fade); // the foam sinks as it spreads
		Ring->SetRelativeLocation(FVector(0.0, 0.0, HeightAboveWater));
		Ring->SetRelativeScale3D(FVector(RingRadius / 100.0, RingRadius / 100.0, Height));
	}
	if (Bubbles && BubbleSpots.Num() > 0)
	{
		const double BubbleSeconds = FMath::Max(0.1, static_cast<double>(BubblePeriod));
		TArray<FTransform> Transforms;
		Transforms.Reserve(BubbleSpots.Num());
		for (int32 Index = 0; Index < BubbleSpots.Num(); ++Index)
		{
			const double Phase = FMath::Frac(Time / BubbleSeconds + BubblePhases[Index]);
			const double Size = FMath::Max(0.001, BubbleSize / 100.0 * FMath::Sin(UE_DOUBLE_PI * Phase) * Fade); // grows, then pops
			const FVector2D Spot = BubbleSpots[Index] * Radius;
			const double Z = HeightAboveWater + FMath::Lerp(-6.0, 6.0, Phase);
			Transforms.Add(FTransform(FQuat::Identity, FVector(Spot.X, Spot.Y, Z), FVector(Size)));
		}
		Bubbles->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	}
}

void ULureHotSpotVisualComponent::OnUnregister()
{
	// The parts belong to the actor: when the whole hot spot goes away the actor removes them; only an unregistered
	// visual on a living actor (a swapped look) cleans up after itself.
	if (GetOwner() && !GetOwner()->IsActorBeingDestroyed())
	{
		DestroyPlaceholder();
	}
	Super::OnUnregister();
}

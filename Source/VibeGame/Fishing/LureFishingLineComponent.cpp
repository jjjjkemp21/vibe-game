// Lure: the fishing line (T-006 look, T-032 physics). Spec: docs/specs/fishing-line.md.

#include "Fishing/LureFishingLineComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

namespace LureFishingLinePrivate
{
	/** Segments when a line starts without Setup (DT_Fishing's default). */
	constexpr int32 DefaultSegments = 12;

	/** One message per session about the built-in tuning (every line resolves it). */
	bool bReportedFallback = false;

	/** Loads a soft reference if its package exists (no load errors for assets not imported yet, e.g. in lanes). */
	UDataTable* LoadTableIfExists(const TSoftObjectPtr<UDataTable>& Ref)
	{
		if (Ref.IsNull())
		{
			return nullptr;
		}
		if (UDataTable* Loaded = Ref.Get())
		{
			return Loaded;
		}
		const FString Package = Ref.ToSoftObjectPath().GetLongPackageName();
		if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
		{
			return nullptr;
		}
		return Ref.LoadSynchronous();
	}
}

ULureFishingLineComponent::ULureFishingLineComponent()
{
	// Ticks only while a line is out (or recoiling / carrying an actor), after the camera and animation moved this frame.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
}

// ---- Setup ----

void ULureFishingLineComponent::Setup(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FLinearColor& Color, int32 NumSegments)
{
	DestroySegments();
	Mesh = InMesh;
	Material = InMaterial;
	Sim.Init(NumSegments);
	Widths.SetNumZeroed(Sim.GetNumSegments() + 1);
	ReserveColliders();
	if (!Mesh || !GetOwner())
	{
		return; // simulated, not drawn
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

	for (int32 Index = 0; Index < Sim.GetNumSegments(); ++Index)
	{
		USplineMeshComponent* Segment = NewObject<ULureLineSegmentComponent>(GetOwner(), NAME_None, RF_Transient);
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
}

bool ULureFishingLineComponent::SetTuning(const FLureFishingLineRow& Row)
{
	FString Problem;
	const bool bValid = Row.Validate(Problem);
	Tuning = bValid ? Row : FLureFishingLineRules::GetFallbackRow();
	bFallbackTuning = !bValid;
	bTuningResolved = true;
	return bValid;
}

const FLureFishingLineRow& ULureFishingLineComponent::GetTuning() const
{
	if (!bTuningResolved)
	{
		ResolveTuning();
	}
	return Tuning;
}

bool ULureFishingLineComponent::IsUsingFallbackTuning() const
{
	GetTuning();
	return bFallbackTuning;
}

void ULureFishingLineComponent::ResolveTuning() const
{
	using namespace LureFishingLinePrivate;
	bTuningResolved = true;
	bFallbackTuning = true;
	Tuning = FLureFishingLineRules::GetFallbackRow();

	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const UDataTable* Table = Settings ? LoadTableIfExists(Settings->FishingLineTable) : nullptr;
	if (!Table)
	{
		// Normal until the editor-operator imports it (lanes, fresh checkouts): the built-in row = the shipped CSV. Not a warning.
		if (!bReportedFallback)
		{
			bReportedFallback = true;
			UE_LOG(LogLureFishing, Log, TEXT("Fishing line: '%s' is not imported (source data/tables/DT_FishingLine.csv); %s."),
				Settings ? *Settings->FishingLineTable.ToString() : TEXT("none"), FLureFishingLineRules::FallbackMarker);
		}
		return;
	}
	FString Problem;
	const FName RowName = Settings->FishingLineRow;
	const FLureFishingLineRow* Row = Table->GetRowStruct() == FLureFishingLineRow::StaticStruct()
		? Table->FindRow<FLureFishingLineRow>(RowName, TEXT("fishing line"), /*bWarnIfRowMissing*/ false) : nullptr;
	if (Row && Row->Validate(Problem))
	{
		Tuning = *Row;
		bFallbackTuning = false;
		return;
	}
	if (!bReportedFallback)
	{
		bReportedFallback = true;
		UE_LOG(LogLureFishing, Warning, TEXT("Fishing line: %s row '%s' is %s; %s."), *GetNameSafe(Table), *RowName.ToString(),
			Row ? *FString::Printf(TEXT("invalid (%s)"), *Problem) : TEXT("missing (or the table has another row struct)"), FLureFishingLineRules::FallbackMarker);
	}
}

void ULureFishingLineComponent::SetStartProvider(FLureLinePointProvider Provider)
{
	StartProvider = MoveTemp(Provider);
}

// ---- Every frame while the line is out ----

void ULureFishingLineComponent::SetEndpoints(const FVector& RodTip, const FVector& End)
{
	if (!RodTip.ContainsNaN())
	{
		TipInput = RodTip;
	}
	if (!End.ContainsNaN())
	{
		EndInput = End;
	}
	bOut = true;
	if (Mode == ELureLineMode::None || Mode == ELureLineMode::Recoil)
	{
		StartLine(ELureLineMode::Pinned); // a new line (it also ends a snap recoil)
	}
}

void ULureFishingLineComponent::SetTension(float Tension01)
{
	Tension = FMath::IsNaN(Tension01) ? 0.f : FMath::Clamp(Tension01, 0.f, 1.f); // NaN = slack; +Inf = fully taut
}

void ULureFishingLineComponent::SetSlack(float SlackShare)
{
	Slack = (FMath::IsFinite(SlackShare) && SlackShare >= 0.f) ? FMath::Min(SlackShare, 1.f) : -1.f;
}

void ULureFishingLineComponent::SetWaterSurfaceZ(float WaterZ)
{
	if (FMath::IsFinite(WaterZ))
	{
		bWaterOverride = true;
		WaterOverrideZ = WaterZ;
	}
	else
	{
		ClearWaterSurfaceZ();
	}
}

void ULureFishingLineComponent::ClearWaterSurfaceZ()
{
	bWaterOverride = false;
}

void ULureFishingLineComponent::SetViewer(const FVector& InViewLocation, float HorizontalFovDeg)
{
	if (!InViewLocation.ContainsNaN())
	{
		ViewLocation = InViewLocation;
		ViewFovDeg = FMath::IsFinite(HorizontalFovDeg) ? FMath::Clamp(HorizontalFovDeg, 5.f, 170.f) : 90.f;
		bViewerSet = true;
	}
}

void ULureFishingLineComponent::SetWidthRule(float InPixelWidth, float InReferenceScreenWidth, float InMinWidth)
{
	PixelWidth = FMath::IsFinite(InPixelWidth) ? FMath::Max(0.f, InPixelWidth) : PixelWidth;
	ReferenceScreenWidth = FMath::IsFinite(InReferenceScreenWidth) ? FMath::Max(1.f, InReferenceScreenWidth) : ReferenceScreenWidth;
	MinWidth = FMath::IsFinite(InMinWidth) ? FMath::Max(0.f, InMinWidth) : MinWidth;
}

void ULureFishingLineComponent::SetLine(const FVector& Start, const FVector& End, float Sag, const FVector& InViewLocation, float HorizontalFovDeg,
	float InPixelWidth, float InReferenceScreenWidth, float InMinWidth)
{
	SetViewer(InViewLocation, HorizontalFovDeg);
	SetWidthRule(InPixelWidth, InReferenceScreenWidth, InMinWidth);
	const float SafeSag = FMath::IsFinite(Sag) ? FMath::Max(0.f, Sag) : 0.f;
	SetSlack(8.f / 3.f * SafeSag * SafeSag); // parabola: extra length = 8/3 x sag^2 x length
	SetTension(0.f);
	SetEndpoints(Start, End);
}

void ULureFishingLineComponent::Hide()
{
	bOut = false;
	if (Mode == ELureLineMode::Pinned)
	{
		StopLine();
	}
	// Hanging: the actor keeps its line until DetachEndActor. Recoil: finishes on its own (RecoilTime).
}

// ---- Events ----

void ULureFishingLineComponent::Snap()
{
	if (Mode != ELureLineMode::Pinned || !Sim.HasState())
	{
		return;
	}
	Mode = ELureLineMode::Recoil;
	RecoilElapsed = 0.f;
	RecoilStartLength = RestLength;
	Sim.ReleaseEnd(1.f);
	Sim.AddRecoil(GetTuning().RecoilSpeed);
	SetComponentTickEnabled(true);
}

// ---- A hanging actor ----

void ULureFishingLineComponent::AttachEndActor(AActor* Actor, float InHangLength, const FVector& InHookOffset, bool bOrientAlongLine)
{
	if (!IsValid(Actor))
	{
		return;
	}
	EndActor = Actor;
	HangLength = FMath::IsFinite(InHangLength) ? FMath::Clamp(InHangLength, 1.f, 10000.f) : 100.f;
	HookOffset = InHookOffset.ContainsNaN() ? FVector::ZeroVector : InHookOffset;
	bOrientEndActor = bOrientAlongLine;
	if (Mode == ELureLineMode::Pinned)
	{
		// The line keeps its shape; its end lets go and becomes the actor's hook.
		Mode = ELureLineMode::Hanging;
		Sim.ReleaseEnd(GetTuning().HangEndMass);
		UpdateLine(0.f);
	}
	else if (Mode != ELureLineMode::Hanging)
	{
		StartLine(ELureLineMode::Hanging); // laid from the rod tip toward the actor's hook
	}
}

void ULureFishingLineComponent::DetachEndActor()
{
	EndActor.Reset();
	if (Mode != ELureLineMode::Hanging)
	{
		return;
	}
	if (bOut)
	{
		Mode = ELureLineMode::Pinned; // back to the owner's end (the next step pins it there)
	}
	else
	{
		StopLine();
	}
}

AActor* ULureFishingLineComponent::GetEndActor() const
{
	return EndActor.Get();
}

void ULureFishingLineComponent::AddEndVelocity(const FVector& Velocity)
{
	Sim.AddEndVelocity(Velocity);
}

// ---- Queries ----

FVector ULureFishingLineComponent::GetEndPoint() const
{
	return Sim.HasState() ? Sim.GetEnd() : EndInput;
}

FVector ULureFishingLineComponent::GetEndDirection() const
{
	return Sim.GetEndDirection();
}

FVector ULureFishingLineComponent::GetStartPoint() const
{
	return Sim.HasState() ? Sim.GetStart() : ResolveTip();
}

// ---- Simulation ----

void ULureFishingLineComponent::StartLine(ELureLineMode NewMode)
{
	if (!Sim.IsInitialized())
	{
		Sim.Init(LureFishingLinePrivate::DefaultSegments);
		Widths.SetNumZeroed(Sim.GetNumSegments() + 1);
	}
	Mode = NewMode;
	bNeedsReset = true;
	ReserveColliders();
	bCollidersValid = false; // a new line gathers its solids again (T-032b)
	RecoilElapsed = 0.f;
	RestLength = 0.f;
	TargetRestLength = 0.f;
	LastChord = 0.f;
	bWaterLookedUp = false;
	SetComponentTickEnabled(true);
	UpdateLine(0.f); // valid points (and a drawn line) right away
}

void ULureFishingLineComponent::StopLine()
{
	Mode = ELureLineMode::None;
	bNeedsReset = true;
	bCollidersValid = false;
	Colliders.Reset(); // keeps the memory
	RecoilElapsed = 0.f;
	RestLength = 0.f;
	TargetRestLength = 0.f;
	LastChord = 0.f;
	bWaterLookedUp = false;
	EndActor.Reset();
	SetComponentTickEnabled(false);
	for (USplineMeshComponent* Segment : Segments)
	{
		if (Segment && Segment->IsVisible())
		{
			Segment->SetVisibility(false);
		}
	}
	bLineVisible = false;
}

void ULureFishingLineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Mode == ELureLineMode::None)
	{
		SetComponentTickEnabled(false);
		return;
	}
	UpdateLine(DeltaTime);
}

void ULureFishingLineComponent::UpdateLine(float DeltaTime)
{
	if (Mode == ELureLineMode::None || !Sim.IsInitialized())
	{
		return;
	}
	const float Dt = FMath::IsFinite(DeltaTime) ? FMath::Clamp(DeltaTime, 0.f, 1.f) : 0.f;
	if (Mode == ELureLineMode::Recoil)
	{
		RecoilElapsed += Dt;
		if (RecoilElapsed >= GetTuning().RecoilTime)
		{
			StopLine();
			return;
		}
	}
	if (Mode == ELureLineMode::Hanging && !EndActor.IsValid())
	{
		// The hanging actor was destroyed: back to the owner's end, or the line is gone.
		EndActor.Reset();
		if (!bOut)
		{
			StopLine();
			return;
		}
		Mode = ELureLineMode::Pinned;
	}
	Simulate(Dt);
	MoveEndActor();
	Draw();
}

FVector ULureFishingLineComponent::ResolveTip() const
{
	if (StartProvider.IsBound())
	{
		const FVector Tip = StartProvider.Execute();
		if (!Tip.ContainsNaN())
		{
			return Tip;
		}
	}
	return TipInput;
}

FVector ULureFishingLineComponent::HookPointOf(const AActor& Actor) const
{
	return Actor.GetActorLocation() + Actor.GetActorQuat().RotateVector(HookOffset);
}

bool ULureFishingLineComponent::ResolveWater(const FVector& Near, float& OutWaterZ)
{
	if (bWaterOverride)
	{
		OutWaterZ = WaterOverrideZ;
		return true;
	}
	// Looked up once, then again only when the end moved WaterRefreshDistance (the lookup walks the level's actors).
	const FVector2D XY(Near.X, Near.Y);
	if (!bWaterLookedUp || FVector2D::DistSquared(XY, LookupXY) > FMath::Square(static_cast<double>(GetTuning().WaterRefreshDistance)))
	{
		bWaterLookedUp = true;
		LookupXY = XY;
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		bLookedUpHasWater = Settings && FLureFishingSpots::FindWaterSurfaceZ(GetWorld(), XY, *Settings, LookedUpWaterZ);
	}
	OutWaterZ = LookedUpWaterZ;
	return bLookedUpHasWater;
}

void ULureFishingLineComponent::Simulate(float DeltaTime)
{
	const FLureFishingLineRow& Row = GetTuning();
	FLureLineSimInput In;
	In.Start = ResolveTip();
	switch (Mode)
	{
	case ELureLineMode::Pinned:
	{
		In.End = EndInput;
		const float Chord = static_cast<float>(FVector::Dist(In.Start, In.End));
		TargetRestLength = FLureFishingLineRules::TargetRestLength(Chord, Tension, Slack, Row);
		// Carried to the ends' new distance first (the same share of slack), so ends that close in leave no extra line (T-032b).
		RestLength = FLureFishingLineRules::CarryRestLength(RestLength, LastChord, Chord);
		RestLength = FLureFishingLineRules::TightenRestLength(RestLength, TargetRestLength, Chord, DeltaTime, Row);
		LastChord = Chord;
		In.Float = FLureFishingLineRules::FloatAmount(Tension, Row);
		break;
	}
	case ELureLineMode::Hanging:
	{
		const AActor* Actor = EndActor.Get();
		In.End = (bNeedsReset && Actor) ? HookPointOf(*Actor) : Sim.GetEnd();
		In.bFreeEnd = true;
		In.EndMass = Row.HangEndMass;
		In.EndDrag = Row.HangDrag;
		const float Current = bNeedsReset ? static_cast<float>(FVector::Dist(In.Start, In.End)) : RestLength;
		TargetRestLength = HangLength;
		// Reeled in at HangReelSpeed, slowing at half of gravity, and the swing guard: the actor ends up under the tip (T-032b).
		RestLength = FLureFishingLineRules::ReelInRestLength(Current, HangLength, DeltaTime, Row);
		In.MaxSwingDeg = Row.HangMaxSwingDeg;
		In.Float = Row.FloatStrength;
		LastChord = 0.f;
		break;
	}
	case ELureLineMode::Recoil:
	{
		In.End = Sim.GetEnd();
		In.bFreeEnd = true;
		In.EndMass = 1.f;
		const float Alpha = FMath::SmoothStep(0.f, 1.f, RecoilElapsed / FMath::Max(0.05f, Row.RecoilTime));
		RestLength = RecoilStartLength * FMath::Lerp(1.f, Row.RecoilLengthShare, Alpha);
		TargetRestLength = RecoilStartLength * Row.RecoilLengthShare;
		In.Float = Row.FloatStrength;
		LastChord = 0.f;
		break;
	}
	case ELureLineMode::None:
	default:
		return;
	}
	In.RestLength = RestLength;
	float WaterZ = 0.f;
	In.bHasWater = ResolveWater(In.End, WaterZ);
	In.WaterZ = WaterZ;
	UpdateColliders(In.Start, In.End, DeltaTime); // T-032b: what the line lies on and bends around (none while an actor hangs)
	In.Colliders = (Mode == ELureLineMode::Hanging || Colliders.IsEmpty()) ? nullptr : &Colliders;
	if (bNeedsReset)
	{
		Sim.Reset(In);
		bNeedsReset = false;
	}
	Sim.Step(DeltaTime, In, Row);
}

// ---- Collision (T-032b) ----

void ULureFishingLineComponent::ReserveColliders()
{
	// About one pier's worth (a deck, a few dozen posts); more only grows on a gather frame, never in the steady state.
	Colliders.Reserve(512, 64, 32);
	Overlaps.Reserve(64);
	PlaneScratch.Reserve(64);
}

void ULureFishingLineComponent::UpdateColliders(const FVector& Tip, const FVector& End, float DeltaTime)
{
	if (Mode == ELureLineMode::Hanging)
	{
		// A hanging actor's line does not collide: it can start under a dock, and the reel would trap it there. Gathered afresh
		// when the line pins again.
		if (bCollidersValid || !Colliders.IsEmpty())
		{
			bCollidersValid = false;
			Colliders.Reset();
		}
		return;
	}
	const FLureFishingLineRow& Row = GetTuning();
	CollisionClock += FMath::Max(0.f, DeltaTime);
	FBox LineBox(ForceInit);
	LineBox += Tip;
	LineBox += End;
	if (!bNeedsReset && Sim.HasState())
	{
		for (const FVector& Point : Sim.GetPoints())
		{
			LineBox += Point;
		}
	}
	const double Radius = FMath::Clamp(static_cast<double>(Row.CollisionRadius), 0.0, 50.0);
	const double Margin = FMath::Max(10.0, static_cast<double>(Row.CollisionQueryMargin));
	// Gathered again before the line can reach past the box this frame (it moves at most min(Margin / 2, 50 cm) per frame here).
	const FBox Needed = LineBox.ExpandBy(FMath::Min(0.5 * Margin, 50.0) + Radius);
	const bool bRefreshDue = bMovableColliders && CollisionClock >= Row.CollisionRefreshTime;
	if (bCollidersValid && !bRefreshDue && QueryBox.IsInsideOrOn(Needed))
	{
		return;
	}
	QueryBox = LineBox.ExpandBy(Margin + Radius);
	CollisionClock = 0.f;
	bCollidersValid = true;
	bMovableColliders = false;
	Colliders.Reset();
	Overlaps.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureFishingLineSolids), false, GetOwner());
	if (const AActor* Hanging = EndActor.Get())
	{
		Params.AddIgnoredActor(Hanging);
	}
	World->OverlapMultiByChannel(Overlaps, QueryBox.GetCenter(), FQuat::Identity, FLureFishingSpots::CastChannel,
		FCollisionShape::MakeBox(QueryBox.GetExtent()), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component || !FLureFishingSpots::BlocksCast(FHitResult(Overlap.GetActor(), Component, Component->GetComponentLocation(), FVector::UpVector)))
		{
			continue; // the line collides with exactly what stops a cast
		}
		AddCollidersOf(*Component, Overlap.GetItemIndex());
		bMovableColliders |= Component->GetMobility() == EComponentMobility::Movable;
	}
}

void ULureFishingLineComponent::AddCollidersOf(UPrimitiveComponent& Component, int32 Item)
{
	if (Component.IsA<USkinnedMeshComponent>())
	{
		return; // animated bodies are not in one body setup (creatures are pawns: never solid for a cast anyway)
	}
	FTransform World = Component.GetComponentTransform();
	const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(&Component);
	if (Instanced && (Item == INDEX_NONE || !Instanced->GetInstanceTransform(Item, World, /*bWorldSpace*/ true)))
	{
		return;
	}
	UBodySetup* Body = Component.GetBodySetup();
	if (!Body)
	{
		return; // no body setup at all (a landscape's heightfield): not collided yet (spec: Known limits)
	}
	if (Body->GetCollisionTraceFlag() == CTF_UseComplexAsSimple || Body->AggGeom.GetElementCount() == 0)
	{
		// No simple shapes (complex collision only): its bounds stand in (see FLureLineColliders::FConvex::bBoundsOnly).
		const UStaticMesh* InstancedMesh = Instanced ? Instanced->GetStaticMesh() : nullptr;
		Colliders.AddBoundsBox(InstancedMesh ? InstancedMesh->GetBounds().GetBox().TransformBy(World) : Component.Bounds.GetBox());
		return;
	}
	const FKAggregateGeom& Geometry = Body->AggGeom;
	for (const FKBoxElem& Box : Geometry.BoxElems)
	{
		Colliders.AddBox(FVector(Box.X, Box.Y, Box.Z) * 0.5, Box.GetTransform(), World); // X, Y, Z are full sizes
	}
	for (const FKConvexElem& Convex : Geometry.ConvexElems)
	{
		PlaneScratch.Reset();
		Convex.GetPlanes(PlaneScratch);
		if (!Colliders.AddConvex(PlaneScratch, Convex.ElemBox, Convex.GetTransform(), World) && Convex.ElemBox.IsValid)
		{
			// No cooked hull here: its box stands in.
			Colliders.AddBox(Convex.ElemBox.GetExtent(), FTransform(Convex.ElemBox.GetCenter()) * Convex.GetTransform(), World);
		}
	}
	// Rounded shapes as the physics scales them (a sphere by its smallest axis scale, a capsule's radius by its largest XY scale).
	const FTransform Placement(World.GetRotation(), World.GetTranslation());
	for (const FKSphereElem& Sphere : Geometry.SphereElems)
	{
		const FKSphereElem Scaled = Sphere.GetFinalScaled(World.GetScale3D(), FTransform::Identity);
		Colliders.AddSphere(Placement.TransformPosition(Scaled.Center), Scaled.Radius);
	}
	for (const FKSphylElem& Capsule : Geometry.SphylElems)
	{
		const FKSphylElem Scaled = Capsule.GetFinalScaled(World.GetScale3D(), FTransform::Identity);
		const FVector Half = Scaled.Rotation.RotateVector(FVector(0.0, 0.0, 0.5 * Scaled.Length));
		Colliders.AddCapsule(Placement.TransformPosition(Scaled.Center - Half), Placement.TransformPosition(Scaled.Center + Half), Scaled.Radius);
	}
	// Tapered capsules (skeletal bodies only) are ignored.
}

void ULureFishingLineComponent::MoveEndActor() const
{
	AActor* Actor = EndActor.Get();
	if (Mode != ELureLineMode::Hanging || !Actor)
	{
		return;
	}
	FQuat Rotation = Actor->GetActorQuat();
	if (bOrientEndActor)
	{
		// Head (+X) up the line toward the rod; Y kept as close as possible to where it was, so the actor never spins.
		Rotation = FRotationMatrix::MakeFromXY(Sim.GetEndDirection(), Rotation.GetAxisY()).ToQuat();
	}
	Actor->SetActorLocationAndRotation(Sim.GetEnd() - Rotation.RotateVector(HookOffset), Rotation, false, nullptr, ETeleportType::TeleportPhysics);
}

// ---- Drawing ----

void ULureFishingLineComponent::ResolveViewer(FVector& OutLocation, float& OutFovDeg) const
{
	if (bViewerSet)
	{
		OutLocation = ViewLocation;
		OutFovDeg = ViewFovDeg;
		return;
	}
	OutLocation = Sim.GetStart();
	OutFovDeg = 90.f;
	const UWorld* World = GetWorld();
	const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
	if (Local && Local->IsLocalController() && Local->PlayerCameraManager)
	{
		const FMinimalViewInfo& View = Local->PlayerCameraManager->GetCameraCacheView();
		OutLocation = View.Location;
		OutFovDeg = View.FOV;
	}
}

void ULureFishingLineComponent::Draw()
{
	const TArray<FVector>& Points = Sim.GetPoints();
	if (Widths.Num() != Points.Num())
	{
		Widths.SetNumZeroed(Points.Num()); // only after a new Setup; never in the steady state
	}
	FVector Viewer;
	float Fov = 90.f;
	ResolveViewer(Viewer, Fov);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const float Distance = static_cast<float>(FVector::Dist(Viewer, Points[Index]));
		Widths[Index] = FLureFishingRules::LineWidthAtDistance(PixelWidth, Distance, Fov, ReferenceScreenWidth, MinWidth);
	}

	const int32 NumDrawn = FMath::Min(Segments.Num(), Points.Num() - 1);
	for (int32 Index = 0; Index < NumDrawn; ++Index)
	{
		USplineMeshComponent* Segment = Segments[Index];
		if (!Segment)
		{
			continue;
		}
		const FVector& A = Points[Index];
		const FVector& B = Points[Index + 1];
		// Tangents from the neighbors (Catmull-Rom style), so the line bends smoothly across segments.
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

// ---- Lifetime ----

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
	Widths.Reset();
	Mode = ELureLineMode::None;
	bOut = false;
	bNeedsReset = true;
	bLineVisible = false;
	EndActor.Reset();
	SetComponentTickEnabled(false);
}

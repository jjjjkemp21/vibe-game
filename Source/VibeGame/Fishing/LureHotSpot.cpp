// Lure: a hot spot on the water (T-027). Rules: docs/specs/fishing-water-rules.md.

#include "Fishing/LureHotSpot.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Fishing/FishingWater.h"
#include "Fishing/LureHotSpotVisualComponent.h"
#include "Fishing/LureWaterSettings.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"

namespace LureHotSpotPrivate
{
	/** The anchor as it arrives on clients (FVector_NetQuantize10: 0.1 cm), so every machine drifts from the same point. */
	FVector QuantizeTenth(const FVector& Value)
	{
		return FVector(FMath::RoundToDouble(Value.X * 10.0) / 10.0, FMath::RoundToDouble(Value.Y * 10.0) / 10.0, FMath::RoundToDouble(Value.Z * 10.0) / 10.0);
	}

	UClass* LoadVisualClass(const TSoftClassPtr<ULureHotSpotVisualComponent>& Ref)
	{
		if (Ref.IsNull())
		{
			return nullptr;
		}
		if (UClass* Loaded = Ref.Get())
		{
			return Loaded;
		}
		const FString Package = Ref.ToSoftObjectPath().GetLongPackageName();
		if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
		{
			UE_LOG(LogLureWater, Warning, TEXT("Hot spot visual class '%s' does not exist; using the placeholder."), *Ref.ToString());
			return nullptr;
		}
		return Ref.LoadSynchronous();
	}
}

ALureHotSpot::ALureHotSpot()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true; // a few per level, one small state each; clients see them anywhere
	SetReplicatingMovement(false); // every machine computes the drift from the replicated state
	SetNetUpdateFrequency(1.f); // the state never changes after spawning (ForceNetUpdate sends it at once)
	SetMinNetUpdateFrequency(0.2f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

ALureHotSpot* ALureHotSpot::SpawnHotSpot(UWorld* World, TSubclassOf<ALureHotSpot> Class, FName TypeId, const FLureHotSpotRow& Row, FName AreaId,
	const FVector& Anchor, int32 Seed, double Now, float Lifetime)
{
	if (!World)
	{
		return nullptr;
	}
	UClass* SpawnClass = Class ? Class.Get() : ALureHotSpot::StaticClass();
	FLureHotSpotState NewState;
	NewState.TypeId = TypeId;
	NewState.AreaId = AreaId;
	NewState.Anchor = LureHotSpotPrivate::QuantizeTenth(Anchor);
	NewState.Radius = FMath::Max(1.f, FMath::IsFinite(Row.Radius) ? Row.Radius : 1.f);
	NewState.DriftRange = FMath::IsFinite(Row.DriftRange) ? FMath::Max(0.f, Row.DriftRange) : 0.f;
	NewState.DriftSpeed = FMath::IsFinite(Row.DriftSpeed) ? FMath::Max(0.f, Row.DriftSpeed) : 0.f;
	NewState.Seed = Seed;
	NewState.SpawnTime = Now;
	NewState.EndTime = Now + FMath::Max(1.0, FMath::IsFinite(Lifetime) ? static_cast<double>(Lifetime) : 1.0);
	NewState.Color = Row.VisualColor.ToFColor(/*bSRGB*/ true);
	NewState.VisualClass = Row.VisualClass;

	const FTransform Transform(FVector(NewState.Anchor));
	ALureHotSpot* HotSpot = World->SpawnActorDeferred<ALureHotSpot>(SpawnClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!HotSpot)
	{
		return nullptr;
	}
	HotSpot->InitHotSpot(NewState, FLureWaterRules::BonusFromRow(TypeId, Row));
	HotSpot->FinishSpawning(Transform);
	HotSpot->ForceNetUpdate();
	UE_LOG(LogLureWater, Verbose, TEXT("Hot spot %s (%s) at (%.0f, %.0f) in %s for %.0f s."), *HotSpot->GetName(), *TypeId.ToString(),
		Anchor.X, Anchor.Y, AreaId.IsNone() ? TEXT("default water") : *AreaId.ToString(), NewState.EndTime - NewState.SpawnTime);
	return HotSpot;
}

void ALureHotSpot::InitHotSpot(const FLureHotSpotState& InState, const FLureHotSpotBonus& InBonus)
{
	State = InState;
	Bonus = InBonus;
	if (HasActorBegunPlay())
	{
		EnsureVisual();
		UpdateLocation();
	}
}

double ALureHotSpot::GetTime() const
{
	return FLureWaterQuery::GetTime(GetWorld());
}

FVector ALureHotSpot::GetCenterAt(double Time) const
{
	const double Age = FMath::Max(0.0, Time - State.SpawnTime);
	const FVector2D Offset = FLureWaterRules::DriftOffset(State.DriftRange, State.DriftSpeed, State.Seed, Age);
	return FVector(State.Anchor) + FVector(Offset.X, Offset.Y, 0.0);
}

bool ALureHotSpot::IsActiveAt(double Time) const
{
	return State.EndTime > State.SpawnTime && Time >= State.SpawnTime && Time < State.EndTime;
}

bool ALureHotSpot::ContainsAt(const FVector2D& XY, double Time) const
{
	if (!IsActiveAt(Time))
	{
		return false;
	}
	const FVector Center = GetCenterAt(Time);
	return FVector2D::DistSquared(XY, FVector2D(Center.X, Center.Y)) <= static_cast<double>(State.Radius) * static_cast<double>(State.Radius);
}

float ALureHotSpot::GetFadeAt(double Time) const
{
	if (!IsActiveAt(Time))
	{
		return 0.f;
	}
	const double Fade = static_cast<double>(GetDefault<ULureWaterSettings>()->HotSpotFadeSeconds);
	if (!(Fade > 0.0))
	{
		return 1.f;
	}
	const double In = (Time - State.SpawnTime) / Fade;
	const double Out = (State.EndTime - Time) / Fade;
	return static_cast<float>(FMath::Clamp(FMath::Min(In, Out), 0.0, 1.0));
}

void ALureHotSpot::BeginPlay()
{
	Super::BeginPlay();
	UpdateLocation();
	EnsureVisual();
}

void ALureHotSpot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLocation();
	if (HasAuthority() && State.EndTime > 0.0 && GetTime() >= State.EndTime)
	{
		Destroy(); // replication removes it on clients
	}
}

void ALureHotSpot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALureHotSpot, State);
}

void ALureHotSpot::OnRep_State()
{
	if (HasActorBegunPlay())
	{
		EnsureVisual();
		UpdateLocation();
	}
}

void ALureHotSpot::UpdateLocation()
{
	if (State.EndTime <= 0.0)
	{
		return; // no state yet (a client before the first update)
	}
	SetActorLocation(GetCenterAt(GetTime()), /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
}

void ALureHotSpot::EnsureVisual()
{
	if (GetNetMode() == NM_DedicatedServer || State.EndTime <= 0.0)
	{
		return;
	}
	if (!Visual)
	{
		UClass* VisualClass = LureHotSpotPrivate::LoadVisualClass(State.VisualClass);
		if (!VisualClass || !VisualClass->IsChildOf(ULureHotSpotVisualComponent::StaticClass()))
		{
			VisualClass = ULureHotSpotVisualComponent::StaticClass();
		}
		Visual = NewObject<ULureHotSpotVisualComponent>(this, VisualClass, NAME_None, RF_Transient);
		Visual->SetupAttachment(Root);
		Visual->RegisterComponent();
	}
	Visual->InitFromHotSpot(State);
}

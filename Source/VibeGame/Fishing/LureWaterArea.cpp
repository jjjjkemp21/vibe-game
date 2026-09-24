// Lure: a painted water area (T-027). Rules: docs/specs/fishing-water-rules.md.

#include "Fishing/LureWaterArea.h"
#include "Components/LineBatchComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagsManager.h"

ALureWaterArea::ALureWaterArea()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // level data: every machine loads it with the map

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	Outline = CreateDefaultSubobject<ULineBatchComponent>(TEXT("Outline"));
	Outline->SetupAttachment(Root);
	Outline->bIsEditorOnly = true;
	Outline->SetHiddenInGame(true);
	Outline->PrimaryComponentTick.bCanEverTick = false; // persistent lines: nothing to age
	Outline->bTickInEditor = false;
}

bool ALureWaterArea::SetAreaTags(FName Habitat, FName Region)
{
	bool bOk = true;
	HabitatTag = UGameplayTagsManager::Get().RequestGameplayTag(Habitat, /*ErrorIfNotFound*/ false);
	if (!HabitatTag.IsValid())
	{
		UE_LOG(LogLureWater, Warning, TEXT("Water area %s: habitat '%s' is not a registered gameplay tag (Config/Tags/*.ini); the area is skipped until it has one."),
			*GetName(), *Habitat.ToString());
		bOk = false;
	}
	RegionTag = FGameplayTag();
	if (!Region.IsNone() && !Region.ToString().IsEmpty())
	{
		RegionTag = UGameplayTagsManager::Get().RequestGameplayTag(Region, /*ErrorIfNotFound*/ false);
		if (!RegionTag.IsValid())
		{
			UE_LOG(LogLureWater, Warning, TEXT("Water area %s: region '%s' is not a registered gameplay tag; the default region is used."), *GetName(), *Region.ToString());
			bOk = false;
		}
	}
	RefreshOutline();
	return bOk;
}

void ALureWaterArea::SetShapeCircle(float InRadius)
{
	Shape = ELureWaterAreaShape::Circle;
	Radius = InRadius;
	RefreshOutline();
}

void ALureWaterArea::SetShapeBox(FVector2D HalfSize)
{
	Shape = ELureWaterAreaShape::Box;
	BoxHalfSize = HalfSize;
	RefreshOutline();
}

int32 ALureWaterArea::SetShapePolygon(const TArray<FVector2D>& WorldPoints)
{
	Shape = ELureWaterAreaShape::Polygon;
	PolygonPoints.Reset(WorldPoints.Num());
	const FVector Location = GetActorLocation();
	const FRotator Yaw(0.0, GetActorRotation().Yaw, 0.0);
	for (const FVector2D& Point : WorldPoints)
	{
		const FVector Local = Yaw.UnrotateVector(FVector(Point.X - Location.X, Point.Y - Location.Y, 0.0));
		PolygonPoints.Add(FVector2D(Local.X, Local.Y));
	}
	RefreshOutline();
	return PolygonPoints.Num();
}

void ALureWaterArea::SetShapeEverywhere()
{
	Shape = ELureWaterAreaShape::Everywhere;
	RefreshOutline();
}

FLureWaterAreaInfo ALureWaterArea::GetWaterArea() const
{
	FLureWaterAreaInfo Area;
	Area.AreaId = AreaId.IsNone() ? GetFName() : AreaId;
	Area.DisplayName = DisplayName;
	Area.HabitatTag = HabitatTag;
	Area.RegionTag = RegionTag;
	Area.Priority = Priority;
	Area.Luck = Luck;
	Area.MinDepth = MinDepth;
	Area.MaxDepth = MaxDepth;
	Area.Shape = Shape;
	const FVector Location = GetActorLocation();
	Area.Center = FVector2D(Location.X, Location.Y);
	Area.YawDegrees = static_cast<float>(GetActorRotation().Yaw);
	Area.Radius = Radius;
	Area.HalfSize = BoxHalfSize;
	if (Shape == ELureWaterAreaShape::Polygon)
	{
		const FRotator Yaw(0.0, GetActorRotation().Yaw, 0.0);
		Area.Polygon.Reserve(PolygonPoints.Num());
		for (const FVector2D& Local : PolygonPoints)
		{
			const FVector World = Location + Yaw.RotateVector(FVector(Local.X, Local.Y, 0.0));
			Area.Polygon.Add(FVector2D(World.X, World.Y));
		}
	}
	Area.Source = ELureWaterSource::Area;
	return Area;
}

TArray<FVector> ALureWaterArea::GetOutlinePoints(float Z) const
{
	TArray<FVector> Points;
	const FLureWaterAreaInfo Area = GetWaterArea();
	if (!Area.HasShape())
	{
		return Points;
	}
	switch (Area.Shape)
	{
	case ELureWaterAreaShape::Circle:
		for (int32 Index = 0; Index < 64; ++Index)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Index / 64.0;
			Points.Add(FVector(Area.Center.X + Area.Radius * FMath::Cos(Angle), Area.Center.Y + Area.Radius * FMath::Sin(Angle), Z));
		}
		break;
	case ELureWaterAreaShape::Box:
	{
		const FRotator Yaw(0.0, Area.YawDegrees, 0.0);
		for (const FVector2D& Corner : { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(-1, -1), FVector2D(1, -1) })
		{
			const FVector Offset = Yaw.RotateVector(FVector(Corner.X * Area.HalfSize.X, Corner.Y * Area.HalfSize.Y, 0.0));
			Points.Add(FVector(Area.Center.X + Offset.X, Area.Center.Y + Offset.Y, Z));
		}
		break;
	}
	case ELureWaterAreaShape::Polygon:
		for (const FVector2D& Point : Area.Polygon)
		{
			Points.Add(FVector(Point.X, Point.Y, Z));
		}
		break;
	default:
		break;
	}
	return Points;
}

FLinearColor ALureWaterArea::GetHabitatColor(const FGameplayTag& Habitat)
{
	if (!Habitat.IsValid())
	{
		return FLinearColor::Red;
	}
	const uint32 Hash = GetTypeHash(Habitat.GetTagName().ToString());
	const uint8 Hue = static_cast<uint8>(Hash & 0xFF);
	return FLinearColor::MakeFromHSV8(Hue, 200, 255);
}

void ALureWaterArea::RefreshOutline()
{
	if (!Outline)
	{
		return;
	}
	Outline->Flush();
	const float Z = static_cast<float>(GetActorLocation().Z) + 5.f;
	const FLinearColor Color = GetHabitatColor(HabitatTag);
	const TArray<FVector> Points = GetOutlinePoints(Z);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Outline->DrawLine(Points[Index], Points[(Index + 1) % Points.Num()], Color, SDPG_World, /*Thickness*/ 8.f, /*LifeTime*/ 0.f);
	}
	// The anchor: a small cross (the only mark of an Everywhere area).
	const FVector Center(GetActorLocation().X, GetActorLocation().Y, Z);
	Outline->DrawLine(Center - FVector(150.f, 0.f, 0.f), Center + FVector(150.f, 0.f, 0.f), Color, SDPG_World, 8.f, 0.f);
	Outline->DrawLine(Center - FVector(0.f, 150.f, 0.f), Center + FVector(0.f, 150.f, 0.f), Color, SDPG_World, 8.f, 0.f);
}

void ALureWaterArea::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshOutline();
}

void ALureWaterArea::PostLoad()
{
	Super::PostLoad();
	RefreshOutline(); // the lines are not saved with the map
}

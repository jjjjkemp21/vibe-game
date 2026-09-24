// Lure: water areas and hot spots (T-027). Rules: docs/specs/fishing-water-rules.md.

#include "Fishing/FishingWaterTypes.h"
#include "GameplayTagsManager.h"

DEFINE_LOG_CATEGORY(LogLureWater);

namespace LureWaterTypesPrivate
{
	/** Twice the signed area of a polygon (shoelace); 0 for fewer than 3 points. */
	double TwiceSignedArea(const TArray<FVector2D>& Points)
	{
		const int32 Count = Points.Num();
		if (Count < 3)
		{
			return 0.0;
		}
		double Sum = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& A = Points[Index];
			const FVector2D& B = Points[(Index + 1) % Count];
			Sum += A.X * B.Y - B.X * A.Y;
		}
		return Sum;
	}

	bool IsFinite2D(const FVector2D& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y);
	}

	/** XY in the box's own frame (the box turned by YawDegrees around Center). */
	FVector2D ToBoxFrame(const FVector2D& XY, const FVector2D& Center, float YawDegrees)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(YawDegrees));
		const double Cos = FMath::Cos(Radians);
		const double Sin = FMath::Sin(Radians);
		const FVector2D D = XY - Center;
		return FVector2D(D.X * Cos + D.Y * Sin, -D.X * Sin + D.Y * Cos);
	}
}

bool FLureWaterAreaInfo::HasShape() const
{
	using namespace LureWaterTypesPrivate;
	switch (Shape)
	{
	case ELureWaterAreaShape::Everywhere:
		return true;
	case ELureWaterAreaShape::Circle:
		return FMath::IsFinite(Radius) && Radius > 0.f && IsFinite2D(Center);
	case ELureWaterAreaShape::Box:
		return IsFinite2D(HalfSize) && HalfSize.X > 0.0 && HalfSize.Y > 0.0 && IsFinite2D(Center) && FMath::IsFinite(YawDegrees);
	case ELureWaterAreaShape::Polygon:
	{
		if (Polygon.Num() < 3)
		{
			return false;
		}
		for (const FVector2D& Point : Polygon)
		{
			if (!IsFinite2D(Point))
			{
				return false;
			}
		}
		return FMath::Abs(TwiceSignedArea(Polygon)) > UE_KINDA_SMALL_NUMBER;
	}
	default:
		return false;
	}
}

bool FLureWaterAreaInfo::Contains(const FVector2D& XY) const
{
	using namespace LureWaterTypesPrivate;
	if (!HasShape() || !IsFinite2D(XY))
	{
		return false;
	}
	switch (Shape)
	{
	case ELureWaterAreaShape::Everywhere:
		return true;
	case ELureWaterAreaShape::Circle:
		return FVector2D::DistSquared(XY, Center) <= static_cast<double>(Radius) * static_cast<double>(Radius);
	case ELureWaterAreaShape::Box:
	{
		const FVector2D Local = ToBoxFrame(XY, Center, YawDegrees);
		return FMath::Abs(Local.X) <= HalfSize.X && FMath::Abs(Local.Y) <= HalfSize.Y;
	}
	case ELureWaterAreaShape::Polygon:
	{
		// Even-odd crossing test (a horizontal ray to +X). Half-open on Y, so a vertex on the ray is counted once.
		bool bInside = false;
		const int32 Count = Polygon.Num();
		for (int32 I = 0, J = Count - 1; I < Count; J = I++)
		{
			const FVector2D& A = Polygon[I];
			const FVector2D& B = Polygon[J];
			if ((A.Y > XY.Y) != (B.Y > XY.Y))
			{
				const double CrossX = A.X + (XY.Y - A.Y) * (B.X - A.X) / (B.Y - A.Y);
				if (XY.X < CrossX)
				{
					bInside = !bInside;
				}
			}
		}
		return bInside;
	}
	default:
		return false;
	}
}

bool FLureWaterAreaInfo::AcceptsDepth(float DepthCm) const
{
	if (!FMath::IsFinite(DepthCm))
	{
		return false;
	}
	const float Min = FMath::IsFinite(MinDepth) ? FMath::Max(0.f, MinDepth) : 0.f;
	if (DepthCm < Min)
	{
		return false;
	}
	return !(FMath::IsFinite(MaxDepth) && MaxDepth > 0.f) || DepthCm < MaxDepth;
}

double FLureWaterAreaInfo::GetSize() const
{
	using namespace LureWaterTypesPrivate;
	if (!HasShape())
	{
		return 0.0;
	}
	switch (Shape)
	{
	case ELureWaterAreaShape::Everywhere:
		return TNumericLimits<double>::Max();
	case ELureWaterAreaShape::Circle:
		return UE_DOUBLE_PI * static_cast<double>(Radius) * static_cast<double>(Radius);
	case ELureWaterAreaShape::Box:
		return 4.0 * HalfSize.X * HalfSize.Y;
	case ELureWaterAreaShape::Polygon:
		return 0.5 * FMath::Abs(TwiceSignedArea(Polygon));
	default:
		return 0.0;
	}
}

FBox2D FLureWaterAreaInfo::GetBounds() const
{
	FBox2D Bounds(ForceInit);
	if (!HasShape())
	{
		return Bounds;
	}
	switch (Shape)
	{
	case ELureWaterAreaShape::Circle:
		Bounds += Center - FVector2D(Radius, Radius);
		Bounds += Center + FVector2D(Radius, Radius);
		break;
	case ELureWaterAreaShape::Box:
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(YawDegrees));
		const FVector2D AxisX(FMath::Cos(Radians), FMath::Sin(Radians));
		const FVector2D AxisY(-AxisX.Y, AxisX.X);
		for (const double SX : { -1.0, 1.0 })
		{
			for (const double SY : { -1.0, 1.0 })
			{
				Bounds += Center + AxisX * (SX * HalfSize.X) + AxisY * (SY * HalfSize.Y);
			}
		}
		break;
	}
	case ELureWaterAreaShape::Polygon:
		for (const FVector2D& Point : Polygon)
		{
			Bounds += Point;
		}
		break;
	default:
		break; // Everywhere: unbounded
	}
	return Bounds;
}

FString FLureWaterAreaInfo::GetLabel() const
{
	return DisplayName.IsEmpty() ? AreaId.ToString() : DisplayName;
}

TArray<FString> FLureHotSpotRow::Validate(FName RowId) const
{
	TArray<FString> Problems;
	const FString Row = RowId.ToString();
	auto Problem = [&Problems, &Row](const FString& Text)
	{
		Problems.Add(FString::Printf(TEXT("DT_HotSpot %s: %s"), *Row, *Text));
	};
	const float Numbers[] = { SpawnInterval, MinSpacing, Radius, LifetimeMin, LifetimeMax, DriftSpeed, DriftRange, MinDepth, MaxDepth,
		LuckBonus, SizeBonus, ValueMultiplier, BiteWaitScale };
	for (const float Value : Numbers)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			Problem(TEXT("every number must be finite and >= 0"));
			break;
		}
	}
	if (MaxPerArea < 0)
	{
		Problem(FString::Printf(TEXT("MaxPerArea %d must be >= 0"), MaxPerArea));
	}
	if (!(Radius >= 1.f))
	{
		Problem(FString::Printf(TEXT("Radius %.1f must be >= 1 cm"), Radius));
	}
	if (!(LifetimeMin >= 1.f) || !(LifetimeMax >= LifetimeMin))
	{
		Problem(FString::Printf(TEXT("need 1 <= LifetimeMin (%.1f) <= LifetimeMax (%.1f)"), LifetimeMin, LifetimeMax));
	}
	if (MaxDepth > 0.f && !(MaxDepth > MinDepth))
	{
		Problem(FString::Printf(TEXT("MaxDepth %.0f must be 0 (no limit) or > MinDepth %.0f"), MaxDepth, MinDepth));
	}
	if (SizeBonus > 1.f)
	{
		Problem(FString::Printf(TEXT("SizeBonus %.2f must be in [0, 1]"), SizeBonus));
	}
	if (!(ValueMultiplier > 0.f) || !(BiteWaitScale > 0.f))
	{
		Problem(TEXT("ValueMultiplier and BiteWaitScale must be > 0"));
	}
	if (DriftSpeed > 0.f && DriftRange <= 0.f)
	{
		Problem(TEXT("DriftSpeed > 0 needs a DriftRange > 0 (or set DriftSpeed 0 for a still hot spot)"));
	}
	if (DisplayName.IsEmpty() || HudText.IsEmpty())
	{
		Problem(TEXT("DisplayName and HudText must be set"));
	}
	const FGameplayTag HabitatRoot = FGameplayTag::RequestGameplayTag(TEXT("Habitat"), /*ErrorIfNotFound*/ false);
	for (const FGameplayTag& Tag : AllowedHabitats)
	{
		if (!Tag.IsValid())
		{
			Problem(TEXT("AllowedHabitats has an empty or unregistered tag"));
		}
		else if (HabitatRoot.IsValid() && !Tag.MatchesTag(HabitatRoot))
		{
			Problem(FString::Printf(TEXT("AllowedHabitats tag %s is not a Habitat.* tag"), *Tag.ToString()));
		}
	}
	return Problems;
}

bool FLureHotSpotRow::AllowsWater(const FGameplayTag& Habitat, float DepthCm) const
{
	if (!FMath::IsFinite(DepthCm) || DepthCm < MinDepth || (MaxDepth > 0.f && DepthCm >= MaxDepth))
	{
		return false;
	}
	if (AllowedHabitats.Num() == 0)
	{
		return true;
	}
	for (const FGameplayTag& Allowed : AllowedHabitats)
	{
		if (Allowed.IsValid() && Habitat.IsValid() && Habitat.MatchesTag(Allowed))
		{
			return true;
		}
	}
	return false;
}

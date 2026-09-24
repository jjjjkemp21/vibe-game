// Lure: things the player can use with the two use keys.

#include "Interaction/LureInteractable.h"
#include "Character/LureCharacterSettings.h"
#include "GameFramework/Pawn.h"

bool ILureInteractable::IsInInteractionRange(const APawn* Pawn, float Slack) const
{
	if (!Pawn)
	{
		return false;
	}
	const float Radius = FMath::Max(0.0f, GetInteractionRadius()) + FMath::Max(0.0f, Slack);
	return FVector::DistSquared(Pawn->GetActorLocation(), GetInteractionLocation()) <= FMath::Square(static_cast<double>(Radius));
}

float ILureInteractable::GetFocusAngle(const FVector& ViewLocation, const FVector& ViewDirection) const
{
	return AngleToSphere(ViewLocation, ViewDirection, GetInteractionLocation(), GetFocusRadius());
}

float ILureInteractable::AngleToSphere(const FVector& ViewLocation, const FVector& ViewDirection, const FVector& Center, float Radius)
{
	const FVector ToCenter = Center - ViewLocation;
	const double Distance = ToCenter.Size();
	const double SafeRadius = FMath::Max(0.0, static_cast<double>(FMath::IsFinite(Radius) ? Radius : 0.0f));
	if (Distance <= SafeRadius || Distance < UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	const FVector Direction = ViewDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 180.0f;
	}
	const double Cos = FMath::Clamp(FVector::DotProduct(ToCenter / Distance, Direction), -1.0, 1.0);
	const double Angle = FMath::RadiansToDegrees(FMath::Acos(Cos));
	const double AngularRadius = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SafeRadius / Distance, 0.0, 1.0)));
	return static_cast<float>(FMath::Max(0.0, Angle - AngularRadius));
}

float ILureInteractable::AngleToBox(const FVector& ViewLocation, const FVector& ViewDirection, const FTransform& BoxTransform, const FVector& HalfExtent)
{
	const FVector Extent = HalfExtent.ComponentMax(FVector::ZeroVector);
	const FVector Origin = BoxTransform.InverseTransformPositionNoScale(ViewLocation);
	const FVector Direction = BoxTransform.InverseTransformVectorNoScale(ViewDirection).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 180.0f;
	}

	// Slab test: does the ray (t >= 0) pass through the box?
	double TMin = 0.0;
	double TMax = TNumericLimits<double>::Max();
	bool bHit = true;
	for (int32 Axis = 0; Axis < 3 && bHit; ++Axis)
	{
		const double O = Origin[Axis];
		const double D = Direction[Axis];
		const double Lo = -Extent[Axis];
		const double Hi = Extent[Axis];
		if (FMath::Abs(D) < 1.0e-9)
		{
			bHit = O >= Lo && O <= Hi;
			continue;
		}
		double T1 = (Lo - O) / D;
		double T2 = (Hi - O) / D;
		if (T1 > T2)
		{
			Swap(T1, T2);
		}
		TMin = FMath::Max(TMin, T1);
		TMax = FMath::Min(TMax, T2);
		bHit = TMin <= TMax;
	}
	if (bHit)
	{
		return 0.0f;
	}

	// Otherwise the smallest angle to a point of the box: sample the ray's closest box points (convex box, smooth enough).
	const double Reach = Origin.Size() + 2.0 * Extent.Size() + 1.0;
	constexpr int32 Samples = 32;
	double Best = 180.0;
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		const double T = Reach * static_cast<double>(Index) / Samples;
		const FVector OnRay = Origin + Direction * T;
		const FVector Closest(FMath::Clamp(OnRay.X, -Extent.X, Extent.X), FMath::Clamp(OnRay.Y, -Extent.Y, Extent.Y), FMath::Clamp(OnRay.Z, -Extent.Z, Extent.Z));
		const FVector ToPoint = Closest - Origin;
		const double Length = ToPoint.Size();
		if (Length < UE_KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		const double Cos = FMath::Clamp(FVector::DotProduct(ToPoint / Length, Direction), -1.0, 1.0);
		Best = FMath::Min(Best, FMath::RadiansToDegrees(FMath::Acos(Cos)));
	}
	return static_cast<float>(Best);
}

FString ILureInteractable::GetKeyLabel(ELureInteractKey Key)
{
	const ULureCharacterSettings* Settings = GetDefault<ULureCharacterSettings>();
	const TArray<FKey>& Keys = Key == ELureInteractKey::Primary ? Settings->InteractKeys : Settings->AltInteractKeys;
	if (Keys.Num() > 0 && Keys[0].IsValid())
	{
		return Keys[0].GetDisplayName(/*bLongDisplayName*/ false).ToString();
	}
	return Key == ELureInteractKey::Primary ? TEXT("Interact") : TEXT("Alt");
}

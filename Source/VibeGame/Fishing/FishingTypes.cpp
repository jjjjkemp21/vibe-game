// Lure: fishing data types and pure rules (T-006).

#include "Fishing/FishingTypes.h"
#include "Fish/FishRoll.h"

DEFINE_LOG_CATEGORY(LogLureFishing);

const TCHAR* FLureFishingRules::FallbackWarningMarker = TEXT("using the built-in fishing profile");

bool FLureFishingRow::Validate(FString& OutProblem) const
{
	const float Values[] = { ChargeTime, ChargeExponent, MinCastDistance, MaxCastDistance, CastSpeed, CastFlightTimeMin, CastFlightTimeMax,
		CastArcHeightRatio, MaxLineLength, BiteWaitMin, BiteWaitMax, NibbleInterval, NibbleDuration, HookWindow, HookLatencyGrace,
		RebiteWaitMin, RebiteWaitMax, SpookDelay, AutoLandDelay, NoBiteHintDelay, BobberScale, BobberBobAmplitude, BobberBobFrequency,
		BobberTiltDeg, NibbleTiltDeg, BiteDipDepth, BiteDipRate, LinePixelWidth, LineMinWidth, LineSag, BiteRumbleIntensity,
		BiteRumbleDuration, CastSwingBackDeg, CastSwingForwardDeg, CastSwingTime };
	for (const float Value : Values)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			OutProblem = TEXT("every value must be a finite number >= 0");
			return false;
		}
	}
	auto Fail = [&OutProblem](const FString& Problem)
	{
		OutProblem = Problem;
		return false;
	};
	if (ChargeTime <= 0.f || ChargeExponent <= 0.f)
	{
		return Fail(TEXT("ChargeTime and ChargeExponent must be > 0"));
	}
	if (MaxCastDistance < MinCastDistance)
	{
		return Fail(FString::Printf(TEXT("MaxCastDistance %.0f must be >= MinCastDistance %.0f"), MaxCastDistance, MinCastDistance));
	}
	if (CastSpeed <= 0.f || CastFlightTimeMin <= 0.f || CastFlightTimeMax < CastFlightTimeMin)
	{
		return Fail(TEXT("CastSpeed and CastFlightTimeMin must be > 0 and CastFlightTimeMax >= CastFlightTimeMin"));
	}
	if (BiteWaitMax < BiteWaitMin || RebiteWaitMax < RebiteWaitMin)
	{
		return Fail(TEXT("BiteWaitMax >= BiteWaitMin and RebiteWaitMax >= RebiteWaitMin"));
	}
	if (NibblesMin < 0 || NibblesMax < NibblesMin || NibbleInterval <= 0.f || NibbleDuration <= 0.f)
	{
		return Fail(TEXT("0 <= NibblesMin <= NibblesMax, NibbleInterval and NibbleDuration > 0"));
	}
	if (HookWindow <= 0.f)
	{
		return Fail(FString::Printf(TEXT("HookWindow %.2f must be > 0"), HookWindow));
	}
	if (BobberScale <= 0.f || LinePixelWidth <= 0.f || CastSwingTime <= 0.f)
	{
		return Fail(TEXT("BobberScale, LinePixelWidth and CastSwingTime must be > 0"));
	}
	if (LineSegments < 1 || LineSegments > 64)
	{
		return Fail(FString::Printf(TEXT("LineSegments %d must be in [1, 64]"), LineSegments));
	}
	if (BiteRumbleIntensity > 1.f)
	{
		return Fail(TEXT("BiteRumbleIntensity must be in [0, 1]"));
	}
	return true;
}

FLureFishingRow FLureFishingRules::GetFallbackRow()
{
	// The struct defaults ARE the shipped DT_Fishing "Default" row (a test checks they match).
	return FLureFishingRow();
}

float FLureFishingRules::ChargeFromHoldTime(const FLureFishingRow& Row, float HeldSeconds)
{
	if (!FMath::IsFinite(HeldSeconds) || HeldSeconds <= 0.f)
	{
		return 0.f;
	}
	if (!(Row.ChargeTime > 0.f))
	{
		return 1.f;
	}
	return FMath::Clamp(HeldSeconds / Row.ChargeTime, 0.f, 1.f);
}

float FLureFishingRules::CastDistance(const FLureFishingRow& Row, float Charge01)
{
	const float Charge = FMath::IsFinite(Charge01) ? FMath::Clamp(Charge01, 0.f, 1.f) : 0.f;
	const float Exponent = Row.ChargeExponent > 0.f ? Row.ChargeExponent : 1.f;
	const float Min = FMath::Max(0.f, Row.MinCastDistance);
	const float Max = FMath::Max(Min, Row.MaxCastDistance);
	return Min + (Max - Min) * FMath::Pow(Charge, Exponent);
}

float FLureFishingRules::CastFlightTime(const FLureFishingRow& Row, float Distance)
{
	const float Speed = Row.CastSpeed > 0.f ? Row.CastSpeed : 1.f;
	const float Min = FMath::Max(0.05f, Row.CastFlightTimeMin);
	const float Max = FMath::Max(Min, Row.CastFlightTimeMax);
	const float Raw = FMath::IsFinite(Distance) ? FMath::Max(0.f, Distance) / Speed : Min;
	return FMath::Clamp(Raw, Min, Max);
}

FVector FLureFishingRules::CastArcPoint(const FVector& From, const FVector& To, float ArcHeight, float Alpha)
{
	const float T = FMath::IsFinite(Alpha) ? FMath::Clamp(Alpha, 0.f, 1.f) : 1.f;
	return FMath::Lerp(From, To, T) + FVector::UpVector * (ArcHeight * 4.f * T * (1.f - T));
}

double FLureFishingRules::HookDeadline(const FLureFishingRow& Row, double BiteStart, float Grace)
{
	return BiteStart + static_cast<double>(FMath::Max(0.f, Row.HookWindow)) + static_cast<double>(FMath::Max(0.f, Grace));
}

bool FLureFishingRules::IsInHookWindow(const FLureFishingRow& Row, double BiteStart, double Now, float Grace)
{
	return Now >= BiteStart && Now <= HookDeadline(Row, BiteStart, Grace);
}

bool FLureFishingRules::IsRodTuckedByMotion(const FLureMovementRow& Row, float Speed2D)
{
	return FLureRodPose::IsTucked(Row.RodPoseMoving) && FMath::IsFinite(Speed2D) && Speed2D > Row.RodMoveSpeedIn;
}

ELureCastBlock FLureFishingRules::GetCastBlock(const FLureCastConditions& Conditions, const FLureMovementRow& Row)
{
	if (!Conditions.bHasRod)
	{
		return ELureCastBlock::NoRod;
	}
	if (Conditions.bLineOut)
	{
		return ELureCastBlock::Busy;
	}
	if (Conditions.bSwimming)
	{
		return ELureCastBlock::Swimming;
	}
	if (Conditions.bClimbing)
	{
		return ELureCastBlock::Climbing;
	}
	if (Conditions.bFalling)
	{
		return ELureCastBlock::InAir;
	}
	if (!Row.CanFish)
	{
		return ELureCastBlock::Sprinting;
	}
	if (IsRodTuckedByMotion(Row, Conditions.Speed2D))
	{
		return ELureCastBlock::RodTucked;
	}
	return ELureCastBlock::None;
}

ELureCastBlock FLureFishingRules::GetLineCancel(const FLureCastConditions& Conditions, const FLureMovementRow& Row, const FLureFishingRow& Fishing, float BobberDistance2D)
{
	if (!Conditions.bHasRod)
	{
		return ELureCastBlock::NoRod;
	}
	if (Conditions.bSwimming)
	{
		return ELureCastBlock::Swimming;
	}
	if (Conditions.bClimbing)
	{
		return ELureCastBlock::Climbing;
	}
	if (!Row.CanFish)
	{
		return ELureCastBlock::Sprinting;
	}
	if (IsRodTuckedByMotion(Row, Conditions.Speed2D))
	{
		return ELureCastBlock::RodTucked;
	}
	if (Fishing.MaxLineLength > 0.f && FMath::IsFinite(BobberDistance2D) && BobberDistance2D > Fishing.MaxLineLength)
	{
		return ELureCastBlock::TooFar;
	}
	return ELureCastBlock::None;
}

float FLureFishingRules::RandomBiteWait(const FLureFishingRow& Row, FRandomStream& Rng, bool bAfterMiss)
{
	const float Min = FMath::Max(0.f, bAfterMiss ? Row.RebiteWaitMin : Row.BiteWaitMin);
	const float Max = FMath::Max(Min, bAfterMiss ? Row.RebiteWaitMax : Row.BiteWaitMax);
	return Min + (Max - Min) * Rng.FRand();
}

TArray<float> FLureFishingRules::NibbleTimes(const FLureFishingRow& Row, FRandomStream& Rng, float Wait)
{
	TArray<float> Times;
	const int32 Min = FMath::Max(0, Row.NibblesMin);
	const int32 Max = FMath::Max(Min, Row.NibblesMax);
	const int32 Count = Min + (Max > Min ? Rng.RandHelper(Max - Min + 1) : 0);
	const float Interval = FMath::Max(0.05f, Row.NibbleInterval);
	for (int32 Index = Count; Index >= 1; --Index)
	{
		// Nibbles lead up to the bite: the last one ends one interval before it, earlier ones one interval apart (+-20 % jitter).
		const float Jitter = 1.f + 0.2f * (2.f * Rng.FRand() - 1.f);
		const float Time = Wait - Interval * static_cast<float>(Index) * Jitter;
		if (Time > 0.25f && Time + Row.NibbleDuration < Wait)
		{
			Times.Add(Time);
		}
	}
	Times.Sort();
	return Times;
}

bool FLureFishingRules::CanHaveBites(const FLureFishingSpot* Spot, const FLureFishingEnvironment& Environment)
{
	return (Spot && Spot->IsValid()) || Environment.OffSpotHabitatTag.IsValid();
}

FFishRollContext FLureFishingRules::MakeRollContext(const FLureFishingSpot* Spot, const FLureFishingEnvironment& Environment, int32 Seed)
{
	FFishRollContext Context;
	Context.Seed = Seed;
	Context.TimeOfDayHours = Environment.TimeOfDayHours;
	Context.WeatherTag = Environment.WeatherTag;
	Context.BaitTag = Environment.BaitTag;
	const bool bSpot = Spot && Spot->IsValid();
	Context.HabitatTag = bSpot ? Spot->HabitatTag : Environment.OffSpotHabitatTag;
	Context.RegionTag = (bSpot && Spot->RegionTag.IsValid()) ? Spot->RegionTag : Environment.DefaultRegionTag;
	const float SpotLuck = (bSpot && FMath::IsFinite(Spot->Luck)) ? Spot->Luck : 0.f;
	const float GearLuck = FMath::IsFinite(Environment.GearLuck) ? Environment.GearLuck : 0.f;
	Context.Luck = SpotLuck + GearLuck; // the roll clamps to [0, MaxLuck]
	return Context;
}

bool FLureFishingRules::DecideBite(const FFishTables& Tables, const FFishRollContext& Context, FFishInstance& OutFish)
{
	OutFish = FFishInstance();
	FName SpeciesId;
	if (!FFishRoll::PickSpecies(Tables, Context, SpeciesId))
	{
		return false;
	}
	FFishRollContext RollContext = Context;
	RollContext.SpeciesId = SpeciesId;
	return FFishRoll::Roll(Tables, RollContext, OutFish);
}

float FLureFishingRules::LineWidthAtDistance(float PixelWidth, float DistanceCm, float HorizontalFovDeg, float ReferenceScreenWidth, float MinWidth)
{
	const float Fov = (FMath::IsFinite(HorizontalFovDeg) && HorizontalFovDeg > 1.f && HorizontalFovDeg < 179.f) ? HorizontalFovDeg : 90.f;
	const float ScreenWidth = (FMath::IsFinite(ReferenceScreenWidth) && ReferenceScreenWidth > 0.f) ? ReferenceScreenWidth : 1920.f;
	const float Distance = FMath::IsFinite(DistanceCm) ? FMath::Max(0.f, DistanceCm) : 0.f;
	const float Pixels = FMath::IsFinite(PixelWidth) ? FMath::Max(0.f, PixelWidth) : 0.f;
	// Pixels per cm at distance D: (ScreenWidth / 2) / (D * tan(FOV / 2)).
	const float Width = Pixels * Distance * 2.f * FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)) / ScreenWidth;
	return FMath::Max(FMath::Max(0.f, MinWidth), Width);
}

float FLureFishingRules::LinePixelsAtDistance(float WidthCm, float DistanceCm, float HorizontalFovDeg, float ReferenceScreenWidth)
{
	if (!(DistanceCm > 0.f))
	{
		return TNumericLimits<float>::Max();
	}
	const float Fov = (FMath::IsFinite(HorizontalFovDeg) && HorizontalFovDeg > 1.f && HorizontalFovDeg < 179.f) ? HorizontalFovDeg : 90.f;
	const float ScreenWidth = (FMath::IsFinite(ReferenceScreenWidth) && ReferenceScreenWidth > 0.f) ? ReferenceScreenWidth : 1920.f;
	return WidthCm * ScreenWidth / (DistanceCm * 2.f * FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)));
}

void FLureFishingRules::ComputeLinePoints(const FVector& Start, const FVector& End, float Sag, int32 Segments, TArray<FVector>& OutPoints)
{
	const int32 Count = FMath::Clamp(Segments, 1, 64);
	const float Length = static_cast<float>(FVector::Dist(Start, End));
	const float SagDepth = (FMath::IsFinite(Sag) ? FMath::Max(0.f, Sag) : 0.f) * Length;
	OutPoints.Reset(Count + 1);
	for (int32 Index = 0; Index <= Count; ++Index)
	{
		const float T = static_cast<float>(Index) / static_cast<float>(Count);
		OutPoints.Add(FMath::Lerp(Start, End, T) - FVector::UpVector * (SagDepth * 4.f * T * (1.f - T)));
	}
}

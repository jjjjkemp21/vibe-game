// Copyright Epic Games, Inc. All Rights Reserved.

#include "Fish/FishTypes.h"

DEFINE_LOG_CATEGORY(LogLureFish);

float FFishLevelScaling::GetMultiplier(int32 FishLevel, int32 PlayerLevel) const
{
	// int64 so INT32_MAX - INT32_MIN can't wrap
	const int64 Delta = static_cast<int64>(FishLevel) - static_cast<int64>(PlayerLevel);
	const double Over = FMath::IsFinite(OverLevelFactor) ? FMath::Max(0.0, static_cast<double>(OverLevelFactor)) : 0.0;
	const double Under = FMath::IsFinite(UnderLevelFactor) ? FMath::Max(0.0, static_cast<double>(UnderLevelFactor)) : 0.0;
	const double Low = FMath::IsFinite(MinMultiplier) ? static_cast<double>(MinMultiplier) : 0.0;
	const double High = FMath::IsFinite(MaxMultiplier) ? static_cast<double>(MaxMultiplier) : 1.0;

	const double Raw = 1.0
		+ static_cast<double>(FMath::Max<int64>(0, Delta)) * Over
		- static_cast<double>(FMath::Max<int64>(0, -Delta)) * Under;
	return static_cast<float>(FMath::Clamp(Raw, FMath::Min(Low, High), FMath::Max(Low, High)));
}

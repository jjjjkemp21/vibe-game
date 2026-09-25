// Lure: the look per time of day (T-068b), pure.

#include "Environment/LureTimeOfDay.h"
#include "Engine/Scene.h"

namespace LureTimeOfDayPrivate
{
	// levels/layout.py FILM: Unreal's default filmic tonemapper values (TonemapCommon.ush FilmToneMap).
	constexpr double FilmSlope = 0.88;
	constexpr double FilmToe = 0.55;
	constexpr double FilmShoulder = 0.26;
	constexpr double FilmBlackClip = 0.0;
	constexpr double FilmWhiteClip = 0.04;

	float LerpF(float A, float B, float Alpha) { return A + (B - A) * Alpha; }

	FLinearColor LerpC(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(LerpF(A.R, B.R, Alpha), LerpF(A.G, B.G, Alpha), LerpF(A.B, B.B, Alpha), LerpF(A.A, B.A, Alpha));
	}

	FLinearColor HexOrWhite(const FString& Hex)
	{
		FLinearColor Out = FLinearColor::White;
		FLureTimeOfDayBlend::ParseHexColor(Hex, Out);
		return Out;
	}
}

FLinearColor FLureTimeOfDayLook::GetFogSceneColor() const
{
	return FLureTimeOfDayBlend::OnScreenToScene(FogColor, ExposureEV100);
}

bool FLureTimeOfDayBlend::ParseRowName(FName RowName, FName& OutRegion, ELureDayPhase& OutPhase)
{
	const FString Name = RowName.ToString();
	int32 Split = INDEX_NONE;
	if (!Name.FindLastChar(TEXT('_'), Split) || Split <= 0 || Split >= Name.Len() - 1)
	{
		return false;
	}
	if (!FLureDayClock::ParsePhase(Name.Mid(Split + 1), OutPhase))
	{
		return false;
	}
	OutRegion = FName(*Name.Left(Split));
	return true;
}

FName FLureTimeOfDayBlend::MakeRowName(FName Region, ELureDayPhase Phase)
{
	return FName(*FString::Printf(TEXT("%s_%s"), *Region.ToString(), *FLureDayClock::PhaseName(Phase)));
}

bool FLureTimeOfDayBlend::ParseHexColor(const FString& Hex, FLinearColor& OutLinear)
{
	FString Digits = Hex.TrimStartAndEnd();
	Digits.RemoveFromStart(TEXT("#"));
	if (Digits.Len() != 6)
	{
		return false;
	}
	for (const TCHAR C : Digits)
	{
		if (!FChar::IsHexDigit(C))
		{
			return false;
		}
	}
	OutLinear = FLinearColor(FColor::FromHex(Digits)); // FLinearColor(FColor) converts sRGB -> linear
	OutLinear.A = 1.f;
	return true;
}

bool FLureTimeOfDayBlend::ValidateRow(const FLureTimeOfDayRow& Row, TArray<FString>* OutProblems)
{
	bool bOk = true;
	auto Problem = [&bOk, OutProblems](const FString& Text)
	{
		bOk = false;
		if (OutProblems)
		{
			OutProblems->Add(Text);
		}
	};
	const TPair<const TCHAR*, const FString*> Colors[] = {
		{ TEXT("SunColor"), &Row.SunColor }, { TEXT("MoonColor"), &Row.MoonColor }, { TEXT("FogColor"), &Row.FogColor },
		{ TEXT("WaterShallowColor"), &Row.WaterShallowColor }, { TEXT("WaterDeepColor"), &Row.WaterDeepColor },
		{ TEXT("WaterFoamColor"), &Row.WaterFoamColor } };
	for (const TPair<const TCHAR*, const FString*>& Color : Colors)
	{
		FLinearColor Unused;
		if (!ParseHexColor(*Color.Value, Unused))
		{
			Problem(FString::Printf(TEXT("%s '%s' is not #RRGGBB"), Color.Key, **Color.Value));
		}
	}
	const TPair<const TCHAR*, float> NonNegative[] = {
		{ TEXT("SunIntensity"), Row.SunIntensity }, { TEXT("MoonIntensity"), Row.MoonIntensity },
		{ TEXT("SkyLuminanceR"), Row.SkyLuminanceR }, { TEXT("SkyLuminanceG"), Row.SkyLuminanceG }, { TEXT("SkyLuminanceB"), Row.SkyLuminanceB },
		{ TEXT("SkyLightIntensity"), Row.SkyLightIntensity }, { TEXT("FogDensity"), Row.FogDensity },
		{ TEXT("FogHeightFalloff"), Row.FogHeightFalloff }, { TEXT("FogStartDistance"), Row.FogStartDistance },
		{ TEXT("FogSkyAmbient"), Row.FogSkyAmbient }, { TEXT("WaveScale"), Row.WaveScale } };
	for (const TPair<const TCHAR*, float>& Value : NonNegative)
	{
		if (!FMath::IsFinite(Value.Value) || Value.Value < 0.f)
		{
			Problem(FString::Printf(TEXT("%s %g must be >= 0"), Value.Key, Value.Value));
		}
	}
	if (!FMath::IsFinite(Row.FogMaxOpacity) || Row.FogMaxOpacity < 0.f || Row.FogMaxOpacity > 1.f)
	{
		Problem(FString::Printf(TEXT("FogMaxOpacity %g must be in [0, 1]"), Row.FogMaxOpacity));
	}
	if (!FMath::IsFinite(Row.NightLightIntensity) || Row.NightLightIntensity < 0.f || Row.NightLightIntensity > 1.f)
	{
		Problem(FString::Printf(TEXT("NightLightIntensity %g must be in [0, 1]"), Row.NightLightIntensity));
	}
	if (!FMath::IsFinite(Row.SunPitch) || Row.SunPitch < -90.f || Row.SunPitch > 90.f)
	{
		Problem(FString::Printf(TEXT("SunPitch %g must be in [-90, 90]"), Row.SunPitch));
	}
	if (!FMath::IsFinite(Row.ExposureEV100) || Row.ExposureEV100 < -15.f || Row.ExposureEV100 > 20.f)
	{
		Problem(FString::Printf(TEXT("ExposureEV100 %g must be in [-15, 20]"), Row.ExposureEV100));
	}
	if (!FMath::IsFinite(Row.AnchorHour) || Row.AnchorHour >= 24.f)
	{
		Problem(FString::Printf(TEXT("AnchorHour %g must be < 24 (< 0 = the phase midpoint)"), Row.AnchorHour));
	}
	return bOk;
}

bool FLureTimeOfDayBlend::ValidateTable(const UDataTable* Table, TArray<FString>* OutProblems)
{
	auto Problem = [OutProblems](const FString& Text)
	{
		if (OutProblems)
		{
			OutProblems->Add(Text);
		}
	};
	if (!Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FLureTimeOfDayRow::StaticStruct()))
	{
		Problem(TEXT("no DT_TimeOfDay table (row struct LureTimeOfDayRow)"));
		return false;
	}
	bool bOk = true;
	TMap<FName, TSet<ELureDayPhase>> PhasesByRegion;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		FName Region;
		ELureDayPhase Phase;
		if (!ParseRowName(Pair.Key, Region, Phase))
		{
			Problem(FString::Printf(TEXT("row '%s' is not <Region>_<Dawn|Day|Dusk|Night>"), *Pair.Key.ToString()));
			bOk = false;
			continue;
		}
		PhasesByRegion.FindOrAdd(Region).Add(Phase);
		TArray<FString> RowProblems;
		if (!ValidateRow(*reinterpret_cast<const FLureTimeOfDayRow*>(Pair.Value), &RowProblems))
		{
			for (const FString& Text : RowProblems)
			{
				Problem(FString::Printf(TEXT("%s: %s"), *Pair.Key.ToString(), *Text));
			}
			bOk = false;
		}
	}
	if (PhasesByRegion.IsEmpty())
	{
		Problem(TEXT("DT_TimeOfDay has no rows"));
		bOk = false;
	}
	for (const TPair<FName, TSet<ELureDayPhase>>& Region : PhasesByRegion)
	{
		for (int32 Index = 0; Index < FLureDayClock::NumPhases; ++Index)
		{
			const ELureDayPhase Phase = static_cast<ELureDayPhase>(Index);
			if (!Region.Value.Contains(Phase))
			{
				Problem(FString::Printf(TEXT("region '%s' has no %s row"), *Region.Key.ToString(), *MakeRowName(Region.Key, Phase).ToString()));
				bOk = false;
			}
		}
	}
	return bOk;
}

TMap<ELureDayPhase, FLureTimeOfDayRow> FLureTimeOfDayBlend::GatherRegion(const UDataTable* Table, FName Region)
{
	TMap<ELureDayPhase, FLureTimeOfDayRow> Rows;
	if (!Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FLureTimeOfDayRow::StaticStruct()))
	{
		return Rows;
	}
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		FName RowRegion;
		ELureDayPhase Phase;
		if (ParseRowName(Pair.Key, RowRegion, Phase) && RowRegion == Region)
		{
			Rows.Add(Phase, *reinterpret_cast<const FLureTimeOfDayRow*>(Pair.Value));
		}
	}
	return Rows;
}

FLureTimeOfDayLook FLureTimeOfDayBlend::Resolve(const FLureTimeOfDayRow& Row)
{
	using namespace LureTimeOfDayPrivate;
	FLureTimeOfDayLook Look;
	Look.SunElevation = Row.SunPitch;
	Look.SunColor = HexOrWhite(Row.SunColor);
	Look.SunIntensity = Row.SunIntensity;
	Look.MoonColor = HexOrWhite(Row.MoonColor);
	Look.MoonIntensity = Row.MoonIntensity;
	Look.SkyLuminanceFactor = FLinearColor(Row.SkyLuminanceR, Row.SkyLuminanceG, Row.SkyLuminanceB, 1.f);
	Look.SkyLightIntensity = Row.SkyLightIntensity;
	Look.FogColor = HexOrWhite(Row.FogColor);
	Look.FogDensity = Row.FogDensity;
	Look.FogHeightFalloff = Row.FogHeightFalloff;
	Look.FogStartDistance = Row.FogStartDistance;
	Look.FogMaxOpacity = Row.FogMaxOpacity;
	Look.FogSkyAmbient = Row.FogSkyAmbient;
	Look.ExposureEV100 = Row.ExposureEV100;
	Look.WaterShallowColor = HexOrWhite(Row.WaterShallowColor);
	Look.WaterDeepColor = HexOrWhite(Row.WaterDeepColor);
	Look.WaterFoamColor = HexOrWhite(Row.WaterFoamColor);
	Look.WaveScale = Row.WaveScale;
	Look.NightLightIntensity = Row.NightLightIntensity;
	return Look;
}

FLureTimeOfDayLook FLureTimeOfDayBlend::Lerp(const FLureTimeOfDayLook& A, const FLureTimeOfDayLook& B, float Alpha)
{
	using namespace LureTimeOfDayPrivate;
	FLureTimeOfDayLook L;
	L.SunElevation = LerpF(A.SunElevation, B.SunElevation, Alpha);
	L.SunColor = LerpC(A.SunColor, B.SunColor, Alpha);
	L.SunIntensity = LerpF(A.SunIntensity, B.SunIntensity, Alpha);
	L.MoonColor = LerpC(A.MoonColor, B.MoonColor, Alpha);
	L.MoonIntensity = LerpF(A.MoonIntensity, B.MoonIntensity, Alpha);
	L.SkyLuminanceFactor = LerpC(A.SkyLuminanceFactor, B.SkyLuminanceFactor, Alpha);
	L.SkyLightIntensity = LerpF(A.SkyLightIntensity, B.SkyLightIntensity, Alpha);
	L.FogColor = LerpC(A.FogColor, B.FogColor, Alpha);
	L.FogDensity = LerpF(A.FogDensity, B.FogDensity, Alpha);
	L.FogHeightFalloff = LerpF(A.FogHeightFalloff, B.FogHeightFalloff, Alpha);
	L.FogStartDistance = LerpF(A.FogStartDistance, B.FogStartDistance, Alpha);
	L.FogMaxOpacity = LerpF(A.FogMaxOpacity, B.FogMaxOpacity, Alpha);
	L.FogSkyAmbient = LerpF(A.FogSkyAmbient, B.FogSkyAmbient, Alpha);
	L.ExposureEV100 = LerpF(A.ExposureEV100, B.ExposureEV100, Alpha);
	L.WaterShallowColor = LerpC(A.WaterShallowColor, B.WaterShallowColor, Alpha);
	L.WaterDeepColor = LerpC(A.WaterDeepColor, B.WaterDeepColor, Alpha);
	L.WaterFoamColor = LerpC(A.WaterFoamColor, B.WaterFoamColor, Alpha);
	L.WaveScale = LerpF(A.WaveScale, B.WaveScale, Alpha);
	L.NightLightIntensity = LerpF(A.NightLightIntensity, B.NightLightIntensity, Alpha);
	return L;
}

double FLureTimeOfDayBlend::PhaseMidpoint(const FLureDayClock& Clock, ELureDayPhase Phase)
{
	return FLureDayClock::NormalizeHour(Clock.GetPhaseStartHour(Phase) + 0.5 * Clock.GetPhaseHours(Phase));
}

double FLureTimeOfDayBlend::AnchorHour(const FLureTimeOfDayRow& Row, ELureDayPhase Phase, const FLureDayClock& Clock)
{
	return Row.AnchorHour >= 0.f ? FLureDayClock::NormalizeHour(Row.AnchorHour) : PhaseMidpoint(Clock, Phase);
}

TArray<FLureTimeOfDayAnchor> FLureTimeOfDayBlend::BuildAnchors(const TMap<ELureDayPhase, FLureTimeOfDayRow>& Rows, const FLureDayClock& Clock)
{
	TArray<FLureTimeOfDayAnchor> Anchors;
	for (const TPair<ELureDayPhase, FLureTimeOfDayRow>& Pair : Rows)
	{
		FLureTimeOfDayAnchor& Anchor = Anchors.AddDefaulted_GetRef();
		Anchor.Hour = AnchorHour(Pair.Value, Pair.Key, Clock);
		Anchor.Phase = Pair.Key;
		Anchor.Look = Resolve(Pair.Value);
	}
	Anchors.StableSort([](const FLureTimeOfDayAnchor& A, const FLureTimeOfDayAnchor& B) { return A.Hour < B.Hour; });
	return Anchors;
}

FLureTimeOfDayLook FLureTimeOfDayBlend::Evaluate(const TArray<FLureTimeOfDayAnchor>& Anchors, double Hour)
{
	if (Anchors.Num() == 0)
	{
		return FLureTimeOfDayLook();
	}
	const double H = FLureDayClock::NormalizeHour(Hour);
	const int32 Num = Anchors.Num();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FLureTimeOfDayAnchor& From = Anchors[Index];
		const FLureTimeOfDayAnchor& To = Anchors[(Index + 1) % Num];
		double Span = FLureDayClock::NormalizeHour(To.Hour - From.Hour);
		if (Num == 1 || Span <= UE_KINDA_SMALL_NUMBER)
		{
			if (Num == 1)
			{
				return From.Look;
			}
			continue; // two anchors at one hour: the next pair covers the span
		}
		const double Into = FLureDayClock::NormalizeHour(H - From.Hour);
		if (Into < Span)
		{
			return Lerp(From.Look, To.Look, static_cast<float>(Into / Span));
		}
	}
	return Anchors[0].Look;
}

double FLureTimeOfDayBlend::SunAzimuth(double Hour, const FLureDayClock& Clock)
{
	const double Sunrise = PhaseMidpoint(Clock, ELureDayPhase::Dawn);
	const double DayHours = FMath::Max(FLureDayClock::NormalizeHour(PhaseMidpoint(Clock, ELureDayPhase::Dusk) - Sunrise), 0.01);
	const double NightHours = FMath::Max(24.0 - DayHours, 0.01);
	const double Since = FLureDayClock::NormalizeHour(Hour - Sunrise);
	return Since < DayHours ? 180.0 * Since / DayHours : 180.0 + 180.0 * (Since - DayHours) / NightHours;
}

FRotator FLureTimeOfDayBlend::SunRotation(double Hour, float Elevation, const FLureDayClock& Clock, float EastYaw)
{
	// The direction TO the sun has yaw EastYaw + azimuth (yaw grows from +X toward +Y); the light travels the other way.
	const double TowardSunYaw = EastYaw + SunAzimuth(Hour, Clock);
	return FRotator(-Elevation, FRotator::NormalizeAxis(TowardSunYaw + 180.0), 0.0);
}

FRotator FLureTimeOfDayBlend::MoonRotation(double Hour, float SunElevation, const FLureDayClock& Clock, float EastYaw)
{
	const double TowardMoonYaw = EastYaw + SunAzimuth(Hour, Clock) + 180.0;
	return FRotator(SunElevation, FRotator::NormalizeAxis(TowardMoonYaw + 180.0), 0.0);
}

double FLureTimeOfDayBlend::FilmicToneMap(double X)
{
	using namespace LureTimeOfDayPrivate;
	const double S = FilmSlope, B = FilmBlackClip, W = FilmWhiteClip;
	const double ToeScale = 1.0 + B - FilmToe;
	const double ShoulderScale = 1.0 + W - FilmShoulder;
	const double Bt = (0.18 + B) / ToeScale - 1.0;
	const double ToeMatch = FMath::LogX(10.0, 0.18) - 0.5 * FMath::Loge((1.0 + Bt) / (1.0 - Bt)) * (ToeScale / S);
	const double StraightMatch = (1.0 - FilmToe) / S - ToeMatch;
	const double ShoulderMatch = FilmShoulder / S - StraightMatch;

	const double Lg = FMath::LogX(10.0, FMath::Max(X, 1e-7));
	const double Straight = S * (Lg + StraightMatch);
	const double Toe = Lg < ToeMatch ? -B + (2.0 * ToeScale) / (1.0 + FMath::Exp((-2.0 * S / ToeScale) * (Lg - ToeMatch))) : Straight;
	const double Shoulder = Lg > ShoulderMatch ? (1.0 + W) - (2.0 * ShoulderScale) / (1.0 + FMath::Exp((2.0 * S / ShoulderScale) * (Lg - ShoulderMatch))) : Straight;
	double T = FMath::Clamp((Lg - ToeMatch) / (ShoulderMatch - ToeMatch), 0.0, 1.0);
	if (ShoulderMatch < ToeMatch)
	{
		T = 1.0 - T;
	}
	T = (3.0 - 2.0 * T) * T * T;
	return FMath::Max(0.0, Toe + (Shoulder - Toe) * T);
}

double FLureTimeOfDayBlend::FilmicInverse(double Y)
{
	Y = FMath::Clamp(Y, 0.0, 0.995);
	if (Y <= 0.0)
	{
		return 0.0;
	}
	double Lo = 1e-6, Hi = 1e3; // bisection in log space, as levels/layout.py ue_filmic_inverse
	for (int32 Step = 0; Step < 100; ++Step)
	{
		const double Mid = FMath::Sqrt(Lo * Hi);
		if (FilmicToneMap(Mid) < Y)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}
	return FMath::Sqrt(Lo * Hi);
}

double FLureTimeOfDayBlend::ExposureScale(float EV100)
{
	return FMath::Pow(2.0, -static_cast<double>(EV100));
}

FLinearColor FLureTimeOfDayBlend::OnScreenToScene(const FLinearColor& OnScreenLinear, float EV100)
{
	const double K = ExposureScale(EV100);
	return FLinearColor(static_cast<float>(FilmicInverse(OnScreenLinear.R) / K), static_cast<float>(FilmicInverse(OnScreenLinear.G) / K),
		static_cast<float>(FilmicInverse(OnScreenLinear.B) / K), 1.f);
}

void FLureTimeOfDayBlend::ApplyFixedExposure(FPostProcessSettings& Settings, float EV100)
{
	// PostProcessEyeAdaptation.cpp CalculateManualAutoExposure: without the physical camera, exposure scale = 2^bias.
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = -EV100;
}

// Lure: the look per time of day (T-068b). DT_TimeOfDay rows and the pure blend between them. World-free.
// Rules: docs/specs/day-night-water.md §3.2 (rules 7-11), §4 (DT_TimeOfDay), §5 AC7; contract in §7.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Environment/LureDayClock.h"
#include "LureTimeOfDay.generated.h"

struct FPostProcessSettings;

/**
 *  DT_TimeOfDay row (source data/tables/DT_TimeOfDay.json). The row name is `<Region>_<Phase>` (e.g. `Tropical_Dusk`), and
 *  each region has all four phases. A row is the look at its anchor hour (the phase's midpoint by default); between anchors
 *  every value blends linearly (colours in linear space). Colours are sRGB hex ("#8FD3F0"). FogColor is an on-screen
 *  target: the code undoes the fixed exposure and the filmic tonemapper, like the level builder does.
 */
USTRUCT(BlueprintType)
struct FLureTimeOfDayRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The hour this row's look is exact at, [0, 24). < 0 = the phase's midpoint from DT_DayCycle (Day 7-17 -> 12:00). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	float AnchorHour = -1.f;

	/** Sun elevation above the horizon, degrees (negative = below it). The yaw follows the hour (east at sunrise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="-90", ClampMax="90"))
	float SunPitch = 50.f;

	/** Sun colour, sRGB hex. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FString SunColor = TEXT("#FFF4E0");

	/** Sun illuminance, lux. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float SunIntensity = 10.f;

	/** Moon (a second, dim directional light opposite the sun) colour, sRGB hex. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FString MoonColor = TEXT("#9DB4E0");

	/** Moon illuminance, lux. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float MoonIntensity = 0.f;

	/** SkyAtmosphere sky luminance factor (R, G, B). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float SkyLuminanceR = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float SkyLuminanceG = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float SkyLuminanceB = 1.f;

	/** SkyLight intensity (real-time capture). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float SkyLightIntensity = 0.35f;

	/** Height fog colour, sRGB hex, the ON-SCREEN target (the code undoes exposure + tonemapper). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FString FogColor = TEXT("#8FD3F0");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float FogDensity = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float FogHeightFalloff = 0.2f;

	/** cm with no fog in front of the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float FogStartDistance = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0", ClampMax="1"))
	float FogMaxOpacity = 1.f;

	/** SkyAtmosphere light added on top of the fog colour (0 = the fog colour alone). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float FogSkyAmbient = 0.f;

	/** Fixed manual exposure (never auto): scene luminance 2^EV100 cd/m2 maps to 1.0 before the tonemapper. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	float ExposureEV100 = 1.f;

	/** Water colours, sRGB hex (to MPC_Water through the water surface subsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FString WaterShallowColor = TEXT("#3ED1C4");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FString WaterDeepColor = TEXT("#0A5560");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FString WaterFoamColor = TEXT("#F2FBF8");

	/** Wave height multiplier (calm at noon). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0"))
	float WaveScale = 1.f;

	/** Share of their placed intensity that `Lure.NightLight` lights get, [0, 1] (0 = off by day). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0", ClampMax="1"))
	float NightLightIntensity = 0.f;

	/** Notes for designers (not used by the game). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FString DevComment;
};

/** One resolved look: every value numeric, colours linear. What the sky rig applies. */
USTRUCT(BlueprintType)
struct FLureTimeOfDayLook
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float SunElevation = 50.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") FLinearColor SunColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float SunIntensity = 10.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") FLinearColor MoonColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float MoonIntensity = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") FLinearColor SkyLuminanceFactor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float SkyLightIntensity = 0.35f;
	/** The fog's on-screen target (display linear); GetFogSceneColor() is what the fog component gets. */
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") FLinearColor FogColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float FogDensity = 0.05f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float FogHeightFalloff = 0.2f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float FogStartDistance = 3000.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float FogMaxOpacity = 1.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float FogSkyAmbient = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float ExposureEV100 = 1.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") FLinearColor WaterShallowColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") FLinearColor WaterDeepColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") FLinearColor WaterFoamColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float WaveScale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category="Lure|Sky") float NightLightIntensity = 0.f;

	/** The fog inscattering colour (scene linear) that shows as FogColor on screen under ExposureEV100. */
	FLinearColor GetFogSceneColor() const;
};

/** A row placed on the day: its hour and resolved look. */
struct FLureTimeOfDayAnchor
{
	double Hour = 12.0;
	ELureDayPhase Phase = ELureDayPhase::Day;
	FLureTimeOfDayLook Look;
};

/** The pure time-of-day math: rows -> anchors -> the look at an hour, the sun's direction, and the exposure/tonemap maths. */
struct FLureTimeOfDayBlend
{
	/** `<Region>_<Phase>` -> (Region, Phase); false if the name has no known phase after its last '_'. */
	static bool ParseRowName(FName RowName, FName& OutRegion, ELureDayPhase& OutPhase);

	/** `<Region>_<Phase>`. */
	static FName MakeRowName(FName Region, ELureDayPhase Phase);

	/** "#RRGGBB" (or "RRGGBB") -> linear colour; false if it isn't 6 hex digits. */
	static bool ParseHexColor(const FString& Hex, FLinearColor& OutLinear);

	/** True if Row holds usable values (colours parse, ranges hold). */
	static bool ValidateRow(const FLureTimeOfDayRow& Row, TArray<FString>* OutProblems = nullptr);

	/** Every row of Table: names parse, rows validate, and every region has all four phases. */
	static bool ValidateTable(const UDataTable* Table, TArray<FString>* OutProblems = nullptr);

	/** The region's rows by phase (rows with other regions or bad names are skipped). */
	static TMap<ELureDayPhase, FLureTimeOfDayRow> GatherRegion(const UDataTable* Table, FName Region);

	/** Row -> numbers (an unparsable colour becomes white). */
	static FLureTimeOfDayLook Resolve(const FLureTimeOfDayRow& Row);

	/** A + (B - A) x Alpha for every value (colours component-wise, i.e. in linear space). */
	static FLureTimeOfDayLook Lerp(const FLureTimeOfDayLook& A, const FLureTimeOfDayLook& B, float Alpha);

	/** The midpoint hour of Phase on Clock's day (Night 19-5 -> 0:00). */
	static double PhaseMidpoint(const FLureDayClock& Clock, ELureDayPhase Phase);

	/** Row.AnchorHour if >= 0, else the phase's midpoint. */
	static double AnchorHour(const FLureTimeOfDayRow& Row, ELureDayPhase Phase, const FLureDayClock& Clock);

	/** Anchors for the region's rows, ascending by hour. */
	static TArray<FLureTimeOfDayAnchor> BuildAnchors(const TMap<ELureDayPhase, FLureTimeOfDayRow>& Rows, const FLureDayClock& Clock);

	/** The look at Hour: linear between the two anchors around it (across midnight too). No anchors -> the default look. */
	static FLureTimeOfDayLook Evaluate(const TArray<FLureTimeOfDayAnchor>& Anchors, double Hour);

	/**
	 *  The sun's azimuth in degrees from east (0 = east, 90 = noon, 180 = west, 270 = midnight). It turns steadily: 180 deg
	 *  over [Sunrise, Sunset] and 180 deg over the night. Sunrise/Sunset = the Dawn/Dusk midpoints (06:00 / 18:00).
	 */
	static double SunAzimuth(double Hour, const FLureDayClock& Clock);

	/** The sun light's rotation (its forward = the direction the light travels). EastYaw = the level yaw that faces east. */
	static FRotator SunRotation(double Hour, float Elevation, const FLureDayClock& Clock, float EastYaw);

	/** The moon light's rotation: opposite the sun (azimuth + 180, elevation negated). */
	static FRotator MoonRotation(double Hour, float SunElevation, const FLureDayClock& Clock, float EastYaw);

	/** Unreal's default filmic tonemapper (slope 0.88, toe 0.55, shoulder 0.26, black 0, white 0.04), one channel. */
	static double FilmicToneMap(double X);

	/** Display linear -> the exposed scene linear value the tonemapper maps to it (Y clamped to [0, 0.995]). */
	static double FilmicInverse(double Y);

	/** Tonemapper input per scene luminance under a fixed exposure: 2^-EV100. */
	static double ExposureScale(float EV100);

	/** The scene-linear colour that shows as OnScreenLinear under EV100 (levels/layout.py on_screen_to_scene). */
	static FLinearColor OnScreenToScene(const FLinearColor& OnScreenLinear, float EV100);

	/** Manual exposure without the physical camera: method Manual, bias = -EV100 (never auto, AC7). */
	static void ApplyFixedExposure(FPostProcessSettings& Settings, float EV100);
};

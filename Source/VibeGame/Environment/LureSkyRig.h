// Lure: the sky rig (T-068b). Applies the blended DT_TimeOfDay look for the clock's hour to the level's sun, moon, sky,
// fog, exposure and night lights (and the water, once T-069a lands). Cosmetic only: every machine runs it from the
// replicated clock; it never writes gameplay state.
// Rules: docs/specs/day-night-water.md §3.2 (rules 7-11), §3.5 (budget), §5 AC7-AC9; contract in §7.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Environment/LureTimeOfDay.h"
#include "LureSkyRig.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class APostProcessVolume;
class ASkyAtmosphere;
class ASkyLight;
class ULightComponent;
class ULureDayClockComponent;

/** Actor tags the sky rig reads (the level builder sets them, T-068c). */
namespace LureSkyTags
{
	/** A light that follows DT_TimeOfDay NightLightIntensity (off by day, on at dusk and night). */
	inline const FName NightLight(TEXT("Lure.NightLight"));
	/** The sun directional light (fallback lookup when SunLight is unset). */
	inline const FName Sun(TEXT("Lure.Sun"));
	/** The moon directional light (fallback lookup when MoonLight is unset; a moon is never guessed without it). */
	inline const FName Moon(TEXT("Lure.Moon"));
}

/**
 *  One per level. Reads the day clock (ULureDayClockComponent) and, every ApplyIntervalSeconds plus at once when the phase
 *  changes or the clock jumps (Lure.Time.Set), applies the look of FLureTimeOfDayBlend::Evaluate for Region's rows.
 *  Targets are the EditInstanceOnly refs; a missing ref is found once at BeginPlay (by tag, then by class) and cached, and
 *  a target that can't be found is skipped with one warning. `Lure.NightLight` lights are gathered once at BeginPlay with
 *  their placed intensity as "full". Exposure is always manual (never auto): bias = -EV100 on the unbound PostProcessVolume.
 *  The SkyLight uses real-time capture (time-sliced by the engine); the rig never recaptures it by hand.
 */
UCLASS(ClassGroup=(Lure))
class ALureSkyRig : public AActor
{
	GENERATED_BODY()

public:

	ALureSkyRig();

	// ---- Targets (the level builder assigns them; unset = found once at BeginPlay) ----

	/** The sun (tag Lure.Sun, else the directional light used as the atmosphere sun, else the first untagged one). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Lure|Sky")
	TObjectPtr<ADirectionalLight> SunLight;

	/** The moon: a second, dim directional light opposite the sun (tag Lure.Moon only). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Lure|Sky")
	TObjectPtr<ADirectionalLight> MoonLight;

	/** Else the level's first SkyAtmosphere. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Lure|Sky")
	TObjectPtr<ASkyAtmosphere> SkyAtmosphere;

	/** Else the level's first SkyLight. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Lure|Sky")
	TObjectPtr<ASkyLight> SkyLight;

	/** Else the level's first ExponentialHeightFog. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Lure|Sky")
	TObjectPtr<AExponentialHeightFog> HeightFog;

	/** The volume carrying the fixed exposure; else the level's first unbound PostProcessVolume. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Lure|Sky")
	TObjectPtr<APostProcessVolume> PostProcessVolume;

	// ---- Tuning ----

	/** The DT_TimeOfDay region (rows `<Region>_<Phase>`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	FName Region = TEXT("Tropical");

	/** Seconds between applies while the clock runs (a phase change or a clock jump applies at once). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky", meta=(ClampMin="0.0", UIMax="1.0"))
	float ApplyIntervalSeconds = 0.1f;

	/** The level yaw (degrees) that faces east: the sun rises there and sets opposite (spec rule 8: level +X = east). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lure|Sky")
	float EastYawDegrees = 0.f;

	// ---- Code API ----

	/** Uses Table's rows for Region instead of the configured DT_TimeOfDay (tests; call before or after BeginPlay). */
	void SetTimeOfDayTable(const UDataTable* Table);

	/** Applies the look for the clock's hour now (no-op without a clock or rows). */
	void ApplyNow();

	/** The look applied last (valid after the first apply). */
	const FLureTimeOfDayLook& GetAppliedLook() const { return AppliedLook; }
	/** The hour applied last; < 0 before the first apply. */
	double GetAppliedHour() const { return AppliedHour; }
	int32 GetApplyCount() const { return ApplyCount; }
	int32 GetNightLightCount() const { return NightLights.Num(); }
	const TArray<FLureTimeOfDayAnchor>& GetAnchors() const { return Anchors; }

	// AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:

	struct FNightLight
	{
		TWeakObjectPtr<ULightComponent> Light;
		float FullIntensity = 0.f;
	};

	/** Finds unset targets once (tag, then class) and warns once per target that is missing. */
	void ResolveTargets();
	void GatherNightLights();
	/** Finds the clock (the GameState may replicate after BeginPlay on a client) and binds its events. */
	ULureDayClockComponent* FindClock();
	void RebuildAnchors();
	void Apply(double Hour);

	UFUNCTION()
	void HandlePhaseChanged(ELureDayPhase NewPhase, ELureDayPhase OldPhase);
	void HandleClockChanged();

	TWeakObjectPtr<ULureDayClockComponent> BoundClock;
	FDelegateHandle ClockChangedHandle;
	TMap<ELureDayPhase, FLureTimeOfDayRow> RegionRows;
	bool bRowsSet = false;
	TArray<FLureTimeOfDayAnchor> Anchors;
	TArray<FNightLight> NightLights;
	FLureTimeOfDayLook AppliedLook;
	double AppliedHour = -1.0;
	int32 ApplyCount = 0;
	bool bDirty = true;
};

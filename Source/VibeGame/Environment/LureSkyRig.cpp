// Lure: the sky rig (T-068b).

#include "Environment/LureSkyRig.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/LureDayClockComponent.h"
#include "Environment/LureDayNightSettings.h"

namespace LureSkyRigPrivate
{
	/** The first T with Tag (if Tag is set), else the first T that passes Accept. */
	template <typename T, typename FAccept>
	T* FindTarget(UWorld* World, FName Tag, FAccept Accept)
	{
		if (!World)
		{
			return nullptr;
		}
		if (!Tag.IsNone())
		{
			for (TActorIterator<T> It(World); It; ++It)
			{
				if (IsValid(*It) && It->ActorHasTag(Tag))
				{
					return *It;
				}
			}
		}
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (IsValid(*It) && Accept(*It))
			{
				return *It;
			}
		}
		return nullptr;
	}

	void MakeMovable(ADirectionalLight* Light)
	{
		ULightComponent* Component = Light ? Light->GetLightComponent() : nullptr;
		if (Component && Component->Mobility != EComponentMobility::Movable)
		{
			Component->SetMobility(EComponentMobility::Movable);
		}
	}
}

ALureSkyRig::ALureSkyRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.1f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = false; // cosmetic: every machine runs it from the replicated clock
}

void ALureSkyRig::SetTimeOfDayTable(const UDataTable* Table)
{
	RegionRows = FLureTimeOfDayBlend::GatherRegion(Table, Region);
	bRowsSet = true;
	RebuildAnchors();
	if (HasActorBegunPlay())
	{
		ApplyNow();
	}
}

void ALureSkyRig::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false); // nothing to see on a dedicated server
		return;
	}
	SetActorTickInterval(FMath::Max(0.f, ApplyIntervalSeconds));

	if (!bRowsSet)
	{
		const UDataTable* Table = ULureDayNightSettings::LoadTimeOfDayTable();
		TArray<FString> Problems;
		if (!Table)
		{
			UE_LOG(LogLureDayNight, Warning, TEXT("Sky rig: no DT_TimeOfDay ('%s'; source data/tables/DT_TimeOfDay.json); the level keeps its built look."),
				*GetDefault<ULureDayNightSettings>()->TimeOfDayTable.ToString());
		}
		else if (!FLureTimeOfDayBlend::ValidateTable(Table, &Problems))
		{
			UE_LOG(LogLureDayNight, Warning, TEXT("Sky rig: DT_TimeOfDay has problems (%s)."), *FString::Join(Problems, TEXT("; ")));
		}
		RegionRows = FLureTimeOfDayBlend::GatherRegion(Table, Region);
		if (Table && RegionRows.IsEmpty())
		{
			UE_LOG(LogLureDayNight, Warning, TEXT("Sky rig: DT_TimeOfDay has no rows for region '%s'; the level keeps its built look."), *Region.ToString());
		}
	}

	ResolveTargets();
	GatherNightLights();
	FindClock();
	RebuildAnchors();
	ApplyNow();
}

void ALureSkyRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULureDayClockComponent* Clock = BoundClock.Get())
	{
		Clock->OnPhaseChanged.RemoveDynamic(this, &ALureSkyRig::HandlePhaseChanged);
		Clock->OnClockChanged.Remove(ClockChangedHandle);
	}
	BoundClock.Reset();
	Super::EndPlay(EndPlayReason);
}

void ALureSkyRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ULureDayClockComponent* Clock = FindClock();
	if (!Clock || Anchors.IsEmpty())
	{
		return;
	}
	const double Hour = Clock->GetHour();
	if (bDirty || FMath::Abs(Hour - AppliedHour) > 1e-6) // a frozen clock costs nothing
	{
		Apply(Hour);
	}
}

void ALureSkyRig::ResolveTargets()
{
	using namespace LureSkyRigPrivate;
	UWorld* World = GetWorld();
	if (!MoonLight)
	{
		MoonLight = FindTarget<ADirectionalLight>(World, LureSkyTags::Moon, [](ADirectionalLight*) { return false; });
	}
	if (!SunLight)
	{
		ADirectionalLight* Moon = MoonLight;
		auto NotMoon = [Moon](ADirectionalLight* L) { return L != Moon && !L->ActorHasTag(LureSkyTags::Moon); };
		SunLight = FindTarget<ADirectionalLight>(World, LureSkyTags::Sun, [&NotMoon](ADirectionalLight* L)
		{
			const ULightComponent* C = L->GetLightComponent();
			return NotMoon(L) && C && C->IsUsedAsAtmosphereSunLight() && C->GetAtmosphereSunLightIndex() == 0;
		});
		if (!SunLight)
		{
			SunLight = FindTarget<ADirectionalLight>(World, NAME_None, NotMoon);
		}
	}
	auto Any = [](AActor*) { return true; };
	if (!SkyAtmosphere)
	{
		SkyAtmosphere = FindTarget<ASkyAtmosphere>(World, NAME_None, Any);
	}
	if (!SkyLight)
	{
		SkyLight = FindTarget<ASkyLight>(World, NAME_None, Any);
	}
	if (!HeightFog)
	{
		HeightFog = FindTarget<AExponentialHeightFog>(World, NAME_None, Any);
	}
	if (!PostProcessVolume)
	{
		PostProcessVolume = FindTarget<APostProcessVolume>(World, NAME_None, [](APostProcessVolume* V) { return V->bUnbound != 0; });
	}

	const TPair<const TCHAR*, bool> Found[] = {
		{ TEXT("sun (DirectionalLight, tag Lure.Sun)"), SunLight != nullptr },
		{ TEXT("SkyAtmosphere"), SkyAtmosphere != nullptr },
		{ TEXT("SkyLight"), SkyLight != nullptr },
		{ TEXT("ExponentialHeightFog"), HeightFog != nullptr },
		{ TEXT("unbound PostProcessVolume (exposure)"), PostProcessVolume != nullptr } };
	for (const TPair<const TCHAR*, bool>& Target : Found)
	{
		if (!Target.Value)
		{
			UE_LOG(LogLureDayNight, Warning, TEXT("Sky rig '%s': no %s in the level; it is skipped."), *GetName(), Target.Key);
		}
	}
	if (!MoonLight)
	{
		UE_LOG(LogLureDayNight, Log, TEXT("Sky rig '%s': no moon (DirectionalLight, tag Lure.Moon); the moon is skipped."), *GetName());
	}

	MakeMovable(SunLight);
	MakeMovable(MoonLight);
	if (USkyLightComponent* Sky = SkyLight ? SkyLight->GetLightComponent() : nullptr)
	{
		if (!Sky->bRealTimeCapture)
		{
			// Real-time capture follows the sky as it changes (time-sliced by the renderer), with no manual recapture.
			Sky->SetMobility(EComponentMobility::Movable);
			Sky->SetRealTimeCapture(true);
		}
	}
}

void ALureSkyRig::GatherNightLights()
{
	NightLights.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!IsValid(*It) || !It->ActorHasTag(LureSkyTags::NightLight))
		{
			continue;
		}
		TInlineComponentArray<ULightComponent*> Lights(*It);
		for (ULightComponent* Light : Lights)
		{
			NightLights.Add({ Light, Light->Intensity });
		}
	}
}

ULureDayClockComponent* ALureSkyRig::FindClock()
{
	if (ULureDayClockComponent* Clock = BoundClock.Get())
	{
		return Clock;
	}
	ULureDayClockComponent* Clock = ULureDayClockComponent::Get(this);
	if (Clock)
	{
		BoundClock = Clock;
		Clock->OnPhaseChanged.AddUniqueDynamic(this, &ALureSkyRig::HandlePhaseChanged);
		ClockChangedHandle = Clock->OnClockChanged.AddUObject(this, &ALureSkyRig::HandleClockChanged);
		RebuildAnchors();
	}
	return Clock;
}

void ALureSkyRig::RebuildAnchors()
{
	const ULureDayClockComponent* Clock = BoundClock.Get();
	Anchors = FLureTimeOfDayBlend::BuildAnchors(RegionRows, Clock ? Clock->GetClock() : FLureDayClock());
	bDirty = true;
}

void ALureSkyRig::ApplyNow()
{
	const ULureDayClockComponent* Clock = FindClock();
	if (Clock && !Anchors.IsEmpty() && HasActorBegunPlay() && GetNetMode() != NM_DedicatedServer)
	{
		Apply(Clock->GetHour());
	}
}

void ALureSkyRig::HandlePhaseChanged(ELureDayPhase NewPhase, ELureDayPhase OldPhase)
{
	ApplyNow();
}

void ALureSkyRig::HandleClockChanged()
{
	RebuildAnchors(); // the clock's row (phase midpoints) may have changed too
	ApplyNow();
}

void ALureSkyRig::Apply(double Hour)
{
	const ULureDayClockComponent* Clock = BoundClock.Get();
	if (!Clock)
	{
		return;
	}
	const FLureDayClock& DayClock = Clock->GetClock();
	const FLureTimeOfDayLook Look = FLureTimeOfDayBlend::Evaluate(Anchors, Hour);

	if (SunLight)
	{
		SunLight->SetActorRotation(FLureTimeOfDayBlend::SunRotation(Hour, Look.SunElevation, DayClock, EastYawDegrees));
		if (ULightComponent* Light = SunLight->GetLightComponent())
		{
			Light->SetIntensity(Look.SunIntensity);
			Light->SetLightColor(Look.SunColor, /*bSRGB*/ true); // linear in, stored as sRGB FColor
		}
	}
	if (MoonLight)
	{
		MoonLight->SetActorRotation(FLureTimeOfDayBlend::MoonRotation(Hour, Look.SunElevation, DayClock, EastYawDegrees));
		if (ULightComponent* Light = MoonLight->GetLightComponent())
		{
			Light->SetIntensity(Look.MoonIntensity);
			Light->SetLightColor(Look.MoonColor, true);
		}
	}
	if (USkyAtmosphereComponent* Sky = SkyAtmosphere ? SkyAtmosphere->GetComponent() : nullptr)
	{
		Sky->SetSkyLuminanceFactor(Look.SkyLuminanceFactor);
	}
	if (USkyLightComponent* Sky = SkyLight ? SkyLight->GetLightComponent() : nullptr)
	{
		Sky->SetIntensity(Look.SkyLightIntensity);
	}
	if (UExponentialHeightFogComponent* Fog = HeightFog ? HeightFog->GetComponent() : nullptr)
	{
		Fog->SetFogDensity(Look.FogDensity);
		Fog->SetFogHeightFalloff(Look.FogHeightFalloff);
		Fog->SetFogInscatteringColor(Look.GetFogSceneColor());
		Fog->SetStartDistance(Look.FogStartDistance);
		Fog->SetFogMaxOpacity(Look.FogMaxOpacity);
		Fog->SetSkyAtmosphereAmbientContributionColorScale(FLinearColor(Look.FogSkyAmbient, Look.FogSkyAmbient, Look.FogSkyAmbient, 1.f));
	}
	if (PostProcessVolume)
	{
		FLureTimeOfDayBlend::ApplyFixedExposure(PostProcessVolume->Settings, Look.ExposureEV100);
	}
	const float Share = FMath::Clamp(Look.NightLightIntensity, 0.f, 1.f);
	for (const FNightLight& Night : NightLights)
	{
		if (ULightComponent* Light = Night.Light.Get())
		{
			Light->SetIntensity(Night.FullIntensity * Share);
			const bool bOn = Share > UE_KINDA_SMALL_NUMBER;
			if (Light->IsVisible() != bOn)
			{
				Light->SetVisibility(bOn);
			}
		}
	}
	// Water (WaveScale, shallow/deep/foam colours) goes to ULureWaterSurfaceSubsystem once T-069a is on main.

	AppliedLook = Look;
	AppliedHour = Hour;
	++ApplyCount;
	bDirty = false;
}

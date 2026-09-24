// Lure: dev-only console commands for the water model and hot spots (T-027).

#include "Dev/LureWaterDevCommands.h"

#if !UE_BUILD_SHIPPING

#include "Dev/LureDevCommands.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishingWater.h"
#include "Fishing/LureHotSpot.h"
#include "Fishing/LureHotSpotSpawner.h"
#include "Fishing/LureWaterArea.h"
#include "Fishing/LureWaterSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

const TCHAR* const FLureWaterDevCommands::ShowCommand = TEXT("Lure.Water.Show");
const TCHAR* const FLureWaterDevCommands::ProbeCommand = TEXT("Lure.Water.Probe");
const TCHAR* const FLureWaterDevCommands::SpawnHotSpotCommand = TEXT("Lure.HotSpot.Spawn");
const TCHAR* const FLureWaterDevCommands::ClearHotSpotsCommand = TEXT("Lure.HotSpot.Clear");

namespace LureWaterDevPrivate
{
	float ParseFloatArg(const TArray<FString>& Args, int32 Index, float Default)
	{
		return (Args.IsValidIndex(Index) && FCString::IsNumeric(*Args[Index])) ? FCString::Atof(*Args[Index]) : Default;
	}

	FString DescribeContext(const FLureWaterContext& Water)
	{
		if (!Water.bOnWater)
		{
			return TEXT("land (no water here)");
		}
		const TCHAR* Source = Water.Source == ELureWaterSource::Area ? TEXT("area")
			: Water.Source == ELureWaterSource::LegacySpot ? TEXT("legacy spot") : TEXT("default water");
		return FString::Printf(TEXT("%s %s, habitat %s, region %s, luck %.2f, depth %.0f cm, surface z %.0f"), Source,
			Water.AreaId.IsNone() ? TEXT("-") : *Water.AreaId.ToString(), *Water.HabitatTag.ToString(),
			Water.RegionTag.IsValid() ? *Water.RegionTag.ToString() : TEXT("(default)"), Water.Luck, Water.DepthCm, Water.WaterZ);
	}
}

bool FLureWaterDevCommands::GetPointInFront(UWorld* World, float Distance, FVector2D& OutXY)
{
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}
	const FRotator Yaw(0.0, Controller->GetControlRotation().Yaw, 0.0);
	const FVector Point = Pawn->GetActorLocation() + Yaw.Vector() * FMath::Max(0.f, Distance);
	OutXY = FVector2D(Point.X, Point.Y);
	return true;
}

ALureHotSpot* FLureWaterDevCommands::SpawnHotSpotInFront(UWorld* World, FName TypeId, float Distance, float Lifetime, FString& OutMessage)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		OutMessage = TEXT("Lure.HotSpot.Spawn works on the server or in standalone only.");
		return nullptr;
	}
	FVector2D XY;
	if (!GetPointInFront(World, Distance, XY))
	{
		OutMessage = TEXT("no local player with a pawn");
		return nullptr;
	}
	const TArray<TPair<FName, FLureHotSpotRow>> Rows = FLureWaterQuery::GetHotSpotRows(FLureWaterQuery::LoadHotSpotTable());
	const TPair<FName, FLureHotSpotRow>* Row = TypeId.IsNone() ? (Rows.Num() > 0 ? &Rows[0] : nullptr)
		: Rows.FindByPredicate([TypeId](const TPair<FName, FLureHotSpotRow>& Entry) { return Entry.Key == TypeId; });
	if (!Row)
	{
		TArray<FString> Names;
		for (const TPair<FName, FLureHotSpotRow>& Entry : Rows)
		{
			Names.Add(Entry.Key.ToString());
		}
		OutMessage = FString::Printf(TEXT("unknown hot spot type '%s' (DT_HotSpot rows: %s)"), *TypeId.ToString(), *FString::Join(Names, TEXT(", ")));
		return nullptr;
	}
	float WaterZ = 0.f;
	float Depth = 0.f;
	if (!FLureWaterQuery::ProbeWater(World, XY, WaterZ, Depth))
	{
		OutMessage = FString::Printf(TEXT("no fishable water at (%.0f, %.0f): land or no water surface"), XY.X, XY.Y);
		return nullptr;
	}
	const TArray<FLureWaterAreaInfo> Areas = FLureWaterQuery::GatherAreas(World);
	const int32 AreaIndex = FLureWaterRules::FindAreaIndex(Areas, XY, Depth);
	const float Life = Lifetime > 0.f ? Lifetime : FLureWaterRules::LifetimeFromRoll(Row->Value, 0.5f);
	ALureHotSpot* HotSpot = ALureHotSpot::SpawnHotSpot(World, nullptr, Row->Key, Row->Value, AreaIndex != INDEX_NONE ? Areas[AreaIndex].AreaId : NAME_None,
		FVector(XY.X, XY.Y, WaterZ), FFishRoll::MakeRandomSeed(), FLureWaterQuery::GetTime(World), Life);
	OutMessage = HotSpot ? FString::Printf(TEXT("hot spot %s (%s) at (%.0f, %.0f, %.0f), %.0f cm deep, for %.0f s"), *HotSpot->GetName(),
		*Row->Key.ToString(), XY.X, XY.Y, WaterZ, Depth, Life) : TEXT("the world refused to spawn it");
	return HotSpot;
}

int32 FLureWaterDevCommands::ClearHotSpots(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return 0;
	}
	int32 Count = 0;
	for (TActorIterator<ALureHotSpot> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			It->Destroy();
			++Count;
		}
	}
	return Count;
}

FString FLureWaterDevCommands::DescribeWater(UWorld* World, float DrawSeconds)
{
	using namespace LureWaterDevPrivate;
	if (!World)
	{
		return TEXT("no world");
	}
	TArray<FString> Lines;
	bool bLegacy = false;
	const TArray<FLureWaterAreaInfo> Areas = FLureWaterQuery::GatherAreas(World, &bLegacy);
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();
	Lines.Add(FString::Printf(TEXT("%d water area(s)%s; default water habitat %s; gap fallbacks %s"), Areas.Num(),
		bLegacy ? TEXT(" (legacy fishing spots: this level has no water areas yet)") : TEXT(""), *Settings->DefaultWaterHabitat.ToString(),
		*FString::JoinBy(Settings->GapFallbackHabitats, TEXT(", "), [](const FName& Name) { return Name.ToString(); })));
	for (const FLureWaterAreaInfo& Area : Areas)
	{
		static const TCHAR* ShapeNames[] = { TEXT("circle"), TEXT("box"), TEXT("polygon"), TEXT("everywhere") };
		Lines.Add(FString::Printf(TEXT("  %s \"%s\": %s, %s, priority %d, luck %.2f, depth %.0f-%s cm"), *Area.AreaId.ToString(), *Area.GetLabel(),
			ShapeNames[FMath::Clamp(static_cast<int32>(Area.Shape), 0, 3)], *Area.HabitatTag.ToString(), Area.Priority, Area.Luck, Area.MinDepth,
			Area.MaxDepth > 0.f ? *FString::Printf(TEXT("%.0f"), Area.MaxDepth) : TEXT("any")));
	}
	const double Now = FLureWaterQuery::GetTime(World);
	const TArray<ALureHotSpot*> HotSpots = ALureHotSpotSpawner::GetLiveHotSpots(World, Now);
	Lines.Add(FString::Printf(TEXT("%d hot spot(s)"), HotSpots.Num()));
	for (const ALureHotSpot* HotSpot : HotSpots)
	{
		const FVector Center = HotSpot->GetCenterAt(Now);
		Lines.Add(FString::Printf(TEXT("  %s (%s) in %s at (%.0f, %.0f), radius %.0f, %.0f s left"), *HotSpot->GetName(), *HotSpot->GetState().TypeId.ToString(),
			HotSpot->GetState().AreaId.IsNone() ? TEXT("default water") : *HotSpot->GetState().AreaId.ToString(), Center.X, Center.Y,
			HotSpot->GetState().Radius, HotSpot->GetState().EndTime - Now));
	}
#if ENABLE_DRAW_DEBUG
	if (DrawSeconds > 0.f)
	{
		for (TActorIterator<ALureWaterArea> It(World); It; ++It)
		{
			const FLureWaterAreaInfo Area = It->GetWaterArea();
			const float Z = static_cast<float>(It->GetActorLocation().Z) + 10.f;
			const TArray<FVector> Points = It->GetOutlinePoints(Z);
			const FColor Color = ALureWaterArea::GetHabitatColor(Area.HabitatTag).ToFColor(true);
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				DrawDebugLine(World, Points[Index], Points[(Index + 1) % Points.Num()], Color, false, DrawSeconds, 0, 10.f);
			}
			DrawDebugString(World, FVector(Area.Center.X, Area.Center.Y, Z + 150.f), FString::Printf(TEXT("%s\n%s P%d"), *Area.GetLabel(),
				*Area.HabitatTag.ToString(), Area.Priority), nullptr, Color, DrawSeconds);
		}
		if (bLegacy)
		{
			for (const FLureWaterAreaInfo& Area : Areas)
			{
				DrawDebugCircle(World, FVector(Area.Center.X, Area.Center.Y, 10.f), Area.Radius, 48, FColor::Orange, false, DrawSeconds, 0, 10.f,
					FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
			}
		}
		for (const ALureHotSpot* HotSpot : HotSpots)
		{
			const FVector Center = HotSpot->GetCenterAt(Now) + FVector(0.f, 0.f, 15.f);
			DrawDebugCircle(World, Center, HotSpot->GetState().Radius, 32, FColor::Cyan, false, DrawSeconds, 0, 10.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
			DrawDebugCircle(World, FVector(HotSpot->GetState().Anchor) + FVector(0.f, 0.f, 12.f), HotSpot->GetState().DriftRange + HotSpot->GetState().Radius, 32,
				FColor::Blue, false, DrawSeconds, 0, 4.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		}
	}
#endif
	return FString::Join(Lines, TEXT("\n"));
}

FString FLureWaterDevCommands::ProbeInFront(UWorld* World, float Distance)
{
	using namespace LureWaterDevPrivate;
	FVector2D XY;
	if (!GetPointInFront(World, Distance, XY))
	{
		return TEXT("no local player with a pawn");
	}
	float WaterZ = 0.f;
	float Depth = 0.f;
	if (!FLureWaterQuery::ProbeWater(World, XY, WaterZ, Depth))
	{
		return FString::Printf(TEXT("(%.0f, %.0f): land or no water"), XY.X, XY.Y);
	}
	const FLureWaterContext Water = FLureWaterQuery::DescribeWater(World, FVector(XY.X, XY.Y, WaterZ), true, WaterZ);
	const ALureHotSpot* HotSpot = FLureWaterQuery::FindHotSpotAt(World, XY, FLureWaterQuery::GetTime(World));
	return FString::Printf(TEXT("(%.0f, %.0f): %s%s"), XY.X, XY.Y, *DescribeContext(Water),
		HotSpot ? *FString::Printf(TEXT(", in hot spot %s (%s)"), *HotSpot->GetName(), *HotSpot->GetState().TypeId.ToString()) : TEXT(""));
}

namespace LureWaterDevPrivate
{
	FAutoConsoleCommandWithWorldArgsAndOutputDevice ShowConsoleCommand(
		FLureWaterDevCommands::ShowCommand,
		TEXT("Lure.Water.Show [Seconds]: draws and lists every water area (colored by habitat) and hot spot (T-027)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			const FString Text = FLureWaterDevCommands::DescribeWater(World, ParseFloatArg(Args, 0, 20.f));
			UE_LOG(LogLureDev, Display, TEXT("Lure.Water.Show:\n%s"), *Text);
			Ar.Log(Text);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ProbeConsoleCommand(
		FLureWaterDevCommands::ProbeCommand,
		TEXT("Lure.Water.Probe [Distance]: the water area, habitat, depth and hot spot Distance cm (default 1000) in front of you."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			const FString Text = FLureWaterDevCommands::ProbeInFront(World, ParseFloatArg(Args, 0, 1000.f));
			UE_LOG(LogLureDev, Display, TEXT("Lure.Water.Probe: %s"), *Text);
			Ar.Log(Text);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice SpawnHotSpotConsoleCommand(
		FLureWaterDevCommands::SpawnHotSpotCommand,
		TEXT("Lure.HotSpot.Spawn [Type] [Distance] [Lifetime]: a hot spot on the water Distance cm (default 1000) in front of you. Server/standalone only."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			// "Lure.HotSpot.Spawn 800" (a number first) means the default type at 800 cm.
			int32 Next = 0;
			FName Type = NAME_None;
			if (Args.IsValidIndex(0) && !FCString::IsNumeric(*Args[0]))
			{
				Type = FName(*Args[0]);
				Next = 1;
			}
			FString Message;
			const ALureHotSpot* HotSpot = FLureWaterDevCommands::SpawnHotSpotInFront(World, Type, ParseFloatArg(Args, Next, 1000.f), ParseFloatArg(Args, Next + 1, 0.f), Message);
			if (HotSpot)
			{
				UE_LOG(LogLureDev, Display, TEXT("Lure.HotSpot.Spawn: %s"), *Message);
			}
			else
			{
				UE_LOG(LogLureDev, Warning, TEXT("Lure.HotSpot.Spawn refused: %s"), *Message);
			}
			Ar.Log(Message);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ClearHotSpotsConsoleCommand(
		FLureWaterDevCommands::ClearHotSpotsCommand,
		TEXT("Lure.HotSpot.Clear: removes every hot spot. Server/standalone only."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			const int32 Count = FLureWaterDevCommands::ClearHotSpots(World);
			UE_LOG(LogLureDev, Display, TEXT("Lure.HotSpot.Clear: removed %d hot spot(s)."), Count);
			Ar.Logf(TEXT("removed %d hot spot(s)"), Count);
		}));
}

#endif // !UE_BUILD_SHIPPING

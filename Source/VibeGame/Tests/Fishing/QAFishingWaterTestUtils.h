// Lure T-027 QA (qa-engineer): helpers for the independent "fish anywhere + hot spots" tests (Project.Fishing.Water.QA.*).
// Black-box: expectations come from docs/specs/fishing-water-rules.md, GAME_DESIGN.md "Fish: Anywhere, Hot spots" and the
// T-027 acceptance line in docs/TASKS.md, plus the contract comments in Fishing/FishingWater*.h, never from the .cpp files.
// Tables come from the text sources in data/tables/, never the binary /Game/Data assets.
// Everything is inline in namespace LureWaterQA (unity builds merge test files: no file-scope using-directives).

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/FishingWater.h"
#include "Fishing/FishingWaterTypes.h"
#include "Fishing/LureHotSpot.h"
#include "Fishing/LureWaterArea.h"
#include "Fishing/LureWaterSettings.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/FishQATestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace LureWaterQA
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = 1.f / 60.f;
	inline float NaN() { return std::numeric_limits<float>::quiet_NaN(); }
	inline float Inf() { return std::numeric_limits<float>::infinity(); }

	inline FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
	}

	/** Every registered tag under Parent (Parent itself included), sorted by name. */
	inline TArray<FGameplayTag> TagsUnder(const TCHAR* Parent)
	{
		TArray<FGameplayTag> Out;
		const FGameplayTag Root = Tag(Parent);
		if (!Root.IsValid())
		{
			return Out;
		}
		Out.Add(Root);
		const FGameplayTagContainer Children = UGameplayTagsManager::Get().RequestGameplayTagChildren(Root);
		for (const FGameplayTag& Child : Children)
		{
			Out.AddUnique(Child);
		}
		Out.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.GetTagName().LexicalLess(B.GetTagName()); });
		return Out;
	}

	// ---- Pure area builders (independent of the implementer's helpers) ----

	inline FLureWaterAreaInfo QAArea(const TCHAR* Id, const TCHAR* Habitat, ELureWaterAreaShape Shape, int32 Priority)
	{
		FLureWaterAreaInfo Area;
		Area.AreaId = FName(Id);
		Area.DisplayName = FString(Id) + TEXT(" (QA)");
		Area.HabitatTag = Tag(Habitat);
		Area.Priority = Priority;
		Area.Shape = Shape;
		Area.Source = ELureWaterSource::Area;
		return Area;
	}

	inline FLureWaterAreaInfo QACircle(const TCHAR* Id, const TCHAR* Habitat, double X, double Y, float Radius, int32 Priority = 0)
	{
		FLureWaterAreaInfo Area = QAArea(Id, Habitat, ELureWaterAreaShape::Circle, Priority);
		Area.Center = FVector2D(X, Y);
		Area.Radius = Radius;
		return Area;
	}

	inline FLureWaterAreaInfo QABox(const TCHAR* Id, const TCHAR* Habitat, double X, double Y, double HalfX, double HalfY, float Yaw, int32 Priority = 0)
	{
		FLureWaterAreaInfo Area = QAArea(Id, Habitat, ELureWaterAreaShape::Box, Priority);
		Area.Center = FVector2D(X, Y);
		Area.HalfSize = FVector2D(HalfX, HalfY);
		Area.YawDegrees = Yaw;
		return Area;
	}

	inline FLureWaterAreaInfo QAPolygon(const TCHAR* Id, const TCHAR* Habitat, const TArray<FVector2D>& Points, int32 Priority = 0)
	{
		FLureWaterAreaInfo Area = QAArea(Id, Habitat, ELureWaterAreaShape::Polygon, Priority);
		Area.Polygon = Points;
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& Point : Points)
		{
			Sum += Point;
		}
		Area.Center = Points.Num() > 0 ? Sum / Points.Num() : FVector2D::ZeroVector;
		return Area;
	}

	inline FLureWaterAreaInfo QAEverywhere(const TCHAR* Id, const TCHAR* Habitat, int32 Priority, float MinDepth = 0.f, float MaxDepth = 0.f)
	{
		FLureWaterAreaInfo Area = QAArea(Id, Habitat, ELureWaterAreaShape::Everywhere, Priority);
		Area.MinDepth = MinDepth;
		Area.MaxDepth = MaxDepth;
		return Area;
	}

	/** The id of the winning area at XY and depth, or "default" for default water. */
	inline FString WinnerAt(TConstArrayView<FLureWaterAreaInfo> Areas, double X, double Y, float Depth = 500.f)
	{
		const int32 Index = FLureWaterRules::FindAreaIndex(Areas, FVector2D(X, Y), Depth);
		return Index == INDEX_NONE ? FString(TEXT("default")) : Areas[Index].AreaId.ToString();
	}

	/** A deep-water context in the given habitat (a bobber that landed in water 5 m deep). */
	inline FLureWaterContext DeepWater(const TCHAR* Habitat, const TCHAR* Region = TEXT("Region.Tropical.PalmKey"), float Depth = 500.f)
	{
		FLureWaterContext Water;
		Water.bOnWater = true;
		Water.Source = ELureWaterSource::Area;
		Water.AreaId = TEXT("qa_water");
		Water.AreaName = TEXT("QA water");
		Water.HabitatTag = Tag(Habitat);
		Water.RegionTag = Region ? Tag(Region) : FGameplayTag();
		Water.DepthCm = Depth;
		return Water;
	}

	inline FLureFishingEnvironment Environment(float Hours, const TCHAR* Bait = TEXT("Bait.Shrimp"))
	{
		FLureFishingEnvironment Env;
		Env.TimeOfDayHours = Hours;
		Env.BaitTag = Bait ? Tag(Bait) : FGameplayTag();
		Env.DefaultRegionTag = Tag(TEXT("Region.Tropical"));
		return Env;
	}

	/** The shipped bite rules (MinBiteDepth 15, gap fallbacks Shore then Reef), read from the settings. */
	inline FLureBiteRules ShippedRules()
	{
		return FLureBiteRules::FromSettings();
	}

	inline FLureBiteRules Rules(float MinBiteDepth, std::initializer_list<const TCHAR*> Fallbacks)
	{
		FLureBiteRules Out;
		Out.MinBiteDepth = MinBiteDepth;
		for (const TCHAR* Name : Fallbacks)
		{
			Out.GapFallbackHabitats.Add(Tag(Name));
		}
		return Out;
	}

	inline const TCHAR* ReasonName(ELureNoBiteReason Reason)
	{
		switch (Reason)
		{
		case ELureNoBiteReason::None: return TEXT("None");
		case ELureNoBiteReason::NotWater: return TEXT("NotWater");
		case ELureNoBiteReason::TooShallow: return TEXT("TooShallow");
		case ELureNoBiteReason::NoSpecies: return TEXT("NoSpecies");
		case ELureNoBiteReason::WrongBait: return TEXT("WrongBait");
		default: return TEXT("?");
		}
	}

	// ---- Text sources ----

	inline bool LoadProjectText(FAutomationTestBase& Test, const FString& RelativePath, FString& Out)
	{
		return Test.TestTrue(RelativePath + TEXT(" loads"), FFileHelper::LoadFileToString(Out, *(FPaths::ProjectDir() / RelativePath)) && !Out.IsEmpty());
	}

	/** DT_HotSpot built from data/tables/DT_HotSpot.json (import problems are test errors). */
	inline UDataTable* LoadHotSpotTable(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out)
	{
		FString Json;
		if (!LoadProjectText(Test, TEXT("data/tables/DT_HotSpot.json"), Json))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureHotSpotRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromJSONString(Json);
		for (const FString& Problem : Problems)
		{
			Test.AddError(TEXT("DT_HotSpot.json import: ") + Problem);
		}
		Out.Reset(Table);
		return Table;
	}

	/** Every data/levels/*.json layout: file name -> root object. */
	inline TArray<TPair<FString, TSharedPtr<FJsonObject>>> LoadLayouts(FAutomationTestBase& Test)
	{
		TArray<TPair<FString, TSharedPtr<FJsonObject>>> Out;
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(FPaths::ProjectDir() / TEXT("data/levels/*.json")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			FString Text;
			TSharedPtr<FJsonObject> Root;
			if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("data/levels") / File))
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
			{
				Test.AddError(TEXT("layout does not parse as JSON: ") + File);
				continue;
			}
			Out.Emplace(File, Root);
		}
		return Out;
	}

	/** The markers of a layout with the given type. */
	inline TArray<TSharedPtr<FJsonObject>> MarkersOfType(const TSharedPtr<FJsonObject>& Root, const TCHAR* Type)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("markers"), List))
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> Marker = Value.IsValid() ? Value->AsObject() : nullptr;
			FString MarkerType;
			if (Marker.IsValid() && Marker->TryGetStringField(TEXT("type"), MarkerType) && MarkerType == Type)
			{
				Out.Add(Marker);
			}
		}
		return Out;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

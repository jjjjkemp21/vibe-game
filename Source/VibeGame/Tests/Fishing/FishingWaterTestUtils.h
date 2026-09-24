// Lure T-027 (unreal-engineer): shared helpers for the water-area and hot-spot tests (Project.Fishing.Water.*).
// Spec: docs/specs/fishing-water-rules.md. Tables come from the text sources in data/tables/, never the binary assets.
// Everything is inline in namespace LureWaterTest (unity builds merge test files: no file-scope using-directives).

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fish/FishRoll.h"
#include "Fishing/FishingTypes.h"
#include "Fishing/FishingWater.h"
#include "Fishing/FishingWaterTypes.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Fishing/LureHotSpot.h"
#include "Fishing/LureWaterArea.h"
#include "Fishing/LureWaterSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/FishQATestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace LureWaterTest
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float DockTop = 100.f;
	constexpr float DockHalf = 400.f;
	/** The player stands on the dock's +X edge, facing +X over the water. A cast at charge C lands about 250 + 300 + 1500 C cm out. */
	inline const FVector StandFeet() { return FVector(250.f, 0.f, DockTop); }
	/** Where a charge-0.5 cast lands (x), give or take a few cm. */
	constexpr float HalfCastX = 1300.f;

	inline FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
	}

	inline FString LoadText(const TCHAR* RelativePath)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / RelativePath));
		return Text;
	}

	// ---- Pure area builders ----

	inline FLureWaterAreaInfo MakeArea(FName Id, const TCHAR* Habitat, ELureWaterAreaShape Shape, int32 Priority = 0)
	{
		FLureWaterAreaInfo Area;
		Area.AreaId = Id;
		Area.DisplayName = Id.ToString();
		Area.HabitatTag = Tag(Habitat);
		Area.Priority = Priority;
		Area.Shape = Shape;
		return Area;
	}

	inline FLureWaterAreaInfo Circle(FName Id, const TCHAR* Habitat, const FVector2D& Center, float Radius, int32 Priority = 0)
	{
		FLureWaterAreaInfo Area = MakeArea(Id, Habitat, ELureWaterAreaShape::Circle, Priority);
		Area.Center = Center;
		Area.Radius = Radius;
		return Area;
	}

	inline FLureWaterAreaInfo Box(FName Id, const TCHAR* Habitat, const FVector2D& Center, const FVector2D& HalfSize, float Yaw, int32 Priority = 0)
	{
		FLureWaterAreaInfo Area = MakeArea(Id, Habitat, ELureWaterAreaShape::Box, Priority);
		Area.Center = Center;
		Area.HalfSize = HalfSize;
		Area.YawDegrees = Yaw;
		return Area;
	}

	inline FLureWaterAreaInfo Everywhere(FName Id, const TCHAR* Habitat, int32 Priority, float MinDepth = 0.f, float MaxDepth = 0.f)
	{
		FLureWaterAreaInfo Area = MakeArea(Id, Habitat, ELureWaterAreaShape::Everywhere, Priority);
		Area.MinDepth = MinDepth;
		Area.MaxDepth = MaxDepth;
		return Area;
	}

	/** A hot spot row with explicit numbers (DT_HotSpot shape, not the shipped values). */
	inline FLureHotSpotRow TestHotSpotRow(float Radius = 600.f, float LuckBonus = 3.f, float SizeBonus = 0.5f, float ValueMultiplier = 2.f, float BiteWaitScale = 0.5f)
	{
		FLureHotSpotRow Row;
		Row.Radius = Radius;
		Row.DriftSpeed = 0.f;
		Row.DriftRange = 0.f;
		Row.MinDepth = 0.f;
		Row.LuckBonus = LuckBonus;
		Row.SizeBonus = SizeBonus;
		Row.ValueMultiplier = ValueMultiplier;
		Row.BiteWaitScale = BiteWaitScale;
		return Row;
	}

	/** Changes one property of a mutable settings default for the scope. */
	template <typename TSettings, typename TValue>
	struct TScopedSetting
	{
		TValue TSettings::* Member;
		TValue Saved;
		TScopedSetting(TValue TSettings::* InMember, const TValue& Value)
			: Member(InMember)
		{
			TSettings* Settings = GetMutableDefault<TSettings>();
			Saved = Settings->*Member;
			Settings->*Member = Value;
		}
		~TScopedSetting()
		{
			GetMutableDefault<TSettings>()->*Member = Saved;
		}
	};

	/** A flow profile from the built-in (= shipped) row: the bite BiteWait s after landing, no nibbles, stays hooked. */
	inline FLureFishingRow QuickProfile(float BiteWait = 0.5f)
	{
		FLureFishingRow Row = FLureFishingRules::GetFallbackRow();
		Row.BiteWaitMin = Row.BiteWaitMax = BiteWait;
		Row.RebiteWaitMin = Row.RebiteWaitMax = 1.f;
		Row.NibblesMin = Row.NibblesMax = 0;
		Row.HookWindow = 0.8f;
		Row.AutoLandDelay = 0.f;
		Row.NoBiteHintDelay = 1.f;
		return Row;
	}

	/**
	 *  A transient game world (the project's game mode): an 8 x 8 m dock at the origin (top z = 100) and the sea at the
	 *  settings' fallback water z = 0 (no water volume, no seabed unless added).
	 */
	struct FWaterWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		TStrongObjectPtr<UDataTable> Movement;
		FishQA::FTables Fish;

		bool Create(FAutomationTestBase& Test, bool bDock = true)
		{
			FString Csv = LoadText(TEXT("data/tables/DT_Movement.csv"));
			if (!Test.TestFalse(TEXT("data/tables/DT_Movement.csv loads"), Csv.IsEmpty()))
			{
				return false;
			}
			UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
			Table->RowStruct = FLureMovementRow::StaticStruct();
			Table->CreateTableFromCSVString(Csv);
			Movement.Reset(Table);
			if (!FishQA::LoadReal(Test, Fish))
			{
				return false;
			}
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			if (World && bDock)
			{
				AddBox(FVector(0.f, 0.f, DockTop * 0.5f), FVector(DockHalf, DockHalf, DockTop * 0.5f));
			}
			return World != nullptr;
		}

		AActor* AddBox(const FVector& Center, const FVector& Extent)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
			Box->SetMobility(EComponentMobility::Static);
			Box->SetBoxExtent(Extent, false);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->SetRelativeLocation_Direct(Center);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			return Actor;
		}

		/** A solid seabed whose top is TopZ (the water depth is -TopZ with the sea at 0), HalfSize wide around Center. */
		AActor* AddSeabed(float TopZ, const FVector2D& Center = FVector2D(5000.0, 0.0), float HalfSize = 4500.f)
		{
			return AddBox(FVector(Center.X, Center.Y, TopZ - 50.f), FVector(HalfSize, HalfSize, 50.f));
		}

		/** A water area actor configured like the level builder does (the Set* functions). */
		ALureWaterArea* AddArea(FName Id, const TCHAR* Habitat, const FVector& Location, float Yaw, int32 Priority, TFunctionRef<void(ALureWaterArea&)> Shape,
			const TCHAR* Region = nullptr, float Luck = 0.f)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ALureWaterArea* Area = World->SpawnActor<ALureWaterArea>(ALureWaterArea::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), Location), Params);
			if (!Area)
			{
				return nullptr;
			}
			Area->AreaId = Id;
			Area->DisplayName = Id.ToString() + TEXT(" water");
			Area->Priority = Priority;
			Area->Luck = Luck;
			Area->SetAreaTags(FName(Habitat), Region ? FName(Region) : NAME_None);
			Shape(*Area);
			return Area;
		}

		ALureWaterArea* AddCircleArea(FName Id, const TCHAR* Habitat, const FVector2D& Center, float Radius, int32 Priority = 0, const TCHAR* Region = nullptr, float Luck = 0.f)
		{
			return AddArea(Id, Habitat, FVector(Center.X, Center.Y, 0.0), 0.f, Priority, [Radius](ALureWaterArea& Area) { Area.SetShapeCircle(Radius); }, Region, Luck);
		}

		/** A legacy fishing_spot marker (a plain actor with the builder's tags). */
		AActor* AddSpot(const FVector& Location, const TArray<FString>& KeyValues)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None);
			Root->SetRelativeLocation_Direct(Location);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->Tags.Add(GetDefault<ULureFishingSettings>()->FishingSpotTag);
			for (const FString& KeyValue : KeyValues)
			{
				Actor->Tags.Add(FName(*KeyValue));
			}
			return Actor;
		}

		ALurePlayerCharacter* Spawn(FAutomationTestBase& Test, const FVector& Feet = StandFeet(), float Yaw = 0.f)
		{
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
			const float HalfHeight = Rows.Num() > 0 ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.f;
			const FTransform Transform(FRotator(0.f, Yaw, 0.f), Feet + FVector(0.f, 0.f, HalfHeight + 2.15f));
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character || !Character->GetLureMovement() || !Character->GetFishing())
			{
				Test.AddError(TEXT("ALurePlayerCharacter did not spawn with its movement and fishing components"));
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Movement.Get());
			Character->GetLureMovement()->bRunPhysicsWithNoController = true;
			Character->FinishSpawning(Transform);
			return Character;
		}

		/** Possesses Character with a new local player controller (the dev commands look for the first player). */
		APlayerController* PossessLocally(ALurePlayerCharacter* Character)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APlayerController* Controller = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity, Params);
			if (Controller && Character)
			{
				Controller->SetAsLocalPlayerController();
				Controller->Possess(Character);
				Controller->SetControlRotation(Character->GetActorRotation());
			}
			return Controller;
		}

		ULureFishingComponent* SetUpFishing(FAutomationTestBase& Test, ALurePlayerCharacter* Character, const FLureFishingRow& Profile, float Hours = 12.f, int32 Seed = 1234)
		{
			ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
			if (!Fishing)
			{
				Test.AddError(TEXT("no fishing component"));
				return nullptr;
			}
			Fishing->SetFishingProfile(Profile);
			Fishing->SetFishTables(Fish.Get());
			Fishing->SetRandomSeed(Seed);
			Fishing->TimeOfDayOverride = Hours;
			return Fishing;
		}

		void Tick(int32 Frames, float DeltaTime = Dt)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(DeltaTime);
			}
		}

		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, float DeltaTime = Dt)
		{
			for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
			{
				if (Predicate())
				{
					return true;
				}
				Wrapper.TickTestWorld(DeltaTime);
			}
			return Predicate();
		}

		double Now() const
		{
			return FLureWaterQuery::GetTime(World);
		}

		/** Ticks (steps <= Dt) until the world clock reaches Time. */
		void AdvanceTo(double Time)
		{
			for (int32 Guard = 0; Guard < 400000; ++Guard)
			{
				const double Remaining = Time - Now();
				if (Remaining <= 1.0e-7)
				{
					return;
				}
				Wrapper.TickTestWorld(static_cast<float>(FMath::Clamp(Remaining, 0.001, static_cast<double>(Dt))));
			}
		}
	};

	/** Casts along +X with Charge and ticks until the bobber waits. */
	inline bool CastAndLand(FAutomationTestBase& Test, FWaterWorld& World, ULureFishingComponent* Fishing, float Charge = 0.5f)
	{
		if (!Test.TestTrue(TEXT("cast starts"), Fishing && Fishing->AuthorityCast(Charge, 0.f)))
		{
			return false;
		}
		return Test.TestTrue(TEXT("the bobber lands (Waiting)"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 240));
	}

	/** Reflection diff of two structs of the same type: the names of the fields that differ. */
	inline TArray<FString> DiffFields(const UScriptStruct* Struct, const void* A, const void* B)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!It->Identical(It->ContainerPtrToValuePtr<void>(A), It->ContainerPtrToValuePtr<void>(B), PPF_None))
			{
				Names.Add(It->GetName());
			}
		}
		return Names;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

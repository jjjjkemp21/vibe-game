// Lure T-027c (unreal-engineer): a test world that holds a REAL map from /Game/Maps, for level smoke tests
// (Project.Level.*). The map is streamed in as a level instance of a transient game world, so the editor's own world and
// the map asset are never touched, and the test sees exactly what PIE gets: the built actors, their collision, the water
// volume, the water areas and every marker. Tests that build their own little world (Project.Fishing.Water.*) prove the
// rules; these prove that a built level and the rules work together.
// Everything is inline in namespace LureMapTest (unity builds merge test files: no file-scope using-directives).
//
// Pattern for a new level test:
//   LureMapTest::FMapWorld Map;
//   if (!Map.Create(*this, TEXT("/Game/Maps/L_PalmKey"))) return false;   // an error is already reported
//   AActor* Marker = Map.FindTagged(TEXT("Teleport=tp_dock_end"));      // markers carry the builder's tags
//   ALurePlayerCharacter* Player = Map.SpawnPlayer(*this, Marker->GetActorLocation());
//   Map.Tick(Seconds);                                                  // the whole level ticks, like PIE
//   LureMapTest::FMapWorld::TeleportPlayer(Player, NewFeet);             // "walk" somewhere else
//   Map.Release();                                                      // or let it go out of scope
//   return LureMapTest::TestNoMapCopiesLeft(*this, TEXT("/Game/Maps/L_PalmKey"));   // last line of the test
// Loading the map again for every case is cheap (the package stays loaded): use a fresh FMapWorld per seed or case.
// Teardown: each FMapWorld streams in its own package copy (/Game/Maps/<Map>_LevelInstance_<N>). Loaded map worlds carry
// RF_Standalone, an editor GC keep flag, and UWorld::DestroyWorld only clears it on levels still in the world (the forced
// unload removes the streamed one first). Release() clears the keep flags on the copy and collects garbage, as the engine
// does for PIE copies (UWorld::Serialize clears RF_Public|RF_Standalone on PKG_PlayInEditor worlds); a leaked copy makes
// the next PIE session's world leak check (UEditorEngine::CheckForWorldGCLeaks) fatal-error.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

namespace LureMapTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A transient game world (the project's game mode, standalone) with one real map streamed in at the origin. */
	struct FMapWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ULevel* Level = nullptr;
		TStrongObjectPtr<UDataTable> Movement;
		/** The package of the streamed map copy (/Game/Maps/<Map>_LevelInstance_<N>), released in Release(). */
		FName InstancePackage;

		FMapWorld() = default;
		FMapWorld(const FMapWorld&) = delete;
		FMapWorld& operator=(const FMapWorld&) = delete;
		~FMapWorld()
		{
			if (UObjectInitialized() && !IsEngineExitRequested())
			{
				Release();
			}
		}

		/** Destroys the test world and frees its map copy (keep flags cleared, then a garbage collection). Safe to call twice.
		 *  bCollectGarbage false skips the collection: a test that makes many map worlds in a row collects once per batch
		 *  itself (CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)); TestNoMapCopiesLeft still collects and checks at the end. */
		void Release(bool bCollectGarbage = true)
		{
			if (Wrapper.GetTestWorld())
			{
				Wrapper.DestroyTestWorld(false);
			}
			World = nullptr;
			Level = nullptr;
			Movement.Reset();
			if (InstancePackage.IsNone())
			{
				return;
			}
			if (UPackage* Package = FindPackage(nullptr, *InstancePackage.ToString()))
			{
				ForEachObjectWithPackage(Package, [](UObject* Object)
				{
					if (UWorld* CopyWorld = Cast<UWorld>(Object))
					{
						CopyWorld->RemoveFromRoot();
					}
					Object->ClearFlags(RF_Standalone | RF_Public);
					return true;
				});
				Package->ClearFlags(RF_Standalone);
			}
			InstancePackage = NAME_None;
			if (bCollectGarbage)
			{
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			}
		}

		/** Loads MapPackage (e.g. /Game/Maps/L_PalmKey). False, with a test error, if the map or the world can't be made. */
		bool Create(FAutomationTestBase& Test, const TCHAR* MapPackage)
		{
			if (!FPackageName::DoesPackageExist(MapPackage))
			{
				Test.AddError(FString::Printf(TEXT("%s does not exist (the editor-operator builds it from data/levels/*.json)"), MapPackage));
				return false;
			}
			FString Csv;
			if (!FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("data/tables/DT_Movement.csv"))) || Csv.IsEmpty())
			{
				Test.AddError(TEXT("data/tables/DT_Movement.csv does not load"));
				return false;
			}
			UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
			Table->RowStruct = FLureMovementRow::StaticStruct();
			Table->CreateTableFromCSVString(Csv);
			Movement.Reset(Table);
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("the test world could not be created"));
				return false;
			}
			World = Wrapper.GetTestWorld();
			bool bSuccess = false;
			ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstance(World, MapPackage, FVector::ZeroVector, FRotator::ZeroRotator, bSuccess);
			if (!bSuccess || !Streaming)
			{
				Test.AddError(FString::Printf(TEXT("%s could not be streamed into the test world"), MapPackage));
				return false;
			}
			InstancePackage = Streaming->GetWorldAssetPackageFName();
			// Loads it and adds it to the world at once: its actors are registered (collision included) and begin play.
			World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
			Level = Streaming->GetLoadedLevel();
			if (!Level || !Level->bIsVisible)
			{
				Test.AddError(FString::Printf(TEXT("%s did not finish loading into the test world"), MapPackage));
				return false;
			}
			return true;
		}

		/** Every actor of class T in the map (not the test's own), sorted by name. */
		template <typename T>
		TArray<T*> FindActors() const
		{
			TArray<T*> Found;
			for (TActorIterator<T> It(World); It; ++It)
			{
				if (IsValid(*It) && It->GetLevel() == Level)
				{
					Found.Add(*It);
				}
			}
			Found.Sort([](const T& A, const T& B) { return A.GetFName().LexicalLess(B.GetFName()); });
			return Found;
		}

		/** The map actor carrying this tag (the builder writes LureId=<layout id> plus Key=Value tags on every marker). */
		AActor* FindTagged(FName Tag) const
		{
			for (AActor* Actor : FindActors<AActor>())
			{
				if (Actor->ActorHasTag(Tag))
				{
					return Actor;
				}
			}
			return nullptr;
		}

		/** The player start with this layout id (LureId=ps_2), else the first one by name. */
		APlayerStart* FindPlayerStart(const TCHAR* LayoutId = TEXT("ps_2")) const
		{
			const TArray<APlayerStart*> Starts = FindActors<APlayerStart>();
			for (APlayerStart* Start : Starts)
			{
				if (Start->ActorHasTag(FName(FString(TEXT("LureId=")) + LayoutId)))
				{
					return Start;
				}
			}
			return Starts.Num() > 0 ? Starts[0] : nullptr;
		}

		/** A player character standing with its feet at Feet, possessed by a local player controller (what PIE has). */
		ALurePlayerCharacter* SpawnPlayer(FAutomationTestBase& Test, const FVector& Feet, float Yaw = 0.f)
		{
			TArray<FLureMovementRow> Rows;
			TArray<FString> Problems;
			FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
			const float HalfHeight = Rows.Num() > 0 ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.f;
			const FTransform Transform(FRotator(0.f, Yaw, 0.f), Feet + FVector(0.f, 0.f, HalfHeight + 2.15f));
			ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character || !Character->GetLureMovement())
			{
				Test.AddError(TEXT("ALurePlayerCharacter did not spawn with its movement component"));
				return nullptr;
			}
			Character->GetLureMovement()->ApplyMovementTable(Movement.Get());
			Character->FinishSpawning(Transform);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APlayerController* Controller = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity, Params))
			{
				Controller->SetAsLocalPlayerController();
				Controller->Possess(Character);
				Controller->SetControlRotation(Character->GetActorRotation());
			}
			return Character;
		}

		/** Moves a player so its feet stand at Feet (a teleport, like Lure.Teleport). */
		static void TeleportPlayer(ALurePlayerCharacter* Player, const FVector& Feet)
		{
			if (Player && Player->GetCapsuleComponent())
			{
				const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Player->SetActorLocation(Feet + FVector(0.f, 0.f, HalfHeight + 2.15f), false, nullptr, ETeleportType::TeleportPhysics);
			}
		}

		/** Ticks the whole world for Seconds in steps of at most Step (the last one shorter). */
		void Tick(double Seconds, float Step = 1.f / 30.f)
		{
			for (double Left = Seconds; Left > 1.0e-6; Left -= Step)
			{
				Wrapper.TickTestWorld(static_cast<float>(FMath::Min<double>(Left, Step)));
			}
		}
	};

	/** The live UWorlds that are streamed copies of MapPackage (path /Game/Maps/<Map>_LevelInstance_<N>.<Map>). */
	inline TArray<FString> LiveMapCopies(const TCHAR* MapPackage)
	{
		const FString Needle = FPackageName::GetShortName(MapPackage) + TEXT("_LevelInstance_");
		TArray<FString> Found;
		for (TObjectIterator<UWorld> It; It; ++It)
		{
			if (It->GetOutermost()->GetName().Contains(Needle))
			{
				Found.Add(It->GetPathName());
			}
		}
		return Found;
	}

	/** Call last in a map test, after every FMapWorld is released: collects garbage and checks that no copy of MapPackage is
	 *  still alive (a leaked copy crashes the next PIE session in the same run). Returns false with an error if one is. */
	inline bool TestNoMapCopiesLeft(FAutomationTestBase& Test, const TCHAR* MapPackage)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		const TArray<FString> Left = LiveMapCopies(MapPackage);
		if (Left.Num() > 0)
		{
			Test.AddError(FString::Printf(TEXT("%d copies of %s are still alive after the test (first: %s)"), Left.Num(), MapPackage, *Left[0]));
			return false;
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

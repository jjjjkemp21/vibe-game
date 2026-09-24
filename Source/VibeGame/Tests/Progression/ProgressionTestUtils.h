// Lure T-010 tests (unreal-engineer): shared helpers for Project.Progression.* (T-030: the cooler moved to the physical
// ALureCoolerActor, tested in Project.Catch.*; the player state keeps money, XP and level).
// Tables come from the CSV sources in data/tables/ or from CSV fixtures, never the binary /Game/Data assets.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "Fish/FishInstance.h"

class ALurePlayerState;
class APawn;
class UDataTable;
class ULureInteractionComponent;
class ULureProgressionComponent;
class UScriptStruct;
class UWorld;

namespace LureProgressionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Absolute path of data/tables/<File> */
	FString SourcePath(const TCHAR* File);

	/** Reads data/tables/<File>; a missing file is a test error */
	bool LoadSource(FAutomationTestBase& Test, const TCHAR* File, FString& OutCsv);

	/** A transient table from CSV text; OutProblems = the import problems */
	TStrongObjectPtr<UDataTable> MakeTable(UScriptStruct* RowStruct, const FString& Csv, TArray<FString>* OutProblems = nullptr);

	/** A transient table from CSV text; import problems are test errors */
	TStrongObjectPtr<UDataTable> MakeTable(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Csv);

	/** data/tables/<File> as a transient table; a missing file or import problems are test errors */
	TStrongObjectPtr<UDataTable> ShippedTable(FAutomationTestBase& Test, UScriptStruct* RowStruct, const TCHAR* File);

	/** DT_PlayerLevel CSV with levels 1..N and these XpToNext values (the last should be 0) */
	FString LevelCsv(TConstArrayView<int32> XpToNext);

	/** Fixture levels: 1 -> 100 -> 2 -> 150 -> 3 -> 200 -> 4 (cap). Level starts: 0, 100, 250, 450. */
	FString FixtureLevelCsv();

	/** Fixture coolers (T-010 columns only; the T-030 ones are optional): Basic = 3 slots, Big = 5, Tiny = 1 */
	FString FixtureCoolerCsv();

	/** Fixture markets: Default = 1.0, Premium = 1.5, Cheap = 0.5 */
	FString FixtureMarketCsv();

	/** A valid fish record (no roll): species, Value coins, Xp, Level */
	FFishInstance MakeFish(FName SpeciesId, int32 Value, int32 Xp, int32 Level = 1, int32 Seed = 0);

	/** Transient game world that has begun play */
	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test);
		void Tick(int32 Frames = 1);
	};

	/** A player: ALurePlayerState (level table injected before BeginPlay) and optionally a pawn with an interaction component */
	struct FPlayer
	{
		ALurePlayerState* State = nullptr;
		ULureProgressionComponent* Progression = nullptr;
		APawn* Pawn = nullptr;
		ULureInteractionComponent* Interaction = nullptr;

		bool IsValid() const { return State && Progression; }
	};

	FPlayer SpawnPlayer(FAutomationTestBase& Test, FWorld& World, const UDataTable* LevelTable, bool bWithPawn = false,
		const FVector& PawnLocation = FVector::ZeroVector);
}

namespace LPT = LureProgressionTest;

#endif // WITH_DEV_AUTOMATION_TESTS

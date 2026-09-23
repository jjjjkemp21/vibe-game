// Lure T-010 independent QA tests (qa-engineer): shared helpers for Project.Progression.QA.*.
// Written from docs/specs/progression-rules.md and the T-010 acceptance, not from the implementation.
// Tables are built from CSV text (the data/tables sources or QA fixtures), never from the binary /Game/Data assets.
// QA fixture numbers differ from the shipped data and from the implementer's fixtures on purpose.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "Fish/FishInstance.h"

class ALurePlayerState;
class ALureSellPoint;
class APawn;
class UDataTable;
class ULureCoolerComponent;
class ULureInteractionComponent;
class ULureProgressionComponent;
class UScriptStruct;
class UWorld;

namespace QAProg
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** DT_PlayerLevel CSV: levels 1..N with these XpToNext values */
	FString LevelsCsv(TConstArrayView<int32> XpToNext);

	/** QA levels: XpToNext 50, 80, 120, 200, 0. Level starts 0, 50, 130, 250, 450; cap = level 5. */
	FString QALevelsCsv();

	/** QA coolers: Basic (the settings' DefaultCoolerId) = 4 slots, Mega = 10, One = 1 */
	FString QACoolersCsv();

	/** QA markets: Default = 1.0 (the settings' DefaultMarketId), Fancy = 1.25, Stingy = 0.5, Triple = 3.0 */
	FString QAMarketsCsv();

	/** Reads data/tables/<File>; a missing file is a test error */
	bool ReadSource(FAutomationTestBase& Test, const TCHAR* File, FString& OutText);

	/** Transient table from CSV text (kept alive by the returned pointer) */
	TStrongObjectPtr<UDataTable> MakeTable(UScriptStruct* RowStruct, const FString& Csv, TArray<FString>* OutProblems = nullptr);

	/** Same, import problems are test errors */
	TStrongObjectPtr<UDataTable> MakeTableChecked(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Csv, const TCHAR* What);

	/** Minimal CSV parser for raw scans (quoted cells, doubled quotes, commas and newlines inside quotes); rows of cells */
	TArray<TArray<FString>> ParseCsv(const FString& Text);

	/** Optional sign then digits only */
	bool IsWholeNumber(const FString& Cell);

	/** Optional sign, digits, optional one '.', at least one digit */
	bool IsDecimalNumber(const FString& Cell);

	/** A valid hand-made fish record (no roll) */
	FFishInstance Fish(FName SpeciesId, int32 Value, int32 Xp, int32 Seed = 0);

	/** Expects at least one warning containing Substring (any count; plain text, not a regex) */
	void ExpectWarnings(FAutomationTestBase& Test, const TCHAR* Substring);

	struct FPlayer
	{
		ALurePlayerState* State = nullptr;
		ULureCoolerComponent* Cooler = nullptr;
		ULureProgressionComponent* Progression = nullptr;
		APawn* Pawn = nullptr;
		ULureInteractionComponent* Interaction = nullptr;

		bool IsValid() const { return State && Cooler && Progression; }
	};

	/** Everything a progression state holds, to prove that a refused call changed nothing */
	struct FState
	{
		int32 Money = 0;
		int32 TotalXp = 0;
		int32 Level = 0;
		FName CoolerId;
		TArray<FFishInstance> Fish;

		static FState Of(const FPlayer& Player);
		bool Equals(const FState& Other) const;
		FString Describe() const;
	};

	/** A game test world that has begun play, plus the tables the players and sell points get (tables outlive the world) */
	struct FEnv
	{
		TStrongObjectPtr<UDataTable> Levels;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Markets;
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Init(FAutomationTestBase& Test, const FString& LevelCsv, const FString& CoolerCsv, const FString& MarketCsv);
		bool InitQA(FAutomationTestBase& Test) { return Init(Test, QALevelsCsv(), QACoolersCsv(), QAMarketsCsv()); }
		/** The shipped data/tables CSVs */
		bool InitShipped(FAutomationTestBase& Test);

		/** ALurePlayerState with this env's level and cooler tables injected before BeginPlay; optionally a pawn + interaction component */
		FPlayer SpawnPlayer(FAutomationTestBase& Test, bool bWithPawn = false, const FVector& PawnLocation = FVector::ZeroVector);

		/** A pawn with an interaction component and NO player state */
		APawn* SpawnLonePawn(FAutomationTestBase& Test, const FVector& Location, ULureInteractionComponent** OutInteraction = nullptr);

		ALureSellPoint* SpawnSellPoint(FAutomationTestBase& Test, const FVector& Location, FName MarketId = NAME_None, float Radius = 300.0f,
			const UDataTable* MarketTableOverride = nullptr);
	};

	/** Sets the role of every non-null actor (a client sees the player state, pawn and sell point as non-authority) */
	void SetRoles(ENetRole Role, std::initializer_list<AActor*> Actors);
}

namespace QAP = QAProg;

#endif // WITH_DEV_AUTOMATION_TESTS

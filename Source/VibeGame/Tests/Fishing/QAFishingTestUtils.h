// Lure T-006 QA (qa-engineer): shared helpers for the independent Project.Fishing.QA.* tests.
// Black-box: expectations come from docs/specs/fishing-rules.md (incl. "Lead decisions"), docs/specs/movement-rules.md,
// the T-006 line in docs/TASKS.md and the contract comments in Fishing/*.h and Character/FPArmsPose.h, never from the .cpp files.
// Tables are built from the text sources in data/tables/ (or from edited copies of them), never from the binary /Game/Data assets.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/FPArmsPose.h"
#include "Character/LureMovementTypes.h"
#include "Fishing/FishingTypes.h"
#include "Tests/AutomationCommon.h"
#include "Tests/FishQATestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

class AActor;
class ALurePlayerCharacter;
class FJsonObject;
class APlayerController;
class UDataTable;
class ULureFishingComponent;
class UWorld;

namespace QAFishing
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();

	/** The test scene: an 8 x 8 m dock (x, y in [-400, 400]) whose top is at z = 100, and the sea at z = 0 (the settings' fallback). */
	constexpr float DockTop = 100.f;
	constexpr float DockHalf = 400.f;
	/** The player stands here on the dock, facing +X (over the water). */
	const FVector StandFeet(250.f, 0.f, DockTop);
	/** A fishing spot that covers every cast from StandFeet along +X (x in [100, 2500]). */
	const FVector SpotCenter(1300.f, 0.f, 0.f);
	constexpr float SpotRadius = 1200.f;

	// ---- Names (for readable messages) ----
	FString StateName(ELureFishingState State);
	FString ResultName(ELureFishingResult Result);
	FString BlockName(ELureCastBlock Block);
	FString PoseName(EFPArmsPose Pose);
	FGameplayTag Tag(const TCHAR* Name);

	// ---- CSV sources and edited copies ----
	FString SourcePath(const TCHAR* FileName);
	bool LoadSource(FAutomationTestBase& Test, const TCHAR* FileName, FString& OutText);
	/** Header cells (trimmed) and the data lines of a simple CSV (no quoted commas: true for our sources). */
	void SplitCsv(const FString& Csv, TArray<FString>& OutHeader, TArray<TArray<FString>>& OutRows);
	FString JoinCsv(const TArray<FString>& Header, const TArray<TArray<FString>>& Rows);
	/** Returns Csv with the cell (row RowName, column Column) replaced; adds an error if either is missing. */
	FString WithCell(FAutomationTestBase& Test, const FString& Csv, const TCHAR* RowName, const TCHAR* Column, const FString& Value);
	/** Returns Csv plus a copy of row FromRow named NewRow. */
	FString WithRowCopy(FAutomationTestBase& Test, const FString& Csv, const TCHAR* FromRow, const TCHAR* NewRow);

	/** A transient table (kept alive by the caller's TStrongObjectPtr); import problems go to OutProblems. */
	UDataTable* MakeTable(UScriptStruct* RowStruct, const FString& Csv, TArray<FString>* OutProblems = nullptr);
	/** Like MakeTable, but every import problem is a test error. */
	UDataTable* MakeTableChecked(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Csv, const TCHAR* What);

	bool LoadFishingCsv(FAutomationTestBase& Test, FString& OutCsv);
	bool LoadMovementCsv(FAutomationTestBase& Test, FString& OutCsv);
	/** The shipped DT_Fishing "Default" row, read from data/tables/DT_Fishing.csv. */
	bool ShippedFishingRow(FAutomationTestBase& Test, FLureFishingRow& OutRow);
	/** The shipped DT_Movement rows (resolved per state), read from data/tables/DT_Movement.csv. */
	bool ShippedMovementRows(FAutomationTestBase& Test, TArray<FLureMovementRow>& OutRows);
	const FLureMovementRow& RowOf(const TArray<FLureMovementRow>& Rows, ELureMovementState State);

	/**
	 *  A deterministic profile for flow tests, built from the SHIPPED row: the bite comes BiteWait s after landing, no nibbles,
	 *  a rebite after 1 s, the fish stays hooked (AutoLandDelay 0), NoBiteHintDelay 1 s.
	 */
	FLureFishingRow FlowProfile(const FLureFishingRow& Shipped, float BiteWait = 0.5f, float HookWindow = 0.8f);

	/** Pure field-by-field comparison (reflection); returns the names of the fields that differ. */
	TArray<FString> DifferentFields(const UScriptStruct* Struct, const void* A, const void* B);

	/** Builds the actor tags Content/Python/levels/build_level.py marker_tags() writes for a fishing_spot marker (L_PalmKey.md s11). */
	TArray<FName> BuilderSpotTags(const TSharedPtr<FJsonObject>& Marker);

	/** Captures log lines of one category while in scope (to count warnings exactly). */
	struct FLogCapture : public FOutputDevice
	{
		FName Category;
		TArray<TPair<ELogVerbosity::Type, FString>> Lines;
		explicit FLogCapture(FName InCategory);
		virtual ~FLogCapture() override;
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& InCategory) override;
		int32 Count(ELogVerbosity::Type Verbosity, const TCHAR* Contains = nullptr) const;
	};

	/** A transient game world (the project's game mode, standalone unless SetListenServer) with the dock and a stepped clock. */
	struct FScene
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		TStrongObjectPtr<UDataTable> Movement;
		FishQA::FTables Fish;
		bool bFishLoaded = false;

		/** bDock: the dock box. The sea is the settings' fallback water z = 0 (what the greybox levels use). */
		bool Create(FAutomationTestBase& Test, bool bDock = true, const FString* MovementCsv = nullptr);
		~FScene();

		AActor* AddBox(const FVector& Center, const FVector& Extent);
		/** A non-colliding water surface tagged with the settings' WaterTag, top at SurfaceZ, HalfSize wide. */
		AActor* AddTaggedWater(float SurfaceZ, float HalfSize = 20000.f);
		/** A marker like the level builder places: a plain actor tagged Lure.FishingSpot + Key=Value tags. */
		AActor* AddSpot(const FVector& Location, const TArray<FString>& KeyValues);
		/** The standard spot covering every cast from StandFeet: Habitat.Shore, Region.Tropical.PalmKey. */
		AActor* AddShoreSpot(const TArray<FString>& Extra = {});

		/** Spawns a character standing at Feet facing Yaw, with this scene's movement table. */
		ALurePlayerCharacter* Spawn(FAutomationTestBase& Test, const FVector& Feet = StandFeet, float Yaw = 0.f);
		/** Possesses Character with a new LOCAL player controller (the owning client in standalone). */
		APlayerController* PossessLocally(ALurePlayerCharacter* Character);

		/** Fishing component with Profile, the real fish tables (JSON sources), a fixed seed and a fixed time of day. */
		ULureFishingComponent* SetUpFishing(FAutomationTestBase& Test, ALurePlayerCharacter* Character, const FLureFishingRow& Profile, float Hours = 12.f, int32 Seed = 1234);

		void Tick(int32 Frames, float DeltaTime = Dt);
		/** Movement input along Direction every frame. */
		void TickMoving(ALurePlayerCharacter* Character, const FVector& Direction, int32 Frames);
		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, float DeltaTime = Dt);
		double Now() const;
		/** Ticks (steps <= Dt, the last one shorter) until the world clock reaches Time. */
		void AdvanceTo(double Time);

		/** Makes the world report NM_ListenServer (no net driver: the URL derivation the engine uses before one exists). */
		void SetListenServer(bool bListen);
		bool bListenSet = false;
	};

	/** The six moments of one line (Charging is the owning client's; the rest are the server's replicated state). */
	enum class EStage : uint8 { Charging, Casting, Waiting, Nibble, Biting, Hooked };
	FString StageName(EStage Stage);
	TArray<EStage> AllStages();

	/**
	 *  A profile for the stage matrix: 1 s flight, bite 3 s after landing with exactly one nibble ~1 s before it (0.6 s long),
	 *  a 3 s hook window, the fish stays hooked, MaxLineLength 900, casts 300..800 cm.
	 */
	FLureFishingRow StageProfile(const FLureFishingRow& Shipped);

	/** Brings a possessed character's line to Stage (standalone). False (with an error) if it can't. */
	bool DriveToStage(FAutomationTestBase& Test, FScene& Scene, ALurePlayerCharacter* Character, ULureFishingComponent* Fishing, EStage Stage);
}

namespace QAF = QAFishing;

#endif // WITH_DEV_AUTOMATION_TESTS

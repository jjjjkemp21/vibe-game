// Lure T-004 QA (qa-engineer): shared helpers for the independent Project.Movement.QA.* tests.
// Design: Saved/AgentLogs/qa/eng1-T004-test-design.md (main checkout). Decisions: docs/specs/movement-rules.md.
// Tables are built from CSV text (fixtures) or data/tables/DT_Movement.csv, never the binary asset.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureMovementTypes.h"
#include "Tests/AutomationCommon.h"

class AActor;
class ALurePlayerCharacter;
class UDataTable;
class UWorld;

namespace QAMovement
{
	constexpr float Dt = 1.f / 60.f;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The walking capsule hovers 1.9-2.4 cm above the floor (UCharacterMovementComponent MIN/MAX_FLOOR_DIST). */
	constexpr float MaxFloorDist = 2.4f;
	/** Ceiling boundaries: Clear(S) - BlockMargin must block, Clear(S) + PassMargin must pass. */
	constexpr float BlockMargin = 1.f;
	constexpr float PassMargin = 3.f;
	/** Default camera near clip plane, cm. */
	constexpr float NearClip = 10.f;
	/** How far the feet may move on a stance change on flat ground (floor-distance correction), cm. */
	constexpr float FeetTolerance = 2.5f;
	/** The standard crawl gap (GAME_DESIGN / T-004), cm. */
	constexpr float CrawlGap = 60.f;
	/** Frames to let a spawn or a stance change settle. */
	constexpr int32 SettleFrames = 10;

	/** One fixture row (the arms-bob columns get neutral, valid values). */
	struct FRowSpec
	{
		FString Name;
		float MaxSpeed = 0.f;
		float MaxAcceleration = 2048.f;
		float HalfHeight = 0.f;
		float Radius = 0.f;
		float EyeHeight = 0.f;
		float TransitionTime = 0.f;
		float Noise = 1.f;
		float JumpZ = 420.f;
		bool bCanJump = true;
	};

	/** Fixture A: every value unlike the shipped CSV and the class defaults (catches hardcoded numbers and CDO leftovers). */
	TArray<FRowSpec> FixtureARows();
	/** Fixture B: other numbers, Crouch.CanJump = true, Prone.TransitionTime = 0 (data-driven proof). */
	TArray<FRowSpec> FixtureBRows();
	FRowSpec* FindSpec(TArray<FRowSpec>& Rows, const TCHAR* Name);
	FString MakeCsv(const TArray<FRowSpec>& Rows);

	FString ShippedCsvPath();
	bool LoadShippedCsv(FString& OutCsv);
	UDataTable* MakeTable(const FString& Csv, TArray<FString>* OutProblems = nullptr);
	/** Table from fixture rows; import problems are test errors. */
	UDataTable* MakeTable(FAutomationTestBase& Test, const TArray<FRowSpec>& Rows);
	/** data/tables/DT_Movement.csv as a transient table; missing file or import problems are test errors. */
	UDataTable* ShippedTable(FAutomationTestBase& Test);
	UDataTable* FixtureA(FAutomationTestBase& Test);
	UDataTable* FixtureB(FAutomationTestBase& Test);

	/** What the runtime resolves from Table (pure, no logging). */
	TArray<FLureMovementRow> Resolve(const UDataTable* Table, uint8* OutFallbackMask = nullptr, TArray<FString>* OutProblems = nullptr);
	const FLureMovementRow& RowOf(const TArray<FLureMovementRow>& Rows, ELureMovementState State);
	ELureMovementState StateOf(ELureStance Stance);
	FString StanceName(ELureStance Stance);
	FString StateName(ELureMovementState State);
	/** Lowest ceiling (floor top to underside) a capsule of this row fits under: 2 x HalfHeight. */
	inline float Clear(const FLureMovementRow& Row) { return 2.f * Row.CapsuleHalfHeight; }
	bool RowsEqual(const FLureMovementRow& A, const FLureMovementRow& B);

	/** The design rules of DT_Movement (QA design V04-V15). Each returns the broken parts (empty = pass). */
	enum class ERule : uint8
	{
		SpeedsPositiveAndSane,
		SpeedOrdering,
		CapsuleHeightOrdering,
		EyeHeightOrdering,
		RadiusValid,
		EyeInsideCapsule,
		ProneFits60cmGap,
		CrouchDoesNotFit60cmGap,
		StandHeightHumanScale,
		TransitionTimeRange,
		NoiseMultiplierOrdering,
		JumpFlags,
		Count
	};
	TArray<FString> CheckRule(ERule Rule, const TArray<FLureMovementRow>& Rows);
	TArray<FString> CheckAllRules(const TArray<FLureMovementRow>& Rows);

	/** Transient game world with static BlockAll boxes (the floor's top is z = 0). */
	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;

		bool Create(FAutomationTestBase& Test, bool bFloor = true);
		AActor* AddBox(const FVector& Center, const FVector& Extent);
		/** Ceiling slab: underside GapHeight above the floor, X in [MinX, MaxX], 10 m wide in Y. */
		AActor* AddSlab(float GapHeight, float MinX, float MaxX);
		/** Wall whose -X face is at FaceX (it extends toward +X), 10 m wide, 6 m tall. */
		AActor* AddWallFacingMinusX(float FaceX);
		/** Wall whose -Y face is at FaceY (it extends toward +Y). */
		AActor* AddWallFacingMinusY(float FaceY);
		/** Floor top z = 0 for x < EdgeX, lower floor top z = -Drop beyond it. Use with Create(Test, false). */
		void AddLedgeFloors(float EdgeX, float Drop);

		/**
		 *  Spawns the character standing with its feet at Feet. bApplyTable: Table is applied before BeginPlay
		 *  (null = built-in fallback rows); false = the settings path (what the game does). PreFinish runs before BeginPlay.
		 */
		ALurePlayerCharacter* Spawn(FAutomationTestBase& Test, const FVector& Feet, const UDataTable* Table, bool bApplyTable = true,
			TFunction<void(ALurePlayerCharacter&)> PreFinish = nullptr);

		void Tick(int32 Frames, float DeltaTime = Dt);
		/** Full movement input along Direction every frame. */
		void TickMoving(ALurePlayerCharacter* Character, int32 Frames, const FVector& Direction = FVector::ForwardVector, float DeltaTime = Dt);
		/** Ticks until Predicate is true or MaxFrames pass; returns true if it became true. */
		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, ALurePlayerCharacter* MoveCharacter = nullptr, float DeltaTime = Dt);
	};

	float FeetZ(const ALurePlayerCharacter* Character);
	float CameraZ(const ALurePlayerCharacter* Character);
	/** Camera height above the capsule bottom (EyeHeight is measured from the feet), cm. */
	float EyeAboveFeet(const ALurePlayerCharacter* Character);
	float HorizontalSpeed(const ALurePlayerCharacter* Character);
	float MaxSpeedNow(const ALurePlayerCharacter* Character);
	bool IsFalling(const ALurePlayerCharacter* Character);
	bool IsOnGround(const ALurePlayerCharacter* Character);
	bool IsPenetrating(const ALurePlayerCharacter* Character);
	void GetCapsule(const ALurePlayerCharacter* Character, float& OutRadius, float& OutHalfHeight);
	bool CapsuleMatches(const ALurePlayerCharacter* Character, const FLureMovementRow& Row, float Tolerance = 0.05f);
	void TestCapsule(FAutomationTestBase& Test, const ALurePlayerCharacter* Character, const FLureMovementRow& Row, const FString& Label);

	/** RequestStance + settle. Returns true if the stance was reached. */
	bool EnterStance(FWorld& World, ALurePlayerCharacter* Character, ELureStance Stance, int32 Frames = SettleFrames);
}

namespace QAM = QAMovement;

#endif // WITH_DEV_AUTOMATION_TESTS

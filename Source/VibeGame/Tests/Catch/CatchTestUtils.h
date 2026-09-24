// Lure T-030 tests (unreal-engineer): shared helpers for Project.Catch.*. Rules: docs/specs/catch-handling-rules.md.
// Tables come from the text sources in data/tables/ (or fixture text), never the binary /Game/Data assets, and are injected
// into the world's ULureCatchSubsystem. Every fixture UObject is held by TStrongObjectPtr (UWorld::Tick may collect garbage).
// Names are qualified (LureCatchTest::, alias LCT::): no file-scope using-directives (unity builds merge .cpp files).

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "Catch/LureCatchTypes.h"
#include "Fish/FishInstance.h"
#include "Fishing/FishingTypes.h"
#include "Interaction/LureInteractable.h"
#include "Tests/FishQATestHelpers.h"

class ALureCoolerActor;
class ALureFishItem;
class ALurePlayerCharacter;
class ALurePlayerState;
class ALureSellCounter;
class ALureWaterVolume;
class APlayerController;
class APlayerState;
class UDataTable;
class ULureHandsComponent;
class ULureInteractionComponent;
class ULureProgressionComponent;
class UScriptStruct;
class UWorld;

namespace LureCatchTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = 1.0f / 60.0f;

	/** The test dock: a solid box, top at DockTop, |x| and |y| up to DockHalf. Everything else is the fallback sea (z = 0). */
	constexpr float DockTop = 100.0f;
	constexpr float DockHalf = 600.0f;

	/** Absolute path of data/tables/<File> */
	FString SourcePath(const TCHAR* File);

	bool ReadSource(FAutomationTestBase& Test, const TCHAR* File, FString& OutText);

	/** A transient table from CSV or JSON text; OutProblems = the engine's import problems */
	TStrongObjectPtr<UDataTable> MakeTable(UScriptStruct* RowStruct, const FString& Text, bool bJson, TArray<FString>* OutProblems = nullptr);

	/** Same; import problems are test errors */
	TStrongObjectPtr<UDataTable> MakeTableChecked(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Text, bool bJson, const TCHAR* What);

	/** data/tables/<File> (CSV or JSON by extension); a missing file or import problems are test errors */
	TStrongObjectPtr<UDataTable> Shipped(FAutomationTestBase& Test, UScriptStruct* RowStruct, const TCHAR* File);

	/** A valid hand-made fish record (no roll) */
	FFishInstance MakeFish(FName SpeciesId, int32 Value, int32 Xp, float WeightKg = 1.5f, int32 Seed = 0);

	/** A short freshness profile for world tests: grace 1 s, spoil 2 s, exponent 1, min 0.25 (row Default) */
	FString QuickFreshnessCsv();

	/** Markets: Default 1.0, Premium 1.5 */
	FString FixtureMarketCsv();

	/** A game test world with the dock, the shipped catch data injected, and helpers to spawn the T-030 actors. */
	struct FWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		TStrongObjectPtr<UDataTable> Movement;
		TStrongObjectPtr<UDataTable> Levels;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Markets;
		TStrongObjectPtr<UDataTable> Freshness;
		TStrongObjectPtr<UDataTable> Catch;
		TStrongObjectPtr<UDataTable> Display;
		/** The shipped fish tables (data/tables JSON): names, reference weights */
		FishQA::FTables FishTables;
		TArray<TStrongObjectPtr<UObject>> Keep;

		/** bQuickFreshness: DT_Freshness = QuickFreshnessCsv (else the shipped one) */
		bool Create(FAutomationTestBase& Test, bool bQuickFreshness = false);

		/** A player character standing at Feet with an ALurePlayerState (levels injected) and a player controller (local if bLocal) */
		ALurePlayerCharacter* SpawnPlayer(FAutomationTestBase& Test, const FVector& Feet, bool bLocal = true);

		/** A cooler standing at Floor (pivot = bottom center) of CoolerId (None = the default row), made for Owner */
		ALureCoolerActor* SpawnCooler(const FVector& Floor, float Yaw = 0.0f, FName CoolerId = NAME_None, APlayerState* Owner = nullptr);

		/** A sell counter whose top center is Top (+X toward the customers at Yaw) */
		ALureSellCounter* SpawnCounter(const FVector& Top, float Yaw = 0.0f, FName MarketId = TEXT("Default"));

		/** ULureCatchLibrary::HandleFishLanded for Player; returns the fish now on the hook (null if none) */
		ALureFishItem* Land(ALurePlayerCharacter* Player, const FFishInstance& Fish);

		/** Land, then the server puts it in Player's hand (setup shortcut; the keys are tested on their own) */
		ALureFishItem* LandInHand(ALurePlayerCharacter* Player, const FFishInstance& Fish);

		/** A plain actor with a root at Location / Yaw (a level marker when Tag is set, a stand-in visual or line otherwise) */
		AActor* SpawnMarker(const FVector& Location, float Yaw = 0.0f, FName Tag = NAME_None);

		/** A solid box (level geometry: blocks players, casts and drops) */
		AActor* AddBox(const FVector& Center, const FVector& Extent);

		/** Engine water (T-026 ALureWaterVolume): surface at z = 0 over +-3500 cm, Depth deep (under the dock, which stands above it) */
		ALureWaterVolume* AddWater(float Depth = 400.0f);

		void Tick(int32 Frames, float DeltaTime = Dt);
		bool TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, float DeltaTime = Dt);

		/** Advances world time by Seconds in 0.1 s ticks (one big tick is clamped) */
		void Advance(float Seconds);

		/** Server world time now */
		double Now() const;
	};

	/** Turns Player's view (its controller's control rotation) toward Target */
	void LookAt(ALurePlayerCharacter* Player, const FVector& Target);

	/** The player's controller (null if none) */
	APlayerController* ControllerOf(const ALurePlayerCharacter* Player);

	/** The feet of a character (capsule bottom) */
	FVector FeetOf(const ALurePlayerCharacter* Player);

	/** Moves the player so its feet are at Feet, facing Yaw (view and body) */
	void PlaceAt(ALurePlayerCharacter* Player, const FVector& Feet, float Yaw);

	ULureHandsComponent* HandsOf(const ALurePlayerCharacter* Player);
	ULureInteractionComponent* InteractionOf(const ALurePlayerCharacter* Player);
	ULureProgressionComponent* ProgressionOf(const ALurePlayerCharacter* Player);

	/** The player's current notices (placeholder HUD) joined with " | " */
	FString NoticesOf(const ALurePlayerCharacter* Player);

	/** The rod tip as the fishing rules estimate it (eye + RodTipOffsetFromEye), not the drawn rod's socket: removes the rod mesh */
	void UseEstimatedRodTip(ALurePlayerCharacter* Player);

	FString VerbName(ELureInteractVerb Verb);
	FString BlockName(ELureCastBlock Block);
}

namespace LCT = LureCatchTest;

#endif // WITH_DEV_AUTOMATION_TESTS

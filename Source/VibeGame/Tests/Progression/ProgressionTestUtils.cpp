// Lure T-010 tests (unreal-engineer): shared helpers for Project.Progression.*.

#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/DefaultPawn.h"
#include "Interaction/LureInteractionComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Progression/LureProgressionComponent.h"
#include "UObject/Package.h"

namespace LureProgressionTest
{
	FString SourcePath(const TCHAR* File)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / File);
	}

	bool LoadSource(FAutomationTestBase& Test, const TCHAR* File, FString& OutCsv)
	{
		if (!FFileHelper::LoadFileToString(OutCsv, *SourcePath(File)))
		{
			Test.AddError(FString::Printf(TEXT("Can't read %s"), *SourcePath(File)));
			return false;
		}
		return true;
	}

	TStrongObjectPtr<UDataTable> MakeTable(UScriptStruct* RowStruct, const FString& Csv, TArray<FString>* OutProblems)
	{
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = RowStruct;
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		if (OutProblems)
		{
			*OutProblems = Problems;
		}
		return Table;
	}

	TStrongObjectPtr<UDataTable> MakeTable(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Csv)
	{
		TArray<FString> Problems;
		TStrongObjectPtr<UDataTable> Table = MakeTable(RowStruct, Csv, &Problems);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s fixture import problem: %s"), *RowStruct->GetName(), *Problem));
		}
		return Table;
	}

	TStrongObjectPtr<UDataTable> ShippedTable(FAutomationTestBase& Test, UScriptStruct* RowStruct, const TCHAR* File)
	{
		FString Csv;
		if (!LoadSource(Test, File, Csv))
		{
			return TStrongObjectPtr<UDataTable>();
		}
		TArray<FString> Problems;
		TStrongObjectPtr<UDataTable> Table = MakeTable(RowStruct, Csv, &Problems);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), File, *Problem));
		}
		return Table;
	}

	FString LevelCsv(TConstArrayView<int32> XpToNext)
	{
		FString Csv = TEXT("Name,Level,XpToNext,DevComment\n");
		for (int32 Index = 0; Index < XpToNext.Num(); ++Index)
		{
			Csv += FString::Printf(TEXT("L%02d,%d,%d,\n"), Index + 1, Index + 1, XpToNext[Index]);
		}
		return Csv;
	}

	FString FixtureLevelCsv()
	{
		return LevelCsv({ 100, 150, 200, 0 });
	}

	FString FixtureCoolerCsv()
	{
		return TEXT("Name,DisplayName,Slots,DevComment\n")
			TEXT("Basic,\"Fixture basic\",3,\n")
			TEXT("Big,\"Fixture big\",5,\n")
			TEXT("Tiny,\"Fixture tiny\",1,\n");
	}

	FString FixtureMarketCsv()
	{
		return TEXT("Name,DisplayName,SellMultiplier,DevComment\n")
			TEXT("Default,\"Fixture default\",1.0,\n")
			TEXT("Premium,\"Fixture premium\",1.5,\n")
			TEXT("Cheap,\"Fixture cheap\",0.5,\n");
	}

	FFishInstance MakeFish(FName SpeciesId, int32 Value, int32 Xp, int32 Level, int32 Seed)
	{
		FFishInstance Fish;
		Fish.SpeciesId = SpeciesId;
		Fish.RarityId = TEXT("Common");
		Fish.WeightKg = 1.0f;
		Fish.Level = Level;
		Fish.Value = Value;
		Fish.Xp = Xp;
		Fish.DifficultyRating = 1.0f;
		Fish.Seed = Seed;
		return Fish;
	}

	bool FWorld::Create(FAutomationTestBase& Test)
	{
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(&Test);
			Test.AddError(TEXT("The test world could not be created"));
			return false;
		}
		World = Wrapper.GetTestWorld();
		if (!World)
		{
			Test.AddError(TEXT("No test world"));
			return false;
		}
		return true;
	}

	void FWorld::Tick(int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Wrapper.TickTestWorld(1.0f / 60.0f);
		}
	}

	FPlayer SpawnPlayer(FAutomationTestBase& Test, FWorld& World, const UDataTable* LevelTable, bool bWithPawn, const FVector& PawnLocation)
	{
		FPlayer Player;
		if (!World.World)
		{
			return Player;
		}
		ALurePlayerState* State = World.World->SpawnActorDeferred<ALurePlayerState>(ALurePlayerState::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test.TestNotNull(TEXT("ALurePlayerState spawned"), State))
		{
			return Player;
		}
		Player.State = State;
		Player.Progression = State->GetProgression();
		if (Player.Progression)
		{
			Player.Progression->SetLevelTable(LevelTable);
		}
		State->FinishSpawning(FTransform::Identity);
		Test.TestTrue(TEXT("the player state has a progression component"), Player.IsValid());

		if (bWithPawn)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APawn* Pawn = World.World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), FTransform(PawnLocation), Params);
			if (Test.TestNotNull(TEXT("pawn spawned"), Pawn))
			{
				Pawn->SetActorLocation(PawnLocation, false, nullptr, ETeleportType::TeleportPhysics);
				Pawn->SetPlayerState(State);
				ULureInteractionComponent* Interaction = NewObject<ULureInteractionComponent>(Pawn, TEXT("Interaction"));
				Interaction->RegisterComponent();
				Player.Pawn = Pawn;
				Player.Interaction = Interaction;
			}
		}
		return Player;
	}

}

#endif // WITH_DEV_AUTOMATION_TESTS

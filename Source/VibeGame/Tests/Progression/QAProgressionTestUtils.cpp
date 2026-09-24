// Lure T-010 independent QA tests (qa-engineer): shared helpers for Project.Progression.QA.*.

#include "Tests/Progression/QAProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/DefaultPawn.h"
#include "Interaction/LureInteractionComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionTypes.h"
#include "UObject/Package.h"

namespace QAProg
{
	FString LevelsCsv(TConstArrayView<int32> XpToNext)
	{
		FString Csv = TEXT("Name,Level,XpToNext,DevComment\n");
		for (int32 Index = 0; Index < XpToNext.Num(); ++Index)
		{
			Csv += FString::Printf(TEXT("QA_L%d,%d,%d,\"qa fixture\"\n"), Index + 1, Index + 1, XpToNext[Index]);
		}
		return Csv;
	}

	FString QALevelsCsv()
	{
		return LevelsCsv({ 50, 80, 120, 200, 0 });
	}

	FString QACoolersCsv()
	{
		return TEXT("Name,DisplayName,Slots,DevComment\n")
			TEXT("Basic,\"QA basic\",4,\n")
			TEXT("Mega,\"QA mega\",10,\n")
			TEXT("One,\"QA one\",1,\n");
	}

	FString QAMarketsCsv()
	{
		return TEXT("Name,DisplayName,SellMultiplier,DevComment\n")
			TEXT("Default,\"QA default\",1.0,\n")
			TEXT("Fancy,\"QA fancy\",1.25,\n")
			TEXT("Stingy,\"QA stingy\",0.5,\n")
			TEXT("Triple,\"QA triple\",3.0,\n");
	}

	bool ReadSource(FAutomationTestBase& Test, const TCHAR* File, FString& OutText)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / File);
		if (!FFileHelper::LoadFileToString(OutText, *Path))
		{
			Test.AddError(FString::Printf(TEXT("Cannot read %s"), *Path));
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

	TStrongObjectPtr<UDataTable> MakeTableChecked(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Csv, const TCHAR* What)
	{
		TArray<FString> Problems;
		TStrongObjectPtr<UDataTable> Table = MakeTable(RowStruct, Csv, &Problems);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), What, *Problem));
		}
		return Table;
	}

	TArray<TArray<FString>> ParseCsv(const FString& Text)
	{
		TArray<TArray<FString>> Rows;
		TArray<FString> Row;
		FString Cell;
		bool bQuoted = false;
		bool bRowHasContent = false;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR C = Text[Index];
			if (bQuoted)
			{
				if (C == TEXT('"'))
				{
					if (Index + 1 < Text.Len() && Text[Index + 1] == TEXT('"'))
					{
						Cell.AppendChar(TEXT('"'));
						++Index;
					}
					else
					{
						bQuoted = false;
					}
				}
				else
				{
					Cell.AppendChar(C);
				}
				continue;
			}
			if (C == TEXT('"'))
			{
				bQuoted = true;
				bRowHasContent = true;
			}
			else if (C == TEXT(','))
			{
				Row.Add(Cell);
				Cell.Reset();
				bRowHasContent = true;
			}
			else if (C == TEXT('\r'))
			{
			}
			else if (C == TEXT('\n'))
			{
				if (bRowHasContent || !Cell.IsEmpty())
				{
					Row.Add(Cell);
					Rows.Add(Row);
				}
				Row.Reset();
				Cell.Reset();
				bRowHasContent = false;
			}
			else if (C == 0xFEFF && Rows.Num() == 0 && Row.Num() == 0 && Cell.IsEmpty())
			{
				// UTF-8 BOM
			}
			else
			{
				Cell.AppendChar(C);
				bRowHasContent = true;
			}
		}
		if (bRowHasContent || !Cell.IsEmpty())
		{
			Row.Add(Cell);
			Rows.Add(Row);
		}
		return Rows;
	}

	bool IsWholeNumber(const FString& Cell)
	{
		const FString S = Cell.TrimStartAndEnd();
		int32 Start = (S.StartsWith(TEXT("-")) || S.StartsWith(TEXT("+"))) ? 1 : 0;
		if (S.Len() <= Start)
		{
			return false;
		}
		for (int32 Index = Start; Index < S.Len(); ++Index)
		{
			if (!FChar::IsDigit(S[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool IsDecimalNumber(const FString& Cell)
	{
		const FString S = Cell.TrimStartAndEnd();
		int32 Start = (S.StartsWith(TEXT("-")) || S.StartsWith(TEXT("+"))) ? 1 : 0;
		int32 Digits = 0;
		int32 Dots = 0;
		for (int32 Index = Start; Index < S.Len(); ++Index)
		{
			if (FChar::IsDigit(S[Index]))
			{
				++Digits;
			}
			else if (S[Index] == TEXT('.'))
			{
				++Dots;
			}
			else
			{
				return false;
			}
		}
		return Digits > 0 && Dots <= 1;
	}

	FFishInstance Fish(FName SpeciesId, int32 Value, int32 Xp, int32 Seed)
	{
		FFishInstance Out;
		Out.SpeciesId = SpeciesId;
		Out.RarityId = TEXT("Common");
		Out.WeightKg = 2.5f;
		Out.Level = 1;
		Out.Value = Value;
		Out.Xp = Xp;
		Out.DifficultyRating = 1.0f;
		Out.Seed = Seed;
		return Out;
	}

	void ExpectWarnings(FAutomationTestBase& Test, const TCHAR* Substring)
	{
		Test.AddExpectedMessage(Substring, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, /*IsRegex*/ false);
	}

	FState FState::Of(const FPlayer& Player)
	{
		FState State;
		State.Money = Player.Progression->GetMoney();
		State.TotalXp = Player.Progression->GetTotalXp();
		State.Level = Player.Progression->GetLevel();
		return State;
	}

	bool FState::Equals(const FState& Other) const
	{
		return Money == Other.Money && TotalXp == Other.TotalXp && Level == Other.Level;
	}

	FString FState::Describe() const
	{
		return FString::Printf(TEXT("money %d, XP %d, level %d"), Money, TotalXp, Level);
	}

	bool FEnv::Init(FAutomationTestBase& Test, const FString& LevelCsv, const FString& CoolerCsv, const FString& MarketCsv)
	{
		Levels = MakeTableChecked(Test, FPlayerLevelRow::StaticStruct(), LevelCsv, TEXT("levels"));
		Coolers = MakeTableChecked(Test, FCoolerRow::StaticStruct(), CoolerCsv, TEXT("coolers"));
		Markets = MakeTableChecked(Test, FFishMarketRow::StaticStruct(), MarketCsv, TEXT("markets"));
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(&Test);
			Test.AddError(TEXT("The QA test world could not be created"));
			return false;
		}
		World = Wrapper.GetTestWorld();
		return Test.TestNotNull(TEXT("QA test world"), World);
	}

	bool FEnv::InitShipped(FAutomationTestBase& Test)
	{
		FString LevelCsv;
		FString CoolerCsv;
		FString MarketCsv;
		if (!ReadSource(Test, TEXT("DT_PlayerLevel.csv"), LevelCsv) || !ReadSource(Test, TEXT("DT_Cooler.csv"), CoolerCsv)
			|| !ReadSource(Test, TEXT("DT_FishMarket.csv"), MarketCsv))
		{
			return false;
		}
		return Init(Test, LevelCsv, CoolerCsv, MarketCsv);
	}

	FPlayer FEnv::SpawnPlayer(FAutomationTestBase& Test, bool bWithPawn, const FVector& PawnLocation)
	{
		FPlayer Player;
		if (!World)
		{
			return Player;
		}
		ALurePlayerState* State = World->SpawnActorDeferred<ALurePlayerState>(ALurePlayerState::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test.TestNotNull(TEXT("QA player state"), State))
		{
			return Player;
		}
		Player.State = State;
		Player.Progression = State->GetProgression();
		if (Player.Progression)
		{
			Player.Progression->SetLevelTable(Levels.Get());
		}
		State->FinishSpawning(FTransform::Identity);
		Test.TestTrue(TEXT("QA player state has progression"), Player.IsValid());
		if (bWithPawn)
		{
			Player.Pawn = SpawnLonePawn(Test, PawnLocation, &Player.Interaction);
			if (Player.Pawn)
			{
				Player.Pawn->SetPlayerState(State);
			}
		}
		return Player;
	}

	APawn* FEnv::SpawnLonePawn(FAutomationTestBase& Test, const FVector& Location, ULureInteractionComponent** OutInteraction)
	{
		if (!World)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APawn* Pawn = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), FTransform(Location), Params);
		if (!Test.TestNotNull(TEXT("QA pawn"), Pawn))
		{
			return nullptr;
		}
		Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		ULureInteractionComponent* Interaction = NewObject<ULureInteractionComponent>(Pawn, TEXT("QAInteraction"));
		Interaction->RegisterComponent();
		if (OutInteraction)
		{
			*OutInteraction = Interaction;
		}
		return Pawn;
	}

	void SetRoles(ENetRole Role, std::initializer_list<AActor*> Actors)
	{
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				Actor->SetRole(Role);
			}
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

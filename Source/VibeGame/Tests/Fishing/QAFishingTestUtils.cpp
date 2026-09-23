// Lure T-006 QA (qa-engineer): shared helpers for Project.Fishing.QA.* (see QAFishingTestUtils.h).

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "UObject/UnrealType.h"

namespace QAFishing
{
	// ---- Names ----

	template <typename EnumType>
	static FString EnumName(EnumType Value)
	{
		return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	FString StateName(ELureFishingState State) { return EnumName(State); }
	FString ResultName(ELureFishingResult Result) { return EnumName(Result); }
	FString BlockName(ELureCastBlock Block) { return EnumName(Block); }
	FString PoseName(EFPArmsPose Pose) { return EnumName(Pose); }

	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
	}

	// ---- CSV ----

	FString SourcePath(const TCHAR* FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
	}

	bool LoadSource(FAutomationTestBase& Test, const TCHAR* FileName, FString& OutText)
	{
		const bool bLoaded = FFileHelper::LoadFileToString(OutText, *SourcePath(FileName));
		if (!bLoaded)
		{
			Test.AddError(FString::Printf(TEXT("QA: can't read data/tables/%s"), FileName));
		}
		return bLoaded;
	}

	void SplitCsv(const FString& Csv, TArray<FString>& OutHeader, TArray<TArray<FString>>& OutRows)
	{
		OutHeader.Reset();
		OutRows.Reset();
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			TArray<FString> Cells;
			Lines[Index].ParseIntoArray(Cells, TEXT(","), /*CullEmpty*/ false);
			for (FString& Cell : Cells)
			{
				Cell.TrimStartAndEndInline();
			}
			if (Index == 0)
			{
				OutHeader = MoveTemp(Cells);
			}
			else
			{
				OutRows.Add(MoveTemp(Cells));
			}
		}
	}

	FString JoinCsv(const TArray<FString>& Header, const TArray<TArray<FString>>& Rows)
	{
		FString Out = FString::Join(Header, TEXT(",")) + TEXT("\n");
		for (const TArray<FString>& Row : Rows)
		{
			Out += FString::Join(Row, TEXT(",")) + TEXT("\n");
		}
		return Out;
	}

	FString WithCell(FAutomationTestBase& Test, const FString& Csv, const TCHAR* RowName, const TCHAR* Column, const FString& Value)
	{
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		SplitCsv(Csv, Header, Rows);
		const int32 ColumnIndex = Header.IndexOfByKey(FString(Column));
		bool bDone = false;
		for (TArray<FString>& Row : Rows)
		{
			if (ColumnIndex > 0 && Row.Num() > ColumnIndex && Row[0] == RowName)
			{
				Row[ColumnIndex] = Value;
				bDone = true;
			}
		}
		if (!bDone)
		{
			Test.AddError(FString::Printf(TEXT("QA fixture: no cell %s.%s to edit"), RowName, Column));
		}
		return JoinCsv(Header, Rows);
	}

	FString WithRowCopy(FAutomationTestBase& Test, const FString& Csv, const TCHAR* FromRow, const TCHAR* NewRow)
	{
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		SplitCsv(Csv, Header, Rows);
		const TArray<FString>* Source = Rows.FindByPredicate([FromRow](const TArray<FString>& Row) { return Row.Num() > 0 && Row[0] == FromRow; });
		if (!Source)
		{
			Test.AddError(FString::Printf(TEXT("QA fixture: no row %s to copy"), FromRow));
			return Csv;
		}
		TArray<FString> Copy = *Source;
		Copy[0] = NewRow;
		Rows.Add(MoveTemp(Copy));
		return JoinCsv(Header, Rows);
	}

	UDataTable* MakeTable(UScriptStruct* RowStruct, const FString& Csv, TArray<FString>* OutProblems)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = RowStruct;
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		if (OutProblems)
		{
			*OutProblems = Problems;
		}
		return Table;
	}

	UDataTable* MakeTableChecked(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Csv, const TCHAR* What)
	{
		TArray<FString> Problems;
		UDataTable* Table = MakeTable(RowStruct, Csv, &Problems);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), What, *Problem));
		}
		return Table;
	}

	bool LoadFishingCsv(FAutomationTestBase& Test, FString& OutCsv)
	{
		return LoadSource(Test, TEXT("DT_Fishing.csv"), OutCsv);
	}

	bool LoadMovementCsv(FAutomationTestBase& Test, FString& OutCsv)
	{
		return LoadSource(Test, TEXT("DT_Movement.csv"), OutCsv);
	}

	bool ShippedFishingRow(FAutomationTestBase& Test, FLureFishingRow& OutRow)
	{
		FString Csv;
		if (!LoadFishingCsv(Test, Csv))
		{
			return false;
		}
		const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(Test, FLureFishingRow::StaticStruct(), Csv, TEXT("DT_Fishing.csv")));
		const FLureFishingRow* Row = Table->FindRow<FLureFishingRow>(TEXT("Default"), TEXT("QA"), false);
		if (!Row)
		{
			Test.AddError(TEXT("QA: DT_Fishing.csv has no Default row"));
			return false;
		}
		OutRow = *Row;
		return true;
	}

	bool ShippedMovementRows(FAutomationTestBase& Test, TArray<FLureMovementRow>& OutRows)
	{
		FString Csv;
		if (!LoadMovementCsv(Test, Csv))
		{
			return false;
		}
		const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(Test, FLureMovementRow::StaticStruct(), Csv, TEXT("DT_Movement.csv")));
		TArray<FString> Problems;
		const uint8 FallbackMask = FLureMovementData::ResolveRows(Table.Get(), OutRows, Problems);
		if (FallbackMask != 0)
		{
			Test.AddError(FString::Printf(TEXT("QA: the shipped DT_Movement rows fall back (%s)"), *FString::Join(Problems, TEXT("; "))));
		}
		return OutRows.Num() == FLureMovementData::NumStates;
	}

	const FLureMovementRow& RowOf(const TArray<FLureMovementRow>& Rows, ELureMovementState State)
	{
		return Rows[static_cast<int32>(State)];
	}

	FLureFishingRow FlowProfile(const FLureFishingRow& Shipped, float BiteWait, float HookWindow)
	{
		FLureFishingRow Row = Shipped;
		Row.BiteWaitMin = Row.BiteWaitMax = BiteWait;
		Row.RebiteWaitMin = Row.RebiteWaitMax = 1.f;
		Row.NibblesMin = Row.NibblesMax = 0;
		Row.HookWindow = HookWindow;
		Row.AutoLandDelay = 0.f;
		Row.NoBiteHintDelay = 1.f;
		Row.MissEndsCast = false;
		Row.EarlyHook = ELureEarlyHookRule::ReelIn;
		return Row;
	}

	TArray<FString> DifferentFields(const UScriptStruct* Struct, const void* A, const void* B)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!It->Identical_InContainer(A, B))
			{
				Names.Add(It->GetName());
			}
		}
		return Names;
	}

	// ---- The level builder's marker tags (Content/Python/levels/build_level.py: marker_tags, _fmt) ----

	/** _fmt(float): "%.2f" without trailing zeros and dot. */
	static FString PyFmtFloat(double Value)
	{
		FString Text = FString::Printf(TEXT("%.2f"), Value);
		while (Text.EndsWith(TEXT("0")))
		{
			Text.LeftChopInline(1);
		}
		if (Text.EndsWith(TEXT(".")))
		{
			Text.LeftChopInline(1);
		}
		return Text;
	}

	/** str(x) of a JSON number as Python prints it (ints without a dot). */
	static FString PyStrNumber(double Value)
	{
		if (FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value), 1e-9))
		{
			return FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(Value)));
		}
		return FString::SanitizeFloat(Value);
	}

	TArray<FName> BuilderSpotTags(const TSharedPtr<FJsonObject>& Marker)
	{
		TArray<FString> TagTexts;
		TagTexts.Add(TEXT("Lure.FishingSpot"));
		TagTexts.Add(TEXT("Spot=") + Marker->GetStringField(TEXT("id")));
		TagTexts.Add(TEXT("Habitat=") + Marker->GetStringField(TEXT("habitat")));
		TagTexts.Add(TEXT("Region=") + Marker->GetStringField(TEXT("region")));
		TagTexts.Add(TEXT("Radius=") + PyFmtFloat(Marker->GetNumberField(TEXT("radius"))));
		TArray<FString> Hours;
		for (const TSharedPtr<FJsonValue>& Window : Marker->GetArrayField(TEXT("hours")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Pair = Window->AsArray();
			if (Pair.Num() == 2)
			{
				Hours.Add(PyStrNumber(Pair[0]->AsNumber()) + TEXT("-") + PyStrNumber(Pair[1]->AsNumber()));
			}
		}
		TagTexts.Add(TEXT("Hours=") + FString::Join(Hours, TEXT(";")));
		TArray<FString> Levels;
		const TArray<TSharedPtr<FJsonValue>>* Band = nullptr;
		if (Marker->TryGetArrayField(TEXT("level_band"), Band))
		{
			for (const TSharedPtr<FJsonValue>& Level : *Band)
			{
				Levels.Add(PyStrNumber(Level->AsNumber()));
			}
		}
		TagTexts.Add(TEXT("Levels=") + FString::Join(Levels, TEXT("-")));
		double Luck = 0.0;
		Marker->TryGetNumberField(TEXT("luck"), Luck);
		TagTexts.Add(TEXT("Luck=") + PyFmtFloat(Luck));
		FString Danger = TEXT("none");
		Marker->TryGetStringField(TEXT("danger"), Danger);
		TagTexts.Add(TEXT("Danger=") + Danger);
		TArray<FString> CastFrom;
		for (const TSharedPtr<FJsonValue>& Coordinate : Marker->GetArrayField(TEXT("cast_from")))
		{
			CastFrom.Add(PyFmtFloat(Coordinate->AsNumber()));
		}
		TagTexts.Add(TEXT("CastFrom=") + FString::Join(CastFrom, TEXT(",")));
		FString Name;
		if (Marker->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			TagTexts.Add(TEXT("Name=") + Name);
		}
		const TArray<TSharedPtr<FJsonValue>>* Extra = nullptr;
		if (Marker->TryGetArrayField(TEXT("tags"), Extra))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Extra)
			{
				TagTexts.Add(Value->AsString());
			}
		}
		TArray<FName> Names;
		for (const FString& Text : TagTexts)
		{
			Names.Add(FName(*Text));
		}
		return Names;
	}

	// ---- Log capture ----

	static FCriticalSection GLogCaptureLock;

	FLogCapture::FLogCapture(FName InCategory)
		: Category(InCategory)
	{
		GLog->AddOutputDevice(this);
	}

	FLogCapture::~FLogCapture()
	{
		GLog->RemoveOutputDevice(this);
	}

	void FLogCapture::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& InCategory)
	{
		if (InCategory == Category)
		{
			FScopeLock Lock(&GLogCaptureLock);
			Lines.Emplace(static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask), FString(Message));
		}
	}

	int32 FLogCapture::Count(ELogVerbosity::Type Verbosity, const TCHAR* Contains) const
	{
		FScopeLock Lock(&GLogCaptureLock);
		int32 Found = 0;
		for (const TPair<ELogVerbosity::Type, FString>& Line : Lines)
		{
			if (Line.Key == Verbosity && (!Contains || Line.Value.Contains(Contains)))
			{
				++Found;
			}
		}
		return Found;
	}

	// ---- Scene ----

	bool FScene::Create(FAutomationTestBase& Test, bool bDock, const FString* MovementCsv)
	{
		FString Csv;
		if (MovementCsv)
		{
			Csv = *MovementCsv;
		}
		else if (!LoadMovementCsv(Test, Csv))
		{
			return false;
		}
		Movement.Reset(MakeTableChecked(Test, FLureMovementRow::StaticStruct(), Csv, TEXT("DT_Movement (QA scene)")));
		bFishLoaded = FishQA::LoadReal(Test, Fish);
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(&Test);
			Test.AddError(TEXT("QA: the test world could not be created"));
			return false;
		}
		World = Wrapper.GetTestWorld();
		if (!World)
		{
			Test.AddError(TEXT("QA: no test world"));
			return false;
		}
		if (bDock)
		{
			AddBox(FVector(0.f, 0.f, DockTop * 0.5f), FVector(DockHalf, DockHalf, DockTop * 0.5f));
		}
		return bFishLoaded;
	}

	FScene::~FScene()
	{
		if (World && bListenSet)
		{
			World->NextURL.Reset();
		}
	}

	AActor* FScene::AddBox(const FVector& Center, const FVector& Extent)
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

	AActor* FScene::AddTaggedWater(float SurfaceZ, float HalfSize)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, NAME_None);
		Box->SetBoxExtent(FVector(HalfSize, HalfSize, 25.f), false);
		Box->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Box->SetRelativeLocation_Direct(FVector(0.f, 0.f, SurfaceZ - 25.f));
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Actor->Tags.Add(GetDefault<ULureFishingSettings>()->WaterTag);
		return Actor;
	}

	AActor* FScene::AddSpot(const FVector& Location, const TArray<FString>& KeyValues)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None);
		Root->SetRelativeLocation_Direct(Location);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->Tags.Add(TEXT("LureLayout"));
		Actor->Tags.Add(GetDefault<ULureFishingSettings>()->FishingSpotTag);
		for (const FString& KeyValue : KeyValues)
		{
			Actor->Tags.Add(FName(*KeyValue));
		}
		return Actor;
	}

	AActor* FScene::AddShoreSpot(const TArray<FString>& Extra)
	{
		TArray<FString> Tags = { TEXT("Spot=qa_shore"), TEXT("Name=QA Shore"), TEXT("Habitat=Habitat.Shore"), TEXT("Region=Region.Tropical.PalmKey"),
			FString::Printf(TEXT("Radius=%.0f"), SpotRadius) };
		Tags.Append(Extra);
		return AddSpot(SpotCenter, Tags);
	}

	ALurePlayerCharacter* FScene::Spawn(FAutomationTestBase& Test, const FVector& Feet, float Yaw)
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
			Test.AddError(TEXT("QA: ALurePlayerCharacter did not spawn with its movement and fishing components"));
			return nullptr;
		}
		Character->GetLureMovement()->ApplyMovementTable(Movement.Get());
		Character->GetLureMovement()->bRunPhysicsWithNoController = true;
		Character->FinishSpawning(Transform);
		return Character;
	}

	APlayerController* FScene::PossessLocally(ALurePlayerCharacter* Character)
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

	ULureFishingComponent* FScene::SetUpFishing(FAutomationTestBase& Test, ALurePlayerCharacter* Character, const FLureFishingRow& Profile, float Hours, int32 Seed)
	{
		ULureFishingComponent* Fishing = Character ? Character->GetFishing() : nullptr;
		if (!Fishing)
		{
			Test.AddError(TEXT("QA: no fishing component"));
			return nullptr;
		}
		FString Problem;
		if (!Profile.Validate(Problem))
		{
			Test.AddError(TEXT("QA fixture profile is invalid: ") + Problem);
		}
		Fishing->SetFishingProfile(Profile);
		Fishing->SetFishTables(Fish.Get());
		Fishing->SetRandomSeed(Seed);
		Fishing->TimeOfDayOverride = Hours;
		return Fishing;
	}

	void FScene::Tick(int32 Frames, float DeltaTime)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Wrapper.TickTestWorld(DeltaTime);
		}
	}

	void FScene::TickMoving(ALurePlayerCharacter* Character, const FVector& Direction, int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			if (IsValid(Character))
			{
				Character->AddMovementInput(Direction, 1.f, /*bForce*/ true);
			}
			Wrapper.TickTestWorld(Dt);
		}
	}

	bool FScene::TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, float DeltaTime)
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

	double FScene::Now() const
	{
		return World ? World->GetTimeSeconds() : 0.0;
	}

	void FScene::AdvanceTo(double Time)
	{
		for (int32 Guard = 0; Guard < 200000; ++Guard)
		{
			const double Remaining = Time - Now();
			if (Remaining <= 1.0e-7)
			{
				return;
			}
			Wrapper.TickTestWorld(static_cast<float>(FMath::Clamp(Remaining, 0.001, static_cast<double>(Dt))));
		}
	}

	void FScene::SetListenServer(bool bListen)
	{
		if (World)
		{
			World->NextURL = bListen ? TEXT("?listen") : TEXT("");
			bListenSet = bListen;
		}
	}

	// ---- Stages ----

	FString StageName(EStage Stage)
	{
		switch (Stage)
		{
		case EStage::Charging: return TEXT("Charging");
		case EStage::Casting: return TEXT("Casting");
		case EStage::Waiting: return TEXT("Waiting");
		case EStage::Nibble: return TEXT("Nibble");
		case EStage::Biting: return TEXT("Biting");
		case EStage::Hooked: return TEXT("Hooked");
		default: return TEXT("?");
		}
	}

	TArray<EStage> AllStages()
	{
		return { EStage::Charging, EStage::Casting, EStage::Waiting, EStage::Nibble, EStage::Biting, EStage::Hooked };
	}

	FLureFishingRow StageProfile(const FLureFishingRow& Shipped)
	{
		FLureFishingRow Row = Shipped;
		Row.MinCastDistance = 300.f;
		Row.MaxCastDistance = 800.f;
		Row.MaxLineLength = 900.f;
		Row.CastFlightTimeMin = Row.CastFlightTimeMax = 1.f;
		Row.BiteWaitMin = Row.BiteWaitMax = 3.f;
		Row.RebiteWaitMin = Row.RebiteWaitMax = 1.f;
		Row.NibblesMin = Row.NibblesMax = 1;
		Row.NibbleInterval = 1.f;
		Row.NibbleDuration = 0.6f;
		Row.HookWindow = 3.f;
		Row.AutoLandDelay = 0.f;
		Row.MissEndsCast = false;
		Row.EarlyHook = ELureEarlyHookRule::ReelIn;
		Row.NoBiteHintDelay = 1.f;
		return Row;
	}

	bool DriveToStage(FAutomationTestBase& Test, FScene& Scene, ALurePlayerCharacter* Character, ULureFishingComponent* Fishing, EStage Stage)
	{
		const FString Label = TEXT("QA drive to ") + StageName(Stage);
		if (!Character || !Fishing)
		{
			Test.AddError(Label + TEXT(": no character"));
			return false;
		}
		if (Stage == EStage::Charging)
		{
			Fishing->PressCast();
			Scene.Tick(5);
			if (!Fishing->IsCharging())
			{
				Test.AddError(Label + TEXT(": PressCast did not start charging (block: ") + BlockName(Fishing->GetCastBlock()) + TEXT(")"));
				return false;
			}
			return true;
		}
		if (!Fishing->AuthorityCast(0.f, static_cast<float>(Character->GetActorRotation().Yaw)))
		{
			Test.AddError(Label + TEXT(": the cast was refused (") + BlockName(Fishing->GetNetState().ResultReason) + TEXT(")"));
			return false;
		}
		if (Stage == EStage::Casting)
		{
			Scene.Tick(3);
			return Test.TestEqual(Label, StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Casting));
		}
		if (!Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Waiting; }, 180))
		{
			Test.AddError(Label + TEXT(": the bobber never landed (") + StateName(Fishing->GetFishingState()) + TEXT(")"));
			return false;
		}
		if (Stage == EStage::Waiting)
		{
			Scene.Tick(3);
			return Test.TestEqual(Label, StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Waiting));
		}
		if (Stage == EStage::Nibble)
		{
			const uint8 Before = Fishing->GetNetState().NibbleId;
			if (!Scene.TickUntil([Fishing, Before]() { return Fishing->GetNetState().NibbleId != Before; }, 400))
			{
				Test.AddError(Label + TEXT(": no nibble came"));
				return false;
			}
			Scene.Tick(2);
			const bool bInNibble = Fishing->GetFishingState() == ELureFishingState::Waiting
				&& Fishing->GetFishingTime() - Fishing->GetNetState().LastNibbleTime < Fishing->GetProfile().NibbleDuration;
			return Test.TestTrue(Label + TEXT(": inside the nibble"), bInNibble);
		}
		if (!Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 600))
		{
			Test.AddError(Label + TEXT(": no bite came (") + StateName(Fishing->GetFishingState()) + TEXT(")"));
			return false;
		}
		Scene.Tick(3);
		if (Stage == EStage::Biting)
		{
			return Test.TestEqual(Label, StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Biting));
		}
		Fishing->AuthorityHook();
		return Test.TestEqual(Label, StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

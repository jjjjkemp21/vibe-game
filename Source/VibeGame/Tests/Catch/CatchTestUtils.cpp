// Lure T-030 tests (unreal-engineer): shared helpers for Project.Catch.*.

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureMovementTypes.h"
#include "Character/LurePlayerCharacter.h"
#include "Character/LureWaterVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fishing/LureFishingComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionTypes.h"
#include "UObject/Package.h"

namespace LureCatchTest
{
	FString SourcePath(const TCHAR* File)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / File);
	}

	bool ReadSource(FAutomationTestBase& Test, const TCHAR* File, FString& OutText)
	{
		if (!FFileHelper::LoadFileToString(OutText, *SourcePath(File)))
		{
			Test.AddError(FString::Printf(TEXT("can't read data/tables/%s"), File));
			return false;
		}
		return true;
	}

	TStrongObjectPtr<UDataTable> MakeTable(UScriptStruct* RowStruct, const FString& Text, bool bJson, TArray<FString>* OutProblems)
	{
		TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient));
		Table->RowStruct = RowStruct;
		const TArray<FString> Problems = bJson ? Table->CreateTableFromJSONString(Text) : Table->CreateTableFromCSVString(Text);
		if (OutProblems)
		{
			*OutProblems = Problems;
		}
		return Table;
	}

	TStrongObjectPtr<UDataTable> MakeTableChecked(FAutomationTestBase& Test, UScriptStruct* RowStruct, const FString& Text, bool bJson, const TCHAR* What)
	{
		TArray<FString> Problems;
		TStrongObjectPtr<UDataTable> Table = MakeTable(RowStruct, Text, bJson, &Problems);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), What, *Problem));
		}
		return Table;
	}

	TStrongObjectPtr<UDataTable> Shipped(FAutomationTestBase& Test, UScriptStruct* RowStruct, const TCHAR* File)
	{
		FString Text;
		if (!ReadSource(Test, File, Text))
		{
			return TStrongObjectPtr<UDataTable>();
		}
		return MakeTableChecked(Test, RowStruct, Text, FString(File).EndsWith(TEXT(".json")), File);
	}

	FFishInstance MakeFish(FName SpeciesId, int32 Value, int32 Xp, float WeightKg, int32 Seed)
	{
		FFishInstance Fish;
		Fish.SpeciesId = SpeciesId;
		Fish.RarityId = TEXT("Common");
		Fish.WeightKg = WeightKg;
		Fish.Level = 1;
		Fish.Value = Value;
		Fish.Xp = Xp;
		Fish.DifficultyRating = 1.0f;
		Fish.Seed = Seed;
		return Fish;
	}

	FString QuickFreshnessCsv()
	{
		return TEXT("Name,GraceSeconds,SpoilSeconds,CurveExponent,MinValueShare,DevComment\n")
			TEXT("Default,1,2,1.0,0.25,\"test: spoils fast\"\n");
	}

	FString FixtureMarketCsv()
	{
		return TEXT("Name,DisplayName,SellMultiplier,DevComment\n")
			TEXT("Default,\"Fixture default\",1.0,\n")
			TEXT("Premium,\"Fixture premium\",1.5,\n");
	}

	bool FWorld::Create(FAutomationTestBase& Test, bool bQuickFreshness)
	{
		Movement = Shipped(Test, FLureMovementRow::StaticStruct(), TEXT("DT_Movement.csv"));
		Levels = MakeTableChecked(Test, FPlayerLevelRow::StaticStruct(), TEXT("Name,Level,XpToNext,DevComment\nL01,1,100,\nL02,2,150,\nL03,3,0,\n"), false, TEXT("levels"));
		Coolers = Shipped(Test, FCoolerRow::StaticStruct(), TEXT("DT_Cooler.csv"));
		Markets = MakeTableChecked(Test, FFishMarketRow::StaticStruct(), FixtureMarketCsv(), false, TEXT("markets"));
		Freshness = bQuickFreshness ? MakeTableChecked(Test, FLureFreshnessRow::StaticStruct(), QuickFreshnessCsv(), false, TEXT("quick freshness"))
			: Shipped(Test, FLureFreshnessRow::StaticStruct(), TEXT("DT_Freshness.csv"));
		Catch = Shipped(Test, FLureCatchRow::StaticStruct(), TEXT("DT_Catch.csv"));
		Display = Shipped(Test, FLureCoolerDisplayRow::StaticStruct(), TEXT("DT_CoolerDisplay.json"));
		if (!Movement.IsValid() || !Coolers.IsValid() || !Freshness.IsValid() || !Catch.IsValid() || !Display.IsValid() || !FishQA::LoadReal(Test, FishTables))
		{
			return false;
		}
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(&Test);
			Test.AddError(TEXT("the catch test world could not be created"));
			return false;
		}
		World = Wrapper.GetTestWorld();
		if (!World)
		{
			return false;
		}
		ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World);
		if (!Test.TestNotNull(TEXT("the catch subsystem"), Subsystem))
		{
			return false;
		}
		const FLureCatchRow* Row = Catch->FindRow<FLureCatchRow>(TEXT("Default"), TEXT("CatchTest"), false);
		if (!Test.TestNotNull(TEXT("DT_Catch.csv has the Default row"), Row))
		{
			return false;
		}
		Subsystem->SetTuning(*Row);
		Subsystem->SetFreshnessTable(Freshness.Get());
		Subsystem->SetCoolerTable(Coolers.Get());
		Subsystem->SetCoolerDisplayTable(Display.Get());
		Subsystem->SetFishTables(FishTables.Get());
		AddBox(FVector(0.0f, 0.0f, DockTop * 0.5f), FVector(DockHalf, DockHalf, DockTop * 0.5f));
		return true;
	}

	AActor* FWorld::AddBox(const FVector& Center, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
		Box->SetBoxExtent(Extent, false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetRelativeLocation_Direct(Center); // before registering: a registered static component can't be moved in a game world
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		return Actor;
	}

	ALureWaterVolume* FWorld::AddWater(float Depth)
	{
		const FTransform Transform(FRotator::ZeroRotator, FVector::ZeroVector);
		ALureWaterVolume* Volume = World->SpawnActorDeferred<ALureWaterVolume>(ALureWaterVolume::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Volume)
		{
			return nullptr;
		}
		Volume->SurfaceHalfSize = FVector2D(3500.0, 3500.0);
		Volume->WaterDepth = Depth;
		Volume->FinishSpawning(Transform);
		return Volume;
	}

	ALurePlayerCharacter* FWorld::SpawnPlayer(FAutomationTestBase& Test, const FVector& Feet, bool bLocal)
	{
		TArray<FLureMovementRow> Rows;
		TArray<FString> Problems;
		FLureMovementData::ResolveRows(Movement.Get(), Rows, Problems);
		const float HalfHeight = Rows.IsValidIndex(static_cast<int32>(ELureMovementState::Stand)) ? Rows[static_cast<int32>(ELureMovementState::Stand)].CapsuleHalfHeight : 90.0f;
		const FTransform Transform(FRotator::ZeroRotator, Feet + FVector(0.0f, 0.0f, HalfHeight + 2.15f));
		ALurePlayerCharacter* Character = World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test.TestNotNull(TEXT("player character spawned"), Character))
		{
			return nullptr;
		}
		Character->GetLureMovement()->ApplyMovementTable(Movement.Get());
		Character->GetLureMovement()->bRunPhysicsWithNoController = true;
		Character->FinishSpawning(Transform);

		ALurePlayerState* State = World->SpawnActorDeferred<ALurePlayerState>(ALurePlayerState::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test.TestNotNull(TEXT("player state spawned"), State) || !State->GetProgression())
		{
			return Character;
		}
		State->GetProgression()->SetLevelTable(Levels.Get());
		State->FinishSpawning(FTransform::Identity);
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("controller spawned"), Controller))
		{
			return Character;
		}
		if (bLocal)
		{
			Controller->SetAsLocalPlayerController();
		}
		State->SetOwner(Controller);
		Controller->SetPlayerState(State);
		Controller->Possess(Character);
		Character->SetPlayerState(State);
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Tick(2);
		return Character;
	}

	ALureCoolerActor* FWorld::SpawnCooler(const FVector& Floor, float Yaw, FName CoolerId, APlayerState* Owner)
	{
		return ALureCoolerActor::SpawnCooler(World, CoolerId, FTransform(FRotator(0.0f, Yaw, 0.0f), Floor), Owner);
	}

	ALureSellCounter* FWorld::SpawnCounter(const FVector& Top, float Yaw, FName MarketId)
	{
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Top);
		ALureSellCounter* Counter = World->SpawnActorDeferred<ALureSellCounter>(ALureSellCounter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Counter)
		{
			return nullptr;
		}
		Counter->MarketId = MarketId;
		Counter->SetMarketTable(Markets.Get());
		Counter->FinishSpawning(Transform);
		return Counter;
	}

	ALureFishItem* FWorld::Land(ALurePlayerCharacter* Player, const FFishInstance& Fish)
	{
		const FLureFishLandedResult Result = ULureCatchLibrary::HandleFishLanded(Player, Fish);
		return Cast<ALureFishItem>(Result.FishItem);
	}

	ALureFishItem* FWorld::LandInHand(ALurePlayerCharacter* Player, const FFishInstance& Fish)
	{
		ALureFishItem* Item = Land(Player, Fish);
		ULureHandsComponent* Hands = HandsOf(Player);
		return (Item && Hands && Hands->AuthorityTakeInHand(Item)) ? Item : nullptr;
	}

	AActor* FWorld::SpawnMarker(const FVector& Location, float Yaw, FName Tag)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Root->SetRelativeLocation_Direct(Location);
		Root->SetRelativeRotation_Direct(FRotator(0.0f, Yaw, 0.0f));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		if (!Tag.IsNone())
		{
			Actor->Tags.Add(Tag);
		}
		return Actor;
	}

	void FWorld::Tick(int32 Frames, float DeltaTime)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Wrapper.TickTestWorld(DeltaTime);
		}
	}

	bool FWorld::TickUntil(TFunctionRef<bool()> Predicate, int32 MaxFrames, float DeltaTime)
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

	void FWorld::Advance(float Seconds)
	{
		const double End = World->GetTimeSeconds() + Seconds;
		for (int32 Guard = 0; World->GetTimeSeconds() < End && Guard < 100000; ++Guard)
		{
			Wrapper.TickTestWorld(0.1f);
		}
	}

	double FWorld::Now() const
	{
		return FLureFreshness::GetServerTime(World);
	}

	APlayerController* ControllerOf(const ALurePlayerCharacter* Player)
	{
		return Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	}

	void LookAt(ALurePlayerCharacter* Player, const FVector& Target)
	{
		if (APlayerController* Controller = ControllerOf(Player))
		{
			Controller->SetControlRotation((Target - Player->GetPawnViewLocation()).Rotation());
		}
	}

	FVector FeetOf(const ALurePlayerCharacter* Player)
	{
		const float HalfHeight = Player && Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
		return Player ? Player->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight) : FVector::ZeroVector;
	}

	void PlaceAt(ALurePlayerCharacter* Player, const FVector& Feet, float Yaw)
	{
		if (!Player)
		{
			return;
		}
		const float HalfHeight = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
		Player->SetActorLocationAndRotation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.15f), FRotator(0.0f, Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (APlayerController* Controller = ControllerOf(Player))
		{
			Controller->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
		}
	}

	ULureHandsComponent* HandsOf(const ALurePlayerCharacter* Player)
	{
		return Player ? Player->GetHands() : nullptr;
	}

	ULureInteractionComponent* InteractionOf(const ALurePlayerCharacter* Player)
	{
		return Player ? Player->FindComponentByClass<ULureInteractionComponent>() : nullptr;
	}

	ULureProgressionComponent* ProgressionOf(const ALurePlayerCharacter* Player)
	{
		const ALurePlayerState* State = Player ? Player->GetPlayerState<ALurePlayerState>() : nullptr;
		return State ? State->GetProgression() : nullptr;
	}

	void UseEstimatedRodTip(ALurePlayerCharacter* Player)
	{
		ULureFishingComponent* Fishing = Player ? Player->GetFishing() : nullptr;
		if (UStaticMeshComponent* Rod = Fishing ? Fishing->GetRodMesh() : nullptr)
		{
			Rod->SetStaticMesh(nullptr); // no rod socket: ULureFishingComponent::GetLineStart uses the eye estimate
		}
	}

	FString VerbName(ELureInteractVerb Verb)
	{
		return StaticEnum<ELureInteractVerb>()->GetNameStringByValue(static_cast<int64>(Verb));
	}

	FString BlockName(ELureCastBlock Block)
	{
		return StaticEnum<ELureCastBlock>()->GetNameStringByValue(static_cast<int64>(Block));
	}

	FString NoticesOf(const ALurePlayerCharacter* Player)
	{
		const ULureProgressionComponent* Progression = ProgressionOf(Player);
		return Progression ? FString::Join(Progression->GetNoticeLines(), TEXT(" | ")) : FString();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

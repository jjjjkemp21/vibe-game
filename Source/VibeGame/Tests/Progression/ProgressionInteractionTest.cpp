// Lure T-010 tests (unreal-engineer): selling at ALureSellPoint, the Interact key, and the replication setup
// (Project.Progression.Sell.*, .Interact.*, .Net.*).

#include "Tests/Progression/ProgressionTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "Game/LureGameMode.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Interaction/LureInteractionComponent.h"
#include "Interaction/LureInteractionSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Progression/LureCoolerComponent.h"
#include "Progression/LureProgressionComponent.h"
#include "Progression/LureProgressionLibrary.h"
#include "Progression/LureSellPoint.h"
#include "UObject/UnrealType.h"

namespace ProgressionInteractionTest
{
	struct FTables
	{
		TStrongObjectPtr<UDataTable> Levels;
		TStrongObjectPtr<UDataTable> Coolers;
		TStrongObjectPtr<UDataTable> Markets;

		explicit FTables(FAutomationTestBase& Test)
		{
			Levels = LPT::MakeTable(Test, FPlayerLevelRow::StaticStruct(), LPT::FixtureLevelCsv());
			Coolers = LPT::MakeTable(Test, FCoolerRow::StaticStruct(), LPT::FixtureCoolerCsv());
			Markets = LPT::MakeTable(Test, FFishMarketRow::StaticStruct(), LPT::FixtureMarketCsv());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionSellAllAndOneAtSellPoint, "Project.Progression.Sell.SellAllAndOneAtSellPoint", LPT::Flags)
bool FProgressionSellAllAndOneAtSellPoint::RunTest(const FString& Parameters)
{
	using namespace ProgressionInteractionTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get(), /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	ALureSellPoint* Premium = LPT::SpawnSellPoint(*this, World, FVector::ZeroVector, Tables.Markets.Get(), TEXT("Premium"));
	ALureSellPoint* Default = LPT::SpawnSellPoint(*this, World, FVector(0.0f, 200.0f, 0.0f), Tables.Markets.Get());
	ALureSellPoint* Far = LPT::SpawnSellPoint(*this, World, FVector(5000.0f, 0.0f, 0.0f), Tables.Markets.Get());
	if (!Player.IsValid() || !Player.Pawn || !Premium || !Default || !Far)
	{
		return false;
	}
	TestEqual(TEXT("Premium market multiplier from DT_FishMarket"), Premium->GetSellMultiplier(), 1.5f, 1e-6f);
	TestEqual(TEXT("MarketId None uses the default market"), Default->GetEffectiveMarketId(), FName(TEXT("Default")));
	TestEqual(TEXT("default market multiplier"), Default->GetSellMultiplier(), 1.0f, 1e-6f);

	// Values 10, 7, 21 at x1.5: 15 + round(10.5) = 11 + round(31.5) = 32 -> 58.
	TestEqual(TEXT("3 fish in the cooler"), LPT::FillCooler(Player.Cooler, { 10, 7, 21 }), 3);
	TestEqual(TEXT("the quote = the per-fish prices"), Premium->QuoteAll(Player.Pawn), 58);

	FLureSaleResult Sale = Premium->SellOne(Player.Pawn, 1); // the 7-coin fish
	TestEqual(TEXT("SellOne sells 1"), Sale.FishSold, 1);
	TestEqual(TEXT("SellOne pays round-half-up(7 x 1.5) = 11"), Sale.MoneyEarned, 11);
	TestEqual(TEXT("money after SellOne"), Player.Progression->GetMoney(), 11);
	TestEqual(TEXT("2 fish left"), Player.Cooler->GetNumFish(), 2);
	TestEqual(TEXT("SellOne of an empty slot sells nothing"), Premium->SellOne(Player.Pawn, 7).FishSold, 0);

	TestEqual(TEXT("a sell point out of range sells nothing"), Far->SellAll(Player.Pawn).FishSold, 0);
	TestEqual(TEXT("... and leaves the cooler"), Player.Cooler->GetNumFish(), 2);

	Sale = Premium->SellAll(Player.Pawn);
	TestEqual(TEXT("SellAll sells the rest"), Sale.FishSold, 2);
	TestEqual(TEXT("SellAll pays 15 + 32"), Sale.MoneyEarned, 47);
	TestEqual(TEXT("money after SellAll"), Player.Progression->GetMoney(), 58);
	TestEqual(TEXT("the cooler is empty"), Player.Cooler->GetNumFish(), 0);
	TestEqual(TEXT("selling an empty cooler pays nothing"), Premium->SellAll(Player.Pawn).MoneyEarned, 0);
	TestEqual(TEXT("XP is not paid again when selling"), Player.Progression->GetTotalXp(), 0);

	// A missing market row pays x1 (and warns once).
	ALureSellPoint* Unknown = LPT::SpawnSellPoint(*this, World, FVector::ZeroVector, Tables.Markets.Get(), TEXT("NoSuchMarket"));
	AddExpectedMessage(TEXT("has no row 'NoSuchMarket'"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("a missing market row pays x1"), Unknown ? Unknown->GetSellMultiplier() : 0.0f, 1.0f, 1e-6f);
	TestEqual(TEXT("... and warns only once"), Unknown ? Unknown->GetSellMultiplier() : 0.0f, 1.0f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionInteractSellPointFlow, "Project.Progression.Interact.SellPointFlow", LPT::Flags)
bool FProgressionInteractSellPointFlow::RunTest(const FString& Parameters)
{
	using namespace ProgressionInteractionTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get(), /*bWithPawn*/ true, FVector(2000.0f, 0.0f, 0.0f));
	ALureSellPoint* Near = LPT::SpawnSellPoint(*this, World, FVector::ZeroVector, Tables.Markets.Get(), TEXT("Premium"), 300.0f);
	ALureSellPoint* Other = LPT::SpawnSellPoint(*this, World, FVector(500.0f, 0.0f, 0.0f), Tables.Markets.Get(), TEXT("Cheap"), 300.0f);
	if (!Player.IsValid() || !Player.Pawn || !Player.Interaction || !Near || !Other)
	{
		return false;
	}
	const ULureInteractionSubsystem* Subsystem = ULureInteractionSubsystem::Get(World.World);
	if (!TestNotNull(TEXT("interaction subsystem"), Subsystem))
	{
		return false;
	}
	TestTrue(TEXT("sell points register themselves"), Subsystem->GetInteractables().Contains(Near) && Subsystem->GetInteractables().Contains(Other));

	TestNull(TEXT("far from both: nothing to interact with"), Player.Interaction->FindBestInteractable());
	TestTrue(TEXT("far away: no prompt"), Player.Interaction->GetPromptText().IsEmpty());
	TestFalse(TEXT("the server refuses a sell point out of range"), Player.Interaction->TryInteract(Near));

	LPT::FillCooler(Player.Cooler, { 10, 20 });
	Player.Pawn->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	TestTrue(TEXT("between both, the nearer one wins (Near at 200 cm, Other at 300 cm)"), Player.Interaction->FindBestInteractable() == Near);
	Player.Pawn->SetActorLocation(FVector(400.0f, 0.0f, 0.0f));
	TestTrue(TEXT("closer to Other: Other wins"), Player.Interaction->FindBestInteractable() == Other);
	Player.Pawn->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("next to Near: Near wins"), Player.Interaction->FindBestInteractable() == Near);

	const FString Prompt = Player.Interaction->GetPromptText();
	const FString Key = GetDefault<ULureCharacterSettings>()->InteractKeys.Num() > 0
		? GetDefault<ULureCharacterSettings>()->InteractKeys[0].GetDisplayName(false).ToString() : FString();
	TestTrue(FString::Printf(TEXT("the prompt names the key [%s] (%s)"), *Key, *Prompt), Prompt.StartsWith(FString::Printf(TEXT("[%s]"), *Key)));
	TestTrue(FString::Printf(TEXT("the prompt says 2 fish (%s)"), *Prompt), Prompt.Contains(TEXT("2 fish")));
	TestTrue(FString::Printf(TEXT("the prompt quotes 45 coins (%s)"), *Prompt), Prompt.Contains(TEXT("45 coins")));
	const TArray<FString> Lines = ULureProgressionLibrary::GetPlaceholderStatusLines(nullptr);
	TestEqual(TEXT("no controller, no HUD lines"), Lines.Num(), 0);

	// The Interact press (what the key runs): standalone = server, so it sells at once.
	Player.Interaction->HandleInteractPressed();
	TestEqual(TEXT("Interact sold the cooler"), Player.Cooler->GetNumFish(), 0);
	TestEqual(TEXT("Interact paid round(15) + round(30) = 45"), Player.Progression->GetMoney(), 45);
	TestTrue(TEXT("empty cooler prompt"), Player.Interaction->GetPromptText().Contains(TEXT("empty")));

	// A specific slot through the same server path (Option = slot), as a later UI will send it.
	LPT::FillCooler(Player.Cooler, { 4, 8 });
	TestTrue(TEXT("RequestInteract with a slot"), Player.Interaction->RequestInteract(Near, 1));
	TestEqual(TEXT("slot 1 sold (8 x 1.5 = 12)"), Player.Progression->GetMoney(), 57);
	TestEqual(TEXT("1 fish left"), Player.Cooler->GetNumFish(), 1);

	Near->Destroy();
	TestFalse(TEXT("a destroyed sell point unregisters"), Subsystem->GetInteractables().Contains(Near));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionInteractDestroyedSellPointSellsNothing, "Project.Progression.Interact.DestroyedSellPointSellsNothing", LPT::Flags)
bool FProgressionInteractDestroyedSellPointSellsNothing::RunTest(const FString& Parameters)
{
	// QA T010-O1: a sell point that is being destroyed, passed in directly, must not sell (TryInteract, SellAll, SellOne).
	using namespace ProgressionInteractionTest;
	FTables Tables(*this);
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const LPT::FPlayer Player = LPT::SpawnPlayer(*this, World, Tables.Levels.Get(), Tables.Coolers.Get(), /*bWithPawn*/ true, FVector(100.0f, 0.0f, 0.0f));
	ALureSellPoint* Point = LPT::SpawnSellPoint(*this, World, FVector::ZeroVector, Tables.Markets.Get(), TEXT("Premium"), 300.0f);
	if (!Player.IsValid() || !Player.Pawn || !Player.Interaction || !Point)
	{
		return false;
	}
	TestEqual(TEXT("2 fish in the cooler"), LPT::FillCooler(Player.Cooler, { 10, 20 }), 2);
	TestTrue(TEXT("in range of a live sell point"), Point->IsInInteractionRange(Player.Pawn, 0.0f));

	Point->Destroy();
	TestFalse(TEXT("the sell point is no longer valid"), IsValid(Point));
	TestFalse(TEXT("TryInteract refuses a destroyed sell point"), Player.Interaction->TryInteract(Point, INDEX_NONE));
	TestFalse(TEXT("... also for one slot"), Player.Interaction->TryInteract(Point, 0));
	TestEqual(TEXT("SellAll on a destroyed sell point sells nothing"), Point->SellAll(Player.Pawn).FishSold, 0);
	TestEqual(TEXT("SellOne on a destroyed sell point sells nothing"), Point->SellOne(Player.Pawn, 0).FishSold, 0);
	TestEqual(TEXT("the fish are still in the cooler"), Player.Cooler->GetNumFish(), 2);
	TestEqual(TEXT("no money paid"), Player.Progression->GetMoney(), 0);
	TestFalse(TEXT("TryInteract refuses null"), Player.Interaction->TryInteract(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionInteractCharacterBindsInteract, "Project.Progression.Interact.CharacterBindsInteract", LPT::Flags)
bool FProgressionInteractCharacterBindsInteract::RunTest(const FString& Parameters)
{
	// The action: Boolean, mapped to E and gamepad face-left in the shared mapping context.
	const UInputAction* Action = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Interact);
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	if (!TestNotNull(TEXT("the Interact action exists"), Action) || !TestNotNull(TEXT("default mapping context"), Context))
	{
		return false;
	}
	TestEqual(TEXT("Interact is a button"), static_cast<int32>(Action->ValueType), static_cast<int32>(EInputActionValueType::Boolean));
	TestTrue(TEXT("GetInputActionNames lists Interact"), ULureInputSubsystem::GetInputActionNames().Contains(FLureInputActionNames::Interact));
	auto IsMapped = [Context, Action](const FKey& Key)
	{
		return Context->GetMappings().ContainsByPredicate([&](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action && Mapping.Key == Key; });
	};
	TestTrue(TEXT("E interacts"), IsMapped(EKeys::E));
	TestTrue(TEXT("gamepad face-left interacts"), IsMapped(EKeys::Gamepad_FaceButton_Left));

	// The character creates the interaction component and binds the action when a local player possesses it.
	LPT::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	const FTransform Transform(FVector(0.0f, 0.0f, 200.0f));
	ALurePlayerCharacter* Character = World.World->SpawnActorDeferred<ALurePlayerCharacter>(ALurePlayerCharacter::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("character"), Character))
	{
		return false;
	}
	Character->GetLureMovement()->ApplyMovementTable(nullptr);
	Character->FinishSpawning(Transform);
	TestNotNull(TEXT("the character has an interaction component"), Character->GetInteraction());

	APlayerController* Controller = World.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	World.Tick(3);
	const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Character->InputComponent);
	if (!TestNotNull(TEXT("enhanced input component after possession"), Input))
	{
		return false;
	}
	const bool bBound = Input->GetActionEventBindings().ContainsByPredicate([Action](const TUniquePtr<FEnhancedInputActionEventBinding>& Binding)
	{
		return Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == ETriggerEvent::Started;
	});
	TestTrue(TEXT("Interact (Started) is bound after possession"), bBound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionNetReplicationSetup, "Project.Progression.Net.ReplicationSetup", LPT::Flags)
bool FProgressionNetReplicationSetup::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("ALureGameMode uses ALurePlayerState"), GetDefault<ALureGameMode>()->PlayerStateClass == ALurePlayerState::StaticClass());

	const ALurePlayerState* State = GetDefault<ALurePlayerState>();
	TestNotNull(TEXT("the player state has a cooler"), State->GetCooler());
	TestNotNull(TEXT("the player state has progression"), State->GetProgression());
	TestTrue(TEXT("the cooler replicates"), State->GetCooler() && State->GetCooler()->GetIsReplicated());
	TestTrue(TEXT("progression replicates"), State->GetProgression() && State->GetProgression()->GetIsReplicated());
	TestTrue(TEXT("the interaction component replicates (server RPC)"), GetDefault<ULureInteractionComponent>()->GetIsReplicated());

	auto FindRep = [this](UClass* Class, const UObject* Default, FName Property, ELifetimeCondition Expected)
	{
		const FProperty* Found = Class->FindPropertyByName(Property);
		if (!TestNotNull(FString::Printf(TEXT("%s.%s exists"), *Class->GetName(), *Property.ToString()), Found))
		{
			return;
		}
		TestTrue(FString::Printf(TEXT("%s.%s is replicated"), *Class->GetName(), *Property.ToString()), Found->HasAnyPropertyFlags(CPF_Net));
		// RepIndex is only valid once the class's replication data exists (the net driver does this before replicating).
		Class->SetUpRuntimeReplicationData();
		TArray<FLifetimeProperty> Lifetime;
		Default->GetLifetimeReplicatedProps(Lifetime);
		const FLifetimeProperty* Entry = Lifetime.FindByPredicate([Found](const FLifetimeProperty& P) { return P.RepIndex == Found->RepIndex; });
		if (TestNotNull(FString::Printf(TEXT("%s.%s is registered for replication"), *Class->GetName(), *Property.ToString()), Entry))
		{
			TestEqual(FString::Printf(TEXT("%s.%s condition"), *Class->GetName(), *Property.ToString()), static_cast<int32>(Entry->Condition), static_cast<int32>(Expected));
		}
	};
	FindRep(ULureCoolerComponent::StaticClass(), GetDefault<ULureCoolerComponent>(), TEXT("StoredFish"), COND_OwnerOnly);
	FindRep(ULureCoolerComponent::StaticClass(), GetDefault<ULureCoolerComponent>(), TEXT("Capacity"), COND_None);
	FindRep(ULureCoolerComponent::StaticClass(), GetDefault<ULureCoolerComponent>(), TEXT("CoolerId"), COND_None);
	FindRep(ULureProgressionComponent::StaticClass(), GetDefault<ULureProgressionComponent>(), TEXT("Money"), COND_None);
	FindRep(ULureProgressionComponent::StaticClass(), GetDefault<ULureProgressionComponent>(), TEXT("TotalXp"), COND_None);
	FindRep(ULureProgressionComponent::StaticClass(), GetDefault<ULureProgressionComponent>(), TEXT("Level"), COND_None);

	const UFunction* ServerInteract = ULureInteractionComponent::StaticClass()->FindFunctionByName(TEXT("ServerInteract"));
	TestTrue(TEXT("ServerInteract is a reliable server RPC"), ServerInteract && ServerInteract->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));

	for (const TCHAR* Name : { TEXT("AddFish"), TEXT("RemoveFish"), TEXT("Clear"), TEXT("SetCoolerId") })
	{
		const UFunction* Function = ULureCoolerComponent::StaticClass()->FindFunctionByName(Name);
		TestTrue(FString::Printf(TEXT("cooler %s is BlueprintAuthorityOnly"), Name), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
	}
	for (const TCHAR* Name : { TEXT("HandleFishLanded"), TEXT("AddMoney"), TEXT("SpendMoney"), TEXT("AddXp"), TEXT("ApplySaveData") })
	{
		const UFunction* Function = ULureProgressionComponent::StaticClass()->FindFunctionByName(Name);
		TestTrue(FString::Printf(TEXT("progression %s is BlueprintAuthorityOnly"), Name), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure: spawns and removes the fighting fish (T-029).

#include "Fish/LureFightFishSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/FishSettings.h"
#include "Fish/FishTypes.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Fishing/LureFishingComponent.h"
#include "GameFramework/Pawn.h"
#include "Misc/PackageName.h"

ULureFightFishSubsystem* ULureFightFishSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<ULureFightFishSubsystem>() : nullptr;
}

bool ULureFightFishSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool ULureFightFishSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void FLureFightFishTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && TickType != LEVELTICK_TimeOnly)
	{
		Target->UpdateVisuals(DeltaTime);
	}
}

void ULureFightFishSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!UpdateTick.IsTickFunctionRegistered() && InWorld.PersistentLevel && InWorld.GetNetMode() != NM_DedicatedServer)
	{
		// After TG_PostUpdateWork (the fight step in ULureFishingComponent's tick): this frame's fight state, same-frame ends.
		UpdateTick.TickGroup = TG_LastDemotable;
		UpdateTick.EndTickGroup = TG_LastDemotable;
		UpdateTick.bCanEverTick = true;
		UpdateTick.bStartWithTickEnabled = true;
		UpdateTick.bAllowTickOnDedicatedServer = false;
		UpdateTick.bTickEvenWhenPaused = false;
		UpdateTick.Target = this;
		UpdateTick.RegisterTickFunction(InWorld.PersistentLevel);
	}
}

void ULureFightFishSubsystem::Deinitialize()
{
	if (UpdateTick.IsTickFunctionRegistered())
	{
		UpdateTick.UnRegisterTickFunction();
	}
	UpdateTick.Target = nullptr;
	Active.Reset();
	Ending.Reset();
	KeepAlive.Reset();
	Loaded.Reset();
	Super::Deinitialize();
}

void ULureFightFishSubsystem::SetTables(const UDataTable* InSpeciesTable, const UDataTable* InVisualTable)
{
	SpeciesTable = const_cast<UDataTable*>(InSpeciesTable);
	VisualTable = const_cast<UDataTable*>(InVisualTable);
	bSpeciesOverride = InSpeciesTable != nullptr;
	bVisualOverride = InVisualTable != nullptr;
	bResolved = false;
}

UObject* ULureFightFishSubsystem::LoadPath(const FSoftObjectPath& Path)
{
	if (Path.IsNull() || Missing.Contains(Path))
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<UObject>* Found = Loaded.Find(Path))
	{
		if (Found->IsValid())
		{
			return Found->Get();
		}
	}
	UObject* Object = Path.ResolveObject();
	if (!Object)
	{
		// Only load what exists (a missing asset is normal in lanes and before the editor-operator builds it): no load errors.
		const FString Package = Path.GetLongPackageName();
		if (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package))
		{
			Object = Path.TryLoad();
		}
	}
	if (!Object)
	{
		Missing.Add(Path);
		return nullptr;
	}
	Loaded.Add(Path, Object);
	KeepAlive.Add(Object);
	return Object;
}

void ULureFightFishSubsystem::Resolve()
{
	if (bResolved)
	{
		return;
	}
	bResolved = true;
	const ULureFishVisualSettings* Settings = GetDefault<ULureFishVisualSettings>();
	if (!bSpeciesOverride)
	{
		SpeciesTable = Cast<UDataTable>(LoadPath(GetDefault<UFishSettings>()->SpeciesTable.ToSoftObjectPath()));
	}
	if (!bVisualOverride)
	{
		VisualTable = Cast<UDataTable>(LoadPath(Settings->VisualTable.ToSoftObjectPath()));
	}

	Row = FFishVisualRow::GetFallbackRow();
	const FFishVisualRow* Found = (VisualTable && VisualTable->GetRowStruct() == FFishVisualRow::StaticStruct())
		? VisualTable->FindRow<FFishVisualRow>(Settings->VisualRow, TEXT("FightFish"), false) : nullptr;
	FString Problem;
	static bool bWarnedMissingRow = false; // one warning per session (lanes and tests run without the imported asset)
	if (!Found)
	{
		if (bWarnedMissingRow)
		{
			return;
		}
		bWarnedMissingRow = true;
		UE_LOG(LogLureFish, Warning, TEXT("Fight fish: no DT_FishVisual row '%s' ('%s' not imported? source data/tables/DT_FishVisual.json): using the built-in tuning."),
			*Settings->VisualRow.ToString(), *Settings->VisualTable.ToString());
	}
	else if (!Found->Validate(Problem))
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fight fish: DT_FishVisual row '%s' is invalid (%s): using the built-in tuning."), *Settings->VisualRow.ToString(), *Problem);
	}
	else
	{
		Row = *Found;
	}
}

const FFishVisualRow& ULureFightFishSubsystem::GetVisualRow()
{
	Resolve();
	return Row;
}

TArray<FSoftObjectPath> ULureFightFishSubsystem::MeshCandidates(const FFishSpeciesRow* Species, const TSoftObjectPtr<USkeletalMesh>& Fallback)
{
	TArray<FSoftObjectPath> Out;
	if (Species)
	{
		if (!Species->SkeletalMesh.IsNull())
		{
			Out.Add(Species->SkeletalMesh.ToSoftObjectPath());
		}
		if (!Species->Mesh.IsNull())
		{
			Out.AddUnique(Species->Mesh.ToSoftObjectPath());
		}
	}
	if (!Fallback.IsNull())
	{
		Out.AddUnique(Fallback.ToSoftObjectPath());
	}
	return Out;
}

USkeletalMesh* ULureFightFishSubsystem::ResolveMesh(const FFishSpeciesRow* Species)
{
	for (const FSoftObjectPath& Path : MeshCandidates(Species, GetDefault<ULureFishVisualSettings>()->FallbackMesh))
	{
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(LoadPath(Path)))
		{
			return Mesh; // a static Mesh (the in-hand / journal mesh) is skipped
		}
	}
	return nullptr;
}

UClass* ULureFightFishSubsystem::ResolveAnimClass()
{
	UClass* Class = Cast<UClass>(LoadPath(GetDefault<ULureFishVisualSettings>()->AnimClass.ToSoftObjectPath()));
	return (Class && Class->IsChildOf(UAnimInstance::StaticClass())) ? Class : UFishAnimInstance::StaticClass();
}

UClass* ULureFightFishSubsystem::ResolveActorClass()
{
	UClass* Class = Cast<UClass>(LoadPath(GetDefault<ULureFishVisualSettings>()->FishActorClass.ToSoftObjectPath()));
	return (Class && Class->IsChildOf(ALureFightFish::StaticClass())) ? Class : ALureFightFish::StaticClass();
}

ALureFightFish* ULureFightFishSubsystem::SpawnFish(ULureFishingComponent& Fishing, const FFightFishView& View)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	Resolve();
	const FFishSpeciesRow* Species = SpeciesTable ? SpeciesTable->FindRow<FFishSpeciesRow>(View.Fish.SpeciesId, TEXT("FightFish"), false) : nullptr;

	FLureFightFishSetup Setup;
	Setup.Row = Row;
	Setup.Fish = View.Fish;
	Setup.FightId = View.FightId;
	if (Species)
	{
		Setup.ReferenceWeightKg = Species->ReferenceWeight;
		Setup.AnimRate = Species->AnimRate;
		Setup.AnimAmplitude = Species->AnimAmplitude;
	}
	else
	{
		UE_LOG(LogLureFish, Verbose, TEXT("Fight fish: species '%s' not found; scale 1 and the fallback mesh."), *View.Fish.SpeciesId.ToString());
	}
	Setup.Mesh = ResolveMesh(Species);
	Setup.AnimClass = Setup.Mesh ? ResolveAnimClass() : nullptr;

	FActorSpawnParameters Params;
	Params.Owner = Fishing.GetOwner();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	const FRotator Facing = (View.LineEnd - View.PlayerLocation).GetSafeNormal2D().Rotation();
	ALureFightFish* Fish = World->SpawnActor<ALureFightFish>(ResolveActorClass(), View.LineEnd, Facing, Params);
	if (Fish)
	{
		Fish->Setup(Setup);
		Fish->ApplyView(View, 0.f); // placed at once (the first view snaps)
	}
	return Fish;
}

void ULureFightFishSubsystem::EndFish(ULureFishingComponent* Fishing, ALureFightFish* Fish, const FFightFishView& View)
{
	if (!Fish)
	{
		return;
	}
	if (View.End == EFightFishEnd::Landed)
	{
		Fish->BeginEnd(EFightFishEnd::Landed, View.PlayerLocation);
		// The hand-off: T-030's hanging fish takes over here (KeepLandedFish, or its own item at Fish's transform).
		LandedOffer = Fish;
		bLandedKept = false;
		const FFishInstance Landed = Fish->GetFish();
		OnFightFishLandedNative.Broadcast(Fishing, Fish, Landed);
		OnFightFishLanded.Broadcast(Fishing, Fish, Landed);
		LandedOffer.Reset();
		if (!bLandedKept && IsValid(Fish))
		{
			Fish->Destroy();
		}
		return;
	}
	Fish->BeginEnd(EFightFishEnd::Escaped, View.PlayerLocation);
	if (Fish->IsFinished())
	{
		Fish->Destroy(); // EscapeTime 0
	}
	else
	{
		Ending.Add(Fish);
	}
}

bool ULureFightFishSubsystem::KeepLandedFish(ALureFightFish* Fish)
{
	if (Fish && LandedOffer.Get() == Fish)
	{
		bLandedKept = true;
		return true;
	}
	return false;
}

void ULureFightFishSubsystem::UpdateVisuals(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Fish swimming away after a lost fight.
	for (int32 Index = Ending.Num() - 1; Index >= 0; --Index)
	{
		ALureFightFish* Fish = Ending[Index].Get();
		if (Fish)
		{
			Fish->TickAfterFight(DeltaTime);
		}
		if (!Fish || Fish->IsFinished())
		{
			if (Fish)
			{
				Fish->Destroy();
			}
			Ending.RemoveAtSwap(Index);
		}
	}

	// Fishing components that left (their pawn was destroyed): their fish go at once.
	for (int32 Index = Active.Num() - 1; Index >= 0; --Index)
	{
		if (!Active[Index].Fishing.IsValid())
		{
			if (ALureFightFish* Fish = Active[Index].Fish.Get())
			{
				Fish->Destroy();
			}
			Active.RemoveAtSwap(Index);
		}
	}

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		ULureFishingComponent* Fishing = It->FindComponentByClass<ULureFishingComponent>();
		if (!Fishing)
		{
			continue;
		}
		const FFightFishView View = FFightFishViewAdapter::FromComponent(*Fishing);
		const int32 Index = Active.IndexOfByPredicate([Fishing](const FEntry& Entry) { return Entry.Fishing.Get() == Fishing; });
		if (Index != INDEX_NONE)
		{
			FEntry& Entry = Active[Index];
			ALureFightFish* Fish = Entry.Fish.Get();
			if (View.bFighting && Fish && Entry.FightId == View.FightId)
			{
				Fish->ApplyView(View, DeltaTime);
				continue;
			}
			// The fight ended, or a new one started before we saw the end (the old fish got away as far as we can tell).
			FFightFishView EndView = View;
			if (View.bFighting)
			{
				EndView.End = EFightFishEnd::Escaped;
			}
			Active.RemoveAtSwap(Index);
			EndFish(Fishing, Fish, EndView);
		}
		if (View.bFighting)
		{
			if (ALureFightFish* Fish = SpawnFish(*Fishing, View))
			{
				FEntry Entry;
				Entry.Fishing = Fishing;
				Entry.Fish = Fish;
				Entry.FightId = View.FightId;
				Active.Add(Entry);
			}
		}
	}
}

ALureFightFish* ULureFightFishSubsystem::FindFish(const ULureFishingComponent* Fishing) const
{
	for (const FEntry& Entry : Active)
	{
		if (Entry.Fishing.Get() == Fishing)
		{
			return Entry.Fish.Get();
		}
	}
	return nullptr;
}

int32 ULureFightFishSubsystem::GetNumFish() const
{
	int32 Count = 0;
	for (const FEntry& Entry : Active)
	{
		Count += Entry.Fish.IsValid() ? 1 : 0;
	}
	for (const TWeakObjectPtr<ALureFightFish>& Fish : Ending)
	{
		Count += Fish.IsValid() ? 1 : 0;
	}
	return Count;
}

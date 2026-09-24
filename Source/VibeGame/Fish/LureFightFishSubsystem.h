// Lure: spawns and removes the fighting fish (T-029) on every machine except a dedicated server. Spec: docs/specs/fight-fish-visual.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishInstance.h"
#include "LureFightFishSubsystem.generated.h"

class ALureFightFish;
class UDataTable;
class ULureFishingComponent;
class USkeletalMesh;
struct FFishSpeciesRow;

/**
 *  Every machine: a fish was reeled in (its fight's Outcome is Landed). Fish still stands at the line's end playing the
 *  flop; it is destroyed right after this event unless a listener calls KeepLandedFish(Fish) during it (then it is the
 *  listener's). T-030's hanging fish item hooks in here. Fires on clients too (cosmetic; the server's landing is
 *  ULureFishingComponent::OnFishLanded).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLureFightFishLandedSignature, ULureFishingComponent*, Fishing, ALureFightFish*, Fish, const FFishInstance&, Landed);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FLureFightFishLandedNative, ULureFishingComponent* /*Fishing*/, ALureFightFish* /*Fish*/, const FFishInstance& /*Landed*/);

class ULureFightFishSubsystem;

/**
 *  The subsystem's frame update, in TG_LastDemotable: after TG_PostUpdateWork, where ULureFishingComponent steps the fight,
 *  so the fish is placed from this frame's fight state and ends are seen the same frame (T029-B2).
 */
USTRUCT()
struct FLureFightFishTickFunction : public FTickFunction
{
	GENERATED_BODY()

	ULureFightFishSubsystem* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("ULureFightFishSubsystem::UpdateVisuals"); }
	virtual FName DiagnosticContext(bool bDetailed) override { return FName(TEXT("LureFightFishSubsystem")); }
};

template<>
struct TStructOpsTypeTraits<FLureFightFishTickFunction> : public TStructOpsTypeTraitsBase2<FLureFightFishTickFunction>
{
	enum { WithCopy = false };
};

/**
 *  Watches every pawn's ULureFishingComponent through FFightFishViewAdapter (replicated state only, so it works the same
 *  on the server, the owning client and proxies). A new fight (bFighting with a new FightId) spawns one local
 *  ALureFightFish (mesh from DT_FishSpecies SkeletalMesh, scale from weight, tuning from DT_FishVisual); every frame after
 *  the characters tick it moves the fish; when the fight ends the fish is handed off (Landed) or swims away (Escaped).
 */
UCLASS()
class ULureFightFishSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	static ULureFightFishSubsystem* Get(const UObject* WorldContext);

	/** The fish of Fishing's current fight (null if none). */
	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	ALureFightFish* FindFish(const ULureFishingComponent* Fishing) const;

	/** Fish in fights plus fish still swimming away. */
	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	int32 GetNumFish() const;

	/** Call from OnFightFishLanded to keep the landed fish (it is not destroyed; the caller owns it). False outside the event. */
	UFUNCTION(BlueprintCallable, Category="Lure|Fish")
	bool KeepLandedFish(ALureFightFish* Fish);

	UPROPERTY(BlueprintAssignable, Category="Lure|Fish")
	FLureFightFishLandedSignature OnFightFishLanded;

	FLureFightFishLandedNative OnFightFishLandedNative;

	/** One update (the tick function calls it with the frame time, after the fight step). Public for tests. */
	void UpdateVisuals(float DeltaTime);

	/** Tests: species and visual tables to use instead of the settings' (null = the settings' table). Resets the cache. */
	void SetTables(const UDataTable* InSpeciesTable, const UDataTable* InVisualTable);

	/** The DT_FishVisual row in use (the built-in one if the table or row is missing or invalid). */
	const FFishVisualRow& GetVisualRow();

	/** Where the fish mesh comes from, in order: species SkeletalMesh, species Mesh (if it is a skeletal mesh), Fallback. Null paths skipped. */
	static TArray<FSoftObjectPath> MeshCandidates(const FFishSpeciesRow* Species, const TSoftObjectPtr<USkeletalMesh>& Fallback);

	/** The frame update's tick function (registered at world BeginPlay; TG_LastDemotable). Public for tests. */
	const FTickFunction& GetUpdateTickFunction() const { return UpdateTick; }

	// ---- UWorldSubsystem ----
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:

	struct FEntry
	{
		TWeakObjectPtr<ULureFishingComponent> Fishing;
		TWeakObjectPtr<ALureFightFish> Fish;
		uint8 FightId = 0;
	};
	TArray<FEntry> Active;
	FLureFightFishTickFunction UpdateTick;
	TArray<TWeakObjectPtr<ALureFightFish>> Ending;

	/** The fish offered to KeepLandedFish during OnFightFishLanded, and whether it was kept. */
	TWeakObjectPtr<ALureFightFish> LandedOffer;
	bool bLandedKept = false;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> SpeciesTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> VisualTable;

	bool bSpeciesOverride = false;
	bool bVisualOverride = false;
	bool bResolved = false;
	FFishVisualRow Row;

	/** Loaded meshes and classes (kept alive) by path; a path that failed to load maps to null. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> KeepAlive;
	TMap<FSoftObjectPath, TWeakObjectPtr<UObject>> Loaded;
	TSet<FSoftObjectPath> Missing;

	void Resolve();
	UObject* LoadPath(const FSoftObjectPath& Path);
	USkeletalMesh* ResolveMesh(const FFishSpeciesRow* Species);
	UClass* ResolveAnimClass();
	UClass* ResolveActorClass();
	ALureFightFish* SpawnFish(ULureFishingComponent& Fishing, const FFightFishView& View);
	void EndFish(ULureFishingComponent* Fishing, ALureFightFish* Fish, const FFightFishView& View);
};

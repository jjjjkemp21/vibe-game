// Lure: a hot spot on the water (T-027): bubbling or rippling water with better fish. Rules: docs/specs/fishing-water-rules.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/FishingWaterTypes.h"
#include "LureHotSpot.generated.h"

class ULureHotSpotVisualComponent;

/**
 *  One hot spot: a patch of water that wanders slowly (DriftOffset) for a while and gives the bites of a cast that lands in
 *  it a bonus (luck, size, value, a sooner bite; FLureHotSpotBonus, from its DT_HotSpot row).
 *
 *  Server-authoritative: the server spawns it (ALureHotSpotSpawner, or the dev command Lure.HotSpot.Spawn), decides which
 *  casts land in it and destroys it at its end time. It replicates once (FLureHotSpotState: type, anchor, radius, drift,
 *  seed, spawn and end time, look); every machine computes the same drifting center from the synchronized server time, so
 *  no movement is sent. Always relevant (a few per level, tiny state).
 *
 *  The look is a separate, swappable component: the row's VisualClass (a thin Blueprint child of
 *  ULureHotSpotVisualComponent with real VFX), else the C++ placeholder (foam rings and bubbles). None on dedicated servers.
 *  Not placeable by hand: a hot spot only exists with a state (SpawnHotSpot); levels place an ALureHotSpotSpawner.
 */
UCLASS(Blueprintable, NotPlaceable)
class ALureHotSpot : public AActor
{
	GENERATED_BODY()

public:

	ALureHotSpot();

	/**
	 *  Server: spawns a hot spot of DT_HotSpot row TypeId at Anchor (a point on the water surface) that lives Lifetime
	 *  seconds from Now (FLureWaterQuery::GetTime). Class null = ALureHotSpot. Null if the world is missing.
	 */
	static ALureHotSpot* SpawnHotSpot(UWorld* World, TSubclassOf<ALureHotSpot> Class, FName TypeId, const FLureHotSpotRow& Row, FName AreaId,
		const FVector& Anchor, int32 Seed, double Now, float Lifetime);

	/** The replicated state (every machine). */
	const FLureHotSpotState& GetState() const { return State; }

	/** Server: what the bites of a cast that lands in it get. */
	const FLureHotSpotBonus& GetBonus() const { return Bonus; }

	/** Where its center is at Time (the anchor plus the drift). */
	FVector GetCenterAt(double Time) const;

	/** It exists at Time: SpawnTime <= Time < EndTime. */
	bool IsActiveAt(double Time) const;

	/** XY is within Radius of the center at Time, and it is active then. */
	bool ContainsAt(const FVector2D& XY, double Time) const;

	/** 0..1: the visual grows in after it appears and fades out before its end (ULureWaterSettings::HotSpotFadeSeconds). */
	float GetFadeAt(double Time) const;

	/** The synchronized time this hot spot runs on (FLureWaterQuery::GetTime). */
	double GetTime() const;

	ULureHotSpotVisualComponent* GetVisual() const { return Visual; }

	/** Server: sets the state and bonus before FinishSpawning (SpawnHotSpot does; tests may). */
	void InitHotSpot(const FLureHotSpotState& InState, const FLureHotSpotBonus& InBonus);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Lure|Hot Spot")
	FLureHotSpotState State;

	UFUNCTION()
	void OnRep_State();

private:

	/** Server only (not replicated). */
	UPROPERTY(Transient)
	FLureHotSpotBonus Bonus;

	UPROPERTY(VisibleAnywhere, Category="Lure|Hot Spot")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<ULureHotSpotVisualComponent> Visual;

	void EnsureVisual();
	void UpdateLocation();
};

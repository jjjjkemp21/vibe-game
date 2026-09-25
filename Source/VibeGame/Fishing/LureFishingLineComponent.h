// Lure: the fishing line (T-006 look, T-032 physics). Cosmetic, on every machine that renders. Spec: docs/specs/fishing-line.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Fishing/FishingLineSim.h"
#include "Fishing/FishingLineTypes.h"
#include "LureFishingLineComponent.generated.h"

class AActor;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;

/** Gives a world point on demand (the rod tip where the rod is drawn this frame). */
DECLARE_DELEGATE_RetVal(FVector, FLureLinePointProvider);

/**
 *  One drawn piece of the fishing line: a spline mesh that never has a physics state (it is only drawn). Without this, moving a
 *  spline mesh every frame rebuilds its collision body in worlds with trace collision (editor-created and test worlds, even with
 *  NoCollision): ~1 ms per segment. PIE and packaged games don't enable that, but a drawn-only line never needs physics anyway.
 */
UCLASS(ClassGroup=(Lure))
class ULureLineSegmentComponent : public USplineMeshComponent
{
	GENERATED_BODY()

public:

	virtual bool ShouldCreatePhysicsState() const override { return false; }
};

/** What the line's end does. */
UENUM(BlueprintType)
enum class ELureLineMode : uint8
{
	/** No line: hidden and not simulated (no tick). */
	None,
	/** The end is pinned to the owner's end point (the bobber, or the fish in a fight). */
	Pinned,
	/** An actor hangs from the free end and swings (the landed fish, T-030). */
	Hanging,
	/** The line snapped: the free end whips back toward the rod, then the line is gone. */
	Recoil
};

/**
 *  The fishing line: a simulated rope (FLureLineSim) drawn as a chain of spline-mesh segments (a thin mesh along Z, e.g. the
 *  engine cylinder). It sags when slack, lies on the water, pulls straight with tension (fully straight at the snap threshold),
 *  whips back when it snaps and can carry a hanging actor. The width is set per point from the viewer's distance so it shows
 *  at least LinePixelWidth pixels on a 1080p screen at any distance (designer B-S3).
 *
 *  Cosmetic only: every machine simulates its own line from replicated state (ends, tension, water); nothing here affects
 *  gameplay or replicates. Cost: nothing while no line is out (the tick is off); see docs/specs/fishing-line.md for numbers.
 *
 *  Use (the owner, every frame while its line is out, any time before the line ticks):
 *      SetEndpoints(RodTip, End); SetTension(Tension01); optionally SetSlack, SetWaterSurfaceZ, SetViewer, SetWidthRule.
 *  and Hide() when the line comes in. The line simulates and redraws once per frame in its own tick (TG_PostUpdateWork: after
 *  the camera and animation), so with SetStartProvider bound it starts exactly at the rod tip as drawn this frame.
 *  Other features: Snap() (recoil), AttachEndActor/DetachEndActor (a hanging, swinging actor), GetEndPoint/GetEndDirection.
 *  The T-006 one-call API (SetLine) still works.
 */
UCLASS(ClassGroup=(Lure))
class ULureFishingLineComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	ULureFishingLineComponent();

	// ---- Setup ----

	/**
	 *  Mesh and material of the segments (the material gets its "Color" vector parameter set to Color) and the fixed segment count
	 *  (DT_Fishing LineSegments; clamped to [1, 64]). Allocates everything the line needs, once. Without a mesh the line is still
	 *  simulated (tests), just not drawn.
	 */
	void Setup(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FLinearColor& Color, int32 Segments);

	/** Physics tuning (a DT_FishingLine row). Default: the settings' FishingLineTable row, else the built-in row. False if Row is invalid (the built-in row is used). */
	bool SetTuning(const FLureFishingLineRow& Row);

	const FLureFishingLineRow& GetTuning() const;

	/** The built-in tuning is in use (DT_FishingLine missing or its row invalid). */
	bool IsUsingFallbackTuning() const;

	/**
	 *  When bound, the line asks it for the rod tip each time it simulates (after the camera and animation moved), so the line
	 *  never lags behind or cuts through the rod when the view turns; SetEndpoints' tip is then only the fallback.
	 */
	void SetStartProvider(FLureLinePointProvider Provider);

	// ---- Every frame while the line is out ----

	/** Where the line leaves the rod and where it ends (bobber, fish). The first call starts the line. While an actor hangs, End waits. */
	void SetEndpoints(const FVector& RodTip, const FVector& End);

	/** Line tension, 0..1 of the line's strength (1 = the snap threshold: fully straight). Out-of-range values are clamped (+Inf = 1); NaN = 0. */
	void SetTension(float Tension01);

	/** Extra line with no tension, as a share of the straight distance (< 0 = the tuning's SlackShare). */
	void SetSlack(float SlackShare);

	/** The water surface under the line, cm. Without it the line looks it up itself (water volumes, Lure.Water, the fallback sea level). */
	void SetWaterSurfaceZ(float WaterZ);
	void ClearWaterSurfaceZ();

	/** Who looks at the line (for the per-point width). Default: the local player's camera. */
	void SetViewer(const FVector& ViewLocation, float HorizontalFovDeg);

	/** Width rule: PixelWidth px at each point's distance on a ReferenceScreenWidth-wide view, at least MinWidth cm (DT_Fishing). */
	void SetWidthRule(float PixelWidth, float ReferenceScreenWidth, float MinWidth);

	/**
	 *  T-006 API (kept so older callers still work): SetViewer + SetWidthRule + SetEndpoints, with Sag (a slack line's sag as a
	 *  share of its length) turned into the matching slack: a line that sags Sag x length is about 8/3 x Sag^2 longer.
	 */
	void SetLine(const FVector& Start, const FVector& End, float Sag, const FVector& ViewLocation, float HorizontalFovDeg,
		float PixelWidth, float ReferenceScreenWidth, float MinWidth);

	/** The line comes in: hidden now and no longer simulated. A playing snap recoil finishes first; a hanging actor stays until DetachEndActor. */
	void Hide();

	// ---- Events ----

	/** The line snapped: the end lets go and the line whips back toward the rod tip, falls, and is gone after RecoilTime. */
	void Snap();

	bool IsRecoiling() const { return Mode == ELureLineMode::Recoil; }

	// ---- A hanging actor (the landed fish) ----

	/**
	 *  Hangs Actor from the line's end: the end is let go and swings under the rod tip on HangLength cm of line (a longer line is
	 *  reeled up smoothly, a shorter one drops). Each frame the actor is moved so HookOffset (actor space: the mouth) sits on the
	 *  end; with bOrientAlongLine its +X axis points up the line (head up) and it keeps its facing, or with bFaceViewer too it
	 *  turns about the line so its +Y side faces the viewer (SetViewer, else the local camera), smoothed by HangFaceTime (T-043,
	 *  FLureFishingLineRules::SideOnHangRotation). The actor should not replicate its movement (every machine swings its own).
	 *  Starts the line if it was not out.
	 */
	void AttachEndActor(AActor* Actor, float HangLength, const FVector& HookOffset = FVector::ZeroVector, bool bOrientAlongLine = true,
		bool bFaceViewer = false);

	/** Lets the actor go (it stays where it is). The line goes back to its pinned end if it is still out, else it is gone. */
	void DetachEndActor();

	AActor* GetEndActor() const;

	/** Adds a velocity (cm/s) to a free end: a fish flopping, a kick. */
	void AddEndVelocity(const FVector& Velocity);

	// ---- Queries ----

	/** The line's end as simulated now: the pinned end, or where the hanging actor's hook is. */
	FVector GetEndPoint() const;

	/** Unit vector from the end up the line (toward the rod). */
	FVector GetEndDirection() const;

	/** The line's first point (the rod tip it was drawn from). */
	FVector GetStartPoint() const;

	/** Line length now and the length it moves toward, cm. */
	float GetRestLength() const { return RestLength; }
	float GetTargetRestLength() const { return TargetRestLength; }

	float GetTension() const { return Tension; }

	ELureLineMode GetMode() const { return Mode; }

	/** The line is out (being simulated and drawn). */
	bool IsLineVisible() const { return bLineVisible; }

	/** The points drawn last (Segments + 1), world. */
	const TArray<FVector>& GetPoints() const { return Sim.GetPoints(); }

	/** The widths drawn last, cm (one per point). */
	const TArray<float>& GetWidths() const { return Widths; }

	int32 GetNumSegments() const { return Sim.GetNumSegments(); }

	const FLureLineSim& GetSimulation() const { return Sim; }

	/** The solids the line collides with, as last gathered (T-032b; empty while an actor hangs). */
	const FLureLineColliders& GetColliders() const { return Colliders; }

	/** Simulates DeltaTime and redraws. The tick calls it once per frame; tests may call it directly. */
	void UpdateLine(float DeltaTime);

	// ---- UActorComponent ----

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:

	// Look.
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> Segments;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material;

	/** Diameter of Mesh across its long axis, cm (the engine cylinder: 100). */
	float MeshDiameter = 100.f;

	TArray<float> Widths;
	float PixelWidth = 2.5f;
	float ReferenceScreenWidth = 1920.f;
	float MinWidth = 0.15f;
	FVector ViewLocation = FVector::ZeroVector;
	float ViewFovDeg = 90.f;
	bool bViewerSet = false;
	bool bLineVisible = false;

	// Simulation.
	FLureLineSim Sim;
	mutable FLureFishingLineRow Tuning;
	mutable bool bTuningResolved = false;
	mutable bool bFallbackTuning = true;
	ELureLineMode Mode = ELureLineMode::None;
	/** The owner fed endpoints since the last Hide. */
	bool bOut = false;
	bool bNeedsReset = true;
	FVector TipInput = FVector::ZeroVector;
	FVector EndInput = FVector::ZeroVector;
	FLureLinePointProvider StartProvider;
	float Tension = 0.f;
	float Slack = -1.f;
	float RestLength = 0.f;
	float TargetRestLength = 0.f;
	/** Straight tip-to-end distance of the last pinned frame (0 = none: a new line, or not pinned then); CarryRestLength input. */
	float LastChord = 0.f;

	// Water.
	bool bWaterOverride = false;
	float WaterOverrideZ = 0.f;
	bool bWaterLookedUp = false;
	bool bLookedUpHasWater = false;
	float LookedUpWaterZ = 0.f;
	FVector2D LookupXY = FVector2D::ZeroVector;

	// Snap.
	float RecoilElapsed = 0.f;
	float RecoilStartLength = 0.f;

	// Hanging actor.
	TWeakObjectPtr<AActor> EndActor;
	float HangLength = 100.f;
	FVector HookOffset = FVector::ZeroVector;
	bool bOrientEndActor = true;
	/** T-043: the hanging actor turns its +Y side to the viewer (with bOrientEndActor). */
	bool bEndActorFacesViewer = false;

	// Collision (T-032b): the line lies on and bends around what blocks a cast (docs/specs/fishing-line.md "Collision").
	FLureLineColliders Colliders;
	TArray<FOverlapResult> Overlaps;
	TArray<FPlane> PlaneScratch;
	/** Where the solids were gathered: the line's box grown by CollisionQueryMargin. */
	FBox QueryBox = FBox(ForceInit);
	/** Seconds since the last gather. */
	float CollisionClock = 0.f;
	bool bCollidersValid = false;
	/** A gathered solid is Movable: it is gathered again every CollisionRefreshTime. */
	bool bMovableColliders = false;

	/** Reserves the gather's memory once, so a line in the same place never allocates. */
	void ReserveColliders();
	/** Gathers the solids again when the line (tip, end, points) nears the edge of QueryBox, or a movable one's refresh is due. */
	void UpdateColliders(const FVector& Tip, const FVector& End, float DeltaTime);
	/** Adds Component's simple collision shapes (instance Item of an instanced mesh), or its bounds without any. */
	void AddCollidersOf(UPrimitiveComponent& Component, int32 Item);

	void ResolveTuning() const;
	void StartLine(ELureLineMode NewMode);
	void StopLine();
	FVector ResolveTip() const;
	FVector HookPointOf(const AActor& Actor) const;
	bool ResolveWater(const FVector& Near, float& OutWaterZ);
	void Simulate(float DeltaTime);
	void MoveEndActor(float DeltaTime) const;
	void Draw();
	void ResolveViewer(FVector& OutLocation, float& OutFovDeg) const;
	void DestroySegments();
};

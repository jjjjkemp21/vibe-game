// Lure: the fishing line (T-006). Cosmetic, on every machine that renders.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LureFishingLineComponent.generated.h"

class UMaterialInterface;
class USplineMeshComponent;
class UStaticMesh;

/**
 *  A sagging line drawn as a chain of spline-mesh segments (a thin mesh along Z, e.g. the engine cylinder).
 *  The width is set per point from the viewer's distance, so it shows at least LinePixelWidth pixels on a 1080p screen
 *  at any distance (designer B-S3), without looking like a rope up close.
 *  Uses absolute world transforms; the owner calls SetLine every frame while the line is out and Hide otherwise.
 */
UCLASS(ClassGroup=(Lure))
class ULureFishingLineComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	ULureFishingLineComponent();

	/** Mesh and material of the segments (material gets its "Color" vector parameter set to Color). */
	void Setup(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FLinearColor& Color, int32 Segments);

	/**
	 *  Shows the line from Start to End with Sag (share of the length). Widths: PixelWidth pixels at each point's distance from
	 *  ViewLocation, for a ReferenceScreenWidth-wide view with HorizontalFovDeg, at least MinWidth cm.
	 */
	void SetLine(const FVector& Start, const FVector& End, float Sag, const FVector& ViewLocation, float HorizontalFovDeg,
		float PixelWidth, float ReferenceScreenWidth, float MinWidth);

	void Hide();

	bool IsLineVisible() const { return bLineVisible; }

	/** The points drawn last (Segments + 1), world. */
	const TArray<FVector>& GetPoints() const { return Points; }

	/** The widths drawn last, cm (one per point). */
	const TArray<float>& GetWidths() const { return Widths; }

	int32 GetNumSegments() const { return Segments.Num(); }

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> Segments;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material;

	/** Diameter of Mesh across its long axis, cm (the engine cylinder: 100). */
	float MeshDiameter = 100.f;

	TArray<FVector> Points;
	TArray<float> Widths;
	bool bLineVisible = false;

	void DestroySegments();
};

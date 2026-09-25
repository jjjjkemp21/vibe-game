// T-041e (unreal-engineer): the fishing line draws with the project material /Game/Materials/M_FishingLine (Responsive AA on,
// so TSR leaves no ghost loop behind a fast-turning thin line). Project.Fishing.Line.Material
// Spec: docs/specs/fishing-line.md ("Look").

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LurePlayerCharacter.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace LureLineMaterialTest
{
	const TCHAR* const ProjectLineMaterial = TEXT("/Game/Materials/M_FishingLine.M_FishingLine");

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLineMaterial, "Project.Fishing.Line.Material", LCT::Flags)
	bool FLineMaterial::RunTest(const FString& Parameters)
	{
		// The setting (class default and the live settings: no Config/*.ini override).
		const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
		TestEqual(TEXT("LineMaterial is the project material"), Settings->LineMaterial.ToSoftObjectPath().ToString(), FString(ProjectLineMaterial));

		// It loads, it is the material itself, and Responsive AA is on.
		UMaterialInterface* Loaded = Settings->LoadLineMaterial();
		if (!TestNotNull(TEXT("the line material loads"), Loaded))
		{
			return false;
		}
		TestEqual(TEXT("... the project material, not the fallback"), Loaded->GetPathName(), FString(ProjectLineMaterial));
		const UMaterial* Base = Loaded->GetMaterial();
		TestTrue(TEXT("M_FishingLine has Responsive AA on"), Base && Base->bEnableResponsiveAA);
		TestTrue(TEXT("M_FishingLine is usable with spline meshes"), Base && Base->bUsedWithSplineMeshes);

		// A cast line's drawn segments use it, with Color = LineColor.
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Player = W.SpawnPlayer(*this, FVector(-500.0f, 0.0f, LCT::DockTop));
		ULureFishingComponent* Fishing = Player ? Player->GetFishing() : nullptr;
		if (!TestNotNull(TEXT("fishing"), Fishing))
		{
			return false;
		}
		W.Tick(20);
		TestTrue(TEXT("cast out to sea"), Fishing->AuthorityCast(0.6f, 180.0f));
		W.Tick(120);
		TestTrue(TEXT("the line is out and drawn"), Fishing->GetLine() && Fishing->GetLine()->IsLineVisible());

		TInlineComponentArray<ULureLineSegmentComponent*> Segments;
		Player->GetComponents(Segments);
		int32 Checked = 0;
		for (const ULureLineSegmentComponent* Segment : Segments)
		{
			const UMaterialInterface* Used = Segment->GetMaterial(0);
			const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Used);
			TestTrue(FString::Printf(TEXT("%s uses a dynamic instance of M_FishingLine (%s)"), *Segment->GetName(), *GetPathNameSafe(Used)),
				Dynamic && Dynamic->Parent == Loaded);
			FLinearColor Color;
			TestTrue(FString::Printf(TEXT("%s: Color = LineColor"), *Segment->GetName()),
				Dynamic && Dynamic->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), Color) && Color.Equals(Settings->LineColor, 1.0e-3f));
			++Checked;
		}
		TestTrue(FString::Printf(TEXT("the line has segments (%d)"), Checked), Checked > 0);
		return true;
	}
}

#endif

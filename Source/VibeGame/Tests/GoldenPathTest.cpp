#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGoldenPathCrateImportedTest,
	"Project.GoldenPath.CrateImported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGoldenPathCrateImportedTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Props/SM_GoldenCrate.SM_GoldenCrate"));
	if (!TestNotNull(TEXT("SM_GoldenCrate loads"), Mesh))
	{
		return false;
	}
	const FVector Extent = Mesh->GetBounds().BoxExtent;
	TestTrue(TEXT("Crate X extent is ~50 uu (1 m wide)"), FMath::IsNearlyEqual(Extent.X, 50.0, 2.0));
	TestTrue(TEXT("Crate Y extent is ~50 uu (1 m deep)"), FMath::IsNearlyEqual(Extent.Y, 50.0, 2.0));
	TestTrue(TEXT("Crate Z extent is ~50 uu (1 m tall)"), FMath::IsNearlyEqual(Extent.Z, 50.0, 2.0));
	return true;
}

#endif

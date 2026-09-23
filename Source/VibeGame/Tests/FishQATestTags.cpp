// QA-owned test-only gameplay tags for Project.Fish.QA.* (approved in docs/specs/fish-system-rules.md, Q33).
// Native tags, compiled only with automation tests, so fixtures can use habitats, baits, regions, weather, families
// and one extra stat without touching Config/Tags/FishTags.ini.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "NativeGameplayTags.h"

namespace FishQATags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(StatQA, "Fish.Stat.QA_TestOnly");

	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH0, "Test.Fish.Habitat.H0");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH1, "Test.Fish.Habitat.H1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH2, "Test.Fish.Habitat.H2");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH3, "Test.Fish.Habitat.H3");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH4, "Test.Fish.Habitat.H4");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH5, "Test.Fish.Habitat.H5");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH6, "Test.Fish.Habitat.H6");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH7, "Test.Fish.Habitat.H7");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH8, "Test.Fish.Habitat.H8");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(HabitatH9, "Test.Fish.Habitat.H9");

	UE_DEFINE_GAMEPLAY_TAG_STATIC(BaitA, "Test.Fish.Bait.A");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(BaitAChild, "Test.Fish.Bait.A.Child");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(BaitB, "Test.Fish.Bait.B");

	UE_DEFINE_GAMEPLAY_TAG_STATIC(RegionR1, "Test.Fish.Region.R1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RegionR1Sub, "Test.Fish.Region.R1.Sub");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RegionR2, "Test.Fish.Region.R2");

	UE_DEFINE_GAMEPLAY_TAG_STATIC(WeatherSun, "Test.Fish.Weather.Sun");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(WeatherRain, "Test.Fish.Weather.Rain");

	UE_DEFINE_GAMEPLAY_TAG_STATIC(FamilyF1, "Test.Fish.Family.F1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(FamilyF1Sub, "Test.Fish.Family.F1.Sub");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(FamilyF2, "Test.Fish.Family.F2");
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure T-027 (unreal-engineer): hot spots - drift, data rules, the bonus in the roll, capture at landing, the spawner, the
// look, replication and the dev commands. Project.Fishing.Water.{HotSpot,Net,Net2P,Dev}.* - spec: docs/specs/fishing-water-rules.md.

#include "Tests/Fishing/FishingWaterTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Dev/LureWaterDevCommands.h"
#include "EngineUtils.h"
#include "Fishing/LureHotSpotSpawner.h"
#include "Fishing/LureHotSpotVisualComponent.h"
#include "HAL/IConsoleManager.h"
#include "Net/RepLayout.h"
#include "Net/UnrealNetwork.h"
#include "UObject/CoreNet.h"

#if WITH_EDITOR
#include "Tests/NetTestHelpers.h"
#endif

namespace LureWaterTest
{

namespace HotSpotLocal
{
	/** Copies a struct property from Source to Target through the engine's replication layout (the wire format). Bits, or -1. */
	int64 NetCopy(FAutomationTestBase& Test, const FStructProperty* Property, UObject* Source, UObject* Target)
	{
		const TSharedPtr<FRepLayout> Layout = FRepLayout::CreateFromStruct(Property->Struct, nullptr, ECreateRepLayoutFlags::None);
		if (!Layout.IsValid())
		{
			Test.AddError(TEXT("no FRepLayout for ") + Property->Struct->GetName());
			return -1;
		}
		FNetBitWriter Writer(nullptr, 64 * 1024 * 8);
		bool bUnmapped = false;
		Layout->SerializePropertiesForStruct(Property->Struct, Writer, nullptr, Property->ContainerPtrToValuePtr<void>(Source), bUnmapped);
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		Layout->SerializePropertiesForStruct(Property->Struct, Reader, nullptr, Property->ContainerPtrToValuePtr<void>(Target), bUnmapped);
		if (Writer.IsError() || Reader.IsError())
		{
			Test.AddError(TEXT("the wire round trip failed for ") + Property->GetName());
			return -1;
		}
		return Writer.GetNumBits();
	}

	/** Spawns a hot spot of Row as type TypeId at XY on the sea (z = 0), living Lifetime s from now. */
	ALureHotSpot* SpawnAt(FWaterWorld& World, FName TypeId, const FLureHotSpotRow& Row, const FVector2D& XY, int32 Seed, float Lifetime, FName AreaId = NAME_None)
	{
		return ALureHotSpot::SpawnHotSpot(World.World, nullptr, TypeId, Row, AreaId, FVector(XY.X, XY.Y, 0.0), Seed, World.Now(), Lifetime);
	}

	bool IsGone(const TWeakObjectPtr<ALureHotSpot>& HotSpot)
	{
		return !HotSpot.IsValid() || HotSpot->IsActorBeingDestroyed();
	}

	/** A spawner test row: explicit numbers, no drift unless asked. */
	FLureHotSpotRow SpawnRow(float SpawnInterval, int32 MaxPerArea, std::initializer_list<const TCHAR*> Habitats, float MinDepth = 60.f)
	{
		FLureHotSpotRow Row;
		Row.SpawnInterval = SpawnInterval;
		Row.MaxPerArea = MaxPerArea;
		Row.MinSpacing = 300.f;
		Row.Radius = 200.f;
		Row.LifetimeMin = 20.f;
		Row.LifetimeMax = 30.f;
		Row.DriftSpeed = 0.f;
		Row.DriftRange = 0.f;
		Row.MinDepth = MinDepth;
		Row.MaxDepth = 0.f;
		Row.AllowedHabitats.Reset();
		for (const TCHAR* Habitat : Habitats)
		{
			Row.AllowedHabitats.Add(Tag(Habitat));
		}
		return Row;
	}

	UDataTable* MakeTable(const TArray<TPair<FName, FLureHotSpotRow>>& Rows)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FLureHotSpotRow::StaticStruct();
		for (const TPair<FName, FLureHotSpotRow>& Row : Rows)
		{
			Table->AddRow(Row.Key, Row.Value);
		}
		return Table;
	}

	ALureHotSpotSpawner* MakeSpawner(FWaterWorld& World, const UDataTable* Table, int32 Seed)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureHotSpotSpawner* Spawner = World.World->SpawnActor<ALureHotSpotSpawner>(ALureHotSpotSpawner::StaticClass(), FTransform::Identity, Params);
		if (Spawner)
		{
			Spawner->SetAutoSpawn(false);
			Spawner->SetHotSpotTable(Table);
			Spawner->SetRandomSeed(Seed);
		}
		return Spawner;
	}

	/**
	 *  The spawner scene: the sea 5 m deep over x in [-1000, 11000], y in [-6000, 6000] (a seabed at -500), a 40 cm island at
	 *  (5000, 0), a reef circle (r 3000) around it and a lagoon circle (r 1000) to the north-east.
	 */
	bool MakeSpawnerScene(FAutomationTestBase& Test, FWaterWorld& World)
	{
		if (!World.Create(Test))
		{
			return false;
		}
		World.AddSeabed(-500.f, FVector2D(5000.0, 0.0), 6000.f);
		World.AddBox(FVector(5000.f, 0.f, 20.f), FVector(400.f, 400.f, 20.f)); // the island (land)
		World.AddCircleArea(TEXT("qa_reef"), TEXT("Habitat.Reef"), FVector2D(5000.0, 0.0), 3000.f);
		World.AddCircleArea(TEXT("qa_lagoon"), TEXT("Habitat.Lagoon"), FVector2D(5000.0, 4800.0), 1000.f);
		return true;
	}
}

// =====================================================================================================================
// Pure rules
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotDrift, "Project.Fishing.Water.HotSpot.DriftBoundedAndSmooth", LureWaterTest::Flags)
bool FLureHotSpotDrift::RunTest(const FString& Parameters)
{
	// The wander never leaves DriftRange, moves at DriftSpeed on average (RMS), is smooth, and is the same for the same seed.
	const float Range = 400.f;
	const float Speed = 15.f;
	const double Step = 0.25;
	for (int32 Seed = 1; Seed <= 40; ++Seed)
	{
		double MaxDistance = 0.0;
		double MaxStep = 0.0;
		double SumSquares = 0.0;
		int32 Samples = 0;
		FVector2D Previous = FLureWaterRules::DriftOffset(Range, Speed, Seed, 0.0);
		for (double Age = Step; Age <= 1200.0; Age += Step)
		{
			const FVector2D Offset = FLureWaterRules::DriftOffset(Range, Speed, Seed, Age);
			MaxDistance = FMath::Max(MaxDistance, Offset.Size());
			const double Moved = FVector2D::Distance(Offset, Previous);
			MaxStep = FMath::Max(MaxStep, Moved);
			SumSquares += FMath::Square(Moved / Step);
			++Samples;
			Previous = Offset;
		}
		const double Rms = FMath::Sqrt(SumSquares / Samples);
		if (MaxDistance > Range + 1.0e-3 || FMath::Abs(Rms - Speed) > 0.1 * Speed || MaxStep > Speed * Step * 3.0)
		{
			AddError(FString::Printf(TEXT("seed %d: farthest %.2f cm (range %.0f), RMS speed %.2f cm/s (want %.0f), biggest step %.2f cm"), Seed, MaxDistance, Range, Rms, Speed, MaxStep));
		}
	}
	TestTrue(TEXT("it appears at its anchor (offset 0 at age 0)"), FLureWaterRules::DriftOffset(Range, Speed, 7, 0.0).IsNearlyZero(1.0e-9));
	TestTrue(TEXT("the same inputs give the same offset"), FLureWaterRules::DriftOffset(Range, Speed, 7, 123.4).Equals(FLureWaterRules::DriftOffset(Range, Speed, 7, 123.4), 0.0));
	TestFalse(TEXT("another seed wanders elsewhere"), FLureWaterRules::DriftOffset(Range, Speed, 7, 123.4).Equals(FLureWaterRules::DriftOffset(Range, Speed, 8, 123.4), 1.0));
	TestTrue(TEXT("range 0: it stays put"), FLureWaterRules::DriftOffset(0.f, Speed, 7, 50.0).IsZero());
	TestTrue(TEXT("speed 0: it stays put"), FLureWaterRules::DriftOffset(Range, 0.f, 7, 50.0).IsZero());
	TestTrue(TEXT("NaN age: no motion"), FLureWaterRules::DriftOffset(Range, Speed, 7, std::numeric_limits<double>::quiet_NaN()).IsZero());
	TestTrue(TEXT("NaN range: no motion"), FLureWaterRules::DriftOffset(std::numeric_limits<float>::quiet_NaN(), Speed, 7, 50.0).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotRowRules, "Project.Fishing.Water.HotSpot.RowRules", LureWaterTest::Flags)
bool FLureHotSpotRowRules::RunTest(const FString& Parameters)
{
	// Spawn chance, lifetime, the bonus a cast gets, where a type may appear, and the row validation.
	FLureHotSpotRow Row;
	Row.SpawnInterval = 40.f;
	TestNearlyEqual(TEXT("spawn chance in 1 s with a 40 s interval"), FLureWaterRules::SpawnChance(Row, 1.f), 1.f - FMath::Exp(-1.f / 40.f), 1.0e-6f);
	TestNearlyEqual(TEXT("spawn chance over 45 s (the prewarm)"), FLureWaterRules::SpawnChance(Row, 45.f), 1.f - FMath::Exp(-45.f / 40.f), 1.0e-6f);
	TestEqual(TEXT("no time: no chance"), FLureWaterRules::SpawnChance(Row, 0.f), 0.f);
	Row.SpawnInterval = 0.f;
	TestEqual(TEXT("interval 0: never"), FLureWaterRules::SpawnChance(Row, 100.f), 0.f);
	Row.LifetimeMin = 120.f;
	Row.LifetimeMax = 240.f;
	TestEqual(TEXT("lifetime at U 0"), FLureWaterRules::LifetimeFromRoll(Row, 0.f), 120.f);
	TestEqual(TEXT("lifetime at U 1"), FLureWaterRules::LifetimeFromRoll(Row, 1.f), 240.f);
	TestEqual(TEXT("lifetime at U NaN"), FLureWaterRules::LifetimeFromRoll(Row, std::numeric_limits<float>::quiet_NaN()), 120.f);

	FLureHotSpotRow Bad;
	Bad.LuckBonus = std::numeric_limits<float>::quiet_NaN();
	Bad.SizeBonus = 2.f;
	Bad.ValueMultiplier = 0.f;
	Bad.BiteWaitScale = -1.f;
	const FLureHotSpotBonus Bonus = FLureWaterRules::BonusFromRow(TEXT("QA"), Bad);
	TestTrue(TEXT("the bonus is sanitized (NaN luck 0, size <= 1, value and wait scale > 0)"), Bonus.TypeId == TEXT("QA") && Bonus.LuckBonus == 0.f && Bonus.SizeBonus == 1.f
		&& Bonus.ValueMultiplier == 1.f && Bonus.BiteWaitScale == 1.f);

	FLureHotSpotRow Where;
	Where.MinDepth = 60.f;
	Where.MaxDepth = 400.f;
	Where.AllowedHabitats = { Tag(TEXT("Habitat.Reef")) };
	TestTrue(TEXT("reef, 100 cm"), Where.AllowsWater(Tag(TEXT("Habitat.Reef")), 100.f));
	TestTrue(TEXT("a reef child (Habitat.Reef.Edge)"), Where.AllowsWater(Tag(TEXT("Habitat.Reef.Edge")), 100.f));
	TestFalse(TEXT("lagoon"), Where.AllowsWater(Tag(TEXT("Habitat.Lagoon")), 100.f));
	TestFalse(TEXT("59 cm is too shallow"), Where.AllowsWater(Tag(TEXT("Habitat.Reef")), 59.f));
	TestFalse(TEXT("400 cm is too deep (exclusive)"), Where.AllowsWater(Tag(TEXT("Habitat.Reef")), 400.f));
	Where.AllowedHabitats.Reset();
	TestTrue(TEXT("no habitat list: any water"), Where.AllowsWater(Tag(TEXT("Habitat.Lagoon")), 100.f));

	TestEqual(TEXT("the default (Bubbles) row is valid"), FLureHotSpotRow().Validate(TEXT("Bubbles")).Num(), 0);
	struct FCase
	{
		const TCHAR* What;
		TFunction<void(FLureHotSpotRow&)> Break;
	};
	const FCase Cases[] = {
		{ TEXT("radius 0"), [](FLureHotSpotRow& R) { R.Radius = 0.f; } },
		{ TEXT("lifetime min > max"), [](FLureHotSpotRow& R) { R.LifetimeMin = 50.f; R.LifetimeMax = 10.f; } },
		{ TEXT("size bonus 1.5"), [](FLureHotSpotRow& R) { R.SizeBonus = 1.5f; } },
		{ TEXT("value multiplier 0"), [](FLureHotSpotRow& R) { R.ValueMultiplier = 0.f; } },
		{ TEXT("NaN luck"), [](FLureHotSpotRow& R) { R.LuckBonus = std::numeric_limits<float>::quiet_NaN(); } },
		{ TEXT("negative spacing"), [](FLureHotSpotRow& R) { R.MinSpacing = -1.f; } },
		{ TEXT("max depth under min depth"), [](FLureHotSpotRow& R) { R.MinDepth = 100.f; R.MaxDepth = 50.f; } },
		{ TEXT("drift speed without a range"), [](FLureHotSpotRow& R) { R.DriftSpeed = 10.f; R.DriftRange = 0.f; } },
		{ TEXT("negative max per area"), [](FLureHotSpotRow& R) { R.MaxPerArea = -1; } },
		{ TEXT("no HUD text"), [](FLureHotSpotRow& R) { R.HudText = FText::GetEmpty(); } },
		{ TEXT("a non-habitat tag"), [](FLureHotSpotRow& R) { R.AllowedHabitats = { Tag(TEXT("Region.Tropical")) }; } },
		{ TEXT("an empty tag"), [](FLureHotSpotRow& R) { R.AllowedHabitats = { FGameplayTag() }; } },
	};
	for (const FCase& Case : Cases)
	{
		FLureHotSpotRow Broken;
		Case.Break(Broken);
		TestTrue(FString::Printf(TEXT("validation catches: %s"), Case.What), Broken.Validate(TEXT("QA")).Num() > 0);
	}
	return true;
}

// =====================================================================================================================
// A cast in a hot spot
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotBonusReachesRoll, "Project.Fishing.Water.HotSpot.BonusReachesTheRoll", LureWaterTest::Flags)
bool FLureHotSpotBonusReachesRoll::RunTest(const FString& Parameters)
{
	// With a fixed seed, a cast that lands in a hot spot rolls the same bite as without it, except for the documented bonuses:
	// luck + LuckBonus, SizeBonus, ValueMultiplier (the fish is still exactly PickSpecies + Roll of that context), and the wait
	// scaled by BiteWaitScale. The HUD says so; the type replicates in the net state.
	using namespace HotSpotLocal;
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, World.Spawn(*this), QuickProfile(2.f), 12.f, 777);
	if (!Fishing)
	{
		return false;
	}
	World.Tick(10);

	// Control: no hot spot.
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	const FVector Rest = Fishing->GetNetState().BobberRest;
	const double ControlWait = Fishing->GetScheduledBiteTime() - Fishing->GetNetState().StateStartTime;
	TestTrue(TEXT("control: no hot spot"), Fishing->GetNetState().Water.HotSpotType.IsNone() && !Fishing->GetHotSpotBonus().IsActive());
	if (!TestTrue(TEXT("control: a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240)))
	{
		return false;
	}
	const FFishInstance ControlFish = Fishing->GetPendingFish();
	const FFishRollContext ControlContext = Fishing->GetLastRollContext();
	Fishing->AuthorityReelIn();
	World.Tick(2);

	// The same cast (same seed), now into a hot spot centered where the bobber lands.
	ALureHotSpot* HotSpot = SpawnAt(World, TEXT("Bubbles"), TestHotSpotRow(600.f, 3.f, 0.5f, 2.f, 0.5f), FVector2D(Rest.X, Rest.Y), 11, 300.f);
	if (!TestNotNull(TEXT("the hot spot spawns"), HotSpot))
	{
		return false;
	}
	Fishing->SetRandomSeed(777);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestTrue(TEXT("the same landing point"), Fishing->GetNetState().BobberRest.Equals(Rest, 0.5));
	TestEqual(TEXT("replicated: the hot spot type"), Fishing->GetNetState().Water.HotSpotType, FName(TEXT("Bubbles")));
	const FLureHotSpotBonus& Bonus = Fishing->GetHotSpotBonus();
	TestTrue(TEXT("server: the bonus of that hot spot"), Bonus.IsActive() && Bonus.LuckBonus == 3.f && Bonus.SizeBonus == 0.5f && Bonus.ValueMultiplier == 2.f && Bonus.BiteWaitScale == 0.5f);
	TestTrue(TEXT("HUD: 'Bubbling water: better fish here'"), Fishing->GetStatusText().Contains(TEXT("Bubbling water: better fish here")));
	const double HotWait = Fishing->GetScheduledBiteTime() - Fishing->GetNetState().StateStartTime;
	TestNearlyEqual(TEXT("the fish bite in half the time (BiteWaitScale 0.5)"), HotWait, ControlWait * 0.5, 1.0e-4);
	if (!TestTrue(TEXT("a bite"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240)))
	{
		return false;
	}
	const FFishRollContext& Context = Fishing->GetLastRollContext();
	TestEqual(TEXT("the same bite seed as without the hot spot"), Context.Seed, ControlContext.Seed);
	TestNearlyEqual(TEXT("luck = 0 (default water) + 0 (gear) + 3 (hot spot)"), Context.Luck, ControlContext.Luck + 3.f, 1.0e-5f);
	TestTrue(TEXT("size bonus and value multiplier reach the roll"), Context.SizeBonus == 0.5f && Context.ValueMultiplier == 2.f);
	FName Species;
	FFishInstance Expected;
	FFishRollContext Oracle = Context;
	FFishRoll::PickSpecies(World.Fish.Get(), Context, Species);
	Oracle.SpeciesId = Species;
	FFishRoll::Roll(World.Fish.Get(), Oracle, Expected);
	const FFishInstance& Hot = Fishing->GetPendingFish();
	const TArray<FString> Diff = DiffFields(FFishInstance::StaticStruct(), &Hot, &Expected);
	TestEqual(FString::Printf(TEXT("the bite is exactly PickSpecies + Roll of that context (differs in %s)"), *FString::Join(Diff, TEXT(", "))), Diff.Num(), 0);
	if (TestEqual(TEXT("the same species (the hot spot doesn't change the water)"), Hot.SpeciesId, ControlFish.SpeciesId))
	{
		TestTrue(FString::Printf(TEXT("bigger or equal (%.3f kg vs %.3f kg)"), Hot.WeightKg, ControlFish.WeightKg), Hot.WeightKg >= ControlFish.WeightKg);
		TestTrue(FString::Printf(TEXT("worth more (%d vs %d coins)"), Hot.Value, ControlFish.Value), Hot.Value > ControlFish.Value);
		TestEqual(TEXT("the same modifiers (their own RNG stream)"), Hot.ModifierIds.Num(), ControlFish.ModifierIds.Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotCaptureRules, "Project.Fishing.Water.HotSpot.CaptureRules", LureWaterTest::Flags)
bool FLureHotSpotCaptureRules::RunTest(const FString& Parameters)
{
	// The hot spot counts where the bobber lands (the nearest center relative to its radius), stays for the whole cast even if it
	// goes away, never counts after its end, and never for a bobber on land.
	using namespace HotSpotLocal;
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureFishingComponent* Fishing = World.SetUpFishing(*this, World.Spawn(*this), QuickProfile(3.f), 12.f, 99);
	if (!Fishing)
	{
		return false;
	}
	World.Tick(10);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	const FVector Rest = Fishing->GetNetState().BobberRest;
	const FVector2D RestXY(Rest.X, Rest.Y);
	Fishing->AuthorityReelIn();
	World.Tick(2);

	ALureHotSpot* Near = SpawnAt(World, TEXT("Bubbles"), TestHotSpotRow(300.f, 1.f, 0.1f, 1.5f, 0.9f), RestXY, 1, 600.f);
	ALureHotSpot* Beside = SpawnAt(World, TEXT("Ripples"), TestHotSpotRow(300.f, 4.f, 0.3f, 1.f, 0.9f), RestXY + FVector2D(250.0, 0.0), 2, 600.f);
	if (!Near || !Beside || !CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestEqual(TEXT("two hot spots over the bobber: the one it is deepest in"), Fishing->GetHotSpotBonus().TypeId, FName(TEXT("Bubbles")));
	Near->Destroy();
	Beside->Destroy();
	World.Tick(2);
	if (TestTrue(TEXT("a bite after the hot spots are gone"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 300)))
	{
		TestNearlyEqual(TEXT("... still with the bonus it landed in (for the whole cast)"), Fishing->GetLastRollContext().Luck, 1.f, 1.0e-5f);
	}
	Fishing->AuthorityReelIn();
	World.Tick(2);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestFalse(TEXT("no hot spot now: no bonus"), Fishing->GetHotSpotBonus().IsActive() || !Fishing->GetNetState().Water.HotSpotType.IsNone());
	TestFalse(TEXT("... and no hot spot line on the HUD"), Fishing->GetStatusText().Contains(TEXT("Bubbling")));
	Fishing->AuthorityReelIn();
	World.Tick(2);

	// A hot spot ends at its end time (no capture after it, even if the actor is still there this frame).
	ALureHotSpot* Short = SpawnAt(World, TEXT("Bubbles"), TestHotSpotRow(300.f), RestXY, 3, 1.f);
	TestTrue(TEXT("found while it lasts"), FLureWaterQuery::FindHotSpotAt(World.World, RestXY, World.Now()) == Short);
	TestNull(TEXT("not found after its end time"), FLureWaterQuery::FindHotSpotAt(World.World, RestXY, World.Now() + 1.5));
	TestNull(TEXT("not found outside its radius"), FLureWaterQuery::FindHotSpotAt(World.World, RestXY + FVector2D(301.0, 0.0), World.Now()));
	Short->Destroy();

	// On land inside a hot spot's circle: nothing.
	World.AddBox(FVector(Rest.X, Rest.Y, 20.f), FVector(300.f, 300.f, 20.f));
	SpawnAt(World, TEXT("Bubbles"), TestHotSpotRow(600.f), RestXY, 4, 600.f);
	if (!CastAndLand(*this, World, Fishing))
	{
		return false;
	}
	TestFalse(TEXT("QA precondition: the bobber is on the rock"), Fishing->GetNetState().bOnWater);
	TestTrue(TEXT("on land: no hot spot"), Fishing->GetNetState().Water.HotSpotType.IsNone() && !Fishing->GetHotSpotBonus().IsActive());
	return true;
}

// =====================================================================================================================
// The spawner
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotSpawnerData, "Project.Fishing.Water.HotSpot.SpawnerFollowsData", LureWaterTest::Flags)
bool FLureHotSpotSpawnerData::RunTest(const FString& Parameters)
{
	// Hot spots appear per their row: only in allowed habitats, never on land or shallower than MinDepth, at most MaxPerArea per
	// area, MinSpacing apart, with the row's radius and a lifetime in [min, max]; a 0 interval never spawns; the level cap holds;
	// unbounded water gets them near players only; the same seed gives the same hot spots; they end on time.
	using namespace HotSpotLocal;
	FLureHotSpotRow Reef = SpawnRow(0.5f, 2, { TEXT("Habitat.Reef") });
	Reef.Radius = 250.f;
	const FLureHotSpotRow Never = SpawnRow(0.f, 5, {});
	const TStrongObjectPtr<UDataTable> Table(MakeTable({ { TEXT("QA_Reef"), Reef }, { TEXT("QA_Never"), Never } }));
	const ULureWaterSettings* Settings = GetDefault<ULureWaterSettings>();

	auto RunScene = [this, &Table](int32 Seed, TArray<FVector>& OutAnchors, FWaterWorld& World) -> ALureHotSpotSpawner*
	{
		if (!MakeSpawnerScene(*this, World))
		{
			return nullptr;
		}
		ALureHotSpotSpawner* Spawner = MakeSpawner(World, Table.Get(), Seed);
		if (!Spawner)
		{
			return nullptr;
		}
		int32 Spawned = 0;
		for (int32 Step = 0; Step < 6; ++Step)
		{
			Spawned += Spawner->SpawnStep(5.f);
		}
		for (const ALureHotSpot* HotSpot : ALureHotSpotSpawner::GetLiveHotSpots(World.World, World.Now()))
		{
			OutAnchors.Add(FVector(HotSpot->GetState().Anchor));
		}
		AddInfo(FString::Printf(TEXT("seed %d: %d hot spots spawned"), Seed, Spawned));
		return Spawner;
	};

	// A first run with seed 42, for the determinism check (one world at a time).
	TArray<FVector> TwinAnchors;
	{
		FWaterWorld Twin;
		RunScene(42, TwinAnchors, Twin);
	}

	FWaterWorld World;
	TArray<FVector> Anchors;
	ALureHotSpotSpawner* Spawner = RunScene(42, Anchors, World);
	if (!TestNotNull(TEXT("the scene and spawner"), Spawner))
	{
		return false;
	}
	bool bSame = TwinAnchors.Num() == Anchors.Num();
	for (int32 Index = 0; bSame && Index < Anchors.Num(); ++Index)
	{
		bSame = Anchors[Index].Equals(TwinAnchors[Index], 0.01);
	}
	TestTrue(TEXT("the same seed gives the same hot spots"), bSame);
	const TArray<ALureHotSpot*> Live = ALureHotSpotSpawner::GetLiveHotSpots(World.World, World.Now());
	TArray<TWeakObjectPtr<ALureHotSpot>> LiveWeak;
	for (ALureHotSpot* HotSpot : Live)
	{
		LiveWeak.Add(HotSpot);
	}
	TestEqual(TEXT("the reef gets its MaxPerArea (2) after 30 s of 0.5 s intervals"), Live.Num(), 2);
	for (const ALureHotSpot* HotSpot : Live)
	{
		const FLureHotSpotState& State = HotSpot->GetState();
		const FString Label = HotSpot->GetName();
		TestEqual(Label + TEXT(": the reef row"), State.TypeId, FName(TEXT("QA_Reef")));
		TestEqual(Label + TEXT(": in the reef area"), State.AreaId, FName(TEXT("qa_reef")));
		const FVector2D XY(State.Anchor.X, State.Anchor.Y);
		TestTrue(Label + TEXT(": inside the reef circle"), FVector2D::Distance(XY, FVector2D(5000.0, 0.0)) <= 3000.0);
		TestFalse(Label + TEXT(": not over the island"), FMath::Abs(XY.X - 5000.0) <= 400.0 && FMath::Abs(XY.Y) <= 400.0);
		float WaterZ = 0.f;
		float Depth = 0.f;
		TestTrue(Label + TEXT(": fishable water"), FLureWaterQuery::ProbeWater(World.World, XY, WaterZ, Depth) && Depth >= 60.f);
		TestTrue(Label + TEXT(": the row's radius and drift"), State.Radius == 250.f && State.DriftRange == 0.f && State.DriftSpeed == 0.f);
		const double Lifetime = State.EndTime - State.SpawnTime;
		TestTrue(FString::Printf(TEXT("%s: a lifetime in [20, 30] s (%.2f)"), *Label, Lifetime), Lifetime >= 20.0 - 1.0e-6 && Lifetime <= 30.0 + 1.0e-6);
		TestTrue(Label + TEXT(": its bonus is the row's"), HotSpot->GetBonus().LuckBonus == Reef.LuckBonus && HotSpot->GetBonus().SizeBonus == Reef.SizeBonus);
	}
	if (Live.Num() == 2)
	{
		TestTrue(TEXT("MinSpacing apart"), FVector::Dist2D(FVector(Live[0]->GetState().Anchor), FVector(Live[1]->GetState().Anchor)) >= Reef.MinSpacing);
	}
	TestFalse(TEXT("none of the 0-interval row, none in the lagoon, none in default water"), Live.ContainsByPredicate([](const ALureHotSpot* HotSpot)
	{
		return HotSpot->GetState().TypeId != TEXT("QA_Reef") || HotSpot->GetState().AreaId != TEXT("qa_reef");
	}));

	// They end on time (the server destroys them).
	const double LatestEnd = Live.Num() > 0 ? FMath::Max(Live[0]->GetState().EndTime, Live.Last()->GetState().EndTime) : World.Now();
	World.AdvanceTo(LatestEnd + 0.1);
	TestTrue(TEXT("every hot spot is gone after its end time"), LiveWeak.FilterByPredicate([](const TWeakObjectPtr<ALureHotSpot>& HotSpot) { return !IsGone(HotSpot); }).Num() == 0);

	// The level cap.
	Spawner->MaxHotSpots = 1;
	Spawner->SpawnStep(60.f);
	TestTrue(TEXT("MaxHotSpots 1: at most one alive"), ALureHotSpotSpawner::GetLiveHotSpots(World.World, World.Now()).Num() <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotSpawnerOpenWater, "Project.Fishing.Water.HotSpot.SpawnerOpenWaterNearPlayers", LureWaterTest::Flags)
bool FLureHotSpotSpawnerOpenWater::RunTest(const FString& Parameters)
{
	// Unbounded water (the level's default water) only gets hot spots near a player, and never on land (the dock).
	using namespace HotSpotLocal;
	FWaterWorld Open;
	if (!Open.Create(*this))
	{
		return false;
	}
	Open.AddSeabed(-500.f, FVector2D(0.0, 0.0), 8000.f);
	const TStrongObjectPtr<UDataTable> AnyWater(MakeTable({ { TEXT("QA_Any"), SpawnRow(0.5f, 1, {}) } }));
	ALureHotSpotSpawner* Spawner = MakeSpawner(Open, AnyWater.Get(), 5);
	if (!TestNotNull(TEXT("the spawner"), Spawner))
	{
		return false;
	}
	TestEqual(TEXT("no player, only default water: nothing spawns"), Spawner->SpawnStep(60.f), 0);
	ALurePlayerCharacter* Player = Open.Spawn(*this);
	const int32 Count = Spawner->SpawnStep(60.f);
	const TArray<ALureHotSpot*> Near = ALureHotSpotSpawner::GetLiveHotSpots(Open.World, Open.Now());
	if (TestEqual(TEXT("a player: one in the default water"), Count, 1) && Near.Num() == 1 && Player)
	{
		const FVector Anchor(Near[0]->GetState().Anchor);
		TestTrue(TEXT("... in default water (no area)"), Near[0]->GetState().AreaId.IsNone());
		TestTrue(TEXT("... within OpenWaterSpawnRadius of the player"), FVector::Dist2D(Anchor, Player->GetActorLocation()) <= GetDefault<ULureWaterSettings>()->OpenWaterSpawnRadius + 1.0);
		TestFalse(TEXT("... not on the dock"), FMath::Abs(Anchor.X) <= DockHalf + 1.0 && FMath::Abs(Anchor.Y) <= DockHalf + 1.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotDriftInWorld, "Project.Fishing.Water.HotSpot.LifetimeAndDriftInWorld", LureWaterTest::Flags)
bool FLureHotSpotDriftInWorld::RunTest(const FString& Parameters)
{
	// In a world the actor follows its drift (within DriftRange), fades in and out, and is gone at its end time.
	using namespace HotSpotLocal;
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	FLureHotSpotRow Row = TestHotSpotRow(150.f);
	Row.DriftRange = 200.f;
	Row.DriftSpeed = 100.f;
	ALureHotSpot* Spawn = SpawnAt(World, TEXT("Bubbles"), Row, FVector2D(2000.0, 0.0), 5, 3.f);
	if (!TestNotNull(TEXT("spawned"), Spawn))
	{
		return false;
	}
	const TWeakObjectPtr<ALureHotSpot> HotSpot = Spawn;
	const double Spawned = HotSpot->GetState().SpawnTime;
	const float Fade = GetDefault<ULureWaterSettings>()->HotSpotFadeSeconds;
	TestTrue(TEXT("active from its spawn time"), HotSpot->IsActiveAt(Spawned) && !HotSpot->IsActiveAt(Spawned - 0.01));
	TestNearlyEqual(TEXT("fade 0 when it appears"), HotSpot->GetFadeAt(Spawned), 0.f, 1.0e-4f);
	TestNearlyEqual(TEXT("fade 0.5 half way in"), HotSpot->GetFadeAt(Spawned + Fade * 0.5), 0.5f, 1.0e-3f);
	TestNearlyEqual(TEXT("fade 1 in the middle of its life"), HotSpot->GetFadeAt(Spawned + 1.5), 1.f, 1.0e-3f);
	TestNearlyEqual(TEXT("fading out before its end"), HotSpot->GetFadeAt(HotSpot->GetState().EndTime - Fade * 0.5), 0.5f, 1.0e-3f);
	const FVector Start = HotSpot->GetActorLocation();
	double Farthest = 0.0;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		World.Tick(1);
		if (IsGone(HotSpot))
		{
			break;
		}
		const FVector Center = HotSpot->GetCenterAt(World.Now());
		if (!FVector2D(HotSpot->GetActorLocation()).Equals(FVector2D(Center), 0.5))
		{
			AddError(FString::Printf(TEXT("frame %d: the actor is at (%.1f, %.1f), its center (%.1f, %.1f)"), Frame, HotSpot->GetActorLocation().X, HotSpot->GetActorLocation().Y, Center.X, Center.Y));
			break;
		}
		Farthest = FMath::Max(Farthest, FVector::Dist2D(Center, FVector(HotSpot->GetState().Anchor)));
	}
	TestTrue(TEXT("it moves"), !IsGone(HotSpot) && FVector::Dist2D(HotSpot->GetActorLocation(), Start) > 10.0);
	TestTrue(FString::Printf(TEXT("never farther than DriftRange from its anchor (%.1f cm)"), Farthest), Farthest <= 200.0 + 0.01);
	World.AdvanceTo(Spawned + 2.9);
	TestFalse(TEXT("still there at 2.9 s"), IsGone(HotSpot));
	World.AdvanceTo(Spawned + 3.05);
	TestTrue(TEXT("gone at its end time (3 s)"), IsGone(HotSpot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotVisual, "Project.Fishing.Water.HotSpot.VisualPlaceholder", LureWaterTest::Flags)
bool FLureHotSpotVisual::RunTest(const FString& Parameters)
{
	// Every non-dedicated machine gets the look: a separate component, the C++ placeholder by default (foam rings and bubbles,
	// no collision, no shadows, animated); a missing VFX class falls back to the placeholder.
	using namespace HotSpotLocal;
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	AddExpectedMessagePlain(TEXT("does not exist; using the placeholder"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	ALureHotSpot* HotSpot = SpawnAt(World, TEXT("Bubbles"), TestHotSpotRow(300.f), FVector2D(2000.0, 0.0), 9, 60.f);
	ULureHotSpotVisualComponent* Visual = HotSpot ? HotSpot->GetVisual() : nullptr;
	if (!TestNotNull(TEXT("the hot spot has its visual component"), Visual))
	{
		return false;
	}
	TestTrue(TEXT("the C++ placeholder class"), Visual->GetClass() == ULureHotSpotVisualComponent::StaticClass() && Visual->bDrawPlaceholder);
	TestEqual(TEXT("... built from the replicated state (radius)"), Visual->GetHotSpotState().Radius, 300.f);
	const TArray<UInstancedStaticMeshComponent*> Parts = Visual->GetPlaceholderParts();
	TestEqual(TEXT("2 rings + the bubbles"), Parts.Num(), 3);
	for (const UInstancedStaticMeshComponent* Part : Parts)
	{
		TestTrue(TEXT("a part blocks nothing and casts no shadow"), Part && Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Part->CastShadow);
	}
	if (Parts.Num() == 3)
	{
		TestEqual(TEXT("a ring has DotsPerRing dots"), Parts[0]->GetInstanceCount(), Visual->DotsPerRing);
		TestEqual(TEXT("the bubbles"), Parts[2]->GetInstanceCount(), Visual->BubbleCount);
		World.Tick(60); // past the fade in
		const FVector ScaleA = Parts[0]->GetRelativeScale3D();
		World.Tick(20);
		TestFalse(TEXT("the rings spread (animated)"), Parts[0]->GetRelativeScale3D().Equals(ScaleA, 1.0e-4));
		TestTrue(TEXT("visible once faded in"), Visual->IsVisible());
	}
	FLureHotSpotRow Missing = TestHotSpotRow(300.f);
	Missing.VisualClass = TSoftClassPtr<ULureHotSpotVisualComponent>(FSoftObjectPath(TEXT("/Game/QA/NotThere/BP_HotSpotNope.BP_HotSpotNope_C")));
	ALureHotSpot* Fallback = SpawnAt(World, TEXT("Bubbles"), Missing, FVector2D(4000.0, 0.0), 10, 60.f);
	TestTrue(TEXT("a missing VFX class: the placeholder"), Fallback && Fallback->GetVisual() && Fallback->GetVisual()->GetClass() == ULureHotSpotVisualComponent::StaticClass());
	return true;
}

// =====================================================================================================================
// Replication
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotNetState, "Project.Fishing.Water.Net.HotSpotStateReplicates", LureWaterTest::Flags)
bool FLureHotSpotNetState::RunTest(const FString& Parameters)
{
	// The hot spot replicates one state (always relevant, no movement); through the wire format a copy computes the same drifting
	// center. The bobber's water part of the fishing net state (hot spot type, no-bite reason) survives the wire too.
	using namespace HotSpotLocal;
	const ALureHotSpot* Default = GetDefault<ALureHotSpot>();
	TestTrue(TEXT("replicates, always relevant, no movement replication"), Default->GetIsReplicated() && Default->bAlwaysRelevant && !Default->IsReplicatingMovement());
	const FStructProperty* StateProperty = CastField<FStructProperty>(ALureHotSpot::StaticClass()->FindPropertyByName(TEXT("State")));
	if (!TestNotNull(TEXT("the State property"), StateProperty))
	{
		return false;
	}
	// The class's replication indices exist once a net driver used it; set them up here (no networking in this test).
	ALureHotSpot::StaticClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Lifetime;
	Default->GetLifetimeReplicatedProps(Lifetime);
	const FLifetimeProperty* Rep = Lifetime.FindByPredicate([StateProperty](const FLifetimeProperty& Property) { return Property.RepIndex == StateProperty->RepIndex; });
	TestTrue(TEXT("State is replicated to everyone (COND_None)"), StateProperty->HasAnyPropertyFlags(CPF_Net) && Rep && Rep->Condition == COND_None);
	const FProperty* BonusProperty = ALureHotSpot::StaticClass()->FindPropertyByName(TEXT("Bonus"));
	TestTrue(TEXT("the bonus is server-only (not replicated)"), !BonusProperty || !BonusProperty->HasAnyPropertyFlags(CPF_Net));

	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	FLureHotSpotRow Row = TestHotSpotRow(275.f);
	Row.DriftRange = 350.f;
	Row.DriftSpeed = 22.f;
	Row.VisualColor = FLinearColor(0.2f, 0.6f, 0.9f, 1.f);
	ALureHotSpot* Source = ALureHotSpot::SpawnHotSpot(World.World, nullptr, TEXT("Ripples"), Row, TEXT("qa_area"), FVector(1234.56, -789.01, -60.0), 4242, World.Now(), 90.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALureHotSpot* Target = World.World->SpawnActor<ALureHotSpot>(ALureHotSpot::StaticClass(), FTransform::Identity, Params);
	if (!Source || !Target)
	{
		return false;
	}
	const int64 Bits = NetCopy(*this, StateProperty, Source, Target);
	AddInfo(FString::Printf(TEXT("a hot spot's state is %lld bits (%lld bytes) on the wire, once"), Bits, (Bits + 7) / 8));
	TestTrue(TEXT("small: <= 96 bytes"), Bits > 0 && (Bits + 7) / 8 <= 96);
	const FLureHotSpotState& A = Source->GetState();
	const FLureHotSpotState& B = Target->GetState();
	TestTrue(TEXT("type, area, seed, radius, drift, times, color"), A.TypeId == B.TypeId && A.AreaId == B.AreaId && A.Seed == B.Seed && A.Radius == B.Radius
		&& A.DriftRange == B.DriftRange && A.DriftSpeed == B.DriftSpeed && A.SpawnTime == B.SpawnTime && A.EndTime == B.EndTime && A.Color == B.Color);
	TestTrue(TEXT("the anchor (0.1 cm on the wire, pre-quantized on the server)"), FVector(A.Anchor).Equals(FVector(B.Anchor), 0.001));
	for (const double Offset : { 0.0, 1.5, 17.25, 60.0 })
	{
		const double Time = A.SpawnTime + Offset;
		TestTrue(FString::Printf(TEXT("the same center %.2f s in"), Offset), Source->GetCenterAt(Time).Equals(Target->GetCenterAt(Time), 0.001));
	}

	// The bobber's water in the fishing net state.
	const FStructProperty* NetProperty = CastField<FStructProperty>(ULureFishingComponent::StaticClass()->FindPropertyByName(TEXT("NetState")));
	ULureFishingComponent* Server = NewObject<ULureFishingComponent>();
	ULureFishingComponent* Client = NewObject<ULureFishingComponent>();
	if (!TestNotNull(TEXT("the NetState property"), NetProperty))
	{
		return false;
	}
	FLureFishingNetState* ServerState = NetProperty->ContainerPtrToValuePtr<FLureFishingNetState>(Server);
	ServerState->State = ELureFishingState::Waiting;
	ServerState->bOnWater = true;
	ServerState->bNoFishHere = true;
	ServerState->SpotId = TEXT("qa_reef");
	ServerState->Water.HotSpotType = TEXT("Ripples");
	ServerState->Water.NoBiteReason = ELureNoBiteReason::WrongBait;
	const int64 NetBits = NetCopy(*this, NetProperty, Server, Client);
	TestTrue(TEXT("the net state still fits in 128 bytes"), NetBits > 0 && (NetBits + 7) / 8 <= 128);
	TestTrue(TEXT("the client gets the hot spot type, the reason and the area"), Client->GetNetState().Water.HotSpotType == TEXT("Ripples")
		&& Client->GetNetState().Water.NoBiteReason == ELureNoBiteReason::WrongBait && Client->GetNetState().SpotId == TEXT("qa_reef") && Client->GetNetState().bNoFishHere);
	return true;
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotNet2P, "Project.Fishing.Water.Net2P.HotSpotsReachClients", LureWaterTest::Flags)
bool FLureHotSpotNet2P::RunTest(const FString& Parameters)
{
	// A real server and client (in-process net drivers): a hot spot spawned on the server appears on the client with the same
	// state and the same drifting center, disappears there when the server ends it, and a client never spawns its own.
	using namespace HotSpotLocal;
	AddExpectedMessagePlain(TEXT("Player start not found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	UE::Net::FTestWorlds Worlds(TEXT("/Engine/Maps/Entry"), TEXT("/Script/VibeGame.LureGameMode"));
	UWorld* ServerWorld = Worlds.Server.GetWorld();
	if (!TestTrue(TEXT("harness: the server world is up"), Worlds.Server.IsLoaded() && ServerWorld && ServerWorld->GetNetDriver())
		|| !TestTrue(TEXT("harness: a client connects"), Worlds.CreateAndConnectClient()))
	{
		return false;
	}
	FLureHotSpotRow Row = TestHotSpotRow(250.f);
	Row.DriftRange = 300.f;
	Row.DriftSpeed = 40.f;
	ALureHotSpot* ServerHotSpot = ALureHotSpot::SpawnHotSpot(ServerWorld, nullptr, TEXT("Bubbles"), Row, TEXT("qa_area"), FVector(1000.0, 500.0, 0.0), 321,
		FLureWaterQuery::GetTime(ServerWorld), 3.f);
	if (!TestNotNull(TEXT("the server spawns a hot spot"), ServerHotSpot))
	{
		return false;
	}
	ALureHotSpot* ClientHotSpot = nullptr;
	const bool bArrived = Worlds.TickAllUntil([&]()
	{
		// Ask the server first: the harness lookup ensures on an object that has no net id yet.
		if (!Worlds.IsServerObjectReplicated(ServerHotSpot) || !Worlds.DoesReplicatedObjectExistOnClient(ServerHotSpot, 0))
		{
			return false;
		}
		ClientHotSpot = Cast<ALureHotSpot>(Worlds.FindReplicatedObjectOnClient(ServerHotSpot, 0));
		return ClientHotSpot && ClientHotSpot->GetState().EndTime > 0.0;
	}, Dt, 300);
	if (!TestTrue(TEXT("the hot spot reaches the client"), bArrived))
	{
		return false;
	}
	const FLureHotSpotState& A = ServerHotSpot->GetState();
	const FLureHotSpotState& B = ClientHotSpot->GetState();
	TestTrue(TEXT("the same state"), A.TypeId == B.TypeId && A.AreaId == B.AreaId && A.Seed == B.Seed && A.Radius == B.Radius && A.DriftRange == B.DriftRange
		&& A.DriftSpeed == B.DriftSpeed && A.SpawnTime == B.SpawnTime && A.EndTime == B.EndTime && FVector(A.Anchor).Equals(FVector(B.Anchor), 0.001));
	const double Time = A.SpawnTime + 1.234;
	TestTrue(TEXT("the same center at the same server time"), ServerHotSpot->GetCenterAt(Time).Equals(ClientHotSpot->GetCenterAt(Time), 0.001));
	TestNotNull(TEXT("the client draws it (a visual component)"), ClientHotSpot->GetVisual());
	TestNull(TEXT("the dedicated server draws nothing"), ServerHotSpot->GetVisual());

	// A spawner on the client does nothing (a level actor that doesn't replicate has authority there too).
	UWorld* ClientWorld = Worlds.Clients[0].GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALureHotSpotSpawner* ClientSpawner = ClientWorld ? ClientWorld->SpawnActor<ALureHotSpotSpawner>(ALureHotSpotSpawner::StaticClass(), FTransform::Identity, Params) : nullptr;
	TestEqual(TEXT("a client's spawner never spawns"), ClientSpawner ? ClientSpawner->SpawnStep(600.f) : -1, 0);

	TWeakObjectPtr<ALureHotSpot> ClientWeak = ClientHotSpot;
	const bool bGone = Worlds.TickAllUntil([&ClientWeak]() { return !ClientWeak.IsValid() || ClientWeak->IsActorBeingDestroyed(); }, Dt, 600);
	TestTrue(TEXT("it disappears on the client after the server ends it"), bGone);
	return true;
}

#endif // WITH_EDITOR

// =====================================================================================================================
// Dev commands
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureHotSpotDevCommands, "Project.Fishing.Water.Dev.HotSpotCommands", LureWaterTest::Flags)
bool FLureHotSpotDevCommands::RunTest(const FString& Parameters)
{
	// Lure.HotSpot.Spawn puts one on the water in front of the player (refused on land or for an unknown type), Lure.Water.Show
	// and Lure.Water.Probe describe the water, Lure.HotSpot.Clear removes them all. The console commands are registered.
	FWaterWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	for (const TCHAR* Command : { FLureWaterDevCommands::ShowCommand, FLureWaterDevCommands::ProbeCommand, FLureWaterDevCommands::SpawnHotSpotCommand,
		FLureWaterDevCommands::ClearHotSpotsCommand })
	{
		TestNotNull(FString::Printf(TEXT("console command %s is registered"), Command), IConsoleManager::Get().FindConsoleObject(Command));
	}
	ALurePlayerCharacter* Player = World.Spawn(*this);
	World.PossessLocally(Player);
	World.Tick(5);
	FString Message;
	ALureHotSpot* HotSpot = FLureWaterDevCommands::SpawnHotSpotInFront(World.World, NAME_None, 1000.f, 30.f, Message);
	if (TestNotNull(FString::Printf(TEXT("a hot spot 10 m in front (%s)"), *Message), HotSpot))
	{
		TestNearlyEqual(TEXT("... 10 m in front of the player"), static_cast<float>(FVector::Dist2D(FVector(HotSpot->GetState().Anchor), Player->GetActorLocation())), 1000.f, 1.f);
		TestEqual(TEXT("... of the first type (the built-in Bubbles without the table)"), HotSpot->GetState().TypeId, FLureWaterRules::FallbackHotSpotType());
		TestNearlyEqual(TEXT("... for the lifetime asked"), static_cast<float>(HotSpot->GetState().EndTime - HotSpot->GetState().SpawnTime), 30.f, 1.0e-3f);
	}
	TestNull(TEXT("an unknown type is refused"), FLureWaterDevCommands::SpawnHotSpotInFront(World.World, TEXT("QA_NoSuchType"), 1000.f, 30.f, Message));
	TestTrue(TEXT("... naming the known types"), Message.Contains(TEXT("unknown hot spot type")) && Message.Contains(TEXT("Bubbles")));
	TestNull(TEXT("on the dock (land) it is refused"), FLureWaterDevCommands::SpawnHotSpotInFront(World.World, NAME_None, 50.f, 30.f, Message));
	TestTrue(TEXT("... saying why"), Message.Contains(TEXT("no fishable water")));
	TestTrue(TEXT("Lure.Water.Show lists the hot spot"), FLureWaterDevCommands::DescribeWater(World.World, 0.f).Contains(TEXT("1 hot spot(s)")));
	const FString Probe = FLureWaterDevCommands::ProbeInFront(World.World, 1000.f);
	TestTrue(FString::Printf(TEXT("Lure.Water.Probe: default water and the hot spot (%s)"), *Probe), Probe.Contains(TEXT("default water")) && Probe.Contains(TEXT("in hot spot")));
	TestEqual(TEXT("Lure.HotSpot.Clear removes it"), FLureWaterDevCommands::ClearHotSpots(World.World), 1);
	World.Tick(2);
	TestEqual(TEXT("... none left"), ALureHotSpotSpawner::GetLiveHotSpots(World.World, World.Now()).Num(), 0);
	return true;
}

} // namespace LureWaterTest

#endif // WITH_DEV_AUTOMATION_TESTS

// Lure T-030 independent QA (qa-engineer): shared helpers for Project.Catch.QA.*. Black-box from
// docs/specs/catch-handling-rules.md and the public headers (never the .cpp). Every helper is inline in namespace LureCatchQA
// (unity builds merge .cpp files: no file-scope using-directives, no non-inline definitions in this header).
//
// Patterns for the next QA agent:
// - Conservation: after EVERY step of a flow, CountCopies(World, Fish) must be exactly 1 (or 0 once sold/lost). It counts
//   fish items (any hold state) plus records inside every cooler, found with TActorIterator (not the catch subsystem's
//   registry, so a registry bug can't hide a duplicate).
// - Money: compare with OraclePrice (the spec formula restated here), never with the code's own quote alone.
// - Races: resolve BOTH players' verbs first, perform one, then send the other's stale verb through TryInteract (the
//   server path of ServerInteract): it must be refused and nothing may move.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EngineUtils.h"
#include "Tests/Catch/CatchTestUtils.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureFishItem.h"
#include "Fish/FishRoll.h"
#include "Progression/LureCoolerComponent.h"

namespace LureCatchQA
{
	/** Spec "Freshness": x = clamp((Exposure - Grace) / Spoil, 0, 1); freshness = 1 - x ^ CurveExponent */
	inline double OracleFreshness(double Grace, double Spoil, double Exponent, double Exposure)
	{
		const double X = FMath::Clamp((Exposure - Grace) / Spoil, 0.0, 1.0);
		return 1.0 - FMath::Pow(X, Exponent);
	}

	/** ValueShare = MinValueShare + (1 - MinValueShare) x Freshness */
	inline double OracleShare(double Grace, double Spoil, double Exponent, double MinShare, double Exposure)
	{
		return MinShare + (1.0 - MinShare) * OracleFreshness(Grace, Spoil, Exponent, Exposure);
	}

	/** max(1, round-half-up(Value x Share x Multiplier)), round-half-up(x) = floor(x + 0.5), saturating at MAX_int32 */
	inline int32 OraclePrice(int32 Value, double Share, double Multiplier)
	{
		const double Raw = FMath::FloorToDouble(static_cast<double>(Value) * Share * Multiplier + 0.5);
		return static_cast<int32>(FMath::Clamp(Raw, 1.0, static_cast<double>(MAX_int32)));
	}

	/** How many copies of Fish exist in World: fish items (hook, hand, ground, counter) + records in every cooler */
	inline int32 CountCopies(UWorld* World, const FFishInstance& Fish)
	{
		int32 Count = 0;
		if (!World)
		{
			return 0;
		}
		for (TActorIterator<ALureFishItem> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed() && FishQA::Same(It->GetCatch().Fish, Fish))
			{
				++Count;
			}
		}
		for (TActorIterator<ALureCoolerActor> It(World); It; ++It)
		{
			if (!IsValid(*It) || It->IsActorBeingDestroyed() || !It->GetStorage())
			{
				continue;
			}
			for (const FLureCaughtFish& Record : It->GetStorage()->GetFish())
			{
				Count += FishQA::Same(Record.Fish, Fish) ? 1 : 0;
			}
		}
		return Count;
	}

	/** Live coolers in World */
	inline int32 CountCoolers(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ALureCoolerActor> It(World); It; ++It)
		{
			Count += (IsValid(*It) && !It->IsActorBeingDestroyed()) ? 1 : 0;
		}
		return Count;
	}

	/** Cosine between the cooler's front (+X, the latch) and the direction to Viewer, horizontally: 1 = faces them */
	inline float FrontFacing(const AActor* Cooler, const FVector& Viewer)
	{
		const FVector Front = Cooler->GetActorForwardVector().GetSafeNormal2D();
		const FVector To = (Viewer - Cooler->GetActorLocation()).GetSafeNormal2D();
		return static_cast<float>(FVector::DotProduct(Front, To));
	}

	/** A real catch from the roll pipeline on the shipped fish tables (Palm Key, 10:00) */
	inline FFishInstance Roll(FAutomationTestBase& Test, const FishQA::FTables& Tables, FName Species, int32 Seed)
	{
		FFishInstance Fish;
		Test.TestTrue(FString::Printf(TEXT("QA precondition: the roll pipeline makes a %s (seed %d)"), *Species.ToString(), Seed),
			FFishRoll::Roll(Tables.Get(), FishQA::Ctx(Species, Seed), Fish) && Fish.IsValid() && Fish.Value > 0);
		return Fish;
	}

	/** A catch record landed at Now (fresh) */
	inline FLureCaughtFish Record(const FFishInstance& Fish, double Now)
	{
		return FLureCaughtFish::Landed(Fish, Now);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

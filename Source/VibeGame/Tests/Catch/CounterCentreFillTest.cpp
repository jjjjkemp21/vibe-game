// Lure T-041a tests (unreal-engineer): the sell counter fills from the centre outwards (centre, +1, -1, +2, -2 ...), a
// freed spot nearer the centre is refilled first, and a full counter falls back to the centre.
// Rules: docs/specs/catch-handling-rules.md "The sell counter". Project.Catch.Counter.CentreFill.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureFishItem.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"

namespace LureCounterCentreFillTest
{
	FString Join(const TArray<float>& Offsets)
	{
		FString Out;
		for (const float Offset : Offsets)
		{
			Out += FString::Printf(TEXT("%s%.1f"), Out.IsEmpty() ? TEXT("") : TEXT(", "), Offset);
		}
		return Out;
	}

	bool SameOffsets(const TArray<float>& Actual, const TArray<float>& Expected)
	{
		if (Actual.Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Actual.Num(); ++Index)
		{
			if (!FMath::IsNearlyEqual(Actual[Index], Expected[Index], 0.01f))
			{
				return false;
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCounterCentreFillOrder, "Project.Catch.Counter.CentreFill.SpotOrder", LCT::Flags)
	bool FCounterCentreFillOrder::RunTest(const FString& Parameters)
	{
		// Odd count: 100 long / 20 = 5 spots.
		const TArray<float> Odd = ALureSellCounter::GetSpotOffsets(50.0f, 20.0f);
		TestTrue(FString::Printf(TEXT("odd: centre, +1, -1, +2, -2 (%s)"), *Join(Odd)), SameOffsets(Odd, { 0.0f, 20.0f, -20.0f, 40.0f, -40.0f }));

		// Even count: 80 long / 20 = 4 spots: the two middle ones first.
		const TArray<float> Even = ALureSellCounter::GetSpotOffsets(40.0f, 20.0f);
		TestTrue(FString::Printf(TEXT("even: the two middle spots first (%s)"), *Join(Even)), SameOffsets(Even, { 10.0f, -10.0f, 30.0f, -30.0f }));

		// The default counter (230 half length, 45 spacing): 10 spots, all inside the counter, symmetric.
		const TArray<float> Default = ALureSellCounter::GetSpotOffsets(230.0f, 45.0f);
		TestEqual(TEXT("default: 10 spots"), Default.Num(), 10);
		bool bInside = true;
		for (int32 Index = 0; Index < Default.Num(); ++Index)
		{
			bInside &= FMath::Abs(Default[Index]) <= 230.0f;
			if (Index > 0)
			{
				TestTrue(FString::Printf(TEXT("default: never farther from the centre than the next (%s)"), *Join(Default)),
					FMath::Abs(Default[Index - 1]) <= FMath::Abs(Default[Index]) + 0.01f);
			}
		}
		TestTrue(FString::Printf(TEXT("default: every spot inside the counter (%s)"), *Join(Default)), bInside);
		TestTrue(TEXT("default: first spot half a step from the centre"), FMath::IsNearlyEqual(FMath::Abs(Default[0]), 22.5f, 0.01f));

		// Spacing larger than the counter: one centre spot.
		const TArray<float> One = ALureSellCounter::GetSpotOffsets(20.0f, 100.0f);
		TestTrue(FString::Printf(TEXT("spacing > counter: one centre spot (%s)"), *Join(One)), SameOffsets(One, { 0.0f }));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCounterCentreFillWorld, "Project.Catch.Counter.CentreFill.PlacesCentreFirst", LCT::Flags)
	bool FCounterCentreFillWorld::RunTest(const FString& Parameters)
	{
		LCT::FWorld W;
		if (!W.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Pawn = W.SpawnPlayer(*this, FVector(0.0f, 0.0f, LCT::DockTop));
		ALureSellCounter* Counter = W.SpawnCounter(FVector(150.0f, 0.0f, LCT::DockTop), 180.0f);
		if (!TestNotNull(TEXT("player"), Pawn) || !TestNotNull(TEXT("counter"), Counter))
		{
			return false;
		}
		W.Tick(1);
		const float Spacing = Counter->FishSpacing;
		const FTransform Frame = Counter->GetActorTransform();
		const FVector Origin = Counter->GetActorLocation();
		auto Place = [&](int32 Seed) -> ALureFishItem*
		{
			ALureFishItem* Fish = W.LandInHand(Pawn, LCT::MakeFish(TEXT("Bonefish"), 45, 1, 1.5f, Seed));
			return Fish && Counter->AuthorityPlaceFish(Pawn, Fish) ? Fish : nullptr;
		};
		auto LocalY = [&](const ALureFishItem* Fish) { return Fish ? Frame.InverseTransformPositionNoScale(Fish->GetActorLocation()).Y : 1.0e6; };
		auto Inside = [&](const ALureFishItem* Fish) { return Fish && FMath::Abs(LocalY(Fish)) <= Counter->CounterHalfSize.Y; };

		ALureFishItem* A = Place(1);
		if (!TestNotNull(TEXT("1st fish placed"), A))
		{
			return false;
		}
		const double DistA = FVector::Dist2D(A->GetActorLocation(), Origin);
		TestTrue(FString::Printf(TEXT("1st fish at the centre: %.1f from the origin (<= half the spacing %.1f)"), DistA, 0.5f * Spacing), DistA <= 0.5f * Spacing + 0.5f);

		ALureFishItem* B = Place(2);
		ALureFishItem* C = Place(3);
		if (!TestNotNull(TEXT("2nd fish placed"), B) || !TestNotNull(TEXT("3rd fish placed"), C))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("2nd and 3rd on opposite sides (y %.1f, %.1f)"), LocalY(B), LocalY(C)), LocalY(B) * LocalY(C) < 0.0);
		TestTrue(TEXT("... each next to the centre (within 1.5 spacings)"), FMath::Abs(LocalY(B)) <= 1.5f * Spacing + 0.5f && FMath::Abs(LocalY(C)) <= 1.5f * Spacing + 0.5f);
		TestTrue(TEXT("all three inside the counter"), Inside(A) && Inside(B) && Inside(C) && Counter->ContainsPoint(A->GetActorLocation())
			&& Counter->ContainsPoint(B->GetActorLocation()) && Counter->ContainsPoint(C->GetActorLocation()));

		// A spot freed nearer the centre is refilled first.
		const FVector SpotA = A->GetActorLocation();
		A->Destroy();
		W.Tick(1);
		ALureFishItem* D = Place(4);
		TestTrue(TEXT("a freed centre spot is refilled first"), D && FVector::Dist2D(D->GetActorLocation(), SpotA) < 0.5f * Spacing);

		// Take back the last one: its spot is the next one filled again.
		const FVector SpotD = D ? D->GetActorLocation() : FVector::ZeroVector;
		TestTrue(TEXT("take back"), Counter->AuthorityTakeBack(Pawn));
		ALureFishItem* HeldAgain = D;
		TestTrue(TEXT("put back: the same spot (nearest the centre that is free)"), HeldAgain && Counter->AuthorityPlaceFish(Pawn, HeldAgain)
			&& FVector::Dist2D(HeldAgain->GetActorLocation(), SpotD) < 0.5f * Spacing);

		// Fill the counter; one more falls back to the centre spot (not the end).
		const int32 Capacity = ALureSellCounter::GetSpotOffsets(Counter->CounterHalfSize.Y, Spacing).Num();
		for (int32 Seed = 10; Counter->GetFishOnCounter().Num() < Capacity && Seed < 10 + Capacity; ++Seed)
		{
			Place(Seed);
		}
		TestEqual(TEXT("counter full"), Counter->GetFishOnCounter().Num(), Capacity);
		ALureFishItem* Extra = Place(100);
		const double DistExtra = Extra ? FVector::Dist2D(Extra->GetActorLocation(), Origin) : 1.0e6;
		TestTrue(FString::Printf(TEXT("full counter: falls back to the centre (%.1f from the origin)"), DistExtra), DistExtra <= 0.5f * Spacing + 0.5f);
		return true;
	}
}

#endif

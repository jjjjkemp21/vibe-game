// Lure T-030i tests (unreal-engineer): where the starter cooler goes on a map with no Lure.CoolerSpawn marker and no
// PlayerStart (the engine then hands RestartPlayerAtPlayerStart its world settings, at the origin): next to the first
// player's pawn, in the same row rule, on the floor, with a warning. Rules: docs/specs/catch-handling-rules.md.
// Project.Catch.CoolerSpawnFallback.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Catch/LureCatchLibrary.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCoolerActor.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/World.h"
#include "Game/LurePlayerState.h"
#include "GameFramework/WorldSettings.h"

namespace LureCoolerSpawnFallbackTest
{
	constexpr const TCHAR* NoStartWarning = TEXT("and no player start");

	struct FPlayer
	{
		ALurePlayerCharacter* Pawn = nullptr;
		ALurePlayerState* State = nullptr;

		bool Spawn(FAutomationTestBase& Test, LCT::FWorld& W, const FVector& Feet, float Yaw)
		{
			Pawn = W.SpawnPlayer(Test, Feet);
			LCT::PlaceAt(Pawn, Feet, Yaw);
			State = Pawn ? Pawn->GetPlayerState<ALurePlayerState>() : nullptr;
			return Test.TestNotNull(TEXT("player"), Pawn) && Test.TestNotNull(TEXT("player state"), State);
		}
	};

	/** Slot Index of the row next to Anchor (StarterCoolerOffset in its yaw frame, StarterCoolerSpacing along its +Y) */
	FVector RowSlot(const AActor* Anchor, int32 Index)
	{
		const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
		const FTransform Frame(FRotator(0.0f, Anchor->GetActorRotation().Yaw, 0.0f), Anchor->GetActorLocation());
		return Frame.TransformPositionNoScale(Settings->StarterCoolerOffset + FVector(0.0f, Settings->StarterCoolerSpacing * static_cast<float>(Index), 0.0f));
	}

	bool Near2D(const FVector& A, const FVector& B)
	{
		return FVector2D::Distance(FVector2D(A), FVector2D(B)) < 0.5f;
	}

	FString Where(const AActor* Actor)
	{
		return Actor ? Actor->GetActorLocation().ToCompactString() : FString(TEXT("-"));
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoStartUsesFirstPawn, "Project.Catch.CoolerSpawnFallback.NoStartUsesFirstPlayerPawn", LCT::Flags)
bool FNoStartUsesFirstPawn::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	FPlayer P1;
	FPlayer P2;
	FPlayer P3;
	if (!W.Create(*this) || !P1.Spawn(*this, W, FVector(-200.0f, 100.0f, LCT::DockTop), 90.0f)
		|| !P2.Spawn(*this, W, FVector(300.0f, -300.0f, LCT::DockTop), 0.0f) || !P3.Spawn(*this, W, FVector(300.0f, 300.0f, LCT::DockTop), 0.0f))
	{
		return false;
	}
	AWorldSettings* NoStart = W.World->GetWorldSettings();
	if (!TestNotNull(TEXT("the world settings (what the engine passes when a map has no PlayerStart)"), NoStart))
	{
		return false;
	}
	AddExpectedMessagePlain(NoStartWarning, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);

	// The first player: next to their pawn (StarterCoolerOffset in its yaw frame), on the dock, not at the origin.
	ALureCoolerActor* First = ULureCatchLibrary::EnsureStarterCooler(P1.State, NoStart);
	if (!TestNotNull(TEXT("no marker, no player start: still a starter cooler"), First))
	{
		return false;
	}
	const FVector Expected = RowSlot(P1.Pawn, 0);
	TestTrue(FString::Printf(TEXT("... next to the first player's pawn (%s, expected %s)"), *Where(First), *Expected.ToCompactString()), Near2D(First->GetActorLocation(), Expected));
	TestTrue(FString::Printf(TEXT("... on the floor (z %.2f)"), First->GetActorLocation().Z), FMath::IsNearlyEqual(First->GetActorLocation().Z, LCT::DockTop, 0.5));
	TestTrue(TEXT("... not at the world origin"), First->GetActorLocation().Size2D() > 100.0);
	const FVector ToPawn = P1.Pawn->GetActorLocation() - First->GetActorLocation();
	TestTrue(TEXT("... its front toward the pawn"), FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(First->GetActorRotation().Yaw)
		- static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToPawn.Y, ToPawn.X))))) < 0.5f);
	TestTrue(TEXT("... theirs, a starter"), First->GetOwningPlayerState() == P1.State && First->IsStarter());

	// The next players: the same row by the first player's pawn, one spacing further each, never inside each other.
	ALureCoolerActor* Second = ULureCatchLibrary::EnsureStarterCooler(P2.State, NoStart);
	const FVector Slot1 = RowSlot(P1.Pawn, 1);
	TestTrue(FString::Printf(TEXT("the second player's: next slot in the row (%s, expected %s)"), *Where(Second), *Slot1.ToCompactString()),
		Second && Second->GetOwningPlayerState() == P2.State && Near2D(Second->GetActorLocation(), Slot1));
	// A null start spot (RestartPlayer found nothing) takes the same fallback.
	ALureCoolerActor* Third = ULureCatchLibrary::EnsureStarterCooler(P3.State, nullptr);
	const FVector Slot2 = RowSlot(P1.Pawn, 2);
	TestTrue(FString::Printf(TEXT("a null start: the third slot (%s, expected %s)"), *Where(Third), *Slot2.ToCompactString()),
		Third && Third->GetOwningPlayerState() == P3.State && Near2D(Third->GetActorLocation(), Slot2));
	TestTrue(TEXT("... all on the floor"), Second && Third && FMath::IsNearlyEqual(Second->GetActorLocation().Z, LCT::DockTop, 0.5)
		&& FMath::IsNearlyEqual(Third->GetActorLocation().Z, LCT::DockTop, 0.5));
	TestNull(TEXT("only once per player"), ULureCatchLibrary::EnsureStarterCooler(P1.State, NoStart));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuleOrder, "Project.Catch.CoolerSpawnFallback.MarkerThenStartThenPawn", LCT::Flags)
bool FRuleOrder::RunTest(const FString& Parameters)
{
	LCT::FWorld W;
	FPlayer P1;
	FPlayer P2;
	if (!W.Create(*this) || !P1.Spawn(*this, W, FVector(-200.0f, 100.0f, LCT::DockTop), 90.0f) || !P2.Spawn(*this, W, FVector(300.0f, -300.0f, LCT::DockTop), 0.0f))
	{
		return false;
	}
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();

	// A real player start beats the pawn fallback (and logs no warning).
	const AActor* Start = W.SpawnMarker(FVector(-300.0f, -300.0f, LCT::DockTop + 92.0f), 0.0f);
	ALureCoolerActor* ByStart = ULureCatchLibrary::EnsureStarterCooler(P1.State, Start);
	const FVector AtStart = Start->GetActorTransform().TransformPositionNoScale(Settings->StarterCoolerOffset);
	TestTrue(FString::Printf(TEXT("a player start: next to it, not the pawn (%s, expected %s)"), *Where(ByStart), *AtStart.ToCompactString()),
		ByStart && Near2D(ByStart->GetActorLocation(), AtStart));

	// A Lure.CoolerSpawn marker beats everything, also when the map has no PlayerStart.
	const AActor* Marker = W.SpawnMarker(FVector(300.0f, 300.0f, LCT::DockTop), 180.0f, Settings->CoolerSpawnTag);
	ALureCoolerActor* ByMarker = ULureCatchLibrary::EnsureStarterCooler(P2.State, W.World->GetWorldSettings());
	// One starter already stands in the level, so this is the marker's second slot.
	const FVector AtMarker = Marker->GetActorTransform().TransformPositionNoScale(FVector(0.0f, Settings->StarterCoolerSpacing, 0.0f));
	TestTrue(FString::Printf(TEXT("a marker: at the marker (%s, expected %s)"), *Where(ByMarker), *AtMarker.ToCompactString()),
		ByMarker && Near2D(ByMarker->GetActorLocation(), AtMarker));
	TestTrue(TEXT("... facing like the marker"), ByMarker && FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(ByMarker->GetActorRotation().Yaw) - 180.0f)) < 0.5f);
	return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS

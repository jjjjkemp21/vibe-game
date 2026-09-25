// T-075 (Sprint 1 playtest, P0): "the hooked fish is never visible during the fight". The fish was there (spawned, SK_Bonefish +
// ABP_Fish, mouth on the line, 25 cm deep), but T-048 turned its body straight away from the rod and T-048b swung it only
// 0-20 deg for the Bonefish's moves, so from the angler's eye it was end-on: its 9 x 16 cm front hid behind the 4.5x bobber,
// which the fight pulls under right at the mouth. T-075: the body always lies DT_FishVisual BodyAngleDeg off the line.
// Project.FishVisual.T075.*: the body-angle rules (pure); from the dock-end eye (2.26 m above the water) at 4, 10 and 18 m the
// real fish mesh shows past the real bobber (with BodyAngleDeg 0, the T-048 rule, it does not: the playtest bug); the same in
// a real fight (fish actor, mesh, visible, mouth under the bobber). The shares are floors against the end-on regression, at
// the shipped bobber (7.9x since T-075a: it still covers 70-80 % of a 0.6 kg fish, 50-57 % of a 3 kg one). Data from
// data/tables/ (never the binary DataTables); SK_Bonefish, ABP_Fish and SM_Bobber are the real assets.

#include "Tests/FishFight/FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Camera/CameraComponent.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"
#include "Fish/LureFightFishSubsystem.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Fishing/LureFishingSettings.h"
#if WITH_EDITOR
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#endif

namespace LureT075Test
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The Sprint 1 playtest's eye at the dock end: dock top 70 cm + the standing eye, above the water (z 0), cm. */
	constexpr float DockEyeHeight = 226.f;

	/** Share of the fish mesh that must be outside the bobber, seen from the angler's eye: the regression floor. The T-048 rule
	 *  (end-on) gives 0-1 % with the 7.9x bobber (T-075a) and 0-8 % with the playtest's 4.5x; T-075 gives a 0.6 kg Bonefish
	 *  20-30 % at 7.9x (48-52 % at 4.5x): a small fish is about as long as the 7.9x bobber is wide. */
	constexpr float MinVisible = 0.15f;

	/** A fish clearly longer than the bobber is wide (3 kg Bonefish, ~70 cm) shows at least this share (43-51 % at 7.9x). */
	constexpr float MinVisibleBig = 0.35f;

	/** A real fight (a 0.9 kg Bonefish, all moves, reeling on and off): the mean share outside the bobber (32 % at 7.9x). */
	constexpr float MinVisibleMean = 0.25f;

	bool LoadVisualRow(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Table, FFishVisualRow& OutRow)
	{
		FString Text;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_FishVisual.json"), Text)
			|| !LureFightQA::MakeTableChecked(Test, Table, FFishVisualRow::StaticStruct(), Text, true, TEXT("DT_FishVisual.json")))
		{
			return false;
		}
		const FFishVisualRow* Row = Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("T075"), false);
		if (!Row)
		{
			Test.AddError(TEXT("DT_FishVisual.json has no row named by ULureFishVisualSettings::VisualRow"));
			return false;
		}
		OutRow = *Row;
		return true;
	}

	/** DT_Fishing.csv row Default: the bobber's scale and its fight dip. */
	bool LoadBobberTuning(FAutomationTestBase& Test, float& OutScale, float& OutDip)
	{
		FString Csv;
		TStrongObjectPtr<UDataTable> Table;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_Fishing.csv"), Csv)
			|| !LureFightQA::MakeTableChecked(Test, Table, FLureFishingRow::StaticStruct(), Csv, false, TEXT("DT_Fishing.csv")))
		{
			return false;
		}
		const FLureFishingRow* Row = Table->FindRow<FLureFishingRow>(TEXT("Default"), TEXT("T075"), false);
		if (!Test.TestNotNull(TEXT("DT_Fishing.csv row Default"), Row))
		{
			return false;
		}
		OutScale = Row->BobberScale;
		OutDip = Row->BiteDipDepth;
		return true;
	}

	/** Signed yaw of Facing off the line mouth -> player, degrees (+ = larger yaw). */
	float OffLine(const FRotator& Facing, const FVector& Mouth, const FVector& Player)
	{
		const float Base = static_cast<float>((Player - Mouth).GetSafeNormal2D().Rotation().Yaw);
		return FMath::FindDeltaAngleDegrees(Base, static_cast<float>(Facing.Yaw));
	}

	/** Seen from above, how much nearer the player than the mouth the nearest body point is (<= 0: the mouth is nearest), cm. */
	float BodyNearerThanMouth(const FVector& Forward, const FVector& Mouth, const FVector& Player, float BodyLengthCm)
	{
		const double MouthDistance = FVector::Dist2D(Mouth, Player);
		double Worst = -UE_BIG_NUMBER;
		for (int32 Index = 1; Index <= 20; ++Index)
		{
			const FVector Point = Mouth - Forward.GetSafeNormal() * (BodyLengthCm * Index / 20.f);
			Worst = FMath::Max(Worst, MouthDistance - FVector::Dist2D(Point, Player));
		}
		return static_cast<float>(Worst);
	}

	/** The mesh's imported vertices (reference pose, component space); empty outside the editor. */
	TArray<FVector> MeshVertices(const USkeletalMesh* Asset)
	{
		TArray<FVector> Out;
#if WITH_EDITOR
		const FSkeletalMeshModel* Model = Asset ? Asset->GetImportedModel() : nullptr;
		if (Model && Model->LODModels.Num() > 0)
		{
			for (const FSkelMeshSection& Section : Model->LODModels[0].Sections)
			{
				for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
				{
					Out.Add(FVector(Vertex.Position));
				}
			}
		}
#endif
		return Out;
	}

	/** What an eye sees of the fish past the bobber (the bobber = its mesh's local box at its transform). */
	struct FSight
	{
		/** Share of the fish's vertices whose line of sight misses the bobber. */
		float Visible01 = 0.f;
		/** Largest angular extent of those vertices, degrees (on a 1008 px wide, 90 deg view: x 11.2 = pixels). */
		float SpanDeg = 0.f;
	};

	FSight Measure(const FVector& Eye, const TArray<FVector>& FishVertices, const FTransform& FishToWorld, const FBox& BobberBox, const FTransform& BobberToWorld)
	{
		FSight Sight;
		if (FishVertices.Num() == 0)
		{
			return Sight;
		}
		const FVector Forward = (FishToWorld.GetLocation() - Eye).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Forward, Right);
		const FVector EyeInBobber = BobberToWorld.InverseTransformPosition(Eye);
		FBox2D Seen(ForceInit);
		int32 Visible = 0;
		for (const FVector& Vertex : FishVertices)
		{
			const FVector Point = FishToWorld.TransformPosition(Vertex);
			const FVector PointInBobber = BobberToWorld.InverseTransformPosition(Point);
			if (!FMath::LineBoxIntersection(BobberBox, EyeInBobber, PointInBobber, PointInBobber - EyeInBobber))
			{
				++Visible;
				const FVector D = Point - Eye;
				const double Z = FVector::DotProduct(D, Forward);
				Seen += FVector2D(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(D, Right), Z)), FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(D, Up), Z)));
			}
		}
		Sight.Visible01 = static_cast<float>(Visible) / static_cast<float>(FishVertices.Num());
		Sight.SpanDeg = Seen.bIsValid ? static_cast<float>(Seen.GetSize().GetMax()) : 0.f;
		return Sight;
	}

	/** The real assets: SK_Bonefish (DT_FishSpecies Bonefish), SM_Bobber (the fishing settings' bobber). */
	struct FAssets
	{
		USkeletalMesh* Fish = nullptr;
		UStaticMesh* Bobber = nullptr;
		TArray<FVector> FishVertices;

		bool Load(FAutomationTestBase& Test)
		{
			Fish = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Fish/SK_Bonefish.SK_Bonefish"));
			Bobber = GetDefault<ULureFishingSettings>()->BobberMesh.LoadSynchronous();
			FishVertices = MeshVertices(Fish);
			return Test.TestNotNull(TEXT("SK_Bonefish (the real fish mesh)"), Fish) && Test.TestNotNull(TEXT("SM_Bobber (the real bobber)"), Bobber)
				&& Test.TestTrue(FString::Printf(TEXT("SK_Bonefish vertices (%d)"), FishVertices.Num()), FishVertices.Num() > 100);
		}
	};
}

// =====================================================================================================================
// The body-angle rules (pure)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT075BodyAngleRules, "Project.FishVisual.T075.BodyAngleRules", LureT075Test::Flags)
bool FT075BodyAngleRules::RunTest(const FString& Parameters)
{
	namespace T = LureT075Test;
	TStrongObjectPtr<UDataTable> Table;
	FFishVisualRow Row;
	if (!T::LoadVisualRow(*this, Table, Row))
	{
		return false;
	}
	FString Problem;
	TestTrue(FString::Printf(TEXT("the shipped row is valid (%s)"), *Problem), Row.Validate(Problem));
	const float Body = Row.BodyAngleDeg;
	const float Max = FFightFishVisual::MaxBodyAngleDeg(Row);
	TestTrue(FString::Printf(TEXT("BodyAngleDeg %.0f is in (0, 80]: the fish is never end-on to the rod"), Body), Body > 0.f && Body <= 80.f);
	TestEqual(TEXT("MaxBodyAngleDeg = min(80, BodyAngleDeg + RunSwingDeg)"), Max, FMath::Min(80.f, Body + Row.RunSwingDeg), 1.e-4f);
	TestTrue(TEXT("the built-in row has the same body angle (Data.VisualRowValidAndMatchesFallback compares the rest)"),
		FMath::IsNearlyEqual(FFishVisualRow::GetFallbackRow().BodyAngleDeg, Body));
	for (const float Bad : { -1.f, 81.f, NAN })
	{
		FFishVisualRow Broken = Row;
		Broken.BodyAngleDeg = Bad;
		TestFalse(FString::Printf(TEXT("BodyAngleDeg %g is refused"), Bad), Broken.Validate(Problem));
	}

	const FVector Player(0.f, 0.f, 100.f);
	const FVector Mouth(1000.f, 0.f, -25.f); // mouth -> player = yaw 180
	auto Facing = [&](const FVector& Velocity, float CurrentOff, bool bTired, bool bSwims = false, float Side = 0.f)
	{
		return FFightFishVisual::FightFacing(Row, Velocity, Mouth, Player, FRotator(0.f, 180.f + CurrentOff, 0.f), bTired, bSwims, Side);
	};
	// Not swinging: BodyAngleDeg on the side it leans to; exactly away from the player (just spawned) or end-on: the + side.
	TestEqual(TEXT("still, leaning +: +BodyAngleDeg"), T::OffLine(Facing(FVector::ZeroVector, 20.f, false), Mouth, Player), Body, 0.01f);
	TestEqual(TEXT("still, leaning -: -BodyAngleDeg"), T::OffLine(Facing(FVector::ZeroVector, -20.f, false), Mouth, Player), -Body, 0.01f);
	TestEqual(TEXT("still, end-on: +BodyAngleDeg"), T::OffLine(Facing(FVector::ZeroVector, 0.f, false), Mouth, Player), Body, 0.01f);
	TestEqual(TEXT("just spawned (faces away, +180): +BodyAngleDeg"), T::OffLine(Facing(FVector::ZeroVector, 180.f, false), Mouth, Player), Body, 0.01f);
	TestEqual(TEXT("just spawned (faces away, -180): +BodyAngleDeg (the same on every machine)"), T::OffLine(Facing(FVector::ZeroVector, -180.f, false), Mouth, Player), Body, 0.01f);
	TestEqual(TEXT("a move with no side (Shake, Charge, Dive): BodyAngleDeg on its lean"), T::OffLine(Facing(FVector::ZeroVector, -30.f, false, true, 0.f), Mouth, Player), -Body, 0.01f);
	const FRotator Tired = Facing(FVector(0.f, 400.f, 0.f), -10.f, true, true, 1.f);
	TestEqual(TEXT("tired, dragged sideways, even while a move swims: BodyAngleDeg, no swing"), T::OffLine(Tired, Mouth, Player), -Body, 0.01f);
	TestEqual(TEXT("tired: roll ExhaustedRollDeg (upright)"), static_cast<float>(Tired.Roll), Row.ExhaustedRollDeg, 0.01f);
	// Swinging: BodyAngleDeg + the swing (capped at 80), toward the swim side.
	TestEqual(TEXT("a move swimming to the player's right (Swim, 0.58): -(BodyAngleDeg + 0.58 x RunSwingDeg), head to the right"),
		T::OffLine(Facing(FVector::ZeroVector, 30.f, false, true, 0.58f), Mouth, Player), -FMath::Min(80.f, Body + 0.58f * Row.RunSwingDeg), 0.01f);
	TestEqual(TEXT("a move swimming to the player's left (Run, 0.29): +(BodyAngleDeg + 0.29 x RunSwingDeg)"),
		T::OffLine(Facing(FVector::ZeroVector, -30.f, false, true, -0.29f), Mouth, Player), FMath::Min(80.f, Body + 0.29f * Row.RunSwingDeg), 0.01f);
	TestEqual(TEXT("Rest dragged to -Y at full swing speed: head to -Y (larger yaw here) by BodyAngleDeg + RunSwingDeg, capped at 80"),
		T::OffLine(Facing(FVector(0.f, -Row.RunSwingFullSpeed * 2.f, 0.f), -30.f, false), Mouth, Player), Max, 0.01f);
	// A side switch is now a big turn (2 x BodyAngleDeg and more), so a fish reeled straight in must not flip-flop on a small
	// sideways wobble: under a quarter of its speed sideways it keeps the side it leans to.
	for (const float Wobble : { 0.2f, -0.2f })
	{
		const FVector ReeledIn(-150.f, Wobble * 150.f, 0.f);
		TestTrue(FString::Printf(TEXT("reeled in, sideways wobble %+.1f, leaning +: stays + (%.1f)"), Wobble, T::OffLine(Facing(ReeledIn, 20.f, false), Mouth, Player)),
			T::OffLine(Facing(ReeledIn, 20.f, false), Mouth, Player) >= Body - 0.01f);
		TestTrue(FString::Printf(TEXT("reeled in, sideways wobble %+.1f, leaning -: stays - (%.1f)"), Wobble, T::OffLine(Facing(ReeledIn, -20.f, false), Mouth, Player)),
			T::OffLine(Facing(ReeledIn, -20.f, false), Mouth, Player) <= -Body + 0.01f);
	}
	// BodyAngleDeg 0 is the T-048 rule.
	FFishVisualRow Old = Row;
	Old.BodyAngleDeg = 0.f;
	TestEqual(TEXT("BodyAngleDeg 0: still = straight toward the rod (T-048)"),
		T::OffLine(FFightFishVisual::FightFacing(Old, FVector::ZeroVector, Mouth, Player, FRotator(0.f, 200.f, 0.f), false), Mouth, Player), 0.f, 0.01f);
	TestEqual(TEXT("BodyAngleDeg 0: a move's swing alone (T-048b)"),
		T::OffLine(FFightFishVisual::FightFacing(Old, FVector::ZeroVector, Mouth, Player, FRotator(0.f, 180.f, 0.f), false, true, 0.58f), Mouth, Player), -0.58f * Row.RunSwingDeg, 0.01f);
	TestEqual(TEXT("ClampToLine keeps the yaw within MaxBodyAngleDeg"), FMath::Abs(T::OffLine(FFightFishVisual::ClampToLine(Row, FRotator(0.f, 180.f + 120.f, 0.f), Mouth, Player), Mouth, Player)), Max, 0.01f);
	TestEqual(TEXT("ClampToLine keeps a yaw inside it"), T::OffLine(FFightFishVisual::ClampToLine(Row, FRotator(0.f, 180.f - Body, 0.f), Mouth, Player), Mouth, Player), -Body, 0.01f);

	// Every case: (a) the mouth is at the line end, (b) BodyAngleDeg <= |yaw off the line| <= MaxBodyAngleDeg, (c) no body point
	// nearer the player than the mouth (seen from above).
	int32 Cases = 0;
	float WorstMouth = 0.f;
	float LeastOff = UE_BIG_NUMBER;
	float MostOff = 0.f;
	float WorstBody = -UE_BIG_NUMBER;
	for (const float SideDeg : { -70.f, 0.f, 45.f })
	{
		const FVector Away = FVector::ForwardVector.RotateAngleAxis(SideDeg, FVector::UpVector);
		const FVector Perp(-Away.Y, Away.X, 0.f);
		FFightFishView View;
		View.bFighting = true;
		View.PlayerLocation = Player;
		View.LineEnd = FVector(Player.X, Player.Y, 0.f) + Away * 1200.f;
		for (const FVector& Velocity : { FVector::ZeroVector, Away * 300.f, -Away * 150.f, Perp * 250.f, -Perp * 60.f, Away * 300.f + FVector(0.f, 0.f, -200.f) })
		{
			for (const bool bTired : { false, true })
			{
				for (const float MoveSide : { 0.f, 0.29f, -0.58f, 0.98f })
				{
					for (const float CurrentOff : { 0.f, 70.f, -70.f, 180.f })
					{
						const FVector MouthPoint = FFightFishVisual::MouthTarget(Row, View, -UE_BIG_NUMBER);
						const float BaseYaw = static_cast<float>((Player - MouthPoint).GetSafeNormal2D().Rotation().Yaw);
						const FRotator Desired = FFightFishVisual::FightFacing(Row, Velocity, MouthPoint, Player, FRotator(0.f, BaseYaw + CurrentOff, 0.f), bTired, MoveSide != 0.f, MoveSide);
						const FRotator Final = FFightFishVisual::ClampToLine(Row, Desired, MouthPoint, Player);
						const float Off = FMath::Abs(T::OffLine(Final, MouthPoint, Player));
						LeastOff = FMath::Min(LeastOff, Off);
						MostOff = FMath::Max(MostOff, Off);
						for (const float Offset : { 16.f, 26.3f, 60.f })
						{
							const FVector Center = FFightFishVisual::CenterForMouth(MouthPoint, Final.Vector(), Offset);
							const FVector MouthNow = Center + Final.Vector() * Offset; // the nose of the (maybe pitched) body
							WorstMouth = FMath::Max(WorstMouth, static_cast<float>(FVector::Dist2D(MouthNow, View.LineEnd)));
							WorstBody = FMath::Max(WorstBody, T::BodyNearerThanMouth(Final.Vector(), MouthNow, Player, 2.f * Offset));
							++Cases;
						}
					}
				}
			}
		}
	}
	TestTrue(FString::Printf(TEXT("cases: %d"), Cases), Cases > 800);
	TestTrue(FString::Printf(TEXT("(a) the mouth is at the line end (worst %.3f cm)"), WorstMouth), WorstMouth <= 0.5f);
	TestTrue(FString::Printf(TEXT("(b) never less than BodyAngleDeg %.0f off the line (least %.2f)"), Body, LeastOff), LeastOff >= Body - 0.01f);
	TestTrue(FString::Printf(TEXT("(b) never more than MaxBodyAngleDeg %.0f (most %.2f)"), Max, MostOff), MostOff <= Max + 0.01f);
	TestTrue(FString::Printf(TEXT("(c) no body point nearer the player than the mouth (worst %.3f cm nearer)"), WorstBody), WorstBody <= 0.01f);
	return true;
}

// =====================================================================================================================
// Seen from the rod at the dock end: 4, 10 and 18 m (the playtest's distances), small and big Bonefish
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT075VisibleFromTheDock, "Project.FishVisual.T075.VisibleFromTheDock", LureT075Test::Flags)
bool FT075VisibleFromTheDock::RunTest(const FString& Parameters)
{
	namespace T = LureT075Test;
	TStrongObjectPtr<UDataTable> Table;
	FFishVisualRow Row;
	float BobberScale = 1.f;
	float BiteDip = 0.f;
	T::FAssets Assets;
	LureFightQA::FWorld World;
	if (!T::LoadVisualRow(*this, Table, Row) || !T::LoadBobberTuning(*this, BobberScale, BiteDip) || !Assets.Load(*this) || !World.Create(*this))
	{
		return false;
	}
	FFishVisualRow Old = Row;
	Old.BodyAngleDeg = 0.f; // the T-048 rule the playtest ran with
	const FBox BobberBox = Assets.Bobber->GetBoundingBox();
	const FVector Player(0.f, 3000.f, 100.f); // away from the test dock
	const FVector Eye(Player.X, Player.Y, T::DockEyeHeight);
	struct FCase { const TCHAR* Name; FName MoveId; bool bSwims; float Side; bool bTired; };
	const FCase Cases[] = {
		{ TEXT("resting"), TEXT("Rest"), false, 0.f, false },
		{ TEXT("head shake"), TEXT("Shake"), false, 0.f, false },
		{ TEXT("running left"), TEXT("Run"), true, -0.29f, false },
		{ TEXT("tired"), NAME_None, false, 0.f, true } };
	int32 Checked = 0;
	for (const float WeightKg : { 0.6f, 3.f })
	{
		for (const float Distance : { 400.f, 1000.f, 1800.f })
		{
			for (const FCase& Case : Cases)
			{
				float OldVisible = 0.f;
				for (const bool bShipped : { true, false })
				{
					FLureFightFishSetup Setup;
					Setup.Row = bShipped ? Row : Old;
					Setup.Fish.SpeciesId = TEXT("Bonefish");
					Setup.Fish.WeightKg = WeightKg;
					Setup.ReferenceWeightKg = 1.5f;
					Setup.Mesh = Assets.Fish;
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					const FVector LineEnd(Player.X + Distance, Player.Y, 0.f);
					// Spawned facing away from the player, as ULureFightFishSubsystem spawns it.
					ALureFightFish* Fish = World.World->SpawnActor<ALureFightFish>(ALureFightFish::StaticClass(), FTransform(FRotator::ZeroRotator, LineEnd), Params);
					if (!TestNotNull(TEXT("fish"), Fish))
					{
						return false;
					}
					Fish->Setup(Setup);
					FFightFishView View;
					View.bFighting = true;
					View.FightId = 1;
					View.bHasAuthority = true;
					View.PlayerLocation = Player;
					View.WaterZ = 0.f;
					View.LineEnd = LineEnd;
					View.MoveId = Case.MoveId;
					View.bMoveSwims = Case.bSwims;
					View.MoveSwimSide = Case.Side;
					View.bExhausted = Case.bTired;
					Fish->ApplyView(View, 0.f);
					for (int32 Frame = 0; Frame < 90; ++Frame)
					{
						Fish->ApplyView(View, LureFightQA::WorldDt);
					}
					// The bobber as ULureFishingComponent draws it in a fight at half tension: over the mouth, pulled under,
					// tipped 20 deg, facing along the line.
					const FVector Mouth = Fish->GetMouthLocation();
					const FTransform Bobber(FRotator(20.f, (LineEnd - Player).GetSafeNormal2D().Rotation().Yaw, 0.f),
						FVector(LineEnd.X, LineEnd.Y, -0.75f * BiteDip), FVector(BobberScale));
					const T::FSight Sight = T::Measure(Eye, Assets.FishVertices, Fish->GetMesh()->GetComponentTransform(), BobberBox, Bobber);
					const FString What = FString::Printf(TEXT("%.1f kg at %.0f m, %s"), WeightKg, Distance / 100.f, Case.Name);
					if (bShipped)
					{
						AddInfo(FString::Printf(TEXT("%s: %.0f%% of the fish outside the bobber (x%.1f), %.2f deg wide"), *What, 100.f * Sight.Visible01, BobberScale, Sight.SpanDeg));
						TestTrue(FString::Printf(TEXT("%s: the mouth is under the bobber (%.1f cm)"), *What, FVector::Dist2D(Mouth, LineEnd)), FVector::Dist2D(Mouth, LineEnd) <= Row.MouthMaxLagCm + 2.f);
						const float Min = WeightKg >= 3.f ? T::MinVisibleBig : T::MinVisible;
						TestTrue(FString::Printf(TEXT("%s: %.0f%% of the fish outside the bobber, %.2f deg wide (min %.0f%%)"), *What, 100.f * Sight.Visible01, Sight.SpanDeg, 100.f * Min),
							Sight.Visible01 >= Min);
						TestTrue(FString::Printf(TEXT("%s: body BodyAngleDeg or more off the line (%.1f)"), *What, FMath::Abs(T::OffLine(Fish->GetActorRotation(), Mouth, Player))),
							FMath::Abs(T::OffLine(Fish->GetActorRotation(), Mouth, Player)) >= Row.BodyAngleDeg - 0.5f);
						++Checked;
					}
					else
					{
						OldVisible = Sight.Visible01;
					}
					Fish->Destroy();
				}
				AddInfo(FString::Printf(TEXT("%.1f kg at %.0f m, %s: T-048 rule %.0f%% visible"), WeightKg, Distance / 100.f, Case.Name, 100.f * OldVisible));
				if (Distance >= 1000.f && !Case.bSwims)
				{
					// Why T-075 exists: with the T-048 rule (straight away from the rod) the bobber hides the fish.
					TestTrue(FString::Printf(TEXT("%.1f kg at %.0f m, %s, T-048 rule: hidden behind the bobber (%.0f%% visible, under the %.0f%% floor)"), WeightKg, Distance / 100.f, Case.Name, 100.f * OldVisible, 100.f * T::MinVisible),
						OldVisible < T::MinVisible);
				}
			}
		}
	}
	TestEqual(TEXT("cases checked"), Checked, 2 * 3 * 4);
	return true;
}

// =====================================================================================================================
// A real fight: the fish actor exists, is drawn, its mouth is under the bobber, and the angler sees it past the bobber
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FT075RealFight, "Project.FishVisual.T075.VisibleInARealFight", LureT075Test::Flags)
bool FT075RealFight::RunTest(const FString& Parameters)
{
	namespace T = LureT075Test;
	LureFightQA::FFightTables Data;
	FishQA::FTables Fish;
	TStrongObjectPtr<UDataTable> Visual;
	FFishVisualRow Row;
	T::FAssets Assets;
	if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !T::LoadVisualRow(*this, Visual, Row) || !Assets.Load(*this))
	{
		return false;
	}
	FFishInstance Bonefish;
	if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.1f, 481, Bonefish))
	{
		return false;
	}
	LureFightQA::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
	ALurePlayerCharacter* Angler = World.Spawn(LureFightQA::StandAt());
	ULureFishingComponent* Fishing = LureFightQA::SetUpFishing(Angler, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
	if (!TestNotNull(TEXT("fight fish subsystem"), Visuals) || !TestNotNull(TEXT("fishing"), Fishing) || !TestNotNull(TEXT("angler camera"), Angler->GetFirstPersonCamera()))
	{
		return false;
	}
	Visuals->SetTables(Fish.Species.Get(), Visual.Get());
	if (!LureFightQA::CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hooked"), Fishing->AuthorityHookFish(Bonefish)))
	{
		return false;
	}
	int32 Samples = 0;
	int32 Seen = 0;
	int32 Drawn = 0;
	float VisibleSum = 0.f;
	TArray<float> Shares;
	// Side switches of the body (clearly on one side: 30 deg or more off the line) against the fight's own move changes.
	int32 BodySide = 0;
	int32 BodySwitches = 0;
	int32 MoveChanges = 0;
	FName LastMove = NAME_None;
	int32 FightFrames = 0;
	float WorstMouth = 0.f;
	float WorstDepth = 0.f;
	for (int32 Frame = 0; Frame < 14 * 60 && Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
	{
		Fishing->AuthoritySetReeling((Frame / 90) % 2 == 1); // 1.5 s runs, 1.5 s reeling
		World.Tick(1);
		const FFightFishView View = FFightFishViewAdapter::FromComponent(*Fishing);
		const ALureFightFish* Actor = Visuals->FindFish(Fishing);
		const UStaticMeshComponent* Bobber = Fishing->GetBobberMesh();
		if (View.bFighting && Actor)
		{
			++FightFrames;
			MoveChanges += (FightFrames > 1 && View.MoveId != LastMove) ? 1 : 0;
			LastMove = View.MoveId;
			const float Off = T::OffLine(Actor->GetActorRotation(), Actor->GetMouthLocation(), View.PlayerLocation);
			const int32 Side = Off >= 30.f ? 1 : (Off <= -30.f ? -1 : 0);
			BodySwitches += (Side != 0 && BodySide != 0 && Side != BodySide) ? 1 : 0;
			BodySide = Side != 0 ? Side : BodySide;
		}
		if (!View.bFighting || Frame % 10 != 0)
		{
			continue;
		}
		if (!TestNotNull(TEXT("a fighting fish is on screen: its actor"), Actor) || !TestNotNull(TEXT("the bobber"), Bobber)
			|| !TestNotNull(TEXT("the bobber's mesh"), Bobber->GetStaticMesh().Get()))
		{
			return false;
		}
		const USkeletalMeshComponent* Mesh = Actor->GetMesh();
		Drawn += (Mesh->GetSkeletalMeshAsset() == Assets.Fish && Mesh->IsVisible() && !Mesh->bHiddenInGame && !Actor->IsHidden()) ? 1 : 0;
		const FVector Mouth = Actor->GetMouthLocation();
		WorstMouth = FMath::Max(WorstMouth, static_cast<float>(FVector::Dist2D(Mouth, Fishing->GetBobberLocation())));
		// The placement target (before smoothing): ShownDepth under the (lifted) surface; no seabed under this test's water.
		WorstDepth = FMath::Max(WorstDepth, FMath::Abs(static_cast<float>(View.WaterZ - Actor->GetLastTarget().Z) - FFightFishVisual::ShownDepth(Row, View.DepthCm)));
		const T::FSight Sight = T::Measure(Angler->GetFirstPersonCamera()->GetComponentLocation(), Assets.FishVertices, Mesh->GetComponentTransform(),
			Bobber->GetStaticMesh()->GetBoundingBox(), Bobber->GetComponentTransform());
		VisibleSum += Sight.Visible01;
		Seen += Sight.Visible01 >= T::MinVisible ? 1 : 0;
		Shares.Add(Sight.Visible01);
		++Samples;
	}
	Shares.Sort();
	TestTrue(FString::Printf(TEXT("fought for a while (%d samples)"), Samples), Samples >= 40);
	TestEqual(TEXT("every sample: SK_Bonefish drawn (visible, not hidden)"), Drawn, Samples);
	TestTrue(FString::Printf(TEXT("the mouth stays under the bobber (worst %.1f cm)"), WorstMouth), WorstMouth <= Row.MouthMaxLagCm + 2.f);
	TestTrue(FString::Printf(TEXT("the fish is ShownDepth under the surface (worst off %.1f cm)"), WorstDepth), WorstDepth <= 1.f);
	const float Mean = Samples > 0 ? VisibleSum / Samples : 0.f;
	AddInfo(FString::Printf(TEXT("%.2f kg Bonefish: outside the bobber %.0f%% on average, least %.0f%%, 10th percentile %.0f%%, median %.0f%%"),
		Bonefish.WeightKg, 100.f * Mean, Shares.Num() > 0 ? 100.f * Shares[0] : 0.f, Shares.Num() > 0 ? 100.f * Shares[Shares.Num() / 10] : 0.f,
		Shares.Num() > 0 ? 100.f * Shares[Shares.Num() / 2] : 0.f));
	TestTrue(FString::Printf(TEXT("seen from the angler's camera past the bobber in %d of %d samples (at least %.0f%% of the fish)"), Seen, Samples, 100.f * T::MinVisible),
		Samples > 0 && Seen >= FMath::CeilToInt(0.9f * Samples));
	TestTrue(FString::Printf(TEXT("on average %.0f%% of the fish is outside the bobber (min %.0f%%)"), 100.f * Mean, 100.f * T::MinVisibleMean), Mean >= T::MinVisibleMean);
	AddInfo(FString::Printf(TEXT("body side switches %d, move changes %d, in %.1f s of fight"), BodySwitches, MoveChanges, FightFrames * LureFightQA::WorldDt));
	TestTrue(FString::Printf(TEXT("no flip-flopping: the body changes side only with the fight's moves (%d switches, %d move changes)"), BodySwitches, MoveChanges),
		BodySwitches <= MoveChanges + 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

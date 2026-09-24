// Tests for the fish you see fighting on the line (T-029): Project.FishVisual.*
// Data from the text sources (data/tables/DT_FishVisual.json, DT_FishSpecies.json, DT_FightPattern.json), never the
// binary assets. Worlds are LureFightQA::FWorld (an 8 x 8 m dock over a Lure.Water surface at z = 0). A "proxy" is a copy
// of a fishing component whose owner has ROLE_SimulatedProxy, fed by LureFightQA::ReplicateFishing (the engine's
// replication layout).

#include "Tests/FishFight/FightQATestUtils.h"
#include "Tests/FishVisual/FightFishVisualQATestUtils.h" // LureFightFishQA::LandedFishHandedOff / ClearHangingCatch (was only reachable through the unity blob)

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/FishVisualSettings.h"
#include "Fish/LureFightFish.h"
#include "Fish/LureFightFishSubsystem.h"
#include "Fishing/FightFishViewAdapter.h"
#include "Fishing/LureRodControl.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/PackageName.h"
#include <limits>

namespace LureFightFishTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	inline FString RoleName(EFishAnimRole Role)
	{
		return StaticEnum<EFishAnimRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	/** data/tables/DT_FishVisual.json as a transient table (import problems are test errors). */
	inline bool LoadVisual(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, FString* OutText = nullptr)
	{
		FString Text;
		if (!LureFightQA::ReadSource(Test, TEXT("DT_FishVisual.json"), Text))
		{
			return false;
		}
		if (OutText)
		{
			*OutText = Text;
		}
		return LureFightQA::MakeTableChecked(Test, Out, FFishVisualRow::StaticStruct(), Text, true, TEXT("DT_FishVisual.json"));
	}

	inline const FFishVisualRow* DefaultRow(FAutomationTestBase& Test, const TStrongObjectPtr<UDataTable>& Table)
	{
		const FFishVisualRow* Row = Table.IsValid() ? Table->FindRow<FFishVisualRow>(GetDefault<ULureFishVisualSettings>()->VisualRow, TEXT("FishVisualTest"), false) : nullptr;
		if (!Row)
		{
			Test.AddError(TEXT("DT_FishVisual.json has no row named by ULureFishVisualSettings::VisualRow (Default)"));
		}
		return Row;
	}

	/** A fighting view: the player at the dock, the line end out along +X at the water surface (z = 0). */
	inline FFightFishView MakeView(const FVector& LineEnd, FName MoveId, float Depth = 0.f, bool bAuthority = true)
	{
		FFightFishView View;
		View.bFighting = true;
		View.FightId = 1;
		View.MoveId = MoveId;
		View.DepthCm = Depth;
		View.PlayerLocation = FVector(0.f, 0.f, 100.f);
		View.LineEnd = FVector(LineEnd.X, LineEnd.Y, 0.f);
		View.WaterZ = 0.f;
		View.bHasAuthority = bAuthority;
		View.Fish.SpeciesId = TEXT("Bonefish");
		View.Fish.WeightKg = 1.5f;
		return View;
	}

	/** A fish actor without a mesh (tests never need the binary assets), weight = reference weight unless given. */
	inline ALureFightFish* SpawnBareFish(UWorld* World, const FFishVisualRow& Row, float WeightKg = 1.5f, float ReferenceWeightKg = 1.5f)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALureFightFish* Fish = World ? World->SpawnActor<ALureFightFish>(ALureFightFish::StaticClass(), FTransform::Identity, Params) : nullptr;
		if (Fish)
		{
			FLureFightFishSetup Setup;
			Setup.Row = Row;
			Setup.Fish.SpeciesId = TEXT("Bonefish");
			Setup.Fish.WeightKg = WeightKg;
			Setup.ReferenceWeightKg = ReferenceWeightKg;
			Fish->Setup(Setup);
		}
		return Fish;
	}

	/** Makes Character a copy on another machine: no authority, and it never moves on its own. */
	inline void MakeCopy(ALurePlayerCharacter* Character, ENetRole Role)
	{
		Character->GetCharacterMovement()->SetComponentTickEnabled(false);
		Character->SetRole(Role);
	}

	// =================================================================================================================
	// Clip state (the ABP_Fish values) from each fight move
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualAnimStateFromMoves, "Project.FishVisual.Anim.StateFromEachFightMove", Flags)
	bool FFishVisualAnimStateFromMoves::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		if (!LoadVisual(*this, Table))
		{
			return false;
		}
		const FFishVisualRow* RowPtr = DefaultRow(*this, Table);
		if (!RowPtr)
		{
			return false;
		}
		const FFishVisualRow& Row = *RowPtr;

		FFightFishAnimInput In;
		In.SecondsSinceHook = 5.f;
		In.SpeedCmS = 60.f;
		In.BodyLengthCm = 53.5f;
		In.WeightKg = 1.5f;
		In.ReferenceWeightKg = 1.5f;

		// Each shipped fight move plays its clip (SK_Fish.anim.md table).
		const TPair<const TCHAR*, EFishAnimRole> Expected[] = {
			{ TEXT("Run"), EFishAnimRole::Run }, { TEXT("Dive"), EFishAnimRole::Dive }, { TEXT("Dart"), EFishAnimRole::Dart },
			{ TEXT("Swim"), EFishAnimRole::SwimFast }, { TEXT("Charge"), EFishAnimRole::SwimFast }, { TEXT("Rest"), EFishAnimRole::SwimIdle },
			{ TEXT("Sulk"), EFishAnimRole::Thrash } };
		for (const TPair<const TCHAR*, EFishAnimRole>& Case : Expected)
		{
			In.MoveId = Case.Key;
			TestEqual(FString::Printf(TEXT("move %s plays %s"), Case.Key, *RoleName(Case.Value)), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(Case.Value));
		}
		In.MoveId = TEXT("QA_BrandNewMove");
		TestEqual(TEXT("an unknown move plays UnknownMoveRole (SwimFast)"), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::SwimFast));

		// Every move of the shipped DT_FightPattern is mapped explicitly (a new move wants a MoveRoles line, not a guess).
		TStrongObjectPtr<UDataTable> Patterns;
		FString PatternJson;
		if (LureFightQA::ReadSource(*this, TEXT("DT_FightPattern.json"), PatternJson)
			&& LureFightQA::MakeTableChecked(*this, Patterns, FLureFightPatternRow::StaticStruct(), PatternJson, true, TEXT("DT_FightPattern.json")))
		{
			Patterns->ForeachRow<FLureFightPatternRow>(TEXT("FishVisualTest"), [this, &Row](const FName& Name, const FLureFightPatternRow& Pattern)
			{
				for (const FLureFightMove& Move : Pattern.Moves)
				{
					if (!Row.MoveRoles.ContainsByPredicate([&Move](const FFishMoveAnimRole& Entry) { return Entry.MoveId == Move.Id; }))
					{
						AddWarning(FString::Printf(TEXT("DT_FightPattern %s move %s has no DT_FishVisual MoveRoles line (plays %s)"), *Name.ToString(),
							*Move.Id.ToString(), *RoleName(Row.UnknownMoveRole)));
					}
				}
			});
		}

		// The hook set thrashes first, whatever the move.
		In.MoveId = TEXT("Run");
		In.SecondsSinceHook = 0.5f * Row.HookSetThrashTime;
		TestEqual(TEXT("right after the hook set: Thrash"), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::Thrash));
		In.SecondsSinceHook = Row.HookSetThrashTime + 0.01f;
		TestEqual(TEXT("after HookSetThrashTime: the move's clip"), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::Run));

		// Tired: calm swim, slow, half alpha.
		In.AnimAmplitude = 0.8f;
		In.bExhausted = true;
		FFishAnimState State = FFightFishVisual::ComputeAnimState(Row, In);
		TestEqual(TEXT("exhausted: SwimIdle"), RoleName(State.Role), RoleName(EFishAnimRole::SwimIdle));
		TestEqual(TEXT("exhausted: ExhaustedPlayRate"), State.PlayRate, Row.ExhaustedPlayRate, 1.e-5f);
		TestEqual(TEXT("exhausted: amplitude x ExhaustedAmplitudeScale"), State.Amplitude, 0.8f * Row.ExhaustedAmplitudeScale, 1.e-5f);
		In.bExhausted = false;

		// Species amplitude on fight clips; the flop always at alpha 1 (dock clearance).
		TestEqual(TEXT("fight clip: the species' AnimAmplitude"), FFightFishVisual::ComputeAnimState(Row, In).Amplitude, 0.8f, 1.e-5f);
		In.Phase = EFightFishPhase::Landed;
		State = FFightFishVisual::ComputeAnimState(Row, In);
		TestEqual(TEXT("landed: Flop"), RoleName(State.Role), RoleName(EFishAnimRole::Flop));
		TestEqual(TEXT("landed: alpha 1 even for a 0.8 species"), State.Amplitude, 1.f, 1.e-6f);
		In.Phase = EFightFishPhase::Escaping;
		TestEqual(TEXT("escaping: SwimFast"), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::SwimFast));
		In.Phase = EFightFishPhase::Fighting;
		In.AnimAmplitude = 1.f;

		// Swim roles: tail beat matched to speed. A reference Bonefish (53.5 cm) at 94 cm/s in Swim_Fast (2.5 Hz) is rate ~1.
		In.MoveId = TEXT("Swim");
		In.SpeedCmS = 94.f;
		TestEqual(TEXT("swim rate = Speed / (0.7 x L x Hz)"), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, 94.f / (0.7f * 53.5f * 2.5f), 1.e-4f);
		In.AnimRate = 1.15f;
		TestEqual(TEXT("... x the species' AnimRate"), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, 1.15f * 94.f / (0.7f * 53.5f * 2.5f), 1.e-4f);
		In.AnimRate = 1.f;
		In.SpeedCmS = 0.f;
		TestEqual(TEXT("a still fish: MinPlayRate"), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, Row.MinPlayRate, 1.e-6f);
		In.SpeedCmS = 100000.f;
		TestEqual(TEXT("a rocket: MaxPlayRate"), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, Row.MaxPlayRate, 1.e-6f);

		// Other roles: heavier = slower, (Ref / Weight)^(1/6).
		In.MoveId = TEXT("Sulk");
		In.WeightKg = 3.f;
		TestEqual(TEXT("a fish twice the reference weight thrashes ~11 % slower"), FFightFishVisual::ComputeAnimState(Row, In).PlayRate,
			FMath::Pow(0.5f, Row.OtherRateWeightExponent), 1.e-5f);
		In.WeightKg = 1.5f;

		// Dart: which half of the clip starts.
		In.MoveId = TEXT("Dart");
		In.bDartRight = false;
		TestEqual(TEXT("dart to the left: start 0"), FFightFishVisual::ComputeAnimState(Row, In).DartStartTime, 0.f, 1.e-6f);
		In.bDartRight = true;
		TestEqual(TEXT("dart to the right: DartRightStartTime"), FFightFishVisual::ComputeAnimState(Row, In).DartStartTime, Row.DartRightStartTime, 1.e-6f);
		In.MoveId = TEXT("Run");
		TestEqual(TEXT("not a dart: start 0"), FFightFishVisual::ComputeAnimState(Row, In).DartStartTime, 0.f, 1.e-6f);
		TestEqual(TEXT("blend time from the row"), FFightFishVisual::ComputeAnimState(Row, In).RoleBlendTime, Row.RoleBlendTime, 1.e-6f);
		return true;
	}

	/** UFishAnimInstance (the parent of ABP_Fish) takes its values from the fish actor it animates, and clamps what it is given. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualAnimInstance, "Project.FishVisual.Anim.InstanceReadsFishActor", Flags)
	bool FFishVisualAnimInstance::RunTest(const FString& Parameters)
	{
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		const FFishVisualRow Row = FFishVisualRow::GetFallbackRow();
		ALureFightFish* Fish = SpawnBareFish(World.World, Row);
		if (!TestNotNull(TEXT("fish actor"), Fish))
		{
			return false;
		}
		FFightFishView View = MakeView(FVector(2000.f, 0.f, 0.f), TEXT("Dive"), 50.f);
		Fish->ApplyView(View, 0.f);
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			Fish->ApplyView(View, LureFightQA::WorldDt);
		}
		TestEqual(TEXT("the actor plays the move's clip after the hook set"), RoleName(Fish->GetAnimState().Role), RoleName(EFishAnimRole::Dive));

		UFishAnimInstance* Anim = NewObject<UFishAnimInstance>(Fish->GetMesh());
		TestTrue(TEXT("UpdateFromOwner finds the fish actor"), Anim->UpdateFromOwner());
		const FFishAnimState Expected = Fish->GetAnimState();
		TestEqual(TEXT("Role"), RoleName(Anim->Role), RoleName(Expected.Role));
		TestEqual(TEXT("PlayRate"), Anim->PlayRate, Expected.PlayRate, 1.e-6f);
		TestEqual(TEXT("Amplitude"), Anim->Amplitude, Expected.Amplitude, 1.e-6f);
		TestEqual(TEXT("RoleBlendTime"), Anim->RoleBlendTime, Expected.RoleBlendTime, 1.e-6f);
		TestEqual(TEXT("DartStartTime"), Anim->DartStartTime, Expected.DartStartTime, 1.e-6f);

		FFishAnimState Wild;
		Wild.Role = EFishAnimRole::Flop;
		Wild.Amplitude = 3.f;
		Wild.PlayRate = -1.f;
		Wild.RoleBlendTime = std::numeric_limits<float>::quiet_NaN();
		Anim->SetAnimState(Wild);
		TestEqual(TEXT("SetAnimState: role"), RoleName(Anim->GetAnimState().Role), RoleName(EFishAnimRole::Flop));
		TestEqual(TEXT("SetAnimState: amplitude clamped to 1"), Anim->Amplitude, 1.f, 1.e-6f);
		TestEqual(TEXT("SetAnimState: play rate never negative"), Anim->PlayRate, 0.f, 1.e-6f);
		TestTrue(TEXT("SetAnimState: a NaN blend time is replaced"), FMath::IsFinite(Anim->RoleBlendTime));

		UFishAnimInstance* Orphan = NewObject<UFishAnimInstance>(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
		TestFalse(TEXT("no fish actor as owner: nothing to read"), Orphan->UpdateFromOwner());
		return true;
	}

	// =================================================================================================================
	// Size
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualWeightScale, "Project.FishVisual.Size.ScaleFromWeight", Flags)
	bool FFishVisualWeightScale::RunTest(const FString& Parameters)
	{
		const FFishVisualRow Row = FFishVisualRow::GetFallbackRow();
		TestEqual(TEXT("reference weight: scale 1"), FFightFishVisual::WeightScale(1.5f, 1.5f, Row), 1.f, 1.e-6f);
		TestEqual(TEXT("8 x the weight: twice the size"), FFightFishVisual::WeightScale(12.f, 1.5f, Row), 2.f, 1.e-5f);
		TestEqual(TEXT("1/8 of the weight: half the size"), FFightFishVisual::WeightScale(0.1875f, 1.5f, Row), 0.5f, 1.e-5f);
		TestEqual(TEXT("(W / R)^(1/3) in general"), FFightFishVisual::WeightScale(4.5f, 2.5f, Row), FMath::Pow(4.5f / 2.5f, 1.f / 3.f), 1.e-5f);
		TestEqual(TEXT("zero weight: 1"), FFightFishVisual::WeightScale(0.f, 1.5f, Row), 1.f, 1.e-6f);
		TestEqual(TEXT("zero reference: 1"), FFightFishVisual::WeightScale(2.f, 0.f, Row), 1.f, 1.e-6f);
		TestEqual(TEXT("NaN weight: 1"), FFightFishVisual::WeightScale(std::numeric_limits<float>::quiet_NaN(), 1.5f, Row), 1.f, 1.e-6f);
		TestEqual(TEXT("absurdly heavy: MaxScale"), FFightFishVisual::WeightScale(1.0e6f, 1.f, Row), Row.MaxScale, 1.e-6f);
		TestEqual(TEXT("absurdly light: MinScale"), FFightFishVisual::WeightScale(1.0e-6f, 1.f, Row), Row.MinScale, 1.e-6f);

		// The actor applies it, from the species' ReferenceWeight in the shipped data.
		FishQA::FTables Fish;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !World.Create(*this))
		{
			return false;
		}
		for (const TCHAR* SpeciesId : { TEXT("Bonefish"), TEXT("CoralSnapper") })
		{
			const FFishSpeciesRow* Species = FishQA::Row<FFishSpeciesRow>(Fish.Species, SpeciesId);
			if (!TestNotNull(FString::Printf(TEXT("species %s"), SpeciesId), Species))
			{
				continue;
			}
			for (const float Fraction : { 0.f, 0.5f, 1.f })
			{
				const float Weight = FMath::Lerp(Species->WeightMin, Species->WeightMax, Fraction);
				ALureFightFish* Actor = SpawnBareFish(World.World, Row, Weight, Species->ReferenceWeight);
				if (!TestNotNull(TEXT("fish actor"), Actor))
				{
					continue;
				}
				const float Expected = FMath::Pow(Weight / Species->ReferenceWeight, 1.f / 3.f);
				TestEqual(FString::Printf(TEXT("%s %.2f kg: scale"), SpeciesId, Weight), Actor->GetFishScale(), Expected, 1.e-4f);
				TestTrue(FString::Printf(TEXT("%s %.2f kg: the actor is scaled uniformly"), SpeciesId, Weight), Actor->GetActorScale3D().Equals(FVector(Expected), 1.e-4));
				Actor->Destroy();
			}
		}
		return true;
	}

	// =================================================================================================================
	// Data
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualDataRow, "Project.FishVisual.Data.VisualRowValidAndMatchesFallback", Flags)
	bool FFishVisualDataRow::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		if (!LoadVisual(*this, Table))
		{
			return false;
		}
		Table->ForeachRow<FFishVisualRow>(TEXT("FishVisualTest"), [this](const FName& Name, const FFishVisualRow& Row)
		{
			FString Problem;
			TestTrue(FString::Printf(TEXT("DT_FishVisual %s is valid (%s)"), *Name.ToString(), *Problem), Row.Validate(Problem));
		});
		const FFishVisualRow* Row = DefaultRow(*this, Table);
		if (!Row)
		{
			return false;
		}
		FFishVisualRow Fallback = FFishVisualRow::GetFallbackRow();
		Fallback.DevComment = Row->DevComment;
		TestTrue(TEXT("the built-in row equals the shipped Default row (update GetFallbackRow with the JSON)"),
			FFishVisualRow::StaticStruct()->CompareScriptStruct(&Fallback, Row, PPF_None));
		FString Problem;
		TestTrue(TEXT("the built-in row is valid"), FFishVisualRow::GetFallbackRow().Validate(Problem));

		struct FCase { const TCHAR* What; TFunction<void(FFishVisualRow&)> Break; };
		const TArray<FCase> Bad = {
			{ TEXT("MinPlayRate > MaxPlayRate"), [](FFishVisualRow& R) { R.MinPlayRate = 3.f; } },
			{ TEXT("MinScale > MaxScale"), [](FFishVisualRow& R) { R.MinScale = 5.f; } },
			{ TEXT("NaN SurfaceDepth"), [](FFishVisualRow& R) { R.SurfaceDepth = std::numeric_limits<float>::quiet_NaN(); } },
			{ TEXT("MaxPitchDeg 95"), [](FFishVisualRow& R) { R.MaxPitchDeg = 95.f; } },
			{ TEXT("negative EscapeTime"), [](FFishVisualRow& R) { R.EscapeTime = -1.f; } },
			{ TEXT("a move listed twice"), [](FFishVisualRow& R) { const FFishMoveAnimRole First = R.MoveRoles[0]; R.MoveRoles.Add(First); } },
			{ TEXT("a move without an id"), [](FFishVisualRow& R) { R.MoveRoles.AddDefaulted(); } },
			{ TEXT("a tail beat of 0 Hz"), [](FFishVisualRow& R) { R.RoleTailBeats[0].Hz = 0.f; } },
			{ TEXT("a role's tail beat twice"), [](FFishVisualRow& R) { const FFishRoleTailBeat First = R.RoleTailBeats[0]; R.RoleTailBeats.Add(First); } },
		};
		for (const FCase& Case : Bad)
		{
			FFishVisualRow Broken = *Row;
			Case.Break(Broken);
			FString Why;
			TestFalse(FString::Printf(TEXT("rejected: %s"), Case.What), Broken.Validate(Why));
			TestFalse(FString::Printf(TEXT("%s: says why"), Case.What), Why.IsEmpty());
		}
		return true;
	}

	/** DT_FishSpecies look columns: every species has a skinned mesh and sane AnimAmplitude/AnimRate; the mesh fallback order. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualDataSpecies, "Project.FishVisual.Data.SpeciesLookColumns", Flags)
	bool FFishVisualDataSpecies::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		if (!FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		Fish.Species->ForeachRow<FFishSpeciesRow>(TEXT("FishVisualTest"), [this](const FName& Name, const FFishSpeciesRow& Species)
		{
			FString Problem;
			TestTrue(FString::Printf(TEXT("%s: look numbers valid (%s)"), *Name.ToString(), *Problem), FFightFishVisual::ValidateSpeciesLook(Species, Problem));
			TestFalse(FString::Printf(TEXT("%s: has a SkeletalMesh"), *Name.ToString()), Species.SkeletalMesh.IsNull());
			TestTrue(FString::Printf(TEXT("%s: its SkeletalMesh is a fishkit SK_ in /Game/Art/Fish (%s)"), *Name.ToString(), *Species.SkeletalMesh.ToString()),
				Species.SkeletalMesh.ToSoftObjectPath().GetLongPackageName().StartsWith(TEXT("/Game/Art/Fish/SK_")));
		});
		const FFishSpeciesRow* Bonefish = FishQA::Row<FFishSpeciesRow>(Fish.Species, TEXT("Bonefish"));
		if (TestNotNull(TEXT("Bonefish row"), Bonefish))
		{
			TestEqual(TEXT("Bonefish AnimRate (anim.md)"), Bonefish->AnimRate, 1.15f, 1.e-6f);
			TestEqual(TEXT("Bonefish AnimAmplitude (anim.md)"), Bonefish->AnimAmplitude, 1.f, 1.e-6f);
		}

		// The optional columns really are optional: a row without them gets 1 / 1 / None.
		FFishSpeciesRow Defaults;
		TestEqual(TEXT("default AnimRate 1"), Defaults.AnimRate, 1.f);
		TestEqual(TEXT("default AnimAmplitude 1"), Defaults.AnimAmplitude, 1.f);
		TestTrue(TEXT("default SkeletalMesh None"), Defaults.SkeletalMesh.IsNull());

		// Mesh order: species SkeletalMesh, species Mesh, the settings' fallback; duplicates and empty paths skipped.
		const TSoftObjectPtr<USkeletalMesh> Fallback(FSoftObjectPath(TEXT("/Game/Art/Fish/SK_Bonefish.SK_Bonefish")));
		FFishSpeciesRow Species;
		Species.SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Art/Fish/SK_CoralSnapper.SK_CoralSnapper")));
		Species.Mesh = TSoftObjectPtr<UStreamableRenderAsset>(FSoftObjectPath(TEXT("/Game/Art/Fish/SM_CoralSnapper.SM_CoralSnapper")));
		TArray<FSoftObjectPath> Order = ULureFightFishSubsystem::MeshCandidates(&Species, Fallback);
		if (TestEqual(TEXT("three candidates"), Order.Num(), 3))
		{
			TestEqual(TEXT("1: SkeletalMesh"), Order[0].ToString(), Species.SkeletalMesh.ToString());
			TestEqual(TEXT("2: Mesh"), Order[1].ToString(), Species.Mesh.ToString());
			TestEqual(TEXT("3: the fallback"), Order[2].ToString(), Fallback.ToString());
		}
		Species.SkeletalMesh.Reset();
		Species.Mesh.Reset();
		Order = ULureFightFishSubsystem::MeshCandidates(&Species, Fallback);
		TestTrue(TEXT("a species without meshes: only the fallback"), Order.Num() == 1 && Order[0] == Fallback.ToSoftObjectPath());
		Order = ULureFightFishSubsystem::MeshCandidates(nullptr, Fallback);
		TestTrue(TEXT("an unknown species: only the fallback"), Order.Num() == 1 && Order[0] == Fallback.ToSoftObjectPath());
		TestEqual(TEXT("no fallback either: nothing (nothing drawn, no crash)"), ULureFightFishSubsystem::MeshCandidates(nullptr, TSoftObjectPtr<USkeletalMesh>()).Num(), 0);
		return true;
	}

	// =================================================================================================================
	// The adapter: the only place the visual reads the fight's structs
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualAdapter, "Project.FishVisual.Adapter.MapsReplicatedFightState", Flags)
	bool FFishVisualAdapter::RunTest(const FString& Parameters)
	{
		FLureFightNetState Fight;
		Fight.bActive = true;
		Fight.FightId = 7;
		Fight.MoveId = TEXT("Dive");
		Fight.bExhausted = true;
		Fight.Depth = 120.f;
		Fight.SideDeg = 90.f;
		Fight.LineOut = 1000.f;
		Fight.Tension = 5.f;
		Fight.LineStrength = 10.f;
		FLureFishingNetState Line;
		Line.State = ELureFishingState::Hooked;
		Line.BobberRest = FVector(1500.f, 0.f, -3.f);
		FFishInstance Hooked;
		Hooked.SpeciesId = TEXT("CoralSnapper");
		Hooked.WeightKg = 3.25f;
		const FVector Player(0.f, 0.f, 190.f);

		FFightFishView View = FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, false);
		TestTrue(TEXT("fighting"), View.bFighting);
		TestEqual(TEXT("FightId"), static_cast<int32>(View.FightId), 7);
		TestTrue(TEXT("no end while fighting"), View.End == EFightFishEnd::None);
		TestEqual(TEXT("MoveId"), View.MoveId, FName(TEXT("Dive")));
		TestTrue(TEXT("exhausted"), View.bExhausted);
		TestEqual(TEXT("depth"), View.DepthCm, 120.f, 1.e-6f);
		TestEqual(TEXT("tension 0..1"), View.Tension01, 0.5f, 1.e-6f);
		TestEqual(TEXT("water height = the bobber's rest height"), View.WaterZ, -3.f, 1.e-4f);
		TestTrue(TEXT("the line end: LineOut from the player toward the bobber, swung by SideDeg, at the surface"),
			View.LineEnd.Equals(FVector(0.f, 1000.f, -3.f), 0.05));
		TestTrue(TEXT("player location"), View.PlayerLocation.Equals(Player));
		TestFalse(TEXT("authority passed through"), View.bHasAuthority);
		TestEqual(TEXT("the fish record"), View.Fish.SpeciesId, FName(TEXT("CoralSnapper")));
		TestEqual(TEXT("... its weight"), View.Fish.WeightKg, 3.25f, 1.e-6f);

		Line.BobberRest = FVector(0.f, 0.f, -3.f); // right under the player: the player's forward is the fallback direction
		View = FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector(0.f, -1.f, 0.f), true);
		TestTrue(TEXT("bobber under the player: direction from the player's forward"), View.LineEnd.Equals(FVector(1000.f, 0.f, -3.f), 0.05));
		Line.BobberRest = FVector(1500.f, 0.f, -3.f);

		Line.State = ELureFishingState::Waiting;
		TestFalse(TEXT("an active fight on a line that is not Hooked is not shown"), FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, true).bFighting);
		Line.State = ELureFishingState::Idle;
		Fight.bActive = false;
		Fight.Outcome = ELureFightOutcome::Landed;
		TestTrue(TEXT("ended, Outcome Landed: Landed"), FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, true).End == EFightFishEnd::Landed);
		Fight.Outcome = ELureFightOutcome::None;
		Line.LastResult = ELureFishingResult::Landed;
		TestTrue(TEXT("ended, line result Landed: Landed"), FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, true).End == EFightFishEnd::Landed);
		for (const ELureFightOutcome Outcome : { ELureFightOutcome::Snapped, ELureFightOutcome::Spooled, ELureFightOutcome::ThrewHook, ELureFightOutcome::None })
		{
			Fight.Outcome = Outcome;
			Line.LastResult = Outcome == ELureFightOutcome::None ? ELureFishingResult::Lost : ELureFishingResult::Snapped;
			TestTrue(FString::Printf(TEXT("ended, %s: Escaped"), *LureFightQA::OutcomeName(Outcome)),
				FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, true).End == EFightFishEnd::Escaped);
		}

		// The visual runs on the view alone: no fishing component, no fight struct anywhere in this world.
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALureFightFish* Fish = SpawnBareFish(World.World, FFishVisualRow::GetFallbackRow());
		if (!TestNotNull(TEXT("fish actor"), Fish))
		{
			return false;
		}
		FFightFishView Plain = MakeView(FVector(1800.f, 300.f, 0.f), TEXT("Run"), 40.f);
		Fish->ApplyView(Plain, 0.f);
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Fish->ApplyView(Plain, LureFightQA::WorldDt);
		}
		TestEqual(TEXT("a hand-made view drives the clip"), RoleName(Fish->GetAnimState().Role), RoleName(EFishAnimRole::Run));
		TestTrue(TEXT("... and the placement (under the line end)"), FVector::Dist2D(Fish->GetActorLocation(), Plain.LineEnd) < 60.f);
		return true;
	}

	// =================================================================================================================
	// Placement: under the line end, deeper on dives, smoothed, facing its swim
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualPlacement, "Project.FishVisual.Placement.FollowsTheLineEnd", Flags)
	bool FFishVisualPlacement::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		LureFightQA::FWorld World;
		if (!LoadVisual(*this, Table) || !World.Create(*this))
		{
			return false;
		}
		const FFishVisualRow* RowPtr = DefaultRow(*this, Table);
		if (!RowPtr)
		{
			return false;
		}
		const FFishVisualRow& Row = *RowPtr;
		ALureFightFish* Fish = SpawnBareFish(World.World, Row);
		ALureFightFish* ProxyFish = SpawnBareFish(World.World, Row);
		if (!TestNotNull(TEXT("fish"), Fish) || !TestNotNull(TEXT("proxy fish"), ProxyFish))
		{
			return false;
		}
		const float Mouth = Fish->GetMouthOffsetCm(); // no mesh: half the default body length
		TestEqual(TEXT("no mesh: the nose is half a body length ahead"), Mouth, 0.5f * Row.DefaultBodyLengthCm, 1.e-4f);

		// The first view snaps: nose at the line's end (the body toward the player), SurfaceDepth under the surface.
		FFightFishView View = MakeView(FVector(2000.f, 0.f, 0.f), TEXT("Rest"));
		Fish->ApplyView(View, 0.f);
		FFightFishView ProxyView = View;
		ProxyView.bHasAuthority = false;
		ProxyFish->ApplyView(ProxyView, 0.f);
		TestTrue(FString::Printf(TEXT("placed at once: %s"), *Fish->GetActorLocation().ToString()),
			Fish->GetActorLocation().Equals(FVector(2000.f - Mouth, 0.f, -Row.SurfaceDepth), 0.01));
		TestEqual(TEXT("still fish: faces away from the player"), static_cast<float>(Fish->GetActorRotation().Yaw), 0.f, 0.5f);

		// A dive: the target sinks by DepthShare x Depth, capped at MaxShownDepth.
		View.DepthCm = 100.f;
		ProxyView.DepthCm = 100.f;
		const FVector Before = Fish->GetActorLocation();
		Fish->ApplyView(View, LureFightQA::WorldDt);
		ProxyFish->ApplyView(ProxyView, LureFightQA::WorldDt);
		const float TargetZ = -Row.SurfaceDepth - Row.DepthShare * 100.f;
		TestEqual(TEXT("dive target depth"), static_cast<float>(Fish->GetLastTarget().Z), TargetZ, 0.01f);
		const float AuthorityStep = static_cast<float>(Before.Z - Fish->GetActorLocation().Z);
		const float ProxyStep = static_cast<float>(Before.Z - ProxyFish->GetActorLocation().Z);
		TestEqual(TEXT("authority: smoothed with AuthoritySmoothTime"), AuthorityStep,
			(static_cast<float>(Before.Z) - TargetZ) * FFightFishVisual::SmoothAlpha(LureFightQA::WorldDt, Row.AuthoritySmoothTime), 0.01f);
		TestEqual(TEXT("proxy: smoothed with ProxySmoothTime"), ProxyStep,
			(static_cast<float>(Before.Z) - TargetZ) * FFightFishVisual::SmoothAlpha(LureFightQA::WorldDt, Row.ProxySmoothTime), 0.01f);
		TestTrue(TEXT("a proxy moves more smoothly (smaller steps toward a jump)"), ProxyStep > 0.f && ProxyStep < AuthorityStep);
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Fish->ApplyView(View, LureFightQA::WorldDt);
			ProxyFish->ApplyView(ProxyView, LureFightQA::WorldDt);
		}
		TestEqual(TEXT("settles at the dive depth"), static_cast<float>(Fish->GetActorLocation().Z), TargetZ, 0.5f);
		TestEqual(TEXT("the proxy settles there too"), static_cast<float>(ProxyFish->GetActorLocation().Z), TargetZ, 0.5f);
		View.DepthCm = 5000.f;
		Fish->ApplyView(View, LureFightQA::WorldDt);
		TestEqual(TEXT("deep dives are capped at MaxShownDepth"), static_cast<float>(Fish->GetLastTarget().Z), -Row.SurfaceDepth - Row.MaxShownDepth, 0.01f);
		View.DepthCm = 0.f;

		// Swimming sideways (a run across): it faces its swim.
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			View.LineEnd.Y += 4.f; // 240 cm/s to +Y
			Fish->ApplyView(View, LureFightQA::WorldDt);
		}
		TestTrue(FString::Printf(TEXT("swimming to +Y: faces +Y (yaw %.1f)"), Fish->GetActorRotation().Yaw), FMath::Abs(Fish->GetActorRotation().Yaw - 90.f) < 10.f);
		TestTrue(FString::Printf(TEXT("its speed is the swim speed (%.0f cm/s)"), Fish->GetFishVelocity().Size()), FMath::Abs(Fish->GetFishVelocity().Size() - 240.f) < 25.f);
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			Fish->ApplyView(View, LureFightQA::WorldDt);
		}
		const FVector Away = (Fish->GetActorLocation() - View.PlayerLocation).GetSafeNormal2D();
		TestTrue(FString::Printf(TEXT("stopped: faces away from the player again (yaw %.1f)"), Fish->GetActorRotation().Yaw),
			FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Fish->GetActorRotation().Yaw), static_cast<float>(Away.Rotation().Yaw))) < 3.f);

		// Tired: rolled onto its side.
		View.bExhausted = true;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Fish->ApplyView(View, LureFightQA::WorldDt);
		}
		TestEqual(TEXT("exhausted: rolled ExhaustedRollDeg"), static_cast<float>(Fish->GetActorRotation().Roll), Row.ExhaustedRollDeg, 2.f);
		View.bExhausted = false;

		// Shallow water: it stays FloorClearance above the bottom (a seabed box whose top is at -60).
		World.AddBox(FVector(3000.f, 0.f, -80.f), FVector(300.f, 300.f, 20.f));
		View.LineEnd = FVector(3000.f, 0.f, 0.f);
		View.DepthCm = 200.f;
		Fish->ApplyView(View, LureFightQA::WorldDt);
		TestEqual(TEXT("above the bottom"), static_cast<float>(Fish->GetLastTarget().Z), -60.f + Row.FloorClearance, 0.5f);
		TestTrue(TEXT("a jump farther than SnapDistance is snapped to"), Fish->GetActorLocation().Equals(Fish->GetLastTarget(), 0.01));
		return true;
	}

	// =================================================================================================================
	// Spawn and despawn with the fight (server / standalone)
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualLifecycle, "Project.FishVisual.Lifecycle.SpawnsAndDespawnsWithTheFight", Flags)
	bool FFishVisualLifecycle::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual))
		{
			return false;
		}
		FFishInstance FishA;
		FFishInstance FishB;
		FFishInstance FishC;
		if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.2f, 61, FishA) || !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.3f, 62, FishB)
			|| !LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.1f, 63, FishC))
		{
			return false;
		}
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		ULureFishingComponent* Fishing = LureFightQA::SetUpFishing(World.Spawn(LureFightQA::StandAt()), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!TestNotNull(TEXT("the fight fish subsystem exists in a game world"), Visuals) || !TestNotNull(TEXT("fishing"), Fishing))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());
		const FFishVisualRow& Row = Visuals->GetVisualRow();
		int32 Landed = 0;
		FFishInstance LandedFish;
		bool bKeep = false;
		TWeakObjectPtr<ALureFightFish> Kept;
		Visuals->OnFightFishLandedNative.AddLambda([&](ULureFishingComponent* By, ALureFightFish* Actor, const FFishInstance& Record)
		{
			++Landed;
			LandedFish = Record;
			TestTrue(TEXT("landed event: from this player's fishing"), By == Fishing);
			TestTrue(TEXT("landed event: the fish plays the flop at alpha 1"), Actor && Actor->GetAnimState().Role == EFishAnimRole::Flop && Actor->GetAnimState().Amplitude == 1.f);
			if (bKeep)
			{
				TestTrue(TEXT("KeepLandedFish during the event"), Visuals->KeepLandedFish(Actor));
				Kept = Actor;
			}
		});

		if (!LureFightQA::CastAndWait(*this, World, Fishing, 0.f))
		{
			return false;
		}
		TestEqual(TEXT("no fish while waiting for a bite"), Visuals->GetNumFish(), 0);

		// Hook: a fish appears at the line's end, sized by weight, with the species' mesh (if imported).
		if (!TestTrue(TEXT("hooks A"), Fishing->AuthorityHookFish(FishA)))
		{
			return false;
		}
		World.Tick(1);
		ALureFightFish* Actor = Visuals->FindFish(Fishing);
		if (!TestNotNull(TEXT("a fish is spawned when the fight starts"), Actor))
		{
			return false;
		}
		TestEqual(TEXT("one fish"), Visuals->GetNumFish(), 1);
		TestEqual(TEXT("its fight id"), static_cast<int32>(Actor->GetFightId()), static_cast<int32>(Fishing->GetFightNet().FightId));
		TestTrue(TEXT("it is the hooked fish"), LureFightQA::SameFish(Actor->GetFish(), FishA));
		const FFishSpeciesRow* Bonefish = FishQA::Row<FFishSpeciesRow>(Fish.Species, TEXT("Bonefish"));
		if (Bonefish)
		{
			TestEqual(TEXT("scale from weight"), Actor->GetFishScale(), FFightFishVisual::WeightScale(FishA.WeightKg, Bonefish->ReferenceWeight, Row), 1.e-5f);
		}
		TestFalse(TEXT("the fish is not replicated (each machine has its own)"), Actor->GetIsReplicated());
		TestEqual(TEXT("hook set: Thrash"), RoleName(Actor->GetAnimState().Role), RoleName(EFishAnimRole::Thrash));
		if (FPackageName::DoesPackageExist(TEXT("/Game/Art/Fish/SK_Bonefish")))
		{
			const USkeletalMesh* Mesh = Actor->GetMesh()->GetSkeletalMeshAsset();
			TestEqual(TEXT("the species' skinned mesh"), Mesh ? Mesh->GetName() : FString(), FString(TEXT("SK_Bonefish")));
		}
		else
		{
			AddInfo(TEXT("SK_Bonefish is not in this checkout: mesh assignment not checked"));
		}

		// While fighting: follows the fight's view; the clip follows the move.
		World.Tick(90);
		const FFightFishView View = FFightFishViewAdapter::FromComponent(*Fishing);
		TestTrue(TEXT("still fighting"), View.bFighting);
		TestTrue(TEXT("the same fish all fight long"), Visuals->FindFish(Fishing) == Actor);
		const EFishAnimRole Expected = View.bExhausted ? EFishAnimRole::SwimIdle : FFightFishVisual::RoleForMove(Row, View.MoveId);
		TestEqual(FString::Printf(TEXT("after the hook set the move's clip (move %s)"), *View.MoveId.ToString()), RoleName(Actor->GetAnimState().Role), RoleName(Expected));
		TestTrue(FString::Printf(TEXT("the target is under the line end (%.0f cm off)"), FVector::Dist2D(Actor->GetLastTarget(), View.LineEnd)),
			FVector::Dist2D(Actor->GetLastTarget(), View.LineEnd) <= Actor->GetMouthOffsetCm() * Actor->GetFishScale() + 0.1f);
		TestTrue(TEXT("under the water"), Actor->GetActorLocation().Z < View.WaterZ);

		// Landed: the hand-off event fires once, then the fish is gone.
		TWeakObjectPtr<ALureFightFish> WeakA = Actor;
		Fishing->AuthoritySetReeling(true);
		TestTrue(TEXT("A is landed by reeling"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 60 * 60));
		World.Tick(2);
		TestEqual(TEXT("landed: OnFightFishLanded once"), Landed, 1);
		TestTrue(TEXT("landed: with A"), LureFightQA::SameFish(LandedFish, FishA));
		TestTrue(TEXT("landed: the fight fish is destroyed, or is the hanging fish's look (T-030)"), LureFightFishQA::LandedFishHandedOff(WeakA, Fishing->GetOwner()));
		LureFightFishQA::ClearHangingCatch(Fishing->GetOwner()); // T-030: off the hook before the next cast; its look goes with it
		TestFalse(TEXT("landed: the fight fish is gone once the hanging fish is"), WeakA.IsValid());
		TestNull(TEXT("landed: no fish for this player"), Visuals->FindFish(Fishing));
		TestEqual(TEXT("landed: no fish at all"), Visuals->GetNumFish(), 0);

		// Lost (reeled in mid-fight): the fish swims away, then disappears; no landed event.
		Fishing->AuthoritySetReeling(false);
		if (!LureFightQA::CastAndWait(*this, World, Fishing, 0.f) || !TestTrue(TEXT("hooks B"), Fishing->AuthorityHookFish(FishB)))
		{
			return false;
		}
		World.Tick(30);
		ALureFightFish* ActorB = Visuals->FindFish(Fishing);
		if (!TestNotNull(TEXT("B has a fish"), ActorB))
		{
			return false;
		}
		TWeakObjectPtr<ALureFightFish> WeakB = ActorB;
		const FVector Player = Fishing->GetOwner()->GetActorLocation();
		const double DistanceBefore = FVector::Dist2D(ActorB->GetActorLocation(), Player);
		Fishing->AuthorityReelIn();
		World.Tick(1);
		TestNull(TEXT("lost: no longer this player's fight fish"), Visuals->FindFish(Fishing));
		if (TestTrue(TEXT("lost: still there, swimming away"), WeakB.IsValid()))
		{
			TestEqual(TEXT("lost: escaping at SwimFast"), RoleName(WeakB->GetAnimState().Role), RoleName(EFishAnimRole::SwimFast));
			TestEqual(TEXT("lost: counted while it swims away"), Visuals->GetNumFish(), 1);
			World.Tick(20);
			if (WeakB.IsValid())
			{
				TestTrue(TEXT("lost: it swims away from the player"), FVector::Dist2D(WeakB->GetActorLocation(), Player) > DistanceBefore + 30.0);
			}
		}
		World.Tick(FMath::CeilToInt((Row.EscapeTime + 0.25f) / LureFightQA::WorldDt));
		TestFalse(TEXT("lost: gone after EscapeTime"), WeakB.IsValid());
		TestEqual(TEXT("lost: no fish left"), Visuals->GetNumFish(), 0);
		TestEqual(TEXT("lost: no landed event"), Landed, 1);

		// The seam for T-030: a listener keeps the landed fish.
		bKeep = true;
		if (!LureFightQA::CastAndWait(*this, World, Fishing, 0.f) || !TestTrue(TEXT("hooks C"), Fishing->AuthorityHookFish(FishC)))
		{
			return false;
		}
		Fishing->AuthoritySetReeling(true);
		TestTrue(TEXT("C is landed by reeling"), World.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Idle; }, 60 * 60));
		World.Tick(2);
		TestEqual(TEXT("keep: the landed event fired"), Landed, 2);
		if (TestTrue(TEXT("keep: the kept fish lives on"), Kept.IsValid()))
		{
			TestTrue(TEXT("keep: in its Landed phase"), Kept->GetPhase() == EFightFishPhase::Landed);
			TestEqual(TEXT("keep: the subsystem let go of it"), Visuals->GetNumFish(), 0);
			Kept->Destroy();
		}
		TestFalse(TEXT("KeepLandedFish outside the event does nothing"), Visuals->KeepLandedFish(nullptr));
		return true;
	}

	// =================================================================================================================
	// T-028: a rod-steered fight. The fish stays on the line where the bobber rides while the rod turns it.
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualAdapterRodSteered, "Project.FishVisual.Adapter.FollowsRodSteeredFight", Flags)
	bool FFishVisualAdapterRodSteered::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual))
		{
			return false;
		}
		FFishInstance Hooked;
		if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.5f, 71, Hooked))
		{
			return false;
		}
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		ULureFishingComponent* Fishing = LureFightQA::SetUpFishing(World.Spawn(LureFightQA::StandAt()), Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !TestNotNull(TEXT("fishing"), Fishing))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());
		if (!LureFightQA::CastAndWait(*this, World, Fishing) || !TestTrue(TEXT("hooks the Bonefish (Run pattern: sideways runs)"), Fishing->AuthorityHookFish(Hooked)))
		{
			return false;
		}
		const int32 DefaultStep = Data.Tuning()->ReelDefaultStep - 1;
		const FVector Player = Fishing->GetOwner()->GetActorLocation();
		const FVector Rest = FVector(Fishing->GetNetState().BobberRest);
		const double RestAngle = FMath::RadiansToDegrees(FMath::Atan2(Rest.Y - Player.Y, Rest.X - Player.X));

		int32 Frames = 0;
		int32 SteeredFrames = 0;
		int32 OffBobber = 0;
		int32 OffAngle = 0;
		int32 OffTension = 0;
		int32 OffFish = 0;
		double TurnAgainstRun = 0.0; // sum over steered frames of (SideDeg change x run direction): < 0 = turned against the run
		float PreviousSideDeg = Fishing->GetFightNet().SideDeg;
		for (int32 Frame = 0; Frame < 20 * 60 && Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			// The rod held fully against the fish's current run (level, default reel step, not reeling: the fish keeps running).
			const ELureFightRunSide RunSide = Fishing->GetFightNet().RunSide;
			const int32 RunDir = FLureRodControl::DirectionFromRunSide(RunSide);
			Fishing->AuthoritySetFightInput(Fishing->GetFightNet().FightId, 0.f, -static_cast<float>(RunDir), DefaultStep);
			World.Tick(1);

			const FLureFightNetState& Net = Fishing->GetFightNet();
			const FFightFishView View = FFightFishViewAdapter::FromComponent(*Fishing);
			if (!View.bFighting)
			{
				continue;
			}
			++Frames;
			if (RunDir != 0 && Net.RunSide == RunSide)
			{
				++SteeredFrames;
				TurnAgainstRun += static_cast<double>(Net.SideDeg - PreviousSideDeg) * RunDir;
			}
			PreviousSideDeg = Net.SideDeg;

			OffBobber += FVector::Dist2D(View.LineEnd, Fishing->GetBobberLocation()) > 0.5 ? 1 : 0;
			if (Net.LineOut > 50.f)
			{
				const double ViewAngle = FMath::RadiansToDegrees(FMath::Atan2(View.LineEnd.Y - Player.Y, View.LineEnd.X - Player.X));
				OffAngle += FMath::Abs(FMath::UnwindDegrees(ViewAngle - RestAngle - Net.SideDeg)) > 0.05 ? 1 : 0;
			}
			OffTension += FMath::Abs(View.Tension01 - Net.GetTension01()) > 1.0e-6f ? 1 : 0;
			if (const ALureFightFish* Actor = Visuals->FindFish(Fishing))
			{
				OffFish += FVector::Dist2D(Actor->GetLastTarget(), View.LineEnd) > Actor->GetMouthOffsetCm() * Actor->GetFishScale() + 0.1f ? 1 : 0;
			}
		}
		AddInfo(FString::Printf(TEXT("rod-steered fight: %d frames shown, %d steered against a run, SideDeg turned %.2f deg against the run"), Frames, SteeredFrames, -TurnAgainstRun));
		TestTrue(FString::Printf(TEXT("the fight showed a fish for a while (%d frames)"), Frames), Frames > 120);
		TestTrue(FString::Printf(TEXT("the fish ran sideways while the rod steered against it (%d frames)"), SteeredFrames), SteeredFrames > 30);
		TestTrue(FString::Printf(TEXT("the rod turned the fish against its run (sum %.2f deg < 0)"), TurnAgainstRun), TurnAgainstRun < 0.0);
		TestEqual(TEXT("frames with the view's line end off the bobber (where the camera and the line look)"), OffBobber, 0);
		TestEqual(TEXT("frames with the view's line end not at the fight's SideDeg"), OffAngle, 0);
		TestEqual(TEXT("frames with the view's tension not the fight's (rod pressure included)"), OffTension, 0);
		TestEqual(TEXT("frames with the fish's target off the line end"), OffFish, 0);
		return true;
	}

	// =================================================================================================================
	// Proxies: the fish shows on machines without authority, from replicated state only
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualProxy, "Project.FishVisual.Lifecycle.RunsOnProxiesWithoutAuthority", Flags)
	bool FFishVisualProxy::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Data;
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual))
		{
			return false;
		}
		FFishInstance Bonefish;
		if (!LureFightQA::RollFish(*this, Fish, TEXT("Bonefish"), TEXT("Common"), 0.2f, 61, Bonefish))
		{
			return false;
		}
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		ALurePlayerCharacter* ServerCharacter = World.Spawn(LureFightQA::StandAt());
		ALurePlayerCharacter* ProxyCharacter = World.Spawn(LureFightQA::StandAt() + FVector(0.f, -250.f, 0.f));
		ULureFishingComponent* Server = LureFightQA::SetUpFishing(ServerCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		ULureFishingComponent* Proxy = LureFightQA::SetUpFishing(ProxyCharacter, Fish, Data.Gear.Get(), Data.Patterns.Get(), Data.Fight.Get());
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !TestNotNull(TEXT("server"), Server) || !TestNotNull(TEXT("proxy"), Proxy))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());
		const FFishVisualRow& Row = Visuals->GetVisualRow();
		MakeCopy(ProxyCharacter, ROLE_SimulatedProxy);
		int32 ProxyLanded = 0;
		Visuals->OnFightFishLandedNative.AddLambda([&ProxyLanded, Proxy](ULureFishingComponent* By, ALureFightFish*, const FFishInstance&) { ProxyLanded += By == Proxy ? 1 : 0; });

		if (!LureFightQA::CastAndWait(*this, World, Server, 0.f) || !TestTrue(TEXT("the server hooks"), Server->AuthorityHookFish(Bonefish)))
		{
			ProxyCharacter->SetRole(ROLE_Authority);
			return false;
		}
		LureFightQA::ReplicateFishing(*this, Server, Proxy);
		World.Tick(1);
		ALureFightFish* ProxyFish = Visuals->FindFish(Proxy);
		ALureFightFish* ServerFish = Visuals->FindFish(Server);
		if (TestNotNull(TEXT("proxy: a fish from the replicated fight"), ProxyFish) && TestNotNull(TEXT("server: its own fish"), ServerFish))
		{
			TestTrue(TEXT("proxy and server fish are different local actors"), ProxyFish != ServerFish);
			TestTrue(TEXT("proxy: the same fish record"), LureFightQA::SameFish(ProxyFish->GetFish(), Bonefish));
			TestEqual(TEXT("proxy: the same size"), ProxyFish->GetFishScale(), ServerFish->GetFishScale(), 1.e-6f);
		}
		TestFalse(TEXT("proxy: its view has no authority (proxy smoothing)"), FFightFishViewAdapter::FromComponent(*Proxy).bHasAuthority);

		// Through the fight: the proxy's fish follows its replicated state; the proxy never simulates.
		Server->AuthoritySetReeling(true);
		bool bRolesMatch = true;
		bool bUnderLine = true;
		int32 Frames = 0;
		for (; Frames < 60 * 60 && Server->GetFishingState() == ELureFishingState::Hooked; ++Frames)
		{
			// What the proxy's copy holds now is what its fish is updated from during the next tick.
			LureFightQA::ReplicateFishing(*this, Server, Proxy);
			const FFightFishView View = FFightFishViewAdapter::FromComponent(*Proxy);
			World.Tick(1);
			const ALureFightFish* Current = Visuals->FindFish(Proxy);
			if (Current && View.bFighting && Current->GetSeconds() > Row.HookSetThrashTime + 0.1f)
			{
				const EFishAnimRole Want = View.bExhausted ? EFishAnimRole::SwimIdle : FFightFishVisual::RoleForMove(Row, View.MoveId);
				bRolesMatch &= Current->GetAnimState().Role == Want;
				bUnderLine &= FVector::Dist2D(Current->GetLastTarget(), View.LineEnd) <= Current->GetMouthOffsetCm() * Current->GetFishScale() + 0.1f;
			}
		}
		TestTrue(TEXT("proxy: its fish plays the replicated move's clip"), bRolesMatch);
		TestTrue(TEXT("proxy: its fish stays under its line end"), bUnderLine);
		TestEqual(TEXT("proxy: its copy never simulated the fight"), Proxy->GetFightState().Steps, 0);
		TestEqual(TEXT("the server landed the fish"), LureFightQA::ResultName(Server->GetFishingNetState().LastResult), LureFightQA::ResultName(ELureFishingResult::Landed));

		// The landing reaches the proxy: its fish is handed off and removed.
		LureFightQA::ReplicateFishing(*this, Server, Proxy);
		World.Tick(2);
		TestEqual(TEXT("proxy: its landed event fired once"), ProxyLanded, 1);
		TestNull(TEXT("proxy: its fight fish is gone"), Visuals->FindFish(Proxy));
		TestEqual(TEXT("no fish left anywhere"), Visuals->GetNumFish(), 0);
		ProxyCharacter->SetRole(ROLE_Authority);
		return true;
	}
	/** A lost fish sinks while it swims away, but never into the seabed (FloorClearance above it), and still goes after EscapeTime. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualEscapeFloor, "Project.FishVisual.Placement.EscapeStaysAboveTheSeabed", Flags)
	bool FFishVisualEscapeFloor::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		LureFightQA::FWorld World;
		if (!LoadVisual(*this, Table) || !World.Create(*this))
		{
			return false;
		}
		const FFishVisualRow* RowPtr = DefaultRow(*this, Table);
		if (!RowPtr)
		{
			return false;
		}
		const FFishVisualRow& Row = *RowPtr;

		// Pure rule: no floor = plain sink; a floor lifts the step to FloorZ + FloorClearance, capped at the surface.
		const FVector Start(1000.f, 0.f, -25.f);
		const FVector Free = FFightFishVisual::EscapeStep(Row, Start, FVector(1.f, 0.f, 0.f), 1.f, 0.f, -UE_BIG_NUMBER);
		TestTrue(TEXT("no floor: swims away and sinks"), Free.Equals(Start + FVector(Row.EscapeSpeed, 0.f, -Row.EscapeSinkSpeed), 0.01));
		const FVector Floored = FFightFishVisual::EscapeStep(Row, Start, FVector(1.f, 0.f, 0.f), 1.f, 0.f, -50.f);
		TestEqual(TEXT("floor: held at FloorZ + FloorClearance"), static_cast<float>(Floored.Z), -50.f + Row.FloorClearance, 0.01f);
		TestEqual(TEXT("floor: still swims away"), static_cast<float>(Floored.X), 1000.f + Row.EscapeSpeed, 0.01f);
		const FVector Beach = FFightFishVisual::EscapeStep(Row, Start, FVector(1.f, 0.f, 0.f), 1.f, 0.f, 30.f);
		TestTrue(TEXT("floor above the water: never lifted out of it"), Beach.Z <= 0.0);
		const FVector Deep = FFightFishVisual::EscapeStep(Row, Start, FVector(1.f, 0.f, 0.f), 0.1f, 0.f, -500.f);
		TestEqual(TEXT("deep floor: no effect"), static_cast<float>(Deep.Z), -25.f - 0.1f * Row.EscapeSinkSpeed, 0.01f);
		FFishVisualRow NoClamp = Row;
		NoClamp.FloorClearance = -1.f;
		TestTrue(TEXT("FloorClearance < 0: no clamp"), FFightFishVisual::EscapeStep(NoClamp, Start, FVector(1.f, 0.f, 0.f), 1.f, 0.f, -50.f).Z < -80.0);
		TestTrue(TEXT("NaN time: no move"), FFightFishVisual::EscapeStep(Row, Start, FVector(1.f, 0.f, 0.f), std::numeric_limits<float>::quiet_NaN(), 0.f, -50.f).Equals(Start, 0.01));

		// A live fish over shallow sand (top at z = -50, water at 0): it sinks to the bottom and glides along it.
		constexpr float SandTop = -50.f;
		World.AddBox(FVector(4000.f, 0.f, SandTop - 100.f), FVector(3000.f, 3000.f, 100.f));
		ALureFightFish* Fish = SpawnBareFish(World.World, Row);
		if (!TestNotNull(TEXT("fish"), Fish))
		{
			return false;
		}
		Fish->ApplyView(MakeView(FVector(2000.f, 0.f, 0.f), TEXT("Rest")), 0.f);
		const FVector Before = Fish->GetActorLocation();
		Fish->BeginEnd(EFightFishEnd::Escaped, FVector(0.f, 0.f, 100.f));
		const float Step = 1.f / 60.f;
		float LowestZ = static_cast<float>(Before.Z);
		int32 Frames = 0;
		for (; Frames < 600 && !Fish->IsFinished(); ++Frames)
		{
			Fish->TickAfterFight(Step);
			LowestZ = FMath::Min(LowestZ, static_cast<float>(Fish->GetActorLocation().Z));
		}
		TestTrue(TEXT("finished after EscapeTime"), Fish->IsFinished());
		TestEqual(TEXT("removed on time (frames)"), static_cast<float>(Frames), Row.EscapeTime / Step, 2.f);
		TestTrue(FString::Printf(TEXT("sank at first (%.1f < %.1f)"), LowestZ, Before.Z), LowestZ < Before.Z - 10.f);
		TestTrue(FString::Printf(TEXT("never below the sand + clearance (lowest %.2f, min %.2f)"), LowestZ, SandTop + Row.FloorClearance),
			LowestZ >= SandTop + Row.FloorClearance - 0.5f);
		TestTrue(TEXT("swam away from the player"), Fish->GetActorLocation().X > Before.X + 0.8f * Row.EscapeSpeed * Row.EscapeTime);
		Fish->Destroy();
		return true;
	}

	/** The subsystem's update runs in TG_LastDemotable, after the fight step in ULureFishingComponent (TG_PostUpdateWork). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualTickOrder, "Project.FishVisual.Lifecycle.UpdateTicksAfterTheFightStep", Flags)
	bool FFishVisualTickOrder::RunTest(const FString& Parameters)
	{
		LureFightQA::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		const ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		if (!TestNotNull(TEXT("subsystem"), Visuals))
		{
			return false;
		}
		const FTickFunction& Tick = Visuals->GetUpdateTickFunction();
		TestTrue(TEXT("registered at BeginPlay"), Tick.IsTickFunctionRegistered());
		TestTrue(TEXT("enabled"), Tick.IsTickFunctionEnabled());
		TestEqual(TEXT("tick group"), static_cast<int32>(Tick.TickGroup.GetValue()), static_cast<int32>(TG_LastDemotable));
		const ULureFishingComponent* Fishing = GetDefault<ULureFishingComponent>();
		TestTrue(TEXT("the fight steps in an earlier group"), static_cast<int32>(Fishing->PrimaryComponentTick.TickGroup.GetValue()) < static_cast<int32>(Tick.TickGroup.GetValue()));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Independent QA tests for the fighting fish visual (T-029): size, clip roles, placement, the fight adapter, fallbacks and
// data validation. Project.FishVisual.QA.{Size,Roles,Placement,Adapter,Fallback,Data}.*
// Written by the qa-engineer from docs/specs/fight-fish-visual.md and the header contracts (black-box).

#include "Tests/FishVisual/FightFishVisualQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "ReferenceSkeleton.h"
#include <limits>

namespace LureFightFishQA
{
	static const float QNaN = std::numeric_limits<float>::quiet_NaN();
	static const float QInf = std::numeric_limits<float>::infinity();

	// =================================================================================================================
	// Size: clamp((Weight / ReferenceWeight)^(1/3), MinScale, MaxScale); 1 when a weight is not positive and finite
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQASizeEdges, "Project.FishVisual.QA.Size.FormulaEdges", Flags)
	bool FFishVisualQASizeEdges::RunTest(const FString& Parameters)
	{
		const FFishVisualRow Row = FFishVisualRow::GetFallbackRow();
		const float R = 1.5f;
		struct FCase { const TCHAR* What; float W; float Ref; };
		const FCase Neutral[] = {
			{ TEXT("weight 0"), 0.f, R }, { TEXT("negative weight"), -3.f, R }, { TEXT("NaN weight"), QNaN, R }, { TEXT("infinite weight"), QInf, R },
			{ TEXT("ReferenceWeight 0 (missing)"), 2.f, 0.f }, { TEXT("negative ReferenceWeight"), 2.f, -1.f }, { TEXT("NaN ReferenceWeight"), 2.f, QNaN },
			{ TEXT("infinite ReferenceWeight"), 2.f, QInf }, { TEXT("both 0"), 0.f, 0.f } };
		for (const FCase& Case : Neutral)
		{
			TestEqual(FString::Printf(TEXT("%s: scale 1"), Case.What), FFightFishVisual::WeightScale(Case.W, Case.Ref, Row), 1.f, 1.e-6f);
		}
		// Clamp edges: 64 x the reference weight = exactly 4 (MaxScale); 1/64 = 0.25 (MinScale).
		TestEqual(TEXT("64 x reference: MaxScale"), FFightFishVisual::WeightScale(R * 64.f, R, Row), Row.MaxScale, 1.e-4f);
		TestEqual(TEXT("just over 64 x: clamped to MaxScale"), FFightFishVisual::WeightScale(R * 64.f * 1.05f, R, Row), Row.MaxScale, 1.e-6f);
		const float Under = FFightFishVisual::WeightScale(R * 64.f * 0.95f, R, Row);
		TestTrue(TEXT("just under 64 x: below MaxScale, on the formula"), Under < Row.MaxScale && FMath::IsNearlyEqual(Under, FMath::Pow(64.f * 0.95f, 1.f / 3.f), 1.e-4f));
		TestEqual(TEXT("1/64 of reference: MinScale"), FFightFishVisual::WeightScale(R / 64.f, R, Row), Row.MinScale, 1.e-4f);
		TestEqual(TEXT("just under 1/64: clamped to MinScale"), FFightFishVisual::WeightScale(R / 64.f * 0.95f, R, Row), Row.MinScale, 1.e-6f);
		const float Over = FFightFishVisual::WeightScale(R / 64.f * 1.05f, R, Row);
		TestTrue(TEXT("just over 1/64: above MinScale, on the formula"), Over > Row.MinScale && FMath::IsNearlyEqual(Over, FMath::Pow(1.05f / 64.f, 1.f / 3.f), 1.e-4f));
		TestEqual(TEXT("huge (1e30 kg): MaxScale, finite"), FFightFishVisual::WeightScale(1.e30f, 1.f, Row), Row.MaxScale, 1.e-6f);
		TestEqual(TEXT("FLT_MAX / tiny reference (ratio overflows): MaxScale"), FFightFishVisual::WeightScale(FLT_MAX, 1.e-30f, Row), Row.MaxScale, 1.e-6f);
		TestEqual(TEXT("tiny (1e-30 kg): MinScale"), FFightFishVisual::WeightScale(1.e-30f, 1.f, Row), Row.MinScale, 1.e-6f);

		// Monotone and inside the clamps over 12 decades.
		bool bMonotone = true;
		bool bInside = true;
		float Previous = 0.f;
		for (int32 Step = 0; Step <= 1200; ++Step)
		{
			const float W = FMath::Pow(10.f, -6.f + Step * 0.01f);
			const float S = FFightFishVisual::WeightScale(W, R, Row);
			bMonotone &= S >= Previous;
			bInside &= FMath::IsFinite(S) && S >= Row.MinScale && S <= Row.MaxScale;
			Previous = S;
		}
		TestTrue(TEXT("heavier never looks smaller"), bMonotone);
		TestTrue(TEXT("always finite and within [MinScale, MaxScale]"), bInside);

		FFishVisualRow Fixed = Row;
		Fixed.MinScale = 1.7f;
		Fixed.MaxScale = 1.7f;
		TestEqual(TEXT("MinScale = MaxScale: that size for any valid weight"), FFightFishVisual::WeightScale(0.01f, R, Fixed), 1.7f, 1.e-6f);
		TestEqual(TEXT("... and a heavy one"), FFightFishVisual::WeightScale(500.f, R, Fixed), 1.7f, 1.e-6f);
		return true;
	}

	/** Fish spawned by the subsystem: scale from the species' ReferenceWeight; weight 0, missing ReferenceWeight and unknown species = 1. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQASizeSpawned, "Project.FishVisual.QA.Size.SpawnedFishEdges", Flags)
	bool FFishVisualQASizeSpawned::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		const FFishSpeciesRow* Bonefish = FishQA::Row<FFishSpeciesRow>(Fish.Species, TEXT("Bonefish"));
		if (!TestNotNull(TEXT("Bonefish row"), Bonefish))
		{
			return false;
		}
		FFishSpeciesRow NoReference = *Bonefish;
		NoReference.ReferenceWeight = 0.f;
		const TStrongObjectPtr<UDataTable> Species = SpeciesWith(Fish, { { TEXT("QA_NoReference"), NoReference } });
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		FPuppet Puppet;
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !Puppet.Create(*this, World, LureFightQA::StandAt()))
		{
			return false;
		}
		Visuals->SetTables(Species.Get(), Visual.Get());
		const FFishVisualRow& Row = Visuals->GetVisualRow();
		const float Ref = Bonefish->ReferenceWeight;
		struct FCase { const TCHAR* What; FName Species; float Weight; float Expected; };
		const FCase Cases[] = {
			{ TEXT("reference weight"), TEXT("Bonefish"), Ref, 1.f },
			{ TEXT("8 x reference"), TEXT("Bonefish"), Ref * 8.f, 2.f },
			{ TEXT("weight 0"), TEXT("Bonefish"), 0.f, 1.f },
			{ TEXT("64 x reference (MaxScale)"), TEXT("Bonefish"), Ref * 64.f, Row.MaxScale },
			{ TEXT("a million kg"), TEXT("Bonefish"), 1.e6f, Row.MaxScale },
			{ TEXT("1/64 of reference (MinScale)"), TEXT("Bonefish"), Ref / 64.f, Row.MinScale },
			{ TEXT("species with ReferenceWeight 0"), TEXT("QA_NoReference"), 8.f, 1.f },
			{ TEXT("unknown species (no ReferenceWeight at all), 8 kg"), TEXT("QA_NotASpecies"), 8.f, 1.f },
		};
		uint8 Id = 1;
		for (const FCase& Case : Cases)
		{
			Puppet.Start(Id++, MakeRecord(Case.Species, Case.Weight));
			World.Tick(2);
			ALureFightFish* Actor = Visuals->FindFish(Puppet.Fishing);
			if (TestNotNull(FString::Printf(TEXT("%s: a fish"), Case.What), Actor))
			{
				TestEqual(FString::Printf(TEXT("%s: scale"), Case.What), Actor->GetFishScale(), Case.Expected, 1.e-3f);
				TestTrue(FString::Printf(TEXT("%s: the actor is scaled uniformly by it"), Case.What), Actor->GetActorScale3D().Equals(FVector(Actor->GetFishScale()), 1.e-4));
				TestTrue(FString::Printf(TEXT("%s: under the water"), Case.What), Actor->GetActorLocation().Z < Puppet.BobberRest().Z);
			}
			Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
			World.Tick(1);
		}
		return true;
	}

	// =================================================================================================================
	// Roles: which clip plays, from DT_FishVisual
	// =================================================================================================================

	static FFishMoveAnimRole MoveRole(FName Move, EFishAnimRole Role)
	{
		FFishMoveAnimRole Entry;
		Entry.MoveId = Move;
		Entry.Role = Role;
		return Entry;
	}

	static FFightFishAnimInput SettledInput(FName Move)
	{
		FFightFishAnimInput In;
		In.MoveId = Move;
		In.SecondsSinceHook = 10.f;
		In.SpeedCmS = 80.f;
		In.BodyLengthCm = 55.f;
		In.WeightKg = 1.5f;
		In.ReferenceWeightKg = 1.5f;
		return In;
	}

	/** Every MoveRoles line of the shipped table plays its role; unknown moves play UnknownMoveRole (read from the data, not hard-coded). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQARolesFromData, "Project.FishVisual.QA.Roles.EveryMoveFromTheTable", Flags)
	bool FFishVisualQARolesFromData::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		if (!LoadVisual(*this, Table))
		{
			return false;
		}
		const FFishVisualRow* Row = SettingsRow(Table.Get());
		if (!TestNotNull(TEXT("Default row"), Row))
		{
			return false;
		}
		for (const FFishMoveAnimRole& Entry : Row->MoveRoles)
		{
			TestEqual(FString::Printf(TEXT("RoleForMove(%s)"), *Entry.MoveId.ToString()), RoleName(FFightFishVisual::RoleForMove(*Row, Entry.MoveId)), RoleName(Entry.Role));
			TestEqual(FString::Printf(TEXT("clip for move %s"), *Entry.MoveId.ToString()), RoleName(FFightFishVisual::ComputeAnimState(*Row, SettledInput(Entry.MoveId)).Role), RoleName(Entry.Role));
		}
		for (const TCHAR* Unknown : { TEXT("QA_Zigzag"), TEXT("") })
		{
			TestEqual(FString::Printf(TEXT("unknown move '%s': UnknownMoveRole"), Unknown), RoleName(FFightFishVisual::ComputeAnimState(*Row, SettledInput(Unknown)).Role),
				RoleName(Row->UnknownMoveRole));
		}
		// A remapped fixture row changes the clips with no code.
		FFishVisualRow Remap = *Row;
		Remap.MoveRoles.Reset();
		Remap.MoveRoles.Add(MoveRole(TEXT("Run"), EFishAnimRole::Dart));
		Remap.MoveRoles.Add(MoveRole(TEXT("QA_Zigzag"), EFishAnimRole::Dive));
		Remap.UnknownMoveRole = EFishAnimRole::Thrash;
		TestEqual(TEXT("fixture: Run -> Dart"), RoleName(FFightFishVisual::ComputeAnimState(Remap, SettledInput(TEXT("Run"))).Role), RoleName(EFishAnimRole::Dart));
		TestEqual(TEXT("fixture: a new move -> Dive"), RoleName(FFightFishVisual::ComputeAnimState(Remap, SettledInput(TEXT("QA_Zigzag"))).Role), RoleName(EFishAnimRole::Dive));
		TestEqual(TEXT("fixture: unlisted Swim -> the fixture's UnknownMoveRole"), RoleName(FFightFishVisual::ComputeAnimState(Remap, SettledInput(TEXT("Swim"))).Role),
			RoleName(EFishAnimRole::Thrash));
		return true;
	}

	/** Priorities and the hook-set window: Landed/Escaping over everything; Thrash first; tired over the move. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQARolesPriority, "Project.FishVisual.QA.Roles.ThrashWindowAndPriorities", Flags)
	bool FFishVisualQARolesPriority::RunTest(const FString& Parameters)
	{
		const FFishVisualRow Row = FFishVisualRow::GetFallbackRow();
		FFightFishAnimInput In = SettledInput(TEXT("Run"));
		In.SecondsSinceHook = Row.HookSetThrashTime - 0.001f;
		TestEqual(TEXT("just inside the hook-set window: Thrash"), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::Thrash));
		In.SecondsSinceHook = Row.HookSetThrashTime + 0.001f;
		TestEqual(TEXT("just after it: the move"), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(FFightFishVisual::RoleForMove(Row, TEXT("Run"))));
		In.SecondsSinceHook = 0.f;
		In.bExhausted = true;
		TestEqual(TEXT("tired right at the hook set: still Thrash first"), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::Thrash));
		In.SecondsSinceHook = 10.f;
		for (const FFishMoveAnimRole& Entry : Row.MoveRoles)
		{
			In.MoveId = Entry.MoveId;
			TestEqual(FString::Printf(TEXT("tired during %s: SwimIdle"), *Entry.MoveId.ToString()), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::SwimIdle));
		}
		In.bExhausted = false;
		FFishVisualRow NoThrash = Row;
		NoThrash.HookSetThrashTime = 0.f;
		In.MoveId = TEXT("Dive");
		In.SecondsSinceHook = 0.f;
		TestEqual(TEXT("HookSetThrashTime 0: no thrash even at t = 0"), RoleName(FFightFishVisual::ComputeAnimState(NoThrash, In).Role), RoleName(FFightFishVisual::RoleForMove(Row, TEXT("Dive"))));
		for (const float T : { 0.f, 0.5f, 10.f })
		{
			In.SecondsSinceHook = T;
			In.bExhausted = T > 1.f;
			In.AnimAmplitude = 0.3f;
			In.Phase = EFightFishPhase::Landed;
			const FFishAnimState Landed = FFightFishVisual::ComputeAnimState(Row, In);
			TestEqual(FString::Printf(TEXT("landed at t=%.1f: Flop"), T), RoleName(Landed.Role), RoleName(EFishAnimRole::Flop));
			TestEqual(FString::Printf(TEXT("landed at t=%.1f: alpha 1 whatever the species"), T), Landed.Amplitude, 1.f, 1.e-6f);
			In.Phase = EFightFishPhase::Escaping;
			TestEqual(FString::Printf(TEXT("escaping at t=%.1f: SwimFast"), T), RoleName(FFightFishVisual::ComputeAnimState(Row, In).Role), RoleName(EFishAnimRole::SwimFast));
			In.Phase = EFightFishPhase::Fighting;
		}
		return true;
	}

	/** Play rate and alpha: clamps at the edges, and always finite and in range for garbage inputs (the ABP must never get NaN). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQARates, "Project.FishVisual.QA.Roles.PlayRateAndAlphaEdges", Flags)
	bool FFishVisualQARates::RunTest(const FString& Parameters)
	{
		const FFishVisualRow Row = FFishVisualRow::GetFallbackRow();
		for (const FFishRoleTailBeat& Beat : Row.RoleTailBeats)
		{
			const FFishMoveAnimRole* Move = Row.MoveRoles.FindByPredicate([&Beat](const FFishMoveAnimRole& M) { return M.Role == Beat.Role; });
			if (!Move)
			{
				continue;
			}
			FFightFishAnimInput In = SettledInput(Move->MoveId);
			const float OneX = Row.StrideBodyLengths * In.BodyLengthCm * Beat.Hz; // speed for rate 1
			In.SpeedCmS = OneX;
			TestEqual(FString::Printf(TEXT("%s at the stride speed: rate 1"), *RoleName(Beat.Role)), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, 1.f, 1.e-4f);
			In.SpeedCmS = OneX * Row.MinPlayRate * 0.99f;
			TestEqual(FString::Printf(TEXT("%s just under MinPlayRate: clamped"), *RoleName(Beat.Role)), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, Row.MinPlayRate, 1.e-5f);
			In.SpeedCmS = OneX * Row.MinPlayRate * 1.01f;
			TestEqual(FString::Printf(TEXT("%s just over MinPlayRate: the formula"), *RoleName(Beat.Role)), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, Row.MinPlayRate * 1.01f, 1.e-4f);
			In.SpeedCmS = OneX * Row.MaxPlayRate * 1.01f;
			TestEqual(FString::Printf(TEXT("%s just over MaxPlayRate: clamped"), *RoleName(Beat.Role)), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, Row.MaxPlayRate, 1.e-5f);
			In.SpeedCmS = OneX * Row.MaxPlayRate * 0.99f;
			TestEqual(FString::Printf(TEXT("%s just under MaxPlayRate: the formula"), *RoleName(Beat.Role)), FFightFishVisual::ComputeAnimState(Row, In).PlayRate, Row.MaxPlayRate * 0.99f, 1.e-4f);
		}
		// Species amplitude outside 0..1 is clamped; garbage inputs stay finite.
		FFightFishAnimInput In = SettledInput(TEXT("Swim"));
		In.AnimAmplitude = 1.7f;
		TestTrue(TEXT("AnimAmplitude 1.7: alpha <= 1"), FFightFishVisual::ComputeAnimState(Row, In).Amplitude <= 1.f);
		In.AnimAmplitude = -0.5f;
		TestTrue(TEXT("AnimAmplitude -0.5: alpha >= 0"), FFightFishVisual::ComputeAnimState(Row, In).Amplitude >= 0.f);
		struct FBad { const TCHAR* What; TFunction<void(FFightFishAnimInput&)> Break; };
		const TArray<FBad> Bad = {
			{ TEXT("weight 0"), [](FFightFishAnimInput& I) { I.WeightKg = 0.f; } },
			{ TEXT("reference weight 0"), [](FFightFishAnimInput& I) { I.ReferenceWeightKg = 0.f; } },
			{ TEXT("NaN weight"), [](FFightFishAnimInput& I) { I.WeightKg = QNaN; } },
			{ TEXT("body length 0"), [](FFightFishAnimInput& I) { I.BodyLengthCm = 0.f; } },
			{ TEXT("NaN speed"), [](FFightFishAnimInput& I) { I.SpeedCmS = QNaN; } },
			{ TEXT("infinite speed"), [](FFightFishAnimInput& I) { I.SpeedCmS = QInf; } },
			{ TEXT("AnimRate 0"), [](FFightFishAnimInput& I) { I.AnimRate = 0.f; } },
			{ TEXT("NaN AnimAmplitude"), [](FFightFishAnimInput& I) { I.AnimAmplitude = QNaN; } },
		};
		const FName Moves[] = { TEXT("Swim"), TEXT("Sulk"), TEXT("Dart"), TEXT("Rest") };
		for (const FBad& Case : Bad)
		{
			for (const EFightFishPhase Phase : { EFightFishPhase::Fighting, EFightFishPhase::Landed, EFightFishPhase::Escaping })
			{
				for (const FName Move : Moves)
				{
					FFightFishAnimInput Input = SettledInput(Move);
					Input.Phase = Phase;
					Case.Break(Input);
					const FFishAnimState State = FFightFishVisual::ComputeAnimState(Row, Input);
					const bool bOk = FMath::IsFinite(State.PlayRate) && State.PlayRate >= 0.f && FMath::IsFinite(State.Amplitude) && State.Amplitude >= 0.f
						&& State.Amplitude <= 1.f && FMath::IsFinite(State.RoleBlendTime) && FMath::IsFinite(State.DartStartTime);
					if (!bOk)
					{
						AddError(FString::Printf(TEXT("%s, phase %d, move %s: rate %f alpha %f (must be finite, rate >= 0, alpha 0..1)"), Case.What, static_cast<int32>(Phase),
							*Move.ToString(), State.PlayRate, State.Amplitude));
					}
				}
			}
		}
		return true;
	}

	/** The subsystem plays the clips of the table it is given: a remapped fixture row drives the actor, no code. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQARolesActor, "Project.FishVisual.QA.Roles.TableDrivesTheActor", Flags)
	bool FFishVisualQARolesActor::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		const FFishVisualRow* Shipped = SettingsRow(Visual.Get());
		if (!TestNotNull(TEXT("Default row"), Shipped))
		{
			return false;
		}
		FFishVisualRow Remap = *Shipped;
		Remap.MoveRoles.Reset();
		Remap.MoveRoles.Add(MoveRole(TEXT("Run"), EFishAnimRole::Dart));
		Remap.MoveRoles.Add(MoveRole(TEXT("Dive"), EFishAnimRole::Run));
		Remap.MoveRoles.Add(MoveRole(TEXT("QA_Zigzag"), EFishAnimRole::Dive));
		Remap.UnknownMoveRole = EFishAnimRole::Thrash;
		Remap.HookSetThrashTime = 0.25f;
		FString Problem;
		if (!TestTrue(TEXT("the fixture row is valid"), Remap.Validate(Problem)))
		{
			return false;
		}
		const TStrongObjectPtr<UDataTable> Table = OneVisualTable(Remap);
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		FPuppet Puppet;
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !Puppet.Create(*this, World, LureFightQA::StandAt()))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Table.Get());
		Puppet.Start(1, MakeRecord(TEXT("Bonefish"), 1.5f), TEXT("Run"));
		World.Tick(2);
		ALureFightFish* Actor = Visuals->FindFish(Puppet.Fishing);
		if (!TestNotNull(TEXT("a fish"), Actor))
		{
			return false;
		}
		TestEqual(TEXT("hook set: Thrash"), RoleName(Actor->GetAnimState().Role), RoleName(EFishAnimRole::Thrash));
		World.Tick(30); // past the fixture's 0.25 s
		const TPair<const TCHAR*, EFishAnimRole> Cases[] = { { TEXT("Run"), EFishAnimRole::Dart }, { TEXT("Dive"), EFishAnimRole::Run },
			{ TEXT("QA_Zigzag"), EFishAnimRole::Dive }, { TEXT("Swim"), EFishAnimRole::Thrash } };
		for (const TPair<const TCHAR*, EFishAnimRole>& Case : Cases)
		{
			Puppet.Fight->MoveId = Case.Key;
			World.Tick(2);
			TestEqual(FString::Printf(TEXT("move %s plays the fixture's role"), Case.Key), RoleName(Actor->GetAnimState().Role), RoleName(Case.Value));
		}
		Puppet.Fight->bExhausted = true;
		World.Tick(2);
		TestEqual(TEXT("tired: SwimIdle"), RoleName(Actor->GetAnimState().Role), RoleName(EFishAnimRole::SwimIdle));
		TestEqual(TEXT("the actor holds the fixture row"), Actor->GetRow().HookSetThrashTime, 0.25f, 1.e-6f);
		Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
		World.Tick(1);
		return true;
	}

	// =================================================================================================================
	// Fallbacks: missing or broken table, missing mesh
	// =================================================================================================================

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAFallbackRow, "Project.FishVisual.QA.Fallback.VisualTableMissingOrBroken", Flags)
	bool FFishVisualQAFallbackRow::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		const FFishVisualRow* Shipped = SettingsRow(Visual.Get());
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !TestNotNull(TEXT("Default row"), Shipped))
		{
			return false;
		}
		const FFishVisualRow BuiltIn = FFishVisualRow::GetFallbackRow();

		FFishVisualRow Custom = *Shipped;
		Custom.SurfaceDepth = 77.f;
		const TStrongObjectPtr<UDataTable> Valid = OneVisualTable(Custom);
		Visuals->SetTables(Fish.Species.Get(), Valid.Get());
		TestEqual(TEXT("a valid table is used (SurfaceDepth 77)"), Visuals->GetVisualRow().SurfaceDepth, 77.f, 1.e-6f);

		FFishVisualRow Broken = *Shipped;
		Broken.MinScale = 5.f;
		const TStrongObjectPtr<UDataTable> Invalid = OneVisualTable(Broken);
		const TStrongObjectPtr<UDataTable> OtherRow = OneVisualTable(Custom, TEXT("QA_NotDefault"));
		TStrongObjectPtr<UDataTable> WrongStruct(FishQA::NewTable(FFishSpeciesRow::StaticStruct()));
		WrongStruct->AddRow(GetDefault<ULureFishVisualSettings>()->VisualRow, FFishSpeciesRow());
		TStrongObjectPtr<UDataTable> Empty(FishQA::NewTable(FFishVisualRow::StaticStruct()));
		const TPair<const TCHAR*, const UDataTable*> Cases[] = { { TEXT("invalid Default row (MinScale > MaxScale)"), Invalid.Get() },
			{ TEXT("no Default row"), OtherRow.Get() }, { TEXT("wrong row struct"), WrongStruct.Get() }, { TEXT("empty table"), Empty.Get() } };
		for (const TPair<const TCHAR*, const UDataTable*>& Case : Cases)
		{
			Visuals->SetTables(Fish.Species.Get(), Case.Value);
			TestTrue(FString::Printf(TEXT("%s: the built-in row"), Case.Key), SameVisualRow(Visuals->GetVisualRow(), BuiltIn));
		}
		Visuals->SetTables(Fish.Species.Get(), nullptr);
		TestTrue(TEXT("the settings' table (imported asset, or built-in when missing): same numbers as the JSON Default row"),
			SameVisualRow(Visuals->GetVisualRow(), *Shipped));

		// A fish spawned with the valid fixture uses its numbers (placement from data).
		FPuppet Puppet;
		if (!Puppet.Create(*this, World, LureFightQA::StandAt()))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Valid.Get());
		Puppet.Start(1, MakeRecord(TEXT("Bonefish"), 1.5f), TEXT("Rest"), 0.f);
		World.Tick(2);
		if (ALureFightFish* Actor = Visuals->FindFish(Puppet.Fishing))
		{
			TestEqual(TEXT("the fish sits SurfaceDepth 77 under the water"), static_cast<float>(Actor->GetLastTarget().Z), static_cast<float>(Puppet.BobberRest().Z) - 77.f, 0.5f);
		}
		else
		{
			AddError(TEXT("no fish with the fixture table"));
		}
		Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
		World.Tick(1);
		return true;
	}

	/** A missing skinned mesh falls back to FallbackMesh; with no mesh at all the fish is invisible but still moves, animates and lands. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAFallbackMesh, "Project.FishVisual.QA.Fallback.MeshMissing", Flags)
	bool FFishVisualQAFallbackMesh::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		const FFishSpeciesRow* Bonefish = FishQA::Row<FFishSpeciesRow>(Fish.Species, TEXT("Bonefish"));
		if (!TestNotNull(TEXT("Bonefish row"), Bonefish))
		{
			return false;
		}
		FFishSpeciesRow Missing = *Bonefish;
		Missing.SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/QA/Missing/SK_QA_Nope.SK_QA_Nope")));
		Missing.Mesh = TSoftObjectPtr<UStreamableRenderAsset>(FSoftObjectPath(TEXT("/Game/QA/Missing/SM_QA_Nope.SM_QA_Nope")));
		const TStrongObjectPtr<UDataTable> Species = SpeciesWith(Fish, { { TEXT("QA_MissingMesh"), Missing } });
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		FPuppet Puppet;
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !Puppet.Create(*this, World, LureFightQA::StandAt()))
		{
			return false;
		}
		Visuals->SetTables(Species.Get(), Visual.Get());
		ULureFishVisualSettings* Settings = GetMutableDefault<ULureFishVisualSettings>();
		const TSoftObjectPtr<USkeletalMesh> SavedFallback = Settings->FallbackMesh;
		ON_SCOPE_EXIT { Settings->FallbackMesh = SavedFallback; };
		int32 Landed = 0;
		Visuals->OnFightFishLandedNative.AddLambda([&Landed](ULureFishingComponent*, ALureFightFish*, const FFishInstance&) { ++Landed; });

		// 1) Species mesh missing: the settings' fallback (SK_Bonefish) if it is in this checkout.
		const bool bHaveFallback = !SavedFallback.IsNull() && FPackageName::DoesPackageExist(SavedFallback.ToSoftObjectPath().GetLongPackageName());
		Puppet.Start(1, MakeRecord(TEXT("QA_MissingMesh"), 1.5f), TEXT("Rest"));
		World.Tick(120);
		ALureFightFish* Actor = Visuals->FindFish(Puppet.Fishing);
		if (TestNotNull(TEXT("missing species mesh: a fish anyway"), Actor))
		{
			const USkeletalMesh* Mesh = Actor->GetMesh()->GetSkeletalMeshAsset();
			if (bHaveFallback)
			{
				TestEqual(TEXT("missing species mesh: the fallback mesh"), Mesh ? FSoftObjectPath(Mesh).ToString() : FString(), SavedFallback.ToSoftObjectPath().ToString());
				TestTrue(TEXT("with a mesh: visible"), Actor->GetMesh()->IsVisible());
				UAnimInstance* Anim = Actor->GetMesh()->GetAnimInstance();
				TestTrue(FString::Printf(TEXT("with a mesh: the anim instance is a UFishAnimInstance (%s)"), Anim ? *Anim->GetClass()->GetName() : TEXT("none")),
					Anim && Anim->IsA<UFishAnimInstance>());
				const float Off = static_cast<float>(FVector::Dist2D(Actor->GetMouthLocation(), Puppet.LineEnd()));
				TestTrue(FString::Printf(TEXT("with a mesh: the Mouth bone sits at the line end (%.1f cm off)"), Off), Off < 5.f);
			}
			else
			{
				AddInfo(TEXT("FallbackMesh is not in this checkout: fallback assignment not checked"));
			}
		}
		Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
		World.Tick(1);

		// 2) No mesh anywhere: invisible, but it still moves, animates and hands off.
		Settings->FallbackMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/QA/Missing/SK_QA_NoFallback.SK_QA_NoFallback")));
		Visuals->SetTables(Species.Get(), Visual.Get());
		Puppet.Start(2, MakeRecord(TEXT("QA_MissingMesh"), 1.5f), TEXT("Rest"));
		World.Tick(2);
		Actor = Visuals->FindFish(Puppet.Fishing);
		if (TestNotNull(TEXT("no mesh at all: the fish actor still exists"), Actor))
		{
			TestNull(TEXT("no mesh at all: nothing drawn (no mesh)"), Actor->GetMesh()->GetSkeletalMeshAsset());
			TestFalse(TEXT("no mesh at all: hidden"), Actor->GetMesh()->IsVisible());
			TestEqual(TEXT("no mesh at all: still thrashes at the hook set"), RoleName(Actor->GetAnimState().Role), RoleName(EFishAnimRole::Thrash));
			World.Tick(120);
			TestTrue(TEXT("no mesh at all: still under the line end"), FVector::Dist2D(Actor->GetActorLocation(), Puppet.LineEnd()) <= Actor->GetMouthOffsetCm() * Actor->GetFishScale() + 1.f);
			TestTrue(TEXT("no mesh at all: GetMouthLocation still at the line end"), FVector::Dist2D(Actor->GetMouthLocation(), Puppet.LineEnd()) < 1.f);
		}
		Puppet.End(ELureFightOutcome::Landed, ELureFishingResult::Landed);
		World.Tick(1);
		TestEqual(TEXT("both fights handed off"), Landed, 2);
		TestEqual(TEXT("nothing left"), CountFishActors(World.World), 0);
		return true;
	}

	// =================================================================================================================
	// Placement
	// =================================================================================================================

	/** TargetLocation: nose at the line end, under the surface always, deeper with depth (capped), above the floor. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAPlacementRules, "Project.FishVisual.QA.Placement.TargetRules", Flags)
	bool FFishVisualQAPlacementRules::RunTest(const FString& Parameters)
	{
		const FFishVisualRow Row = FFishVisualRow::GetFallbackRow();
		FFightFishView View;
		View.bFighting = true;
		View.PlayerLocation = FVector(0.f, 0.f, 190.f);
		View.WaterZ = -3.f;
		View.LineEnd = FVector(1000.f, 200.f, View.WaterZ);
		const FVector Forward = (View.LineEnd - View.PlayerLocation).GetSafeNormal2D();
		const float Mouth = 27.f;
		const float NoFloor = -UE_BIG_NUMBER;

		const FVector AtSurface = FFightFishVisual::TargetLocation(Row, View, Forward, Mouth, NoFloor);
		TestTrue(TEXT("depth 0: nose at the line end (center Mouth back toward the player)"), FVector2D(AtSurface).Equals(FVector2D(View.LineEnd - Forward * Mouth), 0.01));
		TestEqual(TEXT("depth 0: SurfaceDepth under the water"), static_cast<float>(AtSurface.Z), View.WaterZ - Row.SurfaceDepth, 0.01f);
		TestEqual(TEXT("ShownDepth(0) = SurfaceDepth"), FFightFishVisual::ShownDepth(Row, 0.f), Row.SurfaceDepth, 1.e-5f);
		TestEqual(TEXT("ShownDepth(100) = SurfaceDepth + DepthShare x 100"), FFightFishVisual::ShownDepth(Row, 100.f), Row.SurfaceDepth + Row.DepthShare * 100.f, 1.e-4f);
		TestEqual(TEXT("ShownDepth(huge) capped"), FFightFishVisual::ShownDepth(Row, 1.e6f), Row.SurfaceDepth + Row.MaxShownDepth, 1.e-3f);

		bool bMonotone = true;
		bool bUnder = true;
		bool bXYFixed = true;
		double PreviousZ = UE_BIG_NUMBER;
		for (int32 Step = -100; Step <= 1000; ++Step)
		{
			View.DepthCm = Step * 10.f;
			const FVector T = FFightFishVisual::TargetLocation(Row, View, Forward, Mouth, NoFloor);
			bUnder &= T.Z <= View.WaterZ && T.Z >= View.WaterZ - Row.SurfaceDepth - Row.MaxShownDepth - 0.01;
			bMonotone &= T.Z <= PreviousZ + 1.e-3;
			bXYFixed &= FVector2D(T).Equals(FVector2D(AtSurface), 0.01);
			PreviousZ = T.Z;
		}
		TestTrue(TEXT("never above the surface, never deeper than the cap (depth -1000..10000)"), bUnder);
		TestTrue(TEXT("deeper fight depth = never higher"), bMonotone);
		TestTrue(TEXT("depth never moves it off the line end horizontally"), bXYFixed);

		View.DepthCm = 200.f;
		const FVector Floored = FFightFishVisual::TargetLocation(Row, View, Forward, Mouth, View.WaterZ - 40.f);
		TestEqual(TEXT("shallow floor: FloorClearance above it"), static_cast<float>(Floored.Z), View.WaterZ - 40.f + Row.FloorClearance, 0.01f);
		View.DepthCm = 0.f;
		TestEqual(TEXT("floor below the shown depth: no effect"), static_cast<float>(FFightFishVisual::TargetLocation(Row, View, Forward, Mouth, View.WaterZ - 500.f).Z),
			View.WaterZ - Row.SurfaceDepth, 0.01f);
		View.DepthCm = 100.f;
		TestTrue(TEXT("floor above the water (a sandbar): still not above the surface"), FFightFishVisual::TargetLocation(Row, View, Forward, Mouth, View.WaterZ + 50.f).Z <= View.WaterZ);
		TestTrue(TEXT("mouth offset 0: center at the line end"), FVector2D(FFightFishVisual::TargetLocation(Row, View, Forward, 0.f, NoFloor)).Equals(FVector2D(View.LineEnd), 0.01));

		FFishVisualRow Flat = Row;
		Flat.DepthShare = 0.f;
		View.DepthCm = 500.f;
		TestEqual(TEXT("DepthShare 0: always SurfaceDepth"), static_cast<float>(FFightFishVisual::TargetLocation(Flat, View, Forward, Mouth, NoFloor).Z), View.WaterZ - Flat.SurfaceDepth, 0.01f);

		// Facing and smoothing.
		const FVector Fish(1000.f, 0.f, -30.f);
		const FRotator Fast = FFightFishVisual::FacingRotation(Row, FVector(0.f, 300.f, 0.f), Fish, View.PlayerLocation, FRotator::ZeroRotator, false);
		TestEqual(TEXT("swimming +Y fast: yaw 90"), static_cast<float>(Fast.Yaw), 90.f, 0.5f);
		TestEqual(TEXT("... not rolled"), static_cast<float>(Fast.Roll), 0.f, 0.01f);
		const FRotator Slow = FFightFishVisual::FacingRotation(Row, FVector(0.f, Row.MinFacingSpeed * 0.5f, 0.f), Fish, View.PlayerLocation, FRotator::ZeroRotator, false);
		TestEqual(TEXT("slower than MinFacingSpeed: faces away from the player"), static_cast<float>(Slow.Yaw), static_cast<float>((Fish - View.PlayerLocation).GetSafeNormal2D().Rotation().Yaw), 0.5f);
		TestEqual(TEXT("... level"), static_cast<float>(Slow.Pitch), 0.f, 0.01f);
		const FRotator Dive = FFightFishVisual::FacingRotation(Row, FVector(50.f, 0.f, -800.f), Fish, View.PlayerLocation, FRotator::ZeroRotator, false);
		TestTrue(FString::Printf(TEXT("a steep dive: pitch clamped to MaxPitchDeg (%.1f)"), Dive.Pitch), FMath::Abs(Dive.Pitch) <= Row.MaxPitchDeg + 0.01f && Dive.Pitch < 0.f);
		const FRotator Climb = FFightFishVisual::FacingRotation(Row, FVector(50.f, 0.f, 800.f), Fish, View.PlayerLocation, FRotator::ZeroRotator, false);
		TestTrue(FString::Printf(TEXT("a steep climb: pitch clamped (%.1f)"), Climb.Pitch), Climb.Pitch <= Row.MaxPitchDeg + 0.01f && Climb.Pitch > 0.f);
		const FRotator Tired = FFightFishVisual::FacingRotation(Row, FVector::ZeroVector, Fish, View.PlayerLocation, FRotator::ZeroRotator, true);
		TestEqual(TEXT("tired: rolled ExhaustedRollDeg"), static_cast<float>(Tired.Roll), Row.ExhaustedRollDeg, 0.01f);
		TestEqual(TEXT("SmoothAlpha: time constant 0 snaps"), FFightFishVisual::SmoothAlpha(Dt, 0.f), 1.f, 1.e-6f);
		TestEqual(TEXT("SmoothAlpha: no time, no move"), FFightFishVisual::SmoothAlpha(0.f, 0.2f), 0.f, 1.e-6f);
		TestEqual(TEXT("SmoothAlpha: 1 - exp(-dt/T)"), FFightFishVisual::SmoothAlpha(0.1f, 0.2f), 1.f - FMath::Exp(-0.5f), 1.e-5f);
		TestTrue(TEXT("SmoothAlpha: a long hitch stays <= 1"), FFightFishVisual::SmoothAlpha(100.f, 0.2f) <= 1.f);
		return true;
	}

	/** A real Coral Snapper fight that only dives: every frame the fish is under the water, on its line end, deeper with the dive. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAPlacementReal, "Project.FishVisual.QA.Placement.RealFightUnderTheLineEnd", Flags)
	bool FFishVisualQAPlacementReal::RunTest(const FString& Parameters)
	{
		FRealScene Scene;
		if (!Scene.Init(*this))
		{
			return false;
		}
		FFishInstance Snapper;
		if (!LureFightQA::RollFish(*this, Scene.Fish, TEXT("CoralSnapper"), TEXT("Common"), 0.4f, 73, Snapper))
		{
			return false;
		}
		const FFishSpeciesRow* Species = FishQA::Row<FFishSpeciesRow>(Scene.Fish.Species, TEXT("CoralSnapper"));
		const FLureFightPatternRow* Dive = Species ? Scene.Data.Pattern(Species->FightPatternId) : nullptr;
		if (!TestNotNull(TEXT("the Coral Snapper's pattern"), Dive))
		{
			return false;
		}
		const FLureFightMove* DiveMove = Dive->Moves.FindByPredicate([](const FLureFightMove& M) { return M.Id == FName(TEXT("Dive")); });
		if (!TestNotNull(TEXT("its Dive move"), DiveMove))
		{
			return false;
		}
		TStrongObjectPtr<UDataTable> OnlyDive(FishQA::NewTable(FLureFightPatternRow::StaticStruct()));
		OnlyDive->AddRow(Species->FightPatternId, LureFightQA::MakePattern({ *DiveMove }, TEXT("Dive")));
		Scene.Fishing->SetFightTables(Scene.Gear.Get(), OnlyDive.Get(), Scene.Data.Fight.Get());
		if (!LureFightQA::CastAndWait(*this, Scene.World, Scene.Fishing) || !TestTrue(TEXT("hooked"), Scene.Fishing->AuthorityHookFish(Snapper)))
		{
			return false;
		}
		const FFishVisualRow& Row = Scene.Visuals->GetVisualRow();
		int32 Frames = 0;
		int32 AboveWater = 0;
		int32 OffDepth = 0;
		int32 OffLine = 0;
		int32 FarFromTarget = 0;
		float MaxDepth = 0.f;
		float DeepestZ = 0.f;
		float WorstLine = 0.f;
		for (int32 Frame = 0; Frame < 30 * 60 && Scene.Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			Scene.World.Tick(1);
			const ALureFightFish* Actor = Scene.Visuals->FindFish(Scene.Fishing);
			const FFightFishView View = FFightFishViewAdapter::FromComponent(*Scene.Fishing);
			if (!Actor || !View.bFighting)
			{
				continue;
			}
			++Frames;
			AboveWater += Actor->GetActorLocation().Z >= View.WaterZ ? 1 : 0;
			const float WantZ = View.WaterZ - FFightFishVisual::ShownDepth(Row, View.DepthCm);
			OffDepth += FMath::Abs(Actor->GetLastTarget().Z - WantZ) > 5.f ? 1 : 0;
			const float LineOff = FMath::Abs(static_cast<float>(FVector::Dist2D(Actor->GetLastTarget(), View.LineEnd)) - Actor->GetMouthOffsetCm() * Actor->GetFishScale());
			WorstLine = FMath::Max(WorstLine, LineOff);
			OffLine += LineOff > 8.f ? 1 : 0;
			FarFromTarget += FVector::Dist(Actor->GetActorLocation(), Actor->GetLastTarget()) > 150.f ? 1 : 0;
			if (View.DepthCm > MaxDepth)
			{
				MaxDepth = View.DepthCm;
				DeepestZ = static_cast<float>(Actor->GetLastTarget().Z);
			}
		}
		TestTrue(FString::Printf(TEXT("the fight showed a fish for a while (%d frames)"), Frames), Frames > 60);
		TestEqual(TEXT("frames with the fish at or above the surface"), AboveWater, 0);
		TestEqual(TEXT("frames where the target depth is not SurfaceDepth + min(DepthShare x Depth, Max)"), OffDepth, 0);
		TestEqual(FString::Printf(TEXT("frames with the nose off the line end by > 8 cm (worst %.1f)"), WorstLine), OffLine, 0);
		TestEqual(TEXT("frames with the fish > 150 cm behind its target"), FarFromTarget, 0);
		TestTrue(FString::Printf(TEXT("the dive went down (max fight depth %.0f cm)"), MaxDepth), MaxDepth > 20.f);
		TestTrue(FString::Printf(TEXT("at the deepest the fish is deeper than at the surface (%.0f < %.0f)"), DeepestZ, -Row.SurfaceDepth), DeepestZ < -Row.SurfaceDepth - 5.f);
		return true;
	}

	/**
	 *  Server / standalone (single player and the host): each frame the fish is placed from THIS frame's fight state, not the
	 *  previous one (spec: the subsystem moves the fish after the fight and the characters have ticked). Per frame, the
	 *  target is compared with the targets built from the view before and after the world tick.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAThisFrame, "Project.FishVisual.QA.Placement.UsesThisFramesFightState", Flags)
	bool FFishVisualQAThisFrame::RunTest(const FString& Parameters)
	{
		FRealScene Scene;
		if (!Scene.Init(*this))
		{
			return false;
		}
		if (!LureFightQA::CastAndWait(*this, Scene.World, Scene.Fishing) || !TestTrue(TEXT("hooked"), Scene.Fishing->AuthorityHookFish(Scene.Bonefish)))
		{
			return false;
		}
		Scene.Fishing->AuthoritySetReeling(false); // the fish runs: its line end moves every frame
		Scene.World.Tick(1);
		int32 Compared = 0;
		int32 Current = 0;
		int32 Previous = 0;
		for (int32 Frame = 0; Frame < 10 * 60 && Scene.Fishing->GetFishingState() == ELureFishingState::Hooked; ++Frame)
		{
			const FFightFishView Before = FFightFishViewAdapter::FromComponent(*Scene.Fishing);
			Scene.World.Tick(1);
			const FFightFishView After = FFightFishViewAdapter::FromComponent(*Scene.Fishing);
			const ALureFightFish* Actor = Scene.Visuals->FindFish(Scene.Fishing);
			if (!Actor || !Before.bFighting || !After.bFighting || FVector::Dist2D(Before.LineEnd, After.LineEnd) < 0.5)
			{
				continue;
			}
			const double ToAfter = FMath::Abs(FVector::Dist2D(Actor->GetLastTarget(), After.LineEnd) - Actor->GetMouthOffsetCm() * Actor->GetFishScale())
				+ FVector::Dist2D(Actor->GetLastTarget(), After.LineEnd - (After.LineEnd - After.PlayerLocation).GetSafeNormal2D() * Actor->GetMouthOffsetCm() * Actor->GetFishScale());
			const double ToBefore = FMath::Abs(FVector::Dist2D(Actor->GetLastTarget(), Before.LineEnd) - Actor->GetMouthOffsetCm() * Actor->GetFishScale())
				+ FVector::Dist2D(Actor->GetLastTarget(), Before.LineEnd - (Before.LineEnd - Before.PlayerLocation).GetSafeNormal2D() * Actor->GetMouthOffsetCm() * Actor->GetFishScale());
			++Compared;
			Current += ToAfter < ToBefore ? 1 : 0;
			Previous += ToBefore < ToAfter ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("enough moving frames compared (%d)"), Compared), Compared > 30);
		TestEqual(FString::Printf(TEXT("frames placed from the previous frame's fight state (%d of %d; this frame's: %d)"), Previous, Compared, Current), Previous, 0);
		return true;
	}

	/** A lost fish swims away from the player at EscapeSpeed, sinking at EscapeSinkSpeed, and is removed after EscapeTime (0 = at once). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAEscape, "Project.FishVisual.QA.Placement.EscapeSwimsAwayAndSinks", Flags)
	bool FFishVisualQAEscape::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		LureFightQA::FWorld World;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual) || !World.Create(*this))
		{
			return false;
		}
		ULureFightFishSubsystem* Visuals = World.World->GetSubsystem<ULureFightFishSubsystem>();
		FPuppet Puppet;
		if (!TestNotNull(TEXT("subsystem"), Visuals) || !Puppet.Create(*this, World, LureFightQA::StandAt()))
		{
			return false;
		}
		Visuals->SetTables(Fish.Species.Get(), Visual.Get());
		const FFishVisualRow Row = Visuals->GetVisualRow();
		Puppet.Start(1, MakeRecord(TEXT("Bonefish"), 1.5f), TEXT("Rest"));
		World.Tick(120);
		TWeakObjectPtr<ALureFightFish> Weak = Visuals->FindFish(Puppet.Fishing);
		if (!TestTrue(TEXT("a fish"), Weak.IsValid()))
		{
			return false;
		}
		Puppet.End(ELureFightOutcome::ThrewHook, ELureFishingResult::ThrewHook);
		World.Tick(1);
		const FVector Player = Puppet.Player();
		const FVector Start = Weak->GetActorLocation();
		const int32 Half = FMath::RoundToInt(0.5f * Row.EscapeTime / Dt);
		World.Tick(Half);
		if (!TestTrue(TEXT("still there halfway through EscapeTime"), Weak.IsValid()))
		{
			return false;
		}
		const float T = Half * Dt;
		const float Away = static_cast<float>(FVector::Dist2D(Weak->GetActorLocation(), Player) - FVector::Dist2D(Start, Player));
		const float Sink = static_cast<float>(Start.Z - Weak->GetActorLocation().Z);
		TestEqual(TEXT("swims away from the player at EscapeSpeed"), Away, Row.EscapeSpeed * T, 0.1f * Row.EscapeSpeed * T + 2.f);
		TestEqual(TEXT("sinks at EscapeSinkSpeed"), Sink, Row.EscapeSinkSpeed * T, 0.1f * Row.EscapeSinkSpeed * T + 2.f);
		TestTrue(TEXT("stays under the water"), Weak->GetActorLocation().Z < Puppet.BobberRest().Z);
		World.Tick(FMath::Max(0, FMath::RoundToInt(Row.EscapeTime / Dt) - Half - 3));
		TestTrue(TEXT("still there just before EscapeTime"), Weak.IsValid());
		World.Tick(6);
		TestFalse(TEXT("gone just after EscapeTime"), Weak.IsValid());

		FFishVisualRow Instant = Row;
		Instant.EscapeTime = 0.f;
		const TStrongObjectPtr<UDataTable> InstantTable = OneVisualTable(Instant);
		Visuals->SetTables(Fish.Species.Get(), InstantTable.Get());
		Puppet.Start(2, MakeRecord(TEXT("Bonefish"), 1.5f));
		World.Tick(5);
		Weak = Visuals->FindFish(Puppet.Fishing);
		Puppet.End(ELureFightOutcome::Snapped, ELureFishingResult::Snapped);
		World.Tick(1);
		TestFalse(TEXT("EscapeTime 0: removed at once"), Weak.IsValid());
		TestEqual(TEXT("nothing left"), CountFishActors(World.World), 0);
		return true;
	}

	// =================================================================================================================
	// The adapter
	// =================================================================================================================

	/** Removes comments and string/char literals from C++ text (so docs and log text don't count as reads). */
	static FString StripCommentsAndStrings(const FString& Text)
	{
		FString Out;
		Out.Reserve(Text.Len());
		const int32 N = Text.Len();
		for (int32 I = 0; I < N; ++I)
		{
			const TCHAR C = Text[I];
			const TCHAR Next = I + 1 < N ? Text[I + 1] : TEXT('\0');
			if (C == TEXT('/') && Next == TEXT('/'))
			{
				while (I < N && Text[I] != TEXT('\n')) { ++I; }
				Out.AppendChar(TEXT('\n'));
			}
			else if (C == TEXT('/') && Next == TEXT('*'))
			{
				I += 2;
				while (I + 1 < N && !(Text[I] == TEXT('*') && Text[I + 1] == TEXT('/'))) { ++I; }
				++I;
				Out.AppendChar(TEXT(' '));
			}
			else if (C == TEXT('"') || C == TEXT('\''))
			{
				const TCHAR Quote = C;
				++I;
				while (I < N && Text[I] != Quote) { I += Text[I] == TEXT('\\') ? 2 : 1; }
				Out.AppendChar(TEXT(' '));
			}
			else
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	static bool HasToken(const FString& Code, const TCHAR* Token)
	{
		const int32 Len = FCString::Strlen(Token);
		int32 From = 0;
		while (true)
		{
			const int32 At = Code.Find(Token, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
			if (At == INDEX_NONE)
			{
				return false;
			}
			const bool bLeft = At == 0 || !(FChar::IsAlnum(Code[At - 1]) || Code[At - 1] == TEXT('_'));
			const bool bRight = At + Len >= Code.Len() || !(FChar::IsAlnum(Code[At + Len]) || Code[At + Len] == TEXT('_'));
			if (bLeft && bRight)
			{
				return true;
			}
			From = At + 1;
		}
	}

	/** Source scan: only Fishing/FightFishViewAdapter.* reads the fight's replicated structs; the visual files never do. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAAdapterOnly, "Project.FishVisual.QA.Adapter.OnlyReaderOfFightStructs", Flags)
	bool FFishVisualQAAdapterOnly::RunTest(const FString& Parameters)
	{
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source/VibeGame"));
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.*"), true, false);
		const TCHAR* Forbidden[] = { TEXT("FLureFightNetState"), TEXT("FLureFishingNetState"), TEXT("FLureFightState"), TEXT("GetFightNet"), TEXT("GetNetState"),
			TEXT("GetFishingNetState"), TEXT("GetFightState"), TEXT("GetHookedFish"), TEXT("GetFishingState"), TEXT("GetLastLandedFish"), TEXT("ELureFishingState"),
			TEXT("ELureFightOutcome"), TEXT("ELureFishingResult"), TEXT("IsServerReeling") };
		const TCHAR* ForbiddenIncludes[] = { TEXT("Fishing/FishFightTypes.h"), TEXT("Fishing/FishingTypes.h"), TEXT("Fishing/FishFight.h") };
		int32 VisualFiles = 0;
		bool bAdapterReads = false;
		for (const FString& File : Files)
		{
			const FString Normal = File.Replace(TEXT("\\"), TEXT("/"));
			const FString Name = FPaths::GetCleanFilename(Normal);
			if (!(Name.EndsWith(TEXT(".h")) || Name.EndsWith(TEXT(".cpp"))) || Normal.Contains(TEXT("/Tests/")))
			{
				continue;
			}
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File))
			{
				AddError(FString::Printf(TEXT("cannot read %s"), *File));
				continue;
			}
			const FString Code = StripCommentsAndStrings(Text);
			if (Name.StartsWith(TEXT("FightFishViewAdapter")))
			{
				bAdapterReads |= HasToken(Code, TEXT("FLureFightNetState")) && HasToken(Code, TEXT("GetFightNet"));
				continue;
			}
			const bool bVisual = Name.Contains(TEXT("FightFish")) || Name.Contains(TEXT("FishAnim")) || Name.Contains(TEXT("FishVisual"));
			if (!bVisual)
			{
				continue;
			}
			++VisualFiles;
			for (const TCHAR* Token : Forbidden)
			{
				if (HasToken(Code, Token))
				{
					AddError(FString::Printf(TEXT("%s uses %s: only FightFishViewAdapter may read the fight's structs (spec: fight-fish-visual.md)"), *Name, Token));
				}
			}
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				if (Line.TrimStart().StartsWith(TEXT("#include")))
				{
					for (const TCHAR* Include : ForbiddenIncludes)
					{
						if (Line.Contains(Include))
						{
							AddError(FString::Printf(TEXT("%s includes %s directly: go through FightFishViewAdapter"), *Name, Include));
						}
					}
				}
			}
		}
		TestTrue(TEXT("the scan found the visual files (FightFishVisual, LureFightFish, LureFightFishSubsystem, FishAnimInstance, FishVisualSettings: >= 10)"), VisualFiles >= 10);
		TestTrue(TEXT("the scan works: the adapter itself reads FLureFightNetState via GetFightNet"), bAdapterReads);
		return true;
	}

	/** Make(): fighting only when active AND hooked, for every line state; the ending for every outcome x result; line end geometry. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQAAdapterMap, "Project.FishVisual.QA.Adapter.EveryStateAndEnding", Flags)
	bool FFishVisualQAAdapterMap::RunTest(const FString& Parameters)
	{
		const UEnum* States = StaticEnum<ELureFishingState>();
		const UEnum* Outcomes = StaticEnum<ELureFightOutcome>();
		const UEnum* Results = StaticEnum<ELureFishingResult>();
		const FVector Player(0.f, 0.f, 190.f);
		FFishInstance Hooked = MakeRecord(TEXT("Bonefish"), 2.f);
		int32 Cases = 0;
		for (int32 S = 0; S < States->NumEnums() - 1; ++S)
		{
			for (const bool bActive : { false, true })
			{
				for (int32 O = 0; O < Outcomes->NumEnums() - 1; ++O)
				{
					for (int32 R = 0; R < Results->NumEnums() - 1; ++R)
					{
						FLureFightNetState Fight;
						Fight.bActive = bActive;
						Fight.FightId = 3;
						Fight.Outcome = static_cast<ELureFightOutcome>(Outcomes->GetValueByIndex(O));
						Fight.LineOut = 900.f;
						FLureFishingNetState Line;
						Line.State = static_cast<ELureFishingState>(States->GetValueByIndex(S));
						Line.LastResult = static_cast<ELureFishingResult>(Results->GetValueByIndex(R));
						Line.BobberRest = FVector(1500.f, 0.f, -2.f);
						const FFightFishView View = FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, true);
						const bool bFighting = bActive && Line.State == ELureFishingState::Hooked;
						const EFightFishEnd Want = bFighting ? EFightFishEnd::None
							: ((Fight.Outcome == ELureFightOutcome::Landed || Line.LastResult == ELureFishingResult::Landed) ? EFightFishEnd::Landed : EFightFishEnd::Escaped);
						++Cases;
						if (View.bFighting != bFighting || View.End != Want)
						{
							AddError(FString::Printf(TEXT("state %s active %d outcome %s result %s: fighting %d end %d (want %d / %d)"), *States->GetNameStringByIndex(S), bActive ? 1 : 0,
								*Outcomes->GetNameStringByIndex(O), *Results->GetNameStringByIndex(R), View.bFighting ? 1 : 0, static_cast<int32>(View.End), bFighting ? 1 : 0,
								static_cast<int32>(Want)));
						}
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("all %d combinations checked"), Cases), Cases > 100);

		FLureFightNetState Fight;
		Fight.bActive = true;
		Fight.LineOut = 1000.f;
		Fight.LineStrength = 0.f;
		Fight.Tension = 5.f;
		FLureFishingNetState Line;
		Line.State = ELureFishingState::Hooked;
		Line.BobberRest = FVector(0.f, 1500.f, -2.f);
		FFightFishView View = FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, false);
		TestTrue(TEXT("line end toward the bobber (+Y), LineOut away, at the bobber's height"), View.LineEnd.Equals(FVector(0.f, 1000.f, -2.f), 0.05));
		TestTrue(TEXT("LineStrength 0: tension share finite"), FMath::IsFinite(View.Tension01));
		Fight.SideDeg = 180.f;
		View = FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, false);
		TestTrue(TEXT("SideDeg 180: behind the player"), View.LineEnd.Equals(FVector(0.f, -1000.f, -2.f), 0.05));
		Fight.SideDeg = 0.f;
		Fight.LineOut = -50.f;
		View = FFightFishViewAdapter::Make(Fight, Line, Hooked, Player, FVector::ForwardVector, false);
		TestTrue(TEXT("negative LineOut: at the player, never behind"), FVector2D(View.LineEnd).Equals(FVector2D(Player), 0.05));
		TestEqual(TEXT("the record is passed through"), View.Fish.WeightKg, 2.f, 1.e-6f);
		return true;
	}

	// =================================================================================================================
	// Data validation
	// =================================================================================================================

	/** Every DT_FishVisual row against rules restated here; every fight move has a MoveRoles line; the settings point at it. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQADataVisual, "Project.FishVisual.QA.Data.FishVisualEveryRow", Flags)
	bool FFishVisualQADataVisual::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Table;
		LureFightQA::FFightTables Fight;
		if (!LoadVisual(*this, Table) || !Fight.Load(*this))
		{
			return false;
		}
		const ULureFishVisualSettings* Settings = GetDefault<ULureFishVisualSettings>();
		TestEqual(TEXT("settings: VisualTable = /Game/Data/DT_FishVisual"), Settings->VisualTable.ToSoftObjectPath().GetLongPackageName(), FString(TEXT("/Game/Data/DT_FishVisual")));
		TestNotNull(TEXT("the settings' row exists in the JSON"), SettingsRow(Table.Get()));
		TestTrue(TEXT("at least one row"), Table->GetRowMap().Num() > 0);
		const UEnum* Roles = StaticEnum<EFishAnimRole>();
		auto Range = [this](const FString& Where, const TCHAR* What, float Value, float Min, float Max)
		{
			if (!FMath::IsFinite(Value) || Value < Min || Value > Max)
			{
				AddError(FString::Printf(TEXT("%s: %s = %g not in [%g, %g]"), *Where, What, Value, Min, Max));
			}
		};
		Table->ForeachRow<FFishVisualRow>(TEXT("FishVisualQA"), [&](const FName& Name, const FFishVisualRow& Row)
		{
			const FString Where = FString::Printf(TEXT("DT_FishVisual %s"), *Name.ToString());
			FString Problem;
			TestTrue(FString::Printf(TEXT("%s: Validate (%s)"), *Where, *Problem), Row.Validate(Problem));
			TSet<FName> Moves;
			for (const FFishMoveAnimRole& Entry : Row.MoveRoles)
			{
				TestFalse(FString::Printf(TEXT("%s: a MoveRoles line without an id"), *Where), Entry.MoveId.IsNone());
				TestFalse(FString::Printf(TEXT("%s: move %s listed twice"), *Where, *Entry.MoveId.ToString()), Moves.Contains(Entry.MoveId));
				Moves.Add(Entry.MoveId);
				TestTrue(FString::Printf(TEXT("%s: move %s has a valid role"), *Where, *Entry.MoveId.ToString()), Roles->IsValidEnumValue(static_cast<int64>(Entry.Role)));
				TestTrue(FString::Printf(TEXT("%s: move %s never plays Flop (out of the water)"), *Where, *Entry.MoveId.ToString()), Entry.Role != EFishAnimRole::Flop);
			}
			TestTrue(FString::Printf(TEXT("%s: UnknownMoveRole is not Flop"), *Where), Row.UnknownMoveRole != EFishAnimRole::Flop);
			TSet<EFishAnimRole> Beats;
			for (const FFishRoleTailBeat& Beat : Row.RoleTailBeats)
			{
				Range(Where, *FString::Printf(TEXT("tail beat Hz of %s"), *RoleName(Beat.Role)), Beat.Hz, 0.01f, 20.f);
				TestFalse(FString::Printf(TEXT("%s: tail beat of %s listed twice"), *Where, *RoleName(Beat.Role)), Beats.Contains(Beat.Role));
				Beats.Add(Beat.Role);
			}
			TestTrue(FString::Printf(TEXT("%s: SwimFast (escaping) has a tail beat"), *Where), Beats.Contains(EFishAnimRole::SwimFast));
			Range(Where, TEXT("StrideBodyLengths"), Row.StrideBodyLengths, 0.05f, 5.f);
			Range(Where, TEXT("MinPlayRate"), Row.MinPlayRate, 0.01f, Row.MaxPlayRate);
			Range(Where, TEXT("MaxPlayRate"), Row.MaxPlayRate, Row.MinPlayRate, 5.f);
			Range(Where, TEXT("OtherRateWeightExponent"), Row.OtherRateWeightExponent, 0.f, 1.f);
			Range(Where, TEXT("RoleBlendTime"), Row.RoleBlendTime, 0.f, 1.f);
			Range(Where, TEXT("HookSetThrashTime"), Row.HookSetThrashTime, 0.f, 5.f);
			Range(Where, TEXT("DartRightStartTime"), Row.DartRightStartTime, 0.f, 5.f);
			Range(Where, TEXT("ExhaustedPlayRate"), Row.ExhaustedPlayRate, 0.01f, 5.f);
			Range(Where, TEXT("ExhaustedAmplitudeScale"), Row.ExhaustedAmplitudeScale, 0.f, 1.f);
			Range(Where, TEXT("ExhaustedRollDeg"), Row.ExhaustedRollDeg, -180.f, 180.f);
			Range(Where, TEXT("MinScale"), Row.MinScale, 0.01f, 1.f);
			Range(Where, TEXT("MaxScale"), Row.MaxScale, 1.f, 20.f);
			Range(Where, TEXT("DefaultBodyLengthCm"), Row.DefaultBodyLengthCm, 1.f, 1000.f);
			TestFalse(FString::Printf(TEXT("%s: MouthBone set"), *Where), Row.MouthBone.IsNone());
			Range(Where, TEXT("SurfaceDepth (the fish must be under the water)"), Row.SurfaceDepth, 1.f, 500.f);
			Range(Where, TEXT("DepthShare"), Row.DepthShare, 0.f, 1.f);
			Range(Where, TEXT("MaxShownDepth"), Row.MaxShownDepth, 0.f, 2000.f);
			Range(Where, TEXT("FloorClearance"), Row.FloorClearance, -1.f, 200.f);
			Range(Where, TEXT("AuthoritySmoothTime"), Row.AuthoritySmoothTime, 0.f, 2.f);
			Range(Where, TEXT("ProxySmoothTime"), Row.ProxySmoothTime, 0.f, 2.f);
			Range(Where, TEXT("SnapDistance"), Row.SnapDistance, 1.f, 100000.f);
			Range(Where, TEXT("RotationSmoothTime"), Row.RotationSmoothTime, 0.f, 2.f);
			Range(Where, TEXT("MinFacingSpeed"), Row.MinFacingSpeed, 0.f, 1000.f);
			Range(Where, TEXT("MaxPitchDeg"), Row.MaxPitchDeg, 0.f, 89.f);
			Range(Where, TEXT("EscapeTime"), Row.EscapeTime, 0.f, 10.f);
			Range(Where, TEXT("EscapeSpeed"), Row.EscapeSpeed, 0.f, 5000.f);
			Range(Where, TEXT("EscapeSinkSpeed"), Row.EscapeSinkSpeed, 0.f, 5000.f);
			// Every move a fish can make is mapped explicitly (a new move needs a MoveRoles line, not a silent fallback).
			Fight.Patterns->ForeachRow<FLureFightPatternRow>(TEXT("FishVisualQA"), [&](const FName& Pattern, const FLureFightPatternRow& P)
			{
				for (const FLureFightMove& Move : P.Moves)
				{
					TestTrue(FString::Printf(TEXT("%s: DT_FightPattern %s move %s has a MoveRoles line"), *Where, *Pattern.ToString(), *Move.Id.ToString()), Moves.Contains(Move.Id));
				}
			});
		});

		// Bad rows typed as JSON are refused (by import or by Validate).
		FString Text;
		LureFightQA::ReadSource(*this, TEXT("DT_FishVisual.json"), Text);
		struct FBad { const TCHAR* What; const TCHAR* From; const TCHAR* To; };
		const FBad Bad[] = {
			{ TEXT("unknown role name"), TEXT("{ \"MoveId\": \"Run\", \"Role\": \"Run\" }"), TEXT("{ \"MoveId\": \"Run\", \"Role\": \"Backflip\" }") },
			{ TEXT("a move listed twice"), TEXT("{ \"MoveId\": \"Dive\", \"Role\": \"Dive\" }"), TEXT("{ \"MoveId\": \"Run\", \"Role\": \"Dive\" }") },
			{ TEXT("0 Hz tail beat"), TEXT("{ \"Role\": \"SwimFast\", \"Hz\": 2.5 }"), TEXT("{ \"Role\": \"SwimFast\", \"Hz\": 0 }") },
			{ TEXT("MinPlayRate > MaxPlayRate"), TEXT("\"MinPlayRate\": 0.5"), TEXT("\"MinPlayRate\": 9") },
			{ TEXT("negative EscapeTime"), TEXT("\"EscapeTime\": 1.0"), TEXT("\"EscapeTime\": -1") },
			{ TEXT("MaxPitchDeg 120"), TEXT("\"MaxPitchDeg\": 30.0"), TEXT("\"MaxPitchDeg\": 120") },
		};
		for (const FBad& Case : Bad)
		{
			if (!TestTrue(FString::Printf(TEXT("%s: fixture text found in the JSON"), Case.What), Text.Contains(Case.From)))
			{
				continue;
			}
			TStrongObjectPtr<UDataTable> Broken;
			const TArray<FString> Problems = LureFightQA::MakeTable(Broken, FFishVisualRow::StaticStruct(), Text.Replace(Case.From, Case.To), true);
			const FFishVisualRow* Row = SettingsRow(Broken.Get());
			FString Why;
			const bool bRefused = Problems.Num() > 0 || !Row || !Row->Validate(Why);
			TestTrue(FString::Printf(TEXT("refused: %s (%s%s)"), Case.What, Problems.Num() > 0 ? *Problems[0] : TEXT(""), *Why), bRefused);
		}
		return true;
	}

	/** DT_FishSpecies look columns: ranges, the skinned mesh resolves to a SKEL_Fish mesh with the Mouth bone, the columns are optional. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQADataSpecies, "Project.FishVisual.QA.Data.SpeciesLookColumns", Flags)
	bool FFishVisualQADataSpecies::RunTest(const FString& Parameters)
	{
		FishQA::FTables Fish;
		TStrongObjectPtr<UDataTable> Visual;
		if (!FishQA::LoadReal(*this, Fish) || !LoadVisual(*this, Visual))
		{
			return false;
		}
		const FFishVisualRow* VisualRow = SettingsRow(Visual.Get());
		const FName Mouth = VisualRow ? VisualRow->MouthBone : FName(TEXT("Mouth"));
		int32 Resolved = 0;
		Fish.Species->ForeachRow<FFishSpeciesRow>(TEXT("FishVisualQA"), [&](const FName& Name, const FFishSpeciesRow& Species)
		{
			const FString Where = Name.ToString();
			TestTrue(FString::Printf(TEXT("%s: AnimAmplitude in [0, 1] (%g)"), *Where, Species.AnimAmplitude), FMath::IsFinite(Species.AnimAmplitude) && Species.AnimAmplitude >= 0.f && Species.AnimAmplitude <= 1.f);
			TestTrue(FString::Printf(TEXT("%s: AnimRate in (0, 3] (%g)"), *Where, Species.AnimRate), FMath::IsFinite(Species.AnimRate) && Species.AnimRate > 0.f && Species.AnimRate <= 3.f);
			TestTrue(FString::Printf(TEXT("%s: ReferenceWeight > 0 (the scale needs it)"), *Where), Species.ReferenceWeight > 0.f);
			FString Problem;
			TestTrue(FString::Printf(TEXT("%s: ValidateSpeciesLook"), *Where), FFightFishVisual::ValidateSpeciesLook(Species, Problem));
			if (!TestFalse(FString::Printf(TEXT("%s: has a SkeletalMesh"), *Where), Species.SkeletalMesh.IsNull()))
			{
				return;
			}
			const FString Package = Species.SkeletalMesh.ToSoftObjectPath().GetLongPackageName();
			if (!FPackageName::DoesPackageExist(Package))
			{
				AddInfo(FString::Printf(TEXT("%s: %s is not in this checkout (not loaded)"), *Where, *Package));
				return;
			}
			const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Species.SkeletalMesh.ToSoftObjectPath().TryLoad());
			if (!TestNotNull(FString::Printf(TEXT("%s: %s loads as a skeletal mesh"), *Where, *Package), Mesh))
			{
				return;
			}
			++Resolved;
			TestEqual(FString::Printf(TEXT("%s: on SKEL_Fish (ABP_Fish's skeleton)"), *Where), Mesh->GetSkeleton() ? Mesh->GetSkeleton()->GetName() : FString(), FString(TEXT("SKEL_Fish")));
			TestTrue(FString::Printf(TEXT("%s: has the %s bone (the line end)"), *Where, *Mouth.ToString()), Mesh->GetRefSkeleton().FindBoneIndex(Mouth) != INDEX_NONE);
			const float Length = static_cast<float>(2.0 * Mesh->GetImportedBounds().BoxExtent.X);
			TestTrue(FString::Printf(TEXT("%s: a fish-sized mesh along +X (%.1f cm)"), *Where, Length), Length > 15.f && Length < 300.f);
		});
		AddInfo(FString::Printf(TEXT("%d species meshes loaded and checked"), Resolved));

		struct FBad { const TCHAR* What; float Amplitude; float Rate; };
		const FBad Bad[] = { { TEXT("AnimAmplitude -0.1"), -0.1f, 1.f }, { TEXT("AnimAmplitude 1.1"), 1.1f, 1.f }, { TEXT("AnimRate 0"), 1.f, 0.f },
			{ TEXT("AnimRate -1"), 1.f, -1.f }, { TEXT("AnimRate NaN"), 1.f, QNaN }, { TEXT("AnimAmplitude NaN"), QNaN, 1.f } };
		for (const FBad& Case : Bad)
		{
			FFishSpeciesRow Row;
			Row.AnimAmplitude = Case.Amplitude;
			Row.AnimRate = Case.Rate;
			FString Why;
			TestFalse(FString::Printf(TEXT("refused: %s"), Case.What), FFightFishVisual::ValidateSpeciesLook(Row, Why));
			TestFalse(FString::Printf(TEXT("%s: says why"), Case.What), Why.IsEmpty());
		}

		// The three columns are optional: the JSON without them imports cleanly with 1 / 1 / None.
		FString Text;
		if (LureFightQA::ReadSource(*this, TEXT("DT_FishSpecies.json"), Text))
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, false);
			FString Stripped;
			for (const FString& Line : Lines)
			{
				const FString Trim = Line.TrimStart();
				if (!Trim.StartsWith(TEXT("\"SkeletalMesh\"")) && !Trim.StartsWith(TEXT("\"AnimAmplitude\"")) && !Trim.StartsWith(TEXT("\"AnimRate\"")))
				{
					Stripped += Line + TEXT("\n");
				}
			}
			TStrongObjectPtr<UDataTable> Old;
			const TArray<FString> Problems = LureFightQA::MakeTable(Old, FFishSpeciesRow::StaticStruct(), Stripped, true);
			TestEqual(FString::Printf(TEXT("without the look columns: no import problems (%s)"), Problems.Num() ? *Problems[0] : TEXT("")), Problems.Num(), 0);
			if (Old.IsValid())
			{
				Old->ForeachRow<FFishSpeciesRow>(TEXT("FishVisualQA"), [this](const FName& Name, const FFishSpeciesRow& Row)
				{
					TestTrue(FString::Printf(TEXT("%s without the columns: AnimRate 1, AnimAmplitude 1, no SkeletalMesh"), *Name.ToString()),
						Row.AnimRate == 1.f && Row.AnimAmplitude == 1.f && Row.SkeletalMesh.IsNull());
				});
			}
		}
		return true;
	}

	/** In the main checkout after the editor-operator's work: DT_FishVisual matches its JSON; ABP_Fish is a UFishAnimInstance on SKEL_Fish. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishVisualQADataAssets, "Project.FishVisual.QA.Data.ImportedAssetsMatch", Flags)
	bool FFishVisualQADataAssets::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UDataTable> Source;
		if (!LoadVisual(*this, Source))
		{
			return false;
		}
		const ULureFishVisualSettings* Settings = GetDefault<ULureFishVisualSettings>();
		const FString TablePackage = Settings->VisualTable.ToSoftObjectPath().GetLongPackageName();
		if (FPackageName::DoesPackageExist(TablePackage))
		{
			const UDataTable* Asset = Cast<UDataTable>(Settings->VisualTable.ToSoftObjectPath().TryLoad());
			if (TestNotNull(TEXT("DT_FishVisual loads"), Asset))
			{
				TestTrue(TEXT("row struct FFishVisualRow"), Asset->GetRowStruct() == FFishVisualRow::StaticStruct());
				TestEqual(TEXT("same row count as the JSON"), Asset->GetRowMap().Num(), Source->GetRowMap().Num());
				Source->ForeachRow<FFishVisualRow>(TEXT("FishVisualQA"), [this, Asset](const FName& Name, const FFishVisualRow& Row)
				{
					const FFishVisualRow* Imported = Asset->GetRowStruct() == FFishVisualRow::StaticStruct() ? Asset->FindRow<FFishVisualRow>(Name, TEXT("FishVisualQA"), false) : nullptr;
					TestTrue(FString::Printf(TEXT("row %s matches the JSON (reimport DT_FishVisual)"), *Name.ToString()), Imported && SameVisualRow(*Imported, Row));
				});
			}
		}
		else
		{
			AddInfo(FString::Printf(TEXT("%s not imported in this checkout: skipped"), *TablePackage));
		}
		const FSoftObjectPath AnimPath = Settings->AnimClass.ToSoftObjectPath();
		if (FPackageName::DoesPackageExist(AnimPath.GetLongPackageName()))
		{
			const UClass* Class = Cast<UClass>(AnimPath.TryLoad());
			TestTrue(FString::Printf(TEXT("%s is a child of UFishAnimInstance"), *AnimPath.ToString()), Class && Class->IsChildOf(UFishAnimInstance::StaticClass()));
		}
		else
		{
			AddInfo(FString::Printf(TEXT("%s not built in this checkout: skipped"), *AnimPath.ToString()));
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

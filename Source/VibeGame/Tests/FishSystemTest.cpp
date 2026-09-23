// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Fish/FishDataValidator.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

/**
 *  Implementer tests for the fish system (T-008). Tables come from the JSON sources in data/tables/ (never the binary
 *  /Game/Data assets), or are built in memory. Formulas: FishRoll.h. Rules: docs/specs/fish-system-rules.md.
 */
namespace FishTest
{

	static FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
	}

	struct FTestTables
	{
		TStrongObjectPtr<UDataTable> Species;
		TStrongObjectPtr<UDataTable> Rarities;
		TStrongObjectPtr<UDataTable> Modifiers;
		TStrongObjectPtr<UDataTable> Stats;
		FFishRollTuning Tuning;

		FFishTables Get() const
		{
			FFishTables Tables;
			Tables.Species = Species.Get();
			Tables.Rarities = Rarities.Get();
			Tables.Modifiers = Modifiers.Get();
			Tables.Stats = Stats.Get();
			Tables.Tuning = Tuning;
			return Tables;
		}
	};

	static UDataTable* NewTable(UScriptStruct* RowStruct)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = RowStruct;
		return Table;
	}

	static FString SourcePath(const TCHAR* FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
	}

	static FString ReadSource(FAutomationTestBase& Test, const TCHAR* FileName)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *SourcePath(FileName)))
		{
			Test.AddError(FString::Printf(TEXT("Can't read %s"), *SourcePath(FileName)));
		}
		return Json;
	}

	/** Imports JSON text into a new transient table and returns the import problems */
	static TArray<FString> ImportJson(TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const FString& Json)
	{
		Out.Reset(NewTable(RowStruct));
		return Out->CreateTableFromJSONString(Json);
	}

	static bool LoadSource(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const TCHAR* FileName)
	{
		const TArray<FString> Problems = ImportJson(Out, RowStruct, ReadSource(Test, FileName));
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), FileName, *Problem));
		}
		return Problems.Num() == 0 && Out->GetRowMap().Num() > 0;
	}

	/** The real starter data from data/tables/*.json */
	static bool LoadRealTables(FAutomationTestBase& Test, FTestTables& Out)
	{
		bool bOk = LoadSource(Test, Out.Species, FFishSpeciesRow::StaticStruct(), TEXT("DT_FishSpecies.json"));
		bOk &= LoadSource(Test, Out.Rarities, FFishRarityRow::StaticStruct(), TEXT("DT_FishRarity.json"));
		bOk &= LoadSource(Test, Out.Modifiers, FFishModifierRow::StaticStruct(), TEXT("DT_FishModifier.json"));
		bOk &= LoadSource(Test, Out.Stats, FFishStatRow::StaticStruct(), TEXT("DT_FishStat.json"));
		return bOk;
	}

	static bool Same(const FFishInstance& A, const FFishInstance& B)
	{
		return FFishInstance::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
	}

	static bool AllFinite(const FFishInstance& Fish)
	{
		bool bFinite = FMath::IsFinite(Fish.WeightKg) && FMath::IsFinite(Fish.DifficultyRating);
		for (const FFishStatValue& Stat : Fish.Stats)
		{
			bFinite &= FMath::IsFinite(Stat.Value);
		}
		return bFinite;
	}

	static FFishRollContext MakeContext(FName SpeciesId, int32 Seed)
	{
		FFishRollContext Context;
		Context.SpeciesId = SpeciesId;
		Context.Seed = Seed;
		Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
		Context.TimeOfDayHours = 10.0f;
		return Context;
	}

	/** |Observed - Expected| within Sigmas binomial standard deviations for N samples */
	static bool WithinSigma(double Observed, double Expected, int32 N, double Sigmas = 4.5)
	{
		const double Sd = FMath::Sqrt(FMath::Max(Expected * (1.0 - Expected), 0.0) / N);
		return FMath::Abs(Observed - Expected) <= Sigmas * Sd + 1e-9;
	}

	static FFishStatMod MakeMod(const TCHAR* Stat, EFishStatModOp Op, float Value)
	{
		FFishStatMod Mod;
		Mod.StatTag = Tag(Stat);
		Mod.Op = Op;
		Mod.Value = Value;
		return Mod;
	}

	static FFishModifierRow MakeModifier(const TCHAR* Name, float Chance, FName Group, TArray<FFishStatMod> Mods)
	{
		FFishModifierRow Row;
		Row.DisplayName = FText::FromString(Name);
		Row.RollChance = Chance;
		Row.ExclusivityGroup = Group;
		Row.StatMods = MoveTemp(Mods);
		return Row;
	}

	static FFishStatRow MakeStat(const TCHAR* TagName, float Min, float Max, float Default, bool bDifficulty)
	{
		FFishStatRow Row;
		Row.Tag = Tag(TagName);
		Row.DisplayName = FText::FromString(TagName);
		Row.Min = Min;
		Row.Max = Max;
		Row.Default = Default;
		Row.bIsDifficultyStat = bDifficulty;
		return Row;
	}

	/**
	 *  Small in-memory tables: stats Weight + Strength (difficulty); species T_Fish with a fixed 2 kg weight (= reference,
	 *  so the weight factor is exactly 1), Strength 10, 10 coins/kg; one tier T_Tier (weight 1, x1); no modifiers.
	 */
	static void MakeSyntheticTables(FTestTables& Out)
	{
		Out.Stats.Reset(NewTable(FFishStatRow::StaticStruct()));
		Out.Stats->AddRow(TEXT("Weight"), MakeStat(TEXT("Fish.Stat.Weight"), 0.01f, 1000.0f, 1.0f, false));
		Out.Stats->AddRow(TEXT("Strength"), MakeStat(TEXT("Fish.Stat.Strength"), 0.0f, 1000.0f, 0.0f, true));

		FFishSpeciesRow Fish;
		Fish.DisplayName = FText::FromString(TEXT("Test Fish"));
		Fish.JournalText = FText::FromString(TEXT("Test"));
		Fish.BaseLevel = 1;
		Fish.WeightMin = 2.0f;
		Fish.WeightMax = 2.0f;
		Fish.SizeSkew = 1.0f;
		Fish.ReferenceWeight = 2.0f;
		Fish.WeightStatExponent = 0.5f;
		Fish.BaseStats.Add(FFishStatValue(Tag(TEXT("Fish.Stat.Strength")), 10.0f));
		Fish.BaseValuePerKg = 10.0f;
		Fish.BiteWeight = 1.0f;
		Fish.MaxModifiers = 8;
		Fish.FightPatternId = TEXT("Test");
		Out.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct()));
		Out.Species->AddRow(TEXT("T_Fish"), Fish);

		FFishRarityRow Tier;
		Tier.DisplayName = FText::FromString(TEXT("Tier"));
		Tier.Rank = 0;
		Tier.RollWeight = 1.0f;
		Out.Rarities.Reset(NewTable(FFishRarityRow::StaticStruct()));
		Out.Rarities->AddRow(TEXT("T_Tier"), Tier);

		Out.Modifiers.Reset(NewTable(FFishModifierRow::StaticStruct()));
	}

	static bool AnyProblemMentions(const TArray<FString>& Problems, const FString& Text)
	{
		return Problems.ContainsByPredicate([&Text](const FString& Problem) { return Problem.Contains(Text); });
	}

	/** floor(X + 0.5), the pipeline's rounding for value and XP */
	static int32 RoundHalfUp(double X)
	{
		return static_cast<int32>(FMath::FloorToDouble(X + 0.5));
	}

	static int32 PickIndex(std::initializer_list<float> Weights, float U)
	{
		const TArray<float> Array(Weights);
		return FFishRoll::PickWeightedIndex(Array, U);
	}

// The tests below also live inside namespace FishTest (no file-scope using-directive, so unity builds stay clean)

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishDataSourcesImportTest, "Project.Fish.Data.SourcesImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishDataSourcesImportTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!TestTrue(TEXT("All four JSON sources import with no problems"), LoadRealTables(*this, T)))
	{
		return false;
	}
	const FFishTables Tables = T.Get();
	TestEqual(TEXT("2 starter species"), T.Species->GetRowMap().Num(), 2);
	TestNotNull(TEXT("Bonefish"), Tables.FindSpecies(TEXT("Bonefish")));
	TestNotNull(TEXT("CoralSnapper"), Tables.FindSpecies(TEXT("CoralSnapper")));
	for (const TCHAR* Id : { TEXT("Common"), TEXT("Uncommon"), TEXT("Rare") })
	{
		const FFishRarityRow* Row = Tables.FindRarity(Id);
		TestTrue(FString::Printf(TEXT("Rarity %s exists with RollWeight > 0"), Id), Row && Row->RollWeight > 0.0f);
	}
	for (const TCHAR* Id : { TEXT("Epic"), TEXT("Legendary") })
	{
		const FFishRarityRow* Row = Tables.FindRarity(Id);
		TestTrue(FString::Printf(TEXT("Rarity %s exists with RollWeight 0"), Id), Row && Row->RollWeight == 0.0f);
	}
	for (const TCHAR* Id : { TEXT("Heavy"), TEXT("Feisty"), TEXT("Giant"), TEXT("Albino") })
	{
		TestNotNull(FString::Printf(TEXT("Modifier %s"), Id), Tables.FindModifier(Id));
	}
	TestNotNull(TEXT("DT_FishStat has Fish.Stat.Weight"), Tables.FindStat(Tag(TEXT("Fish.Stat.Weight"))));
	TestNotNull(TEXT("DT_FishStat has Fish.Stat.Strength"), Tables.FindStat(Tag(TEXT("Fish.Stat.Strength"))));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishDataValidationTest, "Project.Fish.Data.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishDataValidationTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const TArray<FString> Problems = FFishDataValidator::Validate(T.Get());
	for (const FString& Problem : Problems)
	{
		AddError(TEXT("Data problem: ") + Problem);
	}
	TestEqual(TEXT("The starter data has no validation problems"), Problems.Num(), 0);

	// Reflection walk: every gameplay tag in every imported row is registered (belt and braces with the validator)
	int32 TagCount = 0;
	TFunction<void(const UStruct*, const void*, const FString&)> Walk = [&](const UStruct* Struct, const void* Data, const FString& Where)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			auto VisitValue = [&](const FProperty* Inner, const void* ValuePtr, const FString& ValueWhere)
			{
				if (const FStructProperty* StructProp = CastField<FStructProperty>(Inner))
				{
					if (StructProp->Struct == FGameplayTag::StaticStruct())
					{
						const FGameplayTag& Value = *static_cast<const FGameplayTag*>(ValuePtr);
						++TagCount;
						TestTrue(FString::Printf(TEXT("%s: tag '%s' is registered"), *ValueWhere, *Value.ToString()),
							Value.IsValid() && FGameplayTag::RequestGameplayTag(Value.GetTagName(), false).IsValid());
					}
					else
					{
						Walk(StructProp->Struct, ValuePtr, ValueWhere);
					}
				}
			};
			if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Data));
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					VisitValue(ArrayProp->Inner, Helper.GetRawPtr(i), FString::Printf(TEXT("%s.%s[%d]"), *Where, *Property->GetName(), i));
				}
			}
			else
			{
				VisitValue(Property, Property->ContainerPtrToValuePtr<void>(Data), Where + TEXT(".") + Property->GetName());
			}
		}
	};
	for (const TStrongObjectPtr<UDataTable>* Table : { &T.Species, &T.Rarities, &T.Modifiers, &T.Stats })
	{
		for (const TPair<FName, uint8*>& Row : (*Table)->GetRowMap())
		{
			Walk((*Table)->GetRowStruct(), Row.Value, (*Table)->GetRowStruct()->GetName() + TEXT(" ") + Row.Key.ToString());
		}
	}
	TestTrue(TEXT("The walk found tags"), TagCount > 20);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishValidatorCatchesBadDataTest, "Project.Fish.Data.ValidatorCatchesBadData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishValidatorCatchesBadDataTest::RunTest(const FString& Parameters)
{
	// Each case breaks one thing in a fresh copy of the real data and expects a problem naming it
	auto Check = [this](const TCHAR* Case, TFunctionRef<void(FTestTables&)> Break, const FString& Expected)
	{
		FTestTables T;
		if (!LoadRealTables(*this, T))
		{
			return;
		}
		Break(T);
		const TArray<FString> Problems = FFishDataValidator::Validate(T.Get());
		TestTrue(FString::Printf(TEXT("%s is reported (expected a problem mentioning '%s'; got: %s)"), Case, *Expected, *FString::Join(Problems, TEXT(" | "))),
			AnyProblemMentions(Problems, Expected));
	};

	Check(TEXT("WeightMin > WeightMax"), [](FTestTables& T)
	{
		FFishSpeciesRow Row = *T.Get().FindSpecies(TEXT("Bonefish"));
		Row.WeightMin = 5.0f;
		Row.WeightMax = 2.0f;
		T.Species->AddRow(TEXT("Bonefish"), Row);
	}, TEXT("WeightMin 5 must be <= WeightMax 2"));

	Check(TEXT("Negative rarity weight"), [](FTestTables& T)
	{
		FFishRarityRow Row = *T.Get().FindRarity(TEXT("Rare"));
		Row.RollWeight = -5.0f;
		T.Rarities->AddRow(TEXT("Rare"), Row);
	}, TEXT("DT_FishRarity row 'Rare'.RollWeight = -5 must be >= 0"));

	Check(TEXT("RollChance above 1"), [](FTestTables& T)
	{
		FFishModifierRow Row = *T.Get().FindModifier(TEXT("Feisty"));
		Row.RollChance = 1.5f;
		T.Modifiers->AddRow(TEXT("Feisty"), Row);
	}, TEXT("DT_FishModifier row 'Feisty'.RollChance = 1.5"));

	Check(TEXT("Dangling rarity id"), [](FTestTables& T)
	{
		FFishSpeciesRow Row = *T.Get().FindSpecies(TEXT("Bonefish"));
		Row.AllowedRarities = { TEXT("Common"), TEXT("NoSuchTier") };
		T.Species->AddRow(TEXT("Bonefish"), Row);
	}, TEXT("'NoSuchTier' is not a DT_FishRarity row"));

	Check(TEXT("Dangling species id in a modifier condition"), [](FTestTables& T)
	{
		FFishModifierRow Row = *T.Get().FindModifier(TEXT("Heavy"));
		Row.SpeciesIds = { TEXT("NoSuchFish") };
		T.Modifiers->AddRow(TEXT("Heavy"), Row);
	}, TEXT("'NoSuchFish' is not a DT_FishSpecies row"));

	Check(TEXT("Stat mod on a stat with no DT_FishStat row"), [](FTestTables& T)
	{
		T.Stats->RemoveRow(TEXT("Glow"));
		FFishModifierRow Row = *T.Get().FindModifier(TEXT("Albino"));
		Row.StatMods.Add(MakeMod(TEXT("Fish.Stat.Glow"), EFishStatModOp::Add, 5.0f));
		T.Modifiers->AddRow(TEXT("Albino"), Row);
	}, TEXT("stat 'Fish.Stat.Glow' has no DT_FishStat row"));

	Check(TEXT("A non-stat tag used as a stat"), [](FTestTables& T)
	{
		FFishModifierRow Row = *T.Get().FindModifier(TEXT("Albino"));
		Row.StatMods.Add(MakeMod(TEXT("Habitat.Shore"), EFishStatModOp::Add, 5.0f));
		T.Modifiers->AddRow(TEXT("Albino"), Row);
	}, TEXT("tag 'Habitat.Shore' must be under Fish.Stat.*"));

	Check(TEXT("Multiply by 0"), [](FTestTables& T)
	{
		FFishModifierRow Row = *T.Get().FindModifier(TEXT("Heavy"));
		Row.StatMods.Add(MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Multiply, 0.0f));
		T.Modifiers->AddRow(TEXT("Heavy"), Row);
	}, TEXT("Multiply value 0 must be > 0"));

	Check(TEXT("ValueMultiplier not increasing with Rank"), [](FTestTables& T)
	{
		FFishRarityRow Row = *T.Get().FindRarity(TEXT("Rare"));
		Row.ValueMultiplier = 1.2f;
		T.Rarities->AddRow(TEXT("Rare"), Row);
	}, TEXT("ValueMultiplier must strictly increase with Rank"));

	Check(TEXT("Duplicate rank"), [](FTestTables& T)
	{
		FFishRarityRow Row = *T.Get().FindRarity(TEXT("Legendary"));
		Row.Rank = 3;
		T.Rarities->AddRow(TEXT("Legendary"), Row);
	}, TEXT("Rank 3 is already used"));

	Check(TEXT("Empty time window"), [](FTestTables& T)
	{
		FFishSpeciesRow Row = *T.Get().FindSpecies(TEXT("Bonefish"));
		Row.TimeWindows = { FFishTimeWindow() };
		Row.TimeWindows[0].StartHour = 6.0f;
		Row.TimeWindows[0].EndHour = 6.0f;
		T.Species->AddRow(TEXT("Bonefish"), Row);
	}, TEXT("StartHour == EndHour"));

	Check(TEXT("Species that can roll nothing"), [](FTestTables& T)
	{
		FFishSpeciesRow Row = *T.Get().FindSpecies(TEXT("Bonefish"));
		Row.AllowedRarities = { TEXT("Epic"), TEXT("Legendary") };
		T.Species->AddRow(TEXT("Bonefish"), Row);
	}, TEXT("can't roll any rarity"));

	Check(TEXT("Missing weight stat row"), [](FTestTables& T)
	{
		T.Stats->RemoveRow(TEXT("Weight"));
	}, TEXT("no row for Fish.Stat.Weight"));

	// A typo in a tag string is caught at import (the engine reports it) or by the validator (the tag is empty)
	{
		FTestTables T;
		if (LoadRealTables(*this, T))
		{
			AddExpectedMessage(TEXT("Habitat.Shoer"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
			FString Json = ReadSource(*this, TEXT("DT_FishSpecies.json"));
			Json.ReplaceInline(TEXT("\"Habitat.Shore\""), TEXT("\"Habitat.Shoer\""));
			const TArray<FString> ImportProblems = ImportJson(T.Species, FFishSpeciesRow::StaticStruct(), Json);
			const TArray<FString> Problems = FFishDataValidator::Validate(T.Get());
			TestTrue(FString::Printf(TEXT("Tag typo 'Habitat.Shoer' is caught (import: %s | validator: %s)"),
				*FString::Join(ImportProblems, TEXT(" | ")), *FString::Join(Problems, TEXT(" | "))),
				ImportProblems.Num() > 0 || AnyProblemMentions(Problems, TEXT("HabitatTags")));
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishRollDeterministicTest, "Project.Fish.Roll.Deterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishRollDeterministicTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	FTestTables Fresh;
	if (!LoadRealTables(*this, T) || !LoadRealTables(*this, Fresh))
	{
		return false;
	}
	const FFishTables Tables = T.Get();

	int32 Mismatches = 0;
	int32 Failures = 0;
	TArray<int32> Seeds = { 0, 1, -1, MAX_int32, MIN_int32, 12345 };
	for (int32 Seed = 1; Seed <= 300; ++Seed)
	{
		Seeds.Add(Seed);
	}
	for (const FName Species : { FName(TEXT("Bonefish")), FName(TEXT("CoralSnapper")) })
	{
		for (const int32 Seed : Seeds)
		{
			const FFishRollContext Context = MakeContext(Species, Seed);
			FFishInstance A, B, C;
			const bool bOk = FFishRoll::Roll(Tables, Context, A) && FFishRoll::Roll(Tables, Context, B) && FFishRoll::Roll(Fresh.Get(), Context, C);
			Failures += bOk ? 0 : 1;
			Mismatches += (Same(A, B) && Same(A, C) && A.Seed == Seed && AllFinite(A) && A.IsValid()) ? 0 : 1;
		}
	}
	TestEqual(TEXT("Every roll succeeds"), Failures, 0);
	TestEqual(TEXT("Same seed (and same data re-imported) gives the identical instance"), Mismatches, 0);

	// Different seeds give different fish
	TSet<float> Weights;
	for (int32 Seed = 1; Seed <= 1000; ++Seed)
	{
		FFishInstance Fish;
		FFishRoll::Roll(Tables, MakeContext(TEXT("CoralSnapper"), Seed), Fish);
		Weights.Add(Fish.WeightKg);
	}
	TestTrue(FString::Printf(TEXT("1000 seeds give >= 950 distinct weights (got %d)"), Weights.Num()), Weights.Num() >= 950);

	// Stage isolation: an extra modifier row or more luck never changes the weight; the extra row never changes the rarity
	FTestTables Extra;
	LoadRealTables(*this, Extra);
	Extra.Modifiers->AddRow(TEXT("AAA_Extra"), MakeModifier(TEXT("Extra"), 0.5f, NAME_None, {}));
	int32 WeightChanged = 0;
	int32 RarityChanged = 0;
	for (int32 Seed = 1; Seed <= 500; ++Seed)
	{
		FFishRollContext Context = MakeContext(TEXT("Bonefish"), Seed);
		FFishInstance Base, WithRow, Lucky;
		FFishRoll::Roll(Tables, Context, Base);
		FFishRoll::Roll(Extra.Get(), Context, WithRow);
		Context.Luck = 5.0f;
		FFishRoll::Roll(Tables, Context, Lucky);
		const FGameplayTag WeightTag = Tag(TEXT("Fish.Stat.Weight"));
		// The modifier stage is one stream in id order, so the extra row may shift which modifiers hit: compare the
		// weight roll only when no weight modifier (Heavy, Giant) is on any of the three catches
		auto HasWeightMod = [](const FFishInstance& Fish) { return Fish.HasModifier(TEXT("Heavy")) || Fish.HasModifier(TEXT("Giant")); };
		const bool bNoWeightMods = !HasWeightMod(Base) && !HasWeightMod(WithRow) && !HasWeightMod(Lucky);
		if (bNoWeightMods && (Base.WeightKg != WithRow.WeightKg || Base.WeightKg != Lucky.WeightKg))
		{
			++WeightChanged;
		}
		RarityChanged += (Base.RarityId != WithRow.RarityId) ? 1 : 0;
		TestEqual(TEXT("WeightKg equals GetStat(Fish.Stat.Weight)"), Base.WeightKg, Base.GetStat(WeightTag));
	}
	TestEqual(TEXT("Weight doesn't depend on modifier rows or luck"), WeightChanged, 0);
	TestEqual(TEXT("Rarity doesn't depend on modifier rows"), RarityChanged, 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishRollFailsGracefullyTest, "Project.Fish.Roll.FailsGracefully", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishRollFailsGracefullyTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	AddExpectedMessage(TEXT("Fish roll"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

	FFishInstance Fish;
	TestFalse(TEXT("Unknown species fails"), FFishRoll::Roll(T.Get(), MakeContext(TEXT("NoSuchFish"), 1), Fish));
	TestFalse(TEXT("... and leaves an empty instance"), Fish.IsValid());
	TestFalse(TEXT("None species fails"), FFishRoll::Roll(T.Get(), MakeContext(NAME_None, 1), Fish));

	FFishTables NoStats = T.Get();
	NoStats.Stats = nullptr;
	TestFalse(TEXT("Missing DT_FishStat fails"), FFishRoll::Roll(NoStats, MakeContext(TEXT("Bonefish"), 1), Fish));

	FFishTables NoRarities = T.Get();
	NoRarities.Rarities = nullptr;
	TestFalse(TEXT("Missing DT_FishRarity fails"), FFishRoll::Roll(NoRarities, MakeContext(TEXT("Bonefish"), 1), Fish));

	FFishRollContext ZeroTiers = MakeContext(TEXT("Bonefish"), 1);
	FFishSpeciesRow OnlyDisabled = *T.Get().FindSpecies(TEXT("Bonefish"));
	OnlyDisabled.AllowedRarities = { TEXT("Epic"), TEXT("Legendary") };
	T.Species->AddRow(TEXT("T_OnlyDisabled"), OnlyDisabled);
	ZeroTiers.SpeciesId = TEXT("T_OnlyDisabled");
	TestFalse(TEXT("A species whose tiers all have RollWeight 0 fails (never rolls a disabled tier)"), FFishRoll::Roll(T.Get(), ZeroTiers, Fish));

	FFishTables EmptyModifiers = T.Get();
	TStrongObjectPtr<UDataTable> Empty(NewTable(FFishModifierRow::StaticStruct()));
	EmptyModifiers.Modifiers = Empty.Get();
	TestTrue(TEXT("An empty modifier table is fine (no modifiers)"), FFishRoll::Roll(EmptyModifiers, MakeContext(TEXT("Bonefish"), 1), Fish) && Fish.ModifierIds.Num() == 0);

	FFishRollContext NaNLuck = MakeContext(TEXT("Bonefish"), 7);
	NaNLuck.Luck = std::numeric_limits<float>::quiet_NaN();
	FFishInstance LuckyNaN, Plain;
	FFishRoll::Roll(T.Get(), NaNLuck, LuckyNaN);
	FFishRoll::Roll(T.Get(), MakeContext(TEXT("Bonefish"), 7), Plain);
	TestTrue(TEXT("NaN luck counts as 0"), Same(LuckyNaN, Plain));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishRarityOddsTest, "Project.Fish.Roll.RarityOdds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishRarityOddsTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const FFishTables Tables = T.Get();
	constexpr int32 N = 10000;
	for (const FName Species : { FName(TEXT("Bonefish")), FName(TEXT("CoralSnapper")) })
	{
		const TArray<TPair<FName, float>> Weights = FFishRoll::GetRarityWeights(Tables, *Tables.FindSpecies(Species), 0.0f);
		double Total = 0.0;
		for (const TPair<FName, float>& Entry : Weights) { Total += Entry.Value; }

		TMap<FName, int32> Counts;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(Tables, MakeContext(Species, Seed), Fish);
			Counts.FindOrAdd(Fish.RarityId)++;
		}
		for (const TPair<FName, float>& Entry : Weights)
		{
			const double Expected = Entry.Value / Total;
			const double Observed = static_cast<double>(Counts.FindRef(Entry.Key)) / N;
			if (Entry.Value <= 0.0f)
			{
				TestEqual(FString::Printf(TEXT("%s: disabled tier %s never rolls"), *Species.ToString(), *Entry.Key.ToString()), Counts.FindRef(Entry.Key), 0);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: %s share %.4f is within 4.5 sigma of %.4f"), *Species.ToString(), *Entry.Key.ToString(), Observed, Expected),
					WithinSigma(Observed, Expected, N));
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishLuckTest, "Project.Fish.Roll.LuckShiftsRarity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishLuckTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const FFishTables Tables = T.Get();
	constexpr int32 N = 10000;
	auto RareShare = [&Tables](float Luck, int32& OutDisabled)
	{
		int32 Rare = 0;
		OutDisabled = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FFishRollContext Context = MakeContext(TEXT("Bonefish"), Seed);
			Context.Luck = Luck;
			FFishInstance Fish;
			FFishRoll::Roll(Tables, Context, Fish);
			Rare += Fish.RarityId == TEXT("Rare") ? 1 : 0;
			OutDisabled += (Fish.RarityId == TEXT("Epic") || Fish.RarityId == TEXT("Legendary")) ? 1 : 0;
		}
		return static_cast<double>(Rare) / N;
	};
	int32 Disabled0 = 0, Disabled4 = 0, DisabledNeg = 0, DisabledHuge = 0;
	const double Share0 = RareShare(0.0f, Disabled0);
	const double Share4 = RareShare(4.0f, Disabled4);
	const double ShareNeg = RareShare(-3.0f, DisabledNeg);
	RareShare(1e6f, DisabledHuge);

	// Luck 4: weights Common 70, Uncommon 22 x (1 + 4 x 1 x 0.25) = 44, Rare 8 x (1 + 4 x 2 x 0.25) = 24 -> Rare 24/138
	TestTrue(FString::Printf(TEXT("Luck 0: Rare share %.4f is ~8/100"), Share0), WithinSigma(Share0, 8.0 / 100.0, N));
	TestTrue(FString::Printf(TEXT("Luck 4: Rare share %.4f is ~24/138"), Share4), WithinSigma(Share4, 24.0 / 138.0, N));
	TestTrue(TEXT("Luck raises the Rare share"), Share4 > Share0);
	TestEqual(TEXT("Negative luck counts as 0"), ShareNeg, Share0);
	TestEqual(TEXT("Luck never unlocks a disabled tier (luck 0, 4, -3, 1e6)"), Disabled0 + Disabled4 + DisabledNeg + DisabledHuge, 0);

	// The luck formula itself
	const TArray<TPair<FName, float>> Weights = FFishRoll::GetRarityWeights(Tables, *Tables.FindSpecies(TEXT("Bonefish")), 4.0f);
	TestEqual(TEXT("Weights sorted by rank"), Weights.Num(), 5);
	if (Weights.Num() == 5)
	{
		TestEqual(TEXT("Common x1"), Weights[0].Value, 70.0f);
		TestEqual(TEXT("Uncommon x2"), Weights[1].Value, 44.0f);
		TestEqual(TEXT("Rare x3"), Weights[2].Value, 24.0f);
		TestEqual(TEXT("Epic stays 0"), Weights[3].Value, 0.0f);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishModifierOrderTest, "Project.Fish.Roll.ModifierAddsThenMultiplies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishModifierOrderTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Strength = Tag(TEXT("Fish.Stat.Strength"));

	// Mod A lists its Multiply first on purpose: all Adds still go first. (10 + 5 + 3) x 2 x 1.5 = 54, never 49.5 or 40.5
	auto RollWith = [&](bool bReverseRows) -> float
	{
		FTestTables T;
		MakeSyntheticTables(T);
		const FFishModifierRow A = MakeModifier(TEXT("A"), 1.0f, NAME_None, { MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Multiply, 2.0f), MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Add, 5.0f) });
		const FFishModifierRow B = MakeModifier(TEXT("B"), 1.0f, NAME_None, { MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Add, 3.0f), MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Multiply, 1.5f) });
		if (bReverseRows)
		{
			T.Modifiers->AddRow(TEXT("T_ModB"), B);
			T.Modifiers->AddRow(TEXT("T_ModA"), A);
		}
		else
		{
			T.Modifiers->AddRow(TEXT("T_ModA"), A);
			T.Modifiers->AddRow(TEXT("T_ModB"), B);
		}
		FFishInstance Fish;
		FFishRollContext Context = MakeContext(TEXT("T_Fish"), 99);
		TestTrue(TEXT("Roll succeeds"), FFishRoll::Roll(T.Get(), Context, Fish));
		TestEqual(TEXT("Both modifiers kept, in id order"), Fish.ModifierIds, TArray<FName>{ TEXT("T_ModA"), TEXT("T_ModB") });

		// Forcing the same modifiers gives the same stats
		Context.bForceModifiers = true;
		Context.ForcedModifierIds = { TEXT("T_ModB"), TEXT("T_ModA"), TEXT("T_ModB") };
		FFishInstance Forced;
		FFishRoll::Roll(T.Get(), Context, Forced);
		TestTrue(TEXT("Forced modifiers (any order, duplicates) give the same fish"), Same(Fish, Forced));
		return Fish.GetStat(Strength);
	};
	TestEqual(TEXT("Adds first, then Multiplies (product): (10 + 5 + 3) x 2 x 1.5"), RollWith(false), 54.0f);
	TestEqual(TEXT("Row order in the table doesn't matter"), RollWith(true), 54.0f);

	// Rarity mods apply before modifier mods: rarity x2, then modifier +5 -> 10 x 2 + 5 = 25
	FTestTables T;
	MakeSyntheticTables(T);
	FFishRarityRow Tier = *T.Get().FindRarity(TEXT("T_Tier"));
	Tier.StatMods = { MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Multiply, 2.0f) };
	T.Rarities->AddRow(TEXT("T_Tier"), Tier);
	T.Modifiers->AddRow(TEXT("T_Plus5"), MakeModifier(TEXT("Plus5"), 1.0f, NAME_None, { MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Add, 5.0f) }));
	FFishInstance Fish;
	FFishRoll::Roll(T.Get(), MakeContext(TEXT("T_Fish"), 3), Fish);
	TestEqual(TEXT("Rarity step before modifiers: 10 x 2 + 5"), Fish.GetStat(Strength), 25.0f);

	// Clamp after everything: a huge multiply ends at the stat Max, never Inf
	T.Modifiers->AddRow(TEXT("T_Huge"), MakeModifier(TEXT("Huge"), 1.0f, NAME_None, { MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Multiply, 1e30f), MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Multiply, 1e30f) }));
	FFishRoll::Roll(T.Get(), MakeContext(TEXT("T_Fish"), 3), Fish);
	TestEqual(TEXT("Huge multiplies clamp to the stat Max"), Fish.GetStat(Strength), 1000.0f);
	TestTrue(TEXT("Everything is finite"), AllFinite(Fish));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishExclusivityTest, "Project.Fish.Roll.ExclusivityAndCap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishExclusivityTest::RunTest(const FString& Parameters)
{
	// Three chance-1 modifiers in group G: exactly one kept, weighted by RollChance (equal here -> ~1/3 each)
	{
		FTestTables T;
		MakeSyntheticTables(T);
		for (const TCHAR* Id : { TEXT("T_G1"), TEXT("T_G2"), TEXT("T_G3") })
		{
			T.Modifiers->AddRow(Id, MakeModifier(Id, 1.0f, TEXT("G"), {}));
		}
		T.Modifiers->AddRow(TEXT("T_Free1"), MakeModifier(TEXT("Free1"), 1.0f, NAME_None, {}));
		T.Modifiers->AddRow(TEXT("T_Free2"), MakeModifier(TEXT("Free2"), 1.0f, NAME_None, {}));

		constexpr int32 N = 6000;
		TMap<FName, int32> Winners;
		int32 BadGroupCount = 0;
		int32 MissingFree = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), MakeContext(TEXT("T_Fish"), Seed), Fish);
			int32 InGroup = 0;
			for (const FName& Id : Fish.ModifierIds)
			{
				if (Id.ToString().StartsWith(TEXT("T_G")))
				{
					++InGroup;
					Winners.FindOrAdd(Id)++;
				}
			}
			BadGroupCount += InGroup == 1 ? 0 : 1;
			MissingFree += (Fish.HasModifier(TEXT("T_Free1")) && Fish.HasModifier(TEXT("T_Free2"))) ? 0 : 1;
		}
		TestEqual(TEXT("Exactly one modifier of the group per catch"), BadGroupCount, 0);
		TestEqual(TEXT("Ungrouped chance-1 modifiers are always kept"), MissingFree, 0);
		for (const TCHAR* Id : { TEXT("T_G1"), TEXT("T_G2"), TEXT("T_G3") })
		{
			const double Share = static_cast<double>(Winners.FindRef(Id)) / N;
			TestTrue(FString::Printf(TEXT("%s wins ~1/3 of the time (%.4f)"), Id, Share), WithinSigma(Share, 1.0 / 3.0, N));
		}
	}

	// MaxModifiers keeps the first hits in id order
	{
		FTestTables T;
		MakeSyntheticTables(T);
		FFishSpeciesRow Fish = *T.Get().FindSpecies(TEXT("T_Fish"));
		Fish.MaxModifiers = 2;
		T.Species->AddRow(TEXT("T_Fish"), Fish);
		for (const TCHAR* Id : { TEXT("T_M4"), TEXT("T_M2"), TEXT("T_M1"), TEXT("T_M3") })
		{
			T.Modifiers->AddRow(Id, MakeModifier(Id, 1.0f, NAME_None, {}));
		}
		FFishInstance Rolled;
		FFishRoll::Roll(T.Get(), MakeContext(TEXT("T_Fish"), 5), Rolled);
		TestEqual(TEXT("Cap 2 keeps T_M1 and T_M2"), Rolled.ModifierIds, TArray<FName>{ TEXT("T_M1"), TEXT("T_M2") });
	}

	// Conditions: a modifier restricted to another species never appears and doesn't block its group
	{
		FTestTables T;
		MakeSyntheticTables(T);
		FFishModifierRow Other = MakeModifier(TEXT("Other"), 1.0f, TEXT("G"), {});
		Other.SpeciesIds = { TEXT("SomeOtherFish") };
		T.Modifiers->AddRow(TEXT("T_A_Other"), Other);
		T.Modifiers->AddRow(TEXT("T_B_Mine"), MakeModifier(TEXT("Mine"), 1.0f, TEXT("G"), {}));
		FFishModifierRow Night = MakeModifier(TEXT("Night"), 1.0f, NAME_None, {});
		Night.TimeWindows = { FFishTimeWindow() };
		Night.TimeWindows[0].StartHour = 20.0f;
		Night.TimeWindows[0].EndHour = 4.0f;
		T.Modifiers->AddRow(TEXT("T_Night"), Night);

		FFishInstance Day, Late;
		FFishRoll::Roll(T.Get(), MakeContext(TEXT("T_Fish"), 1), Day);
		FFishRollContext LateContext = MakeContext(TEXT("T_Fish"), 1);
		LateContext.TimeOfDayHours = 2.0f;
		FFishRoll::Roll(T.Get(), LateContext, Late);
		TestEqual(TEXT("Day: only T_B_Mine"), Day.ModifierIds, TArray<FName>{ TEXT("T_B_Mine") });
		TestEqual(TEXT("02:00: T_B_Mine and the night modifier"), Late.ModifierIds, TArray<FName>{ TEXT("T_B_Mine"), TEXT("T_Night") });
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishWeightScalingTest, "Project.Fish.Roll.WeightScaling", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishWeightScalingTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const FFishTables Tables = T.Get();
	const FFishSpeciesRow& Bonefish = *Tables.FindSpecies(TEXT("Bonefish"));
	const FGameplayTag Strength = Tag(TEXT("Fish.Stat.Strength"));
	const FGameplayTag Speed = Tag(TEXT("Fish.Stat.Speed"));

	// SampleWeight: Min + (Max - Min) * U ^ SizeSkew
	TestEqual(TEXT("U 0 -> WeightMin"), FFishRoll::SampleWeight(Bonefish, 0.0f), Bonefish.WeightMin);
	TestEqual(TEXT("U 1 -> WeightMax"), FFishRoll::SampleWeight(Bonefish, 1.0f), Bonefish.WeightMax);
	TestTrue(TEXT("U 0.5 follows the skew"), FMath::IsNearlyEqual(FFishRoll::SampleWeight(Bonefish, 0.5f), 0.5f + 4.0f * FMath::Pow(0.5f, 1.8f), 1e-5f));

	// Forced fractions 0..1, Common, no modifiers: weight, difficulty stats and value never go down
	FFishInstance Previous;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		FFishRollContext Context = MakeContext(TEXT("Bonefish"), 42);
		Context.ForcedRarityId = TEXT("Common");
		Context.bForceModifiers = true;
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = Step / 20.0f;
		FFishInstance Fish;
		FFishRoll::Roll(Tables, Context, Fish);

		const float ExpectedWeight = 0.5f + 4.0f * (Step / 20.0f);
		TestTrue(FString::Printf(TEXT("Fraction %.2f: weight %.4f"), Step / 20.0f, Fish.WeightKg), FMath::IsNearlyEqual(Fish.WeightKg, ExpectedWeight, 1e-4f));
		const float ExpectedStrength = 10.0f * FMath::Pow(Fish.WeightKg / 1.5f, 0.5f);
		TestTrue(FString::Printf(TEXT("Strength = 10 x (w / 1.5) ^ 0.5 = %.4f (got %.4f)"), ExpectedStrength, Fish.GetStat(Strength)), FMath::IsNearlyEqual(Fish.GetStat(Strength), ExpectedStrength, 1e-3f));
		TestEqual(TEXT("Value = round-half-up(6 x w)"), Fish.Value, FMath::Max(1, RoundHalfUp(6.0 * Fish.WeightKg)));
		if (Step > 0)
		{
			TestTrue(TEXT("Heavier -> heavier"), Fish.WeightKg > Previous.WeightKg);
			TestTrue(TEXT("Heavier -> stronger"), Fish.GetStat(Strength) > Previous.GetStat(Strength));
			TestTrue(TEXT("Heavier -> faster or equal (Speed is a difficulty stat)"), Fish.GetStat(Speed) >= Previous.GetStat(Speed));
			TestTrue(TEXT("Heavier -> worth at least as much"), Fish.Value >= Previous.Value);
		}
		Previous = Fish;
	}

	// At the reference weight (fraction 0.25 -> 1.5 kg) the stats are exactly the species base stats
	FFishRollContext Neutral = MakeContext(TEXT("Bonefish"), 42);
	Neutral.ForcedRarityId = TEXT("Common");
	Neutral.bForceModifiers = true;
	Neutral.bForceWeightFraction = true;
	Neutral.ForcedWeightFraction = 0.25f;
	FFishInstance Fish;
	FFishRoll::Roll(Tables, Neutral, Fish);
	TestEqual(TEXT("Reference weight 1.5 kg"), Fish.WeightKg, 1.5f);
	TestEqual(TEXT("Strength is the base 10"), Fish.GetStat(Strength), 10.0f);
	TestEqual(TEXT("Speed is the base 28"), Fish.GetStat(Speed), 28.0f);
	TestEqual(TEXT("Value 6 x 1.5 = 9"), Fish.Value, 9);
	TestEqual(TEXT("Level = BaseLevel"), Fish.Level, 1);
	TestTrue(TEXT("DifficultyRating 1 for an average fish"), FMath::IsNearlyEqual(Fish.DifficultyRating, 1.0f, 1e-5f));

	// A weight modifier can pass WeightMax (record fish): Giant x1.8 on the heaviest Bonefish = 8.1 kg
	Neutral.ForcedModifierIds = { TEXT("Giant") };
	Neutral.ForcedWeightFraction = 1.0f;
	FFishRoll::Roll(Tables, Neutral, Fish);
	TestTrue(FString::Printf(TEXT("Giant pushes past WeightMax: %.3f kg"), Fish.WeightKg), FMath::IsNearlyEqual(Fish.WeightKg, 8.1f, 1e-4f));
	TestEqual(TEXT("Value uses the final weight: round-half-up(6 x 8.1 x 1.5)"), Fish.Value, RoundHalfUp(6.0 * Fish.WeightKg * 1.5));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishValueRarityTest, "Project.Fish.Roll.ValueRisesWithRarity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishValueRarityTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const FFishTables Tables = T.Get();
	TArray<TPair<FName, const FFishRarityRow*>> Tiers;
	for (const TPair<FName, uint8*>& Pair : T.Rarities->GetRowMap())
	{
		Tiers.Emplace(Pair.Key, reinterpret_cast<const FFishRarityRow*>(Pair.Value));
	}
	Tiers.Sort([](const TPair<FName, const FFishRarityRow*>& A, const TPair<FName, const FFishRarityRow*>& B) { return A.Value->Rank < B.Value->Rank; });

	for (const FName Species : { FName(TEXT("Bonefish")), FName(TEXT("CoralSnapper")) })
	{
		const FFishSpeciesRow& Row = *Tables.FindSpecies(Species);
		int32 PreviousValue = 0;
		for (const TPair<FName, const FFishRarityRow*>& Tier : Tiers)
		{
			FFishRollContext Context = MakeContext(Species, 7);
			Context.ForcedRarityId = Tier.Key;
			Context.bForceModifiers = true;
			Context.bForceWeightFraction = true;
			Context.ForcedWeightFraction = 0.5f;
			FFishInstance Fish;
			FFishRoll::Roll(Tables, Context, Fish);

			const int32 ExpectedValue = FMath::Max(1, RoundHalfUp(static_cast<double>(Row.BaseValuePerKg) * Fish.WeightKg * Tier.Value->ValueMultiplier));
			TestEqual(FString::Printf(TEXT("%s %s value = round-half-up(%g x %.3f x %g)"), *Species.ToString(), *Tier.Key.ToString(), Row.BaseValuePerKg, Fish.WeightKg, Tier.Value->ValueMultiplier), Fish.Value, ExpectedValue);
			TestTrue(FString::Printf(TEXT("%s %s value %d > previous tier %d"), *Species.ToString(), *Tier.Key.ToString(), Fish.Value, PreviousValue), Fish.Value > PreviousValue);
			PreviousValue = Fish.Value;

			const int32 Level = Row.BaseLevel + Tier.Value->LevelBonus;
			TestEqual(TEXT("Level = BaseLevel + LevelBonus"), Fish.Level, Level);
			TestEqual(TEXT("Xp = round-half-up((10 + 5 x (Level - 1)) x XpMultiplier)"), Fish.Xp, RoundHalfUp((10.0 + 5.0 * (Level - 1)) * Tier.Value->XpMultiplier));
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishNewRowsTest, "Project.Fish.Data.NewRowsNeedNoCode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishNewRowsTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const FGameplayTag Glow = Tag(TEXT("Fish.Stat.Glow"));

	// Start without the Glow stat row: no fish has the stat
	FFishStatRow GlowRow = *T.Get().FindStat(Glow);
	T.Stats->RemoveRow(TEXT("Glow"));
	FFishInstance Before;
	FFishRoll::Roll(T.Get(), MakeContext(TEXT("Bonefish"), 1), Before);
	TestFalse(TEXT("Without a DT_FishStat row there is no Glow stat"), Before.HasStat(Glow));

	// Add a stat row, a species, a rarity tier and a modifier, all in memory: no code change
	T.Stats->AddRow(TEXT("Glow"), GlowRow);

	FFishSpeciesRow NewFish = *T.Get().FindSpecies(TEXT("CoralSnapper"));
	NewFish.DisplayName = FText::FromString(TEXT("Lantern Grouper"));
	NewFish.JournalText = FText::FromString(TEXT("Glows faintly in the deep."));
	NewFish.HabitatTags = { Tag(TEXT("Habitat.DeepDrop")) };
	NewFish.TimeWindows.Reset();
	NewFish.BaseLevel = 6;
	NewFish.MaxModifiers = 5; // room for every modifier, so the cap never drops T_Glowing (last in id order)
	NewFish.AllowedRarities = { TEXT("Common"), TEXT("T_Mythic") };
	T.Species->AddRow(TEXT("T_LanternGrouper"), NewFish);

	FFishRarityRow Mythic;
	Mythic.DisplayName = FText::FromString(TEXT("Mythic"));
	Mythic.Rank = 5;
	Mythic.RollWeight = 5.0f;
	Mythic.ValueMultiplier = 20.0f;
	Mythic.XpMultiplier = 10.0f;
	Mythic.LevelBonus = 4;
	Mythic.StatMods = { MakeMod(TEXT("Fish.Stat.Strength"), EFishStatModOp::Multiply, 2.0f) };
	T.Rarities->AddRow(TEXT("T_Mythic"), Mythic);

	FFishModifierRow Glowing = MakeModifier(TEXT("Glowing"), 1.0f, NAME_None, { MakeMod(TEXT("Fish.Stat.Glow"), EFishStatModOp::Add, 7.0f) });
	Glowing.SpeciesIds = { TEXT("T_LanternGrouper") };
	Glowing.ValueMultiplier = 2.0f;
	T.Modifiers->AddRow(TEXT("T_Glowing"), Glowing);

	const FFishTables Tables = T.Get();
	const TArray<FString> Problems = FFishDataValidator::Validate(Tables);
	TestEqual(FString::Printf(TEXT("The new rows are valid data (%s)"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);

	// The bite picker finds the new species at its habitat
	FFishRollContext Deep = MakeContext(NAME_None, 3);
	Deep.HabitatTag = Tag(TEXT("Habitat.DeepDrop"));
	Deep.BaitTag = Tag(TEXT("Bait.Squid"));
	FName Picked;
	TestTrue(TEXT("Something bites at the deep drop"), FFishRoll::PickSpecies(Tables, Deep, Picked));
	TestEqual(TEXT("It's the new species"), Picked, FName(TEXT("T_LanternGrouper")));

	// Rolls: Mythic appears at its odds (5 / 75), Glowing always, Glow = 7; old species unaffected by the new modifier
	int32 Mythics = 0;
	int32 GlowOk = 0;
	constexpr int32 N = 4000;
	for (int32 Seed = 1; Seed <= N; ++Seed)
	{
		FFishInstance Fish;
		FFishRoll::Roll(Tables, MakeContext(TEXT("T_LanternGrouper"), Seed), Fish);
		Mythics += Fish.RarityId == TEXT("T_Mythic") ? 1 : 0;
		GlowOk += (Fish.HasModifier(TEXT("T_Glowing")) && Fish.GetStat(Glow) == 7.0f) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("Mythic share %.4f is ~5/75"), static_cast<double>(Mythics) / N), WithinSigma(static_cast<double>(Mythics) / N, 5.0 / 75.0, N));
	TestEqual(TEXT("Every Lantern Grouper is Glowing with Glow 7"), GlowOk, N);

	FFishInstance Old;
	FFishRoll::Roll(Tables, MakeContext(TEXT("Bonefish"), 1), Old);
	TestTrue(TEXT("Old species get the new stat at its default and not the new modifier"), Old.HasStat(Glow) && Old.GetStat(Glow) == 0.0f && !Old.HasModifier(TEXT("T_Glowing")));

	// Forced Mythic applies its multipliers: value ratio to Common = 20 at the same weight
	FFishRollContext Forced = MakeContext(TEXT("T_LanternGrouper"), 9);
	Forced.bForceModifiers = true;
	Forced.bForceWeightFraction = true;
	Forced.ForcedWeightFraction = 1.0f;
	Forced.ForcedRarityId = TEXT("Common");
	FFishInstance CommonFish, MythicFish;
	FFishRoll::Roll(Tables, Forced, CommonFish);
	Forced.ForcedRarityId = TEXT("T_Mythic");
	FFishRoll::Roll(Tables, Forced, MythicFish);
	TestEqual(TEXT("Mythic value = 20 x Common value (7 kg x 9/kg = 63 -> 1260)"), MythicFish.Value, CommonFish.Value * 20);
	TestEqual(TEXT("Mythic level = 6 + 4"), MythicFish.Level, 10);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishInstanceSaveTest, "Project.Fish.Instance.SaveRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishInstanceSaveTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	FFishRollContext Context = MakeContext(TEXT("CoralSnapper"), MIN_int32);
	Context.ForcedRarityId = TEXT("Rare");
	Context.bForceModifiers = true;
	Context.ForcedModifierIds = { TEXT("Giant"), TEXT("Feisty"), TEXT("Albino") };
	FFishInstance Original;
	TestTrue(TEXT("Rich instance rolls"), FFishRoll::Roll(T.Get(), Context, Original));

	for (const bool bSaveGameFlag : { false, true })
	{
		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails*/ false);
			Ar.ArIsSaveGame = bSaveGameFlag;
			FFishInstance::StaticStruct()->SerializeItem(Ar, static_cast<void*>(&Original), nullptr);
		}
		FFishInstance Loaded;
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Ar(Reader, /*bInLoadIfFindFails*/ true);
			Ar.ArIsSaveGame = bSaveGameFlag;
			FFishInstance::StaticStruct()->SerializeItem(Ar, static_cast<void*>(&Loaded), nullptr);
		}
		TestTrue(FString::Printf(TEXT("Round trip (ArIsSaveGame %d, %d bytes) gives the identical instance"), bSaveGameFlag ? 1 : 0, Bytes.Num()), Same(Original, Loaded));
		TestTrue(TEXT("HasModifier survives"), Loaded.HasModifier(TEXT("Giant")) && Loaded.HasModifier(TEXT("Albino")));
		TestEqual(TEXT("GetStat survives"), Loaded.GetStat(Tag(TEXT("Fish.Stat.Strength"))), Original.GetStat(Tag(TEXT("Fish.Stat.Strength"))));
	}

	// Every field is SaveGame and replication-friendly (no maps, sets, object pointers or text)
	TFunction<void(const UStruct*, const FString&)> Lint = [&](const UStruct* Struct, const FString& Where)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			const FString Name = Where + TEXT(".") + Property->GetName();
			TestTrue(Name + TEXT(" is SaveGame"), Property->HasAnyPropertyFlags(CPF_SaveGame));
			TestFalse(Name + TEXT(" is not Transient"), Property->HasAnyPropertyFlags(CPF_Transient | CPF_RepSkip));
			const FProperty* Value = Property;
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				Value = Array->Inner;
			}
			TestFalse(Name + TEXT(" is not a map, set, object pointer or text"),
				Value->IsA<FMapProperty>() || Value->IsA<FSetProperty>() || Value->IsA<FObjectPropertyBase>() || Value->IsA<FTextProperty>());
			if (const FStructProperty* Inner = CastField<FStructProperty>(Value))
			{
				if (Inner->Struct != FGameplayTag::StaticStruct())
				{
					Lint(Inner->Struct, Name);
				}
			}
		}
	};
	Lint(FFishInstance::StaticStruct(), TEXT("FFishInstance"));

	// A default instance is an empty slot
	const FFishInstance Empty;
	TestFalse(TEXT("Default instance is not a fish"), Empty.IsValid());
	TestEqual(TEXT("Default instance has value 0"), Empty.Value, 0);
	TestFalse(TEXT("Default instance has no modifiers"), Empty.HasModifier(TEXT("Giant")) || Empty.HasModifier(NAME_None));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishBitePickerTest, "Project.Fish.Bite.PickSpecies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishBitePickerTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const FFishTables Tables = T.Get();
	auto Pick = [&Tables](const TCHAR* Habitat, float Hours, const TCHAR* Bait, int32 Seed = 1, const TCHAR* Region = TEXT("Region.Tropical.PalmKey"))
	{
		FFishRollContext Context;
		Context.Seed = Seed;
		Context.RegionTag = Tag(Region);
		Context.HabitatTag = Tag(Habitat);
		Context.TimeOfDayHours = Hours;
		Context.BaitTag = Bait ? Tag(Bait) : FGameplayTag();
		FName Species;
		return FFishRoll::PickSpecies(Tables, Context, Species) ? Species : NAME_None;
	};

	const FName Bonefish(TEXT("Bonefish"));
	const FName Snapper(TEXT("CoralSnapper"));
	TestEqual(TEXT("Shore at noon with worms: Bonefish"), Pick(TEXT("Habitat.Shore"), 12.0f, TEXT("Bait.Worm")), Bonefish);
	TestEqual(TEXT("Lagoon counts as Bonefish habitat"), Pick(TEXT("Habitat.Lagoon"), 12.0f, TEXT("Bait.Shrimp")), Bonefish);
	TestEqual(TEXT("Bonefish window [5, 19): 5.0 in"), Pick(TEXT("Habitat.Shore"), 5.0f, TEXT("Bait.Worm")), Bonefish);
	TestEqual(TEXT("Bonefish window [5, 19): 19.0 out"), Pick(TEXT("Habitat.Shore"), 19.0f, TEXT("Bait.Worm")), FName());
	TestEqual(TEXT("Shore with squid: nothing"), Pick(TEXT("Habitat.Shore"), 12.0f, TEXT("Bait.Squid")), FName());
	TestEqual(TEXT("Shore with no bait: nothing"), Pick(TEXT("Habitat.Shore"), 12.0f, nullptr), FName());
	TestEqual(TEXT("Foggy region: nothing"), Pick(TEXT("Habitat.Shore"), 12.0f, TEXT("Bait.Worm"), 1, TEXT("Region.Foggy")), FName());
	TestEqual(TEXT("Snapper wraps midnight [15, 9): 23:00 in"), Pick(TEXT("Habitat.Reef"), 23.0f, TEXT("Bait.Shrimp")), Snapper);
	TestEqual(TEXT("Snapper: 0:00 in"), Pick(TEXT("Habitat.Reef"), 0.0f, TEXT("Bait.Shrimp")), Snapper);
	TestEqual(TEXT("Snapper: 24.0 is 0:00"), Pick(TEXT("Habitat.Reef"), 24.0f, TEXT("Bait.Shrimp")), Snapper);
	TestEqual(TEXT("Snapper: 8.99 in"), Pick(TEXT("Habitat.Reef"), 8.99f, TEXT("Bait.Squid")), Snapper);
	TestEqual(TEXT("Snapper: 9.0 out"), Pick(TEXT("Habitat.Reef"), 9.0f, TEXT("Bait.Squid")), FName());
	TestEqual(TEXT("Snapper: 12.0 out"), Pick(TEXT("Habitat.Reef"), 12.0f, TEXT("Bait.Squid")), FName());
	TestEqual(TEXT("Snapper: 15.0 in"), Pick(TEXT("Habitat.Reef"), 15.0f, TEXT("Bait.Squid")), Snapper);
	TestEqual(TEXT("Unknown habitat: nothing"), Pick(TEXT("Habitat.DeepDrop"), 12.0f, TEXT("Bait.Squid")), FName());

	// Bite weights among eligible species: 1 : 3
	FTestTables W;
	MakeSyntheticTables(W);
	FFishSpeciesRow Second = *W.Get().FindSpecies(TEXT("T_Fish"));
	Second.DisplayName = FText::FromString(TEXT("Second"));
	Second.BiteWeight = 3.0f;
	W.Species->AddRow(TEXT("T_Second"), Second);
	constexpr int32 N = 8000;
	int32 SecondCount = 0;
	int32 Distinct = 0;
	for (int32 Seed = 1; Seed <= N; ++Seed)
	{
		FFishRollContext Context = MakeContext(NAME_None, Seed);
		FName Species;
		FFishRoll::PickSpecies(W.Get(), Context, Species);
		SecondCount += Species == TEXT("T_Second") ? 1 : 0;
		FName Again;
		FFishRoll::PickSpecies(W.Get(), Context, Again);
		Distinct += Again == Species ? 0 : 1;
	}
	TestTrue(FString::Printf(TEXT("BiteWeight 3 vs 1 gives ~75%% (%.4f)"), static_cast<double>(SecondCount) / N), WithinSigma(static_cast<double>(SecondCount) / N, 0.75, N));
	TestEqual(TEXT("Same seed, same pick"), Distinct, 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishHelpersTest, "Project.Fish.Helpers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishHelpersTest::RunTest(const FString& Parameters)
{
	// PickWeightedIndex never returns a 0-weight entry
	TestEqual(TEXT("{0,5,0} U0"), PickIndex({ 0.0f, 5.0f, 0.0f }, 0.0f), 1);
	TestEqual(TEXT("{0,5,0} U1"), PickIndex({ 0.0f, 5.0f, 0.0f }, 1.0f), 1);
	TestEqual(TEXT("{1,1} U0.49"), PickIndex({ 1.0f, 1.0f }, 0.49f), 0);
	TestEqual(TEXT("{1,1} U0.5 (boundary goes up)"), PickIndex({ 1.0f, 1.0f }, 0.5f), 1);
	TestEqual(TEXT("{70,22,8,0,0} U just below 1"), PickIndex({ 70.0f, 22.0f, 8.0f, 0.0f, 0.0f }, 0.99999994f), 2);
	TestEqual(TEXT("{70,22,8,0,0} U 1"), PickIndex({ 70.0f, 22.0f, 8.0f, 0.0f, 0.0f }, 1.0f), 2);
	TestEqual(TEXT("{-5,0,NaN} none"), PickIndex({ -5.0f, 0.0f, std::numeric_limits<float>::quiet_NaN() }, 0.5f), INDEX_NONE);
	TestEqual(TEXT("{} none"), PickIndex({}, 0.5f), INDEX_NONE);
	TestEqual(TEXT("{50,-5,50} U0.6 skips the negative"), PickIndex({ 50.0f, -5.0f, 50.0f }, 0.6f), 2);
	TestEqual(TEXT("Tiny weights"), PickIndex({ 1e-6f, 1e-6f }, 0.75f), 1);
	TestEqual(TEXT("Huge weights"), PickIndex({ 1.5e9f, 1.5e9f }, 0.75f), 1);

	// Hours and windows
	TestEqual(TEXT("24 -> 0"), FFishRoll::NormalizeHours(24.0f), 0.0f);
	TestEqual(TEXT("-1 -> 23"), FFishRoll::NormalizeHours(-1.0f), 23.0f);
	TestEqual(TEXT("25.5 -> 1.5"), FFishRoll::NormalizeHours(25.5f), 1.5f);
	TestEqual(TEXT("NaN -> 0"), FFishRoll::NormalizeHours(std::numeric_limits<float>::quiet_NaN()), 0.0f);
	FFishTimeWindow Dawn;
	Dawn.StartHour = 5.0f;
	Dawn.EndHour = 7.0f;
	FFishTimeWindow Night;
	Night.StartHour = 20.0f;
	Night.EndHour = 4.0f;
	TestTrue(TEXT("Dawn 5.0 in"), Dawn.Contains(5.0f));
	TestTrue(TEXT("Dawn 6.99 in"), Dawn.Contains(6.99f));
	TestFalse(TEXT("Dawn 7.0 out"), Dawn.Contains(7.0f));
	TestFalse(TEXT("Dawn 4.99 out"), Dawn.Contains(4.99f));
	TestTrue(TEXT("Night 20.0 in"), Night.Contains(20.0f));
	TestTrue(TEXT("Night 23.99 in"), Night.Contains(23.99f));
	TestTrue(TEXT("Night 0.0 in"), Night.Contains(0.0f));
	TestTrue(TEXT("Night 3.99 in"), Night.Contains(3.99f));
	TestFalse(TEXT("Night 4.0 out"), Night.Contains(4.0f));
	TestFalse(TEXT("Night 12.0 out"), Night.Contains(12.0f));
	TestTrue(TEXT("No windows = any time"), FFishRoll::IsInTimeWindows(TArray<FFishTimeWindow>(), 3.0f));

	// Stage streams are independent and stable
	TestEqual(TEXT("StageSeed is HashCombine(Seed, Stage)"), FFishRoll::StageSeed(1, EFishRollStage::Weight), HashCombine(1u, 1u));
	TestNotEqual(TEXT("Weight and rarity streams differ"), FFishRoll::StageSeed(1, EFishRollStage::Weight), FFishRoll::StageSeed(1, EFishRollStage::Rarity));
	TestEqual(TEXT("Streams are deterministic"), FFishRoll::MakeStageStream(77, EFishRollStage::Bite).FRand(), FFishRoll::MakeStageStream(77, EFishRollStage::Bite).FRand());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishLevelScalingTest, "Project.Fish.Level.DifficultyMultiplier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishLevelScalingTest::RunTest(const FString& Parameters)
{
	const FFishLevelScaling Scaling; // defaults: over 0.35, under 0.1, clamp [0.5, 5]
	TestEqual(TEXT("Same level = 1"), FFishRoll::LevelDifficultyMultiplier(5, 5, Scaling), 1.0f);
	TestTrue(TEXT("1 above = 1.35"), FMath::IsNearlyEqual(FFishRoll::LevelDifficultyMultiplier(6, 5, Scaling), 1.35f, 1e-5f));
	TestTrue(TEXT("4 above = 2.4"), FMath::IsNearlyEqual(FFishRoll::LevelDifficultyMultiplier(9, 5, Scaling), 2.4f, 1e-5f));
	TestTrue(TEXT("3 below = 0.7"), FMath::IsNearlyEqual(FFishRoll::LevelDifficultyMultiplier(2, 5, Scaling), 0.7f, 1e-5f));
	TestEqual(TEXT("Far below clamps to 0.5"), FFishRoll::LevelDifficultyMultiplier(1, 50, Scaling), 0.5f);
	TestEqual(TEXT("Far above clamps to 5"), FFishRoll::LevelDifficultyMultiplier(100, 1, Scaling), 5.0f);
	TestEqual(TEXT("INT32_MAX vs INT32_MIN doesn't wrap"), FFishRoll::LevelDifficultyMultiplier(MAX_int32, MIN_int32, Scaling), 5.0f);
	TestEqual(TEXT("INT32_MIN vs INT32_MAX doesn't wrap"), FFishRoll::LevelDifficultyMultiplier(MIN_int32, MAX_int32, Scaling), 0.5f);
	float Previous = 0.0f;
	bool bMonotone = true;
	for (int32 FishLevel = 1; FishLevel <= 40; ++FishLevel)
	{
		const float Value = FFishRoll::LevelDifficultyMultiplier(FishLevel, 10, Scaling);
		bMonotone &= Value >= Previous && FMath::IsFinite(Value);
		Previous = Value;
	}
	TestTrue(TEXT("Non-decreasing in fish level"), bMonotone);
	TestEqual(TEXT("Settings default uses the same formula"), UFishLibrary::GetLevelDifficultyMultiplier(5, 5), 1.0f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishSettingsTest, "Project.Fish.Settings.TablesAndMissingAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishSettingsTest::RunTest(const FString& Parameters)
{
	const UFishSettings* Defaults = GetDefault<UFishSettings>();
	TestEqual(TEXT("Species table path"), Defaults->SpeciesTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_FishSpecies.DT_FishSpecies")));
	TestEqual(TEXT("Rarity table path"), Defaults->RarityTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_FishRarity.DT_FishRarity")));
	TestEqual(TEXT("Modifier table path"), Defaults->ModifierTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_FishModifier.DT_FishModifier")));
	TestEqual(TEXT("Stat table path"), Defaults->StatTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_FishStat.DT_FishStat")));
	for (const TCHAR* Source : { TEXT("DT_FishSpecies.json"), TEXT("DT_FishRarity.json"), TEXT("DT_FishModifier.json"), TEXT("DT_FishStat.json") })
	{
		TestTrue(FString::Printf(TEXT("Source %s exists"), Source), FPaths::FileExists(SourcePath(Source)));
	}

	// Missing assets fail gracefully with a readable reason
	UFishSettings* Missing = NewObject<UFishSettings>(GetTransientPackage());
	Missing->SpeciesTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_DoesNotExist.DT_DoesNotExist")));
	Missing->StatTable.Reset();
	FFishTables Tables;
	FString Error;
	TestFalse(TEXT("LoadTables fails when a table is missing"), Missing->LoadTables(Tables, Error));
	TestTrue(FString::Printf(TEXT("The error names the missing asset (%s)"), *Error), Error.Contains(TEXT("DT_DoesNotExist")));
	TestTrue(TEXT("The error names the unset table"), Error.Contains(TEXT("DT_FishStat is not set")));
	TestNull(TEXT("The missing table is null"), Tables.Species);

	AddExpectedMessage(TEXT("Fish roll"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FFishInstance Fish;
	TestFalse(TEXT("Rolling with missing tables fails"), FFishRoll::Roll(Tables, MakeContext(TEXT("Bonefish"), 1), Fish));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishWorkedExampleTest, "Project.Fish.Roll.WorkedExample", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFishWorkedExampleTest::RunTest(const FString& Parameters)
{
	FTestTables T;
	if (!LoadRealTables(*this, T))
	{
		return false;
	}
	const FFishTables Tables = T.Get();
	FFishRollContext Context = MakeContext(TEXT("CoralSnapper"), 20260922);
	Context.TimeOfDayHours = 17.5f;
	TArray<FString> Trace;
	FFishInstance Fish;
	TestTrue(TEXT("Roll succeeds"), FFishRoll::Roll(Tables, Context, Fish, &Trace));
	for (const FString& Line : Trace)
	{
		AddInfo(Line);
	}
	TestTrue(TEXT("Trace has every step"), Trace.Num() >= 8);

	// Recompute the parts that don't depend on which modifiers rolled
	const FFishSpeciesRow& Snapper = *Tables.FindSpecies(TEXT("CoralSnapper"));
	const float U = FFishRoll::MakeStageStream(Context.Seed, EFishRollStage::Weight).FRand();
	float ExpectedWeight = FFishRoll::SampleWeight(Snapper, U);
	double ValueMultiplier = Tables.FindRarity(Fish.RarityId)->ValueMultiplier;
	for (const FName& Id : Fish.ModifierIds)
	{
		const FFishModifierRow& Modifier = *Tables.FindModifier(Id);
		ValueMultiplier *= Modifier.ValueMultiplier;
		for (const FFishStatMod& Mod : Modifier.StatMods)
		{
			if (Mod.StatTag == Tag(TEXT("Fish.Stat.Weight")) && Mod.Op == EFishStatModOp::Multiply)
			{
				ExpectedWeight *= Mod.Value;
			}
		}
	}
	TestTrue(FString::Printf(TEXT("Weight %.4f = SampleWeight(U %.6f) x weight mods = %.4f"), Fish.WeightKg, U, ExpectedWeight), FMath::IsNearlyEqual(Fish.WeightKg, ExpectedWeight, 1e-4f));
	TestEqual(TEXT("Value = max(1, round-half-up(9 x weight x multipliers))"), Fish.Value, FMath::Max(1, RoundHalfUp(9.0 * Fish.WeightKg * ValueMultiplier)));
	return true;
}

} // namespace FishTest

#endif // WITH_DEV_AUTOMATION_TESTS

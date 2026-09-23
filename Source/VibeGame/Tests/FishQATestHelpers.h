// QA-owned helpers for the independent fish-system tests (Project.Fish.QA.*), written by the qa-engineer.
// Black-box: expectations come from docs/specs/fish-system-rules.md and the contract comments in Fish/FishRoll.h,
// Fish/FishTypes.h, Fish/FishInstance.h and Fish/FishDataValidator.h, never from the implementation.
// Tables come from the JSON sources in data/tables/ (never the binary /Game/Data assets) or are built in memory.
// Test design: Saved/AgentLogs/qa/eng2-T008-test-design.md (QA-xx numbers in the test comments).

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "HAL/CriticalSection.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Templates/TypeHash.h"
#include "Math/RandomStream.h"
#include "Fish/FishRoll.h"
#include "Fish/FishDataValidator.h"
#include "Fish/FishSettings.h"

#define FISH_QA_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace FishQA
{
	// ------------------------------------------------------------------------------------------------------------
	// Tags. Test-only tags (Test.Fish.*, Fish.Stat.QA_TestOnly) are native tags defined in FishQATestTags.cpp
	// under WITH_DEV_AUTOMATION_TESTS (approved in fish-system-rules.md, Q33).
	// ------------------------------------------------------------------------------------------------------------

	inline FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
	}

	inline bool IsRegisteredTag(FName Name)
	{
		return !Name.IsNone() && UGameplayTagsManager::Get().RequestGameplayTag(Name, /*ErrorIfNotFound*/ false).IsValid();
	}

	static const TCHAR* const QAStat = TEXT("Fish.Stat.QA_TestOnly");

	// ------------------------------------------------------------------------------------------------------------
	// Tables
	// ------------------------------------------------------------------------------------------------------------

	/** Owns four transient tables (kept alive against GC) and builds the FFishTables view the core takes */
	struct FTables
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

	inline UDataTable* NewTable(UScriptStruct* RowStruct)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = RowStruct;
		return Table;
	}

	inline FString SourcePath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables") / FileName);
	}

	inline bool ReadSource(const FString& FileName, FString& OutText)
	{
		return FFileHelper::LoadFileToString(OutText, *SourcePath(FileName));
	}

	/** Imports JSON text into a new transient table; returns the engine's import problems */
	inline TArray<FString> ImportJson(TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const FString& Json)
	{
		Out.Reset(NewTable(RowStruct));
		return Out->CreateTableFromJSONString(Json);
	}

	/** Imports and turns every import problem into a test error */
	inline bool ImportJsonChecked(FAutomationTestBase& Test, TStrongObjectPtr<UDataTable>& Out, UScriptStruct* RowStruct, const FString& Json, const TCHAR* What)
	{
		const TArray<FString> Problems = ImportJson(Out, RowStruct, Json);
		for (const FString& Problem : Problems)
		{
			Test.AddError(FString::Printf(TEXT("%s import problem: %s"), What, *Problem));
		}
		return Problems.Num() == 0;
	}

	struct FSourceTexts
	{
		FString Species, Rarities, Modifiers, Stats;
	};

	inline bool ReadRealSources(FAutomationTestBase& Test, FSourceTexts& Out)
	{
		bool bOk = ReadSource(TEXT("DT_FishSpecies.json"), Out.Species);
		bOk &= ReadSource(TEXT("DT_FishRarity.json"), Out.Rarities);
		bOk &= ReadSource(TEXT("DT_FishModifier.json"), Out.Modifiers);
		bOk &= ReadSource(TEXT("DT_FishStat.json"), Out.Stats);
		if (!bOk)
		{
			Test.AddError(TEXT("Can't read the fish table sources in data/tables/"));
		}
		return bOk;
	}

	inline bool ImportAll(FAutomationTestBase& Test, FTables& Out, const FSourceTexts& Texts)
	{
		bool bOk = ImportJsonChecked(Test, Out.Species, FFishSpeciesRow::StaticStruct(), Texts.Species, TEXT("DT_FishSpecies"));
		bOk &= ImportJsonChecked(Test, Out.Rarities, FFishRarityRow::StaticStruct(), Texts.Rarities, TEXT("DT_FishRarity"));
		bOk &= ImportJsonChecked(Test, Out.Modifiers, FFishModifierRow::StaticStruct(), Texts.Modifiers, TEXT("DT_FishModifier"));
		bOk &= ImportJsonChecked(Test, Out.Stats, FFishStatRow::StaticStruct(), Texts.Stats, TEXT("DT_FishStat"));
		Out.Tuning = GetDefault<UFishSettings>()->RollTuning;
		return bOk;
	}

	/** FX-REAL: the starter data from data/tables/*.json with the project's roll tuning */
	inline bool LoadReal(FAutomationTestBase& Test, FTables& Out)
	{
		FSourceTexts Texts;
		return ReadRealSources(Test, Texts) && ImportAll(Test, Out, Texts);
	}

	template <typename RowType>
	inline TArray<FName> SortedRowNames(const UDataTable* Table)
	{
		TArray<FName> Names;
		if (Table)
		{
			Table->GetRowMap().GetKeys(Names);
		}
		Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		return Names;
	}

	template <typename RowType>
	inline RowType* Row(const TStrongObjectPtr<UDataTable>& Table, FName Name)
	{
		return Table.IsValid() ? Table->FindRow<RowType>(Name, TEXT("FishQA"), /*bWarnIfRowMissing*/ false) : nullptr;
	}

	// ------------------------------------------------------------------------------------------------------------
	// In-memory fixture builders (FX-ID and friends). Numbers are exactly representable in float.
	// ------------------------------------------------------------------------------------------------------------

	inline FFishStatMod Mod(const TCHAR* Stat, EFishStatModOp Op, float Value)
	{
		FFishStatMod StatMod;
		StatMod.StatTag = Tag(Stat);
		StatMod.Op = Op;
		StatMod.Value = Value;
		return StatMod;
	}

	inline FFishStatMod AddMod(const TCHAR* Stat, float Value) { return Mod(Stat, EFishStatModOp::Add, Value); }
	inline FFishStatMod MulMod(const TCHAR* Stat, float Value) { return Mod(Stat, EFishStatModOp::Multiply, Value); }

	inline FFishStatRow MakeStat(const TCHAR* TagName, float Min, float Max, float Default, bool bDifficulty)
	{
		FFishStatRow Stat;
		Stat.Tag = Tag(TagName);
		Stat.DisplayName = FText::FromString(TagName);
		Stat.Min = Min;
		Stat.Max = Max;
		Stat.Default = Default;
		Stat.bIsDifficultyStat = bDifficulty;
		return Stat;
	}

	/** Species with the FX-ID defaults: Strength 10, Stamina 20, Speed 4, 10 coins/kg, level 5, reference weight = WeightMin */
	inline FFishSpeciesRow MakeSpecies(float WeightMin = 2.0f, float WeightMax = 2.0f)
	{
		FFishSpeciesRow Species;
		Species.DisplayName = FText::FromString(TEXT("QA Fish"));
		Species.JournalText = FText::FromString(TEXT("QA fixture"));
		Species.BaseLevel = 5;
		Species.WeightMin = WeightMin;
		Species.WeightMax = WeightMax;
		Species.SizeSkew = 1.0f;
		Species.ReferenceWeight = WeightMin;
		Species.WeightStatExponent = 0.5f;
		Species.BaseStats.Add(FFishStatValue(Tag(TEXT("Fish.Stat.Strength")), 10.0f));
		Species.BaseStats.Add(FFishStatValue(Tag(TEXT("Fish.Stat.Stamina")), 20.0f));
		Species.BaseStats.Add(FFishStatValue(Tag(TEXT("Fish.Stat.Speed")), 4.0f));
		Species.BaseValuePerKg = 10.0f;
		Species.BiteWeight = 1.0f;
		Species.MaxModifiers = 16;
		Species.FightPatternId = TEXT("QA");
		return Species;
	}

	inline FFishRarityRow MakeRarity(int32 Rank, float RollWeight, float ValueMultiplier = 1.0f, float XpMultiplier = 1.0f, int32 LevelBonus = 0)
	{
		FFishRarityRow Rarity;
		Rarity.DisplayName = FText::FromString(FString::Printf(TEXT("QA Tier %d"), Rank));
		Rarity.Rank = Rank;
		Rarity.RollWeight = RollWeight;
		Rarity.ValueMultiplier = ValueMultiplier;
		Rarity.XpMultiplier = XpMultiplier;
		Rarity.LevelBonus = LevelBonus;
		return Rarity;
	}

	inline FFishModifierRow MakeModifier(float Chance, FName Group = NAME_None, TArray<FFishStatMod> Mods = TArray<FFishStatMod>(), float ValueMultiplier = 1.0f)
	{
		FFishModifierRow Modifier;
		Modifier.DisplayName = FText::FromString(TEXT("QA Modifier"));
		Modifier.RollChance = Chance;
		Modifier.ExclusivityGroup = Group;
		Modifier.StatMods = MoveTemp(Mods);
		Modifier.ValueMultiplier = ValueMultiplier;
		return Modifier;
	}

	/** Stats: Weight [0.01, WeightMax] (default 1), Strength/Stamina/Speed [0, 1000] difficulty, QA_TestOnly [0, 1000] */
	inline void MakeStandardStats(FTables& T, float WeightMax = 10000.0f)
	{
		T.Stats.Reset(NewTable(FFishStatRow::StaticStruct()));
		T.Stats->AddRow(TEXT("Weight"), MakeStat(TEXT("Fish.Stat.Weight"), 0.01f, WeightMax, 1.0f, false));
		T.Stats->AddRow(TEXT("Strength"), MakeStat(TEXT("Fish.Stat.Strength"), 0.0f, 1000.0f, 0.0f, true));
		T.Stats->AddRow(TEXT("Stamina"), MakeStat(TEXT("Fish.Stat.Stamina"), 0.0f, 1000.0f, 0.0f, true));
		T.Stats->AddRow(TEXT("Speed"), MakeStat(TEXT("Fish.Stat.Speed"), 0.0f, 1000.0f, 0.0f, true));
		T.Stats->AddRow(TEXT("QA_TestOnly"), MakeStat(QAStat, 0.0f, 1000.0f, 0.0f, false));
	}

	/**
	 *  FX-ID: standard stats; species QA_Fixed (fixed 2 kg = reference weight, so the weight factor is exactly 1);
	 *  one tier QA_Neutral (weight 1, all multipliers 1); an empty modifier table. Default tuning.
	 */
	inline void MakeIdentity(FTables& T)
	{
		MakeStandardStats(T);
		T.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct()));
		T.Species->AddRow(TEXT("QA_Fixed"), MakeSpecies(2.0f, 2.0f));
		T.Rarities.Reset(NewTable(FFishRarityRow::StaticStruct()));
		T.Rarities->AddRow(TEXT("QA_Neutral"), MakeRarity(0, 1.0f));
		T.Modifiers.Reset(NewTable(FFishModifierRow::StaticStruct()));
		T.Tuning = FFishRollTuning();
	}

	inline void ResetRarities(FTables& T)
	{
		T.Rarities.Reset(NewTable(FFishRarityRow::StaticStruct()));
	}

	inline void ResetModifiers(FTables& T)
	{
		T.Modifiers.Reset(NewTable(FFishModifierRow::StaticStruct()));
	}

	inline FFishRollContext Ctx(FName SpeciesId, int32 Seed)
	{
		FFishRollContext Context;
		Context.SpeciesId = SpeciesId;
		Context.Seed = Seed;
		Context.RegionTag = Tag(TEXT("Region.Tropical.PalmKey"));
		Context.TimeOfDayHours = 10.0f;
		return Context;
	}

	// ------------------------------------------------------------------------------------------------------------
	// Instance checks
	// ------------------------------------------------------------------------------------------------------------

	inline bool Same(const FFishInstance& A, const FFishInstance& B)
	{
		return FFishInstance::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
	}

	inline bool AllFinite(const FFishInstance& Fish)
	{
		bool bFinite = FMath::IsFinite(Fish.WeightKg) && FMath::IsFinite(Fish.DifficultyRating);
		for (const FFishStatValue& Stat : Fish.Stats)
		{
			bFinite &= FMath::IsFinite(Stat.Value);
		}
		return bFinite;
	}

	/** Stat value by tag name (-12345 if missing, so a missing stat never passes a numeric check by accident) */
	inline float StatOf(const FFishInstance& Fish, const TCHAR* TagName)
	{
		return Fish.GetStat(Tag(TagName), -12345.0f);
	}

	inline FString Describe(const FFishInstance& Fish)
	{
		return Fish.ToString();
	}

	inline FString ModsKey(const FFishInstance& Fish)
	{
		FString Key;
		for (const FName& Id : Fish.ModifierIds)
		{
			Key += Id.ToString() + TEXT("|");
		}
		return Key;
	}

	inline bool NearlyRel(double A, double B, double RelTol = 1e-4, double AbsTol = 1e-4)
	{
		return FMath::Abs(A - B) <= FMath::Max(AbsTol, RelTol * FMath::Max(FMath::Abs(A), FMath::Abs(B)));
	}

	// ------------------------------------------------------------------------------------------------------------
	// Statistics (see the test design, section 3): fixed seeds, 4.5 sigma per category, chi-square at alpha 1e-4
	// ------------------------------------------------------------------------------------------------------------

	inline double Sigma(double P, int32 N)
	{
		return FMath::Sqrt(FMath::Max(P * (1.0 - P), 0.0) / FMath::Max(N, 1));
	}

	inline bool WithinSigma(double Observed, double Expected, int32 N, double K = 4.5)
	{
		return FMath::Abs(Observed - Expected) <= K * Sigma(Expected, N) + 1e-12;
	}

	/** Upper 1e-4 critical value of chi-square (df 1..8 from tables, Wilson-Hilferty above) */
	inline double ChiSquareCritical1e4(int32 Df)
	{
		static const double Table[] = { 0.0, 15.137, 18.421, 21.108, 23.513, 25.745, 27.856, 29.878, 31.828 };
		if (Df >= 1 && Df <= 8)
		{
			return Table[Df];
		}
		const double K = FMath::Max(Df, 1);
		const double Z = 3.719;
		const double Term = 1.0 - 2.0 / (9.0 * K) + Z * FMath::Sqrt(2.0 / (9.0 * K));
		return K * Term * Term * Term;
	}

	/** Pearson chi-square over categories with P > 0 (categories with P == 0 must be checked as hard zeros) */
	inline double ChiSquare(const TArray<int32>& Counts, const TArray<double>& Probs, int32 N, int32& OutDf)
	{
		double Chi = 0.0;
		int32 Used = 0;
		for (int32 i = 0; i < Counts.Num() && i < Probs.Num(); ++i)
		{
			if (Probs[i] > 0.0)
			{
				const double Expected = Probs[i] * N;
				Chi += FMath::Square(Counts[i] - Expected) / Expected;
				++Used;
			}
		}
		OutDf = FMath::Max(Used - 1, 1);
		return Chi;
	}

	/** Checks observed category counts against expected probabilities: hard zeros, 4.5 sigma each, chi-square */
	inline bool CheckOdds(FAutomationTestBase& Test, const FString& What, const TArray<FString>& Names, const TArray<int32>& Counts, const TArray<double>& Probs, int32 N)
	{
		bool bOk = true;
		for (int32 i = 0; i < Counts.Num(); ++i)
		{
			const double Observed = double(Counts[i]) / N;
			if (Probs[i] <= 0.0)
			{
				if (Counts[i] != 0)
				{
					Test.AddError(FString::Printf(TEXT("%s: %s has probability 0 but rolled %d times in %d"), *What, *Names[i], Counts[i], N));
					bOk = false;
				}
			}
			else if (!WithinSigma(Observed, Probs[i], N))
			{
				Test.AddError(FString::Printf(TEXT("%s: %s observed %.5f, expected %.5f (+/- %.5f at 4.5 sigma, N=%d)"),
					*What, *Names[i], Observed, Probs[i], 4.5 * Sigma(Probs[i], N), N));
				bOk = false;
			}
		}
		int32 Df = 1;
		const double Chi = ChiSquare(Counts, Probs, N, Df);
		if (Chi > ChiSquareCritical1e4(Df))
		{
			Test.AddError(FString::Printf(TEXT("%s: chi-square %.2f > critical %.2f (df %d, alpha 1e-4)"), *What, Chi, ChiSquareCritical1e4(Df), Df));
			bOk = false;
		}
		return bOk;
	}

	// ------------------------------------------------------------------------------------------------------------
	// Log capture: counts LogLureFish warnings/errors during a scope (warnings don't fail tests in this project)
	// ------------------------------------------------------------------------------------------------------------

	class FLogCapture : public FOutputDevice
	{
	public:
		FLogCapture()
		{
			GLog->AddOutputDevice(this);
		}

		virtual ~FLogCapture() override
		{
			GLog->RemoveOutputDevice(this);
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Level = ELogVerbosity::Type(Verbosity & ELogVerbosity::VerbosityMask);
			if (Category != FName(TEXT("LogLureFish")) || Level == ELogVerbosity::NoLogging || Level > ELogVerbosity::Warning)
			{
				return;
			}
			FScopeLock Lock(&Mutex);
			(Level == ELogVerbosity::Warning ? Warnings : Errors).Add(V);
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		int32 NumWarnings() const { GLog->Flush(); FScopeLock Lock(&Mutex); return Warnings.Num(); }
		int32 NumErrors() const { GLog->Flush(); FScopeLock Lock(&Mutex); return Errors.Num(); }

		bool AnyWarningContains(const FString& Text) const
		{
			GLog->Flush();
			FScopeLock Lock(&Mutex);
			return Warnings.ContainsByPredicate([&Text](const FString& Line) { return Line.Contains(Text); });
		}

		FString First() const
		{
			GLog->Flush();
			FScopeLock Lock(&Mutex);
			return Errors.Num() > 0 ? Errors[0] : (Warnings.Num() > 0 ? Warnings[0] : FString());
		}

		void Reset()
		{
			GLog->Flush();
			FScopeLock Lock(&Mutex);
			Warnings.Reset();
			Errors.Reset();
		}

	private:
		mutable FCriticalSection Mutex;
		TArray<FString> Warnings;
		TArray<FString> Errors;
	};

	// ------------------------------------------------------------------------------------------------------------
	// Independent oracle of the documented pipeline (fish-system-rules.md "Pipeline" + FishRoll.h header comment).
	// Written from the spec text only. Used to check Roll() field by field on real and fixture data.
	// ------------------------------------------------------------------------------------------------------------

	inline float OracleHours(float Hours)
	{
		if (!FMath::IsFinite(Hours))
		{
			return 0.0f;
		}
		float H = FMath::Fmod(Hours, 24.0f);
		if (H < 0.0f)
		{
			H += 24.0f;
		}
		return H >= 24.0f ? 0.0f : H;
	}

	inline bool OracleInWindows(const TArray<FFishTimeWindow>& Windows, float Hours)
	{
		if (Windows.Num() == 0)
		{
			return true;
		}
		const float H = OracleHours(Hours);
		for (const FFishTimeWindow& W : Windows)
		{
			const bool bIn = W.StartHour <= W.EndHour ? (H >= W.StartHour && H < W.EndHour) : (H >= W.StartHour || H < W.EndHour);
			if (bIn)
			{
				return true;
			}
		}
		return false;
	}

	/** Empty list = any; otherwise the context tag must match (hierarchically) any listed tag */
	inline bool OracleTagListMatches(const TArray<FGameplayTag>& List, const FGameplayTag& ContextTag)
	{
		if (List.Num() == 0)
		{
			return true;
		}
		if (!ContextTag.IsValid())
		{
			return false;
		}
		for (const FGameplayTag& Listed : List)
		{
			if (ContextTag.MatchesTag(Listed))
			{
				return true;
			}
		}
		return false;
	}

	inline bool OracleModifierEligible(FName ModifierId, const FFishModifierRow& Modifier, FName SpeciesId, const FFishSpeciesRow& Species, const FFishRollContext& Context)
	{
		if (Species.AllowedModifiers.Num() > 0 && !Species.AllowedModifiers.Contains(ModifierId))
		{
			return false;
		}
		if (Modifier.SpeciesIds.Num() > 0 && !Modifier.SpeciesIds.Contains(SpeciesId))
		{
			return false;
		}
		if (Modifier.SpeciesTags.Num() > 0)
		{
			bool bAny = false;
			for (const FGameplayTag& Condition : Modifier.SpeciesTags)
			{
				for (const FGameplayTag& Own : Species.SpeciesTags)
				{
					bAny |= Own.MatchesTag(Condition);
				}
			}
			if (!bAny)
			{
				return false;
			}
		}
		return OracleTagListMatches(Modifier.RegionTags, Context.RegionTag)
			&& OracleInWindows(Modifier.TimeWindows, Context.TimeOfDayHours)
			&& OracleTagListMatches(Modifier.WeatherTags, Context.WeatherTag);
	}

	inline bool OracleSpeciesEligible(const FFishSpeciesRow& Species, const FFishRollContext& Context)
	{
		return Species.BiteWeight > 0.0f
			&& OracleTagListMatches(Species.RegionTags, Context.RegionTag)
			&& OracleTagListMatches(Species.HabitatTags, Context.HabitatTag)
			&& OracleInWindows(Species.TimeWindows, Context.TimeOfDayHours)
			&& OracleTagListMatches(Species.WeatherTags, Context.WeatherTag)
			&& OracleTagListMatches(Species.AcceptedBait, Context.BaitTag);
	}

	/** First index i with U * Total < cumulative; non-positive and non-finite weights count 0; INDEX_NONE if none */
	inline int32 OraclePick(const TArray<float>& Weights, float U)
	{
		float Total = 0.0f;
		for (float W : Weights)
		{
			Total += (FMath::IsFinite(W) && W > 0.0f) ? W : 0.0f;
		}
		if (Total <= 0.0f)
		{
			return INDEX_NONE;
		}
		const float Target = FMath::Clamp(U, 0.0f, 1.0f) * Total;
		float Cumulative = 0.0f;
		int32 LastPositive = INDEX_NONE;
		for (int32 i = 0; i < Weights.Num(); ++i)
		{
			const float W = (FMath::IsFinite(Weights[i]) && Weights[i] > 0.0f) ? Weights[i] : 0.0f;
			if (W <= 0.0f)
			{
				continue;
			}
			LastPositive = i;
			Cumulative += W;
			if (Target < Cumulative)
			{
				return i;
			}
		}
		return LastPositive;
	}

	inline bool OracleModValid(const FFishStatMod& StatMod, const TMap<FName, const FFishStatRow*>& StatRows)
	{
		return StatRows.Contains(StatMod.StatTag.GetTagName()) && FMath::IsFinite(StatMod.Value)
			&& (StatMod.Op == EFishStatModOp::Add || StatMod.Value > 0.0f);
	}

	/** All Adds of all lists (in order), then all Multiplies (product) */
	inline void OracleApply(TMap<FName, float>& Stats, const TMap<FName, const FFishStatRow*>& StatRows, const TArray<const TArray<FFishStatMod>*>& Lists)
	{
		for (const TArray<FFishStatMod>* List : Lists)
		{
			for (const FFishStatMod& StatMod : *List)
			{
				if (StatMod.Op == EFishStatModOp::Add && OracleModValid(StatMod, StatRows))
				{
					Stats.FindOrAdd(StatMod.StatTag.GetTagName()) += StatMod.Value;
				}
			}
		}
		for (const TArray<FFishStatMod>* List : Lists)
		{
			for (const FFishStatMod& StatMod : *List)
			{
				if (StatMod.Op == EFishStatModOp::Multiply && OracleModValid(StatMod, StatRows))
				{
					Stats.FindOrAdd(StatMod.StatTag.GetTagName()) *= StatMod.Value;
				}
			}
		}
	}

	struct FOracleOut
	{
		FFishInstance Fish;
		/** Unrounded value (to tolerate float-vs-double rounding exactly at .5) */
		double RawValue = 0.0;
	};

	inline bool OracleRoll(const FFishTables& T, const FFishRollContext& C, FOracleOut& Out)
	{
		Out = FOracleOut();
		const FFishSpeciesRow* Species = T.Species ? T.Species->FindRow<FFishSpeciesRow>(C.SpeciesId, TEXT("Oracle"), false) : nullptr;
		if (!Species || !T.Rarities || !T.Stats)
		{
			return false;
		}

		// 1. Species base: every stat row starts at Default, BaseStats override
		TMap<FName, const FFishStatRow*> StatRows;
		TMap<FName, float> Stats;
		T.Stats->ForeachRow<FFishStatRow>(TEXT("Oracle"), [&](const FName&, const FFishStatRow& StatRow)
		{
			StatRows.Add(StatRow.Tag.GetTagName(), &StatRow);
			Stats.Add(StatRow.Tag.GetTagName(), StatRow.Default);
		});
		for (const FFishStatValue& Base : Species->BaseStats)
		{
			if (Stats.Contains(Base.Tag.GetTagName()) && FMath::IsFinite(Base.Value))
			{
				Stats[Base.Tag.GetTagName()] = Base.Value;
			}
		}
		const TMap<FName, float> BaseValues = Stats;

		// 2. Weight
		float Weight = 0.0f;
		if (C.bForceWeightFraction)
		{
			Weight = Species->WeightMin + (Species->WeightMax - Species->WeightMin) * C.ForcedWeightFraction;
		}
		else
		{
			const FRandomStream WeightStream{ int32(HashCombine(uint32(C.Seed), uint32(EFishRollStage::Weight))) };
			const float U = WeightStream.FRand();
			Weight = Species->WeightMin + (Species->WeightMax - Species->WeightMin) * FMath::Pow(U, Species->SizeSkew);
		}
		const float Factor = FMath::Pow(Weight / Species->ReferenceWeight, Species->WeightStatExponent);
		for (TPair<FName, const FFishStatRow*>& Pair : StatRows)
		{
			if (Pair.Value->bIsDifficultyStat)
			{
				Stats[Pair.Key] *= Factor;
			}
		}
		const FName WeightName(TEXT("Fish.Stat.Weight"));
		Stats.FindOrAdd(WeightName) = Weight;

		// 3. Rarity
		FName RarityId;
		const FFishRarityRow* Rarity = nullptr;
		if (!C.ForcedRarityId.IsNone())
		{
			RarityId = C.ForcedRarityId;
			Rarity = T.Rarities->FindRow<FFishRarityRow>(RarityId, TEXT("Oracle"), false);
		}
		else
		{
			TArray<TPair<FName, const FFishRarityRow*>> Candidates;
			T.Rarities->ForeachRow<FFishRarityRow>(TEXT("Oracle"), [&](const FName& Id, const FFishRarityRow& RarityRow)
			{
				if (Species->AllowedRarities.Num() == 0 || Species->AllowedRarities.Contains(Id))
				{
					Candidates.Add(TPair<FName, const FFishRarityRow*>(Id, &RarityRow));
				}
			});
			Candidates.Sort([](const TPair<FName, const FFishRarityRow*>& A, const TPair<FName, const FFishRarityRow*>& B)
			{
				return A.Value->Rank != B.Value->Rank ? A.Value->Rank < B.Value->Rank : A.Key.LexicalLess(B.Key);
			});
			const float Luck = FMath::IsNaN(C.Luck) ? 0.0f : FMath::Clamp(C.Luck, 0.0f, T.Tuning.MaxLuck);
			TArray<float> Weights;
			for (const TPair<FName, const FFishRarityRow*>& Candidate : Candidates)
			{
				const float W = Candidate.Value->RollWeight;
				Weights.Add((FMath::IsFinite(W) && W > 0.0f) ? W * (1.0f + Luck * float(Candidate.Value->Rank) * T.Tuning.LuckRankFactor) : 0.0f);
			}
			const FRandomStream RarityStream{ int32(HashCombine(uint32(C.Seed), uint32(EFishRollStage::Rarity))) };
			const int32 Index = OraclePick(Weights, RarityStream.FRand());
			if (Index == INDEX_NONE)
			{
				return false;
			}
			RarityId = Candidates[Index].Key;
			Rarity = Candidates[Index].Value;
		}
		if (!Rarity)
		{
			return false;
		}
		{
			TArray<const TArray<FFishStatMod>*> Lists;
			Lists.Add(&Rarity->StatMods);
			OracleApply(Stats, StatRows, Lists);
		}
		const int32 Level = Species->BaseLevel + Rarity->LevelBonus;

		// 4. Modifiers
		TArray<FName> Kept;
		if (C.bForceModifiers)
		{
			for (const FName& Id : C.ForcedModifierIds)
			{
				if (!Id.IsNone() && T.Modifiers && T.Modifiers->FindRow<FFishModifierRow>(Id, TEXT("Oracle"), false))
				{
					Kept.AddUnique(Id);
				}
			}
			Kept.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		}
		else if (T.Modifiers)
		{
			const TArray<FName> Ids = SortedRowNames<FFishModifierRow>(T.Modifiers);
			const FRandomStream ModStream{ int32(HashCombine(uint32(C.Seed), uint32(EFishRollStage::Modifiers))) };
			TArray<FName> Hits;
			for (const FName& Id : Ids)
			{
				const FFishModifierRow* Modifier = T.Modifiers->FindRow<FFishModifierRow>(Id, TEXT("Oracle"), false);
				const float U = ModStream.FRand();
				if (OracleModifierEligible(Id, *Modifier, C.SpeciesId, *Species, C) && U < Modifier->RollChance)
				{
					Hits.Add(Id);
				}
			}
			TArray<FName> Groups;
			for (const FName& Id : Hits)
			{
				const FName Group = T.Modifiers->FindRow<FFishModifierRow>(Id, TEXT("Oracle"), false)->ExclusivityGroup;
				if (!Group.IsNone())
				{
					Groups.AddUnique(Group);
				}
			}
			Groups.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
			for (const FName& Group : Groups)
			{
				TArray<FName> Members;
				TArray<float> Chances;
				for (const FName& Id : Hits)
				{
					const FFishModifierRow* Modifier = T.Modifiers->FindRow<FFishModifierRow>(Id, TEXT("Oracle"), false);
					if (Modifier->ExclusivityGroup == Group)
					{
						Members.Add(Id);
						Chances.Add(Modifier->RollChance);
					}
				}
				if (Members.Num() >= 2)
				{
					const int32 Keep = OraclePick(Chances, ModStream.FRand());
					for (int32 i = 0; i < Members.Num(); ++i)
					{
						if (i != Keep)
						{
							Hits.Remove(Members[i]);
						}
					}
				}
			}
			for (int32 i = 0; i < Hits.Num() && Kept.Num() < FMath::Max(Species->MaxModifiers, 0); ++i)
			{
				Kept.Add(Hits[i]);
			}
		}
		double ModValueProduct = 1.0;
		{
			TArray<const TArray<FFishStatMod>*> Lists;
			for (const FName& Id : Kept)
			{
				const FFishModifierRow* Modifier = T.Modifiers->FindRow<FFishModifierRow>(Id, TEXT("Oracle"), false);
				Lists.Add(&Modifier->StatMods);
				ModValueProduct *= Modifier->ValueMultiplier;
			}
			OracleApply(Stats, StatRows, Lists);
		}

		// 5. Clamp
		for (TPair<FName, float>& Pair : Stats)
		{
			if (const FFishStatRow* const* StatRow = StatRows.Find(Pair.Key))
			{
				Pair.Value = FMath::Clamp(Pair.Value, (*StatRow)->Min, (*StatRow)->Max);
			}
		}
		const float WeightKg = Stats.FindRef(WeightName);

		// 6. Value, XP, difficulty rating, sorted stats
		Out.RawValue = double(Species->BaseValuePerKg) * WeightKg * Rarity->ValueMultiplier * ModValueProduct;
		const int32 Value = FMath::Max(1, int32(FMath::FloorToDouble(Out.RawValue + 0.5)));
		const double RawXp = (double(T.Tuning.XpBase) + double(T.Tuning.XpPerLevel) * (Level - 1)) * Rarity->XpMultiplier;
		const int32 Xp = FMath::Max(0, int32(FMath::FloorToDouble(RawXp + 0.5)));
		double RatingSum = 0.0;
		int32 RatingCount = 0;
		for (const TPair<FName, const FFishStatRow*>& Pair : StatRows)
		{
			const float Base = BaseValues.FindRef(Pair.Key);
			if (Pair.Value->bIsDifficultyStat && Base > 0.0f)
			{
				RatingSum += Stats.FindRef(Pair.Key) / Base;
				++RatingCount;
			}
		}

		FFishInstance& Fish = Out.Fish;
		Fish.SpeciesId = C.SpeciesId;
		Fish.RarityId = RarityId;
		Fish.ModifierIds = Kept;
		Fish.WeightKg = WeightKg;
		Fish.Level = Level;
		Fish.Value = Value;
		Fish.Xp = Xp;
		Fish.DifficultyRating = RatingCount > 0 ? float(RatingSum / RatingCount) : 1.0f;
		Fish.Seed = C.Seed;
		for (const TPair<FName, float>& Pair : Stats)
		{
			Fish.Stats.Add(FFishStatValue(FGameplayTag::RequestGameplayTag(Pair.Key, false), Pair.Value));
		}
		Fish.Stats.Sort([](const FFishStatValue& A, const FFishStatValue& B) { return A.Tag.GetTagName().LexicalLess(B.Tag.GetTagName()); });
		return true;
	}

	/** Field-by-field comparison of Roll() against the oracle; returns an empty string when they agree */
	inline FString DiffAgainstOracle(const FFishInstance& Actual, const FOracleOut& Expected)
	{
		const FFishInstance& E = Expected.Fish;
		TArray<FString> Diffs;
		if (Actual.SpeciesId != E.SpeciesId) Diffs.Add(FString::Printf(TEXT("SpeciesId %s vs %s"), *Actual.SpeciesId.ToString(), *E.SpeciesId.ToString()));
		if (Actual.RarityId != E.RarityId) Diffs.Add(FString::Printf(TEXT("RarityId %s vs %s"), *Actual.RarityId.ToString(), *E.RarityId.ToString()));
		if (Actual.ModifierIds != E.ModifierIds) Diffs.Add(FString::Printf(TEXT("ModifierIds [%s] vs [%s]"), *ModsKey(Actual), *ModsKey(E)));
		if (Actual.Level != E.Level) Diffs.Add(FString::Printf(TEXT("Level %d vs %d"), Actual.Level, E.Level));
		if (Actual.Xp != E.Xp) Diffs.Add(FString::Printf(TEXT("Xp %d vs %d"), Actual.Xp, E.Xp));
		if (Actual.Seed != E.Seed) Diffs.Add(FString::Printf(TEXT("Seed %d vs %d"), Actual.Seed, E.Seed));
		if (!NearlyRel(Actual.WeightKg, E.WeightKg)) Diffs.Add(FString::Printf(TEXT("WeightKg %.6f vs %.6f"), Actual.WeightKg, E.WeightKg));
		if (!NearlyRel(Actual.DifficultyRating, E.DifficultyRating)) Diffs.Add(FString::Printf(TEXT("DifficultyRating %.6f vs %.6f"), Actual.DifficultyRating, E.DifficultyRating));
		if (Actual.Value != E.Value)
		{
			// Tolerate +/-1 only when the unrounded value sits within 1e-3 of a .5 boundary (float vs double rounding)
			const double Frac = Expected.RawValue - FMath::FloorToDouble(Expected.RawValue);
			if (!(FMath::Abs(Actual.Value - E.Value) == 1 && FMath::Abs(Frac - 0.5) < 1e-3))
			{
				Diffs.Add(FString::Printf(TEXT("Value %d vs %d (raw %.4f)"), Actual.Value, E.Value, Expected.RawValue));
			}
		}
		if (Actual.Stats.Num() != E.Stats.Num())
		{
			Diffs.Add(FString::Printf(TEXT("Stats count %d vs %d"), Actual.Stats.Num(), E.Stats.Num()));
		}
		else
		{
			for (int32 i = 0; i < E.Stats.Num(); ++i)
			{
				if (Actual.Stats[i].Tag != E.Stats[i].Tag)
				{
					Diffs.Add(FString::Printf(TEXT("Stats[%d] tag %s vs %s (order)"), i, *Actual.Stats[i].Tag.ToString(), *E.Stats[i].Tag.ToString()));
				}
				else if (!NearlyRel(Actual.Stats[i].Value, E.Stats[i].Value))
				{
					Diffs.Add(FString::Printf(TEXT("%s %.6f vs %.6f"), *E.Stats[i].Tag.ToString(), Actual.Stats[i].Value, E.Stats[i].Value));
				}
			}
		}
		return FString::Join(Diffs, TEXT("; "));
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

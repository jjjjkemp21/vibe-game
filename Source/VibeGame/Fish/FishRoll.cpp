// Copyright Epic Games, Inc. All Rights Reserved.

#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Engine/DataTable.h"
#include "Templates/TypeHash.h"

namespace FishRollPrivate
{
	/** Short stat name for traces: "Fish.Stat.Strength" -> "Strength" */
	static FString StatName(const FGameplayTag& Tag)
	{
		FString Name = Tag.ToString();
		Name.RemoveFromStart(TEXT("Fish.Stat."));
		return Name;
	}

	static bool HasRows(const UDataTable* Table, const UScriptStruct* RowStruct)
	{
		return Table && Table->GetRowStruct() == RowStruct;
	}

	/** floor(X + 0.5), clamped into int32 range; non-finite -> Fallback */
	static int32 RoundHalfUpToInt(double X, int32 Fallback)
	{
		if (!FMath::IsFinite(X))
		{
			return Fallback;
		}
		const double Rounded = FMath::FloorToDouble(X + 0.5);
		return static_cast<int32>(FMath::Clamp(Rounded, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32)));
	}

	/** Context tag plus its parents, so HasTag(Required) is a hierarchical "context is Required or a child of it" */
	static FGameplayTagContainer WithParents(const FGameplayTag& Tag)
	{
		return Tag.IsValid() ? FGameplayTagContainer(Tag) : FGameplayTagContainer();
	}

	static FGameplayTagContainer WithParents(const TArray<FGameplayTag>& Tags)
	{
		FGameplayTagContainer Container;
		for (const FGameplayTag& Tag : Tags)
		{
			if (Tag.IsValid())
			{
				Container.AddTag(Tag);
			}
		}
		return Container;
	}

	/** Empty Required = any. Otherwise the context (with parents) must have any Required tag. */
	static bool MatchesAnyOrEmpty(const FGameplayTagContainer& ContextWithParents, const TArray<FGameplayTag>& Required)
	{
		if (Required.Num() == 0)
		{
			return true;
		}
		for (const FGameplayTag& Tag : Required)
		{
			if (Tag.IsValid() && ContextWithParents.HasTag(Tag))
			{
				return true;
			}
		}
		return false;
	}

	static bool SpeciesEligible(const FFishSpeciesRow& Species, float Hours, const FGameplayTagContainer& Region,
		const FGameplayTagContainer& Habitat, const FGameplayTagContainer& Weather, const FGameplayTagContainer& Bait, FString* OutReason)
	{
		auto Fail = [OutReason](const TCHAR* Reason) { if (OutReason) { *OutReason = Reason; } return false; };
		if (!(FMath::IsFinite(Species.BiteWeight) && Species.BiteWeight > 0.0f)) { return Fail(TEXT("BiteWeight is 0")); }
		if (!MatchesAnyOrEmpty(Region, Species.RegionTags)) { return Fail(TEXT("wrong region")); }
		if (!MatchesAnyOrEmpty(Habitat, Species.HabitatTags)) { return Fail(TEXT("wrong habitat")); }
		if (!FFishRoll::IsInTimeWindows(Species.TimeWindows, Hours)) { return Fail(TEXT("wrong time of day")); }
		if (!MatchesAnyOrEmpty(Weather, Species.WeatherTags)) { return Fail(TEXT("wrong weather")); }
		if (!MatchesAnyOrEmpty(Bait, Species.AcceptedBait)) { return Fail(TEXT("wrong bait")); }
		return true;
	}

	/** A stat during the roll (double so huge multiplies can't overflow before the clamp) */
	struct FWorkStat
	{
		FGameplayTag Tag;
		const FFishStatRow* Row = nullptr;
		double Value = 0.0;
		double Base = 0.0;
	};

	struct FWorkingStats
	{
		TArray<FWorkStat> Stats;

		FWorkStat* Find(const FGameplayTag& Tag)
		{
			return Stats.FindByPredicate([&Tag](const FWorkStat& Stat) { return Stat.Tag == Tag; });
		}

		FString ToString() const
		{
			TArray<FWorkStat> Sorted = Stats;
			Sorted.Sort([](const FWorkStat& A, const FWorkStat& B) { return A.Tag.GetTagName().LexicalLess(B.Tag.GetTagName()); });
			FString Out;
			for (const FWorkStat& Stat : Sorted)
			{
				Out += FString::Printf(TEXT("%s%s %.4f"), Out.IsEmpty() ? TEXT("") : TEXT(", "), *StatName(Stat.Tag), Stat.Value);
			}
			return Out;
		}
	};

	/** All Adds (in order), then all Multiplies (in order, a product). Invalid mods are skipped with a warning. */
	static void ApplyStatMods(FWorkingStats& Stats, const TArray<TPair<FName, const FFishStatMod*>>& Mods, FName SpeciesId, TArray<FString>* Trace)
	{
		for (const EFishStatModOp Phase : { EFishStatModOp::Add, EFishStatModOp::Multiply })
		{
			for (const TPair<FName, const FFishStatMod*>& Entry : Mods)
			{
				const FFishStatMod& Mod = *Entry.Value;
				if (Mod.Op != Phase)
				{
					continue;
				}
				FWorkStat* Stat = Stats.Find(Mod.StatTag);
				if (!Stat)
				{
					UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: %s changes stat '%s', which has no DT_FishStat row; skipped"),
						*SpeciesId.ToString(), *Entry.Key.ToString(), *Mod.StatTag.ToString());
					continue;
				}
				if (!FMath::IsFinite(Mod.Value) || (Phase == EFishStatModOp::Multiply && Mod.Value <= 0.0f))
				{
					UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: %s has an invalid %s %g on '%s'; skipped"),
						*SpeciesId.ToString(), *Entry.Key.ToString(), Phase == EFishStatModOp::Add ? TEXT("Add") : TEXT("Multiply"), Mod.Value, *Mod.StatTag.ToString());
					continue;
				}
				const double Before = Stat->Value;
				Stat->Value = (Phase == EFishStatModOp::Add) ? Stat->Value + Mod.Value : Stat->Value * Mod.Value;
				if (Trace)
				{
					Trace->Add(FString::Printf(TEXT("    %s: %s %s %g: %.4f -> %.4f"), *Entry.Key.ToString(), *StatName(Mod.StatTag),
						Phase == EFishStatModOp::Add ? TEXT("+") : TEXT("x"), Mod.Value, Before, Stat->Value));
				}
			}
		}
	}

	template <typename RowType>
	static TArray<TPair<FName, const RowType*>> SortedRows(const UDataTable* Table)
	{
		TArray<TPair<FName, const RowType*>> Rows;
		if (HasRows(Table, RowType::StaticStruct()))
		{
			Rows.Reserve(Table->GetRowMap().Num());
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				Rows.Emplace(Pair.Key, reinterpret_cast<const RowType*>(Pair.Value));
			}
			Rows.Sort([](const TPair<FName, const RowType*>& A, const TPair<FName, const RowType*>& B) { return A.Key.LexicalLess(B.Key); });
		}
		return Rows;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FFishTables

const FFishSpeciesRow* FFishTables::FindSpecies(FName Id) const
{
	return (!Id.IsNone() && FishRollPrivate::HasRows(Species, FFishSpeciesRow::StaticStruct()))
		? reinterpret_cast<const FFishSpeciesRow*>(Species->FindRowUnchecked(Id)) : nullptr;
}

const FFishRarityRow* FFishTables::FindRarity(FName Id) const
{
	return (!Id.IsNone() && FishRollPrivate::HasRows(Rarities, FFishRarityRow::StaticStruct()))
		? reinterpret_cast<const FFishRarityRow*>(Rarities->FindRowUnchecked(Id)) : nullptr;
}

const FFishModifierRow* FFishTables::FindModifier(FName Id) const
{
	return (!Id.IsNone() && FishRollPrivate::HasRows(Modifiers, FFishModifierRow::StaticStruct()))
		? reinterpret_cast<const FFishModifierRow*>(Modifiers->FindRowUnchecked(Id)) : nullptr;
}

const FFishStatRow* FFishTables::FindStat(const FGameplayTag& Tag) const
{
	if (Tag.IsValid() && FishRollPrivate::HasRows(Stats, FFishStatRow::StaticStruct()))
	{
		for (const TPair<FName, uint8*>& Pair : Stats->GetRowMap())
		{
			const FFishStatRow* Row = reinterpret_cast<const FFishStatRow*>(Pair.Value);
			if (Row->Tag == Tag)
			{
				return Row;
			}
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pure helpers

int32 FFishRoll::PickWeightedIndex(TConstArrayView<float> Weights, float U)
{
	double Total = 0.0;
	int32 LastPositive = INDEX_NONE;
	for (int32 i = 0; i < Weights.Num(); ++i)
	{
		if (FMath::IsFinite(Weights[i]) && Weights[i] > 0.0f)
		{
			Total += Weights[i];
			LastPositive = i;
		}
	}
	if (LastPositive == INDEX_NONE || !(Total > 0.0) || !FMath::IsFinite(Total))
	{
		return INDEX_NONE;
	}

	const double Target = static_cast<double>(FMath::IsNaN(U) ? 0.0f : FMath::Clamp(U, 0.0f, 1.0f)) * Total;
	double Cumulative = 0.0;
	for (int32 i = 0; i <= LastPositive; ++i)
	{
		if (FMath::IsFinite(Weights[i]) && Weights[i] > 0.0f)
		{
			Cumulative += Weights[i];
			if (Target < Cumulative)
			{
				return i;
			}
		}
	}
	return LastPositive; // U == 1 or rounding: never falls through to a 0-weight entry
}

float FFishRoll::SampleWeight(const FFishSpeciesRow& Species, float U)
{
	const double Clamped = FMath::IsNaN(U) ? 0.0 : FMath::Clamp(static_cast<double>(U), 0.0, 1.0);
	const double Skew = (FMath::IsFinite(Species.SizeSkew) && Species.SizeSkew > 0.0f) ? Species.SizeSkew : 1.0;
	return static_cast<float>(Species.WeightMin + (static_cast<double>(Species.WeightMax) - Species.WeightMin) * FMath::Pow(Clamped, Skew));
}

float FFishRoll::WeightStatFactor(const FFishSpeciesRow& Species, float WeightKg)
{
	if (!(Species.ReferenceWeight > 0.0f) || !(WeightKg > 0.0f) || !FMath::IsFinite(Species.WeightStatExponent))
	{
		return 1.0f;
	}
	const double Factor = FMath::Pow(static_cast<double>(WeightKg) / Species.ReferenceWeight, static_cast<double>(Species.WeightStatExponent));
	return FMath::IsFinite(Factor) ? static_cast<float>(Factor) : 1.0f;
}

float FFishRoll::NormalizeHours(float Hours)
{
	if (!FMath::IsFinite(Hours))
	{
		return 0.0f;
	}
	float Wrapped = FMath::Fmod(Hours, 24.0f);
	if (Wrapped < 0.0f)
	{
		Wrapped += 24.0f;
	}
	return (Wrapped >= 24.0f) ? 0.0f : Wrapped;
}

bool FFishRoll::IsInTimeWindows(TConstArrayView<FFishTimeWindow> Windows, float Hours)
{
	if (Windows.Num() == 0)
	{
		return true;
	}
	const float Normalized = NormalizeHours(Hours);
	for (const FFishTimeWindow& Window : Windows)
	{
		if (Window.Contains(Normalized))
		{
			return true;
		}
	}
	return false;
}

bool FFishRoll::IsSpeciesEligible(const FFishSpeciesRow& Species, const FFishRollContext& Context, FString* OutReason)
{
	using namespace FishRollPrivate;
	return SpeciesEligible(Species, Context.TimeOfDayHours, WithParents(Context.RegionTag), WithParents(Context.HabitatTag),
		WithParents(Context.WeatherTag), WithParents(Context.BaitTag), OutReason);
}

bool FFishRoll::IsModifierEligible(FName ModifierId, const FFishModifierRow& Modifier, FName SpeciesId, const FFishSpeciesRow& Species,
	const FFishRollContext& Context, FString* OutReason)
{
	using namespace FishRollPrivate;
	auto Fail = [OutReason](const TCHAR* Reason) { if (OutReason) { *OutReason = Reason; } return false; };

	if (Species.AllowedModifiers.Num() > 0 && !Species.AllowedModifiers.Contains(ModifierId)) { return Fail(TEXT("not allowed by the species")); }
	if (Modifier.SpeciesIds.Num() > 0 && !Modifier.SpeciesIds.Contains(SpeciesId)) { return Fail(TEXT("species not listed")); }
	if (!MatchesAnyOrEmpty(WithParents(Species.SpeciesTags), Modifier.SpeciesTags)) { return Fail(TEXT("species tags don't match")); }
	if (!MatchesAnyOrEmpty(WithParents(Context.RegionTag), Modifier.RegionTags)) { return Fail(TEXT("wrong region")); }
	if (!IsInTimeWindows(Modifier.TimeWindows, Context.TimeOfDayHours)) { return Fail(TEXT("wrong time of day")); }
	if (!MatchesAnyOrEmpty(WithParents(Context.WeatherTag), Modifier.WeatherTags)) { return Fail(TEXT("wrong weather")); }
	return true;
}

TArray<TPair<FName, float>> FFishRoll::GetRarityWeights(const FFishTables& Tables, const FFishSpeciesRow& Species, float Luck)
{
	TArray<TPair<FName, const FFishRarityRow*>> Rows;
	if (Species.AllowedRarities.Num() > 0)
	{
		for (const FName& Id : Species.AllowedRarities)
		{
			const FFishRarityRow* Row = Tables.FindRarity(Id);
			if (Row && !Rows.ContainsByPredicate([&Id](const TPair<FName, const FFishRarityRow*>& Entry) { return Entry.Key == Id; }))
			{
				Rows.Emplace(Id, Row);
			}
		}
	}
	else
	{
		Rows = FishRollPrivate::SortedRows<FFishRarityRow>(Tables.Rarities);
	}
	Rows.Sort([](const TPair<FName, const FFishRarityRow*>& A, const TPair<FName, const FFishRarityRow*>& B)
	{
		return A.Value->Rank != B.Value->Rank ? A.Value->Rank < B.Value->Rank : A.Key.LexicalLess(B.Key);
	});

	const double SafeLuck = FMath::IsFinite(Luck) ? FMath::Max(0.0, static_cast<double>(Luck)) : 0.0;
	const double Factor = FMath::IsFinite(Tables.Tuning.LuckRankFactor) ? FMath::Max(0.0, static_cast<double>(Tables.Tuning.LuckRankFactor)) : 0.0;
	TArray<TPair<FName, float>> Result;
	Result.Reserve(Rows.Num());
	for (const TPair<FName, const FFishRarityRow*>& Entry : Rows)
	{
		const float Base = Entry.Value->RollWeight;
		double Weight = 0.0;
		if (FMath::IsFinite(Base) && Base > 0.0f)
		{
			Weight = Base * FMath::Max(0.0, 1.0 + SafeLuck * Entry.Value->Rank * Factor);
		}
		Result.Emplace(Entry.Key, FMath::IsFinite(Weight) ? static_cast<float>(FMath::Min(Weight, static_cast<double>(MAX_flt))) : 0.0f);
	}
	return Result;
}

int32 FFishRoll::ComputeXp(const FFishRollTuning& Tuning, int32 Level, float XpMultiplier)
{
	const double BaseXp = static_cast<double>(Tuning.XpBase) + static_cast<double>(Tuning.XpPerLevel) * (static_cast<double>(Level) - 1.0);
	return FMath::Max(0, FishRollPrivate::RoundHalfUpToInt(BaseXp * XpMultiplier, 0));
}

float FFishRoll::LevelDifficultyMultiplier(int32 FishLevel, int32 PlayerLevel, const FFishLevelScaling& Scaling)
{
	return Scaling.GetMultiplier(FishLevel, PlayerLevel);
}

uint32 FFishRoll::StageSeed(int32 Seed, EFishRollStage Stage)
{
	return HashCombine(static_cast<uint32>(Seed), static_cast<uint32>(Stage));
}

FRandomStream FFishRoll::MakeStageStream(int32 Seed, EFishRollStage Stage)
{
	return FRandomStream(static_cast<int32>(StageSeed(Seed, Stage)));
}

int32 FFishRoll::MakeRandomSeed()
{
	// Three FMath::Rand calls cover all 32 bits even where RAND_MAX is 0x7fff
	return static_cast<int32>((static_cast<uint32>(FMath::Rand()) << 17) ^ (static_cast<uint32>(FMath::Rand()) << 2) ^ static_cast<uint32>(FMath::Rand()));
}

FName FFishRoll::WeightStatName()
{
	static const FName Name(TEXT("Fish.Stat.Weight"));
	return Name;
}

// ---------------------------------------------------------------------------------------------------------------------
// The roll pipeline

bool FFishRoll::Roll(const FFishTables& Tables, const FFishRollContext& Context, FFishInstance& OutFish, TArray<FString>* OutTrace)
{
	using namespace FishRollPrivate;
	OutFish = FFishInstance();
	const FName SpeciesId = Context.SpeciesId;

	auto Fail = [OutTrace, &SpeciesId](const FString& Error)
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s failed: %s"), *SpeciesId.ToString(), *Error);
		if (OutTrace) { OutTrace->Add(TEXT("FAILED: ") + Error); }
		return false;
	};

	if (!HasRows(Tables.Species, FFishSpeciesRow::StaticStruct())) { return Fail(TEXT("DT_FishSpecies table is missing or has the wrong row struct")); }
	if (!HasRows(Tables.Rarities, FFishRarityRow::StaticStruct())) { return Fail(TEXT("DT_FishRarity table is missing or has the wrong row struct")); }
	if (!HasRows(Tables.Stats, FFishStatRow::StaticStruct())) { return Fail(TEXT("DT_FishStat table is missing or has the wrong row struct")); }
	const FFishSpeciesRow* SpeciesPtr = Tables.FindSpecies(SpeciesId);
	if (!SpeciesPtr) { return Fail(TEXT("unknown species")); }
	const FFishSpeciesRow& Species = *SpeciesPtr;

	if (!(FMath::IsFinite(Species.WeightMin) && FMath::IsFinite(Species.WeightMax) && Species.WeightMin > 0.0f && Species.WeightMax >= Species.WeightMin))
	{
		return Fail(FString::Printf(TEXT("invalid weight range [%g, %g]"), Species.WeightMin, Species.WeightMax));
	}
	if (!(FMath::IsFinite(Species.SizeSkew) && Species.SizeSkew > 0.0f)) { return Fail(FString::Printf(TEXT("invalid SizeSkew %g"), Species.SizeSkew)); }
	if (!(FMath::IsFinite(Species.ReferenceWeight) && Species.ReferenceWeight > 0.0f)) { return Fail(FString::Printf(TEXT("invalid ReferenceWeight %g"), Species.ReferenceWeight)); }

	const int32 Seed = Context.Seed;
	if (OutTrace)
	{
		OutTrace->Add(FString::Printf(TEXT("Roll %s, seed %d, luck %g, region %s, %.2f h"), *SpeciesId.ToString(), Seed, Context.Luck,
			*Context.RegionTag.ToString(), NormalizeHours(Context.TimeOfDayHours)));
	}

	// 1. Species base: every registered stat at its Default, then the species BaseStats -------------------------------
	FWorkingStats Stats;
	FWorkStat* WeightStat = nullptr;
	for (const TPair<FName, const FFishStatRow*>& Entry : SortedRows<FFishStatRow>(Tables.Stats))
	{
		const FFishStatRow& Row = *Entry.Value;
		if (!Row.Tag.IsValid() || Stats.Find(Row.Tag))
		{
			continue; // empty or duplicate stat rows are validation problems; the first row of a tag wins
		}
		FWorkStat& Stat = Stats.Stats.AddDefaulted_GetRef();
		Stat.Tag = Row.Tag;
		Stat.Row = &Row;
		Stat.Value = FMath::IsFinite(Row.Default) ? Row.Default : 0.0;
	}
	for (FWorkStat& Stat : Stats.Stats)
	{
		if (Stat.Tag.GetTagName() == WeightStatName())
		{
			WeightStat = &Stat;
		}
	}
	if (!WeightStat) { return Fail(TEXT("DT_FishStat has no Fish.Stat.Weight row")); }

	for (const FFishStatValue& BaseStat : Species.BaseStats)
	{
		FWorkStat* Stat = Stats.Find(BaseStat.Tag);
		if (!Stat || Stat == WeightStat || !FMath::IsFinite(BaseStat.Value))
		{
			UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: base stat '%s' = %g is unregistered, the weight, or not finite; skipped"),
				*SpeciesId.ToString(), *BaseStat.Tag.ToString(), BaseStat.Value);
			continue;
		}
		Stat->Value = BaseStat.Value;
	}
	for (FWorkStat& Stat : Stats.Stats)
	{
		Stat.Base = Stat.Value;
	}
	if (OutTrace)
	{
		OutTrace->Add(FString::Printf(TEXT("1 base: %s"), *Stats.ToString()));
	}

	// 2. Weight ---------------------------------------------------------------------------------------------------------
	float Weight = Species.WeightMin;
	{
		FRandomStream WeightStream = MakeStageStream(Seed, EFishRollStage::Weight);
		const float U = WeightStream.FRand();
		if (Context.bForceWeightFraction)
		{
			float Fraction = Context.ForcedWeightFraction;
			if (FMath::IsNaN(Fraction))
			{
				UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: ForcedWeightFraction is NaN, using 0"), *SpeciesId.ToString());
				Fraction = 0.0f;
			}
			Fraction = FMath::Clamp(Fraction, 0.0f, 1.0f);
			Weight = static_cast<float>(Species.WeightMin + (static_cast<double>(Species.WeightMax) - Species.WeightMin) * Fraction);
			if (OutTrace)
			{
				OutTrace->Add(FString::Printf(TEXT("2 weight: forced fraction %g -> %.4f kg"), Fraction, Weight));
			}
		}
		else
		{
			Weight = SampleWeight(Species, U);
			if (OutTrace)
			{
				OutTrace->Add(FString::Printf(TEXT("2 weight: U %.6f ^ skew %g = %.6f -> %g + (%g - %g) x %.6f = %.4f kg"),
					U, Species.SizeSkew, FMath::Pow(U, Species.SizeSkew), Species.WeightMin, Species.WeightMax, Species.WeightMin, FMath::Pow(U, Species.SizeSkew), Weight));
			}
		}
		const float Factor = WeightStatFactor(Species, Weight);
		if (OutTrace)
		{
			OutTrace->Add(FString::Printf(TEXT("  difficulty stat factor (%.4f / %g) ^ %g = %.6f"), Weight, Species.ReferenceWeight, Species.WeightStatExponent, Factor));
		}
		for (FWorkStat& Stat : Stats.Stats)
		{
			if (&Stat != WeightStat && Stat.Row->bIsDifficultyStat)
			{
				const double Before = Stat.Value;
				Stat.Value *= Factor;
				if (OutTrace)
				{
					OutTrace->Add(FString::Printf(TEXT("    %s: %.4f x %.6f = %.4f"), *StatName(Stat.Tag), Before, Factor, Stat.Value));
				}
			}
		}
		WeightStat->Value = Weight;
	}

	// 3. Rarity ---------------------------------------------------------------------------------------------------------
	float Luck = Context.Luck;
	if (FMath::IsNaN(Luck))
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: luck is NaN, using 0"), *SpeciesId.ToString());
		Luck = 0.0f;
	}
	const float MaxLuck = (FMath::IsFinite(Tables.Tuning.MaxLuck) && Tables.Tuning.MaxLuck > 0.0f) ? Tables.Tuning.MaxLuck : 0.0f;
	Luck = FMath::Clamp(Luck, 0.0f, MaxLuck);

	FName RarityId;
	if (!Context.ForcedRarityId.IsNone())
	{
		RarityId = Context.ForcedRarityId;
		if (!Tables.FindRarity(RarityId))
		{
			return Fail(FString::Printf(TEXT("unknown forced rarity '%s'"), *RarityId.ToString()));
		}
		if (OutTrace)
		{
			OutTrace->Add(FString::Printf(TEXT("3 rarity: forced %s"), *RarityId.ToString()));
		}
	}
	else
	{
		for (const FName& Id : Species.AllowedRarities)
		{
			if (!Tables.FindRarity(Id))
			{
				UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: allowed rarity '%s' is not a DT_FishRarity row; ignored"), *SpeciesId.ToString(), *Id.ToString());
			}
		}
		const TArray<TPair<FName, float>> Candidates = GetRarityWeights(Tables, Species, Luck);
		TArray<float> Weights;
		FString WeightText;
		for (const TPair<FName, float>& Entry : Candidates)
		{
			const float Raw = Tables.FindRarity(Entry.Key)->RollWeight;
			if (!FMath::IsFinite(Raw) || Raw < 0.0f)
			{
				UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: rarity '%s' has an invalid RollWeight %g; counted as 0"), *SpeciesId.ToString(), *Entry.Key.ToString(), Raw);
			}
			Weights.Add(Entry.Value);
			if (OutTrace)
			{
				WeightText += FString::Printf(TEXT("%s%s %g"), WeightText.IsEmpty() ? TEXT("") : TEXT(", "), *Entry.Key.ToString(), Entry.Value);
			}
		}
		FRandomStream RarityStream = MakeStageStream(Seed, EFishRollStage::Rarity);
		const float U = RarityStream.FRand();
		const int32 Index = PickWeightedIndex(Weights, U);
		if (Index == INDEX_NONE)
		{
			return Fail(TEXT("no allowed rarity has a roll weight above 0"));
		}
		RarityId = Candidates[Index].Key;
		if (OutTrace)
		{
			double Total = 0.0;
			for (const float W : Weights) { Total += W; }
			OutTrace->Add(FString::Printf(TEXT("3 rarity: luck %g, weights [%s], total %g; U %.6f x total = %.4f -> %s"),
				Luck, *WeightText, Total, U, U * Total, *RarityId.ToString()));
		}
	}
	const FFishRarityRow& Rarity = *Tables.FindRarity(RarityId);
	{
		TArray<TPair<FName, const FFishStatMod*>> Mods;
		for (const FFishStatMod& Mod : Rarity.StatMods)
		{
			Mods.Emplace(RarityId, &Mod);
		}
		ApplyStatMods(Stats, Mods, SpeciesId, OutTrace);
	}
	const int32 Level = static_cast<int32>(FMath::Clamp(static_cast<int64>(Species.BaseLevel) + Rarity.LevelBonus, int64(1), static_cast<int64>(MAX_int32)));
	if (OutTrace)
	{
		OutTrace->Add(FString::Printf(TEXT("  level %d + %d = %d"), Species.BaseLevel, Rarity.LevelBonus, Level));
	}

	// 4. Modifiers ------------------------------------------------------------------------------------------------------
	TArray<FName> Kept;
	if (Context.bForceModifiers)
	{
		for (const FName& Id : Context.ForcedModifierIds)
		{
			if (!Tables.FindModifier(Id))
			{
				return Fail(FString::Printf(TEXT("unknown forced modifier '%s'"), *Id.ToString()));
			}
			Kept.AddUnique(Id);
		}
		Kept.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		if (OutTrace)
		{
			OutTrace->Add(FString::Printf(TEXT("4 modifiers: forced [%s]"), *FString::JoinBy(Kept, TEXT(", "), [](const FName& Id) { return Id.ToString(); })));
		}
	}
	else if (!HasRows(Tables.Modifiers, FFishModifierRow::StaticStruct()))
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: DT_FishModifier table is missing or has the wrong row struct; no modifiers"), *SpeciesId.ToString());
	}
	else
	{
		const TArray<TPair<FName, const FFishModifierRow*>> Rows = SortedRows<FFishModifierRow>(Tables.Modifiers);
		FRandomStream ModifierStream = MakeStageStream(Seed, EFishRollStage::Modifiers);
		TArray<int32> Hits;
		if (OutTrace)
		{
			OutTrace->Add(TEXT("4 modifiers:"));
		}
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const float U = ModifierStream.FRand(); // one draw per row, eligible or not
			const FFishModifierRow& Row = *Rows[i].Value;
			FString Reason;
			const bool bEligible = IsModifierEligible(Rows[i].Key, Row, SpeciesId, Species, Context, &Reason);
			float Chance = Row.RollChance;
			if (!FMath::IsFinite(Chance) || Chance < 0.0f || Chance > 1.0f)
			{
				UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: modifier '%s' has an invalid RollChance %g; clamped to [0, 1]"), *SpeciesId.ToString(), *Rows[i].Key.ToString(), Chance);
				Chance = FMath::IsFinite(Chance) ? FMath::Clamp(Chance, 0.0f, 1.0f) : 0.0f;
			}
			const bool bHit = bEligible && U < Chance;
			if (bHit)
			{
				Hits.Add(i);
			}
			if (OutTrace)
			{
				OutTrace->Add(bEligible
					? FString::Printf(TEXT("  %s: chance %g, U %.6f -> %s"), *Rows[i].Key.ToString(), Chance, U, bHit ? TEXT("HIT") : TEXT("no"))
					: FString::Printf(TEXT("  %s: not eligible (%s), U %.6f unused"), *Rows[i].Key.ToString(), *Reason, U));
			}
		}

		// Exclusivity groups: if 2+ hits share a group, keep one, weighted by RollChance
		TArray<FName> Groups;
		for (const int32 Hit : Hits)
		{
			const FName Group = Rows[Hit].Value->ExclusivityGroup;
			if (!Group.IsNone())
			{
				Groups.AddUnique(Group);
			}
		}
		Groups.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		TSet<int32> Dropped;
		for (const FName& Group : Groups)
		{
			TArray<int32> Members;
			TArray<float> Chances;
			for (const int32 Hit : Hits)
			{
				if (Rows[Hit].Value->ExclusivityGroup == Group)
				{
					Members.Add(Hit);
					Chances.Add(FMath::Clamp(Rows[Hit].Value->RollChance, 0.0f, 1.0f));
				}
			}
			if (Members.Num() < 2)
			{
				continue;
			}
			const float U = ModifierStream.FRand();
			const int32 Winner = FMath::Max(PickWeightedIndex(Chances, U), 0);
			for (int32 m = 0; m < Members.Num(); ++m)
			{
				if (m != Winner)
				{
					Dropped.Add(Members[m]);
				}
			}
			if (OutTrace)
			{
				OutTrace->Add(FString::Printf(TEXT("  group %s: %d hits, U %.6f -> keep %s"), *Group.ToString(), Members.Num(), U, *Rows[Members[Winner]].Key.ToString()));
			}
		}

		const int32 MaxModifiers = FMath::Max(Species.MaxModifiers, 0);
		for (const int32 Hit : Hits)
		{
			if (Dropped.Contains(Hit))
			{
				continue;
			}
			if (Kept.Num() >= MaxModifiers)
			{
				if (OutTrace)
				{
					OutTrace->Add(FString::Printf(TEXT("  %s: dropped, MaxModifiers %d reached"), *Rows[Hit].Key.ToString(), MaxModifiers));
				}
				continue;
			}
			Kept.Add(Rows[Hit].Key);
		}
	}
	{
		TArray<TPair<FName, const FFishStatMod*>> Mods;
		for (const FName& Id : Kept)
		{
			for (const FFishStatMod& Mod : Tables.FindModifier(Id)->StatMods)
			{
				Mods.Emplace(Id, &Mod);
			}
		}
		if (OutTrace)
		{
			OutTrace->Add(FString::Printf(TEXT("  kept [%s]; all Adds, then all Multiplies:"), *FString::JoinBy(Kept, TEXT(", "), [](const FName& Id) { return Id.ToString(); })));
		}
		ApplyStatMods(Stats, Mods, SpeciesId, OutTrace);
	}

	// 5. Clamp ----------------------------------------------------------------------------------------------------------
	for (FWorkStat& Stat : Stats.Stats)
	{
		const double Low = FMath::Min(Stat.Row->Min, Stat.Row->Max);
		const double High = FMath::Max(Stat.Row->Min, Stat.Row->Max);
		if (FMath::IsNaN(Stat.Value))
		{
			UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: stat '%s' became NaN; reset to its default"), *SpeciesId.ToString(), *Stat.Tag.ToString());
			Stat.Value = Stat.Row->Default;
		}
		Stat.Value = FMath::Clamp(Stat.Value, Low, High);
		if (!FMath::IsFinite(Stat.Value))
		{
			Stat.Value = 0.0; // only reachable with non-finite Min/Max rows (validation problem)
		}
	}
	const float WeightKg = static_cast<float>(WeightStat->Value);
	if (OutTrace)
	{
		OutTrace->Add(FString::Printf(TEXT("5 clamp: %s"), *Stats.ToString()));
	}

	// 6. Value, XP, difficulty rating ------------------------------------------------------------------------------------
	double ValueMultiplier = Rarity.ValueMultiplier;
	for (const FName& Id : Kept)
	{
		ValueMultiplier *= Tables.FindModifier(Id)->ValueMultiplier;
	}
	const double RawValue = static_cast<double>(Species.BaseValuePerKg) * WeightKg * ValueMultiplier;
	if (!FMath::IsFinite(RawValue) || RawValue < 0.0)
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: value %g is invalid (check BaseValuePerKg and value multipliers); using 1"), *SpeciesId.ToString(), RawValue);
	}
	const int32 Value = FMath::Max(1, RoundHalfUpToInt(RawValue, 1));
	const int32 Xp = ComputeXp(Tables.Tuning, Level, Rarity.XpMultiplier);

	double RatioSum = 0.0;
	int32 RatioCount = 0;
	for (const FWorkStat& Stat : Stats.Stats)
	{
		if (&Stat != WeightStat && Stat.Row->bIsDifficultyStat && Stat.Base > 0.0)
		{
			RatioSum += Stat.Value / Stat.Base;
			++RatioCount;
		}
	}
	const double Rating = RatioCount > 0 ? RatioSum / RatioCount : 1.0;

	if (OutTrace)
	{
		OutTrace->Add(FString::Printf(TEXT("6 value: %g/kg x %.4f kg x %.6f = %.4f -> %d coins; XP (%g + %g x (%d - 1)) x %g -> %d; difficulty rating %.4f"),
			Species.BaseValuePerKg, WeightKg, ValueMultiplier, RawValue, Value, Tables.Tuning.XpBase, Tables.Tuning.XpPerLevel, Level, Rarity.XpMultiplier, Xp, Rating));
	}

	Stats.Stats.Sort([](const FWorkStat& A, const FWorkStat& B) { return A.Tag.GetTagName().LexicalLess(B.Tag.GetTagName()); });
	OutFish.SpeciesId = SpeciesId;
	OutFish.RarityId = RarityId;
	OutFish.ModifierIds = MoveTemp(Kept);
	OutFish.WeightKg = WeightKg;
	OutFish.Level = Level;
	OutFish.Stats.Reserve(Stats.Stats.Num());
	for (const FWorkStat& Stat : Stats.Stats)
	{
		OutFish.Stats.Emplace(Stat.Tag, static_cast<float>(Stat.Value));
	}
	OutFish.Value = Value;
	OutFish.Xp = Xp;
	OutFish.DifficultyRating = FMath::IsFinite(Rating) ? static_cast<float>(Rating) : 1.0f;
	OutFish.Seed = Seed;
	if (OutTrace)
	{
		OutTrace->Add(TEXT("= ") + OutFish.ToString());
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Bite picker

bool FFishRoll::PickSpecies(const FFishTables& Tables, const FFishRollContext& Context, FName& OutSpeciesId, TArray<FString>* OutTrace)
{
	using namespace FishRollPrivate;
	OutSpeciesId = NAME_None;
	if (!HasRows(Tables.Species, FFishSpeciesRow::StaticStruct()))
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fish bite: DT_FishSpecies table is missing or has the wrong row struct"));
		return false;
	}

	const FGameplayTagContainer Region = WithParents(Context.RegionTag);
	const FGameplayTagContainer Habitat = WithParents(Context.HabitatTag);
	const FGameplayTagContainer Weather = WithParents(Context.WeatherTag);
	const FGameplayTagContainer Bait = WithParents(Context.BaitTag);
	const float Hours = NormalizeHours(Context.TimeOfDayHours);

	TArray<FName> Ids;
	TArray<float> Weights;
	for (const TPair<FName, uint8*>& Pair : Tables.Species->GetRowMap())
	{
		const FFishSpeciesRow& Species = *reinterpret_cast<const FFishSpeciesRow*>(Pair.Value);
		FString Reason;
		if (SpeciesEligible(Species, Hours, Region, Habitat, Weather, Bait, OutTrace ? &Reason : nullptr))
		{
			Ids.Add(Pair.Key);
			Weights.Add(Species.BiteWeight);
		}
		else if (OutTrace)
		{
			OutTrace->Add(FString::Printf(TEXT("bite: %s not eligible (%s)"), *Pair.Key.ToString(), *Reason));
		}
	}

	FRandomStream BiteStream = MakeStageStream(Context.Seed, EFishRollStage::Bite);
	const float U = BiteStream.FRand();
	const int32 Index = PickWeightedIndex(Weights, U);
	if (Index == INDEX_NONE)
	{
		if (OutTrace)
		{
			OutTrace->Add(TEXT("bite: nothing can bite here"));
		}
		return false;
	}
	OutSpeciesId = Ids[Index];
	if (OutTrace)
	{
		OutTrace->Add(FString::Printf(TEXT("bite: %d eligible, U %.6f -> %s"), Ids.Num(), U, *OutSpeciesId.ToString()));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// UFishLibrary

bool UFishLibrary::RollFishFromSettings(const FFishRollContext& Context, FFishInstance& OutFish)
{
	FFishTables Tables;
	FString Error;
	if (!GetDefault<UFishSettings>()->LoadTables(Tables, Error))
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fish roll %s: tables not ready: %s"), *Context.SpeciesId.ToString(), *Error);
		OutFish = FFishInstance();
		return false;
	}
	return FFishRoll::Roll(Tables, Context, OutFish);
}

bool UFishLibrary::PickSpeciesFromSettings(const FFishRollContext& Context, FName& OutSpeciesId)
{
	FFishTables Tables;
	FString Error;
	if (!GetDefault<UFishSettings>()->LoadTables(Tables, Error))
	{
		UE_LOG(LogLureFish, Warning, TEXT("Fish bite: tables not ready: %s"), *Error);
		OutSpeciesId = NAME_None;
		return false;
	}
	return FFishRoll::PickSpecies(Tables, Context, OutSpeciesId);
}

float UFishLibrary::GetFishStat(const FFishInstance& Fish, FGameplayTag Stat, float Default)
{
	return Fish.GetStat(Stat, Default);
}

bool UFishLibrary::FishHasModifier(const FFishInstance& Fish, FName ModifierId)
{
	return Fish.HasModifier(ModifierId);
}

float UFishLibrary::GetLevelDifficultyMultiplier(int32 FishLevel, int32 PlayerLevel)
{
	return FFishRoll::LevelDifficultyMultiplier(FishLevel, PlayerLevel, GetDefault<UFishSettings>()->LevelScaling);
}

int32 UFishLibrary::MakeRandomFishSeed()
{
	return FFishRoll::MakeRandomSeed();
}

FString UFishLibrary::FishToString(const FFishInstance& Fish)
{
	return Fish.ToString();
}

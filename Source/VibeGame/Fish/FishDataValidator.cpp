// Copyright Epic Games, Inc. All Rights Reserved.

#include "Fish/FishDataValidator.h"
#include "Fish/FishRoll.h"
#include "Engine/DataTable.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace FishValidatorPrivate
{
	struct FChecker
	{
		const FFishTables& Tables;
		TArray<FString>& Out;

		void Problem(const FString& Text) { Out.Add(Text); }

		static bool IsRegistered(const FGameplayTag& Tag)
		{
			return Tag.IsValid() && FGameplayTag::RequestGameplayTag(Tag.GetTagName(), /*ErrorIfNotFound*/ false).IsValid();
		}

		/** Registered, and under one of the roots ("Habitat" means "Habitat.*"). Test.* fixture tags skip the category check. */
		void CheckTag(const FGameplayTag& Tag, std::initializer_list<const TCHAR*> Roots, const FString& Where)
		{
			if (!Tag.IsValid())
			{
				Problem(Where + TEXT(": tag is empty or unknown (register it in Config/Tags/FishTags.ini)"));
				return;
			}
			if (!IsRegistered(Tag))
			{
				Problem(FString::Printf(TEXT("%s: tag '%s' is not registered (add it to Config/Tags/FishTags.ini)"), *Where, *Tag.ToString()));
				return;
			}
			const FString Name = Tag.ToString();
			if (Name.StartsWith(TEXT("Test."), ESearchCase::IgnoreCase))
			{
				return;
			}
			FString Expected;
			for (const TCHAR* Root : Roots)
			{
				if (Name.StartsWith(FString(Root) + TEXT("."), ESearchCase::IgnoreCase))
				{
					return;
				}
				Expected += (Expected.IsEmpty() ? FString() : FString(TEXT(" or "))) + Root + TEXT(".*");
			}
			Problem(FString::Printf(TEXT("%s: tag '%s' must be under %s"), *Where, *Name, *Expected));
		}

		void CheckTagList(const TArray<FGameplayTag>& Tags, std::initializer_list<const TCHAR*> Roots, const FString& Where)
		{
			TSet<FGameplayTag> Seen;
			for (int32 i = 0; i < Tags.Num(); ++i)
			{
				CheckTag(Tags[i], Roots, FString::Printf(TEXT("%s[%d]"), *Where, i));
				bool bDuplicate = false;
				Seen.Add(Tags[i], &bDuplicate);
				if (bDuplicate && Tags[i].IsValid())
				{
					Problem(FString::Printf(TEXT("%s: tag '%s' is listed twice"), *Where, *Tags[i].ToString()));
				}
			}
		}

		/** A stat tag: registered, under Fish.Stat, with a DT_FishStat row */
		void CheckStatTag(const FGameplayTag& Tag, const FString& Where)
		{
			const int32 Before = Out.Num();
			CheckTag(Tag, { TEXT("Fish.Stat") }, Where);
			if (Out.Num() == Before && !Tables.FindStat(Tag))
			{
				Problem(FString::Printf(TEXT("%s: stat '%s' has no DT_FishStat row"), *Where, *Tag.ToString()));
			}
		}

		void CheckStatMods(const TArray<FFishStatMod>& Mods, const FString& Where)
		{
			for (int32 i = 0; i < Mods.Num(); ++i)
			{
				const FString ModWhere = FString::Printf(TEXT("%s[%d]"), *Where, i);
				CheckStatTag(Mods[i].StatTag, ModWhere);
				if (!FMath::IsFinite(Mods[i].Value))
				{
					Problem(ModWhere + TEXT(": Value is not a finite number"));
				}
				else if (Mods[i].Op == EFishStatModOp::Multiply && !(Mods[i].Value > 0.0f))
				{
					Problem(FString::Printf(TEXT("%s: Multiply value %g must be > 0"), *ModWhere, Mods[i].Value));
				}
			}
		}

		void CheckTimeWindows(const TArray<FFishTimeWindow>& Windows, const FString& Where)
		{
			for (int32 i = 0; i < Windows.Num(); ++i)
			{
				const FFishTimeWindow& Window = Windows[i];
				const bool bInRange = FMath::IsFinite(Window.StartHour) && FMath::IsFinite(Window.EndHour)
					&& Window.StartHour >= 0.0f && Window.StartHour <= 24.0f && Window.EndHour >= 0.0f && Window.EndHour <= 24.0f;
				if (!bInRange)
				{
					Problem(FString::Printf(TEXT("%s[%d]: hours %g -> %g must be within [0, 24]"), *Where, i, Window.StartHour, Window.EndHour));
				}
				else if (Window.StartHour == Window.EndHour)
				{
					Problem(FString::Printf(TEXT("%s[%d]: StartHour == EndHour (%g) is an empty window (use 0 -> 24 for all day)"), *Where, i, Window.StartHour));
				}
			}
		}

		void CheckIdList(const TArray<FName>& Ids, const FString& Where, TFunctionRef<bool(FName)> Exists, const TCHAR* TableName)
		{
			TSet<FName> Seen;
			for (int32 i = 0; i < Ids.Num(); ++i)
			{
				if (Ids[i].IsNone())
				{
					Problem(FString::Printf(TEXT("%s[%d]: id is empty"), *Where, i));
					continue;
				}
				if (!Exists(Ids[i]))
				{
					Problem(FString::Printf(TEXT("%s[%d]: '%s' is not a %s row"), *Where, i, *Ids[i].ToString(), TableName));
				}
				bool bDuplicate = false;
				Seen.Add(Ids[i], &bDuplicate);
				if (bDuplicate)
				{
					Problem(FString::Printf(TEXT("%s: id '%s' is listed twice"), *Where, *Ids[i].ToString()));
				}
			}
		}

		void Positive(float Value, const FString& Where)
		{
			if (!(FMath::IsFinite(Value) && Value > 0.0f))
			{
				Problem(FString::Printf(TEXT("%s = %g must be > 0"), *Where, Value));
			}
		}

		void NonNegative(float Value, const FString& Where)
		{
			if (!(FMath::IsFinite(Value) && Value >= 0.0f))
			{
				Problem(FString::Printf(TEXT("%s = %g must be >= 0"), *Where, Value));
			}
		}
	};

	template <typename RowType>
	static TArray<TPair<FName, const RowType*>> SortedRows(const UDataTable* Table)
	{
		TArray<TPair<FName, const RowType*>> Rows;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			Rows.Emplace(Pair.Key, reinterpret_cast<const RowType*>(Pair.Value));
		}
		Rows.Sort([](const TPair<FName, const RowType*>& A, const TPair<FName, const RowType*>& B) { return A.Key.LexicalLess(B.Key); });
		return Rows;
	}

	static bool CheckTable(const UDataTable* Table, const UScriptStruct* RowStruct, const TCHAR* Name, TArray<FString>& Out)
	{
		if (!Table)
		{
			Out.Add(FString::Printf(TEXT("%s: table is missing"), Name));
			return false;
		}
		if (Table->GetRowStruct() != RowStruct)
		{
			Out.Add(FString::Printf(TEXT("%s: row struct is '%s', expected '%s'"), Name,
				Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("None"), *RowStruct->GetName()));
			return false;
		}
		return true;
	}
}

TArray<FString> FFishDataValidator::Validate(const FFishTables& Tables)
{
	using namespace FishValidatorPrivate;
	TArray<FString> Out;

	bool bTablesOk = CheckTable(Tables.Species, FFishSpeciesRow::StaticStruct(), TEXT("DT_FishSpecies"), Out);
	bTablesOk &= CheckTable(Tables.Rarities, FFishRarityRow::StaticStruct(), TEXT("DT_FishRarity"), Out);
	bTablesOk &= CheckTable(Tables.Modifiers, FFishModifierRow::StaticStruct(), TEXT("DT_FishModifier"), Out);
	bTablesOk &= CheckTable(Tables.Stats, FFishStatRow::StaticStruct(), TEXT("DT_FishStat"), Out);
	if (!bTablesOk)
	{
		return Out;
	}

	FChecker C{ Tables, Out };
	const TArray<TPair<FName, const FFishStatRow*>> StatRows = SortedRows<FFishStatRow>(Tables.Stats);
	const TArray<TPair<FName, const FFishSpeciesRow*>> SpeciesRows = SortedRows<FFishSpeciesRow>(Tables.Species);
	const TArray<TPair<FName, const FFishRarityRow*>> RarityRows = SortedRows<FFishRarityRow>(Tables.Rarities);
	const TArray<TPair<FName, const FFishModifierRow*>> ModifierRows = SortedRows<FFishModifierRow>(Tables.Modifiers);
	if (StatRows.Num() == 0) { C.Problem(TEXT("DT_FishStat has no rows")); }
	if (SpeciesRows.Num() == 0) { C.Problem(TEXT("DT_FishSpecies has no rows")); }
	if (RarityRows.Num() == 0) { C.Problem(TEXT("DT_FishRarity has no rows")); }

	// ---- Stats ----
	const FFishStatRow* WeightRow = nullptr;
	TMap<FGameplayTag, FName> StatTags;
	for (const TPair<FName, const FFishStatRow*>& Entry : StatRows)
	{
		const FFishStatRow& Row = *Entry.Value;
		const FString Where = FString::Printf(TEXT("DT_FishStat row '%s'"), *Entry.Key.ToString());
		C.CheckTag(Row.Tag, { TEXT("Fish.Stat") }, Where + TEXT(".Tag"));
		if (Row.Tag.IsValid())
		{
			if (const FName* Other = StatTags.Find(Row.Tag))
			{
				C.Problem(FString::Printf(TEXT("%s: tag '%s' is already used by row '%s'"), *Where, *Row.Tag.ToString(), *Other->ToString()));
			}
			else
			{
				StatTags.Add(Row.Tag, Entry.Key);
			}
		}
		if (Row.DisplayName.IsEmptyOrWhitespace()) { C.Problem(Where + TEXT(".DisplayName is empty")); }
		if (!(FMath::IsFinite(Row.Min) && FMath::IsFinite(Row.Max) && FMath::IsFinite(Row.Default) && Row.Min <= Row.Default && Row.Default <= Row.Max))
		{
			C.Problem(FString::Printf(TEXT("%s: needs Min <= Default <= Max (got %g, %g, %g)"), *Where, Row.Min, Row.Default, Row.Max));
		}
		if (Row.Tag.GetTagName() == FFishRoll::WeightStatName())
		{
			WeightRow = &Row;
			if (!(Row.Min > 0.0f)) { C.Problem(FString::Printf(TEXT("%s: the weight stat's Min %g must be > 0"), *Where, Row.Min)); }
			if (Row.bIsDifficultyStat) { C.Problem(Where + TEXT(": the weight stat can't be a difficulty stat")); }
		}
	}
	if (!WeightRow) { C.Problem(TEXT("DT_FishStat has no row for Fish.Stat.Weight")); }

	// ---- Rarities ----
	TMap<int32, FName> Ranks;
	for (const TPair<FName, const FFishRarityRow*>& Entry : RarityRows)
	{
		const FFishRarityRow& Row = *Entry.Value;
		const FString Where = FString::Printf(TEXT("DT_FishRarity row '%s'"), *Entry.Key.ToString());
		if (Row.DisplayName.IsEmptyOrWhitespace()) { C.Problem(Where + TEXT(".DisplayName is empty")); }
		if (Row.Rank < 0) { C.Problem(FString::Printf(TEXT("%s.Rank = %d must be >= 0"), *Where, Row.Rank)); }
		if (const FName* Other = Ranks.Find(Row.Rank))
		{
			C.Problem(FString::Printf(TEXT("%s: Rank %d is already used by '%s' (ranks must be unique)"), *Where, Row.Rank, *Other->ToString()));
		}
		else
		{
			Ranks.Add(Row.Rank, Entry.Key);
		}
		C.NonNegative(Row.RollWeight, Where + TEXT(".RollWeight"));
		C.Positive(Row.ValueMultiplier, Where + TEXT(".ValueMultiplier"));
		C.Positive(Row.XpMultiplier, Where + TEXT(".XpMultiplier"));
		C.CheckStatMods(Row.StatMods, Where + TEXT(".StatMods"));
	}
	{
		TArray<TPair<FName, const FFishRarityRow*>> ByRank = RarityRows;
		ByRank.StableSort([](const TPair<FName, const FFishRarityRow*>& A, const TPair<FName, const FFishRarityRow*>& B) { return A.Value->Rank < B.Value->Rank; });
		for (int32 i = 1; i < ByRank.Num(); ++i)
		{
			const FFishRarityRow& Low = *ByRank[i - 1].Value;
			const FFishRarityRow& High = *ByRank[i].Value;
			if (!(High.ValueMultiplier > Low.ValueMultiplier))
			{
				C.Problem(FString::Printf(TEXT("DT_FishRarity: ValueMultiplier must strictly increase with Rank ('%s' rank %d has %g, '%s' rank %d has %g)"),
					*ByRank[i].Key.ToString(), High.Rank, High.ValueMultiplier, *ByRank[i - 1].Key.ToString(), Low.Rank, Low.ValueMultiplier));
			}
		}
		// Among enabled tiers (RollWeight > 0) a rarer tier is never more likely; disabled tiers (0) are staged content
		ByRank.RemoveAll([](const TPair<FName, const FFishRarityRow*>& Entry) { return !(Entry.Value->RollWeight > 0.0f); });
		for (int32 i = 1; i < ByRank.Num(); ++i)
		{
			const FFishRarityRow& Low = *ByRank[i - 1].Value;
			const FFishRarityRow& High = *ByRank[i].Value;
			if (High.RollWeight > Low.RollWeight)
			{
				C.Problem(FString::Printf(TEXT("DT_FishRarity: RollWeight must not increase with Rank among enabled tiers ('%s' rank %d has %g, '%s' rank %d has %g)"),
					*ByRank[i].Key.ToString(), High.Rank, High.RollWeight, *ByRank[i - 1].Key.ToString(), Low.Rank, Low.RollWeight));
			}
		}
		// Among enabled tiers a rarer tier never gives less XP (ties allowed; non-finite values are reported by the row checks)
		for (int32 i = 1; i < ByRank.Num(); ++i)
		{
			const FFishRarityRow& Low = *ByRank[i - 1].Value;
			const FFishRarityRow& High = *ByRank[i].Value;
			if (High.XpMultiplier < Low.XpMultiplier)
			{
				C.Problem(FString::Printf(TEXT("DT_FishRarity: XpMultiplier must not decrease with Rank among enabled tiers ('%s' rank %d has %g, '%s' rank %d has %g)"),
					*ByRank[i].Key.ToString(), High.Rank, High.XpMultiplier, *ByRank[i - 1].Key.ToString(), Low.Rank, Low.XpMultiplier));
			}
		}
	}

	// ---- Species ----
	TMap<FString, FName> DisplayNames;
	for (const TPair<FName, const FFishSpeciesRow*>& Entry : SpeciesRows)
	{
		const FFishSpeciesRow& Row = *Entry.Value;
		const FString Where = FString::Printf(TEXT("DT_FishSpecies row '%s'"), *Entry.Key.ToString());
		if (Row.DisplayName.IsEmptyOrWhitespace())
		{
			C.Problem(Where + TEXT(".DisplayName is empty"));
		}
		else if (const FName* Other = DisplayNames.Find(Row.DisplayName.ToString().ToLower()))
		{
			C.Problem(FString::Printf(TEXT("%s: DisplayName '%s' is also used by '%s'"), *Where, *Row.DisplayName.ToString(), *Other->ToString()));
		}
		else
		{
			DisplayNames.Add(Row.DisplayName.ToString().ToLower(), Entry.Key);
		}
		if (Row.JournalText.IsEmptyOrWhitespace()) { C.Problem(Where + TEXT(".JournalText is empty")); }
		if (Row.BaseLevel < 1) { C.Problem(FString::Printf(TEXT("%s.BaseLevel = %d must be >= 1"), *Where, Row.BaseLevel)); }

		C.Positive(Row.WeightMin, Where + TEXT(".WeightMin"));
		if (!(Row.WeightMin <= Row.WeightMax) || !FMath::IsFinite(Row.WeightMax))
		{
			C.Problem(FString::Printf(TEXT("%s: WeightMin %g must be <= WeightMax %g"), *Where, Row.WeightMin, Row.WeightMax));
		}
		if (WeightRow && (Row.WeightMin < WeightRow->Min || Row.WeightMax > WeightRow->Max))
		{
			C.Problem(FString::Printf(TEXT("%s: weight range [%g, %g] must be inside the Fish.Stat.Weight range [%g, %g]"),
				*Where, Row.WeightMin, Row.WeightMax, WeightRow->Min, WeightRow->Max));
		}
		C.Positive(Row.ReferenceWeight, Where + TEXT(".ReferenceWeight"));
		C.Positive(Row.SizeSkew, Where + TEXT(".SizeSkew"));
		if (!FMath::IsFinite(Row.WeightStatExponent)) { C.Problem(Where + TEXT(".WeightStatExponent is not a finite number")); }

		TSet<FGameplayTag> BaseTags;
		for (int32 i = 0; i < Row.BaseStats.Num(); ++i)
		{
			const FFishStatValue& Stat = Row.BaseStats[i];
			const FString StatWhere = FString::Printf(TEXT("%s.BaseStats[%d]"), *Where, i);
			C.CheckStatTag(Stat.Tag, StatWhere);
			if (Stat.Tag.GetTagName() == FFishRoll::WeightStatName())
			{
				C.Problem(StatWhere + TEXT(": Fish.Stat.Weight comes from the weight roll, not BaseStats"));
			}
			bool bDuplicate = false;
			BaseTags.Add(Stat.Tag, &bDuplicate);
			if (bDuplicate) { C.Problem(FString::Printf(TEXT("%s: stat '%s' is listed twice"), *StatWhere, *Stat.Tag.ToString())); }
			const FFishStatRow* StatRow = Tables.FindStat(Stat.Tag);
			if (!FMath::IsFinite(Stat.Value) || (StatRow && (Stat.Value < StatRow->Min || Stat.Value > StatRow->Max)))
			{
				C.Problem(FString::Printf(TEXT("%s: value %g must be finite and inside the stat's range"), *StatWhere, Stat.Value));
			}
		}

		C.NonNegative(Row.BaseValuePerKg, Where + TEXT(".BaseValuePerKg"));
		C.NonNegative(Row.BiteWeight, Where + TEXT(".BiteWeight"));
		C.CheckTagList(Row.HabitatTags, { TEXT("Habitat") }, Where + TEXT(".HabitatTags"));
		C.CheckTagList(Row.RegionTags, { TEXT("Region") }, Where + TEXT(".RegionTags"));
		C.CheckTimeWindows(Row.TimeWindows, Where + TEXT(".TimeWindows"));
		C.CheckTagList(Row.WeatherTags, { TEXT("Weather") }, Where + TEXT(".WeatherTags"));
		C.CheckTagList(Row.AcceptedBait, { TEXT("Bait"), TEXT("Hook") }, Where + TEXT(".AcceptedBait"));
		C.CheckTagList(Row.SpeciesTags, { TEXT("Fish.Family"), TEXT("Fish.Trait") }, Where + TEXT(".SpeciesTags"));
		C.CheckIdList(Row.AllowedRarities, Where + TEXT(".AllowedRarities"), [&Tables](FName Id) { return Tables.FindRarity(Id) != nullptr; }, TEXT("DT_FishRarity"));
		C.CheckIdList(Row.AllowedModifiers, Where + TEXT(".AllowedModifiers"), [&Tables](FName Id) { return Tables.FindModifier(Id) != nullptr; }, TEXT("DT_FishModifier"));
		if (Row.MaxModifiers < 0) { C.Problem(FString::Printf(TEXT("%s.MaxModifiers = %d must be >= 0"), *Where, Row.MaxModifiers)); }
		if (Row.FightPatternId.IsNone()) { C.Problem(Where + TEXT(".FightPatternId is empty")); }

		bool bCanRoll = false;
		for (const TPair<FName, float>& Rarity : FFishRoll::GetRarityWeights(Tables, Row, 0.0f))
		{
			bCanRoll |= Rarity.Value > 0.0f;
		}
		if (!bCanRoll)
		{
			C.Problem(Where + TEXT(": can't roll any rarity (every allowed tier has RollWeight 0)"));
		}
	}

	// ---- Modifiers ----
	for (const TPair<FName, const FFishModifierRow*>& Entry : ModifierRows)
	{
		const FFishModifierRow& Row = *Entry.Value;
		const FString Where = FString::Printf(TEXT("DT_FishModifier row '%s'"), *Entry.Key.ToString());
		if (Row.DisplayName.IsEmptyOrWhitespace()) { C.Problem(Where + TEXT(".DisplayName is empty")); }
		if (!(FMath::IsFinite(Row.RollChance) && Row.RollChance >= 0.0f && Row.RollChance <= 1.0f))
		{
			C.Problem(FString::Printf(TEXT("%s.RollChance = %g must be within [0, 1]"), *Where, Row.RollChance));
		}
		C.Positive(Row.ValueMultiplier, Where + TEXT(".ValueMultiplier"));
		C.CheckStatMods(Row.StatMods, Where + TEXT(".StatMods"));
		C.CheckIdList(Row.SpeciesIds, Where + TEXT(".SpeciesIds"), [&Tables](FName Id) { return Tables.FindSpecies(Id) != nullptr; }, TEXT("DT_FishSpecies"));
		C.CheckTagList(Row.SpeciesTags, { TEXT("Fish.Family"), TEXT("Fish.Trait") }, Where + TEXT(".SpeciesTags"));
		C.CheckTagList(Row.RegionTags, { TEXT("Region") }, Where + TEXT(".RegionTags"));
		C.CheckTimeWindows(Row.TimeWindows, Where + TEXT(".TimeWindows"));
		C.CheckTagList(Row.WeatherTags, { TEXT("Weather") }, Where + TEXT(".WeatherTags"));
	}

	return Out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Raw JSON source check (types the importer would silently misread)

namespace FishValidatorPrivate
{
	static FString DescribeJson(const FJsonValue& Value)
	{
		switch (Value.Type)
		{
		case EJson::String: return FString::Printf(TEXT("text \"%s\""), *Value.AsString());
		case EJson::Number: return TEXT("a number");
		case EJson::Boolean: return TEXT("a boolean");
		case EJson::Array: return TEXT("an array");
		case EJson::Object: return TEXT("an object");
		case EJson::Null: return TEXT("null");
		default: return TEXT("nothing");
		}
	}

	static void CheckJsonObject(const FJsonObject& Object, const UStruct* Struct, const FString& Where, TArray<FString>& Out);

	static void CheckJsonValue(const FJsonValue& Value, const FProperty* Property, const FString& Where, TArray<FString>& Out)
	{
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (Value.Type == EJson::Array) // any other type is an import problem already
			{
				const TArray<TSharedPtr<FJsonValue>>& Items = Value.AsArray();
				for (int32 i = 0; i < Items.Num(); ++i)
				{
					if (Items[i].IsValid())
					{
						CheckJsonValue(*Items[i], ArrayProperty->Inner, FString::Printf(TEXT("%s[%d]"), *Where, i), Out);
					}
				}
			}
			return;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FGameplayTag::StaticStruct() || StructProperty->Struct == FGameplayTagContainer::StaticStruct())
			{
				return; // tags are written as text; Validate checks them after import
			}
			if (Value.Type != EJson::Object)
			{
				Out.Add(FString::Printf(TEXT("%s is %s: write it as a JSON object {...} (the struct text form hides its numbers from this check)"), *Where, *DescribeJson(Value)));
				return;
			}
			CheckJsonObject(*Value.AsObject(), StructProperty->Struct, Where, Out);
			return;
		}
		if (CastField<FBoolProperty>(Property))
		{
			if (Value.Type != EJson::Boolean)
			{
				Out.Add(FString::Printf(TEXT("%s is %s: it must be JSON true or false (the JSON import reads text in a bool field as false)"), *Where, *DescribeJson(Value)));
			}
			return;
		}
		const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
		if (NumericProperty && !NumericProperty->IsEnum() && Value.Type != EJson::Number)
		{
			Out.Add(FString::Printf(TEXT("%s is %s: it must be a JSON number (the JSON import reads text in an int field as 0)"), *Where, *DescribeJson(Value)));
		}
	}

	static void CheckJsonObject(const FJsonObject& Object, const UStruct* Struct, const FString& Where, TArray<FString>& Out)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const TSharedPtr<FJsonValue> Value = Object.TryGetField(It->GetName());
			if (Value.IsValid())
			{
				CheckJsonValue(*Value, *It, Where + TEXT(".") + It->GetName(), Out);
			}
		}
	}
}

TArray<FString> FFishDataValidator::ValidateJsonSource(const FString& Json, const UScriptStruct* RowStruct, const FString& TableName)
{
	using namespace FishValidatorPrivate;
	TArray<FString> Out;
	if (!RowStruct)
	{
		Out.Add(TableName + TEXT(": no row struct given"));
		return Out;
	}
	TArray<TSharedPtr<FJsonValue>> Rows;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Rows))
	{
		Out.Add(FString::Printf(TEXT("%s: the source is not a JSON array of rows (%s)"), *TableName, *Reader->GetErrorMessage()));
		return Out;
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		if (!Rows[i].IsValid() || !Rows[i]->TryGetObject(Row) || !Row || !Row->IsValid())
		{
			Out.Add(FString::Printf(TEXT("%s: row #%d is not a JSON object"), *TableName, i));
			continue;
		}
		FString Name;
		(*Row)->TryGetStringField(TEXT("Name"), Name);
		const FString Where = FString::Printf(TEXT("%s row '%s'"), *TableName, Name.IsEmpty() ? *FString::Printf(TEXT("#%d"), i) : *Name);
		CheckJsonObject(**Row, RowStruct, Where, Out);
	}
	return Out;
}

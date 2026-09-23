// QA-owned, independent data-validation tests for the T-008 fish tables (every row of every table), the production
// validator (negative fixtures), "new content is data only" (extensibility) and the settings.
// Written by the qa-engineer from docs/specs/fish-system-rules.md, Fish/FishTypes.h and Fish/FishDataValidator.h.
// The checks here are an independent re-statement of the rules, so a validator bug can't hide a data bug.

#include "FishQATestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Internationalization/Regex.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace FishQA_Data
{
	using namespace FishQA;

	static const TCHAR* const TableNames[] = { TEXT("DT_FishSpecies"), TEXT("DT_FishRarity"), TEXT("DT_FishModifier"), TEXT("DT_FishStat") };

	static UScriptStruct* RowStructFor(const FString& TableName)
	{
		// CLAUDE.md convention: data/tables/DT_<Name>.* <-> row struct F<Name>Row
		const FString StructPath = FString::Printf(TEXT("/Script/VibeGame.%sRow"), *TableName.RightChop(3));
		return FindObject<UScriptStruct>(nullptr, *StructPath);
	}

	static bool ParseJsonArray(const FString& Text, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
	}

	/** Every tag-looking token in non-text fields of a JSON source (text fields like DevComment are skipped) */
	static void CollectTagTokens(const TSharedPtr<FJsonValue>& Value, const FString& Key, TArray<FString>& Out)
	{
		static const TSet<FString> TextKeys = { TEXT("DevComment"), TEXT("Description"), TEXT("JournalText"), TEXT("DisplayName"), TEXT("Mesh"), TEXT("Name") };
		if (!Value.IsValid())
		{
			return;
		}
		switch (Value->Type)
		{
		case EJson::String:
			if (!TextKeys.Contains(Key))
			{
				const FRegexPattern Pattern(TEXT("\\b(?:Fish|Habitat|Region|Weather|Bait|Hook|Test)(?:\\.[A-Za-z0-9_]+)+"));
				FRegexMatcher Matcher(Pattern, Value->AsString());
				while (Matcher.FindNext())
				{
					Out.Add(Matcher.GetCaptureGroup(0));
				}
			}
			break;
		case EJson::Array:
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				CollectTagTokens(Item, Key, Out);
			}
			break;
		case EJson::Object:
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Value->AsObject()->Values)
			{
				CollectTagTokens(Field.Value, Field.Key, Out);
			}
			break;
		default:
			break;
		}
	}

	/** Visits every FGameplayTag inside a struct value (nested structs and arrays included) */
	static void VisitTags(const FProperty* Property, const void* Value, const FString& Path, TFunctionRef<void(const FGameplayTag&, const FString&)> Visit);

	static void VisitStructTags(const UStruct* Struct, const void* Data, const FString& Path, TFunctionRef<void(const FGameplayTag&, const FString&)> Visit)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			for (int32 i = 0; i < It->ArrayDim; ++i)
			{
				VisitTags(*It, It->ContainerPtrToValuePtr<void>(Data, i), Path + TEXT(".") + It->GetName(), Visit);
			}
		}
	}

	static void VisitTags(const FProperty* Property, const void* Value, const FString& Path, TFunctionRef<void(const FGameplayTag&, const FString&)> Visit)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FGameplayTag::StaticStruct())
			{
				Visit(*static_cast<const FGameplayTag*>(Value), Path);
			}
			else if (StructProperty->Struct == FGameplayTagContainer::StaticStruct())
			{
				for (const FGameplayTag& ContainedTag : *static_cast<const FGameplayTagContainer*>(Value))
				{
					Visit(ContainedTag, Path);
				}
			}
			else
			{
				VisitStructTags(StructProperty->Struct, Value, Path, Visit);
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, Value);
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				VisitTags(ArrayProperty->Inner, Helper.GetRawPtr(i), FString::Printf(TEXT("%s[%d]"), *Path, i), Visit);
			}
		}
	}

	static bool AnyProblemMentions(const TArray<FString>& Problems, const FString& Keyword)
	{
		return Problems.ContainsByPredicate([&Keyword](const FString& Problem) { return Keyword.IsEmpty() || Problem.Contains(Keyword); });
	}

	static bool TagUnder(const FGameplayTag& InTag, std::initializer_list<const TCHAR*> Roots)
	{
		for (const TCHAR* Root : Roots)
		{
			if (InTag.MatchesTag(FishQA::Tag(Root)))
			{
				return true;
			}
		}
		return InTag.GetTagName().ToString().StartsWith(TEXT("Test."));
	}

	static bool WindowValid(const FFishTimeWindow& W)
	{
		return FMath::IsFinite(W.StartHour) && FMath::IsFinite(W.EndHour) && W.StartHour >= 0.0f && W.StartHour <= 24.0f
			&& W.EndHour >= 0.0f && W.EndHour <= 24.0f && W.StartHour != W.EndHour;
	}

	static void AppendRow(FString& ArrayText, const FString& RowJson)
	{
		int32 Index = INDEX_NONE;
		if (ArrayText.FindLastChar(TEXT(']'), Index))
		{
			ArrayText.InsertAt(Index, TEXT(",\n") + RowJson + TEXT("\n"));
		}
	}

	static FString F(float X)
	{
		return FString::SanitizeFloat(X);
	}

	static FString QuotedList(const FString& Tag)
	{
		return Tag.IsEmpty() ? FString() : FString::Printf(TEXT("\"%s\""), *Tag);
	}

	// ================================================================================================================
	// Data validation of the real sources (QA-85..QA-99)
	// ================================================================================================================

	/** QA-85: the four sources exist where the settings say, parse with zero import problems and have rows */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataSourcesTest, "Project.Fish.QA.Data.SourcesExistAndParse", FISH_QA_FLAGS)
	bool FFishQADataSourcesTest::RunTest(const FString& Parameters)
	{
		const UFishSettings* Settings = GetDefault<UFishSettings>();
		const FSoftObjectPath Paths[] = { Settings->SpeciesTable.ToSoftObjectPath(), Settings->RarityTable.ToSoftObjectPath(), Settings->ModifierTable.ToSoftObjectPath(), Settings->StatTable.ToSoftObjectPath() };
		for (int32 i = 0; i < 4; ++i)
		{
			const FString Name = TableNames[i];
			TestEqual(FString::Printf(TEXT("settings point at %s"), *Name), Paths[i].GetAssetName(), Name);
			FString Text;
			if (!TestTrue(FString::Printf(TEXT("%s.json exists"), *Name), ReadSource(Name + TEXT(".json"), Text)))
			{
				continue;
			}
			UScriptStruct* RowStruct = RowStructFor(Name);
			if (!TestNotNull(FString::Printf(TEXT("row struct for %s"), *Name), RowStruct))
			{
				continue;
			}
			TStrongObjectPtr<UDataTable> Table;
			if (ImportJsonChecked(*this, Table, RowStruct, Text, *Name))
			{
				TestTrue(FString::Printf(TEXT("%s has rows"), *Name), Table->GetRowMap().Num() > 0);
			}
		}
		return true;
	}

	/** QA-86: every data/tables/DT_* source (future tables too) resolves its row struct by convention and parses cleanly */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataAllSourcesTest, "Project.Fish.QA.Data.AllTableSourcesParse", FISH_QA_FLAGS)
	bool FFishQADataAllSourcesTest::RunTest(const FString& Parameters)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data/tables"));
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("DT_*.*")), /*Files*/ true, /*Directories*/ false);
		int32 Checked = 0;
		for (const FString& File : Files)
		{
			const FString Extension = FPaths::GetExtension(File).ToLower();
			if (Extension != TEXT("json") && Extension != TEXT("csv"))
			{
				continue;
			}
			const FString Name = FPaths::GetBaseFilename(File);
			UScriptStruct* RowStruct = RowStructFor(Name);
			if (!RowStruct)
			{
				AddError(FString::Printf(TEXT("%s: no row struct F%sRow (CLAUDE.md DataTable naming convention)"), *File, *Name.RightChop(3)));
				continue;
			}
			FString Text;
			if (!TestTrue(FString::Printf(TEXT("read %s"), *File), FFileHelper::LoadFileToString(Text, *(Dir / File))))
			{
				continue;
			}
			TStrongObjectPtr<UDataTable> Table(NewTable(RowStruct));
			const TArray<FString> Problems = Extension == TEXT("json") ? Table->CreateTableFromJSONString(Text) : Table->CreateTableFromCSVString(Text);
			for (const FString& Problem : Problems)
			{
				AddError(FString::Printf(TEXT("%s: %s"), *File, *Problem));
			}
			++Checked;
		}
		TestTrue(FString::Printf(TEXT("at least the 4 fish tables were checked (%d)"), Checked), Checked >= 4);
		return true;
	}

	/** QA-87: row ids are unique case-insensitively; the engine import reports a duplicate */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataUniqueIdsTest, "Project.Fish.QA.Data.UniqueIds", FISH_QA_FLAGS)
	bool FFishQADataUniqueIdsTest::RunTest(const FString& Parameters)
	{
		for (const TCHAR* Name : TableNames)
		{
			FString Text;
			TArray<TSharedPtr<FJsonValue>> Rows;
			if (!ReadSource(FString(Name) + TEXT(".json"), Text) || !TestTrue(FString::Printf(TEXT("%s parses as a JSON array"), Name), ParseJsonArray(Text, Rows)))
			{
				continue;
			}
			TSet<FString> Seen;
			for (const TSharedPtr<FJsonValue>& JsonRow : Rows)
			{
				const FString Id = JsonRow->AsObject()->GetStringField(TEXT("Name"));
				TestFalse(FString::Printf(TEXT("%s: row name '%s' is set"), Name, *Id), Id.IsEmpty() || Id == TEXT("None"));
				TestFalse(FString::Printf(TEXT("%s: '%s' is unique (case-insensitive)"), Name, *Id), Seen.Contains(Id.ToLower()));
				Seen.Add(Id.ToLower());
			}
		}
		// Negative fixture: the importer itself must report a case-only duplicate
		const FString RowBody = TEXT("\"DisplayName\":\"QA\",\"Description\":\"QA\",\"RollChance\":0.5,\"ExclusivityGroup\":\"None\",\"StatMods\":[],\"ValueMultiplier\":1,\"SpeciesIds\":[],\"SpeciesTags\":[],\"RegionTags\":[],\"TimeWindows\":[],\"WeatherTags\":[],\"TintColor\":{\"R\":1,\"G\":1,\"B\":1,\"A\":0}");
		const FString Json = FString::Printf(TEXT("[{\"Name\":\"QA_Dup\",%s},{\"Name\":\"qa_dup\",%s}]"), *RowBody, *RowBody);
		TStrongObjectPtr<UDataTable> Table;
		const TArray<FString> Problems = ImportJson(Table, FFishModifierRow::StaticStruct(), Json);
		TestTrue(TEXT("import reports 'QA_Dup' vs 'qa_dup' as a duplicate"), AnyProblemMentions(Problems, TEXT("Duplicate")));
		return true;
	}

	/** QA-88: every tag in the sources is registered (raw text AND after import), and the silent-clear cases are caught */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataTagsTest, "Project.Fish.QA.Data.TagsRegistered", FISH_QA_FLAGS)
	bool FFishQADataTagsTest::RunTest(const FString& Parameters)
	{
		// Raw text scan
		int32 Tokens = 0;
		for (const TCHAR* Name : TableNames)
		{
			FString Text;
			TArray<TSharedPtr<FJsonValue>> Rows;
			if (!ReadSource(FString(Name) + TEXT(".json"), Text) || !ParseJsonArray(Text, Rows))
			{
				AddError(FString::Printf(TEXT("can't parse %s"), Name));
				continue;
			}
			TArray<FString> Found;
			for (const TSharedPtr<FJsonValue>& JsonRow : Rows)
			{
				CollectTagTokens(JsonRow, TEXT(""), Found);
			}
			for (const FString& Token : Found)
			{
				TestTrue(FString::Printf(TEXT("%s: tag '%s' is registered"), Name, *Token), IsRegisteredTag(FName(*Token)));
				++Tokens;
			}
		}
		TestTrue(FString::Printf(TEXT("the scan found tags (%d)"), Tokens), Tokens > 10);

		// After import: every FGameplayTag in every row is set and registered
		FTables Real;
		if (LoadReal(*this, Real))
		{
			const UDataTable* Tables[] = { Real.Species.Get(), Real.Rarities.Get(), Real.Modifiers.Get(), Real.Stats.Get() };
			for (const UDataTable* Table : Tables)
			{
				for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
				{
					VisitStructTags(Table->RowStruct, Pair.Value, Table->GetName() + TEXT(":") + Pair.Key.ToString(), [this](const FGameplayTag& Found, const FString& Path)
					{
						TestTrue(FString::Printf(TEXT("%s is a registered, non-empty tag ('%s')"), *Path, *Found.ToString()), Found.IsValid() && IsRegisteredTag(Found.GetTagName()));
					});
				}
			}
		}

		// Negative fixtures on a copy of the species source
		FString Species;
		if (ReadSource(TEXT("DT_FishSpecies.json"), Species))
		{
			const FString Original = TEXT("\"HabitatTags\": [ \"Habitat.Shore\", \"Habitat.Lagoon\" ]");
			if (!TestTrue(TEXT("fixture anchor found in DT_FishSpecies.json"), Species.Contains(Original)))
			{
				return true;
			}
			// (a) plain string form: the importer reports it
			{
				TStrongObjectPtr<UDataTable> Table;
				const TArray<FString> Problems = ImportJson(Table, FFishSpeciesRow::StaticStruct(), Species.Replace(*Original, TEXT("\"HabitatTags\": [ \"Habitat.QA_Unregistered\" ]")));
				TestTrue(TEXT("(a) an unregistered tag string is an import problem"), Problems.Num() > 0);
			}
			// (b) object form {"TagName": ...}: imported without a problem, so the validator must catch it
			// (c) struct-text form "(TagName=...)": silently imported as an empty tag, so the validator must catch that too
			const TPair<const TCHAR*, const TCHAR*> Forms[] = {
				TPair<const TCHAR*, const TCHAR*>(TEXT("(b) object form"), TEXT("\"HabitatTags\": [ { \"TagName\": \"Habitat.QA_Unregistered\" } ]")),
				TPair<const TCHAR*, const TCHAR*>(TEXT("(c) struct-text form"), TEXT("\"HabitatTags\": [ \"(TagName=\\\"Habitat.QA_Unregistered\\\")\" ]")),
			};
			for (const TPair<const TCHAR*, const TCHAR*>& Form : Forms)
			{
				FTables Broken;
				if (!LoadReal(*this, Broken))
				{
					continue;
				}
				const TArray<FString> ImportProblems = ImportJson(Broken.Species, FFishSpeciesRow::StaticStruct(), Species.Replace(*Original, Form.Value));
				bool bWalkFlags = false;
				for (const TPair<FName, uint8*>& Pair : Broken.Species->GetRowMap())
				{
					VisitStructTags(Broken.Species->RowStruct, Pair.Value, Pair.Key.ToString(), [&bWalkFlags](const FGameplayTag& Found, const FString&)
					{
						bWalkFlags |= !(Found.IsValid() && IsRegisteredTag(Found.GetTagName()));
					});
				}
				const TArray<FString> Problems = FFishDataValidator::Validate(Broken.Get());
				AddInfo(FString::Printf(TEXT("%s: %d import problems, validator %d problems"), Form.Key, ImportProblems.Num(), Problems.Num()));
				TestTrue(FString::Printf(TEXT("%s: caught by the import or by QA's reflection walk"), Form.Key), ImportProblems.Num() > 0 || bWalkFlags);
				TestTrue(FString::Printf(TEXT("%s: caught by the import or by FFishDataValidator (naming Bonefish)"), Form.Key), ImportProblems.Num() > 0 || AnyProblemMentions(Problems, TEXT("Bonefish")));
			}
		}
		return true;
	}

	/** QA-89: tags live in their categories (stats under Fish.Stat, habitats under Habitat, ...); Test.* fixtures are exempt */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataNamespacesTest, "Project.Fish.QA.Data.TagNamespaces", FISH_QA_FLAGS)
	bool FFishQADataNamespacesTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		auto Check = [this](const FString& Where, const TArray<FGameplayTag>& Tags, std::initializer_list<const TCHAR*> Roots)
		{
			for (const FGameplayTag& Listed : Tags)
			{
				TestTrue(FString::Printf(TEXT("%s: %s in its category"), *Where, *Listed.ToString()), TagUnder(Listed, Roots));
			}
		};
		auto StatTags = [](const TArray<FFishStatMod>& Mods)
		{
			TArray<FGameplayTag> Tags;
			for (const FFishStatMod& StatMod : Mods)
			{
				Tags.Add(StatMod.StatTag);
			}
			return Tags;
		};
		Real.Stats->ForeachRow<FFishStatRow>(TEXT("QA"), [&](const FName& Id, const FFishStatRow& Stat) { Check(TEXT("stat ") + Id.ToString(), { Stat.Tag }, { TEXT("Fish.Stat") }); });
		Real.Species->ForeachRow<FFishSpeciesRow>(TEXT("QA"), [&](const FName& Id, const FFishSpeciesRow& Species)
		{
			TArray<FGameplayTag> Base;
			for (const FFishStatValue& Stat : Species.BaseStats)
			{
				Base.Add(Stat.Tag);
			}
			Check(Id.ToString() + TEXT(".BaseStats"), Base, { TEXT("Fish.Stat") });
			Check(Id.ToString() + TEXT(".HabitatTags"), Species.HabitatTags, { TEXT("Habitat") });
			Check(Id.ToString() + TEXT(".RegionTags"), Species.RegionTags, { TEXT("Region") });
			Check(Id.ToString() + TEXT(".WeatherTags"), Species.WeatherTags, { TEXT("Weather") });
			Check(Id.ToString() + TEXT(".AcceptedBait"), Species.AcceptedBait, { TEXT("Bait"), TEXT("Hook") });
			Check(Id.ToString() + TEXT(".SpeciesTags"), Species.SpeciesTags, { TEXT("Fish.Family"), TEXT("Fish.Trait") });
		});
		Real.Rarities->ForeachRow<FFishRarityRow>(TEXT("QA"), [&](const FName& Id, const FFishRarityRow& Rarity) { Check(Id.ToString() + TEXT(".StatMods"), StatTags(Rarity.StatMods), { TEXT("Fish.Stat") }); });
		Real.Modifiers->ForeachRow<FFishModifierRow>(TEXT("QA"), [&](const FName& Id, const FFishModifierRow& Modifier)
		{
			Check(Id.ToString() + TEXT(".StatMods"), StatTags(Modifier.StatMods), { TEXT("Fish.Stat") });
			Check(Id.ToString() + TEXT(".SpeciesTags"), Modifier.SpeciesTags, { TEXT("Fish.Family"), TEXT("Fish.Trait") });
			Check(Id.ToString() + TEXT(".RegionTags"), Modifier.RegionTags, { TEXT("Region") });
			Check(Id.ToString() + TEXT(".WeatherTags"), Modifier.WeatherTags, { TEXT("Weather") });
		});
		return true;
	}

	/** QA-90: every id and stat referenced by a row exists (allow-lists, modifier species conditions, stat rows) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataReferencesTest, "Project.Fish.QA.Data.ReferencesResolve", FISH_QA_FLAGS)
	bool FFishQADataReferencesTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TSet<FName> StatTags;
		Real.Stats->ForeachRow<FFishStatRow>(TEXT("QA"), [&](const FName&, const FFishStatRow& Stat) { StatTags.Add(Stat.Tag.GetTagName()); });
		TestTrue(TEXT("DT_FishStat has Fish.Stat.Weight"), StatTags.Contains(FName(TEXT("Fish.Stat.Weight"))));
		auto CheckMods = [&](const FString& Where, const TArray<FFishStatMod>& Mods)
		{
			for (const FFishStatMod& StatMod : Mods)
			{
				TestTrue(FString::Printf(TEXT("%s: stat %s has a DT_FishStat row"), *Where, *StatMod.StatTag.ToString()), StatTags.Contains(StatMod.StatTag.GetTagName()));
			}
		};
		Real.Species->ForeachRow<FFishSpeciesRow>(TEXT("QA"), [&](const FName& Id, const FFishSpeciesRow& Species)
		{
			for (const FName& Rarity : Species.AllowedRarities)
			{
				TestNotNull(FString::Printf(TEXT("%s.AllowedRarities: %s exists"), *Id.ToString(), *Rarity.ToString()), Row<FFishRarityRow>(Real.Rarities, Rarity));
			}
			for (const FName& Modifier : Species.AllowedModifiers)
			{
				TestNotNull(FString::Printf(TEXT("%s.AllowedModifiers: %s exists"), *Id.ToString(), *Modifier.ToString()), Row<FFishModifierRow>(Real.Modifiers, Modifier));
			}
			TestEqual(FString::Printf(TEXT("%s: allow-lists have no duplicates"), *Id.ToString()),
				TSet<FName>(Species.AllowedRarities).Num() + TSet<FName>(Species.AllowedModifiers).Num(), Species.AllowedRarities.Num() + Species.AllowedModifiers.Num());
			for (const FFishStatValue& Stat : Species.BaseStats)
			{
				TestTrue(FString::Printf(TEXT("%s: base stat %s has a DT_FishStat row"), *Id.ToString(), *Stat.Tag.ToString()), StatTags.Contains(Stat.Tag.GetTagName()));
			}
		});
		Real.Rarities->ForeachRow<FFishRarityRow>(TEXT("QA"), [&](const FName& Id, const FFishRarityRow& Rarity) { CheckMods(Id.ToString(), Rarity.StatMods); });
		Real.Modifiers->ForeachRow<FFishModifierRow>(TEXT("QA"), [&](const FName& Id, const FFishModifierRow& Modifier)
		{
			CheckMods(Id.ToString(), Modifier.StatMods);
			for (const FName& SpeciesId : Modifier.SpeciesIds)
			{
				TestNotNull(FString::Printf(TEXT("%s.SpeciesIds: %s exists"), *Id.ToString(), *SpeciesId.ToString()), Row<FFishSpeciesRow>(Real.Species, SpeciesId));
			}
		});
		return true;
	}

	/** QA-91/93: every number in every row is in its documented range (weights, chances, multipliers, windows, levels) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataRangesTest, "Project.Fish.QA.Data.RangesSane", FISH_QA_FLAGS)
	bool FFishQADataRangesTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		const FFishStatRow* WeightStat = nullptr;
		TMap<FName, const FFishStatRow*> Stats;
		Real.Stats->ForeachRow<FFishStatRow>(TEXT("QA"), [&](const FName& Id, const FFishStatRow& Stat)
		{
			const FString W = TEXT("stat ") + Id.ToString();
			TestTrue(W + TEXT(": finite Min/Default/Max"), FMath::IsFinite(Stat.Min) && FMath::IsFinite(Stat.Default) && FMath::IsFinite(Stat.Max));
			TestTrue(W + TEXT(": Min <= Default <= Max"), Stat.Min <= Stat.Default && Stat.Default <= Stat.Max);
			TestFalse(W + TEXT(": display name"), Stat.DisplayName.IsEmpty());
			TestFalse(W + TEXT(": unique tag"), Stats.Contains(Stat.Tag.GetTagName()));
			Stats.Add(Stat.Tag.GetTagName(), &Stat);
			if (Stat.Tag.GetTagName() == FName(TEXT("Fish.Stat.Weight")))
			{
				WeightStat = &Stat;
			}
		});
		if (!TestNotNull(TEXT("Weight stat row"), WeightStat))
		{
			return false;
		}
		TestTrue(TEXT("Weight stat Min > 0"), WeightStat->Min > 0.0f);
		TestFalse(TEXT("Weight is not a difficulty stat"), WeightStat->bIsDifficultyStat);

		auto CheckMods = [&](const FString& Where, const TArray<FFishStatMod>& Mods)
		{
			for (const FFishStatMod& StatMod : Mods)
			{
				TestTrue(FString::Printf(TEXT("%s: %s finite"), *Where, *StatMod.StatTag.ToString()), FMath::IsFinite(StatMod.Value));
				if (StatMod.Op == EFishStatModOp::Multiply)
				{
					TestTrue(FString::Printf(TEXT("%s: %s Multiply > 0"), *Where, *StatMod.StatTag.ToString()), StatMod.Value > 0.0f);
				}
			}
		};
		Real.Species->ForeachRow<FFishSpeciesRow>(TEXT("QA"), [&](const FName& Id, const FFishSpeciesRow& S)
		{
			const FString W = Id.ToString();
			TestFalse(W + TEXT(": DisplayName"), S.DisplayName.IsEmpty());
			TestFalse(W + TEXT(": JournalText"), S.JournalText.IsEmpty());
			TestTrue(W + TEXT(": BaseLevel >= 1"), S.BaseLevel >= 1);
			TestTrue(W + TEXT(": 0 < WeightMin <= WeightMax"), S.WeightMin > 0.0f && S.WeightMin <= S.WeightMax);
			TestTrue(W + TEXT(": weight range inside the Weight stat range"), S.WeightMin >= WeightStat->Min && S.WeightMax <= WeightStat->Max);
			TestTrue(W + TEXT(": ReferenceWeight > 0"), S.ReferenceWeight > 0.0f);
			TestTrue(W + TEXT(": SizeSkew > 0"), S.SizeSkew > 0.0f && FMath::IsFinite(S.SizeSkew));
			TestTrue(W + TEXT(": WeightStatExponent finite"), FMath::IsFinite(S.WeightStatExponent));
			TestTrue(W + TEXT(": BaseValuePerKg >= 0"), S.BaseValuePerKg >= 0.0f && FMath::IsFinite(S.BaseValuePerKg));
			TestTrue(W + TEXT(": BiteWeight >= 0"), S.BiteWeight >= 0.0f && FMath::IsFinite(S.BiteWeight));
			TestTrue(W + TEXT(": MaxModifiers >= 0"), S.MaxModifiers >= 0);
			TestFalse(W + TEXT(": FightPatternId set"), S.FightPatternId.IsNone());
			TSet<FName> Seen;
			for (const FFishStatValue& Base : S.BaseStats)
			{
				const FFishStatRow* const* Stat = Stats.Find(Base.Tag.GetTagName());
				TestFalse(W + TEXT(": base stat is not Weight"), Base.Tag.GetTagName() == FName(TEXT("Fish.Stat.Weight")));
				TestFalse(W + TEXT(": no duplicate base stats"), Seen.Contains(Base.Tag.GetTagName()));
				Seen.Add(Base.Tag.GetTagName());
				TestTrue(FString::Printf(TEXT("%s: base %s within its stat range"), *W, *Base.Tag.ToString()), Stat && Base.Value >= (*Stat)->Min && Base.Value <= (*Stat)->Max);
			}
			for (const FFishTimeWindow& Window : S.TimeWindows)
			{
				TestTrue(FString::Printf(TEXT("%s: window [%g, %g) valid"), *W, Window.StartHour, Window.EndHour), WindowValid(Window));
			}
		});
		TSet<int32> Ranks;
		Real.Rarities->ForeachRow<FFishRarityRow>(TEXT("QA"), [&](const FName& Id, const FFishRarityRow& R)
		{
			const FString W = Id.ToString();
			TestFalse(W + TEXT(": DisplayName"), R.DisplayName.IsEmpty());
			TestTrue(W + TEXT(": Rank >= 0"), R.Rank >= 0);
			TestFalse(W + TEXT(": unique Rank"), Ranks.Contains(R.Rank));
			Ranks.Add(R.Rank);
			TestTrue(W + TEXT(": RollWeight finite and >= 0"), FMath::IsFinite(R.RollWeight) && R.RollWeight >= 0.0f);
			TestTrue(W + TEXT(": ValueMultiplier > 0"), R.ValueMultiplier > 0.0f);
			TestTrue(W + TEXT(": XpMultiplier > 0"), R.XpMultiplier > 0.0f);
			CheckMods(W, R.StatMods);
		});
		Real.Modifiers->ForeachRow<FFishModifierRow>(TEXT("QA"), [&](const FName& Id, const FFishModifierRow& M)
		{
			const FString W = Id.ToString();
			TestFalse(W + TEXT(": DisplayName"), M.DisplayName.IsEmpty());
			TestTrue(W + TEXT(": RollChance in [0, 1]"), M.RollChance >= 0.0f && M.RollChance <= 1.0f);
			TestTrue(W + TEXT(": ValueMultiplier > 0"), M.ValueMultiplier > 0.0f);
			CheckMods(W, M.StatMods);
			for (const FFishTimeWindow& Window : M.TimeWindows)
			{
				TestTrue(W + TEXT(": window valid"), WindowValid(Window));
			}
		});
		return true;
	}

	/** QA-92: every species can roll at least one enabled tier */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataCanRollTest, "Project.Fish.QA.Data.EverySpeciesCanRoll", FISH_QA_FLAGS)
	bool FFishQADataCanRollTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		Real.Species->ForeachRow<FFishSpeciesRow>(TEXT("QA"), [&](const FName& Id, const FFishSpeciesRow& S)
		{
			bool bAny = false;
			Real.Rarities->ForeachRow<FFishRarityRow>(TEXT("QA"), [&](const FName& Tier, const FFishRarityRow& R)
			{
				bAny |= (S.AllowedRarities.Num() == 0 || S.AllowedRarities.Contains(Tier)) && R.RollWeight > 0.0f;
			});
			TestTrue(FString::Printf(TEXT("%s can roll an enabled tier"), *Id.ToString()), bAny);
		});
		return true;
	}

	/** QA-94: rarity order: ValueMultiplier strictly rises with Rank; among enabled tiers RollWeight never rises with Rank */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataRarityOrderTest, "Project.Fish.QA.Data.RarityOrderConsistent", FISH_QA_FLAGS)
	bool FFishQADataRarityOrderTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TArray<const FFishRarityRow*> Tiers;
		TArray<FName> Ids;
		Real.Rarities->ForeachRow<FFishRarityRow>(TEXT("QA"), [&](const FName& Id, const FFishRarityRow& R) { Tiers.Add(&R); Ids.Add(Id); });
		TArray<int32> Order;
		for (int32 i = 0; i < Tiers.Num(); ++i)
		{
			Order.Add(i);
		}
		Order.Sort([&Tiers](int32 A, int32 B) { return Tiers[A]->Rank < Tiers[B]->Rank; });
		const FFishRarityRow* PreviousEnabled = nullptr;
		for (int32 k = 1; k < Order.Num(); ++k)
		{
			const FFishRarityRow* Lower = Tiers[Order[k - 1]];
			const FFishRarityRow* Higher = Tiers[Order[k]];
			TestTrue(FString::Printf(TEXT("%s ValueMultiplier %g > %s %g"), *Ids[Order[k]].ToString(), Higher->ValueMultiplier, *Ids[Order[k - 1]].ToString(), Lower->ValueMultiplier), Higher->ValueMultiplier > Lower->ValueMultiplier);
		}
		for (int32 k = 0; k < Order.Num(); ++k)
		{
			const FFishRarityRow* Tier = Tiers[Order[k]];
			if (Tier->RollWeight > 0.0f)
			{
				if (PreviousEnabled)
				{
					TestTrue(FString::Printf(TEXT("%s RollWeight %g <= the previous enabled tier's %g"), *Ids[Order[k]].ToString(), Tier->RollWeight, PreviousEnabled->RollWeight), Tier->RollWeight <= PreviousEnabled->RollWeight);
				}
				PreviousEnabled = Tier;
			}
		}
		return true;
	}

	/** QA-96 (P2): exclusivity groups are shared by 2+ modifiers (a single-use group is usually a typo; warning only) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataGroupsTest, "Project.Fish.QA.Data.ExclusivityGroupsMeaningful", FISH_QA_FLAGS)
	bool FFishQADataGroupsTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TMap<FName, int32> Groups;
		Real.Modifiers->ForeachRow<FFishModifierRow>(TEXT("QA"), [&](const FName&, const FFishModifierRow& M)
		{
			if (!M.ExclusivityGroup.IsNone())
			{
				Groups.FindOrAdd(M.ExclusivityGroup)++;
			}
		});
		for (const TPair<FName, int32>& Group : Groups)
		{
			if (Group.Value < 2)
			{
				AddWarning(FString::Printf(TEXT("exclusivity group '%s' has only one modifier (typo?)"), *Group.Key.ToString()));
			}
		}
		return true;
	}

	/** QA-97 (P2): journal info is complete and display names are unique */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataJournalTest, "Project.Fish.QA.Data.JournalInfoComplete", FISH_QA_FLAGS)
	bool FFishQADataJournalTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TSet<FString> Names;
		int32 NoMesh = 0;
		Real.Species->ForeachRow<FFishSpeciesRow>(TEXT("QA"), [&](const FName& Id, const FFishSpeciesRow& S)
		{
			TestFalse(FString::Printf(TEXT("%s: DisplayName"), *Id.ToString()), S.DisplayName.IsEmpty());
			TestFalse(FString::Printf(TEXT("%s: JournalText"), *Id.ToString()), S.JournalText.IsEmpty());
			TestFalse(FString::Printf(TEXT("%s: display name '%s' unique"), *Id.ToString(), *S.DisplayName.ToString()), Names.Contains(S.DisplayName.ToString()));
			Names.Add(S.DisplayName.ToString());
			NoMesh += S.Mesh.IsNull() ? 1 : 0;
		});
		if (NoMesh > 0)
		{
			AddInfo(FString::Printf(TEXT("%d species have no mesh yet (allowed in lanes; the model-artist's meshes come later)"), NoMesh));
		}
		return true;
	}

	/** QA-98: T-008 starter content (update at T-009): 2 species; Common/Uncommon/Rare enabled; Epic/Legendary staged at 0; Heavy/Feisty/Giant/Albino */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataStarterTest, "Project.Fish.QA.Data.StarterContentPresent", FISH_QA_FLAGS)
	bool FFishQADataStarterTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TestEqual(TEXT("2 species"), Real.Species->GetRowMap().Num(), 2);
		for (const TCHAR* Tier : { TEXT("Common"), TEXT("Uncommon"), TEXT("Rare") })
		{
			const FFishRarityRow* R = Row<FFishRarityRow>(Real.Rarities, Tier);
			TestTrue(FString::Printf(TEXT("%s exists and is enabled"), Tier), R && R->RollWeight > 0.0f);
		}
		for (const TCHAR* Tier : { TEXT("Epic"), TEXT("Legendary") })
		{
			const FFishRarityRow* R = Row<FFishRarityRow>(Real.Rarities, Tier);
			TestTrue(FString::Printf(TEXT("%s exists and is staged (RollWeight 0)"), Tier), R && R->RollWeight == 0.0f);
		}
		for (const TCHAR* Modifier : { TEXT("Heavy"), TEXT("Feisty"), TEXT("Giant"), TEXT("Albino") })
		{
			TestNotNull(FString::Printf(TEXT("modifier %s"), Modifier), Row<FFishModifierRow>(Real.Modifiers, Modifier));
		}
		return true;
	}

	/** QA-99 (P2): when the binary /Game/Data assets exist (main checkout), their rows equal the sources */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataAssetTest, "Project.Fish.QA.Data.AssetMatchesSource", FISH_QA_FLAGS)
	bool FFishQADataAssetTest::RunTest(const FString& Parameters)
	{
		for (const TCHAR* Name : TableNames)
		{
			const FString Package = FString::Printf(TEXT("/Game/Data/%s"), Name);
			if (!FPackageName::DoesPackageExist(Package))
			{
				AddInfo(FString::Printf(TEXT("%s not imported in this checkout (normal in lanes); comparison skipped"), *Package));
				continue;
			}
			const UDataTable* Asset = LoadObject<UDataTable>(nullptr, *FString::Printf(TEXT("%s.%s"), *Package, Name));
			FString Text;
			TStrongObjectPtr<UDataTable> Source;
			if (!TestNotNull(FString::Printf(TEXT("load %s"), *Package), Asset) || !ReadSource(FString(Name) + TEXT(".json"), Text)
				|| !ImportJsonChecked(*this, Source, RowStructFor(Name), Text, Name))
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("%s: the asset's row struct matches"), Name), Asset->RowStruct == Source->RowStruct);
			TestEqual(FString::Printf(TEXT("%s: the asset matches data/tables/%s.json (reimport it if not)"), Name, Name),
				Asset->GetTableAsJSON(), Source->GetTableAsJSON());
		}
		return true;
	}

	/** QA-100: the production validator passes the starter data and catches every class of bad row (negative fixtures) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQADataValidatorTest, "Project.Fish.QA.Data.ValidatorAgrees", FISH_QA_FLAGS)
	bool FFishQADataValidatorTest::RunTest(const FString& Parameters)
	{
		{
			FTables Real;
			if (!LoadReal(*this, Real))
			{
				return false;
			}
			const TArray<FString> Problems = FFishDataValidator::Validate(Real.Get());
			for (const FString& Problem : Problems)
			{
				AddError(TEXT("starter data: ") + Problem);
			}
		}

		const float NaN = std::numeric_limits<float>::quiet_NaN();
		auto Sp = [](FTables& T) { return Row<FFishSpeciesRow>(T.Species, TEXT("Bonefish")); };
		auto Ra = [](FTables& T, const TCHAR* Id) { return Row<FFishRarityRow>(T.Rarities, Id); };
		auto Mo = [](FTables& T) { return Row<FFishModifierRow>(T.Modifiers, TEXT("Feisty")); };
		auto St = [](FTables& T, const TCHAR* Id) { return T.Stats->FindRow<FFishStatRow>(Id, TEXT("QA")); };
		struct FCase { const TCHAR* What; const TCHAR* Keyword; TFunction<void(FTables&)> Break; };
		const TArray<FCase> Bad = {
			{ TEXT("species WeightMin > WeightMax"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->WeightMin = 5.0f; } },
			{ TEXT("species WeightMin 0"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->WeightMin = 0.0f; } },
			{ TEXT("species WeightMax past the Weight stat Max"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->WeightMax = 5000.0f; } },
			{ TEXT("species BaseLevel 0"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->BaseLevel = 0; } },
			{ TEXT("species ReferenceWeight 0"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->ReferenceWeight = 0.0f; } },
			{ TEXT("species SizeSkew 0"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->SizeSkew = 0.0f; } },
			{ TEXT("species WeightStatExponent NaN"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->WeightStatExponent = NaN; } },
			{ TEXT("species base stat with no DT_FishStat row"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->BaseStats.Add(FFishStatValue(Tag(QAStat), 1.0f)); } },
			{ TEXT("species base stat Weight"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->BaseStats.Add(FFishStatValue(Tag(TEXT("Fish.Stat.Weight")), 1.0f)); } },
			{ TEXT("species duplicate base stat"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->BaseStats.Add(FFishStatValue(Tag(TEXT("Fish.Stat.Strength")), 3.0f)); } },
			{ TEXT("species base stat past its Max"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->BaseStats[0].Value = 5000.0f; } },
			{ TEXT("species BaseValuePerKg -1"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->BaseValuePerKg = -1.0f; } },
			{ TEXT("species BiteWeight -1"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->BiteWeight = -1.0f; } },
			{ TEXT("species window Start == End"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->TimeWindows[0].StartHour = 5.0f; Sp(T)->TimeWindows[0].EndHour = 5.0f; } },
			{ TEXT("species window past 24"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->TimeWindows[0].EndHour = 25.0f; } },
			{ TEXT("species habitat tag in the wrong category"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->HabitatTags = { Tag(TEXT("Region.Tropical")) }; } },
			{ TEXT("species bait tag in the wrong category"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->AcceptedBait = { Tag(TEXT("Habitat.Shore")) }; } },
			{ TEXT("species empty tag in a list"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->HabitatTags.Add(FGameplayTag()); } },
			{ TEXT("species unknown allowed rarity"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->AllowedRarities = { TEXT("QA_Nope") }; } },
			{ TEXT("species unknown allowed modifier"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->AllowedModifiers = { TEXT("QA_Nope") }; } },
			{ TEXT("species duplicate allowed rarity"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->AllowedRarities = { TEXT("Common"), TEXT("Common") }; } },
			{ TEXT("species MaxModifiers -1"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->MaxModifiers = -1; } },
			{ TEXT("species FightPatternId None"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->FightPatternId = NAME_None; } },
			{ TEXT("species DisplayName empty"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->DisplayName = FText::GetEmpty(); } },
			{ TEXT("species can only roll staged tiers"), TEXT("Bonefish"), [&](FTables& T) { Sp(T)->AllowedRarities = { TEXT("Epic"), TEXT("Legendary") }; } },
			{ TEXT("rarity duplicate Rank"), TEXT("Rank"), [&](FTables& T) { Ra(T, TEXT("Uncommon"))->Rank = 0; } },
			{ TEXT("rarity ValueMultiplier not rising"), TEXT("ValueMultiplier"), [&](FTables& T) { Ra(T, TEXT("Rare"))->ValueMultiplier = 1.2f; } },
			{ TEXT("rarity enabled RollWeight rising"), TEXT("RollWeight"), [&](FTables& T) { Ra(T, TEXT("Rare"))->RollWeight = 50.0f; } },
			{ TEXT("rarity RollWeight -1"), TEXT("Common"), [&](FTables& T) { Ra(T, TEXT("Common"))->RollWeight = -1.0f; } },
			{ TEXT("rarity RollWeight NaN"), TEXT("Common"), [&](FTables& T) { Ra(T, TEXT("Common"))->RollWeight = NaN; } },
			{ TEXT("rarity ValueMultiplier 0"), TEXT("Common"), [&](FTables& T) { Ra(T, TEXT("Common"))->ValueMultiplier = 0.0f; } },
			{ TEXT("rarity XpMultiplier 0"), TEXT("Common"), [&](FTables& T) { Ra(T, TEXT("Common"))->XpMultiplier = 0.0f; } },
			{ TEXT("rarity Rank -1"), TEXT("Common"), [&](FTables& T) { Ra(T, TEXT("Common"))->Rank = -1; } },
			{ TEXT("rarity StatMod Multiply 0"), TEXT("Uncommon"), [&](FTables& T) { Ra(T, TEXT("Uncommon"))->StatMods[0].Value = 0.0f; } },
			{ TEXT("rarity StatMod on a stat with no row"), TEXT("Uncommon"), [&](FTables& T) { Ra(T, TEXT("Uncommon"))->StatMods.Add(MulMod(QAStat, 2.0f)); } },
			{ TEXT("rarity DisplayName empty"), TEXT("Rare"), [&](FTables& T) { Ra(T, TEXT("Rare"))->DisplayName = FText::GetEmpty(); } },
			{ TEXT("every tier disabled"), TEXT(""), [&](FTables& T) { for (const TCHAR* Id : { TEXT("Common"), TEXT("Uncommon"), TEXT("Rare") }) { Ra(T, Id)->RollWeight = 0.0f; } } },
			{ TEXT("modifier RollChance 1.5"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->RollChance = 1.5f; } },
			{ TEXT("modifier RollChance -0.1"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->RollChance = -0.1f; } },
			{ TEXT("modifier ValueMultiplier 0"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->ValueMultiplier = 0.0f; } },
			{ TEXT("modifier Multiply 0"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->StatMods.Add(MulMod(TEXT("Fish.Stat.Speed"), 0.0f)); } },
			{ TEXT("modifier Multiply -2"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->StatMods.Add(MulMod(TEXT("Fish.Stat.Speed"), -2.0f)); } },
			{ TEXT("modifier Add NaN"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->StatMods.Add(AddMod(TEXT("Fish.Stat.Speed"), NaN)); } },
			{ TEXT("modifier stat with no row"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->StatMods.Add(AddMod(QAStat, 1.0f)); } },
			{ TEXT("modifier unknown species id"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->SpeciesIds = { TEXT("QA_Nope") }; } },
			{ TEXT("modifier region tag in the wrong category"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->RegionTags = { Tag(TEXT("Weather.Rain")) }; } },
			{ TEXT("modifier species tag in the wrong category"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->SpeciesTags = { Tag(TEXT("Habitat.Reef")) }; } },
			{ TEXT("modifier window Start == End"), TEXT("Feisty"), [&](FTables& T) { FFishTimeWindow W; W.StartHour = 3.0f; W.EndHour = 3.0f; Mo(T)->TimeWindows = { W }; } },
			{ TEXT("modifier DisplayName empty"), TEXT("Feisty"), [&](FTables& T) { Mo(T)->DisplayName = FText::GetEmpty(); } },
			{ TEXT("stat Min > Default"), TEXT("Strength"), [&](FTables& T) { St(T, TEXT("Strength"))->Min = 10.0f; } },
			{ TEXT("stat Max < Min"), TEXT("Strength"), [&](FTables& T) { St(T, TEXT("Strength"))->Max = -1.0f; } },
			{ TEXT("no Weight stat row"), TEXT("Weight"), [&](FTables& T) { T.Stats->RemoveRow(TEXT("Weight")); } },
			{ TEXT("Weight marked as a difficulty stat"), TEXT("Weight"), [&](FTables& T) { St(T, TEXT("Weight"))->bIsDifficultyStat = true; } },
			{ TEXT("Weight stat Min 0"), TEXT("Weight"), [&](FTables& T) { St(T, TEXT("Weight"))->Min = 0.0f; } },
			{ TEXT("two stat rows with the same tag"), TEXT("Strength"), [&](FTables& T) { T.Stats->AddRow(TEXT("Strength2"), MakeStat(TEXT("Fish.Stat.Strength"), 0.0f, 10.0f, 0.0f, true)); } },
			{ TEXT("stat tag outside Fish.Stat"), TEXT(""), [&](FTables& T) { T.Stats->AddRow(TEXT("QA_Odd"), MakeStat(TEXT("Habitat.Shore"), 0.0f, 10.0f, 0.0f, false)); } },
			{ TEXT("empty species table"), TEXT(""), [&](FTables& T) { T.Species.Reset(NewTable(FFishSpeciesRow::StaticStruct())); } },
			{ TEXT("empty rarity table"), TEXT(""), [&](FTables& T) { T.Rarities.Reset(NewTable(FFishRarityRow::StaticStruct())); } },
			{ TEXT("empty stat table"), TEXT(""), [&](FTables& T) { T.Stats.Reset(NewTable(FFishStatRow::StaticStruct())); } },
			{ TEXT("species table has the wrong row struct"), TEXT(""), [&](FTables& T) { T.Species.Reset(NewTable(FFishRarityRow::StaticStruct())); } },
		};
		for (const FCase& Case : Bad)
		{
			FTables T;
			if (!LoadReal(*this, T))
			{
				return false;
			}
			Case.Break(T);
			const TArray<FString> Problems = FFishDataValidator::Validate(T.Get());
			TestTrue(FString::Printf(TEXT("validator flags: %s (expects a problem mentioning '%s'; got %d: %s)"), Case.What, Case.Keyword, Problems.Num(),
				Problems.Num() > 0 ? *Problems[0] : TEXT("-")), AnyProblemMentions(Problems, Case.Keyword));
		}

		// Null tables: problems, never a crash
		{
			FFishTables Null;
			TestTrue(TEXT("all tables null: problems reported"), FFishDataValidator::Validate(Null).Num() > 0);
		}

		// Legal edge cases must stay clean
		struct FGood { const TCHAR* What; TFunction<void(FTables&)> Change; };
		const TArray<FGood> Good = {
			{ TEXT("Test.* tags skip the category check"), [](FTables& T)
				{
					FFishSpeciesRow* S = Row<FFishSpeciesRow>(T.Species, TEXT("Bonefish"));
					S->HabitatTags = { Tag(TEXT("Test.Fish.Habitat.H0")) };
					S->RegionTags = { Tag(TEXT("Test.Fish.Region.R1")) };
					S->WeatherTags = { Tag(TEXT("Test.Fish.Weather.Rain")) };
					S->AcceptedBait = { Tag(TEXT("Test.Fish.Bait.A")) };
					S->SpeciesTags = { Tag(TEXT("Test.Fish.Family.F1")) };
				} },
			{ TEXT("a new stat row (Fish.Stat.QA_TestOnly) used by a modifier"), [](FTables& T)
				{
					T.Stats->AddRow(TEXT("QA_TestOnly"), MakeStat(QAStat, 0.0f, 100.0f, 0.0f, false));
					Row<FFishModifierRow>(T.Modifiers, TEXT("Feisty"))->StatMods.Add(AddMod(QAStat, 2.0f));
				} },
			{ TEXT("enabled tiers with equal RollWeight (ties allowed)"), [](FTables& T) { Row<FFishRarityRow>(T.Rarities, TEXT("Rare"))->RollWeight = 22.0f; } },
			{ TEXT("a staged 0-weight tier above the others"), [](FTables& T)
				{
					FFishRarityRow Staged = MakeRarity(9, 0.0f, 50.0f);
					T.Rarities->AddRow(TEXT("QA_Staged"), Staged);
				} },
			{ TEXT("a wrapping window 22 -> 3"), [](FTables& T)
				{
					FFishSpeciesRow* S = Row<FFishSpeciesRow>(T.Species, TEXT("Bonefish"));
					S->TimeWindows[0].StartHour = 22.0f;
					S->TimeWindows[0].EndHour = 3.0f;
				} },
			{ TEXT("an all-day window 0 -> 24"), [](FTables& T)
				{
					FFishSpeciesRow* S = Row<FFishSpeciesRow>(T.Species, TEXT("Bonefish"));
					S->TimeWindows[0].StartHour = 0.0f;
					S->TimeWindows[0].EndHour = 24.0f;
				} },
			{ TEXT("WeightMin == WeightMax"), [](FTables& T) { Row<FFishSpeciesRow>(T.Species, TEXT("Bonefish"))->WeightMax = Row<FFishSpeciesRow>(T.Species, TEXT("Bonefish"))->WeightMin; } },
			{ TEXT("a single-use exclusivity group"), [](FTables& T) { Row<FFishModifierRow>(T.Modifiers, TEXT("Albino"))->ExclusivityGroup = TEXT("QA_Solo"); } },
			{ TEXT("an empty modifier table"), [](FTables& T) { T.Modifiers.Reset(NewTable(FFishModifierRow::StaticStruct())); } },
		};
		for (const FGood& Case : Good)
		{
			FTables T;
			if (!LoadReal(*this, T))
			{
				return false;
			}
			Case.Change(T);
			const TArray<FString> Problems = FFishDataValidator::Validate(T.Get());
			TestEqual(FString::Printf(TEXT("validator accepts: %s (first problem: %s)"), Case.What, Problems.Num() > 0 ? *Problems[0] : TEXT("-")), Problems.Num(), 0);
		}
		return true;
	}

	// ================================================================================================================
	// Extensibility: new content is a data edit (QA-101..QA-103)
	// ================================================================================================================

	static FString SpeciesJson(const FString& Name, float WeightMin, float WeightMax, const FString& Habitat, const FString& Bait, float BiteWeight, float ValuePerKg, int32 Level = 1)
	{
		return FString::Printf(TEXT("{\"Name\":\"%s\",\"DisplayName\":\"%s\",\"JournalText\":\"QA generated species\",\"BaseLevel\":%d,\"WeightMin\":%s,\"WeightMax\":%s,")
			TEXT("\"SizeSkew\":1,\"ReferenceWeight\":%s,\"WeightStatExponent\":0.5,\"BaseStats\":[{\"Tag\":\"Fish.Stat.Strength\",\"Value\":10},{\"Tag\":\"Fish.Stat.Stamina\",\"Value\":20}],")
			TEXT("\"BaseValuePerKg\":%s,\"BiteWeight\":%s,\"HabitatTags\":[%s],\"RegionTags\":[],\"TimeWindows\":[],\"WeatherTags\":[],\"AcceptedBait\":[%s],\"SpeciesTags\":[],")
			TEXT("\"AllowedRarities\":[],\"AllowedModifiers\":[],\"MaxModifiers\":8,\"FightPatternId\":\"Run\",\"Mesh\":\"None\"}"),
			*Name, *Name, Level, *F(WeightMin), *F(WeightMax), *F(WeightMin), *F(ValuePerKg), *F(BiteWeight), *QuotedList(Habitat), *QuotedList(Bait));
	}

	/** QA-101: a new species, rarity tier, modifier and stat added as JSON rows work with no code change */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAExtNewRowsTest, "Project.Fish.QA.Extensibility.NewRowsNeedNoCode", FISH_QA_FLAGS)
	bool FFishQAExtNewRowsTest::RunTest(const FString& Parameters)
	{
		FSourceTexts Texts;
		if (!ReadRealSources(*this, Texts))
		{
			return false;
		}
		AppendRow(Texts.Species, SpeciesJson(TEXT("QA_NewFish"), 2.0f, 2.0f, TEXT("Habitat.DeepDrop"), TEXT("Bait.Squid"), 1.0f, 10.0f, 4));
		AppendRow(Texts.Rarities, TEXT("{\"Name\":\"QA_Mythic\",\"DisplayName\":\"Mythic\",\"Rank\":5,\"RollWeight\":5,\"ValueMultiplier\":20,\"XpMultiplier\":8,\"LevelBonus\":4,")
			TEXT("\"StatMods\":[{\"StatTag\":\"Fish.Stat.Strength\",\"Op\":\"Multiply\",\"Value\":1.5}],\"CueColor\":{\"R\":1,\"G\":0.2,\"B\":0.2,\"A\":1}}"));
		AppendRow(Texts.Modifiers, TEXT("{\"Name\":\"QA_Glowing\",\"DisplayName\":\"Glowing\",\"Description\":\"QA\",\"RollChance\":1,\"ExclusivityGroup\":\"None\",")
			TEXT("\"StatMods\":[{\"StatTag\":\"Fish.Stat.QA_TestOnly\",\"Op\":\"Add\",\"Value\":3}],\"ValueMultiplier\":1,\"SpeciesIds\":[\"QA_NewFish\"],\"SpeciesTags\":[],")
			TEXT("\"RegionTags\":[],\"TimeWindows\":[],\"WeatherTags\":[],\"TintColor\":{\"R\":1,\"G\":1,\"B\":1,\"A\":0}}"));
		AppendRow(Texts.Stats, TEXT("{\"Name\":\"QA_TestOnly\",\"Tag\":\"Fish.Stat.QA_TestOnly\",\"DisplayName\":\"QA stat\",\"Min\":0,\"Max\":100,\"Default\":0,\"bIsDifficultyStat\":false}"));
		FTables T;
		if (!ImportAll(*this, T, Texts))
		{
			return false;
		}
		const TArray<FString> Problems = FFishDataValidator::Validate(T.Get());
		for (const FString& Problem : Problems)
		{
			AddError(TEXT("extended data: ") + Problem);
		}

		int32 Mythic = 0, Glowing = 0, BadStat = 0;
		for (int32 Seed = 1; Seed <= 10000; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_NewFish"), Seed), Fish);
			Mythic += Fish.RarityId == FName(TEXT("QA_Mythic")) ? 1 : 0;
			Glowing += Fish.HasModifier(TEXT("QA_Glowing")) ? 1 : 0;
			BadStat += StatOf(Fish, QAStat) == 3.0f ? 0 : 1;
		}
		TestTrue(FString::Printf(TEXT("the new tier rolls (%d / 10000, expected ~476)"), Mythic), Mythic > 300 && Mythic < 650);
		TestEqual(TEXT("the new modifier (chance 1, species condition) is on every QA_NewFish"), Glowing, 10000);
		TestEqual(TEXT("the new stat carries +3 on every catch"), BadStat, 0);

		auto Forced = [&T](FName Tier)
		{
			FFishRollContext Context = Ctx(TEXT("QA_NewFish"), 1);
			Context.ForcedRarityId = Tier;
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), Context, Fish);
			return Fish;
		};
		const FFishInstance Common = Forced(TEXT("Common"));
		const FFishInstance Mythic20 = Forced(TEXT("QA_Mythic"));
		TestEqual(TEXT("Common value = 10 coins/kg * 2 kg"), Common.Value, 20);
		TestEqual(TEXT("Mythic value = 20 x Common"), Mythic20.Value, 400);
		TestEqual(TEXT("Mythic level = 4 + 4"), Mythic20.Level, 8);
		TestEqual(TEXT("Mythic StatMods apply (Strength x1.5)"), StatOf(Mythic20, TEXT("Fish.Stat.Strength")), 15.0f);

		FFishInstance Bonefish;
		FFishRoll::Roll(T.Get(), Ctx(TEXT("Bonefish"), 1), Bonefish);
		TestFalse(TEXT("the species condition keeps QA_Glowing off Bonefish"), Bonefish.HasModifier(TEXT("QA_Glowing")));

		FFishRollContext Bite = Ctx(NAME_None, 5);
		Bite.HabitatTag = Tag(TEXT("Habitat.DeepDrop"));
		Bite.BaitTag = Tag(TEXT("Bait.Squid"));
		FName Picked;
		TestTrue(TEXT("the new species bites at its habitat"), FFishRoll::PickSpecies(T.Get(), Bite, Picked));
		TestEqual(TEXT("and it is the one that bites"), Picked, FName(TEXT("QA_NewFish")));
		return true;
	}

	/** QA-102: renaming every row (rarities, modifiers, stats, species) keeps everything working: no code knows row names */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAExtRenamedTest, "Project.Fish.QA.Extensibility.RenamedRowsWork", FISH_QA_FLAGS)
	bool FFishQAExtRenamedTest::RunTest(const FString& Parameters)
	{
		FSourceTexts Texts;
		if (!ReadRealSources(*this, Texts))
		{
			return false;
		}
		auto Rename = [this](FString& Text, const TCHAR* From, const TCHAR* To)
		{
			const FString Needle = FString::Printf(TEXT("\"Name\": \"%s\""), From);
			TestTrue(FString::Printf(TEXT("rename anchor %s found"), From), Text.Contains(Needle, ESearchCase::CaseSensitive));
			Text.ReplaceInline(*Needle, *FString::Printf(TEXT("\"Name\": \"%s\""), To), ESearchCase::CaseSensitive);
		};
		Rename(Texts.Rarities, TEXT("Common"), TEXT("Tier_A"));
		Rename(Texts.Rarities, TEXT("Uncommon"), TEXT("Tier_B"));
		Rename(Texts.Rarities, TEXT("Rare"), TEXT("Tier_C"));
		Rename(Texts.Rarities, TEXT("Epic"), TEXT("Tier_D"));
		Rename(Texts.Rarities, TEXT("Legendary"), TEXT("Tier_E"));
		Rename(Texts.Modifiers, TEXT("Albino"), TEXT("Trait_4"));
		Rename(Texts.Modifiers, TEXT("Feisty"), TEXT("Trait_3"));
		Rename(Texts.Modifiers, TEXT("Giant"), TEXT("Trait_2"));
		Rename(Texts.Modifiers, TEXT("Heavy"), TEXT("Trait_1"));
		Rename(Texts.Stats, TEXT("Weight"), TEXT("S_1"));
		Rename(Texts.Stats, TEXT("Strength"), TEXT("S_2"));
		Rename(Texts.Species, TEXT("Bonefish"), TEXT("Fish_1"));
		Rename(Texts.Species, TEXT("CoralSnapper"), TEXT("Fish_2"));
		FTables T;
		if (!ImportAll(*this, T, Texts))
		{
			return false;
		}
		for (const FString& Problem : FFishDataValidator::Validate(T.Get()))
		{
			AddError(TEXT("renamed data: ") + Problem);
		}
		const int32 N = 50000;
		TMap<FName, int32> Tiers;
		int32 Feisty = 0, BothSize = 0, Failures = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FFishInstance Fish;
			Failures += FFishRoll::Roll(T.Get(), Ctx(TEXT("Fish_1"), Seed), Fish) ? 0 : 1;
			Tiers.FindOrAdd(Fish.RarityId)++;
			Feisty += Fish.HasModifier(TEXT("Trait_3")) ? 1 : 0;
			BothSize += (Fish.HasModifier(TEXT("Trait_1")) && Fish.HasModifier(TEXT("Trait_2"))) ? 1 : 0;
		}
		TestEqual(TEXT("every roll succeeds"), Failures, 0);
		const double Total = 70.0 + 22.0 + 8.0;
		CheckOdds(*this, TEXT("renamed tiers"), { TEXT("Tier_A"), TEXT("Tier_B"), TEXT("Tier_C"), TEXT("Tier_D"), TEXT("Tier_E") },
			{ Tiers.FindRef(TEXT("Tier_A")), Tiers.FindRef(TEXT("Tier_B")), Tiers.FindRef(TEXT("Tier_C")), Tiers.FindRef(TEXT("Tier_D")), Tiers.FindRef(TEXT("Tier_E")) },
			{ 70.0 / Total, 22.0 / Total, 8.0 / Total, 0.0, 0.0 }, N);
		TestTrue(FString::Printf(TEXT("renamed Feisty still rolls at ~15%% (%.4f)"), double(Feisty) / N), WithinSigma(double(Feisty) / N, 0.15, N));
		TestEqual(TEXT("renamed Size group is still exclusive"), BothSize, 0);
		FFishInstance Fish;
		FFishRoll::Roll(T.Get(), Ctx(TEXT("Fish_1"), 1), Fish);
		TestTrue(TEXT("Weight still found by tag"), Fish.WeightKg > 0.0f && Fish.GetStat(Tag(TEXT("Fish.Stat.Weight"))) == Fish.WeightKg);
		return true;
	}

	/** QA-103 (P2): 50 rarity tiers and 200 modifiers in 20 groups: valid, exclusive, odds hold */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQAExtManyRowsTest, "Project.Fish.QA.Extensibility.ManyTiersAndModifiers", FISH_QA_FLAGS)
	bool FFishQAExtManyRowsTest::RunTest(const FString& Parameters)
	{
		FTables T;
		MakeIdentity(T);
		Row<FFishSpeciesRow>(T.Species, TEXT("QA_Fixed"))->MaxModifiers = 50;
		ResetRarities(T);
		double Total = 0.0;
		for (int32 i = 0; i < 50; ++i)
		{
			const float Weight = 100.0f - 2.0f * i;
			T.Rarities->AddRow(FName(*FString::Printf(TEXT("Tier_%d"), i)), MakeRarity(i, Weight, 1.0f + i));
			Total += Weight;
		}
		for (int32 i = 0; i < 200; ++i)
		{
			T.Modifiers->AddRow(FName(*FString::Printf(TEXT("Trait_%d"), i)), MakeModifier(0.3f, FName(*FString::Printf(TEXT("Group_%d"), i % 20)), { AddMod(TEXT("Fish.Stat.Strength"), 0.5f) }));
		}
		for (const FString& Problem : FFishDataValidator::Validate(T.Get()))
		{
			AddError(TEXT("many rows: ") + Problem);
		}
		const int32 N = 20000;
		int32 GroupViolations = 0, Tier0 = 0;
		for (int32 Seed = 1; Seed <= N; ++Seed)
		{
			FFishInstance Fish;
			FFishRoll::Roll(T.Get(), Ctx(TEXT("QA_Fixed"), Seed), Fish);
			TSet<int32> Groups;
			for (const FName& Id : Fish.ModifierIds)
			{
				const int32 Group = FCString::Atoi(*Id.ToString().RightChop(6)) % 20;
				GroupViolations += Groups.Contains(Group) ? 1 : 0;
				Groups.Add(Group);
			}
			Tier0 += Fish.RarityId == FName(TEXT("Tier_0")) ? 1 : 0;
		}
		TestEqual(TEXT("at most one modifier per group"), GroupViolations, 0);
		TestTrue(FString::Printf(TEXT("Tier_0 odds %.4f ~ %.4f"), double(Tier0) / N, 100.0 / Total), WithinSigma(double(Tier0) / N, 100.0 / Total, N));
		return true;
	}

	// ================================================================================================================
	// Settings (QA-117, QA-118)
	// ================================================================================================================

	/** QA-117: the settings point at /Game/Data/DT_Fish* and each has a source in data/tables */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQASettingsDefaultsTest, "Project.Fish.QA.Settings.DefaultsPointToDataFolder", FISH_QA_FLAGS)
	bool FFishQASettingsDefaultsTest::RunTest(const FString& Parameters)
	{
		const UFishSettings* Settings = GetDefault<UFishSettings>();
		const TSoftObjectPtr<UDataTable>* Tables[] = { &Settings->SpeciesTable, &Settings->RarityTable, &Settings->ModifierTable, &Settings->StatTable };
		for (int32 i = 0; i < 4; ++i)
		{
			const FSoftObjectPath Path = Tables[i]->ToSoftObjectPath();
			TestEqual(FString::Printf(TEXT("%s package"), TableNames[i]), Path.GetLongPackageName(), FString::Printf(TEXT("/Game/Data/%s"), TableNames[i]));
			TestTrue(FString::Printf(TEXT("%s source exists"), TableNames[i]), FPaths::FileExists(SourcePath(FString(TableNames[i]) + TEXT(".json"))));
		}
		TestTrue(TEXT("tuning: MaxLuck > 0"), Settings->RollTuning.MaxLuck > 0.0f);
		TestTrue(TEXT("level scaling: 0 < Min <= 1 <= Max"), Settings->LevelScaling.MinMultiplier > 0.0f && Settings->LevelScaling.MinMultiplier <= 1.0f && Settings->LevelScaling.MaxMultiplier >= 1.0f);
		return true;
	}

	/** QA-118: missing assets are graceful: LoadTables reports false with a readable error; the settings wrappers fail with a warning */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQASettingsMissingTest, "Project.Fish.QA.Settings.MissingAssetsGraceful", FISH_QA_FLAGS)
	bool FFishQASettingsMissingTest::RunTest(const FString& Parameters)
	{
		FLogCapture Log;
		UFishSettings* Settings = NewObject<UFishSettings>(GetTransientPackage(), NAME_None, RF_Transient);
		const TSoftObjectPtr<UDataTable> Missing(FSoftObjectPath(TEXT("/Game/Data/DT_QA_DoesNotExist.DT_QA_DoesNotExist")));
		Settings->SpeciesTable = Missing;
		Settings->RarityTable = Missing;
		Settings->ModifierTable = Missing;
		Settings->StatTable = Missing;
		FFishTables Tables;
		FString Error;
		TestFalse(TEXT("LoadTables fails for missing assets"), Settings->LoadTables(Tables, Error));
		TestFalse(TEXT("with a readable error"), Error.IsEmpty());
		TestNull(TEXT("species table left null"), Tables.Species);

		// The real project settings: in a lane the assets don't exist yet (false + warning); in the main checkout they load
		bool bAllExist = true;
		for (const TCHAR* Name : TableNames)
		{
			bAllExist &= FPackageName::DoesPackageExist(FString::Printf(TEXT("/Game/Data/%s"), Name));
		}
		FFishRollContext Context = Ctx(TEXT("Bonefish"), 1);
		FFishInstance Fish;
		const bool bRolled = UFishLibrary::RollFishFromSettings(Context, Fish);
		TestEqual(TEXT("RollFishFromSettings succeeds exactly when the assets exist"), bRolled, bAllExist);
		if (!bAllExist)
		{
			TestFalse(TEXT("no fish without tables"), Fish.IsValid());
			TestTrue(TEXT("warns"), Log.NumWarnings() > 0);
		}
		TestEqual(TEXT("no LogLureFish errors"), Log.NumErrors(), 0);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

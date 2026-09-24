// Lure: cooler, selling, money, XP and levels (T-010).

#include "Progression/LureProgressionTypes.h"
#include "DataTableUtils.h"
#include "Serialization/Csv/CsvParser.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY(LogLureProgression);

// ---------------------------------------------------------------------------------------------------------------------
// FLureLevelCurve
// ---------------------------------------------------------------------------------------------------------------------

namespace LureLevelCurvePrivate
{
	/** Keeps values up to and including the first entry <= 0 (that level is the cap). */
	static TArray<int32> Sanitize(TConstArrayView<int32> Values)
	{
		TArray<int32> Result;
		for (int32 Value : Values)
		{
			Result.Add(FMath::Max(0, Value));
			if (Value <= 0)
			{
				break;
			}
		}
		return Result;
	}
}

FLureLevelCurve FLureLevelCurve::FromValues(TConstArrayView<int32> InXpToNext)
{
	FLureLevelCurve Curve;
	Curve.XpToNext = LureLevelCurvePrivate::Sanitize(InXpToNext);
	return Curve;
}

FLureLevelCurve FLureLevelCurve::FromTable(const UDataTable* Table, TArray<FString>* OutProblems)
{
	auto Report = [OutProblems](const FString& Problem)
	{
		if (OutProblems)
		{
			OutProblems->Add(Problem);
		}
	};

	FLureLevelCurve Curve;
	if (!Table)
	{
		Report(TEXT("DT_PlayerLevel: no table"));
		return Curve;
	}
	if (Table->GetRowStruct() != FPlayerLevelRow::StaticStruct())
	{
		Report(FString::Printf(TEXT("DT_PlayerLevel: row struct is %s, expected PlayerLevelRow"), *GetNameSafe(Table->GetRowStruct())));
		return Curve;
	}

	TArray<const FPlayerLevelRow*> Rows;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		Rows.Add(reinterpret_cast<const FPlayerLevelRow*>(Pair.Value));
	}
	Rows.Sort([](const FPlayerLevelRow& A, const FPlayerLevelRow& B) { return A.Level < B.Level; });

	TArray<int32> Values;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FPlayerLevelRow& Row = *Rows[Index];
		if (Row.Level != Index + 1)
		{
			Report(FString::Printf(TEXT("DT_PlayerLevel: expected level %d, found level %d; the curve stops at level %d"), Index + 1, Row.Level, FMath::Max(1, Index)));
			break;
		}
		Values.Add(Row.XpToNext);
		if (Row.XpToNext <= 0 && Index + 1 < Rows.Num())
		{
			Report(FString::Printf(TEXT("DT_PlayerLevel: level %d has XpToNext %d below the last level; the curve stops there"), Row.Level, Row.XpToNext));
			break;
		}
	}
	Curve.XpToNext = LureLevelCurvePrivate::Sanitize(Values);
	return Curve;
}

int64 FLureLevelCurve::GetXpAtLevelStart(int32 Level) const
{
	const int32 Clamped = FMath::Clamp(Level, 1, GetMaxLevel());
	int64 Total = 0;
	for (int32 Index = 0; Index < Clamped - 1 && Index < XpToNext.Num(); ++Index)
	{
		Total += FMath::Max(0, XpToNext[Index]);
	}
	return Total;
}

int32 FLureLevelCurve::GetLevelForXp(int64 TotalXp) const
{
	const int64 Xp = FMath::Max<int64>(0, TotalXp);
	int32 Level = 1;
	int64 Start = 0;
	for (int32 Index = 0; Index + 1 < XpToNext.Num(); ++Index)
	{
		Start += FMath::Max(0, XpToNext[Index]);
		if (Xp < Start)
		{
			break;
		}
		Level = Index + 2;
	}
	return Level;
}

FLureLevelProgress FLureLevelCurve::GetProgress(int64 TotalXp, int32 CurrentLevel) const
{
	FLureLevelProgress Progress;
	Progress.Level = FMath::Clamp(FMath::Max(CurrentLevel, GetLevelForXp(TotalXp)), 1, GetMaxLevel());
	const int64 Into = FMath::Max<int64>(0, FMath::Max<int64>(0, TotalXp) - GetXpAtLevelStart(Progress.Level));
	Progress.XpIntoLevel = static_cast<int32>(FMath::Min<int64>(Into, MAX_int32));
	Progress.bIsMaxLevel = Progress.Level >= GetMaxLevel();
	if (Progress.bIsMaxLevel)
	{
		Progress.XpForNextLevel = 0;
		Progress.Fraction = 1.0f;
	}
	else
	{
		Progress.XpForNextLevel = FMath::Max(1, XpToNext[Progress.Level - 1]);
		Progress.Fraction = FMath::Clamp(static_cast<float>(static_cast<double>(Progress.XpIntoLevel) / Progress.XpForNextLevel), 0.0f, 1.0f);
	}
	return Progress;
}

// ---------------------------------------------------------------------------------------------------------------------
// FLureProgressionRules
// ---------------------------------------------------------------------------------------------------------------------

int32 FLureProgressionRules::GetSellPrice(const FFishInstance& Fish, float SellMultiplier)
{
	if (!Fish.IsValid() || Fish.Value <= 0 || !FMath::IsFinite(SellMultiplier) || !(SellMultiplier > 0.0f))
	{
		return 0;
	}
	const double Price = FMath::FloorToDouble(static_cast<double>(Fish.Value) * static_cast<double>(SellMultiplier) + 0.5);
	return static_cast<int32>(FMath::Clamp(Price, 1.0, static_cast<double>(MAX_int32)));
}

int32 FLureProgressionRules::GetSellTotal(TConstArrayView<FFishInstance> Fish, float SellMultiplier)
{
	int32 Total = 0;
	for (const FFishInstance& One : Fish)
	{
		Total = SaturatingAdd(Total, GetSellPrice(One, SellMultiplier));
	}
	return Total;
}

int32 FLureProgressionRules::SaturatingAdd(int32 A, int32 B)
{
	return static_cast<int32>(FMath::Clamp(static_cast<int64>(A) + static_cast<int64>(B), static_cast<int64>(0), static_cast<int64>(MAX_int32)));
}

// ---------------------------------------------------------------------------------------------------------------------
// FLureProgressionData
// ---------------------------------------------------------------------------------------------------------------------

namespace LureProgressionDataPrivate
{
	static bool CheckTable(const UDataTable* Table, const UScriptStruct* RowStruct, const TCHAR* Name, TArray<FString>& Problems)
	{
		if (!Table)
		{
			Problems.Add(FString::Printf(TEXT("%s: no table"), Name));
			return false;
		}
		if (Table->GetRowStruct() != RowStruct)
		{
			Problems.Add(FString::Printf(TEXT("%s: row struct is %s, expected %s"), Name, *GetNameSafe(Table->GetRowStruct()), *RowStruct->GetName()));
			return false;
		}
		if (Table->GetRowMap().Num() == 0)
		{
			Problems.Add(FString::Printf(TEXT("%s: the table has no rows"), Name));
			return false;
		}
		return true;
	}

	template <typename RowType>
	static void ForEachRow(const UDataTable* Table, TFunctionRef<void(FName, const RowType&)> Visit)
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			Visit(Pair.Key, *reinterpret_cast<const RowType*>(Pair.Value));
		}
	}
}

TArray<FString> FLureProgressionData::ValidatePlayerLevelTable(const UDataTable* Table)
{
	using namespace LureProgressionDataPrivate;
	TArray<FString> Problems;
	if (!CheckTable(Table, FPlayerLevelRow::StaticStruct(), TEXT("DT_PlayerLevel"), Problems))
	{
		return Problems;
	}

	TMap<int32, FName> ByLevel;
	ForEachRow<FPlayerLevelRow>(Table, [&](FName RowName, const FPlayerLevelRow& Row)
	{
		if (Row.Level < 1)
		{
			Problems.Add(FString::Printf(TEXT("DT_PlayerLevel row %s: Level %d must be >= 1"), *RowName.ToString(), Row.Level));
			return;
		}
		if (const FName* Existing = ByLevel.Find(Row.Level))
		{
			Problems.Add(FString::Printf(TEXT("DT_PlayerLevel rows %s and %s both declare level %d"), *Existing->ToString(), *RowName.ToString(), Row.Level));
			return;
		}
		ByLevel.Add(Row.Level, RowName);
	});

	const int32 MaxLevel = ByLevel.Num();
	for (int32 Level = 1; Level <= MaxLevel; ++Level)
	{
		if (!ByLevel.Contains(Level))
		{
			Problems.Add(FString::Printf(TEXT("DT_PlayerLevel: level %d is missing (levels must be 1..N with no gaps)"), Level));
		}
	}

	int32 PreviousXp = 0;
	for (int32 Level = 1; Level <= MaxLevel; ++Level)
	{
		const FName* RowName = ByLevel.Find(Level);
		if (!RowName)
		{
			continue;
		}
		const FPlayerLevelRow* Row = Table->FindRow<FPlayerLevelRow>(*RowName, TEXT("ValidatePlayerLevelTable"), false);
		if (!Row)
		{
			continue;
		}
		if (Level < MaxLevel)
		{
			if (Row->XpToNext <= 0)
			{
				Problems.Add(FString::Printf(TEXT("DT_PlayerLevel row %s: XpToNext %d must be > 0 below the last level"), *RowName->ToString(), Row->XpToNext));
			}
			else if (Row->XpToNext < PreviousXp)
			{
				Problems.Add(FString::Printf(TEXT("DT_PlayerLevel row %s: XpToNext %d is less than the level before (%d); the curve must not get easier"),
					*RowName->ToString(), Row->XpToNext, PreviousXp));
			}
			PreviousXp = FMath::Max(PreviousXp, Row->XpToNext);
		}
		else if (Row->XpToNext != 0)
		{
			Problems.Add(FString::Printf(TEXT("DT_PlayerLevel row %s: the last level (%d) is the cap, so XpToNext must be 0 (found %d)"), *RowName->ToString(), Level, Row->XpToNext));
		}
	}
	return Problems;
}

TArray<FString> FLureProgressionData::ValidateCoolerTable(const UDataTable* Table, FName DefaultCoolerId)
{
	using namespace LureProgressionDataPrivate;
	TArray<FString> Problems;
	if (!CheckTable(Table, FCoolerRow::StaticStruct(), TEXT("DT_Cooler"), Problems))
	{
		return Problems;
	}
	ForEachRow<FCoolerRow>(Table, [&](FName RowName, const FCoolerRow& Row)
	{
		if (Row.Slots < 1 || Row.Slots > MaxCoolerSlots)
		{
			Problems.Add(FString::Printf(TEXT("DT_Cooler row %s: Slots %d must be in [1, %d]"), *RowName.ToString(), Row.Slots, MaxCoolerSlots));
		}
		if (Row.DisplayName.IsEmptyOrWhitespace())
		{
			Problems.Add(FString::Printf(TEXT("DT_Cooler row %s: DisplayName is empty"), *RowName.ToString()));
		}
		// T-030: freshness speeds inside (0 = holds) and the carry speed.
		for (const TPair<const TCHAR*, float>& Rate : { TPair<const TCHAR*, float>(TEXT("OpenDecayRate"), Row.OpenDecayRate), TPair<const TCHAR*, float>(TEXT("ClosedDecayRate"), Row.ClosedDecayRate) })
		{
			if (!FMath::IsFinite(Rate.Value) || Rate.Value < 0.0f || Rate.Value > MaxDecayRate)
			{
				Problems.Add(FString::Printf(TEXT("DT_Cooler row %s: %s %g must be in [0, %g]"), *RowName.ToString(), Rate.Key, Rate.Value, MaxDecayRate));
			}
		}
		if (!FMath::IsFinite(Row.CarrySpeedMultiplier) || !(Row.CarrySpeedMultiplier > 0.0f) || Row.CarrySpeedMultiplier > MaxCarrySpeedMultiplier)
		{
			Problems.Add(FString::Printf(TEXT("DT_Cooler row %s: CarrySpeedMultiplier %g must be in (0, %g]"), *RowName.ToString(), Row.CarrySpeedMultiplier, MaxCarrySpeedMultiplier));
		}
	});
	if (!DefaultCoolerId.IsNone() && !Table->GetRowMap().Contains(DefaultCoolerId))
	{
		Problems.Add(FString::Printf(TEXT("DT_Cooler: the default cooler '%s' (ULureProgressionSettings DefaultCoolerId) has no row"), *DefaultCoolerId.ToString()));
	}
	return Problems;
}

TArray<FString> FLureProgressionData::ValidateMarketTable(const UDataTable* Table, FName DefaultMarketId)
{
	using namespace LureProgressionDataPrivate;
	TArray<FString> Problems;
	if (!CheckTable(Table, FFishMarketRow::StaticStruct(), TEXT("DT_FishMarket"), Problems))
	{
		return Problems;
	}
	ForEachRow<FFishMarketRow>(Table, [&](FName RowName, const FFishMarketRow& Row)
	{
		if (!FMath::IsFinite(Row.SellMultiplier) || !(Row.SellMultiplier > 0.0f) || Row.SellMultiplier > MaxSellMultiplier)
		{
			Problems.Add(FString::Printf(TEXT("DT_FishMarket row %s: SellMultiplier %g must be in (0, %g]"), *RowName.ToString(), Row.SellMultiplier, MaxSellMultiplier));
		}
		if (Row.DisplayName.IsEmptyOrWhitespace())
		{
			Problems.Add(FString::Printf(TEXT("DT_FishMarket row %s: DisplayName is empty"), *RowName.ToString()));
		}
	});
	if (!DefaultMarketId.IsNone() && !Table->GetRowMap().Contains(DefaultMarketId))
	{
		Problems.Add(FString::Printf(TEXT("DT_FishMarket: the default market '%s' (ULureProgressionSettings DefaultMarketId) has no row"), *DefaultMarketId.ToString()));
	}
	return Problems;
}

TArray<FString> FLureProgressionData::ValidateCsvSource(const FString& Csv, const UScriptStruct* RowStruct, const FString& TableName)
{
	TArray<FString> Problems;
	if (!RowStruct)
	{
		Problems.Add(FString::Printf(TEXT("%s: no row struct"), *TableName));
		return Problems;
	}
	const FCsvParser Parser(Csv);
	const FCsvParser::FRows& Rows = Parser.GetRows();
	if (Rows.Num() < 2 || Rows[0].Num() < 2)
	{
		Problems.Add(FString::Printf(TEXT("%s: needs a header row and at least one data row"), *TableName));
		return Problems;
	}

	// Header: column 0 = row name, then one column per property.
	TArray<const FProperty*> Columns;
	Columns.Add(nullptr);
	TSet<const FProperty*> Seen;
	for (int32 Column = 1; Column < Rows[0].Num(); ++Column)
	{
		const FString Header = FString(Rows[0][Column]).TrimStartAndEnd();
		const FProperty* Match = nullptr;
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			if (DataTableUtils::GetPropertyImportNames(*It).Contains(Header))
			{
				Match = *It;
				break;
			}
		}
		if (!Match)
		{
			Problems.Add(FString::Printf(TEXT("%s: column '%s' is not a property of %s"), *TableName, *Header, *RowStruct->GetName()));
		}
		else if (Seen.Contains(Match))
		{
			Problems.Add(FString::Printf(TEXT("%s: column '%s' appears twice"), *TableName, *Header));
			Match = nullptr;
		}
		else
		{
			Seen.Add(Match);
		}
		Columns.Add(Match);
	}
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		bool bOptional = false;
#if WITH_METADATA
		bOptional = It->HasMetaData(TEXT("DataTableImportOptional"));
#endif
		if (!bOptional && !Seen.Contains(*It))
		{
			Problems.Add(FString::Printf(TEXT("%s: no column for the required property %s"), *TableName, *It->GetName()));
		}
	}

	TSet<FString> RowNames;
	for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
	{
		const TArray<const TCHAR*>& Cells = Rows[RowIndex];
		if (Cells.Num() == 0 || (Cells.Num() == 1 && FString(Cells[0]).TrimStartAndEnd().IsEmpty()))
		{
			continue; // blank line
		}
		const FString RowName = FString(Cells[0]).TrimStartAndEnd();
		if (RowName.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s: line %d has no row name"), *TableName, RowIndex + 1));
		}
		else if (RowNames.Contains(RowName))
		{
			Problems.Add(FString::Printf(TEXT("%s: row name '%s' appears twice"), *TableName, *RowName));
		}
		RowNames.Add(RowName);

		for (int32 Column = 1; Column < Columns.Num(); ++Column)
		{
			const FNumericProperty* Numeric = CastField<FNumericProperty>(Columns[Column]);
			if (!Numeric || Numeric->IsEnum())
			{
				continue;
			}
			const FString Cell = Column < Cells.Num() ? FString(Cells[Column]).TrimStartAndEnd() : FString();
			if (Cell.IsEmpty() || !FCString::IsNumeric(*Cell))
			{
				Problems.Add(FString::Printf(TEXT("%s row %s: %s '%s' is not a number"), *TableName, *RowName, *Columns[Column]->GetName(), *Cell));
			}
			else if (Numeric->IsInteger() && Cell.Contains(TEXT(".")))
			{
				Problems.Add(FString::Printf(TEXT("%s row %s: %s '%s' must be a whole number"), *TableName, *RowName, *Columns[Column]->GetName(), *Cell));
			}
		}
	}
	return Problems;
}

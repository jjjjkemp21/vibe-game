// Lure T-028 QA (qa-engineer, 2026-09-23): the rod-steering columns of DT_FishFight, as data.
// Project.Fishing.Fight.Rod.QA.Data.*
// QA's own rules (restated from FishFightTypes.h and reel-fight-rules.md "Tuning columns"), checked on the raw CSV text (the engine
// imports text in a number cell as 0 with no problem, T004-Q2), on the imported row, and through the component's fallback.

#include "RodQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace LureRodQA
{
	namespace RodData
	{
		/** The T-028 columns (every one optional: DataTableImportOptional). */
		const TCHAR* const RodColumns[] = {
			TEXT("RodAimUpDeg"), TEXT("RodAimDownDeg"), TEXT("RodAimSideDeg"), TEXT("PitchBackPressure"), TEXT("PitchDipPressure"), TEXT("SideMinShare"),
			TEXT("SideLeverage"), TEXT("SideTurnRate"), TEXT("SideTurnPull"), TEXT("SideDrain"), TEXT("ReelSteps"), TEXT("ReelDefaultStep"), TEXT("ReelSpeedMin"),
			TEXT("ReelSpeedMax"), TEXT("ReelLoadPerSpeed"), TEXT("CameraFollowTime"), TEXT("CameraRodYawShare"), TEXT("CameraRodPitchShare"),
			TEXT("RodAimLookPitchDeg"), TEXT("RodAimLookYawDeg"), TEXT("RodAimBlendTime") };

		/** QA's playable ranges per column (inclusive): wider than "valid", narrower than "anything Validate lets through". */
		struct FRange { const TCHAR* Column; double Min; double Max; };
		const FRange Ranges[] = {
			{ TEXT("RodAimUpDeg"), 5.0, 90.0 }, { TEXT("RodAimDownDeg"), 5.0, 90.0 }, { TEXT("RodAimSideDeg"), 5.0, 90.0 },
			{ TEXT("PitchBackPressure"), 0.01, 1.0 }, { TEXT("PitchDipPressure"), 0.01, 0.95 }, { TEXT("SideMinShare"), 0.01, 0.99 },
			{ TEXT("SideLeverage"), 0.01, 0.95 }, { TEXT("SideTurnRate"), 0.0, 5.0 }, { TEXT("SideTurnPull"), 0.0, 0.95 }, { TEXT("SideDrain"), 0.0, 5.0 },
			{ TEXT("ReelSteps"), 2.0, 9.0 }, { TEXT("ReelDefaultStep"), 1.0, 9.0 }, { TEXT("ReelSpeedMin"), 0.05, 0.99 }, { TEXT("ReelSpeedMax"), 1.01, 4.0 },
			{ TEXT("ReelLoadPerSpeed"), 0.01, 5.0 }, { TEXT("CameraFollowTime"), 0.0, 2.0 }, { TEXT("CameraRodYawShare"), 0.0, 1.0 },
			{ TEXT("CameraRodPitchShare"), 0.0, 1.0 }, { TEXT("RodAimLookPitchDeg"), 0.0, 90.0 }, { TEXT("RodAimLookYawDeg"), 0.0, 90.0 },
			{ TEXT("RodAimBlendTime"), 0.0, 1.0 } };

		inline bool IsIntColumn(const FString& Column)
		{
			return Column == TEXT("ReelSteps") || Column == TEXT("ReelDefaultStep");
		}

		/** A plain decimal number (what a designer should type): digits, an optional point and digits, an optional leading minus. */
		inline bool IsNumberText(const FString& Text, bool bInt)
		{
			const FString Cell = Text.TrimStartAndEnd();
			if (Cell.IsEmpty())
			{
				return false;
			}
			int32 Index = Cell[0] == TEXT('-') ? 1 : 0;
			bool bDigits = false;
			bool bPoint = false;
			for (; Index < Cell.Len(); ++Index)
			{
				const TCHAR C = Cell[Index];
				if (FChar::IsDigit(C))
				{
					bDigits = true;
				}
				else if (C == TEXT('.') && !bPoint && !bInt)
				{
					bPoint = true;
				}
				else
				{
					return false;
				}
			}
			return bDigits;
		}

		/** The CSV's header and its rows (the shipped file is simple: no quoted commas in the numeric part). */
		inline void Split(const FString& Csv, TArray<FString>& OutHeader, TArray<TArray<FString>>& OutRows)
		{
			TArray<FString> LinesOut;
			Csv.ParseIntoArrayLines(LinesOut, true);
			OutHeader.Reset();
			OutRows.Reset();
			if (LinesOut.Num() == 0)
			{
				return;
			}
			LinesOut[0].ParseIntoArray(OutHeader, TEXT(","), false);
			for (FString& Cell : OutHeader)
			{
				Cell.TrimStartAndEndInline();
			}
			for (int32 Line = 1; Line < LinesOut.Num(); ++Line)
			{
				TArray<FString> Cells;
				LinesOut[Line].ParseIntoArray(Cells, TEXT(","), false);
				OutRows.Add(MoveTemp(Cells));
			}
		}

		inline FString Join(const TArray<FString>& Header, const TArray<TArray<FString>>& Rows)
		{
			FString Out = FString::Join(Header, TEXT(",")) + TEXT("\n");
			for (const TArray<FString>& Row : Rows)
			{
				Out += FString::Join(Row, TEXT(",")) + TEXT("\n");
			}
			return Out;
		}

		/** The raw scan QA keeps for every numeric rod column: the text is a number. Returns the columns that are not. */
		inline TArray<FString> NonNumericRodCells(const FString& Csv)
		{
			TArray<FString> Header;
			TArray<TArray<FString>> Rows;
			Split(Csv, Header, Rows);
			TArray<FString> Bad;
			for (const TArray<FString>& Row : Rows)
			{
				for (const TCHAR* Column : RodColumns)
				{
					const int32 Index = Header.IndexOfByKey(FString(Column));
					if (Index != INDEX_NONE && Row.IsValidIndex(Index) && !IsNumberText(Row[Index], IsIntColumn(Column)))
					{
						Bad.Add(FString::Printf(TEXT("%s='%s'"), Column, *Row[Index]));
					}
				}
			}
			return Bad;
		}

		/** The value of Column in a row, through reflection (float or int). */
		inline double Value(const FLureFishFightRow& Row, const TCHAR* Column)
		{
			const FProperty* Property = FLureFishFightRow::StaticStruct()->FindPropertyByName(FName(Column));
			if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
			{
				return Float->GetPropertyValue_InContainer(&Row);
			}
			if (const FIntProperty* Int = CastField<FIntProperty>(Property))
			{
				return Int->GetPropertyValue_InContainer(&Row);
			}
			return TNumericLimits<double>::Max();
		}

		inline const FLureFishFightRow* DefaultRow(const TStrongObjectPtr<UDataTable>& Table)
		{
			return Table.IsValid() ? Table->FindRow<FLureFishFightRow>(GetDefault<ULureFishingSettings>()->FishFightRow, TEXT("RodQA"), false) : nullptr;
		}
	}

	/**
	 *  The shipped DT_FishFight.csv: every rod column is there, typed as a plain number, inside QA's playable range, and the relations the
	 *  design needs hold (pulling back and dipping both do something, both side effects are on, the wheel has steps and fast costs tension,
	 *  the default step is the T-007 reel exactly). The built-in fallback row equals the CSV in every rod column; every shipped pattern can be steered.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQADataShipped, "Project.Fishing.Fight.Rod.QA.Data.ShippedRodColumns", Flags)
	bool FRodQADataShipped::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Tables;
		if (!Tables.Load(*this))
		{
			return false;
		}
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		RodData::Split(Tables.FightCsv, Header, Rows);
		for (const TCHAR* Column : RodData::RodColumns)
		{
			TestTrue(FString::Printf(TEXT("DT_FishFight.csv has the column %s"), Column), Header.Contains(FString(Column)));
			TestNotNull(FString::Printf(TEXT("FLureFishFightRow has the property %s"), Column), FLureFishFightRow::StaticStruct()->FindPropertyByName(FName(Column)));
		}
		for (const FString& Column : Header)
		{
			TestTrue(FString::Printf(TEXT("CSV column %s is a row property (or the row name)"), *Column), Column == TEXT("Name") || FLureFishFightRow::StaticStruct()->FindPropertyByName(FName(*Column)) != nullptr);
		}
		const TArray<FString> NonNumeric = RodData::NonNumericRodCells(Tables.FightCsv);
		TestEqual(TEXT("rod cells that are not plain numbers: ") + FString::Join(NonNumeric, TEXT(", ")), NonNumeric.Num(), 0);

		const FLureFishFightRow* Row = Tables.Tuning();
		if (!TestNotNull(TEXT("the Default row"), Row))
		{
			return false;
		}
		FString Problem;
		const bool bValid = Row->Validate(Problem);
		TestTrue(TEXT("the shipped row validates: ") + Problem, bValid);
		for (const RodData::FRange& Range : RodData::Ranges)
		{
			const double V = RodData::Value(*Row, Range.Column);
			TestTrue(FString::Printf(TEXT("%s = %g is in QA's playable range [%g, %g]"), Range.Column, V, Range.Min, Range.Max), V >= Range.Min && V <= Range.Max);
		}
		TestTrue(TEXT("ReelDefaultStep is one of the steps"), Row->ReelDefaultStep >= 1 && Row->ReelDefaultStep <= Row->ReelSteps);
		TestTrue(TEXT("the default step is the T-007 reel: speed exactly 1"), FLureFight::ReelStepSpeed(FLureFight::DefaultReelStep(*Row), *Row) == 1.f);
		TestTrue(TEXT("... and cranking load exactly 1"), FLureFight::ReelStepLoad(FLureFight::DefaultReelStep(*Row), *Row) == 1.f);
		TestTrue(TEXT("turning a fish does something (SideTurnRate, SideTurnPull or SideDrain > 0)"), Row->SideTurnRate > 0.f || Row->SideTurnPull > 0.f || Row->SideDrain > 0.f);
		TestTrue(TEXT("fast gains line but builds tension (ReelSpeedMax > 1, ReelLoadPerSpeed > 0)"), Row->ReelSpeedMax > 1.f && Row->ReelLoadPerSpeed > 0.f);

		const FLureFishFightRow Fallback = FLureFishFightRow::GetFallbackRow();
		for (const TCHAR* Column : RodData::RodColumns)
		{
			TestTrue(FString::Printf(TEXT("the built-in row equals the CSV in %s (%g vs %g)"), Column, RodData::Value(Fallback, Column), RodData::Value(*Row, Column)),
				RodData::Value(Fallback, Column) == RodData::Value(*Row, Column));
		}

		// Every shipped pattern has something to steer against and something straight (so the run hint comes and goes).
		for (const FName PatternId : { FName(TEXT("Run")), FName(TEXT("Dive")), FName(TEXT("Dart")) })
		{
			const FLureFightPatternRow* Pattern = Tables.Pattern(PatternId);
			if (!TestNotNull(TEXT("pattern ") + PatternId.ToString(), Pattern))
			{
				continue;
			}
			int32 Sideways = 0;
			int32 Straight = 0;
			for (const FLureFightMove& Move : Pattern->Moves)
			{
				const bool bSide = FMath::Abs(Move.Side) >= Row->SideMinShare && Move.Side != 0.f;
				Sideways += bSide ? 1 : 0;
				Straight += bSide ? 0 : 1;
			}
			TestTrue(FString::Printf(TEXT("%s: at least one sideways move (%d) and one straight move (%d)"), *PatternId.ToString(), Sideways, Straight), Sideways > 0 && Straight > 0);
		}
		return true;
	}

	/** Every rod column is optional on its own: a CSV without it imports cleanly and gets the struct default, which is the shipped value. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQADataOptional, "Project.Fishing.Fight.Rod.QA.Data.EachRodColumnIsOptional", Flags)
	bool FRodQADataOptional::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Tables;
		if (!Tables.Load(*this) || !Tables.Tuning())
		{
			return false;
		}
		const FLureFishFightRow Shipped = *Tables.Tuning();
		const FLureFishFightRow Defaults;
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		RodData::Split(Tables.FightCsv, Header, Rows);
		for (const TCHAR* Column : RodData::RodColumns)
		{
			const int32 Index = Header.IndexOfByKey(FString(Column));
			if (!TestTrue(FString::Printf(TEXT("column %s in the CSV"), Column), Index != INDEX_NONE))
			{
				continue;
			}
			TArray<FString> Without = Header;
			Without.RemoveAt(Index);
			TArray<TArray<FString>> RowsWithout = Rows;
			for (TArray<FString>& Cells : RowsWithout)
			{
				if (Cells.IsValidIndex(Index))
				{
					Cells.RemoveAt(Index);
				}
			}
			TStrongObjectPtr<UDataTable> Table;
			const TArray<FString> Problems = LureFightQA::MakeTable(Table, FLureFishFightRow::StaticStruct(), RodData::Join(Without, RowsWithout), false);
			TestEqual(FString::Printf(TEXT("without %s: import problems (%s)"), Column, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
			const FLureFishFightRow* Row = RodData::DefaultRow(Table);
			if (TestNotNull(FString::Printf(TEXT("without %s: the Default row"), Column), Row))
			{
				TestTrue(FString::Printf(TEXT("without %s: the struct default (%g), which is the shipped value (%g)"), Column, RodData::Value(*Row, Column), RodData::Value(Shipped, Column)),
					RodData::Value(*Row, Column) == RodData::Value(Defaults, Column) && RodData::Value(Defaults, Column) == RodData::Value(Shipped, Column));
				FString Problem;
				const bool bValid = Row->Validate(Problem);
				TestTrue(FString::Printf(TEXT("without %s: the row validates (%s)"), Column, *Problem), bValid);
			}
		}
		return true;
	}

	/**
	 *  Bad rod cells typed into the real CSV: out-of-range numbers are refused by Validate and the component falls back to the built-in tuning
	 *  (one warning); text in a number cell imports as 0 without a problem (T004-Q2), so QA's raw scan must catch every one of those.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRodQADataBadCells, "Project.Fishing.Fight.Rod.QA.Data.BadRodCellsAreCaught", Flags)
	bool FRodQADataBadCells::RunTest(const FString& Parameters)
	{
		LureFightQA::FFightTables Tables;
		if (!Tables.Load(*this) || !Tables.Tuning())
		{
			return false;
		}
		AddExpectedMessagePlain(TEXT("using the built-in fight tuning"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		RodData::Split(Tables.FightCsv, Header, Rows);
		struct FCase { const TCHAR* Column; const TCHAR* Text; bool bText; };
		const FCase Cases[] = {
			{ TEXT("RodAimUpDeg"), TEXT("0.5"), false }, { TEXT("RodAimDownDeg"), TEXT("-5"), false }, { TEXT("RodAimSideDeg"), TEXT("0"), false },
			{ TEXT("PitchBackPressure"), TEXT("-0.3"), false }, { TEXT("PitchDipPressure"), TEXT("0.96"), false }, { TEXT("PitchDipPressure"), TEXT("1"), false },
			{ TEXT("SideMinShare"), TEXT("1.5"), false }, { TEXT("SideLeverage"), TEXT("1"), false }, { TEXT("SideTurnRate"), TEXT("-1"), false },
			{ TEXT("SideTurnPull"), TEXT("0.99"), false }, { TEXT("SideDrain"), TEXT("-0.5"), false }, { TEXT("ReelSteps"), TEXT("0"), false },
			{ TEXT("ReelSteps"), TEXT("10"), false }, { TEXT("ReelDefaultStep"), TEXT("0"), false }, { TEXT("ReelDefaultStep"), TEXT("4"), false },
			{ TEXT("ReelSpeedMin"), TEXT("0.01"), false }, { TEXT("ReelSpeedMax"), TEXT("0.4"), false }, { TEXT("ReelLoadPerSpeed"), TEXT("-1"), false },
			{ TEXT("CameraFollowTime"), TEXT("-1"), false }, { TEXT("CameraRodYawShare"), TEXT("2"), false }, { TEXT("CameraRodPitchShare"), TEXT("-0.2"), false },
			{ TEXT("RodAimLookPitchDeg"), TEXT("-20"), false }, { TEXT("RodAimLookYawDeg"), TEXT("-25"), false }, { TEXT("RodAimBlendTime"), TEXT("-0.1"), false },
			// Text in a number cell: what the engine does with it decides whether only QA's raw scan can see it.
			{ TEXT("SideDrain"), TEXT("fast"), true }, { TEXT("PitchBackPressure"), TEXT("0.3x"), true }, { TEXT("RodAimSideDeg"), TEXT("45deg"), true },
			{ TEXT("ReelSteps"), TEXT("three"), true }, { TEXT("ReelSpeedMax"), TEXT(""), true },
		};
		int32 SilentOnlyQaCatches = 0;
		TArray<FString> Silent;
		for (const FCase& Case : Cases)
		{
			const int32 Index = Header.IndexOfByKey(FString(Case.Column));
			if (!TestTrue(FString::Printf(TEXT("column %s in the CSV"), Case.Column), Index != INDEX_NONE) || Rows.Num() == 0 || !Rows[0].IsValidIndex(Index))
			{
				continue;
			}
			TArray<TArray<FString>> Bad = Rows;
			Bad[0][Index] = Case.Text;
			const FString Csv = RodData::Join(Header, Bad);
			TStrongObjectPtr<UDataTable> Table;
			const TArray<FString> Problems = LureFightQA::MakeTable(Table, FLureFishFightRow::StaticStruct(), Csv, false);
			const FLureFishFightRow* Row = RodData::DefaultRow(Table);
			FString Problem;
			const bool bValid = Row && Row->Validate(Problem);
			const bool bRawScan = RodData::NonNumericRodCells(Csv).Num() > 0;
			const FString Label = FString::Printf(TEXT("%s = '%s'"), Case.Column, Case.Text);
			if (!Case.bText)
			{
				TestFalse(Label + TEXT(": Validate refuses the row"), bValid);
				// The component with this table: the built-in tuning (the bad value never reaches a fight).
				TStrongObjectPtr<ULureFishingComponent> Component(NewObject<ULureFishingComponent>(GetTransientPackage()));
				Component->SetFightTables(Tables.Gear.Get(), Tables.Patterns.Get(), Table.Get());
				const FLureFishFightRow& Used = Component->GetFightTuning();
				TestTrue(Label + TEXT(": the component falls back to the built-in tuning"), RodData::Value(Used, Case.Column) == RodData::Value(FLureFishFightRow::GetFallbackRow(), Case.Column));
			}
			else
			{
				TestTrue(Label + TEXT(": QA's raw scan flags it"), bRawScan);
				if (bValid && Problems.Num() == 0)
				{
					++SilentOnlyQaCatches;
					Silent.Add(FString::Printf(TEXT("%s imports as %g"), *Label, Row ? RodData::Value(*Row, Case.Column) : 0.0));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("text cells the import and Validate both accept silently (only the raw scan sees them): %d: %s"), SilentOnlyQaCatches, *FString::Join(Silent, TEXT("; "))));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
